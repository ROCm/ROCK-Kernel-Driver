/*
 *   Creation Date: <2004/03/13 13:25:42 samuel>
 *   Time-stamp: <2004/03/13 14:07:11 samuel>
 *
 *	<map.h>
 *
 *
 *
 *   Copyright (C) 2004 Samuel Rydh (samuel@ibrium.se)
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 */

#ifndef _H_MAP
#define _H_MAP

/* map a userspace address into the kernel address space */
extern void	*map_phys_range( ulong paddr, ulong size, char **ret_addr );
extern void	unmap_phys_range( void *handle );

/* map a userspace address into the kernel address space */
extern void	*map_virt_range( ulong va, ulong size, char **ret_addr );
extern void	unmap_virt_range( void *handle );

/* map the virtualized PTE hash into the kernel address space */
extern ulong 	*map_emulated_hash( kernel_vars_t *kv, ulong mbase, ulong size );
extern void	unmap_emulated_hash( kernel_vars_t *kv );

#ifdef __linux__
static inline ulong* map_hw_hash( ulong physbase, int size ) {
	return phys_to_virt( physbase );
}
static inline void unmap_hw_hash( ulong *base ) {}
#endif

#ifdef __darwin__
static inline void *map_hw_hash( ulong physbase, int size ) { return NULL; }
static inline void unmap_hw_hash( ulong *base ) {}
#endif

#endif   /* _H_MAP */
