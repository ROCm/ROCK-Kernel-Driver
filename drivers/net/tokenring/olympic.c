/*
 *   olympic.c (c) 1999 Peter De Schrijver All Rights Reserved
 *		   1999 Mike Phillips (phillim@amtrak.com)
 *
 *  Linux driver for IBM PCI tokenring cards based on the Pit/Pit-Phy/Olympic
 *  chipset. 
 *
 *  Base Driver Skeleton:
 *      Written 1993-94 by Donald Becker.
 *
 *      Copyright 1993 United States Government as represented by the
 *      Director, National Security Agency.
 *
 *  Thanks to Erik De Cock, Adrian Bridgett and Frank Fiene for their 
 *  assistance and perserverance with the testing of this driver.
 *
 *  This software may be used and distributed according to the terms
 *  of the GNU Public License, incorporated herein by reference.
 * 
 *  4/27/99 - Alpha Release 0.1.0
 *            First release to the public
 *
 *  6/8/99  - Official Release 0.2.0   
 *            Merged into the kernel code 
 *  8/18/99 - Updated driver for 2.3.13 kernel to use new pci
 *	      resource. Driver also reports the card name returned by
 *            the pci resource.
 *  1/11/00 - Added spinlocks for smp
 *  2/23/00 - Updated to dev_kfree_irq 
 *  3/10/00 - Fixed FDX enable which triggered other bugs also 
 *            squashed.
 *  5/20/00 - Changes to handle Olympic on LinuxPPC. Endian changes.
 *            The odd thing about the changes is that the fix for
 *            endian issues with the big-endian data in the arb, asb...
 *            was to always swab() the bytes, no matter what CPU.
 *            That's because the read[wl]() functions always swap the
 *            bytes on the way in on PPC.
 *            Fixing the hardware descriptors was another matter,
 *            because they weren't going through read[wl](), there all
 *            the results had to be in memory in le32 values. kdaaker
 *
 *
 *  To Do:
 *
 *  If Problems do Occur
 *  Most problems can be rectified by either closing and opening the interface
 *  (ifconfig down and up) or rmmod and insmod'ing the driver (a bit difficult
 *  if compiled into the kernel).
 */

/* Change OLYMPIC_DEBUG to 1 to get verbose, and I mean really verbose, messages */

#define OLYMPIC_DEBUG 0

/* Change OLYMPIC_NETWORK_MONITOR to receive mac frames through the arb channel.
 * Will also create a /proc/net/olympic_tr entry if proc_fs is compiled into the
 * kernel.
 * Intended to be used to create a ring-error reporting network module 
 * i.e. it will give you the source address of beaconers on the ring 
 */

#define OLYMPIC_NETWORK_MONITOR 0

#include <linux/config.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/in.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/trdevice.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <net/checksum.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/bitops.h>

#include "olympic.h"

/* I've got to put some intelligence into the version number so that Peter and I know
 * which version of the code somebody has got. 
 * Version Number = a.b.c.d  where a.b.c is the level of code and d is the latest author.
 * So 0.0.1.pds = Peter, 0.0.1.mlp = Mike
 * 
 * Official releases will only have an a.b.c version number format. 
 */

static char *version = 
"Olympic.c v0.5.0 3/10/00 - Peter De Schrijver & Mike Phillips" ; 

static struct pci_device_id olympic_pci_tbl[] __initdata = {
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_TR_WAKE, PCI_ANY_ID, PCI_ANY_ID, },
	{ }			/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, olympic_pci_tbl);


static char *open_maj_error[]  = {"No error", "Lobe Media Test", "Physical Insertion",
				   "Address Verification", "Neighbor Notification (Ring Poll)",
				   "Request Parameters","FDX Registration Request",
				   "FDX Duplicate Address Check", "Station registration Query Wait",
				   "Unknown stage"};

static char *open_min_error[] = {"No error", "Function Failure", "Signal Lost", "Wire Fault",
				   "Ring Speed Mismatch", "Timeout","Ring Failure","Ring Beaconing",
				   "Duplicate Node Address","Request Parameters","Remove Received",
				   "Reserved", "Reserved", "No Monitor Detected for RPL", 
				   "Monitor Contention failer for RPL", "FDX Protocol Error"};

/* Module paramters */

/* Ring Speed 0,4,16,100 
 * 0 = Autosense         
 * 4,16 = Selected speed only, no autosense
 * This allows the card to be the first on the ring
 * and become the active monitor.
 * 100 = Nothing at present, 100mbps is autodetected
 * if FDX is turned on. May be implemented in the future to 
 * fail if 100mpbs is not detected.
 *
 * WARNING: Some hubs will allow you to insert
 * at the wrong speed
 */

static int ringspeed[OLYMPIC_MAX_ADAPTERS] = {0,} ;

MODULE_PARM(ringspeed, "1-" __MODULE_STRING(OLYMPIC_MAX_ADAPTERS) "i");

/* Packet buffer size */

static int pkt_buf_sz[OLYMPIC_MAX_ADAPTERS] = {0,} ;
 
MODULE_PARM(pkt_buf_sz, "1-" __MODULE_STRING(OLYMPIC_MAX_ADAPTERS) "i") ; 

/* Message Level */

static int message_level[OLYMPIC_MAX_ADAPTERS] = {0,} ; 

MODULE_PARM(message_level, "1-" __MODULE_STRING(OLYMPIC_MAX_ADAPTERS) "i") ; 

static int olympic_scan(struct net_device *dev);
static int olympic_init(struct net_device *dev);
static int olympic_open(struct net_device *dev);
static int olympic_xmit(struct sk_buff *skb, struct net_device *dev);
static int olympic_close(struct net_device *dev);
static void olympic_set_rx_mode(struct net_device *dev);
static void olympic_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static struct net_device_stats * olympic_get_stats(struct net_device *dev);
static int olympic_set_mac_address(struct net_device *dev, void *addr) ; 
static void olympic_arb_cmd(struct net_device *dev);
static int olympic_change_mtu(struct net_device *dev, int mtu);
static void olympic_srb_bh(struct net_device *dev) ; 
static void olympic_asb_bh(struct net_device *dev) ; 
#if OLYMPIC_NETWORK_MONITOR
#ifdef CONFIG_PROC_FS
static int sprintf_info(char *buffer, struct net_device *dev) ; 
#endif
#endif

int __init olympic_probe(struct net_device *dev)
{
	int cards_found;

	cards_found=olympic_scan(dev);
	return cards_found ? 0 : -ENODEV;
}

static int __init olympic_scan(struct net_device *dev)
{
	struct pci_dev *pci_device = NULL ;
	struct olympic_private *olympic_priv;
	int card_no = 0 ;
	if (pci_present()) {

		while((pci_device=pci_find_device(PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_TR_WAKE, pci_device))) {
			__u16 pci_command ; 

			if (pci_enable_device(pci_device))
				continue;

			/* These lines are needed by the PowerPC, it appears
that these flags
			 * are not being set properly for the PPC, this may
well be fixed with
			 * the new PCI code */			
			pci_read_config_word(pci_device, PCI_COMMAND, &pci_command);
			pci_command |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY;
			pci_write_config_word(pci_device, PCI_COMMAND,pci_command);
			pci_set_master(pci_device);

			/* Check to see if io has been allocated, if so, we've already done this card,
			   so continue on the card discovery loop  */

			if (check_region(pci_resource_start(pci_device, 0), OLYMPIC_IO_SPACE)) {
				card_no++ ; 
				continue ; 
			}

			olympic_priv=kmalloc(sizeof (struct olympic_private), GFP_KERNEL);
			memset(olympic_priv, 0, sizeof(struct olympic_private));
			init_waitqueue_head(&olympic_priv->srb_wait);
			init_waitqueue_head(&olympic_priv->trb_wait);
#ifndef MODULE
			dev=init_trdev(dev, 0);
#endif
			dev->priv=(void *)olympic_priv;
#if OLYMPIC_DEBUG  
			printk("pci_device: %p, dev:%p, dev->priv: %p\n", pci_device, dev, dev->priv);
#endif
			dev->irq=pci_device->irq;
			dev->base_addr=pci_resource_start(pci_device, 0);
			dev->init=&olympic_init;
			olympic_priv->olympic_card_name = (char *)pci_device->resource[0].name ; 
			olympic_priv->olympic_mmio = 
				ioremap(pci_resource_start(pci_device,1),256);
			olympic_priv->olympic_lap = 
				ioremap(pci_resource_start(pci_device,2),2048);
			
			if ((pkt_buf_sz[card_no] < 100) || (pkt_buf_sz[card_no] > 18000) )
				olympic_priv->pkt_buf_sz = PKT_BUF_SZ ; 
			else
				olympic_priv->pkt_buf_sz = pkt_buf_sz[card_no] ; 

			olympic_priv->olympic_ring_speed = ringspeed[card_no] ; 
			olympic_priv->olympic_message_level = message_level[card_no] ; 
	
			if(olympic_init(dev)==-1) {
				unregister_netdevice(dev);
				kfree(dev->priv);
				return 0;
			}				

			dev->open=&olympic_open;
			dev->hard_start_xmit=&olympic_xmit;
			dev->change_mtu=&olympic_change_mtu;

			dev->stop=&olympic_close;
			dev->do_ioctl=NULL;
			dev->set_multicast_list=&olympic_set_rx_mode;
			dev->get_stats=&olympic_get_stats ;
			dev->set_mac_address=&olympic_set_mac_address ;  
			return 1; 
		}
	}
	return  0 ;
}


