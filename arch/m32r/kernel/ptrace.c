/*
 * linux/arch/sh/kernel/ptrace.c
 *
 * Copyright (C) 2002  Hirokazu Takata, Takeo Takahashi
 *
 * Original x86 implementation:
 *	By Ross Biro 1/23/92
 *	edited by Linus Torvalds
 *
 * Some code taken from sh version:
 *   Copyright (C) 1999, 2000  Kaz Kojima & Niibe Yutaka
 *
 */

/* $Id$ */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/string.h>

#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/mmu_context.h>

#define DEBUG_PTRACE	0

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * This routine will get a word off of the process kernel stack.
 */
static __inline__ unsigned long int get_stack_long(struct task_struct *task,
	int offset)
{

	unsigned char *stack;

	stack = (unsigned char *)(task->thread_info) + THREAD_SIZE
		- sizeof(struct pt_regs);
	stack += offset;

	return *((unsigned long *)stack);
}

/*
 * This routine will put a word on the process kernel stack.
 */
static __inline__ int put_stack_long(struct task_struct *task, int offset,
	unsigned long data)
{
	unsigned char *stack;

	stack = (unsigned char *)(task->thread_info) + THREAD_SIZE
		- sizeof(struct pt_regs);
	stack += offset;
	*((unsigned long *)stack) = data;

	return 0;
}

static int reg_offset[] = {
	(4 * PT_R0), (4 * PT_R1), (4 * PT_R2), (4 * PT_R3),
	(4 * PT_R4), (4 * PT_R5), (4 * PT_R6), (4 * PT_R7),
	(4 * PT_R8), (4 * PT_R9), (4 * PT_R10), (4 * PT_R11),
	(4 * PT_R12), (4 * PT_FP), (4 * PT_LR), (4 * PT_SPU),
};

static __inline__ int
check_condition_bit(struct task_struct *child)
{
	return (int)((get_stack_long(child, (4 * PT_PSW)) >> 8) & 1);
}

static int
check_condition_src(unsigned long op, unsigned long regno1, unsigned long regno2, struct task_struct *child)
{
	unsigned long reg1, reg2;

	reg2 = get_stack_long(child, reg_offset[regno2]);

	switch (op) {
	case 0x0: /* BEQ */
		reg1 = get_stack_long(child, reg_offset[regno1]);
		return reg1 == reg2;
	case 0x1: /* BNE */
		reg1 = get_stack_long(child, reg_offset[regno1]);
		return reg1 != reg2;
	case 0x8: /* BEQZ */
		return reg2 == 0;
	case 0x9: /* BNEZ */
		return reg2 != 0;
	case 0xa: /* BLTZ */
		return (int)reg2 < 0;
	case 0xb: /* BGEZ */
		return (int)reg2 >= 0;
	case 0xc: /* BLEZ */
		return (int)reg2 <= 0;
	case 0xd: /* BGTZ */
		return (int)reg2 > 0;
	default:
		/* never reached */
		return 0;
	}
}

