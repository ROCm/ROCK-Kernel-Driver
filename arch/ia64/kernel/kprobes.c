/* 
 * Support for kernel probes.
 * (C) 2002 Vamsi Krishna S <vamsi_krishna@in.ibm.com>.
 */

#include <linux/config.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/preempt.h>

#include <asm/cacheflush.h>

/*  Macros for IA-64 instruction field extraction */
#define IA64_MAJ_OPCODE(instr) \
	((instr & (0x0ful << 37)) >> 37) /* major opcode value */
#define IA64_M44_X3(instr) \
	((instr & (0x07ul << 33)) >> 33) /* M44 x3 opcode extension value */
#define IA64_M44_X4(instr) \
	((instr & (0x0ful << 27)) >> 27) /* M44 x4 opcode extension value */
#define IA64_M44_IMM14(instr) \
	((instr & (0x01ul << 20)) >> 20) /* M44 imm bit 14, 1 if set, 0 otherwise */
#define IA64_M35_X3(instr) \
	((instr & (0x07ul << 33)) >> 33) /* M35 x3 opcode extension value */
#define IA64_M35_X6(instr) \
	((instr & (0x3ful << 27)) >> 27) /* M35 x6 opcode extension value */
#define IA64_M35_R2(instr) \
	((instr & (0x7ful << 13)) >> 13) /* M35 r2 operand value */
#define IA64_M36_X3(instr) \
	((instr & (0x07ul << 33)) >> 33) /* M36 x3 opcode extension value */
#define IA64_M36_X6(instr) \
	((instr & (0x3ful << 27)) >> 27) /* M36 x6 opcode extension value */
#define IA64_M36_R1(instr) \
	((instr & (0x7ful << 6)) >> 6)   /* M36 r1 operand value */

/* kprobe_status settings */
#define KPROBE_HIT_ACTIVE	0x00000001
#define KPROBE_HIT_SS		0x00000002

static struct kprobe *current_kprobe;
static unsigned long kprobe_status, kprobe_old_eflags, kprobe_saved_eflags;

void arch_insert_breakpoint(struct kprobe *p)
{
	unsigned long *bundle_start_addr = 
		(unsigned long *)((unsigned long)p->addr & ~0x0f);
	unsigned long actual_instruction = 0;
	actual_instruction = (*bundle_start_addr & KPROBE_SLOT0_INSTR_MASK) >> 5;
	memcpy(p->insn, &actual_instruction, MAX_INSN_SIZE);
	*bundle_start_addr = (*bundle_start_addr & ~KPROBE_SLOT0_INSTR_MASK) | 
				(KPROBE_INSTR_BREAKPOINT << 5);
	flush_icache_range((unsigned long )bundle_start_addr, (unsigned long)bundle_start_addr + MAX_INSN_SIZE);
	return;
}

