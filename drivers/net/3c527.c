/* 3c527.c: 3Com Etherlink/MC32 driver for Linux
 *
 *	(c) Copyright 1998 Red Hat Software Inc
 *	Written by Alan Cox.
 *	Further debugging by Carl Drougge.
 *
 *	Based on skeleton.c written 1993-94 by Donald Becker and ne2.c
 *	(for the MCA stuff) written by Wim Dumon.
 *
 *	Thanks to 3Com for making this possible by providing me with the
 *	documentation.
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU Public License, incorporated herein by reference.
 *
 */

static const char *version =
	"3c527.c:v0.08 2000/02/22 Alan Cox (alan@redhat.com)\n";

/**
 * DOC: Traps for the unwary
 *
 *	The diagram (Figure 1-1) and the POS summary disagree with the
 *	"Interrupt Level" section in the manual.
 *
 *	The documentation in places seems to miss things. In actual fact
 *	I've always eventually found everything is documented, it just
 *	requires careful study.
 *
 * DOC: Theory Of Operation
 *
 *	The 3com 3c527 is a 32bit MCA bus mastering adapter with a large
 *	amount of on board intelligence that housekeeps a somewhat dumber
 *	Intel NIC. For performance we want to keep the transmit queue deep
 *	as the card can transmit packets while fetching others from main
 *	memory by bus master DMA. Transmission and reception are driven by
 *	ring buffers. When updating the ring we are required to do some
 *	housekeeping work using the mailboxes and the command register.
 *
 *	The mailboxes provide a method for sending control requests to the
 *	card. The transmit mail box is used to update the transmit ring 
 *	pointers and the receive mail box to update the receive ring
 *	pointers. The exec mailbox allows a variety of commands to be
 *	executed. Each command must complete before the next is executed.
 *	Primarily we use the exec mailbox for controlling the multicast lists.
 *	We have to do a certain amount of interesting hoop jumping as the 
 *	multicast list changes can occur in interrupt state when the card
 *	has an exec command pending. We defer such events until the command
 *	completion interrupt.
 *
 *	The control register is used to pass status information. It tells us
 *	the transmit and receive status for packets and allows us to control
 *	the card operation mode. You must stop the card when emptying the
 *	receive ring, or you will race with the ring buffer and lose packets.
 */

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/mca.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include "3c527.h"

/*
 * The name of the card. Is used for messages and in the requests for
 * io regions, irqs and dma channels
 */
static const char* cardname = "3c527";

/* use 0 for production, 1 for verification, >2 for debug */
#ifndef NET_DEBUG
#define NET_DEBUG 2
#endif
static unsigned int mc32_debug = NET_DEBUG;

/* The number of low I/O ports used by the ethercard. */
#define NETCARD_IO_EXTENT	8


struct mc32_mailbox
{
	u16	mbox __attribute((packed));
	u16	data[1] __attribute((packed));
};

/* Information that need to be kept for each board. */

#define TX_RING_MAX	16	/* Typically the card supports 37 */
#define RX_RING_MAX	32	/*        "     "       "         */

struct mc32_local 
{
	struct net_device_stats net_stats;
	int slot;
	volatile struct mc32_mailbox *rx_box;
	volatile struct mc32_mailbox *tx_box;
	volatile struct mc32_mailbox *exec_box;
	volatile u16 *stats;
	u16 tx_chain;
	u16 rx_chain;
	u16 tx_len;
	u16 rx_len;
	u32 base;
	u16 rx_halted;
	u16 tx_halted;
	u16 rx_pending;
	u16 exec_pending;
	u16 mc_reload_wait;	/* a multicast load request is pending */
	atomic_t tx_count;		/* buffers left */
	wait_queue_head_t event;
	struct sk_buff *tx_skb[TX_RING_MAX];	/* Transmit ring */
	u16 tx_skb_top;
	u16 tx_skb_end;
	struct sk_buff *rx_skb[RX_RING_MAX];	/* Receive ring */
	void *rx_ptr[RX_RING_MAX];		/* Data pointers */
	u32 mc_list_valid;			/* True when the mclist is set */
};

/* The station (ethernet) address prefix, used for a sanity check. */
#define SA_ADDR0 0x02
#define SA_ADDR1 0x60
#define SA_ADDR2 0xAC

struct mca_adapters_t {
	unsigned int	id;
	char		*name;
};

static struct mca_adapters_t mc32_adapters[] __initdata = {
	{ 0x0041, "3COM EtherLink MC/32" },
	{ 0x8EF5, "IBM High Performance Lan Adapter" },
	{ 0x0000, NULL }
};


/* Index to functions, as function prototypes. */

extern int mc32_probe(struct net_device *dev);

static int	mc32_probe1(struct net_device *dev, int ioaddr);
static int	mc32_open(struct net_device *dev);
static void	mc32_timeout(struct net_device *dev);
static int	mc32_send_packet(struct sk_buff *skb, struct net_device *dev);
static void	mc32_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static int	mc32_close(struct net_device *dev);
static struct	net_device_stats *mc32_get_stats(struct net_device *dev);
static void	mc32_set_multicast_list(struct net_device *dev);
static void	mc32_reset_multicast_list(struct net_device *dev);


/**
 * mc32_probe:
 * @dev: device to probe
 *
 * Because MCA bus is a real bus and we can scan for cards we could do a
 * single scan for all boards here. Right now we use the passed in device
 * structure and scan for only one board. This needs fixing for modules
 * in paticular.
 */

