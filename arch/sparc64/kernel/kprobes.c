/* arch/sparc64/kernel/kprobes.c
 *
 * Copyright (C) 2004 David S. Miller <davem@davemloft.net>
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>

#include <asm/kdebug.h>
#include <asm/signal.h>

/* We do not have hardware single-stepping, so in order
 * to implement post handlers correctly we use two breakpoint
 * instructions.
 *
 *          1) ta 0x70 --> 0x91d02070
 *          2) ta 0x71 --> 0x91d02071
 *
 * When these are hit, control is transferred to kprobe_trap()
 * below.  The arg 'level' tells us which of the two traps occurred.
 *
 * Initially, the instruction at p->addr gets set to "ta 0x70"
 * by code in register_kprobe() by setting that memory address
 * to BREAKPOINT_INSTRUCTION.  When this breakpoint is hit
 * the following happens:
 *
 * 1) We run the pre-handler
 * 2) We replace p->addr with the original opcode
 * 3) We set the instruction at "regs->npc" to "ta 0x71"
 * 4) We mark that we are waiting for the second breakpoint
 *    to hit and return from the trap.
 *
 * At this point we wait for the second breakpoint to hit.
 * When it does:
 *
 * 1) We run the post-handler
 * 2) We re-install "ta 0x70" at p->addr
 * 3) We restore the opcode at the "ta 0x71" breakpoint
 * 4) We reset our "waiting for "ta 0x71" state
 * 5) We return from the trap
 *
 * We could use the trick used by the i386 kprobe code but I
 * think that scheme has problems with exception tables.  On i386
 * they single-step over the original instruction stored at
 * kprobe->insn.  So they set the processor to single step, and
 * set the program counter to kprobe->insn.
 *
 * But that explodes if the original opcode is a user space
 * access instruction and that faults.  It will go wrong because
 * since the location of the instruction being executed is
 * different from that recorded in the exception tables, the
 * kernel will not find it and this will cause an erroneous
 * kernel OOPS.
 */

void arch_prepare_kprobe(struct kprobe *p)
{
	p->insn[0] = *p->addr;
	p->insn[1] = 0xdeadbeef;
}

static void prepare_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	u32 *insn2 = (u32 *) regs->tpc;

	p->insn[1] = *insn2;

	*insn2 = BREAKPOINT_INSTRUCTION_2;
	flushi(insn2);
}

static void undo_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	u32 *insn2 = (u32 *) regs->tpc;

	BUG_ON(p->insn[1] == 0xdeadbeef);

	*insn2 = p->insn[1];
	flushi(insn2);

	p->insn[1] = 0xdeadbeef;
}

/* kprobe_status settings */
#define KPROBE_HIT_ACTIVE	0x00000001
#define KPROBE_HIT_SS		0x00000002

static struct kprobe *current_kprobe;
static unsigned int kprobe_status;

static int kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *p;
	void *addr = (void *) regs->tpc;
	int ret = 0;

	preempt_disable();

	if (kprobe_running()) {
		p = get_kprobe(addr);
		if (p) {
			*p->addr = p->opcode;
			flushi(p->addr);
			ret = 1;
		} else {
			p = current_kprobe;
			if (p->break_handler && p->break_handler(p, regs))
				goto ss_probe;
		}
		goto no_kprobe;
	}

	lock_kprobes();
	p = get_kprobe(addr);
	if (!p) {
		unlock_kprobes();
		if (*(u32 *)addr != BREAKPOINT_INSTRUCTION)
			ret = 1;
		goto no_kprobe;
	}

	kprobe_status = KPROBE_HIT_ACTIVE;
	current_kprobe = p;
	if (p->pre_handler(p, regs))
		return 1;

ss_probe:
	prepare_singlestep(p, regs);
	kprobe_status = KPROBE_HIT_SS;
	return 1;

no_kprobe:
	preempt_enable_no_resched();
	return ret;
}

