/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef __XFS_SUPPORT_KMEM_H__
#define __XFS_SUPPORT_KMEM_H__

#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

/*
 * Cutoff point to use vmalloc instead of kmalloc.
 */
#define MAX_SLAB_SIZE	0x20000

/*
 * XFS uses slightly different names for these due to the
 * IRIX heritage.
 */
#define	kmem_zone	kmem_cache_s
#define kmem_zone_t	kmem_cache_t

#define KM_SLEEP	0x0001
#define KM_NOSLEEP	0x0002
#define KM_NOFS		0x0004

typedef unsigned long xfs_pflags_t;

#define PFLAGS_TEST_FSTRANS()           (current->flags & PF_FSTRANS)

/* these could be nested, so we save state */
#define PFLAGS_SET_FSTRANS(STATEP) do {	\
	*(STATEP) = current->flags;	\
	current->flags |= PF_FSTRANS;	\
} while (0)

#define PFLAGS_CLEAR_FSTRANS(STATEP) do { \
	*(STATEP) = current->flags;	\
	current->flags &= ~PF_FSTRANS;	\
} while (0)

/* Restore the PF_FSTRANS state to what was saved in STATEP */
#define PFLAGS_RESTORE_FSTRANS(STATEP) do {     		\
	current->flags = ((current->flags & ~PF_FSTRANS) |	\
			  (*(STATEP) & PF_FSTRANS));		\
} while (0)

#define PFLAGS_DUP(OSTATEP, NSTATEP) do { \
	*(NSTATEP) = *(OSTATEP);	\
} while (0)

/*
 * XXX get rid of the unconditional  __GFP_NOFAIL by adding
 * a KM_FAIL flag and using it where we're allowed to fail.
 */
static __inline unsigned int
kmem_flags_convert(int flags)
{
	int lflags;

#if DEBUG
	if (unlikely(flags & ~(KM_SLEEP|KM_NOSLEEP|KM_NOFS))) {
		printk(KERN_WARNING
		    "XFS: memory allocation with wrong flags (%x)\n", flags);
		BUG();
	}
#endif

	lflags = (flags & KM_NOSLEEP) ? GFP_ATOMIC : (GFP_KERNEL|__GFP_NOFAIL);

	/* avoid recusive callbacks to filesystem during transactions */
	if (PFLAGS_TEST_FSTRANS() || (flags & KM_NOFS))
		lflags &= ~__GFP_FS;

	return lflags;
}

static __inline void *
kmem_alloc(size_t size, int flags)
{
	if (unlikely(MAX_SLAB_SIZE < size))
		/* Avoid doing filesystem sensitive stuff to get this */
		return __vmalloc(size, kmem_flags_convert(flags), PAGE_KERNEL);
	return kmalloc(size, kmem_flags_convert(flags));
}

static __inline void *
kmem_zalloc(size_t size, int flags)
{
	void *ptr = kmem_alloc(size, flags);
	if (likely(ptr != NULL))
		memset(ptr, 0, size);
	return ptr;
}

static __inline void
kmem_free(void *ptr, size_t size)
{
	if (unlikely((unsigned long)ptr < VMALLOC_START ||
		     (unsigned long)ptr >= VMALLOC_END))
		kfree(ptr);
	else
		vfree(ptr);
}

static __inline void *
kmem_realloc(void *ptr, size_t newsize, size_t oldsize, int flags)
{
	void *new = kmem_alloc(newsize, flags);

	if (likely(ptr != NULL)) {
		if (likely(new != NULL))
			memcpy(new, ptr, min(oldsize, newsize));
		kmem_free(ptr, oldsize);
	}

	return new;
}

static __inline kmem_zone_t *
kmem_zone_init(int size, char *zone_name)
{
	return kmem_cache_create(zone_name, size, 0, 0, NULL, NULL);
}

static __inline void *
kmem_zone_alloc(kmem_zone_t *zone, int flags)
{
	return kmem_cache_alloc(zone, kmem_flags_convert(flags));
}

static __inline void *
kmem_zone_zalloc(kmem_zone_t *zone, int flags)
{
	void *ptr = kmem_zone_alloc(zone, flags);
	if (likely(ptr != NULL))
		memset(ptr, 0, kmem_cache_size(zone));
	return ptr;
}

static __inline void
kmem_zone_free(kmem_zone_t *zone, void *ptr)
{
	kmem_cache_free(zone, ptr);
}

typedef struct shrinker *kmem_shaker_t;
typedef int (*kmem_shake_func_t)(int, unsigned int);

static __inline kmem_shaker_t
kmem_shake_register(kmem_shake_func_t sfunc)
{
	return set_shrinker(DEFAULT_SEEKS, sfunc);
}

static __inline void
kmem_shake_deregister(kmem_shaker_t shrinker)
{
	remove_shrinker(shrinker);
}

static __inline int
kmem_shake_allow(unsigned int gfp_mask)
{
	return (gfp_mask & __GFP_WAIT);
}

#endif /* __XFS_SUPPORT_KMEM_H__ */
