/*
 * resource.c - Contains functions for registering and analyzing resource information
 *
 * based on isapnp.c resource management (c) Jaroslav Kysela <perex@suse.cz>
 * Copyright 2003 Adam Belay <ambx1@neo.rr.com>
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <linux/ioport.h>
#include <linux/init.h>

#include <linux/pnp.h>
#include "base.h"

int pnp_allow_dma0 = -1;		        /* allow dma 0 during auto activation: -1=off (:default), 0=off (set by user), 1=on */
int pnp_skip_pci_scan;				/* skip PCI resource scanning */
int pnp_reserve_irq[16] = { [0 ... 15] = -1 };	/* reserve (don't use) some IRQ */
int pnp_reserve_dma[8] = { [0 ... 7] = -1 };	/* reserve (don't use) some DMA */
int pnp_reserve_io[16] = { [0 ... 15] = -1 };	/* reserve (don't use) some I/O region */
int pnp_reserve_mem[16] = { [0 ... 15] = -1 };	/* reserve (don't use) some memory region */


/*
 * possible resource registration
 */

struct pnp_resources * pnp_build_resource(struct pnp_dev *dev, int dependent)
{
	struct pnp_resources *res, *ptr, *ptra;

	res = pnp_alloc(sizeof(struct pnp_resources));
	if (!res)
		return NULL;
	ptr = dev->possible;
	if (ptr) { /* add to another list */
		ptra = ptr->dep;
		while (ptra && ptra->dep)
			ptra = ptra->dep;
		if (!ptra)
			ptr->dep = res;
		else
			ptra->dep = res;
	} else
		dev->possible = res;
	if (dependent) {
		res->priority = dependent & 0xff;
		if (res->priority > PNP_RES_PRIORITY_FUNCTIONAL)
			res->priority = PNP_RES_PRIORITY_INVALID;
	} else
		res->priority = PNP_RES_PRIORITY_PREFERRED;
	return res;
}

struct pnp_resources * pnp_find_resources(struct pnp_dev *dev, int depnum)
{
	int i;
	struct pnp_resources *res;
	if (!dev)
		return NULL;
	res = dev->possible;
	if (!res)
		return NULL;
	for (i = 0; i < depnum; i++)
	{
		if (res->dep)
			res = res->dep;
		else
			return NULL;
	}
	return res;
}

int pnp_get_max_depnum(struct pnp_dev *dev)
{
	int num = 0;
	struct pnp_resources *res;
	if (!dev)
		return -EINVAL;
	res = dev->possible;
	if (!res)
		return -EINVAL;
	while (res->dep){
		res = res->dep;
		num++;
	}
	return num;
}

int pnp_add_irq_resource(struct pnp_dev *dev, int depnum, struct pnp_irq *data)
{
	int i;
	struct pnp_resources *res;
	struct pnp_irq *ptr;
	res = pnp_find_resources(dev,depnum);
	if (!res)
		return -EINVAL;
	if (!data)
		return -EINVAL;
	ptr = res->irq;
	while (ptr && ptr->next)
		ptr = ptr->next;
	if (ptr)
		ptr->next = data;
	else
		res->irq = data;
#ifdef CONFIG_PCI
	for (i=0; i<16; i++)
		if (data->map & (1<<i))
			pcibios_penalize_isa_irq(i);
#endif
	return 0;
}

int pnp_add_dma_resource(struct pnp_dev *dev, int depnum, struct pnp_dma *data)
{
	struct pnp_resources *res;
	struct pnp_dma *ptr;
	res = pnp_find_resources(dev,depnum);
	if (!res)
		return -EINVAL;
	if (!data)
		return -EINVAL;
	ptr = res->dma;
	while (ptr && ptr->next)
		ptr = ptr->next;
	if (ptr)
		ptr->next = data;
	else
		res->dma = data;
	return 0;
}

int pnp_add_port_resource(struct pnp_dev *dev, int depnum, struct pnp_port *data)
{
	struct pnp_resources *res;
	struct pnp_port *ptr;
	res = pnp_find_resources(dev,depnum);
	if (!res)
		return -EINVAL;
	if (!data)
		return -EINVAL;
	ptr = res->port;
	while (ptr && ptr->next)
		ptr = ptr->next;
	if (ptr)
		ptr->next = data;
	else
		res->port = data;
	return 0;
}

int pnp_add_mem_resource(struct pnp_dev *dev, int depnum, struct pnp_mem *data)
{
	struct pnp_resources *res;
	struct pnp_mem *ptr;
	res = pnp_find_resources(dev,depnum);
	if (!res)
		return -EINVAL;
	if (!data)
		return -EINVAL;
	ptr = res->mem;
	while (ptr && ptr->next)
		ptr = ptr->next;
	if (ptr)
		ptr->next = data;
	else
		res->mem = data;
	return 0;
}