int __init mc32_probe(struct net_device *dev)
{
	static int current_mca_slot = -1;
	int i;
	int adapter_found = 0;

	SET_MODULE_OWNER(dev);

	/* Do not check any supplied i/o locations. 
	   POS registers usually don't fail :) */

	/* MCA cards have POS registers.  
	   Autodetecting MCA cards is extremely simple. 
	   Just search for the card. */

	for(i = 0; (mc32_adapters[i].name != NULL) && !adapter_found; i++) {
		current_mca_slot = 
			mca_find_unused_adapter(mc32_adapters[i].id, 0);

		if((current_mca_slot != MCA_NOTFOUND) && !adapter_found) {
			if(!mc32_probe1(dev, current_mca_slot))
			{
				mca_set_adapter_name(current_mca_slot, 
						mc32_adapters[i].name);
				mca_mark_as_used(current_mca_slot);
				return 0;
			}
			
		}
	}
	return -ENODEV;
}

/**
 * mc32_probe1:
 * @dev:  Device structure to fill in
 * @slot: The MCA bus slot being used by this card
 *
 * Decode the slot data and configure the card structures. Having done this we
 * can reset the card and configure it. The card does a full self test cycle
 * in firmware so we have to wait for it to return and post us either a 
 * failure case or some addresses we use to find the board internals.
 */
 
static int __init mc32_probe1(struct net_device *dev, int slot)
{
	static unsigned version_printed = 0;
	int i;
	u8 POS;
	u32 base;
	struct mc32_local *lp;
	static u16 mca_io_bases[]={
		0x7280,0x7290,
		0x7680,0x7690,
		0x7A80,0x7A90,
		0x7E80,0x7E90
	};
	static u32 mca_mem_bases[]={
		0x00C0000,
		0x00C4000,
		0x00C8000,
		0x00CC000,
		0x00D0000,
		0x00D4000,
		0x00D8000,
		0x00DC000
	};
	static char *failures[]={
		"Processor instruction",
		"Processor data bus",
		"Processor data bus",
		"Processor data bus",
		"Adapter bus",
		"ROM checksum",
		"Base RAM",
		"Extended RAM",
		"82586 internal loopback",
		"82586 initialisation failure",
		"Adapter list configuration error"
	};
	
	/* Time to play MCA games */

	if (mc32_debug  &&  version_printed++ == 0)
		printk(KERN_DEBUG "%s", version);

	printk(KERN_INFO "%s: %s found in slot %d:", dev->name, cardname, slot);

	POS = mca_read_stored_pos(slot, 2);
	
	if(!(POS&1))
	{
		printk(" disabled.\n");
		return -ENODEV;
	}

	/* Fill in the 'dev' fields. */
	dev->base_addr = mca_io_bases[(POS>>1)&7];
	dev->mem_start = mca_mem_bases[(POS>>4)&7];
	
	POS = mca_read_stored_pos(slot, 4);
	if(!(POS&1))
	{
		printk("memory window disabled.\n");
		return -ENODEV;
	}

	POS = mca_read_stored_pos(slot, 5);
	
	i=(POS>>4)&3;
	if(i==3)
	{
		printk("invalid memory window.\n");
		return -ENODEV;
	}
	
	i*=16384;
	i+=16384;
	
	dev->mem_end=dev->mem_start + i;
	
	dev->irq = ((POS>>2)&3)+9;
	
	printk("io 0x%3lX irq %d mem 0x%lX (%dK)\n",
		dev->base_addr, dev->irq, dev->mem_start, i/1024);
	
	
	/* We ought to set the cache line size here.. */
	
	
	/*
	 *	Go PROM browsing
	 */
	 
	printk("%s: Address ", dev->name);
	 
	/* Retrieve and print the ethernet address. */
	for (i = 0; i < 6; i++)
	{
		mca_write_pos(slot, 6, i+12);
		mca_write_pos(slot, 7, 0);
	
		printk(" %2.2x", dev->dev_addr[i] = mca_read_pos(slot,3));
	}

	mca_write_pos(slot, 6, 0);
	mca_write_pos(slot, 7, 0);

	POS = mca_read_stored_pos(slot, 4);
	
	if(POS&2)
		printk(" : BNC port selected.\n");
	else 
		printk(" : AUI port selected.\n");
		
	POS=inb(dev->base_addr+HOST_CTRL);
	POS|=HOST_CTRL_ATTN|HOST_CTRL_RESET;
	POS&=~HOST_CTRL_INTE;
	outb(POS, dev->base_addr+HOST_CTRL);
	/* Reset adapter */
	udelay(100);
	/* Reset off */
	POS&=~(HOST_CTRL_ATTN|HOST_CTRL_RESET);
	outb(POS, dev->base_addr+HOST_CTRL);
	
	udelay(300);
	
	/*
	 *	Grab the IRQ
	 */

	i = request_irq(dev->irq, &mc32_interrupt, 0, dev->name, dev);
	if (i) {
		printk("%s: unable to get IRQ %d.\n", dev->name, dev->irq);
		return i;
	}

	/* Initialize the device structure. */
	dev->priv = kmalloc(sizeof(struct mc32_local), GFP_KERNEL);
	if (dev->priv == NULL)
	{
		free_irq(dev->irq, dev);
		return -ENOMEM;
	}

	memset(dev->priv, 0, sizeof(struct mc32_local));
	lp = dev->priv;
	lp->slot = slot;

	i=0;

	base = inb(dev->base_addr);
	
	while(base==0xFF)
	{
		i++;
		if(i==1000)
		{
			printk("%s: failed to boot adapter.\n", dev->name);
			free_irq(dev->irq, dev);
			return -ENODEV;
		}
		udelay(1000);
		if(inb(dev->base_addr+2)&(1<<5))
			base = inb(dev->base_addr);
	}

	if(base>0)
	{
		if(base < 0x0C)
			printk("%s: %s%s.\n", dev->name, failures[base-1],
				base<0x0A?" test failure":"");
		else
			printk("%s: unknown failure %d.\n", dev->name, base);
		free_irq(dev->irq, dev);
		return -ENODEV;
	}
	
	base=0;
	for(i=0;i<4;i++)
	{
		int n=0;
	
		while(!(inb(dev->base_addr+2)&(1<<5)))
		{
			n++;
			udelay(50);
			if(n>100)
			{
				printk(KERN_ERR "%s: mailbox read fail (%d).\n", dev->name, i);
				free_irq(dev->irq, dev);
				return -ENODEV;
			}
		}

		base|=(inb(dev->base_addr)<<(8*i));
	}
	
	lp->exec_box=bus_to_virt(dev->mem_start+base);
	
	base=lp->exec_box->data[1]<<16|lp->exec_box->data[0];
	
	lp->base = dev->mem_start+base;
	
	lp->rx_box=bus_to_virt(lp->base + lp->exec_box->data[2]);
	lp->tx_box=bus_to_virt(lp->base + lp->exec_box->data[3]);
	
	lp->stats = bus_to_virt(lp->base + lp->exec_box->data[5]);

	/*
	 *	Descriptor chains (card relative)
	 */
	 
	lp->tx_chain 		= lp->exec_box->data[8];
	lp->rx_chain 		= lp->exec_box->data[10];
	lp->tx_len 		= lp->exec_box->data[9];
	lp->rx_len 		= lp->exec_box->data[11];
	init_waitqueue_head(&lp->event);
	
	printk("%s: %d RX buffers, %d TX buffers. Base of 0x%08X.\n",
		dev->name, lp->rx_len, lp->tx_len, lp->base);
		
	if(lp->tx_len > TX_RING_MAX)
		lp->tx_len = TX_RING_MAX;
	
	dev->open		= mc32_open;
	dev->stop		= mc32_close;
	dev->hard_start_xmit	= mc32_send_packet;
	dev->get_stats		= mc32_get_stats;
	dev->set_multicast_list = mc32_set_multicast_list;
	dev->tx_timeout		= mc32_timeout;
	dev->watchdog_timeo	= HZ*5;	/* Board does all the work */
	
	lp->rx_halted		= 1;
	lp->tx_halted		= 1;
	lp->rx_pending		= 0;

	/* Fill in the fields of the device structure with ethernet values. */
	ether_setup(dev);
	return 0;
}


