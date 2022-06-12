/* SPDX-License-Identifier: GPL-2.0 */

#ifndef KCL_LINUX_WORKQUEUE_BACKPORT_H
#define KCL_LINUX_WORKQUEUE_BACKPORT_H

#include <linux/workqueue.h>

#ifndef HAVE_CANCEL_WORK
extern bool (*_kcl_cancel_work)(struct work_struct *work);
#define cancel_work _kcl_cancel_work
#endif

#endif /* KCL_LINUX_WORKQUEUE_BACKPORT_H */
