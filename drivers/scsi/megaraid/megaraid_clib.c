/*
 *
 *			Linux MegaRAID device driver
 *
 * Copyright (c) 2003-2004  LSI Logic Corporation.
 *
 *	   This program is free software; you can redistribute it and/or
 *	   modify it under the terms of the GNU General Public License
 *	   as published by the Free Software Foundation; either version
 *	   2 of the License, or (at your option) any later version.
 *
 * FILE		: megaraid_clib.c
 * Version	: v2.20.0 (Apr 14 2004)
 *
 * Libaray of common routine used by all megaraid drivers.
 */

#include "kdep.h"
#include "mega_common.h"

static void __mraid_wakeup(unsigned long);

/*
 * debug level - threshold for amount of information to be displayed by the
 * driver. This level can be changed through modules parameters, ioctl or
 * sysfs/proc interface. By default, print the announcement messages only.
 */
int debug_level = CL_ANN;
MODULE_PARM(debug_level, "i");
MODULE_PARM_DESC(debug_level, "Debug level for driver (default=0)");

/**
 * mraid_setup_device_map - manage device ids
 * @adapter	: Driver's soft state
 *
 * Manange the device ids to have an appropraite mapping between the kernel
 * scsi addresses and megaraid scsi and logical drive addresses. We export
 * scsi devices on their actual addresses, whereas the logical drives are
 * exported on a virtual scsi channel.
 **/
void
mraid_setup_device_map(adapter_t *adapter)
{
	uint8_t		c;
	uint8_t		t;

	/*
	 * First fill the values on the logical drive channel
	 */
	for (t = 0; t < LSI_MAX_LOGICAL_DRIVES_64LD; t++)
		adapter->device_ids[adapter->max_channel][t] =
			(t < adapter->init_id) ?  t : t - 1;

	adapter->device_ids[adapter->max_channel][adapter->init_id] = 0xFF;

	/*
	 * Fill the values on the physical devices channels
	 */
	for (c = 0; c < adapter->max_channel; c++)
		for (t = 0; t < LSI_MAX_LOGICAL_DRIVES_64LD; t++)
			adapter->device_ids[c][t] = (c << 8) | t;
}


/*
 * Library to allocate memory regions which are DMA'able
 */

/**
 * mraid_pci_blk_pool_create - Creates a pool of pci consistent memory
 * blocks for DMA.
 * @dev		: pci device that will be doing the DMA
 * @blk_count	: number of memory blocks to be created.
 * @blk_size	: size of each blocks to be created.
 * @blk_align	: alignment requirement for blocks; must be a power of two
 * @blk		: array of DMA block pointers where each block information
 * returned.
 *
 * Returns a pci allocation pool handle with the requested characteristics,
 * or null if one can't be created.  mraid_pci_blk_pool_create()
 * may be used to allocate memory.  Such memory will all have "consistent"
 * DMA mappings, accessible by the device and its driver without using
 * cache flushing primitives.  The actual size of blocks allocated may be
 * larger than requested because of alignment.
 *
 * NOTE: This library must be used for requests less than PAGE_SIZE
 */