static int post_kprobe_handler(struct pt_regs *regs)
{
	u32 *insn_p = (u32 *) regs->tpc;

	if (!kprobe_running() || (*insn_p != BREAKPOINT_INSTRUCTION_2))
		return 0;

	if (current_kprobe->post_handler)
		current_kprobe->post_handler(current_kprobe, regs, 0);

	undo_singlestep(current_kprobe, regs);

	unlock_kprobes();
	preempt_enable_no_resched();

	return 1;
}

/* Interrupts disabled, kprobe_lock held. */
static inline int kprobe_fault_handler(struct pt_regs *regs, int trapnr)
{
	if (current_kprobe->fault_handler
	    && current_kprobe->fault_handler(current_kprobe, regs, trapnr))
		return 1;

	if (kprobe_status & KPROBE_HIT_SS) {
		undo_singlestep(current_kprobe, regs);

		unlock_kprobes();
		preempt_enable_no_resched();
	}
	return 0;
}

/*
 * Wrapper routine to for handling exceptions.
 */
int kprobe_exceptions_notify(struct notifier_block *self, unsigned long val,
			     void *data)
{
	struct die_args *args = (struct die_args *)data;
	switch (val) {
	case DIE_DEBUG:
		if (kprobe_handler(args->regs))
			return NOTIFY_OK;
		break;
	case DIE_DEBUG_2:
		if (post_kprobe_handler(args->regs))
			return NOTIFY_OK;
		break;
	case DIE_GPF:
		if (kprobe_running() &&
		    kprobe_fault_handler(args->regs, args->trapnr))
			return NOTIFY_OK;
		break;
	case DIE_PAGE_FAULT:
		if (kprobe_running() &&
		    kprobe_fault_handler(args->regs, args->trapnr))
			return NOTIFY_OK;
		break;
	default:
		break;
	}
	return NOTIFY_BAD;
}

asmlinkage void kprobe_trap(unsigned long trap_level, struct pt_regs *regs)
{
	BUG_ON(trap_level != 0x170 && trap_level != 0x171);

	if (user_mode(regs)) {
		local_irq_enable();
		bad_trap(regs, trap_level);
		return;
	}

	/* trap_level == 0x170 --> ta 0x70
	 * trap_level == 0x171 --> ta 0x71
	 */
	if (notify_die((trap_level == 0x170) ? DIE_DEBUG : DIE_DEBUG_2,
		       (trap_level == 0x170) ? "debug" : "debug_2",
		       regs, 0, trap_level, SIGTRAP) != NOTIFY_OK)
		bad_trap(regs, trap_level);
}

/* Jprobes support.  */
static struct pt_regs jprobe_saved_regs;
static struct sparc_stackf jprobe_saved_stack;

int setjmp_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct jprobe *jp = container_of(p, struct jprobe, kp);

	memcpy(&jprobe_saved_regs, regs, sizeof(*regs));

	/* Save a whole stack frame, this gets arguments
	 * pushed onto the stack after using up all the
	 * arg registers.
	 */
	memcpy(&jprobe_saved_stack,
	       (char *) (regs->u_regs[UREG_FP] + STACK_BIAS),
	       sizeof(jprobe_saved_stack));

	regs->tpc  = (unsigned long) jp->entry;
	regs->tnpc = ((unsigned long) jp->entry) + 0x4UL;

	return 1;
}

void jprobe_return(void)
{
	preempt_enable_no_resched();
	__asm__ __volatile__(
		".globl	jprobe_return_trap_instruction\n"
"jprobe_return_trap_instruction:\n\t"
		"ta 0x70");
}

extern void jprobe_return_trap_instruction(void);

int longjmp_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	u32 *addr = (u32 *) regs->tpc;

	if (addr == (u32 *) jprobe_return_trap_instruction) {
		/* Restore old register state.  Do pt_regs
		 * first so that UREG_FP is the original one for
		 * the stack frame restore.
		 */
		memcpy(regs, &jprobe_saved_regs, sizeof(*regs));

		memcpy((char *) (regs->u_regs[UREG_FP] + STACK_BIAS),
		       &jprobe_saved_stack,
		       sizeof(jprobe_saved_stack));

		return 1;
	}
	return 0;
}
