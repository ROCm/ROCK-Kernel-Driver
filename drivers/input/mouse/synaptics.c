/*
 * Synaptics TouchPad PS/2 mouse driver
 *
 *   2003 Dmitry Torokhov <dtor@mail.ru>
 *     Added support for pass-through port
 *
 *   2003 Peter Osterlund <petero2@telia.com>
 *     Ported to 2.5 input device infrastructure.
 *
 *   Copyright (C) 2001 Stefan Gmeiner <riddlebox@freesurf.ch>
 *     start merging tpconfig and gpm code to a xfree-input module
 *     adding some changes and extensions (ex. 3rd and 4th button)
 *
 *   Copyright (c) 1997 C. Scott Ananian <cananian@alumni.priceton.edu>
 *   Copyright (c) 1998-2000 Bruce Kalk <kall@compass.com>
 *     code for the special synaptics commands (from the tpconfig-source)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Trademarks are the property of their respective owners.
 */

#include <linux/module.h>
#include <linux/input.h>
#include <linux/serio.h>
#include "psmouse.h"
#include "synaptics.h"

/*
 * The x/y limits are taken from the Synaptics TouchPad interfacing Guide,
 * section 2.3.2, which says that they should be valid regardless of the
 * actual size of the sensor.
 */
#define XMIN_NOMINAL 1472
#define XMAX_NOMINAL 5472
#define YMIN_NOMINAL 1408
#define YMAX_NOMINAL 4448

/*****************************************************************************
 *	Synaptics communications functions
 ****************************************************************************/

/*
 * Use the Synaptics extended ps/2 syntax to write a special command byte.
 * special command: 0xE8 rr 0xE8 ss 0xE8 tt 0xE8 uu where (rr*64)+(ss*16)+(tt*4)+uu
 *                  is the command. A 0xF3 or 0xE9 must follow (see synaptics_send_cmd
 *                  and synaptics_mode_cmd)
 */
static int synaptics_special_cmd(struct psmouse *psmouse, unsigned char command)
{
	int i;

	if (psmouse_command(psmouse, NULL, PSMOUSE_CMD_SETSCALE11))
		return -1;

	for (i = 6; i >= 0; i -= 2) {
		unsigned char d = (command >> i) & 3;
		if (psmouse_command(psmouse, &d, PSMOUSE_CMD_SETRES))
			return -1;
	}

	return 0;
}

/*
 * Send a command to the synpatics touchpad by special commands
 */
static int synaptics_send_cmd(struct psmouse *psmouse, unsigned char c, unsigned char *param)
{
	if (synaptics_special_cmd(psmouse, c))
		return -1;
	if (psmouse_command(psmouse, param, PSMOUSE_CMD_GETINFO))
		return -1;
	return 0;
}

/*
 * Set the synaptics touchpad mode byte by special commands
 */
static int synaptics_mode_cmd(struct psmouse *psmouse, unsigned char mode)
{
	unsigned char param[1];

	if (synaptics_special_cmd(psmouse, mode))
		return -1;
	param[0] = SYN_PS_SET_MODE2;
	if (psmouse_command(psmouse, param, PSMOUSE_CMD_SETRATE))
		return -1;
	return 0;
}

static int synaptics_reset(struct psmouse *psmouse)
{
	unsigned char r[2];

	if (psmouse_command(psmouse, r, PSMOUSE_CMD_RESET_BAT))
		return -1;
	if (r[0] == PSMOUSE_RET_BAT && r[1] == PSMOUSE_RET_ID)
		return 0;
	return -1;
}

/*
 * Read the model-id bytes from the touchpad
 * see also SYN_MODEL_* macros
 */
static int synaptics_model_id(struct psmouse *psmouse)
{
	struct synaptics_data *priv = psmouse->private;
	unsigned char mi[3];

	if (synaptics_send_cmd(psmouse, SYN_QUE_MODEL, mi))
		return -1;
	priv->model_id = (mi[0]<<16) | (mi[1]<<8) | mi[2];
	return 0;
}

