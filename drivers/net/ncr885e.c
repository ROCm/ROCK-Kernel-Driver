/*
 *  An Ethernet driver for the dual-function NCR 53C885 SCSI/Ethernet
 *  controller.
 *
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

static const char *version =
"ncr885e.c:v1.0 02/10/00 dan@synergymicro.com, cort@fsmlabs.com\n";

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/dbdma.h>
#include <asm/uaccess.h>

#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include "ncr885e.h"
#include "ncr885_debug.h"

static const char *chipname = "ncr885e";

#define NCR885E_DEBUG   0

/* The 885's Ethernet PCI device id. */
#ifndef PCI_DEVICE_ID_NCR_53C885_ETHERNET
#define PCI_DEVICE_ID_NCR_53C885_ETHERNET  0x0701
#endif

#define NR_RX_RING    8
#define NR_TX_RING    8
#define MAX_TX_ACTIVE (NR_TX_RING-1)
#define NCMDS_TX      NR_TX_RING

#define RX_BUFLEN     (ETH_FRAME_LEN + 8)
#define TX_TIMEOUT    5*HZ

#define NCR885E_TOTAL_SIZE 0xe0

#define TXSR          (1<<6)   /* tx: xfer status written */
#define TXABORT       (1<<7)   /* tx: abort */
#define EOP           (1<<7)   /* rx: end of packet written to buffer */

int ncr885e_debug = NCR885E_DEBUG;
static int print_version = 0;

struct ncr885e_private {

	/* preserve a 1-1 marking with buffs */
	struct dbdma_cmd *head;
	struct dbdma_cmd *tx_cmds;
	struct dbdma_cmd *rx_cmds;
	struct dbdma_cmd *stop_cmd;

	struct sk_buff *tx_skbufs[NR_TX_RING];
	struct sk_buff *rx_skbufs[NR_RX_RING];

	int rx_current;
	int rx_dirty;

	int tx_dirty;
	int tx_current;

	unsigned short tx_status[NR_TX_RING];

	unsigned char tx_fullup;
	unsigned char tx_active;
  
	struct net_device_stats  stats;

	struct net_device *dev;

	struct timer_list tx_timeout;
	int timeout_active;

	spinlock_t lock;
};

static struct net_device *root_dev = NULL;

static int ncr885e_open( struct net_device *dev );
static int ncr885e_close( struct net_device *dev );
static void ncr885e_rx( struct net_device *dev );
static void ncr885e_tx( struct net_device *dev );
static int ncr885e_probe1( unsigned long ioaddr, unsigned char irq );
static int ncr885e_xmit_start( struct sk_buff *skb, struct net_device *dev );
static struct net_device_stats *ncr885e_stats( struct net_device *dev );
static void ncr885e_set_multicast( struct net_device *dev );
static void ncr885e_config( struct net_device *dev );
static int ncr885e_set_address( struct net_device *dev, void *addr );
static void ncr885e_interrupt( int irq, void *dev_id, struct pt_regs *regs );
static void show_dbdma_cmd( volatile struct dbdma_cmd *cmd );
#if 0
static int read_eeprom( unsigned int ioadddr, int location );
#endif

#ifdef NCR885E_DEBUG_MII
static void show_mii( unsigned long ioaddr );
static int read_mii( unsigned long ioaddr, int reg );
static void write_mii( unsigned long ioaddr, int reg, int data );
#endif /* NCR885E_DEBUG_MII */

#define TX_RESET_FLAGS    (TX_CHANNEL_RUN|TX_CHANNEL_PAUSE|TX_CHANNEL_WAKE)
#define RX_RESET_FLAGS    (RX_CHANNEL_RUN|RX_CHANNEL_PAUSE|RX_CHANNEL_WAKE)


static struct pci_device_id ncr885e_pci_tbl[] __initdata = {
	{ PCI_VENDOR_ID_NCR, PCI_DEVICE_ID_NCR_53C885_ETHERNET, PCI_ANY_ID, PCI_ANY_ID, },
	{ }			/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, ncr885e_pci_tbl);

#if 0
static int
debug_ioctl( struct net_device *dev, struct ifreq *req, int cmd )
{
	unsigned long ioaddr = dev->base_addr;
	struct ncr885e_private *sp = (struct ncr885e_private *) dev->priv;
	struct ncr885e_private *data;
	struct ncr885e_regs *regs;
	unsigned long flags;

	union {
		struct ncr885e_regs dump;
		struct ncr885e_private priv;    
	} temp;

	switch( cmd ) {

		/* dump the rx ring status */
	case NCR885E_GET_PRIV:

		data = (struct ncr885e_private *) &req->ifr_data;    

		if ( verify_area(VERIFY_WRITE, &req->ifr_data,
				 sizeof( struct ncr885e_private )))
			return -EFAULT;

		memcpy((char *) &temp.priv, sp, sizeof( struct ncr885e_private ));
		copy_to_user( data, (char *) &temp.priv, sizeof( struct ncr885e_private));
		break;

	case NCR885E_GET_REGS:

		regs = (struct ncr885e_regs *) &req->ifr_data;
  
		if ( verify_area( VERIFY_WRITE, &req->ifr_data,
				  sizeof( struct ncr885e_regs )))
			return -EFAULT;

		spin_lock_irqsave( &sp->lock, flags ); 

		temp.dump.tx_status = inl( ioaddr + TX_CHANNEL_STATUS );
		temp.dump.rx_status = inl( ioaddr + RX_CHANNEL_STATUS );
		temp.dump.mac_config = inl( ioaddr + MAC_CONFIG );
		temp.dump.tx_control = inl( ioaddr + TX_CHANNEL_CONTROL );
		temp.dump.rx_control = inl( ioaddr + RX_CHANNEL_CONTROL );
		temp.dump.tx_cmd_ptr = inl( ioaddr + TX_CMD_PTR_LO );
		temp.dump.rx_cmd_ptr = inl( ioaddr + RX_CMD_PTR_LO );
		temp.dump.int_status = inl( ioaddr + INTERRUPT_STATUS_REG );

		spin_unlock_irqrestore( &sp->lock, flags );
		copy_to_user( regs, (char *) &temp.dump, sizeof( struct ncr885e_regs ));

		break;

	default:
		return -EOPNOTSUPP;
	}
	return 0;
}
#endif

