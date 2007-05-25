/*
 * Kernel Debugger Architecture Dependent Stack Traceback
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2006 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kallsyms.h>
#include <linux/irq.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <asm/system.h>

/* On a 4K stack kernel, hardirq_ctx and softirq_ctx are [NR_CPUS] arrays.  The
 * first element of each per-cpu stack is a struct thread_info.
 */
void
kdba_get_stack_info_alternate(kdb_machreg_t addr, int cpu,
			      struct kdb_activation_record *ar)
{
#ifdef	CONFIG_4KSTACKS
	struct thread_info *tinfo;
	static int first_time = 1;
	static struct thread_info **kdba_hardirq_ctx, **kdba_softirq_ctx;
	if (first_time) {
		kdb_symtab_t symtab;
		kdbgetsymval("hardirq_ctx", &symtab);
		kdba_hardirq_ctx = (struct thread_info **)symtab.sym_start;
		kdbgetsymval("softirq_ctx", &symtab);
		kdba_softirq_ctx = (struct thread_info **)symtab.sym_start;
		first_time = 0;
	}
	tinfo = (struct thread_info *)(addr & -THREAD_SIZE);
	if (cpu < 0) {
		/* Arbitrary address, see if it falls within any of the irq
		 * stacks
		 */
		int found = 0;
		for_each_online_cpu(cpu) {
			if (tinfo == kdba_hardirq_ctx[cpu] ||
			    tinfo == kdba_softirq_ctx[cpu]) {
				found = 1;
				break;
			}
		}
		if (!found)
			return;
	}
	if (tinfo == kdba_hardirq_ctx[cpu] ||
	    tinfo == kdba_softirq_ctx[cpu]) {
		ar->stack.physical_start = (kdb_machreg_t)tinfo;
		ar->stack.physical_end = ar->stack.physical_start + THREAD_SIZE;
		ar->stack.logical_start = ar->stack.physical_start +
					  sizeof(struct thread_info);
		ar->stack.logical_end = ar->stack.physical_end;
		ar->stack.next = tinfo->previous_esp;
		if (tinfo == kdba_hardirq_ctx[cpu])
			ar->stack.id = "hardirq_ctx";
		else
			ar->stack.id = "softirq_ctx";
	}
#endif	/* CONFIG_4KSTACKS */
}

/* Given an address which claims to be on a stack, an optional cpu number and
 * an optional task address, get information about the stack.
 *
 * t == NULL, cpu < 0 indicates an arbitrary stack address with no associated
 * struct task, the address can be in an alternate stack or any task's normal
 * stack.
 *
 * t != NULL, cpu >= 0 indicates a running task, the address can be in an
 * alternate stack or that task's normal stack.
 *
 * t != NULL, cpu < 0 indicates a blocked task, the address can only be in that
 * task's normal stack.
 *
 * t == NULL, cpu >= 0 is not a valid combination.
 */

