/*
 * $Id: psmouse.c,v 1.18 2002/03/13 10:03:43 vojtech Exp $
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
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/init.h>
#include <linux/tqueue.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("PS/2 mouse driver");
MODULE_LICENSE("GPL");

#define PSMOUSE_CMD_SETSCALE11	0x00e6
#define PSMOUSE_CMD_SETRES	0x10e8
#define PSMOUSE_CMD_GETINFO	0x03e9
#define PSMOUSE_CMD_SETSTREAM	0x00ea
#define PSMOUSE_CMD_POLL	0x03eb	
#define PSMOUSE_CMD_GETID	0x01f2
#define PSMOUSE_CMD_SETRATE	0x10f3
#define PSMOUSE_CMD_ENABLE	0x00f4
#define PSMOUSE_CMD_RESET_DIS	0x00f6

#define PSMOUSE_RET_BAT		0xaa
#define PSMOUSE_RET_ACK		0xfa
#define PSMOUSE_RET_NAK		0xfe

struct psmouse {
	struct input_dev dev;
	struct serio *serio;
	char *vendor;
	char *name;
	struct tq_struct tq;
	unsigned char cmdbuf[8];
	unsigned char packet[8];
	unsigned char cmdcnt;
	unsigned char pktcnt;
	unsigned char type;
	unsigned long last;
	char acking;
	char ack;
	char error;
	char devname[64];
	char phys[32];
};

#define PSMOUSE_PS2	1
#define PSMOUSE_PS2PP	2
#define PSMOUSE_PS2TPP	3
#define PSMOUSE_GENPS	4
#define PSMOUSE_IMPS	5
#define PSMOUSE_IMEX	6

static char *psmouse_protocols[] = { "None", "PS/2", "PS2++", "PS2T++", "GenPS/2", "ImPS/2", "ImExPS/2" };

/*
 * psmouse_process_packet() anlyzes the PS/2 mouse packet contents and
 * reports relevant events to the input module.
 */

static void psmouse_process_packet(struct psmouse *psmouse)
{
	struct input_dev *dev = &psmouse->dev;
	unsigned char *packet = psmouse->packet;

/*
 * The PS2++ protocol is a little bit complex
 */

	if (psmouse->type == PSMOUSE_PS2PP || psmouse->type == PSMOUSE_PS2TPP) {

		if ((packet[0] & 0x40) == 0x40 && (int) packet[1] - (int) ((packet[0] & 0x10) << 4) > 191 ) {

			switch (((packet[1] >> 4) & 0x03) | ((packet[0] >> 2) & 0xc0)) {

			case 1: /* Mouse extra info */

				input_report_rel(dev, packet[2] & 0x80 ? REL_HWHEEL : REL_WHEEL,
					(int) (packet[2] & 7) - (int) (packet[2] & 8));
				input_report_key(dev, BTN_SIDE, (packet[2] >> 4) & 1);
				input_report_key(dev, BTN_EXTRA, (packet[2] >> 5) & 1);
					
				break;

			case 3: /* TouchPad extra info */

				input_report_rel(dev, packet[2] & 0x08 ? REL_HWHEEL : REL_WHEEL,
					(int) ((packet[2] >> 4) & 7) - (int) ((packet[2] >> 4) & 8));
				packet[0] = packet[2] | 0x08;

				break;

			default:

				printk(KERN_WARNING "psmouse.c: Received PS2++ packet #%x, but don't know how to handle.\n",
					((packet[1] >> 4) & 0x03) | ((packet[0] >> 2) & 0xc0));

			}

		packet[0] &= 0x0f;
		packet[1] = 0;
		packet[2] = 0;

		}
	}

/*
 * Scroll wheel on IntelliMice, scroll buttons on NetMice
 */

	if (psmouse->type == PSMOUSE_IMPS || psmouse->type == PSMOUSE_GENPS)
		input_report_rel(dev, REL_WHEEL, (signed char) packet[3]);

/*
 * Scroll wheel and buttons on IntelliMouse Explorer
 */

	if (psmouse->type == PSMOUSE_IMEX) {
		input_report_rel(dev, REL_WHEEL, (int) (packet[3] & 7) - (int) (packet[3] & 8));
		input_report_key(dev, BTN_SIDE, (packet[3] >> 4) & 1);
		input_report_key(dev, BTN_EXTRA, (packet[3] >> 5) & 1);
	}

/*
 * Extra buttons on Genius NewNet 3D
 */

	if (psmouse->type == PSMOUSE_GENPS) {
		input_report_key(dev, BTN_SIDE, (packet[0] >> 6) & 1);
		input_report_key(dev, BTN_EXTRA, (packet[0] >> 7) & 1);
	}

/*
 * Generic PS/2 Mouse
 */

	input_report_key(dev, BTN_LEFT,    packet[0]       & 1);
	input_report_key(dev, BTN_MIDDLE, (packet[0] >> 2) & 1);
	input_report_key(dev, BTN_RIGHT,  (packet[0] >> 1) & 1);

	input_report_rel(dev, REL_X, packet[1] ? (int) packet[1] - (int) ((packet[0] << 4) & 0x100) : 0);
	input_report_rel(dev, REL_Y, packet[2] ? (int) ((packet[0] << 3) & 0x100) - (int) packet[2] : 0);

}

