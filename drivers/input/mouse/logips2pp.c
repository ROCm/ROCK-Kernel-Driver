/*
 * Logitech PS/2++ mouse driver
 *
 * Copyright (c) 1999-2003 Vojtech Pavlik <vojtech@suse.cz>
 * Copyright (c) 2003 Eric Wong <eric@yhbt.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/input.h>
#include <linux/serio.h>
#include "psmouse.h"
#include "logips2pp.h"

/*
 * Process a PS2++ or PS2T++ packet.
 */

void ps2pp_process_packet(struct psmouse *psmouse)
{
	struct input_dev *dev = &psmouse->dev;
        unsigned char *packet = psmouse->packet;

	if ((packet[0] & 0x48) == 0x48 && (packet[1] & 0x02) == 0x02) {

		switch ((packet[1] >> 4) | (packet[0] & 0x30)) {

			case 0x0d: /* Mouse extra info */

				input_report_rel(dev, packet[2] & 0x80 ? REL_HWHEEL : REL_WHEEL,
					(int) (packet[2] & 8) - (int) (packet[2] & 7));
				input_report_key(dev, BTN_SIDE, (packet[2] >> 4) & 1);
				input_report_key(dev, BTN_EXTRA, (packet[2] >> 5) & 1);

				break;

			case 0x0e: /* buttons 4, 5, 6, 7, 8, 9, 10 info */

				input_report_key(dev, BTN_SIDE, (packet[2]) & 1);
				input_report_key(dev, BTN_EXTRA, (packet[2] >> 1) & 1);
				input_report_key(dev, BTN_BACK, (packet[2] >> 3) & 1);
				input_report_key(dev, BTN_FORWARD, (packet[2] >> 4) & 1);
				input_report_key(dev, BTN_TASK, (packet[2] >> 2) & 1);

				break;

			case 0x0f: /* TouchPad extra info */

				input_report_rel(dev, packet[2] & 0x08 ? REL_HWHEEL : REL_WHEEL,
					(int) ((packet[2] >> 4) & 8) - (int) ((packet[2] >> 4) & 7));
				packet[0] = packet[2] | 0x08;
				break;

#ifdef DEBUG
			default:
				printk(KERN_WARNING "psmouse.c: Received PS2++ packet #%x, but don't know how to handle.\n",
					(packet[1] >> 4) | (packet[0] & 0x30));
#endif
		}

		packet[0] &= 0x0f;
		packet[1] = 0;
		packet[2] = 0;
	}
}

/*
 * ps2pp_cmd() sends a PS2++ command, sliced into two bit
 * pieces through the SETRES command. This is needed to send extended
 * commands to mice on notebooks that try to understand the PS/2 protocol
 * Ugly.
 */

static int ps2pp_cmd(struct psmouse *psmouse, unsigned char *param, unsigned char command)
{
	if (psmouse_sliced_command(psmouse, command))
		return -1;

	if (psmouse_command(psmouse, param, PSMOUSE_CMD_POLL))
		return -1;

	return 0;
}

/*
 * SmartScroll / CruiseControl for some newer Logitech mice Defaults to
 * enabled if we do nothing to it. Of course I put this in because I want it
 * disabled :P
 * 1 - enabled (if previously disabled, also default)
 * 0/2 - disabled 
 */

static void ps2pp_set_smartscroll(struct psmouse *psmouse)
{
	unsigned char param[4];

	ps2pp_cmd(psmouse, param, 0x32);

	param[0] = 0;
	psmouse_command(psmouse, param, PSMOUSE_CMD_SETRES);
	psmouse_command(psmouse, param, PSMOUSE_CMD_SETRES);
	psmouse_command(psmouse, param, PSMOUSE_CMD_SETRES);

	if (psmouse_smartscroll == 1) 
		param[0] = 1;
	else
	if (psmouse_smartscroll > 2)
		return;

	/* else leave param[0] == 0 to disable */
	psmouse_command(psmouse, param, PSMOUSE_CMD_SETRES);
}

/*
 * Support 800 dpi resolution _only_ if the user wants it (there are good
 * reasons to not use it even if the mouse supports it, and of course there are
 * also good reasons to use it, let the user decide).
 */

