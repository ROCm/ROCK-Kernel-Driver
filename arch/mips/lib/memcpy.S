/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Unified implementation of memcpy, memmove and the __copy_user backend.
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
#endif
#ifdef __MIPSEL__
#define uswL	swr
#define uswU	swl
#define ulwL	lwr
#define ulwU	lwl
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
EXPORT(__copy_user)
	xor	t0, a0, a1
	andi	t0, t0, 0x3
	move	t7, a0
	beqz	t0, can_align
	 sltiu	t8, a2, 0x8

	b	memcpy_u_src			# bad alignment
	 move	t2, a2

can_align:
	bnez	t8, small_memcpy		# < 8 bytes to copy
	 move	t2, a2

	beqz	a2, out
	 andi	t8, a1, 0x1

hword_align:
	beqz	t8, word_align
	 andi	t8, a1, 0x2

	EX(lb, t0, (a1), l_fixup)
	subu	a2, a2, 0x1
	EX(sb, t0, (a0), s_fixup)
	addu	a1, a1, 0x1
	addu	a0, a0, 0x1
	andi	t8, a1, 0x2

word_align:
	beqz	t8, dword_align
	 sltiu	t8, a2, 56
	
	EX(lh, t0, (a1), l_fixup)
	subu	a2, a2, 0x2
	EX(sh, t0, (a0), s_fixup)
	sltiu	t8, a2, 56
	addu	a0, a0, 0x2
	addu	a1, a1, 0x2

dword_align:
	bnez	t8, do_end_words
	 move	t8, a2

	andi	t8, a1, 0x4
	beqz	t8, qword_align
	 andi	t8, a1, 0x8

	EX(lw, t0, 0x00(a1), l_fixup)
	subu	a2, a2, 0x4
	EX(sw, t0, 0x00(a0), s_fixup)
	addu	a1, a1, 0x4
	addu	a0, a0, 0x4
	andi	t8, a1, 0x8

qword_align:
	beqz	t8, oword_align
	 andi	t8, a1, 0x10

	EX(lw, t0, 0x00(a1), l_fixup)
	EX(lw, t1, 0x04(a1), l_fixup)
	subu	a2, a2, 0x8
	EX(sw, t0, 0x00(a0), s_fixup)
	EX(sw, t1, 0x04(a0), s_fixup)
	addu	a1, a1, 0x8
	andi	t8, a1, 0x10
	addu	a0, a0, 0x8

oword_align:
	beqz	t8, begin_movement
	 srl	t8, a2, 0x7

	EX(lw, t3, 0x00(a1), l_fixup)
	EX(lw, t4, 0x04(a1), l_fixup)
	EX(lw, t0, 0x08(a1), l_fixup)
	EX(lw, t1, 0x0c(a1), l_fixup)
	EX(sw, t3, 0x00(a0), s_fixup)
	EX(sw, t4, 0x04(a0), s_fixup)
	EX(sw, t0, 0x08(a0), s_fixup)
	EX(sw, t1, 0x0c(a0), s_fixup)
	subu	a2, a2, 0x10
	addu	a1, a1, 0x10
	srl	t8, a2, 0x7
	addu	a0, a0, 0x10

begin_movement:
	beqz	t8, 0f
	 andi	t2, a2, 0x40

move_128bytes:
	MOVE_BIGCHUNK(a1, a0, 0x00, t0, t1, t3, t4)
	MOVE_BIGCHUNK(a1, a0, 0x20, t0, t1, t3, t4)
	MOVE_BIGCHUNK(a1, a0, 0x40, t0, t1, t3, t4)
	MOVE_BIGCHUNK(a1, a0, 0x60, t0, t1, t3, t4)
	subu	t8, t8, 0x01
	addu	a1, a1, 0x80
	bnez	t8, move_128bytes
	 addu	a0, a0, 0x80

0:
	beqz	t2, 1f
	 andi	t2, a2, 0x20

