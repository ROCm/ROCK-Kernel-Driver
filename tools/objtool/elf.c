/*
 * elf.c - ELF access library
 *
 * Adapted from kpatch (https://github.com/dynup/kpatch):
 * Copyright (C) 2013-2015 Josh Poimboeuf <jpoimboe@redhat.com>
 * Copyright (C) 2014 Seth Jennings <sjenning@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "elf.h"
#include "warn.h"

/*
 * Fallback for systems without this "read, mmaping if possible" cmd.
 */
#ifndef ELF_C_READ_MMAP
#define ELF_C_READ_MMAP ELF_C_READ
#endif

struct section *find_section_by_name(struct elf *elf, const char *name)
{
	struct section *sec;

	list_for_each_entry(sec, &elf->sections, list)
		if (!strcmp(sec->name, name))
			return sec;

	return NULL;
}

static struct section *find_section_by_index(struct elf *elf,
					     unsigned int idx)
{
	struct section *sec;

	list_for_each_entry(sec, &elf->sections, list)
		if (sec->idx == idx)
			return sec;

	return NULL;
}

static struct symbol *find_symbol_by_index(struct elf *elf, unsigned int idx)
{
	struct section *sec;
	struct symbol *sym;

	list_for_each_entry(sec, &elf->sections, list)
		hash_for_each_possible(sec->symbol_hash, sym, hash, idx)
			if (sym->idx == idx)
				return sym;

	return NULL;
}

struct symbol *find_symbol_by_offset(struct section *sec, unsigned long offset)
{
	struct symbol *sym;

	list_for_each_entry(sym, &sec->symbol_list, list)
		if (sym->type != STT_SECTION &&
		    sym->offset == offset)
			return sym;

	return NULL;
}

struct symbol *find_symbol_containing(struct section *sec, unsigned long offset)
{
	struct symbol *sym;

	list_for_each_entry(sym, &sec->symbol_list, list)
		if (sym->type != STT_SECTION &&
		    offset >= sym->offset && offset < sym->offset + sym->len)
			return sym;

	return NULL;
}

struct rela *find_rela_by_dest_range(struct section *sec, unsigned long offset,
				     unsigned int len)
{
	struct rela *rela;
	unsigned long o;

	if (!sec->rela)
		return NULL;

	for (o = offset; o < offset + len; o++)
		hash_for_each_possible(sec->rela->rela_hash, rela, hash, o)
			if (rela->offset == o)
				return rela;

	return NULL;
}

struct rela *find_rela_by_dest(struct section *sec, unsigned long offset)
{
	return find_rela_by_dest_range(sec, offset, 1);
}

struct symbol *find_containing_func(struct section *sec, unsigned long offset)
{
	struct symbol *func;

	list_for_each_entry(func, &sec->symbol_list, list)
		if (func->type == STT_FUNC && offset >= func->offset &&
		    offset < func->offset + func->len)
			return func;

	return NULL;
}

static int read_sections(struct elf *elf)
{
	Elf_Scn *s = NULL;
	struct section *sec;
	size_t shstrndx, sections_nr;
	int i;

	if (elf_getshdrnum(elf->elf, &sections_nr)) {
		perror("elf_getshdrnum");
		return -1;
	}

	if (elf_getshdrstrndx(elf->elf, &shstrndx)) {
		perror("elf_getshdrstrndx");
		return -1;
	}

	for (i = 0; i < sections_nr; i++) {
		sec = malloc(sizeof(*sec));
		if (!sec) {
			perror("malloc");
			return -1;
		}
		memset(sec, 0, sizeof(*sec));

		INIT_LIST_HEAD(&sec->symbol_list);
		INIT_LIST_HEAD(&sec->rela_list);
		hash_init(sec->rela_hash);
		hash_init(sec->symbol_hash);

		list_add_tail(&sec->list, &elf->sections);

		s = elf_getscn(elf->elf, i);
		if (!s) {
			perror("elf_getscn");
			return -1;
		}

		sec->idx = elf_ndxscn(s);

		if (!gelf_getshdr(s, &sec->sh)) {
			perror("gelf_getshdr");
			return -1;
		}

		sec->name = elf_strptr(elf->elf, shstrndx, sec->sh.sh_name);
		if (!sec->name) {
			perror("elf_strptr");
			return -1;
		}

		sec->data = elf_getdata(s, NULL);
		if (!sec->data) {
			perror("elf_getdata");
			return -1;
		}

		if (sec->data->d_off != 0 ||
		    sec->data->d_size != sec->sh.sh_size) {
			WARN("unexpected data attributes for %s", sec->name);
			return -1;
		}

		sec->len = sec->data->d_size;
	}

	/* sanity check, one more call to elf_nextscn() should return NULL */
	if (elf_nextscn(elf->elf, s)) {
		WARN("section entry mismatch");
		return -1;
	}

	return 0;
}

