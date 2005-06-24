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

/*
 * bt_print_one
 *
 *	Print one back trace entry.
 *
 * Inputs:
 *	ip	Current program counter.
 *	symtab	Information about symbol that ip falls within.
 *	ar	Activation record for this frame.
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
bt_print_one(kdb_machreg_t ip, const kdb_ar_t *ar,
	     const kdb_symtab_t *symtab, int argcount,
	     struct unw_frame_info *info /* FIXME: should be part of ar for ia64 */)
{
	int btsymarg = 0;		/* Convert arguments to symbols */
	int btsp = 0;			/* Print stack and backing store pointers */
	int nosect = 0;			/* Suppress section data */
	kdb_machreg_t sp, bsp, cfm;	/* FIXME: should be part of ar for ia64 */

	kdbgetintenv("BTSYMARG", &btsymarg);
	kdbgetintenv("BTSP", &btsp);
	kdbgetintenv("NOSECT", &nosect);

	unw_get_sp(info, &sp);		/* FIXME: should be part of ar for ia64 */
	unw_get_bsp(info, &bsp);	/* FIXME: should be part of ar for ia64 */
	unw_get_cfm(info, &cfm);	/* FIXME: info/cfm should be part of ar for ia64 */
	kdb_symbol_print(ip, symtab, KDB_SP_VALUE|KDB_SP_NEWLINE);
	/* FIXME: number of args should be set in prologue code */
	((kdb_ar_t *)ar)->args = (cfm >> 7) & 0x7f;	/* sol */
	if (!ar->args)
		((kdb_ar_t *)ar)->args = cfm & 0x7f;	/* no in/local, use sof instead */
	if (argcount && ar->args) {
		int i, argc = ar->args;

		kdb_printf("        args (");
		if (argc > argcount)
			argc = argcount;

		for(i = 0; i < argc; i++){
			/* FIXME: prologue code should extract arguments */
			kdb_machreg_t arg;
			char nat;
			if (unw_access_gr(info, i+32, &arg, &nat, 0))
				arg = 0;

			if (i)
				kdb_printf(", ");
			kdb_printf("0x%lx", arg);
		}
		kdb_printf(")\n");
		if (btsymarg) {
			kdb_symtab_t	arg_symtab;
			kdb_machreg_t	arg;
			for(i = 0; i < argc; i++){
				/* FIXME: prologue code should extract arguments */
				char nat;
				if (unw_access_gr(info, i+32, &arg, &nat, 0))
					arg = 0;
				if (kdbnearsym(arg, &arg_symtab)) {
					kdb_printf("        arg %d ", i);
					kdb_symbol_print(arg, &arg_symtab, KDB_SP_DEFAULT|KDB_SP_NEWLINE);
				}
			}
		}
	}
	if (symtab->sym_name) {
		if (!nosect) {
			kdb_printf("        %s", symtab->mod_name);
			if (symtab->sec_name)
				kdb_printf(" %s 0x%lx", symtab->sec_name, symtab->sec_start);
			kdb_printf(" 0x%lx", symtab->sym_start);
			if (symtab->sym_end)
				kdb_printf(" 0x%lx", symtab->sym_end);
			kdb_printf("\n");
		}
		if (strncmp(symtab->sym_name, "ia64_spinlock_contention", 24) == 0) {
			kdb_machreg_t r31;
			char nat;
			kdb_printf("        r31 (spinlock address) ");
			if (unw_access_gr(info, 31, &r31, &nat, 0))
				r31 = 0;
			kdb_symbol_print(r31, NULL, KDB_SP_VALUE|KDB_SP_NEWLINE);
		}
	}
	if (btsp)
		kdb_printf("        sp 0x%016lx bsp 0x%016lx cfm 0x%016lx info->pfs_loc 0x%016lx 0x%016lx\n",
				sp, bsp, cfm, (u64) info->pfs_loc, info->pfs_loc ? *(info->pfs_loc) : 0);
}

/*
 * kdba_bt_stack
 *
 *	Unwind the ia64 backtrace for a specified process.
 *
 * Inputs:
 *	argcount
 *	p	Pointer to task structure to unwind.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	none.
 */