static void
kdba_get_stack_info(kdb_machreg_t esp, int cpu,
		    struct kdb_activation_record *ar,
		    const struct task_struct *t)
{
	struct thread_info *tinfo;
	struct task_struct *g, *p;
	memset(&ar->stack, 0, sizeof(ar->stack));
	if (KDB_DEBUG(ARA))
		kdb_printf("%s: esp=0x%lx cpu=%d task=%p\n",
			   __FUNCTION__, esp, cpu, t);
	if (t == NULL || cpu >= 0) {
		kdba_get_stack_info_alternate(esp, cpu, ar);
		if (ar->stack.logical_start)
			goto out;
	}
	esp &= -THREAD_SIZE;
	tinfo = (struct thread_info *)esp;
	if (t == NULL) {
		/* Arbitrary stack address without an associated task, see if
		 * it falls within any normal process stack, including the idle
		 * tasks.
		 */
		kdb_do_each_thread(g, p) {
			if (tinfo == task_thread_info(p)) {
				t = p;
				goto found;
			}
		} kdb_while_each_thread(g, p);
		for_each_online_cpu(cpu) {
			p = idle_task(cpu);
			if (tinfo == task_thread_info(p)) {
				t = p;
				goto found;
			}
		}
	found:
		if (KDB_DEBUG(ARA))
			kdb_printf("%s: found task %p\n", __FUNCTION__, t);
	} else if (cpu >= 0) {
		/* running task */
		struct kdb_running_process *krp = kdb_running_process + cpu;
		if (krp->p != t || tinfo != task_thread_info(t))
			t = NULL;
		if (KDB_DEBUG(ARA))
			kdb_printf("%s: running task %p\n", __FUNCTION__, t);
	} else {
		/* blocked task */
		if (tinfo != task_thread_info(t))
			t = NULL;
		if (KDB_DEBUG(ARA))
			kdb_printf("%s: blocked task %p\n", __FUNCTION__, t);
	}
	if (t) {
		ar->stack.physical_start = esp;
		ar->stack.physical_end = esp + THREAD_SIZE;
		ar->stack.logical_start = esp + sizeof(struct thread_info);
		ar->stack.logical_end = ar->stack.physical_end;
		ar->stack.next = 0;
		ar->stack.id = "normal";
	}
out:
	if (ar->stack.physical_start && KDB_DEBUG(ARA)) {
		kdb_printf("%s: ar->stack\n", __FUNCTION__);
		kdb_printf("    physical_start=0x%lx\n", ar->stack.physical_start);
		kdb_printf("    physical_end=0x%lx\n", ar->stack.physical_end);
		kdb_printf("    logical_start=0x%lx\n", ar->stack.logical_start);
		kdb_printf("    logical_end=0x%lx\n", ar->stack.logical_end);
		kdb_printf("    next=0x%lx\n", ar->stack.next);
		kdb_printf("    id=%s\n", ar->stack.id);
	}
}

