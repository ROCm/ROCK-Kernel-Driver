/*
 * xircom_cb: A driver for the (tulip-like) Xircom Cardbus ethernet cards 
 *
 * This software is Copyright 2001 by the respective authors, and licensed under the GPL
 * License.
 *
 * Written by Arjan van de Ven for Red Hat, Inc.
 * Based on work by Jeff Garzik, Doug Ledford, Donald Becker and Ion Badulescu
 *
 *  	This software may be used and distributed according to the terms
 *      of the GNU General Public License, incorporated herein by reference.
 *
 *
 * 	$Id: xircom_cb.c,v 1.11 2001/06/05 09:50:57 fenrus Exp $
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <asm/io.h>



#ifdef DEBUG
#define enter()   printk("Enter: %s, %s line %i\n",__FUNCTION__,__FILE__,__LINE__)
#define leave()   printk("Leave: %s, %s line %i\n",__FUNCTION__,__FILE__,__LINE__)
#else
#define enter()   do {} while (0)
#define leave()   do {} while (0)
#endif


MODULE_DESCRIPTION("Xircom Cardbus ethernet driver");
MODULE_AUTHOR("Arjan van de Ven <arjanv@redhat.com>");
MODULE_LICENSE("GPL");



/* IO registers on the card, offsets */
#define CSR0	0x00
#define CSR1	0x08
#define CSR2	0x10
#define CSR3	0x18
#define CSR4	0x20
#define CSR5	0x28
#define CSR6	0x30
#define CSR7	0x38
#define CSR8	0x40
#define CSR9	0x48
#define CSR10	0x50
#define CSR11	0x58
#define CSR12	0x60
#define CSR13	0x68
#define CSR14	0x70
#define CSR15	0x78
#define CSR16	0x80

/* PCI registers */
#define PCI_POWERMGMT 	0x40

/* Offsets of the buffers within the descriptor pages, in bytes */

#define NUMDESCRIPTORS 4
#define RXTXBUFSIZE 8192
#define MAX_PACKETSIZE 1536


#define DescOwnedCard	0x80000000
#define DescOwnedDriver	0x00000000

#define PromiscBit		(1<<6)
#define CollisionBit 		(1<<8)
#define TxActiveBit		(1<<13)
#define RxActiveBit		(1<<1)
#define LastDescBit	 	(1<<25)
#define LinkStatusBit 		(1<<27)

#define PowerMgmtBits		( (1<<31)|(1<<30) )

static const unsigned int bufferoffsets[NUMDESCRIPTORS] = {128,2048,4096,6144};

/* note: this struct is assumed to be packed as this is the "hardware" layout */
struct descriptor {
	u32	status;
	u32	control;
	u32	address1;
	u32	address2;
};


struct xircom_private {
	/* Send and receive buffers, kernel-addressable and dma addressable forms */

	unsigned char *rx_buffer;
	unsigned char *tx_buffer;
	
	struct descriptor *rx_desc;
	struct descriptor *tx_desc;

	dma_addr_t rx_dma_handle;
	dma_addr_t tx_dma_handle;

	struct sk_buff *tx_skb[NUMDESCRIPTORS];

	unsigned long io_port;
	
	/* transmit_used is the rotating counter that indicates which transmit
	   descriptor has to be used next */
	unsigned int transmit_used;

	/* Spinlock to serialize register operations.
	   It must be helt while manipulating the following registers:
	   CSR0, CSR6, CSR7, CSR9, CSR10, CSR15
	 */
	spinlock_t lock;


	struct pci_dev *pdev;
	struct net_device *dev;
	struct net_device_stats stats;
};


/* Function prototypes */
static int xircom_probe(struct pci_dev *pdev, const struct pci_device_id *id);
static void xircom_remove(struct pci_dev *pdev);
static void xircom_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
static int xircom_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int xircom_open(struct net_device *dev);
static int xircom_close(struct net_device *dev);
static void xircom_up(struct xircom_private *card);
static struct net_device_stats *xircom_get_stats(struct net_device *dev);

static void investigate_rx_descriptor(struct net_device *dev,struct xircom_private *card, int descnr, unsigned int bufferoffset);
static unsigned int investigate_tx_descriptor(struct net_device *dev, struct xircom_private *card, unsigned int descnr, unsigned int bufferoffset);
static void read_mac_address(struct xircom_private *card);
static void tranceiver_voodoo(struct xircom_private *card);
static void initialize_card(struct xircom_private *card);
static inline void trigger_transmit(struct xircom_private *card);
static inline void trigger_receive(struct xircom_private *card);
static void setup_descriptors(struct xircom_private *card);
static inline void remove_descriptors(struct xircom_private *card);
static inline unsigned int link_status_changed(struct xircom_private *card);
static void activate_receiver(struct xircom_private *card);
static void deactivate_receiver(struct xircom_private *card);
static void activate_transmitter(struct xircom_private *card);
static void deactivate_transmitter(struct xircom_private *card);
static void enable_transmit_interrupt(struct xircom_private *card);
static void enable_receive_interrupt(struct xircom_private *card);
static void enable_link_interrupt(struct xircom_private *card);
static void disable_all_interrupts(struct xircom_private *card);
static inline unsigned int link_status(struct xircom_private *card);
static int mdio_read(struct xircom_private *card, int phy_id, int location);
static void mdio_write(struct xircom_private *card, int phy_id, int location, int value);



