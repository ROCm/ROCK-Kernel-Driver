/* $Id: process.c,v 1.17 2003/05/27 21:37:11 lethal Exp $
 *
 *  linux/arch/sh/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  SuperH version:  Copyright (C) 1999, 2000  Niibe Yutaka & Kaz Kojima
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <linux/module.h>
#include <linux/unistd.h>
#include <linux/mm.h>
#include <linux/elfcore.h>
#include <linux/slab.h>
#include <linux/a.out.h>
#include <linux/ptrace.h>
#include <linux/platform.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/elf.h>

static int hlt_counter=0;

#define HARD_IDLE_TIMEOUT (HZ / 3)

void disable_hlt(void)
{
	hlt_counter++;
}

EXPORT_SYMBOL(disable_hlt);

void enable_hlt(void)
{
	hlt_counter--;
}

EXPORT_SYMBOL(enable_hlt);

void default_idle(void)
{
	/* endless idle loop with no priority at all */
	while (1) {
		if (hlt_counter) {
			while (1)
				if (need_resched())
					break;
		} else {
			local_irq_disable();
			while (!need_resched()) {
				local_irq_enable();
				asm volatile("sleep" : : : "memory");
				local_irq_disable();
			}
			local_irq_enable();
		}
		schedule();
	}
}

void cpu_idle(void *unused)
{
	default_idle();
}

void machine_restart(char * __unused)
{
	/* SR.BL=1 and invoke address error to let CPU reset (manual reset) */
	asm volatile("ldc %0, sr\n\t"
		     "mov.l @%1, %0" : : "r" (0x10000000), "r" (0x80000001));
}

EXPORT_SYMBOL(machine_restart);

void machine_halt(void)
{
	while (1)
		asm volatile("sleep" : : : "memory");
}

EXPORT_SYMBOL(machine_halt);

void machine_power_off(void)
{
}

EXPORT_SYMBOL(machine_power_off);

void show_regs(struct pt_regs * regs)
{
	printk("\n");
	printk("PC  : %08lx SP  : %08lx SR  : %08lx TEA : %08x    %s\n",
	       regs->pc, regs->regs[15], regs->sr, ctrl_inl(MMU_TEA), print_tainted());
	printk("R0  : %08lx R1  : %08lx R2  : %08lx R3  : %08lx\n",
	       regs->regs[0],regs->regs[1],
	       regs->regs[2],regs->regs[3]);
	printk("R4  : %08lx R5  : %08lx R6  : %08lx R7  : %08lx\n",
	       regs->regs[4],regs->regs[5],
	       regs->regs[6],regs->regs[7]);
	printk("R8  : %08lx R9  : %08lx R10 : %08lx R11 : %08lx\n",
	       regs->regs[8],regs->regs[9],
	       regs->regs[10],regs->regs[11]);
	printk("R12 : %08lx R13 : %08lx R14 : %08lx\n",
	       regs->regs[12],regs->regs[13],
	       regs->regs[14]);
	printk("MACH: %08lx MACL: %08lx GBR : %08lx PR  : %08lx\n",
	       regs->mach, regs->macl, regs->gbr, regs->pr);

	/*
	 * If we're in kernel mode, dump the stack too..
	 */
	if (!user_mode(regs)) {
		extern void show_task(unsigned long *sp);
		unsigned long sp = regs->regs[15];

		show_task((unsigned long *)sp);
	}
}

/*
 * Create a kernel thread
 */

/*
 * This is the mechanism for creating a new kernel thread.
 *
 */
extern void kernel_thread_helper(void);
__asm__(".align 5\n"
	"kernel_thread_helper:\n\t"
	"jsr	@r5\n\t"
	" nop\n\t"
	"mov.l	1f, r1\n\t"
	"jsr	@r1\n\t"
	" mov	r0, r4\n\t"
	".align 2\n\t"
	"1:.long do_exit");

int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{	/* Don't use this in BL=1(cli).  Or else, CPU resets! */
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));
	regs.regs[4] = (unsigned long) arg;
	regs.regs[5] = (unsigned long) fn;

	regs.pc = (unsigned long) kernel_thread_helper;
	regs.sr = (1 << 30);

	/* Ok, create the new process.. */
	return do_fork(flags | CLONE_VM | CLONE_UNTRACED, 0, &regs, 0, NULL, NULL);
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	/* Nothing to do. */
}

void flush_thread(void)
{
#if defined(CONFIG_CPU_SH4)
	struct task_struct *tsk = current;

	/* Forget lazy FPU state */
	clear_fpu(tsk);
	tsk->used_math = 0;
#endif
}

void release_thread(struct task_struct *dead_task)
{
	/* do nothing */
}

/* Fill in the fpu structure for a core dump.. */
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpu)
{
	int fpvalid = 0;

#if defined(CONFIG_CPU_SH4)
	struct task_struct *tsk = current;

	fpvalid = tsk->used_math;
	if (fpvalid) {
		unlazy_fpu(tsk);
		memcpy(fpu, &tsk->thread.fpu.hard, sizeof(*fpu));
	}
#endif

	return fpvalid;
}

/* 
 * Capture the user space registers if the task is not running (in user space)
 */
int dump_task_regs(struct task_struct *tsk, elf_gregset_t *regs)
{
	struct pt_regs ptregs;
	
	ptregs = *(struct pt_regs *)
		((unsigned long)tsk->thread_info+THREAD_SIZE - sizeof(ptregs));
	elf_core_copy_regs(regs, &ptregs);

	return 1;
}

int
dump_task_fpu (struct task_struct *tsk, elf_fpregset_t *fpu)
{
	int fpvalid = 0;

#if defined(CONFIG_CPU_SH4)
	fpvalid = tsk->used_math;
	if (fpvalid) {
		unlazy_fpu(tsk);
		memcpy(fpu, &tsk->thread.fpu.hard, sizeof(*fpu));
	}
#endif

	return fpvalid;
}

