/*
 * $Id: tmdc.c,v 1.18 2000/06/08 19:59:59 vojtech Exp $
 *
 *  Copyright (c) 1998-2000 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 *
 *   Based on the work of:
 *	Trystan Larey-Williams 
 *
 */

/*
 * ThrustMaster DirectConnect (BSP) joystick family driver for Linux
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
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/gameport.h>
#include <linux/input.h>

#define TMDC_MAX_START		400	/* 400 us */
#define TMDC_MAX_STROBE		45	/* 45 us */
#define TMDC_MAX_LENGTH		13
#define TMDC_REFRESH_TIME	HZ/50	/* 20 ms */

#define TMDC_MODE_M3DI		1
#define TMDC_MODE_3DRP		3
#define TMDC_MODE_FGP		163

#define TMDC_BYTE_ID		10
#define TMDC_BYTE_REV		11
#define TMDC_BYTE_DEF		12

#define TMDC_ABS		7	
#define TMDC_ABS_HAT		4
#define TMDC_BTN_PAD		10
#define TMDC_BTN_JOY		16

static unsigned char tmdc_byte_a[16] = { 0, 1, 3, 4, 6, 7 };
static unsigned char tmdc_byte_d[16] = { 2, 5, 8, 9 };

static unsigned char tmdc_abs[TMDC_ABS] =
	{ ABS_X, ABS_Y, ABS_RUDDER, ABS_THROTTLE, ABS_RX, ABS_RY, ABS_RZ };
static unsigned char tmdc_abs_hat[TMDC_ABS_HAT] =
	{ ABS_HAT0X, ABS_HAT0Y, ABS_HAT1X, ABS_HAT1Y };
static unsigned short tmdc_btn_pad[TMDC_BTN_PAD] =
	{ BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z, BTN_START, BTN_SELECT, BTN_TL, BTN_TR };
static unsigned short tmdc_btn_joy[TMDC_BTN_JOY] =
	{ BTN_TRIGGER, BTN_THUMB, BTN_TOP, BTN_TOP2, BTN_BASE, BTN_BASE2, BTN_THUMB2, BTN_PINKIE,
 	  BTN_BASE3, BTN_BASE4, BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z };

struct tmdc {
	struct gameport *gameport;
	struct timer_list timer;
	struct input_dev dev[2];
	char name[2][64];
	int mode[2];
	int used;
	int reads;
	int bads;	
	unsigned char exists;
};

/*
 * tmdc_read_packet() reads a ThrustMaster packet.
 */

static int tmdc_read_packet(struct gameport *gameport, unsigned char data[2][TMDC_MAX_LENGTH])
{
	unsigned char u, v, w, x;
	unsigned long flags;
	int i[2], j[2], t[2], p, k;

	p = gameport_time(gameport, TMDC_MAX_STROBE);

	for (k = 0; k < 2; k++) {
		t[k] = gameport_time(gameport, TMDC_MAX_START);
		i[k] = j[k] = 0;
	}

	__save_flags(flags);
	__cli();
	gameport_trigger(gameport);
	
	w = gameport_read(gameport) >> 4;

	do {
		x = w;
		w = gameport_read(gameport) >> 4;

		for (k = 0, v = w, u = x; k < 2; k++, v >>= 2, u >>= 2) {
			if (~v & u & 2) {
				if (t[k] <= 0 || i[k] >= TMDC_MAX_LENGTH) continue;
				t[k] = p;
				if (j[k] == 0) {				 /* Start bit */
					if (~v & 1) t[k] = 0;
					data[k][i[k]] = 0; j[k]++; continue;
				}
				if (j[k] == 9) {				/* Stop bit */
					if (v & 1) t[k] = 0;
					j[k] = 0; i[k]++; continue;
				}
				data[k][i[k]] |= (~v & 1) << (j[k]++ - 1);	/* Data bit */
			}
			t[k]--; 
		}
	} while (t[0] > 0 || t[1] > 0);

	__restore_flags(flags);

	return (i[0] == TMDC_MAX_LENGTH) | ((i[1] == TMDC_MAX_LENGTH) << 1);
}