static struct pci_device_id xircom_pci_table[] __devinitdata = {
	{0x115D, 0x0003, PCI_ANY_ID, PCI_ANY_ID,},
	{0,},
};
MODULE_DEVICE_TABLE(pci, xircom_pci_table);

static struct pci_driver xircom_ops = {
	name:		"xircom_cb", 
	id_table:	xircom_pci_table, 
	probe:		xircom_probe, 
	remove:		__devexit_p(xircom_remove), 
};


#ifdef DEBUG
static void print_binary(unsigned int number)
{
	int i,i2;
	char buffer[64];
	memset(buffer,0,64);
	i2=0;
	for (i=31;i>=0;i--) {
		if (number & (1<<i))
			buffer[i2++]='1';
		else
			buffer[i2++]='0';
		if ((i&3)==0) 
			buffer[i2++]=' ';
	}
	printk("%s\n",buffer);
}
#endif

/* xircom_probe is the code that gets called on device insertion.
   it sets up the hardware and registers the device to the networklayer.
   
   TODO: Send 1 or 2 "dummy" packets here as the card seems to discard the
         first two packets that get send, and pump hates that.
         
 */
static int __devinit xircom_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct net_device *dev = NULL;
	struct xircom_private *private;
	u8 chip_rev;
	unsigned long flags;
	u32 tmp32;
	u16 tmp16;
	int ret;
	enter();
	
	/* First do the PCI initialisation */

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	/* disable all powermanagement */
	pci_read_config_dword(pdev, PCI_POWERMGMT,&tmp32);
	tmp32 &= ~PowerMgmtBits; 
	pci_write_config_dword(pdev, PCI_POWERMGMT, tmp32);
	
	pci_set_master(pdev); 

	/* clear PCI status, if any */ 
	pci_read_config_word (pdev,PCI_STATUS, &tmp16); 
	pci_write_config_word (pdev, PCI_STATUS,tmp16);
	
	pci_read_config_byte(pdev, PCI_REVISION_ID, &chip_rev);
	
	if (!request_region(pci_resource_start(pdev, 0), 128, "xircom_cb")) {
		printk(KERN_ERR "xircom_probe: failed to allocate io-region\n");
		return -ENODEV;
	}

	
	dev = init_etherdev(dev, sizeof(*private));
	if (dev == NULL) {
		printk(KERN_ERR "xircom_probe: failed to allocate etherdev\n");
		return -ENODEV;
	}
	SET_MODULE_OWNER(dev);
	private = dev->priv;
	if (private==NULL) {
		printk(KERN_ERR "xircom_probe: failed to allocate private device struct\n");
		return -ENODEV;
	}
		
	/* Allocate the send/receive buffers */
	private->rx_buffer = pci_alloc_consistent(pdev,RXTXBUFSIZE,&private->rx_dma_handle);
	if (private->rx_buffer == NULL) {
 		printk(KERN_ERR "xircom_probe: no memory for rx buffer \n");
 		kfree(private);
		return -ENODEV;
	}	
	/* the descriptors are stored in the first bytes of the rx_buffer, hence the ugly cast */
	private->rx_desc = (struct descriptor *)private->rx_buffer;

	private->tx_buffer = pci_alloc_consistent(pdev,RXTXBUFSIZE,&private->tx_dma_handle);
	if (private->tx_buffer == NULL) {
		printk(KERN_ERR "xircom_probe: no memory for tx buffer \n");
		kfree(private->rx_buffer);
		kfree(private);
		return -ENODEV;
	}
	/* the descriptors are stored in the first bytes of the tx_buffer, hence the ugly cast */
	private->tx_desc = (struct descriptor *)private->tx_buffer;
	
	
	printk(KERN_INFO "%s: Xircom cardbus revision %i at irq %i \n", dev->name, chip_rev, pdev->irq);

	private->dev = dev;
	private->pdev = pdev;
	private->io_port = pci_resource_start(pdev, 0);
	private->lock = SPIN_LOCK_UNLOCKED;
	dev->irq = pdev->irq;
	dev->base_addr = private->io_port;
	
	
	initialize_card(private);
	read_mac_address(private);
	setup_descriptors(private);
	
	dev->open = &xircom_open;
	dev->hard_start_xmit = &xircom_start_xmit;
	dev->stop = &xircom_close;
	dev->get_stats = &xircom_get_stats;
	dev->priv = private;
	pci_set_drvdata(pdev,dev);

	
	/* start the transmitter to get a heartbeat; don't do
	   that when there already is one though; Cisco's 
	   really don't like that. */
	if (!link_status(private))
		tranceiver_voodoo(private);
	
	spin_lock_irqsave(&private->lock,flags);
	  activate_transmitter(private);
	  activate_receiver(private);
	spin_unlock_irqrestore(&private->lock,flags);

	/* TODO: send 2 dummy packets here */
	
	trigger_receive(private);
	
	leave();
	return 0;
}


