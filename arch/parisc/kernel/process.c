/*
 *  linux/arch/parisc/kernel/process.c
 *	based on the work for i386
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
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/elf.h>

#include <asm/machdep.h>
#include <asm/offset.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/gsc.h>
#include <asm/processor.h>

spinlock_t semaphore_wake_lock = SPIN_LOCK_UNLOCKED;

#ifdef __LP64__
/* The 64-bit code should work equally well in 32-bit land but I didn't
 * want to take the time to confirm that.  -PB
 */
extern unsigned int ret_from_kernel_thread;
#else
asmlinkage void ret_from_kernel_thread(void) __asm__("ret_from_kernel_thread");
#endif


int hlt_counter=0;

void disable_hlt(void)
{
	hlt_counter++;
}

void enable_hlt(void)
{
	hlt_counter--;
}

/*
 * The idle thread. There's no useful work to be
 * done, so just try to conserve power and have a
 * low exit latency (ie sit in a loop waiting for
 * somebody to say that they'd like to reschedule)
 */
void cpu_idle(void)
{
	/* endless idle loop with no priority at all */
	init_idle();
	current->nice = 20;
	current->counter = -100;

	while (1) {
		while (!current->need_resched) {
		}
		schedule();
		check_pgt_cache();
	}
}

void __init reboot_setup(char *str, int *ints)
{
}

struct notifier_block *mach_notifier;

void machine_restart(char *ptr)
{
	notifier_call_chain(&mach_notifier, MACH_RESTART, ptr);
}

void machine_halt(void)
{
	notifier_call_chain(&mach_notifier, MACH_HALT, NULL);
}

void machine_power_on(void)
{
	notifier_call_chain(&mach_notifier, MACH_POWER_ON, NULL);
}

void machine_power_off(void)
{
	notifier_call_chain(&mach_notifier, MACH_POWER_OFF, NULL);
}


void machine_heartbeat(void)
{
}


/*
 * Create a kernel thread
 */

extern pid_t __kernel_thread(int (*fn)(void *), void *arg, unsigned long flags);
pid_t kernel_thread(int (*fn)(void *), void *arg, unsigned long flags)
{

	/*
	 * FIXME: Once we are sure we don't need any debug here,
	 *	  kernel_thread can become a #define.
	 */

	return __kernel_thread(fn, arg, flags);
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
}

void flush_thread(void)
{
	set_fs(USER_DS);
}

void release_thread(struct task_struct *dead_task)
{
}

/*
 * Fill in the FPU structure for a core dump.
 */
int dump_fpu (struct pt_regs * regs, elf_fpregset_t *r)
{
	memcpy(r, regs->fr, sizeof *r);
	return 1;
}

/* Note that "fork()" is implemented in terms of clone, with
   parameters (SIGCHLD, regs->gr[30], regs). */
int
sys_clone(unsigned long clone_flags, unsigned long usp,
	  struct pt_regs *regs)
{
	return do_fork(clone_flags, usp, regs, 0);
}

int
sys_vfork(struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD,
		       regs->gr[30], regs, 0);
}

int
copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
	    unsigned long unused,	/* in ia64 this is "user_stack_size" */
	    struct task_struct * p, struct pt_regs * pregs)
{
	struct pt_regs * cregs = &(p->thread.regs);
	long ksp;

	*cregs = *pregs;

	/* Set the return value for the child.  Note that this is not
           actually restored by the syscall exit path, but we put it
           here for consistency in case of signals. */
	cregs->gr[28] = 0; /* child */

	/*
	 * We need to differentiate between a user fork and a
	 * kernel fork. We can't use user_mode, because the
	 * the syscall path doesn't save iaoq. Right now
	 * We rely on the fact that kernel_thread passes
	 * in zero for usp.
	 */
	if (usp == 0) {
		/* Kernel Thread */
		ksp = (((unsigned long)(p)) + TASK_SZ_ALGN);
		cregs->ksp = ksp;	    /* always return to kernel */
#ifdef __LP64__
		cregs->kpc = (unsigned long) &ret_from_kernel_thread;
#else
		cregs->kpc = (unsigned long) ret_from_kernel_thread;
#endif

		/*
		 * Copy function and argument to be called from
		 * ret_from_kernel_thread.
		 */
		cregs->gr[26] = pregs->gr[26];
		cregs->gr[25] = pregs->gr[25];

	} else {
		/* User Thread:
		 *
		 * Use same stack depth as parent when in wrapper
		 *
		 * Note that the fork wrappers are responsible
		 * for setting gr[20] and gr[21].
		 */

		cregs->ksp = ((unsigned long)(p))
			+ (pregs->gr[20] & (INIT_TASK_SIZE - 1));
		cregs->kpc = pregs->gr[21];
	}

	return 0;
}

/*
 * sys_execve() executes a new program.
 */

asmlinkage int sys_execve(struct pt_regs *regs)
{
	int error;
	char *filename;

	filename = getname((char *) regs->gr[26]);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, (char **) regs->gr[25],
		(char **) regs->gr[24], regs);
	if (error == 0)
		current->ptrace &= ~PT_DTRACE;
	putname(filename);
out:

	return error;
}
