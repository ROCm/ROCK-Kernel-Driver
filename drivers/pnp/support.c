/*
 * support.c - provides standard pnp functions for the use of pnp protocol drivers,
 *
 * Copyright 2003 Adam Belay <ambx1@neo.rr.com>
 *
 * Resource parsing functions are based on those in the linux pnpbios driver.
 * Copyright Christian Schmidt, Tom Lees, David Hinds, Alan Cox, Thomas Hood,
 * Brian Gerst and Adam Belay.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/ctype.h>

#ifdef CONFIG_PNP_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif

#include <linux/pnp.h>
#include "base.h"

#define SMALL_TAG_PNPVERNO		0x01
#define SMALL_TAG_LOGDEVID		0x02
#define SMALL_TAG_COMPATDEVID		0x03
#define SMALL_TAG_IRQ			0x04
#define SMALL_TAG_DMA			0x05
#define SMALL_TAG_STARTDEP		0x06
#define SMALL_TAG_ENDDEP		0x07
#define SMALL_TAG_PORT			0x08
#define SMALL_TAG_FIXEDPORT		0x09
#define SMALL_TAG_VENDOR		0x0e
#define SMALL_TAG_END			0x0f
#define LARGE_TAG			0x80
#define LARGE_TAG_MEM			0x01
#define LARGE_TAG_ANSISTR		0x02
#define LARGE_TAG_UNICODESTR		0x03
#define LARGE_TAG_VENDOR		0x04
#define LARGE_TAG_MEM32			0x05
#define LARGE_TAG_FIXEDMEM32		0x06


/**
 * pnp_is_active - Determines if a device is active based on its current resources
 * @dev: pointer to the desired PnP device
 *
 */

int pnp_is_active(struct pnp_dev * dev)
{
	if (!pnp_port_start(dev, 0) && pnp_port_len(dev, 0) <= 1 &&
	    !pnp_mem_start(dev, 0) && pnp_mem_len(dev, 0) <= 1 &&
	    pnp_irq(dev, 0) == -1 &&
	    pnp_dma(dev, 0) == -1)
	    	return 0;
	else
		return 1;
}


/*
 * Current resource reading functions *
 */

static void current_irqresource(struct pnp_resource_table * res, int irq)
{
	int i = 0;
	while ((res->irq_resource[i].flags & IORESOURCE_IRQ) && i < PNP_MAX_IRQ) i++;
	if (i < PNP_MAX_IRQ) {
		res->irq_resource[i].flags = IORESOURCE_IRQ;  // Also clears _UNSET flag
		if (irq == -1) {
			res->irq_resource[i].flags |= IORESOURCE_DISABLED;
			return;
		}
		res->irq_resource[i].start =
		res->irq_resource[i].end = (unsigned long) irq;
	}
}

static void current_dmaresource(struct pnp_resource_table * res, int dma)
{
	int i = 0;
	while ((res->dma_resource[i].flags & IORESOURCE_DMA) && i < PNP_MAX_DMA) i++;
	if (i < PNP_MAX_DMA) {
		res->dma_resource[i].flags = IORESOURCE_DMA;  // Also clears _UNSET flag
		if (dma == -1) {
			res->dma_resource[i].flags |= IORESOURCE_DISABLED;
			return;
		}
		res->dma_resource[i].start =
		res->dma_resource[i].end = (unsigned long) dma;
	}
}

static void current_ioresource(struct pnp_resource_table * res, int io, int len)
{
	int i = 0;
	while ((res->port_resource[i].flags & IORESOURCE_IO) && i < PNP_MAX_PORT) i++;
	if (i < PNP_MAX_PORT) {
		res->port_resource[i].flags = IORESOURCE_IO;  // Also clears _UNSET flag
		if (len <= 0 || (io + len -1) >= 0x10003) {
			res->port_resource[i].flags |= IORESOURCE_DISABLED;
			return;
		}
		res->port_resource[i].start = (unsigned long) io;
		res->port_resource[i].end = (unsigned long)(io + len - 1);
	}
}

