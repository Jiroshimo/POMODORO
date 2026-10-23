#include "audio.hpp"
#include "font.hpp"
#include "storage.hpp"
#include "timer.hpp"
#include <gtk/gtk.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <time.h>
#endif

namespace {
std::int64_t now() {
#ifdef _WIN32
    return static_cast<std::int64_t>(GetTickCount64()) * 1000;
#else
#ifdef CLOCK_BOOTTIME
    timespec stamp{};
    if (clock_gettime(CLOCK_BOOTTIME, &stamp) == 0)
        return static_cast<std::int64_t>(stamp.tv_sec) * 1000000 + stamp.tv_nsec / 1000;
#endif
    return g_get_monotonic_time();
#endif
}
std::string number(int value) {
    return (value < 10 ? "0" : "") + std::to_string(value);
}
void style(GtkWidget* widget, const char* name, bool enabled = true) {
    auto* context = gtk_widget_get_style_context(widget);
    if (enabled) gtk_style_context_add_class(context, name);
    else gtk_style_context_remove_class(context, name);
}
GtkWidget* label(const char* text, const char* name = nullptr) {
    GtkWidget* widget = gtk_label_new(text);
    if (name) style(widget, name);
    return widget;
}
void setText(GtkWidget* widget, const std::string& text) {
    if (std::strcmp(gtk_label_get_text(GTK_LABEL(widget)), text.c_str()) != 0)
        gtk_label_set_text(GTK_LABEL(widget), text.c_str());
}
void connect(GtkWidget* widget, const char* signal, std::function<void()> callback) {
    auto* fn = new std::function<void()>(std::move(callback));
    g_signal_connect_data(widget, signal, G_CALLBACK(+[](GtkWidget*, gpointer data) {
        (*static_cast<std::function<void()>*>(data))();
    }), fn, +[](gpointer data, GClosure*) { delete static_cast<std::function<void()>*>(data); }, G_CONNECT_DEFAULT);
}
GtkWidget* button(const char* text, const char* name, const char* hint, std::function<void()> callback) {
    GtkWidget* widget = gtk_button_new_with_label(text);
    if (name) style(widget, name);
    gtk_widget_set_tooltip_text(widget, hint);
    atk_object_set_name(gtk_widget_get_accessible(widget), hint);
    connect(widget, "clicked", std::move(callback));
    return widget;
}
// Keep the familiar 12px circles inside comfortable 28px click targets.
GtkWidget* windowControl(int kind, const char* hint, std::function<void()> callback) {
    auto* widget = gtk_button_new();
    style(widget, "window-control");
    gtk_widget_set_tooltip_text(widget, hint);
    atk_object_set_name(gtk_widget_get_accessible(widget), hint);
    connect(widget, "clicked", std::move(callback));
    auto* dot = gtk_drawing_area_new();
    gtk_widget_set_size_request(dot, 12, 12);
    gtk_widget_set_halign(dot, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(widget), dot);
    g_signal_connect(dot, "draw", G_CALLBACK((+[](GtkWidget* dot, cairo_t* cr, gpointer data) -> gboolean {
        const int kind = GPOINTER_TO_INT(data);
        const double colors[][3] = {{1.0, 0.37, 0.34}, {1.0, 0.74, 0.18}, {0.16, 0.78, 0.25}};
        cairo_set_source_rgb(cr, colors[kind][0], colors[kind][1], colors[kind][2]);
        cairo_arc(cr, 6, 6, 5.5, 0, 2 * G_PI);
        cairo_fill(cr);
        const auto state = gtk_widget_get_state_flags(gtk_widget_get_parent(dot));
        if (state & (GTK_STATE_FLAG_PRELIGHT | GTK_STATE_FLAG_ACTIVE)) {
            cairo_set_source_rgba(cr, 0, 0, 0, 0.65);
            cairo_set_line_width(cr, 1.1);
            if (kind == 0) {
                cairo_move_to(cr, 4, 4); cairo_line_to(cr, 8, 8);
                cairo_move_to(cr, 8, 4); cairo_line_to(cr, 4, 8);
            } else {
                cairo_move_to(cr, 3, 6); cairo_line_to(cr, 9, 6);
                if (kind == 2) { cairo_move_to(cr, 6, 3); cairo_line_to(cr, 6, 9); }
            }
            cairo_stroke(cr);
        }
        return FALSE;
    })), GINT_TO_POINTER(kind));
    g_signal_connect(widget, "state-flags-changed", G_CALLBACK(+[](GtkWidget*, GtkStateFlags, gpointer dot) {
        gtk_widget_queue_draw(GTK_WIDGET(dot));
    }), dot);
    return widget;
}
void pack(GtkWidget* box, GtkWidget* child, bool expand = false) {
    gtk_box_pack_start(GTK_BOX(box), child, expand, expand, 0);
}
void margins(GtkWidget* widget, int horizontal, int top = 0, int bottom = 0) {
    gtk_widget_set_margin_start(widget, horizontal);
    gtk_widget_set_margin_end(widget, horizontal);
    gtk_widget_set_margin_top(widget, top);
    gtk_widget_set_margin_bottom(widget, bottom);
}
void cancel(guint& source) {
    if (source) { g_source_remove(source); source = 0; }
}
struct CalendarGeometry {
    double step, cell, x, y;
    explicit CalendarGeometry(GtkWidget* widget) {
        const int width = gtk_widget_get_allocated_width(widget);
        const int height = gtk_widget_get_allocated_height(widget);
        step = std::min(14.0, std::floor(width / 53.0));
        cell = step - (step <= 9 ? 1 : 2);
        x = (width - 53 * step) / 2;
        y = (height - 7 * step) / 2;
    }
};

struct TaskWidget { GtkWidget* check; std::size_t index; };
struct View {
    GtkWidget *root{}, *content{}, *digits{}, *start{}, *progress{}, *mode{}, *tasks{}, *entry{},
        *taskCount{}, *session{}, *previous{}, *next{}, *error{}, *scroll{},
        *windowControls{}, *brand{}, *settings{}, *close{}, *minimize{}, *fullscreen{};
    std::vector<TaskWidget> checks;
};

class App {
public:
    explicit App(bool smoke) : smoke_(smoke) {
        gchar* path = g_build_filename(g_get_user_config_dir(), "quiet-pomodoro", "state.ini", nullptr);
        path_ = path;
        g_free(path);
        prefs_ = quiet::load(path_);
        audio_.setOutput(prefs_.audioOutput);
        timer_.minutes = prefs_.minutes;
        timer_.cycle = prefs_.plan.finished;
        timer_.reset();
        selected_ = prefs_.plan.current();
        auto* provider = gtk_css_provider_new();
        gtk_css_provider_load_from_resource(provider, "/stash/style.css");
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
                                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(provider);
        g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, nullptr);

        buildWindow();
        buildView(0);
        buildView(1);
        gtk_stack_set_visible_child_name(GTK_STACK(stack_), "main");
        setGeometry();
        renderTasks();
        refresh();
        gtk_widget_show_all(window_);
        updateWindowControls();
        gtk_widget_hide(popover_);
        gtk_widget_grab_focus(views_[0].start);
        if (smoke_) g_timeout_add(250, +[](gpointer data) -> gboolean {
            static_cast<App*>(data)->smokeTest();
            return G_SOURCE_REMOVE;
        }, this);
    }
    int result() const { return result_; }

private:
    GtkWidget *window_{}, *stack_{}, *popover_{}, *target_{}, *compactButton_{}, *audioStatus_{},
        *surface_{}, *grip_{}, *fullscreenAction_{}, *audioDetails_{}, *calendar_{}, *settingsScroll_{};
    std::array<GtkWidget*, 3> modeButtons_{};
    std::array<GtkWidget*, 3> duration_{};
    std::array<View, 2> views_{};
    std::array<std::string, 100> drafts_{};
    quiet::Timer timer_;
    quiet::Preferences prefs_;
    quiet::Audio audio_;
    std::string path_, saveError_;
    std::string calendarDate_;
    std::array<std::string, 371> calendarDays_{};
    guint clock_ = 0, audioWatch_ = 0;
    bool compact_ = false, smoke_ = false, syncing_ = false, alive_ = true;
    bool fullscreen_ = false, returnCompact_ = false, returnMaximized_ = false;
    bool shortLayout_ = false;
    int layoutTier_ = 0;
    int selected_ = 1, result_ = 0;
    int mainWidth_ = 480, mainHeight_ = 700, compactSize_ = 360;
    int shapedWidth_ = 0, shapedHeight_ = 0, cursorEdge_ = -2;

