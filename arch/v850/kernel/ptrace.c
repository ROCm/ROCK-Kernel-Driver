/*
 * arch/v850/kernel/ptrace.c -- `ptrace' system call
 *
 *  Copyright (C) 2002  NEC Corporation
 *  Copyright (C) 2002  Miles Bader <miles@gnu.org>
 *
 * Derived from arch/mips/kernel/ptrace.c:
 *
 *  Copyright (C) 1992 Ross Biro
 *  Copyright (C) Linus Torvalds
 *  Copyright (C) 1994, 95, 96, 97, 98, 2000 Ralf Baechle
 *  Copyright (C) 1996 David S. Miller
 *  Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 *  Copyright (C) 1999 MIPS Technologies, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/ptrace.h>

#include <asm/errno.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/uaccess.h>

int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	int rval;

	lock_kernel();

#if 0
	printk("ptrace(r=%d,pid=%d,addr=%08lx,data=%08lx)\n",
	       (int) request, (int) pid, (unsigned long) addr,
	       (unsigned long) data);
#endif

	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->ptrace & PT_PTRACED) {
			rval = -EPERM;
			goto out;
		}
		/* set the ptrace bit in the process flags. */
		current->ptrace |= PT_PTRACED;
		rval = 0;
		goto out;
	}
	rval = -ESRCH;
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	if (child)
		get_task_struct(child);
	read_unlock(&tasklist_lock);
	if (!child)
		goto out;

	rval = -EPERM;
	if (pid == 1)		/* you may not mess with init */
		goto out;

	if (request == PTRACE_ATTACH) {
		rval = ptrace_attach(child);
		goto out_tsk;
	}
	rval = -ESRCH;
	if (!(child->ptrace & PT_PTRACED))
		goto out_tsk;
	if (child->state != TASK_STOPPED) {
		if (request != PTRACE_KILL)
			goto out_tsk;
	}
	if (child->parent != current)
		goto out_tsk;

	switch (request) {
	case PTRACE_PEEKTEXT: /* read word at location addr. */ 
	case PTRACE_PEEKDATA:{
		unsigned long tmp;
		int copied;

		copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
		rval = -EIO;
		if (copied != sizeof(tmp))
			break;
		rval = put_user(tmp,(unsigned long *) data);

		goto out;
		}

	/* Read the word at location addr in the USER area.  */
	case PTRACE_PEEKUSR: 
		if (addr >= 0 && addr < PT_SIZE && (addr & 0x3) == 0) {
			struct pt_regs *regs = task_regs (child);
			unsigned long val =
				*(unsigned long *)((char *)regs + addr);
			rval = put_user (val, (unsigned long *)data);
		} else {
			rval = 0;
			rval = -EIO;			
		}
		goto out;

	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		rval = 0;
		if (access_process_vm(child, addr, &data, sizeof(data), 1)
		    == sizeof(data))
			break;
		rval = -EIO;
		goto out;

	case PTRACE_POKEUSR:
		if (addr >= 0 && addr < PT_SIZE && (addr & 0x3) == 0) {
			struct pt_regs *regs = task_regs (child);
			unsigned long *loc =
				(unsigned long *)((char *)regs + addr);
			*loc = data;
		} else {
			rval = 0;
			rval = -EIO;			
		}
		goto out;

	case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
	case PTRACE_CONT: /* rvaltart after signal. */
		rval = -EIO;
		if ((unsigned long) data > _NSIG)
			break;
		if (request == PTRACE_SYSCALL)
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		else
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		child->exit_code = data;
		wake_up_process(child);
		rval = 0;
		break;

	/*
	 * make the child exit.  Best I can do is send it a sigkill. 
	 * perhaps it should be put in the status that it wants to 
	 * exit.
	 */
	case PTRACE_KILL:
		rval = 0;
		if (child->state == TASK_ZOMBIE)	/* already dead */
			break;
		child->exit_code = SIGKILL;
		wake_up_process(child);
		break;

	case PTRACE_DETACH: /* detach a process that was attached. */
		rval = ptrace_detach(child, data);
		break;

	default:
		rval = -EIO;
		goto out;
	}

out_tsk:
	put_task_struct(child);
out:
	unlock_kernel();
	return rval;
}

asmlinkage void syscall_trace(void)
{
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;
	if (!(current->ptrace & PT_PTRACED))
		return;
	/* The 0x80 provides a way for the tracing parent to distinguish
	   between a syscall stop and SIGTRAP delivery */
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

void ptrace_disable (struct task_struct *child)
{
	/* nothing to do */
}
