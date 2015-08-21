#ifndef _ASM_X86_FPU_INTERNAL_H
#define switch_fpu_prepare native_switch_fpu_prepare
#include_next <asm/fpu/internal.h>
#undef switch_fpu_prepare

static inline void xen_fpregs_activate_hw(multicall_entry_t **mcl)
{
	if (!use_eager_fpu()) {
		(*mcl)->op = __HYPERVISOR_fpu_taskswitch;
		(*mcl)++->args[0] = 0;
	}
}

static inline void xen_fpregs_deactivate_hw(multicall_entry_t **mcl)
{
	if (!use_eager_fpu()) {
		(*mcl)->op = __HYPERVISOR_fpu_taskswitch;
		(*mcl)++->args[0] = 1;
	}
}

static inline void xen_fpregs_activate(struct fpu *fpu,
				       multicall_entry_t **mcl)
{
	xen_fpregs_activate_hw(mcl);
	__fpregs_activate(fpu);
}

static inline fpu_switch_t xen_switch_fpu_prepare(struct fpu *old_fpu,
						  struct fpu *new_fpu,
						  int cpu,
						  multicall_entry_t **mcl)
{
	fpu_switch_t fpu;

	/*
	 * If the task has used the math, pre-load the FPU on xsave processors
	 * or if the past 5 consecutive context-switches used math.
	 */
	fpu.preload = new_fpu->fpstate_active &&
		      (use_eager_fpu() || new_fpu->counter > 5);

	if (old_fpu->fpregs_active) {
		if (!copy_fpregs_to_fpstate(old_fpu))
			old_fpu->last_cpu = -1;
		else
			old_fpu->last_cpu = cpu;

		/* But leave fpu_fpregs_owner_ctx! */
		old_fpu->fpregs_active = 0;

		/* Don't change CR0.TS if we just switch! */
		if (fpu.preload) {
			new_fpu->counter++;
			__fpregs_activate(new_fpu);
			prefetch(&new_fpu->state);
		} else {
			xen_fpregs_deactivate_hw(mcl);
		}
	} else {
		old_fpu->counter = 0;
		old_fpu->last_cpu = -1;
		if (fpu.preload) {
			new_fpu->counter++;
			if (fpu_want_lazy_restore(new_fpu, cpu))
				fpu.preload = 0;
			else
				prefetch(&new_fpu->state);
			xen_fpregs_activate(new_fpu, mcl);
		}
	}
	return fpu;
}

#endif
