/* U3copy_from_user.S: UltraSparc-III optimized copy from userspace.
 *
 * Copyright (C) 1999, 2000, 2004 David S. Miller (davem@redhat.com)
 */

#include <asm/visasm.h>
#include <asm/asi.h>
#include <asm/dcu.h>
#include <asm/spitfire.h>

#define XCC xcc

#define EXNV_RAW(x,y,a,b)		\
98:	x,y;				\
	.section .fixup;		\
	.align 4;			\
99:	ba U3cfu_fixup;			\
	 a, b, %o1;			\
	.section __ex_table;		\
	.align 4;			\
	.word 98b, 99b;			\
	.text;				\
	.align 4;
#define EXNV(x,y,a,b)			\
98:	x,y;				\
	.section .fixup;		\
	.align 4;			\
99:	add %o1, %o3, %o0;		\
	ba U3cfu_fixup;			\
	 a, b, %o1;			\
	.section __ex_table;		\
	.align 4;			\
	.word 98b, 99b;			\
	.text;				\
	.align 4;
#define EXNV4(x,y,a,b)			\
98:	x,y;				\
	.section .fixup;		\
	.align 4;			\
99:	add %o1, %o3, %o0;		\
	a, b, %o1;			\
	ba U3cfu_fixup;			\
	 add %o1, 4, %o1;		\
	.section __ex_table;		\
	.align 4;			\
	.word 98b, 99b;			\
	.text;				\
	.align 4;
#define EXNV8(x,y,a,b)			\
98:	x,y;				\
	.section .fixup;		\
	.align 4;			\
99:	add %o1, %o3, %o0;		\
	a, b, %o1;			\
	ba U3cfu_fixup;			\
	 add %o1, 8, %o1;		\
	.section __ex_table;		\
	.align 4;			\
	.word 98b, 99b;			\
	.text;				\
	.align 4;
#define EX(x,y,a,b)			\
98:	x,y;				\
	.section .fixup;		\
	.align 4;			\
99:	VISExitHalf;			\
	ba U3cfu_fixup;			\
	 a, b, %o1;			\
	.section __ex_table;		\
	.align 4;			\
	.word 98b, 99b;			\
	.text;				\
	.align 4;
#define EX2(x,y)			\
98:	x,y;				\
	.section .fixup;		\
	.align 4;			\
99:	VISExitHalf;			\
	and %o2, (0x40 - 1), %o1;	\
	add %o1, %o4, %o1;		\
	ba U3cfu_fixup;			\
	 add %o1, 0x1c0, %o1;		\
	.section __ex_table;		\
	.align 4;			\
	.word 98b, 99b;			\
	.text;				\
	.align 4;
#define EX3(x,y)			\
98:	x,y;				\
	.section .fixup;		\
	.align 4;			\
99:	VISExitHalf;			\
	and %o2, (0x40 - 1), %o1;	\
	sll %g3, 6, %g3;		\
	add %o1, 0x80, %o1;		\
	ba U3cfu_fixup;			\
	 add %o1, %g3, %o1;		\
	.section __ex_table;		\
	.align 4;			\
	.word 98b, 99b;			\
	.text;				\
	.align 4;
#define EX4(x,y)			\
98:	x,y;				\
	.section .fixup;		\
	.align 4;			\
99:	VISExitHalf;			\
	and %o2, (0x40 - 1), %o1;	\
	add %o1, 0x40, %o1;		\
	ba U3cfu_fixup;			\
	 add %o1, %g3, %o1;		\
	.section __ex_table;		\
	.align 4;			\
	.word 98b, 99b;			\
	.text;				\
	.align 4;

	.register	%g2,#scratch
	.register	%g3,#scratch

	/* Special/non-trivial issues of this code:
	 *
	 * 1) %o5 is preserved from VISEntryHalf to VISExitHalf
	 * 2) Only low 32 FPU registers are used so that only the
	 *    lower half of the FPU register set is dirtied by this
	 *    code.  This is especially important in the kernel.
	 * 3) This code never prefetches cachelines past the end
	 *    of the source buffer.
	 */

	.text
	.align	32

	/* The cheetah's flexible spine, oversized liver, enlarged heart,
	 * slender muscular body, and claws make it the swiftest hunter
	 * in Africa and the fastest animal on land.  Can reach speeds
	 * of up to 2.4GB per second.
	 */

	.globl	U3copy_from_user
