/**************************************************************************
 * Initio A100 device driver for Linux.
 *
 * Copyright (c) 1994-1998 Initio Corporation
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * --------------------------------------------------------------------------
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Where this Software is combined with software released under the terms of 
 * the GNU General Public License ("GPL") and the terms of the GPL would require the 
 * combined work to also be released under the terms of the GPL, the terms
 * and conditions of this License will apply in addition to those of the
 * GPL with the exception of any terms or conditions of this License that
 * conflict with, or are expressly prohibited by, the GPL.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 **************************************************************************
 * 
 * module: inia100.c
 * DESCRIPTION:
 * 	This is the Linux low-level SCSI driver for Initio INIA100 SCSI host
 * 	adapters
 * 09/24/98 hl - v1.02 initial production release.
 * 12/19/98 bv - v1.02a Use spinlocks for 2.1.95 and up.
 * 06/25/02 Doug Ledford <dledford@redhat.com> - v1.02d
 *          - Remove limit on number of controllers
 *          - Port to DMA mapping API
 *          - Clean up interrupt handler registration
 *          - Fix memory leaks
 *          - Fix allocation of scsi host structs and private data
 * 18/11/03 Christoph Hellwig <hch@lst.de>
 *	    - Port to new probing API
 *	    - Fix some more leaks in init failure cases
 * TODO:
 *	    - use list.h macros for SCB queue
 *	  ( - merge with i60uscsi.c )
 **************************************************************************/

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include "inia100.h"

#define ORC_RDWORD(x,y)         (short)(inl((int)((ULONG)((ULONG)x+(UCHAR)y)) ))

char *inia100_Copyright = "Copyright (C) 1998-99";
char *inia100_InitioName = "by Initio Corporation";
char *inia100_ProductName = "INI-A100U2W";
char *inia100_Version = "v1.02d";

/* ---- EXTERNAL FUNCTIONS ---- */
extern void inia100SCBPost(BYTE * pHcb, BYTE * pScb);
extern int init_inia100Adapter_table(int);
extern ORC_SCB *orc_alloc_scb(ORC_HCS * hcsp);
extern void orc_exec_scb(ORC_HCS * hcsp, ORC_SCB * scbp);
extern void orc_release_scb(ORC_HCS * hcsp, ORC_SCB * scbp);
extern void orc_release_dma(ORC_HCS * hcsp, struct scsi_cmnd * cmnd);
extern void orc_interrupt(ORC_HCS * hcsp);
extern int orc_device_reset(ORC_HCS * pHCB, struct scsi_cmnd *SCpnt, unsigned int target);
extern int orc_reset_scsi_bus(ORC_HCS * pHCB);
extern int abort_SCB(ORC_HCS * hcsp, ORC_SCB * pScb);
extern int orc_abort_srb(ORC_HCS * hcsp, struct scsi_cmnd *SCpnt);
extern int init_orchid(ORC_HCS * hcsp);

/*****************************************************************************
 Function name  : inia100AppendSRBToQueue
 Description    : This function will push current request into save list
 Input          : pSRB  -       Pointer to SCSI request block.
		  pHCB  -       Pointer to host adapter structure
 Output         : None.
 Return         : None.
*****************************************************************************/
static void inia100AppendSRBToQueue(ORC_HCS * pHCB, struct scsi_cmnd * pSRB)
{
	ULONG flags;

	spin_lock_irqsave(&(pHCB->pSRB_lock), flags);

	pSRB->SCp.ptr = NULL;	/* Pointer to next */
	if (pHCB->pSRB_head == NULL)
		pHCB->pSRB_head = pSRB;
	else
		pHCB->pSRB_tail->SCp.ptr = (char *)pSRB;	/* Pointer to next */
	pHCB->pSRB_tail = pSRB;
	spin_unlock_irqrestore(&(pHCB->pSRB_lock), flags);
	return;
}

/*****************************************************************************
 Function name  : inia100PopSRBFromQueue
 Description    : This function will pop current request from save list
 Input          : pHCB  -       Pointer to host adapter structure
 Output         : None.
 Return         : pSRB  -       Pointer to SCSI request block.
*****************************************************************************/
static struct scsi_cmnd *inia100PopSRBFromQueue(ORC_HCS * pHCB)
{
	struct scsi_cmnd *pSRB;
	ULONG flags;
	spin_lock_irqsave(&(pHCB->pSRB_lock), flags);
	if ((pSRB = (struct scsi_cmnd *) pHCB->pSRB_head) != NULL) {
		pHCB->pSRB_head = (struct scsi_cmnd *) pHCB->pSRB_head->SCp.ptr;
		pSRB->SCp.ptr = NULL;
	}
	spin_unlock_irqrestore(&(pHCB->pSRB_lock), flags);
	return (pSRB);
}

