/* U3copy_in_user.S: UltraSparc-III optimized memcpy.
 *
 * Copyright (C) 1999, 2000, 2004 David S. Miller (davem@redhat.com)
 */

#include <asm/visasm.h>
#include <asm/asi.h>
#include <asm/dcu.h>
#include <asm/spitfire.h>

#define XCC xcc

#define EXNV(x,y,a,b)	\
98:	x,y;				\
	.section .fixup;		\
	.align 4;			\
99:	retl;				\
	 a, b, %o0;			\
	.section __ex_table;		\
	.align 4;			\
	.word 98b, 99b;			\
	.text;				\
	.align 4;
#define EXNV1(x,y,a,b)			\
98:	x,y;				\
	.section .fixup;		\
	.align 4;			\
99:	a, b, %o0;			\
	retl;				\
	 add %o0, 1, %o0;		\
	.section __ex_table;		\
	.align 4;			\
	.word 98b, 99b;			\
	.text;				\
	.align 4;
#define EXNV4(x,y,a,b)			\
98:	x,y;				\
	.section .fixup;		\
	.align 4;			\
99:	a, b, %o0;			\
	retl;				\
	 add %o0, 4, %o0;		\
	.section __ex_table;		\
	.align 4;			\
	.word 98b, 99b;			\
	.text;				\
	.align 4;
#define EXNV8(x,y,a,b)			\
98:	x,y;				\
	.section .fixup;		\
	.align 4;			\
99:	a, b, %o0;			\
	retl;				\
	 add %o0, 8, %o0;		\
	.section __ex_table;		\
	.align 4;			\
	.word 98b, 99b;			\
	.text;				\
	.align 4;

	.register	%g2,#scratch
	.register	%g3,#scratch

	.text
	.align	32

	/* Don't try to get too fancy here, just nice and
	 * simple.  This is predominantly used for well aligned
	 * small copies in the compat layer.  It is also used
	 * to copy register windows around during thread cloning.
	 */

	.globl	U3copy_in_user
U3copy_in_user:	/* %o0=dst, %o1=src, %o2=len */
	/* Writing to %asi is _expensive_ so we hardcode it.
	 * Reading %asi to check for KERNEL_DS is comparatively
	 * cheap.
	 */
	rd		%asi, %g1
	cmp		%g1, ASI_AIUS
	bne,pn		%icc, U3memcpy_user_stub
	 nop

	cmp		%o2, 0
	be,pn		%XCC, out
	 or		%o0, %o1, %o3
	cmp		%o2, 16
	bleu,a,pn	%XCC, small_copy
	 or		%o3, %o2, %o3

medium_copy: /* 16 < len <= 64 */
	andcc		%o3, 0x7, %g0
	bne,pn		%XCC, small_copy_unaligned
	 sub		%o0, %o1, %o3

medium_copy_aligned:
	andn		%o2, 0x7, %o4
	and		%o2, 0x7, %o2
1:	subcc		%o4, 0x8, %o4
	EXNV8(ldxa [%o1] %asi, %o5, add %o4, %o2)
	EXNV8(stxa %o5, [%o1 + %o3] ASI_AIUS, add %o4, %o2)
	bgu,pt		%XCC, 1b
	 add		%o1, 0x8, %o1
	andcc		%o2, 0x4, %g0
	be,pt		%XCC, 1f
	 nop
	sub		%o2, 0x4, %o2
	EXNV4(lduwa [%o1] %asi, %o5, add %o4, %o2)
	EXNV4(stwa %o5, [%o1 + %o3] ASI_AIUS, add %o4, %o2)
	add		%o1, 0x4, %o1
1:	cmp		%o2, 0
	be,pt		%XCC, out
	 nop
	ba,pt		%xcc, small_copy_unaligned
	 nop

small_copy: /* 0 < len <= 16 */
	andcc		%o3, 0x3, %g0
	bne,pn		%XCC, small_copy_unaligned
	 sub		%o0, %o1, %o3

small_copy_aligned:
	subcc		%o2, 4, %o2
	EXNV4(lduwa [%o1] %asi, %g1, add %o2, %g0)
	EXNV4(stwa %g1, [%o1 + %o3] ASI_AIUS, add %o2, %g0)
	bgu,pt		%XCC, small_copy_aligned
	 add		%o1, 4, %o1

out:	retl
	 clr		%o0

	.align	32
small_copy_unaligned:
	subcc		%o2, 1, %o2
	EXNV1(lduba [%o1] %asi, %g1, add %o2, %g0)
	EXNV1(stba %g1, [%o1 + %o3] ASI_AIUS, add %o2, %g0)
	bgu,pt		%XCC, small_copy_unaligned
	 add		%o1, 1, %o1
	retl
	 clr		%o0
