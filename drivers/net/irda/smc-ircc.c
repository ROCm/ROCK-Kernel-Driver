/*********************************************************************
 *                
 * Filename:      smc-ircc.c
 * Version:       0.4
 * Description:   Driver for the SMC Infrared Communications Controller
 * Status:        Experimental.
 * Author:        Thomas Davis (tadavis@jps.net)
 * Created at:    
 * Modified at:   Tue Feb 22 10:05:06 2000
 * Modified by:   Dag Brattli <dag@brattli.net>
 * 
 *     Copyright (c) 1999-2000 Dag Brattli
 *     Copyright (c) 1998-1999 Thomas Davis, 
 *     All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License 
 *     along with this program; if not, write to the Free Software 
 *     Foundation, Inc., 59 Temple Place, Suite 330, Boston, 
 *     MA 02111-1307 USA
 *
 *     SIO's: SMC FDC37N869, FDC37C669, FDC37N958
 *     Applicable Models : Fujitsu Lifebook 635t, Sony PCG-505TX
 *
 ********************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/rtnetlink.h>
#include <linux/serial_reg.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/byteorder.h>

#include <linux/pm.h>

#include <net/irda/wrapper.h>
#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/irlap_frame.h>
#include <net/irda/irda_device.h>

#include <net/irda/smc-ircc.h>
#include <net/irda/irport.h>

static char *driver_name = "smc-ircc";

#define CHIP_IO_EXTENT 8

static unsigned int io[]  = { ~0, ~0 }; 
static unsigned int io2[] = { 0, 0 };

static struct ircc_cb *dev_self[] = { NULL, NULL};

/* Some prototypes */
static int  ircc_open(int i, unsigned int iobase, unsigned int board_addr);
#ifdef MODULE
static int  ircc_close(struct ircc_cb *self);
#endif /* MODULE */
static int  ircc_probe(int iobase, int board_addr);
static int  ircc_probe_58(smc_chip_t *chip, chipio_t *info);
static int  ircc_probe_69(smc_chip_t *chip, chipio_t *info);
static int  ircc_dma_receive(struct ircc_cb *self, int iobase); 
static void ircc_dma_receive_complete(struct ircc_cb *self, int iobase);
static int  ircc_hard_xmit(struct sk_buff *skb, struct net_device *dev);
static void ircc_dma_xmit(struct ircc_cb *self, int iobase, int bofs);
static void ircc_change_speed(void *priv, __u32 speed);
static void ircc_interrupt(int irq, void *dev_id, struct pt_regs *regs);
#if 0 /* unused */
static int  ircc_is_receiving(struct ircc_cb *self);
#endif /* unused */

static int  ircc_net_open(struct net_device *dev);
static int  ircc_net_close(struct net_device *dev);
static int  ircc_pmproc(struct pm_dev *dev, pm_request_t rqst, void *data);

/* These are the currently known SMC chipsets */
static smc_chip_t chips[] =
{
	{ "FDC37C669", 0x55, 0x55, 0x0d, 0x04, ircc_probe_69 },
	{ "FDC37N769", 0x55, 0x55, 0x0d, 0x28, ircc_probe_69 },
	{ "FDC37N869", 0x55, 0x00, 0x0d, 0x29, ircc_probe_69 },
	{ "FDC37N958", 0x55, 0x55, 0x20, 0x09, ircc_probe_58 },
	{ NULL }
};

static int ircc_irq=255;
static int ircc_dma=255;

static inline void register_bank(int iobase, int bank)
{
        outb(((inb(iobase+IRCC_MASTER) & 0xf0) | (bank & 0x07)),
               iobase+IRCC_MASTER);
}

/*
 * Function ircc_init ()
 *
 *    Initialize chip. Just try to find out how many chips we are dealing with
 *    and where they are
 */
