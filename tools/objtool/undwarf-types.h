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

#ifndef _UNDWARF_TYPES_H
#define _UNDWARF_TYPES_H

/*
 * The UNDWARF_REG_* registers are base registers which are used to find other
 * registers on the stack.
 *
 * The CFA (call frame address) is the value of the stack pointer on the
 * previous frame, i.e. the caller's SP before it called the callee.
 *
 * The CFA is usually based on SP, unless a frame pointer has been saved, in
 * which case it's based on BP.
 *
 * BP is usually either based on CFA or is undefined (meaning its value didn't
 * change for the current frame).
 *
 * So the CFA base is usually either SP or BP, and the FP base is usually either
 * CFA or undefined.  The rest of the base registers are needed for special
 * cases like entry code and gcc aligned stacks.
 */
#define UNDWARF_REG_UNDEFINED		0
#define UNDWARF_REG_CFA			1
#define UNDWARF_REG_DX			2
#define UNDWARF_REG_DI			3
#define UNDWARF_REG_BP			4
#define UNDWARF_REG_SP			5
#define UNDWARF_REG_R10			6
#define UNDWARF_REG_R13			7
#define UNDWARF_REG_BP_INDIRECT		8
#define UNDWARF_REG_SP_INDIRECT		9
#define UNDWARF_REG_MAX			15

/*
 * UNDWARF_TYPE_CFA: Indicates that cfa_reg+cfa_offset points to the caller's
 * stack pointer (aka the CFA in DWARF terms).  Used for all callable
 * functions, i.e.  all C code and all callable asm functions.
 *
 * UNDWARF_TYPE_REGS: Used in entry code to indicate that cfa_reg+cfa_offset
 * points to a fully populated pt_regs from a syscall, interrupt, or exception.
 *
 * UNDWARF_TYPE_REGS_IRET: Used in entry code to indicate that
 * cfa_reg+cfa_offset points to the iret return frame.
 *
 * The CFI_HINT macros are only used for the undwarf_cfi_hints struct.  They
 * are not used for the undwarf struct due to size and complexity constraints.
 */
#define UNDWARF_TYPE_CFA		0
#define UNDWARF_TYPE_REGS		1
#define UNDWARF_TYPE_REGS_IRET		2
#define CFI_HINT_TYPE_SAVE		3
#define CFI_HINT_TYPE_RESTORE		4

#ifndef __ASSEMBLY__
/*
 * This struct contains a simplified version of the DWARF Call Frame
 * Information standard.  It contains only the necessary parts of the real
 * DWARF, simplified for ease of access by the in-kernel unwinder.  It tells
 * the unwinder how to find the previous SP and BP (and sometimes entry regs)
 * on the stack, given a code address (IP).
 */
struct undwarf {
	int ip;
	unsigned int len;
	short cfa_offset;
	short bp_offset;
	unsigned cfa_reg:4;
	unsigned bp_reg:4;
	unsigned type:2;
};

/*
 * This struct is used by asm and inline asm code to manually annotate the
 * location of registers on the stack for the undwarf unwinder.
 */
struct undwarf_cfi_hint {
	unsigned int ip;
	short cfa_offset;
	unsigned char cfa_reg;
	unsigned char type;
};
#endif /* __ASSEMBLY__ */

#endif /* _UNDWARF_TYPES_H */
