/*
 * Bt8xx based DVB adapter driver 
 *
 * Copyright (C) 2002,2003 Florian Schirmer <jolt@tuxbox.org>
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <asm/bitops.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/i2c.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"

#include "dvb-bt8xx.h"

#include "bt878.h"

static int debug;

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off debugging (default:off).");

#define dprintk( args... ) \
	do { \
		if (debug) printk(KERN_DEBUG args); \
	} while (0)

static void dvb_bt8xx_task(unsigned long data)
{
	struct dvb_bt8xx_card *card = (struct dvb_bt8xx_card *)data;

	//printk("%d ", card->bt->finished_block);

	while (card->bt->last_block != card->bt->finished_block) {
		(card->bt->TS_Size ? dvb_dmx_swfilter_204 : dvb_dmx_swfilter)
			(&card->demux,
			 &card->bt->buf_cpu[card->bt->last_block *
					    card->bt->block_bytes],
			 card->bt->block_bytes);
		card->bt->last_block = (card->bt->last_block + 1) %
					card->bt->block_count;
	}
}

static int dvb_bt8xx_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct dvb_bt8xx_card *card = dvbdmx->priv;

	dprintk("dvb_bt8xx: start_feed\n");
	
	if (!dvbdmx->dmx.frontend)
		return -EINVAL;

	if (card->active)
		return 0;
		
	card->active = 1;
	
//	bt878_start(card->bt, card->gpio_mode);

	return 0;
}

static int dvb_bt8xx_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct dvb_bt8xx_card *card = dvbdmx->priv;

	dprintk("dvb_bt8xx: stop_feed\n");
	
	if (!dvbdmx->dmx.frontend)
		return -EINVAL;
		
	if (!card->active)
		return 0;

//	bt878_stop(card->bt);

	card->active = 0;

	return 0;
}

static int is_pci_slot_eq(struct pci_dev* adev, struct pci_dev* bdev)
{
	if ((adev->subsystem_vendor == bdev->subsystem_vendor) &&
		(adev->subsystem_device == bdev->subsystem_device) &&
		(adev->bus->number == bdev->bus->number) &&
		(PCI_SLOT(adev->devfn) == PCI_SLOT(bdev->devfn)))
		return 1;
	return 0;
}

static struct bt878 __init *dvb_bt8xx_878_match(unsigned int bttv_nr, struct pci_dev* bttv_pci_dev)
{
	unsigned int card_nr;
	
	/* Hmm, n squared. Hope n is small */
	for (card_nr = 0; card_nr < bt878_num; card_nr++) {
		if (is_pci_slot_eq(bt878[card_nr].dev, bttv_pci_dev))
			return &bt878[card_nr];
	}
	return NULL;
}

