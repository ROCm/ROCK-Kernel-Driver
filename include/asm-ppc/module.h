#ifndef _ASM_PPC_MODULE_H
#define _ASM_PPC_MODULE_H
/* Module stuff for PPC.  (C) 2001 Rusty Russell */

/* Thanks to Paul M for explaining this.

   PPC can only do rel jumps += 32MB, and often the kernel and other
   modules are furthur away than this.  So, we jump to a table of
   trampolines attached to the module (the Procedure Linkage Table)
   whenever that happens.
*/

struct ppc_plt_entry
{
	/* 16 byte jump instruction sequence (4 instructions) */
	unsigned int jump[4];
};

struct mod_arch_specific
{
	/* How much of the core is actually taken up with core (then
           we know the rest is for the PLT */
	unsigned int core_plt_offset;

	/* Same for init */
	unsigned int init_plt_offset;
};

#define Elf_Shdr Elf32_Shdr
#define Elf_Sym Elf32_Sym
#define Elf_Ehdr Elf32_Ehdr

#endif /* _ASM_PPC_MODULE_H */
