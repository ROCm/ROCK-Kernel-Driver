/*
 * AT and PS/2 keyboard driver
 *
 * Copyright (c) 1999-2002 Vojtech Pavlik
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

/*
 * This driver can handle standard AT keyboards and PS/2 keyboards in
 * Translated and Raw Set 2 and Set 3, as well as AT keyboards on dumb
 * input-only controllers and AT keyboards connected over a one way RS232
 * converter.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/workqueue.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION("AT and PS/2 keyboard driver");
MODULE_LICENSE("GPL");

static int atkbd_set = 2;
module_param_named(set, atkbd_set, int, 0);
MODULE_PARM_DESC(set, "Select keyboard code set (2 = default, 3 = PS/2 native)");

#if defined(__i386__) || defined(__x86_64__) || defined(__hppa__)
static int atkbd_reset;
#else
static int atkbd_reset = 1;
#endif
module_param_named(reset, atkbd_reset, bool, 0);
MODULE_PARM_DESC(reset, "Reset keyboard during initialization");

static int atkbd_softrepeat;
module_param_named(softrepeat, atkbd_softrepeat, bool, 0);
MODULE_PARM_DESC(softrepeat, "Use software keyboard repeat");

static int atkbd_scroll;
module_param_named(scroll, atkbd_scroll, bool, 0);
MODULE_PARM_DESC(scroll, "Enable scroll-wheel on MS Office and similar keyboards");

static int atkbd_extra;
module_param_named(extra, atkbd_extra, bool, 0);
MODULE_PARM_DESC(extra, "Enable extra LEDs and keys on IBM RapidAcces, EzKey and similar keyboards");

__obsolete_setup("atkbd_set=");
__obsolete_setup("atkbd_reset");
__obsolete_setup("atkbd_softrepeat=");

/*
 * Scancode to keycode tables. These are just the default setting, and
 * are loadable via an userland utility.
 */

#if defined(__hppa__)
#include "hpps2atkbd.h"
#else

static unsigned char atkbd_set2_keycode[512] = {

	  0, 67, 65, 63, 61, 59, 60, 88,  0, 68, 66, 64, 62, 15, 41,117,
	  0, 56, 42, 93, 29, 16,  2,  0,  0,  0, 44, 31, 30, 17,  3,  0,
	  0, 46, 45, 32, 18,  5,  4, 95,  0, 57, 47, 33, 20, 19,  6,183,
	  0, 49, 48, 35, 34, 21,  7,184,  0,  0, 50, 36, 22,  8,  9,185,
	  0, 51, 37, 23, 24, 11, 10,  0,  0, 52, 53, 38, 39, 25, 12,  0,
	  0, 89, 40,  0, 26, 13,  0,  0, 58, 54, 28, 27,  0, 43,  0, 85,
	  0, 86, 91, 90, 92,  0, 14, 94,  0, 79,124, 75, 71,121,  0,  0,
	 82, 83, 80, 76, 77, 72,  1, 69, 87, 78, 81, 74, 55, 73, 70, 99,

	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	217,100,255,  0, 97,165,  0,  0,156,  0,  0,  0,  0,  0,  0,125,
	173,114,  0,113,  0,  0,  0,126,128,  0,  0,140,  0,  0,  0,127,
	159,  0,115,  0,164,  0,  0,116,158,  0,150,166,  0,  0,  0,142,
	157,  0,  0,  0,  0,  0,  0,  0,155,  0, 98,  0,  0,163,  0,  0,
	226,  0,  0,  0,  0,  0,  0,  0,  0,255, 96,  0,  0,  0,143,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,107,  0,105,102,  0,  0,112,
	110,111,108,112,106,103,  0,119,  0,118,109,  0, 99,104,119,  0,

	  0,  0,  0, 65, 99,
};

#endif

