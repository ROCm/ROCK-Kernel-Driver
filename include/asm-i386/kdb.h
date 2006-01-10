#ifndef	_ASM_KDB_H
#define _ASM_KDB_H

/*
 * Kernel Debugger Architecture Dependent Global Headers
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * KDB_ENTER() is a macro which causes entry into the kernel
 * debugger from any point in the kernel code stream.  If it
 * is intended to be used from interrupt level, it must  use
 * a non-maskable entry method.
 */
#define KDB_ENTER()	do {if (kdb_on && !KDB_IS_RUNNING()) { asm("\tint $129\n"); }} while(0)

/*
 * Needed for exported symbols.
 */
typedef unsigned long kdb_machreg_t;

#define kdb_machreg_fmt		"0x%lx"
#define kdb_machreg_fmt0	"0x%08lx"
#define kdb_bfd_vma_fmt		"0x%lx"
#define kdb_bfd_vma_fmt0	"0x%08lx"
#define kdb_elfw_addr_fmt	"0x%x"
#define kdb_elfw_addr_fmt0	"0x%08x"

/*
 * Per cpu arch specific kdb state.  Must be in range 0xff000000.
 */
#define KDB_STATE_A_IF		0x01000000	/* Saved IF flag */

static inline unsigned long
kdba_funcptr_value(void *fp)
{
	return (unsigned long)fp;
}

#endif	/* !_ASM_KDB_H */
