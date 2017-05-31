/*
 * sortextable.h
 *
 * Copyright 2011 - 2012 Cavium, Inc.
 *
 * Some of this code was taken out of recordmcount.h written by:
 *
 * Copyright 2009 John F. Reiser <jreiser@BitWagon.com>.  All rights reserved.
 * Copyright 2010 Steven Rostedt <srostedt@redhat.com>, Red Hat Inc.
 *
 *
 * Licensed under the GNU General Public License, version 2 (GPLv2).
 */

#undef extable_ent_size
#undef generic_compare
#undef do_func
#undef Elf_Addr
#undef Elf_Ehdr
#undef Elf_Shdr
#undef Elf_Rel
#undef Elf_Rela
#undef Elf_Sym
#undef ELF_R_SYM
#undef Elf_r_sym
#undef ELF_R_INFO
#undef Elf_r_info
#undef ELF_ST_BIND
#undef ELF_ST_TYPE
#undef fn_ELF_R_SYM
#undef fn_ELF_R_INFO
#undef uint_t
#undef _r
#undef _w

#ifdef SORTTABLE_64
# define generic_compare	generic_compare_64
# define do_func		do64
# define Elf_Addr		Elf64_Addr
# define Elf_Ehdr		Elf64_Ehdr
# define Elf_Shdr		Elf64_Shdr
# define Elf_Rel		Elf64_Rel
# define Elf_Rela		Elf64_Rela
# define Elf_Sym		Elf64_Sym
# define ELF_R_SYM		ELF64_R_SYM
# define Elf_r_sym		Elf64_r_sym
# define ELF_R_INFO		ELF64_R_INFO
# define Elf_r_info		Elf64_r_info
# define ELF_ST_BIND		ELF64_ST_BIND
# define ELF_ST_TYPE		ELF64_ST_TYPE
# define fn_ELF_R_SYM		fn_ELF64_R_SYM
# define fn_ELF_R_INFO		fn_ELF64_R_INFO
# define uint_t			uint64_t
# define _r			r8
# define _w			w8
#else
# define generic_compare	generic_compare_32
# define do_func		do32
# define Elf_Addr		Elf32_Addr
# define Elf_Ehdr		Elf32_Ehdr
# define Elf_Shdr		Elf32_Shdr
# define Elf_Rel		Elf32_Rel
# define Elf_Rela		Elf32_Rela
# define Elf_Sym		Elf32_Sym
# define ELF_R_SYM		ELF32_R_SYM
# define Elf_r_sym		Elf32_r_sym
# define ELF_R_INFO		ELF32_R_INFO
# define Elf_r_info		Elf32_r_info
# define ELF_ST_BIND		ELF32_ST_BIND
# define ELF_ST_TYPE		ELF32_ST_TYPE
# define fn_ELF_R_SYM		fn_ELF32_R_SYM
# define fn_ELF_R_INFO		fn_ELF32_R_INFO
# define uint_t			uint32_t
# define _r			r
# define _w			w
#endif

static int generic_compare(const void *a, const void *b)
{
	Elf_Addr av = _r(a);
	Elf_Addr bv = _r(b);

	if (av < bv)
		return -1;
	if (av > bv)
		return 1;
	return 0;
}

