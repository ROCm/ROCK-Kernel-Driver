/*
 * Copyright (C) 2017 Josh Poimboeuf <jpoimboe@redhat.com>
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

#include <unistd.h>
#include "undwarf.h"
#include "warn.h"

static const char *reg_name(unsigned int reg)
{
	switch (reg) {
	case UNDWARF_REG_CFA:
		return "cfa";
	case UNDWARF_REG_DX:
		return "dx";
	case UNDWARF_REG_DI:
		return "di";
	case UNDWARF_REG_BP:
		return "bp";
	case UNDWARF_REG_SP:
		return "sp";
	case UNDWARF_REG_R10:
		return "r10";
	case UNDWARF_REG_R13:
		return "r13";
	case UNDWARF_REG_BP_INDIRECT:
		return "bp(ind)";
	case UNDWARF_REG_SP_INDIRECT:
		return "sp(ind)";
	default:
		return "?";
	}
}

static const char *undwarf_type_name(unsigned int type)
{
	switch (type) {
	case UNDWARF_TYPE_CFA:
		return "cfa";
	case UNDWARF_TYPE_REGS:
		return "regs";
	case UNDWARF_TYPE_REGS_IRET:
		return "iret";
	default:
		return "?";
	}
}

static void print_reg(unsigned int reg, int offset)
{
	if (reg == UNDWARF_REG_BP_INDIRECT)
		printf("(bp%+d)", offset);
	else if (reg == UNDWARF_REG_SP_INDIRECT)
		printf("(sp%+d)", offset);
	else if (reg == UNDWARF_REG_UNDEFINED)
		printf("(und)");
	else
		printf("%s%+d", reg_name(reg), offset);
}

int undwarf_dump(const char *_objname)
{
	int fd, nr_entries, i, *undwarf_ip = NULL, undwarf_size = 0;
	struct undwarf *undwarf = NULL;
	char *name;
	unsigned long nr_sections, undwarf_ip_addr = 0;
	size_t shstrtab_idx;
	Elf *elf;
	Elf_Scn *scn;
	GElf_Shdr sh;
	GElf_Rela rela;
	GElf_Sym sym;
	Elf_Data *data, *symtab = NULL, *rela_undwarf_ip = NULL;


	objname = _objname;

	elf_version(EV_CURRENT);

	fd = open(objname, O_RDONLY);
	if (fd == -1) {
		perror("open");
		return -1;
	}

	elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
	if (!elf) {
		WARN_ELF("elf_begin");
		return -1;
	}

	if (elf_getshdrnum(elf, &nr_sections)) {
		WARN_ELF("elf_getshdrnum");
		return -1;
	}

	if (elf_getshdrstrndx(elf, &shstrtab_idx)) {
		WARN_ELF("elf_getshdrstrndx");
		return -1;
	}

	for (i = 0; i < nr_sections; i++) {
		scn = elf_getscn(elf, i);
		if (!scn) {
			WARN_ELF("elf_getscn");
			return -1;
		}

		if (!gelf_getshdr(scn, &sh)) {
			WARN_ELF("gelf_getshdr");
			return -1;
		}

		name = elf_strptr(elf, shstrtab_idx, sh.sh_name);
		if (!name) {
			WARN_ELF("elf_strptr");
			return -1;
		}

		data = elf_getdata(scn, NULL);
		if (!data) {
			WARN_ELF("elf_getdata");
			return -1;
		}

		if (!strcmp(name, ".symtab")) {
			symtab = data;
		} else if (!strcmp(name, ".undwarf")) {
			undwarf = data->d_buf;
			undwarf_size = sh.sh_size;
		} else if (!strcmp(name, ".undwarf_ip")) {
			undwarf_ip = data->d_buf;
			undwarf_ip_addr = sh.sh_addr;
		} else if (!strcmp(name, ".rela.undwarf_ip")) {
			rela_undwarf_ip = data;
		}
	}

	if (!symtab || !undwarf || !undwarf_ip)
		return 0;

	if (undwarf_size % sizeof(*undwarf) != 0) {
		WARN("bad .undwarf section size");
		return -1;
	}

	nr_entries = undwarf_size / sizeof(*undwarf);
	for (i = 0; i < nr_entries; i++) {
		if (rela_undwarf_ip) {
			if (!gelf_getrela(rela_undwarf_ip, i, &rela)) {
				WARN_ELF("gelf_getrela");
				return -1;
			}

			if (!gelf_getsym(symtab, GELF_R_SYM(rela.r_info), &sym)) {
				WARN_ELF("gelf_getsym");
				return -1;
			}

			scn = elf_getscn(elf, sym.st_shndx);
			if (!scn) {
				WARN_ELF("elf_getscn");
				return -1;
			}

			if (!gelf_getshdr(scn, &sh)) {
				WARN_ELF("gelf_getshdr");
				return -1;
			}

			name = elf_strptr(elf, shstrtab_idx, sh.sh_name);
			if (!name || !*name) {
				WARN_ELF("elf_strptr");
				return -1;
			}

			printf("%s+%lx:", name, rela.r_addend);

		} else {
			printf("%lx:", undwarf_ip_addr + (i * sizeof(int)) + undwarf_ip[i]);
		}


		printf(" cfa:");

		print_reg(undwarf[i].cfa_reg, undwarf[i].cfa_offset);

		printf(" bp:");

		print_reg(undwarf[i].bp_reg, undwarf[i].bp_offset);

		printf(" type:%s\n", undwarf_type_name(undwarf[i].type));
	}

	elf_end(elf);
	close(fd);

	return 0;
}
