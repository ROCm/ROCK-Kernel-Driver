/*****************************************************************************
Copyright(c) 2004 - 2006 Intel Corporation. All rights reserved.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2 of the License, or (at your option)
any later version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 59
Temple Place - Suite 330, Boston, MA  02111-1307, USA.

The full GNU General Public License is included in this distribution in the
file called LICENSE.
*****************************************************************************/
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/dmaengine.h>
#include "ioatdma.h"
#include "cb_io.h"
#include "cb_registers.h"
#include "cb_hw.h"

#define to_cb_chan(chan) container_of(chan, struct cb_dma_chan, common)
#define to_cb_device(dev) container_of(dev, struct cb_device, common)
#define to_cb_desc(lh) container_of(lh, struct cb_desc_sw, node)

/* internal functions */
static int __devinit cb_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void __devexit cb_remove(struct pci_dev *pdev);

static int enumerate_dma_channels(struct cb_device *device)
{
	u8 xfercap_scale;
	u32 xfercap;
	int i;
	struct cb_dma_chan *cb_chan;

	device->common.chancnt = read_reg8(device, CB_CHANCNT_OFFSET);
	xfercap_scale = read_reg8(device, CB_XFERCAP_OFFSET);
	xfercap = (xfercap_scale == 0 ? ~0UL : (1 << xfercap_scale));

	for (i = 0; i < device->common.chancnt; i++) {
		cb_chan = kmalloc(sizeof(*cb_chan), GFP_KERNEL);
		if (!cb_chan)
			return -ENOMEM;
		memset(cb_chan, 0, sizeof(*cb_chan));

		cb_chan->device = device;
		cb_chan->common.device = &device->common;
		cb_chan->common.client = NULL;
		cb_chan->reg_base = device->reg_base + (0x80 * (i + 1));
		cb_chan->xfercap = xfercap;
		spin_lock_init(&cb_chan->cleanup_lock);
		spin_lock_init(&cb_chan->desc_lock);
		INIT_LIST_HEAD(&cb_chan->free_desc);
		INIT_LIST_HEAD(&cb_chan->used_desc);
		list_add_tail(&cb_chan->common.device_node, &device->common.channels);
		device->idx[i] = cb_chan;
	}

	return 0;
}

static struct cb_desc_sw * cb_dma_alloc_descriptor(struct cb_dma_chan *cb_chan)
{
	struct cb_dma_descriptor *desc;
	struct cb_desc_sw *desc_sw;
	struct cb_device *cb_device = to_cb_device(cb_chan->common.device);
	dma_addr_t phys;

	desc = dma_pool_alloc(cb_device->dma_pool, GFP_ATOMIC, &phys);
	if (!desc)
		return NULL;

	desc_sw = kmalloc(sizeof(*desc_sw), GFP_ATOMIC);

	if (!desc_sw) {
		dma_pool_free(cb_device->dma_pool, desc, phys);
		return NULL;
	}

	memset(desc, 0, sizeof(*desc));
	memset(desc_sw, 0, sizeof(*desc_sw));
	desc_sw->hw = desc;
	desc_sw->phys = phys;

	return desc_sw;
}

#define INITIAL_CB_DESC_COUNT 32

static void cb_start_null_desc(struct cb_dma_chan *cb_chan);

