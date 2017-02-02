#include <linux/unwind.h>

#if 0
#ifdef CONFIG_DWARF_UNWIND
static int call_trace = 1;
#else
#define call_trace (-1)
#endif
#endif

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

#if 0
static int dump_trace_unwind(struct unwind_state *state)
{
	unsigned long sp = UNW_SP(state);

	pr_info("%s: tady rip=%lx rsp=%lx rbp=%lx\n", __func__,
			UNW_PC(state), UNW_SP(state), UNW_FP(state));

	if (arch_dwarf_user_mode(state))
		return -1;
	while (unwind(state) == 0 && UNW_PC(state)) {
		pr_info("%s: %pS\n", __func__, (void *)UNW_PC(state));
//		ops->address(data, UNW_PC(state), 1);
		if (arch_dwarf_user_mode(state))
			break;
		if ((sp & PAGE_MASK) == (UNW_SP(state) & PAGE_MASK) &&
				sp > UNW_SP(state))
			break;
		sp = UNW_SP(state);
	}
	return 1;
}

int try_stack_unwind(struct task_struct *task, struct pt_regs *regs,
		     unsigned long **stack, unsigned long *bp)
{
#ifdef CONFIG_DWARF_UNWIND
	int unw_ret = 0;
	struct unwind_state state;

	if (call_trace < 0)
		return 0;

	__unwind_start(&state, task, regs, get_stack_pointer(task, regs));
	if (state.on_cpu)
		return 0;
	unw_ret = dump_trace_unwind(&state);

	if (unw_ret > 0) {
		if (call_trace == 1 && !arch_dwarf_user_mode(&state)) {
//			ops->warning_symbol(data, "DWARF2 unwinder stuck at %s\n",
//					    UNW_PC(&state));
			pr_info("%s: DWARF2 unwinder stuck at %lx %pS\n",
					__func__, UNW_PC(&state),
					(void *)UNW_PC(&state));
			if (UNW_SP(&state) >= PAGE_OFFSET) {
//				ops->warning(data, "Leftover inexact backtrace:\n");
				pr_info("%s: Leftover inexact backtrace:\n",
						__func__);
				*stack = (void *)UNW_SP(&state);
				*bp = UNW_FP(&state);
				return 0;
			}
		} else if (call_trace >= 1)
			return -1;
//		ops->warning(data, "Full inexact backtrace again:\n");
		pr_info("%s: Full inexact backtrace again:\n", __func__);
	} else
		pr_info("%s: Inexact backtrace:\n", __func__);
//		ops->warning(data, "Inexact backtrace:\n");
#endif
	return 0;
}

#ifdef CONFIG_DWARF_UNWIND
static int __init call_trace_setup(char *s)
{
	if (!s)
		return -EINVAL;
	if (strcmp(s, "old") == 0)
		call_trace = -1;
	else if (strcmp(s, "both") == 0)
		call_trace = 0;
	else if (strcmp(s, "newfallback") == 0)
		call_trace = 1;
	else if (strcmp(s, "new") == 0)
		call_trace = 2;
	return 0;
}
early_param("call_trace", call_trace_setup);
#endif

#endif
