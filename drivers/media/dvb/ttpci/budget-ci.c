/*
 * budget-ci.c: driver for the SAA7146 based Budget DVB cards 
 *
 * Compiled from various sources by Michael Hunold <michael@mihu.de> 
 *
 *     msp430 IR support contributed by Jack Thomasson <jkt@Helius.COM>
 *     partially based on the Siemens DVB driver by Ralph+Marcus Metzler
 *
 * CI interface support (c) 2004 Andrew de Quincey <adq_dvb@lidskialf.net>
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
#include <linux/spinlock.h>

#include "dvb_functions.h"
#include "dvb_ca_en50221.h"

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
#include "input_fake.h"
#endif

#define DEBIADDR_IR		0x1234
#define DEBIADDR_CICONTROL	0x0000
#define DEBIADDR_CIVERSION	0x4000
#define DEBIADDR_IO		0x1000
#define DEBIADDR_ATTR		0x3000

#define CICONTROL_RESET		0x01
#define CICONTROL_ENABLETS	0x02
#define CICONTROL_CAMDETECT	0x08

#define DEBICICTL		0x00420000
#define DEBICICAM		0x02420000

#define SLOTSTATUS_NONE		1
#define SLOTSTATUS_PRESENT	2
#define SLOTSTATUS_RESET	4
#define SLOTSTATUS_READY	8
#define SLOTSTATUS_OCCUPIED	(SLOTSTATUS_PRESENT|SLOTSTATUS_RESET|SLOTSTATUS_READY)

struct budget_ci {
	struct budget budget;
	struct input_dev input_dev;
	struct tasklet_struct msp430_irq_tasklet;
	struct tasklet_struct ciintf_irq_tasklet;
	spinlock_t debilock;
	int slot_status;
	struct dvb_ca_en50221 ca;
	char ir_dev_name[50];
};

static u32 budget_debiread (struct budget_ci* budget_ci, u32 config, int addr, int count)
{
	struct saa7146_dev *saa = budget_ci->budget.dev;
	u32 result = 0;

	if (count > 4 || count <= 0)
		return 0;

	spin_lock(&budget_ci->debilock);

	if (saa7146_wait_for_debi_done(saa) < 0) {
		spin_unlock(&budget_ci->debilock);
		return 0;
	}

	saa7146_write (saa, DEBI_COMMAND,
		       (count << 17) | 0x10000 | (addr & 0xffff));
	saa7146_write(saa, DEBI_CONFIG, config);
	saa7146_write(saa, DEBI_PAGE, 0);
	saa7146_write(saa, MC2, (2 << 16) | 2);

	saa7146_wait_for_debi_done(saa);

	result = saa7146_read(saa, 0x88);
	result &= (0xffffffffUL >> ((4 - count) * 8));

	spin_unlock(&budget_ci->debilock);
	return result;
}

static u8 budget_debiwrite (struct budget_ci* budget_ci, u32 config, int addr, int count, u32 value)
{
	struct saa7146_dev *saa = budget_ci->budget.dev;

	if (count > 4 || count <= 0)
		return 0;

	spin_lock(&budget_ci->debilock);

	if (saa7146_wait_for_debi_done(saa) < 0) {
		spin_unlock(&budget_ci->debilock);
		return 0;
	}

	saa7146_write (saa, DEBI_COMMAND,
		       (count << 17) | 0x00000 | (addr & 0xffff));
	saa7146_write(saa, DEBI_CONFIG, config);
	saa7146_write(saa, DEBI_PAGE, 0);
	saa7146_write(saa, DEBI_AD, value);
	saa7146_write(saa, MC2, (2 << 16) | 2);

	saa7146_wait_for_debi_done(saa);

	spin_unlock(&budget_ci->debilock);
	return 0;
}


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
	KEY_RED,
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
	KEY_BREAK,
	/* 0x2X */
	KEY_CHANNELUP, KEY_CHANNELDOWN,
	KEY_PREVIOUS,           /* Prev. Ch on Zenith, SOURCE on Hauppauge */
	0, KEY_RESTART, KEY_OK,
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
	KEY_SLOW,
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
	KEY_POWER2,
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
	struct input_dev *dev = &budget_ci->input_dev;
	unsigned int code = budget_debiread(budget_ci, DEBINOSWAP, DEBIADDR_IR, 2) >> 8;

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

	sprintf (budget_ci->ir_dev_name, "Budget-CI dvb ir receiver %s", saa->name);
	budget_ci->input_dev.name = budget_ci->ir_dev_name;

	set_bit(EV_KEY, budget_ci->input_dev.evbit);

	for (i=0; i<sizeof(key_map)/sizeof(*key_map); i++)
		if (key_map[i])
			set_bit(key_map[i], budget_ci->input_dev.keybit);

	input_register_device(&budget_ci->input_dev);

	budget_ci->input_dev.timer.function = msp430_ir_debounce;

	saa7146_write(saa, IER, saa7146_read(saa, IER) | MASK_06);

	saa7146_setgpio(saa, 3, SAA7146_GPIO_IRQHI); 

	return 0;
}


