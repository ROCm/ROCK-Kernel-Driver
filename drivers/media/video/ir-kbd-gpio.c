/*
 * Copyright (c) 2003 Gerd Knorr
 * Copyright (c) 2003 Pavel Machek
 *
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
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/pci.h>

#include <media/ir-common.h>

#include "bttv.h"

/* ---------------------------------------------------------------------- */

static IR_KEYTAB_TYPE ir_codes_avermedia[IR_KEYTAB_SIZE] = {
	[ 17 ] = KEY_KP0, 
	[ 20 ] = KEY_KP1, 
	[ 12 ] = KEY_KP2, 
	[ 28 ] = KEY_KP3, 
	[ 18 ] = KEY_KP4, 
	[ 10 ] = KEY_KP5, 
	[ 26 ] = KEY_KP6, 
	[ 22 ] = KEY_KP7, 
	[ 14 ] = KEY_KP8, 
	[ 30 ] = KEY_KP9, 

	[ 24 ] = KEY_EJECTCD,     // Unmarked on my controller
	[  0 ] = KEY_POWER, 
	[  9 ] = BTN_LEFT,        // DISPLAY/L
	[ 25 ] = BTN_RIGHT,       // LOOP/R
	[  5 ] = KEY_MUTE, 
	[ 19 ] = KEY_RECORD, 
	[ 11 ] = KEY_PAUSE, 
	[ 27 ] = KEY_STOP, 
	[ 15 ] = KEY_VOLUMEDOWN, 
	[ 31 ] = KEY_VOLUMEUP, 

	[ 16 ] = KEY_TUNER,       // TV/FM
	[  8 ] = KEY_CD, 
	[  4 ] = KEY_VIDEO, 
	[  2 ] = KEY_AUDIO, 
	[  6 ] = KEY_ZOOM,        // full screen
	[  1 ] = KEY_INFO,        // preview 
	[ 21 ] = KEY_SEARCH,      // autoscan
	[ 13 ] = KEY_STOP,        // freeze 
	[ 29 ] = KEY_RECORD,      // capture 
	[  3 ] = KEY_PLAY,        // unmarked
	[ 23 ] = KEY_RED,         // unmarked
	[  7 ] = KEY_GREEN,       // unmarked

#if 0
	[ 16 ] = KEY_YELLOW,      // unmarked
	[  8 ] = KEY_CHANNELDOWN, 
	[ 24 ] = KEY_CHANNELUP, 
	[  0 ] = KEY_BLUE,        // unmarked
#endif
};

static IR_KEYTAB_TYPE winfast_codes[IR_KEYTAB_SIZE] = {
	[  5 ] = KEY_KP1,
	[  6 ] = KEY_KP2,
	[  7 ] = KEY_KP3,
	[  9 ] = KEY_KP4,
	[ 10 ] = KEY_KP5,
	[ 11 ] = KEY_KP6,
	[ 13 ] = KEY_KP7,
	[ 14 ] = KEY_KP8,
	[ 15 ] = KEY_KP9,
	[ 18 ] = KEY_KP0,

	[  0 ] = KEY_POWER,
//      [ 27 ] = MTS button
	[  2 ] = KEY_TUNER,     // TV/FM
	[ 30 ] = KEY_VIDEO,
//      [ 22 ] = display button
	[  4 ] = KEY_VOLUMEUP,
	[  8 ] = KEY_VOLUMEDOWN,
	[ 12 ] = KEY_CHANNELUP,
	[ 16 ] = KEY_CHANNELDOWN,
	[  3 ] = KEY_ZOOM,      // fullscreen
	[ 31 ] = KEY_SUBTITLE,  // closed caption/teletext
	[ 32 ] = KEY_SLEEP,
//      [ 41 ] = boss key
	[ 20 ] = KEY_MUTE,
	[ 43 ] = KEY_RED,
	[ 44 ] = KEY_GREEN,
	[ 45 ] = KEY_YELLOW,
	[ 46 ] = KEY_BLUE,
	[ 24 ] = KEY_KPPLUS,    //fine tune +
	[ 25 ] = KEY_KPMINUS,   //fine tune -
//      [ 42 ] = picture in picture
        [ 33 ] = KEY_KPDOT,
	[ 19 ] = KEY_KPENTER,
//      [ 17 ] = recall
	[ 34 ] = KEY_BACK,
	[ 35 ] = KEY_PLAYPAUSE,
	[ 36 ] = KEY_NEXT,
//      [ 37 ] = time shifting
	[ 38 ] = KEY_STOP,
	[ 39 ] = KEY_RECORD
//      [ 40 ] = snapshot
};

