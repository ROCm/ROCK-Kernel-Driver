/*
 * $Id: hid-input.c,v 1.5 2001/05/23 09:25:02 vojtech Exp $
 *
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *
 *  USB HID to Linux Input mapping module
 *
 *  Sponsored by SuSE
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

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/usb.h>

#include "hid.h"

#define unk	KEY_UNKNOWN

static unsigned char hid_keyboard[256] = {
	  0,  0,  0,  0, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38,
	 50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,  2,  3,
	  4,  5,  6,  7,  8,  9, 10, 11, 28,  1, 14, 15, 57, 12, 13, 26,
	 27, 43, 84, 39, 40, 41, 51, 52, 53, 58, 59, 60, 61, 62, 63, 64,
	 65, 66, 67, 68, 87, 88, 99, 70,119,110,102,104,111,107,109,106,
	105,108,103, 69, 98, 55, 74, 78, 96, 79, 80, 81, 75, 76, 77, 71,
	 72, 73, 82, 83, 86,127,116,117, 85, 89, 90, 91, 92, 93, 94, 95,
	120,121,122,123,134,138,130,132,128,129,131,137,133,135,136,113,
	115,114,unk,unk,unk,124,unk,181,182,183,184,185,186,187,188,189,
	190,191,192,193,194,195,196,197,198,unk,unk,unk,unk,unk,unk,unk,
	unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,
	unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,
	unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,
	unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,
	 29, 42, 56,125, 97, 54,100,126,164,166,165,163,161,115,114,113,
	150,158,159,128,136,177,178,176,142,152,173,140,unk,unk,unk,unk
};

static struct {
	__s32 x;
	__s32 y;
}  hid_hat_to_axis[] = {{0, 0}, { 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};

static void hidinput_configure_usage(struct hid_device *device, struct hid_field *field, struct hid_usage *usage)
{
	struct input_dev *input = &device->input;
	int max;
	unsigned long *bit;

	switch (usage->hid & HID_USAGE_PAGE) {

		case HID_UP_KEYBOARD:

			set_bit(EV_REP, input->evbit);
			usage->type = EV_KEY; bit = input->keybit; max = KEY_MAX;

			if ((usage->hid & HID_USAGE) < 256) {
				if (!(usage->code = hid_keyboard[usage->hid & HID_USAGE]))
					return;
				clear_bit(usage->code, bit);
			} else
				usage->code = KEY_UNKNOWN;

			break;

		case HID_UP_BUTTON:

			usage->code = ((usage->hid - 1) & 0xf) + 0x100;
			usage->type = EV_KEY; bit = input->keybit; max = KEY_MAX;

			switch (field->application) {
				case HID_GD_GAMEPAD:  usage->code += 0x10;
				case HID_GD_JOYSTICK: usage->code += 0x10;
				case HID_GD_MOUSE:    usage->code += 0x10; break;
				default:
					if (field->physical == HID_GD_POINTER)
						usage->code += 0x10;
					break;
			}
			break;

		case HID_UP_GENDESK:

			if ((usage->hid & 0xf0) == 0x80) {	/* SystemControl */
				switch (usage->hid & 0xf) {
					case 0x1: usage->code = KEY_POWER;  break;
					case 0x2: usage->code = KEY_SLEEP;  break;
					case 0x3: usage->code = KEY_WAKEUP; break;
					default: usage->code = KEY_UNKNOWN; break;
				}
				usage->type = EV_KEY; bit = input->keybit; max = KEY_MAX;
				break;
			}

			usage->code = usage->hid & 0xf;

			if (field->report_size == 1) {
				usage->code = BTN_MISC;
				usage->type = EV_KEY; bit = input->keybit; max = KEY_MAX;
				break;
			}

			if (field->flags & HID_MAIN_ITEM_RELATIVE) {
				usage->type = EV_REL; bit = input->relbit; max = REL_MAX;
				break;
			}

			usage->type = EV_ABS; bit = input->absbit; max = ABS_MAX;

			if (usage->hid == HID_GD_HATSWITCH) {
				usage->code = ABS_HAT0X;
				usage->hat_min = field->logical_minimum;
				usage->hat_max = field->logical_maximum;
			}
			break;

		case HID_UP_LED:

			usage->code = (usage->hid - 1) & 0xf;
			usage->type = EV_LED; bit = input->ledbit; max = LED_MAX;
			break;

		case HID_UP_DIGITIZER:

			switch (usage->hid & 0xff) {

				case 0x30: /* TipPressure */

					if (!test_bit(BTN_TOUCH, input->keybit)) {
						device->quirks |= HID_QUIRK_NOTOUCH;
						set_bit(EV_KEY, input->evbit);
						set_bit(BTN_TOUCH, input->keybit);
					}
					usage->type = EV_ABS; bit = input->absbit; max = ABS_MAX;
					usage->code = ABS_PRESSURE;
					clear_bit(usage->code, bit);
					break;

				case 0x32: /* InRange */

					usage->type = EV_KEY; bit = input->keybit; max = KEY_MAX;
					switch (field->physical & 0xff) {
						case 0x21: usage->code = BTN_TOOL_MOUSE; break;
						case 0x22: usage->code = BTN_TOOL_FINGER; break;
						default: usage->code = BTN_TOOL_PEN; break;
					}
					break;

				case 0x3c: /* Invert */

					usage->type = EV_KEY; bit = input->keybit; max = KEY_MAX;
					usage->code = BTN_TOOL_RUBBER;
					clear_bit(usage->code, bit);
					break;

				case 0x33: /* Touch */
				case 0x42: /* TipSwitch */
				case 0x43: /* TipSwitch2 */

					device->quirks &= ~HID_QUIRK_NOTOUCH;
					usage->type = EV_KEY; bit = input->keybit; max = KEY_MAX;
					usage->code = BTN_TOUCH;
					clear_bit(usage->code, bit);
					break;

				case 0x44: /* BarrelSwitch */

					usage->type = EV_KEY; bit = input->keybit; max = KEY_MAX;
					usage->code = BTN_STYLUS;
					clear_bit(usage->code, bit);
					break;

				default:  goto unknown;
			}
			break;

		case HID_UP_CONSUMER:	/* USB HUT v1.1, pages 56-62 */

			switch (usage->hid & HID_USAGE) {
				case 0x000: usage->code = 0; break;
				case 0x034: usage->code = KEY_SLEEP;		break;
				case 0x036: usage->code = BTN_MISC;		break;
				case 0x08a: usage->code = KEY_WWW;		break;
				case 0x095: usage->code = KEY_HELP;		break;

				case 0x0b4: usage->code = KEY_REWIND;		break;
				case 0x0b5: usage->code = KEY_NEXTSONG;		break;
				case 0x0b6: usage->code = KEY_PREVIOUSSONG;	break;
				case 0x0b7: usage->code = KEY_STOPCD;		break;
				case 0x0b8: usage->code = KEY_EJECTCD;		break;
				case 0x0cd: usage->code = KEY_PLAYPAUSE;	break;

				case 0x0e2: usage->code = KEY_MUTE;		break;
				case 0x0e9: usage->code = KEY_VOLUMEUP;		break;
				case 0x0ea: usage->code = KEY_VOLUMEDOWN;	break;

				case 0x183: usage->code = KEY_CONFIG;		break;
				case 0x18a: usage->code = KEY_MAIL;		break;
				case 0x192: usage->code = KEY_CALC;		break;
				case 0x194: usage->code = KEY_FILE;		break;

				case 0x21a: usage->code = KEY_UNDO;		break;
				case 0x21b: usage->code = KEY_COPY;		break;
				case 0x21c: usage->code = KEY_CUT;		break;
				case 0x21d: usage->code = KEY_PASTE;		break;

				case 0x221: usage->code = KEY_FIND;		break;
				case 0x223: usage->code = KEY_HOMEPAGE;		break;
				case 0x224: usage->code = KEY_BACK;		break;
				case 0x225: usage->code = KEY_FORWARD;		break;
				case 0x226: usage->code = KEY_STOP;		break;
				case 0x227: usage->code = KEY_REFRESH;		break;
				case 0x22a: usage->code = KEY_BOOKMARKS;	break;

				default:    usage->code = KEY_UNKNOWN;		break;

			}

			usage->type = EV_KEY; bit = input->keybit; max = KEY_MAX;
			break;

		default:
		unknown:

			if (field->report_size == 1) {

				if (field->report->type == HID_OUTPUT_REPORT) {
					usage->code = LED_MISC;
					usage->type = EV_LED; bit = input->ledbit; max = LED_MAX;
					break;
				}

				usage->code = BTN_MISC;
				usage->type = EV_KEY; bit = input->keybit; max = KEY_MAX;
				break;
			}

			if (field->flags & HID_MAIN_ITEM_RELATIVE) {
				usage->code = REL_MISC;
				usage->type = EV_REL; bit = input->relbit; max = REL_MAX;
				break;
			}

			usage->code = ABS_MISC;
			usage->type = EV_ABS; bit = input->absbit; max = ABS_MAX;
			break;
	}

	set_bit(usage->type, input->evbit);

	while (usage->code <= max && test_and_set_bit(usage->code, bit)) {
		usage->code = find_next_zero_bit(bit, max + 1, usage->code);
	}

	if (usage->code > max) return;

	if (usage->type == EV_ABS) {
		int a = field->logical_minimum;
		int b = field->logical_maximum;

		input->absmin[usage->code] = a;
		input->absmax[usage->code] = b;
		input->absfuzz[usage->code] = (b - a) >> 8;
		input->absflat[usage->code] = (b - a) >> 4;
	}

	if (usage->hat_min != usage->hat_max) {
		int i;
		for (i = usage->code; i < usage->code + 2 && i <= max; i++) {
			input->absmax[i] = 1;
			input->absmin[i] = -1;
			input->absfuzz[i] = 0;
			input->absflat[i] = 0;
		}
		set_bit(usage->code + 1, input->absbit);
	}
}