/**
 *	mc32_ring_poll:
 *	@dev:	The device to wait for
 *	
 *	Wait until a command we issues to the control register is completed.
 *	This actually takes very little time at all, which is fortunate as
 *	we often have to busy wait it.
 */
 
static void mc32_ring_poll(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	while(!(inb(ioaddr+HOST_STATUS)&HOST_STATUS_CRR));
}



/**
 *	mc32_command_nowait:
 *	@dev: The 3c527 to issue the command to
 *	@cmd: The command word to write to the mailbox
 *	@data: A data block if the command expects one
 *	@len: Length of the data block
 *
 *	Send a command from interrupt state. If there is a command currently
 *	being executed then we return an error of -1. It simply isnt viable
 *	to wait around as commands may be slow. Providing we get in then
 *	we send the command and busy wait for the board to acknowledge that
 *	a command request is pending. We do not wait for the command to 
 *	complete, just for the card to admit to noticing it.  
 */

static int mc32_command_nowait(struct net_device *dev, u16 cmd, void *data, int len)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	int ioaddr = dev->base_addr;
	
	if(lp->exec_pending)
		return -1;
		
	lp->exec_pending=3;
	lp->exec_box->mbox=0;
	lp->exec_box->mbox=cmd;
	memcpy((void *)lp->exec_box->data, data, len);
	barrier();	/* the memcpy forgot the volatile so be sure */

	/* Send the command */
	while(!(inb(ioaddr+HOST_STATUS)&HOST_STATUS_CRR));
	outb(1<<6, ioaddr+HOST_CMD);	
	return 0;
}


/**
 *	mc32_command: 
 *	@dev: The 3c527 card to issue the command to
 *	@cmd: The command word to write to the mailbox
 *	@data: A data block if the command expects one
 *	@len: Length of the data block
 *
 *	Sends exec commands in a user context. This permits us to wait around
 *	for the replies and also to wait for the command buffer to complete
 *	from a previous command before we execute our command. After our 
 *	command completes we will complete any pending multicast reload
 *	we blocked off by hogging the exec buffer.
 *
 *	You feed the card a command, you wait, it interrupts you get a 
 *	reply. All well and good. The complication arises because you use
 *	commands for filter list changes which come in at bh level from things
 *	like IPV6 group stuff.
 *
 *	We have a simple state machine
 *
 *	0	- nothing issued
 *
 *	1	- command issued, wait reply
 *
 *	2	- reply waiting - reader then goes to state 0
 *
 *	3	- command issued, trash reply. In which case the irq
 *		  takes it back to state 0
 *
 *	Send command and block for results. On completion spot and reissue
 *	multicasts
 */
  
