#pragma once
#include <algorithm>

namespace quiet {
// Progress belongs to the plan; browsing a task list never changes this cursor.
struct SessionPlan {
    int target = 4;
    int finished = 0;

    bool done() const { return finished >= target; }
    int current() const { return std::min(finished + 1, target); }
    void normalize() {
        target = std::clamp(target, 1, 99);
        finished = std::clamp(finished, 0, target);
    }
    bool complete() {
        if (done()) return false;
        ++finished;
        return true;
    }
    void restart() { finished = 0; }
};
} // namespace quiet