static void current_memresource(struct pnp_resource_table * res, int mem, int len)
{
	int i = 0;
	while ((res->mem_resource[i].flags & IORESOURCE_MEM) && i < PNP_MAX_MEM) i++;
	if (i < PNP_MAX_MEM) {
		res->mem_resource[i].flags = IORESOURCE_MEM;  // Also clears _UNSET flag
		if (len <= 0) {
			res->mem_resource[i].flags |= IORESOURCE_DISABLED;
			return;
		}
		res->mem_resource[i].start = (unsigned long) mem;
		res->mem_resource[i].end = (unsigned long)(mem + len - 1);
	}
}

/**
 * pnp_parse_current_resources - Extracts current resource information from a raw PnP resource structure
 * @p: pointer to the start of the structure
 * @end: pointer to the end of the structure
 * @res: pointer to the resource table to record to
 *
 */

unsigned char * pnp_parse_current_resources(unsigned char * p, unsigned char * end, struct pnp_resource_table * res)
{
	int len;

	if (!p)
		return NULL;

	/* Blank the resource table values */
	pnp_init_resource_table(res);

	while ((char *)p < (char *)end) {

		if(p[0] & LARGE_TAG) { /* large tag */
			len = (p[2] << 8) | p[1];
			switch (p[0] & 0x7f) {
			case LARGE_TAG_MEM:
			{
				int io = *(short *) &p[4];
				int size = *(short *) &p[10];
				if (len != 9)
					goto lrg_err;
				current_memresource(res, io, size);
				break;
			}
			case LARGE_TAG_ANSISTR:
			{
				/* ignore this for now */
				break;
			}
			case LARGE_TAG_VENDOR:
			{
				/* do nothing */
				break;
			}
			case LARGE_TAG_MEM32:
			{
				int io = *(int *) &p[4];
				int size = *(int *) &p[16];
				if (len != 17)
					goto lrg_err;
				current_memresource(res, io, size);
				break;
			}
			case LARGE_TAG_FIXEDMEM32:
			{
				int io = *(int *) &p[4];
				int size = *(int *) &p[8];
				if (len != 9)
					goto lrg_err;
				current_memresource(res, io, size);
				break;
			}
			default: /* an unkown tag */
			{
				lrg_err:
				pnp_warn("parser: Unknown large tag '0x%x'.", p[0] & 0x7f);
				break;
			}
			} /* switch */
			p += len + 3;
			continue;
		} /* end large tag */

		/* small tag */
		len = p[0] & 0x07;
		switch ((p[0]>>3) & 0x0f) {
		case SMALL_TAG_IRQ:
		{
			int i, mask, irq = -1;
			if (len < 2 || len > 3)
				goto sm_err;
			mask= p[1] + p[2]*256;
			for (i=0;i<16;i++, mask=mask>>1)
				if(mask & 0x01) irq=i;
			current_irqresource(res, irq);
			break;
		}
		case SMALL_TAG_DMA:
		{
			int i, mask, dma = -1;
			if (len != 2)
				goto sm_err;
			mask = p[1];
			for (i=0;i<8;i++, mask = mask>>1)
				if(mask & 0x01) dma=i;
			current_dmaresource(res, dma);
			break;
		}
		case SMALL_TAG_PORT:
		{
			int io= p[2] + p[3] *256;
			int size = p[7];
			if (len != 7)
				goto sm_err;
			current_ioresource(res, io, size);
			break;
		}
		case SMALL_TAG_VENDOR:
		{
			/* do nothing */
			break;
		}
		case SMALL_TAG_FIXEDPORT:
		{
			int io = p[1] + p[2] * 256;
			int size = p[3];
			if (len != 3)
				goto sm_err;
			current_ioresource(res, io, size);
			break;
		}
		case SMALL_TAG_END:
		{
			p = p + 2;
        		return (unsigned char *)p;
			break;
		}
		default: /* an unkown tag */
		{
			sm_err:
			pnp_warn("parser: Unknown small tag '0x%x'.", p[0]>>3);
			break;
		}
		}
                p += len + 1;
	}
	pnp_err("parser: Resource structure does not contain an end tag.");

	return NULL;
}


/*
 * Possible resource reading functions *
 */

