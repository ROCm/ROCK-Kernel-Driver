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
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/workqueue.h>
#include <linux/timer.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION("AT and PS/2 keyboard driver");
MODULE_PARM(atkbd_set, "1i");
MODULE_PARM(atkbd_reset, "1i");
MODULE_PARM(atkbd_softrepeat, "1i");
MODULE_LICENSE("GPL");

static int atkbd_set = 2;
#if defined(__i386__) || defined (__x86_64__)
static int atkbd_reset;
#else
static int atkbd_reset = 1;
#endif
static int atkbd_softrepeat;

/*
 * Scancode to keycode tables. These are just the default setting, and
 * are loadable via an userland utility.
 */

static unsigned char atkbd_set2_keycode[512] = {
	  0, 67, 65, 63, 61, 59, 60, 88,  0, 68, 66, 64, 62, 15, 41, 85,
	  0, 56, 42,182, 29, 16,  2, 89,  0,  0, 44, 31, 30, 17,  3, 90,
	  0, 46, 45, 32, 18,  5,  4, 91, 90, 57, 47, 33, 20, 19,  6,  0,
	 91, 49, 48, 35, 34, 21,  7,  0,  0,  0, 50, 36, 22,  8,  9,  0,
	  0, 51, 37, 23, 24, 11, 10,  0,  0, 52, 53, 38, 39, 25, 12,  0,
	122, 89, 40,120, 26, 13,  0,  0, 58, 54, 28, 27,  0, 43,  0,  0,
	 85, 86, 90, 91, 92, 93, 14, 94, 95, 79,183, 75, 71,121,  0,123,
	 82, 83, 80, 76, 77, 72,  1, 69, 87, 78, 81, 74, 55, 73, 70, 99,
	  0,  0,  0, 65, 99,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,255,
	  0,  0, 92, 90, 85,  0,137,  0,  0,  0,  0, 91, 89,144,115,  0,
	217,100,255,  0, 97,165,164,  0,156,  0,  0,140,115,  0,  0,125,
	173,114,  0,113,152,163,151,126,128,166,  0,140,  0,147,  0,127,
	159,167,115,160,164,  0,  0,116,158,  0,150,166,  0,  0,  0,142,
	157,  0,114,166,168,  0,  0,213,155,  0, 98,113,  0,163,  0,138,
	226,  0,  0,  0,  0,  0,153,140,  0,255, 96,  0,  0,  0,143,  0,
	133,  0,116,  0,143,  0,174,133,  0,107,  0,105,102,  0,  0,112,
	110,111,108,112,106,103,  0,119,  0,118,109,  0, 99,104,119
};

