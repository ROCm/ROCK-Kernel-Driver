/*
 *  NEC PC-9801 keyboard controller driver for Linux
 *
 *  Copyright (c) 1999-2002 Osamu Tomita <tomita@cinet.co.jp>
 *    Based on i8042.c written by Vojtech Pavlik
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/sched.h>

#include <asm/io.h>

MODULE_AUTHOR("Osamu Tomita <tomita@cinet.co.jp>");
MODULE_DESCRIPTION("NEC PC-9801 keyboard controller driver");
MODULE_LICENSE("GPL");

/*
 * Names.
 */

#define KBD98_PHYS_DESC "isa0041/serio0"

/*
 * IRQs.
 */

#define KBD98_IRQ	1

/*
 * Register numbers.
 */

#define KBD98_COMMAND_REG	0x43
#define KBD98_STATUS_REG	0x43
#define KBD98_DATA_REG		0x41

spinlock_t kbd98io_lock = SPIN_LOCK_UNLOCKED;

static struct serio kbd98_port;
extern struct pt_regs *kbd_pt_regs;

static void kbd98io_interrupt(int irq, void *dev_id, struct pt_regs *regs);

/*
 * kbd98_flush() flushes all data that may be in the keyboard buffers
 */

static int kbd98_flush(void)
{
	unsigned long flags;

	spin_lock_irqsave(&kbd98io_lock, flags);

	while (inb(KBD98_STATUS_REG) & 0x02) /* RxRDY */
		inb(KBD98_DATA_REG);

	if (inb(KBD98_STATUS_REG) & 0x38)
		printk("98kbd-io: Keyboard error!\n");

	spin_unlock_irqrestore(&kbd98io_lock, flags);

	return 0;
}

/*
 * kbd98_write() sends a byte out through the keyboard interface.
 */

static int kbd98_write(struct serio *port, unsigned char c)
{
	unsigned long flags;

	spin_lock_irqsave(&kbd98io_lock, flags);

	outb(0, 0x5f);			/* wait */
	outb(0x17, KBD98_COMMAND_REG);	/* enable send command */
	outb(0, 0x5f);			/* wait */
	outb(c, KBD98_DATA_REG);
	outb(0, 0x5f);			/* wait */
	outb(0x16, KBD98_COMMAND_REG);	/* disable send command */
	outb(0, 0x5f);			/* wait */

	spin_unlock_irqrestore(&kbd98io_lock, flags);

	return 0;
}

/*
 * kbd98_open() is called when a port is open by the higher layer.
 * It allocates the interrupt and enables in in the chip.
 */

static int kbd98_open(struct serio *port)
{
	kbd98_flush();

	if (request_irq(KBD98_IRQ, kbd98io_interrupt, 0, "kbd98", NULL)) {
		printk(KERN_ERR "98kbd-io.c: Can't get irq %d for %s, unregistering the port.\n", KBD98_IRQ, "KBD");
		serio_unregister_port(port);
		return -1;
	}

	return 0;
}

static void kbd98_close(struct serio *port)
{
	free_irq(KBD98_IRQ, NULL);

	kbd98_flush();
}

/*
 * Structures for registering the devices in the serio.c module.
 */

static struct serio kbd98_port =
{
	.type =		SERIO_PC9800,
	.write =	kbd98_write,
	.open =		kbd98_open,
	.close =	kbd98_close,
	.driver =	NULL,
	.name =		"PC-9801 Kbd Port",
	.phys =		KBD98_PHYS_DESC,
};

/*
 * kbd98io_interrupt() is the most important function in this driver -
 * it handles the interrupts from keyboard, and sends incoming bytes
 * to the upper layers.
 */

static void kbd98io_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long flags;
	unsigned char data;

	spin_lock_irqsave(&kbd98io_lock, flags);

	data = inb(KBD98_DATA_REG);
	spin_unlock_irqrestore(&kbd98io_lock, flags);
	serio_interrupt(&kbd98_port, data, 0, regs);

}

int __init kbd98io_init(void)
{
	serio_register_port(&kbd98_port);

	printk(KERN_INFO "serio: PC-9801 %s port at %#lx,%#lx irq %d\n",
	       "KBD",
	       (unsigned long) KBD98_DATA_REG,
	       (unsigned long) KBD98_COMMAND_REG,
	       KBD98_IRQ);

	return 0;
}

void __exit kbd98io_exit(void)
{
	serio_unregister_port(&kbd98_port);
}

module_init(kbd98io_init);
module_exit(kbd98io_exit);
