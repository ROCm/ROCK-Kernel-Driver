#ifndef _linux_POSIX_TIMERS_H
#define _linux_POSIX_TIMERS_H

struct k_clock {
	int res;		/* in nano seconds */
	int (*clock_set) (struct timespec * tp);
	int (*clock_get) (struct timespec * tp);
	int (*nsleep) (int flags,
		       struct timespec * new_setting,
		       struct itimerspec * old_setting);
	int (*timer_set) (struct k_itimer * timr, int flags,
			  struct itimerspec * new_setting,
			  struct itimerspec * old_setting);
	int (*timer_del) (struct k_itimer * timr);
	void (*timer_get) (struct k_itimer * timr,
			   struct itimerspec * cur_setting);
};
struct now_struct {
	unsigned long jiffies;
};

#define posix_get_now(now) (now)->jiffies = jiffies;
#define posix_time_before(timer, now) \
                      time_before((timer)->expires, (now)->jiffies)

#define posix_bump_timer(timr) do { \
                        (timr)->it_timer.expires += (timr)->it_incr; \
                        (timr)->it_overrun++;               \
                       }while (0)
#endif
