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
#include <linux/elf.h>
#include <linux/init.h>
#include <linux/init_task.h>

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

int
dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpregs)
{
	if (regs->msr & MSR_FP)
		giveup_fpu(current);
	memcpy(fpregs, &current->thread.fpr[0], sizeof(*fpregs));
	return 1;
}

void
__switch_to(struct task_struct *prev, struct task_struct *new)
{
	struct thread_struct *new_thread, *old_thread;
	unsigned long flags;

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

	__save_and_cli(flags);
	_switch(old_thread, new_thread);
	__restore_flags(flags);
}

void show_regs(struct pt_regs * regs)
{
	int i;

	printk("NIP: %016lX XER: %016lX LR: %016lX REGS: %p TRAP: %04lx    %s\n",
	       regs->nip, regs->xer, regs->link, regs,regs->trap, print_tainted());
	printk("MSR: %016lx EE: %01x PR: %01x FP: %01x ME: %01x IR/DR: %01x%01x\n",
	       regs->msr, regs->msr&MSR_EE ? 1 : 0, regs->msr&MSR_PR ? 1 : 0,
	       regs->msr & MSR_FP ? 1 : 0,regs->msr&MSR_ME ? 1 : 0,
	       regs->msr&MSR_IR ? 1 : 0,
	       regs->msr&MSR_DR ? 1 : 0);
	printk("TASK = %p[%d] '%s' ",
	       current, current->pid, current->comm);
	printk("\nlast math %p ", last_task_used_math);
	
#ifdef CONFIG_SMP
	/* printk(" CPU: %d last CPU: %d", current->processor,current->last_processor); */
#endif /* CONFIG_SMP */
	
	printk("\n");
	for (i = 0;  i < 32;  i++)
	{
		long r;
		if ((i % 4) == 0)
		{
			printk("GPR%02d: ", i);
		}

		if ( __get_user(r, &(regs->gpr[i])) )
		    return;

		printk("%016lX ", r);
		if ((i % 4) == 3)
		{
			printk("\n");
		}
	}
}

void exit_thread(void)
{
	if (last_task_used_math == current)
		last_task_used_math = NULL;
}

void flush_thread(void)
{
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
	    unsigned long unused,
	    struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *childregs, *kregs;
	extern void ret_from_fork(void);
	unsigned long sp = (unsigned long)p->thread_info + THREAD_SIZE;

	/* Copy registers */
	sp -= sizeof(struct pt_regs);
	childregs = (struct pt_regs *) sp;
	*childregs = *regs;
	if ((childregs->msr & MSR_PR) == 0) {
		/* for kernel thread, set `current' and stackptr in new task */
		childregs->gpr[1] = sp + sizeof(struct pt_regs);
		childregs->gpr[13] = (unsigned long) p;
		p->thread.regs = NULL;	/* no user register state */
		clear_ti_thread_flag(p->thread_info, TIF_32BIT);
#ifdef CONFIG_PPC_ISERIES
		set_ti_thread_flag(p->thread_info, TIF_RUN_LIGHT);
#endif
	} else
		p->thread.regs = childregs;
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

	/*
	 * copy fpu info - assume lazy fpu switch now always
	 *  -- Cort
	 */
	if (regs->msr & MSR_FP) {
		giveup_fpu(current);
		childregs->msr &= ~(MSR_FP | MSR_FE0 | MSR_FE1);
	}
	memcpy(&p->thread.fpr, &current->thread.fpr, sizeof(p->thread.fpr));
	p->thread.fpscr = current->thread.fpscr;

	return 0;
}

/*
 * Set up a thread for executing a new program
 */
void start_thread(struct pt_regs *regs, unsigned long nip, unsigned long sp)
{
	/* NIP is *really* a pointer to the function descriptor for
         * the elf _start routine.  The first entry in the function
         * descriptor is the entry address of _start and the second
         * entry is the TOC value we need to use.
         */
	unsigned long *entry = (unsigned long *)nip;
	unsigned long *toc   = entry + 1;

	set_fs(USER_DS);
	memset(regs->gpr, 0, sizeof(regs->gpr));
	memset(&regs->ctr, 0, 4 * sizeof(regs->ctr));
	__get_user(regs->nip, entry);
	regs->gpr[1] = sp;
	__get_user(regs->gpr[2], toc);
	regs->msr = MSR_USER64;
	if (last_task_used_math == current)
		last_task_used_math = 0;
	current->thread.fpscr = 0;
}

int sys_clone(int p1, int p2, int p3, int p4, int p5, int p6,
	      struct pt_regs *regs)
{
	struct task_struct *p;
	p = do_fork(p1 & ~CLONE_IDLETASK, regs->gpr[1], regs, 0);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;
}

