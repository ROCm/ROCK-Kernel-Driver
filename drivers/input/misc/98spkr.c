/*
 *  PC-9800 Speaker beeper driver for Linux
 *
 *  Copyright (c) 2002 Osamu Tomita
 *  Copyright (c) 2002 Vojtech Pavlik
 *  Copyright (c) 1992 Orest Zborowski
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <asm/io.h>

MODULE_AUTHOR("Osamu Tomita <tomita@cinet.co.jp>");
MODULE_DESCRIPTION("PC-9800 Speaker beeper driver");
MODULE_LICENSE("GPL");

static char spkr98_name[] = "PC-9801 Speaker";
static char spkr98_phys[] = "isa3fdb/input0";
static struct input_dev spkr98_dev;

spinlock_t i8253_beep_lock = SPIN_LOCK_UNLOCKED;

static int spkr98_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	unsigned int count = 0;
	unsigned long flags;

	if (type != EV_SND)
		return -1;

	switch (code) {
		case SND_BELL: if (value) value = 1000;
		case SND_TONE: break;
		default: return -1;
	}

	if (value > 20 && value < 32767)
		count = CLOCK_TICK_RATE / value;

	spin_lock_irqsave(&i8253_beep_lock, flags);

	if (count) {
		outb(0x76, 0x3fdf);
		outb(0, 0x5f);
		outb(count & 0xff, 0x3fdb);
		outb(0, 0x5f);
		outb((count >> 8) & 0xff, 0x3fdb);
		/* beep on */
		outb(6, 0x37);
	} else {
		/* beep off */
		outb(7, 0x37);
	}

	spin_unlock_irqrestore(&i8253_beep_lock, flags);

	return 0;
}

static int __init spkr98_init(void)
{
	spkr98_dev.evbit[0] = BIT(EV_SND);
	spkr98_dev.sndbit[0] = BIT(SND_BELL) | BIT(SND_TONE);
	spkr98_dev.event = spkr98_event;

	spkr98_dev.name = spkr98_name;
	spkr98_dev.phys = spkr98_phys;
	spkr98_dev.id.bustype = BUS_ISA;
	spkr98_dev.id.vendor = 0x001f;
	spkr98_dev.id.product = 0x0001;
	spkr98_dev.id.version = 0x0100;

	input_register_device(&spkr98_dev);

        printk(KERN_INFO "input: %s\n", spkr98_name);

	return 0;
}

static void __exit spkr98_exit(void)
{
        input_unregister_device(&spkr98_dev);
}

module_init(spkr98_init);
module_exit(spkr98_exit);
