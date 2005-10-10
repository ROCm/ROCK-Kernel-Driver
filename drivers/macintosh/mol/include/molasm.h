/*   -*- asm -*-
 *
 *   Creation Date: <2001/01/28 20:33:22 samuel>
 *   Time-stamp: <2003/08/11 21:32:57 samuel>
 *   
 *	<molasm.h>
 *	
 *	Utility assembly macros
 *   
 *   Copyright (C) 2001, 2002, 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _H_MOLASM
#define _H_MOLASM


/************************************************************************/
/*	SPRG usage							*/
/************************************************************************/

/* Darwin and Linux uses the sprg's differently. Linux uses sprg0/1 in
 * the exception vectors while Darwin uses sprg2/3.
 */
#ifdef __linux__
define([mfsprg_a0], [mfsprg0])
define([mfsprg_a1], [mfsprg1])
define([mfsprg_a2], [mfsprg2])
define([mfsprg_a3], [mfsprg3])
define([mtsprg_a0], [mtsprg0])
define([mtsprg_a1], [mtsprg1])
define([mtsprg_a2], [mtsprg2])
define([mtsprg_a3], [mtsprg3])
#else
define([mfsprg_a0], [mfsprg2])
define([mfsprg_a1], [mfsprg3])
define([mfsprg_a2], [mfsprg0])
define([mfsprg_a3], [mfsprg1])
define([mtsprg_a0], [mtsprg2])
define([mtsprg_a1], [mtsprg3])
define([mtsprg_a2], [mtsprg0])
define([mtsprg_a3], [mtsprg1])
#endif


/************************************************************************/
/*	Utility								*/
/************************************************************************/

MACRO(LOAD_VARIABLE, [reg, offs], [
	lis	_reg,HA(k_mol_stack + _offs)
	lwz	_reg,LO(k_mol_stack + _offs)(_reg)
])

MACRO(SET_SESSION_TABLE, [reg], [
	lis	_reg,HA(EXTERN(Symbol_SESSION_TABLE))
	addi	_reg,_reg,LO(EXTERN(Symbol_SESSION_TABLE))
])


/************************************************************************/
/*	GPR save / restore						*/
/************************************************************************/

MACRO(xGPR_SAVE, [reg], [
	stw	rPREFIX[]_reg,(xGPR0 + _reg*4)(r1)
])
		
MACRO(xGPR_LOAD, [reg], [
	lwz	rPREFIX[]_reg,(xGPR0 + _reg*4)(r1)
])

	
/************************************************************************/
/*	FPU misc							*/
/************************************************************************/

MACRO(ENABLE_MSR_FP, [scr], [
	mfmsr	_scr
	ori	_scr,_scr,MSR_FP
	mtmsr	_scr
	isync
])
		
	
/************************************************************************/
/*	Performance Statistics						*/
/************************************************************************/

#ifdef PERFORMANCE_INFO
	define([_bump_ind_], 0)
		
#define __BUMP( str )					\
	.text	92					;\
debug_str_[]_bump_ind_:					;\
	.if (_bump_ind_ >= NUM_ASM_BUMP_CNTRS)		;\
	.print "** too many BUMP counters **" ; .fail 1	;\
	.endif						;\
	.ascii	str "\0"				;\
	balign_4					;\
	.text	90					;\
	.long	debug_str_[]_bump_ind_			;\
	.text						;\
	stw	r3,xDEBUG_SCR1(r1)			;\
	lwz	r3,(K_ASM_BUMP_CNTRS+4*_bump_ind_)(r1)	;\
	addi	r3,r3,1					;\
	stw	r3,(K_ASM_BUMP_CNTRS+4*_bump_ind_)(r1)	;\
	lwz	r3,xDEBUG_SCR1(r1)			;\
	define([_bump_ind_],eval(_bump_ind_+1))


	define([_tick_ind_], 0)
	