/*
 xircom_remove is called on module-unload or on device-eject.
 it unregisters the irq, io-region and network device.
 Interrupts and such are already stopped in the "ifconfig ethX down"
 code.
 */
static void __devexit xircom_remove(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct xircom_private *card;
	enter();
	
	card=dev->priv;

	if (card->rx_buffer!=NULL)
		pci_free_consistent(pdev,RXTXBUFSIZE,card->rx_buffer,card->rx_dma_handle);
	card->rx_buffer = NULL;
	card->rx_desc = NULL;
	if (card->tx_buffer!=NULL)
		pci_free_consistent(pdev,RXTXBUFSIZE,card->tx_buffer,card->tx_dma_handle);
	card->tx_buffer = NULL;			
	card->tx_desc = NULL;

	release_region(dev->base_addr, 128);
	unregister_netdev(dev);
	kfree(dev);
	pci_set_drvdata(pdev,NULL);
	leave();
} 

static void xircom_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct net_device *dev = dev_instance;
	struct xircom_private *card = dev->priv;
	u32 status;
	unsigned int xmit_free_count;
	unsigned int i;

	enter();


	spin_lock(&card->lock);
	status = inl(card->io_port+CSR5);
	if (status==0xffffffff) {/* card has been ejected / powered down */
		spin_unlock(&card->lock);
		return;
	}

	/* Todo: check if there were any events at all; to speed up
	   returning if we're on a shared interrupt */	

	if (link_status_changed(card)) {
		int newlink;
		printk(KERN_DEBUG "xircom_cb: Link status has changed \n");
		newlink = link_status(card);
		if (newlink) {
			printk(KERN_INFO  "xircom_cb: Link is %i mbit \n",newlink);
			netif_carrier_on(dev);
		} else {
			printk(KERN_INFO  "xircom_cb: Link is absent \n");
			netif_carrier_off(dev);
		}
	}

	/* Clear all remaining interrupt events */	
	status |= 0xffffffff; /* FIXME: make this clear only the
				        real existing bits */
	outl(status,card->io_port+CSR5);
	
	xmit_free_count = 0;

	for (i=0;i<NUMDESCRIPTORS;i++) 
		xmit_free_count += investigate_tx_descriptor(dev,card,i,bufferoffsets[i]);
	for (i=0;i<NUMDESCRIPTORS;i++) 
		investigate_rx_descriptor(dev,card,i,bufferoffsets[i]);

	
	if (xmit_free_count)
		netif_start_queue(dev);
		
	spin_unlock(&card->lock);
	leave();
}

static int xircom_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct xircom_private *card;
	unsigned long flags;
	unsigned int nextdescriptor;
	unsigned int desc;
	enter();
	
	card = (struct xircom_private*)dev->priv;

	spin_lock_irqsave(&card->lock,flags);
	
	nextdescriptor = (card->transmit_used +1) % (NUMDESCRIPTORS);
	desc = card->transmit_used;
	
	/* only send the packet if the descriptor is free */
	if (card->tx_desc[desc].status==0) {
			/* Copy the packet data; zero the memory first as the card
			   sometimes sends more than you ask it to. */
			
			memset(&card->tx_buffer[bufferoffsets[desc]],0,MAX_PACKETSIZE);
			memcpy(&(card->tx_buffer[bufferoffsets[desc]]),skb->data,skb->len);
	
	
			/* FIXME: The specification tells us that the length we send HAS to be a multiple of
			   4 bytes. */
			   
			card->tx_desc[desc].control = skb->len;
			if (desc == NUMDESCRIPTORS-1)
				card->tx_desc[desc].control |= LastDescBit;  /* bit 25: last descriptor of the ring */

			card->tx_desc[desc].control |= 0xF0000000;
						 /* 0xF0... means want interrupts*/ 
			card->tx_skb[desc] = skb;
			
			wmb();
			/* This gives the descriptor to the card */
			card->tx_desc[desc].status = DescOwnedCard;
			trigger_transmit(card);
			if (((int)card->tx_desc[nextdescriptor].status)<0) {	/* next descriptor is occupied... */
				netif_stop_queue(dev);
			}
			card->transmit_used = nextdescriptor;
			spin_unlock_irqrestore(&card->lock,flags);
			leave();
			return 0;
	}
	


	/* Uh oh... no free descriptor... drop the packet */
	/* This should not happen in theory...*/
	netif_stop_queue(dev);
	spin_unlock_irqrestore(&card->lock,flags);
	trigger_transmit(card);
	leave();
	
	return -EIO;
}




static int xircom_open(struct net_device *dev)
{
	struct xircom_private *xp = (struct xircom_private *) dev->priv;
	int retval;
	enter();
	printk(KERN_INFO "Xircom cardbus adaptor found, registering as %s, using irq %i \n",dev->name,dev->irq);
	retval = request_irq(dev->irq, &xircom_interrupt, SA_SHIRQ, dev->name, dev);
	if (retval) {
		printk(KERN_ERR "xircom_cb: Unable to aquire IRQ %i, aborting.\n",dev->irq);
		leave();
		return retval;
	}
	
	xircom_up(xp);
	leave();
	return 0;
}

