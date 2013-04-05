/******************************************************************************
 * arch/xen/drivers/blkif/backend/interface.c
 * 
 * Block-device interface management.
 * 
 * Copyright (c) 2004, Keir Fraser
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "common.h"
#include <xen/evtchn.h>
#include <linux/kthread.h>
#include <linux/vmalloc.h>

static struct kmem_cache *blkif_cachep;

blkif_t *blkif_alloc(domid_t domid)
{
	blkif_t *blkif;

	blkif = kmem_cache_zalloc(blkif_cachep, GFP_KERNEL);
	if (!blkif)
		return ERR_PTR(-ENOMEM);

	blkif->domid = domid;
	spin_lock_init(&blkif->blk_ring_lock);
	atomic_set(&blkif->refcnt, 1);
	init_waitqueue_head(&blkif->wq);
	init_completion(&blkif->drain_complete);
	atomic_set(&blkif->drain, 0);
	blkif->st_print = jiffies;
	init_waitqueue_head(&blkif->waiting_to_free);

	return blkif;
}

unsigned int blkif_ring_size(enum blkif_protocol protocol,
			     unsigned int ring_size)
{
	unsigned long size = (unsigned long)ring_size << PAGE_SHIFT;

#define BLKBK_RING_SIZE(p) PFN_UP(offsetof(struct blkif_##p##_sring, \
		ring[__CONST_RING_SIZE(blkif_##p, size)]))
	switch (protocol) {
	case BLKIF_PROTOCOL_NATIVE:
		return BLKBK_RING_SIZE(native);
	case BLKIF_PROTOCOL_X86_32:
		return BLKBK_RING_SIZE(x86_32);
	case BLKIF_PROTOCOL_X86_64:
		return BLKBK_RING_SIZE(x86_64);
	}
#undef BLKBK_RING_SIZE
	BUG();
	return 0;
}

int blkif_map(blkif_t *blkif, const grant_ref_t ring_ref[],
	      unsigned int nr_refs, evtchn_port_t evtchn)
{
	unsigned long size = (unsigned long)nr_refs << PAGE_SHIFT;
	struct vm_struct *area;
	int err;

	/* Already connected through? */
	if (blkif->irq)
		return 0;

	area = xenbus_map_ring_valloc(blkif->be->dev, ring_ref, nr_refs);
	if (IS_ERR(area))
		return PTR_ERR(area);
	blkif->blk_ring_area = area;

	switch (blkif->blk_protocol) {
#define BLKBK_RING_INIT(p) ({ \
		struct blkif_##p##_sring *sring = area->addr; \
		BACK_RING_INIT(&blkif->blk_rings.p, sring, size); \
	})
	case BLKIF_PROTOCOL_NATIVE:
		BLKBK_RING_INIT(native);
		break;
	case BLKIF_PROTOCOL_X86_32:
		BLKBK_RING_INIT(x86_32);
		break;
	case BLKIF_PROTOCOL_X86_64:
		BLKBK_RING_INIT(x86_64);
		break;
	default:
		BUG();
#undef BLKBK_RING_INIT
	}

	err = bind_interdomain_evtchn_to_irqhandler(
		blkif->domid, evtchn, blkif_be_int, 0, "blkif-backend", blkif);
	if (err < 0)
	{
		xenbus_unmap_ring_vfree(blkif->be->dev, area);
		blkif->blk_rings.common.sring = NULL;
		return err;
	}
	blkif->irq = err;

	return 0;
}

void blkif_disconnect(blkif_t *blkif)
{
	if (blkif->xenblkd) {
		kthread_stop(blkif->xenblkd);
		blkif->xenblkd = NULL;
	}

	atomic_dec(&blkif->refcnt);
	wait_event(blkif->waiting_to_free, atomic_read(&blkif->refcnt) == 0);
	atomic_inc(&blkif->refcnt);

	if (blkif->irq) {
		unbind_from_irqhandler(blkif->irq, blkif);
		blkif->irq = 0;
	}

	if (blkif->blk_rings.common.sring) {
		xenbus_unmap_ring_vfree(blkif->be->dev, blkif->blk_ring_area);
		blkif->blk_rings.common.sring = NULL;
	}
}

void blkif_free(blkif_t *blkif)
{
	if (!atomic_dec_and_test(&blkif->refcnt))
		BUG();
	kmem_cache_free(blkif_cachep, blkif);
}

void __init blkif_interface_init(void)
{
	blkif_cachep = kmem_cache_create("blkif_cache", sizeof(blkif_t), 
					 0, 0, NULL);
}
