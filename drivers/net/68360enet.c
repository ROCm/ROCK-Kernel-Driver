/*
 * Ethernet driver for Motorola MPC8xx.
 * Copyright (c) 2000 Michael Leslie <mleslie@lineo.com>
 * Copyright (c) 1997 Dan Malek (dmalek@jlc.net)
 *
 * I copied the basic skeleton from the lance driver, because I did not
 * know how to write the Linux driver, but I did know how the LANCE worked.
 *
 * This version of the driver is somewhat selectable for the different
 * processor/board combinations.  It works for the boards I know about
 * now, and should be easily modified to include others.  Some of the
 * configuration information is contained in "commproc.h" and the
 * remainder is here.
 *
 * Buffer descriptors are kept in the CPM dual port RAM, and the frame
 * buffers are in the host memory.
 *
 * Right now, I am very watseful with the buffers.  I allocate memory
 * pages and then divide them into 2K frame buffers.  This way I know I
 * have buffers large enough to hold one frame within one buffer descriptor.
 * Once I get this working, I will use 64 or 128 byte CPM buffers, which
 * will be much more memory efficient and will easily handle lots of
 * small packets.
 *
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h> 

#include <asm/irq.h>
#include <asm/m68360.h>
/* #include <asm/8xx_immap.h> */
/* #include <asm/pgtable.h> */
/* #include <asm/mpc8xx.h> */
#include <asm/bitops.h>
/* #include <asm/uaccess.h> */
#include <asm/commproc.h>


/*
 *				Theory of Operation
 *
 * The MPC8xx CPM performs the Ethernet processing on SCC1.  It can use
 * an aribtrary number of buffers on byte boundaries, but must have at
 * least two receive buffers to prevent constant overrun conditions.
 *
 * The buffer descriptors are allocated from the CPM dual port memory
 * with the data buffers allocated from host memory, just like all other
 * serial communication protocols.  The host memory buffers are allocated
 * from the free page pool, and then divided into smaller receive and
 * transmit buffers.  The size of the buffers should be a power of two,
 * since that nicely divides the page.  This creates a ring buffer
 * structure similar to the LANCE and other controllers.
 *
 * Like the LANCE driver:
 * The driver runs as two independent, single-threaded flows of control.  One
 * is the send-packet routine, which enforces single-threaded use by the
 * cep->tx_busy flag.  The other thread is the interrupt handler, which is
 * single threaded by the hardware and other software.
 *
 * The send packet thread has partial control over the Tx ring and the
 * 'cep->tx_busy' flag.  It sets the tx_busy flag whenever it's queuing a Tx
 * packet. If the next queue slot is empty, it clears the tx_busy flag when
 * finished otherwise it sets the 'lp->tx_full' flag.
 *
 * The MBX has a control register external to the MPC8xx that has some
 * control of the Ethernet interface.  Information is in the manual for
 * your board.
 *
 * The RPX boards have an external control/status register.  Consult the
 * programming documents for details unique to your board.
 *
 * For the TQM8xx(L) modules, there is no control register interface.
 * All functions are directly controlled using I/O pins.  See commproc.h.
 */


/* The transmitter timeout
 */
#define TX_TIMEOUT	(2*HZ)

/* The number of Tx and Rx buffers.  These are allocated statically here.
 * We don't need to allocate pages for the transmitter.  We just use
 * the skbuffer directly.
 */
#ifdef CONFIG_ENET_BIG_BUFFERS
#define RX_RING_SIZE		64
#define TX_RING_SIZE		64	/* Must be power of two */
#define TX_RING_MOD_MASK	63	/*   for this to work */
#else
#define RX_RING_SIZE		8
#define TX_RING_SIZE		8	/* Must be power of two */
#define TX_RING_MOD_MASK	7	/*   for this to work */
#endif

#define CPM_ENET_RX_FRSIZE  2048 /* overkill left over from ppc page-based allocation */
static char rx_buf_pool[RX_RING_SIZE * CPM_ENET_RX_FRSIZE];


/* The CPM stores dest/src/type, data, and checksum for receive packets.
 */
#define PKT_MAXBUF_SIZE		1518
#define PKT_MINBUF_SIZE		64
#define PKT_MAXBLR_SIZE		1520

