/* $Id: atomic.S,v 1.3 2000/03/16 16:44:37 davem Exp $
 * atomic.S: These things are too big to do inline.
 *
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 */

#include <asm/asi.h>

	.text
	.align	64

	.globl	atomic_impl_begin, atomic_impl_end

	.globl	__atomic_add
atomic_impl_begin:
__atomic_add: /* %o0 = increment, %o1 = atomic_ptr */
	lduw	[%o1], %g5
	add	%g5, %o0, %g7
	cas	[%o1], %g5, %g7
	cmp	%g5, %g7
	bne,pn	%icc, __atomic_add
	 nop
	retl
	 add	%g7, %o0, %o0

	.globl	__atomic_sub
__atomic_sub: /* %o0 = increment, %o1 = atomic_ptr */
	lduw	[%o1], %g5
	sub	%g5, %o0, %g7
	cas	[%o1], %g5, %g7
	cmp	%g5, %g7
	bne,pn	%icc, __atomic_sub
	 nop
	retl
	 sub	%g7, %o0, %o0
atomic_impl_end:
