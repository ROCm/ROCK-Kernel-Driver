/*
 *  drivers/input/keyboard/98kbd.c
 *
 *  PC-9801 keyboard driver for Linux
 *
 *    Based on atkbd.c and xtkbd.c written by Vojtech Pavlik
 *
 *  Copyright (c) 2002 Osamu Tomita
 *  Copyright (c) 1999-2001 Vojtech Pavlik
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
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/serio.h>

#include <asm/io.h>
#include <asm/pc9800.h>

MODULE_AUTHOR("Osamu Tomita <tomita@cinet.co.jp>");
MODULE_DESCRIPTION("PC-9801 keyboard driver");
MODULE_LICENSE("GPL");

#define KBD98_KEY	0x7f
#define KBD98_RELEASE	0x80

static unsigned char kbd98_keycode[256] = {	 
	  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 43, 14, 15,
	 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 41, 26, 28, 30, 31, 32,
	 33, 34, 35, 36, 37, 38, 39, 40, 27, 44, 45, 46, 47, 48, 49, 50,
	 51, 52, 53, 12, 57, 92,109,104,110,111,103,105,106,108,102,107,
	 74, 98, 71, 72, 73, 55, 75, 76, 77, 78, 79, 80, 81,117, 82,121,
	 83, 94, 87, 88,183,184,185,  0,  0,  0,  0,  0,  0,  0,102,  0,
	 99,133, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68,  0,  0,  0,  0,
	 54, 58, 42, 56, 29
};

struct jis_kbd_conv {
	unsigned char scancode;
	struct {
		unsigned char shift;
		unsigned char keycode;
	} emul[2];
};

static struct jis_kbd_conv kbd98_jis[] = {
	{0x02, {{0,   3}, {1,  40}}},
	{0x06, {{0,   7}, {1,   8}}},
	{0x07, {{0,   8}, {0,  40}}},
	{0x08, {{0,   9}, {1,  10}}},
	{0x09, {{0,  10}, {1,  11}}},
	{0x0a, {{0,  11}, {1, 255}}},
	{0x0b, {{0,  12}, {0,  13}}},
	{0x0c, {{1,   7}, {0,  41}}},
	{0x1a, {{1,   3}, {1,  41}}},
	{0x26, {{0,  39}, {1,  13}}},
	{0x27, {{1,  39}, {1,   9}}},
	{0x33, {{0, 255}, {1,  12}}},
	{0xff, {{0, 255}, {1, 255}}}	/* terminater */
};

#define KBD98_CMD_SETEXKEY	0x1095	/* Enable/Disable Windows, Appli key */
#define KBD98_CMD_SETRATE	0x109c	/* Set typematic rate */
#define KBD98_CMD_SETLEDS	0x109d	/* Set keyboard leds */
#define KBD98_CMD_GETLEDS	0x119d	/* Get keyboard leds */
#define KBD98_CMD_GETID		0x019f

#define KBD98_RET_ACK		0xfa
#define KBD98_RET_NAK		0xfc	/* Command NACK, send the cmd again */

#define KBD98_KEY_JIS_EMUL	253
#define KBD98_KEY_UNKNOWN	254
#define KBD98_KEY_NULL		255

static char *kbd98_name = "PC-9801 Keyboard";

struct kbd98 {
	unsigned char keycode[256];
	struct input_dev dev;
	struct serio *serio;
	char phys[32];
	unsigned char cmdbuf[4];
	unsigned char cmdcnt;
	signed char ack;
	unsigned char shift;
	struct {
		unsigned char scancode;
		unsigned char keycode;
	} emul;
	struct jis_kbd_conv jis[16];
};