static void
compute_next_pc_for_16bit_insn(unsigned long insn, unsigned long pc,
	unsigned long *next_pc, struct task_struct *child)
{
	unsigned long op, op2, op3;
	unsigned long disp;
	unsigned long regno;
	int parallel = 0;

	if (insn & 0x00008000)
		parallel = 1;
	if (pc & 3)
		insn &= 0x7fff;	/* right slot */
	else
		insn >>= 16;	/* left slot */

	op = (insn >> 12) & 0xf;
	op2 = (insn >> 8) & 0xf;
	op3 = (insn >> 4) & 0xf;

	if (op == 0x7) {
		switch (op2) {
		case 0xd: /* BNC */
		case 0x9: /* BNCL */
			if (!check_condition_bit(child)) {
				disp = (long)(insn << 24) >> 22;
				*next_pc = (pc & ~0x3) + disp;
				return;
			}
			break;
		case 0x8: /* BCL */
		case 0xc: /* BC */
			if (check_condition_bit(child)) {
				disp = (long)(insn << 24) >> 22;
				*next_pc = (pc & ~0x3) + disp;
				return;
			}
			break;
		case 0xe: /* BL */
		case 0xf: /* BRA */
			disp = (long)(insn << 24) >> 22;
			*next_pc = (pc & ~0x3) + disp;
			return;
			break;
		}
	} else if (op == 0x1) {
		switch (op2) {
		case 0x0:
			if (op3 == 0xf) { /* TRAP */
#if 1
				/* pass through */
#else
 				/* kernel space is not allowed as next_pc */
				unsigned long evb;
				unsigned long trapno;
				trapno = insn & 0xf;
				__asm__ __volatile__ (
					"mvfc %0, cr5\n"
		 			:"=r"(evb)
		 			:
				);
				*next_pc = evb + (trapno << 2);
				return;
#endif
			} else if (op3 == 0xd) { /* RTE */
				*next_pc = get_stack_long(child, (4 * PT_BPC));
				return;
			}
			break;
		case 0xc: /* JC */
			if (op3 == 0xc && check_condition_bit(child)) {
				regno = insn & 0xf;
				*next_pc = get_stack_long(child, reg_offset[regno]);
				return;
			}
			break;
		case 0xd: /* JNC */
			if (op3 == 0xc && !check_condition_bit(child)) {
				regno = insn & 0xf;
				*next_pc = get_stack_long(child, reg_offset[regno]);
				return;
			}
			break;
		case 0xe: /* JL */
		case 0xf: /* JMP */
			if (op3 == 0xc) { /* JMP */
				regno = insn & 0xf;
				*next_pc = get_stack_long(child, reg_offset[regno]);
				return;
			}
			break;
		}
	}
	if (parallel)
		*next_pc = pc + 4;
	else
		*next_pc = pc + 2;
}

static void
compute_next_pc_for_32bit_insn(unsigned long insn, unsigned long pc,
	unsigned long *next_pc, struct task_struct *child)
{
	unsigned long op;
	unsigned long op2;
	unsigned long disp;
	unsigned long regno1, regno2;

	op = (insn >> 28) & 0xf;
	if (op == 0xf) { 	/* branch 24-bit relative */
		op2 = (insn >> 24) & 0xf;
		switch (op2) {
		case 0xd:	/* BNC */
		case 0x9:	/* BNCL */
			if (!check_condition_bit(child)) {
				disp = (long)(insn << 8) >> 6;
				*next_pc = (pc & ~0x3) + disp;
				return;
			}
			break;
		case 0x8:	/* BCL */
		case 0xc:	/* BC */
			if (check_condition_bit(child)) {
				disp = (long)(insn << 8) >> 6;
				*next_pc = (pc & ~0x3) + disp;
				return;
			}
			break;
		case 0xe:	/* BL */
		case 0xf:	/* BRA */
			disp = (long)(insn << 8) >> 6;
			*next_pc = (pc & ~0x3) + disp;
			return;
		}
	} else if (op == 0xb) { /* branch 16-bit relative */
		op2 = (insn >> 20) & 0xf;
		switch (op2) {
		case 0x0: /* BEQ */
		case 0x1: /* BNE */
		case 0x8: /* BEQZ */
		case 0x9: /* BNEZ */
		case 0xa: /* BLTZ */
		case 0xb: /* BGEZ */
		case 0xc: /* BLEZ */
		case 0xd: /* BGTZ */
			regno1 = ((insn >> 24) & 0xf);
			regno2 = ((insn >> 16) & 0xf);
			if (check_condition_src(op2, regno1, regno2, child)) {
				disp = (long)(insn << 16) >> 14;
				*next_pc = (pc & ~0x3) + disp;
				return;
			}
			break;
		}
	}
	*next_pc = pc + 4;
}

