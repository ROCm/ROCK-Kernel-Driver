/*
 *  This file contains the power_save function for 6xx & 7xxx CPUs
 *  rewritten in assembler
 *
 *  Warning ! This code assumes that if your machine has a 750fx
 *  it will have PLL 1 set to low speed mode (used during NAP/DOZE).
 *  if this is not the case some additional changes will have to
 *  be done to check a runtime var (a bit like powersave-nap)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/threads.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/offsets.h>

#undef DEBUG

	.text

/*
 * Init idle, called at early CPU setup time from head.S for each CPU
 * Make sure no rest of NAP mode remains in HID0, save default
 * values for some CPU specific registers. Called with r24
 * containing CPU number and r3 reloc offset
 */
 	.globl	init_idle_power4
init_idle_power4:
BEGIN_FTR_SECTION
	mfspr	r4,SPRN_HID0
	rlwinm	r4,r4,0,10,8	/* Clear NAP */
	mtspr	SPRN_HID0, r4
END_FTR_SECTION_IFSET(CPU_FTR_CAN_NAP)
	blr

/*
 * Here is the power_save_6xx function. This could eventually be
 * split into several functions & changing the function pointer
 * depending on the various features.
 */
	.globl	power4_idle
power4_idle:
	/* Check if we can nap or doze, put HID0 mask in r3
	 */
	lis	r3, 0
BEGIN_FTR_SECTION
	/* We must dynamically check for the NAP feature as it
	 * can be cleared by CPU init after the fixups are done
	 */
	lis	r4,cur_cpu_spec@ha
	lwz	r4,cur_cpu_spec@l(r4)
	lwz	r4,CPU_SPEC_FEATURES(r4)
	andi.	r0,r4,CPU_FTR_CAN_NAP
	beq	1f
	/* Now check if user or arch enabled NAP mode */
	lis	r4,powersave_nap@ha
	lwz	r4,powersave_nap@l(r4)
	cmpi	0,r4,0
	beq	1f
	lis	r3,HID0_NAP@h
1:	
END_FTR_SECTION_IFSET(CPU_FTR_CAN_NAP)
	cmpi	0,r3,0
	beqlr

	/* Clear MSR:EE */
	mfmsr	r7
	rlwinm	r0,r7,0,17,15
	mtmsr	r0

	/* Check current_thread_info()->flags */
	rlwinm	r4,r1,0,0,18
	lwz	r4,TI_FLAGS(r4)
	andi.	r0,r4,_TIF_NEED_RESCHED
	beq	1f
	mtmsr	r7	/* out of line this ? */
	blr
1:	
	/* Go to NAP now */	
	mfspr	r4,SPRN_HID0
	lis	r5,(HID0_NAP|HID0_SLEEP)@h
	andc	r4,r4,r5
	or	r4,r4,r3
	oris	r4,r4,HID0_DPM@h	/* that should be done once for all  */
	mtspr	SPRN_HID0,r4
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
BEGIN_FTR_SECTION
	DSSALL
	sync
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
	ori	r7,r7,MSR_EE /* Could be ommited (already set) */
	oris	r7,r7,MSR_POW@h
	sync
	isync
	mtmsr	r7
	isync
	sync
	blr
	
	.globl powersave_nap
powersave_nap:
	.long	0
