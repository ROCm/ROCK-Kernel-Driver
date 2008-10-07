/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2006-2008 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/pci.h>

#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_fc.h>

#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_crtn.h"
#include "lpfc_ioctl.h"
#include "lpfc_logmsg.h"
#include "lpfc_vport.h"


struct lpfcdfc_event {
	struct list_head node;
	int ref;
	wait_queue_head_t wq;

	/* Event type and waiter identifiers */
	uint32_t type_mask;
	uint32_t req_id;
	uint32_t reg_id;

	/* next two flags are here for the auto-delete logic */
	unsigned long wait_time_stamp;
	int waiting;

	/* seen and not seen events */
	struct list_head events_to_get;
	struct list_head events_to_see;
};

struct event_data {
	struct list_head node;
	uint32_t type;
	uint32_t immed_dat;
	void * data;
	uint32_t len;
};


/* values for a_topology */
#define LNK_LOOP                0x1
#define LNK_PUBLIC_LOOP         0x2
#define LNK_FABRIC              0x3
#define LNK_PT2PT               0x4

/* values for a_linkState */
#define LNK_DOWN                0x1
#define LNK_UP                  0x2
#define LNK_FLOGI               0x3
#define LNK_DISCOVERY           0x4
#define LNK_REDISCOVERY         0x5
#define LNK_READY               0x6

struct lpfcdfc_host {
	struct list_head node;
	int inst;
	struct lpfc_hba * phba;
	struct lpfc_vport *vport;
	struct Scsi_Host * host;
	struct pci_dev * dev;
	void (*base_ct_unsol_event)(struct lpfc_hba *,
				    struct lpfc_sli_ring *,
				    struct lpfc_iocbq *);
	/* Threads waiting for async event */
	struct list_head ev_waiters;
	uint32_t blocked;
	uint32_t ref_count;
};




static void lpfc_ioctl_timeout_iocb_cmpl(struct lpfc_hba *,
				  struct lpfc_iocbq *, struct lpfc_iocbq *);

static struct lpfc_dmabufext *
dfc_cmd_data_alloc(struct lpfc_hba *, char *,
		   struct ulp_bde64 *, uint32_t);
static int dfc_cmd_data_free(struct lpfc_hba *, struct lpfc_dmabufext *);
static int dfc_rsp_data_copy(struct lpfc_hba *, uint8_t *,
				struct lpfc_dmabufext *,
				uint32_t);
static int lpfc_issue_ct_rsp(struct lpfc_hba *, uint32_t, struct lpfc_dmabuf *,
		      struct lpfc_dmabufext *);

static struct lpfcdfc_host * lpfcdfc_host_from_hba(struct lpfc_hba *);

static DEFINE_MUTEX(lpfcdfc_lock);

static struct list_head lpfcdfc_hosts = LIST_HEAD_INIT(lpfcdfc_hosts);

static int lpfcdfc_major = 0;

static int
lpfc_ioctl_hba_rnid(struct lpfc_hba * phba,
			struct lpfcCmdInput * cip,
			void *dataout)
{
	struct nport_id idn;
	struct lpfc_sli *psli;
	struct lpfc_iocbq *cmdiocbq = NULL;
	struct lpfc_iocbq *rspiocbq = NULL;
	RNID *prsp;
	uint32_t *pcmd;
	uint32_t *psta;
	IOCB_t *rsp;
	struct lpfc_sli_ring *pring;
	void *context2;
	int i0;
	int rtnbfrsiz;
	struct lpfc_nodelist *pndl;
	int rc = 0;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];

	if (copy_from_user((uint8_t *) &idn, (void __user *) cip->lpfc_arg1,
			   sizeof(struct nport_id))) {
		rc = EIO;
		return rc;
	}

	if (idn.idType == LPFC_WWNN_TYPE)
		pndl = lpfc_findnode_wwnn(phba->pport,
					  (struct lpfc_name *) idn.wwpn);
	else
		pndl = lpfc_findnode_wwpn(phba->pport,
					  (struct lpfc_name *) idn.wwpn);

	if (!pndl || !NLP_CHK_NODE_ACT(pndl))
		return ENODEV;

	for (i0 = 0;
	     i0 < 10 && (pndl->nlp_flag & NLP_ELS_SND_MASK) == NLP_RNID_SND;
	     i0++) {
		mdelay(1000);
	}

	if (i0 == 10) {
		pndl->nlp_flag &= ~NLP_RNID_SND;
		return EBUSY;
	}

	cmdiocbq = lpfc_prep_els_iocb(phba->pport, 1, (2 * sizeof(uint32_t)), 0,
				      pndl, pndl->nlp_DID, ELS_CMD_RNID);
	if (!cmdiocbq)
		return ENOMEM;

	/*
	 *  Context2 is used by prep/free to locate cmd and rsp buffers,
	 *  but context2 is also used by iocb_wait to hold a rspiocb ptr.
	 *  The rsp iocbq can be returned from the completion routine for
	 *  iocb_wait, so save the prep/free value locally . It will be
	 *  restored after returning from iocb_wait.
	 */
	context2 = cmdiocbq->context2;

	if ((rspiocbq = lpfc_sli_get_iocbq(phba)) == NULL) {
		rc = ENOMEM;
		goto sndrndqwt;
	}
	rsp = &(rspiocbq->iocb);

	pcmd = (uint32_t *) (((struct lpfc_dmabuf *) cmdiocbq->context2)->virt);
	*pcmd++ = ELS_CMD_RNID;

	memset((void *) pcmd, 0, sizeof (RNID));
	((RNID *) pcmd)->Format = 0;
	((RNID *) pcmd)->Format = RNID_TOPOLOGY_DISC;
	cmdiocbq->context1 = NULL;
	cmdiocbq->context2 = NULL;
	cmdiocbq->iocb_flag |= LPFC_IO_LIBDFC;

	pndl->nlp_flag |= NLP_RNID_SND;
	cmdiocbq->iocb.ulpTimeout = (phba->fc_ratov * 2) + 3 ;

	rc = lpfc_sli_issue_iocb_wait(phba, pring, cmdiocbq, rspiocbq,
				(phba->fc_ratov * 2) + LPFC_DRVR_TIMEOUT);
	pndl->nlp_flag &= ~NLP_RNID_SND;
	cmdiocbq->context2 = context2;

	if (rc == IOCB_TIMEDOUT) {
		lpfc_sli_release_iocbq(phba, rspiocbq);
		cmdiocbq->context1 = NULL;
		cmdiocbq->iocb_cmpl = lpfc_ioctl_timeout_iocb_cmpl;
		return EIO;
	}

	if (rc != IOCB_SUCCESS) {
		rc = EIO;
		goto sndrndqwt;
	}

	if (rsp->ulpStatus == IOSTAT_SUCCESS) {
		struct lpfc_dmabuf *buf_ptr1, *buf_ptr;
		buf_ptr1 = (struct lpfc_dmabuf *)(cmdiocbq->context2);
		buf_ptr = list_entry(buf_ptr1->list.next, struct lpfc_dmabuf,
					list);
		psta = (uint32_t*)buf_ptr->virt;
		prsp = (RNID *) (psta + 1);	/*  then rnid response data */
		rtnbfrsiz = prsp->CommonLen + prsp->SpecificLen +
							sizeof (uint32_t);
		memcpy((uint8_t *) dataout, (uint8_t *) psta, rtnbfrsiz);

		if (rtnbfrsiz > cip->lpfc_outsz)
			rtnbfrsiz = cip->lpfc_outsz;
		if (copy_to_user
		    ((void __user *) cip->lpfc_arg2, (uint8_t *) & rtnbfrsiz,
		     sizeof (int)))
			rc = EIO;
	} else if (rsp->ulpStatus == IOSTAT_LS_RJT)  {
		uint8_t ls_rjt[8];
		uint32_t *ls_rjtrsp;

		ls_rjtrsp = (uint32_t*)(ls_rjt + 4);

		/* construct the LS_RJT payload */
		ls_rjt[0] = 0x01;
		ls_rjt[1] = 0x00;
		ls_rjt[2] = 0x00;
		ls_rjt[3] = 0x00;

		*ls_rjtrsp = be32_to_cpu(rspiocbq->iocb.un.ulpWord[4]);
		rtnbfrsiz = 8;
		memcpy((uint8_t *) dataout, (uint8_t *) ls_rjt, rtnbfrsiz);
		if (copy_to_user
		    ((void __user *) cip->lpfc_arg2, (uint8_t *) & rtnbfrsiz,
		     sizeof (int)))
			rc = EIO;
	} else
		rc = EACCES;

sndrndqwt:
	if (cmdiocbq)
		lpfc_els_free_iocb(phba, cmdiocbq);

	if (rspiocbq)
		lpfc_sli_release_iocbq(phba, rspiocbq);

	return rc;
}

static void
lpfc_ioctl_timeout_iocb_cmpl(struct lpfc_hba * phba,
			     struct lpfc_iocbq * cmd_iocb_q,
			     struct lpfc_iocbq * rsp_iocb_q)
{
	struct lpfc_timedout_iocb_ctxt *iocb_ctxt = cmd_iocb_q->context1;

	if (!iocb_ctxt) {
		if (cmd_iocb_q->context2)
			lpfc_els_free_iocb(phba, cmd_iocb_q);
		else
			lpfc_sli_release_iocbq(phba,cmd_iocb_q);
		return;
	}

	if (iocb_ctxt->outdmp)
		dfc_cmd_data_free(phba, iocb_ctxt->outdmp);

	if (iocb_ctxt->indmp)
		dfc_cmd_data_free(phba, iocb_ctxt->indmp);

	if (iocb_ctxt->mp) {
		lpfc_mbuf_free(phba,
			       iocb_ctxt->mp->virt,
			       iocb_ctxt->mp->phys);
		kfree(iocb_ctxt->mp);
	}

	if (iocb_ctxt->bmp) {
		lpfc_mbuf_free(phba,
			       iocb_ctxt->bmp->virt,
			       iocb_ctxt->bmp->phys);
		kfree(iocb_ctxt->bmp);
	}

	lpfc_sli_release_iocbq(phba,cmd_iocb_q);

	if (iocb_ctxt->rspiocbq)
			lpfc_sli_release_iocbq(phba, iocb_ctxt->rspiocbq);

	kfree(iocb_ctxt);
}


static int
lpfc_ioctl_send_els(struct lpfc_hba * phba,
		    struct lpfcCmdInput * cip, void *dataout)
{
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring = &psli->ring[LPFC_ELS_RING];
	struct lpfc_iocbq *cmdiocbq, *rspiocbq;
	struct lpfc_dmabufext *pcmdext = NULL, *prspext = NULL;
	struct lpfc_nodelist *pndl;
	struct ulp_bde64 *bpl;
	IOCB_t *rsp;
	struct lpfc_dmabuf *pcmd, *prsp, *pbuflist = NULL;
	uint16_t rpi = 0;
	struct nport_id destID;
	int rc = 0;
	uint32_t cmdsize;
	uint32_t rspsize;
	uint32_t elscmd;
	int iocb_status;

	elscmd = *(uint32_t *)cip->lpfc_arg2;
	cmdsize = cip->lpfc_arg4;
	rspsize = cip->lpfc_outsz;

	if (copy_from_user((uint8_t *)&destID, (void __user *)cip->lpfc_arg1,
			   sizeof(struct nport_id)))
		return EIO;

	if ((rspiocbq = lpfc_sli_get_iocbq(phba)) == NULL)
		return ENOMEM;

	rsp = &rspiocbq->iocb;

