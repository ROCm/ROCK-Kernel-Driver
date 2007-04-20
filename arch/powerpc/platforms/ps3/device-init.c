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

#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/firmware.h>

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

	dev->match_id = PS3_MATCH_ID_GELIC;

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
		PS3_DMA_OTHER);

	result = ps3_system_bus_device_register(dev);

	if (result) {
		pr_debug("%s:%d ps3_system_bus_device_register failed\n",
			__func__, __LINE__);
		goto fail;
	}

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;

fail:
#if defined(DEBUG)
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

	p->dev.d_region = &p->d_region;
	p->dev.m_region = &p->m_region;
	p->dev.match_id = PS3_MATCH_ID_OHCI;

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
		PS3_DMA_INTERNAL);

	ps3_mmio_region_init(p->dev.m_region, &p->dev.did, bus_addr,
		len, PS3_MMIO_4K);

	result = ps3_system_bus_device_register(&p->dev);

	if (result)
		pr_debug("%s:%d ps3_system_bus_device_register failed\n",
			__func__, __LINE__);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;

fail:
#if defined(DEBUG)
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

	p->dev.d_region = &p->d_region;
	p->dev.m_region = &p->m_region;
	p->dev.match_id = PS3_MATCH_ID_OHCI;

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
		PS3_DMA_INTERNAL);

	ps3_mmio_region_init(p->dev.m_region, &p->dev.did, bus_addr,
		len, PS3_MMIO_4K);

	result = ps3_system_bus_device_register(&p->dev);

	if (result)
		pr_debug("%s:%d ps3_system_bus_device_register failed\n",
			__func__, __LINE__);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;

fail:
#if defined(DEBUG)
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

	p->dev.d_region = &p->d_region;
	p->dev.m_region = &p->m_region;
	p->dev.match_id = PS3_MATCH_ID_EHCI;

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
		PS3_DMA_INTERNAL);

	ps3_mmio_region_init(p->dev.m_region, &p->dev.did, bus_addr,
		len, PS3_MMIO_4K);

	result = ps3_system_bus_device_register(&p->dev);

	if (result)
		pr_debug("%s:%d ps3_system_bus_device_register failed\n",
			__func__, __LINE__);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;

fail:
#if defined(DEBUG)
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

	p->dev.d_region = &p->d_region;
	p->dev.m_region = &p->m_region;
	p->dev.match_id = PS3_MATCH_ID_EHCI;

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
		PS3_DMA_INTERNAL);

	ps3_mmio_region_init(p->dev.m_region, &p->dev.did, bus_addr,
		len, PS3_MMIO_4K);

	result = ps3_system_bus_device_register(&p->dev);

	if (result)
		pr_debug("%s:%d ps3_system_bus_device_register failed\n",
			__func__, __LINE__);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;

fail:
#if defined(DEBUG)
	memset(p, 0xad, sizeof(struct ehci_layout));
#endif
	kfree(p);
	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

static int __devinit
ps3_register_sys_manager (void)
{
	int result;
	static struct ps3_vuart_port_device dev = {
		.match_id = PS3_MATCH_ID_SYSTEM_MANAGER,
	};

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	result = ps3_vuart_port_device_register(&dev);

	if (result)
		pr_debug("%s:%d ps3_vuart_port_device_register failed\n",
			__func__, __LINE__);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

int __init
ps3_register_known_devices (void)
{
	int result;

	if (!firmware_has_feature(FW_FEATURE_PS3_LV1))
		return -ENODEV;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	//ps3_repository_dump_bus_info();

	result = ps3_register_ohci_0();
	result = ps3_register_ehci_0();
	result = ps3_register_ohci_1();
	result = ps3_register_ehci_1();
#if defined(CONFIG_PS3_SYS_MANAGER)
	result = ps3_register_sys_manager();
#endif
	result = ps3_register_gelic();

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

device_initcall(ps3_register_known_devices);