int __init ircc_init(void)
{
	static int smcreg[] = { 0x3f0, 0x370 };
	smc_chip_t *chip;
	chipio_t info;
	int ret = -ENODEV;
	int i;

	IRDA_DEBUG(0, __FUNCTION__ "\n");

	/* Probe for all the NSC chipsets we know about */
	for (chip=chips; chip->name ; chip++,i++) {
		for (i=0; i<2; i++) {
			info.cfg_base = smcreg[i];
			
			/* 
			 * First we check if the user has supplied any
                         * parameters which we should use instead of probed
			 * values
			 */
			if (io[i] < 2000) {
				info.fir_base = io[i];
				info.sir_base = io2[i];
			} else if (chip->probe(chip, &info) < 0)
				continue;
			if (check_region(info.fir_base, CHIP_IO_EXTENT) < 0)
				continue;
			if (check_region(info.sir_base, CHIP_IO_EXTENT) < 0)
				continue;
			if (ircc_open(i, info.fir_base, info.sir_base) == 0)
				ret = 0; 
		}
	}
	return ret;
}

/*
 * Function ircc_cleanup ()
 *
 *    Close all configured chips
 *
 */
#ifdef MODULE
static void ircc_cleanup(void)
{
	int i;

	IRDA_DEBUG(0, __FUNCTION__ "\n");

	for (i=0; i < 2; i++) {
		if (dev_self[i])
			ircc_close(dev_self[i]);
	}
}
#endif /* MODULE */

/*
 * Function ircc_open (iobase, irq)
 *
 *    Open driver instance
 *
 */
static int ircc_open(int i, unsigned int fir_base, unsigned int sir_base)
{
	struct ircc_cb *self;
        struct irport_cb *irport;
	int config;
	int ret;

	IRDA_DEBUG(0, __FUNCTION__ "\n");

	if ((config = ircc_probe(fir_base, sir_base)) == -1) {
	        IRDA_DEBUG(0, __FUNCTION__ 
			   "(), addr 0x%04x - no device found!\n", fir_base);
		return -1;
	}
	
	/*
	 *  Allocate new instance of the driver
	 */
	self = kmalloc(sizeof(struct ircc_cb), GFP_KERNEL);
	if (self == NULL) {
		ERROR("%s, Can't allocate memory for control block!\n",
                      driver_name);
		return -ENOMEM;
	}
	memset(self, 0, sizeof(struct ircc_cb));
	spin_lock_init(&self->lock);
   
	/* Need to store self somewhere */
	dev_self[i] = self;

	irport = irport_open(i, sir_base, config >> 4 & 0x0f);
	if (!irport)
		return -ENODEV;

	/* Steal the network device from irport */
	self->netdev = irport->netdev;
	self->irport = irport;
	irport->priv = self;

	/* Initialize IO */
	self->io.fir_base  = fir_base;
        self->io.sir_base  = sir_base; /* Used by irport */
        self->io.irq       = config >> 4 & 0x0f;
	if (ircc_irq < 255) {
	        MESSAGE("%s, Overriding IRQ - chip says %d, using %d\n",
			driver_name, self->io.irq, ircc_irq);
		self->io.irq = ircc_irq;
	}
        self->io.fir_ext   = CHIP_IO_EXTENT;
        self->io.sir_ext   = 8;       /* Used by irport */
        self->io.dma       = config & 0x0f;
	if (ircc_dma < 255) {
	        MESSAGE("%s, Overriding DMA - chip says %d, using %d\n",
			driver_name, self->io.dma, ircc_dma);
		self->io.dma = ircc_dma;
	}

	/* Lock the port that we need */
	ret = check_region(self->io.fir_base, self->io.fir_ext);
	if (ret < 0) { 
		IRDA_DEBUG(0, __FUNCTION__ ": can't get fir_base of 0x%03x\n",
			   self->io.fir_base);
                kfree(self);
		return -ENODEV;
	}
	request_region(self->io.fir_base, self->io.fir_ext, driver_name);

	/* Initialize QoS for this device */
	irda_init_max_qos_capabilies(&irport->qos);
	
	/* The only value we must override it the baudrate */
	irport->qos.baud_rate.bits = IR_9600|IR_19200|IR_38400|IR_57600|
		IR_115200|IR_576000|IR_1152000|(IR_4000000 << 8);

	irport->qos.min_turn_time.bits = 0x07;
	irport->qos.window_size.bits = 0x01;
	irda_qos_bits_to_value(&irport->qos);

	irport->flags = IFF_FIR|IFF_MIR|IFF_SIR|IFF_DMA|IFF_PIO;
	
	/* Max DMA buffer size needed = (data_size + 6) * (window_size) + 6; */
	self->rx_buff.truesize = 4000; 
	self->tx_buff.truesize = 4000;

	self->rx_buff.head = (__u8 *) kmalloc(self->rx_buff.truesize,
					      GFP_KERNEL|GFP_DMA);
	if (self->rx_buff.head == NULL)
		return -ENOMEM;
	memset(self->rx_buff.head, 0, self->rx_buff.truesize);
	
	self->tx_buff.head = (__u8 *) kmalloc(self->tx_buff.truesize, 
					      GFP_KERNEL|GFP_DMA);
	if (self->tx_buff.head == NULL) {
		kfree(self->rx_buff.head);
		return -ENOMEM;
	}
	memset(self->tx_buff.head, 0, self->tx_buff.truesize);

	self->rx_buff.in_frame = FALSE;
	self->rx_buff.state = OUTSIDE_FRAME;
	self->tx_buff.data = self->tx_buff.head;
	self->rx_buff.data = self->rx_buff.head;
	
	/* Override the speed change function, since we must control it now */
	irport->change_speed = &ircc_change_speed;
	irport->interrupt    = &ircc_interrupt;
	self->netdev->open   = &ircc_net_open;
	self->netdev->stop   = &ircc_net_close;

	irport_start(self->irport);

        self->pmdev = pm_register(PM_SYS_DEV, PM_SYS_IRDA, ircc_pmproc);
        if (self->pmdev)
                self->pmdev->data = self;

	return 0;
}

