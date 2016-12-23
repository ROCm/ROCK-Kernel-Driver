#ifndef AMDKCL_KTHREAD_H
#define AMDKCL_KTHREAD_H

#include <linux/sched.h>
#include <linux/kthread.h>

extern void (*_kcl_kthread_parkme)(void);
extern void (*_kcl_kthread_unpark)(struct task_struct *k);
extern int (*_kcl_kthread_park)(struct task_struct *k);
extern bool (*_kcl_kthread_should_park)(void);

static inline void kcl_kthread_parkme(void)
{
#ifdef BUILD_AS_DKMS
	return _kcl_kthread_parkme();
#else
	return kthread_parkme();
#endif
}

static inline void kcl_kthread_unpark(struct task_struct *k)
{
#ifdef BUILD_AS_DKMS
	return _kcl_kthread_unpark(k);
#else
	return kthread_unpark(k);
#endif
}

static inline int kcl_kthread_park(struct task_struct *k)
{
#ifdef BUILD_AS_DKMS
	return _kcl_kthread_park(k);
#else
	return kthread_park(k);
#endif
}

static inline bool kcl_kthread_should_park(void)
{
#ifdef BUILD_AS_DKMS
	return _kcl_kthread_should_park();
#else
	return kthread_should_park();
#endif
}

#endif /* AMDKCL_KTHREAD_H */
