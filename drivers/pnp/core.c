/*
 * core.c - contains all core device and protocol registration functions
 *
 * Copyright 2002 Adam Belay <ambx1@neo.rr.com>
 *
 */

#include <linux/pnp.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "base.h"


LIST_HEAD(pnp_protocols);
LIST_HEAD(pnp_global);
spinlock_t pnp_lock = SPIN_LOCK_UNLOCKED;

void *pnp_alloc(long size)
{
	void *result;

	result = kmalloc(size, GFP_KERNEL);
	if (!result){
		printk(KERN_ERR "pnp: Out of Memory\n");
		return NULL;
	}
	memset(result, 0, size);
	return result;
}

/**
 * pnp_protocol_register - adds a pnp protocol to the pnp layer
 * @protocol: pointer to the corresponding pnp_protocol structure
 *
 *  Ex protocols: ISAPNP, PNPBIOS, etc
 */

int pnp_protocol_register(struct pnp_protocol *protocol)
{
	int nodenum;
	struct list_head * pos;

	if (!protocol)
		return -EINVAL;

	INIT_LIST_HEAD(&protocol->devices);
	nodenum = 0;
	spin_lock(&pnp_lock);

	/* assign the lowest unused number */
	list_for_each(pos,&pnp_protocols) {
		struct pnp_protocol * cur = to_pnp_protocol(pos);
		if (cur->number == nodenum){
			pos = &pnp_protocols;
			nodenum++;
		}
	}

	list_add_tail(&protocol->protocol_list, &pnp_protocols);
	spin_unlock(&pnp_lock);

	protocol->number = nodenum;
	sprintf(protocol->dev.bus_id, "pnp%d", nodenum);
	strncpy(protocol->dev.name,protocol->name,DEVICE_NAME_SIZE);
	return device_register(&protocol->dev);
}

/**
 * pnp_protocol_unregister - removes a pnp protocol from the pnp layer
 * @protocol: pointer to the corresponding pnp_protocol structure
 *
 */
void pnp_protocol_unregister(struct pnp_protocol *protocol)
{
	spin_lock(&pnp_lock);
	list_del_init(&protocol->protocol_list);
	spin_unlock(&pnp_lock);
	device_unregister(&protocol->dev);
}

/**
 * pnp_init_device - pnp protocols should call this before adding a PnP device
 * @dev: pointer to dev to init
 *
 *  for now it only inits dev->ids, more later?
 */

int pnp_init_device(struct pnp_dev *dev)
{
	INIT_LIST_HEAD(&dev->ids);
	return 0;
}

static void pnp_release_device(struct device *dmdev)
{
	struct pnp_dev * dev = to_pnp_dev(dmdev);
	if (dev->res)
		pnp_free_resources(dev->res);
	pnp_free_ids(dev);
	kfree(dev);
}

/**
 * pnp_add_device - adds a pnp device to the pnp layer
 * @dev: pointer to dev to add
 *
 *  adds to driver model, name database, fixups, interface, etc.
 */

int pnp_add_device(struct pnp_dev *dev)
{
	int error = 0;
	if (!dev || !dev->protocol)
		return -EINVAL;
	if (dev->card)
		sprintf(dev->dev.bus_id, "%02x:%02x.%02x", dev->protocol->number,
		  dev->card->number,dev->number);
	else
		sprintf(dev->dev.bus_id, "%02x:%02x", dev->protocol->number,
		  dev->number);
	pnp_name_device(dev);
	pnp_fixup_device(dev);
	strcpy(dev->dev.name,dev->name);
	dev->dev.parent = &dev->protocol->dev;
	dev->dev.bus = &pnp_bus_type;
	dev->dev.release = &pnp_release_device;
	error = device_register(&dev->dev);
	if (error == 0){
		spin_lock(&pnp_lock);
		list_add_tail(&dev->global_list, &pnp_global);
		list_add_tail(&dev->dev_list, &dev->protocol->devices);
		spin_unlock(&pnp_lock);
		pnp_interface_attach_device(dev);
	}
	return error;
}

/**
 * pnp_remove_device - removes a pnp device from the pnp layer
 * @dev: pointer to dev to add
 *
 * this function will free all mem used by dev
 */
void pnp_remove_device(struct pnp_dev *dev)
{
	if (!dev)
		return;
	device_unregister(&dev->dev);
	spin_lock(&pnp_lock);
	list_del_init(&dev->global_list);
	list_del_init(&dev->dev_list);
	spin_unlock(&pnp_lock);
}

static int __init pnp_init(void)
{
	printk(KERN_INFO "Linux Plug and Play Support v0.9 (c) Adam Belay\n");
	return bus_register(&pnp_bus_type);
}

core_initcall(pnp_init);

EXPORT_SYMBOL(pnp_protocol_register);
EXPORT_SYMBOL(pnp_protocol_unregister);
EXPORT_SYMBOL(pnp_add_device);
EXPORT_SYMBOL(pnp_remove_device);
EXPORT_SYMBOL(pnp_init_device);
