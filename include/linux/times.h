#ifndef _LINUX_TIMES_H
#define _LINUX_TIMES_H

#ifdef __KERNEL__
#include <asm/div64.h>
#include <asm/types.h>
#include <asm/param.h>

#if (HZ % USER_HZ)==0
# define jiffies_to_clock_t(x) ((x) / (HZ / USER_HZ))
#else
# define jiffies_to_clock_t(x) ((clock_t) jiffies_64_to_clock_t((u64) x))
#endif

static inline unsigned long clock_t_to_jiffies(unsigned long x)
{
#if (HZ % USER_HZ)==0
	if (x >= ~0UL / (HZ / USER_HZ))
		return ~0UL;
	return x * (HZ / USER_HZ);
#else
	u64 jif;

	/* Don't worry about loss of precision here .. */
	if (x >= ~0UL / HZ * USER_HZ)
		return ~0UL;

	/* .. but do try to contain it here */
	jif = x * (u64) HZ;
	do_div(jif, USER_HZ);
	return jif;
#endif
}

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
