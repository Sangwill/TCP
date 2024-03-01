#ifndef __TIMERS_H__
#define __TIMERS_H__

#include <functional>
#include <queue>
#include <set>
#include <stdint.h>
#include <vector>

// return -1 means break
// return >=0 means schedule in res ms
typedef std::function<int()> timer_fn;

// Return monotonic timestamp in msecs
uint64_t current_ts_msec();

// Return monotonic timestamp in usecs
uint64_t current_ts_usec();

class Timer {
public:
  Timer(timer_fn fn, uint64_t ts_msec, uint64_t id);

  // Callback function
  timer_fn fn;
  // Timestamp in msecs
  uint64_t ts_msec;
  // Timer id
  uint64_t id;

  bool operator<(const Timer &other) const;
};

class Timers {
  friend class Timer;
private:
  std::priority_queue<Timer> timers;
  std::set<uint64_t> removed_timer_ids;
  uint64_t timer_counter;

public:
  Timers();

  // Trigger timers
  void trigger();

  // Add job to timers
  uint64_t add_job(timer_fn fn, uint64_t ts_msec);

  // Readd job to timers
  void readd_job(timer_fn fn, uint64_t ts_msec, uint64_t id);

  // Schedule job to timers in msecs later
  uint64_t schedule_job(timer_fn fn, uint64_t delay_msec);

  // Reschedule job to timers in msecs later
  void reschedule_job(timer_fn fn, uint64_t delay_msec, uint64_t id);

  // Remove job from timers
  void remove_job(uint64_t id);
};

extern Timers TIMERS;

#endif