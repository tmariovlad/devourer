#ifndef DEVOURER_RX_STOP_H
#define DEVOURER_RX_STOP_H

#include <atomic>
#include <functional>

#include "SignalStop.h" /* g_devourer_should_stop */

namespace devourer {

/* The stop request of a device's blocking RX loop (StartRxLoop / StopRxLoop),
 * one per device, shared by every generation.
 *
 * Contract: a stop requested BEFORE or WHILE the loop runs ends it. The loop
 * consumes the request when it exits, so the device stays restartable (stop,
 * join, start again runs until the next stop). The process-wide signal flag
 * (g_devourer_should_stop) also ends it, as before.
 *
 * Why: the old contract had each StartRxLoop clear the request on entry. That
 * lost a stop landing between an embedder's "should I still start?" check and
 * the clear, and the loop then ran until the process died:
 * - PixelPilot's WfbngLink::run checks stop_requested, then calls StartRxLoop,
 *   while nativeStop arrives on the JNI thread. The adapter looked stuck on the
 *   next start (pixelpilot-xr, 2026-10-01).
 * - examples/timesync main.cpp:391 calls StopRxLoop and then joins an RX thread
 *   that may not have entered the loop yet, which is the same shape.
 * The loop polls requested() through the transport's stop predicate on every
 * event wake, packets or not, so a request that is not cleared is always seen.
 *
 * Thread-safe: request() from any thread; run() on the RX thread. */
class RxStopLatch {
public:
  void request() { requested_.store(true, std::memory_order_release); }

  bool requested() const {
    return requested_.load(std::memory_order_acquire) || g_devourer_should_stop;
  }

  /* One blocking loop: `loop(stop)` must return once stop() is true (it is
   * handed to bulk_read_async_loop as the stop predicate). The request is
   * consumed after the loop returns, never on entry. */
  template <class Loop> void run(Loop &&loop) {
    std::function<bool()> stop = [this]() -> bool { return requested(); };
    loop(stop);
    requested_.store(false, std::memory_order_release);
  }

private:
  std::atomic<bool> requested_{false};
};

} // namespace devourer

#endif
