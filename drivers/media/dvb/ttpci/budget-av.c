/*
 * budget-av.c: driver for the SAA7146 based Budget DVB cards
 *              with analog video in 
 *
 * Compiled from various sources by Michael Hunold <michael@mihu.de> 
 *
 * Copyright (C) 2002 Ralph Metzler <rjkm@metzlerbros.de>
 *
 * Copyright (C) 1999-2002 Ralph  Metzler 
 *                       & Marcus Metzler for convergence integrated media GmbH
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

#include <media/saa7146_vv.h>

#include "budget.h"
#include "dvb_functions.h"

struct budget_av {
	struct budget budget;
	struct video_device vd;
	int cur_input;
	int has_saa7113;
};

/****************************************************************************
 * INITIALIZATION
 ****************************************************************************/


static u8 i2c_readreg (struct dvb_i2c_bus *i2c, u8 id, u8 reg)
{
	u8 mm1[] = {0x00};
	u8 mm2[] = {0x00};
	struct i2c_msg msgs[2];

	msgs[0].flags = 0;
	msgs[1].flags = I2C_M_RD;
	msgs[0].addr = msgs[1].addr=id/2;
	mm1[0] = reg;
	msgs[0].len = 1; msgs[1].len = 1;
	msgs[0].buf = mm1; msgs[1].buf = mm2;

	i2c->xfer(i2c, msgs, 2);

	return mm2[0];
}

static int i2c_readregs(struct dvb_i2c_bus *i2c, u8 id, u8 reg, u8 *buf, u8 len)
{
        u8 mm1[] = { reg };
        struct i2c_msg msgs[2] = {
		{ .addr = id/2, .flags = 0, .buf = mm1, .len = 1 },
		{ .addr = id/2, .flags = I2C_M_RD, .buf = buf, .len = len }
	};

        if (i2c->xfer(i2c, msgs, 2) != 2)
		return -EIO;
	return 0;
}


static int i2c_writereg (struct dvb_i2c_bus *i2c, u8 id, u8 reg, u8 val)
{
        u8 msg[2]={ reg, val }; 
        struct i2c_msg msgs;

        msgs.flags=0;
        msgs.addr=id/2;
        msgs.len=2;
        msgs.buf=msg;
        return i2c->xfer (i2c, &msgs, 1);
}


static const u8 saa7113_tab[] = {
	0x01, 0x08,
	0x02, 0xc0,
	0x03, 0x33,
	0x04, 0x00,
	0x05, 0x00,
	0x06, 0xeb,
	0x07, 0xe0,
	0x08, 0x28,
	0x09, 0x00,
	0x0a, 0x80,
	0x0b, 0x47,
	0x0c, 0x40,
	0x0d, 0x00,
	0x0e, 0x01,
        0x0f, 0x44,

	0x10, 0x08,
	0x11, 0x0c,
	0x12, 0x7b,
	0x13, 0x00,
        0x15, 0x00,  0x16, 0x00,  0x17, 0x00,

        0x57, 0xff, 
        0x40, 0x82,  0x58, 0x00,  0x59, 0x54,  0x5a, 0x07,
        0x5b, 0x83,  0x5e, 0x00,
        0xff
};


static int saa7113_init (struct budget_av *budget_av)
{
	struct budget *budget = &budget_av->budget;
	const u8 *data = saa7113_tab;

        if (i2c_writereg (budget->i2c_bus, 0x4a, 0x01, 0x08) != 1) {
                DEB_D(("saa7113: not found on KNC card\n"));
                return -ENODEV;
        }

        INFO(("saa7113: detected and initializing\n"));

	while (*data != 0xff) {
                i2c_writereg(budget->i2c_bus, 0x4a, *data, *(data+1));
                data += 2;
        }

	DEB_D(("saa7113: status=%02x\n",
	      i2c_readreg(budget->i2c_bus, 0x4a, 0x1f)));

	return 0;
}


