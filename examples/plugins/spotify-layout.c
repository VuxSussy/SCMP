/* Spotify-inspired Player layout for scmp, built on the layout plugin API (scmp_plugin_v2).
 *
 * Dark panels on black: a library sidebar with your music folders and artists, a playlist header
 * tinted by the cover, a two-line song table, a now-playing panel holding the visualizer, and a
 * player bar. Press h for the library, j/k to move, Enter to open a playlist or an artist, and l to
 * go back to the songs. Everything else uses scmp's own keys.
 *
 * Build: make plugins   (or cc -dynamiclib/-shared -fPIC -Iinclude spotify-layout.c)
 */
#include <scmp/plugin.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    BLACK = 16,
    PANEL = 233,
    PILL = 235,
    HOVER = 236,
    RULE = 239,
    FAINT = 242,
    MUTED = 249,
    TEXT = 255,
    GREEN = 35,
    GREEN_TEXT = 41
};

#define MAX_ARTISTS 256

typedef struct {
    char name[96];
    unsigned count;
} Artist;

/* Plugin state survives between calls. */
static struct {
    int library_focus;
    unsigned library_index; /* playlists first, then artists */
    uint32_t first_row;
    Artist artists[MAX_ARTISTS];
    unsigned artist_count;
    double total_seconds;
    uint64_t refreshed_frame;
    uint32_t refreshed_tracks;
    int refreshed_once;
} state;

static int min_int(int a, int b) { return a < b ? a : b; }
static int max_int(int a, int b) { return a > b ? a : b; }

static void format_time(double seconds, char *out, size_t size)
{
    if (seconds < 0) seconds = 0;
    unsigned total = (unsigned)seconds;
    snprintf(out, size, "%u:%02u", total / 60, total % 60);
}

/* Blend a cover color toward the panel: t = 1 keeps the color, t = 0 is the panel. */
static uint32_t blend(uint32_t color, double t)
{
    const double panel = 0x12;
    uint32_t result = 0;
    for (int shift = 16; shift >= 0; shift -= 8) {
        double value = (double)((color >> shift) & 0xff);
        result |= (uint32_t)(panel + (value - panel) * t) << shift;
    }
    return result;
}

static int tile_color(const char *name)
{
    static const int palette[] = {60, 61, 96, 131, 66, 95, 132, 67, 97, 130};
    uint32_t hash = 2166136261u;
    for (const unsigned char *c = (const unsigned char *)name; *c; ++c) hash = (hash ^ *c) * 16777619u;
    return palette[hash % (sizeof(palette) / sizeof(palette[0]))];
}

/* Copies the first UTF-8 character of text into out. */
static void first_letter(const char *text, char *out)
{
    size_t length = 1;
    unsigned char lead = (unsigned char)text[0];
    if (!lead) {
        strcpy(out, "?");
        return;
    }
    if (lead >= 0xf0) length = 4;
    else if (lead >= 0xe0) length = 3;
    else if (lead >= 0xc0) length = 2;
    memcpy(out, text, length);
    out[length] = '\0';
}

static void add_artist(const char *name, size_t length)
{
    while (length && *name == ' ') { ++name; --length; }
    while (length && name[length - 1] == ' ') --length;
    if (!length) return;
    if (length >= sizeof(state.artists[0].name)) length = sizeof(state.artists[0].name) - 1;
    for (unsigned i = 0; i < state.artist_count; ++i) {
        if (strlen(state.artists[i].name) == length && !strncmp(state.artists[i].name, name, length)) {
            ++state.artists[i].count;
            return;
        }
    }
    if (state.artist_count == MAX_ARTISTS) return;
    Artist *artist = &state.artists[state.artist_count++];
    memcpy(artist->name, name, length);
    artist->name[length] = '\0';
    artist->count = 1;
}

/* "A/B, C" lists several artists. Names like "W/N" use the separator themselves, so a string
 * whose pieces include a single letter counts as one artist. */
