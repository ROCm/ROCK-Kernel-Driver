/*
 *  linux/arch/arm/mach-sa1100/pci-sa1111.c
 *
 *  Special pci_{map/unmap/dma_sync}_* routines for SA-1111.
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <asm/hardware/sa1111.h>

//#define DEBUG
#ifdef DEBUG
#define DPRINTK(...) do { printk(KERN_DEBUG __VA_ARGS__); } while (0)
#else
#define DPRINTK(...) do { } while (0)
#endif

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
	int		direction;

	/* safe buffer info */
	struct pci_pool *pool;
	void		*safe;
	dma_addr_t	safe_dma_addr;
};

static LIST_HEAD(safe_buffers);


#define SIZE_SMALL	1024
#define SIZE_LARGE	(4*1024)

static struct pci_pool *small_buffer_pool, *large_buffer_pool;

#ifdef STATS
static unsigned long sbp_allocs __initdata = 0;
static unsigned long lbp_allocs __initdata = 0;
static unsigned long total_allocs __initdata= 0;

static void print_alloc_stats(void)
{
	printk(KERN_INFO
	       "sa1111_pcibuf: sbp: %lu, lbp: %lu, other: %lu, total: %lu\n",
	       sbp_allocs, lbp_allocs,
	       total_allocs - sbp_allocs - lbp_allocs, total_allocs);
}
#endif

static int __init
create_safe_buffer_pools(void)
{
	small_buffer_pool = pci_pool_create("sa1111_small_dma_buffer",
					    SA1111_FAKE_PCIDEV,
					    SIZE_SMALL,
					    0 /* byte alignment */,
					    0 /* no page-crossing issues */);
	if (0 == small_buffer_pool) {
		printk(KERN_ERR
		       "sa1111_pcibuf: could not allocate small pci pool\n");
		return -1;
	}

	large_buffer_pool = pci_pool_create("sa1111_large_dma_buffer",
					    SA1111_FAKE_PCIDEV,
					    SIZE_LARGE,
					    0 /* byte alignment */,
					    0 /* no page-crossing issues */);
	if (0 == large_buffer_pool) {
		printk(KERN_ERR
		       "sa1111_pcibuf: could not allocate large pci pool\n");
		pci_pool_destroy(small_buffer_pool);
		small_buffer_pool = 0;
		return -1;
	}

	printk(KERN_INFO
	       "sa1111_pcibuf: buffer sizes: small=%u, large=%u\n",
	       SIZE_SMALL, SIZE_LARGE);

	return 0;
}

static void __exit
destroy_safe_buffer_pools(void)
{
	if (small_buffer_pool)
		pci_pool_destroy(small_buffer_pool);
	if (large_buffer_pool)
		pci_pool_destroy(large_buffer_pool);

	small_buffer_pool = large_buffer_pool = 0;
}


/* allocate a 'safe' buffer and keep track of it */
static struct safe_buffer *
alloc_safe_buffer(void *ptr, size_t size, int direction)
{
	struct safe_buffer *buf;
	struct pci_pool *pool;
	void *safe;
	dma_addr_t safe_dma_addr;

	DPRINTK("%s(ptr=%p, size=%d, direction=%d)\n",
		__func__, ptr, size, direction);

	DO_STATS ( total_allocs++ );

	buf = kmalloc(sizeof(struct safe_buffer), GFP_ATOMIC);
	if (buf == 0) {
		printk(KERN_WARNING "%s: kmalloc failed\n", __func__);
		return 0;
	}

	if (size <= SIZE_SMALL) {
		pool = small_buffer_pool;
		safe = pci_pool_alloc(pool, GFP_ATOMIC, &safe_dma_addr);

		DO_STATS ( sbp_allocs++ );
	} else if (size <= SIZE_LARGE) {
		pool = large_buffer_pool;
		safe = pci_pool_alloc(pool, GFP_ATOMIC, &safe_dma_addr);

		DO_STATS ( lbp_allocs++ );
	} else {
		pool = 0;
		safe = pci_alloc_consistent(SA1111_FAKE_PCIDEV, size,
					    &safe_dma_addr);
	}

	if (safe == 0) {
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
	buf->direction = direction;
	buf->pool = pool;
	buf->safe = safe;
	buf->safe_dma_addr = safe_dma_addr;

	MOD_INC_USE_COUNT;
	list_add(&buf->node, &safe_buffers);

	return buf;
}

/* determine if a buffer is from our "safe" pool */
static struct safe_buffer *
find_safe_buffer(dma_addr_t safe_dma_addr)
{
	struct list_head *entry;

	list_for_each(entry, &safe_buffers) {
		struct safe_buffer *b =
			list_entry(entry, struct safe_buffer, node);

		if (b->safe_dma_addr == safe_dma_addr) {
			return b;
		}
	}

	return 0;
}

static void
free_safe_buffer(struct safe_buffer *buf)
{
	DPRINTK("%s(buf=%p)\n", __func__, buf);

	list_del(&buf->node);

	if (buf->pool)
		pci_pool_free(buf->pool, buf->safe, buf->safe_dma_addr);
	else
		pci_free_consistent(SA1111_FAKE_PCIDEV, buf->size, buf->safe,
				    buf->safe_dma_addr);
	kfree(buf);

	MOD_DEC_USE_COUNT;
}

static inline int
dma_range_is_safe(dma_addr_t addr, size_t size)
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
	       "sa1111_pcibuf: map_op_count=%lu, bounce_count=%lu\n",
	       map_op_count, bounce_count);
}
#endif