/*****************************************************************************
 Function name  : inia100BuildSCB
 Description    : 
 Input          : pHCB  -       Pointer to host adapter structure
 Output         : None.
 Return         : pSRB  -       Pointer to SCSI request block.
*****************************************************************************/
static void inia100BuildSCB(ORC_HCS * pHCB, ORC_SCB * pSCB, struct scsi_cmnd * SCpnt)
{				/* Create corresponding SCB     */
	struct scatterlist *pSrbSG;
	ORC_SG *pSG;		/* Pointer to SG list           */
	int i, count_sg;
	U32 TotalLen;
	ESCB *pEScb;

	pEScb = pSCB->SCB_EScb;
	pEScb->SCB_Srb = SCpnt;
	pSG = NULL;

	pSCB->SCB_Opcode = ORC_EXECSCSI;
	pSCB->SCB_Flags = SCF_NO_DCHK;	/* Clear done bit               */
	pSCB->SCB_Target = SCpnt->device->id;
	pSCB->SCB_Lun = SCpnt->device->lun;
	pSCB->SCB_Reserved0 = 0;
	pSCB->SCB_Reserved1 = 0;
	pSCB->SCB_SGLen = 0;

	if ((pSCB->SCB_XferLen = (U32) SCpnt->request_bufflen)) {
		pSG = (ORC_SG *) & pEScb->ESCB_SGList[0];
		if (SCpnt->use_sg) {
			TotalLen = 0;
			pSrbSG = (struct scatterlist *) SCpnt->request_buffer;
			count_sg = pci_map_sg(pHCB->pdev, pSrbSG, SCpnt->use_sg,
					SCpnt->sc_data_direction);
			pSCB->SCB_SGLen = (U32) (count_sg * 8);
			for (i = 0; i < count_sg; i++, pSG++, pSrbSG++) {
				pSG->SG_Ptr = (U32) sg_dma_address(pSrbSG);
				pSG->SG_Len = (U32) sg_dma_len(pSrbSG);
				TotalLen += (U32) sg_dma_len(pSrbSG);
			}
		} else if (SCpnt->request_bufflen != 0) {/* Non SG */
			pSCB->SCB_SGLen = 0x8;
			pSG->SG_Ptr = (U32) pci_map_single(pHCB->pdev,
				SCpnt->request_buffer, SCpnt->request_bufflen,
				SCpnt->sc_data_direction);
			SCpnt->host_scribble = (void *)pSG->SG_Ptr;
			pSG->SG_Len = (U32) SCpnt->request_bufflen;
		} else {
			pSCB->SCB_SGLen = 0;
			pSG->SG_Ptr = 0;
			pSG->SG_Len = 0;
		}
	}
	pSCB->SCB_SGPAddr = (U32) pSCB->SCB_SensePAddr;
	pSCB->SCB_HaStat = 0;
	pSCB->SCB_TaStat = 0;
	pSCB->SCB_Link = 0xFF;
	pSCB->SCB_SenseLen = SENSE_SIZE;
	pSCB->SCB_CDBLen = SCpnt->cmd_len;
	if (pSCB->SCB_CDBLen >= IMAX_CDB) {
		printk("max cdb length= %x\b", SCpnt->cmd_len);
		pSCB->SCB_CDBLen = IMAX_CDB;
	}
	pSCB->SCB_Ident = SCpnt->device->lun | DISC_ALLOW;
	if (SCpnt->device->tagged_supported) {	/* Tag Support                  */
		pSCB->SCB_TagMsg = SIMPLE_QUEUE_TAG;	/* Do simple tag only   */
	} else {
		pSCB->SCB_TagMsg = 0;	/* No tag support               */
	}
	memcpy(&pSCB->SCB_CDB[0], &SCpnt->cmnd, pSCB->SCB_CDBLen);
	return;
}