irqreturn_t kbd98_interrupt(struct serio *serio, unsigned char data,
			    unsigned int flags, struct pt_regs *regs)
{
	struct kbd98 *kbd98 = serio->private;
	unsigned char scancode, keycode;
	int press, i;

	switch (data) {
		case KBD98_RET_ACK:
			kbd98->ack = 1;
			goto out;
		case KBD98_RET_NAK:
			kbd98->ack = -1;
			goto out;
	}

	if (kbd98->cmdcnt) {
		kbd98->cmdbuf[--kbd98->cmdcnt] = data;
		goto out;
	}

	scancode = data & KBD98_KEY;
	keycode = kbd98->keycode[scancode];
	press = !(data & KBD98_RELEASE);
	if (kbd98->emul.scancode != KBD98_KEY_UNKNOWN
	    && scancode != kbd98->emul.scancode) {
		input_report_key(&kbd98->dev, kbd98->emul.keycode, 0);
		kbd98->emul.scancode = KBD98_KEY_UNKNOWN;
	}

	if (keycode == KEY_RIGHTSHIFT)
		kbd98->shift = press;

	switch (keycode) {
		case KEY_2:
		case KEY_6:
		case KEY_7:
		case KEY_8:
		case KEY_9:
		case KEY_0:
		case KEY_MINUS:
		case KEY_EQUAL:
		case KEY_GRAVE:
		case KEY_SEMICOLON:
		case KEY_APOSTROPHE:
			/* emulation: JIS keyboard to US101 keyboard */
			i = 0;
			while (kbd98->jis[i].scancode != 0xff) {
				if (scancode == kbd98->jis[i].scancode)
					break;
				i ++;
			}

			keycode = kbd98->jis[i].emul[kbd98->shift].keycode;
			if (keycode == KBD98_KEY_NULL)
				break;

			if (press) {
				kbd98->emul.scancode = scancode;
				kbd98->emul.keycode = keycode;
				if (kbd98->jis[i].emul[kbd98->shift].shift
								!= kbd98->shift)
					input_report_key(&kbd98->dev,
							KEY_RIGHTSHIFT,
							!(kbd98->shift));
			}

			input_report_key(&kbd98->dev, keycode, press);
			if (!press) {
				if (kbd98->jis[i].emul[kbd98->shift].shift
								!= kbd98->shift)
					input_report_key(&kbd98->dev,
							KEY_RIGHTSHIFT,
							kbd98->shift);
				kbd98->emul.scancode = KBD98_KEY_UNKNOWN;
			}

			input_sync(&kbd98->dev);
			break;

		case KEY_CAPSLOCK:
			input_report_key(&kbd98->dev, keycode, 1);
			input_sync(&kbd98->dev);
			input_report_key(&kbd98->dev, keycode, 0);
			input_sync(&kbd98->dev);
			break;

		case KBD98_KEY_NULL:
			break;

		case 0:
			printk(KERN_WARNING "kbd98.c: Unknown key (scancode %#x) %s.\n",
				data & KBD98_KEY, data & KBD98_RELEASE ? "released" : "pressed");
			break;

		default:
			input_report_key(&kbd98->dev, keycode, press);
			input_sync(&kbd98->dev);
			break;
	}

out:
	return IRQ_HANDLED;
}

/*
 * kbd98_sendbyte() sends a byte to the keyboard, and waits for
 * acknowledge. It doesn't handle resends according to the keyboard
 * protocol specs, because if these are needed, the keyboard needs
 * replacement anyway, and they only make a mess in the protocol.
 */

static int kbd98_sendbyte(struct kbd98 *kbd98, unsigned char byte)
{
	int timeout = 10000; /* 100 msec */
	kbd98->ack = 0;

	if (serio_write(kbd98->serio, byte))
		return -1;

	while (!kbd98->ack && timeout--) udelay(10);

	return -(kbd98->ack <= 0);
}

/*
 * kbd98_command() sends a command, and its parameters to the keyboard,
 * then waits for the response and puts it in the param array.
 */

static int kbd98_command(struct kbd98 *kbd98, unsigned char *param, int command)
{
	int timeout = 50000; /* 500 msec */
	int send = (command >> 12) & 0xf;
	int receive = (command >> 8) & 0xf;
	int i;

	kbd98->cmdcnt = receive;
	
	if (command & 0xff)
		if (kbd98_sendbyte(kbd98, command & 0xff))
			return (kbd98->cmdcnt = 0) - 1;

	for (i = 0; i < send; i++)
		if (kbd98_sendbyte(kbd98, param[i]))
			return (kbd98->cmdcnt = 0) - 1;

	while (kbd98->cmdcnt && timeout--) udelay(10);

	if (param)
		for (i = 0; i < receive; i++)
			param[i] = kbd98->cmdbuf[(receive - 1) - i];

	if (kbd98->cmdcnt) 
		return (kbd98->cmdcnt = 0) - 1;

	return 0;
}

