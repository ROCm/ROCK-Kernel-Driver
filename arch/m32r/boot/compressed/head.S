/*
 *  linux/arch/m32r/boot/compressed/head.S
 *
 *  Copyright (c) 2001-2003	Hiroyuki Kondo, Hirokazu Takata,
 *				Hitoshi Yamamoto, Takeo Takahashi
 *  Copyright (c) 2004		Hirokazu Takata
 */

	.text
#include <linux/config.h>
#include <linux/linkage.h>
#include <asm/addrspace.h>
#include <asm/page.h>
#include <asm/assembler.h>

	.global	startup
	__ALIGN
startup:
	ldi	r0, #0x0000			/* SPI, disable EI */
	mvtc	r0, psw

/*
 * Clear BSS first so that there are no surprises...
 */
#ifdef CONFIG_ISA_DUAL_ISSUE

	LDIMM	(r2, __bss_start)
	LDIMM	(r3, _end)
	sub	r3, r2		; BSS size in bytes
	; R4 = BSS size in longwords (rounded down)
	mv	r4, r3		    ||	ldi	r1, #0
	srli	r4, #4		    ||	addi	r2, #-4
	beqz	r4, .Lendloop1
.Lloop1:
#ifndef CONFIG_CHIP_M32310
	; Touch memory for the no-write-allocating cache.
	ld	r0, @(4,r2)
#endif
	st	r1, @+r2	    ||	addi	r4, #-1
	st	r1, @+r2
	st	r1, @+r2
	st	r1, @+r2	    ||	cmpeq	r1, r4	; R4 = 0?
	bnc	.Lloop1
.Lendloop1:
	and3	r4, r3, #15
	addi	r2, #4
	beqz	r4, .Lendloop2
.Lloop2:
	stb	r1, @r2		    ||	addi	r4, #-1
	addi	r2, #1
	bnez	r4, .Lloop2
.Lendloop2:

#else /* not CONFIG_ISA_DUAL_ISSUE */

	LDIMM	(r2, __bss_start)
	LDIMM	(r3, _end)
	sub	r3, r2		; BSS size in bytes
	mv	r4, r3
	srli	r4, #2		; R4 = BSS size in longwords (rounded down)
	ldi	r1, #0		; clear R1 for longwords store
	addi	r2, #-4		; account for pre-inc store
	beqz	r4, .Lendloop1	; any more to go?
.Lloop1:
	st	r1, @+r2	; yep, zero out another longword
	addi	r4, #-1		; decrement count
	bnez	r4, .Lloop1	; go do some more
.Lendloop1:
	and3	r4, r3, #3	; get no. of remaining BSS bytes to clear
	addi	r2, #4		; account for pre-inc store
	beqz	r4, .Lendloop2	; any more to go?
.Lloop2:
	stb	r1, @r2		; yep, zero out another byte
	addi	r2, #1		; bump address
	addi	r4, #-1		; decrement count
	bnez	r4, .Lloop2	; go do some more
.Lendloop2:

#endif /* not CONFIG_ISA_DUAL_ISSUE */

	seth	r0, #shigh(stack_start)
	ld	sp, @(r0, low(stack_start))	/* set stack point */

/*
 * decompress the kernel
 */
	bl	decompress_kernel

#if defined(CONFIG_CHIP_M32700)
	/* Cache flush */
	ldi	r0, -1
	ldi	r1, 0xd0	; invalidate i-cache, copy back d-cache
	stb	r1, @r0
#else
#error "put your cache flush function, please"
#endif
        seth	r0, #high(CONFIG_MEMORY_START)
        or3	r0, r0, #0x2000
        jmp	r0

	.balign 512
fake_headers_as_bzImage:
	.short	0
	.ascii	"HdrS"
	.short	0x0202
	.short	0
	.short	0
	.byte	0x00, 0x10
	.short	0
	.byte	0
	.byte	1
	.byte	0x00, 0x80
	.long	0
	.long	0