static void pnp_free_port(struct pnp_port *port)
{
	struct pnp_port *next;

	while (port) {
		next = port->next;
		kfree(port);
		port = next;
	}
}

static void pnp_free_irq(struct pnp_irq *irq)
{
	struct pnp_irq *next;

	while (irq) {
		next = irq->next;
		kfree(irq);
		irq = next;
	}
}

static void pnp_free_dma(struct pnp_dma *dma)
{
	struct pnp_dma *next;

	while (dma) {
		next = dma->next;
		kfree(dma);
		dma = next;
	}
}

static void pnp_free_mem(struct pnp_mem *mem)
{
	struct pnp_mem *next;

	while (mem) {
		next = mem->next;
		kfree(mem);
		mem = next;
	}
}

void pnp_free_resources(struct pnp_resources *resources)
{
	struct pnp_resources *next;

	while (resources) {
		next = resources->dep;
		pnp_free_port(resources->port);
		pnp_free_irq(resources->irq);
		pnp_free_dma(resources->dma);
		pnp_free_mem(resources->mem);
		kfree(resources);
		resources = next;
	}
}


/*
 * resource validity checking
 */

#define length(start, end) (*(end) - *(start) + 1)

/* ranged_conflict - used to determine if two resource ranges conflict
 * condition 1: check if the start of a is within b
 * condition 2: check if the end of a is within b
 * condition 3: check if b is engulfed by a */

#define ranged_conflict(starta, enda, startb, endb) \
((*(starta) >= *(startb) && *(starta) <= *(endb)) || \
 (*(enda) >= *(startb) && *(enda) <= *(endb)) || \
 (*(starta) < *(startb) && *(enda) > *(endb)))

struct pnp_dev * pnp_check_port_conflicts(struct pnp_dev * dev, int idx, int mode)
{
	int tmp;
	unsigned long *port, *end, *tport, *tend;
	struct pnp_dev *tdev;
	port = &dev->res.port_resource[idx].start;
	end = &dev->res.port_resource[idx].end;

	/* if the resource doesn't exist, don't complain about it */
	if (dev->res.port_resource[idx].start == 0)
		return NULL;

	/* check for cold conflicts */
	pnp_for_each_dev(tdev) {
		/* Is the device configurable? */
		if (tdev == dev || (mode ? !tdev->active : tdev->active))
			continue;
		for (tmp = 0; tmp < PNP_MAX_PORT; tmp++) {
			if (tdev->res.port_resource[tmp].flags & IORESOURCE_IO) {
				tport = &tdev->res.port_resource[tmp].start;
				tend = &tdev->res.port_resource[tmp].end;
				if (ranged_conflict(port,end,tport,tend))
					return tdev;
			}
		}
	}
	return NULL;
}

int pnp_check_port(struct pnp_dev * dev, int idx)
{
	int tmp;
	unsigned long *port, *end, *tport, *tend;
	port = &dev->res.port_resource[idx].start;
	end = &dev->res.port_resource[idx].end;

	/* if the resource doesn't exist, don't complain about it */
	if (dev->res.port_resource[idx].start == 0)
		return 0;

	/* check if the resource is already in use, skip if the device is active because it itself may be in use */
	if(!dev->active) {
		if (check_region(*port, length(port,end)))
			return CONFLICT_TYPE_IN_USE;
	}

	/* check if the resource is reserved */
	for (tmp = 0; tmp < 8; tmp++) {
		int rport = pnp_reserve_io[tmp << 1];
		int rend = pnp_reserve_io[(tmp << 1) + 1] + rport - 1;
		if (ranged_conflict(port,end,&rport,&rend))
			return CONFLICT_TYPE_RESERVED;
	}

	/* check for internal conflicts */
	for (tmp = 0; tmp < PNP_MAX_PORT && tmp != idx; tmp++) {
		if (dev->res.port_resource[tmp].flags & IORESOURCE_IO) {
			tport = &dev->res.port_resource[tmp].start;
			tend = &dev->res.port_resource[tmp].end;
			if (ranged_conflict(port,end,tport,tend))
				return CONFLICT_TYPE_INTERNAL;
		}
	}

	/* check for warm conflicts */
	if (pnp_check_port_conflicts(dev, idx, SEARCH_WARM))
		return CONFLICT_TYPE_PNP_WARM;

	return 0;
}

