/* daynaport.c: A Macintosh 8390 based ethernet driver for linux. */
/*
	Derived from code:
	
	Written 1993-94 by Donald Becker.

	Copyright 1993 United States Government as represented by the
	Director, National Security Agency.

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

	    TODO:

	    The block output routines may be wrong for non Dayna
	    cards

		Fix this driver so that it will attempt to use the info
		(i.e. iobase, iosize) given to it by the new and improved
		NuBus code.

		Despite its misleading filename, this driver is not Dayna-specific
		anymore. */
/* Cabletron E6100 card support added by Tony Mantler (eek@escape.ca) April 1999 */

static const char *version =
	"daynaport.c: v0.02 1999-05-17 Alan Cox (Alan.Cox@linux.org) and others\n";
static int version_printed;

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/nubus.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/hwtest.h>
#include <asm/macints.h>
#include <linux/delay.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include "8390.h"

static int ns8390_probe1(struct net_device *dev, int word16, char *name, int id,
				  int prom, struct nubus_dev *ndev);

static int ns8390_open(struct net_device *dev);
static void ns8390_no_reset(struct net_device *dev);
static int ns8390_close_card(struct net_device *dev);

/* Interlan */
static void interlan_reset(struct net_device *dev);

/* Dayna */
static void dayna_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr,
						int ring_page);
static void dayna_block_input(struct net_device *dev, int count,
						  struct sk_buff *skb, int ring_offset);
static void dayna_block_output(struct net_device *dev, int count,
						   const unsigned char *buf, const int start_page);

/* Sane (32-bit chunk memory read/write) */
static void sane_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr,
						int ring_page);
static void sane_block_input(struct net_device *dev, int count,
						  struct sk_buff *skb, int ring_offset);
static void sane_block_output(struct net_device *dev, int count,
						   const unsigned char *buf, const int start_page);

/* Slow Sane (16-bit chunk memory read/write) */
static void slow_sane_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr,
						int ring_page);
static void slow_sane_block_input(struct net_device *dev, int count,
						  struct sk_buff *skb, int ring_offset);
static void slow_sane_block_output(struct net_device *dev, int count,
						   const unsigned char *buf, const int start_page);


#define WD_START_PG	0x00	/* First page of TX buffer */
#define WD03_STOP_PG	0x20	/* Last page +1 of RX ring */
#define WD13_STOP_PG	0x40	/* Last page +1 of RX ring */

#define CABLETRON_RX_START_PG          0x00    /* First page of RX buffer */
#define CABLETRON_RX_STOP_PG           0x30    /* Last page +1 of RX ring */
#define CABLETRON_TX_START_PG          CABLETRON_RX_STOP_PG  /* First page of TX buffer */


#define DAYNA_MAC_BASE		0xf0007
#define DAYNA_8390_BASE		0x80000 /* 3 */
#define DAYNA_8390_MEM		0x00000
#define DAYNA_MEMSIZE		0x04000	/* First word of each long ! */

#define APPLE_8390_BASE		0xE0000
#define APPLE_8390_MEM		0xD0000
#define APPLE_MEMSIZE		8192    /* FIXME: need to dynamically check */

#define KINETICS_MAC_BASE	0xf0004 /* first byte of each long */
#define KINETICS_8390_BASE	0x80000
#define KINETICS_8390_MEM	0x00000 /* first word of each long */
#define KINETICS_MEMSIZE	8192    /* FIXME: need to dynamically check */
/*#define KINETICS_MEMSIZE	(0x10000/2) * CSA: on the board I have, at least */

#define CABLETRON_8390_BASE		0x90000	
#define CABLETRON_8390_MEM		0x00000