static unsigned char atkbd_set3_keycode[512] = {

	  0,  0,  0,  0,  0,  0,  0, 59,  1,138,128,129,130, 15, 41, 60,
	131, 29, 42, 86, 58, 16,  2, 61,133, 56, 44, 31, 30, 17,  3, 62,
	134, 46, 45, 32, 18,  5,  4, 63,135, 57, 47, 33, 20, 19,  6, 64,
	136, 49, 48, 35, 34, 21,  7, 65,137,100, 50, 36, 22,  8,  9, 66,
	125, 51, 37, 23, 24, 11, 10, 67,126, 52, 53, 38, 39, 25, 12, 68,
	113,114, 40, 43, 26, 13, 87, 99, 97, 54, 28, 27, 43, 43, 88, 70,
	108,105,119,103,111,107, 14,110,  0, 79,106, 75, 71,109,102,104,
	 82, 83, 80, 76, 77, 72, 69, 98,  0, 96, 81,  0, 78, 73, 55,183,

	184,185,186,187, 74, 94, 92, 93,  0,  0,  0,125,126,127,112,  0,
	  0,139,150,163,165,115,152,150,166,140,160,154,113,114,167,168,
	148,149,147,140
};

static unsigned char atkbd_unxlate_table[128] = {
          0,118, 22, 30, 38, 37, 46, 54, 61, 62, 70, 69, 78, 85,102, 13,
         21, 29, 36, 45, 44, 53, 60, 67, 68, 77, 84, 91, 90, 20, 28, 27,
         35, 43, 52, 51, 59, 66, 75, 76, 82, 14, 18, 93, 26, 34, 33, 42,
         50, 49, 58, 65, 73, 74, 89,124, 17, 41, 88,  5,  6,  4, 12,  3,
         11,  2, 10,  1,  9,119,126,108,117,125,123,107,115,116,121,105,
        114,122,112,113,127, 96, 97,120,  7, 15, 23, 31, 39, 47, 55, 63,
         71, 79, 86, 94,  8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 87,111,
         19, 25, 57, 81, 83, 92, 95, 98, 99,100,101,103,104,106,109,110
};

#define ATKBD_CMD_SETLEDS	0x10ed
#define ATKBD_CMD_GSCANSET	0x11f0
#define ATKBD_CMD_SSCANSET	0x10f0
#define ATKBD_CMD_GETID		0x02f2
#define ATKBD_CMD_SETREP	0x10f3
#define ATKBD_CMD_ENABLE	0x00f4
#define ATKBD_CMD_RESET_DIS	0x00f5
#define ATKBD_CMD_SETALL_MBR	0x00fa
#define ATKBD_CMD_RESET_BAT	0x02ff
#define ATKBD_CMD_RESEND	0x00fe
#define ATKBD_CMD_EX_ENABLE	0x10ea
#define ATKBD_CMD_EX_SETLEDS	0x20eb
#define ATKBD_CMD_OK_GETID	0x02e8


#define ATKBD_RET_ACK		0xfa
#define ATKBD_RET_NAK		0xfe
#define ATKBD_RET_BAT		0xaa
#define ATKBD_RET_EMUL0		0xe0
#define ATKBD_RET_EMUL1		0xe1
#define ATKBD_RET_RELEASE	0xf0
#define ATKBD_RET_HANGUEL	0xf1
#define ATKBD_RET_HANJA		0xf2
#define ATKBD_RET_ERR		0xff

#define ATKBD_KEY_UNKNOWN	  0
#define ATKBD_KEY_NULL		255

#define ATKBD_SCR_1		254
#define ATKBD_SCR_2		253
#define ATKBD_SCR_4		252
#define ATKBD_SCR_8		251
#define ATKBD_SCR_CLICK		250

#define ATKBD_SPECIAL		250

static unsigned char atkbd_scroll_keys[5][2] = {
	{ ATKBD_SCR_1,     0x45 },
	{ ATKBD_SCR_2,     0x29 },
	{ ATKBD_SCR_4,     0x36 },
	{ ATKBD_SCR_8,     0x27 },
	{ ATKBD_SCR_CLICK, 0x60 },
};

/*
 * The atkbd control structure
 */

struct atkbd {
	unsigned char keycode[512];
	struct input_dev dev;
	struct serio *serio;

	char name[64];
	char phys[32];
	unsigned short id;
	unsigned char set;
	unsigned int translated:1;
	unsigned int extra:1;
	unsigned int write:1;

	unsigned char cmdbuf[4];
	unsigned char cmdcnt;
	volatile signed char ack;
	unsigned char emul;
	unsigned int resend:1;
	unsigned int release:1;
	unsigned int bat_xl:1;

	unsigned int last;
	unsigned long time;
};

