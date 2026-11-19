#include "Plugin/Plugins.h"

#include "Player/MusicLibrary.h"
#include "Utils/ConfigurationUtils.h"
#include "Utils/Utils.h"
#include "Writer/TerminalText.h"

#include <algorithm>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>

namespace
{
bool plugin_filename(const std::string &file)
{
    const auto dot = file.find_last_of('.');
    return !file.empty() && file.front() != '.' &&
           file.find_first_of(",\r\n") == std::string::npos && dot != std::string::npos &&
           (file.substr(dot) == ".dylib" || file.substr(dot) == ".so");
}

struct PluginSurface
{
    WINDOW *window;
    scmp::NcursesWriter &writer;
};

void put_cell(void *context, int x, int y, uint32_t codepoint, uint8_t color)
{
    auto &surface = *static_cast<PluginSurface *>(context);
    auto *window = surface.window;
    if (y < 0 || y >= getmaxy(window) || x < 0 || x >= getmaxx(window) ||
        codepoint > 0x10ffff || wcwidth(static_cast<wchar_t>(codepoint)) != 1)
    {
        return;
    }
    const auto style = color < COLORS && color < COLOR_PAIRS ? COLOR_PAIR(color) : A_NORMAL;
    surface.writer.write_over(window, y, x, std::wstring(1, static_cast<wchar_t>(codepoint)), style);
}
}

void scmp::Plugins::Close::operator()(void *handle) const
{
    dlclose(handle);
}

