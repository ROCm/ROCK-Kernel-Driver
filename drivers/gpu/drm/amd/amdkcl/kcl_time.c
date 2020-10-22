/* SPDX-License-Identifier: MIT */
#include <linux/ktime.h>
#include <kcl/kcl_timekeeping.h>

#ifndef HAVE_JIFFIES64_TO_MSECS
u64 jiffies64_to_msecs(const u64 j)
{
#if HZ <= MSEC_PER_SEC && !(MSEC_PER_SEC % HZ)
	return (MSEC_PER_SEC / HZ) * j;
#else
	return div_u64(j * HZ_TO_MSEC_NUM, HZ_TO_MSEC_DEN);
#endif
}
EXPORT_SYMBOL(jiffies64_to_msecs);
#endif