static int read_symbols(struct elf *elf)
{
	struct section *symtab;
	struct symbol *sym;
	struct list_head *entry, *tmp;
	int symbols_nr, i;

	symtab = find_section_by_name(elf, ".symtab");
	if (!symtab) {
		WARN("missing symbol table");
		return -1;
	}

	symbols_nr = symtab->sh.sh_size / symtab->sh.sh_entsize;

	for (i = 0; i < symbols_nr; i++) {
		sym = malloc(sizeof(*sym));
		if (!sym) {
			perror("malloc");
			return -1;
		}
		memset(sym, 0, sizeof(*sym));

		sym->idx = i;

		if (!gelf_getsym(symtab->data, i, &sym->sym)) {
			perror("gelf_getsym");
			goto err;
		}

		sym->name = elf_strptr(elf->elf, symtab->sh.sh_link,
				       sym->sym.st_name);
		if (!sym->name) {
			perror("elf_strptr");
			goto err;
		}

		sym->type = GELF_ST_TYPE(sym->sym.st_info);
		sym->bind = GELF_ST_BIND(sym->sym.st_info);

		if (sym->sym.st_shndx > SHN_UNDEF &&
		    sym->sym.st_shndx < SHN_LORESERVE) {
			sym->sec = find_section_by_index(elf,
							 sym->sym.st_shndx);
			if (!sym->sec) {
				WARN("couldn't find section for symbol %s",
				     sym->name);
				goto err;
			}
			if (sym->type == STT_SECTION) {
				sym->name = sym->sec->name;
				sym->sec->sym = sym;
			}
		} else
			sym->sec = find_section_by_index(elf, 0);

		sym->offset = sym->sym.st_value;
		sym->len = sym->sym.st_size;

		/* sorted insert into a per-section list */
		entry = &sym->sec->symbol_list;
		list_for_each_prev(tmp, &sym->sec->symbol_list) {
			struct symbol *s;

			s = list_entry(tmp, struct symbol, list);

			if (sym->offset > s->offset) {
				entry = tmp;
				break;
			}

			if (sym->offset == s->offset && sym->len >= s->len) {
				entry = tmp;
				break;
			}
		}
		list_add(&sym->list, entry);
		hash_add(sym->sec->symbol_hash, &sym->hash, sym->idx);
	}

	return 0;

err:
	free(sym);
	return -1;
}

static int read_relas(struct elf *elf)
{
	struct section *sec;
	struct rela *rela;
	int i;
	unsigned int symndx;

	list_for_each_entry(sec, &elf->sections, list) {
		if (sec->sh.sh_type != SHT_RELA)
			continue;

		sec->base = find_section_by_name(elf, sec->name + 5);
		if (!sec->base) {
			WARN("can't find base section for rela section %s",
			     sec->name);
			return -1;
		}

		sec->base->rela = sec;

		for (i = 0; i < sec->sh.sh_size / sec->sh.sh_entsize; i++) {
			rela = malloc(sizeof(*rela));
			if (!rela) {
				perror("malloc");
				return -1;
			}
			memset(rela, 0, sizeof(*rela));

			if (!gelf_getrela(sec->data, i, &rela->rela)) {
				perror("gelf_getrela");
				return -1;
			}

			rela->type = GELF_R_TYPE(rela->rela.r_info);
			rela->addend = rela->rela.r_addend;
			rela->offset = rela->rela.r_offset;
			symndx = GELF_R_SYM(rela->rela.r_info);
			rela->sym = find_symbol_by_index(elf, symndx);
			if (!rela->sym) {
				WARN("can't find rela entry symbol %d for %s",
				     symndx, sec->name);
				return -1;
			}

			list_add_tail(&rela->list, &sec->rela_list);
			hash_add(sec->rela_hash, &rela->hash, rela->offset);

		}
	}

	return 0;
}

