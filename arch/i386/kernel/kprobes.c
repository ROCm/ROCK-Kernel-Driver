/* 
 * Support for kernel probes.
 * (C) 2002 Vamsi Krishna S <vamsi_krishna@in.ibm.com>.
 */

#include <linux/config.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/spinlock.h>
#include <linux/preempt.h>
#include <asm/cacheflush.h>

/* kprobe_status settings */
#define KPROBE_HIT_ACTIVE	0x00000001
#define KPROBE_HIT_SS		0x00000002

static struct kprobe *current_kprobe;
static unsigned long kprobe_status, kprobe_old_eflags, kprobe_saved_eflags;

void kprobes_asm_code_start(void){}
/*
 * returns non-zero if opcode modifies the interrupt flag.
 */
static inline int is_IF_modifier(kprobe_opcode_t opcode)
{
	switch(opcode) {
		case 0xfa: 	/* cli */
		case 0xfb:	/* sti */
		case 0xcf:	/* iret/iretd */
		case 0x9d:	/* popf/popfd */
			return 1;
	}
	return 0;
}

void arch_prepare_kprobe(struct kprobe *p)
{
	memcpy(p->insn, p->addr, MAX_INSN_SIZE);
}

void set_opcode_k(struct kprobe *p, kprobe_opcode_t opcode)
{
	kprobe_opcode_t *addr = p->addr;
	*addr = opcode;
	flush_icache_range(addr, addr + sizeof(kprobe_opcode_t));
}

void set_opcode_user(struct kprobe *p, kprobe_opcode_t opcode)
{
	*p->user->addr = opcode;
	flush_icache_user_range(p->user->vma, p->user->page, p->user->addr, sizeof(kprobe_opcode_t));
}

static inline void disarm_kprobe(struct kprobe *p, struct pt_regs *regs)
{
	set_opcode(p, p->opcode);
	regs->eip = (unsigned long)p->addr;
}

static void prepare_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	regs->eflags |= TF_MASK;
	regs->eflags &= ~IF_MASK;

	if (!(p->user)) {
		regs->eip = (unsigned long)&p->insn;
	} else {
		set_opcode(p, p->opcode);
		regs->eip = (unsigned long)p->addr;
		}
}

void arch_insert_breakpoint(struct kprobe *p) {
	arch_prepare_kprobe(p);
	p->opcode = *(p->addr);
	set_opcode_k(p, BREAKPOINT_INSTRUCTION);
}

void arch_remove_breakpoint(struct kprobe *p) {
	set_opcode_k(p, p->opcode);
}
/*
 * Interrupts are disabled on entry as trap3 is an interrupt gate and they
 * remain disabled thorough out this function.
 */
int kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *p;
	int ret = 0;
	kprobe_opcode_t *addr = (kprobe_opcode_t *)(regs->eip - sizeof(kprobe_opcode_t));

	/* We're in an interrupt, but this is clear and BUG()-safe. */
	preempt_disable();

	/* Check we're not actually recursing */
	if (kprobe_running()) {
		/* We *are* holding lock here, so this is safe.
                   Disarm the probe we just hit, and ignore it. */
		p = get_kprobe(addr);
		if (p) {
			disarm_kprobe(p, regs);
			put_kprobe(p);
			ret = 1;
		}
		/* If it's not ours, can't be delete race, (we hold lock). */
		goto no_kprobe;
	}

	lock_kprobes();
	p = get_kprobe(addr); 
	if (!p) {
		unlock_kprobes();
		/* Unregistered (on another cpu) after this hit?  Ignore */
		if (*addr != BREAKPOINT_INSTRUCTION)
			ret = 1;
		/* Not one of ours: let kernel handle it */
		goto no_kprobe;
	}
	/* If p->opcode == BREAKPOINT_INSTRUCTION, means we haven't yet inserted
	 * breakpoint corresponding to this probe.No need to handle this probe.
	 */ 
	if (p->user && p->opcode == BREAKPOINT_INSTRUCTION) {
		unlock_kprobes();
		ret = 1;
		goto no_kprobe;
	}
		
	kprobe_status = KPROBE_HIT_ACTIVE;
	current_kprobe = p;
	kprobe_saved_eflags = kprobe_old_eflags 
		= (regs->eflags & (TF_MASK|IF_MASK));
	if (is_IF_modifier(p->opcode))
		kprobe_saved_eflags &= ~IF_MASK;

	/* If the pre_handler returns 1, means probes are marked for emergency
	 *  removal. Restore back the original opcode.
	 */
	if (p->pre_handler(p, regs)) {
		if (!p->user)
			disarm_kprobe(p, regs);
		else {
			set_opcode(p, p->opcode);
			regs->eip = (unsigned long)p->addr;
		}
		ret = 1;
		unlock_kprobes();
		goto no_kprobe;
	}

	prepare_singlestep(p, regs);
	//disarm_kprobe(p, regs);
	kprobe_status = KPROBE_HIT_SS;
	return 1;

	if (p->post_handler)
		p->post_handler(p, regs, 0);
	unlock_kprobes();
	preempt_enable_no_resched();
	return 1;

