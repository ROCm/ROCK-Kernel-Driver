/*
 * +++ THIS DRIVER IS ORPHANED AND UNSUPPORTED +++
 *
 * aic5800.c - Adaptec AIC-5800 PCI-IEEE1394 chip driver
 * Copyright (C)1999 Emanuel Pirker <epirker@edu.uni-klu.ac.at>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <asm/byteorder.h>
#include <asm/atomic.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "ieee1394.h"
#include "aic5800.h"



/// print general (card independent) information 
#define PRINT_G(level, fmt, args...) printk(level "aic5800: " fmt "\n" , ## args)
/// print card specific information 
#define PRINT(level, card, fmt, args...) printk(level "aic5800-%d: " fmt "\n" , card , ## args)

/// card array 
static struct aic5800 cards[MAX_AIC5800_CARDS];
/// holds the number of installed aic5800 cards
static int num_of_cards = 0;

static int add_card(struct pci_dev *dev);
static void remove_card(struct aic5800 *aic);
static int init_driver(void);


/*****************************************************************
 * Auxiliary functions needed to read the EEPROM
 * Daniel Minitti
 *****************************************************************/
#define SEEPDOUT        0x1
#define SEEPDIN 0x02
#define SEEPSK  0x04
#define SEEPCS  0x08
#define SEEPCYC 0x10
#define SEEPBUSY        0x20

#define CLOCK_PULSE() {\
        int cnt=200;\
        while(cnt-->0 && reg_read(aic, misc_SEEPCTL) & SEEPBUSY);\
        if (reg_read(aic, misc_SEEPCTL) & SEEPBUSY) printk("BUSY ");\
        }

static inline unsigned short read_seeprom_word(struct aic5800 *aic,
                                               int offset) 
{
        int i;
        unsigned char temp;
        unsigned char read_cmd[3] = {1,1,0};
        unsigned short rd;

        // send chip select for one clock cycle.
        reg_write(aic, misc_SEEPCTL, SEEPSK|SEEPCS);
        CLOCK_PULSE();

        // write start bit (1) & READ op-code (10b)
        for (i=0; i<sizeof(read_cmd); i++) {
                temp = SEEPCS | SEEPCYC | read_cmd[i];
                reg_write(aic, misc_SEEPCTL, temp);
                CLOCK_PULSE();
                temp = temp ^ SEEPSK;
                reg_write(aic, misc_SEEPCTL, temp);
                CLOCK_PULSE();
        }
        // write 8 bit address (MSB --> LSB)
        for (i=7; i>=0; i--) {
                temp = offset;
                temp = (temp >> i) & 1;
                temp = SEEPCS | SEEPCYC | temp;
                reg_write(aic, misc_SEEPCTL, temp);
                CLOCK_PULSE();
                temp = temp ^ SEEPSK;
                reg_write(aic, misc_SEEPCTL, temp);
                CLOCK_PULSE();
        }
        // read 16 bit (MSB --> LSB)
        rd = 0;
        for (i=0; i<=16; i++) {
                temp = SEEPCS | SEEPCYC;
                reg_write(aic, misc_SEEPCTL, temp);
                CLOCK_PULSE();
                temp = temp ^ SEEPSK;
                rd = (rd << 1) | (unsigned short)((reg_read(aic, misc_SEEPCTL)
& SEEPDIN)>>1);
                reg_write(aic, misc_SEEPCTL, temp);
                CLOCK_PULSE();
        }

        // reset chip select for the next command cycle
        reg_write(aic, misc_SEEPCTL, SEEPCYC);
        CLOCK_PULSE();
        reg_write(aic, misc_SEEPCTL, SEEPCYC | SEEPSK);
        CLOCK_PULSE();
        reg_write(aic, misc_SEEPCTL, SEEPCYC);
        CLOCK_PULSE();

        reg_write(aic, misc_SEEPCTL, 0);
        CLOCK_PULSE();

        return rd;
}

#undef DEBUG_SEEPROM

/** Read 64-bit GUID (Global Unique ID) from SEEPROM
 *
 * It works well on AHA-8945.
 * On AHA-8920 it works well only on first time, It returns ffff... on
 * the other times.
 *****************************************************************/
