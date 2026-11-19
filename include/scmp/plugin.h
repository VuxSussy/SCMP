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

/* ------------------------------------------------------------------------------------------------
 * Version 2 adds layout plugins. A layout plugin draws the whole Player screen from a snapshot of
 * the library and playback, and can answer keys by sending commands back to scmp. Export
 * scmp_plugin_v2 instead of (or as well as) scmp_plugin_v1. Every pointer in ScmpLayout is valid
 * only during the call that received it; copy what you need to keep.
 * ------------------------------------------------------------------------------------------------ */

#define SCMP_PLUGIN_V2_ABI_VERSION 2u

/* Index meaning "no track". */
#define SCMP_NONE 0xffffffffu
/* Colors are xterm 256-color indices. SCMP_COLOR_DEFAULT is the terminal's own color, and
 * SCMP_COLOR_KEEP (backgrounds only) keeps whatever is already drawn in the cell. */
#define SCMP_COLOR_DEFAULT (-1)
#define SCMP_COLOR_KEEP (-2)

enum {
    SCMP_ATTR_BOLD = 1,
    SCMP_ATTR_DIM = 2,
    SCMP_ATTR_REVERSE = 4,
    SCMP_ATTR_UNDERLINE = 8
};

/* Cover images that layout->image can show. */
enum {
    SCMP_IMAGE_SELECTED = 0, /* the selected track */
    SCMP_IMAGE_PLAYING = 1   /* the playing track, or the selected one when stopped */
};

/* Keys passed to layout_key: printable ASCII as is, or one of these. */
enum {
    SCMP_KEY_UP = 0x1000,
    SCMP_KEY_DOWN,
    SCMP_KEY_LEFT,
    SCMP_KEY_RIGHT,
    SCMP_KEY_ENTER,
    SCMP_KEY_ESCAPE,
    SCMP_KEY_PAGE_UP,
    SCMP_KEY_PAGE_DOWN,
    SCMP_KEY_HOME,
    SCMP_KEY_END
};

/* Commands a layout sends with layout->command. value and text are used where noted. */
enum {
    SCMP_COMMAND_PLAY_SELECTED,
    SCMP_COMMAND_TOGGLE_PAUSE,
    SCMP_COMMAND_NEXT,
    SCMP_COMMAND_PREVIOUS,
    SCMP_COMMAND_SEEK,          /* value: seconds, relative */
    SCMP_COMMAND_VOLUME,        /* value: change from -1 to 1 */
    SCMP_COMMAND_SHUFFLE,
    SCMP_COMMAND_REPEAT,
    SCMP_COMMAND_SEARCH,        /* text: filter, "" clears */
    SCMP_COMMAND_SELECT,        /* value: rows to move the selection, relative */
    SCMP_COMMAND_PLAYLIST       /* value: playlist index */
};

typedef struct {
    const char *path;
    const char *title;   /* from tags, or the filename until tags are read */
    const char *artist;  /* may be empty */
    const char *album;   /* may be empty */
    double duration;     /* seconds, 0 until known */
    uint32_t playlist;   /* index of the folder the track belongs to */
} ScmpTrack;

typedef struct {
    const char *name;
    const char *directory; /* empty for "All music" */
    uint32_t track_count;
} ScmpPlaylist;

typedef struct ScmpLayout ScmpLayout;
struct ScmpLayout {
    void *host;
    int width;            /* whole terminal, in cells */
    int height;
    double time_seconds;
    uint64_t frame;
    const char *user;

    /* Library */
    uint32_t track_count;
    const ScmpTrack *tracks;
    uint32_t visible_count;    /* tracks in the current playlist that match the search, in order */
    const uint32_t *visible;   /* indices into tracks */
    uint32_t selected;         /* index into tracks, or SCMP_NONE */
    uint32_t playing_track;    /* index into tracks, or SCMP_NONE */
    uint32_t playlist_count;
    const ScmpPlaylist *playlists;
    uint32_t playlist;         /* current playlist */
    const char *search;

    /* Playback */
    int has_track;   /* a track is loaded */
    int playing;
    int shuffle;
    int repeat;      /* 0 off, 1 all, 2 one */
    double elapsed;
    double duration;
    double volume;   /* 0 to 1 */
    const char *message; /* error or status text to show, may be empty */

    /* Drawing. Text is UTF-8 and clipped to max_width cells; the call returns the cells drawn. */
    int (*text)(ScmpLayout *layout, int x, int y, int max_width, const char *utf8, int fg, int bg, int attrs);
    int (*text_width)(ScmpLayout *layout, const char *utf8);
    void (*fill)(ScmpLayout *layout, int x, int y, int width, int height, int bg);
    /* Shows a cover. Real images in terminals that support them, colored cells elsewhere. */
    void (*image)(ScmpLayout *layout, int which, int x, int y, int width, int height);
    /* Average color of a cover as 0xRRGGBB, for tinting. */
    uint32_t (*image_color)(ScmpLayout *layout, int which);
    /* Nearest xterm 256-color index for 0xRRGGBB. */
    int (*color)(ScmpLayout *layout, uint32_t rgb);
    /* Reserves an area for the audio visualizer. scmp draws it after layout_draw returns. Leave the
     * area unfilled to let the wallpaper show behind it. */
    void (*visualizer)(ScmpLayout *layout, int x, int y, int width, int height);
    void (*command)(ScmpLayout *layout, int command, double value, const char *text);
};

typedef struct {
    uint32_t abi_version; /* SCMP_PLUGIN_V2_ABI_VERSION */
    const char *name;
    /* The version 1 layers still work; leave unused ones NULL. */
    ScmpPluginDraw background;
    ScmpPluginDraw visualizer;
    ScmpPluginDraw overlay;
    /* Draws the Player screen. scmp has already painted the wallpaper. */
    void (*layout_draw)(ScmpLayout *layout);
    /* Return 1 when the key was handled. Unhandled keys get scmp's default action. Drawing calls
     * do nothing here; send commands instead. */
    int (*layout_key)(ScmpLayout *layout, int key);
    /* Smallest terminal the layout needs. Smaller terminals use the classic layout. */
    int min_width;
    int min_height;
} ScmpPluginV2;

const ScmpPluginV2 *scmp_plugin_v2(void);

#ifdef __cplusplus
}
#endif

#endif
