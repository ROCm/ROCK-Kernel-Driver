/*
 *  arch/s390/kernel/process.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Hartmut Penner (hp@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *
 *  Derived from "arch/i386/kernel/process.c"
 *    Copyright (C) 1995, Linus Torvalds
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
#include <linux/vmalloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/irq.h>

asmlinkage void ret_from_fork(void) __asm__("ret_from_fork");

/*
 * Return saved PC of a blocked thread. used in kernel/sched.
 * resume in entry.S does not create a new stack frame, it
 * just stores the registers %r6-%r15 to the frame given by
 * schedule. We want to return the address of the caller of
 * schedule, so we have to walk the backchain one time to
 * find the frame schedule() store its return address.
 */
unsigned long thread_saved_pc(struct task_struct *tsk)
{
	unsigned long bc;

	bc = *((unsigned long *) tsk->thread.ksp);
#ifndef CONFIG_ARCH_S390X
	return *((unsigned long *) (bc+56));
#else
	return *((unsigned long *) (bc+112));
#endif
}

/*
 * The idle loop on a S390...
 */

void default_idle(void)
{
	psw_t wait_psw;
	unsigned long reg;

        if (need_resched()) {
                schedule();
                return;
        }

	/* 
	 * Wait for external, I/O or machine check interrupt and
	 * switch off machine check bit after the wait has ended.
	 */
	wait_psw.mask = PSW_KERNEL_BITS | PSW_MASK_MCHECK | PSW_MASK_WAIT |
		PSW_MASK_IO | PSW_MASK_EXT;
#ifndef CONFIG_ARCH_S390X
	asm volatile (
		"    basr %0,0\n"
		"0:  la   %0,1f-0b(%0)\n"
		"    st   %0,4(%1)\n"
		"    oi   4(%1),0x80\n"
		"    lpsw 0(%1)\n"
		"1:  la   %0,2f-1b(%0)\n"
		"    st   %0,4(%1)\n"
		"    oi   4(%1),0x80\n"
		"    ni   1(%1),0xf9\n"
		"    lpsw 0(%1)\n"
		"2:"
		: "=&a" (reg) : "a" (&wait_psw) : "memory", "cc" );
#else /* CONFIG_ARCH_S390X */
	asm volatile (
		"    larl  %0,0f\n"
		"    stg   %0,8(%1)\n"
		"    lpswe 0(%1)\n"
		"0:  larl  %0,1f\n"
		"    stg   %0,8(%1)\n"
		"    ni    1(%1),0xf9\n"
		"    lpswe 0(%1)\n"
		"1:"
		: "=&a" (reg) : "a" (&wait_psw) : "memory", "cc" );
#endif /* CONFIG_ARCH_S390X */
}

int cpu_idle(void)
{
	for (;;)
		default_idle();
	return 0;
}

void show_regs(struct pt_regs *regs)
{
	struct task_struct *tsk = current;

        printk("CPU:    %d    %s\n", tsk->thread_info->cpu, print_tainted());
        printk("Process %s (pid: %d, task: %p, ksp: %p)\n",
	       current->comm, current->pid, (void *) tsk,
	       (void *) tsk->thread.ksp);

	show_registers(regs);
	/* Show stack backtrace if pt_regs is from kernel mode */
	if (!(regs->psw.mask & PSW_MASK_PSTATE))
		show_trace(0,(unsigned long *) regs->gprs[15]);
}

extern void kernel_thread_starter(void);

#ifndef CONFIG_ARCH_S390X

__asm__(".align 4\n"
	"kernel_thread_starter:\n"
	"    l     15,0(8)\n"
	"    sr    15,7\n"
	"    stosm 24(15),3\n"
	"    lr    2,10\n"
	"    basr  14,9\n"
	"    sr    2,2\n"
	"    br    11\n");

#else /* CONFIG_ARCH_S390X */

