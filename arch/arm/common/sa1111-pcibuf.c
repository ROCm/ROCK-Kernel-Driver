/*
 *  linux/arch/arm/mach-sa1100/sa1111-pcibuf.c
 *
 *  Special dma_{map/unmap/dma_sync}_* routines for SA-1111.
 *
 *  These functions utilize bouncer buffers to compensate for a bug in
 *  the SA-1111 hardware which don't allow DMA to/from addresses
 *  certain addresses above 1MB.
 *
 *  Re-written by Christopher Hoover <ch@murgatroid.com>
 *  Original version by Brad Parker (brad@heeltoe.com)
 *
 *  Copyright (C) 2002 Hewlett Packard Company.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 * */

//#define DEBUG

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <asm/hardware/sa1111.h>

//#define STATS
#ifdef STATS
#define DO_STATS(X) do { X ; } while (0)
#else
#define DO_STATS(X) do { } while (0)
#endif

/* ************************************************** */

struct safe_buffer {
	struct list_head node;

	/* original request */
	void		*ptr;
	size_t		size;
	enum dma_data_direction direction;

	/* safe buffer info */
	struct dma_pool *pool;
	void		*safe;
	dma_addr_t	safe_dma_addr;
	struct device	*dev;
};

static LIST_HEAD(safe_buffers);


#define SIZE_SMALL	1024
#define SIZE_LARGE	(4*1024)

static struct dma_pool *small_buffer_pool, *large_buffer_pool;

#ifdef STATS
static unsigned long sbp_allocs __initdata = 0;
static unsigned long lbp_allocs __initdata = 0;
static unsigned long total_allocs __initdata= 0;

static void print_alloc_stats(void)
{
	printk(KERN_INFO
	       "sa1111_dmabuf: sbp: %lu, lbp: %lu, other: %lu, total: %lu\n",
	       sbp_allocs, lbp_allocs,
	       total_allocs - sbp_allocs - lbp_allocs, total_allocs);
}
#endif

static int __init create_safe_buffer_pools(void)
{
	small_buffer_pool = dma_pool_create("sa1111_small_dma_buffer",
					    NULL, SIZE_SMALL,
					    0 /* byte alignment */,
					    0 /* no page-crossing issues */);
	if (small_buffer_pool == NULL) {
		printk(KERN_ERR
		       "sa1111_dmabuf: could not allocate small pci pool\n");
		return -ENOMEM;
	}

	large_buffer_pool = dma_pool_create("sa1111_large_dma_buffer",
					    NULL, SIZE_LARGE,
					    0 /* byte alignment */,
					    0 /* no page-crossing issues */);
	if (large_buffer_pool == NULL) {
		printk(KERN_ERR
		       "sa1111_dmabuf: could not allocate large pci pool\n");
		dma_pool_destroy(small_buffer_pool);
		small_buffer_pool = NULL;
		return -ENOMEM;
	}

	printk(KERN_INFO "SA1111: DMA buffer sizes: small=%u, large=%u\n",
	       SIZE_SMALL, SIZE_LARGE);

	return 0;
}

static void __exit destroy_safe_buffer_pools(void)
{
	if (small_buffer_pool)
		dma_pool_destroy(small_buffer_pool);
	if (large_buffer_pool)
		dma_pool_destroy(large_buffer_pool);

	small_buffer_pool = large_buffer_pool = NULL;
}


/* allocate a 'safe' buffer and keep track of it */
static struct safe_buffer *alloc_safe_buffer(struct device *dev, void *ptr,
					     size_t size,
					     enum dma_data_direction dir)
{
	struct safe_buffer *buf;
	struct dma_pool *pool;
	void *safe;
	dma_addr_t safe_dma_addr;

	dev_dbg(dev, "%s(ptr=%p, size=%d, direction=%d)\n",
		__func__, ptr, size, dir);

	DO_STATS ( total_allocs++ );

	buf = kmalloc(sizeof(struct safe_buffer), GFP_ATOMIC);
	if (buf == NULL) {
		printk(KERN_WARNING "%s: kmalloc failed\n", __func__);
		return 0;
	}