no_kprobe:
	preempt_enable_no_resched();
	return ret;
}

static void resume_execution(struct kprobe *p, struct pt_regs *regs)
{
	unsigned long *tos = &regs->esp;
	unsigned long next_eip = 0;
	unsigned long copy_eip = (unsigned long)&p->insn;
	unsigned long orig_eip = (unsigned long)p->addr;

	/*
	 * We singlestepped with interrupts disabled. So, the result on
	 * the stack would be incorrect for "pushfl" instruction.
	 * Note that regs->esp is actually the top of the stack when the
	 * trap occurs in kernel space.
	 */
	switch(p->insn[0]) {
		case 0x9c:	/* pushfl */
			if (regs->eip < PAGE_OFFSET)
				tos = (unsigned long *)regs->esp;
			*tos &= ~(TF_MASK | IF_MASK);
			*tos |= kprobe_old_eflags;
			break;
		case 0xe8:	/* call relative */
			*tos = orig_eip + (*tos - copy_eip);
			break;
		case 0xff:	
			if ((p->insn[1] & 0x30) == 0x10) { /* call absolute, indirect */
				next_eip = regs->eip;
				*tos = orig_eip + (*tos - copy_eip);
			} else if (((p->insn[1] & 0x31) == 0x20) || /* jmp near, absolute indirect */
				   ((p->insn[1] & 0x31) == 0x21)) { /* jmp far, absolute indirect */
				next_eip = regs->eip;
			}
			break;
		case 0xea:	/* jmp absolute */
			next_eip = regs->eip;
			break;
		default:
			break;
	}
	
	regs->eflags &= ~TF_MASK;
	if (next_eip) {
		regs->eip = next_eip;
	} else {
		regs->eip = orig_eip + (regs->eip - copy_eip);
	}
}

static void resume_execution_user(struct kprobe *p, struct pt_regs *regs)
{
	regs->eflags &= ~TF_MASK;
	set_opcode(p, BREAKPOINT_INSTRUCTION);
}

/*
 * Interrupts are disabled on entry as trap1 is an interrupt gate and they
 * remain disabled thorough out this function.  And we hold kprobe lock.
 */
int post_kprobe_handler(struct pt_regs *regs)
{
	if (!kprobe_running())
		return 0;

	if (current_kprobe->post_handler)
		current_kprobe->post_handler(current_kprobe, regs, 0);

	if (!(current_kprobe->user))
		resume_execution(current_kprobe, regs);
	else
		resume_execution_user(current_kprobe, regs);
	regs->eflags |= kprobe_saved_eflags;

	put_kprobe(current_kprobe);
	unlock_kprobes();
	preempt_enable_no_resched();

        /*
	 * if somebody else is singlestepping across a probe point, eflags
	 * will have TF set, in which case, continue the remaining processing
	 * of do_debug, as if this is not a probe hit.
	 */
	if (regs->eflags & TF_MASK)
		return 0;

	return 1;
}

/* Interrupts disabled, kprobe_lock held. */
int kprobe_fault_handler(struct pt_regs *regs, int trapnr)
{
	if (current_kprobe->fault_handler
	    && current_kprobe->fault_handler(current_kprobe, regs, trapnr))
		return 1;

	if (kprobe_status & KPROBE_HIT_SS) {
		if (!(current_kprobe->user))
			resume_execution(current_kprobe, regs);
		else
			resume_execution_user(current_kprobe, regs);

		regs->eflags |= kprobe_old_eflags;
		put_kprobe(current_kprobe);
		unlock_kprobes();
		preempt_enable_no_resched();
	}
	return 0;
}
void kprobes_asm_code_end(void) {}