static void atkbd_report_key(struct input_dev *dev, struct pt_regs *regs, int code, int value)
{
	input_regs(dev, regs);
	if (value == 3) {
		input_report_key(dev, code, 1);
		input_report_key(dev, code, 0);
	} else
		input_event(dev, EV_KEY, code, value);
	input_sync(dev);
}

/*
 * atkbd_interrupt(). Here takes place processing of data received from
 * the keyboard into events.
 */

static irqreturn_t atkbd_interrupt(struct serio *serio, unsigned char data,
			unsigned int flags, struct pt_regs *regs)
{
	struct atkbd *atkbd = serio->private;
	unsigned int code = data;
	int scroll = 0, click = -1;
	int value;

#ifdef ATKBD_DEBUG
	printk(KERN_DEBUG "atkbd.c: Received %02x flags %02x\n", data, flags);
#endif

#if !defined(__i386__) && !defined (__x86_64__)
	if ((flags & (SERIO_FRAME | SERIO_PARITY)) && (~flags & SERIO_TIMEOUT) && !atkbd->resend && atkbd->write) {
		printk("atkbd.c: frame/parity error: %02x\n", flags);
		serio_write(serio, ATKBD_CMD_RESEND);
		atkbd->resend = 1;
		goto out;
	}

	if (!flags && data == ATKBD_RET_ACK)
		atkbd->resend = 0;
#endif

	if (!atkbd->ack)
		switch (code) {
			case ATKBD_RET_ACK:
				atkbd->ack = 1;
				goto out;
			case ATKBD_RET_NAK:
				atkbd->ack = -1;
				goto out;
		}

	if (atkbd->cmdcnt) {
		atkbd->cmdbuf[--atkbd->cmdcnt] = code;
		goto out;
	}

	if (atkbd->translated) {

		if (atkbd->emul ||
		    !(code == ATKBD_RET_EMUL0 || code == ATKBD_RET_EMUL1 ||
		      code == ATKBD_RET_HANGUEL || code == ATKBD_RET_HANJA ||
		      code == ATKBD_RET_ERR ||
	             (code == ATKBD_RET_BAT && !atkbd->bat_xl))) {
			atkbd->release = code >> 7;
			code &= 0x7f;
		}

		if (!atkbd->emul &&
		     (code & 0x7f) == (ATKBD_RET_BAT & 0x7f))
			atkbd->bat_xl = !atkbd->release;
	}

	switch (code) {
		case ATKBD_RET_BAT:
			serio_rescan(atkbd->serio);
			goto out;
		case ATKBD_RET_EMUL0:
			atkbd->emul = 1;
			goto out;
		case ATKBD_RET_EMUL1:
			atkbd->emul = 2;
			goto out;
		case ATKBD_RET_RELEASE:
			atkbd->release = 1;
			goto out;
		case ATKBD_RET_HANGUEL:
			atkbd_report_key(&atkbd->dev, regs, KEY_HANGUEL, 3);
			goto out;
		case ATKBD_RET_HANJA:
			atkbd_report_key(&atkbd->dev, regs, KEY_HANJA, 3);
			goto out;
		case ATKBD_RET_ERR:
			printk(KERN_WARNING "atkbd.c: Keyboard on %s reports too many keys pressed.\n", serio->phys);
			goto out;
	}

	if (atkbd->set != 3)
		code = (code & 0x7f) | ((code & 0x80) << 1);
	if (atkbd->emul) {
		if (--atkbd->emul)
			goto out;
		code |= (atkbd->set != 3) ? 0x80 : 0x100;
	}

	switch (atkbd->keycode[code]) {
		case ATKBD_KEY_NULL:
			break;
		case ATKBD_KEY_UNKNOWN:
			if (data == ATKBD_RET_ACK || data == ATKBD_RET_NAK) {
				printk(KERN_WARNING "atkbd.c: Spurious %s on %s. Some program, "
				       "like XFree86, might be trying access hardware directly.\n",
				       data == ATKBD_RET_ACK ? "ACK" : "NAK", serio->phys);
			} else {
				printk(KERN_WARNING "atkbd.c: Unknown key %s "
				       "(%s set %d, code %#x on %s).\n",
				       atkbd->release ? "released" : "pressed",
				       atkbd->translated ? "translated" : "raw",
				       atkbd->set, code, serio->phys);
				printk(KERN_WARNING "atkbd.c: Use 'setkeycodes %s%02x <keycode>' "
				       "to make it known.\n",
				       code & 0x80 ? "e0" : "", code & 0x7f);
			}
			break;
		case ATKBD_SCR_1:
			scroll = 1 - atkbd->release * 2;
			break;
		case ATKBD_SCR_2:
			scroll = 2 - atkbd->release * 4;
			break;
		case ATKBD_SCR_4:
			scroll = 4 - atkbd->release * 8;
			break;
		case ATKBD_SCR_8:
			scroll = 8 - atkbd->release * 16;
			break;
		case ATKBD_SCR_CLICK:
			click = !atkbd->release;
			break;
		default:
			value = atkbd->release ? 0 :
				(1 + (!atkbd_softrepeat && test_bit(atkbd->keycode[code], atkbd->dev.key)));

			switch (value) { 	/* Workaround Toshiba laptop multiple keypress */
				case 0:
					atkbd->last = 0;
					break;
				case 1:
					atkbd->last = code;
					atkbd->time = jiffies + (atkbd->dev.rep[REP_DELAY] * HZ + 500) / 1000 / 2;
					break;
				case 2:
					if (!time_after(jiffies, atkbd->time) && atkbd->last == code)
						value = 1;
					break;
			}

			atkbd_report_key(&atkbd->dev, regs, atkbd->keycode[code], value);
	}

	if (scroll || click != -1) {
		input_regs(&atkbd->dev, regs);
		input_report_key(&atkbd->dev, BTN_MIDDLE, click);
		input_report_rel(&atkbd->dev, REL_WHEEL, scroll);
		input_sync(&atkbd->dev);
	}

	atkbd->release = 0;
out:
	return IRQ_HANDLED;
}

