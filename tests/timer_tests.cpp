#include "timer.hpp"
#include <cstdlib>
#include <iostream>

void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

int main() {
    using quiet::Mode;
    quiet::Timer timer;
    check(timer.display() == "25:00", "initial numeric display");
    timer.start(0);
    timer.update(500001);
    check(timer.display() == "25:00", "round fractional seconds up");
    timer.update(1000000);
    check(timer.display() == "24:59", "elapsed time, not tick count");
    timer.pause(10200000);
    check(!timer.running() && timer.seconds() == 1490, "pause preserves remaining time");
    timer.start(100000000);
    timer.update(100800000);
    check(timer.seconds() == 1489, "resume preserves fractional second");
    timer.reset();
    check(timer.seconds() == 1500 && !timer.running(), "reset stops and restores duration");

    timer.minutes = {1, 1, 1};
    timer.reset();
    timer.start(0);
    check(!timer.update(49999999).warning, "warning is not early");
    check(timer.update(50000000).warning, "warning at ten seconds remaining");
    check(!timer.update(51000000).warning, "warning only once");
    timer.pause(52000000);
    timer.start(100000000);
    check(!timer.update(101000000).warning, "resuming cannot repeat warning");
    auto tick = timer.update(108000000);
    check(tick.complete && tick.focusComplete, "completion after pause and resume");
    check(tick.focusSeconds == 60, "completed duration excludes paused time");
    check(timer.mode == Mode::ShortBreak && timer.running(), "focus automatically starts break");
    check(!timer.update(108000000).complete, "completion emitted only once at boundary");
    tick = timer.update(168000000);
    check(tick.complete && !tick.focusComplete && timer.mode == Mode::Focus && timer.running(),
          "break automatically starts focus");
    check(tick.focusSeconds == 0, "break completion has no focus duration");

    for (int session = 2; session <= 4; ++session) {
        timer.select(Mode::Focus);
        timer.start(0);
        tick = timer.update(90000000);
        check(tick.complete && !tick.warning, "delayed tick completes without stale warning");
        check(timer.mode == (session == 4 ? Mode::LongBreak : Mode::ShortBreak), "four-session cycle");
    }
    check(timer.running(), "long break starts automatically");
    tick = timer.update(150000000);
    check(tick.complete && !tick.focusComplete && timer.mode == Mode::Focus && timer.running(), "long break returns to running focus");
    tick = timer.pause(210000000);
    check(tick.complete && tick.focusComplete, "pause at deadline still records completion");
    check(!timer.running() && timer.mode == Mode::ShortBreak, "pause at boundary pauses the next phase");
    check(!timer.update(900000000).complete, "paused timer cannot auto-advance");
    timer.start(900000000);
    check(timer.update(950000000).warning, "next phase gets its own ten-second warning");
    check(!timer.update(951000000).warning, "next phase warning is also emitted only once");
    timer.select(Mode::LongBreak);
    check(!timer.running() && timer.display() == "01:00", "changing mode resets and pauses");
    timer.start(0);
    tick = timer.update(3600000000);
    check(tick.complete && !tick.focusComplete && timer.running() && timer.seconds() == 60,
          "sleep advances one phase and starts a full fresh timer");
    timer.reset();
    check(!timer.running(), "reset stops the automatic loop");
    timer.minutes[0] = 25;
    timer.select(Mode::Focus);
    timer.start(0);
    timer.minutes[0] = 90;
    tick = timer.update(1500000000);
    check(tick.focusSeconds == 1500, "settings changes do not inflate the active session's recorded duration");
    check(!timer.update(1500000000).focusSeconds, "repeated update cannot record the session twice");
    timer.select(Mode::Focus);
    timer.start(0);
    tick = timer.update(86400000000LL);
    check(tick.focusSeconds == 5400, "delayed updates record one scheduled focus interval");
    std::cout << "Timer checks passed\n";
}