/* The CPM buffer descriptors track the ring buffers.  The rx_bd_base and
 * tx_bd_base always point to the base of the buffer descriptors.  The
 * cur_rx and cur_tx point to the currently available buffer.
 * The dirty_tx tracks the current buffer that is being sent by the
 * controller.  The cur_tx and dirty_tx are equal under both completely
 * empty and completely full conditions.  The empty/ready indicator in
 * the buffer descriptor determines the actual condition.
 */
struct scc_enet_private {
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	struct	sk_buff* tx_skbuff[TX_RING_SIZE];
	ushort	skb_cur;
	ushort	skb_dirty;

	/* CPM dual port RAM relative addresses.
	*/
	QUICC_BD	*rx_bd_base;		/* Address of Rx and Tx buffers. */
	QUICC_BD	*tx_bd_base;
	QUICC_BD	*cur_rx, *cur_tx;		/* The next free ring entry */
	QUICC_BD	*dirty_tx;	/* The ring entries to be free()ed. */
	volatile struct scc_regs	*sccp;
	/* struct	net_device_stats stats; */
	struct	net_device_stats stats;
	uint	tx_full;
	/* spinlock_t lock; */
	volatile unsigned int lock;
};



static int scc_enet_open(struct net_device *dev);
static int scc_enet_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int scc_enet_rx(struct net_device *dev);
static irqreturn_t scc_enet_interrupt(int vec, void *dev_id, struct pt_regs *fp);
static int scc_enet_close(struct net_device *dev);
/* static struct net_device_stats *scc_enet_get_stats(struct net_device *dev); */
static struct net_device_stats *scc_enet_get_stats(struct net_device *dev);
static void set_multicast_list(struct net_device *dev);

/* Get this from various configuration locations (depends on board).
*/
/*static	ushort	my_enet_addr[] = { 0x0800, 0x3e26, 0x1559 };*/

/* Typically, 860(T) boards use SCC1 for Ethernet, and other 8xx boards
 * use SCC2.  This is easily extended if necessary.
 */

#define CONFIG_SCC1_ENET /* by default */

#ifdef CONFIG_SCC1_ENET
#define CPM_CR_ENET CPM_CR_CH_SCC1
#define PROFF_ENET	PROFF_SCC1
#define SCC_ENET	0
#define CPMVEC_ENET	CPMVEC_SCC1
#endif

#ifdef CONFIG_SCC2_ENET
#define CPM_CR_ENET	CPM_CR_CH_SCC2
#define PROFF_ENET	PROFF_SCC2
#define SCC_ENET	1		/* Index, not number! */
#define CPMVEC_ENET	CPMVEC_SCC2
#endif

static int
scc_enet_open(struct net_device *dev)
{

	/* I should reset the ring buffers here, but I don't yet know
	 * a simple way to do that.
	 * mleslie: That's no biggie. Worth doing, too.
	 */

	/* netif_start_queue(dev); */
	return 0;					/* Always succeed */
}


static int
scc_enet_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct scc_enet_private *cep = (struct scc_enet_private *)dev->priv;
	volatile QUICC_BD	*bdp;

	/* Fill in a Tx ring entry */
	bdp = cep->cur_tx;

#ifndef final_version
	if (bdp->status & BD_ENET_TX_READY) {
		/* Ooops.  All transmit buffers are full.  Bail out.
		 * This should not happen, since cep->tx_busy should be set.
		 */
		printk("%s: tx queue full!.\n", dev->name);
		return 1;
	}
#endif

	/* Clear all of the status flags.
	 */
	bdp->status &= ~BD_ENET_TX_STATS;

	/* If the frame is short, tell CPM to pad it.
	*/
	if (skb->len <= ETH_ZLEN)
		bdp->status |= BD_ENET_TX_PAD;
	else
		bdp->status &= ~BD_ENET_TX_PAD;

	/* Set buffer length and buffer pointer.
	*/
	bdp->length = skb->len;
	/* bdp->buf = __pa(skb->data); */
	bdp->buf = skb->data;

	/* Save skb pointer.
	*/
	cep->tx_skbuff[cep->skb_cur] = skb;

	/* cep->stats.tx_bytes += skb->len; */ /* TODO: It would really be nice... */

	cep->skb_cur = (cep->skb_cur+1) & TX_RING_MOD_MASK;
	

	/* Push the data cache so the CPM does not get stale memory
	 * data.
	 */
