/*
 * $Id: amikbd.c,v 1.13 2002/02/01 16:02:24 vojtech Exp $
 *
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Hamish Macdonald
 */

/*
 * Amiga keyboard driver for Linux/m68k
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include <asm/amigaints.h>
#include <asm/amigahw.h>
#include <asm/irq.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Amiga keyboard driver");
MODULE_LICENSE("GPL");

static unsigned char amikbd_keycode[0x78] = {
	 41,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 43,  0, 82,
	 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,  0, 79, 80, 81,
	 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,  0,  0, 75, 76, 77,
	  0, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,  0, 83, 71, 72, 73,
	 57, 14, 15, 96, 28,  1,111,  0,  0,  0, 74,  0,103,108,106,105,
	 59, 60, 61, 62, 63, 64, 65, 66, 67, 68,179,180, 98, 55, 78,138,
	 42, 54, 58, 29, 56,100,125,126
};

static const char *amikbd_messages[8] = {
	KERN_ALERT "amikbd: Ctrl-Amiga-Amiga reset warning!!\n",
	KERN_WARNING "amikbd: keyboard lost sync\n",
	KERN_WARNING "amikbd: keyboard buffer overflow\n",
	KERN_WARNING "amikbd: keyboard controller failure\n",
	KERN_ERR "amikbd: keyboard selftest failure\n",
	KERN_INFO "amikbd: initiate power-up key stream\n",
	KERN_INFO "amikbd: terminate power-up key stream\n",
	KERN_WARNING "amikbd: keyboard interrupt\n"
};

static struct input_dev amikbd_dev;

static char *amikbd_name = "Amiga keyboard";
static char *amikbd_phys = "amikbd/input0";

static irqreturn_t amikbd_interrupt(int irq, void *dummy, struct pt_regs *fp)
{
	unsigned char scancode, down;

	scancode = ~ciaa.sdr;		/* get and invert scancode (keyboard is active low) */
	ciaa.cra |= 0x40;		/* switch SP pin to output for handshake */
	udelay(85);			/* wait until 85 us have expired */
	ciaa.cra &= ~0x40;		/* switch CIA serial port to input mode */

	down = !(scancode & 1);		/* lowest bit is release bit */
	scancode >>= 1;

	if (scancode < 0x78) {		/* scancodes < 0x78 are keys */

		scancode = amikbd_keycode[scancode];

		input_regs(&amikbd_dev, fp);

		if (scancode == KEY_CAPSLOCK) {	/* CapsLock is a toggle switch key on Amiga */
			input_report_key(&amikbd_dev, scancode, 1);
			input_report_key(&amikbd_dev, scancode, 0);
			input_sync(&amikbd_dev);
		} else {
			input_report_key(&amikbd_dev, scancode, down);
			input_sync(&amikbd_dev);
		}
	} else				/* scancodes >= 0x78 are error codes */
		printk(amikbd_messages[scancode - 0x78]);

	return IRQ_HANDLED;
}

static int __init amikbd_init(void)
{
	int i;

	if (!AMIGAHW_PRESENT(AMI_KEYBOARD))
		return -EIO;

	if (!request_mem_region(CIAA_PHYSADDR-1+0xb00, 0x100, "amikeyb"))
		return -EBUSY;

	init_input_dev(&amikbd_dev);

	amikbd_dev.evbit[0] = BIT(EV_KEY) | BIT(EV_REP);
	amikbd_dev.keycode = amikbd_keycode;
	amikbd_dev.keycodesize = sizeof(unsigned char);
	amikbd_dev.keycodemax = ARRAY_SIZE(amikbd_keycode);

	for (i = 0; i < 0x78; i++)
		if (amikbd_keycode[i])
			set_bit(amikbd_keycode[i], amikbd_dev.keybit);

	ciaa.cra &= ~0x41;	 /* serial data in, turn off TA */
	request_irq(IRQ_AMIGA_CIAA_SP, amikbd_interrupt, 0, "amikbd", amikbd_interrupt);

	amikbd_dev.name = amikbd_name;
	amikbd_dev.phys = amikbd_phys;
	amikbd_dev.id.bustype = BUS_AMIGA;
	amikbd_dev.id.vendor = 0x0001;
	amikbd_dev.id.product = 0x0001;
	amikbd_dev.id.version = 0x0100;

	input_register_device(&amikbd_dev);

	printk(KERN_INFO "input: %s\n", amikbd_name);

	return 0;
}

static void __exit amikbd_exit(void)
{
	input_unregister_device(&amikbd_dev);
	free_irq(IRQ_AMIGA_CIAA_SP, amikbd_interrupt);
	release_mem_region(CIAA_PHYSADDR-1+0xb00, 0x100);
}

module_init(amikbd_init);
module_exit(amikbd_exit);
