#include <scmp/plugin.h>

/* A replacement visualizer with a grid and a stereo oscilloscope. */
static void background(const ScmpPluginFrame *frame, const ScmpPluginCanvas *canvas)
{
    (void)frame;
    for (int y = 2; y < canvas->height; y += 3) {
        for (int x = 2; x < canvas->width; x += 5) {
            canvas->put(canvas->context, x, y, 0x00b7, 60);
        }
    }
}

static void waveform(const ScmpPluginFrame *frame, const ScmpPluginCanvas *canvas)
{
    if (!frame->sample_count) {
        return;
    }
    const int middle = canvas->height / 2;
    const int amplitude = middle > 0 ? middle - 1 : 0;
    for (int x = 0; x < canvas->width; ++x) {
        const uint32_t i = (uint64_t)x * frame->sample_count / canvas->width;
        const int left = frame->samples[i].left * amplitude / 32768;
        const int right = frame->samples[i].right * amplitude / 32768;
        canvas->put(canvas->context, x, middle - left, 0x25aa, 87);
        canvas->put(canvas->context, x, middle + right, 0x25aa, 213);
    }
}

static void overlay(const ScmpPluginFrame *frame, const ScmpPluginCanvas *canvas)
{
    const char label[] = "Pulse Grid";
    const int color = frame->playing ? 87 : 245;
    for (int x = 0; label[x]; ++x) {
        canvas->put(canvas->context, x + 1, 0, (uint32_t)label[x], color);
    }
}

const ScmpPluginV1 *scmp_plugin_v1(void)
{
    static const ScmpPluginV1 plugin = {
        SCMP_PLUGIN_ABI_VERSION, "Pulse Grid", background, waveform, overlay
    };
    return &plugin;
}
