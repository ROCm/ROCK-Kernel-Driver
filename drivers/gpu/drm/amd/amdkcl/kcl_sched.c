/* SPDX-License-Identifier: MIT */
#include <kcl/kcl_sched.h>

#ifndef HAVE_SCHED_SET_FIFO_LOW
int (*_kcl_sched_setscheduler_nocheck)(struct task_struct *p, int policy,
					       const struct sched_param *param);
void sched_set_fifo_low(struct task_struct *p)
{
	struct sched_param sp = { .sched_priority = 1 };
	WARN_ON_ONCE(_kcl_sched_setscheduler_nocheck(p, SCHED_FIFO, &sp) != 0);
}
EXPORT_SYMBOL_GPL(sched_set_fifo_low);
#endif

void amdkcl_sched_init(void)
{
#ifndef HAVE_SCHED_SET_FIFO_LOW
	_kcl_sched_setscheduler_nocheck = amdkcl_fp_setup("sched_setscheduler_nocheck",
							  NULL);
#endif
}
