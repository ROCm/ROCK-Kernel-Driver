/*********************************************************************
 *
 *	vlsi_ir.c:	VLSI82C147 PCI IrDA controller driver for Linux
 *
 *	Version:	0.1, Aug 6, 2001
 *
 *	Copyright (c) 2001 Martin Diehl
 *
 *	This program is free software; you can redistribute it and/or 
 *	modify it under the terms of the GNU General Public License as 
 *	published by the Free Software Foundation; either version 2 of 
 *	the License, or (at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License 
 *	along with this program; if not, write to the Free Software 
 *	Foundation, Inc., 59 Temple Place, Suite 330, Boston, 
 *	MA 02111-1307 USA
 *
 ********************************************************************/

#include <linux/module.h>
 
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>

#include <net/irda/irda.h>
#include <net/irda/irda_device.h>
#include <net/irda/wrapper.h>
#include <net/irda/irlap.h>

#include <net/irda/vlsi_ir.h>


/********************************************************/


MODULE_DESCRIPTION("IrDA SIR/MIR/FIR driver for VLSI 82C147");
MODULE_AUTHOR("Martin Diehl <info@mdiehl.de>");
MODULE_LICENSE("GPL");



static /* const */ char drivername[] = "vlsi_ir";


#define PCI_CLASS_IRDA_GENERIC 0x0d00

static struct pci_device_id vlsi_irda_table [] __devinitdata = { {

	class:          PCI_CLASS_IRDA_GENERIC << 8,
	vendor:         PCI_VENDOR_ID_VLSI,
	device:         PCI_DEVICE_ID_VLSI_82C147,
	}, { /* all zeroes */ }
};

MODULE_DEVICE_TABLE(pci, vlsi_irda_table);


/********************************************************/


MODULE_PARM(clksrc, "i");
MODULE_PARM_DESC(clksrc, "clock input source selection");

/*	clksrc: which clock source to be used
 *		0: auto - try PLL, fallback to 40MHz XCLK
 *		1: on-chip 48MHz PLL
 *		2: external 48MHz XCLK
 *		3: external 40MHz XCLK (HP OB-800)
 */

static int clksrc = 0;			/* default is 0(auto) */


MODULE_PARM(ringsize, "1-2i");
MODULE_PARM_DESC(ringsize, "tx, rx ring descriptor size");

/*	ringsize: size of the tx and rx descriptor rings
 *		independent for tx and rx
 *		specify as ringsize=tx[,rx]
 *		allowed values: 4, 8, 16, 32, 64
 */

static int ringsize[] = {16,16};	/* default is tx=rx=16 */


MODULE_PARM(sirpulse, "i");
MODULE_PARM_DESC(sirpulse, "sir pulse width tuning");

/*	sirpulse: tuning of the sir pulse width within IrPHY 1.3 limits
 *		0: real short, 1.5us (exception 6us at 2.4kb/s)
 *		1: nominal 3/16 bittime width
 */

static int sirpulse = 1;		/* default is 3/16 bittime */


MODULE_PARM(mtt_bits, "i");
MODULE_PARM_DESC(mtt_bits, "IrLAP bitfield representing min-turn-time");

/*	mtt_bit: encoded min-turn-time values we accept for connections
 *		 according to IrLAP definition (section 6.6.8)
 *		 the widespreadly used HP HDLS-1100 requires 1 msec
 */

static int mtt_bits = 0x07;		/* default is 1 ms or more */


/********************************************************/


/* some helpers for operations on ring descriptors */


static inline int rd_is_active(struct ring_descr *rd)
{
	return ((rd->rd_status & RD_STAT_ACTIVE) != 0);
}

static inline void rd_set_addr_status(struct ring_descr *rd, dma_addr_t a, u8 s)
{
	/* overlayed - order is important! */

	rd->rd_addr = a;
	rd->rd_status = s;
}

static inline void rd_set_status(struct ring_descr *rd, u8 s)
{
	rd->rd_status = s;
}

static inline void rd_set_count(struct ring_descr *rd, u16 c)
{
	rd->rd_count = c;
}

static inline u8 rd_get_status(struct ring_descr *rd)
{
	return rd->rd_status;
}

static inline dma_addr_t rd_get_addr(struct ring_descr *rd)
{
	dma_addr_t	a;

	a = (rd->rd_addr & DMA_MASK_MSTRPAGE) | (MSTRPAGE_VALUE << 24);
	return a;
}

static inline u16 rd_get_count(struct ring_descr *rd)
{
	return rd->rd_count;
}


/* advancing indices pointing into descriptor rings */

static inline void ring_ptr_inc(unsigned *ptr, unsigned mask)
{
	*ptr = (*ptr + 1) & mask;
}


/********************************************************/


#define MAX_PACKET_LEN		2048	/* IrDA MTU */

/* increase transfer buffer size somewhat so we have enough space left
 * when packet size increases during wrapping due to XBOFs and escapes.
 * well, this wastes some memory - anyway, later we will
 * either map skb's directly or use pci_pool allocator...
 */

#define XFER_BUF_SIZE		(MAX_PACKET_LEN+512)

/* the memory required to hold the 2 descriptor rings */

#define RING_AREA_SIZE		(2 * MAX_RING_DESCR * sizeof(struct ring_descr))

