/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Enterprise Fibre Channel Host Bus Adapters.                     *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2004 Emulex Corporation.                          *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

/*
 * $Id: lpfc_hbadisc.c 1.185 2004/10/18 19:01:31EDT sf_support Exp  $
 */

#include <linux/version.h>
#include <linux/blkdev.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/smp_lock.h>

#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include <scsi/scsi_transport_fc.h>

#include "lpfc_sli.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_crtn.h"
#include "lpfc_fcp.h"
#include "lpfc_hw.h"
#include "lpfc_logmsg.h"
#include "lpfc_mem.h"

/* AlpaArray for assignment of scsid for scan-down and bind_method */
uint8_t lpfcAlpaArray[] = {
	0xEF, 0xE8, 0xE4, 0xE2, 0xE1, 0xE0, 0xDC, 0xDA, 0xD9, 0xD6,
	0xD5, 0xD4, 0xD3, 0xD2, 0xD1, 0xCE, 0xCD, 0xCC, 0xCB, 0xCA,
	0xC9, 0xC7, 0xC6, 0xC5, 0xC3, 0xBC, 0xBA, 0xB9, 0xB6, 0xB5,
	0xB4, 0xB3, 0xB2, 0xB1, 0xAE, 0xAD, 0xAC, 0xAB, 0xAA, 0xA9,
	0xA7, 0xA6, 0xA5, 0xA3, 0x9F, 0x9E, 0x9D, 0x9B, 0x98, 0x97,
	0x90, 0x8F, 0x88, 0x84, 0x82, 0x81, 0x80, 0x7C, 0x7A, 0x79,
	0x76, 0x75, 0x74, 0x73, 0x72, 0x71, 0x6E, 0x6D, 0x6C, 0x6B,
	0x6A, 0x69, 0x67, 0x66, 0x65, 0x63, 0x5C, 0x5A, 0x59, 0x56,
	0x55, 0x54, 0x53, 0x52, 0x51, 0x4E, 0x4D, 0x4C, 0x4B, 0x4A,
	0x49, 0x47, 0x46, 0x45, 0x43, 0x3C, 0x3A, 0x39, 0x36, 0x35,
	0x34, 0x33, 0x32, 0x31, 0x2E, 0x2D, 0x2C, 0x2B, 0x2A, 0x29,
	0x27, 0x26, 0x25, 0x23, 0x1F, 0x1E, 0x1D, 0x1B, 0x18, 0x17,
	0x10, 0x0F, 0x08, 0x04, 0x02, 0x01
};

static void
lpfc_evt_iocb_free(struct lpfc_hba * phba, struct lpfc_iocbq * saveq)
{
	struct lpfc_iocbq  *rspiocbp, *tmpiocbp;

	/* Free up iocb buffer chain for cmd just processed */
	list_for_each_entry_safe(rspiocbp, tmpiocbp,
		&saveq->list, list) {
		list_del(&rspiocbp->list);
		mempool_free( rspiocbp, phba->iocb_mem_pool);
	}
	mempool_free( saveq, phba->iocb_mem_pool);
}

static void
lpfc_disc_done(struct lpfc_hba * phba)
{
	struct lpfc_sli *psli = &phba->sli;
	LPFC_DISC_EVT_t  *evtp, *next_evtp;
	LPFC_MBOXQ_t *pmb;
	struct lpfc_iocbq  *cmdiocbp, *saveq;
	LPFC_RING_MASK_t *func;
	LIST_HEAD(local_dpc_disc);

	list_splice_init(&phba->dpc_disc, &local_dpc_disc);

	/* check discovery event list */
	list_for_each_entry_safe(evtp, next_evtp, &local_dpc_disc, evt_listp) {
		list_del(&evtp->evt_listp);

		switch(evtp->evt) {
		case LPFC_EVT_MBOX:
			pmb = (LPFC_MBOXQ_t *)(evtp->evt_arg1);
			(pmb->mbox_cmpl) (phba, pmb);
			break;
		case LPFC_EVT_SOL_IOCB:
			cmdiocbp = (struct lpfc_iocbq *)(evtp->evt_arg1);
			saveq = (struct lpfc_iocbq *)(evtp->evt_arg2);
			(cmdiocbp->iocb_cmpl) (phba, cmdiocbp, saveq);
			lpfc_evt_iocb_free(phba, saveq);
			break;
		case LPFC_EVT_UNSOL_IOCB:
			func = (LPFC_RING_MASK_t *)(evtp->evt_arg1);
			saveq = (struct lpfc_iocbq *)(evtp->evt_arg2);
			(func->lpfc_sli_rcv_unsol_event) (phba,
	 		&psli->ring[LPFC_ELS_RING], saveq);
			lpfc_evt_iocb_free(phba, saveq);
			break;
		}

		kfree(evtp);
	}
}

int
lpfc_do_dpc(void *p)
{
	unsigned long flags;
	DECLARE_MUTEX_LOCKED(sem);
	struct lpfc_hba *phba = (struct lpfc_hba *)p;

	lock_kernel();

	daemonize("lpfc_dpc_%d", phba->brd_no);
	allow_signal(SIGHUP);

	phba->dpc_wait = &sem;
	set_user_nice(current, -20);

	unlock_kernel();

	complete(&phba->dpc_startup);

	while (1) {
		if (down_interruptible(&sem))
			break;

		if (signal_pending(current))
			break;

		if (phba->dpc_kill)
			break;

		spin_lock_irqsave(phba->host->host_lock, flags);
		lpfc_disc_done(phba);
		spin_unlock_irqrestore(phba->host->host_lock, flags);
	}

	/* Zero out semaphore we were waiting on. */
	phba->dpc_wait = NULL;
	complete_and_exit(&phba->dpc_exiting, 0);
	return(0);
}

/*
 * This is only called to handle FC discovery events. Since this a rare
 * occurance, we allocate an LPFC_DISC_EVT_t structure here instead of
 * embedding it in the IOCB.
 */
int
lpfc_discq_post_event(struct lpfc_hba * phba, void *arg1, void *arg2,
		      uint32_t evt)
{
	LPFC_DISC_EVT_t  *evtp;

	/* All Mailbox completions and LPFC_ELS_RING rcv ring IOCB events
	 * will be queued to DPC for processing
	 */
	evtp = (LPFC_DISC_EVT_t *) kmalloc(sizeof(LPFC_DISC_EVT_t), GFP_ATOMIC);
	if (!evtp)
		return 0;

	evtp->evt_arg1  = arg1;
	evtp->evt_arg2  = arg2;
	evtp->evt       = evt;
	evtp->evt_listp.next = NULL;
	evtp->evt_listp.prev = NULL;

	/* Queue the event to the DPC to be processed later */
	list_add_tail(&evtp->evt_listp, &phba->dpc_disc);
	if (phba->dpc_wait)
		up(phba->dpc_wait);

	return 1;
}

int
lpfc_linkdown(struct lpfc_hba * phba)
{
	struct lpfc_sli       *psli;
	struct lpfc_nodelist  *ndlp, *next_ndlp;
	struct list_head *listp;
	struct list_head *node_list[7];
	LPFC_MBOXQ_t     *mb;
	int               rc, i;

	psli = &phba->sli;
	phba->hba_state = LPFC_LINK_DOWN;

#if !defined(FC_TRANS_VER1) && !defined(FC_TRANS_265_BLKPATCH)
	/* Stop all requests to the driver from the midlayer. */
	scsi_block_requests(phba->host);
#endif

	lpfc_put_event(phba, HBA_EVENT_LINK_DOWN, phba->fc_myDID, NULL, 0, 0);

	/* Clean up any firmware default rpi's */
	if ((mb = mempool_alloc(phba->mbox_mem_pool, GFP_ATOMIC))) {
		lpfc_unreg_did(phba, 0xffffffff, mb);
		if (lpfc_sli_issue_mbox(phba, mb, (MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			mempool_free( mb, phba->mbox_mem_pool);
		}
	}

	/* Cleanup any outstanding RSCN activity */
	lpfc_els_flush_rscn(phba);

	/* Cleanup any outstanding ELS commands */
	lpfc_els_flush_cmd(phba);

	/*
	 * If this function was called by the lpfc_do_dpc, don't recurse into
	 * the routine again.  If not, just process any outstanding
	 * discovery events.
	 */
	if (!list_empty(&phba->dpc_disc)) {
		lpfc_disc_done(phba);
	}

	/* Issue a LINK DOWN event to all nodes */
	node_list[0] = &phba->fc_npr_list;  /* MUST do this list first */
	node_list[1] = &phba->fc_nlpmap_list;
	node_list[2] = &phba->fc_nlpunmap_list;
	node_list[3] = &phba->fc_prli_list;
	node_list[4] = &phba->fc_reglogin_list;
	node_list[5] = &phba->fc_adisc_list;
	node_list[6] = &phba->fc_plogi_list;
	for (i = 0; i < 7; i++) {
		listp = node_list[i];
		if (list_empty(listp))
			continue;

		list_for_each_entry_safe(ndlp, next_ndlp, listp, nlp_listp) {
			/* Fabric nodes are not handled thru state machine for
			   link down */
			if (ndlp->nlp_type & NLP_FABRIC) {
				/* Remove ALL Fabric nodes except Fabric_DID */
				if (ndlp->nlp_DID != Fabric_DID) {
					/* Take it off current list and free */
					lpfc_nlp_list(phba, ndlp,
						NLP_NO_LIST);
				}
			}
			else {
				lpfc_set_failmask(phba, ndlp,
						  LPFC_DEV_LINK_DOWN,
						  LPFC_SET_BITMASK);

				rc = lpfc_disc_state_machine(phba, ndlp, NULL,
						     NLP_EVT_DEVICE_RECOVERY);
			}
		}
	}

	/* free any ndlp's on unused list */
	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_unused_list,
				nlp_listp) {
		lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	}

	/* Setup myDID for link up if we are in pt2pt mode */
	if (phba->fc_flag & FC_PT2PT) {
		phba->fc_myDID = 0;
		if ((mb = mempool_alloc(phba->mbox_mem_pool, GFP_ATOMIC))) {
			lpfc_config_link(phba, mb);
			if (lpfc_sli_issue_mbox
			    (phba, mb, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				mempool_free( mb, phba->mbox_mem_pool);
			}
		}
		phba->fc_flag &= ~(FC_PT2PT | FC_PT2PT_PLOGI);
	}
	phba->fc_flag &= ~FC_LBIT;

	/* Turn off discovery timer if its running */
	lpfc_can_disctmo(phba);

	/* Must process IOCBs on all rings to handle ABORTed I/Os */
	return (0);
}

static int
lpfc_linkup(struct lpfc_hba * phba)
{
	struct lpfc_nodelist *ndlp, *next_ndlp;
	struct list_head *listp;
	struct list_head *node_list[7];
	int i;

	phba->hba_state = LPFC_LINK_UP;
	phba->fc_flag &= ~(FC_PT2PT | FC_PT2PT_PLOGI |
			   FC_RSCN_MODE | FC_NLP_MORE | FC_RSCN_DISCOVERY);
	phba->fc_ns_retry = 0;


	lpfc_put_event(phba, HBA_EVENT_LINK_UP, phba->fc_myDID,
			(void *)(unsigned long)(phba->fc_topology),
			0, phba->fc_linkspeed);

	/*
	 * Clean up old Fabric NLP_FABRIC logins.
	 */
	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_nlpunmap_list,
				nlp_listp) {
		if (ndlp->nlp_DID == Fabric_DID) {
			/* Take it off current list and free */
			lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
		}
	}

	/* free any ndlp's on unused list */
	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_unused_list,
				nlp_listp) {
		lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	}

	/* Mark all nodes for LINK UP */
	node_list[0] = &phba->fc_plogi_list;
	node_list[1] = &phba->fc_adisc_list;
	node_list[2] = &phba->fc_reglogin_list;
	node_list[3] = &phba->fc_prli_list;
	node_list[4] = &phba->fc_nlpunmap_list;
	node_list[5] = &phba->fc_nlpmap_list;
	node_list[6] = &phba->fc_npr_list;
	for (i = 0; i < 7; i++) {
		listp = node_list[i];
		if (list_empty(listp))
			continue;

		list_for_each_entry(ndlp, listp, nlp_listp) {
			lpfc_set_failmask(phba, ndlp, LPFC_DEV_DISCOVERY_INP,
					  LPFC_SET_BITMASK);
			lpfc_set_failmask(phba, ndlp, LPFC_DEV_LINK_DOWN,
					  LPFC_CLR_BITMASK);
		}
	}

#if !defined(FC_TRANS_VER1) && !defined(FC_TRANS_265_BLKPATCH)
	spin_unlock_irq(phba->host->host_lock);
	scsi_unblock_requests(phba->host);
	spin_lock_irq(phba->host->host_lock);
#endif
	return 0;
}

