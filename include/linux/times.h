#ifndef _LINUX_TIMES_H
#define _LINUX_TIMES_H

#ifdef __KERNEL__
#include <asm/div64.h>
#include <asm/types.h>

#if (HZ % USER_HZ)==0
# define jiffies_to_clock_t(x) ((x) / (HZ / USER_HZ))
#else
# define jiffies_to_clock_t(x) ((clock_t) jiffies_64_to_clock_t((u64) x))
#endif

static inline u64 jiffies_64_to_clock_t(u64 x)
{
#if (HZ % USER_HZ)==0
	do_div(x, HZ / USER_HZ);
#else
	/*
	 * There are better ways that don't overflow early,
	 * but even this doesn't overflow in hundreds of years
	 * in 64 bits, so..
	 */
	x *= USER_HZ;
	do_div(x, HZ);
#endif
	return x;
}
#endif

struct tms {
	clock_t tms_utime;
	clock_t tms_stime;
	clock_t tms_cutime;
	clock_t tms_cstime;
};

#endif
