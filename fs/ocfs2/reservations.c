/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * reservations.c
 *
 * Allocation reservations implementation
 *
 * Some code borrowed from fs/ext3/balloc.c
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

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/bitops.h>

#define MLOG_MASK_PREFIX ML_RESERVATIONS
#include <cluster/masklog.h>

#include "ocfs2.h"

DEFINE_SPINLOCK(resv_lock);

static inline unsigned int ocfs2_resv_end(struct ocfs2_alloc_reservation *resv)
{
	if (resv->r_len)
		return resv->r_start + resv->r_len - 1;
	return resv->r_start;
}

static inline int ocfs2_resv_empty(struct ocfs2_alloc_reservation *resv)
{
	return !!(resv->r_len == 0);
}

static void ocfs2_dump_resv(struct ocfs2_reservation_map *resmap)
{
	struct ocfs2_super *osb = resmap->m_osb;
	struct rb_node *node;
	struct ocfs2_alloc_reservation *resv;
	int i = 0;

	mlog(ML_NOTICE, "Dumping resmap for device %s. Bitmap length: %u\n",
	     osb->dev_str, resmap->m_bitmap_len);

	node = rb_first(&resmap->m_reservations);
	while (node) {
		resv = rb_entry(node, struct ocfs2_alloc_reservation, r_node);

		mlog(ML_NOTICE, "start: %u\tend: %u\tlen: %u\tlast_start: %u"
		     "\tlast_len: %u\tallocated: %u\n", resv->r_start,
		     ocfs2_resv_end(resv), resv->r_len, resv->r_last_start,
		     resv->r_last_len, resv->r_allocated);

		node = rb_next(node);
		i++;
	}

	mlog(ML_NOTICE, "%d reservations found\n", i);
}

void ocfs2_resv_init_once(struct ocfs2_alloc_reservation *resv)
{
	memset(resv, 0, sizeof(*resv));
}

int ocfs2_resmap_init(struct ocfs2_super *osb,
		      struct ocfs2_reservation_map *resmap)
{
	memset(resmap, 0, sizeof(*resmap));

	resmap->m_osb = osb;
	resmap->m_reservations = RB_ROOT;
	/* m_bitmap_len is initialized to zero by the above memset. */

	return 0;
}

static void __ocfs2_resv_trunc(struct ocfs2_alloc_reservation *resv)
{
	resv->r_len = 0;
	resv->r_allocated = 0;
}

static void __ocfs2_resv_discard(struct ocfs2_reservation_map *resmap,
				 struct ocfs2_alloc_reservation *resv)
{
		__ocfs2_resv_trunc(resv);
		rb_erase(&resv->r_node, &resmap->m_reservations);
}

/* does nothing if 'resv' is null */
void ocfs2_resv_discard(struct ocfs2_reservation_map *resmap,
			struct ocfs2_alloc_reservation *resv)
{
	if (resv) {
		spin_lock(&resv_lock);
		__ocfs2_resv_discard(resmap, resv);
		spin_unlock(&resv_lock);
	}
}

static void ocfs2_resmap_clear_all_resv(struct ocfs2_reservation_map *resmap)
{
	struct rb_node *node;
	struct ocfs2_alloc_reservation *resv;

	assert_spin_locked(&resv_lock);

	while ((node = rb_last(&resmap->m_reservations)) != NULL) {
		resv = rb_entry(node, struct ocfs2_alloc_reservation, r_node);

		__ocfs2_resv_trunc(resv);
		rb_erase(&resv->r_node, &resmap->m_reservations);
	}
}

/* If any parameters have changed, this function will call
 * ocfs2_resv_trunc against all existing reservations. */
void ocfs2_resmap_restart(struct ocfs2_reservation_map *resmap,
			  unsigned int clen)
{
	spin_lock(&resv_lock);

	ocfs2_resmap_clear_all_resv(resmap);
	resmap->m_bitmap_len = clen;

	spin_unlock(&resv_lock);
}

void ocfs2_resmap_uninit(struct ocfs2_reservation_map *resmap)
{
	/* Does nothing for now. Keep this around for API symmetry */
}

unsigned int ocfs2_resv_target_len(unsigned int last_len)
{
	if (last_len < 8)
		return 8;

	last_len += 4;

	if (last_len > 32)
		return 32;

	return last_len;
}

static int ocfs2_try_to_extend_resv(struct ocfs2_reservation_map *resmap,
				    struct ocfs2_alloc_reservation *my_resv)
{
	unsigned int available, avail_end;
	struct rb_node *next, *node = &my_resv->r_node;
	struct ocfs2_alloc_reservation *next_resv;

	next = rb_next(node);

	if (next) {
		next_resv = rb_entry(next, struct ocfs2_alloc_reservation,
				     r_node);
		avail_end = next_resv->r_start;
	} else {
		avail_end = resmap->m_bitmap_len - 1;
	}

	if (ocfs2_resv_end(my_resv) == avail_end)
		return -ENOENT;

	available = avail_end - ocfs2_resv_end(my_resv) + 1;

	if (available > 4)
		available = 4;