/*  Enable interrupts on the 53C885 */
static inline void
ncr885e_enable( struct net_device *dev )

{
	unsigned long ioaddr = dev->base_addr;
	unsigned short reg;

	reg = inw(ioaddr + INTERRUPT_ENABLE);
	outw(reg | INTERRUPT_INTE, ioaddr + INTERRUPT_ENABLE);
}

/*  Disable interrupts on the 53c885 */
static inline void
ncr885e_disable( struct net_device *dev )

{
	unsigned long ioaddr = dev->base_addr;
	unsigned short reg;

	reg = inw( ioaddr + INTERRUPT_ENABLE );
	outw( reg & ~INTERRUPT_INTE, ioaddr + INTERRUPT_ENABLE );
}


static inline void
ncr885e_reset( struct net_device *dev )

{
	unsigned short reg;  
	unsigned long cntl;
	int i;
	unsigned long ioaddr = dev->base_addr;

	if (ncr885e_debug > 1)
		printk( KERN_INFO "%s: Resetting 53C885...\n", dev->name );

	/* disable interrupts on the 53C885 */
	ncr885e_disable( dev );
  
	/* disable rx in the MAC */
	reg = inw( ioaddr + MAC_CONFIG );
	outw( reg & ~MAC_CONFIG_RXEN, ioaddr + MAC_CONFIG );
  
	for( i=0; i < 100; i++ ) {

		if ( !(inw( ioaddr + MAC_CONFIG ) & MAC_CONFIG_RXEN ))
			break;
		udelay( 10 );
	}
  
	reg = inw( ioaddr + MAC_CONFIG );
	outw( reg | MAC_CONFIG_SRST, ioaddr + MAC_CONFIG );
	outw( reg, ioaddr + MAC_CONFIG );

	/* disable both rx and tx DBDMA channels */
	outl( TX_DBDMA_ENABLE << 16, ioaddr + TX_CHANNEL_CONTROL );
	outl( RX_DBDMA_ENABLE << 16, ioaddr + RX_CHANNEL_CONTROL );

	for( i=0; i < 100; i++ ) {
    
		if ( !(inw( ioaddr + TX_CHANNEL_STATUS ) & TX_DBDMA_ENABLE ) &&
		     !(inw( ioaddr + RX_CHANNEL_STATUS ) & RX_DBDMA_ENABLE ))
			break;
		udelay( 10 );
	}

	/* perform a "software reset" */
	cntl = inl( ioaddr + DBDMA_CONTROL );
	outl( cntl | DBDMA_SRST, ioaddr + DBDMA_CONTROL );

	for( i=0; i < 100; i++ ) {
  
		if ( !(inl( ioaddr + DBDMA_CONTROL ) & DBDMA_SRST ))
			break;
		udelay( 10 );
	}

	/* books says that a software reset should be done to the MAC, as
	   well.  This true??? */

	if (ncr885e_debug > 3) 
		printk( KERN_INFO "%s: reset complete\n", dev->name );

}


/*  configure the 53C885 chip.

    The DBDMA command descriptors on the 53C885 can be programmed to
    branch, interrupt or pause conditionally or always by using the
    interrupt, branch and wait select registers.  */

static void
ncr885e_config( struct net_device *dev )

{
	unsigned long ioaddr = dev->base_addr;

	if (ncr885e_debug > 3)
		printk( KERN_INFO "%s: Configuring 53C885.\n", dev->name );

	ncr885e_reset( dev );
 
	/* The 53C885 can be programmed to perform conditional DBDMA
	   branches, interrupts or waits.
  
	   Neither channel makes use of "wait", as it requires that the
	   DBDMA engine to be restarted.  Don't go there.  The rx channel
	   will branch upon the successful reception of a packet ('EOP' in
	   the xfer_status field).  The branch address is to the STOP
	   DBDMA command descriptor, which shuts down the rx channel until
	   the interrupt is serviced.   */
     
	/* cause tx channel to stop after "status received" */
	outl( 0, ioaddr + TX_INT_SELECT );
	outl( (TX_WAIT_STAT_RECV << 16) | TX_WAIT_STAT_RECV, 
	      ioaddr + TX_WAIT_SELECT );
	outl( 0, ioaddr + TX_BRANCH_SELECT );

	/* cause rx channel to branch to the STOP descriptor on "End-of-Packet" */
#if 0
	outl( (RX_INT_SELECT_EOP << 16) | RX_INT_SELECT_EOP,
	      ioaddr + RX_INT_SELECT );
#else
	outl( 0, ioaddr + RX_INT_SELECT );
#endif
#if 0
	outl( 0, ioaddr + RX_WAIT_SELECT );
#else
	outl( (RX_WAIT_SELECT_EOP << 16) | RX_WAIT_SELECT_EOP, 
	      ioaddr + RX_WAIT_SELECT );
#endif
#if 1
	outl( 0, ioaddr + RX_BRANCH_SELECT );
#else
	outl( (RX_BRANCH_SELECT_EOP << 16) | RX_BRANCH_SELECT_EOP,
	      ioaddr + RX_BRANCH_SELECT );
#endif

	/* configure DBDMA */
	outl( (DBDMA_BE | DBDMA_DPMRLE | DBDMA_TDPCE |
	       DBDMA_DDPE | DBDMA_TDPE |
	       (DBDMA_BURST_4 << DBDMA_TX_BST_SHIFT) |
	       (DBDMA_BURST_4 << DBDMA_RX_BST_SHIFT) |
	       (DBDMA_TX_ARBITRATION_DEFAULT) |
	       (DBDMA_RX_ARBITRATION_DEFAULT)), ioaddr + DBDMA_CONTROL );

	outl( 0, ioaddr + TX_THRESHOLD );

	/* disable MAC loopback */
	outl( (MAC_CONFIG_ITXA | MAC_CONFIG_RXEN | MAC_CONFIG_RETRYL |
	       MAC_CONFIG_PADEN | (0x18 << 16)),
	      ioaddr + MAC_CONFIG );

	/* configure MAC */
	outl( (MAC_CONFIG_ITXA | MAC_CONFIG_RXEN | MAC_CONFIG_RETRYL |
	       MAC_CONFIG_PADEN | ( 0x18 << 16)), ioaddr + MAC_CONFIG );

	outw( (0x1018), ioaddr + NBTOB_INTP_GAP );

	/* clear and enable interrupts */
	inw( ioaddr + INTERRUPT_CLEAR );
	ncr885e_enable( dev );

	/* and enable them in the chip */
	outl( (INTERRUPT_INTE|INTERRUPT_TX_MASK|INTERRUPT_RX_MASK)<<16,
	      ioaddr + INTERRUPT_ENABLE - 2);

	if (ncr885e_debug > 3)
		printk( KERN_INFO "%s: 53C885 config complete.\n", dev->name );

	return;
}



