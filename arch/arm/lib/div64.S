#include <linux/linkage.h>

ql	.req	r0			@ quotient low
qh	.req	r1			@ quotient high
dl	.req	r3			@ divisor low
dh	.req	r2			@ divisor high
nl	.req	r4			@ dividend low
nh	.req	r5			@ dividend high

ENTRY(do_div64)
	stmfd	sp!, {r4, r5, lr}
	mov	nl, r0
	movs	nh, r1			@ if high bits are zero
	movne	lr, #33
	moveq	lr, #1			@ only divide low bits
	moveq	nh, r0

1:	cmp	nh, dh
	bls	2f
	add	lr, lr, #1
	movs	dh, dh, lsl #1		@ left justify divisor
	bpl	1b

2:	movs	nh, r1
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

	mov	r2, nl
	ldmfd	sp!, {r4, r5, pc}
