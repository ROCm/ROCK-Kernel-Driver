/*
 * SGI O2 MACE PS2 controller driver for linux
 *
 * Copyright (C) 2002 Vivien Chappelier
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/ip32/mace.h>
#include <asm/ip32/ip32_ints.h>

MODULE_AUTHOR("Vivien Chappelier <vivien.chappelier@linux-mips.org");
MODULE_DESCRIPTION("SGI O2 MACE PS2 controller driver");
MODULE_LICENSE("GPL");

#define MACE_PS2_TIMEOUT 10000 /* in 50us unit */

#define PS2_STATUS_CLOCK_SIGNAL  BIT(0) /* external clock signal */
#define PS2_STATUS_CLOCK_INHIBIT BIT(1) /* clken output signal */
#define PS2_STATUS_TX_INPROGRESS BIT(2) /* transmission in progress */
#define PS2_STATUS_TX_EMPTY      BIT(3) /* empty transmit buffer */
#define PS2_STATUS_RX_FULL       BIT(4) /* full receive buffer */
#define PS2_STATUS_RX_INPROGRESS BIT(5) /* reception in progress */
#define PS2_STATUS_ERROR_PARITY  BIT(6) /* parity error */
#define PS2_STATUS_ERROR_FRAMING BIT(7) /* framing error */

#define PS2_CONTROL_TX_CLOCK_DISABLE BIT(0) /* inhibit clock signal after TX */
#define PS2_CONTROL_TX_ENABLE        BIT(1) /* transmit enable */
#define PS2_CONTROL_TX_INT_ENABLE    BIT(2) /* enable transmit interrupt */
#define PS2_CONTROL_RX_INT_ENABLE    BIT(3) /* enable receive interrupt */
#define PS2_CONTROL_RX_CLOCK_ENABLE  BIT(4) /* pause reception if set to 0 */
#define PS2_CONTROL_RESET            BIT(5) /* reset */


struct maceps2_data {
	struct mace_ps2port *port;
	int irq;
};

static int maceps2_write(struct serio *dev, unsigned char val)
{
	struct mace_ps2port *port = ((struct maceps2_data *)dev->driver)->port;
	unsigned int timeout = MACE_PS2_TIMEOUT;

	do {
		if (mace_read(port->status) & PS2_STATUS_TX_EMPTY) {
			mace_write(val, port->tx);
			return 0;
		}
		udelay(50);
	} while (timeout--);

	return -1;
}

static irqreturn_t maceps2_interrupt(int irq, void *dev_id,
				     struct pt_regs *regs)
{
	struct serio *dev = dev_id;
	struct mace_ps2port *port = ((struct maceps2_data *)dev->driver)->port;
	unsigned int byte;

	if (mace_read(port->status) & PS2_STATUS_RX_FULL) {
		byte = mace_read(port->rx);
		serio_interrupt(dev, byte & 0xff, 0, regs);
        }

	return IRQ_HANDLED;
}

static int maceps2_open(struct serio *dev)
{
	struct maceps2_data *data = (struct maceps2_data *)dev->driver;

	if (request_irq(data->irq, maceps2_interrupt, 0, "PS/2 port", dev)) {
		printk(KERN_ERR "Could not allocate PS/2 IRQ\n");
		return -EBUSY;
	}

	/* Reset port */
	mace_write(PS2_CONTROL_TX_CLOCK_DISABLE | PS2_CONTROL_RESET,
		   data->port->control);
	udelay(100);

        /* Enable interrupts */
	mace_write(PS2_CONTROL_RX_CLOCK_ENABLE | PS2_CONTROL_TX_ENABLE |
		   PS2_CONTROL_RX_INT_ENABLE, data->port->control);

	return 0;
}

static void maceps2_close(struct serio *dev)
{
	struct maceps2_data *data = (struct maceps2_data *)dev->driver;

	mace_write(PS2_CONTROL_TX_CLOCK_DISABLE | PS2_CONTROL_RESET,
		   data->port->control);
	udelay(100);
	free_irq(data->irq, dev);
}

static struct maceps2_data port0_data, port1_data;

static struct serio maceps2_port0 =
{
	.type	= SERIO_8042,
	.open	= maceps2_open,
	.close	= maceps2_close,
	.write	= maceps2_write,
	.name	= "MACE PS/2 port0",
	.phys	= "mace/serio0",
	.driver = &port0_data,
};

static struct serio maceps2_port1 =
{
	.type	= SERIO_8042,
	.open	= maceps2_open,
	.close	= maceps2_close,
	.write	= maceps2_write,
	.name	= "MACE PS/2 port1",
	.phys	= "mace/serio1",
	.driver = &port1_data,
};

static int __init maceps2_init(void)
{
	port0_data.port = &mace->perif.ps2.keyb;
	port0_data.irq  = MACEISA_KEYB_IRQ;
	port1_data.port = &mace->perif.ps2.mouse;
	port1_data.irq  = MACEISA_MOUSE_IRQ;
	serio_register_port(&maceps2_port0);
	serio_register_port(&maceps2_port1);

	return 0;
}

static void __exit maceps2_exit(void)
{
	serio_unregister_port(&maceps2_port0);
	serio_unregister_port(&maceps2_port1);
}

module_init(maceps2_init);
module_exit(maceps2_exit);
