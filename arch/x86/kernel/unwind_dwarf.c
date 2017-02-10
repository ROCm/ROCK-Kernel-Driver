#include <linux/unwind.h>

#if 0
#define dprintk(fmt, args...) printk(KERN_DEBUG "unwind: " fmt, ##args)
#else
#define dprintk(fmt, args...) no_printk(KERN_DEBUG "unwind: " fmt, ##args)
#endif

unsigned long unwind_get_return_address(struct unwind_state *state)
{
	unsigned long *addr = unwind_get_return_address_ptr(state);

	if (unwind_done(state))
		return 0;

	return ftrace_graph_ret_addr(state->task, &state->graph_idx,
			*addr, addr);
}
EXPORT_SYMBOL_GPL(unwind_get_return_address);

bool unwind_next_frame(struct unwind_state *state)
{
	if (unwind_done(state))
		goto bad;

	if (arch_dwarf_user_mode(state))
		goto bad;

	if ((state->dw_sp & PAGE_MASK) == (UNW_SP(state) & PAGE_MASK) &&
			state->dw_sp > UNW_SP(state))
		goto bad;

	if (dwarf_unwind(state) || !UNW_PC(state))
		goto bad;

	state->dw_sp = UNW_SP(state);

	return true;
bad:
	state->stack_info.type = STACK_TYPE_UNKNOWN;
	return false;
}
EXPORT_SYMBOL_GPL(unwind_next_frame);

void __unwind_start(struct unwind_state *state, struct task_struct *task,
		struct pt_regs *regs, unsigned long *first_frame)
{
	bool do_skipping = true;
	char type;

	memset(state, 0, sizeof(*state));
	state->task = task;

	if (regs) {
		arch_dwarf_init_frame_info(state, regs);
		type = 'R';
	} else if (task == current) {
		arch_dwarf_init_running(&state->u.regs);
		type = 'C';
#ifdef CONFIG_SMP
	} else if (task->on_cpu) {
		state->on_cpu = 1;
		return;
#endif
	} else {
		arch_dwarf_init_blocked(state);
		type = 'B';
		do_skipping = false;
	}

	dprintk("%s: %c FF=%p rip=%lx (%pS) rsp=%lx rbp=%lx\n",
			__func__, type, first_frame, UNW_PC(state),
			(void *)UNW_PC(state), UNW_SP(state), UNW_FP(state));

	get_stack_info((void *)UNW_SP(state), task, &state->stack_info,
			&state->stack_mask);

	state->dw_sp = UNW_SP(state);

	if (arch_dwarf_user_mode(state))
		return;

	while (do_skipping) {
		if (UNW_SP(state) > (unsigned long)first_frame) {
			dprintk("%s: hit first=%p sp=%lx\n", __func__,
					first_frame, UNW_SP(state));
			break;
		}
		if (!unwind_next_frame(state))
			break;
		dprintk("%s: skipped to %pS rsp=%lx rbp=%lx\n", __func__,
				(void *)UNW_PC(state), UNW_SP(state),
				UNW_FP(state));
	}
}
EXPORT_SYMBOL_GPL(__unwind_start);
