// Local playback for Linux and other non-macOS systems.
// FFmpeg decodes every format to 48 kHz stereo PCM, and a PulseAudio, PipeWire or ALSA
// command-line client plays it. SCMP_AUDIO_SINK overrides the player command.
#ifndef _OS_OSX

#include "Player/LocalPlayer.h"
#include "Domain/ScmpException.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <spawn.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char **environ;

namespace
{
bool on_path(const std::string &program)
{
    const char *path = std::getenv("PATH");
    if (!path) return false;
    std::stringstream folders(path);
    std::string folder;
    while (std::getline(folders, folder, ':'))
    {
        if (!folder.empty() && access((folder + "/" + program).c_str(), X_OK) == 0) return true;
    }
    return false;
}

class Process
{
  public:
    // Starts argv with a pipe on stdin (write_end) or stdout (read_end). Stderr is discarded.
    Process(const std::vector<std::string> &argv, bool pipe_stdin)
    {
        int fds[2];
        if (pipe(fds) != 0) return;
        // Close-on-exec keeps other children from holding this pipe open, which would hide EOF.
        fcntl(fds[0], F_SETFD, FD_CLOEXEC);
        fcntl(fds[1], F_SETFD, FD_CLOEXEC);
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipe_stdin ? fds[0] : fds[1], pipe_stdin ? 0 : 1);
        posix_spawn_file_actions_addclose(&actions, fds[0]);
        posix_spawn_file_actions_addclose(&actions, fds[1]);
        posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
        if (pipe_stdin) posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
        else posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
        std::vector<char *> args;
        for (const auto &arg : argv) args.push_back(const_cast<char *>(arg.c_str()));
        args.push_back(nullptr);
        if (posix_spawnp(&m_pid, args[0], &actions, nullptr, args.data(), environ) != 0) m_pid = -1;
        posix_spawn_file_actions_destroy(&actions);
        close(pipe_stdin ? fds[0] : fds[1]);
        m_fd = pipe_stdin ? fds[1] : fds[0];
        if (m_pid < 0)
        {
            close(m_fd);
            m_fd = -1;
        }
    }

    ~Process() { stop(); }
    Process(const Process &) = delete;
    Process &operator=(const Process &) = delete;

    bool running() const { return m_pid > 0; }
    int fd() const { return m_fd; }

    void stop()
    {
        if (m_fd >= 0) close(m_fd);
        m_fd = -1;
        if (m_pid > 0)
        {
            // SIGKILL: FFmpeg finishes writing its output before it honors SIGTERM.
            kill(m_pid, SIGKILL);
            waitpid(m_pid, nullptr, 0);
        }
        m_pid = -1;
    }

  private:
    pid_t m_pid = -1;
    int m_fd = -1;
};

// Runs a command and returns its standard output.
std::string capture(const std::vector<std::string> &argv, size_t limit = 64 << 20)
{
    Process process(argv, false);
    std::string output;
    if (!process.running()) return output;
    char buffer[65536];
    ssize_t count;
    while ((count = read(process.fd(), buffer, sizeof(buffer))) > 0 && output.size() < limit)
    {
        output.append(buffer, static_cast<size_t>(count));
    }
    return output;
}

std::vector<std::string> sink_command()
{
    const auto rate = std::to_string(scmp::LocalPlayer::sample_rate);
    if (const char *custom = std::getenv("SCMP_AUDIO_SINK"))
    {
        std::vector<std::string> words;
        std::stringstream stream(custom);
        std::string word;
        while (stream >> word) words.push_back(word);
        if (!words.empty()) return words;
    }
    if (on_path("pacat"))
        return {"pacat", "--raw", "--format=s16le", "--rate=" + rate, "--channels=2", "--latency-msec=80",
                "--client-name=scmp", "--stream-name=scmp"};
    if (on_path("pw-cat"))
        return {"pw-cat", "--playback", "--format=s16", "--rate=" + rate, "--channels=2", "--latency=80ms", "-"};
    if (on_path("aplay")) return {"aplay", "-q", "-t", "raw", "-f", "S16_LE", "-r", rate, "-c", "2", "-B", "80000"};
    return {};
}

scmp::PixelImage decode_cover(const std::vector<std::string> &input)
{
    // FFmpeg scales the picture into a 256x256 BGRA frame, which matches PixelImage's 0xAARRGGBB layout.
    constexpr int side = 256;
    std::vector<std::string> argv{"ffmpeg", "-nostdin", "-v", "error"};
    argv.insert(argv.end(), input.begin(), input.end());
    argv.insert(argv.end(), {"-an", "-frames:v", "1", "-vf",
                             "scale=256:256:force_original_aspect_ratio=increase,crop=256:256",
                             "-f", "rawvideo", "-pix_fmt", "bgra", "-"});
    const auto raw = capture(argv);
    scmp::PixelImage image;
    if (raw.size() != static_cast<size_t>(side) * side * 4) return image;
    image.width = image.height = side;
    image.pixels.resize(static_cast<size_t>(side) * side);
    std::memcpy(image.pixels.data(), raw.data(), raw.size());
    return image;
}