/*
 * Event callback from the input module. Events that change the state of
 * the hardware are processed here.
 */

static int kbd98_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct kbd98 *kbd98 = dev->private;
	char param[2];

	switch (type) {

		case EV_LED:

			if (__PC9800SCA_TEST_BIT(0x481, 3)) {
				/* 98note with Num Lock key */
				/* keep Num Lock status     */
				*param = 0x60;
				if (kbd98_command(kbd98, param,
							KBD98_CMD_GETLEDS))
					printk(KERN_DEBUG
						"kbd98: Get keyboard LED"
						" status Error\n");

				*param &= 1;
			} else {
				/* desktop PC-9801 */
				*param = 1;	/* Always set Num Lock */
			}

			*param |= 0x70
			       | (test_bit(LED_CAPSL,   dev->led) ? 4 : 0)
			       | (test_bit(LED_KANA,    dev->led) ? 8 : 0);
		        kbd98_command(kbd98, param, KBD98_CMD_SETLEDS);

			return 0;
	}

	return -1;
}

void kbd98_connect(struct serio *serio, struct serio_dev *dev)
{
	struct kbd98 *kbd98;
	int i;

	if ((serio->type & SERIO_TYPE) != SERIO_PC9800)
		return;

	if (!(kbd98 = kmalloc(sizeof(struct kbd98), GFP_KERNEL)))
		return;

	memset(kbd98, 0, sizeof(struct kbd98));
	kbd98->emul.scancode = KBD98_KEY_UNKNOWN;
	
	kbd98->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_LED) | BIT(EV_REP);
	kbd98->dev.ledbit[0] = BIT(LED_NUML) | BIT(LED_CAPSL) | BIT(LED_KANA);

	kbd98->serio = serio;

	init_input_dev(&kbd98->dev);
	kbd98->dev.keycode = kbd98->keycode;
	kbd98->dev.keycodesize = sizeof(unsigned char);
	kbd98->dev.keycodemax = ARRAY_SIZE(kbd98_keycode);
	kbd98->dev.event = kbd98_event;
	kbd98->dev.private = kbd98;

	serio->private = kbd98;

	if (serio_open(serio, dev)) {
		kfree(kbd98);
		return;
	}

	memcpy(kbd98->jis, kbd98_jis, sizeof(kbd98_jis));
	memcpy(kbd98->keycode, kbd98_keycode, sizeof(kbd98->keycode));
	for (i = 0; i < 255; i++)
		set_bit(kbd98->keycode[i], kbd98->dev.keybit);
	clear_bit(0, kbd98->dev.keybit);

	sprintf(kbd98->phys, "%s/input0", serio->phys);

	kbd98->dev.name = kbd98_name;
	kbd98->dev.phys = kbd98->phys;
	kbd98->dev.id.bustype = BUS_XTKBD;
	kbd98->dev.id.vendor = 0x0002;
	kbd98->dev.id.product = 0x0001;
	kbd98->dev.id.version = 0x0100;

	input_register_device(&kbd98->dev);

	printk(KERN_INFO "input: %s on %s\n", kbd98_name, serio->phys);
}

void kbd98_disconnect(struct serio *serio)
{
	struct kbd98 *kbd98 = serio->private;
	input_unregister_device(&kbd98->dev);
	serio_close(serio);
	kfree(kbd98);
}

struct serio_dev kbd98_dev = {
	.interrupt =	kbd98_interrupt,
	.connect =	kbd98_connect,
	.disconnect =	kbd98_disconnect
};

int __init kbd98_init(void)
{
	serio_register_device(&kbd98_dev);
	return 0;
}

void __exit kbd98_exit(void)
{
	serio_unregister_device(&kbd98_dev);
}

module_init(kbd98_init);
module_exit(kbd98_exit);
