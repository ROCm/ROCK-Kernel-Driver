/*
 * interface.c - contains everything related to the user interface
 *
 * Some code, especially possible resource dumping is based on isapnp_proc.c (c) Jaroslav Kysela <perex@suse.cz>
 * Copyright 2002 Adam Belay <ambx1@neo.rr.com>
 *
 */

#include <linux/pnp.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#include "base.h"

struct pnp_info_buffer {
	char *buffer;		/* pointer to begin of buffer */
	char *curr;		/* current position in buffer */
	unsigned long size;	/* current size */
	unsigned long len;	/* total length of buffer */
	int stop;		/* stop flag */
	int error;		/* error code */
};

typedef struct pnp_info_buffer pnp_info_buffer_t;

int pnp_printf(pnp_info_buffer_t * buffer, char *fmt,...)
{
	va_list args;
	int res;

	if (buffer->stop || buffer->error)
		return 0;
	va_start(args, fmt);
	res = vsnprintf(buffer->curr, buffer->len - buffer->size, fmt, args);
	va_end(args);
	if (buffer->size + res >= buffer->len) {
		buffer->stop = 1;
		return 0;
	}
	buffer->curr += res;
	buffer->size += res;
	return res;
}

static void pnp_print_port(pnp_info_buffer_t *buffer, char *space, struct pnp_port *port)
{
	pnp_printf(buffer, "%sport 0x%x-0x%x, align 0x%x, size 0x%x, %i-bit address decoding\n",
			space, port->min, port->max, port->align ? (port->align-1) : 0, port->size,
			port->flags & PNP_PORT_FLAG_16BITADDR ? 16 : 10);
}

static void pnp_print_irq(pnp_info_buffer_t *buffer, char *space, struct pnp_irq *irq)
{
	int first = 1, i;

	pnp_printf(buffer, "%sirq ", space);
	for (i = 0; i < 16; i++)
		if (irq->map & (1<<i)) {
			if (!first) {
				pnp_printf(buffer, ",");
			} else {
				first = 0;
			}
			if (i == 2 || i == 9)
				pnp_printf(buffer, "2/9");
			else
				pnp_printf(buffer, "%i", i);
		}
	if (!irq->map)
		pnp_printf(buffer, "<none>");
	if (irq->flags & IORESOURCE_IRQ_HIGHEDGE)
		pnp_printf(buffer, " High-Edge");
	if (irq->flags & IORESOURCE_IRQ_LOWEDGE)
		pnp_printf(buffer, " Low-Edge");
	if (irq->flags & IORESOURCE_IRQ_HIGHLEVEL)
		pnp_printf(buffer, " High-Level");
	if (irq->flags & IORESOURCE_IRQ_LOWLEVEL)
		pnp_printf(buffer, " Low-Level");
	pnp_printf(buffer, "\n");
}

static void pnp_print_dma(pnp_info_buffer_t *buffer, char *space, struct pnp_dma *dma)
{
	int first = 1, i;
	char *s;

	pnp_printf(buffer, "%sdma ", space);
	for (i = 0; i < 8; i++)
		if (dma->map & (1<<i)) {
			if (!first) {
				pnp_printf(buffer, ",");
			} else {
				first = 0;
			}
			pnp_printf(buffer, "%i", i);
		}
	if (!dma->map)
		pnp_printf(buffer, "<none>");
	switch (dma->flags & IORESOURCE_DMA_TYPE_MASK) {
	case IORESOURCE_DMA_8BIT:
		s = "8-bit";
		break;
	case IORESOURCE_DMA_8AND16BIT:
		s = "8-bit&16-bit";
		break;
	default:
		s = "16-bit";
	}
	pnp_printf(buffer, " %s", s);
	if (dma->flags & IORESOURCE_DMA_MASTER)
		pnp_printf(buffer, " master");
	if (dma->flags & IORESOURCE_DMA_BYTE)
		pnp_printf(buffer, " byte-count");
	if (dma->flags & IORESOURCE_DMA_WORD)
		pnp_printf(buffer, " word-count");
	switch (dma->flags & IORESOURCE_DMA_SPEED_MASK) {
	case IORESOURCE_DMA_TYPEA:
		s = "type-A";
		break;
	case IORESOURCE_DMA_TYPEB:
		s = "type-B";
		break;
	case IORESOURCE_DMA_TYPEF:
		s = "type-F";
		break;
	default:
		s = "compatible";
		break;
	}
	pnp_printf(buffer, " %s\n", s);
}

