#ifndef SCMP_LAVA_TRANSFORMER_H
#define SCMP_LAVA_TRANSFORMER_H

#include <chrono>
#include "Transformer/GenericTransformer.h"

namespace scmp
{
class LavaTransformer final : public GenericTransformer
{
  public:
    explicit LavaTransformer(const Settings *settings);

    void execute_stereo(pcm_stereo_sample *buffer, NcursesWriter *writer) override;
    void execute_mono(pcm_stereo_sample *buffer, NcursesWriter *writer) override;
    void move_pointer(int row, int column);
    void move_cursor(int vertical, int horizontal, int height, int width);
    void reset_pointer();

  private:
    void draw(pcm_stereo_sample *buffer, NcursesWriter *writer, bool stereo);

    const Settings *m_settings;
    std::chrono::steady_clock::time_point m_last_frame = std::chrono::steady_clock::now();
    double m_time = 0;
    double m_energy = 0;
    double m_separation = 0;
    int m_row = -1;
    int m_column = -1;
    bool m_keyboard = false;
    bool m_inside = false;
};
}

#endif
