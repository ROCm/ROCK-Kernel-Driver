#ifndef _ASM_MODULE_H
#define _ASM_MODULE_H

struct mod_arch_specific {
	/* Data Bus Error exception tables */
	const struct exception_table_entry *dbe_table_start;
	const struct exception_table_entry *dbe_table_end;
};

#define Elf_Shdr Elf32_Shdr
#define Elf_Sym Elf32_Sym
#define Elf_Ehdr Elf32_Ehdr

#endif /* _ASM_MODULE_H */