static int xircom_close(struct net_device *dev)
{
	struct xircom_private *card;
	unsigned long flags;
	
	enter();
	card = dev->priv;
	netif_stop_queue(dev); /* we don't want to send new packets */

	
	spin_lock_irqsave(&card->lock,flags);
	
	disable_all_interrupts(card);
#if 0	
	/* We can enable this again once we send dummy packets on ifconfig ethX up */
	deactivate_receiver(card);
	deactivate_transmitter(card);
#endif	
	remove_descriptors(card);
	
	spin_unlock_irqrestore(&card->lock,flags);
	
	free_irq(dev->irq,dev);
	
	leave();
		
	return 0;
	
}



static struct net_device_stats *xircom_get_stats(struct net_device *dev)
{
        struct xircom_private *card = (struct xircom_private *)dev->priv;
        return &card->stats;
} 
                                                 



static void initialize_card(struct xircom_private *card)
{
	unsigned int val;
	unsigned long flags;
	enter();


	spin_lock_irqsave(&card->lock, flags);

	/* First: reset the card */
	val = inl(card->io_port + CSR0);
	val |= 0x01;		/* Software reset */
	outl(val, card->io_port + CSR0);

	udelay(100);		/* give the card some time to reset */

	val = inl(card->io_port + CSR0);
	val &= ~0x01;		/* disable Software reset */
	outl(val, card->io_port + CSR0);


	val = 0;		/* Value 0x00 is a safe and conservative value 
				   for the PCI configuration settings */
	outl(val, card->io_port + CSR0);


	disable_all_interrupts(card);
	deactivate_receiver(card);
	deactivate_transmitter(card);

	spin_unlock_irqrestore(&card->lock, flags);

	leave();
}

/*
trigger_transmit causes the card to check for frames to be transmitted.
This is accomplished by writing to the CSR1 port. The documentation
claims that the act of writing is sufficient and that the value is
ignored; I chose zero.
*/
static inline void trigger_transmit(struct xircom_private *card)
{
	enter();
	outl(0, card->io_port + CSR1);
	leave();
}

/*
trigger_receive causes the card to check for empty frames in the
descriptor list in which packets can be received.
This is accomplished by writing to the CSR2 port. The documentation
claims that the act of writing is sufficient and that the value is
ignored; I chose zero.
*/
static inline void trigger_receive(struct xircom_private *card)
{
	enter();
	outl(0, card->io_port + CSR2);
	leave();
}

/*
setup_descriptors initializes the send and receive buffers to be valid
descriptors and programs the addresses into the card.
*/
static void setup_descriptors(struct xircom_private *card)
{
	unsigned int val;
	u32 address;
	unsigned int i;
	enter();


	if (card->rx_buffer == NULL)
		BUG();
	if (card->tx_buffer == NULL)
		BUG();

	/* Receive descriptors */
	memset(card->rx_desc, 0, 128);	/* clear the descriptors */
	for (i=0;i<NUMDESCRIPTORS;i++ ) {

		/* Rx Descr0: It's empty, let the card own it, no errors -> 0x80000000 */
		card->rx_desc[i].status = DescOwnedCard;
		/* Rx Descr1: buffer 1 is 1536 bytes, buffer 2 is 0 bytes */
		card->rx_desc[i].control = MAX_PACKETSIZE;
		if (i==NUMDESCRIPTORS-1)
			card->rx_desc[i].control |= LastDescBit; /* bit 25 is "last descriptor" */

		/* Rx Descr2: address of the buffer
		   we store the buffer at the 2nd half of the page */
	
		address = card->rx_dma_handle;
		
		card->rx_desc[i].address1 = cpu_to_le32(address + bufferoffsets[i]);
		/* Rx Desc3: address of 2nd buffer -> 0 */
		card->rx_desc[i].address2 = 0;
	}
	
	wmb();
	/* Write the receive descriptor ring address to the card */
	address = card->rx_dma_handle;
	val = cpu_to_le32(address); 
	outl(val, card->io_port + CSR3);	/* Receive descr list address */


	/* transmit descriptors */
	memset(card->tx_desc, 0, 128);	/* clear the descriptors */
	
	for (i=0;i<NUMDESCRIPTORS;i++ ) {
		/* Tx Descr0: Empty, we own it, no errors -> 0x00000000 */
		card->tx_desc[i].status = DescOwnedDriver;
		/* Tx Descr1: buffer 1 is 1536 bytes, buffer 2 is 0 bytes */
		card->tx_desc[i].control = MAX_PACKETSIZE;
		if (i==NUMDESCRIPTORS-1)
			card->tx_desc[i].control |= LastDescBit; /* bit 25 is "last descriptor" */
		
		/* Tx Descr2: address of the buffer
		   we store the buffer at the 2nd half of the page */
		address = card->tx_dma_handle;
		card->tx_desc[i].address1 = cpu_to_le32(address + bufferoffsets[i]);
		/* Tx Desc3: address of 2nd buffer -> 0 */
		card->tx_desc[i].address2 = 0;
	}

	wmb();
	/* wite the transmit descriptor ring to the card */
	address = card->tx_dma_handle;
	val =cpu_to_le32(address);
	outl(val, card->io_port + CSR4);	/* xmit descr list address */

	leave();
}

