#include "Player/MusicSession.h"
#include "Domain/ScmpException.h"
#include "Utils/Utils.h"

#include <algorithm>
#include <cstdlib>

void scmp::MusicSession::open(const std::string &path)
{
#ifdef _OS_OSX
    auto directory = expand_path(path);
    std::unique_ptr<char, decltype(&std::free)> resolved(realpath(directory.c_str(), nullptr), &std::free);
    if (resolved) directory = resolved.get();
    auto tracks = scan_music_directory(directory);
    const bool preserve = m_active < m_tracks.size();
    const auto active = preserve ? std::find_if(tracks.begin(), tracks.end(), [&](const MusicTrack &track) {
        return track.path == m_tracks[m_active].path;
    }) : tracks.end();
    if (active == tracks.end())
    {
        m_player = make_local_player();
        m_active = static_cast<size_t>(-1);
        m_selected = 0;
        m_now_playing.clear();
    }
    else
    {
        m_active = m_selected = static_cast<size_t>(active - tracks.begin());
    }
    m_directory = std::move(directory);
    m_tracks = std::move(tracks);
    m_error.clear();
    filter("");
#else
    static_cast<void>(path);
    throw ScmpException("Playing local files is available on macOS.");
#endif
}

void scmp::MusicSession::close()
{
    *this = MusicSession{};
}

void scmp::MusicSession::update_details()
{
#ifdef _OS_OSX
    m_details = m_selected < m_tracks.size() ? load_track_details(m_tracks[m_selected]) : TrackDetails{};
#endif
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
        if (needle.empty() || Utils::lowercase(track.folder + "/" + track.title).find(needle) != std::string::npos)
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
#ifdef _OS_OSX
        m_now_playing = m_selected == track ? m_details.title : load_track_details(m_tracks[track]).title;
#endif
        m_error.clear();
    }
    catch (const std::exception &exception)
    {
        m_error = exception.what();
    }
}

void scmp::MusicSession::play_selected()
{
    play(m_selected, false);
}

void scmp::MusicSession::play_neighbor(int offset)
{
    if (m_active >= m_tracks.size())
    {
        play_selected();
        return;
    }
    const auto next = static_cast<int64_t>(m_active) + offset;
    if (next >= 0)
    {
        play(static_cast<size_t>(next), m_selected == m_active);
    }
}

void scmp::MusicSession::advance()
{
    if (m_active < m_tracks.size() && m_player->finished())
    {
        const auto before = m_active;
        play_neighbor(1);
        if (m_active == before)
        {
            m_active = static_cast<size_t>(-1);
        }
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

void scmp::MusicSession::restore(const SessionState &state)
{
    const auto find = [&](const std::string &path) {
        return static_cast<size_t>(std::find_if(m_tracks.begin(), m_tracks.end(), [&](const MusicTrack &track) {
            return track.path == path;
        }) - m_tracks.begin());
    };
    m_selected = find(state.selection);
    filter(state.filter);
    play(find(state.track), false, state.elapsed, state.playing);
}

void scmp::MusicSession::remember(SessionState &state) const
{
    state.folder = m_directory;
    state.selection = m_selected < m_tracks.size() ? m_tracks[m_selected].path : "";
    state.filter = m_query;
    const bool active = m_active < m_tracks.size() && !m_player->finished();
    state.track = active ? m_tracks[m_active].path : "";
    state.elapsed = active ? m_player->elapsed() : 0;
    state.playing = active && m_player->playing();
}