static void msp430_ir_deinit (struct budget_ci *budget_ci)
{
	struct saa7146_dev *saa = budget_ci->budget.dev;
	struct input_dev *dev = &budget_ci->input_dev;

	saa7146_write(saa, IER, saa7146_read(saa, IER) & ~MASK_06);
	saa7146_setgpio(saa, 3, SAA7146_GPIO_INPUT);

	if (del_timer(&dev->timer))
		input_event(dev, EV_KEY, key_map[dev->repeat_key], !!0);

	input_unregister_device(dev);
}

static int ciintf_read_attribute_mem(struct dvb_ca_en50221* ca, int slot, int address) {
	struct budget_ci* budget_ci = (struct budget_ci*) ca->data;

	if (slot != 0) return -EINVAL;

	return budget_debiread(budget_ci, DEBICICAM, DEBIADDR_ATTR | (address & 0xfff), 1);
}

static int ciintf_write_attribute_mem(struct dvb_ca_en50221* ca, int slot, int address, u8 value) {
	struct budget_ci* budget_ci = (struct budget_ci*) ca->data;

	if (slot != 0) return -EINVAL;

	return budget_debiwrite(budget_ci, DEBICICAM, DEBIADDR_ATTR | (address & 0xfff), 1, value);
}

static int ciintf_read_cam_control(struct dvb_ca_en50221* ca, int slot, u8 address) {
	struct budget_ci* budget_ci = (struct budget_ci*) ca->data;

	if (slot != 0) return -EINVAL;

	return budget_debiread(budget_ci, DEBICICAM, DEBIADDR_IO | (address & 3), 1);
}

static int ciintf_write_cam_control(struct dvb_ca_en50221* ca, int slot, u8 address, u8 value) {
	struct budget_ci* budget_ci = (struct budget_ci*) ca->data;

	if (slot != 0) return -EINVAL;

	return budget_debiwrite(budget_ci, DEBICICAM, DEBIADDR_IO | (address & 3), 1, value);
}

static int ciintf_slot_reset(struct dvb_ca_en50221* ca, int slot) {
	struct budget_ci* budget_ci = (struct budget_ci*) ca->data;
	struct saa7146_dev *saa = budget_ci->budget.dev;

	if (slot != 0) return -EINVAL;

	// trigger on RISING edge during reset so we know when READY is re-asserted
	saa7146_setgpio(saa, 0, SAA7146_GPIO_IRQHI);
	budget_ci->slot_status = SLOTSTATUS_RESET;
	budget_debiwrite(budget_ci, DEBICICTL, DEBIADDR_CICONTROL, 1, 0);
	dvb_delay(1);
	budget_debiwrite(budget_ci, DEBICICTL, DEBIADDR_CICONTROL, 1, CICONTROL_RESET);

	saa7146_setgpio(saa, 1, SAA7146_GPIO_OUTHI);
   	ttpci_budget_set_video_port(saa, BUDGET_VIDEO_PORTB);
	return 0;
}

static int ciintf_slot_shutdown(struct dvb_ca_en50221* ca, int slot) {
   	struct budget_ci* budget_ci = (struct budget_ci*) ca->data;
	struct saa7146_dev *saa = budget_ci->budget.dev;

	if (slot != 0) return -EINVAL;

	saa7146_setgpio(saa, 1, SAA7146_GPIO_OUTHI);
	ttpci_budget_set_video_port(saa, BUDGET_VIDEO_PORTB);
	return 0;
}

static int ciintf_slot_ts_enable(struct dvb_ca_en50221* ca, int slot) {
	struct budget_ci* budget_ci = (struct budget_ci*) ca->data;
	struct saa7146_dev *saa = budget_ci->budget.dev;
	int tmp;

	if (slot != 0) return -EINVAL;


	saa7146_setgpio(saa, 1, SAA7146_GPIO_OUTLO);

	tmp = budget_debiread(budget_ci, DEBICICTL, DEBIADDR_CICONTROL, 1);
	budget_debiwrite(budget_ci, DEBICICTL, DEBIADDR_CICONTROL, 1, tmp | CICONTROL_ENABLETS);

   	ttpci_budget_set_video_port(saa, BUDGET_VIDEO_PORTA);
	return 0;
}