/* the memory required to hold the rings' buffer entries */

#define RING_ENTRY_SIZE		(2 * MAX_RING_DESCR * sizeof(struct ring_entry))


/********************************************************/

/* just dump all registers */

static void vlsi_reg_debug(int iobase, const char *s)
{
	int	i;

	mb();
	printk(KERN_DEBUG "%s: ", s);
	for (i = 0; i < 0x20; i++)
		printk("%02x", (unsigned)inb((iobase+i)));
	printk("\n");
}

/********************************************************/


static int vlsi_set_clock(struct pci_dev *pdev)
{
	u8	clkctl, lock;
	int	i, count;

	if (clksrc < 0  ||  clksrc > 3) {
		printk(KERN_ERR "%s: invalid clksrc=%d\n", __FUNCTION__, clksrc);
		return -1;
	}
	if (clksrc < 2) { /* auto or PLL: try PLL */
		clkctl = CLKCTL_NO_PD | CLKCTL_CLKSTP;
		pci_write_config_byte(pdev, VLSI_PCI_CLKCTL, clkctl);

		/* protocol to detect PLL lock synchronisation */
		udelay(500);
		count = 0;
		for (i = 500; i <= 10000; i += 50) { /* max 10 msec */
			pci_read_config_byte(pdev, VLSI_PCI_CLKCTL, &lock);
			if (lock&CLKCTL_LOCK) {
				if (++count >= 3)
					break;
			}
			udelay(50);
		}
		if (count < 3) {
			if (clksrc == 1) { /* explicitly asked for PLL hence bail out */
				printk(KERN_ERR "%s: no PLL or failed to lock!\n",
					__FUNCTION__);
				clkctl = CLKCTL_CLKSTP;
				pci_write_config_byte(pdev, VLSI_PCI_CLKCTL, clkctl);
				return -1;
			}
			else /* was: clksrc=0(auto) */
				clksrc = 3; /* fallback to 40MHz XCLK (OB800) */

			printk(KERN_INFO "%s: PLL not locked, fallback to clksrc=%d\n",
				__FUNCTION__, clksrc);
		}
		else { /* got succesful PLL lock */
			clksrc = 1;
			return 0;
		}
	}

	/* we get here if either no PLL detected in auto-mode or
	   the external clock source explicitly specified */

	clkctl = CLKCTL_EXTCLK | CLKCTL_CLKSTP;
	if (clksrc == 3)
		clkctl |= CLKCTL_XCKSEL;	
	pci_write_config_byte(pdev, VLSI_PCI_CLKCTL, clkctl);

	/* no way to test for working XCLK */

	return 0;
}


static void vlsi_start_clock(struct pci_dev *pdev)
{
	u8	clkctl;

	printk(KERN_INFO "%s: start clock using %s as input\n", __FUNCTION__,
		(clksrc&2)?((clksrc&1)?"40MHz XCLK":"48MHz XCLK"):"48MHz PLL");
	pci_read_config_byte(pdev, VLSI_PCI_CLKCTL, &clkctl);
	clkctl &= ~CLKCTL_CLKSTP;
	pci_write_config_byte(pdev, VLSI_PCI_CLKCTL, clkctl);
}
			

static void vlsi_stop_clock(struct pci_dev *pdev)
{
	u8	clkctl;

	pci_read_config_byte(pdev, VLSI_PCI_CLKCTL, &clkctl);
	clkctl |= CLKCTL_CLKSTP;
	pci_write_config_byte(pdev, VLSI_PCI_CLKCTL, clkctl);
}
			

static void vlsi_unset_clock(struct pci_dev *pdev)
{
	u8	clkctl;

	pci_read_config_byte(pdev, VLSI_PCI_CLKCTL, &clkctl);
	if (!(clkctl&CLKCTL_CLKSTP))
		/* make sure clock is already stopped */
		vlsi_stop_clock(pdev);

	clkctl &= ~(CLKCTL_EXTCLK | CLKCTL_NO_PD);
	pci_write_config_byte(pdev, VLSI_PCI_CLKCTL, clkctl);
}

/********************************************************/


/* ### FIXME: don't use old virt_to_bus() anymore! */

static int vlsi_alloc_buffers_init(vlsi_irda_dev_t *idev)
{
	void *buf;
	int i, j;

	idev->ring_buf = kmalloc(RING_ENTRY_SIZE,GFP_KERNEL);
	if (!idev->ring_buf)
		return -ENOMEM;
	memset(idev->ring_buf, 0, RING_ENTRY_SIZE);

	for (i = MAX_RING_DESCR; i < MAX_RING_DESCR+ringsize[0]; i++) {
		buf = kmalloc(XFER_BUF_SIZE, GFP_KERNEL|GFP_DMA);
		if (!buf) {
			for (j = MAX_RING_DESCR; j < i; j++)
				kfree(idev->ring_buf[j].head);
			kfree(idev->ring_buf);
			idev->ring_buf = NULL;
			return -ENOMEM;
		}
		idev->ring_buf[i].head = buf;
		idev->ring_buf[i].skb = NULL;
		rd_set_addr_status(idev->ring_hw+i,virt_to_bus(buf), 0);
	}

	for (i = 0; i < ringsize[1]; i++) {
		buf = kmalloc(XFER_BUF_SIZE, GFP_KERNEL|GFP_DMA);
		if (!buf) {
			for (j = 0; j < i; j++)
				kfree(idev->ring_buf[j].head);
			for (j = MAX_RING_DESCR; j < MAX_RING_DESCR+ringsize[0]; j++)
				kfree(idev->ring_buf[j].head);
			kfree(idev->ring_buf);
			idev->ring_buf = NULL;
			return -ENOMEM;
		}
		idev->ring_buf[i].head = buf;
		idev->ring_buf[i].skb = NULL;
		rd_set_addr_status(idev->ring_hw+i,virt_to_bus(buf), RD_STAT_ACTIVE);
	}

	return 0;
}


