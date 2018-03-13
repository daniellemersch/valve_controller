#ifndef __MAC_COMPAT_H
#define __MAC_COMPAT_H

// only compile this code on MacOS X
#ifdef __MACH__

#include <time.h>
#include <sys/time.h>

// define types as defined on Linux
typedef int clockid_t;

// definitions taken from Linux headers (<bits/time.h>)

/* Identifier for system-wide realtime clock.  */
#define CLOCK_REALTIME        0
/* Monotonic system-wide clock.  */
#define CLOCK_MONOTONIC       1

int clock_gettime(clockid_t clk_id, struct timespec *tp);

#endif    // defined(__MACH__)
#endif    // !defined(__MAC_COMPAT_H)
