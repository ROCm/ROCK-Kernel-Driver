/*
 * manager.c - Resource Management, Conflict Resolution, Activation and Disabling of Devices
 *
 * Copyright 2003 Adam Belay <ambx1@neo.rr.com>
 *
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>

#ifdef CONFIG_PNP_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif

#include <linux/pnp.h>
#include "base.h"


int pnp_max_moves = 4;


static int pnp_next_port(struct pnp_dev * dev, int idx)
{
	struct pnp_port *port;
	unsigned long *start, *end, *flags;
	if (!dev || idx < 0 || idx >= PNP_MAX_PORT)
		return 0;
	port = dev->rule->port[idx];
	if (!port)
		return 1;

	start = &dev->res.port_resource[idx].start;
	end = &dev->res.port_resource[idx].end;
	flags = &dev->res.port_resource[idx].flags;

	/* set the initial values if this is the first time */
	if (*start == 0) {
		*start = port->min;
		*end = *start + port->size - 1;
		*flags = port->flags | IORESOURCE_IO;
		if (!pnp_check_port(dev, idx))
			return 1;
	}

	/* run through until pnp_check_port is happy */
	do {
		*start += port->align;
		*end = *start + port->size - 1;
		if (*start > port->max || !port->align)
			return 0;
	} while (pnp_check_port(dev, idx));
	return 1;
}

static int pnp_next_mem(struct pnp_dev * dev, int idx)
{
	struct pnp_mem *mem;
	unsigned long *start, *end, *flags;
	if (!dev || idx < 0 || idx >= PNP_MAX_MEM)
		return 0;
	mem = dev->rule->mem[idx];
	if (!mem)
		return 1;

	start = &dev->res.mem_resource[idx].start;
	end = &dev->res.mem_resource[idx].end;
	flags = &dev->res.mem_resource[idx].flags;

	/* set the initial values if this is the first time */
	if (*start == 0) {
		*start = mem->min;
		*end = *start + mem->size -1;
		*flags = mem->flags | IORESOURCE_MEM;
		if (!(mem->flags & IORESOURCE_MEM_WRITEABLE))
			*flags |= IORESOURCE_READONLY;
		if (mem->flags & IORESOURCE_MEM_CACHEABLE)
			*flags |= IORESOURCE_CACHEABLE;
		if (mem->flags & IORESOURCE_MEM_RANGELENGTH)
			*flags |= IORESOURCE_RANGELENGTH;
		if (mem->flags & IORESOURCE_MEM_SHADOWABLE)
			*flags |= IORESOURCE_SHADOWABLE;
		if (!pnp_check_mem(dev, idx))
			return 1;
	}

	/* run through until pnp_check_mem is happy */
	do {
		*start += mem->align;
		*end = *start + mem->size - 1;
		if (*start > mem->max || !mem->align)
			return 0;
	} while (pnp_check_mem(dev, idx));
	return 1;
}

static int pnp_next_irq(struct pnp_dev * dev, int idx)
{
	struct pnp_irq *irq;
	unsigned long *start, *end, *flags;
	int i, mask;
	if (!dev || idx < 0 || idx >= PNP_MAX_IRQ)
		return 0;
	irq = dev->rule->irq[idx];
	if (!irq)
		return 1;

	start = &dev->res.irq_resource[idx].start;
	end = &dev->res.irq_resource[idx].end;
	flags = &dev->res.irq_resource[idx].flags;

	/* set the initial values if this is the first time */
	if (*start == -1) {
		*start = *end = 0;
		*flags = irq->flags | IORESOURCE_IRQ;
		if (!pnp_check_irq(dev, idx))
			return 1;
	}

	mask = irq->map;
	for (i = *start + 1; i < 16; i++)
	{
		if(mask>>i & 0x01) {
			*start = *end = i;
			if(!pnp_check_irq(dev, idx))
				return 1;
		}
	}
	return 0;
}

