/*------------------------------------------------------------------------
 . smc91111.c
 . This is a driver for SMSC's 91C111 single-chip Ethernet device.
 .
 . Copyright (C) 2001 Standard Microsystems Corporation (SMSC)
 .       Developed by Simple Network Magic Corporation (SNMC)
 . Copyright (C) 1996 by Erik Stahlman (ES)
 .
 . This program is free software; you can redistribute it and/or modify
 . it under the terms of the GNU General Public License as published by
 . the Free Software Foundation; either version 2 of the License, or
 . (at your option) any later version.
 .
 . This program is distributed in the hope that it will be useful,
 . but WITHOUT ANY WARRANTY; without even the implied warranty of
 . MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 . GNU General Public License for more details.
 .
 . You should have received a copy of the GNU General Public License
 . along with this program; if not, write to the Free Software
 . Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 .
 . Information contained in this file was obtained from the LAN91C111
 . manual from SMC.  To get a copy, if you really want one, you can find
 . information under www.smsc.com.
 .
 .
 . "Features" of the SMC chip:
 .   Integrated PHY/MAC for 10/100BaseT Operation
 .   Supports internal and external MII
 .   Integrated 8K packet memory
 .   EEPROM interface for configuration
 .
 . Arguments:
 . 	io	= for the base address
 .	irq	= for the IRQ
 .	nowait	= 0 for normal wait states, 1 eliminates additional wait states
 .
 . author:
 . 	Erik Stahlman				( erik@vt.edu )
 . 	Daris A Nevil				( dnevil@snmc.com )
 . contributors:
 .      Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 .
 . Hardware multicast code from Peter Cammaert ( pc@denkart.be )
 .
 . Sources:
 .    o   SMSC LAN91C111 databook (www.smsc.com)
 .    o   smc9194.c by Erik Stahlman
 .    o   skeleton.c by Donald Becker ( becker@scyld.com )
 .
 . History:
 .      08/20/00  Arnaldo Melo   fix kfree(skb) in smc_hardware_send_packet
 .      12/15/00  Christian Jullien fix "Warning: kfree_skb on hard IRQ"
 .	04/25/01  Daris A Nevil  Initial public release through SMSC
 .	03/16/01  Daris A Nevil  Modified smc9194.c for use with LAN91C111
 .	08/22/01  Scott Anderson Merge changes from smc9194 to smc91111
 ----------------------------------------------------------------------------*/

// Use power-down feature of the chip
#define POWER_DOWN	1

/*
 . DEBUGGING LEVELS
 .
 . 0 for normal operation
 . 1 for slightly more details
 . >2 for various levels of increasingly useless information
 .    2 for interrupt tracking, status flags
 .    3 for packet info
 .    4 for complete packet dumps
*/
#ifndef SMC_DEBUG
#define SMC_DEBUG 0
#endif

static const char version[] =
	"smc91111.c:v1.0saa 08/22/01 by Daris A Nevil (dnevil@snmc.com)\n";

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <linux/errno.h>
#include <linux/delay.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#ifdef CONFIG_SYSCTL
#include <linux/proc_fs.h>
#include <linux/sysctl.h>
#endif

#include "smc91111.h"

#ifdef CONFIG_ISA

/*
 .the LAN91C111 can be at any of the following port addresses.  To change,
 .for a slightly different card, you can add it to the array.  Keep in
 .mind that the array must end in zero.
*/
static unsigned int smc_portlist[] __initdata = {
	0x200, 0x220, 0x240, 0x260, 0x280, 0x2A0, 0x2C0, 0x2E0,
	0x300, 0x320, 0x340, 0x360, 0x380, 0x3A0, 0x3C0, 0x3E0, 0
};

#endif  /* CONFIG_ISA */

static struct net_device *global_dev = NULL;
#ifndef SMC91111_BASE_ADDR
# define SMC91111_BASE_ADDR -1
#endif
static int io = SMC91111_BASE_ADDR;
#ifndef SMC91111_IRQ
# define SMC91111_IRQ -1
#endif
static int irq = SMC91111_IRQ;
static int nowait = 0;

MODULE_PARM(io, "i");
MODULE_PARM(irq, "i");
MODULE_PARM(nowait, "i");
MODULE_PARM_DESC(io, "SMC 91111 I/O base address");
MODULE_PARM_DESC(irq, "SMC 91111 IRQ number");

/*
 . Wait time for memory to be free.  This probably shouldn't be
 . tuned that much, as waiting for this means nothing else happens
 . in the system
*/
#define MEMORY_WAIT_TIME 16

#if SMC_DEBUG > 2
#define PRINTK3(args...) printk(args)
#else
#define PRINTK3(args...)
#endif

#if SMC_DEBUG > 1
#define PRINTK2(args...) printk(args)
#else
#define PRINTK2(args...)
#endif

#if SMC_DEBUG > 0
#define PRINTK(args...) printk(args)
#else
#define PRINTK(args...)
#endif


/*------------------------------------------------------------------------
 .
 . The internal workings of the driver.  If you are changing anything
 . here with the SMC stuff, you should have the datasheet and know
 . what you are doing.
 .
 -------------------------------------------------------------------------*/
#define CARDNAME "LAN91C111"

// Memory sizing constant
#define LAN91C111_MEMORY_MULTIPLIER	(1024*2)

/* store this information for the driver.. */
struct smc_local {

 	// these are things that the kernel wants me to keep, so users
	// can find out semi-useless statistics of how well the card is
	// performing
	struct net_device_stats stats;

	// If I have to wait until memory is available to send
	// a packet, I will store the skbuff here, until I get the
	// desired memory.  Then, I'll send it out and free it.
	struct sk_buff * saved_skb;

 	// This keeps track of how many packets that I have
 	// sent out.  When an TX_EMPTY interrupt comes, I know
	// that all of these have been sent.
	int	packets_waiting;

	// Set to true during the auto-negotiation sequence
	int	autoneg_active;

	// Address of our PHY port
	word	phyaddr;

	// Type of PHY
	word	phytype;

	// Last contents of PHY Register 18
	word	lastPhy18;

	// Contains the current active transmission mode
	word	tcr_cur_mode;

	// Contains the current active receive mode
	word	rcr_cur_mode;

	// Contains the current active receive/phy mode
	word	rpc_cur_mode;


#ifdef CONFIG_SYSCTL

	// Root directory /proc/sys/dev
	// Second entry must be null to terminate the table
	ctl_table root_table[2];

	// Directory for this device /proc/sys/dev/ethX
	// Again the second entry must be zero to terminate
	ctl_table eth_table[2];

	// This is the parameters (file) table
	ctl_table param_table[CTL_SMC_LAST_ENTRY];

	// Saves the sysctl header returned by register_sysctl_table()
	// we send this to unregister_sysctl_table()
	struct ctl_table_header *sysctl_header;

	// Parameter variables (files) go here
	char ctl_info[1024];
	int ctl_swfdup;
	int ctl_ephloop;
	int ctl_miiop;
	int ctl_autoneg;
	int ctl_rfduplx;
	int ctl_rspeed;
	int ctl_afduplx;
	int ctl_aspeed;
	int ctl_lnkfail;
	int ctl_forcol;
	int ctl_filtcar;
	int ctl_freemem;
	int ctl_totmem;
	int ctl_leda;
	int ctl_ledb;
	int ctl_chiprev;
#if SMC_DEBUG > 0
	int ctl_reg_bsr;
	int ctl_reg_tcr;
	int ctl_reg_esr;
	int ctl_reg_rcr;
	int ctl_reg_ctrr;
	int ctl_reg_mir;
	int ctl_reg_rpcr;
	int ctl_reg_cfgr;
	int ctl_reg_bar;
	int ctl_reg_iar0;
	int ctl_reg_iar1;
	int ctl_reg_iar2;
	int ctl_reg_gpr;
	int ctl_reg_ctlr;
	int ctl_reg_mcr;
	int ctl_reg_pnr;
	int ctl_reg_fpr;
	int ctl_reg_ptr;
	int ctl_reg_dr;
	int ctl_reg_isr;
	int ctl_reg_mtr1;
	int ctl_reg_mtr2;
	int ctl_reg_mtr3;
	int ctl_reg_mtr4;
	int ctl_reg_miir;
	int ctl_reg_revr;
	int ctl_reg_ercvr;
	int ctl_reg_extr;
	int ctl_phy_ctrl;
	int ctl_phy_stat;
	int ctl_phy_id1;
	int ctl_phy_id2;
	int ctl_phy_adc;
	int ctl_phy_remc;
	int ctl_phy_cfg1;
	int ctl_phy_cfg2;
	int ctl_phy_int;
	int ctl_phy_mask;
#endif // SMC_DEBUG > 0


#endif // CONFIG_SYSCTL

};


/*-----------------------------------------------------------------
 .
 .  The driver can be entered at any of the following entry points.
 .
 .------------------------------------------------------------------  */

/*
 . The kernel calls this function when someone wants to use the device,
 . typically 'ifconfig ethX up'.
*/
static int smc_open(struct net_device *dev);

/*
 . Our watchdog timed out. Called by the networking layer
*/
static void smc_timeout(struct net_device *dev);

/*
 . This is called by the kernel in response to 'ifconfig ethX down'.  It
 . is responsible for cleaning up everything that the open routine
 . does, and maybe putting the card into a powerdown state.
*/
static int smc_close(struct net_device *dev);

/*
 . This routine allows the proc file system to query the driver's
 . statistics.
*/
static struct net_device_stats * smc_query_statistics( struct net_device *dev);

/*
 . Finally, a call to set promiscuous mode ( for TCPDUMP and related
 . programs ) and multicast modes.
*/
static void smc_set_multicast_list(struct net_device *dev);

/*
 . CRC compute
 */
static int crc32( char * s, int length );

/*
 . Configures the PHY through the MII Management interface
*/
static void smc_phy_configure(struct net_device* dev);

/*---------------------------------------------------------------
 .
 . Interrupt level calls..
 .
 ----------------------------------------------------------------*/

/*
 . Handles the actual interrupt
*/
static void smc_interrupt(int irq, void *, struct pt_regs *regs);
/*
 . This is a separate procedure to handle the receipt of a packet, to
 . leave the interrupt code looking slightly cleaner
*/
inline static void smc_rcv( struct net_device *dev );
/*
 . This handles a TX interrupt, which is only called when an error
 . relating to a packet is sent.
*/
inline static void smc_tx( struct net_device * dev );

/*
 . This handles interrupts generated from PHY register 18
*/
static void smc_phy_interrupt(struct net_device* dev);

/*
 ------------------------------------------------------------
 .
 . Internal routines
 .
 ------------------------------------------------------------
*/

/*
 . Test if a given location contains a chip, trying to cause as
 . little damage as possible if it's not a SMC chip.
*/
static int smc_probe(struct net_device *dev, unsigned long ioaddr);

/*
 . A rather simple routine to print out a packet for debugging purposes.
*/
#if SMC_DEBUG > 2
static void print_packet( byte *, int );
#endif

#define tx_done(dev) 1

/* this is called to actually send the packet to the chip */
static void smc_hardware_send_packet( struct net_device * dev );

/* Since I am not sure if I will have enough room in the chip's ram
 . to store the packet, I call this routine, which either sends it
 . now, or generates an interrupt when the card is ready for the
 . packet */
static int  smc_wait_to_send_packet( struct sk_buff * skb, struct net_device *dev );

/* this does a soft reset on the device */
static void smc_reset( struct net_device* dev );

/* Enable Interrupts, Receive, and Transmit */
static void smc_enable( struct net_device *dev );

/* this puts the device in an inactive state */
static void smc_shutdown( unsigned long ioaddr );

/* This routine will find the IRQ of the driver if one is not
 . specified in the input to the device.  */
static int smc_findirq( unsigned long ioaddr );

/* Routines to Read and Write the PHY Registers across the
   MII Management Interface
*/

static word smc_read_phy_register(unsigned long ioaddr,
				  byte phyaddr, byte phyreg);
static void smc_write_phy_register(unsigned long ioaddr,
	byte phyaddr, byte phyreg, word phydata);

/*
  Initilizes our device's sysctl proc filesystem
*/

#ifdef CONFIG_SYSCTL
static void smc_sysctl_register(struct net_device *dev);
static void smc_sysctl_unregister(struct net_device *dev);
#endif /* CONFIG_SYSCTL */

/*
 . Function: smc_reset( struct net_device* dev )
 . Purpose:
 .  	This sets the SMC91111 chip to its normal state, hopefully from whatever
 . 	mess that any other DOS driver has put it in.
 .
 . Maybe I should reset more registers to defaults in here?  SOFTRST  should
 . do that for me.
 .
 . Method:
 .	1.  send a SOFT RESET
 .	2.  wait for it to finish
 .	3.  enable autorelease mode
 .	4.  reset the memory management unit
 .	5.  clear all interrupts
 .
*/
static void smc_reset( struct net_device* dev )
{
	//struct smc_local *lp 	= (struct smc_local *)dev->priv;
	unsigned long ioaddr = dev->base_addr;

	PRINTK2("%s:smc_reset\n", dev->name);

	/* This resets the registers mostly to defaults, but doesn't
	   affect EEPROM.  That seems unnecessary */
	SMC_SELECT_BANK( 0 );
	SMC_SET_RCR( RCR_SOFTRST );

	/* Setup the Configuration Register */
	/* This is necessary because the CONFIG_REG is not affected */
	/* by a soft reset */

	SMC_SELECT_BANK( 1 );
	SMC_SET_CONFIG( CONFIG_DEFAULT );

	/* Setup for fast accesses if requested */
	/* If the card/system can't handle it then there will */
	/* be no recovery except for a hard reset or power cycle */

	if (dev->dma)
		SMC_SET_CONFIG( SMC_GET_CONFIG() | CONFIG_NO_WAIT );

#ifdef POWER_DOWN
	/* Release from possible power-down state */
	/* Configuration register is not affected by Soft Reset */
	SMC_SELECT_BANK( 1 );
	SMC_SET_CONFIG( SMC_GET_CONFIG() | CONFIG_EPH_POWER_EN );
#endif

	SMC_SELECT_BANK( 0 );

	/* this should pause enough for the chip to be happy */
	mdelay(10);

	/* Disable transmit and receive functionality */
	SMC_SET_RCR( RCR_CLEAR );
	SMC_SET_TCR( TCR_CLEAR );

	/* set the control register to automatically
	   release successfully transmitted packets, to make the best
	   use out of our limited memory */
	SMC_SELECT_BANK( 1 );
	SMC_SET_CTL( SMC_GET_CTL() | CTL_AUTO_RELEASE );

	/* Reset the MMU */
	SMC_SELECT_BANK( 2 );
	SMC_SET_MMU_CMD( MC_RESET );

	/* Note:  It doesn't seem that waiting for the MMU busy is needed here,
	   but this is a place where future chipsets _COULD_ break.  Be wary
 	   of issuing another MMU command right after this */

	/* Disable all interrupts */
	SMC_SET_INT_MASK( 0 );
}

