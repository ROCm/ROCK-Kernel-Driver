/*
 *  linux/arch/arm/kernel/debug-armv.S
 *
 *  Copyright (C) 1994-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  32-bit debugging code
 */
#include <linux/config.h>
#include <linux/linkage.h>
#include <asm/hardware.h>
#include <asm/hardware/dec21285.h>

		.text

/*
 * Some debugging routines (useful if you've got MM problems and
 * printk isn't working).  For DEBUGGING ONLY!!!  Do not leave
 * references to these in a production kernel!
 */
#if defined(CONFIG_ARCH_RPC)
		.macro	addruart,rx
		mov	\rx, #0xe0000000
		orr	\rx, \rx, #0x00010000
		orr	\rx, \rx, #0x00000fe0
		.endm

		.macro	senduart,rd,rx
		strb	\rd, [\rx]
		.endm

		.macro	busyuart,rd,rx
1001:		ldrb	\rd, [\rx, #0x14]
		and	\rd, \rd, #0x60
		teq	\rd, #0x60
		bne	1001b
		.endm

		.macro	waituart,rd,rx
1001:		ldrb	\rd, [\rx, #0x18]
		tst	\rd, #0x10
		beq	1001b
		.endm

#elif defined(CONFIG_ARCH_EBSA110)
		.macro	addruart,rx
		mov	\rx, #0xf0000000
		orr	\rx, \rx, #0x00000be0
		.endm

		.macro	senduart,rd,rx
		strb	\rd, [\rx]
		.endm

		.macro	busyuart,rd,rx
1002:		ldrb	\rd, [\rx, #0x14]
		and	\rd, \rd, #0x60
		teq	\rd, #0x60
		bne	1002b
		.endm

		.macro	waituart,rd,rx
1001:		ldrb	\rd, [\rx, #0x18]
		tst	\rd, #0x10
		beq	1001b
		.endm

#elif defined(CONFIG_FOOTBRIDGE)
#ifndef CONFIG_DEBUG_DC21285_PORT
	/* For NetWinder debugging */
		.macro	addruart,rx
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		moveq	\rx, #0x7c000000	@ physical
		movne	\rx, #0xff000000	@ virtual
		orr	\rx, \rx, #0x000003f8
		.endm

		.macro	senduart,rd,rx
		strb	\rd, [\rx]
		.endm

		.macro	busyuart,rd,rx
1002:		ldrb	\rd, [\rx, #0x5]
		and	\rd, \rd, #0x60
		teq	\rd, #0x60
		bne	1002b
		.endm

		.macro	waituart,rd,rx
1001:		ldrb	\rd, [\rx, #0x6]
		tst	\rd, #0x10
		beq	1001b
		.endm
#else
	/* For EBSA285 debugging */
		.equ	dc21285_high, ARMCSR_BASE & 0xff000000
		.equ	dc21285_low,  ARMCSR_BASE & 0x00ffffff

		.macro	addruart,rx
		mov	\rx, #dc21285_high
		.if	dc21285_low
		orr	\rx, \rx, #dc21285_low
		.endif
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #0x160]	@ UARTDR
		.endm

		.macro	busyuart,rd,rx
1001:		ldr	\rd, [\rx, #0x178]	@ UARTFLG
		tst	\rd, #1 << 3
		bne	1001b
		.endm

		.macro	waituart,rd,rx
		.endm
#endif
#elif defined(CONFIG_ARCH_NEXUSPCI)
		.macro	addruart,rx
		ldr	\rx, =0xfff00000
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #0xc]
		.endm

		.macro	busyuart,rd,rx
1001:		ldr	\rd, [\rx, #0x4]
		tst	\rd, #1 << 0
		bne	1001b
		.endm

		.macro	waituart,rd,rx
		.endm

#elif defined(CONFIG_ARCH_SA1100)
		.macro	addruart,rx
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		moveq	\rx, #0x80000000	@ physical base address
		movne	\rx, #0xf8000000	@ virtual address
		add	\rx, \rx, #0x00050000	@ Ser3
		@add	\rx, \rx, #0x00010000	@ Ser1
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #0x14]	@ UTDR
		.endm

		.macro	waituart,rd,rx
1001:		ldr	\rd, [\rx, #0x20]	@ UTSR1
		tst	\rd, #1 << 2		@ UTSR1_TNF
		beq	1001b
		.endm

		.macro	busyuart,rd,rx
1001:		ldr	\rd, [\rx, #0x20]	@ UTSR1
		tst	\rd, #1 << 0		@ UTSR1_TBY
		bne	1001b
		.endm

#elif defined(CONFIG_ARCH_L7200)

		.equ	io_virt, IO_BASE
		.equ	io_phys, IO_START

		.macro	addruart,rx
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		moveq	\rx, #io_phys		@ physical base address
		movne	\rx, #io_virt		@ virtual address
		add	\rx, \rx, #0x00044000	@ Ser1
@		add	\rx, \rx, #0x00045000	@ Ser2
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #0x0]	@ UARTDR1
		.endm

		.macro	waituart,rd,rx
1001:		ldr	\rd, [\rx, #0x18]	@ UARTFLG
		tst	\rd, #1 << 5		@ UARTFLGUTXFF - 1 when full
		bne	1001b
		.endm

		.macro	busyuart,rd,rx
1001:		ldr	\rd, [\rx, #0x18]	@ UARTFLG
		tst	\rd, #1 << 3		@ UARTFLGUBUSY - 1 when busy
		bne	1001b
		.endm

#elif defined(CONFIG_ARCH_INTEGRATOR)

#include <asm/hardware/serial_amba.h>

		.equ	io_virt, 0xf0000000 + (0x16000000 >> 4)
		.equ	io_phys, 0x16000000

		.macro	addruart,rx
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		moveq	\rx, #0x16000000	@ physical base address
		movne	\rx, #0xf0000000	@ virtual base
		addne	\rx, \rx, #0x16000000 >> 4
		.endm

		.macro	senduart,rd,rx
		strb	\rd, [\rx, #AMBA_UARTDR]
		.endm

		.macro	waituart,rd,rx
1001:		ldr	\rd, [\rx, #0x18]	@ UARTFLG
		tst	\rd, #1 << 5		@ UARTFLGUTXFF - 1 when full
		bne	1001b
		.endm

		.macro	busyuart,rd,rx
1001:		ldr	\rd, [\rx, #0x18]	@ UARTFLG
		tst	\rd, #1 << 3		@ UARTFLGUBUSY - 1 when busy
		bne	1001b
		.endm

#else
#error Unknown architecture
#endif

/*
 * Useful debugging routines
 */
ENTRY(printhex8)
		mov	r1, #8
		b	printhex

ENTRY(printhex4)
		mov	r1, #4
		b	printhex

ENTRY(printhex2)
		mov	r1, #2
printhex:	adr	r2, hexbuf
		add	r3, r2, r1
		mov	r1, #0
		strb	r1, [r3]
1:		and	r1, r0, #15
		mov	r0, r0, lsr #4
		cmp	r1, #10
		addlt	r1, r1, #'0'
		addge	r1, r1, #'a' - 10
		strb	r1, [r3, #-1]!
		teq	r3, r2
		bne	1b
		mov	r0, r2
		b	printascii

		.ltorg

ENTRY(printascii)
		addruart r3
		b	2f
1:		waituart r2, r3
		senduart r1, r3
		busyuart r2, r3
		teq	r1, #'\n'
		moveq	r1, #'\r'
		beq	1b
2:		teq	r0, #0
		ldrneb	r1, [r0], #1
		teqne	r1, #0
		bne	1b
		mov	pc, lr

ENTRY(printch)
		addruart r3
		mov	r1, r0
		mov	r0, #0
		b	1b

hexbuf:		.space 16
