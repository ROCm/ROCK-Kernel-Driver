/* $Id: clear_page.S,v 1.1 2001/07/23 10:08:50 gniibe Exp $
 *
 * clear_page implementation of SuperH
 *
 * Copyright (C) 2001  Niibe Yutaka & Kaz Kojima
 *
 */

/*
 * clear_page
 * @to: P1 address
 *
 * void clear_page(void *to)
 */

/*
 * r0 --- scratch
 * r4 --- to
 * r5 --- to + 4096
 */
#include <linux/linkage.h>
ENTRY(clear_page)
	mov	r4,r5
	mov.w	.Llimit,r0
	add	r0,r5
	mov	#0,r0
	!
1:
#if defined(__sh3__)
	mov.l	r0,@r4
#elif defined(__SH4__)
	movca.l	r0,@r4
	mov	r4,r1
#endif
	add	#32,r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
	mov.l	r0,@-r4
#if defined(__SH4__)
	ocbwb	@r1
#endif
	cmp/eq	r5,r4
	bf/s	1b
	 add	#28,r4
	!
	rts
	 nop
.Llimit:	.word	(4096-28)
