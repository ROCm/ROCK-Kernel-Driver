/*
 *  linux/arch/arm/kernel/ptrace.c
 *
 *  By Ross Biro 1/23/92
 * edited by Linus Torvalds
 * ARM modifications Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/ptrace.h>
#include <linux/user.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>

#include "ptrace.h"

#define REG_PC	15
#define REG_PSR	16
/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * Breakpoint SWI instruction: SWI &9F0001
 */
#define BREAKINST	0xef9f0001

/*
 * Get the address of the live pt_regs for the specified task.
 * These are saved onto the top kernel stack when the process
 * is not running.
 */
static inline struct pt_regs *
get_user_regs(struct task_struct *task)
{
	return (struct pt_regs *)
		((unsigned long)task + 8192 - sizeof(struct pt_regs));
}

/*
 * this routine will get a word off of the processes privileged stack.
 * the offset is how far from the base addr as stored in the THREAD.
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */
static inline long get_stack_long(struct task_struct *task, int offset)
{
	return get_user_regs(task)->uregs[offset];
}

/*
 * this routine will put a word on the processes privileged stack.
 * the offset is how far from the base addr as stored in the THREAD.
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */
static inline int
put_stack_long(struct task_struct *task, int offset, long data)
{
	struct pt_regs newregs, *regs = get_user_regs(task);
	int ret = -EINVAL;

	newregs = *regs;
	newregs.uregs[offset] = data;
	
	if (valid_user_regs(&newregs)) {
		regs->uregs[offset] = data;
		ret = 0;
	}

	return ret;
}

static inline int
read_tsk_long(struct task_struct *child, unsigned long addr, unsigned long *res)
{
	int copied;

	copied = access_process_vm(child, addr, res, sizeof(*res), 0);

	return copied != sizeof(*res) ? -EIO : 0;
}

static inline int
write_tsk_long(struct task_struct *child, unsigned long addr, unsigned long val)
{
	int copied;

	copied = access_process_vm(child, addr, &val, sizeof(val), 1);

	return copied != sizeof(val) ? -EIO : 0;
}

/*
 * Get value of register `rn' (in the instruction)
 */
static unsigned long
ptrace_getrn(struct task_struct *child, unsigned long insn)
{
	unsigned int reg = (insn >> 16) & 15;
	unsigned long val;

	val = get_stack_long(child, reg);
	if (reg == 15)
		val = pc_pointer(val);

	return val;
}

/*
 * Get value of operand 2 (in an ALU instruction)
 */
static unsigned long
ptrace_getaluop2(struct task_struct *child, unsigned long insn)
{
	unsigned long val;
	int shift;
	int type;

	if (insn & 1 << 25) {
		val = insn & 255;
		shift = (insn >> 8) & 15;
		type = 3;
	} else {
		val = get_stack_long (child, insn & 15);

		if (insn & (1 << 4))
			shift = (int)get_stack_long (child, (insn >> 8) & 15);
		else
			shift = (insn >> 7) & 31;

		type = (insn >> 5) & 3;
	}

	switch (type) {
	case 0:	val <<= shift;	break;
	case 1:	val >>= shift;	break;
	case 2:
		val = (((signed long)val) >> shift);
		break;
	case 3:
 		val = (val >> shift) | (val << (32 - shift));
		break;
	}
	return val;
}

/*
 * Get value of operand 2 (in a LDR instruction)
 */
static unsigned long
ptrace_getldrop2(struct task_struct *child, unsigned long insn)
{
	unsigned long val;
	int shift;
	int type;

	val = get_stack_long(child, insn & 15);
	shift = (insn >> 7) & 31;
	type = (insn >> 5) & 3;

	switch (type) {
	case 0:	val <<= shift;	break;
	case 1:	val >>= shift;	break;
	case 2:
		val = (((signed long)val) >> shift);
		break;
	case 3:
 		val = (val >> shift) | (val << (32 - shift));
		break;
	}
	return val;
}

