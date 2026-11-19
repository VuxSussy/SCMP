#include <fstream>
#include <cerrno>
#include <cstring>
#include <memory>
#include <sys/stat.h>
#include <string>
#include <unordered_map>

#include "Utils/ConfigurationUtils.h"
#include "Domain/ScmpException.h"
#include "Utils/Logger.h"
#include "Utils/NcursesUtils.h"
#include "Utils/Utils.h"
#include "Writer/Wallpaper.h"

namespace
{
const static std::string k_audio_sources_setting{"audio.sources"};

const static std::string k_mpd_fifo_path_setting{"mpd.fifo.path"};

const static std::string k_stereo_enabled_setting{"audio.stereo.enabled"};

const static std::string k_vis_rotation_interval{"visualizer.rotation.secs"};

const static std::string k_visualizers_setting{"visualizers"};
const static std::string k_fps_setting{"visualizer.fps"};

const static std::string k_color_scheme_path_setting{"colors.scheme"};
const static std::string k_sampling_frequency_setting{
    "audio.sampling.frequency"};
const static std::string k_low_cutoff_frequency_setting{
    "audio.low.cutoff.frequency"};
const static std::string k_high_cutoff_frequency_setting{
    "audio.high.cutoff.frequency"};

const static std::string k_ellipse_character{"visualizer.ellipse.character"};
const static std::string k_ellipse_radius{"visualizer.ellipse.radius"};
const static std::string k_lorenz_character{"visualizer.lorenz.character"};

// Spectrum settings
const static std::string k_spectrum_character{"visualizer.spectrum.character"};
const static std::string k_spectrum_bar_width{"visualizer.spectrum.bar.width"};
const static std::string k_spectrum_bar_spacing{
    "visualizer.spectrum.bar.spacing"};
const static std::string k_spectrum_smoothing_mode{
    "visualizer.spectrum.smoothing.mode"};
const static std::string k_spectrum_falloff_mode{
    "visualizer.spectrum.falloff.mode"};
const static std::string k_spectrum_falloff_weight{
    "visualizer.spectrum.falloff.weight"};

const static std::string k_monstercat_smoothing_factor{
    "visualizer.monstercat.smoothing.factor"};

const static std::string k_sgs_smoothing_points{
    "visualizer.sgs.smoothing.points"};
const static std::string k_sgs_smoothing_passes{
    "visualizer.sgs.smoothing.passes"};
}

