/*
 *  linux/drivers/input/serio/amba_kmi.c
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 *  Copyright (C) 2002 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/hardware/amba_kmi.h>

#define KMI_BASE	(kmi->base)

struct amba_kmi_port {
	struct serio		io;
	struct amba_kmi_port	*next;
	void			*base;
	unsigned int		irq;
	unsigned int		divisor;
	char			name[32];
	char			phys[16];
	struct resource		*res;
};

static irqreturn_t amba_kmi_int(int irq, void *dev_id, struct pt_regs *regs)
{
	struct amba_kmi_port *kmi = dev_id;
	unsigned int status = readb(KMIIR);
	int handled = IRQ_NONE;

	while (status & KMIIR_RXINTR) {
		serio_interrupt(&kmi->io, readb(KMIDATA), 0, regs);
		status = readb(KMIIR);
		handled = IRQ_HANDLED;
	}

	return handled;
}

static int amba_kmi_write(struct serio *io, unsigned char val)
{
	struct amba_kmi_port *kmi = io->driver;
	unsigned int timeleft = 10000; /* timeout in 100ms */

	while ((readb(KMISTAT) & KMISTAT_TXEMPTY) == 0 && timeleft--)
		udelay(10);

	if (timeleft)
		writeb(val, KMIDATA);

	return timeleft ? 0 : SERIO_TIMEOUT;
}

static int amba_kmi_open(struct serio *io)
{
	struct amba_kmi_port *kmi = io->driver;
	int ret;

	writeb(kmi->divisor, KMICLKDIV);
	writeb(KMICR_EN, KMICR);

	ret = request_irq(kmi->irq, amba_kmi_int, 0, kmi->phys, kmi);
	if (ret) {
		printk(KERN_ERR "kmi: failed to claim IRQ%d\n", kmi->irq);
		writeb(0, KMICR);
		return ret;
	}

	writeb(KMICR_EN | KMICR_RXINTREN, KMICR);

	return 0;
}

static void amba_kmi_close(struct serio *io)
{
	struct amba_kmi_port *kmi = io->driver;

	writeb(0, KMICR);

	free_irq(kmi->irq, kmi);
}

static struct amba_kmi_port *list;

static int __init amba_kmi_init_one(char *type, unsigned long base, int irq, int nr)
{
	struct amba_kmi_port *kmi;

	kmi = kmalloc(sizeof(struct amba_kmi_port), GFP_KERNEL);
	if (!kmi)
		return -ENOMEM;

	memset(kmi, 0, sizeof(struct amba_kmi_port));

	kmi->io.type	= SERIO_8042;
	kmi->io.write	= amba_kmi_write;
	kmi->io.open	= amba_kmi_open;
	kmi->io.close	= amba_kmi_close;
	kmi->io.name	= kmi->name;
	kmi->io.phys	= kmi->phys;
	kmi->io.driver	= kmi;

	snprintf(kmi->name, sizeof(kmi->name), "AMBA KMI PS/2 %s port", type);
	snprintf(kmi->phys, sizeof(kmi->phys), "amba/serio%d", nr);

	kmi->res	= request_mem_region(base, KMI_SIZE, kmi->phys);
	if (!kmi->res) {
		kfree(kmi);
		return -EBUSY;
	}

	kmi->base	= ioremap(base, KMI_SIZE);
	if (!kmi->base) {
		release_resource(kmi->res);
		kfree(kmi);
		return -ENOMEM;
	}

	kmi->irq	= irq;
	kmi->divisor	= 24 / 8 - 1;

	kmi->next	= list;
	list		= kmi;

	serio_register_port(&kmi->io);
	return 0;
}

static int __init amba_kmi_init(void)
{
	amba_kmi_init_one("keyboard", KMI0_BASE, IRQ_KMIINT0, 0);
	amba_kmi_init_one("mouse", KMI1_BASE, IRQ_KMIINT1, 1);
	return 0;
}

static void __exit amba_kmi_exit(void)
{
	struct amba_kmi_port *kmi, *next;

	kmi = list;
	while (kmi) {
		next = kmi->next;

		serio_unregister_port(&kmi->io);
		iounmap(kmi->base);
		release_resource(kmi->res);
		kfree(kmi);

		kmi = next;
	}
}

module_init(amba_kmi_init);
module_exit(amba_kmi_exit);

MODULE_AUTHOR("Russell King <rmk@arm.linux.org.uk>");
MODULE_DESCRIPTION("AMBA KMI controller driver");
MODULE_LICENSE("GPL");