static unsigned long
get_branch_address(struct task_struct *child, unsigned long pc, unsigned long insn)
{
	unsigned long alt = 0;

	switch (insn & 0x0e000000) {
	case 0x00000000:
	case 0x02000000: {
		/*
		 * data processing
		 */
		long aluop1, aluop2, ccbit;

		if ((insn & 0xf000) != 0xf000)
			break;

		aluop1 = ptrace_getrn(child, insn);
		aluop2 = ptrace_getaluop2(child, insn);
		ccbit  = get_stack_long(child, REG_PSR) & CC_C_BIT ? 1 : 0;

		switch (insn & 0x01e00000) {
		case 0x00000000: alt = aluop1 & aluop2;		break;
		case 0x00200000: alt = aluop1 ^ aluop2;		break;
		case 0x00400000: alt = aluop1 - aluop2;		break;
		case 0x00600000: alt = aluop2 - aluop1;		break;
		case 0x00800000: alt = aluop1 + aluop2;		break;
		case 0x00a00000: alt = aluop1 + aluop2 + ccbit;	break;
		case 0x00c00000: alt = aluop1 - aluop2 + ccbit;	break;
		case 0x00e00000: alt = aluop2 - aluop1 + ccbit;	break;
		case 0x01800000: alt = aluop1 | aluop2;		break;
		case 0x01a00000: alt = aluop2;			break;
		case 0x01c00000: alt = aluop1 & ~aluop2;	break;
		case 0x01e00000: alt = ~aluop2;			break;
		}
		break;
	}

	case 0x04000000:
	case 0x06000000:
		/*
		 * ldr
		 */
		if ((insn & 0x0010f000) == 0x0010f000) {
			unsigned long base;

			base = ptrace_getrn(child, insn);
			if (insn & 1 << 24) {
				long aluop2;

				if (insn & 0x02000000)
					aluop2 = ptrace_getldrop2(child, insn);
				else
					aluop2 = insn & 0xfff;

				if (insn & 1 << 23)
					base += aluop2;
				else
					base -= aluop2;
			}
			if (read_tsk_long(child, base, &alt) == 0)
				alt = pc_pointer(alt);
		}
		break;

	case 0x08000000:
		/*
		 * ldm
		 */
		if ((insn & 0x00108000) == 0x00108000) {
			unsigned long base;
			unsigned int nr_regs;

			if (insn & (1 << 23)) {
				nr_regs = insn & 65535;

				nr_regs = (nr_regs & 0x5555) + ((nr_regs & 0xaaaa) >> 1);
				nr_regs = (nr_regs & 0x3333) + ((nr_regs & 0xcccc) >> 2);
				nr_regs = (nr_regs & 0x0707) + ((nr_regs & 0x7070) >> 4);
				nr_regs = (nr_regs & 0x000f) + ((nr_regs & 0x0f00) >> 8);
				nr_regs <<= 2;

				if (!(insn & (1 << 24)))
					nr_regs -= 4;
			} else {
				if (insn & (1 << 24))
					nr_regs = -4;
				else
					nr_regs = 0;
			}

			base = ptrace_getrn(child, insn);

			if (read_tsk_long(child, base + nr_regs, &alt) == 0)
				alt = pc_pointer (alt);
			break;
		}
		break;

	case 0x0a000000: {
		/*
		 * bl or b
		 */
		signed long displ;
		/* It's a branch/branch link: instead of trying to
		 * figure out whether the branch will be taken or not,
		 * we'll put a breakpoint at both locations.  This is
		 * simpler, more reliable, and probably not a whole lot
		 * slower than the alternative approach of emulating the
		 * branch.
		 */
		displ = (insn & 0x00ffffff) << 8;
		displ = (displ >> 6) + 8;
		if (displ != 0 && displ != 4)
			alt = pc + displ;
	    }
	    break;
	}

	return alt;
}