/*****************************************************************************
 Function name  : inia100_queue
 Description    : Queue a command and setup interrupts for a free bus.
 Input          : pHCB  -       Pointer to host adapter structure
 Output         : None.
 Return         : pSRB  -       Pointer to SCSI request block.
*****************************************************************************/
static int inia100_queue(struct scsi_cmnd * SCpnt, void (*done) (struct scsi_cmnd *))
{
	register ORC_SCB *pSCB;
	ORC_HCS *pHCB;		/* Point to Host adapter control block */

	pHCB = (ORC_HCS *) SCpnt->device->host->hostdata;
	SCpnt->scsi_done = done;
	/* Get free SCSI control block  */
	if ((pSCB = orc_alloc_scb(pHCB)) == NULL) {
		inia100AppendSRBToQueue(pHCB, SCpnt);	/* Buffer this request  */
		/* printk("inia100_entry: can't allocate SCB\n"); */
		return (0);
	}
	inia100BuildSCB(pHCB, pSCB, SCpnt);
	orc_exec_scb(pHCB, pSCB);	/* Start execute SCB            */

	return (0);
}

/*****************************************************************************
 Function name  : inia100_abort
 Description    : Abort a queued command.
	                 (commands that are on the bus can't be aborted easily)
 Input          : pHCB  -       Pointer to host adapter structure
 Output         : None.
 Return         : pSRB  -       Pointer to SCSI request block.
*****************************************************************************/
static int inia100_abort(struct scsi_cmnd * SCpnt)
{
	ORC_HCS *hcsp;

	hcsp = (ORC_HCS *) SCpnt->device->host->hostdata;
	return orc_abort_srb(hcsp, SCpnt);
}

/*****************************************************************************
 Function name  : inia100_reset
 Description    : Reset registers, reset a hanging bus and
                  kill active and disconnected commands for target w/o soft reset
 Input          : pHCB  -       Pointer to host adapter structure
 Output         : None.
 Return         : pSRB  -       Pointer to SCSI request block.
*****************************************************************************/
static int inia100_bus_reset(struct scsi_cmnd * SCpnt)
{				/* I need Host Control Block Information */
	ORC_HCS *pHCB;
	pHCB = (ORC_HCS *) SCpnt->device->host->hostdata;
	return orc_reset_scsi_bus(pHCB);
}

/*****************************************************************************
 Function name  : inia100_device_reset
 Description    : Reset the device
 Input          : pHCB  -       Pointer to host adapter structure
 Output         : None.
 Return         : pSRB  -       Pointer to SCSI request block.
*****************************************************************************/
static int inia100_device_reset(struct scsi_cmnd * SCpnt)
{				/* I need Host Control Block Information */
	ORC_HCS *pHCB;
	pHCB = (ORC_HCS *) SCpnt->device->host->hostdata;
	return orc_device_reset(pHCB, SCpnt, SCpnt->device->id);

}

/*****************************************************************************
 Function name  : inia100SCBPost
 Description    : This is callback routine be called when orc finish one
			SCSI command.
 Input          : pHCB  -       Pointer to host adapter control block.
		  pSCB  -       Pointer to SCSI control block.
 Output         : None.
 Return         : None.
*****************************************************************************/
void inia100SCBPost(BYTE * pHcb, BYTE * pScb)
{
	struct scsi_cmnd *pSRB;	/* Pointer to SCSI request block */
	ORC_HCS *pHCB;
	ORC_SCB *pSCB;
	ESCB *pEScb;

	pHCB = (ORC_HCS *) pHcb;
	pSCB = (ORC_SCB *) pScb;
	pEScb = pSCB->SCB_EScb;
	if ((pSRB = (struct scsi_cmnd *) pEScb->SCB_Srb) == 0) {
		printk("inia100SCBPost: SRB pointer is empty\n");
		orc_release_scb(pHCB, pSCB);	/* Release SCB for current channel */
		return;
	}
	pEScb->SCB_Srb = NULL;

	switch (pSCB->SCB_HaStat) {
	case 0x0:
	case 0xa:		/* Linked command complete without error and linked normally */
	case 0xb:		/* Linked command complete without error interrupt generated */
		pSCB->SCB_HaStat = 0;
		break;

	case 0x11:		/* Selection time out-The initiator selection or target
				   reselection was not complete within the SCSI Time out period */
		pSCB->SCB_HaStat = DID_TIME_OUT;
		break;

	case 0x14:		/* Target bus phase sequence failure-An invalid bus phase or bus
				   phase sequence was requested by the target. The host adapter
				   will generate a SCSI Reset Condition, notifying the host with
				   a SCRD interrupt */
		pSCB->SCB_HaStat = DID_RESET;
		break;

	case 0x1a:		/* SCB Aborted. 07/21/98 */
		pSCB->SCB_HaStat = DID_ABORT;
		break;

	case 0x12:		/* Data overrun/underrun-The target attempted to transfer more data
				   than was allocated by the Data Length field or the sum of the
				   Scatter / Gather Data Length fields. */
	case 0x13:		/* Unexpected bus free-The target dropped the SCSI BSY at an unexpected time. */
	case 0x16:		/* Invalid CCB Operation Code-The first byte of the CCB was invalid. */

	default:
		printk("inia100: %x %x\n", pSCB->SCB_HaStat, pSCB->SCB_TaStat);
		pSCB->SCB_HaStat = DID_ERROR;	/* Couldn't find any better */
		break;
	}

	if (pSCB->SCB_TaStat == 2) {	/* Check condition              */
		memcpy((unsigned char *) &pSRB->sense_buffer[0],
		   (unsigned char *) &pEScb->ESCB_SGList[0], SENSE_SIZE);
	}
	pSRB->result = pSCB->SCB_TaStat | (pSCB->SCB_HaStat << 16);
	orc_release_dma(pHCB, pSRB);  /* release DMA before we call scsi_done */
	pSRB->scsi_done(pSRB);	/* Notify system DONE           */

	/* Find the next pending SRB    */
	if ((pSRB = inia100PopSRBFromQueue(pHCB)) != NULL) {	/* Assume resend will success   */
		inia100BuildSCB(pHCB, pSCB, pSRB);	/* Create corresponding SCB     */
		orc_exec_scb(pHCB, pSCB);	/* Start execute SCB            */
	} else {
		orc_release_scb(pHCB, pSCB);	/* Release SCB for current channel */
	}
	return;
}

