
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

#endif