static int mc32_command(struct net_device *dev, u16 cmd, void *data, int len)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	int ioaddr = dev->base_addr;
	unsigned long flags;
	int ret = 0;
	
	/*
	 *	Wait for a command
	 */
	 
	save_flags(flags);
	cli();
	 
	while(lp->exec_pending)
		sleep_on(&lp->event);
		
	/*
	 *	Issue mine
	 */

	lp->exec_pending=1;
	
	restore_flags(flags);
	
	lp->exec_box->mbox=0;
	lp->exec_box->mbox=cmd;
	memcpy((void *)lp->exec_box->data, data, len);
	barrier();	/* the memcpy forgot the volatile so be sure */

	/* Send the command */
	while(!(inb(ioaddr+HOST_STATUS)&HOST_STATUS_CRR));
	outb(1<<6, ioaddr+HOST_CMD);	
	
	save_flags(flags);
	cli();
	while(lp->exec_pending!=2)
		sleep_on(&lp->event);
	lp->exec_pending=0;
	restore_flags(flags);
	
	 
	if(lp->exec_box->data[0]&(1<<13))
		ret = -1;
	/*
	 *	A multicast set got blocked - do it now
	 */
		
	if(lp->mc_reload_wait)
		mc32_reset_multicast_list(dev);

	return ret;
}


/**
 *	mc32_rx_abort:
 *	@dev: 3c527 to abort
 *
 *	Peforms a receive abort sequence on the card. In fact after some
 *	experimenting we now simply tell the card to suspend reception. When
 *	issuing aborts occasionally odd things happened.
 */
 
static void mc32_rx_abort(struct net_device *dev)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	int ioaddr = dev->base_addr;

	while(!(inb(ioaddr+HOST_STATUS)&HOST_STATUS_CRR));
	
	lp->rx_box->mbox=0;
	outb(3<<3, ioaddr+HOST_CMD);	/* Suspend reception */
}

 
/**
 *	mc32_rx_begin:
 *	@dev: 3c527 to enable
 *
 *	We wait for any pending command to complete and then issue 
 *	a start reception command to the board itself. At this point 
 *	receive handling continues as it was before.
 */
 
static void mc32_rx_begin(struct net_device *dev)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	int ioaddr = dev->base_addr;
	
	while(!(inb(ioaddr+HOST_STATUS)&HOST_STATUS_CRR));
	
	lp->rx_box->mbox=0;
	outb(1<<3, ioaddr+HOST_CMD);	/* GO */
	mc32_ring_poll(dev);	
	
	lp->rx_halted=0;
	lp->rx_pending=0;
}

/**
 *	mc32_tx_abort:
 *	@dev: 3c527 to abort
 *
 *	Peforms a receive abort sequence on the card. In fact after some
 *	experimenting we now simply tell the card to suspend transmits . When
 *	issuing aborts occasionally odd things happened. In theory we want
 *	an abort to be sure we can recycle our buffers. As it happens we
 *	just have to be careful to shut the card down on close, and
 *	boot it carefully from scratch on setup.
 */
 
static void mc32_tx_abort(struct net_device *dev)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	int ioaddr = dev->base_addr;
	
	while(!(inb(ioaddr+HOST_STATUS)&HOST_STATUS_CRR));
	
	lp->tx_box->mbox=0;
	outb(3, ioaddr+HOST_CMD);	/* Suspend */
	
	/* Ring empty */
	
	atomic_set(&lp->tx_count, lp->tx_len);
	
	/* Flush */
	if(lp->tx_skb_top!=lp->tx_skb_end)
	{
		int i;
		if(lp->tx_skb_top<=lp->tx_skb_end)
		{
			for(i=lp->tx_skb_top;i<lp->tx_skb_end;i++)
			{
				dev_kfree_skb(lp->tx_skb[i]);
				lp->tx_skb[i]=NULL;
			}
		}
		else
		{
			for(i=lp->tx_skb_end;i<TX_RING_MAX;i++)
			{
				dev_kfree_skb(lp->tx_skb[i]);
				lp->tx_skb[i]=NULL;
			}
			for(i=0;i<lp->tx_skb_top;i++)
			{
				dev_kfree_skb(lp->tx_skb[i]);
				lp->tx_skb[i]=NULL;
			}
		}
	}
	lp->tx_skb_top=lp->tx_skb_end=0;
}

/**
 *	mc32_tx_begin:
 *	@dev: 3c527 to enable
 *
 *	We wait for any pending command to complete and then issue 
 *	a start transmit command to the board itself. At this point 
 *	transmit handling continues as it was before. The ring must
 *	be setup before you do this and must have an end marker in it.
 *	It turns out we can avoid issuing this specific command when
 *	doing our setup so we avoid it.
 */
 
static void mc32_tx_begin(struct net_device *dev)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	int ioaddr = dev->base_addr;
	
	while(!(inb(ioaddr+HOST_STATUS)&HOST_STATUS_CRR));
	
	lp->tx_box->mbox=0;
#if 0	
	outb(5, ioaddr+HOST_CMD);	/* GO */
	printk("TX=>5\n");
	mc32_ring_poll(dev);	
	if(lp->tx_box->mbox&(1<<13))
		printk("TX begin error!\n");