/*
   transmit interrupt  */

static void
ncr885e_tx( struct net_device *dev )

{
	struct ncr885e_private *sp = (struct ncr885e_private *) dev->priv;
	volatile struct dbdma_cmd *cp, *dp;
	unsigned short txbits, xfer;
	int i;

	del_timer( &sp->tx_timeout );

	if (ncr885e_debug > 3)
		printk( KERN_INFO "%s: ncr885e_tx: active=%d, dirty=%d, current=%d\n", 
			dev->name, sp->tx_active, sp->tx_dirty, sp->tx_current );

	sp->timeout_active = 0;

	i = sp->tx_dirty;
	cp = sp->tx_cmds + (i*3);
	dp = cp+1;
	sp->tx_active--;

	xfer = inw( &dp->xfer_status );
	txbits = inw( &sp->tx_status[i] );

	if (ncr885e_debug > 4) {
		show_dbdma_cmd( cp );
		show_dbdma_cmd( dp );
	}

	/* get xmit result */
	txbits = inw( &sp->tx_status[i] );

	if (ncr885e_debug > 3)
		printk( KERN_INFO "%s: tx xfer=%04x, txbits=%04x\n", dev->name,
			xfer, txbits );

	/* look for any channel status (?) */
	if ( xfer ) {

		dev_kfree_skb_irq( sp->tx_skbufs[i] );

		if ( txbits & TX_STATUS_TXOK ) {
			sp->stats.tx_packets++;
			sp->stats.tx_bytes += inw( &cp->req_count );
		}

		/* dropped packets */
		if ( txbits & (TX_STATUS_TDLC|TX_STATUS_TDEC) ) {
			sp->stats.tx_dropped++;
		}

		/* add the collisions */
		sp->stats.collisions += ( txbits & 0x04 );

	}

	netif_start_queue(dev);
  
	return;
}

/*  rx interrupt handling */
static void
ncr885e_rx( struct net_device *dev )

{
	struct ncr885e_private *sp = (struct ncr885e_private *) dev->priv;
	volatile struct dbdma_cmd *cp;
	struct sk_buff *skb;
	int i, nb;
	unsigned short status;
	unsigned char *data, *stats;
	unsigned long rxbits, ioaddr = dev->base_addr;

	i = sp->rx_current;
	cp = sp->rx_cmds + (i*2);

	if (ncr885e_debug > 3)
		printk( KERN_INFO "%s: ncr885e_rx dirty=%d, current=%d (cp@%p)\n",
			dev->name, sp->rx_dirty, sp->rx_current, cp );

	nb = inw( &cp->req_count ) - inw( &cp->res_count );
	status = inw( &cp->xfer_status );

	if (ncr885e_debug > 3)
		printk( KERN_INFO "%s: (rx %d) bytes=%d, xfer_status=%04x\n", 
			dev->name, i, nb, status );

	if ( status ) {

		skb = sp->rx_skbufs[i];
		data = skb->data;
		stats = data + nb - 3;
		rxbits = (stats[0]|stats[1]<<8|stats[2]<<16);
  
		if (ncr885e_debug > 3)
			printk( KERN_INFO "  rx_bits=%06lx\n", rxbits );

		skb->dev = dev;
		skb_put( skb, nb-3 );
		skb->protocol = eth_type_trans( skb, dev );
		netif_rx( skb );
		sp->rx_skbufs[i] = 0;

		if ( rxbits & RX_STATUS_RXOK ) {
			sp->stats.rx_packets++;
			sp->stats.rx_bytes += nb;
		}

		if ( rxbits & RX_STATUS_MCAST )
			sp->stats.multicast++;

	}

	sp->rx_dirty = sp->rx_current;

	if ( ++sp->rx_current >= NR_RX_RING )
		sp->rx_current = 0;

	/* fix up the one we just trashed */
	cp = sp->rx_cmds + (sp->rx_dirty * 2);

	skb = dev_alloc_skb( RX_BUFLEN + 2 );
	if ( skb != 0 ) {
		skb_reserve( skb, 2 );
		sp->rx_skbufs[sp->rx_dirty] = skb;
	}

	if (ncr885e_debug > 2)
		printk( KERN_INFO "%s: ncr885e_rx: using ring index %d, filling cp @ %p\n", 
			dev->name, sp->rx_current, cp );
  
	outw( RX_BUFLEN, &cp->req_count );
	outw( 0, &cp->res_count );
	data = skb->data;
	outl( virt_to_bus( data ), &cp->phy_addr );
	outw( 0, &cp->xfer_status );

	cp = sp->rx_cmds + (sp->rx_current * 2);

	/* restart rx DMA */
	outl( virt_to_bus( cp ), ioaddr + RX_CMD_PTR_LO );
	outl( (RX_DBDMA_ENABLE << 16)|RX_CHANNEL_RUN,
	      ioaddr + RX_CHANNEL_CONTROL );

	return;
}

