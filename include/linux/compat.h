#ifndef _LINUX_COMPAT_H
#define _LINUX_COMPAT_H
/*
 * These are the type definitions for the architecture specific
 * syscall compatibility layer.
 */
#include <linux/config.h>

#ifdef CONFIG_COMPAT

#include <linux/stat.h>
#include <asm/compat.h>

#define compat_jiffies_to_clock_t(x)	((x) / (HZ / COMPAT_USER_HZ))

struct compat_utimbuf {
	compat_time_t		actime;
	compat_time_t		modtime;
};

struct compat_itimerval {
	struct compat_timeval	it_interval;
	struct compat_timeval	it_value;
};

struct compat_tms {
	compat_clock_t		tms_utime;
	compat_clock_t		tms_stime;
	compat_clock_t		tms_cutime;
	compat_clock_t		tms_cstime;
};

extern int cp_compat_stat(struct kstat *, struct compat_stat *);

#endif /* CONFIG_COMPAT */
#endif /* _LINUX_COMPAT_H */
