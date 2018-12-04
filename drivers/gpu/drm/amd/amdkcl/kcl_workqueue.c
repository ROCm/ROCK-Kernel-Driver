#include <linux/version.h>
#include <kcl/kcl_workqueue.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)
struct workqueue_struct *system_highpri_wq;
EXPORT_SYMBOL(system_highpri_wq);
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
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
	system_highpri_wq = __create_workqueue("events_highpri", 0, 0, 1);
#else
	system_highpri_wq = alloc_workqueue("events_highpri", WQ_HIGHPRI, 0);
#endif
	BUG_ON(!system_highpri_wq);
	return 0;
}
#endif
