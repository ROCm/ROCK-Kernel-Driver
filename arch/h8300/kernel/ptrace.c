/*
 *  linux/arch/h8300/kernel/ptrace.c
 *
 *  Yoshinori Sato <qzb04471@nifty.ne.jp>
 *
 *  Based on:
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
#include <asm/signal.h>

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
/* Mapping from PT_xxx to the stack offset at which the register is
   saved.  Notice that usp has no stack-slot and needs to be treated
   specially (see get_reg/put_reg below). */
static const int regoff[] = {
	PT_REG(er1), PT_REG(er2), PT_REG(er3), PT_REG(er4),
	PT_REG(er5), PT_REG(er6), PT_REG(er0), PT_REG(orig_er0),
	PT_REG(ccr), PT_REG(pc)
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

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure the single step bit is not set.
 */
int ptrace_cancel_bpt(struct task_struct *child)
{
        int i,r=0;

	for(i=0; i<4; i++) {
	        if (child->thread.debugreg[i]) {
		        if (child->thread.debugreg[i] != ~0)
		                put_user(child->thread.debugreg[i+4],
                                         (unsigned short *)child->thread.debugreg[i]);
			r = 1;
			child->thread.debugreg[i] = 0;
		}
	}
	return r;
}

const static unsigned char opcode0[]={
  0x04,0x02,0x04,0x02,0x04,0x02,0x04,0x02,  /* 0x58 */
  0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,  /* 0x60 */
  0x02,0x02,0x11,0x11,0x02,0x02,0x04,0x04,  /* 0x68 */
  0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,  /* 0x70 */
  0x08,0x04,0x06,0x04,0x04,0x04,0x04,0x04}; /* 0x78 */

const static int table_parser01(unsigned char *pc);
const static int table_parser02(unsigned char *pc);
const static int table_parser100(unsigned char *pc);
const static int table_parser101(unsigned char *pc);

const static int (*parsers[])(unsigned char *pc)={table_parser01,table_parser02};

static int insn_length(unsigned char *pc)
{
  if (*pc == 0x01)
    return table_parser01(pc+1);
  if (*pc < 0x58 || *pc>=0x80) 
    return 2;
  else
    if (opcode0[*pc-0x58]<0x10)
      return opcode0[*pc-0x58];
    else
      return (*parsers[opcode0[*pc-0x58]-0x10])(pc+1);
}

const static int table_parser01(unsigned char *pc)
{
  const unsigned char codelen[]={0x10,0x00,0x00,0x00,0x11,0x00,0x00,0x00,
                                 0x02,0x00,0x00,0x00,0x04,0x04,0x00,0x04};
  const static int (*parsers[])(unsigned char *)={table_parser100,table_parser101};
  unsigned char second_index;
  second_index = (*pc) >> 4;
  if (codelen[second_index]<0x10)
    return codelen[second_index];
  else
    return parsers[codelen[second_index]-0x10](pc);
}

const static int table_parser02(unsigned char *pc)
{
  return (*pc & 0x20)?0x06:0x04;
}

const static int table_parser100(unsigned char *pc)
{
  return (*(pc+2) & 0x02)?0x08:0x06;
}

const static int table_parser101(unsigned char *pc)
{
  return (*(pc+2) & 0x02)?0x08:0x06;
}

#define BREAK_INST 0x5730 /* TRAPA #3 */

int ptrace_set_bpt(struct task_struct *child)
{
        unsigned long pc,next;
	unsigned short insn;
	pc = get_reg(child,PT_PC);
	next = insn_length((unsigned char *)pc) + pc;
	get_user(insn,(unsigned short *)pc);
	if (insn == 0x5470) {
	        /* rts */ 
	        unsigned long sp;
		sp = get_reg(child,PT_USP);
		get_user(next,(unsigned long *)sp);
	} else if ((insn & 0xfb00) != 0x5800) {
	        /* jmp / jsr */
	        int regs;
		const short reg_tbl[]={PT_ER0,PT_ER1,PT_ER2,PT_ER3,
                                       PT_ER4,PT_ER5,PT_ER6,PT_USP};
	        switch(insn & 0xfb00) {
		        case 0x5900:
			       regs = (insn & 0x0070) >> 8;
                               next = get_reg(child,reg_tbl[regs]);
			       break;
		        case 0x5a00:
			       get_user(next,(unsigned long *)(pc+2));
			       next &= 0x00ffffff;
			       break;
		        case 0x5b00:
			       /* unneccessary? */
			       next = *(unsigned long *)(insn & 0xff);
                               break;
		}
	} else if (((insn & 0xf000) == 0x4000) || ((insn &0xff00) == 0x5500)) { 
	        /* b**:8 */
	        unsigned long dsp;
		dsp = (long)(insn && 0xff)+pc+2;
		child->thread.debugreg[1] = dsp;
		get_user(child->thread.debugreg[5],(unsigned short *)dsp);
		put_user(BREAK_INST,(unsigned short *)dsp);
	} else if (((insn & 0xff00) == 0x5800) || ((insn &0xff00) == 0x5c00)) { 
	        /* b**:16 */
	        unsigned long dsp;
		get_user(dsp,(unsigned short *)(pc+2));
		dsp = (long)dsp+pc+4;
		child->thread.debugreg[1] = dsp;
		get_user(child->thread.debugreg[5],(unsigned short *)dsp);
		put_user(BREAK_INST,(unsigned short *)dsp);
	}
	child->thread.debugreg[0] = next;
	get_user(child->thread.debugreg[4],(unsigned short *)next);
	put_user(BREAK_INST,(unsigned short *)next);
	return 0;
}

inline
static int read_long(struct task_struct * tsk, unsigned long addr,
	unsigned long * result)
{
	*result = *(unsigned long *)addr;
	return 0;
}

void ptrace_disable(struct task_struct *child)
{
	ptrace_cancel_bpt(child);
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
	ret = ptrace_check_attach(child, request == PTRACE_KILL);
	if (ret < 0)
		goto out_tsk;

	switch (request) {
		case PTRACE_PEEKTEXT: /* read word at location addr. */ 
		case PTRACE_PEEKDATA: {
			unsigned long tmp;

			ret = read_long(child, addr, &tmp);
			if (ret < 0)
				break ;
			ret = put_user(tmp, (unsigned long *) data);
			break ;
		}

	/* read the word at location addr in the USER area. */
		case PTRACE_PEEKUSR: {
			unsigned long tmp;
			
			if ((addr & 3) || addr < 0 || addr >= sizeof(struct user))
				ret = -EIO;
			
			tmp = 0;  /* Default return condition */
			addr = addr >> 2; /* temporary hack. */
			if (addr < 10)
				tmp = get_reg(child, addr);
			else {
				ret = -EIO;
				break ;
			}
			ret = put_user(tmp,(unsigned long *) data);
			break ;
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
			if ((addr & 3) || addr < 0 || addr >= sizeof(struct user)) {
				ret = -EIO;
				break ;
			}
			addr = addr >> 2; /* temporary hack. */
			    
			if (addr == PT_ORIG_ER0) {
				ret = -EIO;
				break ;
			}
			if (addr == PT_CCR) {
				data &= SR_MASK;
			}
			if (addr < 10) {
				if (put_reg(child, addr, data))
					ret = -EIO;
				else
					ret = 0;
				break ;
			}
			ret = -EIO;
			break ;
		case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
		case PTRACE_CONT: { /* restart after signal. */
			ret = -EIO;
			if ((unsigned long) data >= _NSIG)
				break ;
			if (request == PTRACE_SYSCALL)
				set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
			else
				clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
			child->exit_code = data;
			wake_up_process(child);
			/* make sure the single step bit is not set. */
			ptrace_cancel_bpt(child);
			ret = 0;
		}

/*
 * make the child exit.  Best I can do is send it a sigkill. 
 * perhaps it should be put in the status that it wants to 
 * exit.
 */
		case PTRACE_KILL: {

			ret = 0;
			if (child->state == TASK_ZOMBIE) /* already dead */
				break;
			child->exit_code = SIGKILL;
			ptrace_cancel_bpt(child);
			wake_up_process(child);
			break;
		}

		case PTRACE_SINGLESTEP: {  /* set the trap flag. */
			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				break;
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
			child->thread.debugreg[0]=-1;
			child->exit_code = data;
			wake_up_process(child);
			ret = 0;
			break;
		}

		case PTRACE_DETACH:	/* detach a process that was attached. */
			ret = ptrace_detach(child, data);
			break;

		case PTRACE_GETREGS: { /* Get all gp regs from the child. */
		  	int i;
			unsigned long tmp;
			for (i = 0; i < 19; i++) {
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
			for (i = 0; i < 10; i++) {
			    if (get_user(tmp, (unsigned long *) data)) {
				ret = -EFAULT;
				break;
			    }
			    put_reg(child, i, tmp);
			    data += sizeof(long);
			}
			ret = 0;
			break;
		}

		default:
			ret = -EIO;
			break;
	}
out_tsk:
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

asmlinkage void trace_trap(unsigned long bp)
{
	if (current->thread.debugreg[0] == bp ||
            current->thread.debugreg[1] == bp) {
	        ptrace_cancel_bpt(current);
		force_sig(SIGTRAP,current);
	} else
	        force_sig(SIGILL,current);
}