static void ciintf_interrupt (unsigned long data)
{
	struct budget_ci *budget_ci = (struct budget_ci*) data;
	struct saa7146_dev *saa = budget_ci->budget.dev;
	unsigned int flags;

	// ensure we don't get spurious IRQs during initialisation
	if (!budget_ci->budget.ci_present) return;

	flags = budget_debiread(budget_ci, DEBICICTL, DEBIADDR_CICONTROL, 1);

	// always set the GPIO mode back to "normal", in case the card is
	// yanked at an inopportune moment
	saa7146_setgpio(saa, 0, SAA7146_GPIO_IRQLO);

	if (flags & CICONTROL_CAMDETECT) {

		if (budget_ci->slot_status & SLOTSTATUS_NONE) {
			// CAM insertion IRQ
			budget_ci->slot_status = SLOTSTATUS_PRESENT;
			dvb_ca_en50221_camchange_irq(&budget_ci->ca, 0, DVB_CA_EN50221_CAMCHANGE_INSERTED);

		} else if (budget_ci->slot_status & SLOTSTATUS_RESET) {
			// CAM ready (reset completed)
			budget_ci->slot_status = SLOTSTATUS_READY;
			dvb_ca_en50221_camready_irq(&budget_ci->ca, 0);

		} else if (budget_ci->slot_status & SLOTSTATUS_READY) {
			// FR/DA IRQ
			dvb_ca_en50221_frda_irq(&budget_ci->ca, 0);
		}
	} else {
		if (budget_ci->slot_status & SLOTSTATUS_OCCUPIED) {
			budget_ci->slot_status = SLOTSTATUS_NONE;
			dvb_ca_en50221_camchange_irq(&budget_ci->ca, 0, DVB_CA_EN50221_CAMCHANGE_REMOVED);
		}
	}
}

static int ciintf_init(struct budget_ci* budget_ci)
{
	struct saa7146_dev *saa = budget_ci->budget.dev;
	int flags;
	int result;

	memset(&budget_ci->ca, 0, sizeof(struct dvb_ca_en50221));

	// enable DEBI pins
	saa7146_write(saa, MC1, saa7146_read(saa, MC1) | (0x800 << 16) | 0x800);

	// test if it is there
	if ((budget_debiread(budget_ci, DEBICICTL, DEBIADDR_CIVERSION, 1) & 0xa0) != 0xa0) {
		result = -ENODEV;
		goto error;
	}

	// determine whether a CAM is present or not
	flags = budget_debiread(budget_ci, DEBICICTL, DEBIADDR_CICONTROL, 1);
	budget_ci->slot_status = SLOTSTATUS_NONE;
	if (flags & CICONTROL_CAMDETECT) budget_ci->slot_status = SLOTSTATUS_PRESENT;


	// register CI interface
	budget_ci->ca.read_attribute_mem = ciintf_read_attribute_mem;
	budget_ci->ca.write_attribute_mem = ciintf_write_attribute_mem;
	budget_ci->ca.read_cam_control = ciintf_read_cam_control;
	budget_ci->ca.write_cam_control = ciintf_write_cam_control;
	budget_ci->ca.slot_reset = ciintf_slot_reset;
	budget_ci->ca.slot_shutdown = ciintf_slot_shutdown;
	budget_ci->ca.slot_ts_enable = ciintf_slot_ts_enable;
	budget_ci->ca.data = budget_ci;
	if ((result = dvb_ca_en50221_init(budget_ci->budget.dvb_adapter,
					  &budget_ci->ca,
					  DVB_CA_EN50221_FLAG_IRQ_CAMCHANGE |
					  DVB_CA_EN50221_FLAG_IRQ_FR |
					  DVB_CA_EN50221_FLAG_IRQ_DA,
				  1)) != 0) {
		printk("budget_ci: CI interface detected, but initialisation failed.\n");
		goto error;
	}

	// Setup CI slot IRQ
	tasklet_init (&budget_ci->ciintf_irq_tasklet, ciintf_interrupt, (unsigned long) budget_ci);
	saa7146_setgpio(saa, 0, SAA7146_GPIO_IRQLO);
	saa7146_write(saa, IER, saa7146_read(saa, IER) | MASK_03);
	budget_debiwrite(budget_ci, DEBICICTL, DEBIADDR_CICONTROL, 1, CICONTROL_RESET);

	// success!
	printk("budget_ci: CI interface initialised\n");
	budget_ci->budget.ci_present = 1;

	// forge a fake CI IRQ so the CAM state is setup correctly
	flags = DVB_CA_EN50221_CAMCHANGE_REMOVED;
	if (budget_ci->slot_status != SLOTSTATUS_NONE) flags = DVB_CA_EN50221_CAMCHANGE_INSERTED;
	dvb_ca_en50221_camchange_irq(&budget_ci->ca, 0, flags);

	return 0;

error:
	saa7146_write(saa, MC1, saa7146_read(saa, MC1) | (0x800 << 16));
	return result;
}