/*
 * tmdc_read() reads and analyzes ThrustMaster joystick data.
 */

static void tmdc_timer(unsigned long private)
{
	unsigned char data[2][TMDC_MAX_LENGTH];
	struct tmdc *tmdc = (void *) private;
	struct input_dev *dev;
	unsigned char r, bad = 0;
	int i, j;

	tmdc->reads++;

	if ((r = tmdc_read_packet(tmdc->gameport, data)) != tmdc->exists)
		bad = 1;

	for (j = 0; j < 2; j++) 
		if (r & (1 << j) & tmdc->exists) {

			if (data[j][TMDC_BYTE_ID] != tmdc->mode[j]) {
				bad = 1;
				continue;
			}

			dev = tmdc->dev + j;

			for (i = 0; i < data[j][TMDC_BYTE_DEF] >> 4; i++)
				input_report_abs(dev, tmdc_abs[i], data[j][tmdc_byte_a[i]]);

			switch (tmdc->mode[j]) {

				case TMDC_MODE_M3DI:

					i = tmdc_byte_d[0];

					input_report_abs(dev, ABS_HAT0X, ((data[j][i] >> 3) & 1) - ((data[j][i] >> 1) & 1));
					input_report_abs(dev, ABS_HAT0Y, ((data[j][i] >> 2) & 1) - ( data[j][i]       & 1));

					for (i = 0; i < 4; i++)
						input_report_key(dev, tmdc_btn_joy[i],
							(data[j][tmdc_byte_d[0]] >> (i + 4)) & 1);
					for (i = 0; i < 2; i++)
						input_report_key(dev, tmdc_btn_joy[i + 4],
							(data[j][tmdc_byte_d[1]] >> (i + 6)) & 1);

					break;

				case TMDC_MODE_3DRP:
				case TMDC_MODE_FGP:

					for (i = 0; i < 10; i++)
						input_report_key(dev, tmdc_btn_pad[i],
							(data[j][tmdc_byte_d[i >> 3]] >> (i & 7)) & 1);

					break;

				default:

					for (i = 0; i < ((data[j][TMDC_BYTE_DEF] & 0xf) << 3) && i < TMDC_BTN_JOY; i++)
						input_report_key(dev, tmdc_btn_joy[i],
							(data[j][tmdc_byte_d[i >> 3]] >> (i & 7)) & 1);

					break;

			}
	}

	tmdc->bads += bad;

	mod_timer(&tmdc->timer, jiffies + TMDC_REFRESH_TIME);
}

static int tmdc_open(struct input_dev *dev)
{
	struct tmdc *tmdc = dev->private;
	if (!tmdc->used++)
		mod_timer(&tmdc->timer, jiffies + TMDC_REFRESH_TIME);	
	return 0;
}

static void tmdc_close(struct input_dev *dev)
{
	struct tmdc *tmdc = dev->private;
	if (!--tmdc->used)
		del_timer(&tmdc->timer);
}

/*
 * tmdc_probe() probes for ThrustMaster type joysticks.
 */

