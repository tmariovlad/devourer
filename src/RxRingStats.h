#pragma once
/* Async RX ring telemetry: the counters the URB callback updates, and the periodic snapshot that both the rx.ring
 * event and an embedder's RxRingCallback receive (DeviceConfig::Rx::on_ring). Relaxed atomics, best-effort: a rare
 * lost update is acceptable for a diagnostic counter. Only touched when telemetry is on (ring_ms > 0), so the default
 * RX path pays nothing. Selftest: tests/rx_ring_counters_selftest.cpp. */
#include <atomic>
#include <functional>

namespace devourer {

/* One telemetry window. `armed` = URBs posted to the host controller now (the depth that collapses when a slow
 * inline consumer delays resubmit); `min_armed` = its low-water mark in the window; `cb_max_us` = the worst inline
 * consume in the window. `completions` / `empties` / `resubmit_fail` are cumulative: empties = completions that left
 * zero URBs posted, so empties/completions is the host-starvation rate (RF loss leaves the ring armed, host
 * starvation drains it). `dropped` (cumulative) = received buffers the host discarded because the spsc pool was
 * exhausted: the one host-side SW drop the ring can see (0 in async/reorder modes, which consume inline instead).
 * `pool_free` = spare buffers in the host pool (-1 = plain async, no pool); `qdepth` = spsc
 * consumer backlog. */
struct RxRingStats {
  const char *mode = "async";
  int n_urbs = 0;
  int armed = 0;
  int min_armed = 0;
  long long cb_max_us = 0;
  unsigned long long resubmit_fail = 0;
  unsigned long long completions = 0;
  unsigned long long empties = 0;
  unsigned long long dropped = 0;
  long long pool_free = -1;
  long long qdepth = 0;
};

/* Called on the RX pump thread once per telemetry window; keep it short (store, don't block). */
using RxRingCallback = std::function<void(const RxRingStats &)>;

class RxRingCounters {
public:
  /* Start of the ring: `armed0` URBs posted. */
  void reset(int armed0) {
    _armed.store(armed0, std::memory_order_relaxed);
    _min_armed.store(armed0, std::memory_order_relaxed);
  }

  /* A URB completed: it has left the wire until resubmitted. */
  void on_complete() {
    const int a = _armed.fetch_sub(1, std::memory_order_relaxed) - 1;
    lower(_min_armed, a);
    _completions.fetch_add(1, std::memory_order_relaxed);
    if (a <= 0)
      _empties.fetch_add(1, std::memory_order_relaxed);
  }

  /* A URB went back on the wire. */
  void on_armed() { _armed.fetch_add(1, std::memory_order_relaxed); }

  /* A resubmit that was wanted failed. */
  void on_resubmit_fail() { _resubmit_fail.fetch_add(1, std::memory_order_relaxed); }

  /* A received buffer was discarded host-side (spsc pool exhausted). */
  void on_dropped() { _dropped.fetch_add(1, std::memory_order_relaxed); }

  /* One inline consume took `us` microseconds. */
  void on_consume_us(long long us) {
    long long cur = _cb_max_us.load(std::memory_order_relaxed);
    while (us > cur && !_cb_max_us.compare_exchange_weak(cur, us, std::memory_order_relaxed))
      ;
  }

  /* Read the window and start the next one: min_armed restarts at the current depth, cb_max_us at 0. */
  RxRingStats snapshot(const char *mode, int n_urbs, long long pool_free, long long qdepth) {
    RxRingStats s;
    s.mode = mode;
    s.n_urbs = n_urbs;
    s.armed = _armed.load(std::memory_order_relaxed);
    s.min_armed = _min_armed.exchange(s.armed, std::memory_order_relaxed);
    s.cb_max_us = _cb_max_us.exchange(0, std::memory_order_relaxed);
    s.resubmit_fail = _resubmit_fail.load(std::memory_order_relaxed);
    s.completions = _completions.load(std::memory_order_relaxed);
    s.empties = _empties.load(std::memory_order_relaxed);
    s.dropped = _dropped.load(std::memory_order_relaxed);
    s.pool_free = pool_free;
    s.qdepth = qdepth;
    return s;
  }

private:
  static void lower(std::atomic<int> &m, int v) {
    int cur = m.load(std::memory_order_relaxed);
    while (v < cur && !m.compare_exchange_weak(cur, v, std::memory_order_relaxed))
      ;
  }

  std::atomic<int> _armed{0};
  std::atomic<int> _min_armed{0};
  std::atomic<long long> _cb_max_us{0};
  std::atomic<unsigned long long> _resubmit_fail{0};
  std::atomic<unsigned long long> _completions{0};
  std::atomic<unsigned long long> _empties{0};
  std::atomic<unsigned long long> _dropped{0};
};

} // namespace devourer
