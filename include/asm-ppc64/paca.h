#ifndef _PPC64_PACA_H
#define _PPC64_PACA_H

/*============================================================================
 *                                                         Header File Id
 * Name______________:	paca.h
 *
 * Description_______:
 *
 * This control block defines the PACA which defines the processor 
 * specific data for each logical processor on the system.  
 * There are some pointers defined that are utilized by PLIC.
 *
 * C 2001 PPC 64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */    
#include	<asm/types.h>

#define N_EXC_STACK    2

/*-----------------------------------------------------------------------------
 * Other Includes
 *-----------------------------------------------------------------------------
 */
#include	<asm/iSeries/ItLpPaca.h>
#include	<asm/iSeries/ItLpRegSave.h>
#include	<asm/iSeries/ItLpQueue.h>
#include	<asm/rtas.h>
#include	<asm/mmu.h>
#include	<asm/processor.h>

extern struct paca_struct paca[];
register struct paca_struct *local_paca asm("r13");
#define get_paca()	local_paca

/*============================================================================
 * Name_______:	paca
 *
 * Description:
 *
 *	Defines the layout of the paca.  
 *
 *	This structure is not directly accessed by PLIC or the SP except
 *	for the first two pointers that point to the ItLpPaca area and the
 *	ItLpRegSave area for this processor.  Both the ItLpPaca and
 *	ItLpRegSave objects are currently contained within the
 *	PACA but they do not need to be.
 *
 *============================================================================
 */
struct paca_struct {
/*=====================================================================================
 * CACHE_LINE_1 0x0000 - 0x007F
 *=====================================================================================
 */
	struct ItLpPaca *xLpPacaPtr;	/* Pointer to LpPaca for PLIC		0x00 */
	struct ItLpRegSave *xLpRegSavePtr; /* Pointer to LpRegSave for PLIC	0x08 */
	u64 xCurrent;  		        /* Pointer to current			0x10 */
	u16 xPacaIndex;			/* Logical processor number		0x18 */
	u16 active;			/* Is this cpu active?			0x1a */
	u32 default_decr;		/* Default decrementer value		0x1c */	
	u64 unused1;
	u64 xKsave;			/* Saved Kernel stack addr or zero	0x28 */
	u64 pvr;			/* Processor version register		0x30 */
	u8 *exception_sp;		/*					0x38 */

	struct ItLpQueue *lpQueuePtr;	/* LpQueue handled by this processor    0x40 */
	u64  xTOC;			/* Kernel TOC address			0x48 */
	STAB xStab_data;		/* Segment table information		0x50,0x58,0x60 */
	u8 xSegments[STAB_CACHE_SIZE];	/* Cache of used stab entries		0x68,0x70 */
	u8 xProcEnabled;		/* 1=soft enabled			0x78 */
	u8 unused2;
	u8 prof_enabled;		/* 1=iSeries profiling enabled          0x7A */
	u8 stab_cache_pointer;	
	u8 resv1[4];			/*					0x7B-0x7F */

/*=====================================================================================
 * CACHE_LINE_2 0x0080 - 0x00FF
 *=====================================================================================
 */
	u64 spare1;			/*					0x00 */
	u64 spare2;			/*					0x08 */
	u64 spare3;			/*					0x10 */
	u64 spare4;		/*					0x18 */
	u64 next_jiffy_update_tb;	/* TB value for next jiffy update	0x20 */
	u32 lpEvent_count;		/* lpEvents processed			0x28 */
	u32 prof_multiplier;		/*					0x2C */
	u32 prof_counter;		/*					0x30 */
	u32 prof_shift;			/* iSeries shift for profile bucket size0x34 */
	u32 *prof_buffer;		/* iSeries profiling buffer		0x38 */
	u32 *prof_stext;		/* iSeries start of kernel text		0x40 */
	u32 prof_len;			/* iSeries length of profile buffer -1	0x48 */
	u8  rsvd2[128-76];		/*					0x4C */

/*=====================================================================================
 * CACHE_LINE_3 0x0100 - 0x017F
 *=====================================================================================
 */
	u8		xProcStart;	/* At startup, processor spins until	0x100 */
  					/* xProcStart becomes non-zero. */
	u8		rsvd3[127];

/*=====================================================================================
 * CACHE_LINE_4-8  0x0180 - 0x03FF Contains ItLpPaca
 *=====================================================================================
 */
	struct ItLpPaca xLpPaca;	/* Space for ItLpPaca */

/*=====================================================================================
 * CACHE_LINE_9-16 0x0400 - 0x07FF Contains ItLpRegSave
 *=====================================================================================
 */
	struct ItLpRegSave xRegSav;	/* Register save for proc */

/*=====================================================================================
 * CACHE_LINE_17-18 0x0800 - 0x0EFF Reserved
 *=====================================================================================
 */
	struct rtas_args xRtas;		/* Per processor RTAS struct */
	u64 xR1;			/* r1 save for RTAS calls */
	u64 xSavedMsr;			/* Old msr saved here by HvCall */
	u8 rsvd5[256-16-sizeof(struct rtas_args)];

/*=====================================================================================
 * CACHE_LINE_19-30 0x0800 - 0x0EFF Reserved
 *=====================================================================================
 */
	u8 rsvd6[0x600];

/*=====================================================================================
 * CACHE_LINE_31 0x0F00 - 0x0F7F Exception stack
 *=====================================================================================
 */
	u8 exception_stack[N_EXC_STACK*EXC_FRAME_SIZE];

/*=====================================================================================
 * CACHE_LINE_32 0x0F80 - 0x0FFF Reserved
 *=====================================================================================
 */
	u8 rsvd7[0x80];                  /* Give the stack some rope ... */

/*=====================================================================================
 * Page 2 Reserved for guard page.  Also used as a stack early in SMP boots before
 *        relocation is enabled.
 *=====================================================================================
 */
	u8 guard[0x1000];               /* ... and then hang 'em         */ 
};

#endif /* _PPC64_PACA_H */