/*
 * This routine handles processing a CLEAR_LA mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_clear_la(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	struct lpfc_sli *psli;
	MAILBOX_t *mb;
	uint32_t control;

	psli = &phba->sli;
	mb = &pmb->mb;
	/* Since we don't do discovery right now, turn these off here */
	psli->ring[psli->ip_ring].flag &= ~LPFC_STOP_IOCB_EVENT;
	psli->ring[psli->fcp_ring].flag &= ~LPFC_STOP_IOCB_EVENT;
	psli->ring[psli->next_ring].flag &= ~LPFC_STOP_IOCB_EVENT;
	/* Check for error */
	if ((mb->mbxStatus) && (mb->mbxStatus != 0x1601)) {
		/* CLEAR_LA mbox error <mbxStatus> state <hba_state> */
		lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
				"%d:0320 CLEAR_LA mbxStatus error x%x hba "
				"state x%x\n",
				phba->brd_no, mb->mbxStatus, phba->hba_state);

		phba->hba_state = LPFC_HBA_ERROR;
		goto out;
	}

	phba->num_disc_nodes = 0;
	/* go thru NPR list and issue ELS PLOGIs */
	if (phba->fc_npr_cnt) {
		lpfc_els_disc_plogi(phba);
	}

	phba->hba_state = LPFC_HBA_READY;

out:
	/* Device Discovery completes */
	lpfc_printf_log(phba,
			 KERN_INFO,
			 LOG_DISCOVERY,
			 "%d:0225 Device Discovery completes\n",
			 phba->brd_no);

	mempool_free( pmb, phba->mbox_mem_pool);
	if (phba->fc_flag & FC_ESTABLISH_LINK) {
		phba->fc_flag &= ~FC_ESTABLISH_LINK;
	}
	del_timer_sync(&phba->fc_estabtmo);
	lpfc_can_disctmo(phba);

	/* turn on Link Attention interrupts */
	psli->sliinit.sli_flag |= LPFC_PROCESS_LA;
	control = readl(phba->HCregaddr);
	control |= HC_LAINT_ENA;
	writel(control, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */

	return;
}

static void
lpfc_mbx_cmpl_config_link(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	struct lpfc_sli *psli;
	MAILBOX_t *mb;

	psli = &phba->sli;
	mb = &pmb->mb;
	/* Check for error */
	if (mb->mbxStatus) {
		/* CONFIG_LINK mbox error <mbxStatus> state <hba_state> */
		lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
				"%d:0306 CONFIG_LINK mbxStatus error x%x "
				"HBA state x%x\n",
				phba->brd_no, mb->mbxStatus, phba->hba_state);

		lpfc_linkdown(phba);
		phba->hba_state = LPFC_HBA_ERROR;
		goto out;
	}

	if (phba->hba_state == LPFC_LOCAL_CFG_LINK) {
		if (phba->fc_topology == TOPOLOGY_LOOP) {
			/* If we are public loop and L bit was set */
			if ((phba->fc_flag & FC_PUBLIC_LOOP) &&
			    !(phba->fc_flag & FC_LBIT)) {
				/* Need to wait for FAN - use discovery timer
				 * for timeout.  hba_state is identically
				 * LPFC_LOCAL_CFG_LINK while waiting for FAN
				 */
				lpfc_set_disctmo(phba);
				mempool_free( pmb, phba->mbox_mem_pool);
				return;
			}
		}

		/* Start discovery by sending a FLOGI hba_state is identically
		 * LPFC_FLOGI while waiting for FLOGI cmpl
		 */
		phba->hba_state = LPFC_FLOGI;
		lpfc_set_disctmo(phba);
		lpfc_initial_flogi(phba);
		mempool_free( pmb, phba->mbox_mem_pool);
		return;
	}
	if (phba->hba_state == LPFC_FABRIC_CFG_LINK) {
		mempool_free( pmb, phba->mbox_mem_pool);
		return;
	}

out:
	/* CONFIG_LINK bad hba state <hba_state> */
	lpfc_printf_log(phba,
			KERN_ERR,
			LOG_DISCOVERY,
			"%d:0200 CONFIG_LINK bad hba state x%x\n",
			phba->brd_no, phba->hba_state);

	if (phba->hba_state != LPFC_CLEAR_LA) {
		lpfc_clear_la(phba, pmb);
		pmb->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
		if (lpfc_sli_issue_mbox(phba, pmb, (MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			mempool_free( pmb, phba->mbox_mem_pool);
			lpfc_disc_flush_list(phba);
			psli->ring[(psli->ip_ring)].flag &=
				~LPFC_STOP_IOCB_EVENT;
			psli->ring[(psli->fcp_ring)].flag &=
				~LPFC_STOP_IOCB_EVENT;
			psli->ring[(psli->next_ring)].flag &=
				~LPFC_STOP_IOCB_EVENT;
			phba->hba_state = LPFC_HBA_READY;
		}
	} else {
		mempool_free( pmb, phba->mbox_mem_pool);
	}
	return;
}

static void
lpfc_mbx_cmpl_read_sparam(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	struct lpfc_sli *psli = &phba->sli;
	MAILBOX_t *mb = &pmb->mb;
	struct lpfc_dmabuf *mp = (struct lpfc_dmabuf *) pmb->context1;


	/* Check for error */
	if (mb->mbxStatus) {
		/* READ_SPARAM mbox error <mbxStatus> state <hba_state> */
		lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
				"%d:0319 READ_SPARAM mbxStatus error x%x "
				"hba state x%x>\n",
				phba->brd_no, mb->mbxStatus, phba->hba_state);

		lpfc_linkdown(phba);
		phba->hba_state = LPFC_HBA_ERROR;
		goto out;
	}

	/* The mailbox was populated by the HBA.  Flush it to main store for the
	 * driver.  Note that all context buffers are from the driver's
	 * dma pool and have length LPFC_BPL_SIZE.
	 */
	pci_dma_sync_single_for_cpu(phba->pcidev, mp->phys, LPFC_BPL_SIZE,
			PCI_DMA_FROMDEVICE);

	memcpy((uint8_t *) & phba->fc_sparam, (uint8_t *) mp->virt,
	       sizeof (struct serv_parm));
	memcpy((uint8_t *) & phba->fc_nodename,
	       (uint8_t *) & phba->fc_sparam.nodeName,
	       sizeof (struct lpfc_name));
	memcpy((uint8_t *) & phba->fc_portname,
	       (uint8_t *) & phba->fc_sparam.portName,
	       sizeof (struct lpfc_name));
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free( pmb, phba->mbox_mem_pool);
	return;

out:
	pmb->context1 = 0;
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	if (phba->hba_state != LPFC_CLEAR_LA) {
		lpfc_clear_la(phba, pmb);
		pmb->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
		if (lpfc_sli_issue_mbox(phba, pmb, (MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			mempool_free( pmb, phba->mbox_mem_pool);
			lpfc_disc_flush_list(phba);
			psli->ring[(psli->ip_ring)].flag &=
			    ~LPFC_STOP_IOCB_EVENT;
			psli->ring[(psli->fcp_ring)].flag &=
			    ~LPFC_STOP_IOCB_EVENT;
			psli->ring[(psli->next_ring)].flag &=
			    ~LPFC_STOP_IOCB_EVENT;
			phba->hba_state = LPFC_HBA_READY;
		}
	} else {
		mempool_free( pmb, phba->mbox_mem_pool);
	}
	return;
}

/*
 * This routine handles processing a READ_LA mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_read_la(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	struct lpfc_sli *psli = &phba->sli;
	READ_LA_VAR *la;
	LPFC_MBOXQ_t *mbox;
	MAILBOX_t *mb = &pmb->mb;
	struct lpfc_dmabuf *mp = (struct lpfc_dmabuf *) (pmb->context1);
	uint32_t control;
	int i;

	/* Check for error */
	if (mb->mbxStatus) {
		/* READ_LA mbox error <mbxStatus> state <hba_state> */
		lpfc_printf_log(phba,
				KERN_INFO,
				LOG_LINK_EVENT,
				"%d:1307 READ_LA mbox error x%x state x%x\n",
				phba->brd_no,
				mb->mbxStatus, phba->hba_state);

		lpfc_linkdown(phba);
		phba->hba_state = LPFC_HBA_ERROR;

		/* turn on Link Attention interrupts */
		psli->sliinit.sli_flag |= LPFC_PROCESS_LA;
		control = readl(phba->HCregaddr);
		control |= HC_LAINT_ENA;
		writel(control, phba->HCregaddr);
		readl(phba->HCregaddr); /* flush */
		return;
	}
	la = (READ_LA_VAR *) & pmb->mb.un.varReadLA;


	/* The mailbox was populated by the HBA.  Flush it to main store for the
	 * driver.  Note that all context buffers are from the driver's
	 * dma pool and have length LPFC_BPL_SIZE.
	 */
	pci_dma_sync_single_for_cpu(phba->pcidev, mp->phys, LPFC_BPL_SIZE,
				    PCI_DMA_FROMDEVICE);

	/* Get Loop Map information */
	if (mp) {
		memcpy(&phba->alpa_map[0], mp->virt, 128);
	} else {
		memset(&phba->alpa_map[0], 0, 128);
	}

	if (((phba->fc_eventTag + 1) < la->eventTag) ||
	    (phba->fc_eventTag == la->eventTag)) {
		phba->fc_stat.LinkMultiEvent++;
		if (la->attType == AT_LINK_UP) {
			if (phba->fc_eventTag != 0) {

				lpfc_linkdown(phba);
			}
		}
	}

	phba->fc_eventTag = la->eventTag;

	if (la->attType == AT_LINK_UP) {
		phba->fc_stat.LinkUp++;
		/* Link Up Event <eventTag> received */
		lpfc_printf_log(phba, KERN_ERR, LOG_LINK_EVENT,
				"%d:1303 Link Up Event x%x received "
				"Data: x%x x%x x%x x%x\n",
				phba->brd_no, la->eventTag, phba->fc_eventTag,
				la->granted_AL_PA, la->UlnkSpeed,
				phba->alpa_map[0]);

		if (la->UlnkSpeed == LA_2GHZ_LINK)
			phba->fc_linkspeed = LA_2GHZ_LINK;
		else
			phba->fc_linkspeed = 0;

		if ((phba->fc_topology = la->topology) == TOPOLOGY_LOOP) {

			if (la->il) {
				phba->fc_flag |= FC_LBIT;
			}

			phba->fc_myDID = la->granted_AL_PA;

			i = la->un.lilpBde64.tus.f.bdeSize;
			if (i == 0) {
				phba->alpa_map[0] = 0;
			} else {
				if (phba->cfg_log_verbose
				    & LOG_LINK_EVENT) {
					int numalpa, j, k;
					union {
						uint8_t pamap[16];
						struct {
							uint32_t wd1;
							uint32_t wd2;
							uint32_t wd3;
							uint32_t wd4;
						} pa;
					} un;

					numalpa = phba->alpa_map[0];
					j = 0;
					while (j < numalpa) {
						memset(un.pamap, 0, 16);
						for (k = 1; j < numalpa; k++) {
							un.pamap[k - 1] =
							    phba->alpa_map[j +
									   1];
							j++;
							if (k == 16)
								break;
						}
						/* Link Up Event ALPA map */
						lpfc_printf_log(phba,
							KERN_WARNING,
							LOG_LINK_EVENT,
							"%d:1304 Link Up Event "
							"ALPA map Data: x%x "
							"x%x x%x x%x\n",
							phba->brd_no,
							un.pa.wd1, un.pa.wd2,
							un.pa.wd3, un.pa.wd4);
					}
				}
			}
		} else {
			phba->fc_myDID = phba->fc_pref_DID;
			phba->fc_flag |= FC_LBIT;
		}

		lpfc_linkup(phba);
		if ((mbox = mempool_alloc(phba->mbox_mem_pool, GFP_ATOMIC))) {
			lpfc_read_sparam(phba, mbox);
			mbox->mbox_cmpl = lpfc_mbx_cmpl_read_sparam;
			lpfc_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB));
		}

		if ((mbox = mempool_alloc(phba->mbox_mem_pool, GFP_ATOMIC))) {
			phba->hba_state = LPFC_LOCAL_CFG_LINK;
			lpfc_config_link(phba, mbox);
			mbox->mbox_cmpl = lpfc_mbx_cmpl_config_link;
			lpfc_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB));
		}
	} else {
		phba->fc_stat.LinkDown++;
		/* Link Down Event <eventTag> received */
		lpfc_printf_log(phba, KERN_ERR, LOG_LINK_EVENT,
				"%d:1305 Link Down Event x%x received "
				"Data: x%x x%x x%x\n",
				phba->brd_no, la->eventTag, phba->fc_eventTag,
				phba->hba_state, phba->fc_flag);

		lpfc_linkdown(phba);

		/* turn on Link Attention interrupts - no CLEAR_LA needed */
		psli->sliinit.sli_flag |= LPFC_PROCESS_LA;
		control = readl(phba->HCregaddr);
		control |= HC_LAINT_ENA;
		writel(control, phba->HCregaddr);
		readl(phba->HCregaddr); /* flush */
	}

	pmb->context1 = NULL;
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free( pmb, phba->mbox_mem_pool);
	return;
}

