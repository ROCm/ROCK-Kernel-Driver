#ifndef _ASM_GENERIC_RELAY_H
#define _ASM_GENERIC_RELAY_H
/*
 * linux/include/asm-generic/relay.h
 *
 * Copyright (C) 2002, 2003 - Tom Zanussi (zanussi@us.ibm.com), IBM Corp
 * Copyright (C) 2002 - Karim Yaghmour (karim@opersys.com)
 *
 * Architecture-independent definitions for relayfs
 */

#include <linux/relayfs_fs.h>

/**
 *	get_time_delta - utility function for getting time delta
 *	@now: pointer to a timeval struct that may be given current time
 *	@rchan: the channel
 *
 *	Returns the time difference between the current time and the buffer
 *	start time.
 */
static inline u32
get_time_delta(struct timeval *now, struct rchan *rchan)
{
	u32 time_delta;

	do_gettimeofday(now);
	time_delta = calc_time_delta(now, &rchan->buf_start_time);

	return time_delta;
}

/**
 *	get_timestamp - utility function for getting a time and TSC pair
 *	@now: current time
 *	@tsc: the TSC associated with now
 *	@rchan: the channel
 *
 *	Sets the value pointed to by now to the current time. Value pointed to
 *	by tsc is not set since there is no generic TSC support.
 */
static inline void 
get_timestamp(struct timeval *now, 
	      u32 *tsc,
	      struct rchan *rchan)
{
	do_gettimeofday(now);
}

/**
 *	get_time_or_tsc: - Utility function for getting a time or a TSC.
 *	@now: current time
 *	@tsc: current TSC
 *	@rchan: the channel
 *
 *	Sets the value pointed to by now to the current time.
 */
static inline void 
get_time_or_tsc(struct timeval *now, 
		u32 *tsc,
		struct rchan *rchan)
{
	do_gettimeofday(now);
}

/**
 *	have_tsc - does this platform have a useable TSC?
 *
 *	Returns 0.
 */
static inline int 
have_tsc(void)
{
	return 0;
}
#endif
