#ifndef _SCMP_VISUALIZER_H
#define _SCMP_VISUALIZER_H

#include <csignal>
#include <memory>
#include "Domain/Settings.h"
#include "Player/MusicSession.h"
#include "Source/AudioSource.h"
#include "Transformer/GenericTransformer.h"

namespace scmp
{
class Visualizer
{
  public:
    Visualizer(Settings &settings, MusicSession &music);

    // Reload settings when the editor saves.
    bool run(const std::string &config_path, std::string &mode, ViewMode &view, std::string &theme,
             std::string &layout,
             const volatile std::sig_atomic_t &interrupted);

  private:
    void set_sample_rate(uint32_t rate);
    void setup_transformers();

    Settings &m_settings;
    MusicSession &m_music;
    uint32_t m_configured_rate;
    uint32_t m_low_cutoff;
    uint32_t m_high_cutoff;
    std::vector<std::string> m_names;
    std::vector<std::unique_ptr<GenericTransformer>> m_transformers;
};
}

#endif
