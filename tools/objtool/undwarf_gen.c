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
			undwarf->cfa_reg = UNDWARF_REG_R13;
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

static int create_undwarf_entry(struct section *u_sec, struct section *ip_relasec,
				unsigned int idx, struct section *insn_sec,
				unsigned long insn_off, struct undwarf *u)
{
	struct undwarf *undwarf;
	struct rela *rela;

	/* populate undwarf */
	undwarf = (struct undwarf *)u_sec->data->d_buf + idx;
	memcpy(undwarf, u, sizeof(*undwarf));

	/* populate rela for ip */
	rela = malloc(sizeof(*rela));
	if (!rela) {
		perror("malloc");
		return -1;
	}
	memset(rela, 0, sizeof(*rela));

	rela->sym = insn_sec->sym;
	rela->addend = insn_off;
	rela->type = R_X86_64_PC32;
	rela->offset = idx * sizeof(int);

	list_add_tail(&rela->list, &ip_relasec->rela_list);
	hash_add(ip_relasec->rela_hash, &rela->hash, rela->offset);

	return 0;
}

int create_undwarf_sections(struct objtool_file *file)
{
	struct instruction *insn, *prev_insn;
	struct section *sec, *u_sec, *ip_relasec;
	unsigned int idx;

	struct undwarf empty = {
		.cfa_reg = UNDWARF_REG_UNDEFINED,
		.bp_reg  = UNDWARF_REG_UNDEFINED,
		.type    = UNDWARF_TYPE_CFA,
	};

	sec = find_section_by_name(file->elf, ".undwarf");
	if (sec) {
		WARN("file already has .undwarf section, skipping");
		return -1;
	}

	/* count the number of needed undwarves */
	idx = 0;
	for_each_sec(file, sec) {
		if (!sec->text)
			continue;

		prev_insn = NULL;
		sec_for_each_insn(file, sec, insn) {
			if (!prev_insn ||
			    memcmp(&insn->undwarf, &prev_insn->undwarf,
				   sizeof(struct undwarf))) {
				idx++;
			}
			prev_insn = insn;
		}

		/* section terminator */
		if (prev_insn)
			idx++;
	}
	if (!idx)
		return -1;


	/* create .undwarf_ip and .rela.undwarf_ip sections */
	sec = elf_create_section(file->elf, ".undwarf_ip", sizeof(int), idx);

	ip_relasec = elf_create_rela_section(file->elf, sec);
	if (!ip_relasec)
		return -1;

	/* create .undwarf section */
	u_sec = elf_create_section(file->elf, ".undwarf",
				   sizeof(struct undwarf), idx);

	/* populate sections */
	idx = 0;
	for_each_sec(file, sec) {
		if (!sec->text)
			continue;

		prev_insn = NULL;
		sec_for_each_insn(file, sec, insn) {
			if (!prev_insn || memcmp(&insn->undwarf,
						 &prev_insn->undwarf,
						 sizeof(struct undwarf))) {

				if (create_undwarf_entry(u_sec, ip_relasec, idx,
							 insn->sec, insn->offset,
							 &insn->undwarf))
					return -1;

				idx++;
			}
			prev_insn = insn;
		}

		/* section terminator */
		if (prev_insn) {
			if (create_undwarf_entry(u_sec, ip_relasec, idx,
						 prev_insn->sec,
						 prev_insn->offset + prev_insn->len,
						 &empty))
				return -1;

			idx++;
		}
	}

	if (elf_rebuild_rela_section(ip_relasec))
		return -1;

	return 0;
}