/*
 * psmouse_interrupt() handles incoming characters, either gathering them into
 * packets or passing them to the command routine as command output.
 */

static void psmouse_interrupt(struct serio *serio, unsigned char data, unsigned int flags)
{
	struct psmouse *psmouse = serio->private;

	if (psmouse->acking) {
		switch (data) {
			case PSMOUSE_RET_ACK:
				psmouse->ack = 1;
				break;
			case PSMOUSE_RET_NAK:
				psmouse->ack = -1;
				break;
			default:
				psmouse->ack = 1;	/* Workaround for mice which don't ACK the Get ID command */
				if (psmouse->cmdcnt)
					psmouse->cmdbuf[--psmouse->cmdcnt] = data;
				break;
		}
		psmouse->acking = 0;
		return;
	}

	if (psmouse->cmdcnt) {
		psmouse->cmdbuf[--psmouse->cmdcnt] = data;
		return;
	}

	if (psmouse->pktcnt && time_after(jiffies, psmouse->last + HZ/20)) {
		printk(KERN_WARNING "psmouse.c: Lost synchronization, throwing %d bytes away.\n", psmouse->pktcnt);
		psmouse->pktcnt = 0;
	}
	
	psmouse->last = jiffies;
	psmouse->packet[psmouse->pktcnt++] = data;

	if (psmouse->pktcnt == 3 + (psmouse->type >= PSMOUSE_GENPS)) {
		if ((psmouse->packet[0] & 0x08) == 0x08) psmouse_process_packet(psmouse);
		psmouse->pktcnt = 0;
		return;
	}

	if (psmouse->pktcnt == 1 && psmouse->packet[0] == PSMOUSE_RET_BAT) {
		serio_rescan(serio);
		return;
	}	
}

/*
 * psmouse_sendbyte() sends a byte to the mouse, and waits for acknowledge.
 * It doesn't handle retransmission, though it could - because when there would
 * be need for retransmissions, the mouse has to be replaced anyway.
 */

static int psmouse_sendbyte(struct psmouse *psmouse, unsigned char byte)
{
	int timeout = 10000; /* 100 msec */
	psmouse->ack = 0;
	psmouse->acking = 1;

	serio_write(psmouse->serio, byte);
	while (!psmouse->ack && timeout--) udelay(10);

	return -(psmouse->ack <= 0);
}

/*
 * psmouse_command() sends a command and its parameters to the mouse,
 * then waits for the response and puts it in the param array.
 */

static int psmouse_command(struct psmouse *psmouse, unsigned char *param, int command)
{
	int timeout = 500000; /* 500 msec */
	int send = (command >> 12) & 0xf;
	int receive = (command >> 8) & 0xf;
	int i;

	psmouse->cmdcnt = receive;

	if (command & 0xff)
		if (psmouse_sendbyte(psmouse, command & 0xff))
			return (psmouse->cmdcnt = 0) - 1;

	for (i = 0; i < send; i++)
		if (psmouse_sendbyte(psmouse, param[i]))
			return (psmouse->cmdcnt = 0) - 1;

	while (psmouse->cmdcnt && timeout--) udelay(1);

	for (i = 0; i < receive; i++)
		param[i] = psmouse->cmdbuf[(receive - 1) - i];

	if (psmouse->cmdcnt) 
		return (psmouse->cmdcnt = 0) - 1;

	return 0;
}

