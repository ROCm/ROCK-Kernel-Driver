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
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/i2c.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"

#include "dvb-bt8xx.h"

#include "dvb_functions.h"

#include "bt878.h"

/* ID THAT MUST GO INTO i2c ids */
#ifndef  I2C_DRIVERID_DVB_BT878A
# define I2C_DRIVERID_DVB_BT878A I2C_DRIVERID_EXP0+10
#endif


#define dprintk if (debug) printk

extern int bttv_get_cardinfo(unsigned int card, int *type, int *cardid);
extern struct pci_dev* bttv_get_pcidev(unsigned int card);

static LIST_HEAD(card_list);
static int debug = 0;

static void dvb_bt8xx_task(unsigned long data)
{
	struct dvb_bt8xx_card *card = (struct dvb_bt8xx_card *)data;

	//printk("%d ", finished_block);

	while (card->bt->last_block != card->bt->finished_block) {
		(card->bt->TS_Size ? dvb_dmx_swfilter_204 : dvb_dmx_swfilter)(&card->demux, &card->bt->buf_cpu[card->bt->last_block * card->bt->block_bytes], card->bt->block_bytes);
		card->bt->last_block = (card->bt->last_block + 1) % card->bt->block_count;
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

static int master_xfer (struct dvb_i2c_bus *i2c, const struct i2c_msg msgs[], int num)
{
	struct dvb_bt8xx_card *card = i2c->data;
	int retval;

	if (down_interruptible (&card->bt->gpio_lock))
		return -ERESTARTSYS;

	retval = i2c_transfer(card->i2c_adapter,
			      (struct i2c_msg*) msgs,
			      num);

	up(&card->bt->gpio_lock);

	return retval;
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

static int __init dvb_bt8xx_card_match(unsigned int bttv_nr, char *card_name, u32 gpio_mode, u32 op_sync_orin, u32 irq_err_ignore)
{
	struct dvb_bt8xx_card *card;
	struct pci_dev* bttv_pci_dev;

	dprintk("dvb_bt8xx: identified card%d as %s\n", bttv_nr, card_name);
			
	if (!(card = kmalloc(sizeof(struct dvb_bt8xx_card), GFP_KERNEL)))
		return -ENOMEM;

	memset(card, 0, sizeof(*card));
	card->bttv_nr = bttv_nr;
	strncpy(card->card_name, card_name, sizeof(card_name) - 1);
	
	if (!(bttv_pci_dev = bttv_get_pcidev(bttv_nr))) {
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
	card->bt->bttv_nr = bttv_nr;
	card->gpio_mode = gpio_mode;
	card->op_sync_orin = op_sync_orin;
	card->irq_err_ignore = irq_err_ignore;
	list_add_tail(&card->list, &card_list);

	return 0;
}

static struct dvb_bt8xx_card *dvb_bt8xx_find_by_i2c_adap(struct i2c_adapter *adap)
{
	struct dvb_bt8xx_card *card;
	struct list_head *item;
	
	printk("find by i2c adap: checking \"%s\"\n",adap->name);
	list_for_each(item, &card_list) {
		card = list_entry(item, struct dvb_bt8xx_card, list);
		if (card->i2c_adapter == adap)
			return card;
	}
	return NULL;
}

static struct dvb_bt8xx_card *dvb_bt8xx_find_by_pci(struct i2c_adapter *adap)
{
	struct dvb_bt8xx_card *card;
	struct list_head *item;
	struct device  *dev;
	struct pci_dev *pci;
	
	printk("find by pci: checking \"%s\"\n",adap->name);
	dev = adap->dev.parent;
	if (NULL == dev) {
		/* shoudn't happen with 2.6.0-test7 + newer */
		printk("attach: Huh? i2c adapter not in sysfs tree?\n");
		return 0;
	}
	pci = to_pci_dev(dev);
	list_for_each(item, &card_list) {
		card = list_entry(item, struct dvb_bt8xx_card, list);
		if (is_pci_slot_eq(pci, card->bt->dev)) {
			return card;
		}
	}
	return NULL;
}

static int dvb_bt8xx_attach(struct i2c_adapter *adap)
{
	struct dvb_bt8xx_card *card;
	
	printk("attach: checking \"%s\"\n",adap->name);

	/* looking for bt878 cards ... */
	if (adap->id != (I2C_ALGO_BIT | I2C_HW_B_BT848))
		return 0;
	card = dvb_bt8xx_find_by_pci(adap);
	if (!card)
		return 0;
	card->i2c_adapter = adap;
	printk("attach: \"%s\", to card %d\n",
	       adap->name, card->bttv_nr);
	try_module_get(adap->owner);

	return 0;
}

static void dvb_bt8xx_i2c_adap_free(struct i2c_adapter *adap)
{
	module_put(adap->owner);
}

static int dvb_bt8xx_detach(struct i2c_adapter *adap)
{
	struct dvb_bt8xx_card *card;

	card = dvb_bt8xx_find_by_i2c_adap(adap);
	if (!card)
		return 0;

	/* This should not happen. We have locked the module! */
	printk("detach: \"%s\", for card %d removed\n",
	       adap->name, card->bttv_nr);
	return 0;
}

static struct i2c_driver dvb_bt8xx_driver = {
	.owner           = THIS_MODULE,
	.name            = "dvb_bt8xx",
        .id              = I2C_DRIVERID_DVB_BT878A,
	.flags           = I2C_DF_NOTIFY,
        .attach_adapter  = dvb_bt8xx_attach,
        .detach_adapter  = dvb_bt8xx_detach,
};

static void __init dvb_bt8xx_get_adaps(void)
{
	i2c_add_driver(&dvb_bt8xx_driver);
}

static void __exit dvb_bt8xx_exit_adaps(void)
{
	i2c_del_driver(&dvb_bt8xx_driver);
}

static int __init dvb_bt8xx_load_card( struct dvb_bt8xx_card *card)
{
	int result;

	if (!card->i2c_adapter) {
		printk("dvb_bt8xx: unable to determine i2c adaptor of card %d, deleting\n", card->bttv_nr);

		return -EFAULT;
	
	}

	if ((result = dvb_register_adapter(&card->dvb_adapter, card->card_name, THIS_MODULE)) < 0) {
	
		printk("dvb_bt8xx: dvb_register_adapter failed (errno = %d)\n", result);
		
		dvb_bt8xx_i2c_adap_free(card->i2c_adapter);
		return result;
		
	}
	card->bt->adap_ptr = card->dvb_adapter;

	if (!(dvb_register_i2c_bus(master_xfer, card, card->dvb_adapter, 0))) {
		printk("dvb_bt8xx: dvb_register_i2c_bus of card%d failed\n", card->bttv_nr);

		dvb_unregister_adapter(card->dvb_adapter);
		dvb_bt8xx_i2c_adap_free(card->i2c_adapter);

		return -EFAULT;
	}

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

		dvb_unregister_i2c_bus(master_xfer, card->dvb_adapter, 0);
		dvb_unregister_adapter(card->dvb_adapter);
		dvb_bt8xx_i2c_adap_free(card->i2c_adapter);
		
		return result;
	}

	card->dmxdev.filternum = 256;
	card->dmxdev.demux = &card->demux.dmx;
	card->dmxdev.capabilities = 0;
	
	if ((result = dvb_dmxdev_init(&card->dmxdev, card->dvb_adapter)) < 0) {
		printk("dvb_bt8xx: dvb_dmxdev_init failed (errno = %d)\n", result);

		dvb_dmx_release(&card->demux);
		dvb_unregister_i2c_bus(master_xfer, card->dvb_adapter, 0);
		dvb_unregister_adapter(card->dvb_adapter);
		dvb_bt8xx_i2c_adap_free(card->i2c_adapter);
		
		return result;
	}

	card->fe_hw.source = DMX_FRONTEND_0;

	if ((result = card->demux.dmx.add_frontend(&card->demux.dmx, &card->fe_hw)) < 0) {
		printk("dvb_bt8xx: dvb_dmx_init failed (errno = %d)\n", result);

		dvb_dmxdev_release(&card->dmxdev);
		dvb_dmx_release(&card->demux);
		dvb_unregister_i2c_bus(master_xfer, card->dvb_adapter, 0);
		dvb_unregister_adapter(card->dvb_adapter);
		dvb_bt8xx_i2c_adap_free(card->i2c_adapter);
		
		return result;
	}
	
	card->fe_mem.source = DMX_MEMORY_FE;

	if ((result = card->demux.dmx.add_frontend(&card->demux.dmx, &card->fe_mem)) < 0) {
		printk("dvb_bt8xx: dvb_dmx_init failed (errno = %d)\n", result);

		card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_hw);
		dvb_dmxdev_release(&card->dmxdev);
		dvb_dmx_release(&card->demux);
		dvb_unregister_i2c_bus(master_xfer, card->dvb_adapter, 0);
		dvb_unregister_adapter(card->dvb_adapter);
		dvb_bt8xx_i2c_adap_free(card->i2c_adapter);
		
		return result;
	}

	if ((result = card->demux.dmx.connect_frontend(&card->demux.dmx, &card->fe_hw)) < 0) {
		printk("dvb_bt8xx: dvb_dmx_init failed (errno = %d)\n", result);

		card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_mem);
		card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_hw);
		dvb_dmxdev_release(&card->dmxdev);
		dvb_dmx_release(&card->demux);
		dvb_unregister_i2c_bus(master_xfer, card->dvb_adapter, 0);
		dvb_unregister_adapter(card->dvb_adapter);
		dvb_bt8xx_i2c_adap_free(card->i2c_adapter);
		
		return result;
	}

	dvb_net_init(card->dvb_adapter, &card->dvbnet, &card->demux.dmx);

	tasklet_init(&card->bt->tasklet, dvb_bt8xx_task, (unsigned long) card);
	
	bt878_start(card->bt, card->gpio_mode, card->op_sync_orin, card->irq_err_ignore);

	return 0;
}