static int __init olympic_init(struct net_device *dev)
{
    	struct olympic_private *olympic_priv;
	__u8 *olympic_mmio, *init_srb,*adapter_addr;
	unsigned long t; 
	unsigned int uaa_addr;

    	olympic_priv=(struct olympic_private *)dev->priv;
	olympic_mmio=olympic_priv->olympic_mmio;

	printk("%s \n", version);
	printk("%s: %s. I/O at %hx, MMIO at %p, LAP at %p, using irq %d\n",dev->name, olympic_priv->olympic_card_name, (unsigned int) dev->base_addr,olympic_priv->olympic_mmio, olympic_priv->olympic_lap, dev->irq);

	request_region(dev->base_addr, OLYMPIC_IO_SPACE, "olympic");
	writel(readl(olympic_mmio+BCTL) | BCTL_SOFTRESET,olympic_mmio+BCTL);
	t=jiffies;
	while((readl(olympic_priv->olympic_mmio+BCTL)) & BCTL_SOFTRESET) {
		schedule();		
		if(jiffies-t > 40*HZ) {
			printk(KERN_ERR "IBM PCI tokenring card not responding.\n");
			release_region(dev->base_addr, OLYMPIC_IO_SPACE) ; 
			return -1;
		}
	}

	spin_lock_init(&olympic_priv->olympic_lock) ; 

#if OLYMPIC_DEBUG
	printk("BCTL: %x\n",readl(olympic_mmio+BCTL));
	printk("GPR: %x\n",readw(olympic_mmio+GPR));
	printk("SISRMASK: %x\n",readl(olympic_mmio+SISR_MASK));
#endif
	/* Aaaahhh, You have got to be real careful setting GPR, the card
	   holds the previous values from flash memory, including autosense 
           and ring speed */

	writel(readl(olympic_mmio+BCTL)|BCTL_MIMREB,olympic_mmio+BCTL);
	
	if (olympic_priv->olympic_ring_speed  == 0) { /* Autosense */
		writel(readl(olympic_mmio+GPR)|GPR_AUTOSENSE,olympic_mmio+GPR);
		if (olympic_priv->olympic_message_level) 
			printk(KERN_INFO "%s: Ringspeed autosense mode on\n",dev->name);
	} else if (olympic_priv->olympic_ring_speed == 16) {
		if (olympic_priv->olympic_message_level) 
			printk(KERN_INFO "%s: Trying to open at 16 Mbps as requested\n", dev->name);
		writel(GPR_16MBPS, olympic_mmio+GPR);
	} else if (olympic_priv->olympic_ring_speed == 4) {
		if (olympic_priv->olympic_message_level) 
			printk(KERN_INFO "%s: Trying to open at 4 Mbps as requested\n", dev->name) ; 
		writel(0, olympic_mmio+GPR);
	} 
	
	writel(readl(olympic_mmio+GPR)|GPR_NEPTUNE_BF,olympic_mmio+GPR);

#if OLYMPIC_DEBUG
	printk("GPR = %x\n",readw(olympic_mmio + GPR) ) ; 
#endif
	/* start solo init */
	writel((1<<15),olympic_mmio+SISR_MASK_SUM);

	t=jiffies;
	while(!((readl(olympic_mmio+SISR_RR)) & SISR_SRB_REPLY)) {
		schedule();		
		if(jiffies-t > 40*HZ) {
			printk(KERN_ERR "IBM PCI tokenring card not responding.\n");
			release_region(dev->base_addr, OLYMPIC_IO_SPACE); 
			return -1;
		}
	}
	
	writel(readl(olympic_mmio+LAPWWO),olympic_mmio+LAPA);

#if OLYMPIC_DEBUG
	printk("LAPWWO: %x, LAPA: %x\n",readl(olympic_mmio+LAPWWO), readl(olympic_mmio+LAPA));
#endif

	init_srb=olympic_priv->olympic_lap + ((readl(olympic_mmio+LAPWWO)) & (~0xf800));

#if OLYMPIC_DEBUG		
{
	int i;
	printk("init_srb(%p): ",init_srb);
	for(i=0;i<20;i++)
		printk("%x ",readb(init_srb+i));
	printk("\n");
}
#endif	
	if(readw(init_srb+6)) {
		printk(KERN_INFO "tokenring card intialization failed. errorcode : %x\n",readw(init_srb+6));
		release_region(dev->base_addr, OLYMPIC_IO_SPACE);
		return -1;
	}

	if (olympic_priv->olympic_message_level) {
		if ( readb(init_srb +2) & 0x40) { 
			printk(KERN_INFO "Olympic: Adapter is FDX capable.\n") ;
		} else { 
			printk(KERN_INFO "Olympic: Adapter cannot do FDX.\n");
		}
	}
  
	uaa_addr=swab16(readw(init_srb+8));

#if OLYMPIC_DEBUG
	printk("UAA resides at %x\n",uaa_addr);
#endif

	writel(uaa_addr,olympic_mmio+LAPA);
	adapter_addr=olympic_priv->olympic_lap + (uaa_addr & (~0xf800));

#if OLYMPIC_DEBUG
	printk("adapter address: %02x:%02x:%02x:%02x:%02x:%02x\n",
			readb(adapter_addr), readb(adapter_addr+1),readb(adapter_addr+2),
			readb(adapter_addr+3),readb(adapter_addr+4),readb(adapter_addr+5));
#endif

	memcpy_fromio(&dev->dev_addr[0], adapter_addr,6);

	olympic_priv->olympic_addr_table_addr = swab16(readw(init_srb + 12)); 
	olympic_priv->olympic_parms_addr = swab16(readw(init_srb + 14)); 

	return 0;

}

