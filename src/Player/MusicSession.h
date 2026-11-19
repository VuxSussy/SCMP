#ifndef SCMP_MUSIC_SESSION_H
#define SCMP_MUSIC_SESSION_H

#include "Player/LocalPlayer.h"
#include "Player/SessionState.h"

#include <memory>

namespace scmp
{
enum class RepeatMode
{
    Off,
    All,
    One
};

class TagLoader;

class MusicSession
{
  public:
    MusicSession();
    ~MusicSession();
    MusicSession(MusicSession &&) noexcept;
    MusicSession &operator=(MusicSession &&) noexcept;

    void open(const std::string &path) { open_folders({path}); }
    // Opens several folders. Each one becomes a playlist, plus "All music" when there are two or more.
    void open_folders(const std::vector<std::string> &paths);
    void add_folder(const std::string &path);
    void remove_folder(size_t index);
    const std::vector<std::string> &folders() const { return m_folders; }
    const std::vector<Playlist> &playlists() const { return m_playlists; }
    size_t playlist() const { return m_playlist; }
    void set_playlist(size_t index);
    std::string playlist_name() const;
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
    void seek(double seconds);
    void change_volume(double delta);
    double volume() const { return m_volume; }
    void toggle_shuffle();
    bool shuffle() const { return m_shuffle; }
    void cycle_repeat();
    RepeatMode repeat() const { return m_repeat; }

    // Tags read in the background. Until a track is read, the title is its filename.
    TrackTags tags(size_t track) const;

    LocalPlayer *player() const { return m_player.get(); }
    // The folder of the current playlist, or the first folder for "All music".
    const std::string &directory() const;
    const std::vector<MusicTrack> &tracks() const { return m_tracks; }
    const std::vector<size_t> &visible_tracks() const { return m_visible; }
    const std::string &query() const { return m_query; }
    const TrackDetails &details() const { return m_details; }
    const TrackDetails &playing_details() const { return m_playing_details; }
    const std::string &now_playing() const { return m_now_playing; }
    size_t selected() const { return m_selected; }
    size_t selected_row() const;
    size_t active() const { return m_active; }
    const std::string &error() const { return m_error; }
    void set_error(const std::string &error) { m_error = error; }

  private:
    void play(size_t track, bool follow_selection, double position = 0, bool start = true);
    void update_details();
    void reset_order();
    size_t neighbor(size_t track, int offset, bool wrap) const;
    bool in_playlist(size_t track, size_t playlist) const;

    std::vector<std::string> m_folders;
    std::vector<Playlist> m_playlists;
    size_t m_playlist = 0;
    // The playlist the playing track was started from; next and previous stay inside it.
    size_t m_scope = 0;
    std::vector<MusicTrack> m_tracks;
    std::vector<size_t> m_visible;
    std::vector<size_t> m_order;
    std::string m_query;
    std::unique_ptr<LocalPlayer> m_player;
    std::unique_ptr<TagLoader> m_tags;
    TrackDetails m_details;
    TrackDetails m_playing_details;
    std::string m_now_playing;
    std::string m_error;
    size_t m_selected = 0;
    size_t m_active = static_cast<size_t>(-1);
    size_t m_playing_track = static_cast<size_t>(-1);
    double m_volume = 1.0;
    bool m_shuffle = false;
    RepeatMode m_repeat = RepeatMode::Off;
};
}

#endif
