/*
 * resource.c - contains resource management algorithms
 *
 * based on isapnp.c resource management (c) Jaroslav Kysela <perex@suse.cz>
 * Copyright 2002 Adam Belay <ambx1@neo.rr.com>
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
#include <linux/config.h>
#include <linux/init.h>

#ifdef CONFIG_PNP_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif

#include <linux/pnp.h>
#include "base.h"

int pnp_allow_dma0 = -1;		        /* allow dma 0 during auto activation: -1=off (:default), 0=off (set by user), 1=on */
int pnp_skip_pci_scan;				/* skip PCI resource scanning */
int pnp_reserve_irq[16] = { [0 ... 15] = -1 };	/* reserve (don't use) some IRQ */
int pnp_reserve_dma[8] = { [0 ... 7] = -1 };	/* reserve (don't use) some DMA */
int pnp_reserve_io[16] = { [0 ... 15] = -1 };	/* reserve (don't use) some I/O region */
int pnp_reserve_mem[16] = { [0 ... 15] = -1 };	/* reserve (don't use) some memory region */


/* resource information adding functions */

struct pnp_resources * pnp_build_resource(struct pnp_dev *dev, int dependent)
{
	struct pnp_resources *res, *ptr, *ptra;

	res = pnp_alloc(sizeof(struct pnp_resources));
	if (!res)
		return NULL;
	ptr = dev->res;
	if (ptr && ptr->dependent && dependent) { /* add to another list */
		ptra = ptr->dep;
		while (ptra && ptra->dep)
			ptra = ptra->dep;
		if (!ptra)
			ptr->dep = res;
		else
			ptra->dep = res;
	} else {
		if (!ptr){
			dev->res = res;
		}
		else{
			kfree(res);
			return NULL;
		}
	}
	if (dependent) {
		res->priority = dependent & 0xff;
		if (res->priority > PNP_RES_PRIORITY_FUNCTIONAL)
			res->priority = PNP_RES_PRIORITY_INVALID;
		res->dependent = 1;
	} else {
		res->priority = PNP_RES_PRIORITY_PREFERRED;
		res->dependent = 1;
	}
	return res;
}

struct pnp_resources * pnp_find_resources(struct pnp_dev *dev, int depnum)
{
	int i;
	struct pnp_resources *res;
	if (!dev)
		return NULL;
	res = dev->res;
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
	res = dev->res;
	if (!res)
		return -EINVAL;
	while (res->dep){
		res = res->dep;
		num++;
	}
	return num;
}

/*
 *  Add IRQ resource to resources list.
 */

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

/*
 *  Add DMA resource to resources list.
 */

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

/*
 *  Add port resource to resources list.
 */

int pnp_add_port_resource(struct pnp_dev *dev, int depnum, struct pnp_port *data)
{
	struct pnp_resources *res;
	struct pnp_port *ptr;
	res = pnp_find_resources(dev,depnum);
	if (res==NULL)
		return -EINVAL;
	if (!data)
		return -EINVAL;
	data->res = res;
	ptr = res->port;
	while (ptr && ptr->next)
		ptr = ptr->next;
	if (ptr)
		ptr->next = data;
	else
		res->port = data;
	return 0;
}

/*
 *  Add memory resource to resources list.
 */

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

/*
 *  Add 32-bit memory resource to resources list.
 */

int pnp_add_mem32_resource(struct pnp_dev *dev, int depnum, struct pnp_mem32 *data)
{
	struct pnp_resources *res;
	struct pnp_mem32 *ptr;
	res = pnp_find_resources(dev,depnum);
	if (!res)
		return -EINVAL;
	if (!data)
		return -EINVAL;
	ptr = res->mem32;
	while (ptr && ptr->next)
		ptr = ptr->next;
	if (ptr)
		ptr->next = data;
	else
		res->mem32 = data;
	return 0;
}


/* resource removing functions */

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

