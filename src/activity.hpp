#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>

namespace quiet {
using Activity = std::map<std::string, std::int64_t>;
bool validActivityDate(const std::string& date);
// Sunday-first columns: 52 previous weeks and the current week. Future cells are empty.
std::array<std::string, 371> calendarDays(const std::string& date);
int activityLevel(std::int64_t seconds);
void recordActivity(Activity& activity, const std::string& date, int seconds);
} // namespace quiet
