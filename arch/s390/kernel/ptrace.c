/*
 *  arch/s390/kernel/ptrace.c
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Based on PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Derived from "arch/m68k/kernel/ptrace.c"
 *  Copyright (C) 1994 by Hamish Macdonald
 *  Taken from linux/kernel/ptrace.c and modified for M680x0.
 *  linux/kernel/ptrace.c is by Ross Biro 1/23/92, edited by Linus Torvalds
 *
 * Modified by Cort Dougan (cort@cs.nmt.edu) 
 *
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file README.legal in the main directory of
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
#include <linux/security.h>

#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/system.h>
#include <asm/uaccess.h>


static void FixPerRegisters(struct task_struct *task)
{
	struct pt_regs *regs;
	per_struct *per_info;

	regs = __KSTK_PTREGS(task);
	per_info = (per_struct *) &task->thread.per_info;
	per_info->control_regs.bits.em_instruction_fetch =
		per_info->single_step | per_info->instruction_fetch;
	
	if (per_info->single_step) {
		per_info->control_regs.bits.starting_addr = 0;
		per_info->control_regs.bits.ending_addr = 0x7fffffffUL;
	} else {
		per_info->control_regs.bits.starting_addr =
			per_info->starting_addr;
		per_info->control_regs.bits.ending_addr =
			per_info->ending_addr;
	}
	/*
	 * if any of the control reg tracing bits are on 
	 * we switch on per in the psw
	 */
	if (per_info->control_regs.words.cr[0] & PER_EM_MASK)
		regs->psw.mask |= PSW_MASK_PER;
	else
		regs->psw.mask &= ~PSW_MASK_PER;

	if (per_info->control_regs.bits.em_storage_alteration)
		per_info->control_regs.bits.storage_alt_space_ctl = 1;
	else
		per_info->control_regs.bits.storage_alt_space_ctl = 0;
}

void set_single_step(struct task_struct *task)
{
	task->thread.per_info.single_step = 1;
	FixPerRegisters(task);
}

void clear_single_step(struct task_struct *task)
{
	task->thread.per_info.single_step = 0;
	FixPerRegisters(task);
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *child)
{
	/* make sure the single step bit is not set. */
	clear_single_step(child);
}

/*
 * Read the word at offset addr from the user area of a process. The
 * trouble here is that the information is littered over different
 * locations. The process registers are found on the kernel stack,
 * the floating point stuff and the trace settings are stored in
 * the task structure. In addition the different structures in
 * struct user contain pad bytes that should be read as zeroes.
 * Lovely...
 */
static int peek_user(struct task_struct *child, addr_t addr, addr_t data)
{
	struct user *dummy = NULL;
	addr_t offset;
	__u32 tmp;

	if ((addr & 3) || addr > sizeof(struct user) - 3)
		return -EIO;

	if (addr <= (addr_t) &dummy->regs.orig_gpr2) {
		/*
		 * psw, gprs, acrs and orig_gpr2 are stored on the stack
		 */
		tmp = *(__u32 *)((addr_t) __KSTK_PTREGS(child) + addr);

	} else if (addr >= (addr_t) &dummy->regs.fp_regs &&
		   addr < (addr_t) (&dummy->regs.fp_regs + 1)) {
		/* 
		 * floating point regs. are stored in the thread structure
		 */
		offset = addr - (addr_t) &dummy->regs.fp_regs;
		tmp = *(__u32 *)((addr_t) &child->thread.fp_regs + offset);

	} else if (addr >= (addr_t) &dummy->regs.per_info &&
		   addr < (addr_t) (&dummy->regs.per_info + 1)) {
		/*
		 * per_info is found in the thread structure
		 */
		offset = addr - (addr_t) &dummy->regs.per_info;
		tmp = *(__u32 *)((addr_t) &child->thread.per_info + offset);

	} else
		tmp = 0;

	return put_user(tmp, (__u32 *) data);
}

/*
 * Write a word to the user area of a process at location addr. This
 * operation does have an additional problem compared to peek_user.
 * Stores to the program status word and on the floating point
 * control register needs to get checked for validity.
 */
static int poke_user(struct task_struct *child, addr_t addr, addr_t data)
{
	struct user *dummy = NULL;
	addr_t offset;

	if ((addr & 3) || addr > sizeof(struct user) - 3)
		return -EIO;

	if (addr <= (addr_t) &dummy->regs.orig_gpr2) {
		/*
		 * psw, gprs, acrs and orig_gpr2 are stored on the stack
		 */
		if (addr == (addr_t) &dummy->regs.psw.mask &&
		    (data & ~PSW_MASK_CC) != PSW_USER_BITS)
			/* Invalid psw mask. */
			return -EINVAL;
		if (addr == (addr_t) &dummy->regs.psw.addr)
			/* I'd like to reject addresses without the
			   high order bit but older gdb's rely on it */
			data |= PSW_ADDR_AMODE31;
		*(__u32 *)((addr_t) __KSTK_PTREGS(child) + addr) = data;

	} else if (addr >= (addr_t) &dummy->regs.fp_regs &&
		   addr < (addr_t) (&dummy->regs.fp_regs + 1)) {
		/*
		 * floating point regs. are stored in the thread structure
		 */
		if (addr == (addr_t) &dummy->regs.fp_regs.fpc &&
		    (data & ~FPC_VALID_MASK) != 0)
			return -EINVAL;
		offset = addr - (addr_t) &dummy->regs.fp_regs;
		*(__u32 *)((addr_t) &child->thread.fp_regs + offset) = data;

	} else if (addr >= (addr_t) &dummy->regs.per_info &&
		   addr < (addr_t) (&dummy->regs.per_info + 1)) {
		/*
		 * per_info is found in the thread structure 
		 */
		offset = addr - (addr_t) &dummy->regs.per_info;
		*(__u32 *)((addr_t) &child->thread.per_info + offset) = data;

	}

	FixPerRegisters(child);
	return 0;
}