unsigned long kp_get_gr(unsigned short regnum, struct pt_regs *regs, struct switch_stack *sw)
{
	unsigned long val = 0, sof = 0, sol, soo, bsp, ip, cfm, *addr;
	unsigned long *krbs, num_regs_on_krbs, *bspstore, *urbs_end;
	char nat = 0;
	struct unw_frame_info info;

	if (regnum < 32) {
		/* gr0-gr31, Static General Registers */
		switch(regnum) {
			case 0:  /* r0, read only, always 0 */
				val = 0;
				break;
			case 1:  /* r1, gp */
			case 2:  /* r2 */
			case 3:  /* r3 */
				addr = &regs->r1 + (regnum - 1);
				val = *addr;
				break;
			case 4:  /* r4 */
			case 5:  /* r5 */
			case 6:  /* r6 */
			case 7:  /* r7 */
				addr = &sw->r4 + (regnum - 4);
				val = *addr;
				break;
			case 8:  /* r8, ret0 */
			case 9:  /* r9, ret1 */
			case 10: /* r10, ret2 */
			case 11: /* r11, ret3 */
				addr = &regs->r8 + (regnum - 8);
				val = *addr;
				break;
			case 12: /* r12, sp */
			case 13: /* r13 */
			case 14: /* r14 */
			case 15: /* r15 */
				addr = &regs->r12 + (regnum - 12);
				val = *addr;
				break;
			case 16:  /* r16 */
			case 17:  /* r17 */
			case 18:  /* r18 */
			case 19:  /* r19 */
			case 20:  /* r20 */
			case 21:  /* r21 */
			case 22:  /* r22 */
			case 23:  /* r23 */
			case 24:  /* r24 */
			case 25:  /* r25 */
			case 26:  /* r26 */
			case 27:  /* r27 */
			case 28:  /* r28 */
			case 29:  /* r29 */
			case 30:  /* r30 */
			case 31:  /* r31 */
				addr = &regs->r16 + (regnum - 16);
				val = *addr;
				break;
			default:
				/* Shouldn't get here */
				break;
		}
	} else {
		if (regs == ia64_task_regs(current)) {
			/*
			 * - Probe point in kernel code.
			 * - RPN instruction: 'push u' (user context).
			 * - Some stacked GR values are on the kernel
			 *   register backing store and some are on the user
			 *   register backing store.
			 *
			 * - This code appears to successfully obtain the stacked
			 *   GR values for the frame corresponding to the user
			 *   function that made a system call.  The code may need
			 *   to be updated to correctly handle other 
			 *   (non system call) kernel entries.
			 */
			krbs = (unsigned long *) current + IA64_RBS_OFFSET/8;
			sof = regs->ar_pfs & 0x7f;
			sol = (regs->ar_pfs & 0x3f80) >> 7;
			soo = sof - sol;

			/* unwind to return function and grab it's bsp */
			unw_init_frame_info(&info, current, sw);
			do {
				/* nothing */
			} while (unw_unwind(&info) >= 0);
			unw_get_bsp(&info, &bsp);

			/* The bsp for the return function points to out0 for the
			 * currently executing user application function so we
			 * need to add the number of user application function output
			 * registers (soo) to obtain a correct count of the number of
			 * registers on the kernel register backing store.
			 */
			num_regs_on_krbs = ia64_rse_num_regs(krbs, (unsigned long *)bsp) + soo;
			addr = ia64_rse_skip_regs((unsigned long *) bsp, soo - sof + (regnum - 32));
			if (addr >= krbs) {
				/* register value is on the krbs at address addr */
				if ((regnum - 32) < sof)
				val = *addr;
			} else {
				/* register value is on the urbs */
				bspstore = (unsigned long *)regs->ar_bspstore;
				urbs_end = ia64_rse_skip_regs(bspstore, num_regs_on_krbs);
				bsp = (unsigned long) ia64_rse_skip_regs(urbs_end, -sof);
				addr = ia64_rse_skip_regs((unsigned long *) bsp, regnum - 32);
				ia64_peek(current, sw, (unsigned long) urbs_end,
						(unsigned long) addr, (unsigned long *) &val);
			}
		} else {
			/* - Probe point in kernel code.
			 * - RPN instruction: 'push r' (current context).
			 * - Stacked GR values are on kernel backing store so we can
			 *   unwind to the frame for the function where the break
			 *   instruction is located and get register value using
			 *   the standard kernel unwind API.
			 */
			unw_init_frame_info(&info, current, sw);
			do {
				unw_get_ip(&info, &ip);
				if (ip == regs->cr_iip) {
					unw_get_cfm(&info, &cfm);
					sof = cfm & 0x7f;
					break;
				}
			} while (unw_unwind(&info) >= 0);
			if ((regnum - 32) < sof)
				unw_get_gr(&info, regnum, &val, &nat);
		}
	}
	return val;
}

/*
 * returns non-zero if instruction modifies the interrupt flag.
 */
static inline int is_IF_modifier(unsigned char *actual_insn, struct pt_regs * regs,  
				    struct switch_stack *sw)
{
	unsigned long val;
	unsigned short regnum;
	unsigned long instr = 0;
	memcpy(&instr, actual_insn, MAX_INSN_SIZE);
	/* 
	 * Reset System Mask (rsm imm24)
	 * changes PSR.i from 1 to 0 if:
	 *    - rsm immediate bit 14 is set to 1 and
	 *    - PSR.i is set to 1 prior to rsm execution
	 */
	if (IA64_MAJ_OPCODE(instr) == 0 &&
		IA64_M44_X3(instr) == 0 &&
		IA64_M44_X4(instr) == 7 &&
		IA64_M44_IMM14(instr) &&
		ia64_psr(regs)->i == 1) {
			return 1;
	}

	/* 
	 * Set System Mask (ssm imm24) 
	 * changes PSR.i from 0 to 1 if:
	 *   - rsm immediate bit 14 is set to 1 and
	 *   - PSR.i is set to 0 prior to rsm execution
	 */
	if (IA64_MAJ_OPCODE(instr) == 0 &&
		IA64_M44_X3(instr) == 0 &&
		IA64_M44_X4(instr) == 6 &&
		IA64_M44_IMM14(instr) &&
		ia64_psr(regs)->i == 0) {
			return 1;
	}

