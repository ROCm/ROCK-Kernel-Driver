/* 
 *   Creation Date: <1998-11-11 13:55:49 samuel>
 *   Time-stamp: <2004/02/28 19:20:23 samuel>
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

#ifdef CONFIG_MOL_HOSTED
#define IO_PAGE_MAGIC_1		0x10BADA57
#define IO_PAGE_MAGIC_2		0x136AB779
#else
#define IO_PAGE_MAGIC_1		0x10BACA57
#define IO_PAGE_MAGIC_2		0x135AB779
#endif

#ifndef __ASSEMBLY__
typedef struct io_page {		/* Must fit within a single 4K page */
	ulong		magic;		/* identifier 1 */
	ulong		magic2;		/* identifier 2 */

	ulong		me_phys;	/* physical address of this block */
	ulong		mphys;		/* mac-physical address of block */
	struct io_page 	*next;		/* next io_page */

	void		*usr_data[512];	/* usr data (grain=double word) */
} io_page_t;

/* from mmu.c */
extern int 	init_mmu( kernel_vars_t *kv );
extern void 	cleanup_mmu( kernel_vars_t *kv );
extern void	do_flush( ulong vsid, ulong va, ulong *dummy, int num );
extern void	mmu_altered( kernel_vars_t *kv );
extern void	clear_vsid_refs( kernel_vars_t *kv );

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
extern void	video_pte_inserted( kernel_vars_t *kv, ulong lvptr, ulong *slot,
				    ulong pte0, ulong pte1, ulong ea );
extern void	setup_fb_acceleration( kernel_vars_t *kv, char *lvbase, int bytes_per_row, int height );
extern int	get_dirty_fb_lines( kernel_vars_t *kv, short *retbuf, int num_bytes );

/* from mmu_tracker.c */
extern int 	init_mmu_tracker( kernel_vars_t *kv );
extern void 	cleanup_mmu_tracker( kernel_vars_t *kv );
extern int 	track_lvrange( kernel_vars_t *kv );
extern size_t 	get_track_buffer( kernel_vars_t *kv, char *retbuf );
extern void 	set_track_buffer( kernel_vars_t *kv, char *buf );
extern void	lvpage_dirty( kernel_vars_t *kv, ulong lvpage );

/* These functions should be used by the debugger only */
struct dbg_page_info;
extern int	dbg_get_PTE( kernel_vars_t *kv, int context, ulong va, mPTE_t *ret );
extern int 	dbg_get_linux_page( ulong va, struct dbg_page_info *r );
extern int	dbg_translate_ea( kernel_vars_t *kv, int context, ulong va, int *ret_mphys, int data_access );

/* arch functions */
extern ulong	get_phys_page( kernel_vars_t *kv, ulong lvptr, int request_rw );

/* VSID stuff */
#define VSID_MASK		0xffffff


#endif   /* __ASSEMBLY__ */
#endif   /* _H_MMU */