#endif		
	lp->tx_halted=0;
}

	
/**
 *	mc32_load_rx_ring:
 *	@dev: 3c527 to build the ring for
 *
 *	The card setups up the receive ring for us. We are required to
 *	use the ring it provides although we can change the size of the
 *	ring.
 *
 *	We allocate an sk_buff for each ring entry in turn and set the entry
 *	up for a single non s/g buffer. The first buffer we mark with the
 *	end marker bits. Finally we clear the rx mailbox.
 */
 
static int mc32_load_rx_ring(struct net_device *dev)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	int i;
	u16 base;
	volatile struct skb_header *p;
	
	base = lp->rx_box->data[0];
	
	/* Fix me - should use card size - also fix flush ! */ 

	for(i=0;i<RX_RING_MAX;i++)
	{
		lp->rx_skb[i]=alloc_skb(1532, GFP_KERNEL);
		if(lp->rx_skb[i]==NULL)
		{
			for(;i>=0;i--)
				kfree_skb(lp->rx_skb[i]);
			return -ENOBUFS;
		}
		lp->rx_ptr[i]=lp->rx_skb[i]->data+18;
		
		p=bus_to_virt(lp->base+base);
		p->control=0;
		p->data = virt_to_bus(lp->rx_ptr[i]);
		p->status=0;
		p->length = 1532;
		base = p->next;
	}
	p->control = (1<<6);
	lp->rx_box->mbox = 0;
	return 0;
}	

/**
 *	mc32_flush_rx_ring:
 *	@lp: Local data of 3c527 to flush the rx ring of
 *
 *	Free the buffer for each ring slot. Because of the receive 
 *	algorithm we use the ring will always be loaded will a full set
 *	of buffers.
 */

static void mc32_flush_rx_ring(struct mc32_local *lp)
{
	int i;
	for(i=0;i<RX_RING_MAX;i++)
		kfree_skb(lp->rx_skb[i]);
}

/**
 *	mc32_flush_tx_ring:
 *	@lp: Local data of 3c527 to flush the tx ring of
 *
 *	We have to consider two cases here. We want to free the pending
 *	buffers only. If the ring buffer head is past the start then the
 *	ring segment we wish to free wraps through zero.
 */

static void mc32_flush_tx_ring(struct mc32_local *lp)
{
	int i;
	
	if(lp->tx_skb_top <= lp->tx_skb_end)
	{
		for(i=lp->tx_skb_top;i<lp->tx_skb_end;i++)
			dev_kfree_skb(lp->tx_skb[i]);
	}
	else
	{
		for(i=0;i<lp->tx_skb_end;i++)
			dev_kfree_skb(lp->tx_skb[i]);
		for(i=lp->tx_skb_top;i<TX_RING_MAX;i++)
			dev_kfree_skb(lp->tx_skb[i]);
	}
}
 	
/**
 *	mc32_open
 *	@dev: device to open
 *
 *	The user is trying to bring the card into ready state. This requires
 *	a brief dialogue with the card. Firstly we enable interrupts and then
 *	'indications'. Without these enabled the card doesn't bother telling
 *	us what it has done. This had me puzzled for a week.
 *
 *	We then load the network address and multicast filters. Turn on the
 *	workaround mode. This works around a bug in the 82586 - it asks the
 *	firmware to do so. It has a performance hit but is needed on busy
 *	[read most] lans. We load the ring with buffers then we kick it
 *	all off.
 */

static int mc32_open(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	u16 zero_word=0;
	u8 one=1;
	u8 regs;
	
	/*
	 *	Interrupts enabled
	 */

	regs=inb(ioaddr+HOST_CTRL);
	regs|=HOST_CTRL_INTE;
	outb(regs, ioaddr+HOST_CTRL);
	

	/*
	 *	Send the indications on command
	 */

	mc32_command(dev, 4, &one, 2);

	 	
	/*
	 *	Send the command sequence "abort, resume" for RX and TX.
	 *	The abort cleans up the buffer chains if needed.
	 */

	mc32_rx_abort(dev);
	mc32_tx_abort(dev);
	
	/* Set Network Address */
	mc32_command(dev, 1, dev->dev_addr, 6);
	
	/* Set the filters */
	mc32_set_multicast_list(dev);
	
	/* Issue the 82586 workaround command - this is for "busy lans",
	   but basically means for all lans now days - has a performance
	   cost but best set */
	   
	mc32_command(dev, 0x0D, &zero_word, 2); /* 82586 bug workaround on */
	
	/* Load the ring we just initialised */
	
	if(mc32_load_rx_ring(dev))
	{
		mc32_close(dev);
		return -ENOBUFS;
	}
	
	/* And the resume command goes last */
	
	mc32_rx_begin(dev);
	mc32_tx_begin(dev);

	netif_start_queue(dev);	
	return 0;
}

/**
 *	mc32_timeout:
 *	@dev: 3c527 that timed out
 *
 *	Handle a timeout on transmit from the 3c527. This normally means
 *	bad things as the hardware handles cable timeouts and mess for
 *	us.
 *
 */

static void mc32_timeout(struct net_device *dev)
{
	printk(KERN_WARNING "%s: transmit timed out?\n", dev->name);
	/* Try to restart the adaptor. */
	netif_wake_queue(dev);
}
 
