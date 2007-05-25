/*
 *  PS3 device init routines.
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define DEBUG 1

#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/init.h>

#include <asm/firmware.h>
#include <asm/lv1call.h>
#include <asm/ps3stor.h>

#include "platform.h"

static int __devinit
ps3_register_gelic (void)
{
	int result;
	struct ps3_system_bus_device *dev;
	struct ps3_repository_device repo;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	/* Puts the regions at the end of the system_bus_device. */

	dev = kzalloc(sizeof(struct ps3_system_bus_device)
		+ sizeof(struct ps3_dma_region), GFP_KERNEL);

	ps3_system_bus_device_init(dev,
				   PS3_MATCH_ID_GELIC,
				   NULL,
				   NULL);

	result = ps3_repository_find_first_device(PS3_BUS_TYPE_SB,
		PS3_DEV_TYPE_SB_GELIC, &repo);

	if (result) {
		pr_debug("%s:%d ps3_repository_find_first_device failed\n",
			__func__, __LINE__);
		goto fail;
	}

	dev->did = repo.did;

	result = ps3_repository_find_interrupt(&repo,
		PS3_INTERRUPT_TYPE_EVENT_PORT, &dev->interrupt_id);

	if (result) {
		pr_debug("%s:%d ps3_repository_find_interrupt failed\n",
			__func__, __LINE__);
		goto fail;
	}

	BUG_ON(dev->interrupt_id != 0);

	if (result) {
		pr_debug("%s:%d ps3_repository_get_interrupt_id failed\n",
			__func__, __LINE__);
		goto fail;
	}

	dev->d_region = (struct ps3_dma_region *)((char*)dev
		+ sizeof(struct ps3_system_bus_device));

	ps3_dma_region_init(dev->d_region, &dev->did, PS3_DMA_64K,
			    PS3_DMA_OTHER, NULL, 0, PS3_IOBUS_SB);

	result = ps3_system_bus_device_register(dev, PS3_IOBUS_SB);

	if (result) {
		pr_debug("%s:%d ps3_system_bus_device_register failed\n",
			__func__, __LINE__);
		goto fail;
	}

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;

fail:
#ifdef DEBUG
	memset(dev, 0xad, sizeof(struct ps3_system_bus_device)
		+ sizeof(struct ps3_dma_region));
#endif
	kfree(dev);
	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