	if (destID.idType == 0)
		pndl = lpfc_findnode_wwpn(phba->pport,
					  (struct lpfc_name *)&destID.wwpn);
	else {
		destID.d_id = (destID.d_id & Mask_DID);
		pndl = lpfc_findnode_did(phba->pport, destID.d_id);
	}

	if (!pndl || !NLP_CHK_NODE_ACT(pndl)) {
		if (destID.idType == 0) {
			lpfc_sli_release_iocbq(phba, rspiocbq);
			return ENODEV;
		}
		if (!pndl) {
			pndl = kmalloc(sizeof (struct lpfc_nodelist),
					GFP_KERNEL);
			if (!pndl) {
				lpfc_sli_release_iocbq(phba, rspiocbq);
				return ENODEV;
			}
			lpfc_nlp_init(phba->pport, pndl, destID.d_id);
			lpfc_nlp_set_state(phba->pport, pndl, NLP_STE_NPR_NODE);
		} else {
			pndl = lpfc_enable_node(phba->pport, pndl,
						NLP_STE_NPR_NODE);
			if (!pndl) {
				lpfc_sli_release_iocbq(phba, rspiocbq);
				return ENODEV;
			}
		}
	} else {
		lpfc_nlp_get(pndl);
		rpi = pndl->nlp_rpi;
	}

	cmdiocbq = lpfc_prep_els_iocb(phba->pport, 1, cmdsize, 0, pndl,
				      pndl->nlp_DID, elscmd);

	/* release the new pndl once the iocb complete */
	lpfc_nlp_put(pndl);

	if (cmdiocbq == NULL) {
		lpfc_sli_release_iocbq(phba, rspiocbq);
		return EIO;
	}

	pcmd = (struct lpfc_dmabuf *) cmdiocbq->context2;
	prsp = (struct lpfc_dmabuf *) pcmd->list.next;

	/*
	 * If we exceed the size of the allocated mbufs we need to
	 * free them and allocate our own.
	 */
	if ((cmdsize > LPFC_BPL_SIZE) || (rspsize > LPFC_BPL_SIZE)) {
		lpfc_mbuf_free(phba, pcmd->virt, pcmd->phys);
		kfree(pcmd);
		lpfc_mbuf_free(phba, prsp->virt, prsp->phys);
		kfree(prsp);
		cmdiocbq->context2 = NULL;

		pbuflist = (struct lpfc_dmabuf *) cmdiocbq->context3;
		bpl = (struct ulp_bde64 *) pbuflist->virt;
		pcmdext = dfc_cmd_data_alloc(phba, cip->lpfc_arg2,
					     bpl, cmdsize);
		if (!pcmdext) {
			lpfc_els_free_iocb(phba, cmdiocbq);
			lpfc_sli_release_iocbq(phba, rspiocbq);
			return ENOMEM;
		}
		bpl += pcmdext->flag;
		prspext = dfc_cmd_data_alloc(phba, NULL, bpl, rspsize);
		if (!prspext) {
			dfc_cmd_data_free(phba, pcmdext);
			lpfc_els_free_iocb(phba, cmdiocbq);
			lpfc_sli_release_iocbq(phba, rspiocbq);
			return ENOMEM;
		}
	} else {
		/* Copy the command from user space */
		if (copy_from_user((uint8_t *) pcmd->virt,
				   (void __user *) cip->lpfc_arg2,
				   cmdsize)) {
			lpfc_els_free_iocb(phba, cmdiocbq);
			lpfc_sli_release_iocbq(phba, rspiocbq);
			return EIO;
		}
	}

	cmdiocbq->iocb.ulpContext = rpi;
	cmdiocbq->iocb_flag |= LPFC_IO_LIBDFC;
	cmdiocbq->context1 = NULL;
	cmdiocbq->context2 = NULL;

	iocb_status = lpfc_sli_issue_iocb_wait(phba, pring, cmdiocbq, rspiocbq,
				      (phba->fc_ratov*2) + LPFC_DRVR_TIMEOUT);
	rc = iocb_status;

	if (rc == IOCB_SUCCESS) {
		if (rsp->ulpStatus == IOSTAT_SUCCESS) {
			if (rspsize < (rsp->un.ulpWord[0] & 0xffffff)) {
				rc = ERANGE;
			} else {
				rspsize = rsp->un.ulpWord[0] & 0xffffff;
				if (pbuflist) {
					if (dfc_rsp_data_copy(
						phba,
						(uint8_t *) cip->lpfc_dataout,
						prspext,
						rspsize)) {
						rc = EIO;
					} else {
						cip->lpfc_outsz = 0;
					}
				} else {
					if (copy_to_user( (void __user *)
						cip->lpfc_dataout,
						(uint8_t *) prsp->virt,
						rspsize)) {
						rc = EIO;
					} else {
						cip->lpfc_outsz = 0;
					}
				}
			}
		} else if (rsp->ulpStatus == IOSTAT_LS_RJT) {
			uint8_t ls_rjt[8];

			/* construct the LS_RJT payload */
			ls_rjt[0] = 0x01;
			ls_rjt[1] = 0x00;
			ls_rjt[2] = 0x00;
			ls_rjt[3] = 0x00;
			memcpy(&ls_rjt[4], (uint8_t *) &rsp->un.ulpWord[4],
			       sizeof(uint32_t));

			if (rspsize < 8)
				rc = ERANGE;
			else
				rspsize = 8;

			memcpy(dataout, ls_rjt, rspsize);
		} else
			rc = EIO;

		if (copy_to_user((void __user *)cip->lpfc_arg3,
				 (uint8_t *)&rspsize, sizeof(uint32_t)))
			rc = EIO;
	} else {
		rc = EIO;
	}

	if (pbuflist) {
		dfc_cmd_data_free(phba, pcmdext);
		dfc_cmd_data_free(phba, prspext);
	} else
		cmdiocbq->context2 = (uint8_t *) pcmd;

	if (iocb_status != IOCB_TIMEDOUT)
		lpfc_els_free_iocb(phba, cmdiocbq);

	lpfc_sli_release_iocbq(phba, rspiocbq);
	return rc;
}

static int
lpfc_ioctl_send_mgmt_rsp(struct lpfc_hba * phba,
			 struct lpfcCmdInput * cip)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(phba->pport);
	struct ulp_bde64 *bpl;
	struct lpfc_dmabuf *bmp = NULL;
	struct lpfc_dmabufext *indmp = NULL;
	uint32_t tag =  (uint32_t)cip->lpfc_flag; /* XRI for XMIT_SEQUENCE */
	unsigned long reqbfrcnt = (unsigned long)cip->lpfc_arg2;
	int rc = 0;
	unsigned long iflag;

	if (!reqbfrcnt || (reqbfrcnt > (80 * BUF_SZ_4K))) {
		rc = ERANGE;
		return rc;
	}

	bmp = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);
	if (!bmp) {
		rc = ENOMEM;
		goto send_mgmt_rsp_exit;
	}
	spin_lock_irqsave(shost->host_lock, iflag);
	bmp->virt = lpfc_mbuf_alloc(phba, 0, &bmp->phys);
	spin_unlock_irqrestore(shost->host_lock, iflag); /* remove */
	if (!bmp->virt) {
		rc = ENOMEM;
		goto send_mgmt_rsp_free_bmp;
	}

	INIT_LIST_HEAD(&bmp->list);
	bpl = (struct ulp_bde64 *) bmp->virt;

	indmp = dfc_cmd_data_alloc(phba, cip->lpfc_arg1, bpl, reqbfrcnt);
	if (!indmp) {
		rc = ENOMEM;
		goto send_mgmt_rsp_free_bmpvirt;
	}
	rc = lpfc_issue_ct_rsp(phba, tag, bmp, indmp);
	if (rc) {
		if (rc == IOCB_TIMEDOUT)
			rc = ETIMEDOUT;
		else if (rc == IOCB_ERROR)
			rc = EACCES;
	}

	dfc_cmd_data_free(phba, indmp);
send_mgmt_rsp_free_bmpvirt:
	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
send_mgmt_rsp_free_bmp:
	kfree(bmp);
send_mgmt_rsp_exit:
	return rc;
}