static void
ncr885e_misc_ints( struct net_device *dev, unsigned short status )

{
	struct ncr885e_private *sp = (struct ncr885e_private *) dev->priv;
	struct dbdma_cmd *cp;
	unsigned long ioaddr = dev->base_addr;

	if (ncr885e_debug > 1)
		printk( KERN_INFO "miscellaneous interrupt handled; status=%02x\n", 
			status );

	/* various transmit errors */
	if ( status & 
	     (INTERRUPT_PPET | INTERRUPT_PBFT | INTERRUPT_IIDT) ) {

		/* illegal instruction in tx dma */
		if ( status & INTERRUPT_IIDT ) {

			cp = (struct dbdma_cmd *) bus_to_virt( inl( ioaddr + TX_CMD_PTR_LO ));
			printk( KERN_INFO "%s: tx illegal insn:\n", dev->name );
			printk( KERN_INFO " tx DBDMA - cmd = %p, status = %04x\n", 
				cp, inw( ioaddr + TX_CHANNEL_STATUS ));
			printk( KERN_INFO " command = %04x, phy_addr=%08x, req_count=%04x\n",
				inw( &cp->command ), inw( &cp->phy_addr ), inw( &cp->req_count ));
		}

		if ( status & INTERRUPT_PPET )
			printk( KERN_INFO "%s: tx PCI parity error\n", dev->name );
 
		if ( status & INTERRUPT_PBFT )
			printk( KERN_INFO "%s: tx PCI bus fault\n", dev->name );
	}

	/* look for rx errors */
	if ( status &
	     (INTERRUPT_PPER | INTERRUPT_PBFR | INTERRUPT_IIDR)) {

		/* illegal instruction in rx dma */
		if ( status & INTERRUPT_IIDR ) {
#if 0
			cmd = inl( ioaddr + RX_CMD_PTR_LO );      
#endif
			printk( KERN_ERR "%s: rx illegal DMA instruction:\n", dev->name );
			printk( KERN_ERR "    channel status=%04x,\n",
				inl( ioaddr + RX_CHANNEL_STATUS ));
#if 0
			show_dbdma_cmd( bus_to_virt( inl( ioaddr + RX_CMD_PTR_LO )));
			printk( KERN_ERR "    instr (%08x) %08x %08x %08x\n",
				(int) cmd, cmd[0], cmd[1], cmd[2] );
#endif
		}

		/* PCI parity error */
		if ( status & INTERRUPT_PPER )
			printk( KERN_INFO "%s: rx PCI parity error\n", dev->name );

		if ( status & INTERRUPT_PBFR )
			printk( KERN_INFO "%s: rx PCI bus fault\n", dev->name );

		sp->stats.rx_errors++;
	}

	if ( status & INTERRUPT_WI ) {
		printk( KERN_INFO "%s: link pulse\n", dev->name );
	}

	/* bump any counters */
  

	return;
}

static void
ncr885e_interrupt( int irq, void *dev_id, struct pt_regs *regs )

{
	struct net_device  *dev = (struct net_device *) dev_id;
	struct ncr885e_private *sp;
	unsigned short status;
	int ioaddr;

	if ( dev == NULL ) {
		printk( KERN_ERR "symba: Interrupt IRQ %d for unknown device\n", irq );
		return;
	}

	ioaddr = dev->base_addr;
	sp = (struct ncr885e_private *) dev->priv;
	spin_lock( &sp->lock );
  
	status = inw( ioaddr + INTERRUPT_CLEAR );

	if (ncr885e_debug > 2)
		printk( KERN_INFO "%s: 53C885 interrupt 0x%02x\n", dev->name, status );

	/* handle non-tx and rx interrupts first */
	if ( status & ~(INTERRUPT_DIT|INTERRUPT_DIR))
		ncr885e_misc_ints( dev, status );

	/* look for tx interrupt: more to transmit, DBDMA stopped, or tx done */
	if ( ( status & INTERRUPT_DIT ) ) {

		if (ncr885e_debug > 2)
			printk( KERN_INFO "%s: tx int; int=%02x, chan stat=%02x\n", 
				dev->name, status, inw( ioaddr + TX_CHANNEL_STATUS ));

		/* turn off timer */
		del_timer( &sp->tx_timeout );
		sp->timeout_active = 0;

		/* stop DMA */
		outl( TX_DBDMA_ENABLE << 16, ioaddr + TX_CHANNEL_CONTROL );

		ncr885e_tx( dev );
	}

	if ( status & INTERRUPT_DIR ) {

		if ( ncr885e_debug > 2 )
			printk( KERN_INFO "%s: rx interrupt; int=%02x, rx channel stat=%02x\n", 
				dev->name, status, inw( ioaddr + RX_CHANNEL_STATUS ));

		/* stop DMA */
		outl( RX_DBDMA_ENABLE << 16, ioaddr + RX_CHANNEL_CONTROL );
    
		/* and handle the interrupt */
		ncr885e_rx( dev );
	}
  
	spin_unlock( &sp->lock );

	return;
}


/*  doesn't set the address permanently, however... */
static int 
ncr885e_set_address( struct net_device *dev, void *addr )

