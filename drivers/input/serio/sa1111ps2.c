/*
 *  linux/drivers/input/serio/sa1111ps2.c
 *
 *  Copyright (C) 2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/device.h>

#include <asm/hardware/sa1111.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>

extern struct pt_regs *kbd_pt_regs;

struct ps2if {
	struct serio	io;
	struct resource	*res;
	unsigned long	base;
	unsigned int	irq;
	unsigned int	skpcr_mask;
};

/*
 * Read all bytes waiting in the PS2 port.  There should be
 * at the most one, but we loop for safety.  If there was a
 * framing error, we have to manually clear the status.
 */
static void ps2_int(int irq, void *dev_id, struct pt_regs *regs)
{
	struct ps2if *sa = dev_id;
	unsigned int scancode, flag, status;

	kbd_pt_regs = regs;

	status = sa1111_readl(sa->base + SA1111_PS2STAT);
	while (status & PS2STAT_RXF) {
		if (status & PS2STAT_STP)
			sa1111_writel(PS2STAT_STP, sa->base + SA1111_PS2STAT);

		flag = (status & PS2STAT_STP ? SERIO_FRAME : 0) |
		       (status & PS2STAT_RXP ? 0 : SERIO_PARITY);

		scancode = sa1111_readl(sa->base + SA1111_PS2DATA) & 0xff;

		if (hweight8(scancode) & 1)
			flag ^= SERIO_PARITY;

		serio_interrupt(&sa->io, scancode, flag);

               	status = sa1111_readl(sa->base + SA1111_PS2STAT);
        }
}

/*
 * Write a byte to the PS2 port.  We have to wait for the
 * port to indicate that the transmitter is empty.
 */
static int ps2_write(struct serio *io, unsigned char val)
{
	struct ps2if *sa = io->driver;
	unsigned int timeleft = 10000; /* timeout in 100ms */

	while ((sa1111_readl(sa->base + SA1111_PS2STAT) & PS2STAT_TXE) == 0 &&
	       timeleft--)
		udelay(10);

	if (timeleft)
		sa1111_writel(val, sa->base + SA1111_PS2DATA);

	return timeleft ? 0 : SERIO_TIMEOUT;
}

static int ps2_open(struct serio *io)
{
	struct ps2if *sa = io->driver;
	int ret;

	sa1111_enable_device(sa->skpcr_mask);

	ret = request_irq(sa->irq, ps2_int, 0, "ps2", sa);
	if (ret) {
		printk(KERN_ERR "sa1111ps2: could not allocate IRQ%d: %d\n",
			sa->irq, ret);
		return ret;
	}

	sa1111_writel(PS2CR_ENA, sa->base + SA1111_PS2CR);

	return 0;
}

static void ps2_close(struct serio *io)
{
	struct ps2if *sa = io->driver;

	sa1111_writel(0, sa->base + SA1111_PS2CR);

	free_irq(sa->irq, sa);

	sa1111_disable_device(sa->skpcr_mask);
}

/*
 * Clear the input buffer.
 */
static void __init ps2_clear_input(struct ps2if *sa)
{
	int maxread = 100;

	while (maxread--) {
		if ((sa1111_readl(sa->base + SA1111_PS2DATA) & 0xff) == 0xff)
			break;
	}
}

static inline unsigned int
ps2_test_one(struct ps2if *sa, unsigned int mask)
{
	unsigned int val;

	sa1111_writel(PS2CR_ENA | mask, sa->base + SA1111_PS2CR);

	udelay(2);

	val = sa1111_readl(sa->base + SA1111_PS2STAT);
	return val & (PS2STAT_KBC | PS2STAT_KBD);
}

/*
 * Test the keyboard interface.  We basically check to make sure that
 * we can drive each line to the keyboard independently of each other.
 */
static int __init ps2_test(struct ps2if *sa)
{
	unsigned int stat;
	int ret = 0;

	stat = ps2_test_one(sa, PS2CR_FKC);
	if (stat != PS2STAT_KBD) {
		printk("Keyboard interface test failed[1]: %02x\n", stat);
		ret = -ENODEV;
	}

	stat = ps2_test_one(sa, 0);
	if (stat != (PS2STAT_KBC | PS2STAT_KBD)) {
		printk("Keyboard interface test failed[2]: %02x\n", stat);
		ret = -ENODEV;
	}

	stat = ps2_test_one(sa, PS2CR_FKD);
	if (stat != PS2STAT_KBC) {
		printk("Keyboard interface test failed[3]: %02x\n", stat);
		ret = -ENODEV;
	}

	sa1111_writel(0, sa->base + SA1111_PS2CR);

	return ret;
}

/*
 * Initialise one PS/2 port.
 */
static int __init ps2_init_one(struct sa1111_device *dev, struct ps2if *sa)
{
	int ret;

	/*
	 * Request the physical region for this PS2 port.
	 */
	sa->res = request_mem_region(_SA1111(sa->base), 512, "ps2");
	if (!sa->res)
		return -EBUSY;

	/*
	 * Convert the chip offset to virtual address.
	 */
	sa->base += (unsigned long)dev->base;

	sa1111_enable_device(sa->skpcr_mask);

	/* Incoming clock is 8MHz */
	sa1111_writel(0, sa->base + SA1111_PS2CLKDIV);
	sa1111_writel(127, sa->base + SA1111_PS2PRECNT);

	/*
	 * Flush any pending input.
	 */
	ps2_clear_input(sa);

	/*
	 * Test the keyboard interface.
	 */
	ret = ps2_test(sa);
	if (ret)
		goto out;

	/*
	 * Flush any pending input.
	 */
	ps2_clear_input(sa);
	sa1111_disable_device(sa->skpcr_mask);

	serio_register_port(&sa->io);
	return 0;

 out:
	sa1111_disable_device(sa->skpcr_mask);
	release_resource(sa->res);
	return ret;
}

/*
 * Remove one PS/2 port.
 */
static void __exit ps2_remove_one(struct ps2if *sa)
{
	serio_unregister_port(&sa->io);
 	release_resource(sa->res);
}

static struct ps2if ps2_kbd_port =
{
	io: {
		type:		SERIO_8042,
		write:		ps2_write,
		open:		ps2_open,
		close:		ps2_close,
		name: 		"SA1111 PS/2 kbd port",
		phys:		"sa1111/serio0",
		driver:		&ps2_kbd_port,
	},
	base:		SA1111_KBD,
	irq:		IRQ_TPRXINT,
	skpcr_mask:	SKPCR_PTCLKEN,
};

static struct ps2if ps2_mse_port =
{
	io: {
		type:		SERIO_8042,
		write:		ps2_write,
		open:		ps2_open,
		close:		ps2_close,
		name: 		"SA1111 PS/2 mouse port",
		phys:		"sa1111/serio1",
		driver:		&ps2_mse_port,
	},
	base:		SA1111_MSE,
	irq:		IRQ_MSRXINT,
	skpcr_mask:	SKPCR_PMCLKEN,
};

static int __init ps2_init(void)
{
	int ret = -ENODEV;

	if (sa1111) {
		ret = ps2_init_one(sa1111, &ps2_kbd_port);
	}

	return ret;
}

static void __exit ps2_exit(void)
{
	ps2_remove_one(&ps2_kbd_port);
}

module_init(ps2_init);
module_exit(ps2_exit);

MODULE_AUTHOR("Russell King <rmk@arm.linux.org.uk>");
MODULE_DESCRIPTION("SA1111 PS2 controller driver");
MODULE_LICENSE("GPL");
