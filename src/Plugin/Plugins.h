#ifndef SCMP_PLUGINS_H
#define SCMP_PLUGINS_H

#include "Writer/NcursesWriter.h"
#include <csignal>
#include <memory>
#include <scmp/plugin.h>

namespace scmp
{
class Plugins
{
  public:
    void load(const std::string &folder, const std::string &enabled);
    void menu(const std::string &config_path, const volatile std::sig_atomic_t &interrupted);
    bool active() const;
    std::string visualizer_name() const;
    // The first enabled plugin that draws a Player layout, or nullptr.
    const ScmpPluginV2 *layout() const;
    void draw_background(const ScmpPluginFrame &frame, WINDOW *surface, NcursesWriter &writer) const;
    void draw_visualizer(const ScmpPluginFrame &frame, WINDOW *surface, NcursesWriter &writer) const;
    void draw_overlay(const ScmpPluginFrame &frame, WINDOW *surface, NcursesWriter &writer) const;
    // Passes the local player's track (nullptr when none) to plugins that export scmp_plugin_now_playing.
    void now_playing(const ScmpNowPlaying *now) const;

  private:
    struct Close
    {
        void operator()(void *handle) const;
    };
    struct Entry
    {
        std::string file;
        std::string error;
        bool enabled = false;
        std::unique_ptr<void, Close> handle;
        const ScmpPluginV1 *api = nullptr;
        // Version 2 plugins: their layers are copied into v1 so the drawing code serves both.
        const ScmpPluginV2 *api2 = nullptr;
        std::unique_ptr<ScmpPluginV1> layers;
        decltype(&scmp_plugin_now_playing) now_playing = nullptr;
    };

    void draw(const ScmpPluginFrame &frame, WINDOW *surface, NcursesWriter &writer, ScmpPluginDraw ScmpPluginV1::*layer,
              bool first_only) const;

    std::string m_folder;
    std::vector<Entry> m_entries;
};
}

#endif
