#ifndef __LINUX_PREEMPT_H
#define __LINUX_PREEMPT_H

#include <linux/config.h>

#define preempt_count() (current_thread_info()->preempt_count)

#ifdef CONFIG_PREEMPT

extern void preempt_schedule(void);

#define preempt_disable() \
do { \
	preempt_count()++; \
	barrier(); \
} while (0)

#define preempt_enable_no_resched() \
do { \
	preempt_count()--; \
	barrier(); \
} while (0)

#define preempt_enable() \
do { \
	preempt_enable_no_resched(); \
	if (unlikely(test_thread_flag(TIF_NEED_RESCHED))) \
		preempt_schedule(); \
} while (0)

#define preempt_check_resched() \
do { \
	if (unlikely(test_thread_flag(TIF_NEED_RESCHED))) \
		preempt_schedule(); \
} while (0)

#else

#define preempt_disable()		do { } while (0)
#define preempt_enable_no_resched()	do {} while(0)
#define preempt_enable()		do { } while (0)
#define preempt_check_resched()		do { } while (0)

#endif

#endif /* __LINUX_PREEMPT_H */
