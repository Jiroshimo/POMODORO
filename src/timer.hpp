#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace quiet {

enum class Mode { Focus, ShortBreak, LongBreak };
struct Tick { bool warning = false; bool complete = false; bool focusComplete = false; };

// Time is injected in microseconds so timer behavior is independent of UI ticks.
class Timer {
public:
    std::array<int, 3> minutes{25, 5, 15};
    Mode mode = Mode::Focus;
    int cycle = 0;

    int seconds() const { return static_cast<int>((remaining_ + 999999) / 1000000); }
    bool running() const { return running_; }

    void select(Mode value) { mode = value; reset(); }
    void reset() {
        running_ = false;
        warned_ = false;
        remaining_ = static_cast<std::int64_t>(minutes[static_cast<int>(mode)]) * 60000000;
    }
    void start(std::int64_t now) {
        if (running_) return;
        deadline_ = now + remaining_;
        running_ = true;
    }
    Tick pause(std::int64_t now) {
        Tick result = update(now);
        running_ = false;
        return result;
    }
    Tick update(std::int64_t now) {
        Tick result;
        if (!running_) return result;
        remaining_ = std::max<std::int64_t>(0, deadline_ - now);
        if (remaining_ == 0) {
            result.complete = true;
            result.focusComplete = mode == Mode::Focus;
            if (result.focusComplete) {
                ++cycle;
                mode = cycle % 4 == 0 ? Mode::LongBreak : Mode::ShortBreak;
            } else {
                mode = Mode::Focus;
            }
            reset();
            // Start one fresh phase at the observed completion time. Delayed
            // updates (including sleep) must not invent unattended sessions.
            start(now);
        } else if (remaining_ <= 10000000 && !warned_) {
            result.warning = true;
            warned_ = true;
        }
        return result;
    }
    std::string display() const {
        char text[16];
        std::snprintf(text, sizeof(text), "%02d:%02d", seconds() / 60, seconds() % 60);
        return text;
    }

private:
    std::int64_t remaining_ = 25LL * 60000000;
    std::int64_t deadline_ = 0;
    bool running_ = false;
    bool warned_ = false;
};
} // namespace quiet
