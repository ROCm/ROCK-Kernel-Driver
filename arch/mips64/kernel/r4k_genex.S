/* $Id: r4k_genex.S,v 1.3 1999/11/23 17:12:49 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics
 *
 * Low level exception handling
 */
#define __ASSEMBLY__
#include <linux/init.h>
#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/fpregdef.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>
#include <asm/r4kcacheops.h>

	.macro	__build_clear_none
	.endm

	.macro	__build_clear_sti
	STI
	.endm

	.macro	__build_clear_cli
	CLI
	.endm

	.macro	__build_clear_fpe
	cfc1	a1, fcr31
	li	a2, ~(0x3f << 13)
	and	a2, a1
	ctc1	a2, fcr31
	STI
	.endm

	.macro	__build_clear_ade
	dmfc0	t0, CP0_BADVADDR
	sd	t0, PT_BVADDR(sp)
	KMODE
	.endm

	.macro	__BUILD_silent exception
	.endm

	/* Gas tries to parse the PRINT argument as a string containing
	   string escapes and emits bogus warnings if it believes to
	   recognize an unknown escape code.  So make the arguments
	   start with an n and gas will believe \n is ok ...  */
	.macro	__BUILD_verbose	nexception
	ld	a1, PT_EPC(sp)
	PRINT("Got \nexception at %016lx\012")
	.endm

	.macro	__BUILD_count exception
	.set	reorder
	ld	t0,exception_count_\exception
	daddiu	t0, 1
	sd	t0,exception_count_\exception
	.set	noreorder
	.comm	exception_count\exception, 8, 8
	.endm

	.macro	BUILD_HANDLER exception handler clear verbose
	.align	5
	NESTED(handle_\exception, PT_SIZE, sp)
	.set	noat
	SAVE_ALL
#if DEBUG_MIPS64
jal dodebug2
ld $4, PT_R4(sp)
ld $5, PT_R5(sp)
ld $6, PT_R6(sp)
ld $7, PT_R7(sp)
ld $2, PT_R2(sp)
#endif
	__BUILD_clear_\clear
	.set	at
	__BUILD_\verbose \exception
	move	a0, sp
	jal	do_\handler
	j	ret_from_sys_call
	 nop
	END(handle_\exception)
	.endm

	BUILD_HANDLER adel ade ade silent		/* #4  */
	BUILD_HANDLER ades ade ade silent		/* #5  */
	BUILD_HANDLER ibe ibe cli silent		/* #6  */
	BUILD_HANDLER dbe dbe cli silent		/* #7  */
	BUILD_HANDLER bp bp sti silent			/* #9  */
	BUILD_HANDLER ri ri sti silent			/* #10 */
	BUILD_HANDLER cpu cpu sti silent		/* #11 */
	BUILD_HANDLER ov ov sti silent			/* #12 */
	BUILD_HANDLER tr tr sti silent			/* #13 */
	BUILD_HANDLER fpe fpe fpe silent		/* #15 */
	BUILD_HANDLER watch watch sti verbose		/* #23 */
	BUILD_HANDLER reserved reserved sti verbose	/* others */

	__INIT

/* General exception handler for CPUs with virtual coherency exception.
 *
 * Be careful when changing this, it has to be at most 128 bytes to fit
 * into space reserved for the exception handler.
 */
	NESTED(except_vec3_r4000, 0, sp)
	.set	noat
	mfc0	k1, CP0_CAUSE
	andi	k1, k1, 0x7c
	li	k0, 31<<2
	beq	k1, k0, handle_vced
	 li	k0, 14<<2
	beq	k1, k0, handle_vcei
	 dsll	k1, k1, 1
	ld	k0, exception_handlers(k1)
	jr	k0

/*
 * Big shit, we now may have two dirty primary cache lines for the same
 * physical address.  We can savely invalidate the line pointed to by
 * c0_badvaddr because after return from this exception handler the load /
 * store will be re-executed.
 */
handle_vced:
	mfc0	k0, CP0_BADVADDR
	li	k1, -4					# Is this ...
	and	k0, k1					# ... really needed?
	mtc0	zero, CP0_TAGLO
	cache	Index_Store_Tag_D,(k0)
	cache	Hit_Writeback_Inv_SD,(k0)
	lui	k0, %hi(vced_count)
	lw	k1, %lo(vced_count)(k0)
	addiu	k1, 1
	sw	k1, %lo(vced_count)(k0)
	eret

handle_vcei:
	mfc0	k0, CP0_BADVADDR
	cache	Hit_Writeback_Inv_SD,(k0)		# also cleans pi
	lui	k0, %hi(vcei_count)
	lw	k1, %lo(vcei_count)(k0)
	addiu	k1, 1
	sw	k1, %lo(vcei_count)(k0)
	eret

	END(except_vec3_r4000)
	.set	at

	/* General exception vector for all other CPUs. */
	NESTED(except_vec3_generic, 0, sp)
	.set	noat
	mfc0	k1, CP0_CAUSE
	andi	k1, k1, 0x7c
	dsll	k1, k1, 1
	ld	k0, exception_handlers(k1)
	jr	k0
	 nop
	END(except_vec3_generic)
	.set	at

/*
 * Special interrupt vector for embedded MIPS.  This is a dedicated interrupt
 * vector which reduces interrupt processing overhead.  The jump instruction
 * will be inserted here at initialization time.  This handler may only be 8
 * bytes in size!
 */
NESTED(except_vec4, 0, sp)
1:	j	1b			/* Dummy, will be replaced */
	 nop
	END(except_vec4)

	__FINIT