static void add_artists(const char *artists)
{
    const char *separators = "/,;&";
    int single_letter = 0;
    for (const char *start = artists; *start;) {
        size_t piece = strcspn(start, separators);
        const char *s = start;
        size_t n = piece;
        while (n && *s == ' ') { ++s; --n; }
        while (n && s[n - 1] == ' ') --n;
        if (n == 1) single_letter = 1;
        start += piece + (start[piece] ? 1 : 0);
    }
    if (single_letter) {
        add_artist(artists, strlen(artists));
        return;
    }
    for (const char *start = artists; *start;) {
        size_t piece = strcspn(start, separators);
        add_artist(start, piece);
        start += piece + (start[piece] ? 1 : 0);
    }
}

static int by_count(const void *a, const void *b)
{
    const Artist *x = a, *y = b;
    if (x->count != y->count) return x->count < y->count ? 1 : -1;
    return strcmp(x->name, y->name);
}

static void refresh_library(const ScmpLayout *layout)
{
    /* Tags arrive in the background; recount a few times a second. */
    if (state.refreshed_once && state.refreshed_tracks == layout->track_count &&
        layout->frame - state.refreshed_frame < 8)
        return;
    state.refreshed_once = 1;
    state.refreshed_frame = layout->frame;
    state.refreshed_tracks = layout->track_count;
    state.artist_count = 0;
    state.total_seconds = 0;
    for (uint32_t i = 0; i < layout->visible_count; ++i) {
        const ScmpTrack *track = &layout->tracks[layout->visible[i]];
        state.total_seconds += track->duration;
    }
    for (uint32_t i = 0; i < layout->track_count; ++i) {
        if (layout->tracks[i].artist && *layout->tracks[i].artist) add_artists(layout->tracks[i].artist);
    }
    qsort(state.artists, state.artist_count, sizeof(Artist), by_count);
}

static unsigned library_size(const ScmpLayout *layout)
{
    return layout->playlist_count + state.artist_count;
}

static int text(ScmpLayout *l, int x, int y, int width, const char *s, int fg, int attrs)
{
    return l->text(l, x, y, width, s, fg, SCMP_COLOR_KEEP, attrs);
}

static void draw_top(ScmpLayout *l)
{
    text(l, 2, 0, 2, "♫", GREEN_TEXT, SCMP_ATTR_BOLD);
    text(l, 4, 0, 5, "scmp", TEXT, SCMP_ATTR_BOLD);
    const int tabs_x = l->width - 25;
    const int room = tabs_x - 14;
    const int pill_width = min_int(48, min_int(l->width / 3, room));
    if (pill_width >= 16) {
        const int pill_x = 12 + (room - pill_width) / 2;
        l->fill(l, pill_x, 0, pill_width, 1, PILL);
        text(l, pill_x + 2, 0, 2, "⌕", MUTED, 0);
        const int searching = l->search && *l->search;
        text(l, pill_x + 4, 0, pill_width - 6, searching ? l->search : "What do you want to play?   /",
             searching ? TEXT : FAINT, searching ? SCMP_ATTR_BOLD : 0);
    }
    text(l, tabs_x, 0, 12, "1 Visualizer", MUTED, 0);
    text(l, tabs_x + 15, 0, 8, "2 Player", TEXT, SCMP_ATTR_BOLD);
}

static void draw_library(ScmpLayout *l, int x, int width, int top, int height)
{
    text(l, x + 2, top + 1, width - 4, "▤  Your Library", state.library_focus ? TEXT : MUTED, SCMP_ATTR_BOLD);
    const unsigned count = library_size(l);
    const int list_top = top + 3;
    const int capacity = max_int(1, (top + height - list_top) / 3);
    if (state.library_index >= count) state.library_index = count ? count - 1 : 0;
    unsigned first = state.library_index >= (unsigned)capacity ? state.library_index - capacity + 1 : 0;
    int y = list_top;
    for (unsigned i = first; i < count && y + 1 < top + height; ++i, y += 3) {
        const int is_playlist = i < l->playlist_count;
        const char *name = is_playlist ? l->playlists[i].name : state.artists[i - l->playlist_count].name;
        const unsigned songs = is_playlist ? l->playlists[i].track_count : state.artists[i - l->playlist_count].count;
        int current = is_playlist ? (i == l->playlist && !(l->search && *l->search))
                                  : (l->search && !strcmp(l->search, name));
        if (state.library_focus && i == state.library_index) l->fill(l, x, y, width, 2, HOVER);
        /* Playlists get the cover tint, artists a color from their name. */
        const int tile = is_playlist ? l->color(l, blend(l->image_color(l, SCMP_IMAGE_PLAYING), 0.9)) : tile_color(name);
        l->fill(l, x + 2, y, 4, 2, tile);
        char letter[8];
        first_letter(name, letter);
        text(l, x + 3, y, 2, is_playlist ? "♫" : letter, TEXT, SCMP_ATTR_BOLD);
        char subtitle[64];
        snprintf(subtitle, sizeof(subtitle), "%s · %u song%s", is_playlist ? "Playlist" : "Artist", songs,
                 songs == 1 ? "" : "s");
        if (l->text_width(l, subtitle) > width - 10)
            snprintf(subtitle, sizeof(subtitle), "%s · %u", is_playlist ? "Playlist" : "Artist", songs);
        text(l, x + 8, y, width - 10, name, current ? GREEN_TEXT : TEXT, SCMP_ATTR_BOLD);
        text(l, x + 8, y + 1, width - 10, subtitle, MUTED, 0);
    }
}