static unsigned long long read_guid(struct aic5800 *aic) 
{
        int i;
        unsigned long long guid;

#ifdef DEBUG_SEEPROM
        printk("\n");
        printk("SEEPCTL value = 0x%x\n", reg_read(aic, misc_SEEPCTL));
#endif

        /* read GUID */
        guid = 0;
        for (i=0x10; i<0x14; i++)
                guid = (guid << 16) | read_seeprom_word(aic,i);

#ifdef DEBUG_SEEPROM
        for (i=0; i<3; i++)
                printk("%x ", (unsigned int) read_seeprom_word(aic,i));
        printk("\nGUID = ");
        for (i=3; i>=0; i--)
                printk("%x ", (unsigned int)(guid>>(16*i))&0xffff);

        printk("\nSEEPCTL value = 0x%x\n", reg_read(aic, misc_SEEPCTL));
#endif
        return guid;
}

#undef CLOCK_PULSE()

static int aic_detect(struct hpsb_host_template *tmpl)
{
        struct hpsb_host *host;
        int i;

        init_driver();

        for (i = 0; i < num_of_cards; i++) {
                host = hpsb_get_host(tmpl, 0);
                if (host == NULL) {
                        /* simply don't init more after out of mem */
                        return i;
                }
                host->hostdata = &cards[i];
                cards[i].host = host;
        }

        return num_of_cards;
}

static int aic_devctl(struct hpsb_host *host, enum devctl_cmd cmd, int arg)
{
    struct aic5800 *aic = host->hostdata;
    int retval = 0;
    unsigned long flags;
    struct hpsb_packet *packet, *lastpacket;

    switch (cmd) {
    case RESET_BUS:
	reg_write(aic, misc_PhyControl, 0x00004140 );
	break;

    case GET_CYCLE_COUNTER:
	arg = reg_read(aic, misc_CycleTimer);
	break;
	
    case SET_CYCLE_COUNTER:
	reg_write(aic, misc_CycleTimer, arg);
	break;
	
    case SET_BUS_ID:
	reg_clear_bits(aic, misc_NodeID, 0xFFC0);
	reg_set_bits(aic, misc_NodeID, (arg<<6));
	break;
	
    case ACT_CYCLE_MASTER:
	if (arg) {
	    /* enable cycleMaster */
	    reg_set_bits(aic, misc_Control, 0x20000);
	} else {
	    /* disable cycleMaster */
	    reg_clear_bits(aic, misc_Control, 0x20000);
	};
	break;
	
    case CANCEL_REQUESTS:
	spin_lock_irqsave(&aic->async_queue_lock, flags);
	/* stop any chip activity */
	reg_write( aic, AT_ChannelControl, 0x80000000);
        packet = aic->async_queue;
	aic->async_queue = NULL;                
	spin_unlock_irqrestore(&aic->async_queue_lock, flags);

        while (packet != NULL) {
            lastpacket = packet;
            packet = packet->xnext;
            hpsb_packet_sent(host, lastpacket, ACKX_ABORTED);
        }

	break;

    case MODIFY_USAGE:
        if (arg) {
            MOD_INC_USE_COUNT;
        } else {
            MOD_DEC_USE_COUNT;
        }
        break;

#if 0
   case DEBUG_DUMPINFO:
      PRINT(KERN_INFO, aic->id, AIC5800_DRIVER_NAME);
      PRINT(KERN_INFO, aic->id, "  Register MMIO base: 0x%p\n", 
	    aic->registers); 
      PRINT(KERN_INFO, aic->id, "  NodeID: 0x%x\n", 
	    reg_read(aic, misc_NodeID) ); 
      PRINT(KERN_INFO,aic->id, "  #Intr: %lu  BusResets: %lu\n",
	      aic->NumInterrupts, aic->NumBusResets); 
      PRINT(KERN_INFO, aic->id, "  TxPackets: %lu    RxPackets: %lu\n",
	      aic->TxPackets, aic->RxPackets); 
      PRINT(KERN_INFO,aic->id, "  TxRdy: %lu  ATErr: %lu HdrErr: %lu TcodeErr: %lu SendRej: %lu\n",
	    aic->TxRdy, aic->ATError, aic->HdrErr, 
	    aic->TCodeErr, aic->SendRej); 
      break;
#endif
	
    default:
	PRINT(KERN_ERR, aic->id, "unknown devctl command %d", cmd);
	retval = -1;
    }
    
    return retval;
    
}

