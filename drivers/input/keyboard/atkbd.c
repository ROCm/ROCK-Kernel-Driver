/*
 * $Id: atkbd.c,v 1.33 2002/02/12 09:34:34 vojtech Exp $
 *
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
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/tqueue.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("AT and PS/2 keyboard driver");
MODULE_PARM(atkbd_set, "1i");
MODULE_LICENSE("GPL");

static int atkbd_set = 2;

/*
 * Scancode to keycode tables. These are just the default setting, and
 * are loadable via an userland utility.
 */

static unsigned char atkbd_set2_keycode[512] = {
	  0, 67, 65, 63, 61, 59, 60, 88,  0, 68, 66, 64, 62, 15, 41, 85,
	  0, 56, 42,  0, 29, 16,  2, 89,  0,  0, 44, 31, 30, 17,  3, 90,
	  0, 46, 45, 32, 18,  5,  4, 91,  0, 57, 47, 33, 20, 19,  6,  0,
	  0, 49, 48, 35, 34, 21,  7,  0,  0,  0, 50, 36, 22,  8,  9,  0,
	  0, 51, 37, 23, 24, 11, 10,  0,  0, 52, 53, 38, 39, 25, 12,  0,
	122, 89, 40,120, 26, 13,  0,  0, 58, 54, 28, 27,  0, 43,  0,  0,
	 85, 86, 90, 91, 92, 93, 14, 94, 95, 79,  0, 75, 71,121,  0,123,
	 82, 83, 80, 76, 77, 72,  1, 69, 87, 78, 81, 74, 55, 73, 70, 99,
	252,  0,  0, 65, 99,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,251,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	252,253,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	254,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,255,
	  0,  0, 92, 90, 85,  0,137,  0,  0,  0,  0, 91, 89,144,115,  0,
	136,100,255,  0, 97,149,164,  0,156,  0,  0,140,115,  0,  0,125,
	  0,150,  0,154,152,163,151,126,112,166,  0,140,  0,147,  0,127,
	159,167,139,160,163,  0,  0,116,158,  0,150,165,  0,  0,  0,142,
	157,  0,114,166,168,  0,  0,  0,155,  0, 98,113,  0,148,  0,138,
	  0,  0,  0,  0,  0,  0,153,140,  0,255, 96,  0,  0,  0,143,  0,
	133,  0,116,  0,143,  0,174,133,  0,107,  0,105,102,  0,  0,112,
	110,111,108,112,106,103,  0,119,  0,118,109,  0, 99,104,119
};

static unsigned char atkbd_set3_keycode[512] = {
	  0,  0,  0,  0,  0,  0,  0, 59,  1,138,128,129,130, 15, 41, 60,
	131, 29, 42, 86, 58, 16,  2, 61,133, 56, 44, 31, 30, 17,  3, 62,
	134, 46, 45, 32, 18,  5,  4, 63,135, 57, 47, 33, 20, 19,  6, 64,
	136, 49, 48, 35, 34, 21,  7, 65,137,100, 50, 36, 22,  8,  9, 66,
	125, 51, 37, 23, 24, 11, 10, 67,126, 52, 53, 38, 39, 25, 12, 68,
	113,114, 40, 84, 26, 13, 87, 99,100, 54, 28, 27, 43, 84, 88, 70,
	108,105,119,103,111,107, 14,110,  0, 79,106, 75, 71,109,102,104,
	 82, 83, 80, 76, 77, 72, 69, 98,  0, 96, 81,  0, 78, 73, 55, 85,
	 89, 90, 91, 92, 74,  0,  0,  0,  0,  0,  0,125,126,127,112,  0,
	  0,139,150,163,165,115,152,150,166,140,160,154,113,114,167,168,
	148,149,147,140,  0,  0,  0,  0,  0,  0,251,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	252,253,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	254,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,255
};

#define ATKBD_CMD_SETLEDS	0x10ed
#define ATKBD_CMD_GSCANSET	0x11f0
#define ATKBD_CMD_SSCANSET	0x10f0
#define ATKBD_CMD_GETID		0x02f2
#define ATKBD_CMD_ENABLE	0x00f4
#define ATKBD_CMD_RESET_DIS	0x00f5
#define ATKBD_CMD_SETALL_MB	0x00f8
#define ATKBD_CMD_EX_ENABLE	0x10ea
#define ATKBD_CMD_EX_SETLEDS	0x20eb

#define ATKBD_RET_ACK		0xfa
#define ATKBD_RET_NAK		0xfe

