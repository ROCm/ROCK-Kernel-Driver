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

#ifndef NETFRONT_ACCEL_BUFS_H
#define NETFRONT_ACCEL_BUFS_H

#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <xen/xenbus.h>

#include "accel_msg_iface.h"


/*! Buffer descriptor structure */
typedef struct netfront_accel_pkt_desc {
	int buf_id;
	u32 pkt_buff_addr;
	void *pkt_kva;
	/* This is the socket buffer currently married to this buffer */
	struct sk_buff *skb;
	int next_free;
} netfront_accel_pkt_desc;


#define NETFRONT_ACCEL_DEFAULT_BUF_PAGES (384)
#define NETFRONT_ACCEL_BUF_PAGES_PER_BLOCK_SHIFT (4)
#define NETFRONT_ACCEL_BUF_PAGES_PER_BLOCK		\
	(1 << (NETFRONT_ACCEL_BUF_PAGES_PER_BLOCK_SHIFT))
#define NETFRONT_ACCEL_BUFS_PER_PAGE_SHIFT (1)
#define NETFRONT_ACCEL_BUFS_PER_PAGE			\
	(1 << (NETFRONT_ACCEL_BUFS_PER_PAGE_SHIFT))
#define NETFRONT_ACCEL_BUFS_PER_BLOCK_SHIFT		\
	(NETFRONT_ACCEL_BUF_PAGES_PER_BLOCK_SHIFT +     \
	 NETFRONT_ACCEL_BUFS_PER_PAGE_SHIFT)
#define NETFRONT_ACCEL_BUFS_PER_BLOCK			\
	(1 << NETFRONT_ACCEL_BUFS_PER_BLOCK_SHIFT)
#define NETFRONT_ACCEL_BUF_NUM_BLOCKS(max_pages)			\
	(((max_pages)+NETFRONT_ACCEL_BUF_PAGES_PER_BLOCK-1) /		\
	 NETFRONT_ACCEL_BUF_PAGES_PER_BLOCK)

/*! Buffer management structure. */
struct netfront_accel_bufinfo {
	/* number added to this manager */
	unsigned npages;
	/* number currently used from this manager */
	unsigned nused;

	int first_free;

	int internally_locked;
	spinlock_t *lock;

	/*
	 * array of pointers (length NETFRONT_ACCEL_BUF_NUM_BLOCKS) to
	 * pkt descs
	 */
	struct netfront_accel_pkt_desc **desc_blocks; 
};


struct netfront_accel_bufpages {
	/* length of lists of pages/grants */
	int max_pages;
	/* list of pages allocated for network buffers */
	void **page_list;
	/* list of grants for the above pages */
	grant_ref_t *grant_list;
	
	/* number of page requests that have been made */
	unsigned page_reqs;
};


/*! Allocate memory for the buffer manager, set up locks etc.
 * Optionally takes a lock to use, if not supplied it makes its own.
 *
 * \return pointer to netfront_accel_bufinfo structure that represents the
 * buffer manager
 */
extern struct netfront_accel_bufinfo *
netfront_accel_init_bufs(spinlock_t *lock);

/*! Allocate memory for the buffers
 */
extern int
netfront_accel_alloc_buffer_mem(struct netfront_accel_bufpages *bufpages,
				struct netfront_accel_bufinfo *rx_res,
				struct netfront_accel_bufinfo *tx_res,
				int pages);
extern void
netfront_accel_free_buffer_mem(struct netfront_accel_bufpages *bufpages,
			       struct netfront_accel_bufinfo *rx_res,
			       struct netfront_accel_bufinfo *tx_res);

/*! Release memory for the buffer manager, buffers, etc.
 *
 * \param manager pointer to netfront_accel_bufinfo structure that
 * represents the buffer manager
 */
extern void netfront_accel_fini_bufs(struct netfront_accel_bufinfo *manager);

/*! Release a buffer.
 *
 * \param manager  The buffer manager which owns the buffer.
 * \param id   The buffer identifier.
 */
extern int netfront_accel_buf_put(struct netfront_accel_bufinfo *manager, 
				  u16 id);

/*! Get the packet descriptor associated with a buffer id.
 *
 * \param manager  The buffer manager which owns the buffer.
 * \param id       The buffer identifier.
 *
 * The returned value is the packet descriptor for this buffer.
 */
extern netfront_accel_pkt_desc *
netfront_accel_buf_find(struct netfront_accel_bufinfo *manager, u16 id);


/*! Fill out a message request for some buffers to be mapped by the
 * back end driver
 * 
 * \param manager The buffer manager 
 * \param msg Pointer to an ef_msg to complete.
 * \return 0 on success
 */
extern int 
netfront_accel_buf_map_request(struct xenbus_device *dev,
			       struct netfront_accel_bufpages *bufpages,
			       struct net_accel_msg *msg, 
			       int pages, int offset);

/*! Process a response to a buffer request. 
 * 
 * Deal with a received message from the back end in response to our
 * request for buffers
 * 
 * \param manager The buffer manager
 * \param msg The received message from the back end describing new
 * buffers
 * \return 0 on success
 */
extern int 
netfront_accel_add_bufs(struct netfront_accel_bufpages *bufpages,
			struct netfront_accel_bufinfo *manager,
			struct net_accel_msg *msg);


/*! Allocate a buffer from the buffer manager 
 *
 * \param manager The buffer manager data structure
 * \param id On exit, the id of the buffer allocated
 * \return Pointer to buffer descriptor.
 */
struct netfront_accel_pkt_desc *
netfront_accel_buf_get(struct netfront_accel_bufinfo *manager);

#endif /* NETFRONT_ACCEL_BUFS_H */

