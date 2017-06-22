#include <linux/module.h>
#include <linux/sort.h>
#include <asm/ptrace.h>
#include <asm/stacktrace.h>
#include <asm/unwind.h>
#include <asm/undwarf.h>
#include <asm/sections.h>

#define undwarf_warn(fmt, ...) \
	printk_deferred_once(KERN_WARNING pr_fmt("WARNING: " fmt), ##__VA_ARGS__)

extern int __start_undwarf_ip[];
extern int __stop_undwarf_ip[];
extern struct undwarf __start_undwarf[];
extern struct undwarf __stop_undwarf[];

bool undwarf_init;
static DEFINE_MUTEX(sort_mutex);

int *cur_undwarf_ip_table = __start_undwarf_ip;
struct undwarf *cur_undwarf_table = __start_undwarf;

#define LOOKUP_NUM_BLOCKS		(8 * 1024)
#define LOOKUP_FIRST_IP			(undwarf_ip(__start_undwarf_ip))
#define LOOKUP_LAST_IP			(unsigned long)_etext
#define LOOKUP_BLOCK_SIZE						\
	(DIV_ROUND_UP(LOOKUP_LAST_IP - LOOKUP_FIRST_IP,			\
		      LOOKUP_NUM_BLOCKS))

static unsigned int undwarf_lookup[LOOKUP_NUM_BLOCKS];

static inline unsigned long undwarf_ip(const int *ip)
{
	return (unsigned long)ip + *ip;
}

static struct undwarf *__undwarf_find(int *ip_table, struct undwarf *u_table,
				      unsigned int num_entries,
				      unsigned long ip)
{
	int *first = ip_table;
	int *last = ip_table + num_entries - 1;
	int *mid = first, *found = first;

	/*
	 * This is a binary range search to find the rightmost duplicate of a
	 * given starting address.  Some entries are section terminators which
	 * are "weak" entries for ensuring there are no gaps.  They should be
	 * ignored when they conflict with a real entry.
	 */

	if (!num_entries || undwarf_ip(ip_table) > ip)
		return NULL;

	while (first <= last) {
		mid = first + ((last - first) / 2);

		if (undwarf_ip(mid) <= ip) {
			found = mid;
			first = mid + 1;
		} else
			last = mid - 1;
	}

	return u_table + (found - ip_table);
}

static struct undwarf *undwarf_find(unsigned long ip)
{
	struct module *mod;

	if (!undwarf_init)
		return NULL;

	/*
	 * For non-init vmlinux addresses, we have a fast lookup table which
	 * associates the given address with a range of undwarf table indices,
	 * so that only a small subset of the undwarf table will need to be
	 * searched.
	 */
	if (ip >= LOOKUP_FIRST_IP && ip < LOOKUP_LAST_IP) {
		unsigned int idx, start, stop;

		idx = (ip - LOOKUP_FIRST_IP) / LOOKUP_BLOCK_SIZE;

		if (WARN_ON_ONCE(idx >= LOOKUP_NUM_BLOCKS ||
				 idx+1 >= LOOKUP_NUM_BLOCKS))
			return NULL;

		start = undwarf_lookup[idx];
		stop = undwarf_lookup[idx + 1] + 1;

		if (WARN_ON_ONCE(__start_undwarf + start >= __stop_undwarf) ||
				 __start_undwarf + stop > __stop_undwarf)
			return NULL;

		return __undwarf_find(__start_undwarf_ip + start,
				      __start_undwarf + start,
				      stop - start, ip);
	}

	/* vmlinux .init slow lookup: */
	if (ip >= (unsigned long)_sinittext && ip <= (unsigned long)_einittext)
		return __undwarf_find(__start_undwarf_ip, __start_undwarf,
				      __stop_undwarf - __start_undwarf, ip);

	mod = __module_address(ip);
	if (!mod || !mod->arch.undwarf || !mod->arch.undwarf_ip)
		return NULL;

	return __undwarf_find(mod->arch.undwarf_ip, mod->arch.undwarf,
			      mod->arch.num_undwarves, ip);
}

static void undwarf_sort_swap(void *_a, void *_b, int size)
{
	int *a = _a, *b = _b, tmp;
	int delta = _b - _a;
	struct undwarf *undwarf_a = cur_undwarf_table + (a - cur_undwarf_ip_table);
	struct undwarf *undwarf_b = cur_undwarf_table + (b - cur_undwarf_ip_table);
	struct undwarf undwarf_tmp;

	tmp = *a;
	*a = *b + delta;
	*b = tmp - delta;

	undwarf_tmp = *undwarf_a;
	*undwarf_a = *undwarf_b;
	*undwarf_b = undwarf_tmp;
}

static int undwarf_sort_cmp(const void *_a, const void *_b)
{
	const int *a = _a, *b = _b;
	unsigned long a_val = undwarf_ip(a);
	unsigned long b_val = undwarf_ip(b);
	struct undwarf *undwarf_a = cur_undwarf_table + (a - cur_undwarf_ip_table);

	if (a_val > b_val)
		return 1;
	if (a_val < b_val)
		return -1;

	/*
	 * The "weak" section terminator entries need to always be on the left
	 * to ensure the lookup code skips them in favor of real entries.
	 * These terminator entries exist to handle any gaps created by
	 * whitelisted .o files which didn't get objtool generation.
	 */
	return undwarf_a->cfa_reg == UNDWARF_REG_UNDEFINED ? -1 : 1;
}

void unwind_module_init(struct module *mod, void *_undwarf_ip,
			size_t undwarf_ip_size, void *_undwarf,
			size_t undwarf_size)
{
	int *undwarf_ip = _undwarf_ip;
	struct undwarf *undwarf = _undwarf;
	unsigned int num_entries = undwarf_ip_size / sizeof(int);

	WARN_ON_ONCE(undwarf_ip_size % sizeof(int) != 0 ||
		     undwarf_size % sizeof(*undwarf) != 0 ||
		     num_entries != undwarf_size / sizeof(*undwarf));

	/*
	 * The 'cur_undwarf_*' globals allow the undwarf_sort_swap() callback
	 * to associate an undwarf_ip table entry with its corresponding
	 * undwarf entry so they can both be swapped.
	 */
	mutex_lock(&sort_mutex);
	cur_undwarf_ip_table = undwarf_ip;
	cur_undwarf_table = undwarf;
	sort(undwarf_ip, num_entries, sizeof(int),undwarf_sort_cmp,
	     undwarf_sort_swap);
	mutex_unlock(&sort_mutex);

	mod->arch.undwarf_ip = undwarf_ip;
	mod->arch.undwarf = undwarf;
	mod->arch.num_undwarves = num_entries;
}

void __init unwind_init(void)
{
	size_t undwarf_ip_size = (void *)__stop_undwarf_ip - (void *)__start_undwarf_ip;
	size_t undwarf_size = (void *)__stop_undwarf - (void *)__start_undwarf;
	size_t num_entries = undwarf_ip_size / sizeof(int);
	int i;

	if (!num_entries || undwarf_ip_size % sizeof(int) != 0 ||
	    undwarf_size % sizeof(struct undwarf) != 0 ||
	    num_entries != undwarf_size / sizeof(struct undwarf)) {
		pr_warn("WARNING: Bad or missing undwarf table.  Disabling unwinder.\n");
		return;
	}

	sort(__start_undwarf_ip, num_entries, sizeof(int), undwarf_sort_cmp,
	     undwarf_sort_swap);

	if (LOOKUP_FIRST_IP < (unsigned long)_stext ||
	    LOOKUP_FIRST_IP >= (unsigned long)_etext) {
		pr_warn("WARNING: Corrupt undwarf table.  Disabling unwinder.\n");
		return;
	}

	for (i = 0; i < LOOKUP_NUM_BLOCKS; i++) {
		struct undwarf *undwarf;

		undwarf = __undwarf_find(__start_undwarf_ip, __start_undwarf,
					 num_entries,
					 LOOKUP_FIRST_IP + (LOOKUP_BLOCK_SIZE * i));
		if (!undwarf) {
			pr_warn("WARNING: Corrupt undwarf table.  Disabling unwinder.\n");
			return;
		}

		undwarf_lookup[i] = undwarf - __start_undwarf;
	}

	undwarf_init = true;
}

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

static bool stack_access_ok(struct unwind_state *state, unsigned long addr,
			    size_t len)
{
	struct stack_info *info = &state->stack_info;

	/*
	 * If the address isn't on the current stack, switch to the next one.
	 *
	 * We may have to traverse multiple stacks to deal with the possibility
	 * that info->next_sp could point to an empty stack and the address
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
	unsigned long cfa, orig_ip;
	bool indirect = false;
	enum stack_type prev_type = state->stack_info.type;
	unsigned long ip_p, prev_sp = state->sp;

	if (unwind_done(state))
		return false;

	/* Don't let modules unload while we're reading their undwarf data. */
	preempt_disable();

	/* Have we reached the end? */
	if (state->regs && user_mode(state->regs))
		goto done;

	/*
	 * Find the undwarf table entry associated with the text address.
	 *
	 * Decrement call return addresses by one so they work for sibling
	 * calls and calls to noreturn functions.
	 */
	undwarf = undwarf_find(state->signal ? state->ip : state->ip - 1);
	if (!undwarf || undwarf->cfa_reg == UNDWARF_REG_UNDEFINED)
		goto done;
	orig_ip = state->ip;

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

	case UNDWARF_REG_R13:
		if (!state->regs) {
			undwarf_warn("missing regs for base reg R13 at ip %p\n",
				     (void *)state->ip);
			goto done;
		}
		cfa = state->regs->r13;
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
			undwarf_warn("missing regs for base reg DX at ip %p\n",
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
		state->signal = false;
		break;

	case UNDWARF_TYPE_REGS:
		if (!deref_stack_regs(state, cfa, &state->ip, &state->sp, true)) {
			undwarf_warn("can't dereference registers at %p for ip %p\n",
				     (void *)cfa, (void *)orig_ip);
			goto done;
		}

		state->regs = (struct pt_regs *)cfa;
		state->signal = true;
		break;

	case UNDWARF_TYPE_REGS_IRET:
		if (!deref_stack_regs(state, cfa, &state->ip, &state->sp, false)) {
			undwarf_warn("can't dereference iret registers at %p for ip %p\n",
				     (void *)cfa, (void *)orig_ip);
			goto done;
		}

		state->regs = NULL;
		state->signal = true;
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
			     undwarf->bp_reg, (void *)orig_ip);
		goto done;
	}

	/* Prevent a recursive loop due to bad undwarf data: */
	if (state->stack_info.type == prev_type &&
	    on_stack(&state->stack_info, (void *)state->sp, sizeof(long)) &&
	    state->sp <= prev_sp) {
		undwarf_warn("stack going in the wrong direction? ip=%p\n",
			     (void *)orig_ip);
		goto done;
	}

	preempt_enable();
	return true;

done:
	preempt_enable();
	state->stack_info.type = STACK_TYPE_UNKNOWN;
	return false;
}
EXPORT_SYMBOL_GPL(unwind_next_frame);