static int
lpfc_ioctl_send_mgmt_cmd(struct lpfc_hba * phba,
			 struct lpfcCmdInput * cip, void *dataout)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(phba->pport);
	struct lpfc_nodelist *pndl = NULL;
	struct ulp_bde64 *bpl = NULL;
	struct lpfc_name findwwn;
	uint32_t finddid, timeout;
	struct lpfc_iocbq *cmdiocbq = NULL, *rspiocbq = NULL;
	struct lpfc_dmabufext *indmp = NULL, *outdmp = NULL;
	IOCB_t *cmd = NULL, *rsp = NULL;
	struct lpfc_dmabuf *bmp = NULL;
	struct lpfc_sli *psli = NULL;
	struct lpfc_sli_ring *pring = NULL;
	int i0 = 0, rc = 0, reqbfrcnt, snsbfrcnt;
	struct lpfc_timedout_iocb_ctxt *iocb_ctxt;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];

	if (!(psli->sli_flag & LPFC_SLI2_ACTIVE)) {
		rc = EACCES;
		goto send_mgmt_cmd_exit;
	}

	reqbfrcnt = cip->lpfc_arg4;
	snsbfrcnt = cip->lpfc_arg5;

	if (!reqbfrcnt || !snsbfrcnt
		|| (reqbfrcnt + snsbfrcnt > 80 * BUF_SZ_4K)) {
		rc = ERANGE;
		goto send_mgmt_cmd_exit;
	}

	if (phba->pport->port_state != LPFC_VPORT_READY) {
		rc = ENODEV;
		goto send_mgmt_cmd_exit;
	}

	if (cip->lpfc_cmd == LPFC_HBA_SEND_MGMT_CMD) {
		rc = copy_from_user(&findwwn, (void __user *)cip->lpfc_arg3,
						sizeof(struct lpfc_name));
		if (rc) {
			rc = EIO;
			goto send_mgmt_cmd_exit;
		}
		pndl = lpfc_findnode_wwpn(phba->pport, &findwwn);
		/* Do additional get to pndl found so that at the end of the
		 * function we can do unditional lpfc_nlp_put on it.
		 */
		if (pndl && NLP_CHK_NODE_ACT(pndl))
			lpfc_nlp_get(pndl);
	} else {
		finddid = (uint32_t)(unsigned long)cip->lpfc_arg3;
		pndl = lpfc_findnode_did(phba->pport, finddid);
		if (!pndl || !NLP_CHK_NODE_ACT(pndl)) {
			if (phba->pport->fc_flag & FC_FABRIC) {
				if (!pndl) {
					pndl = kmalloc(sizeof
						(struct lpfc_nodelist),
						GFP_KERNEL);
					if (!pndl) {
						rc = ENODEV;
						goto send_mgmt_cmd_exit;
					}
					lpfc_nlp_init(phba->pport, pndl,
							finddid);
					lpfc_nlp_set_state(phba->pport,
						pndl, NLP_STE_PLOGI_ISSUE);
					/* Indicate free ioctl allocated
					 * memory for ndlp after it's done
					 */
					NLP_SET_FREE_REQ(pndl);
				} else
					lpfc_enable_node(phba->pport,
						pndl, NLP_STE_PLOGI_ISSUE);

				if (lpfc_issue_els_plogi(phba->pport,
							 pndl->nlp_DID, 0)) {
					rc = ENODEV;
					goto send_mgmt_cmd_free_pndl_exit;
				}

				/* Allow the node to complete discovery */
				while (i0++ < 4) {
					if (pndl->nlp_state ==
						NLP_STE_UNMAPPED_NODE)
						break;
					msleep(500);
				}

				if (i0 == 4) {
					rc = ENODEV;
					goto send_mgmt_cmd_free_pndl_exit;
				}
			} else {
				rc = ENODEV;
				goto send_mgmt_cmd_exit;
			}
		} else
			/* Do additional get to pndl found so at the end of
			 * the function we can do unconditional lpfc_nlp_put.
			 */
			lpfc_nlp_get(pndl);
	}

	if (!pndl || !NLP_CHK_NODE_ACT(pndl)) {
		rc = ENODEV;
		goto send_mgmt_cmd_exit;
	}

	if (pndl->nlp_flag & NLP_ELS_SND_MASK) {
		rc = ENODEV;
		goto send_mgmt_cmd_free_pndl_exit;
	}

	spin_lock_irq(shost->host_lock);
	cmdiocbq = lpfc_sli_get_iocbq(phba);
	if (!cmdiocbq) {
		rc = ENOMEM;
		spin_unlock_irq(shost->host_lock);
		goto send_mgmt_cmd_free_pndl_exit;
	}
	cmd = &cmdiocbq->iocb;

	rspiocbq = lpfc_sli_get_iocbq(phba);
	if (!rspiocbq) {
		rc = ENOMEM;
		goto send_mgmt_cmd_free_cmdiocbq;
	}
	spin_unlock_irq(shost->host_lock);

	rsp = &rspiocbq->iocb;

	bmp = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);
	if (!bmp) {
		rc = ENOMEM;
		spin_lock_irq(shost->host_lock);
		goto send_mgmt_cmd_free_rspiocbq;
	}

	spin_lock_irq(shost->host_lock);
	bmp->virt = lpfc_mbuf_alloc(phba, 0, &bmp->phys);
	if (!bmp->virt) {
		rc = ENOMEM;
		goto send_mgmt_cmd_free_bmp;
	}
	spin_unlock_irq(shost->host_lock);

	INIT_LIST_HEAD(&bmp->list);
	bpl = (struct ulp_bde64 *) bmp->virt;
	indmp = dfc_cmd_data_alloc(phba, cip->lpfc_arg1, bpl, reqbfrcnt);
	if (!indmp) {
		rc = ENOMEM;
		spin_lock_irq(shost->host_lock);
		goto send_mgmt_cmd_free_bmpvirt;
	}

	/* flag contains total number of BPLs for xmit */
	bpl += indmp->flag;

	outdmp = dfc_cmd_data_alloc(phba, NULL, bpl, snsbfrcnt);
	if (!outdmp) {
		rc = ENOMEM;
		spin_lock_irq(shost->host_lock);
		goto send_mgmt_cmd_free_indmp;
	}

	cmd->un.genreq64.bdl.ulpIoTag32 = 0;
	cmd->un.genreq64.bdl.addrHigh = putPaddrHigh(bmp->phys);
	cmd->un.genreq64.bdl.addrLow = putPaddrLow(bmp->phys);
	cmd->un.genreq64.bdl.bdeFlags = BUFF_TYPE_BLP_64;
	cmd->un.genreq64.bdl.bdeSize =
	    (outdmp->flag + indmp->flag) * sizeof (struct ulp_bde64);
	cmd->ulpCommand = CMD_GEN_REQUEST64_CR;
	cmd->un.genreq64.w5.hcsw.Fctl = (SI | LA);
	cmd->un.genreq64.w5.hcsw.Dfctl = 0;
	cmd->un.genreq64.w5.hcsw.Rctl = FC_UNSOL_CTL;
	cmd->un.genreq64.w5.hcsw.Type = FC_COMMON_TRANSPORT_ULP;
	cmd->ulpBdeCount = 1;
	cmd->ulpLe = 1;
	cmd->ulpClass = CLASS3;
	cmd->ulpContext = pndl->nlp_rpi;
	cmd->ulpOwner = OWN_CHIP;
	cmdiocbq->vport = phba->pport;
	cmdiocbq->context1 = NULL;
	cmdiocbq->context2 = NULL;
	cmdiocbq->iocb_flag |= LPFC_IO_LIBDFC;

	if (cip->lpfc_flag == 0 )
		timeout = phba->fc_ratov * 2 ;
	else
		timeout = cip->lpfc_flag;

	cmd->ulpTimeout = timeout;

	rc = lpfc_sli_issue_iocb_wait(phba, pring, cmdiocbq, rspiocbq,
					timeout + LPFC_DRVR_TIMEOUT);

	if (rc == IOCB_TIMEDOUT) {
		lpfc_sli_release_iocbq(phba, rspiocbq);
		iocb_ctxt = kmalloc(sizeof(struct lpfc_timedout_iocb_ctxt),
				    GFP_KERNEL);
		if (!iocb_ctxt) {
			rc = EACCES;
			goto send_mgmt_cmd_free_pndl_exit;
		}

		cmdiocbq->context1 = iocb_ctxt;
		cmdiocbq->context2 = NULL;
		iocb_ctxt->rspiocbq = NULL;
		iocb_ctxt->mp = NULL;
		iocb_ctxt->bmp = bmp;
		iocb_ctxt->outdmp = outdmp;
		iocb_ctxt->lpfc_cmd = NULL;
		iocb_ctxt->indmp = indmp;

		cmdiocbq->iocb_cmpl = lpfc_ioctl_timeout_iocb_cmpl;
		rc = EACCES;
		goto send_mgmt_cmd_free_pndl_exit;
	}

	if (rc != IOCB_SUCCESS) {
		rc = EACCES;
		goto send_mgmt_cmd_free_outdmp;
	}

	if (rsp->ulpStatus) {
		if (rsp->ulpStatus == IOSTAT_LOCAL_REJECT) {
			switch (rsp->un.ulpWord[4] & 0xff) {
			case IOERR_SEQUENCE_TIMEOUT:
				rc = ETIMEDOUT;
				break;
			case IOERR_INVALID_RPI:
				rc = EFAULT;
				break;
			default:
				rc = EACCES;
				break;
			}
			goto send_mgmt_cmd_free_outdmp;
		}
	} else
		outdmp->flag = rsp->un.genreq64.bdl.bdeSize;

	/* Copy back response data */
	if (outdmp->flag > snsbfrcnt) {
		rc = ERANGE;
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
				"1209 C_CT Request error Data: x%x x%x\n",
				outdmp->flag, BUF_SZ_4K);
		goto send_mgmt_cmd_free_outdmp;
	}

	/* copy back size of response, and response itself */
	memcpy(dataout, &outdmp->flag, sizeof (int));
	rc = dfc_rsp_data_copy (phba, cip->lpfc_arg2, outdmp, outdmp->flag);
	if (rc)
		rc = EIO;

send_mgmt_cmd_free_outdmp:
	dfc_cmd_data_free(phba, outdmp);
send_mgmt_cmd_free_indmp:
	dfc_cmd_data_free(phba, indmp);
	spin_lock_irq(shost->host_lock);
send_mgmt_cmd_free_bmpvirt:
	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
send_mgmt_cmd_free_bmp:
	kfree(bmp);
send_mgmt_cmd_free_rspiocbq:
	lpfc_sli_release_iocbq(phba, rspiocbq);
send_mgmt_cmd_free_cmdiocbq:
	lpfc_sli_release_iocbq(phba, cmdiocbq);
	spin_unlock_irq(shost->host_lock);
send_mgmt_cmd_free_pndl_exit:
	lpfc_nlp_put(pndl);
send_mgmt_cmd_exit:
	return rc;
}

static inline struct lpfcdfc_event *
lpfcdfc_event_new(uint32_t ev_mask,
			int      ev_reg_id,
			uint32_t ev_req_id)
{
	struct  lpfcdfc_event * evt = kzalloc(sizeof(*evt), GFP_KERNEL);
	if (evt == NULL)
		return NULL;

	INIT_LIST_HEAD(&evt->events_to_get);
	INIT_LIST_HEAD(&evt->events_to_see);
	evt->type_mask = ev_mask;
	evt->req_id = ev_req_id;
	evt->reg_id = ev_reg_id;
	evt->wait_time_stamp = jiffies;
	init_waitqueue_head(&evt->wq);

	return evt;
}

static inline void lpfcdfc_event_free(struct lpfcdfc_event * evt)
{
	struct event_data  * ed;

	list_del(&evt->node);

	while(!list_empty(&evt->events_to_get)) {
		ed = list_entry(evt->events_to_get.next, typeof(*ed), node);
		list_del(&ed->node);
		kfree(ed->data);
		kfree(ed);
	}

	while(!list_empty(&evt->events_to_see)) {
		ed = list_entry(evt->events_to_see.next, typeof(*ed), node);
		list_del(&ed->node);
		kfree(ed->data);
		kfree(ed);
	}

	kfree(evt);
}

#define lpfcdfc_event_ref(evt) evt->ref++

#define lpfcdfc_event_unref(evt)                \
	if (--evt->ref < 0)                     \
		lpfcdfc_event_free(evt);

static int
lpfc_ioctl_hba_get_event(struct lpfc_hba * phba,
			 struct lpfcCmdInput * cip,
			 void **dataout, int *data_size)
{
	uint32_t ev_mask   = ((uint32_t)(unsigned long)cip->lpfc_arg3 &
			      FC_REG_EVENT_MASK);
	int      ev_reg_id = (uint32_t) cip->lpfc_flag;
	uint32_t ev_req_id = 0;
	struct lpfcdfc_host * dfchba;
	struct lpfcdfc_event * evt;
	struct event_data * evt_dat = NULL;
	int ret_val = 0;

	/* All other events supported through NET_LINK_EVENTs */
	if (ev_mask != FC_REG_CT_EVENT)
		return ENOENT;

	mutex_lock(&lpfcdfc_lock);
	list_for_each_entry(dfchba, &lpfcdfc_hosts, node)
		if (dfchba->phba == phba)
			break;
	mutex_unlock(&lpfcdfc_lock);

	BUG_ON(&dfchba->node == &lpfcdfc_hosts);

	if ((ev_mask == FC_REG_CT_EVENT) &&
	    copy_from_user(&ev_req_id, (void __user *)cip->lpfc_arg2,
			sizeof (uint32_t)))
		return EIO;

	mutex_lock(&lpfcdfc_lock);
	list_for_each_entry(evt, &dfchba->ev_waiters, node)
		if (evt->reg_id == ev_reg_id) {
			if(list_empty(&evt->events_to_get))
				break;
			lpfcdfc_event_ref(evt);
			evt->wait_time_stamp = jiffies;
			evt_dat = list_entry(evt->events_to_get.prev,
					     struct event_data, node);
			list_del(&evt_dat->node);
			break;
		}
	mutex_unlock(&lpfcdfc_lock);

	if (evt_dat == NULL)
		return ENOENT;

	BUG_ON((ev_mask & evt_dat->type) == 0);

	if (evt_dat->len > cip->lpfc_outsz)
		evt_dat->len = cip->lpfc_outsz;

	if (copy_to_user((void __user *)cip->lpfc_arg2, &evt_dat->immed_dat,
		sizeof (uint32_t)) ||
	    copy_to_user((void __user *)cip->lpfc_arg1, &evt_dat->len,
			 sizeof (uint32_t))) {
		ret_val =  EIO;
		goto error_get_event_exit;
	}

	if (evt_dat->len > 0) {
		*data_size = evt_dat->len;
		*dataout = kmalloc(*data_size, GFP_KERNEL);
		if (*dataout)
			memcpy(*dataout, evt_dat->data, *data_size);
		else
			*data_size = 0;

	} else
		*data_size = 0;
	ret_val = 0;

error_get_event_exit:

	kfree(evt_dat->data);
	kfree(evt_dat);
	mutex_lock(&lpfcdfc_lock);
	lpfcdfc_event_unref(evt);
	mutex_unlock(&lpfcdfc_lock);

	return ret_val;
}