__asm__(".align 4\n"
	"kernel_thread_starter:\n"
	"    lg    15,0(8)\n"
	"    sgr   15,7\n"
	"    stosm 48(15),3\n"
	"    lgr   2,10\n"
	"    basr  14,9\n"
	"    sgr   2,2\n"
	"    br    11\n");

#endif /* CONFIG_ARCH_S390X */

int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));
	regs.psw.mask = PSW_KERNEL_BITS;
	regs.psw.addr = (unsigned long) kernel_thread_starter | PSW_ADDR_AMODE;
	regs.gprs[7] = STACK_FRAME_OVERHEAD;
	regs.gprs[8] = __LC_KERNEL_STACK;
	regs.gprs[9] = (unsigned long) fn;
	regs.gprs[10] = (unsigned long) arg;
	regs.gprs[11] = (unsigned long) do_exit;
	regs.orig_gpr2 = -1;

	/* Ok, create the new process.. */
	return do_fork(flags | CLONE_VM | CLONE_UNTRACED,
		       0, &regs, 0, NULL, NULL);
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
}

void flush_thread(void)
{

        current->used_math = 0;
	clear_tsk_thread_flag(current, TIF_USEDFPU);
}

void release_thread(struct task_struct *dead_task)
{
}

int copy_thread(int nr, unsigned long clone_flags, unsigned long new_stackp,
	unsigned long unused,
        struct task_struct * p, struct pt_regs * regs)
{
        struct stack_frame
          {
            unsigned long back_chain;
            unsigned long eos;
            unsigned long glue1;
            unsigned long glue2;
            unsigned long scratch[2];
            unsigned long gprs[10];    /* gprs 6 -15                       */
            unsigned int  fprs[4];     /* fpr 4 and 6                      */
            unsigned int  empty[4];
            struct pt_regs childregs;
          } *frame;

        frame = ((struct stack_frame *)
		 (THREAD_SIZE + (unsigned long) p->thread_info)) - 1;
        p->thread.ksp = (unsigned long) frame;
	p->set_child_tid = p->clear_child_tid = NULL;
        frame->childregs = *regs;
	frame->childregs.gprs[2] = 0;	/* child returns 0 on fork. */
        frame->childregs.gprs[15] = new_stackp;
        frame->back_chain = frame->eos = 0;

        /* new return point is ret_from_fork */
        frame->gprs[8] = (unsigned long) ret_from_fork;

        /* fake return stack for resume(), don't go back to schedule */
        frame->gprs[9] = (unsigned long) frame;
#ifndef CONFIG_ARCH_S390X
        /*
	 * save fprs to current->thread.fp_regs to merge them with
	 * the emulated registers and then copy the result to the child.
	 */
	save_fp_regs(&current->thread.fp_regs);
	memcpy(&p->thread.fp_regs, &current->thread.fp_regs,
	       sizeof(s390_fp_regs));
        p->thread.user_seg = __pa((unsigned long) p->mm->pgd) | _SEGMENT_TABLE;
	/* Set a new TLS ?  */
	if (clone_flags & CLONE_SETTLS)
		frame->childregs.acrs[0] = regs->gprs[6];
#else /* CONFIG_ARCH_S390X */
	/* Save the fpu registers to new thread structure. */
	save_fp_regs(&p->thread.fp_regs);
        p->thread.user_seg = __pa((unsigned long) p->mm->pgd) | _REGION_TABLE;
	/* Set a new TLS ?  */
	if (clone_flags & CLONE_SETTLS) {
		if (test_thread_flag(TIF_31BIT)) {
			frame->childregs.acrs[0] =
				(unsigned int) regs->gprs[6];
		} else {
			frame->childregs.acrs[0] =
				(unsigned int)(regs->gprs[6] >> 32);
			frame->childregs.acrs[1] =
				(unsigned int) regs->gprs[6];
		}
	}
#endif /* CONFIG_ARCH_S390X */
	/* start new process with ar4 pointing to the correct address space */
	p->thread.ar4 = get_fs().ar4;
        /* Don't copy debug registers */
        memset(&p->thread.per_info,0,sizeof(p->thread.per_info));