/*
 * atkbd_sendbyte() sends a byte to the keyboard, and waits for
 * acknowledge. It doesn't handle resends according to the keyboard
 * protocol specs, because if these are needed, the keyboard needs
 * replacement anyway, and they only make a mess in the protocol.
 */

static int atkbd_sendbyte(struct atkbd *atkbd, unsigned char byte)
{
	int timeout = 20000; /* 200 msec */
	atkbd->ack = 0;

#ifdef ATKBD_DEBUG
	printk(KERN_DEBUG "atkbd.c: Sent: %02x\n", byte);
#endif
	if (serio_write(atkbd->serio, byte))
		return -1;

	while (!atkbd->ack && timeout--) udelay(10);

	return -(atkbd->ack <= 0);
}

/*
 * atkbd_command() sends a command, and its parameters to the keyboard,
 * then waits for the response and puts it in the param array.
 */

static int atkbd_command(struct atkbd *atkbd, unsigned char *param, int command)
{
	int timeout = 500000; /* 500 msec */
	int send = (command >> 12) & 0xf;
	int receive = (command >> 8) & 0xf;
	int i;

	atkbd->cmdcnt = receive;

	if (command == ATKBD_CMD_RESET_BAT)
		timeout = 2000000; /* 2 sec */

	if (receive && param)
		for (i = 0; i < receive; i++)
			atkbd->cmdbuf[(receive - 1) - i] = param[i];

	if (command & 0xff)
		if (atkbd_sendbyte(atkbd, command & 0xff))
			return (atkbd->cmdcnt = 0) - 1;

	for (i = 0; i < send; i++)
		if (atkbd_sendbyte(atkbd, param[i]))
			return (atkbd->cmdcnt = 0) - 1;

	while (atkbd->cmdcnt && timeout--) {

		if (atkbd->cmdcnt == 1 &&
		    command == ATKBD_CMD_RESET_BAT && timeout > 100000)
			timeout = 100000;

		if (atkbd->cmdcnt == 1 && command == ATKBD_CMD_GETID &&
		    atkbd->cmdbuf[1] != 0xab && atkbd->cmdbuf[1] != 0xac) {
			atkbd->cmdcnt = 0;
			break;
		}

		udelay(1);
	}

	if (param)
		for (i = 0; i < receive; i++)
			param[i] = atkbd->cmdbuf[(receive - 1) - i];

	if (command == ATKBD_CMD_RESET_BAT && atkbd->cmdcnt == 1)
		atkbd->cmdcnt = 0;

	if (atkbd->cmdcnt) {
		atkbd->cmdcnt = 0;
		return -1;
	}

	return 0;
}