static int
lpfc_ioctl_hba_set_event(struct lpfc_hba * phba,
			 struct lpfcCmdInput * cip)
{
	uint32_t ev_mask   = ((uint32_t)(unsigned long)cip->lpfc_arg3 &
			      FC_REG_EVENT_MASK);
	int      ev_reg_id =  cip->lpfc_flag;
	uint32_t ev_req_id = 0;

	struct lpfcdfc_host * dfchba;
	struct lpfcdfc_event * evt;

	int ret_val = 0;

	/* All other events supported through NET_LINK_EVENTs */
	if (ev_mask != FC_REG_CT_EVENT)
		return ENOENT;

	mutex_lock(&lpfcdfc_lock);
	list_for_each_entry(dfchba, &lpfcdfc_hosts, node) {
		if (dfchba->phba == phba)
			break;
	}
	mutex_unlock(&lpfcdfc_lock);
	BUG_ON(&dfchba->node == &lpfcdfc_hosts);

	if (ev_mask == FC_REG_CT_EVENT)
		ev_req_id = ((uint32_t)(unsigned long)cip->lpfc_arg2);

	mutex_lock(&lpfcdfc_lock);
	list_for_each_entry(evt, &dfchba->ev_waiters, node) {
		if (evt->reg_id == ev_reg_id) {
			lpfcdfc_event_ref(evt);
			evt->wait_time_stamp = jiffies;
			break;
		}
	}
	mutex_unlock(&lpfcdfc_lock);

	if (&evt->node == &dfchba->ev_waiters) {
		/* no event waiting struct yet - first call */
		evt = lpfcdfc_event_new(ev_mask, ev_reg_id, ev_req_id);
		if (evt == NULL)
			return ENOMEM;

		mutex_lock(&lpfcdfc_lock);
		list_add(&evt->node, &dfchba->ev_waiters);
		lpfcdfc_event_ref(evt);
		mutex_unlock(&lpfcdfc_lock);
	}

	evt->waiting = 1;
	if (wait_event_interruptible(evt->wq,
				     (!list_empty(&evt->events_to_see) ||
					dfchba->blocked))) {
		mutex_lock(&lpfcdfc_lock);
		lpfcdfc_event_unref(evt); /* release ref */
		lpfcdfc_event_unref(evt); /* delete */
		mutex_unlock(&lpfcdfc_lock);
		return EINTR;
	}

	mutex_lock(&lpfcdfc_lock);
	if (dfchba->blocked) {
		lpfcdfc_event_unref(evt);
		lpfcdfc_event_unref(evt);
		mutex_unlock(&lpfcdfc_lock);
		return ENODEV;
	}
	mutex_unlock(&lpfcdfc_lock);

	evt->wait_time_stamp = jiffies;
	evt->waiting = 0;

	BUG_ON(list_empty(&evt->events_to_see));

	mutex_lock(&lpfcdfc_lock);
	list_move(evt->events_to_see.prev, &evt->events_to_get);
	lpfcdfc_event_unref(evt); /* release ref */
	mutex_unlock(&lpfcdfc_lock);

	return ret_val;
}

static int
lpfc_ioctl_loopback_mode(struct lpfc_hba *phba,
		   struct lpfcCmdInput  *cip, void *dataout)
{
	struct Scsi_Host *shost;
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring = &psli->ring[LPFC_FCP_RING];
	uint32_t link_flags = cip->lpfc_arg4;
	uint32_t timeout = cip->lpfc_arg5 * 100;
	struct lpfc_vport **vports;
	LPFC_MBOXQ_t *pmboxq;
	int mbxstatus;
	int i = 0;
	int rc = 0;

	if ((phba->link_state == LPFC_HBA_ERROR) ||
	    (psli->sli_flag & LPFC_BLOCK_MGMT_IO) ||
	    (!(psli->sli_flag & LPFC_SLI2_ACTIVE)))
		return EACCES;

	if ((pmboxq = mempool_alloc(phba->mbox_mem_pool,GFP_KERNEL)) == 0)
		return ENOMEM;

	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL) {
		for(i = 0; i <= phba->max_vpi && vports[i] != NULL; i++){
			shost = lpfc_shost_from_vport(vports[i]);
			scsi_block_requests(shost);
		}
		lpfc_destroy_vport_work_array(phba, vports);
	}
	else {
		shost = lpfc_shost_from_vport(phba->pport);
		scsi_block_requests(shost);
	}

	while (pring->txcmplq_cnt) {
		if (i++ > 500)	/* wait up to 5 seconds */
			break;

		mdelay(10);
	}

	memset((void *)pmboxq, 0, sizeof (LPFC_MBOXQ_t));
	pmboxq->mb.mbxCommand = MBX_DOWN_LINK;
	pmboxq->mb.mbxOwner = OWN_HOST;

	mbxstatus = lpfc_sli_issue_mbox_wait(phba, pmboxq, LPFC_MBOX_TMO);

	if ((mbxstatus == MBX_SUCCESS) && (pmboxq->mb.mbxStatus == 0)) {

		/* wait for link down before proceeding */
		i = 0;
		while (phba->link_state != LPFC_LINK_DOWN) {
			if (i++ > timeout) {
				rc = ETIMEDOUT;
				goto loopback_mode_exit;
			}
			msleep(10);
		}

		memset((void *)pmboxq, 0, sizeof (LPFC_MBOXQ_t));
		if (link_flags == INTERNAL_LOOP_BACK)
			pmboxq->mb.un.varInitLnk.link_flags = FLAGS_LOCAL_LB;
		else
			pmboxq->mb.un.varInitLnk.link_flags =
				FLAGS_TOPOLOGY_MODE_LOOP;

		pmboxq->mb.mbxCommand = MBX_INIT_LINK;
		pmboxq->mb.mbxOwner = OWN_HOST;

		mbxstatus = lpfc_sli_issue_mbox_wait(phba, pmboxq,
						     LPFC_MBOX_TMO);

		if ((mbxstatus != MBX_SUCCESS) || (pmboxq->mb.mbxStatus))
			rc = ENODEV;
		else {
			phba->link_flag |= LS_LOOPBACK_MODE;
			/* wait for the link attention interrupt */
			msleep(100);

			i = 0;
			while (phba->link_state != LPFC_HBA_READY) {
				if (i++ > timeout) {
					rc = ETIMEDOUT;
					break;
				}
				msleep(10);
			}
		}
	} else
		rc = ENODEV;

loopback_mode_exit:
	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL) {
		for(i = 0; i <= phba->max_vpi && vports[i] != NULL; i++){
			shost = lpfc_shost_from_vport(vports[i]);
			scsi_unblock_requests(shost);
		}
		lpfc_destroy_vport_work_array(phba, vports);
	}
	else {
		shost = lpfc_shost_from_vport(phba->pport);
		scsi_unblock_requests(shost);
	}

	/*
	 * Let SLI layer release mboxq if mbox command completed after timeout.
	 */
	if (mbxstatus != MBX_TIMEOUT)
		mempool_free( pmboxq, phba->mbox_mem_pool);

	return rc;
}

static int lpfcdfc_loop_self_reg(struct lpfc_hba *phba, uint16_t * rpi)
{
	LPFC_MBOXQ_t *mbox;
	struct lpfc_dmabuf *dmabuff;
	int status;

	mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (mbox == NULL)
		return ENOMEM;

	status = lpfc_reg_login(phba, 0, phba->pport->fc_myDID,
				(uint8_t *)&phba->pport->fc_sparam, mbox, 0);
	if (status) {
		mempool_free(mbox, phba->mbox_mem_pool);
		return ENOMEM;
	}

	dmabuff = (struct lpfc_dmabuf *) mbox->context1;
	mbox->context1 = NULL;
	status = lpfc_sli_issue_mbox_wait(phba, mbox, LPFC_MBOX_TMO);

	if ((status != MBX_SUCCESS) || (mbox->mb.mbxStatus)) {
		lpfc_mbuf_free(phba, dmabuff->virt, dmabuff->phys);
		kfree(dmabuff);
		if (status != MBX_TIMEOUT)
			mempool_free(mbox, phba->mbox_mem_pool);
		return ENODEV;
	}

	*rpi = mbox->mb.un.varWords[0];

	lpfc_mbuf_free(phba, dmabuff->virt, dmabuff->phys);
	kfree(dmabuff);
	mempool_free(mbox, phba->mbox_mem_pool);

	return 0;
}

static int lpfcdfc_loop_self_unreg(struct lpfc_hba *phba, uint16_t rpi)
{
	LPFC_MBOXQ_t * mbox;
	int status;

	/* Allocate mboxq structure */
	mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (mbox == NULL)
		return ENOMEM;

	lpfc_unreg_login(phba, 0, rpi, mbox);
	status = lpfc_sli_issue_mbox_wait(phba, mbox, LPFC_MBOX_TMO);

	if ((status != MBX_SUCCESS) || (mbox->mb.mbxStatus)) {
		if (status != MBX_TIMEOUT)
			mempool_free(mbox, phba->mbox_mem_pool);
		return EIO;
	}

	mempool_free(mbox, phba->mbox_mem_pool);
	return 0;
}