static int olympic_open(struct net_device *dev) 
{
	struct olympic_private *olympic_priv=(struct olympic_private *)dev->priv;
	__u8 *olympic_mmio=olympic_priv->olympic_mmio,*init_srb;
	unsigned long flags;
	char open_error[255] ; 
	int i, open_finished = 1 ;

#if OLYMPIC_NETWORK_MONITOR
	__u8 *oat ; 
	__u8 *opt ; 
#endif

	if(request_irq(dev->irq, &olympic_interrupt, SA_SHIRQ , "olympic", dev)) {
		return -EAGAIN;
	}

#if OLYMPIC_DEBUG
	printk("BMCTL: %x\n",readl(olympic_mmio+BMCTL_SUM));
	printk("pending ints: %x\n",readl(olympic_mmio+SISR_RR));
#endif

	writel(SISR_MI,olympic_mmio+SISR_MASK_SUM);

	writel(SISR_MI | SISR_SRB_REPLY, olympic_mmio+SISR_MASK); /* more ints later, doesn't stop arb cmd interrupt */

	writel(LISR_LIE,olympic_mmio+LISR); /* more ints later */

	/* adapter is closed, so SRB is pointed to by LAPWWO */

	writel(readl(olympic_mmio+LAPWWO),olympic_mmio+LAPA);
	init_srb=olympic_priv->olympic_lap + ((readl(olympic_mmio+LAPWWO)) & (~0xf800));
	
#if OLYMPIC_DEBUG
	printk("LAPWWO: %x, LAPA: %x\n",readl(olympic_mmio+LAPWWO), readl(olympic_mmio+LAPA));
	printk("SISR Mask = %04x\n", readl(olympic_mmio+SISR_MASK));
	printk("Before the open command \n");
#endif	
	do {
		int i;

		save_flags(flags);
		cli();
		for(i=0;i<SRB_COMMAND_SIZE;i+=4)
			writel(0,init_srb+i);
		if(SRB_COMMAND_SIZE & 2)
			writew(0,init_srb+(SRB_COMMAND_SIZE & ~3));
		if(SRB_COMMAND_SIZE & 1)
			writeb(0,init_srb+(SRB_COMMAND_SIZE & ~1));

		writeb(SRB_OPEN_ADAPTER,init_srb) ; 	/* open */
		writeb(OLYMPIC_CLEAR_RET_CODE,init_srb+2);

		/* If Network Monitor, instruct card to copy MAC frames through the ARB */

#if OLYMPIC_NETWORK_MONITOR
		writew(swab16(OPEN_ADAPTER_ENABLE_FDX | OPEN_ADAPTER_PASS_ADC_MAC | OPEN_ADAPTER_PASS_ATT_MAC | OPEN_ADAPTER_PASS_BEACON), init_srb+8);
#else
		writew(swab16(OPEN_ADAPTER_ENABLE_FDX), init_srb+8);
#endif		

		if (olympic_priv->olympic_laa[0]) {
			writeb(olympic_priv->olympic_laa[0],init_srb+12);
			writeb(olympic_priv->olympic_laa[1],init_srb+13);
			writeb(olympic_priv->olympic_laa[2],init_srb+14);
			writeb(olympic_priv->olympic_laa[3],init_srb+15);
			writeb(olympic_priv->olympic_laa[4],init_srb+16);
			writeb(olympic_priv->olympic_laa[5],init_srb+17);
			memcpy(dev->dev_addr,olympic_priv->olympic_laa,dev->addr_len) ;  
		} 	
		writeb(1,init_srb+30);
	
		olympic_priv->srb_queued=1;

		writel(LISR_SRB_CMD,olympic_mmio+LISR_SUM);

 		while(olympic_priv->srb_queued) {        
        		interruptible_sleep_on_timeout(&olympic_priv->srb_wait, 60*HZ);
        		if(signal_pending(current))	{            
				printk(KERN_WARNING "%s: SRB timed out.\n",
                			dev->name);
            			printk(KERN_WARNING "SISR=%x MISR=%x\n",
                			readl(olympic_mmio+SISR),
                			readl(olympic_mmio+LISR));
            			olympic_priv->srb_queued=0;
            			break;
        		}
    		}
		restore_flags(flags);
#if OLYMPIC_DEBUG
		printk("init_srb(%p): ",init_srb);
		for(i=0;i<20;i++)
			printk("%02x ",readb(init_srb+i));
		printk("\n");
#endif
		
		/* If we get the same return response as we set, the interrupt wasn't raised and the open
                 * timed out.
		 */

		if(readb(init_srb+2)== OLYMPIC_CLEAR_RET_CODE) {
			printk(KERN_WARNING "%s: Adapter Open time out or error.\n", dev->name) ; 
			return -EIO ; 
		}	

		if(readb(init_srb+2)!=0) {
			if (readb(init_srb+2) == 0x07) {  
				if (!olympic_priv->olympic_ring_speed && open_finished) { /* Autosense , first time around */
					printk(KERN_WARNING "%s: Retrying at different ring speed \n", dev->name); 
					open_finished = 0 ;  
				} else {

					strcpy(open_error, open_maj_error[(readb(init_srb+7) & 0xf0) >> 4]) ; 
					strcat(open_error," - ") ; 
					strcat(open_error, open_min_error[(readb(init_srb+7) & 0x0f)]) ;

					if (!olympic_priv->olympic_ring_speed && ((readb(init_srb+7) & 0x0f) == 0x0d)) { 
						printk(KERN_WARNING "%s: Tried to autosense ring speed with no monitors present\n",dev->name);
						printk(KERN_WARNING "%s: Please try again with a specified ring speed \n",dev->name);
						free_irq(dev->irq, dev);
						return -EIO ;
					}

					printk(KERN_WARNING "%s: %s\n",dev->name,open_error);
					free_irq(dev->irq,dev) ; 
					return -EIO ; 
 
				}	/* if autosense && open_finished */
			} else {  
				printk(KERN_WARNING "%s: Bad OPEN response: %x\n", dev->name,init_srb[2]);
				free_irq(dev->irq, dev);
				return -EIO;
			} 
		} else 
			open_finished = 1 ; 
	} while (!(open_finished)) ; /* Will only loop if ring speed mismatch re-open attempted && autosense is on */	

	if (readb(init_srb+18) & (1<<3)) 
		if (olympic_priv->olympic_message_level) 
			printk(KERN_INFO "%s: Opened in FDX Mode\n",dev->name);

	if (readb(init_srb+18) & (1<<1))
		olympic_priv->olympic_ring_speed = 100 ; 
	else if (readb(init_srb+18) & 1)
		olympic_priv->olympic_ring_speed = 16 ; 
	else
		olympic_priv->olympic_ring_speed = 4 ; 

	if (olympic_priv->olympic_message_level) 
		printk(KERN_INFO "%s: Opened in %d Mbps mode\n",dev->name, olympic_priv->olympic_ring_speed);

	olympic_priv->asb = swab16(readw(init_srb+8));
	olympic_priv->srb = swab16(readw(init_srb+10));
	olympic_priv->arb = swab16(readw(init_srb+12));
	olympic_priv->trb = swab16(readw(init_srb+16));

	olympic_priv->olympic_receive_options = 0x01 ; 
	olympic_priv->olympic_copy_all_options = 0 ; 
	
	/* setup rx ring */
	
	writel((3<<16),olympic_mmio+BMCTL_RWM); /* Ensure end of frame generated interrupts */ 

	writel(BMCTL_RX_DIS|3,olympic_mmio+BMCTL_RWM); /* Yes, this the enables RX channel */

	for(i=0;i<OLYMPIC_RX_RING_SIZE;i++) {

		struct sk_buff *skb;
		
		skb=dev_alloc_skb(olympic_priv->pkt_buf_sz);
		if(skb == NULL)
			break;

		skb->dev = dev;

		olympic_priv->olympic_rx_ring[i].buffer = cpu_to_le32(virt_to_bus(skb->data));
		olympic_priv->olympic_rx_ring[i].res_length = cpu_to_le32(olympic_priv->pkt_buf_sz); 
		olympic_priv->rx_ring_skb[i]=skb;
	}

	if (i==0) {
		printk(KERN_WARNING "%s: Not enough memory to allocate rx buffers. Adapter disabled\n",dev->name);
		free_irq(dev->irq, dev);
		return -EIO;
	}

	writel(virt_to_bus(&olympic_priv->olympic_rx_ring[0]), olympic_mmio+RXDESCQ);
	writel(virt_to_bus(&olympic_priv->olympic_rx_ring[0]), olympic_mmio+RXCDA);
	writew(i, olympic_mmio+RXDESCQCNT);
		
	writel(virt_to_bus(&olympic_priv->olympic_rx_status_ring[0]), olympic_mmio+RXSTATQ);
	writel(virt_to_bus(&olympic_priv->olympic_rx_status_ring[0]), olympic_mmio+RXCSA);
	
 	olympic_priv->rx_ring_last_received = OLYMPIC_RX_RING_SIZE - 1;	/* last processed rx status */
	olympic_priv->rx_status_last_received = OLYMPIC_RX_RING_SIZE - 1;  

	writew(i, olympic_mmio+RXSTATQCNT);

#if OLYMPIC_DEBUG 
	printk("# of rx buffers: %d, RXENQ: %x\n",i, readw(olympic_mmio+RXENQ));
	printk("RXCSA: %x, rx_status_ring[0]: %p\n",bus_to_virt(readl(olympic_mmio+RXCSA)),&olympic_priv->olympic_rx_status_ring[0]);
	printk(" stat_ring[1]: %p, stat_ring[2]: %p, stat_ring[3]: %p\n", &(olympic_priv->olympic_rx_status_ring[1]), &(olympic_priv->olympic_rx_status_ring[2]), &(olympic_priv->olympic_rx_status_ring[3]) );
	printk(" stat_ring[4]: %p, stat_ring[5]: %p, stat_ring[6]: %p\n", &(olympic_priv->olympic_rx_status_ring[4]), &(olympic_priv->olympic_rx_status_ring[5]), &(olympic_priv->olympic_rx_status_ring[6]) );
	printk(" stat_ring[7]: %p\n", &(olympic_priv->olympic_rx_status_ring[7])  );

	printk("RXCDA: %x, rx_ring[0]: %p\n",bus_to_virt(readl(olympic_mmio+RXCDA)),&olympic_priv->olympic_rx_ring[0]);
#endif

	writew((((readw(olympic_mmio+RXENQ)) & 0x8000) ^ 0x8000) | i,olympic_mmio+RXENQ);

#if OLYMPIC_DEBUG 
	printk("# of rx buffers: %d, RXENQ: %x\n",i, readw(olympic_mmio+RXENQ));
	printk("RXCSA: %x, rx_ring[0]: %p\n",bus_to_virt(readl(olympic_mmio+RXCSA)),&olympic_priv->olympic_rx_status_ring[0]);
	printk("RXCDA: %x, rx_ring[0]: %p\n",bus_to_virt(readl(olympic_mmio+RXCDA)),&olympic_priv->olympic_rx_ring[0]);
#endif 

	writel(SISR_RX_STATUS | SISR_RX_NOBUF,olympic_mmio+SISR_MASK_SUM);

	/* setup tx ring */

	writel(BMCTL_TX1_DIS,olympic_mmio+BMCTL_RWM); /* Yes, this enables TX channel 1 */
	for(i=0;i<OLYMPIC_TX_RING_SIZE;i++) 
		olympic_priv->olympic_tx_ring[i].buffer=0xdeadbeef;

	olympic_priv->free_tx_ring_entries=OLYMPIC_TX_RING_SIZE;
	writel(virt_to_bus(&olympic_priv->olympic_tx_ring[0]), olympic_mmio+TXDESCQ_1);
	writel(virt_to_bus(&olympic_priv->olympic_tx_ring[0]), olympic_mmio+TXCDA_1);
	writew(OLYMPIC_TX_RING_SIZE, olympic_mmio+TXDESCQCNT_1);
	
	writel(virt_to_bus(&olympic_priv->olympic_tx_status_ring[0]),olympic_mmio+TXSTATQ_1);
	writel(virt_to_bus(&olympic_priv->olympic_tx_status_ring[0]),olympic_mmio+TXCSA_1);
	writew(OLYMPIC_TX_RING_SIZE,olympic_mmio+TXSTATQCNT_1);
		
	olympic_priv->tx_ring_free=0; /* next entry in tx ring to use */
	olympic_priv->tx_ring_last_status=OLYMPIC_TX_RING_SIZE-1; /* last processed tx status */

	writel(SISR_TX1_EOF | SISR_ADAPTER_CHECK | SISR_ARB_CMD | SISR_TRB_REPLY | SISR_ASB_FREE,olympic_mmio+SISR_MASK_SUM);

#if OLYMPIC_DEBUG 
	printk("BMCTL: %x\n",readl(olympic_mmio+BMCTL_SUM));
	printk("SISR MASK: %x\n",readl(olympic_mmio+SISR_MASK));
#endif

#if OLYMPIC_NETWORK_MONITOR
	oat = (__u8 *)(olympic_priv->olympic_lap + olympic_priv->olympic_addr_table_addr) ; 
	opt = (__u8 *)(olympic_priv->olympic_lap + olympic_priv->olympic_parms_addr) ; 

	printk("%s: Node Address: %02x:%02x:%02x:%02x:%02x:%02x\n",dev->name, 
		    readb(oat+offsetof(struct olympic_adapter_addr_table,node_addr)), 
		    readb(oat+offsetof(struct olympic_adapter_addr_table,node_addr)+1),
		    readb(oat+offsetof(struct olympic_adapter_addr_table,node_addr)+2),
		    readb(oat+offsetof(struct olympic_adapter_addr_table,node_addr)+3),
		    readb(oat+offsetof(struct olympic_adapter_addr_table,node_addr)+4),
		    readb(oat+offsetof(struct olympic_adapter_addr_table,node_addr)+5));
	printk("%s: Functional Address: %02x:%02x:%02x:%02x\n",dev->name, 
		    readb(oat+offsetof(struct olympic_adapter_addr_table,func_addr)), 
		    readb(oat+offsetof(struct olympic_adapter_addr_table,func_addr)+1),
		    readb(oat+offsetof(struct olympic_adapter_addr_table,func_addr)+2),
		    readb(oat+offsetof(struct olympic_adapter_addr_table,func_addr)+3));

	printk("%s: NAUN Address: %02x:%02x:%02x:%02x:%02x:%02x\n",dev->name, 
			readb(opt+offsetof(struct olympic_parameters_table, up_node_addr)),
			readb(opt+offsetof(struct olympic_parameters_table, up_node_addr)+1),
			readb(opt+offsetof(struct olympic_parameters_table, up_node_addr)+2),
			readb(opt+offsetof(struct olympic_parameters_table, up_node_addr)+3),
			readb(opt+offsetof(struct olympic_parameters_table, up_node_addr)+4),
			readb(opt+offsetof(struct olympic_parameters_table, up_node_addr)+5));


#endif 	
	
	netif_start_queue(dev);
	MOD_INC_USE_COUNT ;
	return 0;
	
}	

