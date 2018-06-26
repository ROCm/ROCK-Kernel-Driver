#ifndef AMDKCL_PREEMPT_H
#define AMDKCL_PREEMPT_H

#include <linux/preempt.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
#define in_task()		(!(preempt_count() & \
				(NMI_MASK | HARDIRQ_MASK | SOFTIRQ_OFFSET)))

#endif

#endif /* AMDKCL_PREEMPT_H*/