struct pnp_dev * pnp_check_mem_conflicts(struct pnp_dev * dev, int idx, int mode)
{
	int tmp;
	unsigned long *addr, *end, *taddr, *tend;
	struct pnp_dev *tdev;
	addr = &dev->res.mem_resource[idx].start;
	end = &dev->res.mem_resource[idx].end;

	/* if the resource doesn't exist, don't complain about it */
	if (dev->res.mem_resource[idx].start == 0)
		return NULL;

	/* check for cold conflicts */
	pnp_for_each_dev(tdev) {
		/* Is the device configurable? */
		if (tdev == dev || (mode ? !tdev->active : tdev->active))
			continue;
		for (tmp = 0; tmp < PNP_MAX_MEM; tmp++) {
			if (tdev->res.mem_resource[tmp].flags & IORESOURCE_MEM) {
				taddr = &tdev->res.mem_resource[tmp].start;
				tend = &tdev->res.mem_resource[tmp].end;
				if (ranged_conflict(addr,end,taddr,tend))
					return tdev;
			}
		}
	}
	return NULL;
}

int pnp_check_mem(struct pnp_dev * dev, int idx)
{
	int tmp;
	unsigned long *addr, *end, *taddr, *tend;
	addr = &dev->res.mem_resource[idx].start;
	end = &dev->res.mem_resource[idx].end;

	/* if the resource doesn't exist, don't complain about it */
	if (dev->res.mem_resource[idx].start == 0)
		return 0;

	/* check if the resource is already in use, skip if the device is active because it itself may be in use */
	if(!dev->active) {
		if (__check_region(&iomem_resource, *addr, length(addr,end)))
			return CONFLICT_TYPE_IN_USE;
	}

	/* check if the resource is reserved */
	for (tmp = 0; tmp < 8; tmp++) {
		int raddr = pnp_reserve_mem[tmp << 1];
		int rend = pnp_reserve_mem[(tmp << 1) + 1] + raddr - 1;
		if (ranged_conflict(addr,end,&raddr,&rend))
			return CONFLICT_TYPE_RESERVED;
	}

	/* check for internal conflicts */
	for (tmp = 0; tmp < PNP_MAX_MEM && tmp != idx; tmp++) {
		if (dev->res.mem_resource[tmp].flags & IORESOURCE_MEM) {
			taddr = &dev->res.mem_resource[tmp].start;
			tend = &dev->res.mem_resource[tmp].end;
			if (ranged_conflict(addr,end,taddr,tend))
				return CONFLICT_TYPE_INTERNAL;
		}
	}

	/* check for warm conflicts */
	if (pnp_check_mem_conflicts(dev, idx, SEARCH_WARM))
		return CONFLICT_TYPE_PNP_WARM;

	return 0;
}

struct pnp_dev * pnp_check_irq_conflicts(struct pnp_dev * dev, int idx, int mode)
{
	int tmp;
	struct pnp_dev * tdev;
	unsigned long * irq = &dev->res.irq_resource[idx].start;

	/* if the resource doesn't exist, don't complain about it */
	if (dev->res.irq_resource[idx].start == -1)
		return NULL;

	/* check for cold conflicts */
	pnp_for_each_dev(tdev) {
		/* Is the device configurable? */
		if (tdev == dev || (mode ? !tdev->active : tdev->active))
			continue;
		for (tmp = 0; tmp < PNP_MAX_IRQ; tmp++) {
			if (tdev->res.irq_resource[tmp].flags & IORESOURCE_IRQ) {
				if ((tdev->res.irq_resource[tmp].start == *irq))
					return tdev;
			}
		}
	}
	return NULL;
}

static irqreturn_t pnp_test_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	return IRQ_HANDLED;
}

int pnp_check_irq(struct pnp_dev * dev, int idx)
{
	int tmp;
	unsigned long * irq = &dev->res.irq_resource[idx].start;

	/* if the resource doesn't exist, don't complain about it */
	if (dev->res.irq_resource[idx].start == -1)
		return 0;

	/* check if the resource is valid */
	if (*irq < 0 || *irq > 15)
		return CONFLICT_TYPE_INVALID;

	/* check if the resource is reserved */
	for (tmp = 0; tmp < 16; tmp++) {
		if (pnp_reserve_irq[tmp] == *irq)
			return CONFLICT_TYPE_RESERVED;
	}

	/* check for internal conflicts */
	for (tmp = 0; tmp < PNP_MAX_IRQ && tmp != idx; tmp++) {
		if (dev->res.irq_resource[tmp].flags & IORESOURCE_IRQ) {
			if (dev->res.irq_resource[tmp].start == *irq)
				return CONFLICT_TYPE_INTERNAL;
		}
	}

#ifdef CONFIG_PCI
	/* check if the resource is being used by a pci device */
	if (!pnp_skip_pci_scan) {
		struct pci_dev * pci = NULL;
		while ((pci = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, pci)) != NULL) {
			if (pci->irq == *irq)
				return CONFLICT_TYPE_PCI;
		}
	}