{
	struct ncr885e_private *sp = (struct ncr885e_private *) dev->priv;
	struct sockaddr *saddr = addr;
	unsigned long  flags;
	unsigned short reg[3];
	unsigned char *ioaddr, *p;
	int i;

	memcpy( dev->dev_addr, saddr->sa_data, dev->addr_len );

	p = (unsigned char *) dev->dev_addr;
	printk( KERN_INFO "%s: setting new MAC address - ", dev->name );
#if 0
	for( p = (unsigned char *) dev->dev_addr, i=0; i < 6; i++, p++ ) 
		printk("%c%2.2x", i ? ':' : ' ', *p );
#endif


	p = (unsigned char *) &reg;
	for( i=0; i < 6; i++ )
		p[i] = dev->dev_addr[i];

#if 0
	printk("%s: Setting new mac address - ", dev->name );
	for( i=0; i < 6; i++ ) {
		printk("%02x", i ? ':' : ' ', p[i] );
	}

	printk("\n");
#endif

	/* stop rx for the change */
	outl( RX_DBDMA_ENABLE << 16, ioaddr + RX_CHANNEL_CONTROL );
  
	spin_lock_irqsave( &sp->lock, flags );

	ioaddr = (unsigned char *) dev->base_addr;

	for( i = 0; i < 3; i++ ) {
		reg[i] = ((reg[i] & 0xff) << 8) | ((reg[i] >> 8) & 0xff);
		printk("%04x ", reg[i] );
		outw( reg[i], ioaddr + STATION_ADDRESS_0 + (i*2));
	}
	printk("\n");

	spin_unlock_irqrestore( &sp->lock, flags );

	/* restart rx */
	outl((RX_DBDMA_ENABLE << 16)|RX_CHANNEL_RUN, 
	     ioaddr + RX_CHANNEL_CONTROL );

	return 0;
}

static void 
ncr885e_tx_timeout( unsigned long data )

{
	struct net_device *dev = (struct net_device *) data;
	struct ncr885e_private *sp = (struct ncr885e_private *) dev->priv;
	unsigned long flags, ioaddr;
	int i;
  
	save_flags( flags );
	cli();

	ioaddr = dev->base_addr;
	sp->timeout_active = 0;
	i = sp->tx_dirty;

	/* if we weren't active, bail... */
	if ( sp->tx_active == 0 ) {
		printk( KERN_INFO "%s: ncr885e_timeout...tx not active!\n", dev->name );
		goto out;
	}

	printk( KERN_ERR "%s: 53C885 timed out.  Resetting...\n", dev->name );

	/* disable rx and tx DMA */
	outl( (TX_DBDMA_ENABLE << 16), ioaddr + TX_CHANNEL_CONTROL );
	outl( (RX_DBDMA_ENABLE << 16), ioaddr + RX_CHANNEL_CONTROL );

	/* reset the chip */
	ncr885e_config( dev );
	ncr885e_enable( dev );

	/* clear the wedged skb in the tx ring */
	sp->tx_active = 0;
	++sp->stats.tx_errors;
  
	if ( sp->tx_skbufs[i] ) {
		dev_kfree_skb( sp->tx_skbufs[i] );
		sp->tx_skbufs[i] = 0;
	}

	/* start anew from the beginning of the ring buffer (why not?) */
	sp->tx_current = 0;
	netif_wake_queue(dev);

	/* restart rx dma */
	outl( (RX_DBDMA_ENABLE << 16) | RX_CHANNEL_RUN,
	      ioaddr + RX_CHANNEL_CONTROL );  
 out:

	restore_flags( flags );
}

static inline void
ncr885e_set_timeout( struct net_device *dev )

{
	struct ncr885e_private *sp = (struct ncr885e_private *) dev->priv;
	unsigned long flags;

	save_flags(flags);
	cli();

	if ( sp->timeout_active )
		del_timer( &sp->tx_timeout );

	sp->tx_timeout.expires = jiffies + TX_TIMEOUT;
	sp->tx_timeout.function = ncr885e_tx_timeout;
	sp->tx_timeout.data = (unsigned long) dev;
	add_timer( &sp->tx_timeout );
	sp->timeout_active = 1;
	restore_flags( flags );
}


/*
 *  The goal is to set up DBDMA such that the rx ring contains only
 *  one DMA descriptor per ring element and the tx ring has two (using
 *  the cool features of branch- and wait-select.  However, I'm not sure
 *  if it's possible.  For now, we plod through it with 3 descriptors
 *  for tx, and two for rx.
 */

static int
ncr885e_open( struct net_device *dev )

