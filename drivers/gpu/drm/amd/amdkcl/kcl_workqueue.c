/* SPDX-License-Identifier: MIT */
#include <linux/version.h>
#include <kcl/kcl_workqueue.h>

#ifndef HAVE_SYSTEM_HIGHPRI_WQ_EXPORTED
struct workqueue_struct *system_highpri_wq;
EXPORT_SYMBOL(system_highpri_wq);
#endif
/**
 * workqueue_init_early - early init for workqueue subsystem
 *
 * This is the first half of two-staged workqueue subsystem initialization
 * and invoked as soon as the bare basics - memory allocation, cpumasks and
 * idr are up.  It sets up all the data structures and system workqueues
 * and allows early boot code to create workqueues and queue/cancel work
 * items.  Actual work item execution starts only after kthreads can be
 * created and scheduled right before early initcalls.
 * history:
 * v2.6.27-6518-g0d557dc97f4b workqueue: introduce create_rt_workqueue
 * v2.6.35-rc3-10-gc790bce04818 workqueue: kill RT workqueue
 * v2.6.35-rc3-36-g649027d73a63 workqueue: implement high priority workqueue
 * v2.6.35-rc3-34-gd320c03830b1 workqueue: s/__create_workqueue()/alloc_workqueue()/, and add system workqueues
 * v2.6.36-rc4-167-g81dcaf6516d8 workqueue: implement alloc_ordered_workqueue()
 */
int amdkcl_workqueue_init_early(void)
{
#ifndef HAVE_SYSTEM_HIGHPRI_WQ_EXPORTED
	system_highpri_wq = alloc_workqueue("events_highpri", WQ_HIGHPRI, 0);
#endif
	BUG_ON(!system_highpri_wq);
	return 0;
}
