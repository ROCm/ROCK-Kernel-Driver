	;; $Id: checksumcopy.S,v 1.2 2000/08/08 16:57:31 bjornw Exp $
	;; A fast checksum+copy routine using movem
	;; Copyright (c) 1998, 2000 Axis Communications AB
	;;
	;; Authors:	Bjorn Wesen
	;; 
	;; csum_partial_copy_nocheck(const char *src, char *dst,
	;;		             int len, unsigned int sum)

	.globl	_csum_partial_copy_nocheck
_csum_partial_copy_nocheck:	
	
	;; check for breakeven length between movem and normal word looping versions
	
	cmpu.w	80,r12
	bcs	no_movem
	nop

	;; need to save the registers we use below in the movem loop
	;; this overhead is why we have a check above for breakeven length
	
	subq	9*4,sp
	movem	r8,[sp]
	
	;; do a movem copy and checksum

	;; r10 - src
	;; r11 - dst
	;; r12 - length
	;; r13 - checksum

	subq	10*4,r12	; update length for the first loop
	
mloop:	movem	[r10+],r9	; read 10 longwords
	movem	r9,[r11+]	; write 10 longwords

	;; perform dword checksumming on the 10 longwords
	
	add.d	r0,r13
	ax
	add.d	r1,r13
	ax
	add.d	r2,r13
	ax
	add.d	r3,r13
	ax
	add.d	r4,r13
	ax
	add.d	r5,r13
	ax
	add.d	r6,r13
	ax
	add.d	r7,r13
	ax
	add.d	r8,r13
	ax
	add.d	r9,r13

	;; fold the carry into the checksum, to avoid having to loop the carry
	;; back into the top
	
	ax
	addq	0,r13

	subq	10*4,r12
	bge	mloop
	nop

	addq	10*4,r12	; compensate for last loop underflowing length

	;; fold 32-bit checksum into a 16-bit checksum, to avoid carries below
	
	moveq	-1,r1		; put 0xffff in r1, faster than move.d 0xffff,r1
	lsrq	16,r1
	
	move.d	r13,r0
	lsrq	16,r0		; r0 = checksum >> 16
	and.d	r1,r13		; checksum = checksum & 0xffff
	add.d	r0,r13		; checksum += r0
	move.d	r13,r0		; do the same again, maybe we got a carry last add
	lsrq	16,r0
	and.d	r1,r13
	add.d	r0,r13
	
	movem	[sp+],r8	; restore regs
		
no_movem:
	cmpq	2,r12
	blt	no_words
	nop
	
	;; copy and checksum the rest of the words
	
	subq	2,r12
	
wloop:	move.w	[r10+],r9
	addu.w	r9,r13
	subq	2,r12
	bge	wloop
	move.w	r9,[r11+]
	
	addq	2,r12
		
no_words:
	;; see if we have one odd byte more
	cmpq	1,r12
	beq	do_byte
	nop
	ret
	move.d	r13, r10

do_byte:	
	;; copy and checksum the last byte
	move.b	[r10],r9
	addu.b	r9,r13
	move.b	r9,[r11]
	ret
	move.d	r13, r10
		
	