/*
 * Function ircc_close (self)
 *
 *    Close driver instance
 *
 */
#ifdef MODULE
static int ircc_close(struct ircc_cb *self)
{
	int iobase;

	IRDA_DEBUG(0, __FUNCTION__ "\n");

	ASSERT(self != NULL, return -1;);

        iobase = self->io.fir_base;

	irport_close(self->irport);

	/* Stop interrupts */
	register_bank(iobase, 0);
	outb(0, iobase+IRCC_IER);
	outb(IRCC_MASTER_RESET, iobase+IRCC_MASTER);
	outb(0x00, iobase+IRCC_MASTER);
#if 0
	/* Reset to SIR mode */
	register_bank(iobase, 1);
        outb(IRCC_CFGA_IRDA_SIR_A|IRCC_CFGA_TX_POLARITY, iobase+IRCC_SCE_CFGA);
        outb(IRCC_CFGB_IR, iobase+IRCC_SCE_CFGB);
#endif
	/* Release the PORT that this driver is using */
	IRDA_DEBUG(0, __FUNCTION__ "(), releasing 0x%03x\n", 
		   self->io.fir_base);

	release_region(self->io.fir_base, self->io.fir_ext);

	if (self->tx_buff.head)
		kfree(self->tx_buff.head);
	
	if (self->rx_buff.head)
		kfree(self->rx_buff.head);

	kfree(self);

	return 0;
}
#endif /* MODULE */

/*
 * Function ircc_probe_69 (chip, info)
 *
 *    Probes for the SMC FDC37C669 and FDC37N869
 *
 */
static int ircc_probe_69(smc_chip_t *chip, chipio_t *info)
{
	int cfg_base = info->cfg_base;
	__u8 devid, mode;
	int ret = -ENODEV;
	int fir_io;
	
	IRDA_DEBUG(0, __FUNCTION__ "()\n");

	/* Enter configuration */
	outb(chip->entr1, cfg_base);
	outb(chip->entr2, cfg_base);
	
	outb(chip->cid_index, cfg_base);
	devid = inb(cfg_base+1);
	IRDA_DEBUG(0, __FUNCTION__ "(), devid=0x%02x\n",devid);
	
	/* Check for expected device ID; are there others? */
	if (devid == chip->cid_value) {
		outb(0x0c, cfg_base);
		mode = inb(cfg_base+1);
		mode = (mode & 0x38) >> 3;
		
		/* Value for IR port */
		if (mode && mode < 4) {
			/* SIR iobase */
			outb(0x25, cfg_base);
			info->sir_base = inb(cfg_base+1) << 2;

		       	/* FIR iobase */
			outb(0x2b, cfg_base);
			fir_io = inb(cfg_base+1) << 3;
			if (fir_io) {
				ret = 0;
				info->fir_base = fir_io;
			}
		}
	}
	
	/* Exit configuration */
	outb(0xaa, cfg_base);

	return ret;
}

