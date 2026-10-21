#include "plan.hpp"
#include "timer.hpp"
#include <cstdlib>
#include <iostream>

void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main() {
    quiet::SessionPlan plan;
    check(plan.target == 4 && plan.current() == 1 && !plan.done(), "default plan");
    for (int i = 1; i <= 4; ++i) {
        check(plan.complete() && plan.finished == i, "advance once per focus completion");
        check(plan.current() == (i == 4 ? 4 : i + 1), "next session cursor");
    }
    check(plan.done() && !plan.complete() && plan.finished == 4, "stop at goal");
    plan.target = 6;
    plan.normalize();
    check(!plan.done() && plan.current() == 5, "increasing target extends plan");
    plan.target = 2;
    plan.normalize();
    check(plan.finished == 2 && plan.done(), "decreasing target clamps progress");
    plan.restart();
    check(plan.finished == 0 && plan.current() == 1 && plan.target == 2, "new plan retains goal");
    plan.target = 0;
    plan.finished = -2;
    plan.normalize();
    check(plan.target == 1 && plan.finished == 0, "invalid lower bounds");
    plan.target = 500;
    plan.finished = 500;
    plan.normalize();
    check(plan.target == 99 && plan.finished == 99, "upper bounds");
    quiet::Timer timer;
    timer.minutes = {1, 1, 1};
    timer.reset();
    plan = {3, 0};
    timer.start(0);
    for (int phase = 1; phase <= 5; ++phase) {
        const auto event = timer.update(phase * 60000000LL);
        check(event.complete, "automatic phase completes");
        if (event.focusComplete) plan.complete();
        if (plan.done()) timer.select(quiet::Mode::Focus);
        check(plan.finished == (phase + 1) / 2, "only focus phases count toward the target");
        check(timer.running() == !plan.done(), "automatic loop runs until the goal");
    }
    check(plan.finished == 3 && !timer.update(999000000).complete, "target prevents another automatic break");
    std::cout << "Plan checks passed\n";
}