static void pnp_free_mem32(struct pnp_mem32 *mem32)
{
	struct pnp_mem32 *next;

	while (mem32) {
		next = mem32->next;
		kfree(mem32);
		mem32 = next;
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
		pnp_free_mem32(resources->mem32);
		kfree(resources);
		resources = next;
	}
}


/* resource validity checking functions */

static int pnp_check_port(int port, int size, int idx, struct pnp_cfg *config)
{
	int i, tmp, rport, rsize;
	struct pnp_dev *dev;

	if (check_region(port, size))
		return 1;
	for (i = 0; i < 8; i++) {
		rport = pnp_reserve_io[i << 1];
		rsize = pnp_reserve_io[(i << 1) + 1];
		if (port >= rport && port < rport + rsize)
			return 1;
		if (port + size > rport && port + size < (rport + rsize) - 1)
			return 1;
	}

	pnp_for_each_dev(dev) {
		if (dev->active) {
			for (tmp = 0; tmp < 8; tmp++) {
				if (pnp_port_valid(dev, tmp)) {
					rport = pnp_port_start(dev, tmp);
					rsize = pnp_port_len(dev, tmp);
					if (port >= rport && port < rport + rsize)
						return 1;
					if (port + size > rport && port + size < (rport + rsize) - 1)
						return 1;
				}
			}
		}
	}
	for (tmp = 0; tmp < 8 && tmp != idx; tmp++) {
		if (pnp_port_valid(dev, tmp) &&
		    pnp_flags_valid(&config->request.io_resource[tmp])) {
			rport = config->request.io_resource[tmp].start;
			rsize = (config->request.io_resource[tmp].end - rport) + 1;
			if (port >= rport && port < rport + rsize)
				return 1;
			if (port + size > rport && port + size < (rport + rsize) - 1)
				return 1;
		}
	}
	return 0;
}

static int pnp_check_mem(unsigned int addr, unsigned int size, int idx, struct pnp_cfg *config)
{
	int i, tmp;
	unsigned int raddr, rsize;
	struct pnp_dev *dev;

	for (i = 0; i < 8; i++) {
		raddr = (unsigned int)pnp_reserve_mem[i << 1];
		rsize = (unsigned int)pnp_reserve_mem[(i << 1) + 1];
		if (addr >= raddr && addr < raddr + rsize)
			return 1;
		if (addr + size > raddr && addr + size < (raddr + rsize) - 1)
			return 1;
		if (__check_region(&iomem_resource, addr, size))
			return 1;
	}
	pnp_for_each_dev(dev) {
		if (dev->active) {
			for (tmp = 0; tmp < 4; tmp++) {
				if (pnp_mem_valid(dev, tmp)) {
					raddr = pnp_mem_start(dev, tmp);
					rsize = pnp_mem_len(dev, tmp);
					if (addr >= raddr && addr < raddr + rsize)
						return 1;
					if (addr + size > raddr && addr + size < (raddr + rsize) - 1)
						return 1;
				}
			}
		}
	}
	for (tmp = 0; tmp < 4 && tmp != idx; tmp++) {
		if (pnp_mem_valid(dev, tmp) &&
		    pnp_flags_valid(&config->request.mem_resource[tmp])) {
			raddr = config->request.mem_resource[tmp].start;
			rsize = (config->request.mem_resource[tmp].end - raddr) + 1;
			if (addr >= raddr && addr < raddr + rsize)
				return 1;
			if (addr + size > raddr && addr + size < (raddr + rsize) - 1)
				return 1;
		}
	}
	return 0;
}

static void pnp_test_handler(int irq, void *dev_id, struct pt_regs *regs)
{
}