static void pnp_print_mem(pnp_info_buffer_t *buffer, char *space, struct pnp_mem *mem)
{
	char *s;

	pnp_printf(buffer, "%sMemory 0x%x-0x%x, align 0x%x, size 0x%x",
			space, mem->min, mem->max, mem->align, mem->size);
	if (mem->flags & IORESOURCE_MEM_WRITEABLE)
		pnp_printf(buffer, ", writeable");
	if (mem->flags & IORESOURCE_MEM_CACHEABLE)
		pnp_printf(buffer, ", cacheable");
	if (mem->flags & IORESOURCE_MEM_RANGELENGTH)
		pnp_printf(buffer, ", range-length");
	if (mem->flags & IORESOURCE_MEM_SHADOWABLE)
		pnp_printf(buffer, ", shadowable");
	if (mem->flags & IORESOURCE_MEM_EXPANSIONROM)
		pnp_printf(buffer, ", expansion ROM");
	switch (mem->flags & IORESOURCE_MEM_TYPE_MASK) {
	case IORESOURCE_MEM_8BIT:
		s = "8-bit";
		break;
	case IORESOURCE_MEM_8AND16BIT:
		s = "8-bit&16-bit";
		break;
	case IORESOURCE_MEM_32BIT:
		s = "32-bit";
		break;
	default:
		s = "16-bit";
	}
	pnp_printf(buffer, ", %s\n", s);
}

static void pnp_print_resources(pnp_info_buffer_t *buffer, char *space, struct pnp_resources *res, int dep)
{
	char *s;
	struct pnp_port *port;
	struct pnp_irq *irq;
	struct pnp_dma *dma;
	struct pnp_mem *mem;

	switch (res->priority) {
	case PNP_RES_PRIORITY_PREFERRED:
		s = "preferred";
		break;
	case PNP_RES_PRIORITY_ACCEPTABLE:
		s = "acceptable";
		break;
	case PNP_RES_PRIORITY_FUNCTIONAL:
		s = "functional";
		break;
	default:
		s = "invalid";
	}
	if (dep > 0)
		pnp_printf(buffer, "Dependent: %02i - Priority %s\n",dep, s);
	for (port = res->port; port; port = port->next)
		pnp_print_port(buffer, space, port);
	for (irq = res->irq; irq; irq = irq->next)
		pnp_print_irq(buffer, space, irq);
	for (dma = res->dma; dma; dma = dma->next)
		pnp_print_dma(buffer, space, dma);
	for (mem = res->mem; mem; mem = mem->next)
		pnp_print_mem(buffer, space, mem);
}

static ssize_t pnp_show_possible_resources(struct device *dmdev, char *buf)
{
	struct pnp_dev *dev = to_pnp_dev(dmdev);
	struct pnp_resources * res = dev->possible;
	int ret, dep = 0;
	pnp_info_buffer_t *buffer = (pnp_info_buffer_t *)
				 pnp_alloc(sizeof(pnp_info_buffer_t));
	if (!buffer)
		return -ENOMEM;
	buffer->len = PAGE_SIZE;
	buffer->buffer = buf;
	buffer->curr = buffer->buffer;
	while (res){
		if (dep == 0)
			pnp_print_resources(buffer, "", res, dep);
		else
			pnp_print_resources(buffer, "   ", res, dep);
		res = res->dep;
		dep++;
	}
	ret = (buffer->curr - buf);
	kfree(buffer);
	return ret;
}

static DEVICE_ATTR(possible,S_IRUGO,pnp_show_possible_resources,NULL);

static void pnp_print_conflict_node(pnp_info_buffer_t *buffer, struct pnp_dev * dev)
{
	if (!dev)
		return;
	pnp_printf(buffer, "'%s'.\n", dev->dev.bus_id);
}

static void pnp_print_conflict_desc(pnp_info_buffer_t *buffer, int conflict)
{
	if (!conflict)
		return;
	pnp_printf(buffer, "  Conflict Detected: %2x - ", conflict);
	switch (conflict) {
	case CONFLICT_TYPE_RESERVED:
		pnp_printf(buffer, "manually reserved.\n");
		break;

	case CONFLICT_TYPE_IN_USE:
		pnp_printf(buffer, "currently in use.\n");
		break;

	case CONFLICT_TYPE_PCI:
		pnp_printf(buffer, "PCI device.\n");
		break;

	case CONFLICT_TYPE_INVALID:
		pnp_printf(buffer, "invalid.\n");
		break;

	case CONFLICT_TYPE_INTERNAL:
		pnp_printf(buffer, "another resource on this device.\n");
		break;

	case CONFLICT_TYPE_PNP_WARM:
		pnp_printf(buffer, "active PnP device ");
		break;

	case CONFLICT_TYPE_PNP_COLD:
		pnp_printf(buffer, "disabled PnP device ");
		break;
	default:
		pnp_printf(buffer, "Unknown conflict.\n");
		break;
	}
}