static unsigned char atkbd_set3_keycode[512] = {
	  0,  0,  0,  0,  0,  0,  0, 59,  1,138,128,129,130, 15, 41, 60,
	131, 29, 42, 86, 58, 16,  2, 61,133, 56, 44, 31, 30, 17,  3, 62,
	134, 46, 45, 32, 18,  5,  4, 63,135, 57, 47, 33, 20, 19,  6, 64,
	136, 49, 48, 35, 34, 21,  7, 65,137,100, 50, 36, 22,  8,  9, 66,
	125, 51, 37, 23, 24, 11, 10, 67,126, 52, 53, 38, 39, 25, 12, 68,
	113,114, 40, 84, 26, 13, 87, 99, 97, 54, 28, 27, 43, 84, 88, 70,
	108,105,119,103,111,107, 14,110,  0, 79,106, 75, 71,109,102,104,
	 82, 83, 80, 76, 77, 72, 69, 98,  0, 96, 81,  0, 78, 73, 55, 85,
	 89, 90, 91, 92, 74,185,184,182,  0,  0,  0,125,126,127,112,  0,
	  0,139,150,163,165,115,152,150,166,140,160,154,113,114,167,168,
	148,149,147,140,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,255
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
#define ATKBD_RET_EMULX		0x80
#define ATKBD_RET_EMUL1		0xe1
#define ATKBD_RET_RELEASE	0xf0

#define ATKBD_KEY_UNKNOWN	  0
#define ATKBD_KEY_NULL		255

/*
 * The atkbd control structure
 */

struct atkbd {
	unsigned char keycode[512];
	struct input_dev dev;
	struct serio *serio;
	struct timer_list timer;
	char name[64];
	char phys[32];
	unsigned char cmdbuf[4];
	unsigned char cmdcnt;
	unsigned char set;
	unsigned char release;
	int lastkey;
	volatile signed char ack;
	unsigned char emul;
	unsigned short id;
	unsigned char write;
	unsigned char translated;
	unsigned char resend;
	unsigned char bat_xl;
	unsigned int last;
	unsigned long time;
};

/*
 * atkbd_interrupt(). Here takes place processing of data received from
 * the keyboard into events.
 */

static irqreturn_t atkbd_interrupt(struct serio *serio, unsigned char data,
			unsigned int flags, struct pt_regs *regs)
{
	struct atkbd *atkbd = serio->private;
	unsigned int code = data;
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

	switch (code) {
		case ATKBD_RET_ACK:
			atkbd->ack = 1;
			goto out;
		case ATKBD_RET_NAK:
			atkbd->ack = -1;
			goto out;
	}

	if (atkbd->translated) do {

		if (atkbd->emul != 1) {
			if (code == ATKBD_RET_EMUL0 || code == ATKBD_RET_EMUL1)
				break;
			if (code == ATKBD_RET_BAT) {
				if (!atkbd->bat_xl)
					break;
				atkbd->bat_xl = 0;
			}
			if (code == (ATKBD_RET_BAT & 0x7f))
				atkbd->bat_xl = 1;
		}

		if (code < 0x80) {
			code = atkbd_unxlate_table[code];
			break;
		}

		if (atkbd->cmdcnt)
			break;

		code = atkbd_unxlate_table[code & 0x7f];
		atkbd->release = 1;

	} while (0);

	if (atkbd->cmdcnt) {
		atkbd->cmdbuf[--atkbd->cmdcnt] = code;
		goto out;
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
	}

	if (atkbd->emul) {
		if (--atkbd->emul)
			goto out;
		code |= 0x100;
	}

	switch (atkbd->keycode[code]) {
		case ATKBD_KEY_NULL:
			break;
		case ATKBD_KEY_UNKNOWN:
			printk(KERN_WARNING "atkbd.c: Unknown key %s (%s set %d, code %#x, data %#x, on %s).\n",
				atkbd->release ? "released" : "pressed",
				atkbd->translated ? "translated" : "raw", 
				atkbd->set, code, data, serio->phys);
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

			input_regs(&atkbd->dev, regs);
			input_event(&atkbd->dev, EV_KEY, atkbd->keycode[code], value);
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
	struct { int p; u8 v; } period[] =	
		{ {30, 0x00}, {25, 0x02}, {20, 0x04}, {15, 0x08}, {10, 0x0c}, {7, 0x10}, {5, 0x14}, {0, 0x14} };
	struct { int d; u8 v; } delay[] =
        	{ {1000, 0x60}, {750, 0x40}, {500, 0x20}, {250, 0x00}, {0, 0x00} };
	char param[2];
	int i, j;

	if (!atkbd->write)
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
				         | (test_bit(LED_MISC,    dev->led) ? 0x10 : 0)
				         | (test_bit(LED_MUTE,    dev->led) ? 0x20 : 0);
				atkbd_command(atkbd, param, ATKBD_CMD_EX_SETLEDS);
			}

			return 0;


		case EV_REP:

			if (atkbd_softrepeat) return 0;

			i = j = 0;
			while (period[i].p > dev->rep[REP_PERIOD]) i++;
			while (delay[j].d > dev->rep[REP_DELAY]) j++;
			dev->rep[REP_PERIOD] = period[i].p;
			dev->rep[REP_DELAY] = delay[j].d;
			param[0] = period[i].v | delay[j].v;
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

	if (atkbd_set != 2) 
		if (!atkbd_command(atkbd, param, ATKBD_CMD_OK_GETID)) {
			atkbd->id = param[0] << 8 | param[1];
			return 2;
		}

	if (atkbd_set == 4) {
		param[0] = 0x71;
		if (!atkbd_command(atkbd, param, ATKBD_CMD_EX_ENABLE))
			return 4;
	}

	if (atkbd_set != 3) 
		return 2;

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
			kfree(atkbd);
			return;
		}
		
		atkbd->set = atkbd_set_3(atkbd);
		atkbd_enable(atkbd);

	} else {
		atkbd->set = 2;
		atkbd->id = 0xab00;
	}

	if (atkbd->set == 4) {
		atkbd->dev.ledbit[0] |= BIT(LED_COMPOSE) | BIT(LED_SUSPEND) | BIT(LED_SLEEP) | BIT(LED_MUTE) | BIT(LED_MISC);
		sprintf(atkbd->name, "AT Set 2 Extended keyboard");
	} else
		sprintf(atkbd->name, "AT %s Set %d keyboard",
			atkbd->translated ? "Translated" : "Raw", atkbd->set);

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
		if (atkbd->keycode[i] && atkbd->keycode[i] < 255)
			set_bit(atkbd->keycode[i], atkbd->dev.keybit);

	input_register_device(&atkbd->dev);

	printk(KERN_INFO "input: %s on %s\n", atkbd->name, serio->phys);
}


static struct serio_dev atkbd_dev = {
	.interrupt =	atkbd_interrupt,
	.connect =	atkbd_connect,
	.disconnect =	atkbd_disconnect,
	.cleanup =	atkbd_cleanup,
};

#ifndef MODULE
static int __init atkbd_setup_set(char *str)
{
        int ints[4];
        str = get_options(str, ARRAY_SIZE(ints), ints);
        if (ints[0] > 0) atkbd_set = ints[1];
        return 1;
}
static int __init atkbd_setup_reset(char *str)
{
        int ints[4];
        str = get_options(str, ARRAY_SIZE(ints), ints);
        if (ints[0] > 0) atkbd_reset = ints[1];
        return 1;
}

__setup("atkbd_set=", atkbd_setup_set);
__setup("atkbd_reset", atkbd_setup_reset);
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
