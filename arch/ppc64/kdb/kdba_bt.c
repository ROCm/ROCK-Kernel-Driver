/*
 * Minimalist Kernel Debugger - Architecture Dependent Stack Traceback
 *
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) Scott Lurndal (slurn@engr.sgi.com)
 * Copyright (C) Scott Foehner (sfoehner@engr.sgi.com)
 * Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
 *
 * See the file LIA-COPYRIGHT for additional information.
 *
 * Written March 1999 by Scott Lurndal at Silicon Graphics, Inc.
 *
 * Modifications from:
 *      Richard Bass                    1999/07/20
 *              Many bug fixes and enhancements.
 *      Scott Foehner
 *              Port to ia64
 *      Srinivasa Thirumalachar
 *              RSE support for ia64
 *	Masahiro Adegawa                1999/12/01
 *		'sr' command, active flag in 'ps'
 *	Scott Lurndal			1999/12/12
 *		Significantly restructure for linux2.3
 *	Keith Owens			2000/05/23
 *		KDB v1.2
 *
 */

#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kallsyms.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <linux/ptrace.h>	/* for STACK_FRAME_OVERHEAD */
#include <asm/system.h>
#include "privinst.h"

void systemreset(struct pt_regs *regs)
{
	udbg_printf("Oh no!\n");
	kdb_printf("Oh no!\n");
	kdb(KDB_REASON_OOPS, 0, (kdb_eframe_t) regs);
	for (;;);
}

/* human name vector lookup. */
static
const char *getvecname(unsigned long vec)
{
	char *ret;
	switch (vec) {
	case 0x100:	ret = "(System Reset)"; break; 
	case 0x200:	ret = "(Machine Check)"; break; 
	case 0x300:	ret = "(Data Access)"; break; 
	case 0x400:	ret = "(Instruction Access)"; break; 
	case 0x500:	ret = "(Hardware Interrupt)"; break; 
	case 0x600:	ret = "(Alignment)"; break; 
	case 0x700:	ret = "(Program Check)"; break; 
	case 0x800:	ret = "(FPU Unavailable)"; break; 
	case 0x900:	ret = "(Decrementer)"; break; 
	case 0xc00:	ret = "(System Call)"; break; 
	case 0xd00:	ret = "(Single Step)"; break; 
	case 0xf00:	ret = "(Performance Monitor)"; break; 
	default: ret = "";
	}
	return ret;
}

extern unsigned long kdba_getword(unsigned long addr, size_t width);

/* Copy a block of memory using kdba_getword().
 * This is not efficient.
 */
static void kdba_getmem(unsigned long addr, void *p, int size)
{
	unsigned char *dst = (unsigned char *)p;
	while (size > 0) {
		*dst++ = kdba_getword(addr++, 1);
		size--;
	}
}

/*
 * kdba_bt_stack_ppc
 *
 *	kdba_bt_stack with ppc specific parameters.
 *	Specification as kdba_bt_stack plus :-
 *
 * Inputs:
 *	As kba_bt_stack plus
 *	regs_esp If 1 get esp from the registers (exception frame), if 0
 *		 get esp from kdba_getregcontents.
 */

static int
kdba_bt_stack_ppc(struct pt_regs *regs, kdb_machreg_t *addr, int argcount,
		   struct task_struct *p, int regs_esp)
{

	kdb_machreg_t esp, eip, ebp;

	const char *name;
	unsigned long symsize, symoffset;
	char *symmodname;
	int flag = 0;
	kdb_machreg_t lr;
	char namebuf[128];

	/*
	 * The caller may have supplied an address at which the
	 * stack traceback operation should begin.  This address
	 * is assumed by this code to point to a return-address
	 * on the stack to be traced back.
	 *
	 * The end result of this will make it appear as if a function
	 * entitled '<unknown>' was called from the function which
	 * contains return-address.
	 */
	if (!addr)
		addr = (kdb_machreg_t *)p->thread.ksp;

	if (addr && (!p || !task_curr(p))) {
		eip = 0;
		esp = *addr;
		ebp = 0;
	} else {
		if (task_curr(p)) {
			struct kdb_running_process *krp = kdb_running_process + task_cpu(p);
			if (!krp->seqno) {
				kdb_printf("Process did not save state, cannot backtrace \n");
				kdb_ps1(p);
				return 0;
			}
			regs = krp->regs;
		} else {
			if (!regs)
				regs = p->thread.regs;
		}
		if (KDB_NULL_REGS(regs))
			return KDB_BADREG;
		ebp = regs->link;
		eip = regs->nip;
		if (regs_esp)
			esp = regs->gpr[1];
		else
			kdba_getregcontents("esp", regs, &esp);
	}

	kdb_printf("          SP(esp)            PC(eip)      Function(args)\n");

	/* (Ref: 64-bit PowerPC ELF ABI Supplement: 
				Ian Lance Taylor, Zembu Labs).
	 A PPC stack frame looks like this:

	 High Address
	 Back Chain
	 FP reg save area
	 GP reg save area
	 Local var space
	 Parameter save area		(SP+48)
	 TOC save area		(SP+40)
	 link editor doubleword	(SP+32)
	 compiler doubleword		(SP+24)
	 LR save			(SP+16)
	 CR save			(SP+8)
	 Back Chain			(SP+0)

	 Note that the LR (ret addr) may not be saved in the *current* frame if
	 no functions have been called from the current function.
	 */

