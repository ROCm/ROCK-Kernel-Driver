/* $Id: process.c,v 1.8 2000/09/13 14:34:13 bjornw Exp $
 * 
 *  linux/arch/cris/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (C) 2000  Axis Communications AB
 *
 *  Authors:   Bjorn Wesen (bjornw@axis.com)
 *
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#define __KERNEL_SYSCALLS__
#include <stdarg.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/io.h>
#include <linux/smp.h>

//#define DEBUG

/*
 * Initial task structure. Make this a per-architecture thing,
 * because different architectures tend to have different
 * alignment requirements and potentially different initial
 * setup.
 */

static struct vm_area_struct init_mmap = INIT_MMAP;
static struct fs_struct init_fs = INIT_FS;
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS;
struct mm_struct init_mm = INIT_MM(init_mm);

/*
 * Initial task structure.
 *
 * We need to make sure that this is 8192-byte aligned due to the
 * way process stacks are handled. This is done by having a special
 * "init_task" linker map entry..
 */

union task_union init_task_union 
      __attribute__((__section__(".data.init_task"))) =
             { INIT_TASK(init_task_union.task) };

static int hlt_counter=0;

/* in a system call, set_esp0 is called to remember the stack frame, therefore
   in the implementation of syscalls we can use that value to access the stack
   frame and saved registers.
*/

#define currentregs ((struct pt_regs *)current->thread.esp0)

asmlinkage void set_esp0(unsigned long ssp)
{
	current->thread.esp0 = ssp;
}

void disable_hlt(void)
{
	hlt_counter++;
}

void enable_hlt(void)
{
	hlt_counter--;
}
 
int cpu_idle(void *unused)
{
	while(1) {
		current->counter = -100;
		schedule();
	}
}

/* if the watchdog is enabled, we can simply disable interrupts and go
 * into an eternal loop, and the watchdog will reset the CPU after 0.1s
 */

void hard_reset_now (void)
{
	printk("*** HARD RESET ***\n");
	cli();
	while(1) /* waiting for RETRIBUTION! */ ;
}

void machine_restart(void)
{
	hard_reset_now();
}

/* can't do much here... */

void machine_halt(void)
{
}

void machine_power_off(void)
{
}

/*
 * This is the mechanism for creating a new kernel thread.
 *
 * NOTE! Only a kernel-only process(ie the swapper or direct descendants
 * who haven't done an "execve()") should use this: it will work within
 * a system call from a "real" process, but the process memory space will
 * not be free'd until both the parent and the child have exited.
 */

int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	register long __a __asm__ ("r10");
	
	__asm__ __volatile__
		("movu.w %1,r1\n\t"     /* r1 contains syscall number, to sys_clone */
		 "clear.d r10\n\t"      /* r10 is argument 1 to clone */
		 "move.d %2,r11\n\t"    /* r11 is argument 2 to clone, the flags */
		 "break 13\n\t"         /* call sys_clone, this will fork */
		 "test.d r10\n\t"       /* parent or child? child returns 0 here. */
		 "bne 1f\n\t"           /* jump if parent */
		 "nop\n\t"              /* delay slot */
		 "move.d %4,r10\n\t"    /* set argument to function to call */
		 "jsr %5\n\t"           /* call specified function */
		 "movu.w %3,r1\n\t"     /* r1 is sys_exit syscall number */
		 "moveq -1,r10\n\t"     /* Give a really bad exit-value */
		 "break 13\n\t"         /* call sys_exit, killing the child */
		 "1:\n\t"
		 : "=r" (__a) 
		 : "g" (__NR_clone), "r" (flags | CLONE_VM), "g" (__NR_exit),
		   "r" (arg), "r" (fn) 
		 : "r10", "r11", "r1");
	
	return __a;
}



void flush_thread(void)
{
}

asmlinkage void ret_from_sys_call(void);

/* setup the child's kernel stack with a pt_regs and switch_stack on it.
 * it will be un-nested during _resume and _ret_from_sys_call when the
 * new thread is scheduled.
 *
 * also setup the thread switching structure which is used to keep
 * thread-specific data during _resumes.
 *
 */

int copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
		unsigned long unused,
		struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs * childregs;
	struct switch_stack *swstack;
	
	/* put the pt_regs structure at the end of the new kernel stack page and fix it up
	 * remember that the task_struct doubles as the kernel stack for the task
	 */

	childregs = ((struct pt_regs *) ((unsigned long)p + THREAD_SIZE)) - 1;

	*childregs = *regs;  /* struct copy of pt_regs */

	childregs->r10 = 0;  /* child returns 0 after a fork/clone */
	
	/* put the switch stack right below the pt_regs */

	swstack = ((struct switch_stack *)childregs) - 1;

	swstack->r9 = 0; /* parameter to ret_from_sys_call, 0 == dont restart the syscall */

	/* we want to return into ret_from_sys_call after the _resume */

	swstack->return_ip = (unsigned long) ret_from_sys_call;
	
	/* fix the user-mode stackpointer */

	p->thread.usp = usp;	

	/* and the kernel-mode one */

	p->thread.ksp = (unsigned long) swstack;

	/* esp0 keeps the pt_regs stacked structure pointer */

	p->thread.esp0 = (unsigned long) childregs;

#ifdef DEBUG
	printk("kern_stack_page 0x%x, used stack %d, thread.usp 0x%x, usp 0x%x\n", 
	       current->kernel_stack_page, usedstack, p->thread.usp, usp);
#endif
	return 0;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
	int i;
#if 0
/* changed the size calculations - should hopefully work better. lbt */
	dump->magic = CMAGIC;
	dump->start_code = 0;
	dump->start_stack = regs->esp & ~(PAGE_SIZE - 1);
	dump->u_tsize = ((unsigned long) current->mm->end_code) >> PAGE_SHIFT;
	dump->u_dsize = ((unsigned long) (current->mm->brk + (PAGE_SIZE-1))) >> PAGE_SHIFT;
	dump->u_dsize -= dump->u_tsize;
	dump->u_ssize = 0;
	for (i = 0; i < 8; i++)
		dump->u_debugreg[i] = current->debugreg[i];  

	if (dump->start_stack < TASK_SIZE)
		dump->u_ssize = ((unsigned long) (TASK_SIZE - dump->start_stack)) >> PAGE_SHIFT;

	dump->regs = *regs;

	dump->u_fpvalid = dump_fpu (regs, &dump->i387);
#endif 
}

asmlinkage int sys_fork(void)
{
	return do_fork(SIGCHLD, rdusp(), currentregs, 0);
}

/* if newusp is 0, we just grab the old usp */

asmlinkage int sys_clone(unsigned long newusp, unsigned long flags)
{
	if (!newusp)
		newusp = rdusp();
	return do_fork(flags, newusp, currentregs, 0);
}

/* vfork is a system call in i386 because of register-pressure - maybe
 * we can remove it and handle it in libc but we put it here until then.
 */

asmlinkage int sys_vfork(void)
{
        return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, rdusp(), currentregs, 0);
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(const char *fname, char **argv, char **envp)
{
	int error;
	char *filename;

	filename = getname(fname);
	error = PTR_ERR(filename);

	if (IS_ERR(filename))
	        goto out;
	error = do_execve(filename, argv, envp, currentregs);
	putname(filename);
 out:
	return error;
}

/*
 * These bracket the sleeping functions..
 */

extern void scheduling_functions_start_here(void);
extern void scheduling_functions_end_here(void);
#define first_sched     ((unsigned long) scheduling_functions_start_here)
#define last_sched      ((unsigned long) scheduling_functions_end_here)

unsigned long get_wchan(struct task_struct *p)
{
#if 0
	/* YURGH. TODO. */

        unsigned long ebp, esp, eip;
        unsigned long stack_page;
        int count = 0;
        if (!p || p == current || p->state == TASK_RUNNING)
                return 0;
        stack_page = (unsigned long)p;
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
#endif
        return 0;
}
#undef last_sched
#undef first_sched