static int pnp_next_dma(struct pnp_dev * dev, int idx)
{
	struct pnp_dma *dma;
	unsigned long *start, *end, *flags;
	int i, mask;
	if (!dev || idx < 0 || idx >= PNP_MAX_DMA)
		return -EINVAL;
	dma = dev->rule->dma[idx];
	if (!dma)
		return 1;

	start = &dev->res.dma_resource[idx].start;
	end = &dev->res.dma_resource[idx].end;
	flags = &dev->res.dma_resource[idx].flags;

	/* set the initial values if this is the first time */
	if (*start == -1) {
		*start = *end = 0;
		*flags = dma->flags | IORESOURCE_DMA;
		if (!pnp_check_dma(dev, idx))
			return 1;
	}

	mask = dma->map;
	for (i = *start + 1; i < 8; i++)
	{
		if(mask>>i & 0x01) {
			*start = *end = i;
			if(!pnp_check_dma(dev, idx))
				return 1;
		}
	}
	return 0;
}

static int pnp_next_rule(struct pnp_dev *dev)
{
	int depnum = dev->rule->depnum;
        int max = pnp_get_max_depnum(dev);
	int priority = PNP_RES_PRIORITY_PREFERRED;

	if (depnum < 0)
		return 0;

	if (max == 0) {
		if (pnp_generate_rule(dev, 0, dev->rule)) {
			dev->rule->depnum = -1;
			return 1;
		}
	}

	if(depnum > 0) {
		struct pnp_resources * res = pnp_find_resources(dev, depnum);
		priority = res->priority;
	}

	for (; priority <= PNP_RES_PRIORITY_FUNCTIONAL; priority++, depnum = 0) {
		depnum += 1;
		for (; depnum <= max; depnum++) {
			struct pnp_resources * res = pnp_find_resources(dev, depnum);
			if (res->priority == priority) {
				if(pnp_generate_rule(dev, depnum, dev->rule)) {
					dev->rule->depnum = depnum;
					return 1;
				}
			}
		}
	}
	return 0;
}

struct pnp_change {
	struct list_head change_list;
	struct list_head changes;
	struct pnp_resource_table res_bak;
	struct pnp_rule_table rule_bak;
	struct pnp_dev * dev;
};

static void pnp_free_changes(struct pnp_change * parent)
{
	struct list_head * pos, * temp;
	list_for_each_safe(pos, temp, &parent->changes) {
		struct pnp_change * change = list_entry(pos, struct pnp_change, change_list);
		list_del(&change->change_list);
		kfree(change);
	}
}

static void pnp_undo_changes(struct pnp_change * parent)
{
	struct list_head * pos, * temp;
	list_for_each_safe(pos, temp, &parent->changes) {
		struct pnp_change * change = list_entry(pos, struct pnp_change, change_list);
		*change->dev->rule = change->rule_bak;
		change->dev->res = change->res_bak;
		list_del(&change->change_list);
		kfree(change);
	}
}

static struct pnp_change * pnp_add_change(struct pnp_change * parent, struct pnp_dev * dev)
{
	struct pnp_change * change = pnp_alloc(sizeof(struct pnp_change));
	if (!change)
		return NULL;
	change->res_bak = dev->res;
	change->rule_bak = *dev->rule;
	change->dev = dev;
	INIT_LIST_HEAD(&change->changes);
	if (parent)
		list_add(&change->change_list, &parent->changes);
	return change;
}

static void pnp_commit_changes(struct pnp_change * parent, struct pnp_change * change)
{
	/* check if it's the root change */
	if (!parent)
		return;
	if (!list_empty(&change->changes))
		list_splice_init(&change->changes, &parent->changes);
}

static int pnp_next_config(struct pnp_dev * dev, int move, struct pnp_change * parent);