	/*
	 * Run through the activation records and print them.
	 */
	while (1) {
		kdb_printf("0x%016lx  0x%016lx  ", esp, eip);
		name = NULL;
		if (esp >= PAGE_OFFSET) { 
			/* 
			 * if this fails, eip is outside of kernel space, 
			 * dont trust it. 
			 */
			if (eip > PAGE_OFFSET) {
				name = kallsyms_lookup(eip, &symsize, 
					&symoffset, &symmodname, namebuf);
			}
			if (name) { 
				kdb_printf("%s", name);
			} else {
				kdb_printf("NO_SYMBOL or Userspace"); 
			}
		}

		/* 
		 * if this fails, eip is outside of kernel space, 
		 * dont trust data. 
		 */
		if (eip > PAGE_OFFSET) { 
			if (eip - symoffset > 0) {
				kdb_printf(" +0x%lx", /*eip -*/ symoffset);
			}
		}
		kdb_printf("\n");
		if (!flag && (task_curr(p))) {
			unsigned long start = 0, end = 0;

			flag++;
			if ((!regs) || (regs->link < PAGE_OFFSET) || 
					(regs->link == eip))
				goto next_frame;

			lr = regs->link;
			start = eip - symoffset;
			end = eip - symoffset + symsize;
			if (lr >= start && lr < end)
				goto next_frame;

			name = NULL;
			name = kallsyms_lookup(lr, &symsize, 
				&symoffset, &symmodname, namebuf);
			if (name)
				kdb_printf("0x%016lx  0x%016lx (lr) %s +0x%lx\n", 
					esp, lr, name, symoffset);
		}

next_frame:
		if (esp < PAGE_OFFSET) { /* below kernelspace..   */
			kdb_printf("<Stack contents outside of kernel space.  %.16lx>\n", esp );
			break;
		} else {
			unsigned long *sp = (unsigned long *)esp;
			if (esp <= (unsigned long) p->thread_info + THREAD_SIZE
					 + sizeof(struct pt_regs) + 400
					 && sp[12] == 0x7265677368657265) {
				struct pt_regs eregs;
				kdba_getmem(esp + STACK_FRAME_OVERHEAD, 
						&eregs, sizeof(eregs));
				kdb_printf("--- Exception: %lx: %s ", 
					eregs.trap, getvecname(eregs.trap));
				name = kallsyms_lookup(eregs.nip, &symsize, 
					&symoffset, &symmodname, namebuf);
				if (name) { 
					kdb_printf("at %s +0x%lx\n", 
							name, symoffset);
				} else {
					kdb_printf("NO_SYMBOL or Userspace\n");
				}
				flag = 0;
				/* 
				 * we want to follow exception registers, 
				 * not into user stack.  ...   
				 */
				esp = eregs.gpr[1];
				eip = eregs.nip;
				regs = &eregs;
			} else {
				esp = kdba_getword(esp, 8);
				if (!esp)
					break;
				eip = kdba_getword(esp+16, 8);	/* saved lr */
			}
		}
	}
	return 0;
}


/*
 * kdba_bt_stack
 *
 *	This function implements the 'bt' command.  Print a stack
 *	traceback.
 *
 *	bt [<address-expression>]   (addr-exp is for alternate stacks)
 *	btp <pid>		     (Kernel stack for <pid>)
 *
 * 	address expression refers to a return address on the stack.  It
 *	may be preceeded by a frame pointer.
 *
 * Inputs:
 *	regs	registers at time kdb was entered.
 *	addr	Pointer to Address provided to 'bt' command, if any.
 *	argcount
 *	p	Pointer to task for 'btp' command.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	mds comes in handy when examining the stack to do a manual
 *	traceback.
 */

int
kdba_bt_stack(struct pt_regs *regs, kdb_machreg_t *addr, int argcount,
	      struct task_struct *p)
{
	return(kdba_bt_stack_ppc(regs, addr, argcount, p, 0));
}

/*
 * kdba_bt_address
 *
 *	Do a backtrace starting at a specified stack address.  Use this if the
 *	heuristics get the i386 stack decode wrong.
 *
 * Inputs:
 *	addr	Address provided to 'bt' command.
 *	argcount
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	mds %esp comes in handy when examining the stack to do a manual
 *	traceback.
 */

int
kdba_bt_address(kdb_machreg_t addr, int argcount)
{
	return kdba_bt_stack(NULL, &addr, argcount, NULL);
}

/*
 * kdba_bt_process
 *
 *	Do a backtrace for a specified process.
 *
 * Inputs:
 *	p	Struct task pointer extracted by 'bt' command.
 *	argcount
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 */
int
kdba_bt_process(struct task_struct *p, int argcount)
{
	return (kdba_bt_stack_ppc(NULL, NULL, argcount, p, 0));
}
