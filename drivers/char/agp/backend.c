/*
 * AGPGART driver backend routines.
 * Copyright (C) 2002-2003 Dave Jones.
 * Copyright (C) 1999 Jeff Hartmann.
 * Copyright (C) 1999 Precision Insight, Inc.
 * Copyright (C) 1999 Xi Graphics, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * JEFF HARTMANN, DAVE JONES, OR ANY OTHER CONTRIBUTORS BE LIABLE FOR ANY CLAIM, 
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR 
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE 
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * TODO: 
 * - Allocate more than order 0 pages to avoid too much linear map splitting.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/miscdevice.h>
#include <linux/pm.h>
#include <linux/agp_backend.h>
#include <linux/vmalloc.h>
#include <asm/io.h>
#include "agp.h"

/* Due to XFree86 brain-damage, we can't go to 1.0 until they
 * fix some real stupidity. It's only by chance we can bump
 * past 0.99 at all due to some boolean logic error. */
#define AGPGART_VERSION_MAJOR 0
#define AGPGART_VERSION_MINOR 100
static struct agp_version agp_current_version =
{
	.major = AGPGART_VERSION_MAJOR,
	.minor = AGPGART_VERSION_MINOR,
};

static int agp_count=0;

struct agp_bridge_data agp_bridge_dummy = { .type = NOT_SUPPORTED };
struct agp_bridge_data *agp_bridge = &agp_bridge_dummy;
EXPORT_SYMBOL(agp_bridge);


/**
 *	agp_backend_acquire  -  attempt to acquire the agp backend.
 *
 *	returns -EBUSY if agp is in use,
 *	returns 0 if the caller owns the agp backend
 */
int agp_backend_acquire(void)
{
	if (agp_bridge->type == NOT_SUPPORTED)
		return -EINVAL;
	if (atomic_read(&agp_bridge->agp_in_use))
		return -EBUSY;
	atomic_inc(&agp_bridge->agp_in_use);
	return 0;
}
EXPORT_SYMBOL(agp_backend_acquire);


/**
 *	agp_backend_release  -  release the lock on the agp backend.
 *
 *	The caller must insure that the graphics aperture translation table
 *	is read for use by another entity.
 *
 *	(Ensure that all memory it bound is unbound.)
 */
void agp_backend_release(void)
{
	if (agp_bridge->type != NOT_SUPPORTED)
		atomic_dec(&agp_bridge->agp_in_use);
}
EXPORT_SYMBOL(agp_backend_release);


struct { int mem, agp; } maxes_table[] = {
	{0, 0},
	{32, 4},
	{64, 28},
	{128, 96},
	{256, 204},
	{512, 440},
	{1024, 942},
	{2048, 1920},
	{4096, 3932}
};

static int agp_find_max(void)
{
	long memory, index, result;

	memory = (num_physpages << PAGE_SHIFT) >> 20;
	index = 1;

	while ((memory > maxes_table[index].mem) && (index < 8))
		index++;

	result = maxes_table[index - 1].agp +
	   ( (memory - maxes_table[index - 1].mem)  *
	     (maxes_table[index].agp - maxes_table[index - 1].agp)) /
	   (maxes_table[index].mem - maxes_table[index - 1].mem);

	printk(KERN_INFO PFX "Maximum main memory to use for agp memory: %ldM\n", result);
	result = result << (20 - PAGE_SHIFT);
	return result;
}