static void possible_mem(unsigned char *p, int size, struct pnp_option *option)
{
	struct pnp_mem * mem;
	mem = pnp_alloc(sizeof(struct pnp_mem));
	if (!mem)
		return;
	mem->min = ((p[5] << 8) | p[4]) << 8;
	mem->max = ((p[7] << 8) | p[6]) << 8;
	mem->align = (p[9] << 8) | p[8];
	mem->size = ((p[11] << 8) | p[10]) << 8;
	mem->flags = p[3];
	pnp_register_mem_resource(option,mem);
	return;
}

static void possible_mem32(unsigned char *p, int size, struct pnp_option *option)
{
	struct pnp_mem * mem;
	mem = pnp_alloc(sizeof(struct pnp_mem));
	if (!mem)
		return;
	mem->min = (p[7] << 24) | (p[6] << 16) | (p[5] << 8) | p[4];
	mem->max = (p[11] << 24) | (p[10] << 16) | (p[9] << 8) | p[8];
	mem->align = (p[15] << 24) | (p[14] << 16) | (p[13] << 8) | p[12];
	mem->size = (p[19] << 24) | (p[18] << 16) | (p[17] << 8) | p[16];
	mem->flags = p[3];
	pnp_register_mem_resource(option,mem);
	return;
}

static void possible_fixed_mem32(unsigned char *p, int size, struct pnp_option *option)
{
	struct pnp_mem * mem;
	mem = pnp_alloc(sizeof(struct pnp_mem));
	if (!mem)
		return;
	mem->min = mem->max = (p[7] << 24) | (p[6] << 16) | (p[5] << 8) | p[4];
	mem->size = (p[11] << 24) | (p[10] << 16) | (p[9] << 8) | p[8];
	mem->align = 0;
	mem->flags = p[3];
	pnp_register_mem_resource(option,mem);
	return;
}

static void possible_irq(unsigned char *p, int size, struct pnp_option *option)
{
	struct pnp_irq * irq;
	irq = pnp_alloc(sizeof(struct pnp_irq));
	if (!irq)
		return;
	irq->map = (p[2] << 8) | p[1];
	if (size > 2)
		irq->flags = p[3];
	else
		irq->flags = IORESOURCE_IRQ_HIGHEDGE;
	pnp_register_irq_resource(option,irq);
	return;
}

static void possible_dma(unsigned char *p, int size, struct pnp_option *option)
{
	struct pnp_dma * dma;
	dma = pnp_alloc(sizeof(struct pnp_dma));
	if (!dma)
		return;
	dma->map = p[1];
	dma->flags = p[2];
	pnp_register_dma_resource(option,dma);
	return;
}

static void possible_port(unsigned char *p, int size, struct pnp_option *option)
{
	struct pnp_port * port;
	port = pnp_alloc(sizeof(struct pnp_port));
	if (!port)
		return;
	port->min = (p[3] << 8) | p[2];
	port->max = (p[5] << 8) | p[4];
	port->align = p[6];
	port->size = p[7];
	port->flags = p[1] ? PNP_PORT_FLAG_16BITADDR : 0;
	pnp_register_port_resource(option,port);
	return;
}

static void possible_fixed_port(unsigned char *p, int size, struct pnp_option *option)
{
	struct pnp_port * port;
	port = pnp_alloc(sizeof(struct pnp_port));
	if (!port)
		return;
	port->min = port->max = (p[2] << 8) | p[1];
	port->size = p[3];
	port->align = 0;
	port->flags = PNP_PORT_FLAG_FIXED;
	pnp_register_port_resource(option,port);
	return;
}

/**
 * pnp_parse_possible_resources - Extracts possible resource information from a raw PnP resource structure
 * @p: pointer to the start of the structure
 * @end: pointer to the end of the structure
 * @dev: pointer to the desired PnP device
 *
 */

