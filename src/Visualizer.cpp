#include "Visualizer.h"
#include "Source/MpdAudioSource.h"
#ifdef _OS_OSX
#include "Source/MacAudioSource.h"
#endif
#include "Domain/ScmpConstants.h"
#include "Domain/ScmpException.h"
#include "Player/MediaControls.h"
#include "Player/Spotify.h"
#include "Plugin/Plugins.h"
#include "Transformer/SpectrumTransformer.h"
#include "Transformer/SpectrumCircleTransformer.h"
#include "Transformer/EllipseTransformer.h"
#include "Transformer/LorenzTransformer.h"
#include "Transformer/LavaTransformer.h"
#include "Writer/SettingsMenu.h"
#include "Writer/StudioView.h"
#include "Writer/TerminalText.h"
#include "Utils/ConfigurationUtils.h"
#include "Utils/Utils.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>

namespace
{
using AudioStart = std::pair<std::unique_ptr<scmp::AudioSource>, uint32_t>;

#ifdef _OS_OSX
std::future<AudioStart> start_mac_capture()
{
    std::packaged_task<AudioStart()> start([] {
        uint32_t rate = 0;
        auto audio = scmp::make_mac_audio_source(rate);
        return AudioStart{std::move(audio), rate};
    });
    auto result = start.get_future();
    // Core Audio can wait for the OS consent dialog. The task owns its result independently of the UI.
    std::thread(std::move(start)).detach();
    return result;
}
#endif

void save_folders(const scmp::MusicSession &music, const std::string &config_path)
{
    try
    {
        scmp::ConfigurationUtils::save_options(config_path, {{"music.directory", scmp::join_folders(music.folders())}});
    }
    catch (const std::exception &exception)
    {
        // Keep the library open even when the config cannot be written.
        const_cast<scmp::MusicSession &>(music).set_error(exception.what());
    }
}

// The music folders screen: each folder is a playlist. Returns true when the library changed or a
// playlist was chosen, so the caller can show the player.
bool manage_folders(scmp::MusicSession &music, const std::string &config_path,
                    const volatile std::sig_atomic_t &interrupted)
{
    using scmp::draw_text;
    size_t selected = music.playlist();
    bool changed = false;
    std::string message;
    timeout(100);
    while (!interrupted)
    {
        const auto &playlists = music.playlists();
        selected = playlists.empty() ? 0 : std::min(selected, playlists.size() - 1);
        erase();
        draw_text(stdscr, 0, 2, COLS - 4, "Music folders", A_BOLD);
        draw_text(stdscr, 1, 2, COLS - 4, "Each folder is a playlist. Changes are saved to your config.", A_DIM);
        if (playlists.empty()) draw_text(stdscr, 3, 2, COLS - 4, "No folders yet. Press a to add one.", A_NORMAL);
        for (size_t i = 0; i < playlists.size() && static_cast<int>(i) + 3 < LINES - 3; ++i)
        {
            const auto &playlist = playlists[i];
            const auto label = (i == music.playlist() ? "▶ " : "  ") + playlist.name + "  (" +
                               std::to_string(playlist.tracks) + (playlist.tracks == 1 ? " song)" : " songs)");
            const int row = 3 + static_cast<int>(i);
            draw_text(stdscr, row, 2, COLS / 2, label, i == selected ? A_REVERSE | A_BOLD : A_BOLD);
            if (!playlist.directory.empty()) draw_text(stdscr, row, COLS / 2 + 2, COLS / 2 - 4, playlist.directory, A_DIM);
        }
        if (!message.empty()) draw_text(stdscr, LINES - 3, 2, COLS - 4, message, A_BOLD);
        draw_text(stdscr, LINES - 1, 2, COLS - 4, "j/k select   Enter open   a add folder   d remove   Esc back", A_DIM);
        refresh();
        const int key = getch();
        if (key == 27 || key == 'q') return changed;
        if ((key == KEY_UP || key == 'k') && selected > 0) --selected;
        if ((key == KEY_DOWN || key == 'j') && selected + 1 < playlists.size()) ++selected;
        if ((key == KEY_ENTER || key == '\n' || key == '\r') && selected < playlists.size())
        {
            music.set_playlist(selected);
            return true;
        }
        if (key == 'a')
        {
            auto folder = scmp::Utils::get_home_directory() + "Music";
            if (scmp::prompt_text(folder, "Add music folder", "Enter a folder path. Subfolders are included.",
                                  interrupted) && !folder.empty())
            {
                try
                {
                    if (music.loaded()) music.add_folder(folder);
                    else music.open(folder);
                    save_folders(music, config_path);
                    selected = music.playlist();
                    changed = true;
                    message.clear();
                }
                catch (const std::exception &exception)
                {
                    message = exception.what();
                }
            }
            timeout(100);
        }
        if (key == 'd' && selected < music.folders().size())
        {
            music.remove_folder(selected);
            save_folders(music, config_path);
            changed = true;
        }
    }
    return changed;
}

void apply_layout_commands(scmp::MusicSession &music, const std::vector<scmp::StudioView::LayoutCommand> &commands)
{
    for (const auto &command : commands)
    {
        if (!music.loaded()) return;
        switch (command.command)
        {
        case SCMP_COMMAND_PLAY_SELECTED: music.play_selected(); break;
        case SCMP_COMMAND_TOGGLE_PAUSE: music.toggle_pause(); break;
        case SCMP_COMMAND_NEXT: music.play_neighbor(1); break;
        case SCMP_COMMAND_PREVIOUS: music.play_neighbor(-1); break;
        case SCMP_COMMAND_SEEK: music.seek(command.value); break;
        case SCMP_COMMAND_VOLUME: music.change_volume(command.value); break;
        case SCMP_COMMAND_SHUFFLE: music.toggle_shuffle(); break;
        case SCMP_COMMAND_REPEAT: music.cycle_repeat(); break;
        case SCMP_COMMAND_SEARCH: music.filter(command.text); break;
        case SCMP_COMMAND_SELECT: music.select(static_cast<int>(command.value)); break;
        case SCMP_COMMAND_PLAYLIST:
            if (command.value >= 0) music.set_playlist(static_cast<size_t>(command.value));
            break;
        default: break;
        }
    }
}

class MouseTracking
{
  public:
    void set(bool active)
    {
        if (active == m_active)
        {
            return;
        }
        if (active)
        {
            mouseinterval(0);
            m_active = mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, nullptr) != 0;
            if (m_active)
            {
                std::fputs("\033[?1003h", stdout);
            }
        }
        else
        {
            std::fputs("\033[?1003l", stdout);
            mousemask(0, nullptr);
            m_active = false;
        }
        std::fflush(stdout);
    }

