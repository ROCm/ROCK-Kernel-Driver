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

	kdb_machreg_t	esp,eip,ebp,old_esp;
/*	kdb_symtab_t	symtab, *sym; */
	kdbtbtable_t	tbtab;
	/* declare these as raw ptrs so we don't get func descriptors */
	extern void *ret_from_except, *ret_from_syscall_1;
/*	int do_bottom_half_ret=0; */

	const char *name;
	char namebuf[128];
	unsigned long symsize,symoffset;
	char *symmodname;

	if (!regs && !addr)
	{
		kdb_printf(" invalid regs pointer \n");
		return 0;
	}

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
	if (addr) {
		eip = 0;
		esp = *addr;
		ebp=0;
	} else {
		ebp=regs->link;
		eip = regs->nip;
		if (regs_esp)
			esp = regs->gpr[1];
		else
			kdba_getregcontents("esp", regs, &esp);
	}

	kdb_printf("          SP(esp)            PC(eip)      Function(args)\n");

	/* (Ref: 64-bit PowerPC ELF ABI Spplement; Ian Lance Taylor, Zembu Labs).
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
		/*		kdbnearsym(eip, &symtab); */
		kdba_find_tb_table(eip, &tbtab); 

		/*		sym = symtab.sym_name ? &symtab : &tbtab.symtab; *//* use fake symtab if necessary */
		name = NULL;
		if (esp >= PAGE_OFFSET) { 
			/*if ((sym) )*/ 
			/* if this fails, eip is outside of kernel space, dont trust it. */
			if (eip > PAGE_OFFSET) {
				name = kallsyms_lookup(eip, &symsize, &symoffset, &symmodname,
						       namebuf);
			}
			if (name) { 
				kdb_printf("%s", name);
			} else {
				kdb_printf("NO_SYMBOL or Userspace"); 
			}
		}

		/* if this fails, eip is outside of kernel space, dont trust data. */
		if (eip > PAGE_OFFSET) { 
			if (eip - symoffset > 0) {
				kdb_printf(" +0x%lx", /*eip -*/ symoffset);
			}
		}
		kdb_printf("\n");

		/* ret_from_except=0xa5e0 ret_from_syscall_1=a378 do_bottom_half_ret=a5e0 */
		if (esp < PAGE_OFFSET) { /* below kernelspace..   */
			kdb_printf("<Stack contents outside of kernel space.  %.16lx>\n", esp );
			break;
		} else {
			if (eip == (kdb_machreg_t)ret_from_except ||
			    eip == (kdb_machreg_t)ret_from_syscall_1 /* ||
								      eip == (kdb_machreg_t)do_bottom_half_ret */) {
				/* pull exception regs from the stack */
				struct pt_regs eregs;
				kdba_getmem(esp+STACK_FRAME_OVERHEAD, &eregs, sizeof(eregs));
				kdb_printf("  [exception: %lx:%s regs 0x%lx] nip:[0x%lx] gpr[1]:[0x%x]\n", eregs.trap,getvecname(eregs.trap), esp+STACK_FRAME_OVERHEAD,(unsigned int)eregs.nip,(unsigned int)eregs.gpr[1]);
				old_esp = esp;
				esp = kdba_getword(esp, 8);
				if (!esp)
					break;
				eip = kdba_getword(esp+16, 8);	/* saved lr */
				if (esp < PAGE_OFFSET) {  /* userspace... */
					if (old_esp > PAGE_OFFSET) {
						kdb_printf("<Stack drops into userspace here %.16lx>\n",esp);
						break;
					}
				}
				/* we want to follow exception registers, not into user stack.  ...   */
				esp = eregs.gpr[1];
				eip = eregs.nip;
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

int
kdba_bt_process(struct task_struct *p, int argcount)
{
	return (kdba_bt_stack_ppc(p->thread.regs, (kdb_machreg_t *) p->thread.ksp, argcount, p, 0));
}
