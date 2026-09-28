/* Headless guard for the async RX ring telemetry counters (src/RxRingStats.h): how a URB completion, a re-arm, a
 * failed resubmit and an inline consume move the counters, and what a periodic snapshot reads and resets. The
 * rx.ring event and the RxRingCallback both carry this snapshot. Pure logic; no libusb, no hardware. */
#include "RxRingStats.h"

#include <cstdio>
#include <cstring>

using namespace devourer;

static int g_fail = 0;

static void check(const char *what, long long got, long long want) {
  if (got != want) {
    std::printf("FAIL %s: got %lld want %lld\n", what, got, want);
    ++g_fail;
  }
}

int main() {
  RxRingCounters c;
  c.reset(8); /* 8 URBs posted at start */

  /* Three completions with no re-arm in between: the ring drains 8 -> 5. */
  c.on_complete();
  c.on_complete();
  c.on_complete();
  c.on_consume_us(120);
  c.on_consume_us(4000); /* worst inline consume in the window */
  c.on_consume_us(300);
  RxRingStats s = c.snapshot("async", 8, -1, 0);
  check("armed after 3 completions", s.armed, 5);
  check("min_armed", s.min_armed, 5);
  check("completions", (long long)s.completions, 3);
  check("empties (never reached 0)", (long long)s.empties, 0);
  check("cb_max_us", s.cb_max_us, 4000);
  check("n_urbs", s.n_urbs, 8);
  check("pool_free", s.pool_free, -1);
  if (std::strcmp(s.mode, "async") != 0) {
    std::printf("FAIL mode: got %s\n", s.mode);
    ++g_fail;
  }

  /* The snapshot restarts the window: min_armed back to the current depth, cb_max to 0; totals stay cumulative. */
  s = c.snapshot("async", 8, -1, 0);
  check("min_armed reset to current depth", s.min_armed, 5);
  check("cb_max_us reset", s.cb_max_us, 0);
  check("completions cumulative", (long long)s.completions, 3);

  /* Re-arm all three, then drain the whole ring: the completion that leaves 0 armed is an empty. */
  c.on_armed();
  c.on_armed();
  c.on_armed();
  for (int i = 0; i < 8; ++i)
    c.on_complete();
  c.on_resubmit_fail();
  s = c.snapshot("spsc-fat", 8, 3, 2);
  check("armed after full drain", s.armed, 0);
  check("min_armed hit 0", s.min_armed, 0);
  check("empties = completions that left 0 armed", (long long)s.empties, 1);
  check("completions total", (long long)s.completions, 11);
  check("resubmit_fail", (long long)s.resubmit_fail, 1);
  check("pool_free passed through", s.pool_free, 3);
  check("qdepth passed through", s.qdepth, 2);

  /* A later window only sees its own worst consume. */
  c.on_consume_us(50);
  s = c.snapshot("spsc-fat", 8, 3, 0);
  check("cb_max_us of the new window", s.cb_max_us, 50);

  if (g_fail == 0)
    std::printf("rx_ring_counters: all checks passed\n");
  return g_fail == 0 ? 0 : 1;
}