/*
 * Function ircc_probe_58 (chip, info)
 *
 *    Probes for the SMC FDC37N958
 *
 */
static int ircc_probe_58(smc_chip_t *chip, chipio_t *info)
{
	int cfg_base = info->cfg_base;
	__u8 devid;
	int ret = -ENODEV;
	int fir_io;
	
	IRDA_DEBUG(0, __FUNCTION__ "()\n");

	/* Enter configuration */
	outb(chip->entr1, cfg_base);
	outb(chip->entr2, cfg_base);
	
	outb(chip->cid_index, cfg_base);
	devid = inb(cfg_base+1);
	IRDA_DEBUG(0, __FUNCTION__ "(), devid=0x%02x\n",devid);
	
	/* Check for expected device ID; are there others? */
	if (devid == chip->cid_value) {
		/* Select logical device (UART2) */
		outb(0x07, cfg_base);
		outb(0x05, cfg_base + 1);
		
		/* SIR iobase */
		outb(0x60, cfg_base);
		info->sir_base = inb(cfg_base + 1) << 8;
		outb(0x61, cfg_base);
		info->sir_base |= inb(cfg_base + 1);
		
		/* Read FIR base */
		outb(0x62, cfg_base);
		fir_io = inb(cfg_base + 1) << 8;
		outb(0x63, cfg_base);
		fir_io |= inb(cfg_base + 1);
		outb(0x2b, cfg_base);
		if (fir_io) {
			ret = 0;
			info->fir_base = fir_io;
		}
	}
	
	/* Exit configuration */
	outb(0xaa, cfg_base);

	return ret;
}

/*
 * Function ircc_probe (iobase, board_addr, irq, dma)
 *
 *    Returns non-negative on success.
 *
 */
static int ircc_probe(int fir_base, int sir_base) 
{
	int low, high, chip, config, dma, irq;
	int iobase = fir_base;
	int version = 1;

	IRDA_DEBUG(0, __FUNCTION__ "\n");

	register_bank(iobase, 3);
	high    = inb(iobase+IRCC_ID_HIGH);
	low     = inb(iobase+IRCC_ID_LOW);
	chip    = inb(iobase+IRCC_CHIP_ID);
	version = inb(iobase+IRCC_VERSION);
	config  = inb(iobase+IRCC_INTERFACE);
	irq     = config >> 4 & 0x0f;
	dma     = config & 0x0f;

        if (high == 0x10 && low == 0xb8 && (chip == 0xf1 || chip == 0xf2)) { 
                MESSAGE("SMC IrDA Controller found; IrCC version %d.%d, "
			"port 0x%03x, dma=%d, irq=%d\n",
			chip & 0x0f, version, iobase, dma, irq);
	} else
		return -ENODEV;

	/* Power on device */
	outb(0x00, iobase+IRCC_MASTER);

	return config;
}

/*
 * Function ircc_change_speed (self, baud)
 *
 *    Change the speed of the device
 *
 */