static int test_8390(volatile char *ptr, int scale)
{
	int regd;
	int v;
	
	if(hwreg_present(&ptr[0x00])==0)
		return -EIO;
	if(hwreg_present(&ptr[0x0D<<scale])==0)
		return -EIO;
	if(hwreg_present(&ptr[0x0D<<scale])==0)
		return -EIO;
	ptr[0x00]=E8390_NODMA+E8390_PAGE1+E8390_STOP;
	regd=ptr[0x0D<<scale];
	ptr[0x0D<<scale]=0xFF;
	ptr[0x00]=E8390_NODMA+E8390_PAGE0;
	v=ptr[0x0D<<scale];
	if(ptr[0x0D<<scale]!=0)
	{
		ptr[0x0D<<scale]=regd;
		return -ENODEV;
	}
/*	printk("NS8390 found at %p scaled %d\n", ptr,scale);*/
	return 0;
}
/*
 *    Identify the species of NS8390 card/driver we need
 */

enum mac8390_type {
	NS8390_DAYNA,
	NS8390_INTERLAN,
	NS8390_KINETICS,
	NS8390_APPLE,
	NS8390_FARALLON,
	NS8390_ASANTE,
	NS8390_CABLETRON
};

static int __init ns8390_ident(struct nubus_dev* ndev)
{
	/* This really needs to be tested and tested hard.  */
		
	/* Summary of what we know so far --
	 * SW: 0x0104 -- asante,    16 bit, back4_offsets
	 * SW: 0x010b -- daynaport, 16 bit, fwrd4_offsets
	 * SW: 0x010c -- farallon,  16 bit, back4_offsets, no long word access
	 * SW: 0x011a -- focus,     [no details yet]
	 * SW: ?????? -- interlan,  16 bit, back4_offsets, funny reset
	 * SW: ?????? -- kinetics,   8 bit, back4_offsets
	 * -- so i've this hypothesis going that says DrSW&1 says whether the
	 *    map is forward or backwards -- and maybe DrSW&256 says what the
	 *    register spacing is -- for all cards that report a DrSW in some
	 *    range.
	 *    This would allow the "apple compatible" driver to drive many
	 *    seemingly different types of cards.  More DrSW info is needed
	 *    to investigate this properly. [CSA, 21-May-1999]
	 */
	/* Dayna ex Kinetics board */
	if(ndev->dr_sw == NUBUS_DRSW_DAYNA)
		return NS8390_DAYNA;
	if(ndev->dr_sw == NUBUS_DRSW_ASANTE)
		return NS8390_ASANTE;
	if(ndev->dr_sw == NUBUS_DRSW_FARALLON) /* farallon or sonic systems */
		return NS8390_FARALLON;
	if(ndev->dr_sw == NUBUS_DRSW_KINETICS)
		return NS8390_KINETICS;
	/* My ATI Engineering card with this combination crashes the */
	/* driver trying to xmit packets. Best not touch it for now. */
	/*     - 1999-05-20 (funaho@jurai.org)                       */
	if(ndev->dr_sw == NUBUS_DRSW_FOCUS)
		return -1;

	/* Check the HW on this one, because it shares the same DrSW as
	   the on-board SONIC chips */
	if(ndev->dr_hw == NUBUS_DRHW_CABLETRON)
		return NS8390_CABLETRON;
	/* does anyone have one of these? */
	if(ndev->dr_hw == NUBUS_DRHW_INTERLAN)
		return NS8390_INTERLAN;

	/* FIXME: what do genuine Apple boards look like? */
	return -1;
}

/*
 *	Memory probe for 8390 cards
 */
 
static int __init apple_8390_mem_probe(volatile unsigned short *p)
{
	int i, j;
	/*
	 *	Algorithm.
	 *	1.	Check each block size of memory doesn't fault
	 *	2.	Write a value to it
	 *	3.	Check all previous blocks are unaffected
	 */
	
	for(i=0;i<2;i++)
	{
		volatile unsigned short *m=p+4096*i;
		/* Unwriteable - we have a fully decoded card and the
		   RAM end located */
		   
		if(hwreg_present(m)==0)
			return 8192*i;
			
		*m=0xA5A0|i;
		
		for(j=0;j<i;j++)
		{
			/* Partial decode and wrap ? */
			if(p[4096*j]!=(0xA5A0|j))
			{
				/* This is the first misdecode, so it had
				   one less page than we tried */
				return 8192*i;
			}
 			j++;
 		}
 		/* Ok it still decodes.. move on 8K */
 	}
 	/* 
 	 *	We don't look past 16K. That should cover most cards
 	 *	and above 16K there isnt really any gain.
 	 */
 	return 16384;
 }
 		