/*
 * Event callback from the input module. Events that change the state of
 * the hardware are processed here.
 */

static int atkbd_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct atkbd *atkbd = dev->private;
	const short period[32] =
		{ 33,  37,  42,  46,  50,  54,  58,  63,  67,  75,  83,  92, 100, 109, 116, 125,
		 133, 149, 167, 182, 200, 217, 232, 250, 270, 303, 333, 370, 400, 435, 470, 500 };
	const short delay[4] =
		{ 250, 500, 750, 1000 };
	unsigned char param[2];
	int i, j;

	if (!atkbd->write)
		return -1;

	switch (type) {

		case EV_LED:

			param[0] = (test_bit(LED_SCROLLL, dev->led) ? 1 : 0)
			         | (test_bit(LED_NUML,    dev->led) ? 2 : 0)
			         | (test_bit(LED_CAPSL,   dev->led) ? 4 : 0);
		        atkbd_command(atkbd, param, ATKBD_CMD_SETLEDS);

			if (atkbd->extra) {
				param[0] = 0;
				param[1] = (test_bit(LED_COMPOSE, dev->led) ? 0x01 : 0)
					 | (test_bit(LED_SLEEP,   dev->led) ? 0x02 : 0)
					 | (test_bit(LED_SUSPEND, dev->led) ? 0x04 : 0)
				         | (test_bit(LED_MISC,    dev->led) ? 0x10 : 0)
				         | (test_bit(LED_MUTE,    dev->led) ? 0x20 : 0);
				atkbd_command(atkbd, param, ATKBD_CMD_EX_SETLEDS);
			}

			return 0;


		case EV_REP:

			if (atkbd_softrepeat) return 0;

			i = j = 0;
			while (i < 32 && period[i] < dev->rep[REP_PERIOD]) i++;
			while (j < 4 && delay[j] < dev->rep[REP_DELAY]) j++;
			dev->rep[REP_PERIOD] = period[i];
			dev->rep[REP_DELAY] = delay[j];
			param[0] = i | (j << 5);
			atkbd_command(atkbd, param, ATKBD_CMD_SETREP);

			return 0;
	}

	return -1;
}

/*
 * atkbd_probe() probes for an AT keyboard on a serio port.
 */

static int atkbd_probe(struct atkbd *atkbd)
{
	unsigned char param[2];

/*
 * Some systems, where the bit-twiddling when testing the io-lines of the
 * controller may confuse the keyboard need a full reset of the keyboard. On
 * these systems the BIOS also usually doesn't do it for us.
 */

	if (atkbd_reset)
		if (atkbd_command(atkbd, NULL, ATKBD_CMD_RESET_BAT))
			printk(KERN_WARNING "atkbd.c: keyboard reset failed on %s\n", atkbd->serio->phys);

/*
 * Then we check the keyboard ID. We should get 0xab83 under normal conditions.
 * Some keyboards report different values, but the first byte is always 0xab or
 * 0xac. Some old AT keyboards don't report anything. If a mouse is connected, this
 * should make sure we don't try to set the LEDs on it.
 */

	param[0] = param[1] = 0xa5;	/* initialize with invalid values */
	if (atkbd_command(atkbd, param, ATKBD_CMD_GETID)) {

/*
 * If the get ID command failed, we check if we can at least set the LEDs on
 * the keyboard. This should work on every keyboard out there. It also turns
 * the LEDs off, which we want anyway.
 */
		param[0] = 0;
		if (atkbd_command(atkbd, param, ATKBD_CMD_SETLEDS))
			return -1;
		atkbd->id = 0xabba;
		return 0;
	}

	if (param[0] != 0xab && param[0] != 0xac)
		return -1;
	atkbd->id = (param[0] << 8) | param[1];

	if (atkbd->id == 0xaca1 && atkbd->translated) {
		printk(KERN_ERR "atkbd.c: NCD terminal keyboards are only supported on non-translating\n");
		printk(KERN_ERR "atkbd.c: controllers. Use i8042.direct=1 to disable translation.\n");
		return -1;
	}

	return 0;
}