/*
 *	When we enter the rx routine we do not know how many frames have been 
 *	queued on the rx channel.  Therefore we start at the next rx status
 *	position and travel around the receive ring until we have completed
 *	all the frames.
 *
 *	This means that we may process the frame before we receive the end
 *	of frame interrupt. This is why we always test the status instead
 *	of blindly processing the next frame.
 *	
 */
static void olympic_rx(struct net_device *dev)
{
	struct olympic_private *olympic_priv=(struct olympic_private *)dev->priv;
	__u8 *olympic_mmio=olympic_priv->olympic_mmio;
	struct olympic_rx_status *rx_status;
	struct olympic_rx_desc *rx_desc ; 
	int rx_ring_last_received,length, buffer_cnt, cpy_length, frag_len;
	struct sk_buff *skb, *skb2;
	int i;

	rx_status=&(olympic_priv->olympic_rx_status_ring[(olympic_priv->rx_status_last_received + 1) & (OLYMPIC_RX_RING_SIZE - 1)]) ; 
 
	while (rx_status->status_buffercnt) { 
                __u32 l_status_buffercnt;

		olympic_priv->rx_status_last_received++ ;
		olympic_priv->rx_status_last_received &= (OLYMPIC_RX_RING_SIZE -1);
#if OLYMPIC_DEBUG
		printk(" stat_ring addr: %x \n", &(olympic_priv->olympic_rx_status_ring[olympic_priv->rx_status_last_received]) ); 
		printk("rx status: %x rx len: %x \n", le32_to_cpu(rx_status->status_buffercnt), le32_to_cpu(rx_status->fragmentcnt_framelen));
#endif
		length = le32_to_cpu(rx_status->fragmentcnt_framelen) & 0xffff;
		buffer_cnt = le32_to_cpu(rx_status->status_buffercnt) & 0xffff; 
		i = buffer_cnt ; /* Need buffer_cnt later for rxenq update */ 
		frag_len = le32_to_cpu(rx_status->fragmentcnt_framelen) >> 16; 

#if OLYMPIC_DEBUG 
		printk("length: %x, frag_len: %x, buffer_cnt: %x\n", length, frag_len, buffer_cnt);
#endif
                l_status_buffercnt = le32_to_cpu(rx_status->status_buffercnt);
		if(l_status_buffercnt & 0xC0000000) {
			if (l_status_buffercnt & 0x3B000000) {
				if (olympic_priv->olympic_message_level) {
					if (l_status_buffercnt & (1<<29))  /* Rx Frame Truncated */
						printk(KERN_WARNING "%s: Rx Frame Truncated \n",dev->name);
					if (l_status_buffercnt & (1<<28)) /*Rx receive overrun */
						printk(KERN_WARNING "%s: Rx Frame Receive overrun \n",dev->name);
					if (l_status_buffercnt & (1<<27)) /* No receive buffers */
						printk(KERN_WARNING "%s: No receive buffers \n",dev->name);
					if (l_status_buffercnt & (1<<25)) /* Receive frame error detect */
						printk(KERN_WARNING "%s: Receive frame error detect \n",dev->name);
					if (l_status_buffercnt & (1<<24)) /* Received Error Detect */
						printk(KERN_WARNING "%s: Received Error Detect \n",dev->name);
				} 
				olympic_priv->rx_ring_last_received += i ; 
				olympic_priv->rx_ring_last_received &= (OLYMPIC_RX_RING_SIZE -1) ; 
				olympic_priv->olympic_stats.rx_errors++;	 
			} else {	
			
				if (buffer_cnt == 1) {
					skb = dev_alloc_skb(olympic_priv->pkt_buf_sz) ; 
				} else {
					skb = dev_alloc_skb(length) ; 
				}

				if (skb == NULL) {
					printk(KERN_WARNING "%s: Not enough memory to copy packet to upper layers. \n",dev->name) ;
					olympic_priv->olympic_stats.rx_dropped++ ; 
					/* Update counters even though we don't transfer the frame */
					olympic_priv->rx_ring_last_received += i ; 
					olympic_priv->rx_ring_last_received &= (OLYMPIC_RX_RING_SIZE -1) ;  
				} else  {
					skb->dev = dev ; 

					/* Optimise based upon number of buffers used. 
			   	   	   If only one buffer is used we can simply swap the buffers around.
			   	   	   If more than one then we must use the new buffer and copy the information
			   	   	   first. Ideally all frames would be in a single buffer, this can be tuned by
                               	   	   altering the buffer size. */
				
 					if (buffer_cnt==1) {
						olympic_priv->rx_ring_last_received++ ; 
						olympic_priv->rx_ring_last_received &= (OLYMPIC_RX_RING_SIZE -1);
						rx_ring_last_received = olympic_priv->rx_ring_last_received ;
						skb2=olympic_priv->rx_ring_skb[rx_ring_last_received] ; 
						skb_put(skb2,length);
						skb2->protocol = tr_type_trans(skb2,dev);
						olympic_priv->olympic_rx_ring[rx_ring_last_received].buffer = cpu_to_le32(virt_to_bus(skb->data));
						olympic_priv->olympic_rx_ring[rx_ring_last_received].res_length = cpu_to_le32(olympic_priv->pkt_buf_sz); 
						olympic_priv->rx_ring_skb[rx_ring_last_received] = skb ; 
						netif_rx(skb2) ; 
					} else {
						do { /* Walk the buffers */ 
							olympic_priv->rx_ring_last_received++ ; 
							olympic_priv->rx_ring_last_received &= (OLYMPIC_RX_RING_SIZE -1);
							rx_ring_last_received = olympic_priv->rx_ring_last_received ; 
							rx_desc = &(olympic_priv->olympic_rx_ring[rx_ring_last_received]);
							cpy_length = (i == 1 ? frag_len : le32_to_cpu(rx_desc->res_length)); 
							memcpy(skb_put(skb, cpy_length), bus_to_virt(le32_to_cpu(rx_desc->buffer)), cpy_length) ; 
						} while (--i) ; 
		
						skb->protocol = tr_type_trans(skb,dev);
						netif_rx(skb) ; 
					} 
					olympic_priv->olympic_stats.rx_packets++ ; 
					olympic_priv->olympic_stats.rx_bytes += length ; 
				} /* if skb == null */
			} /* If status & 0x3b */

		} else { /*if buffercnt & 0xC */
			olympic_priv->rx_ring_last_received += i ; 
			olympic_priv->rx_ring_last_received &= (OLYMPIC_RX_RING_SIZE - 1) ; 
		} 

		rx_status->fragmentcnt_framelen = 0 ; 
		rx_status->status_buffercnt = 0 ; 
		rx_status = &(olympic_priv->olympic_rx_status_ring[(olympic_priv->rx_status_last_received+1) & (OLYMPIC_RX_RING_SIZE -1) ]);

		writew((((readw(olympic_mmio+RXENQ)) & 0x8000) ^ 0x8000) |  buffer_cnt , olympic_mmio+RXENQ); 
	} /* while */

}