static int pnp_check_interrupt(int irq, struct pnp_cfg *config)
{
	int i;
#ifdef CONFIG_PCI
	struct pci_dev *pci;
#endif
	struct pnp_dev *dev;
	if (!config)
		return 1;

	if (irq < 0 || irq > 15)
		return 1;
	for (i = 0; i < 16; i++) {
		if (pnp_reserve_irq[i] == irq)
			return 1;
	}
	pnp_for_each_dev(dev) {
		if (dev->active) {
			if ((pnp_irq_valid(dev, 0) && dev->irq_resource[0].start == irq) ||
			    (pnp_irq_valid(dev, 1) && dev->irq_resource[1].start == irq))
				return 1;
		}
	}
	if (pnp_flags_valid(&config->request.irq_resource[0]) &&
	    pnp_flags_valid(&config->request.irq_resource[1]) &&
	    (config->request.irq_resource[0].start == irq))
		return 1;
#ifdef CONFIG_PCI
	if (!pnp_skip_pci_scan) {
		pci_for_each_dev(pci) {
			if (pci->irq == irq)
				return 1;
		}
	}
#endif
	if (request_irq(irq, pnp_test_handler, SA_INTERRUPT, "pnp", NULL))
		return 1;
	free_irq(irq, NULL);
	return 0;
}

static int pnp_check_dma(int dma, struct pnp_cfg *config)
{
	int i, mindma = 1;
	struct pnp_dev *dev;
	if (!config)
		return 1;

	if (pnp_allow_dma0 == 1)
		mindma = 0;
	if (dma < mindma || dma == 4 || dma > 7)
		return 1;
	for (i = 0; i < 8; i++) {
		if (pnp_reserve_dma[i] == dma)
			return 1;
	}
	pnp_for_each_dev(dev) {
		if (dev->active) {
			if ((pnp_dma_valid(dev, 0) && pnp_dma(dev, 0) == dma) ||
			    (pnp_dma_valid(dev, 1) && pnp_dma(dev, 1) == dma))
				return 1;
		}
	}
	if (pnp_flags_valid(&config->request.dma_resource[0]) &&
	    pnp_flags_valid(&config->request.dma_resource[1]) &&
	    (config->request.dma_resource[0].start == dma))
		return 1;
	if (request_dma(dma, "pnp"))
		return 1;
	free_dma(dma);
	return 0;
}


/* config generation functions */
static int pnp_generate_port(struct pnp_cfg *config, int num)
{
	struct pnp_port *port;
	unsigned long *value1, *value2, *value3;
	if (!config || num < 0 || num > 7)
		return -EINVAL;
	port = config->port[num];
	if (!port)
		return 0;
	value1 = &config->request.io_resource[num].start;
	value2 = &config->request.io_resource[num].end;
	value3 = &config->request.io_resource[num].flags;
	*value1 = port->min;
	*value2 = *value1 + port->size - 1;
	*value3 = port->flags | IORESOURCE_IO;
	while (pnp_check_port(*value1, port->size, num, config)) {
		*value1 += port->align;
		*value2 = *value1 + port->size - 1;
		if (*value1 > port->max || !port->align)
			return -ENOENT;
	}
	return 0;
}

static int pnp_generate_mem(struct pnp_cfg *config, int num)
{
	struct pnp_mem *mem;
	unsigned long *value1, *value2, *value3;
	if (!config || num < 0 || num > 3)
		return -EINVAL;
	mem = config->mem[num];
	if (!mem)
		return 0;
	value1 = &config->request.mem_resource[num].start;
	value2 = &config->request.mem_resource[num].end;
	value3 = &config->request.mem_resource[num].flags;
	*value1 = mem->min;
	*value2 = *value1 + mem->size - 1;
	*value3 = mem->flags | IORESOURCE_MEM;
	if (!(mem->flags & IORESOURCE_MEM_WRITEABLE))
		*value3 |= IORESOURCE_READONLY;
	if (mem->flags & IORESOURCE_MEM_CACHEABLE)
		*value3 |= IORESOURCE_CACHEABLE;
	if (mem->flags & IORESOURCE_MEM_RANGELENGTH)
		*value3 |= IORESOURCE_RANGELENGTH;
	if (mem->flags & IORESOURCE_MEM_SHADOWABLE)
		*value3 |= IORESOURCE_SHADOWABLE;
	while (pnp_check_mem(*value1, mem->size, num, config)) {
		*value1 += mem->align;
		*value2 = *value1 + mem->size - 1;
		if (*value1 > mem->max || !mem->align)
			return -ENOENT;
	}
	return 0;
}

