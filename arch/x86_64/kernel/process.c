/*
 *  linux/arch/x86-64/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 * 
 *  X86-64 port
 *	Andi Kleen.
 * 
 *  $Id: process.c,v 1.38 2002/01/15 10:08:03 ak Exp $
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#define __KERNEL_SYSCALLS__
#include <stdarg.h>

#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/elfcore.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/user.h>
#include <linux/module.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/slab.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/ldt.h>
#include <asm/processor.h>
#include <asm/i387.h>
#include <asm/desc.h>
#include <asm/mmu_context.h>
#include <asm/pda.h>
#include <asm/prctl.h>
#include <asm/kdebug.h>

#include <linux/irq.h>

asmlinkage extern void ret_from_fork(void);
int sys_arch_prctl(int code, unsigned long addr);

unsigned long kernel_thread_flags = CLONE_VM | CLONE_UNTRACED;

int hlt_counter;

/*
 * Powermanagement idle function, if any..
 */
void (*pm_idle)(void);

void disable_hlt(void)
{
	hlt_counter++;
}

void enable_hlt(void)
{
	hlt_counter--;
}

/*
 * We use this if we don't have any better
 * idle routine..
 */
void default_idle(void)
{
	if (!hlt_counter) {
		local_irq_disable();
		if (!need_resched())
			safe_halt();
		else
			local_irq_enable();
	}
}

/*
 * On SMP it's slightly faster (but much more power-consuming!)
 * to poll the ->need_resched flag instead of waiting for the
 * cross-CPU IPI to arrive. Use this option with caution.
 */
static void poll_idle (void)
{
	int oldval;

	local_irq_enable();

	/*
	 * Deal with another CPU just having chosen a thread to
	 * run here:
	 */
	oldval = test_and_clear_thread_flag(TIF_NEED_RESCHED);

	if (!oldval) {
		set_thread_flag(TIF_POLLING_NRFLAG); 
		asm volatile(
			"2:"
			"testl %0,%1;"
			"rep; nop;"
			"je 2b;"
			: :
			"i" (_TIF_NEED_RESCHED), 
			"m" (current_thread_info()->flags));
	} else {
		set_need_resched();
	}
}

/*
 * The idle thread. There's no useful work to be
 * done, so just try to conserve power and have a
 * low exit latency (ie sit in a loop waiting for
 * somebody to say that they'd like to reschedule)
 */
void cpu_idle (void)
{
	/* endless idle loop with no priority at all */
	while (1) {
		void (*idle)(void) = pm_idle;
		if (!idle)
			idle = default_idle;
		while (!need_resched())
			idle();
		schedule();
	}
}

static int __init idle_setup (char *str)
{
	if (!strncmp(str, "poll", 4)) {
		printk("using polling idle threads.\n");
		pm_idle = poll_idle;
	}

	return 1;
}

__setup("idle=", idle_setup);


