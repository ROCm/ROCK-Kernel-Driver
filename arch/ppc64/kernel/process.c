/*
 *  linux/arch/ppc64/kernel/process.c
 *
 *  Derived from "arch/i386/kernel/process.c"
 *    Copyright (C) 1995  Linus Torvalds
 *
 *  Updated and modified by Cort Dougan (cort@cs.nmt.edu) and
 *  Paul Mackerras (paulus@cs.anu.edu.au)
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/elf.h>
#include <linux/init.h>
#include <linux/init_task.h>
#include <linux/prctl.h>
#include <linux/ptrace.h>
#include <linux/kallsyms.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/prom.h>
#include <asm/ppcdebug.h>
#include <asm/machdep.h>
#include <asm/iSeries/HvCallHpt.h>
#include <asm/hardirq.h>
#include <asm/cputable.h>
#include <asm/sections.h>

struct task_struct *last_task_used_math = NULL;

struct mm_struct ioremap_mm = { pgd             : ioremap_dir  
                               ,page_table_lock : SPIN_LOCK_UNLOCKED };

char *sysmap = NULL;
unsigned long sysmap_size = 0;

void
enable_kernel_fp(void)
{
#ifdef CONFIG_SMP
	if (current->thread.regs && (current->thread.regs->msr & MSR_FP))
		giveup_fpu(current);
	else
		giveup_fpu(NULL);	/* just enables FP for kernel */
#else
	giveup_fpu(last_task_used_math);
#endif /* CONFIG_SMP */
}

int dump_task_fpu(struct task_struct *tsk, elf_fpregset_t *fpregs)
{
	struct pt_regs *regs = tsk->thread.regs;

	if (!regs)
		return 0;
	if (tsk == current && (regs->msr & MSR_FP))
		giveup_fpu(current);

	memcpy(fpregs, &tsk->thread.fpr[0], sizeof(*fpregs));

	return 1;
}

struct task_struct *__switch_to(struct task_struct *prev,
				struct task_struct *new)
{
	struct thread_struct *new_thread, *old_thread;
	unsigned long flags;
	struct task_struct *last;

#ifdef CONFIG_SMP
	/* avoid complexity of lazy save/restore of fpu
	 * by just saving it every time we switch out if
	 * this task used the fpu during the last quantum.
	 * 
	 * If it tries to use the fpu again, it'll trap and
	 * reload its fp regs.  So we don't have to do a restore
	 * every switch, just a save.
	 *  -- Cort
	 */
	if (prev->thread.regs && (prev->thread.regs->msr & MSR_FP))
		giveup_fpu(prev);
#endif /* CONFIG_SMP */

	new_thread = &new->thread;
	old_thread = &current->thread;

	local_irq_save(flags);
	last = _switch(old_thread, new_thread);
	local_irq_restore(flags);
	return last;
}

void show_regs(struct pt_regs * regs)
{
	int i;

	printk("NIP: %016lX XER: %016lX LR: %016lX\n",
	       regs->nip, regs->xer, regs->link);
	printk("REGS: %p TRAP: %04lx    %s\n",
	       regs, regs->trap, print_tainted());
	printk("MSR: %016lx EE: %01x PR: %01x FP: %01x ME: %01x IR/DR: %01x%01x\n",
	       regs->msr, regs->msr&MSR_EE ? 1 : 0, regs->msr&MSR_PR ? 1 : 0,
	       regs->msr & MSR_FP ? 1 : 0,regs->msr&MSR_ME ? 1 : 0,
	       regs->msr&MSR_IR ? 1 : 0,
	       regs->msr&MSR_DR ? 1 : 0);
	if (regs->trap == 0x300 || regs->trap == 0x380 || regs->trap == 0x600)
		printk("DAR: %016lx, DSISR: %016lx\n", regs->dar, regs->dsisr);
	printk("TASK = %p[%d] '%s' ",
	       current, current->pid, current->comm);

#ifdef CONFIG_SMP
	printk(" CPU: %d", smp_processor_id());
#endif /* CONFIG_SMP */

	for (i = 0; i < 32; i++) {
		long r;
		if ((i % 4) == 0) {
			printk("\n" KERN_INFO "GPR%02d: ", i);
		}
		if (__get_user(r, &(regs->gpr[i])))
		    return;
		printk("%016lX ", r);
	}
	printk("\n");
	/*
	 * Lookup NIP late so we have the best change of getting the
	 * above info out without failing
	 */
	printk("NIP [%016lx] ", regs->nip);
	print_symbol("%s\n", regs->nip);
	show_stack(current, (unsigned long *)regs->gpr[1]);
}