/*
 *    Probe for 8390 cards.  
 *    The ns8390_probe1() routine initializes the card and fills the
 *    station address field.
 *
 *    The NuBus interface has changed!  We now scan for these somewhat
 *    like how the PCI and Zorro drivers do.  It's not clear whether
 *    this is actually better, but it makes things more consistent.
 *
 *    dev->mem_start points
 *    at the memory ring, dev->mem_end gives the end of it.
 */

int __init mac8390_probe(struct net_device *dev)
{
	static int slots;
	volatile unsigned short *i;
	volatile unsigned char *p;
	int plen;
	int id;
	static struct nubus_dev* ndev;

	/* Find the first card that hasn't already been seen */
	while ((ndev = nubus_find_type(NUBUS_CAT_NETWORK,
								   NUBUS_TYPE_ETHERNET, ndev)) != NULL) {
		/* Have we seen it already? */
		if (slots & (1<<ndev->board->slot))
			continue;
		slots |= 1<<ndev->board->slot;

		/* Is it one of ours? */
		if ((id = ns8390_ident(ndev)) != -1)
			break;
	}

	/* Hm.  No more cards, then */
	if (ndev == NULL)
		return -ENODEV;

	dev = init_etherdev(dev, 0);
	if (!dev)
		return -ENOMEM;
	SET_MODULE_OWNER(dev);

	if (!version_printed) {
		printk(KERN_INFO "%s", version);
		version_printed = 1;
	}

	/*
	 *	Dayna specific init
	 */
	if(id==NS8390_DAYNA)
	{
		dev->base_addr = (int)(ndev->board->slot_addr+DAYNA_8390_BASE);
		dev->mem_start = (int)(ndev->board->slot_addr+DAYNA_8390_MEM);
		dev->mem_end = dev->mem_start+DAYNA_MEMSIZE; /* 8K it seems */
	
		printk(KERN_INFO "%s: daynaport. testing board: ", dev->name);
			
		printk("memory - ");	
			
		i = (void *)dev->mem_start;
		memset((void *)i,0xAA, DAYNA_MEMSIZE);
		while(i<(volatile unsigned short *)dev->mem_end)
		{
			if(*i!=0xAAAA)
				goto membad;
			*i=0x5678; /* make sure we catch byte smearing */
			if(*i!=0x5678)
				goto membad;
			i+=2;	/* Skip a word */
		}
			
		printk("controller - ");
			
		p=(void *)dev->base_addr;
		plen=0;
			
		while(plen<0x3FF00)
		{
			if(test_8390(p,0)==0)
				break;
			if(test_8390(p,1)==0)
				break;
			if(test_8390(p,2)==0)
				break;
			if(test_8390(p,3)==0)
				break;
			plen++;
			p++;
		}
		if(plen==0x3FF00)
			goto membad;
		printk("OK\n");
		dev->irq = SLOT2IRQ(ndev->board->slot);
		if(ns8390_probe1(dev, 0, "dayna", id, -1, ndev)==0)
			return 0;
	}
	/* Cabletron */
	if (id==NS8390_CABLETRON) {
		int memsize = 16<<10; /* fix this */
		  
		dev->base_addr=(int)(ndev->board->slot_addr+CABLETRON_8390_BASE);
		dev->mem_start=(int)(ndev->board->slot_addr+CABLETRON_8390_MEM);
		dev->mem_end=dev->mem_start+memsize;
		dev->irq = SLOT2IRQ(ndev->board->slot);
		  
		/* The base address is unreadable if 0x00 has been written to the command register */
		/* Reset the chip by writing E8390_NODMA+E8390_PAGE0+E8390_STOP just to be sure */
		i = (void *)dev->base_addr;
		*i = 0x21;
		  
		printk(KERN_INFO "%s: cabletron: testing board: ", dev->name);
		printk("%dK memory - ", memsize>>10);		
		i=(void *)dev->mem_start;
		while(i<(volatile unsigned short *)(dev->mem_start+memsize))
		{
			*i=0xAAAA;
			if(*i!=0xAAAA)
				goto membad;
			*i=0x5555;
			if(*i!=0x5555)
				goto membad;
			i+=2;	/* Skip a word */
		}
		printk("OK\n");
		  
		if(ns8390_probe1(dev, 1, "cabletron", id, -1, ndev)==0)
			return 0;
	}
	/* Apple, Farallon, Asante */
	if(id==NS8390_APPLE || id==NS8390_FARALLON || id==NS8390_ASANTE)
	{
		int memsize;
			
		dev->base_addr=(int)(ndev->board->slot_addr+APPLE_8390_BASE);
		dev->mem_start=(int)(ndev->board->slot_addr+APPLE_8390_MEM);
			
		memsize = apple_8390_mem_probe((void *)dev->mem_start);
			
		dev->mem_end=dev->mem_start+memsize;
		dev->irq = SLOT2IRQ(ndev->board->slot);
			
		switch(id)
		{
		case NS8390_FARALLON:
			printk(KERN_INFO "%s: farallon: testing board: ", dev->name);
			break;
		case NS8390_ASANTE:
			printk(KERN_INFO "%s: asante: testing board: ", dev->name);
			break;
		case NS8390_APPLE:
		default:
			printk(KERN_INFO "%s: apple/clone: testing board: ", dev->name);
			break;
		}
			
		printk("%dK memory - ", memsize>>10);		
			
		i=(void *)dev->mem_start;
		memset((void *)i,0xAA, memsize);
		while(i<(volatile unsigned short *)dev->mem_end)
		{
			if(*i!=0xAAAA)
				goto membad;
			*i=0x5555;
			if(*i!=0x5555)
				goto membad;
			i+=2;	/* Skip a word */
		}
		printk("OK\n");
			
		switch (id)
		{
		case NS8390_FARALLON:
			if(ns8390_probe1(dev, 1, "farallon", id, -1, ndev)==0)
				return 0;
			break;
		case NS8390_ASANTE:
			if(ns8390_probe1(dev, 1, "asante", id, -1, ndev)==0)
				return 0;
			break;
		case NS8390_APPLE:
		default:
			if(ns8390_probe1(dev, 1, "apple/clone", id, -1, ndev)==0)
				return 0;
			break;
		}
	}
	/* Interlan */
	if(id==NS8390_INTERLAN)
	{
		/* As apple and asante */
		dev->base_addr=(int)(ndev->board->slot_addr+APPLE_8390_BASE);
		dev->mem_start=(int)(ndev->board->slot_addr+APPLE_8390_MEM);
		dev->mem_end=dev->mem_start+APPLE_MEMSIZE; /* 8K it seems */
		dev->irq = SLOT2IRQ(ndev->board->slot);
		if(ns8390_probe1(dev, 1, "interlan", id, -1, ndev)==0)
			return 0;
	}
	/* Kinetics (Shiva Etherport) */
	if(id==NS8390_KINETICS)
	{
		dev->base_addr=(int)(ndev->board->slot_addr+KINETICS_8390_BASE);
		dev->mem_start=(int)(ndev->board->slot_addr+KINETICS_8390_MEM);
		dev->mem_end=dev->mem_start+KINETICS_MEMSIZE; /* 8K it seems */
		dev->irq = SLOT2IRQ(ndev->board->slot);
		if(ns8390_probe1(dev, 0, "kinetics", id, -1, ndev)==0)
			return 0;
	}

	/* We should hopefully not get here */
	printk(KERN_ERR "Probe unsuccessful.\n");
	return -ENODEV;

 membad:
	printk(KERN_ERR "failed at %p in %p - %p.\n", i,
		   (void *)dev->mem_start, (void *)dev->mem_end);
	return -ENODEV;
}