static int
do_ptrace(struct task_struct *child, long request, long addr, long data)
{
	unsigned long tmp;
	ptrace_area parea; 
	int copied, ret;

	if (request == PTRACE_ATTACH)
		return ptrace_attach(child);

	/*
	 * Special cases to get/store the ieee instructions pointer.
	 */
	if (child == current) {
		if (request == PTRACE_PEEKUSR && addr == PT_IEEE_IP)
			return peek_user(child, addr, data);
		if (request == PTRACE_POKEUSR && addr == PT_IEEE_IP)
			return poke_user(child, addr, data);
	}

	ret = ptrace_check_attach(child, request == PTRACE_KILL);
	if (ret < 0)
		return ret;

	switch (request) {
	case PTRACE_PEEKTEXT:
	case PTRACE_PEEKDATA:
		/* Remove high order bit from address. */
		addr &= PSW_ADDR_INSN;
		/* read word at location addr. */
		copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
		if (copied != sizeof(tmp))
			return -EIO;
		return put_user(tmp, (unsigned long *) data);

	case PTRACE_PEEKUSR:
		/* read the word at location addr in the USER area. */
		return peek_user(child, addr, data);

	case PTRACE_POKETEXT:
	case PTRACE_POKEDATA:
		/* Remove high order bit from address. */
		addr &= PSW_ADDR_INSN;
		/* write the word at location addr. */
		copied = access_process_vm(child, addr, &data, sizeof(data),1);
		if (copied != sizeof(data))
			return -EIO;
		return 0;

	case PTRACE_POKEUSR:
		/* write the word at location addr in the USER area */
		return poke_user(child, addr, data);

	case PTRACE_SYSCALL:
		/* continue and stop at next (return from) syscall */
	case PTRACE_CONT:
		/* restart after signal. */
		if ((unsigned long) data >= _NSIG)
			return -EIO;
		if (request == PTRACE_SYSCALL)
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		else
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		child->exit_code = data;
		/* make sure the single step bit is not set. */
		clear_single_step(child);
		wake_up_process(child);
		return 0;

	case PTRACE_KILL:
		/*
		 * make the child exit.  Best I can do is send it a sigkill. 
		 * perhaps it should be put in the status that it wants to 
		 * exit.
		 */
		if (child->state == TASK_ZOMBIE) /* already dead */
			return 0;
		child->exit_code = SIGKILL;
		/* make sure the single step bit is not set. */
		clear_single_step(child);
		wake_up_process(child);
		return 0;

	case PTRACE_SINGLESTEP:
		/* set the trap flag. */
		if ((unsigned long) data >= _NSIG)
			return -EIO;
		clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		child->exit_code = data;
		set_single_step(child);
		/* give it a chance to run. */
		wake_up_process(child);
		return 0;

	case PTRACE_DETACH:
		/* detach a process that was attached. */
		return ptrace_detach(child, data);

	case PTRACE_PEEKUSR_AREA:
	case PTRACE_POKEUSR_AREA:
		if (!copy_from_user(&parea, (void *) addr, sizeof(parea)))
			return -EFAULT;
		addr = parea.kernel_addr;
		data = parea.process_addr;
		copied = 0;
		while (copied < parea.len) {
			if (request == PTRACE_PEEKUSR_AREA)
				ret = peek_user(child, addr, data);
			else
				ret = poke_user(child, addr, data);
			if (ret)
				return ret;
			addr += sizeof(unsigned long);
			data += sizeof(unsigned long);
			copied += sizeof(unsigned long);
		}
		return 0;
	}
	return ptrace_request(child, request, addr, data);
}

asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	int ret;

	lock_kernel();

	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		ret = -EPERM;
		if (current->ptrace & PT_PTRACED)
			goto out;
		ret = security_ptrace(current->parent, current);
		if (ret)
			goto out;
		/* set the ptrace bit in the process flags. */
		current->ptrace |= PT_PTRACED;
		goto out;
	}

	ret = -EPERM;
	if (pid == 1)		/* you may not mess with init */
		goto out;

	ret = -ESRCH;
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	if (child)
		get_task_struct(child);
	read_unlock(&tasklist_lock);
	if (!child)
		goto out;

	ret = do_ptrace(child, request, addr, data);

	put_task_struct(child);
out:
	unlock_kernel();
	return ret;
}

asmlinkage void syscall_trace(void)
{
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;
	if (!(current->ptrace & PT_PTRACED))
		return;
	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
				 ? 0x80 : 0));

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
