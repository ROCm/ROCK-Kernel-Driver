/*
 *  linux/arch/sh/boot/compressed/head.S
 *
 *  Copyright (C) 1999 Stuart Menefy
 */

.text

#include <linux/linkage.h>

	.global	startup
startup:
	/* Load initial status register */
	mov.l   init_sr, r1
	ldc     r1, sr

	/* First clear BSS */
	mov.l	end_addr, r1
	mov.l	bss_start_addr, r2
	mov	#0, r0
l1:
	mov.l	r0, @-r1
	cmp/eq	r1,r2
	bf	l1

	/* Set the initial pointer. */
	mov.l	init_stack_addr, r0
	mov.l	@r0, r15

	/* Decompress the kernel */
	mov.l	decompress_kernel_addr, r0
	jsr	@r0
	nop

	/* Jump to the start of the decompressed kernel */
	mov.l	kernel_start_addr, r0
	jmp	@r0
	nop
	
	.align	2
bss_start_addr:
	.long	__bss_start
end_addr:
	.long	_end
init_sr:
	.long	0x40000000	/* Privileged mode, Bank=0, Block=0, I3-I0=0 */
init_stack_addr:
	.long	stack_start
decompress_kernel_addr:
	.long	decompress_kernel
kernel_start_addr:
	.long	_text+0x1000

	.align	9
fake_headers_as_bzImage:
	.word	0
	.ascii	"HdrS"		! header signature
	.word	0x0202		! header version number (>= 0x0105)
				! or else old loadlin-1.5 will fail)
	.word	0		! default_switch
	.word	0		! SETUPSEG
	.word	0x1000
	.word	0		! pointing to kernel version string
	.byte	0		! = 0, old one (LILO, Loadlin,
				! 0xTV: T=0 for LILO
				!       V = version
	.byte	1		! Load flags bzImage=1
	.word	0x8000		! size to move, when setup is not
	.long	0x100000	! 0x100000 = default for big kernel
	.long	0		! address of loaded ramdisk image
	.long	0		# its size in bytes
