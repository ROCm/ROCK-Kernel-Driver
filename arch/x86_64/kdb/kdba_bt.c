/*
 * Kernel Debugger Architecture Dependent Stack Traceback
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/config.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kallsyms.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <asm/system.h>

#ifdef CONFIG_FRAME_POINTER
#define RFPSTR	"RBP"
#define RFP	rbp
#define NOBP	0
#else
#define RFPSTR	"RSP"
#define RFP	rsp
#define NOBP	rsp
#endif

/*
 * bt_print_one
 *
 *	Print one back trace entry.
 *
 * Inputs:
 *	rip	Current program counter, or return address.
 *	efp	#ifdef CONFIG_FRAME_POINTER: Previous frame pointer rbp,
 *		0 if not valid; #else: Stack pointer rsp when at rip.
 *	ar	Activation record for this frame.
 *	symtab	Information about symbol that rip falls within.
 *	argcount Maximum number of arguments to print.
 * Outputs:
 *	None.
 * Returns:
 *	None.
 * Locking:
 *	None.
 * Remarks:
 *	None.
 */

static void
bt_print_one(kdb_machreg_t rip, kdb_machreg_t efp, const kdb_ar_t *ar,
	     const kdb_symtab_t *symtab, int argcount)
{
	int	btsymarg = 0;
	int	nosect = 0;
	kdb_machreg_t word;

	kdbgetintenv("BTSYMARG", &btsymarg);
	kdbgetintenv("NOSECT", &nosect);

	if (efp)
		kdb_printf("0x%08lx", efp);
	else
		kdb_printf("          ");
	kdb_symbol_print(rip, symtab, KDB_SP_SPACEB|KDB_SP_VALUE);
	if (argcount && ar->args) {
		int i, argc = ar->args / 8;

		kdb_printf(" (");
		if (argc > argcount)
			argc = argcount;

		for(i=1; i<=argc; i++){
			kdb_machreg_t argp = ar->arg0 - ar->args + 8*i;

			if (i != 1)
				kdb_printf(", ");
			kdb_getword(&word, argp, sizeof(word));
			kdb_printf("0x%lx", word);
		}
		kdb_printf(")");
	}
	if (symtab->sym_name) {
		if (!nosect) {
			kdb_printf("\n");
			kdb_printf("                                  %s %s 0x%lx 0x%lx 0x%lx",
				symtab->mod_name,
				symtab->sec_name,
				symtab->sec_start,
				symtab->sym_start,
				symtab->sym_end);
		}
	}
	kdb_printf("\n");
	if (argcount && ar->args && btsymarg) {
		int i, argc = ar->args / 8;
		kdb_symtab_t	arg_symtab;
		kdb_machreg_t	arg;
		for(i=1; i<=argc; i++){
			kdb_machreg_t argp = ar->arg0 - ar->args + 8*i;
			kdb_getword(&arg, argp, sizeof(arg));
			if (kdbnearsym(arg, &arg_symtab)) {
				kdb_printf("                               ");
				kdb_symbol_print(arg, &arg_symtab, KDB_SP_DEFAULT|KDB_SP_NEWLINE);
			}
		}
	}
}