{
	struct ncr885e_private *sp = (struct ncr885e_private *) dev->priv;
	unsigned long ioaddr = dev->base_addr;
	struct sk_buff *skb;
	int i, size;
	char *data;
	struct dbdma_cmd *cp;
	unsigned long flags;

	/* allocate enough space for the tx and rx rings and a STOP descriptor */  
	size = (sizeof( struct dbdma_cmd ) *
		((NR_TX_RING * 3) + (NR_RX_RING * 2) + 1));

	cp = kmalloc( size, GFP_KERNEL );

	if ( cp == 0 ) {
		printk( KERN_ERR "Insufficient memory (%d bytes) for DBDMA\n", size );
		return -ENOMEM;
	}

	spin_lock_init( &sp->lock );
	spin_lock_irqsave( &sp->lock, flags );

	memset((char *) cp, 0, size );
	sp->head = cp;

	sp->stop_cmd = cp;
	outl( DBDMA_STOP, &cp->command );
  
	sp->rx_cmds = ++cp;

	for( i = 0; i < NR_RX_RING; i++ ) {

		cp = sp->rx_cmds + (i*2);
		skb = dev_alloc_skb( RX_BUFLEN + 2 );

		/* if there is insufficient memory, make this last ring use a 
		   static buffer and leave the loop with that skb as final one */
		if ( skb == 0 ) {
			printk( KERN_ERR "%s: insufficient memory for rx ring buffer\n",
				dev->name );
			break;
		}

		skb_reserve( skb, 2 );
		sp->rx_skbufs[i] = skb;
		data = skb->data;

		/* The DMA commands here are done such that an EOP is the only
		   way that we should get an interrupt.  This means that we could
		   fill more than one skbuff before getting the interrupt at EOP. */

		/* Handle rx DMA such that it always interrupts.... */
		outw( (INPUT_MORE|INTR_ALWAYS), &cp->command );
		outw( RX_BUFLEN, &cp->req_count );
		outw( 0, &cp->res_count );
		outl( virt_to_bus( data ), &cp->phy_addr );
		outl( virt_to_bus( sp->stop_cmd ), &cp->cmd_dep );
		outw( 0, &cp->xfer_status );
#if 0
		printk( KERN_INFO "rx at %p\n", cp );
		show_dbdma_cmd( cp );
#endif
		++cp;

		outw( DBDMA_STOP, &cp->command );

	}

	/* initialize to all rx buffers are available, fill limit is the end */
	sp->rx_dirty = 0;
	sp->rx_current = 0;

	/* fill the tx ring */
	sp->tx_cmds = cp+1;

	for( i = 0; i < NR_TX_RING; i++ ) {

		/* minimal setup for tx command */
		cp = sp->tx_cmds + (i*3);
		outw( OUTPUT_LAST, &cp->command );
		if (ncr885e_debug > 3) {
			printk( KERN_INFO "tx OUTPUT_LAST at %p\n", cp );
			show_dbdma_cmd( cp );
		}

		/* full setup for the status cmd */
		cp++;
		outw( INPUT_LAST|INTR_ALWAYS|WAIT_IFCLR, &cp->command );
		outl( virt_to_bus( &sp->tx_status[i] ), &cp->phy_addr );
		outw( 2, &cp->req_count );
		if ( ncr885e_debug > 3) {
			printk( KERN_INFO "tx INPUT_LAST cmd at %p\n", cp );
			show_dbdma_cmd( cp );
		}

		++cp;
		outw( DBDMA_STOP, &cp->command );
 
	}
#if 0
	/* chain the last tx DMA command to the STOP cmd */
	outw((INPUT_LAST|INTR_ALWAYS|BR_ALWAYS), &cp->command );
	outl( virt_to_bus( sp->stop_cmd ), &cp->cmd_dep );
#endif
	sp->tx_active = 0;
	sp->tx_current = 0;
	sp->tx_dirty = 0;

	spin_unlock_irqrestore( &sp->lock, flags );

	/* the order seems important here for some reason.  If the MPIC isn't
	   enabled before the ethernet chip is enabled, shrapnel from the
	   bootloader causes us to receive interrupts even though we've not 
	   yet enabled the tx channel.  Go figure.  It'd be better to configure
	   the chip in the probe1() routine, but then we don't see interrupts
	   at all.  Everything looks all right on the logic analyzer, but... */

	ncr885e_config( dev );

	/* enable ethernet interrupts */
	if ( request_irq( dev->irq, &ncr885e_interrupt, SA_SHIRQ, chipname, dev )) {
		printk( KERN_ERR "%s: can't get irq %d\n", dev->name, dev->irq );
		return -EAGAIN;
	}

	(void) inw( ioaddr + INTERRUPT_CLEAR );

	ncr885e_enable( dev );

	/* start rx DBDMA */
	outl( virt_to_bus( sp->rx_cmds ), ioaddr + RX_CMD_PTR_LO );
	outl( (RX_DBDMA_ENABLE << 16)|RX_CHANNEL_RUN,
	      ioaddr + RX_CHANNEL_CONTROL );

	netif_start_queue(dev);

	return 0;
}

static int
ncr885e_xmit_start( struct sk_buff *skb, struct net_device *dev )

{
	struct ncr885e_private *sp = (struct ncr885e_private *) dev->priv;
	volatile struct dbdma_cmd *cp, *dp;
	unsigned long flags, ioaddr = dev->base_addr;
	int len, next, fill, entry;

	if ( ncr885e_debug > 3)
		printk( KERN_INFO "%s: xmit_start len=%d, dirty=%d, current=%d, active=%d\n",
			dev->name, skb->len, sp->tx_dirty, sp->tx_current, sp->tx_active );

	spin_lock_irqsave( &sp->lock, flags );

	/* find the free slot in the ring buffer */
	fill = sp->tx_current;
	next = fill + 1;

	if ( next >= NR_TX_RING )
		next = 0;
#if 0
	/* mark ourselves as busy, even if we have too many packets waiting */
	netif_stop_queue(dev);
#endif

	/* see if it's necessary to defer this packet */
	if ( sp->tx_active >= MAX_TX_ACTIVE ) {
		spin_unlock_irqrestore( &sp->lock, flags );
		return -1;
	}

	sp->tx_active++;  /* bump "active tx" count */
	sp->tx_current = next;  /* and show that we've used this buffer */
	sp->tx_dirty = fill;     /* and mark this one to get picked up */

	len = skb->len;

	if ( len > ETH_FRAME_LEN ) {
		printk( KERN_DEBUG "%s: xmit frame too long (%d)\n", dev->name, len );
		len = ETH_FRAME_LEN;
	}  

	/* get index into the tx DBDMA chain */
	entry = fill * 3;
	sp->tx_skbufs[fill] = skb;
	cp = sp->tx_cmds + entry;
	dp = cp + 1;

	/* update the rest of the OUTPUT_MORE descriptor */
	outw( len, &cp->req_count );
	outl( virt_to_bus( skb->data ), &cp->phy_addr );
	outw( 0, &cp->xfer_status );
	outw( 0, &cp->res_count );

	/* and finish off the INPUT_MORE */
	outw( 0, &dp->xfer_status );
	outw( 0, &dp->res_count );
	sp->tx_status[fill] = 0;
	outl( virt_to_bus( &sp->tx_status[fill] ), &dp->phy_addr );

	if ( ncr885e_debug > 2 )
		printk(KERN_INFO "%s: xmit_start: active %d, tx_current %d, tx_dirty %d\n",
		       dev->name, sp->tx_active, sp->tx_current, sp->tx_dirty );

	if ( ncr885e_debug > 4 ) {
		show_dbdma_cmd( cp );
		show_dbdma_cmd( dp );
	}


	/* restart the tx DMA engine */
	outl( virt_to_bus( cp ), ioaddr + TX_CMD_PTR_LO );
	outl( (TX_DBDMA_ENABLE << 16)|TX_CHANNEL_RUN, 
	      ioaddr + TX_CHANNEL_CONTROL );

	ncr885e_set_timeout( dev );

	spin_unlock_irqrestore( &sp->lock, flags );
	dev->trans_start = jiffies;

	return 0;      
}