/** Initialize the host adapter chip and corresponding data
    structures. We reset the chip, enable transmitter, receiver,
    the physical DMA units, cycle timer, cycle source, reception
    of selfid packets and initialize several other registers. */
static int aic_initialize(struct hpsb_host *host)
{
    int i;
    struct aic5800 *aic = host->hostdata;

    /* Reset data structures */
    aic->async_queue = NULL;
    spin_lock_init(&aic->async_queue_lock);

    /* Reset the chip */
    reg_write( aic, misc_Reset, 0x37);
    udelay(10); // FIXME
    reg_write( aic, misc_Reset, 0);
   
    /* Enable Transmitter/Receiver, enable physDMA,
     * enable CycleTimer, cycleSource */
    reg_write( aic, misc_Control, 0x82050003);

    /* Enable reception of SelfID packets */
    reg_set_bits(aic, misc_PacketControl, 0x20);

    reg_write(aic, AT_InterruptSelect, 0x00F0001);
    reg_write(aic, AT_BranchSelect, 0x0100010);
    reg_write(aic, AT_WaitSelect, 0x00F0001);
    reg_write(aic, misc_ATRetries, reg_read(aic, misc_ATRetries) | 0x7);

    /* initialize AR DMA */

    /* unset run bit */
    reg_write( aic, AR_ChannelControl, 0x80000000);
	    
    /* here we should have 0 iterations because of the code
       in the DmaAR handler. However, to be sure we do it */
    i = 0;
    while (reg_read(aic, AR_ChannelStatus) & 0x400) {
	i++;
	if (i>100000) {
	    PRINT(KERN_ERR, aic->id, 
		  "Huh! Can't set AR_ChannelControl... card can not receive!");
	    break;
	}
    }
    
    (aic->AR_program)->control = ( DMA_CMD_INPUTLAST | DMA_KEY_STREAM0 
				   | DMA_INTR_ALWAYS | DMA_BRANCH_ALWAYS) 
	+ AIC5800_ARFIFO_SIZE;
    (aic->AR_program)->address = virt_to_bus(aic->rcv_page);
    (aic->AR_program)->branchAddress = virt_to_bus(aic->AR_program);
    (aic->AR_program)->status = AIC5800_ARFIFO_SIZE;
    
    (aic->AR_program+1)->control = DMA_CMD_STOP;
    (aic->AR_program+1)->address = 0;
    (aic->AR_program+1)->branchAddress = 0;
    (aic->AR_program+1)->status = 0;
    
    reg_write( aic, AR_CommandPtr, (u32) virt_to_bus(aic->AR_program)); 
    reg_write( aic, AR_ChannelControl, 0x80008000);
   
    /* Enable Interrupts */
    reg_write(aic, misc_InterruptClear, 0xFFFFFFFF);
    reg_write(aic, misc_InterruptMask, 0xFFFFFFFF);
    /*reg_write(aic, misc_InterruptMask, 0x00F1F03F);*/

    return 1;
}

static void aic_release(struct hpsb_host *host)
{
    struct aic5800 *aic;
        
    if (host != NULL) {
	aic = host->hostdata;
	remove_card(aic);
    } 
}