/*
 * kdba_bt_stack
 *
 * Inputs:
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

static int
kdba_bt_stack(kdb_machreg_t addr, int argcount, const struct task_struct *p)
{
	extern void thread_return(void);
	kdb_ar_t	ar;
	kdb_machreg_t	rip, rsp, rbp, ss, cs;
	kdb_symtab_t	symtab;

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
		rip = 0;
		rbp = 0;
		rsp = addr;
		cs  = __KERNEL_CS;	/* have to assume kernel space */
	} else {
		if (task_curr(p)) {
			struct kdb_running_process *krp = kdb_running_process + task_cpu(p);
			struct pt_regs *regs;

			if (!krp->seqno) {
				kdb_printf("Process did not save state, cannot backtrace\n");
				kdb_ps1(p);
				return 0;
			}
			regs = krp->regs;
			if (KDB_NULL_REGS(regs))
				return KDB_BADREG;
			kdba_getregcontents("rip", regs, &rip);
			kdba_getregcontents("rbp", regs, &rbp);
			kdba_getregcontents("rsp", regs, &rsp);
			kdba_getregcontents("cs", regs, &cs);
		}
		else {
			/* Not on cpu, assume blocked.  Blocked tasks do
			 * not have pt_regs.  p->thread.rsp is set, rsp
			 * points to the rbp value, assume kernel space.
			 */
			rsp = p->thread.rsp;
			/*
			 * The rip is no longer in the thread struct.
			 * We know that the stack value was saved in
			 * schedule near the label thread_return.
			 * Setting rip to thread_return-1 lets the
			 * stack trace find that we are in schedule
			 * and correctly decode its prologue.  We
			 * extract the saved rbp and adjust the stack
			 * to undo the effects of the inline assembly
			 * code which switches the stack.
			 */
			rbp = *(unsigned long *)rsp;
			rip = (kdb_machreg_t)&thread_return-1;
			rsp = rsp + 16;
			cs  = __KERNEL_CS;
		}
	}
	ss = rsp & -THREAD_SIZE;

	if ((cs & 0xffff) != __KERNEL_CS) {
		kdb_printf("Stack is not in kernel space, backtrace not available\n");
		return 0;
	}

	kdb_printf(RFPSTR "           RIP                Function (args)\n");

	/*
	 * Run through the activation records and print them.
	 */

	while (1) {
		kdb_ar_t save_ar = ar;
		kdbnearsym(rip, &symtab);
		if (!kdb_get_next_ar(rsp, symtab.sym_start, rip, rbp, ss,
			&ar, &symtab)) {
			break;
		}

		if (strncmp(".text.lock.", symtab.sym_name, 11) == 0) {
			/*
			 * Instructions in the .text.lock area are generated by
			 * the out of line code in lock handling, see
			 * include/asm-x86_64 semaphore.h and rwlock.h.  There can
			 * be multiple instructions which eventually end with a
			 * jump back to the mainline code.  Use the disassmebler
			 * to silently step through the code until we find the
			 * jump, resolve its destination and translate it to a
			 * symbol.  Replace '.text.lock' with the symbol.
			 */
			unsigned char inst;
			kdb_machreg_t offset = 0, realrip = rip;
			int length, offsize = 0;
			kdb_symtab_t lock_symtab;
			/* Dummy out the disassembler print function */
			fprintf_ftype save_fprintf_func = kdb_di.fprintf_func;

			kdb_di.fprintf_func = &kdb_dis_fprintf_dummy;
			while((length = kdba_id_printinsn(realrip, &kdb_di)) > 0) {
				kdb_getarea(inst, realrip);
				offsize = 0;
				switch (inst) {
				case 0xeb:	/* jmp with 1 byte offset */
					offsize = 1-4;
					/* drop through */
				case 0xe9:	/* jmp with 4 byte offset */
					offsize += 4;
					kdb_getword(&offset, realrip+1, offsize);
					break;
				default:
					realrip += length;	/* next instruction */
					break;
				}
				if (offsize)
					break;
			}
			kdb_di.fprintf_func = save_fprintf_func;

			if (offsize) {
				realrip += 1 + offsize + (offsize == 1 ? (s8)offset : (s32)offset);
				if (kdbnearsym(realrip, &lock_symtab)) {
					/* Print the stext entry without args */
					bt_print_one(rip, NOBP, &ar, &symtab, 0);
					/* Point to mainline code */
					rip = realrip;
					ar = save_ar;	/* lock text does not consume an activation frame */
					continue;
				}
			}
		}

		if (strcmp("ret_from_intr", symtab.sym_name) == 0 ||
		    strcmp("error_code", symtab.sym_name) == 0) {
			if (strcmp("ret_from_intr", symtab.sym_name) == 0) {
				/*
				 * Non-standard frame.  ret_from_intr is
				 * preceded by 9 registers (ebx, ecx, edx, esi,
				 * edi, ebp, eax, ds, cs), original eax and the
				 * return address for a total of 11 words.
				 */
				ar.start = ar.end + 11*4;
			}
			if (strcmp("error_code", symtab.sym_name) == 0) {
				/*
				 * Non-standard frame.  error_code is preceded
				 * by two parameters (-> registers, error code),
				 * 9 registers (ebx, ecx, edx, esi, edi, ebp,
				 * eax, ds, cs), original eax and the return
				 * address for a total of 13 words.
				 */
				ar.start = ar.end + 13*4;
			}
			/* Print the non-standard entry without args */
			bt_print_one(rip, NOBP, &ar, &symtab, 0);
			kdb_printf("Interrupt registers:\n");
			kdba_dumpregs((struct pt_regs *)(ar.end), NULL, NULL);
			/* Step the frame to the interrupted code */
			kdb_getword(&rip, ar.start-8, 8);
			rbp = 0;
			rsp = ar.start;
			if ((((struct pt_regs *)(ar.end))->cs & 0xffff) != __KERNEL_CS) {
				kdb_printf("Interrupt from user space, end of kernel trace\n");
				break;
			}
			continue;
		}

		bt_print_one(rip, RFP, &ar, &symtab, argcount);

		if (ar.ret == 0)
			break;	/* End of frames */
		rip = ar.ret;
		rbp = ar.oldfp;
		rsp = ar.start;
	}

	return 0;
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
	return kdba_bt_stack(addr, argcount, NULL);
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
kdba_bt_process(const struct task_struct *p, int argcount)
{
	return kdba_bt_stack(0, argcount, p);
}
