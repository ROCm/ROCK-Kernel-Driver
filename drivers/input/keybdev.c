/*
 * $Id: keybdev.c,v 1.19 2002/03/13 10:09:20 vojtech Exp $
 *
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 *
 *  Input core to console keyboard binding.
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

#include <linux/config.h>
#include <linux/kbd_ll.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/kbd_kern.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Input core to console keyboard binding");
MODULE_LICENSE("GPL");

char keybdev_name[] = "keyboard";

#if defined(CONFIG_X86) || defined(CONFIG_IA64) || defined(__alpha__) || \
    defined(__mips__) || defined(CONFIG_SPARC64) || defined(CONFIG_SUPERH) || \
    defined(CONFIG_PPC) || defined(__mc68000__) || defined(__hppa__) || \
    defined(__arm__) || defined(__x86_64__)

static int x86_sysrq_alt = 0;
#ifdef CONFIG_SPARC64
static int sparc_l1_a_state = 0;
#endif

static unsigned short x86_keycodes[256] =
	{ 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
	 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
	 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
	 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
	 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
	 80, 81, 82, 83, 43, 85, 86, 87, 88,115,119,120,121,375,123, 90,
	284,285,309,298,312, 91,327,328,329,331,333,335,336,337,338,339,
	367,294,293,286,350, 92,334,512,116,377,109,111,373,347,348,349,
	360, 93, 94, 95, 98,376,100,101,357,316,354,304,289,102,351,355,
	103,104,105,275,281,272,306,106,274,107,288,364,358,363,362,361,
	291,108,381,290,287,292,279,305,280, 99,112,257,258,113,270,114,
	118,117,125,374,379,115,112,125,121,123,264,265,266,267,268,269,
	271,273,276,277,278,282,283,295,296,297,299,300,301,302,303,307,
	308,310,313,314,315,317,318,319,320,321,322,323,324,325,326,330,
	332,340,341,342,343,344,345,346,356,359,365,368,369,370,371,372 };

#ifdef CONFIG_MAC_EMUMOUSEBTN
extern int mac_hid_mouse_emulate_buttons(int, int, int);
#endif /* CONFIG_MAC_EMUMOUSEBTN */
 
static int emulate_raw(unsigned int keycode, int down)
{
#ifdef CONFIG_MAC_EMUMOUSEBTN
	if (mac_hid_mouse_emulate_buttons(1, keycode, down))
		return 0;
#endif /* CONFIG_MAC_EMUMOUSEBTN */

	if (keycode > 255 || !x86_keycodes[keycode])
		return -1; 

	if (keycode == KEY_PAUSE) {
		handle_scancode(0xe1, 1);
		handle_scancode(0x1d, down);
		handle_scancode(0x45, down);
		return 0;
	} 

	if (keycode == KEY_SYSRQ && x86_sysrq_alt) {
		handle_scancode(0x54, down);

		return 0;
	}

#ifdef CONFIG_SPARC64
	if (keycode == KEY_A && sparc_l1_a_state) {
		sparc_l1_a_state = 0;
		sun_do_break();
	}
#endif

	if (x86_keycodes[keycode] & 0x100)
		handle_scancode(0xe0, 1);

	handle_scancode(x86_keycodes[keycode] & 0x7f, down);

	if (keycode == KEY_SYSRQ) {
		handle_scancode(0xe0, 1);
		handle_scancode(0x37, down);
	}

	if (keycode == KEY_LEFTALT || keycode == KEY_RIGHTALT)
		x86_sysrq_alt = down;
#ifdef CONFIG_SPARC64
	if (keycode == KEY_STOP)
		sparc_l1_a_state = down;
#endif

	return 0;
}

#endif /* CONFIG_X86 || CONFIG_IA64 || __alpha__ || __mips__ || CONFIG_PPC */

static struct input_handler keybdev_handler;

void keybdev_ledfunc(unsigned int led)
{
	struct input_handle *handle;	

	for (handle = keybdev_handler.handle; handle; handle = handle->hnext) {
		input_event(handle->dev, EV_LED, LED_SCROLLL, !!(led & 0x01));
		input_event(handle->dev, EV_LED, LED_NUML,    !!(led & 0x02));
		input_event(handle->dev, EV_LED, LED_CAPSL,   !!(led & 0x04));
		input_sync(handle->dev);
	}
}

/* Tell the user who may be running in X and not see the console that we have 
   panic'ed. This is to distingush panics from "real" lockups. 
   Could in theory send the panic message as morse, but that is left as an
   exercise for the reader.  */ 

void panic_blink(void)
{ 
	static unsigned long last_jiffie;
	static char led;
	/* Roughly 1/2s frequency. KDB uses about 1s. Make sure it is different. */
	if (time_after(jiffies, last_jiffie + HZ/2)) {
		led ^= 0x01 | 0x04;
		keybdev_ledfunc(led);
		last_jiffie = jiffies;
	}
}  

void keybdev_event(struct input_handle *handle, unsigned int type, unsigned int code, int down)
{
	if (type != EV_KEY) return;
	emulate_raw(code, down);
	tasklet_schedule(&keyboard_tasklet);
}

static struct input_handle *keybdev_connect(struct input_handler *handler, struct input_dev *dev, struct input_device_id *id)
{
	struct input_handle *handle;
	int i;

	for (i = KEY_ESC; i < BTN_MISC; i++)
		if (test_bit(i, dev->keybit))
			break;

	if (i == BTN_MISC)
 		return NULL;

	if (!(handle = kmalloc(sizeof(struct input_handle), GFP_KERNEL)))
		return NULL;
	memset(handle, 0, sizeof(struct input_handle));

	handle->dev = dev;
	handle->name = keybdev_name;
	handle->handler = handler;

	input_open_device(handle);

	return handle;
}

static void keybdev_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	kfree(handle);
}

static struct input_device_id keybdev_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT(EV_KEY) },
	},	
	{ }, 	/* Terminating entry */
};

MODULE_DEVICE_TABLE(input, keybdev_ids);
	
static struct input_handler keybdev_handler = {
	.event =	keybdev_event,
	.connect =	keybdev_connect,
	.disconnect =	keybdev_disconnect,
	.name =		"keybdev",
	.id_table =	keybdev_ids,
};

static int __init keybdev_init(void)
{
	input_register_handler(&keybdev_handler);
	kbd_ledfunc = keybdev_ledfunc;
	return 0;
}

static void __exit keybdev_exit(void)
{
	kbd_ledfunc = NULL;
	input_unregister_handler(&keybdev_handler);
}

module_init(keybdev_init);
module_exit(keybdev_exit);