void exit_thread(void)
{
	if (last_task_used_math == current)
		last_task_used_math = NULL;
}

void flush_thread(void)
{
	struct thread_info *t = current_thread_info();

	if (t->flags & _TIF_ABI_PENDING)
		t->flags ^= (_TIF_ABI_PENDING | _TIF_32BIT);

	if (last_task_used_math == current)
		last_task_used_math = NULL;
}

void
release_thread(struct task_struct *t)
{
}

/*
 * Copy a thread..
 */
int
copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
	    unsigned long unused, struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *childregs, *kregs;
	extern void ret_from_fork(void);
	unsigned long sp = (unsigned long)p->thread_info + THREAD_SIZE;

	p->set_child_tid = p->clear_child_tid = NULL;

	/* Copy registers */
	sp -= sizeof(struct pt_regs);
	childregs = (struct pt_regs *) sp;
	*childregs = *regs;
	if ((childregs->msr & MSR_PR) == 0) {
		/* for kernel thread, set stackptr in new task */
		childregs->gpr[1] = sp + sizeof(struct pt_regs);
		p->thread.regs = NULL;	/* no user register state */
		clear_ti_thread_flag(p->thread_info, TIF_32BIT);
#ifdef CONFIG_PPC_ISERIES
		set_ti_thread_flag(p->thread_info, TIF_RUN_LIGHT);
#endif
	} else {
		childregs->gpr[1] = usp;
		p->thread.regs = childregs;
		if (clone_flags & CLONE_SETTLS) {
			if (test_thread_flag(TIF_32BIT))
				childregs->gpr[2] = childregs->gpr[6];
			else
				childregs->gpr[13] = childregs->gpr[6];
		}
	}
	childregs->gpr[3] = 0;  /* Result from fork() */
	sp -= STACK_FRAME_OVERHEAD;

	/*
	 * The way this works is that at some point in the future
	 * some task will call _switch to switch to the new task.
	 * That will pop off the stack frame created below and start
	 * the new task running at ret_from_fork.  The new task will
	 * do some house keeping and then return from the fork or clone
	 * system call, using the stack frame created above.
	 */
	sp -= sizeof(struct pt_regs);
	kregs = (struct pt_regs *) sp;
	sp -= STACK_FRAME_OVERHEAD;
	p->thread.ksp = sp;

	/*
	 * The PPC64 ABI makes use of a TOC to contain function 
	 * pointers.  The function (ret_from_except) is actually a pointer
	 * to the TOC entry.  The first entry is a pointer to the actual
	 * function.
 	 */
	kregs->nip = *((unsigned long *)ret_from_fork);

	return 0;
}

/*
 * Set up a thread for executing a new program
 */
void start_thread(struct pt_regs *regs, unsigned long fdptr, unsigned long sp)
{
	unsigned long entry, toc, load_addr = regs->gpr[2];

	/* fdptr is a relocated pointer to the function descriptor for
         * the elf _start routine.  The first entry in the function
         * descriptor is the entry address of _start and the second
         * entry is the TOC value we need to use.
         */
	set_fs(USER_DS);
	__get_user(entry, (unsigned long *)fdptr);
	__get_user(toc, (unsigned long *)fdptr+1);

	/* Check whether the e_entry function descriptor entries
	 * need to be relocated before we can use them.
	 */
	if ( load_addr != 0 ) {
		entry += load_addr;
		toc   += load_addr;
	}

	regs->nip = entry;
	regs->gpr[1] = sp;
	regs->gpr[2] = toc;
	regs->msr = MSR_USER64;
	if (last_task_used_math == current)
		last_task_used_math = 0;
	current->thread.fpscr = 0;
}

int set_fpexc_mode(struct task_struct *tsk, unsigned int val)
{
	struct pt_regs *regs = tsk->thread.regs;

	if (val > PR_FP_EXC_PRECISE)
		return -EINVAL;
	tsk->thread.fpexc_mode = __pack_fe01(val);
	if (regs != NULL && (regs->msr & MSR_FP) != 0)
		regs->msr = (regs->msr & ~(MSR_FE0|MSR_FE1))
			| tsk->thread.fpexc_mode;
	return 0;
}

