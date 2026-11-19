#include <clocale>
#include <csignal>
#include <iostream>
#include <sys/stat.h>

#include "Domain/ScmpException.h"
#include "Player/Spotify.h"
#include "Plugin/Plugins.h"
#include "Utils/ConfigurationUtils.h"
#include "Utils/Logger.h"
#include "Utils/Utils.h"
#include "Visualizer.h"
#include "Writer/SettingsMenu.h"
#include "Writer/Wallpaper.h"

namespace
{
volatile std::sig_atomic_t interrupted = 0;

void shutdown(int)
{
    interrupted = 1;
}

const char *help =
    "SCMP - Sussy's CLI Music Player\n\n"
    "Usage: scmp [-c FILE] [--view visualizer|player] [--music DIR] [--mode NAME] [--theme NAME]\n"
    "       scmp [-c FILE] [--settings | --plugins | --set KEY=VALUE ...]\n"
    "       scmp spotify import URL [--download] [-o DIR]\n"
    "       scmp spotify download DIR\n"
    "\n"
    "  -c FILE          Config file path (default: ~/.scmp/config)\n"
    "  --music DIR      Browse and play a folder of local tracks\n"
    "  --view NAME      Open visualizer or player\n"
    "  --mode NAME      Choose spectrum, spectrum_circle, ellipse, lorenz, or lava\n"
    "  --theme NAME     Choose a wallpaper for this session\n"
    "  --settings       Open the terminal settings editor\n"
    "  --plugins        Open the plugin manager\n"
    "  --set KEY=VALUE  Save a setting and exit (repeatable)\n"
    "  -h, --help       Show this help\n"
    "\n"
    "While running: 1 Visualizer, 2 Player, i Spotify (macOS), o folder, v effect, T themes, q quit.\n"
    "Player: Space pause, ←/→ seek, +/- volume, n/N next/prev, z shuffle, R repeat.\n";
}