/*
 . Function: smc_enable
 . Purpose: let the chip talk to the outside work
 . Method:
 .	1.  Enable the transmitter
 .	2.  Enable the receiver
 .	3.  Enable interrupts
*/
static void smc_enable( struct net_device *dev )
{
	unsigned long ioaddr 	= dev->base_addr;
	struct smc_local *lp 	= (struct smc_local *)dev->priv;

	PRINTK2("%s:smc_enable\n", dev->name);

	SMC_SELECT_BANK( 0 );
	/* see the header file for options in TCR/RCR DEFAULT*/
	SMC_SET_TCR( lp->tcr_cur_mode );
	SMC_SET_RCR( lp->rcr_cur_mode );

	/* now, enable interrupts */
	SMC_SELECT_BANK( 2 );
	SMC_SET_INT_MASK( SMC_INTERRUPT_MASK );
}

/*
 . Function: smc_shutdown
 . Purpose:  closes down the SMC91xxx chip.
 . Method:
 .	1. zero the interrupt mask
 .	2. clear the enable receive flag
 .	3. clear the enable xmit flags
 .
 . TODO:
 .   (1) maybe utilize power down mode.
 .	Why not yet?  Because while the chip will go into power down mode,
 .	the manual says that it will wake up in response to any I/O requests
 .	in the register space.   Empirical results do not show this working.
*/
static void smc_shutdown( unsigned long ioaddr )
{
	PRINTK2(CARDNAME ":smc_shutdown\n");

	/* no more interrupts for me */
	SMC_SELECT_BANK( 2 );
	SMC_SET_INT_MASK( 0 );

	/* and tell the card to stay away from that nasty outside world */
	SMC_SELECT_BANK( 0 );
	SMC_SET_RCR( RCR_CLEAR );
	SMC_SET_TCR( TCR_CLEAR );

#ifdef POWER_DOWN
	/* finally, shut the chip down */
	SMC_SELECT_BANK( 1 );
	SMC_SET_CONFIG( SMC_GET_CONFIG() & ~CONFIG_EPH_POWER_EN );
#endif
}


/*
 . Function: smc_setmulticast( int ioaddr, int count, dev_mc_list * adds )
 . Purpose:
 .    This sets the internal hardware table to filter out unwanted multicast
 .    packets before they take up memory.
 .
 .    The SMC chip uses a hash table where the high 6 bits of the CRC of
 .    address are the offset into the table.  If that bit is 1, then the
 .    multicast packet is accepted.  Otherwise, it's dropped silently.
 .
 .    To use the 6 bits as an offset into the table, the high 3 bits are the
 .    number of the 8 bit register, while the low 3 bits are the bit within
 .    that register.
 .
 . This routine is based very heavily on the one provided by Peter Cammaert.
*/


static void smc_setmulticast( unsigned long ioaddr, int count,
			      struct dev_mc_list * addrs ) {
	int			i;
	unsigned char		multicast_table[ 8 ];
	struct dev_mc_list	* cur_addr;
	/* table for flipping the order of 3 bits */
	unsigned char invert3[] = { 0, 4, 2, 6, 1, 5, 3, 7 };

	PRINTK2(CARDNAME ":smc_setmulticast\n");

	/* start with a table of all zeros: reject all */
	memset( multicast_table, 0, sizeof( multicast_table ) );

	cur_addr = addrs;
	for ( i = 0; i < count ; i ++, cur_addr = cur_addr->next  ) {
		int position;

		/* do we have a pointer here? */
		if ( !cur_addr )
			break;
		/* make sure this is a multicast address - shouldn't this
		   be a given if we have it here ? */
		if ( !( *cur_addr->dmi_addr & 1 ) )
			continue;

		/* only use the low order bits */
		position = crc32( cur_addr->dmi_addr, 6 ) & 0x3f;

		/* do some messy swapping to put the bit in the right spot */
		multicast_table[invert3[position&7]] |=
					(1<<invert3[(position>>3)&7]);

	}
	/* now, the table can be loaded into the chipset */
	SMC_SELECT_BANK( 3 );
	SMC_SET_MCAST( multicast_table );
}

/*
  Finds the CRC32 of a set of bytes.
  Again, from Peter Cammaert's code.
*/
static int crc32( char * s, int length ) {
	/* indices */
	int perByte;
	int perBit;
	/* crc polynomial for Ethernet */
	const unsigned long poly = 0xedb88320;
	/* crc value - preinitialized to all 1's */
	unsigned long crc_value = 0xffffffff;

	for ( perByte = 0; perByte < length; perByte ++ ) {
		unsigned char	c;

		c = *(s++);
		for ( perBit = 0; perBit < 8; perBit++ ) {
			crc_value = (crc_value>>1)^
				(((crc_value^c)&0x01)?poly:0);
			c >>= 1;
		}
	}
	return	crc_value;
}


/*
 . Function: smc_wait_to_send_packet( struct sk_buff * skb, struct net_device * )
 . Purpose:
 .    Attempt to allocate memory for a packet, if chip-memory is not
 .    available, then tell the card to generate an interrupt when it
 .    is available.
 .
 . Algorithm:
 .
 . o	if the saved_skb is not currently null, then drop this packet
 .	on the floor.  This should never happen, because of TBUSY.
 . o	if the saved_skb is null, then replace it with the current packet,
 . o	See if I can sending it now.
 . o 	(NO): Enable interrupts and let the interrupt handler deal with it.
 . o	(YES):Send it now.
*/
static int smc_wait_to_send_packet( struct sk_buff * skb, struct net_device * dev )
{
	struct smc_local *lp 	= (struct smc_local *)dev->priv;
	unsigned long ioaddr 	= dev->base_addr;
	word 			length;
	unsigned short 		numPages;
	word			time_out;
	word			status;

	PRINTK3("%s:smc_wait_to_send_packet\n", dev->name);

	netif_stop_queue(dev);
	/* Well, I want to send the packet.. but I don't know
	   if I can send it right now...  */

	if ( lp->saved_skb) {
		/* THIS SHOULD NEVER HAPPEN. */
		lp->stats.tx_aborted_errors++;
		printk("%s: Bad Craziness - sent packet while busy.\n",
			dev->name);
		return 1;
	}
	lp->saved_skb = skb;

	length = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;


	/*
	** The MMU wants the number of pages to be the number of 256 bytes
	** 'pages', minus 1 ( since a packet can't ever have 0 pages :) )
	**
	** The 91C111 ignores the size bits, but the code is left intact
	** for backwards and future compatibility.
	**
	** Pkt size for allocating is data length +6 (for additional status
	** words, length and ctl!)
	**
	** If odd size then last byte is included in this header.
	*/
	numPages =   ((length & 0xfffe) + 6);
	numPages >>= 8; // Divide by 256

	if (numPages > 7 ) {
		printk("%s: Far too big packet error. \n", dev->name);
		/* freeing the packet is a good thing here... but should
		 . any packets of this size get down here?   */
		dev_kfree_skb (skb);
		lp->saved_skb = NULL;
		/* this IS an error, but, i don't want the skb saved */
		netif_wake_queue(dev);
		return 0;
	}
	/* either way, a packet is waiting now */
	lp->packets_waiting++;

	/* now, try to allocate the memory */
	SMC_SELECT_BANK( 2 );
	SMC_SET_MMU_CMD( MC_ALLOC | numPages );
	/*
 	. Performance Hack
	.
 	. wait a short amount of time.. if I can send a packet now, I send
	. it now.  Otherwise, I enable an interrupt and wait for one to be
	. available.
	.
	. I could have handled this a slightly different way, by checking to
	. see if any memory was available in the FREE MEMORY register.  However,
	. either way, I need to generate an allocation, and the allocation works
	. no matter what, so I saw no point in checking free memory.
	*/
	time_out = MEMORY_WAIT_TIME;
	do {
		status = SMC_GET_INT();
		if ( status & IM_ALLOC_INT ) {
			/* acknowledge the interrupt */
			SMC_ACK_INT( IM_ALLOC_INT );
  			break;
		}
   	} while ( -- time_out );

   	if ( !time_out ) {
		/* oh well, wait until the chip finds memory later */
		SMC_ENABLE_INT( IM_ALLOC_INT );

		/* Check the status bit one more time just in case */
		/* it snuk in between the time we last checked it */
		/* and when we set the interrupt bit */
		status = SMC_GET_INT();
		if ( !(status & IM_ALLOC_INT) ) {
      			PRINTK2("%s: memory allocation deferred. \n",
				dev->name);
			/* it's deferred, but I'll handle it later */
      			return 0;
			}

		/* Looks like it did sneak in, so disable */
		/* the interrupt */
		SMC_DISABLE_INT( IM_ALLOC_INT );
   	}
	/* or YES! I can send the packet now.. */
	smc_hardware_send_packet(dev);
	netif_wake_queue(dev);
	return 0;
}

/*
 . Function:  smc_hardware_send_packet(struct net_device * )
 . Purpose:
 .	This sends the actual packet to the SMC9xxx chip.
 .
 . Algorithm:
 . 	First, see if a saved_skb is available.
 .		( this should NOT be called if there is no 'saved_skb'
 .	Now, find the packet number that the chip allocated
 .	Point the data pointers at it in memory
 .	Set the length word in the chip's memory
 .	Dump the packet to chip memory
 .	Check if a last byte is needed ( odd length packet )
 .		if so, set the control flag right
 . 	Tell the card to send it
 .	Enable the transmit interrupt, so I know if it failed
 . 	Free the kernel data if I actually sent it.
*/
static void smc_hardware_send_packet( struct net_device * dev )
{
	struct smc_local *lp = (struct smc_local *)dev->priv;
	byte	 		packet_no;
	struct sk_buff * 	skb = lp->saved_skb;
	word			length;
	unsigned long		ioaddr;
	byte			* buf;

	PRINTK3("%s:smc_hardware_send_packet\n", dev->name);

	ioaddr = dev->base_addr;

	if ( !skb ) {
		PRINTK("%s: In XMIT with no packet to send \n", dev->name);
		return;
	}
	length = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
	buf = skb->data;

	/* If I get here, I _know_ there is a packet slot waiting for me */
	packet_no = SMC_GET_AR();
	if ( packet_no & AR_FAILED ) {
		/* or isn't there?  BAD CHIP! */
		printk(KERN_DEBUG "%s: Memory allocation failed. \n",
			dev->name);
		dev_kfree_skb_any(skb);
		lp->saved_skb = NULL;
		netif_wake_queue(dev);
		return;
	}

	/* we have a packet address, so tell the card to use it */
	SMC_SET_PN( packet_no );

	/* point to the beginning of the packet */
	SMC_SET_PTR( PTR_AUTOINC );

   	PRINTK3("%s: Trying to xmit packet of length %x\n",
		dev->name, length);

#if SMC_DEBUG > 2
	printk("Transmitting Packet\n");
	print_packet( buf, length );
#endif

	/* send the packet length ( +6 for status, length and ctl byte )
 	   and the status word ( set to zeros ) */
#ifdef CONFIG_SMC91111_USE_32_BIT
	SMC_outl(  (length +6 ) << 16 , ioaddr + DATA_REG );
#else
	SMC_outw( 0, ioaddr + DATA_REG );
	/* send the packet length ( +6 for status words, length, and ctl*/
	SMC_outw( (length+6), ioaddr + DATA_REG );
#endif

	/* send the actual data
	 . I _think_ it's faster to send the longs first, and then
	 . mop up by sending the last word.  It depends heavily
 	 . on alignment, at least on the 486.  Maybe it would be
 	 . a good idea to check which is optimal?  But that could take
	 . almost as much time as is saved?
	*/
#ifdef CONFIG_SMC91111_USE_32_BIT
	SMC_outsl(ioaddr + DATA_REG, buf,  length >> 2 );
	if ( length & 0x2  )
		SMC_outw(*((word *)(buf + (length & 0xFFFFFFFC))),ioaddr +
				DATA_REG);
#else
	SMC_outsw(ioaddr + DATA_REG , buf, (length ) >> 1);
#endif // CONFIG_SMC91111_USE_32_BIT

	/* Send the last byte, if there is one.   */
	if ( (length & 1) == 0 ) {
		SMC_outw( 0, ioaddr + DATA_REG );
	} else {
		SMC_outw( 0x2000 | buf[length -1 ], ioaddr + DATA_REG );
	}

	/* enable the interrupts */
	SMC_ENABLE_INT( (IM_TX_INT | IM_TX_EMPTY_INT) );

	/* and let the chipset deal with it */
	SMC_SET_MMU_CMD( MC_ENQUEUE );

	PRINTK2("%s: Sent packet of length %d \n", dev->name, length);

	lp->saved_skb = NULL;
	dev_kfree_skb_any (skb);

	dev->trans_start = jiffies;

	/* we can send another packet */
	netif_wake_queue(dev);

	return;
}

/*-------------------------------------------------------------------------
 |
 | smc_init( void )
 |   Input parameters:
 |	dev->base_addr == 0, try to find all possible locations
 |	dev->base_addr > 0x1ff, this is the address to check
 |	dev->base_addr == <anything else>, return failure code
 |
 |   Output:
 |	0 --> there is a device
 |	anything else, error
 |
 ---------------------------------------------------------------------------
*/
static int __init smc_init( void )
{
	int rtn;

	PRINTK2(CARDNAME ":smc_init\n");

#ifdef MODULE
	if (io == -1)
		printk(KERN_WARNING
		CARDNAME": You shouldn't use auto-probing with insmod!\n" );
#endif

	if (global_dev) {
		printk(CARDNAME ": already initialized.\n");
		return -EBUSY;
	}

	global_dev = init_etherdev(0, sizeof(struct smc_local));
	if (!global_dev) {
		printk(CARDNAME ": could not allocate device.\n");
		return -ENODEV;
	}
	SET_MODULE_OWNER(global_dev);

	/* copy the parameters from insmod into the device structure */
	if (io != -1)
		global_dev->base_addr	= io;
	if (irq != -1)
		global_dev->irq	= irq;
	global_dev->dma		= nowait; // Use DMA field for nowait

#ifdef CONFIG_ISA
	/*  try a specific location */
	if (global_dev->base_addr > 0x1ff)
		rtn = smc_probe(global_dev, global_dev->base_addr);
	else if (global_dev->base_addr != 0)
		rtn = -ENXIO;
	else {
		int i;

		/* check every ethernet address */
		for (i = 0; smc_portlist[i]; i++) {
			rtn = smc_probe(global_dev, smc_portlist[i]);
			if (rtn == 0)
				break;
		}
	}
#else
	if (global_dev->base_addr == -1) {
		printk(KERN_WARNING
		CARDNAME": SMC91111_BASE_ADDR not set!\n" );
		rtn = -ENXIO;
	}
	else
		rtn = smc_probe(global_dev,
				(int)ioremap(global_dev->base_addr,
					     SMC_IO_EXTENT));
#endif

	if (rtn != 0) {
		printk(CARDNAME ": not found.\n");
		/* couldn't find anything */
#ifndef CONFIG_ISA
		iounmap((void *)global_dev->base_addr);
#endif
		kfree(global_dev->priv);
		unregister_netdev(global_dev);
		kfree(global_dev);
	}

	return rtn;
}