const std::vector<scmp::ConfigOption> &scmp::ConfigurationUtils::options()
{
    using Type = ConfigOption::Type;
    static const std::vector<ConfigOption> entries{
        {"Audio", "Source", k_audio_sources_setting.c_str(), ScmpConstants::k_default_audio_sources,
         "mac = system audio (macOS 14.2+), mpd = FIFO. The first supported source is used.", Type::Sources},
        {"Audio", "Stereo", k_stereo_enabled_setting.c_str(), "true",
         "true or false. Changes how audio channels are drawn.", Type::Switch},
        {"Audio", "MPD FIFO", k_mpd_fifo_path_setting.c_str(), ScmpConstants::k_default_mpd_fifo_path,
         "Path to the MPD PCM FIFO.", Type::Text},
        {"Audio", "MPD sample rate", k_sampling_frequency_setting.c_str(),
         std::to_string(ScmpConstants::k_default_sampling_frequency),
         "Rate in Hz. Core Audio uses the Mac's native tap rate.", Type::Integer, 8000, 384000},
        {"Audio", "Low cutoff", k_low_cutoff_frequency_setting.c_str(),
         std::to_string(ScmpConstants::k_default_low_cutoff_frequency),
         "Lowest spectrum frequency in Hz.", Type::Integer, 1, 192000},
        {"Audio", "High cutoff", k_high_cutoff_frequency_setting.c_str(),
         std::to_string(ScmpConstants::k_default_high_cutoff_frequency),
         "Highest spectrum frequency in Hz, capped at half the sample rate.", Type::Integer, 1, 192000},
        {"Display", "Modes / order", k_visualizers_setting.c_str(), ScmpConstants::k_default_visualizers,
         "Comma-separated: spectrum, spectrum_circle, ellipse, lorenz, lava.", Type::Modes},
        {"Display", "Refresh rate", k_fps_setting.c_str(), std::to_string(ScmpConstants::k_default_fps),
         "Frames per second (1-240).", Type::Integer, 1, 240},
        {"Display", "Mode rotation", k_vis_rotation_interval.c_str(),
         std::to_string(ScmpConstants::k_default_visualizer_rotation_interval),
         "Seconds between modes. Set to 0 to turn off rotation.", Type::Integer, 0, 2147483647},
        {"Display", "Color scheme", k_color_scheme_path_setting.c_str(), ScmpConstants::k_default_colors_path,
         "Filename in ~/.scmp/colors. Lava uses its own palette.", Type::Text},
        {"Spectrum", "Bar character", k_spectrum_character.c_str(), " ",
         "One character. A space draws a filled block.", Type::Character},
        {"Spectrum", "Bar width", k_spectrum_bar_width.c_str(),
         std::to_string(ScmpConstants::k_default_spectrum_bar_width),
         "Columns per bar (1-200).", Type::Integer, 1, 200},
        {"Spectrum", "Bar spacing", k_spectrum_bar_spacing.c_str(),
         std::to_string(ScmpConstants::k_default_spectrum_bar_spacing),
         "Columns between bars (0-200).", Type::Integer, 0, 200},
        {"Spectrum", "Smoothing", k_spectrum_smoothing_mode.c_str(), "monstercat",
         "monstercat, sgs, none", Type::Choice},
        {"Spectrum", "Falloff", k_spectrum_falloff_mode.c_str(), "fill",
         "fill, top, none", Type::Choice},
        {"Spectrum", "Falloff weight", k_spectrum_falloff_weight.c_str(), "0.99",
         "Decay weight from 0 to 1.", Type::Decimal, 0, 1},
        {"Spectrum", "Monstercat factor", k_monstercat_smoothing_factor.c_str(), "1.5",
         "Spread of the Monstercat smoothing (1-10).", Type::Decimal, 1, 10},
        {"Spectrum", "SGS points", k_sgs_smoothing_points.c_str(),
         std::to_string(ScmpConstants::k_default_sgs_smoothing_points),
         "Odd number of points in the smoothing window (1-101).", Type::Integer, 1, 101},
        {"Spectrum", "SGS passes", k_sgs_smoothing_passes.c_str(),
         std::to_string(ScmpConstants::k_default_sgs_smoothing_passes),
         "Number of smoothing passes (1-20).", Type::Integer, 1, 20},
        {"Shapes", "Ellipse character", k_ellipse_character.c_str(), "█",
         "One character. A space draws a filled block.", Type::Character},
        {"Shapes", "Ellipse ring radius", k_ellipse_radius.c_str(),
         std::to_string(ScmpConstants::k_default_ellipse_radius),
         "Radius of each ellipse color ring (1-200).", Type::Integer, 1, 200},
        {"Shapes", "Lorenz character", k_lorenz_character.c_str(), "█",
         "One character. A space draws a filled block.", Type::Character},
        {"Library", "Music folders", "music.directory", "none",
         "Folders to open at launch, separated by commas. Each one is a playlist. Set to none for system audio.",
         Type::Text},
        {"Themes", "Pixel scene", "theme.name", "auto",
         theme_choices().c_str(), Type::Choice},
        {"Themes", "Wallpaper motion", "theme.motion", "true",
         "true or false. Static wallpapers still show a live audio visualizer.", Type::Switch},
        {"Themes", "Background image", "theme.background", "none",
         "Local image path (macOS). Set to none to use the selected pixel scene.", Type::Text},
        {"Plugins", "Plugin folder", "plugins.directory", "~/.scmp/plugins",
         "Folder containing native .dylib or .so plugins.", Type::Text},
        {"Plugins", "Enabled plugins", "plugins.enabled", "none",
         "Comma-separated filenames. Press p in scmp to manage plugins.", Type::Text},
    };
    return entries;
}

