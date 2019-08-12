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
 */
int amdkcl_workqueue_init_early(void)
{
#ifndef HAVE_SYSTEM_HIGHPRI_WQ_EXPORTED
#ifdef HAVE_WQ_HIGHPRI
	system_highpri_wq = alloc_workqueue("events_highpri", WQ_HIGHPRI, 0);
#else
	system_highpri_wq = create_workqueue("events_highpri");
#endif
#endif
	BUG_ON(!system_highpri_wq);
	return 0;
}
