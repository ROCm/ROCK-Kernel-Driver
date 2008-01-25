/*
 *   Creation Date: <2002/01/13 16:35:18 samuel>
 *   Time-stamp: <2004/01/25 17:36:49 samuel>
 *
 *	<alloc.h>
 *
 *	Memory allocation and mappings
 *
 *   Copyright (C) 2002, 2003, 2004 Samuel Rydh (samuel@ibrium.se)
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *
 */

#ifndef _H_ALLOC
#define _H_ALLOC

#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <asm/uaccess.h>

#ifdef LINUX_26
#include <asm/cacheflush.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,10)
#include <asm/io.h>
#endif
#endif

static inline void *kmalloc_mol( int size ) {
	return kmalloc( size, GFP_KERNEL );
}
static inline void kfree_mol( void *p ) {
	kfree( p );
}
static inline void *vmalloc_mol( int size ) {
	return vmalloc( size );
}
static inline void vfree_mol( void *p ) {
	vfree( p );
}
static inline ulong alloc_page_mol( void ) {
	return get_zeroed_page( GFP_KERNEL );
}
static inline void free_page_mol( ulong addr ) {
	free_page( addr );
}
static inline void *kmalloc_cont_mol( int size ) {
	return kmalloc( size, GFP_KERNEL );
}
static inline void kfree_cont_mol( void *addr ) {
	kfree( addr );
}
static inline ulong tophys_mol( void *addr ) {
	return virt_to_phys(addr);
}
static inline void flush_icache_mol( ulong start, ulong stop ) {
	flush_icache_range( start, stop );
}
static inline void *map_phys_range( ulong paddr, ulong size, char **ret_addr ) {
	/* Warning: This works only for certain addresses... */
	*ret_addr = phys_to_virt(paddr);
	return (void*)(-2);	/* dummy */
}
static inline void unmap_phys_range( void *handle ) {}


#endif   /* _H_ALLOC */
