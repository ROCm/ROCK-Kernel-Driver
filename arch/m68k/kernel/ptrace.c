/*
 *  linux/arch/m68k/kernel/ptrace.c
 *
 *  Copyright (C) 1994 by Hamish Macdonald
 *  Taken from linux/kernel/ptrace.c and modified for M680x0.
 *  linux/kernel/ptrace.c is by Ross Biro 1/23/92, edited by Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of
 * this archive for more details.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/config.h>

#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/* determines which bits in the SR the user has access to. */
/* 1 = access 0 = no access */
#define SR_MASK 0x001f

/* sets the trace bits. */
#define TRACE_BITS 0x8000

/* Find the stack offset for a register, relative to thread.esp0. */
#define PT_REG(reg)	((long)&((struct pt_regs *)0)->reg)
#define SW_REG(reg)	((long)&((struct switch_stack *)0)->reg \
			 - sizeof(struct switch_stack))
/* Mapping from PT_xxx to the stack offset at which the register is
   saved.  Notice that usp has no stack-slot and needs to be treated
   specially (see get_reg/put_reg below). */
static int regoff[] = {
	PT_REG(d1), PT_REG(d2), PT_REG(d3), PT_REG(d4),
	PT_REG(d5), SW_REG(d6), SW_REG(d7), PT_REG(a0),
	PT_REG(a1), PT_REG(a2), SW_REG(a3), SW_REG(a4),
	SW_REG(a5), SW_REG(a6), PT_REG(d0), -1,
	PT_REG(orig_d0), PT_REG(sr), PT_REG(pc),
};

/*
 * Get contents of register REGNO in task TASK.
 */
static inline long get_reg(struct task_struct *task, int regno)
{
	unsigned long *addr;

	if (regno == PT_USP)
		addr = &task->thread.usp;
	else if (regno < sizeof(regoff)/sizeof(regoff[0]))
		addr = (unsigned long *)(task->thread.esp0 + regoff[regno]);
	else
		return 0;
	return *addr;
}

/*
 * Write contents of register REGNO in task TASK.
 */
static inline int put_reg(struct task_struct *task, int regno,
			  unsigned long data)
{
	unsigned long *addr;

	if (regno == PT_USP)
		addr = &task->thread.usp;
	else if (regno < sizeof(regoff)/sizeof(regoff[0]))
		addr = (unsigned long *) (task->thread.esp0 + regoff[regno]);
	else
		return -1;
	*addr = data;
	return 0;
}

asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	unsigned long flags;
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
	read_unlock(&tasklist_lock);	/* FIXME!!! */
	if (!child)
		goto out;
	ret = -EPERM;
	if (pid == 1)		/* you may not mess with init */
		goto out;
	if (request == PTRACE_ATTACH) {
		if (child == current)
			goto out;
		if ((!child->dumpable ||
		    (current->uid != child->euid) ||
		    (current->uid != child->suid) ||
		    (current->uid != child->uid) ||
	 	    (current->gid != child->egid) ||
		    (current->gid != child->sgid) ||
	 	    (!cap_issubset(child->cap_permitted, current->cap_permitted)) ||
	 	    (current->gid != child->gid)) && !capable(CAP_SYS_PTRACE))
			goto out;
		/* the same process cannot be attached many times */
		if (child->ptrace & PT_PTRACED)
			goto out;
		child->ptrace |= PT_PTRACED;

		write_lock_irqsave(&tasklist_lock, flags);
		if (child->p_pptr != current) {
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
		}
		write_unlock_irqrestore(&tasklist_lock, flags);

		send_sig(SIGSTOP, child, 1);
		ret = 0;
		goto out;
	}
	ret = -ESRCH;
	if (!(child->ptrace & PT_PTRACED))
		goto out;
	if (child->state != TASK_STOPPED) {
		if (request != PTRACE_KILL)
			goto out;
	}
	if (child->p_pptr != current)
		goto out;

	switch (request) {
	/* when I and D space are separate, these will need to be fixed. */
		case PTRACE_PEEKTEXT: /* read word at location addr. */ 
		case PTRACE_PEEKDATA: {
			unsigned long tmp;
			int copied;

			copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
			ret = -EIO;
			if (copied != sizeof(tmp))
				goto out;
			ret = put_user(tmp,(unsigned long *) data);
			goto out;
		}

	/* read the word at location addr in the USER area. */
		case PTRACE_PEEKUSR: {
			unsigned long tmp;
			
			ret = -EIO;
			if ((addr & 3) || addr < 0 || addr >= sizeof(struct user))
				goto out;
			
			tmp = 0;  /* Default return condition */
			addr = addr >> 2; /* temporary hack. */
			ret = -EIO;
			if (addr < 19) {
				tmp = get_reg(child, addr);
				if (addr == PT_SR)
					tmp >>= 16;
			} else if (addr >= 21 && addr < 49) {
				tmp = child->thread.fp[addr - 21];
#ifdef CONFIG_M68KFPU_EMU
				/* Convert internal fpu reg representation
				 * into long double format
				 */
				if (FPU_IS_EMU && (addr < 45) && !(addr % 3))
					tmp = ((tmp & 0xffff0000) << 15) |
					      ((tmp & 0x0000ffff) << 16);
#endif
			} else
				goto out;
			ret = put_user(tmp,(unsigned long *) data);
			goto out;
		}

      /* when I and D space are separate, this will have to be fixed. */
		case PTRACE_POKETEXT: /* write the word at location addr. */
		case PTRACE_POKEDATA:
			ret = 0;
			if (access_process_vm(child, addr, &data, sizeof(data), 1) == sizeof(data))
				goto out;
			ret = -EIO;
			goto out;

		case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
			ret = -EIO;
			if ((addr & 3) || addr < 0 || addr >= sizeof(struct user))
				goto out;

			addr = addr >> 2; /* temporary hack. */
			    
			if (addr == PT_SR) {
				data &= SR_MASK;
				data <<= 16;
				data |= get_reg(child, PT_SR) & ~(SR_MASK << 16);
			}
			if (addr < 19) {
				if (put_reg(child, addr, data))
					goto out;
				ret = 0;
				goto out;
			}
			if (addr >= 21 && addr < 48)
			{
#ifdef CONFIG_M68KFPU_EMU
				/* Convert long double format
				 * into internal fpu reg representation
				 */
				if (FPU_IS_EMU && (addr < 45) && !(addr % 3)) {
					data = (unsigned long)data << 15;
					data = (data & 0xffff0000) |
					       ((data & 0x0000ffff) >> 1);
				}
#endif
				child->thread.fp[addr - 21] = data;
				ret = 0;
			}
			goto out;

		case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
		case PTRACE_CONT: { /* restart after signal. */
			long tmp;

			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				goto out;
			if (request == PTRACE_SYSCALL)
				child->ptrace |= PT_TRACESYS;
			else
				child->ptrace &= ~PT_TRACESYS;
			child->exit_code = data;
			/* make sure the single step bit is not set. */
			tmp = get_reg(child, PT_SR) & ~(TRACE_BITS << 16);
			put_reg(child, PT_SR, tmp);
			wake_up_process(child);
			ret = 0;
			goto out;
		}

/*
 * make the child exit.  Best I can do is send it a sigkill. 
 * perhaps it should be put in the status that it wants to 
 * exit.
 */
		case PTRACE_KILL: {
			long tmp;

			ret = 0;
			if (child->state == TASK_ZOMBIE) /* already dead */
				goto out;
			child->exit_code = SIGKILL;
	/* make sure the single step bit is not set. */
			tmp = get_reg(child, PT_SR) & ~(TRACE_BITS << 16);
			put_reg(child, PT_SR, tmp);
			wake_up_process(child);
			goto out;
		}

		case PTRACE_SINGLESTEP: {  /* set the trap flag. */
			long tmp;

			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				goto out;
			child->ptrace &= ~PT_TRACESYS;
			tmp = get_reg(child, PT_SR) | (TRACE_BITS << 16);
			put_reg(child, PT_SR, tmp);

			child->exit_code = data;
	/* give it a chance to run. */
			wake_up_process(child);
			ret = 0;
			goto out;
		}

		case PTRACE_DETACH: { /* detach a process that was attached. */
			long tmp;

			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				goto out;
			child->ptrace &= ~(PT_PTRACED|PT_TRACESYS);
			child->exit_code = data;
			write_lock_irqsave(&tasklist_lock, flags);
			REMOVE_LINKS(child);
			child->p_pptr = child->p_opptr;
			SET_LINKS(child);
			write_unlock_irqrestore(&tasklist_lock, flags);
			/* make sure the single step bit is not set. */
			tmp = get_reg(child, PT_SR) & ~(TRACE_BITS << 16);
			put_reg(child, PT_SR, tmp);
			wake_up_process(child);
			ret = 0;
			goto out;
		}

		case PTRACE_GETREGS: { /* Get all gp regs from the child. */
		  	int i;
			unsigned long tmp;
			for (i = 0; i < 19; i++) {
			    tmp = get_reg(child, i);
			    if (i == PT_SR)
				tmp >>= 16;
			    if (put_user(tmp, (unsigned long *) data)) {
				ret = -EFAULT;
				goto out;
			    }
			    data += sizeof(long);
			}
			ret = 0;
			goto out;
		}

		case PTRACE_SETREGS: { /* Set all gp regs in the child. */
			int i;
			unsigned long tmp;
			for (i = 0; i < 19; i++) {
			    if (get_user(tmp, (unsigned long *) data)) {
				ret = -EFAULT;
				goto out;
			    }
			    if (i == PT_SR) {
				tmp &= SR_MASK;
				tmp <<= 16;
				tmp |= get_reg(child, PT_SR) & ~(SR_MASK << 16);
			    }
			    put_reg(child, i, tmp);
			    data += sizeof(long);
			}
			ret = 0;
			goto out;
		}

		case PTRACE_GETFPREGS: { /* Get the child FPU state. */
			ret = 0;
			if (copy_to_user((void *)data, &child->thread.fp,
					 sizeof(struct user_m68kfp_struct)))
				ret = -EFAULT;
			goto out;
		}

		case PTRACE_SETFPREGS: { /* Set the child FPU state. */
			ret = 0;
			if (copy_from_user(&child->thread.fp, (void *)data,
					   sizeof(struct user_m68kfp_struct)))
				ret = -EFAULT;
			goto out;
		}

		default:
			ret = -EIO;
			goto out;
	}
out:
	unlock_kernel();
	return ret;
}

asmlinkage void syscall_trace(void)
{
	lock_kernel();
	if ((current->ptrace & (PT_PTRACED|PT_TRACESYS))
			!= (PT_PTRACED|PT_TRACESYS))
		goto out;
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
out:
	unlock_kernel();
}
