/*
 *  linux/arch/arm/boot/bootp/init.S
 *
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Header file for splitting kernel + initrd.  Note that we pass
 *  r0 through to r3 straight through.
 */
		.section .start,#alloc,#execinstr
		.type	_entry, #function
_entry:
kernel_addr:	adr	r10, initdata
		ldmia	r10, {r11, r12}
		sub	r11, r10, r11		@ work out exec offset
		add	r12, r12, r11		@ correct "splitify"
		mov	pc, r12			@ jump to splitify
		.size	_entry,. - _entry

		.type	initdata, #object
initdata:	.word	initdata		@ compiled address of this
		.word	splitify
		.size	initdata,. - initdata

		.text
splitify:	adr	r13, data
		ldmia	r13!, {r4-r6}		@ move the kernel
		add	r4, r4, r11		@ correction
		mov	r12, r5
		bl	move

		ldmia	r13!, {r4-r6}		@ then the initrd
		add	r4, r4, r11		@ correction
		bl	move

		ldmib	r13, {r5,r6,r7}		@ get size and addr of initrd
		add	r7, r7, #16*4		@ offset of initrd_start in param_struct
		stmia	r7, {r5,r6}		@ save in param_struct
		mov	pc, r12			@ call kernel

move:		ldmia	r4!, {r7 - r10}		@ move 32-bytes at a time
		stmia	r5!, {r7 - r10}
		ldmia	r4!, {r7 - r10}
		stmia	r5!, {r7 - r10}
		subs	r6, r6, #8 * 4
		bcs	move
		mov	pc, lr

data:		.word	kernel_start
		.word	kernel_addr
		.word	kernel_len

		.word	initrd_start
		.word	initrd_addr
		.word	initrd_len

		.word	initrd_virt
		.word	initrd_len
		.word	params

		.type	kernel_start,#object
		.type	initrd_start,#object