#define ATKBD_KEY_UNKNOWN	  0
#define ATKBD_KEY_BAT		251
#define ATKBD_KEY_EMUL0		252
#define ATKBD_KEY_EMUL1		253
#define ATKBD_KEY_RELEASE	254
#define ATKBD_KEY_NULL		255

/*
 * The atkbd control structure
 */

struct atkbd {
	unsigned char keycode[512];
	struct input_dev dev;
	struct serio *serio;
	char name[64];
	char phys[32];
	unsigned char cmdbuf[4];
	unsigned char cmdcnt;
	unsigned char set;
	unsigned char release;
	signed char ack;
	unsigned char emul;
	unsigned short id;
};

/*
 * atkbd_interrupt(). Here takes place processing of data received from
 * the keyboard into events.
 */

static void atkbd_interrupt(struct serio *serio, unsigned char data, unsigned int flags)
{
	struct atkbd *atkbd = serio->private;
	int code = data;

#ifdef ATKBD_DEBUG
	printk(KERN_DEBUG "atkbd.c: Received %02x\n", data);
#endif

	switch (code) {
		case ATKBD_RET_ACK:
			atkbd->ack = 1;
			return;
		case ATKBD_RET_NAK:
			atkbd->ack = -1;
			return;
	}

	if (atkbd->cmdcnt) {
		atkbd->cmdbuf[--atkbd->cmdcnt] = code;
		return;
	}

	switch (atkbd->keycode[code]) {
		case ATKBD_KEY_BAT:
			serio_rescan(atkbd->serio);
			return;
		case ATKBD_KEY_EMUL0:
			atkbd->emul = 1;
			return;
		case ATKBD_KEY_EMUL1:
			atkbd->emul = 2;
			return;
		case ATKBD_KEY_RELEASE:
			atkbd->release = 1;
			return;
	}

	if (atkbd->emul) {
		if (--atkbd->emul) return;
		code |= 0x100;
	}

	switch (atkbd->keycode[code]) {
		case ATKBD_KEY_NULL:
			break;
		case ATKBD_KEY_UNKNOWN:
			printk(KERN_WARNING "atkbd.c: Unknown key (set %d, scancode %#x, on %s) %s.\n",
				atkbd->set, code, serio->phys, atkbd->release ? "released" : "pressed");
			break;
		default:
			input_report_key(&atkbd->dev, atkbd->keycode[code], !atkbd->release);
	}
		
	atkbd->release = 0;
}

/*
 * atkbd_sendbyte() sends a byte to the keyboard, and waits for
 * acknowledge. It doesn't handle resends according to the keyboard
 * protocol specs, because if these are needed, the keyboard needs
 * replacement anyway, and they only make a mess in the protocol.
 */

static int atkbd_sendbyte(struct atkbd *atkbd, unsigned char byte)
{
	int timeout = 10000; /* 100 msec */
	atkbd->ack = 0;

#ifdef ATKBD_DEBUG
	printk(KERN_DEBUG "atkbd.c: Sent: %02x\n", byte);
#endif
	serio_write(atkbd->serio, byte);
	while (!atkbd->ack && timeout--) udelay(10);

	return -(atkbd->ack <= 0);
}

/*
 * atkbd_command() sends a command, and its parameters to the keyboard,
 * then waits for the response and puts it in the param array.
 */

static int atkbd_command(struct atkbd *atkbd, unsigned char *param, int command)
{
	int timeout = 50000; /* 500 msec */
	int send = (command >> 12) & 0xf;
	int receive = (command >> 8) & 0xf;
	int i;

	atkbd->cmdcnt = receive;
	
	if (command & 0xff)
		if (atkbd_sendbyte(atkbd, command & 0xff))
			return (atkbd->cmdcnt = 0) - 1;

	for (i = 0; i < send; i++)
		if (atkbd_sendbyte(atkbd, param[i]))
			return (atkbd->cmdcnt = 0) - 1;

	while (atkbd->cmdcnt && timeout--) udelay(10);

	for (i = 0; i < receive; i++)
		param[i] = atkbd->cmdbuf[(receive - 1) - i];

	if (atkbd->cmdcnt) 
		return (atkbd->cmdcnt = 0) - 1;

	return 0;
}

/*
 * Event callback from the input module. Events that change the state of
 * the hardware are processed here.
 */