static void olympic_interrupt(int irq, void *dev_id, struct pt_regs *regs) 
{
	struct net_device *dev= (struct net_device *)dev_id;
	struct olympic_private *olympic_priv=(struct olympic_private *)dev->priv;
	__u8 *olympic_mmio=olympic_priv->olympic_mmio;
	__u32 sisr;
	__u8 *adapter_check_area ; 
	
	sisr=readl(olympic_mmio+SISR_RR) ; /* Reset sisr */ 
	
	if (!(sisr & SISR_MI)) /* Interrupt isn't for us */ 
		return ;

	spin_lock(&olympic_priv->olympic_lock);

	if (sisr & (SISR_SRB_REPLY | SISR_TX1_EOF | SISR_RX_STATUS | SISR_ADAPTER_CHECK |  
			SISR_ASB_FREE | SISR_ARB_CMD | SISR_TRB_REPLY | SISR_RX_NOBUF)) {  
	
		if(sisr & SISR_SRB_REPLY) {
			if(olympic_priv->srb_queued==1) {
				wake_up_interruptible(&olympic_priv->srb_wait);
			} else if (olympic_priv->srb_queued==2) { 
				olympic_srb_bh(dev) ; 
			}
			olympic_priv->srb_queued=0;
		} /* SISR_SRB_REPLY */

		if (sisr & SISR_TX1_EOF) {
			olympic_priv->tx_ring_last_status++;
			olympic_priv->tx_ring_last_status &= (OLYMPIC_TX_RING_SIZE-1);
			olympic_priv->free_tx_ring_entries++;
			olympic_priv->olympic_stats.tx_bytes += olympic_priv->tx_ring_skb[olympic_priv->tx_ring_last_status]->len;
			olympic_priv->olympic_stats.tx_packets++ ; 
			dev_kfree_skb_irq(olympic_priv->tx_ring_skb[olympic_priv->tx_ring_last_status]);
			olympic_priv->olympic_tx_ring[olympic_priv->tx_ring_last_status].buffer=0xdeadbeef;
			olympic_priv->olympic_tx_status_ring[olympic_priv->tx_ring_last_status].status=0;
			netif_wake_queue(dev);
		} /* SISR_TX1_EOF */
	
		if (sisr & SISR_RX_STATUS) {
			olympic_rx(dev);
		} /* SISR_RX_STATUS */
	
		if (sisr & SISR_ADAPTER_CHECK) {
			printk(KERN_WARNING "%s: Adapter Check Interrupt Raised, 8 bytes of information follow:\n", dev->name);
			writel(readl(olympic_mmio+LAPWWO),olympic_mmio+LAPA);
			adapter_check_area = (__u8 *)(olympic_mmio+LAPWWO) ; 
			printk(KERN_WARNING "%s: Bytes %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",dev->name, readb(adapter_check_area+0), readb(adapter_check_area+1), readb(adapter_check_area+2), readb(adapter_check_area+3), readb(adapter_check_area+4), readb(adapter_check_area+5), readb(adapter_check_area+6), readb(adapter_check_area+7)) ; 
			free_irq(dev->irq, dev) ; 
	
		} /* SISR_ADAPTER_CHECK */
	
		if (sisr & SISR_ASB_FREE) {
			/* Wake up anything that is waiting for the asb response */  
			if (olympic_priv->asb_queued) {
				olympic_asb_bh(dev) ; 
			}
		} /* SISR_ASB_FREE */
	
		if (sisr & SISR_ARB_CMD) {
			olympic_arb_cmd(dev) ; 
		} /* SISR_ARB_CMD */
	
		if (sisr & SISR_TRB_REPLY) {
			/* Wake up anything that is waiting for the trb response */
			if (olympic_priv->trb_queued) {
				wake_up_interruptible(&olympic_priv->trb_wait);
			}
			olympic_priv->trb_queued = 0 ; 
		} /* SISR_TRB_REPLY */	
	
		if (sisr & SISR_RX_NOBUF) {
			/* According to the documentation, we don't have to do anything, but trapping it keeps it out of
                  	   	   /var/log/messages.  */
		} /* SISR_RX_NOBUF */
	} else { 
		printk(KERN_WARNING "%s: Unexpected interrupt: %x\n",dev->name, sisr);
		printk(KERN_WARNING "%s: SISR_MASK: %x\n",dev->name, readl(olympic_mmio+SISR_MASK)) ;
	} /* One if the interrupts we want */

	writel(SISR_MI,olympic_mmio+SISR_MASK_SUM);
	
	spin_unlock(&olympic_priv->olympic_lock) ; 
}	

static int olympic_xmit(struct sk_buff *skb, struct net_device *dev) 
{
	struct olympic_private *olympic_priv=(struct olympic_private *)dev->priv;
	__u8 *olympic_mmio=olympic_priv->olympic_mmio;
	unsigned long flags ; 

	spin_lock_irqsave(&olympic_priv->olympic_lock, flags);

	netif_stop_queue(dev);
	
	if(olympic_priv->free_tx_ring_entries) {
		olympic_priv->olympic_tx_ring[olympic_priv->tx_ring_free].buffer = cpu_to_le32(virt_to_bus(skb->data));
		olympic_priv->olympic_tx_ring[olympic_priv->tx_ring_free].status_length = cpu_to_le32(skb->len | (0x80000000));
		olympic_priv->tx_ring_skb[olympic_priv->tx_ring_free]=skb;
		olympic_priv->free_tx_ring_entries--;

        	olympic_priv->tx_ring_free++;
        	olympic_priv->tx_ring_free &= (OLYMPIC_TX_RING_SIZE-1);


		writew((((readw(olympic_mmio+TXENQ_1)) & 0x8000) ^ 0x8000) | 1,olympic_mmio+TXENQ_1);

		netif_wake_queue(dev);
		spin_unlock_irqrestore(&olympic_priv->olympic_lock,flags);
		return 0;
	} else {
		spin_unlock_irqrestore(&olympic_priv->olympic_lock,flags);
		return 1;
	} 

}
	

static int olympic_close(struct net_device *dev) 
{
	struct olympic_private *olympic_priv=(struct olympic_private *)dev->priv;
    	__u8 *olympic_mmio=olympic_priv->olympic_mmio,*srb;
	unsigned long flags;
	int i;

	netif_stop_queue(dev);
	
	writel(olympic_priv->srb,olympic_mmio+LAPA);
	srb=olympic_priv->olympic_lap + (olympic_priv->srb & (~0xf800));
	
    	writeb(SRB_CLOSE_ADAPTER,srb+0);
	writeb(0,srb+1);
	writeb(OLYMPIC_CLEAR_RET_CODE,srb+2);

	save_flags(flags);
	cli();	

	olympic_priv->srb_queued=1;

	writel(LISR_SRB_CMD,olympic_mmio+LISR_SUM);

	while(olympic_priv->srb_queued) {
	        interruptible_sleep_on_timeout(&olympic_priv->srb_wait, jiffies+60*HZ);
        	if(signal_pending(current))	{            
			printk(KERN_WARNING "%s: SRB timed out.\n",
                		dev->name);
            		printk(KERN_WARNING "SISR=%x MISR=%x\n",
                	readl(olympic_mmio+SISR),
                	readl(olympic_mmio+LISR));
            		olympic_priv->srb_queued=0;
            		break;
        	}
    	}

	restore_flags(flags) ; 
	olympic_priv->rx_status_last_received++;
	olympic_priv->rx_status_last_received&=OLYMPIC_RX_RING_SIZE-1;
	
	for(i=0;i<OLYMPIC_RX_RING_SIZE;i++) {
		dev_kfree_skb(olympic_priv->rx_ring_skb[olympic_priv->rx_status_last_received]);
		olympic_priv->rx_status_last_received++;
		olympic_priv->rx_status_last_received&=OLYMPIC_RX_RING_SIZE-1;
	}

	/* reset tx/rx fifo's and busmaster logic */

	writel(readl(olympic_mmio+BCTL)|(3<<13),olympic_mmio+BCTL);
	udelay(1);
	writel(readl(olympic_mmio+BCTL)&~(3<<13),olympic_mmio+BCTL);

#if OLYMPIC_DEBUG
	printk("srb(%p): ",srb);
	for(i=0;i<4;i++)
		printk("%x ",readb(srb+i));
	printk("\n");
#endif
	free_irq(dev->irq,dev);

	MOD_DEC_USE_COUNT ; 
	return 0;
	
}

static void olympic_set_rx_mode(struct net_device *dev) 
{
	struct olympic_private *olympic_priv = (struct olympic_private *) dev->priv ; 
   	__u8 *olympic_mmio = olympic_priv->olympic_mmio ; 
	__u8 options = 0; 
	__u8 *srb;
	struct dev_mc_list *dmi ; 
	unsigned char dev_mc_address[4] ; 
	int i ; 

	writel(olympic_priv->srb,olympic_mmio+LAPA);
	srb=olympic_priv->olympic_lap + (olympic_priv->srb & (~0xf800));
	options = olympic_priv->olympic_copy_all_options; 

	if (dev->flags&IFF_PROMISC)  
		options |= 0x61 ;
	else
		options &= ~0x61 ; 

	/* Only issue the srb if there is a change in options */

	if ((options ^ olympic_priv->olympic_copy_all_options)) { 
	
		/* Now to issue the srb command to alter the copy.all.options */
	
		writeb(SRB_MODIFY_RECEIVE_OPTIONS,srb);
		writeb(0,srb+1);
		writeb(OLYMPIC_CLEAR_RET_CODE,srb+2);
		writeb(0,srb+3);
		writeb(olympic_priv->olympic_receive_options,srb+4);
		writeb(options,srb+5);

		olympic_priv->srb_queued=2; /* Can't sleep, use srb_bh */

		writel(LISR_SRB_CMD,olympic_mmio+LISR_SUM);

		olympic_priv->olympic_copy_all_options = options ;
		
		return ;  
	} 

	/* Set the functional addresses we need for multicast */

	dev_mc_address[0] = dev_mc_address[1] = dev_mc_address[2] = dev_mc_address[3] = 0 ; 

	for (i=0,dmi=dev->mc_list;i < dev->mc_count; i++,dmi = dmi->next) { 
		dev_mc_address[0] |= dmi->dmi_addr[2] ; 
		dev_mc_address[1] |= dmi->dmi_addr[3] ; 
		dev_mc_address[2] |= dmi->dmi_addr[4] ; 
		dev_mc_address[3] |= dmi->dmi_addr[5] ; 
	}

	writeb(SRB_SET_FUNC_ADDRESS,srb+0);
	writeb(0,srb+1);
	writeb(OLYMPIC_CLEAR_RET_CODE,srb+2);
	writeb(0,srb+3);
	writeb(0,srb+4);
	writeb(0,srb+5);
	writeb(dev_mc_address[0],srb+6);
	writeb(dev_mc_address[1],srb+7);
	writeb(dev_mc_address[2],srb+8);
	writeb(dev_mc_address[3],srb+9);

	olympic_priv->srb_queued = 2 ;
	writel(LISR_SRB_CMD,olympic_mmio+LISR_SUM);

}

