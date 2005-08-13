/* 
 *   Creation Date: <2002/01/13 16:35:18 samuel>
 *   Time-stamp: <2003/08/30 16:31:40 samuel>
 *   
 *	<alloc.h>
 *	
 *	Memory allocation and mappings
 *   
 *   Copyright (C) 2002, 2003 Samuel Rydh (samuel@ibrium.se)
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
#include <asm/io.h>

#ifdef LINUX_26
#include <asm/cacheflush.h>
#endif

#define	spin_lock_mol(x)			spin_lock(x)
#define	spin_unlock_mol(x)			spin_unlock(x)
#define	spin_lock_irqsave_mol(x, flags)		spin_lock_irqsave(x, flags)
#define	spin_unlock_irqrestore_mol(x,flags)	spin_unlock_irqrestore(x, flags)
#define spin_lock_init_mol(x)			spin_lock_init(x)
#define mol_spinlock_t			spinlock_t

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

static inline unsigned int copy_to_user_mol( void *to, const void *from, ulong len ) {
	return copy_to_user( to, from, len );
}
static inline unsigned int copy_from_user_mol( void *to, const void *from, ulong len ) {
	return copy_from_user( to, from, len );
}

static inline unsigned int copy_int_to_user( int *to, int val ) {
	return copy_to_user_mol( to, &val, sizeof(int) );
}
static inline unsigned int copy_int_from_user( int *retint, int *userptr ) {
	return copy_from_user_mol( retint, userptr, sizeof(int) );
}

#if 0
#undef		kfree
#undef		kmalloc
#undef		vmalloc
#undef		vfree
#undef		get_free_page
#undef		get_zeroed_page
#undef		free_page

#define 	kfree			do_not_use_kfree
#define		kmalloc			do_not_use_kmalloc
#define		vmalloc			do_not_use_vmalloc
#define		vfree			do_not_use_vfree
#define		get_free_page		do_not_use_get_free_page
#define		get_zeroed_page		do_not_use_get_zeroed_page
#define		free_page		do_not_use_free_page
#endif

#endif   /* _H_ALLOC */