/* This must be called with the async_queue_lock held. */
static void send_next_async(struct aic5800 *aic)
{
    int i;
    struct hpsb_packet *packet = aic->async_queue;

    /* stop the channel program if it's still running */
    reg_write( aic, AT_ChannelControl, 0x80000000);
    
    /* re-format packet header for AIC-5800 chip */
    packet->header[1] = (packet->header[1] & 0xFFFF) | 
	(packet->header[0] & 0xFFFF0000);
    packet->header[0] = (packet->header[0] & 0xFFFF);

#ifndef __BIG_ENDIAN
    /* Packet must be byte-swapped in non-big-endian environments, 
     * see AIC-5800 specification...
     */
    { u32 i;
    for ( i = 0 ; i < packet->header_size/sizeof(u32) ; i++ ) 
	packet->header[i] = cpu_to_be32( packet->header[i] ); 
    for ( i = 0 ; i < packet->data_size/sizeof(u32) ; i++ ) 
	packet->data[i] = cpu_to_be32( packet->data[i] ); 
    }
    
#endif
    
    /* typically we use only a few iterations here */
    i = 0;
    while (reg_read(aic, AT_ChannelStatus) & 0x400) {
	i++;
	if (i>5000) {
	    PRINT(KERN_ERR, aic->id, 
		  "runaway loop 1 in send_next_async() - bailing out...");
	    break;
	};
    };
    
    /* set data buffer address and packet length */
    memset(aic->AT_program, 0, MAX_AT_PROGRAM_SIZE * sizeof(struct dma_cmd));
    
    if (packet->data_size) {
	aic->AT_program[0].control = ( DMA_CMD_OUTPUTMORE | DMA_KEY_STREAM0 ) + 
	    packet -> header_size;
	aic->AT_program[0].address = virt_to_bus( packet->header );
	aic->AT_program[1].control = ( DMA_CMD_OUTPUTLAST | DMA_KEY_STREAM0 
				       | DMA_INTR_ALWAYS ) 
	    + packet -> data_size;
	aic->AT_program[1].address = virt_to_bus( packet->data );
	
	aic->AT_program[2].control = DMA_CMD_STOP;
	
    } else {
	aic->AT_program[0].control = ( DMA_CMD_OUTPUTLAST | DMA_INTR_ALWAYS | 
				       DMA_KEY_STREAM0 ) + 
	    packet -> header_size;
	aic->AT_program[0].address = virt_to_bus( packet->header );
	
	aic->AT_program[1].control = DMA_CMD_STOP;
    };
    
    /* set program start address */
    reg_write(aic, AT_CommandPtr, (unsigned int) virt_to_bus(aic->AT_program));

    /* typically we use only a few iterations here */
    i = 0;
    while (reg_read(aic, AT_CommandPtr) != (unsigned int) 
	   virt_to_bus(aic->AT_program)) {
		i++;
	if (i>5000) {
	    PRINT(KERN_ERR, aic->id, 
		  "runaway loop 2 in send_next_async() - bailing out...");
	    break;
	};
    };
     
    /* run program */
    reg_write( aic, AT_ChannelControl, 0x80008000);
}


static int aic_transmit(struct hpsb_host *host, struct hpsb_packet *packet)
{
    struct aic5800 *aic = host->hostdata;
    struct hpsb_packet *p;
    unsigned long flags;

    if (packet->data_size >= 4096) {
	PRINT(KERN_ERR, aic->id, "transmit packet data too big (%d)",
	      packet->data_size);
	return 0;
    }

    packet->xnext = NULL;
    
    spin_lock_irqsave(&aic->async_queue_lock, flags);

    if (aic->async_queue == NULL) {
	aic->async_queue = packet;
	send_next_async(aic);
    } else {
	p = aic->async_queue;
	while (p->xnext != NULL) {
	    p = p->xnext;
	}
	
	p->xnext = packet;
    }
    
    spin_unlock_irqrestore(&aic->async_queue_lock, flags);

    return 1;
}

static int get_phy_reg(struct aic5800 *aic, int addr)
{
        int retval;
        int i = 0;

	/* sanity check */
        if (addr > 15) {
                PRINT(KERN_ERR, aic->id, __FUNCTION__
                      ": PHY register address %d out of range", addr);
                return -1;
        }

	/* request data from PHY */
        reg_write(aic, misc_PhyControl, LINK_PHY_READ | LINK_PHY_ADDR(addr));

	/* read data from PhyControl register */
	/* note that we have to wait until the register is updated */
        do {
                retval = reg_read(aic, misc_PhyControl);

                if (i > 10000) {
                        PRINT(KERN_ERR, aic->id, __FUNCTION__ 
                              ": runaway loop, aborting");
                        retval = -1;
                        break;
                }
                i++;
        } while ((retval & 0xf000000) != LINK_PHY_RADDR(addr));

	/* we don't want a PhyInt interrupt */
        reg_write(aic, misc_InterruptClear, INT_PhyInt);

        if (retval != -1) {
                return ((retval & 0xff0000)>>16);
        } else {
                return -1;
        }
}

