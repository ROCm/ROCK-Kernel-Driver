/* SPDX-License-Identifier: GPL-2.0 */

#ifndef KCL_LINUX_WORKQUEUE_BACKPORT_H
#define KCL_LINUX_WORKQUEUE_BACKPORT_H

#include <linux/workqueue.h>

#ifndef HAVE_CANCEL_WORK
extern bool kcl_cancel_work(struct work_struct *work);
#define cancel_work kcl_cancel_work
#endif

#endif /* KCL_LINUX_WORKQUEUE_BACKPORT_H */