static void olympic_srb_bh(struct net_device *dev) 
{ 
	struct olympic_private *olympic_priv = (struct olympic_private *) dev->priv ; 
   	__u8 *olympic_mmio = olympic_priv->olympic_mmio ; 
	__u8 *srb;

	writel(olympic_priv->srb,olympic_mmio+LAPA);
	srb=olympic_priv->olympic_lap + (olympic_priv->srb & (~0xf800));

	switch (readb(srb)) { 

		/* SRB_MODIFY_RECEIVE_OPTIONS i.e. set_multicast_list options (promiscuous) 
                 * At some point we should do something if we get an error, such as
                 * resetting the IFF_PROMISC flag in dev
		 */

		case SRB_MODIFY_RECEIVE_OPTIONS:
			switch (readb(srb+2)) { 
				case 0x01:
					printk(KERN_WARNING "%s: Unrecognized srb command\n",dev->name) ; 
					break ; 
				case 0x04:
					printk(KERN_WARNING "%s: Adapter must be open for this operation, doh!!\n",dev->name);
					break ; 
				default:
					if (olympic_priv->olympic_message_level) 
						printk(KERN_WARNING "%s: Receive Options Modified to %x,%x\n",dev->name,olympic_priv->olympic_copy_all_options, olympic_priv->olympic_receive_options) ; 
					break ; 	
			} /* switch srb[2] */ 
			break ;
		
		/* SRB_SET_GROUP_ADDRESS - Multicast group setting 
                 */

		case SRB_SET_GROUP_ADDRESS:
			switch (readb(srb+2)) { 
				case 0x00:
					break ; 
				case 0x01:
					printk(KERN_WARNING "%s: Unrecognized srb command \n",dev->name) ; 
					break ;
				case 0x04:
					printk(KERN_WARNING "%s: Adapter must be open for this operation, doh!!\n",dev->name); 
					break ;
				case 0x3c:
					printk(KERN_WARNING "%s: Group/Functional address indicator bits not set correctly\n",dev->name) ; 
					break ;
				case 0x3e: /* If we ever implement individual multicast addresses, will need to deal with this */
					printk(KERN_WARNING "%s: Group address registers full\n",dev->name) ; 
					break ;  
				case 0x55:
					printk(KERN_INFO "%s: Group Address already set.\n",dev->name) ; 
					break ;
				default:
					break ; 
			} /* switch srb[2] */ 
			break ; 

		/* SRB_RESET_GROUP_ADDRESS - Remove a multicast address from group list
 		 */

		case SRB_RESET_GROUP_ADDRESS:
			switch (readb(srb+2)) { 
				case 0x00:
					break ; 
				case 0x01:
					printk(KERN_WARNING "%s: Unrecognized srb command \n",dev->name) ; 
					break ; 
				case 0x04:
					printk(KERN_WARNING "%s: Adapter must be open for this operation, doh!!\n",dev->name) ; 
					break ; 
				case 0x39: /* Must deal with this if individual multicast addresses used */
					printk(KERN_INFO "%s: Group address not found \n",dev->name); 
					break ;
				default:
					break ; 
			} /* switch srb[2] */
			break ; 

		
		/* SRB_SET_FUNC_ADDRESS - Called by the set_rx_mode 
		 */

		case SRB_SET_FUNC_ADDRESS:
			switch (readb(srb+2)) { 
				case 0x00:
					if (olympic_priv->olympic_message_level)
						printk(KERN_INFO "%s: Functional Address Mask Set \n",dev->name) ; 
					break ;
				case 0x01:
					printk(KERN_WARNING "%s: Unrecognized srb command \n",dev->name) ; 
					break ; 
				case 0x04:
					printk(KERN_WARNING "%s: Adapter must be open for this operation, doh!!\n",dev->name) ; 
					break ; 
				default:
					break ; 
			} /* switch srb[2] */
			break ; 
	
		/* SRB_READ_LOG - Read and reset the adapter error counters
 		 */

		case SRB_READ_LOG:
			switch (readb(srb+2)) { 
				case 0x00: 
					if (olympic_priv->olympic_message_level) 
						printk(KERN_INFO "%s: Read Log issued\n",dev->name) ; 
					break ; 
				case 0x01:
					printk(KERN_WARNING "%s: Unrecognized srb command \n",dev->name) ; 
					break ; 
				case 0x04:
					printk(KERN_WARNING "%s: Adapter must be open for this operation, doh!!\n",dev->name) ; 
					break ; 
			
			} /* switch srb[2] */
			break ; 
		
		/* SRB_READ_SR_COUNTERS - Read and reset the source routing bridge related counters */

		case SRB_READ_SR_COUNTERS:
			switch (readb(srb+2)) { 
				case 0x00: 
					if (olympic_priv->olympic_message_level) 
						printk(KERN_INFO "%s: Read Source Routing Counters issued\n",dev->name) ; 
					break ; 
				case 0x01:
					printk(KERN_WARNING "%s: Unrecognized srb command \n",dev->name) ; 
					break ; 
				case 0x04:
					printk(KERN_WARNING "%s: Adapter must be open for this operation, doh!!\n",dev->name) ; 
					break ; 
				default:
					break ; 
			} /* switch srb[2] */
			break ;
 
		default:
			printk(KERN_WARNING "%s: Unrecognized srb bh return value.\n",dev->name);
			break ; 
	} /* switch srb[0] */

} 

static struct net_device_stats * olympic_get_stats(struct net_device *dev)
{
	struct olympic_private *olympic_priv ;
	olympic_priv=(struct olympic_private *) dev->priv;
	return (struct net_device_stats *) &olympic_priv->olympic_stats; 
}

static int olympic_set_mac_address (struct net_device *dev, void *addr) 
{
	struct sockaddr *saddr = addr ; 
	struct olympic_private *olympic_priv = (struct olympic_private *)dev->priv ; 

	if (netif_running(dev)) { 
		printk(KERN_WARNING "%s: Cannot set mac/laa address while card is open\n", dev->name) ; 
		return -EIO ; 
	}

	memcpy(olympic_priv->olympic_laa, saddr->sa_data,dev->addr_len) ; 
	
	if (olympic_priv->olympic_message_level) { 
 		printk(KERN_INFO "%s: MAC/LAA Set to  = %x.%x.%x.%x.%x.%x\n",dev->name, olympic_priv->olympic_laa[0],
		olympic_priv->olympic_laa[1], olympic_priv->olympic_laa[2],
		olympic_priv->olympic_laa[3], olympic_priv->olympic_laa[4],
		olympic_priv->olympic_laa[5]);
	} 

	return 0 ; 
}

