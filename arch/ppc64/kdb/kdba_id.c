/*
 * Minimalist Kernel Debugger - Architecture Dependent Instruction Disassembly
 *
 * Copyright (C) 1999 Silicon Graphics, Inc.
 *
 * See the file LIA-COPYRIGHT for additional information.
 *
 * Written March 1999 by Scott Lurndal at Silicon Graphics, Inc.
 *
 * Modifications from:
 *      Richard Bass                    1999/07/20
 *              Many bug fixes and enhancements.
 *      Scott Foehner
 *              Port to ia64
 *      Srinivasa Thirumalachar
 *              RSE support for ia64
 *	Masahiro Adegawa                1999/12/01
 *		'sr' command, active flag in 'ps'
 *	Scott Lurndal			1999/12/12
 *		Significantly restructure for linux2.3
 *	Keith Owens			2000/05/23
 *		KDB v1.2
 *
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
 *	number of chars printed
 * Locking:
 * Remarks:
 *
 */

/* ARGSUSED */
void
kdba_printaddress(kdb_machreg_t addr, disassemble_info *dip, int flag)
{
	kdb_symtab_t symtab;

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
			2*sizeof(addr), addr, symtab.sym_name);
		/* Add offset if needed.  Pad output with blanks to get
		 * consistent size symbols for disassembly listings.
		 */
		if (addr == symtab.sym_start) {
			if (!flag)
				dip->fprintf_func(dip->stream, "         ");
		} else {
			int len, i;
			char buf[20];
			sprintf(buf, "%lx", addr - symtab.sym_start);
			dip->fprintf_func(dip->stream, "+0x%s", buf);
			if (!flag) {
				len = strlen(buf);
				for (i = len; i < 6; i++)
					dip->fprintf_func(dip->stream, " ");
			}
		}

	} else {
		dip->fprintf_func(dip->stream, "0x%0*lx", 2*sizeof(addr), addr);
	}

	if (flag)
		dip->fprintf_func(dip->stream, ":   ");
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
 *	number of chars printed.
 * Locking:
 * Remarks:
 *	This function will never append ":<tab>" to the printed
 *	symbolic address.
 */

static void
kdba_dis_printaddr(bfd_vma addr, disassemble_info *dip)
{
	return kdba_printaddress(addr, dip, 0);
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
 *	0
 * Locking:
 * Remarks:
 *
 */
extern int kdba_getword(unsigned long addr, size_t width);


/* ARGSUSED */
static int
kdba_dis_getmem(bfd_vma addr, bfd_byte *buf, unsigned int length, disassemble_info *dip)
{
	bfd_byte	*bp = buf;
	int		i;

	/*
	 * Fill the provided buffer with bytes from
	 * memory, starting at address 'addr' for 'length bytes.
	 *
	 */

	for(i=0; i<length; i++ ){
		*bp++ = (bfd_byte)kdba_getword(addr++, sizeof(bfd_byte));
	}

	return 0;
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
	kdba_dis_printaddr(pc, dip);
	return print_insn_big_powerpc(pc, dip);
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

void __init
kdba_id_init(disassemble_info *dip)
{
	dip->read_memory_func       = kdba_dis_getmem;
	dip->print_address_func     = kdba_dis_printaddr;
	dip->symbol_at_address_func = kdba_dis_getsym;

	dip->flavour                = bfd_target_elf_flavour;
	dip->arch		    = bfd_arch_powerpc;
	dip->mach		    = bfd_mach_ppc_750;
	dip->endian	    	    = BFD_ENDIAN_BIG;

	dip->display_endian         = BFD_ENDIAN_BIG;
}
