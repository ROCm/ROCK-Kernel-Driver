/* $Id: shmem.c,v 1.2.10.1 2001/09/23 22:24:59 kai Exp $
 *
 * Copyright (C) 1996  SpellCaster Telecommunications Inc.
 *
 * Card functions implementing ISDN4Linux functionality
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * For more information, please contact gpl-info@spellcast.com or write:
 *
 *     SpellCaster Telecommunications Inc.
 *     5621 Finch Avenue East, Unit #3
 *     Scarborough, Ontario  Canada
 *     M1B 2T9
 *     +1 (416) 297-8565
 *     +1 (416) 297-6433 Facsimile
 */

#include "includes.h"		/* This must be first */
#include "hardware.h"
#include "card.h"

/*
 * Main adapter array
 */
extern board *adapter[];
extern int cinst;

/*
 *
 */
void *memcpy_toshmem(int card, void *dest, const void *src, size_t n)
{
	unsigned long flags;
	void *ret;
	unsigned char ch;

	if(!IS_VALID_CARD(card)) {
		pr_debug("Invalid param: %d is not a valid card id\n", card);
		return NULL;
	}

	if(n > SRAM_PAGESIZE) {
		return NULL;
	}

	/*
	 * determine the page to load from the address
	 */
	ch = (unsigned long) dest / SRAM_PAGESIZE;
	pr_debug("%s: loaded page %d\n",adapter[card]->devicename,ch);
	/*
	 * Block interrupts and load the page
	 */
	spin_lock_irqsave(&adapter[card]->lock, flags);

	outb(((adapter[card]->shmem_magic + ch * SRAM_PAGESIZE) >> 14) | 0x80,
		adapter[card]->ioport[adapter[card]->shmem_pgport]);
	ret = memcpy_toio(adapter[card]->rambase + 
		((unsigned long) dest % 0x4000), src, n);
	spin_unlock_irqrestore(&adapter[card]->lock, flags);
	pr_debug("%s: set page to %#x\n",adapter[card]->devicename,
		((adapter[card]->shmem_magic + ch * SRAM_PAGESIZE)>>14)|0x80);
	pr_debug("%s: copying %d bytes from %#x to %#x\n",adapter[card]->devicename, n,
		 (unsigned long) src, adapter[card]->rambase + ((unsigned long) dest %0x4000));

	return ret;
}

/*
 * Reverse of above
 */
void *memcpy_fromshmem(int card, void *dest, const void *src, size_t n)
{
	unsigned long flags;
	void *ret;
	unsigned char ch;

	if(!IS_VALID_CARD(card)) {
		pr_debug("Invalid param: %d is not a valid card id\n", card);
		return NULL;
	}

	if(n > SRAM_PAGESIZE) {
		return NULL;
	}

	/*
	 * determine the page to load from the address
	 */
	ch = (unsigned long) src / SRAM_PAGESIZE;
	pr_debug("%s: loaded page %d\n",adapter[card]->devicename,ch);
	
	
	/*
	 * Block interrupts and load the page
	 */
	spin_lock_irqsave(&adapter[card]->lock, flags);

	outb(((adapter[card]->shmem_magic + ch * SRAM_PAGESIZE) >> 14) | 0x80,
		adapter[card]->ioport[adapter[card]->shmem_pgport]);
	ret = memcpy_fromio(dest,(void *)(adapter[card]->rambase + 
		((unsigned long) src % 0x4000)), n);
	spin_unlock_irqrestore(&adapter[card]->lock, flags);
	pr_debug("%s: set page to %#x\n",adapter[card]->devicename,
		((adapter[card]->shmem_magic + ch * SRAM_PAGESIZE)>>14)|0x80);
/*	pr_debug("%s: copying %d bytes from %#x to %#x\n",
		adapter[card]->devicename, n,
		adapter[card]->rambase + ((unsigned long) src %0x4000), (unsigned long) dest); */

	return ret;
}

void *memset_shmem(int card, void *dest, int c, size_t n)
{
	unsigned long flags;
	unsigned char ch;
	void *ret;

	if(!IS_VALID_CARD(card)) {
		pr_debug("Invalid param: %d is not a valid card id\n", card);
		return NULL;
	}

	if(n > SRAM_PAGESIZE) {
		return NULL;
	}

	/*
	 * determine the page to load from the address
	 */
	ch = (unsigned long) dest / SRAM_PAGESIZE;
	pr_debug("%s: loaded page %d\n",adapter[card]->devicename,ch);

	/*
	 * Block interrupts and load the page
	 */
	spin_lock_irqsave(&adapter[card]->lock, flags);

	outb(((adapter[card]->shmem_magic + ch * SRAM_PAGESIZE) >> 14) | 0x80,
		adapter[card]->ioport[adapter[card]->shmem_pgport]);
	ret = memset_io(adapter[card]->rambase + 
		((unsigned long) dest % 0x4000), c, n);
	pr_debug("%s: set page to %#x\n",adapter[card]->devicename,
		((adapter[card]->shmem_magic + ch * SRAM_PAGESIZE)>>14)|0x80);
	spin_unlock_irqrestore(&adapter[card]->lock, flags);

	return ret;
}