/*
 * psmouse_ps2pp_cmd() sends a PS2++ command, sliced into two bit
 * pieces through the SETRES command. This is needed to send extended
 * commands to mice on notebooks that try to understand the PS/2 protocol
 * Ugly.
 */

static int psmouse_ps2pp_cmd(struct psmouse *psmouse, unsigned char *param, unsigned char command)
{
	unsigned char d;
	int i;

	if (psmouse_command(psmouse,  NULL, PSMOUSE_CMD_SETSCALE11))
		return -1;

	for (i = 6; i >= 0; i -= 2) {
		d = (command >> i) & 3;
		if(psmouse_command(psmouse, &d, PSMOUSE_CMD_SETRES))
			return -1;
	}

	if (psmouse_command(psmouse, param, PSMOUSE_CMD_POLL))
		return -1;

	return 0;
}

/*
 * psmouse_extensions() probes for any extensions to the basic PS/2 protocol
 * the mouse may have.
 */

static int psmouse_extensions(struct psmouse *psmouse)
{
	unsigned char param[4];

	param[0] = 0;
	psmouse->vendor = "Generic";
	psmouse->name = "Mouse";

/*
 * Try Genius NetMouse magic init.
 */

	param[0] = 3;
	psmouse_command(psmouse, param, PSMOUSE_CMD_SETRES);
	psmouse_command(psmouse,  NULL, PSMOUSE_CMD_SETSCALE11);
	psmouse_command(psmouse,  NULL, PSMOUSE_CMD_SETSCALE11);
	psmouse_command(psmouse,  NULL, PSMOUSE_CMD_SETSCALE11);
	psmouse_command(psmouse, param, PSMOUSE_CMD_GETINFO);

	if (param[0] == 0x00 && param[1] == 0x33 && param[2] == 0x55) {
		psmouse->vendor = "Genius";
		psmouse->name = "Mouse";

		set_bit(BTN_EXTRA, psmouse->dev.keybit);
		set_bit(BTN_SIDE, psmouse->dev.keybit);
		set_bit(REL_WHEEL, psmouse->dev.relbit);

		return PSMOUSE_GENPS;
	}

/*
 * Try Logitech magic ID.
 */

	param[0] = 0;
	psmouse_command(psmouse, param, PSMOUSE_CMD_SETRES);
	psmouse_command(psmouse,  NULL, PSMOUSE_CMD_SETSCALE11);
	psmouse_command(psmouse,  NULL, PSMOUSE_CMD_SETSCALE11);
	psmouse_command(psmouse,  NULL, PSMOUSE_CMD_SETSCALE11);
	psmouse_command(psmouse, param, PSMOUSE_CMD_GETINFO);

	if (param[1]) {

		int i;
		static int logitech_4btn[] = { 12, 40, 41, 42, 43, 73, 80, -1 };
		static int logitech_wheel[] = { 75, 76, 80, 81, 83, 88, -1 };
		static int logitech_ps2pp[] = { 12, 13, 40, 41, 42, 43, 50, 51, 52, 53, 73, 75,
							76, 80, 81, 83, 88, 96, 97, -1 };

		int devicetype = ((param[0] >> 4) & 0x07) | ((param[0] << 3) & 0x78);

		psmouse->vendor = "Logitech";
		psmouse->name = "Mouse";

		if (param[1] < 3)
			clear_bit(BTN_MIDDLE, psmouse->dev.keybit);
		if (param[1] < 2)
			clear_bit(BTN_RIGHT, psmouse->dev.keybit);

		psmouse->type = PSMOUSE_PS2;

		for (i = 0; logitech_ps2pp[i] != -1; i++)
			if (logitech_ps2pp[i] == devicetype) psmouse->type = PSMOUSE_PS2PP;

		if (psmouse->type != PSMOUSE_PS2PP) return PSMOUSE_PS2;

		for (i = 0; logitech_4btn[i] != -1; i++)
			if (logitech_4btn[i] == devicetype) set_bit(BTN_SIDE, psmouse->dev.keybit);

		for (i = 0; logitech_wheel[i] != -1; i++)
			if (logitech_wheel[i] == devicetype) set_bit(REL_WHEEL, psmouse->dev.relbit);

/*
 * Do Logitech PS2++ / PS2T++ magic init.
 */

		if (devicetype == 97) { /* TouchPad 3 */

			set_bit(REL_WHEEL, psmouse->dev.relbit);
			set_bit(REL_HWHEEL, psmouse->dev.relbit);

			param[0] = 0x11; param[1] = 0x04; param[2] = 0x68; /* Unprotect RAM */
			psmouse_command(psmouse, param, 0x30d1);
			param[0] = 0x11; param[1] = 0x05; param[2] = 0x0b; /* Enable features */
			psmouse_command(psmouse, param, 0x30d1);
			param[0] = 0x11; param[1] = 0x09; param[2] = 0xc3; /* Enable PS2++ */
			psmouse_command(psmouse, param, 0x30d1);

			param[0] = 0;
			if (!psmouse_command(psmouse, param, 0x13d1) &&
				param[0] == 0x06 && param[1] == 0x00 && param[2] == 0x14)
				return PSMOUSE_PS2TPP;

		} else {
			psmouse_ps2pp_cmd(psmouse, param, 0x39); /* Magic knock */
			psmouse_ps2pp_cmd(psmouse, param, 0xDB);

			if ((param[0] & 0x78) == 0x48 && (param[1] & 0xf3) == 0xc2 &&
				(param[2] & 3) == ((param[1] >> 2) & 3))
					return PSMOUSE_PS2PP;
		}

	}

/*
 * Try IntelliMouse magic init.
 */

	param[0] = 200;
	psmouse_command(psmouse, param, PSMOUSE_CMD_SETRATE);
	param[0] = 100;
	psmouse_command(psmouse, param, PSMOUSE_CMD_SETRATE);
	param[0] =  80;
	psmouse_command(psmouse, param, PSMOUSE_CMD_SETRATE);
	psmouse_command(psmouse, param, PSMOUSE_CMD_GETID);
	
	if (param[0] == 3) {

		set_bit(REL_WHEEL, psmouse->dev.relbit);

/*
 * Try IntelliMouse Explorer magic init.
 */

		param[0] = 200;
		psmouse_command(psmouse, param, PSMOUSE_CMD_SETRATE);
		param[0] = 200;
		psmouse_command(psmouse, param, PSMOUSE_CMD_SETRATE);
		param[0] =  80;
		psmouse_command(psmouse, param, PSMOUSE_CMD_SETRATE);
		psmouse_command(psmouse, param, PSMOUSE_CMD_GETID);

		if (param[0] == 4) {

			psmouse->vendor = "Microsoft";
			psmouse->name = "IntelliMouse Explorer";

			set_bit(BTN_SIDE, psmouse->dev.keybit);
			set_bit(BTN_EXTRA, psmouse->dev.keybit);

			return PSMOUSE_IMEX;
		}

		psmouse->vendor = "Microsoft";
		psmouse->name = "IntelliMouse";

		return PSMOUSE_IMPS;
	}

/*
 * Okay, all failed, we have a standard mouse here. The number of the buttons is
 * still a question, though.
 */

	psmouse->vendor = "Generic";
	psmouse->name = "Mouse";

	return PSMOUSE_PS2;
}