int main(int argc, char *argv[])
{
    std::setlocale(LC_CTYPE, "");
    std::signal(SIGINT, shutdown);
    std::signal(SIGTERM, shutdown);

    using scmp::ConfigurationUtils;
    auto config_path = ConfigurationUtils::default_config_path();
    std::string mode;
    std::string music_path;
    std::string theme;
    auto view = scmp::ViewMode::Visualizer;
    bool view_explicit = false;
    bool settings_only = false;
    bool plugins_only = false;
    scmp::ConfigValues changes;

    try
    {
        if (argc > 1 && std::string(argv[1]) == "spotify")
        {
            return scmp::run_spotify({argv + 2, argv + argc});
        }
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg(argv[i]);
            if (arg == "-h" || arg == "--help")
            {
                std::cout << help;
                return 0;
            }
            if (arg == "--settings")
            {
                settings_only = true;
                continue;
            }
            if (arg == "--plugins")
            {
                plugins_only = true;
                continue;
            }
            if (arg != "-c" && arg != "--mode" && arg != "--set" && arg != "--music" && arg != "--theme" &&
                arg != "--view")
            {
                throw scmp::ScmpException("Unknown argument: %s. See scmp --help.", arg.c_str());
            }
            if (++i >= argc)
            {
                throw scmp::ScmpException("Missing value for %s.", arg.c_str());
            }
            const std::string value(argv[i]);
            if (arg == "-c")
            {
                config_path = value;
            }
            else if (arg == "--music")
            {
                music_path = value;
                if (music_path.empty())
                {
                    throw scmp::ScmpException("Use --music DIR with a folder path.");
                }
            }
            else if (arg == "--mode")
            {
                if (!ConfigurationUtils::is_visualizer(value))
                {
                    throw scmp::ScmpException("Unknown mode: %s.", value.c_str());
                }
                mode = value;
            }
            else if (arg == "--theme")
            {
                const auto &themes = scmp::themes();
                if (value != "auto" && std::none_of(themes.begin(), themes.end(), [&](const scmp::Theme &item) {
                        return value == item.id;
                    }))
                {
                    throw scmp::ScmpException("Unknown theme: %s. Available: %s", value.c_str(), scmp::theme_choices().c_str());
                }
                theme = value;
            }
            else if (arg == "--view")
            {
                if (value != "visualizer" && value != "player")
                {
                    throw scmp::ScmpException("Unknown view: %s. Use visualizer or player.", value.c_str());
                }
                view = value == "player" ? scmp::ViewMode::Player : scmp::ViewMode::Visualizer;
                view_explicit = true;
            }
            else
            {
                const auto separator = value.find('=');
                if (separator == std::string::npos || separator == 0)
                {
                    throw scmp::ScmpException("Use --set KEY=VALUE.");
                }
                changes[value.substr(0, separator)] = value.substr(separator + 1);
            }
        }

        const int standalone = settings_only + plugins_only + !changes.empty();
        if (standalone > 1 || (standalone != 0 && (!mode.empty() || !music_path.empty() || !theme.empty() ||
                                                view_explicit)))
        {
            throw scmp::ScmpException("Use --settings, --plugins, or --set separately from playback options.");
        }

        scmp::Logger::initialize(ScmpConstants::k_default_log_path);
        if (!changes.empty())
        {
            ConfigurationUtils::save_options(config_path, changes);
            std::cout << "Saved " << config_path << '\n';
        }
        else if (settings_only || plugins_only)
        {
            scmp::Settings settings;
            scmp::NcursesWriter writer(&settings);
            if (settings_only)
            {
                scmp::edit_settings(config_path, interrupted);
            }
            else
            {
                const auto values = ConfigurationUtils::read_config(config_path);
                scmp::Plugins plugins;
                plugins.load(scmp::Utils::get(values, std::string{"plugins.directory"}, settings.workspace.plugin_directory),
                             scmp::Utils::get(values, std::string{"plugins.enabled"}, settings.workspace.enabled_plugins));
                plugins.menu(config_path, interrupted);
            }
        }
        else
        {
            const auto state_path = config_path + ".session";
            auto state = scmp::read_session(state_path);
            std::string selected_theme, configured_theme;
            std::string layout = state.layout;
            bool first_run = true;
            std::vector<std::string> previous_modes;
            std::string previous_folder;
            scmp::MusicSession music;
            while (!interrupted)
            {
                scmp::Settings settings;
                ConfigurationUtils::load_settings(settings, config_path);
                configured_theme = settings.workspace.theme;
                if (first_run && theme.empty() && state.theme_setting == configured_theme) selected_theme = state.theme;
                if (!theme.empty())
                {
                    settings.workspace.theme = theme;
                    settings.workspace.background = "none";
                }
                auto modes = settings.get_visualizers();
                const auto &configured_folder = settings.workspace.music_directory;
                if (first_run && mode.empty() && std::find(modes.begin(), modes.end(), state.mode) != modes.end())
                    mode = state.mode;
                bool restored_library = false;
                if (first_run && music_path.empty() && !state.folder.empty() && state.library_setting == configured_folder)
                {
                    try
                    {
                        music.open(state.folder);
                        music.restore(state);
                        restored_library = true;
                    }
                    catch (const std::exception &error)
                    {
                        music.set_error(error.what());
                    }
                }
                if (first_run && !music_path.empty())
                {
                    music.open(music_path);
                }
                else if (!restored_library && configured_folder != previous_folder)
                {
                    if (configured_folder == "none")
                    {
                        music.close();
                    }
                    else
                    {
                        try
                        {
                            music.open(configured_folder);
                        }
                        catch (const std::exception &exception)
                        {
                            if (first_run) throw;
                            music.set_error(exception.what());
                        }
                    }
                }
                previous_folder = configured_folder;
                if (first_run && !view_explicit)
                {
                    if (music_path.empty() && state.view == "visualizer") view = scmp::ViewMode::Visualizer;
                    else if ((music_path.empty() && state.view == "player") || !music.tracks().empty())
                        view = scmp::ViewMode::Player;
                }
                const bool keep_mode = first_run || modes == previous_modes;
                previous_modes = modes;
                if (keep_mode && !mode.empty() && std::find(modes.begin(), modes.end(), mode) == modes.end())
                {
                    modes.push_back(mode);
                    settings.set_visualizers(modes);
                }
                first_run = false;
                scmp::Visualizer visualizer(settings, music);
                if (!visualizer.run(config_path, mode, view, selected_theme, layout, interrupted)) break;
            }
            if (!first_run)
            {
                if (config_path == ConfigurationUtils::default_config_path())
                    ::mkdir((scmp::Utils::get_home_directory() + ".scmp").c_str(), 0700);
                state.view = view == scmp::ViewMode::Player ? "player" : "visualizer";
                state.mode = mode;
                state.theme = selected_theme;
                state.layout = layout;
                state.library_setting = previous_folder;
                state.theme_setting = configured_theme;
                music.remember(state);
                if (!scmp::save_session(state_path, state))
                    std::cerr << "scmp: Could not save session to " << state_path << '\n';
            }
        }
        scmp::Logger::uninitialize();
    }
    catch (const std::exception &exception)
    {
        scmp::Logger::uninitialize();
        std::cerr << "scmp: " << exception.what() << '\n';
        return 1;
    }
    return 0;
}
