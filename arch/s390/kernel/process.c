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

#define __KERNEL_SYSCALLS__
#include <stdarg.h>

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
#include <asm/misc390.h>
#include <asm/irq.h>

spinlock_t semaphore_wake_lock = SPIN_LOCK_UNLOCKED;

asmlinkage void ret_from_fork(void) __asm__("ret_from_fork");

/*
 * The idle loop on a S390...
 */

static psw_t wait_psw;

int cpu_idle(void *unused)
{
	/* endless idle loop with no priority at all */
        init_idle();
	current->nice = 20;
	current->counter = -100;
	wait_psw.mask = _WAIT_PSW_MASK;
	wait_psw.addr = (unsigned long) &&idle_wakeup | 0x80000000L;
	while(1) {
                if (softirq_active(smp_processor_id()) &
		    softirq_mask(smp_processor_id())) {
                        do_softirq();
                        __sti();
                        if (!current->need_resched)
                                continue;
                }
                if (current->need_resched) {
                        schedule();
                        check_pgt_cache();
                        continue;
                }

		/* load wait psw */
		asm volatile (
                        "lpsw %0"
                        : : "m" (wait_psw) );
idle_wakeup:
	}
}

/*
  As all the register will only be made displayable to the root
  user ( via printk ) or checking if the uid of the user is 0 from
  the /proc filesystem please god this will be secure enough DJB.
  The lines are given one at a time so as not to chew stack space in
  printk on a crash & also for the proc filesystem when you get
  0 returned you know you've got all the lines
 */

static int sprintf_regs(int line, char *buff, struct task_struct *task, struct pt_regs *regs)
{
	int linelen=0;
	int regno,chaincnt;
	u32 backchain,prev_backchain,endchain;
	u32 ksp = 0;
	char *mode = "???";

	enum
	{
		sp_linefeed,
		sp_psw,
		sp_ksp,
		sp_gprs,
		sp_gprs1,
		sp_gprs2,
		sp_gprs3,
		sp_gprs4,
		sp_acrs,
		sp_acrs1,
		sp_acrs2,
		sp_acrs3,
		sp_acrs4,
		sp_kern_backchain,
		sp_kern_backchain1
	};

	if (task)
		ksp = task->thread.ksp;
	if (regs && !(regs->psw.mask & PSW_PROBLEM_STATE))
		ksp = regs->gprs[15];

	if (regs)
		mode = (regs->psw.mask & PSW_PROBLEM_STATE)?
		       "User" : "Kernel";

	switch(line)
	{
	case sp_linefeed: 
		linelen=sprintf(buff,"\n");
		break;
	case sp_psw:
		if(regs)
			linelen=sprintf(buff, "%s PSW:    %08lx %08lx\n", mode,
				(unsigned long) regs->psw.mask,
				(unsigned long) regs->psw.addr);
		else
			linelen=sprintf(buff,"pt_regs=NULL some info unavailable\n");
		break;
	case sp_ksp:
		linelen=sprintf(&buff[linelen],
				"task: %08x ksp: %08x pt_regs: %08x\n",
				(addr_t)task, (addr_t)ksp, (addr_t)regs);
		break;
	case sp_gprs:
		if(regs)
			linelen=sprintf(buff, "%s GPRS:\n", mode);
		break;
	case sp_gprs1 ... sp_gprs4:
		if(regs)
		{
			regno=(line-sp_gprs1)*4;
			linelen=sprintf(buff,"%08x  %08x  %08x  %08x\n",
					regs->gprs[regno], 
					regs->gprs[regno+1],
					regs->gprs[regno+2],
					regs->gprs[regno+3]);
		}
		break;
	case sp_acrs:
		if(regs)
			linelen=sprintf(buff, "%s ACRS:\n", mode);
		break;	
        case sp_acrs1 ... sp_acrs4:
		if(regs)
		{
			regno=(line-sp_acrs1)*4;
			linelen=sprintf(buff,"%08x  %08x  %08x  %08x\n",
					regs->acrs[regno],
					regs->acrs[regno+1],
					regs->acrs[regno+2],
					regs->acrs[regno+3]);
		}
		break;
	case sp_kern_backchain:
		if (regs && (regs->psw.mask & PSW_PROBLEM_STATE))
			break;	
		if (ksp)
			linelen=sprintf(buff, "Kernel BackChain  CallChain\n");
		break;
	default:
		if (ksp)
		{
			
			backchain=ksp&PSW_ADDR_MASK;
			endchain=((backchain&(-8192))+8192);
			prev_backchain=backchain-1;
			line-=sp_kern_backchain1;
			for(chaincnt=0;;chaincnt++)
			{
				if((backchain==0)||(backchain>=endchain)
				   ||(chaincnt>=8)||(prev_backchain>=backchain))
					break;
				if(chaincnt==line)
				{
					linelen+=sprintf(&buff[linelen],"       %08x   [<%08lx>]\n",
							 backchain,
							 *(u32 *)(backchain+56)&PSW_ADDR_MASK);
					break;
				}
				prev_backchain=backchain;
				backchain=(*((u32 *)backchain))&PSW_ADDR_MASK;
			}
		}
	}
	return(linelen);
}