/*
 * This routine handles processing a REG_LOGIN mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_reg_login(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	struct lpfc_sli *psli;
	MAILBOX_t *mb;
	struct lpfc_dmabuf *mp;
	struct lpfc_nodelist *ndlp;

	psli = &phba->sli;
	mb = &pmb->mb;

	ndlp = (struct lpfc_nodelist *) pmb->context2;
	mp = (struct lpfc_dmabuf *) (pmb->context1);

	/* The mailbox was populated by the HBA.  Flush it to main store for the
	 * driver.  Note that all context buffers are from the driver's
	 * dma pool and have length LPFC_BPL_SIZE.
	 */
	pci_dma_sync_single_for_cpu(phba->pcidev, mp->phys, LPFC_BPL_SIZE,
			PCI_DMA_FROMDEVICE);

	pmb->context1 = NULL;

	/* Good status, call state machine */
	lpfc_disc_state_machine(phba, ndlp, pmb, NLP_EVT_CMPL_REG_LOGIN);
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free( pmb, phba->mbox_mem_pool);

	return;
}

/*
 * This routine handles processing a Fabric REG_LOGIN mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_fabric_reg_login(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	struct lpfc_sli *psli;
	MAILBOX_t *mb;
	struct lpfc_dmabuf *mp;
	struct lpfc_nodelist *ndlp;
	struct lpfc_nodelist *ndlp_fdmi;


	psli = &phba->sli;
	mb = &pmb->mb;

	ndlp = (struct lpfc_nodelist *) pmb->context2;
	mp = (struct lpfc_dmabuf *) (pmb->context1);

	if (mb->mbxStatus) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		mempool_free( pmb, phba->mbox_mem_pool);
		mempool_free( ndlp, phba->nlp_mem_pool);

		/* FLOGI failed, so just use loop map to make discovery list */
		lpfc_disc_list_loopmap(phba);

		/* Start discovery */
		lpfc_disc_start(phba);
		return;
	}

	/* The mailbox was populated by the HBA.  Flush it to main store for the
	 * driver.  Note that all context buffers are from the driver's
	 * dma pool and have length LPFC_BPL_SIZE.
	 */
	pci_dma_sync_single_for_cpu(phba->pcidev, mp->phys, LPFC_BPL_SIZE,
			PCI_DMA_FROMDEVICE);

	pmb->context1 = NULL;

	if (ndlp->nlp_rpi != 0)
		lpfc_findnode_remove_rpi(phba, ndlp->nlp_rpi);
	ndlp->nlp_rpi = mb->un.varWords[0];
	lpfc_addnode_rpi(phba, ndlp, ndlp->nlp_rpi);
	ndlp->nlp_type |= NLP_FABRIC;
	ndlp->nlp_state = NLP_STE_UNMAPPED_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_UNMAPPED_LIST);

	if (phba->hba_state == LPFC_FABRIC_CFG_LINK) {
		/* This NPort has been assigned an NPort_ID by the fabric as a
		 * result of the completed fabric login.  Issue a State Change
		 * Registration (SCR) ELS request to the fabric controller
		 * (SCR_DID) so that this NPort gets RSCN events from the
		 * fabric.
		 */
		lpfc_issue_els_scr(phba, SCR_DID, 0);

		/* Allocate a new node instance.  If the pool is empty, just
		 * start the discovery process and skip the Nameserver login
		 * process.  This is attempted again later on.  Otherwise, issue
		 * a Port Login (PLOGI) to the NameServer
		 */
		if ((ndlp = mempool_alloc(phba->nlp_mem_pool, GFP_ATOMIC))
		    == 0) {
			lpfc_disc_start(phba);
		} else {
			lpfc_nlp_init(phba, ndlp, NameServer_DID);
			ndlp->nlp_type |= NLP_FABRIC;
			ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
			lpfc_nlp_list(phba, ndlp, NLP_PLOGI_LIST);
			lpfc_issue_els_plogi(phba, ndlp, 0);
			if (phba->cfg_fdmi_on) {
				if ((ndlp_fdmi = mempool_alloc(
						       phba->nlp_mem_pool,
						       GFP_ATOMIC))) {
					lpfc_nlp_init(phba, ndlp_fdmi,
						FDMI_DID);
					ndlp_fdmi->nlp_type |= NLP_FABRIC;
					ndlp_fdmi->nlp_state =
					    NLP_STE_PLOGI_ISSUE;
					lpfc_issue_els_plogi(phba, ndlp_fdmi,
							     0);
				}
			}
		}
	}

	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free( pmb, phba->mbox_mem_pool);

	return;
}

/*
 * This routine handles processing a NameServer REG_LOGIN mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_ns_reg_login(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	struct lpfc_sli *psli;
	MAILBOX_t *mb;
	struct lpfc_dmabuf *mp;
	struct lpfc_nodelist *ndlp;

	psli = &phba->sli;
	mb = &pmb->mb;

	ndlp = (struct lpfc_nodelist *) pmb->context2;
	mp = (struct lpfc_dmabuf *) (pmb->context1);

	if (mb->mbxStatus) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		mempool_free( pmb, phba->mbox_mem_pool);
		lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);

		/* RegLogin failed, so just use loop map to make discovery
		   list */
		lpfc_disc_list_loopmap(phba);

		/* Start discovery */
		lpfc_disc_start(phba);
		return;
	}

	/* The mailbox was populated by the HBA.  Flush it to main store for the
	 * driver.  Note that all context buffers are from the driver's
	 * dma pool and have length LPFC_BPL_SIZE.
	 */
	pci_dma_sync_single_for_cpu(phba->pcidev, mp->phys, LPFC_BPL_SIZE,
			PCI_DMA_FROMDEVICE);

	pmb->context1 = NULL;

	if (ndlp->nlp_rpi != 0)
		lpfc_findnode_remove_rpi(phba, ndlp->nlp_rpi);
	ndlp->nlp_rpi = mb->un.varWords[0];
	lpfc_addnode_rpi(phba, ndlp, ndlp->nlp_rpi);
	ndlp->nlp_type |= NLP_FABRIC;
	ndlp->nlp_state = NLP_STE_UNMAPPED_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_UNMAPPED_LIST);

	if (phba->hba_state < LPFC_HBA_READY) {
		/* Link up discovery requires Fabrib registration. */
		lpfc_ns_cmd(phba, ndlp, SLI_CTNS_RNN_ID);
		lpfc_ns_cmd(phba, ndlp, SLI_CTNS_RSNN_NN);
		lpfc_ns_cmd(phba, ndlp, SLI_CTNS_RFT_ID);
	}

	phba->fc_ns_retry = 0;
	/* Good status, issue CT Request to NameServer */
	if (lpfc_ns_cmd(phba, ndlp, SLI_CTNS_GID_FT)) {
		/* Cannot issue NameServer Query, so finish up discovery */
		lpfc_disc_start(phba);
	}

	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free( pmb, phba->mbox_mem_pool);

	return;
}

