/*
 * include/asm-arm/arch-ixp4xx/entry-macro.S
 *
 * Low-level IRQ helper macros for IXP4xx-based platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr	\irqstat, =(IXP4XX_INTC_BASE_VIRT+IXP4XX_ICIP_OFFSET)
		ldr	\irqstat, [\irqstat]		@ get interrupts
		cmp	\irqstat, #0
		beq	1001f
		clz     \irqnr, \irqstat
		mov     \base, #31
		subs    \irqnr, \base, \irqnr

1001:
		.endm