static int pnp_next_request(struct pnp_dev * dev, int move, struct pnp_change * parent, struct pnp_change * change)
{
	int i;
	struct pnp_dev * cdev;

	for (i = 0; i < PNP_MAX_PORT; i++) {
		if (dev->res.port_resource[i].start == 0
		 || pnp_check_port_conflicts(dev,i,SEARCH_WARM)) {
			if (!pnp_next_port(dev,i))
				return 0;
		}
		do {
			cdev = pnp_check_port_conflicts(dev,i,SEARCH_COLD);
			if (cdev && (!move || !pnp_next_config(cdev,move,change))) {
				pnp_undo_changes(change);
				if (!pnp_next_port(dev,i))
					return 0;
			}
		} while (cdev);
		pnp_commit_changes(parent, change);
	}
	for (i = 0; i < PNP_MAX_MEM; i++) {
		if (dev->res.mem_resource[i].start == 0
		 || pnp_check_mem_conflicts(dev,i,SEARCH_WARM)) {
			if (!pnp_next_mem(dev,i))
				return 0;
		}
		do {
			cdev = pnp_check_mem_conflicts(dev,i,SEARCH_COLD);
			if (cdev && (!move || !pnp_next_config(cdev,move,change))) {
				pnp_undo_changes(change);
				if (!pnp_next_mem(dev,i))
					return 0;
			}
		} while (cdev);
		pnp_commit_changes(parent, change);
	}
	for (i = 0; i < PNP_MAX_IRQ; i++) {
		if (dev->res.irq_resource[i].start == -1
		 || pnp_check_irq_conflicts(dev,i,SEARCH_WARM)) {
			if (!pnp_next_irq(dev,i))
				return 0;
		}
		do {
			cdev = pnp_check_irq_conflicts(dev,i,SEARCH_COLD);
			if (cdev && (!move || !pnp_next_config(cdev,move,change))) {
				pnp_undo_changes(change);
				if (!pnp_next_irq(dev,i))
					return 0;
			}
		} while (cdev);
		pnp_commit_changes(parent, change);
	}
	for (i = 0; i < PNP_MAX_DMA; i++) {
		if (dev->res.dma_resource[i].start == -1
		 || pnp_check_dma_conflicts(dev,i,SEARCH_WARM)) {
			if (!pnp_next_dma(dev,i))
				return 0;
		}
		do {
			cdev = pnp_check_dma_conflicts(dev,i,SEARCH_COLD);
			if (cdev && (!move || !pnp_next_config(cdev,move,change))) {
				pnp_undo_changes(change);
				if (!pnp_next_dma(dev,i))
					return 0;
			}
		} while (cdev);
		pnp_commit_changes(parent, change);
	}
	return 1;
}

static int pnp_next_config(struct pnp_dev * dev, int move, struct pnp_change * parent)
{
	struct pnp_change * change;
	move--;
	if (!dev->rule)
		return 0;
	change = pnp_add_change(parent,dev);
	if (!change)
		return 0;
	if (!pnp_can_configure(dev))
		goto fail;
	if (!dev->rule->depnum) {
		if (!pnp_next_rule(dev))
			goto fail;
	}
	while (!pnp_next_request(dev, move, parent, change)) {
		if(!pnp_next_rule(dev))
			goto fail;
		pnp_init_resource_table(&dev->res);
	}
	if (!parent) {
		pnp_free_changes(change);
		kfree(change);
	}
	return 1;

fail:
	if (!parent)
		kfree(change);
	return 0;
}

/* this advanced algorithm will shuffle other configs to make room and ensure that the most possible devices have configs */
static int pnp_advanced_config(struct pnp_dev * dev)
{
	int move;
	/* if the device cannot be configured skip it */
	if (!pnp_can_configure(dev))
		return 1;
	if (!dev->rule) {
		dev->rule = pnp_alloc(sizeof(struct pnp_rule_table));
		if (!dev->rule)
			return -ENOMEM;
	}

	spin_lock(&pnp_lock);
	for (move = 1; move <= pnp_max_moves; move++) {
		dev->rule->depnum = 0;
		pnp_init_resource_table(&dev->res);
		if (pnp_next_config(dev,move,NULL)) {
			spin_unlock(&pnp_lock);
			return 1;
		}
	}

	pnp_init_resource_table(&dev->res);
	dev->rule->depnum = 0;
	spin_unlock(&pnp_lock);
	pnp_err("res: Unable to resolve resource conflicts for the device '%s', some devices may not be usable.", dev->dev.bus_id);
	return 0;
}