static int __init mac8390_ethernet_addr(struct nubus_dev* ndev, unsigned char addr[6])
{
	struct nubus_dir dir;
	struct nubus_dirent ent;

	/* Get the functional resource for this device */
	if (nubus_get_func_dir(ndev, &dir) == -1)
		return -1;
	if (nubus_find_rsrc(&dir, NUBUS_RESID_MAC_ADDRESS, &ent) == -1)
		return -1;
	
	nubus_get_rsrc_mem(addr, &ent, 6);
	return 0;
}

static int __init ns8390_probe1(struct net_device *dev, int word16, char *model_name,
			 	int type, int promoff, struct nubus_dev *ndev)
{
	static u32 fwrd4_offsets[16]={
		0,      4,      8,      12,
		16,     20,     24,     28,
		32,     36,     40,     44,
		48,     52,     56,     60
	};
	static u32 back4_offsets[16]={
		60,     56,     52,     48,
		44,     40,     36,     32,
		28,     24,     20,     16,
		12,     8,      4,      0
	};
	static u32 fwrd2_offsets[16]={
		0,      2,      4,      6,
		8,     10,     12,     14,
		16,    18,     20,     22,
		24,    26,     28,     30
	};

	unsigned char *prom = (unsigned char*) ndev->board->slot_addr + promoff;

	/* Allocate dev->priv and fill in 8390 specific dev fields. */
	if (ethdev_init(dev)) 
	{	
		printk ("%s: unable to get memory for dev->priv.\n", dev->name);
		return -ENOMEM;
	}

	/* OK, we are certain this is going to work.  Setup the device. */

	ei_status.name = model_name;
	ei_status.word16 = word16;

       if (type==NS8390_CABLETRON) {
               /* Cabletron card puts the RX buffer before the TX buffer */
               ei_status.tx_start_page = CABLETRON_TX_START_PG;
               ei_status.rx_start_page = CABLETRON_RX_START_PG;
               ei_status.stop_page = CABLETRON_RX_STOP_PG;
               dev->rmem_start = dev->mem_start;
               dev->rmem_end = dev->mem_start + CABLETRON_RX_STOP_PG*256;
       } else {
               ei_status.tx_start_page = WD_START_PG;
               ei_status.rx_start_page = WD_START_PG + TX_PAGES;
               ei_status.stop_page = (dev->mem_end - dev->mem_start)/256;
               dev->rmem_start = dev->mem_start + TX_PAGES*256;
               dev->rmem_end = dev->mem_end;
       }
	
	if(promoff==-1)		/* Use nubus resources ? */
	{
		if(mac8390_ethernet_addr(ndev, dev->dev_addr))
		{
		  printk("mac_ns8390: MAC address not in resources!\n");
		  return -ENODEV;
		}
	}
	else			/* Pull it off the card */
	{
		int i=0;
		int x=1;
		/* These should go in the end I hope */
		if(type==NS8390_DAYNA)
			x=2;
		if(type==NS8390_INTERLAN || type==NS8390_KINETICS)
			x=4;
		while(i<6)
		{
			dev->dev_addr[i]=*prom;
			prom+=x;
			if(i)
				printk(":");
			printk("%02X",dev->dev_addr[i++]);
		}
	}

	printk(KERN_INFO "%s: %s in slot %X (type %s)\n",
		   dev->name, ndev->board->name, ndev->board->slot, model_name);
	printk(KERN_INFO "MAC ");
	{
		int i;
		for (i = 0; i < 6; i++) {
			printk("%2.2x", dev->dev_addr[i]);
			if (i < 5)
				printk(":");
		}
	}
	printk(" IRQ %d, shared memory at %#lx-%#lx.\n",
		   dev->irq, dev->mem_start, dev->mem_end-1);

	switch(type)
	{
		case NS8390_DAYNA:      /* Dayna card */
		case NS8390_KINETICS:   /* Kinetics --  8 bit config, but 16 bit mem */
			/* 16 bit, 4 word offsets */
			ei_status.reset_8390 = &ns8390_no_reset;
			ei_status.block_input = &dayna_block_input;
			ei_status.block_output = &dayna_block_output;
			ei_status.get_8390_hdr = &dayna_get_8390_hdr;
			ei_status.reg_offset = fwrd4_offsets;
			break;
		case NS8390_CABLETRON: /* Cabletron */
			/*		16 bit card, register map is short forward */
			ei_status.reset_8390 = &ns8390_no_reset;
			/* Ctron card won't accept 32bit values read or written to it */
			ei_status.block_input = &slow_sane_block_input;
			ei_status.block_output = &slow_sane_block_output;
			ei_status.get_8390_hdr = &slow_sane_get_8390_hdr;
			ei_status.reg_offset = fwrd2_offsets;
			break;
		case NS8390_FARALLON:
		case NS8390_APPLE:	/* Apple/Asante/Farallon */
			/*      16 bit card, register map is reversed */
			ei_status.reset_8390 = &ns8390_no_reset;
			ei_status.block_input = &slow_sane_block_input;
			ei_status.block_output = &slow_sane_block_output;
			ei_status.get_8390_hdr = &slow_sane_get_8390_hdr;
			ei_status.reg_offset = back4_offsets;
			break;
		case NS8390_ASANTE:
			/*      16 bit card, register map is reversed */
			ei_status.reset_8390 = &ns8390_no_reset;
			ei_status.block_input = &sane_block_input;
			ei_status.block_output = &sane_block_output;
			ei_status.get_8390_hdr = &sane_get_8390_hdr;
			ei_status.reg_offset = back4_offsets;
			break;
		case NS8390_INTERLAN:   /* Interlan */
			/*      16 bit card, map is forward */
			ei_status.reset_8390 = &interlan_reset;
			ei_status.block_input = &sane_block_input;
			ei_status.block_output = &sane_block_output;
			ei_status.get_8390_hdr = &sane_get_8390_hdr;
			ei_status.reg_offset = back4_offsets;
			break;
#if 0 /* i think this suffered code rot.  my kinetics card has much
	   * different settings.  -- CSA [22-May-1999] */
		case NS8390_KINETICS:   /* Kinetics */
			/*      8bit card, map is forward */
			ei_status.reset_8390 = &ns8390_no_reset;
			ei_status.block_input = &sane_block_input;
			ei_status.block_output = &sane_block_output;
			ei_status.get_8390_hdr = &sane_get_8390_hdr;
			ei_status.reg_offset = back4_offsets;
			break;
#endif
		default:
			panic("Detected a card I can't drive - whoops\n");
	}
	dev->open = &ns8390_open;
	dev->stop = &ns8390_close_card;

	NS8390_init(dev, 0);

	return 0;
}

