#include "Transformer/GenericTransformer.h"

scmp::GenericTransformer::GenericTransformer()
{
}

scmp::GenericTransformer::~GenericTransformer()
{
}

void scmp::GenericTransformer::recalculate_colors(
    const size_t max, std::vector<ColorIndex> &precomputed_colors,
    const NcursesWriter *writer)
{
    if (max != precomputed_colors.size())
    {
        precomputed_colors.resize(max);
        for (size_t i = 0ul; i < max; ++i)
        {
            precomputed_colors[i] = writer->to_color_pair(
                static_cast<int32_t>(i), static_cast<int32_t>(max));
        }
    }
}