int pnp_resolve_conflicts(struct pnp_dev *dev)
{
	int i;
	struct pnp_dev * cdev;

	for (i = 0; i < PNP_MAX_PORT; i++)
	{
		do {
			cdev = pnp_check_port_conflicts(dev,i,SEARCH_COLD);
			if (cdev)
				pnp_advanced_config(cdev);
		} while (cdev);
	}
	for (i = 0; i < PNP_MAX_MEM; i++)
	{
		do {
			cdev = pnp_check_mem_conflicts(dev,i,SEARCH_COLD);
			if (cdev)
				pnp_advanced_config(cdev);
		} while (cdev);
	}
	for (i = 0; i < PNP_MAX_IRQ; i++)
	{
		do {
			cdev = pnp_check_irq_conflicts(dev,i,SEARCH_COLD);
			if (cdev)
				pnp_advanced_config(cdev);
		} while (cdev);
	}
	for (i = 0; i < PNP_MAX_DMA; i++)
	{
		do {
			cdev = pnp_check_dma_conflicts(dev,i,SEARCH_COLD);
			if (cdev)
				pnp_advanced_config(cdev);
		} while (cdev);
	}
	return 1;
}

/* this is a much faster algorithm but it may not leave resources for other devices to use */
static int pnp_simple_config(struct pnp_dev * dev)
{
	int i;
	spin_lock(&pnp_lock);
	if (dev->active) {
		spin_unlock(&pnp_lock);
		return 1;
	}
	if (!dev->rule) {
		dev->rule = pnp_alloc(sizeof(struct pnp_rule_table));
		if (!dev->rule) {
			spin_unlock(&pnp_lock);
			return -ENOMEM;
		}
	}
	dev->rule->depnum = 0;
	pnp_init_resource_table(&dev->res);
	while (pnp_next_rule(dev)) {
		for (i = 0; i < PNP_MAX_PORT; i++) {
			if (!pnp_next_port(dev,i))
				continue;
		}
		for (i = 0; i < PNP_MAX_MEM; i++) {
			if (!pnp_next_mem(dev,i))
				continue;
		}
		for (i = 0; i < PNP_MAX_IRQ; i++) {
			if (!pnp_next_irq(dev,i))
				continue;
		}
		for (i = 0; i < PNP_MAX_DMA; i++) {
			if (!pnp_next_dma(dev,i))
				continue;
		}
		goto done;
	}
	pnp_init_resource_table(&dev->res);
	dev->rule->depnum = 0;
	spin_unlock(&pnp_lock);
	return 0;

done:
	pnp_resolve_conflicts(dev);	/* this is required or we will break the advanced configs */
	return 1;
}

static int pnp_compare_resources(struct pnp_resource_table * resa, struct pnp_resource_table * resb)
{
	int idx;
	for (idx = 0; idx < PNP_MAX_IRQ; idx++) {
		if (resa->irq_resource[idx].start != resb->irq_resource[idx].start)
			return 1;
	}
	for (idx = 0; idx < PNP_MAX_DMA; idx++) {
		if (resa->dma_resource[idx].start != resb->dma_resource[idx].start)
			return 1;
	}
	for (idx = 0; idx < PNP_MAX_PORT; idx++) {
		if (resa->port_resource[idx].start != resb->port_resource[idx].start)
			return 1;
		if (resa->port_resource[idx].end != resb->port_resource[idx].end)
			return 1;
	}
	for (idx = 0; idx < PNP_MAX_MEM; idx++) {
		if (resa->mem_resource[idx].start != resb->mem_resource[idx].start)
			return 1;
		if (resa->mem_resource[idx].end != resb->mem_resource[idx].end)
			return 1;
	}
	return 0;
}


/*
 * PnP Device Resource Management
 */

/**
 * pnp_auto_config_dev - determines the best possible resource configuration based on available information
 * @dev: pointer to the desired device
 *
 */

int pnp_auto_config_dev(struct pnp_dev *dev)
{
	int error;
	if(!dev)
		return -EINVAL;

	dev->config_mode = PNP_CONFIG_AUTO;

	if(dev->active)
		error = pnp_resolve_conflicts(dev);
	else
		error = pnp_advanced_config(dev);
	return error;
}

