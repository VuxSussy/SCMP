#include "Domain/Settings.h"
#include "Domain/ScmpConstants.h"

scmp::Settings::Settings()
    : m_fps{ScmpConstants::k_default_fps},
      m_sampling_frequency{ScmpConstants::k_default_sampling_frequency},
      m_low_cutoff_frequency{ScmpConstants::k_default_low_cutoff_frequency},
      m_high_cutoff_frequency{ScmpConstants::k_default_high_cutoff_frequency},
      m_is_stereo_enabled{true},
      m_spectrum_character{ScmpConstants::k_default_spectrum_character},
      m_lorenz_character{ScmpConstants::k_default_lorenz_character},
      m_ellipse_character{ScmpConstants::k_default_ellipse_character},
      m_ellipse_radius{ScmpConstants::k_default_ellipse_radius},
      m_spectrum_bar_width{ScmpConstants::k_default_spectrum_bar_width},
      m_spectrum_bar_spacing{ScmpConstants::k_default_spectrum_bar_spacing},
      m_monstercat_smoothing_factor{
          ScmpConstants::k_default_monstercat_smoothing_factor},
      m_sgs_smoothing_points{ScmpConstants::k_default_sgs_smoothing_points},
      m_sgs_smoothing_passes{ScmpConstants::k_default_sgs_smoothing_passes},
      m_spectrum_smoothing_mode{
          ScmpConstants::k_default_spectrum_smoothing_mode},
      m_spectrum_falloff_mode{ScmpConstants::k_default_spectrum_falloff_mode},
      m_spectrum_falloff_weight{ScmpConstants::k_default_spectrum_falloff_weight},
      m_rotation_interval{ScmpConstants::k_default_visualizer_rotation_interval}
{
}

scmp::Settings::~Settings()
{
}