#endif

	/* check if the resource is already in use, skip if the device is active because it itself may be in use */
	if(!dev->active) {
		if (request_irq(*irq, pnp_test_handler, SA_INTERRUPT, "pnp", NULL))
			return CONFLICT_TYPE_IN_USE;
		free_irq(*irq, NULL);
	}

	/* check for warm conflicts */
	if (pnp_check_irq_conflicts(dev, idx, SEARCH_WARM))
		return CONFLICT_TYPE_PNP_WARM;

	return 0;
}


struct pnp_dev * pnp_check_dma_conflicts(struct pnp_dev * dev, int idx, int mode)
{
	int tmp;
	struct pnp_dev * tdev;
	unsigned long * dma = &dev->res.dma_resource[idx].start;

	/* if the resource doesn't exist, don't complain about it */
	if (dev->res.dma_resource[idx].start == -1)
		return NULL;

	/* check for cold conflicts */
	pnp_for_each_dev(tdev) {
		/* Is the device configurable? */
		if (tdev == dev || (mode ? !tdev->active : tdev->active))
			continue;
		for (tmp = 0; tmp < PNP_MAX_DMA; tmp++) {
			if (tdev->res.dma_resource[tmp].flags & IORESOURCE_DMA) {
				if ((tdev->res.dma_resource[tmp].start == *dma))
					return tdev;
			}
		}
	}
	return NULL;
}

int pnp_check_dma(struct pnp_dev * dev, int idx)
{
	int tmp, mindma = 1;
	unsigned long * dma = &dev->res.dma_resource[idx].start;

	/* if the resource doesn't exist, don't complain about it */
	if (dev->res.dma_resource[idx].start == -1)
		return 0;

	/* check if the resource is valid */
	if (pnp_allow_dma0 == 1)
		mindma = 0;
	if (*dma < mindma || *dma == 4 || *dma > 7)
		return CONFLICT_TYPE_INVALID;

	/* check if the resource is reserved */
	for (tmp = 0; tmp < 8; tmp++) {
		if (pnp_reserve_dma[tmp] == *dma)
			return CONFLICT_TYPE_RESERVED;
	}

	/* check for internal conflicts */
	for (tmp = 0; tmp < PNP_MAX_DMA && tmp != idx; tmp++) {
		if (dev->res.dma_resource[tmp].flags & IORESOURCE_DMA) {
			if (dev->res.dma_resource[tmp].start == *dma)
				return CONFLICT_TYPE_INTERNAL;
		}
	}

	/* check if the resource is already in use, skip if the device is active because it itself may be in use */
	if(!dev->active) {
		if (request_dma(*dma, "pnp"))
			return CONFLICT_TYPE_IN_USE;
		free_dma(*dma);
	}

	/* check for warm conflicts */
	if (pnp_check_dma_conflicts(dev, idx, SEARCH_WARM))
		return CONFLICT_TYPE_PNP_WARM;
	return 0;
}


/**
 * pnp_init_resource_table - Resets a resource table to default values.
 * @table: pointer to the desired resource table
 *
 */

void pnp_init_resource_table(struct pnp_resource_table *table)
{
	int idx;
	for (idx = 0; idx < PNP_MAX_IRQ; idx++) {
		table->irq_resource[idx].name = NULL;
		table->irq_resource[idx].start = -1;
		table->irq_resource[idx].end = -1;
		table->irq_resource[idx].flags = IORESOURCE_AUTO;
	}
	for (idx = 0; idx < PNP_MAX_DMA; idx++) {
		table->dma_resource[idx].name = NULL;
		table->dma_resource[idx].start = -1;
		table->dma_resource[idx].end = -1;
		table->dma_resource[idx].flags = IORESOURCE_AUTO;
	}
	for (idx = 0; idx < PNP_MAX_PORT; idx++) {
		table->port_resource[idx].name = NULL;
		table->port_resource[idx].start = 0;
		table->port_resource[idx].end = 0;
		table->port_resource[idx].flags = IORESOURCE_AUTO;
	}
	for (idx = 0; idx < PNP_MAX_MEM; idx++) {
		table->mem_resource[idx].name = NULL;
		table->mem_resource[idx].start = 0;
		table->mem_resource[idx].end = 0;
		table->mem_resource[idx].flags = IORESOURCE_AUTO;
	}
}