/* returns the actual number of allocated descriptors */
static int cb_dma_alloc_chan_resources(struct dma_chan *chan)
{
	struct cb_dma_chan *cb_chan = to_cb_chan(chan);
	struct cb_desc_sw *desc = NULL;
	u16 chanctrl;
	u32 chanerr;
	int i;

	/*
	 * In-use bit automatically set by reading chanctrl
	 * If 0, we got it, if 1, someone else did
	 */
	chanctrl = chan_read_reg16(cb_chan, CB_CHANCTRL_OFFSET);
	if (chanctrl & CB_CHANCTRL_CHANNEL_IN_USE)
		return -EBUSY;

        /* Setup register to interrupt and write completion status on error */
	chanctrl = CB_CHANCTRL_CHANNEL_IN_USE |
		CB_CHANCTRL_ERR_INT_EN |
		CB_CHANCTRL_ANY_ERR_ABORT_EN |
		CB_CHANCTRL_ERR_COMPLETION_EN;
        chan_write_reg16(cb_chan, CB_CHANCTRL_OFFSET, chanctrl);

	chanerr = chan_read_reg32(cb_chan, CB_CHANERR_OFFSET);
	if (chanerr) {
		printk("CB: CHANERR = %x, clearing\n", chanerr);
		chan_write_reg32(cb_chan, CB_CHANERR_OFFSET, chanerr);
	}

	/* Allocate descriptors */
	spin_lock_bh(&cb_chan->desc_lock);
	for (i = 0; i < INITIAL_CB_DESC_COUNT; i++) {
		desc = cb_dma_alloc_descriptor(cb_chan);
		if (!desc) {
			printk(KERN_ERR "CB: Only %d initial descriptors\n", i);
			break;
		}
		list_add_tail(&desc->node, &cb_chan->free_desc);
	}
	spin_unlock_bh(&cb_chan->desc_lock);

	/* TODO - need to stop using virt_to_bus */
	/* allocate a completion writeback area with pci_alloc_conststent? */
	/* doing 2 32bit writes to mmio since 1 64b write doesn't work */
	chan_write_reg32(cb_chan, CB_CHANCMP_OFFSET_LOW,
		(u64) virt_to_bus(&cb_chan->completion) & 0xFFFFFFFF);
	chan_write_reg32(cb_chan, CB_CHANCMP_OFFSET_HIGH,
		(u64) virt_to_bus(&cb_chan->completion) >> 32);

	cb_start_null_desc(cb_chan);

	return i;
}

static void cb_dma_memcpy_cleanup(struct cb_dma_chan *cb_chan);

static void cb_dma_free_chan_resources(struct dma_chan *chan)
{
	struct cb_dma_chan *cb_chan = to_cb_chan(chan);
	struct cb_device *cb_device = to_cb_device(chan->device);
	struct cb_desc_sw *desc, *_desc;
	u16 chanctrl;
	int in_use_descs = 0;

	cb_dma_memcpy_cleanup(cb_chan);

	chan_write_reg8(cb_chan, CB_CHANCMD_OFFSET, CB_CHANCMD_RESET);

	spin_lock_bh(&cb_chan->desc_lock);
	list_for_each_entry_safe(desc, _desc, &cb_chan->used_desc, node) {
		in_use_descs++;
		list_del(&desc->node);
		dma_pool_free(cb_device->dma_pool, desc->hw, desc->phys);
		kfree(desc);
	}
	list_for_each_entry_safe(desc, _desc, &cb_chan->free_desc, node) {
		list_del(&desc->node);
		dma_pool_free(cb_device->dma_pool, desc->hw, desc->phys);
		kfree(desc);
	}
	spin_unlock_bh(&cb_chan->desc_lock);

	/* one is ok since we left it on there on purpose */
	if (in_use_descs > 1)
		printk(KERN_ERR "CB: Freeing %d in use descriptors!\n",
			in_use_descs - 1);

	cb_chan->last_completion = cb_chan->completion = 0;

	/* Tell hw the chan is free */
	chanctrl = chan_read_reg16(cb_chan, CB_CHANCTRL_OFFSET);
	chanctrl &= ~CB_CHANCTRL_CHANNEL_IN_USE;
	chan_write_reg16(cb_chan, CB_CHANCTRL_OFFSET, chanctrl);
}

/**
 * do_cb_dma_memcpy - actual function that initiates a CB DMA transaction
 * @chan: CB DMA channel handle
 * @dest: DMA destination address
 * @src: DMA source address
 * @len: transaction length in bytes
 */

