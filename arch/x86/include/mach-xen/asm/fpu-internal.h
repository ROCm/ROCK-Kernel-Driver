#ifndef _FPU_INTERNAL_H
#include <asm/i387.h>
#define switch_fpu_prepare native_switch_fpu_prepare
#include_next <asm/fpu-internal.h>
#undef switch_fpu_prepare

static inline bool xen_thread_fpu_begin(struct task_struct *tsk,
					multicall_entry_t *mcl)
{
	bool ret = false;

	if (mcl && !use_eager_fpu()) {
		mcl->op = __HYPERVISOR_fpu_taskswitch;
		mcl->args[0] = 0;
		ret = true;
	}
	__thread_set_has_fpu(tsk);

	return ret;
}

static inline fpu_switch_t xen_switch_fpu_prepare(struct task_struct *old,
						  struct task_struct *new,
						  int cpu,
						  multicall_entry_t **mcl)
{
	fpu_switch_t fpu;

	/*
	 * If the task has used the math, pre-load the FPU on xsave processors
	 * or if the past 5 consecutive context-switches used math.
	 */
	fpu.preload = tsk_used_math(new) && (use_eager_fpu() ||
					     new->fpu_counter > 5);
	if (__thread_has_fpu(old)) {
		if (!__save_init_fpu(old))
			cpu = ~0;
		old->thread.fpu.last_cpu = cpu;
		old->thread.fpu.has_fpu = 0;	/* But leave fpu_owner_task! */

		/* Don't change CR0.TS if we just switch! */
		if (fpu.preload) {
			new->fpu_counter++;
			__thread_set_has_fpu(new);
			prefetch(new->thread.fpu.state);
		} else if (!use_eager_fpu()) {
			(*mcl)->op = __HYPERVISOR_fpu_taskswitch;
			(*mcl)++->args[0] = 1;
		}
	} else {
		old->fpu_counter = 0;
		old->thread.fpu.last_cpu = ~0;
		if (fpu.preload) {
			new->fpu_counter++;
			if (!use_eager_fpu() && fpu_lazy_restore(new, cpu))
				fpu.preload = 0;
			else
				prefetch(new->thread.fpu.state);
			if (xen_thread_fpu_begin(new, *mcl))
				++*mcl;
		}
	}
	return fpu;
}

#endif
