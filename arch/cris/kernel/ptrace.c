/*
 *  linux/arch/cris/kernel/ptrace.c
 *
 * Parts taken from the m68k port.
 * 
 * Copyright (c) 2000, 2001 Axis Communications AB
 *
 * Authors:   Bjorn Wesen
 *
 * $Log: ptrace.c,v $
 * Revision 1.2  2001/12/18 13:35:20  bjornw
 * Applied the 2.4.13->2.4.16 CRIS patch to 2.5.1 (is a copy of 2.4.15).
 *
 * Revision 1.8  2001/11/12 18:26:21  pkj
 * Fixed compiler warnings.
 *
 * Revision 1.7  2001/09/26 11:53:49  bjornw
 * PTRACE_DETACH works more simple in 2.4.10
 *
 * Revision 1.6  2001/07/25 16:08:47  bjornw
 * PTRACE_ATTACH bulk moved into arch-independant code in 2.4.7
 *
 * Revision 1.5  2001/03/26 14:24:28  orjanf
 * * Changed loop condition.
 * * Added comment documenting non-standard ptrace behaviour.
 *
 * Revision 1.4  2001/03/20 19:44:41  bjornw
 * Use the user_regs macro instead of thread.esp0
 *
 * Revision 1.3  2000/12/18 23:45:25  bjornw
 * Linux/CRIS first version
 *
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>

#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/* determines which bits in DCCR the user has access to. */
/* 1 = access 0 = no access */
#define DCCR_MASK 0x0000001f     /* XNZVC */

/*
 * Get contents of register REGNO in task TASK.
 */
static inline long get_reg(struct task_struct *task, unsigned int regno)
{
	/* USP is a special case, it's not in the pt_regs struct but
	 * in the tasks thread struct
	 */

	if (regno == PT_USP)
		return task->thread.usp;
	else if (regno < PT_MAX)
		return ((unsigned long *)user_regs(task))[regno];
	else
		return 0;
}

/*
 * Write contents of register REGNO in task TASK.
 */