unsigned char * pnp_parse_possible_resources(unsigned char * p, unsigned char * end, struct pnp_dev *dev)
{
	int len, priority = 0;
	struct pnp_option *option;

	if (!p)
		return NULL;

	option = pnp_register_independent_option(dev);
	if (!option)
		return NULL;

	while ((char *)p < (char *)end) {

		if(p[0] & LARGE_TAG) { /* large tag */
			len = (p[2] << 8) | p[1];
			switch (p[0] & 0x7f) {
			case LARGE_TAG_MEM:
			{
				if (len != 9)
					goto lrg_err;
				possible_mem(p,len,option);
				break;
			}
			case LARGE_TAG_MEM32:
			{
				if (len != 17)
					goto lrg_err;
				possible_mem32(p,len,option);
				break;
			}
			case LARGE_TAG_FIXEDMEM32:
			{
				if (len != 9)
					goto lrg_err;
				possible_fixed_mem32(p,len,option);
				break;
			}
			default: /* an unkown tag */
			{
				lrg_err:
				pnp_warn("parser: Unknown large tag '0x%x'.", p[0] & 0x7f);
				break;
			}
			} /* switch */
                        p += len + 3;
			continue;
		} /* end large tag */

		/* small tag */
		len = p[0] & 0x07;
		switch ((p[0]>>3) & 0x0f) {
		case SMALL_TAG_IRQ:
		{
			if (len < 2 || len > 3)
				goto sm_err;
			possible_irq(p,len,option);
			break;
		}
		case SMALL_TAG_DMA:
		{
			if (len != 2)
				goto sm_err;
			possible_dma(p,len,option);
			break;
		}
		case SMALL_TAG_STARTDEP:
		{
			if (len > 1)
				goto sm_err;
			priority = 0x100 | PNP_RES_PRIORITY_ACCEPTABLE;
			if (len > 0)
				priority = 0x100 | p[1];
			option = pnp_register_dependent_option(dev, priority);
			if (!option)
				return NULL;
			break;
		}
		case SMALL_TAG_ENDDEP:
		{
			if (len != 0)
				goto sm_err;
			break;
		}
		case SMALL_TAG_PORT:
		{
			if (len != 7)
				goto sm_err;
			possible_port(p,len,option);
			break;
		}
		case SMALL_TAG_FIXEDPORT:
		{
			if (len != 3)
				goto sm_err;
			possible_fixed_port(p,len,option);
			break;
		}
		case SMALL_TAG_END:
		{
			p = p + 2;
			return (unsigned char *)p;
			break;
		}
		default: /* an unkown tag */
		{
			sm_err:
			pnp_warn("parser: Unknown small tag '0x%x'.", p[0]>>3);
			break;
		}
		}
                p += len + 1;
	}
	pnp_err("parser: Resource structure does not contain an end tag.");

	return NULL;
}


/*
 * Resource Writing functions
 */

static void write_mem(unsigned char *p, struct resource * res)
{
	unsigned long base = res->start;
	unsigned long len = res->end - res->start + 1;
	p[4] = (base >> 8) & 0xff;
	p[5] = ((base >> 8) >> 8) & 0xff;
	p[6] = (base >> 8) & 0xff;
	p[7] = ((base >> 8) >> 8) & 0xff;
	p[10] = (len >> 8) & 0xff;
	p[11] = ((len >> 8) >> 8) & 0xff;
	return;
}

static void write_mem32(unsigned char *p, struct resource * res)
{
	unsigned long base = res->start;
	unsigned long len = res->end - res->start + 1;
	p[4] = base & 0xff;
	p[5] = (base >> 8) & 0xff;
	p[6] = (base >> 16) & 0xff;
	p[7] = (base >> 24) & 0xff;
	p[8] = base & 0xff;
	p[9] = (base >> 8) & 0xff;
	p[10] = (base >> 16) & 0xff;
	p[11] = (base >> 24) & 0xff;
	p[16] = len & 0xff;
	p[17] = (len >> 8) & 0xff;
	p[18] = (len >> 16) & 0xff;
	p[19] = (len >> 24) & 0xff;
	return;
}

static void write_fixed_mem32(unsigned char *p, struct resource * res)
{	unsigned long base = res->start;
	unsigned long len = res->end - res->start + 1;
	p[4] = base & 0xff;
	p[5] = (base >> 8) & 0xff;
	p[6] = (base >> 16) & 0xff;
	p[7] = (base >> 24) & 0xff;
	p[8] = len & 0xff;
	p[9] = (len >> 8) & 0xff;
	p[10] = (len >> 16) & 0xff;
	p[11] = (len >> 24) & 0xff;
	return;
}