static dma_addr_t
map_single(void *ptr, size_t size, int direction)
{
	dma_addr_t dma_addr;

	DO_STATS ( map_op_count++ );

	dma_addr = virt_to_bus(ptr);

	if (!dma_range_is_safe(dma_addr, size)) {
		struct safe_buffer *buf;

		DO_STATS ( bounce_count++ ) ;

		buf = alloc_safe_buffer(ptr, size, direction);
		if (buf == 0) {
			printk(KERN_ERR
			       "%s: unable to map unsafe buffer %p!\n",
			       __func__, ptr);
			return 0;
		}

		DPRINTK("%s: unsafe buffer %p (phy=%p) mapped to %p (phy=%p)\n",
			__func__,
			buf->ptr, (void *) virt_to_bus(buf->ptr),
			buf->safe, (void *) buf->safe_dma_addr);

		if ((direction == PCI_DMA_TODEVICE) ||
		    (direction == PCI_DMA_BIDIRECTIONAL)) {
			DPRINTK("%s: copy out from unsafe %p, to safe %p, size %d\n",
				__func__, ptr, buf->safe, size);
			memcpy(buf->safe, ptr, size);
		}
		consistent_sync(buf->safe, size, direction);

		dma_addr = buf->safe_dma_addr;
	} else {
		consistent_sync(ptr, size, direction);
	}

#ifdef STATS
	if (map_op_count % 1000 == 0)
		print_map_stats();
#endif

	return dma_addr;
}

static void
unmap_single(dma_addr_t dma_addr, size_t size, int direction)
{
	struct safe_buffer *buf;

	buf = find_safe_buffer(dma_addr);

	if (buf) {
		BUG_ON(buf->size != size);
		BUG_ON(buf->direction != direction);

		DPRINTK("%s: unsafe buffer %p (phy=%p) mapped to %p (phy=%p)\n",
			__func__,
			buf->ptr, (void *) virt_to_bus(buf->ptr),
			buf->safe, (void *) buf->safe_dma_addr);


		DO_STATS ( bounce_count++ );

		if ((direction == PCI_DMA_FROMDEVICE) ||
		    (direction == PCI_DMA_BIDIRECTIONAL)) {
			DPRINTK("%s: copy back from safe %p, to unsafe %p size %d\n",
				__func__, buf->safe, buf->ptr, size);
			memcpy(buf->ptr, buf->safe, size);
		}
		free_safe_buffer(buf);
	}
}

static void
sync_single(dma_addr_t dma_addr, size_t size, int direction)
{
	struct safe_buffer *buf;

	buf = find_safe_buffer(dma_addr);

	if (buf) {
		BUG_ON(buf->size != size);
		BUG_ON(buf->direction != direction);

		DPRINTK("%s: unsafe buffer %p (phy=%p) mapped to %p (phy=%p)\n",
			__func__,
			buf->ptr, (void *) virt_to_bus(buf->ptr),
			buf->safe, (void *) buf->safe_dma_addr);

		DO_STATS ( bounce_count++ );

		switch (direction) {
		case PCI_DMA_FROMDEVICE:
			DPRINTK("%s: copy back from safe %p, to unsafe %p size %d\n",
				__func__, buf->safe, buf->ptr, size);
			memcpy(buf->ptr, buf->safe, size);
			break;
		case PCI_DMA_TODEVICE:
			DPRINTK("%s: copy out from unsafe %p, to safe %p, size %d\n",
				__func__,buf->ptr, buf->safe, size);
			memcpy(buf->safe, buf->ptr, size);
			break;
		case PCI_DMA_BIDIRECTIONAL:
			BUG();	/* is this allowed?  what does it mean? */
		default:
			BUG();
		}
		consistent_sync(buf->safe, size, direction);
	} else {
		consistent_sync(bus_to_virt(dma_addr), size, direction);
	}
}

/* ************************************************** */