void show_regs(struct pt_regs *regs)
{
	char buff[80];
	int line;

        printk("CPU:    %d\n",smp_processor_id());
        printk("Process %s (pid: %d, stackpage=%08X)\n",
                current->comm, current->pid, 4096+(addr_t)current);
	
	for (line = 0; sprintf_regs(line, buff, current, regs); line++)
		printk(buff);
}

char *task_show_regs(struct task_struct *task, char *buffer)
{
	int line, len;

	for (line = 0; ; line++)
	{
		len = sprintf_regs(line, buffer, task, task->thread.regs);
		if (!len) break;
		buffer += len;
	}
	return buffer;
}


int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
        int clone_arg = flags | CLONE_VM;
        int retval;

        __asm__ __volatile__(
                "     sr    2,2\n"
                "     lr    3,%1\n"
                "     l     4,%6\n"     /* load kernel stack ptr of parent */
                "     svc   %b2\n"                     /* Linux system call*/
                "     cl    4,%6\n"    /* compare ksp's: child or parent ? */
                "     je    0f\n"                          /* parent - jump*/
                "     l     15,%6\n"            /* fix kernel stack pointer*/
                "     ahi   15,%7\n"
                "     xc    0(96,15),0(15)\n"           /* clear save area */
                "     lr    2,%4\n"                        /* load argument*/
                "     lr    14,%5\n"                      /* get fn-pointer*/
                "     basr  14,14\n"                             /* call fn*/
                "     svc   %b3\n"                     /* Linux system call*/
                "0:   lr    %0,2"
                : "=a" (retval)
                : "d" (clone_arg), "i" (__NR_clone), "i" (__NR_exit),
                  "d" (arg), "d" (fn), "i" (__LC_KERNEL_STACK) , "i" (-STACK_FRAME_OVERHEAD)
                : "2", "3", "4" );
        return retval;
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
        current->flags &= ~PF_USEDFPU;
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
            unsigned long fprs[4];     /* fpr 4 and 6                      */
            unsigned long empty[4];
#if CONFIG_REMOTE_DEBUG
	    gdb_pt_regs childregs;
#else
            pt_regs childregs;
