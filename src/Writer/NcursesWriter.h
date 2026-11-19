#ifndef _NCURSES_WRITER_H
#define _NCURSES_WRITER_H

#ifdef NCURSESW
#include <ncursesw/ncurses.h>
#else
#include <ncurses.h>
#endif

#include "Domain/ColorDefinition.h"
#include "Domain/Settings.h"
#include <unordered_map>

namespace scmp
{

class NcursesWriter
{
  public:
    explicit NcursesWriter(const Settings *const settings);

    virtual ~NcursesWriter();

    virtual void write(int32_t height, int32_t width, ColorIndex color,
                       const std::wstring &msg, const wchar_t character);

    virtual void clear();

    /**
     * A scaling function for coloring. For numbers 0 to max this function
     * returns a coloring from the lowest color to the highest. If "wrap" is set
     * to true, colors will not loop from 0 to max. If "wrap" is set to true
     * then colors will wrap.
     */
    virtual ColorIndex to_color_pair(int32_t number, int32_t max,
                                     bool wrap = true) const;

    virtual void flush();
    void set_colors(WINDOW *window, int foreground, int background, attr_t style = A_NORMAL);
    void write_over(WINDOW *window, int row, int column, const std::wstring &text, attr_t style);

    void set_surface(WINDOW *surface) { m_surface = surface; }
    WINDOW *surface() const { return m_surface ? m_surface : stdscr; }
    bool composed() const { return m_surface != nullptr; }
    int height() const { return getmaxy(surface()); }
    int width() const { return getmaxx(surface()); }

  private:
    const Settings *const m_settings;
    WINDOW *m_surface = nullptr;
    std::unordered_map<int, int> m_pairs;

    /**
     * Approximate a RGB color to a ncurses color pair. This idea was originally
     * taken from ruby's rainbow gem https://github.com/sickill/rainbow
     *
     * This is not a true RGB color, just an approximation. This is also
     * dependent on terminal settings. For example, if the terminal colors have
     * all been set to red, then they will still all be red.
     */
    static int16_t to_ansi_color(const int16_t red, const int16_t green,
                                 const int16_t blue);

    /**
     * Initialize color pairs for ncurses
     */
    void setup_colors();

    static int16_t to_ansi_color_domain(const int16_t color);

    /**
     * Draw a solid block for each character in msg.
     */
    void write_blocks(int32_t height, int32_t width, scmp::ColorIndex color,
                      const std::wstring &msg);

    /**
     * Write msg to ncurses window using foreground colors
     */
    void write_foreground(int32_t height, int32_t width, scmp::ColorIndex color,
                          const std::wstring &msg);
};
}

#endif