static quadlet_t generate_own_selfid(struct aic5800 *aic, int phyid)
{
    quadlet_t lsid;
    char phyreg[7];
    int i;

    for (i = 1; i < 7; i++) {
	phyreg[i] = get_phy_reg(aic, i);
    }

    /* Standard PHY register map */
    lsid = 0x80400000 | (phyid << 24);
    lsid |= (phyreg[1] & 0x3f) << 16; /* gap count */
    lsid |= (phyreg[2] & 0xc0) << 8; /* max speed */
    lsid |= (phyreg[6] & 0x01) << 11; /* contender (phy dep) */
    lsid |= (phyreg[6] & 0x10) >> 3; /* initiated reset */

    for (i = 0; i < (phyreg[2] & 0x1f); i++) { /* ports */
	if (phyreg[3 + i] & 0x4) {
	    lsid |= (((phyreg[3 + i] & 0x8) | 0x10) >> 3)
						    << (6 - i*2);
	} else {
	    lsid |= 1 << (6 - i*2);
	}
    }

    return lsid;
};

/* moved out to make interrupt routine more readable */
inline static void handle_selfid(struct aic5800 *aic, struct hpsb_host *host,
                          int phyid, int isroot, size_t size)
{
    quadlet_t *q = aic->rcv_page;
    quadlet_t lsid;

    /* we need our own self-id packet */
    lsid = generate_own_selfid(aic, phyid);

    /* unconnected state? only begin and end marker in rcv_page */
    if (size==8) {
	hpsb_selfid_received(host, lsid);
    }

    /* process buffer... AIC's FIFO often contains some strangenesses */
    while (size > 0) {
	if (q[0] == 0xe0) {
	    /* marker */
	    q += 1;
	    size -= 4;
	    continue;
	};
	if (q[0] == 0x1) {
	    /* marker */
	    q += 1;
	    size -= 4;
	    break;
	};

	if (q[0] == ~q[1]) {
	    /* correct self-id */

	    if ((q[0] & 0x3f800000) == ((phyid + 1) << 24)) {
		/* its our turn now! */
		//PRINT(KERN_INFO, 
		//      aic->id, "selfid packet 0x%x included", lsid);
		
		hpsb_selfid_received(host, lsid);
	    }

	    //PRINT(KERN_INFO, aic->id, "selfid packet 0x%x rcvd", q[0]);
	    hpsb_selfid_received(host, q[0]);
	    q += 2;
	    size -= 8;
	    continue;
	};
    }

    /* if we are root, our self-id packet is last */
    if (isroot && phyid != 0) {
	hpsb_selfid_received(host, lsid);
    }

    hpsb_selfid_complete(host, phyid, isroot);
}