	my_resv += available;

	return 0;
}

static void ocfs2_resv_insert(struct ocfs2_reservation_map *resmap,
			      struct ocfs2_alloc_reservation *new)
{
	struct rb_root *root = &resmap->m_reservations;
	struct rb_node *parent = NULL;
	struct rb_node **p = &root->rb_node;
	struct ocfs2_alloc_reservation *tmp;

	mlog(0, "Insert reservation start: %u len: %u\n", new->r_start,
	     new->r_len);

	while(*p) {
		parent = *p;

		tmp = rb_entry(parent, struct ocfs2_alloc_reservation, r_node);

		if (new->r_start < tmp->r_start)
			p = &(*p)->rb_left;
		else if (new->r_start > ocfs2_resv_end(tmp))
			p = &(*p)->rb_right;
		else {
			/* This should never happen! */
			mlog(ML_ERROR, "Duplicate reservation window!\n");
			BUG();
		}
	}

	rb_link_node(&new->r_node, parent, p);
	rb_insert_color(&new->r_node, root);

//	ocfs2_dump_resv(resmap);
}

/**
 * ocfs2_find_resv() - find the window which contains goal
 * @resmap: reservation map to search
 * @goal: which bit to search for
 *
 * If a window containing that goal is not found, we return the window
 * which comes before goal. Returns NULL on empty rbtree.
 */
static struct ocfs2_alloc_reservation *
ocfs2_find_resv(struct ocfs2_reservation_map *resmap, unsigned int goal)
{
	struct ocfs2_alloc_reservation *resv;
	struct rb_node *n = resmap->m_reservations.rb_node;

	assert_spin_locked(&resv_lock);

	if (!n)
		return NULL;

	do {
		resv = rb_entry(n, struct ocfs2_alloc_reservation, r_node);

		if (goal < resv->r_start)
			n = n->rb_left;
		else if (goal > ocfs2_resv_end(resv))
			n = n->rb_right;
		else
			return resv;
	} while (n);

	/*
	 * The goal sits on one end of the tree. If it's the leftmost
	 * end, we return NULL.
	 */
	if (resv->r_start > goal)
		return NULL;

	return resv;
}

static void ocfs2_resv_find_window(struct ocfs2_reservation_map *resmap,
				   struct ocfs2_alloc_reservation *resv)
{
	struct rb_root *root = &resmap->m_reservations;
	unsigned int last_start = resv->r_last_start;
	unsigned int last_end = last_start + resv->r_last_len - 1;
	unsigned int goal = last_end + 1;
	unsigned int len = ocfs2_resv_target_len(resv->r_last_len);
	unsigned int gap_start, gap_end, gap_len;
	struct ocfs2_alloc_reservation *prev_resv, *next_resv;
	struct rb_node *prev, *next;

	/* Math above doesn't work otherwise... */
	BUG_ON(resv->r_last_len == 0);

	/*
	 * Nasty cases to consider:
	 *
	 * - rbtree is empty
	 * - our window should be first in all reservations
	 * - our window should be last in all reservations
	 * - need to make sure we don't go past end of bitmap
	 */

	assert_spin_locked(&resv_lock);

	if (RB_EMPTY_ROOT(root)) {
		/*
		 * Easiest case - empty tree. We can just take
		 * whatever window we want.
		 */

		mlog(0, "Empty root\n");

		resv->r_start = goal;
		resv->r_len = len;
		ocfs2_resv_insert(resmap, resv);
		return;
	}

	prev_resv = ocfs2_find_resv(resmap, goal);

	if (prev_resv == NULL) {
		mlog(0, "Farthest left window\n");

		/* Ok, we're the farthest left window. */
		next = rb_first(root);
		next_resv = rb_entry(next, struct ocfs2_alloc_reservation,
				     r_node);

		/*
		 * Try to allocate at far left of tree. If that
		 * doesn't fit, we just start our linear search from
		 * next_resv
		 */
		if (next_resv->r_start > (goal + len - 1)) {
			resv->r_start = goal;
			resv->r_len = len;
			ocfs2_resv_insert(resmap, resv);
			return;
		}

		prev_resv = next_resv;
		next_resv = NULL;
	}

	prev = &prev_resv->r_node;

	/* Now we do a linear search for a window, starting at 'prev_rsv' */
	while (1) {
		next = rb_next(prev);
		if (next) {
			mlog(0, "One more resv found in linear search\n");
			next_resv = rb_entry(next,
					     struct ocfs2_alloc_reservation,
					     r_node);

			gap_start = ocfs2_resv_end(prev_resv) + 1;
			gap_end = next_resv->r_start - 1;
			gap_len = gap_end - gap_start + 1;
		} else {
			mlog(0, "No next node\n");
			/*
			 * We're at the rightmost edge of the
			 * tree. See if a reservation between this
			 * window and the end of the bitmap will work.
			 */
			gap_start = ocfs2_resv_end(prev_resv) + 1;
			gap_end = resmap->m_bitmap_len - 1;
			gap_len = gap_end - gap_start + 1;
		}

		if (gap_start <= gap_end
		    && gap_start >= goal
		    && gap_len >= len) {
			resv->r_start = gap_start;
			resv->r_len = len;
			ocfs2_resv_insert(resmap, resv);
			return;
		}

		if (!next)
			break;

		prev = next;
		prev_resv = rb_entry(prev, struct ocfs2_alloc_reservation,
				     r_node);
	}
}

