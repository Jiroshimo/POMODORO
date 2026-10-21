#pragma once
#include "plan.hpp"
#include "audio.hpp"
#include <array>
#include <string>
#include <vector>

namespace quiet {
struct Task { std::string text; bool done = false; int session = 1; };
struct Preferences {
    std::array<int, 3> minutes{25, 5, 15};
    bool sound = true;
    AudioOutput audioOutput = AudioOutput::Automatic;
    int completed = 0;
    std::string date;
    SessionPlan plan;
    std::vector<Task> tasks;
};
std::string today();
Preferences load(const std::string& path);
bool save(const std::string& path, const Preferences& prefs, std::string& error);
} // namespace quiet
