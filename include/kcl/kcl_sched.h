/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KCL_KCL_SCHED_H
#define _KCL_KCL_SCHED_H

#include <linux/sched.h>
#include <kcl/header/kcl_uapi_linux_sched_types_h.h>

#ifndef HAVE_SCHED_SET_FIFO_LOW
void sched_set_fifo_low(struct task_struct *p);
#endif

#endif
