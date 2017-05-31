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

#include <stdlib.h>
#include <string.h>

#include "undwarf.h"
#include "check.h"
#include "warn.h"

int create_undwarf(struct objtool_file *file)
{
	struct instruction *insn;

	for_each_insn(file, insn) {
		struct undwarf *undwarf = &insn->undwarf;
		struct cfi_reg *cfa = &insn->state.cfa;
		struct cfi_reg *bp = &insn->state.regs[CFI_BP];

		if (cfa->base == CFI_UNDEFINED) {
			undwarf->cfa_reg = UNDWARF_REG_UNDEFINED;
			continue;
		}

		switch (cfa->base) {
		case CFI_SP:
			undwarf->cfa_reg = UNDWARF_REG_SP;
			break;
		case CFI_SP_INDIRECT:
			undwarf->cfa_reg = UNDWARF_REG_SP_INDIRECT;
			break;
		case CFI_BP:
			undwarf->cfa_reg = UNDWARF_REG_BP;
			break;
		case CFI_BP_INDIRECT:
			undwarf->cfa_reg = UNDWARF_REG_BP_INDIRECT;
			break;
		case CFI_R10:
			undwarf->cfa_reg = UNDWARF_REG_R10;
			break;
		case CFI_R13:
			undwarf->cfa_reg = UNDWARF_REG_R10;
			break;
		case CFI_DI:
			undwarf->cfa_reg = UNDWARF_REG_DI;
			break;
		case CFI_DX:
			undwarf->cfa_reg = UNDWARF_REG_DX;
			break;
		default:
			WARN_FUNC("unknown CFA base reg %d",
				  insn->sec, insn->offset, cfa->base);
			return -1;
		}

		switch(bp->base) {
		case CFI_UNDEFINED:
			undwarf->bp_reg = UNDWARF_REG_UNDEFINED;
			break;
		case CFI_CFA:
			undwarf->bp_reg = UNDWARF_REG_CFA;
			break;
		case CFI_BP:
			undwarf->bp_reg = UNDWARF_REG_BP;
			break;
		default:
			WARN_FUNC("unknown BP base reg %d",
				  insn->sec, insn->offset, bp->base);
			return -1;
		}

		undwarf->cfa_offset = cfa->offset;
		undwarf->bp_offset = bp->offset;
		undwarf->type = insn->state.type;
	}

	return 0;
}

int create_undwarf_section(struct objtool_file *file)
{
	struct instruction *insn, *prev_insn = NULL;
	struct section *sec, *relasec;
	struct rela *rela;
	unsigned int index, nr = 0;
	struct undwarf *undwarf = NULL;

	sec = find_section_by_name(file->elf, ".undwarf");
	if (sec) {
		WARN("file already has .undwarf section, skipping");
		return -1;
	}

	/* count number of needed undwarves */
	for_each_insn(file, insn) {
		if (insn->needs_cfi &&
		    (!prev_insn || prev_insn->sec != insn->sec ||
		     memcmp(&insn->undwarf, &prev_insn->undwarf,
			    sizeof(struct undwarf)))) {
			nr++;
		}
		prev_insn = insn;
	}

	if (!nr)
		return 0;

	/* create .undwarf and .rela.undwarf sections */
	sec = elf_create_section(file->elf, ".undwarf",
				 sizeof(struct undwarf), nr);

	sec->sh.sh_type = SHT_PROGBITS;
	sec->sh.sh_addralign = 1;
	sec->sh.sh_flags = SHF_ALLOC;

	relasec = elf_create_rela_section(file->elf, sec);
	if (!relasec)
		return -1;

	/* populate sections */
	index = 0;
	prev_insn = NULL;
	for_each_insn(file, insn) {
		if (insn->needs_cfi &&
		    (!prev_insn || prev_insn->sec != insn->sec ||
		     memcmp(&insn->undwarf, &prev_insn->undwarf,
			    sizeof(struct undwarf)))) {

#if 0
			printf("%s:%lx: cfa:%d+%d bp:%d+%d type:%d\n",
			       insn->sec->name, insn->offset, insn->undwarf.cfa_reg,
			       insn->undwarf.cfa_offset, insn->undwarf.fp_reg,
			       insn->undwarf.fp_offset, insn->undwarf.type);
#endif

			undwarf = (struct undwarf *)sec->data->d_buf + index;

			memcpy(undwarf, &insn->undwarf, sizeof(*undwarf));
			undwarf->len = insn->len;

			/* add rela for undwarf->ip */
			rela = malloc(sizeof(*rela));
			if (!rela) {
				perror("malloc");
				return -1;
			}
			memset(rela, 0, sizeof(*rela));

			rela->sym = insn->sec->sym;
			rela->addend = insn->offset;
			rela->type = R_X86_64_PC32;
			rela->offset = index * sizeof(struct undwarf);

			list_add_tail(&rela->list, &relasec->rela_list);
			hash_add(relasec->rela_hash, &rela->hash, rela->offset);

			index++;

		} else if (insn->needs_cfi) {
			undwarf->len += insn->len;
		}
		prev_insn = insn;
	}

	if (elf_rebuild_rela_section(relasec))
		return -1;

	return 0;
}

int update_file(struct objtool_file *file)
{
	char *outfile;
	int ret;

	outfile = malloc(strlen(objname) + strlen(".undwarf") + 1);
	if (!outfile) {
		perror("malloc");
		return -1;
	}

	strcpy(outfile, objname);
	strcat(outfile, ".undwarf");
	ret = elf_write_to_file(file->elf, outfile);
	if (ret < 0)
		return -1;

	if (rename(outfile, objname) < 0) {
		WARN("can't rename file");
		perror("rename");
		return -1;
	}

	free(outfile);

	return 0;
}

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
	struct elf *elf;
	struct section *sec;
	struct rela *rela;
	struct undwarf *undwarf;
	int nr, i;

	objname = _objname;

	elf = elf_open(objname);
	if (!elf) {
		WARN("error reading elf file %s\n", objname);
		return 1;
	}

	sec = find_section_by_name(elf, ".undwarf");
	if (!sec || !sec->rela)
		return 0;

	nr = sec->len / sizeof(*undwarf);
	for (i = 0; i < nr; i++) {
		undwarf = (struct undwarf *)sec->data->d_buf + i;

		rela = find_rela_by_dest(sec, i * sizeof(*undwarf));
		if (!rela) {
			WARN("can't find rela for undwarf[%d]\n", i);
			return 1;
		}

		printf("%s+%x: len:%u cfa:",
		       rela->sym->name, rela->addend, undwarf->len);

		print_reg(undwarf->cfa_reg, undwarf->cfa_offset);

		printf(" bp:");

		print_reg(undwarf->bp_reg, undwarf->bp_offset);

		printf(" type:%s\n", undwarf_type_name(undwarf->type));
	}

	return 0;
}