void hidinput_hid_event(struct hid_device *hid, struct hid_field *field, struct hid_usage *usage, __s32 value)
{
	struct input_dev *input = &hid->input;
	int *quirks = &hid->quirks;

	if (usage->hat_min != usage->hat_max) {
		value = (value - usage->hat_min) * 8 / (usage->hat_max - usage->hat_min + 1) + 1;
		if (value < 0 || value > 8) value = 0;
		input_event(input, usage->type, usage->code    , hid_hat_to_axis[value].x);
		input_event(input, usage->type, usage->code + 1, hid_hat_to_axis[value].y);
		return;
	}

	if (usage->hid == (HID_UP_DIGITIZER | 0x003c)) { /* Invert */
		*quirks = value ? (*quirks | HID_QUIRK_INVERT) : (*quirks & ~HID_QUIRK_INVERT);
		return;
	}

	if (usage->hid == (HID_UP_DIGITIZER | 0x0032)) { /* InRange */
		if (value) {
			input_event(input, usage->type, (*quirks & HID_QUIRK_INVERT) ? BTN_TOOL_RUBBER : usage->code, 1);
			return;
		}
		input_event(input, usage->type, usage->code, 0);
		input_event(input, usage->type, BTN_TOOL_RUBBER, 0);
		return;
	}

	if (usage->hid == (HID_UP_DIGITIZER | 0x0030) && (*quirks & HID_QUIRK_NOTOUCH)) { /* Pressure */
		int a = field->logical_minimum;
		int b = field->logical_maximum;
		input_event(input, EV_KEY, BTN_TOUCH, value > a + ((b - a) >> 3));
	}

	if((usage->type == EV_KEY) && (usage->code == 0)) /* Key 0 is "unassigned", not KEY_UKNOWN */
		return;

	input_event(input, usage->type, usage->code, value);

	if ((field->flags & HID_MAIN_ITEM_RELATIVE) && (usage->type == EV_KEY))
		input_event(input, usage->type, usage->code, 0);
}

