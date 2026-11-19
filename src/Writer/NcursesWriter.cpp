#include "Writer/NcursesWriter.h"
#include "Utils/Logger.h"
#include "Utils/NcursesUtils.h"

scmp::NcursesWriter::NcursesWriter(const scmp::Settings *const settings)
    : m_settings{settings}
{
    setlocale(LC_CTYPE, "");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    set_escdelay(35);
    curs_set(0); // sets the cursor to invisible

    if (has_colors() == TRUE)
    {
        start_color();        // turns on color
        use_default_colors(); // uses default colors of terminal, which allows
                              // transparency to work

        // initialize color pairs
        setup_colors();
    }
}

void scmp::NcursesWriter::setup_colors()
{
    // initialize colors
    for (int16_t i = 1; i < COLORS && i < 256 && i < COLOR_PAIRS; ++i)
    {
        init_pair(i, i, -1);
    }
}

void scmp::NcursesWriter::set_colors(WINDOW *window, int foreground, int background, attr_t style)
{
#if defined(NCURSES_EXT_COLORS)
    if (background >= 0 && COLORS >= 256 && COLOR_PAIRS > 256)
    {
        const int key = (foreground + 1) * 256 + background;
        auto known = m_pairs.find(key);
        if (known == m_pairs.end() && m_pairs.size() + 256 < static_cast<size_t>(COLOR_PAIRS))
        {
            const int next = static_cast<int>(m_pairs.size() + 256);
            if (init_extended_pair(next, foreground, background) == OK)
            {
                known = m_pairs.emplace(key, next).first;
            }
        }
        if (known != m_pairs.end())
        {
            int pair = known->second;
            wattr_set(window, style, 0, &pair);
            return;
        }
    }
#endif
    wattrset(window, style | (foreground >= 0 && foreground < COLORS && foreground < COLOR_PAIRS
                               ? COLOR_PAIR(foreground) : A_NORMAL));
}

void scmp::NcursesWriter::write_over(WINDOW *window, int row, int column, const std::wstring &text,
                                   attr_t style)
{
    const int width = getmaxx(window);
    if (row < 0 || row >= getmaxy(window) || column < 0 || column >= width)
    {
        return;
    }
#if defined(NCURSES_EXT_COLORS)
    const int foreground = PAIR_NUMBER(style);
    for (size_t i = 0; i < text.size();)
    {
        const auto cells = wcwidth(text[i]);
        if (cells <= 0)
        {
            ++i;
            continue;
        }
        if (column + cells > width)
        {
            break;
        }
        size_t count = 1;
        while (i + count < text.size() && wcwidth(text[i + count]) == 0)
        {
            ++count;
        }
        int background = -1;
        int previous_foreground = 0;
        cchar_t previous{};
        if (mvwin_wch(window, row, column, &previous) == OK && previous.ext_color > 0)
        {
            extended_pair_content(previous.ext_color, &previous_foreground, &background);
        }
        set_colors(window, foreground ? foreground : -1, background, style & ~A_COLOR);
        mvwaddnwstr(window, row, column, text.data() + i, static_cast<int>(count));
        i += count;
        column += cells;
    }
#else
    wattrset(window, style);
    mvwaddnwstr(window, row, column, text.c_str(), std::min<int>(width - column, text.size()));
#endif
    wattrset(window, A_NORMAL);
}

void scmp::NcursesWriter::write_blocks(int32_t height, int32_t width,
                                     scmp::ColorIndex color,
                                     const std::wstring &msg)
{
    write_foreground(height, width, color,
                     std::wstring(msg.size(), ScmpConstants::k_full_block_wchar));
}

void scmp::NcursesWriter::write_foreground(int32_t height, int32_t width,
                                          scmp::ColorIndex color,
                                          const std::wstring &msg)
{
    auto *window = surface();
    if (composed())
    {
        write_over(window, height, width, msg, COLOR_PAIR(color));
        return;
    }
    if (height < 0 || height >= getmaxy(window) || width < 0 || width >= getmaxx(window))
    {
        return;
    }
    wattrset(window, COLOR_PAIR(color));
    mvwaddnwstr(window, height, width, msg.c_str(), std::min<int>(getmaxx(window) - width, msg.size()));
    wattrset(window, A_NORMAL);
}

void scmp::NcursesWriter::write(const int32_t row, const int32_t column,
                               const scmp::ColorIndex color,
                               const std::wstring &msg, const wchar_t character)
{
    if (character == ScmpConstants::k_space_wchar)
    {
        write_blocks(row, column, color, msg);
    }
    else
    {
        write_foreground(row, column, color, msg);
    }
}

void scmp::NcursesWriter::clear()
{
    if (!composed())
    {
        standend();
        erase();
    }
}

void scmp::NcursesWriter::flush()
{
    if (!composed())
    {
        refresh();
    }
}

int16_t scmp::NcursesWriter::to_ansi_color_domain(const int16_t color)
{
    return static_cast<int16_t>(6.0 * static_cast<double>(color) / 256.0);
}

/**
 * Returns nearest supported 256-color an rgb value as an ansi color value
 * Inspired by the rainbow gem https://github.com/sickill/rainbow
 *
 * This is an approximate result. Actual results depend on what colors the
 * terminal has set.
 */
int16_t scmp::NcursesWriter::to_ansi_color(const int16_t red,
                                          const int16_t green,
                                          const int16_t blue)
{
    return 16 + static_cast<int16_t>((to_ansi_color_domain(red) * 36.0) +
                                     (to_ansi_color_domain(green) * 6.0) +
                                     (to_ansi_color_domain(blue) * 1.0));
}

scmp::ColorIndex scmp::NcursesWriter::to_color_pair(int32_t number, int32_t max,
                                                  bool wrap) const
{
    const auto colors_size = static_cast<scmp::ColorIndex>(
        m_settings->get_color_definitions().size());
    if (COLORS < 256)
    {
        return 0;
    }
    if (colors_size == 0)
    {
        const ColorIndex live[] = {24, 31, 37, 43, 79, 122};
        const ColorIndex studio[] = {44, 50, 87, 123, 159, 231};
        const auto index = std::max(0, std::min(5, (number * 6) / (max + 1)));
        return composed() ? studio[index] : live[index];
    }

    const auto index = (number * colors_size) / (max + 1);

    const auto color_definition =
        m_settings->get_color_definitions()[static_cast<size_t>(
            wrap ? index % colors_size : std::min(index, colors_size - 1))];

    return to_ansi_color(color_definition.get_red(),
                         color_definition.get_green(),
                         color_definition.get_blue());
}

scmp::NcursesWriter::~NcursesWriter()
{
    endwin();
}