static void aic_irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{
    struct aic5800 *aic = (struct aic5800 *)dev_id;
    struct hpsb_host *host = aic->host;
    quadlet_t *q = aic->rcv_page;
 
    int phyid = -1, isroot = 0;

    u32 interruptEvent = reg_read(aic, misc_InterruptEvents);
    reg_write(aic, misc_InterruptClear, interruptEvent);

    //printk("InterruptEvent 0x%x\n", interruptEvent);
    if ( (interruptEvent & 0x3f) == 0x3f ) {
	PRINT(KERN_INFO, aic->id, "Dma Engine Error");
    };

    if ( interruptEvent & INT_DmaAT ) {
	if (aic->AT_program[0].status & 0xFFFF) 
	    PRINT(KERN_INFO, aic->id, "AT: could not transfer %d bytes", 
		  aic->AT_program[0].status & 0xFFFF);
    };

    if ( interruptEvent & INT_PhyInt) {
	PRINT(KERN_INFO, aic->id, "PhyInt");
    };

    if ( interruptEvent & INT_DmaAR ) {
	int rcv_bytes;
	int i;

	/* we calculate the number of received bytes from the
	   residual count field */
	rcv_bytes = AIC5800_ARFIFO_SIZE - (aic->AR_program->status & 0xFFFF);

	//PRINT(KERN_INFO, aic->id, "AR_status 0x%x, %d bytes read", aic->AR_program->status, rcv_bytes);

	if ((aic->AR_program->status & 0x84000000) 
	    && (aic->AR_program->status & 0xFFFF) >= 8 ) {

#ifndef __BIG_ENDIAN 
	    /* we have to do byte-swapping on non-bigendian architectures */
	    for (i=0; i< (rcv_bytes / sizeof(quadlet_t)); i++) {
		*q = be32_to_cpu(*q);
		q++;
	    };
	    q = aic->rcv_page;
#endif

	    if (*q == 0xe0) {
		phyid = reg_read(aic, misc_NodeID);
		isroot = phyid & 0x800000;
		phyid = phyid & 0x3F;
		handle_selfid(aic, host, phyid, isroot, rcv_bytes);
	    } else {
		hpsb_packet_received(host, aic->rcv_page, rcv_bytes, 0);
	    };
	} else {
	    PRINT(KERN_ERR, aic->id, 
		  "AR DMA program status value 0x%x is incorrect!",
		  aic->AR_program->status);
	};
    }
    if ( interruptEvent & INT_BusReset ) {
	PRINT(KERN_INFO, aic->id, "bus reset occured");
	if (!host->in_bus_reset) {
	    hpsb_bus_reset(host);
	}
	reg_set_bits(aic, misc_Control, 0x1);
	aic->NumBusResets++;
    };

    if (interruptEvent & INT_RcvData ) { 
	aic->RxPackets++;
    };

    if (interruptEvent & INT_TxRdy) { 
	/* async packet sent - transmitter ready */
	u32 ack;
	struct hpsb_packet *packet;

	if (aic->async_queue) {

	    spin_lock(&aic->async_queue_lock);

	    
	    ack = reg_read(aic, AT_ChannelStatus) & 0xF;
	    
	    packet = aic->async_queue;
	    aic->async_queue = packet->xnext;
	    
	    if (aic->async_queue != NULL) {
		send_next_async(aic);
	    }
	    spin_unlock(&aic->async_queue_lock);
	    PRINT(KERN_INFO,aic->id,"packet sent with ack code %d",ack);
	    hpsb_packet_sent(host, packet, ack);	    
	} // else 
	    //PRINT(KERN_INFO,aic->id,"packet sent without async_queue (self-id?)");

	aic->TxRdy++;
    };
    if (interruptEvent & INT_ATError ) { 
	PRINT(KERN_INFO,aic->id,"ATError");
	aic->ATError++;
    };
    if (interruptEvent & INT_SendRej ) { 
	aic->SendRej++;
    };
    if (interruptEvent & INT_HdrErr ) { 
	aic->HdrErr++;
    };
    if (interruptEvent & INT_TCodeErr ) { 
	PRINT(KERN_INFO,aic->id,"TCodeErr");
	aic->TCodeErr++;
    };

    aic->NumInterrupts++;

}

inline static void * quadquadalign(void *buf)
{
  if ((unsigned int) buf % 0x10 != 0) {
	return (void *)(((unsigned int)buf + 0x10) & 0xFFFFFFF0);
	} else {
	return buf;
  };
}

static int add_card(struct pci_dev *dev)
{
#define FAIL(fmt, args...) do {\
        PRINT_G(KERN_ERR, fmt , ## args); \
        num_of_cards--; \
        remove_card(aic); \
        return 1; } while (0)

        struct aic5800 *aic; /* shortcut to currently handled device */
        unsigned long page;

	if (pci_enable_device(dev))
		return 1;

        if (num_of_cards == MAX_AIC5800_CARDS) {
                PRINT_G(KERN_WARNING, "cannot handle more than %d cards.  "
                        "Adjust MAX_AIC5800_CARDS in aic5800.h.",
                        MAX_AIC5800_CARDS);
                return 1;
        }

        aic = &cards[num_of_cards++];

        aic->id = num_of_cards-1;
        aic->dev = dev;

        if (!request_irq(dev->irq, aic_irq_handler, SA_SHIRQ,
                         AIC5800_DRIVER_NAME, aic)) {
                PRINT(KERN_INFO, aic->id, "allocated interrupt %d", dev->irq);
        } else {
                FAIL("failed to allocate shared interrupt %d", dev->irq);
        }

        page = get_free_page(GFP_KERNEL);
        if (page != 0) {
                aic->rcv_page = phys_to_virt(page);
        } else {
                FAIL("failed to allocate receive buffer");
        }

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,13)
        aic->registers = ioremap_nocache(dev->base_address[0],
                                          AIC5800_REGSPACE_SIZE);