static int vlsi_init_ring(vlsi_irda_dev_t *idev)
{

	idev->tx_mask = MAX_RING_DESCR | (ringsize[0] - 1);
	idev->rx_mask = ringsize[1] - 1;

	idev->ring_hw = pci_alloc_consistent(idev->pdev,
		RING_AREA_SIZE, &idev->busaddr);
	if (!idev->ring_hw) {
		printk(KERN_ERR "%s: insufficient memory for descriptor rings\n",
			__FUNCTION__);
		return -ENOMEM;
	}
#if 0
	printk(KERN_DEBUG "%s: (%d,%d)-ring %p / %p\n", __FUNCTION__,
		ringsize[0], ringsize[1], idev->ring_hw, 
		(void *)(unsigned)idev->busaddr);
#endif
	memset(idev->ring_hw, 0, RING_AREA_SIZE);

	if (vlsi_alloc_buffers_init(idev)) {
		
		pci_free_consistent(idev->pdev, RING_AREA_SIZE,
			idev->ring_hw, idev->busaddr);
		printk(KERN_ERR "%s: insufficient memory for ring buffers\n",
			__FUNCTION__);
		return -1;
	}

	return 0;
}



/********************************************************/



static int vlsi_set_baud(struct net_device *ndev)
{
	vlsi_irda_dev_t *idev = ndev->priv;
	unsigned long flags;
	u16 nphyctl;
	int iobase; 
	u16 config;
	unsigned mode;
	int	ret;
	int	baudrate;

	baudrate = idev->new_baud;
	iobase = ndev->base_addr;

	printk(KERN_DEBUG "%s: %d -> %d\n", __FUNCTION__, idev->baud, idev->new_baud);

	spin_lock_irqsave(&idev->lock, flags);

	outw(0, iobase+VLSI_PIO_IRENABLE);

	if (baudrate == 4000000) {
		mode = IFF_FIR;
		config = IRCFG_FIR;
		nphyctl = PHYCTL_FIR;
	}
	else if (baudrate == 1152000) {
		mode = IFF_MIR;
		config = IRCFG_MIR | IRCFG_CRC16;
		nphyctl = PHYCTL_MIR(baudrate);
	}
	else {
		mode = IFF_SIR;
		config = IRCFG_SIR | IRCFG_SIRFILT | IRCFG_RXANY;
		switch(baudrate) {
			default:
				printk(KERN_ERR "%s: undefined baudrate %d - fallback to 9600!\n",
					__FUNCTION__, baudrate);
				baudrate = 9600;
				/* fallthru */
			case 2400:
			case 9600:
			case 19200:
			case 38400:
			case 57600:
			case 115200:
				nphyctl = PHYCTL_SIR(baudrate,sirpulse,clksrc==3);
				break;
		}
	}

	config |= IRCFG_MSTR | IRCFG_ENRX;

	outw(config, iobase+VLSI_PIO_IRCFG);

	outw(nphyctl, iobase+VLSI_PIO_NPHYCTL);
	wmb();
	outw(IRENABLE_IREN, iobase+VLSI_PIO_IRENABLE);
		/* chip fetches IRCFG on next rising edge of its 8MHz clock */

	mb();
	config = inw(iobase+VLSI_PIO_IRENABLE) & IRENABLE_MASK;

	if (mode == IFF_FIR)
		config ^= IRENABLE_FIR_ON;
	else if (mode == IFF_MIR)
		config ^= (IRENABLE_FIR_ON|IRENABLE_CRC16_ON);
	else
		config ^= IRENABLE_SIR_ON;


	if (config != (IRENABLE_IREN|IRENABLE_ENRXST)) {
		printk(KERN_ERR "%s: failed to set %s mode!\n", __FUNCTION__,
			(mode==IFF_SIR)?"SIR":((mode==IFF_MIR)?"MIR":"FIR"));
		ret = -1;
	}
	else {
		if (inw(iobase+VLSI_PIO_PHYCTL) != nphyctl) {
			printk(KERN_ERR "%s: failed to apply baudrate %d\n",
				__FUNCTION__, baudrate);
			ret = -1;
		}
		else {
			idev->mode = mode;
			idev->baud = baudrate;
			idev->new_baud = 0;
			ret = 0;
		}
	}
	spin_unlock_irqrestore(&idev->lock, flags);

	if (ret)
		vlsi_reg_debug(iobase,__FUNCTION__);

	return ret;
}