#define __ZERO_TICK_CNT(cntr)					\
	ifdef([##cntr##_ind_],[],[				\
	  define([##cntr##_ind_], _tick_ind_)			\
	  define([_tick_ind_], eval(_tick_ind_+1))		\
	])							\
	.if (_tick_ind_ > NUM_ASM_TICK_CNTRS)			;\
	.print "** too many TICK counters **" ; .fail 1		;\
	.endif							;\
	stw	r3,xDEBUG_SCR1(r1)				;\
	mftb	r3						;\
	stw	r3,(K_ASM_TICK_STAMPS + 4*cntr##_ind_)(r1)	;\
	lwz	r3,xDEBUG_SCR1(r1)
	
#define __GET_TICK_CNT(cntr, name) \
	.text	92						;\
debug_str_[]_bump_ind_:						;\
	.if (_bump_ind_ >= NUM_ASM_BUMP_CNTRS)			;\
	.print "** too many BUMP counters **" ; .fail 1		;\
	.endif							;\
	.ascii	name "_ticks\0"					;\
	balign_4						;\
	.text	90						;\
	.long	debug_str_[]_bump_ind_				;\
	.text							;\
	ifdef([##cntr##_ind_],[],[				\
	  define([##cntr##_ind_], _tick_ind_)			\
	  define([_tick_ind_], eval(_tick_ind_+1))		\
	])							\
	.if (_tick_ind_ > NUM_ASM_TICK_CNTRS)			;\
	.print "** too many TICK counters **" ; .fail 1		;\
	.endif							;\
	stw	r3,xDEBUG_SCR1(r1)				;\
	mftb	r3						;\
	stw	r4,xDEBUG_SCR2(r1)				;\
	lwz	r4,(K_ASM_TICK_STAMPS + 4*cntr##_ind_)(r1)	;\
	sub	r3,r3,r4					;\
	lwz	r4,(K_ASM_BUMP_CNTRS+4*_bump_ind_)(r1)		;\
	add	r4,r4,r3					;\
	stw	r4,(K_ASM_BUMP_CNTRS+4*_bump_ind_)(r1)		;\
	lwz	r3,xDEBUG_SCR1(r1)				;\
	mftb	r4						;\
	stw	r4,(K_ASM_TICK_STAMPS + 4*cntr##_ind_)(r1)	;\
	lwz	r4,xDEBUG_SCR2(r1)				;\
	define([_bump_ind_],eval(_bump_ind_+1))

#endif /* PERFORMANCE_INFO */

#ifndef PERFORMANCE_INFO_LIGHT
#define BUMP(s)			__BUMP(s)
#define ZERO_TICK_CNT(c)	__ZERO_TICK_CNT(c)
#define GET_TICK_CNT(c, name)	__GET_TICK_CNT(c,name)
#else
#define BUMP(s)
#define ZERO_TICK_CNT(c)
#define GET_TICK_CNT(c, name)
#endif

#ifndef __BUMP
#define __BUMP(str)
#define __ZERO_TICK_CNT(cntr)
#define __GET_TICK_CNT(cntr, name)
#endif

#ifdef PERF_MONITOR
MACRO(PERF_MONITOR_GET, [
	stw	r5,xDEBUG_SCR1(r1)
	mfspr	r5,S_PMC2
	stw	r4,xDEBUG_SCR2(r1)
	mfmsr	r4
	ori	r4,r4,MSR_PE
	mtmsr	r4
	stw	r5,xDEBUG0(r1)
	li	r5,0
	mtspr	S_PMC2,r5
	lwz	r4,xDEBUG_SCR2(r1)
	lwz	r5,xDEBUG_SCR1(r1)
])
MACRO(PERF_MONITOR_SETUP, [scr], [
	LOADI	_scr,BIT(2) | BIT(3) | BIT(31)	// count in SV-mode if PM is zero.
	mtspr	S_MMCR0,_scr
	li	_scr,0
	mtspr	S_MMCR1,_scr
	li	_scr,0
	mtspr	S_PMC2,_scr
])
#else /* PERF_MONITOR */
#define PERF_MONITOR_GET
MACRO(PERF_MONITOR_SETUP, [scr], [])
#endif

	
/************************************************************************/
/*	D E B U G							*/
/************************************************************************/

MACRO(STOP_EMULATION, [val], [
	stw	r3,xDEBUG_SCR1(r1)
	li	r3,_val
	stw	r3,xKERNEL_DBG_STOP(r1)
	li	r3,1
	stw	r3,xINTERRUPT(r1)
	lwz	r3,xDEBUG_SCR1(r1)	
])

MACRO(DEBUG_TRACE, [num, dummy], [
	stw	r3,xDEBUG_SCR1(r1)
	lwz	r3,xDEBUG_TRACE(r1)
	addi	r3,r3,1
	stw	r3,xDEBUG_TRACE(r1)
	stw	r3,(xDEBUG0+4*_num)(r1)
	lwz	r3,xDEBUG_SCR1(r1)
])

MACRO(TRACE_VAL, [val, dummy], [
#if DBG_TRACE
	stw	r30,xDEBUG_SCR1(r1)
	stw	r29,xDEBUG_SCR2(r1)
	lwz	r30,xDEBUG_TRACE(r1)
	rlwinm	r30,r30,0,24,31			// 256 entries
	rlwinm	r30,r30,2,22,29
	addi	r30,r30,xDBG_TRACE_SPACE
	lis	r29,HA(_val)
	addi	r29,r29,LO(_val)
	stwx	r29,r30,r1
	lwz	r30,xDEBUG_TRACE(r1)
	addi	r30,r30,1
	rlwinm	r30,r30,0,24,31			// 256 entries
	stw	r30,xDEBUG_TRACE(r1)
	lwz	r29,xDEBUG_SCR2(r1)	
	lwz	r30,xDEBUG_SCR1(r1)
#endif
])
#define TRACE( a,b ) TRACE_VAL a,b

	
#endif   /* _H_MOLASM */