struct Probe
{
    std::string title, artist, album;
    double duration = 0;
};

Probe probe(const std::string &path)
{
    Probe result;
    const auto output = capture({"ffprobe", "-v", "error", "-show_entries",
                                 "format=duration:format_tags=title,artist,album", "-of", "default=nw=1", path});
    std::stringstream lines(output);
    std::string line;
    while (std::getline(lines, line))
    {
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        auto key = line.substr(0, equals);
        const auto value = line.substr(equals + 1);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
        if (key == "duration") result.duration = std::atof(value.c_str());
        else if (key == "tag:title") result.title = value;
        else if (key == "tag:artist") result.artist = value;
        else if (key == "tag:album") result.album = value;
    }
    if (!std::isfinite(result.duration) || result.duration < 0) result.duration = 0;
    return result;
}

struct SampleBuffer
{
    std::mutex lock;
    std::vector<scmp::pcm_stereo_sample> frames = std::vector<scmp::pcm_stereo_sample>(scmp::LocalPlayer::sample_rate);
    size_t write = 0;
    size_t available = 0;

    void capture(const scmp::pcm_stereo_sample *data, size_t count)
    {
        std::lock_guard<std::mutex> guard(lock);
        for (size_t i = 0; i < count; ++i)
        {
            frames[write] = data[i];
            if (++write == frames.size()) write = 0;
        }
        available = std::min(frames.size(), available + count);
    }

    bool read(scmp::pcm_stereo_sample *buffer, size_t count)
    {
        std::lock_guard<std::mutex> guard(lock);
        if (available == 0) return false;
        const auto frames_read = std::min(count, available);
        const auto first = (write + frames.size() - frames_read) % frames.size();
        const auto before_wrap = std::min(frames_read, frames.size() - first);
        std::copy_n(frames.data() + first, before_wrap, buffer);
        std::copy_n(frames.data(), frames_read - before_wrap, buffer + before_wrap);
        std::fill(buffer + frames_read, buffer + count, scmp::pcm_stereo_sample{});
        available = 0;
        return true;
    }
};

class PipePlayer final : public scmp::LocalPlayer
{
  public:
    // A player that exits mid-write must not take scmp down with SIGPIPE.
    PipePlayer() { std::signal(SIGPIPE, SIG_IGN); }
    ~PipePlayer() override { stop(); }

    void play(const std::string &path, double position, bool start) override
    {
        stop();
        if (!on_path("ffmpeg")) throw scmp::ScmpException("Install FFmpeg to play local files.");
        const auto sink = sink_command();
        if (sink.empty())
        {
            throw scmp::ScmpException("Install pacat, pw-cat or aplay, or set SCMP_AUDIO_SINK, to play audio.");
        }
        const auto info = probe(path);
        m_length = info.duration;
        position = std::max(0.0, m_length > 0 ? std::min(position, m_length) : position);
        char seek[32];
        std::snprintf(seek, sizeof(seek), "%.3f", position);
        auto decoder = std::make_unique<Process>(std::vector<std::string>{
            "ffmpeg", "-nostdin", "-v", "error", "-ss", seek, "-i", path, "-vn", "-f", "s16le", "-ac", "2",
            "-ar", std::to_string(sample_rate), "-"}, false);
        auto output = std::make_unique<Process>(sink, true);
        if (!decoder->running()) throw scmp::ScmpException("Could not start FFmpeg for %s", path.c_str());
        if (!output->running()) throw scmp::ScmpException("Could not start the audio player %s", sink[0].c_str());

        m_decoder = std::move(decoder);
        m_output = std::move(output);
        m_offset = position;
        m_written = 0;
        m_finished = false;
        m_stop = false;
        m_paused = !start;
        m_samples = std::make_shared<SampleBuffer>();
        m_worker = std::thread([this] { pump(); });
    }

    void toggle_pause() override
    {
        if (!m_worker.joinable() || m_finished) return;
        {
            std::lock_guard<std::mutex> guard(m_lock);
            m_paused = !m_paused;
        }
        m_wake.notify_all();
    }

    bool playing() const override { return m_worker.joinable() && !m_paused && !m_finished; }
    bool finished() const override { return m_finished; }

