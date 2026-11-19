#ifndef SCMP_PLUGIN_H
#define SCMP_PLUGIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCMP_PLUGIN_ABI_VERSION 1u

typedef struct {
    int16_t left;
    int16_t right;
} ScmpPluginSample;

typedef struct {
    int width;
    int height;
    void *context;
    /* color is an xterm 256-color index. codepoint must occupy one terminal cell. */
    void (*put)(void *context, int x, int y, uint32_t codepoint, uint8_t color);
} ScmpPluginCanvas;

typedef struct {
    const ScmpPluginSample *samples;
    uint32_t sample_count;
    uint32_t sample_rate;
    double time_seconds;
    double track_elapsed;
    const char *track_title;
    const char *visualizer;
    int playing;
} ScmpPluginFrame;

typedef void (*ScmpPluginDraw)(const ScmpPluginFrame *frame, const ScmpPluginCanvas *canvas);

typedef struct {
    uint32_t abi_version;
    const char *name;
    ScmpPluginDraw background;
    ScmpPluginDraw visualizer;
    ScmpPluginDraw overlay;
} ScmpPluginV1;

/* Export this function. All frame, canvas, and context pointers are valid only during a draw call. */
const ScmpPluginV1 *scmp_plugin_v1(void);

#ifdef __cplusplus
}
#endif

#endif