struct mraid_pci_blk_pool *
mraid_pci_blk_pool_create(struct pci_dev *dev, size_t blk_count, size_t
	blk_size, size_t blk_align, struct mraid_pci_blk blk[])
{
	struct mraid_pci_blk_pool	*pool;
	size_t				each_blk_size = blk_size;
	int				blks_per_page;
	int				num_pages;
	int				pg_idx;
	caddr_t				page_addr;
	dma_addr_t			tmp_dmah;
	int				i;

	if (blk_align == 0) blk_align = 1;

	if (blk_count == 0 || each_blk_size == 0) return NULL;

	if (each_blk_size < blk_align) {
		each_blk_size = blk_align;
	}
	else if ((each_blk_size % blk_align) != 0) {
		each_blk_size += blk_align + 1;
		each_blk_size &= ~(blk_align - 1);
	}

	/*
	 * each_blk_size is the actual size that we apportion for each block.
	 * Note that each_blk_size >= blk_size
	 *
	 * Next task is to find the number of blocks (each_blk_size) that we
	 * can fit in a single page.
	 */
	blks_per_page = PAGE_SIZE / each_blk_size;

	if (!blks_per_page) {
		con_log(CL_ANN, (KERN_WARNING
		"megaraid: memlib request more than PAGE_SIZE buffer.\n"));

		return NULL;
	}

	/*
	 * If the number of blocks that we cat fit in a page is blks_per_page,
	 * then the total number of pages required to accommodate "blk_count"
	 * number of blocks is ...
	 */
	num_pages = ( (blk_count - 1) / blks_per_page ) + 1;

	if (num_pages > MEMLIB_MAX_PAGES) {
		con_log(CL_ANN, (KERN_WARNING
		"megaraid: not allocating %#x pages\n", num_pages));

		return NULL;
	}

	/*
	 * Let us allocate the mraid_pci_blk_pool first. This is akin to the
	 * handle that the client must pass back to us to deallocate memory
	 */
	pool = kmalloc(sizeof (struct mraid_pci_blk_pool), GFP_KERNEL);
	if (pool == NULL) return NULL;

	/*
	 * Initialize the pool
	 */
	memset(pool, 0, sizeof(struct mraid_pci_blk_pool));
	pool->page_count = num_pages;
	pool->dev = dev;

	/*
	 * Allocate all pages in a loop
	 */
	for (i = 0; i < num_pages; i++) {

		pool->page_arr[i] = pci_alloc_consistent( dev, PAGE_SIZE-1,
							&pool->dmah_arr[i] );
		if (pool->page_arr[i] == NULL) {
			con_log(CL_ANN, (KERN_WARNING
				"megaraid: Failed to alloc page # %d\n", i ));
			goto memlib_fail_alloc;
		}
	}


	/*
	 * Now we have required number of pages. All we have to do is to divy
	 * up each page into blks_per_page number of blocks
	 */
	pg_idx		= -1;
	page_addr	= NULL;
	tmp_dmah	= 0;

	for (i = 0; i < blk_count; i++) {

		if ((i % blks_per_page) == 0) {
			pg_idx++;
			page_addr	= pool->page_arr[ pg_idx ];
			tmp_dmah	= pool->dmah_arr[ pg_idx ];
		}

		blk[i].vaddr	= page_addr;
		blk[i].dma_addr	= tmp_dmah;

		page_addr	+= each_blk_size;
		tmp_dmah	+= each_blk_size;
	}

	return pool;

memlib_fail_alloc:

	mraid_pci_blk_pool_destroy( pool );
	return NULL;
}


/**
 * mraid_pci_pool_destroy - destroys a pool of pci memory blocks.
 * @blk_pool	: pci block pool that will be destroyed
 *
 * Caller guarantees that no more memory from the pool is in use.
 */
void
mraid_pci_blk_pool_destroy(struct mraid_pci_blk_pool *blk_pool)
{
	int i;

	if (!blk_pool) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid critical: null pointer for pool destroy\n"));
		return;
	}

	for (i = 0; i < blk_pool->page_count; i++) {

		if (blk_pool->page_arr[i]) {
			pci_free_consistent( blk_pool->dev, PAGE_SIZE-1,
						blk_pool->page_arr[i],
						blk_pool->dmah_arr[i] );
			blk_pool->dmah_arr[i] = 0;
			blk_pool->page_arr[i] = NULL;
		}
	}

	kfree( blk_pool );
}


/**
 * mraid_sleep - induces sleep for specified number of seconds
 * @param seconds	: number of seconds to sleep for
 */
void
mraid_sleep(int seconds)
{
	struct timer_list	timer;
	struct __sleep_obj {
		wait_queue_head_t       wq;
		int			sem;
	} sleep_obj;


	init_waitqueue_head(&sleep_obj.wq);
	sleep_obj.sem = 0;

	init_timer(&timer);
	timer.function	= __mraid_wakeup;
	timer.data	= (unsigned long)&sleep_obj;
	timer.expires	= jiffies + seconds*HZ;
	add_timer(&timer);

	wait_event(sleep_obj.wq, sleep_obj.sem);

}

static void
__mraid_wakeup(unsigned long ptr)
{
	struct __sleep_obj {
		wait_queue_head_t       wq;
		int			sem;
	} *sleep_objp;

	sleep_objp = (struct __sleep_obj *)ptr;

	wake_up(&sleep_objp->wq);
}

/* vim: set ts=8 sw=8 tw=78 ai si: */
