#include "font.hpp"
#include <gio/gio.h>
#include <glib/gstdio.h>
#ifdef _WIN32
#include <pango/pangocairo.h>
#else
#include <fontconfig/fontconfig.h>
#endif

namespace quiet {
PixelFont::PixelFont() {
    gchar* directory = g_dir_make_tmp("pomodoro-font-XXXXXX", nullptr);
    if (!directory) {
        g_printerr("App font directory unavailable; using system fonts.\n");
        return;
    }
    directory_ = directory;
    g_free(directory);
    for (const auto* name : {"Silkscreen-Regular.ttf", "DejaVuSansMono.ttf"}) {
        const auto resource = std::string("/quiet-pomodoro/") + name;
        GBytes* bytes = g_resources_lookup_data(resource.c_str(), G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
        if (!bytes) { g_printerr("Bundled font missing: %s\n", name); continue; }
        const auto path = directory_ + G_DIR_SEPARATOR_S + name;
        paths_.push_back(path);
        gsize size = 0;
        const auto* data = static_cast<const char*>(g_bytes_get_data(bytes, &size));
        bool registered = false;
        if (g_file_set_contents(path.c_str(), data, static_cast<gssize>(size), nullptr)) {
#ifdef _WIN32
            // Pango's Win32 font map caches its own font collection. GDI registration
            // alone succeeds but leaves GTK rendering a fallback face.
            GError* error = nullptr;
            registered = pango_font_map_add_font_file(pango_cairo_font_map_get_default(), path.c_str(), &error);
            if (error) { g_printerr("Font %s: %s\n", name, error->message); g_error_free(error); }
#else
            registered = FcConfigAppFontAddFile(FcConfigGetCurrent(), reinterpret_cast<const FcChar8*>(path.c_str()));
#endif
        }
        g_bytes_unref(bytes);
        if (!registered) g_printerr("App font unavailable: %s; using the monospace fallback.\n", name);
    }
}
PixelFont::~PixelFont() {
    for (const auto& path : paths_) g_remove(path.c_str());
    if (!directory_.empty()) g_rmdir(directory_.c_str());
}
} // namespace quiet