static inline int put_reg(struct task_struct *task, unsigned int regno,
			  unsigned long data)
{
	if (regno == PT_USP)
		task->thread.usp = data;
	else if (regno < PT_MAX)
		((unsigned long *)user_regs(task))[regno] = data;
	else
		return -1;
	return 0;
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure the single step bit is not set.
 */
void ptrace_disable(struct task_struct *child)
{
       /* Todo - pending singlesteps? */
}

/* Note that this implementation of ptrace behaves differently from vanilla
 * ptrace.  Contrary to what the man page says, in the PTRACE_PEEKTEXT,
 * PTRACE_PEEKDATA, and PTRACE_PEEKUSER requests the data variable is not
 * ignored.  Instead, the data variable is expected to point at a location
 * (in user space) where the result of the ptrace call is written (instead of
 * being returned).
 */

asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	int ret;

	lock_kernel();
	ret = -EPERM;
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->ptrace & PT_PTRACED)
			goto out;
		/* set the ptrace bit in the process flags. */
		current->ptrace |= PT_PTRACED;
		ret = 0;
		goto out;
	}
	ret = -ESRCH;
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	if (child)
		get_task_struct(child);
	read_unlock(&tasklist_lock);
	if (!child)
		goto out;
	ret = -EPERM;
	if (pid == 1)		/* you may not mess with init */
		goto out_tsk;
	if (request == PTRACE_ATTACH) {
		ret = ptrace_attach(child);
		goto out_tsk;
	}
	ret = -ESRCH;
	if (!(child->ptrace & PT_PTRACED))
		goto out_tsk;
	if (child->state != TASK_STOPPED) {
		if (request != PTRACE_KILL)
			goto out_tsk;
	}
	if (child->p_pptr != current)
		goto out_tsk;

	switch (request) {
	/* when I and D space are separate, these will need to be fixed. */
		case PTRACE_PEEKTEXT: /* read word at location addr. */ 
		case PTRACE_PEEKDATA: {
			unsigned long tmp;
			int copied;

			copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
			ret = -EIO;
			if (copied != sizeof(tmp))
				break;
			ret = put_user(tmp,(unsigned long *) data);
			break;
		}

		/* read the word at location addr in the USER area. */
		case PTRACE_PEEKUSR: {
			unsigned long tmp;
			
			ret = -EIO;
			if ((addr & 3) || addr < 0 || addr >= sizeof(struct user))
				break;
			
			tmp = 0;  /* Default return condition */
			ret = -EIO;
			if (addr < sizeof(struct pt_regs)) {
				tmp = get_reg(child, addr >> 2);
				ret = put_user(tmp, (unsigned long *)data);
			}
			break;
		}

		/* when I and D space are separate, this will have to be fixed. */
		case PTRACE_POKETEXT: /* write the word at location addr. */
		case PTRACE_POKEDATA:
			ret = 0;
			if (access_process_vm(child, addr, &data, sizeof(data), 1) == sizeof(data))
				break;
			ret = -EIO;
			break;

		case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
			ret = -EIO;
			if ((addr & 3) || addr < 0 || addr >= sizeof(struct user))
				break;

			if (addr < sizeof(struct pt_regs)) {
				addr >>= 2;

				if (addr == PT_DCCR) {
				/* don't allow the tracing process to change stuff like
				 * interrupt enable, kernel/user bit, dma enables etc.
				 */
					data &= DCCR_MASK;
					data |= get_reg(child, PT_DCCR) & ~DCCR_MASK;
				}
				if (put_reg(child, addr, data))
					break;
				ret = 0;
			}
			break;

		case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
		case PTRACE_CONT: /* restart after signal. */
			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				break;
			if (request == PTRACE_SYSCALL)
				child->ptrace |= PT_TRACESYS;
			else
				child->ptrace &= ~PT_TRACESYS;
			child->exit_code = data;
			/* TODO: make sure any pending breakpoint is killed */
			wake_up_process(child);
			ret = 0;
			break;

/*
 * make the child exit.  Best I can do is send it a sigkill. 
 * perhaps it should be put in the status that it wants to 
 * exit.
 */
		case PTRACE_KILL:
			ret = 0;
			if (child->state == TASK_ZOMBIE) /* already dead */
				break;
			child->exit_code = SIGKILL;
			/* TODO: make sure any pending breakpoint is killed */
			wake_up_process(child);
			break;

		case PTRACE_SINGLESTEP: /* set the trap flag. */
			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				break;
			child->ptrace &= ~PT_TRACESYS;

			/* TODO: set some clever breakpoint mechanism... */

			child->exit_code = data;
			/* give it a chance to run. */
			wake_up_process(child);
			ret = 0;
			break;

		case PTRACE_DETACH:
			ret = ptrace_detach(child, data);
			break;

		case PTRACE_GETREGS: { /* Get all gp regs from the child. */
		  	int i;
			unsigned long tmp;
			for (i = 0; i <= PT_MAX; i++) {
				tmp = get_reg(child, i);
				if (put_user(tmp, (unsigned long *) data)) {
					ret = -EFAULT;
					break;
				}
				data += sizeof(long);
			}
			ret = 0;
			break;
		}

		case PTRACE_SETREGS: { /* Set all gp regs in the child. */
			int i;
			unsigned long tmp;
			for (i = 0; i <= PT_MAX; i++) {
				if (get_user(tmp, (unsigned long *) data)) {
					ret = -EFAULT;
					break;
				}
				if (i == PT_DCCR) {
					tmp &= DCCR_MASK;
					tmp |= get_reg(child, PT_DCCR) & ~DCCR_MASK;
				}
				put_reg(child, i, tmp);
				data += sizeof(long);
			}
			ret = 0;
			break;
		}

		default:
			ret = ptrace_request(child, request, addr, data);
			break;
	}
out_tsk:
	free_task_struct(child);
out:
	unlock_kernel();
	return ret;
}

asmlinkage void syscall_trace(void)
{
	if ((current->ptrace & (PT_PTRACED | PT_TRACESYS)) !=
	    (PT_PTRACED | PT_TRACESYS))
		return;
	current->exit_code = SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
					? 0x80 : 0);
	current->state = TASK_STOPPED;
	notify_parent(current, SIGCHLD);
	schedule();
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}
