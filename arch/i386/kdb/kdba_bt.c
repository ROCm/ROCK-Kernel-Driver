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
#define EFPSTR	"EBP"
#define EFP	ebp
#define NOBP	0
#else
#define EFPSTR	"ESP"
#define EFP	esp
#define NOBP	esp
#endif

/*
 * bt_print_one
 *
 *	Print one back trace entry.
 *
 * Inputs:
 *	eip	Current program counter, or return address.
 *	efp	#ifdef CONFIG_FRAME_POINTER: Previous frame pointer ebp,
 *		0 if not valid; #else: Stack pointer esp when at eip.
 *	ar	Activation record for this frame.
 *	symtab	Information about symbol that eip falls within.
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
bt_print_one(kdb_machreg_t eip, kdb_machreg_t efp, const kdb_ar_t *ar,
	     const kdb_symtab_t *symtab, int argcount)
{
	int btsymarg = 0;
	int nosect = 0;
	kdb_machreg_t word;

	kdbgetintenv("BTSYMARG", &btsymarg);
	kdbgetintenv("NOSECT", &nosect);

	if (efp)
		kdb_printf("0x%08lx", efp);
	else
		kdb_printf("          ");
	kdb_symbol_print(eip, symtab, KDB_SP_SPACEB|KDB_SP_VALUE);
	if (argcount && ar->args) {
		int i, argc = ar->args / 4;

		kdb_printf(" (");
		if (argc > argcount)
			argc = argcount;

		for(i=1; i<=argc; i++){
			kdb_machreg_t argp = ar->arg0 - ar->args + 4*i;

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
			kdb_printf("                               %s",
				symtab->mod_name);
			if (symtab->sec_name && symtab->sec_start)
				kdb_printf(" 0x%lx 0x%lx",
					symtab->sec_start,
					symtab->sec_end);
			kdb_printf(" 0x%lx 0x%lx",
				symtab->sym_start,
				symtab->sym_end);
		}
	}
	kdb_printf("\n");
	if (argcount && ar->args && btsymarg) {
		int i, argc = ar->args / 4;
		kdb_symtab_t arg_symtab;
		kdb_machreg_t arg;
		for(i=1; i<=argc; i++){
			kdb_machreg_t argp = ar->arg0 - ar->args + 4*i;
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
kdba_bt_stack(kdb_machreg_t addr, int argcount, struct task_struct *p)
{
	kdb_ar_t ar;
	kdb_machreg_t eip, esp, ebp, ss, cs, esp_base;
	kdb_symtab_t symtab;
	int count;

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
		ebp = 0;
		esp = addr;
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
			kdba_getregcontents("eip", regs, &eip);
			kdba_getregcontents("ebp", regs, &ebp);
			kdba_getregcontents("esp", regs, &esp);
			kdba_getregcontents("xcs", regs, &cs);
		}
		else {
			/* Not on cpu, assume blocked.  Blocked i386 tasks do
			 * not have pt_regs.  p->thread.{esp,eip} are set, esp
			 * points to the ebp value, assume kernel space.
			 */
			eip = p->thread.eip;
			esp = p->thread.esp;
			ebp = *(unsigned long *)esp;
			cs  = __KERNEL_CS;
		}
		esp_base = (unsigned long)(p->thread_info);
		if (esp < esp_base || esp >= (esp_base + THREAD_SIZE)) {
			kdb_printf("Stack is not in thread_info, backtrace not available\n");
			kdb_printf("esp_base=%lx, esp=%lx\n", esp_base, esp);
			return(0);
		}
	}
	ss = esp & -THREAD_SIZE;

	if ((cs & 0xffff) != __KERNEL_CS) {
		kdb_printf("Stack is not in kernel space, backtrace not available\n");
		return 0;
	}

	kdb_printf(EFPSTR "        EIP        Function (args)\n");

	/*
	 * Run through the activation records and print them.
	 */

	for (count = 0; count < 200; ++count) {
		kdb_ar_t save_ar = ar;
		kdbnearsym(eip, &symtab);
		if (!kdb_get_next_ar(esp, symtab.sym_start, eip, ebp, ss,
			&ar, &symtab)) {
			break;
		}

		if (strncmp(".text.lock.", symtab.sym_name, 11) == 0) {
			/*
			 * Instructions in the .text.lock area are generated by
			 * the out of line code in lock handling, see
			 * include/asm-i386 semaphore.h and rwlock.h.  There can
			 * be multiple instructions which eventually end with a
			 * jump back to the mainline code.  Use the disassmebler
			 * to silently step through the code until we find the
			 * jump, resolve its destination and translate it to a
			 * symbol.  Replace '.text.lock' with the symbol.
			 */
			unsigned char inst;
			kdb_machreg_t offset = 0, realeip = eip;
			int length, offsize = 0;
			kdb_symtab_t lock_symtab;
			/* Dummy out the disassembler print function */
			fprintf_ftype save_fprintf_func = kdb_di.fprintf_func;

			kdb_di.fprintf_func = &kdb_dis_fprintf_dummy;
			while((length = kdba_id_printinsn(realeip, &kdb_di)) > 0) {
				kdb_getarea(inst, realeip);
				offsize = 0;
				switch (inst) {
				case 0xeb:	/* jmp with 1 byte offset */
					offsize = 1-4;
					/* drop through */
				case 0xe9:	/* jmp with 4 byte offset */
					offsize += 4;
					kdb_getword(&offset, realeip+1, offsize);
					break;
				default:
					realeip += length;	/* next instruction */
					break;
				}
				if (offsize)
					break;
			}
			kdb_di.fprintf_func = save_fprintf_func;

			if (offsize) {
				realeip += 1 + offsize + (offsize == 1 ? (s8)offset : (s32)offset);
				if (kdbnearsym(realeip, &lock_symtab)) {
					/* Print the stext entry without args */
					bt_print_one(eip, NOBP, &ar, &symtab, 0);
					/* Point to mainline code */
					eip = realeip;
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
			bt_print_one(eip, NOBP, &ar, &symtab, 0);
			kdb_printf("Interrupt registers:\n");
			kdba_dumpregs((struct pt_regs *)(ar.end), NULL, NULL);
			/* Step the frame to the interrupted code */
			kdb_getword(&eip, ar.start-4, 4);
			ebp = 0;
			esp = ar.start;
			if ((((struct pt_regs *)(ar.end))->xcs & 0xffff) != __KERNEL_CS) {
				kdb_printf("Interrupt from user space, end of kernel trace\n");
				break;
			}
			continue;
		}

		bt_print_one(eip, EFP, &ar, &symtab, argcount);

		if (ar.ret == 0)
			break;	/* End of frames */
		eip = ar.ret;
		ebp = ar.oldfp;
		esp = ar.start;
	}
	if (count >= 200)
		kdb_printf("bt truncated, count limit reached\n");

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
kdba_bt_process(struct task_struct *p, int argcount)
{
	return kdba_bt_stack(0, argcount, p);
}