static int ns8390_open(struct net_device *dev)
{
	int ret;

	ei_open(dev);

	/* At least on my card (a Focus Enhancements PDS card) I start */
	/* getting interrupts right away, so the driver needs to be    */
	/* completely initialized before enabling the interrupt.        */
	/*                             - funaho@jurai.org (1999-05-17) */

	/* Non-slow interrupt, works around issues with the SONIC driver */
	ret = request_irq(dev->irq, ei_interrupt, 0, dev->name, dev); 
	if (ret) {
		printk ("%s: unable to get IRQ %d.\n", dev->name, dev->irq);
		return ret;
	}
	return 0;
}

static void ns8390_no_reset(struct net_device *dev)
{
	if (ei_debug > 1) 
		printk("Need to reset the NS8390 t=%lu...", jiffies);
	ei_status.txing = 0;
	if (ei_debug > 1) printk("reset not supported\n");
}

static int ns8390_close_card(struct net_device *dev)
{
	if (ei_debug > 1)
		printk("%s: Shutting down ethercard.\n", dev->name);
	free_irq(dev->irq, dev);
	ei_close(dev);
	return 0;
}

/*
 *    Interlan Specific Code Starts Here
 */

static void interlan_reset(struct net_device *dev)
{
	unsigned char *target=nubus_slot_addr(IRQ2SLOT(dev->irq));
	if (ei_debug > 1) 
		printk("Need to reset the NS8390 t=%lu...", jiffies);
	ei_status.txing = 0;
	/* This write resets the card */
	target[0xC0000]=0;
	if (ei_debug > 1) printk("reset complete\n");
	return;
}

