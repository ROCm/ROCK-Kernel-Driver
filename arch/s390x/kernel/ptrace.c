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

#include <stddef.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>

#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#ifdef CONFIG_S390_SUPPORT
#include "ptrace32.h"
#endif

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
#ifdef CONFIG_S390_SUPPORT
		if (current->thread.flags & S390_FLAG_31BIT)
			per_info->control_regs.bits.ending_addr = 0x7fffffffUL;
		else
#endif
			per_info->control_regs.bits.ending_addr = -1;
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
	per_struct *per_info= (per_struct *) &task->thread.per_info;	
	
	per_info->single_step = 1;  /* Single step */
	FixPerRegisters (task);
}

void clear_single_step(struct task_struct *task)
{
	per_struct *per_info= (per_struct *) &task->thread.per_info;

	per_info->single_step = 0;
	FixPerRegisters (task);
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
	__u64 tmp;

	if ((addr & 7) || addr > sizeof(struct user) - 7)
		return -EIO;

	if (addr <= (addr_t) &dummy->regs.orig_gpr2) {
		/*
		 * psw, gprs, acrs and orig_gpr2 are stored on the stack
		 */
		tmp = *(__u64 *)((addr_t) __KSTK_PTREGS(child) + addr);

	} else if (addr >= (addr_t) &dummy->regs.fp_regs &&
		   addr < (addr_t) (&dummy->regs.fp_regs + 1)) {
		/*
		 * floating point regs. are stored in the thread structure
		 */
		offset = addr - (addr_t) &dummy->regs.fp_regs;
		tmp = *(__u64 *)((addr_t) &child->thread.fp_regs + offset);

	} else if (addr >= (addr_t) &dummy->regs.per_info &&
		   addr < (addr_t) (&dummy->regs.per_info + 1)) {
		/*
		 * per_info is found in the thread structure
		 */
		offset = addr - (addr_t) &dummy->regs.per_info;
		tmp = *(__u64 *)((addr_t) &child->thread.per_info + offset);

	} else
		tmp = 0;

	return put_user(tmp, (__u64 *) data);
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

	if ((addr & 7) || addr > sizeof(struct user) - 7)
		return -EIO;

	if (addr <= (addr_t) &dummy->regs.orig_gpr2) {
		/*
		 * psw, gprs, acrs and orig_gpr2 are stored on the stack
		 */
		if (addr == (addr_t) &dummy->regs.psw.mask &&
#ifdef CONFIG_S390_SUPPORT
		    (data & ~PSW_MASK_CC) != PSW_USER32_BITS &&
#endif
		    (data & ~PSW_MASK_CC) != PSW_USER_BITS)
			/* Invalid psw mask. */
			return -EINVAL;
		*(__u64 *)((addr_t) __KSTK_PTREGS(child) + addr) = data;

	} else if (addr >= (addr_t) &dummy->regs.fp_regs &&
		   addr < (addr_t) (&dummy->regs.fp_regs + 1)) {
		/*
		 * floating point regs. are stored in the thread structure
		 */
		if (addr == (addr_t) &dummy->regs.fp_regs.fpc &&
		    ((data >> 32) & ~FPC_VALID_MASK) != 0)
			/* Invalid floating pointer control. */
			return -EINVAL;
		offset = addr - (addr_t) &dummy->regs.fp_regs;
		*(__u64 *)((addr_t) &child->thread.fp_regs + offset) = data;

	} else if (addr >= (addr_t) &dummy->regs.per_info &&
		   addr < (addr_t) (&dummy->regs.per_info + 1)) {
		/*
		 * per_info is found in the thread structure
		 */
		offset = addr - (addr_t) &dummy->regs.per_info;
		*(__u64 *)((addr_t) &child->thread.per_info + offset) = data;

	}

	FixPerRegisters(child);
	return 0;
}