void ps2pp_set_800dpi(struct psmouse *psmouse)
{
	unsigned char param = 3;
	psmouse_command(psmouse, NULL, PSMOUSE_CMD_SETSCALE11);
	psmouse_command(psmouse, NULL, PSMOUSE_CMD_SETSCALE11);
	psmouse_command(psmouse, NULL, PSMOUSE_CMD_SETSCALE11);
	psmouse_command(psmouse, &param, PSMOUSE_CMD_SETRES);
}

/*
 * Detect the exact model and features of a PS2++ or PS2T++ Logitech mouse or
 * touchpad.
 */

static int ps2pp_detect_model(struct psmouse *psmouse, unsigned char *param)
{
	int i;
	static int logitech_4btn[] = { 12, 40, 41, 42, 43, 52, 73, 80, -1 };
	static int logitech_wheel[] = { 52, 53, 75, 76, 80, 81, 83, 88, 112, -1 };
	static int logitech_ps2pp[] = { 12, 13, 40, 41, 42, 43, 50, 51, 52, 53, 73, 75,
						76, 80, 81, 83, 88, 96, 97, 112, -1 };
	static int logitech_mx[] = { 61, 112, -1 };

	psmouse->vendor = "Logitech";
	psmouse->model = ((param[0] >> 4) & 0x07) | ((param[0] << 3) & 0x78);

	if (param[1] < 3)
		clear_bit(BTN_MIDDLE, psmouse->dev.keybit);
	if (param[1] < 2)
		clear_bit(BTN_RIGHT, psmouse->dev.keybit);

	psmouse->type = PSMOUSE_PS2;

	for (i = 0; logitech_ps2pp[i] != -1; i++)
		if (logitech_ps2pp[i] == psmouse->model)
			psmouse->type = PSMOUSE_PS2PP;

	if (psmouse->type == PSMOUSE_PS2PP) {

		for (i = 0; logitech_4btn[i] != -1; i++)
			if (logitech_4btn[i] == psmouse->model)
				set_bit(BTN_SIDE, psmouse->dev.keybit);

		for (i = 0; logitech_wheel[i] != -1; i++)
			if (logitech_wheel[i] == psmouse->model) {
				set_bit(REL_WHEEL, psmouse->dev.relbit);
				psmouse->name = "Wheel Mouse";
			}

		for (i = 0; logitech_mx[i] != -1; i++)
			if (logitech_mx[i]  == psmouse->model) {
				set_bit(BTN_SIDE, psmouse->dev.keybit);
				set_bit(BTN_EXTRA, psmouse->dev.keybit);
				set_bit(BTN_BACK, psmouse->dev.keybit);
				set_bit(BTN_FORWARD, psmouse->dev.keybit);
				set_bit(BTN_TASK, psmouse->dev.keybit);
				psmouse->name = "MX Mouse";
			}

/*
 * Do Logitech PS2++ / PS2T++ magic init.
 */

		if (psmouse->model == 97) { /* TouchPad 3 */

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
				param[0] == 0x06 && param[1] == 0x00 && param[2] == 0x14) {
				psmouse->name = "TouchPad 3";
				return PSMOUSE_PS2TPP;
			}

		} else {

			param[0] = param[1] = param[2] = 0;
			ps2pp_cmd(psmouse, param, 0x39); /* Magic knock */
			ps2pp_cmd(psmouse, param, 0xDB);

			if ((param[0] & 0x78) == 0x48 && (param[1] & 0xf3) == 0xc2 &&
				(param[2] & 3) == ((param[1] >> 2) & 3)) {
					ps2pp_set_smartscroll(psmouse);
					return PSMOUSE_PS2PP;
			}
		}
	}

	return 0;
}

/*
 * Logitech magic init.
 */
int ps2pp_detect(struct psmouse *psmouse)
{
	unsigned char param[4];

	param[0] = 0;
	psmouse_command(psmouse, param, PSMOUSE_CMD_SETRES);
	psmouse_command(psmouse,  NULL, PSMOUSE_CMD_SETSCALE11);
	psmouse_command(psmouse,  NULL, PSMOUSE_CMD_SETSCALE11);
	psmouse_command(psmouse,  NULL, PSMOUSE_CMD_SETSCALE11);
	param[1] = 0;
	psmouse_command(psmouse, param, PSMOUSE_CMD_GETINFO);

	return param[1] != 0 ? ps2pp_detect_model(psmouse, param) : 0;
}