/*
 * psmouse_probe() probes for a PS/2 mouse.
 */

static int psmouse_probe(struct psmouse *psmouse)
{
	unsigned char param[2];

/*
 * First we reset and disable the mouse.
 */

	if (psmouse_command(psmouse, NULL, PSMOUSE_CMD_RESET_DIS))
		return -1;

/*
 * Next, we check if it's a mouse. It should send 0x00 or 0x03
 * in case of an IntelliMouse in 4-byte mode or 0x04 for IM Explorer.
 */

	param[0] = param[1] = 0xa5;

	if (psmouse_command(psmouse, param, PSMOUSE_CMD_GETID))
		return -1;

	if (param[0] != 0x00 && param[0] != 0x03 && param[0] != 0x04)
		return -1;

/*
 * And here we try to determine if it has any extensions over the
 * basic PS/2 3-button mouse.
 */

	return psmouse->type = psmouse_extensions(psmouse);
}

/*
 * psmouse_initialize() initializes the mouse to a sane state.
 */

static void psmouse_initialize(struct psmouse *psmouse)
{
	unsigned char param[2];

/*
 * We set the mouse report rate to a highest possible value.
 * We try 100 first in case mouse fails to set 200.
 */

	param[0] = 100;
	psmouse_command(psmouse, param, PSMOUSE_CMD_SETRATE);

	param[0] = 200;
	psmouse_command(psmouse, param, PSMOUSE_CMD_SETRATE);

/*
 * We also set the resolution and scaling.
 */

	param[0] = 3;
	psmouse_command(psmouse, param, PSMOUSE_CMD_SETRES);
	psmouse_command(psmouse,  NULL, PSMOUSE_CMD_SETSCALE11);

/*
 * We set the mouse into streaming mode.
 */

	psmouse_command(psmouse, param, PSMOUSE_CMD_SETSTREAM);

/*
 * Last, we enable the mouse so that we get reports from it.
 */

	if (psmouse_command(psmouse, NULL, PSMOUSE_CMD_ENABLE)) {
		printk(KERN_WARNING "psmouse.c: Failed to enable mouse on %s\n", psmouse->serio->phys);
	}

}