    void buildWindow() {
        window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(window_), "Stash");
        gtk_window_set_default_size(GTK_WINDOW(window_), mainWidth_, mainHeight_);
        gtk_window_set_position(GTK_WINDOW(window_), GTK_WIN_POS_CENTER);
        gtk_window_set_resizable(GTK_WINDOW(window_), TRUE);
        gtk_window_set_decorated(GTK_WINDOW(window_), FALSE);
        GdkPixbuf* icon = gdk_pixbuf_new_from_resource("/stash/stash.png", nullptr);
        if (icon) {
            gtk_window_set_icon(GTK_WINDOW(window_), icon);
            g_object_unref(icon);
        }
        gtk_widget_set_app_paintable(window_, TRUE);
        GdkVisual* rgba = gdk_screen_get_rgba_visual(gtk_widget_get_screen(window_));
        if (rgba) gtk_widget_set_visual(window_, rgba);
        connect(window_, "destroy", [this] {
            alive_ = false;
            cancel(clock_);
            cancel(audioWatch_);
            gtk_main_quit();
        });
        g_signal_connect(window_, "draw", G_CALLBACK(+[](GtkWidget*, cairo_t* cr, gpointer) -> gboolean {
            cairo_save(cr);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_set_source_rgba(cr, 0, 0, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
            return FALSE;
        }), nullptr);
        g_signal_connect(window_, "size-allocate", G_CALLBACK(+[](GtkWidget*, GtkAllocation*, gpointer data) {
            auto* app = static_cast<App*>(data);
            app->updateLayout();
            app->roundCorners();
        }), this);
        g_signal_connect(window_, "window-state-event", G_CALLBACK(+[](GtkWidget*, GdkEventWindowState* event, gpointer data) -> gboolean {
            auto* app = static_cast<App*>(data);
            if (event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN) {
                app->fullscreen_ = (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) != 0;
                app->updateWindowControls();
            }
            app->shapedWidth_ = 0;
            app->updateLayout();
            app->roundCorners();
            return FALSE;
        }), this);
        connect(window_, "realize", [this] { roundCorners(); });
        g_signal_connect(window_, "key-press-event", G_CALLBACK(keyPress), this);

        GtkWidget* border = gtk_event_box_new();
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(border), FALSE);
        gtk_widget_add_events(border, GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK);
        g_signal_connect(border, "button-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton* event, gpointer data) -> gboolean {
            auto* app = static_cast<App*>(data);
            const int edge = app->edgeAt(event->x_root, event->y_root);
            if (event->button != 1 || edge < 0 || app->fullscreen_ || gtk_window_is_maximized(GTK_WINDOW(app->window_))) return FALSE;
            gtk_window_begin_resize_drag(GTK_WINDOW(app->window_), static_cast<GdkWindowEdge>(edge), 1,
                                          static_cast<int>(event->x_root), static_cast<int>(event->y_root), event->time);
            return TRUE;
        }), this);
        g_signal_connect(border, "motion-notify-event", G_CALLBACK((+[](GtkWidget*, GdkEventMotion* event, gpointer data) -> gboolean {
            auto* app = static_cast<App*>(data);
            const int edge = app->fullscreen_ || gtk_window_is_maximized(GTK_WINDOW(app->window_)) ? -1 : app->edgeAt(event->x_root, event->y_root);
            if (edge == app->cursorEdge_) return FALSE;
            app->cursorEdge_ = edge;
            static const char* cursors[] = {"nw-resize", "n-resize", "ne-resize", "w-resize", "e-resize", "sw-resize", "s-resize", "se-resize"};
            GdkCursor* cursor = edge >= 0 ? gdk_cursor_new_from_name(gtk_widget_get_display(app->window_), cursors[edge]) : nullptr;
            gdk_window_set_cursor(gtk_widget_get_window(app->window_), cursor);
            if (cursor) g_object_unref(cursor);
            return FALSE;
        })), this);
        GtkWidget* surface = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        surface_ = surface;
        style(surface, "surface");
        gtk_container_add(GTK_CONTAINER(border), surface);
        gtk_container_add(GTK_CONTAINER(window_), border);
        GtkWidget* overlay = gtk_overlay_new();
        pack(surface, overlay, true);
        stack_ = gtk_stack_new();
        gtk_stack_set_homogeneous(GTK_STACK(stack_), FALSE);
        gtk_container_add(GTK_CONTAINER(overlay), stack_);
        GtkWidget* grip = gtk_event_box_new();
        grip_ = grip;
        gtk_widget_set_no_show_all(grip, TRUE);
        GtkWidget* gripText = label("◢", "dim");
        gtk_container_add(GTK_CONTAINER(grip), gripText);
        gtk_widget_set_halign(grip, GTK_ALIGN_END);
        gtk_widget_set_valign(grip, GTK_ALIGN_END);
        gtk_widget_set_tooltip_text(grip, "Drag to resize");
        gtk_widget_add_events(grip, GDK_BUTTON_PRESS_MASK);
        g_signal_connect(grip, "button-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton* event, gpointer data) -> gboolean {
            auto* app = static_cast<App*>(data);
            if (event->button != 1 || app->fullscreen_ || gtk_window_is_maximized(GTK_WINDOW(app->window_))) return FALSE;
            gtk_window_begin_resize_drag(GTK_WINDOW(app->window_), GDK_WINDOW_EDGE_SOUTH_EAST, 1,
                                          static_cast<int>(event->x_root), static_cast<int>(event->y_root), event->time);
            return TRUE;
        }), this);
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), grip);
        gtk_widget_show_all(grip);
    }

    void roundCorners() {
        if (!gtk_widget_get_realized(window_)) return;
        const int w = gtk_widget_get_allocated_width(window_), h = gtk_widget_get_allocated_height(window_);
        if (w == shapedWidth_ && h == shapedHeight_) return;
        shapedWidth_ = w;
        shapedHeight_ = h;
        const int radius = fullscreen_ || gtk_window_is_maximized(GTK_WINDOW(window_)) ? 0 : std::min({20, w / 2, h / 2});
        cairo_region_t* region = cairo_region_create();
        cairo_rectangle_int_t middle{0, radius, w, h - 2 * radius};
        cairo_region_union_rectangle(region, &middle);
        for (int y = 0; y < radius; ++y) {
            const double dy = radius - y - 0.5;
            const int inset = static_cast<int>(std::ceil(radius - std::sqrt(radius * radius - dy * dy)));
            cairo_rectangle_int_t top{inset, y, w - 2 * inset, 1};
            cairo_rectangle_int_t bottom{inset, h - y - 1, w - 2 * inset, 1};
            cairo_region_union_rectangle(region, &top);
            cairo_region_union_rectangle(region, &bottom);
        }
        GdkWindow* window = gtk_widget_get_window(window_);
        GdkDisplay* display = gtk_widget_get_display(window_);
        // Composited desktops retain smooth alpha edges; others get a real shape.
        if (!gdk_screen_is_composited(gtk_widget_get_screen(window_)) && gdk_display_supports_shapes(display))
            gdk_window_shape_combine_region(window, region, 0, 0);
        if (gdk_display_supports_input_shapes(display)) gdk_window_input_shape_combine_region(window, region, 0, 0);
        cairo_region_destroy(region);
    }
    int edgeAt(double rootX, double rootY) const {
        int originX = 0, originY = 0;
        gdk_window_get_origin(gtk_widget_get_window(window_), &originX, &originY);
        const double x = rootX - originX, y = rootY - originY;
        const int w = gtk_widget_get_allocated_width(window_), h = gtk_widget_get_allocated_height(window_);
        const bool left = x < 7, right = x >= w - 7, top = y < 7, bottom = y >= h - 7;
        if (top) return left ? GDK_WINDOW_EDGE_NORTH_WEST : right ? GDK_WINDOW_EDGE_NORTH_EAST : GDK_WINDOW_EDGE_NORTH;
        if (bottom) return left ? GDK_WINDOW_EDGE_SOUTH_WEST : right ? GDK_WINDOW_EDGE_SOUTH_EAST : GDK_WINDOW_EDGE_SOUTH;
        if (left) return GDK_WINDOW_EDGE_WEST;
        return right ? GDK_WINDOW_EDGE_EAST : -1;
    }
    GtkWidget* draggable(GtkWidget* child) {
        GtkWidget* area = gtk_event_box_new();
        gtk_container_add(GTK_CONTAINER(area), child);
        gtk_widget_add_events(area, GDK_BUTTON_PRESS_MASK);
        g_signal_connect(area, "button-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton* event, gpointer data) -> gboolean {
            auto* app = static_cast<App*>(data);
            for (GtkWidget* w = gtk_get_event_widget(reinterpret_cast<GdkEvent*>(event)); w; w = gtk_widget_get_parent(w))
                if (GTK_IS_BUTTON(w) || GTK_IS_ENTRY(w)) return FALSE;
            if (event->button != 1) return FALSE;
            if (event->type == GDK_2BUTTON_PRESS) { app->setCompact(!app->compact_); return TRUE; }
            if (app->fullscreen_) return TRUE;
            gtk_window_begin_move_drag(GTK_WINDOW(app->window_), 1,
                static_cast<int>(event->x_root), static_cast<int>(event->y_root), event->time);
            return TRUE;
        }), this);
        return area;
    }

    void buildView(int id) {
        auto& view = views_[id];
        const bool mini = id == 1;
        view.root = mini ? gtk_box_new(GTK_ORIENTATION_VERTICAL, 0) : gtk_overlay_new();
        if (mini) style(view.root, "compact");
        gtk_stack_add_named(GTK_STACK(stack_), view.root, mini ? "mini" : "main");
        GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        margins(header, mini ? 10 : 16, 3, 0);
        view.windowControls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        view.close = windowControl(0, "Close (Ctrl+Q)", [this] { gtk_widget_destroy(window_); });
        view.minimize = windowControl(1, "Minimize to the taskbar (Ctrl+H)", [this] {
            gtk_window_iconify(GTK_WINDOW(window_));
        });
        view.fullscreen = windowControl(2, "Enter fullscreen (F11)", [this] { setFullscreen(!fullscreen_); });
        pack(view.windowControls, view.close);
        pack(view.windowControls, view.minimize);
        pack(view.windowControls, view.fullscreen);
        pack(header, view.windowControls);
        view.brand = label("STASH", "brand");
        gtk_widget_set_halign(view.brand, GTK_ALIGN_START);
        gtk_widget_set_margin_start(view.brand, 12);
        pack(header, view.brand, true);
        view.error = label("!", "error");
        gtk_widget_set_no_show_all(view.error, TRUE);
        pack(header, view.error);
        view.settings = button("···", "icon", "Settings", [this, id] {
            gtk_popover_set_relative_to(GTK_POPOVER(popover_), views_[id].settings);
            gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(settingsScroll_),
                std::clamp(gtk_widget_get_allocated_height(window_) - 96, 200, 640));
            gtk_widget_show_all(popover_);
            gtk_popover_popup(GTK_POPOVER(popover_));
        });
        gtk_box_pack_end(GTK_BOX(header), view.settings, FALSE, FALSE, 0);
        if (!mini) buildSettings(view.settings);
        GtkWidget* titlebar = draggable(header);
        if (mini) pack(view.root, titlebar);
        else {
            gtk_widget_set_valign(titlebar, GTK_ALIGN_START);
            gtk_overlay_add_overlay(GTK_OVERLAY(view.root), titlebar);
        }

        view.content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        if (mini) pack(view.root, view.content, true);
        else {
            // Keep the whole working area at its natural size, centered on both
            // axes. Equal margins reserve room for the overlaid window controls.
            gtk_widget_set_halign(view.content, GTK_ALIGN_CENTER);
            gtk_widget_set_valign(view.content, GTK_ALIGN_CENTER);
            margins(view.content, 0, 40, 40);
            GtkWidget* stage = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
            pack(stage, view.content, true);
            gtk_container_add(GTK_CONTAINER(view.root), stage);
        }

        GtkWidget* center = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        margins(center, mini ? 12 : 26, mini ? 5 : 8, 0);
        view.mode = label("FOCUS", "section");
        pack(center, view.mode);
        view.digits = label("25:00", mini ? "mini-timer" : "timer");
        atk_object_set_name(gtk_widget_get_accessible(view.digits), "Time remaining");
        gtk_widget_set_margin_top(view.digits, mini ? 0 : 14);
        pack(center, draggable(view.digits));
        GtkWidget* controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_halign(controls, GTK_ALIGN_CENTER);
        gtk_widget_set_margin_top(controls, mini ? 2 : 12);
        view.start = button("START", "primary", "Start or pause (Space)", [this] { toggle(); });
        gtk_widget_set_size_request(view.start, mini ? 115 : 160, -1);
        pack(controls, view.start);
        pack(controls, button("↺", "reset", "Reset timer (R)", [this] { reset(); }));
        pack(center, controls);
        GtkWidget* progress = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
        gtk_widget_set_halign(progress, GTK_ALIGN_CENTER);
        margins(progress, 0, mini ? 8 : 17, mini ? 0 : 1);
        pack(progress, label("SESSIONS", "section"));
        view.progress = label("", "count");
        pack(progress, view.progress);
        gtk_widget_set_tooltip_text(progress, "Completed sessions / target. Change the target in settings.");
        pack(center, progress);
        pack(view.content, center);
        buildTasks(id);
        if (!mini) buildCalendar();
    }

    void buildCalendar() {
        calendar_ = gtk_drawing_area_new();
        gtk_widget_set_size_request(calendar_, 424, 84);
        margins(calendar_, 16, 4, 12);
        atk_object_set_name(gtk_widget_get_accessible(calendar_), "Daily completed focus time over the past year");
        gtk_widget_set_has_tooltip(calendar_, TRUE);
        g_signal_connect(calendar_, "draw", G_CALLBACK((+[](GtkWidget* widget, cairo_t* cr, gpointer data) -> gboolean {
            const auto* app = static_cast<App*>(data);
            const CalendarGeometry grid(widget);
            if (grid.step < 3) return FALSE;
            const double colors[][3] = {
                {0.13, 0.15, 0.16}, {0.06, 0.30, 0.17}, {0.0, 0.43, 0.20},
                {0.15, 0.65, 0.25}, {0.26, 0.83, 0.33}
            };
            for (std::size_t i = 0; i < app->calendarDays_.size(); ++i) {
                const auto& day = app->calendarDays_[i];
                if (day.empty()) continue;
                const auto found = app->prefs_.activity.find(day);
                const auto* color = colors[quiet::activityLevel(found == app->prefs_.activity.end() ? 0 : found->second)];
                cairo_set_source_rgb(cr, color[0], color[1], color[2]);
                cairo_rectangle(cr, grid.x + (i / 7) * grid.step, grid.y + (i % 7) * grid.step,
                                grid.cell, grid.cell);
                cairo_fill(cr);
            }
            return FALSE;
        })), this);
        g_signal_connect(calendar_, "query-tooltip", G_CALLBACK(+[](GtkWidget* widget, gint x, gint y,
                         gboolean keyboard, GtkTooltip* tooltip, gpointer data) -> gboolean {
            const auto* app = static_cast<App*>(data);
            const CalendarGeometry grid(widget);
            if (keyboard || grid.step < 3 || x < grid.x || y < grid.y) return FALSE;
            const int column = static_cast<int>((x - grid.x) / grid.step);
            const int row = static_cast<int>((y - grid.y) / grid.step);
            if (column >= 53 || row >= 7) return FALSE;
            const auto& day = app->calendarDays_[column * 7 + row];
            if (day.empty()) return FALSE;
            const auto found = app->prefs_.activity.find(day);
            const auto seconds = found == app->prefs_.activity.end() ? 0 : found->second;
            const auto text = day + " · " + std::to_string(seconds / 3600) + "h " +
                              std::to_string((seconds % 3600) / 60) + "m";
            gtk_tooltip_set_text(tooltip, text.c_str());
            return TRUE;
        }), this);
        pack(views_[0].content, calendar_);
    }
    void refreshCalendar() {
        const auto date = quiet::today();
        if (calendarDate_ == date) return;
        calendarDate_ = date;
        calendarDays_ = quiet::calendarDays(date);
        gtk_widget_queue_draw(calendar_);
    }

    void buildTasks(int id) {
        auto& view = views_[id];
        const bool mini = id == 1;
        GtkWidget* todo = gtk_box_new(GTK_ORIENTATION_VERTICAL, mini ? 5 : 10);
        margins(todo, mini ? 12 : 26, mini ? 10 : 18, mini ? 11 : 12);
        pack(todo, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
        GtkWidget* heading = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
        view.taskCount = label("TO DO", "section");
        gtk_widget_set_halign(view.taskCount, GTK_ALIGN_START);
        pack(heading, view.taskCount, true);
        view.previous = button("‹", "icon", "Previous session's tasks", [this] { selectSession(selected_ - 1); });
        pack(heading, view.previous);
        view.session = label("01", "count");
        pack(heading, view.session);
        view.next = button("›", "icon", "Next session's tasks", [this] { selectSession(selected_ + 1); });
        pack(heading, view.next);
        pack(todo, heading);
        GtkWidget* input = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        view.entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(view.entry), "Add task...");
        gtk_entry_set_width_chars(GTK_ENTRY(view.entry), 6);
        gtk_entry_set_max_length(GTK_ENTRY(view.entry), 240);
        atk_object_set_name(gtk_widget_get_accessible(view.entry), "New task for selected session");
        connect(view.entry, "activate", [this, id] { addTask(id); });
        connect(view.entry, "changed", [this, id] {
            if (syncing_) return;
            drafts_[selected_] = gtk_entry_get_text(GTK_ENTRY(views_[id].entry));
            syncEntries();
        });
        pack(input, view.entry, true);
        pack(input, button("+", "add", "Add task (Enter)", [this, id] { addTask(id); }));
        pack(todo, input);
        GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
        view.scroll = scroll;
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(scroll), TRUE);
        gtk_widget_set_size_request(scroll, -1, mini ? 40 : 120);
        if (!mini) gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroll), 340);
        view.tasks = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add(GTK_CONTAINER(scroll), view.tasks);
        pack(todo, scroll, true);
        pack(view.content, todo, true);
    }

    void buildSettings(GtkWidget* anchor) {
        popover_ = gtk_popover_new(anchor);
        GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_container_set_border_width(GTK_CONTAINER(content), 18);
        GtkWidget* modes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        style(modes, "modes");
        const char* modeNames[] = {"FOCUS", "SHORT", "LONG"};
        const char* hints[] = {"Focus timer", "Short break", "Long break"};
        for (int i = 0; i < 3; ++i) {
            modeButtons_[i] = button(modeNames[i], "mode", hints[i], [this, i] {
                timer_.select(static_cast<quiet::Mode>(i));
                cancel(clock_);
                refresh();
            });
            pack(modes, modeButtons_[i], true);
        }
        pack(content, modes);
        GtkWidget* sessionRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        GtkWidget* sessionLabel = label("SESSIONS", "section");
        gtk_widget_set_halign(sessionLabel, GTK_ALIGN_START);
        pack(sessionRow, sessionLabel, true);
        target_ = gtk_spin_button_new_with_range(1, 99, 1);
        gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(target_), TRUE);
        gtk_entry_set_width_chars(GTK_ENTRY(target_), 2);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(target_), prefs_.plan.target);
        gtk_widget_set_tooltip_text(target_, "Number of focus sessions in this plan");
        atk_object_set_name(gtk_widget_get_accessible(target_), "Session target");
        connect(target_, "value-changed", [this] { changeTarget(); });
        pack(sessionRow, target_);
        pack(content, sessionRow);
        const char* names[] = {"FOCUS (MIN)", "SHORT (MIN)", "LONG (MIN)"};
        for (int i = 0; i < 3; ++i) {
            GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
            GtkWidget* name = label(names[i], "section");
            gtk_widget_set_halign(name, GTK_ALIGN_START);
            pack(row, name, true);
            duration_[i] = gtk_spin_button_new_with_range(1, 180, 1);
            gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(duration_[i]), TRUE);
            gtk_entry_set_width_chars(GTK_ENTRY(duration_[i]), 3);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(duration_[i]), prefs_.minutes[i]);
            atk_object_set_name(gtk_widget_get_accessible(duration_[i]), names[i]);
            connect(duration_[i], "value-changed", [this, i] {
                prefs_.minutes[i] = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(duration_[i]));
                timer_.minutes = prefs_.minutes;
                if (!timer_.running()) timer_.reset();
                persist();
                refresh();
            });
            pack(row, duration_[i]);
            pack(content, row);
        }
        GtkWidget* sound = gtk_check_button_new_with_label("AUDIO");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sound), prefs_.sound);
        connect(sound, "toggled", [this, sound] {
            prefs_.sound = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sound));
            persist();
        });
        pack(content, sound);
        GtkWidget* output = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(output), "Automatic output");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(output), "Windows speakers");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(output), "Linux / WSLg audio");
        gtk_combo_box_set_active(GTK_COMBO_BOX(output), static_cast<int>(prefs_.audioOutput));
        atk_object_set_name(gtk_widget_get_accessible(output), "Audio output");
        gtk_widget_set_tooltip_text(output, "Choose the preferred output. If it fails, the app tries another available output.");
        connect(output, "changed", [this, output] {
            prefs_.audioOutput = static_cast<quiet::AudioOutput>(gtk_combo_box_get_active(GTK_COMBO_BOX(output)));
            audio_.setOutput(prefs_.audioOutput);
            persist();
        });
        pack(content, output);
        pack(content, button("TEST SOUND", nullptr, "Preview completion chime", [this] { playSound(true); }));
        audioStatus_ = label("", "dim");
        gtk_label_set_line_wrap(GTK_LABEL(audioStatus_), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(audioStatus_), 32);
        gtk_widget_set_no_show_all(audioStatus_, TRUE);
        pack(content, audioStatus_);
        audioDetails_ = button("AUDIO ERROR DETAILS", nullptr, "Show and copy the actual audio error", [this] {
            const auto result = audio_.result();
            auto* dialog = gtk_message_dialog_new(GTK_WINDOW(window_),
                static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", "Sound could not start");
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", result.error.c_str());
            gtk_dialog_add_button(GTK_DIALOG(dialog), "COPY DETAILS", GTK_RESPONSE_APPLY);
            g_object_set_data_full(G_OBJECT(dialog), "audio-error", g_strdup(result.error.c_str()), g_free);
            g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* dialog, gint response, gpointer) {
                if (response == GTK_RESPONSE_APPLY) {
                    const auto* text = static_cast<const char*>(g_object_get_data(G_OBJECT(dialog), "audio-error"));
                    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
                } else gtk_widget_destroy(GTK_WIDGET(dialog));
            }), nullptr);
            gtk_popover_popdown(GTK_POPOVER(popover_));
            gtk_widget_show_all(dialog);
        });
        gtk_widget_set_no_show_all(audioDetails_, TRUE);
        pack(content, audioDetails_);
        pack(content, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
        compactButton_ = button("COMPACT MODE", nullptr, "Toggle compact panel (M)", [this] {
            gtk_popover_popdown(GTK_POPOVER(popover_));
            setCompact(!compact_);
        });
        pack(content, compactButton_);
        fullscreenAction_ = button("ENTER FULLSCREEN", nullptr, "Toggle fullscreen (F11 or Escape)", [this] {
            setFullscreen(!fullscreen_);
        });
        pack(content, fullscreenAction_);
        pack(content, button("NEW PLAN", nullptr, "Reset session progress and uncheck tasks; keep the task lists", [this] {
            newPlan();
            gtk_popover_popdown(GTK_POPOVER(popover_));
        }));
        pack(content, button("HIDE", nullptr, "Minimize to the taskbar (Ctrl+H)", [this] {
            gtk_popover_popdown(GTK_POPOVER(popover_));
            gtk_window_iconify(GTK_WINDOW(window_));
        }));
        settingsScroll_ = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(settingsScroll_), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(settingsScroll_), TRUE);
        gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(settingsScroll_), TRUE);
        gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(settingsScroll_), 270);
        gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(settingsScroll_), 600);
        gtk_container_add(GTK_CONTAINER(settingsScroll_), content);
        gtk_container_add(GTK_CONTAINER(popover_), settingsScroll_);
    }

    void setGeometry() {
        GdkGeometry geometry{};
        geometry.min_width = compact_ ? 340 : 420;
        geometry.min_height = compact_ ? 340 : 560;
        geometry.min_aspect = geometry.max_aspect = 1;
        const int flags = GDK_HINT_MIN_SIZE | (compact_ ? GDK_HINT_ASPECT : 0);
        gtk_window_set_geometry_hints(GTK_WINDOW(window_), nullptr, &geometry, static_cast<GdkWindowHints>(flags));
    }
    void updateWindowControls() {
        for (auto& view : views_) {
            if (!view.windowControls) continue;
            gtk_widget_set_no_show_all(view.windowControls, fullscreen_);
            gtk_widget_set_no_show_all(view.brand, fullscreen_);
            gtk_widget_set_visible(view.windowControls, !fullscreen_);
            gtk_widget_set_visible(view.brand, !fullscreen_);
        }
        if (fullscreenAction_)
            gtk_button_set_label(GTK_BUTTON(fullscreenAction_), fullscreen_ ? "EXIT FULLSCREEN" : "ENTER FULLSCREEN");
        if (compactButton_)
            gtk_button_set_label(GTK_BUTTON(compactButton_), compact_ ? "EXPANDED MODE" : "COMPACT MODE");
        updateErrors();
    }
    void updateLayout() {
        if (!views_[0].scroll) return;
        const bool expanded = fullscreen_ || gtk_window_is_maximized(GTK_WINDOW(window_));
        style(surface_, "edge-to-edge", expanded);
        gtk_widget_set_visible(grip_, !expanded);
        const int w = gtk_widget_get_allocated_width(window_), h = gtk_widget_get_allocated_height(window_);
        const int tier = !compact_ && expanded && w >= 900 && h >= 700 ? (w >= 1400 && h >= 1000 ? 2 : 1) : 0;
        const bool shortLayout = tier == 1 && h < 740;
        if (tier == layoutTier_ && shortLayout == shortLayout_) return;
        layoutTier_ = tier;
        shortLayout_ = shortLayout;
        style(views_[0].root, "immersive", tier > 0);
        style(views_[0].root, "roomy", tier == 2);
        style(views_[0].root, "short-screen", shortLayout);
        gtk_widget_set_size_request(views_[0].content, tier == 2 ? 680 : tier == 1 ? 560 : -1, -1);
        gtk_widget_set_size_request(views_[0].scroll, -1, tier == 2 ? 220 : shortLayout ? 80 : 120);
        gtk_widget_set_size_request(calendar_, 424, shortLayout ? 70 : tier > 0 ? 98 : 84);
    }
    void setFullscreen(bool value) {
        if (fullscreen_ == value) return;
        gtk_popover_popdown(GTK_POPOVER(popover_));
        if (value) {
            returnCompact_ = compact_;
            returnMaximized_ = gtk_window_is_maximized(GTK_WINDOW(window_));
            if (compact_) setCompact(false);
            else if (!returnMaximized_) gtk_window_get_size(GTK_WINDOW(window_), &mainWidth_, &mainHeight_);
            fullscreen_ = true;
            gtk_window_fullscreen(GTK_WINDOW(window_));
        } else {
            fullscreen_ = false;
            // Drop the larger minimum requisition before asking the compositor
            // to restore the window, otherwise it can retain fullscreen sizing.
            updateLayout();
            gtk_window_unfullscreen(GTK_WINDOW(window_));
            if (returnCompact_) setCompact(true, false);
            else if (returnMaximized_) gtk_window_maximize(GTK_WINDOW(window_));
            else gtk_window_resize(GTK_WINDOW(window_), mainWidth_, mainHeight_);
        }
        shapedWidth_ = 0;
        updateWindowControls();
        updateLayout();
        roundCorners();
    }
    void setCompact(bool value, bool remember = true) {
        if (compact_ == value) return;
        const bool maximized = gtk_window_is_maximized(GTK_WINDOW(window_));
        int width = 0, height = 0;
        gtk_window_get_size(GTK_WINDOW(window_), &width, &height);
        if (remember && compact_) compactSize_ = std::max({340, width, height});
        else if (remember && !maximized && !fullscreen_) { mainWidth_ = width; mainHeight_ = height; }
        if (fullscreen_) {
            fullscreen_ = false;
            gtk_window_unfullscreen(GTK_WINDOW(window_));
            updateWindowControls();
        }
        if (maximized) gtk_window_unmaximize(GTK_WINDOW(window_));
        compact_ = value;
        updateLayout();
        gtk_popover_popdown(GTK_POPOVER(popover_));
        gtk_stack_set_visible_child_name(GTK_STACK(stack_), value ? "mini" : "main");
        setGeometry();
        gtk_window_set_keep_above(GTK_WINDOW(window_), value);
        gtk_window_resize(GTK_WINDOW(window_), value ? compactSize_ : mainWidth_, value ? compactSize_ : mainHeight_);
        gtk_widget_grab_focus(views_[value ? 1 : 0].start);
        updateWindowControls();
    }
    void toggle() {
        if (prefs_.plan.done()) { newPlan(); return; }
        if (timer_.running()) { handle(timer_.pause(now())); cancel(clock_); }
        else {
            timer_.start(now());
            clock_ = g_timeout_add(125, +[](gpointer data) -> gboolean {
                auto* app = static_cast<App*>(data);
                app->handle(app->timer_.update(now()));
                app->refreshTime();
                if (!app->timer_.running()) { app->clock_ = 0; return G_SOURCE_REMOVE; }
                return G_SOURCE_CONTINUE;
            }, this);
        }
        refresh();
    }
    void reset() {
        if (prefs_.plan.done()) { newPlan(); return; }
        cancel(clock_);
        timer_.reset();
        refresh();
    }
    void newPlan() {
        cancel(clock_);
        prefs_.plan.restart();
        timer_.cycle = 0;
        timer_.select(quiet::Mode::Focus);
        selected_ = 1;
        for (auto& task : prefs_.tasks) task.done = false;
        syncEntries();
        renderTasks();
        persist();
        refresh();
    }
    void changeTarget() {
        const bool wasDone = prefs_.plan.done();
        prefs_.plan.target = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(target_));
        prefs_.plan.normalize();
        if (prefs_.plan.done()) { cancel(clock_); timer_.reset(); }
        else if (wasDone) { timer_.select(quiet::Mode::Focus); selected_ = prefs_.plan.current(); }
        selected_ = std::clamp(selected_, 1, prefs_.plan.target);
        syncEntries();
        renderTasks();
        persist();
        refresh();
    }
    void handle(quiet::Tick event) {
        if (event.warning && prefs_.sound) playSound(false);
        if (!event.complete) return;
        if (event.focusComplete && prefs_.plan.complete()) {
            const auto date = quiet::today();
            if (prefs_.date != date) { prefs_.date = date; prefs_.completed = 0; }
            ++prefs_.completed;
            quiet::recordActivity(prefs_.activity, date, event.focusSeconds);
            gtk_widget_queue_draw(calendar_);
            selected_ = prefs_.plan.current();
            if (prefs_.plan.done()) timer_.select(quiet::Mode::Focus);
            syncEntries();
            renderTasks();
            persist();
        }
        if (prefs_.sound) playSound(true);
        refresh();
    }
    void refreshTime() {
        refreshCalendar();
        const auto text = prefs_.plan.done() ? "00:00" : timer_.display();
        for (auto& view : views_) setText(view.digits, text);
    }
    void refresh() {
        refreshTime();
        const char* start = prefs_.plan.done() ? "NEW PLAN" : timer_.running() ? "PAUSE" : "START";
        for (int id = 0; id < 2; ++id) {
            auto& view = views_[id];
            if (std::strcmp(gtk_button_get_label(GTK_BUTTON(view.start)), start) != 0)
                gtk_button_set_label(GTK_BUTTON(view.start), start);
            setText(view.progress, number(prefs_.plan.finished) + " / " + number(prefs_.plan.target));
        }
        const char* names[] = {"FOCUS", "SHORT BREAK", "LONG BREAK"};
        for (auto& view : views_)
            setText(view.mode, prefs_.plan.done() ? "COMPLETE" : names[static_cast<int>(timer_.mode)]);
        for (int i = 0; i < 3; ++i) {
            style(modeButtons_[i], "selected", i == static_cast<int>(timer_.mode));
            gtk_widget_set_sensitive(modeButtons_[i], !prefs_.plan.done());
        }
        updateTaskCounts();
        updateErrors();
    }
    void playSound(bool complete) {
        if (smoke_) return;
        audio_.play(complete);
        style(audioStatus_, "error", false);
        setText(audioStatus_, "Playing...");
        gtk_widget_show(audioStatus_);
        gtk_widget_hide(audioDetails_);
        if (!audioWatch_) audioWatch_ = g_timeout_add(150, +[](gpointer data) -> gboolean {
            auto* app = static_cast<App*>(data);
            if (app->audio_.playing()) return G_SOURCE_CONTINUE;
            app->audioWatch_ = 0;
            const auto result = app->audio_.result();
            setText(app->audioStatus_, result.ok() ? "Output: " + result.backend : "Sound unavailable. See error details.");
            gtk_widget_set_visible(app->audioDetails_, !result.ok());
            if (!result.ok()) std::cerr << "Sound unavailable:\n" << result.error << '\n';
            gtk_widget_set_tooltip_text(app->audioStatus_, result.ok() ? result.backend.c_str() : result.error.c_str());
            style(app->audioStatus_, "error", !result.ok());
            app->updateErrors();
            return G_SOURCE_REMOVE;
        }, this);
    }
    void persist() {
        saveError_.clear();
        quiet::save(path_, prefs_, saveError_);
        updateErrors();
    }
    void updateErrors() {
        std::string error = saveError_.empty() ? "" : "Changes could not be saved: " + saveError_;
        if (audio_.failed()) error += (error.empty() ? "" : "\n") + audio_.result().error;
        for (auto& view : views_) if (view.error) {
            gtk_widget_set_visible(view.error, !fullscreen_ && !error.empty());
            gtk_widget_set_tooltip_text(view.error, error.empty() ? nullptr : error.c_str());
            style(view.settings, "error", !error.empty());
            gtk_widget_set_tooltip_text(view.settings, error.empty() ? "Settings" : error.c_str());
        }
    }

    void selectSession(int session) {
        session = std::clamp(session, 1, prefs_.plan.target);
        if (session == selected_) return;
        selected_ = session;
        syncEntries();
        renderTasks();
    }
    void syncEntries() {
        syncing_ = true;
        for (auto& view : views_) if (view.entry && drafts_[selected_] != gtk_entry_get_text(GTK_ENTRY(view.entry)))
            gtk_entry_set_text(GTK_ENTRY(view.entry), drafts_[selected_].c_str());
        syncing_ = false;
    }
    void addTask(int id) {
        auto* entry = views_[id].entry;
        gchar* text = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
        g_strstrip(text);
        if (*text && prefs_.tasks.size() < 1000) {
            prefs_.tasks.push_back({text, false, selected_});
            drafts_[selected_].clear();
            syncEntries();
            renderTasks();
            persist();
        }
        g_free(text);
        gtk_widget_grab_focus(entry);
    }
    void setTaskDone(std::size_t index, bool done) {
        if (syncing_) return;
        prefs_.tasks[index].done = done;
        syncing_ = true;
        for (auto& view : views_) for (const auto& item : view.checks) if (item.index == index) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(item.check), done);
            style(item.check, "done", done);
        }
        syncing_ = false;
        updateTaskCounts();
        persist();
    }
    void renderTasks() {
        for (int id = 0; id < 2; ++id) {
            auto& view = views_[id];
            view.checks.clear();
            GList* children = gtk_container_get_children(GTK_CONTAINER(view.tasks));
            for (GList* item = children; item; item = item->next) gtk_widget_destroy(GTK_WIDGET(item->data));
            g_list_free(children);
            for (std::size_t i = 0; i < prefs_.tasks.size(); ++i) {
                const auto& task = prefs_.tasks[i];
                if (task.session != selected_) continue;
                GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
                style(row, "task-row");
                GtkWidget* check = gtk_check_button_new_with_label(task.text.c_str());
                GtkWidget* text = gtk_bin_get_child(GTK_BIN(check));
                gtk_label_set_ellipsize(GTK_LABEL(text), PANGO_ELLIPSIZE_END);
                gtk_label_set_max_width_chars(GTK_LABEL(text), id == 1 ? 20 : 28);
                gtk_widget_set_tooltip_text(check, task.text.c_str());
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), task.done);
                style(check, "done", task.done);
                connect(check, "toggled", [this, i, check] {
                    setTaskDone(i, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)));
                });
                view.checks.push_back({check, i});
                pack(row, check, true);
                pack(row, button("×", "delete", "Delete task", [this, i, id] {
                    prefs_.tasks.erase(prefs_.tasks.begin() + static_cast<std::ptrdiff_t>(i));
                    renderTasks();
                    persist();
                    gtk_widget_grab_focus(views_[id].entry);
                }));
                pack(view.tasks, row);
            }
            // An empty list stays empty: no decorative copy or placeholder rows.
            gtk_widget_show_all(view.tasks);
        }
        updateTaskCounts();
    }
    void updateTaskCounts() {
        int total = 0, done = 0;
        for (const auto& task : prefs_.tasks) if (task.session == selected_) { ++total; if (task.done) ++done; }
        for (auto& view : views_) {
            setText(view.taskCount, "TO DO " + std::to_string(done) + "/" + std::to_string(total));
            setText(view.session, number(selected_) + (selected_ <= prefs_.plan.finished ? " ✓" : ""));
            style(view.session, "done-session", selected_ <= prefs_.plan.finished);
            const std::string hint = "Tasks for session " + std::to_string(selected_) +
                (selected_ <= prefs_.plan.finished ? " (session completed)" : "");
            gtk_widget_set_tooltip_text(view.session, hint.c_str());
            gtk_widget_set_sensitive(view.previous, selected_ > 1);
            gtk_widget_set_sensitive(view.next, selected_ < prefs_.plan.target);
        }
    }
    static gboolean keyPress(GtkWidget*, GdkEventKey* event, gpointer data) {
        auto* app = static_cast<App*>(data);
        if (event->keyval == GDK_KEY_F11 && !(event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK))) {
            app->setFullscreen(!app->fullscreen_);
            return TRUE;
        }
        if (event->state & GDK_CONTROL_MASK) {
            if (event->keyval == GDK_KEY_q) { gtk_widget_destroy(app->window_); return TRUE; }
            if (event->keyval == GDK_KEY_h) { gtk_window_iconify(GTK_WINDOW(app->window_)); return TRUE; }
        }
        if (event->keyval == GDK_KEY_Escape) {
            if (gtk_widget_get_visible(app->popover_)) {
                gtk_popover_popdown(GTK_POPOVER(app->popover_));
                return TRUE;
            }
            gtk_popover_popdown(GTK_POPOVER(app->popover_));
            if (app->fullscreen_) app->setFullscreen(false);
            else if (app->compact_) app->setCompact(false);
            return TRUE;
        }
        if (GTK_IS_ENTRY(gtk_window_get_focus(GTK_WINDOW(app->window_)))) return FALSE;
        if (event->keyval == GDK_KEY_space && GTK_IS_TOGGLE_BUTTON(gtk_window_get_focus(GTK_WINDOW(app->window_)))) return FALSE;
        if (gtk_widget_get_visible(app->popover_) || (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK))) return FALSE;
        switch (event->keyval) {
        case GDK_KEY_space: app->toggle(); return TRUE;
        case GDK_KEY_r: case GDK_KEY_R: app->reset(); return TRUE;
        case GDK_KEY_m: case GDK_KEY_M: app->setCompact(!app->compact_); return TRUE;
        case GDK_KEY_Left: app->selectSession(app->selected_ - 1); return TRUE;
        case GDK_KEY_Right: app->selectSession(app->selected_ + 1); return TRUE;
        default: return FALSE;
        }
    }

    void check(bool condition, const char* name) {
        std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
        if (!condition) result_ = 1;
    }
    void checkCenteredLayout() {
        const auto& view = views_[0];
        int x = 0, y = 0;
        gtk_widget_translate_coordinates(view.content, view.root, 0, 0, &x, &y);
        const int w = gtk_widget_get_allocated_width(view.content);
        const int h = gtk_widget_get_allocated_height(view.content);
        check(std::abs(2 * x + w - gtk_widget_get_allocated_width(view.root)) <= 2,
              "Working area horizontally centered");
        check(std::abs(2 * y + h - gtk_widget_get_allocated_height(view.root)) <= 2,
              "Working area vertically centered");
        check(w <= (layoutTier_ == 2 ? 700 : layoutTier_ == 1 ? 580 : 520) &&
              h <= gtk_widget_get_allocated_height(view.root) - 80, "Working area fits with balanced margins");
        int timerX = 0, taskX = 0, unused = 0;
        gtk_widget_translate_coordinates(view.digits, view.content, 0, 0, &timerX, &unused);
        gtk_widget_translate_coordinates(view.tasks, view.content, 0, 0, &taskX, &unused);
        check(std::abs(2 * timerX + gtk_widget_get_allocated_width(view.digits) - w) <= 3 &&
              std::abs(2 * taskX + gtk_widget_get_allocated_width(view.tasks) - w) <= 3,
              "Timer and tasks share the same center line");
    }
    void screenshot(const char* filename) {
        while (alive_ && gtk_events_pending()) gtk_main_iteration();
        if (!alive_) return;
        // A changed label or scroll position can still await the next frame's
        // allocation after the event queue is empty. Resolve it before drawing.
        gtk_container_check_resize(GTK_CONTAINER(window_));
        if (gtk_widget_get_visible(popover_)) gtk_container_check_resize(GTK_CONTAINER(popover_));
        const int w = gtk_widget_get_allocated_width(window_), h = gtk_widget_get_allocated_height(window_);
        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t* cr = cairo_create(surface);
        gtk_widget_draw(window_, cr);
        check(cairo_surface_write_to_png(surface, filename) == CAIRO_STATUS_SUCCESS, filename);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
    }
    void smokeFullscreen(int stage = 0) {
        auto press = [this](guint keyval) {
            GdkEventKey event{};
            event.keyval = keyval;
            keyPress(window_, &event, this);
        };
        if (stage == 0) {
            gtk_widget_grab_focus(views_[0].entry);
            gtk_entry_set_text(GTK_ENTRY(views_[0].entry), "Keep this draft");
            toggle();
            press(GDK_KEY_F11);
            check(fullscreen_ && !compact_, "F11 enters fullscreen while task entry has focus");
        } else if (stage == 1 || stage == 4 || stage == 7) {
            GdkWindow* window = gtk_widget_get_window(window_);
            if (gdk_window_get_state(window) & GDK_WINDOW_STATE_FULLSCREEN) {
                checkCenteredLayout();
                const int w = gtk_widget_get_allocated_width(window_), h = gtk_widget_get_allocated_height(window_);
                if (w >= 900 && h >= 700) {
                    check(layoutTier_ > 0 && gtk_widget_get_allocated_width(views_[0].content) >= 560,
                          "Fullscreen enlarges timer and task area");
                }
                if (stage == 1) screenshot("docs/fullscreen.png");
            } else std::cout << "SKIP Native fullscreen unavailable on this display\n";
            check(!gtk_widget_get_visible(views_[0].windowControls) &&
                  !gtk_widget_get_visible(views_[0].brand) && gtk_widget_get_mapped(views_[0].settings),
                  "Fullscreen header shows only Settings");
            check(timer_.running() && drafts_[selected_] == "Keep this draft", "Fullscreen preserves timer and task draft");
            if (stage == 1) {
                g_signal_emit_by_name(views_[0].settings, "clicked");
                check(gtk_widget_get_visible(popover_) &&
                      gtk_popover_get_relative_to(GTK_POPOVER(popover_)) == views_[0].settings &&
                      !gtk_widget_get_visible(views_[0].windowControls),
                      "Fullscreen Settings opens without revealing window controls");
                g_signal_emit_by_name(fullscreenAction_, "clicked");
            } else press(GDK_KEY_Escape);
        } else if (stage == 2) {
            check(!fullscreen_ && !compact_ && gtk_widget_get_mapped(views_[0].windowControls),
                  "Leaving fullscreen restores expanded window controls");
            setCompact(true);
        } else if (stage == 3) {
            g_signal_emit_by_name(views_[1].fullscreen, "clicked");
            check(fullscreen_ && !compact_, "Green window control expands compact panel into fullscreen");
        } else if (stage == 5) {
            int w = 0, h = 0;
            gtk_window_get_size(GTK_WINDOW(window_), &w, &h);
            check(!fullscreen_ && compact_ && w == compactSize_ && h == compactSize_, "Escape restores compact size after fullscreen");
            check(gtk_widget_get_mapped(views_[1].windowControls), "Compact window controls return after fullscreen");
            setCompact(false);
            gtk_window_resize(GTK_WINDOW(window_), 620, 760);
        } else if (stage == 6) {
            press(GDK_KEY_F11);
        } else if (stage == 8) {
            int w = 0, h = 0;
            gtk_window_get_size(GTK_WINDOW(window_), &w, &h);
            check(!fullscreen_ && !compact_ && w == 620 && h == 760 && layoutTier_ == 0,
                  "Fullscreen restores normal window size and typography");
            toggle();
            g_signal_emit_by_name(views_[0].close, "clicked");
            return;
        }
        struct Step { App* app; int stage; };
        g_timeout_add_full(G_PRIORITY_DEFAULT, 400, +[](gpointer data) -> gboolean {
            const auto* step = static_cast<Step*>(data);
            step->app->smokeFullscreen(step->stage);
            return G_SOURCE_REMOVE;
        }, new Step{this, stage + 1}, +[](gpointer data) { delete static_cast<Step*>(data); });
    }
    void smokeTest() {
        check(gtk_widget_get_mapped(views_[0].windowControls) && gtk_widget_get_mapped(views_[0].settings),
              "Window controls and Settings are visible in the normal window");
        gtk_entry_set_text(GTK_ENTRY(views_[0].entry), "Build inventory system");
        g_signal_emit_by_name(views_[0].entry, "activate");
        gtk_entry_set_text(GTK_ENTRY(views_[0].entry), "Test item pickup");
        g_signal_emit_by_name(views_[0].entry, "activate");
        check(prefs_.tasks.size() == 2 && views_[1].checks.size() == 2, "Tasks in both views");
        if (views_[0].checks.empty()) { gtk_widget_destroy(window_); return; }
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(views_[0].checks[0].check), TRUE);
        check(prefs_.tasks[0].done && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(views_[1].checks[0].check)), "Checkbox synchronization");
        selectSession(2);
        gtk_entry_set_text(GTK_ENTRY(views_[1].entry), "Polish animations");
        g_signal_emit_by_name(views_[1].entry, "activate");
        check(prefs_.tasks.back().session == 2 && views_[0].checks.size() == 1, "Tasks assigned and filtered by session");
        gtk_entry_set_text(GTK_ENTRY(views_[0].entry), "Temporary task");
        g_signal_emit_by_name(views_[0].entry, "activate");
        GList* rows = gtk_container_get_children(GTK_CONTAINER(views_[1].tasks));
        GList* lastRow = gtk_container_get_children(GTK_CONTAINER(g_list_last(rows)->data));
        g_signal_emit_by_name(g_list_last(lastRow)->data, "clicked");
        g_list_free(lastRow);
        g_list_free(rows);
        check(prefs_.tasks.size() == 3 && views_[0].checks.size() == 1, "Delete task synchronizes both lists");
        selectSession(1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(target_), 3);
        g_signal_emit_by_name(views_[0].start, "clicked");
        check(timer_.running() && clock_ != 0, "Start timer scheduling");
        check(prefs_.activity.empty(), "Starting a session records no activity");
        g_signal_emit_by_name(views_[0].start, "clicked");
        check(!timer_.running() && clock_ == 0, "Pause removes timer source");
        check(prefs_.activity.empty(), "Pausing a session records no activity");
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(duration_[0]), 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(duration_[1]), 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(duration_[2]), 1);
        timer_.select(quiet::Mode::Focus);
        timer_.start(0);
        handle(timer_.update(60000000));
        check(timer_.running() && timer_.mode == quiet::Mode::ShortBreak && prefs_.plan.finished == 1,
              "Focus automatically continues to break");
        check(prefs_.activity.at(quiet::today()) == 60, "Completed focus records its duration");
        handle(timer_.update(120000000));
        check(timer_.running() && timer_.mode == quiet::Mode::Focus && prefs_.plan.finished == 1,
              "Break automatically continues to focus");
        check(prefs_.activity.at(quiet::today()) == 60, "Break time adds no calendar activity");
        for (int phase = 3; phase <= 5; ++phase) {
            handle(timer_.update(phase * 60000000LL));
        }
        check(prefs_.plan.done() && prefs_.plan.finished == 3 && !timer_.running(), "Stop at session goal");
        check(prefs_.tasks[0].done && !prefs_.tasks[1].done, "Session completion preserves manual task marks");
        const auto loaded = quiet::load(path_);
        check(loaded.activity.at(quiet::today()) == 180, "Completed focus time persists to calendar history");
        check(loaded.plan.target == 3 && loaded.plan.finished == 3 && loaded.tasks.back().session == 2, "Plan storage round trip");
        newPlan();
        check(!prefs_.tasks[0].done && prefs_.plan.finished == 0, "New plan reuses and unchecks tasks");
        check(prefs_.activity.at(quiet::today()) == 180, "New plan keeps completed focus history");
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(duration_[0]), 25);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(duration_[1]), 5);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(duration_[2]), 15);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(target_), 4);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(views_[0].checks[0].check), TRUE);
        screenshot("docs/main.png");
        g_signal_emit_by_name(compactButton_, "clicked");
        g_timeout_add(350, +[](gpointer data) -> gboolean {
            auto* app = static_cast<App*>(data);
            int w = 0, h = 0;
            gtk_window_get_size(GTK_WINDOW(app->window_), &w, &h);
            app->check(app->compact_ && std::abs(w - h) <= 1 && w <= 380, "Square compact panel");
            app->check(gtk_widget_get_mapped(app->views_[1].tasks), "Tasks visible in compact mode");
            app->screenshot("docs/compact.png");
            g_signal_emit_by_name(app->views_[1].settings, "clicked");
            app->check(gtk_popover_get_relative_to(GTK_POPOVER(app->popover_)) == app->views_[1].settings,
                       "Compact Settings uses its visible button as the anchor");
            app->screenshot("docs/compact-settings.png");
            app->check(gtk_widget_get_allocated_height(app->popover_) < h - 48,
                       "Compact Settings fits inside the window");
            auto* adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(app->settingsScroll_));
            gtk_adjustment_set_value(adjustment, gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment));
            app->screenshot("docs/compact-settings-actions.png");
            int actionX = 0, actionY = 0;
            gtk_widget_translate_coordinates(app->fullscreenAction_, app->settingsScroll_, 0, 0, &actionX, &actionY);
            app->check(actionY >= 0 && actionY + gtk_widget_get_allocated_height(app->fullscreenAction_) <=
                       gtk_widget_get_allocated_height(app->settingsScroll_),
                       "Scrolling compact Settings reaches the window actions");
            gtk_adjustment_set_value(adjustment, gtk_adjustment_get_lower(adjustment));
            gtk_popover_popdown(GTK_POPOVER(app->popover_));
            gtk_window_resize(GTK_WINDOW(app->window_), 420, 420);
            g_timeout_add(250, +[](gpointer data) -> gboolean {
                auto* app = static_cast<App*>(data);
                int w = 0, h = 0;
                gtk_window_get_size(GTK_WINDOW(app->window_), &w, &h);
                app->check(w == 420 && h == 420, "Compact resize");
                app->setCompact(false);
                gtk_window_resize(GTK_WINDOW(app->window_), 620, 760);
                g_timeout_add(250, +[](gpointer data) -> gboolean {
                    auto* app = static_cast<App*>(data);
                    int w = 0, h = 0;
                    gtk_window_get_size(GTK_WINDOW(app->window_), &w, &h);
                    app->check(w == 620 && h == 760, "Expanded resize");
                    app->checkCenteredLayout();
                    app->screenshot("docs/expanded.png");
                    gtk_window_maximize(GTK_WINDOW(app->window_));
                    g_timeout_add(350, +[](gpointer data) -> gboolean {
                        auto* app = static_cast<App*>(data);
                        if (gtk_window_is_maximized(GTK_WINDOW(app->window_))) {
                            app->checkCenteredLayout();
                            app->screenshot("docs/maximized.png");
                        } else std::cout << "SKIP Maximize unavailable on this display\n";
                        app->smokeFullscreen();
                        return G_SOURCE_REMOVE;
                    }, app);
                    return G_SOURCE_REMOVE;
                }, app);
                return G_SOURCE_REMOVE;
            }, app);
            return G_SOURCE_REMOVE;
        }, this);
    }
};
} // namespace

