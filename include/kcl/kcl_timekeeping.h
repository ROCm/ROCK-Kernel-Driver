#ifndef _KCL_LINUX_TIMEKEEPING_H
#define _KCL_LINUX_TIMEKEEPING_H

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)) && !defined(OS_NAME_RHEL_7_X)
static inline u64 ktime_get_ns(void)
{
	return ktime_to_ns(ktime_get());
}
#endif
#endif
