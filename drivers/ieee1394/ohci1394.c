/*
 * ohci1394.c - driver for OHCI 1394 boards
 * Copyright (C)1999,2000 Sebastien Rougeaux <sebastien.rougeaux@anu.edu.au>
 *                        Gord Peters <GordPeters@smarttech.com>
 *              2001      Ben Collins <bcollins@debian.org>
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
 * . Config ROM generation
 *
 * Things implemented, but still in test phase:
 * . Iso Transmit
 * 
 * Things not implemented:
 * . Async Stream Packets
 * . DMA error recovery
 *
 * Known bugs:
 * . devctl BUS_RESET arg confusion (reset type or root holdoff?)
 */

/* 
 * Acknowledgments:
 *
 * Adam J Richter <adam@yggdrasil.com>
 *  . Use of pci_class to find device
 *
 * Andreas Tobler <toa@pop.agri.ch>
 *  . Updated proc_fs calls
 *
 * Emilie Chung	<emilie.chung@axis.com>
 *  . Tip on Async Request Filter
 *
 * Pascal Drolet <pascal.drolet@informission.ca>
 *  . Various tips for optimization and functionnalities
 *
 * Robert Ficklin <rficklin@westengineering.com>
 *  . Loop in irq_handler
 *
 * James Goodwin <jamesg@Filanet.com>
 *  . Various tips on initialization, self-id reception, etc.
 *
 * Albrecht Dress <ad@mpifr-bonn.mpg.de>
 *  . Apple PowerBook detection
 *
 * Daniel Kobras <daniel.kobras@student.uni-tuebingen.de>
 *  . Reset the board properly before leaving + misc cleanups
 *
 * Leon van Stuivenberg <leonvs@iae.nl>
 *  . Bug fixes
 *
 * Ben Collins <bcollins@debian.org>
 *  . Working big-endian support
 *  . Updated to 2.4.x module scheme (PCI aswell)
 *  . Removed procfs support since it trashes random mem
 *  . Config ROM generation
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/list.h>
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
#include <asm/uaccess.h>
#include <linux/tqueue.h>
#include <linux/delay.h>
#include <linux/spinlock.h>

#include <asm/pgtable.h>
#include <asm/page.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/wrapper.h>
#include <linux/vmalloc.h>
#include <linux/init.h>

#ifdef CONFIG_ALL_PPC
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#endif

#include "ieee1394.h"
#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "highlevel.h"
#include "ohci1394.h"

#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
#define OHCI1394_DEBUG
#endif

#ifdef DBGMSG
#undef DBGMSG
#endif

#ifdef OHCI1394_DEBUG
#define DBGMSG(card, fmt, args...) \
printk(KERN_INFO "%s_%d: " fmt "\n" , OHCI1394_DRIVER_NAME, card , ## args)
#else
#define DBGMSG(card, fmt, args...)
#endif

#ifdef CONFIG_IEEE1394_OHCI_DMA_DEBUG
#define OHCI_DMA_ALLOC(fmt, args...) \
	HPSB_ERR("%s(%s)alloc(%d): "fmt, OHCI1394_DRIVER_NAME, __FUNCTION__, \
		++global_outstanding_dmas, ## args)
#define OHCI_DMA_FREE(fmt, args...) \
	HPSB_ERR("%s(%s)free(%d): "fmt, OHCI1394_DRIVER_NAME, __FUNCTION__, \
		--global_outstanding_dmas, ## args)
u32 global_outstanding_dmas = 0;
#else
#define OHCI_DMA_ALLOC(fmt, args...)
#define OHCI_DMA_FREE(fmt, args...)
#endif

/* print general (card independent) information */
#define PRINT_G(level, fmt, args...) \
printk(level "%s: " fmt "\n" , OHCI1394_DRIVER_NAME , ## args)

/* print card specific information */
#define PRINT(level, card, fmt, args...) \
printk(level "%s_%d: " fmt "\n" , OHCI1394_DRIVER_NAME, card , ## args)

static char version[] __devinitdata =
	"$Rev: 578 $ Ben Collins <bcollins@debian.org>";

/* Module Parameters */
MODULE_PARM(attempt_root,"i");
MODULE_PARM_DESC(attempt_root, "Attempt to make the host root (default = 0).");
static int attempt_root = 0;

MODULE_PARM(phys_dma,"i");
MODULE_PARM_DESC(phys_dma, "Enable physical dma (default = 1).");
static int phys_dma = 1;

static void dma_trm_tasklet(unsigned long data);
static void dma_trm_reset(struct dma_trm_ctx *d);

static void ohci1394_pci_remove(struct pci_dev *pdev);

#ifndef __LITTLE_ENDIAN
/* Swap a series of quads inplace. */
static __inline__ void block_swab32(quadlet_t *data, size_t size) {
	while (size--)
		data[size] = swab32(data[size]);
}

static unsigned hdr_sizes[] = 
{
	3,	/* TCODE_WRITEQ */
	4,	/* TCODE_WRITEB */
	3,	/* TCODE_WRITE_RESPONSE */
	0,	/* ??? */
	3,	/* TCODE_READQ */
	4,	/* TCODE_READB */
	3,	/* TCODE_READQ_RESPONSE */
	4,	/* TCODE_READB_RESPONSE */
	1,	/* TCODE_CYCLE_START (???) */
	4,	/* TCODE_LOCK_REQUEST */
	2,	/* TCODE_ISO_DATA */
	4,	/* TCODE_LOCK_RESPONSE */
};

/* Swap headers */
static inline void packet_swab(quadlet_t *data, int tcode, int len)
{
	if (tcode > TCODE_LOCK_RESPONSE || hdr_sizes[tcode] == 0)
		return;
	block_swab32(data, hdr_sizes[tcode]);
}
#else
/* Don't waste cycles on same sex byte swaps */
#define packet_swab(w,x,y)
#define block_swab32(x,y)
#endif /* !LITTLE_ENDIAN */

/***********************************
 * IEEE-1394 functionality section *
 ***********************************/

static u8 get_phy_reg(struct ti_ohci *ohci, u8 addr) 
{
	int i;
	unsigned long flags;
	quadlet_t r;

	spin_lock_irqsave (&ohci->phy_reg_lock, flags);

	reg_write(ohci, OHCI1394_PhyControl, (addr << 8) | 0x00008000);

	for (i = 0; i < OHCI_LOOP_COUNT; i++) {
		if (reg_read(ohci, OHCI1394_PhyControl) & 0x80000000)
			break;

		mdelay(1);
	}

	r = reg_read(ohci, OHCI1394_PhyControl);

	if (i >= OHCI_LOOP_COUNT)
		PRINT (KERN_ERR, ohci->id, "Get PHY Reg timeout [0x%08x/0x%08x/%d]",
		       r, r & 0x80000000, i);
  
	spin_unlock_irqrestore (&ohci->phy_reg_lock, flags);
     
	return (r & 0x00ff0000) >> 16;
}

static void set_phy_reg(struct ti_ohci *ohci, u8 addr, u8 data)
{
	int i;
	unsigned long flags;
	u32 r = 0;

	spin_lock_irqsave (&ohci->phy_reg_lock, flags);

	reg_write(ohci, OHCI1394_PhyControl, (addr << 8) | data | 0x00004000);

	for (i = 0; i < OHCI_LOOP_COUNT; i++) {
		r = reg_read(ohci, OHCI1394_PhyControl);
		if (!(r & 0x00004000))
			break;

		mdelay(1);
	}

	if (i == OHCI_LOOP_COUNT)
		PRINT (KERN_ERR, ohci->id, "Set PHY Reg timeout [0x%08x/0x%08x/%d]",
		       r, r & 0x00004000, i);

	spin_unlock_irqrestore (&ohci->phy_reg_lock, flags);

	return;
}

/* Or's our value into the current value */
static void set_phy_reg_mask(struct ti_ohci *ohci, u8 addr, u8 data)
{
	u8 old;

	old = get_phy_reg (ohci, addr);
	old |= data;
	set_phy_reg (ohci, addr, old);

	return;
}

static void handle_selfid(struct ti_ohci *ohci, struct hpsb_host *host,
				int phyid, int isroot)
{
	quadlet_t *q = ohci->selfid_buf_cpu;
	quadlet_t self_id_count=reg_read(ohci, OHCI1394_SelfIDCount);
	size_t size;
	quadlet_t q0, q1;

	/* Check status of self-id reception */

	if (ohci->selfid_swap)
		q0 = le32_to_cpu(q[0]);
	else
		q0 = q[0];

	if ((self_id_count & 0x80000000) || 
	    ((self_id_count & 0x00FF0000) != (q0 & 0x00FF0000))) {
		PRINT(KERN_ERR, ohci->id, 
		      "Error in reception of SelfID packets [0x%08x/0x%08x] (count: %d)",
		      self_id_count, q0, ohci->self_id_errors);

		/* Tip by James Goodwin <jamesg@Filanet.com>:
		 * We had an error, generate another bus reset in response.  */
		if (ohci->self_id_errors<OHCI1394_MAX_SELF_ID_ERRORS) {
			set_phy_reg_mask (ohci, 1, 0x40);
			ohci->self_id_errors++;
		} else {
			PRINT(KERN_ERR, ohci->id, 
			      "Too many errors on SelfID error reception, giving up!");
		}
		return;
	}

	/* SelfID Ok, reset error counter. */
	ohci->self_id_errors = 0;
	
	size = ((self_id_count & 0x00001FFC) >> 2) - 1;
	q++;

	while (size > 0) {
		if (ohci->selfid_swap) {
			q0 = le32_to_cpu(q[0]);
			q1 = le32_to_cpu(q[1]);
		} else {
			q0 = q[0];
			q1 = q[1];
		}
		
		if (q0 == ~q1) {
			DBGMSG (ohci->id, "SelfID packet 0x%x received", q0);
			hpsb_selfid_received(host, cpu_to_be32(q0));
			if (((q0 & 0x3f000000) >> 24) == phyid)
				DBGMSG (ohci->id, "SelfID for this node is 0x%08x", q0);
		} else {
			PRINT(KERN_ERR, ohci->id,
			      "SelfID is inconsistent [0x%08x/0x%08x]", q0, q1);
		}
		q += 2;
		size -= 2;
	}

	DBGMSG(ohci->id, "SelfID complete");

	hpsb_selfid_complete(host, phyid, isroot);

	return;
}

static void ohci_soft_reset(struct ti_ohci *ohci) {
	int i;

	reg_write(ohci, OHCI1394_HCControlSet, 0x00010000);
  
	for (i = 0; i < OHCI_LOOP_COUNT; i++) {
		if (reg_read(ohci, OHCI1394_HCControlSet) & 0x00010000)
			break;
		mdelay(1);
	}
	DBGMSG (ohci->id, "Soft reset finished");
}

static int run_context(struct ti_ohci *ohci, int reg, char *msg)
{
	u32 nodeId;

	/* check that the node id is valid */
	nodeId = reg_read(ohci, OHCI1394_NodeID);
	if (!(nodeId&0x80000000)) {
		PRINT(KERN_ERR, ohci->id, 
		      "Running dma failed because Node ID is not valid");
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
	
	if (msg) PRINT(KERN_DEBUG, ohci->id, "%s", msg);
	
	return 0;
}

/* Generate the dma receive prgs and start the context */
static void initialize_dma_rcv_ctx(struct dma_rcv_ctx *d, int generate_irq)
{
	struct ti_ohci *ohci = (struct ti_ohci*)(d->ohci);
	int i;

	ohci1394_stop_context(ohci, d->ctrlClear, NULL);

	for (i=0; i<d->num_desc; i++) {
		u32 c;
		
		c = DMA_CTL_INPUT_MORE | DMA_CTL_UPDATE | DMA_CTL_BRANCH;
		if (generate_irq)
			c |= DMA_CTL_IRQ;
				
		d->prg_cpu[i]->control = cpu_to_le32(c | d->buf_size);

		/* End of descriptor list? */
		if (i + 1 < d->num_desc) {
			d->prg_cpu[i]->branchAddress =
				cpu_to_le32((d->prg_bus[i+1] & 0xfffffff0) | 0x1);
		} else {
			d->prg_cpu[i]->branchAddress =
				cpu_to_le32((d->prg_bus[0] & 0xfffffff0));
		}

		d->prg_cpu[i]->address = cpu_to_le32(d->buf_bus[i]);
		d->prg_cpu[i]->status = cpu_to_le32(d->buf_size);
	}

        d->buf_ind = 0;
        d->buf_offset = 0;

	/* Tell the controller where the first AR program is */
	reg_write(ohci, d->cmdPtr, d->prg_bus[0] | 0x1);

	/* Run AR context */
	reg_write(ohci, d->ctrlSet, 0x00008000);

	DBGMSG(ohci->id, "Receive DMA ctx=%d initialized", d->ctx);
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
	INIT_LIST_HEAD(&d->fifo_list);
	INIT_LIST_HEAD(&d->pending_list);

	DBGMSG(ohci->id, "Transmit DMA ctx=%d initialized", d->ctx);
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

static void ohci_init_config_rom(struct ti_ohci *ohci);

/* Global initialization */
static void ohci_initialize(struct ti_ohci *ohci)
{
	quadlet_t buf;

	spin_lock_init(&ohci->phy_reg_lock);
	spin_lock_init(&ohci->event_lock);
  
	/* Put some defaults to these undefined bus options */
	buf = reg_read(ohci, OHCI1394_BusOptions);
	buf |=  0xE0000000; /* Enable IRMC, CMC and ISC */
	buf &= ~0x00ff0000; /* XXX: Set cyc_clk_acc to zero for now */
	buf &= ~0x18000000; /* Disable PMC and BMC */
	reg_write(ohci, OHCI1394_BusOptions, buf);

	/* Set the bus number */
	reg_write(ohci, OHCI1394_NodeID, 0x0000ffc0);

	/* Enable posted writes */
	reg_write(ohci, OHCI1394_HCControlSet, 0x00040000);

	/* Clear link control register */
	reg_write(ohci, OHCI1394_LinkControlClear, 0xffffffff);
  
	/* Enable cycle timer and cycle master and set the IRM
	 * contender bit in our self ID packets. */
	reg_write(ohci, OHCI1394_LinkControlSet, 0x00300000);
	set_phy_reg_mask(ohci, 4, 0xc0);

	/* Clear interrupt registers */
	reg_write(ohci, OHCI1394_IntMaskClear, 0xffffffff);
	reg_write(ohci, OHCI1394_IntEventClear, 0xffffffff);

	/* Set up self-id dma buffer */
	reg_write(ohci, OHCI1394_SelfIDBuffer, ohci->selfid_buf_bus);

	/* enable self-id dma */
	reg_write(ohci, OHCI1394_LinkControlSet, 0x00000200);

	/* Set the Config ROM mapping register */
	reg_write(ohci, OHCI1394_ConfigROMmap, ohci->csr_config_rom_bus);

	/* Initialize the Config ROM */
	ohci_init_config_rom(ohci);

	/* Now get our max packet size */
	ohci->max_packet_size = 
		1<<(((reg_read(ohci, OHCI1394_BusOptions)>>12)&0xf)+1);

	/* Don't accept phy packets into AR request context */ 
	reg_write(ohci, OHCI1394_LinkControlClear, 0x00000400);

	/* Set bufferFill, isochHeader, multichannel for IR context */
	reg_write(ohci, OHCI1394_IsoRcvContextControlSet, 0xd0000000);
			
	/* Set the context match register to match on all tags */
	reg_write(ohci, OHCI1394_IsoRcvContextMatch, 0xf0000000);

	/* Clear the interrupt mask */
	reg_write(ohci, OHCI1394_IsoRecvIntMaskClear, 0xffffffff);
	reg_write(ohci, OHCI1394_IsoRecvIntEventClear, 0xffffffff);

	/* Clear the interrupt mask */
	reg_write(ohci, OHCI1394_IsoXmitIntMaskClear, 0xffffffff);
	reg_write(ohci, OHCI1394_IsoXmitIntEventClear, 0xffffffff);

	/* Clear the multi channel mask high and low registers */
	reg_write(ohci, OHCI1394_IRMultiChanMaskHiClear, 0xffffffff);
	reg_write(ohci, OHCI1394_IRMultiChanMaskLoClear, 0xffffffff);

	/* Initialize AR dma */
	initialize_dma_rcv_ctx(&ohci->ar_req_context, 0);
	initialize_dma_rcv_ctx(&ohci->ar_resp_context, 0);

	/* Initialize AT dma */
	initialize_dma_trm_ctx(&ohci->at_req_context);
	initialize_dma_trm_ctx(&ohci->at_resp_context);

	/* Initialize IR dma */
	initialize_dma_rcv_ctx(&ohci->ir_context, 1);

        /* Initialize IT dma */
        initialize_dma_trm_ctx(&ohci->it_context);

	/* Set up isoRecvIntMask to generate interrupts for context 0
	   (thanks to Michael Greger for seeing that I forgot this) */
	reg_write(ohci, OHCI1394_IsoRecvIntMaskSet, 0x00000001);

	/* Set up isoXmitIntMask to generate interrupts for context 0 */
	reg_write(ohci, OHCI1394_IsoXmitIntMaskSet, 0x00000001);

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

	/* We don't want hardware swapping */
	reg_write(ohci, OHCI1394_HCControlClear, 0x40000000);

	/* Enable interrupts */
	reg_write(ohci, OHCI1394_IntMaskSet, 
		  OHCI1394_masterIntEnable | 
		  OHCI1394_busReset | 
		  OHCI1394_selfIDComplete |
		  OHCI1394_RSPkt |
		  OHCI1394_RQPkt |
		  OHCI1394_respTxComplete |
		  OHCI1394_reqTxComplete |
		  OHCI1394_isochRx |
		  OHCI1394_isochTx |
		  OHCI1394_cycleInconsistent);

	/* Enable link */
	reg_write(ohci, OHCI1394_HCControlSet, 0x00020000);

	buf = reg_read(ohci, OHCI1394_Version);
	PRINT(KERN_INFO, ohci->id, "OHCI-1394 %d.%d (PCI): IRQ=[%d]  "
	      "MMIO=[%lx-%lx]  Max Packet=[%d]",
	      ((((buf) >> 16) & 0xf) + (((buf) >> 20) & 0xf) * 10),
	      ((((buf) >> 4) & 0xf) + ((buf) & 0xf) * 10), ohci->dev->irq,
	      pci_resource_start(ohci->dev, 0),
	      pci_resource_start(ohci->dev, 0) + OHCI1394_REGISTER_SIZE - 1,
	      ohci->max_packet_size);
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

	DBGMSG(ohci->id, "Inserting packet for node %d, tlabel=%d, tcode=0x%x, speed=%d",
			packet->node_id, packet->tlabel, packet->tcode, packet->speed_code);

	d->prg_cpu[idx]->begin.address = 0;
	d->prg_cpu[idx]->begin.branchAddress = 0;

	if (d->type == DMA_CTX_ASYNC_RESP) {
		/* 
		 * For response packets, we need to put a timeout value in
		 * the 16 lower bits of the status... let's try 1 sec timeout 
		 */ 
		cycleTimer = reg_read(ohci, OHCI1394_IsochronousCycleTimer);
		d->prg_cpu[idx]->begin.status = cpu_to_le32(
			(((((cycleTimer>>25)&0x7)+1)&0x7)<<13) | 
			((cycleTimer&0x01fff000)>>12));

		DBGMSG(ohci->id, "cycleTimer: %08x timeStamp: %08x",
		       cycleTimer, d->prg_cpu[idx]->begin.status);
	} else 
		d->prg_cpu[idx]->begin.status = 0;

        if ( (packet->type == hpsb_async) || (packet->type == hpsb_raw) ) {

                if (packet->type == hpsb_raw) {
			d->prg_cpu[idx]->data[0] = cpu_to_le32(OHCI1394_TCODE_PHY<<4);
                        d->prg_cpu[idx]->data[1] = packet->header[0];
                        d->prg_cpu[idx]->data[2] = packet->header[1];
                } else {
                        d->prg_cpu[idx]->data[0] = packet->speed_code<<16 |
                                (packet->header[0] & 0xFFFF);
                        d->prg_cpu[idx]->data[1] =
                                (packet->header[1] & 0xFFFF) | 
                                (packet->header[0] & 0xFFFF0000);
                        d->prg_cpu[idx]->data[2] = packet->header[2];
                        d->prg_cpu[idx]->data[3] = packet->header[3];
			packet_swab(d->prg_cpu[idx]->data, packet->tcode,
					packet->header_size>>2);
                }

                if (packet->data_size) { /* block transmit */
                        d->prg_cpu[idx]->begin.control =
                                cpu_to_le32(DMA_CTL_OUTPUT_MORE |
					    DMA_CTL_IMMEDIATE | 0x10);
                        d->prg_cpu[idx]->end.control =
                                cpu_to_le32(DMA_CTL_OUTPUT_LAST |
					    DMA_CTL_IRQ | 
					    DMA_CTL_BRANCH |
					    packet->data_size);
                        /* 
                         * Check that the packet data buffer
                         * does not cross a page boundary.
                         */
                        if (cross_bound((unsigned long)packet->data, 
                                        packet->data_size)>0) {
                                /* FIXME: do something about it */
                                PRINT(KERN_ERR, ohci->id,
                                      "%s: packet data addr: %p size %Zd bytes "
                                      "cross page boundary", __FUNCTION__,
                                      packet->data, packet->data_size);
                        }

                        d->prg_cpu[idx]->end.address = cpu_to_le32(
                                pci_map_single(ohci->dev, packet->data,
                                               packet->data_size,
                                               PCI_DMA_TODEVICE));
			OHCI_DMA_ALLOC("single, block transmit packet");

                        d->prg_cpu[idx]->end.branchAddress = 0;
                        d->prg_cpu[idx]->end.status = 0;
                        if (d->branchAddrPtr) 
                                *(d->branchAddrPtr) =
					cpu_to_le32(d->prg_bus[idx] | 0x3);
                        d->branchAddrPtr =
                                &(d->prg_cpu[idx]->end.branchAddress);
                } else { /* quadlet transmit */
                        if (packet->type == hpsb_raw)
                                d->prg_cpu[idx]->begin.control = 
					cpu_to_le32(DMA_CTL_OUTPUT_LAST |
						    DMA_CTL_IMMEDIATE |
						    DMA_CTL_IRQ | 
						    DMA_CTL_BRANCH |
						    (packet->header_size + 4));
                        else
                                d->prg_cpu[idx]->begin.control =
					cpu_to_le32(DMA_CTL_OUTPUT_LAST |
						    DMA_CTL_IMMEDIATE |
						    DMA_CTL_IRQ | 
						    DMA_CTL_BRANCH |
						    packet->header_size);

                        if (d->branchAddrPtr) 
                                *(d->branchAddrPtr) =
					cpu_to_le32(d->prg_bus[idx] | 0x2);
                        d->branchAddrPtr =
                                &(d->prg_cpu[idx]->begin.branchAddress);
                }

        } else { /* iso packet */
                d->prg_cpu[idx]->data[0] = packet->speed_code<<16 |
                        (packet->header[0] & 0xFFFF);
                d->prg_cpu[idx]->data[1] = packet->header[0] & 0xFFFF0000;
		packet_swab(d->prg_cpu[idx]->data, packet->tcode, packet->header_size>>2);
  
                d->prg_cpu[idx]->begin.control = 
			cpu_to_le32(DMA_CTL_OUTPUT_MORE | 
				    DMA_CTL_IMMEDIATE | 0x8);
                d->prg_cpu[idx]->end.control = 
			cpu_to_le32(DMA_CTL_OUTPUT_LAST |
				    DMA_CTL_UPDATE |
				    DMA_CTL_IRQ |
				    DMA_CTL_BRANCH |
				    packet->data_size);
                d->prg_cpu[idx]->end.address = cpu_to_le32(
				pci_map_single(ohci->dev, packet->data,
				packet->data_size, PCI_DMA_TODEVICE));
		OHCI_DMA_ALLOC("single, iso transmit packet");

                d->prg_cpu[idx]->end.branchAddress = 0;
                d->prg_cpu[idx]->end.status = 0;
                DBGMSG(ohci->id, "Iso xmit context info: header[%08x %08x]\n"
                       "                       begin=%08x %08x %08x %08x\n"
                       "                             %08x %08x %08x %08x\n"
                       "                       end  =%08x %08x %08x %08x",
                       d->prg_cpu[idx]->data[0], d->prg_cpu[idx]->data[1],
                       d->prg_cpu[idx]->begin.control,
                       d->prg_cpu[idx]->begin.address,
                       d->prg_cpu[idx]->begin.branchAddress,
                       d->prg_cpu[idx]->begin.status,
                       d->prg_cpu[idx]->data[0],
                       d->prg_cpu[idx]->data[1],
                       d->prg_cpu[idx]->data[2],
                       d->prg_cpu[idx]->data[3],
                       d->prg_cpu[idx]->end.control,
                       d->prg_cpu[idx]->end.address,
                       d->prg_cpu[idx]->end.branchAddress,
                       d->prg_cpu[idx]->end.status);
                if (d->branchAddrPtr) 
  		        *(d->branchAddrPtr) = cpu_to_le32(d->prg_bus[idx] | 0x3);
                d->branchAddrPtr = &(d->prg_cpu[idx]->end.branchAddress);
        }
	d->free_prgs--;

	/* queue the packet in the appropriate context queue */
	list_add_tail(&packet->driver_list, &d->fifo_list);
	d->prg_ind = (d->prg_ind+1)%d->num_desc;
}

/*
 * This function fills the AT FIFO with the (eventual) pending packets
 * and runs or wakes up the AT DMA prg if necessary.
 *
 * The function MUST be called with the d->lock held.
 */ 
static int dma_trm_flush(struct ti_ohci *ohci, struct dma_trm_ctx *d)
{
	struct hpsb_packet *p;
	int idx,z;

	if (list_empty(&d->pending_list) || d->free_prgs == 0)
		return 0;

	p = driver_packet(d->pending_list.next);
	idx = d->prg_ind;
	z = (p->data_size) ? 3 : 2;

	/* insert the packets into the at dma fifo */
	while (d->free_prgs > 0 && !list_empty(&d->pending_list)) {
		struct hpsb_packet *p = driver_packet(d->pending_list.next);
		list_del(&p->driver_list);
		insert_packet(ohci, d, p);
	}

	if (d->free_prgs == 0)
		PRINT(KERN_INFO, ohci->id, 
		      "Transmit DMA FIFO ctx=%d is full... waiting",d->ctx);

	/* Is the context running ? (should be unless it is 
	   the first packet to be sent in this context) */
	if (!(reg_read(ohci, d->ctrlSet) & 0x8000)) {
		DBGMSG(ohci->id,"Starting transmit DMA ctx=%d",d->ctx);
		reg_write(ohci, d->cmdPtr, d->prg_bus[idx]|z);
		run_context(ohci, d->ctrlSet, NULL);
	}
	else {
		/* Wake up the dma context if necessary */
		if (!(reg_read(ohci, d->ctrlSet) & 0x400)) {
			DBGMSG(ohci->id,"Waking transmit DMA ctx=%d",d->ctx);
			reg_write(ohci, d->ctrlSet, 0x1000);
		}
	}
	return 1;
}

/* Transmission of an async packet */
static int ohci_transmit(struct hpsb_host *host, struct hpsb_packet *packet)
{
	struct ti_ohci *ohci = host->hostdata;
	struct dma_trm_ctx *d;
	unsigned long flags;

	if (packet->data_size > ohci->max_packet_size) {
		PRINT(KERN_ERR, ohci->id, 
		      "Transmit packet size %Zd is too big",
		      packet->data_size);
		return 0;
	}

	/* Decide wether we have an iso, a request, or a response packet */
	if (packet->type == hpsb_raw)
		d = &ohci->at_req_context;
	else if (packet->tcode == TCODE_ISO_DATA)
		d = &ohci->it_context;
	else if (packet->tcode & 0x02)
		d = &ohci->at_resp_context;
	else 
		d = &ohci->at_req_context;

	spin_lock_irqsave(&d->lock,flags);

	list_add_tail(&packet->driver_list, &d->pending_list);
	
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
		DBGMSG(ohci->id, "devctl: Bus reset requested%s",
		       attempt_root ? " and attempting to become root" : "");
		set_phy_reg_mask (ohci, 1, 0x40 | (attempt_root ? 0x80 : 0));
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
		dma_trm_reset(&ohci->at_req_context);
		dma_trm_reset(&ohci->at_resp_context);
		break;

	case MODIFY_USAGE:
                if (arg) {
                        MOD_INC_USE_COUNT;
                } else {
                        MOD_DEC_USE_COUNT;
                }
		retval = 1;
                break;

	case ISO_LISTEN_CHANNEL:
        {
		u64 mask;

		if (arg<0 || arg>63) {
			PRINT(KERN_ERR, ohci->id,
			      "%s: IS0 listen channel %d is out of range", 
			      __FUNCTION__, arg);
			return -EFAULT;
		}

		mask = (u64)0x1<<arg;
		
                spin_lock_irqsave(&ohci->IR_channel_lock, flags);

		if (ohci->ISO_channel_usage & mask) {
			PRINT(KERN_ERR, ohci->id,
			      "%s: IS0 listen channel %d is already used", 
			      __FUNCTION__, arg);
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
                DBGMSG(ohci->id, "Listening enabled on channel %d", arg);
                break;
        }
	case ISO_UNLISTEN_CHANNEL:
        {
		u64 mask;

		if (arg<0 || arg>63) {
			PRINT(KERN_ERR, ohci->id,
			      "%s: IS0 unlisten channel %d is out of range", 
			      __FUNCTION__, arg);
			return -EFAULT;
		}

		mask = (u64)0x1<<arg;
		
                spin_lock_irqsave(&ohci->IR_channel_lock, flags);

		if (!(ohci->ISO_channel_usage & mask)) {
			PRINT(KERN_ERR, ohci->id,
			      "%s: IS0 unlisten channel %d is not used", 
			      __FUNCTION__, arg);
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
                DBGMSG(ohci->id, "Listening disabled on channel %d", arg);
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
	unsigned long flags;
	LIST_HEAD(packet_list);

	ohci1394_stop_context(d->ohci, d->ctrlClear, NULL);

	/* Lock the context, reset it and release it. Move the packets
	 * that were pending in the context to packet_list and free
	 * them after releasing the lock. */

	spin_lock_irqsave(&d->lock, flags);

	list_splice(&d->fifo_list, &packet_list);
	list_splice(&d->pending_list, &packet_list);
	INIT_LIST_HEAD(&d->fifo_list);
	INIT_LIST_HEAD(&d->pending_list);

	d->branchAddrPtr = NULL;
	d->sent_ind = d->prg_ind;
	d->free_prgs = d->num_desc;

	spin_unlock_irqrestore(&d->lock, flags);

	/* Now process subsystem callbacks for the packets from the
	 * context. */

	while (!list_empty(&packet_list)) {
		struct hpsb_packet *p = driver_packet(packet_list.next);
		PRINT(KERN_INFO, d->ohci->id, 
		      "AT dma reset ctx=%d, aborting transmission", d->ctx);
		list_del(&p->driver_list);
		hpsb_packet_sent(d->ohci->host, p, ACKX_ABORTED);
	}
}

static void ohci_schedule_iso_tasklets(struct ti_ohci *ohci, 
				       quadlet_t rx_event,
				       quadlet_t tx_event)
{
	struct list_head *lh;
	struct ohci1394_iso_tasklet *t;
	unsigned long mask;

	spin_lock(&ohci->iso_tasklet_list_lock);

	list_for_each(lh, &ohci->iso_tasklet_list) {
		t = list_entry(lh, struct ohci1394_iso_tasklet, link);
		mask = 1 << t->context;

		if (t->type == OHCI_ISO_TRANSMIT && tx_event & mask)
			tasklet_schedule(&t->tasklet);
		if (t->type == OHCI_ISO_RECEIVE && rx_event & mask)
			tasklet_schedule(&t->tasklet);
	}

	spin_unlock(&ohci->iso_tasklet_list_lock);

}

static void ohci_irq_handler(int irq, void *dev_id,
                             struct pt_regs *regs_are_unused)
{
	quadlet_t event, node_id;
	struct ti_ohci *ohci = (struct ti_ohci *)dev_id;
	struct hpsb_host *host = ohci->host;
	int phyid = -1, isroot = 0;
	unsigned long flags;

	/* Read and clear the interrupt event register.  Don't clear
	 * the busReset event, though, this is done when we get the
	 * selfIDComplete interrupt. */
	spin_lock_irqsave(&ohci->event_lock, flags);
	event = reg_read(ohci, OHCI1394_IntEventClear);
	reg_write(ohci, OHCI1394_IntEventClear, event & ~OHCI1394_busReset);
	spin_unlock_irqrestore(&ohci->event_lock, flags);

	if (!event) return;

	DBGMSG(ohci->id, "IntEvent: %08x", event);

	/* Die right here an now */
	if (event & OHCI1394_unrecoverableError) {
		PRINT(KERN_ERR, ohci->id, "Unrecoverable error, shutting down card!");
		return;
	}

	if (event & OHCI1394_cycleInconsistent) {
		/* We subscribe to the cycleInconsistent event only to
		 * clear the corresponding event bit... otherwise,
		 * isochronous cycleMatch DMA wont work. */
		DBGMSG(ohci->id, "OHCI1394_cycleInconsistent");
		event &= ~OHCI1394_cycleInconsistent;
	}

	if (event & OHCI1394_busReset) {
		/* The busReset event bit can't be cleared during the
		 * selfID phase, so we disable busReset interrupts, to
		 * avoid burying the cpu in interrupt requests. */
		spin_lock_irqsave(&ohci->event_lock, flags);
  		reg_write(ohci, OHCI1394_IntMaskClear, OHCI1394_busReset);
		if (ohci->dev->vendor == PCI_VENDOR_ID_APPLE && 
		    ohci->dev->device == PCI_DEVICE_ID_APPLE_UNI_N_FW) {
  			udelay(10);
  			while(reg_read(ohci, OHCI1394_IntEventSet) & OHCI1394_busReset) {
  				reg_write(ohci, OHCI1394_IntEventClear, OHCI1394_busReset);
				spin_unlock_irqrestore(&ohci->event_lock, flags);
	  			udelay(10);
				spin_lock_irqsave(&ohci->event_lock, flags);
  			}
  		}
		spin_unlock_irqrestore(&ohci->event_lock, flags);
		if (!host->in_bus_reset) {
			DBGMSG(ohci->id, "irq_handler: Bus reset requested%s",
			      (attempt_root) ? " and attempting to become root"
			       : "");

			/* Subsystem call */
			hpsb_bus_reset(ohci->host);
		}
		event &= ~OHCI1394_busReset;
	}

	/* XXX: We need a way to also queue the OHCI1394_reqTxComplete,
	 * but for right now we simply run it upon reception, to make sure
	 * we get sent acks before response packets. This sucks mainly
	 * because it halts the interrupt handler.  */
	if (event & OHCI1394_reqTxComplete) {
		struct dma_trm_ctx *d = &ohci->at_req_context;
		DBGMSG(ohci->id, "Got reqTxComplete interrupt "
		       "status=0x%08X", reg_read(ohci, d->ctrlSet));
		if (reg_read(ohci, d->ctrlSet) & 0x800)
			ohci1394_stop_context(ohci, d->ctrlClear,
					      "reqTxComplete");
		else
			dma_trm_tasklet ((unsigned long)d);
		event &= ~OHCI1394_reqTxComplete;
	}
	if (event & OHCI1394_respTxComplete) {
		struct dma_trm_ctx *d = &ohci->at_resp_context;
		DBGMSG(ohci->id, "Got respTxComplete interrupt "
		       "status=0x%08X", reg_read(ohci, d->ctrlSet));
		if (reg_read(ohci, d->ctrlSet) & 0x800)
			ohci1394_stop_context(ohci, d->ctrlClear,
					      "respTxComplete");
		else
			tasklet_schedule(&d->task);
		event &= ~OHCI1394_respTxComplete;
	}
	if (event & OHCI1394_RQPkt) {
		struct dma_rcv_ctx *d = &ohci->ar_req_context;
		DBGMSG(ohci->id, "Got RQPkt interrupt status=0x%08X",
		       reg_read(ohci, d->ctrlSet));
		if (reg_read(ohci, d->ctrlSet) & 0x800)
			ohci1394_stop_context(ohci, d->ctrlClear, "RQPkt");
		else
			tasklet_schedule(&d->task);
		event &= ~OHCI1394_RQPkt;
	}
	if (event & OHCI1394_RSPkt) {
		struct dma_rcv_ctx *d = &ohci->ar_resp_context;
		DBGMSG(ohci->id, "Got RSPkt interrupt status=0x%08X",
		       reg_read(ohci, d->ctrlSet));
		if (reg_read(ohci, d->ctrlSet) & 0x800)
			ohci1394_stop_context(ohci, d->ctrlClear, "RSPkt");
		else
			tasklet_schedule(&d->task);
		event &= ~OHCI1394_RSPkt;
	}
	if (event & OHCI1394_isochRx) {
		quadlet_t rx_event;

		rx_event = reg_read(ohci, OHCI1394_IsoRecvIntEventSet);
		reg_write(ohci, OHCI1394_IsoRecvIntEventClear, rx_event);
		ohci_schedule_iso_tasklets(ohci, rx_event, 0);
		event &= ~OHCI1394_isochRx;
	}
	if (event & OHCI1394_isochTx) {
		quadlet_t tx_event;		

		tx_event = reg_read(ohci, OHCI1394_IsoXmitIntEventSet);
		reg_write(ohci, OHCI1394_IsoXmitIntEventClear, tx_event);
		ohci_schedule_iso_tasklets(ohci, 0, tx_event);
		event &= ~OHCI1394_isochTx;
	}
	if (event & OHCI1394_selfIDComplete) {
		if (host->in_bus_reset) {
			node_id = reg_read(ohci, OHCI1394_NodeID); 

			/* If our nodeid is not valid, give a msec delay
			 * to let it settle in and try again.  */
			if (!(node_id & 0x80000000)) {
				mdelay(1);
				node_id = reg_read(ohci, OHCI1394_NodeID);
			}

			if (node_id & 0x80000000) { /* NodeID valid */
				phyid =  node_id & 0x0000003f;
				isroot = (node_id & 0x40000000) != 0;

				DBGMSG(ohci->id,
				      "SelfID interrupt received "
				      "(phyid %d, %s)", phyid, 
				      (isroot ? "root" : "not root"));

				handle_selfid(ohci, host, 
					      phyid, isroot);
			} else {
				PRINT(KERN_ERR, ohci->id, 
				      "SelfID interrupt received, but "
				      "NodeID is not valid: %08X",
				      node_id);
			}

			/* Accept Physical requests from all nodes. */
			reg_write(ohci,OHCI1394_AsReqFilterHiSet, 
				  0xffffffff);
			reg_write(ohci,OHCI1394_AsReqFilterLoSet, 
				  0xffffffff);
		} else
			PRINT(KERN_ERR, ohci->id, 
			      "SelfID received outside of bus reset sequence");

		/* Finally, we clear the busReset event and reenable
		 * the busReset interrupt. */
		spin_lock_irqsave(&ohci->event_lock, flags);
		reg_write(ohci, OHCI1394_IntEventClear, OHCI1394_busReset);
		reg_write(ohci, OHCI1394_IntMaskSet, OHCI1394_busReset); 
		spin_unlock_irqrestore(&ohci->event_lock, flags);
		event &= ~OHCI1394_selfIDComplete;	

		/* Turn on phys dma reception. We should
		 * probably manage the filtering somehow, 
		 * instead of blindly turning it on.  */

		/*
		 * CAUTION!
		 * Some chips (TI TSB43AB22) won't take a value in
		 * the PhyReqFilter register until after the IntEvent
		 * is cleared for bus reset, and even then a short
		 * delay is required.
		 */
		if (phys_dma) {
			mdelay(1);
			reg_write(ohci,OHCI1394_PhyReqFilterHiSet,
				  0xffffffff);
			reg_write(ohci,OHCI1394_PhyReqFilterLoSet,
				  0xffffffff);
			reg_write(ohci,OHCI1394_PhyUpperBound,
				  0xffff0000);
		}

		DBGMSG(ohci->id, "PhyReqFilter=%08x%08x\n",
		       reg_read(ohci,OHCI1394_PhyReqFilterHiSet),
		       reg_read(ohci,OHCI1394_PhyReqFilterLoSet));
	}

	/* Make sure we handle everything, just in case we accidentally
	 * enabled an interrupt that we didn't write a handler for.  */
	if (event)
		PRINT(KERN_ERR, ohci->id, "Unhandled interrupt(s) 0x%08x",
		      event);
}

/* Put the buffer back into the dma context */
static void insert_dma_buffer(struct dma_rcv_ctx *d, int idx)
{
	struct ti_ohci *ohci = (struct ti_ohci*)(d->ohci);
	DBGMSG(ohci->id, "Inserting dma buf ctx=%d idx=%d", d->ctx, idx);

	d->prg_cpu[idx]->status = cpu_to_le32(d->buf_size);
	d->prg_cpu[idx]->branchAddress &= le32_to_cpu(0xfffffff0);
	idx = (idx + d->num_desc - 1 ) % d->num_desc;
	d->prg_cpu[idx]->branchAddress |= le32_to_cpu(0x00000001);

	/* wake up the dma context if necessary */
	if (!(reg_read(ohci, d->ctrlSet) & 0x400)) {
		PRINT(KERN_INFO, ohci->id, 
		      "Waking dma ctx=%d ... processing is probably too slow",
		      d->ctx);
		reg_write(ohci, d->ctrlSet, 0x1000);
	}
}

#define cond_le32_to_cpu(data, noswap) \
	(noswap ? data : le32_to_cpu(data))

static const int TCODE_SIZE[16] = {20, 0, 16, -1, 16, 20, 20, 0, 
			    -1, 0, -1, 0, -1, -1, 16, -1};

/* 
 * Determine the length of a packet in the buffer
 * Optimization suggested by Pascal Drolet <pascal.drolet@informission.ca>
 */
static __inline__ int packet_length(struct dma_rcv_ctx *d, int idx, quadlet_t *buf_ptr,
			 int offset, unsigned char tcode, int noswap)
{
	int length = -1;

	if (d->type == DMA_CTX_ASYNC_REQ || d->type == DMA_CTX_ASYNC_RESP) {
		length = TCODE_SIZE[tcode];
		if (length == 0) {
			if (offset + 12 >= d->buf_size) {
				length = (cond_le32_to_cpu(d->buf_cpu[(idx + 1) % d->num_desc]
						[3 - ((d->buf_size - offset) >> 2)], noswap) >> 16);
			} else {
				length = (cond_le32_to_cpu(buf_ptr[3], noswap) >> 16);
			}
			length += 20;
		}
	} else if (d->type == DMA_CTX_ISO) {
		/* Assumption: buffer fill mode with header/trailer */
		length = (cond_le32_to_cpu(buf_ptr[0], noswap) >> 16) + 8;
	}

	if (length > 0 && length % 4)
		length += 4 - (length % 4);

	return length;
}

/* Tasklet that processes dma receive buffers */
static void dma_rcv_tasklet (unsigned long data)
{
	struct dma_rcv_ctx *d = (struct dma_rcv_ctx*)data;
	struct ti_ohci *ohci = (struct ti_ohci*)(d->ohci);
	unsigned int split_left, idx, offset, rescount;
	unsigned char tcode;
	int length, bytes_left, ack;
	unsigned long flags;
	quadlet_t *buf_ptr;
	char *split_ptr;
	char msg[256];

	spin_lock_irqsave(&d->lock, flags);

	idx = d->buf_ind;
	offset = d->buf_offset;
	buf_ptr = d->buf_cpu[idx] + offset/4;

	rescount = le32_to_cpu(d->prg_cpu[idx]->status) & 0xffff;
	bytes_left = d->buf_size - rescount - offset;

	while (bytes_left > 0) {
		tcode = (cond_le32_to_cpu(buf_ptr[0], ohci->no_swap_incoming) >> 4) & 0xf;

		/* packet_length() will return < 4 for an error */
		length = packet_length(d, idx, buf_ptr, offset, tcode, ohci->no_swap_incoming);

		if (length < 4) { /* something is wrong */
			sprintf(msg,"Unexpected tcode 0x%x(0x%08x) in AR ctx=%d, length=%d",
				tcode, cond_le32_to_cpu(buf_ptr[0], ohci->no_swap_incoming),
				d->ctx, length);
			ohci1394_stop_context(ohci, d->ctrlClear, msg);
			spin_unlock_irqrestore(&d->lock, flags);
			return;
		}

		/* The first case is where we have a packet that crosses
		 * over more than one descriptor. The next case is where
		 * it's all in the first descriptor.  */
		if ((offset + length) > d->buf_size) {
			DBGMSG(ohci->id,"Split packet rcv'd");
			if (length > d->split_buf_size) {
				ohci1394_stop_context(ohci, d->ctrlClear,
					     "Split packet size exceeded");
				d->buf_ind = idx;
				d->buf_offset = offset;
				spin_unlock_irqrestore(&d->lock, flags);
				return;
			}

			if (le32_to_cpu(d->prg_cpu[(idx+1)%d->num_desc]->status)
			    == d->buf_size) {
				/* Other part of packet not written yet.
				 * this should never happen I think
				 * anyway we'll get it on the next call.  */
				PRINT(KERN_INFO, ohci->id,
				      "Got only half a packet!");
				d->buf_ind = idx;
				d->buf_offset = offset;
				spin_unlock_irqrestore(&d->lock, flags);
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

			if (split_left > 0) {
				memcpy(split_ptr, buf_ptr, split_left);
				offset = split_left;
				buf_ptr += offset/4;
			}
		} else {
			DBGMSG(ohci->id,"Single packet rcv'd");
			memcpy(d->spb, buf_ptr, length);
			offset += length;
			buf_ptr += length/4;
			if (offset==d->buf_size) {
				insert_dma_buffer(d, idx);
				idx = (idx+1) % d->num_desc;
				buf_ptr = d->buf_cpu[idx];
				offset=0;
			}
		}
		
		/* We get one phy packet to the async descriptor for each
		 * bus reset. We always ignore it.  */
		if (tcode != OHCI1394_TCODE_PHY) {
			if (!ohci->no_swap_incoming)
				packet_swab(d->spb, tcode, (length - 4) >> 2);
			DBGMSG(ohci->id, "Packet received from node"
				" %d ack=0x%02X spd=%d tcode=0x%X"
				" length=%d ctx=%d tlabel=%d",
				(d->spb[1]>>16)&0x3f,
				(cond_le32_to_cpu(d->spb[length/4-1], ohci->no_swap_incoming)>>16)&0x1f,
				(cond_le32_to_cpu(d->spb[length/4-1], ohci->no_swap_incoming)>>21)&0x3,
				tcode, length, d->ctx,
				(cond_le32_to_cpu(d->spb[length/4-1], ohci->no_swap_incoming)>>10)&0x3f);

			ack = (((cond_le32_to_cpu(d->spb[length/4-1], ohci->no_swap_incoming)>>16)&0x1f)
				== 0x11) ? 1 : 0;

			hpsb_packet_received(ohci->host, d->spb, 
					     length-4, ack);
		}
#ifdef OHCI1394_DEBUG
		else
			PRINT (KERN_DEBUG, ohci->id, "Got phy packet ctx=%d ... discarded",
			       d->ctx);
#endif

	       	rescount = le32_to_cpu(d->prg_cpu[idx]->status) & 0xffff;

		bytes_left = d->buf_size - rescount - offset;

	}

	d->buf_ind = idx;
	d->buf_offset = offset;

	spin_unlock_irqrestore(&d->lock, flags);
}

/* Bottom half that processes sent packets */
static void dma_trm_tasklet (unsigned long data)
{
	struct dma_trm_ctx *d = (struct dma_trm_ctx*)data;
	struct ti_ohci *ohci = (struct ti_ohci*)(d->ohci);
	struct hpsb_packet *packet;
	unsigned long flags;
	u32 ack;
        size_t datasize;

	spin_lock_irqsave(&d->lock, flags);

	while (!list_empty(&d->fifo_list)) {
		packet = driver_packet(d->fifo_list.next);
                datasize = packet->data_size;
		if (datasize && packet->type != hpsb_raw)
			ack = le32_to_cpu(
				d->prg_cpu[d->sent_ind]->end.status) >> 16;
		else 
			ack = le32_to_cpu(
				d->prg_cpu[d->sent_ind]->begin.status) >> 16;

		if (ack == 0) 
			/* this packet hasn't been sent yet*/
			break;

		if (!(ack & 0x10)) {
			/* XXX: This is an OHCI evt_* code. We need to handle
			 * this specially! For right now, we just fake an
			 * ackx_send_error. */
			PRINT(KERN_DEBUG, ohci->id, "Received OHCI evt_* error 0x%x",
			       ack & 0xf);
			ack = (ack & 0xffe0) | ACK_BUSY_A;
		}

#ifdef OHCI1394_DEBUG
		if (datasize)
			DBGMSG(ohci->id,
			       "Packet sent to node %d tcode=0x%X tLabel="
			       "0x%02X ack=0x%X spd=%d dataLength=%d ctx=%d", 
                                (le32_to_cpu(d->prg_cpu[d->sent_ind]->data[1])
                                        >>16)&0x3f,
                                (le32_to_cpu(d->prg_cpu[d->sent_ind]->data[0])
                                        >>4)&0xf,
                                (le32_to_cpu(d->prg_cpu[d->sent_ind]->data[0])
                                        >>10)&0x3f,
                                ack&0x1f, (ack>>5)&0x3, 
                                le32_to_cpu(d->prg_cpu[d->sent_ind]->data[3])
                                        >>16,
                                d->ctx);
		else 
			DBGMSG(ohci->id,
			       "Packet sent to node %d tcode=0x%X tLabel="
			       "0x%02X ack=0x%X spd=%d data=0x%08X ctx=%d", 
                                (le32_to_cpu(d->prg_cpu[d->sent_ind]->data[1])
                                        >>16)&0x3f,
                                (le32_to_cpu(d->prg_cpu[d->sent_ind]->data[0])
                                        >>4)&0xf,
                                (le32_to_cpu(d->prg_cpu[d->sent_ind]->data[0])
                                        >>10)&0x3f,
                                ack&0x1f, (ack>>5)&0x3, 
                                le32_to_cpu(d->prg_cpu[d->sent_ind]->data[3]),
                                d->ctx);
#endif		

                list_del(&packet->driver_list);
		hpsb_packet_sent(ohci->host, packet, ack & 0xf);

		if (datasize) {
			pci_unmap_single(ohci->dev, 
					 cpu_to_le32(d->prg_cpu[d->sent_ind]->end.address),
					 datasize, PCI_DMA_TODEVICE);
			OHCI_DMA_FREE("single Xmit data packet");
		}

		d->sent_ind = (d->sent_ind+1)%d->num_desc;
		d->free_prgs++;
	}

	dma_trm_flush(ohci, d);

	spin_unlock_irqrestore(&d->lock, flags);
}

static void free_dma_rcv_ctx(struct dma_rcv_ctx *d)
{
	int i;

	if (d->ohci == NULL)
		return;

	DBGMSG(d->ohci->id, "Freeing dma_rcv_ctx %d", d->ctx);
	
	ohci1394_stop_context(d->ohci, d->ctrlClear, NULL);

	if (d->type == DMA_CTX_ISO)
		ohci1394_unregister_iso_tasklet(d->ohci, &d->ohci->ir_tasklet);
	else
		tasklet_kill(&d->task);

	if (d->buf_cpu) {
		for (i=0; i<d->num_desc; i++)
			if (d->buf_cpu[i] && d->buf_bus[i]) {
				pci_free_consistent(
					d->ohci->dev, d->buf_size, 
					d->buf_cpu[i], d->buf_bus[i]);
				OHCI_DMA_FREE("consistent dma_rcv buf[%d]", i);
			}
		kfree(d->buf_cpu);
		kfree(d->buf_bus);
	}
	if (d->prg_cpu) {
		for (i=0; i<d->num_desc; i++) 
			if (d->prg_cpu[i] && d->prg_bus[i]) {
				pci_free_consistent(
					d->ohci->dev, sizeof(struct dma_cmd), 
					d->prg_cpu[i], d->prg_bus[i]);
				OHCI_DMA_FREE("consistent dma_rcv prg[%d]", i);
			}
		kfree(d->prg_cpu);
		kfree(d->prg_bus);
	}
	if (d->spb) kfree(d->spb);

	/* Mark this context as freed. */
	d->ohci = NULL;
}

static int
alloc_dma_rcv_ctx(struct ti_ohci *ohci, struct dma_rcv_ctx *d,
		  enum context_type type, int ctx, int num_desc,
		  int buf_size, int split_buf_size, int context_base)
{
	int i;

	d->ohci = ohci;
	d->type = type;
	d->ctx = ctx;

	d->num_desc = num_desc;
	d->buf_size = buf_size;
	d->split_buf_size = split_buf_size;

	d->ctrlSet = context_base + OHCI1394_ContextControlSet;
	d->ctrlClear = context_base + OHCI1394_ContextControlClear;
	d->cmdPtr = context_base + OHCI1394_ContextCommandPtr;

	d->buf_cpu = kmalloc(d->num_desc * sizeof(quadlet_t*), GFP_KERNEL);
	d->buf_bus = kmalloc(d->num_desc * sizeof(dma_addr_t), GFP_KERNEL);

	if (d->buf_cpu == NULL || d->buf_bus == NULL) {
		PRINT(KERN_ERR, ohci->id, "Failed to allocate dma buffer");
		free_dma_rcv_ctx(d);
		return -ENOMEM;
	}
	memset(d->buf_cpu, 0, d->num_desc * sizeof(quadlet_t*));
	memset(d->buf_bus, 0, d->num_desc * sizeof(dma_addr_t));

	d->prg_cpu = kmalloc(d->num_desc * sizeof(struct dma_cmd*), 
			     GFP_KERNEL);
	d->prg_bus = kmalloc(d->num_desc * sizeof(dma_addr_t), GFP_KERNEL);

	if (d->prg_cpu == NULL || d->prg_bus == NULL) {
		PRINT(KERN_ERR, ohci->id, "Failed to allocate dma prg");
		free_dma_rcv_ctx(d);
		return -ENOMEM;
	}
	memset(d->prg_cpu, 0, d->num_desc * sizeof(struct dma_cmd*));
	memset(d->prg_bus, 0, d->num_desc * sizeof(dma_addr_t));

	d->spb = kmalloc(d->split_buf_size, GFP_KERNEL);

	if (d->spb == NULL) {
		PRINT(KERN_ERR, ohci->id, "Failed to allocate split buffer");
		free_dma_rcv_ctx(d);
		return -ENOMEM;
	}

	for (i=0; i<d->num_desc; i++) {
		d->buf_cpu[i] = pci_alloc_consistent(ohci->dev, 
						     d->buf_size,
						     d->buf_bus+i);
		OHCI_DMA_ALLOC("consistent dma_rcv buf[%d]", i);
		
		if (d->buf_cpu[i] != NULL) {
			memset(d->buf_cpu[i], 0, d->buf_size);
		} else {
			PRINT(KERN_ERR, ohci->id, 
			      "Failed to allocate dma buffer");
			free_dma_rcv_ctx(d);
			return -ENOMEM;
		}

		
                d->prg_cpu[i] = pci_alloc_consistent(ohci->dev, 
						     sizeof(struct dma_cmd),
						     d->prg_bus+i);
		OHCI_DMA_ALLOC("consistent dma_rcv prg[%d]", i);

                if (d->prg_cpu[i] != NULL) {
                        memset(d->prg_cpu[i], 0, sizeof(struct dma_cmd));
		} else {
			PRINT(KERN_ERR, ohci->id, 
			      "Failed to allocate dma prg");
			free_dma_rcv_ctx(d);
			return -ENOMEM;
		}
	}

        spin_lock_init(&d->lock);

	if (type == DMA_CTX_ISO) {
		ohci1394_init_iso_tasklet(&ohci->ir_tasklet, OHCI_ISO_RECEIVE,
					  dma_rcv_tasklet, (unsigned long) d);
		if (ohci1394_register_iso_tasklet(ohci,
						  &ohci->ir_tasklet) < 0) {
			PRINT(KERN_ERR, ohci->id, "No IR DMA context available");
			free_dma_rcv_ctx(d);
			return -EBUSY;
		}
	}
	else
		tasklet_init (&d->task, dma_rcv_tasklet, (unsigned long) d);

	return 0;
}

static void free_dma_trm_ctx(struct dma_trm_ctx *d)
{
	int i;

	if (d->ohci == NULL)
		return;

	DBGMSG(d->ohci->id, "Freeing dma_trm_ctx %d", d->ctx);

	ohci1394_stop_context(d->ohci, d->ctrlClear, NULL);

	if (d->type == DMA_CTX_ISO)
		ohci1394_unregister_iso_tasklet(d->ohci, &d->ohci->it_tasklet);
	else
		tasklet_kill(&d->task);

	if (d->prg_cpu) {
		for (i=0; i<d->num_desc; i++) 
			if (d->prg_cpu[i] && d->prg_bus[i]) {
				pci_free_consistent(
					d->ohci->dev, sizeof(struct at_dma_prg), 
					d->prg_cpu[i], d->prg_bus[i]);
				OHCI_DMA_FREE("consistent dma_trm prg[%d]", i);
			}
		kfree(d->prg_cpu);
		kfree(d->prg_bus);
	}

	/* Mark this context as freed. */
	d->ohci = NULL;
}

static int
alloc_dma_trm_ctx(struct ti_ohci *ohci, struct dma_trm_ctx *d,
		  enum context_type type, int ctx, int num_desc,
		  int context_base)
{
	int i;

	d->ohci = ohci;
	d->type = type;
	d->ctx = ctx;
	d->num_desc = num_desc;
	d->ctrlSet = context_base + OHCI1394_ContextControlSet;
	d->ctrlClear = context_base + OHCI1394_ContextControlClear;
	d->cmdPtr = context_base + OHCI1394_ContextCommandPtr;

	d->prg_cpu = kmalloc(d->num_desc * sizeof(struct at_dma_prg*), 
			     GFP_KERNEL);
	d->prg_bus = kmalloc(d->num_desc * sizeof(dma_addr_t), GFP_KERNEL);

	if (d->prg_cpu == NULL || d->prg_bus == NULL) {
		PRINT(KERN_ERR, ohci->id, "Failed to allocate at dma prg");
		free_dma_trm_ctx(d);
		return -ENOMEM;
	}
	memset(d->prg_cpu, 0, d->num_desc * sizeof(struct at_dma_prg*));
	memset(d->prg_bus, 0, d->num_desc * sizeof(dma_addr_t));

	for (i = 0; i < d->num_desc; i++) {
                d->prg_cpu[i] = pci_alloc_consistent(ohci->dev, 
						     sizeof(struct at_dma_prg),
						     d->prg_bus+i);
		OHCI_DMA_ALLOC("consistent dma_trm prg[%d]", i);

                if (d->prg_cpu[i] != NULL) {
                        memset(d->prg_cpu[i], 0, sizeof(struct at_dma_prg));
		} else {
			PRINT(KERN_ERR, ohci->id, 
			      "Failed to allocate at dma prg");
			free_dma_trm_ctx(d);
			return -ENOMEM;
		}
	}

        spin_lock_init(&d->lock);

	/* initialize tasklet */
	if (type == DMA_CTX_ISO) {
		ohci1394_init_iso_tasklet(&ohci->it_tasklet, OHCI_ISO_TRANSMIT,
					  dma_rcv_tasklet, (unsigned long) d);
		if (ohci1394_register_iso_tasklet(ohci,
						  &ohci->it_tasklet) < 0) {
			PRINT(KERN_ERR, ohci->id, "No IT DMA context available");
			free_dma_trm_ctx(d);
			return -EBUSY;
		}
	}
	else
		tasklet_init (&d->task, dma_trm_tasklet, (unsigned long)d);

	return 0;
}

static u16 ohci_crc16 (u32 *ptr, int length)
{
	int shift;
	u32 crc, sum, data;

	crc = 0;
	for (; length > 0; length--) {
		data = be32_to_cpu(*ptr++);
		for (shift = 28; shift >= 0; shift -= 4) {
			sum = ((crc >> 12) ^ (data >> shift)) & 0x000f;
			crc = (crc << 4) ^ (sum << 12) ^ (sum << 5) ^ sum;
		}
		crc &= 0xffff;
	}
	return crc;
}

/* Config ROM macro implementation influenced by NetBSD OHCI driver */

struct config_rom_unit {
	u32 *start;
	u32 *refer;
	int length;
	int refunit;
};

struct config_rom_ptr {
	u32 *data;
	int unitnum;
	struct config_rom_unit unitdir[10];
};

#define cf_put_1quad(cr, q) (((cr)->data++)[0] = cpu_to_be32(q))

#define cf_put_4bytes(cr, b1, b2, b3, b4) \
	(((cr)->data++)[0] = cpu_to_be32(((b1) << 24) | ((b2) << 16) | ((b3) << 8) | (b4)))

#define cf_put_keyval(cr, key, val) (((cr)->data++)[0] = cpu_to_be32(((key) << 24) | (val)))

static inline void cf_put_str(struct config_rom_ptr *cr, const char *str)
{
	int t;
	char fourb[4];

	while (str[0]) {
		memset(fourb, 0, 4);
		for (t = 0; t < 4 && str[t]; t++)
			fourb[t] = str[t];
		cf_put_4bytes(cr, fourb[0], fourb[1], fourb[2], fourb[3]);
		str += strlen(str) < 4 ? strlen(str) : 4;
	}
	return;
}

static inline void cf_put_crc16(struct config_rom_ptr *cr, int unit)
{
	*cr->unitdir[unit].start =
		cpu_to_be32((cr->unitdir[unit].length << 16) |
			    ohci_crc16(cr->unitdir[unit].start + 1,
				       cr->unitdir[unit].length));
}

static inline void cf_unit_begin(struct config_rom_ptr *cr, int unit)
{
	if (cr->unitdir[unit].refer != NULL) {
		*cr->unitdir[unit].refer |=
			cpu_to_be32 (cr->data - cr->unitdir[unit].refer);
		cf_put_crc16(cr, cr->unitdir[unit].refunit);
	}
	cr->unitnum = unit;
	cr->unitdir[unit].start = cr->data++;
}

static inline void cf_put_refer(struct config_rom_ptr *cr, char key, int unit)
{
	cr->unitdir[unit].refer = cr->data;
	cr->unitdir[unit].refunit = cr->unitnum;
	(cr->data++)[0] = cpu_to_be32(key << 24);
}

static inline void cf_unit_end(struct config_rom_ptr *cr)
{
	cr->unitdir[cr->unitnum].length = cr->data -
		(cr->unitdir[cr->unitnum].start + 1);
	cf_put_crc16(cr, cr->unitnum);
}

/* End of NetBSD derived code.  */

static void ohci_init_config_rom(struct ti_ohci *ohci)
{
	struct config_rom_ptr cr;

	memset(&cr, 0, sizeof(cr));
	memset(ohci->csr_config_rom_cpu, 0, sizeof (ohci->csr_config_rom_cpu));

	cr.data = ohci->csr_config_rom_cpu;

	/* Bus info block */
	cf_unit_begin(&cr, 0);
	cf_put_1quad(&cr, reg_read(ohci, OHCI1394_BusID));
	cf_put_1quad(&cr, reg_read(ohci, OHCI1394_BusOptions));
	cf_put_1quad(&cr, reg_read(ohci, OHCI1394_GUIDHi));
	cf_put_1quad(&cr, reg_read(ohci, OHCI1394_GUIDLo));
	cf_unit_end(&cr);

	DBGMSG(ohci->id, "GUID: %08x:%08x", reg_read(ohci, OHCI1394_GUIDHi),
		reg_read(ohci, OHCI1394_GUIDLo));

	/* IEEE P1212 suggests the initial ROM header CRC should only
	 * cover the header itself (and not the entire ROM). Since we do
	 * this, then we can make our bus_info_len the same as the CRC
	 * length.  */
	ohci->csr_config_rom_cpu[0] |= cpu_to_be32(
		(be32_to_cpu(ohci->csr_config_rom_cpu[0]) & 0x00ff0000) << 8);
	reg_write(ohci, OHCI1394_ConfigROMhdr,
		  be32_to_cpu(ohci->csr_config_rom_cpu[0]));

	/* Root directory */
	cf_unit_begin(&cr, 1);
	/* Vendor ID */
	cf_put_keyval(&cr, 0x03, reg_read(ohci,OHCI1394_VendorID) & 0xFFFFFF);
	cf_put_refer(&cr, 0x81, 2);		/* Textual description unit */
	cf_put_keyval(&cr, 0x0c, 0x0083c0);	/* Node capabilities */
	/* NOTE: Add other unit referers here, and append at bottom */
	cf_unit_end(&cr);

	/* Textual description - "Linux 1394" */
	cf_unit_begin(&cr, 2);
	cf_put_keyval(&cr, 0, 0);
	cf_put_1quad(&cr, 0);
	cf_put_str(&cr, "Linux OHCI-1394");
	cf_unit_end(&cr);

	ohci->csr_config_rom_length = cr.data - ohci->csr_config_rom_cpu;
}

static size_t ohci_get_rom(struct hpsb_host *host, const quadlet_t **ptr)
{
	struct ti_ohci *ohci=host->hostdata;

	DBGMSG(ohci->id, "request csr_rom address: %p",
		ohci->csr_config_rom_cpu);

	*ptr = ohci->csr_config_rom_cpu;

	return ohci->csr_config_rom_length * 4;
}

static quadlet_t ohci_hw_csr_reg(struct hpsb_host *host, int reg,
                                 quadlet_t data, quadlet_t compare)
{
	struct ti_ohci *ohci = host->hostdata;
	int i;

	reg_write(ohci, OHCI1394_CSRData, data);
	reg_write(ohci, OHCI1394_CSRCompareData, compare);
	reg_write(ohci, OHCI1394_CSRControl, reg & 0x3);

	for (i = 0; i < OHCI_LOOP_COUNT; i++) {
		if (reg_read(ohci, OHCI1394_CSRControl) & 0x80000000)
			break;

		mdelay(1);
	}

	return reg_read(ohci, OHCI1394_CSRData);
}

static struct hpsb_host_driver ohci1394_driver = {
	.name =			OHCI1394_DRIVER_NAME,
	.get_rom =		ohci_get_rom,
	.transmit_packet =	ohci_transmit,
	.devctl =		ohci_devctl,
	.hw_csr_reg =		ohci_hw_csr_reg,
};



/***********************************
 * PCI Driver Interface functions  *
 ***********************************/

#define FAIL(err, fmt, args...)			\
do {						\
	PRINT_G(KERN_ERR, fmt , ## args);	\
        ohci1394_pci_remove(dev);               \
	return err;				\
} while(0)

static int __devinit ohci1394_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	static unsigned int card_id_counter = 0;
	static int version_printed = 0;

	struct hpsb_host *host;
	struct ti_ohci *ohci;	/* shortcut to currently handled device */
	unsigned long ohci_base;
	
	if (version_printed++ == 0)
		PRINT_G(KERN_INFO, "%s", version);

        if (pci_enable_device(dev))
		FAIL(-ENXIO, "Failed to enable OHCI hardware %d",
		        card_id_counter++);
        pci_set_master(dev);

	host = hpsb_alloc_host(&ohci1394_driver, sizeof(struct ti_ohci));
	if (!host) FAIL(-ENOMEM, "Failed to allocate host structure");

	ohci = host->hostdata;
	ohci->id = card_id_counter++;
	ohci->dev = dev;
	ohci->host = host;
	ohci->init_state = OHCI_INIT_ALLOC_HOST;
	host->pdev = dev;
	pci_set_drvdata(dev, ohci);

	/* We don't want hardware swapping */
	pci_write_config_dword(dev, OHCI1394_PCI_HCI_Control, 0);

	/* Some oddball Apple controllers do not order the selfid
	 * properly, so we make up for it here.  */
#ifndef __LITTLE_ENDIAN
	/* XXX: Need a better way to check this. I'm wondering if we can
	 * read the values of the OHCI1394_PCI_HCI_Control and the
	 * noByteSwapData registers to see if they were not cleared to
	 * zero. Should this work? Obviously it's not defined what these
	 * registers will read when they aren't supported. Bleh! */
	if (dev->vendor == PCI_VENDOR_ID_APPLE && 
	    dev->device == PCI_DEVICE_ID_APPLE_UNI_N_FW) {
		ohci->no_swap_incoming = 1;
		ohci->selfid_swap = 0;
	} else
		ohci->selfid_swap = 1;
#endif

	/* We hardwire the MMIO length, since some CardBus adaptors
	 * fail to report the right length.  Anyway, the ohci spec
	 * clearly says it's 2kb, so this shouldn't be a problem. */ 
	ohci_base = pci_resource_start(dev, 0);
	if (pci_resource_len(dev, 0) != OHCI1394_REGISTER_SIZE)
		PRINT(KERN_WARNING, ohci->id, "Unexpected PCI resource length of %lx!",
		      pci_resource_len(dev, 0));

	/* Seems PCMCIA handles this internally. Not sure why. Seems
	 * pretty bogus to force a driver to special case this.  */
#ifndef PCMCIA
	if (!request_mem_region (ohci_base, OHCI1394_REGISTER_SIZE, OHCI1394_DRIVER_NAME))
		FAIL(-ENOMEM, "MMIO resource (0x%lx - 0x%lx) unavailable",
		     ohci_base, ohci_base + OHCI1394_REGISTER_SIZE);
#endif
	ohci->init_state = OHCI_INIT_HAVE_MEM_REGION;

	ohci->registers = ioremap(ohci_base, OHCI1394_REGISTER_SIZE);
	if (ohci->registers == NULL)
		FAIL(-ENXIO, "Failed to remap registers - card not accessible");
	ohci->init_state = OHCI_INIT_HAVE_IOMAPPING;
	DBGMSG(ohci->id, "Remapped memory spaces reg 0x%p", ohci->registers);

	/* csr_config rom allocation */
	ohci->csr_config_rom_cpu =
		pci_alloc_consistent(ohci->dev, OHCI_CONFIG_ROM_LEN,
				     &ohci->csr_config_rom_bus);
	OHCI_DMA_ALLOC("consistent csr_config_rom");
	if (ohci->csr_config_rom_cpu == NULL)
		FAIL(-ENOMEM, "Failed to allocate buffer config rom");
	ohci->init_state = OHCI_INIT_HAVE_CONFIG_ROM_BUFFER;

	/* self-id dma buffer allocation */
	ohci->selfid_buf_cpu = 
		pci_alloc_consistent(ohci->dev, OHCI1394_SI_DMA_BUF_SIZE,
                      &ohci->selfid_buf_bus);
	OHCI_DMA_ALLOC("consistent selfid_buf");
	
	if (ohci->selfid_buf_cpu == NULL)
		FAIL(-ENOMEM, "Failed to allocate DMA buffer for self-id packets");
	ohci->init_state = OHCI_INIT_HAVE_SELFID_BUFFER;

	if ((unsigned long)ohci->selfid_buf_cpu & 0x1fff)
		PRINT(KERN_INFO, ohci->id, "SelfID buffer %p is not aligned on "
		      "8Kb boundary... may cause problems on some CXD3222 chip", 
		      ohci->selfid_buf_cpu);  

	/* No self-id errors at startup */
	ohci->self_id_errors = 0;

	ohci->init_state = OHCI_INIT_HAVE_TXRX_BUFFERS__MAYBE;
	/* AR DMA request context allocation */
	if (alloc_dma_rcv_ctx(ohci, &ohci->ar_req_context,
			      DMA_CTX_ASYNC_REQ, 0, AR_REQ_NUM_DESC,
			      AR_REQ_BUF_SIZE, AR_REQ_SPLIT_BUF_SIZE,
			      OHCI1394_AsReqRcvContextBase) < 0)
		FAIL(-ENOMEM, "Failed to allocate AR Req context");

	/* AR DMA response context allocation */
	if (alloc_dma_rcv_ctx(ohci, &ohci->ar_resp_context,
			      DMA_CTX_ASYNC_RESP, 0, AR_RESP_NUM_DESC,
			      AR_RESP_BUF_SIZE, AR_RESP_SPLIT_BUF_SIZE,
			      OHCI1394_AsRspRcvContextBase) < 0)
		FAIL(-ENOMEM, "Failed to allocate AR Resp context");

	/* AT DMA request context */
	if (alloc_dma_trm_ctx(ohci, &ohci->at_req_context,
			      DMA_CTX_ASYNC_REQ, 0, AT_REQ_NUM_DESC,
			      OHCI1394_AsReqTrContextBase) < 0)
		FAIL(-ENOMEM, "Failed to allocate AT Req context");

	/* AT DMA response context */
	if (alloc_dma_trm_ctx(ohci, &ohci->at_resp_context,
			      DMA_CTX_ASYNC_RESP, 1, AT_RESP_NUM_DESC,
			      OHCI1394_AsRspTrContextBase) < 0)
		FAIL(-ENOMEM, "Failed to allocate AT Resp context");

	/* Start off with a soft reset, to clear everything to a sane
	 * state. */
	ohci_soft_reset(ohci);

	/* Now enable LPS, which we need in order to start accessing
	 * most of the registers.  In fact, on some cards (ALI M5251),
	 * accessing registers in the SClk domain without LPS enabled
	 * will lock up the machine.  Wait 50msec to make sure we have
	 * full link enabled.  */
	reg_write(ohci, OHCI1394_HCControlSet, 0x00080000);
	mdelay(50);

	/* Determine the number of available IR and IT contexts. */
	ohci->nb_iso_rcv_ctx =
		get_nb_iso_ctx(ohci, OHCI1394_IsoRecvIntMaskSet);
	DBGMSG(ohci->id, "%d iso receive contexts available",
	       ohci->nb_iso_rcv_ctx);

	ohci->nb_iso_xmit_ctx =
		get_nb_iso_ctx(ohci, OHCI1394_IsoXmitIntMaskSet);
	DBGMSG(ohci->id, "%d iso transmit contexts available",
	       ohci->nb_iso_xmit_ctx);

	/* Set the usage bits for non-existent contexts so they can't
	 * be allocated */
	ohci->ir_ctx_usage = ~0 << ohci->nb_iso_rcv_ctx;
	ohci->it_ctx_usage = ~0 << ohci->nb_iso_xmit_ctx;

	INIT_LIST_HEAD(&ohci->iso_tasklet_list);
	spin_lock_init(&ohci->iso_tasklet_list_lock);
	ohci->ISO_channel_usage = 0;
        spin_lock_init(&ohci->IR_channel_lock);

	/* IR DMA context */
	if (alloc_dma_rcv_ctx(ohci, &ohci->ir_context,
			      DMA_CTX_ISO, 0, IR_NUM_DESC,
			      IR_BUF_SIZE, IR_SPLIT_BUF_SIZE,
			      OHCI1394_IsoRcvContextBase) < 0)
		FAIL(-ENOMEM, "Failed to allocate IR context");

	
	/* IT DMA context allocation */
	if (alloc_dma_trm_ctx(ohci, &ohci->it_context,
			      DMA_CTX_ISO, 0, IT_NUM_DESC,
			      OHCI1394_IsoXmitContextBase) < 0)
		FAIL(-ENOMEM, "Failed to allocate IT context");

	if (request_irq(dev->irq, ohci_irq_handler, SA_SHIRQ,
			 OHCI1394_DRIVER_NAME, ohci))
		FAIL(-ENOMEM, "Failed to allocate shared interrupt %d", dev->irq);

	ohci->init_state = OHCI_INIT_HAVE_IRQ;
	ohci_initialize(ohci);

	/* Tell the highlevel this host is ready */
	hpsb_add_host(host);
	ohci->init_state = OHCI_INIT_DONE;

	return 0;
#undef FAIL
}

static void ohci1394_pci_remove(struct pci_dev *pdev)
{
	struct ti_ohci *ohci;

	ohci = pci_get_drvdata(pdev);
	if (!ohci)
		return;

	switch (ohci->init_state) {
	case OHCI_INIT_DONE:
		hpsb_remove_host(ohci->host);

	case OHCI_INIT_HAVE_IRQ:
		/* Soft reset before we start - this disables
		 * interrupts and clears linkEnable and LPS. */
		ohci_soft_reset(ohci);
		free_irq(ohci->dev->irq, ohci);

	case OHCI_INIT_HAVE_TXRX_BUFFERS__MAYBE:
		/* Free AR dma */
		free_dma_rcv_ctx(&ohci->ar_req_context);
		free_dma_rcv_ctx(&ohci->ar_resp_context);

		/* Free AT dma */
		free_dma_trm_ctx(&ohci->at_req_context);
		free_dma_trm_ctx(&ohci->at_resp_context);

		/* Free IR dma */
		free_dma_rcv_ctx(&ohci->ir_context);
		
		/* Free IT dma */
		free_dma_trm_ctx(&ohci->it_context);
	
	case OHCI_INIT_HAVE_SELFID_BUFFER:
		pci_free_consistent(ohci->dev, OHCI1394_SI_DMA_BUF_SIZE, 
				    ohci->selfid_buf_cpu,
				    ohci->selfid_buf_bus);
		OHCI_DMA_FREE("consistent selfid_buf");
		
	case OHCI_INIT_HAVE_CONFIG_ROM_BUFFER:
		pci_free_consistent(ohci->dev, OHCI_CONFIG_ROM_LEN,
				    ohci->csr_config_rom_cpu,
				    ohci->csr_config_rom_bus);
		OHCI_DMA_FREE("consistent csr_config_rom");

	case OHCI_INIT_HAVE_IOMAPPING:
		iounmap(ohci->registers);

	case OHCI_INIT_HAVE_MEM_REGION:
#ifndef PCMCIA
		release_mem_region(pci_resource_start(ohci->dev, 0),
				   OHCI1394_REGISTER_SIZE);
#endif

#ifdef CONFIG_ALL_PPC
	/* On UniNorth, power down the cable and turn off the chip
	 * clock when the module is removed to save power on
	 * laptops. Turning it back ON is done by the arch code when
	 * pci_enable_device() is called */
	{
		struct device_node* of_node;

		of_node = pci_device_to_OF_node(ohci->dev);
		if (of_node) {
			pmac_call_feature(PMAC_FTR_1394_ENABLE, of_node, 0, 0);
			pmac_call_feature(PMAC_FTR_1394_CABLE_POWER, of_node, 0, 0);
		}
	}
#endif /* CONFIG_ALL_PPC */

	case OHCI_INIT_ALLOC_HOST:
		pci_set_drvdata(ohci->dev, NULL);
		hpsb_unref_host(ohci->host);
	}
}

#define PCI_CLASS_FIREWIRE_OHCI     ((PCI_CLASS_SERIAL_FIREWIRE << 8) | 0x10)

static struct pci_device_id ohci1394_pci_tbl[] __devinitdata = {
	{
		.class = 		PCI_CLASS_FIREWIRE_OHCI,
		.class_mask = 	0x00ffffff,
		.vendor =		PCI_ANY_ID,
		.device =		PCI_ANY_ID,
		.subvendor =	PCI_ANY_ID,
		.subdevice =	PCI_ANY_ID,
	},
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, ohci1394_pci_tbl);

static struct pci_driver ohci1394_pci_driver = {
	.name =		OHCI1394_DRIVER_NAME,
	.id_table =	ohci1394_pci_tbl,
	.probe =	ohci1394_pci_probe,
	.remove =	ohci1394_pci_remove,
};



/***********************************
 * OHCI1394 Video Interface        *
 ***********************************/

/* essentially the only purpose of this code is to allow another
   module to hook into ohci's interrupt handler */

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
			      "Runaway loop while stopping context...");
			break;
		}
	}
	if (msg) PRINT(KERN_ERR, ohci->id, "%s: dma prg stopped", msg);
}

void ohci1394_init_iso_tasklet(struct ohci1394_iso_tasklet *tasklet, int type,
			       void (*func)(unsigned long), unsigned long data)
{
	tasklet_init(&tasklet->tasklet, func, data);
	tasklet->type = type;
	/* We init the tasklet->link field, so we can list_del() it
	 * without worrying wether it was added to the list or not. */
	INIT_LIST_HEAD(&tasklet->link);
}

int ohci1394_register_iso_tasklet(struct ti_ohci *ohci,
				  struct ohci1394_iso_tasklet *tasklet)
{
	unsigned long flags, *usage;
	int n, i, r = -EBUSY;

	if (tasklet->type == OHCI_ISO_TRANSMIT) {
		n = ohci->nb_iso_xmit_ctx;
		usage = &ohci->it_ctx_usage;
	}
	else {
		n = ohci->nb_iso_rcv_ctx;
		usage = &ohci->ir_ctx_usage;
	}

	spin_lock_irqsave(&ohci->iso_tasklet_list_lock, flags);

	for (i = 0; i < n; i++)
		if (!test_and_set_bit(i, usage)) {
			tasklet->context = i;
			list_add_tail(&tasklet->link, &ohci->iso_tasklet_list);
			r = 0;
			break;
		}

	spin_unlock_irqrestore(&ohci->iso_tasklet_list_lock, flags);
	
	return r;
}

void ohci1394_unregister_iso_tasklet(struct ti_ohci *ohci,
				     struct ohci1394_iso_tasklet *tasklet)
{
	unsigned long flags;

	tasklet_kill(&tasklet->tasklet);

	spin_lock_irqsave(&ohci->iso_tasklet_list_lock, flags);

	if (tasklet->type == OHCI_ISO_TRANSMIT)
		clear_bit(tasklet->context, &ohci->it_ctx_usage);
	else
		clear_bit(tasklet->context, &ohci->ir_ctx_usage);

	list_del(&tasklet->link);

	spin_unlock_irqrestore(&ohci->iso_tasklet_list_lock, flags);
}

EXPORT_SYMBOL(ohci1394_stop_context);
EXPORT_SYMBOL(ohci1394_init_iso_tasklet);
EXPORT_SYMBOL(ohci1394_register_iso_tasklet);
EXPORT_SYMBOL(ohci1394_unregister_iso_tasklet);


/***********************************
 * General module initialization   *
 ***********************************/

MODULE_AUTHOR("Sebastien Rougeaux <sebastien.rougeaux@anu.edu.au>");
MODULE_DESCRIPTION("Driver for PCI OHCI IEEE-1394 controllers");
MODULE_LICENSE("GPL");

static void __exit ohci1394_cleanup (void)
{
	pci_unregister_driver(&ohci1394_pci_driver);
}

static int __init ohci1394_init(void)
{
	return pci_module_init(&ohci1394_pci_driver);
}

module_init(ohci1394_init);
module_exit(ohci1394_cleanup);