static void pnp_print_conflict(pnp_info_buffer_t *buffer, struct pnp_dev * dev, int idx, int type)
{
	struct pnp_dev * cdev, * wdev = NULL;
	int conflict;
	switch (type) {
	case IORESOURCE_IO:
		conflict = pnp_check_port(dev, idx);
		if (conflict == CONFLICT_TYPE_PNP_WARM)
			wdev = pnp_check_port_conflicts(dev, idx, SEARCH_WARM);
		cdev = pnp_check_port_conflicts(dev, idx, SEARCH_COLD);
		break;
	case IORESOURCE_MEM:
		conflict = pnp_check_mem(dev, idx);
		if (conflict == CONFLICT_TYPE_PNP_WARM)
			wdev = pnp_check_mem_conflicts(dev, idx, SEARCH_WARM);
		cdev = pnp_check_mem_conflicts(dev, idx, SEARCH_COLD);
		break;
	case IORESOURCE_IRQ:
		conflict = pnp_check_irq(dev, idx);
		if (conflict == CONFLICT_TYPE_PNP_WARM)
			wdev = pnp_check_irq_conflicts(dev, idx, SEARCH_WARM);
		cdev = pnp_check_irq_conflicts(dev, idx, SEARCH_COLD);
		break;
	case IORESOURCE_DMA:
		conflict = pnp_check_dma(dev, idx);
		if (conflict == CONFLICT_TYPE_PNP_WARM)
			wdev = pnp_check_dma_conflicts(dev, idx, SEARCH_WARM);
		cdev = pnp_check_dma_conflicts(dev, idx, SEARCH_COLD);
		break;
	default:
		return;
	}

	pnp_print_conflict_desc(buffer, conflict);

	if (wdev)
		pnp_print_conflict_node(buffer, wdev);

	if (cdev) {
		pnp_print_conflict_desc(buffer, CONFLICT_TYPE_PNP_COLD);
		pnp_print_conflict_node(buffer, cdev);
	}
}

static ssize_t pnp_show_current_resources(struct device *dmdev, char *buf)
{
	struct pnp_dev *dev = to_pnp_dev(dmdev);
	int i, ret;
	pnp_info_buffer_t *buffer = (pnp_info_buffer_t *)
				pnp_alloc(sizeof(pnp_info_buffer_t));
	if (!buffer)
		return -ENOMEM;
	if (!dev)
		return -EINVAL;
	buffer->len = PAGE_SIZE;
	buffer->buffer = buf;
	buffer->curr = buffer->buffer;

	pnp_printf(buffer,"state = ");
	if (dev->active)
		pnp_printf(buffer,"active\n");
	else
		pnp_printf(buffer,"disabled\n");
	for (i = 0; i < PNP_MAX_PORT; i++) {
		if (pnp_port_valid(dev, i)) {
			pnp_printf(buffer,"io");
			pnp_printf(buffer," 0x%lx-0x%lx \n",
						pnp_port_start(dev, i),
						pnp_port_end(dev, i));
			pnp_print_conflict(buffer, dev, i, IORESOURCE_IO);
		}
	}
	for (i = 0; i < PNP_MAX_MEM; i++) {
		if (pnp_mem_valid(dev, i)) {
			pnp_printf(buffer,"mem");
			pnp_printf(buffer," 0x%lx-0x%lx \n",
						pnp_mem_start(dev, i),
						pnp_mem_end(dev, i));
			pnp_print_conflict(buffer, dev, i, IORESOURCE_MEM);
		}
	}
	for (i = 0; i < PNP_MAX_IRQ; i++) {
		if (pnp_irq_valid(dev, i)) {
			pnp_printf(buffer,"irq");
			pnp_printf(buffer," %ld \n", pnp_irq(dev, i));
			pnp_print_conflict(buffer, dev, i, IORESOURCE_IRQ);
		}
	}
	for (i = 0; i < PNP_MAX_DMA; i++) {
		if (pnp_dma_valid(dev, i)) {
			pnp_printf(buffer,"dma");
			pnp_printf(buffer," %ld \n", pnp_dma(dev, i));
			pnp_print_conflict(buffer, dev, i, IORESOURCE_DMA);
		}
	}
	ret = (buffer->curr - buf);
	kfree(buffer);
	return ret;
}

extern int pnp_resolve_conflicts(struct pnp_dev *dev);

