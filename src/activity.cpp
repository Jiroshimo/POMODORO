#include "activity.hpp"
#include <glib.h>
#include <algorithm>
#include <cstdio>
#include <limits>

namespace quiet {
namespace {
GDate parseDate(const std::string& text) {
    GDate date{};
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') return date;
    for (std::size_t i = 0; i < text.size(); ++i)
        if (i != 4 && i != 7 && (text[i] < '0' || text[i] > '9')) return date;
    const int year = std::stoi(text.substr(0, 4));
    const int month = std::stoi(text.substr(5, 2));
    const int day = std::stoi(text.substr(8, 2));
    if (g_date_valid_dmy(day, static_cast<GDateMonth>(month), year))
        g_date_set_dmy(&date, day, static_cast<GDateMonth>(month), year);
    return date;
}
}
bool validActivityDate(const std::string& date) {
    auto parsed = parseDate(date);
    return g_date_valid(&parsed);
}
std::array<std::string, 371> calendarDays(const std::string& text) {
    std::array<std::string, 371> days{};
    auto date = parseDate(text);
    if (!g_date_valid(&date)) return days;
    const auto today = g_date_get_julian(&date);
    const int offset = 364 + g_date_get_weekday(&date) % 7;
    if (today <= static_cast<guint>(offset)) return days;
    g_date_subtract_days(&date, offset);
    for (auto& day : days) {
        if (g_date_get_julian(&date) > today) break;
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u",
                      g_date_get_year(&date), g_date_get_month(&date), g_date_get_day(&date));
        day = buffer;
        g_date_add_days(&date, 1);
    }
    return days;
}
int activityLevel(std::int64_t seconds) {
    return seconds <= 0 ? 0 : seconds < 3600 ? 1 : seconds < 7200 ? 2 : seconds < 14400 ? 3 : 4;
}
void recordActivity(Activity& activity, const std::string& date, int seconds) {
    if (seconds <= 0 || !validActivityDate(date)) return;
    auto& total = activity[date];
    total += std::min<std::int64_t>(seconds, std::numeric_limits<std::int64_t>::max() - total);
}
} // namespace quiet
