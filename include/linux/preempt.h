#ifndef __LINUX_PREEMPT_H
#define __LINUX_PREEMPT_H

#include <linux/config.h>

#define preempt_count() (current_thread_info()->preempt_count)

#define inc_preempt_count() \
do { \
	preempt_count()++; \
} while (0)

#define dec_preempt_count() \
do { \
	preempt_count()--; \
} while (0)

#ifdef CONFIG_PREEMPT

extern void preempt_schedule(void);

#define preempt_disable() \
do { \
	inc_preempt_count(); \
	barrier(); \
} while (0)

#define preempt_enable_no_resched() \
do { \
	dec_preempt_count(); \
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

#define inc_preempt_count_non_preempt()	do { } while (0)
#define dec_preempt_count_non_preempt()	do { } while (0)

#else

#define preempt_disable()		do { } while (0)
#define preempt_enable_no_resched()	do {} while(0)
#define preempt_enable()		do { } while (0)
#define preempt_check_resched()		do { } while (0)

/*
 * Sometimes we want to increment the preempt count, but we know that it's
 * already incremented if the kernel is compiled for preemptibility.
 */
#define inc_preempt_count_non_preempt()	inc_preempt_count()
#define dec_preempt_count_non_preempt()	dec_preempt_count()

#endif

#endif /* __LINUX_PREEMPT_H */
