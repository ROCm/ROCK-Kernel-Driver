/*
 * driver.c - device id matching, driver model, etc.
 *
 * Copyright 2002 Adam Belay <ambx1@neo.rr.com>
 *
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/slab.h>

#ifdef CONFIG_PNP_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif

#include <linux/pnp.h>

static int compare_func(const char *ida, const char *idb)
{
	int i;
	/* we only need to compare the last 4 chars */
	for (i=3; i<7; i++)
	{
		if (ida[i] != 'X' &&
		    idb[i] != 'X' &&
		    toupper(ida[i]) != toupper(idb[i]))
			return 0;
	}
	return 1;
}

int compare_pnp_id(struct list_head *id_list, const char *id)
{
	struct list_head *pos;
	if (!id_list || !id || (strlen(id) != 7))
		return 0;
	if (memcmp(id,"ANYDEVS",7)==0)
		return 1;
	list_for_each(pos,id_list){
		struct pnp_id *pnp_id = to_pnp_id(pos);
		if (memcmp(pnp_id->id,id,3)==0)
			if (compare_func(pnp_id->id,id)==1)
				return 1;
	}
	return 0;
}

static const struct pnp_id * match_card(struct pnp_driver *drv, struct pnp_card *card)
{
	const struct pnp_id *drv_card_id = drv->card_id_table;
	if (!drv)
		return NULL;
	if (!card)
		return NULL;
	while (*drv_card_id->id){
		if (compare_pnp_id(&card->ids,drv_card_id->id))
			return drv_card_id;
		drv_card_id++;
	}
	return NULL;
}

static const struct pnp_id * match_device(struct pnp_driver *drv, struct pnp_dev *dev)
{
	const struct pnp_id *drv_id = drv->id_table;
	if (!drv)
		return NULL;
	if (!dev)
		return NULL;
	while (*drv_id->id){
		if (compare_pnp_id(&dev->ids,drv_id->id))
			return drv_id;
		drv_id++;
	}
	return NULL;
}

static int pnp_device_probe(struct device *dev)
{
	int error = 0;
	struct pnp_driver *pnp_drv;
	struct pnp_dev *pnp_dev;
	const struct pnp_id *card_id = NULL;
	const struct pnp_id *dev_id = NULL;
	pnp_dev = to_pnp_dev(dev);
	pnp_drv = to_pnp_driver(dev->driver);
	pnp_dbg("pnp: match found with the PnP device '%s' and the driver '%s'", dev->bus_id,pnp_drv->name);

	if (pnp_dev->active == 0)
		if(pnp_activate_dev(pnp_dev)<0)
			return -1;
	if (pnp_drv->probe && pnp_dev->active) {
		if (pnp_dev->card && pnp_drv->card_id_table){
			card_id = match_card(pnp_drv, pnp_dev->card);
			if (card_id != NULL)
				dev_id = match_device(pnp_drv, pnp_dev);
			if (dev_id != NULL)
				error = pnp_drv->probe(pnp_dev, card_id, dev_id);
		}
		else{
			dev_id = match_device(pnp_drv, pnp_dev);
			if (dev_id != NULL)
				error = pnp_drv->probe(pnp_dev, card_id, dev_id);
		}
		if (error >= 0){
			pnp_dev->driver = pnp_drv;
			error = 0;
		}
	}
	return error;
}

static int pnp_device_remove(struct device *dev)
{
	struct pnp_dev * pnp_dev = to_pnp_dev(dev);
	struct pnp_driver * drv = pnp_dev->driver;

	if (drv) {
		if (drv->remove)
			drv->remove(pnp_dev);
		pnp_dev->driver = NULL;
	}
	pnp_disable_dev(pnp_dev);
	return 0;
}

static int pnp_bus_match(struct device *dev, struct device_driver *drv)
{
	struct pnp_dev * pnp_dev = to_pnp_dev(dev);
	struct pnp_driver * pnp_drv = to_pnp_driver(drv);
	if (pnp_dev->card && pnp_drv->card_id_table
	    && match_card(pnp_drv, pnp_dev->card) == NULL)
		return 0;
	if (match_device(pnp_drv, pnp_dev) == NULL)
		return 0;
	return 1;
}


struct bus_type pnp_bus_type = {
	name:	"pnp",
	match:	pnp_bus_match,
};


int pnp_register_driver(struct pnp_driver *drv)
{
	int count;
	struct list_head *pos;

	pnp_dbg("the driver '%s' has been registered", drv->name);

	drv->driver.name = drv->name;
	drv->driver.bus = &pnp_bus_type;
	drv->driver.probe = pnp_device_probe;
	drv->driver.remove = pnp_device_remove;

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

void pnp_unregister_driver(struct pnp_driver *drv)
{
	pnp_dbg("the driver '%s' has been unregistered", drv->name);
	driver_unregister(&drv->driver);
}

/**
 * pnp_add_id - adds an EISA id to the specified device
 * @id: pointer to a pnp_id structure
 * @dev: pointer to the desired device
 *
 */

int pnp_add_id(struct pnp_id *id, struct pnp_dev *dev)
{
	if (!id)
		return -EINVAL;
	if (!dev)
		return -EINVAL;
	list_add_tail(&id->id_list,&dev->ids);
	return 0;
}

void pnp_free_ids(struct pnp_dev *dev)
{
	struct list_head *pos;
	if (!dev)
		return;
	list_for_each(pos,&dev->ids){
		struct pnp_id *pnp_id = to_pnp_id(pos);
		kfree(pnp_id);
	}
}

EXPORT_SYMBOL(pnp_register_driver);
EXPORT_SYMBOL(pnp_unregister_driver);
EXPORT_SYMBOL(pnp_add_id);