static __inline__ void
compute_next_pc(unsigned long insn, unsigned long pc,
	unsigned long *next_pc, struct task_struct *child)
{
	if (insn & 0x80000000)
		compute_next_pc_for_32bit_insn(insn, pc, next_pc, child);
	else
		compute_next_pc_for_16bit_insn(insn, pc, next_pc, child);
}

static int
register_debug_trap(struct task_struct *child, unsigned long next_pc,
	unsigned long next_insn, unsigned long *code)
{
	struct debug_trap *p = &child->thread.debug_trap;
	unsigned long addr = next_pc & ~3;

	if (p->nr_trap != 0) {
		printk("kernel BUG at %s %d: p->nr_trap = %d\n",
					__FILE__, __LINE__, p->nr_trap);
		return -1;
	}
	p->addr = addr;
	p->insn = next_insn;
	p->nr_trap++;
	if (next_pc & 3) {
		*code = (next_insn & 0xffff0000) | 0x10f1;
		/* xxx --> TRAP1 */
	} else {
		if ((next_insn & 0x80000000) || (next_insn & 0x8000)) {
			*code = 0x10f17000;
			/* TRAP1 --> NOP */
		} else {
			*code = (next_insn & 0xffff) | 0x10f10000;
			/* TRAP1 --> xxx */
		}
	}
	return 0;
}

int withdraw_debug_trap_for_signal(struct task_struct *child)
{
	struct debug_trap *p = &child->thread.debug_trap;
	int nr_trap = p->nr_trap;

	if (nr_trap) {
		access_process_vm(child, p->addr, &p->insn, sizeof(p->insn), 1);
		p->nr_trap = 0;
		p->addr = 0;
		p->insn = 0;
	}
	return nr_trap;
}

static int
unregister_debug_trap(struct task_struct *child, unsigned long addr, unsigned long *code)
{
	struct debug_trap *p = &child->thread.debug_trap;

	if (p->nr_trap != 1 || p->addr != addr) {
		/* The trap may be requested from debugger.
		 * ptrace should do nothing in this case.
		 */
		return 0;
	}
	*code = p->insn;
	p->insn = 0;
	p->addr = 0;
	p->nr_trap--;
	return 1;
}

static void
unregister_all_debug_traps(struct task_struct *child)
{
	struct debug_trap *p = &child->thread.debug_trap;

	if (p->nr_trap) {
		access_process_vm(child, p->addr, &p->insn, sizeof(p->insn), 1);
		p->addr = 0;
		p->insn = 0;
		p->nr_trap = 0;
	}
}

static void
invalidate_cache(void)
{
#if defined(CONFIG_CHIP_M32700) || defined(CONFIG_CHIP_OPSP)

	_flush_cache_copyback_all();

#else	/* ! CONFIG_CHIP_M32700 */

	/* Invalidate cache */
	__asm__ __volatile__ (
                "ldi    r0, #-1					\n\t"
                "ldi    r1, #0					\n\t"
                "stb    r1, @r0		; cache off		\n\t"
                ";						\n\t"
                "ldi    r0, #-2					\n\t"
                "ldi    r1, #1					\n\t"
                "stb    r1, @r0		; cache invalidate	\n\t"
                ".fillinsn					\n"
                "0:						\n\t"
                "ldb    r1, @r0		; invalidate check	\n\t"
                "bnez   r1, 0b					\n\t"
                ";						\n\t"
                "ldi    r0, #-1					\n\t"
                "ldi    r1, #1					\n\t"
                "stb    r1, @r0		; cache on		\n\t"
		: : : "r0", "r1", "memory"
	);
	/* FIXME: copying-back d-cache and invalidating i-cache are needed.
	 */
#endif	/* CONFIG_CHIP_M32700 */
}

