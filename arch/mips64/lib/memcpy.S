/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Unified implementation of memcpy, memmove and the __copy_user backend.
 *
 * Copyright (C) 1998, 1999, 2000 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 *
 * For __rmemcpy and memmove an exception is always a kernel bug, therefore
 * they're not protected.  In order to keep the exception fixup routine
 * simple all memory accesses in __copy_user to src rsp. dst are stricly
 * incremental.  The fixup routine depends on $at not being changed.
 */
#include <asm/asm.h>
#include <asm/offset.h>
#include <asm/regdef.h>

/*
 * The fixup routine for copy_to_user depends on copying strictly in
 * increasing order.  Gas expands the ulw/usw macros in the wrong order for
 * little endian machines, so we cannot depend on them.
 */
#ifdef __MIPSEB__
#define uswL	swl
#define uswU	swr
#define ulwL	lwl
#define ulwU	lwr
#define usdL	sdl
#define usdU	sdr
#define uldL	ldl
#define uldU	ldr
#endif
#ifdef __MIPSEL__
#define uswL	swr
#define uswU	swl
#define ulwL	lwr
#define ulwU	lwl
#define usdL	sdr
#define usdU	sdl
#define uldL	ldr
#define uldU	ldl
#endif

#define EX(insn,reg,addr,handler)			\
9:	insn	reg, addr;				\
	.section __ex_table,"a"; 			\
	PTR	9b, handler; 				\
	.previous

#define UEX(insn,reg,addr,handler)			\
9:	insn ## L reg, addr;				\
10:	insn ## U reg, 3 + addr;			\
	.section __ex_table,"a"; 			\
	PTR	9b, handler; 				\
	PTR	10b, handler; 				\
	.previous

#define UEXD(insn,reg,addr,handler)			\
9:	insn ## L reg, addr;				\
10:	insn ## U reg, 7 + addr;			\
	.section __ex_table,"a"; 			\
	PTR	9b, handler; 				\
	PTR	10b, handler; 				\
	.previous

/* ascending order, destination aligned  */
#define MOVE_BIGGERCHUNK(src, dst, offset, t0, t1, t2, t3) \
	EX(ld, t0, (offset + 0x00)(src), l_fixup); \
	EX(ld, t1, (offset + 0x08)(src), l_fixup); \
	EX(ld, t2, (offset + 0x10)(src), l_fixup); \
	EX(ld, t3, (offset + 0x18)(src), l_fixup); \
	EX(sd, t0, (offset + 0x00)(dst), s_fixup); \
	EX(sd, t1, (offset + 0x08)(dst), s_fixup); \
	EX(sd, t2, (offset + 0x10)(dst), s_fixup); \
	EX(sd, t3, (offset + 0x18)(dst), s_fixup); \
	EX(ld, t0, (offset + 0x20)(src), l_fixup); \
	EX(ld, t1, (offset + 0x28)(src), l_fixup); \
	EX(ld, t2, (offset + 0x30)(src), l_fixup); \
	EX(ld, t3, (offset + 0x38)(src), l_fixup); \
	EX(sd, t0, (offset + 0x20)(dst), s_fixup); \
	EX(sd, t1, (offset + 0x28)(dst), s_fixup); \
	EX(sd, t2, (offset + 0x30)(dst), s_fixup); \
	EX(sd, t3, (offset + 0x38)(dst), s_fixup)

/* ascending order, destination aligned  */
#define MOVE_BIGCHUNK(src, dst, offset, t0, t1, t2, t3) \
	EX(lw, t0, (offset + 0x00)(src), l_fixup); \
	EX(lw, t1, (offset + 0x04)(src), l_fixup); \
	EX(lw, t2, (offset + 0x08)(src), l_fixup); \
	EX(lw, t3, (offset + 0x0c)(src), l_fixup); \
	EX(sw, t0, (offset + 0x00)(dst), s_fixup); \
	EX(sw, t1, (offset + 0x04)(dst), s_fixup); \
	EX(sw, t2, (offset + 0x08)(dst), s_fixup); \
	EX(sw, t3, (offset + 0x0c)(dst), s_fixup); \
	EX(lw, t0, (offset + 0x10)(src), l_fixup); \
	EX(lw, t1, (offset + 0x14)(src), l_fixup); \
	EX(lw, t2, (offset + 0x18)(src), l_fixup); \
	EX(lw, t3, (offset + 0x1c)(src), l_fixup); \
	EX(sw, t0, (offset + 0x10)(dst), s_fixup); \
	EX(sw, t1, (offset + 0x14)(dst), s_fixup); \
	EX(sw, t2, (offset + 0x18)(dst), s_fixup); \
	EX(sw, t3, (offset + 0x1c)(dst), s_fixup)

