#ifndef _LINUX_TIMES_H
#define _LINUX_TIMES_H

#ifdef __KERNEL__
# define jiffies_to_clock_t(x) ((x) / (HZ / USER_HZ))
#endif

struct tms {
	clock_t tms_utime;
	clock_t tms_stime;
	clock_t tms_cutime;
	clock_t tms_cstime;
};

#endif