    ~MouseTracking()
    {
        set(false);
    }

  private:
    bool m_active = false;
};

void move_lava_pointer(scmp::LavaTransformer &lava, int key, const scmp::NcursesWriter &writer,
                       int stage_row, int stage_column)
{
    const int height = writer.height() - (writer.composed() ? 0 : 1);
    const int width = writer.width();
    switch (key)
    {
    case KEY_MOUSE:
    {
        MEVENT event{};
        if (getmouse(&event) == OK)
        {
            lava.move_pointer(event.y - stage_row, event.x - stage_column);
        }
        break;
    }
    case 'h':
    case KEY_LEFT: lava.move_cursor(0, -2, height, width); break;
    case 'j':
    case KEY_DOWN: lava.move_cursor(1, 0, height, width); break;
    case 'k':
    case KEY_UP: lava.move_cursor(-1, 0, height, width); break;
    case 'l':
    case KEY_RIGHT: lava.move_cursor(0, 2, height, width); break;
    }
}
}

scmp::Visualizer::Visualizer(Settings &settings, MusicSession &music)
    : m_settings(settings), m_music(music), m_configured_rate(settings.get_sampling_frequency()),
      m_low_cutoff(settings.get_low_cutoff_frequency()), m_high_cutoff(settings.get_high_cutoff_frequency())
{
}