/*
remove_descriptors informs the card the descriptors are no longer
valid by setting the address in the card to 0x00.
*/
static inline void remove_descriptors(struct xircom_private *card)
{
	unsigned int val;
	enter();

	val = 0;
	outl(val, card->io_port + CSR3);	/* Receive descriptor address */
	outl(val, card->io_port + CSR4);	/* Send descriptor address */

	leave();
}

/*
link_status_changed returns 1 if the card has indicated that
the link status has changed. The new link status has to be read from CSR12.

This function also clears the status-bit.
*/
static inline unsigned int link_status_changed(struct xircom_private *card)
{
	unsigned int val;
	enter();

	val = inl(card->io_port + CSR5);	/* Status register */

	if ((val & LinkStatusBit) == 0) {	/* no change */
		leave();
		return 0;
	}

	/* clear the event by writing a 1 to the bit in the
	   status register. */
	val = LinkStatusBit;
	outl(val, card->io_port + CSR5);

	leave();
	return 1;
}


/*
transmit_active returns 1 if the transmitter on the card is
in a non-stopped state.
*/
static inline int transmit_active(struct xircom_private *card)
{
	unsigned int val;
	enter();

	val = inl(card->io_port + CSR5);	/* Status register */

	if ((val & (7 << 20)) == 0) {	/* transmitter disabled */
		leave();
		return 0;
	}

	leave();
	return 1;
}

/*
receive_active returns 1 if the receiver on the card is
in a non-stopped state.
*/
static inline unsigned int receive_active(struct xircom_private *card)
{
	unsigned int val;
	enter();


	val = inl(card->io_port + CSR5);	/* Status register */

	if ((val & (7 << 17)) == 0) {	/* receiver disabled */
		leave();
		return 0;
	}

	leave();
	return 1;
}

/*
activate_receiver enables the receiver on the card.
Before being allowed to active the receiver, the receiver
must be completely de-activated. To achieve this,
this code actually disables the receiver first; then it waits for the 
receiver to become inactive, then it activates the receiver and then
it waits for the receiver to be active.

must be called with the lock held and interrupts disabled.
*/
static void activate_receiver(struct xircom_private *card)
{
	unsigned int val;
	int counter;
	enter();


	val = inl(card->io_port + CSR6);	/* Operation mode */
	
	/* If the "active" bit (1) is set and the receiver is already
	   active, no need to do the expensive thing */
	if ((val& RxActiveBit) && (receive_active(card)))
		return;
	
	
	val = val & ~RxActiveBit;		/* disable the receiver */
	outl(val, card->io_port + CSR6);

	counter = 10;
	while (counter > 0) {
		if (!receive_active(card))
			break;
		/* wait a while */
		udelay(50);
		counter--;
		if (counter <= 0)
			printk(KERN_ERR "xircom_cb: Receiver failed to deactivate\n");
	}

	/* enable the receiver */
	val = inl(card->io_port + CSR6);	/* Operation mode */
	val = val | RxActiveBit;		/* enable the receiver */
	outl(val, card->io_port + CSR6);

	/* now wait for the card to activate again */
	counter = 10;
	while (counter > 0) {
		if (receive_active(card))
			break;
		/* wait a while */
		udelay(50);
		counter--;
		if (counter <= 0)
			printk(KERN_ERR "xircom_cb: Receiver failed to re-activate\n");
	}

	leave();
}

/*
deactivate_receiver disables the receiver on the card.
To achieve this this code disables the receiver first; 
then it waits for the receiver to become inactive.

must be called with the lock held and interrupts disabled.
*/
static void deactivate_receiver(struct xircom_private *card)
{
	unsigned int val;
	int counter;
	enter();

	val = inl(card->io_port + CSR6);	/* Operation mode */
	val = val & ~RxActiveBit;		/* disable the receiver */
	outl(val, card->io_port + CSR6);

	counter = 10;
	while (counter > 0) {
		if (!receive_active(card))
			break;
		/* wait a while */
		udelay(50);
		counter--;
		if (counter <= 0)
			printk(KERN_ERR "xircom_cb: Receiver failed to deactivate\n");
	}


	leave();
}


/*
activate_transmitter enables the transmitter on the card.
Before being allowed to active the transmitter, the transmitter
must be completely de-activated. To achieve this,
this code actually disables the transmitter first; then it waits for the 
transmitter to become inactive, then it activates the transmitter and then
it waits for the transmitter to be active again.

must be called with the lock held and interrupts disabled.
*/
static void activate_transmitter(struct xircom_private *card)
{
	unsigned int val;
	int counter;
	enter();


	val = inl(card->io_port + CSR6);	/* Operation mode */

	/* If the "active" bit (13) is set and the receiver is already
	   active, no need to do the expensive thing */	 
	if ((val & TxActiveBit) && (transmit_active(card)))
		return;

	val = val & ~TxActiveBit;	/* disable the transmitter */
	outl(val, card->io_port + CSR6);

	counter = 10;
	while (counter > 0) {
		if (!transmit_active(card))
			break;
		/* wait a while */
		udelay(50);
		counter--;
		if (counter <= 0)
			printk(KERN_ERR "xircom_cb: Transmitter failed to deactivate\n");
	}

	/* enable the transmitter */
	val = inl(card->io_port + CSR6);	/* Operation mode */
	val = val | TxActiveBit;	/* enable the transmitter */
	outl(val, card->io_port + CSR6);

	/* now wait for the card to activate again */
	counter = 10;
	while (counter > 0) {
		if (transmit_active(card))
			break;
		/* wait a while */
		udelay(50);
		counter--;
		if (counter <= 0)
			printk(KERN_ERR "xircom_cb: Transmitter failed to re-activate\n");
	}

	leave();
}

