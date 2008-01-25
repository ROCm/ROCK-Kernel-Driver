/*
 *   Creation Date: <2004/01/29 20:12:41 samuel>
 *   Time-stamp: <2004/03/06 13:17:36 samuel>
 *
 *	<asmdbg.h>
 *
 *	debug support
 *
 *   Copyright (C) 2004 Samuel Rydh (samuel@ibrium.se)
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *
 */

#ifndef _H_ASMDBG
#define _H_ASMDBG


/************************************************************************/
/*	Performance Statistics						*/
/************************************************************************/

#ifdef PERFORMANCE_INFO
	define([_bump_ind_], 0)

#define __BUMP( str )						\
	.text	92						;\
debug_str_[]_bump_ind_:						;\
	.if (_bump_ind_ >= NUM_ASM_BUMP_CNTRS)			;\
	.print "** too many BUMP counters **" ; .fail 1		;\
	.endif							;\
	.ascii	str "\0"					;\
	balign_4						;\
	.text	90						;\
	.long	(debug_str_[]_bump_ind_-__start_bumptable)	;\
	.text							;\
	stw	r3,xDEBUG_SCR1(r1)				;\
	lwz	r3,(K_ASM_BUMP_CNTRS+4*_bump_ind_)(r1)		;\
	addi	r3,r3,1						;\
	stw	r3,(K_ASM_BUMP_CNTRS+4*_bump_ind_)(r1)		;\
	lwz	r3,xDEBUG_SCR1(r1)				;\
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
	.long	(debug_str_[]_bump_ind_-__start_bumptable)	;\
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
	LOADI	_scr,MOL_BIT(2) | MOL_BIT(3) | MOL_BIT(31)	// count in SV-mode if PM is zero.
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
/*	debug								*/
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


#endif   /* _H_ASMDBG */
