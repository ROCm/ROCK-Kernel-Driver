#ifndef _LINUX_COMPAT_H
#define _LINUX_COMPAT_H
/*
 * These are the type definitions for the arhitecure sepcific
 * compatibility layer.
 */
#include <linux/config.h>

#ifdef CONFIG_COMPAT

#include <asm/compat.h>

struct compat_utimbuf {
	compat_time_t		actime;
	compat_time_t		modtime;
};

struct compat_itimerval {
	struct compat_timeval	it_interval;
	struct compat_timeval	it_value;
};

#endif /* CONFIG_COMPAT */
#endif /* _LINUX_COMPAT_H */