static void write_irq(unsigned char *p, struct resource * res)
{
	unsigned long map = 0;
	map = 1 << res->start;
	p[1] = map & 0xff;
	p[2] = (map >> 8) & 0xff;
	return;
}

static void write_dma(unsigned char *p, struct resource * res)
{
	unsigned long map = 0;
	map = 1 << res->start;
	p[1] = map & 0xff;
	return;
}

static void write_port(unsigned char *p, struct resource * res)
{
	unsigned long base = res->start;
	unsigned long len = res->end - res->start + 1;
	p[2] = base & 0xff;
	p[3] = (base >> 8) & 0xff;
	p[4] = base & 0xff;
	p[5] = (base >> 8) & 0xff;
	p[7] = len & 0xff;
	return;
}

static void write_fixed_port(unsigned char *p, struct resource * res)
{
	unsigned long base = res->start;
	unsigned long len = res->end - res->start + 1;
	p[1] = base & 0xff;
	p[2] = (base >> 8) & 0xff;
	p[3] = len & 0xff;
	return;
}

/**
 * pnp_write_resources - Writes resource information to a raw PnP resource structure
 * @p: pointer to the start of the structure
 * @end: pointer to the end of the structure
 * @res: pointer to a resource table containing the resources to set
 *
 */

unsigned char * pnp_write_resources(unsigned char * p, unsigned char * end, struct pnp_resource_table * res)
{
	int len, port = 0, irq = 0, dma = 0, mem = 0;

	if (!p)
		return NULL;

	while ((char *)p < (char *)end) {

		if(p[0] & LARGE_TAG) { /* large tag */
			len = (p[2] << 8) | p[1];
			switch (p[0] & 0x7f) {
			case LARGE_TAG_MEM:
			{
				if (len != 9)
					goto lrg_err;
				write_mem(p, &res->mem_resource[mem]);
				mem++;
				break;
			}
			case LARGE_TAG_MEM32:
			{
				if (len != 17)
					goto lrg_err;
				write_mem32(p, &res->mem_resource[mem]);
				break;
			}
			case LARGE_TAG_FIXEDMEM32:
			{
				if (len != 9)
					goto lrg_err;
				write_fixed_mem32(p, &res->mem_resource[mem]);
				break;
			}
			default: /* an unkown tag */
			{
				lrg_err:
				pnp_warn("parser: Unknown large tag '0x%x'.", p[0] & 0x7f);
				break;
			}
			} /* switch */
                        p += len + 3;
			continue;
		} /* end large tag */

		/* small tag */
		len = p[0] & 0x07;
		switch ((p[0]>>3) & 0x0f) {
		case SMALL_TAG_IRQ:
		{
			if (len < 2 || len > 3)
				goto sm_err;
			write_irq(p, &res->irq_resource[irq]);
			irq++;
			break;
		}
		case SMALL_TAG_DMA:
		{
			if (len != 2)
				goto sm_err;
			write_dma(p, &res->dma_resource[dma]);
			dma++;
			break;
		}
		case SMALL_TAG_PORT:
		{
			if (len != 7)
				goto sm_err;
			write_port(p, &res->port_resource[port]);
			port++;
			break;
		}
		case SMALL_TAG_FIXEDPORT:
		{
			if (len != 3)
				goto sm_err;
			write_fixed_port(p, &res->port_resource[port]);
			port++;
			break;
		}
		case SMALL_TAG_END:
		{
			p = p + 2;
			return (unsigned char *)p;
			break;
		}
		default: /* an unkown tag */
		{
			sm_err:
			pnp_warn("parser: Unknown small tag '0x%x'.", p[0]>>3);
			break;
		}
		}
                p += len + 1;
	}
	pnp_err("parser: Resource structure does not contain an end tag.");

	return NULL;
}

EXPORT_SYMBOL(pnp_is_active);
EXPORT_SYMBOL(pnp_parse_current_resources);
EXPORT_SYMBOL(pnp_parse_possible_resources);
EXPORT_SYMBOL(pnp_write_resources);