/*
 * Read the capability-bits from the touchpad
 * see also the SYN_CAP_* macros
 */
static int synaptics_capability(struct psmouse *psmouse)
{
	struct synaptics_data *priv = psmouse->private;
	unsigned char cap[3];

	if (synaptics_send_cmd(psmouse, SYN_QUE_CAPABILITIES, cap))
		return -1;
	priv->capabilities = (cap[0]<<16) | (cap[1]<<8) | cap[2];
	priv->ext_cap = 0;
	if (!SYN_CAP_VALID(priv->capabilities))
		return -1;

	if (SYN_EXT_CAP_REQUESTS(priv->capabilities)) {
		if (synaptics_send_cmd(psmouse, SYN_QUE_EXT_CAPAB, cap)) {
			printk(KERN_ERR "Synaptics claims to have extended capabilities,"
			       " but I'm not able to read them.");
		} else
			priv->ext_cap = (cap[0]<<16) | (cap[1]<<8) | cap[2];
	}
	return 0;
}

/*
 * Identify Touchpad
 * See also the SYN_ID_* macros
 */
static int synaptics_identify(struct psmouse *psmouse)
{
	struct synaptics_data *priv = psmouse->private;
	unsigned char id[3];

	if (synaptics_send_cmd(psmouse, SYN_QUE_IDENTIFY, id))
		return -1;
	priv->identity = (id[0]<<16) | (id[1]<<8) | id[2];
	if (SYN_ID_IS_SYNAPTICS(priv->identity))
		return 0;
	return -1;
}

static void print_ident(struct synaptics_data *priv)
{
	printk(KERN_INFO "Synaptics Touchpad, model: %ld\n", SYN_ID_MODEL(priv->identity));
	printk(KERN_INFO " Firmware: %ld.%ld\n", SYN_ID_MAJOR(priv->identity),
	       SYN_ID_MINOR(priv->identity));
	if (SYN_MODEL_ROT180(priv->model_id))
		printk(KERN_INFO " 180 degree mounted touchpad\n");
	if (SYN_MODEL_PORTRAIT(priv->model_id))
		printk(KERN_INFO " portrait touchpad\n");
	printk(KERN_INFO " Sensor: %ld\n", SYN_MODEL_SENSOR(priv->model_id));
	if (SYN_MODEL_NEWABS(priv->model_id))
		printk(KERN_INFO " new absolute packet format\n");
	if (SYN_MODEL_PEN(priv->model_id))
		printk(KERN_INFO " pen detection\n");

	if (SYN_CAP_EXTENDED(priv->capabilities)) {
		printk(KERN_INFO " Touchpad has extended capability bits\n");
		if (SYN_CAP_MULTI_BUTTON_NO(priv->ext_cap) &&
		    SYN_CAP_MULTI_BUTTON_NO(priv->ext_cap) <= 8)
			printk(KERN_INFO " -> %d multi-buttons, i.e. besides standard buttons\n",
			       (int)(SYN_CAP_MULTI_BUTTON_NO(priv->ext_cap)));
		else if (SYN_CAP_FOUR_BUTTON(priv->capabilities))
			printk(KERN_INFO " -> four buttons\n");
		if (SYN_CAP_MULTIFINGER(priv->capabilities))
			printk(KERN_INFO " -> multifinger detection\n");
		if (SYN_CAP_PALMDETECT(priv->capabilities))
			printk(KERN_INFO " -> palm detection\n");
		if (SYN_CAP_PASS_THROUGH(priv->capabilities))
			printk(KERN_INFO " -> pass-through port\n");
	}
}