static void ircc_change_speed(void *priv, __u32 speed)
{
	int iobase, ir_mode, ctrl, fast; 
	struct ircc_cb *self = (struct ircc_cb *) priv;
	struct net_device *dev;

	IRDA_DEBUG(0, __FUNCTION__ "\n");

	ASSERT(self != NULL, return;);

	dev = self->netdev;
	iobase = self->io.fir_base;

	/* Update accounting for new speed */
	self->io.speed = speed;

	outb(IRCC_MASTER_RESET, iobase+IRCC_MASTER);
	outb(0x00, iobase+IRCC_MASTER);

	switch (speed) {
	default:
		IRDA_DEBUG(0, __FUNCTION__ "(), unknown baud rate of %d\n", 
			   speed);
		/* FALLTHROUGH */
	case 9600:
	case 19200:
	case 38400:
	case 57600:
	case 115200:		
		ir_mode = IRCC_CFGA_IRDA_SIR_A;
		ctrl = 0;
		fast = 0;
		break;
	case 576000:		
		ir_mode = IRCC_CFGA_IRDA_HDLC;
		ctrl = IRCC_CRC;
		fast = 0;
		IRDA_DEBUG(0, __FUNCTION__ "(), handling baud of 576000\n");
		break;
	case 1152000:
		ir_mode = IRCC_CFGA_IRDA_HDLC;
		ctrl = IRCC_1152 | IRCC_CRC;
		fast = 0;
		IRDA_DEBUG(0, __FUNCTION__ "(), handling baud of 1152000\n");
		break;
	case 4000000:
		ir_mode = IRCC_CFGA_IRDA_4PPM;
		ctrl = IRCC_CRC;
		fast = IRCC_LCR_A_FAST;
		IRDA_DEBUG(0, __FUNCTION__ "(), handling baud of 4000000\n");
		break;
	}
	
	register_bank(iobase, 0);
	outb(0, iobase+IRCC_IER);
	outb(IRCC_MASTER_INT_EN, iobase+IRCC_MASTER);

	/* Make special FIR init if necessary */
	if (speed > 115200) {
		irport_stop(self->irport);

		/* Install FIR transmit handler */
		dev->hard_start_xmit = &ircc_hard_xmit;

		/* 
		 * Don't know why we have to do this, but FIR interrupts 
		 * stops working if we remove it.
		 */
		/* outb(UART_MCR_OUT2, self->io.sir_base + UART_MCR); */

		/* Be ready for incomming frames */
		ircc_dma_receive(self, iobase);
	} else {
		/* Install SIR transmit handler */
		dev->hard_start_xmit = &irport_hard_xmit;
		irport_start(self->irport);
		
	        IRDA_DEBUG(0, __FUNCTION__ 
			   "(), using irport to change speed to %d\n", speed);
		irport_change_speed(self->irport, speed);
	}	

	register_bank(iobase, 1);
	outb(((inb(iobase+IRCC_SCE_CFGA) & 0x87) | ir_mode), 
	     iobase+IRCC_SCE_CFGA);

#ifdef SMC_669 /* Uses pin 88/89 for Rx/Tx */
	outb(((inb(iobase+IRCC_SCE_CFGB) & 0x3f) | IRCC_CFGB_MUX_COM), 
	     iobase+IRCC_SCE_CFGB);
#else	
	outb(((inb(iobase+IRCC_SCE_CFGB) & 0x3f) | IRCC_CFGB_MUX_IR),
	     iobase+IRCC_SCE_CFGB);
#endif	
	(void) inb(iobase+IRCC_FIFO_THRESHOLD);
	outb(64, iobase+IRCC_FIFO_THRESHOLD);
	
	register_bank(iobase, 4);
	outb((inb(iobase+IRCC_CONTROL) & 0x30) | ctrl, iobase+IRCC_CONTROL);
	
	register_bank(iobase, 0);
	outb(fast, iobase+IRCC_LCR_A);
	
	netif_start_queue(dev);
}

/*
 * Function ircc_hard_xmit (skb, dev)
 *
 *    Transmit the frame!
 *
 */
static int ircc_hard_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct irport_cb *irport;
	struct ircc_cb *self;
	unsigned long flags;
	__u32 speed;
	int iobase;
	int mtt;

	irport = (struct irport_cb *) dev->priv;
	self = (struct ircc_cb *) irport->priv;
	ASSERT(self != NULL, return 0;);

	iobase = self->io.fir_base;

	netif_stop_queue(dev);

	/* Check if we need to change the speed after this frame */
	if ((speed = irda_get_speed(skb)) != self->io.speed) {
		/* Check for empty frame */
		if (!skb->len) {
			ircc_change_speed(self, speed); 
			return 0;
		} else
			self->new_speed = speed;
	}
	
	spin_lock_irqsave(&self->lock, flags);

	memcpy(self->tx_buff.head, skb->data, skb->len);

	self->tx_buff.len = skb->len;
	self->tx_buff.data = self->tx_buff.head;
	
	mtt = irda_get_mtt(skb);	
	if (mtt) {
		int bofs;

		/* 
		 * Compute how many BOFs (STA or PA's) we need to waste the
		 * min turn time given the speed of the link.
		 */
		bofs = mtt * (self->io.speed / 1000) / 8000;
		if (bofs > 4095)
			bofs = 4095;

		ircc_dma_xmit(self, iobase, bofs);
	} else {
		/* Transmit frame */
		ircc_dma_xmit(self, iobase, 0);
	}
	spin_unlock_irqrestore(&self->lock, flags);
	dev_kfree_skb(skb);

	return 0;
}