/*
 *    Daynaport code (some is used by other drivers)
 */


/* Grab the 8390 specific header. Similar to the block_input routine, but
   we don't need to be concerned with ring wrap as the header will be at
   the start of a page, so we optimize accordingly. */


/* Block input and output are easy on shared memory ethercards, and trivial
   on the Daynaport card where there is no choice of how to do it.
   The only complications are that the ring buffer wraps.
*/

static void dayna_memcpy_fromcard(struct net_device *dev, void *to, int from, int count)
{
	volatile unsigned short *ptr;
	unsigned short *target=to;
	from<<=1;	/* word, skip overhead */
	ptr=(unsigned short *)(dev->mem_start+from);
	/*
	 * Leading byte?
	 */
	if (from&2) {
		*((char *)target)++ = *(((char *)ptr++)-1);
		count--;
	}
	while(count>=2)
	{
		*target++=*ptr++;	/* Copy and */
		ptr++;			/* skip cruft */
		count-=2;
	}
	/*
	 *	Trailing byte ?
	 */
	if(count)
	{
		/* Big endian */
		unsigned short v=*ptr;
		*((char *)target)=v>>8;
	}
}

static void dayna_memcpy_tocard(struct net_device *dev, int to, const void *from, int count)
{
	volatile unsigned short *ptr;
	const unsigned short *src=from;
	to<<=1;	/* word, skip overhead */
	ptr=(unsigned short *)(dev->mem_start+to);
	/*
	 * Leading byte?
	 */
	if (to&2) { /* avoid a byte write (stomps on other data) */
		ptr[-1] = (ptr[-1]&0xFF00)|*((unsigned char *)src)++;
		ptr++;
		count--;
	}
	while(count>=2)
	{
		*ptr++=*src++;		/* Copy and */
		ptr++;			/* skip cruft */
		count-=2;
	}
	/*
	 *	Trailing byte ?
	 */
	if(count)
	{
		/* Big endian */
		unsigned short v=*src;
		/* card doesn't like byte writes */
		*ptr=(*ptr&0x00FF)|(v&0xFF00);
	}
}