static int hidinput_input_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct hid_device *hid = dev->private;
	struct hid_field *field = NULL;
	int offset;

	if ((offset = hid_find_field(hid, type, code, &field)) == -1) {
		warn("event field not found");
		return -1;
	}

	hid_set_field(field, offset, value);
	hid_write_report(hid, field->report);

	return 0;
}

static int hidinput_open(struct input_dev *dev)
{
	struct hid_device *hid = dev->private;
	return hid_open(hid);
}

static void hidinput_close(struct input_dev *dev)
{
	struct hid_device *hid = dev->private;
	hid_close(hid);
}

/*
 * Register the input device; print a message.
 * Configure the input layer interface
 * Read all reports and initalize the absoulte field values.
 */

int hidinput_connect(struct hid_device *hid)
{
	struct usb_device *dev = hid->dev;
	struct hid_report_enum *report_enum;
	struct hid_report *report;
	struct list_head *list;
	int i, j, k;

	for (i = 0; i < hid->maxapplication; i++)
		if (IS_INPUT_APPLICATION(hid->application[i]))
			break;

	if (i == hid->maxapplication)
		return -1;

	hid->input.private = hid;
	hid->input.event = hidinput_input_event;
	hid->input.open = hidinput_open;
	hid->input.close = hidinput_close;

	hid->input.name = hid->name;
	hid->input.idbus = BUS_USB;
	hid->input.idvendor = dev->descriptor.idVendor;
	hid->input.idproduct = dev->descriptor.idProduct;
	hid->input.idversion = dev->descriptor.bcdDevice;

	for (k = HID_INPUT_REPORT; k <= HID_OUTPUT_REPORT; k++) {
		report_enum = hid->report_enum + k;
		list = report_enum->report_list.next;
		while (list != &report_enum->report_list) {
			report = (struct hid_report *) list;
			for (i = 0; i < report->maxfield; i++)
				for (j = 0; j < report->field[i]->maxusage; j++)
					hidinput_configure_usage(hid, report->field[i], report->field[i]->usage + j);
			list = list->next;
		}
	}

	input_register_device(&hid->input);

	return 0;
}

void hidinput_disconnect(struct hid_device *hid)
{
	input_unregister_device(&hid->input);
}