/* Embed a debug trap (TRAP1) code */
static int
embed_debug_trap(struct task_struct *child, unsigned long next_pc)
{
	unsigned long next_insn, code;
	unsigned long addr = next_pc & ~3;

	if (access_process_vm(child, addr, &next_insn, sizeof(next_insn), 0)
	    != sizeof(next_insn)) {
		return -1; /* error */
	}

	/* Set a trap code. */
	if (register_debug_trap(child, next_pc, next_insn, &code)) {
		return -1; /* error */
	}
	if (access_process_vm(child, addr, &code, sizeof(code), 1)
	    != sizeof(code)) {
		return -1; /* error */
	}
	return 0; /* success */
}

void
embed_debug_trap_for_signal(struct task_struct *child)
{
	unsigned long next_pc;
	unsigned long pc, insn;
	int ret;

	pc = get_stack_long(child, (4 * PT_BPC));
	ret = access_process_vm(child, pc&~3, &insn, sizeof(insn), 0);
	if (ret != sizeof(insn)) {
		printk("kernel BUG at %s %d: access_process_vm returns %d\n",
					__FILE__, __LINE__, ret);
		return;
	}
	compute_next_pc(insn, pc, &next_pc, child);
	if (next_pc & 0x80000000) {
		printk("kernel BUG at %s %d: next_pc = 0x%08x\n",
				__FILE__, __LINE__, (int)next_pc);
		return;
	}
	if (embed_debug_trap(child, next_pc)) {
		printk("kernel BUG at %s %d: embed_debug_trap error\n",
					__FILE__, __LINE__);
		return;
	}
	invalidate_cache();
}

void
withdraw_debug_trap(struct pt_regs *regs)
{
	unsigned long addr;
	unsigned long code;

 	addr = (regs->bpc - 2) & ~3;
	regs->bpc -= 2;
	if (unregister_debug_trap(current, addr, &code)) {
	    access_process_vm(current, addr, &code, sizeof(code), 1);
	    invalidate_cache();
	}
}


/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *child)
{
	/* nothing to do.. */
}

static void
init_debug_traps(struct task_struct *child)
{
	struct debug_trap *p = &child->thread.debug_trap;
	p->nr_trap = 0;
	p->addr = 0;
	p->insn = 0;
}

asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	int ret;
#ifndef NO_FPU
	struct user * dummy = NULL;
