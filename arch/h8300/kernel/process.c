/*
 *  linux/arch/h8300/kernel/process.c
 *
 * Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 *  Based on:
 *
 *  linux/arch/m68knommu/kernel/process.c
 *
 *  Copyright (C) 1998  D. Jeff Dionne <jeff@ryeham.ee.ryerson.ca>,
 *                      Kenneth Albanowski <kjahds@kjahds.com>,
 *                      The Silver Hammer Group, Ltd.
 *
 *  linux/arch/m68k/kernel/process.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 *
 *  68060 fixes by Jesper Skov
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/traps.h>
#include <asm/setup.h>
#include <asm/pgtable.h>

asmlinkage void ret_from_exception(void);

/*
 * The idle loop on an H8/300..
 */
#if !defined(CONFIG_H8300H_SIM)
void default_idle(void)
{
	while(1) {
		if (need_resched()) {
			sti();
			__asm__("sleep");
			cli();
		}
		schedule();
	}
}
#else
void default_idle(void)
{
	while(1) {
		if (need_resched())
			schedule();
	}
}
#endif
void (*idle)(void) = default_idle;

/*
 * The idle thread. There's no useful work to be
 * done, so just try to conserve power and have a
 * low exit latency (ie sit in a loop waiting for
 * somebody to say that they'd like to reschedule)
 */
void cpu_idle(void)
{
	idle();
}

void machine_restart(char * __unused)
{
	cli();
	__asm__("jmp @@0"); 
}

void machine_halt(void)
{
	cli();
	__asm__("sleep");
	for (;;);
}

void machine_power_off(void)
{
	cli();
	__asm__("sleep");
	for (;;);
}

void show_regs(struct pt_regs * regs)
{
	printk("\n");
	printk("PC: %08lx  Status: %02x\n",
	       regs->pc, regs->ccr);
	printk("ORIG_ER0: %08lx ER0: %08lx ER1: %08lx\n",
	       regs->orig_er0, regs->er0, regs->er1);
	printk("ER2: %08lx ER3: %08lx\n",
	       regs->er2, regs->er3);
	if (!(regs->ccr & 0x10))
		printk("USP: %08lx\n", rdusp());
}

/*
 * Create a kernel thread
 */
int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	long retval;
	register long clone_arg asm("er1");
	mm_segment_t fs;

	fs = get_fs();
	set_fs (KERNEL_DS);
	clone_arg = flags | CLONE_VM;

	__asm__ __volatile__ (
			"mov.l	sp, er2\n\t"
			"mov.l  %1,er0\n\t"
			"mov.l  %5,er1\n\t"
			"trapa	#0\n\t"
			"cmp.l	sp, er2\n\t"
			"beq	1f\n\t"
			"mov.l	%3, er0\n\t"
			"jsr	@%4\n\t"
			"mov.l	%2, er0\n\t"
			"trapa	#0\n"
			"1:\n\t"
			"mov.l er0,%0"
		: "=r" (retval)
		: "i" (__NR_clone),
		  "i" (__NR_exit),
		  "r" (arg),
		  "r" (fn),
		  "r" (clone_arg)
		: "cc", "er0", "er1", "er2", "er3");

	set_fs (fs);
	return retval;
}

void flush_thread(void)
{
}

/*
 * "h8300_fork()".. By the time we get here, the
 * non-volatile registers have also been saved on the
 * stack. We do some ugly pointer stuff here.. (see
 * also copy_thread)
 */

asmlinkage int h8300_fork(struct pt_regs *regs)
{
	return -EINVAL;
}

asmlinkage int h8300_vfork(struct pt_regs *regs)
{
	struct task_struct *p;
	p = do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, rdusp(), regs, 0, NULL, NULL);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;
}

asmlinkage int h8300_clone(struct pt_regs *regs)
{
	unsigned long clone_flags;
	unsigned long newsp;
	struct task_struct *p;

	/* syscall2 puts clone_flags in er1 and usp in er2 */
	clone_flags = regs->er1;
	newsp = regs->er2;
	if (!newsp)
		newsp  = rdusp();
	p = do_fork(clone_flags & ~CLONE_IDLETASK, newsp, regs, 0, NULL, NULL);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;

}

