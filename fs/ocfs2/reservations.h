/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * reservations.h
 *
 * Allocation reservations function prototypes and structures.
 *
 * Copyright (C) 2009 Novell.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef	OCFS2_RESERVATIONS_H
#define	OCFS2_RESERVATIONS_H

#include <linux/rbtree.h>

struct ocfs2_bitmap_resv_ops;

#define OCFS2_DEFAULT_RESV_LEVEL	3
#define OCFS2_MAX_RESV_LEVEL	7
#define OCFS2_MIN_RESV_LEVEL	0

struct ocfs2_alloc_reservation {
	struct rb_node	r_node;

	unsigned int	r_start;
	unsigned int	r_len;

	unsigned int	r_last_len;
	unsigned int	r_last_start;

	unsigned int	r_allocated;

	int		r_inuse;
};

struct ocfs2_reservation_map {
	struct rb_root		m_reservations;

	struct ocfs2_super	*m_osb;

	/* The following are not initialized to meaningful values until a disk
	 * bitmap is provided. */
	u32			m_bitmap_len;	/* Number of valid
						 * bits available */
};

void ocfs2_resv_init_once(struct ocfs2_alloc_reservation *resv);

/**
 * ocfs2_resv_discard() - truncate a reservation
 * @resmap:
 * @resv: the reservation to truncate.
 *
 * After this function is called, the reservation will be empty, and
 * unlinked from the rbtree.
 */
void ocfs2_resv_discard(struct ocfs2_reservation_map *resmap,
			struct ocfs2_alloc_reservation *resv);


/**
 * ocfs2_resmap_init() - Initialize fields of a reservations bitmap
 * @resmap: struct ocfs2_reservation_map to initialize
 * @obj: unused for now
 * @ops: unused for now
 * @max_bitmap_bytes: Maximum size of the bitmap (typically blocksize)
 *
 * Only possible return value other than '0' is -ENOMEM for failure to
 * allocation mirror bitmap.
 */
int ocfs2_resmap_init(struct ocfs2_super *osb,
		      struct ocfs2_reservation_map *resmap);

/**
 * ocfs2_resmap_restart() - "restart" a reservation bitmap
 * @resmap: reservations bitmap
 * @clen: Number of valid bits in the bitmap
 *
 * Re-initialize the parameters of a reservation bitmap. This is
 * useful for local alloc window slides.
 * 
 * If any bitmap parameters have changed, this function will call
 * ocfs2_trunc_resv against all existing reservations. A future
 * version will recalculate existing reservations based on the new
 * bitmap.
 */
void ocfs2_resmap_restart(struct ocfs2_reservation_map *resmap,
			  unsigned int clen);

/**
 * ocfs2_resmap_uninit() - uninitialize a reservation bitmap structure
 * @resmap: the struct ocfs2_reservation_map to uninitialize
 */
void ocfs2_resmap_uninit(struct ocfs2_reservation_map *resmap);

/**
 * ocfs2_resmap_resv_bits() - Return still-valid reservation bits
 * @resmap: reservations bitmap
 * @resv: reservation to base search from
 * @disk_bitmap: up to date (from disk) allocation bitmap
 * @cstart: start of proposed allocation
 * @clen: length (in clusters) of proposed allocation
 *
 * Using the reservation data from resv, this function will compare
 * resmap and disk_bitmap to determine what part (if any) of the
 * reservation window is still clear to use. An empty resv passed here
 * will just return no allocation.
 *
 * On success, zero is returned and the valid allocation area is set in cstart
 * and clen. If no allocation is found, they are set to zero.
 *
 * Returns nonzero on error.
 */
int ocfs2_resmap_resv_bits(struct ocfs2_reservation_map *resmap,
			   struct ocfs2_alloc_reservation *resv,
			   char *disk_bitmap, int *cstart, int *clen);

/**
 * ocfs2_resmap_claimed_bits() - Tell the reservation code that bits were used.
 * @resmap: reservations bitmap
 * @resv: optional reservation to recalulate based on new bitmap
 * @cstart: start of allocation in clusters
 * @clen: end of allocation in clusters.
 *
 * Tell the reservation code that bits were used to fulfill allocation in
 * resmap. The bits don't have to have been part of any existing
 * reservation. But we must always call this function when bits are claimed.
 * Internally, the reservations code will use this information to mark the
 * reservations bitmap. If resv is passed, it's next allocation window will be
 * calculated.
 */
void ocfs2_resmap_claimed_bits(struct ocfs2_reservation_map *resmap,
			       struct ocfs2_alloc_reservation *resv,
			       u32 cstart, u32 clen);

#endif	/* OCFS2_RESERVATIONS_H */