/*----------------------------------------------------------------------
 . smc_findirq
 .
 . This routine has a simple purpose -- make the SMC chip generate an
 . interrupt, so an auto-detect routine can detect it, and find the IRQ,
 ------------------------------------------------------------------------
*/
int __init smc_findirq( unsigned long ioaddr )
{
	int	timeout = 20;
	unsigned long cookie;

	PRINTK2(CARDNAME ":smc_findirq\n");

	/* I have to do a STI() here, because this is called from
	   a routine that does an CLI during this process, making it
	   rather difficult to get interrupts for auto detection */
	sti();

	cookie = probe_irq_on();

	/*
	 * What I try to do here is trigger an ALLOC_INT. This is done
	 * by allocating a small chunk of memory, which will give an interrupt
	 * when done.
	 */


	SMC_SELECT_BANK(2);
	/* enable ALLOCation interrupts ONLY */
	SMC_SET_INT_MASK( IM_ALLOC_INT );

	/*
 	 . Allocate 512 bytes of memory.  Note that the chip was just
	 . reset so all the memory is available
	*/
	SMC_SET_MMU_CMD( MC_ALLOC | 1 );

	/*
	 . Wait until positive that the interrupt has been generated
	*/
	while ( timeout ) {
		byte	int_status;

		int_status = SMC_GET_INT();

		if ( int_status & IM_ALLOC_INT )
			break;		/* got the interrupt */
		timeout--;
	}

	/* there is really nothing that I can do here if timeout fails,
	   as autoirq_report will return a 0 anyway, which is what I
	   want in this case.   Plus, the clean up is needed in both
	   cases.  */

	/* DELAY HERE!
	   On a fast machine, the status might change before the interrupt
	   is given to the processor.  This means that the interrupt was
	   never detected, and autoirq_report fails to report anything.
	   This should fix autoirq_* problems.
	*/
	mdelay(10);

	/* and disable all interrupts again */
	SMC_SET_INT_MASK( 0 );

	/* clear hardware interrupts again, because that's how it
	   was when I was called... */
	cli();

	/* and return what I found */
	return probe_irq_off(cookie);
}

/*----------------------------------------------------------------------
 . Function: smc_probe( unsigned long ioaddr )
 .
 . Purpose:
 .	Tests to see if a given ioaddr points to an SMC91111 chip.
 .	Returns a 0 on success
 .
 . Algorithm:
 .	(1) see if the high byte of BANK_SELECT is 0x33
 . 	(2) compare the ioaddr with the base register's address
 .	(3) see if I recognize the chip ID in the appropriate register
 .
 .---------------------------------------------------------------------
 */
/*---------------------------------------------------------------
 . Here I do typical initialization tasks.
 .
 . o  Initialize the structure if needed
 . o  print out my vanity message if not done so already
 . o  print out what type of hardware is detected
 . o  print out the ethernet address
 . o  find the IRQ
 . o  set up my private data
 . o  configure the dev structure with my subroutines
 . o  actually GRAB the irq.
 . o  GRAB the region
 .-----------------------------------------------------------------
*/
static int __init smc_probe(struct net_device *dev, unsigned long ioaddr)
{
	int i, memory, retval;
	static unsigned version_printed;
	unsigned int	bank;

	const char *version_string;
	const char *if_string;

	/* registers */
	word	revision_register;
	word  base_address_register;
	word memory_info_register;

	PRINTK2(CARDNAME ":smc_probe\n");

	/* Grab the region so that no one else tries to probe our ioports. */
	if (!request_region(ioaddr, SMC_IO_EXTENT, dev->name))
		return -EBUSY;

	/* First, see if the high byte is 0x33 */
	bank = SMC_CURRENT_BANK();
	if ( (bank & 0xFF00) != 0x3300 ) {
		if ( (bank & 0xFF) == 0x33 ) {
			printk(CARDNAME
			       ": Detected possible byte-swapped interface"
			       " at IOADDR %lx\n", ioaddr);
		}
		retval = -ENODEV;
		goto err_out;
	}
	/* The above MIGHT indicate a device, but I need to write to further
 	 	test this.  */
	SMC_SELECT_BANK(0);
	bank = SMC_CURRENT_BANK();
	if ( (bank & 0xFF00 ) != 0x3300 ) {
		retval = -ENODEV;
		goto err_out;
	}
	/* well, we've already written once, so hopefully another time won't
 	   hurt.  This time, I need to switch the bank register to bank 1,
	   so I can access the base address register */
	SMC_SELECT_BANK(1);
	base_address_register = SMC_GET_BASE();
	base_address_register = ((base_address_register & 0xE000)
				 | ((base_address_register & 0x1F00) >> 3));
	if ( (ioaddr & (PAGE_SIZE-1)) != base_address_register )  {
		printk(CARDNAME ": IOADDR %lx doesn't match configuration (%x)."
			"Probably not a SMC chip\n",
			ioaddr, base_address_register );
		/* well, the base address register didn't match.  Must not have
		   been a SMC chip after all. */
		retval = -ENODEV;
		goto err_out;
	}

	/*  check if the revision register is something that I recognize.
	    These might need to be added to later, as future revisions
	    could be added.  */
	SMC_SELECT_BANK(3);
	revision_register  = SMC_GET_REV();
	if ( !chip_ids[ ( revision_register  >> 4 ) & 0xF  ] ) {
		/* I don't recognize this chip, so... */
		printk(CARDNAME ": IO %lx: Unrecognized revision register:"
			" %x, Contact author. \n",
			ioaddr, revision_register );

		retval = -ENODEV;
		goto err_out;
	}

	/* at this point I'll assume that the chip is an SMC9xxx.
	   It might be prudent to check a listing of MAC addresses
	   against the hardware address, or do some other tests. */

	if (version_printed++ == 0)
		printk("%s", version);

	/* fill in some of the fields */
	dev->base_addr = ioaddr;

	/*
 	 . Get the MAC address ( bank 1, regs 4 - 9 )
	*/
	SMC_SELECT_BANK( 1 );
	for ( i = 0; i < 6; i += 2 ) {
		word	address;

		address = SMC_inw( ioaddr + ADDR0_REG + i  );
		dev->dev_addr[ i + 1] = address >> 8;
		dev->dev_addr[ i ] = address & 0xFF;
	}

	/* get the memory information */

	SMC_SELECT_BANK( 0 );
	memory_info_register = SMC_GET_MIR();
	memory = memory_info_register & (word)0x00ff;
	memory *= LAN91C111_MEMORY_MULTIPLIER;

	/*
	 Now, I want to find out more about the chip.  This is sort of
 	 redundant, but it's cleaner to have it in both, rather than having
 	 one VERY long probe procedure.
	*/
	SMC_SELECT_BANK(3);
	revision_register  = SMC_GET_REV();
	version_string = chip_ids[ ( revision_register  >> 4 ) & 0xF  ];
	if ( !version_string ) {
		/* I shouldn't get here because this call was done before.... */
		retval = -ENODEV;
		goto err_out;
	}


	/* now, reset the chip, and put it into a known state */
	smc_reset( dev );

	/*
	 . If dev->irq is 0, then the device has to be banged on to see
	 . what the IRQ is.
 	 .
	 . This banging doesn't always detect the IRQ, for unknown reasons.
	 . a workaround is to reset the chip and try again.
	 .
	 . Interestingly, the DOS packet driver *SETS* the IRQ on the card to
	 . be what is requested on the command line.   I don't do that, mostly
	 . because the card that I have uses a non-standard method of accessing
	 . the IRQs, and because this _should_ work in most configurations.
	 .
	 . Specifying an IRQ is done with the assumption that the user knows
	 . what (s)he is doing.  No checking is done!!!!
 	 .
	*/
	if ( dev->irq < 2 ) {
		int	trials;

		trials = 3;
		while ( trials-- ) {
			dev->irq = smc_findirq( ioaddr );
			if ( dev->irq )
				break;
			/* kick the card and try again */
			smc_reset( dev );
		}
	}
	if (dev->irq == 0 ) {
		printk("%s: Couldn't autodetect your IRQ. Use irq=xx.\n",
			dev->name);
		retval = -ENODEV;
		goto err_out;
	}
	if (dev->irq == 2) {
		/* Fixup for users that don't know that IRQ 2 is really IRQ 9,
		 * or don't know which one to set.
		 */
		dev->irq = 9;
	}

	/* now, print out the card info, in a short format.. */

	printk("%s: %s(r:%d) at %#lx IRQ:%d\n", dev->name,
	       version_string, revision_register & 0xF, ioaddr, dev->irq );
	printk(" INTF:%s MEM:%db NOWAIT:%d", if_string, memory, dev->dma );
	/*
	 . Print the Ethernet address
	*/
	printk("  ADDR ");
	for (i = 0; i < 5; i++)
		printk("%2.2x:", dev->dev_addr[i] );
	printk("%2.2x \n", dev->dev_addr[5] );

	/* set the private data to zero by default */
	memset(dev->priv, 0, sizeof(struct smc_local));

	/* Fill in the fields of the device structure with ethernet values. */
	ether_setup(dev);

	/* Grab the IRQ */
      	retval = request_irq(dev->irq, &smc_interrupt, 0, dev->name, dev);
      	if (retval) {
  	  	goto err_out;
      	}

	dev->open		        = smc_open;
	dev->stop		        = smc_close;
	dev->hard_start_xmit    	= smc_wait_to_send_packet;
	dev->tx_timeout		    	= smc_timeout;
	dev->watchdog_timeo		= HZ/20;
	dev->get_stats			= smc_query_statistics;
	dev->set_multicast_list 	= smc_set_multicast_list;

	return 0;

err_out:
	release_region(ioaddr, SMC_IO_EXTENT);
	return retval;
}

#if SMC_DEBUG > 2
static void print_packet( byte * buf, int length )
{
#if 1
#if SMC_DEBUG > 3
	int i;
	int remainder;
	int lines;
#endif

	printk("Packet of length %d \n", length );

#if SMC_DEBUG > 3
	lines = length / 16;
	remainder = length % 16;

	for ( i = 0; i < lines ; i ++ ) {
		int cur;

		for ( cur = 0; cur < 8; cur ++ ) {
			byte a, b;

			a = *(buf ++ );
			b = *(buf ++ );
			printk("%02x%02x ", a, b );
		}
		printk("\n");
	}
	for ( i = 0; i < remainder/2 ; i++ ) {
		byte a, b;

		a = *(buf ++ );
		b = *(buf ++ );
		printk("%02x%02x ", a, b );
	}
	printk("\n");
#endif
#endif
}
#endif


/*
 * Open and Initialize the board
 *
 * Set up everything, reset the card, etc ..
 *
 */
static int smc_open(struct net_device *dev)
{
	struct smc_local *lp 	= (struct smc_local *)dev->priv;
	unsigned long ioaddr	= dev->base_addr;
	int	i;	/* used to set hw ethernet address */

	PRINTK2("%s:smc_open\n", dev->name);

	/* clear out all the junk that was put here before... */
	memset(dev->priv, 0, sizeof(struct smc_local));

	// Setup the default Register Modes
	lp->tcr_cur_mode = TCR_DEFAULT;
	lp->rcr_cur_mode = RCR_DEFAULT;
	lp->rpc_cur_mode = RPC_DEFAULT;

	// Set default parameters (files)
	lp->ctl_swfdup = 0;
	lp->ctl_ephloop = 0;
	lp->ctl_miiop = 0;
	lp->ctl_autoneg = 1;
	lp->ctl_rfduplx = 1;
	lp->ctl_rspeed = 100;
	lp->ctl_afduplx = 1;
	lp->ctl_aspeed = 100;
	lp->ctl_lnkfail = 1;
	lp->ctl_forcol = 0;
	lp->ctl_filtcar = 0;

	/* reset the hardware */

	smc_reset( dev );
	smc_enable( dev );

	/* Configure the PHY */
	smc_phy_configure(dev);

	/*
  		According to Becker, I have to set the hardware address
		at this point, because the (l)user can set it with an
		ioctl.  Easily done...
	*/
	SMC_SELECT_BANK( 1 );
	for ( i = 0; i < 6; i += 2 ) {
		word	address;

		address = dev->dev_addr[ i + 1 ] << 8 ;
		address  |= dev->dev_addr[ i ];
		SMC_outw( address, ioaddr + ADDR0_REG + i );
	}

#ifdef CONFIG_SYSCTL
	smc_sysctl_register(dev);
#endif /* CONFIG_SYSCTL */

	netif_start_queue(dev);
	return 0;
}

/*--------------------------------------------------------
 . Called by the kernel to send a packet out into the void
 . of the net.  This routine is largely based on
 . skeleton.c, from Becker.
 .--------------------------------------------------------
*/
static void smc_timeout(struct net_device *dev)
{
	struct smc_local *lp 	= (struct smc_local *)dev->priv;

	PRINTK3("%s:smc_timeout\n", dev->name);

	/* If we get here, some higher level has decided we are broken.
	   There should really be a "kick me" function call instead. */
	printk(KERN_WARNING "%s: transmit timed out, %s?\n",
		dev->name, tx_done(dev) ? "IRQ conflict" :
		"network cable problem");
	/* "kick" the adaptor */
	smc_reset( dev );
	smc_enable( dev );

#if 0
	/* Reconfiguring the PHY doesn't seem like a bad idea here, but
	 * it introduced a problem.  Now that this is a timeout routine,
	 * we are getting called from within an interrupt context.
	 * smc_phy_configure() calls smc_wait_ms() which calls
	 * schedule_timeout() which calls schedule().  When schedule()
	 * is called from an interrupt context, it prints out
	 * "Scheduling in interrupt" and then calls BUG().  This is
	 * obviously not desirable.  This was worked around by removing
	 * the call to smc_phy_configure() here because it didn't seem
	 * absolutely necessary.  Ultimately, if smc_wait_ms() is
	 * supposed to be usable from an interrupt context (which it
	 * looks like it thinks it should handle), it should be fixed.
	 */
	/* Reconfigure the PHY */
	smc_phy_configure(dev);
#endif
	dev->trans_start = jiffies;
	/* clear anything saved */
	if (lp->saved_skb != NULL) {
		dev_kfree_skb (lp->saved_skb);
		lp->saved_skb = NULL;
	}
	((struct smc_local *)dev->priv)->saved_skb = NULL;
	netif_wake_queue(dev);
}

/*--------------------------------------------------------------------
 .
 . This is the main routine of the driver, to handle the device when
 . it needs some attention.
 .
 . So:
 .   first, save state of the chipset
 .   branch off into routines to handle each case, and acknowledge
 .	    each to the interrupt register
 .   and finally restore state.
 .
 ---------------------------------------------------------------------*/