/*
deactivate_transmitter disables the transmitter on the card.
To achieve this this code disables the transmitter first; 
then it waits for the transmitter to become inactive.

must be called with the lock held and interrupts disabled.
*/
static void deactivate_transmitter(struct xircom_private *card)
{
	unsigned int val;
	int counter;
	enter();

	val = inl(card->io_port + CSR6);	/* Operation mode */
	val = val & ~TxActiveBit;		/* disable the transmitter */
	outl(val, card->io_port + CSR6);

	counter = 20;
	while (counter > 0) {
		if (!transmit_active(card))
			break;
		/* wait a while */
		udelay(50);
		counter--;
		if (counter <= 0)
			printk(KERN_ERR "xircom_cb: Transmitter failed to deactivate\n");
	}


	leave();
}


/*
enable_transmit_interrupt enables the transmit interrupt

must be called with the lock held and interrupts disabled.
*/
static void enable_transmit_interrupt(struct xircom_private *card)
{
	unsigned int val;
	enter();

	val = inl(card->io_port + CSR7);	/* Interrupt enable register */
	val |= 1;				/* enable the transmit interrupt */
	outl(val, card->io_port + CSR7);

	leave();
}


/*
enable_receive_interrupt enables the receive interrupt

must be called with the lock held and interrupts disabled.
*/
static void enable_receive_interrupt(struct xircom_private *card)
{
	unsigned int val;
	enter();

	val = inl(card->io_port + CSR7);	/* Interrupt enable register */
	val = val | (1 << 6);			/* enable the receive interrupt */
	outl(val, card->io_port + CSR7);

	leave();
}

/*
enable_link_interrupt enables the link status change interrupt

must be called with the lock held and interrupts disabled.
*/
static void enable_link_interrupt(struct xircom_private *card)
{
	unsigned int val;
	enter();

	val = inl(card->io_port + CSR7);	/* Interrupt enable register */
	val = val | LinkStatusBit;			/* enable the link status chage interrupt */
	outl(val, card->io_port + CSR7);

	leave();
}



/*
disable_all_interrupts disables all interrupts

must be called with the lock held and interrupts disabled.
*/
static void disable_all_interrupts(struct xircom_private *card)
{
	unsigned int val;
	enter();
	
	val = 0;				/* disable all interrupts */
	outl(val, card->io_port + CSR7);

	leave();
}

/*
enable_common_interrupts enables several weird interrupts

must be called with the lock held and interrupts disabled.
*/
static void enable_common_interrupts(struct xircom_private *card)
{
	unsigned int val;
	enter();

	val = inl(card->io_port + CSR7);	/* Interrupt enable register */
	val |= (1<<16); /* Normal Interrupt Summary */
	val |= (1<<15); /* Abnormal Interrupt Summary */
	val |= (1<<13); /* Fatal bus error */
	val |= (1<<8);  /* Receive Process Stopped */
	val |= (1<<7);  /* Receive Buffer Unavailable */
	val |= (1<<5);  /* Transmit Underflow */
	val |= (1<<2);  /* Transmit Buffer Unavailable */
	val |= (1<<1);  /* Transmit Process Stopped */
	outl(val, card->io_port + CSR7);

	leave();
}

/*
enable_promisc starts promisc mode

must be called with the lock held and interrupts disabled.
*/
static inline void enable_promisc(struct xircom_private *card)
{
	unsigned int val;
	enter();

	val = inl(card->io_port + CSR6);	
	val = val | PromiscBit;	 /* Bit 6 */
	outl(val, card->io_port + CSR6);

	printk(KERN_INFO "xircom_cb: enabling promiscuous mode \n");
	leave();
}




/* 
link_status() checks the the links status and will return 0 for no link, 
10 for 10mbit link and 100 for.. guess what.

Must be called in locked state with interrupts disabled
*/
static inline unsigned int link_status(struct xircom_private *card)
{
	unsigned int val;
	enter();
	
	val = inb(card->io_port + CSR12);
	
	if (!(val&(1<<2)))  /* bit 2 is 0 for 10mbit link, 1 for not an 10mbit link */
		return 10;
	if (!(val&(1<<1)))  /* bit 1 is 0 for 100mbit link, 1 for not an 100mbit link */
		return 100;
		
	/* If we get here -> no link at all */	

	leave();
	return 0;
}



