/****************************************************************************
 * Solarflare driver for Xen network acceleration
 *
 * Copyright 2006-2008: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Maintained by Solarflare Communications <linux-xen-drivers@solarflare.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

#include <xen/gnttab.h>

#include "accel_bufs.h"
#include "accel_util.h"

#include "accel.h"


static int 
netfront_accel_alloc_buf_desc_blocks(struct netfront_accel_bufinfo *manager,
				     int pages)
{
	manager->desc_blocks = 
		kzalloc(sizeof(struct netfront_accel_pkt_desc *) * 
			NETFRONT_ACCEL_BUF_NUM_BLOCKS(pages), GFP_KERNEL);
	if (manager->desc_blocks == NULL) {
		return -ENOMEM;
	}
	
	return 0;
}

static int 
netfront_accel_alloc_buf_lists(struct netfront_accel_bufpages *bufpages,
			       int pages)
{
	bufpages->page_list = kmalloc(pages * sizeof(void *), GFP_KERNEL);
	if (bufpages->page_list == NULL) {
		return -ENOMEM;
	}

	bufpages->grant_list = kzalloc(pages * sizeof(grant_ref_t), GFP_KERNEL);
	if (bufpages->grant_list == NULL) {
		kfree(bufpages->page_list);
		bufpages->page_list = NULL;
		return -ENOMEM;
	}

	return 0;
}


int netfront_accel_alloc_buffer_mem(struct netfront_accel_bufpages *bufpages,
				    struct netfront_accel_bufinfo *rx_manager,
				    struct netfront_accel_bufinfo *tx_manager,
				    int pages)
{
	int n, rc;

	if ((rc = netfront_accel_alloc_buf_desc_blocks
	     (rx_manager, pages - (pages / sfc_netfront_buffer_split))) < 0) {
		goto rx_fail;
	}

	if ((rc = netfront_accel_alloc_buf_desc_blocks
	     (tx_manager, pages / sfc_netfront_buffer_split)) < 0) {
		goto tx_fail;
	}

	if ((rc = netfront_accel_alloc_buf_lists(bufpages, pages)) < 0) {
		goto lists_fail;
	}

	for (n = 0; n < pages; n++) {
		void *tmp = (void*)__get_free_page(GFP_KERNEL);
		if (tmp == NULL)
			break;

		bufpages->page_list[n] = tmp;
	}

	if (n != pages) {
		EPRINTK("%s: not enough pages: %d != %d\n", __FUNCTION__, n, 
			pages);
		for (; n >= 0; n--)
			free_page((unsigned long)(bufpages->page_list[n]));
		rc = -ENOMEM;
		goto pages_fail;
	}

	bufpages->max_pages = pages;
	bufpages->page_reqs = 0;

	return 0;

 pages_fail:
	kfree(bufpages->page_list);
	kfree(bufpages->grant_list);

	bufpages->page_list = NULL;
	bufpages->grant_list = NULL;
 lists_fail:
	kfree(tx_manager->desc_blocks);
	tx_manager->desc_blocks = NULL;

 tx_fail:
	kfree(rx_manager->desc_blocks);
	rx_manager->desc_blocks = NULL;
 rx_fail:
	return rc;
}


void netfront_accel_free_buffer_mem(struct netfront_accel_bufpages *bufpages,
				    struct netfront_accel_bufinfo *rx_manager,
				    struct netfront_accel_bufinfo *tx_manager)
{
	int i;

	for (i = 0; i < bufpages->max_pages; i++) {
		if (bufpages->grant_list[i] != 0)
			net_accel_ungrant_page(bufpages->grant_list[i]);
		free_page((unsigned long)(bufpages->page_list[i]));
	}

	if (bufpages->max_pages) {
		kfree(bufpages->page_list);
		kfree(bufpages->grant_list);
		kfree(rx_manager->desc_blocks);
		kfree(tx_manager->desc_blocks);
	}
}


/*
 * Allocate memory for the buffer manager and create a lock.  If no
 * lock is supplied its own is allocated.
 */
