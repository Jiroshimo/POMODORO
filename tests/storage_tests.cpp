#include "storage.hpp"
#include <glib.h>
#include <glib/gstdio.h>
#include <cstdlib>
#include <iostream>

void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

int main() {
    gchar* temp = g_dir_make_tmp("quiet-storage-XXXXXX", nullptr);
    check(temp != nullptr, "temporary test directory");
    const std::string path = std::string(temp) + "/state.ini";
    auto prefs = quiet::load(path);
    check(prefs.minutes[0] == 25 && prefs.sound && prefs.tasks.empty(), "missing file defaults");
    prefs.minutes = {35, 7, 20};
    prefs.tasks = {{"Read <chapter> & write notes ✓", false, 1}, {"A task\nwith a new line", true, 3}};
    prefs.plan.target = 6;
    prefs.plan.finished = 2;
    prefs.sound = false;
    prefs.audioOutput = quiet::AudioOutput::Linux;
    prefs.completed = 3;
    std::string error;
    check(quiet::save(path, prefs, error), "save preferences");
    auto restored = quiet::load(path);
    check(restored.minutes == prefs.minutes && !restored.sound && restored.completed == 3, "settings round trip");
    check(restored.audioOutput == quiet::AudioOutput::Linux, "audio output preference round trip");
    check(restored.tasks.size() == 2 && restored.tasks[0].text == prefs.tasks[0].text && restored.tasks[1].done,
          "task text and completion round trip");
    check(restored.tasks[1].session == 3 && restored.plan.target == 6 && restored.plan.finished == 2,
          "session assignments and plan progress round trip");
    prefs.date = "2000-01-01";
    check(quiet::save(path, prefs, error), "save earlier date");
    restored = quiet::load(path);
    check(restored.completed == 0 && restored.tasks.size() == 2, "new day resets count and preserves tasks");
    check(restored.plan.finished == 2, "plan progress survives date rollover");
    const char* legacy = "[timer]\nfocus=25\n[tasks]\ncount=1\n[task-0]\ntext=Existing task\ndone=true\n";
    g_file_set_contents(path.c_str(), legacy, -1, nullptr);
    restored = quiet::load(path);
    check(restored.audioOutput == quiet::AudioOutput::Automatic, "old settings default to automatic audio output");
    check(restored.plan.target == 4 && restored.plan.finished == 0 && restored.tasks.size() == 1 &&
          restored.tasks[0].session == 1 && restored.tasks[0].done, "old data migrates to session one");
    const char* malformed = "[timer]\nfocus=-99\nshort-break=0\nlong-break=9999\n[tasks]\ncount=-1\n";
    g_file_set_contents(path.c_str(), malformed, -1, nullptr);
    restored = quiet::load(path);
    check(restored.minutes == std::array<int, 3>{25, 5, 15} && restored.tasks.empty(), "invalid values fall back safely");
    const char* badPlan = "[plan]\ntarget=9999\nfinished=99999\n[tasks]\ncount=2\n[task-0]\ntext=First\nsession=-3\n[task-1]\ntext=Second\nsession=999\n";
    g_file_set_contents(path.c_str(), badPlan, -1, nullptr);
    restored = quiet::load(path);
    check(restored.plan.target == 99 && restored.plan.finished == 99 && restored.tasks[0].session == 1 &&
          restored.tasks[1].session == 99, "invalid plan and task session bounds");
    prefs.plan.target = 1;
    prefs.plan.finished = 0;
    check(quiet::save(path, prefs, error), "save smaller plan");
    restored = quiet::load(path);
    check(restored.tasks[1].session == 3, "reducing target preserves future session tasks");
    check(!quiet::save(std::string(temp), prefs, error) && !error.empty(), "write failure reported");
    g_remove(path.c_str());
    g_rmdir(temp);
    g_free(temp);
    std::cout << "Storage checks passed\n";
}
