/*
 *  linux/arch/i386/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#define __KERNEL_SYSCALLS__
#include <stdarg.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/mc146818rtc.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/ldt.h>
#include <asm/processor.h>
#include <asm/i387.h>
#include <asm/desc.h>
#ifdef CONFIG_MATH_EMULATION
#include <asm/math_emu.h>
#endif

#include <linux/irq.h>
#include <linux/err.h>

asmlinkage void ret_from_fork(void) __asm__("ret_from_fork");

int hlt_counter;

/*
 * Return saved PC of a blocked thread.
 */
unsigned long thread_saved_pc(struct task_struct *tsk)
{
	return ((unsigned long *)tsk->thread.esp)[3];
}

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
	if (current_cpu_data.hlt_works_ok && !hlt_counter) {
		local_irq_disable();
		if (!need_resched())
			safe_halt();
		else
			local_irq_enable();
	}
}

/*
 * On SMP it's slightly faster (but much more power-consuming!)
 * to poll the ->work.need_resched flag instead of waiting for the
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
			"testl %0, %1;"
			"rep; nop;"
			"je 2b;"
			: : "i"(_TIF_NEED_RESCHED), "m" (current_thread_info()->flags));

		clear_thread_flag(TIF_POLLING_NRFLAG);
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
		irq_stat[smp_processor_id()].idle_timestamp = jiffies;
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

extern void show_trace(unsigned long* esp);

void show_regs(struct pt_regs * regs)
{
	unsigned long cr0 = 0L, cr2 = 0L, cr3 = 0L, cr4 = 0L;

	printk("\n");
	printk("Pid: %d, comm: %20s\n", current->pid, current->comm);
	printk("EIP: %04x:[<%08lx>] CPU: %d",0xffff & regs->xcs,regs->eip, smp_processor_id());
	if (regs->xcs & 3)
		printk(" ESP: %04x:%08lx",0xffff & regs->xss,regs->esp);
	printk(" EFLAGS: %08lx    %s\n",regs->eflags, print_tainted());
	printk("EAX: %08lx EBX: %08lx ECX: %08lx EDX: %08lx\n",
		regs->eax,regs->ebx,regs->ecx,regs->edx);
	printk("ESI: %08lx EDI: %08lx EBP: %08lx",
		regs->esi, regs->edi, regs->ebp);
	printk(" DS: %04x ES: %04x\n",
		0xffff & regs->xds,0xffff & regs->xes);

	__asm__("movl %%cr0, %0": "=r" (cr0));
	__asm__("movl %%cr2, %0": "=r" (cr2));
	__asm__("movl %%cr3, %0": "=r" (cr3));
	/* This could fault if %cr4 does not exist */
	__asm__("1: movl %%cr4, %0		\n"
		"2:				\n"
		".section __ex_table,\"a\"	\n"
		".long 1b,2b			\n"
		".previous			\n"
		: "=r" (cr4): "0" (0));
	printk("CR0: %08lx CR2: %08lx CR3: %08lx CR4: %08lx\n", cr0, cr2, cr3, cr4);
	show_trace(&regs->esp);
}

/*
 * Create a kernel thread
 */
int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	long retval, d0;

	__asm__ __volatile__(
		"movl %%esp,%%esi\n\t"
		"int $0x80\n\t"		/* Linux/i386 system call */
		"cmpl %%esp,%%esi\n\t"	/* child or parent? */
		"je 1f\n\t"		/* parent - jump */
		/* Load the argument into eax, and push it.  That way, it does
		 * not matter whether the called function is compiled with
		 * -mregparm or not.  */
		"movl %4,%%eax\n\t"
		"pushl %%eax\n\t"		
		"call *%5\n\t"		/* call fn */
		"movl %3,%0\n\t"	/* exit */
		"int $0x80\n"
		"1:\t"
		:"=&a" (retval), "=&S" (d0)
		:"0" (__NR_clone), "i" (__NR_exit),
		 "r" (arg), "r" (fn),
		 "b" (flags | CLONE_VM)
		: "memory");
	return retval;
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	struct task_struct *tsk = current;

	/* The process may have allocated an io port bitmap... nuke it. */
	if (unlikely(NULL != tsk->thread.ts_io_bitmap)) {
		kfree(tsk->thread.ts_io_bitmap);
		tsk->thread.ts_io_bitmap = NULL;
	}
}

void flush_thread(void)
{
	struct task_struct *tsk = current;

	memset(tsk->thread.debugreg, 0, sizeof(unsigned long)*8);
	/*
	 * Forget coprocessor state..
	 */
	clear_fpu(tsk);
	tsk->used_math = 0;
}

void release_thread(struct task_struct *dead_task)
{
	if (dead_task->mm) {
		// temporary debugging check
		if (dead_task->mm->context.size) {
			printk("WARNING: dead process %8s still has LDT? <%p/%d>\n",
					dead_task->comm,
					dead_task->mm->context.ldt,
					dead_task->mm->context.size);
			BUG();
		}
	}
}