/* Prints also some state that isn't saved in the pt_regs */ 
void show_regs(struct pt_regs * regs)
{
	unsigned long cr0 = 0L, cr2 = 0L, cr3 = 0L, cr4 = 0L, fs, gs, shadowgs;
	unsigned int fsindex,gsindex;
	unsigned int ds,cs,es; 

	printk("\n");
	print_modules();
	printk("Pid: %d, comm: %.20s %s\n", current->pid, current->comm, print_tainted());
	printk("RIP: %04lx:[<%016lx>] ", regs->cs & 0xffff, regs->rip);
	printk_address(regs->rip); 
	printk("\nRSP: %04lx:%016lx  EFLAGS: %08lx\n", regs->ss, regs->rsp, regs->eflags);
	printk("RAX: %016lx RBX: %016lx RCX: %016lx\n",
	       regs->rax, regs->rbx, regs->rcx);
	printk("RDX: %016lx RSI: %016lx RDI: %016lx\n",
	       regs->rdx, regs->rsi, regs->rdi); 
	printk("RBP: %016lx R08: %016lx R09: %016lx\n",
	       regs->rbp, regs->r8, regs->r9); 
	printk("R10: %016lx R11: %016lx R12: %016lx\n",
	       regs->r10, regs->r11, regs->r12); 
	printk("R13: %016lx R14: %016lx R15: %016lx\n",
	       regs->r13, regs->r14, regs->r15); 

	asm("movl %%ds,%0" : "=r" (ds)); 
	asm("movl %%cs,%0" : "=r" (cs)); 
	asm("movl %%es,%0" : "=r" (es)); 
	asm("movl %%fs,%0" : "=r" (fsindex));
	asm("movl %%gs,%0" : "=r" (gsindex));

	rdmsrl(MSR_FS_BASE, fs);
	rdmsrl(MSR_GS_BASE, gs); 
	rdmsrl(MSR_KERNEL_GS_BASE, shadowgs); 

	asm("movq %%cr0, %0": "=r" (cr0));
	asm("movq %%cr2, %0": "=r" (cr2));
	asm("movq %%cr3, %0": "=r" (cr3));
	asm("movq %%cr4, %0": "=r" (cr4));

	printk("FS:  %016lx(%04x) GS:%016lx(%04x) knlGS:%016lx\n", 
	       fs,fsindex,gs,gsindex,shadowgs); 
	printk("CS:  %04x DS: %04x ES: %04x CR0: %016lx\n", cs, ds, es, cr0); 
	printk("CR2: %016lx CR3: %016lx CR4: %016lx\n", cr2, cr3, cr4);
}

extern void load_gs_index(unsigned);

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	struct task_struct *me = current;
	if (me->thread.io_bitmap_ptr) { 
		kfree(me->thread.io_bitmap_ptr); 
		me->thread.io_bitmap_ptr = NULL;
		(init_tss + smp_processor_id())->io_map_base = 
			INVALID_IO_BITMAP_OFFSET;
	}
}

void flush_thread(void)
{
	struct task_struct *tsk = current;

	memset(tsk->thread.debugreg, 0, sizeof(unsigned long)*8);
	memset(tsk->thread.tls_array, 0, sizeof(tsk->thread.tls_array));	
	/*
	 * Forget coprocessor state..
	 */
	clear_fpu(tsk);
	tsk->used_math = 0;
}

void release_thread(struct task_struct *dead_task)
{
	if (dead_task->mm) {
		if (dead_task->mm->context.size) {
			printk("WARNING: dead process %8s still has LDT? <%p/%d>\n",
					dead_task->comm,
					dead_task->mm->context.ldt,
					dead_task->mm->context.size);
			BUG();
		}
	}
}

int copy_thread(int nr, unsigned long clone_flags, unsigned long rsp, 
		unsigned long unused,
	struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;
	struct task_struct *me = current;

	childregs = ((struct pt_regs *) (THREAD_SIZE + (unsigned long) p->thread_info)) - 1;

	*childregs = *regs;

	childregs->rax = 0;
	childregs->rsp = rsp;
	if (rsp == ~0) {
		childregs->rsp = (unsigned long)childregs;
	}
	p->set_child_tid = p->clear_child_tid = NULL;

	p->thread.rsp = (unsigned long) childregs;
	p->thread.rsp0 = (unsigned long) (childregs+1);
	p->thread.userrsp = current->thread.userrsp; 

	p->thread.rip = (unsigned long) ret_from_fork;

	p->thread.fs = me->thread.fs;
	p->thread.gs = me->thread.gs;

	asm("movl %%gs,%0" : "=m" (p->thread.gsindex));
	asm("movl %%fs,%0" : "=m" (p->thread.fsindex));
	asm("movl %%es,%0" : "=m" (p->thread.es));
	asm("movl %%ds,%0" : "=m" (p->thread.ds));

	unlazy_fpu(current);	
	p->thread.i387 = current->thread.i387;

	if (unlikely(me->thread.io_bitmap_ptr != NULL)) { 
		p->thread.io_bitmap_ptr = kmalloc((IO_BITMAP_SIZE+1)*4, GFP_KERNEL);
		if (!p->thread.io_bitmap_ptr) 
			return -ENOMEM;
		memcpy(p->thread.io_bitmap_ptr, me->thread.io_bitmap_ptr, 
		       (IO_BITMAP_SIZE+1)*4);
	} 

	/*
	 * Set a new TLS for the child thread?
	 */
	if (clone_flags & CLONE_SETTLS) {
		struct n_desc_struct *desc;
		struct user_desc info;
		int idx;

		if (copy_from_user(&info, test_thread_flag(TIF_IA32) ? 
								  (void *)childregs->rsi : 
								  (void *)childregs->rdx, sizeof(info)))
			return -EFAULT;
		if (LDT_empty(&info))
			return -EINVAL;

		idx = info.entry_number;
		if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
			return -EINVAL;

		desc = (struct n_desc_struct *)(p->thread.tls_array) + idx - GDT_ENTRY_TLS_MIN;
		desc->a = LDT_entry_a(&info);
		desc->b = LDT_entry_b(&info);
	}

	return 0;
}

