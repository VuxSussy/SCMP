#include "Transformer/LavaTransformer.h"
#include "Domain/ScmpConstants.h"

#include <algorithm>
#include <cmath>

namespace
{
double field(double x, double y, double center_x, double center_y, double radius)
{
    const double dx = x - center_x;
    const double dy = y - center_y;
    return radius * radius / (dx * dx + dy * dy + 0.04);
}
}

scmp::LavaTransformer::LavaTransformer(const Settings *settings) : m_settings(settings)
{
}

void scmp::LavaTransformer::execute_stereo(pcm_stereo_sample *buffer, NcursesWriter *writer)
{
    draw(buffer, writer, true);
}

void scmp::LavaTransformer::execute_mono(pcm_stereo_sample *buffer, NcursesWriter *writer)
{
    draw(buffer, writer, false);
}

void scmp::LavaTransformer::move_pointer(int row, int column)
{
    m_row = row;
    m_column = column;
    m_keyboard = false;
}

void scmp::LavaTransformer::move_cursor(int vertical, int horizontal, int height, int width)
{
    if (m_row < 0)
    {
        m_row = height / 2;
        m_column = width / 2;
    }
    m_row = std::max(0, std::min(height - 1, m_row + vertical));
    m_column = std::max(0, std::min(width - 1, m_column + horizontal));
    m_keyboard = true;
}

void scmp::LavaTransformer::reset_pointer()
{
    m_row = -1;
    m_column = -1;
    m_keyboard = false;
    m_inside = false;
}

void scmp::LavaTransformer::draw(pcm_stereo_sample *buffer, NcursesWriter *writer, bool stereo)
{
    const int width = writer->width();
    const int height = writer->height() - (writer->composed() ? 0 : 1);
    if (height < 5 || width < 12)
    {
        writer->clear();
        writer->flush();
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const double dt = std::min(0.2, std::chrono::duration<double>(now - m_last_frame).count());
    m_last_frame = now;
    m_time += dt;

    double energy = 0;
    const auto count = m_settings->get_sample_size();
    for (uint32_t i = 0; i < count; ++i)
    {
        const double left = buffer[i].l / 32768.0;
        const double right = buffer[i].r / 32768.0;
        const double mono = (left + right) * 0.5;
        energy += stereo ? (left * left + right * right) * 0.5 : mono * mono;
    }
    const double target = std::min(1.0, std::sqrt(energy / count) * 4.0);
    m_energy += (target - m_energy) * (1.0 - std::exp(-9.0 * dt));

    const double scale = std::min(height * 0.28, width * 0.14);
    const double center_x = (width - 1) * 0.5 + std::sin(m_time * 0.6) * scale * 0.25;
    const double center_y = (height - 1) * 0.5 + std::sin(m_time * 0.8) * scale * 0.12;
    const double radius = 0.59 + 0.24 * m_energy;
    const double sway = std::sin(m_time * 1.2) * 0.20;
    auto intensity = [&](double x, double y, double split) {
        const double spread = 0.36 + 1.12 * split;
        return field(x, y, -spread, sway, radius) +
               field(x, y, spread, -sway, radius * 0.92);
    };

    const bool pointer_valid = m_row >= 0 && m_row < height && m_column >= 0 && m_column < width;
    const double pointer_x = (m_column - center_x) / (scale * 2);
    const double pointer_y = (m_row - center_y) / scale;
    m_inside = pointer_valid &&
        (intensity(pointer_x, pointer_y, 0) >= (m_inside ? 0.76 : 1.0) ||
         intensity(pointer_x, pointer_y, m_separation) >= 1.0);
    const double split = m_inside ? 1.0 : 0.0;
    m_separation += (split - m_separation) * (1.0 - std::exp(-8.0 * dt));

    const ColorIndex hot[] = {52, 88, 124, 160, 196, 202, 208, 214, 220, 229};
    const ColorIndex basic[] = {COLOR_RED, COLOR_RED, COLOR_RED, COLOR_RED, COLOR_RED,
                              COLOR_YELLOW, COLOR_YELLOW, COLOR_YELLOW, COLOR_YELLOW, COLOR_WHITE};
    const std::wstring block(1, ScmpConstants::k_full_block_wchar);
    const std::wstring dither(L" .:-=+*#%@");
    writer->clear();
    for (int row = 0; row < height; ++row)
    {
        const double y = (row - center_y) / scale;
        for (int column = 0; column < width; ++column)
        {
            const double x = (column - center_x) / (scale * 2);
            const double heat = intensity(x, y, m_separation);
            if (heat >= 1.0)
            {
                const int shade = static_cast<int>(std::min(9.0, (heat - 1.0) * 2.8 + m_energy));
                if (COLORS > COLOR_WHITE)
                {
                    const auto color = COLORS >= 256 ? hot[shade] : basic[shade];
                    writer->write(row, column, color, block, block.front());
                }
                else
                {
                    mvwaddch(writer->surface(), row, column, dither[std::max(1, shade)]);
                }
            }
        }
    }
    if (m_keyboard && pointer_valid)
    {
        writer->write_over(writer->surface(), m_row, m_column, L"+", A_REVERSE);
    }
    if (!writer->composed())
    {
        attron(A_DIM);
        mvaddnstr(height, 1, "Lava  mouse / arrows  o folder  T themes  p plugins  Space mode  q quit", width - 2);
        attroff(A_DIM);
    }
    writer->flush();
}
