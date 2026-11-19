#include "Writer/TerminalText.h"

#include <algorithm>
#include <climits>
#include <cwchar>
#include <cwctype>

void scmp::draw_text(WINDOW *window, int row, int column, int width, const std::string &text, int style,
                    NcursesWriter *writer)
{
    if (row < 0 || row >= getmaxy(window) || column < 0 || column >= getmaxx(window) || width <= 0)
    {
        return;
    }
    width = std::min(width, getmaxx(window) - column);
    std::wstring visible;
    std::mbstate_t state{};
    size_t offset = 0;
    while (offset < text.size() && width > 0)
    {
        wchar_t character = L' ';
        auto consumed = std::mbrtowc(&character, text.data() + offset, text.size() - offset, &state);
        if (consumed == 0 || consumed == static_cast<size_t>(-1) || consumed == static_cast<size_t>(-2))
        {
            state = {};
            character = L'?';
            consumed = 1;
        }
        if (!std::iswprint(character))
        {
            character = L' ';
        }
        const int cells = std::max(0, wcwidth(character));
        if (cells > width)
        {
            break;
        }
        visible += character;
        width -= cells;
        offset += consumed;
    }
    if (writer)
    {
        writer->write_over(window, row, column, visible, style);
        return;
    }
    wattrset(window, style);
    mvwaddwstr(window, row, column, visible.c_str());
    wattrset(window, A_NORMAL);
}

bool scmp::prompt_text(std::string &value, const char *title, const char *hint,
                      const volatile std::sig_atomic_t &interrupted)
{
    std::string input = value;
    timeout(120);
    while (!interrupted)
    {
        erase();
        draw_text(stdscr, 1, 2, COLS - 4, title, A_BOLD);
        draw_text(stdscr, 3, 2, COLS - 4, hint, A_DIM);
        const size_t max_visible = static_cast<size_t>(std::max(1, COLS - 7));
        const auto tail = input.size() > max_visible ? input.size() - max_visible : 0;
        draw_text(stdscr, 5, 2, COLS - 4, "> " + input.substr(tail), A_REVERSE);
        draw_text(stdscr, LINES - 2, 2, COLS - 4, "Enter apply  Ctrl-U clear  Esc cancel", A_DIM);
        refresh();

        wint_t key = 0;
        const int result = get_wch(&key);
        if (result == ERR)
        {
            continue;
        }
        if (key == 27)
        {
            return false;
        }
        if (key == '\n' || key == '\r' || key == KEY_ENTER)
        {
            value = std::move(input);
            return true;
        }
        if (key == 21)
        {
            input.clear();
        }
        else if ((key == KEY_BACKSPACE || key == 8 || key == 127) && !input.empty())
        {
            size_t end = input.size() - 1;
            while (end > 0 && (static_cast<unsigned char>(input[end]) & 0xc0) == 0x80)
            {
                --end;
            }
            input.resize(end);
        }
        else if (result == OK && std::iswprint(key) && input.size() < 4096)
        {
            char bytes[MB_LEN_MAX];
            std::mbstate_t state{};
            const auto count = std::wcrtomb(bytes, static_cast<wchar_t>(key), &state);
            if (count != static_cast<size_t>(-1))
            {
                input.append(bytes, count);
            }
        }
    }
    return false;
}