	/* 
	 * Move to PSR (mov psr.l=rX) 
	 * changes PSR.i if:
	 *  - the current PSR.i differs from bit 14 of the 
	 *    value stored in the source register
	 */
	if (IA64_MAJ_OPCODE(instr) == 1 &&
		IA64_M35_X3(instr) == 0 &&
		IA64_M35_X6(instr) == 0x2d) {
			regnum = IA64_M35_R2(instr);
			val = kp_get_gr(regnum, regs, sw);
			if (ia64_psr(regs)->i != (val & IA64_PSR_I)) {
				return 1;
			}
	}
	return 0;
}


/* Restore the original instruction */
static inline void disarm_kprobe(struct kprobe *p, struct pt_regs *regs)
{
	unsigned long *bundle_start_addr = 
		(unsigned long *)((unsigned long)regs->cr_iip & ~0x0f);
	unsigned long actual_instruction = 0;
	memcpy(&actual_instruction, p->insn, MAX_INSN_SIZE);
	*bundle_start_addr = (*bundle_start_addr & ~KPROBE_SLOT0_INSTR_MASK) | 
				(actual_instruction << 5); 
	return;
}

void arch_remove_breakpoint(struct kprobe *p) 
{
        unsigned long *bundle_start_addr = 
		(unsigned long *)((unsigned long)p->addr & ~0x0f);
	unsigned long actual_instruction = 0;
	memcpy(&actual_instruction, p->insn, MAX_INSN_SIZE);
	*bundle_start_addr = (*bundle_start_addr & ~KPROBE_SLOT0_INSTR_MASK) | 
				(actual_instruction << 5); 
	flush_icache_range((unsigned long)bundle_start_addr,(unsigned long) bundle_start_addr + MAX_INSN_SIZE);
	return;
}

/*
 * Does necessary setup for single stepping.
 */
static inline void prepare_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	
	unsigned long *bundle_start_addr = 
		(unsigned long *)((unsigned long)regs->cr_iip & ~0x0f);

	unsigned long actual_instruction = 0;
	memcpy(&actual_instruction, p->insn, MAX_INSN_SIZE);
	
	*bundle_start_addr = (*bundle_start_addr & ~KPROBE_SLOT0_INSTR_MASK) | 
				(actual_instruction << 5); 
	ia64_psr(regs)->ss = 1; /* enable single-step */
	ia64_psr(regs)->i  = 0; /* disable interrupts */
}

static void kprobe_trap_handler(struct unw_frame_info *info, void *arg)
{
	struct kprobe_trap_data *data = arg;
	struct pt_regs *regs = data->regs;
	struct kprobe *p;
	unsigned long *addr = (unsigned long *)(regs->cr_iip);
		
	/* We're in an interrupt, but this is clear and BUG()-safe. */
	/* Check we're not actually recursing */
	if (kprobe_running()) {
		/* We *are* holding lock here, so this is safe.
		   Disarm the probe we just hit, and ignore it. */
		p = get_kprobe(addr);
		if (p) {
			disarm_kprobe(p, regs);
			data->return_val = 1;
		}
		/* If it's not ours, can't be delete race, (we hold lock). */
		goto no_kprobe;
	}

	lock_kprobes();
	p = get_kprobe(addr); 
	if (!p) {
		unlock_kprobes();
		/* Unregistered (on another cpu) after this hit?  Ignore */
		if (*addr != KPROBE_INSTR_BREAKPOINT)
			data->return_val = 1;
		/* Not one of ours: let kernel handle it */
		goto no_kprobe;
	}

	kprobe_status = KPROBE_HIT_ACTIVE;
	current_kprobe = p;
	kprobe_saved_eflags = kprobe_old_eflags 
		= regs->cr_ipsr & (IA64_PSR_SS | IA64_PSR_I);
	if(is_IF_modifier(p->insn, regs, info->sw))
		kprobe_saved_eflags &= ~IA64_PSR_I;

	p->pre_handler(p, regs, info->sw);

	prepare_singlestep(p, regs);
	//disarm_kprobe(p, regs);
	kprobe_status = KPROBE_HIT_SS;
	data->return_val = 1;
	return;

	if (p->post_handler)
		p->post_handler(p, regs, 0);
	unlock_kprobes();
	preempt_enable_no_resched();
	data->return_val = 1;
	return ;

no_kprobe:
	preempt_enable_no_resched();
	data->return_val = 0;
	return ;
}

/*
 * Interrupts are disabled on entry as trap3 is an interrupt gate and they
 * remain disabled thorough out this function.
 */
