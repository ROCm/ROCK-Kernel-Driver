/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 2000 by Ralf Baechle and others.
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/mman.h>
#include <linux/sys.h>
#include <linux/user.h>
#include <linux/a.out.h>

#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/mipsregs.h>
#include <asm/processor.h>
#include <asm/stackframe.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/elf.h>
#include <asm/isadep.h>

void cpu_idle(void)
{
	/* endless idle loop with no priority at all */
	current->nice = 20;
	current->counter = -100;
	init_idle();

	while (1) {
		while (!current->need_resched)
			if (cpu_wait)
				(*cpu_wait)();
		schedule();
		check_pgt_cache();
	}
}

struct task_struct *last_task_used_math = NULL;

asmlinkage void ret_from_fork(void);

void exit_thread(void)
{
	/* Forget lazy fpu state */
	if (last_task_used_math == current) {
		set_cp0_status(ST0_CU1);
		__asm__ __volatile__("cfc1\t$0,$31");
		last_task_used_math = NULL;
	}
}

void flush_thread(void)
{
	/* Forget lazy fpu state */
	if (last_task_used_math == current) {
		set_cp0_status(ST0_CU1);
		__asm__ __volatile__("cfc1\t$0,$31");
		last_task_used_math = NULL;
	}
}

int copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
		 unsigned long unused,
                 struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;
	long childksp;
	extern void save_fp(void*);

	childksp = (unsigned long)p + KERNEL_STACK_SIZE - 32;

	if (last_task_used_math == current)
		if (mips_cpu.options & MIPS_CPU_FPU) {
			set_cp0_status(ST0_CU1);
			save_fp(p);
		}
	/* set up new TSS. */
	childregs = (struct pt_regs *) childksp - 1;
	*childregs = *regs;
	childregs->regs[7] = 0;	/* Clear error flag */
	if(current->personality == PER_LINUX) {
		childregs->regs[2] = 0;	/* Child gets zero as return value */
		regs->regs[2] = p->pid;
	} else {
		/* Under IRIX things are a little different. */
		childregs->regs[2] = 0;
		childregs->regs[3] = 1;
		regs->regs[2] = p->pid;
		regs->regs[3] = 0;
	}
	if (childregs->cp0_status & ST0_CU0) {
		childregs->regs[28] = (unsigned long) p;
		childregs->regs[29] = childksp;
		p->thread.current_ds = KERNEL_DS;
	} else {
		childregs->regs[29] = usp;
		p->thread.current_ds = USER_DS;
	}
	p->thread.reg29 = (unsigned long) childregs;
	p->thread.reg31 = (unsigned long) ret_from_fork;

	/*
	 * New tasks loose permission to use the fpu. This accelerates context
	 * switching for most programs since they don't use the fpu.
	 */
	p->thread.cp0_status = read_32bit_cp0_register(CP0_STATUS) &
                            ~(ST0_CU2|ST0_CU1|KU_MASK);
	childregs->cp0_status &= ~(ST0_CU2|ST0_CU1);

	return 0;
}

/* Fill in the fpu structure for a core dump.. */
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *r)
{
	/* We actually store the FPU info in the task->thread
	 * area.
	 */
	if(regs->cp0_status & ST0_CU1) {
		memcpy(r, &current->thread.fpu, sizeof(current->thread.fpu));
		return 1;
	}
	return 0; /* Task didn't use the fpu at all. */
}

/* Fill in the user structure for a core dump.. */
void dump_thread(struct pt_regs *regs, struct user *dump)
{
	dump->magic = CMAGIC;
	dump->start_code  = current->mm->start_code;
	dump->start_data  = current->mm->start_data;
	dump->start_stack = regs->regs[29] & ~(PAGE_SIZE - 1);
	dump->u_tsize = (current->mm->end_code - dump->start_code) >> PAGE_SHIFT;
	dump->u_dsize = (current->mm->brk + (PAGE_SIZE - 1) - dump->start_data) >> PAGE_SHIFT;
	dump->u_ssize =
		(current->mm->start_stack - dump->start_stack + PAGE_SIZE - 1) >> PAGE_SHIFT;
	memcpy(&dump->regs[0], regs, sizeof(struct pt_regs));
	memcpy(&dump->regs[EF_SIZE/4], &current->thread.fpu, sizeof(current->thread.fpu));
}

/*
 * Create a kernel thread
 */
int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	long retval;

	__asm__ __volatile__(
		".set noreorder               \n"
		"    move    $6,$sp           \n"
		"    move    $4,%5            \n"
		"    li      $2,%1            \n"
		"    syscall                  \n"
		"    beq     $6,$sp,1f        \n"
		"    subu    $sp,32           \n"	/* delay slot */
		"    jalr    %4               \n"
		"    move    $4,%3            \n"	/* delay slot */
		"    move    $4,$2            \n"
		"    li      $2,%2            \n"
		"    syscall                  \n"
		"1:  addiu   $sp,32           \n"
		"    move    %0,$2            \n"
		".set reorder"
		:"=r" (retval)
		:"i" (__NR_clone), "i" (__NR_exit),
		 "r" (arg), "r" (fn),
		 "r" (flags | CLONE_VM)
		 /*
		  * The called subroutine might have destroyed any of the
		  * at, result, argument or temporary registers ...
		  */
		:"$1", "$2", "$3", "$4", "$5", "$6", "$7", "$8",
		 "$9","$10","$11","$12","$13","$14","$15","$24","$25");

	return retval;
}

/*
 * These bracket the sleeping functions..
 */
extern void scheduling_functions_start_here(void);
extern void scheduling_functions_end_here(void);
#define first_sched	((unsigned long) scheduling_functions_start_here)
#define last_sched	((unsigned long) scheduling_functions_end_here)

/* get_wchan - a maintenance nightmare ...  */
unsigned long get_wchan(struct task_struct *p)
{
	unsigned long frame, pc;

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	pc = thread_saved_pc(&p->thread);
	if (pc < first_sched || pc >= last_sched) {
		return pc;
	}

	if (pc >= (unsigned long) sleep_on_timeout)
		goto schedule_timeout_caller;
	if (pc >= (unsigned long) sleep_on)
		goto schedule_caller;
	if (pc >= (unsigned long) interruptible_sleep_on_timeout)
		goto schedule_timeout_caller;
	if (pc >= (unsigned long)interruptible_sleep_on)
		goto schedule_caller;
	goto schedule_timeout_caller;

schedule_caller:
	frame = ((unsigned long *)p->thread.reg30)[9];
	pc    = ((unsigned long *)frame)[11];
	return pc;

schedule_timeout_caller:
	/* Must be schedule_timeout ...  */
	pc    = ((unsigned long *)p->thread.reg30)[10];
	frame = ((unsigned long *)p->thread.reg30)[9];

	/* The schedule_timeout frame ...  */
	pc    = ((unsigned long *)frame)[14];
	frame = ((unsigned long *)frame)[13];

	if (pc >= first_sched && pc < last_sched) {
		/* schedule_timeout called by interruptible_sleep_on_timeout */
		pc    = ((unsigned long *)frame)[11];
		frame = ((unsigned long *)frame)[10];
	}

	return pc;
}
