/*
 * budget-ci.c: driver for the SAA7146 based Budget DVB cards 
 *
 * Compiled from various sources by Michael Hunold <michael@mihu.de> 
 *
 *     msp430 IR support contributed by Jack Thomasson <jkt@Helius.COM>
 *     partially based on the Siemens DVB driver by Ralph+Marcus Metzler
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 * 
 *
 * the project's page is at http://www.linuxtv.org/dvb/
 */

#include "budget.h"

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/input.h>

struct budget_ci {
	struct budget budget;
	struct input_dev input_dev;
	struct tasklet_struct msp430_irq_tasklet;
};



#ifndef BORROWED_FROM_AV7110_H_BUT_REALLY_BELONGS_IN_SAA7146_DEFS_H

#define DEBINOSWAP 0x000e0000
#define GPIO_IRQHI 0x10
#define GPIO_INPUT 0x00

void gpio_set(struct saa7146_dev* saa, u8 pin, u8 data)
{
        u32 value = 0;

        /* sanity check */
        if(pin > 3)
                return;

        /* read old register contents */
        value = saa7146_read(saa, GPIO_CTRL );

        value &= ~(0xff << (8*pin));
        value |= (data << (8*pin));

        saa7146_write(saa, GPIO_CTRL, value);
}



static int wait_for_debi_done(struct saa7146_dev *saa)
{
	int start = jiffies;

	/* wait for registers to be programmed */
	while (1) {
		if (saa7146_read(saa, MC2) & 2)
			break;
		if (jiffies - start > HZ / 20) {
			printk ("DVB (%s): timed out while waiting"
				" for registers getting programmed\n",
				__FUNCTION__);
			return -ETIMEDOUT;
		}
	}

	/* wait for transfer to complete */
	start = jiffies;
	while (1) {
		if (!(saa7146_read(saa, PSR) & SPCI_DEBI_S))
			break;
		saa7146_read(saa, MC2);
		if (jiffies - start > HZ / 4) {
			printk ("DVB (%s): timed out while waiting"
				" for transfer completion\n",
				__FUNCTION__);
			return -ETIMEDOUT;
		}
	}

	return 0;
}


static u32 debiread (struct saa7146_dev *saa, u32 config, int addr, int count)
{
	u32 result = 0;

	if (count > 4 || count <= 0)
		return 0;

	if (wait_for_debi_done(saa) < 0)
		return 0;

	saa7146_write (saa, DEBI_COMMAND,
		       (count << 17) | 0x10000 | (addr & 0xffff));

	saa7146_write(saa, DEBI_CONFIG, config);
	saa7146_write(saa, MC2, (2 << 16) | 2);

	wait_for_debi_done(saa);

	result = saa7146_read(saa, DEBI_AD);
	result &= (0xffffffffUL >> ((4 - count) * 8));

	return result;
}



/* DEBI during interrupt */
static inline u32 irdebi(struct saa7146_dev *saa, u32 config, int addr, u32 val, int count)
{
	u32 res;
	res = debiread(saa, config, addr, count);
	return res;
}
#endif




/* from reading the following remotes:
   Zenith Universal 7 / TV Mode 807 / VCR Mode 837
   Hauppauge (from NOVA-CI-s box product)
   i've taken a "middle of the road" approach and note the differences
*/
static  u16 key_map[64] = {
	/* 0x0X */
	KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8,
	KEY_9,
	KEY_ENTER,
	0,
	KEY_POWER,              /* RADIO on Hauppauge */
	KEY_MUTE,
	0,
	KEY_A,                  /* TV on Hauppauge */
	/* 0x1X */
	KEY_VOLUMEUP, KEY_VOLUMEDOWN,
	0, 0,
	KEY_B,
	0, 0, 0, 0, 0, 0, 0,
	KEY_UP, KEY_DOWN,
	KEY_OPTION,             /* RESERVED on Hauppauge */
	0,
	/* 0x2X */
	KEY_CHANNELUP, KEY_CHANNELDOWN,
	KEY_PREVIOUS,           /* Prev. Ch on Zenith, SOURCE on Hauppauge */
	0, 0, 0,
	KEY_CYCLEWINDOWS,       /* MINIMIZE on Hauppauge */
	0,
	KEY_ENTER,              /* VCR mode on Zenith */
	KEY_PAUSE,
	0,
	KEY_RIGHT, KEY_LEFT,
	0,
	KEY_MENU,               /* FULL SCREEN on Hauppauge */
	0,
	/* 0x3X */
	0,
	KEY_PREVIOUS,           /* VCR mode on Zenith */
	KEY_REWIND,
	0,
	KEY_FASTFORWARD,
	KEY_PLAY, KEY_STOP,
	KEY_RECORD,
	KEY_TUNER,              /* TV/VCR on Zenith */
	0,
	KEY_C,
	0,
	KEY_EXIT,
	0,
	KEY_TUNER,              /* VCR mode on Zenith */
	0,
};


static void msp430_ir_debounce (unsigned long data)
{
	struct input_dev *dev = (struct input_dev *) data;

	if (dev->rep[0] == 0 || dev->rep[0] == ~0) {
		input_event(dev, EV_KEY, key_map[dev->repeat_key], !!0);
		return;
	}

	dev->rep[0] = 0;
	dev->timer.expires = jiffies + HZ * 350 / 1000;
	add_timer(&dev->timer);
	input_event(dev, EV_KEY, key_map[dev->repeat_key], 2);  /* REPEAT */
}