/*
 * see if a buffer address is in an 'unsafe' range.  if it is
 * allocate a 'safe' buffer and copy the unsafe buffer into it.
 * substitute the safe buffer for the unsafe one.
 * (basically move the buffer from an unsafe area to a safe one)
 */
dma_addr_t
sa1111_map_single(void *ptr, size_t size, int direction)
{
	unsigned long flags;
	dma_addr_t dma_addr;

	DPRINTK("%s(ptr=%p,size=%d,dir=%x)\n",
	       __func__, ptr, size, direction);

	BUG_ON(direction == PCI_DMA_NONE);

	local_irq_save(flags);

	dma_addr = map_single(ptr, size, direction);

	local_irq_restore(flags);

	return dma_addr;
}

/*
 * see if a mapped address was really a "safe" buffer and if so, copy
 * the data from the safe buffer back to the unsafe buffer and free up
 * the safe buffer.  (basically return things back to the way they
 * should be)
 */

void
sa1111_unmap_single(dma_addr_t dma_addr, size_t size, int direction)
{
	unsigned long flags;

	DPRINTK("%s(ptr=%p,size=%d,dir=%x)\n",
		__func__, (void *) dma_addr, size, direction);

	BUG_ON(direction == PCI_DMA_NONE);

	local_irq_save(flags);

	unmap_single(dma_addr, size, direction);

	local_irq_restore(flags);
}

int
sa1111_map_sg(struct scatterlist *sg, int nents, int direction)
{
	unsigned long flags;
	int i;

	DPRINTK("%s(sg=%p,nents=%d,dir=%x)\n",
		__func__, sg, nents, direction);

	BUG_ON(direction == PCI_DMA_NONE);

	local_irq_save(flags);

	for (i = 0; i < nents; i++, sg++) {
		struct page *page = sg->page;
		unsigned int offset = sg->offset;
		unsigned int length = sg->length;
		void *ptr = page_address(page) + offset;

		sg->dma_address =
			map_single(ptr, length, direction);
	}

	local_irq_restore(flags);

	return nents;
}

void
sa1111_unmap_sg(struct scatterlist *sg, int nents, int direction)
{
	unsigned long flags;
	int i;

	DPRINTK("%s(sg=%p,nents=%d,dir=%x)\n",
		__func__, sg, nents, direction);

	BUG_ON(direction == PCI_DMA_NONE);

	local_irq_save(flags);

	for (i = 0; i < nents; i++, sg++) {
		dma_addr_t dma_addr = sg->dma_address;
		unsigned int length = sg->length;

		unmap_single(dma_addr, length, direction);
	}

	local_irq_restore(flags);
}

void
sa1111_dma_sync_single(dma_addr_t dma_addr, size_t size, int direction)
{
	unsigned long flags;

	DPRINTK("%s(ptr=%p,size=%d,dir=%x)\n",
		__func__, (void *) dma_addr, size, direction);

	local_irq_save(flags);

	sync_single(dma_addr, size, direction);

	local_irq_restore(flags);
}

void
sa1111_dma_sync_sg(struct scatterlist *sg, int nents, int direction)
{
	unsigned long flags;
	int i;

	DPRINTK("%s(sg=%p,nents=%d,dir=%x)\n",
		__func__, sg, nents, direction);

	BUG_ON(direction == PCI_DMA_NONE);

	local_irq_save(flags);

	for (i = 0; i < nents; i++, sg++) {
		dma_addr_t dma_addr = sg->dma_address;
		unsigned int length = sg->length;

		sync_single(dma_addr, length, direction);
	}

	local_irq_restore(flags);
}

EXPORT_SYMBOL(sa1111_map_single);
EXPORT_SYMBOL(sa1111_unmap_single);
EXPORT_SYMBOL(sa1111_map_sg);
EXPORT_SYMBOL(sa1111_unmap_sg);
EXPORT_SYMBOL(sa1111_dma_sync_single);
EXPORT_SYMBOL(sa1111_dma_sync_sg);

/* **************************************** */

static int __init sa1111_pcibuf_init(void)
{
	int ret;

	printk(KERN_DEBUG
	       "sa1111_pcibuf: initializing SA-1111 DMA workaround\n");

	ret = create_safe_buffer_pools();

	return ret;
}
module_init(sa1111_pcibuf_init);

static void __exit sa1111_pcibuf_exit(void)
{
	BUG_ON(!list_empty(&safe_buffers));

#ifdef STATS
	print_alloc_stats();
	print_map_stats();
#endif

	destroy_safe_buffer_pools();
}
module_exit(sa1111_pcibuf_exit);

MODULE_AUTHOR("Christopher Hoover <ch@hpl.hp.com>");
MODULE_DESCRIPTION("Special pci_{map/unmap/dma_sync}_* routines for SA-1111.");
MODULE_LICENSE("GPL");
