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
 * $Id: lpfc_nportdisc.c 1.139 2004/10/13 17:22:59EDT sf_support Exp  $
 */

#include <linux/version.h>
#include <linux/blkdev.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <scsi/scsi_device.h>
#include "lpfc_sli.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_crtn.h"
#include "lpfc_hw.h"
#include "lpfc_logmsg.h"
#include "lpfc_mem.h"

extern uint8_t lpfcAlpaArray[];


/* Called to verify a rcv'ed ADISC was intended for us. */
static int
lpfc_check_adisc(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp,
		 struct lpfc_name * nn, struct lpfc_name * pn)
{
	/* Compare the ADISC rsp WWNN / WWPN matches our internal node
	 * table entry for that node.
	 */
	if (memcmp(nn, &ndlp->nlp_nodename, sizeof (struct lpfc_name)) != 0)
		return (0);

	if (memcmp(pn, &ndlp->nlp_portname, sizeof (struct lpfc_name)) != 0)
		return (0);

	/* we match, return success */
	return (1);
}


int
lpfc_check_sparm(struct lpfc_hba * phba,
		 struct lpfc_nodelist * ndlp, struct serv_parm * sp,
		 uint32_t class)
{
	volatile struct serv_parm *hsp = &phba->fc_sparam;
	/* First check for supported version */

	/* Next check for class validity */
	if (sp->cls1.classValid) {

		if (sp->cls1.rcvDataSizeMsb > hsp->cls1.rcvDataSizeMsb)
			sp->cls1.rcvDataSizeMsb = hsp->cls1.rcvDataSizeMsb;
		if (sp->cls1.rcvDataSizeLsb > hsp->cls1.rcvDataSizeLsb)
			sp->cls1.rcvDataSizeLsb = hsp->cls1.rcvDataSizeLsb;
	} else if (class == CLASS1) {
		return (0);
	}

	if (sp->cls2.classValid) {

		if (sp->cls2.rcvDataSizeMsb > hsp->cls2.rcvDataSizeMsb)
			sp->cls2.rcvDataSizeMsb = hsp->cls2.rcvDataSizeMsb;
		if (sp->cls2.rcvDataSizeLsb > hsp->cls2.rcvDataSizeLsb)
			sp->cls2.rcvDataSizeLsb = hsp->cls2.rcvDataSizeLsb;
	} else if (class == CLASS2) {
		return (0);
	}

	if (sp->cls3.classValid) {

		if (sp->cls3.rcvDataSizeMsb > hsp->cls3.rcvDataSizeMsb)
			sp->cls3.rcvDataSizeMsb = hsp->cls3.rcvDataSizeMsb;
		if (sp->cls3.rcvDataSizeLsb > hsp->cls3.rcvDataSizeLsb)
			sp->cls3.rcvDataSizeLsb = hsp->cls3.rcvDataSizeLsb;
	} else if (class == CLASS3) {
		return (0);
	}

	if (sp->cmn.bbRcvSizeMsb > hsp->cmn.bbRcvSizeMsb)
		sp->cmn.bbRcvSizeMsb = hsp->cmn.bbRcvSizeMsb;
	if (sp->cmn.bbRcvSizeLsb > hsp->cmn.bbRcvSizeLsb)
		sp->cmn.bbRcvSizeLsb = hsp->cmn.bbRcvSizeLsb;

	/* If check is good, copy wwpn wwnn into ndlp */
	memcpy(&ndlp->nlp_nodename, &sp->nodeName, sizeof (struct lpfc_name));
	memcpy(&ndlp->nlp_portname, &sp->portName, sizeof (struct lpfc_name));
	return (1);
}

static void *
lpfc_check_elscmpl_iocb(struct lpfc_hba * phba,
		      struct lpfc_iocbq *cmdiocb,
		      struct lpfc_iocbq *rspiocb)
{
	struct lpfc_dmabuf *pcmd, *prsp;
	uint32_t *lp;
	void     *ptr;
	IOCB_t   *irsp;

	irsp = &rspiocb->iocb;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	/* For lpfc_els_abort, context2 could be zero'ed to delay
	 * freeing associated memory till after ABTS completes.
	 */
	if (pcmd) {
		prsp = (struct lpfc_dmabuf *) pcmd->list.next;
		lp = (uint32_t *) prsp->virt;

		pci_dma_sync_single_for_cpu(phba->pcidev, prsp->phys,
			LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

		ptr = (void *)((uint8_t *)lp + sizeof(uint32_t));
	}
	else {
		/* Force ulpStatus error since we are returning NULL ptr */
		if (!(irsp->ulpStatus)) {
			irsp->ulpStatus = IOSTAT_LOCAL_REJECT;
			irsp->un.ulpWord[4] = IOERR_SLI_ABORTED;
		}
		ptr = NULL;
	}
	return (ptr);
}


/*
 * Free resources / clean up outstanding I/Os
 * associated with a LPFC_NODELIST entry. This
 * routine effectively results in a "software abort".
 */
static int
lpfc_els_abort(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp,
	int send_abts)
{
	struct lpfc_sli *psli;
	struct lpfc_sli_ring *pring;
	struct lpfc_iocbq *iocb, *next_iocb;
	IOCB_t *icmd;

	/* Abort outstanding I/O on NPort <nlp_DID> */
	lpfc_printf_log(phba, KERN_INFO, LOG_DISCOVERY,
			"%d:0201 Abort outstanding I/O on NPort x%x "
			"Data: x%x x%x x%x\n",
			phba->brd_no, ndlp->nlp_DID, ndlp->nlp_flag,
			ndlp->nlp_state, ndlp->nlp_rpi);

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];

	/* First check the txq */
	list_for_each_entry_safe(iocb, next_iocb, &pring->txq, list) {
		/* Check to see if iocb matches the nport we are looking for */
		if ((lpfc_check_sli_ndlp(phba, pring, iocb, ndlp))) {
			/* It matches, so deque and call compl with an error */
			list_del(&iocb->list);
			pring->txq_cnt--;
			if (iocb->iocb_cmpl) {
				icmd = &iocb->iocb;
				icmd->ulpStatus = IOSTAT_LOCAL_REJECT;
				icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
				(iocb->iocb_cmpl) (phba, iocb, iocb);
			} else {
				mempool_free(iocb, phba->iocb_mem_pool);
			}
		}
	}

	/* Everything on txcmplq will be returned by firmware
 	* with a no rpi / linkdown / abort error.  For ring 0,
	* ELS discovery, we want to get rid of it right here.
 	*/
	/* Next check the txcmplq */
	list_for_each_entry_safe(iocb, next_iocb, &pring->txcmplq, list) {
		/* Check to see if iocb matches the nport we are looking for */
		if ((lpfc_check_sli_ndlp (phba, pring, iocb, ndlp))) {
			/* It matches, so deque and call compl with an error */
			list_del(&iocb->list);
			pring->txcmplq_cnt--;

			icmd = &iocb->iocb;
			/* If the driver is completing an ELS
			 * command early, flush it out of the firmware.
			 */
			if (send_abts &&
			   (icmd->ulpCommand == CMD_ELS_REQUEST64_CR) &&
			   (icmd->un.elsreq64.bdl.ulpIoTag32)) {
				lpfc_sli_issue_abort_iotag32(phba, pring, iocb);
			}
			if (iocb->iocb_cmpl) {
				icmd->ulpStatus = IOSTAT_LOCAL_REJECT;
				icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
				(iocb->iocb_cmpl) (phba, iocb, iocb);
			} else {
				mempool_free(iocb, phba->iocb_mem_pool);
			}
		}
	}

	/* If we are delaying issuing an ELS command, cancel it */
	if(ndlp->nlp_flag & NLP_DELAY_TMO) {
		ndlp->nlp_flag &= ~NLP_DELAY_TMO;
		del_timer_sync(&ndlp->nlp_delayfunc);
	}
	return (0);
}

static int
lpfc_rcv_plogi(struct lpfc_hba * phba,
		      struct lpfc_nodelist * ndlp,
		      struct lpfc_iocbq *cmdiocb)
{
	struct lpfc_dmabuf *pcmd;
	uint32_t *lp;
	IOCB_t *icmd;
	struct serv_parm *sp;
	LPFC_MBOXQ_t *mbox;
	struct ls_rjt stat;

