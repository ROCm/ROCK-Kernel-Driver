/*
 * Copyright (C) 2000-2003, Axis Communications AB.
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
 * Determines which bits in DCCR the user has access to.
 * 1 = access, 0 = no access.
 */
#define DCCR_MASK 0x0000001f     /* XNZVC */

extern inline long get_reg(struct task_struct *, unsigned int);
extern inline long put_reg(struct task_struct *, unsigned int, unsigned long);

/*
 * Called by kernel/ptrace.c when detaching.
 *
 * Make sure the single step bit is not set.
 */
void 
ptrace_disable(struct task_struct *child)
{
       /* Todo - pending singlesteps? */
}

/* 
 * Note that this implementation of ptrace behaves differently from vanilla
 * ptrace.  Contrary to what the man page says, in the PTRACE_PEEKTEXT,
 * PTRACE_PEEKDATA, and PTRACE_PEEKUSER requests the data variable is not
 * ignored.  Instead, the data variable is expected to point at a location
 * (in user space) where the result of the ptrace call is written (instead of
 * being returned).
 */
asmlinkage int 
sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	int ret;

	lock_kernel();
	ret = -EPERM;
	
	if (request == PTRACE_TRACEME) {
		if (current->ptrace & PT_PTRACED)
			goto out;

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
	
	if (pid == 1)		/* Leave the init process alone! */
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
	
	if (child->parent != current)
		goto out_tsk;

	switch (request) {
		/* Read word at location address. */ 
		case PTRACE_PEEKTEXT:
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

		/* Read the word at location address in the USER area. */
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
		
		/* Write the word at location address. */
		case PTRACE_POKETEXT:
		case PTRACE_POKEDATA:
			ret = 0;
			
			if (access_process_vm(child, addr, &data, sizeof(data), 1) == sizeof(data))
				break;
			
			ret = -EIO;
			break;
 
 		/* Write the word at location address in the USER area. */
		case PTRACE_POKEUSR:
			ret = -EIO;
			
			if ((addr & 3) || addr < 0 || addr >= sizeof(struct user))
				break;

			if (addr < sizeof(struct pt_regs)) {
				addr >>= 2;

				if (addr == PT_DCCR) {
					/*
					 * Don't allow the tracing process to
					 * change stuff like interrupt enable,
					 * kernel/user bit, etc.
					 */
					data &= DCCR_MASK;
					data |= get_reg(child, PT_DCCR) & ~DCCR_MASK;
				}
				
				if (put_reg(child, addr, data))
					break;
				
				ret = 0;
			}
			break;

		case PTRACE_SYSCALL:
		case PTRACE_CONT:
			ret = -EIO;
			
			if ((unsigned long) data > _NSIG)
				break;
                        
			if (request == PTRACE_SYSCALL) {
				set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
			}
			else {
				clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
			}
			
			child->exit_code = data;
			
			/* TODO: make sure any pending breakpoint is killed */
			wake_up_process(child);
			ret = 0;
			
			break;
		
 		/* Make the child exit by sending it a sigkill. */
		case PTRACE_KILL:
			ret = 0;
			
			if (child->state == TASK_ZOMBIE)
				break;
			
			child->exit_code = SIGKILL;
			
			/* TODO: make sure any pending breakpoint is killed */
			wake_up_process(child);
			break;

		/* Set the trap flag. */
		case PTRACE_SINGLESTEP:
			ret = -EIO;
			
			if ((unsigned long) data > _NSIG)
				break;
			
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);

			/* TODO: set some clever breakpoint mechanism... */

			child->exit_code = data;
			wake_up_process(child);
			ret = 0;
			break;

		case PTRACE_DETACH:
			ret = ptrace_detach(child, data);
			break;

		/* Get all GP registers from the child. */
		case PTRACE_GETREGS: {
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

		/* Set all GP registers in the child. */
		case PTRACE_SETREGS: {
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
	put_task_struct(child);
out:
	unlock_kernel();
	return ret;
}

void do_syscall_trace(void)
{
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;
	
	if (!(current->ptrace & PT_PTRACED))
		return;
	
	current->exit_code = SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
					? 0x80 : 0);
	
	current->state = TASK_STOPPED;
	notify_parent(current, SIGCHLD);
	schedule();
	
	/*
	 * This isn't the same as continuing with a signal, but it will do for
	 * normal use.
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}