static int lpfcdfc_loop_get_xri(struct lpfc_hba *phba, uint16_t rpi,
			 uint16_t *txxri, uint16_t * rxxri)
{
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring = &psli->ring[LPFC_ELS_RING];

	struct lpfcdfc_host * dfchba;
	struct lpfcdfc_event * evt;

	struct lpfc_iocbq *cmdiocbq, *rspiocbq;
	IOCB_t *cmd, *rsp;

	struct lpfc_dmabuf * dmabuf;
	struct ulp_bde64 *bpl = NULL;
	struct lpfc_sli_ct_request *ctreq = NULL;

	int ret_val = 0;

	*txxri = 0;
	*rxxri = 0;

	mutex_lock(&lpfcdfc_lock);
	list_for_each_entry(dfchba, &lpfcdfc_hosts, node) {
		if (dfchba->phba == phba)
			break;
	}
	mutex_unlock(&lpfcdfc_lock);
	BUG_ON(&dfchba->node == &lpfcdfc_hosts);

	evt = lpfcdfc_event_new(FC_REG_CT_EVENT, current->pid,
				SLI_CT_ELX_LOOPBACK);
	if (evt == NULL)
		return ENOMEM;

	mutex_lock(&lpfcdfc_lock);
	list_add(&evt->node, &dfchba->ev_waiters);
	lpfcdfc_event_ref(evt);
	mutex_unlock(&lpfcdfc_lock);

	cmdiocbq = lpfc_sli_get_iocbq(phba);
	rspiocbq = lpfc_sli_get_iocbq(phba);

	dmabuf = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);
	if (dmabuf) {
		dmabuf->virt = lpfc_mbuf_alloc(phba, 0, &dmabuf->phys);
		INIT_LIST_HEAD(&dmabuf->list);
		bpl = (struct ulp_bde64 *) dmabuf->virt;
		memset(bpl, 0, sizeof(*bpl));
		ctreq = (struct lpfc_sli_ct_request *)(bpl + 1);
		bpl->addrHigh =
			le32_to_cpu(putPaddrHigh(dmabuf->phys + sizeof(*bpl)));
		bpl->addrLow =
			le32_to_cpu(putPaddrLow(dmabuf->phys + sizeof(*bpl)));
		bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64;
		bpl->tus.f.bdeSize = ELX_LOOPBACK_HEADER_SZ;
		bpl->tus.w = le32_to_cpu(bpl->tus.w);
	}

	if (cmdiocbq == NULL || rspiocbq == NULL ||
	    dmabuf == NULL || bpl == NULL || ctreq == NULL) {
		ret_val = ENOMEM;
		goto err_get_xri_exit;
	}

	cmd = &cmdiocbq->iocb;
	rsp = &rspiocbq->iocb;

	memset(ctreq, 0, ELX_LOOPBACK_HEADER_SZ);

	ctreq->RevisionId.bits.Revision = SLI_CT_REVISION;
	ctreq->RevisionId.bits.InId = 0;
	ctreq->FsType = SLI_CT_ELX_LOOPBACK;
	ctreq->FsSubType = 0;
	ctreq->CommandResponse.bits.CmdRsp = ELX_LOOPBACK_XRI_SETUP;
	ctreq->CommandResponse.bits.Size = 0;


	cmd->un.xseq64.bdl.addrHigh = putPaddrHigh(dmabuf->phys);
	cmd->un.xseq64.bdl.addrLow = putPaddrLow(dmabuf->phys);
	cmd->un.xseq64.bdl.bdeFlags = BUFF_TYPE_BLP_64;
	cmd->un.xseq64.bdl.bdeSize = sizeof(*bpl);

	cmd->un.xseq64.w5.hcsw.Fctl = LA;
	cmd->un.xseq64.w5.hcsw.Dfctl = 0;
	cmd->un.xseq64.w5.hcsw.Rctl = FC_UNSOL_CTL;
	cmd->un.xseq64.w5.hcsw.Type = FC_COMMON_TRANSPORT_ULP;

	cmd->ulpCommand = CMD_XMIT_SEQUENCE64_CR;
	cmd->ulpBdeCount = 1;
	cmd->ulpLe = 1;
	cmd->ulpClass = CLASS3;
	cmd->ulpContext = rpi;

	cmdiocbq->iocb_flag |= LPFC_IO_LIBDFC;
	cmdiocbq->vport = phba->pport;

	ret_val = lpfc_sli_issue_iocb_wait(phba, pring, cmdiocbq, rspiocbq,
					   (phba->fc_ratov * 2)
					   + LPFC_DRVR_TIMEOUT);
	if (ret_val)
		goto err_get_xri_exit;

	*txxri =  rsp->ulpContext;

	evt->waiting = 1;
	evt->wait_time_stamp = jiffies;
	ret_val = wait_event_interruptible_timeout(
		evt->wq, !list_empty(&evt->events_to_see),
		((phba->fc_ratov * 2) + LPFC_DRVR_TIMEOUT) * HZ);
	if (list_empty(&evt->events_to_see))
		ret_val = (ret_val) ? EINTR : ETIMEDOUT;
	else {
		ret_val = IOCB_SUCCESS;
		mutex_lock(&lpfcdfc_lock);
		list_move(evt->events_to_see.prev, &evt->events_to_get);
		mutex_unlock(&lpfcdfc_lock);
		*rxxri = (list_entry(evt->events_to_get.prev,
				     typeof(struct event_data),
				     node))->immed_dat;
	}
	evt->waiting = 0;

err_get_xri_exit:
	mutex_lock(&lpfcdfc_lock);
	lpfcdfc_event_unref(evt); /* release ref */
	lpfcdfc_event_unref(evt); /* delete */
	mutex_unlock(&lpfcdfc_lock);

	if(dmabuf) {
		if(dmabuf->virt)
			lpfc_mbuf_free(phba, dmabuf->virt, dmabuf->phys);
		kfree(dmabuf);
	}

	if (cmdiocbq && (ret_val != IOCB_TIMEDOUT))
		lpfc_sli_release_iocbq(phba, cmdiocbq);
	if (rspiocbq)
		lpfc_sli_release_iocbq(phba, rspiocbq);

	return ret_val;
}

static int lpfcdfc_loop_post_rxbufs(struct lpfc_hba *phba, uint16_t rxxri,
			     size_t len)
{
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring = &psli->ring[LPFC_ELS_RING];
	struct lpfc_iocbq *cmdiocbq;
	IOCB_t *cmd = NULL;
	struct list_head head, *curr, *next;
	struct lpfc_dmabuf *rxbmp;
	struct lpfc_dmabuf *dmp;
	struct lpfc_dmabuf *mp[2] = {NULL, NULL};
	struct ulp_bde64 *rxbpl = NULL;
	uint32_t num_bde;
	struct lpfc_dmabufext *rxbuffer = NULL;
	int ret_val = 0;
	int i = 0;

	cmdiocbq = lpfc_sli_get_iocbq(phba);
	rxbmp = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);
	if (rxbmp != NULL) {
		rxbmp->virt = lpfc_mbuf_alloc(phba, 0, &rxbmp->phys);
		INIT_LIST_HEAD(&rxbmp->list);
		rxbpl = (struct ulp_bde64 *) rxbmp->virt;
		rxbuffer = dfc_cmd_data_alloc(phba, NULL, rxbpl, len);
	}

	if(cmdiocbq == NULL || rxbmp == NULL ||
	   rxbpl == NULL || rxbuffer == NULL) {
		ret_val = ENOMEM;
		goto err_post_rxbufs_exit;
	}

	/* Queue buffers for the receive exchange */
	num_bde = (uint32_t)rxbuffer->flag;
	dmp = &rxbuffer->dma;

	cmd = &cmdiocbq->iocb;
	i = 0;

	INIT_LIST_HEAD(&head);
	list_add_tail(&head, &dmp->list);
	list_for_each_safe(curr, next, &head) {
		mp[i] = list_entry(curr, struct lpfc_dmabuf, list);
		list_del(curr);

		if (phba->sli3_options & LPFC_SLI3_HBQ_ENABLED) {
			mp[i]->buffer_tag = lpfc_sli_get_buffer_tag(phba);
			cmd->un.quexri64cx.buff.bde.addrHigh =
				putPaddrHigh(mp[i]->phys);
			cmd->un.quexri64cx.buff.bde.addrLow =
				putPaddrLow(mp[i]->phys);
			cmd->un.quexri64cx.buff.bde.tus.f.bdeSize =
				((struct lpfc_dmabufext *)mp[i])->size;
			cmd->un.quexri64cx.buff.buffer_tag = mp[i]->buffer_tag;
			cmd->ulpCommand = CMD_QUE_XRI64_CX;
			cmd->ulpPU = 0;
			cmd->ulpLe = 1;
			cmd->ulpBdeCount = 1;
			cmd->unsli3.que_xri64cx_ext_words.ebde_count = 0;

		} else {
			cmd->un.cont64[i].addrHigh = putPaddrHigh(mp[i]->phys);
			cmd->un.cont64[i].addrLow = putPaddrLow(mp[i]->phys);
			cmd->un.cont64[i].tus.f.bdeSize =
				((struct lpfc_dmabufext *)mp[i])->size;
					cmd->ulpBdeCount = ++i;

			if ((--num_bde > 0) && (i < 2))
				continue;

			cmd->ulpCommand = CMD_QUE_XRI_BUF64_CX;
			cmd->ulpLe = 1;
		}

		cmd->ulpClass = CLASS3;
		cmd->ulpContext = rxxri;

		ret_val = lpfc_sli_issue_iocb(phba, pring, cmdiocbq, 0);

		if (ret_val == IOCB_ERROR) {
			dfc_cmd_data_free(phba, (struct lpfc_dmabufext *)mp[0]);
			if (mp[1])
				dfc_cmd_data_free(phba,
					  (struct lpfc_dmabufext *)mp[1]);
			dmp = list_entry(next, struct lpfc_dmabuf, list);
			ret_val = EIO;
			goto err_post_rxbufs_exit;
		}

		lpfc_sli_ringpostbuf_put(phba, pring, mp[0]);
		if (mp[1]) {
			lpfc_sli_ringpostbuf_put(phba, pring, mp[1]);
			mp[1] = NULL;
		}

		/* The iocb was freed by lpfc_sli_issue_iocb */
		if ((cmdiocbq = lpfc_sli_get_iocbq(phba)) == NULL) {
			dmp = list_entry(next, struct lpfc_dmabuf, list);
			ret_val = EIO;
			goto err_post_rxbufs_exit;
		}
		cmd = &cmdiocbq->iocb;
		i = 0;
	}
	list_del(&head);

err_post_rxbufs_exit:

	if(rxbmp) {
		if(rxbmp->virt)
			lpfc_mbuf_free(phba, rxbmp->virt, rxbmp->phys);
		kfree(rxbmp);
	}

	if (cmdiocbq)
		lpfc_sli_release_iocbq(phba, cmdiocbq);

	return ret_val;
}
static int
lpfc_ioctl_loopback_test(struct lpfc_hba *phba,
		   struct lpfcCmdInput  *cip, void *dataout)
{
	struct lpfcdfc_host * dfchba;
	struct lpfcdfc_event * evt;
	struct event_data * evdat;

	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring = &psli->ring[LPFC_ELS_RING];
	uint32_t size = cip->lpfc_outsz;
	uint32_t full_size = size + ELX_LOOPBACK_HEADER_SZ;
	size_t segment_len = 0, segment_offset = 0, current_offset = 0;
	uint16_t rpi;
	struct lpfc_iocbq *cmdiocbq, *rspiocbq;
	IOCB_t *cmd, *rsp;
	struct lpfc_sli_ct_request *ctreq;
	struct lpfc_dmabuf *txbmp;
	struct ulp_bde64 *txbpl = NULL;
	struct lpfc_dmabufext *txbuffer = NULL;
	struct list_head head;
	struct lpfc_dmabuf  *curr;
	uint16_t txxri, rxxri;
	uint32_t num_bde;
	uint8_t *ptr = NULL, *rx_databuf = NULL;
	int rc;

	if ((phba->link_state == LPFC_HBA_ERROR) ||
	    (psli->sli_flag & LPFC_BLOCK_MGMT_IO) ||
	    (!(psli->sli_flag & LPFC_SLI2_ACTIVE)))
		return EACCES;

	if (!lpfc_is_link_up(phba) || !(phba->link_flag & LS_LOOPBACK_MODE))
		return EACCES;

	if ((size == 0) || (size > 80 * BUF_SZ_4K))
		return  ERANGE;

	mutex_lock(&lpfcdfc_lock);
	list_for_each_entry(dfchba, &lpfcdfc_hosts, node) {
		if (dfchba->phba == phba)
			break;
	}
	mutex_unlock(&lpfcdfc_lock);
	BUG_ON(&dfchba->node == &lpfcdfc_hosts);

	rc = lpfcdfc_loop_self_reg(phba, &rpi);
	if (rc)
		return rc;

	rc = lpfcdfc_loop_get_xri(phba, rpi, &txxri, &rxxri);
	if (rc) {
		lpfcdfc_loop_self_unreg(phba, rpi);
		return rc;
	}

	rc = lpfcdfc_loop_post_rxbufs(phba, rxxri, full_size);
	if (rc) {
		lpfcdfc_loop_self_unreg(phba, rpi);
		return rc;
	}

	evt = lpfcdfc_event_new(FC_REG_CT_EVENT, current->pid,
				SLI_CT_ELX_LOOPBACK);
	if (evt == NULL) {
		lpfcdfc_loop_self_unreg(phba, rpi);
		return ENOMEM;
	}

	mutex_lock(&lpfcdfc_lock);
	list_add(&evt->node, &dfchba->ev_waiters);
	lpfcdfc_event_ref(evt);
	mutex_unlock(&lpfcdfc_lock);

