#ifndef _TRACE_TIMER_H
#define _TRACE_TIMER_H

#include <linux/tracepoint.h>

DEFINE_TRACE(timer_itimer_expired,
	TPPROTO(struct signal_struct *sig),
	TPARGS(sig));
DEFINE_TRACE(timer_itimer_set,
	TPPROTO(int which, struct itimerval *value),
	TPARGS(which, value));
DEFINE_TRACE(timer_set,
	TPPROTO(struct timer_list *timer),
	TPARGS(timer));
/*
 * xtime_lock is taken when kernel_timer_update_time tracepoint is reached.
 */
DEFINE_TRACE(timer_update_time,
	TPPROTO(struct timespec *_xtime, struct timespec *_wall_to_monotonic),
	TPARGS(_xtime, _wall_to_monotonic));
DEFINE_TRACE(timer_timeout,
	TPPROTO(struct task_struct *p),
	TPARGS(p));
#endif