static void tmdc_connect(struct gameport *gameport, struct gameport_dev *dev)
{
	struct tmdc *tmdc;
	struct js_tm_models {
		unsigned char id;
		char *name;
		char abs;
		char hats;
		char joybtn;
		char padbtn;
	} models[] = {	{   1, "ThrustMaster Millenium 3D Inceptor",	  6, 0, 6,  0 },
			{   3, "ThrustMaster Rage 3D Gamepad",		  2, 0, 0, 10 },
			{ 163, "Thrustmaster Fusion GamePad",		  2, 0, 0, 10 },
			{   0, "Unknown %d-axis, %d-button TM device %d", 0, 0, 0,  0 }};
	unsigned char data[2][TMDC_MAX_LENGTH];
	int i, j, m;

	if (!(tmdc = kmalloc(sizeof(struct tmdc), GFP_KERNEL)))
		return;
	memset(tmdc, 0, sizeof(struct tmdc));

	gameport->private = tmdc;

	tmdc->gameport = gameport;
	init_timer(&tmdc->timer);
	tmdc->timer.data = (long) tmdc;
	tmdc->timer.function = tmdc_timer;

	if (gameport_open(gameport, dev, GAMEPORT_MODE_RAW))
		goto fail1;

	if (!(tmdc->exists = tmdc_read_packet(gameport, data)))
		goto fail2;

	for (j = 0; j < 2; j++)
		if (tmdc->exists & (1 << j)) {

			tmdc->mode[j] = data[j][TMDC_BYTE_ID];

			for (m = 0; models[m].id && models[m].id != tmdc->mode[j]; m++);

			if (!models[m].id) {
				models[m].abs = data[j][TMDC_BYTE_DEF] >> 4;
				models[m].joybtn = (data[j][TMDC_BYTE_DEF] & 0xf) << 3;
			}

			sprintf(tmdc->name[j], models[m].name, models[m].abs, models[m].joybtn, tmdc->mode[j]);

			tmdc->dev[j].private = tmdc;
			tmdc->dev[j].open = tmdc_open;
			tmdc->dev[j].close = tmdc_close;

			tmdc->dev[j].name = tmdc->name[j];
			tmdc->dev[j].idbus = BUS_GAMEPORT;
			tmdc->dev[j].idvendor = GAMEPORT_ID_VENDOR_THRUSTMASTER;
			tmdc->dev[j].idproduct = models[m].id;
			tmdc->dev[j].idversion = 0x0100;

			tmdc->dev[j].evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);

			for (i = 0; i < models[m].abs && i < TMDC_ABS; i++) {
				set_bit(tmdc_abs[i], tmdc->dev[j].absbit);
				tmdc->dev[j].absmin[tmdc_abs[i]] = 8;
				tmdc->dev[j].absmax[tmdc_abs[i]] = 248;
				tmdc->dev[j].absfuzz[tmdc_abs[i]] = 2;
				tmdc->dev[j].absflat[tmdc_abs[i]] = 4;
			}

			for (i = 0; i < models[m].hats && i < TMDC_ABS_HAT; i++) {
				set_bit(tmdc_abs_hat[i], tmdc->dev[j].absbit);
				tmdc->dev[j].absmin[tmdc_abs_hat[i]] = -1;
				tmdc->dev[j].absmax[tmdc_abs_hat[i]] = 1;
			}

			for (i = 0; i < models[m].joybtn && i < TMDC_BTN_JOY; i++)
				set_bit(tmdc_btn_joy[i], tmdc->dev[j].keybit);

			for (i = 0; i < models[m].padbtn && i < TMDC_BTN_PAD; i++)
				set_bit(tmdc_btn_pad[i], tmdc->dev[j].keybit);

			input_register_device(tmdc->dev + j);
			printk(KERN_INFO "input%d: %s on gameport%d.%d\n",
				tmdc->dev[j].number, tmdc->name[j], gameport->number, j);
		}

	return;
fail2:	gameport_close(gameport);
fail1:	kfree(tmdc);
}

static void tmdc_disconnect(struct gameport *gameport)
{
	struct tmdc *tmdc = gameport->private;
	int i;
	for (i = 0; i < 2; i++)
		if (tmdc->exists & (1 << i)) 
			input_unregister_device(tmdc->dev + i);
	gameport_close(gameport);
	kfree(tmdc);
}

static struct gameport_dev tmdc_dev = {
	connect:	tmdc_connect,
	disconnect:	tmdc_disconnect,
};

int __init tmdc_init(void)
{
	gameport_register_device(&tmdc_dev);
	return 0;
}

void __exit tmdc_exit(void)
{
	gameport_unregister_device(&tmdc_dev);
}

module_init(tmdc_init);
module_exit(tmdc_exit);
