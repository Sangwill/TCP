#include "timers.h"
#include <sys/time.h>
#include <time.h>

Timers TIMERS;

uint64_t current_ts_msec() {
  struct timespec tp;
  clock_gettime(CLOCK_MONOTONIC, &tp);
  return tp.tv_sec * 1000 + tp.tv_nsec / 1000 / 1000;
}

uint64_t current_ts_usec() {
  struct timespec tp;
  clock_gettime(CLOCK_MONOTONIC, &tp);
  return tp.tv_sec * 1000 * 1000 + tp.tv_nsec / 1000;
}

Timer::Timer(timer_fn fn, uint64_t ts_msec, uint64_t id) {
  this->fn = fn;
  this->ts_msec = ts_msec;
  this->id = id;
}

bool Timer::operator<(const Timer &other) const {
  // smallest ts first
  return this->ts_msec > other.ts_msec || (this->ts_msec == other.ts_msec && this->id > other.id);
}

Timers::Timers() {
  timer_counter = 0;
}

void Timers::trigger() {
  uint64_t cur_ts = current_ts_msec();
  while (!timers.empty()) {
    auto timer = timers.top();
    if (timer.ts_msec > cur_ts) {
      break;
    }
    timers.pop();
    if (removed_timer_ids.find(timer.id) != removed_timer_ids.end()) {
      removed_timer_ids.erase(timer.id);
      continue;
    }
    int res = timer.fn();
    if (res >= 0) {
      reschedule_job(timer.fn, res, timer.id);
    }
  }
}

uint64_t Timers::add_job(timer_fn fn, uint64_t ts_msec) {
  timers.emplace(fn, ts_msec, timer_counter);
  return timer_counter++;
}

void Timers::readd_job(timer_fn fn, uint64_t ts_msec, uint64_t id) {
  timers.emplace(fn, ts_msec, id);
}

uint64_t Timers::schedule_job(timer_fn fn, uint64_t delay_msec) {
  timers.emplace(fn, delay_msec + current_ts_msec(), timer_counter);
  return timer_counter++;
}

void Timers::reschedule_job(timer_fn fn, uint64_t delay_msec, uint64_t id) {
  timers.emplace(fn, delay_msec + current_ts_msec(), id);
}

void Timers::remove_job(uint64_t id) {
  removed_timer_ids.emplace(id);
}