static int vlsi_init_chip(struct net_device *ndev)
{
	vlsi_irda_dev_t *idev = ndev->priv;
	u16 ptr;
	unsigned  iobase;


	iobase = ndev->base_addr;

	outw(0, iobase+VLSI_PIO_IRENABLE);

	outb(IRINTR_INT_MASK, iobase+VLSI_PIO_IRINTR); /* w/c pending IRQ, disable all INT */

	/* disable everything, particularly IRCFG_MSTR - which resets the RING_PTR */

	outw(0, iobase+VLSI_PIO_IRCFG);
	wmb();
	outw(IRENABLE_IREN, iobase+VLSI_PIO_IRENABLE);

	mb();

	outw(0, iobase+VLSI_PIO_IRENABLE);

	outw(MAX_PACKET_LEN, iobase+VLSI_PIO_MAXPKT);

	outw(BUS_TO_RINGBASE(idev->busaddr), iobase+VLSI_PIO_RINGBASE);

	outw(TX_RX_TO_RINGSIZE(ringsize[0], ringsize[1]), iobase+VLSI_PIO_RINGSIZE);	


	ptr = inw(iobase+VLSI_PIO_RINGPTR);
	idev->rx_put = idev->rx_get = RINGPTR_GET_RX(ptr);
	idev->tx_put = idev->tx_get = RINGPTR_GET_TX(ptr);

	outw(IRCFG_MSTR, iobase+VLSI_PIO_IRCFG);		/* ready for memory access */
	wmb();
	outw(IRENABLE_IREN, iobase+VLSI_PIO_IRENABLE);

	mb();

	idev->new_baud = 9600;		/* start with IrPHY using 9600(SIR) mode */
	vlsi_set_baud(ndev);

	outb(IRINTR_INT_MASK, iobase+VLSI_PIO_IRINTR);
	wmb();

	/* DO NOT BLINDLY ENABLE IRINTR_ACTEN!
	 * basically every received pulse fires an ACTIVITY-INT
	 * leading to >1000 INT's per second instead of few 10
	 */

	outb(IRINTR_RPKTEN|IRINTR_TPKTEN, iobase+VLSI_PIO_IRINTR);
	wmb();

	return 0;
}


/**************************************************************/


static int vlsi_rx_interrupt(struct net_device *ndev)
{
	vlsi_irda_dev_t *idev = ndev->priv;
	int	iobase;
	int	entry;
	int	len;
	u8	status;
	u16	word;
	struct sk_buff	*skb;
	int	crclen;

	iobase = ndev->base_addr;

	entry = idev->rx_get;

	while ( !rd_is_active(idev->ring_hw+idev->rx_get) ) {

		ring_ptr_inc(&idev->rx_get, idev->rx_mask);

		while (entry != idev->rx_get) {

			status = rd_get_status(idev->ring_hw+entry);

			if (status & RD_STAT_ACTIVE) {
				printk(KERN_CRIT "%s: rx still active!!!\n",
					__FUNCTION__);
				break;
			}
			if (status & RX_STAT_ERROR) {
				idev->stats.rx_errors++;
				if (status & RX_STAT_OVER)  
					idev->stats.rx_over_errors++;
				if (status & RX_STAT_LENGTH)  
					idev->stats.rx_length_errors++;
				if (status & RX_STAT_PHYERR)  
					idev->stats.rx_frame_errors++;
				if (status & RX_STAT_CRCERR)  
					idev->stats.rx_crc_errors++;
			}
			else {
				len = rd_get_count(idev->ring_hw+entry);
				crclen = (idev->mode==IFF_FIR) ? 4 : 2;
				if (len < crclen)
					printk(KERN_ERR "%s: strange frame (len=%d)\n",
						__FUNCTION__, len);
				else
					len -= crclen;		/* remove trailing CRC */

				skb = dev_alloc_skb(len+1);
				if (skb) {
					skb->dev = ndev;
					skb_reserve(skb,1);
					memcpy(skb_put(skb,len), idev->ring_buf[entry].head, len);
					idev->stats.rx_packets++;
					idev->stats.rx_bytes += len;
					skb->mac.raw = skb->data;
					skb->protocol = htons(ETH_P_IRDA);
					netif_rx(skb);				
				}
				else {
					idev->stats.rx_dropped++;
					printk(KERN_ERR "%s: rx packet dropped\n", __FUNCTION__);
				}
			}
			rd_set_count(idev->ring_hw+entry, 0);
			rd_set_status(idev->ring_hw+entry, RD_STAT_ACTIVE);
			ring_ptr_inc(&entry, idev->rx_mask);
		}
	}
	idev->rx_put = idev->rx_get;
	idev->rx_get = entry;

	word = inw(iobase+VLSI_PIO_IRENABLE);
	if (!(word & IRENABLE_ENTXST)) {

		/* only rewrite ENRX, if tx not running!
		 * rewriting ENRX during tx in progress wouldn't hurt
		 * but would be racy since we would also have to rewrite
		 * ENTX then (same register) - which might get disabled meanwhile.
		 */

		outw(0, iobase+VLSI_PIO_IRENABLE);

		word = inw(iobase+VLSI_PIO_IRCFG);
		mb();
		outw(word | IRCFG_ENRX, iobase+VLSI_PIO_IRCFG);
		wmb();
		outw(IRENABLE_IREN, iobase+VLSI_PIO_IRENABLE);
	}
	mb();
	outw(0, iobase+VLSI_PIO_PROMPT);
	return 0;
}