int kprobe_handler(struct pt_regs *regs, unsigned long vector)
{
	struct kprobe_trap_data data;
	data.return_val = 0;
	data.regs = regs;		
	if (vector == KPROBE_BREAK_NUM) 
		unw_init_running(kprobe_trap_handler, &data);
	return(data.return_val);	
}

void kp_set_gr(unsigned short regnum, unsigned long val, 
			struct pt_regs *regs,  struct switch_stack *sw)
{
	unsigned long sof = 0, sol, soo, bsp, ip, cfm;
	unsigned long *krbs, num_regs_on_krbs, *bspstore, *urbs_end;
	char nat = 0;
	unsigned long *addr;
	struct unw_frame_info info;

	if (regnum < 32) {
		/* gr0-gr31, Static General Registers */
		switch(regnum) {
			case 0:  /* r0, read only, always 0 */
				/* Shouldn't get here */
				break;
			case 1:  /* r1, gp */
			case 2:  /* r2 */
			case 3:  /* r3 */
				addr = &regs->r1 + (regnum - 1);
				*addr = val;
				break;
			case 4:  /* r4 */
			case 5:  /* r5 */
			case 6:  /* r6 */
			case 7:  /* r7 */
				addr = &sw->r4 + (regnum - 4);
				*addr = val;
				break;
			case 8:  /* r8, ret0 */
			case 9:  /* r9, ret1 */
			case 10: /* r10, ret2 */
			case 11: /* r11, ret3 */
				addr = &regs->r8 + (regnum - 8);
				*addr = val;
				break;
			case 12: /* r12, sp */
			case 13: /* r13 */
			case 14: /* r14 */
			case 15: /* r15 */
				addr = &regs->r12 + (regnum - 12);
				*addr = val;
				break;
			case 16:  /* r16 */
			case 17:  /* r17 */
			case 18:  /* r18 */
			case 19:  /* r19 */
			case 20:  /* r20 */
			case 21:  /* r21 */
			case 22:  /* r22 */
			case 23:  /* r23 */
			case 24:  /* r24 */
			case 25:  /* r25 */
			case 26:  /* r26 */
			case 27:  /* r27 */
			case 28:  /* r28 */
			case 29:  /* r29 */
			case 30:  /* r30 */
			case 31:  /* r31 */
				addr = &regs->r16 + (regnum - 16);
				*addr = val;
				break;
			default:
				/* Shouldn't get here */
				break;
		}
	} else {
		if (regs == ia64_task_regs(current)) {
			/*
			 * - Probe point in kernel code.
			 * - RPN instruction: 'push u' (user context).
			 * - Some stacked GR values are on the kernel
			 *   register backing store and some are on the user
			 *   register backing store.
			 *
			 * - This code appears to successfully obtain the stacked
			 *   GR values for the frame corresponding to the user
			 *   function that made a system call.  The code may need
			 *   to be updated to correctly handle other 
			 *   (non system call) kernel entries.
			 */
			krbs = (unsigned long *) current + IA64_RBS_OFFSET/8;
			sof = regs->ar_pfs & 0x7f;
			sol = (regs->ar_pfs & 0x3f80) >> 7;
			soo = sof - sol;

			/* unwind to return function and grab it's bsp */
			unw_init_frame_info(&info, current, sw);
			do {
				/* nothing */
			} while (unw_unwind(&info) >= 0);
			unw_get_bsp(&info, &bsp);

			/* The bsp for the return function points to out0 for the
			 * currently executing user application function so we
			 * need to add the number of user application function output
			 * registers (soo) to obtain a correct count of the number of
			 * registers on the kernel register backing store.
			 */
			num_regs_on_krbs = ia64_rse_num_regs(krbs, (unsigned long *)bsp) + soo;
			addr = ia64_rse_skip_regs((unsigned long *) bsp, soo - sof + (regnum - 32));
			if (addr >= krbs) {
				/* register value is on the krbs at address addr */
				if ((regnum - 32) < sof)
					*addr = val;
			} else {
				/* register value is on the urbs */
				bspstore = (unsigned long *)regs->ar_bspstore;
				urbs_end = ia64_rse_skip_regs(bspstore, num_regs_on_krbs);
				bsp = (unsigned long) ia64_rse_skip_regs(urbs_end, -sof);
				addr = ia64_rse_skip_regs((unsigned long *) bsp, regnum - 32);
				ia64_poke(current, sw, (unsigned long) urbs_end, (unsigned long) addr, (long) val);
			}
		} else {
			/* 
			 * - Probe point in kernel code.
			 * - RPN instruction: 'push r' (current context).
			 * - All stacked GR values are on kernel backing store.
			 *   We unwind to the frame for the function where the break
			 *   instruction is located and set register value using
			 *   the standard kernel unwind API.
			 */
			unw_init_frame_info(&info, current, sw);
			do {
				unw_get_ip(&info, &ip);
				if (ip == regs->cr_iip) {
					unw_get_cfm(&info, &cfm);
					sof = cfm & 0x7f;
					break;
				}
			} while (unw_unwind(&info) >= 0);
			if ((regnum - 32) < sof)
				unw_set_gr (&info, regnum, val, nat);
		}
	}
}


