/*
 * rsrc_mgr.c -- Resource management routines
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * (C) 1999		David A. Hinds
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/pci.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/cs.h>
#include <pcmcia/bulkmem.h>
#include <pcmcia/cistpl.h>
#include "cs_internal.h"

static DECLARE_MUTEX(rsrc_sem);

#ifdef CONFIG_PCMCIA_PROBE

typedef struct irq_info_t {
    u_int			Attributes;
    int				time_share, dyn_share;
    struct pcmcia_socket	*Socket;
} irq_info_t;

/* Table of IRQ assignments */
static irq_info_t irq_table[NR_IRQS];

#endif


/*======================================================================

    This checks to see if an interrupt is available, with support
    for interrupt sharing.  We don't support reserving interrupts
    yet.  If the interrupt is available, we allocate it.
    
======================================================================*/

#ifdef CONFIG_PCMCIA_PROBE

static irqreturn_t fake_irq(int i, void *d, struct pt_regs *r) { return IRQ_NONE; }
static inline int check_irq(int irq)
{
    if (request_irq(irq, fake_irq, 0, "bogus", NULL) != 0)
	return -1;
    free_irq(irq, NULL);
    return 0;
}

int try_irq(u_int Attributes, int irq, int specific)
{
    irq_info_t *info = &irq_table[irq];
    int ret = 0;

    down(&rsrc_sem);
    if (info->Attributes & RES_ALLOCATED) {
	switch (Attributes & IRQ_TYPE) {
	case IRQ_TYPE_EXCLUSIVE:
	    ret = CS_IN_USE;
	    break;
	case IRQ_TYPE_TIME:
	    if ((info->Attributes & RES_IRQ_TYPE)
		!= RES_IRQ_TYPE_TIME) {
		ret = CS_IN_USE;
		break;
	    }
	    if (Attributes & IRQ_FIRST_SHARED) {
		ret = CS_BAD_ATTRIBUTE;
		break;
	    }
	    info->Attributes |= RES_IRQ_TYPE_TIME | RES_ALLOCATED;
	    info->time_share++;
	    break;
	case IRQ_TYPE_DYNAMIC_SHARING:
	    if ((info->Attributes & RES_IRQ_TYPE)
		!= RES_IRQ_TYPE_DYNAMIC) {
		ret = CS_IN_USE;
		break;
	    }
	    if (Attributes & IRQ_FIRST_SHARED) {
		ret = CS_BAD_ATTRIBUTE;
		break;
	    }
	    info->Attributes |= RES_IRQ_TYPE_DYNAMIC | RES_ALLOCATED;
	    info->dyn_share++;
	    break;
	}
    } else {
	if ((info->Attributes & RES_RESERVED) && !specific) {
	    ret = CS_IN_USE;
	    goto out;
	}
	if (check_irq(irq) != 0) {
	    ret = CS_IN_USE;
	    goto out;
	}
	switch (Attributes & IRQ_TYPE) {
	case IRQ_TYPE_EXCLUSIVE:
	    info->Attributes |= RES_ALLOCATED;
	    break;
	case IRQ_TYPE_TIME:
	    if (!(Attributes & IRQ_FIRST_SHARED)) {
		ret = CS_BAD_ATTRIBUTE;
		break;
	    }
	    info->Attributes |= RES_IRQ_TYPE_TIME | RES_ALLOCATED;
	    info->time_share = 1;
	    break;
	case IRQ_TYPE_DYNAMIC_SHARING:
	    if (!(Attributes & IRQ_FIRST_SHARED)) {
		ret = CS_BAD_ATTRIBUTE;
		break;
	    }
	    info->Attributes |= RES_IRQ_TYPE_DYNAMIC | RES_ALLOCATED;
	    info->dyn_share = 1;
	    break;
	}
    }
 out:
    up(&rsrc_sem);
    return ret;
}

#endif

/*====================================================================*/

#ifdef CONFIG_PCMCIA_PROBE