/* ascending order, destination unaligned  */
#define UMOVE_BIGGERCHUNK(src, dst, offset, t0, t1, t2, t3) \
	EX(ld, t0, (offset + 0x00)(src), l_fixup); \
	EX(ld, t1, (offset + 0x08)(src), l_fixup); \
	EX(ld, t2, (offset + 0x10)(src), l_fixup); \
	EX(ld, t3, (offset + 0x18)(src), l_fixup); \
	UEXD(usd, t0, (offset + 0x00)(dst), s_fixup); \
	UEXD(usd, t1, (offset + 0x08)(dst), s_fixup); \
	UEXD(usd, t2, (offset + 0x10)(dst), s_fixup); \
	UEXD(usd, t3, (offset + 0x18)(dst), s_fixup); \
	EX(ld, t0, (offset + 0x20)(src), l_fixup); \
	EX(ld, t1, (offset + 0x28)(src), l_fixup); \
	EX(ld, t2, (offset + 0x30)(src), l_fixup); \
	EX(ld, t3, (offset + 0x38)(src), l_fixup); \
	UEXD(usd, t0, (offset + 0x20)(dst), s_fixup); \
	UEXD(usd, t1, (offset + 0x28)(dst), s_fixup); \
	UEXD(usd, t2, (offset + 0x30)(dst), s_fixup); \
	UEXD(usd, t3, (offset + 0x38)(dst), s_fixup)

/* ascending order, destination unaligned  */
#define UMOVE_BIGCHUNK(src, dst, offset, t0, t1, t2, t3) \
	EX(lw, t0, (offset + 0x00)(src), l_fixup); \
	EX(lw, t1, (offset + 0x04)(src), l_fixup); \
	EX(lw, t2, (offset + 0x08)(src), l_fixup); \
	EX(lw, t3, (offset + 0x0c)(src), l_fixup); \
	UEX(usw, t0, (offset + 0x00)(dst), s_fixup); \
	UEX(usw, t1, (offset + 0x04)(dst), s_fixup); \
	UEX(usw, t2, (offset + 0x08)(dst), s_fixup); \
	UEX(usw, t3, (offset + 0x0c)(dst), s_fixup); \
	EX(lw, t0, (offset + 0x10)(src), l_fixup); \
	EX(lw, t1, (offset + 0x14)(src), l_fixup); \
	EX(lw, t2, (offset + 0x18)(src), l_fixup); \
	EX(lw, t3, (offset + 0x1c)(src), l_fixup); \
	UEX(usw, t0, (offset + 0x10)(dst), s_fixup); \
	UEX(usw, t1, (offset + 0x14)(dst), s_fixup); \
	UEX(usw, t2, (offset + 0x18)(dst), s_fixup); \
	UEX(usw, t3, (offset + 0x1c)(dst), s_fixup)

	.text
	.set	noreorder
	.set	noat

	.align	5
LEAF(memcpy)					/* a0=dst a1=src a2=len */
	move	v0, a0				/* return value */
__memcpy:
FEXPORT(__copy_user)
	xor	ta0, a0, a1
	andi	ta0, ta0, 0x7
	move	t3, a0
	beqz	ta0, can_align
	 sltiu	t8, a2, 0x8

	b	memcpy_u_src			# bad alignment
	 move	ta2, a2

can_align:
	bnez	t8, small_memcpy		# < 8 bytes to copy
	 move	ta2, a2

	beqz	a2, out
	 andi	t8, a1, 0x1

