/* $Id: r4k_tlb_glue.S,v 1.5 1999/11/23 17:12:49 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999 Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#define __ASSEMBLY__
#include <linux/init.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

	.macro	__BUILD_cli
	CLI
	.endm

	.macro	__BUILD_sti
	STI
	.endm

	.macro	tlb_handler name interruptible writebit
	NESTED(__\name, PT_SIZE, sp)
	SAVE_ALL
#if DEBUG_MIPS64
jal dodebug2
ld $4, PT_R4(sp)
ld $5, PT_R5(sp)
ld $6, PT_R6(sp)
ld $7, PT_R7(sp)
ld $2, PT_R2(sp)
#endif
	dmfc0	a2, CP0_BADVADDR
	__BUILD_\interruptible
	li	a1, \writebit
	sd	a2, PT_BVADDR(sp)
	move	a0, sp
	jal	do_page_fault
	j	ret_from_sys_call
	END(__\name)
	.endm

	tlb_handler	xtlb_mod sti 1
	tlb_handler	xtlb_tlbl sti 0
	tlb_handler	xtlb_tlbs sti 1