static void dayna_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr, int ring_page)
{
	unsigned long hdr_start = (ring_page - WD_START_PG)<<8;
	dayna_memcpy_fromcard(dev, (void *)hdr, hdr_start, 4);
	/* Register endianism - fix here rather than 8390.c */
	hdr->count=(hdr->count&0xFF)<<8|(hdr->count>>8);
}

static void dayna_block_input(struct net_device *dev, int count, struct sk_buff *skb, int ring_offset)
{
	unsigned long xfer_base = ring_offset - (WD_START_PG<<8);
	unsigned long xfer_start = xfer_base+dev->mem_start;

	/*
	 *	Note the offset maths is done in card memory space which
	 *	is word per long onto our space.
	 */
	 
	if (xfer_start + count > dev->rmem_end) 
	{
		/* We must wrap the input move. */
		int semi_count = dev->rmem_end - xfer_start;
		dayna_memcpy_fromcard(dev, skb->data, xfer_base, semi_count);
		count -= semi_count;
		dayna_memcpy_fromcard(dev, skb->data + semi_count, 
			dev->rmem_start - dev->mem_start, count);
	}
	else
	{
		dayna_memcpy_fromcard(dev, skb->data, xfer_base, count);
	}
}

static void dayna_block_output(struct net_device *dev, int count, const unsigned char *buf,
				int start_page)
{
	long shmem = (start_page - WD_START_PG)<<8;
	
	dayna_memcpy_tocard(dev, shmem, buf, count);
}