	cmdiocbq = lpfc_sli_get_iocbq(phba);
	rspiocbq = lpfc_sli_get_iocbq(phba);
	txbmp = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);

	if (txbmp) {
		txbmp->virt = lpfc_mbuf_alloc(phba, 0, &txbmp->phys);
		INIT_LIST_HEAD(&txbmp->list);
		txbpl = (struct ulp_bde64 *) txbmp->virt;
		if (txbpl)
			txbuffer = dfc_cmd_data_alloc(phba, NULL,
						      txbpl, full_size);
	}

	if (cmdiocbq == NULL || rspiocbq == NULL
	    || txbmp == NULL || txbpl == NULL || txbuffer == NULL) {
		rc = ENOMEM;
		goto err_loopback_test_exit;
	}

	cmd = &cmdiocbq->iocb;
	rsp = &rspiocbq->iocb;

	INIT_LIST_HEAD(&head);
	list_add_tail(&head, &txbuffer->dma.list);
	list_for_each_entry(curr, &head, list) {
		segment_len = ((struct lpfc_dmabufext *)curr)->size;
		if (current_offset == 0) {
			ctreq = curr->virt;
			memset(ctreq, 0, ELX_LOOPBACK_HEADER_SZ);
			ctreq->RevisionId.bits.Revision = SLI_CT_REVISION;
			ctreq->RevisionId.bits.InId = 0;
			ctreq->FsType = SLI_CT_ELX_LOOPBACK;
			ctreq->FsSubType = 0;
			ctreq->CommandResponse.bits.CmdRsp = ELX_LOOPBACK_DATA ;
			ctreq->CommandResponse.bits.Size   = size;
			segment_offset = ELX_LOOPBACK_HEADER_SZ;
		} else
			segment_offset = 0;

		BUG_ON(segment_offset >= segment_len);
		if (copy_from_user (curr->virt + segment_offset,
				    (void __user *)cip->lpfc_arg1
				    + current_offset,
				    segment_len - segment_offset)) {
			rc = EIO;
			list_del(&head);
			goto err_loopback_test_exit;
		}

		current_offset += segment_len - segment_offset;
		BUG_ON(current_offset > size);
	}
	list_del(&head);

	/* Build the XMIT_SEQUENCE iocb */

	num_bde = (uint32_t)txbuffer->flag;

	cmd->un.xseq64.bdl.addrHigh = putPaddrHigh(txbmp->phys);
	cmd->un.xseq64.bdl.addrLow = putPaddrLow(txbmp->phys);
	cmd->un.xseq64.bdl.bdeFlags = BUFF_TYPE_BLP_64;
	cmd->un.xseq64.bdl.bdeSize = (num_bde * sizeof(struct ulp_bde64));

	cmd->un.xseq64.w5.hcsw.Fctl = (LS | LA);
	cmd->un.xseq64.w5.hcsw.Dfctl = 0;
	cmd->un.xseq64.w5.hcsw.Rctl = FC_UNSOL_CTL;
	cmd->un.xseq64.w5.hcsw.Type = FC_COMMON_TRANSPORT_ULP;

	cmd->ulpCommand = CMD_XMIT_SEQUENCE64_CX;
	cmd->ulpBdeCount = 1;
	cmd->ulpLe = 1;
	cmd->ulpClass = CLASS3;
	cmd->ulpContext = txxri;

	cmdiocbq->iocb_flag |= LPFC_IO_LIBDFC;
	cmdiocbq->vport = phba->pport;

	rc = lpfc_sli_issue_iocb_wait(phba, pring, cmdiocbq, rspiocbq,
				      (phba->fc_ratov * 2) + LPFC_DRVR_TIMEOUT);

	if ((rc != IOCB_SUCCESS) || (rsp->ulpStatus != IOCB_SUCCESS)) {
		rc = EIO;
		goto err_loopback_test_exit;
	}

	evt->waiting = 1;
	rc = wait_event_interruptible_timeout(
		evt->wq, !list_empty(&evt->events_to_see),
		((phba->fc_ratov * 2) + LPFC_DRVR_TIMEOUT) * HZ);
	evt->waiting = 0;
	if (list_empty(&evt->events_to_see))
		rc = (rc) ? EINTR : ETIMEDOUT;
	else {
		ptr = dataout;
		mutex_lock(&lpfcdfc_lock);
		list_move(evt->events_to_see.prev, &evt->events_to_get);
		evdat = list_entry(evt->events_to_get.prev,
				   typeof(*evdat), node);
		mutex_unlock(&lpfcdfc_lock);
		rx_databuf = evdat->data;
		if (evdat->len != full_size) {
			lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
				"1603 Loopback test did not receive expected "
				"data length. actual length 0x%x expected "
				"length 0x%x\n",
				evdat->len, full_size);
			rc = EIO;
		}
		else if (rx_databuf == NULL)
			rc = EIO;
		else {
			rx_databuf += ELX_LOOPBACK_HEADER_SZ;
			memcpy(ptr, rx_databuf, size);
			rc = IOCB_SUCCESS;
		}
	}

err_loopback_test_exit:
	lpfcdfc_loop_self_unreg(phba, rpi);

	mutex_lock(&lpfcdfc_lock);
	lpfcdfc_event_unref(evt); /* release ref */
	lpfcdfc_event_unref(evt); /* delete */
	mutex_unlock(&lpfcdfc_lock);

	if (cmdiocbq != NULL)
		lpfc_sli_release_iocbq(phba, cmdiocbq);

	if (rspiocbq != NULL)
		lpfc_sli_release_iocbq(phba, rspiocbq);

	if (txbmp != NULL) {
		if (txbpl != NULL) {
			if (txbuffer != NULL)
				dfc_cmd_data_free(phba, txbuffer);
			lpfc_mbuf_free(phba, txbmp->virt, txbmp->phys);
		}
		kfree(txbmp);
	}
	return rc;
}

static int
dfc_rsp_data_copy(struct lpfc_hba * phba,
		  uint8_t * outdataptr, struct lpfc_dmabufext * mlist,
		  uint32_t size)
{
	struct lpfc_dmabufext *mlast = NULL;
	int cnt, offset = 0;
	struct list_head head, *curr, *next;

	if (!mlist)
		return 0;

	list_add_tail(&head, &mlist->dma.list);

	list_for_each_safe(curr, next, &head) {
		mlast = list_entry(curr, struct lpfc_dmabufext , dma.list);
		if (!size)
			break;

		/* We copy chucks of 4K */
		if (size > BUF_SZ_4K)
			cnt = BUF_SZ_4K;
		else
			cnt = size;

		if (outdataptr) {
			pci_dma_sync_single_for_device(phba->pcidev,
			    mlast->dma.phys, LPFC_BPL_SIZE, PCI_DMA_TODEVICE);

			/* Copy data to user space */
			if (copy_to_user
			    ((void __user *) (outdataptr + offset),
			     (uint8_t *) mlast->dma.virt, cnt))
				return 1;
		}
		offset += cnt;
		size -= cnt;
	}
	list_del(&head);
	return 0;
}

static int
lpfc_issue_ct_rsp(struct lpfc_hba * phba, uint32_t tag,
		  struct lpfc_dmabuf * bmp,
		  struct lpfc_dmabufext * inp)
{
	struct lpfc_sli *psli;
	IOCB_t *icmd;
	struct lpfc_iocbq *ctiocb;
	struct lpfc_sli_ring *pring;
	uint32_t num_entry;
	int rc = 0;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];
	num_entry = inp->flag;
	inp->flag = 0;

	/* Allocate buffer for  command iocb */
	ctiocb = lpfc_sli_get_iocbq(phba);
	if (!ctiocb) {
		rc = ENOMEM;
		goto issue_ct_rsp_exit;
	}
	icmd = &ctiocb->iocb;

	icmd->un.xseq64.bdl.ulpIoTag32 = 0;
	icmd->un.xseq64.bdl.addrHigh = putPaddrHigh(bmp->phys);
	icmd->un.xseq64.bdl.addrLow = putPaddrLow(bmp->phys);
	icmd->un.xseq64.bdl.bdeFlags = BUFF_TYPE_BLP_64;
	icmd->un.xseq64.bdl.bdeSize = (num_entry * sizeof (struct ulp_bde64));
	icmd->un.xseq64.w5.hcsw.Fctl = (LS | LA);
	icmd->un.xseq64.w5.hcsw.Dfctl = 0;
	icmd->un.xseq64.w5.hcsw.Rctl = FC_SOL_CTL;
	icmd->un.xseq64.w5.hcsw.Type = FC_COMMON_TRANSPORT_ULP;

	pci_dma_sync_single_for_device(phba->pcidev, bmp->phys, LPFC_BPL_SIZE,
							PCI_DMA_TODEVICE);

	/* Fill in rest of iocb */
	icmd->ulpCommand = CMD_XMIT_SEQUENCE64_CX;
	icmd->ulpBdeCount = 1;
	icmd->ulpLe = 1;
	icmd->ulpClass = CLASS3;
	icmd->ulpContext = (ushort) tag;
	icmd->ulpTimeout = phba->fc_ratov * 2;

	/* Xmit CT response on exchange <xid> */
	lpfc_printf_log(phba, KERN_INFO, LOG_ELS,
			"1200 Xmit CT response on exchange x%x Data: x%x x%x\n",
			icmd->ulpContext, icmd->ulpIoTag, phba->link_state);

	ctiocb->iocb_cmpl = NULL;
	ctiocb->iocb_flag |= LPFC_IO_LIBDFC;
	ctiocb->vport = phba->pport;
	rc = lpfc_sli_issue_iocb_wait(phba, pring, ctiocb, NULL,
				     phba->fc_ratov * 2 + LPFC_DRVR_TIMEOUT);

	if (rc == IOCB_TIMEDOUT) {
		ctiocb->context1 = NULL;
		ctiocb->context2 = NULL;
		ctiocb->iocb_cmpl = lpfc_ioctl_timeout_iocb_cmpl;
		return rc;
	}

	/* Calling routine takes care of IOCB_ERROR => EIO translation */
	if (rc != IOCB_SUCCESS)
		rc = IOCB_ERROR;

	lpfc_sli_release_iocbq(phba, ctiocb);
issue_ct_rsp_exit:
	return rc;
}


static void
lpfcdfc_ct_unsol_event(struct lpfc_hba * phba,
			    struct lpfc_sli_ring * pring,
			    struct lpfc_iocbq * piocbq)
{
	struct lpfcdfc_host * dfchba = lpfcdfc_host_from_hba(phba);
	uint32_t evt_req_id = 0;
	uint32_t cmd;
	uint32_t len;
	struct lpfc_dmabuf *dmabuf = NULL;
	struct lpfcdfc_event * evt;
	struct event_data * evt_dat = NULL;
	struct lpfc_iocbq * iocbq;
	size_t offset = 0;
	struct list_head head;
	struct ulp_bde64 * bde;
	dma_addr_t dma_addr;
	int i;
	struct lpfc_dmabuf *bdeBuf1 = piocbq->context2;
	struct lpfc_dmabuf *bdeBuf2 = piocbq->context3;
	struct lpfc_hbq_entry *hbqe;

	BUG_ON(&dfchba->node == &lpfcdfc_hosts);
	INIT_LIST_HEAD(&head);
	if (piocbq->iocb.ulpBdeCount == 0 ||
	    piocbq->iocb.un.cont64[0].tus.f.bdeSize == 0)
		goto error_unsol_ct_exit;