/*
 * psmouse_disconnect() cleans up after we don't want talk
 * to the mouse anymore.
 */

static void psmouse_disconnect(struct serio *serio)
{
	struct psmouse *psmouse = serio->private;
	input_unregister_device(&psmouse->dev);
	serio_close(serio);
	kfree(psmouse);
}

/*
 * psmouse_connect() is a callback form the serio module when
 * an unhandled serio port is found.
 */

static void psmouse_connect(struct serio *serio, struct serio_dev *dev)
{
	struct psmouse *psmouse;
	
	if ((serio->type & SERIO_TYPE) != SERIO_8042)
		return;

	if (!(psmouse = kmalloc(sizeof(struct psmouse), GFP_KERNEL)))
		return;

	memset(psmouse, 0, sizeof(struct psmouse));

	psmouse->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_REL);
	psmouse->dev.keybit[LONG(BTN_MOUSE)] = BIT(BTN_LEFT) | BIT(BTN_MIDDLE) | BIT(BTN_RIGHT);
	psmouse->dev.relbit[0] = BIT(REL_X) | BIT(REL_Y);

	psmouse->serio = serio;
	psmouse->dev.private = psmouse;

	serio->private = psmouse;

	if (serio_open(serio, dev)) {
		kfree(psmouse);
		return;
	}

	if (psmouse_probe(psmouse) <= 0) {
		serio_close(serio);
		kfree(psmouse);
		return;
	}
	
	sprintf(psmouse->devname, "%s %s %s",
		psmouse_protocols[psmouse->type], psmouse->vendor, psmouse->name);
	sprintf(psmouse->phys, "%s/input0",
		serio->phys);

	psmouse->dev.name = psmouse->devname;
	psmouse->dev.phys = psmouse->phys;
	psmouse->dev.id.bustype = BUS_I8042;
	psmouse->dev.id.vendor = psmouse->type;
	psmouse->dev.id.product = 0x0002;
	psmouse->dev.id.version = 0x0100;

	input_register_device(&psmouse->dev);
	
	printk(KERN_INFO "input: %s on %s\n", psmouse->devname, serio->phys);

	psmouse_initialize(psmouse);
}

static struct serio_dev psmouse_dev = {
	.interrupt =	psmouse_interrupt,
	.connect =	psmouse_connect,
	.disconnect =	psmouse_disconnect
};

int __init psmouse_init(void)
{
	serio_register_device(&psmouse_dev);
	return 0;
}

void __exit psmouse_exit(void)
{
	serio_unregister_device(&psmouse_dev);
}

module_init(psmouse_init);
module_exit(psmouse_exit);