/*
 * atkbd_set_3 checks if a keyboard has a working Set 3 support, and
 * sets it into that. Unfortunately there are keyboards that can be switched
 * to Set 3, but don't work well in that (BTC Multimedia ...)
 */

static int atkbd_set_3(struct atkbd *atkbd)
{
	unsigned char param[2];

/*
 * For known special keyboards we can go ahead and set the correct set.
 * We check for NCD PS/2 Sun, NorthGate OmniKey 101 and
 * IBM RapidAccess / IBM EzButton / Chicony KBP-8993 keyboards.
 */

	if (atkbd->translated)
		return 2;

	if (atkbd->id == 0xaca1) {
		param[0] = 3;
		atkbd_command(atkbd, param, ATKBD_CMD_SSCANSET);
		return 3;
	}

	if (atkbd_extra) {
		param[0] = 0x71;
		if (!atkbd_command(atkbd, param, ATKBD_CMD_EX_ENABLE)) {
			atkbd->extra = 1;
			return 2;
		}
	}

	if (atkbd_set != 3)
		return 2;

	if (!atkbd_command(atkbd, param, ATKBD_CMD_OK_GETID)) {
		atkbd->id = param[0] << 8 | param[1];
		return 2;
	}

	param[0] = 3;
	if (atkbd_command(atkbd, param, ATKBD_CMD_SSCANSET))
		return 2;

	param[0] = 0;
	if (atkbd_command(atkbd, param, ATKBD_CMD_GSCANSET))
		return 2;

	if (param[0] != 3) {
		param[0] = 2;
		if (atkbd_command(atkbd, param, ATKBD_CMD_SSCANSET))
		return 2;
	}

	atkbd_command(atkbd, param, ATKBD_CMD_SETALL_MBR);

	return 3;
}

static int atkbd_enable(struct atkbd *atkbd)
{
	unsigned char param[1];

/*
 * Set the LEDs to a defined state.
 */

	param[0] = 0;
	if (atkbd_command(atkbd, param, ATKBD_CMD_SETLEDS))
		return -1;

/*
 * Set autorepeat to fastest possible.
 */

	param[0] = 0;
	if (atkbd_command(atkbd, param, ATKBD_CMD_SETREP))
		return -1;

/*
 * Enable the keyboard to receive keystrokes.
 */

	if (atkbd_command(atkbd, NULL, ATKBD_CMD_ENABLE)) {
		printk(KERN_ERR "atkbd.c: Failed to enable keyboard on %s\n",
			atkbd->serio->phys);
		return -1;
	}

	return 0;
}

/*
 * atkbd_cleanup() restores the keyboard state so that BIOS is happy after a
 * reboot.
 */

static void atkbd_cleanup(struct serio *serio)
{
	struct atkbd *atkbd = serio->private;
	atkbd_command(atkbd, NULL, ATKBD_CMD_RESET_BAT);
}

