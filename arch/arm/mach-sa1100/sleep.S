/*
 * SA11x0 Assembler Sleep/WakeUp Management Routines
 *
 * Copyright (c) 2001 Cliff Brake <cbrake@accelent.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 *
 * History:
 *
 * 2001-02-06: Cliff Brake         Initial code
 *
 * 2001-08-29:	Nicolas Pitre	Simplified.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/hardware.h>
#include "sleep.h"

/*
 * sa1100_cpu_suspend()
 *
 * Causes sa11x0 to enter sleep state
 *
 */

	.text

ENTRY(sleep_save)	.word	0		@ virtual address of parameter array
ENTRY(sleep_save_p)	.word	0		@ physical address of parameter array

ENTRY(sa1100_cpu_suspend)

	@ save registers on stack
	stmfd	sp!, {r4 - r12, lr}

	@ load virtual address for sleep_save array
	ldr	r4, sleep_save

	@ save stack pointer
	str	sp, [r4, #(SLEEP_SAVE_SP*4)]

	@ save coprocessor registers
	mrc 	p15, 0, r1, c1, c0, 0
	str	r1, [r4, #(SLEEP_SAVE_CP15_R1*4)]
	mrc 	p15, 0, r1, c2, c0, 0
	str	r1, [r4, #(SLEEP_SAVE_CP15_R2*4)]
	mrc 	p15, 0, r1, c3, c0, 0
	str	r1, [r4, #(SLEEP_SAVE_CP15_R3*4)]
	mrc 	p15, 0, r1, c5, c0, 0
	str	r1, [r4, #(SLEEP_SAVE_CP15_R5*4)]
	mrc 	p15, 0, r1, c6, c0, 0
	str	r1, [r4, #(SLEEP_SAVE_CP15_R6*4)]
	mrc 	p15, 0, r1, c13, c0, 0
	str	r1, [r4, #(SLEEP_SAVE_CP15_R13*4)]

	@ clean data cache and invalidate WB
	bl	cpu_sa1100_cache_clean_invalidate_all

	@ disable clock switching
	mcr	p15, 0, r1, c15, c2, 2

        @ Adjust memory timing before lowering CPU clock
	@ Clock speed adjustment without changing memory timing makes
	@ CPU hang in some cases
        ldr     r0, =MDREFR
        ldr     r1, [r0]
        orr     r1, r1, #MDREFR_K1DB2
        str     r1, [r0]

	@ delay 90us and set CPU PLL to lowest speed
	@ fixes resume problem on high speed SA1110
	mov	r0, #90
	bl	SYMBOL_NAME(udelay)
	ldr	r0, =PPCR
	mov	r1, #0
	str	r1, [r0]
	mov	r0, #90
	bl	SYMBOL_NAME(udelay)


/* setup up register contents for jump to page containing SA1110 SDRAM controller bug fix suspend code
 *
 * r0 points to MSC0 register
 * r1 points to MSC1 register
 * r2 points to MSC2 register
 * r3 is MSC0 value
 * r4 is MSC1 value
 * r5 is MSC2 value
 * r6 points to MDREFR register
 * r7 is first MDREFR value
 * r8 is second MDREFR value
 * r9 is pointer to MDCNFG register
 * r10 is MDCNFG value
 * r11 is third MDREFR value
 * r12 is pointer to PMCR register
 * r13 is PMCR value (1)
 *
 */

	ldr	r0, =MSC0
	ldr	r1, =MSC1
	ldr	r2, =MSC2

        ldr     r3, [r0]
        bic     r3, r3, #FMsk(MSC_RT)
        bic     r3, r3, #FMsk(MSC_RT)<<16

        ldr     r4, [r1]
        bic     r4, r4, #FMsk(MSC_RT)
        bic     r4, r4, #FMsk(MSC_RT)<<16

        ldr     r5, [r2]
        bic     r5, r5, #FMsk(MSC_RT)
        bic     r5, r5, #FMsk(MSC_RT)<<16

        ldr     r6, =MDREFR

        ldr     r7, [r6]
        bic     r7, r7, #0x0000FF00
        bic     r7, r7, #0x000000F0
        orr     r8, r7, #MDREFR_SLFRSH

        ldr     r9, =MDCNFG
        ldr     r10, [r9]
        bic     r10, r10, #(MDCNFG_DE0+MDCNFG_DE1)
        bic     r10, r10, #(MDCNFG_DE2+MDCNFG_DE3)

        bic     r11, r8, #MDREFR_SLFRSH
        bic     r11, r11, #MDREFR_E1PIN

        ldr     r12, =PMCR

        mov     r13, #PMCR_SF

	b	sa1110_sdram_controller_fix

	.align 5
sa1110_sdram_controller_fix:

	@ Step 1 clear RT field of all MSCx registers
	str 	r3, [r0]
	str	r4, [r1]
	str	r5, [r2]

	@ Step 2 clear DRI field in MDREFR
	str	r7, [r6]

	@ Step 3 set SLFRSH bit in MDREFR
	str	r8, [r6]

	@ Step 4 clear DE bis in MDCNFG
	str	r10, [r9]

	@ Step 5 clear DRAM refresh control register
	str	r11, [r6]

	@ Wow, now the hardware suspend request pins can be used, that makes them functional for
	@ about 7 ns out of the	entire time that the CPU is running!

	@ Step 6 set force sleep bit in PMCR

	str	r13, [r12]

20:	b	20b			@ loop waiting for sleep

/*
 * cpu_sa1100_resume()
 *
 * entry point from bootloader into kernel during resume
 *
 */

	.align 5
ENTRY(sa1100_cpu_resume)

	@ set SVC, irqs off
	mov	r0, #I_BIT | MODE_SVC
	msr	cpsr_c, r0

	@ load physical address of sleep_save
	ldr	r4, sleep_save_p

	@ restore cp15_r3, domain id
	ldr	r1, [r4, #(SLEEP_SAVE_CP15_R3*4)]
	mcr 	p15, 0, r1, c3, c0 ,0

	@ restore cp15_r2, translation table base address
	ldr	r1, [r4, #(SLEEP_SAVE_CP15_R2*4)]
	mcr 	p15, 0, r1, c2, c0 ,0

	mov	r1, #0
	mcr	p15, 0, r1, c8, c7, 0   	@ flush I+D TLBs
	mcr	p15, 0, r1, c7, c7, 0		@ flush I&D cache

	@ get saved cp15 r1 (control register)
	ldr	r1, [r4, #(SLEEP_SAVE_CP15_R1*4)]

	@ get address to jump to after turning on MMU
	ldr	r2, =resume_after_mmu

	cmp	r2, #0

	b	resume_turn_on_mmu

	.align 5
resume_turn_on_mmu:

	@ turn on mmu
	mcr 	p15, 0, r1, c1, c0 ,0

	@ jump to resume_after_mmu
	mov	pc, r2
	nop
	nop

	.align 5
resume_after_mmu:

	@ load virtual address for sleep_save array
	ldr	r4, sleep_save

	@ restore the rest of CPU state
	ldr	r1, [r4, #(SLEEP_SAVE_CP15_R13*4)]
	mcr	p15, 0, r1, c13, c0, 0
	ldr	r1, [r4, #(SLEEP_SAVE_CP15_R5*4)]
	mcr 	p15, 0, r1, c5, c0 ,0
	ldr	r1, [r4, #(SLEEP_SAVE_CP15_R6*4)]
	mcr 	p15, 0, r1, c6, c0 ,0

	@ restore stack pointer
	ldr	sp, [r4, #(SLEEP_SAVE_SP*4)]

	@ return to caller
	ldmfd	sp!, {r4 - r12, pc}