static int
add_breakpoint(struct task_struct *child, struct debug_info *dbg, unsigned long addr)
{
	int nr = dbg->nsaved;
	int res = -EINVAL;

	if (nr < 2) {
		res = read_tsk_long(child, addr, &dbg->bp[nr].insn);
		if (res == 0)
			res = write_tsk_long(child, addr, BREAKINST);

		if (res == 0) {
			dbg->bp[nr].address = addr;
			dbg->nsaved += 1;
		}
	} else
		printk(KERN_DEBUG "add_breakpoint: too many breakpoints\n");

	return res;
}

int ptrace_set_bpt(struct task_struct *child)
{
	unsigned long insn, pc;
	int res;

	pc = pc_pointer(get_stack_long(child, REG_PC));

	res = read_tsk_long(child, pc, &insn);
	if (!res) {
		struct debug_info *dbg = &child->thread.debug;
		unsigned long alt;

		dbg->nsaved = 0;

		alt = get_branch_address(child, pc, insn);
		if (alt)
			res = add_breakpoint(child, dbg, alt);

		if (!res && (!alt || predicate(insn) != PREDICATE_ALWAYS))
			res = add_breakpoint(child, dbg, pc + 4);
	}

	return res;
}

/*
 * Ensure no single-step breakpoint is pending.  Returns non-zero
 * value if child was being single-stepped.
 */
void __ptrace_cancel_bpt(struct task_struct *child)
{
	struct debug_info *dbg = &child->thread.debug;
	int i, nsaved = dbg->nsaved;

	dbg->nsaved = 0;

	if (nsaved > 2) {
		printk("ptrace_cancel_bpt: bogus nsaved: %d!\n", nsaved);
		nsaved = 2;
	}

	for (i = 0; i < nsaved; i++) {
		unsigned long tmp;

		read_tsk_long(child, dbg->bp[i].address, &tmp);
		if (tmp != BREAKINST)
			printk(KERN_ERR "ptrace_cancel_bpt: weirdness\n");
		write_tsk_long(child, dbg->bp[i].address, dbg->bp[i].insn);
	}
}