/*
 * This special macro can be used to load a debugging register
 */
#define loaddebug(thread,register) \
		set_debug(thread->debugreg[register], register)

/*
 *	switch_to(x,y) should switch tasks from x to y.
 *
 * This could still be optimized: 
 * - fold all the options into a flag word and test it with a single test.
 * - could test fs/gs bitsliced
 */
void __switch_to(struct task_struct *prev_p, struct task_struct *next_p)
{
	struct thread_struct *prev = &prev_p->thread,
				 *next = &next_p->thread;
	int cpu = smp_processor_id();  
	struct tss_struct *tss = init_tss + cpu;

	unlazy_fpu(prev_p);

	/*
	 * Reload esp0, LDT and the page table pointer:
	 */
	tss->rsp0 = next->rsp0;

	/* 
	 * Switch DS and ES.
	 * This won't pick up thread selector changes, but I guess that is ok.
	 */
	asm volatile("movl %%es,%0" : "=m" (prev->es)); 
	if (unlikely(next->es | prev->es))
		loadsegment(es, next->es); 
	
	asm volatile ("movl %%ds,%0" : "=m" (prev->ds)); 
	if (unlikely(next->ds | prev->ds))
		loadsegment(ds, next->ds);

	load_TLS(next, cpu);

	/* 
	 * Switch FS and GS.
	 */
	{ 
		unsigned fsindex;
		asm volatile("movl %%fs,%0" : "=g" (fsindex)); 
		/* segment register != 0 always requires a reload. 
		   also reload when it has changed. 
		   when prev process used 64bit base always reload
		   to avoid an information leak. */
		if (unlikely((fsindex | next->fsindex) || prev->fs))
			loadsegment(fs, next->fsindex);
		/* check if the user changed the selector
		   if yes clear 64bit base. */
		if (unlikely(fsindex != prev->fsindex))
			prev->fs = 0;				
		/* when next process has a 64bit base use it */
		if (next->fs) 
			wrmsrl(MSR_FS_BASE, next->fs); 
		prev->fsindex = fsindex;
	}
	{ 
		unsigned gsindex;
		asm volatile("movl %%gs,%0" : "=g" (gsindex)); 
		if (unlikely((gsindex | next->gsindex) || prev->gs))
			load_gs_index(next->gsindex);
		if (unlikely(gsindex != prev->gsindex)) 
			prev->gs = 0;				
		if (next->gs)
			wrmsrl(MSR_KERNEL_GS_BASE, next->gs); 
		prev->gsindex = gsindex;
	}

	/* 
	 * Switch the PDA context.
	 */
	prev->userrsp = read_pda(oldrsp); 
	write_pda(oldrsp, next->userrsp); 
	write_pda(pcurrent, next_p); 
	write_pda(kernelstack, (unsigned long)next_p->thread_info + THREAD_SIZE - PDA_STACKOFFSET);


	/*
	 * Now maybe reload the debug registers
	 */
	if (unlikely(next->debugreg[7])) {
		loaddebug(next, 0);
		loaddebug(next, 1);
		loaddebug(next, 2);
		loaddebug(next, 3);
		/* no 4 and 5 */
		loaddebug(next, 6);
		loaddebug(next, 7);
	}


	/* 
	 * Handle the IO bitmap 
	 */ 
	if (unlikely(prev->io_bitmap_ptr || next->io_bitmap_ptr)) {
		if (next->io_bitmap_ptr) {
			/*
			 * 4 cachelines copy ... not good, but not that
			 * bad either. Anyone got something better?
			 * This only affects processes which use ioperm().
			 */
			memcpy(tss->io_bitmap, next->io_bitmap_ptr,
				 IO_BITMAP_SIZE*sizeof(u32));
			tss->io_map_base = IO_BITMAP_OFFSET;
		} else {
			/*
			 * a bitmap offset pointing outside of the TSS limit
			 * causes a nicely controllable SIGSEGV if a process
			 * tries to use a port IO instruction. The first
			 * sys_ioperm() call sets up the bitmap properly.
			 */
			tss->io_map_base = INVALID_IO_BITMAP_OFFSET;
		}
	}
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage 
long sys_execve(char *name, char **argv,char **envp, struct pt_regs regs)
{
	long error;
	char * filename;

	filename = getname(name);
	error = PTR_ERR(filename);
	if (IS_ERR(filename)) 
		return error;
	error = do_execve(filename, argv, envp, &regs); 
	if (error == 0)
		current->ptrace &= ~PT_DTRACE;
	putname(filename);
	return error;
}

void set_personality_64bit(void)
{
	/* inherit personality from parent */

	/* Make sure to be in 64bit mode */
	clear_thread_flag(TIF_IA32); 
}

asmlinkage long sys_fork(struct pt_regs regs)
{
	struct task_struct *p;
	p = do_fork(SIGCHLD, regs.rsp, &regs, 0, NULL, NULL);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;
}

asmlinkage long sys_clone(unsigned long clone_flags, unsigned long newsp, void *parent_tid, void *child_tid, struct pt_regs regs)
{
	struct task_struct *p;
	if (!newsp)
		newsp = regs.rsp;
	p = do_fork(clone_flags & ~CLONE_IDLETASK, newsp, &regs, 0, 
		    parent_tid, child_tid);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;
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
asmlinkage long sys_vfork(struct pt_regs regs)
{
	struct task_struct *p;
	p = do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs.rsp, &regs, 0, 
		    NULL, NULL);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;
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
	u64 fp,rip;
	int count = 0;

	if (!p || p == current || p->state==TASK_RUNNING)
		return 0; 
	if (p->thread.rsp < (u64)p || p->thread.rsp > (u64)p + THREAD_SIZE)
		return 0;
	fp = *(u64 *)(p->thread.rsp);
	do { 
		if (fp < (unsigned long)p || fp > (unsigned long)p+THREAD_SIZE)
			return 0; 
		rip = *(u64 *)(fp+8); 
		if (rip < first_sched || rip >= last_sched)
			return rip; 
		fp = *(u64 *)fp; 
	} while (count++ < 16); 
	return 0;
}
#undef last_sched
#undef first_sched

int sys_arch_prctl(int code, unsigned long addr)
{ 
	int ret = 0; 
	unsigned long tmp; 

	switch (code) { 
	case ARCH_SET_GS:
		if (addr >= TASK_SIZE) 
			return -EPERM; 
		load_gs_index(0);
		current->thread.gsindex = 0;
		current->thread.gs = addr;
		ret = checking_wrmsrl(MSR_KERNEL_GS_BASE, addr); 
		break;
	case ARCH_SET_FS:
		/* Not strictly needed for fs, but do it for symmetry
		   with gs */
		if (addr >= TASK_SIZE)
			return -EPERM; 
		asm volatile("movl %0,%%fs" :: "r" (0));
		current->thread.fsindex = 0;
		current->thread.fs = addr;
		ret = checking_wrmsrl(MSR_FS_BASE, addr); 
		break;

		/* Returned value may not be correct when the user changed fs/gs */ 
	case ARCH_GET_FS:
		rdmsrl(MSR_FS_BASE, tmp);
		ret = put_user(tmp, (unsigned long *)addr); 
		break; 

	case ARCH_GET_GS: 
		rdmsrl(MSR_KERNEL_GS_BASE, tmp); 
		ret = put_user(tmp, (unsigned long *)addr); 
		break;

	default:
		ret = -EINVAL;
		break;
	} 
	return ret;	
} 

/*
 * sys_alloc_thread_area: get a yet unused TLS descriptor index.
 */
static int get_free_idx(void)
{
	struct thread_struct *t = &current->thread;
	int idx;

	for (idx = 0; idx < GDT_ENTRY_TLS_ENTRIES; idx++)
		if (desc_empty((struct n_desc_struct *)(t->tls_array) + idx))
			return idx + GDT_ENTRY_TLS_MIN;
	return -ESRCH;
}

/*
 * Set a given TLS descriptor:
 * When you want addresses > 32bit use arch_prctl() 
 */
asmlinkage int sys_set_thread_area(struct user_desc *u_info)
{
	struct thread_struct *t = &current->thread;
	struct user_desc info;
	struct n_desc_struct *desc;
	int cpu, idx;

	if (copy_from_user(&info, u_info, sizeof(info)))
		return -EFAULT;
	idx = info.entry_number;

	/*
	 * index -1 means the kernel should try to find and
	 * allocate an empty descriptor:
	 */
	if (idx == -1) {
		idx = get_free_idx();
		if (idx < 0)
			return idx;
		if (put_user(idx, &u_info->entry_number))
			return -EFAULT;
	}

	if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
		return -EINVAL;

	desc = ((struct n_desc_struct *)t->tls_array) + idx - GDT_ENTRY_TLS_MIN;

	/*
	 * We must not get preempted while modifying the TLS.
	 */
	cpu = get_cpu();

	if (LDT_empty(&info)) {
		desc->a = 0;
		desc->b = 0;
	} else {
		desc->a = LDT_entry_a(&info);
		desc->b = LDT_entry_b(&info);
	}
	load_TLS(t, cpu);

	put_cpu();
	return 0;
}

/*
 * Get the current Thread-Local Storage area:
 */

#define GET_BASE(desc) ( \
	(((desc)->a >> 16) & 0x0000ffff) | \
	(((desc)->b << 16) & 0x00ff0000) | \
	( (desc)->b        & 0xff000000)   )