static dma_cookie_t do_cb_dma_memcpy( struct cb_dma_chan *cb_chan, dma_addr_t dest, dma_addr_t src, size_t len)
{
	struct cb_desc_sw *first;
	struct cb_desc_sw *prev;
	struct cb_desc_sw *new;
	dma_cookie_t cookie;
	LIST_HEAD(new_chain);
	u32 copy;
	size_t orig_len;
	dma_addr_t orig_src, orig_dst;
	unsigned int desc_count = 0;
	unsigned int append = 0;

	if (!cb_chan || !dest || !src)
		return -EFAULT;

	if (!len)
		return cb_chan->common.cookie;

	orig_len = len;
	orig_src = src;
	orig_dst = dest;

	first = NULL;
	prev = NULL;

	spin_lock_bh(&cb_chan->desc_lock);

	while (len) {
		if (!list_empty(&cb_chan->free_desc)) {
			new = to_cb_desc(cb_chan->free_desc.next);
			list_del(&new->node);
		} else {
			/* try to get another desc */
			new = cb_dma_alloc_descriptor(cb_chan);
			/* will this ever happen? */
			/* TODO add upper limit on these */
			BUG_ON(!new);
		}

		copy = min((u32) len, cb_chan->xfercap);

		new->hw->size = copy;
		new->hw->ctl = 0;
		new->hw->src_addr = src;
		new->hw->dst_addr = dest;
		new->cookie = 0;

		/* chain together the physical address list for the HW */
		if (!first)
			first = new;
		else
			prev->hw->next = (u64) new->phys;

		prev = new;

		len  -= copy;
		dest += copy;
		src  += copy;

		list_add_tail(&new->node, &new_chain);
		desc_count++;
	}
	new->hw->ctl = CB_DMA_DESCRIPTOR_CTL_CP_STS;
	new->hw->next = 0;

	/* cookie incr and addition to used_list must be atomic */

	cookie = cb_chan->common.cookie;
	cookie++;
	if (cookie < 0)
		cookie = 1;
	cb_chan->common.cookie = new->cookie = cookie;

	pci_unmap_addr_set(new, src, orig_src);
	pci_unmap_addr_set(new, dst, orig_dst);
	pci_unmap_len_set(new, src_len, orig_len);
	pci_unmap_len_set(new, dst_len, orig_len);

	/* write address into NextDescriptor field of last desc in chain */
	to_cb_desc(cb_chan->used_desc.prev)->hw->next = first->phys;
	list_splice_init(&new_chain, cb_chan->used_desc.prev);

	cb_chan->pending += desc_count;
	if (cb_chan->pending >= 20) {
		append = 1;
		cb_chan->pending = 0;
	}

	spin_unlock_bh(&cb_chan->desc_lock);

	if (append)
		chan_write_reg8(cb_chan, CB_CHANCMD_OFFSET, CB_CHANCMD_APPEND);

	return cookie;
}

/**
 * cb_dma_memcpy_buf_to_buf - wrapper that takes src & dest bufs
 * @chan: CB DMA channel handle
 * @dest: DMA destination address
 * @src: DMA source address
 * @len: transaction length in bytes
 */

static dma_cookie_t cb_dma_memcpy_buf_to_buf( struct dma_chan *chan, void *dest, void *src, size_t len)
{
	dma_addr_t dest_addr;
	dma_addr_t src_addr;
	struct cb_dma_chan *cb_chan = to_cb_chan(chan);

	dest_addr = pci_map_single(cb_chan->device->pdev,
		dest, len, PCI_DMA_FROMDEVICE);
	src_addr = pci_map_single(cb_chan->device->pdev,
		src, len, PCI_DMA_TODEVICE);

	return do_cb_dma_memcpy(cb_chan, dest_addr, src_addr, len);
}

/**
 * cb_dma_memcpy_buf_to_pg - wrapper, copying from a buf to a page
 * @chan: CB DMA channel handle
 * @page: pointer to the page to copy to
 * @offset: offset into that page
 * @src: DMA source address
 * @len: transaction length in bytes
 */

static dma_cookie_t cb_dma_memcpy_buf_to_pg( struct dma_chan *chan, struct page *page, unsigned int offset, void *src, size_t len)
{
	dma_addr_t dest_addr;
	dma_addr_t src_addr;
	struct cb_dma_chan *cb_chan = to_cb_chan(chan);

	dest_addr = pci_map_page(cb_chan->device->pdev,
		page, offset, len, PCI_DMA_FROMDEVICE);
	src_addr = pci_map_single(cb_chan->device->pdev,
		src, len, PCI_DMA_TODEVICE);

	return do_cb_dma_memcpy(cb_chan, dest_addr, src_addr, len);
}