	memset(&stat, 0, sizeof (struct ls_rjt));
	if (phba->hba_state <= LPFC_FLOGI) {
		/* Before responding to PLOGI, check for pt2pt mode.
		 * If we are pt2pt, with an outstanding FLOGI, abort
		 * the FLOGI and resend it first.
		 */
		if (phba->fc_flag & FC_PT2PT) {
			lpfc_els_abort_flogi(phba);
		        if(!(phba->fc_flag & FC_PT2PT_PLOGI)) {
				/* If the other side is supposed to initiate
				 * the PLOGI anyway, just ACC it now and
				 * move on with discovery.
				 */
				phba->fc_edtov = FF_DEF_EDTOV;
				phba->fc_ratov = FF_DEF_RATOV;
				/* Start discovery - this should just do
				   CLEAR_LA */
				lpfc_disc_start(phba);
			}
			else {
				lpfc_initial_flogi(phba);
			}
		}
		else {
			stat.un.b.lsRjtRsnCode = LSRJT_LOGICAL_BSY;
			stat.un.b.lsRjtRsnCodeExp = LSEXP_NOTHING_MORE;
			goto out;
		}
	}
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	lp = (uint32_t *) pcmd->virt;
	sp = (struct serv_parm *) ((uint8_t *) lp + sizeof (uint32_t));
	if ((lpfc_check_sparm(phba, ndlp, sp, CLASS3) == 0)) {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		goto out;
	}
	icmd = &cmdiocb->iocb;

	/* PLOGI chkparm OK */
	lpfc_printf_log(phba,
			KERN_INFO,
			LOG_ELS,
			"%d:0114 PLOGI chkparm OK Data: x%x x%x x%x x%x\n",
			phba->brd_no,
			ndlp->nlp_DID, ndlp->nlp_state, ndlp->nlp_flag,
			ndlp->nlp_rpi);

	if ((phba->cfg_fcp_class == 2) &&
	    (sp->cls2.classValid)) {
		ndlp->nlp_fcp_info |= CLASS2;
	} else {
		ndlp->nlp_fcp_info |= CLASS3;
	}

	/* no need to reg_login if we are already in one of these states */
	switch(ndlp->nlp_state) {
	case  NLP_STE_REG_LOGIN_ISSUE:
	case  NLP_STE_PRLI_ISSUE:
	case  NLP_STE_UNMAPPED_NODE:
	case  NLP_STE_MAPPED_NODE:
		lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp, NULL, 0);
		return (1);
	}

	if ((mbox = mempool_alloc(phba->mbox_mem_pool, GFP_ATOMIC)) == 0) {
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_OUT_OF_RESOURCE;
		goto out;
	}

	if ((phba->fc_flag & FC_PT2PT)
	    && !(phba->fc_flag & FC_PT2PT_PLOGI)) {
		/* rcv'ed PLOGI decides what our NPortId will be */
		phba->fc_myDID = icmd->un.rcvels.parmRo;
		lpfc_config_link(phba, mbox);
		if (lpfc_sli_issue_mbox
		    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			mempool_free( mbox, phba->mbox_mem_pool);
			stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
			stat.un.b.lsRjtRsnCodeExp = LSEXP_OUT_OF_RESOURCE;
			goto out;
		}
		if ((mbox = mempool_alloc(phba->mbox_mem_pool,
			GFP_ATOMIC)) == 0) {
			stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
			stat.un.b.lsRjtRsnCodeExp = LSEXP_OUT_OF_RESOURCE;
			goto out;
		}
		lpfc_can_disctmo(phba);
	}

	if(lpfc_reg_login(phba, icmd->un.rcvels.remoteID,
			   (uint8_t *) sp, mbox, 0)) {
		mempool_free( mbox, phba->mbox_mem_pool);
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_OUT_OF_RESOURCE;
out:
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
		return (0);
	}

	/* ACC PLOGI rsp command needs to execute first,
	 * queue this mbox command to be processed later.
	 */
	mbox->mbox_cmpl = lpfc_mbx_cmpl_reg_login;
	mbox->context2  = ndlp;
	ndlp->nlp_flag |= NLP_ACC_REGLOGIN;

	/* If there is an outstanding PLOGI issued, abort it before
	 * sending ACC rsp to PLOGI recieved.
	 */
	if(ndlp->nlp_state == NLP_STE_PLOGI_ISSUE) {
		/* software abort outstanding PLOGI */
		lpfc_els_abort(phba, ndlp, 1);
	}
	ndlp->nlp_flag |= NLP_RCV_PLOGI;
	lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp, mbox, 0);
	return (1);
}

static int
lpfc_rcv_padisc(struct lpfc_hba * phba,
		struct lpfc_nodelist * ndlp,
		struct lpfc_iocbq *cmdiocb)
{
	struct lpfc_dmabuf *pcmd;
	struct serv_parm *sp;
	struct lpfc_name *pnn, *ppn;
	struct ls_rjt stat;
	ADISC *ap;
	IOCB_t *icmd;
	uint32_t *lp;
	uint32_t cmd;

	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	lp = (uint32_t *) pcmd->virt;

	cmd = *lp++;
	if (cmd == ELS_CMD_ADISC) {
		ap = (ADISC *) lp;
		pnn = (struct lpfc_name *) & ap->nodeName;
		ppn = (struct lpfc_name *) & ap->portName;
	} else {
		sp = (struct serv_parm *) lp;
		pnn = (struct lpfc_name *) & sp->nodeName;
		ppn = (struct lpfc_name *) & sp->portName;
	}

	icmd = &cmdiocb->iocb;
	if ((icmd->ulpStatus == 0) &&
	    (lpfc_check_adisc(phba, ndlp, pnn, ppn))) {
		if (cmd == ELS_CMD_ADISC) {
			lpfc_els_rsp_adisc_acc(phba, cmdiocb, ndlp);
		}
		else {
			lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp,
				NULL, 0);
		}
		return (1);
	}
	/* Reject this request because invalid parameters */
	stat.un.b.lsRjtRsvd0 = 0;
	stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
	stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
	stat.un.b.vendorUnique = 0;
	lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);

	ndlp->nlp_last_elscmd = (unsigned long)ELS_CMD_PLOGI;
	/* 1 sec timeout */
	mod_timer(&ndlp->nlp_delayfunc, jiffies + HZ);

	ndlp->nlp_flag |= NLP_DELAY_TMO;
	ndlp->nlp_state = NLP_STE_NPR_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
	return (0);
}

static int
lpfc_rcv_logo(struct lpfc_hba * phba,
		      struct lpfc_nodelist * ndlp,
		      struct lpfc_iocbq *cmdiocb)
{
	/* Put ndlp on NPR list with 1 sec timeout for plogi, ACC logo */
	/* Only call LOGO ACC for first LOGO, this avoids sending unnecessary
	 * PLOGIs during LOGO storms from a device.
	 */
	ndlp->nlp_flag |= NLP_LOGO_ACC;
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, NULL, 0);

	ndlp->nlp_last_elscmd = (unsigned long)ELS_CMD_PLOGI;
	mod_timer(&ndlp->nlp_delayfunc, jiffies + HZ * 1);

	ndlp->nlp_flag |= NLP_DELAY_TMO;
	ndlp->nlp_state = NLP_STE_NPR_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);

	/* The driver has to wait until the ACC completes before it continues
	 * processing the LOGO.  The action will resume in
	 * lpfc_cmpl_els_logo_acc routine. Since part of processing includes an
	 * unreg_login, the driver waits so the ACC does not get aborted.
	 */
	return (0);
}

static int
lpfc_binding_found(struct lpfc_bindlist * blp, struct lpfc_nodelist * ndlp)
{
	uint16_t bindtype = blp->nlp_bind_type;

	if ((bindtype & FCP_SEED_DID) &&
		   (ndlp->nlp_DID == be32_to_cpu(blp->nlp_DID))) {
		return (1);
	} else if ((bindtype & FCP_SEED_WWPN) &&
		   (memcmp(&ndlp->nlp_portname, &blp->nlp_portname,
			   sizeof (struct lpfc_name)) == 0)) {
		return (1);
	} else if ((bindtype & FCP_SEED_WWNN) &&
		   (memcmp(&ndlp->nlp_nodename, &blp->nlp_nodename,
			    sizeof (struct lpfc_name)) == 0)) {
		return (1);
	}
	return (0);
}

static int
lpfc_binding_useid(struct lpfc_hba * phba, uint32_t sid)
{
	struct lpfc_bindlist *blp;

	list_for_each_entry(blp, &phba->fc_nlpbind_list, nlp_listp) {
		if (blp->nlp_sid == sid) {
			return (1);
		}
	}
	return (0);
}