bool scmp::ConfigurationUtils::is_visualizer(const std::string &name)
{
    return name == ScmpConstants::k_spectrum_visualizer_name ||
           name == ScmpConstants::k_spectrum_circle_visualizer_name ||
           name == ScmpConstants::k_ellipse_visualizer_name ||
           name == ScmpConstants::k_lorenz_visualizer_name ||
           name == ScmpConstants::k_lava_visualizer_name;
}

namespace
{
bool valid_value(const scmp::ConfigOption &option, const std::string &value)
{
    using Type = scmp::ConfigOption::Type;
    if (value.empty() || value.find_first_of("\r\n") != std::string::npos ||
        value.find('\0') != std::string::npos)
    {
        return false;
    }
    switch (option.type)
    {
    case Type::Text:
        return true;
    case Type::Character:
    {
        wchar_t character{};
        std::mbstate_t state{};
        return std::mbrtowc(&character, value.data(), value.size(), &state) == value.size() &&
               wcwidth(character) == 1;
    }
    case Type::Switch:
        return value == "true" || value == "false" || value == "1" || value == "0";
    case Type::Sources:
    case Type::Modes:
    {
        const auto names = scmp::Utils::split(value, ',');
        return value.back() != ',' && std::all_of(names.begin(), names.end(), [&](const std::string &name) {
            if (option.type == Type::Modes)
            {
                return scmp::ConfigurationUtils::is_visualizer(name);
            }
#ifdef _OS_OSX
            if (name == ScmpConstants::k_mac_audio_source_name)
            {
                return true;
            }
#endif
            return name == ScmpConstants::k_mpd_audio_source_name;
        });
    }
    case Type::Choice:
    {
        const auto choices = scmp::Utils::split(option.hint, ',');
        return std::any_of(choices.begin(), choices.end(), [&](const std::string &choice) {
            return choice.substr(choice.find_first_not_of(' ')) == value;
        });
    }
    case Type::Integer:
    case Type::Decimal:
    {
        const char *allowed = option.type == Type::Integer ? "0123456789" : "0123456789.eE+-";
        if (value.find_first_not_of(allowed) != std::string::npos)
        {
            return false;
        }
        char *end = nullptr;
        const double number = std::strtod(value.c_str(), &end);
        return end == value.data() + value.size() &&
               number >= option.minimum && number <= option.maximum &&
               (option.key != k_sgs_smoothing_points || static_cast<int>(number) % 2 == 1);
    }
    }
    return false;
}
}

scmp::ConfigurationUtils::ConfigurationUtils()
{
}

scmp::ConfigurationUtils::~ConfigurationUtils()
{
}

std::unordered_map<std::string, std::string>
scmp::ConfigurationUtils::read_config(const std::string &config_path)
{
    std::unordered_map<std::string, std::string> properties_map;

    std::ifstream file(config_path.c_str(), std::ifstream::in);
    std::string line;
    if (!file.good())
    {
        SCMP_LOG(scmp::LogLevel::WARN, "Configuration not found at %s",
                config_path.c_str());
        return properties_map;
    }

    std::pair<std::string, std::string> split_line{"", ""};

    while (file.good() && std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (!line.empty() && line[0] != '#' && line.find('=') != std::string::npos)
        {
            scmp::Utils::split_first(line, '=', split_line);

            if (!split_line.first.empty())
            {
                properties_map[split_line.first] = split_line.second;
            }
            else
            {
                SCMP_LOG(scmp::LogLevel::WARN,
                        "Configuration line was not valid at %s", line.c_str());
            }
        }
    }

    return properties_map;
}

std::vector<scmp::ColorDefinition>
scmp::ConfigurationUtils::read_colors(const std::string &colors_path)
{
    std::vector<scmp::ColorDefinition> color_definitions;

    std::ifstream file(colors_path.c_str(), std::ifstream::in);
    std::string line;
    if (!file.good())
    {
        SCMP_LOG(scmp::LogLevel::WARN, "Colors configuration not found at %s",
                colors_path.c_str());
        return color_definitions;
    }

    std::vector<std::string> split_line;

    ColorIndex i = 0;
    while (file.good() && std::getline(file, line))
    {
        if (!line.empty())
        {
            if (line.size() >= 7)
            {
                auto hex_color = scmp::Utils::hex_to_int(line.substr(1,6));

                int16_t red = (hex_color >> 16) % 256;
                int16_t green = (hex_color >> 8) % 256;
                int16_t blue = hex_color % 256;

                color_definitions.push_back(
                    scmp::ColorDefinition(i, red, green, blue));
            }
            else
            {
                SCMP_LOG(
                    scmp::LogLevel::WARN,
                    "Configuration color definition line was not valid at %s",
                    line.c_str());
            }
        }
    }

    return color_definitions;
}

