/* $Id: bitops.S,v 1.3 2001/11/18 00:12:56 davem Exp $
 * bitops.S: Sparc64 atomic bit operations.
 *
 * Copyright (C) 2000 David S. Miller (davem@redhat.com)
 */

#include <asm/asi.h>

	.text
	.align	64
	.globl	___test_and_set_bit
	.type	___test_and_set_bit,#function
___test_and_set_bit:	/* %o0=nr, %o1=addr */
	srlx	%o0, 6, %g1
	mov	1, %g5
	sllx	%g1, 3, %g3
	and	%o0, 63, %g2
	sllx	%g5, %g2, %g5
	add	%o1, %g3, %o1
	ldx	[%o1], %g7
1:	andcc	%g7, %g5, %o0
	bne,pn	%xcc, 2f
	 xor	%g7, %g5, %g1
	casx	[%o1], %g7, %g1
	cmp	%g7, %g1
	bne,a,pn %xcc, 1b
	 ldx	[%o1], %g7
2:	retl
	 membar	#StoreLoad | #StoreStore
	.size	___test_and_set_bit, .-___test_and_set_bit

	.globl	___test_and_clear_bit
	.type	___test_and_clear_bit,#function
___test_and_clear_bit:	/* %o0=nr, %o1=addr */
	srlx	%o0, 6, %g1
	mov	1, %g5
	sllx	%g1, 3, %g3
	and	%o0, 63, %g2
	sllx	%g5, %g2, %g5
	add	%o1, %g3, %o1
	ldx	[%o1], %g7
1:	andcc	%g7, %g5, %o0
	be,pn	%xcc, 2f
	 xor	%g7, %g5, %g1
	casx	[%o1], %g7, %g1
	cmp	%g7, %g1
	bne,a,pn %xcc, 1b
	 ldx	[%o1], %g7
2:	retl
	 membar	#StoreLoad | #StoreStore
	.size	___test_and_clear_bit, .-___test_and_clear_bit

	.globl	___test_and_change_bit
	.type	___test_and_change_bit,#function
___test_and_change_bit:	/* %o0=nr, %o1=addr */
	srlx	%o0, 6, %g1
	mov	1, %g5
	sllx	%g1, 3, %g3
	and	%o0, 63, %g2
	sllx	%g5, %g2, %g5
	add	%o1, %g3, %o1
	ldx	[%o1], %g7
1:	and	%g7, %g5, %o0
	xor	%g7, %g5, %g1
	casx	[%o1], %g7, %g1
	cmp	%g7, %g1
	bne,a,pn %xcc, 1b
	 ldx	[%o1], %g7
2:	retl
	 membar	#StoreLoad | #StoreStore
	nop
	.size	___test_and_change_bit, .-___test_and_change_bit
