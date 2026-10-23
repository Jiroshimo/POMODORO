#include "activity.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>

void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main() {
    quiet::Activity activity;
    quiet::recordActivity(activity, "2026-10-23", 25 * 60);
    quiet::recordActivity(activity, "2026-10-23", 35 * 60);
    quiet::recordActivity(activity, "2026-10-24", 90 * 60);
    check(activity.at("2026-10-23") == 3600 && activity.at("2026-10-24") == 5400,
          "completed sessions accumulate by date and duration");
    quiet::recordActivity(activity, "2026-02-30", 60);
    quiet::recordActivity(activity, "2026-10-25", 0);
    quiet::recordActivity(activity, "2026-10-25", -60);
    check(activity.size() == 2, "invalid dates and unfinished sessions add no activity");
    check(quiet::activityLevel(0) == 0 && quiet::activityLevel(1) == 1 &&
          quiet::activityLevel(3599) == 1 && quiet::activityLevel(3600) == 2 &&
          quiet::activityLevel(7200) == 3 && quiet::activityLevel(14400) == 4 &&
          quiet::activityLevel(100000) == 4, "hour thresholds have stable highlight levels");
    const auto friday = quiet::calendarDays("2026-10-23");
    check(friday.front() == "2025-10-19" && friday[364] == "2026-10-18" &&
          friday[369] == "2026-10-23" && friday[370].empty(),
          "calendar starts on Sunday, includes today and leaves future days empty");
    const auto sunday = quiet::calendarDays("2026-10-25");
    check(sunday[364] == "2026-10-25" && sunday[365].empty(), "Sunday starts the final column");
    const auto leap = quiet::calendarDays("2024-03-01");
    check(leap[368] == "2024-02-29" && leap[369] == "2024-03-01", "calendar includes leap day");
    check(!quiet::validActivityDate("2026-02-29") && quiet::validActivityDate("2024-02-29") &&
          !quiet::validActivityDate("2026-1-01") && !quiet::validActivityDate("abcdefghij") &&
          quiet::calendarDays("invalid").front().empty(), "malformed dates are rejected");
    activity["2026-10-23"] = std::numeric_limits<std::int64_t>::max() - 1;
    quiet::recordActivity(activity, "2026-10-23", 60);
    check(activity.at("2026-10-23") == std::numeric_limits<std::int64_t>::max(), "totals do not overflow");
    std::cout << "Activity checks passed\n";
}
