/* 
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

#ifndef __BLKIF__BACKEND__COMMON_H__
#define __BLKIF__BACKEND__COMMON_H__

#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/wait.h>
#include <asm/hypervisor.h>
#include <xen/barrier.h>
#include <xen/blkif.h>
#include <xen/xenbus.h>
#include <xen/interface/event_channel.h>
#include "blkback-pagemap.h"


#define DPRINTK(_f, _a...)			\
	pr_debug("(file=%s, line=%d) " _f,	\
		 __FILE__ , __LINE__ , ## _a )

struct vbd {
	blkif_vdev_t   handle;      /* what the domain refers to this vbd as */
	fmode_t        mode;        /* FMODE_xxx */
	unsigned char  type;        /* VDISK_xxx */
	bool           flush_support;
	bool           discard_secure;
	u32            pdevice;     /* phys device that this vbd maps to */
	struct block_device *bdev;
	sector_t       size;        /* Cached size parameter */
};

#define BLKIF_MAX_RING_PAGE_ORDER 4
#define BLKIF_MAX_RING_PAGES (1 << BLKIF_MAX_RING_PAGE_ORDER)
#define BLK_RING_SIZE(order) __CONST_RING_SIZE(blkif, PAGE_SIZE << (order))

typedef struct blkif_st {
	/* Unique identifier for this interface. */
	domid_t           domid;
	unsigned int      handle;
	/* Physical parameters of the comms window. */
	unsigned int      irq;
	/* Comms information. */
	enum blkif_protocol blk_protocol;
	blkif_back_rings_t blk_rings;
	struct vm_struct *blk_ring_area;
	/* The VBD attached to this interface. */
	struct vbd        vbd;
	/* Back pointer to the backend_info. */
	struct backend_info *be;
	/* Private fields. */
	spinlock_t       blk_ring_lock;
	atomic_t         refcnt;
	struct gnttab_map_grant_ref *map;
	union blkif_seg {
		unsigned int nsec;
		struct bio *bio;
	}                *seg;
	struct blkbk_request {
		uint8_t        operation;
		blkif_vdev_t   handle;
		unsigned int   nr_segments;
		uint64_t       id;
		blkif_sector_t sector_number;
		struct blkif_request_segment seg[];
	}                *req;
	xen_pfn_t         seg_mfn[BLKIF_MAX_INDIRECT_PAGES_PER_REQUEST];
	unsigned int      seg_offs;

	wait_queue_head_t   wq;
	/* for barrier (drain) requests */
	struct completion   drain_complete;
	atomic_t            drain;
	struct task_struct  *xenblkd;
	unsigned int        waiting_reqs;

	/* statistics */
	unsigned long       st_print;
	unsigned long       st_rd_req;
	unsigned long       st_wr_req;
	unsigned long       st_oo_req;
	unsigned long       st_br_req;
	unsigned long       st_fl_req;
	unsigned long       st_ds_req;
	unsigned long       st_pk_req;
	unsigned long       st_rd_sect;
	unsigned long       st_wr_sect;

	wait_queue_head_t waiting_to_free;
	wait_queue_head_t shutdown_wq;
} blkif_t;

struct backend_info
{
	struct xenbus_device *dev;
	blkif_t *blkif;
	struct xenbus_watch backend_watch;
	struct xenbus_watch cdrom_watch;
	unsigned major;
	unsigned minor;
	char *mode;
};

extern unsigned int blkif_max_ring_page_order;
extern unsigned int blkif_max_segs_per_req;

blkif_t *blkif_alloc(domid_t domid);
void blkif_disconnect(blkif_t *blkif);
void blkif_free(blkif_t *blkif);
unsigned int blkif_ring_size(enum blkif_protocol, unsigned int);
int blkif_map(blkif_t *blkif, const grant_ref_t [],
	      unsigned int nr_refs, evtchn_port_t);
void vbd_resize(blkif_t *blkif);

#define blkif_get(_b) (atomic_inc(&(_b)->refcnt))
#define blkif_put(_b)					\
	do {						\
		if (atomic_dec_and_test(&(_b)->refcnt))	\
			wake_up(&(_b)->waiting_to_free);\
	} while (0)

/* Create a vbd. */
int vbd_create(blkif_t *blkif, blkif_vdev_t vdevice, unsigned major,
	       unsigned minor, fmode_t mode, bool cdrom);
void vbd_free(struct vbd *vbd);

unsigned long long vbd_size(struct vbd *vbd);
unsigned long vbd_secsize(struct vbd *vbd);

struct phys_req {
	unsigned short       dev;
	blkif_sector_t       nr_sects;
	struct block_device *bdev;
	blkif_sector_t       sector_number;
};

int vbd_translate(struct phys_req *req, blkif_t *blkif, int operation);

void blkif_interface_init(void);

void blkif_xenbus_init(void);

irqreturn_t blkif_be_int(int irq, void *dev_id);
int blkif_schedule(void *arg);

void blkback_barrier(struct xenbus_transaction, struct backend_info *,
		     int state);
void blkback_flush_diskcache(struct xenbus_transaction,
			     struct backend_info *, int state);

/* cdrom media change */
void cdrom_add_media_watch(struct backend_info *be);

#endif /* __BLKIF__BACKEND__COMMON_H__ */