int sys_fork(int p1, int p2, int p3, int p4, int p5, int p6,
	     struct pt_regs *regs)
{
	struct task_struct *p;
	p = do_fork(SIGCHLD, regs->gpr[1], regs, 0);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;
}

int sys_vfork(int p1, int p2, int p3, int p4, int p5, int p6,
			 struct pt_regs *regs)
{
	struct task_struct *p;
	p = do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs->gpr[1], regs, 0);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;
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

void initialize_paca_hardware_interrupt_stack(void)
{
	int i;
	unsigned long stack;
	unsigned long end_of_stack =0;

	for (i=1; i < NR_CPUS; i++) {
		if (!cpu_possible(i))
			continue;
		/* Carve out storage for the hardware interrupt stack */
		stack = __get_free_pages(GFP_KERNEL, get_order(8*PAGE_SIZE));

		if ( !stack ) {     
			printk("ERROR, cannot find space for hardware stack.\n");
			panic(" no hardware stack ");
		}


		/* Store the stack value in the PACA for the processor */
		paca[i].xHrdIntStack = stack + (8*PAGE_SIZE) - STACK_FRAME_OVERHEAD;
		paca[i].xHrdIntCount = 0;

	}

	/*
	 * __get_free_pages() might give us a page > KERNBASE+256M which
	 * is mapped with large ptes so we can't set up the guard page.
	 */
	if (cpu_has_largepage())
		return;

	for (i=0; i < NR_CPUS; i++) {
		if (!cpu_possible(i))
			continue;
		/* set page at the top of stack to be protected - prevent overflow */
		end_of_stack = paca[i].xHrdIntStack - (8*PAGE_SIZE - STACK_FRAME_OVERHEAD);
		ppc_md.hpte_updateboltedpp(PP_RXRX,end_of_stack);
	}
}

extern char _stext[], _etext[];

char * ppc_find_proc_name( unsigned * p, char * buf, unsigned buflen )
{
	unsigned long tb_flags;
	unsigned short name_len;
	unsigned long tb_start, code_start, code_ptr, code_offset;
	unsigned code_len;
	strcpy( buf, "Unknown" );
	code_ptr = (unsigned long)p;
	code_offset = 0;
	if ( ( (unsigned long)p >= (unsigned long)_stext ) && ( (unsigned long)p <= (unsigned long)_etext ) ) {
		while ( (unsigned long)p <= (unsigned long)_etext ) {
			if ( *p == 0 ) {
				tb_start = (unsigned long)p;
				++p;	/* Point to traceback flags */
				tb_flags = *((unsigned long *)p);
				p += 2;	/* Skip over traceback flags */
				if ( tb_flags & TB_NAME_PRESENT ) {
					if ( tb_flags & TB_PARMINFO )
						++p;	/* skip over parminfo data */
					if ( tb_flags & TB_HAS_TBOFF ) {
						code_len = *p;	/* get code length */
						code_start = tb_start - code_len;
						code_offset = code_ptr - code_start + 1;
						if ( code_offset > 0x100000 )
							break;
						++p;		/* skip over code size */
					}
					name_len = *((unsigned short *)p);
					if ( name_len > (buflen-20) )
						name_len = buflen-20;
					memcpy( buf, ((char *)p)+2, name_len );
					buf[name_len] = 0;
					if ( code_offset )
						sprintf( buf+name_len, "+0x%lx", code_offset-1 ); 
				}
				break;
			}
			++p;
		}
	}
	return buf;
}

void
print_backtrace(unsigned long *sp)
{
	int cnt = 0;
	unsigned long i;
	char name_buf[256];

	printk("Call backtrace: \n");
	while (sp) {
		if (__get_user( i, &sp[2] ))
			break;
		printk("%016lX ", i);
		printk("%s\n", ppc_find_proc_name( (unsigned *)i, name_buf, 256 ));
		if (cnt > 32) break;
		if (__get_user(sp, (unsigned long **)sp))
			break;
	}
	printk("\n");
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
			if (ip < first_sched || ip >= last_sched)
				return (ip & 0xFFFFFFFF);
		}
	} while (count++ < 16);
	return 0;
}

void show_trace_task(struct task_struct *p)
{
	unsigned long ip, sp;
	unsigned long stack_page = (unsigned long)p->thread_info;
	int count = 0;

	if (!p)
		return;

	printk("Call Trace: ");
	sp = p->thread.ksp;
	do {
		sp = *(unsigned long *)sp;
		if (sp < (stack_page + sizeof(struct thread_struct)) ||
		    sp >= (stack_page + THREAD_SIZE))
			break;
		if (count > 0) {
			ip = *(unsigned long *)(sp + 16);
			printk("[%016lx] ", ip);
		}
	} while (count++ < 16);
	printk("\n");
}
