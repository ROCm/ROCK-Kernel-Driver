/* $Id: copy_page.S,v 1.2 2001/07/23 10:27:25 gniibe Exp $
 *
 * copy_page implementation of SuperH
 *
 * Copyright (C) 2001  Niibe Yutaka & Kaz Kojima
 *
 */

/*
 * copy_page
 * @to: P1 address
 * @from: P1 address
 *
 * void copy_page(void *to, void *from)
 */

/*
 * r0, r1, r2, r3, r4, r5, r6, r7 --- scratch 
 * r8 --- from + 4096
 * r9 --- not used
 * r10 --- to
 * r11 --- from
 */
#include <linux/linkage.h>
ENTRY(copy_page)
	mov.l	r8,@-r15
	mov.l	r10,@-r15
	mov.l	r11,@-r15
	mov	r4,r10
	mov	r5,r11
	mov	r5,r8
	mov.w	.L4096,r0
	add	r0,r8
	!
1:	mov.l	@r11+,r0
	mov.l	@r11+,r1
	mov.l	@r11+,r2
	mov.l	@r11+,r3
	mov.l	@r11+,r4
	mov.l	@r11+,r5
	mov.l	@r11+,r6
	mov.l	@r11+,r7
#if defined(__sh3__)
	mov.l	r0,@r10
#elif defined(__SH4__)
	movca.l	r0,@r10
	mov	r10,r0
#endif
	add	#32,r10
	mov.l	r7,@-r10
	mov.l	r6,@-r10
	mov.l	r5,@-r10
	mov.l	r4,@-r10
	mov.l	r3,@-r10
	mov.l	r2,@-r10
	mov.l	r1,@-r10
#if defined(__SH4__)
	ocbwb	@r0
#endif
	cmp/eq	r11,r8
	bf/s	1b
	 add	#28,r10
	!
	mov.l	@r15+,r11
	mov.l	@r15+,r10
	mov.l	@r15+,r8
	rts
	 nop
.L4096:	.word	4096