struct elf *elf_open(const char *name)
{
	struct elf *elf;

	elf_version(EV_CURRENT);

	elf = malloc(sizeof(*elf));
	if (!elf) {
		perror("malloc");
		return NULL;
	}
	memset(elf, 0, sizeof(*elf));

	INIT_LIST_HEAD(&elf->sections);

	elf->fd = open(name, O_RDONLY);
	if (elf->fd == -1) {
		perror("open");
		goto err;
	}

	elf->elf = elf_begin(elf->fd, ELF_C_READ_MMAP, NULL);
	if (!elf->elf) {
		perror("elf_begin");
		goto err;
	}

	if (!gelf_getehdr(elf->elf, &elf->ehdr)) {
		perror("gelf_getehdr");
		goto err;
	}

	if (read_sections(elf))
		goto err;

	if (read_symbols(elf))
		goto err;

	if (read_relas(elf))
		goto err;

	return elf;

err:
	elf_close(elf);
	return NULL;
}

struct section *elf_create_section(struct elf *elf, const char *name,
				   size_t entsize, int nr)
{
	struct section *sec, *shstrtab;
	size_t size = entsize * nr;
	char *buf;

	sec = malloc(sizeof(*sec));
	if (!sec) {
		perror("malloc");
		return NULL;
	}
	memset(sec, 0, sizeof(*sec));

	INIT_LIST_HEAD(&sec->symbol_list);
	INIT_LIST_HEAD(&sec->rela_list);
	hash_init(sec->rela_hash);
	hash_init(sec->symbol_hash);

	list_add_tail(&sec->list, &elf->sections);

	sec->name = strdup(name);
	if (!sec->name) {
		perror("strdup");
		return NULL;
	}

	sec->idx = list_prev_entry(sec, list)->idx + 1;
	sec->len = size;

	sec->sh.sh_entsize = entsize;
	sec->sh.sh_size = size;

	sec->data = malloc(sizeof(*sec->data));
	if (!sec->data) {
		perror("malloc");
		return NULL;
	}

	sec->data->d_buf = NULL;
	if (size) {
		sec->data->d_buf = malloc(size);
		if (!sec->data->d_buf) {
			perror("malloc");
			return NULL;
		}
	}

	sec->data->d_size = size;
	sec->data->d_type = ELF_T_BYTE;

	shstrtab = find_section_by_name(elf, ".shstrtab");
	if (!shstrtab) {
		WARN("can't find .shstrtab section");
		return NULL;
	}
	size = shstrtab->len + strlen(name) + 1;
	buf = malloc(size);
	memcpy(buf, shstrtab->data->d_buf, shstrtab->len);
	strcpy(buf + shstrtab->len, name);
	sec->sh.sh_name = shstrtab->len;
	shstrtab->data->d_buf = buf;
	shstrtab->data->d_size = shstrtab->len = size;

	return sec;
}

struct section *elf_create_rela_section(struct elf *elf, struct section *base)
{
	char *relaname;
	struct section *sec;

	relaname = malloc(strlen(base->name) + strlen(".rela") + 1);
	if (!relaname) {
		perror("malloc");
		return NULL;
	}
	strcpy(relaname, ".rela");
	strcat(relaname, base->name);

	sec = elf_create_section(elf, relaname, sizeof(GElf_Rela), 0);
	if (!sec)
		return NULL;

	base->rela = sec;
	sec->base = base;

	sec->sh.sh_type = SHT_RELA;
	sec->sh.sh_addralign = 8;
	sec->sh.sh_link = find_section_by_name(elf, ".symtab")->idx;
	sec->sh.sh_info = base->idx;
	sec->sh.sh_flags = SHF_INFO_LINK;