static void smc_interrupt(int irq, void * dev_id,  struct pt_regs * regs)
{
	struct net_device *dev 	= dev_id;
	unsigned long ioaddr	= dev->base_addr;
	struct smc_local *lp 	= (struct smc_local *)dev->priv;

	byte	status;
	word	card_stats;
	byte	mask;
	int	timeout;
	/* state registers */
	word	saved_bank;
	word	saved_pointer;

	PRINTK3("%s: SMC interrupt started \n", dev->name);

	if (dev == NULL) {
		printk(KERN_WARNING "%s: irq %d for unknown device.\n",
			dev->name, irq);
		return;
	}

	saved_bank = SMC_CURRENT_BANK();

	SMC_SELECT_BANK(2);
	saved_pointer = SMC_GET_PTR();

	/* read the interrupt mask register */
	mask = SMC_GET_INT_MASK();

	/* disable all interrupts */
	SMC_SET_INT_MASK( 0 );

	/* set a timeout value, so I don't stay here forever */
	timeout = 4;

	PRINTK2(KERN_WARNING "%s: MASK IS %x \n", dev->name, mask);
	do {
		/* read the status flag, and mask it */
		status = SMC_GET_INT() & mask;
		if (!status )
			break;

		PRINTK3(KERN_WARNING "%s: Handling interrupt status %x \n",
			dev->name, status);

		if (status & IM_RCV_INT) {
			/* Got a packet(s). */
			PRINTK2(KERN_WARNING
				"%s: Receive Interrupt\n", dev->name);
			smc_rcv(dev);
		} else if (status & IM_TX_INT ) {
			PRINTK2(KERN_WARNING "%s: TX ERROR handled\n",
				dev->name);
			smc_tx(dev);
			// Acknowledge the interrupt
			SMC_ACK_INT( IM_TX_INT );
		} else if (status & IM_TX_EMPTY_INT ) {
			/* update stats */
			SMC_SELECT_BANK( 0 );
			card_stats = SMC_GET_COUNTER();
			/* single collisions */
			lp->stats.collisions += card_stats & 0xF;
			card_stats >>= 4;
			/* multiple collisions */
			lp->stats.collisions += card_stats & 0xF;

			/* these are for when linux supports these statistics */

			SMC_SELECT_BANK( 2 );
			PRINTK2(KERN_WARNING "%s: TX_BUFFER_EMPTY handled\n",
				dev->name);
			// Acknowledge the interrupt
			SMC_ACK_INT( IM_TX_EMPTY_INT );
			mask &= ~IM_TX_EMPTY_INT;
			lp->stats.tx_packets += lp->packets_waiting;
			lp->packets_waiting = 0;

		} else if (status & IM_ALLOC_INT ) {
			PRINTK2(KERN_DEBUG "%s: Allocation interrupt \n",
				dev->name);
			/* clear this interrupt so it doesn't happen again */
			mask &= ~IM_ALLOC_INT;

			smc_hardware_send_packet( dev );

			/* enable xmit interrupts based on this */
			mask |= ( IM_TX_EMPTY_INT | IM_TX_INT );

			/* and let the card send more packets to me */
			netif_wake_queue(dev);

			PRINTK2("%s: Handoff done successfully.\n",
				dev->name);
		} else if (status & IM_RX_OVRN_INT ) {
			lp->stats.rx_errors++;
			lp->stats.rx_fifo_errors++;
			// Acknowledge the interrupt
			SMC_ACK_INT( IM_RX_OVRN_INT );
		} else if (status & IM_EPH_INT ) {
			PRINTK("%s: UNSUPPORTED: EPH INTERRUPT \n",
				dev->name);
		} else if (status & IM_MDINT ) {
			smc_phy_interrupt(dev);
			// Acknowledge the interrupt
			SMC_ACK_INT( IM_MDINT );
		} else if (status & IM_ERCV_INT ) {
			PRINTK("%s: UNSUPPORTED: ERCV INTERRUPT \n",
				dev->name);
			// Acknowledge the interrupt
			SMC_ACK_INT( IM_ERCV_INT );
		}
	} while ( timeout -- );


	/* restore register states */

	SMC_SELECT_BANK( 2 );

	SMC_SET_INT_MASK( mask );

	PRINTK3( KERN_WARNING "%s: MASK is now %x \n", dev->name, mask);
	SMC_SET_PTR( saved_pointer );

	SMC_SELECT_BANK( saved_bank );

	PRINTK3("%s: Interrupt done\n", dev->name);
	return;
}

/*-------------------------------------------------------------
 .
 . smc_rcv -  receive a packet from the card
 .
 . There is ( at least ) a packet waiting to be read from
 . chip-memory.
 .
 . o Read the status
 . o If an error, record it
 . o otherwise, read in the packet
 --------------------------------------------------------------
*/
static void smc_rcv(struct net_device *dev)
{
	struct smc_local *lp = (struct smc_local *)dev->priv;
	unsigned long ioaddr  = dev->base_addr;
	int 	packet_number;
	word	status;
	word	packet_length;

	PRINTK3("%s:smc_rcv\n", dev->name);

	/* assume bank 2 */

	packet_number = SMC_GET_RXFIFO();

	if ( packet_number & RXFIFO_REMPTY ) {

		/* we got called , but nothing was on the FIFO */
		PRINTK("%s: WARNING: smc_rcv with nothing on FIFO. \n",
			dev->name);
		/* don't need to restore anything */
		return;
	}

	/*  start reading from the start of the packet */
	SMC_SET_PTR( PTR_READ | PTR_RCV | PTR_AUTOINC );

	/* First two words are status and packet_length */
	status 		= SMC_inw( ioaddr + DATA_REG );
	packet_length 	= SMC_inw( ioaddr + DATA_REG );

	packet_length &= 0x07ff;  /* mask off top bits */

	PRINTK2("RCV: STATUS %4x LENGTH %4x\n", status, packet_length );

	if ( !(status & RS_ERRORS ) ){
		/* do stuff to make a new packet */
		struct sk_buff  * skb;
		byte		* data;

		/* set multicast stats */
		if ( status & RS_MULTICAST )
			lp->stats.multicast++;

		// Allocate enough memory for entire receive frame, to be safe
		skb = dev_alloc_skb( packet_length );

		/* Adjust for having already read the first two words */
		packet_length -= 4;

		if ( skb == NULL ) {
			printk(KERN_NOTICE "%s: Low memory, packet dropped.\n",
				dev->name);
			lp->stats.rx_dropped++;
			goto done;
		}

		/*
		 ! This should work without alignment, but it could be
		 ! in the worse case
		*/

		skb_reserve( skb, 2 );   /* 16 bit alignment */

		skb->dev = dev;

		// set odd length for bug in LAN91C111,
		// which never sets RS_ODDFRAME
		data = skb_put( skb, packet_length + 1 );

#ifdef CONFIG_SMC91111_USE_32_BIT
		PRINTK3(" Reading %d dwords (and %d bytes) \n",
			packet_length >> 2, packet_length & 3 );
		/* QUESTION:  Like in the TX routine, do I want
		   to send the DWORDs or the bytes first, or some
		   mixture.  A mixture might improve already slow PIO
		   performance  */
		SMC_insl(ioaddr + DATA_REG , data, packet_length >> 2 );
		/* read the left over bytes */
#ifdef CONFIG_SMC91111_USE_8_BIT
		SMC_insb( ioaddr + DATA_REG, data + (packet_length & 0xFFFFFC),
			  packet_length & 0x3  );
#else
		if (packet_length & 0x3)
		{
			unsigned long remaining_data;
			insl(ioaddr + DATA_REG , &remaining_data, 1);
			memcpy(data + (packet_length & 0xFFFFFC),
			       &remaining_data, packet_length & 0x3  );
		}
#endif // CONFIG_SMC91111_USE_8_BIT
#else
		PRINTK3(" Reading %d words and %d byte(s) \n",
			(packet_length >> 1 ), packet_length & 1 );
		SMC_insw(ioaddr + DATA_REG , data, packet_length >> 1);

#endif // CONFIG_SMC91111_USE_32_BIT

#if	SMC_DEBUG > 2
		printk("Receiving Packet\n");
		print_packet( data, packet_length );
#endif

		skb->protocol = eth_type_trans(skb, dev );
		netif_rx(skb);
		dev->last_rx = jiffies;
		lp->stats.rx_packets++;
		lp->stats.rx_bytes += packet_length;
	} else {
		/* error ... */
		lp->stats.rx_errors++;

		if ( status & RS_ALGNERR )  lp->stats.rx_frame_errors++;
		if ( status & (RS_TOOSHORT | RS_TOOLONG ) )
			lp->stats.rx_length_errors++;
		if ( status & RS_BADCRC)	lp->stats.rx_crc_errors++;
	}

	while ( SMC_GET_MMU_CMD() & MC_BUSY )
		udelay(1); // Wait until not busy

done:
	/*  error or good, tell the card to get rid of this packet */
	SMC_SET_MMU_CMD( MC_RELEASE );
}


/*************************************************************************
 . smc_tx
 .
 . Purpose:  Handle a transmit error message.   This will only be called
 .   when an error, because of the AUTO_RELEASE mode.
 .
 . Algorithm:
 .	Save pointer and packet no
 .	Get the packet no from the top of the queue
 .	check if it's valid ( if not, is this an error??? )
 .	read the status word
 .	record the error
 .	( resend?  Not really, since we don't want old packets around )
 .	Restore saved values
 ************************************************************************/
static void smc_tx( struct net_device * dev )
{
	unsigned long  ioaddr = dev->base_addr;
	struct smc_local *lp = (struct smc_local *)dev->priv;
	byte saved_packet;
	byte packet_no;
	word tx_status;


	PRINTK3("%s:smc_tx\n", dev->name);

	/* assume bank 2  */

	saved_packet = SMC_GET_PN();
	packet_no = SMC_GET_RXFIFO();
	packet_no &= 0x7F;

	/* If the TX FIFO is empty then nothing to do */
	if ( packet_no & TXFIFO_TEMPTY )
		return;

	/* select this as the packet to read from */
	SMC_SET_PN( packet_no );

	/* read the first word (status word) from this packet */
	SMC_SET_PTR( PTR_AUTOINC | PTR_READ );

	tx_status = SMC_inw( ioaddr + DATA_REG );
	PRINTK3("%s: TX DONE STATUS: %4x \n", dev->name, tx_status);

	lp->stats.tx_errors++;
	if ( tx_status & TS_LOSTCAR ) lp->stats.tx_carrier_errors++;
	if ( tx_status & TS_LATCOL  ) {
		printk(KERN_DEBUG
			"%s: Late collision occurred on last xmit.\n",
			dev->name);
		lp->stats.tx_window_errors++;
		lp->ctl_forcol = 0; // Reset forced collsion
	}
#if 0
	if ( tx_status & TS_16COL ) { ... }
#endif

	if ( tx_status & TS_SUCCESS ) {
		printk("%s: Successful packet caused interrupt \n", dev->name);
	}
	/* re-enable transmit */
	SMC_SELECT_BANK( 0 );
	SMC_SET_TCR( SMC_GET_TCR() | TCR_ENABLE );

	/* kill the packet */
	SMC_SELECT_BANK( 2 );
	SMC_SET_MMU_CMD( MC_FREEPKT );

	/* one less packet waiting for me */
	lp->packets_waiting--;

	/* Don't change Packet Number Reg until busy bit is cleared */
	/* Per LAN91C111 Spec, Page 50 */
	while ( SMC_GET_MMU_CMD() & MC_BUSY );

	SMC_SET_PN( saved_packet );
	return;
}


/*----------------------------------------------------
 . smc_close
 .
 . this makes the board clean up everything that it can
 . and not talk to the outside world.   Caused by
 . an 'ifconfig ethX down'
 .
 -----------------------------------------------------*/
static int smc_close(struct net_device *dev)
{
	PRINTK2("%s:smc_close\n", dev->name);

	netif_stop_queue(dev);

#ifdef CONFIG_SYSCTL
	smc_sysctl_unregister(dev);
#endif /* CONFIG_SYSCTL */

	/* clear everything */
	smc_shutdown( dev->base_addr );

	/* Update the statistics here. */
	return 0;
}

/*------------------------------------------------------------
 . Get the current statistics.
 . This may be called with the card open or closed.
 .-------------------------------------------------------------*/
static struct net_device_stats* smc_query_statistics(struct net_device *dev) {
	struct smc_local *lp = (struct smc_local *)dev->priv;

	PRINTK2("%s:smc_query_statistics\n", dev->name);

	return &lp->stats;
}

/*-----------------------------------------------------------
 . smc_set_multicast_list
 .
 . This routine will, depending on the values passed to it,
 . either make it accept multicast packets, go into
 . promiscuous mode ( for TCPDUMP and cousins ) or accept
 . a select set of multicast packets
*/
static void smc_set_multicast_list(struct net_device *dev)
{
	unsigned long ioaddr = dev->base_addr;

	PRINTK2("%s:smc_set_multicast_list\n", dev->name);

	SMC_SELECT_BANK(0);
	if ( dev->flags & IFF_PROMISC ) {
		PRINTK2("%s:smc_set_multicast_list:RCR_PRMS\n", dev->name);
		SMC_SET_RCR( SMC_GET_RCR() | RCR_PRMS );
	}

/* BUG?  I never disable promiscuous mode if multicasting was turned on.
   Now, I turn off promiscuous mode, but I don't do anything to multicasting
   when promiscuous mode is turned on.
*/

	/* Here, I am setting this to accept all multicast packets.
	   I don't need to zero the multicast table, because the flag is
	   checked before the table is
	*/
	else if (dev->flags & IFF_ALLMULTI) {
		SMC_SET_RCR( SMC_GET_RCR() | RCR_ALMUL );
		PRINTK2("%s:smc_set_multicast_list:RCR_ALMUL\n", dev->name);
	}

	/* We just get all multicast packets even if we only want them
	 . from one source.  This will be changed at some future
	 . point. */
	else if (dev->mc_count )  {
		/* support hardware multicasting */

		/* be sure I get rid of flags I might have set */
		SMC_SET_RCR( SMC_GET_RCR() & ~(RCR_PRMS | RCR_ALMUL) );
		/* NOTE: this has to set the bank, so make sure it is the
		   last thing called.  The bank is set to zero at the top */
		smc_setmulticast( ioaddr, dev->mc_count, dev->mc_list );
	} else  {
		PRINTK2("%s:smc_set_multicast_list:~(RCR_PRMS|RCR_ALMUL)\n",
			dev->name);
		SMC_SET_RCR( SMC_GET_RCR() & ~(RCR_PRMS | RCR_ALMUL) );

		/*
		  since I'm disabling all multicast entirely, I need to
		  clear the multicast list
		*/
		SMC_SELECT_BANK( 3 );
		SMC_CLEAR_MCAST();
	}
}

/*------------------------------------------------------------
 . Cleanup when module is removed with rmmod
 .-------------------------------------------------------------*/
static void __exit smc_cleanup(void)
{
	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	unregister_netdev(global_dev);

	free_irq(global_dev->irq, global_dev);
	release_region(global_dev->base_addr, SMC_IO_EXTENT);

#ifndef CONFIG_ISA
	iounmap((void *)global_dev->base_addr);
#endif

	kfree(global_dev);
	global_dev = NULL;
}

module_init(smc_init);
module_exit(smc_cleanup);


#ifdef CONFIG_SYSCTL
/*------------------------------------------------------------
 . Modify a bit in the LAN91C111 register set
 .-------------------------------------------------------------*/
static word smc_modify_regbit(int bank, unsigned long ioaddr, int reg,
	unsigned int bit, int val)
{
	word regval;

	SMC_SELECT_BANK( bank );

	regval = SMC_inw( ioaddr+reg );
	if (val)
		regval |= bit;
	else
		regval &= ~bit;

	SMC_outw( regval, ioaddr );
	return(regval);
}


/*------------------------------------------------------------
 . Retrieve a bit in the LAN91C111 register set
 .-------------------------------------------------------------*/