/**
 *	mc32_send_packet:
 *	@skb: buffer to transmit
 *	@dev: 3c527 to send it out of
 *
 *	Transmit a buffer. This normally means throwing the buffer onto
 *	the transmit queue as the queue is quite large. If the queue is
 *	full then we set tx_busy and return. Once the interrupt handler
 *	gets messages telling it to reclaim transmit queue entries we will
 *	clear tx_busy and the kernel will start calling this again.
 *
 *	We use cli rather than spinlocks. Since I have no access to an SMP
 *	MCA machine I don't plan to change it. It is probably the top 
 *	performance hit for this driver on SMP however.
 */
 
static int mc32_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	int ioaddr = dev->base_addr;
	unsigned long flags;
		
	u16 tx_head;
	volatile struct skb_header *p, *np;

	netif_stop_queue(dev);
	
	save_flags(flags);
	cli();
		
	if(atomic_read(&lp->tx_count)==0)
	{
		restore_flags(flags);
		return 1;
	}

	tx_head = lp->tx_box->data[0];
	atomic_dec(&lp->tx_count);
	/* We will need this to flush the buffer out */
	
	lp->tx_skb[lp->tx_skb_end] = skb;
	lp->tx_skb_end++;
	lp->tx_skb_end&=(TX_RING_MAX-1);

	/* TX suspend - shouldnt be needed but apparently is.
	   This is a research item ... */
		   
	while(!(inb(ioaddr+HOST_STATUS)&HOST_STATUS_CRR));
	lp->tx_box->mbox=0;
	outb(3, ioaddr+HOST_CMD);
	
	/* Transmit now stopped */

	/* P is the last sending/sent buffer as a pointer */
	p=(struct skb_header *)bus_to_virt(lp->base+tx_head);
	
	/* NP is the buffer we will be loading */
	np=(struct skb_header *)bus_to_virt(lp->base+p->next);
		
	np->control	|= (1<<6);	/* EOL */
	wmb();
		
	np->length	= skb->len;
		
	if(np->length < 60)
		np->length = 60;
			
	np->data	= virt_to_bus(skb->data);
	np->status	= 0;
	np->control	= (1<<7)|(1<<6);	/* EOP EOL */
	wmb();
		
	p->status	= 0;
	p->control	&= ~(1<<6);
	
	while(!(inb(ioaddr+HOST_STATUS)&HOST_STATUS_CRR));
	lp->tx_box->mbox=0;
	outb(5, ioaddr+HOST_CMD);		/* Restart TX */
	restore_flags(flags);
	
	netif_wake_queue(dev);
	return 0;
}

/**
 *	mc32_update_stats:
 *	@dev: 3c527 to service
 *
 *	When the board signals us that its statistics need attention we
 *	should query the table and clear it. In actual fact we currently
 *	track all our statistics in software and I haven't implemented it yet.
 */
 
static void mc32_update_stats(struct net_device *dev)
{
}

/**
 *	mc32_rx_ring:
 *	@dev: 3c527 that needs its receive ring processing
 *
 *	We have received one or more indications from the card that
 *	a receive has completed. The ring buffer thus contains dirty
 *	entries. Firstly we tell the card to stop receiving, then We walk 
 *	the ring from the first filled entry, which is pointed to by the 
 *	card rx mailbox and for each completed packet we will either copy 
 *	it and pass it up the stack or if the packet is near MTU sized we 
 *	allocate another buffer and flip the old one up the stack.
 *
 *	We must succeed in keeping a buffer on the ring. If neccessary we
 *	will toss a received packet rather than lose a ring entry. Once the
 *	first packet that is unused is found we reload the mailbox with the
 *	buffer so that the card knows it can use the buffers again. Finally
 *	we set it receiving again. 
 *
 *	We must stop reception during the ring walk. I thought it would be
 *	neat to avoid it by clever tricks, but it turns out the event order
 *	on the card means you have to play by the manual.
 */
 
static void mc32_rx_ring(struct net_device *dev)
{
	struct mc32_local *lp=dev->priv;
	int ioaddr = dev->base_addr;
	int x=0;
	volatile struct skb_header *p;
	u16 base;
	u16 top;

	/* Halt RX before walking the ring */
	
	while(!(inb(ioaddr+HOST_STATUS)&HOST_STATUS_CRR));
	outb(3<<3, ioaddr+HOST_CMD);
	while(inb(ioaddr+HOST_STATUS)&HOST_STATUS_CRR);
	
	top = base = lp->rx_box->data[0];
	do
	{
		p=(struct skb_header *)bus_to_virt(base+lp->base);
		if(!(p->status & (1<<7)))
			break;
		if(p->status & (1<<6))
		{
			u16 length = p->length;
			struct sk_buff *skb=dev_alloc_skb(length+2);
			if(skb!=NULL)
			{
				skb_reserve(skb,2);
				/*printk("Frame at %p\n", bus_to_virt(p->data)); */
				memcpy(skb_put(skb, length),
					bus_to_virt(p->data), length);
				skb->protocol=eth_type_trans(skb,dev);
				skb->dev=dev;
				lp->net_stats.rx_packets++;
				lp->net_stats.rx_bytes+=skb->len;
				netif_rx(skb);
			}
			else
				lp->net_stats.rx_dropped++;
		}
		else
		{
			lp->net_stats.rx_errors++;
			switch(p->status&0x0F)
			{
				case 1:
					lp->net_stats.rx_crc_errors++;break;
				case 2:
					lp->net_stats.rx_fifo_errors++;break;
				case 3:
					lp->net_stats.rx_frame_errors++;break;
				case 4:
					lp->net_stats.rx_missed_errors++;break;
				case 5:
					lp->net_stats.rx_length_errors++;break;
			}
		}
		p->length = 1532;
		p->control &= ~(1<<6);
		p->status = 0;
		base = p->next;
	}
	while(x++<48);

	/*
	 *	Restart ring processing
	 */	
	
	while(!(inb(ioaddr+HOST_STATUS)&HOST_STATUS_CRR));
	lp->rx_box->mbox=0;
	lp->rx_box->data[0] = top;
	outb(1<<3, ioaddr+HOST_CMD);	
	lp->rx_halted=0;
}