static int
do_ptrace_normal(struct task_struct *child, long request, long addr, long data)
{
	unsigned long tmp;
	ptrace_area parea; 
	int copied, ret;

	switch (request) {
	case PTRACE_PEEKTEXT:
	case PTRACE_PEEKDATA:
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
		/* write the word at location addr. */
		copied = access_process_vm(child, addr, &data, sizeof(data),1);
		if (copied != sizeof(data))
			return -EIO;
		return 0;

	case PTRACE_POKEUSR:
		/* write the word at location addr in the USER area */
		return poke_user(child, addr, data);

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
	return -EIO;
}

#ifdef CONFIG_S390_SUPPORT
/*
 * Now the fun part starts... a 31 bit program running in the
 * 31 bit emulation tracing another program. PTRACE_PEEKTEXT,
 * PTRACE_PEEKDATA, PTRACE_POKETEXT and PTRACE_POKEDATA are easy
 * to handle, the difference to the 64 bit versions of the requests
 * is that the access is done in multiples of 4 byte instead of
 * 8 bytes (sizeof(unsigned long) on 31/64 bit).
 * The ugly part are PTRACE_PEEKUSR, PTRACE_PEEKUSR_AREA,
 * PTRACE_POKEUSR and PTRACE_POKEUSR_AREA. If the traced program
 * is a 31 bit program too, the content of struct user can be
 * emulated. A 31 bit program peeking into the struct user of
 * a 64 bit program is a no-no.
 */

/*
 * Same as peek_user but for a 31 bit program.
 */
static int peek_user_emu31(struct task_struct *child, addr_t addr, addr_t data)
{
	struct user32 *dummy32 = NULL;
	per_struct32 *dummy_per32 = NULL;
	addr_t offset;
	__u32 tmp;

	if (!(child->thread.flags & S390_FLAG_31BIT) ||
	    (addr & 3) || addr > sizeof(struct user) - 3)
		return -EIO;

	if (addr <= (addr_t) &dummy32->regs.orig_gpr2) {
		/*
		 * psw, gprs, acrs and orig_gpr2 are stored on the stack
		 */
		if (addr == (addr_t) &dummy32->regs.psw.mask) {
			/* Fake a 31 bit psw mask. */
			tmp = (__u32)(__KSTK_PTREGS(child)->psw.mask >> 32);
			tmp = (tmp & PSW32_MASK_CC) | PSW32_USER_BITS;
		} else if (addr == (addr_t) &dummy32->regs.psw.addr) {
			/* Fake a 31 bit psw address. */
			tmp = (__u32) __KSTK_PTREGS(child)->psw.addr |
				PSW32_ADDR_AMODE31;
		} else
			tmp = *(__u32 *)((addr_t) __KSTK_PTREGS(child) + 
					 addr*2 + 4);
	} else if (addr >= (addr_t) &dummy32->regs.fp_regs &&
		   addr < (addr_t) (&dummy32->regs.fp_regs + 1)) {
		/*
		 * floating point regs. are stored in the thread structure 
		 */
	        offset = addr - (addr_t) &dummy32->regs.fp_regs;
		tmp = *(__u32 *)((addr_t) &child->thread.fp_regs + offset);

	} else if (addr >= (addr_t) &dummy32->regs.per_info &&
		   addr < (addr_t) (&dummy32->regs.per_info + 1)) {
		/*
		 * per_info is found in the thread structure
		 */
		offset = addr - (addr_t) &dummy32->regs.per_info;
		/* This is magic. See per_struct and per_struct32. */
		if ((offset >= (addr_t) &dummy_per32->control_regs &&
		     offset < (addr_t) (&dummy_per32->control_regs + 1)) ||
		    (offset >= (addr_t) &dummy_per32->starting_addr &&
		     offset <= (addr_t) &dummy_per32->ending_addr) ||
		    offset == (addr_t) &dummy_per32->lowcore.words.address)
			offset = offset*2 + 4;
		else
			offset = offset*2;
		tmp = *(__u32 *)((addr_t) &child->thread.per_info + offset);

	} else
		tmp = 0;

	return put_user(tmp, (__u32 *) data);
}

/*
 * Same as poke_user but for a 31 bit program.
 */
static int poke_user_emu31(struct task_struct *child, addr_t addr, addr_t data)
{
	struct user32 *dummy32 = NULL;
	per_struct32 *dummy_per32 = NULL;
	addr_t offset;
	__u32 tmp;
	int ret;

	if (!(child->thread.flags & S390_FLAG_31BIT) ||
	    (addr & 3) || addr > sizeof(struct user32) - 3)
		return -EIO;

	tmp = (__u32) data;

	if (addr <= (addr_t) &dummy32->regs.orig_gpr2) {
		/*
		 * psw, gprs, acrs and orig_gpr2 are stored on the stack
		 */
		if (addr == (addr_t) &dummy32->regs.psw.mask) {
			/* Build a 64 bit psw mask from 31 bit mask. */
			if ((tmp & ~PSW32_MASK_CC) != PSW32_USER_BITS)
				/* Invalid psw mask. */
				return -EINVAL;
			__KSTK_PTREGS(child)->psw.mask = PSW_USER_BITS |
				((tmp & PSW32_MASK_CC) << 32);
		} else if (addr == (addr_t) &dummy32->regs.psw.addr) {
			/* Build a 64 bit psw address from 31 bit address. */
			__KSTK_PTREGS(child)->psw.addr = 
				(__u64) tmp & PSW32_ADDR_INSN;
		} else
			*(__u32*)((addr_t) __KSTK_PTREGS(child) + addr*2 + 4) =
				tmp;
	} else if (addr >= (addr_t) &dummy32->regs.fp_regs &&
		   addr < (addr_t) (&dummy32->regs.fp_regs + 1)) {
		/*
		 * floating point regs. are stored in the thread structure 
		 */
		if (addr == (addr_t) &dummy32->regs.fp_regs.fpc &&
		    (tmp & ~FPC_VALID_MASK) != 0)
			/* Invalid floating pointer control. */
			return -EINVAL;
	        offset = addr - (addr_t) &dummy32->regs.fp_regs;
		*(__u32 *)((addr_t) &child->thread.fp_regs + offset) = tmp;

	} else if (addr >= (addr_t) &dummy32->regs.per_info &&
		   addr < (addr_t) (&dummy32->regs.per_info + 1)) {
		/*
		 * per_info is found in the thread structure.
		 */
		offset = addr - (addr_t) &dummy32->regs.per_info;
		/*
		 * This is magic. See per_struct and per_struct32.
		 * By incident the offsets in per_struct are exactly
		 * twice the offsets in per_struct32 for all fields.
		 * The 8 byte fields need special handling though,
		 * because the second half (bytes 4-7) is needed and
		 * not the first half.
		 */
		if ((offset >= (addr_t) &dummy_per32->control_regs &&
		     offset < (addr_t) (&dummy_per32->control_regs + 1)) ||
		    (offset >= (addr_t) &dummy_per32->starting_addr &&
		     offset <= (addr_t) &dummy_per32->ending_addr) ||
		    offset == (addr_t) &dummy_per32->lowcore.words.address)
			offset = offset*2 + 4;
		else
			offset = offset*2;
		*(__u32 *)((addr_t) &child->thread.per_info + offset) = tmp;

	}

	FixPerRegisters(child);
	return 0;
}

static int
do_ptrace_emu31(struct task_struct *child, long request, long addr, long data)
{
	unsigned int tmp;  /* 4 bytes !! */
	ptrace_area_emu31 parea; 
	int copied, ret;

	switch (request) {
	case PTRACE_PEEKTEXT:
	case PTRACE_PEEKDATA:
		/* read word at location addr. */
		copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
		if (copied != sizeof(tmp))
			return -EIO;
		return put_user(tmp, (unsigned int *) data);

	case PTRACE_PEEKUSR:
		/* read the word at location addr in the USER area. */
		return peek_user_emu31(child, addr, data);

	case PTRACE_POKETEXT:
	case PTRACE_POKEDATA:
		/* write the word at location addr. */
		tmp = data;
		copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 1);
		if (copied != sizeof(tmp))
			return -EIO;
		return 0;

	case PTRACE_POKEUSR:
		/* write the word at location addr in the USER area */
		return poke_user_emu31(child, addr, data);

	case PTRACE_PEEKUSR_AREA:
	case PTRACE_POKEUSR_AREA:
		if (!copy_from_user(&parea, (void *) addr, sizeof(parea)))
			return -EFAULT;
		addr = parea.kernel_addr;
		data = parea.process_addr;
		copied = 0;
		while (copied < parea.len) {
			if (request == PTRACE_PEEKUSR_AREA)
				ret = peek_user_emu31(child, addr, data);
			else
				ret = poke_user_emu31(child, addr, data);
			if (ret)
				return ret;
			addr += sizeof(unsigned int);
			data += sizeof(unsigned int);
			copied += sizeof(unsigned int);
		}
		return 0;
	}
	return -EIO;
}
#endif

static int
do_ptrace(struct task_struct *child, long request, long addr, long data)
{
	int ret;

	if (request == PTRACE_ATTACH)
		return ptrace_attach(child);

	/*
	 * I added child != current line so we can get the
	 * ieee_instruction_pointer from the user structure DJB
	 */
	if (child != current) {
		ret = ptrace_check_attach(child, request == PTRACE_KILL);
		if (ret < 0)
			return ret;
	}

	switch (request) {
	/* First the common request for 31/64 bit */
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

	case PTRACE_SETOPTIONS:
		if (data & PTRACE_O_TRACESYSGOOD)
			child->ptrace |= PT_TRACESYSGOOD;
		else
			child->ptrace &= ~PT_TRACESYSGOOD;
		return 0;
	/* Do requests that differ for 31/64 bit */
	default:
#ifdef CONFIG_S390_SUPPORT
		if (current->thread.flags & S390_FLAG_31BIT)
			return do_ptrace_emu31(child, request, addr, data);
#endif
		return do_ptrace_normal(child, request, addr, data);
		
	}
	return -EIO;
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
		ret = security_ops->ptrace(current->parent, current);
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
	current->exit_code =
		SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD) ? 0x80 : 0);
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
