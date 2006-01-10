#ifndef _ASM_KDB_H
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
 * is intended to be used from interrupt level, it must use
 * a non-maskable entry method.
 */
#include <asm/kdb_break.h>		/* break numbers are separated for CONFIG_KDB_LOCK */
#define __KDB_ENTER2(b)	asm("\tbreak.m "#b"\n")
#define __KDB_ENTER1(b)	__KDB_ENTER2(b)
#define KDB_ENTER()		do {if (kdb_on && !KDB_IS_RUNNING()) { __KDB_ENTER1(KDB_BREAK_ENTER); }} while(0)
#define KDB_ENTER_SLAVE()	do {if (kdb_on) { __KDB_ENTER1(KDB_BREAK_ENTER_SLAVE); }} while(0)

	/*
	 * Needed for exported symbols.
	 */
typedef unsigned long kdb_machreg_t;

#define kdb_machreg_fmt		"0x%lx"
#define kdb_machreg_fmt0	"0x%016lx"
#define kdb_bfd_vma_fmt		"0x%lx"
#define kdb_bfd_vma_fmt0	"0x%016lx"
#define kdb_elfw_addr_fmt	"0x%lx"
#define kdb_elfw_addr_fmt0	"0x%016lx"

static inline unsigned long
kdba_funcptr_value(void *fp)
{
	/* ia64 function descriptor, first word is address of code */
	return *(unsigned long *)fp;
}

#endif	/* !_ASM_KDB_H */