void ocfs2_resmap_claimed_bits(struct ocfs2_reservation_map *resmap,
			       struct ocfs2_alloc_reservation *resv,
			       u32 cstart, u32 clen)
{
	unsigned int cend = cstart + clen - 1;

	if (resmap == NULL)
		return;

	if (resv == NULL)
		return;

	spin_lock(&resv_lock);

	mlog(0, "claim bits: cstart: %u cend: %u clen: %u r_start: %u "
	     "r_end: %u r_len: %u, r_last_start: %u r_last_len: %u\n",
	     cstart, cend, clen, resv->r_start, ocfs2_resv_end(resv),
	     resv->r_len, resv->r_last_start, resv->r_last_len);

	resv->r_last_len = clen;
	resv->r_last_start = cstart;

	if (ocfs2_resv_empty(resv)) {
		mlog(0, "Empty reservation, find a new window.\n");
		/*
		 * Allocation occured without a window. We find an
		 * initial reservation for this inode, based on what
		 * was allocated already.
		 */
		ocfs2_resv_find_window(resmap, resv);
		goto out_unlock;
	}

	/*
	 * Did the allocation occur completely outside our
	 * reservation? Clear it then. Otherwise, try to extend our
	 * reservation or alloc a new one, if we've used all the bits.
	 */
	if (cend < resv->r_start ||
	    cstart > ocfs2_resv_end(resv)) {
		mlog(0, "Allocated outside reservation\n");

		/* Truncate and remove reservation */
		__ocfs2_resv_discard(resmap, resv);

		if (cend < resv->r_start) {
			/*
			 * The window wasn't used for some reason. We
			 * should start our search *past* it to give a
			 * better chance the next window will be
			 * used. Best way to do this right now is to
			 * fool the search code...
			 */
			resv->r_last_start = ocfs2_resv_end(resv) + 1;
			resv->r_last_len = 1;
		}

		ocfs2_resv_find_window(resmap, resv);
		goto out_unlock;
	}

	/*
	 * We allocated at least partially from our
	 * reservation. Adjust it and try to extend. Otherwise, we
	 * search for a new window.
	 */

	resv->r_allocated += clen;

	if (cend < ocfs2_resv_end(resv)) {
		u32 old_end;

		mlog(0, "Allocation left at end\n");

		/*
		 * Partial allocation, leaving some bits free at
		 * end. We move over the start of the window to take
		 * this into account and try to extend it.
		 */
		old_end = ocfs2_resv_end(resv);
		resv->r_start = cend + 1; /* Start just past last allocation */
		resv->r_len = old_end - resv->r_start + 1;

		if (ocfs2_try_to_extend_resv(resmap, resv) == 0)
			goto out_unlock;
	}

	mlog(0, "discard reservation\n");

	/*
	 * No free bits at end or extend failed above. Truncate and
	 * re-search for a new window.
	 */

	__ocfs2_resv_discard(resmap, resv);

	ocfs2_resv_find_window(resmap, resv);

out_unlock:
	mlog(0, "Reservation now looks like: r_start: %u r_end: %u "
	     "r_len: %u r_last_start: %u r_last_len: %u\n",
	     resv->r_start, ocfs2_resv_end(resv), resv->r_len,
	     resv->r_last_start, resv->r_last_len);

	spin_unlock(&resv_lock);
}

int ocfs2_resmap_resv_bits(struct ocfs2_reservation_map *resmap,
			   struct ocfs2_alloc_reservation *resv,
			   char *disk_bitmap, int *cstart, int *clen)
{
	int ret = -ENOSPC;
	unsigned int start, len, best_start = 0, best_len = 0;

	if (resv == NULL)
		return -ENOSPC;

	spin_lock(&resv_lock);

	if (ocfs2_resv_empty(resv)) {
		/*
		 * If resv is empty, we return zero bytes and allow
		 * ocfs2_resmap_claimed_bits() to start our new reservation.
		 */
		*cstart = *clen = 0;
		ret = 0;
		goto out;
	}

	start = resv->r_start;
	len = 0;

	while (start <= ocfs2_resv_end(resv)) {
		if (ocfs2_test_bit(start, disk_bitmap)) {
			mlog(0,
			     "Reservation was taken at bit %d\n",
			     start + len);
			best_len = 0;
			goto next;
		}

		/* This is basic, but since the local alloc is
		 * used very predictably, I think we're ok. */
		if (!best_len) {
			best_start = start;
			best_len = 1;
		} else {
			best_len++;
		}

next:
		start++;
	}

	if (best_len) {
		ret = 0;
		*cstart = best_start;
		*clen = best_len;
	}
out:
	spin_unlock(&resv_lock);

	return ret;
}