int main(int argc, char** argv) {
    const bool smoke = argc == 2 && std::string(argv[1]) == "--smoke-test";
    if (argc >= 2 && argc <= 3 && std::string(argv[1]) == "--test-sound") {
        gchar* path = g_build_filename(g_get_user_config_dir(), "quiet-pomodoro", "state.ini", nullptr);
        auto output = quiet::load(path).audioOutput;
        g_free(path);
        if (argc == 3) {
            const std::string option = argv[2];
            if (option == "--output=auto") output = quiet::AudioOutput::Automatic;
            else if (option == "--output=windows") output = quiet::AudioOutput::Windows;
            else if (option == "--output=linux") output = quiet::AudioOutput::Linux;
            else { std::cerr << "Expected --output=auto, --output=windows, or --output=linux\n"; return 1; }
        }
        quiet::Audio audio(output, argc == 2 || output == quiet::AudioOutput::Automatic);
        audio.play(true);
        while (audio.playing()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const auto result = audio.result();
        if (result.ok()) std::cout << "Output accepted the chime: " << result.backend << "\nVerify that you can hear it on your speakers or headphones.\n";
        else std::cerr << "Sound unavailable:\n" << result.error << '\n';
        return result.ok() ? 0 : 1;
    }
    if (argc > 1 && !smoke) {
        std::cout << "Stash 1.4.3\nUsage: stash [--smoke-test | --test-sound [--output=auto|windows|linux]]\n";
        return std::string(argv[1]) == "--help" ? 0 : 1;
    }
    if (smoke && !g_getenv("QUIET_SMOKE_CONFIG")) {
        std::cerr << "Set QUIET_SMOKE_CONFIG to an isolated directory for smoke tests.\n";
        return 1;
    }
    if (smoke) g_setenv("XDG_CONFIG_HOME", g_getenv("QUIET_SMOKE_CONFIG"), TRUE);
    quiet::PixelFont pixelFont;
    if (!gtk_init_check(&argc, &argv)) {
        std::cerr << "Couldn't open a desktop display. Run this app from your graphical desktop.\n";
        return 1;
    }
    g_set_application_name("Stash");
    g_set_prgname("stash");
    App app(smoke);
    gtk_main();
    return app.result();
}