struct netfront_accel_bufinfo *netfront_accel_init_bufs(spinlock_t *lock)
{
	struct netfront_accel_bufinfo *res = kmalloc(sizeof(*res), GFP_KERNEL);
	if (res != NULL) {
		res->npages = res->nused = 0;
		res->first_free = -1;

		if (lock == NULL) {
			res->lock = kmalloc(sizeof(*res->lock), GFP_KERNEL);
			if (res->lock == NULL) {
				kfree(res);
				return NULL;
			}
			spin_lock_init(res->lock);
			res->internally_locked = 1;
		} else {
			res->lock = lock;
			res->internally_locked = 0;
		}
		
		res->desc_blocks = NULL;
	}

	return res;
}


void netfront_accel_fini_bufs(struct netfront_accel_bufinfo *bufs)
{
	if (bufs->internally_locked)
		kfree(bufs->lock);
	kfree(bufs);
}


int netfront_accel_buf_map_request(struct xenbus_device *dev,
				   struct netfront_accel_bufpages *bufpages,
				   struct net_accel_msg *msg, 
				   int pages, int offset)
{
	int i, mfn;
	int err;

	net_accel_msg_init(msg, NET_ACCEL_MSG_MAPBUF);

	BUG_ON(pages > NET_ACCEL_MSG_MAX_PAGE_REQ);

	msg->u.mapbufs.pages = pages;

	for (i = 0; i < msg->u.mapbufs.pages; i++) {
		/* 
		 * This can happen if we tried to send this message
		 * earlier but the queue was full.
		 */
		if (bufpages->grant_list[offset+i] != 0) {
			msg->u.mapbufs.grants[i] = 
				bufpages->grant_list[offset+i];
			continue;
		}

		mfn = virt_to_mfn(bufpages->page_list[offset+i]);
		VPRINTK("%s: Granting page %d, mfn %08x\n",
			__FUNCTION__, i, mfn);

		bufpages->grant_list[offset+i] =
			net_accel_grant_page(dev, mfn, 0);
		msg->u.mapbufs.grants[i] = bufpages->grant_list[offset+i];

		if (msg->u.mapbufs.grants[i] < 0) {
			EPRINTK("%s: Failed to grant buffer: %d\n",
				__FUNCTION__, msg->u.mapbufs.grants[i]);
			err = -EIO;
			goto error;
		}
	}

	/* This is interpreted on return as the offset in the the page_list */
	msg->u.mapbufs.reqid = offset;

	return 0;

error:
	/* Ungrant all the pages we've successfully granted. */
	for (i--; i >= 0; i--) {
		net_accel_ungrant_page(bufpages->grant_list[offset+i]);
		bufpages->grant_list[offset+i] = 0;
	}
	return err;
}


/* Process a response to a buffer request. */
int netfront_accel_add_bufs(struct netfront_accel_bufpages *bufpages,
			    struct netfront_accel_bufinfo *manager, 
			    struct net_accel_msg *msg)
{
	int msg_pages, page_offset, i, newtot;
	int old_block_count, new_block_count;
	u32 msg_buf;
	unsigned long flags;

	VPRINTK("%s: manager %p msg %p\n", __FUNCTION__, manager, msg);

	BUG_ON(msg->id != (NET_ACCEL_MSG_MAPBUF | NET_ACCEL_MSG_REPLY));

	msg_pages = msg->u.mapbufs.pages;
	msg_buf = msg->u.mapbufs.buf;
	page_offset = msg->u.mapbufs.reqid;

	spin_lock_irqsave(manager->lock, flags);
	newtot = manager->npages + msg_pages;
	old_block_count = 
		(manager->npages + NETFRONT_ACCEL_BUF_PAGES_PER_BLOCK - 1) >>
		NETFRONT_ACCEL_BUF_PAGES_PER_BLOCK_SHIFT;
	new_block_count = 
		(newtot + NETFRONT_ACCEL_BUF_PAGES_PER_BLOCK - 1) >>
		NETFRONT_ACCEL_BUF_PAGES_PER_BLOCK_SHIFT;

