// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 1992, 1998-2006 Linus Torvalds, Ingo Molnar
 * Copyright (C) 2005-2006, Thomas Gleixner, Russell King
 *
 * This file contains the interrupt descriptor management code. Detailed
 * information is available in Documentation/core-api/genericirq.rst
 *
 */
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/radix-tree.h>
#include <linux/bitmap.h>
#include <linux/irqdomain.h>
#include <linux/sysfs.h>
#include <linux/irqdesc.h>
/**
 * generic_handle_irq - Invoke the handler for a particular irq
 * @irq:	The irq number to handle
 *
 * Returns:	0 on success, or -EINVAL if conversion has failed
 *
 * 		This function must be called from an IRQ context with irq regs
 * 		initialized.
  */
#ifndef HAVE_GENERIC_HANDLE_DOMAIN_IRQ
int kcl_generic_handle_domain_irq(struct irq_domain *domain, unsigned int hwirq)
{
    int irq;
    irq = irq_find_mapping(domain, hwirq);

    return generic_handle_irq(irq);
}
EXPORT_SYMBOL_GPL(kcl_generic_handle_domain_irq);
#endif