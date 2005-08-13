/* 
 *   Creation Date: <97/06/24 22:25:04 samuel>
 *   Time-stamp: <2003/08/18 23:20:07 samuel>
 *   
 *	<mac_registers.h>
 *	
 *	
 *   
 *   Copyright (C) 1997-2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _MAC_REGISTERS_H
#define _MAC_REGISTERS_H

#ifndef __ASSEMBLY__

#include "mmutypes.h"
#include "processor.h"

typedef struct {
	unsigned long	h,l;
} fpu_register;

#define NUM_DEBUG_REGS		10

typedef struct {
	unsigned long	words[4];
} altivec_reg_t;

typedef struct mac_regs {			/* this struct is page aligned */
	/* the sprs should be page aligned (occupies one page) */
	unsigned long	spr[NUM_SPRS];		/* special purpose registers  */

	unsigned long	segr[16];		/* segment registers */
	altivec_reg_t	vec[32];		/* AltiVec vector registers */
	fpu_register	fpr[32];		/* FPU registers (64 bits) */

	unsigned long	vscr_pad[3], vscr;	/* AltiVec status control register */
	unsigned long	pad_fpscr, fpscr;	/* fp. status and control register */
	unsigned long	pad_ef, emulator_fpscr;	/* emulator fp. status and control reg */

	/* Keep this cache-block aligned (typcically 8 words) */
	unsigned long	cr;			/* Condition register */
	unsigned long	link;			/* Link register */
	unsigned long	flag_bits;		/* Various flags (fbXXXXX) */
	unsigned long	inst_opcode;		/* opcode of instruction */
	unsigned long 	gpr[32];		/* gprs */

	unsigned long	ctr;			/* Count register */
	unsigned long	xer;			/* Integer exception register */
	unsigned long	nip;			/* Instruction pointer */
	unsigned long	msr;			/* Machine state register (virtual) */

	/* interrupts and timers */
	int		interrupt;		/* set if the kernel should return to the emulator */
	int		in_virtual_mode;	/* set while MOL is in virtualization mode */
	ulong		dec_stamp;		/* xDEC = dec_stamp - tbl */
	ulong		timer_stamp;		/* TIMER = dec_stamp - tbl */
	int		obsolete_irq;		/* unused */

	/* RVEC parameters */
	ulong		rvec_param[3];		/* Used in kernel C-mode */

	/* misc */
	int		fpu_state;		/* FPU_STATE_xxx (see below) */
	int   		processor;		/* processor to emulate, 1=601, 4=604 */
	int		altivec_used;		/* useful for altivec detection */
	int		no_altivec;		/* Don't use altivec (e.g. no kernel support) */

	int		use_bat_hack;		/* Newworld BAT optimization (HACK) */

#ifdef EMULATE_603
	unsigned long	gprsave_603[4];		/* GPR0-3 (for 603 DMISS/IMISS) */
#endif
	/* moldeb support */
	unsigned long	mdbg_ea_break;		/* used together with BREAK_EA_PAGE */

	/* DEBUG */
	unsigned long   debug[NUM_DEBUG_REGS];
	unsigned long	debug_scr1;		/* dbg scratch register */
	unsigned long	debug_scr2;		/* dbg scratch register */
	unsigned long	debug_trace;		/* dbg trace register */
	unsigned long	dbg_trace_space[256];
	unsigned long	dbg_last_rvec;		/* useful for tracing segfaults etc. */
	unsigned long	dbg_last_osi;

	unsigned long	kernel_dbg_stop;	/* stop emulation flag */
} mac_regs_t;


#define	BIT(n)			(1U<<(31-(n)))	/* bit 0 is MSB */

#ifndef __KERNEL__
extern mac_regs_t *mregs;
#endif
#endif /* __ASSEMBLY__ */

/* mregs->fpu_state (only valid when FBIT_FPUInUse is false) */
#define FPU_STATE_HALF_SAVED	0	/* fpscr & fr0-fr13 saved */
#define FPU_STATE_DIRTY		1	/* fpscr & fr13 saved */
#define FPU_STATE_SAVED		3	/* everything is saved to mregs */

/* flag_bits (loaded into cr4-7). TOUCH THESE *ONLY* FROM THE MAIN THREAD! */
#ifdef __KERNEL__
/* these must be in cr7 (set through a mtcrf) */
#define MMU_CR_FIELD		0x01
#define FBIT_InSplitmode	31	/* (S) */
#define FBIT_PrepareSplitmode	30	/* (S) */
#define FBIT_LoadSegreg		29	/* (S) */

/* must be in cr6; (set through a mtcrf) */
#define TRACE_CR_FIELD		0x02
#define FBIT_DbgTrace		27	/* (S) equals BREAK_SINGLE_STEP */
#define FBIT_Trace		26	/* (S) */

#define FBIT_MolDecLoaded	23	/* (S) */
#define FBIT_DecSeenZero	22	/* (S) */
#define FBIT_DecINT		21	/* (S) */
#define FBIT_FPUInUse		20	/* (S) Set when fpu is mac-owned (only valid in the kernel) */

#endif

#define FBIT_MsrModified	19	/* (U) */
#define FBIT_RecalcDecInt	18	/* (U) */
#define FBIT_IRQPending		17	/* (U) IRQ is pending */
#ifdef EMULATE_603
#define FBIT_603_AltGPR		16	/* (U) Alternate GPR0-3 in use */
#endif


#ifdef __KERNEL__
#define fb_DbgTrace		BIT( FBIT_DbgTrace )
#define fb_Trace		BIT( FBIT_Trace )
#define fb_PrepareSplitmode	BIT( FBIT_PrepareSplitmode )
#define fb_InSplitmode		BIT( FBIT_InSplitmode )
#define fb_LoadSegreg		BIT( FBIT_LoadSegreg )
#endif
#define fb_MsrModified		BIT( FBIT_MsrModified )
#define fb_RecalcDecInt		BIT( FBIT_RecalcDecInt )
#define fb_IRQPending		BIT( FBIT_IRQPending )
#ifdef EMULATE_603
#define fb_603_AltGPR		BIT( FBIT_603_AltGPR )
#endif

#endif /* _MAC_REGISTERS_H */
