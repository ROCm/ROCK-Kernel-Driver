/* $Id$
 *
 * __clear_user_page implementation of SuperH
 *
 * Copyright (C) 2001  Niibe Yutaka & Kaz Kojima
 *
 */

/*
 * __clear_user_page
 * @to: P1 address (with same color)
 * @orig_to: P1 address
 *
 * void __clear_user_page(void *to, void *orig_to)
 */

/*
 * r0 --- scratch 
 * r4 --- to
 * r5 --- orig_to
 * r6 --- to + 4096
 */
#include <linux/linkage.h>
ENTRY(__clear_user_page)
	mov	r4,r6
	mov.w	.L4096,r0
	add	r0,r6
	mov	#0,r0
	!
1:	ocbi	@r5
	add	#32,r5
	movca.l	r0,@r4
	mov	r4,r1
	add	#32,r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	add	#28,r4
	cmp/eq	r6,r4
	bf/s	1b
	 ocbwb	@r1
	!
	rts
	 nop
.L4096:	.word	4096