static void pnp_process_manual_resources(struct pnp_resource_table * ctab, struct pnp_resource_table * ntab)
{
	int idx;
	for (idx = 0; idx < PNP_MAX_IRQ; idx++) {
		if (ntab->irq_resource[idx].flags & IORESOURCE_AUTO)
			continue;
		ctab->irq_resource[idx].start = ntab->irq_resource[idx].start;
		ctab->irq_resource[idx].end = ntab->irq_resource[idx].end;
		ctab->irq_resource[idx].flags = ntab->irq_resource[idx].flags;
	}
	for (idx = 0; idx < PNP_MAX_DMA; idx++) {
		if (ntab->dma_resource[idx].flags & IORESOURCE_AUTO)
			continue;
		ctab->dma_resource[idx].start = ntab->dma_resource[idx].start;
		ctab->dma_resource[idx].end = ntab->dma_resource[idx].end;
		ctab->dma_resource[idx].flags = ntab->dma_resource[idx].flags;
	}
	for (idx = 0; idx < PNP_MAX_PORT; idx++) {
		if (ntab->port_resource[idx].flags & IORESOURCE_AUTO)
			continue;
		ctab->port_resource[idx].start = ntab->port_resource[idx].start;
		ctab->port_resource[idx].end = ntab->port_resource[idx].end;
		ctab->port_resource[idx].flags = ntab->port_resource[idx].flags;
	}
	for (idx = 0; idx < PNP_MAX_MEM; idx++) {
		if (ntab->irq_resource[idx].flags & IORESOURCE_AUTO)
			continue;
		ctab->irq_resource[idx].start = ntab->mem_resource[idx].start;
		ctab->irq_resource[idx].end = ntab->mem_resource[idx].end;
		ctab->irq_resource[idx].flags = ntab->mem_resource[idx].flags;
	}
}

/**
 * pnp_manual_config_dev - Disables Auto Config and Manually sets the resource table
 * @dev: pointer to the desired device
 * @res: pointer to the new resource config
 *
 * This function can be used by drivers that want to manually set thier resources.
 */

int pnp_manual_config_dev(struct pnp_dev *dev, struct pnp_resource_table * res, int mode)
{
	int i;
	struct pnp_resource_table * bak;
	if (!dev || !res)
		return -EINVAL;
	if (dev->active)
		return -EBUSY;
	bak = pnp_alloc(sizeof(struct pnp_resource_table));
	if (!bak)
		return -ENOMEM;
	*bak = dev->res;

	spin_lock(&pnp_lock);
	pnp_process_manual_resources(&dev->res, res);
	if (!(mode & PNP_CONFIG_FORCE)) {
		for (i = 0; i < PNP_MAX_PORT; i++) {
			if(pnp_check_port(dev,i))
				goto fail;
		}
		for (i = 0; i < PNP_MAX_MEM; i++) {
			if(pnp_check_mem(dev,i))
				goto fail;
		}
		for (i = 0; i < PNP_MAX_IRQ; i++) {
			if(pnp_check_irq(dev,i))
				goto fail;
		}
		for (i = 0; i < PNP_MAX_DMA; i++) {
			if(pnp_check_dma(dev,i))
				goto fail;
		}
	}
	dev->config_mode = PNP_CONFIG_MANUAL;
	spin_unlock(&pnp_lock);

	pnp_resolve_conflicts(dev);
	kfree(bak);
	return 0;

fail:
	dev->res = *bak;
	spin_unlock(&pnp_lock);
	kfree(bak);
	return -EINVAL;
}

/**
 * pnp_activate_dev - activates a PnP device for use
 * @dev: pointer to the desired device
 *
 * finds the best resource configuration and then informs the correct pnp protocol
 */

