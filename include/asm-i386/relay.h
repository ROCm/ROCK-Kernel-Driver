#ifndef _ASM_I386_RELAY_H
#define _ASM_I386_RELAY_H
/*
 * linux/include/asm-i386/relay.h
 *
 * Copyright (C) 2002, 2003 - Tom Zanussi (zanussi@us.ibm.com), IBM Corp
 * Copyright (C) 2002 - Karim Yaghmour (karim@opersys.com)
 *
 * i386 definitions for relayfs
 */

#include <linux/relayfs_fs.h>

#ifdef CONFIG_X86_TSC
#include <asm/msr.h>

/**
 *	get_time_delta - utility function for getting time delta
 *	@now: pointer to a timeval struct that may be given current time
 *	@rchan: the channel
 *
 *	Returns either the TSC if TSCs are being used, or the time and the
 *	time difference between the current time and the buffer start time 
 *	if TSCs are not being used.
 */
static inline u32
get_time_delta(struct timeval *now, struct rchan *rchan)
{
	u32 time_delta;

	if ((using_tsc(rchan) == 1) && cpu_has_tsc)
		rdtscl(time_delta);
	else {
		do_gettimeofday(now);
		time_delta = calc_time_delta(now, &rchan->buf_start_time);
	}

	return time_delta;
}

/**
 *	get_timestamp - utility function for getting a time and TSC pair
 *	@now: current time
 *	@tsc: the TSC associated with now
 *	@rchan: the channel
 *
 *	Sets the value pointed to by now to the current time and the value
 *	pointed to by tsc to the tsc associated with that time, if the 
 *	platform supports TSC.
 */
static inline void 
get_timestamp(struct timeval *now,
	      u32 *tsc,
	      struct rchan *rchan)
{
	do_gettimeofday(now);

	if ((using_tsc(rchan) == 1) && cpu_has_tsc)
		rdtscl(*tsc);
}

/**
 *	get_time_or_tsc - utility function for getting a time or a TSC
 *	@now: current time
 *	@tsc: current TSC
 *	@rchan: the channel
 *
 *	Sets the value pointed to by now to the current time or the value
 *	pointed to by tsc to the current tsc, depending on whether we're
 *	using TSCs or not.
 */
static inline void 
get_time_or_tsc(struct timeval *now,
		u32 *tsc,
		struct rchan *rchan)
{
	if ((using_tsc(rchan) == 1) && cpu_has_tsc)
		rdtscl(*tsc);
	else
		do_gettimeofday(now);
}

/**
 *	have_tsc - does this platform have a useable TSC?
 *
 *	Returns 1 if this platform has a useable TSC counter for
 *	timestamping purposes, 0 otherwise.
 */
static inline int
have_tsc(void)
{
	if (cpu_has_tsc)
		return 1;
	else
		return 0;
}

#else /* No TSC support (#ifdef CONFIG_X86_TSC) */
#include <asm-generic/relay.h>
#endif /* #ifdef CONFIG_X86_TSC */
#endif