static int __devinit
ps3_register_ohci_0 (void)
{
	int result;
	struct ps3_repository_device repo;
	u64 bus_addr;
	u64 len;

	/* Puts the regions at the end of the system_bus_device. */

	struct ohci_layout {
		struct ps3_system_bus_device dev;
		struct ps3_dma_region d_region;
		struct ps3_mmio_region m_region;
	} *p;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	p = kzalloc(sizeof(struct ohci_layout), GFP_KERNEL);

	ps3_system_bus_device_init(&p->dev,
				   PS3_MATCH_ID_OHCI,
				   &p->d_region,
				   &p->m_region);

	result = ps3_repository_find_first_device(PS3_BUS_TYPE_SB,
		PS3_DEV_TYPE_SB_USB, &repo);

	if (result) {
		pr_debug("%s:%d ps3_repository_find_device failed\n",
			__func__, __LINE__);
		goto fail;
	}

	p->dev.did = repo.did;

	result = ps3_repository_find_interrupt(&repo,
		PS3_INTERRUPT_TYPE_SB_OHCI, &p->dev.interrupt_id);

	if (result) {
		pr_debug("%s:%d ps3_repository_find_interrupt failed\n",
			__func__, __LINE__);
		goto fail;
	}

	ps3_repository_find_reg(&repo, PS3_REG_TYPE_SB_OHCI,
		&bus_addr, &len);

	BUG_ON(p->dev.interrupt_id != 16);
	BUG_ON(bus_addr != 0x3010000);
	BUG_ON(len != 0x10000);

	ps3_dma_region_init(p->dev.d_region, &p->dev.did, PS3_DMA_64K,
			    PS3_DMA_INTERNAL, NULL, 0, PS3_IOBUS_SB);

	ps3_mmio_region_init(p->dev.m_region, &p->dev.did, bus_addr,
			     len, PS3_MMIO_4K, PS3_IOBUS_SB);

	result = ps3_system_bus_device_register(&p->dev, PS3_IOBUS_SB);

	if (result)
		pr_debug("%s:%d ps3_system_bus_device_register failed\n",
			__func__, __LINE__);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;

fail:
#ifdef DEBUG
	memset(p, 0xad, sizeof(struct ohci_layout));
#endif
	kfree(p);
	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

static int __devinit
ps3_register_ohci_1 (void)
{
	int result;
	struct ps3_repository_device repo;
	u64 bus_addr;
	u64 len;

	/* Puts the regions at the end of the system_bus_device. */

	struct ohci_layout {
		struct ps3_system_bus_device dev;
		struct ps3_dma_region d_region;
		struct ps3_mmio_region m_region;
	} *p;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	p = kzalloc(sizeof(struct ohci_layout), GFP_KERNEL);

	ps3_system_bus_device_init(&p->dev,
				   PS3_MATCH_ID_OHCI,
				   &p->d_region,
				   &p->m_region);

	result = ps3_repository_find_first_device(PS3_BUS_TYPE_SB,
		PS3_DEV_TYPE_SB_USB, &repo);

	if (result) {
		pr_debug("%s:%d ps3_repository_find_device failed\n",
			__func__, __LINE__);
		goto fail;
	}

	result = ps3_repository_find_device(PS3_BUS_TYPE_SB,
		PS3_DEV_TYPE_SB_USB, &repo, &repo);

	if (result) {
		pr_debug("%s:%d ps3_repository_find_device failed\n",
			__func__, __LINE__);
		goto fail;
	}

	p->dev.did = repo.did;

	result = ps3_repository_find_interrupt(&repo,
		PS3_INTERRUPT_TYPE_SB_OHCI, &p->dev.interrupt_id);

	if (result) {
		pr_debug("%s:%d ps3_repository_find_interrupt failed\n",
			__func__, __LINE__);
		goto fail;
	}

	ps3_repository_find_reg(&repo, PS3_REG_TYPE_SB_OHCI,
		&bus_addr, &len);

	BUG_ON(p->dev.interrupt_id != 17);
	BUG_ON(bus_addr != 0x3020000);
	BUG_ON(len != 0x10000);

	ps3_dma_region_init(p->dev.d_region, &p->dev.did, PS3_DMA_64K,
			    PS3_DMA_INTERNAL, NULL, 0, PS3_IOBUS_SB);

	ps3_mmio_region_init(p->dev.m_region, &p->dev.did, bus_addr,
			     len, PS3_MMIO_4K, PS3_IOBUS_SB);

	result = ps3_system_bus_device_register(&p->dev, PS3_IOBUS_SB);

	if (result)
		pr_debug("%s:%d ps3_system_bus_device_register failed\n",
			__func__, __LINE__);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;

fail:
#ifdef DEBUG
	memset(p, 0xad, sizeof(struct ohci_layout));
#endif
	kfree(p);
	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

static int __devinit
ps3_register_ehci_0 (void)
{
	int result;
	struct ps3_repository_device repo;
	u64 bus_addr;
	u64 len;

	/* Puts the regions at the end of the system_bus_device. */

	struct ehci_layout {
		struct ps3_system_bus_device dev;
		struct ps3_dma_region d_region;
		struct ps3_mmio_region m_region;
	} *p;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	p = kzalloc(sizeof(struct ehci_layout), GFP_KERNEL);

	ps3_system_bus_device_init(&p->dev,
				   PS3_MATCH_ID_EHCI,
				   &p->d_region,
				   &p->m_region);

	result = ps3_repository_find_first_device(PS3_BUS_TYPE_SB,
		PS3_DEV_TYPE_SB_USB, &repo);

	if (result) {
		pr_debug("%s:%d ps3_repository_find_device failed\n",
			__func__, __LINE__);
		goto fail;
	}

	p->dev.did = repo.did;

	result = ps3_repository_find_interrupt(&repo,
		PS3_INTERRUPT_TYPE_SB_EHCI, &p->dev.interrupt_id);

	if (result) {
		pr_debug("%s:%d ps3_repository_find_interrupt failed\n",
			__func__, __LINE__);
		goto fail;
	}

	ps3_repository_find_reg(&repo, PS3_REG_TYPE_SB_EHCI,
		&bus_addr, &len);

	BUG_ON(p->dev.interrupt_id != 10);
	BUG_ON(bus_addr != 0x3810000);
	BUG_ON(len != 0x10000);

	ps3_dma_region_init(p->dev.d_region, &p->dev.did, PS3_DMA_64K,
			    PS3_DMA_INTERNAL, NULL, 0, PS3_IOBUS_SB);

	ps3_mmio_region_init(p->dev.m_region, &p->dev.did, bus_addr,
			     len, PS3_MMIO_4K, PS3_IOBUS_SB);

	result = ps3_system_bus_device_register(&p->dev, PS3_IOBUS_SB);

	if (result)
		pr_debug("%s:%d ps3_system_bus_device_register failed\n",
			__func__, __LINE__);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;

fail:
#ifdef DEBUG
	memset(p, 0xad, sizeof(struct ehci_layout));
#endif
	kfree(p);
	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

static int __devinit
ps3_register_ehci_1 (void)
{
	int result;
	struct ps3_repository_device repo;
	u64 bus_addr;
	u64 len;

	/* Puts the regions at the end of the system_bus_device. */

	struct ehci_layout {
		struct ps3_system_bus_device dev;
		struct ps3_dma_region d_region;
		struct ps3_mmio_region m_region;
	} *p;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	p = kzalloc(sizeof(struct ehci_layout), GFP_KERNEL);

	ps3_system_bus_device_init(&p->dev,
				   PS3_MATCH_ID_EHCI,
				   &p->d_region,
				   &p->m_region);

	result = ps3_repository_find_first_device(PS3_BUS_TYPE_SB,
		PS3_DEV_TYPE_SB_USB, &repo);

	if (result) {
		pr_debug("%s:%d ps3_repository_find_device failed\n",
			__func__, __LINE__);
		goto fail;
	}

	result = ps3_repository_find_device(PS3_BUS_TYPE_SB,
		PS3_DEV_TYPE_SB_USB, &repo, &repo);

	if (result) {
		pr_debug("%s:%d ps3_repository_find_device failed\n",
			__func__, __LINE__);
		goto fail;
	}

	p->dev.did = repo.did;

	result = ps3_repository_find_interrupt(&repo,
		PS3_INTERRUPT_TYPE_SB_EHCI, &p->dev.interrupt_id);

	if (result) {
		pr_debug("%s:%d ps3_repository_find_interrupt failed\n",
			__func__, __LINE__);
		goto fail;
	}

	ps3_repository_find_reg(&repo, PS3_REG_TYPE_SB_EHCI,
		&bus_addr, &len);

	BUG_ON(p->dev.interrupt_id != 11);
	BUG_ON(bus_addr != 0x3820000);
	BUG_ON(len != 0x10000);

	ps3_dma_region_init(p->dev.d_region, &p->dev.did, PS3_DMA_64K,
			    PS3_DMA_INTERNAL, NULL, 0, PS3_IOBUS_SB);

	ps3_mmio_region_init(p->dev.m_region, &p->dev.did, bus_addr,
			     len, PS3_MMIO_4K, PS3_IOBUS_SB);

	result = ps3_system_bus_device_register(&p->dev, PS3_IOBUS_SB);

	if (result)
		pr_debug("%s:%d ps3_system_bus_device_register failed\n",
			__func__, __LINE__);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;

fail:
#ifdef DEBUG
	memset(p, 0xad, sizeof(struct ehci_layout));
#endif
	kfree(p);
	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

static int __devinit ps3_register_sound(void)
{
	int result;

	struct snd_ps3_layout {
		struct ps3_system_bus_device dev;
		struct ps3_dma_region d_region;
		struct ps3_mmio_region m_region;
	} *p;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	ps3_system_bus_device_init(&p->dev, PS3_MATCH_ID_SOUND,
				   &p->d_region,
				   &p->m_region);

#warning need to get the device specific data here

	result = ps3_system_bus_device_register(&p->dev, PS3_IOBUS_IOC0);

	if (result)
		kfree(p);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

static int __devinit ps3_register_sys_manager(void)
{
	int result;
	struct ps3_vuart_port_device *p;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->match_id = PS3_MATCH_ID_SYSTEM_MANAGER;

#if defined(CONFIG_PS3_SYS_MANAGER) || defined(CONFIG_PS3_SYS_MANAGER_MODULE)
	result = ps3_vuart_port_device_register(p);

	if (result)
		pr_debug("%s:%d ps3_vuart_port_device_register failed\n",
			__func__, __LINE__);
#endif

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

#ifdef DEBUG
static const char *ps3stor_dev_type(enum ps3_dev_type dev_type)
{
	switch (dev_type) {
	case PS3_DEV_TYPE_STOR_DISK:
		return "disk";

	case PS3_DEV_TYPE_STOR_ROM:
		return "rom";

	case PS3_DEV_TYPE_STOR_FLASH:
		return "flash";

	case PS3_DEV_TYPE_NONE:
		return "not present";

	default:
		return "unknown";
	}
}
#else
static inline const char *ps3stor_dev_type(enum ps3_dev_type dev_type)
{
    return NULL;
}
#endif /* DEBUG */

#define NOTIFICATION_DEVID	((u64)(-1L))
#define NOTIFICATION_TIMEOUT	HZ

static u64 ps3stor_wait_for_completion(u64 devid, u64 tag,
				       unsigned int timeout)
{
	unsigned int retries = 0;
	u64 res = -1, status;

	for (retries = 0; retries < timeout; retries++) {
		res = lv1_storage_check_async_status(NOTIFICATION_DEVID, tag,
						     &status);
		if (!res)
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
	}
	if (res)
		pr_debug("%s:%u: check_async_status returns %ld status %lx\n",
			 __func__, __LINE__, res, status);

	return res;
}

static int ps3stor_probe_notification(struct ps3_storage_device *dev,
				      enum ps3_dev_type dev_type)
{
	int error = -ENODEV, res;
	u64 *buf;
	u64 lpar;

	pr_info("%s:%u: Requesting notification\n", __func__, __LINE__);

	buf = kzalloc(512, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	lpar = ps3_mm_phys_to_lpar(__pa(buf));

	/* 2-1) open special event device */
	res = lv1_open_device(dev->sbd.did.bus_id, NOTIFICATION_DEVID, 0);
	if (res) {
		printk(KERN_ERR "%s:%u: open notification device failed %d\n",
		       __func__, __LINE__, res);
		goto fail_free;
	}

	/* 2-2) write info to request notify */
	buf[0] = 0;
	buf[1] = (1 << 1); /* region update info only */
	res = lv1_storage_write(NOTIFICATION_DEVID, 0, 0, 1, 0, lpar,
				&dev->tag);
	if (res) {
		printk(KERN_ERR "%s:%u: notify request write failed %d\n",
		       __func__, __LINE__, res);
		goto fail_close;
	}

	/* wait for completion in one second */
	res = ps3stor_wait_for_completion(NOTIFICATION_DEVID, dev->tag,
					  NOTIFICATION_TIMEOUT);
	if (res) {
		/* write not completed */
		printk(KERN_ERR "%s:%u: write not completed %d\n", __func__,
		       __LINE__, res);
		goto fail_close;
	}

	/* 2-3) read to wait region notification for each device */
	while (1) {
		memset(buf, 0, 512);
		lv1_storage_read(NOTIFICATION_DEVID, 0, 0, 1, 0, lpar,
				 &dev->tag);
		res = ps3stor_wait_for_completion(NOTIFICATION_DEVID, dev->tag,
						  NOTIFICATION_TIMEOUT);
		if (res) {
			/* read not completed */
			printk(KERN_ERR "%s:%u: read not completed %d\n",
			       __func__, __LINE__, res);
			break;
		}

		/* 2-4) verify the notification */
		if (buf[0] != 1 || buf[1] != dev->sbd.did.bus_id) {
			/* other info notified */
			pr_debug("%s:%u: notification info %ld dev=%lx type=%lx\n",
				 __func__, __LINE__, buf[0], buf[2], buf[3]);
			break;
		}

		if (buf[2] == dev->sbd.did.dev_id && buf[3] == dev_type) {
			pr_debug("%s:%u: device ready\n", __func__, __LINE__);
			error = 0;
			break;
		}
	}

fail_close:
	lv1_close_device(dev->sbd.did.bus_id, NOTIFICATION_DEVID);

fail_free:
	kfree(buf);
	return error;
}

static int ps3stor_probe_dev(struct ps3_repository_device *repo)
{
	int error;
	u64 port, blk_size, num_blocks;
	unsigned int num_regions, i;
	struct ps3_storage_device *dev;
	enum ps3_dev_type dev_type;
	enum ps3_match_id match_id;

	pr_info("%s:%u: Probing new storage device %u\n", __func__, __LINE__,
		 repo->dev_index);

	error = ps3_repository_read_dev_id(repo->bus_index, repo->dev_index,
					   &repo->did.dev_id);
	if (error) {
		printk(KERN_ERR "%s:%u: read_dev_id failed %d\n", __func__,
		       __LINE__, error);
		return -ENODEV;
	}

	error = ps3_repository_read_dev_type(repo->bus_index, repo->dev_index,
					     &dev_type);
	if (error) {
		printk(KERN_ERR "%s:%u: read_dev_type failed %d\n", __func__,
		       __LINE__, error);
		return -ENODEV;
	}

	pr_debug("%s:%u: index %u:%u: id %u:%u dev_type %u (%s)\n", __func__,
		 __LINE__, repo->bus_index, repo->dev_index, repo->did.bus_id,
		 repo->did.dev_id, dev_type, ps3stor_dev_type(dev_type));

	switch (dev_type) {
	case PS3_DEV_TYPE_STOR_DISK:
		match_id = PS3_MATCH_ID_STOR_DISK;
		break;

	case PS3_DEV_TYPE_STOR_ROM:
		match_id = PS3_MATCH_ID_STOR_ROM;
		break;

	case PS3_DEV_TYPE_STOR_FLASH:
		match_id = PS3_MATCH_ID_STOR_FLASH;
		break;

	default:
		return 0;
	}

	error = ps3_repository_read_stor_dev_info(repo->bus_index,
						  repo->dev_index, &port,
						  &blk_size, &num_blocks,
						  &num_regions);
	if (error) {
		printk(KERN_ERR "%s:%u: _read_stor_dev_info failed %d\n",
		       __func__, __LINE__, error);
		return -ENODEV;
	}
	pr_debug("%s:%u: index %u:%u: port %lu blk_size %lu num_blocks %lu "
		 "num_regions %u\n",
		 __func__, __LINE__, repo->bus_index, repo->dev_index, port,
		 blk_size, num_blocks, num_regions);

	dev = kzalloc(sizeof(struct ps3_storage_device)+
		      num_regions*sizeof(struct ps3_storage_region),
		      GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->sbd.did = repo->did;
	ps3_system_bus_device_init(&dev->sbd, match_id, &dev->dma_region,
				   NULL);
	dev->blk_size = blk_size;
	dev->num_regions = num_regions;

	error = ps3_repository_find_interrupt(repo,
					      PS3_INTERRUPT_TYPE_EVENT_PORT,
					      &dev->sbd.interrupt_id);
	if (error) {
		printk(KERN_ERR "%s:%u: find_interrupt failed %d\n", __func__,
			__LINE__, error);
		goto cleanup;
	}

#if defined(CONFIG_PS3_STORAGE_OLD) || defined(CONFIG_PS3_STORAGE_OLD_MODULE)
	switch (match_id) {
#if defined(CONFIG_PS3_DISK) || defined(CONFIG_PS3_DISK_MODULE)
	case PS3_MATCH_ID_STOR_DISK:
		break;
#endif
#if defined(CONFIG_PS3_ROM) || defined(CONFIG_PS3_ROM_MODULE)
	case PS3_MATCH_ID_STOR_ROM:
		break;
#endif
#if defined(CONFIG_PS3_FLASH) || defined(CONFIG_PS3_FLASH_MODULE)
	case PS3_MATCH_ID_STOR_FLASH:
		break;
#endif

	default:
		/*
		 * FIXME As this driver conflicts with the old storage driver,
		 *	 we cannot do a full probe here
		 */
		printk(KERN_ERR
		       "Ignoring storage device, let the old driver handle it\n");
		goto cleanup;
	}
#endif

	/* FIXME Do we really need this? I guess for kboot only? */
	error = ps3stor_probe_notification(dev, dev_type);
	if (error) {
		printk(KERN_ERR "%s:%u: probe_notification failed %d\n",
		       __func__, __LINE__, error);
		goto cleanup;
	}

	for (i = 0; i < num_regions; i++) {
		unsigned int id;
		u64 start, size;

		error = ps3_repository_read_stor_dev_region(repo->bus_index,
							    repo->dev_index, i,
							    &id, &start,
							    &size);
		if (error) {
			printk(KERN_ERR
			       "%s:%u: read_stor_dev_region failed %d\n",
			       __func__, __LINE__, error);
			goto cleanup;
		}
		pr_debug("%s:%u: region %u: id %u start %lu size %lu\n",
			 __func__, __LINE__, i, id, start, size);

		dev->regions[i].id = id;
		dev->regions[i].start = start;
		dev->regions[i].size = size;
	}

	error = ps3_system_bus_device_register(&dev->sbd, PS3_IOBUS_SB);
	if (error) {
		printk(KERN_ERR
		       "%s:%u: ps3_system_bus_device_register failed %d\n",
		       __func__, __LINE__, error);
		goto cleanup;
	}
	return 0;

cleanup:
	kfree(dev);
	return -ENODEV;
}

static int ps3stor_thread(void *data)
{
	struct ps3_repository_device *repo = data;
	int error;
	unsigned int n, ms = 250;

	pr_debug("%s:%u: kthread started\n", __func__, __LINE__);

	do {
		try_to_freeze();

//		pr_debug("%s:%u: Checking for new storage devices...\n",
//			 __func__, __LINE__);
		error = ps3_repository_read_bus_num_dev(repo->bus_index, &n);
		if (error) {
			printk(KERN_ERR "%s:%u: read_bus_num_dev failed %d\n",
			       __func__, __LINE__, error);
			break;
		}

		if (n > repo->dev_index) {
			pr_debug("%s:%u: Found %u storage devices (%u new)\n",
				 __func__, __LINE__, n, n - repo->dev_index);

			while (repo->dev_index < n && !error) {
				error = ps3stor_probe_dev(repo);
				repo->dev_index++;
			}

			ms = 250;
		}

		msleep_interruptible(ms);
		if (ms < 60000)
			ms <<= 1;
	} while (!kthread_should_stop());

	pr_debug("%s:%u: kthread finished\n", __func__, __LINE__);

	return 0;
}

static int __devinit ps3_register_storage_devices(void)
{
	int error;
	static struct ps3_repository_device repo;
	struct task_struct *task;

	if (!firmware_has_feature(FW_FEATURE_PS3_LV1))
		return -ENODEV;

	error = ps3_repository_find_bus(PS3_BUS_TYPE_STORAGE, 0,
					&repo.bus_index);
	if (error) {
		printk(KERN_ERR "%s: Cannot find storage bus (%d)\n", __func__,
		       error);
		return -ENODEV;
	}
	pr_debug("%s:%u: Storage bus has index %u\n", __func__, __LINE__,
		 repo.bus_index);

	error = ps3_repository_read_bus_id(repo.bus_index, &repo.did.bus_id);
	if (error) {
		printk(KERN_ERR "%s: read_bus_id failed %d\n", __func__,
		       error);
		return -ENODEV;
	}

	pr_debug("%s:%u: Storage bus has id %u\n", __func__, __LINE__,
		 repo.did.bus_id);

	task = kthread_run(ps3stor_thread, &repo, "ps3stor-probe");
	if (IS_ERR(task)) {
		error = PTR_ERR(task);
		printk(KERN_ERR "%s: kthread_run failed %d\n", __func__,
		       error);
		return error;
	}

	return 0;
}

static int __devinit ps3_register_fb(void)
{
	int error;
	struct ps3_system_bus_device *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	ps3_system_bus_device_init(dev, PS3_MATCH_ID_GFX, NULL, NULL);

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	error = ps3_system_bus_device_register(dev, PS3_IOBUS_IOC0);
	if (error) {
		printk(KERN_ERR
		       "%s:%u: ps3_system_bus_device_register failed %d\n",
		       __func__, __LINE__, error);
		goto cleanup;
	}
	return 0;

cleanup:
	kfree(dev);
	return -ENODEV;
}

static int __init ps3_register_known_devices(void)
{
	int result;

	if (!firmware_has_feature(FW_FEATURE_PS3_LV1))
		return -ENODEV;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	//ps3_repository_dump_bus_info();

	result = ps3_register_fb();
	result = ps3_register_ohci_0();
	result = ps3_register_ehci_0();
	result = ps3_register_ohci_1();
	result = ps3_register_ehci_1();
	result = ps3_register_sound();

	result = ps3_register_sys_manager();
	result = ps3_register_gelic();
	result = ps3_register_storage_devices();

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

device_initcall(ps3_register_known_devices);
