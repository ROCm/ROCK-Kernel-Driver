	;; $Id: checksum.S,v 1.1 2000/07/10 16:25:21 bjornw Exp $
	;; A fast checksum routine using movem
	;; Copyright (c) 1998 Bjorn Wesen/Axis Communications AB

	;; csum_partial(const unsigned char * buff, int len, unsigned int sum)
	
	.globl	_csum_partial
_csum_partial:
	
	;; check for breakeven length between movem and normal word looping versions
	
	cmpu.w	80,r11
	bcs	no_movem
	nop

	;; need to save the registers we use below in the movem loop
	;; this overhead is why we have a check above for breakeven length
	
	subq	9*4,sp
	movem	r8,[sp]
	
	;; do a movem checksum

	;; r10 - src
	;; r11 - length
	;; r12 - checksum

	subq	10*4,r11	; update length for the first loop
	
mloop:	movem	[r10+],r9	; read 10 longwords

	;; perform dword checksumming on the 10 longwords
	
	add.d	r0,r12
	ax
	add.d	r1,r12
	ax
	add.d	r2,r12
	ax
	add.d	r3,r12
	ax
	add.d	r4,r12
	ax
	add.d	r5,r12
	ax
	add.d	r6,r12
	ax
	add.d	r7,r12
	ax
	add.d	r8,r12
	ax
	add.d	r9,r12

	;; fold the carry into the checksum, to avoid having to loop the carry
	;; back into the top
	
	ax
	addq	0,r12
	ax			; do it again, since we might have generated a carry
	addq	0,r12

	subq	10*4,r11
	bge	mloop
	nop

	addq	10*4,r11	; compensate for last loop underflowing length

	;; fold 32-bit checksum into a 16-bit checksum, to avoid carries below
	
	moveq	-1,r1		; put 0xffff in r1, faster than move.d 0xffff,r1
	lsrq	16,r1
	
	move.d	r12,r0
	lsrq	16,r0		; r0 = checksum >> 16
	and.d	r1,r12		; checksum = checksum & 0xffff
	add.d	r0,r12		; checksum += r0
	move.d	r12,r0		; do the same again, maybe we got a carry last add
	lsrq	16,r0
	and.d	r1,r12
	add.d	r0,r12
	
	movem	[sp+],r8	; restore regs

no_movem:
	cmpq	2,r11
	blt	no_words
	nop
	
	;; checksum the rest of the words
	
	subq	2,r11
	
wloop:	subq	2,r11
	bge	wloop
	addu.w	[r10+],r12
	
	addq	2,r11
		
no_words:
	;; see if we have one odd byte more
	cmpq	1,r11
	beq	do_byte
	nop
	ret
	move.d	r12, r10

do_byte:	
	;; copy and checksum the last byte
	addu.b	[r10],r12
	ret
	move.d	r12, r10
		
	