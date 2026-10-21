#include "font.hpp"
#include <gio/gio.h>
#include <glib/gstdio.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fontconfig/fontconfig.h>
#endif

namespace quiet {
PixelFont::PixelFont() {
    GBytes* bytes = g_resources_lookup_data("/quiet-pomodoro/Silkscreen-Regular.ttf", G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
    if (!bytes) return;
    gchar* directory = g_dir_make_tmp("pomodoro-font-XXXXXX", nullptr);
    if (directory) {
        directory_ = directory;
        path_ = directory_ + G_DIR_SEPARATOR_S + "Silkscreen-Regular.ttf";
        g_free(directory);
        gsize size = 0;
        const auto* data = static_cast<const char*>(g_bytes_get_data(bytes, &size));
        if (g_file_set_contents(path_.c_str(), data, static_cast<gssize>(size), nullptr)) {
#ifdef _WIN32
            auto* wide = g_utf8_to_utf16(path_.c_str(), -1, nullptr, nullptr, nullptr);
            registered_ = wide && AddFontResourceExW(reinterpret_cast<LPCWSTR>(wide), FR_PRIVATE, nullptr) != 0;
            g_free(wide);
#else
            registered_ = FcConfigAppFontAddFile(FcConfigGetCurrent(), reinterpret_cast<const FcChar8*>(path_.c_str()));
#endif
        }
    }
    g_bytes_unref(bytes);
    if (!registered_) g_printerr("Pixel font unavailable; using the monospace fallback.\n");
}
PixelFont::~PixelFont() {
#ifdef _WIN32
    if (registered_) {
        auto* wide = g_utf8_to_utf16(path_.c_str(), -1, nullptr, nullptr, nullptr);
        if (wide) RemoveFontResourceExW(reinterpret_cast<LPCWSTR>(wide), FR_PRIVATE, nullptr);
        g_free(wide);
    }
#endif
    if (!path_.empty()) g_remove(path_.c_str());
    if (!directory_.empty()) g_rmdir(directory_.c_str());
}
} // namespace quiet
