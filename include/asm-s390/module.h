#ifndef _ASM_S390_MODULE_H
#define _ASM_S390_MODULE_H
/*
 * This file contains the s390 architecture specific module code.
 */

struct mod_arch_specific
{
	void *module_got, *module_plt;
	unsigned long got_size, plt_size;
};

#ifdef CONFIG_ARCH_S390X
#define ElfW(x) Elf64_ ## x
#define ELFW(x) ELF64_ ## x
#else
#define ElfW(x) Elf32_ ## x
#define ELFW(x) ELF32_ ## x
#endif

#define Elf_Shdr ElfW(Shdr)
#define Elf_Sym ElfW(Sym)
#define Elf_Ehdr ElfW(Ehdr)
#define ELF_R_TYPE ELFW(R_TYPE)
#endif /* _ASM_S390_MODULE_H */
