#include "Writer/SettingsMenu.h"
#include "Writer/NcursesWriter.h"
#include "Utils/ConfigurationUtils.h"

#include <algorithm>
#include <climits>
#include <cwctype>
#include <sstream>

namespace
{
void line(int row, int column, int width, const std::string &text, int style = A_NORMAL)
{
    if (width > 0 && row >= 0 && row < LINES && column >= 0 && column < COLS)
    {
        attrset(style);
        mvaddnstr(row, column, text.c_str(), std::min(width, COLS - column));
        attrset(A_NORMAL);
    }
}

void wrapped(int row, int column, int width, int bottom, const std::string &text)
{
    std::istringstream words(text);
    std::string word;
    std::string buffer;
    while (row < bottom && words >> word)
    {
        if (!buffer.empty() && static_cast<int>(buffer.size() + word.size() + 1) > width)
        {
            line(row++, column, width, buffer);
            buffer.clear();
        }
        if (!buffer.empty())
        {
            buffer += ' ';
        }
        buffer += word;
    }
    if (!buffer.empty() && row < bottom)
    {
        line(row, column, width, buffer);
    }
}
}

bool scmp::edit_settings(const std::string &config_path, const volatile std::sig_atomic_t &interrupted)
{
    const auto configured = ConfigurationUtils::read_config(config_path);
    ConfigValues changes;
    std::vector<std::string> categories;
    std::vector<std::vector<const ConfigOption *>> groups;
    for (const auto &option : ConfigurationUtils::options())
    {
        if (categories.empty() || categories.back() != option.category)
        {
            categories.emplace_back(option.category);
            groups.emplace_back();
        }
        groups.back().push_back(&option);
    }

    size_t category = 0;
    size_t selected = 0;
    bool in_options = false;
    bool editing = false;
    std::string input;
    std::string error;

    timeout(100);
    while (!interrupted)
    {
        const auto &group = groups[category];
        const auto &option = *group[selected];
        const auto pending = changes.find(option.key);
        const auto saved = configured.find(option.key);
        const bool is_default = saved == configured.end();
        const std::string &value = pending != changes.end() ? pending->second
            : is_default ? option.default_value : saved->second;

        erase();
        const int accent = A_BOLD | (COLORS > COLOR_CYAN ? COLOR_PAIR(COLOR_CYAN) : 0);
        const int footer = LINES - 1;
        const int category_width = 12;
        const int second = category_width + 2;
        const int third = std::max(39, COLS / 2);
        const bool wide = COLS >= 66;
        const int end_options = wide ? third - 1 : COLS - 1;
        const int options_bottom = wide ? footer - 2 : std::max(4, footer - 7);
        const int option_rows = std::max(1, options_bottom - 4);
        const size_t first_option = selected < static_cast<size_t>(option_rows)
            ? 0 : selected - option_rows + 1;

        line(0, 1, COLS - 2, changes.empty() ? "Settings" : "Settings  * unsaved", accent);
        line(1, 1, COLS - 2, config_path, A_DIM);
        line(3, 1, category_width, "Category", A_BOLD);
        line(3, second, end_options - second, "Setting", A_BOLD);
        for (size_t i = 0; i < categories.size() && 4 + static_cast<int>(i) < footer - 1; ++i)
        {
            const int style = i == category ? (in_options ? accent : A_REVERSE) : A_NORMAL;
            line(4 + static_cast<int>(i), 1, category_width - 1, categories[i], style);
        }
        for (size_t i = first_option; i < group.size() && i - first_option < static_cast<size_t>(option_rows); ++i)
        {
            const int style = i == selected ? (in_options ? A_REVERSE : accent) : A_NORMAL;
            std::string label(group[i]->label);
            if (changes.count(group[i]->key) != 0)
            {
                label += " *";
            }
            line(4 + static_cast<int>(i - first_option), second, end_options - second, label, style);
        }
        if (LINES > 10)
        {
            const int detail_row = wide ? 3 : options_bottom;
            const int detail_column = wide ? third + 1 : second;
            const int detail_width = COLS - detail_column - 1;
            line(detail_row, detail_column, detail_width, option.key, accent);
            const std::string shown = value == " " ? "(space)" : value;
            line(detail_row + 1, detail_column, detail_width, shown, A_BOLD);
            line(detail_row + 2, detail_column, detail_width,
                 pending != changes.end() ? "Pending" : is_default ? "Default" : "Saved", A_DIM);
            wrapped(detail_row + 3, detail_column, detail_width, footer - 1, option.hint);
        }
        line(footer - 1, 1, COLS - 2, error, A_BOLD);
        if (editing)
        {
            const size_t available = static_cast<size_t>(std::max(0, COLS - 4));
            const auto visible = input.substr(input.size() > available ? input.size() - available : 0);
            line(footer, 1, COLS - 2, "> " + visible, A_REVERSE);
        }
        else
        {
            line(footer, 1, COLS - 2, "hjkl/arrows  Enter edit  w save & close  q cancel", A_REVERSE);
        }
        refresh();

        wint_t key = 0;
        int result = ERR;
        do
        {
            result = get_wch(&key);
        } while (result == ERR && !interrupted);
        if (interrupted)
        {
            break;
        }
        if (editing)
        {
            if (key == '\n' || key == '\r' || key == KEY_ENTER)
            {
                changes[option.key] = input;
                editing = false;
                error.clear();
            }
            else if (key == 27)
            {
                editing = false;
            }
            else if (key == 21)
            {
                input.clear();
            }
            else if ((key == KEY_BACKSPACE || key == 127 || key == 8) && !input.empty())
            {
                size_t end = input.size() - 1;
                while (end > 0 && (static_cast<unsigned char>(input[end]) & 0xc0) == 0x80)
                {
                    --end;
                }
                input.resize(end);
            }
            else if (result == OK && std::iswprint(key) && input.size() < 2048)
            {
                char bytes[MB_LEN_MAX];
                std::mbstate_t state{};
                const auto count = std::wcrtomb(bytes, static_cast<wchar_t>(key), &state);
                if (count != static_cast<size_t>(-1))
                {
                    input.append(bytes, count);
                }
            }
            continue;
        }

        switch (key)
        {
        case 'q':
        case 27:
            return false;
        case 'w':
            try
            {
                ConfigurationUtils::save_options(config_path, changes);
                return !changes.empty();
            }
            catch (const std::exception &exception)
            {
                error = exception.what();
            }
            break;
        case KEY_UP:
        case 'k':
            if (in_options && selected > 0) --selected;
            if (!in_options && category > 0) { --category; selected = 0; }
            break;
        case KEY_DOWN:
        case 'j':
            if (in_options && selected + 1 < group.size()) ++selected;
            if (!in_options && category + 1 < groups.size()) { ++category; selected = 0; }
            break;
        case KEY_LEFT:
        case 'h':
            in_options = false;
            break;
        case KEY_RIGHT:
        case 'l':
            in_options = true;
            break;
        case KEY_ENTER:
        case '\n':
        case '\r':
            if (in_options)
            {
                editing = true;
                input = value;
                error = "Ctrl-U clear  Enter stage  Esc cancel edit";
            }
            in_options = true;
            break;
        }
    }
    return false;
}