/**
 * cb_dma_memcpy_pg_to_pg - wrapper, copying between two pages
 * @chan: CB DMA channel handle
 * @dest_pg: pointer to the page to copy to
 * @dest_off: offset into that page
 * @src_pg: pointer to the page to copy from
 * @src_off: offset into that page
 * @len: transaction length in bytes. This is guaranteed to not make a copy
 *	 across a page boundary.
 */

static dma_cookie_t cb_dma_memcpy_pg_to_pg( struct dma_chan *chan, struct page *dest_pg, unsigned int dest_off, struct page *src_pg, unsigned int src_off, size_t len)
{
	dma_addr_t dest_addr;
	dma_addr_t src_addr;
	struct cb_dma_chan *cb_chan = to_cb_chan(chan);

	dest_addr = pci_map_page(cb_chan->device->pdev,
		dest_pg, dest_off, len, PCI_DMA_FROMDEVICE);
	src_addr = pci_map_page(cb_chan->device->pdev,
		src_pg, src_off, len, PCI_DMA_TODEVICE);

	return do_cb_dma_memcpy(cb_chan, dest_addr, src_addr, len);
}

/**
 * cb_dma_memcpy_issue_pending - push potentially unrecognoized appended descriptors to hw
 * @chan: DMA channel handle
 */

static void cb_dma_memcpy_issue_pending(struct dma_chan *chan)
{
	struct cb_dma_chan *cb_chan = to_cb_chan(chan);

	if (cb_chan->pending != 0) {
		cb_chan->pending = 0;
		chan_write_reg8(cb_chan, CB_CHANCMD_OFFSET, CB_CHANCMD_APPEND);
	}
}

static void cb_dma_memcpy_cleanup(struct cb_dma_chan *chan)
{
	unsigned long phys_complete;
	struct cb_desc_sw *desc, *_desc;
	dma_cookie_t cookie = 0;

	prefetch(&chan->completion);

	if (!spin_trylock(&chan->cleanup_lock))
		return;

	/* The completion writeback can happen at any time,
	   so reads by the driver need to be atomic operations
	   The descriptor physical addresses are limited to 32-bits
	   when the CPU can only do a 32-bit mov */

#if (BITS_PER_LONG == 64)
	phys_complete = chan->completion & CB_CHANSTS_COMPLETED_DESCRIPTOR_ADDR;
#else
	phys_complete = chan->completion_low & CB_LOW_COMPLETION_MASK;
#endif

	if ((chan->completion & CB_CHANSTS_DMA_TRANSFER_STATUS) ==
		CB_CHANSTS_DMA_TRANSFER_STATUS_HALTED) {
		printk("CB: Channel halted, chanerr = %x\n",
			chan_read_reg32(chan, CB_CHANERR_OFFSET));

		/* TODO do something to salvage the situation */
	}

	if (phys_complete == chan->last_completion) {
		spin_unlock(&chan->cleanup_lock);
		return;
	}

	spin_lock_bh(&chan->desc_lock);
	list_for_each_entry_safe(desc, _desc, &chan->used_desc, node) {

		/*
		 * Incoming DMA requests may use multiple descriptors, due to
		 * exceeding xfercap, perhaps. If so, only the last one will
		 * have a cookie, and require unmapping.
		 */
		if (desc->cookie) {
			cookie = desc->cookie;

			/* yes we are unmapping both _page and _single alloc'd
			   regions with unmap_page. Is this *really* that bad?
			*/
			pci_unmap_page(chan->device->pdev,
					pci_unmap_addr(desc, dst),
					pci_unmap_len(desc, dst_len),
					PCI_DMA_FROMDEVICE);
			pci_unmap_page(chan->device->pdev,
					pci_unmap_addr(desc, src),
					pci_unmap_len(desc, src_len),
					PCI_DMA_TODEVICE);
		}

		if (desc->phys != phys_complete) {
			/* a completed entry, but not the last, so cleanup */
			list_del(&desc->node);
			list_add_tail(&desc->node, &chan->free_desc);
		} else {
			/* last used desc. Do not remove, so we can append from
			   it, but don't look at it next time, either */
			desc->cookie = 0;

			/* TODO check status bits? */
			break;
		}
	}

	spin_unlock_bh(&chan->desc_lock);

	chan->last_completion = phys_complete;
	if (cookie != 0)
		chan->completed_cookie = cookie;

	spin_unlock(&chan->cleanup_lock);
}

