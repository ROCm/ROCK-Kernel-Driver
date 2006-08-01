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
#include <linux/irq.h>
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

/* Getting the starting point for a backtrace on a running process is
 * moderately tricky.  kdba_save_running() saved the esp in krp->arch.esp, but
 * that esp is not 100% accurate, it can be offset by a frame pointer or by the
 * size of local variables in kdba_main_loop() and kdb_save_running().
 *
 * The calling sequence is kdb() -> kdba_main_loop() -> kdb_save_running() ->
 * kdba_save_running().  Walk up the stack until we find a return address
 * inside the main kdb() function and start the backtrace from there.
 */

static int
kdba_bt_stack_running(const struct task_struct *p, kdb_machreg_t *eip,
		      kdb_machreg_t *esp, kdb_machreg_t *ebp)
{
	kdb_machreg_t addr, *sp;
	kdb_symtab_t symtab;
	struct kdb_running_process *krp = kdb_running_process + task_cpu(p);

	if (kdbgetsymval("kdb", &symtab) == 0)
		return 0;
	if (kdbnearsym(symtab.sym_start, &symtab) == 0)
		return 0;
	for (sp = (long *)krp->arch.esp; ; ++sp) {
		addr = *sp;
		if (addr >= symtab.sym_start && addr < symtab.sym_end)
			break;
	}
	*esp = (kdb_machreg_t)sp;
	*ebp = *esp;
	*eip = *sp;
	return 1;
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
	kdb_ar_t ar;
	kdb_machreg_t eip, esp, ebp, ss, cs, esp_base;
	kdb_symtab_t symtab;
	int count, kernel_stack, alt_stack = 0, btsp = 0, suppress;
	struct pt_regs *regs = NULL;

	kdbgetintenv("BTSP", &btsp);
	suppress = !btsp;

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
		suppress = 0;
	} else {
		if (task_curr(p)) {
			struct kdb_running_process *krp = kdb_running_process + task_cpu(p);

			if (krp->seqno && krp->p == p && krp->seqno >= kdb_seqno - 1) {
				/* valid saved state, continue processing */
			} else {
				kdb_printf("Process did not save state, cannot backtrace\n");
				kdb_ps1(p);
				return 0;
			}
			regs = krp->regs;
			if (KDB_NULL_REGS(regs))
				return KDB_BADREG;
			kdba_getregcontents("xcs", regs, &cs);
			kdba_getregcontents("eip", regs, &eip);
			kdba_getregcontents("ebp", regs, &ebp);
			esp = (long)regs;
			if ((cs & 0xffff) == __KERNEL_CS &&
			    kdba_bt_stack_running(p, &eip, &esp, &ebp) == 0) {
				kdb_printf("%s: cannot find backtrace starting point for running task\n",
					   __FUNCTION__);
			}
		} else {
			/* Not on cpu, assume blocked.  Blocked i386 tasks do
			 * not have pt_regs.  p->thread.{esp,eip} are set, esp
			 * points to the ebp value, assume kernel space.
			 */
			eip = p->thread.eip;
			esp = p->thread.esp;
			ebp = *(unsigned long *)esp;
			cs  = __KERNEL_CS;
			suppress = 0;
		}
		esp_base = (unsigned long)(p->thread_info);
		kernel_stack = esp >= esp_base && esp < (esp_base + THREAD_SIZE);
#ifdef	CONFIG_4KSTACKS
		if (!kernel_stack && kdb_task_has_cpu(p)) {
			int cpu = kdb_process_cpu(p);
			struct thread_info *tinfo = (struct thread_info *)(esp & -THREAD_SIZE);
			if (kdba_irq_ctx_type(cpu, tinfo))
				alt_stack = kernel_stack = 1;
		}
#endif	/* CONFIG_4KSTACKS */
		if (!kernel_stack) {
			kdb_printf("esp is not in a valid kernel stack, backtrace not available\n");
			kdb_printf("esp_base=%lx, esp=%lx\n", esp_base, esp);
			return 0;
		}
	}
	ss = esp & -THREAD_SIZE;

	if ((cs & 0xffff) != __KERNEL_CS) {
		kdb_printf("Stack is not in kernel space, backtrace not available\n");
		return 0;
	}

	kdb_printf(EFPSTR "        EIP        Function (args)\n");
	if (alt_stack && !suppress)
		kdb_printf("Starting on an alternate kernel stack\n");

	/*
	 * Run through the activation records and print them.
	 */

	alt_stack = 0;
	for (count = 0; count < 200; ++count) {
		kdb_ar_t save_ar = ar;
		kdbnearsym(eip, &symtab);
		if (!kdb_get_next_ar(esp, symtab.sym_start, eip, ebp, ss,
			&ar, &symtab)) {
			struct thread_info *tinfo = (struct thread_info *)ss;
			esp = tinfo->previous_esp;
			if (!esp)
				break;
			ss = esp & -THREAD_SIZE;
			if (!suppress)
				kdb_printf(" =======================\n");
			alt_stack = 1;
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
					if (!suppress)
						bt_print_one(eip, NOBP, &ar, &symtab, 0);
					/* Point to mainline code */
					eip = realeip;
					ar = save_ar;	/* lock text does not consume an activation frame */
					continue;
				}
			}
		}

		if (strcmp("ret_from_intr", symtab.sym_name) == 0 ||
		    strcmp("error_code", symtab.sym_name) == 0 ||
		    strcmp("kdb_call", symtab.sym_name) == 0) {
			    int parms;
			if (strcmp("ret_from_intr", symtab.sym_name) == 0) {
				/*
				 * Non-standard frame.  ret_from_intr is
				 * preceded by pt_regs;
				 */
				parms = 0;
			} else if (strcmp("error_code", symtab.sym_name) == 0) {
				/*
				 * Non-standard frame.  error_code is preceded
				 * by two parameters (-> registers, error
				 * code), and pt_regs.
				 */
				parms = 2;
			} else if (strcmp("kdb_call", symtab.sym_name) == 0) {
				/*
				 * Non-standard frame.  kdb_call is preceded by
				 * three parameters (-> registers, error code,
				 * kdb reason code) and pt_regs.
				 */
				parms = 3;
			} else {
				kdb_printf("%s: unexpected special case function name '%s'\n",
					   __FUNCTION__, symtab.sym_name);
				parms = 0;
			}
			/* Adjust esp to pt_regs by the number of parameters to
			 * get pt_regs
			 */
			esp = ar.end + parms*4;
			ebp = 0;
			/* Adjust by the number of number of registers in
			 * pt_regs (excluding the old esp and xss which may not
			 * be on this stack) plus the original eax and the
			 * return address.
			 */
			ar.start = esp + (9 + 2)*4;
		}

		if (alt_stack) {
			ebp = 0;
			alt_stack = 0;
		} else {
			if (!suppress)
				bt_print_one(eip, EFP, &ar, &symtab, argcount);
			if (ar.ret == 0)
				break;	/* End of frames */
			if (esp == (long)regs) {
				/* reached the code that was interrupted */
				if (!suppress) {
					kdb_printf("Interrupt registers:\n");
					kdba_dumpregs((struct pt_regs *)(esp), NULL, NULL);
				}
				eip = regs->eip;
				kdbnearsym(eip, &symtab);
				bt_print_one(eip, NOBP, &ar, &symtab, 0);
				suppress = 0;
			}
			eip = ar.ret;
			ebp = ar.oldfp;
			esp = ar.start;
		}
	}
	if (count >= 200)
		kdb_printf("bt truncated, count limit reached\n");
	else if (suppress)
		kdb_printf("bt did not find pt_regs - no trace produced.  Suggest 'set BTSP 1'\n");

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