/* 

set_half_duplex() sets the card to half duplex mode. In order to do this,
set_half_duplex() has to deactivate the transmitter and receiver first. It
will re-enable the transmitter and receiver if those were active from the
beginning.

Update: the above is not enough. It doesn't touch the MII, in fact it ensures
the main chipset and the MII are never in sync if a full-duplex connection
is negotiated. The proper fix is to tell the MII to force a half-duplex
connection. -Ion

Must be called in locked state
*/
static void set_half_duplex(struct xircom_private *card)
{
	unsigned int val;
	int rx,tx,tmp;
	enter();
	
	rx=receive_active(card);
	tx=transmit_active(card);
	
	deactivate_transmitter(card);
	deactivate_receiver(card);
	
	val = inb(card->io_port + CSR6);
	val &= ~(1<<9);
	outb(val,card->io_port + CSR6);

	/* tell the MII not to advertise 10/100FDX */
	tmp = mdio_read(card, 0, 4);
	printk("xircom_cb: capabilities changed from %#x to %#x\n",
	       tmp, tmp & ~0x140);
	tmp &= ~0x140;
	mdio_write(card, 0, 4, tmp);
	/* restart autonegotiation */
	tmp = mdio_read(card, 0, 0);
	mdio_write(card, 0, 0, tmp | 0x1200);

	if (rx)
		activate_receiver(card);
	if (tx)
		activate_transmitter(card);	

	leave();
}


/*
  read_mac_address() reads the MAC address from the NIC and stores it in the "dev" structure.
 
  This function will take the spinlock itself and can, as a result, not be called with the lock helt.
 */
static void read_mac_address(struct xircom_private *card)
{
	unsigned char j, tuple, link, data_id, data_count;
	unsigned long flags;
	int i;

	enter();
		
	spin_lock_irqsave(&card->lock, flags);

	outl(1 << 12, card->io_port + CSR9);	/* enable boot rom access */
	for (i = 0x100; i < 0x1f7; i += link + 2) {
		outl(i, card->io_port + CSR10);
		tuple = inl(card->io_port + CSR9) & 0xff;
		outl(i + 1, card->io_port + CSR10);
		link = inl(card->io_port + CSR9) & 0xff;
		outl(i + 2, card->io_port + CSR10);
		data_id = inl(card->io_port + CSR9) & 0xff;
		outl(i + 3, card->io_port + CSR10);
		data_count = inl(card->io_port + CSR9) & 0xff;
		if ((tuple == 0x22) && (data_id == 0x04) && (data_count == 0x06)) {
			/* 
			 * This is it.  We have the data we want.
			 */
			for (j = 0; j < 6; j++) {
				outl(i + j + 4, card->io_port + CSR10);
				card->dev->dev_addr[j] = inl(card->io_port + CSR9) & 0xff;
			}
			break;
		} else if (link == 0) {
			break;
		}
	}
	spin_unlock_irqrestore(&card->lock, flags);
#ifdef DEBUG
	for (i = 0; i < 6; i++)
		printk("%c%2.2X", i ? ':' : ' ', card->dev->dev_addr[i]);
	printk("\n");
#endif
	leave();
}


/* MII transceiver control section.
   Read and write the MII registers using software-generated serial
   MDIO protocol.  See the MII specifications or DP83840A data sheet
   for details. */

/* The maximum data clock rate is 2.5 Mhz.  The minimum timing is usually
   met by back-to-back PCI I/O cycles, but we insert a delay to avoid
   "overclocking" issues or future 66Mhz PCI. */
#define mdio_delay() inl(mdio_addr)

/* Read and write the MII registers using software-generated serial
   MDIO protocol.  It is just different enough from the EEPROM protocol
   to not share code.  The maxium data clock rate is 2.5 Mhz. */
#define MDIO_SHIFT_CLK	0x10000
#define MDIO_DATA_WRITE0 0x00000
#define MDIO_DATA_WRITE1 0x20000
#define MDIO_ENB	0x00000		/* Ignore the 0x02000 databook setting. */
#define MDIO_ENB_IN	0x40000
#define MDIO_DATA_READ	0x80000