/*
 *	Cards with full width memory
 */


static void sane_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr, int ring_page)
{
	unsigned long hdr_start = (ring_page - WD_START_PG)<<8;
	memcpy((void *)hdr, (char *)dev->mem_start+hdr_start, 4);
	/* Register endianism - fix here rather than 8390.c */
	hdr->count=(hdr->count&0xFF)<<8|(hdr->count>>8);
}

static void sane_block_input(struct net_device *dev, int count, struct sk_buff *skb, int ring_offset)
{
	unsigned long xfer_base = ring_offset - (WD_START_PG<<8);
	unsigned long xfer_start = xfer_base+dev->mem_start;

	if (xfer_start + count > dev->rmem_end) 
	{
		/* We must wrap the input move. */
		int semi_count = dev->rmem_end - xfer_start;
		memcpy(skb->data, (char *)dev->mem_start+xfer_base, semi_count);
		count -= semi_count;
		memcpy(skb->data + semi_count, 
			(char *)dev->rmem_start, count);
	}
	else
	{
		memcpy(skb->data, (char *)dev->mem_start+xfer_base, count);
	}
}


static void sane_block_output(struct net_device *dev, int count, const unsigned char *buf,
				int start_page)
{
	long shmem = (start_page - WD_START_PG)<<8;
	
	memcpy((char *)dev->mem_start+shmem, buf, count);
}

static void word_memcpy_tocard(void *tp, const void *fp, int count)
{
	volatile unsigned short *to = tp;
	const unsigned short *from = fp;
	
	count++;
	count/=2;
	
	while(count--)
		*to++=*from++;
}

static void word_memcpy_fromcard(void *tp, const void *fp, int count)
{
	unsigned short *to = tp;
	const volatile unsigned short *from = fp;
	
	count++;
	count/=2;
	
	while(count--)
		*to++=*from++;
}

static void slow_sane_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr, int ring_page)
{
	unsigned long hdr_start = (ring_page - WD_START_PG)<<8;
	word_memcpy_fromcard((void *)hdr, (char *)dev->mem_start+hdr_start, 4);
	/* Register endianism - fix here rather than 8390.c */
	hdr->count=(hdr->count&0xFF)<<8|(hdr->count>>8);
}

static void slow_sane_block_input(struct net_device *dev, int count, struct sk_buff *skb, int ring_offset)
{
	unsigned long xfer_base = ring_offset - (WD_START_PG<<8);
	unsigned long xfer_start = xfer_base+dev->mem_start;

	if (xfer_start + count > dev->rmem_end) 
	{
		/* We must wrap the input move. */
		int semi_count = dev->rmem_end - xfer_start;
		word_memcpy_fromcard(skb->data, (char *)dev->mem_start+xfer_base, semi_count);
		count -= semi_count;
		word_memcpy_fromcard(skb->data + semi_count, 
			(char *)dev->rmem_start, count);
	}
	else
	{
		word_memcpy_fromcard(skb->data, (char *)dev->mem_start+xfer_base, count);
	}
}

static void slow_sane_block_output(struct net_device *dev, int count, const unsigned char *buf,
				int start_page)
{
	long shmem = (start_page - WD_START_PG)<<8;
	
	word_memcpy_tocard((char *)dev->mem_start+shmem, buf, count);
#if 0
	long shmem = (start_page - WD_START_PG)<<8;
	volatile unsigned short *to=(unsigned short *)(dev->mem_start+shmem);
	volatile int p;
	unsigned short *bp=(unsigned short *)buf;
	
	count=(count+1)/2;
	
	while(count--)
	{
		*to++=*bp++;
		for(p=0;p<10;p++)
			p++;
	}
#endif	
}

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c daynaport.c"
 *  version-control: t
 *  c-basic-offset: 4
 *  tab-width: 4
 *  kept-new-versions: 5
 * End:
 */
