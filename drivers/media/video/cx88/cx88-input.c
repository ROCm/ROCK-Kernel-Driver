/*
 * $Id: cx88-input.c,v 1.4 2005/01/07 13:58:49 kraxel Exp $
 *
 * Device driver for GPIO attached remote control interfaces
 * on Conexant 2388x based TV/DVB cards.
 *
 * Copyright (c) 2003 Pavel Machek
 * Copyright (c) 2004 Gerd Knorr
 * Copyright (c) 2004 Chris Pascoe
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

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include <media/ir-common.h>

#include "cx88.h"

/* ---------------------------------------------------------------------- */

/* DigitalNow DNTV Live DVB-T Remote */
static IR_KEYTAB_TYPE ir_codes_dntv_live_dvb_t[IR_KEYTAB_SIZE] = {
	[ 0x00 ] = KEY_ESC,         // 'go up a level?'
	[ 0x01 ] = KEY_KP1,         // '1'
	[ 0x02 ] = KEY_KP2,         // '2'
	[ 0x03 ] = KEY_KP3,         // '3'
	[ 0x04 ] = KEY_KP4,         // '4'
	[ 0x05 ] = KEY_KP5,         // '5'
	[ 0x06 ] = KEY_KP6,         // '6'
	[ 0x07 ] = KEY_KP7,         // '7'
	[ 0x08 ] = KEY_KP8,         // '8'
	[ 0x09 ] = KEY_KP9,         // '9'
	[ 0x0a ] = KEY_KP0,         // '0'
	[ 0x0b ] = KEY_TUNER,       // 'tv/fm'
	[ 0x0c ] = KEY_SEARCH,      // 'scan'
	[ 0x0d ] = KEY_STOP,        // 'stop'
	[ 0x0e ] = KEY_PAUSE,       // 'pause'
	[ 0x0f ] = KEY_LIST,        // 'source'

	[ 0x10 ] = KEY_MUTE,        // 'mute'
	[ 0x11 ] = KEY_REWIND,      // 'backward <<'
	[ 0x12 ] = KEY_POWER,       // 'power'
	[ 0x13 ] = KEY_S,           // 'snap'
	[ 0x14 ] = KEY_AUDIO,       // 'stereo'
	[ 0x15 ] = KEY_CLEAR,       // 'reset'
	[ 0x16 ] = KEY_PLAY,        // 'play'
	[ 0x17 ] = KEY_ENTER,       // 'enter'
	[ 0x18 ] = KEY_ZOOM,        // 'full screen'
	[ 0x19 ] = KEY_FASTFORWARD, // 'forward >>'
	[ 0x1a ] = KEY_CHANNELUP,   // 'channel +'
	[ 0x1b ] = KEY_VOLUMEUP,    // 'volume +'
	[ 0x1c ] = KEY_INFO,        // 'preview'
	[ 0x1d ] = KEY_RECORD,      // 'record'
	[ 0x1e ] = KEY_CHANNELDOWN, // 'channel -'
	[ 0x1f ] = KEY_VOLUMEDOWN,  // 'volume -'
};

/* Happauge: the newer, gray remote */
static IR_KEYTAB_TYPE ir_codes_hauppauge_new[IR_KEYTAB_SIZE] = {
	[ 0x00 ] = KEY_KP0,             // 0
	[ 0x01 ] = KEY_KP1,             // 1
	[ 0x02 ] = KEY_KP2,             // 2
	[ 0x03 ] = KEY_KP3,             // 3
	[ 0x04 ] = KEY_KP4,             // 4
	[ 0x05 ] = KEY_KP5,             // 5
	[ 0x06 ] = KEY_KP6,             // 6
	[ 0x07 ] = KEY_KP7,             // 7
	[ 0x08 ] = KEY_KP8,             // 8
	[ 0x09 ] = KEY_KP9,             // 9
	[ 0x0b ] = KEY_RED,             // red button 
	[ 0x0c ] = KEY_OPTION,          // black key without text
	[ 0x0d ] = KEY_MENU,            // menu
	[ 0x0f ] = KEY_MUTE,            // mute
	[ 0x10 ] = KEY_VOLUMEUP,        // volume +
	[ 0x11 ] = KEY_VOLUMEDOWN,      // volume -
	[ 0x1e ] = KEY_NEXT,            // skip >|
	[ 0x1f ] = KEY_EXIT,            // back/exit
	[ 0x20 ] = KEY_CHANNELUP,       // channel / program +
	[ 0x21 ] = KEY_CHANNELDOWN,     // channel / program -
	[ 0x24 ] = KEY_PREVIOUS,        // replay |<
	[ 0x25 ] = KEY_ENTER,           // OK
	[ 0x29 ] = KEY_BLUE,            // blue key
	[ 0x2e ] = KEY_GREEN,           // green button
	[ 0x30 ] = KEY_PAUSE,           // pause
	[ 0x32 ] = KEY_REWIND,          // backward <<
	[ 0x34 ] = KEY_FASTFORWARD,     // forward >>
	[ 0x35 ] = KEY_PLAY,            // play
	[ 0x36 ] = KEY_STOP,            // stop
	[ 0x37 ] = KEY_RECORD,          // recording
	[ 0x38 ] = KEY_YELLOW,          // yellow key
	[ 0x3b ] = KEY_SELECT,          // top right button
	[ 0x3c ] = KEY_ZOOM,            // full
	[ 0x3d ] = KEY_POWER,           // system power (green button)
};

