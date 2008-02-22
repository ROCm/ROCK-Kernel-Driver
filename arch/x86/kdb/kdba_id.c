/*
 * Kernel Debugger Architecture Dependent Instruction Disassembly
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <stdarg.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>

/*
 * kdba_dis_getsym
 *
 *	Get a symbol for the disassembler.
 *
 * Parameters:
 *	addr	Address for which to get symbol
 *	dip	Pointer to disassemble_info
 * Returns:
 *	0
 * Locking:
 * Remarks:
 *	Not used for kdb.
 */

/* ARGSUSED */
static int
kdba_dis_getsym(bfd_vma addr, disassemble_info *dip)
{

	return 0;
}

/*
 * kdba_printaddress
 *
 *	Print (symbolically) an address.
 *
 * Parameters:
 *	addr	Address for which to get symbol
 *	dip	Pointer to disassemble_info
 *	flag	True if a ":<tab>" sequence should follow the address
 * Returns:
 *	0
 * Locking:
 * Remarks:
 *
 */

/* ARGSUSED */
static void
kdba_printaddress(kdb_machreg_t addr, disassemble_info *dip, int flag)
{
	kdb_symtab_t symtab;
	int spaces = 5;
	unsigned int offset;

	/*
	 * Print a symbol name or address as necessary.
	 */
	kdbnearsym(addr, &symtab);
	if (symtab.sym_name) {
		/* Do not use kdb_symbol_print here, it always does
		 * kdb_printf but we want dip->fprintf_func.
		 */
		dip->fprintf_func(dip->stream,
			"0x%0*lx %s",
			(int)(2*sizeof(addr)), addr, symtab.sym_name);
		if ((offset = addr - symtab.sym_start) == 0) {
			spaces += 4;
		}
		else {
			unsigned int o = offset;
			while (o >>= 4)
				--spaces;
			dip->fprintf_func(dip->stream, "+0x%x", offset);
		}

	} else {
		dip->fprintf_func(dip->stream, "0x%lx", addr);
	}

	if (flag) {
		if (spaces < 1) {
			spaces = 1;
		}
		dip->fprintf_func(dip->stream, ":%*s", spaces, " ");
	}
}

/*
 * kdba_dis_printaddr
 *
 *	Print (symbolically) an address.  Called by GNU disassembly
 *	code via disassemble_info structure.
 *
 * Parameters:
 *	addr	Address for which to get symbol
 *	dip	Pointer to disassemble_info
 * Returns:
 *	0
 * Locking:
 * Remarks:
 *	This function will never append ":<tab>" to the printed
 *	symbolic address.
 */

static void
kdba_dis_printaddr(bfd_vma addr, disassemble_info *dip)
{
	kdba_printaddress(addr, dip, 0);
}

/*
 * kdba_dis_getmem
 *
 *	Fetch 'length' bytes from 'addr' into 'buf'.
 *
 * Parameters:
 *	addr	Address for which to get symbol
 *	buf	Address of buffer to fill with bytes from 'addr'
 *	length	Number of bytes to fetch
 *	dip	Pointer to disassemble_info
 * Returns:
 *	0 if data is available, otherwise error.
 * Locking:
 * Remarks:
 *
 */

/* ARGSUSED */
static int
kdba_dis_getmem(bfd_vma addr, bfd_byte *buf, unsigned int length, disassemble_info *dip)
{
	return kdb_getarea_size(buf, addr, length);
}

/*
 * kdba_id_parsemode
 *
 * 	Parse IDMODE environment variable string and
 *	set appropriate value into "disassemble_info" structure.
 *
 * Parameters:
 *	mode	Mode string
 *	dip	Disassemble_info structure pointer
 * Returns:
 * Locking:
 * Remarks:
 *	We handle the values 'x86' and '8086' to enable either
 *	32-bit instruction set or 16-bit legacy instruction set.
 */

int
kdba_id_parsemode(const char *mode, disassemble_info *dip)
{
	if (mode) {
		if (strcmp(mode, "x86_64") == 0) {
			dip->mach = bfd_mach_x86_64;
		} else if (strcmp(mode, "x86") == 0) {
			dip->mach = bfd_mach_i386_i386;
		} else if (strcmp(mode, "8086") == 0) {
			dip->mach = bfd_mach_i386_i8086;
		} else {
			return KDB_BADMODE;
		}
	}

	return 0;
}

/*
 * kdba_check_pc
 *
 * 	Check that the pc is satisfactory.
 *
 * Parameters:
 *	pc	Program Counter Value.
 * Returns:
 *	None
 * Locking:
 *	None.
 * Remarks:
 *	Can change pc.
 */

void
kdba_check_pc(kdb_machreg_t *pc)
{
	/* No action */
}

/*
 * kdba_id_printinsn
 *
 * 	Format and print a single instruction at 'pc'. Return the
 *	length of the instruction.
 *
 * Parameters:
 *	pc	Program Counter Value.
 *	dip	Disassemble_info structure pointer
 * Returns:
 *	Length of instruction, -1 for error.
 * Locking:
 *	None.
 * Remarks:
 *	Depends on 'IDMODE' environment variable.
 */

int
kdba_id_printinsn(kdb_machreg_t pc, disassemble_info *dip)
{
	kdba_printaddress(pc, dip, 1);
	return print_insn_i386_att(pc, dip);
}

/*
 * kdba_id_init
 *
 * 	Initialize the architecture dependent elements of
 *	the disassembly information structure
 *	for the GNU disassembler.
 *
 * Parameters:
 *	None.
 * Outputs:
 *	None.
 * Returns:
 *	None.
 * Locking:
 *	None.
 * Remarks:
 */

void
kdba_id_init(disassemble_info *dip)
{
	dip->read_memory_func       = kdba_dis_getmem;
	dip->print_address_func     = kdba_dis_printaddr;
	dip->symbol_at_address_func = kdba_dis_getsym;

	dip->flavour                = bfd_target_elf_flavour;
	dip->arch		    = bfd_arch_i386;
#ifdef CONFIG_X86_64
	dip->mach		    = bfd_mach_x86_64;
#endif
#ifdef CONFIG_X86_32
	dip->mach		    = bfd_mach_i386_i386;
#endif
	dip->endian	    	    = BFD_ENDIAN_LITTLE;

	dip->display_endian         = BFD_ENDIAN_LITTLE;
}