/**
 *	mc32_interrupt:
 *	@irq: Interrupt number
 *	@dev_id: 3c527 that requires servicing
 *	@regs: Registers (unused)
 *
 *	The 3c527 interrupts us for four reasons. The command register 
 *	contains the message it wishes to send us packed into a single
 *	byte field. We keep reading status entries until we have processed
 *	all the transmit and control items, but simply count receive
 *	reports. When the receive reports are in we can call the mc32_rx_ring
 *	and empty the ring. This saves the overhead of multiple command requests
 */
 
static void mc32_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	struct net_device *dev = dev_id;
	struct mc32_local *lp;
	int ioaddr, status, boguscount = 0;
	
	if (dev == NULL) {
		printk(KERN_WARNING "%s: irq %d for unknown device.\n", cardname, irq);
		return;
	}
	ioaddr = dev->base_addr;
	lp = (struct mc32_local *)dev->priv;

	/* See whats cooking */
	
	while((inb(ioaddr+2)&(1<<5)) && boguscount++<2000)
	{
		status=inb(ioaddr+HOST_CMD);

#ifdef DEBUG_IRQ		
		printk("Status TX%d RX%d EX%d OV%d\n",
			(status&7), (status>>3)&7, (status>>6)&1,
			(status>>7)&1);
#endif
			
		switch(status&7)
		{
			case 0:
				break;
			case 6: /* TX fail */
				lp->net_stats.tx_errors++;
			case 2:	/* TX ok */
				lp->net_stats.tx_packets++;
				/* Packets are sent in order - this is
				   basically a FIFO queue of buffers matching
				   the card ring */
				lp->net_stats.tx_bytes+=lp->tx_skb[lp->tx_skb_top]->len;
				dev_kfree_skb_irq(lp->tx_skb[lp->tx_skb_top]);
				lp->tx_skb[lp->tx_skb_top]=NULL;
				lp->tx_skb_top++;
				lp->tx_skb_top&=(TX_RING_MAX-1);
				atomic_inc(&lp->tx_count);
				netif_wake_queue(dev);
				break;
			case 3: /* Halt */
			case 4: /* Abort */
				lp->tx_halted=1;
				wake_up(&lp->event);
				break;
			case 5:
				lp->tx_halted=0;
				wake_up(&lp->event);
				break;
			default:
				printk("%s: strange tx ack %d\n", 
					dev->name, status&7);
		}
		status>>=3;
		switch(status&7)
		{
			case 0:
				break;
			case 2:	/* RX */
				lp->rx_pending=1;
				if(!lp->rx_halted)
				{
					/*
					 *	Halt ring receive
					 */
					while(!(inb(ioaddr+HOST_STATUS)&HOST_STATUS_CRR));
					outb(3<<3, ioaddr+HOST_CMD);
				}
				break;
			case 3:
			case 4:
				lp->rx_halted=1;
				wake_up(&lp->event);
				break;
			case 5:
				lp->rx_halted=0;
				wake_up(&lp->event);
				break;
			case 6:
				/* Out of RX buffers stat */
				lp->net_stats.rx_dropped++;
				lp->rx_pending=1;
				/* Must restart */
				lp->rx_halted=1;
				break;
			default:
				printk("%s: strange rx ack %d\n", 
					dev->name, status&7);
			
		}
		status>>=3;
		if(status&1)
		{
			/* 0=no 1=yes 2=replied, get cmd, 3 = wait reply & dump it */
			if(lp->exec_pending!=3)
				lp->exec_pending=2;
			else
				lp->exec_pending=0;
			wake_up(&lp->event);
		}
		if(status&2)
		{
			/*
			 *	Update the stats as soon as
			 *	we have it flagged and can 
			 *	send an immediate reply (CRR set)
			 */
			 
			if(inb(ioaddr+HOST_STATUS)&HOST_STATUS_CRR)
			{
				mc32_update_stats(dev);
				outb(0, ioaddr+HOST_CMD);
			}
		}
	}
	
	/*
	 *	Process and restart the receive ring. This has some state
	 *	as we must halt the ring to process it and halting the ring
	 *	might not occur in the same IRQ handling loop as we issue
	 *	the halt.
	 */
	 
	if(lp->rx_pending && lp->rx_halted)
	{
		mc32_rx_ring(dev);
		lp->rx_pending = 0;
	}
	return;
}


/**
 *	mc32_close:
 *	@dev: 3c527 card to shut down
 *
 *	The 3c527 is a bus mastering device. We must be careful how we
 *	shut it down. It may also be running shared interrupt so we have
 *	to be sure to silence it properly
 *
 *	We abort any receive and transmits going on and then wait until
 *	any pending exec commands have completed in other code threads.
 *	In theory we can't get here while that is true, in practice I am
 *	paranoid
 *
 *	We turn off the interrupt enable for the board to be sure it can't
 *	intefere with other devices.
 */
 