/*
 * bt_print_one
 *
 *	Print one back trace entry.
 *
 * Inputs:
 *	eip	Current program counter, or return address.
 *	esp	Stack pointer esp when at eip.
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
bt_print_one(kdb_machreg_t eip, kdb_machreg_t esp,
	     const struct kdb_activation_record *ar,
	     const kdb_symtab_t *symtab, int argcount)
{
	int btsymarg = 0;
	int nosect = 0;
	kdb_machreg_t word;

	kdbgetintenv("BTSYMARG", &btsymarg);
	kdbgetintenv("NOSECT", &nosect);

	kdb_printf(kdb_machreg_fmt0, esp);
	kdb_symbol_print(eip, symtab, KDB_SP_SPACEB|KDB_SP_VALUE);
	if (argcount && ar->args) {
		int i, argc = ar->args;
		kdb_printf(" (");
		if (argc > argcount)
			argc = argcount;
		for (i = 0; i < argc; i++) {
			kdb_machreg_t argp = ar->arg[i];
			if (i)
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
					   symtab->sec_start, symtab->sec_end);
			kdb_printf(" 0x%lx 0x%lx",
				   symtab->sym_start, symtab->sym_end);
		}
	}
	kdb_printf("\n");
	if (argcount && ar->args && btsymarg) {
		int i, argc = ar->args;
		kdb_symtab_t arg_symtab;
		for (i = 0; i < argc; i++) {
			kdb_machreg_t argp = ar->arg[i];
			kdb_getword(&word, argp, sizeof(word));
			if (kdbnearsym(word, &arg_symtab)) {
				kdb_printf("                               ");
				kdb_symbol_print(word, &arg_symtab,
						 KDB_SP_DEFAULT|KDB_SP_NEWLINE);
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
kdba_bt_stack_running(const struct task_struct *p,
		      const struct kdb_activation_record *ar,
		      kdb_machreg_t *eip, kdb_machreg_t *esp,
		      kdb_machreg_t *ebp)
{
	kdb_machreg_t addr, sp;
	kdb_symtab_t symtab;
	struct kdb_running_process *krp = kdb_running_process + task_cpu(p);
	int found = 0;

	if (kdbgetsymval("kdb", &symtab) == 0)
		return 0;
	if (kdbnearsym(symtab.sym_start, &symtab) == 0)
		return 0;
	sp = krp->arch.esp;
	if (sp < ar->stack.logical_start || sp >= ar->stack.logical_end)
		return 0;
	while (sp < ar->stack.logical_end) {
		addr = *(kdb_machreg_t *)sp;
		if (addr >= symtab.sym_start && addr < symtab.sym_end) {
			found = 1;
			break;
		}
		sp += sizeof(kdb_machreg_t);
	}
	if (!found)
		return 0;
	*ebp = *esp = sp;
	*eip = addr;
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
	struct kdb_activation_record ar;
	kdb_machreg_t eip, esp, ebp, cs;
	kdb_symtab_t symtab;
	int first_time = 1, count = 0, btsp = 0, suppress;
	struct pt_regs *regs = NULL;

	kdbgetintenv("BTSP", &btsp);
	suppress = !btsp;
	memset(&ar, 0, sizeof(ar));

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
		cs = __KERNEL_CS;	/* have to assume kernel space */
		suppress = 0;
		kdba_get_stack_info(esp, -1, &ar, NULL);
	} else {
		if (task_curr(p)) {
			struct kdb_running_process *krp =
			    kdb_running_process + task_cpu(p);

			if (krp->seqno && krp->p == p
			    && krp->seqno >= kdb_seqno - 1) {
				/* valid saved state, continue processing */
			} else {
				kdb_printf
				    ("Process did not save state, cannot backtrace\n");
				kdb_ps1(p);
				return 0;
			}
			regs = krp->regs;
			if (KDB_NULL_REGS(regs))
				return KDB_BADREG;
			kdba_getregcontents("xcs", regs, &cs);
			if ((cs & 0xffff) != __KERNEL_CS) {
				kdb_printf("Stack is not in kernel space, backtrace not available\n");
				return 0;
			}
			kdba_getregcontents("eip", regs, &eip);
			kdba_getregcontents("ebp", regs, &ebp);
			esp = krp->arch.esp;
			kdba_get_stack_info(esp, kdb_process_cpu(p), &ar, p);
			if (kdba_bt_stack_running(p, &ar, &eip, &esp, &ebp) == 0) {
				kdb_printf("%s: cannot adjust esp=0x%lx for a running task\n",
					   __FUNCTION__, esp);
			}
		} else {
			/* Not on cpu, assume blocked.  Blocked tasks do not
			 * have pt_regs.  p->thread.{esp,eip} are set, esp
			 * points to the ebp value, assume kernel space.
			 */
			eip = p->thread.eip;
			esp = p->thread.esp;
			ebp = *(unsigned long *)esp;
			cs = __KERNEL_CS;
			suppress = 0;
			kdba_get_stack_info(esp, -1, &ar, p);
		}
	}
	if (!ar.stack.physical_start) {
		kdb_printf("esp=0x%lx is not in a valid kernel stack, backtrace not available\n",
			   esp);
		return 0;
	}

	kdb_printf("esp        eip        Function (args)\n");
	if (ar.stack.next && !suppress)
		kdb_printf(" ======================= <%s>\n",
			   ar.stack.id);

	/* Run through all the stacks */
	while (ar.stack.physical_start) {
		if (!first_time)
			eip = *(kdb_machreg_t *)esp;
		first_time = 0;
		if (!suppress && __kernel_text_address(eip)) {
			kdbnearsym(eip, &symtab);
			bt_print_one(eip, esp, &ar, &symtab, argcount);
			++count;
		}
		if ((struct pt_regs *)esp == regs) {
			if (ar.stack.next && suppress)
				kdb_printf(" ======================= <%s>\n",
					   ar.stack.id);
			++count;
			suppress = 0;
		}
		esp += sizeof(eip);
		if (count > 200)
			break;
		if (esp < ar.stack.logical_end)
			continue;
		if (!ar.stack.next)
			break;
		esp = ar.stack.next;
		if (KDB_DEBUG(ARA))
			kdb_printf("new esp=0x%lx\n", esp);
		kdba_get_stack_info(esp, -1, &ar, NULL);
		if (!ar.stack.physical_start) {
			kdb_printf("+++ Cannot resolve next stack\n");
		} else if (!suppress) {
			kdb_printf(" ======================= <%s>\n",
				   ar.stack.id);
			++count;
		}
	}

	if (count > 200)
		kdb_printf("bt truncated, count limit reached\n");
	else if (suppress)
		kdb_printf
		    ("bt did not find pt_regs - no trace produced.  Suggest 'set BTSP 1'\n");

	return 0;
}

/*
 * kdba_bt_address
 *
 *	Do a backtrace starting at a specified stack address.  Use this if the
 *	heuristics get the stack decode wrong.
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

int kdba_bt_address(kdb_machreg_t addr, int argcount)
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

int kdba_bt_process(const struct task_struct *p, int argcount)
{
	return kdba_bt_stack(0, argcount, p);
}
