#include <linux/linkage.h>

#ifndef __ARMEB__
ql	.req	r0			@ quotient low
qh	.req	r1			@ quotient high
onl	.req	r0			@ original dividend low
onh	.req	r1			@ original dividend high
nl	.req	r4			@ dividend low
nh	.req	r5			@ dividend high
res	.req	r4			@ result
#else
ql	.req	r1
qh	.req	r0
onl	.req	r1
onh	.req	r0
nl	.req	r5
nh	.req	r4
res	.req	r5
#endif

dl	.req	r3			@ divisor low
dh	.req	r2			@ divsor high


ENTRY(do_div64)
	stmfd	sp!, {r4, r5, lr}
	mov	nl, onl
	movs	nh, onh			@ if high bits are zero
	movne	lr, #33
	moveq	lr, #1			@ only divide low bits
	moveq	nh, onl

	tst	dh, #0x80000000
	bne	2f
1:	cmp	nh, dh
	bls	2f
	add	lr, lr, #1
	movs	dh, dh, lsl #1		@ left justify disor
	bpl	1b

2:	movs	nh, onh
	moveq	dl, dh
	moveq	dh, #0
	movne	dl, #0
	mov	ql, #0
	mov	qh, #0
3:	subs	ip, nl, dl		@ trial subtraction
	sbcs	ip, nh, dh
	movcs	nh, ip			@ only update if successful
	subcs	nl, nl, dl		@ (repeat the subtraction)
	adcs	ql, ql, ql		@ C=1 if successful, shift into
	adc	qh, qh, qh		@ quotient
	movs	dh, dh, lsr #1		@ shift base high part right
	mov	dl, dl, rrx		@ shift base low part right
	subs	lr, lr, #1
	bne	3b

	mov	r2, res
	ldmfd	sp!, {r4, r5, pc}