void scmp::Plugins::load(const std::string &folder, const std::string &enabled)
{
    m_entries.clear();
    const auto expanded = expand_path(folder);
    m_folder = expanded.find('/') == std::string::npos ? "./" + expanded : expanded;
    const auto names = enabled == "none" ? std::vector<std::string>{} : Utils::split(enabled, ',');
    std::vector<Entry> entries;
    std::unique_ptr<DIR, decltype(&closedir)> directory(opendir(m_folder.c_str()), closedir);
    if (directory)
    {
        while (auto *item = readdir(directory.get()))
        {
            std::string file(item->d_name);
            const auto full = m_folder + '/' + file;
            struct stat info{};
            if (!plugin_filename(file) || stat(full.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
            {
                continue;
            }
            Entry entry;
            entry.file = std::move(file);
            entry.enabled = std::find(names.begin(), names.end(), entry.file) != names.end();
            if (entry.enabled)
            {
                entry.handle.reset(dlopen(full.c_str(), RTLD_NOW | RTLD_LOCAL));
                if (!entry.handle)
                {
                    const char *message = dlerror();
                    entry.error = message ? message : "Could not load plugin";
                }
                else
                {
                    const auto entry_point = reinterpret_cast<decltype(&scmp_plugin_v1)>(
                        dlsym(entry.handle.get(), "scmp_plugin_v1"));
                    entry.api = entry_point ? entry_point() : nullptr;
                    if (!entry.api || entry.api->abi_version != SCMP_PLUGIN_ABI_VERSION)
                    {
                        entry.error = "Missing or incompatible scmp_plugin_v1";
                        entry.api = nullptr;
                        entry.handle.reset();
                    }
                }
            }
            entries.push_back(std::move(entry));
        }
    }
    for (const auto &name : names)
    {
        if (plugin_filename(name) && std::none_of(entries.begin(), entries.end(), [&](const Entry &entry) {
                return entry.file == name;
            }))
        {
            Entry entry;
            entry.file = name;
            entry.enabled = true;
            entry.error = "Plugin file is missing";
            entries.push_back(std::move(entry));
        }
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return a.file < b.file;
    });
    m_entries = std::move(entries);
}

bool scmp::Plugins::active() const
{
    return std::any_of(m_entries.begin(), m_entries.end(), [](const Entry &entry) { return entry.api != nullptr; });
}

std::string scmp::Plugins::visualizer_name() const
{
    const auto found = std::find_if(m_entries.begin(), m_entries.end(), [](const Entry &entry) {
        return entry.api && entry.api->visualizer;
    });
    if (found == m_entries.end())
    {
        return {};
    }
    return found->api->name && *found->api->name ? found->api->name : found->file;
}

void scmp::Plugins::draw(const ScmpPluginFrame &frame, WINDOW *surface, NcursesWriter &writer,
                       ScmpPluginDraw ScmpPluginV1::*layer, bool first_only) const
{
    PluginSurface context{surface, writer};
    const ScmpPluginCanvas canvas{getmaxx(surface), getmaxy(surface), &context, put_cell};
    for (const auto &entry : m_entries)
    {
        if (entry.api && entry.api->*layer)
        {
            (entry.api->*layer)(&frame, &canvas);
            if (first_only)
            {
                break;
            }
        }
    }
}

void scmp::Plugins::draw_background(const ScmpPluginFrame &frame, WINDOW *surface, NcursesWriter &writer) const
{
    draw(frame, surface, writer, &ScmpPluginV1::background, false);
}

void scmp::Plugins::draw_visualizer(const ScmpPluginFrame &frame, WINDOW *surface, NcursesWriter &writer) const
{
    draw(frame, surface, writer, &ScmpPluginV1::visualizer, true);
}

void scmp::Plugins::draw_overlay(const ScmpPluginFrame &frame, WINDOW *surface, NcursesWriter &writer) const
{
    draw(frame, surface, writer, &ScmpPluginV1::overlay, false);
}

void scmp::Plugins::menu(const std::string &config_path, const volatile std::sig_atomic_t &interrupted)
{
    size_t selected = 0;
    std::string error;
    timeout(120);
    while (!interrupted)
    {
        erase();
        draw_text(stdscr, 1, 2, COLS - 4, "Plugins", A_BOLD);
        draw_text(stdscr, 2, 2, COLS - 4, m_folder, A_DIM);
        draw_text(stdscr, 4, 2, COLS - 4, "Enable only plugins you trust. They run as part of scmp.", A_DIM);
        const size_t rows = static_cast<size_t>(std::max(1, LINES - 10));
        const auto start = selected >= rows ? selected - rows + 1 : 0;
        if (m_entries.empty())
        {
            draw_text(stdscr, 6, 2, COLS - 4, "Install a .dylib or .so in this folder, then press r.");
        }
        for (size_t index = start; index < m_entries.size() && index - start < rows; ++index)
        {
            const auto &entry = m_entries[index];
            std::string label = (entry.enabled ? "[x] " : "[ ] ") + entry.file;
            if (entry.api && entry.api->name)
            {
                label += "  " + std::string(entry.api->name);
            }
            if (!entry.error.empty())
            {
                label += "  (load failed)";
            }
            draw_text(stdscr, 6 + static_cast<int>(index - start), 2, COLS - 4,
                      label, index == selected ? A_REVERSE : A_NORMAL);
        }
        const auto &detail = error.empty() && !m_entries.empty() ? m_entries[selected].error : error;
        draw_text(stdscr, LINES - 3, 2, COLS - 4, detail, A_BOLD);
        draw_text(stdscr, LINES - 1, 2, COLS - 4, "↑↓ browse  Enter toggle  r reload  q back", A_REVERSE);
        refresh();

        const int key = getch();
        if (key == 'q' || key == 27 || key == 'p')
        {
            return;
        }
        if ((key == KEY_UP || key == 'k') && selected > 0)
        {
            --selected;
        }
        if ((key == KEY_DOWN || key == 'j') && selected + 1 < m_entries.size())
        {
            ++selected;
        }
        if (key == KEY_ENTER || key == '\n' || key == '\r' || key == 'r')
        {
            auto flags = std::string{};
            for (size_t index = 0; index < m_entries.size(); ++index)
            {
                if (m_entries[index].enabled != (index == selected && key != 'r'))
                {
                    if (!flags.empty())
                    {
                        flags += ',';
                    }
                    flags += m_entries[index].file;
                }
            }
            if (flags.empty())
            {
                flags = "none";
            }
            try
            {
                if (key != 'r' && !m_entries.empty())
                {
                    ConfigurationUtils::save_options(config_path, {{"plugins.enabled", flags}});
                }
                const auto folder = m_folder;
                load(folder, flags);
                selected = m_entries.empty() ? 0 : std::min(selected, m_entries.size() - 1);
                error.clear();
            }
            catch (const std::exception &exception)
            {
                error = exception.what();
            }
        }
    }
}