U3copy_from_user:	/* %o0=dst, %o1=src, %o2=len */
	cmp		%o2, 0
	be,pn		%XCC, 85f
	 or		%o0, %o1, %o3
	cmp		%o2, 16
	bleu,a,pn	%XCC, 80f
	 or		%o3, %o2, %o3

	cmp		%o2, 256
	blu,pt		%XCC, 70f
	 andcc		%o3, 0x7, %g0

	ba,pt		%xcc, 1f
	 andcc		%o0, 0x3f, %g2

	/* Here len >= 256 and condition codes reflect execution
	 * of "andcc %o0, 0x7, %g2", done by caller.
	 */
	.align		64
1:
	/* Is 'dst' already aligned on an 64-byte boundary? */
	be,pt		%XCC, 2f

	/* Compute abs((dst & 0x3f) - 0x40) into %g2.  This is the number
	 * of bytes to copy to make 'dst' 64-byte aligned.  We pre-
	 * subtract this from 'len'.
	 */
	 sub		%g2, 0x40, %g2
	sub		%g0, %g2, %g2
	sub		%o2, %g2, %o2

	/* Copy %g2 bytes from src to dst, one byte at a time. */
1:	EXNV_RAW(lduba [%o1 + 0x00] %asi, %o3, add %o2, %g2)
	add		%o1, 0x1, %o1
	add		%o0, 0x1, %o0
	subcc		%g2, 0x1, %g2

	bg,pt		%XCC, 1b
	 stb		%o3, [%o0 + -1]

2:	VISEntryHalf
	and		%o1, 0x7, %g1
	ba,pt		%xcc, 1f
	 alignaddr	%o1, %g0, %o1

	.align		64
1:
	membar		#StoreLoad | #StoreStore | #LoadStore
	prefetcha	[%o1 + 0x000] %asi, #one_read
	prefetcha	[%o1 + 0x040] %asi, #one_read
	andn		%o2, (0x40 - 1), %o4
	prefetcha	[%o1 + 0x080] %asi, #one_read
	prefetcha	[%o1 + 0x0c0] %asi, #one_read
	EX(ldda [%o1 + 0x000] %asi, %f0, add %o2, %g0)
	prefetcha	[%o1 + 0x100] %asi, #one_read
	EX(ldda [%o1 + 0x008] %asi, %f2, add %o2, %g0)
	prefetcha	[%o1 + 0x140] %asi, #one_read
	EX(ldda [%o1 + 0x010] %asi, %f4, add %o2, %g0)
	prefetcha	[%o1 + 0x180] %asi, #one_read
	faligndata	%f0, %f2, %f16
	EX(ldda [%o1 + 0x018] %asi, %f6, add %o2, %g0)
	faligndata	%f2, %f4, %f18
	EX(ldda [%o1 + 0x020] %asi, %f8, add %o2, %g0)
	faligndata	%f4, %f6, %f20
	EX(ldda [%o1 + 0x028] %asi, %f10, add %o2, %g0)
	faligndata	%f6, %f8, %f22

	EX(ldda [%o1 + 0x030] %asi, %f12, add %o2, %g0)
	faligndata	%f8, %f10, %f24
	EX(ldda [%o1 + 0x038] %asi, %f14, add %o2, %g0)
	faligndata	%f10, %f12, %f26
	EX(ldda [%o1 + 0x040] %asi, %f0, add %o2, %g0)

	sub		%o4, 0x80, %o4
	add		%o1, 0x40, %o1
	ba,pt		%xcc, 1f
	 srl		%o4, 6, %o3

	.align		64