/**
 * cb_dma_is_complete - poll the status of a CB DMA transaction
 * @chan: CB DMA channel handle
 * @cookie: DMA transaction identifier
 */

static enum dma_status cb_dma_is_complete(struct dma_chan *chan, dma_cookie_t cookie, dma_cookie_t *done, dma_cookie_t *used)
{
	struct cb_dma_chan *cb_chan = to_cb_chan(chan);
	dma_cookie_t last_used;
	dma_cookie_t last_complete;
	enum dma_status ret;

	last_used = chan->cookie;
	last_complete = cb_chan->completed_cookie;

	if (done)
		*done= last_complete;
	if (used)
		*used = last_used;

	ret = dma_async_is_complete(cookie, last_complete, last_used);
	if (ret == DMA_SUCCESS)
		return ret;

	cb_dma_memcpy_cleanup(cb_chan);

	last_used = chan->cookie;
	last_complete = cb_chan->completed_cookie;

	if (done)
		*done= last_complete;
	if (used)
		*used = last_used;

	return dma_async_is_complete(cookie, last_complete, last_used);
}

/* PCI API */

static struct pci_device_id cb_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_CB) },
	{ 0, }
};

static struct pci_driver cb_pci_drv = {
	.name 	= "ioat-dma",
	.id_table = cb_pci_tbl,
	.probe	= cb_probe,
	.remove	= __devexit_p(cb_remove),
};

static irqreturn_t cb_do_interrupt(int irq, void *data, struct pt_regs *regs)
{
	struct cb_device *instance = data;
	unsigned long attnstatus;
	u8 intrctrl;

	intrctrl = read_reg8(instance, CB_INTRCTRL_OFFSET);

	if (!(intrctrl & CB_INTRCTRL_MASTER_INT_EN)) {
		return IRQ_NONE;
	}

	if (!(intrctrl & CB_INTRCTRL_INT_STATUS)) {
		write_reg8(instance, CB_INTRCTRL_OFFSET, intrctrl);
		return IRQ_NONE;
	}

	attnstatus = (unsigned long) read_reg32(instance, CB_ATTNSTATUS_OFFSET);

	write_reg8(instance, CB_INTRCTRL_OFFSET, intrctrl);
	return IRQ_HANDLED;
}

static void cb_start_null_desc(struct cb_dma_chan *cb_chan)
{
	struct cb_desc_sw *desc;

	spin_lock_bh(&cb_chan->desc_lock);

	if (!list_empty(&cb_chan->free_desc)) {
		desc = to_cb_desc(cb_chan->free_desc.next);
		list_del(&desc->node);
	} else {
		/* try to get another desc */
		desc = cb_dma_alloc_descriptor(cb_chan);
		/* will this ever happen? */
		BUG_ON(!desc);
	}

	desc->hw->ctl = CB_DMA_DESCRIPTOR_NUL;
	desc->hw->next = 0;

	list_add_tail(&desc->node, &cb_chan->used_desc);

#if (BITS_PER_LONG == 64)
	chan_write_reg64(cb_chan, CB_CHAINADDR_OFFSET, desc->phys);
#else
	chan_write_reg32(cb_chan, CB_CHAINADDR_OFFSET_LOW, (u32) desc->phys);
	chan_write_reg32(cb_chan, CB_CHAINADDR_OFFSET_HIGH, 0);
#endif
	chan_write_reg8(cb_chan, CB_CHANCMD_OFFSET, CB_CHANCMD_START);

	spin_unlock_bh(&cb_chan->desc_lock);
}