move_64bytes:
	MOVE_BIGCHUNK(a1, a0, 0x00, t0, t1, t3, t4)
	MOVE_BIGCHUNK(a1, a0, 0x20, t0, t1, t3, t4)
	addu	a1, a1, 0x40
	addu	a0, a0, 0x40

1:
	beqz	t2, do_end_words
	 andi	t8, a2, 0x1c

move_32bytes:
	MOVE_BIGCHUNK(a1, a0, 0x00, t0, t1, t3, t4)
	andi	t8, a2, 0x1c
	addu	a1, a1, 0x20
	addu	a0, a0, 0x20

do_end_words:
	beqz	t8, maybe_end_cruft
	 srl	t8, t8, 0x2

end_words:
	EX(lw, t0, (a1), l_fixup)
	subu	t8, t8, 0x1
	EX(sw, t0, (a0), s_fixup)
	addu	a1, a1, 0x4
	bnez	t8, end_words
	 addu	a0, a0, 0x4

maybe_end_cruft:
	andi	t2, a2, 0x3

small_memcpy:
	beqz	t2, out
	 move	a2, t2

end_bytes:
	EX(lb, t0, (a1), l_fixup)
	subu	a2, a2, 0x1
	EX(sb, t0, (a0), s_fixup)
	addu	a1, a1, 0x1
	bnez	a2, end_bytes
	 addu	a0, a0, 0x1

out:	jr	ra
	 move	a2, zero

/* ------------------------------------------------------------------------- */

/* Bad, bad.  At least try to align the source  */

memcpy_u_src:
	bnez	t8, small_memcpy		# < 8 bytes?
	 move	t2, a2

	addiu	t0, a1, 7			# t0: how much to align
	ori	t0, 7
	xori	t0, 7
	subu	t0, a1

	UEX(ulw, t1, 0(a1), l_fixup)		# dword alignment
	UEX(ulw, t2, 4(a1), l_fixup)
	UEX(usw, t1, 0(a0), s_fixup)
	UEX(usw, t2, 4(a0), s_fixup)

	addu	a1, t0				# src
	addu	a0, t0				# dst
	subu	a2, t0				# len

	sltiu	t8, a2, 56
	bnez	t8, u_do_end_words
	 andi	t8, a2, 0x3c

	andi	t8, a1, 8			# now qword aligned?

u_qword_align:
	beqz	t8, u_oword_align
	 andi	t8, a1, 0x10

	EX(lw, t0, 0x00(a1), l_fixup)
	EX(lw, t1, 0x04(a1), l_fixup)
	subu	a2, a2, 0x8
	UEX(usw, t0, 0x00(a0), s_fixup)
	UEX(usw, t1, 0x04(a0), s_fixup)
	addu	a1, a1, 0x8
	andi	t8, a1, 0x10
	addu	a0, a0, 0x8

u_oword_align:
	beqz	t8, u_begin_movement
	 srl	t8, a2, 0x7

	EX(lw, t3, 0x08(a1), l_fixup)
	EX(lw, t4, 0x0c(a1), l_fixup)
	EX(lw, t0, 0x00(a1), l_fixup)
	EX(lw, t1, 0x04(a1), l_fixup)
	UEX(usw, t3, 0x08(a0), s_fixup)
	UEX(usw, t4, 0x0c(a0), s_fixup)
	UEX(usw, t0, 0x00(a0), s_fixup)
	UEX(usw, t1, 0x04(a0), s_fixup)
	subu	a2, a2, 0x10
	addu	a1, a1, 0x10
	srl	t8, a2, 0x7
	addu	a0, a0, 0x10

u_begin_movement:
	beqz	t8, 0f
	 andi	t2, a2, 0x40