int pnp_activate_dev(struct pnp_dev *dev)
{
	if (!dev)
		return -EINVAL;
	if (dev->active) {
		return 0; /* the device is already active */
	}
	/* If this condition is true, advanced configuration failed, we need to get this device up and running
	 * so we use the simple config engine which ignores cold conflicts, this of course may lead to new failures */
	if (!pnp_is_active(dev)) {
		if (!pnp_simple_config(dev)) {
			pnp_err("res: Unable to resolve resource conflicts for the device '%s'.", dev->dev.bus_id);
			goto fail;
		}
	}

	spin_lock(&pnp_lock);	/* we lock just in case the device is being configured during this call */
	dev->active = 1;
	spin_unlock(&pnp_lock); /* once the device is claimed active we know it won't be configured so we can unlock */

	if (dev->config_mode & PNP_CONFIG_INVALID) {
		pnp_info("res: Unable to activate the PnP device '%s' because its resource configuration is invalid.", dev->dev.bus_id);
		goto fail;
	}
	if (dev->status != PNP_READY && dev->status != PNP_ATTACHED){
		pnp_err("res: Activation failed because the PnP device '%s' is busy.", dev->dev.bus_id);
		goto fail;
	}
	if (!pnp_can_write(dev)) {
		pnp_info("res: Unable to activate the PnP device '%s' because this feature is not supported.", dev->dev.bus_id);
		goto fail;
	}
	if (dev->protocol->set(dev, &dev->res)<0) {
		pnp_err("res: The protocol '%s' reports that activating the PnP device '%s' has failed.", dev->protocol->name, dev->dev.bus_id);
		goto fail;
	}
	if (pnp_can_read(dev)) {
		struct pnp_resource_table * res = pnp_alloc(sizeof(struct pnp_resource_table));
		if (!res)
			goto fail;
		dev->protocol->get(dev, res);
		if (pnp_compare_resources(&dev->res, res)) /* if this happens we may be in big trouble but it's best just to continue */
			pnp_err("res: The resources requested do not match those set for the PnP device '%s'.", dev->dev.bus_id);
		kfree(res);
	} else
		dev->active = pnp_is_active(dev);
	pnp_dbg("res: the device '%s' has been activated.", dev->dev.bus_id);
	if (dev->rule) {
		kfree(dev->rule);
		dev->rule = NULL;
	}
	return 0;

fail:
	dev->active = 0; /* fixes incorrect active state */
	return -EINVAL;
}

/**
 * pnp_disable_dev - disables device
 * @dev: pointer to the desired device
 *
 * inform the correct pnp protocol so that resources can be used by other devices
 */

int pnp_disable_dev(struct pnp_dev *dev)
{
        if (!dev)
                return -EINVAL;
	if (!dev->active) {
		return 0; /* the device is already disabled */
	}
	if (dev->status != PNP_READY){
		pnp_info("res: Disable failed becuase the PnP device '%s' is busy.", dev->dev.bus_id);
		return -EINVAL;
	}
	if (!pnp_can_disable(dev)) {
		pnp_info("res: Unable to disable the PnP device '%s' because this feature is not supported.", dev->dev.bus_id);
		return -EINVAL;
	}
	if (dev->protocol->disable(dev)<0) {
		pnp_err("res: The protocol '%s' reports that disabling the PnP device '%s' has failed.", dev->protocol->name, dev->dev.bus_id);
		return -1;
	}
	dev->active = 0; /* just in case the protocol doesn't do this */
	pnp_dbg("res: the device '%s' has been disabled.", dev->dev.bus_id);
	return 0;
}

/**
 * pnp_resource_change - change one resource
 * @resource: pointer to resource to be changed
 * @start: start of region
 * @size: size of region
 *
 */

void pnp_resource_change(struct resource *resource, unsigned long start, unsigned long size)
{
	if (resource == NULL)
		return;
	resource->flags &= ~IORESOURCE_AUTO;
	resource->start = start;
	resource->end = start + size - 1;
}


EXPORT_SYMBOL(pnp_auto_config_dev);
EXPORT_SYMBOL(pnp_manual_config_dev);
EXPORT_SYMBOL(pnp_activate_dev);
EXPORT_SYMBOL(pnp_disable_dev);
EXPORT_SYMBOL(pnp_resource_change);


/* format is: pnp_max_moves=num */

static int __init pnp_setup_max_moves(char *str)
{
	get_option(&str,&pnp_max_moves);
	return 1;
}

__setup("pnp_max_moves=", pnp_setup_max_moves);