static int pnp_generate_irq(struct pnp_cfg *config, int num)
{
	struct pnp_irq *irq;
	unsigned long *value1, *value2, *value3;
	/* IRQ priority: this table is good for i386 */
	static unsigned short xtab[16] = {
		5, 10, 11, 12, 9, 14, 15, 7, 3, 4, 13, 0, 1, 6, 8, 2
	};
	int i;
	if (!config || num < 0 || num > 1)
		return -EINVAL;
	irq = config->irq[num];
	if (!irq)
		return 0;
	value1 = &config->request.irq_resource[num].start;
	value2 = &config->request.irq_resource[num].end;
	value3 = &config->request.irq_resource[num].flags;
	*value3 = irq->flags | IORESOURCE_IRQ;

	for (i=0; i < 16; i++)
	{
		if(irq->map & (1<<xtab[i])) {
		*value1 = *value2 = xtab[i];
		if(pnp_check_interrupt(*value1,config)==0)
			return 0;
		}
	}
	return -ENOENT;
}

static int pnp_generate_dma(struct pnp_cfg *config, int num)
{
	struct pnp_dma *dma;
	unsigned long *value1, *value2, *value3;
	/* DMA priority: this table is good for i386 */
	static unsigned short xtab[16] = {
		1, 3, 5, 6, 7, 0, 2, 4
	};
	int i;
	if (!config || num < 0 || num > 1)
		return -EINVAL;
	dma = config->dma[num];
	if (!dma)
		return 0;
	value1 = &config->request.dma_resource[num].start;
	value2 = &config->request.dma_resource[num].end;
	value3 = &config->request.dma_resource[num].flags;
	*value3 = dma->flags | IORESOURCE_DMA;

	for (i=0; i < 8; i++)
	{
		if(dma->map & (1<<xtab[i])) {
		*value1 = *value2 = xtab[i];
		if(pnp_check_dma(*value1,config)==0)
			return 0;
		}
	}
	return -ENOENT;
}

int pnp_init_res_cfg(struct pnp_res_cfg *res_config)
{
	int idx;

	if (!res_config)
		return -EINVAL;
	for (idx = 0; idx < DEVICE_COUNT_IRQ; idx++) {
		res_config->irq_resource[idx].start = -1;
		res_config->irq_resource[idx].end = -1;
		res_config->irq_resource[idx].flags = IORESOURCE_IRQ|IORESOURCE_UNSET;
	}
	for (idx = 0; idx < DEVICE_COUNT_DMA; idx++) {
		res_config->dma_resource[idx].name = NULL;
		res_config->dma_resource[idx].start = -1;
		res_config->dma_resource[idx].end = -1;
		res_config->dma_resource[idx].flags = IORESOURCE_DMA|IORESOURCE_UNSET;
	}
	for (idx = 0; idx < DEVICE_COUNT_IO; idx++) {
		res_config->io_resource[idx].name = NULL;
		res_config->io_resource[idx].start = 0;
		res_config->io_resource[idx].end = 0;
		res_config->io_resource[idx].flags = IORESOURCE_IO|IORESOURCE_UNSET;
	}
	for (idx = 0; idx < DEVICE_COUNT_MEM; idx++) {
		res_config->mem_resource[idx].name = NULL;
		res_config->mem_resource[idx].start = 0;
		res_config->mem_resource[idx].end = 0;
		res_config->mem_resource[idx].flags = IORESOURCE_MEM|IORESOURCE_UNSET;
	}
	return 0;
}

