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
#include "base.h"

LIST_HEAD(pnp_cards);


static const struct pnp_card_id * match_card(struct pnp_card_driver * drv, struct pnp_card * card)
{
	const struct pnp_card_id * drv_id = drv->id_table;
	while (*drv_id->id){
		if (compare_pnp_id(card->id,drv_id->id))
			return drv_id;
		drv_id++;
	}
	return NULL;
}

static void generic_card_remove_handler(struct pnp_dev * dev)
{
	struct pnp_card_driver * drv = to_pnp_card_driver(dev->driver);
	if (!dev->card || !drv)
		return;
	if (drv->remove)
		drv->remove(dev->card);
	drv->link.remove = NULL;
}

/**
 * pnp_add_card_id - adds an EISA id to the specified card
 * @id: pointer to a pnp_id structure
 * @card: pointer to the desired card
 *
 */

int pnp_add_card_id(struct pnp_id *id, struct pnp_card * card)
{
	struct pnp_id * ptr;
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

static void pnp_free_card_ids(struct pnp_card * card)
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

/**
 * pnp_add_card - adds a PnP card to the PnP Layer
 * @card: pointer to the card to add
 */

int pnp_add_card(struct pnp_card * card)
{
	int error = 0;
	struct list_head * pos;
	if (!card || !card->protocol)
		return -EINVAL;

	spin_lock(&pnp_lock);
	list_add_tail(&card->global_list, &pnp_cards);
	list_add_tail(&card->protocol_list, &card->protocol->cards);
	spin_unlock(&pnp_lock);

	/* we wait until now to add devices in order to ensure the drivers
	 * will be able to use all of the related devices on the card 
	 * without waiting any unresonable length of time */
	list_for_each(pos,&card->devices){
		struct pnp_dev *dev = card_to_pnp_dev(pos);
		__pnp_add_device(dev);
	}
	return error;
}

/**
 * pnp_remove_card - removes a PnP card from the PnP Layer
 * @card: pointer to the card to remove
 */

void pnp_remove_card(struct pnp_card * card)
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
		pnp_remove_card_device(dev);
	}
	pnp_free_card_ids(card);
	kfree(card);
}

/**
 * pnp_add_card_device - adds a device to the specified card
 * @card: pointer to the card to add to
 * @dev: pointer to the device to add
 */

int pnp_add_card_device(struct pnp_card * card, struct pnp_dev * dev)
{
	if (!card || !dev || !dev->protocol)
		return -EINVAL;
	dev->dev.parent = &dev->protocol->dev;
	snprintf(dev->dev.bus_id, BUS_ID_SIZE, "%02x:%02x.%02x", dev->protocol->number,
		 card->number,dev->number);
	spin_lock(&pnp_lock);
	dev->card = card;
	list_add_tail(&dev->card_list, &card->devices);
	spin_unlock(&pnp_lock);
	return 0;
}

/**
 * pnp_remove_card_device- removes a device from the specified card
 * @card: pointer to the card to remove from
 * @dev: pointer to the device to remove
 */

void pnp_remove_card_device(struct pnp_dev * dev)
{
	spin_lock(&pnp_lock);
	dev->card = NULL;
	list_del(&dev->card_list);
	spin_unlock(&pnp_lock);
	__pnp_remove_device(dev);
}

/**
 * pnp_request_card_device - Searches for a PnP device under the specified card
 * @drv: pointer to the driver requesting the card
 * @card: pointer to the card to search under, cannot be NULL
 * @id: pointer to a PnP ID structure that explains the rules for finding the device
 * @from: Starting place to search from. If NULL it will start from the begining.
 */

struct pnp_dev * pnp_request_card_device(struct pnp_card_driver * drv, struct pnp_card *card, const char * id, struct pnp_dev * from)
{
	struct list_head * pos;
	struct pnp_dev * dev;
	if (!card || !id || !drv)
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
	down_write(&dev->dev.bus->subsys.rwsem);
	dev->dev.driver = &drv->link.driver;
	if (drv->link.driver.probe) {
		if (drv->link.driver.probe(&dev->dev)) {
			dev->dev.driver = NULL;
			return NULL;
		}
	}
	device_bind_driver(&dev->dev);
	up_write(&dev->dev.bus->subsys.rwsem);

	return dev;
}

/**
 * pnp_release_card_device - call this when the driver no longer needs the device
 * @dev: pointer to the PnP device stucture
 */

void pnp_release_card_device(struct pnp_dev * dev)
{
	struct pnp_card_driver * drv = to_pnp_card_driver(to_pnp_driver(dev->dev.driver));
	if (!drv)
		return;
	down_write(&dev->dev.bus->subsys.rwsem);
	drv->link.remove = NULL;
	device_release_driver(&dev->dev);
	drv->link.remove = &generic_card_remove_handler;
	up_write(&dev->dev.bus->subsys.rwsem);
}

/**
 * pnp_register_card_driver - registers a PnP card driver with the PnP Layer
 * @drv: pointer to the driver to register
 */

int pnp_register_card_driver(struct pnp_card_driver * drv)
{
	int count = 0;
	struct list_head *pos, *temp;

	drv->link.name = drv->name;
	drv->link.id_table = NULL;	/* this will disable auto matching */
	drv->link.flags = drv->flags;
	drv->link.probe = NULL;
	drv->link.remove = &generic_card_remove_handler;

	pnp_register_driver(&drv->link);

	list_for_each_safe(pos,temp,&pnp_cards){
		struct pnp_card *card = list_entry(pos, struct pnp_card, global_list);
		const struct pnp_card_id *id = match_card(drv,card);
		if (id) {
			if (drv->probe) {
				if (drv->probe(card, id)>=0)
					count++;
			} else
				count++;
		}
	}
	return count;
}

/**
 * pnp_unregister_card_driver - unregisters a PnP card driver from the PnP Layer
 * @drv: pointer to the driver to unregister
 */

void pnp_unregister_card_driver(struct pnp_card_driver * drv)
{
	pnp_unregister_driver(&drv->link);

	pnp_dbg("the card driver '%s' has been unregistered", drv->name);
}

EXPORT_SYMBOL(pnp_add_card);
EXPORT_SYMBOL(pnp_remove_card);
EXPORT_SYMBOL(pnp_add_card_device);
EXPORT_SYMBOL(pnp_remove_card_device);
EXPORT_SYMBOL(pnp_add_card_id);
EXPORT_SYMBOL(pnp_request_card_device);
EXPORT_SYMBOL(pnp_release_card_device);
EXPORT_SYMBOL(pnp_register_card_driver);
EXPORT_SYMBOL(pnp_unregister_card_driver);