	if (phba->sli3_options & LPFC_SLI3_HBQ_ENABLED)
		dmabuf = bdeBuf1;
	else {
		dma_addr = getPaddr(piocbq->iocb.un.cont64[0].addrHigh,
				    piocbq->iocb.un.cont64[0].addrLow);
		dmabuf = lpfc_sli_ringpostbuf_get(phba, pring, dma_addr);
	}
	BUG_ON(dmabuf == NULL);
	evt_req_id = ((struct lpfc_sli_ct_request *)(dmabuf->virt))->FsType;
	cmd = ((struct lpfc_sli_ct_request *)
			(dmabuf->virt))->CommandResponse.bits.CmdRsp;
	len = ((struct lpfc_sli_ct_request *)
			(dmabuf->virt))->CommandResponse.bits.Size;
	if (!(phba->sli3_options & LPFC_SLI3_HBQ_ENABLED))
		lpfc_sli_ringpostbuf_put(phba, pring, dmabuf);

	mutex_lock(&lpfcdfc_lock);
	list_for_each_entry(evt, &dfchba->ev_waiters, node) {
		if (!(evt->type_mask & FC_REG_CT_EVENT) ||
		    evt->req_id != evt_req_id)
			continue;

		lpfcdfc_event_ref(evt);

		if ((evt_dat = kzalloc(sizeof(*evt_dat), GFP_KERNEL)) == NULL) {
			lpfcdfc_event_unref(evt);
			break;
		}

		mutex_unlock(&lpfcdfc_lock);

		INIT_LIST_HEAD(&head);
		list_add_tail(&head, &piocbq->list);
		if (phba->sli3_options & LPFC_SLI3_HBQ_ENABLED) {
			/* take accumulated byte count from the last iocbq */
			iocbq = list_entry(head.prev, typeof(*iocbq), list);
			evt_dat->len = iocbq->iocb.unsli3.rcvsli3.acc_len;
		} else {
			list_for_each_entry(iocbq, &head, list) {
				for (i = 0; i < iocbq->iocb.ulpBdeCount; i++)
					evt_dat->len +=
					iocbq->iocb.un.cont64[i].tus.f.bdeSize;
			}
		}


		evt_dat->data = kzalloc(evt_dat->len, GFP_KERNEL);
		if (evt_dat->data == NULL) {
			kfree (evt_dat);
			mutex_lock(&lpfcdfc_lock);
			lpfcdfc_event_unref(evt);
			mutex_unlock(&lpfcdfc_lock);
			goto error_unsol_ct_exit;
		}

		list_for_each_entry(iocbq, &head, list) {
			if (phba->sli3_options & LPFC_SLI3_HBQ_ENABLED) {
				bdeBuf1 = iocbq->context2;
				bdeBuf2 = iocbq->context3;
			}
			for (i = 0; i < iocbq->iocb.ulpBdeCount; i++) {
				int size = 0;
				if (phba->sli3_options &
				    LPFC_SLI3_HBQ_ENABLED) {
					BUG_ON(i>1);
					if (i == 0) {
						hbqe = (struct lpfc_hbq_entry *)
						  &iocbq->iocb.un.ulpWord[0];
						size = hbqe->bde.tus.f.bdeSize;
						dmabuf = bdeBuf1;
					} else if (i == 1) {
						hbqe = (struct lpfc_hbq_entry *)
							&iocbq->iocb.unsli3.
							sli3Words[4];
						size = hbqe->bde.tus.f.bdeSize;
						dmabuf = bdeBuf2;
					}
					if ((offset + size) > evt_dat->len)
						size = evt_dat->len - offset;
				} else {
					size = iocbq->iocb.un.cont64[i].
						tus.f.bdeSize;
					bde = &iocbq->iocb.un.cont64[i];
					dma_addr = getPaddr(bde->addrHigh,
							    bde->addrLow);
					dmabuf = lpfc_sli_ringpostbuf_get(phba,
							pring, dma_addr);
				}
				if (dmabuf == NULL) {
					kfree (evt_dat->data);
					kfree (evt_dat);
					mutex_lock(&lpfcdfc_lock);
					lpfcdfc_event_unref(evt);
					mutex_unlock(&lpfcdfc_lock);
					goto error_unsol_ct_exit;
				}
				memcpy ((char *)(evt_dat->data) + offset,
					dmabuf->virt, size);
				offset += size;
				if (evt_req_id != SLI_CT_ELX_LOOPBACK &&
				    !(phba->sli3_options &
				      LPFC_SLI3_HBQ_ENABLED))
					lpfc_sli_ringpostbuf_put(phba, pring,
								 dmabuf);
				else {
					switch (cmd) {
					case ELX_LOOPBACK_DATA:
						dfc_cmd_data_free(phba,
						(struct lpfc_dmabufext *)
							dmabuf);
						break;
					case ELX_LOOPBACK_XRI_SETUP:
						if (!(phba->sli3_options &
						      LPFC_SLI3_HBQ_ENABLED))
							lpfc_post_buffer(phba,
									 pring,
									 1);
						else
							lpfc_in_buf_free(phba,
									dmabuf);
						break;
					default:
						if (!(phba->sli3_options &
						      LPFC_SLI3_HBQ_ENABLED))
							lpfc_post_buffer(phba,
									 pring,
									 1);
						break;
					}
				}
			}
		}

		mutex_lock(&lpfcdfc_lock);
		evt_dat->immed_dat = piocbq->iocb.ulpContext;
		evt_dat->type = FC_REG_CT_EVENT;
		list_add(&evt_dat->node, &evt->events_to_see);
		wake_up_interruptible(&evt->wq);
		lpfcdfc_event_unref(evt);
		if (evt_req_id == SLI_CT_ELX_LOOPBACK)
			break;
	}
	mutex_unlock(&lpfcdfc_lock);

error_unsol_ct_exit:
	if(!list_empty(&head))
		list_del(&head);
	if (evt_req_id != SLI_CT_ELX_LOOPBACK &&
	    dfchba->base_ct_unsol_event != NULL)
		(dfchba->base_ct_unsol_event)(phba, pring, piocbq);

	return;
}


struct lpfc_dmabufext *
__dfc_cmd_data_alloc(struct lpfc_hba * phba,
		   char *indataptr, struct ulp_bde64 * bpl, uint32_t size,
		   int nocopydata)
{
	struct lpfc_dmabufext *mlist = NULL;
	struct lpfc_dmabufext *dmp;
	int cnt, offset = 0, i = 0;
	struct pci_dev *pcidev;

	pcidev = phba->pcidev;

	while (size) {
		/* We get chunks of 4K */
		if (size > BUF_SZ_4K)
			cnt = BUF_SZ_4K;
		else
			cnt = size;

		/* allocate struct lpfc_dmabufext buffer header */
		dmp = kmalloc(sizeof (struct lpfc_dmabufext), GFP_KERNEL);
		if (dmp == 0)
			goto out;

		INIT_LIST_HEAD(&dmp->dma.list);

		/* Queue it to a linked list */
		if (mlist)
			list_add_tail(&dmp->dma.list, &mlist->dma.list);
		else
			mlist = dmp;

		/* allocate buffer */
		dmp->dma.virt = dma_alloc_coherent(&pcidev->dev,
						   cnt,
						   &(dmp->dma.phys),
						   GFP_KERNEL);

		if (dmp->dma.virt == NULL)
			goto out;

		dmp->size = cnt;

		if (indataptr || nocopydata) {
			if (indataptr)
				/* Copy data from user space in */
				if (copy_from_user ((uint8_t *) dmp->dma.virt,
					(void __user *) (indataptr + offset),
					cnt)) {
					goto out;
				}
			bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64;
			pci_dma_sync_single_for_device(phba->pcidev,
			        dmp->dma.phys, LPFC_BPL_SIZE, PCI_DMA_TODEVICE);

		} else {
			memset((uint8_t *)dmp->dma.virt, 0, cnt);
			bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64I;
		}
		/* build buffer ptr list for IOCB */
		bpl->addrLow = le32_to_cpu(putPaddrLow(dmp->dma.phys));
		bpl->addrHigh = le32_to_cpu(putPaddrHigh(dmp->dma.phys));
		bpl->tus.f.bdeSize = (ushort) cnt;
		bpl->tus.w = le32_to_cpu(bpl->tus.w);
		bpl++;

		i++;
		offset += cnt;
		size -= cnt;
	}

	mlist->flag = i;
	return mlist;
out:
	dfc_cmd_data_free(phba, mlist);
	return NULL;
}

static struct lpfc_dmabufext *
dfc_cmd_data_alloc(struct lpfc_hba * phba,
		   char *indataptr, struct ulp_bde64 * bpl, uint32_t size)
{
	/* if indataptr is null it is a rsp buffer. */
	return __dfc_cmd_data_alloc(phba, indataptr, bpl, size,
					0 /* don't copy user data */);
}

int
__dfc_cmd_data_free(struct lpfc_hba * phba, struct lpfc_dmabufext * mlist)
{
	return dfc_cmd_data_free(phba, mlist);
}
static int
dfc_cmd_data_free(struct lpfc_hba * phba, struct lpfc_dmabufext * mlist)
{
	struct lpfc_dmabufext *mlast;
	struct pci_dev *pcidev;
	struct list_head head, *curr, *next;

	if ((!mlist) || (!lpfc_is_link_up(phba) &&
		(phba->link_flag & LS_LOOPBACK_MODE))) {
		return 0;
	}

	pcidev = phba->pcidev;
	list_add_tail(&head, &mlist->dma.list);

	list_for_each_safe(curr, next, &head) {
		mlast = list_entry(curr, struct lpfc_dmabufext , dma.list);
		if (mlast->dma.virt)
			dma_free_coherent(&pcidev->dev,
					  mlast->size,
					  mlast->dma.virt,
					  mlast->dma.phys);
		kfree(mlast);
	}
	return 0;
}


/* The only reason we need that reverce find, is because we
 * are bent on keeping original calling conventions.
 */
static struct lpfcdfc_host *
lpfcdfc_host_from_hba(struct lpfc_hba * phba)
{
	struct lpfcdfc_host * dfchba;

	mutex_lock(&lpfcdfc_lock);
	list_for_each_entry(dfchba, &lpfcdfc_hosts, node) {
		if (dfchba->phba == phba)
			break;
	}
	mutex_unlock(&lpfcdfc_lock);

	return dfchba;
}

struct lpfcdfc_host *
lpfcdfc_host_add (struct pci_dev * dev,
		  struct Scsi_Host * host,
		  struct lpfc_hba * phba)
{
	struct lpfcdfc_host * dfchba = NULL;
	struct lpfc_sli_ring_mask * prt = NULL;

	dfchba = kzalloc(sizeof(*dfchba), GFP_KERNEL);
	if (dfchba == NULL)
		return NULL;

	dfchba->inst = phba->brd_no;
	dfchba->phba = phba;
	dfchba->vport = phba->pport;
	dfchba->host = host;
	dfchba->dev = dev;
	dfchba->blocked = 0;

	spin_lock_irq(&phba->hbalock);
	prt = phba->sli.ring[LPFC_ELS_RING].prt;
	dfchba->base_ct_unsol_event = prt[2].lpfc_sli_rcv_unsol_event;
	prt[2].lpfc_sli_rcv_unsol_event = lpfcdfc_ct_unsol_event;
	prt[3].lpfc_sli_rcv_unsol_event = lpfcdfc_ct_unsol_event;
	spin_unlock_irq(&phba->hbalock);
	mutex_lock(&lpfcdfc_lock);
	list_add_tail(&dfchba->node, &lpfcdfc_hosts);
	INIT_LIST_HEAD(&dfchba->ev_waiters);
	mutex_unlock(&lpfcdfc_lock);

