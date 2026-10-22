#include "events.hpp"
#include <iostream>
#include <string>

static void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
int main() {
  using namespace system816;
  try {
    EventClock clock;
    check(clock.now() == 0 && !clock.nmi_pending() && !clock.irq_asserted(), "initial clock state");
    clock.schedule(3, Signal::nmi);
    clock.schedule(1, Signal::irq_on);
    clock.schedule(2, Signal::irq_off);
    clock.advance(0);
    check(clock.now() == 0 && !clock.irq_asserted(), "future event delivered early");
    clock.advance(1);
    check(clock.now() == 1 && clock.irq_asserted() && !clock.nmi_pending(), "IRQ assertion time");
    clock.advance(1);
    check(!clock.irq_asserted() && !clock.nmi_pending(), "IRQ release time");
    clock.advance(1);
    check(clock.nmi_pending(), "NMI was not latched");
    clock.advance(10);
    check(clock.nmi_pending() && clock.now() == 13, "NMI latch lost");
    bool rejected = false;
    try { clock.schedule(12, Signal::irq_on); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && clock.now() == 13 && !clock.irq_asserted(), "past event changed state");
    EventClock ordered;
    ordered.schedule(5, Signal::irq_on);
    ordered.schedule(5, Signal::irq_off);
    ordered.schedule(5, Signal::irq_on);
    ordered.advance(5);
    check(ordered.irq_asserted(), "same-time insertion ordering");
    ordered.schedule(5, Signal::irq_off);
    ordered.advance(0);
    check(!ordered.irq_asserted(), "event at current time ignored");
    EventClock jumped;
    jumped.schedule(2, Signal::irq_on); jumped.schedule(4, Signal::irq_off);
    jumped.schedule(7, Signal::nmi); jumped.advance(100);
    check(jumped.now() == 100 && !jumped.irq_asserted() && jumped.nmi_pending(), "skipped events on time jump");
    auto copy = jumped;
    copy.schedule(100, Signal::irq_on); copy.advance(0);
    check(copy.irq_asserted() && !jumped.irq_asserted(), "clock copy shares queue/state");
    EventClock full;
    const auto maximum = std::numeric_limits<uint64_t>::max();
    full.schedule(maximum, Signal::irq_on);
    full.advance(maximum - 1);
    bool overflow = false;
    try { full.advance(2); } catch (const std::overflow_error&) { overflow = true; }
    check(overflow && full.now() == maximum - 1 && !full.irq_asserted(), "overflow partially delivered events");
    full.advance(1);
    check(full.now() == maximum && full.irq_asserted() && !full.can_tick(), "maximum time boundary");
    full = EventClock{};
    full.advance(100);
    check(full.now() == 100 && !full.irq_asserted() && !full.nmi_pending(), "reset retained scheduled events");
    std::cout << "Logical event clock: ordering, signals, copies, reset and overflow passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