scmp::FalloffMode scmp::ConfigurationUtils::read_falloff_mode(
    const std::unordered_map<std::string, std::string> properties,
    const std::string &config_param,
    const scmp::FalloffMode default_falloff_mode)
{
    auto falloff_mode_str =
        scmp::Utils::get(properties, k_spectrum_falloff_mode, std::string{""});

    scmp::FalloffMode falloff_mode = default_falloff_mode;

    if (!falloff_mode_str.empty())
    {
        std::transform(falloff_mode_str.begin(), falloff_mode_str.end(),
                       falloff_mode_str.begin(), ::tolower);

        if (falloff_mode_str == "none")
        {
            falloff_mode = scmp::FalloffMode::None;
        }
        else if (falloff_mode_str == "fill")
        {
            falloff_mode = scmp::FalloffMode::Fill;
        }
        else if (falloff_mode_str == "top")
        {
            falloff_mode = scmp::FalloffMode::Top;
        }
        else
        {
            SCMP_LOG(scmp::LogLevel::ERROR, "Invalid falloff mode %s for %s",
                    falloff_mode_str.c_str(), config_param.c_str());
        }
    }

    return falloff_mode;
}

scmp::SmoothingMode scmp::ConfigurationUtils::read_smoothing_mode(
    const std::unordered_map<std::string, std::string> properties,
    const std::string &config_param,
    const scmp::SmoothingMode default_smoothing_mode)
{
    auto smoothing_mode_str =
        scmp::Utils::get(properties, k_spectrum_smoothing_mode, std::string{""});

    scmp::SmoothingMode smoothing_mode = default_smoothing_mode;

    if (!smoothing_mode_str.empty())
    {
        std::transform(smoothing_mode_str.begin(), smoothing_mode_str.end(),
                       smoothing_mode_str.begin(), ::tolower);

        if (smoothing_mode_str == "none")
        {
            smoothing_mode = scmp::SmoothingMode::None;
        }
        else if (smoothing_mode_str == "monstercat")
        {
            smoothing_mode = scmp::SmoothingMode::MonsterCat;
        }
        else if (smoothing_mode_str == "sgs")
        {
            smoothing_mode = scmp::SmoothingMode::Sgs;
        }
        else
        {
            SCMP_LOG(scmp::LogLevel::ERROR, "Invalid spectrum mode %s for %s",
                    smoothing_mode_str.c_str(), config_param.c_str());
        }
    }

    return smoothing_mode;
}

void scmp::ConfigurationUtils::load_settings(Settings &settings)
{
    load_settings(settings, default_config_path());
}

std::string scmp::ConfigurationUtils::default_config_path()
{
    return Utils::get_home_directory() + ScmpConstants::k_default_config_path;
}

void scmp::ConfigurationUtils::load_settings(Settings &settings,
                                            const std::string &config_path)
{
    apply_settings(settings, read_config(config_path));
}