static void msp430_ir_interrupt (unsigned long data)
{
	struct budget_ci *budget_ci = (struct budget_ci*) data;
	struct saa7146_dev *saa = budget_ci->budget.dev;
	struct input_dev *dev = &budget_ci->input_dev;
	unsigned int code = irdebi(saa, DEBINOSWAP, 0x1234, 0, 2) >> 8;

	if (code & 0x40) {
	        code &= 0x3f;

        	if (timer_pending(&dev->timer)) {
                	if (code == dev->repeat_key) {
                        	++dev->rep[0];
	                        return;
        	        }
                	del_timer(&dev->timer);
		        input_event(dev, EV_KEY, key_map[dev->repeat_key], !!0);
		}

		if (!key_map[code]) {
        	        printk ("DVB (%s): no key for %02x!\n",
				__FUNCTION__, code);
		        return;
       		}

		/* initialize debounce and repeat */
		dev->repeat_key = code;
		/* Zenith remote _always_ sends 2 sequences */
		dev->rep[0] = ~0;
		/* 350 milliseconds */
		dev->timer.expires = jiffies + HZ * 350 / 1000;
		/* MAKE */
        	input_event(dev, EV_KEY, key_map[code], !0);
		add_timer(&dev->timer);
	}
}


static int msp430_ir_init (struct budget_ci *budget_ci)
{
	struct saa7146_dev *saa = budget_ci->budget.dev;
	int i;

	memset(&budget_ci->input_dev, 0, sizeof(struct input_dev));

	budget_ci->input_dev.name = saa->name;

	set_bit(EV_KEY, budget_ci->input_dev.evbit);

	for (i=0; i<sizeof(key_map)/sizeof(*key_map); i++)
		if (key_map[i])
			set_bit(key_map[i], budget_ci->input_dev.keybit);

	input_register_device(&budget_ci->input_dev);

	budget_ci->input_dev.timer.function = msp430_ir_debounce;

	saa7146_write(saa, IER, saa7146_read(saa, IER) | MASK_06);

	gpio_set(saa, 3, GPIO_IRQHI);

	return 0;
}


static void msp430_ir_deinit (struct budget_ci *budget_ci)
{
	struct saa7146_dev *saa = budget_ci->budget.dev;
	struct input_dev *dev = &budget_ci->input_dev;

	saa7146_write(saa, IER, saa7146_read(saa, IER) & ~MASK_06);
	gpio_set(saa, 3, GPIO_INPUT);
	gpio_set(saa, 2, GPIO_INPUT);

	if (del_timer(&dev->timer))
		input_event(dev, EV_KEY, key_map[dev->repeat_key], !!0);

	input_unregister_device(dev);
}


static void budget_ci_irq (struct saa7146_dev *dev, u32 *isr)
{
        struct budget_ci *budget_ci = (struct budget_ci*) dev->ext_priv;

        DEB_EE(("dev: %p, budget_ci: %p\n", dev, budget_ci));

        if (*isr & MASK_06)
                tasklet_schedule (&budget_ci->msp430_irq_tasklet);

        if (*isr & MASK_10)
		ttpci_budget_irq10_handler (dev, isr);
}



static int budget_ci_attach (struct saa7146_dev* dev,
		      struct saa7146_pci_extension_data *info)
{
	struct budget_ci *budget_ci;
	int err;

	if (!(budget_ci = kmalloc (sizeof(struct budget_ci), GFP_KERNEL)))
		return -ENOMEM;

	DEB_EE(("budget_ci: %p\n", budget_ci));

	if ((err = ttpci_budget_init (&budget_ci->budget, dev, info))) {
		kfree (budget_ci);
		return err;
	}

	dev->ext_priv = budget_ci;

	tasklet_init (&budget_ci->msp430_irq_tasklet, msp430_ir_interrupt,
		      (unsigned long) budget_ci);

	msp430_ir_init (budget_ci);

	return 0;
}



static int budget_ci_detach (struct saa7146_dev* dev)
{
	struct budget_ci *budget_ci = (struct budget_ci*) dev->ext_priv;
	int err;

	err = ttpci_budget_deinit (&budget_ci->budget);

	tasklet_kill (&budget_ci->msp430_irq_tasklet);

	msp430_ir_deinit (budget_ci);

	kfree (budget_ci);

	return err;
}



static struct saa7146_extension budget_extension; 

MAKE_BUDGET_INFO(ttbci,	"TT-Budget/WinTV-NOVA-CI PCI",	BUDGET_TT_HW_DISEQC);
MAKE_BUDGET_INFO(ttbt2,	"TT-Budget/WinTV-NOVA-T  PCI",	BUDGET_TT);

static struct pci_device_id pci_tbl[] = {
	MAKE_EXTENSION_PCI(ttbci, 0x13c2, 0x100c),
	MAKE_EXTENSION_PCI(ttbci, 0x13c2, 0x100f),
	MAKE_EXTENSION_PCI(ttbt2,  0x13c2, 0x1011),
	{
		.vendor    = 0,
	}
};

MODULE_DEVICE_TABLE(pci, pci_tbl);

static struct saa7146_extension budget_extension = {
	.name		= "budget_ci dvb\0",
	.flags	 	= 0,

	.module		= THIS_MODULE,
	.pci_tbl	= &pci_tbl[0],
	.attach		= budget_ci_attach,
	.detach		= budget_ci_detach,

	.irq_mask	= MASK_06 | MASK_10,
	.irq_func	= budget_ci_irq,
};	


static int __init budget_ci_init(void) 
{
	return saa7146_register_extension(&budget_extension);
}


static void __exit budget_ci_exit(void)
{
	DEB_EE((".\n"));
	saa7146_unregister_extension(&budget_extension); 
}

module_init(budget_ci_init);
module_exit(budget_ci_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Hunold, Jack Thomasson, others");
MODULE_DESCRIPTION("driver for the SAA7146 based so-called "
		   "budget PCI DVB cards w/ CI-module produced by "
		   "Siemens, Technotrend, Hauppauge");

