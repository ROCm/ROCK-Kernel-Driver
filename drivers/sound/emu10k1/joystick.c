/*
 **********************************************************************
 *     joystick.c - Creative EMU10K1 Joystick port driver
 *     Copyright 2000 Rui Sousa.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     April  1, 2000       Rui Sousa       initial version 
 *     April 28, 2000       Rui Sousa       fixed a kernel oops,
 *					    make use of kcompat24 for
 *					    2.2 kernels compatibility.
 *     May    1, 2000       Rui Sousa       improved kernel compatibility
 *                                          layer.
 *
 **********************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 **********************************************************************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/list.h>

#define DRIVER_VERSION "0.3.1"

#ifndef PCI_VENDOR_ID_CREATIVE
#define PCI_VENDOR_ID_CREATIVE 0x1102
#endif

#ifndef PCI_DEVICE_ID_CREATIVE_EMU10K1_JOYSTICK
#define PCI_DEVICE_ID_CREATIVE_EMU10K1_JOYSTICK 0x7002
#endif

/* PCI function 1 registers, address = <val> + PCIBASE1 */

#define JOYSTICK1               0x00            /* Analog joystick port register                */
#define JOYSTICK2               0x01            /* Analog joystick port register                */
#define JOYSTICK3               0x02            /* Analog joystick port register                */
#define JOYSTICK4               0x03            /* Analog joystick port register                */
#define JOYSTICK5               0x04            /* Analog joystick port register                */
#define JOYSTICK6               0x05            /* Analog joystick port register                */
#define JOYSTICK7               0x06            /* Analog joystick port register                */
#define JOYSTICK8               0x07            /* Analog joystick port register                */

/* When writing, any write causes JOYSTICK_COMPARATOR output enable to be pulsed on write.      */
/* When reading, use these bitfields: */
#define JOYSTICK_BUTTONS        0x0f            /* Joystick button data                         */
#define JOYSTICK_COMPARATOR     0xf0            /* Joystick comparator data                     */

#define NR_DEV 5

static int io[NR_DEV] = { 0, };

enum {
	EMU10K1_JOYSTICK = 0
};

static char *card_names[] = {
	"EMU10K1 Joystick Port"
};

static struct pci_device_id emu10k1_joy_pci_tbl[] __devinitdata = {
	{PCI_VENDOR_ID_CREATIVE, PCI_DEVICE_ID_CREATIVE_EMU10K1_JOYSTICK,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, EMU10K1_JOYSTICK},
	{0,}
};

MODULE_DEVICE_TABLE(pci, emu10k1_joy_pci_tbl);

struct emu10k1_joy_card {
	struct list_head list;

	struct pci_dev *pci_dev;
	unsigned long iobase;
	unsigned long length;
	u8 addr_changed;
};

static LIST_HEAD(emu10k1_joy_devs);
static unsigned int devindex = 0;

/* Driver initialization routine */
static int __devinit emu10k1_joy_probe(struct pci_dev *pci_dev, const struct pci_device_id *pci_id)
{
	struct emu10k1_joy_card *card;
	u16 model;
	u8 chiprev;

	if ((card = kmalloc(sizeof(struct emu10k1_joy_card), GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "emu10k1-joy: out of memory\n");
		return -ENOMEM;
	}
	memset(card, 0, sizeof(struct emu10k1_joy_card));

	if (pci_enable_device(pci_dev)) {
		printk(KERN_ERR "emu10k1-joy: couldn't enable device\n");
		kfree(card);
		return -ENODEV;
	}

	card->iobase = pci_resource_start(pci_dev, 0); 
	card->length = pci_resource_len(pci_dev, 0);

	if (request_region(card->iobase, card->length, card_names[pci_id->driver_data])
	    == NULL) {
		printk(KERN_ERR "emu10k1-joy: IO space in use\n");
		kfree(card);
		return -ENODEV;
	}

	pci_set_drvdata(pci_dev, card);

	card->pci_dev = pci_dev;
	card->addr_changed = 0;

	pci_read_config_byte(pci_dev, PCI_REVISION_ID, &chiprev);
	pci_read_config_word(pci_dev, PCI_SUBSYSTEM_ID, &model);

	printk(KERN_INFO "emu10k1-joy: %s rev %d model 0x%x found, IO at 0x%04lx-0x%04lx\n",
		card_names[pci_id->driver_data], chiprev, model, card->iobase,
		card->iobase + card->length - 1);

	if (io[devindex]) {
		if ((io[devindex] & ~0x18) != 0x200) {
			printk(KERN_ERR "emu10k1-joy: invalid io value\n");
			release_region(card->iobase, card->length);
			kfree(card);
			return -ENODEV;
		}

		card->addr_changed = 1;
		pci_write_config_dword(pci_dev, PCI_BASE_ADDRESS_0, io[devindex]);
		printk(KERN_INFO "emu10k1-joy: IO ports mirrored at 0x%03x\n", io[devindex]);
	}

	list_add(&card->list, &emu10k1_joy_devs);
	devindex++;

	return 0;
}

static void __devexit emu10k1_joy_remove(struct pci_dev *pci_dev)
{
	struct emu10k1_joy_card *card = pci_get_drvdata(pci_dev);

	if(card->addr_changed)
		pci_write_config_dword(pci_dev, PCI_BASE_ADDRESS_0, card->iobase);

	release_region(card->iobase, card->length);

	list_del(&card->list);
	kfree(card);
	pci_set_drvdata(pci_dev, NULL);
}

MODULE_PARM(io, "1-" __MODULE_STRING(NR_DEV) "i");
MODULE_PARM_DESC(io, "sets joystick port address");
MODULE_AUTHOR("Rui Sousa (Email to: emu10k1-devel@opensource.creative.com)");
MODULE_DESCRIPTION("Creative EMU10K1 PCI Joystick Port v" DRIVER_VERSION
		   "\nCopyright (C) 2000 Rui Sousa");

static struct pci_driver emu10k1_joy_pci_driver = {
	name:"emu10k1 joystick",
	id_table:emu10k1_joy_pci_tbl,
	probe:emu10k1_joy_probe,
	remove:emu10k1_joy_remove,
};

static int __init emu10k1_joy_init_module(void)
{
	printk(KERN_INFO "Creative EMU10K1 PCI Joystick Port, version " DRIVER_VERSION ", " __TIME__
	       " " __DATE__ "\n");

	return pci_module_init(&emu10k1_joy_pci_driver);
}

static void __exit emu10k1_joy_cleanup_module(void)
{
	pci_unregister_driver(&emu10k1_joy_pci_driver);
	return;
}

module_init(emu10k1_joy_init_module);
module_exit(emu10k1_joy_cleanup_module);