void scmp::ConfigurationUtils::save_options(const std::string &config_path, const ConfigValues &changes)
{
    if (changes.empty())
    {
        return;
    }
    auto edits = changes;
    const auto &entries = options();
    for (auto &edit : edits)
    {
        const auto option = std::find_if(entries.begin(), entries.end(), [&](const ConfigOption &entry) {
            return edit.first == entry.key;
        });
        if (option == entries.end())
        {
            throw ScmpException("Unknown setting: %s", edit.first.c_str());
        }
        if (!valid_value(*option, edit.second))
        {
            throw ScmpException("Invalid value for %s. %s", option->key, option->hint);
        }
        if (option->type == ConfigOption::Type::Integer)
        {
            edit.second = std::to_string(std::stol(edit.second));
        }
    }

    if (config_path == default_config_path())
    {
        const auto parent = Utils::get_home_directory() + ".scmp";
        if (::mkdir(parent.c_str(), 0700) != 0 && errno != EEXIST)
        {
            throw ScmpException("Could not create %s: %s", parent.c_str(), std::strerror(errno));
        }
    }
    std::string path = config_path;
    struct stat info{};
    const int stat_result = ::lstat(config_path.c_str(), &info);
    if (stat_result == 0)
    {
        std::unique_ptr<char, decltype(&std::free)> resolved(::realpath(config_path.c_str(), nullptr),
                                                             &std::free);
        if (!resolved || ::stat(resolved.get(), &info) != 0 || !S_ISREG(info.st_mode))
        {
            throw ScmpException("Invalid config file: %s", config_path.c_str());
        }
        path = resolved.get();
    }
    else if (errno != ENOENT)
    {
        throw ScmpException("Could not access %s: %s", config_path.c_str(), std::strerror(errno));
    }

    auto properties = read_config(path);
    for (const auto &edit : edits)
    {
        properties[edit.first] = edit.second;
    }
    Settings settings;
    apply_settings(settings, properties);

    std::string contents;
    ConfigValues added;
    if (stat_result == 0)
    {
        std::ifstream input(path);
        std::string line;
        while (std::getline(input, line))
        {
            const auto separator = line.find('=');
            const auto edit = edits.find(line.substr(0, separator));
            if (separator != std::string::npos && edit != edits.end())
            {
                line = edit->first + '=' + edit->second;
                added.insert(*edit);
            }
            contents += line + '\n';
        }
        if (!input.eof() || input.bad())
        {
            throw ScmpException("Could not read config: %s", config_path.c_str());
        }
    }
    for (const auto &option : entries)
    {
        const auto edit = edits.find(option.key);
        if (edit != edits.end() && added.count(edit->first) == 0)
        {
            contents += edit->first + '=' + edit->second + '\n';
        }
    }

    auto temporary_path = path + ".XXXXXX";
    const int descriptor = ::mkstemp(&temporary_path[0]);
    if (descriptor < 0)
    {
        throw ScmpException("Could not write %s: %s", config_path.c_str(), std::strerror(errno));
    }
    if (stat_result == 0)
    {
        ::fchmod(descriptor, info.st_mode & 0777);
    }
    FILE *output = ::fdopen(descriptor, "w");
    bool saved = false;
    if (output)
    {
        const bool wrote = std::fwrite(contents.data(), 1, contents.size(), output) == contents.size();
        saved = std::fclose(output) == 0 && wrote;
    }
    else
    {
        ::close(descriptor);
    }
    if (!saved || ::rename(temporary_path.c_str(), path.c_str()) != 0)
    {
        const int error = errno;
        ::unlink(temporary_path.c_str());
        throw ScmpException("Could not save %s: %s", config_path.c_str(), std::strerror(error));
    }
}