u_move_128bytes:
	UMOVE_BIGCHUNK(a1, a0, 0x00, t0, t1, t3, t4)
	UMOVE_BIGCHUNK(a1, a0, 0x20, t0, t1, t3, t4)
	UMOVE_BIGCHUNK(a1, a0, 0x40, t0, t1, t3, t4)
	UMOVE_BIGCHUNK(a1, a0, 0x60, t0, t1, t3, t4)
	subu	t8, t8, 0x01
	addu	a1, a1, 0x80
	bnez	t8, u_move_128bytes
	 addu	a0, a0, 0x80

0:
	beqz	t2, 1f
	 andi	t2, a2, 0x20

u_move_64bytes:
	UMOVE_BIGCHUNK(a1, a0, 0x00, t0, t1, t3, t4)
	UMOVE_BIGCHUNK(a1, a0, 0x20, t0, t1, t3, t4)
	addu	a1, a1, 0x40
	addu	a0, a0, 0x40

1:
	beqz	t2, u_do_end_words
	 andi	t8, a2, 0x1c

u_move_32bytes:
	UMOVE_BIGCHUNK(a1, a0, 0x00, t0, t1, t3, t4)
	andi	t8, a2, 0x1c
	addu	a1, a1, 0x20
	addu	a0, a0, 0x20

u_do_end_words:
	beqz	t8, u_maybe_end_cruft
	 srl	t8, t8, 0x2

u_end_words:
	EX(lw, t0, 0x00(a1), l_fixup)
	subu	t8, t8, 0x1
	UEX(usw, t0, 0x00(a0), s_fixup)
	addu	a1, a1, 0x4
	bnez	t8, u_end_words
	 addu	a0, a0, 0x4

u_maybe_end_cruft:
	andi	t2, a2, 0x3

u_cannot_optimize:
	beqz	t2, out
	 move	a2, t2

u_end_bytes:
	EX(lb, t0, (a1), l_fixup)
	subu	a2, a2, 0x1
	EX(sb, t0, (a0), s_fixup)
	addu	a1, a1, 0x1
	bnez	a2, u_end_bytes
	 addu	a0, a0, 0x1

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
	sltu	t0, a0, a1			# dst < src -> memcpy
	bnez	t0, memcpy
	 addu	v0, a0, a2
	sltu	t0, v0, a1			# dst + len < src -> non-
	bnez	t0, __memcpy			# overlapping, can use memcpy
	 move	v0, a0				/* return value */
	beqz	a2, r_out
	END(memmove)

LEAF(__rmemcpy)					/* a0=dst a1=src a2=len */
	addu	a0, a2				# dst = dst + len
	addu	a1, a2				# src = src + len

#if 0 /* Horror fix */
	xor	t0, a0, a1
	andi	t0, t0, 0x3
	move	t7, a0
	beqz	t0, r_can_align
	 sltiu	t8, a2, 0x8

	b	r_memcpy_u_src			# bad alignment
	 move	t2, a2

r_can_align:
	bnez	t8, r_small_memcpy		# < 8 bytes to copy
	 move	t2, a2

	beqz	a2, r_out
	 andi	t8, a1, 0x1

r_hword_align:
	beqz	t8, r_word_align
	 andi	t8, a1, 0x2

	lb	t0, -1(a1)
	subu	a2, a2, 0x1
	sb	t0, -1(a0)
	subu	a1, a1, 0x1
	subu	a0, a0, 0x1
	andi	t8, a1, 0x2

r_word_align:
	beqz	t8, r_dword_align
	 sltiu	t8, a2, 56
	
	lh	t0, -2(a1)
	subu	a2, a2, 0x2
	sh	t0, -2(a0)
	sltiu	t8, a2, 56
	subu	a0, a0, 0x2
	subu	a1, a1, 0x2

r_dword_align:
	bnez	t8, r_do_end_words
	 move	t8, a2

	andi	t8, a1, 0x4
	beqz	t8, r_qword_align
	 andi	t8, a1, 0x8

	lw	t0, -4(a1)
	subu	a2, a2, 0x4
	sw	t0, -4(a0)
	subu	a1, a1, 0x4
	subu	a0, a0, 0x4
	andi	t8, a1, 0x8

