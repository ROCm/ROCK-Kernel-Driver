#ifndef _LINUX_MODULELOADER_H
#define _LINUX_MODULELOADER_H
/* The stuff needed for archs to support modules. */

#include <linux/module.h>
#include <linux/elf.h>

/* Helper function for arch-specific module loaders */
unsigned long find_symbol_internal(Elf_Shdr *sechdrs,
				   unsigned int symindex,
				   const char *strtab,
				   const char *name,
				   struct module *mod,
				   struct kernel_symbol_group **group);

/* These must be implemented by the specific architecture */

/* vmalloc AND zero for the non-releasable code; return ERR_PTR() on error. */
void *module_core_alloc(const Elf_Ehdr *hdr,
			const Elf_Shdr *sechdrs,
			const char *secstrings,
			struct module *mod);

/* vmalloc and zero (if any) for sections to be freed after init.
   Return ERR_PTR() on error. */
void *module_init_alloc(const Elf_Ehdr *hdr,
			const Elf_Shdr *sechdrs,
			const char *secstrings,
			struct module *mod);

/* Free memory returned from module_core_alloc/module_init_alloc */
void module_free(struct module *mod, void *module_region);

/* Apply the given relocation to the (simplified) ELF.  Return -error
   or 0. */
int apply_relocate(Elf_Shdr *sechdrs,
		   const char *strtab,
		   unsigned int symindex,
		   unsigned int relsec,
		   struct module *mod);

/* Apply the given add relocation to the (simplified) ELF.  Return
   -error or 0 */
int apply_relocate_add(Elf_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *mod);

/* Any final processing of module before access.  Return -error or 0. */
int module_finalize(const Elf_Ehdr *hdr,
		    const Elf_Shdr *sechdrs,
		    struct module *mod);

#endif