1:
	EX3(ldda [%o1 + 0x008] %asi, %f2)
	faligndata	%f12, %f14, %f28
	EX3(ldda [%o1 + 0x010] %asi, %f4)
	faligndata	%f14, %f0, %f30
	stda		%f16, [%o0] ASI_BLK_P
	EX3(ldda [%o1 + 0x018] %asi, %f6)
	faligndata	%f0, %f2, %f16

	EX3(ldda [%o1 + 0x020] %asi, %f8)
	faligndata	%f2, %f4, %f18
	EX3(ldda [%o1 + 0x028] %asi, %f10)
	faligndata	%f4, %f6, %f20
	EX3(ldda [%o1 + 0x030] %asi, %f12)
	faligndata	%f6, %f8, %f22
	EX3(ldda [%o1 + 0x038] %asi, %f14)
	faligndata	%f8, %f10, %f24

	EX3(ldda [%o1 + 0x040] %asi, %f0)
	prefetcha	[%o1 + 0x180] %asi, #one_read
	faligndata	%f10, %f12, %f26
	subcc		%o3, 0x01, %o3
	add		%o1, 0x40, %o1
	bg,pt		%XCC, 1b
	 add		%o0, 0x40, %o0

	/* Finally we copy the last full 64-byte block. */
	EX3(ldda [%o1 + 0x008] %asi, %f2)
	faligndata	%f12, %f14, %f28
	EX3(ldda [%o1 + 0x010] %asi, %f4)
	faligndata	%f14, %f0, %f30
	stda		%f16, [%o0] ASI_BLK_P
	EX3(ldda [%o1 + 0x018] %asi, %f6)
	faligndata	%f0, %f2, %f16
	EX3(ldda [%o1 + 0x020] %asi, %f8)
	faligndata	%f2, %f4, %f18
	EX3(ldda [%o1 + 0x028] %asi, %f10)
	faligndata	%f4, %f6, %f20
	EX3(ldda [%o1 + 0x030] %asi, %f12)
	faligndata	%f6, %f8, %f22
	EX3(ldda [%o1 + 0x038] %asi, %f14)
	faligndata	%f8, %f10, %f24
	cmp		%g1, 0
	be,pt		%XCC, 1f
	 add		%o0, 0x40, %o0
	EX4(ldda [%o1 + 0x040] %asi, %f0)
1:	faligndata	%f10, %f12, %f26
	faligndata	%f12, %f14, %f28
	faligndata	%f14, %f0, %f30
	stda		%f16, [%o0] ASI_BLK_P
	add		%o0, 0x40, %o0
	add		%o1, 0x40, %o1

	membar		#Sync

	/* Now we copy the (len modulo 64) bytes at the end.
	 * Note how we borrow the %f0 loaded above.
	 *
	 * Also notice how this code is careful not to perform a
	 * load past the end of the src buffer.
	 */
	and		%o2, 0x3f, %o2
	andcc		%o2, 0x38, %g2
	be,pn		%XCC, 10f
	 subcc		%g2, 0x8, %g2
	be,pn		%XCC, 10f
	 cmp		%g1, 0

	be,a,pt		%XCC, 1f
	 EX(ldda [%o1 + 0x00] %asi, %f0, add %o2, %g0)

1:	EX(ldda [%o1 + 0x08] %asi, %f2, add %o2, %g0)
	add		%o1, 0x8, %o1
	sub		%o2, 0x8, %o2
	subcc		%g2, 0x8, %g2
	faligndata	%f0, %f2, %f8
	std		%f8, [%o0 + 0x00]
	be,pn		%XCC, 10f
	 add		%o0, 0x8, %o0
	EX(ldda [%o1 + 0x08] %asi, %f0, add %o2, %g0)
	add		%o1, 0x8, %o1
	sub		%o2, 0x8, %o2
	subcc		%g2, 0x8, %g2
	faligndata	%f2, %f0, %f8
	std		%f8, [%o0 + 0x00]
	bne,pn		%XCC, 1b
	 add		%o0, 0x8, %o0

	/* If anything is left, we copy it one byte at a time.
	 * Note that %g1 is (src & 0x3) saved above before the
	 * alignaddr was performed.
	 */