static int vlsi_tx_interrupt(struct net_device *ndev)
{
	vlsi_irda_dev_t *idev = ndev->priv;
	int	iobase;
	int	entry;
	int	ret;
	u16	config;
	u16	status;

	ret = 0;
	iobase = ndev->base_addr;

	entry = idev->tx_get;

	while ( !rd_is_active(idev->ring_hw+idev->tx_get) ) {

		if (idev->tx_get == idev->tx_put) { /* tx ring empty */
			/* sth more to do here? */
			break;
		}
		ring_ptr_inc(&idev->tx_get, idev->tx_mask);
		while (entry != idev->tx_get) {
			status = rd_get_status(idev->ring_hw+entry);
			if (status & RD_STAT_ACTIVE) {
				printk(KERN_CRIT "%s: tx still active!!!\n",
					__FUNCTION__);
				break;
			}
			if (status & TX_STAT_UNDRN) {
				idev->stats.tx_errors++;
				idev->stats.tx_fifo_errors++;
			}
			else {
				idev->stats.tx_packets++;
				idev->stats.tx_bytes += rd_get_count(idev->ring_hw+entry);
			}
			rd_set_count(idev->ring_hw+entry, 0);
			rd_set_status(idev->ring_hw+entry, 0);
			ring_ptr_inc(&entry, idev->tx_mask);
		}
	}

	outw(0, iobase+VLSI_PIO_IRENABLE);
	config = inw(iobase+VLSI_PIO_IRCFG);
	mb();

	if (idev->tx_get != idev->tx_put) {	/* tx ring not empty */
		outw(config | IRCFG_ENTX, iobase+VLSI_PIO_IRCFG);
		ret = 1;			/* no speed-change-check */
	}
	else
		outw((config & ~IRCFG_ENTX) | IRCFG_ENRX, iobase+VLSI_PIO_IRCFG);
	wmb();
	outw(IRENABLE_IREN, iobase+VLSI_PIO_IRENABLE);

	mb();

	outw(0, iobase+VLSI_PIO_PROMPT);
	wmb();

	idev->tx_get = entry;
	if (netif_queue_stopped(ndev)) {
		netif_wake_queue(ndev);
		printk(KERN_DEBUG "%s: queue awoken\n", __FUNCTION__);
	}
	return ret;
}


static int vlsi_act_interrupt(struct net_device *ndev)
{
	printk(KERN_DEBUG "%s\n", __FUNCTION__);
	return 0;
}


static void vlsi_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct net_device *ndev = dev_instance;
	vlsi_irda_dev_t *idev = ndev->priv;
	int		iobase;
	u8		irintr;
	int 		boguscount = 20;
	int		no_speed_check = 0;
	unsigned long	flags;


	iobase = ndev->base_addr;
	spin_lock_irqsave(&idev->lock,flags);
	do {
		irintr = inb(iobase+VLSI_PIO_IRINTR);
		rmb();
		outb(irintr, iobase+VLSI_PIO_IRINTR); /* acknowledge asap */
		wmb();

		if (!(irintr&=IRINTR_INT_MASK))		/* not our INT - probably shared */
			break;

//		vlsi_reg_debug(iobase,__FUNCTION__);

		if (irintr&IRINTR_RPKTINT)
			no_speed_check |= vlsi_rx_interrupt(ndev);

		if (irintr&IRINTR_TPKTINT)
			no_speed_check |= vlsi_tx_interrupt(ndev);

		if ((irintr&IRINTR_ACTIVITY) && !(irintr^IRINTR_ACTIVITY) )
			no_speed_check |= vlsi_act_interrupt(ndev);

		if (irintr & ~(IRINTR_RPKTINT|IRINTR_TPKTINT|IRINTR_ACTIVITY))
			printk(KERN_DEBUG "%s: IRINTR = %02x\n",
				__FUNCTION__, (unsigned)irintr);
			
	} while (--boguscount > 0);
	spin_unlock_irqrestore(&idev->lock,flags);

	if (boguscount <= 0)
		printk(KERN_ERR "%s: too much work in interrupt!\n", __FUNCTION__);

	else if (!no_speed_check) {
		if (idev->new_baud)
			vlsi_set_baud(ndev);
	}
}


/**************************************************************/