static IR_KEYTAB_TYPE ir_codes_pixelview[IR_KEYTAB_SIZE] = {
	[  2 ] = KEY_KP0,
	[  1 ] = KEY_KP1,
	[ 11 ] = KEY_KP2,
	[ 27 ] = KEY_KP3,
	[  5 ] = KEY_KP4,
	[  9 ] = KEY_KP5,
	[ 21 ] = KEY_KP6,
	[  6 ] = KEY_KP7,
	[ 10 ] = KEY_KP8,
	[ 18 ] = KEY_KP9,
	
	[  3 ] = KEY_TUNER,       // TV/FM
	[  7 ] = KEY_SEARCH,      // scan
	[ 28 ] = KEY_ZOOM,        // full screen
	[ 30 ] = KEY_POWER,
	[ 23 ] = KEY_VOLUMEDOWN,
	[ 31 ] = KEY_VOLUMEUP,
	[ 20 ] = KEY_CHANNELDOWN,
	[ 22 ] = KEY_CHANNELUP,
	[ 24 ] = KEY_MUTE,
	
	[  0 ] = KEY_LIST,        // source
	[ 19 ] = KEY_INFO,        // loop
	[ 16 ] = KEY_LAST,        // +100
	[ 13 ] = KEY_CLEAR,       // reset
	[ 12 ] = BTN_RIGHT,       // fun++
	[  4 ] = BTN_LEFT,        // fun--
	[ 14 ] = KEY_GOTO,        // function
	[ 15 ] = KEY_STOP,         // freeze
};

/* ---------------------------------------------------------------------- */

struct IR {
	struct bttv_sub_device  *sub;
	struct input_dev        input;
	struct ir_input_state   ir;
	char                    name[32];
	char                    phys[32];
	u32                     mask_keycode;
	u32                     mask_keydown;
	u32                     mask_keyup;

	int                     polling;
	u32                     last_gpio;
	struct work_struct      work;
	struct timer_list       timer;
};

static int debug;
module_param(debug, int, 0644);    /* debug level (0,1,2) */

