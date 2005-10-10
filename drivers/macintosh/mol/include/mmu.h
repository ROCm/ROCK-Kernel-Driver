/* 
 *   Creation Date: <1998-11-11 13:55:49 samuel>
 *   Time-stamp: <2004/02/28 19:20:25 samuel>
 *   
 *	<mmu.h>
 *	
 *	MMU related things
 *   
 *   Copyright (C) 1998-2004 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _H_MMU
#define _H_MMU

#ifndef __ASSEMBLY__
#include "kernel_vars.h"
#include "mac_registers.h"
#include "mmu_mappings.h"
#endif

#define IO_PAGE_MAGIC_1		0x10BACA57
#define IO_PAGE_MAGIC_2		0x135AB779

#ifndef __ASSEMBLY__
typedef struct io_page {		/* Must fit within a single 4K page */
	ulong		magic;		/* identifier 1 */
	ulong		magic2;		/* identifier 2 */

	ulong		me_phys;	/* physical address of this block */
	ulong		mphys;		/* mac-physical address of block */
	struct io_page 	*next;		/* next io_page */

	void		*usr_data[512];	/* usr data (grain=double word) */
} io_page_t;

/* globals from mmu.c */
extern mPTE_t 	*hw_hash_base;
extern ulong	hw_hash_mask;		/* PTEG index mask (_not_ PTE index mask) */
extern ulong	hw_pte_offs_mask;	/* pte = hw_hash_base + (pte & hw_pte_offs_mask) */

/* from mmu.c */
extern int 	init_mmu( kernel_vars_t *kv );
extern void 	cleanup_mmu( kernel_vars_t *kv );
extern void	do_flush( ulong vsid, ulong va, ulong *dummy, int num );
extern void	mmu_altered( kernel_vars_t *kv );
extern void	clear_vsid_refs( kernel_vars_t *kv );

extern void	share_pte_hash( kernel_vars_t *kv, ulong sdr1, mPTE_t *base );
extern int	create_pte_hash( kernel_vars_t *kv, int set_sdr1 );

/* arch/mmu.c */
extern int	arch_mmu_init( kernel_vars_t *kv );

/* from mmu_io.c */
struct pte_lvrange;
extern int 	init_mmu_io( kernel_vars_t *kv );
extern void 	cleanup_mmu_io( kernel_vars_t *kv );
extern int	add_io_trans( kernel_vars_t *kv, ulong mbase, int size, void *usr_data );
extern int 	remove_io_trans( kernel_vars_t *kv, ulong mbase, int size );
extern int 	mphys_to_pte( kernel_vars_t *kv, ulong mphys, ulong *pte1, int is_write, struct pte_lvrange **lvrange );
extern void	mmu_add_map( kernel_vars_t *kv, struct mmu_mapping *m );
extern void	mmu_remove_map( kernel_vars_t *kv, struct mmu_mapping *m );

/* from context.c */
extern int	init_contexts( kernel_vars_t *kv );
extern void	cleanup_contexts( kernel_vars_t *kv );
extern int	alloc_context( kernel_vars_t *kv );
extern void	handle_context_wrap( kernel_vars_t *kv, int nvsid_needed );

/* from mmu_fb.c */
extern int	init_mmu_fb( kernel_vars_t *kv );
extern void	cleanup_mmu_fb( kernel_vars_t *kv );
extern void	video_pte_inserted( kernel_vars_t *kv, char *lvptr, ulong *slot,
				    ulong pte0, ulong pte1, ulong ea );
extern void	setup_fb_acceleration( kernel_vars_t *kv, char *lvbase, int bytes_per_row, int height );
extern int	get_dirty_fb_lines( kernel_vars_t *kv, short *retbuf, int num_bytes );

/* from mmu_tracker.c */
extern int 	init_mmu_tracker( kernel_vars_t *kv );
extern void 	cleanup_mmu_tracker( kernel_vars_t *kv );
extern int 	track_lvrange( kernel_vars_t *kv );
extern size_t 	get_track_buffer( kernel_vars_t *kv, char *retbuf );
extern void 	set_track_buffer( kernel_vars_t *kv, char *buf );
extern void	lvpage_dirty( kernel_vars_t *kv, char *lvpage );

/* These functions should be used by the debugger only */
struct dbg_page_info;
extern int	dbg_get_PTE( kernel_vars_t *kv, int context, ulong va, mPTE_t *ret );
extern int 	dbg_get_linux_page( ulong va, struct dbg_page_info *r );
extern int	dbg_translate_ea( kernel_vars_t *kv, int context, ulong va, int *ret_mphys, int data_access );

/* arch functions */
extern ulong	get_phys_page( kernel_vars_t *kv, char *lvptr, int request_rw );

/************************************************************************/
/*   	Host OS Context Allocation					*/
/************************************************************************/

/* Number of linux-context to allocate for mol (64 is the ABSOLUTE MINIMUM) */
#define VSID_MASK		0xffffff

#ifdef __linux__
/*
 * The new MM implementation uses VSID
 *
 *	VSID = (((context * 897) << 4) + ((va>>28) * 0x111)) & 0xffffff
 *
 * Only context 0..32767 are used. We can use context 32768..0xfffff.
 * The old MM implementation used
 *
 *	VSID = (((context * 897) << 4) + (va>>28)) & 0xffffff
 */
#define CTX_MASK		0xfffff		
#define MUNGE_ADD_NEXT		897
#define MUNGE_MUL_INVERSE	2899073		/* Multiplicative inverse of 897 (modulo VSID_MASK+1) */
#define MUNGE_ESID_ADD		0x111
#define MUNGE_CONTEXT(c)	(((c) * 897) & CTX_MASK)

/* mol_contexts == (linux_context << 4) | esid */
#define PER_SESSION_CONTEXTS	0x10000		/* more than we will need (10^6) */
#define FIRST_MOL_CONTEXT(sess)	((CTX_MASK - PER_SESSION_CONTEXTS*((sess)+1)) << 4)
#define LAST_MOL_CONTEXT(sess)	(((CTX_MASK - PER_SESSION_CONTEXTS*(sess)) << 4) | 0xf)

#if FIRST_MOL_CONTEXT(MAX_NUM_SESSIONS-1) < (32768 << 4)
#error "Too many MOL contexts..."
#endif

#endif   /* __linux__ */


/************************* DARWIN ****************************/

#ifdef __darwin__
/* #include <Kernel/ppc/mappings.h> */

/*
 *	VSID = ((context * incrVSID) & 0x0fffff) + (((va>>28) << 20) & 0xf00000)
 */

#define CTX_MASK		0x0fffff	/* context number does not include top 4 bits */
#define MUNGE_ESID_ADD		0x100000	/* top 4 is segment id */

#define FIRST_MOL_CONTEXT	(CTX_MASK - NUM_MOL_CONTEXTS)
#define LAST_MOL_CONTEXT	(FIRST_MOL_CONTEXT + NUM_MOL_CONTEXTS - 1)
#define MUNGE_ADD_NEXT		( incrVSID )
#endif   /* __darwin__ */

#endif   /* __ASSEMBLY__ */
#endif   /* _H_MMU */