static int
do_func(Elf_Ehdr *ehdr, char const *const fname, char const *const secname,
	size_t entsize, table_sort_t custom_sort,
	char const *const sort_needed_var)
{
	Elf_Shdr *shdr;
	Elf_Shdr *shstrtab_sec;
	Elf_Shdr *strtab_sec = NULL;
	Elf_Shdr *symtab_sec = NULL;
	Elf_Shdr *table_sec = NULL;
	Elf_Sym *sym;
	const Elf_Sym *symtab;
	Elf32_Word *symtab_shndx_start = NULL;
	Elf_Sym *sort_needed_sym;
	Elf_Shdr *sort_needed_sec;
	Elf_Rel *relocs = NULL;
	int relocs_size = 0;
	uint32_t *sort_done_location;
	const char *secstrtab;
	const char *strtab;
	char *table_image;
	int table_index = 0;
	int i;
	int idx;
	unsigned int num_sections;
	unsigned int secindex_strings;

	shdr = (Elf_Shdr *)((char *)ehdr + _r(&ehdr->e_shoff));

	num_sections = r2(&ehdr->e_shnum);
	if (num_sections == SHN_UNDEF)
		num_sections = _r(&shdr[0].sh_size);

	secindex_strings = r2(&ehdr->e_shstrndx);
	if (secindex_strings == SHN_XINDEX)
		secindex_strings = r(&shdr[0].sh_link);

	shstrtab_sec = shdr + secindex_strings;
	secstrtab = (const char *)ehdr + _r(&shstrtab_sec->sh_offset);
	for (i = 0; i < num_sections; i++) {
		idx = r(&shdr[i].sh_name);
		if (strcmp(secstrtab + idx, secname) == 0) {
			table_sec = shdr + i;
			table_index = i;
		}
		if ((r(&shdr[i].sh_type) == SHT_REL ||
		     r(&shdr[i].sh_type) == SHT_RELA) &&
		    r(&shdr[i].sh_info) == table_index) {
			relocs = (void *)ehdr + _r(&shdr[i].sh_offset);
			relocs_size = _r(&shdr[i].sh_size);
		}
		if (strcmp(secstrtab + idx, ".symtab") == 0)
			symtab_sec = shdr + i;
		if (strcmp(secstrtab + idx, ".strtab") == 0)
			strtab_sec = shdr + i;
		if (r(&shdr[i].sh_type) == SHT_SYMTAB_SHNDX)
			symtab_shndx_start = (Elf32_Word *)(
				(const char *)ehdr + _r(&shdr[i].sh_offset));
	}
	if (strtab_sec == NULL) {
		fprintf(stderr,	"no .strtab in file: %s\n", fname);
		return -1;
	}
	if (symtab_sec == NULL) {
		fprintf(stderr,	"no .symtab in file: %s\n", fname);
		return -1;
	}
	symtab = (const Elf_Sym *)((const char *)ehdr +
				   _r(&symtab_sec->sh_offset));
	if (table_sec == NULL) {
		fprintf(stderr,	"no %s section in file: %s\n", secname, fname);
		return -1;
	}
	strtab = (const char *)ehdr + _r(&strtab_sec->sh_offset);

	table_image = (void *)ehdr + _r(&table_sec->sh_offset);

	if (custom_sort) {
		custom_sort(table_image, _r(&table_sec->sh_size), entsize);
	} else {
		int num_entries = _r(&table_sec->sh_size) / entsize;
		qsort(table_image, num_entries, entsize, generic_compare);
	}
	/* If there were relocations, we no longer need them. */
	if (relocs)
		memset(relocs, 0, relocs_size);

	if (!sort_needed_var)
		return 0;

	/* find sort needed variable so we can clear it */
	sort_needed_sym = NULL;
	for (i = 0; i < _r(&symtab_sec->sh_size) / sizeof(Elf_Sym); i++) {
		sym = (void *)ehdr + _r(&symtab_sec->sh_offset);
		sym += i;
		if (ELF_ST_TYPE(sym->st_info) != STT_OBJECT)
			continue;
		idx = r(&sym->st_name);
		if (strcmp(strtab + idx, sort_needed_var) == 0) {
			sort_needed_sym = sym;
			break;
		}
	}
	if (sort_needed_sym == NULL) {
		fprintf(stderr,
			"no %s symbol in file: %s\n",
			sort_needed_var, fname);
		return -1;
	}
	sort_needed_sec = &shdr[get_secindex(r2(&sym->st_shndx),
					     sort_needed_sym - symtab,
					     symtab_shndx_start)];
	sort_done_location = (void *)ehdr +
		_r(&sort_needed_sec->sh_offset) +
		_r(&sort_needed_sym->st_value) -
		_r(&sort_needed_sec->sh_addr);

#if 0
	printf("sort done marker at %lx\n",
	       (unsigned long)((char *)sort_done_location - (char *)ehdr));
#endif
	/* We sorted it, clear the flag. */
	w(0, sort_done_location);
	return 0;
}
