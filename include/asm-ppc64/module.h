#ifndef _ASM_PPC64_MODULE_H
#define _ASM_PPC64_MODULE_H

struct mod_arch_specific
{
	/* Index of stubs section within module. */
	unsigned int stubs_section;

	/* What section is the TOC? */
	unsigned int toc_section;
};

#define Elf_Shdr Elf64_Shdr
#define Elf_Sym Elf64_Sym
#define Elf_Ehdr Elf64_Ehdr

/* Make empty section for module_frob_arch_sections to expand. */
#ifdef MODULE
asm(".section .stubs,\"ax\",@nobits; .align 3; .previous");
#endif

struct exception_table_entry;
void sort_ex_table(struct exception_table_entry *start,
			struct exception_table_entry *finish);

#endif /* _ASM_PPC64_MODULE_H */