/*
 * If the original instruction is move from PSR, it would have not
 * have executed correctly when we single-stepped as we have modified
 * PSR.i to keep interrupts disabled while single_stepping.  We need
 * to fix it.
 */
static inline void
resume_execution(struct kprobe *p, struct pt_regs *regs, 
				struct switch_stack *sw)
{
	unsigned long instr = 0;
	
	memcpy(&instr, p->insn, MAX_INSN_SIZE);
	if (IA64_MAJ_OPCODE(instr) == 1 &&
	IA64_M36_X3(instr) == 0 &&
	IA64_M36_X6(instr) == 0x25) {
		/* mov from psr.l */
		unsigned long regnum = IA64_M36_R1(instr);
		unsigned long val;

		/* Get incorrect PSR value that was just copied to GR */
		val = kp_get_gr(regnum, regs, sw);
		/* 
		 *  At this point the PSR value contained in 'val' has:
		 *   - PSR.i set to 0 because we set it to this value
		 *     prior to the single step
		 *   - PSR.ss set to 0 because the move from PSR 
		 *     instruction that was just executed always 
		 *     reads PSR.ss as 0
		 */

		/* 
		 *  Adjust the PSR value:
		 *   - setting PSR.i to it's original state 
		 *   - leaving PSR.ss set to 0 regardless of it's
		 *     original state since move from PSR always
		 *     reads PSR.ss as 0 
		 */
		val |= (kprobe_old_eflags & ~IA64_PSR_SS); 

		/* Set GR to corrected PSR value */
		kp_set_gr(regnum, val, regs, sw);
	}
	return;
}

/*
 * A return value of 0 means that this single step trap is not
 * handled by us.  Any other return value indicates otherwise.
 */
static void do_kprobe_singlestep(struct unw_frame_info *info, void *arg)
{
	struct kprobe_trap_data *data = arg;
	struct pt_regs *regs = data->regs;

	if (!kprobe_running()) {
		data->return_val = 0;
		return;
	}

	if (current_kprobe->post_handler)
		current_kprobe->post_handler(current_kprobe, regs, 0);
	resume_execution(current_kprobe, regs, info->sw);
	regs->cr_ipsr |= kprobe_saved_eflags;
	arch_insert_breakpoint(current_kprobe);

	unlock_kprobes();
	preempt_enable_no_resched();

	/*
	 * if somebody else is singlestepping across a probe point, the psr
	 * single step enable bit will be set, in which case, continue the
	 * remaining processing of do_debug, as if this is not a probe hit.
	 */
	ia64_psr(regs)->ss = 0;
	data->return_val = 1;
	return;
}

int post_kprobe_handler(struct pt_regs *regs, unsigned long vector)
{
	struct kprobe_trap_data data;
	
	data.return_val = 0; 
	data.regs = regs;
	if (vector == 36) 
		unw_init_running(do_kprobe_singlestep, &data);
	return(data.return_val);
}

static void do_kprobe_fault(struct unw_frame_info *info, void *arg)
{
	struct kprobe_trap_data *data = (struct kprobe_trap_data *)arg;
	struct pt_regs *regs=data->regs;
	if (current_kprobe->fault_handler
		&& current_kprobe->fault_handler(current_kprobe, regs, data->trapnr)) {
		data->return_val=1;
		return;
	}

	if (kprobe_status & KPROBE_HIT_SS) {
		resume_execution(current_kprobe, regs, info->sw);
		regs->cr_ipsr |= kprobe_old_eflags;

		unlock_kprobes();
		preempt_enable_no_resched();
	}
	data->return_val=0;
	return;
}

/* Interrupts disabled, kprobe_lock held. */
int kprobe_fault_handler(struct pt_regs *regs, int trapnr)
{
	struct kprobe_trap_data data;
	
	data.return_val = 0; 
	data.regs = regs;
	data.trapnr = trapnr;
	unw_init_running(do_kprobe_fault, &data);
	return (data.return_val);
}
EXPORT_SYMBOL_GPL(kp_get_gr);
EXPORT_SYMBOL_GPL(kp_set_gr);

