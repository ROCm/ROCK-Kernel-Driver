/* 
 *   Creation Date: <2003/01/24 13:54:52 samuel>
 *   Time-stamp: <2003/08/14 03:12:00 samuel>
 *   
 *	<emuaccel.S>
 *	
 *	Emulation acceleration
 *   
 *   Copyright (C) 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#include "emuaccel_sh.h"

	// emuaccel registers:
	//
	//	r2	= emuaccel_slot
	//	r6,r7	= nip(return address)/mregs
	//
	
	balign_8
emuaccel_mtmsr_rNN:
mFORLOOP([i],0,31,[
	LOAD_EMUGPR_IM r0,i
	b	emulate_mtmsr_accel
])
emuaccel_mtsrr0_rNN:
mFORLOOP([i],0,31,[
	.if (i <= 7)
		LOAD_EMUGPR_IM r0,i
		b	emulate_mtsrr0_accel
	.else
		stw	rPREFIX[]i,xSRR0(r1)
		b	emulation_done_noinc
	.endif
])
emuaccel_mtsrr1_rNN:
mFORLOOP([i],0,31,[
	.if (i <= 7)
		LOAD_EMUGPR_IM r0,i
		b	emulate_mtsrr1_accel
	.else
		stw	rPREFIX[]i,xSRR1(r1)
		b	emulation_done_noinc
	.endif
])
emuaccel_mtsprg0_rNN:
mFORLOOP([i],0,31,[
	.if (i <= 7)
		LOAD_EMUGPR_IM r0,i
		b	emulate_mtsprg0_accel
	.else
		stw	rPREFIX[]i,xSPRG0(r1)
		b	emulation_done_noinc
	.endif
])
emuaccel_mtsprg1_rNN:
mFORLOOP([i],0,31,[
	.if (i <= 7)
		LOAD_EMUGPR_IM r0,i
		b	emulate_mtsprg1_accel
	.else
		stw	rPREFIX[]i,xSPRG1(r1)
		b	emulation_done_noinc
	.endif
])
emuaccel_mtsprg2_rNN:
mFORLOOP([i],0,31,[
	.if (i <= 7)
		LOAD_EMUGPR_IM r0,i
		b	emulate_mtsprg2_accel
	.else
		stw	rPREFIX[]i,xSPRG2(r1)
		b	emulation_done_noinc
	.endif
])
emuaccel_mtsprg3_rNN:
mFORLOOP([i],0,31,[
	.if (i <= 7)
		LOAD_EMUGPR_IM r0,i
		b	emulate_mtsprg3_accel
	.else
		stw	rPREFIX[]i,xSPRG3(r1)
		b	emulation_done_noinc
	.endif
])
emuaccel_mthid0_rNN:
mFORLOOP([i],0,31,[
	.if (i <= 7)
		LOAD_EMUGPR_IM r0,i
		b	emulate_mthid0_accel
	.else
		stw	rPREFIX[]i,xHID0(r1)
		b	emulation_done_noinc
	.endif
])

emuaccel_nop:
	b	emulation_done_noinc

emuaccel_rfi:
	addi	r6,r6,-4			// point nip to the rfi instruction
	b	emulate_rfi

emuaccel_update_dec:
	lwz	r4,xDEC_STAMP(r1)
	mftb	r3
	sub	r0,r4,r3
	stw	r0,xDEC(r1)	
	b	emulation_done_noinc

	balign_8
emuaccel_mtsr:
	lwz	r4,12(r2)			// emuaccel paramamter (opcode)
	addi	r6,r6,-4			// back NIP
	rlwinm	r5,r4,14,24,28			// reg_num << 3
	b	emulate_mtsr


/************************************************************************/
/*	implementation							*/
/************************************************************************/
	
emulate_mtsrr0_accel:
	stw	r0,xSRR0(r1)
	b	emulation_done_noinc
emulate_mtsrr1_accel:
	stw	r0,xSRR1(r1)
	b	emulation_done_noinc
emulate_mtsprg0_accel:
	stw	r0,xSPRG0(r1)
	b	emulation_done_noinc
emulate_mtsprg1_accel:
	stw	r0,xSPRG1(r1)
	b	emulation_done_noinc
emulate_mtsprg2_accel:
	stw	r0,xSPRG2(r1)
	b	emulation_done_noinc
emulate_mtsprg3_accel:
	stw	r0,xSPRG3(r1)
	b	emulation_done_noinc
emulate_mthid0_accel:	
	stw	r0,xHID0(r1)
	b	emulation_done_noinc
	
	balign_32
emulate_mtmsr_accel:
	lwz	r3,xMSR(r1)			// r3 = old MSR
	bl	msr_altered

	GET_TICK_CNT(entry, "mtmsr-accel")
	BUMP("emulate_mtmsr-accel")

	beq+	cr1,emulation_done_noinc	// no doze... we are done
	MAC_EXIT_SAVE( RVEC_MSR_POW )		// doze


	
/************************************************************************/
/*	setup								*/
/************************************************************************/
	
#define EMUACCEL_REL(s)			s - emuaccel_table

	.text	70			/* this table does not need to be relocated */
	// format: emuaccel_inst, offset, table_index_mask
GLOBAL_SYMBOL(emuaccel_table):
	.long	EMUACCEL_MTMSR,		EMUACCEL_REL( emuaccel_mtmsr_rNN ),	0x1f
	.long	EMUACCEL_MTSRR0,	EMUACCEL_REL( emuaccel_mtsrr0_rNN ),	0x1f
	.long	EMUACCEL_MTSRR1,	EMUACCEL_REL( emuaccel_mtsrr1_rNN ),	0x1f
	.long	EMUACCEL_MTSPRG0,	EMUACCEL_REL( emuaccel_mtsprg0_rNN ),	0x1f
	.long	EMUACCEL_MTSPRG1,	EMUACCEL_REL( emuaccel_mtsprg1_rNN ),	0x1f
	.long	EMUACCEL_MTSPRG2,	EMUACCEL_REL( emuaccel_mtsprg2_rNN ),	0x1f
	.long	EMUACCEL_MTSPRG3,	EMUACCEL_REL( emuaccel_mtsprg3_rNN ),	0x1f
	.long	EMUACCEL_MTHID0,	EMUACCEL_REL( emuaccel_mthid0_rNN ),	0x1f
	.long	EMUACCEL_RFI,		EMUACCEL_REL( emuaccel_rfi ),		0
	.long	EMUACCEL_UPDATE_DEC,	EMUACCEL_REL( emuaccel_update_dec ),	0
	.long	EMUACCEL_MTSR,		EMUACCEL_REL( emuaccel_mtsr ),		0
	.long	EMUACCEL_NOP,		EMUACCEL_REL( emuaccel_nop ),		0
	.long	0, 0, 0	/* end marker */

	.text