static int __init dvb_bt8xx_load_card(struct dvb_bt8xx_card *card)
{
	int result;

	if ((result = dvb_register_adapter(&card->dvb_adapter, card->card_name,
					   THIS_MODULE)) < 0) {
		printk("dvb_bt8xx: dvb_register_adapter failed (errno = %d)\n", result);
		return result;
		
	}

	card->bt->adapter = card->i2c_adapter;

	memset(&card->demux, 0, sizeof(struct dvb_demux));

	card->demux.dmx.capabilities = DMX_TS_FILTERING | DMX_SECTION_FILTERING | DMX_MEMORY_BASED_FILTERING;

	card->demux.priv = card;
	card->demux.filternum = 256;
	card->demux.feednum = 256;
	card->demux.start_feed = dvb_bt8xx_start_feed;
	card->demux.stop_feed = dvb_bt8xx_stop_feed;
	card->demux.write_to_decoder = NULL;
	
	if ((result = dvb_dmx_init(&card->demux)) < 0) {
		printk("dvb_bt8xx: dvb_dmx_init failed (errno = %d)\n", result);

		dvb_unregister_adapter(card->dvb_adapter);
		return result;
	}

	card->dmxdev.filternum = 256;
	card->dmxdev.demux = &card->demux.dmx;
	card->dmxdev.capabilities = 0;
	
	if ((result = dvb_dmxdev_init(&card->dmxdev, card->dvb_adapter)) < 0) {
		printk("dvb_bt8xx: dvb_dmxdev_init failed (errno = %d)\n", result);

		dvb_dmx_release(&card->demux);
		dvb_unregister_adapter(card->dvb_adapter);
		return result;
	}

	card->fe_hw.source = DMX_FRONTEND_0;

	if ((result = card->demux.dmx.add_frontend(&card->demux.dmx, &card->fe_hw)) < 0) {
		printk("dvb_bt8xx: dvb_dmx_init failed (errno = %d)\n", result);

		dvb_dmxdev_release(&card->dmxdev);
		dvb_dmx_release(&card->demux);
		dvb_unregister_adapter(card->dvb_adapter);
		return result;
	}
	
	card->fe_mem.source = DMX_MEMORY_FE;

	if ((result = card->demux.dmx.add_frontend(&card->demux.dmx, &card->fe_mem)) < 0) {
		printk("dvb_bt8xx: dvb_dmx_init failed (errno = %d)\n", result);

		card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_hw);
		dvb_dmxdev_release(&card->dmxdev);
		dvb_dmx_release(&card->demux);
		dvb_unregister_adapter(card->dvb_adapter);
		return result;
	}

	if ((result = card->demux.dmx.connect_frontend(&card->demux.dmx, &card->fe_hw)) < 0) {
		printk("dvb_bt8xx: dvb_dmx_init failed (errno = %d)\n", result);

		card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_mem);
		card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_hw);
		dvb_dmxdev_release(&card->dmxdev);
		dvb_dmx_release(&card->demux);
		dvb_unregister_adapter(card->dvb_adapter);
		return result;
	}

	dvb_net_init(card->dvb_adapter, &card->dvbnet, &card->demux.dmx);

	tasklet_init(&card->bt->tasklet, dvb_bt8xx_task, (unsigned long) card);
	
	bt878_start(card->bt, card->gpio_mode, card->op_sync_orin, card->irq_err_ignore);

	return 0;
}

static int dvb_bt8xx_probe(struct device *dev)
{
	struct bttv_sub_device *sub = to_bttv_sub_dev(dev);
	struct dvb_bt8xx_card *card;
	struct pci_dev* bttv_pci_dev;
	int ret;

	if (!(card = kmalloc(sizeof(struct dvb_bt8xx_card), GFP_KERNEL)))
		return -ENOMEM;

	memset(card, 0, sizeof(*card));
	card->bttv_nr = sub->core->nr;
	strncpy(card->card_name, sub->core->name, sizeof(sub->core->name));
	card->i2c_adapter = &sub->core->i2c_adap;

	switch(sub->core->type)
	{
	case BTTV_PINNACLESAT:
		card->gpio_mode = 0x0400C060;
		card->op_sync_orin = 0;
		card->irq_err_ignore = 0;
		/* 26, 15, 14, 6, 5 
		 * A_PWRDN  DA_DPM DA_SBR DA_IOM_DA 
		 * DA_APP(parallel) */
		break;

	case BTTV_NEBULA_DIGITV:
	case BTTV_AVDVBT_761:
		card->gpio_mode = (1 << 26) | (1 << 14) | (1 << 5);
		card->op_sync_orin = 0;
		card->irq_err_ignore = 0;
		/* A_PWRDN DA_SBR DA_APP (high speed serial) */
		break;

	case BTTV_AVDVBT_771: //case 0x07711461:
		card->gpio_mode = 0x0400402B;
		card->op_sync_orin = BT878_RISC_SYNC_MASK;
		card->irq_err_ignore = 0;
		/* A_PWRDN DA_SBR  DA_APP[0] PKTP=10 RISC_ENABLE FIFO_ENABLE*/
		break;

	case BTTV_TWINHAN_DST:
		card->gpio_mode = 0x2204f2c;
		card->op_sync_orin = BT878_RISC_SYNC_MASK;
		card->irq_err_ignore = BT878_APABORT | BT878_ARIPERR |
				       BT878_APPERR | BT878_AFBUS;
		/* 25,21,14,11,10,9,8,3,2 then
		 * 0x33 = 5,4,1,0
		 * A_SEL=SML, DA_MLB, DA_SBR,
		 * DA_SDR=f, fifo trigger = 32 DWORDS
		 * IOM = 0 == audio A/D
		 * DPM = 0 == digital audio mode
		 * == async data parallel port
		 * then 0x33 (13 is set by start_capture)
		 * DA_APP = async data parallel port,
		 * ACAP_EN = 1,
		 * RISC+FIFO ENABLE */
		break;

	default:
		printk(KERN_WARNING "dvb_bt8xx: Unknown bttv card type: %d.\n",
				sub->core->type);
		kfree(card);
		return -ENODEV;
	}

	dprintk("dvb_bt8xx: identified card%d as %s\n", card->bttv_nr, card->card_name);
			
	if (!(bttv_pci_dev = bttv_get_pcidev(card->bttv_nr))) {
		printk("dvb_bt8xx: no pci device for card %d\n", card->bttv_nr);
		kfree(card);
		return -EFAULT;
	}

	if (!(card->bt = dvb_bt8xx_878_match(card->bttv_nr, bttv_pci_dev))) {
		printk("dvb_bt8xx: unable to determine DMA core of card %d\n", card->bttv_nr);
	
		kfree(card);
		return -EFAULT;
		
	}

	init_MUTEX(&card->bt->gpio_lock);
	card->bt->bttv_nr = sub->core->nr;

	if ( (ret = dvb_bt8xx_load_card(card)) ) {
		kfree(card);
		return ret;
	}

	dev_set_drvdata(dev, card);
	return 0;
}