hword_align:
	beqz	t8, word_align
	 andi	t8, a1, 0x2

	EX(lb, ta0, (a1), l_fixup)
	dsubu	a2, a2, 0x1
	EX(sb, ta0, (a0), s_fixup)
	daddu	a1, a1, 0x1
	daddu	a0, a0, 0x1
	andi	t8, a1, 0x2

word_align:
	beqz	t8, dword_align
	 sltiu	t8, a2, 56

	EX(lh, ta0, (a1), l_fixup)
	dsubu	a2, a2, 0x2
	EX(sh, ta0, (a0), s_fixup)
	sltiu	t8, a2, 56
	daddu	a0, a0, 0x2
	daddu	a1, a1, 0x2

dword_align:
	bnez	t8, do_end_words
	 move	t8, a2

	andi	t8, a1, 0x4
	beqz	t8, qword_align
	 andi	t8, a1, 0x8

	EX(lw, ta0, 0x00(a1), l_fixup)
	dsubu	a2, a2, 0x4
	EX(sw, ta0, 0x00(a0), s_fixup)
	daddu	a1, a1, 0x4
	daddu	a0, a0, 0x4
	andi	t8, a1, 0x8

qword_align:
	beqz	t8, oword_align
	 andi	t8, a1, 0x10

	EX(lw, ta0, 0x00(a1), l_fixup)
	EX(lw, ta1, 0x04(a1), l_fixup)
	dsubu	a2, a2, 0x8
	EX(sw, ta0, 0x00(a0), s_fixup)
	EX(sw, ta1, 0x04(a0), s_fixup)
	daddu	a1, a1, 0x8
	andi	t8, a1, 0x10
	daddu	a0, a0, 0x8

oword_align:
	beqz	t8, begin_movement
	 srl	t8, a2, 0x7

	EX(lw, ta3, 0x00(a1), l_fixup)
	EX(lw, t0, 0x04(a1), l_fixup)
	EX(lw, ta0, 0x08(a1), l_fixup)
	EX(lw, ta1, 0x0c(a1), l_fixup)
	EX(sw, ta3, 0x00(a0), s_fixup)
	EX(sw, t0, 0x04(a0), s_fixup)
	EX(sw, ta0, 0x08(a0), s_fixup)
	EX(sw, ta1, 0x0c(a0), s_fixup)
	dsubu	a2, a2, 0x10
	daddu	a1, a1, 0x10
	srl	t8, a2, 0x7
	daddu	a0, a0, 0x10

begin_movement:
	beqz	t8, 0f
	 andi	ta2, a2, 0x40

move_128bytes:
	pref	0, 2*128(a0)
	pref	1, 2*128(a1)
	MOVE_BIGGERCHUNK(a1, a0, 0x00, ta0, ta1, ta3, t0)
	MOVE_BIGGERCHUNK(a1, a0, 0x40, ta0, ta1, ta3, t0)
	dsubu	t8, t8, 0x01
	daddu	a1, a1, 0x80
	bnez	t8, move_128bytes
	 daddu	a0, a0, 0x80

0:
	beqz	ta2, 1f
	 andi	ta2, a2, 0x20

move_64bytes:
	MOVE_BIGGERCHUNK(a1, a0, 0x00, ta0, ta1, ta3, t0)
	daddu	a1, a1, 0x40
	daddu	a0, a0, 0x40

1:
	beqz	ta2, do_end_words
	 andi	t8, a2, 0x1c

move_32bytes:
	MOVE_BIGCHUNK(a1, a0, 0x00, ta0, ta1, ta3, t0)
	andi	t8, a2, 0x1c
	daddu	a1, a1, 0x20
	daddu	a0, a0, 0x20

do_end_words:
	beqz	t8, maybe_end_cruft
	 srl	t8, t8, 0x2

end_words:
	EX(lw, ta0, (a1), l_fixup)
	dsubu	t8, t8, 0x1
	EX(sw, ta0, (a0), s_fixup)
	daddu	a1, a1, 0x4
	bnez	t8, end_words
	 daddu	a0, a0, 0x4

maybe_end_cruft:
	andi	ta2, a2, 0x3

small_memcpy:
	beqz	ta2, out
	 move	a2, ta2