static int smc_get_regbit(int bank, unsigned long ioaddr,
			  int reg, unsigned int bit)
{
	SMC_SELECT_BANK( bank );
	if ( SMC_inw( ioaddr+reg ) & bit)
		return(1);
	else
		return(0);
}


/*------------------------------------------------------------
 . Modify a LAN91C111 register (word access only)
 .-------------------------------------------------------------*/
static void smc_modify_reg(int bank, unsigned long ioaddr,
			   int reg, word val)
{
	SMC_SELECT_BANK( bank );
	SMC_outw( val, ioaddr+reg );
}


/*------------------------------------------------------------
 . Retrieve a LAN91C111 register (word access only)
 .-------------------------------------------------------------*/
static int smc_get_reg(int bank, unsigned long ioaddr,
		       int reg)
{
	SMC_SELECT_BANK( bank );
	return(SMC_inw( ioaddr+reg ));
}


static const char smc_info_string[] =
"\n"
"info           Provides this information blurb\n"
"swver          Prints the software version information of this driver\n"
"autoneg        Auto-negotiate Mode = 1\n"
"rspeed         Requested Speed, 100=100Mbps, 10=10Mpbs\n"
"rfduplx        Requested Full Duplex Operation\n"
"aspeed         Actual Speed, 100=100Mbps, 10=10Mpbs\n"
"afduplx        Actual Full Duplex Operation\n"
"lnkfail        PHY Link Failure when 1\n"
"miiop          External MII when 1, Internal PHY when 0\n"
"swfdup         Switched Full Duplex Mode (allowed only in MII operation)\n"
"ephloop        EPH Block Loopback\n"
"forcol         Force a collision\n"
"filtcar        Filter leading edge of carrier sense for 12 bit times\n"
"freemem        Free buffer memory in bytes\n"
"totmem         Total buffer memory in bytes\n"
"leda           Output of LED-A (green)\n"
"ledb           Output of LED-B (yellow)\n"
"chiprev        Revision ID of the LAN91C111 chip\n"
"";

/*------------------------------------------------------------
 . Sysctl handler for all integer parameters
 .-------------------------------------------------------------*/
static int smc_sysctl_handler(ctl_table *ctl, int write, struct file * filp,
				void *buffer, size_t *lenp)
{
	struct net_device *dev = (struct net_device*)ctl->extra1;
	struct smc_local *lp = (struct smc_local *)ctl->extra2;
	unsigned long ioaddr = dev->base_addr;
	int *valp = ctl->data;
	int val;
	int ret;

	// Update parameters from the real registers
	switch (ctl->ctl_name)
	{
	case CTL_SMC_FORCOL:
		*valp = smc_get_regbit(0, ioaddr, TCR_REG, TCR_FORCOL);
		break;

	case CTL_SMC_FREEMEM:
		*valp = ( (word)smc_get_reg(0, ioaddr, MIR_REG) >> 8 )
			* LAN91C111_MEMORY_MULTIPLIER;
		break;


	case CTL_SMC_TOTMEM:
		*valp = ( smc_get_reg(0, ioaddr, MIR_REG) & (word)0x00ff )
			* LAN91C111_MEMORY_MULTIPLIER;
		break;

	case CTL_SMC_CHIPREV:
		*valp = smc_get_reg(3, ioaddr, REV_REG);
		break;

	case CTL_SMC_AFDUPLX:
		*valp = (lp->lastPhy18 & PHY_INT_DPLXDET) ? 1 : 0;
		break;

	case CTL_SMC_ASPEED:
		*valp = (lp->lastPhy18 & PHY_INT_SPDDET) ? 100 : 10;
		break;

	case CTL_SMC_LNKFAIL:
		*valp = (lp->lastPhy18 & PHY_INT_LNKFAIL) ? 1 : 0;
		break;

	case CTL_SMC_LEDA:
		*valp = (lp->rpc_cur_mode >> RPC_LSXA_SHFT) & (word)0x0007;
		break;

	case CTL_SMC_LEDB:
		*valp = (lp->rpc_cur_mode >> RPC_LSXB_SHFT) & (word)0x0007;
		break;

	case CTL_SMC_MIIOP:
		*valp = smc_get_regbit(1, ioaddr, CONFIG_REG, CONFIG_EXT_PHY);
		break;

#if SMC_DEBUG > 1
	case CTL_SMC_REG_BSR:	// Bank Select
		*valp = smc_get_reg(0, ioaddr, BSR_REG);
		break;

	case CTL_SMC_REG_TCR:	// Transmit Control
		*valp = smc_get_reg(0, ioaddr, TCR_REG);
		break;

	case CTL_SMC_REG_ESR:	// EPH Status
		*valp = smc_get_reg(0, ioaddr, EPH_STATUS_REG);
		break;

	case CTL_SMC_REG_RCR:	// Receive Control
		*valp = smc_get_reg(0, ioaddr, RCR_REG);
		break;

	case CTL_SMC_REG_CTRR:	// Counter
		*valp = smc_get_reg(0, ioaddr, COUNTER_REG);
		break;

	case CTL_SMC_REG_MIR:	// Memory Information
		*valp = smc_get_reg(0, ioaddr, MIR_REG);
		break;

	case CTL_SMC_REG_RPCR:	// Receive/Phy Control
		*valp = smc_get_reg(0, ioaddr, RPC_REG);
		break;

	case CTL_SMC_REG_CFGR:	// Configuration
		*valp = smc_get_reg(1, ioaddr, CONFIG_REG);
		break;

	case CTL_SMC_REG_BAR:	// Base Address
		*valp = smc_get_reg(1, ioaddr, BASE_REG);
		break;

	case CTL_SMC_REG_IAR0:	// Individual Address
		*valp = smc_get_reg(1, ioaddr, ADDR0_REG);
		break;

	case CTL_SMC_REG_IAR1:	// Individual Address
		*valp = smc_get_reg(1, ioaddr, ADDR1_REG);
		break;

	case CTL_SMC_REG_IAR2:	// Individual Address
		*valp = smc_get_reg(1, ioaddr, ADDR2_REG);
		break;

	case CTL_SMC_REG_GPR:	// General Purpose
		*valp = smc_get_reg(1, ioaddr, GP_REG);
		break;

	case CTL_SMC_REG_CTLR:	// Control
		*valp = smc_get_reg(1, ioaddr, CTL_REG);
		break;

	case CTL_SMC_REG_MCR:	// MMU Command
		*valp = smc_get_reg(2, ioaddr, MMU_CMD_REG);
		break;

	case CTL_SMC_REG_PNR:	// Packet Number
		*valp = smc_get_reg(2, ioaddr, PN_REG);
		break;

	case CTL_SMC_REG_FPR:	// Allocation Result/FIFO Ports
		*valp = smc_get_reg(2, ioaddr, RXFIFO_REG);
		break;

	case CTL_SMC_REG_PTR:	// Pointer
		*valp = smc_get_reg(2, ioaddr, PTR_REG);
		break;

	case CTL_SMC_REG_DR:	// Data
		*valp = smc_get_reg(2, ioaddr, DATA_REG);
		break;

	case CTL_SMC_REG_ISR:	// Interrupt Status/Mask
		*valp = smc_get_reg(2, ioaddr, INT_REG);
		break;

	case CTL_SMC_REG_MTR1:	// Multicast Table Entry 1
		*valp = smc_get_reg(3, ioaddr, MCAST_REG1);
		break;

	case CTL_SMC_REG_MTR2:	// Multicast Table Entry 2
		*valp = smc_get_reg(3, ioaddr, MCAST_REG2);
		break;

	case CTL_SMC_REG_MTR3:	// Multicast Table Entry 3
		*valp = smc_get_reg(3, ioaddr, MCAST_REG3);
		break;

	case CTL_SMC_REG_MTR4:	// Multicast Table Entry 4
		*valp = smc_get_reg(3, ioaddr, MCAST_REG4);
		break;

	case CTL_SMC_REG_MIIR:	// Management Interface
		*valp = smc_get_reg(3, ioaddr, MII_REG);
		break;

	case CTL_SMC_REG_REVR:	// Revision
		*valp = smc_get_reg(3, ioaddr, REV_REG);
		break;

	case CTL_SMC_REG_ERCVR:	// Early RCV
		*valp = smc_get_reg(3, ioaddr, ERCV_REG);
		break;

	case CTL_SMC_REG_EXTR:	// External
		*valp = smc_get_reg(7, ioaddr, EXT_REG);
		break;

	case CTL_SMC_PHY_CTRL:
		*valp = smc_read_phy_register(ioaddr, lp->phyaddr,
			PHY_CNTL_REG);
		break;

	case CTL_SMC_PHY_STAT:
		*valp = smc_read_phy_register(ioaddr, lp->phyaddr,
			PHY_STAT_REG);
		break;

	case CTL_SMC_PHY_ID1:
		*valp = smc_read_phy_register(ioaddr, lp->phyaddr,
			PHY_ID1_REG);
		break;

	case CTL_SMC_PHY_ID2:
		*valp = smc_read_phy_register(ioaddr, lp->phyaddr,
			PHY_ID2_REG);
		break;

	case CTL_SMC_PHY_ADC:
		*valp = smc_read_phy_register(ioaddr, lp->phyaddr,
			PHY_AD_REG);
		break;

	case CTL_SMC_PHY_REMC:
		*valp = smc_read_phy_register(ioaddr, lp->phyaddr,
			PHY_RMT_REG);
		break;

	case CTL_SMC_PHY_CFG1:
		*valp = smc_read_phy_register(ioaddr, lp->phyaddr,
			PHY_CFG1_REG);
		break;

	case CTL_SMC_PHY_CFG2:
		*valp = smc_read_phy_register(ioaddr, lp->phyaddr,
			PHY_CFG2_REG);
		break;

	case CTL_SMC_PHY_INT:
		*valp = smc_read_phy_register(ioaddr, lp->phyaddr,
			PHY_INT_REG);
		break;

	case CTL_SMC_PHY_MASK:
		*valp = smc_read_phy_register(ioaddr, lp->phyaddr,
			PHY_MASK_REG);
		break;

#endif // SMC_DEBUG > 1

	default:
		// Just ignore unsupported parameters
		break;
	}

	// Save old state
	val = *valp;

	// Perform the generic integer operation
	if ((ret = proc_dointvec(ctl, write, filp, buffer, lenp)) != 0)
		return(ret);

	// Write changes out to the registers
	if (write && *valp != val) {

		val = *valp;
		switch (ctl->ctl_name) {

		case CTL_SMC_SWFDUP:
			if (val)
				lp->tcr_cur_mode |= TCR_SWFDUP;
			else
				lp->tcr_cur_mode &= ~TCR_SWFDUP;

			smc_modify_regbit(0, ioaddr, TCR_REG, TCR_SWFDUP, val);
			break;

		case CTL_SMC_EPHLOOP:
			if (val)
				lp->tcr_cur_mode |= TCR_EPH_LOOP;
			else
				lp->tcr_cur_mode &= ~TCR_EPH_LOOP;

			smc_modify_regbit(0, ioaddr, TCR_REG, TCR_EPH_LOOP, val);
			break;

		case CTL_SMC_FORCOL:
			if (val)
				lp->tcr_cur_mode |= TCR_FORCOL;
			else
				lp->tcr_cur_mode &= ~TCR_FORCOL;

			// Update the EPH block
			smc_modify_regbit(0, ioaddr, TCR_REG, TCR_FORCOL, val);
			break;

		case CTL_SMC_FILTCAR:
			if (val)
				lp->rcr_cur_mode |= RCR_FILT_CAR;
			else
				lp->rcr_cur_mode &= ~RCR_FILT_CAR;

			// Update the EPH block
			smc_modify_regbit(0, ioaddr, RCR_REG, RCR_FILT_CAR, val);
			break;

		case CTL_SMC_RFDUPLX:
			// Disallow changes if in auto-negotiation mode
			if (lp->ctl_autoneg)
				break;

			if (val)
				lp->rpc_cur_mode |= RPC_DPLX;
			else
				lp->rpc_cur_mode &= ~RPC_DPLX;

			// Reconfigure the PHY
			smc_phy_configure(dev);

			break;

		case CTL_SMC_RSPEED:
			// Disallow changes if in auto-negotiation mode
			if (lp->ctl_autoneg)
				break;

			if (val > 10)
				lp->rpc_cur_mode |= RPC_SPEED;
			else
				lp->rpc_cur_mode &= ~RPC_SPEED;

			// Reconfigure the PHY
			smc_phy_configure(dev);

			break;

		case CTL_SMC_AUTONEG:
			if (val)
				lp->rpc_cur_mode |= RPC_ANEG;
			else
				lp->rpc_cur_mode &= ~RPC_ANEG;

			// Reconfigure the PHY
			smc_phy_configure(dev);

			break;

		case CTL_SMC_LEDA:
			val &= 0x07; // Restrict to 3 ls bits
			lp->rpc_cur_mode &= ~(word)(0x07<<RPC_LSXA_SHFT);
			lp->rpc_cur_mode |= (word)(val<<RPC_LSXA_SHFT);

			// Update the Internal PHY block
			smc_modify_reg(0, ioaddr, RPC_REG, lp->rpc_cur_mode);
			break;

		case CTL_SMC_LEDB:
			val &= 0x07; // Restrict to 3 ls bits
			lp->rpc_cur_mode &= ~(word)(0x07<<RPC_LSXB_SHFT);
			lp->rpc_cur_mode |= (word)(val<<RPC_LSXB_SHFT);

			// Update the Internal PHY block
			smc_modify_reg(0, ioaddr, RPC_REG, lp->rpc_cur_mode);
			break;

		case CTL_SMC_MIIOP:
			// Update the Internal PHY block
			smc_modify_regbit(1, ioaddr, CONFIG_REG,
				CONFIG_EXT_PHY, val);
			break;

#if SMC_DEBUG > 1
		case CTL_SMC_REG_BSR:	// Bank Select
			smc_modify_reg(0, ioaddr, BSR_REG, val);
			break;

		case CTL_SMC_REG_TCR:	// Transmit Control
			smc_modify_reg(0, ioaddr, TCR_REG, val);
			break;

		case CTL_SMC_REG_ESR:	// EPH Status
			smc_modify_reg(0, ioaddr, EPH_STATUS_REG, val);
			break;

		case CTL_SMC_REG_RCR:	// Receive Control
			smc_modify_reg(0, ioaddr, RCR_REG, val);
			break;

		case CTL_SMC_REG_CTRR:	// Counter
			smc_modify_reg(0, ioaddr, COUNTER_REG, val);
			break;

		case CTL_SMC_REG_MIR:	// Memory Information
			smc_modify_reg(0, ioaddr, MIR_REG, val);
			break;

		case CTL_SMC_REG_RPCR:	// Receive/Phy Control
			smc_modify_reg(0, ioaddr, RPC_REG, val);
			break;

		case CTL_SMC_REG_CFGR:	// Configuration
			smc_modify_reg(1, ioaddr, CONFIG_REG, val);
			break;

		case CTL_SMC_REG_BAR:	// Base Address
			smc_modify_reg(1, ioaddr, BASE_REG, val);
			break;

		case CTL_SMC_REG_IAR0:	// Individual Address
			smc_modify_reg(1, ioaddr, ADDR0_REG, val);
			break;

		case CTL_SMC_REG_IAR1:	// Individual Address
			smc_modify_reg(1, ioaddr, ADDR1_REG, val);
			break;

		case CTL_SMC_REG_IAR2:	// Individual Address
			smc_modify_reg(1, ioaddr, ADDR2_REG, val);
			break;

		case CTL_SMC_REG_GPR:	// General Purpose
			smc_modify_reg(1, ioaddr, GP_REG, val);
			break;

		case CTL_SMC_REG_CTLR:	// Control
			smc_modify_reg(1, ioaddr, CTL_REG, val);
			break;

		case CTL_SMC_REG_MCR:	// MMU Command
			smc_modify_reg(2, ioaddr, MMU_CMD_REG, val);
			break;

		case CTL_SMC_REG_PNR:	// Packet Number
			smc_modify_reg(2, ioaddr, PN_REG, val);
			break;

		case CTL_SMC_REG_FPR:	// Allocation Result/FIFO Ports
			smc_modify_reg(2, ioaddr, RXFIFO_REG, val);
			break;

		case CTL_SMC_REG_PTR:	// Pointer
			smc_modify_reg(2, ioaddr, PTR_REG, val);
			break;

		case CTL_SMC_REG_DR:	// Data
			smc_modify_reg(2, ioaddr, DATA_REG, val);
			break;

		case CTL_SMC_REG_ISR:	// Interrupt Status/Mask
			smc_modify_reg(2, ioaddr, INT_REG, val);
			break;

		case CTL_SMC_REG_MTR1:	// Multicast Table Entry 1
			smc_modify_reg(3, ioaddr, MCAST_REG1, val);
			break;

		case CTL_SMC_REG_MTR2:	// Multicast Table Entry 2
			smc_modify_reg(3, ioaddr, MCAST_REG2, val);
			break;

		case CTL_SMC_REG_MTR3:	// Multicast Table Entry 3
			smc_modify_reg(3, ioaddr, MCAST_REG3, val);
			break;

		case CTL_SMC_REG_MTR4:	// Multicast Table Entry 4
			smc_modify_reg(3, ioaddr, MCAST_REG4, val);
			break;

		case CTL_SMC_REG_MIIR:	// Management Interface
			smc_modify_reg(3, ioaddr, MII_REG, val);
			break;

		case CTL_SMC_REG_REVR:	// Revision
			smc_modify_reg(3, ioaddr, REV_REG, val);
			break;

		case CTL_SMC_REG_ERCVR:	// Early RCV
			smc_modify_reg(3, ioaddr, ERCV_REG, val);
			break;

		case CTL_SMC_REG_EXTR:	// External
			smc_modify_reg(7, ioaddr, EXT_REG, val);
			break;

		case CTL_SMC_PHY_CTRL:
			smc_write_phy_register(ioaddr, lp->phyaddr,
				PHY_CNTL_REG, val);
			break;

		case CTL_SMC_PHY_STAT:
			smc_write_phy_register(ioaddr, lp->phyaddr,
				PHY_STAT_REG, val);
			break;

		case CTL_SMC_PHY_ID1:
			smc_write_phy_register(ioaddr, lp->phyaddr,
				PHY_ID1_REG, val);
			break;

		case CTL_SMC_PHY_ID2:
			smc_write_phy_register(ioaddr, lp->phyaddr,
				PHY_ID2_REG, val);
			break;

		case CTL_SMC_PHY_ADC:
			smc_write_phy_register(ioaddr, lp->phyaddr,
				PHY_AD_REG, val);
			break;

		case CTL_SMC_PHY_REMC:
			smc_write_phy_register(ioaddr, lp->phyaddr,
				PHY_RMT_REG, val);
			break;

		case CTL_SMC_PHY_CFG1:
			smc_write_phy_register(ioaddr, lp->phyaddr,
				PHY_CFG1_REG, val);
			break;

		case CTL_SMC_PHY_CFG2:
			smc_write_phy_register(ioaddr, lp->phyaddr,
				PHY_CFG2_REG, val);
			break;

		case CTL_SMC_PHY_INT:
			smc_write_phy_register(ioaddr, lp->phyaddr,
				PHY_INT_REG, val);
			break;

		case CTL_SMC_PHY_MASK:
			smc_write_phy_register(ioaddr, lp->phyaddr,
				PHY_MASK_REG, val);
			break;

#endif // SMC_DEBUG > 1

		default:
			// Just ignore unsupported parameters
			break;
		} // end switch

	} // end if

        return ret;
}