#endif

	lock_kernel();
	ret = -EPERM;
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->ptrace & PT_PTRACED)
			goto out;
		/* set the ptrace bit in the process flags. */
		current->ptrace |= PT_PTRACED;
		ret = 0;
		goto out;
	}
	ret = -ESRCH;
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	if (child)
		get_task_struct(child);
	read_unlock(&tasklist_lock);
	if (!child)
		goto out;

	ret = -EPERM;
	if (pid == 1)		/* you may not mess with init */
		goto out;

	if (request == PTRACE_ATTACH) {
#if (DEBUG_PTRACE > 1)
printk("ptrace: PTRACE_ATTACH: child:%08lx\n", (unsigned long)child);
#endif
		ret = ptrace_attach(child);
		if (ret == 0)
			init_debug_traps(child);
		goto out_tsk;
	}

	ret = ptrace_check_attach(child, request == PTRACE_KILL);
	if (ret < 0)
		goto out_tsk;

	switch (request) {
	/* when I and D space are separate, these will need to be fixed. */
	case PTRACE_PEEKTEXT: /* read word at location addr. */
	case PTRACE_PEEKDATA: {
		unsigned long tmp;
		int copied;

#if (DEBUG_PTRACE > 1)
printk("ptrace: PTRACE_PEEKTEXT/PEEKDATA: child:%08lx, addr:%08lx\n",
	(unsigned long)child, addr);
#endif
		copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
		ret = -EIO;
		if (copied != sizeof(tmp))
			break;
		ret = put_user(tmp,(unsigned long __user *) data);
		break;
	}

	/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		unsigned long tmp;

#if DEBUG_PTRACE
printk("ptrace: PTRACE_PEEKUSER: child:%08lx, addr:%08lx\n",
	(unsigned long)child, addr);
#endif
		ret = -EIO;
		if ((addr & 3) || addr < 0 ||
		    addr > sizeof(struct user) - 3)
			break;

		switch (addr) {
		case (4 * PT_BPC):
			addr = (4 * PT_BBPC);
			break;
		case (4 * PT_EVB):
			__asm__ __volatile__ (
				"mvfc %0, cr5\n"
		 		:"=r"(tmp)
		 		:
			);
			ret = put_user(tmp, (unsigned long __user *)data);
			goto out_tsk;
			break;
		case (4 * PT_CBR): {
				unsigned long psw;
				psw = get_stack_long(child, (4 * PT_PSW));
				tmp = ((psw >> 8) & 1);
				ret = put_user(tmp, (unsigned long __user *)data);
				goto out_tsk;
			}
			break;
		case (4 * PT_PSW): {
				unsigned long psw, bbpsw;
				psw = get_stack_long(child, (4 * PT_PSW));
				bbpsw = get_stack_long(child, (4 * PT_BBPSW));
				tmp = ((psw >> 8) & 0xff) | ((bbpsw & 0xff) << 8);
				ret = put_user(tmp, (unsigned long __user *)data);
				goto out_tsk;
			}
			break;
		case (4 * PT_PC): {
				unsigned long pc;
				pc = get_stack_long(child, (4 * PT_BPC));
				ret = put_user(pc, (unsigned long __user *)data);
				goto out_tsk;
			}
			break;
		}

		if (addr < sizeof(struct pt_regs))
			tmp = get_stack_long(child, addr);
#ifndef NO_FPU
		else if (addr >= (long) &dummy->fpu &&
			 addr < (long) &dummy->u_fpvalid) {
			if (!child->used_math) {
				if (addr == (long)&dummy->fpu.fpscr)
					tmp = FPSCR_INIT;
				else
					tmp = 0;
			} else
				tmp = ((long *)&child->thread.fpu)
					[(addr - (long)&dummy->fpu) >> 2];
		} else if (addr == (long) &dummy->u_fpvalid)
			tmp = child->used_math;
#endif /* not NO_FPU */
		else
			tmp = 0;
		ret = put_user(tmp, (unsigned long __user *)data);
		break;
	}

	/* when I and D space are separate, this will have to be fixed. */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
#if DEBUG_PTRACE
printk("ptrace: PTRACE_POKETEXT/POKEDATA: child:%08lx, addr:%08lx, data:%08lx\n",
	(unsigned long)child, addr, data);
#endif
		ret = -EIO;
		if (access_process_vm(child, addr, &data, sizeof(data), 1)
		    != sizeof(data)) {
			break;
		}
		ret = 0;
		if (request == PTRACE_POKETEXT) {
			invalidate_cache();
		}
		break;

	case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
#if DEBUG_PTRACE
printk("ptrace: PTRACE_POKEUSR: child:%08lx, addr:%08lx, data:%08lx\n",
	(unsigned long)child, addr, data);
#endif
		ret = -EIO;
		if ((addr & 3) || addr < 0 ||
		    addr > sizeof(struct user) - 3)
			break;

		switch (addr) {
		case (4 * PT_EVB):
		case (4 * PT_BPC):
		case (4 * PT_SPI):
			/* We don't allow to modify evb. */
			ret = 0;
			goto out_tsk;
			break;
		case (4 * PT_PSW):
		case (4 * PT_CBR): {
			/* We allow to modify only cbr in psw */
			unsigned long psw;
			psw = get_stack_long(child, (4 * PT_PSW));
			psw = (psw & ~0x100) | ((data & 1) << 8);
			ret = put_stack_long(child, (4 * PT_PSW), psw);
			goto out_tsk;
			}
			break;
		case (4 * PT_PC):
			addr = (4 * PT_BPC);
			data &= ~1;
			break;
		}

		if (addr < sizeof(struct pt_regs))
			ret = put_stack_long(child, addr, data);
