
#ifndef KCL_LINUX_WORKQUEUE_H
#define KCL_LINUX_WORKQUEUE_H

#include <linux/workqueue.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0)
/*
 * System-wide workqueues which are always present.
*
 * system_highpri_wq is similar to system_wq but for work items which
 * require WQ_HIGHPRI.
 *
*/
extern struct workqueue_struct *system_highpri_wq;
#endif

#ifndef INIT_WORK_ONSTACK
#ifdef __INIT_WORK
#define INIT_WORK_ONSTACK(_work, _func) __INIT_WORK((_work), (_func), 1)
#else
#define INIT_WORK_ONSTACK(_work, _func) INIT_WORK((_work), (_func))
#endif
#endif

#if !defined(HAVE_DESTROY_WORK_ON_STACK)
static inline void destroy_work_on_stack(struct work_struct *work) { }
#endif

#endif