/*
 * Function ircc_dma_xmit (self, iobase)
 *
 *    Transmit data using DMA
 *
 */
static void ircc_dma_xmit(struct ircc_cb *self, int iobase, int bofs)
{
	__u8 ctrl;

	IRDA_DEBUG(2, __FUNCTION__ "\n");
#if 0	
	/* Disable Rx */
	register_bank(iobase, 0);
	outb(0x00, iobase+IRCC_LCR_B);
#endif
	register_bank(iobase, 1);
	outb(inb(iobase+IRCC_SCE_CFGB) & ~IRCC_CFGB_DMA_ENABLE, 
	     iobase+IRCC_SCE_CFGB);

	self->io.direction = IO_XMIT;

	/* Set BOF additional count for generating the min turn time */
	register_bank(iobase, 4);
	outb(bofs & 0xff, iobase+IRCC_BOF_COUNT_LO);
	ctrl = inb(iobase+IRCC_CONTROL) & 0xf0;
	outb(ctrl | ((bofs >> 8) & 0x0f), iobase+IRCC_BOF_COUNT_HI);

	/* Set max Tx frame size */
	outb(self->tx_buff.len >> 8, iobase+IRCC_TX_SIZE_HI);
	outb(self->tx_buff.len & 0xff, iobase+IRCC_TX_SIZE_LO);

	/* Setup DMA controller (must be done after enabling chip DMA) */
	setup_dma(self->io.dma, self->tx_buff.data, self->tx_buff.len, 
		  DMA_TX_MODE);

	outb(UART_MCR_OUT2, self->io.sir_base + UART_MCR);
	/* Enable burst mode chip Tx DMA */
	register_bank(iobase, 1);
	outb(inb(iobase+IRCC_SCE_CFGB) | IRCC_CFGB_DMA_ENABLE |
	     IRCC_CFGB_DMA_BURST, iobase+IRCC_SCE_CFGB);

	/* Enable interrupt */
	outb(IRCC_MASTER_INT_EN, iobase+IRCC_MASTER);
	register_bank(iobase, 0);
	outb(IRCC_IER_ACTIVE_FRAME | IRCC_IER_EOM, iobase+IRCC_IER);

	/* Enable transmit */
	outb(IRCC_LCR_B_SCE_TRANSMIT|IRCC_LCR_B_SIP_ENABLE, iobase+IRCC_LCR_B);
}

/*
 * Function ircc_dma_xmit_complete (self)
 *
 *    The transfer of a frame in finished. This function will only be called 
 *    by the interrupt handler
 *
 */
static void ircc_dma_xmit_complete(struct ircc_cb *self, int iobase)
{
	IRDA_DEBUG(2, __FUNCTION__ "\n");
#if 0
	/* Disable Tx */
	register_bank(iobase, 0);
	outb(0x00, iobase+IRCC_LCR_B);
#endif
	register_bank(self->io.fir_base, 1);
	outb(inb(self->io.fir_base+IRCC_SCE_CFGB) & ~IRCC_CFGB_DMA_ENABLE,
	     self->io.fir_base+IRCC_SCE_CFGB);

	/* Check for underrrun! */
	register_bank(iobase, 0);
	if (inb(iobase+IRCC_LSR) & IRCC_LSR_UNDERRUN) {
		self->irport->stats.tx_errors++;
		self->irport->stats.tx_fifo_errors++;

		/* Reset error condition */
		register_bank(iobase, 0);
		outb(IRCC_MASTER_ERROR_RESET, iobase+IRCC_MASTER);
		outb(0x00, iobase+IRCC_MASTER);
	} else {
		self->irport->stats.tx_packets++;
		self->irport->stats.tx_bytes +=  self->tx_buff.len;
	}

	/* Check if it's time to change the speed */
	if (self->new_speed) {
		ircc_change_speed(self, self->new_speed);		
		self->new_speed = 0;
	}

	netif_wake_queue(self->netdev);
}

/*
 * Function ircc_dma_receive (self)
 *
 *    Get ready for receiving a frame. The device will initiate a DMA
 *    if it starts to receive a frame.
 *
 */