static int
lpfc_mapping_useid(struct lpfc_hba * phba, uint32_t sid)
{
	struct lpfc_nodelist *mapnode;
	struct lpfc_bindlist *blp;

	list_for_each_entry(mapnode, &phba->fc_nlpmap_list, nlp_listp) {
		blp = mapnode->nlp_listp_bind;
		if (blp->nlp_sid == sid) {
			return (1);
		}
	}
	return (0);
}

static struct lpfc_bindlist *
lpfc_create_binding(struct lpfc_hba * phba,
		    struct lpfc_nodelist * ndlp, uint16_t index,
		    uint16_t bindtype)
{
	struct lpfc_bindlist *blp;

	if ((blp = mempool_alloc(phba->bind_mem_pool, GFP_ATOMIC))) {
		memset(blp, 0, sizeof (struct lpfc_bindlist));
		switch (bindtype) {
		case FCP_SEED_WWPN:
			blp->nlp_bind_type = FCP_SEED_WWPN;
			break;
		case FCP_SEED_WWNN:
			blp->nlp_bind_type = FCP_SEED_WWNN;
			break;
		case FCP_SEED_DID:
			blp->nlp_bind_type = FCP_SEED_DID;
			break;
		}
		blp->nlp_sid = index;
		blp->nlp_DID = ndlp->nlp_DID;
		memcpy(&blp->nlp_nodename, &ndlp->nlp_nodename,
		       sizeof (struct lpfc_name));
		memcpy(&blp->nlp_portname, &ndlp->nlp_portname,
		       sizeof (struct lpfc_name));

		return (blp);
	}
	return NULL;
}


static struct lpfc_bindlist *
lpfc_consistent_bind_get(struct lpfc_hba * phba, struct lpfc_nodelist * ndlp)
{
	struct lpfc_bindlist *blp, *next_blp;
	uint16_t index;

	/* check binding list */
	list_for_each_entry_safe(blp, next_blp, &phba->fc_nlpbind_list,
				nlp_listp) {
		if (lpfc_binding_found(blp, ndlp)) {

			/* take it off the binding list */
			phba->fc_bind_cnt--;
			list_del_init(&blp->nlp_listp);

			/* Reassign scsi id <sid> to NPort <nlp_DID> */
			lpfc_printf_log(phba,
					KERN_INFO,
					LOG_DISCOVERY | LOG_FCP,
					"%d:0213 Reassign scsi id x%x to "
					"NPort x%x Data: x%x x%x x%x x%x\n",
					phba->brd_no,
					blp->nlp_sid, ndlp->nlp_DID,
					blp->nlp_bind_type, ndlp->nlp_flag,
					ndlp->nlp_state, ndlp->nlp_rpi);

			return (blp);
		}
	}

	/* NOTE: if scan-down = 2 and we have private loop, then we use
	 * AlpaArray to determine sid.
	 */
	if ((phba->cfg_fcp_bind_method == 4) &&
	    ((phba->fc_flag & (FC_PUBLIC_LOOP | FC_FABRIC)) ||
	     (phba->fc_topology != TOPOLOGY_LOOP))) {
		/* Log message: ALPA based binding used on a non loop
		   topology */
		lpfc_printf_log(phba,
				KERN_WARNING,
				LOG_DISCOVERY,
				"%d:0245 ALPA based bind method used on an HBA "
				"which is in a nonloop topology Data: x%x\n",
				phba->brd_no,
				phba->fc_topology);
	}

	if ((phba->cfg_fcp_bind_method == 4) &&
	    !(phba->fc_flag & (FC_PUBLIC_LOOP | FC_FABRIC)) &&
	    (phba->fc_topology == TOPOLOGY_LOOP)) {
		for (index = 0; index < FC_MAXLOOP; index++) {
			if (ndlp->nlp_DID == (uint32_t) lpfcAlpaArray[index]) {
				if ((blp =
				     lpfc_create_binding(phba, ndlp, index,
							 FCP_SEED_DID))) {
					return (blp);
				}
				goto errid;
			}
		}
	}

	if (phba->cfg_automap) {
		while (1) {
			if ((lpfc_binding_useid(phba, phba->sid_cnt))
			     || (lpfc_mapping_useid (phba, phba->sid_cnt))) {

				phba->sid_cnt++;
			} else {
				if ((blp =
				     lpfc_create_binding(phba, ndlp,
							 phba->sid_cnt,
							 phba->fcp_mapping))) {
					blp->nlp_bind_type |= FCP_SEED_AUTO;

					phba->sid_cnt++;
					return (blp);
				}
				goto errid;
			}
		}
	}
	/* if automap on */
errid:
	/* Cannot assign scsi id on NPort <nlp_DID> */
	lpfc_printf_log(phba,
			KERN_INFO,
			LOG_DISCOVERY | LOG_FCP,
			"%d:0230 Cannot assign scsi ID on NPort x%x "
			"Data: x%x x%x x%x\n",
			phba->brd_no,
			ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state,
			ndlp->nlp_rpi);

	return NULL;
}

static uint32_t
lpfc_assign_binding(struct lpfc_hba * phba,
		struct lpfc_nodelist * ndlp, struct lpfc_bindlist *blp)
{
	struct lpfc_target *targetp;

	targetp = lpfc_find_target(phba, blp->nlp_sid, ndlp);
	if(!targetp) {
		/* Cannot assign scsi id <sid> to NPort <nlp_DID> */
		lpfc_printf_log(phba,
			KERN_INFO,
			LOG_DISCOVERY | LOG_FCP,
			"%d:0229 Cannot assign scsi id x%x to NPort x%x "
			"Data: x%x x%x x%x\n",
			phba->brd_no, blp->nlp_sid,
			ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state,
			ndlp->nlp_rpi);
		return(0);
	}
	ndlp->nlp_sid = blp->nlp_sid;
	ndlp->nlp_flag &= ~NLP_SEED_MASK;
	switch ((blp->nlp_bind_type & FCP_SEED_MASK)) {
	case FCP_SEED_WWPN:
		ndlp->nlp_flag |= NLP_SEED_WWPN;
		break;
	case FCP_SEED_WWNN:
		ndlp->nlp_flag |= NLP_SEED_WWNN;
		break;
	case FCP_SEED_DID:
		ndlp->nlp_flag |= NLP_SEED_DID;
		break;
	}
	if (blp->nlp_bind_type & FCP_SEED_AUTO) {
		ndlp->nlp_flag |= NLP_AUTOMAP;
	}
	/* Assign scsi id <sid> to NPort <nlp_DID> */
	lpfc_printf_log(phba,
		KERN_INFO,
		LOG_DISCOVERY | LOG_FCP,
		"%d:0216 Assign scsi "
		"id x%x to NPort x%x "
		"Data: x%x x%x x%x x%x\n",
		phba->brd_no,
		ndlp->nlp_sid, ndlp->nlp_DID,
		blp->nlp_bind_type,
		ndlp->nlp_flag, ndlp->nlp_state,
		ndlp->nlp_rpi);
	return(1);
}

static uint32_t
lpfc_disc_set_adisc(struct lpfc_hba * phba,
		      struct lpfc_nodelist * ndlp)
{
	/* Check config parameter use-adisc or FCP-2 */
	if ((phba->cfg_use_adisc == 0) &&
		!(phba->fc_flag & FC_RSCN_MODE)) {
		return (0);
	}
	ndlp->nlp_flag |= NLP_NPR_ADISC;
	return (1);
}

static uint32_t
lpfc_disc_nodev(struct lpfc_hba * phba,
		struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	/* This routine does nothing, just return the current state */
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_disc_neverdev(struct lpfc_hba * phba,
		   struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	lpfc_printf_log(phba,
			KERN_ERR,
			LOG_DISCOVERY,
			"%d:0253 Freeing node x%x due to invalid event x%x, "
			"state x%x Data: x%x x%x\n",
			phba->brd_no,
			ndlp->nlp_DID, evt, ndlp->nlp_state, ndlp->nlp_rpi,
			ndlp->nlp_flag);
	lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	return (NLP_STE_FREED_NODE);
}


/* Start of Discovery State Machine routines */

static uint32_t
lpfc_rcv_plogi_unused_node(struct lpfc_hba * phba,
			   struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	if(lpfc_rcv_plogi(phba, ndlp, cmdiocb)) {
		ndlp->nlp_state = NLP_STE_UNUSED_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_UNUSED_LIST);
		return (ndlp->nlp_state);
	}
	lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	return (NLP_STE_FREED_NODE);
}