end_bytes:
	EX(lb, ta0, (a1), l_fixup)
	dsubu	a2, a2, 0x1
	EX(sb, ta0, (a0), s_fixup)
	daddu	a1, a1, 0x1
	bnez	a2, end_bytes
	 daddu	a0, a0, 0x1

out:	jr	ra
	 move	a2, zero

/* ------------------------------------------------------------------------- */

/* Bad, bad.  At least try to align the source  */

memcpy_u_src:
	bnez	t8, small_memcpy		# < 8 bytes?
	 move	ta2, a2

	daddiu	ta0, a1, 7			# ta0: how much to align
	ori	ta0, 7
	xori	ta0, 7
	dsubu	ta0, a1

	UEXD(uld, ta1, 0(a1), l_fixup)		# dword alignment
	UEXD(usd, ta1, 0(a0), s_fixup)

	daddu	a1, ta0				# src
	daddu	a0, ta0				# dst
	dsubu	a2, ta0				# len

	sltiu	t8, a2, 56
	bnez	t8, u_do_end_words
	 andi	t8, a2, 0x3c

	andi	t8, a1, 8			# now qword aligned?

u_qword_align:
	beqz	t8, u_oword_align
	 andi	t8, a1, 0x10

	EX(ld, ta0, 0x00(a1), l_fixup)
	dsubu	a2, a2, 0x8
	UEXD(usd, ta0, 0x00(a0), s_fixup)
	daddu	a1, a1, 0x8
	andi	t8, a1, 0x10
	daddu	a0, a0, 0x8

u_oword_align:
	beqz	t8, u_begin_movement
	 srl	t8, a2, 0x7

	EX(lw, ta3, 0x08(a1), l_fixup)
	EX(lw, t0, 0x0c(a1), l_fixup)
	EX(lw, ta0, 0x00(a1), l_fixup)
	EX(lw, ta1, 0x04(a1), l_fixup)
	UEX(usw, ta3, 0x08(a0), s_fixup)
	UEX(usw, t0, 0x0c(a0), s_fixup)
	UEX(usw, ta0, 0x00(a0), s_fixup)
	UEX(usw, ta1, 0x04(a0), s_fixup)
	dsubu	a2, a2, 0x10
	daddu	a1, a1, 0x10
	srl	t8, a2, 0x7
	daddu	a0, a0, 0x10

u_begin_movement:
	beqz	t8, 0f
	 andi	ta2, a2, 0x40

u_move_128bytes:
	UMOVE_BIGGERCHUNK(a1, a0, 0x00, ta0, ta1, ta3, t0)
	UMOVE_BIGGERCHUNK(a1, a0, 0x40, ta0, ta1, ta3, t0)
	dsubu	t8, t8, 0x01
	daddu	a1, a1, 0x80
	bnez	t8, u_move_128bytes
	 daddu	a0, a0, 0x80

0:
	beqz	ta2, 1f
	 andi	ta2, a2, 0x20

u_move_64bytes:
	UMOVE_BIGGERCHUNK(a1, a0, 0x00, ta0, ta1, ta3, t0)
	daddu	a1, a1, 0x40
	daddu	a0, a0, 0x40

1:
	beqz	ta2, u_do_end_words
	 andi	t8, a2, 0x1c

u_move_32bytes:
	UMOVE_BIGCHUNK(a1, a0, 0x00, ta0, ta1, ta3, t0)
	andi	t8, a2, 0x1c
	daddu	a1, a1, 0x20
	daddu	a0, a0, 0x20

u_do_end_words:
	beqz	t8, u_maybe_end_cruft
	 srl	t8, t8, 0x2

u_end_words:
	EX(lw, ta0, 0x00(a1), l_fixup)
	dsubu	t8, t8, 0x1
	UEX(usw, ta0, 0x00(a0), s_fixup)
	daddu	a1, a1, 0x4
	bnez	t8, u_end_words
	 daddu	a0, a0, 0x4

u_maybe_end_cruft:
	andi	ta2, a2, 0x3

u_cannot_optimize:
	beqz	ta2, out
	 move	a2, ta2

u_end_bytes:
	EX(lb, ta0, (a1), l_fixup)
	dsubu	a2, a2, 0x1
	EX(sb, ta0, (a0), s_fixup)
	daddu	a1, a1, 0x1
	bnez	a2, u_end_bytes
	 daddu	a0, a0, 0x1

	jr	ra
	 move	a2, zero
	END(memcpy)