asmlinkage void ret_from_fork(void);

int copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
		unsigned long unused,
		struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *childregs;

	childregs = ((struct pt_regs *)(THREAD_SIZE + (unsigned long) p->thread_info)) - 1;
	*childregs = *regs;

	if (user_mode(regs)) {
		childregs->regs[15] = usp;
	} else {
		childregs->regs[15] = (unsigned long)p->thread_info+THREAD_SIZE;
	}
        if (clone_flags & CLONE_SETTLS) {
		childregs->gbr = childregs->regs[0];
	}
	childregs->regs[0] = 0; /* Set return value for child */
	childregs->sr |= SR_FD; /* Invalidate FPU flag */
	p->set_child_tid = p->clear_child_tid = NULL;

	p->thread.sp = (unsigned long) childregs;
	p->thread.pc = (unsigned long) ret_from_fork;

#if defined(CONFIG_CPU_SH4)
	{
		struct task_struct *tsk = current;

		unlazy_fpu(tsk);
		p->thread.fpu = tsk->thread.fpu;
		p->used_math = tsk->used_math;
		clear_ti_thread_flag(p->thread_info, TIF_USEDFPU);
	}
#endif
	return 0;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
	dump->magic = CMAGIC;
	dump->start_code = current->mm->start_code;
	dump->start_data  = current->mm->start_data;
	dump->start_stack = regs->regs[15] & ~(PAGE_SIZE - 1);
	dump->u_tsize = (current->mm->end_code - dump->start_code) >> PAGE_SHIFT;
	dump->u_dsize = (current->mm->brk + (PAGE_SIZE-1) - dump->start_data) >> PAGE_SHIFT;
	dump->u_ssize = (current->mm->start_stack - dump->start_stack +
			 PAGE_SIZE - 1) >> PAGE_SHIFT;
	/* Debug registers will come here. */

	dump->regs = *regs;

	dump->u_fpvalid = dump_fpu(regs, &dump->fpu);
}

/*
 *	switch_to(x,y) should switch tasks from x to y.
 *
 */
struct task_struct *__switch_to(struct task_struct *prev, struct task_struct *next)
{
#if defined(CONFIG_CPU_SH4)
	unlazy_fpu(prev);
#endif
	/*
	 * Restore the kernel mode register
	 *   	k7 (r7_bank1)
	 */
	asm volatile("ldc	%0, r7_bank"
		     : /* no output */
		     : "r" (next->thread_info));

	return prev;
}

asmlinkage int sys_fork(unsigned long r4, unsigned long r5,
			unsigned long r6, unsigned long r7,
			struct pt_regs regs)
{
#ifdef CONFIG_MMU
	return do_fork(SIGCHLD, regs.regs[15], &regs, 0, NULL, NULL);
#else
	/* fork almost works, enough to trick you into looking elsewhere :-( */
	return -EINVAL;
#endif
}

asmlinkage int sys_clone(unsigned long clone_flags, unsigned long newsp,
			 unsigned long parent_tidptr,
			 unsigned long child_tidptr,
			 struct pt_regs regs)
{
	if (!newsp)
		newsp = regs.regs[15];
	return do_fork(clone_flags & ~CLONE_IDLETASK, newsp, &regs, 0,
			(int *)parent_tidptr, (int *)child_tidptr);
}

/*
 * This is trivial, and on the face of it looks like it
 * could equally well be done in user mode.
 *
 * Not so, for quite unobvious reasons - register pressure.
 * In user mode vfork() cannot have a stack frame, and if
 * done by calling the "clone()" system call directly, you
 * do not have enough call-clobbered registers to hold all
 * the information you need.
 */
asmlinkage int sys_vfork(unsigned long r4, unsigned long r5,
			 unsigned long r6, unsigned long r7,
			 struct pt_regs regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs.regs[15], &regs,
		       0, NULL, NULL);
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(char *ufilename, char **uargv,
			  char **uenvp, unsigned long r7,
			  struct pt_regs regs)
{
	int error;
	char *filename;

	filename = getname(ufilename);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;

	error = do_execve(filename, uargv, uenvp, &regs);
	if (error == 0)
		current->ptrace &= ~PT_DTRACE;
	putname(filename);
out:
	return error;
}

/*
 * These bracket the sleeping functions..
 */
extern void scheduling_functions_start_here(void);
extern void scheduling_functions_end_here(void);
#define first_sched	((unsigned long) scheduling_functions_start_here)
#define last_sched	((unsigned long) scheduling_functions_end_here)

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long schedule_frame;
	unsigned long pc;

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	/*
	 * The same comment as on the Alpha applies here, too ...
	 */
	pc = thread_saved_pc(p);
	if (pc >= (unsigned long) interruptible_sleep_on && pc < (unsigned long) add_timer) {
		schedule_frame = ((unsigned long *)(long)p->thread.sp)[1];
		return (unsigned long)((unsigned long *)schedule_frame)[1];
	}
	return pc;
}

asmlinkage void break_point_trap(unsigned long r4, unsigned long r5,
				 unsigned long r6, unsigned long r7,
				 struct pt_regs regs)
{
	/* Clear tracing.  */
	ctrl_outw(0, UBC_BBRA);
	ctrl_outw(0, UBC_BBRB);

	force_sig(SIGTRAP, current);
}

asmlinkage void break_point_trap_software(unsigned long r4, unsigned long r5,
					  unsigned long r6, unsigned long r7,
					  struct pt_regs regs)
{
	regs.pc -= 2;
	force_sig(SIGTRAP, current);
}