void scmp::ConfigurationUtils::apply_settings(Settings &settings, const ConfigValues &properties)
{
    auto &workspace = settings.workspace;
    workspace.music_directory = Utils::get(properties, std::string{"music.directory"}, workspace.music_directory);
    workspace.theme = Utils::get(properties, std::string{"theme.name"}, workspace.theme);
    workspace.background = Utils::get(properties, std::string{"theme.background"}, workspace.background);
    workspace.theme_motion = Utils::get(properties, std::string{"theme.motion"}, true);
    workspace.plugin_directory = Utils::get(properties, std::string{"plugins.directory"}, workspace.plugin_directory);
    workspace.enabled_plugins = Utils::get(properties, std::string{"plugins.enabled"}, workspace.enabled_plugins);

    // setup mpd
    settings.set_mpd_fifo_path(
        Utils::get(properties, k_mpd_fifo_path_setting,
                   ScmpConstants::k_default_mpd_fifo_path));

    // setup audio sources
    settings.set_audio_sources(
        Utils::split(Utils::get(properties, k_audio_sources_setting,
                                ScmpConstants::k_default_audio_sources),
                     ','));

    settings.set_fps(
        Utils::get(properties, k_fps_setting, ScmpConstants::k_default_fps));

    settings.set_sampling_frequency(
        Utils::get(properties, k_sampling_frequency_setting,
                   ScmpConstants::k_default_sampling_frequency));

    settings.set_low_cutoff_frequency(
        Utils::get(properties, k_low_cutoff_frequency_setting,
                   ScmpConstants::k_default_low_cutoff_frequency));

    settings.set_high_cutoff_frequency(
        Utils::get(properties, k_high_cutoff_frequency_setting,
                   ScmpConstants::k_default_high_cutoff_frequency));

    settings.set_spectrum_character(
        Utils::get(properties, k_spectrum_character,
                   ScmpConstants::k_default_spectrum_character));

    settings.set_spectrum_bar_width(
        Utils::get(properties, k_spectrum_bar_width,
                   ScmpConstants::k_default_spectrum_bar_width));

    settings.set_spectrum_bar_spacing(
        Utils::get(properties, k_spectrum_bar_spacing,
                   ScmpConstants::k_default_spectrum_bar_spacing));

    settings.set_spectrum_smoothing_mode(
        read_smoothing_mode(properties, k_spectrum_smoothing_mode,
                            ScmpConstants::k_default_spectrum_smoothing_mode));

    settings.set_spectrum_falloff_mode(
        read_falloff_mode(properties, k_spectrum_falloff_mode,
                          ScmpConstants::k_default_spectrum_falloff_mode));

    settings.set_spectrum_falloff_weight(
        Utils::get(properties, k_spectrum_falloff_weight,
                   ScmpConstants::k_default_spectrum_falloff_weight));

    settings.set_monstercat_smoothing_factor(
        Utils::get(properties, k_monstercat_smoothing_factor,
                   ScmpConstants::k_default_monstercat_smoothing_factor));

    settings.set_sgs_smoothing_points(
        Utils::get(properties, k_sgs_smoothing_points,
                   ScmpConstants::k_default_sgs_smoothing_points));

    settings.set_sgs_smoothing_passes(
        Utils::get(properties, k_sgs_smoothing_passes,
                   ScmpConstants::k_default_sgs_smoothing_passes));

    settings.set_ellipse_character(
        Utils::get(properties, k_ellipse_character,
                   ScmpConstants::k_default_ellipse_character));

    settings.set_ellipse_radius(Utils::get(
        properties, k_ellipse_radius, ScmpConstants::k_default_ellipse_radius));

    settings.set_lorenz_character(
        Utils::get(properties, k_lorenz_character, ScmpConstants::k_default_lorenz_character));

    settings.set_rotation_interval(
        Utils::get(properties, k_vis_rotation_interval,
                   ScmpConstants::k_default_visualizer_rotation_interval));

    settings.set_is_stereo_enabled(
        Utils::get(properties, k_stereo_enabled_setting, true));

    // set color definitions
    auto colors_path = Utils::get_home_directory();
    colors_path.append(ScmpConstants::k_colors_directory);
    colors_path.append(Utils::get(properties, k_color_scheme_path_setting,
                                  ScmpConstants::k_default_colors_path));
    settings.set_color_definitions(
        scmp::ConfigurationUtils::read_colors(colors_path));

    const auto visualizers =
        Utils::split(Utils::get(properties, k_visualizers_setting,
                                ScmpConstants::k_default_visualizers),
                     ',');

    settings.set_visualizers(visualizers);

    if (settings.get_fps() < 1 || settings.get_fps() > 240 ||
        settings.get_sampling_frequency() < 8000 || settings.get_sampling_frequency() > 384000 ||
        settings.get_low_cutoff_frequency() == 0 ||
        settings.get_low_cutoff_frequency() >= settings.get_high_cutoff_frequency() ||
        settings.get_high_cutoff_frequency() > 192000 ||
        settings.get_spectrum_bar_width() == 0 ||
        settings.get_spectrum_bar_width() > 200 || settings.get_spectrum_bar_spacing() > 200 ||
        settings.get_ellipse_radius() == 0 || settings.get_ellipse_radius() > 200)
    {
        throw ScmpException("Invalid rate, spectrum geometry, cutoff range, or ellipse radius. Use scmp --settings to edit.");
    }
}
