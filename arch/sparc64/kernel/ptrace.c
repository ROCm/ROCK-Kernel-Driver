/* ptrace.c: Sparc process tracing support.
 *
 * Copyright (C) 1996 David S. Miller (davem@caipfs.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 * Based upon code written by Ross Biro, Linus Torvalds, Bob Manson,
 * and David Mosberger.
 *
 * Added Linux support -miguel (wierd, eh?, the orignal code was meant
 * to emulate SunOS).
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/asi.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/psrcompat.h>
#include <asm/visasm.h>

#define MAGIC_CONSTANT 0x80000000

/* Returning from ptrace is a bit tricky because the syscall return
 * low level code assumes any value returned which is negative and
 * is a valid errno will mean setting the condition codes to indicate
 * an error return.  This doesn't work, so we have this hook.
 */
static inline void pt_error_return(struct pt_regs *regs, unsigned long error)
{
	regs->u_regs[UREG_I0] = error;
	regs->tstate |= (TSTATE_ICARRY | TSTATE_XCARRY);
	regs->tpc = regs->tnpc;
	regs->tnpc += 4;
}

static inline void pt_succ_return(struct pt_regs *regs, unsigned long value)
{
	regs->u_regs[UREG_I0] = value;
	regs->tstate &= ~(TSTATE_ICARRY | TSTATE_XCARRY);
	regs->tpc = regs->tnpc;
	regs->tnpc += 4;
}

static inline void
pt_succ_return_linux(struct pt_regs *regs, unsigned long value, long *addr)
{
	if (current->thread.flags & SPARC_FLAG_32BIT) {
		if(put_user(value, (unsigned int *)addr))
			return pt_error_return(regs, EFAULT);
	} else {
		if(put_user(value, addr))
			return pt_error_return(regs, EFAULT);
	}
	regs->u_regs[UREG_I0] = 0;
	regs->tstate &= ~(TSTATE_ICARRY | TSTATE_XCARRY);
	regs->tpc = regs->tnpc;
	regs->tnpc += 4;
}

static void
pt_os_succ_return (struct pt_regs *regs, unsigned long val, long *addr)
{
	if (current->personality == PER_SUNOS)
		pt_succ_return (regs, val);
	else
		pt_succ_return_linux (regs, val, addr);
}

/* #define ALLOW_INIT_TRACING */
/* #define DEBUG_PTRACE */

#ifdef DEBUG_PTRACE
char *pt_rq [] = {
"TRACEME",
"PEEKTEXT",
"PEEKDATA",
"PEEKUSR",
"POKETEXT",
"POKEDATA",
"POKEUSR",
"CONT",
"KILL",
"SINGLESTEP",
"SUNATTACH",
"SUNDETACH",
"GETREGS",
"SETREGS",
"GETFPREGS",
"SETFPREGS",
"READDATA",
"WRITEDATA",
"READTEXT",
"WRITETEXT",
"GETFPAREGS",
"SETFPAREGS",
""
};
#endif

asmlinkage void do_ptrace(struct pt_regs *regs)
{
	int request = regs->u_regs[UREG_I0];
	pid_t pid = regs->u_regs[UREG_I1];
	unsigned long addr = regs->u_regs[UREG_I2];
	unsigned long data = regs->u_regs[UREG_I3];
	unsigned long addr2 = regs->u_regs[UREG_I4];
	struct task_struct *child;

	if (current->thread.flags & SPARC_FLAG_32BIT) {
		addr &= 0xffffffffUL;
		data &= 0xffffffffUL;
		addr2 &= 0xffffffffUL;
	}
	lock_kernel();
#ifdef DEBUG_PTRACE
	{
		char *s;

		if ((request > 0) && (request < 21))
			s = pt_rq [request];
		else
			s = "unknown";

		if (request == PTRACE_POKEDATA && data == 0x91d02001){
			printk ("do_ptrace: breakpoint pid=%d, addr=%016lx addr2=%016lx\n",
				pid, addr, addr2);
		} else 
			printk("do_ptrace: rq=%s(%d) pid=%d addr=%016lx data=%016lx addr2=%016lx\n",
			       s, request, pid, addr, data, addr2);
	}
#endif
	if(request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->ptrace & PT_PTRACED) {
			pt_error_return(regs, EPERM);
			goto out;
		}
		/* set the ptrace bit in the process flags. */
		current->ptrace |= PT_PTRACED;
		pt_succ_return(regs, 0);
		goto out;
	}
