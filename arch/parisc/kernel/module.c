/*  Kernel module help for parisc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt...)
#endif

enum parisc_fsel {
	e_fsel,
	e_lsel,
	e_rsel,
	e_lrsel,
	e_rrsel
};

void *module_alloc(unsigned long size)
{
	if (size == 0)
		return NULL;
	return vmalloc(size);
}


/* Free memory returned from module_alloc */
void module_free(struct module *mod, void *module_region)
{
	vfree(module_region);
	/* FIXME: If module_region == mod->init_region, trim exception
           table entries. */
}

/* We don't need anything special. */
long module_core_size(const Elf_Ehdr *hdr,
		      const Elf_Shdr *sechdrs,
		      const char *secstrings,
		      struct module *module)
{
	return module->core_size;
}

long module_init_size(const Elf_Ehdr *hdr,
		      const Elf_Shdr *sechdrs,
		      const char *secstrings,
		      struct module *module)
{
	return module->init_size;
}

int module_frob_arch_sections(Elf_Ehdr *hdr,
			      Elf_Shdr *sechdrs,
			      char *secstrings,
			      struct module *mod)
{
	/* parisc should not need this ... */
	printk(KERN_ERR "module %s: %s not yet implemented.\n",
	       mod->name, __FUNCTION__);
	return 0;
}



int apply_relocate(Elf_Shdr *sechdrs,
		   const char *strtab,
		   unsigned int symindex,
		   unsigned int relsec,
		   struct module *me)
{
	/* parisc should not need this ... */
	printk(KERN_ERR "module %s: RELOCATION unsupported\n",
	       me->name);
	return -ENOEXEC;
}

#ifndef __LP64__
int apply_relocate_add(Elf_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *me)
{
	int i;
	Elf32_Rela *rel = (void *)sechdrs[relsec].sh_addr;
	Elf32_Sym *sym;
	Elf32_Word *loc;
	Elf32_Addr value;

	DEBUGP("Applying relocate section %u to %u\n", relsec,
	       sechdrs[relsec].sh_info);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		loc = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
		      + rel[i].r_offset;
		/* This is the symbol it is referring to */
		sym = (Elf32_Sym *)sechdrs[symindex].sh_addr
			+ ELF32_R_SYM(rel[i].r_info);
		if (!sym->st_value) {
			printk(KERN_WARNING "%s: Unknown symbol %s\n",
			       me->name, strtab + sym->st_name);
			return -ENOENT;
		}

		value = sym->st_value + rel[i].r_addend;

		DEBUGP("Symbol %s loc 0x%lx value 0x%lx: ",
			strtab + sym->st_name,
			(uint32_t)loc, value);

		switch (ELF32_R_TYPE(rel[i].r_info)) {
		case R_PARISC_PLABEL32:
			/* 32-bit function address */
			DEBUGP("R_PARISC_PLABEL32\n");
			break;
		case R_PARISC_DIR32:
			/* direct 32-bit ref */
			DEBUGP("R_PARISC_DIR32\n");
			break;
		case R_PARISC_DIR21L:
			/* left 21 bits of effective address */
			DEBUGP("R_PARISC_DIR21L\n");
			break;
		case R_PARISC_DIR14R:
			/* right 14 bits of effective address */
			DEBUGP("R_PARISC_DIR14R\n");
			break;
		case R_PARISC_SEGREL32:
			/* 32-bit segment relative address */
			DEBUGP("R_PARISC_SEGREL32\n");
			break;
		case R_PARISC_DPREL21L:
			/* left 21 bit of relative address */
			DEBUGP("R_PARISC_DPREL21L\n");
			break;
		case R_PARISC_DPREL14R:
			/* right 14 bit of relative address */
			DEBUGP("R_PARISC_DPREL14R\n");
			break;
		case R_PARISC_PCREL17F:
			/* 17-bit PC relative address */
			DEBUGP("R_PARISC_PCREL17F\n");
			break;
		case R_PARISC_PCREL22F:
			/* 22-bit PC relative address */
			DEBUGP("R_PARISC_PCREL22F\n");
			break;

		default:
			printk(KERN_ERR "module %s: Unknown relocation: %Lu\n",
			       me->name, ELF32_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
	}

	return -ENOEXEC;
}

#else
int apply_relocate_add(Elf_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *me)
{
	int i;
	Elf64_Rela *rel = (void *)sechdrs[relsec].sh_addr;
	Elf64_Sym *sym;
	Elf64_Word *loc;
	Elf64_Addr value;

	DEBUGP("Applying relocate section %u to %u\n", relsec,
	       sechdrs[relsec].sh_info);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		loc = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
		      + rel[i].r_offset;
		/* This is the symbol it is referring to */
		sym = (Elf64_Sym *)sechdrs[symindex].sh_addr
			+ ELF64_R_SYM(rel[i].r_info);
		if (!sym->st_value) {
			printk(KERN_WARNING "%s: Unknown symbol %s\n",
			       me->name, strtab + sym->st_name);
			return -ENOENT;
		}

		value = sym->st_value + rel[i].r_addend;

		DEBUGP("Symbol %s loc 0x%Lx value 0x%Lx: ",
			strtab + sym->st_name,
			(uint64_t)loc, value);

		switch (ELF64_R_TYPE(rel[i].r_info)) {
		case R_PARISC_LTOFF14R:
			/* LT-relative; right 14 bits */
			DEBUGP("R_PARISC_LTOFF14R\n");
			break;
		case R_PARISC_LTOFF21L:
			/* LT-relative; left 21 bits */
			DEBUGP("R_PARISC_LTOFF21L\n");
			break;
		case R_PARISC_PCREL22F:
			/* PC-relative; 22 bits */
			DEBUGP("R_PARISC_PCREL22F\n");
			break;
		case R_PARISC_DIR64:
			/* 64-bit effective address */
			DEBUGP("R_PARISC_DIR64\n");
			*loc = value;
			break;
		case R_PARISC_SEGREL32:
			/* 32-bit segment relative address */
			DEBUGP("R_PARISC_SEGREL32\n");
			break;
		case R_PARISC_FPTR64:
			/* 64-bit function address */
			DEBUGP("R_PARISC_FPTR64\n");
			break;

		default:
			printk(KERN_ERR "module %s: Unknown relocation: %Lu\n",
			       me->name, ELF64_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
	}
	return -ENOEXEC;
}
#endif

int module_finalize(const Elf_Ehdr *hdr,
		    const Elf_Shdr *sechdrs,
		    struct module *me)
{
	return 0;
}
