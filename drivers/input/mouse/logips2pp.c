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
	static struct _logips2_list {
		const int model;
		unsigned const int features;
	} logips2pp_list [] = {
		{ 12,	PS2PP_4BTN},
		{ 13,	0 },
		{ 40,	PS2PP_4BTN },
		{ 41,	PS2PP_4BTN },
		{ 42,	PS2PP_4BTN },
		{ 43,	PS2PP_4BTN },
		{ 50,	0 },
		{ 51,	0 },
		{ 52,	PS2PP_4BTN | PS2PP_WHEEL },
		{ 53,	PS2PP_WHEEL },
		{ 61,	PS2PP_WHEEL | PS2PP_MX },	/* MX700 */
		{ 73,	PS2PP_4BTN },
		{ 75,	PS2PP_WHEEL },
		{ 76,	PS2PP_WHEEL },
		{ 80,	PS2PP_4BTN | PS2PP_WHEEL },
		{ 81,	PS2PP_WHEEL },
		{ 83,	PS2PP_WHEEL },
		{ 88,	PS2PP_WHEEL },
		{ 96,	0 },
		{ 97,	0 },
		{ 100 ,	PS2PP_WHEEL | PS2PP_MX },	/* MX510 */
		{ 112 ,	PS2PP_WHEEL | PS2PP_MX },	/* MX500 */
		{ 114 ,	PS2PP_WHEEL | PS2PP_MX | PS2PP_MX310 },	/* MX310 */
		{ }
	};

	psmouse->vendor = "Logitech";
	psmouse->model = ((param[0] >> 4) & 0x07) | ((param[0] << 3) & 0x78);

	if (param[1] < 3)
		clear_bit(BTN_MIDDLE, psmouse->dev.keybit);
	if (param[1] < 2)
		clear_bit(BTN_RIGHT, psmouse->dev.keybit);

	psmouse->type = PSMOUSE_PS2;

	for (i = 0; logips2pp_list[i].model; i++){
		if (logips2pp_list[i].model == psmouse->model){
			psmouse->type = PSMOUSE_PS2PP;
			if (logips2pp_list[i].features & PS2PP_4BTN)
				set_bit(BTN_SIDE, psmouse->dev.keybit);

			if (logips2pp_list[i].features & PS2PP_WHEEL){
				set_bit(REL_WHEEL, psmouse->dev.relbit);
				psmouse->name = "Wheel Mouse";
			}
			if (logips2pp_list[i].features & PS2PP_MX) {
				set_bit(BTN_SIDE, psmouse->dev.keybit);
				set_bit(BTN_EXTRA, psmouse->dev.keybit);
				set_bit(BTN_TASK, psmouse->dev.keybit);
				if (!(logips2pp_list[i].features & PS2PP_MX310)){
					set_bit(BTN_BACK, psmouse->dev.keybit);
					set_bit(BTN_FORWARD, psmouse->dev.keybit);
				}
				psmouse->name = "MX Mouse";
			}
			break;
		}
	}
/*
 * Do Logitech PS2++ / PS2T++ magic init.
 */
	if (psmouse->type == PSMOUSE_PS2PP) {

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

