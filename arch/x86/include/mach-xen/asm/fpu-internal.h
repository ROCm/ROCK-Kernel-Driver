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
	fpu.preload = tsk_used_math(new) &&
		      (use_eager_fpu() || new->thread.fpu_counter > 5);

	if (__thread_has_fpu(old)) {
		if (!__save_init_fpu(old))
			task_disable_lazy_fpu_restore(old);
		else
			old->thread.fpu.last_cpu = cpu;

		/* But leave fpu_owner_task! */
		old->thread.fpu.has_fpu = 0;

		/* Don't change CR0.TS if we just switch! */
		if (fpu.preload) {
			new->thread.fpu_counter++;
			__thread_set_has_fpu(new);
			prefetch(new->thread.fpu.state);
		} else if (!use_eager_fpu()) {
			(*mcl)->op = __HYPERVISOR_fpu_taskswitch;
			(*mcl)++->args[0] = 1;
		}
	} else {
		old->thread.fpu_counter = 0;
		task_disable_lazy_fpu_restore(old);
		if (fpu.preload) {
			new->thread.fpu_counter++;
			if (fpu_lazy_restore(new, cpu))
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