static int
ncr885e_close(struct net_device *dev)

{
	int i;
	struct ncr885e_private *np = (struct ncr885e_private *) dev->priv;
	unsigned long ioaddr = dev->base_addr;

	netif_stop_queue(dev);

	spin_lock( &np->lock );

	printk(KERN_INFO "%s: NCR885E Ethernet closing...\n", dev->name );

	if (ncr885e_debug > 1)
		printk(KERN_DEBUG "%s: Shutting down Ethernet chip\n", dev->name);

	ncr885e_disable(dev);

	del_timer(&np->tx_timeout);

	/* flip off rx and tx */
	outl( (RX_DBDMA_ENABLE << 16), ioaddr + RX_CHANNEL_CONTROL );
	outl( (TX_DBDMA_ENABLE << 16), ioaddr + TX_CHANNEL_CONTROL );  

	/* free up the IRQ */
	free_irq( dev->irq, dev );

	for( i = 0; i < NR_RX_RING; i++ ) {
		if (np->rx_skbufs[i])
			dev_kfree_skb( np->rx_skbufs[i] );
		np->rx_skbufs[i] = 0;
	}
#if 0
	for (i = 0; i < NR_TX_RING; i++) {
		if (np->tx_skbufs[i])
			dev_kfree_skb(np->tx_skbufs[i]);
		np->tx_skbufs[i] = 0;
	}
#endif
	spin_unlock( &np->lock );

	kfree( np->head );

	return 0;
}


/*
 *  multicast promiscuous mode isn't used here.  Allow code in the
 *  IP stack to determine which multicast packets are good or bad....
 *  (this avoids having to use the hash table registers)
 */
static void
ncr885e_set_multicast( struct net_device *dev )

{
	int ioaddr = dev->base_addr;

	if ( ncr885e_debug > 3 )
		printk("%s: set_multicast: dev->flags = %x, AF=%04x\n", 
		       dev->name, dev->flags, inw( ioaddr + ADDRESS_FILTER ));

	if ( dev->flags & IFF_PROMISC ) {
		printk( KERN_INFO "%s: Promiscuous mode enabled.\n", dev->name );
		outw( ADDRESS_RPPRO, ioaddr + ADDRESS_FILTER );
	}

	/* accept all multicast packets without checking the mc_list.  */
	else if ( dev->flags & IFF_ALLMULTI ) {
		printk( KERN_INFO "%s: Enabling all multicast packets.\n", 
			dev->name );
		outw( ADDRESS_RPPRM, ioaddr + ADDRESS_FILTER );
	}

	/* enable broadcast rx */
	else {
		outw( ADDRESS_RPABC, ioaddr + ADDRESS_FILTER );
	}  
}

static struct net_device_stats *
ncr885e_stats( struct net_device *dev )

{
	struct ncr885e_private *np = (struct ncr885e_private *) dev->priv;

	return &np->stats;
}

/*  By this function, we're certain that we have a 885 Ethernet controller
 *  so we finish setting it up and wrap up all the required Linux ethernet
 *  configuration.
 */

static int __init ncr885e_probe1(unsigned long ioaddr, unsigned char irq )

{
	struct net_device *dev;
	struct ncr885e_private *sp;
	unsigned short station_addr[3], val;
	unsigned char *p;
	int  i;

	dev = init_etherdev( NULL, sizeof( struct ncr885e_private ) );
	if (!dev)
		return -ENOMEM;
	SET_MODULE_OWNER(dev);

	sp = dev->priv;

	/* snag the station address and display it */
	for( i = 0; i < 3; i++ ) {
		val = inw( ioaddr + STATION_ADDRESS_0 + (i*2));
		station_addr[i] = ((val >> 8) & 0xff) | ((val << 8) & 0xff00);
	}

	printk( KERN_INFO "%s: %s at %08lx,", dev->name, chipname, ioaddr );

	p = (unsigned char *) &station_addr;

	for( i=0; i < 6; i++ ) {
		dev->dev_addr[i] = *p;
		printk("%c%2.2x", i ? ':' : ' ', dev->dev_addr[i] );
		p++;
	}

	printk(", IRQ %d.\n", irq );

	/* set up a timer */
	init_timer( &sp->tx_timeout );
	sp->timeout_active = 0;

	dev->base_addr = ioaddr;
	dev->irq = irq;

	ether_setup( dev );

	/* everything else */
	dev->open = ncr885e_open;
	dev->stop = ncr885e_close;
	dev->get_stats = ncr885e_stats;
	dev->hard_start_xmit = ncr885e_xmit_start;
	dev->set_multicast_list = ncr885e_set_multicast;
	dev->set_mac_address = ncr885e_set_address;
	
	root_dev = dev;

	return 0;
}

/*  Since the NCR 53C885 is a multi-function chip, I'm not worrying about
 *  trying to get the the device(s) in slot order.  For our (Synergy's)
 *  purpose, there's just a single 53C885 on the board and we don't 
 *  worry about the rest.
 */

