/*
 * include/asm-alpha/xor.h
 *
 * Optimized RAID-5 checksumming functions for alpha EV5 and EV6
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example /usr/src/linux/COPYING); if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

extern void xor_alpha_2(unsigned long, unsigned long *, unsigned long *);
extern void xor_alpha_3(unsigned long, unsigned long *, unsigned long *,
		        unsigned long *);
extern void xor_alpha_4(unsigned long, unsigned long *, unsigned long *,
		        unsigned long *, unsigned long *);
extern void xor_alpha_5(unsigned long, unsigned long *, unsigned long *,
		        unsigned long *, unsigned long *, unsigned long *);

extern void xor_alpha_prefetch_2(unsigned long, unsigned long *,
				 unsigned long *);
extern void xor_alpha_prefetch_3(unsigned long, unsigned long *,
				 unsigned long *, unsigned long *);
extern void xor_alpha_prefetch_4(unsigned long, unsigned long *,
				 unsigned long *, unsigned long *,
				 unsigned long *);
extern void xor_alpha_prefetch_5(unsigned long, unsigned long *,
				 unsigned long *, unsigned long *,
				 unsigned long *, unsigned long *);

asm("
	.text
	.align 3
	.ent xor_alpha_2
xor_alpha_2:
	.prologue 0
	srl $16, 6, $16
	.align 4
2:
	ldq $0,0($17)
	ldq $1,0($18)
	ldq $2,8($17)
	ldq $3,8($18)

	ldq $4,16($17)
	ldq $5,16($18)
	ldq $6,24($17)
	ldq $7,24($18)

	ldq $19,32($17)
	ldq $20,32($18)
	ldq $21,40($17)
	ldq $22,40($18)

	ldq $23,48($17)
	ldq $24,48($18)
	ldq $25,56($17)
	xor $0,$1,$0		# 7 cycles from $1 load

	ldq $27,56($18)
	xor $2,$3,$2
	stq $0,0($17)
	xor $4,$5,$4

	stq $2,8($17)
	xor $6,$7,$6
	stq $4,16($17)
	xor $19,$20,$19

	stq $6,24($17)
	xor $21,$22,$21
	stq $19,32($17)
	xor $23,$24,$23

	stq $21,40($17)
	xor $25,$27,$25
	stq $23,48($17)
	subq $16,1,$16

	stq $25,56($17)
	addq $17,64,$17
	addq $18,64,$18
	bgt $16,2b

	ret
	.end xor_alpha_2

	.align 3
	.ent xor_alpha_3
xor_alpha_3:
	.prologue 0
	srl $16, 6, $16
	.align 4
3:
	ldq $0,0($17)
	ldq $1,0($18)
	ldq $2,0($19)
	ldq $3,8($17)

	ldq $4,8($18)
	ldq $6,16($17)
	ldq $7,16($18)
	ldq $21,24($17)

	ldq $22,24($18)
	ldq $24,32($17)
	ldq $25,32($18)
	ldq $5,8($19)

	ldq $20,16($19)
	ldq $23,24($19)
	ldq $27,32($19)
	nop

	xor $0,$1,$1		# 8 cycles from $0 load
	xor $3,$4,$4		# 6 cycles from $4 load
	xor $6,$7,$7		# 6 cycles from $7 load
	xor $21,$22,$22		# 5 cycles from $22 load

	xor $1,$2,$2		# 9 cycles from $2 load
	xor $24,$25,$25		# 5 cycles from $25 load
	stq $2,0($17)
	xor $4,$5,$5		# 6 cycles from $5 load

	stq $5,8($17)
	xor $7,$20,$20		# 7 cycles from $20 load
	stq $20,16($17)
	xor $22,$23,$23		# 7 cycles from $23 load

	stq $23,24($17)
	xor $25,$27,$27		# 7 cycles from $27 load
	stq $27,32($17)
	nop

	ldq $0,40($17)
	ldq $1,40($18)
	ldq $3,48($17)
	ldq $4,48($18)

	ldq $6,56($17)
	ldq $7,56($18)
	ldq $2,40($19)
	ldq $5,48($19)

	ldq $20,56($19)
	xor $0,$1,$1		# 4 cycles from $1 load
	xor $3,$4,$4		# 5 cycles from $4 load
	xor $6,$7,$7		# 5 cycles from $7 load

	xor $1,$2,$2		# 4 cycles from $2 load
	xor $4,$5,$5		# 5 cycles from $5 load
	stq $2,40($17)
	xor $7,$20,$20		# 4 cycles from $20 load

	stq $5,48($17)
	subq $16,1,$16
	stq $20,56($17)
	addq $19,64,$19

	addq $18,64,$18
	addq $17,64,$17
	bgt $16,3b
	ret
	.end xor_alpha_3

	.align 3
	.ent xor_alpha_4
xor_alpha_4:
	.prologue 0
	srl $16, 6, $16
	.align 4
4:
	ldq $0,0($17)
	ldq $1,0($18)
	ldq $2,0($19)
	ldq $3,0($20)

	ldq $4,8($17)
	ldq $5,8($18)
	ldq $6,8($19)
	ldq $7,8($20)

	ldq $21,16($17)
	ldq $22,16($18)
	ldq $23,16($19)
	ldq $24,16($20)

	ldq $25,24($17)
	xor $0,$1,$1		# 6 cycles from $1 load
	ldq $27,24($18)
	xor $2,$3,$3		# 6 cycles from $3 load

	ldq $0,24($19)
	xor $1,$3,$3
	ldq $1,24($20)
	xor $4,$5,$5		# 7 cycles from $5 load

	stq $3,0($17)
	xor $6,$7,$7
	xor $21,$22,$22		# 7 cycles from $22 load
	xor $5,$7,$7

	stq $7,8($17)
	xor $23,$24,$24		# 7 cycles from $24 load
	ldq $2,32($17)
	xor $22,$24,$24

	ldq $3,32($18)
	ldq $4,32($19)
	ldq $5,32($20)
	xor $25,$27,$27		# 8 cycles from $27 load

	ldq $6,40($17)
	ldq $7,40($18)
	ldq $21,40($19)
	ldq $22,40($20)

	stq $24,16($17)
	xor $0,$1,$1		# 9 cycles from $1 load
	xor $2,$3,$3		# 5 cycles from $3 load
	xor $27,$1,$1

	stq $1,24($17)
	xor $4,$5,$5		# 5 cycles from $5 load
	ldq $23,48($17)
	ldq $24,48($18)

	ldq $25,48($19)
	xor $3,$5,$5
	ldq $27,48($20)
	ldq $0,56($17)

	ldq $1,56($18)
	ldq $2,56($19)
	xor $6,$7,$7		# 8 cycles from $6 load
	ldq $3,56($20)

	stq $5,32($17)
	xor $21,$22,$22		# 8 cycles from $22 load
	xor $7,$22,$22
	xor $23,$24,$24		# 5 cycles from $24 load

	stq $22,40($17)
	xor $25,$27,$27		# 5 cycles from $27 load
	xor $24,$27,$27
	xor $0,$1,$1		# 5 cycles from $1 load

	stq $27,48($17)
	xor $2,$3,$3		# 4 cycles from $3 load
	xor $1,$3,$3
	subq $16,1,$16

	stq $3,56($17)
	addq $20,64,$20
	addq $19,64,$19
	addq $18,64,$18

	addq $17,64,$17
	bgt $16,4b
	ret
	.end xor_alpha_4

	.align 3
	.ent xor_alpha_5
xor_alpha_5:
	.prologue 0
	srl $16, 6, $16
	.align 4
5:
	ldq $0,0($17)
	ldq $1,0($18)
	ldq $2,0($19)
	ldq $3,0($20)

	ldq $4,0($21)
	ldq $5,8($17)
	ldq $6,8($18)
	ldq $7,8($19)

	ldq $22,8($20)
	ldq $23,8($21)
	ldq $24,16($17)
	ldq $25,16($18)

	ldq $27,16($19)
	xor $0,$1,$1		# 6 cycles from $1 load
	ldq $28,16($20)
	xor $2,$3,$3		# 6 cycles from $3 load

	ldq $0,16($21)
	xor $1,$3,$3
	ldq $1,24($17)
	xor $3,$4,$4		# 7 cycles from $4 load

	stq $4,0($17)
	xor $5,$6,$6		# 7 cycles from $6 load
	xor $7,$22,$22		# 7 cycles from $22 load
	xor $6,$23,$23		# 7 cycles from $23 load

	ldq $2,24($18)
	xor $22,$23,$23
	ldq $3,24($19)
	xor $24,$25,$25		# 8 cycles from $25 load

	stq $23,8($17)
	xor $25,$27,$27		# 8 cycles from $27 load
	ldq $4,24($20)
	xor $28,$0,$0		# 7 cycles from $0 load

	ldq $5,24($21)
	xor $27,$0,$0
	ldq $6,32($17)
	ldq $7,32($18)

	stq $0,16($17)
	xor $1,$2,$2		# 6 cycles from $2 load
	ldq $22,32($19)
	xor $3,$4,$4		# 4 cycles from $4 load
	
	ldq $23,32($20)
	xor $2,$4,$4
	ldq $24,32($21)
	ldq $25,40($17)

	ldq $27,40($18)
	ldq $28,40($19)
	ldq $0,40($20)
	xor $4,$5,$5		# 7 cycles from $5 load

	stq $5,24($17)
	xor $6,$7,$7		# 7 cycles from $7 load
	ldq $1,40($21)
	ldq $2,48($17)

	ldq $3,48($18)
	xor $7,$22,$22		# 7 cycles from $22 load
	ldq $4,48($19)
	xor $23,$24,$24		# 6 cycles from $24 load

	ldq $5,48($20)
	xor $22,$24,$24
	ldq $6,48($21)
	xor $25,$27,$27		# 7 cycles from $27 load

	stq $24,32($17)
	xor $27,$28,$28		# 8 cycles from $28 load
	ldq $7,56($17)
	xor $0,$1,$1		# 6 cycles from $1 load

	ldq $22,56($18)
	ldq $23,56($19)
	ldq $24,56($20)
	ldq $25,56($21)

	xor $28,$1,$1
	xor $2,$3,$3		# 9 cycles from $3 load
	xor $3,$4,$4		# 9 cycles from $4 load
	xor $5,$6,$6		# 8 cycles from $6 load

	stq $1,40($17)
	xor $4,$6,$6
	xor $7,$22,$22		# 7 cycles from $22 load
	xor $23,$24,$24		# 6 cycles from $24 load

	stq $6,48($17)
	xor $22,$24,$24
	subq $16,1,$16
	xor $24,$25,$25		# 8 cycles from $25 load

	stq $25,56($17)
	addq $21,64,$21
	addq $20,64,$20
	addq $19,64,$19

	addq $18,64,$18
	addq $17,64,$17
	bgt $16,5b
	ret
	.end xor_alpha_5

	.align 3
	.ent xor_alpha_prefetch_2
xor_alpha_prefetch_2:
	.prologue 0
	srl $16, 6, $16

	ldq $31, 0($17)
	ldq $31, 0($18)

	ldq $31, 64($17)
	ldq $31, 64($18)

	ldq $31, 128($17)
	ldq $31, 128($18)

	ldq $31, 192($17)
	ldq $31, 192($18)
	.align 4
2:
	ldq $0,0($17)
	ldq $1,0($18)
	ldq $2,8($17)
	ldq $3,8($18)

	ldq $4,16($17)
	ldq $5,16($18)
	ldq $6,24($17)
	ldq $7,24($18)

	ldq $19,32($17)
	ldq $20,32($18)
	ldq $21,40($17)
	ldq $22,40($18)

	ldq $23,48($17)
	ldq $24,48($18)
	ldq $25,56($17)
	ldq $27,56($18)

	ldq $31,256($17)
	xor $0,$1,$0		# 8 cycles from $1 load
	ldq $31,256($18)
	xor $2,$3,$2

	stq $0,0($17)
	xor $4,$5,$4
	stq $2,8($17)
	xor $6,$7,$6

	stq $4,16($17)
	xor $19,$20,$19
	stq $6,24($17)
	xor $21,$22,$21

	stq $19,32($17)
	xor $23,$24,$23
	stq $21,40($17)
	xor $25,$27,$25

	stq $23,48($17)
	subq $16,1,$16
	stq $25,56($17)
	addq $17,64,$17

	addq $18,64,$18
	bgt $16,2b
	ret
	.end xor_alpha_prefetch_2

	.align 3
	.ent xor_alpha_prefetch_3
xor_alpha_prefetch_3:
	.prologue 0
	srl $16, 6, $16

	ldq $31, 0($17)
	ldq $31, 0($18)
	ldq $31, 0($19)

	ldq $31, 64($17)
	ldq $31, 64($18)
	ldq $31, 64($19)

	ldq $31, 128($17)
	ldq $31, 128($18)
	ldq $31, 128($19)

	ldq $31, 192($17)
	ldq $31, 192($18)
	ldq $31, 192($19)
	.align 4
3:
	ldq $0,0($17)
	ldq $1,0($18)
	ldq $2,0($19)
	ldq $3,8($17)

	ldq $4,8($18)
	ldq $6,16($17)
	ldq $7,16($18)
	ldq $21,24($17)

	ldq $22,24($18)
	ldq $24,32($17)
	ldq $25,32($18)
	ldq $5,8($19)

	ldq $20,16($19)
	ldq $23,24($19)
	ldq $27,32($19)
	nop

	xor $0,$1,$1		# 8 cycles from $0 load
	xor $3,$4,$4		# 7 cycles from $4 load
	xor $6,$7,$7		# 6 cycles from $7 load
	xor $21,$22,$22		# 5 cycles from $22 load

	xor $1,$2,$2		# 9 cycles from $2 load
	xor $24,$25,$25		# 5 cycles from $25 load
	stq $2,0($17)
	xor $4,$5,$5		# 6 cycles from $5 load

	stq $5,8($17)
	xor $7,$20,$20		# 7 cycles from $20 load
	stq $20,16($17)
	xor $22,$23,$23		# 7 cycles from $23 load

	stq $23,24($17)
	xor $25,$27,$27		# 7 cycles from $27 load
	stq $27,32($17)
	nop

	ldq $0,40($17)
	ldq $1,40($18)
	ldq $3,48($17)
	ldq $4,48($18)

	ldq $6,56($17)
	ldq $7,56($18)
	ldq $2,40($19)
	ldq $5,48($19)

	ldq $20,56($19)
	ldq $31,256($17)
	ldq $31,256($18)
	ldq $31,256($19)

	xor $0,$1,$1		# 6 cycles from $1 load
	xor $3,$4,$4		# 5 cycles from $4 load
	xor $6,$7,$7		# 5 cycles from $7 load
	xor $1,$2,$2		# 4 cycles from $2 load
	
	xor $4,$5,$5		# 5 cycles from $5 load
	xor $7,$20,$20		# 4 cycles from $20 load
	stq $2,40($17)
	subq $16,1,$16

	stq $5,48($17)
	addq $19,64,$19
	stq $20,56($17)
	addq $18,64,$18

	addq $17,64,$17
	bgt $16,3b
	ret
	.end xor_alpha_prefetch_3

	.align 3
	.ent xor_alpha_prefetch_4
xor_alpha_prefetch_4:
	.prologue 0
	srl $16, 6, $16

	ldq $31, 0($17)
	ldq $31, 0($18)
	ldq $31, 0($19)
	ldq $31, 0($20)

	ldq $31, 64($17)
	ldq $31, 64($18)
	ldq $31, 64($19)
	ldq $31, 64($20)

	ldq $31, 128($17)
	ldq $31, 128($18)
	ldq $31, 128($19)
	ldq $31, 128($20)

	ldq $31, 192($17)
	ldq $31, 192($18)
	ldq $31, 192($19)
	ldq $31, 192($20)
	.align 4
4:
	ldq $0,0($17)
	ldq $1,0($18)
	ldq $2,0($19)
	ldq $3,0($20)

	ldq $4,8($17)
	ldq $5,8($18)
	ldq $6,8($19)
	ldq $7,8($20)

	ldq $21,16($17)
	ldq $22,16($18)
	ldq $23,16($19)
	ldq $24,16($20)

	ldq $25,24($17)
	xor $0,$1,$1		# 6 cycles from $1 load
	ldq $27,24($18)
	xor $2,$3,$3		# 6 cycles from $3 load

	ldq $0,24($19)
	xor $1,$3,$3
	ldq $1,24($20)
	xor $4,$5,$5		# 7 cycles from $5 load

	stq $3,0($17)
	xor $6,$7,$7
	xor $21,$22,$22		# 7 cycles from $22 load
	xor $5,$7,$7

	stq $7,8($17)
	xor $23,$24,$24		# 7 cycles from $24 load
	ldq $2,32($17)
	xor $22,$24,$24

	ldq $3,32($18)
	ldq $4,32($19)
	ldq $5,32($20)
	xor $25,$27,$27		# 8 cycles from $27 load

	ldq $6,40($17)
	ldq $7,40($18)
	ldq $21,40($19)
	ldq $22,40($20)

	stq $24,16($17)
	xor $0,$1,$1		# 9 cycles from $1 load
	xor $2,$3,$3		# 5 cycles from $3 load
	xor $27,$1,$1

	stq $1,24($17)
	xor $4,$5,$5		# 5 cycles from $5 load
	ldq $23,48($17)
	xor $3,$5,$5

	ldq $24,48($18)
	ldq $25,48($19)
	ldq $27,48($20)
	ldq $0,56($17)

	ldq $1,56($18)
	ldq $2,56($19)
	ldq $3,56($20)
	xor $6,$7,$7		# 8 cycles from $6 load

	ldq $31,256($17)
	xor $21,$22,$22		# 8 cycles from $22 load
	ldq $31,256($18)
	xor $7,$22,$22

	ldq $31,256($19)
	xor $23,$24,$24		# 6 cycles from $24 load
	ldq $31,256($20)
	xor $25,$27,$27		# 6 cycles from $27 load

	stq $5,32($17)
	xor $24,$27,$27
	xor $0,$1,$1		# 7 cycles from $1 load
	xor $2,$3,$3		# 6 cycles from $3 load

	stq $22,40($17)
	xor $1,$3,$3
	stq $27,48($17)
	subq $16,1,$16

	stq $3,56($17)
	addq $20,64,$20
	addq $19,64,$19
	addq $18,64,$18

	addq $17,64,$17
	bgt $16,4b
	ret
	.end xor_alpha_prefetch_4

	.align 3
	.ent xor_alpha_prefetch_5
xor_alpha_prefetch_5:
	.prologue 0
	srl $16, 6, $16

	ldq $31, 0($17)
	ldq $31, 0($18)
	ldq $31, 0($19)
	ldq $31, 0($20)
	ldq $31, 0($21)

	ldq $31, 64($17)
	ldq $31, 64($18)
	ldq $31, 64($19)
	ldq $31, 64($20)
	ldq $31, 64($21)

	ldq $31, 128($17)
	ldq $31, 128($18)
	ldq $31, 128($19)
	ldq $31, 128($20)
	ldq $31, 128($21)

	ldq $31, 192($17)
	ldq $31, 192($18)
	ldq $31, 192($19)
	ldq $31, 192($20)
	ldq $31, 192($21)
	.align 4
5:
	ldq $0,0($17)
	ldq $1,0($18)
	ldq $2,0($19)
	ldq $3,0($20)

	ldq $4,0($21)
	ldq $5,8($17)
	ldq $6,8($18)
	ldq $7,8($19)

	ldq $22,8($20)
	ldq $23,8($21)
	ldq $24,16($17)
	ldq $25,16($18)

	ldq $27,16($19)
	xor $0,$1,$1		# 6 cycles from $1 load
	ldq $28,16($20)
	xor $2,$3,$3		# 6 cycles from $3 load

	ldq $0,16($21)
	xor $1,$3,$3
	ldq $1,24($17)
	xor $3,$4,$4		# 7 cycles from $4 load

	stq $4,0($17)
	xor $5,$6,$6		# 7 cycles from $6 load
	xor $7,$22,$22		# 7 cycles from $22 load
	xor $6,$23,$23		# 7 cycles from $23 load

	ldq $2,24($18)
	xor $22,$23,$23
	ldq $3,24($19)
	xor $24,$25,$25		# 8 cycles from $25 load

	stq $23,8($17)
	xor $25,$27,$27		# 8 cycles from $27 load
	ldq $4,24($20)
	xor $28,$0,$0		# 7 cycles from $0 load

	ldq $5,24($21)
	xor $27,$0,$0
	ldq $6,32($17)
	ldq $7,32($18)

	stq $0,16($17)
	xor $1,$2,$2		# 6 cycles from $2 load
	ldq $22,32($19)
	xor $3,$4,$4		# 4 cycles from $4 load
	
	ldq $23,32($20)
	xor $2,$4,$4
	ldq $24,32($21)
	ldq $25,40($17)

	ldq $27,40($18)
	ldq $28,40($19)
	ldq $0,40($20)
	xor $4,$5,$5		# 7 cycles from $5 load

	stq $5,24($17)
	xor $6,$7,$7		# 7 cycles from $7 load
	ldq $1,40($21)
	ldq $2,48($17)

	ldq $3,48($18)
	xor $7,$22,$22		# 7 cycles from $22 load
	ldq $4,48($19)
	xor $23,$24,$24		# 6 cycles from $24 load

	ldq $5,48($20)
	xor $22,$24,$24
	ldq $6,48($21)
	xor $25,$27,$27		# 7 cycles from $27 load

	stq $24,32($17)
	xor $27,$28,$28		# 8 cycles from $28 load
	ldq $7,56($17)
	xor $0,$1,$1		# 6 cycles from $1 load

	ldq $22,56($18)
	ldq $23,56($19)
	ldq $24,56($20)
	ldq $25,56($21)

	ldq $31,256($17)
	xor $28,$1,$1
	ldq $31,256($18)
	xor $2,$3,$3		# 9 cycles from $3 load

	ldq $31,256($19)
	xor $3,$4,$4		# 9 cycles from $4 load
	ldq $31,256($20)
	xor $5,$6,$6		# 8 cycles from $6 load

	stq $1,40($17)
	xor $4,$6,$6
	xor $7,$22,$22		# 7 cycles from $22 load
	xor $23,$24,$24		# 6 cycles from $24 load

	stq $6,48($17)
	xor $22,$24,$24
	ldq $31,256($21)
	xor $24,$25,$25		# 8 cycles from $25 load

	stq $25,56($17)
	subq $16,1,$16
	addq $21,64,$21
	addq $20,64,$20

	addq $19,64,$19
	addq $18,64,$18
	addq $17,64,$17
	bgt $16,5b

	ret
	.end xor_alpha_prefetch_5
");

static struct xor_block_template xor_block_alpha = {
	name: "alpha",
	do_2: xor_alpha_2,
	do_3: xor_alpha_3,
	do_4: xor_alpha_4,
	do_5: xor_alpha_5,
};

static struct xor_block_template xor_block_alpha_prefetch = {
	name: "alpha prefetch",
	do_2: xor_alpha_prefetch_2,
	do_3: xor_alpha_prefetch_3,
	do_4: xor_alpha_prefetch_4,
	do_5: xor_alpha_prefetch_5,
};

/* For grins, also test the generic routines.  */
#include <asm-generic/xor.h>

#undef XOR_TRY_TEMPLATES
#define XOR_TRY_TEMPLATES				\
	do {						\
		xor_speed(&xor_block_8regs);		\
		xor_speed(&xor_block_32regs);		\
		xor_speed(&xor_block_alpha);		\
		xor_speed(&xor_block_alpha_prefetch);	\
	} while (0)

/* Force the use of alpha_prefetch if EV6, as it is significantly
   faster in the cold cache case.  */
#define XOR_SELECT_TEMPLATE(FASTEST) \
	(implver() == IMPLVER_EV6 ? &xor_block_alpha_prefetch : FASTEST)