/**
 * pnp_generate_rule - Creates a rule table structure based on depnum and device.
 * @dev: pointer to the desired device
 * @depnum: dependent function, if not valid will return an error
 * @rule: pointer to a rule structure to record data to
 *
 */

int pnp_generate_rule(struct pnp_dev * dev, int depnum, struct pnp_rule_table * rule)
{
	int nport = 0, nirq = 0, ndma = 0, nmem = 0;
	struct pnp_resources * res;
	struct pnp_port * port;
	struct pnp_mem * mem;
	struct pnp_irq * irq;
	struct pnp_dma * dma;

	if (depnum < 0 || !rule)
		return -EINVAL;

	/* independent */
	res = pnp_find_resources(dev, 0);
	if (!res)
		return -ENODEV;
	port = res->port;
	mem = res->mem;
	irq = res->irq;
	dma = res->dma;
	while (port){
		rule->port[nport] = port;
		nport++;
		port = port->next;
	}
	while (mem){
		rule->mem[nmem] = mem;
		nmem++;
		mem = mem->next;
	}
	while (irq){
		rule->irq[nirq] = irq;
		nirq++;
		irq = irq->next;
	}
	while (dma){
		rule->dma[ndma] = dma;
		ndma++;
		dma = dma->next;
	}

	/* dependent */
	if (depnum == 0)
		return 1;
	res = pnp_find_resources(dev, depnum);
	if (!res)
		return -ENODEV;
	port = res->port;
	mem = res->mem;
	irq = res->irq;
	dma = res->dma;
	while (port){
		rule->port[nport] = port;
		nport++;
		port = port->next;
	}
	while (mem){
		rule->mem[nmem] = mem;
		nmem++;
		mem = mem->next;
	}

	while (irq){
		rule->irq[nirq] = irq;
		nirq++;
		irq = irq->next;
	}
	while (dma){
		rule->dma[ndma] = dma;
		ndma++;
		dma = dma->next;
	}

	/* clear the remaining values */
	for (; nport < PNP_MAX_PORT; nport++)
		rule->port[nport] = NULL;
	for (; nmem < PNP_MAX_MEM; nmem++)
		rule->mem[nmem] = NULL;
	for (; nirq < PNP_MAX_IRQ; nirq++)
		rule->irq[nirq] = NULL;
	for (; ndma < PNP_MAX_DMA; ndma++)
		rule->dma[ndma] = NULL;
	return 1;
}


EXPORT_SYMBOL(pnp_build_resource);
EXPORT_SYMBOL(pnp_find_resources);
EXPORT_SYMBOL(pnp_get_max_depnum);
EXPORT_SYMBOL(pnp_add_irq_resource);
EXPORT_SYMBOL(pnp_add_dma_resource);
EXPORT_SYMBOL(pnp_add_port_resource);
EXPORT_SYMBOL(pnp_add_mem_resource);
EXPORT_SYMBOL(pnp_init_resource_table);
EXPORT_SYMBOL(pnp_generate_rule);


/* format is: allowdma0 */

static int __init pnp_allowdma0(char *str)
{
        pnp_allow_dma0 = 1;
	return 1;
}

__setup("allowdma0", pnp_allowdma0);

/* format is: pnp_reserve_irq=irq1[,irq2] .... */

static int __init pnp_setup_reserve_irq(char *str)
{
	int i;

	for (i = 0; i < 16; i++)
		if (get_option(&str,&pnp_reserve_irq[i]) != 2)
			break;
	return 1;
}

__setup("pnp_reserve_irq=", pnp_setup_reserve_irq);

/* format is: pnp_reserve_dma=dma1[,dma2] .... */

static int __init pnp_setup_reserve_dma(char *str)
{
	int i;

	for (i = 0; i < 8; i++)
		if (get_option(&str,&pnp_reserve_dma[i]) != 2)
			break;
	return 1;
}

__setup("pnp_reserve_dma=", pnp_setup_reserve_dma);

/* format is: pnp_reserve_io=io1,size1[,io2,size2] .... */

static int __init pnp_setup_reserve_io(char *str)
{
	int i;

	for (i = 0; i < 16; i++)
		if (get_option(&str,&pnp_reserve_io[i]) != 2)
			break;
	return 1;
}

__setup("pnp_reserve_io=", pnp_setup_reserve_io);

/* format is: pnp_reserve_mem=mem1,size1[,mem2,size2] .... */

static int __init pnp_setup_reserve_mem(char *str)
{
	int i;

	for (i = 0; i < 16; i++)
		if (get_option(&str,&pnp_reserve_mem[i]) != 2)
			break;
	return 1;
}

__setup("pnp_reserve_mem=", pnp_setup_reserve_mem);
