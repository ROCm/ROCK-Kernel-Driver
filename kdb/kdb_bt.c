/*
 * Kernel Debugger Architecture Independent Stack Traceback
 *
 * Copyright (C) 1999-2002 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information:  Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
 */

#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <asm/system.h>


/*
 * kdb_bt
 *
 *	This function implements the 'bt' command.  Print a stack
 *	traceback.
 *
 *	bt [<address-expression>]	(addr-exp is for alternate stacks)
 *	btp <pid>			Kernel stack for <pid>
 *	bta [DRSTZU]			All processes, optionally filtered by state
 *
 * 	address expression refers to a return address on the stack.  It
 *	is expected to be preceeded by a frame pointer.
 *
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 *	envp	environment vector
 *	regs	registers at time kdb was entered.
 * Outputs:
 *	None.
 * Returns:
 *	zero for success, a kdb diagnostic if error
 * Locking:
 *	none.
 * Remarks:
 *	Backtrack works best when the code uses frame pointers.  But
 *	even without frame pointers we should get a reasonable trace.
 *
 *	mds comes in handy when examining the stack to do a manual
 *	traceback.
 */

int
kdb_bt(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	int	diag;
	int	argcount = 5;
	int	btaprompt = 1;
	char	buffer[80];
	int 	nextarg;
	unsigned long addr;
	long	offset;
	unsigned long	mask;

	kdbgetintenv("BTARGS", &argcount);	/* Arguments to print */
	kdbgetintenv("BTAPROMPT", &btaprompt);	/* Prompt after each proc in bta */

	if (strcmp(argv[0], "bta") == 0) {
		struct task_struct *p;
		mask = kdb_task_state_string(argc, argv, envp);

		for_each_process(p) {
			if (!kdb_task_state(p, mask))
				continue;
			kdb_printf("Stack traceback for pid %d\n", p->pid);
			kdb_ps1(p);
			diag = kdba_bt_process(p, argcount);

			if (btaprompt) {
				kdb_getstr(buffer, sizeof(buffer),
					   "Enter <q> to end, <cr> to continue:");

				if (buffer[0] == 'q') {
					return 0;
				}
			}
		}
	} else if (strcmp(argv[0], "btp") == 0) {
		struct task_struct *p;
		unsigned long	   pid;
		
		if (argc < 1)
			return KDB_ARGCOUNT;

		diag = kdbgetularg((char *)argv[1], &pid);
		if (diag)
			return diag;

		for_each_process(p) {
			if (p->pid == (pid_t)pid) {
				kdb_ps1(p);
				return kdba_bt_process(p, argcount);
			}
		}

		kdb_printf("No process with pid == %ld found\n", pid);
		return 0;
	} else {
		if (argc) {
			nextarg = 1;
			diag = kdbgetaddrarg(argc, argv, &nextarg, &addr,
					     &offset, NULL, regs);
			if (diag)
				return diag;

			kdb_ps1(current);
			return kdba_bt_stack(regs, &addr, argcount, current);
		} else {
			kdb_ps1(current);
			return kdba_bt_stack(regs, NULL, argcount, current);
		}
	}

	/* NOTREACHED */
	return 0;
}