r_qword_align:
	beqz	t8, r_oword_align
	 andi	t8, a1, 0x10

	subu	a1, a1, 0x8
	lw	t0, 0x04(a1)
	lw	t1, 0x00(a1)
	subu	a0, a0, 0x8
	sw	t0, 0x04(a0)
	sw	t1, 0x00(a0)
	subu	a2, a2, 0x8

	andi	t8, a1, 0x10

r_oword_align:
	beqz	t8, r_begin_movement
	 srl	t8, a2, 0x7

	subu	a1, a1, 0x10
	lw	t3, 0x08(a1)			# assumes subblock ordering
	lw	t4, 0x0c(a1)
	lw	t0, 0x00(a1)
	lw	t1, 0x04(a1)
	subu	a0, a0, 0x10
	sw	t3, 0x08(a0)
	sw	t4, 0x0c(a0)
	sw	t0, 0x00(a0)
	sw	t1, 0x04(a0)
	subu	a2, a2, 0x10
	srl	t8, a2, 0x7

r_begin_movement:
	beqz	t8, 0f
	 andi	t2, a2, 0x40

r_move_128bytes:
	RMOVE_BIGCHUNK(a1, a0, -0x80, t0, t1, t3, t4)
	RMOVE_BIGCHUNK(a1, a0, -0x60, t0, t1, t3, t4)
	RMOVE_BIGCHUNK(a1, a0, -0x40, t0, t1, t3, t4)
	RMOVE_BIGCHUNK(a1, a0, -0x20, t0, t1, t3, t4)
	subu	t8, t8, 0x01
	subu	a1, a1, 0x80
	bnez	t8, r_move_128bytes
	 subu	a0, a0, 0x80

0:
	beqz	t2, 1f
	 andi	t2, a2, 0x20

r_move_64bytes:
	subu	a1, a1, 0x40
	subu	a0, a0, 0x40
	RMOVE_BIGCHUNK(a1, a0, 0x20, t0, t1, t3, t4)
	RMOVE_BIGCHUNK(a1, a0, 0x00, t0, t1, t3, t4)

1:
	beqz	t2, r_do_end_words
	 andi	t8, a2, 0x1c

r_move_32bytes:
	subu	a1, a1, 0x20
	subu	a0, a0, 0x20
	RMOVE_BIGCHUNK(a1, a0, 0x00, t0, t1, t3, t4)
	andi	t8, a2, 0x1c

r_do_end_words:
	beqz	t8, r_maybe_end_cruft
	 srl	t8, t8, 0x2

r_end_words:
	lw	t0, -4(a1)
	subu	t8, t8, 0x1
	sw	t0, -4(a0)
	subu	a1, a1, 0x4
	bnez	t8, r_end_words
	 subu	a0, a0, 0x4

r_maybe_end_cruft:
	andi	t2, a2, 0x3

r_small_memcpy:
	beqz	t2, r_out
	 move	a2, t2
#endif /* Horror fix */

r_end_bytes:
	lb	t0, -1(a1)
	subu	a2, a2, 0x1
	sb	t0, -1(a0)
	subu	a1, a1, 0x1
	bnez	a2, r_end_bytes
	 subu	a0, a0, 0x1

r_out:
	jr	ra
	 move	a2, zero

#if 0 /* Horror fix */
/* ------------------------------------------------------------------------- */

/* Bad, bad.  At least try to align the source  */

r_memcpy_u_src:
	bnez	t8, r_small_memcpy		# < 8 bytes?
	 move	t2, a2

	andi	t0, a1, 7			# t0: how much to align

	ulw	t1, -8(a1)			# dword alignment
	ulw	t2, -4(a1)
	usw	t1, -8(a0)
	usw	t2, -4(a0)

	subu	a1, t0				# src
	subu	a0, t0				# dst
	subu	a2, t0				# len

	sltiu	t8, a2, 56
	bnez	t8, ru_do_end_words
	 andi	t8, a2, 0x3c

	andi	t8, a1, 8			# now qword aligned?

