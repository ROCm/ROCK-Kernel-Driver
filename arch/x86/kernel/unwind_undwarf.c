#include <linux/module.h>
#include <linux/sort.h>
#include <asm/ptrace.h>
#include <asm/stacktrace.h>
#include <asm/unwind.h>
#include <asm/undwarf.h>

#define undwarf_warn(fmt, ...) \
	printk_deferred_once(KERN_WARNING pr_fmt("WARNING: " fmt), ##__VA_ARGS__)

extern struct undwarf __undwarf_start[];
extern struct undwarf __undwarf_end[];

unsigned long unwind_get_return_address(struct unwind_state *state)
{
	if (unwind_done(state))
		return 0;

	return __kernel_text_address(state->ip) ? state->ip : 0;
}
EXPORT_SYMBOL_GPL(unwind_get_return_address);

unsigned long *unwind_get_return_address_ptr(struct unwind_state *state)
{
	if (unwind_done(state))
		return NULL;

	if (state->regs)
		return &state->regs->ip;

	if (state->sp)
		return (unsigned long *)state->sp - 1;

	return NULL;
}

static inline unsigned long undwarf_ip(struct undwarf *undwarf)
{
	return (unsigned long)&undwarf->ip + undwarf->ip;
}

static struct undwarf *__undwarf_lookup(struct undwarf *undwarf,
					unsigned int num, unsigned long ip)
{
	struct undwarf *first = undwarf;
	struct undwarf *last = undwarf + num - 1;
	struct undwarf *mid;
	unsigned long u_ip;

	while (first <= last) {
		mid = first + ((last - first) / 2);
		u_ip = undwarf_ip(mid);

		if (ip >= u_ip) {
			if (ip < u_ip + mid->len)
				return mid;
			first = mid + 1;
		} else
			last = mid - 1;
	}

	return NULL;
}

static struct undwarf *undwarf_lookup(unsigned long ip)
{
	struct undwarf *undwarf;
	struct module *mod;

	/* Look in vmlinux undwarf section: */
	undwarf = __undwarf_lookup(__undwarf_start, __undwarf_end - __undwarf_start, ip);
	if (undwarf)
		return undwarf;

	/* Look in module undwarf sections: */
	preempt_disable();
	mod = __module_address(ip);
	if (!mod || !mod->arch.undwarf)
		goto module_out;
	undwarf = __undwarf_lookup(mod->arch.undwarf, mod->arch.num_undwarves, ip);

module_out:
	preempt_enable();
	return undwarf;
}

static bool stack_access_ok(struct unwind_state *state, unsigned long addr,
			    size_t len)
{
	struct stack_info *info = &state->stack_info;

	/*
	 * If the next bp isn't on the current stack, switch to the next one.
	 *
	 * We may have to traverse multiple stacks to deal with the possibility
	 * that info->next_sp could point to an empty stack and the next bp
	 * could be on a subsequent stack.
	 */
	while (!on_stack(info, (void *)addr, len))
		if (get_stack_info(info->next_sp, state->task, info,
				   &state->stack_mask))
			return false;

	return true;
}

static bool deref_stack_reg(struct unwind_state *state, unsigned long addr,
			    unsigned long *val)
{
	if (!stack_access_ok(state, addr, sizeof(long)))
		return false;

	*val = READ_ONCE_TASK_STACK(state->task, *(unsigned long *)addr);
	return true;
}

#define REGS_SIZE (sizeof(struct pt_regs))
#define SP_OFFSET (offsetof(struct pt_regs, sp))
#define IRET_REGS_SIZE (REGS_SIZE - offsetof(struct pt_regs, ip))
#define IRET_SP_OFFSET (SP_OFFSET - offsetof(struct pt_regs, ip))

static bool deref_stack_regs(struct unwind_state *state, unsigned long addr,
			     unsigned long *ip, unsigned long *sp, bool full)
{
	size_t regs_size = full ? REGS_SIZE : IRET_REGS_SIZE;
	size_t sp_offset = full ? SP_OFFSET : IRET_SP_OFFSET;
	struct pt_regs *regs = (struct pt_regs *)(addr + regs_size - REGS_SIZE);

	if (IS_ENABLED(CONFIG_X86_64)) {
		if (!stack_access_ok(state, addr, regs_size))
			return false;

		*ip = regs->ip;
		*sp = regs->sp;

		return true;
	}

	if (!stack_access_ok(state, addr, sp_offset))
		return false;

	*ip = regs->ip;

	if (user_mode(regs)) {
		if (!stack_access_ok(state, addr + sp_offset, REGS_SIZE - SP_OFFSET))
			return false;

		*sp = regs->sp;
	} else
		*sp = (unsigned long)&regs->sp;

	return true;
}

bool unwind_next_frame(struct unwind_state *state)
{
	struct undwarf *undwarf;
	unsigned long cfa;
	bool indirect = false;
	enum stack_type prev_type = state->stack_info.type;
	unsigned long ip_p, prev_sp = state->sp;

	if (unwind_done(state))
		return false;

	/* Have we reached the end? */
	if (state->regs && user_mode(state->regs))
		goto done;

	/* Look up the instruction address in the .undwarf table: */
	undwarf = undwarf_lookup(state->ip);
	if (!undwarf || undwarf->cfa_reg == UNDWARF_REG_UNDEFINED)
		goto done;

	/* Calculate the CFA (caller frame address): */
	switch (undwarf->cfa_reg) {
	case UNDWARF_REG_SP:
		cfa = state->sp + undwarf->cfa_offset;
		break;

	case UNDWARF_REG_BP:
		cfa = state->bp + undwarf->cfa_offset;
		break;

	case UNDWARF_REG_SP_INDIRECT:
		cfa = state->sp + undwarf->cfa_offset;
		indirect = true;
		break;

	case UNDWARF_REG_BP_INDIRECT:
		cfa = state->bp + undwarf->cfa_offset;
		indirect = true;
		break;

	case UNDWARF_REG_R10:
		if (!state->regs) {
			undwarf_warn("missing regs for base reg R10 at ip %p\n",
				     (void *)state->ip);
			goto done;
		}
		cfa = state->regs->r10;
		break;

	case UNDWARF_REG_DI:
		if (!state->regs) {
			undwarf_warn("missing regs for base reg DI at ip %p\n",
				     (void *)state->ip);
			goto done;
		}
		cfa = state->regs->di;
		break;

	case UNDWARF_REG_DX:
		if (!state->regs) {
			undwarf_warn("missing regs for base reg DI at ip %p\n",
				     (void *)state->ip);
			goto done;
		}
		cfa = state->regs->dx;
		break;

	default:
		undwarf_warn("unknown CFA base reg %d for ip %p\n",
			     undwarf->cfa_reg, (void *)state->ip);
		goto done;
	}

	if (indirect) {
		if (!deref_stack_reg(state, cfa, &cfa))
			goto done;
	}

	/* Find IP, SP and possibly regs: */
	switch (undwarf->type) {
	case UNDWARF_TYPE_CFA:
		ip_p = cfa - sizeof(long);

		if (!deref_stack_reg(state, ip_p, &state->ip))
			goto done;

		state->ip = ftrace_graph_ret_addr(state->task, &state->graph_idx,
						  state->ip, (void *)ip_p);

		state->sp = cfa;
		state->regs = NULL;
		break;

	case UNDWARF_TYPE_REGS:
		if (!deref_stack_regs(state, cfa, &state->ip, &state->sp, true)) {
			undwarf_warn("can't dereference registers at %p for ip %p\n",
				     (void *)cfa, (void *)state->ip);
			goto done;
		}

		state->regs = (struct pt_regs *)cfa;
		break;

	case UNDWARF_TYPE_REGS_IRET:
		if (!deref_stack_regs(state, cfa, &state->ip, &state->sp, false)) {
			undwarf_warn("can't dereference iret registers at %p for ip %p\n",
				     (void *)cfa, (void *)state->ip);
			goto done;
		}

		state->regs = NULL;
		break;

	default:
		undwarf_warn("unknown undwarf type %d\n", undwarf->type);
		break;
	}

	/* Find BP: */
	switch (undwarf->bp_reg) {
	case UNDWARF_REG_UNDEFINED:
		if (state->regs)
			state->bp = state->regs->bp;
		break;

	case UNDWARF_REG_CFA:
		if (!deref_stack_reg(state, cfa + undwarf->bp_offset, &state->bp))
			goto done;
		break;

	case UNDWARF_REG_BP:
		if (!deref_stack_reg(state, state->bp + undwarf->bp_offset, &state->bp))
			goto done;
		break;

	default:
		undwarf_warn("unknown BP base reg %d for ip %p\n",
			     undwarf->bp_reg, (void *)undwarf_ip(undwarf));
		goto done;
	}

	/* Prevent a recursive loop due to bad .undwarf data: */
	if (state->stack_info.type == prev_type &&
	    on_stack(&state->stack_info, (void *)state->sp, sizeof(long)) &&
	    state->sp <= prev_sp) {
		undwarf_warn("stack going in the wrong direction? ip=%p\n",
			     (void *)state->ip);
		goto done;
	}

	return true;

done:
	state->stack_info.type = STACK_TYPE_UNKNOWN;
	return false;
}
EXPORT_SYMBOL_GPL(unwind_next_frame);

void __unwind_start(struct unwind_state *state, struct task_struct *task,
		    struct pt_regs *regs, unsigned long *first_frame)
{
	memset(state, 0, sizeof(*state));
	state->task = task;

	if (regs) {
		if (user_mode(regs)) {
			state->stack_info.type = STACK_TYPE_UNKNOWN;
			return;
		}

		state->ip = regs->ip;
		state->sp = kernel_stack_pointer(regs);
		state->bp = regs->bp;
		state->regs = regs;

	} else if (task == current) {
		register void *__sp asm(_ASM_SP);

		asm volatile("lea (%%rip), %0\n\t"
			     "mov %%rsp, %1\n\t"
			     "mov %%rbp, %2\n\t"
			     : "=r" (state->ip), "=r" (state->sp),
			       "=r" (state->bp), "+r" (__sp));

		state->regs = NULL;

	} else {
		struct inactive_task_frame *frame = (void *)task->thread.sp;

		state->ip = frame->ret_addr;
		state->sp = task->thread.sp;
		state->bp = frame->bp;
		state->regs = NULL;
	}

	if (get_stack_info((unsigned long *)state->sp, state->task,
			   &state->stack_info, &state->stack_mask))
		return;

	/*
	 * The caller can provide the address of the first frame directly
	 * (first_frame) or indirectly (regs->sp) to indicate which stack frame
	 * to start unwinding at.  Skip ahead until we reach it.
	 */
	while (!unwind_done(state) &&
	       (!on_stack(&state->stack_info, first_frame, sizeof(long)) ||
			state->sp <= (unsigned long)first_frame))
		unwind_next_frame(state);
}
EXPORT_SYMBOL_GPL(__unwind_start);

static void undwarf_sort_swap(void *_a, void *_b, int size)
{
	struct undwarf *a = _a, *b = _b, tmp;
	int delta = _b - _a;

	tmp = *a;
	*a = *b;
	*b = tmp;

	a->ip += delta;
	b->ip -= delta;
}

static int undwarf_sort_cmp(const void *_a, const void *_b)
{
	unsigned long a = undwarf_ip((struct undwarf *)_a);
	unsigned long b = undwarf_ip((struct undwarf *)_b);

	if (a > b)
		return 1;
	if (a < b)
		return -1;
	return 0;
}

void unwind_module_init(struct module *mod, void *u, size_t size)
{
	struct undwarf *undwarf = u;
	unsigned int num = size / sizeof(*undwarf);

	WARN_ON_ONCE(size % sizeof(*undwarf) != 0);

	sort(undwarf, num, sizeof(*undwarf), undwarf_sort_cmp, undwarf_sort_swap);

	mod->arch.undwarf = undwarf;
	mod->arch.num_undwarves = num;
}
