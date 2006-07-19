/* 
 *   Creation Date: <1999/09/26 01:02:58 samuel>
 *   Time-stamp: <2003/07/27 19:20:24 samuel>
 *   
 *	<asmfuncs.h>
 *	
 *	Exports from <base.S>
 *   
 *   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _H_ASMFUNCS
#define _H_ASMFUNCS

#include "kernel_vars.h" 
#include "tlbie.h"


/* globl variable defined in actions.c */
extern int reloc_virt_offs;
#define reloc_ptr( v )  ((ulong)(v) + (ulong)reloc_virt_offs)


/* The code in base.o (all low-level assembly) are copied to a physically 
 * continuous memory area. The following inline functions maps function calls
 * to the relocated area.
 */

static inline void msr_altered( kernel_vars_t *kv ) {
	typedef void ftype( kernel_vars_t * );
	extern ftype r__msr_altered;
	(*(ftype*)reloc_ptr( r__msr_altered ))( kv );
}

static inline void invalidate_splitmode_sr( kernel_vars_t *kv ) {
	typedef void ftype( kernel_vars_t *);
	extern ftype r__invalidate_splitmode_sr;
	(*(ftype*)reloc_ptr( r__invalidate_splitmode_sr ))( kv );
}

static inline void initialize_spr_table( kernel_vars_t *kv ) {
	typedef void ftype( kernel_vars_t *);
	extern ftype r__initialize_spr_table;
	(*(ftype*)reloc_ptr( r__initialize_spr_table ))( kv );
}


/************************************************************************/
/*	misc inlines							*/
/************************************************************************/

#define _sync() ({ asm volatile("sync ;\n isync" : : ); })

static inline ulong _get_sdr1( void ) {
	ulong sdr1;
	asm volatile("mfsdr1 %0" : "=r" (sdr1) : );
	return sdr1;
}
static inline void _set_sdr1( ulong sdr1 ) {
	asm volatile("mtsdr1 %0" : : "r" (sdr1) );
}

static inline int cpu_is_601( void ) {
	ulong pvr;
	asm volatile("mfpvr %0" : "=r" (pvr) : );
	return (pvr>>16)==1;
}

static inline int cpu_is_603( void ) {
	ulong pvr;
	asm volatile("mfpvr %0" : "=r" (pvr) : );
	pvr = pvr >> 16;
	return pvr==3 || pvr==6 || pvr==7;	/* 603, 603e, 603ev */
}
#endif   /* _H_ASMFUNCS */