ru_qword_align:
	beqz	t8, ru_oword_align
	 andi	t8, a1, 0x10

	subu	a1, a1, 0x8
	lw	t0, 0x00(a1)
	lw	t1, 0x04(a1)
	subu	a0, a0, 0x8
	usw	t0, 0x00(a0)
	usw	t1, 0x04(a0)
	subu	a2, a2, 0x8

	andi	t8, a1, 0x10

ru_oword_align:
	beqz	t8, ru_begin_movement
	 srl	t8, a2, 0x7

	subu	a1, a1, 0x10
	lw	t3, 0x08(a1)			# assumes subblock ordering
	lw	t4, 0x0c(a1)
	lw	t0, 0x00(a1)
	lw	t1, 0x04(a1)
	subu	a0, a0, 0x10
	usw	t3, 0x08(a0)
	usw	t4, 0x0c(a0)
	usw	t0, 0x00(a0)
	usw	t1, 0x04(a0)
	subu	a2, a2, 0x10

	srl	t8, a2, 0x7

ru_begin_movement:
	beqz	t8, 0f
	 andi	t2, a2, 0x40

ru_move_128bytes:
	RUMOVE_BIGCHUNK(a1, a0, -0x80, t0, t1, t3, t4)
	RUMOVE_BIGCHUNK(a1, a0, -0x60, t0, t1, t3, t4)
	RUMOVE_BIGCHUNK(a1, a0, -0x40, t0, t1, t3, t4)
	RUMOVE_BIGCHUNK(a1, a0, -0x20, t0, t1, t3, t4)
	subu	t8, t8, 0x01
	subu	a1, a1, 0x80
	bnez	t8, ru_move_128bytes
	 subu	a0, a0, 0x80

0:
	beqz	t2, 1f
	 andi	t2, a2, 0x20

ru_move_64bytes:
	subu	a1, a1, 0x40
	subu	a0, a0, 0x40
	RUMOVE_BIGCHUNK(a1, a0, 0x20, t0, t1, t3, t4)
	RUMOVE_BIGCHUNK(a1, a0, 0x00, t0, t1, t3, t4)

1:
	beqz	t2, ru_do_end_words
	 andi	t8, a2, 0x1c

ru_move_32bytes:
	subu	a1, a1, 0x20
	subu	a0, a0, 0x20
	RUMOVE_BIGCHUNK(a1, a0, 0x00, t0, t1, t3, t4)
	andi	t8, a2, 0x1c

ru_do_end_words:
	beqz	t8, ru_maybe_end_cruft
	 srl	t8, t8, 0x2

ru_end_words:
	lw	t0, -4(a1)
	usw	t0, -4(a0)
	subu	t8, t8, 0x1
	subu	a1, a1, 0x4
	bnez	t8, ru_end_words
	 subu	a0, a0, 0x4

ru_maybe_end_cruft:
	andi	t2, a2, 0x3

ru_cannot_optimize:
	beqz	t2, r_out
	 move	a2, t2

ru_end_bytes:
	lb	t0, -1(a1)
	subu	a2, a2, 0x1
	sb	t0, -1(a0)
	subu	a1, a1, 0x1
	bnez	a2, ru_end_bytes
	 subu	a0, a0, 0x1

	jr	ra
	 move	a2, zero
#endif /* Horror fix */
	END(__rmemcpy)

l_fixup:					# clear the rest of the buffer
	lw	t0, THREAD_BUADDR($28)
	 nop
	subu	a2, AT, t0			# a2 bytes to go
	addu	a0, t0				# compute start address in a1
	subu	a0, a1
	j	__bzero
	 move	a1, zero

s_fixup:
	jr	ra
	 nop