static int vlsi_open(struct net_device *ndev)
{
	vlsi_irda_dev_t *idev = ndev->priv;
	struct pci_dev *pdev = idev->pdev;
	char	hwname[32];
	int	err;

	MOD_INC_USE_COUNT;		/* still needed? - we have SET_MODULE_OWNER! */

	if (pci_request_regions(pdev,drivername)) {
		printk(KERN_ERR "%s: io resource busy\n", __FUNCTION__);
		MOD_DEC_USE_COUNT;
		return -EAGAIN;
	}

	if (request_irq(ndev->irq, vlsi_interrupt, SA_SHIRQ|SA_INTERRUPT,
			drivername, ndev)) {
		printk(KERN_ERR "%s: couldn't get IRQ: %d\n",
			__FUNCTION__, ndev->irq);
		pci_release_regions(pdev);
		MOD_DEC_USE_COUNT;
		return -EAGAIN;
	}
	printk(KERN_INFO "%s: got resources for %s - irq=%d / io=%04lx\n",
		__FUNCTION__, ndev->name, ndev->irq, ndev->base_addr );

	if (vlsi_set_clock(pdev)) {
		printk(KERN_ERR "%s: no valid clock source\n",
			__FUNCTION__);
		free_irq(ndev->irq,ndev);
		pci_release_regions(pdev);
		MOD_DEC_USE_COUNT;
		return -EIO;
	}

	vlsi_start_clock(pdev);

	err = vlsi_init_ring(idev);
	if (err) {
		vlsi_unset_clock(pdev);
		free_irq(ndev->irq,ndev);
		pci_release_regions(pdev);
		MOD_DEC_USE_COUNT;
		return err;
	}

	vlsi_init_chip(ndev);

	printk(KERN_INFO "%s: IrPHY setup: %d baud (%s), %s SIR-pulses\n",
		__FUNCTION__, idev->baud, 
		(idev->mode==IFF_SIR)?"SIR":((idev->mode==IFF_MIR)?"MIR":"FIR"),
		(sirpulse)?"3/16 bittime":"short");

	sprintf(hwname, "VLSI-FIR");
	idev->irlap = irlap_open(ndev,&idev->qos,hwname);

	netif_start_queue(ndev);

	printk(KERN_INFO "%s: device %s operational using (%d,%d) tx,rx-ring\n",
		__FUNCTION__, ndev->name, ringsize[0], ringsize[1]);

	return 0;
}


static int vlsi_close(struct net_device *ndev)
{
	vlsi_irda_dev_t *idev = ndev->priv;
	struct pci_dev *pdev = idev->pdev;
	int	i;
	u8	cmd;
	unsigned iobase;


	iobase = ndev->base_addr;
	netif_stop_queue(ndev);

	if (idev->irlap)
		irlap_close(idev->irlap);
	idev->irlap = NULL;

	outb(IRINTR_INT_MASK, iobase+VLSI_PIO_IRINTR);	/* w/c pending + disable further IRQ */
	wmb();
	outw(0, iobase+VLSI_PIO_IRENABLE);
	outw(0, iobase+VLSI_PIO_IRCFG);			/* disable everything */
	wmb();
	outw(IRENABLE_IREN, iobase+VLSI_PIO_IRENABLE);
	mb();				/* from now on */

	outw(0, iobase+VLSI_PIO_IRENABLE);
	wmb();

	vlsi_stop_clock(pdev);

	vlsi_unset_clock(pdev);

	free_irq(ndev->irq,ndev);

	if (idev->ring_buf) {
		for (i = 0; i < 2*MAX_RING_DESCR; i++) {
			if (idev->ring_buf[i].head)
				kfree(idev->ring_buf[i].head);
		}
		kfree(idev->ring_buf);
	}

	if (idev->busaddr)
		pci_free_consistent(idev->pdev,RING_AREA_SIZE,idev->ring_hw,idev->busaddr);

	idev->ring_buf = NULL;
	idev->ring_hw = NULL;
	idev->busaddr = 0;

	pci_read_config_byte(pdev, PCI_COMMAND, &cmd);
	cmd &= ~PCI_COMMAND_MASTER;
	pci_write_config_byte(pdev, PCI_COMMAND, cmd);

	pci_release_regions(pdev);

	printk(KERN_INFO "%s: device %s stopped\n", __FUNCTION__, ndev->name);

	MOD_DEC_USE_COUNT;
	return 0;
}

static struct net_device_stats * vlsi_get_stats(struct net_device *ndev)
{
	vlsi_irda_dev_t *idev = ndev->priv;

	return &idev->stats;
}

static int vlsi_hard_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	vlsi_irda_dev_t *idev = ndev->priv;
	unsigned long flags;
	int iobase;
	u8 status;
	u16 config;
	int mtt;
	int entry;
	int len, speed;


	iobase = ndev->base_addr;

	speed = irda_get_next_speed(skb);

	if (speed != -1  &&  speed != idev->baud) {
		idev->new_baud = speed;
		if (!skb->len) {
			dev_kfree_skb(skb);
			vlsi_set_baud(ndev);
			return 0;
		}
		status = TX_STAT_CLRENTX;  /* stop tx-ring after this frame */
	}
	else
		status = 0;


	spin_lock_irqsave(&idev->lock,flags);

	entry = idev->tx_put;

	if (idev->mode == IFF_SIR) {
		status |= TX_STAT_DISCRC;
		len = async_wrap_skb(skb, idev->ring_buf[entry].head,
			XFER_BUF_SIZE);
	}
	else {				/* hw deals with MIR/FIR mode */
		len = skb->len;
		memcpy(idev->ring_buf[entry].head, skb->data, len);
	}

	if (len == 0)
		printk(KERN_ERR "%s: sending 0-size packet???\n",
			__FUNCTION__);

	status |= RD_STAT_ACTIVE;

	rd_set_count(idev->ring_hw+entry, len);
	rd_set_status(idev->ring_hw+entry, status);
	ring_ptr_inc(&idev->tx_put, idev->tx_mask);

	dev_kfree_skb(skb);	