static int synaptics_query_hardware(struct psmouse *psmouse)
{
	struct synaptics_data *priv = psmouse->private;
	int retries = 0;
	int mode;

	while ((retries++ < 3) && synaptics_reset(psmouse))
		printk(KERN_ERR "synaptics reset failed\n");

	if (synaptics_identify(psmouse))
		return -1;
	if (synaptics_model_id(psmouse))
		return -1;
	if (synaptics_capability(psmouse))
		return -1;

	mode = SYN_BIT_ABSOLUTE_MODE | SYN_BIT_HIGH_RATE;
	if (SYN_ID_MAJOR(priv->identity) >= 4)
		mode |= SYN_BIT_DISABLE_GESTURE;
	if (SYN_CAP_EXTENDED(priv->capabilities))
		mode |= SYN_BIT_W_MODE;
	if (synaptics_mode_cmd(psmouse, mode))
		return -1;

	return 0;
}

/*****************************************************************************
 *	Synaptics pass-through PS/2 port support
 ****************************************************************************/
static int synaptics_pt_open(struct serio *port)
{
	return 0;
}

static void synaptics_pt_close(struct serio *port)
{
}

static int synaptics_pt_write(struct serio *port, unsigned char c)
{
	struct psmouse *parent = port->driver;
	char rate_param = SYN_PS_CLIENT_CMD; /* indicates that we want pass-through port */

	if (synaptics_special_cmd(parent, c))
		return -1;
	if (psmouse_command(parent, &rate_param, PSMOUSE_CMD_SETRATE))
		return -1;
	return 0;
}

static inline int synaptics_is_pt_packet(unsigned char *buf)
{
	return (buf[0] & 0xFC) == 0x84 && (buf[3] & 0xCC) == 0xC4;
}

static void synaptics_pass_pt_packet(struct serio *ptport, unsigned char *packet)
{
	struct psmouse *child = ptport->private;

	if (child) {
		if (child->state == PSMOUSE_ACTIVATED) {
			serio_interrupt(ptport, packet[1], 0, NULL);
			serio_interrupt(ptport, packet[4], 0, NULL);
			serio_interrupt(ptport, packet[5], 0, NULL);
			if (child->type >= PSMOUSE_GENPS)
				serio_interrupt(ptport, packet[2], 0, NULL);
		} else if (child->state != PSMOUSE_IGNORE) {
			serio_interrupt(ptport, packet[1], 0, NULL);
		}
	}
}

int synaptics_pt_init(struct psmouse *psmouse)
{
	struct synaptics_data *priv = psmouse->private;
	struct serio *port;
	struct psmouse *child;

	if (psmouse->type != PSMOUSE_SYNAPTICS)
		return -1;
	if (!SYN_CAP_EXTENDED(priv->capabilities))
		return -1;
	if (!SYN_CAP_PASS_THROUGH(priv->capabilities))
		return -1;

	priv->ptport = port = kmalloc(sizeof(struct serio), GFP_KERNEL);
	if (!port) {
		printk(KERN_ERR "synaptics: not enough memory to allocate serio port\n");
		return -1;
	}

	memset(port, 0, sizeof(struct serio));
	port->type = SERIO_PS_PSTHRU;
	port->name = "Synaptics pass-through";
	port->phys = "synaptics-pt/serio0";
	port->write = synaptics_pt_write;
	port->open = synaptics_pt_open;
	port->close = synaptics_pt_close;
	port->driver = psmouse;

	printk(KERN_INFO "serio: %s port at %s\n", port->name, psmouse->phys);
	serio_register_slave_port(port);

	/* adjust the touchpad to child's choice of protocol */
	child = port->private;
	if (child && child->type >= PSMOUSE_GENPS) {
		if (synaptics_mode_cmd(psmouse, (SYN_BIT_ABSOLUTE_MODE |
					 	 SYN_BIT_HIGH_RATE |
					 	 SYN_BIT_DISABLE_GESTURE |
						 SYN_BIT_FOUR_BYTE_CLIENT |
					 	 SYN_BIT_W_MODE)))
			printk(KERN_INFO "synaptics: failed to enable 4-byte guest protocol\n");
	}

	return 0;
}

/*****************************************************************************
 *	Driver initialization/cleanup functions
 ****************************************************************************/

static inline void set_abs_params(struct input_dev *dev, int axis, int min, int max, int fuzz, int flat)
{
	dev->absmin[axis] = min;
	dev->absmax[axis] = max;
	dev->absfuzz[axis] = fuzz;
	dev->absflat[axis] = flat;

	set_bit(axis, dev->absbit);
}