10:
	cmp		%o2, 0
	add		%o1, %g1, %o1
	VISExitHalf
	be,pn		%XCC, 85f
	 sub		%o0, %o1, %o3

	andcc		%g1, 0x7, %g0
	bne,pn		%icc, 90f
	 andcc		%o2, 0x8, %g0
	be,pt		%icc, 1f
	 nop
	EXNV(ldxa [%o1] %asi, %o5, add %o2, %g0)
	stx		%o5, [%o1 + %o3]
	add		%o1, 0x8, %o1

1:	andcc		%o2, 0x4, %g0
	be,pt		%icc, 1f
	 nop
	EXNV(lduwa [%o1] %asi, %o5, and %o2, 0x7)
	stw		%o5, [%o1 + %o3]
	add		%o1, 0x4, %o1

1:	andcc		%o2, 0x2, %g0
	be,pt		%icc, 1f
	 nop
	EXNV(lduha [%o1] %asi, %o5, and %o2, 0x3)
	sth		%o5, [%o1 + %o3]
	add		%o1, 0x2, %o1

1:	andcc		%o2, 0x1, %g0
	be,pt		%icc, 85f
	 nop
	EXNV(lduba [%o1] %asi, %o5, and %o2, 0x1)
	ba,pt		%xcc, 85f
	 stb		%o5, [%o1 + %o3]

70: /* 16 < len <= 64 */
	bne,pn		%XCC, 90f
	 sub		%o0, %o1, %o3

	andn		%o2, 0x7, %o4
	and		%o2, 0x7, %o2
1:	subcc		%o4, 0x8, %o4
	EXNV8(ldxa [%o1] %asi, %o5, add %o2, %o4)
	stx		%o5, [%o1 + %o3]
	bgu,pt		%XCC, 1b
	 add		%o1, 0x8, %o1
	andcc		%o2, 0x4, %g0
	be,pt		%XCC, 1f
	 nop
	sub		%o2, 0x4, %o2
	EXNV4(lduwa [%o1] %asi, %o5, add %o2, %g0)
	stw		%o5, [%o1 + %o3]
	add		%o1, 0x4, %o1
1:	cmp		%o2, 0
	be,pt		%XCC, 85f
	 nop
	ba,pt		%xcc, 90f
	 nop

80: /* 0 < len <= 16 */
	andcc		%o3, 0x3, %g0
	bne,pn		%XCC, 90f
	 sub		%o0, %o1, %o3

1:
	subcc		%o2, 4, %o2
	EXNV(lduwa [%o1] %asi, %g1, add %o2, %g0)
	stw		%g1, [%o1 + %o3]
	bgu,pt		%XCC, 1b
	 add		%o1, 4, %o1

85:	retl
	 clr		%o0

	.align	32
90:
	subcc		%o2, 1, %o2
	EXNV(lduba [%o1] %asi, %g1, add %o2, %g0)
	stb		%g1, [%o1 + %o3]
	bgu,pt		%XCC, 90b
	 add		%o1, 1, %o1
	retl
	 clr		%o0

U3cfu_fixup:
	/* Since this is copy_from_user(), zero out the rest of the
	 * kernel buffer.
	 */
	cmp		%o1, 0
	ble,pn		%icc, 2f
	 mov		%o1, %g2

1:	subcc		%g2, 1, %g2
	stb		%g0, [%o0]
	bne,pt		%icc, 1b
	 add		%o0, 1, %o0

2:	retl
	 mov		%o1, %o0