#if 0
	printk(KERN_DEBUG "%s: dump entry %d: %u %02x %08x\n",
		__FUNCTION__, entry,
		idev->ring_hw[entry].rd_count,
		(unsigned)idev->ring_hw[entry].rd_status,
		idev->ring_hw[entry].rd_addr & 0xffffffff);
	vlsi_reg_debug(iobase,__FUNCTION__);
#endif

/*
 *	race window due to concurrent controller processing!
 *
 *	we may loose ENTX at any time when the controller
 *	fetches an inactive descr or one with CLR_ENTX set.
 *	therefore we only rely on the controller telling us
 *	tx is already stopped because (cannot restart without PROMPT).
 *	instead we depend on the tx-complete-isr to detect the
 *	false negatives and retrigger the tx ring.
 *	that's why we need interrupts disabled till tx has been
 *	kicked, so the tx-complete-isr was either already finished
 *	before we've put the new active descriptor on the ring - or
 *	the isr will be called after the new active descr is on the
 *	ring _and_ the ring was prompted. Making these two steps
 *	atomic allows to resolve the race.
 */

	iobase = ndev->base_addr;

	if (!(inw(iobase+VLSI_PIO_IRENABLE) & IRENABLE_ENTXST)) {

		mtt = irda_get_mtt(skb);
		if (mtt) {
			udelay(mtt);		/* ### FIXME ... */
		}

		outw(0, iobase+VLSI_PIO_IRENABLE);

		config = inw(iobase+VLSI_PIO_IRCFG);
		rmb();
		outw(config | IRCFG_ENTX, iobase+VLSI_PIO_IRCFG);
		wmb();
		outw(IRENABLE_IREN, iobase+VLSI_PIO_IRENABLE);

		mb();

		outw(0, iobase+VLSI_PIO_PROMPT);
		wmb();
	}

	spin_unlock_irqrestore(&idev->lock, flags);

	if (idev->tx_put == idev->tx_get) {
		netif_stop_queue(ndev);
		printk(KERN_DEBUG "%s: tx ring full - queue stopped: %d/%d\n",
			__FUNCTION__, idev->tx_put, idev->tx_get);
		entry = idev->tx_get;
		printk(KERN_INFO "%s: dump stalled entry %d: %u %02x %08x\n",
			__FUNCTION__, entry,
			idev->ring_hw[entry].rd_count,
			(unsigned)idev->ring_hw[entry].rd_status,
			idev->ring_hw[entry].rd_addr & 0xffffffff);
		vlsi_reg_debug(iobase,__FUNCTION__);
	}

//	vlsi_reg_debug(iobase, __FUNCTION__);

	return 0;
}


static int vlsi_ioctl(struct net_device *ndev, struct ifreq *rq, int cmd)
{
	vlsi_irda_dev_t *idev = ndev->priv;
	struct if_irda_req *irq = (struct if_irda_req *) rq;
	unsigned long flags;
	u16 fifocnt;
	int ret = 0;

	spin_lock_irqsave(&idev->lock,flags);
	switch (cmd) {
		case SIOCSBANDWIDTH:
			if (!capable(CAP_NET_ADMIN)) {
				ret = -EPERM;
				break;
			}
			idev->new_baud = irq->ifr_baudrate;
			break;
		case SIOCSMEDIABUSY:
			if (!capable(CAP_NET_ADMIN)) {
				ret = -EPERM;
				break;
			}
			irda_device_set_media_busy(ndev, TRUE);
			break;
		case SIOCGRECEIVING:
			fifocnt = inw(ndev->base_addr+VLSI_PIO_RCVBCNT) & RCVBCNT_MASK;
			irq->ifr_receiving = (fifocnt!=0) ? 1 : 0;
			break;
		default:
			printk(KERN_ERR "%s: notsupp - cmd=%04x\n",
				__FUNCTION__, cmd);
			ret = -EOPNOTSUPP;
	}	
	spin_unlock_irqrestore(&idev->lock,flags);
	
	return ret;
}



int vlsi_irda_init(struct net_device *ndev)
{
	vlsi_irda_dev_t *idev = ndev->priv;
	struct pci_dev *pdev = idev->pdev;
	u8	byte;


	SET_MODULE_OWNER(ndev);

	ndev->irq = pdev->irq;
	ndev->base_addr = pci_resource_start(pdev,0);

	/* PCI busmastering - see include file for details! */

	if (pci_set_dma_mask(pdev,DMA_MASK_USED_BY_HW)) {
		printk(KERN_ERR "%s: aborting due to PCI BM-DMA address limitations\n",
			__FUNCTION__);
		return -1;
	}
	pci_set_master(pdev);

	pdev->dma_mask = DMA_MASK_MSTRPAGE;
	pci_write_config_byte(pdev, VLSI_PCI_MSTRPAGE, MSTRPAGE_VALUE);

	/* we don't use the legacy UART, disable its address decoding */

	pci_read_config_byte(pdev, VLSI_PCI_IRMISC, &byte);
	byte &= ~(IRMISC_UARTEN | IRMISC_UARTTST);
	pci_write_config_byte(pdev, VLSI_PCI_IRMISC, byte);


	irda_init_max_qos_capabilies(&idev->qos);

	/* the VLSI82C147 does not support 576000! */

	idev->qos.baud_rate.bits = IR_2400 | IR_9600
		| IR_19200 | IR_38400 | IR_57600 | IR_115200
		| IR_1152000 | (IR_4000000 << 8);

	idev->qos.min_turn_time.bits = mtt_bits;

	irda_qos_bits_to_value(&idev->qos);

	irda_device_setup(ndev);

	/* currently no media definitions for SIR/MIR/FIR */

	ndev->flags |= IFF_PORTSEL | IFF_AUTOMEDIA;
	ndev->if_port = IF_PORT_UNKNOWN;
 
	ndev->open	      = vlsi_open;
	ndev->stop	      = vlsi_close;
	ndev->get_stats	      = vlsi_get_stats;
	ndev->hard_start_xmit = vlsi_hard_start_xmit;
	ndev->do_ioctl	      = vlsi_ioctl;

	return 0;
}	