static void set_input_params(struct input_dev *dev, struct synaptics_data *priv)
{
	set_bit(EV_ABS, dev->evbit);
	set_abs_params(dev, ABS_X, XMIN_NOMINAL, XMAX_NOMINAL, 0, 0);
	set_abs_params(dev, ABS_Y, YMIN_NOMINAL, YMAX_NOMINAL, 0, 0);
	set_abs_params(dev, ABS_PRESSURE, 0, 255, 0, 0);
	set_bit(ABS_TOOL_WIDTH, dev->absbit);

	set_bit(EV_KEY, dev->evbit);
	set_bit(BTN_TOUCH, dev->keybit);
	set_bit(BTN_TOOL_FINGER, dev->keybit);
	set_bit(BTN_TOOL_DOUBLETAP, dev->keybit);
	set_bit(BTN_TOOL_TRIPLETAP, dev->keybit);

	set_bit(BTN_LEFT, dev->keybit);
	set_bit(BTN_RIGHT, dev->keybit);
	set_bit(BTN_FORWARD, dev->keybit);
	set_bit(BTN_BACK, dev->keybit);
	if (SYN_CAP_MULTI_BUTTON_NO(priv->ext_cap)) {
		switch (SYN_CAP_MULTI_BUTTON_NO(priv->ext_cap) & ~0x01) {
		default:
			/*
			 * if nExtBtn is greater than 8 it should be considered
			 * invalid and treated as 0
			 */
			break;
		case 8:
			set_bit(BTN_7, dev->keybit);
			set_bit(BTN_6, dev->keybit);
		case 6:
			set_bit(BTN_5, dev->keybit);
			set_bit(BTN_4, dev->keybit);
		case 4:
			set_bit(BTN_3, dev->keybit);
			set_bit(BTN_2, dev->keybit);
		case 2:
			set_bit(BTN_1, dev->keybit);
			set_bit(BTN_0, dev->keybit);
			break;
		}
	}

	clear_bit(EV_REL, dev->evbit);
	clear_bit(REL_X, dev->relbit);
	clear_bit(REL_Y, dev->relbit);
}

int synaptics_init(struct psmouse *psmouse)
{
	struct synaptics_data *priv;

#ifndef CONFIG_MOUSE_PS2_SYNAPTICS
	return -1;
#endif

	psmouse->private = priv = kmalloc(sizeof(struct synaptics_data), GFP_KERNEL);
	if (!priv)
		return -1;
	memset(priv, 0, sizeof(struct synaptics_data));

	if (synaptics_query_hardware(psmouse)) {
		printk(KERN_ERR "Unable to query/initialize Synaptics hardware.\n");
		goto init_fail;
	}

	print_ident(priv);
	set_input_params(&psmouse->dev, priv);

	return 0;

 init_fail:
	kfree(priv);
	return -1;
}

void synaptics_disconnect(struct psmouse *psmouse)
{
	struct synaptics_data *priv = psmouse->private;

	if (psmouse->type == PSMOUSE_SYNAPTICS && priv) {
		synaptics_mode_cmd(psmouse, 0);
		if (priv->ptport) {
			serio_unregister_slave_port(priv->ptport);
			kfree(priv->ptport);
		}
		kfree(priv);
	}
}

/*****************************************************************************
 *	Functions to interpret the absolute mode packets
 ****************************************************************************/