        return 0;
}

asmlinkage int sys_fork(struct pt_regs regs)
{
	return do_fork(SIGCHLD, regs.gprs[15], &regs, 0, NULL, NULL);
}

asmlinkage int sys_clone(struct pt_regs regs)
{
        unsigned long clone_flags;
        unsigned long newsp;
	int *parent_tidptr, *child_tidptr;

        clone_flags = regs.gprs[3];
        newsp = regs.orig_gpr2;
	parent_tidptr = (int *) regs.gprs[4];
	child_tidptr = (int *) regs.gprs[5];
        if (!newsp)
                newsp = regs.gprs[15];
        return do_fork(clone_flags & ~CLONE_IDLETASK, newsp, &regs, 0,
		       parent_tidptr, child_tidptr);
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
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD,
		       regs.gprs[15], &regs, 0, NULL, NULL);
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(struct pt_regs regs)
{
        int error;
        char * filename;

        filename = getname((char *) regs.orig_gpr2);
        error = PTR_ERR(filename);
        if (IS_ERR(filename))
                goto out;
        error = do_execve(filename, (char **) regs.gprs[3],
			  (char **) regs.gprs[4], &regs);
	if (error == 0) {
		current->ptrace &= ~PT_DTRACE;
		current->thread.fp_regs.fpc = 0;
		if (MACHINE_HAS_IEEE)
			asm volatile("sfpc %0,%0" : : "d" (0));
	}
        putname(filename);
out:
        return error;
}


/*
 * fill in the FPU structure for a core dump.
 */
int dump_fpu (struct pt_regs * regs, s390_fp_regs *fpregs)
{
#ifndef CONFIG_ARCH_S390X
        /*
	 * save fprs to current->thread.fp_regs to merge them with
	 * the emulated registers and then copy the result to the dump.
	 */
	save_fp_regs(&current->thread.fp_regs);
	memcpy(fpregs, &current->thread.fp_regs, sizeof(s390_fp_regs));
#else /* CONFIG_ARCH_S390X */
	save_fp_regs(fpregs);
#endif /* CONFIG_ARCH_S390X */
	return 1;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{

/* changed the size calculations - should hopefully work better. lbt */
	dump->magic = CMAGIC;
	dump->start_code = 0;
	dump->start_stack = regs->gprs[15] & ~(PAGE_SIZE - 1);
	dump->u_tsize = current->mm->end_code >> PAGE_SHIFT;
	dump->u_dsize = (current->mm->brk + PAGE_SIZE - 1) >> PAGE_SHIFT;
	dump->u_dsize -= dump->u_tsize;
	dump->u_ssize = 0;
	if (dump->start_stack < TASK_SIZE)
		dump->u_ssize = (TASK_SIZE - dump->start_stack) >> PAGE_SHIFT;
	memcpy(&dump->regs, regs, sizeof(s390_regs));
	dump_fpu (regs, &dump->regs.fp_regs);
	dump->regs.per_info = current->thread.per_info;
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
	unsigned long r14, r15, bc;
	unsigned long stack_page;
	int count = 0;
	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;
	stack_page = (unsigned long) p->thread_info;
	r15 = p->thread.ksp;
	if (!stack_page || r15 < stack_page ||
	    r15 >= THREAD_SIZE - sizeof(unsigned long) + stack_page)
		return 0;
	bc = (*(unsigned long *) r15) & PSW_ADDR_INSN;
	do {
		if (bc < stack_page ||
		    bc >= THREAD_SIZE - sizeof(unsigned long) + stack_page)
			return 0;
#ifndef CONFIG_ARCH_S390X
		r14 = (*(unsigned long *) (bc+56)) & PSW_ADDR_INSN;
#else
		r14 = *(unsigned long *) (bc+112);
#endif
		if (r14 < first_sched || r14 >= last_sched)
			return r14;
		bc = (*(unsigned long *) bc) & PSW_ADDR_INSN;
	} while (count++ < 16);
	return 0;
}
#undef last_sched
#undef first_sched