static uint32_t
lpfc_rcv_els_unused_node(struct lpfc_hba * phba,
			 struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	lpfc_issue_els_logo(phba, ndlp, 0);
	lpfc_nlp_list(phba, ndlp, NLP_UNUSED_LIST);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_logo_unused_node(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq     *cmdiocb;
	struct lpfc_dmabuf    *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	ndlp->nlp_flag |= NLP_LOGO_ACC;
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, NULL, 0);
	lpfc_nlp_list(phba, ndlp, NLP_UNUSED_LIST);

	return (ndlp->nlp_state);
}

static uint32_t
lpfc_cmpl_logo_unused_node(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	return (NLP_STE_FREED_NODE);
}

static uint32_t
lpfc_device_rm_unused_node(struct lpfc_hba * phba,
			   struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	return (NLP_STE_FREED_NODE);
}

static uint32_t
lpfc_rcv_plogi_plogi_issue(struct lpfc_hba * phba,
			   struct lpfc_nodelist * ndlp,
			   struct lpfc_iocbq *cmdiocb, uint32_t evt)
{
	struct lpfc_dmabuf *pcmd;
	struct serv_parm *sp;
	uint32_t *lp;
	struct ls_rjt stat;
	int port_cmp;

	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;
	lp = (uint32_t *) pcmd->virt;
	sp = (struct serv_parm *) ((uint8_t *) lp + sizeof (uint32_t));

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	memset(&stat, 0, sizeof (struct ls_rjt));

	/* For a PLOGI, we only accept if our portname is less
	 * than the remote portname.
	 */
	phba->fc_stat.elsLogiCol++;
	port_cmp = memcmp(&phba->fc_portname, &sp->portName,
			  sizeof (struct lpfc_name));

	if (port_cmp >= 0) {
		/* Reject this request because the remote node will accept
		   ours */
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_CMD_IN_PROGRESS;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	}
	else {
		lpfc_rcv_plogi(phba, ndlp, cmdiocb);
	} /* if our portname was less */

	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_els_plogi_issue(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq     *cmdiocb;
	struct lpfc_dmabuf    *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	/* software abort outstanding PLOGI */
	lpfc_els_abort(phba, ndlp, 1);
	mod_timer(&ndlp->nlp_delayfunc, jiffies + HZ * 1);
	ndlp->nlp_flag |= NLP_DELAY_TMO;

	if(evt == NLP_EVT_RCV_LOGO) {
		lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, NULL, 0);
	}
	else {
		lpfc_issue_els_logo(phba, ndlp, 0);
	}

	/* Put ndlp in npr list set plogi timer for 1 sec */
	ndlp->nlp_last_elscmd = (unsigned long)ELS_CMD_PLOGI;
	ndlp->nlp_state = NLP_STE_NPR_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);

	return (ndlp->nlp_state);
}