/* 	flush_dcache_range((unsigned long)(skb->data), */
/* 					(unsigned long)(skb->data + skb->len)); */

	/* spin_lock_irq(&cep->lock); */ /* TODO: SPINLOCK */
	local_irq_disable();
	if (cep->lock > 0) {
		printk ("scc_enet_start_xmit() lock == %d\n", cep->lock);
	} else {
		cep->lock++;
	}

	/* Send it on its way.  Tell CPM its ready, interrupt when done,
	 * its the last BD of the frame, and to put the CRC on the end.
	 */
	bdp->status |= (BD_ENET_TX_READY | BD_ENET_TX_INTR | BD_ENET_TX_LAST | BD_ENET_TX_TC);

	dev->trans_start = jiffies;

	/* If this was the last BD in the ring, start at the beginning again.
	*/
	if (bdp->status & BD_ENET_TX_WRAP)
		bdp = cep->tx_bd_base;
	else
		bdp++;

	if (bdp->status & BD_ENET_TX_READY) {
		/* netif_stop_queue(dev); */
		cep->tx_full = 1;
	}

	cep->cur_tx = (QUICC_BD *)bdp;

	/* spin_unlock_irq(&cep->lock); */ /* TODO: SPINLOCK */
	cep->lock--;
	sti();

	return 0;
}

#if 0
static void
scc_enet_timeout(struct net_device *dev)
{
	struct scc_enet_private *cep = (struct scc_enet_private *)dev->priv;

	printk("%s: transmit timed out.\n", dev->name);
	cep->stats.tx_errors++;
#ifndef final_version
	{
		int	i;
		QUICC_BD	*bdp;
		printk(" Ring data dump: cur_tx %p%s cur_rx %p.\n",
		       cep->cur_tx, cep->tx_full ? " (full)" : "",
		       cep->cur_rx);
		bdp = cep->tx_bd_base;
		for (i = 0 ; i < TX_RING_SIZE; i++, bdp++)
			printk("%04x %04x %08x\n",
			       bdp->status,
			       bdp->length,
			       (int)(bdp->buf));
		bdp = cep->rx_bd_base;
		for (i = 0 ; i < RX_RING_SIZE; i++, bdp++)
			printk("%04x %04x %08x\n",
			       bdp->status,
			       bdp->length,
			       (int)(bdp->buf));
	}
#endif
/* 	if (!cep->tx_full) */
/* 		netif_wake_queue(dev); */
}
#endif

/* The interrupt handler.
 * This is called from the CPM handler, not the MPC core interrupt.
 */