#ifndef NO_FPU
		else if (addr >= (long) &dummy->fpu &&
			 addr < (long) &dummy->u_fpvalid) {
			child->used_math = 1;
			((long *)&child->thread.fpu)
				[(addr - (long)&dummy->fpu) >> 2] = data;
			ret = 0;
		} else if (addr == (long) &dummy->u_fpvalid) {
			child->used_math = data?1:0;
			ret = 0;
		}
#endif /* not NO_FPU */
		break;

	case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
	case PTRACE_CONT: { /* restart after signal. */
#if DEBUG_PTRACE
printk("ptrace: PTRACE_SYSCALL/CONT: child:%08lx, data:%08lx\n",
	(unsigned long)child, data);
#endif
		ret = -EIO;
		if ((unsigned long) data > _NSIG)
			break;
		if (request == PTRACE_SYSCALL)
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		else
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		child->exit_code = data;
		wake_up_process(child);
		ret = 0;
		break;
	}

/*
 * make the child exit.  Best I can do is send it a sigkill.
 * perhaps it should be put in the status that it wants to
 * exit.
 */
	case PTRACE_KILL: {
#if DEBUG_PTRACE
printk("ptrace: PTRACE_KILL: child:%08lx\n", (unsigned long)child);
#endif
		ret = 0;
		unregister_all_debug_traps(child);
		invalidate_cache();
		if (child->state == TASK_ZOMBIE)	/* already dead */
			break;
		child->exit_code = SIGKILL;
		wake_up_process(child);
		break;
	}

	case PTRACE_SINGLESTEP: {  /* set the trap flag. */
		unsigned long next_pc;
		unsigned long pc, insn;

		ret = -EIO;
		if ((unsigned long) data > _NSIG)
			break;
		clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		if ((child->ptrace & PT_DTRACE) == 0) {
			/* Spurious delayed TF traps may occur */
			child->ptrace |= PT_DTRACE;
		}

		/* Compute next pc.  */
		pc = get_stack_long(child, (4 * PT_BPC));

#if DEBUG_PTRACE
printk("ptrace: PTRACE_SINGLESTEP: child:%08lx, pc:%08lx, ",
	(unsigned long)child, pc);
#endif
		if (access_process_vm(child, pc&~3, &insn, sizeof(insn), 0) != sizeof(insn))
			break;

#if DEBUG_PTRACE
printk("(pc&~3):%08lx, insn:%08lx, ", (pc & ~3), insn);
#endif

		compute_next_pc(insn, pc, &next_pc, child);
#if DEBUG_PTRACE
printk("nextpc:%08lx\n", next_pc);
#endif
		if (next_pc & 0x80000000)
			break;

		if (embed_debug_trap(child, next_pc))
			break;

		invalidate_cache();
		child->exit_code = data;
		/* give it a chance to run. */
		wake_up_process(child);
		ret = 0;
		break;
	}

	case PTRACE_DETACH: /* detach a process that was attached. */
#if DEBUG_PTRACE
printk("ptrace: PTRACE_DETACH: child:%08lx, data:%08lx\n",
	(unsigned long)child, data);
#endif
		ret = 0;
		ret = ptrace_detach(child, data);
		break;

	case PTRACE_SETOPTIONS: {
#if DEBUG_PTRACE
printk("ptrace: PTRACE_SETOPTIONS:\n");
#endif
		if (data & PTRACE_O_TRACESYSGOOD)
			child->ptrace |= PT_TRACESYSGOOD;
		else
			child->ptrace &= ~PT_TRACESYSGOOD;
		ret = 0;
		break;
	}

	default:
		ret = -EIO;
		break;
	}
out_tsk:
	put_task_struct(child);
out:
	unlock_kernel();

	return ret;
}

/* notification of system call entry/exit
 * - triggered by current->work.syscall_trace
 */
void do_syscall_trace(void)
{
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;
	if (!(current->ptrace & PT_PTRACED))
		return;
	/* the 0x80 provides a way for the tracing parent to distinguish
	   between a syscall stop and SIGTRAP delivery */
	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
				 ? 0x80 : 0));

	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}