static ssize_t
pnp_set_current_resources(struct device * dmdev, const char * ubuf, size_t count)
{
	struct pnp_dev *dev = to_pnp_dev(dmdev);
	char	*buf = (void *)ubuf;
	int	retval = 0;

	while (isspace(*buf))
		++buf;
	if (!strnicmp(buf,"disable",7)) {
		retval = pnp_disable_dev(dev);
		goto done;
	}
	if (!strnicmp(buf,"activate",8)) {
		retval = pnp_activate_dev(dev);
		goto done;
	}
	if (!strnicmp(buf,"reset",5)) {
		if (!dev->active)
			goto done;
		retval = pnp_disable_dev(dev);
		if (retval)
			goto done;
		retval = pnp_activate_dev(dev);
		goto done;
	}
	if (!strnicmp(buf,"auto-config",11)) {
		if (dev->active)
			goto done;
		retval = pnp_auto_config_dev(dev);
		goto done;
	}
	if (!strnicmp(buf,"clear-config",12)) {
		if (dev->active)
			goto done;
		spin_lock(&pnp_lock);
		dev->config_mode = PNP_CONFIG_MANUAL;
		pnp_init_resource_table(&dev->res);
		if (dev->rule)
			dev->rule->depnum = 0;
		spin_unlock(&pnp_lock);
		goto done;
	}
	if (!strnicmp(buf,"resolve",7)) {
		retval = pnp_resolve_conflicts(dev);
		goto done;
	}
	if (!strnicmp(buf,"get",3)) {
		spin_lock(&pnp_lock);
		if (pnp_can_read(dev))
			dev->protocol->get(dev, &dev->res);
		spin_unlock(&pnp_lock);
		goto done;
	}
	if (!strnicmp(buf,"set",3)) {
		int nport = 0, nmem = 0, nirq = 0, ndma = 0;
		if (dev->active)
			goto done;
		buf += 3;
		spin_lock(&pnp_lock);
		dev->config_mode = PNP_CONFIG_MANUAL;
		pnp_init_resource_table(&dev->res);
		while (1) {
			while (isspace(*buf))
				++buf;
			if (!strnicmp(buf,"io",2)) {
				buf += 2;
				while (isspace(*buf))
					++buf;
				dev->res.port_resource[nport].start = simple_strtoul(buf,&buf,0);
				while (isspace(*buf))
					++buf;
				if(*buf == '-') {
					buf += 1;
					while (isspace(*buf))
						++buf;
					dev->res.port_resource[nport].end = simple_strtoul(buf,&buf,0);
				} else
					dev->res.port_resource[nport].end = dev->res.port_resource[nport].start;
				dev->res.port_resource[nport].flags = IORESOURCE_IO;
				nport++;
				if (nport >= PNP_MAX_PORT)
					break;
				continue;
			}
			if (!strnicmp(buf,"mem",3)) {
				buf += 3;
				while (isspace(*buf))
					++buf;
				dev->res.mem_resource[nmem].start = simple_strtoul(buf,&buf,0);
				while (isspace(*buf))
					++buf;
				if(*buf == '-') {
					buf += 1;
					while (isspace(*buf))
						++buf;
					dev->res.mem_resource[nmem].end = simple_strtoul(buf,&buf,0);
				} else
					dev->res.mem_resource[nmem].end = dev->res.mem_resource[nmem].start;
				dev->res.mem_resource[nmem].flags = IORESOURCE_MEM;
				nmem++;
				if (nmem >= PNP_MAX_MEM)
					break;
				continue;
			}
			if (!strnicmp(buf,"irq",3)) {
				buf += 3;
				while (isspace(*buf))
					++buf;
				dev->res.irq_resource[nirq].start =
				dev->res.irq_resource[nirq].end = simple_strtoul(buf,&buf,0);
				dev->res.irq_resource[nirq].flags = IORESOURCE_IRQ;
				nirq++;
				if (nirq >= PNP_MAX_IRQ)
					break;
				continue;
			}
			if (!strnicmp(buf,"dma",3)) {
				buf += 3;
				while (isspace(*buf))
					++buf;
				dev->res.dma_resource[ndma].start =
				dev->res.dma_resource[ndma].end = simple_strtoul(buf,&buf,0);
				dev->res.dma_resource[ndma].flags = IORESOURCE_DMA;
				ndma++;
				if (ndma >= PNP_MAX_DMA)
					break;
				continue;
			}
			break;
		}
		spin_unlock(&pnp_lock);
		goto done;
	}
 done:
	if (retval)
		return retval;
	return count;
}

static DEVICE_ATTR(resources,S_IRUGO | S_IWUSR,
		   pnp_show_current_resources,pnp_set_current_resources);

static ssize_t pnp_show_current_ids(struct device *dmdev, char *buf)
{
	char *str = buf;
	struct pnp_dev *dev = to_pnp_dev(dmdev);
	struct pnp_id * pos = dev->id;

	while (pos) {
		str += sprintf(str,"%s\n", pos->id);
		pos = pos->next;
	}
	return (str - buf);
}

static DEVICE_ATTR(id,S_IRUGO,pnp_show_current_ids,NULL);

int pnp_interface_attach_device(struct pnp_dev *dev)
{
	device_create_file(&dev->dev,&dev_attr_possible);
	device_create_file(&dev->dev,&dev_attr_resources);
	device_create_file(&dev->dev,&dev_attr_id);
	return 0;
}
