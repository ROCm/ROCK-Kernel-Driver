/* $Id: __copy_user_page-sh4.S,v 1.1 2001/07/23 09:02:17 gniibe Exp $
 *
 * __copy_user_page implementation of SuperH
 *
 * Copyright (C) 2001  Niibe Yutaka & Kaz Kojima
 *
 */

/*
 * __copy_user_page
 * @to: P1 address (with same color)
 * @from: P1 address
 * @orig_to: P1 address
 *
 * void __copy_user_page(void *to, void *from, void *orig_to)
 */

/*
 * r0, r1, r2, r3, r4, r5, r6, r7 --- scratch 
 * r8 --- from + 4096
 * r9 --- orig_to
 * r10 --- to
 * r11 --- from
 */
#include <linux/linkage.h>
ENTRY(__copy_user_page)
	mov.l	r8,@-r15
	mov.l	r9,@-r15
	mov.l	r10,@-r15
	mov.l	r11,@-r15
	mov	r4,r10
	mov	r5,r11
	mov	r6,r9
	mov	r5,r8
	mov.w	.L4096,r0
	add	r0,r8
	!
1:	ocbi	@r9
	add	#32,r9
	mov.l	@r11+,r0
	mov.l	@r11+,r1
	mov.l	@r11+,r2
	mov.l	@r11+,r3
	mov.l	@r11+,r4
	mov.l	@r11+,r5
	mov.l	@r11+,r6
	mov.l	@r11+,r7
	movca.l	r0,@r10
	mov	r10,r0
	add	#32,r10
	mov.l	r7,@-r10
	mov.l	r6,@-r10
	mov.l	r5,@-r10
	mov.l	r4,@-r10
	mov.l	r3,@-r10
	mov.l	r2,@-r10
	mov.l	r1,@-r10
	ocbwb	@r0
	cmp/eq	r11,r8
	bf/s	1b
	 add	#28,r10
	!
	mov.l	@r15+,r11
	mov.l	@r15+,r10
	mov.l	@r15+,r9
	mov.l	@r15+,r8
	rts
	 nop
.L4096:	.word	4096