static int saa7113_setinput (struct budget_av *budget_av, int input)
{
	struct budget *budget = &budget_av->budget;

	if ( 1 != budget_av->has_saa7113 )
		return -ENODEV;

	if (input == 1) {
		i2c_writereg(budget->i2c_bus, 0x4a, 0x02, 0xc7);
		i2c_writereg(budget->i2c_bus, 0x4a, 0x09, 0x80);
	} else if (input == 0) {
		i2c_writereg(budget->i2c_bus, 0x4a, 0x02, 0xc0);
		i2c_writereg(budget->i2c_bus, 0x4a, 0x09, 0x00);
	} else
		return -EINVAL;

	budget_av->cur_input = input;
	return 0;
}


static int budget_av_detach (struct saa7146_dev *dev)
{
	struct budget_av *budget_av = (struct budget_av*) dev->ext_priv;
	int err;

	DEB_EE(("dev: %p\n",dev));

	if ( 1 == budget_av->has_saa7113 ) {
	saa7146_setgpio(dev, 0, SAA7146_GPIO_OUTLO);

	dvb_delay(200);

	saa7146_unregister_device (&budget_av->vd, dev);
	}

	err = ttpci_budget_deinit (&budget_av->budget);

	kfree (budget_av);

	return err;
}

static struct saa7146_ext_vv vv_data;

static int budget_av_attach (struct saa7146_dev* dev,
		      struct saa7146_pci_extension_data *info)
{
	struct budget_av *budget_av;
	struct budget_info *bi = info->ext_priv;
	u8 *mac;
	int err;

	DEB_EE(("dev: %p\n",dev));

	if (bi->type != BUDGET_KNC1) {
		return -ENODEV;
	}

	if (!(budget_av = kmalloc(sizeof(struct budget_av), GFP_KERNEL)))
		return -ENOMEM;

	memset(budget_av, 0, sizeof(struct budget_av));

	if ((err = ttpci_budget_init(&budget_av->budget, dev, info))) {
		kfree(budget_av);
		return err;
	}

	dev->ext_priv = budget_av;

	/* knc1 initialization */
	saa7146_write(dev, DD1_STREAM_B, 0x04000000);
	saa7146_write(dev, DD1_INIT, 0x07000600);
	saa7146_write(dev, MC2, MASK_09 | MASK_25 | MASK_10 | MASK_26);

	//test_knc_ci(av7110);

	saa7146_setgpio(dev, 0, SAA7146_GPIO_OUTHI);
	dvb_delay(500);

	if ( 0 == saa7113_init(budget_av) ) {
		budget_av->has_saa7113 = 1;

	if ( 0 != saa7146_vv_init(dev,&vv_data)) {
		/* fixme: proper cleanup here */
		ERR(("cannot init vv subsystem.\n"));
		return err;
	}

	if ((err = saa7146_register_device(&budget_av->vd, dev, "knc1",
					   VFL_TYPE_GRABBER)))
	{
		/* fixme: proper cleanup here */
		ERR(("cannot register capture v4l2 device.\n"));
		return err;
	}

	/* beware: this modifies dev->vv ... */
	saa7146_set_hps_source_and_sync(dev, SAA7146_HPS_SOURCE_PORT_A,
					SAA7146_HPS_SYNC_PORT_A);

	saa7113_setinput (budget_av, 0);
	} else {
		budget_av->has_saa7113 = 0;

	saa7146_setgpio(dev, 0, SAA7146_GPIO_OUTLO);
	}

	/* fixme: find some sane values here... */
	saa7146_write(dev, PCI_BT_V1, 0x1c00101f);