static int agp_backend_initialize(struct agp_bridge_data *bridge)
{
	int size_value, rc, got_gatt=0, got_keylist=0;

	bridge->max_memory_agp = agp_find_max();
	bridge->version = &agp_current_version;

	if (bridge->needs_scratch_page) {
		void *addr = bridge->agp_alloc_page();

		if (!addr) {
			printk(KERN_ERR PFX "unable to get memory for scratch page.\n");
			return -ENOMEM;
		}

		bridge->scratch_page_real = virt_to_phys(addr);
		bridge->scratch_page =
		    bridge->mask_memory(bridge->scratch_page_real, 0);
	}

	size_value = bridge->fetch_size();
	if (size_value == 0) {
		printk(KERN_ERR PFX "unable to determine aperture size.\n");
		rc = -EINVAL;
		goto err_out;
	}
	if (bridge->create_gatt_table()) {
		printk(KERN_ERR PFX
		    "unable to get memory for graphics translation table.\n");
		rc = -ENOMEM;
		goto err_out;
	}
	got_gatt = 1;
	
	bridge->key_list = vmalloc(PAGE_SIZE * 4);
	if (bridge->key_list == NULL) {
		printk(KERN_ERR PFX "error allocating memory for key lists.\n");
		rc = -ENOMEM;
		goto err_out;
	}
	got_keylist = 1;
	
	/* FIXME vmalloc'd memory not guaranteed contiguous */
	memset(bridge->key_list, 0, PAGE_SIZE * 4);

	if (bridge->configure()) {
		printk(KERN_ERR PFX "error configuring host chipset.\n");
		rc = -EINVAL;
		goto err_out;
	}

	printk(KERN_INFO PFX "AGP aperture is %dM @ 0x%lx\n",
	       size_value, bridge->gart_bus_addr);

	return 0;

err_out:
	if (bridge->needs_scratch_page)
		bridge->agp_destroy_page(phys_to_virt(bridge->scratch_page_real));
	if (got_gatt)
		bridge->free_gatt_table();
	if (got_keylist)
		vfree(bridge->key_list);
	return rc;
}

/* cannot be __exit b/c as it could be called from __init code */
static void agp_backend_cleanup(struct agp_bridge_data *bridge)
{
	if (bridge->cleanup)
		bridge->cleanup();
	if (bridge->free_gatt_table)
		bridge->free_gatt_table();
	if (bridge->key_list)
		vfree(bridge->key_list);

	if (bridge->agp_destroy_page && bridge->needs_scratch_page)
		bridge->agp_destroy_page(
				phys_to_virt(bridge->scratch_page_real));
}

static int agp_power(struct pm_dev *dev, pm_request_t rq, void *data)
{
	switch(rq) {
		case PM_SUSPEND:
			return agp_bridge->suspend();
		case PM_RESUME:
			agp_bridge->resume();
			return 0;
	}		
	return 0;
}


static const drm_agp_t drm_agp = {
	&agp_free_memory,
	&agp_allocate_memory,
	&agp_bind_memory,
	&agp_unbind_memory,
	&agp_enable,
	&agp_backend_acquire,
	&agp_backend_release,
	&agp_copy_info
};


int agp_register_driver (struct agp_driver *drv)
{
	int ret_val;

	if (drv->dev == NULL) {
		printk (KERN_DEBUG PFX "Erk, registering with no pci_dev!\n");
		return -EINVAL;
	}

	if (agp_count==1) {
		printk (KERN_DEBUG PFX "Only one agpgart device currently supported.\n");
		return -ENODEV;
	}

	/* Grab reference on the chipset driver. */
	if (!try_module_get(drv->owner))
		return -EINVAL;

	ret_val = agp_backend_initialize(agp_bridge);
	if (ret_val)
		goto err_out;

	ret_val = agp_frontend_initialize();
	if (ret_val)
		goto frontend_err;

	/* FIXME: What to do with this? */
	inter_module_register("drm_agp", THIS_MODULE, &drm_agp);

	pm_register(PM_PCI_DEV, PM_PCI_ID(agp_bridge->dev), agp_power);
	agp_count++;
	return 0;

frontend_err:
	agp_backend_cleanup(agp_bridge);
err_out:
	agp_bridge->type = NOT_SUPPORTED;
	module_put(drv->owner);
	drv->dev = NULL;
	return ret_val;
}
EXPORT_SYMBOL_GPL(agp_register_driver);


int agp_unregister_driver(struct agp_driver *drv)
{
	if (drv->dev==NULL)
		return -ENODEV;

	agp_bridge->type = NOT_SUPPORTED;
	pm_unregister_all(agp_power);
	agp_frontend_cleanup();
	agp_backend_cleanup(agp_bridge);
	inter_module_unregister("drm_agp");
	agp_count--;
	module_put(drv->owner);
	return 0;
}
EXPORT_SYMBOL_GPL(agp_unregister_driver);


static int __init agp_init(void)
{
	printk(KERN_INFO "Linux agpgart interface v%d.%d (c) Dave Jones\n",
	       AGPGART_VERSION_MAJOR, AGPGART_VERSION_MINOR);
	return 0;
}

void __exit agp_exit(void)
{
}


MODULE_AUTHOR("Dave Jones <davej@codemonkey.org.uk>");
MODULE_DESCRIPTION("AGP GART driver");
MODULE_LICENSE("GPL and additional rights");

module_init(agp_init);
module_exit(agp_exit);