static void synaptics_parse_hw_state(unsigned char buf[], struct synaptics_data *priv, struct synaptics_hw_state *hw)
{
	hw->up    = 0;
	hw->down  = 0;
	hw->b0    = 0;
	hw->b1    = 0;
	hw->b2    = 0;
	hw->b3    = 0;
	hw->b4    = 0;
	hw->b5    = 0;
	hw->b6    = 0;
	hw->b7    = 0;

	if (SYN_MODEL_NEWABS(priv->model_id)) {
		hw->x = (((buf[3] & 0x10) << 8) |
			 ((buf[1] & 0x0f) << 8) |
			 buf[4]);
		hw->y = (((buf[3] & 0x20) << 7) |
			 ((buf[1] & 0xf0) << 4) |
			 buf[5]);

		hw->z = buf[2];
		hw->w = (((buf[0] & 0x30) >> 2) |
			 ((buf[0] & 0x04) >> 1) |
			 ((buf[3] & 0x04) >> 2));

		hw->left  = (buf[0] & 0x01) ? 1 : 0;
		hw->right = (buf[0] & 0x02) ? 1 : 0;
		if (SYN_CAP_EXTENDED(priv->capabilities) &&
		    (SYN_CAP_FOUR_BUTTON(priv->capabilities))) {
			hw->up = ((buf[3] & 0x01)) ? 1 : 0;
			if (hw->left)
				hw->up = !hw->up;
			hw->down = ((buf[3] & 0x02)) ? 1 : 0;
			if (hw->right)
				hw->down = !hw->down;
		}
		if (SYN_CAP_MULTI_BUTTON_NO(priv->ext_cap) &&
		    ((buf[3] & 2) ? !hw->right : hw->right)) {
			switch (SYN_CAP_MULTI_BUTTON_NO(priv->ext_cap) & ~0x01) {
			default:
				/*
				 * if nExtBtn is greater than 8 it should be
				 * considered invalid and treated as 0
				 */
				break;
			case 8:
				hw->b7 = ((buf[5] & 0x08)) ? 1 : 0;
				hw->b6 = ((buf[4] & 0x08)) ? 1 : 0;
			case 6:
				hw->b5 = ((buf[5] & 0x04)) ? 1 : 0;
				hw->b4 = ((buf[4] & 0x04)) ? 1 : 0;
			case 4:
				hw->b3 = ((buf[5] & 0x02)) ? 1 : 0;
				hw->b2 = ((buf[4] & 0x02)) ? 1 : 0;
			case 2:
				hw->b1 = ((buf[5] & 0x01)) ? 1 : 0;
				hw->b0 = ((buf[4] & 0x01)) ? 1 : 0;
			}
		}
	} else {
		hw->x = (((buf[1] & 0x1f) << 8) | buf[2]);
		hw->y = (((buf[4] & 0x1f) << 8) | buf[5]);

		hw->z = (((buf[0] & 0x30) << 2) | (buf[3] & 0x3F));
		hw->w = (((buf[1] & 0x80) >> 4) | ((buf[0] & 0x04) >> 1));

		hw->left  = (buf[0] & 0x01) ? 1 : 0;
		hw->right = (buf[0] & 0x02) ? 1 : 0;
	}
}

/*
 *  called for each full received packet from the touchpad
 */