#ifndef ALLOW_INIT_TRACING
	if(pid == 1) {
		/* Can't dork with init. */
		pt_error_return(regs, EPERM);
		goto out;
	}
#endif
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	read_unlock(&tasklist_lock);

	if(!child) {
		pt_error_return(regs, ESRCH);
		goto out;
	}

	if ((current->personality == PER_SUNOS && request == PTRACE_SUNATTACH)
	    || (current->personality != PER_SUNOS && request == PTRACE_ATTACH)) {
		unsigned long flags;

		if(child == current) {
			/* Try this under SunOS/Solaris, bwa haha
			 * You'll never be able to kill the process. ;-)
			 */
			pt_error_return(regs, EPERM);
			goto out;
		}
		if((!child->dumpable ||
		    (current->uid != child->euid) ||
		    (current->uid != child->uid) ||
		    (current->uid != child->suid) ||
		    (current->gid != child->egid) ||
		    (current->gid != child->sgid) ||
		    (!cap_issubset(child->cap_permitted, current->cap_permitted)) ||
		    (current->gid != child->gid)) && !capable(CAP_SYS_PTRACE)) {
			pt_error_return(regs, EPERM);
			goto out;
		}
		/* the same process cannot be attached many times */
		if (child->ptrace & PT_PTRACED) {
			pt_error_return(regs, EPERM);
			goto out;
		}
		child->ptrace |= PT_PTRACED;
		write_lock_irqsave(&tasklist_lock, flags);
		if(child->p_pptr != current) {
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
		}
		write_unlock_irqrestore(&tasklist_lock, flags);
		send_sig(SIGSTOP, child, 1);
		pt_succ_return(regs, 0);
		goto out;
	}
	if (!(child->ptrace & PT_PTRACED)) {
		pt_error_return(regs, ESRCH);
		goto out;
	}
	if(child->state != TASK_STOPPED) {
		if(request != PTRACE_KILL) {
			pt_error_return(regs, ESRCH);
			goto out;
		}
	}
	if(child->p_pptr != current) {
		pt_error_return(regs, ESRCH);
		goto out;
	}

	if(!(child->thread.flags & SPARC_FLAG_32BIT)	&&
	   ((request == PTRACE_READDATA64)		||
	    (request == PTRACE_WRITEDATA64)		||
	    (request == PTRACE_READTEXT64)		||
	    (request == PTRACE_WRITETEXT64)		||
	    (request == PTRACE_PEEKTEXT64)		||
	    (request == PTRACE_POKETEXT64)		||
	    (request == PTRACE_PEEKDATA64)		||
	    (request == PTRACE_POKEDATA64))) {
		addr = regs->u_regs[UREG_G2];
		addr2 = regs->u_regs[UREG_G3];
		request -= 30; /* wheee... */
	}

	switch(request) {
	case PTRACE_PEEKTEXT: /* read word at location addr. */ 
	case PTRACE_PEEKDATA: {
		unsigned long tmp64;
		unsigned int tmp32;
		int res, copied;

		res = -EIO;
		if (current->thread.flags & SPARC_FLAG_32BIT) {
			copied = access_process_vm(child, addr,
						   &tmp32, sizeof(tmp32), 0);
			tmp64 = (unsigned long) tmp32;
			if (copied == sizeof(tmp32))
				res = 0;
		} else {
			copied = access_process_vm(child, addr,
						   &tmp64, sizeof(tmp64), 0);
			if (copied == sizeof(tmp64))
				res = 0;
		}
		if (res < 0)
			pt_error_return(regs, -res);
		else
			pt_os_succ_return(regs, tmp64, (long *) data);
		goto flush_and_out;
	}

	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA: {
		unsigned long tmp64;
		unsigned int tmp32;
		int copied, res = -EIO;

		if (current->thread.flags & SPARC_FLAG_32BIT) {
			tmp32 = data;
			copied = access_process_vm(child, addr,
						   &tmp32, sizeof(tmp32), 1);
			if (copied == sizeof(tmp32))
				res = 0;
		} else {
			tmp64 = data;
			copied = access_process_vm(child, addr,
						   &tmp64, sizeof(tmp64), 1);
			if (copied == sizeof(tmp64))
				res = 0;
		}
		if(res < 0)
			pt_error_return(regs, -res);
		else
			pt_succ_return(regs, res);
		goto flush_and_out;
	}

	case PTRACE_GETREGS: {
		struct pt_regs32 *pregs = (struct pt_regs32 *) addr;
		struct pt_regs *cregs = child->thread.kregs;
		int rval;

		if (__put_user(tstate_to_psr(cregs->tstate), (&pregs->psr)) ||
		    __put_user(cregs->tpc, (&pregs->pc)) ||
		    __put_user(cregs->tnpc, (&pregs->npc)) ||
		    __put_user(cregs->y, (&pregs->y))) {
			pt_error_return(regs, EFAULT);
			goto out;
		}
		for(rval = 1; rval < 16; rval++)
			if (__put_user(cregs->u_regs[rval], (&pregs->u_regs[rval - 1]))) {
				pt_error_return(regs, EFAULT);
				goto out;
			}
		pt_succ_return(regs, 0);
#ifdef DEBUG_PTRACE
		printk ("PC=%lx nPC=%lx o7=%lx\n", cregs->tpc, cregs->tnpc, cregs->u_regs [15]);
#endif
		goto out;
	}

	case PTRACE_GETREGS64: {
		struct pt_regs *pregs = (struct pt_regs *) addr;
		struct pt_regs *cregs = child->thread.kregs;
		int rval;

		if (__put_user(cregs->tstate, (&pregs->tstate)) ||
		    __put_user(cregs->tpc, (&pregs->tpc)) ||
		    __put_user(cregs->tnpc, (&pregs->tnpc)) ||
		    __put_user(cregs->y, (&pregs->y))) {
			pt_error_return(regs, EFAULT);
			goto out;
		}
		for(rval = 1; rval < 16; rval++)
			if (__put_user(cregs->u_regs[rval], (&pregs->u_regs[rval - 1]))) {
				pt_error_return(regs, EFAULT);
				goto out;
			}
		pt_succ_return(regs, 0);
#ifdef DEBUG_PTRACE
		printk ("PC=%lx nPC=%lx o7=%lx\n", cregs->tpc, cregs->tnpc, cregs->u_regs [15]);
#endif
		goto out;
	}

	case PTRACE_SETREGS: {
		struct pt_regs32 *pregs = (struct pt_regs32 *) addr;
		struct pt_regs *cregs = child->thread.kregs;
		unsigned int psr, pc, npc, y;
		int i;

		/* Must be careful, tracing process can only set certain
		 * bits in the psr.
		 */
		if (__get_user(psr, (&pregs->psr)) ||
		    __get_user(pc, (&pregs->pc)) ||
		    __get_user(npc, (&pregs->npc)) ||
		    __get_user(y, (&pregs->y))) {
			pt_error_return(regs, EFAULT);
			goto out;
		}
		cregs->tstate &= ~(TSTATE_ICC);
		cregs->tstate |= psr_to_tstate_icc(psr);
               	if(!((pc | npc) & 3)) {
			cregs->tpc = pc;
			cregs->tnpc = npc;
		}
		cregs->y = y;
		for(i = 1; i < 16; i++) {
			if (__get_user(cregs->u_regs[i], (&pregs->u_regs[i-1]))) {
				pt_error_return(regs, EFAULT);
				goto out;
			}
		}
		pt_succ_return(regs, 0);
		goto out;
	}

	case PTRACE_SETREGS64: {
		struct pt_regs *pregs = (struct pt_regs *) addr;
		struct pt_regs *cregs = child->thread.kregs;
		unsigned long tstate, tpc, tnpc, y;
		int i;

		/* Must be careful, tracing process can only set certain
		 * bits in the psr.
		 */
		if (__get_user(tstate, (&pregs->tstate)) ||
		    __get_user(tpc, (&pregs->tpc)) ||
		    __get_user(tnpc, (&pregs->tnpc)) ||
		    __get_user(y, (&pregs->y))) {
			pt_error_return(regs, EFAULT);
			goto out;
		}
		tstate &= (TSTATE_ICC | TSTATE_XCC);
		cregs->tstate &= ~(TSTATE_ICC | TSTATE_XCC);
		cregs->tstate |= tstate;
		if(!((tpc | tnpc) & 3)) {
			cregs->tpc = tpc;
			cregs->tnpc = tnpc;
		}
		cregs->y = y;
		for(i = 1; i < 16; i++) {
			if (__get_user(cregs->u_regs[i], (&pregs->u_regs[i-1]))) {
				pt_error_return(regs, EFAULT);
				goto out;
			}
		}
		pt_succ_return(regs, 0);
		goto out;
	}

	case PTRACE_GETFPREGS: {
		struct fps {
			unsigned int regs[32];
			unsigned int fsr;
			unsigned int flags;
			unsigned int extra;
			unsigned int fpqd;
			struct fq {
				unsigned int insnaddr;
				unsigned int insn;
			} fpq[16];
		} *fps = (struct fps *) addr;
		unsigned long *fpregs = (unsigned long *)(((char *)child) + AOFF_task_fpregs);

		if (copy_to_user(&fps->regs[0], fpregs,
				 (32 * sizeof(unsigned int))) ||
		    __put_user(child->thread.xfsr[0], (&fps->fsr)) ||
		    __put_user(0, (&fps->fpqd)) ||
		    __put_user(0, (&fps->flags)) ||
		    __put_user(0, (&fps->extra)) ||
		    clear_user(&fps->fpq[0], 32 * sizeof(unsigned int))) {
			pt_error_return(regs, EFAULT);
			goto out;
		}
		pt_succ_return(regs, 0);
		goto out;
	}

	case PTRACE_GETFPREGS64: {
		struct fps {
			unsigned int regs[64];
			unsigned long fsr;
		} *fps = (struct fps *) addr;
		unsigned long *fpregs = (unsigned long *)(((char *)child) + AOFF_task_fpregs);

		if (copy_to_user(&fps->regs[0], fpregs,
				 (64 * sizeof(unsigned int))) ||
		    __put_user(child->thread.xfsr[0], (&fps->fsr))) {
			pt_error_return(regs, EFAULT);
			goto out;
		}
		pt_succ_return(regs, 0);
		goto out;
	}

	case PTRACE_SETFPREGS: {
		struct fps {
			unsigned int regs[32];
			unsigned int fsr;
			unsigned int flags;
			unsigned int extra;
			unsigned int fpqd;
			struct fq {
				unsigned int insnaddr;
				unsigned int insn;
			} fpq[16];
		} *fps = (struct fps *) addr;
		unsigned long *fpregs = (unsigned long *)(((char *)child) + AOFF_task_fpregs);
		unsigned fsr;

		if (copy_from_user(fpregs, &fps->regs[0],
				   (32 * sizeof(unsigned int))) ||
		    __get_user(fsr, (&fps->fsr))) {
			pt_error_return(regs, EFAULT);
			goto out;
		}
		child->thread.xfsr[0] &= 0xffffffff00000000UL;
		child->thread.xfsr[0] |= fsr;
		if (!(child->thread.fpsaved[0] & FPRS_FEF))
			child->thread.gsr[0] = 0;
		child->thread.fpsaved[0] |= (FPRS_FEF | FPRS_DL);
		pt_succ_return(regs, 0);
		goto out;
	}

	case PTRACE_SETFPREGS64: {
		struct fps {
			unsigned int regs[64];
			unsigned long fsr;
		} *fps = (struct fps *) addr;
		unsigned long *fpregs = (unsigned long *)(((char *)child) + AOFF_task_fpregs);

		if (copy_from_user(fpregs, &fps->regs[0],
				   (64 * sizeof(unsigned int))) ||
		    __get_user(child->thread.xfsr[0], (&fps->fsr))) {
			pt_error_return(regs, EFAULT);
			goto out;
		}
		if (!(child->thread.fpsaved[0] & FPRS_FEF))
			child->thread.gsr[0] = 0;
		child->thread.fpsaved[0] |= (FPRS_FEF | FPRS_DL | FPRS_DU);
		pt_succ_return(regs, 0);
		goto out;
	}

	case PTRACE_READTEXT:
	case PTRACE_READDATA: {
		int res = ptrace_readdata(child, addr,
					  (void *)addr2, data);
		if (res == data) {
			pt_succ_return(regs, 0);
			goto flush_and_out;
		}
		if (res >= 0)
			res = -EIO;
		pt_error_return(regs, -res);
		goto flush_and_out;
	}

	case PTRACE_WRITETEXT:
	case PTRACE_WRITEDATA: {
		int res = ptrace_writedata(child, (void *) addr2,
					   addr, data);
		if (res == data) {
			pt_succ_return(regs, 0);
			goto flush_and_out;
		}
		if (res >= 0)
			res = -EIO;
		pt_error_return(regs, -res);
		goto flush_and_out;
	}
	case PTRACE_SYSCALL: /* continue and stop at (return from) syscall */
		addr = 1;

	case PTRACE_CONT: { /* restart after signal. */
		if (data > _NSIG) {
			pt_error_return(regs, EIO);
			goto out;
		}
		if (addr != 1) {
			if (addr & 3) {
				pt_error_return(regs, EINVAL);
				goto out;
			}
#ifdef DEBUG_PTRACE
			printk ("Original: %016lx %016lx\n", child->thread.kregs->tpc, child->thread.kregs->tnpc);
			printk ("Continuing with %016lx %016lx\n", addr, addr+4);
#endif
			child->thread.kregs->tpc = addr;
			child->thread.kregs->tnpc = addr + 4;
		}

		if (request == PTRACE_SYSCALL)
			child->ptrace |= PT_TRACESYS;
		else
			child->ptrace &= ~PT_TRACESYS;

		child->exit_code = data;
#ifdef DEBUG_PTRACE
		printk("CONT: %s [%d]: set exit_code = %x %lx %lx\n", child->comm,
			child->pid, child->exit_code,
			child->thread.kregs->tpc,
			child->thread.kregs->tnpc);
		       
#endif
		wake_up_process(child);
		pt_succ_return(regs, 0);
		goto out;
	}

/*
 * make the child exit.  Best I can do is send it a sigkill. 
 * perhaps it should be put in the status that it wants to 
 * exit.
 */
	case PTRACE_KILL: {
		if (child->state == TASK_ZOMBIE) {	/* already dead */
			pt_succ_return(regs, 0);
			goto out;
		}
		child->exit_code = SIGKILL;
		wake_up_process(child);
		pt_succ_return(regs, 0);
		goto out;
	}

	case PTRACE_SUNDETACH: { /* detach a process that was attached. */
		unsigned long flags;

		if ((unsigned long) data > _NSIG) {
			pt_error_return(regs, EIO);
			goto out;
		}
		child->ptrace &= ~(PT_PTRACED|PT_TRACESYS);
		child->exit_code = data;

		write_lock_irqsave(&tasklist_lock, flags);
		REMOVE_LINKS(child);
		child->p_pptr = child->p_opptr;
		SET_LINKS(child);
		write_unlock_irqrestore(&tasklist_lock, flags);

		wake_up_process(child);
		pt_succ_return(regs, 0);
		goto out;
	}

	/* PTRACE_DUMPCORE unsupported... */

	default:
		pt_error_return(regs, EIO);
		goto out;
	}