static void olympic_arb_cmd(struct net_device *dev)
{
	struct olympic_private *olympic_priv = (struct olympic_private *) dev->priv;
    	__u8 *olympic_mmio=olympic_priv->olympic_mmio;
	__u8 *arb_block, *asb_block, *srb  ; 
	__u8 header_len ; 
	__u16 frame_len, buffer_len ;
	struct sk_buff *mac_frame ;  
	__u8 *buf_ptr ;
	__u8 *frame_data ;  
	__u16 buff_off ; 
	__u16 lan_status = 0, lan_status_diff  ; /* Initialize to stop compiler warning */
	__u8 fdx_prot_error ; 
	__u16 next_ptr;
	int i ; 
#if OLYMPIC_NETWORK_MONITOR
	struct trh_hdr *mac_hdr ; 
#endif

	arb_block = (__u8 *)(olympic_priv->olympic_lap + olympic_priv->arb) ; 
	asb_block = (__u8 *)(olympic_priv->olympic_lap + olympic_priv->asb) ; 
	srb = (__u8 *)(olympic_priv->olympic_lap + olympic_priv->srb) ; 
	writel(readl(olympic_mmio+LAPA),olympic_mmio+LAPWWO);

	if (readb(arb_block+0) == ARB_RECEIVE_DATA) { /* Receive.data, MAC frames */

		header_len = readb(arb_block+8) ; /* 802.5 Token-Ring Header Length */	
		frame_len = swab16(readw(arb_block + 10)) ; 

		buff_off = swab16(readw(arb_block + 6)) ;
		
		buf_ptr = olympic_priv->olympic_lap + buff_off ; 

#if OLYMPIC_DEBUG
{
		int i;
		frame_data = buf_ptr+offsetof(struct mac_receive_buffer,frame_data) ; 

		for (i=0 ;  i < 14 ; i++) { 
			printk("Loc %d = %02x\n",i,readb(frame_data + i)); 
		}

		printk("next %04x, fs %02x, len %04x \n",readw(buf_ptr+offsetof(struct mac_receive_buffer,next)), readb(buf_ptr+offsetof(struct mac_receive_buffer,frame_status)), readw(buf_ptr+offsetof(struct mac_receive_buffer,buffer_length))); 
}
#endif 
		mac_frame = dev_alloc_skb(frame_len) ; 

		/* Walk the buffer chain, creating the frame */

		do {
			frame_data = buf_ptr+offsetof(struct mac_receive_buffer,frame_data) ; 
			buffer_len = swab16(readw(buf_ptr+offsetof(struct mac_receive_buffer,buffer_length))); 
			memcpy_fromio(skb_put(mac_frame, buffer_len), frame_data , buffer_len ) ;
			next_ptr=readw(buf_ptr+offsetof(struct mac_receive_buffer,next)); 

		} while (next_ptr && (buf_ptr=olympic_priv->olympic_lap + ntohs(next_ptr)));

#if OLYMPIC_NETWORK_MONITOR
		printk(KERN_WARNING "%s: Received MAC Frame, details: \n",dev->name) ;
		mac_hdr = (struct trh_hdr *)mac_frame->data ; 
		printk(KERN_WARNING "%s: MAC Frame Dest. Addr: %02x:%02x:%02x:%02x:%02x:%02x \n", dev->name , mac_hdr->daddr[0], mac_hdr->daddr[1], mac_hdr->daddr[2], mac_hdr->daddr[3], mac_hdr->daddr[4], mac_hdr->daddr[5]) ; 
		printk(KERN_WARNING "%s: MAC Frame Srce. Addr: %02x:%02x:%02x:%02x:%02x:%02x \n", dev->name , mac_hdr->saddr[0], mac_hdr->saddr[1], mac_hdr->saddr[2], mac_hdr->saddr[3], mac_hdr->saddr[4], mac_hdr->saddr[5]) ; 
#endif
		mac_frame->dev = dev ; 
		mac_frame->protocol = tr_type_trans(mac_frame,dev);
		netif_rx(mac_frame) ; 	

		/* Now tell the card we have dealt with the received frame */

		/* Set LISR Bit 1 */
		writel(LISR_ARB_FREE,olympic_priv->olympic_lap + LISR_SUM);

		/* Is the ASB free ? */ 	
		
		if (readb(asb_block + 2) != 0xff) { 
			olympic_priv->asb_queued = 1 ; 
			writel(LISR_ASB_FREE_REQ,olympic_priv->olympic_mmio+LISR_SUM); 
			return ; 	
			/* Drop out and wait for the bottom half to be run */
		}
		
		writeb(ASB_RECEIVE_DATA,asb_block); /* Receive data */
		writeb(OLYMPIC_CLEAR_RET_CODE,asb_block+2); /* Necessary ?? */
		writeb(readb(arb_block+6),asb_block+6); /* Must send the address back to the adapter */
		writeb(readb(arb_block+7),asb_block+7); /* To let it know we have dealt with the data */		

		writel(LISR_ASB_REPLY | LISR_ASB_FREE_REQ,olympic_priv->olympic_mmio+LISR_SUM);
		
		olympic_priv->asb_queued = 2 ; 
	
		return ; 	
		
	} else if (readb(arb_block) == ARB_LAN_CHANGE_STATUS) { /* Lan.change.status */
		lan_status = swab16(readw(arb_block+6));
		fdx_prot_error = readb(arb_block+8) ; 
		
		/* Issue ARB Free */
		writel(LISR_ARB_FREE,olympic_priv->olympic_mmio+LISR_SUM);

		lan_status_diff = olympic_priv->olympic_lan_status ^ lan_status ; 

		if (lan_status_diff & (LSC_LWF | LSC_ARW | LSC_FPE | LSC_RR) ) { 
			if (lan_status_diff & LSC_LWF) 
					printk(KERN_WARNING "%s: Short circuit detected on the lobe\n",dev->name);
			if (lan_status_diff & LSC_ARW) 
					printk(KERN_WARNING "%s: Auto removal error\n",dev->name);
			if (lan_status_diff & LSC_FPE)
					printk(KERN_WARNING "%s: FDX Protocol Error\n",dev->name);
			if (lan_status_diff & LSC_RR) 
					printk(KERN_WARNING "%s: Force remove MAC frame received\n",dev->name);
		
			/* Adapter has been closed by the hardware */
		
			/* reset tx/rx fifo's and busmaster logic */

			writel(readl(olympic_mmio+BCTL)|(3<<13),olympic_mmio+BCTL);
			udelay(1);
			writel(readl(olympic_mmio+BCTL)&~(3<<13),olympic_mmio+BCTL);
			netif_stop_queue(dev);
			olympic_priv->srb = readw(olympic_priv->olympic_lap + LAPWWO) ; 
			
			olympic_priv->rx_status_last_received++;
			olympic_priv->rx_status_last_received&=OLYMPIC_RX_RING_SIZE-1;
			for(i=0;i<OLYMPIC_RX_RING_SIZE;i++) {
				dev_kfree_skb(olympic_priv->rx_ring_skb[olympic_priv->rx_status_last_received]);
				olympic_priv->rx_status_last_received++;
				olympic_priv->rx_status_last_received&=OLYMPIC_RX_RING_SIZE-1;
			}

			free_irq(dev->irq,dev);
			
			printk(KERN_WARNING "%s: Adapter has been closed \n", dev->name) ; 
			MOD_DEC_USE_COUNT ; 
		} /* If serious error */
		
		if (olympic_priv->olympic_message_level) { 
			if (lan_status_diff & LSC_SIG_LOSS) 
					printk(KERN_WARNING "%s: No receive signal detected \n", dev->name) ; 
			if (lan_status_diff & LSC_HARD_ERR)
					printk(KERN_INFO "%s: Beaconing \n",dev->name);
			if (lan_status_diff & LSC_SOFT_ERR)
					printk(KERN_WARNING "%s: Adapter transmitted Soft Error Report Mac Frame \n",dev->name);
			if (lan_status_diff & LSC_TRAN_BCN) 
					printk(KERN_INFO "%s: We are tranmitting the beacon, aaah\n",dev->name);
			if (lan_status_diff & LSC_SS) 
					printk(KERN_INFO "%s: Single Station on the ring \n", dev->name);
			if (lan_status_diff & LSC_RING_REC)
					printk(KERN_INFO "%s: Ring recovery ongoing\n",dev->name);
			if (lan_status_diff & LSC_FDX_MODE)
					printk(KERN_INFO "%s: Operating in FDX mode\n",dev->name);
		} 	
		
		if (lan_status_diff & LSC_CO) { 
					
				if (olympic_priv->olympic_message_level) 
					printk(KERN_INFO "%s: Counter Overflow \n", dev->name);
					
				/* Issue READ.LOG command */

				writeb(SRB_READ_LOG, srb);
				writeb(0,srb+1);
				writeb(OLYMPIC_CLEAR_RET_CODE,srb+2);
				writeb(0,srb+3);
				writeb(0,srb+4);
				writeb(0,srb+5);
					
				olympic_priv->srb_queued=2; /* Can't sleep, use srb_bh */

				writel(LISR_SRB_CMD,olympic_mmio+LISR_SUM);
					
		}

		if (lan_status_diff & LSC_SR_CO) { 

				if (olympic_priv->olympic_message_level)
					printk(KERN_INFO "%s: Source routing counters overflow\n", dev->name);

				/* Issue a READ.SR.COUNTERS */
				
				writeb(SRB_READ_SR_COUNTERS,srb);
				writeb(0,srb+1);
				writeb(OLYMPIC_CLEAR_RET_CODE,srb+2);
				writeb(0,srb+3);
				
				olympic_priv->srb_queued=2; /* Can't sleep, use srb_bh */

				writel(LISR_SRB_CMD,olympic_mmio+LISR_SUM);

		}

		olympic_priv->olympic_lan_status = lan_status ; 
	
	}  /* Lan.change.status */
	else
		printk(KERN_WARNING "%s: Unknown arb command \n", dev->name);
}

static void olympic_asb_bh(struct net_device *dev) 
{
	struct olympic_private *olympic_priv = (struct olympic_private *) dev->priv ; 
	__u8 *arb_block, *asb_block ; 

	arb_block = (__u8 *)(olympic_priv->olympic_lap + olympic_priv->arb) ; 
	asb_block = (__u8 *)(olympic_priv->olympic_lap + olympic_priv->asb) ; 

	if (olympic_priv->asb_queued == 1) {   /* Dropped through the first time */

		writeb(ASB_RECEIVE_DATA,asb_block); /* Receive data */
		writeb(OLYMPIC_CLEAR_RET_CODE,asb_block+2); /* Necessary ?? */
		writeb(readb(arb_block+6),asb_block+6); /* Must send the address back to the adapter */
		writeb(readb(arb_block+7),asb_block+7); /* To let it know we have dealt with the data */		

		writel(LISR_ASB_REPLY | LISR_ASB_FREE_REQ,olympic_priv->olympic_mmio+LISR_SUM);
		olympic_priv->asb_queued = 2 ; 

		return ; 
	}

	if (olympic_priv->asb_queued == 2) { 
		switch (readb(asb_block+2)) {
			case 0x01:
				printk(KERN_WARNING "%s: Unrecognized command code \n", dev->name);
				break ;
			case 0x26:
				printk(KERN_WARNING "%s: Unrecognized buffer address \n", dev->name);
				break ;
			case 0xFF:
				/* Valid response, everything should be ok again */
				break ;
			default:
				printk(KERN_WARNING "%s: Invalid return code in asb\n",dev->name);
				break ;
		}
	}
	olympic_priv->asb_queued = 0 ; 
}
 
static int olympic_change_mtu(struct net_device *dev, int mtu) 
{
	struct olympic_private *olympic_priv = (struct olympic_private *) dev->priv;
	__u16 max_mtu ; 

	if (olympic_priv->olympic_ring_speed == 4)
		max_mtu = 4500 ; 
	else
		max_mtu = 18000 ; 
	
	if (mtu > max_mtu)
		return -EINVAL ; 
	if (mtu < 100) 
		return -EINVAL ; 

	dev->mtu = mtu ; 
	olympic_priv->pkt_buf_sz = mtu + TR_HLEN ; 

	return 0 ; 
}

