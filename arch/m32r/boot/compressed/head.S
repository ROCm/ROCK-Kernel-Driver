/*
 *  linux/arch/m32r/boot/compressed/head.S
 *
 *  Copyright (c) 2001-2003	Hiroyuki Kondo, Hirokazu Takata,
 *				Hitoshi Yamamoto, Takeo Takahashi
 */

	.text
#include <linux/config.h>
#include <linux/linkage.h>
#include <asm/addrspace.h>
#include <asm/page.h>
#include <asm/assembler.h>
#include "boot.h"

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
	mv	r12, r0 		/* size of decompressed kernel */

/*
 * relocate copy routine & jump routine
 */
	LDIMM	(r1, BOOT_RELOC_ADDR)
	mv	r5, r1		; save reloc addr to jump

	LDIMM	(r2, startup_reloc)
	LDIMM	(r3, exit_reloc)
	sub	r3, r2		; relocated code size in bytes
	mv	r4, r3
	srli	r4, #2		; R4 = code size in longwords (rounded down)
	addi	r1, #-4		; account for pre-inc store
	beqz	r4, 2f		; any more to go?
	.fillinsn
1:
	ld	r6, @r2+	; code to be relocated
	st	r6, @+r1	; relocate code
	addi	r4, #-1		; decrement count
	bnez	r4, 1b		; go do some more
	.fillinsn
2:
	and3	r4, r3, #3	; get no. of remaining bytes
	addi	r1, #4		; account for pre-inc store
	beqz	r4, 4f		; any more to go?
	.fillinsn
3:
	ldb	r6, @r2		; code to be relocated
	stb	r6, @r1		; relocate code
	addi	r1, #1		; bump address
	addi	r2, #1		; bump address
	addi	r4, #-1		; decrement count
	bnez	r4, 3b		; go do some more
	.fillinsn
4:
	jmp	r5		; jump to relocated code

/*
 * startup_reloc runs on BOOT_RELOC_ADDR.
 * copy decompressed kernel to original location
 */
	.text
	__ALIGN
startup_reloc:
	LDIMM	(r1, CONFIG_MEMORY_START)
	LDIMM	(r2, KERNEL_DECOMPRESS_ADDR)
	mv	r4, r12		; r12 holds size of decompressed kernel
	srli	r4, #2		; R4 = code size in longwords (rounded down)
	addi	r1, #-4		; account for pre-inc store
	beqz	r4, 2f		; any more to go?
	.fillinsn
1:
	ld	r6, @r2+	; code to be relocated
	st	r6, @+r1	; relocate code
	addi	r4, #-1		; decrement count
	bnez	r4, 1b		; go do some more
	.fillinsn
2:
	and3	r4, r12, #3	; get no. of remaining bytes
	addi	r1, #4		; account for pre-inc store
	beqz	r4, 4f		; any more to go?
	.fillinsn
3:
	ldb	r6, @r2		; code to be relocated
	stb	r6, @r1		; relocate code
	addi	r1, #1		; bump address
	addi	r2, #1		; bump address
	addi	r4, #-1		; decrement count
	bnez	r4, 3b		; go do some more
	.fillinsn
4:
	/*
	 * invalidate i-cache before jump to kernel
	 */
#if defined(CONFIG_CHIP_VDEC2)
	ldi	r0, #-1
	ldi	r1, #0xc0	; invalidate i-cache
	stb	r1, @r0
#elif defined(CONFIG_CHIP_XNUX2)
	ldi	r0, #-2
	ldi	r1, #0x0100	; invalidate
	sth	r1, @r0
#elif defined(CONFIG_CHIP_M32700)
	ldi	r0, #-1		; MCCR(cache control register)
	ldi	r1, #0xc0	; invalidate i-cache
	stb	r1, @r0
#else
#error unknown chip configuration
#endif
	LDIMM	(r0, KERNEL_ENTRY)
	jmp	r0				/* jump to kernel */

	__ALIGN
exit_reloc:

	.balign 4096
.fake_empty_zero_page:
	/* FIXME: correct table value */
	.word	0
	.ascii	"HdrS"
	.word	0x0202
	.word	0
	.word	0
	.word	0x1000
	.word	0
	.byte	0
	.byte	1
	.word	0x8000
	.long	0
	.long	0

	.section	.fake_eit_vector, "aw"
	.long	0