static int ircc_dma_receive(struct ircc_cb *self, int iobase) 
{	
	/* Turn off chip DMA */
	//register_bank(iobase, 1);
	//outb(inb(iobase+IRCC_SCE_CFGB) & ~IRCC_CFGB_DMA_ENABLE, 
	//     iobase+IRCC_SCE_CFGB);

	setup_dma(self->io.dma, self->rx_buff.data, self->rx_buff.truesize, 
		  DMA_RX_MODE);

	/* Set max Rx frame size */
	register_bank(iobase, 4);
	outb((2050 >> 8) & 0x0f, iobase+IRCC_RX_SIZE_HI);
	outb(2050 & 0xff, iobase+IRCC_RX_SIZE_LO);

	self->io.direction = IO_RECV;
	self->rx_buff.data = self->rx_buff.head;

	/* Setup DMA controller */
	
	/* Enable receiver */
	register_bank(iobase, 0);
	outb(IRCC_LCR_B_SCE_RECEIVE | IRCC_LCR_B_SIP_ENABLE, 
	     iobase+IRCC_LCR_B);
	
	/* Enable burst mode chip Rx DMA */
	register_bank(iobase, 1);
	outb(inb(iobase+IRCC_SCE_CFGB) | IRCC_CFGB_DMA_ENABLE | 
	     IRCC_CFGB_DMA_BURST, iobase+IRCC_SCE_CFGB);

	return 0;
}

/*
 * Function ircc_dma_receive_complete (self)
 *
 *    Finished with receiving frames
 *
 */
static void ircc_dma_receive_complete(struct ircc_cb *self, int iobase)
{
	struct sk_buff *skb;
	int len, msgcnt;

	IRDA_DEBUG(2, __FUNCTION__ "\n");
#if 0
	/* Disable Rx */
	register_bank(iobase, 0);
	outb(0x00, iobase+IRCC_LCR_B);
#endif
	register_bank(iobase, 0);
	msgcnt = inb(iobase+IRCC_LCR_B) & 0x08;

	IRDA_DEBUG(2, __FUNCTION__ ": dma count = %d\n",
		   get_dma_residue(self->io.dma));

	len = self->rx_buff.truesize - get_dma_residue(self->io.dma);
	
	/* Remove CRC */
	if (self->io.speed < 4000000)
		len -= 2;
	else
		len -= 4;

	if ((len < 2) || (len > 2050)) {
		WARNING(__FUNCTION__ "(), bogus len=%d\n", len);
		return;
	}
	IRDA_DEBUG(2, __FUNCTION__ ": msgcnt = %d, len=%d\n", msgcnt, len);

	skb = dev_alloc_skb(len+1);
	if (!skb)  {
		WARNING(__FUNCTION__ "(), memory squeeze, dropping frame.\n");
		return;
	}			
	/* Make sure IP header gets aligned */
	skb_reserve(skb, 1); 

	memcpy(skb_put(skb, len), self->rx_buff.data, len);
	self->irport->stats.rx_packets++;
	self->irport->stats.rx_bytes += len;

	skb->dev = self->netdev;
	skb->mac.raw  = skb->data;
	skb->protocol = htons(ETH_P_IRDA);
	netif_rx(skb);
}

/*
 * Function ircc_interrupt (irq, dev_id, regs)
 *
 *    An interrupt from the chip has arrived. Time to do some work
 *
 */
static void ircc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct irport_cb *irport;
	struct ircc_cb *self;
	int iobase, iir;

	if (dev == NULL) {
		printk(KERN_WARNING "%s: irq %d for unknown device.\n", 
		       driver_name, irq);
		return;
	}
	irport = (struct irport_cb *) dev->priv;
	ASSERT(irport != NULL, return;);
	self = (struct ircc_cb *) irport->priv;
	ASSERT(self != NULL, return;);

	/* Check if we should use the SIR interrupt handler */
	if (self->io.speed < 576000) {
		irport_interrupt(irq, dev_id, regs);
		return;
	}
	iobase = self->io.fir_base;

	spin_lock(&self->lock);	

	register_bank(iobase, 0);
	iir = inb(iobase+IRCC_IIR);

	/* Disable interrupts */
	outb(0, iobase+IRCC_IER);

	IRDA_DEBUG(2, __FUNCTION__ "(), iir = 0x%02x\n", iir);

	if (iir & IRCC_IIR_EOM) {
		if (self->io.direction == IO_RECV)
			ircc_dma_receive_complete(self, iobase);
		else
			ircc_dma_xmit_complete(self, iobase);
		
		ircc_dma_receive(self, iobase);
	}

	/* Enable interrupts again */
	register_bank(iobase, 0);
	outb(IRCC_IER_ACTIVE_FRAME|IRCC_IER_EOM, iobase+IRCC_IER);

	spin_unlock(&self->lock);
}