static int pnp_prepare_request(struct pnp_dev *dev, struct pnp_cfg *config, struct pnp_res_cfg *template)
{
	int idx, err;
	if (!config)
		return -EINVAL;
	if (dev->lock_resources)
		return -EPERM;
	if (dev->active)
		return -EBUSY;
	err = pnp_init_res_cfg(&config->request);
	if (err < 0)
		return err;
	if (!template)
		return 0;
	for (idx = 0; idx < DEVICE_COUNT_IRQ; idx++)
		if (pnp_flags_valid(&template->irq_resource[idx]))
			config->request.irq_resource[idx] = template->irq_resource[idx];
	for (idx = 0; idx < DEVICE_COUNT_DMA; idx++)
		if (pnp_flags_valid(&template->dma_resource[idx]))
			config->request.dma_resource[idx] = template->dma_resource[idx];
	for (idx = 0; idx < DEVICE_COUNT_IO; idx++)
		if (pnp_flags_valid(&template->io_resource[idx]))
			config->request.io_resource[idx] = template->io_resource[idx];
	for (idx = 0; idx < DEVICE_COUNT_MEM; idx++)
		if (pnp_flags_valid(&template->io_resource[idx]))
			config->request.mem_resource[idx] = template->mem_resource[idx];
	return 0;
}

static int pnp_generate_request(struct pnp_dev *dev, struct pnp_cfg *config, struct pnp_res_cfg *template)
{
	int i, err;
	if (!config)
		return -EINVAL;
	if ((err = pnp_prepare_request(dev, config, template))<0)
		return err;
	for (i=0; i<=7; i++)
	{
		if(pnp_generate_port(config,i)<0)
			return -ENOENT;
	}
	for (i=0; i<=3; i++)
	{
		if(pnp_generate_mem(config,i)<0)
			return -ENOENT;
	}
	for (i=0; i<=1; i++)
	{
		if(pnp_generate_irq(config,i)<0)
			return -ENOENT;
	}
	for (i=0; i<=1; i++)
	{
		if(pnp_generate_dma(config,i)<0)
			return -ENOENT;
	}
	return 0;
}



static struct pnp_cfg * pnp_generate_config(struct pnp_dev *dev, int depnum)
{
	struct pnp_cfg * config;
	int nport = 0, nirq = 0, ndma = 0, nmem = 0;
	struct pnp_resources * res;
	struct pnp_port * port;
	struct pnp_mem * mem;
	struct pnp_irq * irq;
	struct pnp_dma * dma;
	if (!dev)
		return NULL;
	if (depnum < 0)
		return NULL;
	config = pnp_alloc(sizeof(struct pnp_cfg));
	if (!config)
		return NULL;

	/* independent */
	res = pnp_find_resources(dev, 0);
	if (!res)
		goto fail;
	port = res->port;
	mem = res->mem;
	irq = res->irq;
	dma = res->dma;
	while (port){
		config->port[nport] = port;
		nport++;
		port = port->next;
	}
	while (mem){
		config->mem[nmem] = mem;
		nmem++;
		mem = mem->next;
	}
	while (irq){
		config->irq[nirq] = irq;
		nirq++;
		irq = irq->next;
	}
	while (dma){
		config->dma[ndma] = dma;
		ndma++;
		dma = dma->next;
	}

	/* dependent */
	if (depnum == 0)
		return config;
	res = pnp_find_resources(dev, depnum);
	if (!res)
		goto fail;
	port = res->port;
	mem = res->mem;
	irq = res->irq;
	dma = res->dma;
	while (port){
		config->port[nport] = port;
		nport++;
		port = port->next;
	}
	while (mem){
		config->mem[nmem] = mem;
		nmem++;
		mem = mem->next;
	}

	while (irq){
		config->irq[nirq] = irq;
		nirq++;
		irq = irq->next;
	}
	while (dma){
		config->dma[ndma] = dma;
		ndma++;
		dma = dma->next;
	}
	return config;

	fail:
	kfree(config);
	return NULL;
}

/* PnP Device Resource Management */

/**
 * pnp_activate_dev - activates a PnP device for use
 * @dev: pointer to the desired device
 *
 * finds the best resource configuration and then informs the correct pnp protocol
 */