static int
kdba_bt_stack(int argcount, struct task_struct *p)
{
	kdb_symtab_t symtab;
	kdb_ar_t ar;
	struct unw_frame_info info;	/* FIXME: should be part of ar */
	struct switch_stack *sw;	/* FIXME: should be part of ar */
	struct pt_regs *regs = NULL;	/* FIXME: should be part of ar */
	int count = 0;
	int btsp = 0;			/* Backtrace the kdb code as well */
	u64 *prev_pfs_loc = NULL;
	extern char __attribute__ ((weak)) ia64_spinlock_contention_pre3_4[];
	extern char __attribute__ ((weak)) ia64_spinlock_contention_pre3_4_end[];
	extern char ia64_ivt[];

	/* FIXME: All the arch specific code should be in activation records, not here */
	memset(&ar, 0, sizeof(ar));

	/*
	 * Upon entering kdb_main_loop, the stack frame looks like this:
	 *
	 *	+---------------------+
	 *	|   struct pt_regs    |
	 *	+---------------------+
	 *	|		      |
	 *	|   kernel stack      |
	 *	|		      |
	 *	+=====================+ <--- top of stack upon entering kdb
	 *	|   struct pt_regs    |
	 *	+---------------------+
	 *	|		      |
	 *	|   kdb stack         |
	 *	|		      |
	 *	+---------------------+
	 *	| struct switch_stack |
	 *	+=====================+ <--- kdb_running_process[cpu].arch.sw from do_kdba_main_loop
	 *
	 * When looking at another process, we do not have the address of the
	 * current pt_regs, it is NULL.  If the process has saved its state, use
	 * that pt_regs instead.
	 */

	kdbgetintenv("BTSP", &btsp);

	if (kdb_task_has_cpu(p)) {
		struct kdb_running_process *krp = kdb_running_process + kdb_process_cpu(p);
		if (krp->seqno) {
			sw = krp->arch.sw;
			regs = krp->regs;
		}
		else
			sw = NULL;
	}
	else {
		/* Not running, assume blocked */
		sw = (struct switch_stack *) (p->thread.ksp + 16);
	}
	if (!sw) {
		kdb_printf("Process does not have a switch_stack, cannot backtrace\n");
		kdb_ps1(p);
		return 0;
	}

	unw_init_frame_info(&info, p, sw);	/* FIXME: should be using activation records */

	/* If we have the address of pt_regs, suppress backtrace on the frames below
	 * pt_regs.  No point in displaying kdb itself, unless the user is debugging
	 * the unwinder using set BTSP=1.
	 */
	if (regs && !btsp) {
		kdb_machreg_t sp;		/* FIXME: should be part of ar for ia64 */
		if (user_mode(regs)) {
			kdb_printf("Process was interrupted in user mode, no backtrace available\n");
			return 0;
		}
		do {
			unw_get_sp(&info, &sp);
			if (sp >= (kdb_machreg_t)regs)
				break;
		} while (unw_unwind(&info) >= 0 && count++ < 200);	/* FIXME: should be using activation records */
	}

	do {
		kdb_machreg_t ip;

		/* Avoid unsightly console message from unw_unwind() when attempting
		 * to unwind through the Interrupt Vector Table which has no unwind
		 * information.
		 */
		if (info.ip >= (u64)ia64_ivt && info.ip < (u64)ia64_ivt+32768)
			return 0;

		/* WAR for spinlock contention from leaf functions.  ia64_spinlock_contention_pre3_4
		 * has ar.pfs == r0.  Leaf functions do not modify ar.pfs so ar.pfs remains
		 * as 0, stopping the backtrace.  Record the previous ar.pfs when the current
		 * IP is in ia64_spinlock_contention_pre3_4 then unwind, if pfs_loc has not changed
		 * after unwind then use pt_regs.ar_pfs which is where the real ar.pfs is for
		 * leaf functions.
		 */
		if (prev_pfs_loc && regs && info.pfs_loc == prev_pfs_loc)
			info.pfs_loc = &regs->ar_pfs;
		prev_pfs_loc = (info.ip >= (u64)ia64_spinlock_contention_pre3_4 &&
				info.ip < (u64)ia64_spinlock_contention_pre3_4_end) ?
			       info.pfs_loc : NULL;

		unw_get_ip(&info, &ip);	/* FIXME: should be using activation records */
		if (ip == 0)
			break;

		kdbnearsym(ip, &symtab);
		if (!symtab.sym_name) {
			kdb_printf("0x%0*lx - No name.  May be an area that has no unwind data\n",
				(int)(2*sizeof(ip)), ip);
			return 0;
		}
		bt_print_one(ip, &ar, &symtab, argcount, &info);
	} while (unw_unwind(&info) >= 0 && count++ < 200);	/* FIXME: should be using activation records */
	if (count >= 200)
		kdb_printf("bt truncated, count limit reached\n");

	return 0;
}

int
kdba_bt_address(kdb_machreg_t addr, int argcount)
{
	kdb_printf("Backtrace from a stack address is not supported on ia64\n");
	return KDB_NOTIMP;
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
	return kdba_bt_stack(argcount, p);
}