/*
 * Interrupt handler (main routine of the driver)
 */
static irqreturn_t inia100_intr(int irqno, void *devid, struct pt_regs *regs)
{
	struct Scsi_Host *host = (struct Scsi_Host *)devid;
	ORC_HCS *pHcb = (ORC_HCS *)host->hostdata;
	unsigned long flags;

	spin_lock_irqsave(host->host_lock, flags);
	orc_interrupt(pHcb);
	spin_unlock_irqrestore(host->host_lock, flags);

	return IRQ_HANDLED;
}

static struct scsi_host_template inia100_template = {
	.proc_name		= "inia100",
	.name			= inia100_REVID,
	.queuecommand		= inia100_queue,
	.eh_abort_handler	= inia100_abort,
	.eh_bus_reset_handler	= inia100_bus_reset,
	.eh_device_reset_handler = inia100_device_reset,
	.can_queue		= 1,
	.this_id		= 1,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun 		= 1,
	.use_clustering		= ENABLE_CLUSTERING,
};

static int __devinit inia100_probe_one(struct pci_dev *pdev,
		const struct pci_device_id *id)
{
	struct Scsi_Host *shost;
	ORC_HCS *pHCB;
	unsigned long port, bios;
	int error = -ENODEV;
	u32 sz;
	unsigned long dBiosAdr;
	char *pbBiosAdr;

	if (pci_enable_device(pdev))
		goto out;
	if (pci_set_dma_mask(pdev, 0xffffffffULL)) {
		printk(KERN_WARNING "Unable to set 32bit DMA "
				    "on inia100 adapter, ignoring.\n");
		goto out_disable_device;
	}

	pci_set_master(pdev);

	port = pci_resource_start(pdev, 0);
	if (!request_region(port, 256, "inia100")) {
		printk(KERN_WARNING "inia100: io port 0x%lx, is busy.\n", port);
		goto out_disable_device;
	}

	/* <02> read from base address + 0x50 offset to get the bios balue. */
	bios = ORC_RDWORD(port, 0x50);


	shost = scsi_host_alloc(&inia100_template, sizeof(ORC_HCS));
	if (!shost)
		goto out_release_region;

	pHCB = (ORC_HCS *)shost->hostdata;
	pHCB->pdev = pdev;
	pHCB->HCS_Base = port;
	pHCB->HCS_BIOS = bios;
	pHCB->pSRB_head = NULL;	/* Initial SRB save queue       */
	pHCB->pSRB_tail = NULL;	/* Initial SRB save queue       */
	pHCB->pSRB_lock = SPIN_LOCK_UNLOCKED; /* SRB save queue lock */
	pHCB->BitAllocFlagLock = SPIN_LOCK_UNLOCKED;

	/* Get total memory needed for SCB */
	sz = ORC_MAXQUEUE * sizeof(ORC_SCB);
	pHCB->HCS_virScbArray = pci_alloc_consistent(pdev, sz,
			&pHCB->HCS_physScbArray);
	if (!pHCB->HCS_virScbArray) {
		printk("inia100: SCB memory allocation error\n");
		goto out_host_put;
	}
	memset(pHCB->HCS_virScbArray, 0, sz);

	/* Get total memory needed for ESCB */
	sz = ORC_MAXQUEUE * sizeof(ESCB);
	pHCB->HCS_virEscbArray = pci_alloc_consistent(pdev, sz,
			&pHCB->HCS_physEscbArray);
	if (!pHCB->HCS_virEscbArray) {
		printk("inia100: ESCB memory allocation error\n");
		goto out_free_scb_array;
	}
	memset(pHCB->HCS_virEscbArray, 0, sz);

	dBiosAdr = pHCB->HCS_BIOS;
	dBiosAdr = (dBiosAdr << 4);
	pbBiosAdr = phys_to_virt(dBiosAdr);
	if (init_orchid(pHCB)) {	/* Initialize orchid chip */
		printk("inia100: initial orchid fail!!\n");
		goto out_free_escb_array;
	}

	shost->io_port = pHCB->HCS_Base;
	shost->n_io_port = 0xff;
	shost->can_queue = ORC_MAXQUEUE;
	shost->unique_id = shost->io_port;
	shost->max_id = pHCB->HCS_MaxTar;
	shost->max_lun = 16;
	shost->irq = pHCB->HCS_Intr = pdev->irq;
	shost->this_id = pHCB->HCS_SCSI_ID;	/* Assign HCS index */
	shost->sg_tablesize = TOTAL_SG_ENTRY;

	/* Initial orc chip           */
	error = request_irq(pdev->irq, inia100_intr, SA_SHIRQ,
			"inia100", shost);
	if (error < 0) {
		printk(KERN_WARNING "inia100: unable to get irq %d\n",
				pdev->irq);
		goto out_free_escb_array;
	}

	pci_set_drvdata(pdev, shost);

	error = scsi_add_host(shost, &pdev->dev);
	if (error)
		goto out_free_irq;

	scsi_scan_host(shost);
	return 0;

 out_free_irq:
        free_irq(shost->irq, shost);
 out_free_escb_array:
	pci_free_consistent(pdev, ORC_MAXQUEUE * sizeof(ESCB),
			pHCB->HCS_virEscbArray, pHCB->HCS_physEscbArray);
 out_free_scb_array:
	pci_free_consistent(pdev, ORC_MAXQUEUE * sizeof(ORC_SCB),
			pHCB->HCS_virScbArray, pHCB->HCS_physScbArray);
 out_host_put:
	scsi_host_put(shost);
 out_release_region:
        release_region(port, 256);
 out_disable_device:
	pci_disable_device(pdev);
 out:
	return error;
}

