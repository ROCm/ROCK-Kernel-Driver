/*
 * interface.c - contains everything related to the user interface
 *
 * Some code is based on isapnp_proc.c (c) Jaroslav Kysela <perex@suse.cz>
 * Copyright 2002 Adam Belay <ambx1@neo.rr.com>
 *
 */

#include <linux/pnp.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/stat.h>

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
	char sbuffer[512];

	if (buffer->stop || buffer->error)
		return 0;
	va_start(args, fmt);
	res = vsprintf(sbuffer, fmt, args);
	va_end(args);
	if (buffer->size + res >= buffer->len) {
		buffer->stop = 1;
		return 0;
	}
	strcpy(buffer->curr, sbuffer);
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
	default:
		s = "16-bit";
	}
	pnp_printf(buffer, ", %s\n", s);
}

static void pnp_print_mem32(pnp_info_buffer_t *buffer, char *space, struct pnp_mem32 *mem32)
{
	int first = 1, i;

	pnp_printf(buffer, "%s32-bit memory ", space);
	for (i = 0; i < 17; i++) {
		if (first) {
			first = 0;
		} else {
			pnp_printf(buffer, ":");
		}
		pnp_printf(buffer, "%02x", mem32->data[i]);
	}
}

static void pnp_print_resources(pnp_info_buffer_t *buffer, char *space, struct pnp_resources *res, int dep)
{
	char *s;
	struct pnp_port *port;
	struct pnp_irq *irq;
	struct pnp_dma *dma;
	struct pnp_mem *mem;
	struct pnp_mem32 *mem32;

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
	for (mem32 = res->mem32; mem32; mem32 = mem32->next)
		pnp_print_mem32(buffer, space, mem32);
}

static ssize_t pnp_show_possible_resources(struct device *dmdev, char *buf)
{
	struct pnp_dev *dev = to_pnp_dev(dmdev);
	struct pnp_resources * res = dev->res;
	int dep = 0;
	pnp_info_buffer_t *buffer;

	buffer = (pnp_info_buffer_t *) pnp_alloc(sizeof(pnp_info_buffer_t));
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
	return (buffer->curr - buf);
}

static DEVICE_ATTR(possible,S_IRUGO,pnp_show_possible_resources,NULL);

static ssize_t pnp_show_current_resources(struct device *dmdev, char *buf)
{
	struct pnp_dev *dev = to_pnp_dev(dmdev);
	char *str = buf;
	int i;

	if (!dev->active){
		str += sprintf(str,"DISABLED\n");
		goto done;
	}
	for (i = 0; i < DEVICE_COUNT_IO; i++) {
		if (pnp_port_valid(dev, i)) {
			str += sprintf(str,"io");
			str += sprintf(str," 0x%lx-0x%lx \n",
						pnp_port_start(dev, i),
						pnp_port_end(dev, i));
		}
	}
	for (i = 0; i < DEVICE_COUNT_MEM; i++) {
		if (pnp_mem_valid(dev, i)) {
			str += sprintf(str,"mem");
			str += sprintf(str," 0x%lx-0x%lx \n",
						pnp_mem_start(dev, i),
						pnp_mem_end(dev, i));
		}
	}
	for (i = 0; i < DEVICE_COUNT_IRQ; i++) {
		if (pnp_irq_valid(dev, i)) {
			str += sprintf(str,"irq");
			str += sprintf(str," %ld \n", pnp_irq(dev, i));
		}
	}
	for (i = 0; i < DEVICE_COUNT_DMA; i++) {
		if (pnp_dma_valid(dev, i)) {
			str += sprintf(str,"dma");
			str += sprintf(str," %ld \n", pnp_dma(dev, i));
		}
	}
	done:
	return (str - buf);
}

static ssize_t
pnp_set_current_resources(struct device * dmdev, const char * buf, size_t count)
{
	struct pnp_dev *dev = to_pnp_dev(dmdev);
	char	command[20];
	int	num_args;
	int	error = 0;
	int	depnum;

	num_args = sscanf(buf,"%10s %i",command,&depnum);
	if (!num_args)
		goto done;
	if (!strnicmp(command,"lock",4)) {
		if (dev->active) {
			dev->lock_resources = 1;
		} else {
			error = -EINVAL;
		}
		goto done;
	}
	if (!strnicmp(command,"unlock",6)) {
		if (dev->lock_resources) {
			dev->lock_resources = 0;
		} else {
			error = -EINVAL;
		}
		goto done;
	}
	if (!strnicmp(command,"disable",7)) {
		error = pnp_disable_dev(dev);
		goto done;
	}
	if (!strnicmp(command,"auto",4)) {
		error = pnp_activate_dev(dev,NULL);
		goto done;
	}
	if (!strnicmp(command,"manual",6)) {
		if (num_args != 2)
			goto done;
		error = pnp_raw_set_dev(dev,depnum,NULL);
		goto done;
	}
 done:
	return error < 0 ? error : count;
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