#endif
            __u32   pgm_old_ilc;       /* single step magic from entry.S */
            __u32   pgm_svc_step;
          } *frame;

        frame = (struct stack_frame *) (2*PAGE_SIZE + (unsigned long) p) -1;
        frame = (struct stack_frame *) (((unsigned long) frame)&-8L);
        p->thread.regs = &frame->childregs;
        p->thread.ksp = (unsigned long) frame;
        frame->childregs = *regs;
        frame->childregs.gprs[15] = new_stackp;
        frame->eos = 0;

        /* new return point is ret_from_sys_call */
        frame->gprs[8] = ((unsigned long) &ret_from_fork) | 0x80000000;

        /* fake return stack for resume(), don't go back to schedule */
        frame->gprs[9]  = (unsigned long) frame;
	frame->pgm_svc_step = 0; /* Nope we aren't single stepping an svc */
        /* save fprs, if used in last task */
	save_fp_regs(&p->thread.fp_regs);
        p->thread.user_seg = __pa((unsigned long) p->mm->pgd) | _SEGMENT_TABLE;
        p->thread.fs = USER_DS;
        /* Don't copy debug registers */
        memset(&p->thread.per_info,0,sizeof(p->thread.per_info));
        return 0;
}

asmlinkage int sys_fork(struct pt_regs regs)
{
        int ret;

        lock_kernel();
        ret = do_fork(SIGCHLD, regs.gprs[15], &regs, 0);
        unlock_kernel();
        return ret;
}

asmlinkage int sys_clone(struct pt_regs regs)
{
        unsigned long clone_flags;
        unsigned long newsp;
        int ret;

        lock_kernel();
        clone_flags = regs.gprs[3];
        newsp = regs.orig_gpr2;
        if (!newsp)
                newsp = regs.gprs[15];
        ret = do_fork(clone_flags, newsp, &regs, 0);
        unlock_kernel();
        return ret;
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
                       regs.gprs[15], &regs, 0);
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
		if(MACHINE_HAS_IEEE)
		{
			__asm__ __volatile__
			("sr  0,0\n\t"
			 "sfpc 0,0\n\t"
				:
			        :
                                :"0");
		}
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
        if (!stack_page || r15 < stack_page || r15 >= 8188+stack_page)
                return 0;
        bc = (*(unsigned long *) r15) & 0x7fffffff;
	do {
                if (bc < stack_page || bc >= 8188+stack_page)
                        return 0;
		r14 = (*(unsigned long *) (bc+56)) & 0x7fffffff;
		if (r14 < first_sched || r14 >= last_sched)
			return r14;
		bc = (*(unsigned long *) bc) & 0x7fffffff;
	} while (count++ < 16);
	return 0;
}
#undef last_sched
#undef first_sched

/*
 * This should be safe even if called from tq_scheduler
 * A typical mask would be sigmask(SIGKILL)|sigmask(SIGINT)|sigmask(SIGTERM) or 0.
 *
 */
void s390_daemonize(char *name,unsigned long mask,int use_init_fs)
{
	struct fs_struct *fs;
	extern struct task_struct *child_reaper;
	struct task_struct *this_process=current;
	
	/*
	 * If we were started as result of loading a module, close all of the
	 * user space pages.  We don't need them, and if we didn't close them
	 * they would be locked into memory.
	 */
	exit_mm(current);

	this_process->session = 1;
	this_process->pgrp = 1;
	if(name)
	{
		strncpy(current->comm,name,15);
		current->comm[15]=0;
	}
	else
		current->comm[0]=0;
	/* set signal mask to what we want to respond */
        siginitsetinv(&current->blocked,mask);
	/* exit_signal isn't set up */
        /* if we inherit from cpu idle  */
	this_process->exit_signal=SIGCHLD;
	/* if priority=0 schedule can go into a tight loop */
	this_process->policy= SCHED_OTHER;
	/* nice goes priority=20-nice; */
	this_process->nice=10;
	if(use_init_fs)
	{
		exit_fs(this_process);	/* current->fs->count--; */
		fs = init_task.fs;
		current->fs = fs;
		atomic_inc(&fs->count);
		exit_files(current);
	}
	write_lock_irq(&tasklist_lock);
	/* We want init as our parent */
	REMOVE_LINKS(this_process);
	this_process->p_opptr=this_process->p_pptr=child_reaper;
	SET_LINKS(this_process);
	write_unlock_irq(&tasklist_lock);
}