static int mdio_read(struct xircom_private *card, int phy_id, int location)
{
	int i;
	int read_cmd = (0xf6 << 10) | (phy_id << 5) | location;
	int retval = 0;
	long mdio_addr = card->io_port + CSR9;

	/* Establish sync by sending at least 32 logic ones. */
	for (i = 32; i >= 0; i--) {
		outl(MDIO_ENB | MDIO_DATA_WRITE1, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Shift the read command bits out. */
	for (i = 15; i >= 0; i--) {
		int dataval = (read_cmd & (1 << i)) ? MDIO_DATA_WRITE1 : 0;

		outl(MDIO_ENB | dataval, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | dataval | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Read the two transition, 16 data, and wire-idle bits. */
	for (i = 19; i > 0; i--) {
		outl(MDIO_ENB_IN, mdio_addr);
		mdio_delay();
		retval = (retval << 1) | ((inl(mdio_addr) & MDIO_DATA_READ) ? 1 : 0);
		outl(MDIO_ENB_IN | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	return (retval>>1) & 0xffff;
}

static void mdio_write(struct xircom_private *card, int phy_id, int location, int value)
{
	int i;
	int cmd = (0x5002 << 16) | (phy_id << 23) | (location<<18) | value;
	long mdio_addr = card->io_port + CSR9;

	/* Establish sync by sending 32 logic ones. */
	for (i = 32; i >= 0; i--) {
		outl(MDIO_ENB | MDIO_DATA_WRITE1, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Shift the command bits out. */
	for (i = 31; i >= 0; i--) {
		int dataval = (cmd & (1 << i)) ? MDIO_DATA_WRITE1 : 0;
		outl(MDIO_ENB | dataval, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | dataval | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Clear out extra bits. */
	for (i = 2; i > 0; i--) {
		outl(MDIO_ENB_IN, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB_IN | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
}


/*
 tranceiver_voodoo() enables the external UTP plug thingy.
 it's called voodoo as I stole this code and cannot cross-reference
 it with the specification.
 */
static void tranceiver_voodoo(struct xircom_private *card)
{
	unsigned long flags;
	u32 tmp32;

	enter();

	/* disable all powermanagement */
	pci_read_config_dword(card->pdev, PCI_POWERMGMT,&tmp32);
	tmp32 &= ~PowerMgmtBits; 
	pci_write_config_dword(card->pdev, PCI_POWERMGMT, tmp32);

	setup_descriptors(card);

	spin_lock_irqsave(&card->lock, flags);

	outl(0x0008, card->io_port + CSR15);
        udelay(25);  
        outl(0xa8050000, card->io_port + CSR15);
        udelay(25);
        outl(0xa00f0000, card->io_port + CSR15);
        udelay(25);
        
        spin_unlock_irqrestore(&card->lock, flags);

	netif_start_queue(card->dev);
	leave();
}


static void xircom_up(struct xircom_private *card)
{
	unsigned long flags;
	int i;
	u32 tmp32;

	enter();

	/* disable all powermanagement */
	pci_read_config_dword(card->pdev, PCI_POWERMGMT,&tmp32);
	tmp32 &= ~PowerMgmtBits; 
	pci_write_config_dword(card->pdev, PCI_POWERMGMT, tmp32);

	setup_descriptors(card);

	spin_lock_irqsave(&card->lock, flags);

	
	enable_link_interrupt(card);
	enable_transmit_interrupt(card);
	enable_receive_interrupt(card);
	enable_common_interrupts(card);
	enable_promisc(card);
	
	/* The card can have received packets already, read them away now */
	for (i=0;i<NUMDESCRIPTORS;i++) 
		investigate_rx_descriptor(card->dev,card,i,bufferoffsets[i]);


	set_half_duplex(card);
	spin_unlock_irqrestore(&card->lock, flags);
	trigger_receive(card);
	trigger_transmit(card);
	netif_start_queue(card->dev);
	leave();
}

static void investigate_rx_descriptor(struct net_device *dev,struct xircom_private *card, int descnr, unsigned int bufferoffset)
{
		int status;		
		
		enter();
		status = card->rx_desc[descnr].status;
		
		if ((status > 0)) {	/* packet received */
		
			/* TODO: discard error packets */
			
			short pkt_len = ((status >> 16) & 0x7ff) - 4;	/* minus 4, we don't want the CRC */
			struct sk_buff *skb;

			if (pkt_len > 1518) {
				printk(KERN_ERR "xircom_cb: Packet length %i is bogus \n",pkt_len);
				pkt_len = 1518;
			}

			skb = dev_alloc_skb(pkt_len + 2);
			if (skb == NULL) {
				card->stats.rx_dropped++;
				goto out;
			}
			skb->dev = dev;
			skb_reserve(skb, 2);
			eth_copy_and_sum(skb, &card->rx_buffer[bufferoffset], pkt_len, 0);
			skb_put(skb, pkt_len);
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			dev->last_rx = jiffies;
			card->stats.rx_packets++;
			card->stats.rx_bytes += pkt_len;
			
		      out:
			/* give the buffer back to the card */
			card->rx_desc[descnr].status = DescOwnedCard;
			trigger_receive(card);
		}

		leave();

}


/* Returns 1 if the descriptor is free or became free */
static unsigned int investigate_tx_descriptor(struct net_device *dev, struct xircom_private *card, unsigned int descnr, unsigned int bufferoffset)
{
		int status,retval = 0;
		enter();
		
		status = card->tx_desc[descnr].status;
		
		if (status == DescOwnedDriver)
			return 1;
#if 0		
		if (status & 0x8000) {	/* Major error */
			printk(KERN_ERR "Major transmit error status %x \n", status);
			card->tx_desc[descnr].status = 0;
			netif_wake_queue (dev);
		}
#endif
		if (status > 0) {	/* bit 31 is 0 when done */
			card->stats.tx_packets++;
			if (card->tx_skb[descnr]!=NULL) {
				card->stats.tx_bytes += card->tx_skb[descnr]->len;
				dev_kfree_skb_irq(card->tx_skb[descnr]);
			}
			card->tx_skb[descnr] = NULL;
			/* Bit 8 in the status field is 1 if there was a collision */
			if (status & CollisionBit)
				card->stats.collisions++;
			card->tx_desc[descnr].status = DescOwnedDriver; /* descriptor is free again */
			retval = 1;
		}

		leave();
		return retval;		
}


static int __init xircom_init(void)
{
	pci_register_driver(&xircom_ops);
	return 0;
}

static void __exit xircom_exit(void)
{
	pci_unregister_driver(&xircom_ops);
} 

module_init(xircom_init) 
module_exit(xircom_exit)