static int __init dvb_bt8xx_load_all(void)
{
	struct dvb_bt8xx_card *card;
	struct list_head *entry, *entry_safe;

	list_for_each_safe(entry, entry_safe, &card_list) {
		card = list_entry(entry, struct dvb_bt8xx_card, list);
		if (dvb_bt8xx_load_card(card) < 0) {
			list_del(&card->list);
			kfree(card);
			continue;
		}
	}
	return 0;

}

#define BT878_NEBULA	0x68
#define BT878_TWINHAN_DST 0x71

static int __init dvb_bt8xx_init(void)
{
	unsigned int card_nr = 0;
	int card_id;
	int card_type;

	dprintk("dvb_bt8xx: enumerating available bttv cards...\n");
	
	while (bttv_get_cardinfo(card_nr, &card_type, &card_id) == 0) {
		switch(card_id) {
			case 0x001C11BD:
				dvb_bt8xx_card_match(card_nr, "Pinnacle PCTV DVB-S",
					       0x0400C060, 0, 0);
				/* 26, 15, 14, 6, 5 
				 * A_G2X  DA_DPM DA_SBR DA_IOM_DA 
				 * DA_APP(parallel) */
				break;
			case 0x01010071:
nebula:
				dvb_bt8xx_card_match(card_nr, "Nebula DigiTV DVB-T",
					     (1 << 26) | (1 << 14) | (1 << 5),
					     0, 0);
				/* A_PWRDN DA_SBR DA_APP (high speed serial) */
				break;
			case 0x07611461:
				dvb_bt8xx_card_match(card_nr, "Avermedia DVB-T",
					     (1 << 26) | (1 << 14) | (1 << 5),
					     0, 0);
				/* A_PWRDN DA_SBR DA_APP (high speed serial) */
				break;
			case 0x0:
				if (card_type == BT878_NEBULA ||
					card_type == BT878_TWINHAN_DST)
					goto dst;
				goto unknown_card;
			case 0x2611BD:
			case 0x11822:
dst:
				dvb_bt8xx_card_match(card_nr, "DST DVB-S", 0x2204f2c,
						BT878_RISC_SYNC_MASK,
						BT878_APABORT | BT878_ARIPERR | BT878_APPERR | BT878_AFBUS);
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
unknown_card:
				printk("%s: unknown card_id found %0X\n",
					__FUNCTION__, card_id);
				if (card_type == BT878_NEBULA) {
					printk("%s: bttv type set to nebula\n",
						__FUNCTION__);
					goto nebula;
				}
				if (card_type == BT878_TWINHAN_DST) {
					printk("%s: bttv type set to Twinhan DST\n",
						__FUNCTION__);
					goto dst;
				}
				printk("%s: unknown card_type found %0X, NOT LOADED\n",
					__FUNCTION__, card_type);
				printk("%s: unknown card_nr found %0X\n",
					__FUNCTION__, card_nr);
		}
		card_nr++;
	}
	dvb_bt8xx_get_adaps();
	dvb_bt8xx_load_all();

	return 0;

}