int pnp_activate_dev(struct pnp_dev *dev, struct pnp_res_cfg *template)
{
	int depnum, max;
	struct pnp_cfg *config;
	if (!dev)
		return -EINVAL;
        max = pnp_get_max_depnum(dev);
	if (!pnp_can_configure(dev))
		return -EBUSY;
	if (dev->status != PNP_READY && dev->status != PNP_ATTACHED){
		printk(KERN_INFO "pnp: Automatic configuration failed because the PnP device '%s' is busy\n", dev->dev.bus_id);
		return -EINVAL;
	}
	if (!pnp_can_write(dev))
		return -EINVAL;
	if (max == 0)
		return 0;
	for (depnum=1; depnum <= max; depnum++)
	{
		config = pnp_generate_config(dev,depnum);
		if (!config)
			return -EINVAL;
		if (pnp_generate_request(dev,config,template)==0)
			goto done;
		kfree(config);
	}
	printk(KERN_ERR "pnp: Automatic configuration failed for device '%s' due to resource conflicts\n", dev->dev.bus_id);
	return -ENOENT;

	done:
	pnp_dbg("the device '%s' has been activated", dev->dev.bus_id);
	dev->protocol->set(dev,config);
	if (pnp_can_read(dev))
		dev->protocol->get(dev);
	kfree(config);
	return 0;
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
	if (dev->status != PNP_READY){
		printk(KERN_INFO "pnp: Disable failed becuase the PnP device '%s' is busy\n", dev->dev.bus_id);
		return -EINVAL;
	}
	if (dev->lock_resources)
		return -EPERM;
	if (!pnp_can_disable(dev) || !dev->active)
		return -EINVAL;
	pnp_dbg("the device '%s' has been disabled", dev->dev.bus_id);
	return dev->protocol->disable(dev);
}

/**
 * pnp_raw_set_dev - same as pnp_activate_dev except the resource config can be specified
 * @dev: pointer to the desired device
 * @depnum: resource dependent function
 * @mode: static or dynamic
 *
 */

int pnp_raw_set_dev(struct pnp_dev *dev, int depnum, struct pnp_res_cfg *template)
{
	struct pnp_cfg *config;
	if (!dev)
		return -EINVAL;
	if (dev->status != PNP_READY){
		printk(KERN_INFO "pnp: Unable to set resources because the PnP device '%s' is busy\n", dev->dev.bus_id);
		return -EINVAL;
	}
	if (!pnp_can_write(dev) || !pnp_can_configure(dev))
		return -EINVAL;
        config = pnp_generate_config(dev,depnum);
	if (!config)
		return -EINVAL;
	if (pnp_generate_request(dev,config,template)==0)
		goto done;
	kfree(config);
	printk(KERN_ERR "pnp: Manual configuration failed for device '%s' due to resource conflicts\n", dev->dev.bus_id);
	return -ENOENT;

	done:
	dev->protocol->set(dev,config);
	if (pnp_can_read(dev))
		dev->protocol->get(dev);
	kfree(config);
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
	resource->flags &= ~(IORESOURCE_AUTO|IORESOURCE_UNSET);
	resource->start = start;
	resource->end = start + size - 1;
}

EXPORT_SYMBOL(pnp_build_resource);
EXPORT_SYMBOL(pnp_find_resources);
EXPORT_SYMBOL(pnp_get_max_depnum);
EXPORT_SYMBOL(pnp_add_irq_resource);
EXPORT_SYMBOL(pnp_add_dma_resource);
EXPORT_SYMBOL(pnp_add_port_resource);
EXPORT_SYMBOL(pnp_add_mem_resource);
EXPORT_SYMBOL(pnp_add_mem32_resource);
EXPORT_SYMBOL(pnp_init_res_cfg);
EXPORT_SYMBOL(pnp_activate_dev);
EXPORT_SYMBOL(pnp_disable_dev);
EXPORT_SYMBOL(pnp_raw_set_dev);
EXPORT_SYMBOL(pnp_resource_change);

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