	if (size <= SIZE_SMALL) {
		pool = small_buffer_pool;
		safe = dma_pool_alloc(pool, GFP_ATOMIC, &safe_dma_addr);

		DO_STATS ( sbp_allocs++ );
	} else if (size <= SIZE_LARGE) {
		pool = large_buffer_pool;
		safe = dma_pool_alloc(pool, GFP_ATOMIC, &safe_dma_addr);

		DO_STATS ( lbp_allocs++ );
	} else {
		pool = NULL;
		safe = dma_alloc_coherent(dev, size, &safe_dma_addr, GFP_ATOMIC);
	}

	if (safe == NULL) {
		printk(KERN_WARNING
		       "%s: could not alloc dma memory (size=%d)\n",
		       __func__, size);
		kfree(buf);
		return 0;
	}

#ifdef STATS
	if (total_allocs % 1000 == 0)
		print_alloc_stats();
#endif

	BUG_ON(sa1111_check_dma_bug(safe_dma_addr));	// paranoia

	buf->ptr = ptr;
	buf->size = size;
	buf->direction = dir;
	buf->pool = pool;
	buf->safe = safe;
	buf->safe_dma_addr = safe_dma_addr;
	buf->dev = dev;

	list_add(&buf->node, &safe_buffers);

	return buf;
}

/* determine if a buffer is from our "safe" pool */
static struct safe_buffer *find_safe_buffer(struct device *dev,
					    dma_addr_t safe_dma_addr)
{
	struct list_head *entry;

	list_for_each(entry, &safe_buffers) {
		struct safe_buffer *b =
			list_entry(entry, struct safe_buffer, node);

		if (b->safe_dma_addr == safe_dma_addr &&
		    b->dev == dev) {
			return b;
		}
	}

	return 0;
}

static void free_safe_buffer(struct safe_buffer *buf)
{
	pr_debug("%s(buf=%p)\n", __func__, buf);

	list_del(&buf->node);

	if (buf->pool)
		dma_pool_free(buf->pool, buf->safe, buf->safe_dma_addr);
	else
		dma_free_coherent(buf->dev, buf->size, buf->safe,
				  buf->safe_dma_addr);
	kfree(buf);
}

static inline int dma_range_is_safe(struct device *dev, dma_addr_t addr,
				    size_t size)
{
	unsigned int physaddr = SA1111_DMA_ADDR((unsigned int) addr);

	/* Any address within one megabyte of the start of the target
         * bank will be OK.  This is an overly conservative test:
         * other addresses can be OK depending on the dram
         * configuration.  (See sa1111.c:sa1111_check_dma_bug() * for
         * details.)
	 *
	 * We take care to ensure the entire dma region is within
	 * the safe range.
	 */

	return ((physaddr + size - 1) < (1<<20));
}

/* ************************************************** */

#ifdef STATS
static unsigned long map_op_count __initdata = 0;
static unsigned long bounce_count __initdata = 0;

static void print_map_stats(void)
{
	printk(KERN_INFO
	       "sa1111_dmabuf: map_op_count=%lu, bounce_count=%lu\n",
	       map_op_count, bounce_count);
}
#endif

static dma_addr_t map_single(struct device *dev, void *ptr,
			     size_t size, enum dma_data_direction dir)
{
	dma_addr_t dma_addr;

	DO_STATS ( map_op_count++ );

	dma_addr = virt_to_bus(ptr);

	if (!dma_range_is_safe(dev, dma_addr, size)) {
		struct safe_buffer *buf;

		DO_STATS ( bounce_count++ ) ;

		buf = alloc_safe_buffer(dev, ptr, size, dir);
		if (buf == NULL) {
			printk(KERN_ERR
			       "%s: unable to map unsafe buffer %p!\n",
			       __func__, ptr);
			return 0;
		}

		dev_dbg(dev, "%s: unsafe buffer %p (phy=%08lx) mapped to %p (phy=%08x)\n",
			__func__,
			buf->ptr, virt_to_bus(buf->ptr),
			buf->safe, buf->safe_dma_addr);

		if (dir == DMA_TO_DEVICE || dir == DMA_BIDIRECTIONAL) {
			dev_dbg(dev, "%s: copy out from unsafe %p, to safe %p, size %d\n",
				__func__, ptr, buf->safe, size);
			memcpy(buf->safe, ptr, size);
		}

		dma_addr = buf->safe_dma_addr;
		ptr = buf->safe;
	}

	consistent_sync(ptr, size, dir);

#ifdef STATS
	if (map_op_count % 1000 == 0)
		print_map_stats();
#endif

	return dma_addr;
}