static int mc32_close(struct net_device *dev)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	int ioaddr = dev->base_addr;
	u8 regs;
	u16 one=1;

	netif_stop_queue(dev);
	
	/*
	 *	Send the indications on command (handy debug check)
	 */

	mc32_command(dev, 4, &one, 2);

	/* Abort RX and Abort TX */
	
	mc32_rx_abort(dev);	
	mc32_tx_abort(dev);
	
	/* Catch any waiting commands */
	
	while(lp->exec_pending==1)
		sleep_on(&lp->event);
		
	/* Ok the card is now stopping */	
	
	regs=inb(ioaddr+HOST_CTRL);
	regs&=~HOST_CTRL_INTE;
	outb(regs, ioaddr+HOST_CTRL);

	mc32_flush_rx_ring(lp);
	mc32_flush_tx_ring(lp);
	
	/* Update the statistics here. */

	return 0;

}

/**
 *	mc32_get_stats:
 *	@dev: The 3c527 card to handle
 *
 *	As we currently handle our statistics in software this one is
 *	easy to handle. With hardware statistics it will get messy
 *	as the get_stats call will need to send exec mailbox messages and
 *	need to lock out the multicast reloads.
 */

static struct net_device_stats *mc32_get_stats(struct net_device *dev)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	return &lp->net_stats;
}

/**
 *	do_mc32_set_multicast_list:
 *	@dev: 3c527 device to load the list on
 *	@retry: indicates this is not the first call. 
 *
 * Actually set or clear the multicast filter for this adaptor. The locking
 * issues are handled by this routine. We have to track state as it may take
 * multiple calls to get the command sequence completed. We just keep trying
 * to schedule the loads until we manage to process them all.
 *
 * num_addrs == -1	Promiscuous mode, receive all packets
 *
 * num_addrs == 0	Normal mode, clear multicast list
 *
 * num_addrs > 0	Multicast mode, receive normal and MC packets,
 *			and do best-effort filtering.
 */

static void do_mc32_set_multicast_list(struct net_device *dev, int retry)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	u16 filt;

	if (dev->flags&IFF_PROMISC)
		/* Enable promiscuous mode */
		filt = 1;
	else if((dev->flags&IFF_ALLMULTI) || dev->mc_count > 10)
	{
		dev->flags|=IFF_PROMISC;
		filt = 1;
	}
	else if(dev->mc_count)
	{
		unsigned char block[62];
		unsigned char *bp;
		struct dev_mc_list *dmc=dev->mc_list;
		
		int i;
		
		filt = 0;
		
		if(retry==0)
			lp->mc_list_valid = 0;
		if(!lp->mc_list_valid)
		{
			block[1]=0;
			block[0]=dev->mc_count;
			bp=block+2;
		
			for(i=0;i<dev->mc_count;i++)
			{
				memcpy(bp, dmc->dmi_addr, 6);
				bp+=6;
				dmc=dmc->next;
			}
			if(mc32_command_nowait(dev, 2, block, 2+6*dev->mc_count)==-1)
			{
				lp->mc_reload_wait = 1;
				return;
			}
			lp->mc_list_valid=1;
		}
	}
	else 
	{
		filt = 0;
	}
	if(mc32_command_nowait(dev, 0, &filt, 2)==-1)
	{
		lp->mc_reload_wait = 1;
	}
}

/**
 *	mc32_set_multicast_list:
 *	@dev: The 3c527 to use
 *
 *	Commence loading the multicast list. This is called when the kernel
 *	changes the lists. It will override any pending list we are trying to
 *	load.
 */
 
static void mc32_set_multicast_list(struct net_device *dev)
{
	do_mc32_set_multicast_list(dev,0);
}

/**
 *	mc32_reset_multicast_list:
 *	@dev: The 3c527 to use
 *
 *	Attempt the next step in loading the multicast lists. If this attempt
 *	fails to complete then it will be scheduled and this function called
 *	again later from elsewhere.
 */
 

static void mc32_reset_multicast_list(struct net_device *dev)
{
	do_mc32_set_multicast_list(dev,1);
}

#ifdef MODULE

static struct net_device this_device;


/**
 *	init_module:
 *
 *	Probe and locate a 3c527 card. This really should probe and locate
 *	all the 3c527 cards in the machine not just one of them. Yes you can
 *	insmod multiple modules for now but its a hack.
 */
 
int init_module(void)
{
	int result;

	this_device.init = mc32_probe;
	if ((result = register_netdev(&this_device)) != 0)
		return result;

	return 0;
}

/**
 *	cleanup_module:
 *
 *	Unloading time. We release the MCA bus resources and the interrupt
 *	at which point everything is ready to unload. The card must be stopped
 *	at this point or we would not have been called. When we unload we
 *	leave the card stopped but not totally shut down. When the card is
 *	initialized it must be rebooted or the rings reloaded before any
 *	transmit operations are allowed to start scribbling into memory.
 */
 
void cleanup_module(void)
{
	int slot;
	
	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	unregister_netdev(&this_device);

	/*
	 * If we don't do this, we can't re-insmod it later.
	 */
	 
	if (this_device.priv)
	{
		struct mc32_local *lp=this_device.priv;
		slot = lp->slot;
		mca_mark_as_unused(slot);
		mca_set_adapter_name(slot, NULL);
		kfree(this_device.priv);
	}
	free_irq(this_device.irq, &this_device);
}

#endif /* MODULE */