	for (i = old_block_count; i < new_block_count; i++) {
		struct netfront_accel_pkt_desc *block;
		if (manager->desc_blocks[i] != NULL) {
			VPRINTK("Not needed\n");
			continue;
		}
		block = kzalloc(NETFRONT_ACCEL_BUFS_PER_BLOCK * 
				sizeof(netfront_accel_pkt_desc), GFP_ATOMIC);
		if (block == NULL) {
			spin_unlock_irqrestore(manager->lock, flags);
			return -ENOMEM;
		}
		manager->desc_blocks[i] = block;
	}
	for (i = manager->npages; i < newtot; i++) {
		int k, j = i - manager->npages;
		int block_num;
		int block_idx;
		struct netfront_accel_pkt_desc *pkt;

		block_num = i >> NETFRONT_ACCEL_BUF_PAGES_PER_BLOCK_SHIFT;
		block_idx = (NETFRONT_ACCEL_BUFS_PER_PAGE*i)
			& (NETFRONT_ACCEL_BUFS_PER_BLOCK-1);

		pkt = manager->desc_blocks[block_num] + block_idx;
		
		for (k = 0; k < NETFRONT_ACCEL_BUFS_PER_PAGE; k++) {
			BUG_ON(page_offset + j >= bufpages->max_pages);

			pkt[k].buf_id = NETFRONT_ACCEL_BUFS_PER_PAGE * i + k;
			pkt[k].pkt_kva = bufpages->page_list[page_offset + j] +
				(PAGE_SIZE/NETFRONT_ACCEL_BUFS_PER_PAGE) * k;
			pkt[k].pkt_buff_addr = msg_buf +
				(PAGE_SIZE/NETFRONT_ACCEL_BUFS_PER_PAGE) * 
				(NETFRONT_ACCEL_BUFS_PER_PAGE * j + k);
			pkt[k].next_free = manager->first_free;
			manager->first_free = pkt[k].buf_id;
			*(int*)(pkt[k].pkt_kva) = pkt[k].buf_id;

			VPRINTK("buf %d desc %p kva %p buffaddr %x\n",
				pkt[k].buf_id, &(pkt[k]), pkt[k].pkt_kva, 
				pkt[k].pkt_buff_addr);
		}
	}
	manager->npages = newtot;
	spin_unlock_irqrestore(manager->lock, flags);
	VPRINTK("Added %d pages. Total is now %d\n", msg_pages,
		manager->npages);
	return 0;
}


netfront_accel_pkt_desc *
netfront_accel_buf_find(struct netfront_accel_bufinfo *manager, u16 id)
{
	netfront_accel_pkt_desc *pkt;
	int block_num = id >> NETFRONT_ACCEL_BUFS_PER_BLOCK_SHIFT;
	int block_idx = id & (NETFRONT_ACCEL_BUFS_PER_BLOCK - 1);
	BUG_ON(id >= manager->npages * NETFRONT_ACCEL_BUFS_PER_PAGE);
	BUG_ON(block_idx >= NETFRONT_ACCEL_BUFS_PER_BLOCK);
	pkt = manager->desc_blocks[block_num] + block_idx;
	return pkt;
}


/* Allocate a buffer from the buffer manager */
netfront_accel_pkt_desc *
netfront_accel_buf_get(struct netfront_accel_bufinfo *manager)
{
	int bufno = -1;
	netfront_accel_pkt_desc *buf = NULL;
	unsigned long flags = 0;

	/* Any spare? */
	if (manager->first_free == -1)
		return NULL;
	/* Take lock */
	if (manager->internally_locked)
		spin_lock_irqsave(manager->lock, flags);
	bufno = manager->first_free;
	if (bufno != -1) {
		buf = netfront_accel_buf_find(manager, bufno);
		manager->first_free = buf->next_free;
		manager->nused++;
	}
	/* Release lock */
	if (manager->internally_locked)
		spin_unlock_irqrestore(manager->lock, flags);

	/* Tell the world */
	VPRINTK("Allocated buffer %i, buffaddr %x\n", bufno,
		buf->pkt_buff_addr);

	return buf;
}


/* Release a buffer back to the buffer manager pool */
int netfront_accel_buf_put(struct netfront_accel_bufinfo *manager, u16 id)
{
	netfront_accel_pkt_desc *buf = netfront_accel_buf_find(manager, id);
	unsigned long flags = 0;
	unsigned was_empty = 0;
	int bufno = id;

	VPRINTK("Freeing buffer %i\n", id);
	BUG_ON(id == (u16)-1);

	if (manager->internally_locked)
		spin_lock_irqsave(manager->lock, flags);

	if (manager->first_free == -1)
		was_empty = 1;

	buf->next_free = manager->first_free;
	manager->first_free = bufno;
	manager->nused--;

	if (manager->internally_locked)
		spin_unlock_irqrestore(manager->lock, flags);

	return was_empty;
}
