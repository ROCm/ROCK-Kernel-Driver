/* SPDX-License-Identifier: GPL-2.0 */

#ifndef KCL_LINUX_WORKQUEUE_BACKPORT_H
#define KCL_LINUX_WORKQUEUE_BACKPORT_H

#include <linux/workqueue.h>

#ifndef HAVE_CANCEL_WORK
extern bool kcl_cancel_work(struct work_struct *work);
#define cancel_work kcl_cancel_work
#endif

/* Copied from kernel/workqueue.c and modified for KCL */
#ifndef HAVE_QUEUE_WORK_NODE
static inline
bool _kcl_queue_work_node(int node, struct workqueue_struct *wq,
                     struct work_struct *work)
{
        return queue_work(wq, work);
}
#define queue_work_node _kcl_queue_work_node
#endif
#endif /* KCL_LINUX_WORKQUEUE_BACKPORT_H */
