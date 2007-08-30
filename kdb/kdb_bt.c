/*
 * Kernel Debugger Architecture Independent Stack Traceback
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <linux/nmi.h>
#include <asm/system.h>


/*
 * kdb_bt
 *
 *	This function implements the 'bt' command.  Print a stack
 *	traceback.
 *
 *	bt [<address-expression>]	(addr-exp is for alternate stacks)
 *	btp <pid>			Kernel stack for <pid>
 *	btt <address-expression>	Kernel stack for task structure at <address-expression>
 *	bta [DRSTCZEUIMA]		All useful processes, optionally filtered by state
 *	btc [<cpu>]			The current process on one cpu, default is all cpus
 *
 *	bt <address-expression> refers to a address on the stack, that location
 *	is assumed to contain a return address.
 *
 *	btt <address-expression> refers to the address of a struct task.
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	Backtrack works best when the code uses frame pointers.  But even
 *	without frame pointers we should get a reasonable trace.
 *
 *	mds comes in handy when examining the stack to do a manual traceback or
 *	to get a starting point for bt <address-expression>.
 */

static int
kdb_bt1(const struct task_struct *p, unsigned long mask, int argcount, int btaprompt)
{
	int diag;
	char buffer[2];
	if (kdb_getarea(buffer[0], (unsigned long)p) ||
	    kdb_getarea(buffer[0], (unsigned long)(p+1)-1))
		return KDB_BADADDR;
	if (!kdb_task_state(p, mask))
		return 0;
	kdb_printf("Stack traceback for pid %d\n", p->pid);
	kdb_ps1(p);
	diag = kdba_bt_process(p, argcount);
	if (btaprompt) {
		kdb_getstr(buffer, sizeof(buffer), "Enter <q> to end, <cr> to continue:");
		if (buffer[0] == 'q') {
			kdb_printf("\n");
			return 1;
		}
	}
	touch_nmi_watchdog();
	return 0;
}

int
kdb_bt(int argc, const char **argv)
{
	int diag;
	int argcount = 5;
	int btaprompt = 1;
	int nextarg;
	unsigned long addr;
	long offset;

	kdbgetintenv("BTARGS", &argcount);	/* Arguments to print */
	kdbgetintenv("BTAPROMPT", &btaprompt);	/* Prompt after each proc in bta */

	if (strcmp(argv[0], "bta") == 0) {
		struct task_struct *g, *p;
		unsigned long cpu;
		unsigned long mask = kdb_task_state_string(argc ? argv[1] : NULL);
		if (argc == 0)
			kdb_ps_suppressed();
		/* Run the active tasks first */
		for (cpu = 0; cpu < NR_CPUS; ++cpu) {
			if (!cpu_online(cpu))
				continue;
			p = kdb_curr_task(cpu);
			if (kdb_bt1(p, mask, argcount, btaprompt))
				return 0;
		}
		/* Now the inactive tasks */
		kdb_do_each_thread(g, p) {
			if (task_curr(p))
				continue;
			if (kdb_bt1(p, mask, argcount, btaprompt))
				return 0;
		} kdb_while_each_thread(g, p);
	} else if (strcmp(argv[0], "btp") == 0) {
		struct task_struct *p;
		unsigned long pid;
		if (argc != 1)
			return KDB_ARGCOUNT;
		if ((diag = kdbgetularg((char *)argv[1], &pid)))
			return diag;
		if ((p = find_task_by_pid(pid))) {
			kdba_set_current_task(p);
			return kdb_bt1(p, ~0UL, argcount, 0);
		}
		kdb_printf("No process with pid == %ld found\n", pid);
		return 0;
	} else if (strcmp(argv[0], "btt") == 0) {
		if (argc != 1)
			return KDB_ARGCOUNT;
		if ((diag = kdbgetularg((char *)argv[1], &addr)))
			return diag;
		kdba_set_current_task((struct task_struct *)addr);
		return kdb_bt1((struct task_struct *)addr, ~0UL, argcount, 0);
	} else if (strcmp(argv[0], "btc") == 0) {
		unsigned long cpu = ~0;
		struct kdb_running_process *krp;
		const struct task_struct *save_current_task = kdb_current_task;
		char buf[80];
		if (argc > 1)
			return KDB_ARGCOUNT;
		if (argc == 1 && (diag = kdbgetularg((char *)argv[1], &cpu)))
			return diag;
		/* Recursive use of kdb_parse, do not use argv after this point */
		argv = NULL;
		if (cpu != ~0) {
			krp = kdb_running_process + cpu;
			if (cpu >= NR_CPUS || !krp->seqno || !cpu_online(cpu)) {
				kdb_printf("no process for cpu %ld\n", cpu);
				return 0;
			}
			sprintf(buf, "btt 0x%p\n", krp->p);
			kdb_parse(buf);
			return 0;
		}
		kdb_printf("btc: cpu status: ");
		kdb_parse("cpu\n");
		for (cpu = 0, krp = kdb_running_process; cpu < NR_CPUS; ++cpu, ++krp) {
			if (!cpu_online(cpu) || !krp->seqno)
				continue;
			sprintf(buf, "btt 0x%p\n", krp->p);
			kdb_parse(buf);
			touch_nmi_watchdog();
		}
		kdba_set_current_task(save_current_task);
		return 0;
	} else {
		if (argc) {
			nextarg = 1;
			diag = kdbgetaddrarg(argc, argv, &nextarg, &addr,
					     &offset, NULL);
			if (diag)
				return diag;
			return kdba_bt_address(addr, argcount);
		} else {
			return kdb_bt1(kdb_current_task, ~0UL, argcount, 0);
		}
	}

	/* NOTREACHED */
	return 0;
}
