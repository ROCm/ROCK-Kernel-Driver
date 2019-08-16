#ifndef _KCL_LINUX_TIMEKEEPING_H
#define _KCL_LINUX_TIMEKEEPING_H

#ifndef HAVE_KTIME_GET_NS
static inline u64 ktime_get_ns(void)
{
	return ktime_to_ns(ktime_get());
}
#endif
#endif
