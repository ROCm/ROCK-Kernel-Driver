
/*
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY 
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */


#include <linux/config.h>
#include <linux/init.h>
#include <linux/fs.h>
#undef N_DATA

#include <linux/kernel.h>

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <errno.h>

#include "adapter.h"
#include "uxio.h"


#ifdef MODULE
#include "idi.h"
void DIVA_DIDD_Write(DESCRIPTOR *, int);
EXPORT_SYMBOL_NOVERS(DIVA_DIDD_Read);
EXPORT_SYMBOL_NOVERS(DIVA_DIDD_Write);
EXPORT_SYMBOL_NOVERS(DivasPrintf);
#endif

int DivasCardsDiscover(void);

static int __init
divas_init(void)
{
	printk(KERN_DEBUG "DIVA Server Driver - initialising\n");
	
	printk(KERN_DEBUG "DIVA Server Driver - Version 2.0.16\n");


#if !defined(CONFIG_PCI)
	printk(KERN_WARNING "CONFIG_PCI is not defined!\n");
	return -ENODEV;
#endif

	if (pci_present())
	{
		if (DivasCardsDiscover() < 0)
		{
			printk(KERN_WARNING "Divas: Not loaded\n");
			return -ENODEV;
		}
	}
	else
	{
		printk(KERN_WARNING "Divas: No PCI bus present\n");
		return -ENODEV;
	}

    return 0;
}

static void __exit
divas_exit(void)
{
	card_t *pCard;
	word wCardIndex;
	extern int Divas_major;

	printk(KERN_DEBUG "DIVA Server Driver - unloading\n");

	pCard = DivasCards;
	for (wCardIndex = 0; wCardIndex < MAX_CARDS; wCardIndex++)
	{
		if ((pCard->hw) && (pCard->hw->in_use))
		{

			(*pCard->card_reset)(pCard);
			
			UxIsrRemove(pCard->hw, pCard);
			UxCardHandleFree(pCard->hw);

			if(pCard->e_tbl != NULL)
			{
				kfree(pCard->e_tbl);
			}

			
			if(pCard->hw->card_type == DIA_CARD_TYPE_DIVA_SERVER_B)
			{	
				release_region(pCard->hw->io_base,0x20);		
				release_region(pCard->hw->reset_base,0x80);		
			}

			// If this is a 4BRI ...
			if (pCard->hw->card_type == DIA_CARD_TYPE_DIVA_SERVER_Q)
			{
				// Skip over the next 3 virtual adapters
				wCardIndex += 3;

				// But free their handles 
				pCard++;
				UxCardHandleFree(pCard->hw);
			
				if(pCard->e_tbl != NULL)
				{
					kfree(pCard->e_tbl);
				}
				
				pCard++;
				UxCardHandleFree(pCard->hw);
				
				if(pCard->e_tbl != NULL)
				{
					kfree(pCard->e_tbl);
				}
				
				pCard++;
				UxCardHandleFree(pCard->hw);
				
				if(pCard->e_tbl != NULL)
				{
					kfree(pCard->e_tbl);
				}
			}
		}
		pCard++;
	}

	unregister_chrdev(Divas_major, "Divas");
}

module_init(divas_init);
module_exit(divas_exit);

