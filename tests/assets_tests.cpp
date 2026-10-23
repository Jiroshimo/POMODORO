#include "font.hpp"
#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#ifndef _WIN32
#include <fontconfig/fontconfig.h>
#endif
#include <cstdlib>
#include <iostream>
#include <cstring>

void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main() {
    quiet::PixelFont font;
#ifndef _WIN32
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
#endif
    // Test the renderer's actual selection: OS font registration alone can succeed
    // while Pango still falls back to a different face on Windows.
    auto* fontMap = pango_cairo_font_map_get_default();
    auto* context = pango_font_map_create_context(fontMap);
    for (const int size : {44, 76, 104, 160}) {
        auto* description = pango_font_description_from_string("Silkscreen");
        pango_font_description_set_absolute_size(description, size * PANGO_SCALE);
        auto* loaded = pango_font_map_load_font(fontMap, context, description);
        check(loaded != nullptr, "timer font loads at all window sizes");
        auto* actual = pango_font_describe(loaded);
        check(std::strcmp(pango_font_description_get_family(actual), "Silkscreen") == 0,
              "Pango renders the bundled Silkscreen face without substitution");
        auto* layout = pango_layout_new(context);
        pango_layout_set_font_description(layout, description);
        pango_layout_set_text(layout, "0123456789: FOCUS", -1);
        check(pango_layout_get_unknown_glyphs_count(layout) == 0, "timer digits and labels have readable glyphs");
        g_object_unref(layout);
        pango_font_description_free(actual);
        g_object_unref(loaded);
        pango_font_description_free(description);
    }
    auto* textDescription = pango_font_description_from_string("Inter 14");
    auto* textFont = pango_font_map_load_font(fontMap, context, textDescription);
    check(textFont != nullptr, "task text font loads");
    auto* actualText = pango_font_describe(textFont);
    check(std::strcmp(pango_font_description_get_family(actualText), "Inter") == 0,
          "task text uses the same readable face on Windows and Linux");
    pango_font_description_free(actualText);
    g_object_unref(textFont);
    pango_font_description_free(textDescription);
    g_object_unref(context);
    GError* error = nullptr;
    GBytes* bytes = g_resources_lookup_data("/stash/style.css", G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
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
    bytes = g_resources_lookup_data("/stash/Inter-OFL.txt", G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
    check(bytes != nullptr, "text font license bundled");
    g_bytes_unref(bytes);
    bytes = g_resources_lookup_data("/stash/Silkscreen-OFL.txt", G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
    check(bytes != nullptr, "font license bundled");
    g_bytes_unref(bytes);
    std::cout << "Assets checks passed\n";
}