/* ---------------------------------------------------------------------- */

struct cx88_IR {
	struct cx88_core	*core;
	struct input_dev        input;
	struct ir_input_state   ir;
	char                    name[32];
	char                    phys[32];

	/* sample from gpio pin 16 */
	int                     sampling;
	u32                     samples[16];
	int                     scount;
	unsigned long           release;

	/* poll external decoder */
	int                     polling;
	struct work_struct      work;
	struct timer_list       timer;
	u32			gpio_addr;
	u32                     last_gpio;
	u32                     mask_keycode;
	u32                     mask_keydown;
	u32                     mask_keyup;
};

static int ir_debug = 0;
module_param(ir_debug, int, 0644);    /* debug level [IR] */
MODULE_PARM_DESC(ir_debug, "enable debug messages [IR]");

#define ir_dprintk(fmt, arg...)	if (ir_debug) \
	printk(KERN_DEBUG "%s IR: " fmt , ir->core->name, ## arg)

/* ---------------------------------------------------------------------- */

static void cx88_ir_handle_key(struct cx88_IR *ir)
{
	struct cx88_core *core = ir->core;
	u32 gpio, data;

	/* read gpio value */
	gpio = cx_read(ir->gpio_addr);
	if (ir->polling) {
		if (ir->last_gpio == gpio)
			return;
		ir->last_gpio = gpio;
	}

	/* extract data */
	data = ir_extract_bits(gpio, ir->mask_keycode);
	ir_dprintk("irq gpio=0x%x code=%d | %s%s%s\n",
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

static void ir_timer(unsigned long data)
{
	struct cx88_IR *ir = (struct cx88_IR*)data;

	schedule_work(&ir->work);
}

static void cx88_ir_work(void *data)
{
	struct cx88_IR *ir = data;
	unsigned long timeout;

	cx88_ir_handle_key(ir);
	timeout = jiffies + (ir->polling * HZ / 1000);
	mod_timer(&ir->timer, timeout);
}

/* ---------------------------------------------------------------------- */

int cx88_ir_init(struct cx88_core *core, struct pci_dev *pci)
{
	struct cx88_IR *ir;
	IR_KEYTAB_TYPE *ir_codes = NULL;
	int ir_type = IR_TYPE_OTHER;

	ir = kmalloc(sizeof(*ir),GFP_KERNEL);
	if (NULL == ir)
		return -ENOMEM;
	memset(ir,0,sizeof(*ir));

	/* detect & configure */
	switch (core->board) {
	case CX88_BOARD_DNTV_LIVE_DVB_T:
		ir_codes         = ir_codes_dntv_live_dvb_t;
		ir->gpio_addr    = MO_GP1_IO;
		ir->mask_keycode = 0x1f;
		ir->mask_keyup   = 0x60;
		ir->polling      = 50; // ms
		break;
	case CX88_BOARD_HAUPPAUGE:
	case CX88_BOARD_HAUPPAUGE_DVB_T1:
		ir_codes         = ir_codes_hauppauge_new;
		ir_type          = IR_TYPE_RC5;
		ir->sampling     = 1;
		break;
	}
	if (NULL == ir_codes) {
		kfree(ir);
		return -ENODEV;
	}

	/* init input device */
	snprintf(ir->name, sizeof(ir->name), "cx88 IR (%s)",
		 cx88_boards[core->board].name);
	snprintf(ir->phys, sizeof(ir->phys), "pci-%s/ir0",
		 pci_name(pci));

	ir_input_init(&ir->input, &ir->ir, ir_type, ir_codes);
	ir->input.name = ir->name;
	ir->input.phys = ir->phys;
	ir->input.id.bustype = BUS_PCI;
	ir->input.id.version = 1;
	if (pci->subsystem_vendor) {
		ir->input.id.vendor  = pci->subsystem_vendor;
		ir->input.id.product = pci->subsystem_device;
	} else {
		ir->input.id.vendor  = pci->vendor;
		ir->input.id.product = pci->device;
	}

	/* record handles to ourself */
	ir->core = core;
	core->ir = ir;

	if (ir->polling) {
		INIT_WORK(&ir->work, cx88_ir_work, ir);
		init_timer(&ir->timer);
		ir->timer.function = ir_timer;
		ir->timer.data     = (unsigned long)ir;
		schedule_work(&ir->work);
	}
	if (ir->sampling) {
		core->pci_irqmask |= (1<<18);   // IR_SMP_INT
		cx_write(MO_DDS_IO, 0xa80a80);  // 4 kHz sample rate
		cx_write(MO_DDSCFG_IO,   0x5);  // enable
	}

	/* all done */
	input_register_device(&ir->input);
	printk("%s: registered IR remote control\n", core->name);

	return 0;
}

int cx88_ir_fini(struct cx88_core *core)
{
	struct cx88_IR *ir = core->ir;

	/* skip detach on non attached boards */
	if (NULL == ir)
		return 0;

	if (ir->polling) {
		del_timer(&ir->timer);
		flush_scheduled_work();
	}

	input_unregister_device(&ir->input);
	kfree(ir);

	/* done */
	core->ir = NULL;
	return 0;
}

/* ---------------------------------------------------------------------- */

static int inline getbit(u32 *samples, int bit)
{
	return (samples[bit/32] & (1 << (31-(bit%32)))) ? 1 : 0;
}

static int dump_samples(u32 *samples, int count)
{
	int i, bit, start;

	printk(KERN_DEBUG "ir samples @ 4kHz: ");
	start = 0;
	for (i = 0; i < count * 32; i++) {
		bit = getbit(samples,i);
		if (bit)
			start = 1;
		if (0 == start)
			continue;
		printk("%s", bit ? "#" : "_");
	}
	printk("\n");
}

static int ir_decode_biphase(u32 *samples, int count, int low, int high)
{
	int i,last,bit,len,flips;
	u32 value;

	/* find start bit (1) */
	for (i = 0; i < 32; i++) {
		bit = getbit(samples,i);
		if (bit)
			break;
	}

	/* go decoding */
	len   = 0;
	flips = 0;
	value = 1;
	for (; i < count * 32; i++) {
		if (len > high)
			break;
		if (flips > 1)
			break;
		last = bit;
		bit  = getbit(samples,i);
		if (last == bit) {
			len++;
			continue;
		}
		if (len < low) {
			len++;
			flips++;
			continue;
		}
		value <<= 1;
		value |= bit;
		flips = 0;
		len   = 1;
	}
	return value;
}

void cx88_ir_irq(struct cx88_core *core)
{
	struct cx88_IR *ir = core->ir;
	u32 samples,rc5;
	int i;

	if (NULL == ir)
		return;
	if (!ir->sampling)
		return;

	samples = cx_read(MO_SAMPLE_IO);
	if (0 != samples  &&  0xffffffff != samples) {
		/* record sample data */
		if (ir->scount < ARRAY_SIZE(ir->samples))
			ir->samples[ir->scount++] = samples;
		return;
	}
	if (!ir->scount) {
		/* nothing to sample */
		if (ir->ir.keypressed && time_after(jiffies,ir->release))
			ir_input_nokey(&ir->input,&ir->ir);
		return;
	}

	/* have a complete sample */
	if (ir->scount < ARRAY_SIZE(ir->samples))
		ir->samples[ir->scount++] = samples;
	for (i = 0; i < ir->scount; i++)
		ir->samples[i] = ~ir->samples[i];
	if (ir_debug)
		dump_samples(ir->samples,ir->scount);

	/* decode it */
	switch (core->board) {
	case CX88_BOARD_HAUPPAUGE:
	case CX88_BOARD_HAUPPAUGE_DVB_T1:
		rc5 = ir_decode_biphase(ir->samples,ir->scount,5,7);
		ir_dprintk("biphase decoded: %x\n",rc5);
		if ((rc5 & 0xfffff000) != 0x3000)
			break;
		ir_input_keydown(&ir->input, &ir->ir, rc5 & 0x3f, rc5);
		ir->release = jiffies + msecs_to_jiffies(120);
		break;
	}

	ir->scount = 0;
	return;
}

/* ---------------------------------------------------------------------- */

MODULE_AUTHOR("Gerd Knorr, Pavel Machek, Chris Pascoe");
MODULE_DESCRIPTION("input driver for cx88 GPIO-based IR remote controls");
MODULE_LICENSE("GPL");

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