static void unmap_single(struct device *dev, dma_addr_t dma_addr,
			 size_t size, enum dma_data_direction dir)
{
	struct safe_buffer *buf;

	buf = find_safe_buffer(dev, dma_addr);

	if (buf) {
		BUG_ON(buf->size != size);
		BUG_ON(buf->direction != dir);

		dev_dbg(dev, "%s: unsafe buffer %p (phy=%08lx) mapped to %p (phy=%08lx)\n",
			__func__,
			buf->ptr, virt_to_bus(buf->ptr),
			buf->safe, buf->safe_dma_addr);

		DO_STATS ( bounce_count++ );

		if (dir == DMA_FROM_DEVICE || dir == DMA_BIDIRECTIONAL) {
			dev_dbg(dev, "%s: copy back from safe %p, to unsafe %p size %d\n",
				__func__, buf->safe, buf->ptr, size);
			memcpy(buf->ptr, buf->safe, size);
		}
		free_safe_buffer(buf);
	}
}

static void sync_single(struct device *dev, dma_addr_t dma_addr,
			size_t size, enum dma_data_direction dir)
{
	struct safe_buffer *buf;
	void *ptr;

	buf = find_safe_buffer(dev, dma_addr);

	if (buf) {
		BUG_ON(buf->size != size);
		BUG_ON(buf->direction != dir);

		dev_dbg(dev, "%s: unsafe buffer %p (phy=%08lx) mapped to %p (phy=%08lx)\n",
			__func__,
			buf->ptr, virt_to_bus(buf->ptr),
			buf->safe, buf->safe_dma_addr);

		DO_STATS ( bounce_count++ );

		switch (dir) {
		case DMA_FROM_DEVICE:
			dev_dbg(dev, "%s: copy back from safe %p, to unsafe %p size %d\n",
				__func__, buf->safe, buf->ptr, size);
			memcpy(buf->ptr, buf->safe, size);
			break;
		case DMA_TO_DEVICE:
			dev_dbg(dev, "%s: copy out from unsafe %p, to safe %p, size %d\n",
				__func__,buf->ptr, buf->safe, size);
			memcpy(buf->safe, buf->ptr, size);
			break;
		case DMA_BIDIRECTIONAL:
			BUG();	/* is this allowed?  what does it mean? */
		default:
			BUG();
		}
		ptr = buf->safe;
	} else {
		ptr = bus_to_virt(dma_addr);
	}
	consistent_sync(ptr, size, dir);
}

/* ************************************************** */

/*
 * see if a buffer address is in an 'unsafe' range.  if it is
 * allocate a 'safe' buffer and copy the unsafe buffer into it.
 * substitute the safe buffer for the unsafe one.
 * (basically move the buffer from an unsafe area to a safe one)
 */
dma_addr_t sa1111_map_single(struct device *dev, void *ptr,
			     size_t size, enum dma_data_direction dir)
{
	unsigned long flags;
	dma_addr_t dma_addr;

	dev_dbg(dev, "%s(ptr=%p,size=%d,dir=%x)\n",
	       __func__, ptr, size, dir);

	BUG_ON(dir == DMA_NONE);

	local_irq_save(flags);

	dma_addr = map_single(dev, ptr, size, dir);

	local_irq_restore(flags);

	return dma_addr;
}

/*
 * see if a mapped address was really a "safe" buffer and if so, copy
 * the data from the safe buffer back to the unsafe buffer and free up
 * the safe buffer.  (basically return things back to the way they
 * should be)
 */
void sa1111_unmap_single(struct device *dev, dma_addr_t dma_addr,
		         size_t size, enum dma_data_direction dir)
{
	unsigned long flags;

	dev_dbg(dev, "%s(ptr=%08lx,size=%d,dir=%x)\n",
		__func__, dma_addr, size, dir);

	local_irq_save(flags);

	unmap_single(dev, dma_addr, size, dir);

	local_irq_restore(flags);
}

