#ifndef _ASM_IA64_MODULE_H
#define _ASM_IA64_MODULE_H

/*
 * IA-64-specific support for kernel module loader.
 *
 * Copyright (C) 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

struct elf64_shdr;			/* forward declration */

struct mod_arch_specific {
	/*
	 * PLTs need to be within 16MB of the call-site.  Since the core and the init
	 * sections are allocated separately, we need to maintain separate PLT areas
	 * for them.  Function descriptors and global-offset-table entries are, in
	 * contrast, always allocated in the core.
	 */
	struct elf64_shdr *init_text_sec;	/* .init.text section (or NULL) */
	unsigned long init_plt_offset;

	struct elf64_shdr *core_text_sec;	/* .text section (or NULL) */
	unsigned long core_plt_offset;
	unsigned long fdesc_offset;
	unsigned long got_offset;
};

#define Elf_Shdr	Elf64_Shdr
#define Elf_Sym		Elf64_Sym
#define Elf_Ehdr	Elf64_Ehdr

#define MODULE_PROC_FAMILY	"ia64"
#define MODULE_ARCH_VERMAGIC	MODULE_PROC_FAMILY

#endif /* _ASM_IA64_MODULE_H */