	return sec;
}

int elf_rebuild_rela_section(struct section *sec)
{
	struct rela *rela;
	int nr, index = 0, size;
	GElf_Rela *relas;

	nr = 0;
	list_for_each_entry(rela, &sec->rela_list, list)
		nr++;

	size = nr * sizeof(*relas);
	relas = malloc(size);
	if (!relas) {
		perror("malloc");
		return -1;
	}

	sec->data->d_buf = relas;
	sec->data->d_size = size;

	sec->sh.sh_size = size;

	index = 0;
	list_for_each_entry(rela, &sec->rela_list, list) {
		relas[index].r_offset = rela->offset;
		relas[index].r_addend = rela->addend;
		relas[index].r_info = GELF_R_INFO(rela->sym->idx, rela->type);
		index++;
	}

	return 0;
}

int elf_write_to_file(struct elf *elf, char *outfile)
{
	int fd;
	struct section *sec;
	Elf *elfout;
	GElf_Ehdr eh, ehout;
	Elf_Scn *scn;
	Elf_Data *data;
	GElf_Shdr sh;

	fd = creat(outfile, 0777);
	if (fd == -1) {
		perror("creat");
		return -1;
	}

	elfout = elf_begin(fd, ELF_C_WRITE, NULL);
	if (!elfout) {
		perror("elf_begin");
		return -1;
	}

	if (!gelf_newehdr(elfout, gelf_getclass(elf->elf))) {
		perror("gelf_newehdr");
		return -1;
	}

	if (!gelf_getehdr(elfout, &ehout)) {
		perror("gelf_getehdr");
		return -1;
	}

	if (!gelf_getehdr(elf->elf, &eh)) {
		perror("gelf_getehdr");
		return -1;
	}

	memset(&ehout, 0, sizeof(ehout));
	ehout.e_ident[EI_DATA] = eh.e_ident[EI_DATA];
	ehout.e_machine = eh.e_machine;
	ehout.e_type = eh.e_type;
	ehout.e_version = EV_CURRENT;
	ehout.e_shstrndx = find_section_by_name(elf, ".shstrtab")->idx;

	list_for_each_entry(sec, &elf->sections, list) {
		if (sec->idx == 0)
			continue;

		scn = elf_newscn(elfout);
		if (!scn) {
			perror("elf_newscn");
			return -1;
		}

		data = elf_newdata(scn);
		if (!data) {
			perror("elf_newdata");
			return -1;
		}

		if (!elf_flagdata(data, ELF_C_SET, ELF_F_DIRTY)) {
			perror("elf_flagdata");
			return -1;
		}

		data->d_type = sec->data->d_type;
		data->d_buf = sec->data->d_buf;
		data->d_size = sec->data->d_size;

		if(!gelf_getshdr(scn, &sh)) {
			perror("gelf_getshdr");
			return -1;
		}

		sh = sec->sh;

		if (!gelf_update_shdr(scn, &sh)) {
			perror("gelf_update_shdr");
			return -1;
		}
	}

	if (!gelf_update_ehdr(elfout, &ehout)) {
		perror("gelf_update_ehdr");
		return -1;
	}

	if (elf_update(elfout, ELF_C_WRITE) < 0) {
		perror("elf_update");
		return -1;
	}

	return 0;
}

void elf_close(struct elf *elf)
{
	struct section *sec, *tmpsec;
	struct symbol *sym, *tmpsym;
	struct rela *rela, *tmprela;

	list_for_each_entry_safe(sec, tmpsec, &elf->sections, list) {
		list_for_each_entry_safe(sym, tmpsym, &sec->symbol_list, list) {
			list_del(&sym->list);
			hash_del(&sym->hash);
			free(sym);
		}
		list_for_each_entry_safe(rela, tmprela, &sec->rela_list, list) {
			list_del(&rela->list);
			hash_del(&rela->hash);
			free(rela);
		}
		list_del(&sec->list);
		free(sec);
	}
	if (elf->fd > 0)
		close(elf->fd);
	if (elf->elf)
		elf_end(elf->elf);
	free(elf);
}