void scmp::Visualizer::set_sample_rate(uint32_t rate)
{
    m_settings.set_sampling_frequency(rate);
    const auto high = std::min(m_high_cutoff, rate / 2);
    m_settings.set_high_cutoff_frequency(high);
    m_settings.set_low_cutoff_frequency(std::min(m_low_cutoff, high - 1));
}

void scmp::Visualizer::setup_transformers()
{
    m_transformers.clear();
    m_names.clear();
    for (const auto &name : m_settings.get_visualizers())
    {
        if (name == ScmpConstants::k_spectrum_visualizer_name)
        {
            m_transformers.emplace_back(std::make_unique<SpectrumTransformer>(&m_settings));
        }
        else if (name == ScmpConstants::k_spectrum_circle_visualizer_name)
        {
            m_transformers.emplace_back(std::make_unique<SpectrumCircleTransformer>(&m_settings));
        }
        else if (name == ScmpConstants::k_ellipse_visualizer_name)
        {
            m_transformers.emplace_back(std::make_unique<EllipseTransformer>(&m_settings));
        }
        else if (name == ScmpConstants::k_lorenz_visualizer_name)
        {
            m_transformers.emplace_back(std::make_unique<LorenzTransformer>(&m_settings));
        }
        else if (name == ScmpConstants::k_lava_visualizer_name)
        {
            m_transformers.emplace_back(std::make_unique<LavaTransformer>(&m_settings));
        }
        else
        {
            continue;
        }
        m_names.push_back(name);
    }
    if (m_transformers.empty())
    {
        throw ScmpException("No supported mode. Use scmp --settings to edit the mode list.");
    }
}