static irqreturn_t scc_enet_interrupt(int vec, void *dev_id, struct pt_regs *fp)
{
	struct	net_device *dev = (struct net_device *)dev_id;
	volatile struct	scc_enet_private *cep;
	volatile QUICC_BD	*bdp;
	ushort	int_events;
	int	must_restart;

	cep = (struct scc_enet_private *)dev->priv;

	/* Get the interrupt events that caused us to be here.
	*/
	int_events = cep->sccp->scc_scce;
	cep->sccp->scc_scce = int_events;
	must_restart = 0;

	/* Handle receive event in its own function.
	*/
	if (int_events & SCCE_ENET_RXF)
		scc_enet_rx(dev_id);

	/* Check for a transmit error.  The manual is a little unclear
	 * about this, so the debug code until I get it figured out.  It
	 * appears that if TXE is set, then TXB is not set.  However,
	 * if carrier sense is lost during frame transmission, the TXE
	 * bit is set, "and continues the buffer transmission normally."
	 * I don't know if "normally" implies TXB is set when the buffer
	 * descriptor is closed.....trial and error :-).
	 */

	/* Transmit OK, or non-fatal error.  Update the buffer descriptors.
	*/
	if (int_events & (SCCE_ENET_TXE | SCCE_ENET_TXB)) {
	    /* spin_lock(&cep->lock); */ /* TODO: SPINLOCK */
		/* local_irq_disable(); */
		if (cep->lock > 0) {
			printk ("scc_enet_interrupt() lock == %d\n", cep->lock);
		} else {
			cep->lock++;
		}

	    bdp = cep->dirty_tx;
	    while ((bdp->status&BD_ENET_TX_READY)==0) {
		if ((bdp==cep->cur_tx) && (cep->tx_full == 0))
		    break;

		if (bdp->status & BD_ENET_TX_HB)	/* No heartbeat */
			cep->stats.tx_heartbeat_errors++;
		if (bdp->status & BD_ENET_TX_LC)	/* Late collision */
			cep->stats.tx_window_errors++;
		if (bdp->status & BD_ENET_TX_RL)	/* Retrans limit */
			cep->stats.tx_aborted_errors++;
		if (bdp->status & BD_ENET_TX_UN)	/* Underrun */
			cep->stats.tx_fifo_errors++;
		if (bdp->status & BD_ENET_TX_CSL)	/* Carrier lost */
			cep->stats.tx_carrier_errors++;


		/* No heartbeat or Lost carrier are not really bad errors.
		 * The others require a restart transmit command.
		 */
		if (bdp->status &
		    (BD_ENET_TX_LC | BD_ENET_TX_RL | BD_ENET_TX_UN)) {
			must_restart = 1;
			cep->stats.tx_errors++;
		}

		cep->stats.tx_packets++;

		/* Deferred means some collisions occurred during transmit,
		 * but we eventually sent the packet OK.
		 */
		if (bdp->status & BD_ENET_TX_DEF)
			cep->stats.collisions++;

		/* Free the sk buffer associated with this last transmit.
		*/
		/* dev_kfree_skb_irq(cep->tx_skbuff[cep->skb_dirty]); */
		dev_kfree_skb (cep->tx_skbuff[cep->skb_dirty]);
		cep->skb_dirty = (cep->skb_dirty + 1) & TX_RING_MOD_MASK;

		/* Update pointer to next buffer descriptor to be transmitted.
		*/
		if (bdp->status & BD_ENET_TX_WRAP)
			bdp = cep->tx_bd_base;
		else
			bdp++;

		/* I don't know if we can be held off from processing these
		 * interrupts for more than one frame time.  I really hope
		 * not.  In such a case, we would now want to check the
		 * currently available BD (cur_tx) and determine if any
		 * buffers between the dirty_tx and cur_tx have also been
		 * sent.  We would want to process anything in between that
		 * does not have BD_ENET_TX_READY set.
		 */

		/* Since we have freed up a buffer, the ring is no longer
		 * full.
		 */
		if (cep->tx_full) {
			cep->tx_full = 0;
/* 			if (netif_queue_stopped(dev)) */
/* 				netif_wake_queue(dev); */
		}

		cep->dirty_tx = (QUICC_BD *)bdp;
	    }

	    if (must_restart) {
			volatile QUICC *cp;

		/* Some transmit errors cause the transmitter to shut
		 * down.  We now issue a restart transmit.  Since the
		 * errors close the BD and update the pointers, the restart
		 * _should_ pick up without having to reset any of our
		 * pointers either.
		 */
		cp = pquicc;
		cp->cp_cr =
		    mk_cr_cmd(CPM_CR_ENET, CPM_CR_RESTART_TX) | CPM_CR_FLG;
		while (cp->cp_cr & CPM_CR_FLG);
	    }
	    /* spin_unlock(&cep->lock); */ /* TODO: SPINLOCK */
		/* sti(); */
		cep->lock--;
	}

	/* Check for receive busy, i.e. packets coming but no place to
	 * put them.  This "can't happen" because the receive interrupt
	 * is tossing previous frames.
	 */
	if (int_events & SCCE_ENET_BSY) {
		cep->stats.rx_dropped++;
		printk("CPM ENET: BSY can't happen.\n");
	}

	return IRQ_HANDLED;
}

/* During a receive, the cur_rx points to the current incoming buffer.
 * When we update through the ring, if the next incoming buffer has
 * not been given to the system, we just set the empty indicator,
 * effectively tossing the packet.
 */