#define GET_LIMIT(desc) ( \
	((desc)->a & 0x0ffff) | \
	 ((desc)->b & 0xf0000) )
	
#define GET_32BIT(desc)		(((desc)->b >> 23) & 1)
#define GET_CONTENTS(desc)	(((desc)->b >> 10) & 3)
#define GET_WRITABLE(desc)	(((desc)->b >>  9) & 1)
#define GET_LIMIT_PAGES(desc)	(((desc)->b >> 23) & 1)
#define GET_PRESENT(desc)	(((desc)->b >> 15) & 1)
#define GET_USEABLE(desc)	(((desc)->b >> 20) & 1)

asmlinkage int sys_get_thread_area(struct user_desc *u_info)
{
	struct user_desc info;
	struct n_desc_struct *desc;
	int idx;

	if (get_user(idx, &u_info->entry_number))
		return -EFAULT;
	if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
		return -EINVAL;

	desc = ((struct n_desc_struct *)current->thread.tls_array) + idx - GDT_ENTRY_TLS_MIN;

	memset(&info, 0, sizeof(struct user_desc));
	info.entry_number = idx;
	info.base_addr = GET_BASE(desc);
	info.limit = GET_LIMIT(desc);
	info.seg_32bit = GET_32BIT(desc);
	info.contents = GET_CONTENTS(desc);
	info.read_exec_only = !GET_WRITABLE(desc);
	info.limit_in_pages = GET_LIMIT_PAGES(desc);
	info.seg_not_present = !GET_PRESENT(desc);
	info.useable = GET_USEABLE(desc);

	if (copy_to_user(u_info, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

/* 
 * Capture the user space registers if the task is not running (in user space)
 */
int dump_task_regs(struct task_struct *tsk, elf_gregset_t *regs)
{
	struct pt_regs *pp, ptregs;

	pp = (struct pt_regs *)(tsk->thread.rsp0);
	--pp; 

	ptregs = *pp; 
	ptregs.cs &= 0xffff;
	ptregs.ss &= 0xffff;

	elf_core_copy_regs(regs, &ptregs);
 
	return 1;
}