/* descending order, destination aligned  */
#define RMOVE_BIGCHUNK(src, dst, offset, t0, t1, t2, t3) \
	lw	t0, (offset + 0x10)(src); \
	lw	t1, (offset + 0x14)(src); \
	lw	t2, (offset + 0x18)(src); \
	lw	t3, (offset + 0x1c)(src); \
	sw	t0, (offset + 0x10)(dst); \
	sw	t1, (offset + 0x14)(dst); \
	sw	t2, (offset + 0x18)(dst); \
	sw	t3, (offset + 0x1c)(dst); \
	lw	t0, (offset + 0x00)(src); \
	lw	t1, (offset + 0x04)(src); \
	lw	t2, (offset + 0x08)(src); \
	lw	t3, (offset + 0x0c)(src); \
	sw	t0, (offset + 0x00)(dst); \
	sw	t1, (offset + 0x04)(dst); \
	sw	t2, (offset + 0x08)(dst); \
	sw	t3, (offset + 0x0c)(dst)

/* descending order, destination ununaligned  */
#define RUMOVE_BIGCHUNK(src, dst, offset, t0, t1, t2, t3) \
	lw	t0, (offset + 0x10)(src); \
	lw	t1, (offset + 0x14)(src); \
	lw	t2, (offset + 0x18)(src); \
	lw	t3, (offset + 0x1c)(src); \
	usw	t0, (offset + 0x10)(dst); \
	usw	t1, (offset + 0x14)(dst); \
	usw	t2, (offset + 0x18)(dst); \
	usw	t3, (offset + 0x1c)(dst); \
	lw	t0, (offset + 0x00)(src); \
	lw	t1, (offset + 0x04)(src); \
	lw	t2, (offset + 0x08)(src); \
	lw	t3, (offset + 0x0c)(src); \
	usw	t0, (offset + 0x00)(dst); \
	usw	t1, (offset + 0x04)(dst); \
	usw	t2, (offset + 0x08)(dst); \
	usw	t3, (offset + 0x0c)(dst)

	.align	5
LEAF(memmove)
	sltu	ta0, a0, a1			# dst < src -> memcpy
	bnez	ta0, memcpy
	 daddu	v0, a0, a2
	sltu	ta0, v0, a1			# dst + len < src -> non-
	bnez	ta0, __memcpy			# overlapping, can use memcpy
	 move	v0, a0				/* return value */
	beqz	a2, r_out
	END(memmove)

LEAF(__rmemcpy)					/* a0=dst a1=src a2=len */
	daddu	a0, a2				# dst = dst + len
	daddu	a1, a2				# src = src + len

#if 0 /* Horror fix */
	xor	ta0, a0, a1
	andi	ta0, ta0, 0x3
	move	t3, a0
	beqz	ta0, r_can_align
	 sltiu	t8, a2, 0x8

	b	r_memcpy_u_src			# bad alignment
	 move	ta2, a2

r_can_align:
	bnez	t8, r_small_memcpy		# < 8 bytes to copy
	 move	ta2, a2

	beqz	a2, r_out
	 andi	t8, a1, 0x1

r_hword_align:
	beqz	t8, r_word_align
	 andi	t8, a1, 0x2

	lb	ta0, -1(a1)
	dsubu	a2, a2, 0x1
	sb	ta0, -1(a0)
	dsubu	a1, a1, 0x1
	dsubu	a0, a0, 0x1
	andi	t8, a1, 0x2

r_word_align:
	beqz	t8, r_dword_align
	 sltiu	t8, a2, 56
	
	lh	ta0, -2(a1)
	dsubu	a2, a2, 0x2
	sh	ta0, -2(a0)
	sltiu	t8, a2, 56
	dsubu	a0, a0, 0x2
	dsubu	a1, a1, 0x2

r_dword_align:
	bnez	t8, r_do_end_words
	 move	t8, a2

	andi	t8, a1, 0x4
	beqz	t8, r_qword_align
	 andi	t8, a1, 0x8

	lw	ta0, -4(a1)
	dsubu	a2, a2, 0x4
	sw	ta0, -4(a0)
	dsubu	a1, a1, 0x4
	dsubu	a0, a0, 0x4
	andi	t8, a1, 0x8