static void draw_main(ScmpLayout *l, int x, int width, int top, int height)
{
    const int header = min_int(10, max_int(6, height / 3));
    const uint32_t tint = l->image_color(l, SCMP_IMAGE_PLAYING);
    for (int row = 0; row < header + 2; ++row) {
        double t = 1.0 - (double)row / (header + 2);
        l->fill(l, x, top + row, width, 1, l->color(l, blend(tint, 0.25 + 0.75 * t)));
    }
    if (!l->track_count && !l->playlist_count) {
        text(l, x + 3, top + 2, width - 6, "Nothing here yet", TEXT, SCMP_ATTR_BOLD);
        text(l, x + 3, top + 3, width - 6, "Press o to add a music folder or i to import a playlist.", TEXT, 0);
        return;
    }
    const int art_height = header - 2;
    const int art_width = art_height * 2;
    l->image(l, SCMP_IMAGE_PLAYING, x + 3, top + 1, art_width, art_height);

    const int text_x = x + art_width + 6;
    const int text_width = width - art_width - 8;
    const int searching = l->search && *l->search;
    const char *name = searching ? l->search
                                 : l->playlist < l->playlist_count ? l->playlists[l->playlist].name : "Music";
    text(l, text_x, top + header - 5, text_width, searching ? "Search" : "Playlist", TEXT, 0);
    /* Spaced capitals stand in for a large title font. */
    char title[256];
    size_t used = 0;
    for (const unsigned char *c = (const unsigned char *)name; *c && used + 3 < sizeof(title); ++c) {
        title[used++] = (char)(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
        if (*c < 0x80) title[used++] = ' ';
    }
    title[used] = '\0';
    text(l, text_x, top + header - 4, text_width, title, TEXT, SCMP_ATTR_BOLD);
    char summary[160];
    unsigned minutes = (unsigned)(state.total_seconds / 60);
    if (minutes >= 60)
        snprintf(summary, sizeof(summary), "%s · %u song%s, about %u hr %u min", l->user, l->visible_count,
                 l->visible_count == 1 ? "" : "s", minutes / 60, minutes % 60);
    else
        snprintf(summary, sizeof(summary), "%s · %u song%s, %u min", l->user, l->visible_count,
                 l->visible_count == 1 ? "" : "s", minutes);
    text(l, text_x, top + header - 2, text_width, summary, TEXT, 0);

    /* Action row. */
    const int actions = top + header + 1;
    l->fill(l, x + 3, actions, 5, 1, GREEN);
    text(l, x + 5, actions, 2, l->playing ? "Ⅱ" : "▶", BLACK, SCMP_ATTR_BOLD);
    text(l, x + 11, actions, 2, "⇄", l->shuffle ? GREEN_TEXT : MUTED, SCMP_ATTR_BOLD);
    text(l, x + 15, actions, 3, l->repeat == 2 ? "↻1" : "↻", l->repeat ? GREEN_TEXT : MUTED, SCMP_ATTR_BOLD);

    /* Song table. */
    const int table = actions + 2;
    char digits_text[16];
    const int digits = snprintf(digits_text, sizeof(digits_text), "%u", l->track_count);
    const int number_x = x + 2;
    const int title_x = number_x + digits + 3;
    const int album_width = width >= 70 ? min_int(30, width * 3 / 10) : 0;
    const int album_x = x + width - 6 - album_width - 2;
    const int title_width = (album_width ? album_x - 2 : x + width - 8) - title_x;
    text(l, number_x + digits - 1, table, 1, "#", MUTED, 0);
    text(l, title_x, table, title_width, "Title", MUTED, 0);
    if (album_width) text(l, album_x, table, album_width, "Album", MUTED, 0);
    text(l, x + width - 6, table, 2, "◷", MUTED, 0);
    for (int column = x + 2; column < x + width - 2; ++column) text(l, column, table + 1, 1, "─", RULE, 0);

    const int list_top = table + 2;
    const int rows = top + height - list_top - 1;
    if (!l->visible_count) {
        text(l, title_x, list_top, title_width, "No songs here. Esc clears the search.", MUTED, 0);
        return;
    }
    const int stacked = rows / 2 >= 5;
    const int row_height = stacked ? 2 : 1;
    const uint32_t capacity = (uint32_t)max_int(1, rows / row_height);
    uint32_t selection = 0;
    for (uint32_t i = 0; i < l->visible_count; ++i)
        if (l->visible[i] == l->selected) selection = i;
    if (state.first_row + capacity > l->visible_count)
        state.first_row = l->visible_count > capacity ? l->visible_count - capacity : 0;
    if (selection < state.first_row) state.first_row = selection;
    else if (selection >= state.first_row + capacity) state.first_row = selection - capacity + 1;

    static const char *const bars[] = {"▂▅▃", "▅▃▇", "▇▂▅", "▃▇▂", "▆▄▂", "▂▆▇"};
    for (uint32_t row = state.first_row; row < l->visible_count && row - state.first_row < capacity; ++row) {
        const uint32_t index = l->visible[row];
        const ScmpTrack *track = &l->tracks[index];
        const int y = list_top + (int)(row - state.first_row) * row_height;
        const int current = index == l->playing_track;
        if (row == selection && !state.library_focus) l->fill(l, x + 1, y, width - 2, row_height, HOVER);
        char number[16];
        snprintf(number, sizeof(number), "%*u", digits, index + 1);
        if (current && l->playing)
            text(l, number_x, y, digits + 1, bars[(l->frame / 3) % 6], GREEN_TEXT, 0);
        else
            text(l, number_x, y, digits, number, current ? GREEN_TEXT : MUTED, 0);
        text(l, title_x, y, title_width, track->title, current ? GREEN_TEXT : TEXT, stacked ? 0 : SCMP_ATTR_BOLD);
        if (stacked) text(l, title_x, y + 1, title_width, track->artist, MUTED, 0);
        if (album_width) text(l, album_x, y, album_width, track->album, MUTED, 0);
        if (track->duration > 0) {
            char length[16];
            format_time(track->duration, length, sizeof(length));
            const int w = l->text_width(l, length);
            text(l, x + width - 2 - w, y, w, length, MUTED, 0);
        }
    }
}

static void draw_now(ScmpLayout *l, int x, int width, int top, int height, int cover_height)
{
    const ScmpTrack *track = l->playing_track != SCMP_NONE ? &l->tracks[l->playing_track]
                           : l->selected != SCMP_NONE ? &l->tracks[l->selected] : NULL;
    const uint32_t from = l->playing_track != SCMP_NONE ? l->tracks[l->playing_track].playlist : SCMP_NONE;
    const char *heading = from < l->playlist_count ? l->playlists[from].name : "Selected";
    text(l, x + 2, top + 1, width - 4, heading, TEXT, SCMP_ATTR_BOLD);
    if (!track) return;
    const int cover_width = cover_height * 2;
    l->image(l, SCMP_IMAGE_PLAYING, x + (width - cover_width) / 2, top + 3, cover_width, cover_height);
    const int y = top + 3 + cover_height + 1;
    text(l, x + 2, y, width - 4, track->title, TEXT, SCMP_ATTR_BOLD);
    text(l, x + 2, y + 1, width - 4, track->artist, MUTED, 0);
    /* The visualizer fills the rest of the panel; leaving it unfilled shows the wallpaper. */
    const int vis_top = y + 3;
    if (top + height - 1 - vis_top >= 6) l->visualizer(l, x + 1, vis_top, width - 2, top + height - 1 - vis_top);
}

static void draw_bar(ScmpLayout *l)
{
    const int y = l->height - 3;
    const int left_width = min_int(40, l->width * 3 / 10);
    if (l->playing_track != SCMP_NONE) {
        const ScmpTrack *track = &l->tracks[l->playing_track];
        l->image(l, SCMP_IMAGE_PLAYING, 2, y, 6, 3);
        text(l, 10, y, left_width - 10, track->title, TEXT, SCMP_ATTR_BOLD);
        text(l, 10, y + 1, left_width - 10, track->artist, MUTED, 0);
    }
    const int controls = l->width / 2 - 13;
    text(l, controls, y, 2, "⇄", l->shuffle ? GREEN_TEXT : MUTED, SCMP_ATTR_BOLD);
    text(l, controls + 6, y, 2, "|◀", MUTED, SCMP_ATTR_BOLD);
    l->fill(l, controls + 11, y, 4, 1, TEXT);
    text(l, controls + 12, y, 2, l->playing ? "Ⅱ" : "▶", BLACK, SCMP_ATTR_BOLD);
    text(l, controls + 19, y, 2, "▶|", MUTED, SCMP_ATTR_BOLD);
    text(l, controls + 24, y, 3, l->repeat == 2 ? "↻1" : "↻", l->repeat ? GREEN_TEXT : MUTED, SCMP_ATTR_BOLD);

    const int center_width = min_int(70, l->width - 2 * left_width);
    const int center_x = (l->width - center_width) / 2;
    if (center_width >= 24) {
        char elapsed[16], total[16];
        format_time(l->elapsed, elapsed, sizeof(elapsed));
        if (l->has_track) format_time(l->duration, total, sizeof(total));
        else strcpy(total, "-:--");
        const int ew = l->text_width(l, elapsed), tw = l->text_width(l, total);
        const int bar_x = center_x + ew + 2;
        const int bar_width = center_width - ew - tw - 4;
        text(l, center_x, y + 1, ew, elapsed, MUTED, 0);
        text(l, bar_x + bar_width + 2, y + 1, tw, total, MUTED, 0);
        const int fill = l->has_track && l->duration > 0
                             ? max_int(0, min_int(bar_width, (int)(bar_width * l->elapsed / l->duration))) : 0;
        for (int i = 0; i < bar_width; ++i) text(l, bar_x + i, y + 1, 1, "━", i < fill ? TEXT : RULE, 0);
        if (l->has_track) text(l, bar_x + min_int(fill, bar_width - 1), y + 1, 1, "●", TEXT, 0);
    }
    if (l->width >= 90) {
        const int volume_width = 12;
        const int vx = l->width - volume_width - 8;
        const int level = (int)(l->volume * volume_width + 0.5);
        text(l, vx - 4, y + 1, 3, "vol", MUTED, 0);
        for (int i = 0; i < volume_width; ++i) text(l, vx + i, y + 1, 1, "━", i < level ? TEXT : RULE, 0);
        char percent[8];
        snprintf(percent, sizeof(percent), "%d%%", (int)(l->volume * 100 + 0.5));
        text(l, vx + volume_width + 1, y + 1, 5, percent, MUTED, 0);
    }

    const int hints_x = max_int(left_width + 2, center_x);
    if (l->message && *l->message) {
        text(l, hints_x, l->height - 1, l->width - hints_x - 2, l->message, 221, SCMP_ATTR_BOLD);
        return;
    }
    static const char *const hints[][2] = {{"?", "help"}, {"L", "classic"}, {"h/l", "library/songs"},
                                           {"p", "pause"}, {">/<", "next/prev"}, {"f/b", "seek"},
                                           {"+/-", "volume"}, {"/", "search"}, {"q", "quit"}};
    int column = hints_x;
    for (size_t i = 0; i < sizeof(hints) / sizeof(hints[0]); ++i) {
        const int need = l->text_width(l, hints[i][0]) + 1 + l->text_width(l, hints[i][1]) + 3;
        if (column + need > l->width - 2) break;
        column += text(l, column, l->height - 1, need, hints[i][0], MUTED, SCMP_ATTR_BOLD);
        column += text(l, column, l->height - 1, need, " ", FAINT, 0);
        column += text(l, column, l->height - 1, need, hints[i][1], FAINT, 0);
        column += 3;
    }
}

static void layout_draw(ScmpLayout *l)
{
    refresh_library(l);
    const int top = 1;
    const int panel_height = l->height - 5;
    const int sidebar = l->width >= 90;
    const int now_panel = l->width >= 120;
    const int sidebar_width = sidebar ? min_int(32, max_int(24, l->width / 5)) : 0;
    const int now_width = now_panel ? min_int(44, max_int(32, l->width / 4)) : 0;
    const int main_x = 1 + (sidebar ? sidebar_width + 1 : 0);
    const int main_width = l->width - main_x - 1 - (now_panel ? now_width + 1 : 0);
    const int now_x = main_x + main_width + 1;
    const int cover_height = now_panel ? min_int((now_width - 4) / 2, max_int(4, panel_height / 3)) : 0;
    const int vis_top = top + 3 + cover_height + 1 + 3;
    const int vis_height = top + panel_height - 1 - vis_top;
    const int has_visualizer = now_panel && vis_height >= 6;

    /* Black frame and dark panels; the visualizer area stays unpainted for the wallpaper. */
    for (int row = 0; row < l->height; ++row) {
        if (has_visualizer && row >= vis_top && row < vis_top + vis_height) {
            l->fill(l, 0, row, now_x + 1, 1, BLACK);
            l->fill(l, now_x + now_width - 1, row, l->width - now_x - now_width + 1, 1, BLACK);
        } else {
            l->fill(l, 0, row, l->width, 1, BLACK);
        }
    }
    for (int row = top; row < top + panel_height; ++row) {
        if (sidebar) l->fill(l, 1, row, sidebar_width, 1, PANEL);
        l->fill(l, main_x, row, main_width, 1, PANEL);
        if (now_panel) {
            if (has_visualizer && row >= vis_top && row < vis_top + vis_height) {
                l->fill(l, now_x, row, 1, 1, PANEL);
                l->fill(l, now_x + now_width - 1, row, 1, 1, PANEL);
            } else {
                l->fill(l, now_x, row, now_width, 1, PANEL);
            }
        }
    }

    draw_top(l);
    if (sidebar) draw_library(l, 1, sidebar_width, top, panel_height);
    draw_main(l, main_x, main_width, top, panel_height);
    if (now_panel) draw_now(l, now_x, now_width, top, panel_height, cover_height);
    draw_bar(l);
}

static int layout_key(ScmpLayout *l, int key)
{
    refresh_library(l);
    const unsigned count = library_size(l);
    if (key == 'h' && l->width >= 90) {
        state.library_focus = 1;
        return 1;
    }
    if (key == 'l' && state.library_focus) {
        state.library_focus = 0;
        return 1;
    }
    if (!state.library_focus) return 0;
    switch (key) {
    case 'j':
    case SCMP_KEY_DOWN:
        if (state.library_index + 1 < count) ++state.library_index;
        return 1;
    case 'k':
    case SCMP_KEY_UP:
        if (state.library_index > 0) --state.library_index;
        return 1;
    case SCMP_KEY_ESCAPE:
        state.library_focus = 0;
        return 1;
    case SCMP_KEY_ENTER:
        if (state.library_index < l->playlist_count) {
            l->command(l, SCMP_COMMAND_PLAYLIST, state.library_index, NULL);
        } else if (state.library_index - l->playlist_count < state.artist_count) {
            l->command(l, SCMP_COMMAND_SEARCH, 0, state.artists[state.library_index - l->playlist_count].name);
        }
        state.library_focus = 0;
        state.refreshed_once = 0;
        return 1;
    default:
        return 0;
    }
}

const ScmpPluginV2 *scmp_plugin_v2(void)
{
    static const ScmpPluginV2 plugin = {
        SCMP_PLUGIN_V2_ABI_VERSION, "Spotify-style layout", NULL, NULL, NULL, layout_draw, layout_key, 70, 20,
    };
    return &plugin;
}