int sa1111_map_sg(struct device *dev, struct scatterlist *sg,
		  int nents, enum dma_data_direction dir)
{
	unsigned long flags;
	int i;

	dev_dbg(dev, "%s(sg=%p,nents=%d,dir=%x)\n",
		__func__, sg, nents, dir);

	BUG_ON(dir == DMA_NONE);

	local_irq_save(flags);

	for (i = 0; i < nents; i++, sg++) {
		struct page *page = sg->page;
		unsigned int offset = sg->offset;
		unsigned int length = sg->length;
		void *ptr = page_address(page) + offset;

		sg->dma_address = map_single(dev, ptr, length, dir);
	}

	local_irq_restore(flags);

	return nents;
}

void sa1111_unmap_sg(struct device *dev, struct scatterlist *sg,
		     int nents, enum dma_data_direction dir)
{
	unsigned long flags;
	int i;

	dev_dbg(dev, "%s(sg=%p,nents=%d,dir=%x)\n",
		__func__, sg, nents, dir);

	local_irq_save(flags);

	for (i = 0; i < nents; i++, sg++) {
		dma_addr_t dma_addr = sg->dma_address;
		unsigned int length = sg->length;

		unmap_single(dev, dma_addr, length, dir);
	}

	local_irq_restore(flags);
}

void sa1111_dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_addr,
				    size_t size, enum dma_data_direction dir)
{
	unsigned long flags;

	dev_dbg(dev, "%s(ptr=%08lx,size=%d,dir=%x)\n",
		__func__, dma_addr, size, dir);

	local_irq_save(flags);

	sync_single(dev, dma_addr, size, dir);

	local_irq_restore(flags);
}

void sa1111_dma_sync_single_for_device(struct device *dev, dma_addr_t dma_addr,
				       size_t size, enum dma_data_direction dir)
{
	unsigned long flags;

	dev_dbg(dev, "%s(ptr=%08lx,size=%d,dir=%x)\n",
		__func__, dma_addr, size, dir);

	local_irq_save(flags);

	sync_single(dev, dma_addr, size, dir);

	local_irq_restore(flags);
}

void sa1111_dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
				int nents, enum dma_data_direction dir)
{
	unsigned long flags;
	int i;

	dev_dbg(dev, "%s(sg=%p,nents=%d,dir=%x)\n",
		__func__, sg, nents, dir);

	local_irq_save(flags);

	for (i = 0; i < nents; i++, sg++) {
		dma_addr_t dma_addr = sg->dma_address;
		unsigned int length = sg->length;

		sync_single(dev, dma_addr, length, dir);
	}

	local_irq_restore(flags);
}

void sa1111_dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
				   int nents, enum dma_data_direction dir)
{
	unsigned long flags;
	int i;

	dev_dbg(dev, "%s(sg=%p,nents=%d,dir=%x)\n",
		__func__, sg, nents, dir);

	local_irq_save(flags);

	for (i = 0; i < nents; i++, sg++) {
		dma_addr_t dma_addr = sg->dma_address;
		unsigned int length = sg->length;

		sync_single(dev, dma_addr, length, dir);
	}

	local_irq_restore(flags);
}

EXPORT_SYMBOL(sa1111_map_single);
EXPORT_SYMBOL(sa1111_unmap_single);
EXPORT_SYMBOL(sa1111_map_sg);
EXPORT_SYMBOL(sa1111_unmap_sg);
EXPORT_SYMBOL(sa1111_dma_sync_single_for_cpu);
EXPORT_SYMBOL(sa1111_dma_sync_single_for_device);
EXPORT_SYMBOL(sa1111_dma_sync_sg_for_cpu);
EXPORT_SYMBOL(sa1111_dma_sync_sg_for_device);

/* **************************************** */

static int __init sa1111_dmabuf_init(void)
{
	printk(KERN_DEBUG "sa1111_dmabuf: initializing SA-1111 DMA buffers\n");

	return create_safe_buffer_pools();
}
module_init(sa1111_dmabuf_init);

static void __exit sa1111_dmabuf_exit(void)
{
	BUG_ON(!list_empty(&safe_buffers));

#ifdef STATS
	print_alloc_stats();
	print_map_stats();
#endif

	destroy_safe_buffer_pools();
}
module_exit(sa1111_dmabuf_exit);

MODULE_AUTHOR("Christopher Hoover <ch@hpl.hp.com>");
MODULE_DESCRIPTION("Special dma_{map/unmap/dma_sync}_* routines for SA-1111.");
MODULE_LICENSE("GPL");