r_qword_align:
	beqz	t8, r_oword_align
	 andi	t8, a1, 0x10

	dsubu	a1, a1, 0x8
	lw	ta0, 0x04(a1)
	lw	ta1, 0x00(a1)
	dsubu	a0, a0, 0x8
	sw	ta0, 0x04(a0)
	sw	ta1, 0x00(a0)
	dsubu	a2, a2, 0x8

	andi	t8, a1, 0x10

r_oword_align:
	beqz	t8, r_begin_movement
	 srl	t8, a2, 0x7

	dsubu	a1, a1, 0x10
	lw	ta3, 0x08(a1)			# assumes subblock ordering
	lw	t0, 0x0c(a1)
	lw	ta0, 0x00(a1)
	lw	ta1, 0x04(a1)
	dsubu	a0, a0, 0x10
	sw	ta3, 0x08(a0)
	sw	t0, 0x0c(a0)
	sw	ta0, 0x00(a0)
	sw	ta1, 0x04(a0)
	dsubu	a2, a2, 0x10
	srl	t8, a2, 0x7

r_begin_movement:
	beqz	t8, 0f
	 andi	ta2, a2, 0x40

r_move_128bytes:
	RMOVE_BIGCHUNK(a1, a0, -0x80, ta0, ta1, ta3, t0)
	RMOVE_BIGCHUNK(a1, a0, -0x60, ta0, ta1, ta3, t0)
	RMOVE_BIGCHUNK(a1, a0, -0x40, ta0, ta1, ta3, t0)
	RMOVE_BIGCHUNK(a1, a0, -0x20, ta0, ta1, ta3, t0)
	dsubu	t8, t8, 0x01
	dsubu	a1, a1, 0x80
	bnez	t8, r_move_128bytes
	 dsubu	a0, a0, 0x80

0:
	beqz	ta2, 1f
	 andi	ta2, a2, 0x20

r_move_64bytes:
	dsubu	a1, a1, 0x40
	dsubu	a0, a0, 0x40
	RMOVE_BIGCHUNK(a1, a0, 0x20, ta0, ta1, ta3, t0)
	RMOVE_BIGCHUNK(a1, a0, 0x00, ta0, ta1, ta3, t0)

1:
	beqz	ta2, r_do_end_words
	 andi	t8, a2, 0x1c

r_move_32bytes:
	dsubu	a1, a1, 0x20
	dsubu	a0, a0, 0x20
	RMOVE_BIGCHUNK(a1, a0, 0x00, ta0, ta1, ta3, t0)
	andi	t8, a2, 0x1c

r_do_end_words:
	beqz	t8, r_maybe_end_cruft
	 srl	t8, t8, 0x2

r_end_words:
	lw	ta0, -4(a1)
	dsubu	t8, t8, 0x1
	sw	ta0, -4(a0)
	dsubu	a1, a1, 0x4
	bnez	t8, r_end_words
	 dsubu	a0, a0, 0x4

r_maybe_end_cruft:
	andi	ta2, a2, 0x3

r_small_memcpy:
	beqz	ta2, r_out
	 move	a2, ta2
#endif /* Horror fix */

r_end_bytes:
	lb	ta0, -1(a1)
	dsubu	a2, a2, 0x1
	sb	ta0, -1(a0)
	dsubu	a1, a1, 0x1
	bnez	a2, r_end_bytes
	 dsubu	a0, a0, 0x1

r_out:
	jr	ra
	 move	a2, zero

#if 0 /* Horror fix */
/* ------------------------------------------------------------------------- */

/* Bad, bad.  At least try to align the source  */

r_memcpy_u_src:
	bnez	t8, r_small_memcpy		# < 8 bytes?
	 move	ta2, a2

	andi	ta0, a1, 7			# ta0: how much to align

	ulw	ta1, -8(a1)			# dword alignment
	ulw	ta2, -4(a1)
	usw	ta1, -8(a0)
	usw	ta2, -4(a0)

	dsubu	a1, ta0				# src
	dsubu	a0, ta0				# dst
	dsubu	a2, ta0				# len

	sltiu	t8, a2, 56
	bnez	t8, ru_do_end_words
	 andi	t8, a2, 0x3c

	andi	t8, a1, 8			# now qword aligned?

