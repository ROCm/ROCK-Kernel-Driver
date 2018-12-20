/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KCL_LINUX_TIMEKEEPING_H
#define _KCL_LINUX_TIMEKEEPING_H
#include <linux/ktime.h>

#ifndef HAVE_KTIME_GET_NS
static inline u64 ktime_get_ns(void)
{
	return ktime_to_ns(ktime_get());
}
#endif

#if !defined(HAVE_KTIME_GET_BOOTTIME_NS)
#if defined(HAVE_KTIME_GET_NS)
static inline u64 ktime_get_boottime_ns(void)
{
	return ktime_get_boot_ns();
}
#else
static inline u64 ktime_get_boottime_ns(void)
{
	struct timespec time;

	get_monotonic_boottime(&time);
	return (u64)timespec_to_ns(&time);
}
#endif /* HAVE_KTIME_GET_NS */
#endif /* HAVE_KTIME_GET_BOOTTIME_NS */

#endif
