#include "Writer/Wallpaper.h"

const std::vector<scmp::Theme> &scmp::themes()
{
    static const std::vector<Theme> items{
#ifdef _OS_OSX
        {"moonlit", "Moonlit"}, {"clawd", "Clawd"}, {"cloud-drift", "Cloud Drift"},
        {"honu-lagoon", "Honu Lagoon"}, {"koi-pond", "Koi Pond"}, {"jellyfish", "Jellyfish"},
        {"lantern-night", "Lantern Night"}, {"fireflies", "Fireflies"}, {"neon-rain", "Neon Rain"},
        {"sakura-dusk", "Sakura Dusk"}, {"snow-cabin", "Snow Cabin"}, {"star-voyage", "Star Voyage"},
#endif
        {"night-drive", "Night Drive"}, {"alpine", "Alpine"}, {"orbit", "Orbit"}, {"none", ""},
    };
    return items;
}

const std::string &scmp::theme_choices()
{
    static const auto choices = [] {
        std::string text = "auto";
        for (const auto &theme : themes())
        {
            text += ", ";
            text += theme.id;
        }
        return text;
    }();
    return choices;
}