/* Put blp on the bind list */
int
lpfc_consistent_bind_save(struct lpfc_hba * phba, struct lpfc_bindlist * blp)
{
	/* Put it at the end of the bind list */
	list_add_tail(&blp->nlp_listp, &phba->fc_nlpbind_list);
	phba->fc_bind_cnt++;

	/* Add scsiid <sid> to BIND list */
	lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
			"%d:0903 Add scsiid %d to BIND list "
			"Data: x%x x%x x%x x%p\n",
			phba->brd_no, blp->nlp_sid, phba->fc_bind_cnt,
			blp->nlp_DID, blp->nlp_bind_type, blp);

	return (0);
}

int
lpfc_nlp_list(struct lpfc_hba * phba, struct lpfc_nodelist * nlp, int list)
{
	struct lpfc_bindlist *blp;
	struct lpfc_target   *targetp;
	struct lpfc_sli      *psli;
	psli = &phba->sli;

	/* Sanity check to ensure we are not moving to / from the same list */
	if((nlp->nlp_flag & NLP_LIST_MASK) == list) {
		if(list != NLP_NO_LIST)
			return(0);
	}

	blp = nlp->nlp_listp_bind;

	switch(nlp->nlp_flag & NLP_LIST_MASK) {
	case NLP_NO_LIST: /* Not on any list */
		break;
	case NLP_UNUSED_LIST:
		phba->fc_unused_cnt--;
		list_del(&nlp->nlp_listp);
		nlp->nlp_flag &= ~NLP_LIST_MASK;
		break;
	case NLP_PLOGI_LIST:
		phba->fc_plogi_cnt--;
		list_del(&nlp->nlp_listp);
		nlp->nlp_flag &= ~NLP_LIST_MASK;
		break;
	case NLP_ADISC_LIST:
		phba->fc_adisc_cnt--;
		list_del(&nlp->nlp_listp);
		nlp->nlp_flag &= ~NLP_LIST_MASK;
		break;
	case NLP_REGLOGIN_LIST:
		phba->fc_reglogin_cnt--;
		list_del(&nlp->nlp_listp);
		nlp->nlp_flag &= ~NLP_LIST_MASK;
		break;
	case NLP_PRLI_LIST:
		phba->fc_prli_cnt--;
		list_del(&nlp->nlp_listp);
		nlp->nlp_flag &= ~NLP_LIST_MASK;
		break;
	case NLP_UNMAPPED_LIST:
		phba->fc_unmap_cnt--;
		list_del(&nlp->nlp_listp);
		nlp->nlp_flag &= ~NLP_LIST_MASK;
		nlp->nlp_flag &= ~NLP_TGT_NO_SCSIID;
	  	nlp->nlp_type &= ~NLP_FC_NODE;
		phba->nport_event_cnt++;
		break;
	case NLP_MAPPED_LIST:
		phba->fc_map_cnt--;
		list_del(&nlp->nlp_listp);
		nlp->nlp_flag &= ~NLP_LIST_MASK;
		phba->nport_event_cnt++;
		lpfc_set_failmask(phba, nlp, LPFC_DEV_DISAPPEARED,
			  LPFC_SET_BITMASK);
	  	nlp->nlp_type &= ~NLP_FCP_TARGET;
		targetp = nlp->nlp_Target;
		if (targetp && (list != NLP_MAPPED_LIST)) {
			nlp->nlp_Target = 0;
#if defined(FC_TRANS_VER1) || defined(FC_TRANS_265_BLKPATCH)
			/*
			 * Do not block the target if the driver has just reset
			 * its interface to the hardware.
			 */
			if (phba->hba_state != LPFC_INIT_START)
				lpfc_target_block(phba, targetp);
#endif
		}

		break;
	case NLP_NPR_LIST:
		phba->fc_npr_cnt--;
		list_del(&nlp->nlp_listp);
		nlp->nlp_flag &= ~NLP_LIST_MASK;
		/* Stop delay tmo if taking node off NPR list */
		if ((nlp->nlp_flag & NLP_DELAY_TMO) &&
		   (list != NLP_NPR_LIST)) {
			nlp->nlp_flag &= ~NLP_DELAY_TMO;
			del_timer_sync(&nlp->nlp_delayfunc);
		}
		break;
	}

	/* Add NPort <did> to <num> list */
	lpfc_printf_log(phba,
			KERN_INFO,
			LOG_NODE,
			"%d:0904 Add NPort x%x to %d list Data: x%x x%p\n",
			phba->brd_no,
			nlp->nlp_DID, list, nlp->nlp_flag, blp);

	nlp->nlp_listp_bind = NULL;

	switch(list) {
	case NLP_NO_LIST: /* No list, just remove it */
		lpfc_nlp_remove(phba, nlp);
		break;
	case NLP_UNUSED_LIST:
		nlp->nlp_flag |= list;
		/* Put it at the end of the unused list */
		list_add_tail(&nlp->nlp_listp, &phba->fc_unused_list);
		phba->fc_unused_cnt++;
		break;
	case NLP_PLOGI_LIST:
		nlp->nlp_flag |= list;
		/* Put it at the end of the plogi list */
		list_add_tail(&nlp->nlp_listp, &phba->fc_plogi_list);
		phba->fc_plogi_cnt++;
		break;
	case NLP_ADISC_LIST:
		nlp->nlp_flag |= list;
		/* Put it at the end of the adisc list */
		list_add_tail(&nlp->nlp_listp, &phba->fc_adisc_list);
		phba->fc_adisc_cnt++;
		break;
	case NLP_REGLOGIN_LIST:
		nlp->nlp_flag |= list;
		/* Put it at the end of the reglogin list */
		list_add_tail(&nlp->nlp_listp, &phba->fc_reglogin_list);
		phba->fc_reglogin_cnt++;
		break;
	case NLP_PRLI_LIST:
		nlp->nlp_flag |= list;
		/* Put it at the end of the prli list */
		list_add_tail(&nlp->nlp_listp, &phba->fc_prli_list);
		phba->fc_prli_cnt++;
		break;
	case NLP_UNMAPPED_LIST:
		nlp->nlp_flag |= list;
		/* Put it at the end of the unmap list */
		list_add_tail(&nlp->nlp_listp, &phba->fc_nlpunmap_list);
		phba->fc_unmap_cnt++;
		phba->nport_event_cnt++;
		/* stop nodev tmo if running */
		if (nlp->nlp_flag & NLP_NODEV_TMO) {
			del_timer_sync(&nlp->nlp_tmofunc);
		}
		nlp->nlp_flag &= ~NLP_NODEV_TMO;
	  	nlp->nlp_type |= NLP_FC_NODE;
		lpfc_set_failmask(phba, nlp, LPFC_DEV_DISCOVERY_INP,
				  LPFC_CLR_BITMASK);
		break;
	case NLP_MAPPED_LIST:
		nlp->nlp_flag |= list;
		/* Put it at the end of the map list */
		list_add_tail(&nlp->nlp_listp, &phba->fc_nlpmap_list);
		phba->fc_map_cnt++;
		phba->nport_event_cnt++;
		/* stop nodev tmo if running */
		if (nlp->nlp_flag & NLP_NODEV_TMO) {
			del_timer_sync(&nlp->nlp_tmofunc);
		}
		nlp->nlp_flag &= ~NLP_NODEV_TMO;
	  	nlp->nlp_type |= NLP_FCP_TARGET;
		lpfc_set_failmask(phba, nlp, LPFC_DEV_DISAPPEARED,
			  LPFC_CLR_BITMASK);
		lpfc_set_failmask(phba, nlp, LPFC_DEV_DISCOVERY_INP,
				  LPFC_CLR_BITMASK);

		targetp = phba->device_queue_hash[nlp->nlp_sid];
		if (targetp && targetp->pnode) {
			nlp->nlp_Target = targetp;
#if defined(FC_TRANS_VER1) || defined(FC_TRANS_265_BLKPATCH)
			/* Unblock I/Os on target */
			if(targetp->blocked)
				lpfc_target_unblock(phba, targetp);
#endif
		}
		break;
	case NLP_NPR_LIST:
		nlp->nlp_flag |= list;
		/* Put it at the end of the npr list */
		list_add_tail(&nlp->nlp_listp, &phba->fc_npr_list);
		phba->fc_npr_cnt++;

		/*
		 * Sanity check to ensure not a Fabric entity.
		 * Set nodev tmo for NPR state, except for Fabric.
		 */
		if (!(nlp->nlp_type & NLP_FABRIC)) {
			mod_timer(&nlp->nlp_tmofunc,
			    jiffies + HZ * phba->cfg_nodev_tmo);
			nlp->nlp_flag |= NLP_NODEV_TMO;
		}
		else {
			if(nlp->nlp_flag & NLP_NODEV_TMO) {
				del_timer_sync(&nlp->nlp_tmofunc);
			}
			nlp->nlp_flag &= ~NLP_NODEV_TMO;
		}
		nlp->nlp_flag &= ~NLP_RCV_PLOGI;
		break;
	case NLP_JUST_DQ:
		break;
	}

	if (blp) {
		nlp->nlp_sid = 0;
		nlp->nlp_flag &= ~NLP_SEED_MASK;
		nlp->nlp_Target = NULL;
		lpfc_consistent_bind_save(phba, blp);
	}
	return (0);
}

/*
 * Start / ReStart rescue timer for Discovery / RSCN handling
 */
void
lpfc_set_disctmo(struct lpfc_hba * phba)
{
	uint32_t tmo;

	tmo = ((phba->fc_ratov * 2) + 1);

	mod_timer(&phba->fc_disctmo, jiffies + HZ * tmo);
	phba->fc_flag |= FC_DISC_TMO;

	/* Start Discovery Timer state <hba_state> */
	lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
			"%d:0247 Start Discovery Timer state x%x "
			"Data: x%x x%lx x%x x%x\n",
			phba->brd_no,
			phba->hba_state, tmo, (unsigned long)&phba->fc_disctmo,
			phba->fc_plogi_cnt, phba->fc_adisc_cnt);

	return;
}

/*
 * Cancel rescue timer for Discovery / RSCN handling
 */