bool scmp::Visualizer::run(const std::string &config_path, std::string &mode, ViewMode &view,
                           std::string &theme, std::string &layout, const volatile std::sig_atomic_t &interrupted)
{
    NcursesWriter writer(&m_settings);
    StudioView studio(m_settings.workspace, writer);
    studio.restore_theme(theme);
    studio.restore_layout(layout);
    Plugins plugins;
    plugins.load(m_settings.workspace.plugin_directory, m_settings.workspace.enabled_plugins);
    set_sample_rate(m_configured_rate);
    setup_transformers();

    using Clock = std::chrono::steady_clock;
    const auto period = std::chrono::microseconds(1000000 / m_settings.get_fps());
    const auto rotation = std::chrono::seconds(m_settings.get_rotation_interval());
    auto last_rotation = Clock::now();
    auto next_frame = last_rotation;
    auto last_signal = Clock::time_point::min();
    std::vector<pcm_stereo_sample> pcm(m_settings.get_sample_size());
    std::vector<ScmpPluginSample> plugin_samples;

    const auto selected = std::find(m_names.begin(), m_names.end(), mode);
    size_t current = selected == m_names.end() ? 0 : static_cast<size_t>(selected - m_names.begin());
    MouseTracking mouse;
    MediaControls media;

    std::unique_ptr<AudioSource> system_audio;
    std::future<AudioStart> pending_capture;
    std::string system_source;
    std::string audio_error;
    bool tried_system_audio = false;
    bool using_local_audio = false;

    const auto apply_rate = [&](uint32_t rate) {
        last_signal = Clock::time_point::min();
        if (rate != m_settings.get_sampling_frequency())
        {
            set_sample_rate(rate);
            setup_transformers();
        }
        pcm.assign(m_settings.get_sample_size(), {});
    };

    while (!interrupted)
    {
        auto *lava = dynamic_cast<LavaTransformer *>(m_transformers[current].get());
        mouse.set(view == ViewMode::Visualizer && lava != nullptr);
        timeout(10);
        const int key = getch();
        if (key == 'q')
        {
            break;
        }
        if (key == '1' || key == '2' || key == '\t')
        {
            if (key == '1') view = ViewMode::Visualizer;
            if (key == '2') view = ViewMode::Player;
            if (key == '\t') view = view == ViewMode::Visualizer ? ViewMode::Player : ViewMode::Visualizer;
            if (lava) lava->reset_pointer();
            writer.set_surface(nullptr);
        }

        if (key == KEY_RESIZE) studio.clear_cover();
        if (key == 's' || key == 'P' || key == 'o' || key == 'i' || key == 'T' || key == '?' ||
            (key == '/' && m_music.loaded()))
        {
            mouse.set(false);
            studio.clear_cover();
            writer.set_surface(nullptr);
            if (lava) lava->reset_pointer();

            if (key == 's' && edit_settings(config_path, interrupted))
            {
                mode = m_names[current];
                theme.clear();
                return true;
            }
            if (key == 'P') plugins.menu(config_path, interrupted);
            if (key == '?') studio.show_help(interrupted);
            if (key == 'T' && studio.choose_theme(interrupted)) theme = studio.theme_choice();
            if (key == 'i')
            {
                view = ViewMode::Player;
#ifdef _OS_OSX
                import_spotify(m_music, interrupted);
#else
                m_music.set_error("Download with scmp spotify import URL --download, then press o to open the folder.");
#endif
            }
            if (key == 'o' && manage_folders(m_music, config_path, interrupted)) view = ViewMode::Player;
            if (key == '/')
            {
                auto query = m_music.query();
                if (prompt_text(query, "Filter playlist", "Search track filenames or folder names.", interrupted))
                {
                    m_music.filter(query);
                    view = ViewMode::Player;
                }
            }
            last_rotation = Clock::now();
            continue;
        }

        if (key == 'L')
        {
            if (plugins.layout())
            {
                studio.toggle_layout();
                layout = studio.layout_name();
                view = ViewMode::Player;
                writer.set_surface(nullptr);
            }
            else
            {
                m_music.set_error("No layout plugin is enabled. Press P to enable one.");
            }
        }
        // A layout plugin sees Player keys first, then scmp runs the commands it sent.
        if (view == ViewMode::Player && m_music.loaded() && key != ERR && studio.plugin_key(m_music, plugins.layout(), key))
        {
            apply_layout_commands(m_music, studio.take_commands());
            continue;
        }
        if (key == 't')
        {
            studio.cycle_theme();
            theme = studio.theme_choice();
        }
        if (key == 'v' || (key == ' ' && view == ViewMode::Visualizer && !m_music.loaded()) ||
            (view == ViewMode::Visualizer && rotation.count() > 0 && Clock::now() - last_rotation >= rotation))
        {
            if (lava) lava->reset_pointer();
            current = (current + 1) % m_transformers.size();
            last_rotation = Clock::now();
            writer.set_surface(nullptr);
        }

        for (const auto &command : media.poll())
        {
            if (!m_music.loaded()) break;
            auto *local = m_music.player();
            switch (command.kind)
            {
            case MediaCommand::Toggle: m_music.toggle_pause(); break;
            case MediaCommand::Play:
                if (!local->playing()) m_music.toggle_pause();
                break;
            case MediaCommand::Pause:
                if (local->playing()) m_music.toggle_pause();
                break;
            case MediaCommand::Next: m_music.play_neighbor(1); break;
            case MediaCommand::Previous: m_music.play_neighbor(-1); break;
            case MediaCommand::SeekTo: m_music.seek(command.position - local->elapsed()); break;
            case MediaCommand::SkipForward: m_music.seek(10); break;
            case MediaCommand::SkipBackward: m_music.seek(-10); break;
            }
        }

        if (m_music.loaded())
        {
            m_music.advance();
            const bool in_player = view == ViewMode::Player;
            switch (key)
            {
            // ncmpcpp-style keys. Space and the arrows stay as aliases.
            case 'p':
            case ' ': m_music.toggle_pause(); break;
            case '>': m_music.play_neighbor(1); break;
            case '<': m_music.play_neighbor(-1); break;
            case 'f': m_music.seek(5); break;
            case 'b': m_music.seek(-5); break;
            case '+':
            case '=': m_music.change_volume(0.05); break;
            case '-':
            case '_': m_music.change_volume(-0.05); break;
            case 'z': m_music.toggle_shuffle(); break;
            case 'r': m_music.cycle_repeat(); break;
            case KEY_LEFT:
                if (in_player || !lava) m_music.seek(-5);
                break;
            case KEY_RIGHT:
                if (in_player || !lava) m_music.seek(5);
                break;
            case 'C':
                view = ViewMode::Player;
                m_music.select_playing();
                break;
            case KEY_UP:
            case 'k':
                if (in_player) m_music.select(-1);
                break;
            case KEY_DOWN:
            case 'j':
                if (in_player) m_music.select(1);
                break;
            case KEY_ENTER:
            case '\r':
            case '\n':
                if (in_player) m_music.play_selected();
                break;
            case KEY_PPAGE:
                if (in_player) m_music.select(-10);
                break;
            case KEY_NPAGE:
                if (in_player) m_music.select(10);
                break;
            case 'g':
            case KEY_HOME:
                if (in_player) m_music.select(-static_cast<int>(m_music.selected_row()));
                break;
            case 'G':
            case KEY_END:
                if (in_player) m_music.select(static_cast<int>(m_music.visible_tracks().size()));
                break;
            case 27:
                if (in_player && !m_music.query().empty()) m_music.filter("");
                break;
            }
        }
        if (lava && view == ViewMode::Visualizer && writer.composed())
        {
            move_lava_pointer(*lava, key, writer, getbegy(writer.surface()), getbegx(writer.surface()));
        }
        if (key == 'R' && view == ViewMode::Visualizer && !using_local_audio && !pending_capture.valid())
        {
            system_audio.reset();
            tried_system_audio = false;
            audio_error.clear();
            apply_rate(m_configured_rate);
        }

        const auto now = Clock::now();
        if (now < next_frame)
        {
            continue;
        }
        next_frame = std::max(next_frame + period, now);
        writer.set_surface(nullptr);

        auto *player = m_music.player();
        const bool has_local_track = player && player->duration() > 0;
        if (has_local_track && !player->finished())
        {
            const auto &details = m_music.playing_details();
            const NowPlaying now_playing{m_music.tracks()[m_music.active()].path, details.title, details.artist,
                                         details.album, &details.cover, player->duration(), player->elapsed(),
                                         player->playing()};
            media.update(&now_playing);
            const ScmpNowPlaying plugin_track{now_playing.track.c_str(), now_playing.title.c_str(),
                                              now_playing.artist.c_str(), now_playing.album.c_str(),
                                              now_playing.duration, now_playing.elapsed, now_playing.playing ? 1 : 0};
            plugins.now_playing(&plugin_track);
        }
        else
        {
            media.update(nullptr);
            plugins.now_playing(nullptr);
        }
        if (has_local_track != using_local_audio)
        {
            using_local_audio = has_local_track;
            system_audio.reset();
            pending_capture = {};
            tried_system_audio = false;
            system_source.clear();
            audio_error.clear();
            apply_rate(using_local_audio ? LocalPlayer::sample_rate : m_configured_rate);
        }

        if (view == ViewMode::Visualizer && !using_local_audio)
        {
            if (!tried_system_audio)
            {
                tried_system_audio = true;
                try
                {
                    for (const auto &source : m_settings.get_audio_sources())
                    {
                        if (source == ScmpConstants::k_mpd_audio_source_name)
                        {
                            system_source = source;
                            system_audio = std::make_unique<MpdAudioSource>(&m_settings);
                            break;
                        }
#ifdef _OS_OSX
                        if (source == ScmpConstants::k_mac_audio_source_name)
                        {
                            system_source = source;
                            pending_capture = start_mac_capture();
                            break;
                        }
#endif
                    }
                    if (system_source.empty()) audio_error = "No supported audio source. Open settings with s.";
                }
                catch (const std::exception &exception)
                {
                    audio_error = exception.what();
                }
            }
            if (pending_capture.valid() && pending_capture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                try
                {
                    auto result = pending_capture.get();
                    system_audio = std::move(result.first);
                    apply_rate(result.second);
                }
                catch (const std::exception &exception)
                {
                    audio_error = exception.what();
                }
            }
        }

        const auto plugin_name = plugins.visualizer_name();
        const auto &name = plugin_name.empty() ? m_names[current] : plugin_name;
        const bool playing = has_local_track && player->playing();
        const bool system_is_mac = system_source == ScmpConstants::k_mac_audio_source_name;
        const auto input = using_local_audio ? "Local track" : system_is_mac ? "Mac system audio" :
            system_source.empty() ? "System audio" : "MPD FIFO";

        const bool full_view = view == ViewMode::Visualizer;
        auto *source = using_local_audio ? static_cast<AudioSource *>(player) :
                       full_view ? system_audio.get() : nullptr;
        const bool frame_available = source && source->read(pcm.data(), static_cast<uint32_t>(pcm.size()));
        const bool audible = frame_available && std::any_of(pcm.begin(), pcm.end(), [](pcm_stereo_sample frame) {
            return frame.l != 0 || frame.r != 0;
        });
        if (audible) last_signal = now;
        const bool signal_present = source && last_signal != Clock::time_point::min() &&
            now - last_signal < std::max(period * 2, std::chrono::microseconds(220000)) &&
            (!using_local_audio || playing);
        if (!frame_available && !signal_present)
        {
            std::fill(pcm.begin(), pcm.end(), pcm_stereo_sample{});
        }

        std::string status;
        const char *signal = "Ready";
        if (using_local_audio || full_view)
        {
            if (signal_present)
            {
                signal = "Signal";
            }
            else if (using_local_audio)
            {
                signal = playing ? "Quiet" : player->finished() ? "Ended" : "Paused";
            }
            else if (pending_capture.valid())
            {
                signal = "Opening";
                status = "Opening Mac audio. Allow System Audio Recording if prompted.";
            }
            else if (!audio_error.empty())
            {
                signal = "Audio error";
                status = audio_error;
            }
            else
            {
                signal = "Waiting";
                status = system_is_mac ? "Play sound on your Mac. Check System Audio Recording if there is no signal."
                                       : "Waiting for PCM audio on the MPD FIFO.";
            }
        }
        auto *surface = studio.begin_frame(m_music, view, name, input, status, plugins.layout());
        apply_layout_commands(m_music, studio.take_commands());
        if (surface)
        {
            ScmpPluginFrame frame{};
            if (plugins.active())
            {
                plugin_samples.resize(pcm.size());
                std::transform(pcm.begin(), pcm.end(), plugin_samples.begin(), [](pcm_stereo_sample sample) {
                    return ScmpPluginSample{sample.l, sample.r};
                });
                frame = {plugin_samples.data(), static_cast<uint32_t>(plugin_samples.size()),
                         m_settings.get_sampling_frequency(),
                         std::chrono::duration<double>(now.time_since_epoch()).count(),
                         using_local_audio ? player->elapsed() : 0,
                         m_music.now_playing().c_str(), m_names[current].c_str(),
                         using_local_audio ? playing : signal_present};
                plugins.draw_background(frame, surface, writer);
            }
            auto *graph = studio.visualizer_surface();
            writer.set_surface(graph);
            if (!plugin_name.empty())
            {
                plugins.draw_visualizer(frame, graph, writer);
            }
            else
            {
                auto *transformer = m_transformers[current].get();
                if (signal_present || dynamic_cast<LavaTransformer *>(transformer))
                {
                    if (m_settings.is_stereo_enabled()) transformer->execute_stereo(pcm.data(), &writer);
                    else transformer->execute_mono(pcm.data(), &writer);
                }
            }
            plugins.draw_overlay(frame, surface, writer);
            studio.draw_visualizer_frame(name, signal, signal_present);
        }
        studio.finish_frame();
    }
    mode = m_names[current];
    return false;
}