#if 0 /* unused */
/*
 * Function ircc_is_receiving (self)
 *
 *    Return TRUE is we are currently receiving a frame
 *
 */
static int ircc_is_receiving(struct ircc_cb *self)
{
	int status = FALSE;
	/* int iobase; */

	IRDA_DEBUG(0, __FUNCTION__ "\n");

	ASSERT(self != NULL, return FALSE;);

	IRDA_DEBUG(0, __FUNCTION__ ": dma count = %d\n",
		   get_dma_residue(self->io.dma));

	status = (self->rx_buff.state != OUTSIDE_FRAME);
	
	return status;
}
#endif /* unused */

/*
 * Function ircc_net_open (dev)
 *
 *    Start the device
 *
 */
static int ircc_net_open(struct net_device *dev)
{
	struct irport_cb *irport;
	struct ircc_cb *self;
	int iobase;

	IRDA_DEBUG(0, __FUNCTION__ "\n");
	
	ASSERT(dev != NULL, return -1;);
	irport = (struct irport_cb *) dev->priv;
	self = (struct ircc_cb *) irport->priv;

	ASSERT(self != NULL, return 0;);
	
	iobase = self->io.fir_base;

	irport_net_open(dev); /* irport allocates the irq */

	/*
	 * Always allocate the DMA channel after the IRQ,
	 * and clean up on failure.
	 */
	if (request_dma(self->io.dma, dev->name)) {
		irport_net_close(dev);

		return -EAGAIN;
	}
	
	MOD_INC_USE_COUNT;

	return 0;
}

/*
 * Function ircc_net_close (dev)
 *
 *    Stop the device
 *
 */
static int ircc_net_close(struct net_device *dev)
{
	struct irport_cb *irport;
	struct ircc_cb *self;
	int iobase;

	IRDA_DEBUG(0, __FUNCTION__ "\n");
	
	ASSERT(dev != NULL, return -1;);
	irport = (struct irport_cb *) dev->priv;
	self = (struct ircc_cb *) irport->priv;
	
	ASSERT(self != NULL, return 0;);
	
	iobase = self->io.fir_base;

	irport_net_close(dev);

	disable_dma(self->io.dma);

	free_dma(self->io.dma);

	MOD_DEC_USE_COUNT;

	return 0;
}

static void ircc_suspend(struct ircc_cb *self)
{
	MESSAGE("%s, Suspending\n", driver_name);

	if (self->io.suspended)
		return;

	ircc_net_close(self->netdev);

	self->io.suspended = 1;
}

static void ircc_wakeup(struct ircc_cb *self)
{
	unsigned long flags;

	if (!self->io.suspended)
		return;

	save_flags(flags);
	cli();

	ircc_net_open(self->netdev);
	
	restore_flags(flags);
	MESSAGE("%s, Waking up\n", driver_name);
}

static int ircc_pmproc(struct pm_dev *dev, pm_request_t rqst, void *data)
{
        struct ircc_cb *self = (struct ircc_cb*) dev->data;
        if (self) {
                switch (rqst) {
                case PM_SUSPEND:
                        ircc_suspend(self);
                        break;
                case PM_RESUME:
                        ircc_wakeup(self);
                        break;
                }
        }
	return 0;
}

#ifdef MODULE
MODULE_AUTHOR("Thomas Davis <tadavis@jps.net>");
MODULE_DESCRIPTION("SMC IrCC controller driver");
MODULE_PARM(ircc_dma, "1i");
MODULE_PARM_DESC(ircc_dma, "DMA channel");
MODULE_PARM(ircc_irq, "1i");
MODULE_PARM_DESC(ircc_irq, "IRQ line");

int init_module(void)
{
	return ircc_init();
}

void cleanup_module(void)
{
	ircc_cleanup();
}

#endif /* MODULE */
