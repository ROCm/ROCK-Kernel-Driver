/*
 * budget.c: driver for the SAA7146 based Budget DVB cards 
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

#include "budget.h"
#include "dvb_functions.h"

static void Set22K (struct budget *budget, int state)
{
	struct saa7146_dev *dev=budget->dev;
	DEB_EE(("budget: %p\n",budget));
	saa7146_setgpio(dev, 3, (state ? SAA7146_GPIO_OUTHI : SAA7146_GPIO_OUTLO));
}


/* Diseqc functions only for TT Budget card */
/* taken from the Skyvision DVB driver by
   Ralph Metzler <rjkm@metzlerbros.de> */

static void DiseqcSendBit (struct budget *budget, int data)
{
	struct saa7146_dev *dev=budget->dev;
	DEB_EE(("budget: %p\n",budget));

	saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTHI);
	udelay(data ? 500 : 1000);
	saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTLO);
	udelay(data ? 1000 : 500);
}


static void DiseqcSendByte (struct budget *budget, int data)
{
	int i, par=1, d;

	DEB_EE(("budget: %p\n",budget));

	for (i=7; i>=0; i--) {
		d = (data>>i)&1;
		par ^= d;
		DiseqcSendBit(budget, d);
	}

	DiseqcSendBit(budget, par);
}


static int SendDiSEqCMsg (struct budget *budget, int len, u8 *msg, unsigned long burst)
{
	struct saa7146_dev *dev=budget->dev;
	int i;

	DEB_EE(("budget: %p\n",budget));

	saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTLO);
	mdelay(16);

	for (i=0; i<len; i++)
		DiseqcSendByte(budget, msg[i]);

	mdelay(16);

	if (burst!=-1) {
		if (burst)
			DiseqcSendByte(budget, 0xff);
		else {
			saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTHI);
			udelay(12500);
			saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTLO);
		}
		dvb_delay(20);
	}

	return 0;
}


int budget_diseqc_ioctl (struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
       struct budget *budget = fe->before_after_data;

       DEB_EE(("budget: %p\n",budget));

       switch (cmd) {
       case FE_SET_TONE:
               switch ((fe_sec_tone_mode_t) arg) {
               case SEC_TONE_ON:
                       Set22K (budget, 1);
                       break;
               case SEC_TONE_OFF:
                       Set22K (budget, 0);
                       break;
               default:
                       return -EINVAL;
               };
               break;

       case FE_DISEQC_SEND_MASTER_CMD:
       {
               struct dvb_diseqc_master_cmd *cmd = arg;

               SendDiSEqCMsg (budget, cmd->msg_len, cmd->msg, 0);
               break;
       }

       case FE_DISEQC_SEND_BURST:
               SendDiSEqCMsg (budget, 0, NULL, (unsigned long)arg);
               break;

       default:
               return -EOPNOTSUPP;
       };

       return 0;
}


static int budget_attach (struct saa7146_dev* dev, struct saa7146_pci_extension_data *info)
{
	struct budget *budget = NULL;
	int err;

	budget = kmalloc(sizeof(struct budget), GFP_KERNEL);
	if( NULL == budget ) {
		return -ENOMEM;
	}

	DEB_EE(("dev:%p, info:%p, budget:%p\n",dev,info,budget));

	if ((err = ttpci_budget_init (budget, dev, info))) {
		printk("==> failed\n");
		kfree (budget);
		return err;
	}

	dvb_add_frontend_ioctls (budget->dvb_adapter,
				 budget_diseqc_ioctl, NULL, budget);

	dev->ext_priv = budget;

	return 0;
}


static int budget_detach (struct saa7146_dev* dev)
{
	struct budget *budget = (struct budget*) dev->ext_priv;
	int err;

	dvb_remove_frontend_ioctls (budget->dvb_adapter,
				    budget_diseqc_ioctl, NULL);

	err = ttpci_budget_deinit (budget);

	kfree (budget);
	dev->ext_priv = NULL;

	return err;
}



static struct saa7146_extension budget_extension;

MAKE_BUDGET_INFO(ttbs,	"TT-Budget/WinTV-NOVA-S  PCI",	BUDGET_TT);
MAKE_BUDGET_INFO(ttbc,	"TT-Budget/WinTV-NOVA-C  PCI",	BUDGET_TT);
MAKE_BUDGET_INFO(ttbt,	"TT-Budget/WinTV-NOVA-T  PCI",	BUDGET_TT);
MAKE_BUDGET_INFO(satel,	"SATELCO Multimedia PCI",	BUDGET_TT_HW_DISEQC);
/* Uncomment for Budget Patch */
/*MAKE_BUDGET_INFO(fs_1_3,"Siemens/Technotrend/Hauppauge PCI rev1.3+Budget_Patch", BUDGET_PATCH);*/

static struct pci_device_id pci_tbl[] = {
	/* Uncomment for Budget Patch */
	/*MAKE_EXTENSION_PCI(fs_1_3,0x13c2, 0x0000),*/
	MAKE_EXTENSION_PCI(ttbs,  0x13c2, 0x1003),
	MAKE_EXTENSION_PCI(ttbc,  0x13c2, 0x1004),
	MAKE_EXTENSION_PCI(ttbt,  0x13c2, 0x1005),
	MAKE_EXTENSION_PCI(satel, 0x13c2, 0x1013),
	{
		.vendor    = 0,
	}
};

MODULE_DEVICE_TABLE(pci, pci_tbl);

static struct saa7146_extension budget_extension = {
	.name		= "budget dvb\0",
	.flags	 	= 0,
	
	.module		= THIS_MODULE,
	.pci_tbl	= pci_tbl,
	.attach		= budget_attach,
	.detach		= budget_detach,

	.irq_mask	= MASK_10,
	.irq_func	= ttpci_budget_irq10_handler,
};	


static int __init budget_init(void) 
{
	return saa7146_register_extension(&budget_extension);
}


static void __exit budget_exit(void)
{
	DEB_EE((".\n"));
	saa7146_unregister_extension(&budget_extension); 
}

module_init(budget_init);
module_exit(budget_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ralph Metzler, Marcus Metzler, Michael Hunold, others");
MODULE_DESCRIPTION("driver for the SAA7146 based so-called "
		   "budget PCI DVB cards by Siemens, Technotrend, Hauppauge");