void __unwind_start(struct unwind_state *state, struct task_struct *task,
		    struct pt_regs *regs, unsigned long *first_frame)
{
	memset(state, 0, sizeof(*state));
	state->task = task;

	/*
	 * Preemptively refuse to unwind the stack of a task while it's
	 * executing on another CPU.  This check is racy, but that's ok: the
	 * unwinder has other checks to prevent it from going off the rails.
	 */
	if (task_on_another_cpu(task))
		goto done;

	if (regs) {
		if (user_mode(regs))
			goto done;

		state->ip = regs->ip;
		state->sp = kernel_stack_pointer(regs);
		state->bp = regs->bp;
		state->regs = regs;
		state->signal = true;

	} else if (task == current) {
		asm volatile("lea (%%rip), %0\n\t"
			     "mov %%rsp, %1\n\t"
			     "mov %%rbp, %2\n\t"
			     : "=r" (state->ip), "=r" (state->sp),
			       "=r" (state->bp));

	} else {
		struct inactive_task_frame *frame = (void *)task->thread.sp;

		state->ip = frame->ret_addr;
		state->sp = task->thread.sp;
		state->bp = frame->bp;
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

	return;

done:
	state->stack_info.type = STACK_TYPE_UNKNOWN;
	return;
}
EXPORT_SYMBOL_GPL(__unwind_start);
