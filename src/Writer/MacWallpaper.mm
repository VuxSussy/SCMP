#include "Writer/Wallpaper.h"
#include "Wallpapers.inc"
#include "Utils/Logger.h"

#include <JavaScriptCore/JavaScriptCore.h>
#include <chrono>
#include <cstring>

scmp::Wallpaper::~Wallpaper()
{
    if (m_context)
    {
        JSGlobalContextRelease(static_cast<JSGlobalContextRef>(m_context));
    }
}

const std::vector<scmp::WallpaperCell> &scmp::Wallpaper::frame(const std::string &scene, int cols, int rows, bool moving)
{
    using Clock = std::chrono::steady_clock;
    const double now = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
    if (scene != m_scene)
    {
        m_started = now;
    }
    const auto tick = moving ? static_cast<int64_t>((now - m_started) * 8) : 0;
    if (scene == m_scene && cols == m_cols && rows == m_rows && tick == m_tick)
    {
        return m_cells;
    }
    m_scene = scene;
    m_cols = cols;
    m_rows = rows;
    m_tick = tick;
    m_cells.clear();

    JSValueRef error = nullptr;
    if (!m_context)
    {
        auto context = JSGlobalContextCreate(nullptr);
        m_context = context;
        auto script = JSStringCreateWithUTF8CString(wallpaper_source);
        JSEvaluateScript(context, script, nullptr, nullptr, 1, &error);
        JSStringRelease(script);
        if (error)
        {
            SCMP_LOG(LogLevel::ERROR, "Could not initialize the wallpaper scenes");
            return m_cells;
        }
    }
    auto context = static_cast<JSGlobalContextRef>(m_context);
    auto global = JSContextGetGlobalObject(context);
    auto symbol = JSStringCreateWithUTF8CString("visWallpaper");
    auto function = JSValueToObject(context, JSObjectGetProperty(context, global, symbol, &error), &error);
    JSStringRelease(symbol);
    if (error || !function || !JSObjectIsFunction(context, function))
    {
        return m_cells;
    }
    auto name = JSStringCreateWithUTF8CString(scene.c_str());
    const JSValueRef args[] = {JSValueMakeString(context, name), JSValueMakeNumber(context, cols),
        JSValueMakeNumber(context, rows), JSValueMakeNumber(context, 1.0 + tick / 8.0)};
    JSStringRelease(name);
    auto result = JSObjectCallAsFunction(context, function, nullptr, 4, args, &error);
    if (error || !result || JSValueGetTypedArrayType(context, result, &error) != kJSTypedArrayTypeUint32Array)
    {
        return m_cells;
    }
    auto array = JSValueToObject(context, result, &error);
    const size_t length = JSObjectGetTypedArrayByteLength(context, array, &error);
    if (error || length != static_cast<size_t>(cols) * rows * sizeof(WallpaperCell))
    {
        return m_cells;
    }
    m_cells.resize(static_cast<size_t>(cols) * rows);
    auto source = JSObjectGetTypedArrayBytesPtr(context, array, &error);
    if (!error && source)
    {
        std::memcpy(m_cells.data(), source, length);
    }
    else
    {
        m_cells.clear();
    }
    return m_cells;
}