static int atkbd_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct atkbd *atkbd = dev->private;
	char param[2];

	if (!atkbd->serio->write)
		return -1;

	switch (type) {

		case EV_LED:

			*param = (test_bit(LED_SCROLLL, dev->led) ? 1 : 0)
			       | (test_bit(LED_NUML,    dev->led) ? 2 : 0)
			       | (test_bit(LED_CAPSL,   dev->led) ? 4 : 0);
		        atkbd_command(atkbd, param, ATKBD_CMD_SETLEDS);

			if (atkbd->set == 4) {
				param[0] = 0;
				param[1] = (test_bit(LED_COMPOSE, dev->led) ? 0x01 : 0)
					 | (test_bit(LED_SLEEP,   dev->led) ? 0x02 : 0)
					 | (test_bit(LED_SUSPEND, dev->led) ? 0x04 : 0)
				         | (test_bit(LED_MUTE,    dev->led) ? 0x20 : 0);
				atkbd_command(atkbd, param, ATKBD_CMD_EX_SETLEDS);
			}

			return 0;
	}

	return -1;
}

/*
 * atkbd_set_3 checks if a keyboard has a working Set 3 support, and
 * sets it into that. Unfortunately there are keyboards that can be switched
 * to Set 3, but don't work well in that (BTC Multimedia ...)
 */

static int atkbd_set_3(struct atkbd *atkbd)
{
	unsigned char param;

/*
 * For known special keyboards we can go ahead and set the correct set.
 */

	if (atkbd->id == 0xaca1) {
		param = 3;
		atkbd_command(atkbd, &param, ATKBD_CMD_SSCANSET);
		return 3;
	}

/*
 * We check for the extra keys on an some keyboards that need extra
 * command to get enabled. This shouldn't harm any keyboards not
 * knowing the command.
 */

	param = 0x71;
	if (!atkbd_command(atkbd, &param, ATKBD_CMD_EX_ENABLE))
		return 4;

/*
 * Try to set the set we want.
 */

	param = atkbd_set;
	if (atkbd_command(atkbd, &param, ATKBD_CMD_SSCANSET))
		return 2;

/*
 * Read set number. Beware here. Some keyboards always send '2'
 * or some other number regardless into what mode they have been
 * attempted to be set. Other keyboards treat the '0' command as
 * 'set to set 0', and not 'report current set' as they should.
 * In that case we time out, and return 2.
 */

	param = 0;
	if (atkbd_command(atkbd, &param, ATKBD_CMD_GSCANSET))
		return 2;

/*
 * Here we return the set number the keyboard reports about
 * itself.
 */

	return (param == 3) ? 3 : 2;
}

/*
 * atkbd_probe() probes for an AT keyboard on a serio port.
 */

static int atkbd_probe(struct atkbd *atkbd)
{
	unsigned char param[2];

/*
 * Full reset with selftest can on some keyboards be annoyingly slow,
 * so we just do a reset-and-disable on the keyboard, which
 * is considerably faster, but doesn't have to reset everything.
 */

	if (atkbd_command(atkbd, NULL, ATKBD_CMD_RESET_DIS))
		return -1;

/*
 * Next, we check if it's a keyboard. It should send 0xab83
 * (0xab84 on IBM ThinkPad, and 0xaca1 on a NCD Sun layout keyboard,
 * 0xab02 on unxlated i8042 and 0xab03 on unxlated ThinkPad, 0xab7f
 * on Fujitsu Lifebook).
 * If it's a mouse, it'll only send 0x00 (0x03 if it's MS mouse),
 * and we'll time out here, and report an error.
 */

	param[0] = param[1] = 0;

	if (atkbd_command(atkbd, param, ATKBD_CMD_GETID))
		return -1;

	atkbd->id = (param[0] << 8) | param[1];

	if (atkbd->id != 0xab83 && atkbd->id != 0xab84 && atkbd->id != 0xaca1 &&
	    atkbd->id != 0xab7f && atkbd->id != 0xab02 && atkbd->id != 0xab03)
		printk(KERN_WARNING "atkbd.c: Unusual keyboard ID: %#x on %s\n",
			atkbd->id, atkbd->serio->phys);

	return 0;
}

/*
 * atkbd_initialize() sets the keyboard into a sane state.
 */