	return dfchba;
}


void
lpfcdfc_host_del (struct lpfcdfc_host *  dfchba)
{
	struct Scsi_Host * host;
	struct lpfc_hba * phba = NULL;
	struct lpfc_sli_ring_mask * prt = NULL;
	struct lpfcdfc_event * evt;

	mutex_lock(&lpfcdfc_lock);
	dfchba->blocked = 1;

	list_for_each_entry(evt, &dfchba->ev_waiters, node) {
		wake_up_interruptible(&evt->wq);
	}

	while (dfchba->ref_count) {
		mutex_unlock(&lpfcdfc_lock);
		msleep(2000);
		mutex_lock(&lpfcdfc_lock);
	}

	if (dfchba->dev->driver) {
		host = pci_get_drvdata(dfchba->dev);
		if ((host != NULL) &&
		    (struct lpfc_vport *)host->hostdata == dfchba->vport) {
			phba = dfchba->phba;
			mutex_unlock(&lpfcdfc_lock);
			spin_lock_irq(&phba->hbalock);
			prt = phba->sli.ring[LPFC_ELS_RING].prt;
			prt[2].lpfc_sli_rcv_unsol_event =
				dfchba->base_ct_unsol_event;
			prt[3].lpfc_sli_rcv_unsol_event =
				dfchba->base_ct_unsol_event;
			spin_unlock_irq(&phba->hbalock);
			mutex_lock(&lpfcdfc_lock);
		}
	}
	list_del_init(&dfchba->node);
	mutex_unlock(&lpfcdfc_lock);
	kfree (dfchba);
}

/*
 * Retrieve lpfc_hba * matching instance (board no)
 * If found return lpfc_hba *
 * If not found return NULL
 */
static struct lpfcdfc_host *
lpfcdfc_get_phba_by_inst(int inst)
{
	struct Scsi_Host * host = NULL;
	struct lpfcdfc_host * dfchba;

	mutex_lock(&lpfcdfc_lock);
	list_for_each_entry(dfchba, &lpfcdfc_hosts, node) {
		if (dfchba->inst == inst) {
			if (dfchba->dev->driver) {
				host = pci_get_drvdata(dfchba->dev);
				if ((host != NULL) &&
				    (struct lpfc_vport *)host->hostdata ==
					dfchba->vport) {
					mutex_unlock(&lpfcdfc_lock);
					BUG_ON(dfchba->phba->brd_no != inst);
					return dfchba;
				}
			}
			mutex_unlock(&lpfcdfc_lock);
			return NULL;
		}
	}
	mutex_unlock(&lpfcdfc_lock);

	return NULL;
}

static int
lpfcdfc_do_ioctl(struct lpfcCmdInput *cip)
{
	struct lpfcdfc_host * dfchba = NULL;
	struct lpfc_hba *phba = NULL;
	int rc;
	uint32_t total_mem;
	void   *dataout;


	/* Some ioctls are per module and do not need phba */
	switch (cip->lpfc_cmd) {
	case LPFC_GET_DFC_REV:
		break;
	default:
		dfchba = lpfcdfc_get_phba_by_inst(cip->lpfc_brd);
		if (dfchba == NULL)
			return EINVAL;
		phba = dfchba->phba;
		break;
	};

	if (phba)
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"1601 libdfc ioctl entry Data: x%x x%lx x%lx x%x\n",
			cip->lpfc_cmd, (unsigned long) cip->lpfc_arg1,
			(unsigned long) cip->lpfc_arg2, cip->lpfc_outsz);
	mutex_lock(&lpfcdfc_lock);
	if (dfchba && dfchba->blocked) {
		mutex_unlock(&lpfcdfc_lock);
		return EINVAL;
	}
	if (dfchba)
		dfchba->ref_count++;
	mutex_unlock(&lpfcdfc_lock);
	if (cip->lpfc_outsz >= BUF_SZ_4K) {

		/*
		 * Allocate memory for ioctl data. If buffer is bigger than 64k,
		 * then we allocate 64k and re-use that buffer over and over to
		 * xfer the whole block. This is because Linux kernel has a
		 * problem allocating more than 120k of kernel space memory. Saw
		 * problem with GET_FCPTARGETMAPPING...
		 */
		if (cip->lpfc_outsz <= (64 * 1024))
			total_mem = cip->lpfc_outsz;
		else
			total_mem = 64 * 1024;
	} else {
		/* Allocate memory for ioctl data */
		total_mem = BUF_SZ_4K;
	}

	/*
	 * For LPFC_HBA_GET_EVENT allocate memory which is needed to store
	 * event info. Allocating maximum possible buffer size (64KB) can fail
	 * some times under heavy IO.
	 */
	if (cip->lpfc_cmd == LPFC_HBA_GET_EVENT) {
		dataout = NULL;
	} else {
		dataout = kmalloc(total_mem, GFP_KERNEL);

		if (!dataout && dfchba != NULL) {
			mutex_lock(&lpfcdfc_lock);
			if (dfchba)
				dfchba->ref_count--;
			mutex_unlock(&lpfcdfc_lock);
			return ENOMEM;
		}
	}

	switch (cip->lpfc_cmd) {

	case LPFC_GET_DFC_REV:
		((struct DfcRevInfo *) dataout)->a_Major = DFC_MAJOR_REV;
		((struct DfcRevInfo *) dataout)->a_Minor = DFC_MINOR_REV;
		cip->lpfc_outsz = sizeof (struct DfcRevInfo);
		rc = 0;
		break;

	case LPFC_SEND_ELS:
		rc = lpfc_ioctl_send_els(phba, cip, dataout);
		break;

	case LPFC_HBA_SEND_MGMT_RSP:
		rc = lpfc_ioctl_send_mgmt_rsp(phba, cip);
		break;

	case LPFC_HBA_SEND_MGMT_CMD:
	case LPFC_CT:
		rc = lpfc_ioctl_send_mgmt_cmd(phba, cip, dataout);
		break;

	case LPFC_HBA_GET_EVENT:
		rc = lpfc_ioctl_hba_get_event(phba, cip, &dataout, &total_mem);
		if ((total_mem) &&  (copy_to_user ((void __user *)
			cip->lpfc_dataout, (uint8_t *) dataout, total_mem)))
				rc = EIO;
		/* This is to prevent copy_to_user at end of the function. */
		cip->lpfc_outsz = 0;
		break;

	case LPFC_HBA_SET_EVENT:
		rc = lpfc_ioctl_hba_set_event(phba, cip);
		break;

	case LPFC_LOOPBACK_MODE:
		rc = lpfc_ioctl_loopback_mode(phba, cip, dataout);
		break;

	case LPFC_LOOPBACK_TEST:
		rc = lpfc_ioctl_loopback_test(phba, cip, dataout);
		break;

	case LPFC_HBA_RNID:
		rc = lpfc_ioctl_hba_rnid(phba, cip, dataout);
		break;

	default:
		rc = EINVAL;
		break;
	}

	if (phba)
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"1602 libdfc ioctl exit Data: x%x x%x x%lx\n",
			rc, cip->lpfc_outsz, (unsigned long) cip->lpfc_dataout);
	/* Copy data to user space config method */
	if (rc == 0) {
		if (cip->lpfc_outsz) {
			if (copy_to_user
			    ((void __user *) cip->lpfc_dataout,
			     (uint8_t *) dataout, cip->lpfc_outsz)) {
				rc = EIO;
			}
		}
	}

	kfree(dataout);
	mutex_lock(&lpfcdfc_lock);
	if (dfchba)
		dfchba->ref_count--;
	mutex_unlock(&lpfcdfc_lock);

	return rc;
}

static int
lpfcdfc_ioctl(struct inode *inode,
	      struct file *file, unsigned int cmd, unsigned long arg)
{
	int rc;
	struct lpfcCmdInput *ci;

	if (!arg)
		return -EINVAL;

	ci = (struct lpfcCmdInput *) kmalloc(sizeof (struct lpfcCmdInput),
						GFP_KERNEL);

	if (!ci)
		return -ENOMEM;

	if ((rc = copy_from_user
	       ((uint8_t *) ci, (void __user *) arg,
		sizeof (struct lpfcCmdInput)))) {
		kfree(ci);
		return -EIO;
	}

	rc = lpfcdfc_do_ioctl(ci);

	kfree(ci);
	return -rc;
}

#ifdef CONFIG_COMPAT
static long
lpfcdfc_compat_ioctl(struct file * file, unsigned int cmd, unsigned long arg)
{
	struct lpfcCmdInput32 arg32;
	struct lpfcCmdInput arg64;
	int ret;

	if(copy_from_user(&arg32, (void __user *)arg,
		sizeof(struct lpfcCmdInput32)))
		return -EFAULT;

	arg64.lpfc_brd = arg32.lpfc_brd;
	arg64.lpfc_ring = arg32.lpfc_ring;
	arg64.lpfc_iocb = arg32.lpfc_iocb;
	arg64.lpfc_flag = arg32.lpfc_flag;
	arg64.lpfc_arg1 = (void *)(unsigned long) arg32.lpfc_arg1;
	arg64.lpfc_arg2 = (void *)(unsigned long) arg32.lpfc_arg2;
	arg64.lpfc_arg3 = (void *)(unsigned long) arg32.lpfc_arg3;
	arg64.lpfc_dataout = (void *)(unsigned long) arg32.lpfc_dataout;
	arg64.lpfc_cmd = arg32.lpfc_cmd;
	arg64.lpfc_outsz = arg32.lpfc_outsz;
	arg64.lpfc_arg4 = arg32.lpfc_arg4;
	arg64.lpfc_arg5 = arg32.lpfc_arg5;

	ret = lpfcdfc_do_ioctl(&arg64);

	arg32.lpfc_brd = arg64.lpfc_brd;
	arg32.lpfc_ring = arg64.lpfc_ring;
	arg32.lpfc_iocb = arg64.lpfc_iocb;
	arg32.lpfc_flag = arg64.lpfc_flag;
	arg32.lpfc_arg1 = (u32)(unsigned long) arg64.lpfc_arg1;
	arg32.lpfc_arg2 = (u32)(unsigned long) arg64.lpfc_arg2;
	arg32.lpfc_arg3 = (u32)(unsigned long) arg64.lpfc_arg3;
	arg32.lpfc_dataout = (u32)(unsigned long) arg64.lpfc_dataout;
	arg32.lpfc_cmd = arg64.lpfc_cmd;
	arg32.lpfc_outsz = arg64.lpfc_outsz;
	arg32.lpfc_arg4 = arg64.lpfc_arg4;
	arg32.lpfc_arg5 = arg64.lpfc_arg5;

	if(copy_to_user((void __user *)arg, &arg32,
		sizeof(struct lpfcCmdInput32)))
		return -EFAULT;

	return -ret;
}
#endif

static struct file_operations lpfc_fops = {
	.owner        = THIS_MODULE,
	.ioctl        = lpfcdfc_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = lpfcdfc_compat_ioctl,
#endif
};

int
lpfc_cdev_init(void)
{

	lpfcdfc_major = register_chrdev(0,  LPFC_CHAR_DEV_NAME, &lpfc_fops);
	if (lpfcdfc_major < 0) {
		printk(KERN_ERR "%s:%d Unable to register \"%s\" device.\n",
		       __func__, __LINE__, LPFC_CHAR_DEV_NAME);
		return lpfcdfc_major;
	}

	return 0;
}

void
lpfc_cdev_exit(void)
{
	unregister_chrdev(lpfcdfc_major, LPFC_CHAR_DEV_NAME);
}