int
lpfc_can_disctmo(struct lpfc_hba * phba)
{
	/* Turn off discovery timer if its running */
	if(phba->fc_flag & FC_DISC_TMO) {
		del_timer_sync(&phba->fc_disctmo);
	}
	phba->fc_flag &= ~FC_DISC_TMO;

	/* Cancel Discovery Timer state <hba_state> */
	lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
			"%d:0248 Cancel Discovery Timer state x%x "
			"Data: x%x x%x x%x\n",
			phba->brd_no, phba->hba_state, phba->fc_flag,
			phba->fc_plogi_cnt, phba->fc_adisc_cnt);

	return (0);
}

/*
 * Check specified ring for outstanding IOCB on the SLI queue
 * Return true if iocb matches the specified nport
 */
int
lpfc_check_sli_ndlp(struct lpfc_hba * phba,
		    struct lpfc_sli_ring * pring,
		    struct lpfc_iocbq * iocb, struct lpfc_nodelist * ndlp)
{
	struct lpfc_sli *psli;
	IOCB_t *icmd;

	psli = &phba->sli;
	icmd = &iocb->iocb;
	if (pring->ringno == LPFC_ELS_RING) {
		switch (icmd->ulpCommand) {
		case CMD_GEN_REQUEST64_CR:
			if (icmd->ulpContext == (volatile ushort)ndlp->nlp_rpi)
				return (1);
		case CMD_ELS_REQUEST64_CR:
		case CMD_XMIT_ELS_RSP64_CX:
			if (iocb->context1 == (uint8_t *) ndlp)
				return (1);
		}
	} else if (pring->ringno == psli->ip_ring) {

	} else if (pring->ringno == psli->fcp_ring) {
		/* Skip match check if waiting to relogin to FCP target */
	  	if ((ndlp->nlp_type & NLP_FCP_TARGET) &&
		  (ndlp->nlp_flag & NLP_DELAY_TMO)) {
			return (0);
		}
		if (icmd->ulpContext == (volatile ushort)ndlp->nlp_rpi) {
			return (1);
		}
	} else if (pring->ringno == psli->next_ring) {

	}
	return (0);
}

/*
 * Free resources / clean up outstanding I/Os
 * associated with nlp_rpi in the LPFC_NODELIST entry.
 */
static int
lpfc_no_rpi(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp)
{
	struct lpfc_sli *psli;
	struct lpfc_sli_ring *pring;
	struct lpfc_iocbq *iocb, *next_iocb;
	IOCB_t *icmd;
	uint32_t rpi, i;

	psli = &phba->sli;
	rpi = ndlp->nlp_rpi;
	if (rpi) {
		/* Now process each ring */
		for (i = 0; i < psli->sliinit.num_rings; i++) {
			pring = &psli->ring[i];

			list_for_each_entry_safe(iocb, next_iocb, &pring->txq,
						list) {
				/*
				 * Check to see if iocb matches the nport we are
				 * looking for
				 */
				if ((lpfc_check_sli_ndlp
				     (phba, pring, iocb, ndlp))) {
					/* It matches, so deque and call compl
					   with an error */
					list_del(&iocb->list);
					pring->txq_cnt--;
					if (iocb->iocb_cmpl) {
						icmd = &iocb->iocb;
						icmd->ulpStatus =
						    IOSTAT_LOCAL_REJECT;
						icmd->un.ulpWord[4] =
						    IOERR_SLI_ABORTED;
						(iocb->iocb_cmpl) (phba,
								   iocb, iocb);
					} else {
						mempool_free(iocb,
						     phba->iocb_mem_pool);
					}
				}
			}
			/* Everything that matches on txcmplq will be returned
			 * by firmware with a no rpi error.
			 */
		}
	}
	return (0);
}

/*
 * Free rpi associated with LPFC_NODELIST entry.
 * This routine is called from lpfc_freenode(), when we are removing
 * a LPFC_NODELIST entry. It is also called if the driver initiates a
 * LOGO that completes successfully, and we are waiting to PLOGI back
 * to the remote NPort. In addition, it is called after we receive
 * and unsolicated ELS cmd, send back a rsp, the rsp completes and
 * we are waiting to PLOGI back to the remote NPort.
 */
int
lpfc_unreg_rpi(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp)
{
	LPFC_MBOXQ_t *mbox;

	if (ndlp->nlp_rpi) {
		if ((mbox = mempool_alloc(phba->mbox_mem_pool, GFP_ATOMIC))) {
			lpfc_unreg_login(phba, ndlp->nlp_rpi, mbox);
			if (lpfc_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				mempool_free( mbox, phba->mbox_mem_pool);
			}
		}
		lpfc_findnode_remove_rpi(phba, ndlp->nlp_rpi);
		lpfc_no_rpi(phba, ndlp);
		ndlp->nlp_rpi = 0;
		lpfc_set_failmask(phba, ndlp, LPFC_DEV_DISCONNECTED,
				  LPFC_SET_BITMASK);
		return 1;
	}
	return 0;
}

/*
 * Free resources associated with LPFC_NODELIST entry
 * so it can be freed.
 */
static int
lpfc_freenode(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp)
{
	struct lpfc_target *targetp;
	struct lpfc_sli *psli;
	int scsid;

	/* The psli variable gets rid of the long pointer deference. */
	psli = &phba->sli;

	/* Cleanup node for NPort <nlp_DID> */
	lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
			"%d:0900 Cleanup node for NPort x%x "
			"Data: x%x x%x x%x\n",
			phba->brd_no, ndlp->nlp_DID, ndlp->nlp_flag,
			ndlp->nlp_state, ndlp->nlp_rpi);

	lpfc_nlp_list(phba, ndlp, NLP_JUST_DQ);

	if(ndlp->nlp_flag & NLP_NODEV_TMO) {
		del_timer_sync(&ndlp->nlp_tmofunc);
	}
	ndlp->nlp_flag &= ~NLP_NODEV_TMO;

	if(ndlp->nlp_flag & NLP_DELAY_TMO) {
		del_timer_sync(&ndlp->nlp_delayfunc);
	}
	ndlp->nlp_flag &= ~NLP_DELAY_TMO;

	lpfc_unreg_rpi(phba, ndlp);

	for(scsid=0;scsid<MAX_FCP_TARGET;scsid++) {
		targetp = phba->device_queue_hash[scsid];
		/* First see if the SCSI ID has an allocated struct
		   lpfc_target */
		if (targetp) {
			if (targetp->pnode == ndlp) {
				targetp->pnode = NULL;
				ndlp->nlp_Target = NULL;
#ifdef FC_TRANS_VER1
				/*
				 * This code does not apply to SLES9 since there
				 * is no starget defined in the midlayer.
				 * Additionally, dynamic target discovery to the
				 * midlayer is not supported yet.
				 *
				 * Remove SCSI target / SCSI Hotplug.
				 */
				lpfc_target_remove(phba, targetp);
#endif
				break;
			}
		}
	}
	return (0);
}

/*
 * Check to see if we can free the nlp back to the freelist.
 * If we are in the middle of using the nlp in the discovery state
 * machine, defer the free till we reach the end of the state machine.
 */
int
lpfc_nlp_remove(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp)
{
	if(ndlp->nlp_flag & NLP_NODEV_TMO) {
		del_timer_sync(&ndlp->nlp_tmofunc);
	}
	ndlp->nlp_flag &= ~NLP_NODEV_TMO;

	if(ndlp->nlp_flag & NLP_DELAY_TMO) {
		del_timer_sync(&ndlp->nlp_delayfunc);
	}
	ndlp->nlp_flag &= ~NLP_DELAY_TMO;

	if (ndlp->nlp_disc_refcnt) {
		ndlp->nlp_flag |= NLP_DELAY_REMOVE;
	}
	else {
		lpfc_freenode(phba, ndlp);
		mempool_free( ndlp, phba->nlp_mem_pool);
	}
	return(0);
}

static int
lpfc_matchdid(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp, uint32_t did)
{
	D_ID mydid;
	D_ID ndlpdid;
	D_ID matchdid;

	if (did == Bcast_DID)
		return (0);

	if (ndlp->nlp_DID == 0) {
		return (0);
	}

	/* First check for Direct match */
	if (ndlp->nlp_DID == did)
		return (1);

	/* Next check for area/domain identically equals 0 match */
	mydid.un.word = phba->fc_myDID;
	if ((mydid.un.b.domain == 0) && (mydid.un.b.area == 0)) {
		return (0);
	}

	matchdid.un.word = did;
	ndlpdid.un.word = ndlp->nlp_DID;
	if (matchdid.un.b.id == ndlpdid.un.b.id) {
		if ((mydid.un.b.domain == matchdid.un.b.domain) &&
		    (mydid.un.b.area == matchdid.un.b.area)) {
			if ((ndlpdid.un.b.domain == 0) &&
			    (ndlpdid.un.b.area == 0)) {
				if (ndlpdid.un.b.id)
					return (1);
			}
			return (0);
		}

		matchdid.un.word = ndlp->nlp_DID;
		if ((mydid.un.b.domain == ndlpdid.un.b.domain) &&
		    (mydid.un.b.area == ndlpdid.un.b.area)) {
			if ((matchdid.un.b.domain == 0) &&
			    (matchdid.un.b.area == 0)) {
				if (matchdid.un.b.id)
					return (1);
			}
		}
	}
	return (0);
}

/* Search for a nodelist entry on a specific list */
struct lpfc_nodelist *
lpfc_findnode_wwpn(struct lpfc_hba * phba, uint32_t order,
		   struct lpfc_name * wwpn)
{
	struct lpfc_nodelist *ndlp, *next_ndlp;
	uint32_t data1;

	if (order & NLP_SEARCH_UNMAPPED) {
		list_for_each_entry_safe(ndlp, next_ndlp,
					 &phba->fc_nlpunmap_list, nlp_listp) {
			if (memcmp(&ndlp->nlp_portname, wwpn,
				   sizeof(struct lpfc_name)) == 0) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				/* FIND node DID unmapped */
				lpfc_printf_log(phba,
						KERN_INFO,
						LOG_NODE,
						"%d:0911 FIND node DID unmapped"
						" Data: x%p x%x x%x x%x\n",
						phba->brd_no,
						ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				return (ndlp);
			}
		}
	}

	if (order & NLP_SEARCH_MAPPED) {
		list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_nlpmap_list,
					nlp_listp) {
			if (memcmp(&ndlp->nlp_portname, wwpn,
				   sizeof(struct lpfc_name)) == 0) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				/* FIND node DID mapped */
				lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
						"%d:0901 FIND node DID mapped "
						"Data: x%p x%x x%x x%x\n",
						phba->brd_no,
						ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				return (ndlp);
			}
		}
	}

	/* no match found */
	return ((struct lpfc_nodelist *) 0);
}
/* Search for a nodelist entry on a specific list */
struct lpfc_nodelist *
lpfc_findnode_wwnn(struct lpfc_hba * phba, uint32_t order,
		   struct lpfc_name * wwnn)
{
	struct lpfc_nodelist *ndlp, *next_ndlp;
	uint32_t data1;

