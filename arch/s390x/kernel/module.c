/*
 *  arch/s390x/kernel/module.c - Kernel module help for s390x.
 *
 *  S390 version
 *    Copyright (C) 2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Arnd Bergmann (arndb@de.ibm.com)
 *		 Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  based on i386 version
 *    Copyright (C) 2001 Rusty Russell.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/module.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt , ...)
#endif

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

/* s390/s390x needs additional memory for GOT/PLT sections. */
long module_core_size(const Elf32_Ehdr *hdr,
		      const Elf32_Shdr *sechdrs,
		      const char *secstrings,
		      struct module *module)
{
	// FIXME: add space needed for GOT/PLT
	return module->core_size;
}

long module_init_size(const Elf32_Ehdr *hdr,
		      const Elf32_Shdr *sechdrs,
		      const char *secstrings,
		      struct module *module)
{
	return module->init_size;
}



int apply_relocate(Elf_Shdr *sechdrs,
		   const char *strtab,
		   unsigned int symindex,
		   unsigned int relsec,
		   struct module *me)
{
	unsigned int i;
	ElfW(Rel) *rel = (void *)sechdrs[relsec].sh_addr;
	ElfW(Sym) *sym;
	ElfW(Addr) *location;

	DEBUGP("Applying relocate section %u to %u\n", relsec,
	       sechdrs[relsec].sh_info);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset;
		/* This is the symbol it is referring to */
		sym = (ElfW(Sym) *)sechdrs[symindex].sh_addr
			+ ELFW(R_SYM)(rel[i].r_info);
		if (!sym->st_value) {
			printk(KERN_WARNING "%s: Unknown symbol %s\n",
			       me->name, strtab + sym->st_name);
			return -ENOENT;
		}

		switch (ELF_R_TYPE(rel[i].r_info)) {
		case R_390_8:		/* Direct 8 bit.   */
			*(u8*) location += sym->st_value;
			break;
		case R_390_12:		/* Direct 12 bit.  */
			*(u16*) location = (*(u16*) location & 0xf000) | 
				(sym->st_value & 0xfff);
			break;
		case R_390_16:		/* Direct 16 bit.  */
			*(u16*) location += sym->st_value;
			break;
		case R_390_32:		/* Direct 32 bit.  */
			*(u32*) location += sym->st_value;
			break;
		case R_390_64:		/* Direct 64 bit.  */
			*(u64*) location += sym->st_value;
			break;
		case R_390_PC16:	/* PC relative 16 bit.  */
			*(u16*) location += sym->st_value
					    - (unsigned long )location;

		case R_390_PC16DBL:	/* PC relative 16 bit shifted by 1.  */
			*(u16*) location += (sym->st_value
					     - (unsigned long )location) >> 1;
		case R_390_PC32:	/* PC relative 32 bit.  */
			*(u32*) location += sym->st_value
					    - (unsigned long )location;
			break;
		case R_390_PC32DBL:	/* PC relative 32 bit shifted by 1.  */
			*(u32*) location += (sym->st_value
					     - (unsigned long )location) >> 1;
			break;
		case R_390_PC64:	/* PC relative 64 bit.  */
			*(u64*) location += sym->st_value
					    - (unsigned long )location;
			break;
		case R_390_GOT12:	/* 12 bit GOT offset.  */
		case R_390_GOT16:	/* 16 bit GOT offset.  */
		case R_390_GOT32:	/* 32 bit GOT offset.  */
		case R_390_GOT64:	/* 64 bit GOT offset.  */
		case R_390_GOTENT:	/* 32 bit PC rel. to GOT entry >> 1. */
			// FIXME: TODO
			break;

		case R_390_PLT16DBL:	/* 16 bit PC rel. PLT shifted by 1.  */
		case R_390_PLT32:	/* 32 bit PC relative PLT address.  */
		case R_390_PLT32DBL:	/* 32 bit PC rel. PLT shifted by 1.  */
		case R_390_PLT64:	/* 64 bit PC relative PLT address.   */
			// FIXME: TODO
			break;
		case R_390_GLOB_DAT:	/* Create GOT entry.  */
		case R_390_JMP_SLOT:	/* Create PLT entry.  */
			*location = sym->st_value;
			break;
		case R_390_RELATIVE:	/* Adjust by program base.  */
			// FIXME: TODO
			break;
		case R_390_GOTOFF:	/* 32 bit offset to GOT.  */
			// FIXME: TODO
			break;
		case R_390_GOTPC:	/* 32 bit PC relative offset to GOT. */
		case R_390_GOTPCDBL:	/* 32 bit PC rel. GOT shifted by 1.  */
			// FIXME: TODO
			break;
		default:
			printk(KERN_ERR "module %s: Unknown relocation: %lu\n",
			       me->name,
			       (unsigned long)ELF_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
	}
	return 0;
}

int apply_relocate_add(Elf32_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *me)
{
	printk(KERN_ERR "module %s: ADD RELOCATION unsupported\n",
	       me->name);
	return -ENOEXEC;
}

int module_finalize(const Elf_Ehdr *hdr,
		    const Elf_Shdr *sechdrs,
		    struct module *me)
{
	return 0;
}
