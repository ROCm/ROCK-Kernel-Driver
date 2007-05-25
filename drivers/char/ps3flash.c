/*
 * PS3 FLASH ROM Storage Driver
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

#undef DEBUG

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>

#include <asm/uaccess.h>
#include <asm/lv1call.h>
#include <asm/ps3stor.h>


#define DEVICE_NAME		"ps3flash"


#define FLASH_BLOCK_SIZE	(256*1024)


struct ps3flash_private {
	struct mutex mutex;
};
#define ps3flash_priv(dev)	((dev)->sbd.core.driver_data)

static struct ps3_storage_device *ps3flash_dev;

enum {
	PS3FLASH_READ	= 0,
	PS3FLASH_WRITE	= 1,
};

static ssize_t ps3flash_read_sectors(struct ps3_storage_device *dev,
				     u64 start_sector, u64 sectors,
				     unsigned int sector_offset)
{
	u64 max_sectors, lpar, res;

	max_sectors = dev->bounce_size / dev->blk_size;
	if (sectors > max_sectors) {
		dev_dbg(&dev->sbd.core, "%s:%u Limiting sectors to %lu\n",
			__func__, __LINE__, max_sectors);
		sectors = max_sectors;
	}

	lpar = dev->bounce_lpar + sector_offset * dev->blk_size;
	res = ps3stor_read_write_sectors(dev, lpar, start_sector, sectors, 0);
	return res ? -EIO : sectors;
}

static ssize_t ps3flash_write_chunk(struct ps3_storage_device *dev,
				    u64 start_sector)
{
	u64 sectors, res;

	sectors = dev->bounce_size / dev->blk_size;
	res = ps3stor_read_write_sectors(dev, dev->bounce_lpar, start_sector,
					 sectors, 1);
	return res ? -EIO : sectors;
}


static loff_t ps3flash_llseek(struct file *file, loff_t offset, int origin)
{
	struct ps3_storage_device *dev = ps3flash_dev;
	u64 size = dev->regions[dev->region_idx].size*dev->blk_size;

	switch (origin) {
	case 1:
		offset += file->f_pos;
		break;
	case 2:
		offset += size;
		break;
	}
	if (offset < 0)
		return -EINVAL;

	file->f_pos = offset;
	return file->f_pos;
}

static ssize_t ps3flash_read(struct file *file, char __user *buf, size_t count,
			     loff_t *pos)
{
	struct ps3_storage_device *dev = ps3flash_dev;
	struct ps3flash_private *priv = ps3flash_priv(dev);
	u64 size, start_sector, end_sector, offset;
	ssize_t sectors_read;
	size_t remaining, n;

	dev_dbg(&dev->sbd.core,
		"%s:%u: Reading %zu bytes at position %lld to user 0x%p\n",
		__func__, __LINE__, count, *pos, buf);

	size = dev->regions[dev->region_idx].size*dev->blk_size;
	if (*pos >= size || !count)
		return 0;

	if (*pos+count > size) {
		dev_dbg(&dev->sbd.core,
			"%s:%u Truncating count from %zu to %llu\n", __func__,
			__LINE__, count, size-*pos);
		count = size-*pos;
	}

	start_sector = do_div_llr(*pos, dev->blk_size, &offset);
	end_sector = DIV_ROUND_UP(*pos+count, dev->blk_size);

	remaining = count;
	do {
		mutex_lock(&priv->mutex);

		sectors_read = ps3flash_read_sectors(dev, start_sector,
						     end_sector-start_sector,
						     0);
		if (sectors_read < 0) {
			mutex_unlock(&priv->mutex);
			return sectors_read;
		}

		n = min(remaining, sectors_read*dev->blk_size-offset);
		dev_dbg(&dev->sbd.core,
			"%s:%u: copy %lu bytes from 0x%p to user 0x%p\n",
			__func__, __LINE__, n, dev->bounce_buf+offset, buf);
		if (copy_to_user(buf, dev->bounce_buf+offset, n)) {
			mutex_unlock(&priv->mutex);
			return -EFAULT;
		}

		mutex_unlock(&priv->mutex);

		*pos += n;
		buf += n;
		remaining -= n;
		start_sector += sectors_read;
		offset = 0;
	} while (remaining > 0);

	return count;
}

static ssize_t ps3flash_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *pos)
{
	struct ps3_storage_device *dev = ps3flash_dev;
	struct ps3flash_private *priv = ps3flash_priv(dev);
	u64 size, chunk_sectors, start_write_sector, end_write_sector,
	    end_read_sector, start_read_sector, head, tail, offset;
	ssize_t res;
	size_t remaining, n;

	dev_dbg(&dev->sbd.core,
		"%s:%u: Writing %zu bytes at position %lld from user 0x%p\n",
		__func__, __LINE__, count, *pos, buf);

	size = dev->regions[dev->region_idx].size*dev->blk_size;
	if (*pos >= size || !count)
		return 0;

	if (*pos+count > size) {
		dev_dbg(&dev->sbd.core,
			"%s:%u Truncating count from %zu to %llu\n", __func__,
			__LINE__, count, size-*pos);
		count = size-*pos;
	}

	chunk_sectors = dev->bounce_size / dev->blk_size;

	start_write_sector = do_div_llr(*pos, dev->bounce_size, &offset) *
			     chunk_sectors;
	end_write_sector = DIV_ROUND_UP(*pos+count, dev->bounce_size) *
			   chunk_sectors;

	end_read_sector = DIV_ROUND_UP(*pos, dev->blk_size);
	start_read_sector = (*pos+count) / dev->blk_size;

	/*
	 * As we have to write in 256 KiB chunks, while we can read in blk_size
	 * (usually 512 bytes) chunks, we perform the following steps:
	 *   1. Read from start_write_sector to end_read_sector ("head")
	 *   2. Read from start_read_sector to end_write_sector ("tail")
	 *   3. Copy data to buffer
	 *   4. Write from start_write_sector to end_write_sector
	 * All of this is complicated by using only one 256 KiB bounce buffer.
	 */

	head = end_read_sector-start_write_sector;
	tail = end_write_sector-start_read_sector;

	remaining = count;
	do {
		mutex_lock(&priv->mutex);

		if (end_read_sector >= start_read_sector) {
			/* Merge head and tail */
			dev_dbg(&dev->sbd.core,
				"Merged head and tail: %lu sectors at %lu\n",
				chunk_sectors, start_write_sector);
			res = ps3flash_read_sectors(dev, start_write_sector,
						    chunk_sectors, 0);
			if (res < 0)
				goto fail;


		} else {
			if (head) {
				/* Read head */
			dev_dbg(&dev->sbd.core, "head: %lu sectors at %lu\n",
				head, start_write_sector);
				res = ps3flash_read_sectors(dev,
							    start_write_sector,
							    head, 0);
				if (res < 0)
					goto fail;
			}
			if (start_read_sector <
			    start_write_sector+chunk_sectors) {
				/* Read tail */
				dev_dbg(&dev->sbd.core,
					"tail: %lu sectors at %lu\n", tail,
					start_read_sector-start_write_sector);
				res = ps3flash_read_sectors(dev,
							    start_read_sector,
							    tail,
							    start_read_sector-start_write_sector);
				if (res < 0)
					goto fail;
			}
		}

		if (start_read_sector < start_write_sector+chunk_sectors) {



		}

		n = min(remaining, dev->bounce_size-offset);
		dev_dbg(&dev->sbd.core,
			"%s:%u: copy %lu bytes from user 0x%p to 0x%p\n",
			__func__, __LINE__, n, buf, dev->bounce_buf+offset);
		if (copy_from_user(dev->bounce_buf+offset, buf, n)) {
			res = -EFAULT;
			goto fail;
		}

		res = ps3flash_write_chunk(dev, start_write_sector);
		if (res < 0)
			goto fail;

		mutex_unlock(&priv->mutex);

		*pos += n;
		buf += n;
		remaining -= n;
		start_write_sector += chunk_sectors;
		head = 0;
		offset = 0;
	} while (remaining > 0);

	return count;