	if (order & NLP_SEARCH_UNMAPPED) {
		list_for_each_entry_safe(ndlp, next_ndlp,
					 &phba->fc_nlpunmap_list, nlp_listp) {
			if (memcmp(&ndlp->nlp_nodename, wwnn,
				   sizeof(struct lpfc_name)) ==  0) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				/* FIND node DID unmapped */
				lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
						"%d:0910 FIND node DID unmapped"
						"Data: x%p x%x x%x x%x\n",
						phba->brd_no,
						ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				return (ndlp);
			}
		}
	}

	if (order & NLP_SEARCH_MAPPED) {
		list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_nlpmap_list,
					 nlp_listp) {
			if (memcmp(&ndlp->nlp_nodename, wwnn,
				   sizeof(struct lpfc_name)) == 0) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				/* FIND node did mapped */
				lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
						"%d:0902 FIND node DID mapped "
						"Data: x%p x%x x%x x%x\n",
						phba->brd_no,
						ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				return (ndlp);
			}
		}
	}

	/* no match found */
	return ((struct lpfc_nodelist *) 0);
}
/* Search for a nodelist entry on a specific list */
struct lpfc_nodelist *
lpfc_findnode_did(struct lpfc_hba * phba, uint32_t order, uint32_t did)
{
	struct lpfc_nodelist *ndlp, *next_ndlp;
	uint32_t data1;

	if (order & NLP_SEARCH_UNMAPPED) {
		list_for_each_entry_safe(ndlp, next_ndlp,
					 &phba->fc_nlpunmap_list, nlp_listp) {
			if (lpfc_matchdid(phba, ndlp, did)) {
				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				/* FIND node DID unmapped */
				lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
						"%d:0929 FIND node DID unmapped"
						" Data: x%p x%x x%x x%x\n",
						phba->brd_no,
						ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				return (ndlp);
			}
		}
	}

	if (order & NLP_SEARCH_MAPPED) {
		list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_nlpmap_list,
					nlp_listp) {
			if (lpfc_matchdid(phba, ndlp, did)) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				/* FIND node DID mapped */
				lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
						"%d:0930 FIND node DID mapped "
						"Data: x%p x%x x%x x%x\n",
						phba->brd_no,
						ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				return (ndlp);
			}
		}
	}

	if (order & NLP_SEARCH_PLOGI) {
		list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_plogi_list,
					nlp_listp) {
			if (lpfc_matchdid(phba, ndlp, did)) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				/* LOG change to PLOGI */
				/* FIND node DID plogi */
				lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
						"%d:0908 FIND node DID plogi "
						"Data: x%p x%x x%x x%x\n",
						phba->brd_no,
						ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				return (ndlp);
			}
		}
	}

	if (order & NLP_SEARCH_ADISC) {
		list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_adisc_list,
					nlp_listp) {
			if (lpfc_matchdid(phba, ndlp, did)) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				/* LOG change to ADISC */
				/* FIND node DID adisc */
				lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
						"%d:0931 FIND node DID adisc "
						"Data: x%p x%x x%x x%x\n",
						phba->brd_no,
						ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				return (ndlp);
			}
		}
	}

	if (order & NLP_SEARCH_REGLOGIN) {
		list_for_each_entry_safe(ndlp, next_ndlp,
					 &phba->fc_reglogin_list, nlp_listp) {
			if (lpfc_matchdid(phba, ndlp, did)) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				/* LOG change to REGLOGIN */
				/* FIND node DID reglogin */
				lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
						"%d:0931 FIND node DID reglogin"
						" Data: x%p x%x x%x x%x\n",
						phba->brd_no,
						ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				return (ndlp);
			}
		}
	}

	if (order & NLP_SEARCH_PRLI) {
		list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_prli_list,
					nlp_listp) {
			if (lpfc_matchdid(phba, ndlp, did)) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				/* LOG change to PRLI */
				/* FIND node DID prli */
				lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
						"%d:0931 FIND node DID prli "
						"Data: x%p x%x x%x x%x\n",
						phba->brd_no,
						ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				return (ndlp);
			}
		}
	}

	if (order & NLP_SEARCH_NPR) {
		list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_npr_list,
					nlp_listp) {
			if (lpfc_matchdid(phba, ndlp, did)) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				/* LOG change to NPR */
				/* FIND node DID npr */
				lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
						"%d:0931 FIND node DID npr "
						"Data: x%p x%x x%x x%x\n",
						phba->brd_no,
						ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				return (ndlp);
			}
		}
	}

	if (order & NLP_SEARCH_UNUSED) {
		list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_adisc_list,
					nlp_listp) {
			if (lpfc_matchdid(phba, ndlp, did)) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				/* LOG change to UNUSED */
				/* FIND node DID unused */
				lpfc_printf_log(phba, KERN_INFO, LOG_NODE,
						"%d:0931 FIND node DID unused "
						"Data: x%p x%x x%x x%x\n",
						phba->brd_no,
						ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				return (ndlp);
			}
		}
	}

	/* FIND node did <did> NOT FOUND */
	lpfc_printf_log(phba,
			KERN_INFO,
			LOG_NODE,
			"%d:0932 FIND node did x%x NOT FOUND Data: x%x\n",
			phba->brd_no, did, order);

	/* no match found */
	return ((struct lpfc_nodelist *) 0);
}

struct lpfc_nodelist *
lpfc_setup_disc_node(struct lpfc_hba * phba, uint32_t did)
{
	struct lpfc_nodelist *ndlp;
	uint32_t flg;

	if((ndlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL, did)) == 0) {
		if ((phba->hba_state == LPFC_HBA_READY) &&
		   ((lpfc_rscn_payload_check(phba, did) == 0)))
			return NULL;
		ndlp = (struct lpfc_nodelist *)
		     mempool_alloc(phba->nlp_mem_pool, GFP_ATOMIC);
		if (!ndlp)
			return NULL;
		lpfc_nlp_init(phba, ndlp, did);
		ndlp->nlp_state = NLP_STE_NPR_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
		ndlp->nlp_flag |= NLP_NPR_2B_DISC;
		return ndlp;
	}
	if ((phba->hba_state == LPFC_HBA_READY) &&
	    (phba->fc_flag & FC_RSCN_MODE)) {
		if(lpfc_rscn_payload_check(phba, did)) {
			ndlp->nlp_flag |= NLP_NPR_2B_DISC;
		}
		else {
			ndlp->nlp_flag &= ~NLP_NPR_2B_DISC;
			ndlp = NULL;
		}
	}
	else {
		flg = ndlp->nlp_flag & NLP_LIST_MASK;
		if ((flg == NLP_ADISC_LIST) ||
	    	(flg == NLP_PLOGI_LIST)) {
			return NULL;
		}
		ndlp->nlp_state = NLP_STE_NPR_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
		ndlp->nlp_flag |= NLP_NPR_2B_DISC;
	}
	return ndlp;
}

/* Build a list of nodes to discover based on the loopmap */
void
lpfc_disc_list_loopmap(struct lpfc_hba * phba)
{
	int j;
	uint32_t alpa, index;

	if (phba->hba_state <= LPFC_LINK_DOWN) {
		return;
	}
	if (phba->fc_topology != TOPOLOGY_LOOP) {
		return;
	}

	/* Check for loop map present or not */
	if (phba->alpa_map[0]) {
		for (j = 1; j <= phba->alpa_map[0]; j++) {
			alpa = phba->alpa_map[j];

			if (((phba->fc_myDID & 0xff) == alpa) || (alpa == 0)) {
				continue;
			}
			lpfc_setup_disc_node(phba, alpa);
		}
	} else {
		/* No alpamap, so try all alpa's */
		for (j = 0; j < FC_MAXLOOP; j++) {
			/* If cfg_scan_down is set, start from highest
			 * ALPA (0xef) to lowest (0x1).
			 */
			if (phba->cfg_scan_down)
				index = j;
			else
				index = FC_MAXLOOP - j - 1;
			alpa = lpfcAlpaArray[index];
			if ((phba->fc_myDID & 0xff) == alpa) {
				continue;
			}

			lpfc_setup_disc_node(phba, alpa);
		}
	}
	return;
}

/* Start Link up / RSCN discovery on NPR list */
void
lpfc_disc_start(struct lpfc_hba * phba)
{
	struct lpfc_sli *psli;
	LPFC_MBOXQ_t *mbox;
	struct lpfc_nodelist *ndlp, *next_ndlp;
	uint32_t did_changed, num_sent;
	uint32_t clear_la_pending;

	psli = &phba->sli;

	if (phba->hba_state <= LPFC_LINK_DOWN) {
		return;
	}
	if (phba->hba_state == LPFC_CLEAR_LA)
		clear_la_pending = 1;
	else
		clear_la_pending = 0;

	if (phba->hba_state < LPFC_HBA_READY) {
		phba->hba_state = LPFC_DISC_AUTH;
	}
	lpfc_set_disctmo(phba);

	if (phba->fc_prevDID == phba->fc_myDID) {
		did_changed = 0;
	} else {
		did_changed = 1;
	}
	phba->fc_prevDID = phba->fc_myDID;
	phba->num_disc_nodes = 0;

	/* Start Discovery state <hba_state> */
	lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
			"%d:0202 Start Discovery hba state x%x "
			"Data: x%x x%x x%x\n",
			phba->brd_no, phba->hba_state, phba->fc_flag,
			phba->fc_plogi_cnt, phba->fc_adisc_cnt);

	/* If our did changed, we MUST do PLOGI */
	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_npr_list,
				nlp_listp) {
		if(ndlp->nlp_flag & NLP_NPR_2B_DISC) {
			if(did_changed)
				ndlp->nlp_flag &= ~NLP_NPR_ADISC;
		}
	}

	/* First do ADISCs - if any */
	num_sent = lpfc_els_disc_adisc(phba);

	if(num_sent)
		return;

	if ((phba->hba_state < LPFC_HBA_READY) && (!clear_la_pending)) {
		/* If we get here, there is nothing to ADISC */
		if ((mbox = mempool_alloc(phba->mbox_mem_pool, GFP_ATOMIC))) {
			phba->hba_state = LPFC_CLEAR_LA;
			lpfc_clear_la(phba, mbox);
			mbox->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
			if (lpfc_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				mempool_free( mbox, phba->mbox_mem_pool);
				lpfc_disc_flush_list(phba);
				psli->ring[(psli->ip_ring)].flag &=
				    ~LPFC_STOP_IOCB_EVENT;
				psli->ring[(psli->fcp_ring)].flag &=
				    ~LPFC_STOP_IOCB_EVENT;
				psli->ring[(psli->next_ring)].flag &=
				    ~LPFC_STOP_IOCB_EVENT;
				phba->hba_state = LPFC_HBA_READY;
			}
		}
	} else {
		/* Next do PLOGIs - if any */
		num_sent = lpfc_els_disc_plogi(phba);

		if(num_sent)
			return;

		if (phba->fc_flag & FC_RSCN_MODE) {
			/* Check to see if more RSCNs came in while we
			 * were processing this one.
			 */
			if ((phba->fc_rscn_id_cnt == 0) &&
			    (!(phba->fc_flag & FC_RSCN_DISCOVERY))) {
				lpfc_els_flush_rscn(phba);
			} else {
				lpfc_els_handle_rscn(phba);
			}
		}
	}
	return;
}