#ifdef MODULE
/*
 * This is called as the fill_inode function when an inode
 * is going into (fill = 1) or out of service (fill = 0).
 * We use it here to manage the module use counts.
 *
 * Note: only the top-level directory needs to do this; if
 * a lower level is referenced, the parent will be as well.
 */
static void smc_procfs_modcount(struct inode *inode, int fill)
{
	if (fill)
		MOD_INC_USE_COUNT;
	else
		MOD_DEC_USE_COUNT;
}
#endif // MODULE

/*------------------------------------------------------------
 . Sysctl registration function for all parameters (files)
 .-------------------------------------------------------------*/
static void smc_sysctl_register(struct net_device *dev)
{
	struct smc_local *lp = (struct smc_local *)dev->priv;
	static int ctl_name = CTL_SMC;
	ctl_table* ct;
	int i;

	// Make sure the ctl_tables start out as all zeros
	memset(lp->root_table, 0, sizeof lp->root_table);
	memset(lp->eth_table, 0, sizeof lp->eth_table);
	memset(lp->param_table, 0, sizeof lp->param_table);

	// Initialize the root table
	ct = lp->root_table;
	ct->ctl_name = CTL_DEV;
	ct->procname = "dev";
	ct->maxlen = 0;
	ct->mode = 0555;
	ct->child = lp->eth_table;
	// remaining fields are zero

	// Initialize the ethX table (this device's table)
	ct = lp->eth_table;
	ct->ctl_name = ctl_name++; // Must be unique
	ct->procname = dev->name;
	ct->maxlen = 0;
	ct->mode = 0555;
	ct->child = lp->param_table;
	// remaining fields are zero

	// Initialize the parameter (files) table
	// Make sure the last entry remains null
	ct = lp->param_table;
	for (i = 0; i < (CTL_SMC_LAST_ENTRY-1); ++i) {
		// Initialize fields common to all table entries
		ct[i].proc_handler = smc_sysctl_handler;
		ct[i].extra1 = (void*)dev; // Save our device pointer
		ct[i].extra2 = (void*)lp;  // Save our smc_local data pointer
	}

	// INFO - this is our only string parameter
	i = 0;
	ct[i].proc_handler = proc_dostring; // use default handler
	ct[i].ctl_name = CTL_SMC_INFO;
	ct[i].procname = "info";
	ct[i].data = (void*)smc_info_string;
	ct[i].maxlen = sizeof smc_info_string;
	ct[i].mode = 0444; // Read only

	// SWVER
	++i;
	ct[i].proc_handler = proc_dostring; // use default handler
	ct[i].ctl_name = CTL_SMC_SWVER;
	ct[i].procname = "swver";
	ct[i].data = (void*)version;
	ct[i].maxlen = sizeof version;
	ct[i].mode = 0444; // Read only

	// SWFDUP
	++i;
	ct[i].ctl_name = CTL_SMC_SWFDUP;
	ct[i].procname = "swfdup";
	ct[i].data = (void*)&(lp->ctl_swfdup);
	ct[i].maxlen = sizeof lp->ctl_swfdup;
	ct[i].mode = 0644; // Read by all, write by root

	// EPHLOOP
	++i;
	ct[i].ctl_name = CTL_SMC_EPHLOOP;
	ct[i].procname = "ephloop";
	ct[i].data = (void*)&(lp->ctl_ephloop);
	ct[i].maxlen = sizeof lp->ctl_ephloop;
	ct[i].mode = 0644; // Read by all, write by root

	// MIIOP
	++i;
	ct[i].ctl_name = CTL_SMC_MIIOP;
	ct[i].procname = "miiop";
	ct[i].data = (void*)&(lp->ctl_miiop);
	ct[i].maxlen = sizeof lp->ctl_miiop;
	ct[i].mode = 0644; // Read by all, write by root

	// AUTONEG
	++i;
	ct[i].ctl_name = CTL_SMC_AUTONEG;
	ct[i].procname = "autoneg";
	ct[i].data = (void*)&(lp->ctl_autoneg);
	ct[i].maxlen = sizeof lp->ctl_autoneg;
	ct[i].mode = 0644; // Read by all, write by root

	// RFDUPLX
	++i;
	ct[i].ctl_name = CTL_SMC_RFDUPLX;
	ct[i].procname = "rfduplx";
	ct[i].data = (void*)&(lp->ctl_rfduplx);
	ct[i].maxlen = sizeof lp->ctl_rfduplx;
	ct[i].mode = 0644; // Read by all, write by root

	// RSPEED
	++i;
	ct[i].ctl_name = CTL_SMC_RSPEED;
	ct[i].procname = "rspeed";
	ct[i].data = (void*)&(lp->ctl_rspeed);
	ct[i].maxlen = sizeof lp->ctl_rspeed;
	ct[i].mode = 0644; // Read by all, write by root

	// AFDUPLX
	++i;
	ct[i].ctl_name = CTL_SMC_AFDUPLX;
	ct[i].procname = "afduplx";
	ct[i].data = (void*)&(lp->ctl_afduplx);
	ct[i].maxlen = sizeof lp->ctl_afduplx;
	ct[i].mode = 0444; // Read only

	// ASPEED
	++i;
	ct[i].ctl_name = CTL_SMC_ASPEED;
	ct[i].procname = "aspeed";
	ct[i].data = (void*)&(lp->ctl_aspeed);
	ct[i].maxlen = sizeof lp->ctl_aspeed;
	ct[i].mode = 0444; // Read only

	// LNKFAIL
	++i;
	ct[i].ctl_name = CTL_SMC_LNKFAIL;
	ct[i].procname = "lnkfail";
	ct[i].data = (void*)&(lp->ctl_lnkfail);
	ct[i].maxlen = sizeof lp->ctl_lnkfail;
	ct[i].mode = 0444; // Read only

	// FORCOL
	++i;
	ct[i].ctl_name = CTL_SMC_FORCOL;
	ct[i].procname = "forcol";
	ct[i].data = (void*)&(lp->ctl_forcol);
	ct[i].maxlen = sizeof lp->ctl_forcol;
	ct[i].mode = 0644; // Read by all, write by root

	// FILTCAR
	++i;
	ct[i].ctl_name = CTL_SMC_FILTCAR;
	ct[i].procname = "filtcar";
	ct[i].data = (void*)&(lp->ctl_filtcar);
	ct[i].maxlen = sizeof lp->ctl_filtcar;
	ct[i].mode = 0644; // Read by all, write by root

	// FREEMEM
	++i;
	ct[i].ctl_name = CTL_SMC_FREEMEM;
	ct[i].procname = "freemem";
	ct[i].data = (void*)&(lp->ctl_freemem);
	ct[i].maxlen = sizeof lp->ctl_freemem;
	ct[i].mode = 0444; // Read only

	// TOTMEM
	++i;
	ct[i].ctl_name = CTL_SMC_TOTMEM;
	ct[i].procname = "totmem";
	ct[i].data = (void*)&(lp->ctl_totmem);
	ct[i].maxlen = sizeof lp->ctl_totmem;
	ct[i].mode = 0444; // Read only

	// LEDA
	++i;
	ct[i].ctl_name = CTL_SMC_LEDA;
	ct[i].procname = "leda";
	ct[i].data = (void*)&(lp->ctl_leda);
	ct[i].maxlen = sizeof lp->ctl_leda;
	ct[i].mode = 0644; // Read by all, write by root

	// LEDB
	++i;
	ct[i].ctl_name = CTL_SMC_LEDB;
	ct[i].procname = "ledb";
	ct[i].data = (void*)&(lp->ctl_ledb);
	ct[i].maxlen = sizeof lp->ctl_ledb;
	ct[i].mode = 0644; // Read by all, write by root

	// CHIPREV
	++i;
	ct[i].ctl_name = CTL_SMC_CHIPREV;
	ct[i].procname = "chiprev";
	ct[i].data = (void*)&(lp->ctl_chiprev);
	ct[i].maxlen = sizeof lp->ctl_chiprev;
	ct[i].mode = 0444; // Read only

#if SMC_DEBUG > 1
	// REG_BSR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_BSR;
	ct[i].procname = "reg_bsr";
	ct[i].data = (void*)&(lp->ctl_reg_bsr);
	ct[i].maxlen = sizeof lp->ctl_reg_bsr;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_TCR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_TCR;
	ct[i].procname = "reg_tcr";
	ct[i].data = (void*)&(lp->ctl_reg_tcr);
	ct[i].maxlen = sizeof lp->ctl_reg_tcr;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_ESR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_ESR;
	ct[i].procname = "reg_esr";
	ct[i].data = (void*)&(lp->ctl_reg_esr);
	ct[i].maxlen = sizeof lp->ctl_reg_esr;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_RCR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_RCR;
	ct[i].procname = "reg_rcr";
	ct[i].data = (void*)&(lp->ctl_reg_rcr);
	ct[i].maxlen = sizeof lp->ctl_reg_rcr;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_CTRR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_CTRR;
	ct[i].procname = "reg_ctrr";
	ct[i].data = (void*)&(lp->ctl_reg_ctrr);
	ct[i].maxlen = sizeof lp->ctl_reg_ctrr;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_MIR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_MIR;
	ct[i].procname = "reg_mir";
	ct[i].data = (void*)&(lp->ctl_reg_mir);
	ct[i].maxlen = sizeof lp->ctl_reg_mir;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_RPCR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_RPCR;
	ct[i].procname = "reg_rpcr";
	ct[i].data = (void*)&(lp->ctl_reg_rpcr);
	ct[i].maxlen = sizeof lp->ctl_reg_rpcr;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_CFGR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_CFGR;
	ct[i].procname = "reg_cfgr";
	ct[i].data = (void*)&(lp->ctl_reg_cfgr);
	ct[i].maxlen = sizeof lp->ctl_reg_cfgr;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_BAR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_BAR;
	ct[i].procname = "reg_bar";
	ct[i].data = (void*)&(lp->ctl_reg_bar);
	ct[i].maxlen = sizeof lp->ctl_reg_bar;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_IAR0
	++i;
	ct[i].ctl_name = CTL_SMC_REG_IAR0;
	ct[i].procname = "reg_iar0";
	ct[i].data = (void*)&(lp->ctl_reg_iar0);
	ct[i].maxlen = sizeof lp->ctl_reg_iar0;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_IAR1
	++i;
	ct[i].ctl_name = CTL_SMC_REG_IAR1;
	ct[i].procname = "reg_iar1";
	ct[i].data = (void*)&(lp->ctl_reg_iar1);
	ct[i].maxlen = sizeof lp->ctl_reg_iar1;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_IAR2
	++i;
	ct[i].ctl_name = CTL_SMC_REG_IAR2;
	ct[i].procname = "reg_iar2";
	ct[i].data = (void*)&(lp->ctl_reg_iar2);
	ct[i].maxlen = sizeof lp->ctl_reg_iar2;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_GPR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_GPR;
	ct[i].procname = "reg_gpr";
	ct[i].data = (void*)&(lp->ctl_reg_gpr);
	ct[i].maxlen = sizeof lp->ctl_reg_gpr;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_CTLR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_CTLR;
	ct[i].procname = "reg_ctlr";
	ct[i].data = (void*)&(lp->ctl_reg_ctlr);
	ct[i].maxlen = sizeof lp->ctl_reg_ctlr;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_MCR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_MCR;
	ct[i].procname = "reg_mcr";
	ct[i].data = (void*)&(lp->ctl_reg_mcr);
	ct[i].maxlen = sizeof lp->ctl_reg_mcr;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_PNR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_PNR;
	ct[i].procname = "reg_pnr";
	ct[i].data = (void*)&(lp->ctl_reg_pnr);
	ct[i].maxlen = sizeof lp->ctl_reg_pnr;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_FPR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_FPR;
	ct[i].procname = "reg_fpr";
	ct[i].data = (void*)&(lp->ctl_reg_fpr);
	ct[i].maxlen = sizeof lp->ctl_reg_fpr;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_PTR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_PTR;
	ct[i].procname = "reg_ptr";
	ct[i].data = (void*)&(lp->ctl_reg_ptr);
	ct[i].maxlen = sizeof lp->ctl_reg_ptr;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_DR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_DR;
	ct[i].procname = "reg_dr";
	ct[i].data = (void*)&(lp->ctl_reg_dr);
	ct[i].maxlen = sizeof lp->ctl_reg_dr;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_ISR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_ISR;
	ct[i].procname = "reg_isr";
	ct[i].data = (void*)&(lp->ctl_reg_isr);
	ct[i].maxlen = sizeof lp->ctl_reg_isr;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_MTR1
	++i;
	ct[i].ctl_name = CTL_SMC_REG_MTR1;
	ct[i].procname = "reg_mtr1";
	ct[i].data = (void*)&(lp->ctl_reg_mtr1);
	ct[i].maxlen = sizeof lp->ctl_reg_mtr1;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_MTR2
	++i;
	ct[i].ctl_name = CTL_SMC_REG_MTR2;
	ct[i].procname = "reg_mtr2";
	ct[i].data = (void*)&(lp->ctl_reg_mtr2);
	ct[i].maxlen = sizeof lp->ctl_reg_mtr2;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_MTR3
	++i;
	ct[i].ctl_name = CTL_SMC_REG_MTR3;
	ct[i].procname = "reg_mtr3";
	ct[i].data = (void*)&(lp->ctl_reg_mtr3);
	ct[i].maxlen = sizeof lp->ctl_reg_mtr3;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_MTR4
	++i;
	ct[i].ctl_name = CTL_SMC_REG_MTR4;
	ct[i].procname = "reg_mtr4";
	ct[i].data = (void*)&(lp->ctl_reg_mtr4);
	ct[i].maxlen = sizeof lp->ctl_reg_mtr4;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_MIIR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_MIIR;
	ct[i].procname = "reg_miir";
	ct[i].data = (void*)&(lp->ctl_reg_miir);
	ct[i].maxlen = sizeof lp->ctl_reg_miir;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_REVR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_REVR;
	ct[i].procname = "reg_revr";
	ct[i].data = (void*)&(lp->ctl_reg_revr);
	ct[i].maxlen = sizeof lp->ctl_reg_revr;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_ERCVR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_ERCVR;
	ct[i].procname = "reg_ercvr";
	ct[i].data = (void*)&(lp->ctl_reg_ercvr);
	ct[i].maxlen = sizeof lp->ctl_reg_ercvr;
	ct[i].mode = 0644; // Read by all, write by root

	// REG_EXTR
	++i;
	ct[i].ctl_name = CTL_SMC_REG_EXTR;
	ct[i].procname = "reg_extr";
	ct[i].data = (void*)&(lp->ctl_reg_extr);
	ct[i].maxlen = sizeof lp->ctl_reg_extr;
	ct[i].mode = 0644; // Read by all, write by root

	// PHY Control
	++i;
	ct[i].ctl_name = CTL_SMC_PHY_CTRL;
	ct[i].procname = "phy_ctrl";
	ct[i].data = (void*)&(lp->ctl_phy_ctrl);
	ct[i].maxlen = sizeof lp->ctl_phy_ctrl;
	ct[i].mode = 0644; // Read by all, write by root

	// PHY Status
	++i;
	ct[i].ctl_name = CTL_SMC_PHY_STAT;
	ct[i].procname = "phy_stat";
	ct[i].data = (void*)&(lp->ctl_phy_stat);
	ct[i].maxlen = sizeof lp->ctl_phy_stat;
	ct[i].mode = 0644; // Read by all, write by root

	// PHY ID1
	++i;
	ct[i].ctl_name = CTL_SMC_PHY_ID1;
	ct[i].procname = "phy_id1";
	ct[i].data = (void*)&(lp->ctl_phy_id1);
	ct[i].maxlen = sizeof lp->ctl_phy_id1;
	ct[i].mode = 0644; // Read by all, write by root

	// PHY ID2
	++i;
	ct[i].ctl_name = CTL_SMC_PHY_ID2;
	ct[i].procname = "phy_id2";
	ct[i].data = (void*)&(lp->ctl_phy_id2);
	ct[i].maxlen = sizeof lp->ctl_phy_id2;
	ct[i].mode = 0644; // Read by all, write by root

	// PHY Advertise Capabilities
	++i;
	ct[i].ctl_name = CTL_SMC_PHY_ADC;
	ct[i].procname = "phy_adc";
	ct[i].data = (void*)&(lp->ctl_phy_adc);
	ct[i].maxlen = sizeof lp->ctl_phy_adc;
	ct[i].mode = 0644; // Read by all, write by root

	// PHY Remote Capabilities
	++i;
	ct[i].ctl_name = CTL_SMC_PHY_REMC;
	ct[i].procname = "phy_remc";
	ct[i].data = (void*)&(lp->ctl_phy_remc);
	ct[i].maxlen = sizeof lp->ctl_phy_remc;
	ct[i].mode = 0644; // Read by all, write by root

	// PHY Configuration 1
	++i;
	ct[i].ctl_name = CTL_SMC_PHY_CFG1;
	ct[i].procname = "phy_cfg1";
	ct[i].data = (void*)&(lp->ctl_phy_cfg1);
	ct[i].maxlen = sizeof lp->ctl_phy_cfg1;
	ct[i].mode = 0644; // Read by all, write by root

	// PHY Configuration 2
	++i;
	ct[i].ctl_name = CTL_SMC_PHY_CFG2;
	ct[i].procname = "phy_cfg2";
	ct[i].data = (void*)&(lp->ctl_phy_cfg2);
	ct[i].maxlen = sizeof lp->ctl_phy_cfg2;
	ct[i].mode = 0644; // Read by all, write by root

	// PHY Interrupt/Status Output
	++i;
	ct[i].ctl_name = CTL_SMC_PHY_INT;
	ct[i].procname = "phy_int";
	ct[i].data = (void*)&(lp->ctl_phy_int);
	ct[i].maxlen = sizeof lp->ctl_phy_int;
	ct[i].mode = 0644; // Read by all, write by root

	// PHY Interrupt/Status Mask
	++i;
	ct[i].ctl_name = CTL_SMC_PHY_MASK;
	ct[i].procname = "phy_mask";
	ct[i].data = (void*)&(lp->ctl_phy_mask);
	ct[i].maxlen = sizeof lp->ctl_phy_mask;
	ct[i].mode = 0644; // Read by all, write by root

#endif // SMC_DEBUG > 1

	// Register /proc/sys/dev/ethX
	lp->sysctl_header = register_sysctl_table(lp->root_table, 1);

#ifdef MODULE
	// Register our modcount function which adjusts the module count
	lp->root_table->child->de->fill_inode = smc_procfs_modcount;
#endif // MODULE

}


