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
 * Return saved PC of a blocked thread. used in kernel/sched
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
	return *((unsigned long *) (bc+112));
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
	 * switch of machine check bit after the wait has ended.
	 */
	wait_psw.mask = _WAIT_PSW_MASK;
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
}

int cpu_idle(void)
{
	for (;;)
		default_idle();
	return 0;
}

extern void show_registers(struct pt_regs *regs);
extern void show_trace(unsigned long *sp);

void show_regs(struct pt_regs *regs)
{
	struct task_struct *tsk = current;

        printk("CPU:    %d    %s\n", tsk->thread_info->cpu, print_tainted());
        printk("Process %s (pid: %d, task: %016lx, ksp: %016lx)\n",
	       current->comm, current->pid, (unsigned long) tsk,
	       tsk->thread.ksp);

	show_registers(regs);
	/* Show stack backtrace if pt_regs is from kernel mode */
	if (!(regs->psw.mask & PSW_PROBLEM_STATE))
		show_trace((unsigned long *) regs->gprs[15]);
}

extern void kernel_thread_starter(void);
__asm__(".align 4\n"
	"kernel_thread_starter:\n"
	"    lg    15,0(8)\n"
	"    sgr   15,7\n"
	"    stosm 48(15),3\n"
	"    lgr   2,10\n"
	"    basr  14,9\n"
	"    sgr   2,2\n"
	"    br    11\n");

int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	struct task_struct *p;
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));
	regs.psw.mask = _SVC_PSW_MASK;
	regs.psw.addr = (__u64) kernel_thread_starter;
	regs.gprs[7] = STACK_FRAME_OVERHEAD;
	regs.gprs[8] = __LC_KERNEL_STACK;
	regs.gprs[9] = (unsigned long) fn;
	regs.gprs[10] = (unsigned long) arg;
	regs.gprs[11] = (unsigned long) do_exit;
	regs.orig_gpr2 = -1;

	/* Ok, create the new process.. */
	p = do_fork(flags | CLONE_VM, 0, &regs, 0, NULL);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;
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
            unsigned long fprs[2];     /* fpr 4 and 6                      */
            unsigned long empty[2];
            struct pt_regs childregs;
          } *frame;

        frame = ((struct stack_frame *)
		 (THREAD_SIZE + (unsigned long) p->thread_info)) - 1;
        p->thread.ksp = (unsigned long) frame;
        frame->childregs = *regs;
	frame->childregs.gprs[2] = 0;	/* child returns 0 on fork. */
        frame->childregs.gprs[15] = new_stackp;
        frame->back_chain = frame->eos = 0;

        /* new return point is ret_from_fork */
        frame->gprs[8] = (unsigned long) ret_from_fork;

        /* fake return stack for resume(), don't go back to schedule */
        frame->gprs[9] = (unsigned long) frame;
        /* save fprs */
	save_fp_regs(&p->thread.fp_regs);
        p->thread.user_seg = __pa((unsigned long) p->mm->pgd) | _REGION_TABLE;
	/* start new process with ar4 pointing to the correct address space */
	p->thread.ar4 = get_fs().ar4;
        /* Don't copy debug registers */
        memset(&p->thread.per_info,0,sizeof(p->thread.per_info));
        return 0;
}

asmlinkage int sys_fork(struct pt_regs regs)
{
	struct task_struct *p;
        p = do_fork(SIGCHLD, regs.gprs[15], &regs, 0, NULL);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;
}

asmlinkage int sys_clone(struct pt_regs regs)
{
        unsigned long clone_flags;
        unsigned long newsp;
	struct task_struct *p;
	int *user_tid;

        clone_flags = regs.gprs[3];
        newsp = regs.orig_gpr2;
	user_tid = (int *) regs.gprs[4];
        if (!newsp)
                newsp = regs.gprs[15];
        p = do_fork(clone_flags & ~CLONE_IDLETASK, newsp, &regs, 0, user_tid);
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
	p = do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD,
		    regs.gprs[15], &regs, 0, NULL);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;
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
        error = do_execve(filename, (char **) regs.gprs[3], (char **) regs.gprs[4], &regs);
	if (error == 0)
	{
		current->ptrace &= ~PT_DTRACE;
		current->thread.fp_regs.fpc=0;
		__asm__ __volatile__
		        ("sr  0,0\n\t"
		         "sfpc 0,0\n\t"
			 : : :"0");
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
	save_fp_regs(fpregs);
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
	dump->u_tsize = ((unsigned long) current->mm->end_code) >> PAGE_SHIFT;
	dump->u_dsize = ((unsigned long) (current->mm->brk + (PAGE_SIZE-1))) >> PAGE_SHIFT;
	dump->u_dsize -= dump->u_tsize;
	dump->u_ssize = 0;
	if (dump->start_stack < TASK_SIZE)
		dump->u_ssize = ((unsigned long) (TASK_SIZE - dump->start_stack)) >> PAGE_SHIFT;
	memcpy(&dump->regs.gprs[0],regs,sizeof(s390_regs));
	dump_fpu (regs, &dump->regs.fp_regs);
	memcpy(&dump->regs.per_info,&current->thread.per_info,sizeof(per_struct));
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
        stack_page = (unsigned long) p;
        r15 = p->thread.ksp;
        if (!stack_page || r15 < stack_page || r15 >= 16380+stack_page)
                return 0;
        bc = *(unsigned long *) r15;
        do {
                if (bc < stack_page || bc >= 16380+stack_page)
                        return 0;
                r14 = *(unsigned long *) (bc+112);
                if (r14 < first_sched || r14 >= last_sched)
                        return r14;
                bc = *(unsigned long *) bc;
        } while (count++ < 16);
        return 0;
}
#undef last_sched
#undef first_sched