static int do_ptrace(int request, struct task_struct *child, long addr, long data)
{
	unsigned long tmp;
	int ret;

	switch (request) {
		/*
		 * read word at location "addr" in the child process.
		 */
		case PTRACE_PEEKTEXT:
		case PTRACE_PEEKDATA:
			ret = read_tsk_long(child, addr, &tmp);
			if (!ret)
				ret = put_user(tmp, (unsigned long *) data);
			break;

		/*
		 * read the word at location "addr" in the user registers.
		 */
		case PTRACE_PEEKUSR:
			ret = -EIO;
			if ((addr & 3) || addr < 0 || addr >= sizeof(struct user))
				break;

			tmp = 0;  /* Default return condition */
			if (addr < sizeof(struct pt_regs))
				tmp = get_stack_long(child, (int)addr >> 2);
			ret = put_user(tmp, (unsigned long *)data);
			break;

		/*
		 * write the word at location addr.
		 */
		case PTRACE_POKETEXT:
		case PTRACE_POKEDATA:
			ret = write_tsk_long(child, addr, data);
			break;

		/*
		 * write the word at location addr in the user registers.
		 */
		case PTRACE_POKEUSR:
			ret = -EIO;
			if ((addr & 3) || addr < 0 || addr >= sizeof(struct user))
				break;

			if (addr < sizeof(struct pt_regs))
				ret = put_stack_long(child, (int)addr >> 2, data);
			break;

		/*
		 * continue/restart and stop at next (return from) syscall
		 */
		case PTRACE_SYSCALL:
		case PTRACE_CONT:
			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				break;
			if (request == PTRACE_SYSCALL)
				child->ptrace |= PT_TRACESYS;
			else
				child->ptrace &= ~PT_TRACESYS;
			child->exit_code = data;
			/* make sure single-step breakpoint is gone. */
			__ptrace_cancel_bpt(child);
			wake_up_process(child);
			ret = 0;
			break;

		/*
		 * make the child exit.  Best I can do is send it a sigkill.
		 * perhaps it should be put in the status that it wants to
		 * exit.
		 */
		case PTRACE_KILL:
			/* already dead */
			ret = 0;
			if (child->state == TASK_ZOMBIE)
				break;
			child->exit_code = SIGKILL;
			/* make sure single-step breakpoint is gone. */
			__ptrace_cancel_bpt(child);
			wake_up_process(child);
			ret = 0;
			break;

		/*
		 * execute single instruction.
		 */
		case PTRACE_SINGLESTEP:
			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				break;
			child->thread.debug.nsaved = -1;
			child->ptrace &= ~PT_TRACESYS;
			child->exit_code = data;
			/* give it a chance to run. */
			wake_up_process(child);
			ret = 0;
			break;

		/*
		 * detach a process that was attached.
		 */
		case PTRACE_DETACH:
			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				break;
			child->ptrace &= ~(PT_PTRACED|PT_TRACESYS);
			child->exit_code = data;
			write_lock_irq(&tasklist_lock);
			REMOVE_LINKS(child);
			child->p_pptr = child->p_opptr;
			SET_LINKS(child);
			write_unlock_irq(&tasklist_lock);
			/* make sure single-step breakpoint is gone. */
			__ptrace_cancel_bpt(child);
			wake_up_process (child);
			ret = 0;
			break;

		/*
		 * Get all gp regs from the child.
		 */
		case PTRACE_GETREGS: {
			struct pt_regs *regs = get_user_regs(child);

			ret = 0;
			if (copy_to_user((void *)data, regs,
					 sizeof(struct pt_regs)))
				ret = -EFAULT;

			break;
		}

		/*
		 * Set all gp regs in the child.
		 */
		case PTRACE_SETREGS: {
			struct pt_regs newregs;

			ret = -EFAULT;
			if (copy_from_user(&newregs, (void *)data,
					   sizeof(struct pt_regs)) == 0) {
				struct pt_regs *regs = get_user_regs(child);

				ret = -EINVAL;
				if (valid_user_regs(&newregs)) {
					*regs = newregs;
					ret = 0;
				}
			}
			break;
		}

		/*
		 * Get the child FPU state.
		 */
		case PTRACE_GETFPREGS:
			ret = -EIO;
			if (!access_ok(VERIFY_WRITE, (void *)data, sizeof(struct user_fp)))
				break;

			/* we should check child->used_math here */
			ret = __copy_to_user((void *)data, &child->thread.fpstate,
					     sizeof(struct user_fp)) ? -EFAULT : 0;
			break;
		
		/*
		 * Set the child FPU state.
		 */
		case PTRACE_SETFPREGS:
			ret = -EIO;
			if (!access_ok(VERIFY_READ, (void *)data, sizeof(struct user_fp)))
				break;

			child->used_math = 1;
			ret = __copy_from_user(&child->thread.fpstate, (void *)data,
					   sizeof(struct user_fp)) ? -EFAULT : 0;
			break;

		default:
			ret = -EIO;
			break;
	}

	return ret;
}

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

		write_lock_irq(&tasklist_lock);
		if (child->p_pptr != current) {
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
		}
		write_unlock_irq(&tasklist_lock);

		send_sig(SIGSTOP, child, 1);
		ret = 0;
		goto out_tsk;
	}
	ret = -ESRCH;
	if (!(child->ptrace & PT_PTRACED))
		goto out_tsk;
	if (child->state != TASK_STOPPED && request != PTRACE_KILL)
		goto out_tsk;
	if (child->p_pptr != current)
		goto out_tsk;

	ret = do_ptrace(request, child, addr, data);

out_tsk:
	free_task_struct(child);
out:
	unlock_kernel();
	return ret;
}

asmlinkage void syscall_trace(void)
{
	if ((current->ptrace & (PT_PTRACED|PT_TRACESYS))
			!= (PT_PTRACED|PT_TRACESYS))
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