/*------------------------------------------------------------
 . Sysctl unregistration when driver is closed
 .-------------------------------------------------------------*/
static void smc_sysctl_unregister(struct net_device *dev)
{
	struct smc_local *lp = (struct smc_local *)dev->priv;

	unregister_sysctl_table(lp->sysctl_header);
}

#endif /* endif CONFIG_SYSCTL */


//---PHY CONTROL AND CONFIGURATION-----------------------------------------

#if (SMC_DEBUG > 2 )

/*------------------------------------------------------------
 . Debugging function for viewing MII Management serial bitstream
 .-------------------------------------------------------------*/
static void smc_dump_mii_stream(byte* bits, int size)
{
	int i;

	printk("BIT#:");
	for (i = 0; i < size; ++i)
		printk("%d", i%10);

	printk("\nMDOE:");
	for (i = 0; i < size; ++i) {
		if (bits[i] & MII_MDOE)
			printk("1");
		else
			printk("0");
	}

	printk("\nMDO :");
	for (i = 0; i < size; ++i) {
		if (bits[i] & MII_MDO)
			printk("1");
		else
			printk("0");
	}

	printk("\nMDI :");
	for (i = 0; i < size; ++i) {
		if (bits[i] & MII_MDI)
			printk("1");
		else
			printk("0");
	}

	printk("\n");
}
#endif

/*------------------------------------------------------------
 . Reads a register from the MII Management serial interface
 .-------------------------------------------------------------*/
static word smc_read_phy_register(unsigned long ioaddr,
				  byte phyaddr, byte phyreg)
{
	int oldBank;
	int i;
	byte mask;
	word mii_reg;
	byte bits[64];
	int clk_idx = 0;
	int input_idx;
	word phydata;

	// 32 consecutive ones on MDO to establish sync
	for (i = 0; i < 32; ++i)
		bits[clk_idx++] = MII_MDOE | MII_MDO;

	// Start code <01>
	bits[clk_idx++] = MII_MDOE;
	bits[clk_idx++] = MII_MDOE | MII_MDO;

	// Read command <10>
	bits[clk_idx++] = MII_MDOE | MII_MDO;
	bits[clk_idx++] = MII_MDOE;

	// Output the PHY address, msb first
	mask = (byte)0x10;
	for (i = 0; i < 5; ++i) {
		if (phyaddr & mask)
			bits[clk_idx++] = MII_MDOE | MII_MDO;
		else
			bits[clk_idx++] = MII_MDOE;

		// Shift to next lowest bit
		mask >>= 1;
	}

	// Output the phy register number, msb first
	mask = (byte)0x10;
	for (i = 0; i < 5; ++i) {
		if (phyreg & mask)
			bits[clk_idx++] = MII_MDOE | MII_MDO;
		else
			bits[clk_idx++] = MII_MDOE;

		// Shift to next lowest bit
		mask >>= 1;
	}

	// Tristate and turnaround (2 bit times)
	bits[clk_idx++] = 0;
	//bits[clk_idx++] = 0;

	// Input starts at this bit time
	input_idx = clk_idx;

	// Will input 16 bits
	for (i = 0; i < 16; ++i)
		bits[clk_idx++] = 0;

	// Final clock bit
	bits[clk_idx++] = 0;

	// Save the current bank
	oldBank = SMC_CURRENT_BANK();

	// Select bank 3
	SMC_SELECT_BANK( 3 );

	// Get the current MII register value
	mii_reg = SMC_GET_MII();

	// Turn off all MII Interface bits
	mii_reg &= ~(MII_MDOE|MII_MCLK|MII_MDI|MII_MDO);

	// Clock all 64 cycles
	for (i = 0; i < sizeof bits; ++i) {
		// Clock Low - output data
		SMC_SET_MII( mii_reg | bits[i] );
		udelay(50);


		// Clock Hi - input data
		SMC_SET_MII( mii_reg | bits[i] | MII_MCLK );
		udelay(50);
		bits[i] |= SMC_GET_MII() & MII_MDI;
	}

	// Return to idle state
	// Set clock to low, data to low, and output tristated
	SMC_SET_MII( mii_reg );
	udelay(50);

	// Restore original bank select
	SMC_SELECT_BANK( oldBank );

	// Recover input data
	phydata = 0;
	for (i = 0; i < 16; ++i) {
		phydata <<= 1;

		if (bits[input_idx++] & MII_MDI)
			phydata |= 0x0001;
	}

#if (SMC_DEBUG > 2 )
	printk("smc_read_phy_register(): phyaddr=%x,phyreg=%x,phydata=%x\n",
		phyaddr, phyreg, phydata);
	smc_dump_mii_stream(bits, sizeof bits);
#endif

	return(phydata);
}


/*------------------------------------------------------------
 . Writes a register to the MII Management serial interface
 .-------------------------------------------------------------*/
static void smc_write_phy_register(unsigned long ioaddr,
	byte phyaddr, byte phyreg, word phydata)
{
	int oldBank;
	int i;
	word mask;
	word mii_reg;
	byte bits[65];
	int clk_idx = 0;

	// 32 consecutive ones on MDO to establish sync
	for (i = 0; i < 32; ++i)
		bits[clk_idx++] = MII_MDOE | MII_MDO;

	// Start code <01>
	bits[clk_idx++] = MII_MDOE;
	bits[clk_idx++] = MII_MDOE | MII_MDO;

	// Write command <01>
	bits[clk_idx++] = MII_MDOE;
	bits[clk_idx++] = MII_MDOE | MII_MDO;

	// Output the PHY address, msb first
	mask = (byte)0x10;
	for (i = 0; i < 5; ++i) {
		if (phyaddr & mask)
			bits[clk_idx++] = MII_MDOE | MII_MDO;
		else
			bits[clk_idx++] = MII_MDOE;

		// Shift to next lowest bit
		mask >>= 1;
	}

	// Output the phy register number, msb first
	mask = (byte)0x10;
	for (i = 0; i < 5; ++i) {
		if (phyreg & mask)
			bits[clk_idx++] = MII_MDOE | MII_MDO;
		else
			bits[clk_idx++] = MII_MDOE;

		// Shift to next lowest bit
		mask >>= 1;
	}

	// Tristate and turnaround (2 bit times)
	bits[clk_idx++] = 0;
	bits[clk_idx++] = 0;

	// Write out 16 bits of data, msb first
	mask = 0x8000;
	for (i = 0; i < 16; ++i) {
		if (phydata & mask)
			bits[clk_idx++] = MII_MDOE | MII_MDO;
		else
			bits[clk_idx++] = MII_MDOE;

		// Shift to next lowest bit
		mask >>= 1;
	}

	// Final clock bit (tristate)
	bits[clk_idx++] = 0;

	// Save the current bank
	oldBank = SMC_CURRENT_BANK();

	// Select bank 3
	SMC_SELECT_BANK( 3 );

	// Get the current MII register value
	mii_reg = SMC_GET_MII();

	// Turn off all MII Interface bits
	mii_reg &= ~(MII_MDOE|MII_MCLK|MII_MDI|MII_MDO);

	// Clock all cycles
	for (i = 0; i < sizeof bits; ++i) {
		// Clock Low - output data
		SMC_SET_MII( mii_reg | bits[i] );
		udelay(50);


		// Clock Hi - input data
		SMC_SET_MII( mii_reg | bits[i] | MII_MCLK );
		udelay(50);
		bits[i] |= SMC_GET_MII() & MII_MDI;
	}

	// Return to idle state
	// Set clock to low, data to low, and output tristated
	SMC_SET_MII( mii_reg );
	udelay(50);

	// Restore original bank select
	SMC_SELECT_BANK( oldBank );

#if (SMC_DEBUG > 2 )
	printk("smc_write_phy_register(): phyaddr=%x,phyreg=%x,phydata=%x\n",
		phyaddr, phyreg, phydata);
	smc_dump_mii_stream(bits, sizeof bits);
#endif
}


