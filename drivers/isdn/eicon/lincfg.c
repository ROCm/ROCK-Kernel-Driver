/*
 * Copyright (C) Eicon Technology Corporation, 2000.
 *
 * Eicon File Revision :    1.9  
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/fs.h>
#undef N_DATA   /* Because we have our own definition */

#include <asm/io.h>

#include "sys.h"
#include "idi.h"
#include "constant.h"
#include "divas.h"
#undef ID_MASK
#include "pc.h"
#include "pr_pc.h"

#include "adapter.h"
#include "uxio.h"

#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/ioport.h>

struct file_operations Divas_fops;
int Divas_major;

extern int do_ioctl(struct inode *pDivasInode, struct file *pDivasFile, 
			 unsigned int command, unsigned long arg);
extern unsigned int do_poll(struct file *pFile, struct poll_table_struct *pPollTable);
extern ssize_t do_read(struct file *pFile, char *pUserBuffer, size_t BufferSize, loff_t *pOffset);
extern int do_open(struct inode *, struct file *);
extern int do_release(struct inode *, struct file *);

int FPGA_Done=0;

int DivasCardsDiscover(void)
{
	struct pci_dev *pdev = NULL;
	word wNumCards = 0, wDeviceIndex = 0;
	word PCItmp;
	dword j, i;
	unsigned int PCIserial;
	dia_card_t Card;
	byte *b;
	
	while ((pdev = pci_find_device(PCI_VENDOR_ID_EICON, 
				      PCI_DEVICE_ID_EICON_MAESTRAQ, 
				      pdev))) {
		dword dwRAM, dwDivasIOBase, dwCFG, dwCTL;
			
		printk(KERN_DEBUG "Divas: DIVA Server 4BRI Found\n");

		dwRAM = pci_resource_start(pdev, 2);
		dwDivasIOBase = pci_resource_start(pdev, 1);
		dwCFG = pci_resource_start(pdev, 0);
		dwCTL = pci_resource_start(pdev, 3);

		/* Retrieve the serial number */
		pci_write_config_word(pdev, 0x4E, 0x00FC);
		for (j=0, PCItmp=0; j<10000 && !PCItmp; j++) {
			pci_read_config_word(pdev, 0x4E, &PCItmp);
			PCItmp &= 0x8000;  // extract done flag
		}
		pci_read_config_dword(pdev, 0x50, &PCIserial);
		
		Card.memory[DIVAS_RAM_MEMORY] = ioremap(dwRAM, 0x400000);
		Card.memory[DIVAS_CTL_MEMORY] = ioremap(dwCTL, 0x2000);
		Card.memory[DIVAS_CFG_MEMORY] = ioremap(dwCFG, 0x100);
		Card.io_base=dwDivasIOBase;

		Card.card_type = DIA_CARD_TYPE_DIVA_SERVER_Q;
		Card.bus_type = DIA_BUS_TYPE_PCI;
		Card.pdev = pdev;
		Card.irq = pdev->irq;
			
		FPGA_Done = 0;

		/* Create four virtual card structures as we want to treat 
		   the 4Bri card as 4 Bri cards*/
		for(i=0;i<4;i++) {
			b=Card.memory[DIVAS_RAM_MEMORY];
			b+=(MQ_PROTCODE_OFFSET) * (i==0?0:1); 
			DPRINTF(("divas: offset = 0x%x", i* MQ_PROTCODE_OFFSET));
			Card.memory[DIVAS_RAM_MEMORY]=b;
 
			b = Card.memory[DIVAS_RAM_MEMORY];
			b += MQ_SM_OFFSET;
			Card.memory[DIVAS_SHARED_MEMORY] = b;

			Card.slot = -1;

			sprintf(Card.name, "DIVASQ%ld", i);

			Card.serial = PCIserial;

			Card.card_id = wNumCards;

			if (DivasCardNew(&Card) != 0) {
				break;
			}
			wNumCards++;

		}
	}

	pdev = NULL;
	while ((pdev = pci_find_device(PCI_VENDOR_ID_EICON, 
				      PCI_DEVICE_ID_EICON_MAESTRA, 
				      pdev))) {
		dword dwPLXIOBase, dwDivasIOBase;

		printk(KERN_DEBUG "Divas: DIVA Server BRI (S/T) Found\n");
		dwPLXIOBase = pci_resource_start(pdev, 1);
		dwDivasIOBase = pci_resource_start(pdev, 2);
		
		Card.card_id = wNumCards;
		Card.card_type = DIA_CARD_TYPE_DIVA_SERVER_B;
		Card.bus_type = DIA_BUS_TYPE_PCI;
		Card.pdev = pdev;
		Card.irq = pdev->irq;
		Card.reset_base = dwPLXIOBase;
		Card.io_base = dwDivasIOBase;
		Card.slot = -1;
		strcpy(Card.name, "DIVASB");

		if (check_region(Card.io_base, 0x20)) {
			printk(KERN_WARNING "Divas: DIVA I/O Base already in use 0x%x-0x%x\n", Card.io_base, Card.io_base + 0x1F);
		}

		if (check_region(Card.reset_base, 0x80)) {
			printk(KERN_WARNING "Divas: PLX I/O Base already in use 0x%x-0x%x\n", Card.reset_base, Card.reset_base + 0x7F);
			continue;
		}
		
		if (DivasCardNew(&Card) != 0) {
			continue;
		}
		wNumCards++;
	}

	pdev = NULL;
	while ((pdev = pci_find_device(PCI_VENDOR_ID_EICON, 
				      PCI_DEVICE_ID_EICON_MAESTRAQ_U, 
				      pdev))) {
		dword dwPLXIOBase, dwDivasIOBase;

		printk(KERN_DEBUG "Divas: DIVA Server BRI (U) Found\n");

		dwPLXIOBase = pci_resource_start(pdev, 1);
		dwDivasIOBase = pci_resource_start(pdev, 2);
		
		Card.card_id = wNumCards;
		Card.card_type = DIA_CARD_TYPE_DIVA_SERVER_B;
		Card.bus_type = DIA_BUS_TYPE_PCI;
		Card.pdev = pdev;
		Card.irq = pdev->irq;
		Card.reset_base = dwPLXIOBase;
		Card.io_base = dwDivasIOBase;
		Card.slot = -1;
		strcpy(Card.name, "DIVASB");

		if (check_region(Card.io_base, 0x20)) {
			printk(KERN_WARNING "Divas: DIVA I/O Base already in use 0x%x-0x%x\n", Card.io_base, Card.io_base + 0x1F);	
			continue;
		}

		if (check_region(Card.reset_base, 0x80)) {
			printk(KERN_WARNING "Divas: PLX I/O Base already in use 0x%x-0x%x\n", Card.reset_base, Card.reset_base + 0x7F);
			continue;
		}

		if (DivasCardNew(&Card) != 0) {
			continue;
		}
		wNumCards++;
	}

	wDeviceIndex = 0;

	pdev = NULL;
	while ((pdev = pci_find_device(PCI_VENDOR_ID_EICON, 
				      PCI_DEVICE_ID_EICON_MAESTRAQ_U, 
				      pdev))) {
		dword dwRAM, dwREG, dwCFG;

		printk(KERN_DEBUG "Divas: DIVA Server PRI Found\n");
		
		dwRAM = pci_resource_start(pdev, 0);
		dwREG = pci_resource_start(pdev, 2);
		dwCFG = pci_resource_start(pdev, 4);
		
		Card.memory[DIVAS_RAM_MEMORY] = ioremap(dwRAM, 0x10000);
		Card.memory[DIVAS_REG_MEMORY] = ioremap(dwREG, 0x4000);
		Card.memory[DIVAS_CFG_MEMORY] = ioremap(dwCFG, 0x1000);
		Card.memory[DIVAS_SHARED_MEMORY] = Card.memory[DIVAS_RAM_MEMORY] + DIVAS_SHARED_OFFSET;
		
		Card.card_id = wNumCards;
		Card.card_type = DIA_CARD_TYPE_DIVA_SERVER;
		Card.bus_type = DIA_BUS_TYPE_PCI;
		Card.pdev = pdev;
		Card.irq = pdev->irq;
		Card.slot = -1;
		strcpy(Card.name, "DIVASP");
		if (DivasCardNew(&Card) != 0) {
			continue;
		}
		wNumCards++;
	}


	printk(KERN_INFO "Divas: %d cards detected\n", wNumCards);

	if(wNumCards == 0) {
		return -1;
	}

	Divas_fops.ioctl = do_ioctl;
	Divas_fops.poll = do_poll;
	Divas_fops.read = do_read;
	Divas_fops.open = do_open;
	Divas_fops.release = do_release;

	Divas_major = register_chrdev(0, "Divas", &Divas_fops);

	if (Divas_major < 0) {
		printk(KERN_WARNING "Divas: Unable to register character driver\n");
		return -1;
	}

	return 0;
}

/* Error return -1 */
int DivasConfigGet(dia_card_t *card)
{
	/* Retrieve Config from O/S? Not in Linux */
	return 0;
}

dia_config_t *DivasConfig(card_t *card, dia_config_t *config)
{
	/*	If config retrieved from OS then copy the data into a dia_config_t structure here
		and return the pointer here. If the config 'came from above' then just 

			return config;
	*/

	return config;
}

