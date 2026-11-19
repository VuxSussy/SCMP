#ifndef SCMP_MUSIC_SESSION_H
#define SCMP_MUSIC_SESSION_H

#include "Player/LocalPlayer.h"
#include "Player/SessionState.h"

namespace scmp
{
class MusicSession
{
  public:
    void open(const std::string &path);
    void close();
    void restore(const SessionState &state);
    void remember(SessionState &state) const;
    bool loaded() const { return m_player != nullptr; }
    void select(int offset);
    void filter(const std::string &query);
    void select_playing();
    void play_selected();
    void play_neighbor(int offset);
    void advance();
    void toggle_pause();

    LocalPlayer *player() const { return m_player.get(); }
    const std::string &directory() const { return m_directory; }
    const std::vector<MusicTrack> &tracks() const { return m_tracks; }
    const std::vector<size_t> &visible_tracks() const { return m_visible; }
    const std::string &query() const { return m_query; }
    const TrackDetails &details() const { return m_details; }
    const std::string &now_playing() const { return m_now_playing; }
    size_t selected() const { return m_selected; }
    size_t selected_row() const;
    size_t active() const { return m_active; }
    const std::string &error() const { return m_error; }
    void set_error(const std::string &error) { m_error = error; }

  private:
    void play(size_t track, bool follow_selection, double position = 0, bool start = true);
    void update_details();

    std::string m_directory;
    std::vector<MusicTrack> m_tracks;
    std::vector<size_t> m_visible;
    std::string m_query;
    std::unique_ptr<LocalPlayer> m_player;
    TrackDetails m_details;
    std::string m_now_playing;
    std::string m_error;
    size_t m_selected = 0;
    size_t m_active = static_cast<size_t>(-1);
};
}

#endif