/*------------------------------------------------------------
 . Finds and reports the PHY address
 .-------------------------------------------------------------*/
static int smc_detect_phy(struct net_device* dev)
{
	struct smc_local *lp = (struct smc_local *)dev->priv;
	unsigned long ioaddr = dev->base_addr;
	word phy_id1;
	word phy_id2;
	int phyaddr;
	int found = 0;

	PRINTK3("%s:smc_detect_phy()\n", dev->name);

	// Scan all 32 PHY addresses if necessary
	for (phyaddr = 0; phyaddr < 32; ++phyaddr) {
		// Read the PHY identifiers
		phy_id1  = smc_read_phy_register(ioaddr, phyaddr, PHY_ID1_REG);
		phy_id2  = smc_read_phy_register(ioaddr, phyaddr, PHY_ID2_REG);

		PRINTK3("%s: phy_id1=%x, phy_id2=%x\n",
			dev->name, phy_id1, phy_id2);

		// Make sure it is a valid identifier
		if ((phy_id2 > 0x0000) && (phy_id2 < 0xffff) &&
		    (phy_id1 > 0x0000) && (phy_id1 < 0xffff)) {
			if ((phy_id1 != 0x8000) && (phy_id2 != 0x8000)) {
				// Save the PHY's address
				lp->phyaddr = phyaddr;
				found = 1;
				break;
			}
		}
	}

	if (!found) {
		PRINTK("%s: No PHY found\n", dev->name);
		return(0);
	}

	// Set the PHY type
	if ( (phy_id1 == 0x0016) && ((phy_id2 & 0xFFF0) == 0xF840 ) ) {
		lp->phytype = PHY_LAN83C183;
		PRINTK("%s: PHY=LAN83C183 (LAN91C111 Internal)\n", dev->name);
	}

	if ( (phy_id1 == 0x0282) && ((phy_id2 & 0xFFF0) == 0x1C50) ) {
		lp->phytype = PHY_LAN83C180;
		PRINTK("%s: PHY=LAN83C180\n", dev->name);
	}

	return(1);
}

/*------------------------------------------------------------
 . Waits the specified number of milliseconds - kernel friendly
 .-------------------------------------------------------------*/
static void smc_wait_ms(unsigned int ms)
{

	if (!in_interrupt()) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1 + ms * HZ / 1000);
	} else {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1 + ms * HZ / 1000);
		set_current_state(TASK_RUNNING);
	}
}

/*------------------------------------------------------------
 . Sets the PHY to a configuration as determined by the user
 .-------------------------------------------------------------*/
static int smc_phy_fixed(struct net_device* dev)
{
	unsigned long ioaddr = dev->base_addr;
	struct smc_local *lp = (struct smc_local *)dev->priv;
	byte phyaddr = lp->phyaddr;
	word my_fixed_caps;
	word cfg1;

	PRINTK3("%s:smc_phy_fixed()\n", dev->name);

	// Enter Link Disable state
	cfg1 = smc_read_phy_register(ioaddr, phyaddr, PHY_CFG1_REG);
	cfg1 |= PHY_CFG1_LNKDIS;
	smc_write_phy_register(ioaddr, phyaddr, PHY_CFG1_REG, cfg1);

	// Set our fixed capabilities
	// Disable auto-negotiation
	my_fixed_caps = 0;

	if (lp->ctl_rfduplx)
		my_fixed_caps |= PHY_CNTL_DPLX;

	if (lp->ctl_rspeed == 100)
		my_fixed_caps |= PHY_CNTL_SPEED;

	// Write our capabilities to the phy control register
	smc_write_phy_register(ioaddr, phyaddr, PHY_CNTL_REG, my_fixed_caps);

	// Re-Configure the Receive/Phy Control register
	SMC_SET_RPC( lp->rpc_cur_mode );

	// Success
	return(1);
}


/*------------------------------------------------------------
 . Configures the specified PHY using Autonegotiation. Calls
 . smc_phy_fixed() if the user has requested a certain config.
 .-------------------------------------------------------------*/
static void smc_phy_configure(struct net_device* dev)
{
	unsigned long ioaddr = dev->base_addr;
	struct smc_local *lp = (struct smc_local *)dev->priv;
	int timeout;
	byte phyaddr;
	word my_phy_caps; // My PHY capabilities
	word my_ad_caps; // My Advertised capabilities
	word status;
	int failed = 0;

	PRINTK3("%s:smc_program_phy()\n", dev->name);

	// Set the blocking flag
	lp->autoneg_active = 1;

	// Find the address and type of our phy
	if (!smc_detect_phy(dev))
		goto smc_phy_configure_exit;

	// Get the detected phy address
	phyaddr = lp->phyaddr;

	// Reset the PHY, setting all other bits to zero
	smc_write_phy_register(ioaddr, phyaddr, PHY_CNTL_REG, PHY_CNTL_RST);

	// Wait for the reset to complete, or time out
	timeout = 6; // Wait up to 3 seconds
	while (timeout--) {
		if (!(smc_read_phy_register(ioaddr, phyaddr, PHY_CNTL_REG)
		    & PHY_CNTL_RST))
			// reset complete
			break;
		smc_wait_ms(500); // wait 500 millisecs
		if (signal_pending(current)) { // Exit anyway if signaled
			PRINTK2("%s:PHY reset interrupted by signal\n",
				dev->name);
			timeout = 0;
			break;
		}
	}

	if (timeout < 1) {
		printk("%s:PHY reset timed out\n", dev->name);
		goto smc_phy_configure_exit;
	}

	// Read PHY Register 18, Status Output
	lp->lastPhy18 = smc_read_phy_register(ioaddr, phyaddr, PHY_INT_REG);

	// Enable PHY Interrupts (for register 18)
	// Interrupts listed here are disabled
	smc_write_phy_register(ioaddr, phyaddr, PHY_MASK_REG,
		PHY_INT_LOSSSYNC | PHY_INT_CWRD | PHY_INT_SSD |
		PHY_INT_ESD | PHY_INT_RPOL | PHY_INT_JAB |
		PHY_INT_SPDDET | PHY_INT_DPLXDET);

	/* Configure the Receive/Phy Control register */
	SMC_SELECT_BANK( 0 );
	SMC_SET_RPC( lp->rpc_cur_mode );

	// Copy our capabilities from PHY_STAT_REG to PHY_AD_REG
	my_phy_caps = smc_read_phy_register(ioaddr, phyaddr, PHY_STAT_REG);
	my_ad_caps  = PHY_AD_CSMA; // I am CSMA capable

	if (my_phy_caps & PHY_STAT_CAP_T4)
		my_ad_caps |= PHY_AD_T4;

	if (my_phy_caps & PHY_STAT_CAP_TXF)
		my_ad_caps |= PHY_AD_TX_FDX;

	if (my_phy_caps & PHY_STAT_CAP_TXH)
		my_ad_caps |= PHY_AD_TX_HDX;

	if (my_phy_caps & PHY_STAT_CAP_TF)
		my_ad_caps |= PHY_AD_10_FDX;

	if (my_phy_caps & PHY_STAT_CAP_TH)
		my_ad_caps |= PHY_AD_10_HDX;

	// Disable capabilities not selected by our user
	if (lp->ctl_rspeed != 100)
		my_ad_caps &= ~(PHY_AD_T4|PHY_AD_TX_FDX|PHY_AD_TX_HDX);

	if (!lp->ctl_rfduplx)
		my_ad_caps &= ~(PHY_AD_TX_FDX|PHY_AD_10_FDX);

	// Update our Auto-Neg Advertisement Register
	smc_write_phy_register(ioaddr, phyaddr, PHY_AD_REG, my_ad_caps);

	// Read the register back.  Without this, it appears that when
	// auto-negotiation is restarted, sometimes it isn't ready and
	// the link does not come up.
	status = smc_read_phy_register(ioaddr, phyaddr, PHY_AD_REG);

	PRINTK2("%s:phy caps=%x\n", dev->name, my_phy_caps);
	PRINTK2("%s:phy advertised caps=%x\n", dev->name, my_ad_caps);

	// If the user requested no auto neg, then go set his request
	if (!(lp->ctl_autoneg)) {
		smc_phy_fixed(dev);
		goto smc_phy_configure_exit;
	}

	// Restart auto-negotiation process in order to advertise my caps
	smc_write_phy_register( ioaddr, phyaddr, PHY_CNTL_REG,
		PHY_CNTL_ANEG_EN | PHY_CNTL_ANEG_RST );

	// Wait for the auto-negotiation to complete.  This may take from
	// 2 to 3 seconds.
	// Wait for the reset to complete, or time out
	timeout = 20; // Wait up to 10 seconds
	while (timeout--) {
		status = smc_read_phy_register(ioaddr, phyaddr, PHY_STAT_REG);
		if (status & PHY_STAT_ANEG_ACK)
			// auto-negotiate complete
			break;

		smc_wait_ms(500); // wait 500 millisecs
		if (signal_pending(current)) { // Exit anyway if signaled
			printk(KERN_DEBUG
				"%s:PHY auto-negotiate interrupted by signal\n",
				dev->name);
			timeout = 0;
			break;
		}

		// Restart auto-negotiation if remote fault
		if (status & PHY_STAT_REM_FLT) {
			PRINTK2("%s:PHY remote fault detected\n", dev->name);

			// Restart auto-negotiation
			PRINTK2("%s:PHY restarting auto-negotiation\n",
				dev->name);
			smc_write_phy_register( ioaddr, phyaddr, PHY_CNTL_REG,
				PHY_CNTL_ANEG_EN | PHY_CNTL_ANEG_RST |
				PHY_CNTL_SPEED | PHY_CNTL_DPLX);
		}
	}

	if (timeout < 1) {
		printk(KERN_DEBUG "%s:PHY auto-negotiate timed out\n",
			dev->name);
		PRINTK2("%s:PHY auto-negotiate timed out\n", dev->name);
		failed = 1;
	}

	// Fail if we detected an auto-negotiate remote fault
	if (status & PHY_STAT_REM_FLT) {
		printk(KERN_DEBUG "%s:PHY remote fault detected\n", dev->name);
		PRINTK2("%s:PHY remote fault detected\n", dev->name);
		failed = 1;
	}

	// The smc_phy_interrupt() routine will be called to update lastPhy18

	// Set our sysctl parameters to match auto-negotiation results
	if ( lp->lastPhy18 & PHY_INT_SPDDET ) {
		PRINTK2("%s:PHY 100BaseT\n", dev->name);
		lp->rpc_cur_mode |= RPC_SPEED;
	} else {
		PRINTK2("%s:PHY 10BaseT\n", dev->name);
		lp->rpc_cur_mode &= ~RPC_SPEED;
	}

	if ( lp->lastPhy18 & PHY_INT_DPLXDET ) {
		PRINTK2("%s:PHY Full Duplex\n", dev->name);
		lp->rpc_cur_mode |= RPC_DPLX;
	} else {
		PRINTK2("%s:PHY Half Duplex\n", dev->name);
		lp->rpc_cur_mode &= ~RPC_DPLX;
	}

	// Re-Configure the Receive/Phy Control register
	SMC_SET_RPC( lp->rpc_cur_mode );

smc_phy_configure_exit:
	// Exit auto-negotiation
	lp->autoneg_active = 0;
}



/*************************************************************************
 . smc_phy_interrupt
 .
 . Purpose:  Handle interrupts relating to PHY register 18. This is
 .  called from the "hard" interrupt handler.
 .
 ************************************************************************/
static void smc_phy_interrupt(struct net_device* dev)
{
	unsigned long ioaddr	= dev->base_addr;
	struct smc_local *lp 	= (struct smc_local *)dev->priv;
	byte phyaddr = lp->phyaddr;
	word phy18;

	PRINTK2("%s: smc_phy_interrupt\n", dev->name);

	for(;;) {
		// Read PHY Register 18, Status Output
		phy18 = smc_read_phy_register(ioaddr, phyaddr, PHY_INT_REG);

		// Exit if not more changes
		if (phy18 == lp->lastPhy18)
			break;

#if (SMC_DEBUG > 1 )
		PRINTK2("%s:     phy18=0x%x\n", dev->name, phy18);
		PRINTK2("%s: lastPhy18=0x%x\n", dev->name, lp->lastPhy18);

		// Handle events
		if ((phy18 & PHY_INT_LNKFAIL) !=
				(lp->lastPhy18 & PHY_INT_LNKFAIL))
			PRINTK2("%s: PHY Link Fail=%x\n", dev->name,
					phy18 & PHY_INT_LNKFAIL);

		if ((phy18 & PHY_INT_LOSSSYNC) !=
				(lp->lastPhy18 & PHY_INT_LOSSSYNC))
			PRINTK2("%s: PHY LOSS SYNC=%x\n", dev->name,
					phy18 & PHY_INT_LOSSSYNC);

		if ((phy18 & PHY_INT_CWRD) != (lp->lastPhy18 & PHY_INT_CWRD))
			PRINTK2("%s: PHY INVALID 4B5B code=%x\n", dev->name,
					phy18 & PHY_INT_CWRD);

		if ((phy18 & PHY_INT_SSD) != (lp->lastPhy18 & PHY_INT_SSD))
			PRINTK2("%s: PHY No Start Of Stream=%x\n", dev->name,
					phy18 & PHY_INT_SSD);

		if ((phy18 & PHY_INT_ESD) != (lp->lastPhy18 & PHY_INT_ESD))

			PRINTK2("%s: PHY No End Of Stream=%x\n", dev->name,
					phy18 & PHY_INT_ESD);

		if ((phy18 & PHY_INT_RPOL) != (lp->lastPhy18 & PHY_INT_RPOL))
			PRINTK2("%s: PHY Reverse Polarity Detected=%x\n",
					dev->name, phy18 & PHY_INT_RPOL);

		if ((phy18 & PHY_INT_JAB) != (lp->lastPhy18 & PHY_INT_JAB))
			PRINTK2("%s: PHY Jabber Detected=%x\n", dev->name,
					phy18 & PHY_INT_JAB);

		if ((phy18 & PHY_INT_SPDDET) !=
				(lp->lastPhy18 & PHY_INT_SPDDET))
			PRINTK2("%s: PHY Speed Detect=%x\n", dev->name,
					phy18 & PHY_INT_SPDDET);

		if ((phy18 & PHY_INT_DPLXDET) !=
				(lp->lastPhy18 & PHY_INT_DPLXDET))
			PRINTK2("%s: PHY Duplex Detect=%x\n", dev->name,
					phy18 & PHY_INT_DPLXDET);
#endif
		// Update the last phy 18 variable
		lp->lastPhy18 = phy18;
	}
}
