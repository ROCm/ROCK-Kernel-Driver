/*
 *  linux/arch/arm/boot/compressed/head.S
 *
 *  Copyright (C) 1996-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/linkage.h>

/*
 * Debugging stuff
 */
		.macro	kputc,val
		mov	r0, \val
		bl	putc
		.endm

		.macro	kphex,val,len
		mov	r0, \val
		mov	r1, #\len
		bl	phex
		.endm

		.macro	debug_reloc_start
#ifdef DEBUG
		kputc	#'\n'
		kphex	r6, 8
		kputc	#':'
		kphex	r5, 8
		kputc	#'-'
		kphex	r8, 8
		kputc	#'>'
		kphex	r4, 8
		kputc	#'\n'
#endif
		.endm

		.macro	debug_reloc_end
#ifdef DEBUG
		mov	r8, r0
		kphex	r5, 8
		kputc	#'-'
		kphex	r8, 8
		kputc	#'\n'
		mov	r0, r4
		bl	memdump
#endif
		.endm

/*
 * Note that these macros must not contain any code which is not
 * 100% relocatable.  Any attempt to do so will result in a crash.
 */
#if 0
		.macro	loadsp,	rb
		mov	\rb, #0x7c000000
		.endm

		.macro	writeb,	rb
		strb	\rb, [r3, #0x3f8]
		.endm
#else
		.macro	loadsp,	rb
		mov	\rb, #0x03000000
		orr	\rb, \rb, #0x00010000
		.endm

		.macro	writeb,	rb
		strb	\rb, [r3, #0x3f8 << 2]
		.endm
#endif


		.section ".start", #alloc, #execinstr
/*
 * sort out different calling conventions
 */
		.align
start:
		.type	start,#function
		.rept	8
		mov	r0, r0
		.endr

		b	1f
		.word	0x016f2818		@ Magic numbers to help the loader
		.word	start	
1:		mov	r7, r1			@ save architecture ID
		mov	r8, #0			@ save r0
#ifdef CONFIG_ANGELBOOT
		/*
		 * Booting from Angel - need to enter SVC mode and disable
		 * FIQs/IRQs (numeric definitions from angel arm.h source)
		 */
		mov	r0, #0x17		@ angel_SWIreason_EnterSVC
		swi	0x123456		@ angel_SWI_ARM
		mrs	r0, cpsr		@ turn off interrupts to
		orr	r0, r0, #0xc0		@ prevent angel from running
		msr	cpsr_c, r0

		/*
		 * Note that some cache flushing and other stuff may
		 * be needed here - is there an Angel SWI call for this?
		 */
#endif
		/*
		 * some architecture specific code can be inserted
		 * by the linker here, but it should preserve r7 and r8.
		 */

		.text
1:		mrc	p15, 0, r6, c0, c0	@ get processor ID
		adr	r2, LC0
		ldmia	r2, {r2, r3, r4, r5, sp}

		mov	r0, #0
1:		str	r0, [r2], #4		@ clear bss
		str	r0, [r2], #4
		str	r0, [r2], #4
		str	r0, [r2], #4
		cmp	r2, r3
		blt	1b

		bl	cache_on

		mov	r1, sp			@ malloc space above stack
		add	r2, sp, #0x10000	@ 64k max

		teq	r4, r5			@ will we overwrite ourselves?
		moveq	r5, r2
		movne	r5, r4

		mov	r0, r5
		mov	r3, r7
		bl	SYMBOL_NAME(decompress_kernel)

		teq	r4, r5			@ do we need to relocate
		beq	call_kernel		@ the kernel?

		add	r0, r0, #127
		bic	r0, r0, #127		@ align the kernel length
/*
 * r0     = decompressed kernel length
 * r1-r3  = unused
 * r4     = kernel execution address
 * r5     = decompressed kernel start
 * r6     = processor ID
 * r7     = architecture ID
 * r8-r14 = unused
 */
		add	r1, r5, r0		@ end of decompressed kernel
		adr	r2, reloc_start
		adr	r3, reloc_end
1:		ldmia	r2!, {r8 - r13}		@ copy relocation code
		stmia	r1!, {r8 - r13}
		ldmia	r2!, {r8 - r13}
		stmia	r1!, {r8 - r13}
		cmp	r2, r3
		blt	1b

		bl	cache_clean_flush
		add	pc, r5, r0		@ call relocation code

		.type	LC0, #object
LC0:		.word	__bss_start
		.word	_end
		.word	_load_addr
		.word	_start
		.word	user_stack+4096
		.size	LC0, . - LC0

/*
 * Turn on the cache.  We need to setup some page tables so that we
 * can have both the I and D caches on.
 *
 * We place the page tables 16k down from the kernel execution address,
 * and we hope that nothing else is using it.  If we're using it, we
 * will go pop!
 *
 * On entry,
 *  r4 = kernel execution address
 *  r6 = processor ID
 *  r7 = architecture number
 *  r8 = run-time address of "start"
 * On exit,
 *  r0, r1, r2, r3, r8, r9 corrupted
 * This routine must preserve:
 *  r4, r5, r6, r7
 */
		.align	5
cache_on:	ldr	r1, proc_sa110_type
		eor	r1, r1, r6
		movs	r1, r1, lsr #5		@ catch SA110 and SA1100
		beq	1f
		ldr     r1, proc_sa1110_type
		eor	r1, r1, r6
		movs	r1, r1, lsr #4
		movne	pc, lr
1:
		sub	r3, r4, #16384		@ Page directory size
		bic	r3, r3, #0xff		@ Align the pointer
		bic	r3, r3, #0x3f00
/*
 * Initialise the page tables, turning on the cacheable and bufferable
 * bits for the RAM area only.
 */
		mov	r0, r3
		mov	r8, r0, lsr #18
		mov	r8, r8, lsl #18		@ start of RAM
		add	r9, r8, #0x20000000	@ the maximum RAM size
		mov	r1, #0x12
		orr	r1, r1, #3 << 10
		add	r2, r3, #16384
1:		cmp	r1, r8			@ if virt > start of RAM
		orrge	r1, r1, #0x0c		@ set cacheable, bufferable
		cmp	r1, r9			@ if virt > end of RAM
		bicge	r1, r1, #0x0c		@ clear cacheable, bufferable
		str	r1, [r0], #4		@ 1:1 mapping
		add	r1, r1, #1048576
		teq	r0, r2
		bne	1b
/*
 * If ever we are running from Flash, then we surely want the cache
 * to be enabled also for our execution instance...  We map 2MB of it
 * so there is no map overlap problem for up to 1 MB compressed kernel.
 * If the execution is in RAM then we would only be duplicating the above.
 */
		mov	r1, #0x1e
		orr	r1, r1, #3 << 10
		mov	r2, pc, lsr #20
		orr	r1, r1, r2, lsl #20
		add	r0, r3, r2, lsl #2
		str	r1, [r0], #4
		add	r1, r1, #1048576
		str	r1, [r0]

		mov	r0, #0
		mcr	p15, 0, r0, c7, c10, 4	@ drain write buffer
		mcr	p15, 0, r0, c8, c7	@ flush I,D TLBs
		mcr	p15, 0, r3, c2, c0	@ load page table pointer
		mov	r0, #-1
		mcr	p15, 0, r0, c3, c0	@ load domain access register
		mrc	p15, 0, r0, c1, c0
		orr	r0, r0, #0x1000		@ I-cache enable
#ifndef DEBUG
		orr	r0, r0, #0x003d		@ Write buffer, mmu
#endif
		mcr	p15, 0, r0, c1, c0
		mov	pc, lr

/*
 * This code is relocatable.  It is relocated by the above code to the end
 * of the kernel and executed there.  During this time, we have no stacks.
 *
 * r0     = decompressed kernel length
 * r1-r3  = unused
 * r4     = kernel execution address
 * r5     = decompressed kernel start
 * r6     = processor ID
 * r7     = architecture ID
 * r8-r14 = unused
 */
		.align	5
reloc_start:	add	r8, r5, r0
		debug_reloc_start
		mov	r1, r4
1:
		.rept	4
		ldmia	r5!, {r0, r2, r3, r9 - r13}	@ relocate kernel
		stmia	r1!, {r0, r2, r3, r9 - r13}
		.endr

		cmp	r5, r8
		blt	1b
		debug_reloc_end

call_kernel:	bl	cache_clean_flush
		bl	cache_off
		mov	r0, #0
		mov	r1, r7			@ restore architecture number
		mov	pc, r4			@ call kernel

/*
 * Here follow the relocatable cache support functions for
 * the various processors.
 */

		.type	proc_sa110_type,#object
proc_sa110_type:
		.word	0x4401a100
		.size	proc_sa110_type, . - proc_sa110_type

		.type	proc_sa1110_type,#object
proc_sa1110_type:
		.word	0x6901b110
		.size	proc_sa1110_type, . - proc_sa1110_type

/*
 * Turn off StrongARM cache and MMU.  It is safe to
 * leave the I-cache on.
 *
 * On entry,
 *  r6 = processor ID
 * On exit,
 *  r0, r1 corrupted
 * This routine must preserve:
 *  r4, r6, r7
 */
		.align	5
cache_off:	ldr	r1, proc_sa110_type
		eor	r1, r1, r6
		movs	r1, r1, lsr #5		@ catch SA110 and SA1100
		beq	1f
		ldr	r1, proc_sa1110_type
		eor	r1, r1, r6
		movs	r1, r1, lsr #4
		movne	pc, lr
1:
		mrc	p15, 0, r0, c1, c0
		bic	r0, r0, #0x000d
		mcr	p15, 0, r0, c1, c0
		mov	pc, lr		

/*
 * Clean and flush the cache to maintain consistency.
 *
 * On entry,
 *  r6 = processor ID
 * On exit,
 *  r1, r2, r12 corrupted
 * This routine must preserve:
 *  r4, r6, r7
 */
		.align	5
cache_clean_flush:
		ldr	r1, proc_sa110_type
		eor	r1, r1, r6
		movs	r1, r1, lsr #5		@ catch SA110 and SA1100
		beq	1f
		ldr	r1, proc_sa1110_type
		eor	r1, r1, r6
		movs	r1, r1, lsr #4
		movne	pc, lr
1:
		bic	r1, pc, #31
		add	r2, r1, #32768
1:		ldr	r12, [r1], #32		@ s/w flush D cache
		teq	r1, r2
		bne	1b

		mcr	p15, 0, r1, c7, c7, 0	@ flush I cache
		mcr	p15, 0, r1, c7, c10, 4	@ drain WB
		mov	pc, lr

/*
 * Various debugging routines for printing hex characters and
 * memory, which again must be relocatable.
 */
#ifdef DEBUG
		.type	phexbuf,#object
phexbuf:	.space	12
		.size	phexbuf, . - phexbuf

phex:		adr	r3, phexbuf
		mov	r2, #0
		strb	r2, [r3, r1]
1:		subs	r1, r1, #1
		movmi	r0, r3
		bmi	puts
		and	r2, r0, #15
		mov	r0, r0, lsr #4
		cmp	r2, #10
		addge	r2, r2, #7
		add	r2, r2, #'0'
		strb	r2, [r3, r1]
		b	1b

puts:		loadsp	r3
1:		ldrb	r2, [r0], #1
		teq	r2, #0
		moveq	pc, lr
2:		writeb	r2
		mov	r1, #0x00020000
3:		subs	r1, r1, #1
		bne	3b
		teq	r2, #'\n'
		moveq	r2, #'\r'
		beq	2b
		teq	r0, #0
		bne	1b
		mov	pc, lr
putc:
		mov	r2, r0
		mov	r0, #0
		loadsp	r3
		b	2b

memdump:	mov	r12, r0
		mov	r10, lr
		mov	r1, #8
		bl	phex
		mov	r0, #'\n'
		bl	putc
		mov	r11, #0
2:		mov	r0, r11, lsl #2
		mov	r1, #4
		bl	phex
		mov	r0, #':'
		bl	putc
1:		mov	r0, #' '
		bl	putc
		ldr	r0, [r12, r11, lsl #2]
		mov	r1, #8
		bl	phex
		and	r0, r11, #7
		teq	r0, #3
		moveq	r0, #' '
		bleq	putc
		and	r0, r11, #7
		add	r11, r11, #1
		teq	r0, #7
		bne	1b
		mov	r0, #'\n'
		bl	putc
		cmp	r11, #64
		blt	2b
		mov	pc, r10
#endif

reloc_end:

		.align
		.section	".stack"
user_stack:	.space	4096