static int __init ncr885e_probe(void)
{
	struct pci_dev *pdev = NULL;
	unsigned int ioaddr, ret;
	unsigned char irq;

	/* use 'if' not 'while' where because driver only supports one device */
	if (( pdev = pci_find_device( PCI_VENDOR_ID_NCR, 
					PCI_DEVICE_ID_NCR_53C885_ETHERNET,
					pdev )) != NULL ) {

		if ( !print_version ) {
			print_version++;
			printk( KERN_INFO "%s", version );
		}

		if (pci_enable_device(pdev))
			return -ENODEV;

		/* Use I/O space */
		ioaddr = pci_resource_start (pdev, 0);
		irq = pdev->irq;

		if ( !request_region( ioaddr, NCR885E_TOTAL_SIZE, "ncr885e" ))
			return -ENOMEM;

		/* finish off the probe */
		ret = ncr885e_probe1(ioaddr, irq);
		if (ret)
			release_region(ioaddr, NCR885E_TOTAL_SIZE);
		else
			pci_set_master(pdev);
	}

	return ret;
}

/* debugging to peek at dma descriptors */
static void
show_dbdma_cmd( volatile struct dbdma_cmd *cmd )

{
	printk( KERN_INFO " cmd %04x, physaddr %08x, req_count %04x\n",
		inw( &cmd->command ), inl( &cmd->phy_addr ), inw( &cmd->req_count ));
	printk( KERN_INFO " res_count %04x, xfer_status %04x, branch %08x\n", 
		inw( &cmd->res_count ), inw( &cmd->xfer_status ),inl( &cmd->cmd_dep ));
}

#if 0
static int
read_eeprom( unsigned int ioaddr, int location )

{
	int loop;
	unsigned char val;

	outb( (location & 0xff), ioaddr + EE_WORD_ADDR );

	/* take spillover from location in control reg */
	outb(EE_CONTROL_RND_READB | (location & (0x7<<8)), ioaddr + EE_CONTROL);

	loop = 1000;
	while( (inb( ioaddr + EE_STATUS) & EE_SEB) &&
	       (loop > 0) ) {
		udelay( 10 );
		loop--;
	}

	if ( inb( ioaddr + EE_STATUS ) & EE_SEE ) {
		printk("%s: Serial EEPROM read error\n", chipname);
		val = 0xff;
	}

	else 
		val = inb( ioaddr + EE_READ_DATA );

	return (int) val;
}
#endif

#ifdef NCR885E_DEBUG_MII
static void
show_mii( unsigned long ioaddr )

{
	int   phyctrl, phystat, phyadvert, phypartner, phyexpan;

	phyctrl = read_mii( ioaddr, MII_AUTO_NEGOTIATION_CONTROL );
	phystat = read_mii( ioaddr, MII_AUTO_NEGOTIATION_STATUS );
	phyadvert = read_mii( ioaddr, MII_AUTO_NEGOTIATION_ADVERTISEMENT );
	phypartner = read_mii( ioaddr, MII_AUTO_NEGOTIATION_LINK_PARTNER );
	phyexpan = read_mii( ioaddr, MII_AUTO_NEGOTIATION_EXPANSION );

	printk( KERN_INFO "PHY: advert=%d %s, partner=%s %s, link=%d, %s%s\n",
		(phyadvert & MANATECH_100BASETX_FULL_DUPLEX ? 100 : 10),
		(phyctrl & MANC_AUTO_NEGOTIATION_ENABLE ? "auto" : "fixed"),
		(phypartner & MANLP_ACKNOWLEDGE ?
		 (phypartner & MANATECH_100BASETX_FULL_DUPLEX ? "100" : "10") :
		 "?"),
		(phyexpan & MANE_LINK_PARTNER_AUTO_ABLE ? "auto" : "fixed"),
		(phyctrl & MANC_PHY_SPEED_100 ? 100 : 10),
		(phystat & MANS_LINK_STATUS ? "up" : "down"),
		(phyexpan & MANE_PARALLEL_DETECTION_FAULT ? " PD-fault" : "" ));
	return;
}


static int
read_mii( unsigned long ioaddr, int reg )

{
	int    timeout;


	timeout = 100000;

	while( inw( ioaddr + MII_INDICATOR ) & MII_BUSY ) {

		if ( timeout-- < 0 ) {
			printk( KERN_INFO "Timed out waiting for MII\n" );
			return -1;
		}
	}

	outw( (1<<8) + reg, ioaddr + MII_ADDRESS );
	outw( MIIM_RSTAT, ioaddr + MIIM_COMMAND );

	timeout = 100000;
	while( inw( ioaddr + MII_INDICATOR ) & MII_BUSY ) {
		if ( timeout-- < 0 ) {
			printk( KERN_INFO "Timed out waiting for MII\n" );
			return -1;
		}
	}

	return( inw( ioaddr + MII_READ_DATA ));
}

static void
write_mii( unsigned long ioaddr, int reg, int data )

{
	int timeout=100000;

	printk( KERN_INFO "MII indicator: %02x\n", inw( ioaddr + MII_INDICATOR ));

	while( inw( ioaddr + MII_INDICATOR ) & MII_BUSY ) {
		if ( timeout-- <= 0 ) {
			printk( KERN_INFO "Timeout waiting to write to MII\n" );
			return;
		}
		udelay( 10 );
	}

	outw( (1<<8) + reg, ioaddr + MII_ADDRESS );
	outw( data, ioaddr + MII_WRITE_DATA );

	return;
}

#endif /* NCR885E_DEBUG_MII */

static void __exit ncr885e_cleanup(void)
{
	if ( root_dev ) {
		unregister_netdev( root_dev );
		release_region( root_dev->base_addr, NCR885E_TOTAL_SIZE );
		kfree( root_dev );
		root_dev = NULL;
	}  
}

module_init(ncr885e_probe);
module_exit(ncr885e_cleanup);

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