ru_qword_align:
	beqz	t8, ru_oword_align
	 andi	t8, a1, 0x10

	dsubu	a1, a1, 0x8
	lw	ta0, 0x00(a1)
	lw	ta1, 0x04(a1)
	dsubu	a0, a0, 0x8
	usw	ta0, 0x00(a0)
	usw	ta1, 0x04(a0)
	dsubu	a2, a2, 0x8

	andi	t8, a1, 0x10

ru_oword_align:
	beqz	t8, ru_begin_movement
	 srl	t8, a2, 0x7

	dsubu	a1, a1, 0x10
	lw	ta3, 0x08(a1)			# assumes subblock ordering
	lw	t0, 0x0c(a1)
	lw	ta0, 0x00(a1)
	lw	ta1, 0x04(a1)
	dsubu	a0, a0, 0x10
	usw	ta3, 0x08(a0)
	usw	t0, 0x0c(a0)
	usw	ta0, 0x00(a0)
	usw	ta1, 0x04(a0)
	dsubu	a2, a2, 0x10

	srl	t8, a2, 0x7

ru_begin_movement:
	beqz	t8, 0f
	 andi	ta2, a2, 0x40

ru_move_128bytes:
	RUMOVE_BIGCHUNK(a1, a0, -0x80, ta0, ta1, ta3, t0)
	RUMOVE_BIGCHUNK(a1, a0, -0x60, ta0, ta1, ta3, t0)
	RUMOVE_BIGCHUNK(a1, a0, -0x40, ta0, ta1, ta3, t0)
	RUMOVE_BIGCHUNK(a1, a0, -0x20, ta0, ta1, ta3, t0)
	dsubu	t8, t8, 0x01
	dsubu	a1, a1, 0x80
	bnez	t8, ru_move_128bytes
	 dsubu	a0, a0, 0x80

0:
	beqz	ta2, 1f
	 andi	ta2, a2, 0x20

ru_move_64bytes:
	dsubu	a1, a1, 0x40
	dsubu	a0, a0, 0x40
	RUMOVE_BIGCHUNK(a1, a0, 0x20, ta0, ta1, ta3, t0)
	RUMOVE_BIGCHUNK(a1, a0, 0x00, ta0, ta1, ta3, t0)

1:
	beqz	ta2, ru_do_end_words
	 andi	t8, a2, 0x1c

ru_move_32bytes:
	dsubu	a1, a1, 0x20
	dsubu	a0, a0, 0x20
	RUMOVE_BIGCHUNK(a1, a0, 0x00, ta0, ta1, ta3, t0)
	andi	t8, a2, 0x1c

ru_do_end_words:
	beqz	t8, ru_maybe_end_cruft
	 srl	t8, t8, 0x2

ru_end_words:
	lw	ta0, -4(a1)
	usw	ta0, -4(a0)
	dsubu	t8, t8, 0x1
	dsubu	a1, a1, 0x4
	bnez	t8, ru_end_words
	 dsubu	a0, a0, 0x4

ru_maybe_end_cruft:
	andi	ta2, a2, 0x3

ru_cannot_optimize:
	beqz	ta2, r_out
	 move	a2, ta2

ru_end_bytes:
	lb	ta0, -1(a1)
	dsubu	a2, a2, 0x1
	sb	ta0, -1(a0)
	dsubu	a1, a1, 0x1
	bnez	a2, ru_end_bytes
	 dsubu	a0, a0, 0x1

	jr	ra
	 move	a2, zero
#endif /* Horror fix */
	END(__rmemcpy)

l_fixup:					# clear the rest of the buffer
	ld	ta0, THREAD_BUADDR($28)
	 nop
	dsubu	a2, AT, ta0			# a2 bytes to go
	daddu	a0, ta0				# compute start address in a1
	dsubu	a0, a1
	j	__bzero
	 move	a1, zero

s_fixup:
	jr	ra
	 nop
