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

/* x86_64 has multiple alternate stacks, with different sizes and different
 * offsets to get the link from one stack to the next.  Some of the stacks are
 * referenced via cpu_pda, some via per_cpu orig_ist.  Debug events can even
 * have multiple nested stacks within the single physical stack, each nested
 * stack has its own link and some of those links are wrong.
 *
 * Consistent it's not!
 *
 * Do not assume that these stacks are aligned on their size.
 */
#define INTERRUPT_STACK (N_EXCEPTION_STACKS + 1)
void
kdba_get_stack_info_alternate(kdb_machreg_t addr, int cpu,
			      struct kdb_activation_record *ar)
{
	static struct {
		const char *id;
		unsigned int total_size;
		unsigned int nested_size;
		unsigned int next;
	} *sdp, stack_data[] = {
		[STACKFAULT_STACK - 1] =  { "stackfault",    EXCEPTION_STKSZ, EXCEPTION_STKSZ, EXCEPTION_STKSZ - 2*sizeof(void *) },
		[DOUBLEFAULT_STACK - 1] = { "doublefault",   EXCEPTION_STKSZ, EXCEPTION_STKSZ, EXCEPTION_STKSZ - 2*sizeof(void *) },
		[NMI_STACK - 1] =         { "nmi",           EXCEPTION_STKSZ, EXCEPTION_STKSZ, EXCEPTION_STKSZ - 2*sizeof(void *) },
		[DEBUG_STACK - 1] =       { "debug",         DEBUG_STKSZ,     EXCEPTION_STKSZ, EXCEPTION_STKSZ - 2*sizeof(void *) },
		[MCE_STACK - 1] =         { "machine check", EXCEPTION_STKSZ, EXCEPTION_STKSZ, EXCEPTION_STKSZ - 2*sizeof(void *) },
		[INTERRUPT_STACK - 1] =   { "interrupt",     IRQSTACKSIZE,    IRQSTACKSIZE,    IRQSTACKSIZE    -   sizeof(void *) },
	};
	unsigned long total_start = 0, total_size, total_end;
	int sd, found = 0;

	for (sd = 0, sdp = stack_data;
	     sd < ARRAY_SIZE(stack_data);
	     ++sd, ++sdp) {
		total_size = sdp->total_size;
		if (!total_size)
			continue;	/* in case stack_data[] has any holes */
		if (cpu < 0) {
			/* Arbitrary address which can be on any cpu, see if it
			 * falls within any of the alternate stacks
			 */
			int c;
			for_each_online_cpu(c) {
				if (sd == INTERRUPT_STACK - 1)
					total_end = (unsigned long)cpu_pda(c)->irqstackptr;
				else
					total_end = per_cpu(orig_ist, c).ist[sd];
				total_start = total_end - total_size;
				if (addr >= total_start && addr < total_end) {
					found = 1;
					cpu = c;
					break;
				}
			}
			if (!found)
				continue;
		}
		/* Only check the supplied or found cpu */
		if (sd == INTERRUPT_STACK - 1)
			total_end = (unsigned long)cpu_pda(cpu)->irqstackptr;
		else
			total_end = per_cpu(orig_ist, cpu).ist[sd];
		total_start = total_end - total_size;
		if (addr >= total_start && addr < total_end) {
			found = 1;
			break;
		}
	}
	if (!found)
		return;
	/* find which nested stack the address is in */
	while (addr > total_start + sdp->nested_size)
		total_start += sdp->nested_size;
	ar->stack.physical_start = total_start;
	ar->stack.physical_end = total_start + sdp->nested_size;
	ar->stack.logical_start = total_start;
	ar->stack.logical_end = total_start + sdp->next;
	ar->stack.next = *(unsigned long *)ar->stack.logical_end;
	ar->stack.id = sdp->id;

	/* Nasty: common_interrupt builds a partial pt_regs, with r15 through
	 * rbx not being filled in.  It passes struct pt_regs* to do_IRQ (in
	 * rdi) but the stack pointer is not adjusted to account for r15
	 * through rbx.  This has two effects :-
	 *
	 * (1) struct pt_regs on an external interrupt actually overlaps with
	 *     the local stack area used by do_IRQ.  Not only are r15-rbx
	 *     undefined, the area that claims to hold their values can even
	 *     change as the irq is processed..
	 *
	 * (2) The back stack pointer saved for the new frame is not pointing
	 *     at pt_regs, it is pointing at rbx within the pt_regs passed to
	 *     do_IRQ.
	 *
	 * There is nothing that I can do about (1) but I have to fix (2)
	 * because kdb backtrace looks for pt_regs.
	 */