#else
        aic->registers = ioremap_nocache(dev->resource[0].start,
                                          AIC5800_REGSPACE_SIZE);
#endif

        if (aic->registers == NULL) {
                FAIL("failed to remap registers - card not accessible");
        }

        PRINT(KERN_INFO, aic->id, "remapped memory space reg 0x%p", 
	      aic->registers);

        aic->pbuf = kmalloc(AIC5800_PBUF_SIZE, GFP_KERNEL);

        if (!aic->pbuf) {
                FAIL("failed to allocate program buffer");
        }

	aic->AT_program = quadquadalign(aic->pbuf);
	aic->AT_program[2].control = DMA_CMD_STOP;

	aic->AR_program = aic->AT_program + MAX_AT_PROGRAM_SIZE * 
	    sizeof(struct dma_cmd);

        return 0;
#undef FAIL
}

static void remove_card(struct aic5800 *aic)
{
    /* Disable interrupts of this controller */
    reg_write(aic, misc_InterruptMask, 0);
    /* Free AR buffer */
    free_page(virt_to_phys(aic->rcv_page));
    /* Free channel program buffer */
    kfree(aic->pbuf);
    /* Free interrupt request */
    free_irq(aic->dev->irq, aic);
    /* Unmap register space */
    iounmap(aic->registers);
}

static int init_driver()
{
        struct pci_dev *dev = NULL;
        int success = 0;

        if (num_of_cards) {
                PRINT_G(KERN_DEBUG, __PRETTY_FUNCTION__ " called again");
                return 0;
        }

        while ((dev = pci_find_device(PCI_VENDOR_ID_ADAPTEC,
                                      PCI_DEVICE_ID_ADAPTEC_5800, dev)) 
               != NULL) {
                if (add_card(dev) == 0) {
                        success = 1;
                }
        }

        if (success == 0) {
                PRINT_G(KERN_WARNING, "no operable AIC-5800 based cards found");
                return -ENXIO;
        }

        return 0;
}

/** Prepare our local CSR ROM. This is done by using the software-stored
    ROM and inserting the GUID read from the EEPROM */
static size_t get_aic_rom(struct hpsb_host *host, const quadlet_t **ptr)
{
    struct aic5800 *aic = host -> hostdata;
    u64 guid;

    /* Read the GUID from the card's EEPROM and put it into the right
       place in the CONFIG ROM. */
    guid = read_guid(aic);
    aic5800_csr_rom[15] = (u32) (guid >> 32);
    aic5800_csr_rom[16] = (u32) (guid & 0xFFFF);

    *ptr = aic5800_csr_rom;

    return sizeof(aic5800_csr_rom);
}

struct hpsb_host_template *get_aic_template(void)
{
        static struct hpsb_host_template tmpl;
        static int initialized = 0;

        if (!initialized) {
                /* Initialize by field names so that a template structure
                 * reorganization does not influence this code. */
                tmpl.name = "aic5800";
                
                tmpl.detect_hosts = aic_detect;
                tmpl.initialize_host = aic_initialize;
                tmpl.release_host = aic_release;
                tmpl.get_rom = get_aic_rom;
                tmpl.transmit_packet = aic_transmit;
                tmpl.devctl = aic_devctl;

                initialized = 1;
        }

        return &tmpl;
}

#ifdef MODULE

/* EXPORT_NO_SYMBOLS; */

MODULE_AUTHOR("Emanuel Pirker <epirker@edu.uni-klu.ac.at>");
MODULE_DESCRIPTION("Adaptec AIC-5800 PCI-to-IEEE1394 controller driver");
MODULE_SUPPORTED_DEVICE("aic5800");

void cleanup_module(void)
{
        hpsb_unregister_lowlevel(get_aic_template());
        PRINT_G(KERN_INFO, "removed " AIC5800_DRIVER_NAME " module");
}

int init_module(void)
{
        if (hpsb_register_lowlevel(get_aic_template())) {
                PRINT_G(KERN_ERR, "registering failed");
                return -ENXIO;
        } else {
                return 0;
        }
}

#endif /* MODULE */
