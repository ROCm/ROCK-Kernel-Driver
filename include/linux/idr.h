/*
 * include/linux/id.h
 * 
 * 2002-10-18  written by Jim Houston jim.houston@ccur.com
 *	Copyright (C) 2002 by Concurrent Computer Corporation
 *	Distributed under the GNU GPL license version 2.
 *
 * Small id to pointer translation service avoiding fixed sized
 * tables.
 */
#include <linux/types.h>
#include <asm/bitops.h>

#define RESERVED_ID_BITS 8

#if     BITS_PER_LONG == 32
#define IDR_BITS 5
#define IDR_FULL 0xffffffff
#elif BITS_PER_LONG == 64
#define IDR_BITS 6
#define IDR_FULL 0xffffffffffffffff
#else
#error "BITS_PER_LONG is not 32 or 64"
#endif

#define IDR_MASK ((1 << IDR_BITS)-1)

/* Leave the possibility of an incomplete final layer */
#define MAX_LEVEL (BITS_PER_LONG - RESERVED_ID_BITS + IDR_BITS - 1) / IDR_BITS
#define MAX_ID_SHIFT (BITS_PER_LONG - RESERVED_ID_BITS)
#define MAX_ID_BIT (1 << MAX_ID_SHIFT)
#define MAX_ID_MASK (MAX_ID_BIT - 1)

/* Number of id_layer structs to leave in free list */
#define IDR_FREE_MAX MAX_LEVEL + MAX_LEVEL

struct idr_layer {
	unsigned long	        bitmap;     // A zero bit means "space here"
	int                     count;      // When zero, we can release it
	struct idr_layer       *ary[1<<IDR_BITS];
};

struct idr {
	struct idr_layer *top;
	int		  layers;
	int		  count;
	struct idr_layer *id_free;
	int               id_free_cnt;
	spinlock_t        lock;
};

/*
 * This is what we export.
 */

void *idr_find(struct idr *idp, int id);
int idr_pre_get(struct idr *idp);
int idr_get_new(struct idr *idp, void *ptr);
void idr_remove(struct idr *idp, int id);
void idr_init(struct idr *idp);

extern kmem_cache_t *idr_layer_cache;