	if (sd == INTERRUPT_STACK - 1)
		ar->stack.next -= offsetof(struct pt_regs, rbx);
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
kdba_get_stack_info(kdb_machreg_t rsp, int cpu,
		    struct kdb_activation_record *ar,
		    const struct task_struct *t)
{
	struct thread_info *tinfo;
	struct task_struct *g, *p;
	memset(&ar->stack, 0, sizeof(ar->stack));
	if (KDB_DEBUG(ARA))
		kdb_printf("%s: rsp=0x%lx cpu=%d task=%p\n",
			   __FUNCTION__, rsp, cpu, t);
	if (t == NULL || cpu >= 0) {
		kdba_get_stack_info_alternate(rsp, cpu, ar);
		if (ar->stack.logical_start)
			goto out;
	}
	rsp &= -THREAD_SIZE;
	tinfo = (struct thread_info *)rsp;
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
		ar->stack.physical_start = rsp;
		ar->stack.physical_end = rsp + THREAD_SIZE;
		ar->stack.logical_start = rsp + sizeof(struct thread_info);
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
 *	rip	Current program counter, or return address.
 *	rsp	Stack pointer rsp when at rip.
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
bt_print_one(kdb_machreg_t rip, kdb_machreg_t rsp,
	     const struct kdb_activation_record *ar,
	     const kdb_symtab_t *symtab, int argcount)
{
	int btsymarg = 0;
	int nosect = 0;
	kdb_machreg_t word;

	kdbgetintenv("BTSYMARG", &btsymarg);
	kdbgetintenv("NOSECT", &nosect);

	kdb_printf(kdb_machreg_fmt0, rsp);
	kdb_symbol_print(rip, symtab, KDB_SP_SPACEB|KDB_SP_VALUE);
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
 * moderately tricky.  kdba_save_running() saved the rsp in krp->arch.rsp, but
 * that rsp is not 100% accurate, it can be offset by a frame pointer or by the
 * size of local variables in kdba_main_loop() and kdb_save_running().
 *
 * The calling sequence is kdb() -> kdba_main_loop() -> kdb_save_running() ->
 * kdba_save_running().  Walk up the stack until we find a return address
 * inside the main kdb() function and start the backtrace from there.
 */

static int
kdba_bt_stack_running(const struct task_struct *p,
		      const struct kdb_activation_record *ar,
		      kdb_machreg_t *rip, kdb_machreg_t *rsp,
		      kdb_machreg_t *rbp)
{
	kdb_machreg_t addr, sp;
	kdb_symtab_t symtab;
	struct kdb_running_process *krp = kdb_running_process + task_cpu(p);
	int found = 0;

	if (kdbgetsymval("kdb", &symtab) == 0)
		return 0;
	if (kdbnearsym(symtab.sym_start, &symtab) == 0)
		return 0;
	sp = krp->arch.rsp;
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
	*rbp = *rsp = sp;
	*rip = addr;
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
	kdb_machreg_t rip, rsp, rbp, cs;
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
		rip = 0;
		rbp = 0;
		rsp = addr;
		cs = __KERNEL_CS;	/* have to assume kernel space */
		suppress = 0;
		kdba_get_stack_info(rsp, -1, &ar, NULL);
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
			kdba_getregcontents("cs", regs, &cs);
			if ((cs & 0xffff) != __KERNEL_CS) {
				kdb_printf("Stack is not in kernel space, backtrace not available\n");
				return 0;
			}
			kdba_getregcontents("rip", regs, &rip);
			kdba_getregcontents("rbp", regs, &rbp);
			rsp = krp->arch.rsp;
			kdba_get_stack_info(rsp, kdb_process_cpu(p), &ar, p);
			if (kdba_bt_stack_running(p, &ar, &rip, &rsp, &rbp) == 0) {
				kdb_printf("%s: cannot adjust rsp=0x%lx for a running task\n",
					   __FUNCTION__, rsp);
			}
		} else {
			/* Not on cpu, assume blocked.  Blocked tasks do
			 * not have pt_regs.  p->thread.rsp is set, rsp
			 * points to the rbp value, assume kernel space.
			 *
			 * The rip is no longer in the thread struct.  We know
			 * that the stack value was saved in schedule near the
			 * label thread_return.  Setting rip to thread_return-1
			 * lets the stack trace find that we are in schedule
			 * and correctly decode its prologue.  We extract the
			 * saved rbp and adjust the stack to undo the effects
			 * of the inline assembly code which switches the
			 * stack.
			 */
			extern void thread_return(void);
			rip = (kdb_machreg_t)&thread_return-1;
			rsp = p->thread.rsp;
			rbp = *(unsigned long *)rsp;
			rsp += 16;
			cs = __KERNEL_CS;
			suppress = 0;
			kdba_get_stack_info(rsp, -1, &ar, p);
		}
	}
	if (!ar.stack.physical_start) {
		kdb_printf("rsp=0x%lx is not in a valid kernel stack, backtrace not available\n",
			   rsp);
		return 0;
	}

	kdb_printf("rsp                rip                Function (args)\n");
	if (ar.stack.next && !suppress)
		kdb_printf(" ======================= <%s>\n",
			   ar.stack.id);

	/* Run through all the stacks */
	while (ar.stack.physical_start) {
		if (!first_time)
			rip = *(kdb_machreg_t *)rsp;
		first_time = 0;
		if (!suppress && __kernel_text_address(rip)) {
			kdbnearsym(rip, &symtab);
			bt_print_one(rip, rsp, &ar, &symtab, argcount);
			++count;
		}
		if ((struct pt_regs *)rsp == regs) {
			if (ar.stack.next && suppress)
				kdb_printf(" ======================= <%s>\n",
					   ar.stack.id);
			++count;
			suppress = 0;
		}
		rsp += sizeof(rip);
		if (count > 200)
			break;
		if (rsp < ar.stack.logical_end)
			continue;
		if (!ar.stack.next)
			break;
		rsp = ar.stack.next;
		if (KDB_DEBUG(ARA))
			kdb_printf("new rsp=0x%lx\n", rsp);
		kdba_get_stack_info(rsp, -1, &ar, NULL);
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
 *	mds %rsp comes in handy when examining the stack to do a manual
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