static int
scc_enet_rx(struct net_device *dev)
{
	struct	scc_enet_private *cep;
	volatile QUICC_BD	*bdp;
	struct	sk_buff *skb;
	ushort	pkt_len;

	cep = (struct scc_enet_private *)dev->priv;

	/* First, grab all of the stats for the incoming packet.
	 * These get messed up if we get called due to a busy condition.
	 */
	bdp = cep->cur_rx;

	for (;;) {
		if (bdp->status & BD_ENET_RX_EMPTY)
			break;
		
#ifndef final_version
		/* Since we have allocated space to hold a complete frame, both
		 * the first and last indicators should be set.
		 */
		if ((bdp->status & (BD_ENET_RX_FIRST | BD_ENET_RX_LAST)) !=
			(BD_ENET_RX_FIRST | BD_ENET_RX_LAST))
			printk("CPM ENET: rcv is not first+last\n");
#endif

		/* Frame too long or too short.
		 */
		if (bdp->status & (BD_ENET_RX_LG | BD_ENET_RX_SH))
			cep->stats.rx_length_errors++;
		if (bdp->status & BD_ENET_RX_NO)	/* Frame alignment */
			cep->stats.rx_frame_errors++;
		if (bdp->status & BD_ENET_RX_CR)	/* CRC Error */
			cep->stats.rx_crc_errors++;
		if (bdp->status & BD_ENET_RX_OV)	/* FIFO overrun */
			cep->stats.rx_crc_errors++;

		/* Report late collisions as a frame error.
		 * On this error, the BD is closed, but we don't know what we
		 * have in the buffer.  So, just drop this frame on the floor.
		 */
		if (bdp->status & BD_ENET_RX_CL) {
			cep->stats.rx_frame_errors++;
		}
		else {
			
			/* Process the incoming frame.
			 */
			cep->stats.rx_packets++;
			pkt_len = bdp->length;
			/* cep->stats.rx_bytes += pkt_len; */  /* TODO: It would really be nice... */

			/* This does 16 byte alignment, much more than we need.
			 * The packet length includes FCS, but we don't want to
			 * include that when passing upstream as it messes up
			 * bridging applications.
			 */
			skb = dev_alloc_skb(pkt_len-4);

			if (skb == NULL) {
				printk("%s: Memory squeeze, dropping packet.\n", dev->name);
				cep->stats.rx_dropped++;
			}
			else {
				skb->dev = dev;
				skb_put(skb,pkt_len-4);	/* Make room */
				eth_copy_and_sum(skb, (unsigned char *)bdp->buf, pkt_len-4, 0);
				skb->protocol=eth_type_trans(skb,dev);
				netif_rx(skb);
			}
		}

		/* Clear the status flags for this buffer.
		 */
		bdp->status &= ~BD_ENET_RX_STATS;

		/* Mark the buffer empty.
		 */
		bdp->status |= BD_ENET_RX_EMPTY;

		/* Update BD pointer to next entry.
		 */
		if (bdp->status & BD_ENET_RX_WRAP)
			bdp = cep->rx_bd_base;
		else
			bdp++;

	}
	cep->cur_rx = (QUICC_BD *)bdp;

	return 0;
}

static int
scc_enet_close(struct net_device *dev)
{
	/* Don't know what to do yet.
	*/
	/* netif_stop_queue(dev); */

	return 0;
}

/* static struct net_device_stats *scc_enet_get_stats(struct net_device *dev) */
static struct net_device_stats *scc_enet_get_stats(struct net_device *dev)
{
	struct scc_enet_private *cep = (struct scc_enet_private *)dev->priv;

	return &cep->stats;
}

/* Set or clear the multicast filter for this adaptor.
 * Skeleton taken from sunlance driver.
 * The CPM Ethernet implementation allows Multicast as well as individual
 * MAC address filtering.  Some of the drivers check to make sure it is
 * a group multicast address, and discard those that are not.  I guess I
 * will do the same for now, but just remove the test if you want
 * individual filtering as well (do the upper net layers want or support
 * this kind of feature?).
 */

static void set_multicast_list(struct net_device *dev)
{
	struct	scc_enet_private *cep;
	struct	dev_mc_list *dmi;
	u_char	*mcptr, *tdptr;
	volatile scc_enet_t *ep;
	int	i, j;
	volatile QUICC *cp = pquicc;

	cep = (struct scc_enet_private *)dev->priv;

	/* Get pointer to SCC area in parameter RAM.
	*/
	ep = (scc_enet_t *)dev->base_addr;

	if (dev->flags&IFF_PROMISC) {
	  
		/* Log any net taps. */
		printk("%s: Promiscuous mode enabled.\n", dev->name);
		cep->sccp->scc_psmr |= ETHER_PRO; 
	} else {

		cep->sccp->scc_psmr &= ~ETHER_PRO;

		if (dev->flags & IFF_ALLMULTI) {
			/* Catch all multicast addresses, so set the
			 * filter to all 1's.
			 */
			ep->sen_gaddr1 = 0xffff;
			ep->sen_gaddr2 = 0xffff;
			ep->sen_gaddr3 = 0xffff;
			ep->sen_gaddr4 = 0xffff;
		}
		else {
			/* Clear filter and add the addresses in the list.
			*/
			ep->sen_gaddr1 = 0;
			ep->sen_gaddr2 = 0;
			ep->sen_gaddr3 = 0;
			ep->sen_gaddr4 = 0;

			dmi = dev->mc_list;

			for (i=0; i<dev->mc_count; i++) {
				
				/* Only support group multicast for now.
				*/
				if (!(dmi->dmi_addr[0] & 1))
					continue;

				/* The address in dmi_addr is LSB first,
				 * and taddr is MSB first.  We have to
				 * copy bytes MSB first from dmi_addr.
				 */
				mcptr = (u_char *)dmi->dmi_addr + 5;
				tdptr = (u_char *)&ep->sen_taddrh;
				for (j=0; j<6; j++)
					*tdptr++ = *mcptr--;

				/* Ask CPM to run CRC and set bit in
				 * filter mask.
				 */
				cp->cp_cr = mk_cr_cmd(CPM_CR_ENET, CPM_CR_SET_GADDR) | CPM_CR_FLG;
				/* this delay is necessary here -- Cort */
				udelay(10);
				while (cp->cp_cr & CPM_CR_FLG);
			}
		}
	}
}