/*
 * Save a segment.
 */
#define savesegment(seg,value) \
	asm volatile("movl %%" #seg ",%0":"=m" (*(int *)&(value)))

int copy_thread(int nr, unsigned long clone_flags, unsigned long esp,
	unsigned long unused,
	struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;
	struct task_struct *tsk;

	childregs = ((struct pt_regs *) (THREAD_SIZE + (unsigned long) p->thread_info)) - 1;
	struct_cpy(childregs, regs);
	childregs->eax = 0;
	childregs->esp = esp;

	p->thread.esp = (unsigned long) childregs;
	p->thread.esp0 = (unsigned long) (childregs+1);

	p->thread.eip = (unsigned long) ret_from_fork;

	savesegment(fs,p->thread.fs);
	savesegment(gs,p->thread.gs);

	tsk = current;
	unlazy_fpu(tsk);
	struct_cpy(&p->thread.i387, &tsk->thread.i387);

	if (unlikely(NULL != tsk->thread.ts_io_bitmap)) {
		p->thread.ts_io_bitmap = kmalloc(IO_BITMAP_BYTES, GFP_KERNEL);
		if (!p->thread.ts_io_bitmap)
			return -ENOMEM;
		memcpy(p->thread.ts_io_bitmap, tsk->thread.ts_io_bitmap,
			IO_BITMAP_BYTES);
	}

	return 0;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
	int i;

/* changed the size calculations - should hopefully work better. lbt */
	dump->magic = CMAGIC;
	dump->start_code = 0;
	dump->start_stack = regs->esp & ~(PAGE_SIZE - 1);
	dump->u_tsize = ((unsigned long) current->mm->end_code) >> PAGE_SHIFT;
	dump->u_dsize = ((unsigned long) (current->mm->brk + (PAGE_SIZE-1))) >> PAGE_SHIFT;
	dump->u_dsize -= dump->u_tsize;
	dump->u_ssize = 0;
	for (i = 0; i < 8; i++)
		dump->u_debugreg[i] = current->thread.debugreg[i];  

	if (dump->start_stack < TASK_SIZE)
		dump->u_ssize = ((unsigned long) (TASK_SIZE - dump->start_stack)) >> PAGE_SHIFT;

	dump->regs.ebx = regs->ebx;
	dump->regs.ecx = regs->ecx;
	dump->regs.edx = regs->edx;
	dump->regs.esi = regs->esi;
	dump->regs.edi = regs->edi;
	dump->regs.ebp = regs->ebp;
	dump->regs.eax = regs->eax;
	dump->regs.ds = regs->xds;
	dump->regs.es = regs->xes;
	savesegment(fs,dump->regs.fs);
	savesegment(gs,dump->regs.gs);
	dump->regs.orig_eax = regs->orig_eax;
	dump->regs.eip = regs->eip;
	dump->regs.cs = regs->xcs;
	dump->regs.eflags = regs->eflags;
	dump->regs.esp = regs->esp;
	dump->regs.ss = regs->xss;

	dump->u_fpvalid = dump_fpu (regs, &dump->i387);
}

/*
 * This special macro can be used to load a debugging register
 */
#define loaddebug(thread,register) \
		__asm__("movl %0,%%db" #register  \
			: /* no output */ \
			:"r" (thread->debugreg[register]))

/*
 *	switch_to(x,yn) should switch tasks from x to y.
 *
 * We fsave/fwait so that an exception goes off at the right time
 * (as a call from the fsave or fwait in effect) rather than to
 * the wrong process. Lazy FP saving no longer makes any sense
 * with modern CPU's, and this simplifies a lot of things (SMP
 * and UP become the same).
 *
 * NOTE! We used to use the x86 hardware context switching. The
 * reason for not using it any more becomes apparent when you
 * try to recover gracefully from saved state that is no longer
 * valid (stale segment register values in particular). With the
 * hardware task-switch, there is no way to fix up bad state in
 * a reasonable manner.
 *
 * The fact that Intel documents the hardware task-switching to
 * be slow is a fairly red herring - this code is not noticeably
 * faster. However, there _is_ some room for improvement here,
 * so the performance issues may eventually be a valid point.
 * More important, however, is the fact that this allows us much
 * more flexibility.
 */
void __switch_to(struct task_struct *prev_p, struct task_struct *next_p)
{
	struct thread_struct *prev = &prev_p->thread,
				 *next = &next_p->thread;
	int cpu = smp_processor_id();
	struct tss_struct *tss = init_tss + cpu;

	/* never put a printk in __switch_to... printk() calls wake_up*() indirectly */

	unlazy_fpu(prev_p);

	/*
	 * Reload esp0, LDT and the page table pointer:
	 */
	tss->esp0 = next->esp0;

	/*
	 * Save away %fs and %gs. No need to save %es and %ds, as
	 * those are always kernel segments while inside the kernel.
	 */
	asm volatile("movl %%fs,%0":"=m" (*(int *)&prev->fs));
	asm volatile("movl %%gs,%0":"=m" (*(int *)&prev->gs));

	/*
	 * Restore %fs and %gs if needed.
	 */
	if (unlikely(prev->fs | prev->gs | next->fs | next->gs)) {
		loadsegment(fs, next->fs);
		loadsegment(gs, next->gs);
	}

	/*
	 * Load the per-thread Thread-Local Storage descriptor.
	 *
	 * NOTE: it's faster to do the two stores unconditionally
	 * than to branch away.
	 */
	load_TLS_desc(next, cpu);

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

	if (unlikely(prev->ts_io_bitmap || next->ts_io_bitmap)) {
		if (next->ts_io_bitmap) {
			/*
			 * 4 cachelines copy ... not good, but not that
			 * bad either. Anyone got something better?
			 * This only affects processes which use ioperm().
			 * [Putting the TSSs into 4k-tlb mapped regions
			 * and playing VM tricks to switch the IO bitmap
			 * is not really acceptable.]
			 */
			memcpy(tss->io_bitmap, next->ts_io_bitmap,
				IO_BITMAP_BYTES);
			tss->bitmap = IO_BITMAP_OFFSET;
		} else
			/*
			 * a bitmap offset pointing outside of the TSS limit
			 * causes a nicely controllable SIGSEGV if a process
			 * tries to use a port IO instruction. The first
			 * sys_ioperm() call sets up the bitmap properly.
			 */
			tss->bitmap = INVALID_IO_BITMAP_OFFSET;
	}
}

asmlinkage int sys_fork(struct pt_regs regs)
{
	struct task_struct *p;

	p = do_fork(SIGCHLD, regs.esp, &regs, 0);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;
}

asmlinkage int sys_clone(struct pt_regs regs)
{
	struct task_struct *p;
	unsigned long clone_flags;
	unsigned long newsp;

	clone_flags = regs.ebx;
	newsp = regs.ecx;
	if (!newsp)
		newsp = regs.esp;
	p = do_fork(clone_flags & ~CLONE_IDLETASK, newsp, &regs, 0);
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
asmlinkage int sys_vfork(struct pt_regs regs)
{
	struct task_struct *p;

	p = do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs.esp, &regs, 0);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(struct pt_regs regs)
{
	int error;
	char * filename;

	filename = getname((char *) regs.ebx);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, (char **) regs.ecx, (char **) regs.edx, &regs);
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
	unsigned long ebp, esp, eip;
	unsigned long stack_page;
	int count = 0;
	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;
	stack_page = (unsigned long)p->thread_info;
	esp = p->thread.esp;
	if (!stack_page || esp < stack_page || esp > 8188+stack_page)
		return 0;
	/* include/asm-i386/system.h:switch_to() pushes ebp last. */
	ebp = *(unsigned long *) esp;
	do {
		if (ebp < stack_page || ebp > 8184+stack_page)
			return 0;
		eip = *(unsigned long *) (ebp+4);
		if (eip < first_sched || eip >= last_sched)
			return eip;
		ebp = *(unsigned long *) ebp;
	} while (count++ < 16);
	return 0;
}
#undef last_sched
#undef first_sched

/*
 * Set the Thread-Local Storage area:
 */
asmlinkage int sys_set_thread_area(unsigned int base, unsigned int limit, unsigned int flags)
{
	struct thread_struct *t = &current->thread;
	int limit_in_pages = 0, writable = 0;
	int cpu;

	/* do not allow unused flags */
	if (flags & ~TLS_FLAGS_MASK)
		return -EINVAL;

	/* check limit */
	if (limit & 0xfff00000)
		return -EINVAL;

	/*
	 * Clear the TLS?
	 */
	if (flags & TLS_FLAG_CLEAR) {
		cpu = get_cpu();
		t->tls_base = t->tls_limit = t->tls_flags = 0;
        	t->tls_desc.a = t->tls_desc.b = 0;
		load_TLS_desc(t, cpu);
		put_cpu();
		return 0;
	}

	if (flags & TLS_FLAG_LIMIT_IN_PAGES)
		limit_in_pages = 1;
	if (flags & TLS_FLAG_WRITABLE)
		writable = 1;

	/*
	 * We must not get preempted while modifying the TLS.
	 */
	cpu = get_cpu();
	t->tls_base = base;
	t->tls_limit = limit;
	t->tls_flags = flags;

        t->tls_desc.a = ((base & 0x0000ffff) << 16) | (limit & 0x0ffff);

        t->tls_desc.b = (base & 0xff000000) | ((base & 0x00ff0000) >> 16) |
                  (limit & 0xf0000) | (writable << 9) | (1 << 15) |
		  (1 << 22) | (limit_in_pages << 23) | 0x7000;

	load_TLS_desc(t, cpu);
	put_cpu();

	return TLS_ENTRY*8 + 3;
}

