/*
 * card.c - contains functions for managing groups of PnP devices
 *
 * Copyright 2002 Adam Belay <ambx1@neo.rr.com>
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/slab.h>

#ifdef CONFIG_PNP_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif

#include <linux/pnp.h>
#include <linux/init.h>
#include "base.h"


LIST_HEAD(pnp_cards);

static const struct pnp_card_id * match_card(struct pnpc_driver *drv, struct pnp_card *card)
{
	const struct pnp_card_id *drv_id = drv->id_table;
	while (*drv_id->id){
		if (compare_pnp_id(card->id,drv_id->id))
			return drv_id;
		drv_id++;
	}
	return NULL;
}

static int card_bus_match(struct device *dev, struct device_driver *drv)
{
	struct pnp_card * card = to_pnp_card(dev);
	struct pnpc_driver * pnp_drv = to_pnpc_driver(drv);
	if (match_card(pnp_drv, card) == NULL)
		return 0;
	return 1;
}

struct bus_type pnpc_bus_type = {
	name:	"pnp_card",
	match:	card_bus_match,
};


/**
 * pnpc_add_id - adds an EISA id to the specified card
 * @id: pointer to a pnp_id structure
 * @card: pointer to the desired card
 *
 */

int pnpc_add_id(struct pnp_id *id, struct pnp_card *card)
{
	struct pnp_id *ptr;
	if (!id)
		return -EINVAL;
	if (!card)
		return -EINVAL;
	id->next = NULL;
	ptr = card->id;
	while (ptr && ptr->next)
		ptr = ptr->next;
	if (ptr)
		ptr->next = id;
	else
		card->id = id;
	return 0;
}

static void pnpc_free_ids(struct pnp_card *card)
{
	struct pnp_id * id;
	struct pnp_id *next;
	if (!card)
		return;
	id = card->id;
	while (id) {
		next = id->next;
		kfree(id);
		id = next;
	}
}

static void pnp_release_card(struct device *dmdev)
{
	struct pnp_card * card = to_pnp_card(dmdev);
	pnpc_free_ids(card);
	kfree(card);
}

/**
 * pnpc_add_card - adds a PnP card to the PnP Layer
 * @card: pointer to the card to add
 */

int pnpc_add_card(struct pnp_card *card)
{
	int error = 0;
	if (!card || !card->protocol)
		return -EINVAL;
	sprintf(card->dev.bus_id, "%02x:%02x", card->protocol->number, card->number);
	INIT_LIST_HEAD(&card->rdevs);
	card->dev.parent = &card->protocol->dev;
	card->dev.bus = &pnpc_bus_type;
	card->dev.release = &pnp_release_card;
	card->status = PNP_READY;
	error = device_register(&card->dev);
	if (error == 0){
		struct list_head *pos;
		spin_lock(&pnp_lock);
		list_add_tail(&card->global_list, &pnp_cards);
		list_add_tail(&card->protocol_list, &card->protocol->cards);
		spin_unlock(&pnp_lock);
		list_for_each(pos,&card->devices){
			struct pnp_dev *dev = card_to_pnp_dev(pos);
			__pnp_add_device(dev);
		}
	}
	return error;
}

/**
 * pnpc_remove_card - removes a PnP card from the PnP Layer
 * @card: pointer to the card to remove
 */

void pnpc_remove_card(struct pnp_card *card)
{
	struct list_head *pos, *temp;
	if (!card)
		return;
	device_unregister(&card->dev);
	spin_lock(&pnp_lock);
	list_del(&card->global_list);
	list_del(&card->protocol_list);
	spin_unlock(&pnp_lock);
	list_for_each_safe(pos,temp,&card->devices){
		struct pnp_dev *dev = card_to_pnp_dev(pos);
		pnpc_remove_device(dev);
		__pnp_remove_device(dev);
	}
}

/**
 * pnpc_add_device - adds a device to the specified card
 * @card: pointer to the card to add to
 * @dev: pointer to the device to add
 */

int pnpc_add_device(struct pnp_card *card, struct pnp_dev *dev)
{
	if (!dev || !dev->protocol || !card)
		return -EINVAL;
	dev->dev.parent = &card->dev;
	sprintf(dev->dev.bus_id, "%02x:%02x.%02x", dev->protocol->number, card->number,dev->number);
	spin_lock(&pnp_lock);
	dev->card = card;
	list_add_tail(&dev->card_list, &card->devices);
	spin_unlock(&pnp_lock);
	return 0;
}

/**
 * pnpc_remove_device- removes a device from the specified card
 * @card: pointer to the card to remove from
 * @dev: pointer to the device to remove
 */

void pnpc_remove_device(struct pnp_dev *dev)
{
	spin_lock(&pnp_lock);
	dev->card = NULL;
	list_del(&dev->card_list);
	spin_unlock(&pnp_lock);
	__pnp_remove_device(dev);
}

/**
 * pnp_request_card_device - Searches for a PnP device under the specified card
 * @card: pointer to the card to search under, cannot be NULL
 * @id: pointer to a PnP ID structure that explains the rules for finding the device
 * @from: Starting place to search from. If NULL it will start from the begining.
 *
 * Will activate the device
 */