/* Initialize the CPM Ethernet on SCC.
 */
int scc_enet_init(void)
{
	struct net_device *dev;
	struct scc_enet_private *cep;
	int i, j;
	unsigned char	*eap;
	/* unsigned long	mem_addr; */
	/* pte_t		*pte; */
	/* bd_t		*bd; */ /* `board tag' used by ppc - TODO: integrate uC bootloader vars */
	volatile	QUICC_BD	*bdp;
	volatile	QUICC	*cp;
	volatile struct scc_regs	*sccp;
	volatile struct	ethernet_pram	*ep;
	/* volatile	immap_t		*immap; */

	cp = pquicc;	/* Get pointer to Communication Processor */

	/* immap = (immap_t *)IMAP_ADDR; */	/* and to internal registers */

	/* bd = (bd_t *)__res; */

	/* Allocate some private information.
	*/
	cep = (struct scc_enet_private *)kmalloc(sizeof(*cep), GFP_KERNEL);
	memset(cep, 0, sizeof(*cep));
	/* __clear_user(cep,sizeof(*cep)); */
	/* spin_lock_init(&cep->lock); */ /* TODO: SPINLOCK */

	/* Create an Ethernet device instance.
	 */
	dev = init_etherdev(0, 0);

	/* Get pointer to SCC area in parameter RAM.
	*/
	/* ep = (ethernet_pram *)(&cp->cp_dparam[PROFF_ENET]); */
	ep = &pquicc->pram[SCC_ENET].enet_scc;

	/* And another to the SCC register area.
	*/
	sccp = &pquicc->scc_regs[SCC_ENET];
	cep->sccp = sccp;		/* Keep the pointer handy */

	/* Disable receive and transmit in case EPPC-Bug started it.
	*/
	sccp->scc_gsmr.w.low &= ~(SCC_GSMRL_ENR | SCC_GSMRL_ENT);

	/* Set up 360 pins for SCC interface to ethernet transceiver.
	 * Pin mappings (PA_xx and PC_xx) are defined in commproc.h
	 */

	/* Configure port A pins for Txd and Rxd.
	 */
	pquicc->pio_papar |= (PA_ENET_RXD | PA_ENET_TXD);
	pquicc->pio_padir &= ~(PA_ENET_RXD | PA_ENET_TXD);
	pquicc->pio_paodr &= ~PA_ENET_TXD;

	/* Configure port C pins to enable CLSN and RENA.
	 */
	pquicc->pio_pcpar &= ~(PC_ENET_CLSN | PC_ENET_RENA);
	pquicc->pio_pcdir &= ~(PC_ENET_CLSN | PC_ENET_RENA);
	pquicc->pio_pcso |= (PC_ENET_CLSN | PC_ENET_RENA);

	/* Configure port A for TCLK and RCLK.
	*/
	pquicc->pio_papar |= (PA_ENET_TCLK | PA_ENET_RCLK);
	pquicc->pio_padir &= ~(PA_ENET_TCLK | PA_ENET_RCLK);

	/* Configure Serial Interface clock routing.
	 * First, clear all SCC bits to zero, then set the ones we want.
	 */
	pquicc->si_sicr &= ~SICR_ENET_MASK;
	pquicc->si_sicr |= SICR_ENET_CLKRT;


	/* Allocate space for the buffer descriptors in the DP ram.
	 * These are relative offsets in the DP ram address space.
	 * Initialize base addresses for the buffer descriptors.
	 */
	i = m360_cpm_dpalloc(sizeof(QUICC_BD) * RX_RING_SIZE);
	ep->rbase = i;
	cep->rx_bd_base = (QUICC_BD *)((uint)pquicc + i);

	i = m360_cpm_dpalloc(sizeof(QUICC_BD) * TX_RING_SIZE);
	ep->tbase = i;
	cep->tx_bd_base = (QUICC_BD *)((uint)pquicc + i);

	cep->dirty_tx = cep->cur_tx = cep->tx_bd_base;
	cep->cur_rx = cep->rx_bd_base;

	/* Issue init Rx BD command for SCC.
	 * Manual says to perform an Init Rx parameters here.  We have
	 * to perform both Rx and Tx because the SCC may have been
	 * already running. [In uCquicc's case, I don't think that is so - mles]
	 * In addition, we have to do it later because we don't yet have
	 * all of the BD control/status set properly.
	cp->cp_cpcr = mk_cr_cmd(CPM_CR_ENET, CPM_CR_INIT_RX) | CPM_CR_FLG;
	while (cp->cp_cpcr & CPM_CR_FLG);
	 */

	/* Initialize function code registers for big-endian.
	*/
	ep->rfcr = (SCC_EB | SCC_FC_DMA);
	ep->tfcr = (SCC_EB | SCC_FC_DMA);

	/* Set maximum bytes per receive buffer.
	 * This appears to be an Ethernet frame size, not the buffer
	 * fragment size.  It must be a multiple of four.
	 */
	ep->mrblr  = PKT_MAXBLR_SIZE;

	/* Set CRC preset and mask.
	 */
	ep->c_pres = 0xffffffff;
	ep->c_mask = 0xdebb20e3; /* see 360UM p. 7-247 */

	ep->crcec  = 0;	/* CRC Error counter */
	ep->alec   = 0;	/* alignment error counter */
	ep->disfc  = 0;	/* discard frame counter */

	ep->pads   = 0x8888;	/* Tx short frame pad character */
	ep->ret_lim = 0x000f;	/* Retry limit threshold */

	ep->mflr   = PKT_MAXBUF_SIZE;	/* maximum frame length register */
	ep->minflr = PKT_MINBUF_SIZE;	/* minimum frame length register */

	ep->maxd1 = PKT_MAXBLR_SIZE;	/* maximum DMA1 length */
	ep->maxd2 = PKT_MAXBLR_SIZE;	/* maximum DMA2 length */

	/* Clear hash tables, group and individual.
	 */
	ep->gaddr1 = ep->gaddr2 = ep->gaddr3 = ep->gaddr4 = 0;
	ep->iaddr1 = ep->iaddr2 = ep->iaddr3 = ep->iaddr4 = 0;

	/* Set Ethernet station address.
	 *
	 * The uCbootloader provides a hook to the kernel to retrieve
	 * stuff like the MAC address. This is retrieved in config_BSP()
	 */
#if defined (CONFIG_UCQUICC)
	{
 		extern unsigned char *scc1_hwaddr;

		eap = (char *)ep->paddr.b;
		for (i=5; i>=0; i--)
			*eap++ = dev->dev_addr[i] = scc1_hwaddr[i];
	}
#endif


/* #ifndef CONFIG_MBX */
/* 	eap = (unsigned char *)&(ep->paddrh); */

/* 	for (i=5; i>=0; i--) */
/* 		*eap++ = dev->dev_addr[i] = bd->bi_enetaddr[i]; */
/* #else */
/* 	for (i=5; i>=0; i--) */
/* 		dev->dev_addr[i] = *eap++; */
/* #endif */

	ep->p_per   = 0;	/* 'cause the book says so */
	ep->taddr_l = 0;	/* temp address (LSB) */
	ep->taddr_m = 0;
	ep->taddr_h = 0;	/* temp address (MSB) */

	/* Now allocate the host memory pages and initialize the
	 * buffer descriptors.
	 */
	/* initialize rx buffer descriptors */
	bdp = cep->tx_bd_base;
	for (j=0; j<(TX_RING_SIZE-1); j++) {
		bdp->buf = 0;
		bdp->status = 0;
		bdp++;
	}
	bdp->buf = 0;
	bdp->status = BD_SC_WRAP;


	/* initialize rx buffer descriptors */
	bdp = cep->rx_bd_base;
	for (j=0; j<(RX_RING_SIZE-1); j++) {
		bdp->buf = &rx_buf_pool[j * CPM_ENET_RX_FRSIZE];
		bdp->status = BD_SC_EMPTY | BD_SC_INTRPT;
		bdp++;
	}
	bdp->buf = &rx_buf_pool[j * CPM_ENET_RX_FRSIZE];
	bdp->status = BD_SC_WRAP | BD_SC_EMPTY | BD_SC_INTRPT;



	/* Let's re-initialize the channel now.  We have to do it later
	 * than the manual describes because we have just now finished
	 * the BD initialization.
	 */
	cp->cp_cr = mk_cr_cmd(CPM_CR_ENET, CPM_CR_INIT_TRX) | CPM_CR_FLG;
	while (cp->cp_cr & CPM_CR_FLG);

	cep->skb_cur = cep->skb_dirty = 0;

	sccp->scc_scce = 0xffff;	/* Clear any pending events */

	/* Enable interrupts for transmit error, complete frame
	 * received, and any transmit buffer we have also set the
	 * interrupt flag.
	 */
	sccp->scc_sccm = (SCCE_ENET_TXE | SCCE_ENET_RXF | SCCE_ENET_TXB);

	/* Install our interrupt handler.
	 */
	/* cpm_install_handler(CPMVEC_ENET, scc_enet_interrupt, dev); */
	request_irq(CPMVEC_ENET, scc_enet_interrupt,
		IRQ_FLG_LOCK, dev->name, (void *)dev);

	/* Set GSMR_H to enable all normal operating modes.
	 * Set GSMR_L to enable Ethernet to MC68160.
	 */
	sccp->scc_gsmr.w.high = 0;
	sccp->scc_gsmr.w.low  = (SCC_GSMRL_TCI | SCC_GSMRL_TPL_48 |
							 SCC_GSMRL_TPP_10 | SCC_GSMRL_MODE_ENET);

	/* Set sync/delimiters.
	 */
	sccp->scc_dsr = 0xd555;

	/* Set processing mode.  Use Ethernet CRC, catch broadcast, and
	 * start frame search 22 bit times after RENA.
	 */
	sccp->scc_psmr = (SCC_PMSR_ENCRC       /* Ethernet CRC mode */
			  /* | SCC_PSMR_HBC */ /* Enable heartbeat */
			  /* | SCC_PMSR_PRO */ /* Promiscuous mode */
			  /* | SCC_PMSR_FDE */ /* Full duplex enable */
			  | ETHER_NIB_22);
	/* sccp->scc_psmr = (SCC_PMSR_PRO | ETHER_CRC_32 | ETHER_NIB_22); */


	/* It is now OK to enable the Ethernet transmitter.
	 * Unfortunately, there are board implementation differences here.
	 */
#if defined(CONFIG_UCQUICC)
/* 	 immap->im_ioport.iop_pcpar |= PC_ENET_TENA; */
/* 	 immap->im_ioport.iop_pcdir &= ~PC_ENET_TENA; */
	 cp->pio_pcpar |=  PC_ENET_TENA; /* t_en */
	 cp->pio_pcdir &= ~PC_ENET_TENA;

	 cp->pip_pbpar &= ~(0x00000200); /* power up ethernet transceiver */
	 cp->pip_pbdir |=  (0x00000200);
	 cp->pip_pbdat |=  (0x00000200);
#endif


	dev->base_addr = (unsigned long)ep;
	dev->priv = cep;
#if 0
	dev->name = "CPM_ENET";
#endif

	/* The CPM Ethernet specific entries in the device structure. */
	dev->open = scc_enet_open;
	dev->hard_start_xmit = scc_enet_start_xmit;
	/* dev->tx_timeout = scc_enet_timeout; */
	/* dev->watchdog_timeo = TX_TIMEOUT; */
	dev->stop = scc_enet_close;
	dev->get_stats = scc_enet_get_stats;
	dev->set_multicast_list = set_multicast_list;

	/* And last, enable the transmit and receive processing.
	*/
	sccp->scc_gsmr.w.low |= (SCC_GSMRL_ENR | SCC_GSMRL_ENT);

	printk("%s: CPM ENET Version 0.3, ", dev->name);
	for (i=0; i<5; i++)
		printk("%02x:", dev->dev_addr[i]);
	printk("%02x\n", dev->dev_addr[5]);

	return 0;
}



int m68360_enet_probe(struct device *dev)
{
	return(scc_enet_init ());
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