    double elapsed() const override
    {
        if (m_finished) return duration();
        // Frames sit in the audio server's buffer for about the requested latency before they play.
        const double heard = m_written.load() / static_cast<double>(sample_rate) - (m_paused ? 0 : 0.08);
        const auto value = m_offset + std::max(0.0, heard);
        return m_length > 0 ? std::min(value, m_length) : value;
    }

    double duration() const override { return m_worker.joinable() || m_finished ? m_length : 0; }

    bool read(scmp::pcm_stereo_sample *buffer, uint32_t count) override
    {
        return playing() && m_samples && m_samples->read(buffer, count);
    }

    void set_volume(double volume) override { m_volume = std::max(0.0, std::min(1.0, volume)); }
    double volume() const override { return m_volume; }

  private:
    void pump()
    {
        std::vector<scmp::pcm_stereo_sample> chunk(1024);
        const size_t bytes = chunk.size() * sizeof(scmp::pcm_stereo_sample);
        while (!m_stop)
        {
            {
                std::unique_lock<std::mutex> guard(m_lock);
                m_wake.wait(guard, [this] { return !m_paused || m_stop; });
            }
            if (m_stop) break;
            size_t filled = 0;
            auto *data = reinterpret_cast<char *>(chunk.data());
            while (filled < bytes)
            {
                const auto count = ::read(m_decoder->fd(), data + filled, bytes - filled);
                if (count <= 0) break;
                filled += static_cast<size_t>(count);
            }
            const auto frames = filled / sizeof(scmp::pcm_stereo_sample);
            if (frames == 0)
            {
                // Let the audio server play what it has buffered before reporting the end.
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                m_finished = true;
                break;
            }
            const double gain = m_volume;
            for (size_t i = 0; i < frames; ++i)
            {
                chunk[i].l = static_cast<int16_t>(std::lround(chunk[i].l * gain));
                chunk[i].r = static_cast<int16_t>(std::lround(chunk[i].r * gain));
            }
            m_samples->capture(chunk.data(), frames);
            size_t sent = 0;
            const auto length = frames * sizeof(scmp::pcm_stereo_sample);
            while (sent < length && !m_stop)
            {
                const auto count = ::write(m_output->fd(), data + sent, length - sent);
                if (count < 0 && errno == EINTR) continue;
                if (count <= 0)
                {
                    m_finished = true;
                    return;
                }
                sent += static_cast<size_t>(count);
            }
            m_written += frames;
        }
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> guard(m_lock);
            m_stop = true;
        }
        m_wake.notify_all();
        // Closing the pipes unblocks a worker waiting on FFmpeg or on the audio player.
        if (m_decoder) m_decoder->stop();
        if (m_output) m_output->stop();
        if (m_worker.joinable()) m_worker.join();
        m_decoder.reset();
        m_output.reset();
    }

    std::unique_ptr<Process> m_decoder, m_output;
    std::shared_ptr<SampleBuffer> m_samples;
    std::thread m_worker;
    std::mutex m_lock;
    std::condition_variable m_wake;
    std::atomic<bool> m_stop{false}, m_paused{false}, m_finished{false};
    std::atomic<uint64_t> m_written{0};
    std::atomic<double> m_volume{1.0};
    double m_offset = 0;
    double m_length = 0;
};
}

scmp::PixelImage scmp::load_pixel_image(const std::string &path)
{
    struct stat info{};
    if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || !on_path("ffmpeg")) return {};
    return decode_cover({"-i", path});
}

scmp::TrackTags scmp::load_track_tags(const MusicTrack &track)
{
    TrackTags tags;
    if (!on_path("ffprobe")) return tags;
    const auto info = probe(track.path);
    tags.title = info.title;
    tags.artist = info.artist;
    tags.album = info.album;
    tags.duration = info.duration;
    return tags;
}

scmp::TrackDetails scmp::load_track_details(const MusicTrack &track)
{
    TrackDetails details;
    details.title = track.title;
    if (on_path("ffprobe"))
    {
        const auto info = probe(track.path);
        if (!info.title.empty()) details.title = info.title;
        details.artist = info.artist;
        details.album = info.album;
    }
    if (on_path("ffmpeg")) details.cover = decode_cover({"-i", track.path});
    if (details.cover.empty())
    {
        const auto folder = track.path.substr(0, track.path.find_last_of('/') + 1);
        for (const auto *name : {"cover", "folder", "front", "album"})
        {
            for (const auto *extension : {".jpg", ".jpeg", ".png"})
            {
                details.cover = load_pixel_image(folder + name + extension);
                if (!details.cover.empty()) return details;
            }
        }
    }
    return details;
}

std::unique_ptr<scmp::LocalPlayer> scmp::make_local_player()
{
    return std::make_unique<PipePlayer>();
}

#endif