	mac = budget_av->budget.dvb_adapter->proposed_mac;
	if (i2c_readregs(budget_av->budget.i2c_bus, 0xa0, 0x30, mac, 6)) {
		printk("KNC1-%d: Could not read MAC from KNC1 card\n",
				budget_av->budget.dvb_adapter->num);
		memset(mac, 0, 6);
	}
	else
		printk("KNC1-%d: MAC addr = %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n",
				budget_av->budget.dvb_adapter->num,
				mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return 0;
}



#define KNC1_INPUTS 2
static struct v4l2_input knc1_inputs[KNC1_INPUTS] = {
	{ 0,	"Composite", V4L2_INPUT_TYPE_TUNER,  1, 0, V4L2_STD_PAL_BG|V4L2_STD_NTSC_M, 0 }, 
	{ 1,	"S-Video",   V4L2_INPUT_TYPE_CAMERA, 2, 0, V4L2_STD_PAL_BG|V4L2_STD_NTSC_M, 0 },
};


static struct saa7146_extension_ioctls ioctls[] = {
	{ VIDIOC_ENUMINPUT, 	SAA7146_EXCLUSIVE },
	{ VIDIOC_G_INPUT,	SAA7146_EXCLUSIVE },
	{ VIDIOC_S_INPUT,	SAA7146_EXCLUSIVE },
	{ 0,			0 }
};


static int av_ioctl(struct saa7146_fh *fh, unsigned int cmd, void *arg) 
{
	struct saa7146_dev *dev = fh->dev;
	struct budget_av *budget_av = (struct budget_av*) dev->ext_priv;
/*
	struct saa7146_vv *vv = dev->vv_data; 
*/	
	switch(cmd) {
	case VIDIOC_ENUMINPUT:
	{
		struct v4l2_input *i = arg;
		
		DEB_EE(("VIDIOC_ENUMINPUT %d.\n",i->index));
		if( i->index < 0 || i->index >= KNC1_INPUTS) {
			return -EINVAL;
		}
		memcpy(i, &knc1_inputs[i->index], sizeof(struct v4l2_input));
		return 0;
	}
	case VIDIOC_G_INPUT:
	{
		int *input = (int *)arg;

		*input = budget_av->cur_input;

		DEB_EE(("VIDIOC_G_INPUT %d.\n",*input));
		return 0;		
	}	
	case VIDIOC_S_INPUT:
	{
		int input = *(int *)arg;
		DEB_EE(("VIDIOC_S_INPUT %d.\n", input));
		return saa7113_setinput (budget_av, input);
	}
	default:
/*
		DEB2(printk("does not handle this ioctl.\n"));
*/
		return -ENOIOCTLCMD;
	}
	return 0;
}

static struct saa7146_standard standard[] = {
	{
		.name	= "PAL", 	.id	= V4L2_STD_PAL,
		.v_offset	= 0x17,	.v_field 	= 288,
		.h_offset	= 0x14,	.h_pixels 	= 680,  	      
		.v_max_out	= 576,	.h_max_out	= 768
	}, {
		.name	= "NTSC", 	.id	= V4L2_STD_NTSC,
		.v_offset	= 0x16,	.v_field 	= 240,
		.h_offset	= 0x06,	.h_pixels 	= 708,
		.v_max_out	= 480,	.h_max_out	= 640,
	}
};

static struct saa7146_ext_vv vv_data = {
	.inputs		= 2,
	.capabilities	= 0, // perhaps later: V4L2_CAP_VBI_CAPTURE, but that need tweaking with the saa7113
	.flags		= 0,
	.stds		= &standard[0],
	.num_stds	= sizeof(standard)/sizeof(struct saa7146_standard),
	.ioctls		= &ioctls[0],
	.ioctl		= av_ioctl,
};



static struct saa7146_extension budget_extension;


MAKE_BUDGET_INFO(knc1, "KNC1 DVB-S", BUDGET_KNC1);

static struct pci_device_id pci_tbl [] = {
	MAKE_EXTENSION_PCI(knc1, 0x1131, 0x4f56),
	{
		.vendor    = 0,
	}
};

MODULE_DEVICE_TABLE(pci, pci_tbl);

static struct saa7146_extension budget_extension = {
	.name		= "budget dvb /w video in\0",
	.pci_tbl	= pci_tbl,

	.module		= THIS_MODULE,
	.attach		= budget_av_attach,
	.detach		= budget_av_detach,

	.irq_mask	= MASK_10,
	.irq_func	= ttpci_budget_irq10_handler,
};	


static int __init budget_av_init(void) 
{
	DEB_EE((".\n"));

	if (saa7146_register_extension(&budget_extension))
		return -ENODEV;
	
	return 0;
}


static void __exit budget_av_exit(void)
{
	DEB_EE((".\n"));
	saa7146_unregister_extension(&budget_extension); 
}

module_init(budget_av_init);
module_exit(budget_av_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ralph Metzler, Marcus Metzler, Michael Hunold, others");
MODULE_DESCRIPTION("driver for the SAA7146 based so-called "
		   "budget PCI DVB w/ analog input (e.g. the KNC cards)");