static int __devinit cb_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err;
	unsigned long mmio_start, mmio_len;
	void *reg_base;
	struct cb_device *device;

	err = pci_enable_device(pdev);
	if (err)
		goto err_enable_device;

	err = pci_set_dma_mask(pdev, DMA_64BIT_MASK);
	if (err)
		err = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
	if (err)
		goto err_set_dma_mask;

	err = pci_request_regions(pdev, cb_pci_drv.name);
	if (err)
		goto err_request_regions;

	mmio_start = pci_resource_start(pdev, 0);
	mmio_len = pci_resource_len(pdev, 0);

	reg_base = ioremap(mmio_start, mmio_len);
	if (!reg_base) {
		err = -ENOMEM;
		goto err_ioremap;
	}

	device = kmalloc(sizeof(*device), GFP_KERNEL);
	if (!device) {
		err = -ENOMEM;
		goto err_kmalloc;
	}
	memset(device, 0, sizeof(*device));

	/* DMA coherent memory pool for DMA descriptor allocations */
	device->dma_pool = dma_pool_create("dma_desc_pool", &pdev->dev,
		sizeof(struct cb_dma_descriptor), 64, 0);
	if (!device->dma_pool) {
		err = -ENOMEM;
		goto err_dma_pool;
	}

	device->pdev = pdev;
	pci_set_drvdata(pdev, device);
#ifdef CONFIG_PCI_MSI
	if (pci_enable_msi(pdev) == 0) {
		device->msi = 1;
	} else {
		device->msi = 0;
	}
#endif
	err = request_irq(pdev->irq, &cb_do_interrupt, SA_SHIRQ, "ioat",
		device);
	if (err)
		goto err_irq;

	device->reg_base = reg_base;

	write_reg8(device, CB_INTRCTRL_OFFSET, CB_INTRCTRL_MASTER_INT_EN);
	pci_set_master(pdev);

	INIT_LIST_HEAD(&device->common.channels);
	enumerate_dma_channels(device);

	device->common.device_alloc_chan_resources = cb_dma_alloc_chan_resources;
	device->common.device_free_chan_resources = cb_dma_free_chan_resources;
	device->common.device_memcpy_buf_to_buf = cb_dma_memcpy_buf_to_buf;
	device->common.device_memcpy_buf_to_pg = cb_dma_memcpy_buf_to_pg;
	device->common.device_memcpy_pg_to_pg = cb_dma_memcpy_pg_to_pg;
	device->common.device_memcpy_complete = cb_dma_is_complete;
	device->common.device_memcpy_issue_pending = cb_dma_memcpy_issue_pending;
	printk(KERN_INFO "Intel I/OAT DMA Engine found, %d channels\n",
		device->common.chancnt);
	dma_async_device_register(&device->common);

	return 0;

err_irq:
	dma_pool_destroy(device->dma_pool);
err_dma_pool:
	kfree(device);
err_kmalloc:
	iounmap(reg_base);
err_ioremap:
	pci_release_regions(pdev);
err_request_regions:
err_set_dma_mask:
err_enable_device:
	return err;
}

static void __devexit cb_remove(struct pci_dev *pdev)
{
	struct cb_device *device;

	device = pci_get_drvdata(pdev);
	dma_async_device_unregister(&device->common);

	free_irq(device->pdev->irq, device);
#ifdef CONFIG_PCI_MSI
	if (device->msi)
		pci_disable_msi(device->pdev);
#endif
	dma_pool_destroy(device->dma_pool);
	iounmap(device->reg_base);
	pci_release_regions(pdev);
	kfree(device);
}

/* MODULE API */
MODULE_VERSION("0.42");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Intel Corporation");

static int __init cb_init_module(void)
{
	/* it's currently unsafe to unload this module */
	/* if forced, worst case is that rmmod hangs */
	THIS_MODULE->unsafe = 1;
	return pci_module_init(&cb_pci_drv);
}

module_init(cb_init_module);

static void __exit cb_exit_module(void)
{
	pci_unregister_driver(&cb_pci_drv);
}

module_exit(cb_exit_module);
