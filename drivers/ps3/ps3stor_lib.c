/*
 * PS3 Storage Library
 *
 * Copyright (C) 2007 Sony Computer Entertainment Inc.
 * Copyright 2007 Sony Corp.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define DEBUG

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>

#include <asm/lv1call.h>
#include <asm/ps3stor.h>



/**
 *	ps3stor_interrupt - common interrupt routine for storage drivers
 *	@irq: IRQ number
 *	@data: Pointer to a struct ps3_storage_device
 */
irqreturn_t ps3stor_interrupt(int irq, void *data)
{
	struct ps3_storage_device *dev = data;

	dev->lv1_res = lv1_storage_get_async_status(dev->sbd.did.dev_id,
						    &dev->lv1_tag,
						    &dev->lv1_status);
	/*
	 * lv1_status = -1 may mean that ATAPI transport completed OK, but
	 * ATAPI command itself resulted CHECK CONDITION
	 * so, upper layer should issue REQUEST_SENSE to check the sense data
	 */

	if (dev->lv1_tag != dev->tag)
		dev_err(&dev->sbd.core,
			"%s:%u: tag mismatch, got %lx, expected %lx\n",
			__func__, __LINE__, dev->lv1_tag, dev->tag);
	if (dev->lv1_res)
		dev_err(&dev->sbd.core, "%s:%u: res=%d status=0x%lx\n",
			__func__, __LINE__, dev->lv1_res, dev->lv1_status);
	else
		complete(&dev->irq_done);
	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(ps3stor_interrupt);


/**
 *	ps3stor_read_write_sectors - read/write from/to a storage device
 *	@dev: Pointer to a struct ps3_storage_device
 *	@lpar: HV logical partition address
 *	@start_sector: First sector to read/write
 *	@sectors: Number of sectors to read/write
 *	@write: Flag indicating write (non-zero) or read (zero)
 *
 *	Returns 0 for success, -1 in case of failure to submit the command, or
 *	an LV1 status value in case of other errors
 */
u64 ps3stor_read_write_sectors(struct ps3_storage_device *dev, u64 lpar,
			       u64 start_sector, u64 sectors, int write)
{
	unsigned int idx = ffs(dev->accessible_regions)-1;
	unsigned int region_id = dev->regions[idx].id;
	const char *op = write ? "write" : "read";
	int res;

	dev_dbg(&dev->sbd.core, "%s:%u: %s %lu sectors starting at %lu\n",
		__func__, __LINE__, op, sectors, start_sector);

	init_completion(&dev->irq_done);
	res = write ? lv1_storage_write(dev->sbd.did.dev_id, region_id,
					start_sector, sectors, 0, lpar,
					&dev->tag)
		    : lv1_storage_read(dev->sbd.did.dev_id, region_id,
				       start_sector, sectors, 0, lpar,
				       &dev->tag);
	if (res) {
		dev_err(&dev->sbd.core, "%s:%u: %s failed %d\n", __func__,
			__LINE__, op, res);
		return -1;
	}

	wait_for_completion(&dev->irq_done);
	if (dev->lv1_status) {
		dev_err(&dev->sbd.core, "%s:%u: %s failed 0x%lx\n", __func__,
			__LINE__, op, dev->lv1_status);
		return dev->lv1_status;
	}

	dev_dbg(&dev->sbd.core, "%s:%u: %s completed\n", __func__, __LINE__,
		op);

	return 0;
}
EXPORT_SYMBOL_GPL(ps3stor_read_write_sectors);


/**
 *	ps3stor_probe_access - Probe for accessibility of regions
 *	@dev: Pointer to a struct ps3_storage_device
 *
 *	Returns the index of the first accessible region, or an error code
 */
int ps3stor_probe_access(struct ps3_storage_device *dev)
{
	int res, error;
	unsigned int irq, i;
	unsigned long n;
	void *buf;
	dma_addr_t dma;
	u64 lpar;

	error = ps3_open_hv_device(&dev->sbd);
	if (error) {
		dev_err(&dev->sbd.core, "%s:%u: open device %u:%u failed %d\n",
			__func__, __LINE__, dev->sbd.did.bus_id,
			dev->sbd.did.dev_id, error);
		return error;
	}

	error = ps3_sb_event_receive_port_setup(PS3_BINDING_CPU_ANY,
						&dev->sbd.did,
						dev->sbd.interrupt_id, &irq);
	if (error) {
		dev_err(&dev->sbd.core,
			"%s:%u: ps3_sb_event_receive_port_setup failed %d\n",
			__func__, __LINE__, error);
		goto fail_close_device;
	}

	error = request_irq(irq, ps3stor_interrupt, IRQF_DISABLED,
			    "ps3stor-probe", dev);
	if (error) {
		dev_err(&dev->sbd.core, "%s:%u: request_irq failed %d\n",
			__func__, __LINE__, error);
		goto fail_event_receive_port_destroy;
	}

	/* PAGE_SIZE >= 4 KiB buffer for fail safe of large sector devices */
	buf = (void *)__get_free_page(GFP_KERNEL);
	if (!buf) {
		dev_err(&dev->sbd.core, "%s:%u: no memory while probing",
			__func__, dev->sbd.did.dev_id);
		error = -ENOMEM;
		goto fail_free_irq;
	};

	ps3_dma_region_init(&dev->dma_region, &dev->sbd.did, PS3_DMA_4K,
			    PS3_DMA_OTHER, buf, PAGE_SIZE, PS3_IOBUS_SB);
	res = ps3_dma_region_create(&dev->dma_region);
	if (res) {
		dev_err(&dev->sbd.core, "%s:%u: cannot create DMA region\n",
			__func__, __LINE__);
		error = -ENOMEM;
		goto fail_free_buf;
	}

	lpar = ps3_mm_phys_to_lpar(__pa(buf));

	dma = dma_map_single(&dev->sbd.core, buf, PAGE_SIZE, DMA_FROM_DEVICE);
	if (!dma) {
		dev_err(&dev->sbd.core, "%s:%u: map DMA region failed\n",
			__func__, __LINE__);
		error = -ENODEV;
		goto fail_free_dma;
	}

	error = -EPERM;
	for (i = 0; i < dev->num_regions; i++) {
		dev_dbg(&dev->sbd.core,
			"%s:%u: checking accessibility of region %u\n",
			__func__, __LINE__, i);

		init_completion(&dev->irq_done);
		res = lv1_storage_read(dev->sbd.did.dev_id, dev->regions[i].id,
				       0, /* start sector */
				       1, /* sector count */
				       0, /* flags */
				       lpar, &dev->tag);
		if (res) {
			dev_dbg(&dev->sbd.core,
				"%s:%u: read failed %d, region %u is not accessible\n",
				__func__, __LINE__, res, i);
			continue;
		}

		wait_for_completion(&dev->irq_done);

		if (dev->lv1_res || dev->lv1_status) {
			dev_dbg(&dev->sbd.core,
				"%s:%u: read failed, region %u is not accessible\n",
				__func__, __LINE__, i);
			continue;
		}

		if (dev->lv1_tag != dev->tag) {
			dev_err(&dev->sbd.core,
				"%s:%u: tag mismatch, got %lx, expected %lx\n",
				__func__, __LINE__, dev->lv1_tag, dev->tag);
			break;
		}

		dev_dbg(&dev->sbd.core, "%s:%u: region %u is accessible\n",
			__func__, __LINE__, i);
		set_bit(i, &dev->accessible_regions);

		/* We can access at least one region */
		error = 0;
	}
	n = hweight_long(dev->accessible_regions);
	if (n > 1)
		dev_info(&dev->sbd.core,
			 "%s:%u: %lu accessible regions found. Only the first "
			 "one will be used",
			 __func__, __LINE__, n);
	dev->region_idx = __ffs(dev->accessible_regions);
	dev_dbg(&dev->sbd.core,
		"First accessible region has index %u start %lu size %lu\n",
		dev->region_idx, dev->regions[dev->region_idx].start,
		dev->regions[dev->region_idx].size);

	dma_unmap_single(&dev->sbd.core, dma, PAGE_SIZE, DMA_FROM_DEVICE);
fail_free_dma:
	ps3_dma_region_free(&dev->dma_region);
fail_free_buf:
	free_page((unsigned long)buf);
fail_free_irq:
	free_irq(irq, dev);
fail_event_receive_port_destroy:
	ps3_sb_event_receive_port_destroy(&dev->sbd.did, dev->sbd.interrupt_id,
					  irq);
fail_close_device:
	ps3_close_hv_device(&dev->sbd);

	return error;
}
EXPORT_SYMBOL_GPL(ps3stor_probe_access);


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PS3 Storage Bus Library");
MODULE_AUTHOR("Sony Corporation");
