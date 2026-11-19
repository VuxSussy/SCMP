#include "Player/MusicSession.h"
#include "Player/SystemVolume.h"
#include "Domain/ScmpException.h"
#include "Utils/Utils.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <numeric>
#include <random>
#include <thread>

namespace scmp
{
class TagLoader
{
  public:
    explicit TagLoader(const std::vector<MusicTrack> &tracks) : m_state(std::make_shared<State>())
    {
        m_state->tags.resize(tracks.size());
        m_state->ready.resize(tracks.size(), 0);
        auto state = m_state;
        // The worker owns its own copy of the tracks and outlives the session if it must.
        std::thread([state, tracks] {
            for (size_t i = 0; i < tracks.size() && !state->cancel.load(std::memory_order_relaxed); ++i)
            {
                TrackTags tags;
                try
                {
                    tags = load_track_tags(tracks[i]);
                }
                catch (...)
                {
                }
                std::lock_guard<std::mutex> guard(state->lock);
                state->tags[i] = std::move(tags);
                state->ready[i] = 1;
            }
        }).detach();
    }

    ~TagLoader() { m_state->cancel.store(true, std::memory_order_relaxed); }

    bool get(size_t track, TrackTags &tags) const
    {
        std::lock_guard<std::mutex> guard(m_state->lock);
        if (track >= m_state->ready.size() || !m_state->ready[track]) return false;
        tags = m_state->tags[track];
        return true;
    }

  private:
    struct State
    {
        std::mutex lock;
        std::vector<TrackTags> tags;
        std::vector<char> ready;
        std::atomic<bool> cancel{false};
    };
    std::shared_ptr<State> m_state;
};
}

scmp::MusicSession::MusicSession() = default;
scmp::MusicSession::~MusicSession() = default;
scmp::MusicSession::MusicSession(MusicSession &&) noexcept = default;
scmp::MusicSession &scmp::MusicSession::operator=(MusicSession &&) noexcept = default;

void scmp::MusicSession::open_folders(const std::vector<std::string> &paths)
{
    std::vector<std::string> folders;
    std::vector<MusicTrack> tracks;
    std::string failure;
    for (const auto &path : paths)
    {
        auto directory = expand_path(path);
        std::unique_ptr<char, decltype(&std::free)> resolved(realpath(directory.c_str(), nullptr), &std::free);
        if (resolved) directory = resolved.get();
        if (std::find(folders.begin(), folders.end(), directory) != folders.end()) continue;
        try
        {
            auto found = scan_music_directory(directory);
            for (auto &track : found)
            {
                // A folder inside another one lists each file once, under the first folder.
                if (std::any_of(tracks.begin(), tracks.end(), [&](const MusicTrack &known) { return known.path == track.path; }))
                    continue;
                track.root = folders.size();
                tracks.push_back(std::move(track));
            }
            folders.push_back(directory);
        }
        catch (const std::exception &exception)
        {
            if (failure.empty()) failure = exception.what();
        }
    }
    if (folders.empty() && !failure.empty()) throw ScmpException("%s", failure.c_str());

    const bool preserve = m_active < m_tracks.size();
    const auto active = preserve ? std::find_if(tracks.begin(), tracks.end(), [&](const MusicTrack &track) {
        return track.path == m_tracks[m_active].path;
    }) : tracks.end();
    if (active == tracks.end())
    {
        m_player = make_local_player();
        m_player->set_volume(player_gain());
        m_active = m_playing_track = static_cast<size_t>(-1);
        m_selected = 0;
        m_now_playing.clear();
        m_playing_details = {};
    }
    else
    {
        m_active = m_selected = m_playing_track = static_cast<size_t>(active - tracks.begin());
    }
    m_folders = std::move(folders);
    m_tracks = std::move(tracks);
    m_playlists.clear();
    for (size_t i = 0; i < m_folders.size(); ++i)
    {
        const auto count = static_cast<size_t>(std::count_if(m_tracks.begin(), m_tracks.end(),
            [&](const MusicTrack &track) { return track.root == i; }));
        m_playlists.push_back({m_folders[i].substr(m_folders[i].find_last_of('/') + 1), m_folders[i], count});
    }
    if (m_folders.size() > 1) m_playlists.push_back({"All music", "", m_tracks.size()});
    m_playlist = std::min(m_playlist, m_playlists.empty() ? 0 : m_playlists.size() - 1);
    m_scope = std::min(m_scope, m_playlists.empty() ? 0 : m_playlists.size() - 1);
    m_tags = std::make_unique<TagLoader>(m_tracks);
    m_error = failure;
    reset_order();
    filter("");
}

void scmp::MusicSession::add_folder(const std::string &path)
{
    auto folders = m_folders;
    folders.push_back(path);
    open_folders(folders);
    // Show the new folder: it is the last folder playlist.
    auto directory = expand_path(path);
    std::unique_ptr<char, decltype(&std::free)> resolved(realpath(directory.c_str(), nullptr), &std::free);
    if (resolved) directory = resolved.get();
    const auto found = std::find(m_folders.begin(), m_folders.end(), directory);
    if (found != m_folders.end()) set_playlist(static_cast<size_t>(found - m_folders.begin()));
}

