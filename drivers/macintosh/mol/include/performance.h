/* 
 *   Creation Date: <2001/04/01 00:44:40 samuel>
 *   Time-stamp: <2003/01/27 02:42:03 samuel>
 *   
 *	<performance.h>
 *	
 *	performance counters
 *   
 *   Copyright (C) 2001, 2002 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _H_PERFORMANCE
#define _H_PERFORMANCE

typedef struct {
	char 		*name;
	unsigned long 	*ctrptr;
} perf_info_t;

extern perf_info_t g_perf_info_table[];

#if defined(PERFORMANCE_INFO) && !defined(PERFORMANCE_INFO_LIGHT)
#define BUMP(x)		do { extern int gPerf__##x; gPerf__##x++; } while(0)
#define BUMP_N(x,n)	do { extern int gPerf__##x; gPerf__##x+=(n); } while(0)
#else
#define BUMP(x)		do {} while(0)
#define BUMP_N(x,n)	do {} while(0)
#endif


/************************************************************************/
/*	tick counters							*/
/************************************************************************/

#ifdef PERFORMANCE_INFO

#define TICK_CNTR_PUSH( kv ) do {				\
	int ind = (kv)->num_acntrs;				\
	acc_counter_t *c = &(kv)->acntrs[ind];			\
	if( ind < MAX_ACC_CNTR_DEPTH ) {			\
		c->subticks=0;					\
		(kv)->num_acntrs++;				\
		asm volatile( "mftb %0" : "=r" (c->stamp) : );	\
	}							\
} while(0)
	
#define TICK_CNTR_POP( kv, name ) do {				\
	int ind = (kv)->num_acntrs;				\
	ulong now, ticks;					\
	asm volatile( "mftb %0" : "=r" (now) : );		\
	if( --ind >= 0 ) {					\
		acc_counter_t *c = &(kv)->acntrs[ind];		\
		(kv)->num_acntrs = ind;				\
		ticks = now - c->stamp - c->subticks;		\
		BUMP_N( name##_ticks, ticks );			\
		if( ind )					\
			(kv)->acntrs[ind-1].subticks += ticks;	\
	}							\
} while(0)

#else
#define TICK_CNTR_PUSH( kv )		do {} while(0)
#define TICK_CNTR_POP( kv, name )	do {} while(0)
#endif

#endif   /* _H_PERFORMANCE */