fail:
	mutex_unlock(&priv->mutex);
	return res;
}


static const struct file_operations ps3flash_fops = {
	.owner	= THIS_MODULE,
	.llseek	= ps3flash_llseek,
	.read	= ps3flash_read,
	.write	= ps3flash_write,
};

static struct miscdevice ps3flash_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= DEVICE_NAME,
	.fops	= &ps3flash_fops,
};

static int __devinit ps3flash_probe(struct ps3_system_bus_device *_dev)
{
	struct ps3_storage_device *dev = to_ps3_storage_device(&_dev->core);
	struct ps3flash_private *priv;
	int res, error;
	unsigned long tmp;

	error = ps3stor_probe_access(dev);
	if (error) {
		dev_err(&dev->sbd.core, "%s:%u: No accessible regions found\n",
			__func__, __LINE__);
		return error;
	}

	if (!ps3flash_bounce_buffer.address)
		return -ENOMEM;

	if (ps3flash_dev) {
		dev_dbg(&dev->sbd.core,
			"Only one FLASH device is supported\n");
		return -EBUSY;
	}

	ps3flash_dev = dev;

	tmp = dev->regions[dev->region_idx].start*dev->blk_size;
	if (tmp % FLASH_BLOCK_SIZE) {
		dev_err(&dev->sbd.core,
			"%s:%u region start %lu is not aligned\n", __func__,
			__LINE__, tmp);
		error = -EINVAL;
		goto fail;
	}
	tmp = dev->regions[dev->region_idx].size*dev->blk_size;
	if (tmp % FLASH_BLOCK_SIZE) {
		dev_err(&dev->sbd.core,
			"%s:%u region size %lu is not aligned\n", __func__,
			__LINE__, tmp);
		error = -EINVAL;
		goto fail;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		error = -ENOMEM;
		goto fail;
	}

	ps3flash_priv(dev) = priv;
	mutex_init(&priv->mutex);

	error = ps3_open_hv_device(&dev->sbd);
	if (error) {
		dev_err(&dev->sbd.core,
			"%s:%u: ps3_open_hv_device failed %d\n", __func__,
			__LINE__, error);
		goto fail_free_priv;
	}

	error = ps3_sb_event_receive_port_setup(PS3_BINDING_CPU_ANY,
						&dev->sbd.did,
						dev->sbd.interrupt_id,
						&dev->irq);
	if (error) {
		dev_err(&dev->sbd.core,
			"%s:%u: ps3_sb_event_receive_port_setup failed %d\n",
		       __func__, __LINE__, error);
		goto fail_close_device;
	}

	error = request_irq(dev->irq, ps3stor_interrupt, IRQF_DISABLED,
			    DEVICE_NAME, dev);
	if (error) {
		dev_err(&dev->sbd.core, "%s:%u: request_irq failed %d\n",
			__func__, __LINE__, error);
		goto fail_sb_event_receive_port_destroy;
	}

	/* use static buffer, kmalloc cannot allocate 256 KiB */
	dev->bounce_size = ps3flash_bounce_buffer.size;
	dev->bounce_buf = ps3flash_bounce_buffer.address;
	dev->bounce_lpar = ps3_mm_phys_to_lpar(__pa(dev->bounce_buf));

	dev->sbd.d_region = &dev->dma_region;
	ps3_dma_region_init(&dev->dma_region, &dev->sbd.did, PS3_DMA_64K,
			    PS3_DMA_OTHER, dev->bounce_buf, dev->bounce_size,
			    PS3_IOBUS_SB);
	res = ps3_dma_region_create(&dev->dma_region);
	if (res) {
		dev_err(&dev->sbd.core, "%s:%u: cannot create DMA region\n",
			__func__, __LINE__);
		error = -ENOMEM;
		goto fail_free_irq;
	}

	dev->bounce_dma = dma_map_single(&dev->sbd.core, dev->bounce_buf,
					 dev->bounce_size, DMA_BIDIRECTIONAL);
	if (!dev->bounce_dma) {
		dev_err(&dev->sbd.core, "%s:%u: map DMA region failed\n",
			__func__, __LINE__);
		error = -ENODEV;
		goto fail_free_dma;
	}

	error = misc_register(&ps3flash_misc);
	if (error) {
		dev_err(&dev->sbd.core, "%s:%u: misc_register failed %d\n",
			__func__, __LINE__, error);
		goto fail_unmap_dma;
	}

	dev_info(&dev->sbd.core, "%s:%u: registered misc device %d\n",
		 __func__, __LINE__, ps3flash_misc.minor);
	return 0;

fail_unmap_dma:
	dma_unmap_single(&dev->sbd.core, dev->bounce_dma, dev->bounce_size,
			 DMA_BIDIRECTIONAL);
fail_free_dma:
	ps3_dma_region_free(&dev->dma_region);
fail_free_irq:
	free_irq(dev->irq, dev);
fail_sb_event_receive_port_destroy:
	ps3_sb_event_receive_port_destroy(&dev->sbd.did, dev->sbd.interrupt_id,
					  dev->irq);
fail_close_device:
	ps3_close_hv_device(&dev->sbd);
fail_free_priv:
	kfree(priv);
fail:
	ps3flash_dev = NULL;
	return error;
}

