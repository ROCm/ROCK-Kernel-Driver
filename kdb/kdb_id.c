/*
 * Kernel Debugger Architecture Independent Instruction Disassembly
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

#include <stdarg.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>

disassemble_info	kdb_di;

/*
 * kdb_id
 *
 * 	Handle the id (instruction display) command.
 *
 *	id  [<addr>]
 *
 * Parameters:
 *	argc	Count of arguments in argv
 *	argv	Space delimited command line arguments
 *	envp	Environment value
 *	regs	Exception frame at entry to kernel debugger
 * Outputs:
 *	None.
 * Returns:
 *	Zero for success, a kdb diagnostic if failure.
 * Locking:
 *	None.
 * Remarks:
 */

int
kdb_id(int argc, const char **argv, const char **envp, struct pt_regs* regs)
{
	kdb_machreg_t		pc;
	int			icount;
	int			diag;
	int			i;
	char *			mode;
	int			nextarg;
	long			offset = 0;
	static kdb_machreg_t 	lastpc;
	struct disassemble_info *dip = &kdb_di;
	char			lastbuf[50];
	unsigned long		word;

	if (argc != 1)  {
		if (lastpc == 0) {
			return KDB_ARGCOUNT;
		} else {
			sprintf(lastbuf, "0x%lx", lastpc);
			argv[1] = lastbuf;
			argc = 1;
		}
	}


	/*
	 * Fetch PC.  First, check to see if it is a symbol, if not,
	 * try address.
	 */
	nextarg = 1;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &pc, &offset, NULL, regs);
	if (diag)
		return diag;
	kdba_check_pc(&pc);
	if (kdb_getarea(word, pc))
		return(0);

	/*
	 * Number of lines to display
	 */
	diag = kdbgetintenv("IDCOUNT", &icount);
	if (diag)
		return diag;

	dip->fprintf_dummy = kdb_dis_fprintf;

	mode = kdbgetenv("IDMODE");
	diag = kdba_id_parsemode(mode, dip);
	if (diag) {
		return diag;
	}

	for(i=0; i<icount; i++) {
		pc += kdba_id_printinsn(pc, &kdb_di);
		kdb_printf("\n");
	}

	lastpc = pc;

	return 0;
}

/*
 * kdb_id1
 *
 * 	Disassemble a single instruction at 'pc'.
 *
 * Parameters:
 *	pc	Address of instruction to disassemble
 * Outputs:
 *	None.
 * Returns:
 *	Zero for success, a kdb diagnostic if failure.
 * Locking:
 *	None.
 * Remarks:
 */

void
kdb_id1(unsigned long pc)
{
	char   *mode;
	int	diag;

	/*
	 * Allow the user to specify that this instruction
	 * should be treated differently.
	 */

	kdb_di.fprintf_dummy = kdb_dis_fprintf_dummy;

	mode = kdbgetenv("IDMODE");
	diag = kdba_id_parsemode(mode, &kdb_di);
	if (diag) {
		kdb_printf("kdb_id: bad value in 'IDMODE' environment variable ignored\n");
	}

	(void) kdba_id_printinsn(pc, &kdb_di);
	kdb_printf("\n");
}

/*
 * kdb_dis_fprintf
 *
 *	Format and print a string.
 *
 * Parameters:
 *	file	Unused paramter.
 *	fmt	Format string
 *	...	Optional additional parameters.
 * Returns:
 *	0
 * Locking:
 * Remarks:
 * 	Result of format conversion cannot exceed 255 bytes.
 */

int
kdb_dis_fprintf(PTR file, const char *fmt, ...)
{
	char buffer[256];
	va_list ap;

	va_start(ap, fmt);
	vsprintf(buffer, fmt, ap);
	va_end(ap);

	kdb_printf("%s", buffer);

	return 0;
}

/*
 * kdb_dis_fprintf_dummy
 *
 *	A dummy printf function for the disassembler, it does nothing.
 *	This lets code call the disassembler to step through
 *	instructions without actually printing anything.
 * Inputs:
 *	Always ignored.
 * Outputs:
 *	None.
 * Returns:
 *	Always 0.
 * Locking:
 *	none.
 * Remarks:
 *	None.
 */

int
kdb_dis_fprintf_dummy(PTR file, const char *fmt, ...)
{
	return(0);
}

/*
 * kdb_disinit
 *
 * 	Initialize the disassembly information structure
 *	for the GNU disassembler.
 *
 * Parameters:
 *	None.
 * Outputs:
 *	None.
 * Returns:
 *	Zero for success, a kdb diagnostic if failure.
 * Locking:
 *	None.
 * Remarks:
 */

void __init
kdb_id_init(void)
{
	kdb_di.stream	    = NULL;
	kdb_di.application_data = NULL;
	kdb_di.symbols	    = NULL;
	kdb_di.num_symbols  = 0;
	kdb_di.flags	    = 0;
	kdb_di.private_data	    = NULL;
	kdb_di.buffer	    = NULL;
	kdb_di.buffer_vma       = 0;
	kdb_di.buffer_length    = 0;
	kdb_di.bytes_per_line   = 0;
	kdb_di.bytes_per_chunk  = 0;
	kdb_di.insn_info_valid  = 0;
	kdb_di.branch_delay_insns = 0;
	kdb_di.data_size	    = 0;
	kdb_di.insn_type	    = 0;
	kdb_di.target           = 0;
	kdb_di.target2          = 0;
	kdb_di.fprintf_func	= kdb_dis_fprintf;

	kdba_id_init(&kdb_di);
}