static int dvb_bt8xx_remove(struct device *dev)
{
	struct dvb_bt8xx_card *card = dev_get_drvdata(dev);

	dprintk("dvb_bt8xx: unloading card%d\n", card->bttv_nr);

	bt878_stop(card->bt);
	tasklet_kill(&card->bt->tasklet);
	dvb_net_release(&card->dvbnet);
	card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_mem);
	card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_hw);
	dvb_dmxdev_release(&card->dmxdev);
	dvb_dmx_release(&card->demux);
	dvb_unregister_adapter(card->dvb_adapter);

	list_del(&card->list);
	kfree(card);

	return 0;
}

static void dvb_bt8xx_i2c_info(struct bttv_sub_device *sub,
			       struct i2c_client *client, int attach)
{
	struct dvb_bt8xx_card *card = dev_get_drvdata(&sub->dev);

	if (attach) {
		printk("xxx attach\n");
		if (client->driver->command)
			client->driver->command(client, FE_REGISTER,
						card->dvb_adapter);
	} else {
		printk("xxx detach\n");
		if (client->driver->command)
			client->driver->command(client, FE_UNREGISTER,
						card->dvb_adapter);
	}
}

static struct bttv_sub_driver driver = {
	.drv = {
		.name		= "dvb-bt8xx",
		.probe		= dvb_bt8xx_probe,
		.remove		= dvb_bt8xx_remove,
		/* FIXME:
		 * .shutdown	= dvb_bt8xx_shutdown,
		 * .suspend	= dvb_bt8xx_suspend,
		 * .resume	= dvb_bt8xx_resume,
		 */
	},
	.i2c_info = dvb_bt8xx_i2c_info,
};

static int __init dvb_bt8xx_init(void)
{
	return bttv_sub_register(&driver, "dvb");
}

static void __exit dvb_bt8xx_exit(void)
{
	bttv_sub_unregister(&driver);
}

module_init(dvb_bt8xx_init);
module_exit(dvb_bt8xx_exit);

MODULE_DESCRIPTION("Bt8xx based DVB adapter driver");
MODULE_AUTHOR("Florian Schirmer <jolt@tuxbox.org>");
MODULE_LICENSE("GPL");