struct pnp_dev * pnp_request_card_device(struct pnp_card *card, const char *id, struct pnp_dev *from)
{
	struct list_head *pos;
	struct pnp_dev *dev;
	struct pnpc_driver *cdrv;
	if (!card || !id)
		goto done;
	if (!from) {
		pos = card->devices.next;
	} else {
		if (from->card != card)
			goto done;
		pos = from->card_list.next;
	}
	while (pos != &card->devices) {
		dev = card_to_pnp_dev(pos);
		if (compare_pnp_id(dev->id,id))
			goto found;
		pos = pos->next;
	}

done:
	return NULL;

found:
	if (pnp_device_attach(dev) < 0)
		return NULL;
	cdrv = to_pnpc_driver(card->dev.driver);
	if (dev->active == 0) {
		if (!(cdrv->flags & PNPC_DRIVER_DO_NOT_ACTIVATE)) {
			if(pnp_activate_dev(dev)<0) {
				pnp_device_detach(dev);
				return NULL;
			}
		}
	} else {
		if ((cdrv->flags & PNPC_DRIVER_DO_NOT_ACTIVATE))
			pnp_disable_dev(dev);
	}
	spin_lock(&pnp_lock);
	list_add_tail(&dev->rdev_list, &card->rdevs);
	spin_unlock(&pnp_lock);
	return dev;
}

/**
 * pnp_release_card_device - call this when the driver no longer needs the device
 * @dev: pointer to the PnP device stucture
 *
 * Will disable the device
 */

void pnp_release_card_device(struct pnp_dev *dev)
{
	spin_lock(&pnp_lock);
	list_del(&dev->rdev_list);
	spin_unlock(&pnp_lock);
	pnp_device_detach(dev);
}

static void pnpc_recover_devices(struct pnp_card *card)
{
	struct list_head *pos, *temp;
	list_for_each_safe(pos,temp,&card->rdevs){
		struct pnp_dev *dev = list_entry(pos, struct pnp_dev, rdev_list);
		pnp_release_card_device(dev);
	}
}

int pnpc_attach(struct pnp_card *pnp_card)
{
	spin_lock(&pnp_lock);
	if(pnp_card->status != PNP_READY){
		spin_unlock(&pnp_lock);
		return -EBUSY;
	}
	pnp_card->status = PNP_ATTACHED;
	spin_unlock(&pnp_lock);
	return 0;
}
 
void pnpc_detach(struct pnp_card *pnp_card)
{
	spin_lock(&pnp_lock);
	if (pnp_card->status == PNP_ATTACHED)
		pnp_card->status = PNP_READY;
	spin_unlock(&pnp_lock);
	pnpc_recover_devices(pnp_card);
}

static int pnpc_card_probe(struct device *dev)
{
	int error = 0;
	struct pnpc_driver *drv = to_pnpc_driver(dev->driver);
	struct pnp_card *card = to_pnp_card(dev);
	const struct pnp_card_id *card_id = NULL;

	pnp_dbg("pnp: match found with the PnP card '%s' and the driver '%s'", dev->bus_id,drv->name);

	error = pnpc_attach(card);
	if (error < 0)
		return error;
	if (drv->probe) {
		card_id = match_card(drv, card);
		if (card_id != NULL)
			error = drv->probe(card, card_id);
		if (error >= 0){
			card->driver = drv;
			error = 0;
		} else
			pnpc_detach(card);
	}
	return error;
}

static int pnpc_card_remove(struct device *dev)
{
	struct pnp_card * card = to_pnp_card(dev);
	struct pnpc_driver * drv = card->driver;

	if (drv) {
		if (drv->remove)
			drv->remove(card);
		card->driver = NULL;
	}
	pnpc_detach(card);
	return 0;
}

/**
 * pnpc_register_driver - registers a PnP card driver with the PnP Layer
 * @cdrv: pointer to the driver to register
 */

int pnpc_register_driver(struct pnpc_driver * drv)
{
	int count;
	struct list_head *pos;

	drv->driver.name = drv->name;
	drv->driver.bus = &pnpc_bus_type;
	drv->driver.probe = pnpc_card_probe;
	drv->driver.remove = pnpc_card_remove;

	pnp_dbg("the card driver '%s' has been registered", drv->name);

	count = driver_register(&drv->driver);

	/* get the number of initial matches */
	if (count >= 0){
		count = 0;
		list_for_each(pos,&drv->driver.devices){
			count++;
		}
	}
	return count;
}

/**
 * pnpc_unregister_driver - unregisters a PnP card driver from the PnP Layer
 * @cdrv: pointer to the driver to unregister
 *
 * Automatically disables requested devices
 */

void pnpc_unregister_driver(struct pnpc_driver *drv)
{
	driver_unregister(&drv->driver);
	pnp_dbg("the card driver '%s' has been unregistered", drv->name);
}

static int __init pnp_card_init(void)
{
	printk(KERN_INFO "pnp: Enabling Plug and Play Card Services.\n");
	return bus_register(&pnpc_bus_type);
}

subsys_initcall(pnp_card_init);

EXPORT_SYMBOL(pnpc_add_card);
EXPORT_SYMBOL(pnpc_remove_card);
EXPORT_SYMBOL(pnpc_add_device);
EXPORT_SYMBOL(pnpc_remove_device);
EXPORT_SYMBOL(pnp_request_card_device);
EXPORT_SYMBOL(pnp_release_card_device);
EXPORT_SYMBOL(pnpc_register_driver);
EXPORT_SYMBOL(pnpc_unregister_driver);
EXPORT_SYMBOL(pnpc_add_id);
EXPORT_SYMBOL(pnpc_attach);
EXPORT_SYMBOL(pnpc_detach);