void undo_irq(u_int Attributes, int irq)
{
    irq_info_t *info;

    info = &irq_table[irq];
    down(&rsrc_sem);
    switch (Attributes & IRQ_TYPE) {
    case IRQ_TYPE_EXCLUSIVE:
	info->Attributes &= RES_RESERVED;
	break;
    case IRQ_TYPE_TIME:
	info->time_share--;
	if (info->time_share == 0)
	    info->Attributes &= RES_RESERVED;
	break;
    case IRQ_TYPE_DYNAMIC_SHARING:
	info->dyn_share--;
	if (info->dyn_share == 0)
	    info->Attributes &= RES_RESERVED;
	break;
    }
    up(&rsrc_sem);
}

#endif

/*====================================================================*/

static int adjust_irq(adjust_t *adj)
{
    int ret = CS_SUCCESS;
#ifdef CONFIG_PCMCIA_PROBE
    int irq;
    irq_info_t *info;
    
    irq = adj->resource.irq.IRQ;
    if ((irq < 0) || (irq > 15))
	return CS_BAD_IRQ;
    info = &irq_table[irq];

    down(&rsrc_sem);
    switch (adj->Action) {
    case ADD_MANAGED_RESOURCE:
	if (info->Attributes & RES_REMOVED)
	    info->Attributes &= ~(RES_REMOVED|RES_ALLOCATED);
	else
	    if (adj->Attributes & RES_ALLOCATED) {
		ret = CS_IN_USE;
		break;
	    }
	if (adj->Attributes & RES_RESERVED)
	    info->Attributes |= RES_RESERVED;
	else
	    info->Attributes &= ~RES_RESERVED;
	break;
    case REMOVE_MANAGED_RESOURCE:
	if (info->Attributes & RES_REMOVED) {
	    ret = 0;
	    break;
	}
	if (info->Attributes & RES_ALLOCATED) {
	    ret = CS_IN_USE;
	    break;
	}
	info->Attributes |= RES_ALLOCATED|RES_REMOVED;
	info->Attributes &= ~RES_RESERVED;
	break;
    default:
	ret = CS_UNSUPPORTED_FUNCTION;
	break;
    }
    up(&rsrc_sem);
#endif
    return ret;
}

int pcmcia_adjust_resource_info(adjust_t *adj)
{
	struct pcmcia_socket *s;
	int ret = CS_UNSUPPORTED_FUNCTION;

	if (adj->Resource == RES_IRQ)
		return adjust_irq(adj);

	down_read(&pcmcia_socket_list_rwsem);
	list_for_each_entry(s, &pcmcia_socket_list, socket_list) {
		if (s->resource_ops->adjust_resource)
			ret = s->resource_ops->adjust_resource(s, adj);
	}
	up_read(&pcmcia_socket_list_rwsem);

	return (ret);
}
EXPORT_SYMBOL(pcmcia_adjust_resource_info);

void pcmcia_validate_mem(struct pcmcia_socket *s)
{
	if (s->resource_ops->validate_mem)
		s->resource_ops->validate_mem(s);
}
EXPORT_SYMBOL(pcmcia_validate_mem);

int adjust_io_region(struct resource *res, unsigned long r_start,
		     unsigned long r_end, struct pcmcia_socket *s)
{
	if (s->resource_ops->adjust_io_region)
		return s->resource_ops->adjust_io_region(res, r_start, r_end, s);
	return -ENOMEM;
}

struct resource *find_io_region(unsigned long base, int num,
		   unsigned long align, struct pcmcia_socket *s)
{
	if (s->resource_ops->find_io)
		return s->resource_ops->find_io(base, num, align, s);
	return NULL;
}

struct resource *find_mem_region(u_long base, u_long num, u_long align,
				 int low, struct pcmcia_socket *s)
{
	if (s->resource_ops->find_mem)
		return s->resource_ops->find_mem(base, num, align, low, s);
	return NULL;
}

void release_resource_db(struct pcmcia_socket *s)
{
	if (s->resource_ops->exit)
		s->resource_ops->exit(s);
}


struct pccard_resource_ops pccard_static_ops = {
	.validate_mem = NULL,
	.adjust_io_region = NULL,
	.find_io = NULL,
	.find_mem = NULL,
	.adjust_resource = NULL,
	.exit = NULL,
};