void scmp::MusicSession::remove_folder(size_t index)
{
    if (index >= m_folders.size()) return;
    auto folders = m_folders;
    folders.erase(folders.begin() + static_cast<std::ptrdiff_t>(index));
    if (folders.empty())
    {
        const auto volume = m_volume;
        close();
        m_volume = volume;
        return;
    }
    m_playlist = 0;
    open_folders(folders);
}

void scmp::MusicSession::set_playlist(size_t index)
{
    if (index >= m_playlists.size()) return;
    m_playlist = index;
    filter("");
}

std::string scmp::MusicSession::playlist_name() const
{
    return m_playlist < m_playlists.size() ? m_playlists[m_playlist].name : "";
}

const std::string &scmp::MusicSession::directory() const
{
    static const std::string none;
    if (m_folders.empty()) return none;
    return m_playlist < m_folders.size() ? m_folders[m_playlist] : m_folders.front();
}

bool scmp::MusicSession::in_playlist(size_t track, size_t playlist) const
{
    // The last playlist is "All music" when there are several folders.
    return track < m_tracks.size() && (playlist >= m_folders.size() || m_tracks[track].root == playlist);
}

void scmp::MusicSession::close()
{
    *this = MusicSession{};
}

scmp::TrackTags scmp::MusicSession::tags(size_t track) const
{
    TrackTags tags;
    if (track >= m_tracks.size()) return tags;
    if (!m_tags || !m_tags->get(track, tags) || tags.title.empty())
    {
        tags.title = m_tracks[track].title;
    }
    return tags;
}

void scmp::MusicSession::update_details()
{
    m_details = m_selected < m_tracks.size() ? load_track_details(m_tracks[m_selected]) : TrackDetails{};
}

void scmp::MusicSession::select(int offset)
{
    if (m_visible.empty())
    {
        return;
    }
    const auto row = static_cast<size_t>(std::max<int64_t>(0, std::min<int64_t>(
        m_visible.size() - 1, static_cast<int64_t>(selected_row()) + offset)));
    const auto next = m_visible[row];
    if (next != m_selected)
    {
        m_selected = next;
        update_details();
    }
}

size_t scmp::MusicSession::selected_row() const
{
    return static_cast<size_t>(std::lower_bound(m_visible.begin(), m_visible.end(), m_selected) - m_visible.begin());
}

void scmp::MusicSession::filter(const std::string &query)
{
    m_query = query;
    const auto needle = Utils::lowercase(m_query);
    m_visible.clear();
    for (size_t i = 0; i < m_tracks.size(); ++i)
    {
        const auto &track = m_tracks[i];
        const auto tag = needle.empty() ? TrackTags{} : tags(i);
        const auto haystack = track.folder + "/" + track.title + "\n" + tag.title + "\n" + tag.artist;
        if (in_playlist(i, m_playlist) &&
            (needle.empty() || Utils::lowercase(haystack).find(needle) != std::string::npos))
        {
            m_visible.push_back(i);
        }
    }
    if (m_visible.empty())
    {
        m_selected = m_tracks.size();
    }
    else if (!std::binary_search(m_visible.begin(), m_visible.end(), m_selected))
    {
        m_selected = m_visible.front();
    }
    update_details();
}

void scmp::MusicSession::select_playing()
{
    if (m_active >= m_tracks.size())
    {
        return;
    }
    m_selected = m_active;
    if (!std::binary_search(m_visible.begin(), m_visible.end(), m_active))
    {
        filter("");
    }
    else
    {
        update_details();
    }
}

void scmp::MusicSession::play(size_t track, bool follow_selection, double position, bool start)
{
    if (track >= m_tracks.size())
    {
        return;
    }
    try
    {
        m_player->play(m_tracks[track].path, position, start);
        m_active = track;
        if (follow_selection && m_selected != track &&
            std::binary_search(m_visible.begin(), m_visible.end(), track))
        {
            m_selected = track;
            update_details();
        }
        if (track != m_playing_track)
        {
            m_playing_details = m_selected == track ? m_details : load_track_details(m_tracks[track]);
            m_playing_track = track;
        }
        m_now_playing = m_playing_details.title;
        m_error.clear();
    }
    catch (const std::exception &exception)
    {
        m_error = exception.what();
    }
}

void scmp::MusicSession::play_selected()
{
    m_scope = m_playlist;
    play(m_selected, false);
}

void scmp::MusicSession::reset_order()
{
    m_order.resize(m_tracks.size());
    std::iota(m_order.begin(), m_order.end(), size_t{0});
    if (m_shuffle && !m_order.empty())
    {
        std::shuffle(m_order.begin(), m_order.end(), std::mt19937(std::random_device{}()));
        // Keep the current track first so the shuffled queue continues from it.
        const auto current = std::find(m_order.begin(), m_order.end(), m_active);
        if (current != m_order.end()) std::iter_swap(m_order.begin(), current);
    }
}

