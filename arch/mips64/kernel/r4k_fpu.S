/* $Id: r4k_fpu.S,v 1.1 1999/09/27 16:01:38 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Save/restore floating point context for signal handlers.
 *
 * Copyright (C) 1996, 1998, 1999 by Ralf Baechle
 *
 * Multi-arch abstraction and asm macros for easier reading:
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#include <asm/asm.h>
#include <asm/errno.h>
#include <asm/fpregdef.h>
#include <asm/mipsregs.h>
#include <asm/offset.h>
#include <asm/regdef.h>

	.macro	EX insn, reg, src
	.set	push
	.set	nomacro
.ex\@:	\insn	\reg, \src
	.set	pop
	.section __ex_table,"a"
	PTR	.ex\@, fault
	.previous
	.endm

	.set	noreorder
	/* Save floating point context */
LEAF(save_fp_context)
	mfc0	t1,CP0_STATUS
	 sll	t2,t1,5

	bgez	t2,1f
	 cfc1	t1,fcr31
	/* Store the 16 odd double precision registers */
	EX	sdc1 $f1, SC_FPREGS+8(a0)
	EX	sdc1 $f3, SC_FPREGS+24(a0)
	EX	sdc1 $f5, SC_FPREGS+40(a0)
	EX	sdc1 $f7, SC_FPREGS+56(a0)
	EX	sdc1 $f9, SC_FPREGS+72(a0)
	EX	sdc1 $f11, SC_FPREGS+88(a0)
	EX	sdc1 $f13, SC_FPREGS+104(a0)
	EX	sdc1 $f15, SC_FPREGS+120(a0)
	EX	sdc1 $f17, SC_FPREGS+136(a0)
	EX	sdc1 $f19, SC_FPREGS+152(a0)
	EX	sdc1 $f21, SC_FPREGS+168(a0)
	EX	sdc1 $f23, SC_FPREGS+184(a0)
	EX	sdc1 $f25, SC_FPREGS+200(a0)
	EX	sdc1 $f27, SC_FPREGS+216(a0)
	EX	sdc1 $f29, SC_FPREGS+232(a0)
	EX	sdc1 $f31, SC_FPREGS+248(a0)

	/* Store the 16 even double precision registers */
1:
	EX	sdc1 $f0, SC_FPREGS+0(a0)
	EX	sdc1 $f2, SC_FPREGS+16(a0)
	EX	sdc1 $f4, SC_FPREGS+32(a0)
	EX	sdc1 $f6, SC_FPREGS+48(a0)
	EX	sdc1 $f8, SC_FPREGS+64(a0)
	EX	sdc1 $f10, SC_FPREGS+80(a0)
	EX	sdc1 $f12, SC_FPREGS+96(a0)
	EX	sdc1 $f14, SC_FPREGS+112(a0)
	EX	sdc1 $f16, SC_FPREGS+128(a0)
	EX	sdc1 $f18, SC_FPREGS+144(a0)
	EX	sdc1 $f20, SC_FPREGS+160(a0)
	EX	sdc1 $f22, SC_FPREGS+176(a0)
	EX	sdc1 $f24, SC_FPREGS+192(a0)
	EX	sdc1 $f26, SC_FPREGS+208(a0)
	EX	sdc1 $f28, SC_FPREGS+224(a0)
	EX	sdc1 $f30, SC_FPREGS+240(a0)
	EX	sw t1, SC_FPC_CSR(a0)
	cfc1	t0,$0				# implementation/version
	EX	sw t0,SC_FPC_EIR(a0)

	jr	ra
	 li	v0, 0					# success
	END(save_fp_context)

/*
 * Restore FPU state:
 *  - fp gp registers
 *  - cp1 status/control register
 *
 * We base the decision which registers to restore from the signal stack
 * frame on the current content of c0_status, not on the content of the
 * stack frame which might have been changed by the user.
 */
LEAF(restore_fp_context)
	mfc0	t1, CP0_STATUS
	sll	t0,t1,5
	bgez	t0,1f
	 EX	lw t0, SC_FPC_CSR(a0)

	/* Restore the 16 odd double precision registers only
	 * when enabled in the cp0 status register.
	 */
	EX	ldc1 $f1, SC_FPREGS+8(a0)
	EX	ldc1 $f3, SC_FPREGS+24(a0)
	EX	ldc1 $f5, SC_FPREGS+40(a0)
	EX	ldc1 $f7, SC_FPREGS+56(a0)
	EX	ldc1 $f9, SC_FPREGS+72(a0)
	EX	ldc1 $f11, SC_FPREGS+88(a0)
	EX	ldc1 $f13, SC_FPREGS+104(a0)
	EX	ldc1 $f15, SC_FPREGS+120(a0)
	EX	ldc1 $f17, SC_FPREGS+136(a0)
	EX	ldc1 $f19, SC_FPREGS+152(a0)
	EX	ldc1 $f21, SC_FPREGS+168(a0)
	EX	ldc1 $f23, SC_FPREGS+184(a0)
	EX	ldc1 $f25, SC_FPREGS+200(a0)
	EX	ldc1 $f27, SC_FPREGS+216(a0)
	EX	ldc1 $f29, SC_FPREGS+232(a0)
	EX	ldc1 $f31, SC_FPREGS+248(a0)

	/*
	 * Restore the 16 even double precision registers
	 * when cp1 was enabled in the cp0 status register.
	 */
1:	EX	ldc1 $f0, SC_FPREGS+0(a0)
	EX	ldc1 $f2, SC_FPREGS+16(a0)
	EX	ldc1 $f4, SC_FPREGS+32(a0)
	EX	ldc1 $f6, SC_FPREGS+48(a0)
	EX	ldc1 $f8, SC_FPREGS+64(a0)
	EX	ldc1 $f10, SC_FPREGS+80(a0)
	EX	ldc1 $f12, SC_FPREGS+96(a0)
	EX	ldc1 $f14, SC_FPREGS+112(a0)
	EX	ldc1 $f16, SC_FPREGS+128(a0)
	EX	ldc1 $f18, SC_FPREGS+144(a0)
	EX	ldc1 $f20, SC_FPREGS+160(a0)
	EX	ldc1 $f22, SC_FPREGS+176(a0)
	EX	ldc1 $f24, SC_FPREGS+192(a0)
	EX	ldc1 $f26, SC_FPREGS+208(a0)
	EX	ldc1 $f28, SC_FPREGS+224(a0)
	EX	ldc1 $f30, SC_FPREGS+240(a0)
	ctc1	t0,fcr31
	jr	ra
	 li	v0, 0					# success
	END(restore_fp_context)

	.type	fault@function
	.ent	fault
fault:	li	v0, -EFAULT
	jr	ra
	.end	fault
