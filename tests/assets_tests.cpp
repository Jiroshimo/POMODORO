#include "font.hpp"
#include <gtk/gtk.h>
#include <fontconfig/fontconfig.h>
#include <cstdlib>
#include <iostream>
#include <cstring>

void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main() {
    quiet::PixelFont font;
    FcPattern* query = FcNameParse(reinterpret_cast<const FcChar8*>("Silkscreen"));
    FcConfigSubstitute(nullptr, query, FcMatchPattern);
    FcDefaultSubstitute(query);
    FcResult result;
    FcPattern* match = FcFontMatch(nullptr, query, &result);
    FcChar8* family = nullptr;
    check(match && FcPatternGetString(match, FC_FAMILY, 0, &family) == FcResultMatch &&
          std::strcmp(reinterpret_cast<const char*>(family), "Silkscreen") == 0, "bundled pixel font registered privately");
    FcPatternDestroy(match);
    FcPatternDestroy(query);
    GError* error = nullptr;
    GBytes* bytes = g_resources_lookup_data("/quiet-pomodoro/style.css", G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    check(bytes && !error, "embedded stylesheet present");
    gsize length = 0;
    const auto* css = static_cast<const char*>(g_bytes_get_data(bytes, &length));
    GtkCssProvider* provider = gtk_css_provider_new();
    bool warnings = false;
    g_signal_connect(provider, "parsing-error", G_CALLBACK(+[](GtkCssProvider*, GtkCssSection*, GError* error, gpointer data) {
        *static_cast<bool*>(data) = true;
        std::cerr << error->message << '\n';
    }), &warnings);
    check(gtk_css_provider_load_from_data(provider, css, static_cast<gssize>(length), &error) && !error && !warnings,
          "stylesheet parses without warnings");
    g_object_unref(provider);
    g_bytes_unref(bytes);
    bytes = g_resources_lookup_data("/quiet-pomodoro/Silkscreen-OFL.txt", G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
    check(bytes != nullptr, "font license bundled");
    g_bytes_unref(bytes);
    std::cout << "Assets checks passed\n";
}