static void __exit dvb_bt8xx_exit(void)
{
	struct dvb_bt8xx_card *card;
	struct list_head *entry, *entry_safe;

	dvb_bt8xx_exit_adaps();
	list_for_each_safe(entry, entry_safe, &card_list) {
		card = list_entry(entry, struct dvb_bt8xx_card, list);
		
		dprintk("dvb_bt8xx: unloading card%d\n", card->bttv_nr);

		bt878_stop(card->bt);
		tasklet_kill(&card->bt->tasklet);
		dvb_net_release(&card->dvbnet);
		card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_mem);
		card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_hw);
		dvb_dmxdev_release(&card->dmxdev);
		dvb_dmx_release(&card->demux);
		dvb_unregister_i2c_bus(master_xfer, card->dvb_adapter, 0);
		dvb_bt8xx_i2c_adap_free(card->i2c_adapter);
		dvb_unregister_adapter(card->dvb_adapter);
		
		list_del(&card->list);
		kfree(card);
	}

}

module_init(dvb_bt8xx_init);
module_exit(dvb_bt8xx_exit);
MODULE_DESCRIPTION("Bt8xx based DVB adapter driver");
MODULE_AUTHOR("Florian Schirmer <jolt@tuxbox.org>");
MODULE_LICENSE("GPL");
MODULE_PARM(debug, "i");
