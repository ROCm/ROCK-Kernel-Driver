/*
 * ohci1394.c - driver for OHCI 1394 boards
 * Copyright (C)1999,2000 Sebastien Rougeaux <sebastien.rougeaux@anu.edu.au>
 *                        Gord Peters <GordPeters@smarttech.com>
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

/*
 * Things known to be working:
 * . Async Request Transmit
 * . Async Response Receive
 * . Async Request Receive
 * . Async Response Transmit
 * . Iso Receive
 * . DMA mmap for iso receive
 * 
 * Things not implemented:
 * . Iso Transmit
 * . DMA error recovery
 *
 * Things to be fixed:
 * . Config ROM
 *
 * Known bugs:
 * . Self-id are sometimes not received properly 
 *   if card is initialized with no other nodes 
 *   on the bus
 * . Apple PowerBook detected but not working yet
 */

/* 
 * Acknowledgments:
 *
 * Adam J Richter <adam@yggdrasil.com>
 *  . Use of pci_class to find device
 * Andreas Tobler <toa@pop.agri.ch>
 *  . Updated proc_fs calls
 * Emilie Chung	<emilie.chung@axis.com>
 *  . Tip on Async Request Filter
 * Pascal Drolet <pascal.drolet@informission.ca>
 *  . Various tips for optimization and functionnalities
 * Robert Ficklin <rficklin@westengineering.com>
 *  . Loop in irq_handler
 * James Goodwin <jamesg@Filanet.com>
 *  . Various tips on initialization, self-id reception, etc.
 * Albrecht Dress <ad@mpifr-bonn.mpg.de>
 *  . Apple PowerBook detection
 * Daniel Kobras <daniel.kobras@student.uni-tuebingen.de>
 *  . Reset the board properly before leaving + misc cleanups
 * Leon van Stuivenberg <leonvs@iae.nl>
 *  . Bug fixes
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <asm/byteorder.h>
#include <asm/atomic.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/tqueue.h>
#include <linux/delay.h>

#include <asm/pgtable.h>
#include <asm/page.h>
#include <linux/sched.h>
#include <asm/segment.h>
#include <linux/types.h>
#include <linux/wrapper.h>
#include <linux/vmalloc.h>
#include <linux/init.h>

#include "ieee1394.h"
#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "ohci1394.h"

#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
#define OHCI1394_DEBUG
#endif

#ifdef DBGMSG
#undef DBGMSG
#endif

#ifdef OHCI1394_DEBUG
#define DBGMSG(card, fmt, args...) \
printk(KERN_INFO "ohci1394_%d: " fmt "\n" , card , ## args)
#else
#define DBGMSG(card, fmt, args...)
#endif

/* print general (card independent) information */
#define PRINT_G(level, fmt, args...) \
printk(level "ohci1394: " fmt "\n" , ## args)

/* print card specific information */
#define PRINT(level, card, fmt, args...) \
printk(level "ohci1394_%d: " fmt "\n" , card , ## args)

#define FAIL(fmt, args...) \
	PRINT_G(KERN_ERR, fmt , ## args); \
	  num_of_cards--; \
	    remove_card(ohci); \
	      return 1;

#if USE_DEVICE

int supported_chips[][2] = {
	{ PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_OHCI1394_LV22 },
	{ PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_OHCI1394_LV23 },
	{ PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_OHCI1394_LV26 },
	{ PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_OHCI1394_PCI4450 },
	{ PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_OHCI1394 },
	{ PCI_VENDOR_ID_SONY, PCI_DEVICE_ID_SONY_CXD3222 },
	{ PCI_VENDOR_ID_NEC, PCI_DEVICE_ID_NEC_UPD72862 },
	{ PCI_VENDOR_ID_NEC, PCI_DEVICE_ID_NEC_UPD72870 },
	{ PCI_VENDOR_ID_NEC, PCI_DEVICE_ID_NEC_UPD72871 },
	{ PCI_VENDOR_ID_APPLE, PCI_DEVICE_ID_APPLE_UNI_N_FW },
	{ PCI_VENDOR_ID_AL, PCI_DEVICE_ID_ALI_OHCI1394_M5251 },
	{ PCI_VENDOR_ID_LUCENT, PCI_DEVICE_ID_LUCENT_FW323 },
	{ -1, -1 }
};

#else

#define PCI_CLASS_FIREWIRE_OHCI     ((PCI_CLASS_SERIAL_FIREWIRE << 8) | 0x10)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
static struct pci_device_id ohci1394_pci_tbl[] __initdata = {
	{
		class: 		PCI_CLASS_FIREWIRE_OHCI,
		class_mask: 	0x00ffffff,
		vendor:		PCI_ANY_ID,
		device:		PCI_ANY_ID,
		subvendor:	PCI_ANY_ID,
		subdevice:	PCI_ANY_ID,
	},
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, ohci1394_pci_tbl);
#endif

#endif /* USE_DEVICE */

MODULE_PARM(attempt_root,"i");
static int attempt_root = 0;

static struct ti_ohci cards[MAX_OHCI1394_CARDS];
static int num_of_cards = 0;

static int add_card(struct pci_dev *dev);
static void remove_card(struct ti_ohci *ohci);
static int init_driver(void);
static void dma_trm_bh(void *data);
static void dma_rcv_bh(void *data);
static void dma_trm_reset(struct dma_trm_ctx *d);

/***********************************
 * IEEE-1394 functionality section *
 ***********************************/

#if 0 /* not needed at this time */
static int get_phy_reg(struct ti_ohci *ohci, int addr) 
{
	int timeout=10000;
	static quadlet_t r;

	if ((addr < 1) || (addr > 15)) {
		PRINT(KERN_ERR, ohci->id, __FUNCTION__
		      ": PHY register address %d out of range", addr);
		return -EFAULT;
	}

	spin_lock(&ohci->phy_reg_lock);

	/* initiate read request */
	reg_write(ohci, OHCI1394_PhyControl, 
		  ((addr<<8)&0x00000f00) | 0x00008000);

	/* wait */
	while (!(reg_read(ohci, OHCI1394_PhyControl)&0x80000000) && timeout)
		timeout--;


	if (!timeout) {
		PRINT(KERN_ERR, ohci->id, "get_phy_reg timeout !!!\n");
		spin_unlock(&ohci->phy_reg_lock);
		return -EFAULT;
	}
	r = reg_read(ohci, OHCI1394_PhyControl);
  
	spin_unlock(&ohci->phy_reg_lock);
     
	return (r&0x00ff0000)>>16;
}

static int set_phy_reg(struct ti_ohci *ohci, int addr, unsigned char data) {
	int timeout=10000;
	u32 r;

	if ((addr < 1) || (addr > 15)) {
		PRINT(KERN_ERR, ohci->id, __FUNCTION__
		      ": PHY register address %d out of range", addr);
		return -EFAULT;
	}

	r = ((addr<<8)&0x00000f00) | 0x00004000 | ((u32)data & 0x000000ff);

	spin_lock(&ohci->phy_reg_lock);

	reg_write(ohci, OHCI1394_PhyControl, r);

	/* wait */
	while (!(reg_read(ohci, OHCI1394_PhyControl)&0x80000000) && timeout)
		timeout--;

	spin_unlock(&ohci->phy_reg_lock);

	if (!timeout) {
		PRINT(KERN_ERR, ohci->id, "set_phy_reg timeout !!!\n");
		return -EFAULT;
	}

	return 0;
}
#endif /* unneeded functions */

inline static int handle_selfid(struct ti_ohci *ohci, struct hpsb_host *host,
				int phyid, int isroot)
{
	quadlet_t *q = ohci->selfid_buf_cpu;
	quadlet_t self_id_count=reg_read(ohci, OHCI1394_SelfIDCount);
	size_t size;
	quadlet_t lsid;

	/* Self-id handling seems much easier than for the aic5800 chip.
	   All the self-id packets, including this device own self-id,
	   should be correctly arranged in the selfid buffer at this
	   stage */

	/* Check status of self-id reception */
	if ((self_id_count&0x80000000) || 
	    ((self_id_count&0x00FF0000) != (q[0]&0x00FF0000))) {
		PRINT(KERN_ERR, ohci->id, 
		      "Error in reception of self-id packets"
		      "Self-id count: %08x q[0]: %08x",
		      self_id_count, q[0]);

		/*
		 * Tip by James Goodwin <jamesg@Filanet.com>:
		 * We had an error, generate another bus reset in response. 
		 * TODO. Actually read the current value in the phy before 
		 * generating a bus reset (read modify write). This way
		 * we don't stomp any current gap count settings, etc.
		 */
		if (ohci->self_id_errors<OHCI1394_MAX_SELF_ID_ERRORS) {
			reg_write(ohci, OHCI1394_PhyControl, 0x000041ff);      
			ohci->self_id_errors++;
		}
		else {
			PRINT(KERN_ERR, ohci->id, 
			      "Timeout on self-id error reception");
		}
		return -1;
	}
	
	size = ((self_id_count&0x00001FFC)>>2) - 1;
	q++;

	while (size > 0) {
		if (q[0] == ~q[1]) {
			PRINT(KERN_INFO, ohci->id, "selfid packet 0x%x rcvd", 
			      q[0]);
			hpsb_selfid_received(host, cpu_to_be32(q[0]));
			if (((q[0]&0x3f000000)>>24)==phyid) {
				lsid=q[0];
				PRINT(KERN_INFO, ohci->id, 
				      "This node self-id is 0x%08x", lsid);
			}
		} else {
			PRINT(KERN_ERR, ohci->id,
			      "inconsistent selfid 0x%x/0x%x", q[0], q[1]);
		}
		q += 2;
		size -= 2;
	}

	PRINT(KERN_INFO, ohci->id, "calling self-id complete");

	hpsb_selfid_complete(host, phyid, isroot);
	return 0;
}

static int ohci_detect(struct hpsb_host_template *tmpl)
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

static int ohci_soft_reset(struct ti_ohci *ohci) {
	int timeout=10000;

	reg_write(ohci, OHCI1394_HCControlSet, 0x00010000);
  
	while ((reg_read(ohci, OHCI1394_HCControlSet)&0x00010000) && timeout) 
		timeout--;
	if (!timeout) {
		PRINT(KERN_ERR, ohci->id, "soft reset timeout !!!");
		return -EFAULT;
	}
	else PRINT(KERN_INFO, ohci->id, "soft reset finished");
	return 0;
}

static int run_context(struct ti_ohci *ohci, int reg, char *msg)
{
	u32 nodeId;

	/* check that the node id is valid */
	nodeId = reg_read(ohci, OHCI1394_NodeID);
	if (!(nodeId&0x80000000)) {
		PRINT(KERN_ERR, ohci->id, 
		      "Running dma failed because Node ID not valid");
		return -1;
	}

	/* check that the node number != 63 */
	if ((nodeId&0x3f)==63) {
		PRINT(KERN_ERR, ohci->id, 
		      "Running dma failed because Node ID == 63");
		return -1;
	}
	
	/* Run the dma context */
	reg_write(ohci, reg, 0x8000);
	
	if (msg) PRINT(KERN_INFO, ohci->id, "%s", msg);
	
	return 0;
}

/* Generate the dma receive prgs and start the context */
static void initialize_dma_rcv_ctx(struct dma_rcv_ctx *d)
{
	struct ti_ohci *ohci = (struct ti_ohci*)(d->ohci);
	int i;

	ohci1394_stop_context(ohci, d->ctrlClear, NULL);

	for (i=0; i<d->num_desc; i++) {
		
		/* end of descriptor list? */
		if ((i+1) < d->num_desc) {
			d->prg_cpu[i]->control = (0x283C << 16) | d->buf_size;
			d->prg_cpu[i]->branchAddress =
				(d->prg_bus[i+1] & 0xfffffff0) | 0x1;
		} else {
			d->prg_cpu[i]->control = (0x283C << 16) | d->buf_size;
			d->prg_cpu[i]->branchAddress =
				d->prg_bus[0] & 0xfffffff0;
		}

		d->prg_cpu[i]->address = d->buf_bus[i];
		d->prg_cpu[i]->status = d->buf_size;
	}

        d->buf_ind = 0;
        d->buf_offset = 0;

	/* Tell the controller where the first AR program is */
	reg_write(ohci, d->cmdPtr, d->prg_bus[0] | 0x1);

	/* Run AR context */
	reg_write(ohci, d->ctrlSet, 0x00008000);

	PRINT(KERN_INFO, ohci->id, "Receive DMA ctx=%d initialized", d->ctx);
}

/* Initialize the dma transmit context */
static void initialize_dma_trm_ctx(struct dma_trm_ctx *d)
{
	struct ti_ohci *ohci = (struct ti_ohci*)(d->ohci);

	/* Stop the context */
	ohci1394_stop_context(ohci, d->ctrlClear, NULL);

        d->prg_ind = 0;
	d->sent_ind = 0;
	d->free_prgs = d->num_desc;
        d->branchAddrPtr = NULL;
	d->fifo_first = NULL;
	d->fifo_last = NULL;
	d->pending_first = NULL;
	d->pending_last = NULL;

	PRINT(KERN_INFO, ohci->id, "AT dma ctx=%d initialized", d->ctx);
}

/* Count the number of available iso contexts */
static int get_nb_iso_ctx(struct ti_ohci *ohci, int reg)
{
	int i,ctx=0;
	u32 tmp;

	reg_write(ohci, reg, 0xffffffff);
	tmp = reg_read(ohci, reg);
	
	DBGMSG(ohci->id,"Iso contexts reg: %08x implemented: %08x", reg, tmp);

	/* Count the number of contexts */
	for(i=0; i<32; i++) {
	    	if(tmp & 1) ctx++;
		tmp >>= 1;
	}
	return ctx;
}

/* Global initialization */
static int ohci_initialize(struct hpsb_host *host)
{
	struct ti_ohci *ohci=host->hostdata;
	int retval, i;

	spin_lock_init(&ohci->phy_reg_lock);
  
	/*
	 * Tip by James Goodwin <jamesg@Filanet.com>:
	 * We need to add delays after the soft reset, setting LPS, and
	 * enabling our link. This might fixes the self-id reception 
	 * problem at initialization.
	 */ 

	/* Soft reset */
	if ((retval=ohci_soft_reset(ohci))<0) return retval;

	/* 
	 * Delay after soft reset to make sure everything has settled
	 * down (sanity)
	 */
	mdelay(100);    
  
	/* Set Link Power Status (LPS) */
	reg_write(ohci, OHCI1394_HCControlSet, 0x00080000);

	/*
	 * Delay after setting LPS in order to make sure link/phy
	 * communication is established
	 */
	mdelay(100);   

	/* Set the bus number */
	reg_write(ohci, OHCI1394_NodeID, 0x0000ffc0);

	/* Enable posted writes */
	reg_write(ohci, OHCI1394_HCControlSet, 0x00040000);

	/* Clear link control register */
	reg_write(ohci, OHCI1394_LinkControlClear, 0xffffffff);
  
	/* Enable cycle timer and cycle master */
	reg_write(ohci, OHCI1394_LinkControlSet, 0x00300000);

	/* Clear interrupt registers */
	reg_write(ohci, OHCI1394_IntMaskClear, 0xffffffff);
	reg_write(ohci, OHCI1394_IntEventClear, 0xffffffff);

	/* Set up self-id dma buffer */
	reg_write(ohci, OHCI1394_SelfIDBuffer, ohci->selfid_buf_bus);

	/* enable self-id dma */
	reg_write(ohci, OHCI1394_LinkControlSet, 0x00000200);

	/* Set the configuration ROM mapping register */
	reg_write(ohci, OHCI1394_ConfigROMmap, ohci->csr_config_rom_bus);

	/* Set bus options */
	reg_write(ohci, OHCI1394_BusOptions, 
		  cpu_to_be32(ohci->csr_config_rom_cpu[2]));

#if 0	
	/* Write the GUID into the csr config rom */
	ohci->csr_config_rom_cpu[3] = 
		be32_to_cpu(reg_read(ohci, OHCI1394_GUIDHi));
	ohci->csr_config_rom_cpu[4] = 
		be32_to_cpu(reg_read(ohci, OHCI1394_GUIDLo));
#endif

	/* Write the config ROM header */
	reg_write(ohci, OHCI1394_ConfigROMhdr, 
		  cpu_to_be32(ohci->csr_config_rom_cpu[0]));

	ohci->max_packet_size = 
		1<<(((reg_read(ohci, OHCI1394_BusOptions)>>12)&0xf)+1);
	PRINT(KERN_INFO, ohci->id, "max packet size = %d bytes",
	      ohci->max_packet_size);

	/* Don't accept phy packets into AR request context */ 
	reg_write(ohci, OHCI1394_LinkControlClear, 0x00000400);

	/* Initialize IR dma */
	ohci->nb_iso_rcv_ctx = 
		get_nb_iso_ctx(ohci, OHCI1394_IsoRecvIntMaskSet);
	PRINT(KERN_INFO, ohci->id, "%d iso receive contexts available",
	      ohci->nb_iso_rcv_ctx);
	for (i=0;i<ohci->nb_iso_rcv_ctx;i++) {
		reg_write(ohci, OHCI1394_IsoRcvContextControlClear+32*i,
			  0xffffffff);
		reg_write(ohci, OHCI1394_IsoRcvContextMatch+32*i, 0);
		reg_write(ohci, OHCI1394_IsoRcvCommandPtr+32*i, 0);
	}

	/* Set bufferFill, isochHeader, multichannel for IR context */
	reg_write(ohci, OHCI1394_IsoRcvContextControlSet, 0xd0000000);
			
	/* Set the context match register to match on all tags */
	reg_write(ohci, OHCI1394_IsoRcvContextMatch, 0xf0000000);

	/* Clear the interrupt mask */
	reg_write(ohci, OHCI1394_IsoRecvIntMaskClear, 0xffffffff);
	reg_write(ohci, OHCI1394_IsoRecvIntEventClear, 0xffffffff);

	/* Initialize IT dma */
	ohci->nb_iso_xmit_ctx = 
		get_nb_iso_ctx(ohci, OHCI1394_IsoXmitIntMaskSet);
	PRINT(KERN_INFO, ohci->id, "%d iso transmit contexts available",
	      ohci->nb_iso_xmit_ctx);
	for (i=0;i<ohci->nb_iso_xmit_ctx;i++) {
		reg_write(ohci, OHCI1394_IsoXmitContextControlClear+32*i,
			  0xffffffff);
		reg_write(ohci, OHCI1394_IsoXmitCommandPtr+32*i, 0);
	}
	
	/* Clear the interrupt mask */
	reg_write(ohci, OHCI1394_IsoXmitIntMaskClear, 0xffffffff);
	reg_write(ohci, OHCI1394_IsoXmitIntEventClear, 0xffffffff);

	/* Clear the multi channel mask high and low registers */
	reg_write(ohci, OHCI1394_IRMultiChanMaskHiClear, 0xffffffff);
	reg_write(ohci, OHCI1394_IRMultiChanMaskLoClear, 0xffffffff);

	/* Initialize AR dma */
	initialize_dma_rcv_ctx(ohci->ar_req_context);
	initialize_dma_rcv_ctx(ohci->ar_resp_context);

	/* Initialize AT dma */
	initialize_dma_trm_ctx(ohci->at_req_context);
	initialize_dma_trm_ctx(ohci->at_resp_context);

	/* Initialize IR dma */
	initialize_dma_rcv_ctx(ohci->ir_context);

	/* Set up isoRecvIntMask to generate interrupts for context 0
	   (thanks to Michael Greger for seeing that I forgot this) */
	reg_write(ohci, OHCI1394_IsoRecvIntMaskSet, 0x00000001);

	/* 
	 * Accept AT requests from all nodes. This probably 
	 * will have to be controlled from the subsystem
	 * on a per node basis.
	 */
	reg_write(ohci,OHCI1394_AsReqFilterHiSet, 0x80000000);

	/* Specify AT retries */
	reg_write(ohci, OHCI1394_ATRetries, 
		  OHCI1394_MAX_AT_REQ_RETRIES |
		  (OHCI1394_MAX_AT_RESP_RETRIES<<4) |
		  (OHCI1394_MAX_PHYS_RESP_RETRIES<<8));

#ifndef __BIG_ENDIAN
	reg_write(ohci, OHCI1394_HCControlClear, 0x40000000);
#else
	reg_write(ohci, OHCI1394_HCControlSet, 0x40000000);
#endif

	/* Enable interrupts */
	reg_write(ohci, OHCI1394_IntMaskSet, 
		  OHCI1394_masterIntEnable | 
		  OHCI1394_phyRegRcvd | 
		  OHCI1394_busReset | 
		  OHCI1394_selfIDComplete |
		  OHCI1394_RSPkt |
		  OHCI1394_RQPkt |
		  OHCI1394_ARRS |
		  OHCI1394_ARRQ |
		  OHCI1394_respTxComplete |
		  OHCI1394_reqTxComplete |
		  OHCI1394_isochRx |
		  OHCI1394_isochTx
		);

	/* Enable link */
	reg_write(ohci, OHCI1394_HCControlSet, 0x00020000);

	return 1;
}

static void ohci_remove(struct hpsb_host *host)
{
	struct ti_ohci *ohci;
        
	if (host != NULL) {
		ohci = host->hostdata;
		remove_card(ohci);
	}
}

/* 
 * Insert a packet in the AT DMA fifo and generate the DMA prg
 * FIXME: rewrite the program in order to accept packets crossing
 *        page boundaries.
 *        check also that a single dma descriptor doesn't cross a 
 *        page boundary.
 */
static void insert_packet(struct ti_ohci *ohci,
			  struct dma_trm_ctx *d, struct hpsb_packet *packet)
{
	u32 cycleTimer;
	int idx = d->prg_ind;

	d->prg_cpu[idx]->begin.address = 0;
	d->prg_cpu[idx]->begin.branchAddress = 0;
	if (d->ctx==1) {
		/* 
		 * For response packets, we need to put a timeout value in
		 * the 16 lower bits of the status... let's try 1 sec timeout 
		 */ 
		cycleTimer = reg_read(ohci, OHCI1394_IsochronousCycleTimer);
		d->prg_cpu[idx]->begin.status = 
			(((((cycleTimer>>25)&0x7)+1)&0x7)<<13) | 
			((cycleTimer&0x01fff000)>>12);

		DBGMSG(ohci->id, "cycleTimer: %08x timeStamp: %08x",
		       cycleTimer, d->prg_cpu[idx]->begin.status);
	}
	else 
		d->prg_cpu[idx]->begin.status = 0;

        if (packet->type == raw) {
		d->prg_cpu[idx]->data[0] = OHCI1394_TCODE_PHY<<4;
		d->prg_cpu[idx]->data[1] = packet->header[0];
		d->prg_cpu[idx]->data[2] = packet->header[1];
        }
        else {
		d->prg_cpu[idx]->data[0] = packet->speed_code<<16 |
			(packet->header[0] & 0xFFFF);
		d->prg_cpu[idx]->data[1] = (packet->header[1] & 0xFFFF) | 
			(packet->header[0] & 0xFFFF0000);
		d->prg_cpu[idx]->data[2] = packet->header[2];
		d->prg_cpu[idx]->data[3] = packet->header[3];
        }

	if (packet->data_size) { /* block transmit */
		d->prg_cpu[idx]->begin.control = OUTPUT_MORE_IMMEDIATE | 0x10;
		d->prg_cpu[idx]->end.control = OUTPUT_LAST | packet->data_size;
		/* 
		 * FIXME: check that the packet data buffer
		 * do not cross a page boundary 
		 */
		if (cross_bound((unsigned long)packet->data, 
				packet->data_size)>0) {
			/* FIXME: do something about it */
			PRINT(KERN_ERR, ohci->id, __FUNCTION__
			      ": packet data addr: %p size %d bytes "
			      "cross page boundary", 
			      packet->data, packet->data_size);
		}

		d->prg_cpu[idx]->end.address =
			pci_map_single(ohci->dev, packet->data,
				       packet->data_size, PCI_DMA_TODEVICE);
		d->prg_cpu[idx]->end.branchAddress = 0;
		d->prg_cpu[idx]->end.status = 0;
		if (d->branchAddrPtr) 
			*(d->branchAddrPtr) = d->prg_bus[idx] | 0x3;
		d->branchAddrPtr = &(d->prg_cpu[idx]->end.branchAddress);
	}
	else { /* quadlet transmit */
                if (packet->type == raw)
                        d->prg_cpu[idx]->begin.control =
				OUTPUT_LAST_IMMEDIATE|(packet->header_size+4);
                else
                        d->prg_cpu[idx]->begin.control =
                                OUTPUT_LAST_IMMEDIATE|packet->header_size;

		if (d->branchAddrPtr) 
			*(d->branchAddrPtr) = d->prg_bus[idx] | 0x2;
		d->branchAddrPtr = &(d->prg_cpu[idx]->begin.branchAddress);
	}
	d->free_prgs--;

	/* queue the packet in the appropriate context queue */
	if (d->fifo_last) {
		d->fifo_last->xnext = packet;
		d->fifo_last = packet;
	}
	else {
		d->fifo_first = packet;
		d->fifo_last = packet;
	}
	d->prg_ind = (d->prg_ind+1)%d->num_desc;
}

/*
 * This function fills the AT FIFO with the (eventual) pending packets
 * and runs or wake up the AT DMA prg if necessary.
 * The function MUST be called with the d->lock held.
 */ 
static int dma_trm_flush(struct ti_ohci *ohci, struct dma_trm_ctx *d)
{
	int idx,z;

	if (d->pending_first == NULL || d->free_prgs == 0) 
		return 0;

	idx = d->prg_ind;
	z = (d->pending_first->data_size) ? 3 : 2;

	/* insert the packets into the at dma fifo */
	while (d->free_prgs>0 && d->pending_first) {
		insert_packet(ohci, d, d->pending_first);
		d->pending_first = d->pending_first->xnext;
	}
	if (d->pending_first == NULL) 
		d->pending_last = NULL;
	else
		PRINT(KERN_INFO, ohci->id, 
		      "AT DMA FIFO ctx=%d full... waiting",d->ctx);

	/* Is the context running ? (should be unless it is 
	   the first packet to be sent in this context) */
	if (!(reg_read(ohci, d->ctrlSet) & 0x8000)) {
		DBGMSG(ohci->id,"Starting AT DMA ctx=%d",d->ctx);
		reg_write(ohci, d->cmdPtr, d->prg_bus[idx]|z);
		run_context(ohci, d->ctrlSet, NULL);
	}
	else {
		DBGMSG(ohci->id,"Waking AT DMA ctx=%d",d->ctx);
		/* wake up the dma context if necessary */
		if (!(reg_read(ohci, d->ctrlSet) & 0x400))
			reg_write(ohci, d->ctrlSet, 0x1000);
	}
	return 1;
}

/*
 * Transmission of an async packet
 */
static int ohci_transmit(struct hpsb_host *host, struct hpsb_packet *packet)
{
	struct ti_ohci *ohci = host->hostdata;
	struct dma_trm_ctx *d;
	unsigned char tcode;
	unsigned long flags;

	if (packet->data_size > ohci->max_packet_size) {
		PRINT(KERN_ERR, ohci->id, 
		      "transmit packet size = %d too big",
		      packet->data_size);
		return 0;
	}
	packet->xnext = NULL;

	/* Decide wether we have a request or a response packet */
	tcode = (packet->header[0]>>4)&0xf;
	if (tcode & 0x02) d = ohci->at_resp_context;
	else d = ohci->at_req_context;

	spin_lock_irqsave(&d->lock,flags);

	/* queue the packet for later insertion into to dma fifo */
	if (d->pending_last) {
		d->pending_last->xnext = packet;
		d->pending_last = packet;
	}
	else {
		d->pending_first = packet;
		d->pending_last = packet;
	}
	
	dma_trm_flush(ohci, d);

	spin_unlock_irqrestore(&d->lock,flags);

	return 1;
}

static int ohci_devctl(struct hpsb_host *host, enum devctl_cmd cmd, int arg)
{
	struct ti_ohci *ohci = host->hostdata;
	int retval = 0;
	unsigned long flags;

	switch (cmd) {
	case RESET_BUS:
		/*
		 * FIXME: this flag might be necessary in some case
		 */
		PRINT(KERN_INFO, ohci->id, "resetting bus on request%s",
		      ((host->attempt_root || attempt_root) ? 
		       " and attempting to become root" : ""));
		reg_write(ohci, OHCI1394_PhyControl, 
			  (host->attempt_root || attempt_root) ? 
			  0x000041ff : 0x0000417f);
		break;

	case GET_CYCLE_COUNTER:
		retval = reg_read(ohci, OHCI1394_IsochronousCycleTimer);
		break;
	
	case SET_CYCLE_COUNTER:
		reg_write(ohci, OHCI1394_IsochronousCycleTimer, arg);
		break;
	
	case SET_BUS_ID:
		PRINT(KERN_ERR, ohci->id, "devctl command SET_BUS_ID err");
		break;

	case ACT_CYCLE_MASTER:
		if (arg) {
			/* check if we are root and other nodes are present */
			u32 nodeId = reg_read(ohci, OHCI1394_NodeID);
			if ((nodeId & (1<<30)) && (nodeId & 0x3f)) {
				/*
				 * enable cycleTimer, cycleMaster
				 */
				DBGMSG(ohci->id, "Cycle master enabled");
				reg_write(ohci, OHCI1394_LinkControlSet, 
					  0x00300000);
			}
		} else {
			/* disable cycleTimer, cycleMaster, cycleSource */
			reg_write(ohci, OHCI1394_LinkControlClear, 0x00700000);
		}
		break;

	case CANCEL_REQUESTS:
		DBGMSG(ohci->id, "Cancel request received");
		dma_trm_reset(ohci->at_req_context);
		dma_trm_reset(ohci->at_resp_context);
		break;

	case MODIFY_USAGE:
                if (arg) {
                        MOD_INC_USE_COUNT;
                } else {
                        MOD_DEC_USE_COUNT;
                }
                break;

	case ISO_LISTEN_CHANNEL:
        {
		u64 mask;

		if (arg<0 || arg>63) {
			PRINT(KERN_ERR, ohci->id, __FUNCTION__
			      "IS0_LISTEN_CHANNEL channel %d out of range", 
			      arg);
			return -EFAULT;
		}

		mask = (u64)0x1<<arg;
		
                spin_lock_irqsave(&ohci->IR_channel_lock, flags);

		if (ohci->ISO_channel_usage & mask) {
			PRINT(KERN_ERR, ohci->id, __FUNCTION__
			      "IS0_LISTEN_CHANNEL channel %d already used", 
			      arg);
			spin_unlock_irqrestore(&ohci->IR_channel_lock, flags);
			return -EFAULT;
		}
		
		ohci->ISO_channel_usage |= mask;

		if (arg>31) 
			reg_write(ohci, OHCI1394_IRMultiChanMaskHiSet, 
				  1<<(arg-32));			
		else
			reg_write(ohci, OHCI1394_IRMultiChanMaskLoSet, 
				  1<<arg);			

                spin_unlock_irqrestore(&ohci->IR_channel_lock, flags);
                DBGMSG(ohci->id, "listening enabled on channel %d", arg);
                break;
        }
	case ISO_UNLISTEN_CHANNEL:
        {
		u64 mask;

		if (arg<0 || arg>63) {
			PRINT(KERN_ERR, ohci->id, __FUNCTION__
			      "IS0_UNLISTEN_CHANNEL channel %d out of range", 
			      arg);
			return -EFAULT;
		}

		mask = (u64)0x1<<arg;
		
                spin_lock_irqsave(&ohci->IR_channel_lock, flags);

		if (!(ohci->ISO_channel_usage & mask)) {
			PRINT(KERN_ERR, ohci->id, __FUNCTION__
			      "IS0_UNLISTEN_CHANNEL channel %d not used", 
			      arg);
			spin_unlock_irqrestore(&ohci->IR_channel_lock, flags);
			return -EFAULT;
		}
		
		ohci->ISO_channel_usage &= ~mask;

		if (arg>31) 
			reg_write(ohci, OHCI1394_IRMultiChanMaskHiClear, 
				  1<<(arg-32));			
		else
			reg_write(ohci, OHCI1394_IRMultiChanMaskLoClear, 
				  1<<arg);			

                spin_unlock_irqrestore(&ohci->IR_channel_lock, flags);
                DBGMSG(ohci->id, "listening disabled on channel %d", arg);
                break;
        }
	default:
		PRINT_G(KERN_ERR, "ohci_devctl cmd %d not implemented yet",
			cmd);
		break;
	}
	return retval;
}

/***************************************
 * IEEE-1394 functionality section END *
 ***************************************/


/********************************************************
 * Global stuff (interrupt handler, init/shutdown code) *
 ********************************************************/

static void dma_trm_reset(struct dma_trm_ctx *d)
{
	struct ti_ohci *ohci;
	unsigned long flags;
        struct hpsb_packet *nextpacket;

	if (d==NULL) {
		PRINT_G(KERN_ERR, "dma_trm_reset called with NULL arg");
		return;
	}
	ohci = (struct ti_ohci *)(d->ohci);
	ohci1394_stop_context(ohci, d->ctrlClear, NULL);

	spin_lock_irqsave(&d->lock,flags);

	/* is there still any packet pending in the fifo ? */
	while(d->fifo_first) {
		PRINT(KERN_INFO, ohci->id, 
		      "AT dma reset ctx=%d, aborting transmission", 
		      d->ctx);
                nextpacket = d->fifo_first->xnext;
		hpsb_packet_sent(ohci->host, d->fifo_first, ACKX_ABORTED);
		d->fifo_first = nextpacket;
	}
	d->fifo_first = d->fifo_last = NULL;

	/* is there still any packet pending ? */
	while(d->pending_first) {
		PRINT(KERN_INFO, ohci->id, 
		      "AT dma reset ctx=%d, aborting transmission", 
		      d->ctx);
                nextpacket = d->pending_first->xnext;
		hpsb_packet_sent(ohci->host, d->pending_first, 
				 ACKX_ABORTED);
		d->pending_first = nextpacket;
	}
	d->pending_first = d->pending_last = NULL;
	
	d->branchAddrPtr=NULL;
	d->sent_ind = d->prg_ind;
	d->free_prgs = d->num_desc;
	spin_unlock_irqrestore(&d->lock,flags);
}

static void ohci_irq_handler(int irq, void *dev_id,
                             struct pt_regs *regs_are_unused)
{
	quadlet_t event,node_id;
	struct ti_ohci *ohci = (struct ti_ohci *)dev_id;
	struct hpsb_host *host = ohci->host;
	int phyid = -1, isroot = 0;
	int timeout = 255;

	do {
		/* read the interrupt event register */
		event=reg_read(ohci, OHCI1394_IntEventClear);

		if (!event) return;

		DBGMSG(ohci->id, "IntEvent: %08x",event);

		/* clear the interrupt event register */
		reg_write(ohci, OHCI1394_IntEventClear, event);

		if (event & OHCI1394_busReset) {
			if (!host->in_bus_reset) {
				PRINT(KERN_INFO, ohci->id, "Bus reset");
				
				/* Wait for the AT fifo to be flushed */
				dma_trm_reset(ohci->at_req_context);
				dma_trm_reset(ohci->at_resp_context);

				/* Subsystem call */
				hpsb_bus_reset(ohci->host);
				
				ohci->NumBusResets++;
			}
		}
		/*
		 * Problem: How can I ensure that the AT bottom half will be
		 * executed before the AR bottom half (both events may have
		 * occured within a single irq event)
		 * Quick hack: just launch it within the IRQ handler
		 */
		if (event & OHCI1394_reqTxComplete) { 
			struct dma_trm_ctx *d = ohci->at_req_context;
			DBGMSG(ohci->id, "Got reqTxComplete interrupt "
			       "status=0x%08X", reg_read(ohci, d->ctrlSet));
			if (reg_read(ohci, d->ctrlSet) & 0x800)
				ohci1394_stop_context(ohci, d->ctrlClear, 
					     "reqTxComplete");
			else
				dma_trm_bh((void *)d);
		}
		if (event & OHCI1394_respTxComplete) { 
			struct dma_trm_ctx *d = ohci->at_resp_context;
			DBGMSG(ohci->id, "Got respTxComplete interrupt "
			       "status=0x%08X", reg_read(ohci, d->ctrlSet));
			if (reg_read(ohci, d->ctrlSet) & 0x800)
				ohci1394_stop_context(ohci, d->ctrlClear, 
					     "respTxComplete");
			else
				dma_trm_bh((void *)d);
		}
		if (event & OHCI1394_RQPkt) {
			struct dma_rcv_ctx *d = ohci->ar_req_context;
			DBGMSG(ohci->id, "Got RQPkt interrupt status=0x%08X",
			       reg_read(ohci, d->ctrlSet));
			if (reg_read(ohci, d->ctrlSet) & 0x800)
				ohci1394_stop_context(ohci, d->ctrlClear, "RQPkt");
			else {
#if IEEE1394_USE_BOTTOM_HALVES
				queue_task(&d->task, &tq_immediate);
				mark_bh(IMMEDIATE_BH);
#else
				dma_rcv_bh((void *)d);
#endif
			}
		}
		if (event & OHCI1394_RSPkt) {
			struct dma_rcv_ctx *d = ohci->ar_resp_context;
			DBGMSG(ohci->id, "Got RSPkt interrupt status=0x%08X",
			       reg_read(ohci, d->ctrlSet));
			if (reg_read(ohci, d->ctrlSet) & 0x800)
				ohci1394_stop_context(ohci, d->ctrlClear, "RSPkt");
			else {
#if IEEE1394_USE_BOTTOM_HALVES
				queue_task(&d->task, &tq_immediate);
				mark_bh(IMMEDIATE_BH);
#else
				dma_rcv_bh((void *)d);
#endif
			}
		}
		if (event & OHCI1394_isochRx) {
			quadlet_t isoRecvIntEvent;
			struct dma_rcv_ctx *d = ohci->ir_context;
			isoRecvIntEvent = 
				reg_read(ohci, OHCI1394_IsoRecvIntEventSet);
			reg_write(ohci, OHCI1394_IsoRecvIntEventClear,
				  isoRecvIntEvent);
			DBGMSG(ohci->id, "Got isochRx interrupt "
			       "status=0x%08X isoRecvIntEvent=%08x", 
			       reg_read(ohci, d->ctrlSet), isoRecvIntEvent);
			if (isoRecvIntEvent & 0x1) {
				if (reg_read(ohci, d->ctrlSet) & 0x800)
					ohci1394_stop_context(ohci, d->ctrlClear, 
						     "isochRx");
				else {
#if IEEE1394_USE_BOTTOM_HALVES
					queue_task(&d->task, &tq_immediate);
					mark_bh(IMMEDIATE_BH);
#else
					dma_rcv_bh((void *)d);
#endif
				}
			}
			if (ohci->video_tmpl) 
				ohci->video_tmpl->irq_handler(ohci->id,
							      isoRecvIntEvent,
							      0);
		}
		if (event & OHCI1394_isochTx) {
			quadlet_t isoXmitIntEvent;
			isoXmitIntEvent = 
				reg_read(ohci, OHCI1394_IsoXmitIntEventSet);
			reg_write(ohci, OHCI1394_IsoXmitIntEventClear,
				  isoXmitIntEvent);
			DBGMSG(ohci->id, "Got isochTx interrupt");
			if (ohci->video_tmpl) 
				ohci->video_tmpl->irq_handler(ohci->id, 0,
							      isoXmitIntEvent);
		}
		if (event & OHCI1394_selfIDComplete) {
			if (host->in_bus_reset) {
				/* 
				 * Begin Fix (JSG): Check to make sure our 
				 * node id is valid 
				 */
				node_id = reg_read(ohci, OHCI1394_NodeID); 
				if (!(node_id & 0x80000000)) {
					mdelay(1); /* phy is upset - 
						    * this happens once in 
						    * a while on hot-plugs...
						    * give it a ms to recover 
						    */
				}
				/* End Fix (JSG) */

				node_id = reg_read(ohci, OHCI1394_NodeID);
				if (node_id & 0x80000000) { /* NodeID valid */
					phyid =  node_id & 0x0000003f;
					isroot = (node_id & 0x40000000) != 0;

					PRINT(KERN_INFO, ohci->id,
					      "SelfID process finished "
					      "(phyid %d, %s)", phyid, 
					      (isroot ? "root" : "not root"));

					handle_selfid(ohci, host, 
						      phyid, isroot);
				}
				else 
					PRINT(KERN_ERR, ohci->id, 
					      "SelfID process finished but "
					      "NodeID not valid: %08X",
					      node_id);

				/* Accept Physical requests from all nodes. */
				reg_write(ohci,OHCI1394_AsReqFilterHiSet, 
					  0xffffffff);
				reg_write(ohci,OHCI1394_AsReqFilterLoSet, 
					  0xffffffff);
                       		/*
				 * Tip by James Goodwin <jamesg@Filanet.com>
				 * Turn on phys dma reception. We should
				 * probably manage the filtering somehow, 
				 * instead of blindly turning it on.
				 */
				reg_write(ohci,OHCI1394_PhyReqFilterHiSet,
					  0xffffffff);
				reg_write(ohci,OHCI1394_PhyReqFilterLoSet,
					  0xffffffff);
                        	reg_write(ohci,OHCI1394_PhyUpperBound,
					  0xffff0000);
			} 
			else PRINT(KERN_ERR, ohci->id, 
				   "self-id received outside of bus reset"
				   "sequence");
		}
		if (event & OHCI1394_phyRegRcvd) {
#if 1
			if (host->in_bus_reset) {
				PRINT(KERN_INFO, ohci->id, "PhyControl: %08X", 
				      reg_read(ohci, OHCI1394_PhyControl));
			}
			else PRINT(KERN_ERR, ohci->id, 
				   "phy reg received outside of bus reset"
				   "sequence");
#endif
		}
	} while (--timeout);

	PRINT(KERN_ERR, ohci->id, "irq_handler timeout event=0x%08x", event);
}

/* Put the buffer back into the dma context */
static void insert_dma_buffer(struct dma_rcv_ctx *d, int idx)
{
	struct ti_ohci *ohci = (struct ti_ohci*)(d->ohci);
	DBGMSG(ohci->id, "Inserting dma buf ctx=%d idx=%d", d->ctx, idx);

	d->prg_cpu[idx]->status = d->buf_size;
	d->prg_cpu[idx]->branchAddress &= 0xfffffff0;
	idx = (idx + d->num_desc - 1 ) % d->num_desc;
	d->prg_cpu[idx]->branchAddress |= 0x1;

	/* wake up the dma context if necessary */
	if (!(reg_read(ohci, d->ctrlSet) & 0x400)) {
		PRINT(KERN_INFO, ohci->id, 
		      "Waking dma cxt=%d ... processing is probably too slow",
		      d->ctx);
		reg_write(ohci, d->ctrlSet, 0x1000);
	}
}	

static int block_length(struct dma_rcv_ctx *d, int idx, 
			 quadlet_t *buf_ptr, int offset)
{
	int length=0;

	/* Where is the data length ? */
	if (offset+12>=d->buf_size) 
		length = (d->buf_cpu[(idx+1)%d->num_desc]
			  [3-(d->buf_size-offset)/4]>>16);
	else 
		length = (buf_ptr[3]>>16);
	if (length % 4) length += 4 - (length % 4);
	return length;
}

const int TCODE_SIZE[16] = {20, 0, 16, -1, 16, 20, 20, 0, 
			    -1, 0, -1, 0, -1, -1, 16, -1};

/* 
 * Determine the length of a packet in the buffer
 * Optimization suggested by Pascal Drolet <pascal.drolet@informission.ca>
 */
static int packet_length(struct dma_rcv_ctx *d, int idx, quadlet_t *buf_ptr,
int offset)
{
	unsigned char 	tcode;
	int 		length 	= -1;

	/* Let's see what kind of packet is in there */
	tcode = (buf_ptr[0] >> 4) & 0xf;

	if (d->ctx < 2) { /* Async Receive Response/Request */
		length = TCODE_SIZE[tcode];
		if (length == 0) 
			length = block_length(d, idx, buf_ptr, offset) + 20;
	}
	else if (d->ctx==2) { /* Iso receive */
		/* Assumption: buffer fill mode with header/trailer */
		length = (buf_ptr[0]>>16);
		if (length % 4) length += 4 - (length % 4);
		length+=8;
	}
	return length;
}

/* Bottom half that processes dma receive buffers */
static void dma_rcv_bh(void *data)
{
	struct dma_rcv_ctx *d = (struct dma_rcv_ctx*)data;
	struct ti_ohci *ohci = (struct ti_ohci*)(d->ohci);
	unsigned int split_left, idx, offset, rescount;
	unsigned char tcode;
	int length, bytes_left, ack;
	quadlet_t *buf_ptr;
	char *split_ptr;
	char msg[256];

	spin_lock(&d->lock);

	idx = d->buf_ind;
	offset = d->buf_offset;
	buf_ptr = d->buf_cpu[idx] + offset/4;

	rescount = d->prg_cpu[idx]->status&0xffff;
	bytes_left = d->buf_size - rescount - offset;

	while (bytes_left>0) {
		tcode = (buf_ptr[0]>>4)&0xf;
		length = packet_length(d, idx, buf_ptr, offset);

		if (length<4) { /* something is wrong */
			sprintf(msg,"unexpected tcode 0x%X in AR ctx=%d",
				tcode, d->ctx);
			ohci1394_stop_context(ohci, d->ctrlClear, msg);
			spin_unlock(&d->lock);
			return;
		}

		if ((offset+length)>d->buf_size) { /* Split packet */
			if (length>d->split_buf_size) {
				ohci1394_stop_context(ohci, d->ctrlClear,
					     "split packet size exceeded");
				d->buf_ind = idx;
				d->buf_offset = offset;
				spin_unlock(&d->lock);
				return;
			}
			if (d->prg_cpu[(idx+1)%d->num_desc]->status
			    ==d->buf_size) {
				/* other part of packet not written yet */
				/* this should never happen I think */
				/* anyway we'll get it on the next call */
				PRINT(KERN_INFO, ohci->id,
				      "Got only half a packet !!!");
				d->buf_ind = idx;
				d->buf_offset = offset;
				spin_unlock(&d->lock);
				return;
			}
			split_left = length;
			split_ptr = (char *)d->spb;
			memcpy(split_ptr,buf_ptr,d->buf_size-offset);
			split_left -= d->buf_size-offset;
			split_ptr += d->buf_size-offset;
			insert_dma_buffer(d, idx);
			idx = (idx+1) % d->num_desc;
			buf_ptr = d->buf_cpu[idx];
			offset=0;
			while (split_left >= d->buf_size) {
				memcpy(split_ptr,buf_ptr,d->buf_size);
				split_ptr += d->buf_size;
				split_left -= d->buf_size;
				insert_dma_buffer(d, idx);
				idx = (idx+1) % d->num_desc;
				buf_ptr = d->buf_cpu[idx];
			}
			if (split_left>0) {
				memcpy(split_ptr, buf_ptr, split_left);
				offset = split_left;
				buf_ptr += offset/4;
			}

			/* 
			 * We get one phy packet for each bus reset. 
			 * we know that from now on the bus topology may
			 * have changed. Just ignore it for the moment
			 */
			if (tcode != 0xE) {
				DBGMSG(ohci->id, "Split packet received from"
				       " node %d ack=0x%02X spd=%d tcode=0x%X"
				       " length=%d data=0x%08x ctx=%d",
				       (d->spb[1]>>16)&0x3f,
				       (d->spb[length/4-1]>>16)&0x1f,
				       (d->spb[length/4-1]>>21)&0x3,
				       tcode, length, d->spb[3], d->ctx);
				
				ack = (((d->spb[length/4-1]>>16)&0x1f) 
				       == 0x11) ? 1 : 0;

				hpsb_packet_received(ohci->host, d->spb, 
						     length, ack);
			}
			else 
				PRINT(KERN_INFO, ohci->id, 
				      "Got phy packet ctx=%d ... discarded",
				      d->ctx);
		}
		else {
			/* 
			 * We get one phy packet for each bus reset. 
			 * we know that from now on the bus topology may
			 * have changed. Just ignore it for the moment
			 */
			if (tcode != 0xE) {
				DBGMSG(ohci->id, "Packet received from node"
				       " %d ack=0x%02X spd=%d tcode=0x%X"
				       " length=%d data=0x%08x ctx=%d",
				       (buf_ptr[1]>>16)&0x3f,
				       (buf_ptr[length/4-1]>>16)&0x1f,
				       (buf_ptr[length/4-1]>>21)&0x3,
				       tcode, length, buf_ptr[3], d->ctx);

				ack = (((buf_ptr[length/4-1]>>16)&0x1f)
				       == 0x11) ? 1 : 0;

				hpsb_packet_received(ohci->host, buf_ptr, 
						     length, ack);
			}
			else 
				PRINT(KERN_INFO, ohci->id, 
				      "Got phy packet ctx=%d ... discarded",
				      d->ctx);
			offset += length;
			buf_ptr += length/4;
			if (offset==d->buf_size) {
				insert_dma_buffer(d, idx);
				idx = (idx+1) % d->num_desc;
				buf_ptr = d->buf_cpu[idx];
				offset=0;
			}
		}
		rescount = d->prg_cpu[idx]->status & 0xffff;
		bytes_left = d->buf_size - rescount - offset;

	}

	d->buf_ind = idx;
	d->buf_offset = offset;

	spin_unlock(&d->lock);
}

/* Bottom half that processes sent packets */
static void dma_trm_bh(void *data)
{
	struct dma_trm_ctx *d = (struct dma_trm_ctx*)data;
	struct ti_ohci *ohci = (struct ti_ohci*)(d->ohci);
	struct hpsb_packet *packet, *nextpacket;
	unsigned long flags;
	u32 ack;
        size_t datasize;

	spin_lock_irqsave(&d->lock, flags);

	if (d->fifo_first==NULL) {
#if 0
		ohci1394_stop_context(ohci, d->ctrlClear, 
			     "Packet sent ack received but queue is empty");
#endif
		spin_unlock_irqrestore(&d->lock, flags);
		return;
	}

	while (d->fifo_first) {
		packet = d->fifo_first;
                datasize = d->fifo_first->data_size;
		if (datasize)
			ack = d->prg_cpu[d->sent_ind]->end.status>>16;
		else 
			ack = d->prg_cpu[d->sent_ind]->begin.status>>16;

		if (ack==0) 
			/* this packet hasn't been sent yet*/
			break;

#ifdef OHCI1394_DEBUG
		if (datasize)
			DBGMSG(ohci->id,
			       "Packet sent to node %d tcode=0x%X tLabel="
			       "0x%02X ack=0x%X spd=%d dataLength=%d ctx=%d", 
			       (d->prg_cpu[d->sent_ind]->data[1]>>16)&0x3f,
			       (d->prg_cpu[d->sent_ind]->data[0]>>4)&0xf,
			       (d->prg_cpu[d->sent_ind]->data[0]>>10)&0x3f,
			       ack&0x1f, (ack>>5)&0x3, 
			       d->prg_cpu[d->sent_ind]->data[3]>>16,
			       d->ctx);
		else 
			DBGMSG(ohci->id,
			       "Packet sent to node %d tcode=0x%X tLabel="
			       "0x%02X ack=0x%X spd=%d data=0x%08X ctx=%d", 
			       (d->prg_cpu[d->sent_ind]->data[1]>>16)&0x3f,
			       (d->prg_cpu[d->sent_ind]->data[0]>>4)&0xf,
			       (d->prg_cpu[d->sent_ind]->data[0]>>10)&0x3f,
			       ack&0x1f, (ack>>5)&0x3, 
			       d->prg_cpu[d->sent_ind]->data[3],
			       d->ctx);
#endif		

                nextpacket = packet->xnext;
		hpsb_packet_sent(ohci->host, packet, ack&0xf);

		if (datasize)
			pci_unmap_single(ohci->dev, 
					 d->prg_cpu[d->sent_ind]->end.address,
					 datasize, PCI_DMA_TODEVICE);

		d->sent_ind = (d->sent_ind+1)%d->num_desc;
		d->free_prgs++;
		d->fifo_first = nextpacket;
	}
	if (d->fifo_first==NULL) d->fifo_last=NULL;

	dma_trm_flush(ohci, d);

	spin_unlock_irqrestore(&d->lock, flags);
}

static int free_dma_rcv_ctx(struct dma_rcv_ctx **d)
{
	int i;
	struct ti_ohci *ohci;

	if (*d==NULL) return -1;

	ohci = (struct ti_ohci *)(*d)->ohci;

	DBGMSG(ohci->id, "Freeing dma_rcv_ctx %d",(*d)->ctx);
	
	ohci1394_stop_context(ohci, (*d)->ctrlClear, NULL);

	if ((*d)->buf_cpu) {
		for (i=0; i<(*d)->num_desc; i++)
			if ((*d)->buf_cpu[i] && (*d)->buf_bus[i]) 
				pci_free_consistent(
					ohci->dev, (*d)->buf_size, 
					(*d)->buf_cpu[i], (*d)->buf_bus[i]);
		kfree((*d)->buf_cpu);
		kfree((*d)->buf_bus);
	}
	if ((*d)->prg_cpu) {
		for (i=0; i<(*d)->num_desc; i++) 
			if ((*d)->prg_cpu[i] && (*d)->prg_bus[i])
				pci_free_consistent(
					ohci->dev, sizeof(struct dma_cmd), 
					(*d)->prg_cpu[i], (*d)->prg_bus[i]);
		kfree((*d)->prg_cpu);
		kfree((*d)->prg_bus);
	}
	if ((*d)->spb) kfree((*d)->spb);
	
	kfree(*d);
	*d = NULL;

	return 0;
}

static struct dma_rcv_ctx *
alloc_dma_rcv_ctx(struct ti_ohci *ohci, int ctx, int num_desc,
		  int buf_size, int split_buf_size, 
		  int ctrlSet, int ctrlClear, int cmdPtr)
{
	struct dma_rcv_ctx *d=NULL;
	int i;

	d = (struct dma_rcv_ctx *)kmalloc(sizeof(struct dma_rcv_ctx), 
					  GFP_KERNEL);

	if (d==NULL) {
		PRINT(KERN_ERR, ohci->id, "failed to allocate dma_rcv_ctx");
		return NULL;
	}

	d->ohci = (void *)ohci;
	d->ctx = ctx;

	d->num_desc = num_desc;
	d->buf_size = buf_size;
	d->split_buf_size = split_buf_size;
	d->ctrlSet = ctrlSet;
	d->ctrlClear = ctrlClear;
	d->cmdPtr = cmdPtr;

	d->buf_cpu = NULL;
	d->buf_bus = NULL;
	d->prg_cpu = NULL;
	d->prg_bus = NULL;
	d->spb = NULL;

	d->buf_cpu = kmalloc(d->num_desc * sizeof(quadlet_t*), GFP_KERNEL);
	d->buf_bus = kmalloc(d->num_desc * sizeof(dma_addr_t), GFP_KERNEL);

	if (d->buf_cpu == NULL || d->buf_bus == NULL) {
		PRINT(KERN_ERR, ohci->id, "failed to allocate dma buffer");
		free_dma_rcv_ctx(&d);
		return NULL;
	}
	memset(d->buf_cpu, 0, d->num_desc * sizeof(quadlet_t*));
	memset(d->buf_bus, 0, d->num_desc * sizeof(dma_addr_t));

	d->prg_cpu = kmalloc(d->num_desc * sizeof(struct dma_cmd*), 
			     GFP_KERNEL);
	d->prg_bus = kmalloc(d->num_desc * sizeof(dma_addr_t), GFP_KERNEL);

	if (d->prg_cpu == NULL || d->prg_bus == NULL) {
		PRINT(KERN_ERR, ohci->id, "failed to allocate dma prg");
		free_dma_rcv_ctx(&d);
		return NULL;
	}
	memset(d->prg_cpu, 0, d->num_desc * sizeof(struct dma_cmd*));
	memset(d->prg_bus, 0, d->num_desc * sizeof(dma_addr_t));

	d->spb = kmalloc(d->split_buf_size, GFP_KERNEL);

	if (d->spb == NULL) {
		PRINT(KERN_ERR, ohci->id, "failed to allocate split buffer");
		free_dma_rcv_ctx(&d);
		return NULL;
	}

	for (i=0; i<d->num_desc; i++) {
                d->buf_cpu[i] = pci_alloc_consistent(ohci->dev, 
						     d->buf_size,
						     d->buf_bus+i);
		
                if (d->buf_cpu[i] != NULL) {
			memset(d->buf_cpu[i], 0, d->buf_size);
		} else {
			PRINT(KERN_ERR, ohci->id, 
			      "failed to allocate dma buffer");
			free_dma_rcv_ctx(&d);
			return NULL;
		}

		
                d->prg_cpu[i] = pci_alloc_consistent(ohci->dev, 
						     sizeof(struct dma_cmd),
						     d->prg_bus+i);

                if (d->prg_cpu[i] != NULL) {
                        memset(d->prg_cpu[i], 0, sizeof(struct dma_cmd));
		} else {
			PRINT(KERN_ERR, ohci->id, 
			      "failed to allocate dma prg");
			free_dma_rcv_ctx(&d);
			return NULL;
		}
	}

        spin_lock_init(&d->lock);

        /* initialize bottom handler */
        d->task.sync = 0;
        INIT_LIST_HEAD(&d->task.list);
        d->task.routine = dma_rcv_bh;
        d->task.data = (void*)d;

	return d;
}

static int free_dma_trm_ctx(struct dma_trm_ctx **d)
{
	struct ti_ohci *ohci;
	int i;

	if (*d==NULL) return -1;

	ohci = (struct ti_ohci *)(*d)->ohci;

	DBGMSG(ohci->id, "Freeing dma_trm_ctx %d",(*d)->ctx);

	ohci1394_stop_context(ohci, (*d)->ctrlClear, NULL);

	if ((*d)->prg_cpu) {
		for (i=0; i<(*d)->num_desc; i++) 
			if ((*d)->prg_cpu[i] && (*d)->prg_bus[i])
				pci_free_consistent(
					ohci->dev, sizeof(struct at_dma_prg), 
					(*d)->prg_cpu[i], (*d)->prg_bus[i]);
		kfree((*d)->prg_cpu);
		kfree((*d)->prg_bus);
	}

	kfree(*d);
	*d = NULL;
	return 0;
}

static struct dma_trm_ctx *
alloc_dma_trm_ctx(struct ti_ohci *ohci, int ctx, int num_desc,
		  int ctrlSet, int ctrlClear, int cmdPtr)
{
	struct dma_trm_ctx *d=NULL;
	int i;

	d = (struct dma_trm_ctx *)kmalloc(sizeof(struct dma_trm_ctx), 
					  GFP_KERNEL);

	if (d==NULL) {
		PRINT(KERN_ERR, ohci->id, "failed to allocate dma_trm_ctx");
		return NULL;
	}

	d->ohci = (void *)ohci;
	d->ctx = ctx;
	d->num_desc = num_desc;
	d->ctrlSet = ctrlSet;
	d->ctrlClear = ctrlClear;
	d->cmdPtr = cmdPtr;
	d->prg_cpu = NULL;
	d->prg_bus = NULL;

	d->prg_cpu = kmalloc(d->num_desc * sizeof(struct at_dma_prg*), 
			     GFP_KERNEL);
	d->prg_bus = kmalloc(d->num_desc * sizeof(dma_addr_t), GFP_KERNEL);

	if (d->prg_cpu == NULL || d->prg_bus == NULL) {
		PRINT(KERN_ERR, ohci->id, "failed to allocate at dma prg");
		free_dma_trm_ctx(&d);
		return NULL;
	}
	memset(d->prg_cpu, 0, d->num_desc * sizeof(struct at_dma_prg*));
	memset(d->prg_bus, 0, d->num_desc * sizeof(dma_addr_t));

	for (i=0; i<d->num_desc; i++) {
                d->prg_cpu[i] = pci_alloc_consistent(ohci->dev, 
						     sizeof(struct at_dma_prg),
						     d->prg_bus+i);

                if (d->prg_cpu[i] != NULL) {
                        memset(d->prg_cpu[i], 0, sizeof(struct at_dma_prg));
		} else {
			PRINT(KERN_ERR, ohci->id, 
			      "failed to allocate at dma prg");
			free_dma_trm_ctx(&d);
			return NULL;
		}
	}

        spin_lock_init(&d->lock);

        /* initialize bottom handler */
        d->task.routine = dma_trm_bh;
        d->task.data = (void*)d;

	return d;
}

static u32 ohci_crc16(unsigned *data, int length)
{
        int check=0, i;
        int shift, sum, next=0;

        for (i = length; i; i--) {
                for (next = check, shift = 28; shift >= 0; shift -= 4 ) {
                        sum = ((next >> 12) ^ (*data >> shift)) & 0xf;
                        next = (next << 4) ^ (sum << 12) ^ (sum << 5) ^ (sum);
                }
                check = next & 0xffff;
                data++;
        }

        return check;
}

static void ohci_init_config_rom(struct ti_ohci *ohci)
{
	int i;

	ohci_csr_rom[3] = reg_read(ohci, OHCI1394_GUIDHi);
	ohci_csr_rom[4] = reg_read(ohci, OHCI1394_GUIDLo);
	
	ohci_csr_rom[0] = 0x04040000 | ohci_crc16(ohci_csr_rom+1, 4);

	for (i=0;i<sizeof(ohci_csr_rom)/4;i++)
		ohci->csr_config_rom_cpu[i] = cpu_to_be32(ohci_csr_rom[i]);
}

static int add_card(struct pci_dev *dev)
{
	struct ti_ohci *ohci;	/* shortcut to currently handled device */

	if (num_of_cards == MAX_OHCI1394_CARDS) {
		PRINT_G(KERN_WARNING, "cannot handle more than %d cards.  "
			"Adjust MAX_OHCI1394_CARDS in ti_ohci1394.h.",
			MAX_OHCI1394_CARDS);
		return 1;
	}

        if (pci_enable_device(dev)) {
                PRINT_G(KERN_NOTICE, "failed to enable OHCI hardware %d",
                        num_of_cards);
                return 1;
        }
        pci_set_master(dev);

	ohci = &cards[num_of_cards++];

	ohci->id = num_of_cards-1;
	ohci->dev = dev;
	
	ohci->state = 0;

	/* csr_config rom allocation */
	ohci->csr_config_rom_cpu = 
		pci_alloc_consistent(ohci->dev, sizeof(ohci_csr_rom), 
				     &ohci->csr_config_rom_bus);
	if (ohci->csr_config_rom_cpu == NULL) {
		FAIL("failed to allocate buffer config rom");
	}

	/* 
	 * self-id dma buffer allocation
	 * FIXME: some early chips may need 8KB alignment for the 
	 * selfid buffer... if you have problems a temporary fic
	 * is to allocate 8192 bytes instead of 2048
	 */
	ohci->selfid_buf_cpu = 
		pci_alloc_consistent(ohci->dev, 8192, &ohci->selfid_buf_bus);
	if (ohci->selfid_buf_cpu == NULL) {
		FAIL("failed to allocate DMA buffer for self-id packets");
	}
	if ((unsigned long)ohci->selfid_buf_cpu & 0x1fff)
		PRINT(KERN_INFO, ohci->id, "Selfid buffer %p not aligned on "
		      "8Kb boundary... may cause pb on some CXD3222 chip", 
		      ohci->selfid_buf_cpu);  

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,13)
	ohci->registers = ioremap_nocache(dev->base_address[0],
					  OHCI1394_REGISTER_SIZE);
#else
	ohci->registers = ioremap_nocache(dev->resource[0].start,
					  OHCI1394_REGISTER_SIZE);
#endif

	if (ohci->registers == NULL) {
		FAIL("failed to remap registers - card not accessible");
	}

	PRINT(KERN_INFO, ohci->id, "remapped memory spaces reg 0x%p",
	      ohci->registers);

	ohci->ar_req_context = 
		alloc_dma_rcv_ctx(ohci, 0, AR_REQ_NUM_DESC,
				  AR_REQ_BUF_SIZE, AR_REQ_SPLIT_BUF_SIZE,
				  OHCI1394_AsReqRcvContextControlSet,
				  OHCI1394_AsReqRcvContextControlClear,
				  OHCI1394_AsReqRcvCommandPtr);

	if (ohci->ar_req_context == NULL) {
		FAIL("failed to allocate AR Req context");
	}

	ohci->ar_resp_context = 
		alloc_dma_rcv_ctx(ohci, 1, AR_RESP_NUM_DESC,
				  AR_RESP_BUF_SIZE, AR_RESP_SPLIT_BUF_SIZE,
				  OHCI1394_AsRspRcvContextControlSet,
				  OHCI1394_AsRspRcvContextControlClear,
				  OHCI1394_AsRspRcvCommandPtr);
	
	if (ohci->ar_resp_context == NULL) {
		FAIL("failed to allocate AR Resp context");
	}

	ohci->at_req_context = 
		alloc_dma_trm_ctx(ohci, 0, AT_REQ_NUM_DESC,
				  OHCI1394_AsReqTrContextControlSet,
				  OHCI1394_AsReqTrContextControlClear,
				  OHCI1394_AsReqTrCommandPtr);
	
	if (ohci->at_req_context == NULL) {
		FAIL("failed to allocate AT Req context");
	}

	ohci->at_resp_context = 
		alloc_dma_trm_ctx(ohci, 1, AT_RESP_NUM_DESC,
				  OHCI1394_AsRspTrContextControlSet,
				  OHCI1394_AsRspTrContextControlClear,
				  OHCI1394_AsRspTrCommandPtr);
	
	if (ohci->at_resp_context == NULL) {
		FAIL("failed to allocate AT Resp context");
	}
				      
	ohci->ir_context =
		alloc_dma_rcv_ctx(ohci, 2, IR_NUM_DESC,
				  IR_BUF_SIZE, IR_SPLIT_BUF_SIZE,
				  OHCI1394_IsoRcvContextControlSet,
				  OHCI1394_IsoRcvContextControlClear,
				  OHCI1394_IsoRcvCommandPtr);

	if (ohci->ir_context == NULL) {
		FAIL("failed to allocate IR context");
	}

        ohci->ISO_channel_usage= 0;
        spin_lock_init(&ohci->IR_channel_lock);

	if (!request_irq(dev->irq, ohci_irq_handler, SA_SHIRQ,
			 OHCI1394_DRIVER_NAME, ohci)) {
		PRINT(KERN_INFO, ohci->id, "allocated interrupt %d", dev->irq);
	} else {
		FAIL("failed to allocate shared interrupt %d", dev->irq);
	}

	ohci_init_config_rom(ohci);

	DBGMSG(ohci->id, "The 1st byte at offset 0x404 is: 0x%02x",
	       *((char *)ohci->csr_config_rom_cpu+4));

	return 0;
#undef FAIL
}

#ifdef CONFIG_PROC_FS

#define SR(fmt, reg0, reg1, reg2)\
p += sprintf(p,fmt,reg_read(ohci, reg0),\
	       reg_read(ohci, reg1),reg_read(ohci, reg2));

static int ohci_get_status(char *buf)
{
	struct ti_ohci *ohci=&cards[0];
	struct hpsb_host *host=ohci->host;
	char *p=buf;
	//unsigned char phyreg;
	//int i, nports;
	int i;

	struct dma_rcv_ctx *d=NULL;
	struct dma_trm_ctx *dt=NULL;

	p += sprintf(p,"IEEE-1394 OHCI Driver status report:\n");
	p += sprintf(p,"  bus number: 0x%x Node ID: 0x%x\n", 
		     (reg_read(ohci, OHCI1394_NodeID) & 0xFFC0) >> 6, 
		     reg_read(ohci, OHCI1394_NodeID)&0x3f);
#if 0
	p += sprintf(p,"  hardware version %d.%d GUID_ROM is %s\n\n", 
		     (reg_read(ohci, OHCI1394_Version) & 0xFF0000) >>16, 
		     reg_read(ohci, OHCI1394_Version) & 0xFF,
		     (reg_read(ohci, OHCI1394_Version) & 0x01000000) 
		     ? "set" : "clear");
#endif
	p += sprintf(p,"\n### Host data ###\n");
	p += sprintf(p,"node_count: %8d  ",host->node_count);
	p += sprintf(p,"node_id   : %08X\n",host->node_id);
	p += sprintf(p,"irm_id    : %08X  ",host->irm_id);
	p += sprintf(p,"busmgr_id : %08X\n",host->busmgr_id);
	p += sprintf(p,"%s %s %s\n",
		     host->initialized ? "initialized" : "",
		     host->in_bus_reset ? "in_bus_reset" : "",
		     host->attempt_root ? "attempt_root" : "");
	p += sprintf(p,"%s %s %s %s\n",
		     host->is_root ? "root" : "",
		     host->is_cycmst ? "cycle_master" : "",
		     host->is_irm ? "iso_res_mgr" : "",
		     host->is_busmgr ? "bus_mgr" : "");
  
	p += sprintf(p,"\n---Iso Receive DMA---\n");

	d = ohci->ir_context;
#if 0
	for (i=0; i<d->num_desc; i++) {
		p += sprintf(p, "IR buf[%d] : %p prg[%d]: %p\n",
			     i, d->buf[i], i, d->prg[i]);
	}
#endif
	p += sprintf(p, "Current buf: %d offset: %d\n",
		     d->buf_ind,d->buf_offset);

	p += sprintf(p,"\n---Async Receive DMA---\n");
	d = ohci->ar_req_context;
#if 0
	for (i=0; i<d->num_desc; i++) {
		p += sprintf(p, "AR req buf[%d] : %p prg[%d]: %p\n",
			     i, d->buf[i], i, d->prg[i]);
	}
#endif
	p += sprintf(p, "Ar req current buf: %d offset: %d\n",
		     d->buf_ind,d->buf_offset);

	d = ohci->ar_resp_context;
#if 0
	for (i=0; i<d->num_desc; i++) {
		p += sprintf(p, "AR resp buf[%d] : %p prg[%d]: %p\n",
			     i, d->buf[i], i, d->prg[i]);
	}
#endif
	p += sprintf(p, "AR resp current buf: %d offset: %d\n",
		     d->buf_ind,d->buf_offset);

	p += sprintf(p,"\n---Async Transmit DMA---\n");
	dt = ohci->at_req_context;
	p += sprintf(p, "AT req prg: %d sent: %d free: %d branchAddrPtr: %p\n",
		     dt->prg_ind, dt->sent_ind, dt->free_prgs, 
		     dt->branchAddrPtr);
	p += sprintf(p, "AT req queue: first: %p last: %p\n",
		     dt->fifo_first, dt->fifo_last);
	dt = ohci->at_resp_context;
#if 0
	for (i=0; i<dt->num_desc; i++) {
		p += sprintf(p, "------- AT resp prg[%02d] ------\n",i);
		p += sprintf(p, "%p: control  : %08x\n",
			     &(dt->prg[i].begin.control),
			     dt->prg[i].begin.control);
		p += sprintf(p, "%p: address  : %08x\n",
			     &(dt->prg[i].begin.address),
			     dt->prg[i].begin.address);
		p += sprintf(p, "%p: brancAddr: %08x\n",
			     &(dt->prg[i].begin.branchAddress),
			     dt->prg[i].begin.branchAddress);
		p += sprintf(p, "%p: status   : %08x\n",
			     &(dt->prg[i].begin.status),
			     dt->prg[i].begin.status);
		p += sprintf(p, "%p: header[0]: %08x\n",
			     &(dt->prg[i].data[0]),
			     dt->prg[i].data[0]);
		p += sprintf(p, "%p: header[1]: %08x\n",
			     &(dt->prg[i].data[1]),
			     dt->prg[i].data[1]);
		p += sprintf(p, "%p: header[2]: %08x\n",
			     &(dt->prg[i].data[2]),
			     dt->prg[i].data[2]);
		p += sprintf(p, "%p: header[3]: %08x\n",
			     &(dt->prg[i].data[3]),
			     dt->prg[i].data[3]);
		p += sprintf(p, "%p: control  : %08x\n",
			     &(dt->prg[i].end.control),
			     dt->prg[i].end.control);
		p += sprintf(p, "%p: address  : %08x\n",
			     &(dt->prg[i].end.address),
			     dt->prg[i].end.address);
		p += sprintf(p, "%p: brancAddr: %08x\n",
			     &(dt->prg[i].end.branchAddress),
			     dt->prg[i].end.branchAddress);
		p += sprintf(p, "%p: status   : %08x\n",
			     &(dt->prg[i].end.status),
			     dt->prg[i].end.status);
	}
#endif
	p += sprintf(p, "AR resp prg: %d sent: %d free: %d"
		     " branchAddrPtr: %p\n",
		     dt->prg_ind, dt->sent_ind, dt->free_prgs, 
		     dt->branchAddrPtr);
	p += sprintf(p, "AT resp queue: first: %p last: %p\n",
		     dt->fifo_first, dt->fifo_last);
	
	/* ----- Register Dump ----- */
	p += sprintf(p,"\n### HC Register dump ###\n");
	SR("Version     : %08x  GUID_ROM    : %08x  ATRetries   : %08x\n",
	   OHCI1394_Version, OHCI1394_GUID_ROM, OHCI1394_ATRetries);
	SR("CSRData     : %08x  CSRCompData : %08x  CSRControl  : %08x\n",
	   OHCI1394_CSRData, OHCI1394_CSRCompareData, OHCI1394_CSRControl);
	SR("ConfigROMhdr: %08x  BusID       : %08x  BusOptions  : %08x\n",
	   OHCI1394_ConfigROMhdr, OHCI1394_BusID, OHCI1394_BusOptions);
	SR("GUIDHi      : %08x  GUIDLo      : %08x  ConfigROMmap: %08x\n",
	   OHCI1394_GUIDHi, OHCI1394_GUIDLo, OHCI1394_ConfigROMmap);
	SR("PtdWrAddrLo : %08x  PtdWrAddrHi : %08x  VendorID    : %08x\n",
	   OHCI1394_PostedWriteAddressLo, OHCI1394_PostedWriteAddressHi, 
	   OHCI1394_VendorID);
	SR("HCControl   : %08x  SelfIDBuffer: %08x  SelfIDCount : %08x\n",
	   OHCI1394_HCControlSet, OHCI1394_SelfIDBuffer, OHCI1394_SelfIDCount);
	SR("IRMuChMaskHi: %08x  IRMuChMaskLo: %08x  IntEvent    : %08x\n",
	   OHCI1394_IRMultiChanMaskHiSet, OHCI1394_IRMultiChanMaskLoSet, 
	   OHCI1394_IntEventSet);
	SR("IntMask     : %08x  IsoXmIntEvnt: %08x  IsoXmIntMask: %08x\n",
	   OHCI1394_IntMaskSet, OHCI1394_IsoXmitIntEventSet, 
	   OHCI1394_IsoXmitIntMaskSet);
	SR("IsoRcvIntEvt: %08x  IsoRcvIntMsk: %08x  FairnessCtrl: %08x\n",
	   OHCI1394_IsoRecvIntEventSet, OHCI1394_IsoRecvIntMaskSet, 
	   OHCI1394_FairnessControl);
	SR("LinkControl : %08x  NodeID      : %08x  PhyControl  : %08x\n",
	   OHCI1394_LinkControlSet, OHCI1394_NodeID, OHCI1394_PhyControl);
	SR("IsoCyclTimer: %08x  AsRqFilterHi: %08x  AsRqFilterLo: %08x\n",
	   OHCI1394_IsochronousCycleTimer, 
	   OHCI1394_AsReqFilterHiSet, OHCI1394_AsReqFilterLoSet);
	SR("PhyReqFiltHi: %08x  PhyReqFiltLo: %08x  PhyUpperBnd : %08x\n",
	   OHCI1394_PhyReqFilterHiSet, OHCI1394_PhyReqFilterLoSet, 
	   OHCI1394_PhyUpperBound);
	SR("AsRqTrCxtCtl: %08x  AsRqTrCmdPtr: %08x  AsRsTrCtxCtl: %08x\n",
	   OHCI1394_AsReqTrContextControlSet, OHCI1394_AsReqTrCommandPtr, 
	   OHCI1394_AsRspTrContextControlSet);
	SR("AsRsTrCmdPtr: %08x  AsRqRvCtxCtl: %08x  AsRqRvCmdPtr: %08x\n",
	   OHCI1394_AsRspTrCommandPtr, OHCI1394_AsReqRcvContextControlSet,
	   OHCI1394_AsReqRcvCommandPtr);
	SR("AsRsRvCtxCtl: %08x  AsRsRvCmdPtr: %08x  IntEvent    : %08x\n",
	   OHCI1394_AsRspRcvContextControlSet, OHCI1394_AsRspRcvCommandPtr, 
	   OHCI1394_IntEventSet);
	for (i=0;i<ohci->nb_iso_rcv_ctx;i++) {
		p += sprintf(p,"IsoRCtxCtl%02d: %08x  IsoRCmdPtr%02d: %08x"
			     "  IsoRCxtMch%02d: %08x\n", i,
			     reg_read(ohci, 
				      OHCI1394_IsoRcvContextControlSet+32*i),
			     i,reg_read(ohci, OHCI1394_IsoRcvCommandPtr+32*i),
			     i,reg_read(ohci, 
					OHCI1394_IsoRcvContextMatch+32*i));
	}
	for (i=0;i<ohci->nb_iso_xmit_ctx;i++) {
		p += sprintf(p,"IsoTCtxCtl%02d: %08x  IsoTCmdPtr%02d: %08x\n",
			     i, 
			     reg_read(ohci, 
				      OHCI1394_IsoXmitContextControlSet+32*i),
			     i,reg_read(ohci,OHCI1394_IsoXmitCommandPtr+32*i));
	}

#if 0
	p += sprintf(p,"\n### Phy Register dump ###\n");
	phyreg=get_phy_reg(ohci,1);
	p += sprintf(p,"offset: %d val: 0x%02x -> RHB: %d"
		     "IBR: %d Gap_count: %d\n",
		     1,phyreg,(phyreg&0x80) != 0, 
		     (phyreg&0x40) !=0, phyreg&0x3f);
	phyreg=get_phy_reg(ohci,2);
	nports=phyreg&0x1f;
	p += sprintf(p,"offset: %d val: 0x%02x -> SPD: %d"
		     " E  : %d Ports    : %2d\n",
		     2,phyreg, (phyreg&0xC0)>>6, (phyreg&0x20) !=0, nports);
	for (i=0;i<nports;i++) {
		phyreg=get_phy_reg(ohci,3+i);
		p += sprintf(p,"offset: %d val: 0x%02x -> [port %d]"
			     " TPA: %d TPB: %d | %s %s\n",
			     3+i,phyreg,
			     i, (phyreg&0xC0)>>6, (phyreg&0x30)>>4,
			     (phyreg&0x08) ? "child" : "parent",
			     (phyreg&0x04) ? "connected" : "disconnected");
	}
	phyreg=get_phy_reg(ohci,3+i);
	p += sprintf(p,"offset: %d val: 0x%02x -> ENV: %s Reg_count: %d\n",
		     3+i,phyreg,
		     (((phyreg&0xC0)>>6)==0) ? "backplane" :
		     (((phyreg&0xC0)>>6)==1) ? "cable" : "reserved",
		     phyreg&0x3f);
#endif

	return  p - buf;
}

static int ohci1394_read_proc(char *page, char **start, off_t off,
			      int count, int *eof, void *data)
{
        int len = ohci_get_status(page);
        if (len <= off+count) *eof = 1;
        *start = page + off;
        len -= off;
        if (len>count) len = count;
        if (len<0) len = 0;
        return len;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)
struct proc_dir_entry *ohci_proc_entry;
#endif /* LINUX_VERSION_CODE */
#endif /* CONFIG_PROC_FS */

static void remove_card(struct ti_ohci *ohci)
{
	/*
	 * Reset the board properly before leaving
	 * Daniel Kobras <daniel.kobras@student.uni-tuebingen.de>
	 */
	ohci_soft_reset(ohci);

	/* Free AR dma */
	free_dma_rcv_ctx(&ohci->ar_req_context);
	free_dma_rcv_ctx(&ohci->ar_resp_context);

	/* Free AT dma */
	free_dma_trm_ctx(&ohci->at_req_context);
	free_dma_trm_ctx(&ohci->at_resp_context);

	/* Free IR dma */
	free_dma_rcv_ctx(&ohci->ir_context);

	/* Free self-id buffer */
	if (ohci->selfid_buf_cpu)
		pci_free_consistent(ohci->dev, 2048, 
				    ohci->selfid_buf_cpu,
				    ohci->selfid_buf_bus);
	
	/* Free config rom */
	if (ohci->csr_config_rom_cpu)
		pci_free_consistent(ohci->dev, sizeof(ohci_csr_rom), 
				    ohci->csr_config_rom_cpu, 
				    ohci->csr_config_rom_bus);
	
	/* Free the IRQ */
	free_irq(ohci->dev->irq, ohci);

	if (ohci->registers) 
		iounmap(ohci->registers);

	ohci->state = 0;
}

static int init_driver()
{
	struct pci_dev *dev = NULL;
	int success = 0;
#if USE_DEVICE
	int i;
#endif
	if (num_of_cards) {
		PRINT_G(KERN_DEBUG, __PRETTY_FUNCTION__ " called again");
		return 0;
	}

	PRINT_G(KERN_INFO, "looking for Ohci1394 cards");

#if USE_DEVICE
	for (i = 0; supported_chips[i][0] != -1; i++) {
		while ((dev = pci_find_device(supported_chips[i][0],
					      supported_chips[i][1], dev)) 
		       != NULL) {
			if (add_card(dev) == 0) {
				success = 1;
			}
		}
	}
#else
	while ((dev = pci_find_class(PCI_CLASS_FIREWIRE_OHCI, dev)) != NULL ) {
		if (add_card(dev) == 0) success = 1;
 	}
#endif /* USE_DEVICE */
	if (success == 0) {
		PRINT_G(KERN_WARNING, "no operable Ohci1394 cards found");
		return -ENXIO;
	}

#ifdef CONFIG_PROC_FS
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)
	create_proc_read_entry ("ohci1394", 0, NULL, ohci1394_read_proc, NULL);
#else
	if ((ohci_proc_entry = create_proc_entry("ohci1394", 0, NULL)))
		ohci_proc_entry->read_proc = ohci1394_read_proc;
#endif
#endif
	return 0;
}

static size_t get_ohci_rom(struct hpsb_host *host, const quadlet_t **ptr)
{
	struct ti_ohci *ohci=host->hostdata;

	DBGMSG(ohci->id, "request csr_rom address: %08X",
	       (u32)ohci->csr_config_rom_cpu);

	*ptr = ohci->csr_config_rom_cpu;
	return sizeof(ohci_csr_rom);
}

static quadlet_t ohci_hw_csr_reg(struct hpsb_host *host, int reg,
				 quadlet_t data, quadlet_t compare)
{
	struct ti_ohci *ohci=host->hostdata;
	int timeout = 255;

	reg_write(ohci, OHCI1394_CSRData, data);
	reg_write(ohci, OHCI1394_CSRCompareData, compare);
	reg_write(ohci, OHCI1394_CSRControl, reg&0x3);

	while (timeout-- && !(reg_read(ohci, OHCI1394_CSRControl)&0x80000000));

	if (!timeout)
		PRINT(KERN_ERR, ohci->id, __FUNCTION__ "timeout!");

	return reg_read(ohci, OHCI1394_CSRData);
}
		
struct hpsb_host_template *get_ohci_template(void)
{
	static struct hpsb_host_template tmpl;
	static int initialized = 0;

	if (!initialized) {
		/* Initialize by field names so that a template structure
		 * reorganization does not influence this code. */
		tmpl.name = "ohci1394";
                
		tmpl.detect_hosts = ohci_detect;
		tmpl.initialize_host = ohci_initialize;
		tmpl.release_host = ohci_remove;
		tmpl.get_rom = get_ohci_rom;
		tmpl.transmit_packet = ohci_transmit;
		tmpl.devctl = ohci_devctl;
		tmpl.hw_csr_reg = ohci_hw_csr_reg;
		initialized = 1;
	}

	return &tmpl;
}

void ohci1394_stop_context(struct ti_ohci *ohci, int reg, char *msg)
{
	int i=0;

	/* stop the channel program if it's still running */
	reg_write(ohci, reg, 0x8000);
   
	/* Wait until it effectively stops */
	while (reg_read(ohci, reg) & 0x400) {
		i++;
		if (i>5000) {
			PRINT(KERN_ERR, ohci->id, 
			      "runaway loop while stopping context...");
			break;
		}
	}
	if (msg) PRINT(KERN_ERR, ohci->id, "%s\n dma prg stopped\n", msg);
}

struct ti_ohci *ohci1394_get_struct(int card_num)
{
	if (card_num>=0 && card_num<num_of_cards) 
		return &cards[card_num];
	return NULL;
}

int ohci1394_register_video(struct ti_ohci *ohci,
			    struct video_template *tmpl)
{
	if (ohci->video_tmpl)
		return -ENFILE;
	ohci->video_tmpl = tmpl;
	MOD_INC_USE_COUNT;
	return 0;
}
	
void ohci1394_unregister_video(struct ti_ohci *ohci,
			       struct video_template *tmpl)
{
	if (ohci->video_tmpl != tmpl) {
		PRINT(KERN_ERR, ohci->id, 
		      "Trying to unregister wrong video device");
	}
	else {
		ohci->video_tmpl = NULL;
		MOD_DEC_USE_COUNT;
	}
}

#if 0
int ohci_compare_swap(struct ti_ohci *ohci, quadlet_t *data, 
		      quadlet_t compare, int sel)
{
	int timeout = 255;
	reg_write(ohci, OHCI1394_CSRData, *data);
	reg_write(ohci, OHCI1394_CSRCompareData, compare);
	reg_write(ohci, OHCI1394_CSRControl, sel);
	while(!(reg_read(ohci, OHCI1394_CSRControl)&0x80000000)) {
		if (timeout--) {
			PRINT(KERN_INFO, ohci->id, "request_channel timeout");
			return -1;
		}
	}
	*data = reg_read(ohci, OHCI1394_CSRData);
	return 0;
}

int ohci1394_request_channel(struct ti_ohci *ohci, int channel)
{
	int csrSel;
	quadlet_t chan, data1=0, data2=0;
	int timeout = 32;

	if (channel<32) {
		chan = 1<<channel;
		csrSel = 2;
	}
	else {
		chan = 1<<(channel-32);
		csrSel = 3;
	}
	if (ohci_compare_swap(ohci, &data1, 0, csrSel)<0) {
		PRINT(KERN_INFO, ohci->id, "request_channel timeout");
		return -1;
	}
	while (timeout--) {
		if (data1 & chan) {
			PRINT(KERN_INFO, ohci->id, 
			      "request channel %d failed", channel);
			return -1;
		}
		data2 = data1;
		data1 |= chan;
		if (ohci_compare_swap(ohci, &data1, data2, csrSel)<0) {
			PRINT(KERN_INFO, ohci->id, "request_channel timeout");
			return -1;
		}
		if (data1==data2) {
			PRINT(KERN_INFO, ohci->id, 
			      "request channel %d succeded", channel);
			return 0;
		}
	}
	PRINT(KERN_INFO, ohci->id, "request channel %d failed", channel);
	return -1;
}
#endif

EXPORT_SYMBOL(ohci1394_stop_context);
EXPORT_SYMBOL(ohci1394_get_struct);
EXPORT_SYMBOL(ohci1394_register_video);
EXPORT_SYMBOL(ohci1394_unregister_video);

#ifdef MODULE

/* EXPORT_NO_SYMBOLS; */

MODULE_AUTHOR("Sebastien Rougeaux <sebastien.rougeaux@anu.edu.au>");
MODULE_DESCRIPTION("driver for PCI Ohci IEEE-1394 controller");
MODULE_SUPPORTED_DEVICE("ohci1394");

void cleanup_module(void)
{
	hpsb_unregister_lowlevel(get_ohci_template());
#ifdef CONFIG_PROC_FS
	remove_proc_entry ("ohci1394", NULL);
#endif

	PRINT_G(KERN_INFO, "removed " OHCI1394_DRIVER_NAME " module");
}

int init_module(void)
{
	memset(cards, 0, MAX_OHCI1394_CARDS * sizeof (struct ti_ohci));
	
	if (hpsb_register_lowlevel(get_ohci_template())) {
		PRINT_G(KERN_ERR, "registering failed");
		return -ENXIO;
	} 
	return 0;
}

#endif /* MODULE */