static void ciintf_deinit(struct budget_ci* budget_ci)
{
	struct saa7146_dev *saa = budget_ci->budget.dev;

	// disable CI interrupts
	saa7146_write(saa, IER, saa7146_read(saa, IER) & ~MASK_03);
	saa7146_setgpio(saa, 0, SAA7146_GPIO_INPUT);
	tasklet_kill(&budget_ci->ciintf_irq_tasklet);
	budget_debiwrite(budget_ci, DEBICICTL, DEBIADDR_CICONTROL, 1, 0);
	dvb_delay(1);
	budget_debiwrite(budget_ci, DEBICICTL, DEBIADDR_CICONTROL, 1, CICONTROL_RESET);

	// disable TS data stream to CI interface
	saa7146_setgpio(saa, 1, SAA7146_GPIO_INPUT);

	// release the CA device
	dvb_ca_en50221_release(&budget_ci->ca);

	// disable DEBI pins
	saa7146_write(saa, MC1, saa7146_read(saa, MC1) | (0x800 << 16));
}

static void budget_ci_irq (struct saa7146_dev *dev, u32 *isr)
{
        struct budget_ci *budget_ci = (struct budget_ci*) dev->ext_priv;

        DEB_EE(("dev: %p, budget_ci: %p\n", dev, budget_ci));

        if (*isr & MASK_06)
                tasklet_schedule (&budget_ci->msp430_irq_tasklet);

        if (*isr & MASK_10)
		ttpci_budget_irq10_handler (dev, isr);

	if ((*isr & MASK_03) && (budget_ci->budget.ci_present))
		tasklet_schedule (&budget_ci->ciintf_irq_tasklet);
}



static int budget_ci_attach (struct saa7146_dev* dev,
		      struct saa7146_pci_extension_data *info)
{
	struct budget_ci *budget_ci;
	int err;

	if (!(budget_ci = kmalloc (sizeof(struct budget_ci), GFP_KERNEL)))
		return -ENOMEM;

	DEB_EE(("budget_ci: %p\n", budget_ci));

	spin_lock_init(&budget_ci->debilock);
	budget_ci->budget.ci_present = 0;

	if ((err = ttpci_budget_init (&budget_ci->budget, dev, info))) {
		kfree (budget_ci);
		return err;
	}

	dev->ext_priv = budget_ci;

	tasklet_init (&budget_ci->msp430_irq_tasklet, msp430_ir_interrupt,
		      (unsigned long) budget_ci);

	msp430_ir_init (budget_ci);

	// UNCOMMENT TO TEST CI INTERFACE
//	ciintf_init(budget_ci);

	return 0;
}



static int budget_ci_detach (struct saa7146_dev* dev)
{
	struct budget_ci *budget_ci = (struct budget_ci*) dev->ext_priv;
	struct saa7146_dev *saa = budget_ci->budget.dev;
	int err;

	if (budget_ci->budget.ci_present) ciintf_deinit(budget_ci);

	err = ttpci_budget_deinit (&budget_ci->budget);

	tasklet_kill (&budget_ci->msp430_irq_tasklet);

	msp430_ir_deinit (budget_ci);

	// disable frontend and CI interface
	saa7146_setgpio(saa, 2, SAA7146_GPIO_INPUT);

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

	.irq_mask	= MASK_03 | MASK_06 | MASK_10,
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
MODULE_AUTHOR("Michael Hunold, Jack Thomasson, Andrew de Quincey, others");
MODULE_DESCRIPTION("driver for the SAA7146 based so-called "
		   "budget PCI DVB cards w/ CI-module produced by "
		   "Siemens, Technotrend, Hauppauge");

