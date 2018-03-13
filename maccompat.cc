#include "maccompat.h"
#include <errno.h>

#ifdef __MACH__

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <unistd.h>

int clock_gettime(clockid_t clk_id, struct timespec* tp)
{
  /// timebase information, retrieved from system at first call of function
  static mach_timebase_info_data_t tb = { 0, 0 };

  // monotonic clock implemented with MacOS primitives
  if (clk_id == CLOCK_MONOTONIC) {
    uint64_t now = mach_absolute_time();
    if (tb.denom == 0) {
      mach_timebase_info(&tb);
    }
    uint64_t now_ns = (now * tb.numer / tb.denom);
    // the compiler should be optimizing with -O2/-O3 to use a single instruction
    // to compute both division result and remainder
    tp->tv_sec = (now_ns / 1000000000);
    tp->tv_nsec = (now_ns % 1000000000);
    return 0;
  // realtime clock (real calendar time) implemented at us precision with gettimeofday
  } else if (clk_id == CLOCK_REALTIME) {
    timeval tv;
    gettimeofday(&tv, NULL);
    tp->tv_sec = tv.tv_sec;
    tp->tv_nsec = tv.tv_usec * 1000;
    return 0;
  // invalid clock type, returns an error after setting errno appropriately
  } else {
    errno = EINVAL;
    return -1;
  }
}

#endif   // defined(__MACH__)
