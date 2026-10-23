#include "storage.hpp"
#include <glib.h>
#include <glib/gstdio.h>
#include <algorithm>

namespace quiet {
std::string today() {
    GDateTime* now = g_date_time_new_now_local();
    gchar* text = g_date_time_format(now, "%Y-%m-%d");
    std::string result(text);
    g_free(text);
    g_date_time_unref(now);
    return result;
}

Preferences load(const std::string& path) {
    Preferences prefs;
    prefs.date = today();
    GKeyFile* file = g_key_file_new();
    if (!g_key_file_load_from_file(file, path.c_str(), G_KEY_FILE_NONE, nullptr)) {
        g_key_file_unref(file);
        return prefs;
    }
    const char* keys[] = {"focus", "short-break", "long-break"};
    for (int i = 0; i < 3; ++i) {
        const int value = g_key_file_get_integer(file, "timer", keys[i], nullptr);
        if (value >= 1 && value <= 180) prefs.minutes[i] = value;
    }
    if (g_key_file_has_key(file, "timer", "sound", nullptr))
        prefs.sound = g_key_file_get_boolean(file, "timer", "sound", nullptr);
    gchar* output = g_key_file_get_string(file, "timer", "audio-output", nullptr);
    if (g_strcmp0(output, "windows") == 0) prefs.audioOutput = AudioOutput::Windows;
    else if (g_strcmp0(output, "linux") == 0) prefs.audioOutput = AudioOutput::Linux;
    g_free(output);
    gchar* date = g_key_file_get_string(file, "timer", "date", nullptr);
    if (date && prefs.date == date)
        prefs.completed = std::max(0, g_key_file_get_integer(file, "timer", "completed", nullptr));
    g_free(date);
    if (g_key_file_has_key(file, "plan", "target", nullptr))
        prefs.plan.target = g_key_file_get_integer(file, "plan", "target", nullptr);
    prefs.plan.finished = g_key_file_get_integer(file, "plan", "finished", nullptr);
    prefs.plan.normalize();
    gsize dayCount = 0;
    gchar** days = g_key_file_get_keys(file, "activity", &dayCount, nullptr);
    for (gsize i = 0; i < dayCount; ++i) {
        const auto seconds = g_key_file_get_int64(file, "activity", days[i], nullptr);
        if (validActivityDate(days[i]) && seconds > 0) prefs.activity[days[i]] = seconds;
    }
    g_strfreev(days);
    const int count = std::clamp(g_key_file_get_integer(file, "tasks", "count", nullptr), 0, 1000);
    for (int i = 0; i < count; ++i) {
        const auto group = "task-" + std::to_string(i);
        gchar* text = g_key_file_get_string(file, group.c_str(), "text", nullptr);
        if (text && *text && g_utf8_validate(text, -1, nullptr)) {
            const int session = std::clamp(g_key_file_get_integer(file, group.c_str(), "session", nullptr), 1, 99);
            prefs.tasks.push_back({text, g_key_file_get_boolean(file, group.c_str(), "done", nullptr) != 0, session});
        }
        g_free(text);
    }
    g_key_file_unref(file);
    return prefs;
}

bool save(const std::string& path, const Preferences& prefs, std::string& error) {
    GKeyFile* file = g_key_file_new();
    const char* keys[] = {"focus", "short-break", "long-break"};
    for (int i = 0; i < 3; ++i)
        g_key_file_set_integer(file, "timer", keys[i], prefs.minutes[i]);
    g_key_file_set_boolean(file, "timer", "sound", prefs.sound);
    const char* output = prefs.audioOutput == AudioOutput::Windows ? "windows" :
        prefs.audioOutput == AudioOutput::Linux ? "linux" : "automatic";
    g_key_file_set_string(file, "timer", "audio-output", output);
    g_key_file_set_integer(file, "timer", "completed", prefs.completed);
    g_key_file_set_string(file, "timer", "date", prefs.date.c_str());
    g_key_file_set_integer(file, "plan", "target", prefs.plan.target);
    g_key_file_set_integer(file, "plan", "finished", prefs.plan.finished);
    for (const auto& day : prefs.activity)
        if (validActivityDate(day.first) && day.second > 0)
            g_key_file_set_int64(file, "activity", day.first.c_str(), day.second);
    g_key_file_set_integer(file, "tasks", "count", static_cast<int>(prefs.tasks.size()));
    for (std::size_t i = 0; i < prefs.tasks.size(); ++i) {
        const auto group = "task-" + std::to_string(i);
        g_key_file_set_string(file, group.c_str(), "text", prefs.tasks[i].text.c_str());
        g_key_file_set_boolean(file, group.c_str(), "done", prefs.tasks[i].done);
        g_key_file_set_integer(file, group.c_str(), "session", prefs.tasks[i].session);
    }
    gchar* parent = g_path_get_dirname(path.c_str());
    g_mkdir_with_parents(parent, 0700);
    g_free(parent);
    GError* failure = nullptr;
    gsize length = 0;
    gchar* data = g_key_file_to_data(file, &length, nullptr);
    const bool ok = g_file_set_contents(path.c_str(), data, static_cast<gssize>(length), &failure);
    if (failure) { error = failure->message; g_error_free(failure); }
    g_free(data);
    g_key_file_unref(file);
    return ok;
}
} // namespace quiet