#define DEVNAME "ir-kbd-gpio"
#define dprintk(fmt, arg...)	if (debug) \
	printk(KERN_DEBUG DEVNAME ": " fmt , ## arg)

static void ir_irq(struct bttv_sub_device *sub);
static int ir_probe(struct device *dev);
static int ir_remove(struct device *dev);

static struct bttv_sub_driver driver = {
	.drv = {
		.name	= DEVNAME,
		.probe	= ir_probe,
		.remove	= ir_remove,
	},
	.gpio_irq       = ir_irq,
};

/* ---------------------------------------------------------------------- */

static void ir_handle_key(struct IR *ir)
{
	u32 gpio,data;

	/* read gpio value */
	gpio = bttv_gpio_read(ir->sub->core);
	if (ir->polling) {
		if (ir->last_gpio == gpio)
			return;
		ir->last_gpio = gpio;
	}
	
	/* extract data */
	data = ir_extract_bits(gpio, ir->mask_keycode);
	dprintk(DEVNAME ": irq gpio=0x%x code=%d | %s%s%s\n",
		gpio, data,
		ir->polling               ? "poll"  : "irq",
		(gpio & ir->mask_keydown) ? " down" : "",
		(gpio & ir->mask_keyup)   ? " up"   : "");

	if (ir->mask_keydown) {
		/* bit set on keydown */
		if (gpio & ir->mask_keydown) {
			ir_input_keydown(&ir->input,&ir->ir,data,data);
		} else {
			ir_input_nokey(&ir->input,&ir->ir);
		}

	} else if (ir->mask_keyup) {
		/* bit cleared on keydown */
		if (0 == (gpio & ir->mask_keyup)) {
			ir_input_keydown(&ir->input,&ir->ir,data,data);
		} else {
			ir_input_nokey(&ir->input,&ir->ir);
		}

	} else {
		/* can't disturgissh keydown/up :-/ */
		ir_input_keydown(&ir->input,&ir->ir,data,data);
		ir_input_nokey(&ir->input,&ir->ir);
	}
}

static void ir_irq(struct bttv_sub_device *sub)
{
	struct IR *ir = dev_get_drvdata(&sub->dev);

	if (!ir->polling)
		ir_handle_key(ir);
}

static void ir_timer(unsigned long data)
{
	struct IR *ir = (struct IR*)data;

	schedule_work(&ir->work);
}

static void ir_work(void *data)
{
	struct IR *ir = data;
	unsigned long timeout;

	ir_handle_key(ir);
	timeout = jiffies + (ir->polling * HZ / 1000);
	mod_timer(&ir->timer, timeout);
}

/* ---------------------------------------------------------------------- */

static int ir_probe(struct device *dev)
{
	struct bttv_sub_device *sub = to_bttv_sub_dev(dev);
	struct IR *ir;
	IR_KEYTAB_TYPE *ir_codes = NULL;
	int ir_type = IR_TYPE_OTHER;

	ir = kmalloc(sizeof(*ir),GFP_KERNEL);
	if (NULL == ir)
		return -ENOMEM;
	memset(ir,0,sizeof(*ir));

	/* detect & configure */
	switch (sub->core->type) {
	case BTTV_AVERMEDIA:
	case BTTV_AVPHONE98:
	case BTTV_AVERMEDIA98:
		ir_codes         = ir_codes_avermedia;
		ir->mask_keycode = 0xf80000;
		ir->mask_keydown = 0x010000;
		break;

	case BTTV_PXELVWPLTVPAK:
		ir_codes         = ir_codes_pixelview;
		ir->mask_keycode = 0x003e00;
		ir->mask_keyup   = 0x010000;
		ir->polling      = 50; // ms
                break;
	case BTTV_PV_BT878P_9B:
	case BTTV_PV_BT878P_PLUS:
		ir_codes         = ir_codes_pixelview;
		ir->mask_keycode = 0x001f00;
		ir->mask_keyup   = 0x008000;
		ir->polling      = 50; // ms
                break;

	case BTTV_WINFAST2000:
		ir_codes         = winfast_codes;
		ir->mask_keycode = 0x8f8;
		break;
	case BTTV_MAGICTVIEW061:
	case BTTV_MAGICTVIEW063:
		ir_codes         = winfast_codes;
		ir->mask_keycode = 0x0008e000;
		ir->mask_keydown = 0x00200000;
		break;
	}
	if (NULL == ir_codes) {
		kfree(ir);
		return -ENODEV;
	}

	/* init hardware-specific stuff */
	bttv_gpio_inout(sub->core, ir->mask_keycode | ir->mask_keydown, 0);
	ir->sub = sub;
	
	/* init input device */
	snprintf(ir->name, sizeof(ir->name), "bttv IR (card=%d)",
		 sub->core->type);
	snprintf(ir->phys, sizeof(ir->phys), "pci-%s/ir0",
		 pci_name(sub->core->pci));

	ir_input_init(&ir->input, &ir->ir, ir_type, ir_codes);
	ir->input.name = ir->name;
	ir->input.phys = ir->phys;
	ir->input.id.bustype = BUS_PCI;
	ir->input.id.version = 1;
	if (sub->core->pci->subsystem_vendor) {
		ir->input.id.vendor  = sub->core->pci->subsystem_vendor;
		ir->input.id.product = sub->core->pci->subsystem_device;
	} else {
		ir->input.id.vendor  = sub->core->pci->vendor;
		ir->input.id.product = sub->core->pci->device;
	}

	if (ir->polling) {
		INIT_WORK(&ir->work, ir_work, ir);
		init_timer(&ir->timer);
		ir->timer.function = ir_timer;
		ir->timer.data     = (unsigned long)ir;
		schedule_work(&ir->work);
	}

	/* all done */
	dev_set_drvdata(dev,ir);
	input_register_device(&ir->input);
	printk(DEVNAME ": %s detected at %s\n",ir->input.name,ir->input.phys);

	return 0;
}

static int ir_remove(struct device *dev)
{
	struct IR *ir = dev_get_drvdata(dev);

	if (ir->polling) {
		del_timer(&ir->timer);
		flush_scheduled_work();
	}

	input_unregister_device(&ir->input);
	kfree(ir);
	return 0;
}

/* ---------------------------------------------------------------------- */

MODULE_AUTHOR("Gerd Knorr, Pavel Machek");
MODULE_DESCRIPTION("input driver for bt8x8 gpio IR remote controls");
MODULE_LICENSE("GPL");

static int ir_init(void)
{
	return bttv_sub_register(&driver, "remote");
}

static void ir_fini(void)
{
	bttv_sub_unregister(&driver);
}

module_init(ir_init);
module_exit(ir_fini);


/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
