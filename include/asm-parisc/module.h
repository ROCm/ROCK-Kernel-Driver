#ifndef _ASM_PARISC_MODULE_H
#define _ASM_PARISC_MODULE_H
/*
 * This file contains the parisc architecture specific module code.
 */
#ifdef __LP64__
#define Elf_Shdr Elf64_Shdr
#define Elf_Sym Elf64_Sym
#define Elf_Ehdr Elf64_Ehdr
#define Elf_Addr Elf64_Addr
#define Elf_Rela Elf64_Rela
#else
#define Elf_Shdr Elf32_Shdr
#define Elf_Sym Elf32_Sym
#define Elf_Ehdr Elf32_Ehdr
#define Elf_Addr Elf32_Addr
#define Elf_Rela Elf32_Rela
#endif

#define module_map(x)		vmalloc(x)
#define module_unmap(x)		vfree(x)
#define module_arch_init(x)	(0)
#define arch_init_modules(x)	do { } while (0)

struct mod_arch_specific
{
	unsigned long got_offset;
	unsigned long fdesc_offset, fdesc_count;
	unsigned long stub_offset;
	unsigned long stub_count;
};

#endif /* _ASM_PARISC_MODULE_H */