int get_fpexc_mode(struct task_struct *tsk, unsigned long adr)
{
	unsigned int val;

	val = __unpack_fe01(tsk->thread.fpexc_mode);
	return put_user(val, (unsigned int *) adr);
}

int sys_clone(unsigned long clone_flags, unsigned long p2, unsigned long p3,
	      unsigned long p4, unsigned long p5, unsigned long p6,
	      struct pt_regs *regs)
{
	unsigned long parent_tidptr = 0;
	unsigned long child_tidptr = 0;

	if (p2 == 0)
		p2 = regs->gpr[1];	/* stack pointer for child */

	if (clone_flags & (CLONE_PARENT_SETTID | CLONE_CHILD_SETTID |
			   CLONE_CHILD_CLEARTID)) {
		parent_tidptr = p3;
		child_tidptr = p5;
		if (test_thread_flag(TIF_32BIT)) {
			parent_tidptr &= 0xffffffff;
			child_tidptr &= 0xffffffff;
		}
	}

	if (regs->msr & MSR_FP)
		giveup_fpu(current);

	return do_fork(clone_flags & ~CLONE_IDLETASK, p2, regs, 0,
		    (int *)parent_tidptr, (int *)child_tidptr);
}

int sys_fork(unsigned long p1, unsigned long p2, unsigned long p3,
	     unsigned long p4, unsigned long p5, unsigned long p6,
	     struct pt_regs *regs)
{
	if (regs->msr & MSR_FP)
		giveup_fpu(current);

	return do_fork(SIGCHLD, regs->gpr[1], regs, 0, NULL, NULL);
}

int sys_vfork(unsigned long p1, unsigned long p2, unsigned long p3,
	      unsigned long p4, unsigned long p5, unsigned long p6,
	      struct pt_regs *regs)
{
	if (regs->msr & MSR_FP)
		giveup_fpu(current);

	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs->gpr[1], regs, 0,
	            NULL, NULL);
}

int sys_execve(unsigned long a0, unsigned long a1, unsigned long a2,
	       unsigned long a3, unsigned long a4, unsigned long a5,
	       struct pt_regs *regs)
{
	int error;
	char * filename;
	
	filename = getname((char *) a0);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	if (regs->msr & MSR_FP)
		giveup_fpu(current);
  
	error = do_execve(filename, (char **) a1, (char **) a2, regs);
  
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
#define first_sched    (*(unsigned long *)scheduling_functions_start_here)
#define last_sched     (*(unsigned long *)scheduling_functions_end_here)

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long ip, sp;
	unsigned long stack_page = (unsigned long)p->thread_info;
	int count = 0;
	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;
	sp = p->thread.ksp;
	do {
		sp = *(unsigned long *)sp;
		if (sp < (stack_page + sizeof(struct thread_struct)) ||
		    sp >= (stack_page + THREAD_SIZE))
			return 0;
		if (count > 0) {
			ip = *(unsigned long *)(sp + 16);
			/*
			 * XXX we mask the upper 32 bits until procps
			 * gets fixed.
			 */
			if (ip < first_sched || ip >= last_sched)
				return (ip & 0xFFFFFFFF);
		}
	} while (count++ < 16);
	return 0;
}

void show_stack(struct task_struct *p, unsigned long *_sp)
{
	unsigned long ip;
	unsigned long stack_page = (unsigned long)p->thread_info;
	int count = 0;
	unsigned long sp = (unsigned long)_sp;

	if (!p)
		return;

	if (sp == 0)
		sp = p->thread.ksp;
	printk("Call Trace:\n");
	do {
		if (__get_user(sp, (unsigned long *)sp))
			break;
		if (sp < stack_page + sizeof(struct thread_struct))
			break;
		if (sp >= stack_page + THREAD_SIZE)
			break;
		if (__get_user(ip, (unsigned long *)(sp + 16)))
			break;
		printk("[%016lx] ", ip);
		print_symbol("%s\n", ip);
	} while (count++ < 32);
}

void dump_stack(void)
{
	show_stack(current, (unsigned long *)_get_SP());
}

EXPORT_SYMBOL(dump_stack);

void show_trace_task(struct task_struct *tsk)
{
	show_stack(tsk, (unsigned long *)tsk->thread.ksp);
}
