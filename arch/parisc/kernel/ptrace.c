/*
 * Kernel support for the ptrace() and syscall tracing interfaces.
 *
 * Copyright (C) 2000 Hewlett-Packard Co, Linuxcare Inc.
 * Copyright (C) 2000 Matthew Wilcox <matthew@wil.cx>
 * Copyright (C) 2000 David Huggins-Daines <dhd@debian.org>
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
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/offset.h>

/* These are used in entry.S, syscall_restore_rfi.  We need to record the
 * current stepping mode somewhere other than in PSW, because there is no
 * concept of saving and restoring the users PSW over a syscall.  We choose
 * to use these two bits in task->ptrace.  These bits must not clash with
 * any PT_* defined in include/linux/sched.h, and must match with the bit
 * tests in entry.S
 */
#define PT_SINGLESTEP	0x10000
#define PT_BLOCKSTEP	0x20000

long sys_ptrace(long request, pid_t pid, long addr, long data)
{
	struct task_struct *child;
	long ret;

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
	if (pid == 1)		/* no messing around with init! */
		goto out_tsk;

	if (request == PTRACE_ATTACH) {
		if (child == current)
			goto out_tsk;
		if ((!child->dumpable ||
		    (current->uid != child->euid) ||
		    (current->uid != child->suid) ||
		    (current->uid != child->uid) ||
	 	    (current->gid != child->egid) ||
	 	    (current->gid != child->sgid) ||
	 	    (!cap_issubset(child->cap_permitted, current->cap_permitted)) ||
	 	    (current->gid != child->gid)) && !capable(CAP_SYS_PTRACE))
			goto out_tsk;
		/* the same process cannot be attached many times */
		if (child->ptrace & PT_PTRACED)
			goto out_tsk;
		child->ptrace |= PT_PTRACED;
		if (child->p_pptr != current) {
			unsigned long flags;

			write_lock_irqsave(&tasklist_lock, flags);
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
			write_unlock_irqrestore(&tasklist_lock, flags);
		}
		send_sig(SIGSTOP, child, 1);
		ret = 0;
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
	case PTRACE_PEEKTEXT: /* read word at location addr. */ 
	case PTRACE_PEEKDATA: {
		unsigned long tmp;
		int copied;

		copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
		ret = -EIO;
		if (copied != sizeof(tmp))
			goto out_tsk;
		ret = put_user(tmp,(unsigned long *) data);
		goto out_tsk;
	}

	/* when I and D space are separate, this will have to be fixed. */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = 0;
		if (access_process_vm(child, addr, &data, sizeof(data), 1) == sizeof(data))
			goto out_tsk;
		ret = -EIO;
		goto out_tsk;

	/* Read the word at location addr in the USER area.  This will need
	   to change when the kernel no longer saves all regs on a syscall. */
	case PTRACE_PEEKUSR: {
		unsigned long tmp;

		ret = -EIO;
		if ((addr & 3) || (unsigned long) addr >= sizeof(struct pt_regs))
			goto out_tsk;

		tmp = *(unsigned long *) ((char *) task_regs(child) + addr);
		ret = put_user(tmp, (unsigned long *) data);
		goto out_tsk;
	}

	/* Write the word at location addr in the USER area.  This will need
	   to change when the kernel no longer saves all regs on a syscall.
	   FIXME.  There is a problem at the moment in that r3-r18 are only
	   saved if the process is ptraced on syscall entry, and even then
	   those values are overwritten by actual register values on syscall
	   exit. */
	case PTRACE_POKEUSR:
		ret = -EIO;
		if ((addr & 3) || (unsigned long) addr >= sizeof(struct pt_regs))
			goto out_tsk;
		/* XXX This test probably needs adjusting.  We probably want to
		 * allow writes to some bits of PSW, and may want to block writes
		 * to (some) space registers.  Some register values written here
		 * may be ignored in entry.S:syscall_restore_rfi; e.g. iaoq is
		 * written with r31/r31+4, and not with the values in pt_regs.
		 */
		/* Allow writing of gr1-gr31, fr*, sr*, iasq*, iaoq*, sar */
		if (addr == PT_PSW || (addr > PT_IAOQ1 && addr != PT_SAR))
			goto out_tsk;

		*(unsigned long *) ((char *) task_regs(child) + addr) = data;
		ret = 0;
		goto out_tsk;

	case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
	case PTRACE_CONT:
		ret = -EIO;
		if ((unsigned long) data > _NSIG)
			goto out_tsk;
		child->ptrace &= ~(PT_SINGLESTEP|PT_BLOCKSTEP);
		if (request == PTRACE_SYSCALL)
			child->ptrace |= PT_TRACESYS;
		else
			child->ptrace &= ~PT_TRACESYS;
		child->exit_code = data;
		goto out_wake_notrap;

	case PTRACE_KILL:
		/*
		 * make the child exit.  Best I can do is send it a
		 * sigkill.  perhaps it should be put in the status
		 * that it wants to exit.
		 */
		if (child->state == TASK_ZOMBIE)	/* already dead */
			goto out_tsk;
		child->exit_code = SIGKILL;
		goto out_wake_notrap;

	case PTRACE_SINGLEBLOCK:
		ret = -EIO;
		if ((unsigned long) data > _NSIG)
			goto out_tsk;
		child->ptrace &= ~(PT_TRACESYS|PT_SINGLESTEP);
		child->ptrace |= PT_BLOCKSTEP;
		child->exit_code = data;

		/* Enable taken branch trap. */
		pa_psw(child)->r = 0;
		pa_psw(child)->t = 1;
		pa_psw(child)->h = 0;
		pa_psw(child)->l = 0;
		goto out_wake;

	case PTRACE_SINGLESTEP:
		ret = -EIO;
		if ((unsigned long) data > _NSIG)
			goto out_tsk;
		child->ptrace &= ~(PT_TRACESYS|PT_BLOCKSTEP);
		child->ptrace |= PT_SINGLESTEP;
		child->exit_code = data;

		if (pa_psw(child)->n) {
			struct siginfo si;

			/* Nullified, just crank over the queue. */
			task_regs(child)->iaoq[0] = task_regs(child)->iaoq[1];
			task_regs(child)->iasq[0] = task_regs(child)->iasq[1];
			task_regs(child)->iaoq[1] = task_regs(child)->iaoq[0] + 4;
			pa_psw(child)->n = 0;
			pa_psw(child)->x = 0;
			pa_psw(child)->y = 0;
			pa_psw(child)->z = 0;
			pa_psw(child)->b = 0;
			pa_psw(child)->r = 0;
			pa_psw(child)->t = 0;
			pa_psw(child)->h = 0;
			pa_psw(child)->l = 0;
			/* Don't wake up the child, but let the
			   parent know something happened. */
			si.si_code = TRAP_TRACE;
			si.si_addr = (void *) (task_regs(child)->iaoq[0] & ~3);
			si.si_signo = SIGTRAP;
			si.si_errno = 0;
			force_sig_info(SIGTRAP, &si, child);
			//notify_parent(child, SIGCHLD);
			//ret = 0;
			goto out_wake;
		}

		/* Enable recovery counter traps.  The recovery counter
		 * itself will be set to zero on a task switch.  If the
		 * task is suspended on a syscall then the syscall return
		 * path will overwrite the recovery counter with a suitable
		 * value such that it traps once back in user space.  We
		 * disable interrupts in the childs PSW here also, to avoid
		 * interrupts while the recovery counter is decrementing.
		 */
		pa_psw(child)->r = 1;
		pa_psw(child)->t = 0;
		pa_psw(child)->h = 0;
		pa_psw(child)->l = 0;
		/* give it a chance to run. */
		goto out_wake;

	case PTRACE_DETACH:
		ret = -EIO;
		if ((unsigned long) data > _NSIG)
			goto out_tsk;
		child->ptrace &= ~(PT_PTRACED|PT_TRACESYS|PT_SINGLESTEP|PT_BLOCKSTEP);
		child->exit_code = data;
		write_lock_irq(&tasklist_lock);
		REMOVE_LINKS(child);
		child->p_pptr = child->p_opptr;
		SET_LINKS(child);
		write_unlock_irq(&tasklist_lock);
		goto out_wake_notrap;

	default:
		ret = -EIO;
		goto out_tsk;
	}

out_wake_notrap:
	/* make sure the trap bits are not set */
	pa_psw(child)->r = 0;
	pa_psw(child)->t = 0;
	pa_psw(child)->h = 0;
	pa_psw(child)->l = 0;
out_wake:
	wake_up_process(child);
	ret = 0;
out_tsk:
	free_task_struct(child);
out:
	unlock_kernel();
	return ret;
}

void syscall_trace(void)
{
	if ((current->ptrace & (PT_PTRACED|PT_TRACESYS)) !=
			(PT_PTRACED|PT_TRACESYS))
		return;
	current->exit_code = SIGTRAP;
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
