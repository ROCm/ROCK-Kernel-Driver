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

/* Total size to allocate for the non-releasable code; return len or
   -error.  mod->core_size is the current generic tally. */
long module_core_size(const Elf_Ehdr *hdr,
		      const Elf_Shdr *sechdrs,
		      const char *secstrings,
		      struct module *mod);

/* Total size of (if any) sections to be freed after init.  Return 0
   for none, len, or -error. mod->init_size is the current generic
   tally. */
long module_init_size(const Elf_Ehdr *hdr,
		      const Elf_Shdr *sechdrs,
		      const char *secstrings,
		      struct module *mod);

/* Allocator used for allocating struct module, core sections and init
   sections.  Returns NULL on failure. */
void *module_alloc(unsigned long size);

/* Free memory returned from module_alloc. */
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