int copy_thread(int nr, unsigned long clone_flags,
                unsigned long usp, unsigned long topstk,
		 struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;
	struct switch_stack * childstack, *stack;
	unsigned long stack_offset, *retp;

	stack_offset = KTHREAD_SIZE - sizeof(struct pt_regs);
	childregs = ((struct pt_regs *) (THREAD_SIZE + (unsigned long) p->thread_info)) - 1;

	*childregs = *regs;

	retp = (unsigned long *) regs-2;
	stack = ((struct switch_stack *) retp) - 1;

	childstack = ((struct switch_stack *) childregs) - 1;
	*childstack = *stack;
	childregs->er0 = 0;
	childstack->retpc = (unsigned long) ret_from_exception;

	p->thread.usp = usp;
	p->thread.ksp = (unsigned long)childstack;
	p->thread.vfork_ret = 0;

	return 0;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
	struct switch_stack *sw;

/* changed the size calculations - should hopefully work better. lbt */
	dump->magic = CMAGIC;
	dump->start_code = 0;
	dump->start_stack = rdusp() & ~(PAGE_SIZE - 1);
	dump->u_tsize = ((unsigned long) current->mm->end_code) >> PAGE_SHIFT;
	dump->u_dsize = ((unsigned long) (current->mm->brk +
					  (PAGE_SIZE-1))) >> PAGE_SHIFT;
	dump->u_dsize -= dump->u_tsize;
	dump->u_ssize = 0;

	dump->u_ar0 = (struct user_regs_struct *)(((int)(&dump->regs)) -((int)(dump)));
	sw = ((struct switch_stack *)regs) - 1;
	dump->regs.er0 = regs->er0;
	dump->regs.er1 = regs->er1;
	dump->regs.er2 = regs->er2;
	dump->regs.er3 = regs->er3;
	dump->regs.er4 = sw->er4;
	dump->regs.er5 = sw->er5;
	dump->regs.er6 = sw->er6;
	dump->regs.orig_er0 = regs->orig_er0;
	dump->regs.ccr = regs->ccr;
	dump->regs.pc  = regs->pc;
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(char *name, char **argv, char **envp,int dummy,...)
{
	int error;
	char * filename;
	struct pt_regs *regs = (struct pt_regs *) ((unsigned char *)&dummy+4);

	lock_kernel();
	filename = getname(name);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, argv, envp, regs);
	putname(filename);
out:
	unlock_kernel();
	return error;
}

/*
 * These bracket the sleeping functions..
 */
extern void scheduling_functions_start_here(void);
extern void scheduling_functions_end_here(void);
#define first_sched	((unsigned long) scheduling_functions_start_here)
#define last_sched	((unsigned long) scheduling_functions_end_here)

unsigned long thread_saved_pc(struct task_struct *tsk)
{
	struct switch_stack *sw = (struct switch_stack *)(tsk->thread.ksp);

	/* Check whether the thread is blocked in resume() */
	if (sw->retpc > (unsigned long)scheduling_functions_start_here &&
	    sw->retpc < (unsigned long)scheduling_functions_end_here)
		return ((unsigned long *)sw->er6)[1];
	else
		return sw->retpc;
}

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long fp, pc;
	unsigned long stack_page;
	int count = 0;
	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	stack_page = (unsigned long)p;
	fp = ((struct switch_stack *)p->thread.ksp)->er6;
	do {
		if (fp < stack_page+sizeof(struct task_struct) ||
		    fp >= 8184+stack_page)
			return 0;
		pc = ((unsigned long *)fp)[1];
		/* FIXME: This depends on the order of these functions. */
		if (pc < first_sched || pc >= last_sched)
			return pc;
		fp = *(unsigned long *) fp;
	} while (count++ < 16);
	return 0;
}