/*
 * atkbd_disconnect() closes and frees.
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

	if (!(atkbd = kmalloc(sizeof(struct atkbd), GFP_KERNEL)))
		return;
	memset(atkbd, 0, sizeof(struct atkbd));

	switch (serio->type & SERIO_TYPE) {

		case SERIO_8042_XL:
			atkbd->translated = 1;
		case SERIO_8042:
			if (serio->write)
				atkbd->write = 1;
			break;
		case SERIO_RS232:
			if ((serio->type & SERIO_PROTO) == SERIO_PS2SER)
				break;
		default:
			kfree(atkbd);
			return;
	}

	if (atkbd->write) {
		atkbd->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_LED) | BIT(EV_REP);
		atkbd->dev.ledbit[0] = BIT(LED_NUML) | BIT(LED_CAPSL) | BIT(LED_SCROLLL);
	} else  atkbd->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_REP);

	if (!atkbd_softrepeat) {
		atkbd->dev.rep[REP_DELAY] = 250;
		atkbd->dev.rep[REP_PERIOD] = 33;
	}

	atkbd->ack = 1;
	atkbd->serio = serio;

	init_input_dev(&atkbd->dev);

	atkbd->dev.keycode = atkbd->keycode;
	atkbd->dev.keycodesize = sizeof(unsigned char);
	atkbd->dev.keycodemax = ARRAY_SIZE(atkbd_set2_keycode);
	atkbd->dev.event = atkbd_event;
	atkbd->dev.private = atkbd;

	serio->private = atkbd;

	if (serio_open(serio, dev)) {
		kfree(atkbd);
		return;
	}

	if (atkbd->write) {

		if (atkbd_probe(atkbd)) {
			serio_close(serio);
			serio->private = NULL;
			kfree(atkbd);
			return;
		}

		atkbd->set = atkbd_set_3(atkbd);
		atkbd_enable(atkbd);

	} else {
		atkbd->set = 2;
		atkbd->id = 0xab00;
	}

	if (atkbd->extra) {
		atkbd->dev.ledbit[0] |= BIT(LED_COMPOSE) | BIT(LED_SUSPEND) | BIT(LED_SLEEP) | BIT(LED_MUTE) | BIT(LED_MISC);
		sprintf(atkbd->name, "AT Set 2 Extra keyboard");
	} else
		sprintf(atkbd->name, "AT %s Set %d keyboard",
			atkbd->translated ? "Translated" : "Raw", atkbd->set);

	sprintf(atkbd->phys, "%s/input0", serio->phys);

	if (atkbd_scroll) {
		for (i = 0; i < 5; i++)
			atkbd_set2_keycode[atkbd_scroll_keys[i][1]] = atkbd_scroll_keys[i][0];
		atkbd->dev.evbit[0] |= BIT(EV_REL);
		atkbd->dev.relbit[0] = BIT(REL_WHEEL);
		set_bit(BTN_MIDDLE, atkbd->dev.keybit);
	}

	if (atkbd->translated) {
		for (i = 0; i < 128; i++) {
			atkbd->keycode[i] = atkbd_set2_keycode[atkbd_unxlate_table[i]];
			atkbd->keycode[i | 0x80] = atkbd_set2_keycode[atkbd_unxlate_table[i] | 0x80];
		}
	} else if (atkbd->set == 3) {
		memcpy(atkbd->keycode, atkbd_set3_keycode, sizeof(atkbd->keycode));
	} else {
		memcpy(atkbd->keycode, atkbd_set2_keycode, sizeof(atkbd->keycode));
	}

	atkbd->dev.name = atkbd->name;
	atkbd->dev.phys = atkbd->phys;
	atkbd->dev.id.bustype = BUS_I8042;
	atkbd->dev.id.vendor = 0x0001;
	atkbd->dev.id.product = atkbd->translated ? 1 : atkbd->set;
	atkbd->dev.id.version = atkbd->id;

	for (i = 0; i < 512; i++)
		if (atkbd->keycode[i] && atkbd->keycode[i] < ATKBD_SPECIAL)
			set_bit(atkbd->keycode[i], atkbd->dev.keybit);

	input_register_device(&atkbd->dev);

	printk(KERN_INFO "input: %s on %s\n", atkbd->name, serio->phys);
}

/*
 * atkbd_reconnect() tries to restore keyboard into a sane state and is
 * most likely called on resume.
 */

static int atkbd_reconnect(struct serio *serio)
{
	struct atkbd *atkbd = serio->private;
	struct serio_dev *dev = serio->dev;
	unsigned char param[1];

	if (!dev) {
		printk(KERN_DEBUG "atkbd: reconnect request, but serio is disconnected, ignoring...\n");
		return -1;
	}

	if (atkbd->write) {
		param[0] = (test_bit(LED_SCROLLL, atkbd->dev.led) ? 1 : 0)
		         | (test_bit(LED_NUML,    atkbd->dev.led) ? 2 : 0)
 		         | (test_bit(LED_CAPSL,   atkbd->dev.led) ? 4 : 0);

		if (atkbd_probe(atkbd))
			return -1;
		if (atkbd->set != atkbd_set_3(atkbd))
			return -1;

		atkbd_enable(atkbd);

		if (atkbd_command(atkbd, param, ATKBD_CMD_SETLEDS))
			return -1;
	}

	return 0;
}

static struct serio_dev atkbd_dev = {
	.interrupt =	atkbd_interrupt,
	.connect =	atkbd_connect,
	.reconnect = 	atkbd_reconnect,
	.disconnect =	atkbd_disconnect,
	.cleanup =	atkbd_cleanup,
};

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