static void synaptics_process_packet(struct psmouse *psmouse)
{
	struct input_dev *dev = &psmouse->dev;
	struct synaptics_data *priv = psmouse->private;
	struct synaptics_hw_state hw;
	int num_fingers;
	int finger_width;

	synaptics_parse_hw_state(psmouse->packet, priv, &hw);

	if (hw.z > 0) {
		num_fingers = 1;
		finger_width = 5;
		if (SYN_CAP_EXTENDED(priv->capabilities)) {
			switch (hw.w) {
			case 0 ... 1:
				if (SYN_CAP_MULTIFINGER(priv->capabilities))
					num_fingers = hw.w + 2;
				break;
			case 2:
				if (SYN_MODEL_PEN(priv->model_id))
					;   /* Nothing, treat a pen as a single finger */
				break;
			case 4 ... 15:
				if (SYN_CAP_PALMDETECT(priv->capabilities))
					finger_width = hw.w;
				break;
			}
		}
	} else {
		num_fingers = 0;
		finger_width = 0;
	}

	/* Post events */
	if (hw.z > 0) {
		input_report_abs(dev, ABS_X, hw.x);
		input_report_abs(dev, ABS_Y, YMAX_NOMINAL + YMIN_NOMINAL - hw.y);
	}
	input_report_abs(dev, ABS_PRESSURE, hw.z);

	if (hw.z > 30) input_report_key(dev, BTN_TOUCH, 1);
	if (hw.z < 25) input_report_key(dev, BTN_TOUCH, 0);

	input_report_abs(dev, ABS_TOOL_WIDTH, finger_width);
	input_report_key(dev, BTN_TOOL_FINGER, num_fingers == 1);
	input_report_key(dev, BTN_TOOL_DOUBLETAP, num_fingers == 2);
	input_report_key(dev, BTN_TOOL_TRIPLETAP, num_fingers == 3);

	input_report_key(dev, BTN_LEFT,    hw.left);
	input_report_key(dev, BTN_RIGHT,   hw.right);
	input_report_key(dev, BTN_FORWARD, hw.up);
	input_report_key(dev, BTN_BACK,    hw.down);
	if (SYN_CAP_MULTI_BUTTON_NO(priv->ext_cap))
		switch(SYN_CAP_MULTI_BUTTON_NO(priv->ext_cap) & ~0x01) {
		default:
			/*
			 * if nExtBtn is greater than 8 it should be considered
			 * invalid and treated as 0
			 */
			break;
		case 8:
			input_report_key(dev, BTN_7,       hw.b7);
			input_report_key(dev, BTN_6,       hw.b6);
		case 6:
			input_report_key(dev, BTN_5,       hw.b5);
			input_report_key(dev, BTN_4,       hw.b4);
		case 4:
			input_report_key(dev, BTN_3,       hw.b3);
			input_report_key(dev, BTN_2,       hw.b2);
		case 2:
			input_report_key(dev, BTN_1,       hw.b1);
			input_report_key(dev, BTN_0,       hw.b0);
			break;
		}
	input_sync(dev);
}

void synaptics_process_byte(struct psmouse *psmouse, struct pt_regs *regs)
{
	struct input_dev *dev = &psmouse->dev;
	struct synaptics_data *priv = psmouse->private;
	unsigned char data = psmouse->packet[psmouse->pktcnt - 1];
	int newabs = SYN_MODEL_NEWABS(priv->model_id);

	input_regs(dev, regs);

	switch (psmouse->pktcnt) {
	case 1:
		if (newabs ? ((data & 0xC8) != 0x80) : ((data & 0xC0) != 0xC0)) {
			printk(KERN_WARNING "Synaptics driver lost sync at 1st byte\n");
			goto bad_sync;
		}
		break;
	case 2:
		if (!newabs && ((data & 0x60) != 0x00)) {
			printk(KERN_WARNING "Synaptics driver lost sync at 2nd byte\n");
			goto bad_sync;
		}
		break;
	case 4:
		if (newabs ? ((data & 0xC8) != 0xC0) : ((data & 0xC0) != 0x80)) {
			printk(KERN_WARNING "Synaptics driver lost sync at 4th byte\n");
			goto bad_sync;
		}
		break;
	case 5:
		if (!newabs && ((data & 0x60) != 0x00)) {
			printk(KERN_WARNING "Synaptics driver lost sync at 5th byte\n");
			goto bad_sync;
		}
		break;
	default:
		if (psmouse->pktcnt < 6)
			break;		    /* Wait for full packet */

		if (priv->out_of_sync) {
			priv->out_of_sync = 0;
			printk(KERN_NOTICE "Synaptics driver resynced.\n");
		}

		if (priv->ptport && synaptics_is_pt_packet(psmouse->packet))
			synaptics_pass_pt_packet(priv->ptport, psmouse->packet);
		else
			synaptics_process_packet(psmouse);

		psmouse->pktcnt = 0;
		break;
	}
	return;

 bad_sync:
	priv->out_of_sync++;
	psmouse->pktcnt = 0;
	if (psmouse_resetafter > 0 && priv->out_of_sync	== psmouse_resetafter) {
		psmouse->state = PSMOUSE_IGNORE;
		serio_rescan(psmouse->serio);
	}
}