#if OLYMPIC_NETWORK_MONITOR
#ifdef CONFIG_PROC_FS
static int olympic_proc_info(char *buffer, char **start, off_t offset, int length, int *eof, void *data)
{
	struct pci_dev *pci_device = NULL ;
	int len=0;
	off_t begin=0;
	off_t pos=0;
	int size;
	
	struct net_device *dev;


	size = sprintf(buffer, 
		"IBM Pit/Pit-Phy/Olympic Chipset Token Ring Adapters\n");
	
	pos+=size;
	len+=size;
	

	while((pci_device=pci_find_device(PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_TR_WAKE, pci_device))) {
	
		for (dev = dev_base; dev != NULL; dev = dev->next) 
		{
			if (dev->base_addr == pci_device->resource[0].start ) { /* Yep, an Olympic device */	
				size = sprintf_info(buffer+len, dev);
				len+=size;
				pos=begin+len;
				
				if(pos<offset)
				{
					len=0;
					begin=pos;
				}
				if(pos>offset+length)
					break;
			} /* if */
		} /* for */
	} /* While */

	*start=buffer+(offset-begin);	/* Start of wanted data */
	len-=(offset-begin);		/* Start slop */
	if(len>length)
		len=length;		/* Ending slop */
	return len;
}

static int sprintf_info(char *buffer, struct net_device *dev)
{
	struct olympic_private *olympic_priv=(struct olympic_private *)dev->priv;
	__u8 *oat = (__u8 *)(olympic_priv->olympic_lap + olympic_priv->olympic_addr_table_addr) ; 
	__u8 *opt = (__u8 *)(olympic_priv->olympic_lap + olympic_priv->olympic_parms_addr) ; 
	int size = 0 ; 
		
	size = sprintf(buffer, "\n%6s: Adapter Address   : Node Address      : Functional Addr\n",
 	   dev->name); 

	size += sprintf(buffer+size, "%6s: %02x:%02x:%02x:%02x:%02x:%02x : %02x:%02x:%02x:%02x:%02x:%02x : %02x:%02x:%02x:%02x\n",
	   dev->name,
           dev->dev_addr[0],
	   dev->dev_addr[1],
	   dev->dev_addr[2],
 	   dev->dev_addr[3],
	   dev->dev_addr[4],
	   dev->dev_addr[5],
	   readb(oat+offsetof(struct olympic_adapter_addr_table,node_addr)), 
	   readb(oat+offsetof(struct olympic_adapter_addr_table,node_addr)+1),
	   readb(oat+offsetof(struct olympic_adapter_addr_table,node_addr)+2),
	   readb(oat+offsetof(struct olympic_adapter_addr_table,node_addr)+3),
	   readb(oat+offsetof(struct olympic_adapter_addr_table,node_addr)+4),
	   readb(oat+offsetof(struct olympic_adapter_addr_table,node_addr)+5),
	   readb(oat+offsetof(struct olympic_adapter_addr_table,func_addr)), 
	   readb(oat+offsetof(struct olympic_adapter_addr_table,func_addr)+1),
	   readb(oat+offsetof(struct olympic_adapter_addr_table,func_addr)+2),
	   readb(oat+offsetof(struct olympic_adapter_addr_table,func_addr)+3));
	 
	size += sprintf(buffer+size, "\n%6s: Token Ring Parameters Table:\n", dev->name);

	size += sprintf(buffer+size, "%6s: Physical Addr : Up Node Address   : Poll Address      : AccPri : Auth Src : Att Code :\n",
	  dev->name) ; 
	   
	size += sprintf(buffer+size, "%6s: %02x:%02x:%02x:%02x   : %02x:%02x:%02x:%02x:%02x:%02x : %02x:%02x:%02x:%02x:%02x:%02x : %04x   : %04x     :  %04x    :\n",
	  dev->name,
	  readb(opt+offsetof(struct olympic_parameters_table, phys_addr)),
	  readb(opt+offsetof(struct olympic_parameters_table, phys_addr)+1),
	  readb(opt+offsetof(struct olympic_parameters_table, phys_addr)+2),
	  readb(opt+offsetof(struct olympic_parameters_table, phys_addr)+3),
	  readb(opt+offsetof(struct olympic_parameters_table, up_node_addr)),
	  readb(opt+offsetof(struct olympic_parameters_table, up_node_addr)+1),
	  readb(opt+offsetof(struct olympic_parameters_table, up_node_addr)+2),
	  readb(opt+offsetof(struct olympic_parameters_table, up_node_addr)+3),
	  readb(opt+offsetof(struct olympic_parameters_table, up_node_addr)+4),
	  readb(opt+offsetof(struct olympic_parameters_table, up_node_addr)+5),
	  readb(opt+offsetof(struct olympic_parameters_table, poll_addr)),
	  readb(opt+offsetof(struct olympic_parameters_table, poll_addr)+1),
	  readb(opt+offsetof(struct olympic_parameters_table, poll_addr)+2),
	  readb(opt+offsetof(struct olympic_parameters_table, poll_addr)+3),
	  readb(opt+offsetof(struct olympic_parameters_table, poll_addr)+4),
	  readb(opt+offsetof(struct olympic_parameters_table, poll_addr)+5),
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, acc_priority))),
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, auth_source_class))),
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, att_code))));

	size += sprintf(buffer+size, "%6s: Source Address    : Bcn T : Maj. V : Lan St : Lcl Rg : Mon Err : Frame Correl : \n",
	  dev->name) ; 
	
	size += sprintf(buffer+size, "%6s: %02x:%02x:%02x:%02x:%02x:%02x : %04x  : %04x   : %04x   : %04x   : %04x    :     %04x     : \n",
	  dev->name,
	  readb(opt+offsetof(struct olympic_parameters_table, source_addr)),
	  readb(opt+offsetof(struct olympic_parameters_table, source_addr)+1),
	  readb(opt+offsetof(struct olympic_parameters_table, source_addr)+2),
	  readb(opt+offsetof(struct olympic_parameters_table, source_addr)+3),
	  readb(opt+offsetof(struct olympic_parameters_table, source_addr)+4),
	  readb(opt+offsetof(struct olympic_parameters_table, source_addr)+5),
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, beacon_type))),
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, major_vector))),
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, lan_status))),
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, local_ring))),
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, mon_error))),
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, frame_correl))));

	size += sprintf(buffer+size, "%6s: Beacon Details :  Tx  :  Rx  : NAUN Node Address : NAUN Node Phys : \n",
	  dev->name) ; 

	size += sprintf(buffer+size, "%6s:                :  %02x  :  %02x  : %02x:%02x:%02x:%02x:%02x:%02x : %02x:%02x:%02x:%02x    : \n",
	  dev->name,
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, beacon_transmit))),
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, beacon_receive))),
	  readb(opt+offsetof(struct olympic_parameters_table, beacon_naun)),
	  readb(opt+offsetof(struct olympic_parameters_table, beacon_naun)+1),
	  readb(opt+offsetof(struct olympic_parameters_table, beacon_naun)+2),
	  readb(opt+offsetof(struct olympic_parameters_table, beacon_naun)+3),
	  readb(opt+offsetof(struct olympic_parameters_table, beacon_naun)+4),
	  readb(opt+offsetof(struct olympic_parameters_table, beacon_naun)+5),
	  readb(opt+offsetof(struct olympic_parameters_table, beacon_phys)),
	  readb(opt+offsetof(struct olympic_parameters_table, beacon_phys)+1),
	  readb(opt+offsetof(struct olympic_parameters_table, beacon_phys)+2),
	  readb(opt+offsetof(struct olympic_parameters_table, beacon_phys)+3));

 
	return size;
}
#endif
#endif 

#ifdef MODULE

static struct net_device* dev_olympic[OLYMPIC_MAX_ADAPTERS];

int init_module(void)
{
        int i;

#if OLYMPIC_NETWORK_MONITOR
#ifdef CONFIG_PROC_FS
	create_proc_read_entry("net/olympic_tr",0,0,olympic_proc_info,NULL); 
#endif
#endif
        for (i = 0; (i<OLYMPIC_MAX_ADAPTERS); i++) {
		dev_olympic[i] = NULL;
                dev_olympic[i] = init_trdev(dev_olympic[i], 0);
                if (dev_olympic[i] == NULL)
                        return -ENOMEM;

		dev_olympic[i]->init      = &olympic_probe;

	        if (register_trdev(dev_olympic[i]) != 0) {
			kfree(dev_olympic[i]);
			dev_olympic[i] = NULL;
		        if (i == 0) {
			        printk("Olympic: No IBM PCI Token Ring cards found in system.\n");
		                return -EIO;
			} else {
			       	printk("Olympic: %d IBM PCI Token Ring card(s) found in system.\n",i) ; 
				return 0;
			}
	        }
	}

	return 0;
}

void cleanup_module(void)
{
        int i;

        for (i = 0; i < OLYMPIC_MAX_ADAPTERS; i++)
	        if (dev_olympic[i]) {
			 unregister_trdev(dev_olympic[i]);
			 release_region(dev_olympic[i]->base_addr, OLYMPIC_IO_SPACE);
			 kfree(dev_olympic[i]->priv);
			 kfree(dev_olympic[i]);
			 dev_olympic[i] = NULL;
                }

#if OLYMPIC_NETWORK_MONITOR
#ifdef CONFIG_PROC_FS
	remove_proc_entry("net/olympic_tr", NULL) ; 
#endif 
#endif
}
#endif /* MODULE */

