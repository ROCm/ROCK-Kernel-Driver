/* kallsyms headers
   Copyright 2000 Keith Owens <kaos@ocs.com.au>

   This file is part of the Linux modutils.  It is exported to kernel
   space so debuggers can access the kallsyms data.

   The kallsyms data contains all the non-stack symbols from a kernel
   or a module.  The kernel symbols are held between __start___kallsyms
   and __stop___kallsyms.  The symbols for a module are accessed via
   the struct module chain which is based at module_list.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2 of the License, or (at your
   option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ident "$Id: linux-2.4.9-kallsyms.patch,v 1.8 2002/02/11 18:34:53 arjanv Exp $"

#ifndef MODUTILS_KALLSYMS_H
#define MODUTILS_KALLSYMS_H 1

/* Have to (re)define these ElfW entries here because external kallsyms
 * code does not have access to modutils/include/obj.h.  This code is
 * included from user spaces tools (modutils) and kernel, they need
 * different includes.
 */

#ifndef ELFCLASS32
#ifdef __KERNEL__
#include <linux/elf.h>
#else	/* __KERNEL__ */
#include <elf.h>
#endif	/* __KERNEL__ */
#endif	/* ELFCLASS32 */

#ifndef ELFCLASSM
#define ELFCLASSM ELF_CLASS
#endif

#ifndef ElfW
# if ELFCLASSM == ELFCLASS32
#  define ElfW(x)  Elf32_ ## x
#  define ELFW(x)  ELF32_ ## x
# else
#  define ElfW(x)  Elf64_ ## x
#  define ELFW(x)  ELF64_ ## x
# endif
#endif

/* Format of data in the kallsyms section.
 * Most of the fields are small numbers but the total size and all
 * offsets can be large so use the 32/64 bit types for these fields.
 *
 * Do not use sizeof() on these structures, modutils may be using extra
 * fields.  Instead use the size fields in the header to access the
 * other bits of data.
 */  

struct kallsyms_header {
	int		size;		/* Size of this header */
	ElfW(Word)	total_size;	/* Total size of kallsyms data */
	int		sections;	/* Number of section entries */
	ElfW(Off)	section_off;	/* Offset to first section entry */
	int		section_size;	/* Size of one section entry */
	int		symbols;	/* Number of symbol entries */
	ElfW(Off)	symbol_off;	/* Offset to first symbol entry */
	int		symbol_size;	/* Size of one symbol entry */
	ElfW(Off)	string_off;	/* Offset to first string */
	ElfW(Addr)	start;		/* Start address of first section */
	ElfW(Addr)	end;		/* End address of last section */
};

struct kallsyms_section {
	ElfW(Addr)	start;		/* Start address of section */
	ElfW(Word)	size;		/* Size of this section */
	ElfW(Off)	name_off;	/* Offset to section name */
	ElfW(Word)	flags;		/* Flags from section */
};

struct kallsyms_symbol {
	ElfW(Off)	section_off;	/* Offset to section that owns this symbol */
	ElfW(Addr)	symbol_addr;	/* Address of symbol */
	ElfW(Off)	name_off;	/* Offset to symbol name */
};

#define KALLSYMS_SEC_NAME "__kallsyms"
#define KALLSYMS_IDX 2			/* obj_kallsyms creates kallsyms as section 2 */

#define kallsyms_next_sec(h,s) \
	((s) = (struct kallsyms_section *)((char *)(s) + (h)->section_size))
#define kallsyms_next_sym(h,s) \
	((s) = (struct kallsyms_symbol *)((char *)(s) + (h)->symbol_size))

#ifdef CONFIG_KALLSYMS

int kallsyms_symbol_to_address(
	const char       *name,			/* Name to lookup */
	unsigned long    *token,		/* Which module to start with */
	const char      **mod_name,		/* Set to module name or "kernel" */
	unsigned long    *mod_start,		/* Set to start address of module */
	unsigned long    *mod_end,		/* Set to end address of module */
	const char      **sec_name,		/* Set to section name */
	unsigned long    *sec_start,		/* Set to start address of section */
	unsigned long    *sec_end,		/* Set to end address of section */
	const char      **sym_name,		/* Set to full symbol name */
	unsigned long    *sym_start,		/* Set to start address of symbol */
	unsigned long    *sym_end		/* Set to end address of symbol */
	);

int kallsyms_address_to_symbol(
	unsigned long     address,		/* Address to lookup */
	const char      **mod_name,		/* Set to module name */
	unsigned long    *mod_start,		/* Set to start address of module */
	unsigned long    *mod_end,		/* Set to end address of module */
	const char      **sec_name,		/* Set to section name */
	unsigned long    *sec_start,		/* Set to start address of section */
	unsigned long    *sec_end,		/* Set to end address of section */
	const char      **sym_name,		/* Set to full symbol name */
	unsigned long    *sym_start,		/* Set to start address of symbol */
	unsigned long    *sym_end		/* Set to end address of symbol */
	);

int kallsyms_sections(void *token,
		      int (*callback)(void *,	/* token */
		      	const char *,		/* module name */
			const char *,		/* section name */
			ElfW(Addr),		/* Section start */
			ElfW(Addr),		/* Section end */
			ElfW(Word)		/* Section flags */
		      )
		);

#else

static inline int kallsyms_address_to_symbol(
	unsigned long     address,		/* Address to lookup */
	const char      **mod_name,		/* Set to module name */
	unsigned long    *mod_start,		/* Set to start address of module */
	unsigned long    *mod_end,		/* Set to end address of module */
	const char      **sec_name,		/* Set to section name */
	unsigned long    *sec_start,		/* Set to start address of section */
	unsigned long    *sec_end,		/* Set to end address of section */
	const char      **sym_name,		/* Set to full symbol name */
	unsigned long    *sym_start,		/* Set to start address of symbol */
	unsigned long    *sym_end		/* Set to end address of symbol */
	)
{
	return -ESRCH;
}

#endif

#endif /* kallsyms.h */