static void __devexit inia100_remove_one(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	ORC_HCS *pHCB = (ORC_HCS *)shost->hostdata;

	scsi_remove_host(shost);

        free_irq(shost->irq, shost);
	pci_free_consistent(pdev, ORC_MAXQUEUE * sizeof(ESCB),
			pHCB->HCS_virEscbArray, pHCB->HCS_physEscbArray);
	pci_free_consistent(pdev, ORC_MAXQUEUE * sizeof(ORC_SCB),
			pHCB->HCS_virScbArray, pHCB->HCS_physScbArray);
        release_region(shost->io_port, 256);

	scsi_host_put(shost);
} 

static struct pci_device_id inia100_pci_tbl[] = {
	{ORC_VENDOR_ID, ORC_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,}
};
MODULE_DEVICE_TABLE(pci, inia100_pci_tbl);

static struct pci_driver inia100_pci_driver = {
	.name		= "inia100",
	.id_table	= inia100_pci_tbl,
	.probe		= inia100_probe_one,
	.remove		= __devexit_p(inia100_remove_one),
};

static int __init inia100_init(void)
{
	return pci_module_init(&inia100_pci_driver);
}

static void __exit inia100_exit(void)
{
	pci_unregister_driver(&inia100_pci_driver);
}

MODULE_DESCRIPTION("Initio A100U2W SCSI driver");
MODULE_AUTHOR("Initio Corporation");
MODULE_LICENSE("Dual BSD/GPL");

module_init(inia100_init);
module_exit(inia100_exit);
