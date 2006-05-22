/*
 * include/linux/sgrb.h
 *
 * a ringbuffer made up of scattered buffers;
 * holds fixed-size entries smaller than the size of underlying buffers
 * (ringbuffer resizing has not been implemented)
 *
 * (C) Copyright IBM Corp. 2005
 *
 * Author(s): Martin Peschke <mpeschke@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef SGRB_H
#define SGRB_H

#define SGRB_H_REVISION "$Revision: 1.2 $"

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/list.h>

#define SGRB_BUFFER_SIZE	PAGE_SIZE

struct sgrb_seg {
	struct list_head list;
	char *address;
	int offset;
	int size;
};

struct sgrb_ptr {
	struct sgrb_seg *seg;
	signed int offset;
};

struct sgrb {
	struct list_head seg_lh;
	struct sgrb_ptr first;
	struct sgrb_ptr last;
	int entry_size;
	int entries;
};

/**
 * sgrb_ptr_copy - prepare ringbuffer pointer a by copying b
 * @a: duplicate
 * @b: original
 *
 * required to prepare a ringbuffer pointer for use with sgrb_consume_nodelete()
 */
static inline void sgrb_ptr_copy(struct sgrb_ptr *a, struct sgrb_ptr *b)
{
	a->seg = b->seg;
	a->offset = b->offset;
}

/**
 * sgrb_entry - returns address of entry
 * @a: ringbuffer pointer that determines entry
 */
static inline void * sgrb_entry(struct sgrb_ptr *a)
{
	return (a->seg->address + a->offset);
}

extern struct sgrb_seg * sgrb_seg_find(struct list_head *, int, gfp_t);
extern void sgrb_seg_release_all(struct list_head *);

extern int sgrb_alloc(struct sgrb *, int, int, int, gfp_t);
extern void sgrb_release(struct sgrb *);
extern void sgrb_reset(struct sgrb *);

extern void * sgrb_produce_overwrite(struct sgrb *);
extern void * sgrb_produce_nooverwrite(struct sgrb *);
extern void * sgrb_consume_delete(struct sgrb *);
extern void * sgrb_consume_nodelete(struct sgrb *, struct sgrb_ptr *);

#endif /* SGRB_H */