flush_and_out:
	{
		unsigned long va;
		for(va =  0; va < (PAGE_SIZE << 1); va += 32)
			spitfire_put_dcache_tag(va, 0x0);
		if (request == PTRACE_PEEKTEXT ||
		    request == PTRACE_POKETEXT ||
		    request == PTRACE_READTEXT ||
		    request == PTRACE_WRITETEXT) {
			for(va =  0; va < (PAGE_SIZE << 1); va += 32)
				spitfire_put_icache_tag(va, 0x0);
			__asm__ __volatile__("flush %g6");
		}
	}
out:
	unlock_kernel();
}

asmlinkage void syscall_trace(void)
{
#ifdef DEBUG_PTRACE
	printk("%s [%d]: syscall_trace\n", current->comm, current->pid);
#endif
	if ((current->ptrace & (PT_PTRACED|PT_TRACESYS))
	    != (PT_PTRACED|PT_TRACESYS))
		return;
	current->exit_code = SIGTRAP;
	current->state = TASK_STOPPED;
	current->thread.flags ^= MAGIC_CONSTANT;
	notify_parent(current, SIGCHLD);
	schedule();
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
#ifdef DEBUG_PTRACE
	printk("%s [%d]: syscall_trace exit= %x\n", current->comm,
		current->pid, current->exit_code);
#endif
	if (current->exit_code) {
		send_sig (current->exit_code, current, 1);
		current->exit_code = 0;
	}
}
