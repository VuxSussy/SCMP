#include "Writer/Wallpaper.h"

#include <algorithm>
#include <cmath>
#include <utility>

// Animated pixel wallpapers. Each scene paints RGB pixels that are roughly square: the terminal
// shows two of them per cell with a half block.
namespace
{
using Rgb = uint32_t;
constexpr double pi = 3.14159265358979323846;

double clamp01(double value) { return value < 0 ? 0 : value > 1 ? 1 : value; }

Rgb mix(Rgb from, Rgb to, double amount)
{
    amount = clamp01(amount);
    Rgb out = 0;
    for (int shift = 16; shift >= 0; shift -= 8)
    {
        const double a = (from >> shift) & 0xffu;
        const double b = (to >> shift) & 0xffu;
        out |= static_cast<Rgb>(std::lround(a + (b - a) * amount)) << shift;
    }
    return out;
}

struct Stop
{
    double at;
    Rgb color;
};

template <size_t N>
Rgb gradient(const Stop (&stops)[N], double position)
{
    if (position <= stops[0].at) return stops[0].color;
    for (size_t i = 1; i < N; ++i)
    {
        if (position <= stops[i].at)
        {
            const double span = stops[i].at - stops[i - 1].at;
            return mix(stops[i - 1].color, stops[i].color, span > 0 ? (position - stops[i - 1].at) / span : 1);
        }
    }
    return stops[N - 1].color;
}

uint32_t hash(int x, int y, int z = 0)
{
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u +
                 static_cast<uint32_t>(z) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

double noise(int x, int y, int z = 0) { return (hash(x, y, z) & 0xffffu) / 65535.0; }

int whole(double value) { return static_cast<int>(std::floor(value)); }

// A smooth hill line between 0 and 1.
double ridge(double x, double seed)
{
    return 0.5 + 0.25 * std::sin(x * 1.7 + seed) + 0.15 * std::sin(x * 4.3 + seed * 2.1) +
           0.1 * std::sin(x * 9.1 + seed * 3.7);
}

double twinkle(double seconds, double seed) { return 0.5 + 0.5 * std::sin(seconds * 2.3 + seed * 977.0); }

class Canvas
{
  public:
    Canvas(int width, int height, std::vector<Rgb> &pixels) : w(width), h(height), m_pixels(pixels) {}

    const int w;
    const int h;

    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    void set(int x, int y, Rgb color)
    {
        if (inside(x, y)) m_pixels[index(x, y)] = color;
    }
    Rgb get(int x, int y) const
    {
        return m_pixels[index(std::max(0, std::min(w - 1, x)), std::max(0, std::min(h - 1, y)))];
    }
    void blend(int x, int y, Rgb color, double amount)
    {
        if (inside(x, y)) m_pixels[index(x, y)] = mix(m_pixels[index(x, y)], color, amount);
    }
    void rect(double x0, double y0, double x1, double y1, Rgb color)
    {
        const int left = static_cast<int>(std::lround(x0));
        const int top = static_cast<int>(std::lround(y0));
        const int right = std::max(left + 1, static_cast<int>(std::lround(x1)));
        const int bottom = std::max(top + 1, static_cast<int>(std::lround(y1)));
        for (int y = std::max(0, top); y < std::min(h, bottom); ++y)
            for (int x = std::max(0, left); x < std::min(w, right); ++x) m_pixels[index(x, y)] = color;
    }
    void disc(double cx, double cy, double radius, Rgb color)
    {
        for (int y = whole(cy - radius); y <= whole(cy + radius); ++y)
            for (int x = whole(cx - radius); x <= whole(cx + radius); ++x)
                if (std::hypot(x + 0.5 - cx, y + 0.5 - cy) <= radius) set(x, y, color);
    }
    // Draws rows of characters scaled by s; '.' is transparent.
    template <size_t N, typename Palette>
    void sprite(int x, int y, int s, const char *const (&rows)[N], Palette palette)
    {
        for (size_t row = 0; row < N; ++row)
            for (int col = 0; rows[row][col]; ++col)
                if (rows[row][col] != '.')
                    rect(x + col * s, y + static_cast<int>(row) * s, x + (col + 1) * s,
                         y + static_cast<int>(row + 1) * s, palette(rows[row][col]));
    }

  private:
    size_t index(int x, int y) const { return static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x); }
    std::vector<Rgb> &m_pixels;
};

int unit(const Canvas &c) { return std::max(1, c.h / 56); }

// A soft cloud made of overlapping ellipses, lit from below. Pixels outside the clip box are kept.
void cloud(Canvas &c, double cx, double cy, double size, Rgb top, Rgb bottom, int x0, int y0, int x1, int y1)
{
    static const double lobes[][3] = {{-0.55, 0.15, 0.45}, {-0.15, -0.15, 0.55}, {0.3, -0.05, 0.5}, {0.65, 0.18, 0.4}};
    for (int y = std::max(y0, whole(cy - size)); y < std::min(y1, whole(cy + size * 0.6)); ++y)
    {
        for (int x = std::max(x0, whole(cx - size * 1.2)); x < std::min(x1, whole(cx + size * 1.2)); ++x)
        {
            bool hit = false;
            for (const auto &lobe : lobes)
            {
                const double dx = (x - cx) / size - lobe[0];
                const double dy = (y - cy) / size - lobe[1];
                hit = hit || dx * dx + dy * dy * 2.2 < lobe[2] * lobe[2];
            }
            if (hit) c.set(x, y, mix(top, bottom, (y - (cy - size * 0.5)) / (size * 0.9)));
        }
    }
}

void stars(Canvas &c, double seconds, int s, int height, double amount)
{
    if (amount <= 0) return;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < c.w; ++x)
        {
            const double n = noise(x / s, y / s, 1);
            if (n > 0.988 && x % s == 0 && y % s == 0)
                c.blend(x, y, 0xfff6dcu, amount * (0.35 + 0.65 * twinkle(seconds, n)) * (1.0 - 0.6 * y / height));
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Cat Window: a cat watches the dusk from a windowsill.

void cat_window(Canvas &c, double t)
{
    const int s = unit(c);
    const int wx0 = c.w * 14 / 100, wx1 = c.w * 86 / 100;
    const int wy0 = c.h * 9 / 100, wy1 = c.h * 70 / 100;
    const int frame = 2 * s;
    const int sill = wy1 + frame;
    const double ww = wx1 - wx0, wh = wy1 - wy0;
    static const Stop sky[] = {{0, 0x161a42}, {0.45, 0x4b3a78}, {0.8, 0xc4687a}, {1, 0xf0a070}};
    const double horizon = 0.8;

    for (int y = 0; y < c.h; ++y)
    {
        for (int x = 0; x < c.w; ++x)
        {
            Rgb color;
            if (x < wx0 || x >= wx1 || y < wy0 || y >= wy1)
            {
                color = (x / (4 * s)) % 2 ? 0x241a28u : 0x1f1624u;
                if (y >= sill) color = y < sill + 2 * s ? 0x6e4632u : y < sill + 3 * s ? 0x4a2e22u : 0x1a1218u;
            }
            else
            {
                const double u = (x - wx0) / ww, v = (y - wy0) / wh;
                color = gradient(sky, v / horizon);
                const double moon = std::hypot((u - 0.72) * ww, (v - 0.24) * wh) / wh;
                if (moon < 0.075)
                    color = noise(x / (2 * s), y / (2 * s), 9) > 0.8 ? 0xe2d6b4u : 0xf6eccau;
                else if (moon < 0.2)
                    color = mix(color, 0x9a86b8u, 0.45 * (1 - (moon - 0.075) / 0.125));
                const double far = horizon - 0.1 * ridge(u * 3, 1.3);
                const double near = horizon + 0.06 - 0.07 * ridge(u * 5 + 2, 4.1);
                if (v > far) color = 0x2e2652;
                if (v > near)
                {
                    color = 0x1a1634;
                    const int cx = x / (2 * s), cy = y / (2 * s);
                    if (v > near + 0.03 && noise(cx, cy, 7) > 0.93 && noise(cx, cy, whole(t / 3) + 20) > 0.12)
                        color = 0xffcf70;
                }
            }
            c.set(x, y, color);
        }
    }
    stars(c, t, s, wy0 + static_cast<int>(wh * 0.5), 1.0);
    // Stars only belong to the view through the window.
    for (int y = 0; y < wy0; ++y)
        for (int x = 0; x < c.w; ++x) c.set(x, y, (x / (4 * s)) % 2 ? 0x241a28u : 0x1f1624u);
    for (int y = wy0; y < wy1; ++y)
        for (int x = 0; x < c.w; ++x)
            if (x < wx0 || x >= wx1) c.set(x, y, (x / (4 * s)) % 2 ? 0x241a28u : 0x1f1624u);

    for (int k = 0; k < 3; ++k)
    {
        const double size = wh * (0.07 + 0.02 * k);
        const double span = ww + size * 4;
        const double cx = wx0 - size * 2 + std::fmod(t * (1.5 + k) * s + k * ww * 0.37, span);
        cloud(c, cx, wy0 + wh * (0.18 + 0.15 * k), size, 0x5e4a86, 0xb46a86, wx0, wy0, wx1, wy1);
    }
    // A shooting star now and then.
    const double streak = std::fmod(t, 13.0);
    if (t > 0 && streak < 1.2)
    {
        const double p = streak / 1.2;
        for (int i = 0; i < 10 * s; ++i)
        {
            const int x = whole(wx0 + ww * (0.15 + 0.4 * p) - i);
            const int y = whole(wy0 + wh * (0.08 + 0.18 * p) - i * 0.45);
            if (x >= wx0 && x < wx1 && y >= wy0 && y < wy1) c.blend(x, y, 0xffffff, 1.0 - i / (10.0 * s));
        }
    }

    const Rgb wood = 0x3a2620;
    c.rect(wx0 - frame, wy0 - frame, wx1 + frame, wy0, wood);
    c.rect(wx0 - frame, wy1, wx1 + frame, sill, wood);
    c.rect(wx0 - frame, wy0, wx0, wy1, wood);
    c.rect(wx1, wy0, wx1 + frame, wy1, wood);
    c.rect((wx0 + wx1) / 2 - s / 2.0, wy0, (wx0 + wx1) / 2 + s / 2.0 + (s == 1 ? 1 : 0), wy1, wood);
    c.rect(wx0, wy0 + wh * 0.45, wx1, wy0 + wh * 0.45 + s, wood);

    // Curtains sway on both sides of the window.
    c.rect(c.w * 3 / 100, wy0 - 4 * s, c.w * 97 / 100, wy0 - 3 * s, 0x8a6a40);
    for (int y = wy0 - 3 * s; y < sill + s; ++y)
    {
        const double sway = std::sin(t * 0.9 + y * 0.06) * 1.5 * s;
        const int left = whole(c.w * 0.2 + sway * (y - wy0 + 3 * s) / (sill - wy0));
        const int right = whole(c.w * 0.8 - sway * (y - wy0 + 3 * s) / (sill - wy0));
        for (int x = c.w * 4 / 100; x < c.w; ++x)
        {
            if (x >= left && x < right) continue;
            if (x >= c.w * 96 / 100) break;
            c.set(x, y, mix(0x5a2236, 0x8a3a4e, 0.5 + 0.5 * std::sin(x * 1.1 / s)));
        }
    }

    static const char *const cat[] = {
        "....XXXXXXXX....", "...XXXXXXXXXX...", "...XXXXXXXXXX...", "...XXXXXXXXXX...", "....XXXXXXXX....",
        ".....XXXXXX.....", "....XXXXXXXX....", "...XXXXXXXXXX...", "..XXXXXXXXXXXX..", "..XXXXXXXXXXXX..",
        ".XXXXXXXXXXXXXX.", ".XXXXXXXXXXXXXX.", ".XXXXXXXXXXXXXX.", ".XXXXXXXXXXXXXX.", "..XXXXXXXXXXXX..",
    };
    const Rgb fur = 0x0e0a12;
    const int catx = whole(c.w * 0.42) - 8 * s;
    const int caty = sill - 15 * s;
    c.sprite(catx, caty, s, cat, [&](char) { return fur; });
    const bool twitch = t > 0 && std::fmod(t, 7.0) < 0.25;
    for (int side = 0; side < 2; ++side)
    {
        const int ear = catx + (side ? 10 : 4) * s + (side && twitch ? s : 0);
        c.rect(ear - s, caty - s, ear + 2 * s, caty, fur);
        c.rect(ear + (side ? s : 0), caty - 2 * s, ear + (side ? 2 : 1) * s, caty - s, fur);
    }
    const int steps = 24 * s;
    for (int i = 0; i <= steps; ++i)
    {
        const double p = static_cast<double>(i) / steps;
        const double curl = p > 0.55 ? (p - 0.55) / 0.45 : 0.0;
        const double tx = catx + 14 * s + p * 9 * s + std::sin(t * 1.8) * curl * 2.5 * s;
        const double ty = sill - std::pow(curl, 1.5) * 6 * s;
        c.rect(tx, ty - s, tx + s, ty, fur);
    }
}

// ---------------------------------------------------------------------------------------------
// Torii Gate: a gate in the water at sunset with falling petals.

void torii_gate(Canvas &c, double cx, double top, double base, double g)
{
    const Rgb red = 0xd8362c, shade = 0xa52222, black = 0x1e1216;
    for (int side = -1; side <= 1; side += 2)
    {
        const double px = cx + side * 7 * g;
        c.rect(px - g, top + 3 * g, px + g, base, red);
        c.rect(px + 0.3 * g, top + 3 * g, px + g, base, shade);
        c.rect(px - 1.2 * g, base - 2.5 * g, px + 1.2 * g, base, black);
    }
    c.rect(cx - 10 * g, top + 6.5 * g, cx + 10 * g, top + 8 * g, red);
    c.rect(cx - g, top + 3 * g, cx + g, top + 6.5 * g, red);
    c.rect(cx - 11 * g, top + 1.6 * g, cx + 11 * g, top + 3 * g, red);
    c.rect(cx - 12 * g, top, cx + 12 * g, top + 1.6 * g, black);
    c.rect(cx - 13 * g, top - 0.8 * g, cx - 11 * g, top + 0.8 * g, black);
    c.rect(cx + 11 * g, top - 0.8 * g, cx + 13 * g, top + 0.8 * g, black);
}

void torii(Canvas &c, double t)
{
    const int s = unit(c);
    const double hz = c.h * 0.64;
    static const Stop sky[] = {{0, 0x241a4a}, {0.4, 0x6a2d6a}, {0.75, 0xd6506a}, {1, 0xf7a35c}};
    const double sx = c.w * 0.5, sy = hz - c.h * 0.08, sr = c.h * 0.13;
    const double peakx = c.w * 0.2, peaky = hz - c.h * 0.32;
    for (int y = 0; y < whole(hz); ++y)
    {
        for (int x = 0; x < c.w; ++x)
        {
            Rgb color = gradient(sky, y / hz);
            const double d = std::hypot(x - sx, y - sy);
            if (d < sr)
                color = mix(0xffe0a0, 0xff8a50, (y - (sy - sr)) / (2 * sr));
            else if (d < sr * 2.5)
                color = mix(color, 0xffb070, 0.3 * (1 - (d - sr) / (sr * 1.5)));
            const double slope = y - peaky - std::abs(x - peakx) * 0.55;
            if (slope > 0)
                color = y - peaky < c.h * 0.07 + noise(x / s, 0, 4) * 2 * s ? 0xe6dcefu : 0x3a2552u;
            if (y > hz - c.h * 0.06 * ridge(x * 6.0 / c.w, 2.0) && x > c.w * 0.55) color = 0x2a1c44;
            c.set(x, y, color);
        }
    }
    stars(c, t, s, whole(hz * 0.35), 0.7);
    const double g = c.h * 0.43 / 22;
    const double gx = c.w * 0.62, top = hz - c.h * 0.36, base = hz + c.h * 0.07;
    torii_gate(c, gx, top, base, g);
    for (int y = whole(hz); y < c.h; ++y)
    {
        const double depth = (y - hz) / (c.h - hz);
        const int dx = static_cast<int>(std::lround(std::sin(y * 0.9 / s + t * 2.4) * s * (1 + depth * 2)));
        for (int x = 0; x < c.w; ++x)
        {
            Rgb color = mix(c.get(x + dx, whole(2 * hz - y)), 0x1a1236, 0.45 + 0.35 * depth);
            if (noise(x / (3 * s), y / s, whole(t * 3)) > 0.97) color = mix(color, 0xffc890, 0.5);
            c.set(x, y, color);
        }
    }
    torii_gate(c, gx, top, base, g);
    for (int y = whole(base); y < whole(base) + s; ++y)
        for (int x = whole(gx - 9 * g); x < whole(gx + 9 * g); ++x)
            if (noise(x / s, whole(t * 2), 5) > 0.6) c.blend(x, y, 0xffc890, 0.4);

    for (int i = 0; i < 40; ++i)
    {
        const double fall = 3 + noise(i, 1, 3) * 4;
        const double px = std::fmod(noise(i, 2, 3) * c.w + t * (5 + noise(i, 3, 3) * 6) * s, c.w);
        const double py = std::fmod(noise(i, 4, 3) * c.h + t * fall * s, c.h * 0.95) + std::sin(t * 1.5 + i) * 2 * s;
        c.rect(px, py, px + s, py + s, i % 3 ? 0xffb7c5u : 0xf28cabu);
    }
}

// ---------------------------------------------------------------------------------------------
// Sunset: the sun sinks into the sea while birds cross the sky.

void sunset(Canvas &c, double t)
{
    const int s = unit(c);
    const double hz = c.h * 0.62;
    static const Stop sky[] = {{0, 0x2a1a4c}, {0.35, 0x6a2c6a}, {0.65, 0xd8506a}, {0.85, 0xff8a45}, {1, 0xffc46a}};
    const double sx = c.w * 0.5, sy = hz - c.h * 0.03, sr = c.h * 0.17;
    for (int y = 0; y < whole(hz); ++y)
    {
        for (int x = 0; x < c.w; ++x)
        {
            Rgb color = gradient(sky, y / hz);
            const double d = std::hypot(x - sx, y - sy);
            const double rel = (y - sy) / sr;
            const double period = std::max(3, 4 * s);
            const bool slit = rel > -0.15 && std::fmod(y - sy + 1000 * period + t * s * 1.5, period) < (rel + 0.15) * period * 0.55;
            if (d < sr && !slit)
                color = mix(0xfff1a8, 0xff6a3a, (y - (sy - sr)) / (2 * sr));
            else if (d >= sr && d < sr * 2.2)
                color = mix(color, 0xffb070, 0.3 * (1 - (d - sr) / (sr * 1.2)));
            const double island = 1 - std::pow((x - c.w * 0.82) / (c.w * 0.1), 2);
            if (island > 0 && y > hz - c.h * 0.05 * island) color = 0x1e1230;
            c.set(x, y, color);
        }
    }
    stars(c, t, s, whole(hz * 0.25), 0.5);
    for (int k = 0; k < 4; ++k)
    {
        const double size = c.h * (0.05 + 0.015 * k);
        const double span = c.w + size * 6;
        const double cx = -size * 3 + std::fmod(t * (1 + k * 0.6) * s + k * c.w * 0.31, span);
        const double cy = c.h * (0.12 + 0.09 * k);
        for (int y = whole(cy - size * 0.25); y < whole(cy + size * 0.25); ++y)
            for (int x = whole(cx - size * 3); x < whole(cx + size * 3); ++x)
            {
                const double dx = (x - cx) / (size * 3), dy = (y - cy) / (size * 0.25);
                if (dx * dx + dy * dy < 1 && y < hz) c.set(x, y, mix(0xff9a8a, 0x7a3a72, (dy + 1) / 2));
            }
    }
    for (int i = 0; i < 4; ++i)
    {
        const double bx = c.w + 20 * s - std::fmod(t * (9 + i * 2) * s + i * c.w * 0.27, c.w + 40.0 * s);
        const double by = c.h * (0.2 + 0.06 * i) + std::sin(t * 1.3 + i) * 2 * s;
        const int wing = std::sin(t * 7 + i * 1.7) > 0 ? -1 : 1;
        const Rgb ink = 0x2a1428;
        c.rect(bx, by, bx + s, by + s, ink);
        for (int side = -1; side <= 1; side += 2)
        {
            c.rect(bx + side * s, by + (wing < 0 ? -s : 0), bx + side * s + s, by + (wing < 0 ? 0 : s), ink);
            c.rect(bx + side * 2 * s, by + wing * s, bx + side * 2 * s + s, by + wing * s + s, ink);
        }
    }
    for (int y = whole(hz); y < c.h; ++y)
    {
        const double depth = (y - hz) / (c.h - hz);
        const double width = sr * (0.5 + depth * 0.9);
        for (int x = 0; x < c.w; ++x)
        {
            Rgb color = mix(0x7a3a6a, 0x1c1236, std::pow(depth, 0.6));
            if (std::sin(x * 0.2 / s + y * 1.3 / s - t * 1.2) > 0.93) color = mix(color, 0x9a4a7a, 0.6);
            const double off = std::abs(x - sx) / width;
            if (off < 1 && noise(x / (2 * s), y / s, whole(t * 5) + y) > 0.4 + off * 0.45) color = mix(color, 0xffb060, 0.85);
            else if (noise(x / s, y / s, whole(t * 2)) > 0.996) color = 0xf0a0a0;
            c.set(x, y, color);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Day Drive: a car drives from morning through the afternoon into the night.

struct Daylight
{
    double at;
    Rgb top, horizon;
    double light;
};

void day_drive(Canvas &c, double t)
{
    const int s = unit(c);
    static const Daylight keys[] = {
        {0.00, 0x3a4a8a, 0xffb08a, 0.55}, {0.10, 0x4f9ae6, 0xcde8ff, 1.0}, {0.35, 0x3f86dc, 0xbfe0f8, 1.0},
        {0.50, 0x5a6ac0, 0xffc070, 0.85}, {0.60, 0x3a2a6a, 0xff6a4a, 0.55}, {0.70, 0x0c1230, 0x2a2658, 0.18},
        {0.92, 0x0a0f28, 0x1c2048, 0.15}, {1.00, 0x3a4a8a, 0xffb08a, 0.55}};
    const double phase = std::fmod(t / 72.0 + 0.04, 1.0);
    size_t k = 1;
    while (k + 1 < sizeof(keys) / sizeof(keys[0]) && phase > keys[k].at) ++k;
    const double f = (phase - keys[k - 1].at) / (keys[k].at - keys[k - 1].at);
    const Rgb top = mix(keys[k - 1].top, keys[k].top, f);
    const Rgb low = mix(keys[k - 1].horizon, keys[k].horizon, f);
    const double light = keys[k - 1].light + (keys[k].light - keys[k - 1].light) * f;
    const double night = clamp01((0.6 - light) / 0.4);
    const auto lit = [&](Rgb color) { return mix(mix(0, color, 0.22 + 0.78 * light), 0x0a1030, (1 - light) * 0.3); };

    const double hz = c.h * 0.62;
    const double road = c.h * 0.8, road_end = c.h * 0.93;
    for (int y = 0; y < c.h; ++y)
        for (int x = 0; x < c.w; ++x) c.set(x, y, mix(top, low, y / hz));
    stars(c, t, s, whole(hz * 0.8), night);

    if (phase < 0.64)
    {
        const double q = (phase + 0.02) / 0.66;
        const double x = c.w * (0.08 + 0.84 * q), y = hz - std::sin(pi * q) * c.h * 0.5;
        for (int r = 3; r > 0; --r) c.disc(x, y, c.h * 0.06 * (1 + r * 0.4), mix(c.get(whole(x), whole(y)), 0xfff0b0, 0.12));
        c.disc(x, y, c.h * 0.06, mix(0xfff4c0, 0xff8040, 1 - std::sin(pi * q)));
    }
    if (phase > 0.62)
    {
        const double q = (phase - 0.62) / 0.38;
        const double x = c.w * (0.08 + 0.84 * q), y = hz - std::sin(pi * q) * c.h * 0.45;
        c.disc(x, y, c.h * 0.05, 0xeef0ff);
        c.disc(x + c.h * 0.02, y - c.h * 0.01, c.h * 0.012, 0xc8cce0);
    }
    for (int i = 0; i < 3; ++i)
    {
        const double size = c.h * (0.05 + 0.01 * i);
        const double cx = -size * 2 + std::fmod(t * 3 * s + i * c.w * 0.4, c.w + size * 4);
        cloud(c, cx, c.h * (0.12 + 0.1 * i), size, lit(0xffffff), lit(0xc8d4e8), 0, 0, c.w, whole(hz));
    }

    const double far = t * 2 * s, hills = t * 7 * s, side = t * 22 * s, lane = t * 40 * s;
    for (int x = 0; x < c.w; ++x)
    {
        const double mountain = hz - c.h * 0.16 * ridge((x + far) / (60.0 * s), 1.0);
        const double hill = hz + c.h * 0.03 - c.h * 0.09 * ridge((x + hills) / (35.0 * s), 3.0);
        for (int y = whole(mountain); y < whole(road); ++y)
        {
            Rgb color = mix(lit(0x6a7fb0), mix(top, low, 0.8), 0.35);
            if (y >= hill) color = lit(0x4a8a4a);
            if (y >= hz + c.h * 0.08) color = lit(((x + whole(side)) / (3 * s)) % 2 ? 0x5aa04au : 0x529848u);
            c.set(x, y, color);
        }
        for (int y = whole(road); y < c.h; ++y)
        {
            Rgb color = lit(0x44444c);
            if (y < road + s || (y >= road_end - s && y < road_end)) color = lit(0xd8d8d0);
            const double mid = (road + road_end) / 2;
            if (y >= mid - s / 2.0 && y < mid + s / 2.0 + (s == 1 ? 1 : 0) &&
                std::fmod(x + lane, 16.0 * s) < 8 * s)
                color = lit(0xf0d060);
            if (y >= road_end) color = lit(0x3a7a3a);
            c.set(x, y, color);
        }
    }

    const double spacing = 26.0 * s;
    for (int i = whole(side / spacing) - 1; i < whole((side + c.w) / spacing) + 2; ++i)
    {
        const double x = i * spacing - side;
        if (i % 3 == 0)
        {
            const double pole = road - 22 * s;
            if (night > 0)
            {
                for (int y = whole(pole + s); y < whole(road_end); ++y)
                {
                    const double spread = (y - pole) * 0.35;
                    for (int px = whole(x - spread); px <= whole(x + spread); ++px) c.blend(px, y, 0xffe08a, night * 0.25);
                }
            }
            c.rect(x, pole, x + s, road, lit(0x55555c));
            c.rect(x - 2 * s, pole, x + 2 * s, pole + s, night > 0.3 ? 0xffe08au : lit(0x777780));
        }
        else if (noise(i, 0, 11) > 0.3)
        {
            const double h = (8 + noise(i, 1, 11) * 6) * s;
            c.rect(x, road - h * 0.4, x + s, road, lit(0x5a3a22));
            c.disc(x + s / 2.0, road - h * 0.6, h * 0.35, lit(noise(i, 2, 11) > 0.5 ? 0x2f7a3au : 0x3a8a3au));
        }
    }

    static const char *const car[] = {
        "..........RRRRRRRRRRRR..........", "........RRBBBBBRRBBBBBBR........", ".......RRBBBBBBRRBBBBBBBR.......",
        "......RRBBBBBBBRRBBBBBBBBR......", "..RRRRRRRRRRRRRRRRRRRRRRRRRRRR..", ".RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRL",
        "TRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRL", "TRRRRRRrrrrrRRRRRRRRRRrrrrrRRRRR", ".DDDDDDDDDDDDDDDDDDDDDDDDDDDDDD.",
    };
    const int bob = std::sin(t * 9) > 0.6 ? 1 : 0;
    const int carx = whole(c.w * 0.28);
    const int cary = whole((road + road_end) / 2) - 10 * s + bob;
    const double paint = 0.5 + 0.5 * light;
    if (night > 0)
    {
        const double hx = carx + 32 * s, hy = cary + 6 * s;
        for (int x = whole(hx); x < whole(hx + c.w * 0.3); ++x)
        {
            const double d = x - hx;
            const double spread = s + d * 0.18;
            for (int y = whole(hy - spread * 0.4); y < whole(hy + spread); ++y)
                c.blend(x, y, 0xfff0b0, night * 0.5 * (1 - d / (c.w * 0.3)));
        }
    }
    c.sprite(carx, cary, s, car, [&](char key) -> Rgb {
        switch (key)
        {
        case 'B': return mix(0x2a3a5a, 0x9fd8ff, light);
        case 'r': return mix(0, 0xa83028, paint);
        case 'T': return night > 0.3 ? 0xff3030u : 0xb02020u;
        case 'L': return night > 0.3 ? 0xffffd0u : 0xfff0b0u;
        case 'D': return 0x30303a;
        default: return mix(0, 0xd8433a, paint);
        }
    });
    for (int wheel = 0; wheel < 2; ++wheel)
    {
        const double wx = carx + (wheel ? 24 : 8) * s + s / 2.0, wy = cary + 9 * s;
        c.disc(wx, wy, 2.6 * s, 0x161616);
        c.disc(wx, wy, 1.1 * s, 0x9a9aa0);
        const double spin = -t * 12;
        c.rect(wx + std::cos(spin) * 1.8 * s - s / 2.0, wy + std::sin(spin) * 1.8 * s - s / 2.0,
               wx + std::cos(spin) * 1.8 * s + s / 2.0, wy + std::sin(spin) * 1.8 * s + s / 2.0, 0x5a5a60);
    }
}

// ---------------------------------------------------------------------------------------------
// Lighthouse: a beam sweeps over the night sea.

void lighthouse(Canvas &c, double t)
{
    const int s = unit(c);
    const double hz = c.h * 0.66;
    static const Stop sky[] = {{0, 0x070b22}, {0.6, 0x14204a}, {1, 0x2a3a6a}};
    const double mx = c.w * 0.18, my = c.h * 0.18, mr = c.h * 0.06;
    for (int y = 0; y < c.h; ++y)
    {
        for (int x = 0; x < c.w; ++x)
        {
            Rgb color;
            if (y < hz)
            {
                color = gradient(sky, y / hz);
            }
            else
            {
                const double depth = (y - hz) / (c.h - hz);
                color = mix(0x16284a, 0x070c1e, depth);
                const double wave = std::sin(x * 0.18 / s - t * 1.6 + (y - hz) * 0.9 / s) *
                                    std::sin(x * 0.05 / s + t * 0.7 + y * 0.3 / s);
                if (wave > 0.55) color = mix(color, 0x4a6a9a, 0.6);
                if (std::abs(x - mx) < mr * (0.6 + depth) && noise(x / s, y / s, whole(t * 4)) > 0.7)
                    color = mix(color, 0xc8d0e8, 0.6);
            }
            c.set(x, y, color);
        }
    }
    stars(c, t, s, whole(hz * 0.85), 1.0);
    c.disc(mx, my, mr, 0xeef0ff);
    c.disc(mx + mr * 0.45, my - mr * 0.2, mr * 0.85, gradient(sky, my / hz));

    const auto cliff = [&](int x) {
        return hz - c.h * 0.1 * clamp01((x - c.w * 0.58) / (c.w * 0.08)) - c.h * 0.03 * ridge(x / (9.0 * s), 5.0) *
               clamp01((x - c.w * 0.58) / (c.w * 0.04));
    };
    const double lx = c.w * 0.76;
    const double ground = cliff(whole(lx));
    const double tower = c.h * 0.3;
    const double ly = ground - tower - 2 * s;
    const double angle = t * 1.1 + 0.4;
    const double reach = std::cos(angle);
    const int dir = reach > 0 ? 1 : -1;
    const double length = std::abs(reach) * c.w * 0.9;
    for (int y = 0; y < c.h; ++y)
    {
        for (int x = 0; x < c.w; ++x)
        {
            const double dx = (x - lx) * dir;
            if (dx <= 0 || dx >= length) continue;
            const double spread = dx * 0.1 + s * 1.5;
            const double dy = std::abs(y - ly);
            if (dy < spread) c.blend(x, y, 0xfff6c8, 0.55 * (1 - dx / length) * (1 - dy / spread * 0.6));
        }
    }
    for (int x = whole(c.w * 0.58); x < c.w; ++x)
        for (int y = whole(cliff(x)); y < c.h; ++y) c.set(x, y, y < cliff(x) + s ? 0x24202eu : 0x15131cu);

    for (int y = whole(ground - tower); y < whole(ground); ++y)
    {
        const double p = (ground - y) / tower;
        const double half = (3.5 - 1.2 * p) * s;
        const bool red = static_cast<int>(p * 6) % 2 == 1;
        for (int x = whole(lx - half); x < whole(lx + half); ++x)
        {
            const Rgb paint = red ? 0xc8382e : 0xe8e4dc;
            c.set(x, y, x > lx + half / 3 ? mix(paint, 0, 0.35) : paint);
        }
    }
    const double glow = 1 - std::abs(reach);
    c.rect(lx - 3.5 * s, ground - tower - s, lx + 3.5 * s, ground - tower, 0x2a2a30);
    c.rect(lx - 2 * s, ly - 1.5 * s, lx + 2 * s, ly + 1.5 * s, mix(0xffe490, 0xffffff, glow));
    for (int i = 0; i < 3 * s; ++i) c.rect(lx - 2.5 * s + i * 0.8, ly - 1.5 * s - i - 1, lx + 2.5 * s - i * 0.8, ly - 1.5 * s - i, 0x7a1e1e);
    if (glow > 0.6) c.disc(lx, ly, (glow - 0.6) * 10 * s, 0xfff6c8);
}

// ---------------------------------------------------------------------------------------------
// Rainy City: rain over a skyline of flickering windows.

void rainy_city(Canvas &c, double t)
{
    const int s = unit(c);
    const double street = c.h * 0.82;
    const double beat = std::fmod(t, 11.0);
    const double flash = t <= 0 ? 0 : beat < 0.12 ? 0.6 : beat > 0.22 && beat < 0.3 ? 0.4 : 0;
    for (int y = 0; y < whole(street); ++y)
        for (int x = 0; x < c.w; ++x) c.set(x, y, mix(mix(0x0e1220, 0x2a3044, y / street), 0x9aa8c8, flash));

    for (int layer = 0; layer < 2; ++layer)
    {
        int index = 0;
        for (int bx = -whole(noise(layer, 0, 6) * 10 * s); bx < c.w; ++index)
        {
            const int width = (8 + static_cast<int>(hash(index, layer, 7) % 14)) * s;
            const double height = c.h * (layer ? 0.18 + 0.32 * noise(index, layer, 8) : 0.3 + 0.35 * noise(index, layer, 8));
            const Rgb wall = layer ? 0x161a26 : mix(0x1c2232, 0x2a3044, 0.5);
            const int roof = whole(street - height);
            c.rect(bx, roof, bx + width - (layer ? s : 0), street, wall);
            for (int wy = roof + 2 * s; wy < whole(street) - 2 * s; wy += 3 * s)
            {
                for (int wx = bx + s; wx + 2 * s <= bx + width - s; wx += 3 * s)
                {
                    const double seed = noise(wx, wy, 12 + layer);
                    const bool on = seed > 0.5 && noise(wx, wy, whole(t / 5 + seed * 5) + 30) > 0.2;
                    const Rgb glass = !on ? 0x1e2434u : seed > 0.8 ? 0x8fd0ffu : 0xffc96au;
                    c.rect(wx, wy, wx + 2 * s, wy + 2 * s, layer ? glass : mix(glass, wall, 0.55));
                }
            }
            if (layer && index % 5 == 2 && std::fmod(t, 3.0) > 0.4)
                c.rect(bx + s, roof - 3 * s, bx + width - 2 * s, roof - s, 0xff4fa0);
            bx += width;
        }
    }
    for (int y = whole(street); y < c.h; ++y)
    {
        const int dx = static_cast<int>(std::lround(std::sin(y * 0.8 / s + t * 3) * s));
        for (int x = 0; x < c.w; ++x)
        {
            Rgb color = mix(c.get(x + dx, whole(2 * street - y)), 0x0a0d16, 0.55);
            if (noise(x / s, whole(t * 8), y) > 0.992) color = 0xb0c4e0;
            c.set(x, y, color);
        }
    }
    for (int y = 0; y < c.h; ++y)
    {
        for (int x = 0; x < c.w; ++x)
        {
            const int column = (x + y * 35 / 100) / s;
            const uint32_t drop = hash(column, 0, 3);
            if (drop % 4) continue;
            const double speed = (40.0 + drop % 30) * s;
            if (std::fmod(y + t * speed + (drop >> 8) % 200, 30.0 * s) < 3 * s) c.blend(x, y, 0x9ab0d0, 0.45);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// The original still pixel scenes, drawn in 256-color palette indexes.

int legacy_color(const std::string &scene, int col, int row, int width, int height)
{
    const double x = (col + 0.5) / width;
    const double y = (row + 0.5) / height;
    const uint32_t grain = (static_cast<uint32_t>(col) * 374761393u) ^ (static_cast<uint32_t>(row) * 668265263u);
    const bool star = (grain ^ (grain >> 13)) % 79 == 0;
    if (scene == "night-drive")
    {
        const double sun = std::hypot((x - 0.52) * width / (2.0 * height), y - 0.31);
        if (y > 0.57)
        {
            const double road = std::abs(x - 0.52);
            const double spread = (y - 0.57) * 0.76;
            if (std::abs(road - spread) < 0.012) return 60;
            if (road < spread) return road < 0.008 && row % 3 == 0 ? 97 : 235;
            return row % 3 == 0 ? 54 : 17;
        }
        if (y > 0.41 + 0.13 * (grain >> 6 & 7) / 7.0 && std::abs(x - 0.52) > 0.20)
        {
            return star ? 103 : 235;
        }
        if (sun < 0.24 && (y < 0.32 || row % 3 != 0))
        {
            return y < 0.26 ? 126 : y < 0.36 ? 90 : 54;
        }
        return star ? 61 : (y < 0.23 ? 17 : y < 0.45 ? 53 : 54);
    }
    if (scene == "alpine")
    {
        const double peaks = 0.54 - 0.28 * std::max({0.0, 1 - std::abs(x - 0.27) * 4.8, 1 - std::abs(x - 0.73) * 4.0});
        if (y > 0.66) return (row + static_cast<int>(grain % 3)) % 5 == 0 ? 24 : 17;
        if (y > 0.57 - (std::max(0.0, 1 - std::abs(x - 0.50) * 3.5) * 0.12)) return 23;
        if (y > peaks) return y < peaks + 0.08 ? 67 : 24;
        return star ? 67 : (y < 0.23 ? 17 : 18);
    }
    if (scene == "orbit")
    {
        const double dx = (x - 0.73) * width / (2.0 * height);
        const double dy = y - 0.51;
        const double planet = std::hypot(dx, dy);
        const double ring = std::hypot(dx, (dy + 0.26 * dx) * 3.2);
        if (ring > 0.27 && ring < 0.40 && (planet > 0.20 || dy > 0)) return ring > 0.34 ? 59 : 60;
        if (planet < 0.20) return dx - dy < -0.05 ? 60 : (dx - dy < 0.07 ? 54 : 17);
        return star ? 103 : (y < 0.34 ? 234 : 17);
    }
    return -1;
}
}

uint32_t scmp::ansi_rgb(int index)
{
    if (index >= 232)
    {
        return static_cast<uint32_t>(8 + (index - 232) * 10) * 0x010101u;
    }
    const int cube = std::max(0, index - 16);
    static const uint32_t values[] = {0, 95, 135, 175, 215, 255};
    return (values[cube / 36] << 16) | (values[(cube / 6) % 6] << 8) | values[cube % 6];
}

bool scmp::render_scene(const std::string &scene, int width, int height, double seconds, std::vector<uint32_t> &pixels)
{
    using Painter = void (*)(Canvas &, double);
    static const std::pair<const char *, Painter> painters[] = {
        {"cat-window", cat_window}, {"torii", torii},           {"sunset", sunset},
        {"day-drive", day_drive},   {"lighthouse", lighthouse}, {"rainy-city", rainy_city},
    };
    if (width <= 0 || height <= 0) return false;
    pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    Canvas canvas(width, height, pixels);
    for (const auto &painter : painters)
    {
        if (scene == painter.first)
        {
            painter.second(canvas, seconds);
            return true;
        }
    }
    if (legacy_color(scene, 0, 0, width, std::max(1, height / 2)) < 0) return false;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            canvas.set(x, y, ansi_rgb(legacy_color(scene, x, y / 2, width, std::max(1, height / 2))));
    return true;
}