static uint32_t
lpfc_cmpl_plogi_plogi_issue(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb, *rspiocb;
	struct lpfc_dmabuf *pcmd, *prsp;
	uint32_t *lp;
	IOCB_t *irsp;
	struct serv_parm *sp;
	LPFC_MBOXQ_t *mbox;

	cmdiocb = (struct lpfc_iocbq *) arg;
	rspiocb = cmdiocb->context_un.rsp_iocb;

	if (ndlp->nlp_flag & NLP_ACC_REGLOGIN) {
		return (ndlp->nlp_state);
	}

	irsp = &rspiocb->iocb;

	if (irsp->ulpStatus == 0) {
		pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

		prsp = (struct lpfc_dmabuf *) pcmd->list.next;
		lp = (uint32_t *) prsp->virt;

		pci_dma_sync_single_for_cpu(phba->pcidev, prsp->phys,
			LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

		sp = (struct serv_parm *) ((uint8_t *) lp + sizeof (uint32_t));
		if ((lpfc_check_sparm(phba, ndlp, sp, CLASS3))) {
			/* PLOGI chkparm OK */
			lpfc_printf_log(phba,
					KERN_INFO,
					LOG_ELS,
					"%d:0121 PLOGI chkparm OK "
					"Data: x%x x%x x%x x%x\n",
					phba->brd_no,
					ndlp->nlp_DID, ndlp->nlp_state,
					ndlp->nlp_flag, ndlp->nlp_rpi);

			if ((phba->cfg_fcp_class == 2) &&
			    (sp->cls2.classValid)) {
				ndlp->nlp_fcp_info |= CLASS2;
			} else {
				ndlp->nlp_fcp_info |= CLASS3;
			}

			if ((mbox = mempool_alloc(phba->mbox_mem_pool,
						  GFP_ATOMIC))) {
				lpfc_unreg_rpi(phba, ndlp);
				if (lpfc_reg_login
				    (phba, irsp->un.elsreq64.remoteID,
				     (uint8_t *) sp, mbox, 0) == 0) {
					/* set_slim mailbox command needs to
					 * execute first, queue this command to
					 * be processed later.
					 */
					mbox->mbox_cmpl =
					    lpfc_mbx_cmpl_reg_login;
					mbox->context2 = ndlp;
					if (lpfc_sli_issue_mbox(phba, mbox,
					     (MBX_NOWAIT | MBX_STOP_IOCB))
					    != MBX_NOT_FINISHED) {
						ndlp->nlp_state =
					    	NLP_STE_REG_LOGIN_ISSUE;
						lpfc_nlp_list(phba, ndlp,
						      NLP_REGLOGIN_LIST);
						return (ndlp->nlp_state);
					}
					mempool_free(mbox, phba->mbox_mem_pool);
				} else {
					mempool_free(mbox, phba->mbox_mem_pool);
				}
			}
		}
	}

	/* Free this node since the driver cannot login or has the wrong
	   sparm */
	lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	return (NLP_STE_FREED_NODE);
}

static uint32_t
lpfc_device_rm_plogi_issue(struct lpfc_hba * phba,
			   struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	/* software abort outstanding PLOGI */
	lpfc_els_abort(phba, ndlp, 1);

	lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	return (NLP_STE_FREED_NODE);
}

static uint32_t
lpfc_device_recov_plogi_issue(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	/* software abort outstanding PLOGI */
	lpfc_els_abort(phba, ndlp, 1);

	ndlp->nlp_state = NLP_STE_NPR_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
	ndlp->nlp_flag &= ~NLP_NPR_2B_DISC;

	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_plogi_adisc_issue(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	/* software abort outstanding ADISC */
	lpfc_els_abort(phba, ndlp, 1);

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	if(lpfc_rcv_plogi(phba, ndlp, cmdiocb)) {
		return (ndlp->nlp_state);
	}
	ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
	lpfc_nlp_list(phba, ndlp, NLP_PLOGI_LIST);
	lpfc_issue_els_plogi(phba, ndlp, 0);

	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_prli_adisc_issue(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	lpfc_els_rsp_prli_acc(phba, cmdiocb, ndlp);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_logo_adisc_issue(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	/* software abort outstanding ADISC */
	lpfc_els_abort(phba, ndlp, 0);

	lpfc_rcv_logo(phba, ndlp, cmdiocb);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_padisc_adisc_issue(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	lpfc_rcv_padisc(phba, ndlp, cmdiocb);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_prlo_adisc_issue(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	/* Treat like rcv logo */
	lpfc_rcv_logo(phba, ndlp, cmdiocb);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_cmpl_adisc_adisc_issue(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb, *rspiocb;
	struct lpfc_bindlist *blp;
	IOCB_t *irsp;
	ADISC *ap;

	cmdiocb = (struct lpfc_iocbq *) arg;
	rspiocb = cmdiocb->context_un.rsp_iocb;

	ap = (ADISC *)lpfc_check_elscmpl_iocb(phba, cmdiocb, rspiocb);
	irsp = &rspiocb->iocb;

	if ((irsp->ulpStatus) ||
		(!lpfc_check_adisc(phba, ndlp, &ap->nodeName, &ap->portName))) {
		ndlp->nlp_last_elscmd = (unsigned long)ELS_CMD_PLOGI;
		/* 1 sec timeout */
		mod_timer(&ndlp->nlp_delayfunc, jiffies + HZ);
		ndlp->nlp_flag |= NLP_DELAY_TMO;

		memset(&ndlp->nlp_nodename, 0, sizeof (struct lpfc_name));
		memset(&ndlp->nlp_portname, 0, sizeof (struct lpfc_name));

		ndlp->nlp_state = NLP_STE_NPR_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
		lpfc_unreg_rpi(phba, ndlp);
		return (ndlp->nlp_state);
	}
   	/* move to mapped / unmapped list accordingly */
	/* Can we assign a SCSI Id to this NPort */
	if ((blp = lpfc_consistent_bind_get(phba, ndlp))) {
		/* Next 4 lines MUST be in this order */
		if(lpfc_assign_binding(phba, ndlp, blp)) {
			lpfc_nlp_list(phba, ndlp, NLP_MAPPED_LIST);
			ndlp->nlp_listp_bind = blp;
			ndlp->nlp_state = NLP_STE_MAPPED_NODE;

			lpfc_set_failmask(phba, ndlp,
				(LPFC_DEV_DISCOVERY_INP|LPFC_DEV_DISCONNECTED),
				LPFC_CLR_BITMASK);

			return (ndlp->nlp_state);
		}
	}
	ndlp->nlp_flag |= NLP_TGT_NO_SCSIID;
	lpfc_nlp_list(phba, ndlp, NLP_UNMAPPED_LIST);
	ndlp->nlp_state = NLP_STE_UNMAPPED_NODE;

	lpfc_set_failmask(phba, ndlp,
		(LPFC_DEV_DISCOVERY_INP | LPFC_DEV_DISCONNECTED),
		LPFC_CLR_BITMASK);

	return (ndlp->nlp_state);
}

static uint32_t
lpfc_device_rm_adisc_issue(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	/* software abort outstanding ADISC */
	lpfc_els_abort(phba, ndlp, 1);

	lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	return (NLP_STE_FREED_NODE);
}

static uint32_t
lpfc_device_recov_adisc_issue(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	/* software abort outstanding ADISC */
	lpfc_els_abort(phba, ndlp, 1);

	ndlp->nlp_state = NLP_STE_NPR_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
	ndlp->nlp_flag &= ~NLP_NPR_2B_DISC;

	lpfc_disc_set_adisc(phba, ndlp);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_plogi_reglogin_issue(struct lpfc_hba * phba,
			      struct lpfc_nodelist * ndlp, void *arg,
			      uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	lpfc_rcv_plogi(phba, ndlp, cmdiocb);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_prli_reglogin_issue(struct lpfc_hba * phba,
			     struct lpfc_nodelist * ndlp, void *arg,
			     uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	lpfc_els_rsp_prli_acc(phba, cmdiocb, ndlp);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_logo_reglogin_issue(struct lpfc_hba * phba,
			     struct lpfc_nodelist * ndlp, void *arg,
			     uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	lpfc_rcv_logo(phba, ndlp, cmdiocb);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_padisc_reglogin_issue(struct lpfc_hba * phba,
			       struct lpfc_nodelist * ndlp, void *arg,
			       uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	lpfc_rcv_padisc(phba, ndlp, cmdiocb);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_prlo_reglogin_issue(struct lpfc_hba * phba,
			     struct lpfc_nodelist * ndlp, void *arg,
			     uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, NULL, 0);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_cmpl_reglogin_reglogin_issue(struct lpfc_hba * phba,
				  struct lpfc_nodelist * ndlp,
				  void *arg, uint32_t evt)
{
	LPFC_MBOXQ_t *pmb;
	MAILBOX_t *mb;
	uint32_t did;

	pmb = (LPFC_MBOXQ_t *) arg;
	mb = &pmb->mb;
	did = mb->un.varWords[1];
	if (mb->mbxStatus) {
		/* RegLogin failed */
		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_DISCOVERY,
				"%d:0246 RegLogin failed Data: x%x x%x x%x\n",
				phba->brd_no,
				did, mb->mbxStatus, phba->hba_state);

		mod_timer(&ndlp->nlp_delayfunc, jiffies + HZ * 1);
		ndlp->nlp_flag |= NLP_DELAY_TMO;

		lpfc_issue_els_logo(phba, ndlp, 0);
		/* Put ndlp in npr list set plogi timer for 1 sec */
		ndlp->nlp_last_elscmd = (unsigned long)ELS_CMD_PLOGI;
		ndlp->nlp_state = NLP_STE_NPR_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
		return (ndlp->nlp_state);
	}

	if (ndlp->nlp_rpi != 0)
		lpfc_findnode_remove_rpi(phba, ndlp->nlp_rpi);

	ndlp->nlp_rpi = mb->un.varWords[0];
	lpfc_addnode_rpi(phba, ndlp, ndlp->nlp_rpi);

	/* Only if we are not a fabric nport do we issue PRLI */
	if (!(ndlp->nlp_type & NLP_FABRIC)) {
		ndlp->nlp_state = NLP_STE_PRLI_ISSUE;
		lpfc_nlp_list(phba, ndlp, NLP_PRLI_LIST);
		lpfc_issue_els_prli(phba, ndlp, 0);
	} else {
		ndlp->nlp_state = NLP_STE_UNMAPPED_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_UNMAPPED_LIST);
	}
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_device_rm_reglogin_issue(struct lpfc_hba * phba,
			      struct lpfc_nodelist * ndlp, void *arg,
			      uint32_t evt)
{
	lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	return (NLP_STE_FREED_NODE);
}

static uint32_t
lpfc_device_recov_reglogin_issue(struct lpfc_hba * phba,
			       struct lpfc_nodelist * ndlp, void *arg,
			       uint32_t evt)
{
	ndlp->nlp_state = NLP_STE_NPR_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
	ndlp->nlp_flag &= ~NLP_NPR_2B_DISC;
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_plogi_prli_issue(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	lpfc_rcv_plogi(phba, ndlp, cmdiocb);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_prli_prli_issue(struct lpfc_hba * phba,
			 struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	lpfc_els_rsp_prli_acc(phba, cmdiocb, ndlp);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_logo_prli_issue(struct lpfc_hba * phba,
			 struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	/* Software abort outstanding PRLI before sending acc */
	lpfc_els_abort(phba, ndlp, 1);

	lpfc_rcv_logo(phba, ndlp, cmdiocb);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_padisc_prli_issue(struct lpfc_hba * phba,
			   struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	lpfc_rcv_padisc(phba, ndlp, cmdiocb);
	return (ndlp->nlp_state);
}

/* This routine is envoked when we rcv a PRLO request from a nport
 * we are logged into.  We should send back a PRLO rsp setting the
 * appropriate bits.
 * NEXT STATE = PRLI_ISSUE
 */
static uint32_t
lpfc_rcv_prlo_prli_issue(struct lpfc_hba * phba,
			 struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;

	cmdiocb = (struct lpfc_iocbq *) arg;
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, NULL, 0);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_cmpl_prli_prli_issue(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb, *rspiocb;
	IOCB_t *irsp;
	PRLI *npr;
	struct lpfc_bindlist *blp;

	cmdiocb = (struct lpfc_iocbq *) arg;
	rspiocb = cmdiocb->context_un.rsp_iocb;
	npr = (PRLI *)lpfc_check_elscmpl_iocb(phba, cmdiocb, rspiocb);

	irsp = &rspiocb->iocb;
	if (irsp->ulpStatus) {
		ndlp->nlp_state = NLP_STE_UNMAPPED_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_UNMAPPED_LIST);
		lpfc_set_failmask(phba, ndlp, LPFC_DEV_DISCOVERY_INP,
				  LPFC_CLR_BITMASK);
		return (ndlp->nlp_state);
	}

	/* Check out PRLI rsp */
	if ((npr->acceptRspCode != PRLI_REQ_EXECUTED) ||
	    (npr->prliType != PRLI_FCP_TYPE) || (npr->targetFunc != 1)) {
		ndlp->nlp_state = NLP_STE_UNMAPPED_NODE;
		lpfc_nlp_list(phba, ndlp, NLP_UNMAPPED_LIST);
		lpfc_set_failmask(phba, ndlp,
			(LPFC_DEV_DISCOVERY_INP | LPFC_DEV_DISCONNECTED),
			LPFC_CLR_BITMASK);
		return (ndlp->nlp_state);
	}
	if (npr->Retry == 1) {
		ndlp->nlp_fcp_info |= NLP_FCP_2_DEVICE;
	}

	/* Can we assign a SCSI Id to this NPort */
	if ((blp = lpfc_consistent_bind_get(phba, ndlp))) {
		/* Next 4 lines MUST be in this order */
		if(lpfc_assign_binding(phba, ndlp, blp)) {
			lpfc_nlp_list(phba, ndlp, NLP_MAPPED_LIST);
			ndlp->nlp_listp_bind = blp;
			ndlp->nlp_state = NLP_STE_MAPPED_NODE;

			lpfc_set_failmask(phba, ndlp,
				(LPFC_DEV_DISCOVERY_INP|LPFC_DEV_DISCONNECTED),
				LPFC_CLR_BITMASK);
			return (ndlp->nlp_state);
		}
	}
	ndlp->nlp_flag |= NLP_TGT_NO_SCSIID;
	lpfc_nlp_list(phba, ndlp, NLP_UNMAPPED_LIST);
	ndlp->nlp_state = NLP_STE_UNMAPPED_NODE;

	lpfc_set_failmask(phba, ndlp,
		(LPFC_DEV_DISCOVERY_INP | LPFC_DEV_DISCONNECTED),
		LPFC_CLR_BITMASK);
	return (ndlp->nlp_state);
}

/*! lpfc_device_rm_prli_issue
  *
  * \pre
  * \post
  * \param   phba
  * \param   ndlp
  * \param   arg
  * \param   evt
  * \return  uint32_t
  *
  * \b Description:
  *    This routine is envoked when we a request to remove a nport we are in the
  *    process of PRLIing. We should software abort outstanding prli, unreg
  *    login, send a logout. We will change node state to UNUSED_NODE, put it
  *    on plogi list so it can be freed when LOGO completes.
  *
  */
static uint32_t
lpfc_device_rm_prli_issue(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	/* software abort outstanding PRLI */
	lpfc_els_abort(phba, ndlp, 1);

	lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	return (NLP_STE_FREED_NODE);
}


/*! lpfc_device_recov_prli_issue
  *
  * \pre
  * \post
  * \param   phba
  * \param   ndlp
  * \param   arg
  * \param   evt
  * \return  uint32_t
  *
  * \b Description:
  *    The routine is envoked when the state of a device is unknown, like
  *    during a link down. We should remove the nodelist entry from the
  *    unmapped list, issue a UNREG_LOGIN, do a software abort of the
  *    outstanding PRLI command, then free the node entry.
  */
static uint32_t
lpfc_device_recov_prli_issue(struct lpfc_hba * phba,
			   struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	/* software abort outstanding PRLI */
	lpfc_els_abort(phba, ndlp, 1);

	ndlp->nlp_state = NLP_STE_NPR_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
	ndlp->nlp_flag &= ~NLP_NPR_2B_DISC;
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_plogi_unmap_node(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	lpfc_rcv_plogi(phba, ndlp, cmdiocb);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_prli_unmap_node(struct lpfc_hba * phba,
			 struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	lpfc_els_rsp_prli_acc(phba, cmdiocb, ndlp);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_logo_unmap_node(struct lpfc_hba * phba,
			 struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	lpfc_rcv_logo(phba, ndlp, cmdiocb);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_padisc_unmap_node(struct lpfc_hba * phba,
			   struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	lpfc_rcv_padisc(phba, ndlp, cmdiocb);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_prlo_unmap_node(struct lpfc_hba * phba,
			 struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	/* Treat like rcv logo */
	lpfc_rcv_logo(phba, ndlp, cmdiocb);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_device_recov_unmap_node(struct lpfc_hba * phba,
			   struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	ndlp->nlp_state = NLP_STE_NPR_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
	ndlp->nlp_flag &= ~NLP_NPR_2B_DISC;
	lpfc_disc_set_adisc(phba, ndlp);

	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_plogi_mapped_node(struct lpfc_hba * phba,
			   struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	lpfc_rcv_plogi(phba, ndlp, cmdiocb);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_prli_mapped_node(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	lpfc_els_rsp_prli_acc(phba, cmdiocb, ndlp);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_logo_mapped_node(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	lpfc_rcv_logo(phba, ndlp, cmdiocb);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_padisc_mapped_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	lpfc_rcv_padisc(phba, ndlp, cmdiocb);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_prlo_mapped_node(struct lpfc_hba * phba,
			  struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	/* flush the target */
	lpfc_sli_abort_iocb_tgt(phba,
			       &phba->sli.ring[phba->sli.fcp_ring],
			       ndlp->nlp_sid);

	/* Treat like rcv logo */
	lpfc_rcv_logo(phba, ndlp, cmdiocb);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_device_recov_mapped_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	ndlp->nlp_state = NLP_STE_NPR_NODE;
	lpfc_nlp_list(phba, ndlp, NLP_NPR_LIST);
	ndlp->nlp_flag &= ~NLP_NPR_2B_DISC;
	lpfc_disc_set_adisc(phba, ndlp);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_plogi_npr_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq *cmdiocb;
	struct lpfc_dmabuf *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	/* Ignore PLOGI if we have an outstanding LOGO */
	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	if(lpfc_rcv_plogi(phba, ndlp, cmdiocb)) {
		ndlp->nlp_flag &= ~(NLP_NPR_ADISC | NLP_NPR_2B_DISC);
		return (ndlp->nlp_state);
	}

	/* send PLOGI immediately, move to PLOGI issue state */
	if(!(ndlp->nlp_flag & NLP_DELAY_TMO)) {
			ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
			lpfc_nlp_list(phba, ndlp, NLP_PLOGI_LIST);
			lpfc_issue_els_plogi(phba, ndlp, 0);
	}
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_prli_npr_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq     *cmdiocb;
	struct lpfc_dmabuf    *pcmd;
	struct ls_rjt          stat;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	memset(&stat, 0, sizeof (struct ls_rjt));
	stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
	stat.un.b.lsRjtRsnCodeExp = LSEXP_NOTHING_MORE;
	lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);

	if(!(ndlp->nlp_flag & NLP_DELAY_TMO)) {
		if (ndlp->nlp_flag & NLP_NPR_ADISC) {
			ndlp->nlp_state = NLP_STE_ADISC_ISSUE;
			lpfc_nlp_list(phba, ndlp, NLP_ADISC_LIST);
			lpfc_issue_els_adisc(phba, ndlp, 0);
		} else {
			ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
			lpfc_nlp_list(phba, ndlp, NLP_PLOGI_LIST);
			lpfc_issue_els_plogi(phba, ndlp, 0);
		}
	}
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_logo_npr_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq     *cmdiocb;
	struct lpfc_dmabuf         *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	ndlp->nlp_flag |= NLP_LOGO_ACC;
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, NULL, 0);

	if(ndlp->nlp_flag & NLP_DELAY_TMO) {
		if (ndlp->nlp_last_elscmd == (unsigned long)ELS_CMD_PLOGI) {
			return (ndlp->nlp_state);
		} else {
			del_timer_sync(&ndlp->nlp_delayfunc);
			ndlp->nlp_flag &= ~NLP_DELAY_TMO;
		}
	}

	ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
	lpfc_nlp_list(phba, ndlp, NLP_PLOGI_LIST);
	lpfc_issue_els_plogi(phba, ndlp, 0);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_padisc_npr_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq     *cmdiocb;
	struct lpfc_dmabuf    *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	lpfc_rcv_padisc(phba, ndlp, cmdiocb);

	if(!(ndlp->nlp_flag & NLP_DELAY_TMO)) {
		if (ndlp->nlp_flag & NLP_NPR_ADISC) {
			ndlp->nlp_state = NLP_STE_ADISC_ISSUE;
			lpfc_nlp_list(phba, ndlp, NLP_ADISC_LIST);
			lpfc_issue_els_adisc(phba, ndlp, 0);
		} else {
			ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
			lpfc_nlp_list(phba, ndlp, NLP_PLOGI_LIST);
			lpfc_issue_els_plogi(phba, ndlp, 0);
		}
	}
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_rcv_prlo_npr_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	struct lpfc_iocbq     *cmdiocb;
	struct lpfc_dmabuf         *pcmd;

	cmdiocb = (struct lpfc_iocbq *) arg;
	pcmd = (struct lpfc_dmabuf *) cmdiocb->context2;

	pci_dma_sync_single_for_cpu(phba->pcidev, pcmd->phys,
		LPFC_BPL_SIZE, PCI_DMA_FROMDEVICE);

	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, NULL, 0);

	if(ndlp->nlp_flag & NLP_DELAY_TMO) {
		if (ndlp->nlp_last_elscmd == (unsigned long)ELS_CMD_PLOGI) {
			return (ndlp->nlp_state);
		} else {
			del_timer_sync(&ndlp->nlp_delayfunc);
			ndlp->nlp_flag &= ~NLP_DELAY_TMO;
		}
	}

	ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
	lpfc_nlp_list(phba, ndlp, NLP_PLOGI_LIST);
	lpfc_issue_els_plogi(phba, ndlp, 0);
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_cmpl_logo_npr_node(struct lpfc_hba * phba,
		struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	lpfc_unreg_rpi(phba, ndlp);
	/* This routine does nothing, just return the current state */
	return (ndlp->nlp_state);
}

static uint32_t
lpfc_cmpl_reglogin_npr_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	LPFC_MBOXQ_t *pmb;
	MAILBOX_t *mb;

	pmb = (LPFC_MBOXQ_t *) arg;
	mb = &pmb->mb;

	/* save rpi */
	if (ndlp->nlp_rpi != 0)
		lpfc_findnode_remove_rpi(phba, ndlp->nlp_rpi);

	ndlp->nlp_rpi = mb->un.varWords[0];
	lpfc_addnode_rpi(phba, ndlp, ndlp->nlp_rpi);

	return (ndlp->nlp_state);
}

static uint32_t
lpfc_device_rm_npr_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	return (NLP_STE_FREED_NODE);
}

static uint32_t
lpfc_device_recov_npr_node(struct lpfc_hba * phba,
			    struct lpfc_nodelist * ndlp, void *arg,
			    uint32_t evt)
{
	ndlp->nlp_flag &= ~NLP_NPR_2B_DISC;
	return (ndlp->nlp_state);
}


/* This next section defines the NPort Discovery State Machine */

/* There are 4 different double linked lists nodelist entries can reside on.
 * The plogi list and adisc list are used when Link Up discovery or RSCN
 * processing is needed. Each list holds the nodes that we will send PLOGI
 * or ADISC on. These lists will keep track of what nodes will be effected
 * by an RSCN, or a Link Up (Typically, all nodes are effected on Link Up).
 * The unmapped_list will contain all nodes that we have successfully logged
 * into at the Fibre Channel level. The mapped_list will contain all nodes
 * that are mapped FCP targets.
 */
/*
 * The bind list is a list of undiscovered (potentially non-existent) nodes
 * that we have saved binding information on. This information is used when
 * nodes transition from the unmapped to the mapped list.
 */
/* For UNUSED_NODE state, the node has just been allocated .
 * For PLOGI_ISSUE and REG_LOGIN_ISSUE, the node is on
 * the PLOGI list. For REG_LOGIN_COMPL, the node is taken off the PLOGI list
 * and put on the unmapped list. For ADISC processing, the node is taken off
 * the ADISC list and placed on either the mapped or unmapped list (depending
 * on its previous state). Once on the unmapped list, a PRLI is issued and the
 * state changed to PRLI_ISSUE. When the PRLI completion occurs, the state is
 * changed to UNMAPPED_NODE. If the completion indicates a mapped
 * node, the node is taken off the unmapped list. The binding list is checked
 * for a valid binding, or a binding is automatically assigned. If binding
 * assignment is unsuccessful, the node is left on the unmapped list. If
 * binding assignment is successful, the associated binding list entry (if
 * any) is removed, and the node is placed on the mapped list.
 */
/*
 * For a Link Down, all nodes on the ADISC, PLOGI, unmapped or mapped
 * lists will receive a DEVICE_RECOVERY event. If the linkdown or nodev timers
 * expire, all effected nodes will receive a DEVICE_RM event.
 */
/*
 * For a Link Up or RSCN, all nodes will move from the mapped / unmapped lists
 * to either the ADISC or PLOGI list.  After a Nameserver query or ALPA loopmap
 * check, additional nodes may be added or removed (via DEVICE_RM) to / from
 * the PLOGI or ADISC lists. Once the PLOGI and ADISC lists are populated,
 * we will first process the ADISC list.  32 entries are processed initially and
 * ADISC is initited for each one.  Completions / Events for each node are
 * funnelled thru the state machine.  As each node finishes ADISC processing, it
 * starts ADISC for any nodes waiting for ADISC processing. If no nodes are
 * waiting, and the ADISC list count is identically 0, then we are done. For
 * Link Up discovery, since all nodes on the PLOGI list are UNREG_LOGIN'ed, we
 * can issue a CLEAR_LA and reenable Link Events. Next we will process the PLOGI
 * list.  32 entries are processed initially and PLOGI is initited for each one.
 * Completions / Events for each node are funnelled thru the state machine.  As
 * each node finishes PLOGI processing, it starts PLOGI for any nodes waiting
 * for PLOGI processing. If no nodes are waiting, and the PLOGI list count is
 * indentically 0, then we are done. We have now completed discovery / RSCN
 * handling. Upon completion, ALL nodes should be on either the mapped or
 * unmapped lists.
 */

static void *lpfc_disc_action[NLP_STE_MAX_STATE * NLP_EVT_MAX_EVENT] = {
	/* Action routine                          Event       Current State  */
	(void *)lpfc_rcv_plogi_unused_node,	/* RCV_PLOGI   UNUSED_NODE    */
	(void *)lpfc_rcv_els_unused_node,	/* RCV_PRLI        */
	(void *)lpfc_rcv_logo_unused_node,	/* RCV_LOGO        */
	(void *)lpfc_rcv_els_unused_node,	/* RCV_ADISC       */
	(void *)lpfc_rcv_els_unused_node,	/* RCV_PDISC       */
	(void *)lpfc_rcv_els_unused_node,	/* RCV_PRLO        */
	(void *)lpfc_disc_neverdev,		/* CMPL_PLOGI      */
	(void *)lpfc_disc_neverdev,		/* CMPL_PRLI       */
	(void *)lpfc_cmpl_logo_unused_node,	/* CMPL_LOGO       */
	(void *)lpfc_disc_neverdev,		/* CMPL_ADISC      */
	(void *)lpfc_disc_neverdev,		/* CMPL_REG_LOGIN  */
	(void *)lpfc_device_rm_unused_node,	/* DEVICE_RM       */
	(void *)lpfc_disc_neverdev,		/* DEVICE_RECOVERY */

	(void *)lpfc_rcv_plogi_plogi_issue,	/* RCV_PLOGI   PLOGI_ISSUE    */
	(void *)lpfc_rcv_els_plogi_issue,	/* RCV_PRLI        */
	(void *)lpfc_rcv_els_plogi_issue,	/* RCV_LOGO        */
	(void *)lpfc_rcv_els_plogi_issue,	/* RCV_ADISC       */
	(void *)lpfc_rcv_els_plogi_issue,	/* RCV_PDISC       */
	(void *)lpfc_rcv_els_plogi_issue,	/* RCV_PRLO        */
	(void *)lpfc_cmpl_plogi_plogi_issue,	/* CMPL_PLOGI      */
	(void *)lpfc_disc_neverdev,		/* CMPL_PRLI       */
	(void *)lpfc_disc_neverdev,		/* CMPL_LOGO       */
	(void *)lpfc_disc_neverdev,		/* CMPL_ADISC      */
	(void *)lpfc_disc_neverdev,		/* CMPL_REG_LOGIN  */
	(void *)lpfc_device_rm_plogi_issue,	/* DEVICE_RM       */
	(void *)lpfc_device_recov_plogi_issue,	/* DEVICE_RECOVERY */

	(void *)lpfc_rcv_plogi_adisc_issue,     /* RCV_PLOGI   ADISC_ISSUE    */
	(void *)lpfc_rcv_prli_adisc_issue,      /* RCV_PRLI        */
	(void *)lpfc_rcv_logo_adisc_issue,      /* RCV_LOGO        */
	(void *)lpfc_rcv_padisc_adisc_issue,    /* RCV_ADISC       */
	(void *)lpfc_rcv_padisc_adisc_issue,    /* RCV_PDISC       */
	(void *)lpfc_rcv_prlo_adisc_issue,      /* RCV_PRLO        */
	(void *)lpfc_disc_neverdev,    		/* CMPL_PLOGI      */
	(void *)lpfc_disc_neverdev,     	/* CMPL_PRLI       */
	(void *)lpfc_disc_neverdev,   		/* CMPL_LOGO       */
	(void *)lpfc_cmpl_adisc_adisc_issue,    /* CMPL_ADISC      */
	(void *)lpfc_disc_neverdev, 		/* CMPL_REG_LOGIN  */
	(void *)lpfc_device_rm_adisc_issue,     /* DEVICE_RM       */
	(void *)lpfc_device_recov_adisc_issue,  /* DEVICE_RECOVERY */

	(void *)lpfc_rcv_plogi_reglogin_issue,	/* RCV_PLOGI  REG_LOGIN_ISSUE */
	(void *)lpfc_rcv_prli_reglogin_issue,	/* RCV_PLOGI       */
	(void *)lpfc_rcv_logo_reglogin_issue,	/* RCV_LOGO        */
	(void *)lpfc_rcv_padisc_reglogin_issue,	/* RCV_ADISC       */
	(void *)lpfc_rcv_padisc_reglogin_issue,	/* RCV_PDISC       */
	(void *)lpfc_rcv_prlo_reglogin_issue,	/* RCV_PRLO        */
	(void *)lpfc_disc_neverdev,		/* CMPL_PLOGI      */
	(void *)lpfc_disc_neverdev,		/* CMPL_PRLI       */
	(void *)lpfc_disc_neverdev,		/* CMPL_LOGO       */
	(void *)lpfc_disc_neverdev,		/* CMPL_ADISC      */
	(void *)lpfc_cmpl_reglogin_reglogin_issue,/* CMPL_REG_LOGIN  */
	(void *)lpfc_device_rm_reglogin_issue,	/* DEVICE_RM       */
	(void *)lpfc_device_recov_reglogin_issue,/* DEVICE_RECOVERY */

	(void *)lpfc_rcv_plogi_prli_issue,	/* RCV_PLOGI   PRLI_ISSUE     */
	(void *)lpfc_rcv_prli_prli_issue,	/* RCV_PRLI        */
	(void *)lpfc_rcv_logo_prli_issue,	/* RCV_LOGO        */
	(void *)lpfc_rcv_padisc_prli_issue,	/* RCV_ADISC       */
	(void *)lpfc_rcv_padisc_prli_issue,	/* RCV_PDISC       */
	(void *)lpfc_rcv_prlo_prli_issue,	/* RCV_PRLO        */
	(void *)lpfc_disc_neverdev,		/* CMPL_PLOGI      */
	(void *)lpfc_cmpl_prli_prli_issue,	/* CMPL_PRLI       */
	(void *)lpfc_disc_neverdev,		/* CMPL_LOGO       */
	(void *)lpfc_disc_neverdev,		/* CMPL_ADISC      */
	(void *)lpfc_disc_neverdev,		/* CMPL_REG_LOGIN  */
	(void *)lpfc_device_rm_prli_issue,	/* DEVICE_RM       */
	(void *)lpfc_device_recov_prli_issue,	/* DEVICE_RECOVERY */

	(void *)lpfc_rcv_plogi_unmap_node,	/* RCV_PLOGI   UNMAPPED_NODE  */
	(void *)lpfc_rcv_prli_unmap_node,	/* RCV_PRLI        */
	(void *)lpfc_rcv_logo_unmap_node,	/* RCV_LOGO        */
	(void *)lpfc_rcv_padisc_unmap_node,	/* RCV_ADISC       */
	(void *)lpfc_rcv_padisc_unmap_node,	/* RCV_PDISC       */
	(void *)lpfc_rcv_prlo_unmap_node,	/* RCV_PRLO        */
	(void *)lpfc_disc_neverdev,		/* CMPL_PLOGI      */
	(void *)lpfc_disc_neverdev,		/* CMPL_PRLI       */
	(void *)lpfc_disc_neverdev,		/* CMPL_LOGO       */
	(void *)lpfc_disc_neverdev,		/* CMPL_ADISC      */
	(void *)lpfc_disc_neverdev,		/* CMPL_REG_LOGIN  */
	(void *)lpfc_disc_neverdev,		/* DEVICE_RM       */
	(void *)lpfc_device_recov_unmap_node,	/* DEVICE_RECOVERY */

	(void *)lpfc_rcv_plogi_mapped_node,	/* RCV_PLOGI   MAPPED_NODE    */
	(void *)lpfc_rcv_prli_mapped_node,	/* RCV_PRLI        */
	(void *)lpfc_rcv_logo_mapped_node,	/* RCV_LOGO        */
	(void *)lpfc_rcv_padisc_mapped_node,	/* RCV_ADISC       */
	(void *)lpfc_rcv_padisc_mapped_node,	/* RCV_PDISC       */
	(void *)lpfc_rcv_prlo_mapped_node,	/* RCV_PRLO        */
	(void *)lpfc_disc_neverdev,		/* CMPL_PLOGI      */
	(void *)lpfc_disc_neverdev,		/* CMPL_PRLI       */
	(void *)lpfc_disc_neverdev,		/* CMPL_LOGO       */
	(void *)lpfc_disc_neverdev,		/* CMPL_ADISC      */
	(void *)lpfc_disc_neverdev,		/* CMPL_REG_LOGIN  */
	(void *)lpfc_disc_neverdev,		/* DEVICE_RM       */
	(void *)lpfc_device_recov_mapped_node,	/* DEVICE_RECOVERY */

	(void *)lpfc_rcv_plogi_npr_node,        /* RCV_PLOGI   NPR_NODE    */
	(void *)lpfc_rcv_prli_npr_node,         /* RCV_PRLI        */
	(void *)lpfc_rcv_logo_npr_node,         /* RCV_LOGO        */
	(void *)lpfc_rcv_padisc_npr_node,       /* RCV_ADISC       */
	(void *)lpfc_rcv_padisc_npr_node,       /* RCV_PDISC       */
	(void *)lpfc_rcv_prlo_npr_node,         /* RCV_PRLO        */
	(void *)lpfc_disc_neverdev,             /* CMPL_PLOGI      */
	(void *)lpfc_disc_neverdev,             /* CMPL_PRLI       */
	(void *)lpfc_cmpl_logo_npr_node,        /* CMPL_LOGO       */
	(void *)lpfc_disc_nodev,		/* CMPL_ADISC      */
	(void *)lpfc_cmpl_reglogin_npr_node,    /* CMPL_REG_LOGIN  */
	(void *)lpfc_device_rm_npr_node,        /* DEVICE_RM       */
	(void *)lpfc_device_recov_npr_node,     /* DEVICE_RECOVERY */
};

int
lpfc_disc_state_machine(struct lpfc_hba * phba,
			struct lpfc_nodelist * ndlp, void *arg, uint32_t evt)
{
	uint32_t cur_state, rc;
	uint32_t(*func) (struct lpfc_hba *, struct lpfc_nodelist *, void *,
			 uint32_t);

	ndlp->nlp_disc_refcnt++;
	cur_state = ndlp->nlp_state;

	/* DSM in event <evt> on NPort <nlp_DID> in state <cur_state> */
	lpfc_printf_log(phba,
			KERN_INFO,
			LOG_DISCOVERY,
			"%d:0211 DSM in event x%x on NPort x%x in state %d "
			"Data: x%x\n",
			phba->brd_no,
			evt, ndlp->nlp_DID, cur_state, ndlp->nlp_flag);

	func = (uint32_t(*)(struct lpfc_hba *, struct lpfc_nodelist *, void *,
			    uint32_t))
	    lpfc_disc_action[(cur_state * NLP_EVT_MAX_EVENT) + evt];
	rc = (func) (phba, ndlp, arg, evt);

	/* DSM out state <rc> on NPort <nlp_DID> */
	lpfc_printf_log(phba,
		       KERN_INFO,
		       LOG_DISCOVERY,
		       "%d:0212 DSM out state %d on NPort x%x Data: x%x\n",
		       phba->brd_no,
		       rc, ndlp->nlp_DID, ndlp->nlp_flag);

	ndlp->nlp_disc_refcnt--;

	/* Check to see if ndlp removal is deferred */
	if ((ndlp->nlp_disc_refcnt == 0)
	    && (ndlp->nlp_flag & NLP_DELAY_REMOVE)) {

		ndlp->nlp_flag &= ~NLP_DELAY_REMOVE;
		lpfc_nlp_remove(phba, ndlp);
		return (NLP_STE_FREED_NODE);
	}
	if (rc == NLP_STE_FREED_NODE)
		return (NLP_STE_FREED_NODE);
	ndlp->nlp_state = rc;
	return (rc);
}
