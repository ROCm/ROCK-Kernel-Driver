/* splock.S: Spinlock primitives too large to inline.
 *
 * Copyright (C) 2004 David S. Miller (davem@redhat.com)
 */

	.text
	.align	64

	.globl		_raw_spin_lock
_raw_spin_lock:		/* %o0 = lock_ptr */
1:	ldstub		[%o0], %g7
	brnz,pn		%g7, 2f
	 membar		#StoreLoad | #StoreStore
	retl
	 nop
2:	ldub		[%o0], %g7
	brnz,pt		%g7, 2b
	 membar		#LoadLoad
	ba,a,pt		%xcc, 1b

	.globl	_raw_spin_lock_flags
_raw_spin_lock_flags:	/* %o0 = lock_ptr, %o1 = irq_flags */
1:	ldstub		[%o0], %g7
	brnz,pn		%g7, 2f
	 membar		#StoreLoad | #StoreStore
	retl
	 nop

2:	rdpr		%pil, %g2		! Save PIL
	wrpr		%o1, %pil		! Set previous PIL
3:	ldub		[%o0], %g7		! Spin on lock set
	brnz,pt		%g7, 3b
	 membar		#LoadLoad
	ba,pt		%xcc, 1b		! Retry lock acquire
	 wrpr		%g2, %pil		! Restore PIL
