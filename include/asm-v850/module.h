/*
 * include/asm-v850/module.h -- Architecture-specific module hooks
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *  Copyright (C) 2001  Rusty Russell
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 *
 * Derived in part from include/asm-ppc/module.h
 */

#ifndef __V850_MODULE_H__
#define __V850_MODULE_H__

#define MODULE_SYMBOL_PREFIX "_"

struct v850_plt_entry
{
	/* Indirect jump instruction sequence (6-byte mov + 2-byte jr).  */
	unsigned long tramp[2];
};

struct mod_arch_specific
{
	/* How much of the core is actually taken up with core (then
           we know the rest is for the PLT).  */
	unsigned int core_plt_offset;

	/* Same for init.  */
	unsigned int init_plt_offset;
};

#define Elf_Shdr Elf32_Shdr
#define Elf_Sym Elf32_Sym
#define Elf_Ehdr Elf32_Ehdr

#endif /* __V850_MODULE_H__ */