static int ps3flash_remove(struct ps3_system_bus_device *_dev)
{
	struct ps3_storage_device *dev = to_ps3_storage_device(&_dev->core);
	int error;

	misc_deregister(&ps3flash_misc);

	dma_unmap_single(&dev->sbd.core, dev->bounce_dma, dev->bounce_size,
			 DMA_BIDIRECTIONAL);
	ps3_dma_region_free(&dev->dma_region);

	free_irq(dev->irq, dev);

	error = ps3_sb_event_receive_port_destroy(&dev->sbd.did,
						  dev->sbd.interrupt_id,
						  dev->irq);
	if (error)
		dev_err(&dev->sbd.core,
			"%s:%u: destroy event receive port failed %d\n",
			__func__, __LINE__, error);

	error = ps3_close_hv_device(&dev->sbd);
	if (error)
		dev_err(&dev->sbd.core,
			"%s:%u: ps3_close_hv_device failed %d\n", __func__,
			__LINE__, error);

	kfree(ps3flash_priv(dev));
	ps3flash_dev = NULL;

	return 0;
}


static struct ps3_system_bus_driver ps3flash = {
	.match_id	= PS3_MATCH_ID_STOR_FLASH,
	.core.name	= DEVICE_NAME,
	.probe		= ps3flash_probe,
	.remove		= ps3flash_remove,
	.shutdown	= ps3flash_remove,
};


static int __init ps3flash_init(void)
{
	return ps3_system_bus_driver_register(&ps3flash, PS3_IOBUS_SB);
}

static void __exit ps3flash_exit(void)
{
	return ps3_system_bus_driver_unregister(&ps3flash);
}

module_init(ps3flash_init);
module_exit(ps3flash_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PS3 FLASH ROM Storage Driver");
MODULE_AUTHOR("Sony Corporation");