/*
 *  Ignore completion for all IOCBs on tx and txcmpl queue for ELS
 *  ring the match the sppecified nodelist.
 */
static void
lpfc_free_tx(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp)
{
	struct lpfc_sli *psli;
	IOCB_t     *icmd;
	struct lpfc_iocbq    *iocb, *next_iocb;
	struct lpfc_sli_ring *pring;
	struct lpfc_dmabuf   *mp;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];

	/* Error matching iocb on txq or txcmplq
	 * First check the txq.
	 */
	list_for_each_entry_safe(iocb, next_iocb, &pring->txq, list) {
		if (iocb->context1 != ndlp) {
			continue;
		}
		icmd = &iocb->iocb;
		if ((icmd->ulpCommand == CMD_ELS_REQUEST64_CR) ||
		    (icmd->ulpCommand == CMD_XMIT_ELS_RSP64_CX)) {

			list_del(&iocb->list);
			pring->txq_cnt--;
			lpfc_els_free_iocb(phba, iocb);
		}
	}

	/* Next check the txcmplq */
	list_for_each_entry_safe(iocb, next_iocb, &pring->txcmplq, list) {
		if (iocb->context1 != ndlp) {
			continue;
		}
		icmd = &iocb->iocb;
		if ((icmd->ulpCommand == CMD_ELS_REQUEST64_CR) ||
		    (icmd->ulpCommand == CMD_XMIT_ELS_RSP64_CX)) {

			iocb->iocb_cmpl = NULL;
			/* context2 = cmd, context2->next = rsp, context3 =
			   bpl */
			if (iocb->context2) {
				/* Free the response IOCB before handling the
				   command. */

				mp = (struct lpfc_dmabuf *)
				     (((struct lpfc_dmabuf *) (iocb->context2))
				    ->list.next);
				if (mp) {
					/* Delay before releasing rsp buffer to
					 * give UNREG mbox a chance to take
					 * effect.
					 */
					list_add(&mp->list,
						&phba->freebufList);
				}
				lpfc_mbuf_free(phba,
					       ((struct lpfc_dmabuf *)
						iocb->context2)->virt,
					       ((struct lpfc_dmabuf *)
						iocb->context2)->phys);
				kfree(iocb->context2);
			}

			if (iocb->context3) {
				lpfc_mbuf_free(phba,
					       ((struct lpfc_dmabuf *)
						iocb->context3)->virt,
					       ((struct lpfc_dmabuf *)
						iocb->context3)->phys);
				kfree(iocb->context3);
			}
		}
	}

	return;
}

void
lpfc_disc_flush_list(struct lpfc_hba * phba)
{
	struct lpfc_nodelist *ndlp, *next_ndlp;

	if (phba->fc_plogi_cnt) {
		list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_plogi_list,
					nlp_listp) {
			lpfc_set_failmask(phba, ndlp, LPFC_DEV_DISCONNECTED,
					  LPFC_SET_BITMASK);
			lpfc_free_tx(phba, ndlp);
			list_del(&ndlp->nlp_listp);
			ndlp->nlp_flag &= ~NLP_LIST_MASK;
			lpfc_nlp_remove(phba, ndlp);
		}
	}
	if (phba->fc_adisc_cnt) {
		list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_adisc_list,
					nlp_listp) {
			lpfc_set_failmask(phba, ndlp, LPFC_DEV_DISCONNECTED,
					  LPFC_SET_BITMASK);
			lpfc_free_tx(phba, ndlp);
			list_del(&ndlp->nlp_listp);
			ndlp->nlp_flag &= ~NLP_LIST_MASK;
			lpfc_nlp_remove(phba, ndlp);
		}
	}
	return;
}

/*****************************************************************************/
/*
 * NAME:     lpfc_disc_timeout
 *
 * FUNCTION: Fibre Channel driver discovery timeout routine.
 *
 * EXECUTION ENVIRONMENT: interrupt only
 *
 * CALLED FROM:
 *      Timer function
 *
 * RETURNS:
 *      none
 */
/*****************************************************************************/
void
lpfc_disc_timeout(unsigned long ptr)
{
	struct lpfc_hba *phba;
	struct lpfc_sli *psli;
	struct lpfc_nodelist *ndlp;
	LPFC_MBOXQ_t *mbox;
	unsigned long iflag;

	phba = (struct lpfc_hba *)ptr;
	if (!phba) {
		return;
	}
	spin_lock_irqsave(phba->host->host_lock, iflag);

	psli = &phba->sli;
	phba->fc_flag &= ~FC_DISC_TMO;

	/* hba_state is identically LPFC_LOCAL_CFG_LINK while waiting for FAN */
	if (phba->hba_state == LPFC_LOCAL_CFG_LINK) {
		/* FAN timeout */
		lpfc_printf_log(phba,
				 KERN_WARNING,
				 LOG_DISCOVERY,
				 "%d:0221 FAN timeout\n",
				 phba->brd_no);

		/* Forget about FAN, Start discovery by sending a FLOGI
		 * hba_state is identically LPFC_FLOGI while waiting for FLOGI
		 * cmpl
		 */
		phba->hba_state = LPFC_FLOGI;
		lpfc_set_disctmo(phba);
		lpfc_initial_flogi(phba);
		goto out;
	}

	/* hba_state is identically LPFC_FLOGI while waiting for FLOGI cmpl */
	if (phba->hba_state == LPFC_FLOGI) {
		/* Initial FLOGI timeout */
		lpfc_printf_log(phba,
				 KERN_ERR,
				 LOG_DISCOVERY,
				 "%d:0222 Initial FLOGI timeout\n",
				 phba->brd_no);

		/* Assume no Fabric and go on with discovery.
		 * Check for outstanding ELS FLOGI to abort.
		 */

		/* FLOGI failed, so just use loop map to make discovery list */
		lpfc_disc_list_loopmap(phba);

		/* Start discovery */
		lpfc_disc_start(phba);
		goto out;
	}

	/* hba_state is identically LPFC_FABRIC_CFG_LINK while waiting for
	   NameServer login */
	if (phba->hba_state == LPFC_FABRIC_CFG_LINK) {
		/* Timeout while waiting for NameServer login */
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
				"%d:0223 Timeout while waiting for NameServer "
				"login\n", phba->brd_no);

		/* Next look for NameServer ndlp */
		if ((ndlp = lpfc_findnode_did(phba,
				       NLP_SEARCH_ALL, NameServer_DID))) {
			lpfc_nlp_remove(phba, ndlp);
		}
		/* Start discovery */
		lpfc_disc_start(phba);
		goto out;
	}

	/* Check for wait for NameServer Rsp timeout */
	if (phba->hba_state == LPFC_NS_QRY) {
		/* NameServer Query timeout */
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
				"%d:0224 NameServer Query timeout "
				"Data: x%x x%x\n",
				phba->brd_no,
				phba->fc_ns_retry, LPFC_MAX_NS_RETRY);

		if ((ndlp =
		     lpfc_findnode_did(phba, NLP_SEARCH_UNMAPPED,
				       NameServer_DID))) {
			if (phba->fc_ns_retry < LPFC_MAX_NS_RETRY) {
				/* Try it one more time */
				if (lpfc_ns_cmd(phba, ndlp, SLI_CTNS_GID_FT) ==
				    0) {
					goto out;
				}
			}
			phba->fc_ns_retry = 0;
		}

		/* Nothing to authenticate, so CLEAR_LA right now */
		if (phba->hba_state != LPFC_CLEAR_LA) {
			if ((mbox = mempool_alloc(phba->mbox_mem_pool,
						  GFP_ATOMIC))) {
				phba->hba_state = LPFC_CLEAR_LA;
				lpfc_clear_la(phba, mbox);
				mbox->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
				if (lpfc_sli_issue_mbox
				    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
				    == MBX_NOT_FINISHED) {
					mempool_free(mbox, phba->mbox_mem_pool);
					goto clrlaerr;
				}
			} else {
				/* Device Discovery completion error */
				lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
						"%d:0226 Device Discovery "
						"completion error\n",
						phba->brd_no);
				phba->hba_state = LPFC_HBA_ERROR;
			}
		}
		if ((mbox = mempool_alloc(phba->mbox_mem_pool, GFP_ATOMIC))) {
			/* Setup and issue mailbox INITIALIZE LINK command */
			lpfc_linkdown(phba);
			lpfc_init_link(phba, mbox,
				       phba->cfg_topology,
				       phba->cfg_link_speed);
			mbox->mb.un.varInitLnk.lipsr_AL_PA = 0;
			if (lpfc_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				mempool_free( mbox, phba->mbox_mem_pool);
			}
		}
		goto out;
	}

	if (phba->hba_state == LPFC_DISC_AUTH) {
		/* Node Authentication timeout */
		lpfc_printf_log(phba,
				 KERN_ERR,
				 LOG_DISCOVERY,
				 "%d:0227 Node Authentication timeout\n",
				 phba->brd_no);
		lpfc_disc_flush_list(phba);
		if (phba->hba_state != LPFC_CLEAR_LA) {
			if ((mbox = mempool_alloc(phba->mbox_mem_pool,
						  GFP_ATOMIC))) {
				phba->hba_state = LPFC_CLEAR_LA;
				lpfc_clear_la(phba, mbox);
				mbox->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
				if (lpfc_sli_issue_mbox
				    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
				    == MBX_NOT_FINISHED) {
					mempool_free(mbox, phba->mbox_mem_pool);
					goto clrlaerr;
				}
			}
		}
		goto out;
	}

	if (phba->hba_state == LPFC_CLEAR_LA) {
		/* CLEAR LA timeout */
		lpfc_printf_log(phba,
				 KERN_ERR,
				 LOG_DISCOVERY,
				 "%d:0228 CLEAR LA timeout\n",
				 phba->brd_no);
clrlaerr:
		lpfc_disc_flush_list(phba);
		psli->ring[(psli->ip_ring)].flag &= ~LPFC_STOP_IOCB_EVENT;
		psli->ring[(psli->fcp_ring)].flag &= ~LPFC_STOP_IOCB_EVENT;
		psli->ring[(psli->next_ring)].flag &= ~LPFC_STOP_IOCB_EVENT;
		phba->hba_state = LPFC_HBA_READY;
		goto out;
	}

	if ((phba->hba_state == LPFC_HBA_READY) &&
	    (phba->fc_flag & FC_RSCN_MODE)) {
		/* RSCN timeout */
		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_DISCOVERY,
				"%d:0231 RSCN timeout Data: x%x x%x\n",
				phba->brd_no,
				phba->fc_ns_retry, LPFC_MAX_NS_RETRY);

		/* Cleanup any outstanding ELS commands */
		lpfc_els_flush_cmd(phba);

		lpfc_els_flush_rscn(phba);
		lpfc_disc_flush_list(phba);
		goto out;
	}

