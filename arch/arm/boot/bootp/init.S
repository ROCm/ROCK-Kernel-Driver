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
_entry:		adr	r10, initdata
		ldr	r11, initdata
		sub	r11, r10, r11		@ work out exec offset
		b	splitify
		.size	_entry,. - _entry

		.type	initdata, #object
initdata:	.word	initdata		@ compiled address of this
		.size	initdata,. - initdata

		.text
splitify:	adr	r13, data
		ldmia	r13!, {r4-r6}		@ move the initrd
		add	r4, r4, r11		@ correction
		bl	move

		ldmia	r13!, {r4-r6}		@ then the kernel
		mov	r12, r5
		add	r4, r4, r11		@ correction
		bl	move

/*
 * Setup the initrd parameters to pass to the kernel.  This can either be
 * passed in via a param_struct or a tag list.  We spot the param_struct
 * method by looking at the first word; this should either indicate a page
 * size of 4K, 16K or 32K.
 */
		ldmia	r13, {r5-r8}		@ get size and addr of initrd
						@ r5 = ATAG_INITRD
						@ r6 = initrd start
						@ r7 = initrd end
						@ r8 = param_struct address
		ldr	r9, [r8, #0]		@ no param struct?
		teq	r9, #0x1000		@ 4K?
		teqne	r9, #0x4000		@ 16K?
		teqne	r9, #0x8000		@ 32K?
		beq	no_taglist

/*
 * find the end of the tag list, and then add an INITRD tag on the end.
 */
taglist:	ldr	r9, [r8, #0]		@ tag length
		teq	r9, #0			@ last tag?
		addne	r8, r8, r9
		bne	taglist

		mov	r4, #16			@ length of initrd tag
		mov	r9, #0			@ end of tag list terminator
		stmia	r8, {r4, r5, r6, r7, r9}
		mov	pc, r12			@ call kernel

no_taglist:	add	r8, r8, #16*4
		stmia	r8, {r6,r7}		@ save in param_struct
		mov	pc, r12			@ call kernel

move:		ldmia	r4!, {r7 - r10}		@ move 32-bytes at a time
		stmia	r5!, {r7 - r10}
		ldmia	r4!, {r7 - r10}
		stmia	r5!, {r7 - r10}
		subs	r6, r6, #8 * 4
		bcs	move
		mov	pc, lr

data:		.word	initrd_start
		.word	initrd_addr
		.word	initrd_len

		.word	kernel_start
		.word	kernel_addr
		.word	kernel_len

		.word	0x54410005		@ r5 = ATAG_INITRD
		.word	initrd_virt		@ r6
		.word	initrd_len		@ r7
		.word	params			@ r8

		.type	kernel_start,#object
		.type	initrd_start,#object