/**************************************************************/

static int __devinit
vlsi_irda_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct net_device	*ndev;
	vlsi_irda_dev_t		*idev;
	int			alloc_size;

	printk(KERN_INFO "%s: found IrDA PCI controler %s\n", drivername, pdev->name);

	if (pci_enable_device(pdev))
		goto out;

	if ( !pci_resource_start(pdev,0)
	     || !(pci_resource_flags(pdev,0) & IORESOURCE_IO) ) {
		printk(KERN_ERR "%s: bar 0 invalid", __FUNCTION__);
		goto out;
	}

	alloc_size = sizeof(*ndev) + sizeof(*idev);

	ndev = (struct net_device *) kmalloc (alloc_size, GFP_KERNEL);
	if (ndev==NULL) {
		printk(KERN_ERR "%s: Unable to allocate device memory.\n",
			__FUNCTION__);
		goto out;
	}

	memset(ndev, 0, alloc_size);

	idev = (vlsi_irda_dev_t *) (ndev + 1);
	ndev->priv = (void *) idev;

	spin_lock_init(&idev->lock);
	idev->pdev = pdev;
	ndev->init = vlsi_irda_init;
	strcpy(ndev->name,"irda%d");
	if (register_netdev(ndev)) {
		printk(KERN_ERR "%s: register_netdev failed\n",
			__FUNCTION__);
		goto out_freedev;
	}
	printk(KERN_INFO "%s: registered device %s\n", drivername, ndev->name);

	pdev->driver_data = ndev;

	return 0;

out_freedev:
	kfree(ndev);
out:
	pdev->driver_data = NULL;
	return -ENODEV;
}

static void __devexit vlsi_irda_remove(struct pci_dev *pdev)
{
	struct net_device *ndev = pdev->driver_data;

	if (ndev) {
		printk(KERN_INFO "%s: unregister device %s\n",
			drivername, ndev->name);

		unregister_netdev(ndev);
		/* do not free - async completed by unregister_netdev()
		 * ndev->destructor called (if present) when going to free
		 */

	}
	else
		printk(KERN_CRIT "%s: lost netdevice?\n", drivername);
	pdev->driver_data = NULL;

	pci_disable_device(pdev);
	printk(KERN_INFO "%s: %s disabled\n", drivername, pdev->name);
}

static int vlsi_irda_suspend(struct pci_dev *pdev, u32 state)
{
	printk(KERN_ERR "%s - %s\n", __FUNCTION__, pdev->name);
	return 0;
}

static int vlsi_irda_resume(struct pci_dev *pdev)
{
	printk(KERN_ERR "%s - %s\n", __FUNCTION__, pdev->name);
	return 0;
}

/*********************************************************/

static struct pci_driver vlsi_irda_driver = {
	name:           drivername,
	id_table:       vlsi_irda_table,
	probe:          vlsi_irda_probe,
	remove:         vlsi_irda_remove,
	suspend:        vlsi_irda_suspend,
	resume:         vlsi_irda_resume,
};

static int __init vlsi_mod_init(void)
{
	if (clksrc < 0  ||  clksrc > 3) {
		printk(KERN_ERR "%s: invalid clksrc=%d\n", __FUNCTION__, clksrc);
		return -1;
	}
	if ( ringsize[0]==0  ||  (ringsize[0] & ~(64|32|16|8|4))
	     ||  ((ringsize[0]-1)&ringsize[0])) {
		printk(KERN_INFO "%s: invalid tx ringsize %d - using default=16\n",
			__FUNCTION__, ringsize[0]);
		ringsize[0] = 16;
	} 
	if ( ringsize[1]==0  ||  (ringsize[1] & ~(64|32|16|8|4))
	     ||  ((ringsize[1]-1)&ringsize[1])) {
		printk(KERN_INFO "%s: invalid rx ringsize %d - using default=16\n",
			__FUNCTION__, ringsize[1]);
		ringsize[1] = 16;
	}
	sirpulse = !!sirpulse;
	return pci_module_init(&vlsi_irda_driver);
}

static void __exit vlsi_mod_exit(void)
{
	pci_unregister_driver(&vlsi_irda_driver);
}

module_init(vlsi_mod_init);
module_exit(vlsi_mod_exit);