static void atkbd_initialize(struct atkbd *atkbd)
{
	unsigned char param;	

/*
 * Disable autorepeat. We don't need it, as we do it in software anyway,
 * because that way can get faster repeat, and have less system load
 * (less accesses to the slow ISA hardware). If this fails, we don't care,
 * and will just ignore the repeated keys.
 */

	atkbd_command(atkbd, NULL, ATKBD_CMD_SETALL_MB);

/*
 * We also shut off all the leds. The console code will turn them back on,
 * if needed.
 */

	param = 0;
	atkbd_command(atkbd, &param, ATKBD_CMD_SETLEDS);

/*
 * Last, we enable the keyboard so that we get keypresses from it.
 */

	if (atkbd_command(atkbd, NULL, ATKBD_CMD_ENABLE))
		printk(KERN_ERR "atkbd.c: Failed to enable keyboard on %s\n",
			atkbd->serio->phys);
}

/*
 * atkbd_disconnect() cleans up behind us ...
 */

static void atkbd_disconnect(struct serio *serio)
{
	struct atkbd *atkbd = serio->private;
	input_unregister_device(&atkbd->dev);
	serio_close(serio);
	kfree(atkbd);
}

/*
 * atkbd_connect() is called when the serio module finds and interface
 * that isn't handled yet by an appropriate device driver. We check if
 * there is an AT keyboard out there and if yes, we register ourselves
 * to the input module.
 */

static void atkbd_connect(struct serio *serio, struct serio_dev *dev)
{
	struct atkbd *atkbd;
	int i;

	if ((serio->type & SERIO_TYPE) != SERIO_8042)
		return;

	if (!(atkbd = kmalloc(sizeof(struct atkbd), GFP_KERNEL)))
		return;

	memset(atkbd, 0, sizeof(struct atkbd));

	if (serio->write) {
		atkbd->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_LED) | BIT(EV_REP);
		atkbd->dev.ledbit[0] = BIT(LED_NUML) | BIT(LED_CAPSL) | BIT(LED_SCROLLL);
	} else  atkbd->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_REP);

	atkbd->serio = serio;

	atkbd->dev.keycode = atkbd->keycode;
	atkbd->dev.event = atkbd_event;
	atkbd->dev.private = atkbd;

	serio->private = atkbd;

	if (serio_open(serio, dev)) {
		kfree(atkbd);
		return;
	}

	if (serio->write) {

		if (atkbd_probe(atkbd)) {
			serio_close(serio);
			kfree(atkbd);
			return;
		}
		
		atkbd->set = atkbd_set_3(atkbd);

	} else {
		atkbd->set = 2;
		atkbd->id = 0xab00;
	}

	if (atkbd->set == 4) {
		atkbd->dev.ledbit[0] |= BIT(LED_COMPOSE) | BIT(LED_SUSPEND) | BIT(LED_SLEEP) | BIT(LED_MUTE);
		sprintf(atkbd->name, "AT Set 2 Extended keyboard");
	} else
		sprintf(atkbd->name, "AT Set %d keyboard", atkbd->set);

	sprintf(atkbd->phys, "%s/input0", serio->phys);

	if (atkbd->set == 3)
		memcpy(atkbd->keycode, atkbd_set3_keycode, sizeof(atkbd->keycode));
	else
		memcpy(atkbd->keycode, atkbd_set2_keycode, sizeof(atkbd->keycode));

	atkbd->dev.name = atkbd->name;
	atkbd->dev.phys = atkbd->phys;
	atkbd->dev.id.bustype = BUS_I8042;
	atkbd->dev.id.vendor = 0x0001;
	atkbd->dev.id.product = atkbd->set;
	atkbd->dev.id.version = atkbd->id;

	for (i = 0; i < 512; i++)
		if (atkbd->keycode[i] && atkbd->keycode[i] <= 250)
			set_bit(atkbd->keycode[i], atkbd->dev.keybit);

	input_register_device(&atkbd->dev);

	printk(KERN_INFO "input: %s on %s\n", atkbd->name, serio->phys);

	if (serio->write)
		atkbd_initialize(atkbd);
}


static struct serio_dev atkbd_dev = {
	interrupt:	atkbd_interrupt,
	connect:	atkbd_connect,
	disconnect:	atkbd_disconnect
};

#ifndef MODULE
static int __init atkbd_setup(char *str)
{
        int ints[4];
        str = get_options(str, ARRAY_SIZE(ints), ints);
        if (ints[0] > 0) atkbd_set = ints[1];
        return 1;
}
__setup("atkbd_set=", atkbd_setup);
#endif

int __init atkbd_init(void)
{
	serio_register_device(&atkbd_dev);
	return 0;
}

void __exit atkbd_exit(void)
{
	serio_unregister_device(&atkbd_dev);
}

module_init(atkbd_init);
module_exit(atkbd_exit);