out:
	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	return;
}

static void
lpfc_nodev_timeout(unsigned long ptr)
{
	struct lpfc_hba *phba;
	struct lpfc_nodelist *ndlp;
	struct lpfc_target *targetp;
	unsigned long iflag;
	int scsid;

	ndlp = (struct lpfc_nodelist *)ptr;
	phba = ndlp->nlp_phba;
	spin_lock_irqsave(phba->host->host_lock, iflag);

	/* Nodev timeout on NPort <nlp_DID> */
	lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
			"%d:0203 Nodev timeout on NPort x%x "
			"Data: x%x x%x x%x\n",
			phba->brd_no, ndlp->nlp_DID, ndlp->nlp_flag,
			ndlp->nlp_state, ndlp->nlp_rpi);

	ndlp->nlp_flag &= ~NLP_NODEV_TMO;

	for(scsid=0;scsid<MAX_FCP_TARGET;scsid++) {
		targetp = phba->device_queue_hash[scsid];
		/* First see if the SCSI ID has an allocated struct
		   lpfc_target */
		if (targetp) {
			if (targetp->pnode == ndlp) {
				/* flush the target */
				lpfc_sli_abort_iocb_tgt(phba,
					&phba->sli.ring[phba->sli.fcp_ring],
					scsid);
#if defined(FC_TRANS_VER1) || defined(FC_TRANS_265_BLKPATCH)
				if (targetp->blocked)
					lpfc_target_unblock(phba, targetp);
#endif
			}
		}
	}

	lpfc_disc_state_machine(phba, ndlp, NULL, NLP_EVT_DEVICE_RM);
	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	return;
}

/*****************************************************************************/
/*
 * NAME:     lpfc_find_target
 *
 * FUNCTION: Fibre Channel bus/target/LUN to struct lpfc_target lookup
 *
 * EXECUTION ENVIRONMENT:
 *
 * RETURNS:
 *      ptr to desired struct lpfc_target
 */
/*****************************************************************************/
struct lpfc_target *
lpfc_find_target(struct lpfc_hba * phba, uint32_t tgt,
	struct lpfc_nodelist *nlp)
{
	struct lpfc_target *targetp;

	/* search the mapped list for this target ID */
	if(!nlp) {
		list_for_each_entry(nlp, &phba->fc_nlpmap_list, nlp_listp) {
			if (tgt == nlp->nlp_sid)
				break;
		}

		if (&(nlp->nlp_listp) == &(phba->fc_nlpmap_list))
			return NULL;
	}

	targetp = phba->device_queue_hash[tgt];

	/* First see if the SCSI ID has an allocated struct lpfc_target */
	if (!targetp) {
		targetp = kmalloc(sizeof (struct lpfc_target), GFP_ATOMIC);
		if (!targetp)
			return NULL;

		memset(targetp, 0, sizeof (struct lpfc_target));
		phba->device_queue_hash[tgt] = targetp;
		targetp->scsi_id = tgt;

		/* Create SCSI Target <tgt> */
		lpfc_printf_log(phba,
				KERN_INFO,
				LOG_DISCOVERY | LOG_FCP,
				"%d:0204 Create SCSI Target %d\n",
				phba->brd_no, tgt);
	}

	if (targetp->pnode == NULL) {
		targetp->pnode = nlp;
		nlp->nlp_Target = targetp;
#ifdef FC_TRANS_VER1
		/*
		 * This code does not apply to SLES9 since there is no
		 * starget defined in the midlayer.  Additionally,
		 * dynamic target discovery to the midlayer is not
		 * supported yet.
		 */
		if (targetp->pnode->starget) {
			/* Add SCSI target / SCSI Hotplug */
			lpfc_target_add(phba, targetp);
		}
#endif
	}
	else {
		if(targetp->pnode != nlp) {
			/*
			 * Get rid of the old nlp before updating
			 * targetp with the new one.
			 */
			lpfc_nlp_list(phba, targetp->pnode, NLP_NO_LIST);
			targetp->pnode = nlp;
		}
	}
	nlp->nlp_Target = targetp;
	return (targetp);
}

/*
 *   lpfc_set_failmask
 *   Set, or clear, failMask bits in struct lpfc_nodelist
 */
void
lpfc_set_failmask(struct lpfc_hba * phba,
		  struct lpfc_nodelist * ndlp, uint32_t bitmask, uint32_t flag)
{
	uint32_t oldmask;
	uint32_t changed;

	/* Failmask change on NPort <nlp_DID> */
	lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
			"%d:0208 Failmask change on NPort x%x "
			"Data: x%x x%x x%x\n",
			phba->brd_no,
			ndlp->nlp_DID, ndlp->nlp_failMask, bitmask, flag);

	if (flag == LPFC_SET_BITMASK) {
		oldmask = ndlp->nlp_failMask;
		/* Set failMask event */
		ndlp->nlp_failMask |= bitmask;
		if (oldmask != ndlp->nlp_failMask) {
			changed = 1;
		} else {
			changed = 0;
		}

	} else {
		/* Clear failMask event */
		ndlp->nlp_failMask &= ~bitmask;
		changed = 1;
	}
	return;
}

/*
 * This routine handles processing a NameServer REG_LOGIN mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_fdmi_reg_login(struct lpfc_hba * phba, LPFC_MBOXQ_t * pmb)
{
	struct lpfc_sli *psli;
	MAILBOX_t *mb;
	struct lpfc_dmabuf *mp;
	struct lpfc_nodelist *ndlp;

	psli = &phba->sli;
	mb = &pmb->mb;

	ndlp = (struct lpfc_nodelist *) pmb->context2;
	mp = (struct lpfc_dmabuf *) (pmb->context1);

	/* The mailbox was populated by the HBA.  Flush it to main store for the
	 * driver.  Note that all context buffers are from the driver's
	 * dma pool and have length LPFC_BPL_SIZE.
	 */
	pci_dma_sync_single_for_cpu(phba->pcidev, mp->phys, LPFC_BPL_SIZE,
			PCI_DMA_FROMDEVICE);

	pmb->context1 = NULL;

	if (ndlp->nlp_rpi != 0)
		lpfc_findnode_remove_rpi(phba, ndlp->nlp_rpi);
	ndlp->nlp_rpi = mb->un.varWords[0];
	lpfc_addnode_rpi(phba, ndlp, ndlp->nlp_rpi);
	ndlp->nlp_type |= NLP_FABRIC;
	ndlp->nlp_state = NLP_STE_UNMAPPED_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_UNMAPPED_LIST);

	/* Start issuing Fabric-Device Management Interface (FDMI)
	 * command to 0xfffffa (FDMI well known port)
	 */
	if (phba->cfg_fdmi_on == 1) {
		lpfc_fdmi_cmd(phba, ndlp, SLI_MGMT_DHBA);
	} else {
		/*
		 * Delay issuing FDMI command if fdmi-on=2
		 * (supporting RPA/hostnmae)
		 */
		mod_timer(&phba->fc_fdmitmo, jiffies + HZ * 60);
	}

	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	mempool_free( pmb, phba->mbox_mem_pool);

	return;
}

/*
 * This routine looks up the ndlp hash
 * table for the given RPI. If rpi found
 * it return the node list pointer
 * else return 0.
 */
struct lpfc_nodelist *
lpfc_findnode_rpi(struct lpfc_hba * phba, uint16_t rpi)
{
	struct lpfc_nodelist *ret;

	ret = phba->fc_nlplookup[LPFC_RPI_HASH_FUNC(rpi)];
	while ((ret != 0) && (ret->nlp_rpi != rpi)) {
		ret = ret->nlp_rpi_hash_next;
	}
	return ret;
}

/*
 * This routine looks up the ndlp hash table for the
 * given RPI. If rpi found it return the node list
 * pointer else return 0 after deleting the entry
 * from hash table.
 */
struct lpfc_nodelist *
lpfc_findnode_remove_rpi(struct lpfc_hba * phba, uint16_t rpi)
{
	struct lpfc_nodelist *ret, *temp;;

	ret = phba->fc_nlplookup[LPFC_RPI_HASH_FUNC(rpi)];
	if (ret == 0)
		return NULL;

	if (ret->nlp_rpi == rpi) {
		phba->fc_nlplookup[LPFC_RPI_HASH_FUNC(rpi)] =
		    ret->nlp_rpi_hash_next;
		ret->nlp_rpi_hash_next = NULL;
		return ret;
	}

	while ((ret->nlp_rpi_hash_next != 0) &&
	       (ret->nlp_rpi_hash_next->nlp_rpi != rpi)) {
		ret = ret->nlp_rpi_hash_next;
	}

	if (ret->nlp_rpi_hash_next != 0) {
		temp = ret->nlp_rpi_hash_next;
		ret->nlp_rpi_hash_next = temp->nlp_rpi_hash_next;
		temp->nlp_rpi_hash_next = NULL;
		return temp;
	} else {
		return NULL;
	}
}

/*
 * This routine adds the node list entry to the
 * ndlp hash table.
 */
void
lpfc_addnode_rpi(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp,
		 uint16_t rpi)
{

	uint32_t index;

	index = LPFC_RPI_HASH_FUNC(rpi);
	ndlp->nlp_rpi_hash_next = phba->fc_nlplookup[index];
	phba->fc_nlplookup[index] = ndlp;
	return;
}

void
lpfc_nlp_init(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp,
		 uint32_t did)
{
	memset(ndlp, 0, sizeof (struct lpfc_nodelist));
	init_timer(&ndlp->nlp_tmofunc);
	ndlp->nlp_tmofunc.function = lpfc_nodev_timeout;
	ndlp->nlp_tmofunc.data = (unsigned long)ndlp;
	init_timer(&ndlp->nlp_delayfunc);
	ndlp->nlp_delayfunc.function = lpfc_els_retry_delay;
	ndlp->nlp_delayfunc.data = (unsigned long)ndlp;
	ndlp->nlp_DID = did;
	ndlp->nlp_phba = phba;
	return;
}

