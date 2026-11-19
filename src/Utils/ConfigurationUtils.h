#ifndef _SCMP_CONFIGURATION_UTILS_H
#define _SCMP_CONFIGURATION_UTILS_H

#include <unordered_map>

#include "Domain/Settings.h"
#include "Domain/ScmpConstants.h"

namespace scmp
{

using ConfigValues = std::unordered_map<std::string, std::string>;

struct ConfigOption
{
    enum class Type { Text, Integer, Decimal, Switch, Sources, Modes, Choice, Character };

    const char *category;
    const char *label;
    const char *key;
    std::string default_value;
    const char *hint;
    Type type;
    double minimum = 0;
    double maximum = 0;
};

class ConfigurationUtils
{
  public:
    static void load_settings(Settings &settings,
                              const std::string &config_path);

    static void load_settings(Settings &settings);

    static std::string default_config_path();
    static const std::vector<ConfigOption> &options();
    static ConfigValues read_config(const std::string &config_path);
    static void save_options(const std::string &config_path, const ConfigValues &changes);
    static bool is_visualizer(const std::string &name);

  private:
    static void apply_settings(Settings &settings, const ConfigValues &properties);

    static scmp::SmoothingMode read_smoothing_mode(
        const std::unordered_map<std::string, std::string> properties,
        const std::string &config_param,
        const scmp::SmoothingMode default_smoothing_mode);

    static scmp::FalloffMode read_falloff_mode(
        const std::unordered_map<std::string, std::string> properties,
        const std::string &config_param,
        const scmp::FalloffMode default_falloff_mode);

    static std::vector<scmp::ColorDefinition>
    read_colors(const std::string &colors_path);

    explicit ConfigurationUtils();

    virtual ~ConfigurationUtils();
};
}

#endif
