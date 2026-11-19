#ifndef _SCMP_GENERIC_TRANSFORMER_H
#define _SCMP_GENERIC_TRANSFORMER_H

#include "Writer/NcursesWriter.h"
#include "Domain/ScmpTypes.h"

namespace scmp
{

class GenericTransformer
{
  public:
    explicit GenericTransformer();

    virtual ~GenericTransformer();

    virtual void execute_stereo(pcm_stereo_sample *buffer,
                                scmp::NcursesWriter *writer) = 0;
    virtual void execute_mono(pcm_stereo_sample *buffer,
                              scmp::NcursesWriter *writer) = 0;

  protected:
    /**
     * Helper method to recalculate colors for a given range. This is used
     * mainly as a performance tweak since re-calculating colors can be
     * somewhat costly if done on every run.
     *
     * Colors are re-calculated if max != precomputed_colors.size()
     */
    virtual void recalculate_colors(const size_t max,
                                    std::vector<ColorIndex> &precomputed_colors,
                                    const NcursesWriter *writer);
};
}

#endif