size_t scmp::MusicSession::neighbor(size_t track, int offset, bool wrap) const
{
    const auto none = static_cast<size_t>(-1);
    if (m_order.empty()) return none;
    const auto found = std::find(m_order.begin(), m_order.end(), track);
    if (found == m_order.end()) return none;
    const auto count = static_cast<int64_t>(m_order.size());
    const auto step = offset < 0 ? -1 : 1;
    auto remaining = std::abs(offset);
    auto next = static_cast<int64_t>(found - m_order.begin());
    // Walk the play order, counting only tracks in the playlist the music was started from.
    for (int64_t visited = 0; remaining > 0 && visited < count; ++visited)
    {
        next += step;
        if (next < 0 || next >= count)
        {
            if (!wrap) return none;
            next = (next % count + count) % count;
        }
        if (in_playlist(m_order[static_cast<size_t>(next)], m_scope)) --remaining;
    }
    return remaining == 0 ? m_order[static_cast<size_t>(next)] : none;
}

void scmp::MusicSession::play_neighbor(int offset)
{
    if (m_active >= m_tracks.size())
    {
        play_selected();
        return;
    }
    const auto next = neighbor(m_active, offset, m_repeat != RepeatMode::Off);
    if (next < m_tracks.size())
    {
        play(next, m_selected == m_active);
    }
}

void scmp::MusicSession::advance()
{
    if (m_active >= m_tracks.size() || !m_player->finished())
    {
        return;
    }
    if (m_repeat == RepeatMode::One)
    {
        play(m_active, false);
        return;
    }
    const auto next = neighbor(m_active, 1, m_repeat == RepeatMode::All);
    if (next < m_tracks.size())
    {
        play(next, m_selected == m_active);
    }
    else
    {
        m_active = static_cast<size_t>(-1);
    }
}

void scmp::MusicSession::toggle_pause()
{
    if (m_active < m_tracks.size() && !m_player->finished())
    {
        m_player->toggle_pause();
    }
    else
    {
        play_selected();
    }
}

void scmp::MusicSession::seek(double seconds)
{
    if (m_active >= m_tracks.size() || !m_player || m_player->duration() <= 0)
    {
        return;
    }
    const auto length = m_player->duration();
    const auto target = std::max(0.0, std::min(length - 0.5, m_player->elapsed() + seconds));
    const bool resume = m_player->playing() || m_player->finished();
    play(m_active, false, target, resume);
}

void scmp::MusicSession::change_volume(double delta)
{
    const auto step = [&](double from) { return std::max(0.0, std::min(1.0, std::round((from + delta) * 20) / 20)); };
    const auto system = system_volume();
    if (system >= 0 && set_system_volume(step(system)))
    {
        if (m_player) m_player->set_volume(1.0);
        return;
    }
    m_volume = step(m_volume);
    if (m_player) m_player->set_volume(m_volume);
}

double scmp::MusicSession::volume() const
{
    const auto system = system_volume();
    return system >= 0 ? system : m_volume;
}

double scmp::MusicSession::player_gain() const
{
    // The device volume already scales the output, so the player plays at full gain.
    return system_volume() >= 0 ? 1.0 : m_volume;
}

void scmp::MusicSession::toggle_shuffle()
{
    m_shuffle = !m_shuffle;
    reset_order();
}

void scmp::MusicSession::cycle_repeat()
{
    m_repeat = m_repeat == RepeatMode::Off ? RepeatMode::All :
               m_repeat == RepeatMode::All ? RepeatMode::One : RepeatMode::Off;
}

void scmp::MusicSession::restore(const SessionState &state)
{
    const auto find = [&](const std::string &path) {
        return static_cast<size_t>(std::find_if(m_tracks.begin(), m_tracks.end(), [&](const MusicTrack &track) {
            return track.path == path;
        }) - m_tracks.begin());
    };
    m_volume = std::max(0.0, std::min(1.0, state.volume));
    m_playlist = m_scope = std::min(state.playlist, m_playlists.empty() ? 0 : m_playlists.size() - 1);
    // A saved level only applies to the player's gain; the system volume is left as the user set it.
    if (m_player) m_player->set_volume(player_gain());
    m_repeat = state.repeat == 1 ? RepeatMode::All : state.repeat == 2 ? RepeatMode::One : RepeatMode::Off;
    m_selected = find(state.selection);
    filter(state.filter);
    play(find(state.track), false, state.elapsed, state.playing);
    m_shuffle = state.shuffle;
    reset_order();
}

void scmp::MusicSession::remember(SessionState &state) const
{
    state.folder = join_folders(m_folders);
    state.playlist = m_playlist;
    state.selection = m_selected < m_tracks.size() ? m_tracks[m_selected].path : "";
    state.filter = m_query;
    const bool active = m_active < m_tracks.size() && !m_player->finished();
    state.track = active ? m_tracks[m_active].path : "";
    state.elapsed = active ? m_player->elapsed() : 0;
    state.playing = active && m_player->playing();
    state.volume = volume();
    state.shuffle = m_shuffle;
    state.repeat = static_cast<int>(m_repeat);
}
