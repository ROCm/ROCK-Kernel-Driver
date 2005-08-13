/* 
 *   Creation Date: <2001/01/26 21:33:45 samuel>
 *   Time-stamp: <2003/08/12 00:26:22 samuel>
 *   
 *	<return_vectors.h>
 *	
 *	Possible mac-return vectors (see mainloop.S)
 *   
 *   Copyright (C) 2000, 2001, 2002, 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _H_RVEC
#define _H_RVEC


/* ---------------------------------------------------------------------------- */

#define NRVECS_LOG2			6
#define NUM_RVECS			64		/* = 2 ^ NRVECS_LOG2 */
#define RVEC_MASK			(NUM_RVECS-1)

/* ---------------------------------------------------------------------------- */

#define	RVEC_NOP			0		/* Must be zero */
#define RVEC_ENABLE_FPU			3		/* Load up FPU */

#define RVEC_TRACE_TRAP			6
#define RVEC_ISI_TRAP			7		/* r4=nip, r5=srr1 */
#define RVEC_DSI_TRAP			8		/* r4=dar, r5=srr1 */
#define RVEC_ALIGNMENT_TRAP		9		/* r4=dar, r5=srr1 */
#ifdef EMULATE_603
#define RVEC_DMISS_LOAD_TRAP		10
#define RVEC_DMISS_STORE_TRAP		11
#define RVEC_IMISS_TRAP			12
#endif

#define RVEC_SPR_READ			13 		/* r4=spr#, r5=gprnum */
#define RVEC_SPR_WRITE			14		/* r4=spr#, r5=value */
#define RVEC_PRIV_INST			15		/* r4=opcode */
#define RVEC_ILLEGAL_INST		16		/* r4=opcode */

#define RVEC_UNUSUAL_PROGRAM_EXCEP	17		/* r4=opcode, r5=srr1 */

#define RVEC_ALTIVEC_UNAVAIL_TRAP	18
#define RVEC_ALTIVEC_ASSIST		19		/* r4=srr1 */
#define RVEC_ENABLE_ALTIVEC		20

#define	RVEC_EXIT			21
/* 22 was RVEC_INTERRUPT */
#define RVEC_OSI_SYSCALL		23
#define RVEC_TIMER			24

#define RVEC_IO_READ			25
#define RVEC_IO_WRITE			26

#define RVEC_MSR_POW			27		/* (MSR_POW 0->1) => doze */

/* error/debug */
#define RVEC_UNUSUAL_DSISR_BITS		28		/* dar, dsisr (bit 0,5,9 or 11 was set) */
#define RVEC_MMU_IO_SEG_ACCESS		29		/* IO segment access (more or less unused) */
#define	RVEC_INTERNAL_ERROR		30
#define	RVEC_DEBUGGER			31
#define RVEC_BREAK			32		/* r4 = break_flag */
#define RVEC_BAD_NIP			33		/* r4 = phys_nip */
#define RVEC_OUT_OF_MEMORY		34		/* fatal out of memory... */


/************************************************************************/
/*	MOL kernel/emulator switch magic				*/
/************************************************************************/

/* magic to be loaded into r4/r5 before the illegal instruction is issued */
#define MOL_ENTRY_R4_MAGIC		0x7ba5
#define MOL_INITIALIZE_FLAG		0x8000
#define MOL_KERNEL_ENTRY_MAGIC		mfmsr	r0	/* any privileged instruction will do */


/************************************************************************/
/*	Kernel definitions						*/
/************************************************************************/

#if defined(__KERNEL__) && !defined( __ASSEMBLY__ )

#define RVEC_RETURN_1( mregsptr, rvec, arg1 ) \
	({ (mregsptr)->rvec_param[0] = (ulong)(arg1); \
	  return rvec; })

#define RVEC_RETURN_2( mregsptr, rvec, arg1, arg2 ) \
	({ (mregsptr)->rvec_param[0] = (ulong)(arg1); \
	  (mregsptr)->rvec_param[1] = (ulong)(arg2); \
	  return rvec; })

#define RVEC_RETURN_3( mregsptr, rvec, arg1, arg2, arg3 ) \
	({ (mregsptr)->rvec_param[0] = (ulong)(arg1); \
	  (mregsptr)->rvec_param[1] = (ulong)(arg2); \
	  (mregsptr)->rvec_param[2] = (ulong)(arg3); \
	  return rvec; })

#endif /* !__ASSEMBLY__ && __KERNEL__ */


/************************************************************************/
/*	userspace definitions						*/
/************************************************************************/

#if !defined(__KERNEL__) || defined(__MPC107__)

#if !defined(__ASSEMBLY__)
#if !defined(__KERNEL__)

typedef struct {
	int		vnum;
	void		*vector;
	const char	*name;
} rvec_entry_t;

extern void 		rvec_init( void );
extern void		rvec_cleanup( void );
extern void		set_rvector( uint vnum, void *vector, const char *vector_name );
extern void		set_rvecs( rvec_entry_t *table, int tablesize );
#endif

/* this struct is private to rvec.c/mainloop.S (offsets are HARDCODED) */
typedef struct {
	int 		(*rvec)( int rvec /*, arguments */ );
	int 		dbg_count;
	const char 	*name;
	int		filler;
} priv_rvec_entry_t;
#endif /* __ASSEMBLY__ */

#define RVEC_ESIZE_LOG2		4	/* 2^n = sizeof(priv_rvec_entry_t) */
#define RVEC_ESIZE		16	/* sizeof(priv_rvec_entry_t) */

#endif /* __KERNEL__ || __MPC107__ */

#endif   /* _H_RVEC */
