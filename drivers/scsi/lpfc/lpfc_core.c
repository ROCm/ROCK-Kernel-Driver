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

#include "elx_os.h"
#include "elx_util.h"
#include "elx_clock.h"
#include "elx_hw.h"
#include "elx_sli.h"
#include "elx_mem.h"
#include "elx_sched.h"
#include "elx.h"
#include "elx_logmsg.h"
#include "elx_disc.h"
#include "elx_scsi.h"
#include "elx_crtn.h"
#include "elx_cfgparm.h"
#include "lpfc_hw.h"
#include "lpfc_hba.h"
#include "lpfc_crtn.h"
#include "lpfc_cfgparm.h"
#include "lpfc_diag.h"
#include "prod_crtn.h"
#include "hbaapi.h"

extern char *lpfc_release_version;

DMABUF_t *lpfc_alloc_ct_rsp(elxHBA_t *, ULP_BDE64 *, uint32_t, int *);

void
lpfc_ct_unsol_event(elxHBA_t * phba,
		    ELX_SLI_RING_t * pring, ELX_IOCBQ_t * piocbq)
{
	LPFCHBA_t *plhba;
	ELX_SLI_t *psli;
	DMABUF_t *p_mbuf = 0;
	DMABUF_t *last_mp;
	DMABUF_t *matp;
	uint32_t ctx;
	uint32_t count;
	IOCB_t *icmd;
	int i;

	psli = &phba->sli;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	icmd = &piocbq->iocb;
	if (icmd->ulpStatus) {
		goto dropit;
	}
	ctx = 0;
	count = 0;
	last_mp = 0;
	while (piocbq) {
		icmd = &piocbq->iocb;
		if (ctx == 0)
			ctx = (uint32_t) (icmd->ulpContext);
		if (icmd->ulpStatus) {
			if ((icmd->ulpStatus == IOSTAT_LOCAL_REJECT) &&
			    ((icmd->un.ulpWord[4] & 0xff) ==
			     IOERR_RCV_BUFFER_WAITING)) {
				/* FCSTATCTR.NoRcvBuf++; */

				if (!(plhba->fc_flag & FC_NO_RCV_BUF)) {

				}
				plhba->fc_flag |= FC_NO_RCV_BUF;

				lpfc_post_buffer(phba, pring, 0, 1);
			}
			goto dropit;
		}

		if (icmd->ulpBdeCount == 0) {
			piocbq = (ELX_IOCBQ_t *) piocbq->q_f;
			continue;
		}
		for (i = 0; i < (int)icmd->ulpBdeCount; i++) {
			matp = elx_sli_ringpostbuf_get(phba, pring,
						       (elx_dma_addr_t)
						       getPaddr(icmd->un.
								cont64[i].
								addrHigh,
								icmd->un.
								cont64[i].
								addrLow));
			if (matp == 0) {

				goto dropit;
			}

			/* Typically for Unsolicited CT requests */

			if (last_mp) {
				last_mp->next = (void *)matp;
			} else {
				p_mbuf = matp;
			}
			last_mp = matp;
			matp->next = 0;
			count += icmd->un.cont64[i].tus.f.bdeSize;

		}

		lpfc_post_buffer(phba, pring, i, 1);
		icmd->ulpBdeCount = 0;
		piocbq = (ELX_IOCBQ_t *) piocbq->q_f;
	}
	if (p_mbuf == 0) {

		goto dropit;
	}

	/* FC_REG_CT_EVENT for HBAAPI Ioctl event handling */
	if (dfc_put_event
	    (phba, FC_REG_CT_EVENT, ctx, (void *)p_mbuf,
	     (void *)((ulong) count))) {

		return;
	}

      dropit:

	while (p_mbuf) {
		matp = p_mbuf;
		p_mbuf = (DMABUF_t *) matp->next;
		elx_mem_put(phba, MEM_BUF, (uint8_t *) matp);
	}
	return;
}

int
lpfc_ns_cmd(elxHBA_t * phba, LPFC_NODELIST_t * ndlp, int cmdcode)
{
	elxCfgParam_t *clp;
	DMABUF_t *mp, *bmp;
	SLI_CT_REQUEST *CtReq;
	ULP_BDE64 *bpl;
	LPFCHBA_t *plhba;
	void (*cmpl) (struct elxHBA *, ELX_IOCBQ_t *, ELX_IOCBQ_t *);

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	clp = &phba->config[0];

	/* fill in BDEs for command */
	/* Allocate buffer for command payload */
	if ((mp = (DMABUF_t *) elx_mem_get(phba, MEM_BUF)) == 0) {
		return (1);
	}

	/* Allocate buffer for Buffer ptr list */
	if ((bmp = (DMABUF_t *) elx_mem_get(phba, MEM_BPL)) == 0) {
		elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
		return (1);
	}
	bpl = (ULP_BDE64 *) bmp->virt;
	bpl->addrHigh = PCIMEM_LONG(putPaddrHigh(mp->phys));
	bpl->addrLow = PCIMEM_LONG(putPaddrLow(mp->phys));
	bpl->tus.f.bdeFlags = 0;
	if (cmdcode == SLI_CTNS_GID_FT)
		bpl->tus.f.bdeSize = GID_REQUEST_SZ;
	else if (cmdcode == SLI_CTNS_RFT_ID)
		bpl->tus.f.bdeSize = RFT_REQUEST_SZ;
	else if (cmdcode == SLI_CTNS_RNN_ID)
		bpl->tus.f.bdeSize = RNN_REQUEST_SZ;
	else if (cmdcode == SLI_CTNS_RSNN_NN)
		bpl->tus.f.bdeSize = RSNN_REQUEST_SZ;
	else
		bpl->tus.f.bdeSize = 0;
	bpl->tus.w = PCIMEM_LONG(bpl->tus.w);

	CtReq = (SLI_CT_REQUEST *) mp->virt;
	/* NameServer Req */
	elx_printf_log(phba->brd_no, &elx_msgBlk0236,	/* ptr to msg structure */
		       elx_mes0236,	/* ptr to msg */
		       elx_msgBlk0236.msgPreambleStr,	/* begin varargs */
		       cmdcode, plhba->fc_flag, plhba->fc_rscn_id_cnt);	/* end varargs */

	memset((void *)CtReq, 0, sizeof (SLI_CT_REQUEST));
	CtReq->RevisionId.bits.Revision = SLI_CT_REVISION;
	CtReq->RevisionId.bits.InId = 0;

	CtReq->FsType = SLI_CT_DIRECTORY_SERVICE;
	CtReq->FsSubType = SLI_CT_DIRECTORY_NAME_SERVER;

	CtReq->CommandResponse.bits.Size = 0;

	cmpl = 0;
	switch (cmdcode) {
	case SLI_CTNS_GID_FT:
		CtReq->CommandResponse.bits.CmdRsp =
		    SWAP_DATA16(SLI_CTNS_GID_FT);
		CtReq->un.gid.Fc4Type = SLI_CTPT_FCP;
		if (phba->hba_state < ELX_HBA_READY) {
			phba->hba_state = ELX_NS_QRY;
		}
		lpfc_set_disctmo(phba);
		cmpl = lpfc_cmpl_ct_cmd_gid_ft;
		break;
	case SLI_CTNS_RFT_ID:
		CtReq->CommandResponse.bits.CmdRsp =
		    SWAP_DATA16(SLI_CTNS_RFT_ID);
		CtReq->un.rft.PortId = SWAP_DATA(plhba->fc_myDID);
		CtReq->un.rft.fcpReg = 1;
		if (clp[LPFC_CFG_NETWORK_ON].a_current) {
			CtReq->un.rft.ipReg = 1;
		}
		cmpl = lpfc_cmpl_ct_cmd_rft_id;
		break;
	case SLI_CTNS_RNN_ID:
		CtReq->CommandResponse.bits.CmdRsp =
		    SWAP_DATA16(SLI_CTNS_RNN_ID);
		CtReq->un.rnn.PortId = SWAP_DATA(plhba->fc_myDID);
		memcpy(CtReq->un.rnn.wwnn, (uint8_t *) & plhba->fc_nodename,
		       sizeof (NAME_TYPE));
		cmpl = lpfc_cmpl_ct_cmd_rnn_id;
		break;
	case SLI_CTNS_RSNN_NN:

		CtReq->CommandResponse.bits.CmdRsp =
		    SWAP_DATA16(SLI_CTNS_RSNN_NN);
		memcpy(CtReq->un.rsnn.wwnn, (uint8_t *) & plhba->fc_nodename,
		       sizeof (NAME_TYPE));
		lpfc_get_hba_SymbNodeName(phba,
					  (uint8_t *) CtReq->un.rsnn.symbname);
		CtReq->un.rsnn.len =
		    elx_str_len((char *)CtReq->un.rsnn.symbname);
		cmpl = lpfc_cmpl_ct_cmd_rsnn_nn;
		break;
	}

	if (lpfc_ct_cmd(phba, mp, bmp, ndlp, cmpl)) {
		elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
		elx_mem_put(phba, MEM_BPL, (uint8_t *) bmp);
		return (1);
	}
	return (0);
}

int
lpfc_ct_cmd(elxHBA_t * phba,
	    DMABUF_t * inmp,
	    DMABUF_t * bmp,
	    LPFC_NODELIST_t * ndlp,
	    void (*cmpl) (struct elxHBA *, ELX_IOCBQ_t *, ELX_IOCBQ_t *))
{
	ULP_BDE64 *bpl;
	DMABUF_t *outmp;
	int cnt;

	bpl = (ULP_BDE64 *) bmp->virt;
	bpl++;			/* Skip past ct request */

	cnt = 0;
	/* Put buffer(s) for ct rsp in bpl */
	if ((outmp = lpfc_alloc_ct_rsp(phba, bpl, FC_MAX_NS_RSP, &cnt)) == 0) {
		return (ENOMEM);
	}

	/* save ndlp for cmpl */
	inmp->next = (DMABUF_t *) ndlp;

	if ((lpfc_gen_req
	     (phba, bmp, inmp, outmp, cmpl, ndlp->nle.nlp_rpi, 0, (cnt + 1),
	      0))) {
		lpfc_free_ct_rsp(phba, outmp);
		return (ENOMEM);
	}
	return (0);
}

int
lpfc_free_ct_rsp(elxHBA_t * phba, DMABUF_t * mlist)
{
	DMABUF_t *mlast;

	while (mlist) {
		mlast = mlist;
		mlist = (DMABUF_t *) mlist->next;

		elx_mem_put(phba, MEM_BUF, (uint8_t *) mlast);
	}
	return (0);
}

DMABUF_t *
lpfc_alloc_ct_rsp(elxHBA_t * phba, ULP_BDE64 * bpl, uint32_t size, int *entries)
{
	DMABUF_t *mlist;
	DMABUF_t *mlast;
	DMABUF_t *mp;
	int cnt, i;

	mlist = 0;
	mlast = 0;
	i = 0;

	while (size) {

		/* We get chucks of FCELSSIZE */
		if (size > FCELSSIZE)
			cnt = FCELSSIZE;
		else
			cnt = size;

		/* Allocate buffer for rsp payload */
		if ((mp = (DMABUF_t *) elx_mem_get(phba, MEM_BUF)) == 0) {
			lpfc_free_ct_rsp(phba, mlist);
			return (0);
		}

		/* Queue it to a linked list */
		if (mlast == 0) {
			mlist = mp;
			mlast = mp;
		} else {
			mlast->next = mp;
			mlast = mp;
		}
		mp->next = 0;

		bpl->tus.f.bdeFlags = BUFF_USE_RCV;

		/* build buffer ptr list for IOCB */
		bpl->addrLow = PCIMEM_LONG(putPaddrLow(mp->phys));
		bpl->addrHigh = PCIMEM_LONG(putPaddrHigh(mp->phys));
		bpl->tus.f.bdeSize = (uint16_t) cnt;
		bpl->tus.w = PCIMEM_LONG(bpl->tus.w);
		bpl++;

		i++;
		size -= cnt;
	}

	*entries = i;
	return (mlist);
}

int
lpfc_ns_rsp(elxHBA_t * phba, DMABUF_t * mp, uint32_t Size)
{
	LPFCHBA_t *plhba;
	elxCfgParam_t *clp;
	SLI_CT_REQUEST *Response;
	LPFC_NODELIST_t *ndlp;
	LPFC_NODELIST_t *new_ndlp;
	ELXSCSITARGET_t *targetp;
	DMABUF_t *mlast;
	uint32_t *ctptr;
	uint32_t Did;
	uint32_t CTentry;
	int Cnt, new_node;
	unsigned long iflag;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	clp = &phba->config[0];
	ndlp = 0;

	lpfc_set_disctmo(phba);

	Response = (SLI_CT_REQUEST *) mp->virt;
	ctptr = (uint32_t *) & Response->un.gid.PortType;
	while (mp) {
		mlast = mp;
		mp = (DMABUF_t *) mp->next;
		elx_pci_dma_sync((void *)phba, (void *)mp,
				 0, ELX_DMA_SYNC_FORCPU);

		if (Size > FCELSSIZE)
			Cnt = FCELSSIZE;
		else
			Cnt = Size;
		Size -= Cnt;

		if (ctptr == 0)
			ctptr = (uint32_t *) mlast->virt;
		else
			Cnt -= 16;	/* subtract length of CT header */

		/* Loop through entire NameServer list of DIDs */
		while (Cnt) {

			/* Get next DID from NameServer List */
			CTentry = *ctptr++;
			Did = ((SWAP_DATA(CTentry)) & Mask_DID);

			/* If we are processing an RSCN, check to ensure the Did falls
			 * under the juristiction of the RSCN payload.
			 */
			if (phba->hba_state == ELX_HBA_READY) {
				Did = lpfc_rscn_payload_check(phba, Did);
				/* Did = 0 indicates Not part of RSCN, ignore this entry */
			}

			ndlp = 0;
			if ((Did) && (Did != plhba->fc_myDID)) {
				new_node = 0;
				/* Skip if the node is already in the plogi / adisc list */
				if ((ndlp = lpfc_findnode_did(phba,
							      (NLP_SEARCH_PLOGI
							       |
							       NLP_SEARCH_ADISC),
							      Did))) {
					goto nsout0;
				}
				ndlp =
				    lpfc_findnode_did(phba, NLP_SEARCH_ALL,
						      Did);
				if (ndlp) {
					lpfc_disc_state_machine(phba, ndlp,
								(void *)0,
								NLP_EVT_DEVICE_ADD);
				} else {
					new_node = 1;
					if ((ndlp =
					     (LPFC_NODELIST_t *)
					     elx_mem_get(phba, MEM_NLP))) {
						memset((void *)ndlp, 0,
						       sizeof
						       (LPFC_NODELIST_t));
						ndlp->nlp_state =
						    NLP_STE_UNUSED_NODE;
						ndlp->nlp_DID = Did;
						lpfc_disc_state_machine(phba,
									ndlp,
									(void *)
									0,
									NLP_EVT_DEVICE_ADD);
					}
				}
			}
		      nsout0:
			/* Mark all node table entries that are in the Nameserver */
			if (ndlp) {
				ndlp->nlp_flag |= NLP_NS_NODE;
				/* NameServer Rsp */
				elx_printf_log(phba->brd_no, &elx_msgBlk0238,	/* ptr to msg structure */
					       elx_mes0238,	/* ptr to msg */
					       elx_msgBlk0238.msgPreambleStr,	/* begin varargs */
					       Did, ndlp->nlp_flag, plhba->fc_flag, plhba->fc_rscn_id_cnt);	/* end varargs */
			} else {
				/* NameServer Rsp */
				elx_printf_log(phba->brd_no, &elx_msgBlk0239,	/* ptr to msg structure */
					       elx_mes0239,	/* ptr to msg */
					       elx_msgBlk0239.msgPreambleStr,	/* begin varargs */
					       Did, Size, plhba->fc_flag, plhba->fc_rscn_id_cnt);	/* end varargs */
			}

			if (CTentry & (SWAP_DATA(SLI_CT_LAST_ENTRY)))
				goto nsout1;
			Cnt -= sizeof (uint32_t);
		}
		ctptr = 0;
	}

      nsout1:
	ELX_DISC_LOCK(phba, iflag);
	/* Take out all node table entries that are not in the NameServer */
	ndlp = plhba->fc_plogi_start;
	if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_plogi_start)
		ndlp = plhba->fc_adisc_start;
	if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_adisc_start)
		ndlp = plhba->fc_nlpunmap_start;
	if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start)
		ndlp = plhba->fc_nlpmap_start;
	while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start) {
		new_ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
		if ((ndlp->nlp_DID == plhba->fc_myDID) ||
		    (ndlp->nlp_DID == NameServer_DID) ||
		    (ndlp->nlp_DID == FDMI_DID) ||
		    (ndlp->nle.nlp_type & NLP_FABRIC) ||
		    (ndlp->nlp_flag & NLP_NS_NODE)) {
			if (ndlp->nlp_flag & NLP_NS_NODE) {
				ndlp->nlp_flag &= ~NLP_NS_NODE;
			}
			goto loop1;
		}
		ELX_DISC_UNLOCK(phba, iflag);
		/* If we are processing an RSCN, check to ensure the Did falls
		 * under the juristiction of the RSCN payload.
		 */
		if ((phba->hba_state == ELX_HBA_READY) &&
		    (!(lpfc_rscn_payload_check(phba, ndlp->nlp_DID)))) {
			ELX_DISC_LOCK(phba, iflag);
			goto loop1;	/* Not part of RSCN, ignore this entry */
		}

		targetp = ndlp->nlp_Target;
		/* Make sure nodev tmo is NOT running so DEVICE_RM really removes it */
		if (ndlp->nlp_tmofunc) {
			elx_clk_can(phba, ndlp->nlp_tmofunc);
			ndlp->nlp_flag &= ~(NLP_NODEV_TMO | NLP_DELAY_TMO);
			ndlp->nle.nlp_rflag &= ~NLP_NPR_ACTIVE;
			ndlp->nlp_tmofunc = 0;
		}
		lpfc_disc_state_machine(phba, ndlp, (void *)0,
					NLP_EVT_DEVICE_RM);

		/* If we were a FCP target, go into NPort Recovery mode to give
		 * it a chance to come back.
		 */
		if (targetp) {
			if (clp[ELX_CFG_HOLDIO].a_current) {
				targetp->targetFlags |= FC_NPR_ACTIVE;
				if (targetp->tmofunc) {
					elx_clk_can(phba, targetp->tmofunc);
					targetp->tmofunc = 0;
				}
			} else {
				if (clp[ELX_CFG_NODEV_TMO].a_current) {
					targetp->targetFlags |= FC_NPR_ACTIVE;
					if (targetp->tmofunc) {
						elx_clk_can(phba,
							    targetp->tmofunc);
					}
					targetp->tmofunc =
					    elx_clk_set(phba,
							clp[ELX_CFG_NODEV_TMO].
							a_current,
							lpfc_npr_timeout,
							(void *)targetp,
							(void *)0);
				}
			}
		}
		ELX_DISC_LOCK(phba, iflag);
	      loop1:
		ndlp = new_ndlp;
		if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_plogi_start)
			ndlp = plhba->fc_adisc_start;
		if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_adisc_start)
			ndlp = plhba->fc_nlpunmap_start;
		if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start)
			ndlp = plhba->fc_nlpmap_start;
	}
	ELX_DISC_UNLOCK(phba, iflag);

	if (phba->hba_state == ELX_HBA_READY) {
		lpfc_els_flush_rscn(phba);
		plhba->fc_flag |= FC_RSCN_MODE;
	}
	return (0);
}

int
lpfc_issue_ct_rsp(elxHBA_t * phba,
		  uint32_t tag, DMABUF_t * bmp, DMABUFEXT_t * inp)
{
	LPFCHBA_t *plhba;
	ELX_SLI_t *psli;
	IOCB_t *icmd;
	ELX_IOCBQ_t *ctiocb;
	ELX_SLI_RING_t *pring;
	uint32_t num_entry;
	int rc = 0;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	num_entry = (uint32_t) inp->flag;
	inp->flag = 0;

	/* Allocate buffer for  command iocb */
	if ((ctiocb = (ELX_IOCBQ_t *) elx_mem_get(phba, MEM_IOCB)) == 0) {
		return (ENOMEM);
	}
	memset((void *)ctiocb, 0, sizeof (ELX_IOCBQ_t));
	icmd = &ctiocb->iocb;

	icmd->un.xseq64.bdl.ulpIoTag32 = 0;
	icmd->un.xseq64.bdl.addrHigh = putPaddrHigh(bmp->phys);
	icmd->un.xseq64.bdl.addrLow = putPaddrLow(bmp->phys);
	icmd->un.xseq64.bdl.bdeFlags = BUFF_TYPE_BDL;
	icmd->un.xseq64.bdl.bdeSize = (num_entry * sizeof (ULP_BDE64));

	icmd->un.xseq64.w5.hcsw.Fctl = (LS | LA);
	icmd->un.xseq64.w5.hcsw.Dfctl = 0;
	icmd->un.xseq64.w5.hcsw.Rctl = FC_SOL_CTL;
	icmd->un.xseq64.w5.hcsw.Type = FC_COMMON_TRANSPORT_ULP;

	elx_pci_dma_sync((void *)phba, (void *)bmp, 0, ELX_DMA_SYNC_FORDEV);

	icmd->ulpIoTag = elx_sli_next_iotag(phba, pring);

	/* Fill in rest of iocb */
	icmd->ulpCommand = CMD_XMIT_SEQUENCE64_CX;
	icmd->ulpBdeCount = 1;
	icmd->ulpLe = 1;
	icmd->ulpClass = CLASS3;
	icmd->ulpContext = (ushort) tag;
	icmd->ulpOwner = OWN_CHIP;
	/* Xmit CT response on exchange <xid> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0118,	/* ptr to msg structure */
		       elx_mes0118,	/* ptr to msg */
		       elx_msgBlk0118.msgPreambleStr,	/* begin varargs */
		       icmd->ulpContext,	/* xid */
		       icmd->ulpIoTag, phba->hba_state);	/* end varargs */

	ctiocb->iocb_cmpl = 0;
	ctiocb->iocb_flag |= ELX_IO_IOCTL;

	rc = elx_sli_issue_iocb_wait(phba, pring, ctiocb, SLI_IOCB_USE_TXQ,
				     NULL,
				     plhba->fc_ratov * 2 + ELX_DRVR_TIMEOUT);
	elx_mem_put(phba, MEM_IOCB, (uint8_t *) ctiocb);
	return (rc);
}				/* lpfc_issue_ct_rsp */

int
lpfc_gen_req(elxHBA_t * phba,
	     DMABUF_t * bmp,
	     DMABUF_t * inp,
	     DMABUF_t * outp,
	     void (*cmpl) (struct elxHBA *, ELX_IOCBQ_t *, ELX_IOCBQ_t *),
	     uint32_t rpi, uint32_t usr_flg, uint32_t num_entry, uint32_t tmo)
{
	LPFCHBA_t *plhba;
	ELX_SLI_t *psli;
	ELX_SLI_RING_t *pring;
	IOCB_t *icmd;
	ELX_IOCBQ_t *geniocb;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	/* Allocate buffer for  command iocb */
	if ((geniocb = (ELX_IOCBQ_t *) elx_mem_get(phba, MEM_IOCB)) == 0) {
		return (1);
	}
	memset((void *)geniocb, 0, sizeof (ELX_IOCBQ_t));
	icmd = &geniocb->iocb;

	icmd->un.genreq64.bdl.ulpIoTag32 = 0;
	icmd->un.genreq64.bdl.addrHigh = putPaddrHigh(bmp->phys);
	icmd->un.genreq64.bdl.addrLow = putPaddrLow(bmp->phys);
	icmd->un.genreq64.bdl.bdeFlags = BUFF_TYPE_BDL;
	icmd->un.genreq64.bdl.bdeSize = (num_entry * sizeof (ULP_BDE64));

	if (usr_flg)
		geniocb->context3 = 0;
	else
		geniocb->context3 = (uint8_t *) bmp;

	/* Save for completion so we can release these resources */
	geniocb->context1 = (uint8_t *) inp;
	geniocb->context2 = (uint8_t *) outp;

	/* Fill in payload, bp points to frame payload */
	icmd->ulpCommand = CMD_GEN_REQUEST64_CR;

	elx_pci_dma_sync((void *)phba, (void *)bmp, 0, ELX_DMA_SYNC_FORDEV);

	icmd->ulpIoTag = elx_sli_next_iotag(phba, pring);

	/* Fill in rest of iocb */
	icmd->un.genreq64.w5.hcsw.Fctl = (SI | LA);
	icmd->un.genreq64.w5.hcsw.Dfctl = 0;
	icmd->un.genreq64.w5.hcsw.Rctl = FC_UNSOL_CTL;
	icmd->un.genreq64.w5.hcsw.Type = FC_COMMON_TRANSPORT_ULP;

	if (tmo == 0)
		tmo = (2 * plhba->fc_ratov) + 1;
	icmd->ulpTimeout = tmo;
	icmd->ulpBdeCount = 1;
	icmd->ulpLe = 1;
	icmd->ulpClass = CLASS3;
	icmd->ulpContext = (volatile ushort)rpi;
	icmd->ulpOwner = OWN_CHIP;
	/* Issue GEN REQ IOCB for NPORT <did> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0119,	/* ptr to msg structure */
		       elx_mes0119,	/* ptr to msg */
		       elx_msgBlk0119.msgPreambleStr,	/* begin varargs */
		       icmd->un.ulpWord[5],	/* did */
		       icmd->ulpIoTag, phba->hba_state);	/* end varargs */
	geniocb->iocb_cmpl = cmpl;
	geniocb->drvrTimeout = icmd->ulpTimeout + ELX_DRVR_TIMEOUT;
	if (elx_sli_issue_iocb(phba, pring, geniocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		elx_mem_put(phba, MEM_IOCB, (uint8_t *) geniocb);
		return (1);
	}

	return (0);
}

void
lpfc_cmpl_ct_cmd_gid_ft(elxHBA_t * phba,
			ELX_IOCBQ_t * cmdiocb, ELX_IOCBQ_t * rspiocb)
{
	LPFCHBA_t *plhba;
	IOCB_t *irsp;
	ELX_SLI_t *psli;
	DMABUF_t *bmp;
	DMABUF_t *inp;
	DMABUF_t *outp;
	LPFC_NODELIST_t *ndlp;
	SLI_CT_REQUEST *CTrsp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;
	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->q_f = rspiocb;

	inp = (DMABUF_t *) cmdiocb->context1;
	outp = (DMABUF_t *) cmdiocb->context2;
	bmp = (DMABUF_t *) cmdiocb->context3;

	irsp = &rspiocb->iocb;
	if (irsp->ulpStatus) {
		/* Check for retry */
		if (plhba->fc_ns_retry < LPFC_MAX_NS_RETRY) {
			plhba->fc_ns_retry++;
			/* CT command is being retried */
			ndlp =
			    lpfc_findnode_did(phba, NLP_SEARCH_UNMAPPED,
					      NameServer_DID);
			if (ndlp) {
				if (lpfc_ns_cmd(phba, ndlp, SLI_CTNS_GID_FT) ==
				    0) {
					goto out;
				}
			}
		}
	} else {
		/* Good status, continue checking */
		CTrsp = (SLI_CT_REQUEST *) outp->virt;
		if (CTrsp->CommandResponse.bits.CmdRsp ==
		    SWAP_DATA16(SLI_CT_RESPONSE_FS_ACC)) {
			lpfc_ns_rsp(phba, outp,
				    (uint32_t) (irsp->un.genreq64.bdl.bdeSize));
		} else if (CTrsp->CommandResponse.bits.CmdRsp ==
			   SWAP_DATA16(SLI_CT_RESPONSE_FS_RJT)) {
			/* NameServer Rsp Error */
			elx_printf_log(phba->brd_no, &elx_msgBlk0240,	/* ptr to msg structure */
				       elx_mes0240,	/* ptr to msg */
				       elx_msgBlk0240.msgPreambleStr,	/* begin varargs */
				       CTrsp->CommandResponse.bits.CmdRsp, (uint32_t) CTrsp->ReasonCode, (uint32_t) CTrsp->Explanation, plhba->fc_flag);	/* end varargs */
		} else {
			/* NameServer Rsp Error */
			elx_printf_log(phba->brd_no, &elx_msgBlk0241,	/* ptr to msg structure */
				       elx_mes0241,	/* ptr to msg */
				       elx_msgBlk0241.msgPreambleStr,	/* begin varargs */
				       CTrsp->CommandResponse.bits.CmdRsp, (uint32_t) CTrsp->ReasonCode, (uint32_t) CTrsp->Explanation, plhba->fc_flag);	/* end varargs */
		}
	}
	/* Link up / RSCN discovery */
	lpfc_disc_start(phba);
      out:
	lpfc_free_ct_rsp(phba, outp);
	elx_mem_put(phba, MEM_BUF, (uint8_t *) inp);
	elx_mem_put(phba, MEM_BPL, (uint8_t *) bmp);
	elx_mem_put(phba, MEM_IOCB, (uint8_t *) cmdiocb);
	return;
}

void
lpfc_cmpl_ct_cmd_rft_id(elxHBA_t * phba,
			ELX_IOCBQ_t * cmdiocb, ELX_IOCBQ_t * rspiocb)
{
	LPFCHBA_t *plhba;
	ELX_SLI_t *psli;
	DMABUF_t *bmp;
	DMABUF_t *inp;
	DMABUF_t *outp;
	IOCB_t *irsp;
	SLI_CT_REQUEST *CTrsp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;
	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->q_f = rspiocb;

	inp = (DMABUF_t *) cmdiocb->context1;
	outp = (DMABUF_t *) cmdiocb->context2;
	bmp = (DMABUF_t *) cmdiocb->context3;
	irsp = &rspiocb->iocb;

	CTrsp = (SLI_CT_REQUEST *) outp->virt;

	/* RFT request completes status <ulpStatus> CmdRsp <CmdRsp> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0209,	/* ptr to msg structure */
		       elx_mes0209,	/* ptr to msg */
		       elx_msgBlk0209.msgPreambleStr,	/* begin varargs */
		       irsp->ulpStatus, CTrsp->CommandResponse.bits.CmdRsp);	/* end varargs */

	lpfc_free_ct_rsp(phba, outp);
	elx_mem_put(phba, MEM_BUF, (uint8_t *) inp);
	elx_mem_put(phba, MEM_BPL, (uint8_t *) bmp);
	elx_mem_put(phba, MEM_IOCB, (uint8_t *) cmdiocb);
	return;
}

void
lpfc_cmpl_ct_cmd_rnn_id(elxHBA_t * phba,
			ELX_IOCBQ_t * cmdiocb, ELX_IOCBQ_t * rspiocb)
{
	lpfc_cmpl_ct_cmd_rft_id(phba, cmdiocb, rspiocb);
	return;
}

void
lpfc_cmpl_ct_cmd_rsnn_nn(elxHBA_t * phba,
			 ELX_IOCBQ_t * cmdiocb, ELX_IOCBQ_t * rspiocb)
{
	lpfc_cmpl_ct_cmd_rft_id(phba, cmdiocb, rspiocb);
	return;
}

int
lpfc_fdmi_cmd(elxHBA_t * phba, LPFC_NODELIST_t * ndlp, int cmdcode)
{
	elxCfgParam_t *clp;
	DMABUF_t *mp, *bmp;
	SLI_CT_REQUEST *CtReq;
	ULP_BDE64 *bpl;
	LPFCHBA_t *plhba;
	uint32_t size;
	PREG_HBA rh;
	PPORT_ENTRY pe;
	PREG_PORT_ATTRIBUTE pab;
	PATTRIBUTE_BLOCK ab;
	PATTRIBUTE_ENTRY ae;
	uint32_t id;
	void (*cmpl) (struct elxHBA *, ELX_IOCBQ_t *, ELX_IOCBQ_t *);

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	clp = &phba->config[0];

	/* fill in BDEs for command */
	/* Allocate buffer for command payload */
	if ((mp = (DMABUF_t *) elx_mem_get(phba, MEM_BUF)) == 0) {
		/* Issue FDMI request failed */
		elx_printf_log(phba->brd_no, &elx_msgBlk0219,	/* ptr to msg structure */
			       elx_mes0219,	/* ptr to msg */
			       elx_msgBlk0219.msgPreambleStr,	/* begin varargs */
			       cmdcode);	/* end varargs */
		return (1);
	}

	/* Allocate buffer for Buffer ptr list */
	if ((bmp = (DMABUF_t *) elx_mem_get(phba, MEM_BPL)) == 0) {
		elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
		/* Issue FDMI request failed */
		elx_printf_log(phba->brd_no, &elx_msgBlk0243,	/* ptr to msg structure */
			       elx_mes0243,	/* ptr to msg */
			       elx_msgBlk0243.msgPreambleStr,	/* begin varargs */
			       cmdcode);	/* end varargs */
		return (1);
	}

	/* FDMI request */
	elx_printf_log(phba->brd_no, &elx_msgBlk0218,	/* ptr to msg structure */
		       elx_mes0218,	/* ptr to msg */
		       elx_msgBlk0218.msgPreambleStr,	/* begin varargs */
		       plhba->fc_flag, phba->hba_state, cmdcode);	/* end varargs */

	CtReq = (SLI_CT_REQUEST *) mp->virt;

/*
   memset((void *)CtReq, 0, sizeof(SLI_CT_REQUEST));
*/
	memset((void *)CtReq, 0, 1024);
	CtReq->RevisionId.bits.Revision = SLI_CT_REVISION;
	CtReq->RevisionId.bits.InId = 0;

	CtReq->FsType = SLI_CT_MANAGEMENT_SERVICE;
	CtReq->FsSubType = SLI_CT_FDMI_Subtypes;
	size = 0;

#define FOURBYTES	4

	switch (cmdcode) {
	case SLI_MGMT_RHBA:
		{
			elx_vpd_t *vp;
			char *str;
			char lpfc_fwrevision[32];
			uint32_t i, j, incr;
			int len;
			uint8_t HWrev[8];

			vp = &phba->vpd;

			CtReq->CommandResponse.bits.CmdRsp =
			    SWAP_DATA16(SLI_MGMT_RHBA);
			CtReq->CommandResponse.bits.Size = 0;
			rh = (REG_HBA *) & CtReq->un.PortID;
			memcpy((uint8_t *) & rh->hi.PortName,
			       (uint8_t *) & plhba->fc_sparam.portName,
			       sizeof (NAME_TYPE));
			rh->rpl.EntryCnt = SWAP_DATA(1);	/* One entry (port) per adapter */
			memcpy((uint8_t *) & rh->rpl.pe,
			       (uint8_t *) & plhba->fc_sparam.portName,
			       sizeof (NAME_TYPE));

			/* point to the HBA attribute block */
			size =
			    sizeof (NAME_TYPE) + FOURBYTES + sizeof (NAME_TYPE);
			ab = (ATTRIBUTE_BLOCK *) ((uint8_t *) rh + size);
			ab->EntryCnt = 0;

			/* Point to the begin of the first HBA attribute entry */
			/* #1 HBA attribute entry */
			size += FOURBYTES;
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = SWAP_DATA16(NODE_NAME);
			ae->ad.bits.AttrLen =
			    SWAP_DATA16(FOURBYTES + sizeof (NAME_TYPE));
			memcpy((uint8_t *) & ae->un.NodeName,
			       (uint8_t *) & plhba->fc_sparam.nodeName,
			       sizeof (NAME_TYPE));
			ab->EntryCnt++;
			size += FOURBYTES + sizeof (NAME_TYPE);

			/* #2 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = SWAP_DATA16(MANUFACTURER);
			elx_str_cpy((char *)ae->un.Manufacturer,
				    "Emulex Corporation");
			len = elx_str_len((char *)ae->un.Manufacturer);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = SWAP_DATA16(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #3 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = SWAP_DATA16(SERIAL_NUMBER);
			elx_str_cpy((char *)ae->un.SerialNumber,
				    phba->SerialNumber);
			len = elx_str_len((char *)ae->un.SerialNumber);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = SWAP_DATA16(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #4 HBA attribute entry */
			id = elx_read_pci(phba, PCI_VENDOR_ID_REGISTER);
			switch ((id >> 16) & 0xffff) {
			case PCI_DEVICE_ID_SUPERFLY:
				if ((vp->rev.biuRev == 1)
				    || (vp->rev.biuRev == 2)
				    || (vp->rev.biuRev == 3)) {
					ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh
								  + size);
					ae->ad.bits.AttrType =
					    SWAP_DATA16(MODEL);
					elx_str_cpy((char *)ae->un.Model,
						    "LP7000");
					len = elx_str_len((char *)ae->un.Model);
					len += (len & 3) ? (4 - (len & 3)) : 4;
					ae->ad.bits.AttrLen =
					    SWAP_DATA16(FOURBYTES + len);
					ab->EntryCnt++;
					size += FOURBYTES + len;

					/* #5 HBA attribute entry */
					ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh
								  + size);
					ae->ad.bits.AttrType =
					    SWAP_DATA16(MODEL_DESCRIPTION);
					elx_str_cpy((char *)ae->un.
						    ModelDescription,
						    "Emulex LightPulse LP7000 1 Gigabit PCI Fibre Channel Adapter");
					len =
					    elx_str_len((char *)ae->un.
							ModelDescription);
					len += (len & 3) ? (4 - (len & 3)) : 4;
					ae->ad.bits.AttrLen =
					    SWAP_DATA16(FOURBYTES + len);
					ab->EntryCnt++;
					size += FOURBYTES + len;
				} else {
					ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh
								  + size);
					ae->ad.bits.AttrType =
					    SWAP_DATA16(MODEL);
					elx_str_cpy((char *)ae->un.Model,
						    "LP7000E");
					len = elx_str_len((char *)ae->un.Model);
					len += (len & 3) ? (4 - (len & 3)) : 4;
					ae->ad.bits.AttrLen =
					    SWAP_DATA16(FOURBYTES + len);
					ab->EntryCnt++;
					size += FOURBYTES + len;

					/* #5 HBA attribute entry */
					ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh
								  + size);
					ae->ad.bits.AttrType =
					    SWAP_DATA16(MODEL_DESCRIPTION);
					elx_str_cpy((char *)ae->un.
						    ModelDescription,
						    "Emulex LightPulse LP7000E 1 Gigabit PCI Fibre Channel Adapter");
					len =
					    elx_str_len((char *)ae->un.
							ModelDescription);
					len += (len & 3) ? (4 - (len & 3)) : 4;
					ae->ad.bits.AttrLen =
					    SWAP_DATA16(FOURBYTES + len);
					ab->EntryCnt++;
					size += FOURBYTES + len;
				}
				break;
			case PCI_DEVICE_ID_DRAGONFLY:
				ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh +
							  size);
				ae->ad.bits.AttrType = SWAP_DATA16(MODEL);
				elx_str_cpy((char *)ae->un.Model, "LP8000");
				len = elx_str_len((char *)ae->un.Model);
				len += (len & 3) ? (4 - (len & 3)) : 4;
				ae->ad.bits.AttrLen =
				    SWAP_DATA16(FOURBYTES + len);
				ab->EntryCnt++;
				size += FOURBYTES + len;

				/* #5 HBA attribute entry */
				ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh +
							  size);
				ae->ad.bits.AttrType =
				    SWAP_DATA16(MODEL_DESCRIPTION);
				elx_str_cpy((char *)ae->un.ModelDescription,
					    "Emulex LightPulse LP8000 1 Gigabit PCI Fibre Channel Adapter");
				len =
				    elx_str_len((char *)ae->un.
						ModelDescription);
				len += (len & 3) ? (4 - (len & 3)) : 4;
				ae->ad.bits.AttrLen =
				    SWAP_DATA16(FOURBYTES + len);
				ab->EntryCnt++;
				size += FOURBYTES + len;
				break;
			case PCI_DEVICE_ID_CENTAUR:
				if (FC_JEDEC_ID(vp->rev.biuRev) ==
				    CENTAUR_2G_JEDEC_ID) {
					ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh
								  + size);
					ae->ad.bits.AttrType =
					    SWAP_DATA16(MODEL);
					elx_str_cpy((char *)ae->un.Model,
						    "LP9002");
					len = elx_str_len((char *)ae->un.Model);
					len += (len & 3) ? (4 - (len & 3)) : 4;
					ae->ad.bits.AttrLen =
					    SWAP_DATA16(FOURBYTES + len);
					ab->EntryCnt++;
					size += FOURBYTES + len;

					/* #5 HBA attribute entry */
					ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh
								  + size);
					ae->ad.bits.AttrType =
					    SWAP_DATA16(MODEL_DESCRIPTION);
					elx_str_cpy((char *)ae->un.
						    ModelDescription,
						    "Emulex LightPulse LP9002 2 Gigabit PCI Fibre Channel Adapter");
					len =
					    elx_str_len((char *)ae->un.
							ModelDescription);
					len += (len & 3) ? (4 - (len & 3)) : 4;
					ae->ad.bits.AttrLen =
					    SWAP_DATA16(FOURBYTES + len);
					ab->EntryCnt++;
					size += FOURBYTES + len;
				} else {
					ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh
								  + size);
					ae->ad.bits.AttrType =
					    SWAP_DATA16(MODEL);
					elx_str_cpy((char *)ae->un.Model,
						    "LP9000");
					len = elx_str_len((char *)ae->un.Model);
					len += (len & 3) ? (4 - (len & 3)) : 4;
					ae->ad.bits.AttrLen =
					    SWAP_DATA16(FOURBYTES + len);
					ab->EntryCnt++;
					size += FOURBYTES + len;

					/* #5 HBA attribute entry */
					ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh
								  + size);
					ae->ad.bits.AttrType =
					    SWAP_DATA16(MODEL_DESCRIPTION);
					elx_str_cpy((char *)ae->un.
						    ModelDescription,
						    "Emulex LightPulse LP9000 1 Gigabit PCI Fibre Channel Adapter");
					len =
					    elx_str_len((char *)ae->un.
							ModelDescription);
					len += (len & 3) ? (4 - (len & 3)) : 4;
					ae->ad.bits.AttrLen =
					    SWAP_DATA16(FOURBYTES + len);
					ab->EntryCnt++;
					size += FOURBYTES + len;
				}
				break;
			case PCI_DEVICE_ID_RFLY:
				{
					ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh
								  + size);
					ae->ad.bits.AttrType =
					    SWAP_DATA16(MODEL);
					elx_str_cpy((char *)ae->un.Model,
						    "LP952");
					len = elx_str_len((char *)ae->un.Model);
					len += (len & 3) ? (4 - (len & 3)) : 4;
					ae->ad.bits.AttrLen =
					    SWAP_DATA16(FOURBYTES + len);
					ab->EntryCnt++;
					size += FOURBYTES + len;

					/* #5 HBA attribute entry */
					ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh
								  + size);
					ae->ad.bits.AttrType =
					    SWAP_DATA16(MODEL_DESCRIPTION);
					elx_str_cpy((char *)ae->un.
						    ModelDescription,
						    "Emulex LightPulse LP952 2 Gigabit PCI Fibre Channel Adapter");
					len =
					    elx_str_len((char *)ae->un.
							ModelDescription);
					len += (len & 3) ? (4 - (len & 3)) : 4;
					ae->ad.bits.AttrLen =
					    SWAP_DATA16(FOURBYTES + len);
					ab->EntryCnt++;
					size += FOURBYTES + len;
				}
				break;
			case PCI_DEVICE_ID_PEGASUS:
				ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh +
							  size);
				ae->ad.bits.AttrType = SWAP_DATA16(MODEL);
				elx_str_cpy((char *)ae->un.Model, "LP9802");
				len = elx_str_len((char *)ae->un.Model);
				len += (len & 3) ? (4 - (len & 3)) : 4;
				ae->ad.bits.AttrLen =
				    SWAP_DATA16(FOURBYTES + len);
				ab->EntryCnt++;
				size += FOURBYTES + len;

				/* #5 HBA attribute entry */
				ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh +
							  size);
				ae->ad.bits.AttrType =
				    SWAP_DATA16(MODEL_DESCRIPTION);
				elx_str_cpy((char *)ae->un.ModelDescription,
					    "Emulex LightPulse LP9802 2 Gigabit PCI Fibre Channel Adapter");
				len =
				    elx_str_len((char *)ae->un.
						ModelDescription);
				len += (len & 3) ? (4 - (len & 3)) : 4;
				ae->ad.bits.AttrLen =
				    SWAP_DATA16(FOURBYTES + len);
				ab->EntryCnt++;
				size += FOURBYTES + len;
				break;
			case PCI_DEVICE_ID_PFLY:
				ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh +
							  size);
				ae->ad.bits.AttrType = SWAP_DATA16(MODEL);
				elx_str_cpy((char *)ae->un.Model, "LP982");
				len = elx_str_len((char *)ae->un.Model);
				len += (len & 3) ? (4 - (len & 3)) : 4;
				ae->ad.bits.AttrLen =
				    SWAP_DATA16(FOURBYTES + len);
				ab->EntryCnt++;
				size += FOURBYTES + len;

				/* #5 HBA attribute entry */
				ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh +
							  size);
				ae->ad.bits.AttrType =
				    SWAP_DATA16(MODEL_DESCRIPTION);
				elx_str_cpy((char *)ae->un.ModelDescription,
					    "Emulex LightPulse LP982 2 Gigabit PCI Fibre Channel Adapter");
				len =
				    elx_str_len((char *)ae->un.
						ModelDescription);
				len += (len & 3) ? (4 - (len & 3)) : 4;
				ae->ad.bits.AttrLen =
				    SWAP_DATA16(FOURBYTES + len);
				ab->EntryCnt++;
				size += FOURBYTES + len;
				break;
			case PCI_DEVICE_ID_THOR:
				ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh +
							  size);
				ae->ad.bits.AttrType = SWAP_DATA16(MODEL);
				elx_str_cpy((char *)ae->un.Model, "LP10000");
				len = elx_str_len((char *)ae->un.Model);
				len += (len & 3) ? (4 - (len & 3)) : 4;
				ae->ad.bits.AttrLen =
				    SWAP_DATA16(FOURBYTES + len);
				ab->EntryCnt++;
				size += FOURBYTES + len;

				/* #5 HBA attribute entry */
				ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh +
							  size);
				ae->ad.bits.AttrType =
				    SWAP_DATA16(MODEL_DESCRIPTION);
				elx_str_cpy((char *)ae->un.ModelDescription,
					    "Emulex LightPulse LP10000 2 Gigabit PCI Fibre Channel Adapter");
				len =
				    elx_str_len((char *)ae->un.
						ModelDescription);
				len += (len & 3) ? (4 - (len & 3)) : 4;
				ae->ad.bits.AttrLen =
				    SWAP_DATA16(FOURBYTES + len);
				ab->EntryCnt++;
				size += FOURBYTES + len;
				break;
			case PCI_DEVICE_ID_VIPER:
				ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh +
							  size);
				ae->ad.bits.AttrType = SWAP_DATA16(MODEL);
				elx_str_cpy((char *)ae->un.Model, "LPX1000");
				len = elx_str_len((char *)ae->un.Model);
				len += (len & 3) ? (4 - (len & 3)) : 4;
				ae->ad.bits.AttrLen =
				    SWAP_DATA16(FOURBYTES + len);
				ab->EntryCnt++;
				size += FOURBYTES + len;

				/* #5 HBA attribute entry */
				ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh +
							  size);
				ae->ad.bits.AttrType =
				    SWAP_DATA16(MODEL_DESCRIPTION);
				elx_str_cpy((char *)ae->un.ModelDescription,
					    "Emulex LightPulse LPX1000 10 Gigabit PCI Fibre Channel Adapter");
				len =
				    elx_str_len((char *)ae->un.
						ModelDescription);
				len += (len & 3) ? (4 - (len & 3)) : 4;
				ae->ad.bits.AttrLen =
				    SWAP_DATA16(FOURBYTES + len);
				ab->EntryCnt++;
				size += FOURBYTES + len;
				break;
			case PCI_DEVICE_ID_TFLY:
				ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh +
							  size);
				ae->ad.bits.AttrType = SWAP_DATA16(MODEL);
				elx_str_cpy((char *)ae->un.Model, "LP1050");
				len = elx_str_len((char *)ae->un.Model);
				len += (len & 3) ? (4 - (len & 3)) : 4;
				ae->ad.bits.AttrLen =
				    SWAP_DATA16(FOURBYTES + len);
				ab->EntryCnt++;
				size += FOURBYTES + len;

				/* #5 HBA attribute entry */
				ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh +
							  size);
				ae->ad.bits.AttrType =
				    SWAP_DATA16(MODEL_DESCRIPTION);
				elx_str_cpy((char *)ae->un.ModelDescription,
					    "Emulex LightPulse LP1050 2 Gigabit PCI Fibre Channel Adapter");
				len =
				    elx_str_len((char *)ae->un.
						ModelDescription);
				len += (len & 3) ? (4 - (len & 3)) : 4;
				ae->ad.bits.AttrLen =
				    SWAP_DATA16(FOURBYTES + len);
				ab->EntryCnt++;
				size += FOURBYTES + len;
				break;
			case PCI_DEVICE_ID_LP101:
				ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh +
							  size);
				ae->ad.bits.AttrType = SWAP_DATA16(MODEL);
				elx_str_cpy((char *)ae->un.Model, "LP101");
				len = elx_str_len((char *)ae->un.Model);
				len += (len & 3) ? (4 - (len & 3)) : 4;
				ae->ad.bits.AttrLen =
				    SWAP_DATA16(FOURBYTES + len);
				ab->EntryCnt++;
				size += FOURBYTES + len;

				/* #5 HBA attribute entry */
				ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh +
							  size);
				ae->ad.bits.AttrType =
				    SWAP_DATA16(MODEL_DESCRIPTION);
				elx_str_cpy((char *)ae->un.ModelDescription,
					    "Emulex LightPulse LP101 2 Gigabit PCI Fibre Channel Adapter");
				len =
				    elx_str_len((char *)ae->un.
						ModelDescription);
				len += (len & 3) ? (4 - (len & 3)) : 4;
				ae->ad.bits.AttrLen =
				    SWAP_DATA16(FOURBYTES + len);
				ab->EntryCnt++;
				size += FOURBYTES + len;
				break;
			}

			/* #6 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = SWAP_DATA16(HARDWARE_VERSION);
			ae->ad.bits.AttrLen = SWAP_DATA16(FOURBYTES + 8);
			/* Convert JEDEC ID to ascii for hardware version */
			incr = vp->rev.biuRev;
			for (i = 0; i < 8; i++) {
				j = (incr & 0xf);
				if (j <= 9)
					HWrev[7 - i] =
					    (char)((uint8_t) 0x30 +
						   (uint8_t) j);
				else
					HWrev[7 - i] =
					    (char)((uint8_t) 0x61 +
						   (uint8_t) (j - 10));
				incr = (incr >> 4);
			}
			memcpy(ae->un.HardwareVersion, (uint8_t *) HWrev, 8);
			ab->EntryCnt++;
			size += FOURBYTES + 8;

			/* #7 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = SWAP_DATA16(DRIVER_VERSION);
			elx_str_cpy((char *)ae->un.DriverVersion,
				    (char *)lpfc_release_version);
			len = elx_str_len((char *)ae->un.DriverVersion);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = SWAP_DATA16(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #8 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = SWAP_DATA16(OPTION_ROM_VERSION);
			elx_str_cpy((char *)ae->un.OptionROMVersion,
				    (char *)phba->OptionROMVersion);
			len = elx_str_len((char *)ae->un.OptionROMVersion);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = SWAP_DATA16(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #9 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = SWAP_DATA16(FIRMWARE_VERSION);
			lpfc_decode_firmware_rev(phba, lpfc_fwrevision, 1);
			elx_str_cpy((char *)ae->un.FirmwareVersion,
				    (char *)lpfc_fwrevision);
			len = elx_str_len((char *)ae->un.FirmwareVersion);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = SWAP_DATA16(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #10 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = SWAP_DATA16(OS_NAME_VERSION);
			str = lpfc_get_OsNameVersion(GET_OS_VERSION);
			elx_str_cpy((char *)ae->un.OsNameVersion, (char *)str);
			len = elx_str_len((char *)ae->un.OsNameVersion);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = SWAP_DATA16(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #11 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = SWAP_DATA16(MAX_CT_PAYLOAD_LEN);
			ae->ad.bits.AttrLen = SWAP_DATA16(FOURBYTES + 4);
			ae->un.MaxCTPayloadLen = (65 * 4096);
			ab->EntryCnt++;
			size += FOURBYTES + 4;

			ab->EntryCnt = SWAP_DATA(ab->EntryCnt);
			/* Total size */
			size = GID_REQUEST_SZ - 4 + size;
		}
		break;

	case SLI_MGMT_RPA:
		{
			elx_vpd_t *vp;
			SERV_PARM *hsp;
			char *str;
			int len;

			vp = &phba->vpd;

			CtReq->CommandResponse.bits.CmdRsp =
			    SWAP_DATA16(SLI_MGMT_RPA);
			CtReq->CommandResponse.bits.Size = 0;
			pab = (REG_PORT_ATTRIBUTE *) & CtReq->un.PortID;
			size = sizeof (NAME_TYPE) + FOURBYTES;
			memcpy((uint8_t *) & pab->PortName,
			       (uint8_t *) & plhba->fc_sparam.portName,
			       sizeof (NAME_TYPE));
			pab->ab.EntryCnt = 0;

			/* #1 Port attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) pab + size);
			ae->ad.bits.AttrType = SWAP_DATA16(SUPPORTED_FC4_TYPES);
			ae->ad.bits.AttrLen = SWAP_DATA16(FOURBYTES + 32);
			ae->un.SupportFC4Types[2] = 1;
			ae->un.SupportFC4Types[7] = 1;
			pab->ab.EntryCnt++;
			size += FOURBYTES + 32;

			/* #2 Port attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) pab + size);
			ae->ad.bits.AttrType = SWAP_DATA16(SUPPORTED_SPEED);
			ae->ad.bits.AttrLen = SWAP_DATA16(FOURBYTES + 4);
			if (FC_JEDEC_ID(vp->rev.biuRev) == VIPER_JEDEC_ID)
				ae->un.SupportSpeed = HBA_PORTSPEED_10GBIT;
			else if ((FC_JEDEC_ID(vp->rev.biuRev) ==
				  CENTAUR_2G_JEDEC_ID)
				 || (FC_JEDEC_ID(vp->rev.biuRev) ==
				     PEGASUS_JEDEC_ID)
				 || (FC_JEDEC_ID(vp->rev.biuRev) ==
				     THOR_JEDEC_ID))
				ae->un.SupportSpeed = HBA_PORTSPEED_2GBIT;
			else
				ae->un.SupportSpeed = HBA_PORTSPEED_1GBIT;
			pab->ab.EntryCnt++;
			size += FOURBYTES + 4;

			/* #3 Port attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) pab + size);
			ae->ad.bits.AttrType = SWAP_DATA16(PORT_SPEED);
			ae->ad.bits.AttrLen = SWAP_DATA16(FOURBYTES + 4);
			if (plhba->fc_linkspeed == LA_2GHZ_LINK)
				ae->un.PortSpeed = HBA_PORTSPEED_2GBIT;
			else
				ae->un.PortSpeed = HBA_PORTSPEED_1GBIT;
			pab->ab.EntryCnt++;
			size += FOURBYTES + 4;

			/* #4 Port attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) pab + size);
			ae->ad.bits.AttrType = SWAP_DATA16(MAX_FRAME_SIZE);
			ae->ad.bits.AttrLen = SWAP_DATA16(FOURBYTES + 4);
			hsp = (SERV_PARM *) & plhba->fc_sparam;
			ae->un.MaxFrameSize =
			    (((uint32_t) hsp->cmn.
			      bbRcvSizeMsb) << 8) | (uint32_t) hsp->cmn.
			    bbRcvSizeLsb;
			pab->ab.EntryCnt++;
			size += FOURBYTES + 4;

			/* #5 Port attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) pab + size);
			ae->ad.bits.AttrType = SWAP_DATA16(OS_DEVICE_NAME);
			elx_str_cpy((char *)ae->un.OsDeviceName, "lpfcdd");
			len = elx_str_len((char *)ae->un.OsDeviceName);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = SWAP_DATA16(FOURBYTES + len);
			pab->ab.EntryCnt++;
			size += FOURBYTES + len;

			if (clp[LPFC_CFG_FDMI_ON].a_current == 2) {
				/* #6 Port attribute entry */
				ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) pab +
							  size);
				ae->ad.bits.AttrType = SWAP_DATA16(HOST_NAME);
				str = lpfc_get_OsNameVersion(GET_HOST_NAME);
				elx_str_cpy((char *)ae->un.HostName,
					    (char *)str);
				len = elx_str_len((char *)ae->un.HostName);
				len += (len & 3) ? (4 - (len & 3)) : 4;
				ae->ad.bits.AttrLen =
				    SWAP_DATA16(FOURBYTES + len);
				pab->ab.EntryCnt++;
				size += FOURBYTES + len;
			}

			pab->ab.EntryCnt = SWAP_DATA(pab->ab.EntryCnt);
			/* Total size */
			size = GID_REQUEST_SZ - 4 + size;
		}
		break;

	case SLI_MGMT_DHBA:
		CtReq->CommandResponse.bits.CmdRsp = SWAP_DATA16(SLI_MGMT_DHBA);
		CtReq->CommandResponse.bits.Size = 0;
		pe = (PORT_ENTRY *) & CtReq->un.PortID;
		memcpy((uint8_t *) & pe->PortName,
		       (uint8_t *) & plhba->fc_sparam.portName,
		       sizeof (NAME_TYPE));
		size = GID_REQUEST_SZ - 4 + sizeof (NAME_TYPE);
		break;

	case SLI_MGMT_DPRT:
		CtReq->CommandResponse.bits.CmdRsp = SWAP_DATA16(SLI_MGMT_DPRT);
		CtReq->CommandResponse.bits.Size = 0;
		pe = (PORT_ENTRY *) & CtReq->un.PortID;
		memcpy((uint8_t *) & pe->PortName,
		       (uint8_t *) & plhba->fc_sparam.portName,
		       sizeof (NAME_TYPE));
		size = GID_REQUEST_SZ - 4 + sizeof (NAME_TYPE);
		break;
	}

	bpl = (ULP_BDE64 *) bmp->virt;
	bpl->addrHigh = PCIMEM_LONG(putPaddrHigh(mp->phys));
	bpl->addrLow = PCIMEM_LONG(putPaddrLow(mp->phys));
	bpl->tus.f.bdeFlags = 0;
	bpl->tus.f.bdeSize = size;
	bpl->tus.w = PCIMEM_LONG(bpl->tus.w);

	cmpl = lpfc_cmpl_ct_cmd_fdmi;

	if (lpfc_ct_cmd(phba, mp, bmp, ndlp, cmpl)) {
		elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
		elx_mem_put(phba, MEM_BPL, (uint8_t *) bmp);
		/* Issue FDMI request failed */
		elx_printf_log(phba->brd_no, &elx_msgBlk0244,	/* ptr to msg structure */
			       elx_mes0244,	/* ptr to msg */
			       elx_msgBlk0244.msgPreambleStr,	/* begin varargs */
			       cmdcode);	/* end varargs */
		return (1);
	}
	return (0);
}

void
lpfc_cmpl_ct_cmd_fdmi(elxHBA_t * phba,
		      ELX_IOCBQ_t * cmdiocb, ELX_IOCBQ_t * rspiocb)
{
	LPFCHBA_t *plhba;
	DMABUF_t *bmp;
	DMABUF_t *inp;
	DMABUF_t *outp;
	SLI_CT_REQUEST *CTrsp;
	SLI_CT_REQUEST *CTcmd;
	LPFC_NODELIST_t *ndlp;
	uint16_t fdmi_cmd;
	uint16_t fdmi_rsp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;

	inp = (DMABUF_t *) cmdiocb->context1;
	outp = (DMABUF_t *) cmdiocb->context2;
	bmp = (DMABUF_t *) cmdiocb->context3;

	CTcmd = (SLI_CT_REQUEST *) inp->virt;
	CTrsp = (SLI_CT_REQUEST *) outp->virt;
	ndlp = (LPFC_NODELIST_t *) inp->next;

	fdmi_rsp = CTrsp->CommandResponse.bits.CmdRsp;
	fdmi_cmd = CTcmd->CommandResponse.bits.CmdRsp;

	if (fdmi_rsp == SWAP_DATA16(SLI_CT_RESPONSE_FS_RJT)) {
		/* FDMI rsp failed */
		elx_printf_log(phba->brd_no, &elx_msgBlk0220,	/* ptr to msg structure */
			       elx_mes0220,	/* ptr to msg */
			       elx_msgBlk0220.msgPreambleStr,	/* begin varargs */
			       SWAP_DATA16(fdmi_cmd));	/* end varargs */
	}

	switch (SWAP_DATA16(fdmi_cmd)) {
	case SLI_MGMT_RHBA:
		lpfc_fdmi_cmd(phba, ndlp, SLI_MGMT_RPA);
		break;

	case SLI_MGMT_RPA:
		break;

	case SLI_MGMT_DHBA:
		lpfc_fdmi_cmd(phba, ndlp, SLI_MGMT_DPRT);
		break;

	case SLI_MGMT_DPRT:
		lpfc_fdmi_cmd(phba, ndlp, SLI_MGMT_RHBA);
		break;
	}

	lpfc_free_ct_rsp(phba, outp);
	elx_mem_put(phba, MEM_BUF, (uint8_t *) inp);
	elx_mem_put(phba, MEM_BPL, (uint8_t *) bmp);
	elx_mem_put(phba, MEM_IOCB, (uint8_t *) cmdiocb);
	return;
}

void
lpfc_fdmi_tmo(elxHBA_t * phba, void *arg1, void *arg2)
{
	LPFCHBA_t *plhba;
	LPFC_NODELIST_t *ndlp;
	int ret;

	ndlp = (LPFC_NODELIST_t *) arg1;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	ret = lpfc_utsname_nodename_check();
	if (ret) {
		plhba->fc_fdmitmo =
		    elx_clk_set(phba, 60, lpfc_fdmi_tmo, ndlp, 0);
		return;
	}
	plhba->fc_fdmitmo = 0;
	lpfc_fdmi_cmd(phba, ndlp, SLI_MGMT_DHBA);
	return;
}

#define TRUE    1
#define FALSE   0

int lpfc_els_rcv_rscn(elxHBA_t *, ELX_IOCBQ_t *, LPFC_NODELIST_t *);
int lpfc_els_rcv_flogi(elxHBA_t *, ELX_IOCBQ_t *, LPFC_NODELIST_t *);
int lpfc_els_rcv_rrq(elxHBA_t *, ELX_IOCBQ_t *, LPFC_NODELIST_t *);
int lpfc_els_rcv_rnid(elxHBA_t *, ELX_IOCBQ_t *, LPFC_NODELIST_t *);
int lpfc_els_rcv_farp(elxHBA_t *, ELX_IOCBQ_t *, LPFC_NODELIST_t *);
int lpfc_els_rcv_farpr(elxHBA_t *, ELX_IOCBQ_t *, LPFC_NODELIST_t *);
int lpfc_els_rcv_fan(elxHBA_t *, ELX_IOCBQ_t *, LPFC_NODELIST_t *);

int lpfc_max_els_tries = 3;

int
lpfc_initial_flogi(elxHBA_t * phba)
{
	LPFC_NODELIST_t *ndlp;
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	/* First look for Fabric ndlp on the unmapped list */

	if ((ndlp =
	     lpfc_findnode_did(phba, (NLP_SEARCH_UNMAPPED | NLP_SEARCH_DEQUE),
			       Fabric_DID)) == 0) {
		/* Cannot find existing Fabric ndlp, so allocate a new one */
		if ((ndlp =
		     (LPFC_NODELIST_t *) elx_mem_get(phba, MEM_NLP)) == 0) {
			return (0);
		}
		memset((void *)ndlp, 0, sizeof (LPFC_NODELIST_t));
		ndlp->nlp_DID = Fabric_DID;
	}
	if (lpfc_issue_els_flogi(phba, ndlp, 0)) {
		elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
	}
	return (1);
}

int
lpfc_issue_els_flogi(elxHBA_t * phba, LPFC_NODELIST_t * ndlp, uint8_t retry)
{
	SERV_PARM *sp;
	IOCB_t *icmd;
	ELX_IOCBQ_t *elsiocb;
	ELX_SLI_RING_t *pring;
	ELX_SLI_t *psli;
	LPFCHBA_t *plhba;
	uint8_t *pCmd;
	uint16_t cmdsize;
	uint32_t tmo;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	cmdsize = (sizeof (uint32_t) + sizeof (SERV_PARM));
	if ((elsiocb = lpfc_prep_els_iocb(phba, TRUE, cmdsize, retry,
					  ndlp, ELS_CMD_FLOGI)) == 0) {
		return (1);
	}

	icmd = &elsiocb->iocb;
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

	/* For FLOGI request, remainder of payload is service parameters */
	*((uint32_t *) (pCmd)) = ELS_CMD_FLOGI;
	pCmd += sizeof (uint32_t);
	memcpy((void *)pCmd, (void *)&plhba->fc_sparam, sizeof (SERV_PARM));
	sp = (SERV_PARM *) pCmd;

	/* Setup CSPs accordingly for Fabric */
	sp->cmn.e_d_tov = 0;
	sp->cmn.w2.r_a_tov = 0;
	sp->cls1.classValid = 0;
	sp->cls2.seqDelivery = 1;
	sp->cls3.seqDelivery = 1;
	if (sp->cmn.fcphLow < FC_PH3)
		sp->cmn.fcphLow = FC_PH3;
	if (sp->cmn.fcphHigh < FC_PH3)
		sp->cmn.fcphHigh = FC_PH3;

	tmo = plhba->fc_ratov;
	plhba->fc_ratov = LPFC_DISC_FLOGI_TMO;
	lpfc_set_disctmo(phba);
	plhba->fc_ratov = tmo;

	elx_pci_dma_sync((void *)phba, (void *)(elsiocb->context2),
			 0, ELX_DMA_SYNC_FORDEV);

	plhba->fc_stat.elsXmitFLOGI++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_flogi;
	if (elx_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	return (0);
}

void
lpfc_cmpl_els_flogi(elxHBA_t * phba,
		    ELX_IOCBQ_t * cmdiocb, ELX_IOCBQ_t * rspiocb)
{
	LPFCHBA_t *plhba;
	IOCB_t *irsp;
	DMABUF_t *pCmd, *pRsp;
	SERV_PARM *sp;
	uint32_t *lp;
	ELX_MBOXQ_t *mbox;
	ELX_SLI_t *psli;
	LPFC_NODELIST_t *ndlp;
	elxCfgParam_t *clp;
	uint32_t rc;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;
	irsp = &(rspiocb->iocb);
	ndlp = (LPFC_NODELIST_t *) cmdiocb->context1;
	pCmd = (DMABUF_t *) cmdiocb->context2;

	clp = &phba->config[0];

	/* Check to see if link went down during discovery */
	lpfc_els_chk_latt(phba, rspiocb);

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (lpfc_els_retry(phba, cmdiocb, rspiocb)) {
			/* ELS command is being retried */
			goto out;
		}
		/* FLOGI failed, so there is no fabric */
		plhba->fc_flag &= ~(FC_FABRIC | FC_PUBLIC_LOOP);

		/* If private loop, then allow max outstandting els to be
		 * LPFC_MAX_DISC_THREADS (32). Scanning in the case of no 
		 * alpa map would take too long otherwise. 
		 */
		if (plhba->alpa_map[0] == 0) {
			clp[LPFC_CFG_DISC_THREADS].a_current =
			    LPFC_MAX_DISC_THREADS;
		}

		/* FLOGI failure */
		elx_printf_log(phba->brd_no, &elx_msgBlk0100,	/* ptr to msg structure */
			       elx_mes0100,	/* ptr to msg */
			       elx_msgBlk0100.msgPreambleStr,	/* begin varargs */
			       irsp->ulpStatus, irsp->un.ulpWord[4]);	/* end varargs */
	} else {
		pRsp = (DMABUF_t *) pCmd->next;
		/* Good status */
		lp = (uint32_t *) pRsp->virt;
		elx_pci_dma_sync((void *)phba, (void *)pRsp,
				 0, ELX_DMA_SYNC_FORCPU);
		sp = (SERV_PARM *) ((uint8_t *) lp + sizeof (uint32_t));

		/* FLOGI completes successfully */
		elx_printf_log(phba->brd_no, &elx_msgBlk0101,	/* ptr to msg structure */
			       elx_mes0101,	/* ptr to msg */
			       elx_msgBlk0101.msgPreambleStr,	/* begin varargs */
			       irsp->un.ulpWord[4], sp->cmn.e_d_tov, sp->cmn.w2.r_a_tov, sp->cmn.edtovResolution);	/* end varargs */

		if (phba->hba_state == ELX_FLOGI) {
			/* If Common Service Parameters indicate Nport
			 * we are point to point, if Fport we are Fabric.
			 */
			if (sp->cmn.fPort) {
				plhba->fc_flag |= FC_FABRIC;
				if (sp->cmn.edtovResolution) {
					/* E_D_TOV ticks are in nanoseconds */
					plhba->fc_edtov =
					    (SWAP_DATA(sp->cmn.e_d_tov) +
					     999999) / 1000000;
				} else {
					/* E_D_TOV ticks are in milliseconds */
					plhba->fc_edtov =
					    SWAP_DATA(sp->cmn.e_d_tov);
				}
				plhba->fc_ratov =
				    (SWAP_DATA(sp->cmn.w2.r_a_tov) +
				     999) / 1000;
				phba->fcp_timeout_offset =
				    2 * plhba->fc_ratov +
				    clp[ELX_CFG_EXTRA_IO_TMO].a_current;

				if (plhba->fc_topology == TOPOLOGY_LOOP) {
					plhba->fc_flag |= FC_PUBLIC_LOOP;
				} else {
					/* If we are a N-port connected to a Fabric, 
					 * fixup sparam's so logins to devices on
					 * remote loops work.
					 */
					plhba->fc_sparam.cmn.altBbCredit = 1;
				}

				plhba->fc_myDID =
				    irsp->un.ulpWord[4] & Mask_DID;

				memcpy((void *)&ndlp->nlp_portname,
				       (void *)&sp->portName,
				       sizeof (NAME_TYPE));
				memcpy((void *)&ndlp->nlp_nodename,
				       (void *)&sp->nodeName,
				       sizeof (NAME_TYPE));
				memcpy((void *)&plhba->fc_fabparam, (void *)sp,
				       sizeof (SERV_PARM));
				if ((mbox =
				     (ELX_MBOXQ_t *) elx_mem_get(phba,
								 MEM_MBOX)) ==
				    0) {
					goto flogifail;
				}
				phba->hba_state = ELX_FABRIC_CFG_LINK;
				lpfc_config_link(phba, mbox);
				if (elx_sli_issue_mbox
				    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
				    == MBX_NOT_FINISHED) {
					elx_mem_put(phba, MEM_MBOX,
						    (uint8_t *) mbox);
					goto flogifail;
				}

				if ((mbox =
				     (ELX_MBOXQ_t *) elx_mem_get(phba,
								 MEM_MBOX)) ==
				    0) {
					goto flogifail;
				}
				if (lpfc_reg_login(phba, Fabric_DID,
						   (uint8_t *) sp, mbox,
						   0) == 0) {
					/* set_slim mailbox command needs to execute first,
					 * queue this command to be processed later.
					 */
					mbox->mbox_cmpl =
					    lpfc_mbx_cmpl_fabric_reg_login;
					mbox->context2 = (void *)ndlp;
					if (elx_sli_issue_mbox
					    (phba, mbox,
					     (MBX_NOWAIT | MBX_STOP_IOCB))
					    == MBX_NOT_FINISHED) {
						elx_mem_put(phba, MEM_MBOX,
							    (uint8_t *) mbox);
						goto flogifail;
					}
				} else {
					elx_mem_put(phba, MEM_MBOX,
						    (uint8_t *) mbox);
					goto flogifail;
				}
			} else {
				/* We FLOGIed into an NPort, initiate pt2pt protocol */
				plhba->fc_flag &= ~(FC_FABRIC | FC_PUBLIC_LOOP);
				plhba->fc_edtov = FF_DEF_EDTOV;
				plhba->fc_ratov = FF_DEF_RATOV;
				phba->fcp_timeout_offset = 2 * plhba->fc_ratov +
				    clp[ELX_CFG_EXTRA_IO_TMO].a_current;
				if ((rc =
				     lpfc_geportname((NAME_TYPE *) & plhba->
						     fc_portname,
						     (NAME_TYPE *) & sp->
						     portName))) {
					/* This side will initiate the PLOGI */
					plhba->fc_flag |= FC_PT2PT_PLOGI;

					/* N_Port ID cannot be 0, set our to LocalID the 
					 * other side will be RemoteID.
					 */

					/* not equal */
					if (rc == 1)
						plhba->fc_myDID = PT2PT_LocalID;
					rc = 0;

					if ((mbox =
					     (ELX_MBOXQ_t *) elx_mem_get(phba,
									 MEM_MBOX))
					    == 0) {
						goto flogifail;
					}
					lpfc_config_link(phba, mbox);
					if (elx_sli_issue_mbox
					    (phba, mbox,
					     (MBX_NOWAIT | MBX_STOP_IOCB))
					    == MBX_NOT_FINISHED) {
						elx_mem_put(phba, MEM_MBOX,
							    (uint8_t *) mbox);
						goto flogifail;
					}
					elx_mem_put(phba, MEM_NLP,
						    (uint8_t *) ndlp);

					if ((ndlp =
					     lpfc_findnode_did(phba,
							       NLP_SEARCH_ALL,
							       PT2PT_RemoteID))
					    == 0) {
						/* Cannot find existing Fabric ndlp, so allocate a new one */
						if ((ndlp =
						     (LPFC_NODELIST_t *)
						     elx_mem_get(phba,
								 MEM_NLP)) ==
						    0) {
							goto flogifail;
						}
						memset((void *)ndlp, 0,
						       sizeof
						       (LPFC_NODELIST_t));
						ndlp->nlp_DID = PT2PT_RemoteID;
					}
					memcpy((void *)&ndlp->nlp_portname,
					       (void *)&sp->portName,
					       sizeof (NAME_TYPE));
					memcpy((void *)&ndlp->nlp_nodename,
					       (void *)&sp->nodeName,
					       sizeof (NAME_TYPE));
					lpfc_nlp_plogi(phba, ndlp);
				} else {
					/* This side will wait for the PLOGI */
					elx_mem_put(phba, MEM_NLP,
						    (uint8_t *) ndlp);
				}

				plhba->fc_flag |= FC_PT2PT;
				lpfc_set_disctmo(phba);

				/* Start discovery - this should just do CLEAR_LA */
				lpfc_disc_start(phba);
			}
			goto out;
		}
	}

      flogifail:
	elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);

	/* FLOGI failed, so just use loop map to make discovery list */
	lpfc_disc_list_loopmap(phba);

	/* Start discovery */
	lpfc_disc_start(phba);

      out:
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_issue_els_plogi(elxHBA_t * phba, LPFC_NODELIST_t * ndlp, uint8_t retry)
{
	SERV_PARM *sp;
	IOCB_t *icmd;
	ELX_IOCBQ_t *elsiocb;
	ELX_SLI_RING_t *pring;
	ELX_SLI_t *psli;
	LPFCHBA_t *plhba;
	uint8_t *pCmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	cmdsize = (sizeof (uint32_t) + sizeof (SERV_PARM));
	if ((elsiocb = lpfc_prep_els_iocb(phba, TRUE, cmdsize, retry,
					  ndlp, ELS_CMD_PLOGI)) == 0) {
		return (1);
	}

	icmd = &elsiocb->iocb;
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

	/* For PLOGI request, remainder of payload is service parameters */
	*((uint32_t *) (pCmd)) = ELS_CMD_PLOGI;
	pCmd += sizeof (uint32_t);

	/* For LOGI request, remainder of payload is service parameters */
	memcpy((void *)pCmd, (void *)&plhba->fc_sparam, sizeof (SERV_PARM));
	sp = (SERV_PARM *) pCmd;

	if (sp->cmn.fcphLow < FC_PH_4_3)
		sp->cmn.fcphLow = FC_PH_4_3;

	if (sp->cmn.fcphHigh < FC_PH3)
		sp->cmn.fcphHigh = FC_PH3;

	elx_pci_dma_sync((void *)phba, (void *)(elsiocb->context2),
			 0, ELX_DMA_SYNC_FORDEV);

	plhba->fc_stat.elsXmitPLOGI++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_plogi;
	ndlp->nlp_flag |= NLP_PLOGI_SND;
	if (elx_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		ndlp->nlp_flag &= ~NLP_PLOGI_SND;
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	return (0);
}

void
lpfc_cmpl_els_plogi(elxHBA_t * phba,
		    ELX_IOCBQ_t * cmdiocb, ELX_IOCBQ_t * rspiocb)
{
	IOCB_t *irsp;
	ELX_SLI_t *psli;
	LPFC_NODELIST_t *ndlp;
	LPFCHBA_t *plhba;
	int disc;

	psli = &phba->sli;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->q_f = rspiocb;

	irsp = &rspiocb->iocb;
	ndlp = (LPFC_NODELIST_t *) cmdiocb->context1;
	ndlp->nlp_flag &= ~NLP_PLOGI_SND;

	/* Since ndlp can be freed in the disc state machine, note if this node
	 * is being used during discovery.
	 */
	disc = (ndlp->nlp_flag & NLP_DISC_NODE);
	ndlp->nlp_flag &= ~NLP_DISC_NODE;

	/* PLOGI completes to NPort <nlp_DID> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0102,	/* ptr to msg structure */
		       elx_mes0102,	/* ptr to msg */
		       elx_msgBlk0102.msgPreambleStr,	/* begin varargs */
		       ndlp->nlp_DID, irsp->ulpStatus, irsp->un.ulpWord[4], disc, plhba->num_disc_nodes);	/* end varargs */

	/* Check to see if link went down during discovery */
	lpfc_els_chk_latt(phba, rspiocb);

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (lpfc_els_retry(phba, cmdiocb, rspiocb)) {
			/* ELS command is being retried */
			if (disc) {
				ndlp->nlp_flag |= NLP_DISC_NODE;
			}
			goto out;
		}

		/* PLOGI failed */
		lpfc_disc_state_machine(phba, ndlp, (void *)cmdiocb,
					NLP_EVT_CMPL_PLOGI);
	} else {
		/* Good status, call state machine */
		lpfc_disc_state_machine(phba, ndlp, (void *)cmdiocb,
					NLP_EVT_CMPL_PLOGI);
	}

	if (disc && plhba->num_disc_nodes) {
		/* Check to see if there are more PLOGIs to be sent */
		lpfc_more_plogi(phba);
	}

	if (plhba->num_disc_nodes == 0) {
		if (disc) {
			phba->hba_flag &= ~FC_NDISC_ACTIVE;
		}
		lpfc_can_disctmo(phba);
		if (plhba->fc_flag & FC_RSCN_MODE) {
			/* Check to see if more RSCNs came in while we were
			 * processing this one.
			 */
			if ((plhba->fc_rscn_id_cnt == 0) &&
			    (!(plhba->fc_flag & FC_RSCN_DISCOVERY))) {
				lpfc_els_flush_rscn(phba);
			} else {
				lpfc_els_handle_rscn(phba);
			}
		}
	}
      out:
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_issue_els_prli(elxHBA_t * phba, LPFC_NODELIST_t * ndlp, uint8_t retry)
{
	PRLI *npr;
	IOCB_t *icmd;
	ELX_IOCBQ_t *elsiocb;
	ELX_SLI_RING_t *pring;
	ELX_SLI_t *psli;
	LPFCHBA_t *plhba;
	uint8_t *pCmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	cmdsize = (sizeof (uint32_t) + sizeof (PRLI));
	if ((elsiocb = lpfc_prep_els_iocb(phba, TRUE, cmdsize, retry,
					  ndlp, ELS_CMD_PRLI)) == 0) {
		return (1);
	}

	icmd = &elsiocb->iocb;
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

	/* For PRLI request, remainder of payload is service parameters */
	memset((void *)pCmd, 0, (sizeof (PRLI) + sizeof (uint32_t)));
	*((uint32_t *) (pCmd)) = ELS_CMD_PRLI;
	pCmd += sizeof (uint32_t);

	/* For PRLI, remainder of payload is PRLI parameter page */
	npr = (PRLI *) pCmd;
	/*
	 * If our firmware version is 3.20 or later, 
	 * set the following bits for FC-TAPE support.
	 */
	if (phba->vpd.rev.feaLevelHigh >= 0x02) {
		npr->ConfmComplAllowed = 1;
		npr->Retry = 1;
		npr->TaskRetryIdReq = 1;
	}
	npr->estabImagePair = 1;
	npr->readXferRdyDis = 1;

	/* For FCP support */
	npr->prliType = PRLI_FCP_TYPE;
	npr->initiatorFunc = 1;

	elx_pci_dma_sync((void *)phba, (void *)(elsiocb->context2),
			 0, ELX_DMA_SYNC_FORDEV);

	plhba->fc_stat.elsXmitPRLI++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_prli;
	ndlp->nlp_flag |= NLP_PRLI_SND;
	if (elx_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		ndlp->nlp_flag &= ~NLP_PRLI_SND;
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	plhba->fc_prli_sent++;
	return (0);
}

void
lpfc_cmpl_els_prli(elxHBA_t * phba,
		   ELX_IOCBQ_t * cmdiocb, ELX_IOCBQ_t * rspiocb)
{
	LPFCHBA_t *plhba;
	IOCB_t *irsp;
	ELX_SLI_t *psli;
	LPFC_NODELIST_t *ndlp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;
	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->q_f = rspiocb;

	irsp = &(rspiocb->iocb);
	ndlp = (LPFC_NODELIST_t *) cmdiocb->context1;
	ndlp->nlp_flag &= ~NLP_PRLI_SND;

	/* PRLI completes to NPort <nlp_DID> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0103,	/* ptr to msg structure */
		       elx_mes0103,	/* ptr to msg */
		       elx_msgBlk0103.msgPreambleStr,	/* begin varargs */
		       ndlp->nlp_DID, irsp->ulpStatus, irsp->un.ulpWord[4], plhba->num_disc_nodes);	/* end varargs */

	plhba->fc_prli_sent--;
	/* Check to see if link went down during discovery */
	lpfc_els_chk_latt(phba, rspiocb);

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (lpfc_els_retry(phba, cmdiocb, rspiocb)) {
			/* ELS command is being retried */
			goto out;
		}
		/* PRLI failed */
		lpfc_disc_state_machine(phba, ndlp, (void *)cmdiocb,
					NLP_EVT_CMPL_PRLI);
	} else {
		/* Good status, call state machine */
		lpfc_disc_state_machine(phba, ndlp, (void *)cmdiocb,
					NLP_EVT_CMPL_PRLI);
	}
      out:
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_issue_els_adisc(elxHBA_t * phba, LPFC_NODELIST_t * ndlp, uint8_t retry)
{
	ADISC *ap;
	IOCB_t *icmd;
	ELX_IOCBQ_t *elsiocb;
	ELX_SLI_RING_t *pring;
	ELX_SLI_t *psli;
	LPFCHBA_t *plhba;
	uint8_t *pCmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	cmdsize = (sizeof (uint32_t) + sizeof (ADISC));
	if ((elsiocb = lpfc_prep_els_iocb(phba, TRUE, cmdsize, retry,
					  ndlp, ELS_CMD_ADISC)) == 0) {
		return (1);
	}

	icmd = &elsiocb->iocb;
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

	/* For ADISC request, remainder of payload is service parameters */
	*((uint32_t *) (pCmd)) = ELS_CMD_ADISC;
	pCmd += sizeof (uint32_t);

	/* Fill in ADISC payload */
	ap = (ADISC *) pCmd;
	ap->hardAL_PA = plhba->fc_pref_ALPA;
	memcpy((void *)&ap->portName, (void *)&plhba->fc_portname,
	       sizeof (NAME_TYPE));
	memcpy((void *)&ap->nodeName, (void *)&plhba->fc_nodename,
	       sizeof (NAME_TYPE));
	ap->DID = SWAP_DATA(plhba->fc_myDID);

	elx_pci_dma_sync((void *)phba, (void *)(elsiocb->context2),
			 0, ELX_DMA_SYNC_FORDEV);

	plhba->fc_stat.elsXmitADISC++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_adisc;
	ndlp->nlp_flag |= NLP_ADISC_SND;
	if (elx_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		ndlp->nlp_flag &= ~NLP_ADISC_SND;
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	return (0);
}

void
lpfc_cmpl_els_adisc(elxHBA_t * phba,
		    ELX_IOCBQ_t * cmdiocb, ELX_IOCBQ_t * rspiocb)
{
	IOCB_t *irsp;
	ELX_SLI_t *psli;
	LPFC_NODELIST_t *ndlp;
	LPFCHBA_t *plhba;
	ELX_MBOXQ_t *mbox;
	int disc;
	elxCfgParam_t *clp;

	clp = &phba->config[0];
	psli = &phba->sli;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->q_f = rspiocb;

	irsp = &(rspiocb->iocb);
	ndlp = (LPFC_NODELIST_t *) cmdiocb->context1;
	ndlp->nlp_flag &= ~NLP_ADISC_SND;

	/* Since ndlp can be freed in the disc state machine, note if this node
	 * is being used during discovery.
	 */
	disc = (ndlp->nlp_flag & NLP_DISC_NODE);
	ndlp->nlp_flag &= ~NLP_DISC_NODE;

	/* ADISC completes to NPort <nlp_DID> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0104,	/* ptr to msg structure */
		       elx_mes0104,	/* ptr to msg */
		       elx_msgBlk0104.msgPreambleStr,	/* begin varargs */
		       ndlp->nlp_DID, irsp->ulpStatus, irsp->un.ulpWord[4], disc, plhba->num_disc_nodes);	/* end varargs */

	/* Check to see if link went down during discovery */
	lpfc_els_chk_latt(phba, rspiocb);

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (lpfc_els_retry(phba, cmdiocb, rspiocb)) {
			/* ELS command is being retried */
			if (disc) {
				ndlp->nlp_flag |= NLP_DISC_NODE;
				lpfc_set_disctmo(phba);
			}
			goto out;
		}
		/* ADISC failed */
		lpfc_disc_state_machine(phba, ndlp, (void *)cmdiocb,
					NLP_EVT_CMPL_ADISC);
	} else {
		/* Good status, call state machine */
		lpfc_disc_state_machine(phba, ndlp, (void *)cmdiocb,
					NLP_EVT_CMPL_ADISC);
	}

	if (disc && plhba->num_disc_nodes) {
		/* Check to see if there are more ADISCs to be sent */
		lpfc_more_adisc(phba);

		/* Check to see if we are done with ADISC authentication */
		if (plhba->num_disc_nodes == 0) {
			/* If we get here, there is nothing left to wait for */
			if ((phba->hba_state < ELX_HBA_READY) &&
			    (phba->hba_state != ELX_CLEAR_LA)) {
				/* Link up discovery */
				if ((mbox =
				     (ELX_MBOXQ_t *) elx_mem_get(phba,
								 MEM_MBOX |
								 MEM_PRI))) {
					phba->hba_state = ELX_CLEAR_LA;
					lpfc_clear_la(phba, mbox);
					mbox->mbox_cmpl =
					    lpfc_mbx_cmpl_clear_la;
					if (elx_sli_issue_mbox
					    (phba, mbox,
					     (MBX_NOWAIT | MBX_STOP_IOCB))
					    == MBX_NOT_FINISHED) {
						elx_mem_put(phba, MEM_MBOX,
							    (uint8_t *) mbox);
						lpfc_disc_flush_list(phba);
						psli->ring[(psli->ip_ring)].
						    flag &=
						    ~ELX_STOP_IOCB_EVENT;
						psli->ring[(psli->fcp_ring)].
						    flag &=
						    ~ELX_STOP_IOCB_EVENT;
						psli->ring[(psli->next_ring)].
						    flag &=
						    ~ELX_STOP_IOCB_EVENT;
						phba->hba_state = ELX_HBA_READY;
					}
				}
			} else {
				/* RSCN discovery */
				/* go thru PLOGI list and issue ELS PLOGIs */
				if (plhba->fc_plogi_cnt) {
					ndlp = plhba->fc_plogi_start;
					while (ndlp !=
					       (LPFC_NODELIST_t *) & plhba->
					       fc_plogi_start) {
						if (ndlp->nlp_state ==
						    NLP_STE_UNUSED_NODE) {
							ndlp->nlp_state =
							    NLP_STE_PLOGI_ISSUE;
							lpfc_issue_els_plogi
							    (phba, ndlp, 0);
							ndlp->nlp_flag |=
							    NLP_DISC_NODE;
							plhba->num_disc_nodes++;
							if (plhba->
							    num_disc_nodes >=
							    clp
							    [LPFC_CFG_DISC_THREADS].
							    a_current) {
								if (plhba->
								    fc_plogi_cnt
								    >
								    plhba->
								    num_disc_nodes)
									plhba->
									    fc_flag
									    |=
									    FC_NLP_MORE;
								break;
							}
						}
						ndlp =
						    (LPFC_NODELIST_t *) ndlp->
						    nle.nlp_listp_next;
					}
				} else {
					if (plhba->fc_flag & FC_RSCN_MODE) {
						/* Check to see if more RSCNs came in while we were
						 * processing this one.
						 */
						if ((plhba->fc_rscn_id_cnt == 0)
						    &&
						    (!(plhba->
						       fc_flag &
						       FC_RSCN_DISCOVERY))) {
							lpfc_els_flush_rscn
							    (phba);
						} else {
							lpfc_els_handle_rscn
							    (phba);
						}
					}
				}
			}
		}
	}
      out:
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_issue_els_logo(elxHBA_t * phba, LPFC_NODELIST_t * ndlp, uint8_t retry)
{
	IOCB_t *icmd;
	ELX_IOCBQ_t *elsiocb;
	ELX_SLI_RING_t *pring;
	ELX_SLI_t *psli;
	LPFCHBA_t *plhba;
	uint8_t *pCmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	cmdsize = 2 * (sizeof (uint32_t) + sizeof (NAME_TYPE));
	if ((elsiocb = lpfc_prep_els_iocb(phba, TRUE, cmdsize, retry,
					  ndlp, ELS_CMD_LOGO)) == 0) {
		return (1);
	}

	icmd = &elsiocb->iocb;
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);
	*((uint32_t *) (pCmd)) = ELS_CMD_LOGO;
	pCmd += sizeof (uint32_t);

	/* Fill in LOGO payload */
	*((uint32_t *) (pCmd)) = SWAP_DATA(plhba->fc_myDID);
	pCmd += sizeof (uint32_t);
	memcpy((void *)pCmd, (void *)&plhba->fc_portname, sizeof (NAME_TYPE));

	elx_pci_dma_sync((void *)phba, (void *)(elsiocb->context2),
			 0, ELX_DMA_SYNC_FORDEV);

	plhba->fc_stat.elsXmitLOGO++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_logo;
	ndlp->nlp_flag |= NLP_LOGO_SND;
	if (elx_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		ndlp->nlp_flag &= ~NLP_LOGO_SND;
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	return (0);
}

void
lpfc_cmpl_els_logo(elxHBA_t * phba,
		   ELX_IOCBQ_t * cmdiocb, ELX_IOCBQ_t * rspiocb)
{
	IOCB_t *irsp;
	ELX_SLI_t *psli;
	LPFC_NODELIST_t *ndlp;
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;
	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->q_f = rspiocb;

	irsp = &(rspiocb->iocb);
	ndlp = (LPFC_NODELIST_t *) cmdiocb->context1;
	ndlp->nlp_flag &= ~NLP_LOGO_SND;

	/* LOGO completes to NPort <nlp_DID> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0105,	/* ptr to msg structure */
		       elx_mes0105,	/* ptr to msg */
		       elx_msgBlk0105.msgPreambleStr,	/* begin varargs */
		       ndlp->nlp_DID, irsp->ulpStatus, irsp->un.ulpWord[4], plhba->num_disc_nodes);	/* end varargs */

	/* Check to see if link went down during discovery */
	lpfc_els_chk_latt(phba, rspiocb);

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (lpfc_els_retry(phba, cmdiocb, rspiocb)) {
			/* ELS command is being retried */
			goto out;
		}
		/* LOGO failed */
		lpfc_disc_state_machine(phba, ndlp, (void *)cmdiocb,
					NLP_EVT_CMPL_LOGO);
	} else {
		/* Good status, call state machine */
		lpfc_disc_state_machine(phba, ndlp, (void *)cmdiocb,
					NLP_EVT_CMPL_LOGO);
	}
      out:
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_issue_els_scr(elxHBA_t * phba, uint32_t nportid, uint8_t retry)
{
	IOCB_t *icmd;
	ELX_IOCBQ_t *elsiocb;
	ELX_SLI_RING_t *pring;
	ELX_SLI_t *psli;
	LPFCHBA_t *plhba;
	uint8_t *pCmd;
	uint16_t cmdsize;
	LPFC_NODELIST_t *ndlp;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	cmdsize = (sizeof (uint32_t) + sizeof (SCR));
	if ((ndlp = (LPFC_NODELIST_t *) elx_mem_get(phba, MEM_NLP)) == 0) {
		return (1);
	}
	memset((void *)ndlp, 0, sizeof (LPFC_NODELIST_t));
	ndlp->nlp_DID = nportid;

	if ((elsiocb = lpfc_prep_els_iocb(phba, TRUE, cmdsize, retry,
					  ndlp, ELS_CMD_SCR)) == 0) {
		elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
		return (1);
	}

	icmd = &elsiocb->iocb;
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

	*((uint32_t *) (pCmd)) = ELS_CMD_SCR;
	pCmd += sizeof (uint32_t);

	/* For SCR, remainder of payload is SCR parameter page */
	memset((void *)pCmd, 0, sizeof (SCR));
	((SCR *) pCmd)->Function = SCR_FUNC_FULL;

	elx_pci_dma_sync((void *)phba, (void *)(elsiocb->context2),
			 0, ELX_DMA_SYNC_FORDEV);

	plhba->fc_stat.elsXmitSCR++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_cmd;
	if (elx_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
	return (0);
}

int
lpfc_issue_els_farp(elxHBA_t * phba, uint8_t * arg, LPFC_FARP_ADDR_TYPE argFlag)
{
	IOCB_t *icmd;
	ELX_IOCBQ_t *elsiocb;
	ELX_SLI_RING_t *pring;
	ELX_SLI_t *psli;
	LPFCHBA_t *plhba;
	FARP *fp;
	uint8_t *pCmd;
	uint32_t *lp;
	uint16_t cmdsize;
	LPFC_NODELIST_t *ndlp;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	cmdsize = (sizeof (uint32_t) + sizeof (FARP));
	if ((ndlp = (LPFC_NODELIST_t *) elx_mem_get(phba, MEM_NLP)) == 0) {
		return (1);
	}
	memset((void *)ndlp, 0, sizeof (LPFC_NODELIST_t));
	ndlp->nlp_DID = Bcast_DID;
	if ((elsiocb = lpfc_prep_els_iocb(phba, TRUE, cmdsize, 0,
					  ndlp, ELS_CMD_RNID)) == 0) {
		elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
		return (1);
	}

	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);
	*((uint32_t *) (pCmd)) = ELS_CMD_FARP;
	pCmd += sizeof (uint32_t);

	/* Provide a timeout value, function, and context.  If the IP node on
	 * far end never responds, this FARP and all IP bufs must be timed out.
	 */
	icmd = &elsiocb->iocb;
	icmd->ulpTimeout = plhba->fc_ipfarp_timeout;
	icmd->ulpContext = (uint16_t) ELS_CMD_FARP;

	/* Fill in FARP payload */

	fp = (FARP *) (pCmd);
	memset((void *)fp, 0, sizeof (FARP));
	lp = (uint32_t *) pCmd;
	*lp++ = SWAP_DATA(plhba->fc_myDID);
	fp->Mflags = FARP_MATCH_PORT;
	fp->Rflags = FARP_REQUEST_PLOGI;
	memcpy((void *)&fp->OportName, (void *)&plhba->fc_portname,
	       sizeof (NAME_TYPE));
	memcpy((void *)&fp->OnodeName, (void *)&plhba->fc_nodename,
	       sizeof (NAME_TYPE));
	switch (argFlag) {
	case LPFC_FARP_BY_IEEE:
		fp->Mflags = FARP_MATCH_PORT;
		fp->RportName.nameType = NAME_IEEE;	/* IEEE name */
		fp->RportName.IEEEextMsn = 0;
		fp->RportName.IEEEextLsb = 0;
		memcpy((void *)fp->RportName.IEEE, arg, 6);
		fp->RnodeName.nameType = NAME_IEEE;	/* IEEE name */
		fp->RnodeName.IEEEextMsn = 0;
		fp->RnodeName.IEEEextLsb = 0;
		memcpy((void *)fp->RnodeName.IEEE, arg, 6);
		break;
	case LPFC_FARP_BY_WWPN:
		fp->Mflags = FARP_MATCH_PORT;
		memcpy((void *)&fp->RportName, arg, sizeof (NAME_TYPE));
		break;
	case LPFC_FARP_BY_WWNN:
		fp->Mflags = FARP_MATCH_NODE;
		memcpy((void *)&fp->RnodeName, arg, sizeof (NAME_TYPE));
		break;
	}

	elx_pci_dma_sync((void *)phba, (void *)(elsiocb->context2),
			 0, ELX_DMA_SYNC_FORDEV);

	plhba->fc_stat.elsXmitFARP++;

	elsiocb->iocb_cmpl = lpfc_cmpl_els_cmd;
	if (elx_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}

	elx_printf_log(phba->brd_no, &elx_msgBlk0610,	/* ptr to msg structure */
		       elx_mes0610,	/* ptr to msg */
		       elx_msgBlk0610.msgPreambleStr,	/* begin varargs */
		       plhba->fc_nodename.IEEE[0], plhba->fc_nodename.IEEE[1], plhba->fc_nodename.IEEE[2], plhba->fc_nodename.IEEE[3], plhba->fc_nodename.IEEE[4], plhba->fc_nodename.IEEE[5]);	/* end varargs */
	return (0);
}

int
lpfc_issue_els_farpr(elxHBA_t * phba, uint32_t nportid, uint8_t retry)
{
	IOCB_t *icmd;
	ELX_IOCBQ_t *elsiocb;
	ELX_SLI_RING_t *pring;
	ELX_SLI_t *psli;
	LPFCHBA_t *plhba;
	FARP *fp;
	uint8_t *pCmd;
	uint32_t *lp;
	uint16_t cmdsize;
	LPFC_NODELIST_t *ondlp;
	LPFC_NODELIST_t *ndlp;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	cmdsize = (sizeof (uint32_t) + sizeof (FARP));
	if ((ndlp = (LPFC_NODELIST_t *) elx_mem_get(phba, MEM_NLP)) == 0) {
		return (1);
	}
	memset((void *)ndlp, 0, sizeof (LPFC_NODELIST_t));
	ndlp->nlp_DID = nportid;

	if ((elsiocb = lpfc_prep_els_iocb(phba, TRUE, cmdsize, retry,
					  ndlp, ELS_CMD_RNID)) == 0) {
		elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
		return (1);
	}

	icmd = &elsiocb->iocb;
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

	*((uint32_t *) (pCmd)) = ELS_CMD_FARPR;
	pCmd += sizeof (uint32_t);

	/* Fill in FARPR payload */
	fp = (FARP *) (pCmd);
	memset((void *)fp, 0, sizeof (FARP));
	lp = (uint32_t *) pCmd;
	*lp++ = SWAP_DATA(nportid);
	*lp++ = SWAP_DATA(plhba->fc_myDID);
	fp->Rflags = 0;
	fp->Mflags = (FARP_MATCH_PORT | FARP_MATCH_NODE);

	memcpy((void *)&fp->RportName, (void *)&plhba->fc_portname,
	       sizeof (NAME_TYPE));
	memcpy((void *)&fp->RnodeName, (void *)&plhba->fc_nodename,
	       sizeof (NAME_TYPE));
	if ((ondlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL, nportid))) {
		memcpy((void *)&fp->OportName, (void *)&ondlp->nlp_portname,
		       sizeof (NAME_TYPE));
		memcpy((void *)&fp->OnodeName, (void *)&ondlp->nlp_nodename,
		       sizeof (NAME_TYPE));
	}

	elx_pci_dma_sync((void *)phba, (void *)(elsiocb->context2),
			 0, ELX_DMA_SYNC_FORDEV);

	plhba->fc_stat.elsXmitFARPR++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_cmd;
	if (elx_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
	return (0);
}

void
lpfc_cmpl_els_cmd(elxHBA_t * phba, ELX_IOCBQ_t * cmdiocb, ELX_IOCBQ_t * rspiocb)
{
	IOCB_t *irsp;

	irsp = &rspiocb->iocb;

	/* ELS cmd tag <ulpIoTag> completes */
	elx_printf_log(phba->brd_no, &elx_msgBlk0106,	/* ptr to msg structure */
		       elx_mes0106,	/* ptr to msg */
		       elx_msgBlk0106.msgPreambleStr,	/* begin varargs */
		       irsp->ulpIoTag, irsp->ulpStatus, irsp->un.ulpWord[4]);	/* end varargs */

	/* Check to see if link went down during discovery */
	lpfc_els_chk_latt(phba, rspiocb);
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

void
lpfc_els_retry_delay(elxHBA_t * phba, void *l1, void *l2)
{
	LPFC_NODELIST_t *ndlp;
	uint32_t cmd;
	uint32_t did;
	uint8_t retry;

	did = (uint32_t) (unsigned long)l1;
	cmd = (uint32_t) (unsigned long)l2;
	if ((ndlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL, did)) == 0) {
		if ((ndlp =
		     (LPFC_NODELIST_t *) elx_mem_get(phba, MEM_NLP)) == 0) {
			return;
		}
		memset((void *)ndlp, 0, sizeof (LPFC_NODELIST_t));
		ndlp->nlp_DID = did;
	}

	ndlp->nlp_flag &= ~(NLP_NODEV_TMO | NLP_DELAY_TMO);
	ndlp->nle.nlp_rflag &= ~NLP_NPR_ACTIVE;
	ndlp->nlp_tmofunc = 0;
	retry = ndlp->nlp_retry;

	switch (cmd) {
	case ELS_CMD_FLOGI:
		lpfc_issue_els_flogi(phba, ndlp, retry);
		return;
	case ELS_CMD_PLOGI:
		ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
		lpfc_nlp_plogi(phba, ndlp);
		lpfc_issue_els_plogi(phba, ndlp, retry);
		return;
	case ELS_CMD_ADISC:
		lpfc_issue_els_adisc(phba, ndlp, retry);
		return;
	case ELS_CMD_PRLI:
		lpfc_issue_els_prli(phba, ndlp, retry);
		return;
	case ELS_CMD_LOGO:
		lpfc_issue_els_logo(phba, ndlp, retry);
		return;
	}
	return;
}

int
lpfc_els_retry(elxHBA_t * phba, ELX_IOCBQ_t * cmdiocb, ELX_IOCBQ_t * rspiocb)
{
	LPFCHBA_t *plhba;
	IOCB_t *irsp;
	DMABUF_t *pCmd;
	LPFC_NODELIST_t *ndlp;
	ELXSCSITARGET_t *targetp;
	uint32_t *elscmd;
	elxCfgParam_t *clp;
	LS_RJT stat;
	int retry, maxretry;
	int delay;
	uint32_t cmd;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	clp = &phba->config[0];
	retry = 0;
	delay = 0;
	maxretry = lpfc_max_els_tries;
	irsp = &rspiocb->iocb;
	ndlp = (LPFC_NODELIST_t *) cmdiocb->context1;

	pCmd = (DMABUF_t *) cmdiocb->context2;
	cmd = 0;
	/* Note: context2 may be 0 for internal driver abort 
	 * of delays ELS command.
	 */

	if (pCmd && pCmd->virt) {
		elscmd = (uint32_t *) (pCmd->virt);
		cmd = *elscmd++;
	}

	switch (irsp->ulpStatus) {
	case IOSTAT_FCP_RSP_ERROR:
	case IOSTAT_REMOTE_STOP:
		break;

	case IOSTAT_LOCAL_REJECT:
		if ((irsp->un.ulpWord[4] & 0xff) == IOERR_LINK_DOWN)
			break;
		if ((irsp->un.ulpWord[4] & 0xff) == IOERR_LOOP_OPEN_FAILURE) {
			if (cmd == ELS_CMD_PLOGI) {
				if (cmdiocb->retry == 0) {
					delay = 1;
				}
			}
			retry = 1;
			break;
		}
		if ((irsp->un.ulpWord[4] & 0xff) == IOERR_SEQUENCE_TIMEOUT) {
			retry = 1;
			if ((cmd == ELS_CMD_FLOGI)
			    && (plhba->fc_topology != TOPOLOGY_LOOP)) {
				delay = 1;
				maxretry = 48;
			}
			break;
		}
		if ((irsp->un.ulpWord[4] & 0xff) == IOERR_NO_RESOURCES) {
			if (cmd == ELS_CMD_PLOGI) {
				delay = 1;
			}
			retry = 1;
			break;
		}
		if ((irsp->un.ulpWord[4] & 0xff) == IOERR_INVALID_RPI) {
			retry = 1;
			break;
		}
		break;

	case IOSTAT_NPORT_RJT:
	case IOSTAT_FABRIC_RJT:
		if (irsp->un.ulpWord[4] & RJT_UNAVAIL_TEMP) {
			retry = 1;
			break;
		}
		break;

	case IOSTAT_NPORT_BSY:
	case IOSTAT_FABRIC_BSY:
		retry = 1;
		break;

	case IOSTAT_LS_RJT:
		stat.un.lsRjtError = SWAP_DATA(irsp->un.ulpWord[4]);
		/* Added for Vendor specifc support
		 * Just keep retrying for these Rsn / Exp codes
		 */
		switch (stat.un.b.lsRjtRsnCode) {
		case LSRJT_UNABLE_TPC:
			if (stat.un.b.lsRjtRsnCodeExp == LSEXP_CMD_IN_PROGRESS) {
				if (cmd == ELS_CMD_PLOGI) {
					delay = 1;
					maxretry = 48;
				}
				retry = 1;
				break;
			}
			if (cmd == ELS_CMD_PLOGI) {
				delay = 1;
				maxretry = lpfc_max_els_tries + 1;
				retry = 1;
				break;
			}
			break;

		case LSRJT_LOGICAL_BSY:
			if (cmd == ELS_CMD_PLOGI) {
				delay = 1;
				maxretry = 48;
			}
			retry = 1;
			break;
		}
		break;

	case IOSTAT_INTERMED_RSP:
	case IOSTAT_BA_RJT:
		break;

	default:
		break;
	}

	if (ndlp->nlp_DID == FDMI_DID) {
		retry = 1;
	}

	if ((++cmdiocb->retry) >= maxretry) {
		plhba->fc_stat.elsRetryExceeded++;
		retry = 0;
	}

	if (retry) {

		/* Retry ELS command <elsCmd> to remote NPORT <did> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0107,	/* ptr to msg structure */
			       elx_mes0107,	/* ptr to msg */
			       elx_msgBlk0107.msgPreambleStr,	/* begin varargs */
			       cmd, ndlp->nlp_DID, cmdiocb->retry, delay);	/* end varargs */

		if ((cmd == ELS_CMD_PLOGI) || (cmd == ELS_CMD_ADISC)) {
			/* If discovery / RSCN timer is running, reset it */
			if ((plhba->fc_disctmo)
			    || (plhba->fc_flag & FC_RSCN_MODE)) {
				lpfc_set_disctmo(phba);
			}
		}

		plhba->fc_stat.elsXmitRetry++;
		if (delay) {
			plhba->fc_stat.elsDelayRetry++;
			ndlp->nlp_retry = cmdiocb->retry;
			if (ndlp->nlp_tmofunc) {
				ndlp->nlp_flag &=
				    ~(NLP_NODEV_TMO | NLP_DELAY_TMO);
				elx_clk_can(phba, ndlp->nlp_tmofunc);
				ndlp->nlp_tmofunc = 0;
			}
			ndlp->nlp_flag |= NLP_DELAY_TMO;
			ndlp->nle.nlp_rflag |= NLP_NPR_ACTIVE;
			ndlp->nlp_tmofunc = elx_clk_set(phba, 0,
							lpfc_els_retry_delay,
							(void *)((unsigned long)
								 ndlp->nlp_DID),
							(void *)((unsigned long)
								 cmd));
			targetp = ndlp->nlp_Target;
			if (targetp) {
				targetp->targetFlags |= FC_NPR_ACTIVE;
				if (targetp->tmofunc) {
					elx_clk_can(phba, targetp->tmofunc);
				}
				targetp->tmofunc =
				    elx_clk_set(phba,
						clp[ELX_CFG_NODEV_TMO].
						a_current, lpfc_npr_timeout,
						(void *)targetp, (void *)0);
			}
			return (1);
		}
		switch (cmd) {
		case ELS_CMD_FLOGI:
			lpfc_issue_els_flogi(phba, ndlp, cmdiocb->retry);
			return (1);
		case ELS_CMD_PLOGI:
			lpfc_issue_els_plogi(phba, ndlp, cmdiocb->retry);
			return (1);
		case ELS_CMD_ADISC:
			lpfc_issue_els_adisc(phba, ndlp, cmdiocb->retry);
			return (1);
		case ELS_CMD_PRLI:
			lpfc_issue_els_prli(phba, ndlp, cmdiocb->retry);
			return (1);
		case ELS_CMD_LOGO:
			lpfc_issue_els_logo(phba, ndlp, cmdiocb->retry);
			return (1);
		}
	}

	/* No retry ELS command <elsCmd> to remote NPORT <did> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0108,	/* ptr to msg structure */
		       elx_mes0108,	/* ptr to msg */
		       elx_msgBlk0108.msgPreambleStr,	/* begin varargs */
		       cmd, ndlp->nlp_DID, cmdiocb->retry, ndlp->nlp_flag);	/* end varargs */

	return (0);
}

ELX_IOCBQ_t *
lpfc_prep_els_iocb(elxHBA_t * phba,
		   uint8_t expectRsp,
		   uint16_t cmdSize,
		   uint8_t retry, LPFC_NODELIST_t * ndlp, uint32_t elscmd)
{
	ELX_SLI_t *psli;
	ELX_SLI_RING_t *pring;
	ELX_IOCBQ_t *elsiocb;
	LPFCHBA_t *plhba;
	DMABUF_t *pCmd, *pRsp, *pBufList;
	ULP_BDE64 *bpl;
	IOCB_t *icmd;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	if (phba->hba_state < ELX_LINK_UP) {
		return (0);
	}

	/* Allocate buffer for  command iocb */
	if ((elsiocb =
	     (ELX_IOCBQ_t *) elx_mem_get(phba, MEM_IOCB | MEM_PRI)) == 0) {
		return (0);
	}
	memset((void *)elsiocb, 0, sizeof (ELX_IOCBQ_t));
	icmd = &elsiocb->iocb;

	/* fill in BDEs for command */
	/* Allocate buffer for command payload */
	if ((pCmd = (DMABUF_t *) elx_mem_get(phba, MEM_BUF | MEM_PRI)) == 0) {
		elx_mem_put(phba, MEM_IOCB, (uint8_t *) elsiocb);
		return (0);
	}

	/* Allocate buffer for response payload */
	if (expectRsp) {
		if ((pRsp =
		     (DMABUF_t *) elx_mem_get(phba, MEM_BUF | MEM_PRI)) == 0) {
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) elsiocb);
			elx_mem_put(phba, MEM_BUF, (uint8_t *) pCmd);
			return (0);
		}
	} else {
		pRsp = 0;
	}

	/* Allocate buffer for Buffer ptr list */
	if ((pBufList = (DMABUF_t *) elx_mem_get(phba, MEM_BPL | MEM_PRI)) == 0) {
		elx_mem_put(phba, MEM_IOCB, (uint8_t *) elsiocb);
		elx_mem_put(phba, MEM_BUF, (uint8_t *) pCmd);
		elx_mem_put(phba, MEM_BUF, (uint8_t *) pRsp);
		return (0);
	}

	icmd->un.elsreq64.bdl.addrHigh = putPaddrHigh(pBufList->phys);
	icmd->un.elsreq64.bdl.addrLow = putPaddrLow(pBufList->phys);
	icmd->un.elsreq64.bdl.bdeFlags = BUFF_TYPE_BDL;
	if (expectRsp) {
		icmd->un.elsreq64.bdl.bdeSize = (2 * sizeof (ULP_BDE64));
		icmd->un.elsreq64.remoteID = ndlp->nlp_DID;	/* DID */
		icmd->ulpCommand = CMD_ELS_REQUEST64_CR;
	} else {
		icmd->un.elsreq64.bdl.bdeSize = sizeof (ULP_BDE64);
		icmd->ulpCommand = CMD_XMIT_ELS_RSP64_CX;
	}

	/* NOTE: we don't use ulpIoTag0 because it is a t2 structure */
	icmd->ulpIoTag = elx_sli_next_iotag(phba, pring);
	icmd->un.elsreq64.bdl.ulpIoTag32 = (uint32_t) icmd->ulpIoTag;
	icmd->ulpBdeCount = 1;
	icmd->ulpLe = 1;
	icmd->ulpClass = CLASS3;
	icmd->ulpOwner = OWN_CHIP;

	bpl = (ULP_BDE64 *) pBufList->virt;
	bpl->addrLow = PCIMEM_LONG(putPaddrLow(pCmd->phys));
	bpl->addrHigh = PCIMEM_LONG(putPaddrHigh(pCmd->phys));
	bpl->tus.f.bdeSize = cmdSize;
	bpl->tus.f.bdeFlags = 0;
	bpl->tus.w = PCIMEM_LONG(bpl->tus.w);

	if (expectRsp) {
		bpl++;
		bpl->addrLow = PCIMEM_LONG(putPaddrLow(pRsp->phys));
		bpl->addrHigh = PCIMEM_LONG(putPaddrHigh(pRsp->phys));
		bpl->tus.f.bdeSize = FCELSSIZE;
		bpl->tus.f.bdeFlags = BUFF_USE_RCV;
		bpl->tus.w = PCIMEM_LONG(bpl->tus.w);
	}

	/* Save for completion so we can release these resources */
	elsiocb->context1 = (uint8_t *) ndlp;
	elsiocb->context2 = (uint8_t *) pCmd;
	elsiocb->context3 = (uint8_t *) pBufList;
	elsiocb->retry = retry;
	elsiocb->drvrTimeout = (plhba->fc_ratov << 1) + ELX_DRVR_TIMEOUT;
	pCmd->next = pRsp;

	elx_pci_dma_sync((void *)phba, (void *)pBufList,
			 0, ELX_DMA_SYNC_FORDEV);

	if (expectRsp) {
		/* Xmit ELS command <elsCmd> to remote NPORT <did> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0116,	/* ptr to msg structure */
			       elx_mes0116,	/* ptr to msg */
			       elx_msgBlk0116.msgPreambleStr,	/* begin varargs */
			       elscmd, ndlp->nlp_DID,	/* did */
			       icmd->ulpIoTag, phba->hba_state);	/* end varargs */
	} else {
		/* Xmit ELS response <elsCmd> to remote NPORT <did> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0117,	/* ptr to msg structure */
			       elx_mes0117,	/* ptr to msg */
			       elx_msgBlk0117.msgPreambleStr,	/* begin varargs */
			       elscmd, ndlp->nlp_DID,	/* did */
			       icmd->ulpIoTag, cmdSize);	/* end varargs */
	}

	return (elsiocb);
}

int
lpfc_els_free_iocb(elxHBA_t * phba, ELX_IOCBQ_t * elsiocb)
{
	/* context2  = cmd,  context2->next = rsp, context3 = bpl */
	if (elsiocb->context2) {
		/* Free the response before processing the command.  */
		if (((DMABUF_t *) (elsiocb->context2))->next) {
			elx_mem_put(phba, MEM_BUF,
				    (uint8_t
				     *) (((DMABUF_t *) (elsiocb->context2))->
					 next));
		}
		elx_mem_put(phba, MEM_BUF, (uint8_t *) (elsiocb->context2));
	}

	if (elsiocb->context3) {
		elx_mem_put(phba, MEM_BPL, (uint8_t *) (elsiocb->context3));
	}

	elx_mem_put(phba, MEM_IOCB, (uint8_t *) elsiocb);
	return 0;
}

void
lpfc_cmpl_els_logo_acc(elxHBA_t * phba,
		       ELX_IOCBQ_t * cmdiocb, ELX_IOCBQ_t * rspiocb)
{
	LPFC_NODELIST_t *ndlp;
	LPFCHBA_t *plhba;
	ELXSCSITARGET_t *targetp;
	elxCfgParam_t *clp;
	int delay;

	clp = &phba->config[0];
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	ndlp = (LPFC_NODELIST_t *) cmdiocb->context1;

	/* ACC to LOGO completes to NPort <nlp_DID> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0109,	/* ptr to msg structure */
		       elx_mes0109,	/* ptr to msg */
		       elx_msgBlk0109.msgPreambleStr,	/* begin varargs */
		       ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state, ndlp->nle.nlp_rpi);	/* end varargs */

	delay = 1;
	switch (ndlp->nlp_state) {
	case NLP_STE_UNUSED_NODE:	/* node is just allocated */
	case NLP_STE_PLOGI_ISSUE:	/* PLOGI was sent to NL_PORT */
	case NLP_STE_REG_LOGIN_ISSUE:	/* REG_LOGIN was issued for NL_PORT */
		break;
	case NLP_STE_PRLI_ISSUE:	/* PRLI was sent to NL_PORT */
		/* dequeue, cancel timeout, unreg login */
		lpfc_freenode(phba, ndlp);

		/* put back on plogi list and send a new plogi */
		lpfc_nlp_plogi(phba, ndlp);
		ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
		lpfc_issue_els_plogi(phba, ndlp, 0);
		break;

	case NLP_STE_PRLI_COMPL:	/* PRLI completed from NL_PORT */
		delay = 0;

	case NLP_STE_MAPPED_NODE:	/* Identified as a FCP Target */

		lpfc_set_failmask(phba, ndlp, ELX_DEV_DISCONNECTED,
				  ELX_SET_BITMASK);
		if (ndlp->nlp_flag & NLP_ADISC_SND) {
			/* dequeue, cancel timeout, unreg login */
			lpfc_freenode(phba, ndlp);

			/* put back on plogi list and send a new plogi */
			lpfc_nlp_plogi(phba, ndlp);
			ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
			lpfc_issue_els_plogi(phba, ndlp, 0);
		} else {
			targetp = ndlp->nlp_Target;

			/* dequeue, cancel timeout, unreg login */
			lpfc_freenode(phba, ndlp);

			if (targetp) {
				targetp->targetFlags |= FC_NPR_ACTIVE;
				if (targetp->rptlunfunc) {
					elx_clk_can(phba, targetp->rptlunfunc);
					targetp->targetFlags &=
					    ~FC_RETRY_RPTLUN;
				}
				targetp->rptlunfunc = 0;
				if (targetp->tmofunc) {
					elx_clk_can(phba, targetp->tmofunc);
				}
				targetp->tmofunc =
				    elx_clk_set(phba,
						clp[ELX_CFG_NODEV_TMO].
						a_current, lpfc_npr_timeout,
						(void *)targetp, (void *)0);
			}

			if (ndlp->nlp_tmofunc) {
				ndlp->nlp_flag &=
				    ~(NLP_NODEV_TMO | NLP_DELAY_TMO);
				elx_clk_can(phba, ndlp->nlp_tmofunc);
				ndlp->nlp_tmofunc = 0;
			}
			lpfc_nlp_plogi(phba, ndlp);
			ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
			ndlp->nlp_flag |= NLP_DELAY_TMO;
			ndlp->nle.nlp_rflag |= NLP_NPR_ACTIVE;
			ndlp->nlp_retry = 0;
			ndlp->nlp_tmofunc = elx_clk_set(phba, delay,
							lpfc_els_retry_delay,
							(void *)((unsigned long)
								 ndlp->nlp_DID),
							(void *)((unsigned long)
								 ELS_CMD_PLOGI));
			targetp = ndlp->nlp_Target;
			if (targetp) {
				targetp->targetFlags |= FC_NPR_ACTIVE;
				if (targetp->tmofunc) {
					elx_clk_can(phba, targetp->tmofunc);
				}
				targetp->tmofunc =
				    elx_clk_set(phba,
						clp[ELX_CFG_NODEV_TMO].
						a_current, lpfc_npr_timeout,
						(void *)targetp, (void *)0);
			}
		}
		break;
	}
	ndlp->nlp_flag &= ~NLP_LOGO_ACC;
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

void
lpfc_cmpl_els_acc(elxHBA_t * phba, ELX_IOCBQ_t * cmdiocb, ELX_IOCBQ_t * rspiocb)
{
	LPFC_NODELIST_t *ndlp;
	ELX_MBOXQ_t *mbox;
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	ndlp = (LPFC_NODELIST_t *) cmdiocb->context1;

	/* ELS response tag <ulpIoTag> completes */
	elx_printf_log(phba->brd_no, &elx_msgBlk0110,	/* ptr to msg structure */
		       elx_mes0110,	/* ptr to msg */
		       elx_msgBlk0110.msgPreambleStr,	/* begin varargs */
		       cmdiocb->iocb.ulpIoTag, rspiocb->iocb.ulpStatus, rspiocb->iocb.un.ulpWord[4], ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state, ndlp->nle.nlp_rpi);	/* end varargs */

	mbox = (ELX_MBOXQ_t *) ((DMABUF_t *) cmdiocb->context3)->next;
	if ((rspiocb->iocb.ulpStatus == 0)
	    && (ndlp->nlp_flag & NLP_ACC_REGLOGIN)) {
		/* set_slim mailbox command needs to execute first,
		 * queue this command to be processed later.
		 */
		mbox->mbox_cmpl = lpfc_mbx_cmpl_reg_login;
		mbox->context2 = (void *)ndlp;
		ndlp->nlp_state = NLP_STE_REG_LOGIN_ISSUE;
		if (elx_sli_issue_mbox(phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
		    != MBX_NOT_FINISHED) {
			goto out;
		}
		/* NOTE: we should have messages for unsuccessful reglogin */
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) mbox);
	} else {
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) mbox);
	}
      out:
	ndlp->nlp_flag &= ~NLP_ACC_REGLOGIN;

	/* Check to see if link went down during discovery */
	lpfc_els_chk_latt(phba, rspiocb);

	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_els_rsp_acc(elxHBA_t * phba,
		 uint32_t flag,
		 ELX_IOCBQ_t * oldiocb,
		 LPFC_NODELIST_t * ndlp, ELX_MBOXQ_t * mbox)
{
	IOCB_t *icmd;
	IOCB_t *oldcmd;
	ELX_IOCBQ_t *elsiocb;
	ELX_SLI_RING_t *pring;
	ELX_SLI_t *psli;
	LPFCHBA_t *plhba;
	uint8_t *pCmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	oldcmd = &oldiocb->iocb;

	switch (flag) {
	case ELS_CMD_ACC:
		cmdsize = sizeof (uint32_t);
		if ((elsiocb =
		     lpfc_prep_els_iocb(phba, FALSE, cmdsize, oldiocb->retry,
					ndlp, ELS_CMD_ACC)) == 0) {
			return (1);
		}
		icmd = &elsiocb->iocb;
		icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
		pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);
		*((uint32_t *) (pCmd)) = ELS_CMD_ACC;
		pCmd += sizeof (uint32_t);
		break;
	case ELS_CMD_PLOGI:
		cmdsize = (sizeof (SERV_PARM) + sizeof (uint32_t));
		if ((elsiocb =
		     lpfc_prep_els_iocb(phba, FALSE, cmdsize, oldiocb->retry,
					ndlp, ELS_CMD_ACC)) == 0) {
			return (1);
		}
		icmd = &elsiocb->iocb;
		icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
		pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

		if (elsiocb->context3 && mbox) {
			((DMABUF_t *) elsiocb->context3)->next = (void *)mbox;
		}
		*((uint32_t *) (pCmd)) = ELS_CMD_ACC;
		pCmd += sizeof (uint32_t);
		memcpy((void *)pCmd, (void *)&plhba->fc_sparam,
		       sizeof (SERV_PARM));
		break;
	default:
		return (1);
	}

	elx_pci_dma_sync((void *)phba, (void *)(elsiocb->context2),
			 0, ELX_DMA_SYNC_FORDEV);

	if (ndlp->nlp_flag & NLP_LOGO_ACC) {
		elsiocb->iocb_cmpl = lpfc_cmpl_els_logo_acc;
	} else {
		elsiocb->iocb_cmpl = lpfc_cmpl_els_acc;
	}

	plhba->fc_stat.elsXmitACC++;
	if (elx_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	return (0);
}

int
lpfc_els_rsp_reject(elxHBA_t * phba, uint32_t rejectError, ELX_IOCBQ_t * oldiocb,	/* requires */
		    LPFC_NODELIST_t * ndlp)
{
	IOCB_t *icmd;
	IOCB_t *oldcmd;
	ELX_IOCBQ_t *elsiocb;
	ELX_SLI_RING_t *pring;
	ELX_SLI_t *psli;
	LPFCHBA_t *plhba;
	uint8_t *pCmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	cmdsize = 2 * sizeof (uint32_t);
	if ((elsiocb = lpfc_prep_els_iocb(phba, FALSE, cmdsize, oldiocb->retry,
					  ndlp, ELS_CMD_LS_RJT)) == 0) {
		return (1);
	}

	icmd = &elsiocb->iocb;
	oldcmd = &oldiocb->iocb;
	icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

	*((uint32_t *) (pCmd)) = ELS_CMD_LS_RJT;
	pCmd += sizeof (uint32_t);
	*((uint32_t *) (pCmd)) = rejectError;

	elx_pci_dma_sync((void *)phba, (void *)(elsiocb->context2),
			 0, ELX_DMA_SYNC_FORDEV);

	plhba->fc_stat.elsXmitLSRJT++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_acc;

	if (elx_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	return (0);
}

int
lpfc_els_rsp_adisc_acc(elxHBA_t * phba,
		       ELX_IOCBQ_t * oldiocb, LPFC_NODELIST_t * ndlp)
{
	ADISC *ap;
	IOCB_t *icmd;
	IOCB_t *oldcmd;
	ELX_IOCBQ_t *elsiocb;
	ELX_SLI_RING_t *pring;
	ELX_SLI_t *psli;
	LPFCHBA_t *plhba;
	uint8_t *pCmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	cmdsize = sizeof (uint32_t) + sizeof (ADISC);
	if ((elsiocb = lpfc_prep_els_iocb(phba, FALSE, cmdsize, oldiocb->retry,
					  ndlp, ELS_CMD_ACC)) == 0) {
		return (1);
	}

	icmd = &elsiocb->iocb;
	oldcmd = &oldiocb->iocb;
	icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

	*((uint32_t *) (pCmd)) = ELS_CMD_ACC;
	pCmd += sizeof (uint32_t);

	ap = (ADISC *) (pCmd);
	ap->hardAL_PA = plhba->fc_pref_ALPA;
	memcpy((void *)&ap->portName, (void *)&plhba->fc_portname,
	       sizeof (NAME_TYPE));
	memcpy((void *)&ap->nodeName, (void *)&plhba->fc_nodename,
	       sizeof (NAME_TYPE));
	ap->DID = SWAP_DATA(plhba->fc_myDID);

	elx_pci_dma_sync((void *)phba, (void *)(elsiocb->context2),
			 0, ELX_DMA_SYNC_FORDEV);

	plhba->fc_stat.elsXmitACC++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_acc;

	if (elx_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	return (0);
}

int
lpfc_els_rsp_prli_acc(elxHBA_t * phba,
		      ELX_IOCBQ_t * oldiocb, LPFC_NODELIST_t * ndlp)
{
	PRLI *npr;
	elx_vpd_t *vpd;
	IOCB_t *icmd;
	IOCB_t *oldcmd;
	ELX_IOCBQ_t *elsiocb;
	ELX_SLI_RING_t *pring;
	ELX_SLI_t *psli;
	LPFCHBA_t *plhba;
	uint8_t *pCmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	cmdsize = sizeof (uint32_t) + sizeof (PRLI);
	if ((elsiocb = lpfc_prep_els_iocb(phba, FALSE, cmdsize, oldiocb->retry,
					  ndlp,
					  (ELS_CMD_ACC |
					   (ELS_CMD_PRLI & ~ELS_RSP_MASK)))) ==
	    0) {
		return (1);
	}

	icmd = &elsiocb->iocb;
	oldcmd = &oldiocb->iocb;
	icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

	*((uint32_t *) (pCmd)) = (ELS_CMD_ACC | (ELS_CMD_PRLI & ~ELS_RSP_MASK));
	pCmd += sizeof (uint32_t);

	/* For PRLI, remainder of payload is PRLI parameter page */
	memset((void *)pCmd, 0, sizeof (PRLI));

	npr = (PRLI *) pCmd;
	vpd = &phba->vpd;
	/*
	 * If our firmware version is 3.20 or later, 
	 * set the following bits for FC-TAPE support.
	 */
	if (vpd->rev.feaLevelHigh >= 0x02) {
		npr->ConfmComplAllowed = 1;
		npr->Retry = 1;
		npr->TaskRetryIdReq = 1;
	}

	npr->acceptRspCode = PRLI_REQ_EXECUTED;
	npr->estabImagePair = 1;
	npr->readXferRdyDis = 1;
	npr->ConfmComplAllowed = 1;

	npr->prliType = PRLI_FCP_TYPE;
	npr->initiatorFunc = 1;

	elx_pci_dma_sync((void *)phba, (void *)(elsiocb->context2),
			 0, ELX_DMA_SYNC_FORDEV);

	plhba->fc_stat.elsXmitACC++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_acc;

	if (elx_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	return (0);
}

int
lpfc_els_rsp_rnid_acc(elxHBA_t * phba,
		      uint8_t format,
		      ELX_IOCBQ_t * oldiocb, LPFC_NODELIST_t * ndlp)
{
	RNID *rn;
	IOCB_t *icmd;
	IOCB_t *oldcmd;
	ELX_IOCBQ_t *elsiocb;
	ELX_SLI_RING_t *pring;
	ELX_SLI_t *psli;
	LPFCHBA_t *plhba;
	uint8_t *pCmd;
	uint16_t cmdsize;
	elxCfgParam_t *clp;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	clp = &phba->config[0];

	cmdsize =
	    sizeof (uint32_t) + sizeof (uint32_t) + (2 * sizeof (NAME_TYPE));
	if (format)
		cmdsize += sizeof (RNID_TOP_DISC);

	if ((elsiocb = lpfc_prep_els_iocb(phba, FALSE, cmdsize, oldiocb->retry,
					  ndlp, ELS_CMD_ACC)) == 0) {
		return (1);
	}

	icmd = &elsiocb->iocb;
	oldcmd = &oldiocb->iocb;
	icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

	*((uint32_t *) (pCmd)) = ELS_CMD_ACC;
	pCmd += sizeof (uint32_t);

	memset((void *)pCmd, 0, sizeof (RNID));
	rn = (RNID *) (pCmd);
	rn->Format = format;
	rn->CommonLen = (2 * sizeof (NAME_TYPE));
	memcpy((void *)&rn->portName, (void *)&plhba->fc_portname,
	       sizeof (NAME_TYPE));
	memcpy((void *)&rn->nodeName, (void *)&plhba->fc_nodename,
	       sizeof (NAME_TYPE));
	switch (format) {
	case 0:
		rn->SpecificLen = 0;
		break;
	case RNID_TOPOLOGY_DISC:
		rn->SpecificLen = sizeof (RNID_TOP_DISC);
		memcpy((void *)&rn->un.topologyDisc.portName,
		       (void *)&plhba->fc_portname, sizeof (NAME_TYPE));
		rn->un.topologyDisc.unitType = RNID_HBA;
		rn->un.topologyDisc.physPort = 0;
		rn->un.topologyDisc.attachedNodes = 0;

		if (clp[LPFC_CFG_NETWORK_ON].a_current) {
			rn->un.topologyDisc.ipVersion = plhba->ipVersion;
			rn->un.topologyDisc.UDPport = plhba->UDPport;
			memcpy((void *)&rn->un.topologyDisc.ipAddr[0],
			       (void *)&plhba->ipAddr[0], 16);
		}
		break;
	default:
		rn->CommonLen = 0;
		rn->SpecificLen = 0;
		break;
	}

	elx_pci_dma_sync((void *)phba, (void *)(elsiocb->context2),
			 0, ELX_DMA_SYNC_FORDEV);

	plhba->fc_stat.elsXmitACC++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_acc;

	if (elx_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	return (0);
}

void
lpfc_els_unsol_event(elxHBA_t * phba,
		     ELX_SLI_RING_t * pring, ELX_IOCBQ_t * elsiocb)
{
	LPFCHBA_t *plhba;
	ELX_SLI_t *psli;
	LPFC_NODELIST_t *ndlp;
	DMABUF_t *mp;
	uint32_t *lp;
	IOCB_t *icmd;
	LS_RJT stat;
	uint32_t cmd;
	uint32_t did;
	uint32_t newnode;

	psli = &phba->sli;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	icmd = &elsiocb->iocb;
	if (icmd->ulpStatus) {
		goto dropit;
	}
	/* type of ELS cmd is first 32bit word in packet */
	mp = elx_sli_ringpostbuf_get(phba, pring,
				     (elx_dma_addr_t) getPaddr(icmd->un.
							       cont64[0].
							       addrHigh,
							       icmd->un.
							       cont64[0].
							       addrLow));
	if (mp == 0) {
	      dropit:
		/* Dropping received ELS cmd */
		elx_printf_log(phba->brd_no, &elx_msgBlk0111,	/* ptr to msg structure */
			       elx_mes0111,	/* ptr to msg */
			       elx_msgBlk0111.msgPreambleStr,	/* begin varargs */
			       icmd->ulpStatus, icmd->un.ulpWord[4]);	/* end varargs */
		plhba->fc_stat.elsRcvDrop++;
		return;
	}

	newnode = 0;
	lp = (uint32_t *) mp->virt;
	cmd = *lp++;
	lpfc_post_buffer(phba, &psli->ring[LPFC_ELS_RING], 1, 1);

	/* Check to see if link went down during discovery */
	if (lpfc_els_chk_latt(phba, elsiocb)) {
		elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
		goto dropit;
	}

	did = icmd->un.rcvels.remoteID;
	if ((ndlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL, did)) == 0) {
		/* Cannot find existing Fabric ndlp, so allocate a new one */
		if ((ndlp =
		     (LPFC_NODELIST_t *) elx_mem_get(phba, MEM_NLP)) == 0) {
			elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
			goto dropit;
		}
		newnode = 1;
		memset((void *)ndlp, 0, sizeof (LPFC_NODELIST_t));
		ndlp->nlp_DID = did;
		if ((did & Fabric_DID_MASK) == Fabric_DID_MASK) {
			ndlp->nle.nlp_type |= NLP_FABRIC;
		}
	}

	plhba->fc_stat.elsRcvFrame++;
	elsiocb->context1 = ndlp;
	elsiocb->context2 = mp;

	if ((cmd & ELS_CMD_MASK) == ELS_CMD_RSCN) {
		cmd &= ELS_CMD_MASK;
	}
	/* ELS command <elsCmd> received from NPORT <did> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0112,	/* ptr to msg structure */
		       elx_mes0112,	/* ptr to msg */
		       elx_msgBlk0112.msgPreambleStr,	/* begin varargs */
		       cmd, did, phba->hba_state);	/* end varargs */

	switch (cmd) {
	case ELS_CMD_PLOGI:
		plhba->fc_stat.elsRcvPLOGI++;
		lpfc_disc_state_machine(phba, ndlp, (void *)elsiocb,
					NLP_EVT_RCV_PLOGI);
		break;
	case ELS_CMD_FLOGI:
		plhba->fc_stat.elsRcvFLOGI++;
		lpfc_els_rcv_flogi(phba, elsiocb, ndlp);
		if (newnode) {
			elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
		}
		break;
	case ELS_CMD_LOGO:
		plhba->fc_stat.elsRcvLOGO++;
		lpfc_disc_state_machine(phba, ndlp, (void *)elsiocb,
					NLP_EVT_RCV_LOGO);
		break;
	case ELS_CMD_PRLO:
		plhba->fc_stat.elsRcvPRLO++;
		lpfc_disc_state_machine(phba, ndlp, (void *)elsiocb,
					NLP_EVT_RCV_PRLO);
		break;
	case ELS_CMD_RSCN:
		plhba->fc_stat.elsRcvRSCN++;
		lpfc_els_rcv_rscn(phba, elsiocb, ndlp);
		if (newnode) {
			elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
		}
		break;
	case ELS_CMD_ADISC:
		plhba->fc_stat.elsRcvADISC++;
		lpfc_disc_state_machine(phba, ndlp, (void *)elsiocb,
					NLP_EVT_RCV_ADISC);
		break;
	case ELS_CMD_PDISC:
		plhba->fc_stat.elsRcvPDISC++;
		lpfc_disc_state_machine(phba, ndlp, (void *)elsiocb,
					NLP_EVT_RCV_PDISC);
		break;
	case ELS_CMD_FARPR:
		plhba->fc_stat.elsRcvFARPR++;
		lpfc_els_rcv_farpr(phba, elsiocb, ndlp);
		break;
	case ELS_CMD_FARP:
		plhba->fc_stat.elsRcvFARP++;
		lpfc_els_rcv_farp(phba, elsiocb, ndlp);
		break;
	case ELS_CMD_FAN:
		plhba->fc_stat.elsRcvFAN++;
		lpfc_els_rcv_fan(phba, elsiocb, ndlp);
		break;
	case ELS_CMD_RRQ:
		plhba->fc_stat.elsRcvRRQ++;
		lpfc_els_rcv_rrq(phba, elsiocb, ndlp);
		break;
	case ELS_CMD_PRLI:
		plhba->fc_stat.elsRcvPRLI++;
		lpfc_disc_state_machine(phba, ndlp, (void *)elsiocb,
					NLP_EVT_RCV_PRLI);
		break;
	case ELS_CMD_RNID:
		plhba->fc_stat.elsRcvRNID++;
		lpfc_els_rcv_rnid(phba, elsiocb, ndlp);
		break;
	default:
		/* Unsupported ELS command, reject */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_CMD_UNSUPPORTED;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_NOTHING_MORE;
		stat.un.b.vendorUnique = 0;

		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, elsiocb, ndlp);

		/* Unknown ELS command <elsCmd> received from NPORT <did> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0115,	/* ptr to msg structure */
			       elx_mes0115,	/* ptr to msg */
			       elx_msgBlk0115.msgPreambleStr,	/* begin varargs */
			       cmd, did);	/* end varargs */
		if (newnode) {
			elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
		}
		break;
	}
	if (elsiocb->context2) {
		elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
	}
	return;
}

void
lpfc_more_adisc(elxHBA_t * phba)
{
	int sentadisc;
	LPFCHBA_t *plhba;
	LPFC_NODELIST_t *ndlp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;

	if (plhba->num_disc_nodes)
		plhba->num_disc_nodes--;

	/* Continue discovery with <num_disc_nodes> ADISCs to go */
	elx_printf_log(phba->brd_no, &elx_msgBlk0210,	/* ptr to msg structure */
		       elx_mes0210,	/* ptr to msg */
		       elx_msgBlk0210.msgPreambleStr,	/* begin varargs */
		       plhba->num_disc_nodes, plhba->fc_adisc_cnt, plhba->fc_flag, phba->hba_state);	/* end varargs */

	/* Check to see if there are more ADISCs to be sent */
	if (plhba->fc_flag & FC_NLP_MORE) {
		sentadisc = 0;
		lpfc_set_disctmo(phba);

		/* go thru ADISC list and issue any remaining ELS ADISCs */
		ndlp = plhba->fc_adisc_start;
		while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_adisc_start) {
			if (!(ndlp->nlp_flag & NLP_ADISC_SND)) {
				/* If we haven't already sent an ADISC for this node,
				 * send it.
				 */
				lpfc_issue_els_adisc(phba, ndlp, 0);
				plhba->num_disc_nodes++;
				ndlp->nlp_flag |= NLP_DISC_NODE;
				sentadisc = 1;
				break;
			}
			ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
		}
		if (sentadisc == 0) {
			plhba->fc_flag &= ~FC_NLP_MORE;
		}
	}
	return;
}

void
lpfc_more_plogi(elxHBA_t * phba)
{
	int sentplogi;
	LPFC_NODELIST_t *ndlp;
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;

	if (plhba->num_disc_nodes)
		plhba->num_disc_nodes--;

	/* Continue discovery with <num_disc_nodes> PLOGIs to go */
	elx_printf_log(phba->brd_no, &elx_msgBlk0232,	/* ptr to msg structure */
		       elx_mes0232,	/* ptr to msg */
		       elx_msgBlk0232.msgPreambleStr,	/* begin varargs */
		       plhba->num_disc_nodes, plhba->fc_plogi_cnt, plhba->fc_flag, phba->hba_state);	/* end varargs */

	/* Check to see if there are more PLOGIs to be sent */
	if (plhba->fc_flag & FC_NLP_MORE) {
		sentplogi = 0;
		/* go thru PLOGI list and issue any remaining ELS PLOGIs */
		ndlp = plhba->fc_plogi_start;
		while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_plogi_start) {
			if ((!(ndlp->nlp_flag & NLP_PLOGI_SND)) &&
			    (ndlp->nlp_state == NLP_STE_UNUSED_NODE)) {
				/* If we haven't already sent an PLOGI for this node,
				 * send it.
				 */
				ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
				lpfc_issue_els_plogi(phba, ndlp, 0);
				plhba->num_disc_nodes++;
				ndlp->nlp_flag |= NLP_DISC_NODE;
				sentplogi = 1;
				break;
			}
			ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
		}
		if (sentplogi == 0) {
			plhba->fc_flag &= ~FC_NLP_MORE;
		}
	}
	return;
}

int
lpfc_els_flush_rscn(elxHBA_t * phba)
{
	LPFCHBA_t *plhba;
	DMABUF_t *mp;
	int i;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	for (i = 0; i < plhba->fc_rscn_id_cnt; i++) {
		mp = plhba->fc_rscn_id_list[i];
		elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
		plhba->fc_rscn_id_list[i] = 0;
	}
	plhba->fc_rscn_id_cnt = 0;
	plhba->fc_flag &= ~(FC_RSCN_MODE | FC_RSCN_DISCOVERY);
	lpfc_can_disctmo(phba);
	return (0);
}

int
lpfc_rscn_payload_check(elxHBA_t * phba, uint32_t did)
{
	D_ID ns_did;
	D_ID rscn_did;
	LPFCHBA_t *plhba;
	DMABUF_t *mp;
	uint32_t *lp;
	uint32_t payload_len, cmd, i, match;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	ns_did.un.word = did;
	match = 0;

	/* If we are doing a FULL RSCN rediscovery, match everything */
	if (plhba->fc_flag & FC_RSCN_DISCOVERY) {
		return (did);
	}

	for (i = 0; i < plhba->fc_rscn_id_cnt; i++) {
		mp = plhba->fc_rscn_id_list[i];
		lp = (uint32_t *) mp->virt;
		cmd = *lp++;
		payload_len = SWAP_DATA(cmd) & 0xffff;	/* payload length */
		payload_len -= sizeof (uint32_t);	/* take off word 0 */
		while (payload_len) {
			rscn_did.un.word = *lp++;
			rscn_did.un.word = SWAP_DATA(rscn_did.un.word);
			payload_len -= sizeof (uint32_t);
			switch (rscn_did.un.b.resv) {
			case 0:	/* Single N_Port ID effected */
				if (ns_did.un.word == rscn_did.un.word) {
					match = did;
				}
				break;
			case 1:	/* Whole N_Port Area effected */
				if ((ns_did.un.b.domain == rscn_did.un.b.domain)
				    && (ns_did.un.b.area ==
					rscn_did.un.b.area)) {
					match = did;
				}
				break;
			case 2:	/* Whole N_Port Domain effected */
				if (ns_did.un.b.domain == rscn_did.un.b.domain) {
					match = did;
				}
				break;
			case 3:	/* Whole Fabric effected */
				match = did;
				break;
			default:
				/* Unknown Identifier in RSCN list */
				elx_printf_log(phba->brd_no, &elx_msgBlk0217,	/* ptr to msg structure */
					       elx_mes0217,	/* ptr to msg */
					       elx_msgBlk0217.msgPreambleStr,	/* begin varargs */
					       rscn_did.un.word);	/* end varargs */
				break;
			}
			if (match) {
				break;
			}
		}
	}
	return (match);
}

int
lpfc_els_rcv_rscn(elxHBA_t * phba,
		  ELX_IOCBQ_t * cmdiocb, LPFC_NODELIST_t * ndlp)
{
	DMABUF_t *pCmd;
	uint32_t *lp;
	IOCB_t *icmd;
	LPFCHBA_t *plhba;
	uint32_t payload_len, cmd;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	icmd = &cmdiocb->iocb;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	cmd = *lp++;
	payload_len = SWAP_DATA(cmd) & 0xffff;	/* payload length */
	payload_len -= sizeof (uint32_t);	/* take off word 0 */
	cmd &= ELS_CMD_MASK;

	/* RSCN received */
	elx_printf_log(phba->brd_no, &elx_msgBlk0214,	/* ptr to msg structure */
		       elx_mes0214,	/* ptr to msg */
		       elx_msgBlk0214.msgPreambleStr,	/* begin varargs */
		       plhba->fc_flag, payload_len, *lp, plhba->fc_rscn_id_cnt);	/* end varargs */

	/* if we are already processing an RSCN, save the received
	 * RSCN payload buffer, cmdiocb->context2 to process later.
	 * If we zero, cmdiocb->context2, the calling routine will
	 * not try to free it.
	 */
	if (plhba->fc_flag & FC_RSCN_MODE) {
		if ((plhba->fc_rscn_id_cnt < FC_MAX_HOLD_RSCN) &&
		    !(plhba->fc_flag & FC_RSCN_DISCOVERY)) {
			plhba->fc_rscn_id_list[plhba->fc_rscn_id_cnt++] = pCmd;
			cmdiocb->context2 = 0;
			/* Deferred RSCN */
			elx_printf_log(phba->brd_no, &elx_msgBlk0235,	/* ptr to msg structure */
				       elx_mes0235,	/* ptr to msg */
				       elx_msgBlk0235.msgPreambleStr,	/* begin varargs */
				       plhba->fc_rscn_id_cnt, plhba->fc_flag, phba->hba_state);	/* end varargs */
		} else {
			plhba->fc_flag |= FC_RSCN_DISCOVERY;
			/* ReDiscovery RSCN */
			elx_printf_log(phba->brd_no, &elx_msgBlk0234,	/* ptr to msg structure */
				       elx_mes0234,	/* ptr to msg */
				       elx_msgBlk0234.msgPreambleStr,	/* begin varargs */
				       plhba->fc_rscn_id_cnt, plhba->fc_flag, phba->hba_state);	/* end varargs */
		}
		/* Send back ACC */
		lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0);
		return (0);
	}

	plhba->fc_flag |= FC_RSCN_MODE;
	plhba->fc_rscn_id_list[plhba->fc_rscn_id_cnt++] = pCmd;
	/*
	 * If we zero, cmdiocb->context2, the calling routine will
	 * not try to free it.
	 */
	cmdiocb->context2 = 0;

	lpfc_set_disctmo(phba);

	/* Send back ACC */
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0);

	return (lpfc_els_handle_rscn(phba));
}

int
lpfc_els_handle_rscn(elxHBA_t * phba)
{
	LPFCHBA_t *plhba;
	LPFC_NODELIST_t *ndlp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	dfc_hba_put_event(phba, HBA_EVENT_RSCN, plhba->fc_myDID,
			  plhba->fc_myDID, 0, 0);
	dfc_put_event(phba, FC_REG_RSCN_EVENT, plhba->fc_myDID, 0, 0);

	/* RSCN processed */
	elx_printf_log(phba->brd_no, &elx_msgBlk0215,	/* ptr to msg structure */
		       elx_mes0215,	/* ptr to msg */
		       elx_msgBlk0215.msgPreambleStr,	/* begin varargs */
		       plhba->fc_flag, 0, plhba->fc_rscn_id_cnt, phba->hba_state);	/* end varargs */

	/* To process RSCN, first compare RSCN data with NameServer */
	plhba->fc_ns_retry = 0;
	if ((ndlp = lpfc_findnode_did(phba, NLP_SEARCH_UNMAPPED,
				      NameServer_DID))) {
		/* Good ndlp, issue CT Request to NameServer */
		if (lpfc_ns_cmd(phba, ndlp, SLI_CTNS_GID_FT) == 0) {
			/* Wait for NameServer query cmpl before we can continue */
			return (1);
		}
	} else {
		/* If login to NameServer does not exist, issue one */
		/* Good status, issue PLOGI to NameServer */
		if ((ndlp =
		     lpfc_findnode_did(phba, NLP_SEARCH_ALL, NameServer_DID))) {
			/* Wait for NameServer login cmpl before we can continue */
			return (1);
		}
		if ((ndlp =
		     (LPFC_NODELIST_t *) elx_mem_get(phba, MEM_NLP)) == 0) {
			lpfc_els_flush_rscn(phba);
			return (0);
		} else {
			memset((void *)ndlp, 0, sizeof (LPFC_NODELIST_t));
			ndlp->nle.nlp_type |= NLP_FABRIC;
			ndlp->nlp_DID = NameServer_DID;
			ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
			lpfc_issue_els_plogi(phba, ndlp, 0);
			/* Wait for NameServer login cmpl before we can continue */
			return (1);
		}
	}

	lpfc_els_flush_rscn(phba);
	return (0);
}

int
lpfc_els_rcv_flogi(elxHBA_t * phba,
		   ELX_IOCBQ_t * cmdiocb, LPFC_NODELIST_t * ndlp)
{
	DMABUF_t *pCmd;
	uint32_t *lp;
	IOCB_t *icmd;
	LPFCHBA_t *plhba;
	SERV_PARM *sp;
	ELX_MBOXQ_t *mbox;
	ELX_SLI_t *psli;
	elxCfgParam_t *clp;
	LS_RJT stat;
	uint32_t cmd, did;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;
	clp = &phba->config[0];
	icmd = &cmdiocb->iocb;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	cmd = *lp++;
	sp = (SERV_PARM *) lp;

	/* FLOGI received */

	lpfc_set_disctmo(phba);

	if (plhba->fc_topology == TOPOLOGY_LOOP) {
		/* We should never receive a FLOGI in loop mode, ignore it */
		did = icmd->un.elsreq64.remoteID;

		/* An FLOGI ELS command <elsCmd> was received from DID <did> in Loop Mode */
		elx_printf_log(phba->brd_no, &elx_msgBlk0113,	/* ptr to msg structure */
			       elx_mes0113,	/* ptr to msg */
			       elx_msgBlk0113.msgPreambleStr,	/* begin varargs */
			       cmd, did);	/* end varargs */
		return (1);
	}

	did = Fabric_DID;

	if ((lpfc_check_sparm(phba, ndlp, sp, CLASS3))) {
		/* For a FLOGI we accept, then if our portname is greater
		 * then the remote portname we initiate Nport login. 
		 */
		int rc;

		rc = lpfc_geportname((NAME_TYPE *) & plhba->fc_portname,
				     (NAME_TYPE *) & sp->portName);

		if (rc == 2) {
			if ((mbox =
			     (ELX_MBOXQ_t *) elx_mem_get(phba,
							 MEM_MBOX)) == 0) {
				return (1);
			}
			lpfc_linkdown(phba);
			lpfc_init_link(phba, mbox,
				       clp[LPFC_CFG_TOPOLOGY].a_current,
				       clp[LPFC_CFG_LINK_SPEED].a_current);
			mbox->mb.un.varInitLnk.lipsr_AL_PA = 0;
			if (elx_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				elx_mem_put(phba, MEM_MBOX, (uint8_t *) mbox);
			}
			return (1);
		}

		if (rc == 1) {	/* greater than */
			plhba->fc_flag |= FC_PT2PT_PLOGI;
		}
		plhba->fc_flag |= FC_PT2PT;
		plhba->fc_flag &= ~(FC_FABRIC | FC_PUBLIC_LOOP);
	} else {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
		return (1);
	}

	/* Send back ACC */
	lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp, 0);

	return (0);
}

int
lpfc_els_rcv_rnid(elxHBA_t * phba,
		  ELX_IOCBQ_t * cmdiocb, LPFC_NODELIST_t * ndlp)
{
	DMABUF_t *pCmd;
	uint32_t *lp;
	IOCB_t *icmd;
	LPFCHBA_t *plhba;
	RNID *rn;
	LS_RJT stat;
	uint32_t cmd, did;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	icmd = &cmdiocb->iocb;
	did = icmd->un.elsreq64.remoteID;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	cmd = *lp++;
	rn = (RNID *) lp;

	/* RNID received */

	switch (rn->Format) {
	case 0:
	case RNID_TOPOLOGY_DISC:
		/* Send back ACC */
		lpfc_els_rsp_rnid_acc(phba, rn->Format, cmdiocb, ndlp);
		break;
	default:
		/* Reject this request because format not supported */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_CANT_GIVE_DATA;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	}
	return (0);
}

int
lpfc_els_rcv_rrq(elxHBA_t * phba, ELX_IOCBQ_t * cmdiocb, LPFC_NODELIST_t * ndlp)
{
	DMABUF_t *pCmd;
	uint32_t *lp;
	IOCB_t *icmd;
	LPFCHBA_t *plhba;
	ELX_SLI_RING_t *pring;
	ELX_SLI_t *psli;
	RRQ *rrq;
	uint32_t cmd, did;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;
	pring = &psli->ring[LPFC_FCP_RING];	/* FCP ring */
	icmd = &cmdiocb->iocb;
	did = icmd->un.elsreq64.remoteID;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	cmd = *lp++;
	rrq = (RRQ *) lp;

	/* RRQ received */
	/* Get oxid / rxid from payload and abort it */
	if ((rrq->SID == SWAP_DATA(plhba->fc_myDID))) {
		elx_sli_abort_iocb_ctx(phba, pring, rrq->Oxid);
	} else {
		elx_sli_abort_iocb_ctx(phba, pring, rrq->Rxid);
	}
	/* ACCEPT the rrq request */
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0);

	return (0);
}

int
lpfc_els_rcv_farp(elxHBA_t * phba,
		  ELX_IOCBQ_t * cmdiocb, LPFC_NODELIST_t * ndlp)
{
	DMABUF_t *pCmd;
	uint32_t *lp;
	IOCB_t *icmd;
	LPFCHBA_t *plhba;
	FARP *fp;
	uint32_t cmd, cnt, did;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	icmd = &cmdiocb->iocb;
	did = icmd->un.elsreq64.remoteID;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);
	cmd = *lp++;
	fp = (FARP *) lp;

	/* FARP-REQ received from DID <did> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0601,	/* ptr to msg structure */
		       elx_mes0601,	/* ptr to msg */
		       elx_msgBlk0601.msgPreambleStr,	/* begin varargs */
		       did);	/* end varargs */

	/* We will only support match on WWPN or WWNN */
	if (fp->Mflags & ~(FARP_MATCH_NODE | FARP_MATCH_PORT)) {
		return (0);
	}

	cnt = 0;
	/* If this FARP command is searching for my portname */
	if (fp->Mflags & FARP_MATCH_PORT) {
		if (lpfc_geportname(&fp->RportName, &plhba->fc_portname) == 2)
			cnt = 1;
	}

	/* If this FARP command is searching for my nodename */
	if (fp->Mflags & FARP_MATCH_NODE) {
		if (lpfc_geportname(&fp->RnodeName, &plhba->fc_nodename) == 2)
			cnt = 1;
	}

	if (cnt) {
		if ((ndlp->nle.nlp_failMask == 0) &&
		    (!(ndlp->nlp_flag & NLP_ELS_SND_MASK))) {
			/* Log back into the node before sending the FARP. */
			if (fp->Rflags & FARP_REQUEST_PLOGI) {
				lpfc_nlp_plogi(phba, ndlp);
				ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
				lpfc_issue_els_plogi(phba, ndlp, 0);
			}

			/* Send a FARP response to that node */
			if (fp->Rflags & FARP_REQUEST_FARPR) {
				lpfc_issue_els_farpr(phba, did, 0);
			}
		}
	}
	return (0);
}

int
lpfc_els_rcv_farpr(elxHBA_t * phba,
		   ELX_IOCBQ_t * cmdiocb, LPFC_NODELIST_t * ndlp)
{
	DMABUF_t *pCmd;
	uint32_t *lp;
	IOCB_t *icmd;
	LPFCHBA_t *plhba;
	uint32_t cmd, did;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	icmd = &cmdiocb->iocb;
	did = icmd->un.elsreq64.remoteID;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	cmd = *lp++;
	/* FARP-RSP received from DID <did> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0600,	/* ptr to msg structure */
		       elx_mes0600,	/* ptr to msg */
		       elx_msgBlk0600.msgPreambleStr,	/* begin varargs */
		       did);	/* end varargs */

	/* ACCEPT the Farp resp request */
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0);

	return (0);
}

int
lpfc_els_rcv_fan(elxHBA_t * phba, ELX_IOCBQ_t * cmdiocb, LPFC_NODELIST_t * ndlp)
{
	DMABUF_t *pCmd;
	uint32_t *lp;
	IOCB_t *icmd;
	LPFCHBA_t *plhba;
	FAN *fp;
	uint32_t cmd, did;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	icmd = &cmdiocb->iocb;
	did = icmd->un.elsreq64.remoteID;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	cmd = *lp++;
	fp = (FAN *) lp;

	/* FAN received */

	/* ACCEPT the FAN request */
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0);

	if (phba->hba_state == ELX_LOCAL_CFG_LINK) {
		/* The discovery state machine needs to take a different
		 * action if this node has switched fabrics 
		 */
		if ((lpfc_geportname((NAME_TYPE *) & fp->FportName,
				     (NAME_TYPE *) & plhba->fc_fabparam.
				     portName) != 2)
		    ||
		    (lpfc_geportname
		     ((NAME_TYPE *) & fp->FnodeName,
		      (NAME_TYPE *) & plhba->fc_fabparam.nodeName) != 2)) {
			/* This node has switched fabrics.  An FLOGI is required
			 * after the timeout 
			 */
			return (0);
		}

		/* Start discovery */
		lpfc_disc_start(phba);
	}

	return (0);
}

int
lpfc_els_chk_latt(elxHBA_t * phba, ELX_IOCBQ_t * rspiocb)
{
	ELX_SLI_t *psli;
	LPFCHBA_t *plhba;
	IOCB_t *irsp;
	ELX_MBOXQ_t *mbox;
	uint32_t ha_copy;

	psli = &phba->sli;
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	if (phba->hba_state < ELX_HBA_READY) {
		uint32_t tag, stat, wd4;

		/* Read the HBA Host Attention Register */
		ha_copy = (psli->sliinit.elx_sli_read_HA) (phba);

		if (ha_copy & HA_LATT) {	/* Link Attention interrupt */
			if (rspiocb) {
				irsp = &(rspiocb->iocb);
				tag = irsp->ulpIoTag;
				stat = irsp->ulpStatus;
				wd4 = irsp->un.ulpWord[4];
				irsp->ulpStatus = IOSTAT_DRIVER_REJECT;
				irsp->un.ulpWord[4] = IOERR_SLI_ABORTED;
			} else {
				tag = 0;
				stat = 0;
				wd4 = 0;
			}
			/* Pending Link Event during Discovery */
			elx_printf_log(phba->brd_no, &elx_msgBlk0237,	/* ptr to msg structure */
				       elx_mes0237,	/* ptr to msg */
				       elx_msgBlk0237.msgPreambleStr,	/* begin varargs */
				       phba->hba_state, tag, stat, wd4);	/* end varargs */

			if (phba->hba_state != ELX_CLEAR_LA) {
				if ((mbox =
				     (ELX_MBOXQ_t *) elx_mem_get(phba,
								 MEM_MBOX |
								 MEM_PRI))) {
					phba->hba_state = ELX_CLEAR_LA;
					lpfc_clear_la(phba, mbox);
					mbox->mbox_cmpl =
					    lpfc_mbx_cmpl_clear_la;
					if (elx_sli_issue_mbox
					    (phba, mbox,
					     (MBX_NOWAIT | MBX_STOP_IOCB))
					    == MBX_NOT_FINISHED) {
						elx_mem_put(phba, MEM_MBOX,
							    (uint8_t *) mbox);
						phba->hba_state = ELX_HBA_ERROR;
					}
				}
			}
			return (1);
		}
	}

	return (0);
}

void
lpfc_els_timeout_handler(elxHBA_t * phba, void *arg1, void *arg2)
{
	ELX_SLI_t *psli;
	ELX_SLI_RING_t *pring;
	ELX_IOCBQ_t *next_iocb;
	ELX_IOCBQ_t *piocb;
	IOCB_t *cmd = NULL;
	LPFCHBA_t *plhba;
	DMABUF_t *pCmd;
	ELX_DLINK_t *dlp;
	uint32_t *elscmd;
	uint32_t els_command;
	uint32_t timeout;
	uint32_t next_timeout;
	uint32_t remote_ID;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];
	dlp = &pring->txcmplq;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	timeout = (uint32_t) (unsigned long)arg1;
	next_timeout = plhba->fc_ratov << 1;

	next_iocb = (ELX_IOCBQ_t *) pring->txcmplq.q_f;
	while (next_iocb != (ELX_IOCBQ_t *) & pring->txcmplq) {
		piocb = next_iocb;
		next_iocb = next_iocb->q_f;
		cmd = &piocb->iocb;

		if (piocb->iocb_flag & ELX_IO_IOCTL) {
			continue;
		}
		pCmd = (DMABUF_t *) piocb->context2;
		elscmd = (uint32_t *) (pCmd->virt);
		els_command = *elscmd;

		if ((els_command == ELS_CMD_FARP)
		    || (els_command == ELS_CMD_FARPR)) {
			continue;
		}

		if (piocb->drvrTimeout > 0) {
			if (piocb->drvrTimeout >= timeout) {
				piocb->drvrTimeout -= timeout;
			} else {
				piocb->drvrTimeout = 0;
			}
			continue;
		}

		elx_deque(piocb);
		dlp->q_cnt--;

		if (cmd->ulpCommand == CMD_GEN_REQUEST64_CR) {
			LPFC_NODELIST_t *ndlp;

			ndlp = lpfc_findnode_rpi(phba, cmd->ulpContext);
			remote_ID = ndlp->nlp_DID;
		} else {
			remote_ID = cmd->un.elsreq64.remoteID;
		}

		elx_printf_log(phba->brd_no, &elx_msgBlk0127,	/* ptr to msg structure */
			       elx_mes0127,	/* ptr to msg */
			       elx_msgBlk0127.msgPreambleStr,	/* begin varargs */
			       els_command, remote_ID,	/* DID */
			       cmd->ulpCommand, cmd->ulpIoTag);	/* end varargs */

		/*
		 * The iocb has timed out; driver abort it.
		 */
		cmd->ulpStatus = IOSTAT_DRIVER_REJECT;
		cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;

		if (piocb->iocb_cmpl) {
			(piocb->iocb_cmpl) ((void *)phba, piocb, piocb);
		} else {
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) piocb);
		}
	}

	phba->els_tmofunc =
	    elx_clk_set(phba, next_timeout, lpfc_els_timeout_handler,
			(void *)(unsigned long)next_timeout, 0);
}

void
lpfc_els_flush_cmd(elxHBA_t * phba)
{
	ELX_SLI_t *psli;
	ELX_SLI_RING_t *pring;
	ELX_IOCBQ_t *next_iocb;
	ELX_IOCBQ_t *piocb;
	IOCB_t *cmd = NULL;
	LPFCHBA_t *plhba;
	DMABUF_t *pCmd;
	ELX_DLINK_t *dlp;
	uint32_t *elscmd;
	uint32_t els_command;
	uint32_t remote_ID;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];
	dlp = &pring->txcmplq;
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	next_iocb = (ELX_IOCBQ_t *) pring->txcmplq.q_f;
	while (next_iocb != (ELX_IOCBQ_t *) & pring->txcmplq) {
		piocb = next_iocb;
		next_iocb = next_iocb->q_f;
		cmd = &piocb->iocb;

		if (piocb->iocb_flag & ELX_IO_IOCTL) {
			continue;
		}
		pCmd = (DMABUF_t *) piocb->context2;
		elscmd = (uint32_t *) (pCmd->virt);
		els_command = *elscmd;

		if (cmd->ulpCommand == CMD_GEN_REQUEST64_CR) {
			LPFC_NODELIST_t *ndlp;

			ndlp = lpfc_findnode_rpi(phba, cmd->ulpContext);
			remote_ID = ndlp->nlp_DID;
			if (phba->hba_state == ELX_HBA_READY) {
				continue;
			}
		} else {
			remote_ID = cmd->un.elsreq64.remoteID;
		}

		elx_deque(piocb);
		dlp->q_cnt--;

		cmd->ulpStatus = IOSTAT_DRIVER_REJECT;
		cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;

		if (piocb->iocb_cmpl) {
			(piocb->iocb_cmpl) ((void *)phba, piocb, piocb);
		} else {
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) piocb);
		}
	}
	return;
}

int lpfc_matchdid(elxHBA_t *, LPFC_NODELIST_t *, uint32_t);
void lpfc_free_tx(elxHBA_t *, LPFC_NODELIST_t *);
void lpfc_put_buf(elxHBA_t *, void *, void *);
void lpfc_disc_retry_rptlun(elxHBA_t *, void *, void *);

/* Could be put in lpfc.conf; For now defined here */
int lpfc_qfull_retry_count = 5;

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

int
lpfc_linkdown(elxHBA_t * phba)
{
	LPFCHBA_t *plhba;
	ELX_SLI_t *psli;
	LPFC_NODELIST_t *ndlp;
	LPFC_NODELIST_t *new_ndlp;
	ELX_MBOXQ_t *mb;
	ELXSCSITARGET_t *targetp;
	elxCfgParam_t *clp;
	int rc;

	clp = &phba->config[0];
	psli = &phba->sli;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	phba->hba_state = ELX_LINK_DOWN;
	plhba->fc_flag |= FC_LNK_DOWN;

	dfc_put_event(phba, FC_REG_LINK_EVENT, 0, 0, 0);

	dfc_hba_put_event(phba, HBA_EVENT_LINK_DOWN, plhba->fc_myDID, 0, 0, 0);

	/* Clean up any firmware default rpi's */
	if ((mb = (ELX_MBOXQ_t *) elx_mem_get(phba, MEM_MBOX))) {
		lpfc_unreg_did(phba, 0xffffffff, mb);
		if (elx_sli_issue_mbox(phba, mb, (MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) mb);
		}
	}

	/* Cleanup any outstanding RSCN activity */
	lpfc_els_flush_rscn(phba);

	/* Cleanup any outstanding ELS commands */
	lpfc_els_flush_cmd(phba);

	/* Handle linkdown timer logic.   */

	if (!(plhba->fc_flag & FC_LD_TIMER)) {
		/* Should we start the link down watchdog timer */
		if ((clp[ELX_CFG_LINKDOWN_TMO].a_current == 0) ||
		    clp[ELX_CFG_HOLDIO].a_current) {
			plhba->fc_flag |= (FC_LD_TIMER | FC_LD_TIMEOUT);
			phba->hba_flag |= FC_LFR_ACTIVE;
		} else {
			plhba->fc_flag |= FC_LD_TIMER;
			phba->hba_flag |= FC_LFR_ACTIVE;
			if (plhba->fc_linkdown) {
				elx_clk_res(phba,
					    clp[ELX_CFG_LINKDOWN_TMO].a_current,
					    plhba->fc_linkdown);
			} else {
				if (clp[ELX_CFG_HOLDIO].a_current == 0) {
					plhba->fc_linkdown = elx_clk_set(phba,
									 clp
									 [ELX_CFG_LINKDOWN_TMO].
									 a_current,
									 lpfc_linkdown_timeout,
									 0, 0);
				}
			}
		}
	}

	/* Issue a LINK DOWN event to all nodes */
	ndlp = plhba->fc_plogi_start;
	if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_plogi_start) {
		ndlp = plhba->fc_adisc_start;
		if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_adisc_start) {
			ndlp = plhba->fc_nlpunmap_start;
			if (ndlp ==
			    (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start) {
				ndlp = plhba->fc_nlpmap_start;
			}
		}
	}
	while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start) {
		new_ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;

		/* Fabric nodes are not handled thru state machine for link down */
		if (!(ndlp->nle.nlp_type & NLP_FABRIC)) {
			lpfc_set_failmask(phba, ndlp, ELX_DEV_LINK_DOWN,
					  ELX_SET_BITMASK);

			rc = lpfc_disc_state_machine(phba, ndlp, (void *)0,
						     NLP_EVT_DEVICE_UNK);
			if ((rc != NLP_STE_FREED_NODE)
			    && (clp[LPFC_CFG_USE_ADISC].a_current == 0)) {
				/* If we are not using ADISC when the link comes back up, we might
				 * as well free all the nodes right now.
				 */
				targetp = ndlp->nlp_Target;
				/* If we were a FCP target, go into NPort Recovery mode to give
				 * it a chance to come back.
				 */
				if (targetp) {
					if ((clp[ELX_CFG_NODEV_TMO].a_current)
					    && (clp[ELX_CFG_HOLDIO].a_current ==
						0)) {
						targetp->targetFlags |=
						    FC_NPR_ACTIVE;
						if (targetp->tmofunc) {
							elx_clk_can(phba,
								    targetp->
								    tmofunc);
						}
						if (clp[ELX_CFG_NODEV_TMO].
						    a_current >
						    clp[ELX_CFG_LINKDOWN_TMO].
						    a_current) {
							targetp->tmofunc =
							    elx_clk_set(phba,
									clp
									[ELX_CFG_NODEV_TMO].
									a_current,
									lpfc_npr_timeout,
									(void *)
									targetp,
									(void *)
									0);
						} else {
							targetp->tmofunc =
							    elx_clk_set(phba,
									clp
									[ELX_CFG_LINKDOWN_TMO].
									a_current,
									lpfc_npr_timeout,
									(void *)
									targetp,
									(void *)
									0);
						}
					} else {
						elx_sched_flush_target(phba,
								       targetp,
								       IOSTAT_DRIVER_REJECT,
								       IOERR_SLI_ABORTED);
					}
				}

				lpfc_findnode_did(phba,
						  (NLP_SEARCH_ALL |
						   NLP_SEARCH_DEQUE),
						  ndlp->nlp_DID);
				lpfc_freenode(phba, ndlp);
				elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
			}
		}
		ndlp = new_ndlp;
		if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_plogi_start)
			ndlp = plhba->fc_adisc_start;
		if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_adisc_start)
			ndlp = plhba->fc_nlpunmap_start;
		if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start)
			ndlp = plhba->fc_nlpmap_start;
	}

	/* Setup myDID for link up if we are in pt2pt mode */
	if (plhba->fc_flag & FC_PT2PT) {
		plhba->fc_myDID = 0;
		if ((mb = (ELX_MBOXQ_t *) elx_mem_get(phba, MEM_MBOX))) {
			lpfc_config_link(phba, mb);
			if (elx_sli_issue_mbox
			    (phba, mb, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				elx_mem_put(phba, MEM_MBOX, (uint8_t *) mb);
			}
		}
		plhba->fc_flag &= ~(FC_PT2PT | FC_PT2PT_PLOGI);
	}
	plhba->fc_flag &= ~FC_LBIT;

	/* Turn off discovery timer if its running */
	lpfc_can_disctmo(phba);

	if (clp[LPFC_CFG_NETWORK_ON].a_current) {
		plhba->fc_nlp_bcast.nle.nlp_failMask = ELX_DEV_LINK_DOWN;
	}

	/* Must process IOCBs on all rings to handle ABORTed I/Os */
	return (0);
}

int
lpfc_linkup(elxHBA_t * phba)
{
	LPFCHBA_t *plhba;
	LPFC_NODELIST_t *ndlp;
	LPFC_NODELIST_t *new_ndlp;
	elxCfgParam_t *clp;
	unsigned long iflag;

	clp = &phba->config[0];
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	phba->hba_state = ELX_LINK_UP;
	phba->hba_flag |= FC_NDISC_ACTIVE;
	plhba->fc_flag &= ~(FC_LNK_DOWN | FC_PT2PT | FC_PT2PT_PLOGI |
			    FC_RSCN_MODE | FC_NLP_MORE | FC_DELAY_DISC |
			    FC_RSCN_DISC_TMR | FC_RSCN_DISCOVERY | FC_LD_TIMER |
			    FC_LD_TIMEOUT);
	plhba->fc_ns_retry = 0;

	dfc_put_event(phba, FC_REG_LINK_EVENT, 0, 0, 0);

	dfc_hba_put_event(phba, HBA_EVENT_LINK_UP, plhba->fc_myDID,
			  plhba->fc_topology, 0, plhba->fc_linkspeed);

	if (plhba->fc_linkdown) {
		elx_clk_can(phba, plhba->fc_linkdown);
		plhba->fc_linkdown = 0;
	}

	/*
	 * Clean up old Fabric, NameServer and other NLP_FABRIC logins.
	 */
	ELX_DISC_LOCK(phba, iflag);
	ndlp = plhba->fc_nlpunmap_start;
	while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start) {
		new_ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
		if (ndlp->nle.nlp_type & NLP_FABRIC) {
			ndlp->nlp_flag &=
			    ~(NLP_UNMAPPED_LIST | NLP_TGT_NO_SCSIID);
			plhba->fc_unmap_cnt--;
			elx_deque(ndlp);
			ELX_DISC_UNLOCK(phba, iflag);
			lpfc_freenode(phba, ndlp);
			elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
			ELX_DISC_LOCK(phba, iflag);
		}
		ndlp = new_ndlp;
	}
	ELX_DISC_UNLOCK(phba, iflag);

	/* Mark all nodes for LINK UP */
	ELX_DISC_LOCK(phba, iflag);
	ndlp = plhba->fc_plogi_start;
	if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_plogi_start)
		ndlp = plhba->fc_adisc_start;
	if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_adisc_start)
		ndlp = plhba->fc_nlpunmap_start;
	if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start)
		ndlp = plhba->fc_nlpmap_start;
	while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start) {
		new_ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;

		lpfc_set_failmask(phba, ndlp, ELX_DEV_RPTLUN, ELX_SET_BITMASK);
		lpfc_set_failmask(phba, ndlp, ELX_DEV_LINK_DOWN,
				  ELX_CLR_BITMASK);

		if (ndlp->nlp_flag & NLP_NODEV_TMO) {
			if (elx_clk_rem(phba, ndlp->nlp_tmofunc) >
			    clp[ELX_CFG_NODEV_TMO].a_current) {
				elx_clk_res(phba,
					    clp[ELX_CFG_NODEV_TMO].a_current,
					    ndlp->nlp_tmofunc);
			}
		}

		ndlp = new_ndlp;
		if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_plogi_start)
			ndlp = plhba->fc_adisc_start;
		if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_adisc_start)
			ndlp = plhba->fc_nlpunmap_start;
		if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start)
			ndlp = plhba->fc_nlpmap_start;
	}
	ELX_DISC_UNLOCK(phba, iflag);

	return (0);
}

/*
 * This routine handles processing a READ_LA mailbox
 * command upon completion. It is setup in the ELX_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_read_la(elxHBA_t * phba, ELX_MBOXQ_t * pmb)
{
	DMABUF_t *mp;
	ELX_SLI_t *psli;
	READ_LA_VAR *la;
	LPFCHBA_t *plhba;
	ELX_MBOXQ_t *mbox;
	MAILBOX_t *mb;
	elxCfgParam_t *clp;
	uint32_t control;
	int i;

	clp = &phba->config[0];
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;
	mb = &pmb->mb;
	/* Check for error */
	if (mb->mbxStatus) {
		/* READ_LA mbox error <mbxStatus> state <hba_state> */
		elx_printf_log(phba->brd_no, &elx_msgBlk1307,	/* ptr to msg structure */
			       elx_mes1307,	/* ptr to msg */
			       elx_msgBlk1307.msgPreambleStr,	/* begin varargs */
			       mb->mbxStatus, phba->hba_state);	/* end varargs */

		lpfc_linkdown(phba);
		phba->hba_state = ELX_HBA_ERROR;

		/* turn on Link Attention interrupts */
		psli->sliinit.sli_flag |= ELX_PROCESS_LA;
		control = (psli->sliinit.elx_sli_read_HC) (phba);
		control |= HC_LAINT_ENA;
		(psli->sliinit.elx_sli_write_HC) (phba, control);
		return;
	}
	la = (READ_LA_VAR *) & pmb->mb.un.varReadLA;

	mp = (DMABUF_t *) (pmb->context1);
	elx_pci_dma_sync((void *)phba, (void *)mp, 0, ELX_DMA_SYNC_FORCPU);

	/* Get Loop Map information */
	if (mp) {
		memcpy((void *)&plhba->alpa_map[0], (void *)mp->virt, 128);
	} else {
		memset((void *)&plhba->alpa_map[0], 0, 128);
	}

	if (la->pb)
		plhba->fc_flag |= FC_BYPASSED_MODE;
	else
		plhba->fc_flag &= ~FC_BYPASSED_MODE;

	if (((plhba->fc_eventTag + 1) < la->eventTag) ||
	    (plhba->fc_eventTag == la->eventTag)) {
		plhba->fc_stat.LinkMultiEvent++;
		if (la->attType == AT_LINK_UP) {
			if (plhba->fc_eventTag != 0) {

				lpfc_linkdown(phba);
			}
		}
	}

	plhba->fc_eventTag = la->eventTag;

	if (la->attType == AT_LINK_UP) {
		plhba->fc_stat.LinkUp++;
		/* Link Up Event <eventTag> received */
		elx_printf_log(phba->brd_no, &elx_msgBlk1303,	/* ptr to msg structure */
			       elx_mes1303,	/* ptr to msg */
			       elx_msgBlk1303.msgPreambleStr,	/* begin varargs */
			       la->eventTag, plhba->fc_eventTag, la->granted_AL_PA, la->UlnkSpeed, plhba->alpa_map[0]);	/* end varargs */

		if (la->UlnkSpeed == LA_2GHZ_LINK)
			plhba->fc_linkspeed = LA_2GHZ_LINK;
		else
			plhba->fc_linkspeed = 0;

		if ((plhba->fc_topology = la->topology) == TOPOLOGY_LOOP) {

			if (la->il) {
				plhba->fc_flag |= FC_LBIT;
			}

			plhba->fc_myDID = la->granted_AL_PA;

			i = la->un.lilpBde64.tus.f.bdeSize;
			if (i == 0) {
				plhba->alpa_map[0] = 0;
			} else {
				if (clp[ELX_CFG_LOG_VERBOSE].
				    a_current & LOG_LINK_EVENT) {
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

					numalpa = plhba->alpa_map[0];
					j = 0;
					while (j < numalpa) {
						memset(un.pamap, 0, 16);
						for (k = 1; j < numalpa; k++) {
							un.pamap[k - 1] =
							    plhba->alpa_map[j +
									    1];
							j++;
							if (k == 16)
								break;
						}
						/* Link Up Event ALPA map */
						elx_printf_log(phba->brd_no, &elx_msgBlk1304,	/* ptr to msg struct */
							       elx_mes1304,	/* ptr to msg */
							       elx_msgBlk1304.msgPreambleStr,	/* begin varargs */
							       un.pa.wd1, un.pa.wd2, un.pa.wd3, un.pa.wd4);	/* end varargs */
					}
				}
			}
		} else {
			plhba->fc_myDID = plhba->fc_pref_DID;
			plhba->fc_flag |= FC_LBIT;
		}

		lpfc_linkup(phba);
		if ((mbox =
		     (ELX_MBOXQ_t *) elx_mem_get(phba, MEM_MBOX | MEM_PRI))) {
			lpfc_read_sparam(phba, mbox);
			mbox->mbox_cmpl = lpfc_mbx_cmpl_read_sparam;
			elx_mbox_put(phba, mbox);
		}

		if ((mbox =
		     (ELX_MBOXQ_t *) elx_mem_get(phba, MEM_MBOX | MEM_PRI))) {
			phba->hba_state = ELX_LOCAL_CFG_LINK;
			lpfc_config_link(phba, mbox);
			mbox->mbox_cmpl = lpfc_mbx_cmpl_config_link;
			elx_mbox_put(phba, mbox);
		}
	} else {
		plhba->fc_stat.LinkDown++;
		/* Link Down Event <eventTag> received */
		elx_printf_log(phba->brd_no, &elx_msgBlk1305,	/* ptr to msg structure */
			       elx_mes1305,	/* ptr to msg */
			       elx_msgBlk1305.msgPreambleStr,	/* begin varargs */
			       la->eventTag, plhba->fc_eventTag, phba->hba_state, plhba->fc_flag);	/* end varargs */

		lpfc_linkdown(phba);

		/* turn on Link Attention interrupts - no CLEAR_LA needed */
		psli->sliinit.sli_flag |= ELX_PROCESS_LA;
		control = (psli->sliinit.elx_sli_read_HC) (phba);
		control |= HC_LAINT_ENA;
		(psli->sliinit.elx_sli_write_HC) (phba, control);
	}

	pmb->context1 = 0;
	elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
	elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
	return;
}

void
lpfc_mbx_cmpl_config_link(elxHBA_t * phba, ELX_MBOXQ_t * pmb)
{
	LPFCHBA_t *plhba;
	ELX_SLI_t *psli;
	MAILBOX_t *mb;

	psli = &phba->sli;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	mb = &pmb->mb;
	/* Check for error */
	if (mb->mbxStatus) {
		/* CONFIG_LINK mbox error <mbxStatus> state <hba_state> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0306,	/* ptr to msg structure */
			       elx_mes0306,	/* ptr to msg */
			       elx_msgBlk0306.msgPreambleStr,	/* begin varargs */
			       mb->mbxStatus, phba->hba_state);	/* end varargs */

		lpfc_linkdown(phba);
		phba->hba_state = ELX_HBA_ERROR;
		goto out;
	}

	if (phba->hba_state == ELX_LOCAL_CFG_LINK) {
		if (plhba->fc_topology == TOPOLOGY_LOOP) {
			/* If we are public loop and L bit was set */
			if ((plhba->fc_flag & FC_PUBLIC_LOOP) &&
			    !(plhba->fc_flag & FC_LBIT)) {
				/* Need to wait for FAN - use discovery timer for timeout.
				 * hba_state is identically  ELX_LOCAL_CFG_LINK while waiting for FAN
				 */
				lpfc_set_disctmo(phba);
				elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
				return;
			}
		}

		/* Start discovery by sending a FLOGI
		 * hba_state is identically ELX_FLOGI while waiting for FLOGI cmpl
		 */
		phba->hba_state = ELX_FLOGI;
		lpfc_set_disctmo(phba);
		lpfc_initial_flogi(phba);
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
		return;
	}

      out:
	/* CONFIG_LINK bad hba state <hba_state> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0200,	/* ptr to msg structure */
		       elx_mes0200,	/* ptr to msg */
		       elx_msgBlk0200.msgPreambleStr,	/* begin varargs */
		       phba->hba_state);	/* end varargs */

	if (phba->hba_state != ELX_CLEAR_LA) {
		lpfc_clear_la(phba, pmb);
		pmb->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
		if (elx_sli_issue_mbox(phba, pmb, (MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
			lpfc_disc_flush_list(phba);
			psli->ring[(psli->ip_ring)].flag &=
			    ~ELX_STOP_IOCB_EVENT;
			psli->ring[(psli->fcp_ring)].flag &=
			    ~ELX_STOP_IOCB_EVENT;
			psli->ring[(psli->next_ring)].flag &=
			    ~ELX_STOP_IOCB_EVENT;
			phba->hba_state = ELX_HBA_READY;
		}
	} else {
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
	}
	return;
}

void
lpfc_mbx_cmpl_read_sparam(elxHBA_t * phba, ELX_MBOXQ_t * pmb)
{
	LPFCHBA_t *plhba;
	ELX_SLI_t *psli;
	MAILBOX_t *mb;
	DMABUF_t *mp;

	psli = &phba->sli;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	mb = &pmb->mb;
	/* Check for error */
	if (mb->mbxStatus) {
		/* READ_SPARAM mbox error <mbxStatus> state <hba_state> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0319,	/* ptr to msg structure */
			       elx_mes0319,	/* ptr to msg */
			       elx_msgBlk0319.msgPreambleStr,	/* begin varargs */
			       mb->mbxStatus, phba->hba_state);	/* end varargs */

		lpfc_linkdown(phba);
		phba->hba_state = ELX_HBA_ERROR;
		goto out;
	}

	mp = (DMABUF_t *) pmb->context1;
	elx_pci_dma_sync((void *)phba, (void *)mp, 0, ELX_DMA_SYNC_FORCPU);

	memcpy((uint8_t *) & plhba->fc_sparam, (uint8_t *) mp->virt,
	       sizeof (SERV_PARM));
	memcpy((uint8_t *) & plhba->fc_nodename,
	       (uint8_t *) & plhba->fc_sparam.nodeName, sizeof (NAME_TYPE));
	memcpy((uint8_t *) & plhba->fc_portname,
	       (uint8_t *) & plhba->fc_sparam.portName, sizeof (NAME_TYPE));
	memcpy(plhba->phys_addr, plhba->fc_portname.IEEE, 6);
	elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
	elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
	return;

      out:
	if (phba->hba_state != ELX_CLEAR_LA) {
		lpfc_clear_la(phba, pmb);
		pmb->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
		if (elx_sli_issue_mbox(phba, pmb, (MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
			lpfc_disc_flush_list(phba);
			psli->ring[(psli->ip_ring)].flag &=
			    ~ELX_STOP_IOCB_EVENT;
			psli->ring[(psli->fcp_ring)].flag &=
			    ~ELX_STOP_IOCB_EVENT;
			psli->ring[(psli->next_ring)].flag &=
			    ~ELX_STOP_IOCB_EVENT;
			phba->hba_state = ELX_HBA_READY;
		}
	} else {
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
	}
	return;
}

/*
 * This routine handles processing a CLEAR_LA mailbox
 * command upon completion. It is setup in the ELX_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_clear_la(elxHBA_t * phba, ELX_MBOXQ_t * pmb)
{
	LPFCHBA_t *plhba;
	elxCfgParam_t *clp;
	ELX_SLI_t *psli;
	LPFC_NODELIST_t *ndlp;
	MAILBOX_t *mb;
	uint32_t control;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;
	clp = &phba->config[0];
	mb = &pmb->mb;
	/* Since we don't do discovery right now, turn these off here */
	psli->ring[psli->ip_ring].flag &= ~ELX_STOP_IOCB_EVENT;
	psli->ring[psli->fcp_ring].flag &= ~ELX_STOP_IOCB_EVENT;
	psli->ring[psli->next_ring].flag &= ~ELX_STOP_IOCB_EVENT;
	/* Check for error */
	if ((mb->mbxStatus) && (mb->mbxStatus != 0x1601)) {
		/* CLEAR_LA mbox error <mbxStatus> state <hba_state> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0320,	/* ptr to msg structure */
			       elx_mes0320,	/* ptr to msg */
			       elx_msgBlk0320.msgPreambleStr,	/* begin varargs */
			       mb->mbxStatus, phba->hba_state);	/* end varargs */

		phba->hba_state = ELX_HBA_ERROR;
		goto out;
	}

	plhba->num_disc_nodes = 0;
	/* go thru PLOGI list and issue ELS PLOGIs */
	if (plhba->fc_plogi_cnt) {
		ndlp = plhba->fc_plogi_start;
		while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_plogi_start) {
			if (ndlp->nlp_state == NLP_STE_UNUSED_NODE) {
				ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
				lpfc_issue_els_plogi(phba, ndlp, 0);
				ndlp->nlp_flag |= NLP_DISC_NODE;
				plhba->num_disc_nodes++;
				if (plhba->num_disc_nodes >=
				    clp[LPFC_CFG_DISC_THREADS].a_current) {
					if (plhba->fc_plogi_cnt >
					    plhba->num_disc_nodes)
						plhba->fc_flag |= FC_NLP_MORE;
					break;
				}
			}
			ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
		}
	}
	if (plhba->num_disc_nodes == 0) {
		phba->hba_flag &= ~FC_NDISC_ACTIVE;
	}
	phba->hba_state = ELX_HBA_READY;
	if (clp[LPFC_CFG_NETWORK_ON].a_current) {
		plhba->fc_nlp_bcast.nle.nlp_failMask = 0;
	}
      out:
	/* Device Discovery completes */
	elx_printf_log(phba->brd_no, &elx_msgBlk0225,	/* ptr to msg structure */
		       elx_mes0225,	/* ptr to msg */
		       elx_msgBlk0225.msgPreambleStr);	/* begin & end varargs */

	phba->hba_flag &= ~FC_LFR_ACTIVE;

	elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
	if (plhba->fc_flag & FC_ESTABLISH_LINK) {
		plhba->fc_flag &= ~FC_ESTABLISH_LINK;
	}
	if (plhba->fc_estabtmo) {
		elx_clk_can(phba, plhba->fc_estabtmo);
		plhba->fc_estabtmo = 0;
	}
	lpfc_can_disctmo(phba);

	/* turn on Link Attention interrupts */
	psli->sliinit.sli_flag |= ELX_PROCESS_LA;
	control = (psli->sliinit.elx_sli_read_HC) (phba);
	control |= HC_LAINT_ENA;
	(psli->sliinit.elx_sli_write_HC) (phba, control);

	/* If there are mapped FCP nodes still running, restart the scheduler 
	 * to get any pending IOCBs out.
	 */
	if (plhba->fc_map_cnt) {
		elx_sched_check(phba);
	}
	return;
}

/*
 * This routine handles processing a REG_LOGIN mailbox
 * command upon completion. It is setup in the ELX_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_reg_login(elxHBA_t * phba, ELX_MBOXQ_t * pmb)
{
	ELX_SLI_t *psli;
	MAILBOX_t *mb;
	DMABUF_t *mp;
	LPFC_NODELIST_t *ndlp;

	psli = &phba->sli;
	mb = &pmb->mb;

	ndlp = (LPFC_NODELIST_t *) pmb->context2;
	mp = (DMABUF_t *) (pmb->context1);

	elx_pci_dma_sync((void *)phba, (void *)mp, 0, ELX_DMA_SYNC_FORCPU);
	pmb->context1 = 0;

	/* Good status, call state machine */
	lpfc_disc_state_machine(phba, ndlp, (void *)pmb,
				NLP_EVT_CMPL_REG_LOGIN);
	elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
	elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);

	return;
}

/*
 * This routine handles processing a Fabric REG_LOGIN mailbox
 * command upon completion. It is setup in the ELX_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_fabric_reg_login(elxHBA_t * phba, ELX_MBOXQ_t * pmb)
{
	ELX_SLI_t *psli;
	MAILBOX_t *mb;
	DMABUF_t *mp;
	LPFC_NODELIST_t *ndlp;
	LPFC_NODELIST_t *ndlp_fdmi;
	elxCfgParam_t *clp;

	clp = &phba->config[0];

	psli = &phba->sli;
	mb = &pmb->mb;

	ndlp = (LPFC_NODELIST_t *) pmb->context2;
	mp = (DMABUF_t *) (pmb->context1);

	elx_pci_dma_sync((void *)phba, (void *)mp, 0, ELX_DMA_SYNC_FORCPU);
	pmb->context1 = 0;

	if (ndlp->nle.nlp_rpi != 0)
		lpfc_findnode_remove_rpi(phba, ndlp->nle.nlp_rpi);
	ndlp->nle.nlp_rpi = mb->un.varWords[0];
	lpfc_addnode_rpi(phba, ndlp, ndlp->nle.nlp_rpi);
	ndlp->nle.nlp_type |= NLP_FABRIC;
	lpfc_nlp_unmapped(phba, ndlp);
	ndlp->nlp_state = NLP_STE_PRLI_COMPL;

	if (phba->hba_state == ELX_FABRIC_CFG_LINK) {
		/* This NPort has been assigned an NPort_ID by the fabric as a result
		 * of the completed fabric login.  Issue a State Change Registration (SCR) 
		 * ELS request to the fabric controller (SCR_DID) so that this
		 * NPort gets RSCN events from the fabric.
		 */
		lpfc_issue_els_scr(phba, SCR_DID, 0);

		/* Allocate a new node instance.  If the pool is empty, just start 
		 * the discovery process and skip the Nameserver login process.  
		 * This is attempted again later on.  Otherwise, issue a Port Login (PLOGI)
		 * to the NameServer 
		 */
		if ((ndlp =
		     (LPFC_NODELIST_t *) elx_mem_get(phba, MEM_NLP)) == 0) {
			lpfc_disc_start(phba);
		} else {
			memset((void *)ndlp, 0, sizeof (LPFC_NODELIST_t));
			ndlp->nle.nlp_type |= NLP_FABRIC;
			ndlp->nlp_DID = NameServer_DID;
			ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
			lpfc_issue_els_plogi(phba, ndlp, 0);
			if (clp[LPFC_CFG_FDMI_ON].a_current) {
				if ((ndlp_fdmi =
				     (LPFC_NODELIST_t *) elx_mem_get(phba,
								     MEM_NLP)))
				{
					memset((void *)ndlp_fdmi, 0,
					       sizeof (LPFC_NODELIST_t));
					ndlp_fdmi->nle.nlp_type |= NLP_FABRIC;
					ndlp_fdmi->nlp_DID = FDMI_DID;
					ndlp_fdmi->nlp_state =
					    NLP_STE_PLOGI_ISSUE;
					lpfc_issue_els_plogi(phba, ndlp_fdmi,
							     0);
				}
			}
		}
	}

	elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
	elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);

	return;
}

/*
 * This routine handles processing a NameServer REG_LOGIN mailbox
 * command upon completion. It is setup in the ELX_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_ns_reg_login(elxHBA_t * phba, ELX_MBOXQ_t * pmb)
{
	LPFCHBA_t *plhba;
	ELX_SLI_t *psli;
	MAILBOX_t *mb;
	DMABUF_t *mp;
	LPFC_NODELIST_t *ndlp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;
	mb = &pmb->mb;

	ndlp = (LPFC_NODELIST_t *) pmb->context2;
	mp = (DMABUF_t *) (pmb->context1);

	elx_pci_dma_sync((void *)phba, (void *)mp, 0, ELX_DMA_SYNC_FORCPU);
	pmb->context1 = 0;

	if (ndlp->nle.nlp_rpi != 0)
		lpfc_findnode_remove_rpi(phba, ndlp->nle.nlp_rpi);
	ndlp->nle.nlp_rpi = mb->un.varWords[0];
	lpfc_addnode_rpi(phba, ndlp, ndlp->nle.nlp_rpi);
	ndlp->nle.nlp_type |= NLP_FABRIC;
	lpfc_nlp_unmapped(phba, ndlp);
	ndlp->nlp_state = NLP_STE_PRLI_COMPL;

	if (phba->hba_state < ELX_HBA_READY) {
		/* Link up discovery requires Fabrib registration. */
		lpfc_ns_cmd(phba, ndlp, SLI_CTNS_RNN_ID);
		lpfc_ns_cmd(phba, ndlp, SLI_CTNS_RSNN_NN);
		lpfc_ns_cmd(phba, ndlp, SLI_CTNS_RFT_ID);
	}

	plhba->fc_ns_retry = 0;
	/* Good status, issue CT Request to NameServer */
	if (lpfc_ns_cmd(phba, ndlp, SLI_CTNS_GID_FT)) {
		/* Cannot issue NameServer Query, so finish up discovery */
		lpfc_disc_start(phba);
	}

	elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
	elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);

	return;
}

/* Put blp on the bind list */
int
lpfc_nlp_bind(elxHBA_t * phba, LPFC_BINDLIST_t * blp)
{
	ELX_DLINK_t *end_blp;
	LPFCHBA_t *plhba;
	ELXSCSITARGET_t *targetp;
	ELXSCSILUN_t *lunp;
	unsigned long iflag;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	ELX_DISC_LOCK(phba, iflag);
	/* Put it at the end of the bind list */
	end_blp = (ELX_DLINK_t *) plhba->fc_nlpbind_end;
	elx_enque(((ELX_DLINK_t *) blp), end_blp);
	plhba->fc_bind_cnt++;
	targetp =
	    plhba->device_queue_hash[FC_SCSID(blp->nlp_pan, blp->nlp_sid)];
	if (targetp) {
		targetp->pcontext = 0;
		lunp = (ELXSCSILUN_t *) targetp->lunlist.q_first;
		while (lunp) {
			lunp->pnode = 0;
			lunp = lunp->pnextLun;
		}
	}
	ELX_DISC_UNLOCK(phba, iflag);

	/* Add scsiid <sid> to BIND list */
	elx_printf_log(phba->brd_no, &elx_msgBlk0903,	/* ptr to msg structure */
		       elx_mes0903,	/* ptr to msg */
		       elx_msgBlk0903.msgPreambleStr,	/* begin varargs */
		       blp->nlp_sid, plhba->fc_bind_cnt, blp->nlp_DID, blp->nlp_bind_type, (unsigned long)blp);	/* end varargs */

	return (0);
}

/* Put blp on the plogi list */
int
lpfc_nlp_plogi(elxHBA_t * phba, LPFC_NODELIST_t * nlp)
{
	ELX_DLINK_t *end_nlp;
	LPFCHBA_t *plhba;
	unsigned long iflag;
	LPFC_BINDLIST_t *blp;
	ELX_SLI_t *psli;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;
	blp = 0;

	ELX_DISC_LOCK(phba, iflag);
	/* Check to see if this node exists on any other list */
	if (nlp->nlp_flag & NLP_LIST_MASK) {
		if (nlp->nlp_flag & NLP_MAPPED_LIST) {
			nlp->nlp_flag &= ~NLP_MAPPED_LIST;
			plhba->fc_map_cnt--;
			elx_deque(nlp);

			/* Must call before binding is removed */
			lpfc_set_failmask(phba, nlp, ELX_DEV_DISCONNECTED,
					  ELX_SET_BITMASK);

			blp = nlp->nlp_listp_bind;
			if (blp) {
				blp->nlp_Target = nlp->nlp_Target;
			}
			elx_sched_flush_target(phba, nlp->nlp_Target,
					       IOSTAT_DRIVER_REJECT,
					       IOERR_SLI_ABORTED);
			nlp->nlp_listp_bind = 0;
			nlp->nlp_pan = 0;
			nlp->nlp_sid = 0;
			nlp->nlp_Target = 0;
			nlp->nlp_flag &= ~NLP_SEED_MASK;

			/* This node is moving to the plogi list so something has happened.
			 * Flush all pending IP bufs.
			 */
			if (nlp->nle.nlp_type & NLP_IP_NODE) {
				lpfc_ip_flush_iocb(phba,
						   &psli->ring[psli->ip_ring],
						   nlp, FLUSH_NODE);
			}
		} else if (nlp->nlp_flag & NLP_UNMAPPED_LIST) {
			nlp->nlp_flag &=
			    ~(NLP_UNMAPPED_LIST | NLP_TGT_NO_SCSIID);
			plhba->fc_unmap_cnt--;
			elx_deque(nlp);

			/* This node is moving to the plogi list so something has happened.
			 * Flush all pending IP bufs.
			 */
			if (nlp->nle.nlp_type & NLP_IP_NODE) {
				lpfc_ip_flush_iocb(phba,
						   &psli->ring[psli->ip_ring],
						   nlp, FLUSH_NODE);
			}
		} else if (nlp->nlp_flag & NLP_PLOGI_LIST) {
			ELX_DISC_UNLOCK(phba, iflag);
			return (0);	/* Already on plogi list */
		} else if (nlp->nlp_flag & NLP_ADISC_LIST) {
			nlp->nlp_flag &= ~NLP_ADISC_LIST;
			plhba->fc_adisc_cnt--;
			elx_deque(nlp);
		}
	}

	/* Put it at the end of the plogi list */
	end_nlp = (ELX_DLINK_t *) plhba->fc_plogi_end;
	elx_enque(((ELX_DLINK_t *) nlp), end_nlp);
	plhba->fc_plogi_cnt++;
	nlp->nlp_flag |= NLP_PLOGI_LIST;
	ELX_DISC_UNLOCK(phba, iflag);

	/* Add NPort <did> to PLOGI list */
	elx_printf_log(phba->brd_no, &elx_msgBlk0904,	/* ptr to msg structure */
		       elx_mes0904,	/* ptr to msg */
		       elx_msgBlk0904.msgPreambleStr,	/* begin varargs */
		       nlp->nlp_DID, plhba->fc_plogi_cnt, (unsigned long)blp);	/* end varargs */

	if (blp) {
		lpfc_nlp_bind(phba, blp);
	}
	return (0);
}

/* Put nlp on the adisc list */
int
lpfc_nlp_adisc(elxHBA_t * phba, LPFC_NODELIST_t * nlp)
{
	ELX_DLINK_t *end_nlp;
	LPFCHBA_t *plhba;
	unsigned long iflag;
	LPFC_BINDLIST_t *blp;
	ELX_SLI_t *psli;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	blp = 0;
	psli = &phba->sli;

	ELX_DISC_LOCK(phba, iflag);

	/* Check to see if this node exist on any other list */
	if (nlp->nlp_flag & NLP_LIST_MASK) {
		if (nlp->nlp_flag & NLP_MAPPED_LIST) {
			nlp->nlp_flag &= ~NLP_MAPPED_LIST;
			plhba->fc_map_cnt--;
			elx_deque(nlp);

			/* Must call before binding is removed */
			lpfc_set_failmask(phba, nlp, ELX_DEV_DISAPPEARED,
					  ELX_SET_BITMASK);

			blp = nlp->nlp_listp_bind;
			if (blp) {
				blp->nlp_Target = nlp->nlp_Target;
			}

			if (nlp->nlp_Target) {
				elx_sched_flush_target(phba, nlp->nlp_Target,
						       IOSTAT_DRIVER_REJECT,
						       IOERR_SLI_ABORTED);
			}

			nlp->nlp_listp_bind = 0;
			nlp->nlp_Target = 0;

			/* Keep pan and sid since ELX_DEV_DISAPPEARED
			 * is a non-fatal error
			 */
			nlp->nlp_flag &= ~NLP_SEED_MASK;

			/* This node is moving to the adisc list so something has happened.
			 * Flush all pending IP bufs.
			 */
			if (nlp->nle.nlp_type & NLP_IP_NODE) {
				lpfc_ip_flush_iocb(phba,
						   &psli->ring[psli->ip_ring],
						   nlp, FLUSH_NODE);
			}
		} else if (nlp->nlp_flag & NLP_UNMAPPED_LIST) {
			nlp->nlp_flag &=
			    ~(NLP_UNMAPPED_LIST | NLP_TGT_NO_SCSIID);
			plhba->fc_unmap_cnt--;
			elx_deque(nlp);

			/* This node is moving to the adisc list so something has happened.
			 * Flush all pending IP bufs.
			 */
			if (nlp->nle.nlp_type & NLP_IP_NODE) {
				lpfc_ip_flush_iocb(phba,
						   &psli->ring[psli->ip_ring],
						   nlp, FLUSH_NODE);
			}
		} else if (nlp->nlp_flag & NLP_PLOGI_LIST) {
			nlp->nlp_flag &= ~NLP_PLOGI_LIST;
			plhba->fc_plogi_cnt--;
			elx_deque(nlp);
		} else if (nlp->nlp_flag & NLP_ADISC_LIST) {
			ELX_DISC_UNLOCK(phba, iflag);
			return (0);	/* Already on adisc list */
		}
	}

	/* Put it at the end of the adisc list */
	end_nlp = (ELX_DLINK_t *) plhba->fc_adisc_end;
	elx_enque(((ELX_DLINK_t *) nlp), end_nlp);
	plhba->fc_adisc_cnt++;
	nlp->nlp_flag |= NLP_ADISC_LIST;
	ELX_DISC_UNLOCK(phba, iflag);

	/* Add NPort <did> to ADISC list */
	elx_printf_log(phba->brd_no, &elx_msgBlk0905,	/* ptr to msg structure */
		       elx_mes0905,	/* ptr to msg */
		       elx_msgBlk0905.msgPreambleStr,	/* begin varargs */
		       nlp->nlp_DID, plhba->fc_adisc_cnt, (unsigned long)blp);	/* end varargs */

	if (blp) {
		lpfc_nlp_bind(phba, blp);
	}

	return (0);
}

/*
 * Put nlp on the unmapped list 
 * NOTE: - update nlp_type to NLP_FC_NODE
 */
int
lpfc_nlp_unmapped(elxHBA_t * phba, LPFC_NODELIST_t * nlp)
{
	ELX_DLINK_t *end_nlp;
	LPFCHBA_t *plhba;
	unsigned long iflag;
	LPFC_BINDLIST_t *blp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	blp = 0;
	ELX_DISC_LOCK(phba, iflag);
	/* Check to see if this node exists on any other list */
	if (nlp->nlp_flag & NLP_LIST_MASK) {
		if (nlp->nlp_flag & NLP_MAPPED_LIST) {
			nlp->nlp_flag &= ~NLP_MAPPED_LIST;
			plhba->fc_map_cnt--;
			elx_deque(nlp);

			/* Must call before binding is removed */
			lpfc_set_failmask(phba, nlp, ELX_DEV_DISAPPEARED,
					  ELX_SET_BITMASK);

			blp = nlp->nlp_listp_bind;
			if (blp) {
				blp->nlp_Target = nlp->nlp_Target;
			}
			elx_sched_flush_target(phba, nlp->nlp_Target,
					       IOSTAT_DRIVER_REJECT,
					       IOERR_SLI_ABORTED);
			nlp->nlp_listp_bind = 0;
			nlp->nlp_Target = 0;
			/* Keep pan and sid since ELX_DEV_DISAPPEARED
			 * is a non-fatal error
			 */
			nlp->nlp_flag &= ~NLP_SEED_MASK;
		} else if (nlp->nlp_flag & NLP_UNMAPPED_LIST) {
			ELX_DISC_UNLOCK(phba, iflag);
			return (0);	/* Already on unmapped list */
		} else if (nlp->nlp_flag & NLP_PLOGI_LIST) {
			nlp->nlp_flag &= ~NLP_PLOGI_LIST;
			plhba->fc_plogi_cnt--;
			elx_deque(nlp);
		} else if (nlp->nlp_flag & NLP_ADISC_LIST) {
			nlp->nlp_flag &= ~NLP_ADISC_LIST;
			plhba->fc_adisc_cnt--;
			elx_deque(nlp);
		}
	}

	/* Put it at the end of the unmapped list */
	end_nlp = (ELX_DLINK_t *) plhba->fc_nlpunmap_end;
	elx_enque(((ELX_DLINK_t *) nlp), end_nlp);
	plhba->fc_unmap_cnt++;
	nlp->nle.nlp_type |= NLP_FC_NODE;
	nlp->nlp_flag |= NLP_UNMAPPED_LIST;
	ELX_DISC_UNLOCK(phba, iflag);

	/* Add NPort <did> to UNMAP list */
	elx_printf_log(phba->brd_no, &elx_msgBlk0906,	/* ptr to msg structure */
		       elx_mes0906,	/* ptr to msg */
		       elx_msgBlk0906.msgPreambleStr,	/* begin varargs */
		       nlp->nlp_DID, plhba->fc_unmap_cnt, (unsigned long)blp);	/* end varargs */

	if (blp) {
		lpfc_nlp_bind(phba, blp);
	}
	return (0);
}

/*
 * Put nlp on the mapped list 
 * NOTE: - update nlp_type to NLP_FCP_TARGET
 *       - attach binding entry to context2 
 */
int
lpfc_nlp_mapped(elxHBA_t * phba, LPFC_NODELIST_t * nlp, LPFC_BINDLIST_t * blp)
{
	ELX_DLINK_t *end_nlp;
	LPFCHBA_t *plhba;
	ELXSCSITARGET_t *targetp;
	ELXSCSILUN_t *lunp;
	unsigned long iflag;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	ELX_DISC_LOCK(phba, iflag);

	/* Check to see if this node exists on any other list */
	if (nlp->nlp_flag & NLP_LIST_MASK) {
		if (nlp->nlp_flag & NLP_MAPPED_LIST) {
			ELX_DISC_UNLOCK(phba, iflag);
			return (0);	/* Already on mapped list */
		} else if (nlp->nlp_flag & NLP_UNMAPPED_LIST) {
			nlp->nlp_flag &=
			    ~(NLP_UNMAPPED_LIST | NLP_TGT_NO_SCSIID);
			plhba->fc_unmap_cnt--;
			elx_deque(nlp);
		} else if (nlp->nlp_flag & NLP_PLOGI_LIST) {
			nlp->nlp_flag &= ~NLP_PLOGI_LIST;
			plhba->fc_plogi_cnt--;
			elx_deque(nlp);
		} else if (nlp->nlp_flag & NLP_ADISC_LIST) {
			nlp->nlp_flag &= ~NLP_ADISC_LIST;
			plhba->fc_adisc_cnt--;
			elx_deque(nlp);
		}
	}

	/* Put it at the end of the mapped list */
	end_nlp = (ELX_DLINK_t *) plhba->fc_nlpmap_end;
	elx_enque(((ELX_DLINK_t *) nlp), end_nlp);
	plhba->fc_map_cnt++;
	nlp->nlp_flag |= NLP_MAPPED_LIST;
	nlp->nle.nlp_type |= NLP_FCP_TARGET;
	nlp->nlp_pan = blp->nlp_pan;
	nlp->nlp_sid = blp->nlp_sid;
	nlp->nlp_Target = blp->nlp_Target;
	nlp->nlp_listp_bind = blp;
	targetp =
	    plhba->device_queue_hash[FC_SCSID(nlp->nlp_pan, nlp->nlp_sid)];

	/* Add NPort <did> to MAPPED list scsiid <sid> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0907,	/* ptr to msg structure */
		       elx_mes0907,	/* ptr to msg */
		       elx_msgBlk0907.msgPreambleStr,	/* begin varargs */
		       nlp->nlp_DID, nlp->nlp_sid, plhba->fc_map_cnt, (unsigned long)blp);	/* end varargs */

	if (targetp) {
		nlp->nlp_Target = targetp;
		targetp->pcontext = (void *)nlp;
		lunp = (ELXSCSILUN_t *) targetp->lunlist.q_first;
		while (lunp) {
			lunp->pnode = (ELX_NODELIST_t *) nlp;
			lunp = lunp->pnextLun;
		}
	}
	/* Must call after binding is associated */
	ELX_DISC_UNLOCK(phba, iflag);

	return (0);
}

/*
 * Start / ReStart rescue timer for Discovery / RSCN handling
 */
void *
lpfc_set_disctmo(elxHBA_t * phba)
{
	LPFCHBA_t *plhba;
	uint32_t tmo;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	tmo = ((plhba->fc_ratov * 2) + 1);

	/* Turn off discovery timer if its running */
	if (plhba->fc_disctmo) {
		elx_clk_can(phba, plhba->fc_disctmo);
		plhba->fc_disctmo = 0;
	}
	plhba->fc_disctmo = elx_clk_set(phba, tmo, lpfc_disc_timeout, 0, 0);

	/* Start Discovery Timer state <hba_state> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0247,	/* ptr to msg structure */
		       elx_mes0247,	/* ptr to msg */
		       elx_msgBlk0247.msgPreambleStr,	/* begin varargs */
		       phba->hba_state, tmo, (unsigned long)plhba->fc_disctmo, plhba->fc_plogi_cnt, plhba->fc_adisc_cnt);	/* end varargs */

	return ((void *)plhba->fc_disctmo);
}

/*
 * Cancel rescue timer for Discovery / RSCN handling
 */
int
lpfc_can_disctmo(elxHBA_t * phba)
{
	LPFCHBA_t *plhba;
	int rc;

	rc = 0;
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	/* Turn off discovery timer if its running */
	if (plhba->fc_disctmo) {
		elx_clk_can(phba, plhba->fc_disctmo);
		plhba->fc_disctmo = 0;
		rc = 1;
	}

	/* Cancel Discovery Timer state <hba_state> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0248,	/* ptr to msg structure */
		       elx_mes0248,	/* ptr to msg */
		       elx_msgBlk0248.msgPreambleStr,	/* begin varargs */
		       phba->hba_state, plhba->fc_flag, rc, plhba->fc_plogi_cnt, plhba->fc_adisc_cnt);	/* end varargs */

	return (rc);
}

/*
 * Check specified ring for outstanding IOCB on the SLI queue
 * Return true if iocb matches the specified nport
 */
int
lpfc_check_sli_ndlp(elxHBA_t * phba,
		    ELX_SLI_RING_t * pring,
		    ELX_IOCBQ_t * iocb, LPFC_NODELIST_t * ndlp)
{
	ELX_SLI_t *psli;
	IOCB_t *icmd;

	psli = &phba->sli;
	icmd = &iocb->iocb;
	if (pring->ringno == LPFC_ELS_RING) {
		switch (icmd->ulpCommand) {
		case CMD_GEN_REQUEST64_CR:
			if (icmd->ulpContext ==
			    (volatile ushort)ndlp->nle.nlp_rpi)
				return (1);
		case CMD_ELS_REQUEST64_CR:
		case CMD_XMIT_ELS_RSP64_CX:
			if (iocb->context1 == (uint8_t *) ndlp)
				return (1);
		}
	} else if (pring->ringno == psli->ip_ring) {

	} else if (pring->ringno == psli->fcp_ring) {
		if (icmd->ulpContext == (volatile ushort)ndlp->nle.nlp_rpi)
			return (1);
	} else if (pring->ringno == psli->next_ring) {

	}
	return (0);
}

/*
 * Free resources / clean up outstanding I/Os
 * associated with nlp_rpi in the LPFC_NODELIST entry.
 */
int
lpfc_no_rpi(elxHBA_t * phba, LPFC_NODELIST_t * ndlp)
{
	ELX_SLI_t *psli;
	ELX_SLI_RING_t *pring;
	ELX_IOCBQ_t *iocb, *next_iocb;
	IOCB_t *icmd;
	unsigned long iflag;
	uint32_t rpi, i;

	psli = &phba->sli;
	rpi = ndlp->nle.nlp_rpi;
	if (rpi) {
		/* Now process each ring */
		for (i = 0; i < psli->sliinit.num_rings; i++) {
			pring = &psli->ring[i];

			ELX_SLI_LOCK(phba, iflag);
			next_iocb = (ELX_IOCBQ_t *) pring->txq.q_f;
			while (next_iocb != (ELX_IOCBQ_t *) & pring->txq) {
				iocb = next_iocb;
				next_iocb = next_iocb->q_f;
				/* Check to see if iocb matches the nport we are looking for */
				if ((lpfc_check_sli_ndlp
				     (phba, pring, iocb, ndlp))) {
					/* It matches, so deque and call compl with an error */
					elx_deque(iocb);
					pring->txq.q_cnt--;
					if (iocb->iocb_cmpl) {
						icmd = &iocb->iocb;
						icmd->ulpStatus =
						    IOSTAT_DRIVER_REJECT;
						icmd->un.ulpWord[4] =
						    IOERR_SLI_ABORTED;
						ELX_SLI_UNLOCK(phba, iflag);
						(iocb->iocb_cmpl) ((void *)phba,
								   iocb, iocb);
						ELX_SLI_LOCK(phba, iflag);
					} else {
						elx_mem_put(phba, MEM_IOCB,
							    (uint8_t *) iocb);
					}
				}
			}
			/* Everything that matches on txcmplq will be returned by 
			 * firmware with a no rpi error.
			 */
			ELX_SLI_UNLOCK(phba, iflag);
		}
	}
	return (0);
}

/*
 * Free resources / clean up outstanding I/Os
 * associated with a LPFC_NODELIST entry. This
 * routine effectively results in a "software abort".
 */
int
lpfc_driver_abort(elxHBA_t * phba, LPFC_NODELIST_t * ndlp)
{
	ELX_SLI_t *psli;
	ELX_SLI_RING_t *pring;
	ELX_IOCBQ_t *iocb, *next_iocb;
	IOCB_t *icmd;
	ELXCLOCK_t *clkp;
	unsigned long iflag;
	uint32_t i, cmd;

	/* Abort outstanding I/O on NPort <nlp_DID> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0201,	/* ptr to msg structure */
		       elx_mes0201,	/* ptr to msg */
		       elx_msgBlk0201.msgPreambleStr,	/* begin varargs */
		       ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state, ndlp->nle.nlp_rpi);	/* end varargs */

	psli = &phba->sli;
	/* Now process each ring */
	for (i = 0; i < psli->sliinit.num_rings; i++) {
		pring = &psli->ring[i];

		ELX_SLI_LOCK(phba, iflag);
		/* First check the txq */
		next_iocb = (ELX_IOCBQ_t *) pring->txq.q_f;
		while (next_iocb != (ELX_IOCBQ_t *) & pring->txq) {
			iocb = next_iocb;
			next_iocb = next_iocb->q_f;
			/* Check to see if iocb matches the nport we are looking for */
			if ((lpfc_check_sli_ndlp(phba, pring, iocb, ndlp))) {
				/* It matches, so deque and call compl with an error */
				elx_deque(iocb);
				pring->txq.q_cnt--;
				if (iocb->iocb_cmpl) {
					icmd = &iocb->iocb;
					icmd->ulpStatus = IOSTAT_DRIVER_REJECT;
					icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
					ELX_SLI_UNLOCK(phba, iflag);
					(iocb->iocb_cmpl) ((void *)phba, iocb,
							   iocb);
					ELX_SLI_LOCK(phba, iflag);
				} else {
					elx_mem_put(phba, MEM_IOCB,
						    (uint8_t *) iocb);
				}
			}
		}
		/* Everything on txcmplq will be returned by 
		 * firmware with a no rpi / linkdown / abort  error.
		 * For ring 0, ELS discovery, we want to get rid of it right here.
		 */
		if (pring->ringno == LPFC_ELS_RING) {
			/* Next check the txcmplq */
			next_iocb = (ELX_IOCBQ_t *) pring->txcmplq.q_f;
			while (next_iocb != (ELX_IOCBQ_t *) & pring->txcmplq) {
				iocb = next_iocb;
				next_iocb = next_iocb->q_f;
				/* Check to see if iocb matches the nport we are looking for */
				if ((lpfc_check_sli_ndlp
				     (phba, pring, iocb, ndlp))) {
					/* It matches, so deque and call compl with an error */
					elx_deque(iocb);
					pring->txcmplq.q_cnt--;

					icmd = &iocb->iocb;
					/* If the driver is completing an ELS command early, flush it out
					 * of the firmware.
					 */
					if ((icmd->ulpCommand ==
					     CMD_ELS_REQUEST64_CR)
					    && (icmd->un.elsreq64.bdl.
						ulpIoTag32)) {
						ELX_SLI_UNLOCK(phba, iflag);
						elx_sli_issue_abort_iotag32
						    (phba, pring, iocb);
						ELX_SLI_LOCK(phba, iflag);
					}
					if (iocb->iocb_cmpl) {
						icmd->ulpStatus =
						    IOSTAT_DRIVER_REJECT;
						icmd->un.ulpWord[4] =
						    IOERR_SLI_ABORTED;
						ELX_SLI_UNLOCK(phba, iflag);
						(iocb->iocb_cmpl) ((void *)phba,
								   iocb, iocb);
						ELX_SLI_LOCK(phba, iflag);
					} else {
						elx_mem_put(phba, MEM_IOCB,
							    (uint8_t *) iocb);
					}
				}
			}
		}
		ELX_SLI_UNLOCK(phba, iflag);
	}

	/* If we are delaying issuing an ELS command, cancel it */
	if ((ndlp->nlp_tmofunc) && (ndlp->nlp_flag & NLP_DELAY_TMO)) {
		ndlp->nlp_flag &= ~(NLP_NODEV_TMO | NLP_DELAY_TMO);
		clkp = (ELXCLOCK_t *) (ndlp->nlp_tmofunc);
		cmd = (uint32_t) (unsigned long)clkp->cl_arg2;
		elx_clk_can(phba, ndlp->nlp_tmofunc);
		ndlp->nlp_tmofunc = 0;
		ndlp->nle.nlp_rflag &= ~NLP_NPR_ACTIVE;

		/* Allocate an IOCB and indicate an error completion */
		/* Allocate a buffer for the command iocb */
		if ((iocb =
		     (ELX_IOCBQ_t *) elx_mem_get(phba,
						 MEM_IOCB | MEM_PRI)) == 0) {
			return (0);
		}
		memset((void *)iocb, 0, sizeof (ELX_IOCBQ_t));
		icmd = &iocb->iocb;
		icmd->ulpStatus = IOSTAT_DRIVER_REJECT;
		icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
		iocb->context1 = (void *)ndlp;

		switch (cmd) {
		case ELS_CMD_FLOGI:
			iocb->iocb_cmpl = lpfc_cmpl_els_flogi;
			break;
		case ELS_CMD_PLOGI:
			iocb->iocb_cmpl = lpfc_cmpl_els_plogi;
			break;
		case ELS_CMD_ADISC:
			iocb->iocb_cmpl = lpfc_cmpl_els_adisc;
			break;
		case ELS_CMD_PRLI:
			iocb->iocb_cmpl = lpfc_cmpl_els_prli;
			break;
		case ELS_CMD_LOGO:
			iocb->iocb_cmpl = lpfc_cmpl_els_logo;
			break;
		default:
			iocb->iocb_cmpl = lpfc_cmpl_els_cmd;
			break;
		}
		(iocb->iocb_cmpl) ((void *)phba, iocb, iocb);
	}
	return (0);
}

/*
 * Free resources associated with LPFC_NODELIST entry
 * so it can be freed.
 */
int
lpfc_freenode(elxHBA_t * phba, LPFC_NODELIST_t * ndlp)
{
	ELX_MBOXQ_t *mbox;
	ELX_SLI_t *psli;

	/* The psli variable gets rid of the long pointer deference. */
	psli = &phba->sli;

	/* Cleanup node for NPort <nlp_DID> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0900,	/* ptr to msg structure */
		       elx_mes0900,	/* ptr to msg */
		       elx_msgBlk0900.msgPreambleStr,	/* begin varargs */
		       ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state, ndlp->nle.nlp_rpi);	/* end varargs */

	/* NLP_FREED_NODE flag is to protect the node from being freed
	 * more then once. For driver_abort and other cases where the DSM 
	 * calls itself recursively, its possible to free the node twice.
	 */
	if (ndlp->nle.nlp_rflag & NLP_FREED_NODE) {
		return (0);
	}

	if (ndlp->nlp_flag & NLP_LIST_MASK) {
		lpfc_findnode_did(phba, (NLP_SEARCH_ALL | NLP_SEARCH_DEQUE),
				  ndlp->nlp_DID);
	}

	if (ndlp->nlp_tmofunc) {
		elx_clk_can(phba, ndlp->nlp_tmofunc);
		ndlp->nlp_flag &= ~(NLP_NODEV_TMO | NLP_DELAY_TMO);
		ndlp->nle.nlp_rflag &= ~NLP_NPR_ACTIVE;
		ndlp->nlp_tmofunc = 0;
	}

	if (ndlp->nle.nlp_type & NLP_IP_NODE) {
		lpfc_ip_flush_iocb(phba, &psli->ring[psli->ip_ring], ndlp,
				   FLUSH_NODE);
	}

	if (ndlp->nle.nlp_rpi) {
		if ((mbox =
		     (ELX_MBOXQ_t *) elx_mem_get(phba, MEM_MBOX | MEM_PRI))) {
			lpfc_unreg_login(phba, ndlp->nle.nlp_rpi, mbox);
			if (elx_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				elx_mem_put(phba, MEM_MBOX, (uint8_t *) mbox);
			}
		}
		lpfc_findnode_remove_rpi(phba, ndlp->nle.nlp_rpi);
		lpfc_no_rpi(phba, ndlp);
		ndlp->nle.nlp_rpi = 0;
		lpfc_set_failmask(phba, ndlp, ELX_DEV_DISCONNECTED,
				  ELX_SET_BITMASK);
	}

	return (0);
}

int
lpfc_matchdid(elxHBA_t * phba, LPFC_NODELIST_t * ndlp, uint32_t did)
{
	D_ID mydid;
	D_ID ndlpdid;
	D_ID matchdid;
	int zero_did;
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	if (did == Bcast_DID)
		return (0);

	zero_did = 0;
	if (ndlp->nlp_DID == 0) {
		return (0);
	}

	/* First check for Direct match */
	if (ndlp->nlp_DID == did)
		return (1);

	/* Next check for area/domain identically equals 0 match */
	mydid.un.word = plhba->fc_myDID;
	if ((mydid.un.b.domain == 0) && (mydid.un.b.area == 0)) {
		goto out;
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
			goto out;
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
      out:
	if (zero_did)
		ndlp->nlp_DID = 0;
	return (0);
}

/* Search for a nodelist entry on a specific list */
LPFC_NODELIST_t *
lpfc_findnode_scsiid(elxHBA_t * phba, uint32_t scsid)
{
	LPFC_NODELIST_t *ndlp;
	LPFCHBA_t *plhba;
	ELXSCSITARGET_t *targetp;
	unsigned long iflag;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	ELX_DISC_LOCK(phba, iflag);

	targetp = plhba->device_queue_hash[scsid];
	/* First see if the SCSI ID has an allocated ELXSCSITARGET_t */
	if (targetp) {
		if (targetp->pcontext) {
			ELX_DISC_UNLOCK(phba, iflag);
			return ((LPFC_NODELIST_t *) targetp->pcontext);
		}
	}

	/* Now try the hard way */
	ndlp = plhba->fc_nlpmap_start;
	while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start) {
		if (scsid == FC_SCSID(ndlp->nlp_pan, ndlp->nlp_sid)) {
			ELX_DISC_UNLOCK(phba, iflag);
			return (ndlp);
		}
		ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
	}

	ELX_DISC_UNLOCK(phba, iflag);

	/* no match found */
	return ((LPFC_NODELIST_t *) 0);
}

/* Search for a nodelist entry on a specific list */
LPFC_NODELIST_t *
lpfc_findnode_wwnn(elxHBA_t * phba, uint32_t order, NAME_TYPE * wwnn)
{
	LPFC_NODELIST_t *ndlp;
	LPFCHBA_t *plhba;
	uint32_t data1;
	unsigned long iflag;
	LPFC_BINDLIST_t *blp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	blp = 0;
	ELX_DISC_LOCK(phba, iflag);
	if (order & NLP_SEARCH_UNMAPPED) {
		ndlp = plhba->fc_nlpunmap_start;
		while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start) {
			if (lpfc_geportname(&ndlp->nlp_nodename, wwnn) == 2) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nle.nlp_type << 8) |
					 ((uint32_t) ndlp->nle.nlp_rpi & 0xff));
				/* FIND node DID unmapped */
				elx_printf_log(phba->brd_no, &elx_msgBlk0910,	/* ptr to msg structure */
					       elx_mes0910,	/* ptr to msg */
					       elx_msgBlk0910.msgPreambleStr,	/* begin varargs */
					       (ulong) ndlp, ndlp->nlp_DID, ndlp->nlp_flag, data1);	/* end varargs */
				if (order & NLP_SEARCH_DEQUE) {
					ndlp->nlp_flag &=
					    ~(NLP_UNMAPPED_LIST |
					      NLP_TGT_NO_SCSIID);
					plhba->fc_unmap_cnt--;
					elx_deque(ndlp);
				}
				ELX_DISC_UNLOCK(phba, iflag);
				return (ndlp);
			}
			ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
		}
	}

	if (order & NLP_SEARCH_MAPPED) {
		ndlp = plhba->fc_nlpmap_start;
		while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start) {
			if (lpfc_geportname(&ndlp->nlp_nodename, wwnn) == 2) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nle.nlp_type << 8) |
					 ((uint32_t) ndlp->nle.nlp_rpi & 0xff));
				/* FIND node did mapped */
				elx_printf_log(phba->brd_no, &elx_msgBlk0902,	/* ptr to msg structure */
					       elx_mes0902,	/* ptr to msg */
					       elx_msgBlk0902.msgPreambleStr,	/* begin varargs */
					       (ulong) ndlp, ndlp->nlp_DID, ndlp->nlp_flag, data1);	/* end varargs */
				if (order & NLP_SEARCH_DEQUE) {
					ndlp->nlp_flag &= ~NLP_MAPPED_LIST;
					plhba->fc_map_cnt--;
					elx_deque(ndlp);

					/* Must call before binding is removed */
					lpfc_set_failmask(phba, ndlp,
							  ELX_DEV_DISAPPEARED,
							  ELX_SET_BITMASK);

					blp = ndlp->nlp_listp_bind;
					ndlp->nlp_listp_bind = 0;
					if (blp) {
						blp->nlp_Target =
						    ndlp->nlp_Target;
					}
					/* Keep Target, pan and sid since ELX_DEV_DISAPPEARED
					 * is a non-fatal error
					 */
					ndlp->nlp_flag &= ~NLP_SEED_MASK;
				}
				ELX_DISC_UNLOCK(phba, iflag);
				if (blp) {
					lpfc_nlp_bind(phba, blp);
				}
				return (ndlp);
			}
			ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
		}
	}

	ELX_DISC_UNLOCK(phba, iflag);

	/* no match found */
	return ((LPFC_NODELIST_t *) 0);
}

/* Search for a nodelist entry on a specific list */
LPFC_NODELIST_t *
lpfc_findnode_wwpn(elxHBA_t * phba, uint32_t order, NAME_TYPE * wwpn)
{
	LPFC_NODELIST_t *ndlp;
	LPFCHBA_t *plhba;
	uint32_t data1;
	unsigned long iflag;
	LPFC_BINDLIST_t *blp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	blp = 0;
	ELX_DISC_LOCK(phba, iflag);
	if (order & NLP_SEARCH_UNMAPPED) {
		ndlp = plhba->fc_nlpunmap_start;
		while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start) {
			if (lpfc_geportname(&ndlp->nlp_portname, wwpn) == 2) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nle.nlp_type << 8) |
					 ((uint32_t) ndlp->nle.nlp_rpi & 0xff));
				/* FIND node DID unmapped */
				elx_printf_log(phba->brd_no, &elx_msgBlk0911,	/* ptr to msg structure */
					       elx_mes0911,	/* ptr to msg */
					       elx_msgBlk0911.msgPreambleStr,	/* begin varargs */
					       (ulong) ndlp, ndlp->nlp_DID, ndlp->nlp_flag, data1);	/* end varargs */
				if (order & NLP_SEARCH_DEQUE) {
					ndlp->nlp_flag &=
					    ~(NLP_UNMAPPED_LIST |
					      NLP_TGT_NO_SCSIID);
					plhba->fc_unmap_cnt--;
					elx_deque(ndlp);
				}
				ELX_DISC_UNLOCK(phba, iflag);
				return (ndlp);
			}
			ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
		}
	}

	if (order & NLP_SEARCH_MAPPED) {
		ndlp = plhba->fc_nlpmap_start;
		while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start) {
			if (lpfc_geportname(&ndlp->nlp_portname, wwpn) == 2) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nle.nlp_type << 8) |
					 ((uint32_t) ndlp->nle.nlp_rpi & 0xff));
				/* FIND node DID mapped */
				elx_printf_log(phba->brd_no, &elx_msgBlk0901,	/* ptr to msg structure */
					       elx_mes0901,	/* ptr to msg */
					       elx_msgBlk0901.msgPreambleStr,	/* begin varargs */
					       (ulong) ndlp, ndlp->nlp_DID, ndlp->nlp_flag, data1);	/* end varargs */
				if (order & NLP_SEARCH_DEQUE) {
					ndlp->nlp_flag &= ~NLP_MAPPED_LIST;
					plhba->fc_map_cnt--;
					elx_deque(ndlp);

					/* Must call before binding is removed */
					lpfc_set_failmask(phba, ndlp,
							  ELX_DEV_DISAPPEARED,
							  ELX_SET_BITMASK);

					blp = ndlp->nlp_listp_bind;
					ndlp->nlp_listp_bind = 0;
					if (blp) {
						blp->nlp_Target =
						    ndlp->nlp_Target;
					}
					/* Keep Target, pan and sid since ELX_DEV_DISAPPEARED
					 * is a non-fatal error
					 */
					ndlp->nlp_flag &= ~NLP_SEED_MASK;
				}
				ELX_DISC_UNLOCK(phba, iflag);
				if (blp) {
					lpfc_nlp_bind(phba, blp);
				}
				return (ndlp);
			}
			ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
		}
	}

	ELX_DISC_UNLOCK(phba, iflag);

	/* no match found */
	return ((LPFC_NODELIST_t *) 0);
}

/* Search for a nodelist entry on a specific list */
LPFC_NODELIST_t *
lpfc_findnode_did(elxHBA_t * phba, uint32_t order, uint32_t did)
{
	LPFC_NODELIST_t *ndlp;
	LPFCHBA_t *plhba;
	uint32_t data1;
	unsigned long iflag;
	LPFC_BINDLIST_t *blp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	blp = 0;
	ELX_DISC_LOCK(phba, iflag);
	if (order & NLP_SEARCH_UNMAPPED) {
		ndlp = plhba->fc_nlpunmap_start;
		while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start) {
			if (lpfc_matchdid(phba, ndlp, did)) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nle.nlp_type << 8) |
					 ((uint32_t) ndlp->nle.nlp_rpi & 0xff));
				/* FIND node DID unmapped */
				elx_printf_log(phba->brd_no, &elx_msgBlk0929,	/* ptr to msg structure */
					       elx_mes0929,	/* ptr to msg */
					       elx_msgBlk0929.msgPreambleStr,	/* begin varargs */
					       (ulong) ndlp, ndlp->nlp_DID, ndlp->nlp_flag, data1);	/* end varargs */
				if (order & NLP_SEARCH_DEQUE) {
					ndlp->nlp_flag &=
					    ~(NLP_UNMAPPED_LIST |
					      NLP_TGT_NO_SCSIID);
					plhba->fc_unmap_cnt--;
					elx_deque(ndlp);
				}
				ELX_DISC_UNLOCK(phba, iflag);
				return (ndlp);
			}
			ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
		}
	}

	if (order & NLP_SEARCH_MAPPED) {
		ndlp = plhba->fc_nlpmap_start;
		while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start) {
			if (lpfc_matchdid(phba, ndlp, did)) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nle.nlp_type << 8) |
					 ((uint32_t) ndlp->nle.nlp_rpi & 0xff));
				/* FIND node DID mapped */
				elx_printf_log(phba->brd_no, &elx_msgBlk0930,	/* ptr to msg structure */
					       elx_mes0930,	/* ptr to msg */
					       elx_msgBlk0930.msgPreambleStr,	/* begin varargs */
					       (ulong) ndlp, ndlp->nlp_DID, ndlp->nlp_flag, data1);	/* end varargs */
				if (order & NLP_SEARCH_DEQUE) {
					ndlp->nlp_flag &= ~NLP_MAPPED_LIST;
					plhba->fc_map_cnt--;
					elx_deque(ndlp);

					/* Must call before binding is removed */
					lpfc_set_failmask(phba, ndlp,
							  ELX_DEV_DISAPPEARED,
							  ELX_SET_BITMASK);

					blp = ndlp->nlp_listp_bind;
					ndlp->nlp_listp_bind = 0;
					if (blp) {
						blp->nlp_Target =
						    ndlp->nlp_Target;
					}
					/* Keep Target, pan and sid since ELX_DEV_DISAPPEARED
					 * is a non-fatal error
					 */
					ndlp->nlp_flag &= ~NLP_SEED_MASK;
				}
				ELX_DISC_UNLOCK(phba, iflag);
				if (blp) {
					lpfc_nlp_bind(phba, blp);
				}
				return (ndlp);
			}
			ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
		}
	}

	if (order & NLP_SEARCH_PLOGI) {
		ndlp = plhba->fc_plogi_start;
		while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_plogi_start) {
			if (lpfc_matchdid(phba, ndlp, did)) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nle.nlp_type << 8) |
					 ((uint32_t) ndlp->nle.nlp_rpi & 0xff));
				/* LOG change to PLOGI */
				/* FIND node DID bind */
				elx_printf_log(phba->brd_no, &elx_msgBlk0908,	/* ptr to msg structure */
					       elx_mes0908,	/* ptr to msg */
					       elx_msgBlk0908.msgPreambleStr,	/* begin varargs */
					       (ulong) ndlp, ndlp->nlp_DID, ndlp->nlp_flag, data1);	/* end varargs */
				if (order & NLP_SEARCH_DEQUE) {
					ndlp->nlp_flag &= ~NLP_PLOGI_LIST;
					plhba->fc_plogi_cnt--;
					elx_deque(ndlp);
				}
				ELX_DISC_UNLOCK(phba, iflag);
				return (ndlp);
			}
			ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
		}
	}

	if (order & NLP_SEARCH_ADISC) {
		ndlp = plhba->fc_adisc_start;
		while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_adisc_start) {
			if (lpfc_matchdid(phba, ndlp, did)) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nle.nlp_type << 8) |
					 ((uint32_t) ndlp->nle.nlp_rpi & 0xff));
				/* LOG change to ADISC */
				/* FIND node DID bind */
				elx_printf_log(phba->brd_no, &elx_msgBlk0931,	/* ptr to msg structure */
					       elx_mes0931,	/* ptr to msg */
					       elx_msgBlk0931.msgPreambleStr,	/* begin varargs */
					       (ulong) ndlp, ndlp->nlp_DID, ndlp->nlp_flag, data1);	/* end varargs */
				if (order & NLP_SEARCH_DEQUE) {
					ndlp->nlp_flag &= ~NLP_ADISC_LIST;
					plhba->fc_adisc_cnt--;
					elx_deque(ndlp);
				}
				ELX_DISC_UNLOCK(phba, iflag);
				return (ndlp);
			}
			ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
		}
	}
	ELX_DISC_UNLOCK(phba, iflag);

	/* FIND node did <did> NOT FOUND */
	elx_printf_log(phba->brd_no, &elx_msgBlk0932,	/* ptr to msg structure */
		       elx_mes0932,	/* ptr to msg */
		       elx_msgBlk0932.msgPreambleStr,	/* begin varargs */
		       did, order);	/* end varargs */

	/* no match found */
	return ((LPFC_NODELIST_t *) 0);
}

/* Build a list of nodes to discover based on the loopmap */
void
lpfc_disc_list_loopmap(elxHBA_t * phba)
{
	LPFCHBA_t *plhba;
	LPFC_NODELIST_t *ndlp;
	elxCfgParam_t *clp;
	int j;
	uint32_t alpa, index;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	clp = &phba->config[0];

	if (phba->hba_state <= ELX_LINK_DOWN) {
		return;
	}
	if (plhba->fc_topology != TOPOLOGY_LOOP) {
		return;
	}

	/* Check for loop map present or not */
	if (plhba->alpa_map[0]) {
		for (j = 1; j <= plhba->alpa_map[0]; j++) {
			alpa = plhba->alpa_map[j];

			if (((plhba->fc_myDID & 0xff) == alpa) || (alpa == 0)) {
				continue;
			}
			if ((ndlp = lpfc_findnode_did(phba,
						      (NLP_SEARCH_MAPPED |
						       NLP_SEARCH_UNMAPPED |
						       NLP_SEARCH_DEQUE),
						      alpa))) {
				/* Mark node for address authentication */
				lpfc_disc_state_machine(phba, ndlp, 0,
							NLP_EVT_DEVICE_ADD);
				continue;
			}
			/* Skip if the node is already in the plogi / adisc list */
			if ((ndlp = lpfc_findnode_did(phba,
						      (NLP_SEARCH_PLOGI |
						       NLP_SEARCH_ADISC),
						      alpa))) {
				continue;
			}
			/* Cannot find existing Fabric ndlp, so allocate a new one */
			if ((ndlp =
			     (LPFC_NODELIST_t *) elx_mem_get(phba,
							     MEM_NLP)) == 0) {
				continue;
			}
			memset((void *)ndlp, 0, sizeof (LPFC_NODELIST_t));
			ndlp->nlp_state = NLP_STE_UNUSED_NODE;
			ndlp->nlp_DID = alpa;
			/* Mark node for address discovery */
			lpfc_disc_state_machine(phba, ndlp, 0,
						NLP_EVT_DEVICE_ADD);
		}
	} else {
		/* No alpamap, so try all alpa's */
		for (j = 0; j < FC_MAXLOOP; j++) {
			if (clp[LPFC_CFG_SCAN_DOWN].a_current)
				index = FC_MAXLOOP - j - 1;
			else
				index = j;
			alpa = lpfcAlpaArray[index];
			if ((plhba->fc_myDID & 0xff) == alpa) {
				continue;
			}

			if ((ndlp = lpfc_findnode_did(phba,
						      (NLP_SEARCH_MAPPED |
						       NLP_SEARCH_UNMAPPED |
						       NLP_SEARCH_DEQUE),
						      alpa))) {
				/* Mark node for address authentication */
				lpfc_disc_state_machine(phba, ndlp, 0,
							NLP_EVT_DEVICE_ADD);
				continue;
			}
			/* Skip if the node is already in the plogi / adisc list */
			if ((ndlp = lpfc_findnode_did(phba,
						      (NLP_SEARCH_PLOGI |
						       NLP_SEARCH_ADISC),
						      alpa))) {
				continue;
			}
			/* Cannot find existing ndlp, so allocate a new one */
			if ((ndlp =
			     (LPFC_NODELIST_t *) elx_mem_get(phba,
							     MEM_NLP)) == 0) {
				continue;
			}
			memset((void *)ndlp, 0, sizeof (LPFC_NODELIST_t));
			ndlp->nlp_state = NLP_STE_UNUSED_NODE;
			ndlp->nlp_DID = alpa;
			/* Mark node for address discovery */
			lpfc_disc_state_machine(phba, ndlp, 0,
						NLP_EVT_DEVICE_ADD);
		}
	}
	return;
}

/* Start Link up / RSCN discovery on ADISC or PLOGI lists */
void
lpfc_disc_start(elxHBA_t * phba)
{
	LPFCHBA_t *plhba;
	ELX_SLI_t *psli;
	ELX_MBOXQ_t *mbox;
	LPFC_NODELIST_t *ndlp;
	LPFC_NODELIST_t *new_ndlp;
	uint32_t did_changed;
	unsigned long iflag;
	elxCfgParam_t *clp;
	uint32_t clear_la_pending;

	clp = &phba->config[0];
	psli = &phba->sli;
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	if (phba->hba_state <= ELX_LINK_DOWN) {
		return;
	}
	if (phba->hba_state == ELX_CLEAR_LA)
		clear_la_pending = 1;
	else
		clear_la_pending = 0;

	if (phba->hba_state < ELX_HBA_READY) {
		phba->hba_state = ELX_DISC_AUTH;
	}
	lpfc_set_disctmo(phba);

	if (plhba->fc_prevDID == plhba->fc_myDID) {
		did_changed = 0;
	} else {
		did_changed = 1;
	}
	plhba->fc_prevDID = plhba->fc_myDID;

	/* Start Discovery state <hba_state> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0202,	/* ptr to msg structure */
		       elx_mes0202,	/* ptr to msg */
		       elx_msgBlk0202.msgPreambleStr,	/* begin varargs */
		       phba->hba_state, plhba->fc_flag, plhba->fc_plogi_cnt, plhba->fc_adisc_cnt);	/* end varargs */

	/* At this point, nothing should be on the mapped list, without
	 * NODEV_TMO timer running on it, link up discovery only.
	 */
	ELX_DISC_LOCK(phba, iflag);
	if (!(plhba->fc_flag & FC_RSCN_MODE)) {
		ndlp = plhba->fc_nlpmap_start;
		while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start) {
			new_ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;

			/* If nodev timer is not running, get rid of it */
			if (!(ndlp->nlp_flag & NLP_NODEV_TMO)) {
				lpfc_set_failmask(phba, ndlp,
						  ELX_DEV_DISCONNECTED,
						  ELX_SET_BITMASK);
				ELX_DISC_UNLOCK(phba, iflag);
				lpfc_freenode(phba, ndlp);
				elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
				ELX_DISC_LOCK(phba, iflag);
			}
			ndlp = new_ndlp;
		}

	}
	ELX_DISC_UNLOCK(phba, iflag);

	/* First do ADISC for authenrication */
	if (plhba->fc_adisc_cnt) {
		ndlp = plhba->fc_adisc_start;
		if (did_changed == 0) {
			plhba->num_disc_nodes = 0;
			/* go thru ADISC list and issue ELS ADISCs */
			while (ndlp !=
			       (LPFC_NODELIST_t *) & plhba->fc_adisc_start) {
				lpfc_issue_els_adisc(phba, ndlp, 0);
				ndlp->nlp_flag |= NLP_DISC_NODE;
				plhba->num_disc_nodes++;
				if (plhba->num_disc_nodes >=
				    clp[LPFC_CFG_DISC_THREADS].a_current) {
					if (plhba->fc_adisc_cnt >
					    plhba->num_disc_nodes)
						plhba->fc_flag |= FC_NLP_MORE;
					break;
				}
				ndlp =
				    (LPFC_NODELIST_t *) ndlp->nle.
				    nlp_listp_next;
			}
			return;
		}
		/* Move these to PLOGI list instead */
		while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_adisc_start) {
			new_ndlp = ndlp;
			ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
			lpfc_nlp_plogi(phba, new_ndlp);
		}
	}

	if ((phba->hba_state < ELX_HBA_READY) && (!clear_la_pending)) {
		/* If we get here, there is nothing to ADISC */
		if ((mbox =
		     (ELX_MBOXQ_t *) elx_mem_get(phba, MEM_MBOX | MEM_PRI))) {
			phba->hba_state = ELX_CLEAR_LA;
			lpfc_clear_la(phba, mbox);
			mbox->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
			if (elx_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				elx_mem_put(phba, MEM_MBOX, (uint8_t *) mbox);
				lpfc_disc_flush_list(phba);
				psli->ring[(psli->ip_ring)].flag &=
				    ~ELX_STOP_IOCB_EVENT;
				psli->ring[(psli->fcp_ring)].flag &=
				    ~ELX_STOP_IOCB_EVENT;
				psli->ring[(psli->next_ring)].flag &=
				    ~ELX_STOP_IOCB_EVENT;
				phba->hba_state = ELX_HBA_READY;
			}
		}
	} else {
		/* go thru PLOGI list and issue ELS PLOGIs */
		plhba->num_disc_nodes = 0;
		if (plhba->fc_plogi_cnt) {
			ndlp = plhba->fc_plogi_start;
			while (ndlp !=
			       (LPFC_NODELIST_t *) & plhba->fc_plogi_start) {
				if (ndlp->nlp_state == NLP_STE_UNUSED_NODE) {
					ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
					lpfc_issue_els_plogi(phba, ndlp, 0);
					ndlp->nlp_flag |= NLP_DISC_NODE;
					plhba->num_disc_nodes++;
					if (plhba->num_disc_nodes >=
					    clp[LPFC_CFG_DISC_THREADS].
					    a_current) {
						if (plhba->fc_plogi_cnt >
						    plhba->num_disc_nodes)
							plhba->fc_flag |=
							    FC_NLP_MORE;
						break;
					}
				}
				ndlp =
				    (LPFC_NODELIST_t *) ndlp->nle.
				    nlp_listp_next;
			}
		} else {
			if (plhba->fc_flag & FC_RSCN_MODE) {
				/* Check to see if more RSCNs came in while we were
				 * processing this one.
				 */
				if ((plhba->fc_rscn_id_cnt == 0) &&
				    (!(plhba->fc_flag & FC_RSCN_DISCOVERY))) {
					lpfc_els_flush_rscn(phba);
				} else {
					lpfc_els_handle_rscn(phba);
				}
			}
		}
	}
	return;
}

void
lpfc_disc_flush_list(elxHBA_t * phba)
{
	LPFCHBA_t *plhba;
	LPFC_NODELIST_t *ndlp;
	uint8_t *ptr;
	unsigned long iflag;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	ELX_DISC_LOCK(phba, iflag);
	if (plhba->fc_plogi_cnt) {
		ndlp = plhba->fc_plogi_start;
		while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_plogi_start) {
			ptr = (uint8_t *) ndlp;
			lpfc_set_failmask(phba, ndlp, ELX_DEV_DISCONNECTED,
					  ELX_SET_BITMASK);
			ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
			lpfc_free_tx(phba, (LPFC_NODELIST_t *) ptr);
			lpfc_freenode(phba, (LPFC_NODELIST_t *) ptr);
			elx_mem_put(phba, MEM_NLP, ptr);
		}
		plhba->fc_plogi_start =
		    (LPFC_NODELIST_t *) & plhba->fc_plogi_start;
		plhba->fc_plogi_end =
		    (LPFC_NODELIST_t *) & plhba->fc_plogi_start;
	}
	if (plhba->fc_adisc_cnt) {
		ndlp = plhba->fc_adisc_start;
		while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_adisc_start) {
			ptr = (uint8_t *) ndlp;
			lpfc_set_failmask(phba, ndlp, ELX_DEV_DISCONNECTED,
					  ELX_SET_BITMASK);
			ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
			lpfc_free_tx(phba, (LPFC_NODELIST_t *) ptr);
			lpfc_freenode(phba, (LPFC_NODELIST_t *) ptr);
			elx_mem_put(phba, MEM_NLP, ptr);
		}
		plhba->fc_adisc_start =
		    (LPFC_NODELIST_t *) & plhba->fc_adisc_start;
		plhba->fc_adisc_end =
		    (LPFC_NODELIST_t *) & plhba->fc_adisc_start;
	}
	ELX_DISC_UNLOCK(phba, iflag);
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
lpfc_disc_timeout(elxHBA_t * phba, void *l1, void *l2)
{
	LPFCHBA_t *plhba;
	ELX_SLI_t *psli;
	LPFC_NODELIST_t *ndlp;
	ELX_MBOXQ_t *mbox;
	elxCfgParam_t *clp;

	if (!phba) {
		return;
	}
	clp = &phba->config[0];
	psli = &phba->sli;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	plhba->fc_disctmo = 0;	/* timer expired */

	/* hba_state is identically ELX_LOCAL_CFG_LINK while waiting for FAN */
	if (phba->hba_state == ELX_LOCAL_CFG_LINK) {
		/* FAN timeout */
		elx_printf_log(phba->brd_no, &elx_msgBlk0221,	/* ptr to msg structure */
			       elx_mes0221,	/* ptr to msg */
			       elx_msgBlk0221.msgPreambleStr);	/* begin & end varargs */

		/* Forget about FAN, Start discovery by sending a FLOGI
		 * hba_state is identically ELX_FLOGI while waiting for FLOGI cmpl
		 */
		phba->hba_state = ELX_FLOGI;
		lpfc_set_disctmo(phba);
		lpfc_initial_flogi(phba);
		goto out;
	}

	/* hba_state is identically ELX_FLOGI while waiting for FLOGI cmpl */
	if (phba->hba_state == ELX_FLOGI) {
		/* Initial FLOGI timeout */
		elx_printf_log(phba->brd_no, &elx_msgBlk0222,	/* ptr to msg structure */
			       elx_mes0222,	/* ptr to msg */
			       elx_msgBlk0222.msgPreambleStr);	/* begin & end varargs */

		/* Assume no Fabric and go on with discovery.
		 * Check for outstanding ELS FLOGI to abort.
		 */

		/* FLOGI failed, so just use loop map to make discovery list */
		lpfc_disc_list_loopmap(phba);

		/* Start discovery */
		lpfc_disc_start(phba);
		goto out;
	}

	/* hba_state is identically ELX_FABRIC_CFG_LINK while waiting for NameServer login */
	if (phba->hba_state == ELX_FABRIC_CFG_LINK) {
		/* Timeout while waiting for NameServer login */
		elx_printf_log(phba->brd_no, &elx_msgBlk0223,	/* ptr to msg structure */
			       elx_mes0223,	/* ptr to msg */
			       elx_msgBlk0223.msgPreambleStr);	/* begin & end varargs */

		/* Next look for NameServer ndlp */
		if ((ndlp =
		     lpfc_findnode_did(phba,
				       (NLP_SEARCH_ALL | NLP_SEARCH_DEQUE),
				       NameServer_DID))) {
			lpfc_freenode(phba, ndlp);
			elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
		}
		/* Start discovery */
		lpfc_disc_start(phba);
		goto out;
	}

	/* Check for wait for NameServer Rsp timeout */
	if (phba->hba_state == ELX_NS_QRY) {
		/* NameServer Query timeout */
		elx_printf_log(phba->brd_no, &elx_msgBlk0224,	/* ptr to msg structure */
			       elx_mes0224,	/* ptr to msg */
			       elx_msgBlk0224.msgPreambleStr,	/* begin varargs */
			       plhba->fc_ns_retry, LPFC_MAX_NS_RETRY);	/* end varargs */

		if ((ndlp =
		     lpfc_findnode_did(phba, NLP_SEARCH_UNMAPPED,
				       NameServer_DID))) {
			if (plhba->fc_ns_retry < LPFC_MAX_NS_RETRY) {
				/* Try it one more time */
				if (lpfc_ns_cmd(phba, ndlp, SLI_CTNS_GID_FT) ==
				    0) {
					goto out;
				}
			}
			plhba->fc_ns_retry = 0;
		}

		/* Nothing to authenticate, so CLEAR_LA right now */
		if (phba->hba_state != ELX_CLEAR_LA) {
			if ((mbox =
			     (ELX_MBOXQ_t *) elx_mem_get(phba,
							 MEM_MBOX | MEM_PRI))) {
				phba->hba_state = ELX_CLEAR_LA;
				lpfc_clear_la(phba, mbox);
				mbox->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
				if (elx_sli_issue_mbox
				    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
				    == MBX_NOT_FINISHED) {
					elx_mem_put(phba, MEM_MBOX,
						    (uint8_t *) mbox);
					goto clrlaerr;
				}
			} else {
				/* Device Discovery completion error */
				elx_printf_log(phba->brd_no, &elx_msgBlk0226,	/* ptr to msg structure */
					       elx_mes0226,	/* ptr to msg */
					       elx_msgBlk0226.msgPreambleStr);	/* begin & end varargs */
				phba->hba_state = ELX_HBA_ERROR;
			}
		}
		if ((mbox =
		     (ELX_MBOXQ_t *) elx_mem_get(phba, MEM_MBOX | MEM_PRI))) {
			/* Setup and issue mailbox INITIALIZE LINK command */
			lpfc_linkdown(phba);
			lpfc_init_link(phba, mbox,
				       clp[LPFC_CFG_TOPOLOGY].a_current,
				       clp[LPFC_CFG_LINK_SPEED].a_current);
			mbox->mb.un.varInitLnk.lipsr_AL_PA = 0;
			if (elx_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				elx_mem_put(phba, MEM_MBOX, (uint8_t *) mbox);
			}
		}
		goto out;
	}

	if (phba->hba_state == ELX_DISC_AUTH) {
		/* Node Authentication timeout */
		elx_printf_log(phba->brd_no, &elx_msgBlk0227,	/* ptr to msg structure */
			       elx_mes0227,	/* ptr to msg */
			       elx_msgBlk0227.msgPreambleStr);	/* begin & end varargs */
		lpfc_disc_flush_list(phba);
		if (phba->hba_state != ELX_CLEAR_LA) {
			if ((mbox =
			     (ELX_MBOXQ_t *) elx_mem_get(phba,
							 MEM_MBOX | MEM_PRI))) {
				phba->hba_state = ELX_CLEAR_LA;
				lpfc_clear_la(phba, mbox);
				mbox->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
				if (elx_sli_issue_mbox
				    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
				    == MBX_NOT_FINISHED) {
					elx_mem_put(phba, MEM_MBOX,
						    (uint8_t *) mbox);
					goto clrlaerr;
				}
			}
		}
		goto out;
	}

	if (phba->hba_state == ELX_CLEAR_LA) {
		/* CLEAR LA timeout */
		elx_printf_log(phba->brd_no, &elx_msgBlk0228,	/* ptr to msg structure */
			       elx_mes0228,	/* ptr to msg */
			       elx_msgBlk0228.msgPreambleStr);	/* begin & end varargs */
	      clrlaerr:
		lpfc_disc_flush_list(phba);
		psli->ring[(psli->ip_ring)].flag &= ~ELX_STOP_IOCB_EVENT;
		psli->ring[(psli->fcp_ring)].flag &= ~ELX_STOP_IOCB_EVENT;
		psli->ring[(psli->next_ring)].flag &= ~ELX_STOP_IOCB_EVENT;
		phba->hba_state = ELX_HBA_READY;
		goto out;
	}

	if ((phba->hba_state == ELX_HBA_READY) &&
	    (plhba->fc_flag & FC_RSCN_MODE)) {
		/* RSCN timeout */
		elx_printf_log(phba->brd_no, &elx_msgBlk0231,	/* ptr to msg structure */
			       elx_mes0231,	/* ptr to msg */
			       elx_msgBlk0231.msgPreambleStr,	/* begin varargs */
			       plhba->fc_ns_retry, LPFC_MAX_NS_RETRY);	/* end varargs */

		/* Cleanup any outstanding ELS commands */
		lpfc_els_flush_cmd(phba);

		lpfc_els_flush_rscn(phba);
		lpfc_disc_flush_list(phba);
		goto out;
	}

      out:
	return;
}

/*****************************************************************************/
/*
 * NAME:     lpfc_linkdown_timeout
 *
 * FUNCTION: Fibre Channel driver linkdown timeout routine.
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
lpfc_linkdown_timeout(elxHBA_t * phba, void *l1, void *l2)
{
	LPFCHBA_t *plhba;
	LPFC_NODELIST_t *ndlp;
	LPFC_NODELIST_t *new_ndlp;
	ELXSCSITARGET_t *targetp;
	elxCfgParam_t *clp;
	uint32_t tgt;

	if (!phba) {
		return;
	}

	clp = &phba->config[0];
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	/* Link Down timeout */
	elx_printf_log(phba->brd_no, &elx_msgBlk1306,	/* ptr to msg structure */
		       elx_mes1306,	/* ptr to msg */
		       elx_msgBlk1306.msgPreambleStr,	/* begin varargs */
		       phba->hba_state, plhba->fc_flag, plhba->fc_ns_retry);	/* end varargs */

	plhba->fc_linkdown = 0;	/* timer expired */
	plhba->fc_flag |= (FC_LD_TIMER | FC_LD_TIMEOUT);	/* indicate timeout */
	phba->hba_flag &= ~FC_LFR_ACTIVE;

	/* Issue a DEVICE REMOVE event to all nodes */
	ndlp = plhba->fc_plogi_start;
	if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_plogi_start)
		ndlp = plhba->fc_adisc_start;
	if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_adisc_start)
		ndlp = plhba->fc_nlpunmap_start;
	if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start)
		ndlp = plhba->fc_nlpmap_start;
	while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start) {
		new_ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;

		/* Fabric nodes are not handled thru state machine for link down tmo */
		if (!(ndlp->nle.nlp_type & NLP_FABRIC)) {
			lpfc_disc_state_machine(phba, ndlp, (void *)0,
						NLP_EVT_DEVICE_RM);
		}

		ndlp = new_ndlp;
		if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_plogi_start)
			ndlp = plhba->fc_adisc_start;
		if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_adisc_start)
			ndlp = plhba->fc_nlpunmap_start;
		if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start)
			ndlp = plhba->fc_nlpmap_start;
	}
	if ((clp[ELX_CFG_NODEV_TMO].a_current == 0) &&
	    (clp[ELX_CFG_HOLDIO].a_current == 0)) {

		for (tgt = 0; tgt < MAX_FCP_TARGET; tgt++) {
			targetp = plhba->device_queue_hash[tgt];
			if ((targetp) && (targetp->targetFlags & FC_NPR_ACTIVE)) {
				targetp->targetFlags &= ~FC_NPR_ACTIVE;
				elx_sched_flush_target(phba, targetp,
						       IOSTAT_DRIVER_REJECT,
						       IOERR_SLI_ABORTED);
				if (targetp->tmofunc) {
					elx_clk_can(phba, targetp->tmofunc);
					targetp->tmofunc = 0;
				}
			}
		}

		/* Just to be sure */
		elx_sched_flush_hba(phba, IOSTAT_DRIVER_REJECT,
				    IOERR_SLI_ABORTED);
	}

	return;
}

void
lpfc_nodev_timeout(elxHBA_t * phba, void *l1, void *l2)
{
	LPFC_NODELIST_t *ndlp;
	ELXSCSITARGET_t *targetp;

	ndlp = (LPFC_NODELIST_t *) l1;

	/* Nodev timeout on NPort <nlp_DID> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0203,	/* ptr to msg structure */
		       elx_mes0203,	/* ptr to msg */
		       elx_msgBlk0203.msgPreambleStr,	/* begin varargs */
		       ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state, ndlp->nle.nlp_rpi);	/* end varargs */

	ndlp->nlp_flag &= ~(NLP_NODEV_TMO | NLP_DELAY_TMO);
	ndlp->nlp_tmofunc = 0;

	ndlp->nle.nlp_rflag &= ~NLP_NPR_ACTIVE;
	targetp = ndlp->nlp_Target;
	if (targetp) {
		targetp->targetFlags &= ~FC_NPR_ACTIVE;
		elx_sched_flush_target(phba, targetp, IOSTAT_DRIVER_REJECT,
				       IOERR_SLI_ABORTED);
		if (targetp->tmofunc) {
			elx_clk_can(phba, targetp->tmofunc);
			targetp->tmofunc = 0;
		}
	}
	lpfc_disc_state_machine(phba, ndlp, (void *)0, NLP_EVT_DEVICE_RM);
	return;
}

ELXSCSITARGET_t *
lpfc_find_target(elxHBA_t * phba, uint32_t tgt)
{
	LPFCHBA_t *plhba;
	ELXSCSITARGET_t *targetp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	targetp = plhba->device_queue_hash[tgt];
	return (targetp);
}

/*****************************************************************************/
/*
 * NAME:     lpfc_find_lun
 *
 * FUNCTION: Fibre Channel bus/target/LUN to ELXSCSILUN_t lookup
 *
 * EXECUTION ENVIRONMENT: 
 *
 * RETURNS:  
 *      ptr to desired ELXSCSILUN_t
 */
/*****************************************************************************/
ELXSCSILUN_t *
lpfc_find_lun(elxHBA_t * phba, uint32_t tgt, uint64_t lun, int create_flag)
{
	LPFCHBA_t *plhba;
	LPFC_NODELIST_t *nlp;
	LPFC_BINDLIST_t *blp;
	ELXSCSITARGET_t *targetp;
	ELXSCSILUN_t *lunp;
	ELXSCSILUN_t *lastlunp;
	elxCfgParam_t *clp;
	MBUF_INFO_t *buf_info;
	MBUF_INFO_t bufinfo;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	clp = &phba->config[0];
	targetp = plhba->device_queue_hash[tgt];

	/* First see if the SCSI ID has an allocated ELXSCSITARGET_t */
	if (targetp) {
		lunp = (ELXSCSILUN_t *) targetp->lunlist.q_first;
		while (lunp) {
			/* Finally see if the LUN ID has an allocated ELXSCSILUN_t */
			if (lunp->lun_id == lun) {
				return (lunp);
			}
			lunp = lunp->pnextLun;
		}
		if (create_flag) {
			if (lun < targetp->max_lun) {
				buf_info = &bufinfo;
				goto lun_create;
			}
		}
	} else {
		if (create_flag) {
			nlp = lpfc_findnode_scsiid(phba, tgt);
			if (nlp == 0) {
				return 0;
			}

			buf_info = &bufinfo;
			memset(buf_info, 0, sizeof (MBUF_INFO_t));
			buf_info->size = sizeof (ELXSCSITARGET_t);
			buf_info->flags = ELX_MBUF_VIRT;
			buf_info->align = sizeof (void *);
			buf_info->dma_handle = 0;

			elx_malloc(phba, buf_info);
			if (buf_info->virt == 0) {
				return (0);
			}

			targetp = (ELXSCSITARGET_t *) buf_info->virt;
			memset(targetp, 0, sizeof (ELXSCSITARGET_t));
			targetp->scsi_id = tgt;
			if ((targetp->max_lun =
			     clp[ELX_CFG_MAX_LUN].a_current) == 0) {
				targetp->max_lun = 255;
			}
			targetp->pHba = phba;
			plhba->device_queue_hash[tgt] = targetp;
			targetp->pcontext = (void *)nlp;
			if (nlp) {

				/* Create SCSI Target <tgt> */
				elx_printf_log(phba->brd_no, &elx_msgBlk0204,	/* ptr to msg structure */
					       elx_mes0204,	/* ptr to msg */
					       elx_msgBlk0204.msgPreambleStr,	/* begin varargs */
					       tgt);	/* end varargs */

				nlp->nlp_Target = targetp;
				if ((blp = nlp->nlp_listp_bind)) {
					blp->nlp_Target = targetp;
				}
			}
			if (clp[ELX_CFG_DFT_TGT_Q_DEPTH].a_current) {
				elx_sched_target_init(targetp,
						      (uint16_t)
						      clp
						      [ELX_CFG_DFT_TGT_Q_DEPTH].
						      a_current);
			} else {
				elx_sched_target_init(targetp,
						      (uint16_t)
						      clp
						      [ELX_CFG_DFT_HBA_Q_DEPTH].
						      a_current);
			}
		      lun_create:
			memset(buf_info, 0, sizeof (MBUF_INFO_t));
			buf_info->size = sizeof (ELXSCSILUN_t);
			buf_info->flags = ELX_MBUF_VIRT;
			buf_info->align = sizeof (void *);
			buf_info->dma_handle = 0;

			elx_malloc(phba, buf_info);
			if (buf_info->virt == 0) {
				return (0);
			}

			/* Create SCSI LUN <lun> on Target <tgt> */
			elx_printf_log(phba->brd_no, &elx_msgBlk0205,	/* ptr to msg structure */
				       elx_mes0205,	/* ptr to msg */
				       elx_msgBlk0205.msgPreambleStr,	/* begin varargs */
				       (uint32_t) lun, tgt);	/* end varargs */

			lunp = (ELXSCSILUN_t *) buf_info->virt;
			memset(lunp, 0, sizeof (ELXSCSILUN_t));
			lunp->lun_id = lun;
			lunp->qfull_retries = lpfc_qfull_retry_count;	/* For Schedular to retry */
			lunp->pTarget = targetp;
			lunp->pHBA = phba;

			lastlunp = (ELXSCSILUN_t *) targetp->lunlist.q_last;
			if (lastlunp) {
				lastlunp->pnextLun = lunp;
			} else {
				targetp->lunlist.q_first = (ELX_SLINK_t *) lunp;
			}
			lunp->pnextLun = 0;
			targetp->lunlist.q_last = (ELX_SLINK_t *) lunp;
			targetp->lunlist.q_cnt++;
			elx_sched_lun_init(lunp,
					   (uint16_t)
					   clp[ELX_CFG_DFT_LUN_Q_DEPTH].
					   a_current);
			return (lunp);
		}
	}
	return (0);
}

void
lpfc_disc_cmpl_rptlun(elxHBA_t * phba,
		      ELX_IOCBQ_t * cmdiocb, ELX_IOCBQ_t * rspiocb)
{
	DMABUF_t *mp;
	ELX_SCSI_BUF_t *elx_cmd;
	ELXSCSITARGET_t *targetp;
	ELXSCSILUN_t *lunp;
	LPFC_NODELIST_t *ndlp;
	elxCfgParam_t *clp;
	FCP_RSP *fcprsp;
	IOCB_t *iocb;
	uint8_t *datap;
	uint32_t *datap32;
	uint32_t rptLunLen;
	uint32_t max, lun, i;

	elx_cmd = cmdiocb->context1;
	mp = cmdiocb->context2;
	targetp = elx_cmd->pLun->pTarget;
	ndlp = (LPFC_NODELIST_t *) targetp->pcontext;
	iocb = &elx_cmd->cur_iocbq.iocb;
	fcprsp = elx_cmd->fcp_rsp;
	clp = &phba->config[0];

	if (ndlp == 0) {
		targetp->rptLunState = REPORT_LUN_ERRORED;
		targetp->targetFlags &= ~(FC_NPR_ACTIVE | FC_RETRY_RPTLUN);
		if (targetp->tmofunc) {
			elx_clk_can(phba, targetp->tmofunc);
			targetp->tmofunc = 0;
		}
		elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
		elx_free_scsi_buf(elx_cmd);
		return;
	}

	/* Report Lun completes on NPort <nlp_DID> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0206,	/* ptr to msg structure */
		       elx_mes0206,	/* ptr to msg */
		       elx_msgBlk0206.msgPreambleStr,	/* begin varargs */
		       ndlp->nlp_DID, iocb->ulpStatus, fcprsp->rspStatus2, fcprsp->rspStatus3, ndlp->nle.nlp_failMask);	/* end varargs */

	if (targetp) {

		if (clp[ELX_CFG_MAX_LUN].a_current) {
			targetp->max_lun = clp[ELX_CFG_MAX_LUN].a_current;
		} else {
			targetp->max_lun = 255;
		}

		if (((iocb->ulpStatus == IOSTAT_SUCCESS) &&
		     (fcprsp->rspStatus3 == SCSI_STAT_GOOD)) ||
		    ((iocb->ulpStatus == IOSTAT_FCP_RSP_ERROR) &&
		     (fcprsp->rspStatus2 & RESID_UNDER) &&
		     (fcprsp->rspStatus3 == SCSI_STAT_GOOD))) {

			datap = (uint8_t *) mp->virt;
			/*
			 * if Lun0 uses VSA, we assume others use too.
			 */
			if ((datap[8] & 0xc0) == 0x40) {
				targetp->addrMode = VOLUME_SET_ADDRESSING;
			}

			i = 0;
			datap32 = (uint32_t *) mp->virt;
			rptLunLen = *datap32;
			rptLunLen = SWAP_DATA(rptLunLen);
			/* search for the max lun */
			max = 0;
			for (i = 0; ((i < rptLunLen) && (i < 8 * 128)); i += 8) {
				datap32 += 2;
				lun = (((*datap32) >> FC_LUN_SHIFT) & 0xff);
				if (lun > max)
					max = lun;
			}
			if (i) {
				targetp->max_lun = max + 1;
			}

			targetp->rptLunState = REPORT_LUN_COMPLETE;
			ndlp->nle.nlp_rflag &= ~NLP_NPR_ACTIVE;
			targetp->targetFlags &=
			    ~(FC_NPR_ACTIVE | FC_RETRY_RPTLUN);
			if (targetp->tmofunc) {
				elx_clk_can(phba, targetp->tmofunc);
				targetp->tmofunc = 0;
			}

			/* The lpfc_issue_rptlun function does not re-use the buffer pointed to
			 * by targetp->RptLunData.  It always allocates
			 * a new one and frees the old buffer. 
			 */
			if (targetp->RptLunData) {
				elx_mem_put(phba, MEM_BUF,
					    (uint8_t *) targetp->RptLunData);
			}
			targetp->RptLunData = mp;

			lpfc_set_failmask(phba, ndlp, ELX_DEV_RPTLUN,
					  ELX_CLR_BITMASK);
			lunp = (ELXSCSILUN_t *) targetp->lunlist.q_first;
			while (lunp) {
				lunp->pnode = (ELX_NODELIST_t *) ndlp;
				lunp = lunp->pnextLun;
			}
		} else {
			/* Retry RPTLUN */
			if (ndlp
			    && (!(ndlp->nle.nlp_failMask & ELX_DEV_FATAL_ERROR))
			    && (!(targetp->targetFlags & FC_RETRY_RPTLUN))) {
				targetp->targetFlags |= FC_RETRY_RPTLUN;
				targetp->rptlunfunc =
				    elx_clk_set(phba, 1, lpfc_disc_retry_rptlun,
						(void *)targetp, (void *)0);
			} else {
				targetp->rptLunState = REPORT_LUN_ERRORED;

				/* If ReportLun failed, then we allow only lun 0 on this target.
				 * This way, the driver won't create Processor devices when
				 * JBOD failed ReportLun and lun-skip is turned ON.
				 */
				targetp->max_lun = 1;

				ndlp->nle.nlp_rflag &= ~NLP_NPR_ACTIVE;
				targetp->targetFlags &=
				    ~(FC_NPR_ACTIVE | FC_RETRY_RPTLUN);
				if (targetp->tmofunc) {
					elx_clk_can(phba, targetp->tmofunc);
					targetp->tmofunc = 0;
				}
				lpfc_set_failmask(phba, ndlp, ELX_DEV_RPTLUN,
						  ELX_CLR_BITMASK);
				lunp =
				    (ELXSCSILUN_t *) targetp->lunlist.q_first;
				while (lunp) {
					lunp->pnode = (ELX_NODELIST_t *) ndlp;
					lunp = lunp->pnextLun;
				}
			}
		}
	}

	/* We cannot free RptLunData buffer if we already save it in 
	 * the target structure */
	if (mp != targetp->RptLunData) {
		elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
	}
	elx_free_scsi_buf(elx_cmd);
	return;
}

/*****************************************************************************/
/*
 * NAME:     lpfc_disc_retry_rptlun
 *
 * FUNCTION: Try to send report lun again.  Note that NODELIST could have
 *           changed from the last failed repotlun cmd.  That's why we have
 *           to get the latest ndlp before calling lpfc_disc_issue_rptlun. 
 *
 * EXECUTION ENVIRONMENT: 
 *           During device discovery
 *
 */
/*****************************************************************************/
void
lpfc_disc_retry_rptlun(elxHBA_t * phba, void *l1, void *l2)
{
	ELXSCSITARGET_t *targetp;
	LPFC_NODELIST_t *ndlp;

	targetp = (ELXSCSITARGET_t *) l1;
	ndlp = (LPFC_NODELIST_t *) targetp->pcontext;
	if (ndlp) {
		lpfc_disc_issue_rptlun(phba, ndlp);
	}
}

/*****************************************************************************/
/*
 * NAME:     lpfc_disc_issue_rptlun
 *
 * FUNCTION: Issue a RPTLUN SCSI command to a newly mapped FCP device
 *           to determine LUN addressing mode
 *
 * EXECUTION ENVIRONMENT: 
 *           During device discovery
 *
 */
/*****************************************************************************/
int
lpfc_disc_issue_rptlun(elxHBA_t * phba, LPFC_NODELIST_t * nlp)
{
	ELX_SLI_t *psli;
	ELX_SCSI_BUF_t *elx_cmd;
	ELX_IOCBQ_t *piocbq;

	/* Issue Report LUN on NPort <nlp_DID> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0207,	/* ptr to msg structure */
		       elx_mes0207,	/* ptr to msg */
		       elx_msgBlk0207.msgPreambleStr,	/* begin varargs */
		       nlp->nlp_DID, nlp->nle.nlp_failMask, nlp->nlp_state, nlp->nle.nlp_rpi);	/* end varargs */

	psli = &phba->sli;
	elx_cmd = lpfc_build_scsi_cmd(phba, nlp, FCP_SCSI_REPORT_LUNS, 0);
	if (elx_cmd) {
		piocbq = &elx_cmd->cur_iocbq;
		piocbq->iocb_cmpl = lpfc_disc_cmpl_rptlun;

		if (elx_sli_issue_iocb(phba, &psli->ring[psli->fcp_ring],
				       piocbq,
				       SLI_IOCB_USE_TXQ) == IOCB_ERROR) {
			elx_mem_put(phba, MEM_BUF,
				    (uint8_t *) piocbq->context2);
			elx_free_scsi_buf(elx_cmd);
			return (1);
		}
		if (elx_cmd->pLun->pTarget) {
			elx_cmd->pLun->pTarget->rptLunState =
			    REPORT_LUN_ONGOING;
		}
	}
	return (0);
}

/*
 *   lpfc_set_failmask
 *   Set, or clear, failMask bits in LPFC_NODELIST_t
 */
void
lpfc_set_failmask(elxHBA_t * phba,
		  LPFC_NODELIST_t * ndlp, uint32_t bitmask, uint32_t flag)
{
	ELXSCSITARGET_t *targetp;
	ELXSCSILUN_t *lunp;
	uint32_t oldmask;
	uint32_t changed;

	/* Failmask change on NPort <nlp_DID> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0208,	/* ptr to msg structure */
		       elx_mes0208,	/* ptr to msg */
		       elx_msgBlk0208.msgPreambleStr,	/* begin varargs */
		       ndlp->nlp_DID, ndlp->nle.nlp_failMask, bitmask, flag);	/* end varargs */

	targetp = ndlp->nlp_Target;
	if (flag == ELX_SET_BITMASK) {
		oldmask = ndlp->nle.nlp_failMask;
		/* Set failMask event */
		ndlp->nle.nlp_failMask |= bitmask;
		if (oldmask != ndlp->nle.nlp_failMask) {
			changed = 1;
		} else {
			changed = 0;
		}

		if (oldmask == 0) {
			/* Pause the scheduler if this is a FCP node */
			if (targetp) {
				elx_sched_pause_target(targetp);
			}
		}
	} else {
		/* Clear failMask event */
		ndlp->nle.nlp_failMask &= ~bitmask;
		changed = 1;
	}

	/* If mask has changed, there may be more to do */
	if (changed) {
		/* If the map was / is a mapped target, probagate change to 
		 * all ELXSCSILUN_t's
		 */
		if (targetp) {
			lunp = (ELXSCSILUN_t *) targetp->lunlist.q_first;
			while (lunp) {
				if (flag == ELX_SET_BITMASK) {
					/* Set failMask event */
					lunp->failMask |= bitmask;
				} else {
					/* Clear failMask event */
					lunp->failMask &= ~bitmask;
				}
				lunp = lunp->pnextLun;
			}

			/* If the failMask changes to 0, resume the scheduler */
			if (ndlp->nle.nlp_failMask == 0) {
				elx_sched_continue_target(targetp);
			}
		}
	}

	/* Since its fatal, now we can clear pan and sid */
	if ((flag == ELX_SET_BITMASK) && (bitmask & ELX_DEV_FATAL_ERROR)) {
		ndlp->nlp_pan = 0;
		ndlp->nlp_sid = 0;
	}
	return;
}

/*
 *  Ignore completion for all IOCBs on tx and txcmpl queue for ELS 
 *  ring the match the sppecified nodelist.
 */
void
lpfc_free_tx(elxHBA_t * phba, LPFC_NODELIST_t * ndlp)
{
	ELX_SLI_t *psli;
	ELX_IOCBQ_t *iocb, *next_iocb;
	IOCB_t *icmd;
	ELX_SLI_RING_t *pring;
	unsigned long iflag;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];
	ELX_SLI_LOCK(phba, iflag);

	/* Error matching iocb on txq or txcmplq 
	 * First check the txq.
	 */
	next_iocb = (ELX_IOCBQ_t *) pring->txq.q_f;
	while (next_iocb != (ELX_IOCBQ_t *) & pring->txq) {
		iocb = next_iocb;
		next_iocb = next_iocb->q_f;
		if (iocb->context1 != ndlp) {
			continue;
		}
		icmd = &iocb->iocb;
		if ((icmd->ulpCommand == CMD_ELS_REQUEST64_CR) ||
		    (icmd->ulpCommand == CMD_XMIT_ELS_RSP64_CX)) {

			elx_deque(iocb);
			pring->txq.q_cnt--;
			lpfc_els_free_iocb(phba, iocb);
		}
	}

	/* Next check the txcmplq */
	next_iocb = (ELX_IOCBQ_t *) pring->txcmplq.q_f;
	while (next_iocb != (ELX_IOCBQ_t *) & pring->txcmplq) {
		iocb = next_iocb;
		next_iocb = next_iocb->q_f;
		if (iocb->context1 != ndlp) {
			continue;
		}
		icmd = &iocb->iocb;
		if ((icmd->ulpCommand == CMD_ELS_REQUEST64_CR) ||
		    (icmd->ulpCommand == CMD_XMIT_ELS_RSP64_CX)) {

			iocb->iocb_cmpl = 0;
			/* context2  = cmd,  context2->next = rsp, context3 = bpl */
			if (iocb->context2) {
				/* Free the response IOCB before handling the command. */
				if (((DMABUF_t *) (iocb->context2))->next) {

					/* Delay before releasing rsp buffer to give UNREG mbox a 
					 * chance to take effect.
					 */
					elx_clk_set(phba, 1,
						    lpfc_put_buf,
						    (void
						     *)(((DMABUF_t *) (iocb->
								       context2))->
							next), (void *)0);
				}
				elx_mem_put(phba, MEM_BUF,
					    (uint8_t *) (iocb->context2));
			}

			if (iocb->context3) {
				elx_mem_put(phba, MEM_BPL,
					    (uint8_t *) (iocb->context3));
			}
		}
	}
	ELX_SLI_UNLOCK(phba, iflag);

	return;
}

/*****************************************************************************/
/*
 * NAME:     lpfc_put_buf
 *
 * FUNCTION: Fibre Channel driver delayed buffer release routine.
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
lpfc_put_buf(elxHBA_t * phba, void *l1, void *l2)
{
	elx_mem_put(phba, MEM_BUF, (uint8_t *) l1);
	return;
}

/*
 * This routine handles processing a NameServer REG_LOGIN mailbox
 * command upon completion. It is setup in the ELX_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_fdmi_reg_login(elxHBA_t * phba, ELX_MBOXQ_t * pmb)
{
	LPFCHBA_t *plhba;
	ELX_SLI_t *psli;
	MAILBOX_t *mb;
	DMABUF_t *mp;
	LPFC_NODELIST_t *ndlp;
	elxCfgParam_t *clp;

	clp = &phba->config[0];
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;
	mb = &pmb->mb;

	ndlp = (LPFC_NODELIST_t *) pmb->context2;
	mp = (DMABUF_t *) (pmb->context1);

	elx_pci_dma_sync((void *)phba, (void *)mp, 0, ELX_DMA_SYNC_FORCPU);
	pmb->context1 = 0;

	if (ndlp->nle.nlp_rpi != 0)
		lpfc_findnode_remove_rpi(phba, ndlp->nle.nlp_rpi);
	ndlp->nle.nlp_rpi = mb->un.varWords[0];
	lpfc_addnode_rpi(phba, ndlp, ndlp->nle.nlp_rpi);
	ndlp->nle.nlp_type |= NLP_FABRIC;
	lpfc_nlp_unmapped(phba, ndlp);
	ndlp->nlp_state = NLP_STE_PRLI_COMPL;

	/* Start issuing Fabric-Device Management Interface (FDMI)
	 * command to 0xfffffa (FDMI well known port)
	 */
	if (clp[LPFC_CFG_FDMI_ON].a_current == 1) {
		lpfc_fdmi_cmd(phba, ndlp, SLI_MGMT_DHBA);
	} else {
		/*
		 * Delay issuing FDMI command if fdmi-on=2
		 * (supporting RPA/hostnmae)
		 */
		plhba->fc_fdmitmo =
		    elx_clk_set(phba, 60, lpfc_fdmi_tmo, ndlp, 0);
	}

	elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
	elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);

	return;
}

/*
 * This routine looks up the ndlp hash 
 * table for the given RPI. If rpi found
 * it return the node list pointer
 * else return NULL.
 */
LPFC_NODELIST_t *
lpfc_findnode_rpi(elxHBA_t * phba, uint16_t rpi)
{
	LPFCHBA_t *plhba;
	LPFC_NODELIST_t *ret;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	ret = plhba->fc_nlplookup[LPFC_RPI_HASH_FUNC(rpi)];
	while ((ret != NULL) && (ret->nle.nlp_rpi != rpi)) {
		ret = ret->nlp_rpi_hash_next;
	}
	return ret;
}

/*
 * This routine looks up the ndlp hash table for the
 * given RPI. If rpi found it return the node list 
 * pointer else return NULL after deleting the entry 
 * from hash table. 
 */
LPFC_NODELIST_t *
lpfc_findnode_remove_rpi(elxHBA_t * phba, uint16_t rpi)
{
	LPFCHBA_t *plhba;
	LPFC_NODELIST_t *ret, *temp;;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	ret = plhba->fc_nlplookup[LPFC_RPI_HASH_FUNC(rpi)];

	if (ret == NULL) {
		return NULL;
	}

	if (ret->nle.nlp_rpi == rpi) {
		plhba->fc_nlplookup[LPFC_RPI_HASH_FUNC(rpi)] =
		    ret->nlp_rpi_hash_next;
		ret->nlp_rpi_hash_next = NULL;
		return ret;
	}

	while ((ret->nlp_rpi_hash_next != NULL) &&
	       (ret->nlp_rpi_hash_next->nle.nlp_rpi != rpi)) {
		ret = ret->nlp_rpi_hash_next;
	}

	if (ret->nlp_rpi_hash_next != NULL) {
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
lpfc_addnode_rpi(elxHBA_t * phba, LPFC_NODELIST_t * ndlp, uint16_t rpi)
{

	LPFCHBA_t *plhba;
	uint32_t index;

	index = LPFC_RPI_HASH_FUNC(rpi);
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	ndlp->nlp_rpi_hash_next = plhba->fc_nlplookup[index];
	plhba->fc_nlplookup[index] = ndlp;
	return;
}

/*
 * This routine deletes the node list entry from the
 * ndlp hash table.
 */
LPFC_NODELIST_t *
lpfc_removenode_rpihash(elxHBA_t * phba, LPFC_NODELIST_t * ndlp)
{
	LPFCHBA_t *plhba;
	LPFC_NODELIST_t *node_list;
	uint32_t index;

	plhba = (LPFCHBA_t *) phba->pHbaProto;

	for (index = 0; index < LPFC_RPI_HASH_SIZE; index++) {
		node_list = plhba->fc_nlplookup[index];

		if (!node_list)
			continue;
		if (node_list == ndlp) {
			plhba->fc_nlplookup[index] =
			    node_list->nlp_rpi_hash_next;
			node_list->nlp_rpi_hash_next = NULL;
			return node_list;
		}
		while ((node_list->nlp_rpi_hash_next != NULL) &&
		       (node_list->nlp_rpi_hash_next != ndlp)) {
			node_list = node_list->nlp_rpi_hash_next;
		}
		if (node_list->nlp_rpi_hash_next) {
			node_list->nlp_rpi_hash_next = ndlp->nlp_rpi_hash_next;
			ndlp->nlp_rpi_hash_next = NULL;
			return ndlp;
		}
	}
	return NULL;
}

extern elxDRVR_t elxDRVR;

int lpfc_parse_vpd(elxHBA_t *, uint8_t *);
int lpfc_post_rcv_buf(elxHBA_t *);
void lpfc_establish_link_tmo(elxHBA_t *, void *, void *);
int lpfc_check_for_vpd = 1;
int lpfc_rdrev_wd30 = 0;

#define LPFC_MAX_VPD_SIZE   0x100
uint32_t lpfc_vpd_data[LPFC_MAX_VPD_SIZE];

extern int lpfc_instance[MAX_ELX_BRDS];

/************************************************************************/
/*                                                                      */
/*   lpfc_swap_bcopy                                                    */
/*                                                                      */
/************************************************************************/
void
lpfc_swap_bcopy(uint32_t * src, uint32_t * dest, uint32_t cnt)
{
	uint32_t ldata;
	int i;

	for (i = 0; i < (int)cnt; i += sizeof (uint32_t)) {
		ldata = *src++;
		ldata = cpu_to_be32(ldata);
		*dest++ = ldata;
	}
}

/************************************************************************/
/*                                                                      */
/*    lpfc_config_port_prep                                             */
/*    This routine will do LPFC initialization prior to the             */
/*    CONFIG_PORT mailbox command. This will be initialized             */
/*    as a SLI layer callback routine.                                  */
/*    This routine returns 0 on success or ERESTART if it wants         */
/*    the SLI layer to reset the HBA and try again. Any                 */
/*    other return value indicates an error.                            */
/*                                                                      */
/************************************************************************/
int
lpfc_config_port_prep(elxHBA_t * phba)
{
	ELX_MBOXQ_t *pmb;
	MAILBOX_t *mb;
	elx_vpd_t *vp;
	uint32_t status;
	int i;
	char licensed[56] =
	    "key unlock for use with gnu public licensed code only\0";
	uint32_t *pText = (uint32_t *) licensed;
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;

	vp = &phba->vpd;

	/* Get a Mailbox buffer to setup mailbox commands for HBA initialization */
	if ((pmb =
	     (ELX_MBOXQ_t *) elx_mem_get(phba, (MEM_MBOX | MEM_PRI))) == 0) {
		phba->hba_state = ELX_HBA_ERROR;
		return (ENOMEM);
	}
	mb = &pmb->mb;

#ifndef powerpc
	if ((((phba->pci_id >> 16) & 0xffff) == PCI_DEVICE_ID_TFLY) ||
	    (((phba->pci_id >> 16) & 0xffff) == PCI_DEVICE_ID_PFLY) ||
	    (((phba->pci_id >> 16) & 0xffff) == PCI_DEVICE_ID_LP101) ||
	    (((phba->pci_id >> 16) & 0xffff) == PCI_DEVICE_ID_RFLY)) {
#else
	if (((__swab16(phba->pci_id) & 0xffff) == PCI_DEVICE_ID_TFLY) ||
	    ((__swab16(phba->pci_id) & 0xffff) == PCI_DEVICE_ID_PFLY) ||
	    ((__swab16(phba->pci_id) & 0xffff) == PCI_DEVICE_ID_RFLY)) {
#endif
		/* Setup and issue mailbox READ NVPARAMS command */
		phba->hba_state = ELX_INIT_MBX_CMDS;
		lpfc_read_nv(phba, pmb);
		memset((void *)mb->un.varRDnvp.rsvd3, 0,
		       sizeof (mb->un.varRDnvp.rsvd3));
		lpfc_swap_bcopy(pText, pText, 56);
		memcpy((void *)mb->un.varRDnvp.rsvd3, licensed,
		       sizeof (licensed));
		if (elx_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
			/* Adapter initialization error, mbxCmd <cmd> READ_NVPARM, mbxStatus <status> */
			elx_printf_log(phba->brd_no, &elx_msgBlk0324,	/* ptr to msg structure */
				       elx_mes0324,	/* ptr to msg */
				       elx_msgBlk0324.msgPreambleStr,	/* begin varargs */
				       mb->mbxCommand, mb->mbxStatus);	/* end varargs */
			return (ERESTART);
		}
		memcpy((uint8_t *) plhba->wwnn,
		       (uint8_t *) mb->un.varRDnvp.nodename,
		       sizeof (mb->un.varRDnvp.nodename));
	}
	/* Setup and issue mailbox READ REV command */
	phba->hba_state = ELX_INIT_MBX_CMDS;
	elx_read_rev(phba, pmb);
	if (elx_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		/* Adapter failed to init, mbxCmd <mbxCmd> READ_REV, mbxStatus <status> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0439,	/* ptr to msg structure */
			       elx_mes0439,	/* ptr to msg */
			       elx_msgBlk0439.msgPreambleStr,	/* begin varargs */
			       mb->mbxCommand, mb->mbxStatus);	/* end varargs */

		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
		return (ERESTART);
	}

	/* The HBA's current state is provided by the ProgType and rr fields.  Read
	 * and check the value of these fields before continuing to config this port.
	 */
	if (mb->un.varRdRev.rr == 0) {
		/* Old firmware */
		vp->rev.rBit = 0;
		/* Adapter failed to init, mbxCmd <cmd> READ_REV detected outdated firmware */
		elx_printf_log(phba->brd_no, &elx_msgBlk0440,	/* ptr to msg structure */
			       elx_mes0440,	/* ptr to msg */
			       elx_msgBlk0440.msgPreambleStr,	/* begin varargs */
			       mb->mbxCommand, 0);	/* end varargs */

		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
		return (ERESTART);
	} else {
		if (mb->un.varRdRev.un.b.ProgType != 2) {
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
			return (ERESTART);
		}
		vp->rev.rBit = 1;
		vp->rev.sli1FwRev = mb->un.varRdRev.sli1FwRev;
		memcpy((uint8_t *) vp->rev.sli1FwName,
		       (uint8_t *) mb->un.varRdRev.sli1FwName, 16);
		vp->rev.sli2FwRev = mb->un.varRdRev.sli2FwRev;
		memcpy((uint8_t *) vp->rev.sli2FwName,
		       (uint8_t *) mb->un.varRdRev.sli2FwName, 16);
	}

	/* Save information as VPD data */
	vp->rev.biuRev = mb->un.varRdRev.biuRev;
	vp->rev.smRev = mb->un.varRdRev.smRev;
	vp->rev.smFwRev = mb->un.varRdRev.un.smFwRev;
	vp->rev.endecRev = mb->un.varRdRev.endecRev;
	vp->rev.fcphHigh = mb->un.varRdRev.fcphHigh;
	vp->rev.fcphLow = mb->un.varRdRev.fcphLow;
	vp->rev.feaLevelHigh = mb->un.varRdRev.feaLevelHigh;
	vp->rev.feaLevelLow = mb->un.varRdRev.feaLevelLow;
	vp->rev.postKernRev = mb->un.varRdRev.postKernRev;
	vp->rev.opFwRev = mb->un.varRdRev.opFwRev;
	lpfc_rdrev_wd30 = mb->un.varWords[30];
#ifndef powerpc
	if ((((phba->pci_id >> 16) & 0xffff) == PCI_DEVICE_ID_TFLY) ||
	    (((phba->pci_id >> 16) & 0xffff) == PCI_DEVICE_ID_PFLY) ||
	    (((phba->pci_id >> 16) & 0xffff) == PCI_DEVICE_ID_LP101) ||
	    (((phba->pci_id >> 16) & 0xffff) == PCI_DEVICE_ID_RFLY)) {
#else
	if (((__swab16(phba->pci_id) & 0xffff) == PCI_DEVICE_ID_TFLY) ||
	    ((__swab16(phba->pci_id) & 0xffff) == PCI_DEVICE_ID_PFLY) ||
	    ((__swab16(phba->pci_id) & 0xffff) == PCI_DEVICE_ID_RFLY)) {
#endif
		memcpy((uint8_t *) plhba->RandomData,
		       (uint8_t *) & mb->un.varWords[24],
		       sizeof (plhba->RandomData));
	}

	if (lpfc_check_for_vpd) {
		/* Get adapter VPD information */
		lpfc_dump_mem(phba, pmb);
		if (elx_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
			/*
			 * Let it go through even if failed.
			 */
			/* Adapter failed to init, mbxCmd <cmd> DUMP VPD, mbxStatus <status> */
			elx_printf_log(phba->brd_no, &elx_msgBlk0441,	/* ptr to msg structure */
				       elx_mes0441,	/* ptr to msg */
				       elx_msgBlk0441.msgPreambleStr,	/* begin varargs */
				       mb->mbxCommand, mb->mbxStatus);	/* end varargs */

		} else {
			if ((mb->un.varDmp.ra == 1) &&
			    (mb->un.varDmp.word_cnt <= LPFC_MAX_VPD_SIZE)) {
				uint32_t *lp1, *lp2;

				lp1 = (uint32_t *) & mb->un.varDmp.resp_offset;
				lp2 = (uint32_t *) & lpfc_vpd_data[0];
				for (i = 0; i < mb->un.varDmp.word_cnt; i++) {
					status = *lp1++;
					*lp2++ = SWAP_LONG(status);
				}
				lpfc_parse_vpd(phba,
					       (uint8_t *) & lpfc_vpd_data[0]);
			}
		}
	}
	elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
	return (0);
}

/************************************************************************/
/*                                                                      */
/*    lpfc_config_port_post                                             */
/*    This routine will do LPFC initialization after the                */
/*    CONFIG_PORT mailbox command. This will be initialized             */
/*    as a SLI layer callback routine.                                  */
/*    This routine returns 0 on success. Any other return value         */
/*    indicates an error.                                               */
/*                                                                      */
/************************************************************************/
int
lpfc_config_port_post(elxHBA_t * phba)
{
	ELX_MBOXQ_t *pmb;
	MAILBOX_t *mb;
	DMABUF_t *mp;
	ELX_SLI_t *psli;
	LPFCHBA_t *plhba;
	ELXCLOCK_INFO_t *clock_info;
	elxCfgParam_t *clp;
	uint32_t status;
	int i, j, flogi_sent;
	unsigned long iflag, isr_cnt, clk_cnt;
	uint32_t timeout;

	psli = &phba->sli;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	clp = &phba->config[0];

	/* Get a Mailbox buffer to setup mailbox commands for HBA initialization */
	if ((pmb =
	     (ELX_MBOXQ_t *) elx_mem_get(phba, (MEM_MBOX | MEM_PRI))) == 0) {
		phba->hba_state = ELX_HBA_ERROR;
		return (ENOMEM);
	}
	mb = &pmb->mb;

	/* Setup link timers */
	lpfc_config_link(phba, pmb);
	if (elx_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		/* Adapter failed to init, mbxCmd <cmd> CONFIG_LINK mbxStatus <status> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0447,	/* ptr to msg structure */
			       elx_mes0447,	/* ptr to msg */
			       elx_msgBlk0447.msgPreambleStr,	/* begin varargs */
			       mb->mbxCommand, mb->mbxStatus);	/* end varargs */
		phba->hba_state = ELX_HBA_ERROR;
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
		return (EIO);
	}

	/* Get login parameters for NID.  */
	lpfc_read_sparam(phba, pmb);
	if (elx_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		/* Adapter failed to init, mbxCmd <cmd> READ_SPARM mbxStatus <status> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0448,	/* ptr to msg structure */
			       elx_mes0448,	/* ptr to msg */
			       elx_msgBlk0448.msgPreambleStr,	/* begin varargs */
			       mb->mbxCommand, mb->mbxStatus);	/* end varargs */
		phba->hba_state = ELX_HBA_ERROR;
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
		return (EIO);
	}

	mp = (DMABUF_t *) pmb->context1;
	elx_pci_dma_sync((void *)phba, (void *)mp, 0, ELX_DMA_SYNC_FORCPU);

	memcpy((uint8_t *) & plhba->fc_sparam, (uint8_t *) mp->virt,
	       sizeof (SERV_PARM));
	elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
	pmb->context1 = 0;

	memcpy((uint8_t *) & plhba->fc_nodename,
	       (uint8_t *) & plhba->fc_sparam.nodeName, sizeof (NAME_TYPE));
	memcpy((uint8_t *) & plhba->fc_portname,
	       (uint8_t *) & plhba->fc_sparam.portName, sizeof (NAME_TYPE));
	memcpy(plhba->phys_addr, plhba->fc_portname.IEEE, 6);
	/* If no serial number in VPD data, use low 6 bytes of WWNN */
	if (phba->SerialNumber[0] == 0) {
		uint8_t *outptr;

		outptr = (uint8_t *) & plhba->fc_nodename.IEEE[0];
		for (i = 0; i < 12; i++) {
			status = *outptr++;
			j = ((status & 0xf0) >> 4);
			if (j <= 9)
				phba->SerialNumber[i] =
				    (char)((uint8_t) 0x30 + (uint8_t) j);
			else
				phba->SerialNumber[i] =
				    (char)((uint8_t) 0x61 + (uint8_t) (j - 10));
			i++;
			j = (status & 0xf);
			if (j <= 9)
				phba->SerialNumber[i] =
				    (char)((uint8_t) 0x30 + (uint8_t) j);
			else
				phba->SerialNumber[i] =
				    (char)((uint8_t) 0x61 + (uint8_t) (j - 10));
		}
	}

	/* This should turn on DELAYED ABTS for ELS timeouts */
	lpfc_set_slim(phba, pmb, 0x052198, 0x1);
	if (elx_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		phba->hba_state = ELX_HBA_ERROR;
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
		return (EIO);
	}
	if (clp[LPFC_CFG_NETWORK_ON].a_current) {
		if ((plhba->fc_sparam.portName.nameType != NAME_IEEE) ||
		    (plhba->fc_sparam.portName.IEEEextMsn != 0) ||
		    (plhba->fc_sparam.portName.IEEEextLsb != 0)) {
			clp[LPFC_CFG_NETWORK_ON].a_current = 0;

			elx_printf_log(phba->brd_no, &elx_msgBlk0449,	/* ptr to msg structure */
				       elx_mes0449,	/* ptr to msg */
				       elx_msgBlk0449.msgPreambleStr,	/* begin varargs */
				       plhba->fc_sparam.portName.nameType);	/* end varargs */
		}

		/* Issue CONFIG FARP NEW_FEATURE */
		lpfc_config_farp(phba, pmb);
		if (elx_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
			/*
			 * Let it go through even if failed.
			 */
			/* Adapter failed to init, mbxCmd <cmd> FARP, mbxStatus <status> */
			elx_printf_log(phba->brd_no, &elx_msgBlk0450,	/* ptr to msg structure */
				       elx_mes0450,	/* ptr to msg */
				       elx_msgBlk0450.msgPreambleStr,	/* begin varargs */
				       mb->mbxCommand, mb->mbxStatus);	/* end varargs */
		}
	}

	lpfc_read_config(phba, pmb);
	if (elx_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		/* Adapter failed to init, mbxCmd <cmd> READ_CONFIG, mbxStatus <status> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0453,	/* ptr to msg structure */
			       elx_mes0453,	/* ptr to msg */
			       elx_msgBlk0453.msgPreambleStr,	/* begin varargs */
			       mb->mbxCommand, mb->mbxStatus);	/* end varargs */
		phba->hba_state = ELX_HBA_ERROR;
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
		return (EIO);
	}
	if (mb->un.varRdConfig.lmt & LMT_2125_10bit) {
		/* HBA is 2G capable */
		plhba->fc_flag |= FC_2G_CAPABLE;
	} else {
		/* If the HBA is not 2G capable, don't let link speed ask for it */
		if (clp[LPFC_CFG_LINK_SPEED].a_current > 1) {
			/* Reset link speed to auto. 1G HBA cfg'd for 2G */
			elx_printf_log(phba->brd_no, &elx_msgBlk1302,	/* ptr to msg structure */
				       elx_mes1302,	/* ptr to msg */
				       elx_msgBlk1302.msgPreambleStr,	/* begin varargs */
				       clp[LPFC_CFG_LINK_SPEED].a_current);	/* end varargs */
			clp[LPFC_CFG_LINK_SPEED].a_current = LINK_SPEED_AUTO;
		}
	}

	if (phba->intr_inited != 1) {
		/* Add our interrupt routine to kernel's interrupt chain & enable it */

		if ((psli->sliinit.elx_sli_register_intr) ((void *)phba) != 0) {
			/* Enable interrupt handler failed */
			elx_printf_log(phba->brd_no, &elx_msgBlk0451,	/* ptr to msg structure */
				       elx_mes0451,	/* ptr to msg */
				       elx_msgBlk0451.msgPreambleStr);	/* begin & end varargs */
			phba->hba_state = ELX_HBA_ERROR;
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
			return (EIO);
		}
		phba->intr_inited = 1;
	}

	phba->hba_state = ELX_LINK_DOWN;
	plhba->fc_flag |= FC_LNK_DOWN;

	/* Only process IOCBs on ring 0 till hba_state is READY */
	if (psli->ring[psli->ip_ring].cmdringaddr)
		psli->ring[psli->ip_ring].flag |= ELX_STOP_IOCB_EVENT;
	if (psli->ring[psli->fcp_ring].cmdringaddr)
		psli->ring[psli->fcp_ring].flag |= ELX_STOP_IOCB_EVENT;
	if (psli->ring[psli->next_ring].cmdringaddr)
		psli->ring[psli->next_ring].flag |= ELX_STOP_IOCB_EVENT;

	/* Post receive buffers for desired rings */
	lpfc_post_rcv_buf(phba);

	ELX_SLI_LOCK(phba, iflag);

	/* Enable appropriate host interrupts */
	status = (psli->sliinit.elx_sli_read_HC) (phba);
	status |= (uint32_t) (HC_MBINT_ENA | HC_ERINT_ENA | HC_LAINT_ENA);
	if (psli->sliinit.num_rings > 0)
		status |= HC_R0INT_ENA;
	if (psli->sliinit.num_rings > 1)
		status |= HC_R1INT_ENA;
	if (psli->sliinit.num_rings > 2)
		status |= HC_R2INT_ENA;
	if (psli->sliinit.num_rings > 3)
		status |= HC_R3INT_ENA;

	(psli->sliinit.elx_sli_write_HC) (phba, status);

	/* Setup and issue mailbox INITIALIZE LINK command */
	lpfc_init_link(phba, pmb, clp[LPFC_CFG_TOPOLOGY].a_current,
		       clp[LPFC_CFG_LINK_SPEED].a_current);

	clock_info = &elxDRVR.elx_clock_info;
	isr_cnt = psli->slistat.sliIntr;
	clk_cnt = clock_info->ticks;

	ELX_SLI_UNLOCK(phba, iflag);
	if (elx_sli_issue_mbox(phba, pmb, MBX_NOWAIT) != MBX_SUCCESS) {
		/* Adapter failed to init, mbxCmd <cmd> INIT_LINK, mbxStatus <status> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0454,	/* ptr to msg structure */
			       elx_mes0454,	/* ptr to msg */
			       elx_msgBlk0454.msgPreambleStr,	/* begin varargs */
			       mb->mbxCommand, mb->mbxStatus);	/* end varargs */
		(psli->sliinit.elx_sli_unregister_intr) ((void *)phba);
		phba->intr_inited = 0;
		phba->hba_state = ELX_HBA_ERROR;
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
		return (EIO);
	}
	/* MEM_MBOX buffer will be freed in mbox compl */

	/*
	 * Setup the ring 0 (els)  timeout handler
	 */
	timeout = plhba->fc_ratov << 1;
	phba->els_tmofunc = elx_clk_set(phba, timeout, lpfc_els_timeout_handler,
					(void *)(unsigned long)timeout, 0);

	plhba->fc_prevDID = Mask_DID;
	flogi_sent = 0;
	i = 0;
	while ((phba->hba_state != ELX_HBA_READY) ||
	       (plhba->num_disc_nodes) || (plhba->fc_prli_sent) ||
	       (plhba->fc_map_cnt == 0) ||
	       (psli->sliinit.sli_flag & ELX_SLI_MBOX_ACTIVE)) {
		/* Check every second for 30 retries. */
		i++;
		if (i > 30) {
			break;
		}
		if ((i >= 15) && (phba->hba_state <= ELX_LINK_DOWN)) {
			/* The link is down.  Set linkdown timeout */

			if ((clp[ELX_CFG_LINKDOWN_TMO].a_current == 0) ||
			    clp[ELX_CFG_HOLDIO].a_current) {
				plhba->fc_flag |= (FC_LD_TIMER | FC_LD_TIMEOUT);
				phba->hba_flag |= FC_LFR_ACTIVE;
			} else {
				plhba->fc_flag |= FC_LD_TIMER;
				phba->hba_flag |= FC_LFR_ACTIVE;
				if (plhba->fc_linkdown) {
					elx_clk_res(phba,
						    clp[ELX_CFG_LINKDOWN_TMO].
						    a_current,
						    plhba->fc_linkdown);
				} else {
					if (clp[ELX_CFG_HOLDIO].a_current == 0) {
						plhba->fc_linkdown =
						    elx_clk_set(phba,
								clp
								[ELX_CFG_LINKDOWN_TMO].
								a_current,
								lpfc_linkdown_timeout,
								0, 0);
					}
				}
			}
			break;
		}

		/* 20 * 50ms is identically 1sec */
		for (j = 0; j < 20; j++) {
			mdelay(50);
			/* On some systems hardware interrupts cannot interrupt the
			 * attach / detect routine. If this is the case, manually call
			 * the ISR every 50 ms to service any potential interrupt.
			 */
			ELX_DRVR_LOCK(phba, iflag);
			if (isr_cnt == psli->slistat.sliIntr) {
				elx_sli_intr(phba);
				isr_cnt = psli->slistat.sliIntr;
			}
			ELX_DRVR_UNLOCK(phba, iflag);
		}
		isr_cnt = psli->slistat.sliIntr;

		/* On some systems clock interrupts cannot interrupt the
		 * attach / detect routine. If this is the case, manually call
		 * the clock routine every sec to service any potential timeouts.
		 */
		if (clk_cnt == clock_info->ticks) {
			elx_timer(0);
			clk_cnt = clock_info->ticks;
		}
	}

	/* Since num_disc_nodes keys off of PLOGI, delay a bit to let
	 * any potential PRLIs to flush thru the SLI sub-system.
	 */
	mdelay(50);
	ELX_DRVR_LOCK(phba, iflag);
	if (isr_cnt == psli->slistat.sliIntr) {
		elx_sli_intr(phba);
	}

	ELX_DRVR_UNLOCK(phba, iflag);

	return (0);
}

/************************************************************************/
/*                                                                      */
/*    lpfc_hba_down_prep                                                */
/*    This routine will do LPFC uninitialization before the             */
/*    HBA is reset when bringing down the SLI Layer. This will be       */
/*    initialized as a SLI layer callback routine.                      */
/*    This routine returns 0 on success. Any other return value         */
/*    indicates an error.                                               */
/*                                                                      */
/************************************************************************/
int
lpfc_hba_down_prep(elxHBA_t * phba)
{
	ELX_SLI_t *psli;
	ELX_SLINK_t *dlp;
	ELX_SLI_RING_t *pring;
	DMABUF_t *mp;
	DMABUFIP_t *mpip;
	unsigned long iflag;

	psli = &phba->sli;
	ELX_SLI_LOCK(phba, iflag);
	/* Disable interrupts */
	(psli->sliinit.elx_sli_write_HC) (phba, 0);

	/* Now cleanup posted buffers on each ring */
	pring = &psli->ring[LPFC_ELS_RING];	/* RING 0 */
	dlp = &pring->postbufq;
	while (dlp->q_first) {
		mp = (DMABUF_t *) dlp->q_first;
		dlp->q_first = (ELX_SLINK_t *) mp->next;
		dlp->q_cnt--;
		elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
	}
	dlp->q_last = 0;
	pring = &psli->ring[psli->ip_ring];	/* RING 1 */
	dlp = &pring->postbufq;
	while (dlp->q_first) {
		mpip = (DMABUFIP_t *) dlp->q_first;
		dlp->q_first = (ELX_SLINK_t *) mpip->dma.next;
		dlp->q_cnt--;
		elx_mem_put(phba, MEM_IP_RCV_BUF, (uint8_t *) mpip);
	}
	dlp->q_last = 0;
	pring = &psli->ring[psli->fcp_ring];	/* RING 2 */
	pring = &psli->ring[psli->next_ring];	/* RING 3 */
	dlp = &pring->postbufq;
	while (dlp->q_first) {
		mpip = (DMABUFIP_t *) dlp->q_first;
		dlp->q_first = (ELX_SLINK_t *) mpip->dma.next;
		dlp->q_cnt--;
		elx_mem_put(phba, MEM_FCP_CMND_BUF, (uint8_t *) mpip);
	}
	dlp->q_last = 0;
	ELX_SLI_UNLOCK(phba, iflag);
	return (0);
}

/************************************************************************/
/*                                                                      */
/*    lpfc_handle_eratt                                                 */
/*    This routine will handle processing a Host Attention              */
/*    Error Status event. This will be initialized                      */
/*    as a SLI layer callback routine.                                  */
/*                                                                      */
/************************************************************************/
void
lpfc_handle_eratt(elxHBA_t * phba, uint32_t status)
{
	ELX_SLI_t *psli;
	LPFCHBA_t *plhba;
	ELX_SLI_RING_t *pring;
	ELX_IOCBQ_t *iocb, *next_iocb;
	IOCB_t *icmd = NULL, *cmd = NULL;
	ELX_SCSI_BUF_t *elx_cmd;
	unsigned long iflag;
	volatile uint32_t status1, status2;

	psli = &phba->sli;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	(psli->sliinit.elx_sli_read_slim) ((void *)phba, (void *)&status1,
					   (int)0xa8, sizeof (uint32_t));
	(psli->sliinit.elx_sli_read_slim) ((void *)phba, (void *)&status2,
					   (int)0xac, sizeof (uint32_t));

	if (status & HS_FFER6) {
		/* Re-establishing Link */
		elx_printf_log(phba->brd_no, &elx_msgBlk1301,	/* ptr to msg structure */
			       elx_mes1301,	/* ptr to msg */
			       elx_msgBlk1301.msgPreambleStr,	/* begin varargs */
			       status, status1, status2);	/* end varargs */
		plhba->fc_flag |= FC_ESTABLISH_LINK;

		/* 
		 * Firmware stops when it triggled erratt with HS_FFER6.
		 * That could cause the I/Os dropped by the firmware.
		 * Error iocb (I/O) on txcmplq and let the SCSI layer 
		 * retry it after re-establishing link. 
		 */
		pring = &psli->ring[psli->fcp_ring];
		next_iocb = (ELX_IOCBQ_t *) pring->txcmplq.q_f;
		while (next_iocb != (ELX_IOCBQ_t *) & pring->txcmplq) {
			iocb = next_iocb;
			next_iocb = next_iocb->q_f;
			cmd = &iocb->iocb;

			/* Must be a FCP command */
			if ((cmd->ulpCommand != CMD_FCP_ICMND64_CR) &&
			    (cmd->ulpCommand != CMD_FCP_IWRITE64_CR) &&
			    (cmd->ulpCommand != CMD_FCP_IREAD64_CR)) {
				continue;
			}

			/* context1 MUST be a ELX_SCSI_BUF_t */
			elx_cmd = (ELX_SCSI_BUF_t *) (iocb->context1);
			if (elx_cmd == 0) {
				continue;
			}

			elx_deque(iocb);
			pring->txcmplq.q_cnt--;
			if (iocb->iocb_cmpl) {
				icmd = &iocb->iocb;
				icmd->ulpStatus = IOSTAT_DRIVER_REJECT;
				icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
				ELX_SLI_UNLOCK(phba, iflag);
				(iocb->iocb_cmpl) ((void *)phba, iocb, iocb);
				ELX_SLI_LOCK(phba, iflag);
			} else {
				elx_mem_put(phba, MEM_IOCB, (uint8_t *) iocb);
			}
		}

		lpfc_offline(phba);
		if (lpfc_online(phba) == 0) {	/* Initialize the HBA */
			if (plhba->fc_estabtmo) {
				elx_clk_can(phba, plhba->fc_estabtmo);
			}
			plhba->fc_estabtmo =
			    elx_clk_set(phba, 60, lpfc_establish_link_tmo, 0,
					0);
			return;
		}
	}
	/* Adapter Hardware Error */
	elx_printf_log(phba->brd_no, &elx_msgBlk0457,	/* ptr to msg structure */
		       elx_mes0457,	/* ptr to msg */
		       elx_msgBlk0457.msgPreambleStr,	/* begin varargs */
		       status, status1, status2);	/* end varargs */

	lpfc_offline(phba);
	return;
}

/************************************************************************/
/*                                                                      */
/*    lpfc_handle_latt                                                  */
/*    This routine will handle processing a Host Attention              */
/*    Link Status event. This will be initialized                       */
/*    as a SLI layer callback routine.                                  */
/*                                                                      */
/************************************************************************/
void
lpfc_handle_latt(elxHBA_t * phba)
{
	ELX_SLI_t *psli;
	ELX_MBOXQ_t *pmb;
	volatile uint32_t control;

	/* called from host_interrupt, to process LATT */
	psli = &phba->sli;

	psli->slistat.linkEvent++;

	/* Get a buffer which will be used for mailbox commands */
	if ((pmb = (ELX_MBOXQ_t *) elx_mem_get(phba, (MEM_MBOX | MEM_PRI)))) {
		if (lpfc_read_la(phba, pmb) == 0) {
			pmb->mbox_cmpl = lpfc_mbx_cmpl_read_la;
			if (elx_sli_issue_mbox
			    (phba, pmb, (MBX_NOWAIT | MBX_STOP_IOCB))
			    != MBX_NOT_FINISHED) {
				/* Turn off Link Attention interrupts until CLEAR_LA done */
				psli->sliinit.sli_flag &= ~ELX_PROCESS_LA;
				control =
				    (psli->sliinit.elx_sli_read_HC) (phba);
				control &= ~HC_LAINT_ENA;
				(psli->sliinit.elx_sli_write_HC) (phba,
								  control);

				/* Clear Link Attention in HA REG */
				(psli->sliinit.elx_sli_write_HA) (phba,
								  HA_LATT);
			} else {
				elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
			}
		} else {
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
		}
	}
	return;
}

/************************************************************************/
/*                                                                      */
/*   lpfc_parse_vpd                                                     */
/*   This routine will parse the VPD data                               */
/*                                                                      */
/************************************************************************/
int
lpfc_parse_vpd(elxHBA_t * phba, uint8_t * vpd)
{
	uint8_t lenlo, lenhi;
	uint8_t *Length;
	int i, j;
	int finished = 0;
	int index = 0;

	/* Vital Product */
	elx_printf_log(phba->brd_no, &elx_msgBlk0455,	/* ptr to msg structure */
		       elx_mes0455,	/* ptr to msg */
		       elx_msgBlk0455.msgPreambleStr,	/* begin varargs */
		       (uint32_t) vpd[0], (uint32_t) vpd[1], (uint32_t) vpd[2], (uint32_t) vpd[3]);	/* end varargs */
	do {
		switch (vpd[index]) {
		case 0x82:
			index += 1;
			lenlo = vpd[index];
			index += 1;
			lenhi = vpd[index];
			index += 1;
			i = ((((unsigned short)lenhi) << 8) + lenlo);
			index += i;
			break;
		case 0x90:
			index += 1;
			lenlo = vpd[index];
			index += 1;
			lenhi = vpd[index];
			index += 1;
			i = ((((unsigned short)lenhi) << 8) + lenlo);
			do {
				/* Look for Serial Number */
				if ((vpd[index] == 'S')
				    && (vpd[index + 1] == 'N')) {
					index += 2;
					Length = &vpd[index];
					index += 1;
					i = *Length;
					j = 0;
					while (i--) {
						phba->SerialNumber[j++] =
						    vpd[index++];
						if (j == 31)
							break;
					}
					phba->SerialNumber[j] = 0;
					return (1);
				} else {
					index += 2;
					Length = &vpd[index];
					index += 1;
					j = (int)(*Length);
					index += j;
					i -= (3 + j);
				}
			} while (i > 0);
			finished = 0;
			break;
		case 0x78:
			finished = 1;
			break;
		default:
			return (0);
		}
	} while (!finished);
	return (1);
}

/**************************************************/
/*   lpfc_post_buffer                             */
/*                                                */
/*   This routine will post count buffers to the  */
/*   ring with the QUE_RING_BUF_CN command. This  */
/*   allows 3 buffers / command to be posted.     */
/*   Returns the number of buffers NOT posted.    */
/**************************************************/
int
lpfc_post_buffer(elxHBA_t * phba, ELX_SLI_RING_t * pring, int cnt, int type)
{
	IOCB_t *icmd;
	ELX_IOCBQ_t *iocb;
	DMABUF_t *mp1, *mp2;
	int mem_flag;

	cnt += pring->missbufcnt;
	if (type == 2)
		mem_flag = MEM_FCP_CMND_BUF;
	else
		mem_flag = MEM_BUF;

	/* While there are buffers to post */
	while (cnt > 0) {
		/* Allocate buffer for  command iocb */
		if ((iocb =
		     (ELX_IOCBQ_t *) elx_mem_get(phba,
						 MEM_IOCB | MEM_PRI)) == 0) {
			pring->missbufcnt = cnt;
			return (cnt);
		}
		memset((void *)iocb, 0, sizeof (ELX_IOCBQ_t));
		icmd = &iocb->iocb;

		/* 2 buffers can be posted per command */
		/* Allocate buffer to post */
		if ((mp1 =
		     (DMABUF_t *) elx_mem_get(phba,
					      (mem_flag | MEM_PRI))) == 0) {
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) iocb);
			pring->missbufcnt = cnt;
			return (cnt);
		}
		/* Allocate buffer to post */
		if (cnt > 1) {
			if ((mp2 =
			     (DMABUF_t *) elx_mem_get(phba,
						      (mem_flag | MEM_PRI))) ==
			    0) {
				elx_mem_put(phba, mem_flag, (uint8_t *) mp1);
				elx_mem_put(phba, MEM_IOCB, (uint8_t *) iocb);
				pring->missbufcnt = cnt;
				return (cnt);
			}
		} else {
			mp2 = 0;
		}

		icmd->un.cont64[0].addrHigh = putPaddrHigh(mp1->phys);
		icmd->un.cont64[0].addrLow = putPaddrLow(mp1->phys);
		icmd->un.cont64[0].tus.f.bdeSize = FCELSSIZE;
		icmd->ulpBdeCount = 1;
		cnt--;
		if (mp2) {
			icmd->un.cont64[1].addrHigh = putPaddrHigh(mp2->phys);
			icmd->un.cont64[1].addrLow = putPaddrLow(mp2->phys);
			icmd->un.cont64[1].tus.f.bdeSize = FCELSSIZE;
			cnt--;
			icmd->ulpBdeCount = 2;
		}

		icmd->ulpCommand = CMD_QUE_RING_BUF64_CN;
		icmd->ulpIoTag = elx_sli_next_iotag(phba, pring);
		icmd->ulpLe = 1;
		icmd->ulpOwner = OWN_CHIP;

		if (elx_sli_issue_iocb(phba, pring, iocb, SLI_IOCB_USE_TXQ) ==
		    IOCB_ERROR) {
			elx_mem_put(phba, mem_flag, (uint8_t *) mp1);
			if (mp2) {
				elx_mem_put(phba, mem_flag, (uint8_t *) mp2);
			}
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) iocb);
			pring->missbufcnt = cnt;
			return (cnt);
		}
		elx_sli_ringpostbuf_put(phba, pring, mp1);
		if (mp2) {
			elx_sli_ringpostbuf_put(phba, pring, mp2);
		}
	}
	pring->missbufcnt = 0;
	return (0);
}

/************************************************************************/
/*                                                                      */
/*   lpfc_post_rcv_buf                                                  */
/*   This routine post initial rcv buffers to the configured rings      */
/*                                                                      */
/************************************************************************/
int
lpfc_post_rcv_buf(elxHBA_t * phba)
{
	ELX_SLI_t *psli;
	elxCfgParam_t *clp;

	psli = &phba->sli;
	clp = &phba->config[0];

	/* Ring 0, ELS / CT buffers */
	lpfc_post_buffer(phba, &psli->ring[LPFC_ELS_RING], LPFC_BUF_RING0, 1);

	/* Ring 1, IP Buffers */
	if (clp[LPFC_CFG_NETWORK_ON].a_current) {
		lpfc_ip_post_buffer(phba, &psli->ring[LPFC_IP_RING],
				    clp[LPFC_CFG_POST_IP_BUF].a_current);
	}

	/* Ring 2 - FCP no buffers needed */

	return (0);
}

#define S(N,V) (((V)<<(N))|((V)>>(32-(N))))

/************************************************************************/
/*                                                                      */
/*   lpfc_sha_init                                                      */
/*                                                                      */
/************************************************************************/
void
lpfc_sha_init(uint32_t * HashResultPointer)
{
	HashResultPointer[0] = 0x67452301;
	HashResultPointer[1] = 0xEFCDAB89;
	HashResultPointer[2] = 0x98BADCFE;
	HashResultPointer[3] = 0x10325476;
	HashResultPointer[4] = 0xC3D2E1F0;
}

/************************************************************************/
/*                                                                      */
/*   lpfc_sha_iterate                                                   */
/*                                                                      */
/************************************************************************/
void
lpfc_sha_iterate(uint32_t * HashResultPointer, uint32_t * HashWorkingPointer)
{
	int t;
	uint32_t TEMP;
	uint32_t A, B, C, D, E;
	t = 16;
	do {
		HashWorkingPointer[t] =
		    S(1,
		      HashWorkingPointer[t - 3] ^ HashWorkingPointer[t -
								     8] ^
		      HashWorkingPointer[t - 14] ^ HashWorkingPointer[t - 16]);
	} while (++t <= 79);
	t = 0;
	A = HashResultPointer[0];
	B = HashResultPointer[1];
	C = HashResultPointer[2];
	D = HashResultPointer[3];
	E = HashResultPointer[4];

	do {
		if (t < 20) {
			TEMP = ((B & C) | ((~B) & D)) + 0x5A827999;
		} else if (t < 40) {
			TEMP = (B ^ C ^ D) + 0x6ED9EBA1;
		} else if (t < 60) {
			TEMP = ((B & C) | (B & D) | (C & D)) + 0x8F1BBCDC;
		} else {
			TEMP = (B ^ C ^ D) + 0xCA62C1D6;
		}
		TEMP += S(5, A) + E + HashWorkingPointer[t];
		E = D;
		D = C;
		C = S(30, B);
		B = A;
		A = TEMP;
	} while (++t <= 79);

	HashResultPointer[0] += A;
	HashResultPointer[1] += B;
	HashResultPointer[2] += C;
	HashResultPointer[3] += D;
	HashResultPointer[4] += E;

}

/************************************************************************/
/*                                                                      */
/*   lpfc_challenge_key                                                 */
/*                                                                      */
/************************************************************************/
void
lpfc_challenge_key(uint32_t * RandomChallenge, uint32_t * HashWorking)
{
	*HashWorking = (*RandomChallenge ^ *HashWorking);
}

/************************************************************************/
/*                                                                      */
/*   lpfc_hba_init                                                      */
/*                                                                      */
/************************************************************************/
void
lpfc_hba_init(elxHBA_t * phba)
{
	int t;
	uint32_t HashWorking[80];
	uint32_t *pwwnn;
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	pwwnn = plhba->wwnn;
	memset(HashWorking, 0, sizeof (HashWorking));
	HashWorking[0] = HashWorking[78] = *pwwnn++;
	HashWorking[1] = HashWorking[79] = *pwwnn;
	for (t = 0; t < 7; t++) {
		lpfc_challenge_key(plhba->RandomData + t, HashWorking + t);
	}
	lpfc_sha_init(plhba->hbainitEx);
	lpfc_sha_iterate(plhba->hbainitEx, HashWorking);
}

void
lpfc_cleanup(elxHBA_t * phba, uint32_t save_bind)
{
	LPFCHBA_t *plhba;
	LPFC_NODELIST_t *ndlp;
	LPFC_BINDLIST_t *bdlp;
	uint8_t *ptr;
	unsigned long iflag;

	plhba = (LPFCHBA_t *) phba->pHbaProto;

	/* clean up plhba - lpfc specific */
	ELX_DISC_LOCK(phba, iflag);

	lpfc_can_disctmo(phba);

	ndlp = plhba->fc_nlpunmap_start;
	while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start) {
		ptr = (uint8_t *) ndlp;
		ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
		elx_mem_put(phba, MEM_NLP, ptr);
	}
	ndlp = plhba->fc_nlpmap_start;
	while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start) {
		ptr = (uint8_t *) ndlp;
		bdlp = (ndlp->nlp_listp_bind);
		ndlp->nlp_listp_bind = 0;
		ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
		elx_mem_put(phba, MEM_NLP, ptr);
		if (bdlp) {
			if (save_bind == 0) {
				elx_mem_put(phba, MEM_BIND, (uint8_t *) bdlp);
			} else {
				lpfc_nlp_bind(phba, bdlp);
			}
		}
	}
	ndlp = plhba->fc_plogi_start;
	while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_plogi_start) {
		ptr = (uint8_t *) ndlp;
		ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
		elx_mem_put(phba, MEM_NLP, ptr);
	}
	ndlp = plhba->fc_adisc_start;
	while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_adisc_start) {
		ptr = (uint8_t *) ndlp;
		ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
		elx_mem_put(phba, MEM_NLP, ptr);
	}

	if (save_bind == 0) {
		bdlp = plhba->fc_nlpbind_start;
		while (bdlp != (LPFC_BINDLIST_t *) & plhba->fc_nlpbind_start) {
			ptr = (uint8_t *) bdlp;
			bdlp = (LPFC_BINDLIST_t *) bdlp->nlp_listp_next;
			elx_mem_put(phba, MEM_BIND, ptr);
		}
		plhba->fc_nlpbind_start =
		    (LPFC_BINDLIST_t *) & plhba->fc_nlpbind_start;
		plhba->fc_nlpbind_end =
		    (LPFC_BINDLIST_t *) & plhba->fc_nlpbind_start;
		plhba->fc_bind_cnt = 0;
	}
	plhba->fc_nlpmap_start = (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start;
	plhba->fc_nlpmap_end = (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start;
	plhba->fc_map_cnt = 0;
	plhba->fc_nlpunmap_start =
	    (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start;
	plhba->fc_nlpunmap_end = (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start;
	plhba->fc_unmap_cnt = 0;
	plhba->fc_plogi_start = (LPFC_NODELIST_t *) & plhba->fc_plogi_start;
	plhba->fc_plogi_end = (LPFC_NODELIST_t *) & plhba->fc_plogi_start;
	plhba->fc_plogi_cnt = 0;
	plhba->fc_adisc_start = (LPFC_NODELIST_t *) & plhba->fc_adisc_start;
	plhba->fc_adisc_end = (LPFC_NODELIST_t *) & plhba->fc_adisc_start;
	plhba->fc_adisc_cnt = 0;
	ELX_DISC_UNLOCK(phba, iflag);
	return;
}

void
lpfc_establish_link_tmo(elxHBA_t * phba, void *n1, void *n2)
{
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	/* Re-establishing Link, timer expired */
	elx_printf_log(phba->brd_no, &elx_msgBlk1300,	/* ptr to msg structure */
		       elx_mes1300,	/* ptr to msg */
		       elx_msgBlk1300.msgPreambleStr,	/* begin varargs */
		       plhba->fc_flag, phba->hba_state);	/* end varargs */
	plhba->fc_flag &= ~FC_ESTABLISH_LINK;
}

int
lpfc_online(elxHBA_t * phba)
{
	LPFCHBA_t *plhba;

	if (phba) {
		plhba = (LPFCHBA_t *) phba->pHbaProto;
		if (!(plhba->fc_flag & FC_OFFLINE_MODE)) {
			return (0);
		}

		/* Bring Adapter online */
		elx_printf_log(phba->brd_no, &elx_msgBlk0458,	/* ptr to msg structure */
			       elx_mes0458,	/* ptr to msg */
			       elx_msgBlk0458.msgPreambleStr);	/* begin & end varargs */
		plhba->fc_flag &= ~FC_OFFLINE_MODE;

		if (!elx_sli_setup(phba)) {
			return (1);
		}
		if (elx_sli_hba_setup(phba)) {	/* Initialize the HBA */
			return (1);
		}

		elx_unblock_requests(phba);
		return (0);
	}
	return (0);
}

int
lpfc_offline(elxHBA_t * phba)
{
	LPFCHBA_t *plhba;
	ELX_SLI_RING_t *pring;
	ELX_SLI_t *psli;
	unsigned long iflag;
	int i;

	if (phba) {
		plhba = (LPFCHBA_t *) phba->pHbaProto;
		if (plhba->fc_flag & FC_OFFLINE_MODE) {
			return (0);
		}

		psli = &phba->sli;
		pring = &psli->ring[psli->fcp_ring];

		elx_block_requests(phba);

		lpfc_linkdown(phba);

		i = 0;
		while (pring->txcmplq.q_cnt) {
			ELX_DRVR_UNLOCK(phba, iflag);
			mdelay(10);
			ELX_DRVR_LOCK(phba, iflag);
			if (i++ > 3000)	/* 30 secs */
				break;
		}

		/* Bring Adapter offline */
		elx_printf_log(phba->brd_no, &elx_msgBlk0460,	/* ptr to msg structure */
			       elx_mes0460,	/* ptr to msg */
			       elx_msgBlk0460.msgPreambleStr);	/* begin & end varargs */

		elx_sli_hba_down(phba);	/* Bring down the SLI Layer */
		lpfc_cleanup(phba, 1);	/* Save bindings */
		plhba->fc_flag |= FC_OFFLINE_MODE;

		/*
		 * Cancel ELS timer
		 */
		if (phba->els_tmofunc) {
			elx_clk_can(phba, phba->els_tmofunc);
		}

		return (0);
	}
	return (0);
}

/******************************************************************************
* Function name : lpfc_scsi_free
*
* Description   : Called from fc_detach to free scsi tgt / lun resources
* 
******************************************************************************/
int
lpfc_scsi_free(elxHBA_t * phba)
{
	LPFCHBA_t *plhba;
	ELXSCSITARGET_t *targetp;
	ELXSCSILUN_t *lunp;
	ELXSCSILUN_t *nextlunp;
	MBUF_INFO_t *buf_info;
	MBUF_INFO_t bufinfo;
	int i;

	buf_info = &bufinfo;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	for (i = 0; i < MAX_FCP_TARGET; i++) {
		targetp = plhba->device_queue_hash[i];
		if (targetp) {
			lunp = (ELXSCSILUN_t *) targetp->lunlist.q_first;
			while (lunp) {
				nextlunp = lunp->pnextLun;
				memset(buf_info, 0, sizeof (MBUF_INFO_t));
				buf_info->size = sizeof (ELXSCSILUN_t);
				buf_info->flags = ELX_MBUF_VIRT;
				buf_info->align = sizeof (void *);
				buf_info->virt = lunp;
				elx_free(phba, buf_info);
				lunp = nextlunp;
			}

			if (targetp->RptLunData) {
				elx_mem_put(phba, MEM_BUF,
					    (uint8_t *) targetp->RptLunData);
			}

			memset(buf_info, 0, sizeof (MBUF_INFO_t));
			buf_info->size = sizeof (ELXSCSITARGET_t);
			buf_info->flags = ELX_MBUF_VIRT;
			buf_info->align = sizeof (void *);
			buf_info->virt = targetp;
			elx_free(phba, buf_info);
			plhba->device_queue_hash[i] = 0;
		}
	}
	return (0);
}

/******************************************************************************
* Function name : lpfc_parse_binding_entry
*
* Description   : Parse binding entry for WWNN & WWPN
*
* ASCII Input string example: 2000123456789abc:lpfc1t0
* 
* Return        :  0              = Success
*                  Greater than 0 = Binding entry syntax error. SEE defs
*                                   LPFC_SYNTAX_ERR_XXXXXX.
******************************************************************************/
int
lpfc_parse_binding_entry(elxHBA_t * phba,
			 uint8_t * inbuf,
			 uint8_t * outbuf,
			 int in_size,
			 int out_size,
			 int bind_type,
			 unsigned int *sum, int entry, int *lpfc_num)
{
	int brd;
	int c1, cvert_cnt, sumtmp;

	char ds_lpfc[] = "lpfc";

	*lpfc_num = -1;
	if (bind_type == LPFC_BIND_DID) {
		outbuf++;
	}
	/* Parse 16 digit ASC hex address */
	cvert_cnt =
	    elx_str_atox(phba, in_size, out_size, (char *)inbuf,
			 (char *)outbuf);
	if (cvert_cnt < 0)
		return (LPFC_SYNTAX_ERR_ASC_CONVERT);
	inbuf += (ulong) cvert_cnt;

	/* Parse colon */
	if (*inbuf++ != ':')
		return (LPFC_SYNTAX_ERR_EXP_COLON);

	/* Parse lpfc */
	if (elx_str_ncmp((char *)inbuf, ds_lpfc, (sizeof (ds_lpfc) - 1)))
		return (LPFC_SYNTAX_ERR_EXP_LPFC);
	inbuf += sizeof (ds_lpfc) - 1;

	/* Parse lpfc number */
	/* Get 1st lpfc digit */
	c1 = *inbuf++;
	if (elx_is_digit(c1) == 0)
		goto err_lpfc_num;
	sumtmp = c1 - 0x30;

	/* Get 2nd lpfc digit */
	c1 = *inbuf;
	if (elx_is_digit(c1) == 0)
		goto convert_instance;
	inbuf++;
	sumtmp = (sumtmp * 10) + c1 - 0x30;

	/* Get 3rd lpfc digit */
	c1 = *inbuf;
	if (elx_is_digit(c1) == 0)
		goto convert_instance;
	inbuf++;
	sumtmp = (sumtmp * 10) + c1 - 0x30;

	if ((sumtmp < 0) || (sumtmp > MAX_ELX_BRDS))
		goto err_lpfc_num;
	goto convert_instance;

      err_lpfc_num:

	return (LPFC_SYNTAX_ERR_INV_LPFC_NUM);

	/* Convert from ddi instance number to adapter number */
      convert_instance:
	for (brd = 0; brd < MAX_ELX_BRDS; brd++) {
		if (lpfc_instance[brd] == sumtmp)
			break;
	}
	if (phba->brd_no != brd) {
		/* Skip this entry */
		return (LPFC_SYNTAX_OK_BUT_NOT_THIS_BRD);
	}

	/* Parse 't' */
	if (*inbuf++ != 't')
		return (LPFC_SYNTAX_ERR_EXP_T);

	/* Parse target number */
	/* Get 1st target digit */
	c1 = *inbuf++;
	if (elx_is_digit(c1) == 0)
		goto err_target_num;
	sumtmp = c1 - 0x30;

	/* Get 2nd target digit */
	c1 = *inbuf;
	if (elx_is_digit(c1) == 0)
		goto check_for_term;
	inbuf++;
	sumtmp = (sumtmp * 10) + c1 - 0x30;

	/* Get 3nd target digit */
	c1 = *inbuf;
	if (elx_is_digit(c1) == 0)
		goto check_for_term;
	inbuf++;
	sumtmp = (sumtmp * 10) + c1 - 0x30;
	if (sumtmp > (LPFC_MAX_SCSI_ID_PER_PAN - 1))
		goto err_target_num;
	goto check_for_term;

      err_target_num:
	return (LPFC_SYNTAX_ERR_INV_TARGET_NUM);

      check_for_term:

	if (*inbuf != 0)
		return (LPFC_SYNTAX_ERR_EXP_NULL_TERM);

	*sum = sumtmp;
	return (LPFC_SYNTAX_OK);	/* Success */
}

#include "lpfc_ioctl.h"

extern elxDRVR_t elxDRVR;
extern int lpfc_instcnt;
extern char *lpfc_release_version;

extern int elx_initpci(struct dfc_info *, elxHBA_t *);

/*************************************************************************/
/*  Global data structures                                               */
/*************************************************************************/

uint32_t fc_dbg_flag;

struct dfc dfc;

struct dfc_mem {
	uint32_t fc_outsz;
	uint32_t fc_filler;
	void *fc_dataout;
};

/******************************************************************************/
/* NOTE: Allocate functions must make certain the last argument is            */
/*       initialized to zero for all these Macros.                            */
/******************************************************************************/
#define RPTLUN_MIN_LEN 0x1000
#define ELX_FREE(a, b) if (b) elx_free(a, b)
#define ELX_MEM_PUT(a, b, c) if (c) elx_mem_put(a, b, c)
#define DFC_CMD_DATA_FREE(a, b) if (b) dfc_cmd_data_free(a, b)

#define LPFC_MAX_EVENT 4

/* Routine Declaration - Local */
int dfc_issue_mbox(elxHBA_t *, MAILBOX_t *);
DMABUFEXT_t *dfc_cmd_data_alloc(elxHBA_t *, char *, ULP_BDE64 *, uint32_t);
int dfc_cmd_data_free(elxHBA_t *, DMABUFEXT_t *);
int dfc_rsp_data_copy(elxHBA_t *, uint8_t *, DMABUFEXT_t *, uint32_t);
DMABUFEXT_t *dfc_fcp_data_alloc(elxHBA_t * p, ULP_BDE64 * bpl);
int dfc_data_alloc(elxHBA_t *, struct dfc_mem *dm, uint32_t size);
int dfc_data_free(elxHBA_t *, struct dfc_mem *dm);
uint64_t dfc_getLunId(MBUF_INFO_t * bfrnfo, ELXSCSILUN_t * lun,
		      uint64_t lunidx);
int lpfc_issue_rptlun(elxHBA_t * phba, MBUF_INFO_t * pbfrnfo,
		      ELXSCSITARGET_t * pscznod);
int lpfc_reset_dev_q_depth(elxHBA_t * phba);

int
lpfc_ioctl_port_attrib(elxHBA_t * phba, LPFCHBA_t * plhba, struct dfc_mem *dm)
{
	elx_vpd_t *vp;
	SERV_PARM *hsp;
	HBA_PORTATTRIBUTES *hp;
	HBA_OSDN *osdn;
	elxCfgParam_t *clp = &phba->config[0];
	uint32_t cnt;
	int rc = 0;

	vp = &phba->vpd;
	hsp = (SERV_PARM *) (&plhba->fc_sparam);
	hp = (HBA_PORTATTRIBUTES *) dm->fc_dataout;
	memset(dm->fc_dataout, 0, (sizeof (HBA_PORTATTRIBUTES)));
	memcpy((uint8_t *) & hp->NodeWWN,
	       (uint8_t *) & plhba->fc_sparam.nodeName, sizeof (HBA_WWN));
	memcpy((uint8_t *) & hp->PortWWN,
	       (uint8_t *) & plhba->fc_sparam.portName, sizeof (HBA_WWN));

	if (plhba->fc_linkspeed == LA_2GHZ_LINK)
		hp->PortSpeed = HBA_PORTSPEED_2GBIT;
	else
		hp->PortSpeed = HBA_PORTSPEED_1GBIT;

	if (FC_JEDEC_ID(vp->rev.biuRev) == VIPER_JEDEC_ID)
		hp->PortSupportedSpeed = HBA_PORTSPEED_10GBIT;
	else if ((FC_JEDEC_ID(vp->rev.biuRev) == CENTAUR_2G_JEDEC_ID) ||
		 (FC_JEDEC_ID(vp->rev.biuRev) == PEGASUS_JEDEC_ID) ||
		 (FC_JEDEC_ID(vp->rev.biuRev) == THOR_JEDEC_ID))
		hp->PortSupportedSpeed = HBA_PORTSPEED_2GBIT;
	else
		hp->PortSupportedSpeed = HBA_PORTSPEED_1GBIT;

	hp->PortFcId = plhba->fc_myDID;
	hp->PortType = HBA_PORTTYPE_UNKNOWN;
	if (plhba->fc_topology == TOPOLOGY_LOOP) {
		if (plhba->fc_flag & FC_PUBLIC_LOOP) {
			hp->PortType = HBA_PORTTYPE_NLPORT;
			memcpy((uint8_t *) & hp->FabricName,
			       (uint8_t *) & plhba->fc_fabparam.nodeName,
			       sizeof (HBA_WWN));
		} else {
			hp->PortType = HBA_PORTTYPE_LPORT;
		}
	} else {
		if (plhba->fc_flag & FC_FABRIC) {
			hp->PortType = HBA_PORTTYPE_NPORT;
			memcpy((uint8_t *) & hp->FabricName,
			       (uint8_t *) & plhba->fc_fabparam.nodeName,
			       sizeof (HBA_WWN));
		} else {
			hp->PortType = HBA_PORTTYPE_PTP;
		}
	}

	if (plhba->fc_flag & FC_BYPASSED_MODE) {
		hp->PortState = HBA_PORTSTATE_BYPASSED;
	} else if (plhba->fc_flag & FC_OFFLINE_MODE) {
		hp->PortState = HBA_PORTSTATE_DIAGNOSTICS;
	} else {
		switch (phba->hba_state) {
		case ELX_INIT_START:
		case ELX_INIT_MBX_CMDS:
			hp->PortState = HBA_PORTSTATE_UNKNOWN;
			break;

		case ELX_LINK_DOWN:
		case ELX_LINK_UP:
		case ELX_LOCAL_CFG_LINK:
		case ELX_FLOGI:
		case ELX_FABRIC_CFG_LINK:
		case ELX_NS_REG:
		case ELX_NS_QRY:
		case ELX_BUILD_DISC_LIST:
		case ELX_DISC_AUTH:
		case ELX_CLEAR_LA:
			hp->PortState = HBA_PORTSTATE_LINKDOWN;
			break;

		case ELX_HBA_READY:
			hp->PortState = HBA_PORTSTATE_ONLINE;
			break;

		case ELX_HBA_ERROR:
		default:
			hp->PortState = HBA_PORTSTATE_ERROR;
			break;
		}
	}

	cnt = plhba->fc_map_cnt + plhba->fc_unmap_cnt;
	hp->NumberofDiscoveredPorts = cnt;
	if (hsp->cls1.classValid) {
		hp->PortSupportedClassofService |= 1;	/* bit 1 */
	}

	if (hsp->cls2.classValid) {
		hp->PortSupportedClassofService |= 2;	/* bit 2 */
	}

	if (hsp->cls3.classValid) {
		hp->PortSupportedClassofService |= 4;	/* bit 3 */
	}

	hp->PortMaxFrameSize = (((uint32_t) hsp->cmn.bbRcvSizeMsb) << 8) |
	    (uint32_t) hsp->cmn.bbRcvSizeLsb;

	hp->PortSupportedFc4Types.bits[2] = 0x1;
	hp->PortSupportedFc4Types.bits[3] = 0x20;
	hp->PortSupportedFc4Types.bits[7] = 0x1;
	hp->PortActiveFc4Types.bits[2] = 0x1;

	if (clp[LPFC_CFG_NETWORK_ON].a_current) {
		hp->PortActiveFc4Types.bits[3] = 0x20;
	}

	hp->PortActiveFc4Types.bits[7] = 0x1;

	/* OSDeviceName is the device info filled into the HBA_OSDN structure */
	osdn = (HBA_OSDN *) & hp->OSDeviceName[0];
	memcpy(osdn->drvname, "lpfc", 4);
	osdn->instance = lpfc_instance[phba->brd_no];
	osdn->target = (uint32_t) (-1);
	osdn->lun = (uint32_t) (-1);

	return rc;
}

int
lpfc_ioctl_found_port(elxHBA_t * phba,
		      LPFCHBA_t * plhba,
		      LPFC_NODELIST_t * pndl,
		      struct dfc_mem *dm,
		      MAILBOX_t * pmbox, HBA_PORTATTRIBUTES * hp)
{
	ELX_SLI_t *psli = &phba->sli;
	SERV_PARM *hsp;
	DMABUF_t *mp;
	HBA_OSDN *osdn;
	ELX_MBOXQ_t *mboxq;
	unsigned long iflag;
	int mbxstatus;
	int rc = 0;

	/* Check if its the local port */
	if (plhba->fc_myDID == pndl->nlp_DID) {
		/* handle localport */
		rc = lpfc_ioctl_port_attrib(phba, plhba, dm);
		return rc;
	}

	memset((void *)pmbox, 0, sizeof (MAILBOX_t));
	pmbox->un.varRdRPI.reqRpi = (volatile uint16_t)pndl->nle.nlp_rpi;
	pmbox->mbxCommand = MBX_READ_RPI64;
	pmbox->mbxOwner = OWN_HOST;
	if ((mp = (DMABUF_t *) elx_mem_get(phba, MEM_BUF)) == 0) {
		return ENOMEM;
	}

	if ((mboxq =
	     (ELX_MBOXQ_t *) elx_mem_get(phba, MEM_MBOX | MEM_PRI)) == 0) {
		elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
		return ENOMEM;
	}

	hsp = (SERV_PARM *) mp->virt;
	if (psli->sliinit.sli_flag & ELX_SLI2_ACTIVE) {
		pmbox->un.varRdRPI.un.sp64.addrHigh = putPaddrHigh(mp->phys);
		pmbox->un.varRdRPI.un.sp64.addrLow = putPaddrLow(mp->phys);
		pmbox->un.varRdRPI.un.sp64.tus.f.bdeSize = sizeof (SERV_PARM);
	} else {
		pmbox->un.varRdRPI.un.sp.bdeAddress = putPaddrLow(mp->phys);
		pmbox->un.varRdRPI.un.sp.bdeSize = sizeof (SERV_PARM);
	}

	memset((void *)mboxq, 0, sizeof (ELX_MBOXQ_t));
	mboxq->mb.mbxCommand = pmbox->mbxCommand;
	mboxq->mb.mbxOwner = pmbox->mbxOwner;
	mboxq->mb.un = pmbox->un;
	mboxq->mb.us = pmbox->us;
	mboxq->context1 = (uint8_t *) 0;

	if (plhba->fc_flag & FC_OFFLINE_MODE) {
		ELX_DRVR_UNLOCK(phba, iflag);
		mbxstatus = elx_sli_issue_mbox(phba, mboxq, MBX_POLL);
		ELX_DRVR_LOCK(phba, iflag);
	} else
		mbxstatus =
		    elx_sli_issue_mbox_wait(phba, mboxq, plhba->fc_ratov * 2);

	if (mbxstatus != MBX_SUCCESS) {
		if (mbxstatus == MBX_TIMEOUT) {
			/*
			 * Let SLI layer to release mboxq if mbox command completed after timeout.
			 */
			mboxq->mbox_cmpl = 0;
		} else {
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) mboxq);
		}
		return ENODEV;
	}

	pmbox->mbxCommand = mboxq->mb.mbxCommand;
	pmbox->mbxOwner = mboxq->mb.mbxOwner;
	pmbox->un = mboxq->mb.un;
	pmbox->us = mboxq->mb.us;

	if (hsp->cls1.classValid) {
		hp->PortSupportedClassofService |= 1;	/* bit 1 */
	}
	if (hsp->cls2.classValid) {
		hp->PortSupportedClassofService |= 2;	/* bit 2 */
	}
	if (hsp->cls3.classValid) {
		hp->PortSupportedClassofService |= 4;	/* bit 3 */
	}

	hp->PortMaxFrameSize = (((uint32_t) hsp->cmn.bbRcvSizeMsb) << 8) |
	    (uint32_t) hsp->cmn.bbRcvSizeLsb;

	elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
	elx_mem_put(phba, MEM_MBOX, (uint8_t *) mboxq);

	memcpy((uint8_t *) & hp->NodeWWN, (uint8_t *) & pndl->nlp_nodename,
	       sizeof (HBA_WWN));
	memcpy((uint8_t *) & hp->PortWWN, (uint8_t *) & pndl->nlp_portname,
	       sizeof (HBA_WWN));
	hp->PortSpeed = 0;
	/* We only know the speed if the device is on the same loop as us */
	if (((plhba->fc_myDID & 0xffff00) == (pndl->nlp_DID & 0xffff00)) &&
	    (plhba->fc_topology == TOPOLOGY_LOOP)) {
		if (plhba->fc_linkspeed == LA_2GHZ_LINK)
			hp->PortSpeed = HBA_PORTSPEED_2GBIT;
		else
			hp->PortSpeed = HBA_PORTSPEED_1GBIT;
	}

	hp->PortFcId = pndl->nlp_DID;
	if ((plhba->fc_flag & FC_FABRIC) &&
	    ((plhba->fc_myDID & 0xff0000) == (pndl->nlp_DID & 0xff0000))) {
		/* If remote node is in the same domain we are in */
		memcpy((uint8_t *) & hp->FabricName,
		       (uint8_t *) & plhba->fc_fabparam.nodeName,
		       sizeof (HBA_WWN));
	}
	hp->PortState = HBA_PORTSTATE_ONLINE;
	if (pndl->nlp_flag & NLP_FCP_TARGET) {
		hp->PortActiveFc4Types.bits[2] = 0x1;
	}
	if (pndl->nlp_flag & NLP_IP_NODE) {
		hp->PortActiveFc4Types.bits[3] = 0x20;
	}
	hp->PortActiveFc4Types.bits[7] = 0x1;

	hp->PortType = HBA_PORTTYPE_UNKNOWN;
	if (plhba->fc_topology == TOPOLOGY_LOOP) {
		if (plhba->fc_flag & FC_PUBLIC_LOOP) {
			/* Check if Fabric port */
			if (lpfc_geportname(&pndl->nlp_nodename,
					    (NAME_TYPE *) & (plhba->fc_fabparam.
							     nodeName)) == 2) {
				hp->PortType = HBA_PORTTYPE_FLPORT;
			} else {
				/* Based on DID */
				if ((pndl->nlp_DID & 0xff) == 0) {
					hp->PortType = HBA_PORTTYPE_NPORT;
				} else {
					if ((pndl->nlp_DID & 0xff0000) !=
					    0xff0000) {
						hp->PortType =
						    HBA_PORTTYPE_NLPORT;
					}
				}
			}
		} else {
			hp->PortType = HBA_PORTTYPE_LPORT;
		}
	} else {
		if (plhba->fc_flag & FC_FABRIC) {
			/* Check if Fabric port */
			if (lpfc_geportname(&pndl->nlp_nodename,
					    (NAME_TYPE *) & (plhba->fc_fabparam.
							     nodeName)) == 2) {
				hp->PortType = HBA_PORTTYPE_FPORT;
			} else {
				/* Based on DID */
				if ((pndl->nlp_DID & 0xff) == 0) {
					hp->PortType = HBA_PORTTYPE_NPORT;
				} else {
					if ((pndl->nlp_DID & 0xff0000) !=
					    0xff0000) {
						hp->PortType =
						    HBA_PORTTYPE_NLPORT;
					}
				}
			}
		} else {
			hp->PortType = HBA_PORTTYPE_PTP;
		}
	}

	/* for mapped devices OSDeviceName is device info filled into HBA_OSDN 
	 * structure */
	if (pndl->nlp_flag & NLP_MAPPED_LIST) {
		osdn = (HBA_OSDN *) & hp->OSDeviceName[0];
		memcpy(osdn->drvname, "lpfc", 4);
		osdn->instance = lpfc_instance[phba->brd_no];
		osdn->target = FC_SCSID(pndl->nlp_pan, pndl->nlp_sid);
		osdn->lun = (uint32_t) (-1);
	}

	return rc;
}

int
lpfc_ioctl_write_pci(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	uint32_t offset, cnt;
	LPFCHBA_t *plhba;
	int rc = 0;
	unsigned long iflag;
	uint8_t *buffer;
	struct dfc_mem dm_buf;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	offset = (uint32_t) ((ulong) cip->elx_arg1);
	cnt = (uint32_t) ((ulong) cip->elx_arg2);

	if (!(plhba->fc_flag & FC_OFFLINE_MODE)) {
		rc = EPERM;
		return (rc);
	}

	if ((cnt + offset) > 256) {
		rc = ERANGE;
		return (rc);
	}

	dm_buf.fc_dataout = NULL;
	if ((rc = dfc_data_alloc(phba, &dm_buf, 4096))) {
		return (rc);
	}
	buffer = (uint8_t *) dm_buf.fc_dataout;

	ELX_DRVR_UNLOCK(phba, iflag);
	if (copy_from_user((uint8_t *) buffer, (uint8_t *) cip->elx_dataout,
			   cnt)) {
		ELX_DRVR_LOCK(phba, iflag);
		rc = EIO;
		dfc_data_free(phba, &dm_buf);
		return (rc);
	}
	ELX_DRVR_LOCK(phba, iflag);
	elx_cnt_write_pci(phba, offset, cnt, (uint32_t *) buffer);
	dfc_data_free(phba, &dm_buf);
	return (rc);
}

int
lpfc_ioctl_read_pci(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	uint32_t offset, cnt;
	int rc = 0;

	offset = (uint32_t) ((ulong) cip->elx_arg1);
	cnt = (uint32_t) ((ulong) cip->elx_arg2);
	if ((cnt + offset) > 256) {
		rc = ERANGE;
		return (rc);
	}

	elx_cnt_read_pci(phba, offset, cnt, (uint32_t *) dm->fc_dataout);

	return (rc);
}

int
lpfc_ioctl_write_mem(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	uint32_t offset, cnt;
	LPFCHBA_t *plhba;
	ELX_SLI_t *psli;
	int rc = 0;
	unsigned long iflag;
	struct dfc_mem dm_buf;
	uint8_t *buffer;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;
	offset = (uint32_t) ((ulong) cip->elx_arg1);
	cnt = (uint32_t) ((ulong) cip->elx_arg2);

	if (!(plhba->fc_flag & FC_OFFLINE_MODE)) {
		if (offset != 256) {
			rc = EPERM;
			return (rc);
		}
		/* Allow writing of first 128 bytes after mailbox in online mode */
		if (cnt > 128) {
			rc = EPERM;
			return (rc);
		}
	}
	if (offset >= 4096) {
		rc = ERANGE;
		return (rc);
	}
	cnt = (uint32_t) ((ulong) cip->elx_arg2);
	if ((cnt + offset) > 4096) {
		rc = ERANGE;
		return (rc);
	}

	dm_buf.fc_dataout = NULL;

	if ((rc = dfc_data_alloc(phba, &dm_buf, 4096))) {
		return (rc);
	} else {
		buffer = (uint8_t *) dm_buf.fc_dataout;
	}

	ELX_DRVR_UNLOCK(phba, iflag);
	if (copy_from_user((uint8_t *) buffer, (uint8_t *) cip->elx_dataout,
			   (ulong) cnt)) {
		rc = EIO;
		ELX_DRVR_LOCK(phba, iflag);
		dfc_data_free(phba, &dm_buf);
		return (rc);
	}
	ELX_DRVR_LOCK(phba, iflag);

	if (psli->sliinit.sli_flag & ELX_SLI2_ACTIVE) {
		/* copy into SLIM2 */
		elx_sli_pcimem_bcopy((uint32_t *) buffer,
				     ((uint32_t *) phba->slim2p.virt + offset),
				     cnt >> 2);
	} else {
		/* First copy command data */
		(psli->sliinit.elx_sli_write_slim) ((void *)phba,
						    (void *)buffer, 0, cnt);
	}

	dfc_data_free(phba, &dm_buf);
	return (rc);
}

int
lpfc_ioctl_read_mem(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	uint32_t offset, cnt;
	LPFCHBA_t *plhba;
	ELX_SLI_t *psli;
	int i, rc = 0;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;
	offset = (uint32_t) ((ulong) cip->elx_arg1);
	cnt = (uint32_t) ((ulong) cip->elx_arg2);

	if (psli->sliinit.sli_flag & ELX_SLI2_ACTIVE) {
		/* The SLIM2 size is stored in the next field */
		i = (uint32_t) (unsigned long)phba->slim2p.next;
	} else {
		i = 4096;
	}

	if (offset >= i) {
		rc = ERANGE;
		return (rc);
	}

	if ((cnt + offset) > i) {
		/* Adjust cnt instead of error ret */
		cnt = (i - offset);
	}

	if (psli->sliinit.sli_flag & ELX_SLI2_ACTIVE) {
		/* copy results back to user */
		elx_sli_pcimem_bcopy((uint32_t *) psli->MBhostaddr,
				     (uint32_t *) dm->fc_dataout, cnt);
	} else {
		/* First copy command data */
		(psli->sliinit.elx_sli_read_slim) ((void *)phba,
						   (void *)dm->fc_dataout, 0,
						   cnt);
	}
	return (rc);
}

int
lpfc_ioctl_write_ctlreg(elxHBA_t * phba,
			ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	uint32_t offset, incr;
	LPFCHBA_t *plhba;
	ELX_SLI_t *psli;
	int rc = 0;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;
	offset = (uint32_t) ((ulong) cip->elx_arg1);
	incr = (uint32_t) ((ulong) cip->elx_arg2);

	if (!(plhba->fc_flag & FC_OFFLINE_MODE)) {
		rc = EPERM;
		return (rc);
	}

	if (offset > 255) {
		rc = ERANGE;
		return (rc);
	}

	if (offset % 4) {
		rc = EINVAL;
		return (rc);
	}

	lpfc_write_hbaregs_plus_offset(phba, offset, incr);

	return (rc);
}

int
lpfc_ioctl_read_ctlreg(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	uint32_t offset, incr;
	int rc = 0;

	offset = (uint32_t) ((ulong) cip->elx_arg1);

	if (offset > 255) {
		rc = ERANGE;
		return (rc);
	}

	if (offset % 4) {
		rc = EINVAL;
		return (rc);
	}

	incr = lpfc_read_hbaregs_plus_offset(phba, offset);
	*((uint32_t *) dm->fc_dataout) = incr;

	return (rc);
}

int
lpfc_ioctl_setdiag(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	uint32_t offset;
	LPFCHBA_t *plhba;
	int rc = 0;

	offset = (uint32_t) ((ulong) cip->elx_arg1);
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	switch (offset) {
	case DDI_ONDI:
		rc = ENXIO;
		break;

	case DDI_OFFDI:
		rc = ENXIO;
		break;

	case DDI_SHOW:
		rc = ENXIO;
		break;

	case DDI_BRD_ONDI:
		if (plhba->fc_flag & FC_OFFLINE_MODE) {
			lpfc_online(phba);
		}
		*((uint32_t *) (dm->fc_dataout)) = DDI_ONDI;
		break;

	case DDI_BRD_OFFDI:
		if (!(plhba->fc_flag & FC_OFFLINE_MODE)) {
			lpfc_offline(phba);
		}
		*((uint32_t *) (dm->fc_dataout)) = DDI_OFFDI;
		break;

	case DDI_BRD_SHOW:
		if (plhba->fc_flag & FC_OFFLINE_MODE) {
			*((uint32_t *) (dm->fc_dataout)) = DDI_OFFDI;
		} else {
			*((uint32_t *) (dm->fc_dataout)) = DDI_ONDI;
		}
		break;

	default:
		rc = ERANGE;
		break;
	}

	return (rc);
}

int
lpfc_ioctl_lip(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	LPFCHBA_t *plhba;
	ELX_SLI_t *psli;
	ELX_SLI_RING_t *pring;
	elxCfgParam_t *clp;
	ELX_MBOXQ_t *pmboxq;
	int mbxstatus;
	int i, rc;
	unsigned long iflag;

	clp = &phba->config[0];
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;

	rc = 0;

	if ((pmboxq =
	     (ELX_MBOXQ_t *) elx_mem_get(phba, MEM_MBOX | MEM_PRI)) == 0) {
		return ENOMEM;
	}

	mbxstatus = MBXERR_ERROR;
	if (phba->hba_state == ELX_HBA_READY) {
		/* The HBA is reporting ready.  Pause the scheduler so that
		 * all outstanding I/Os complete before LIPing.
		 */
		elx_sched_pause_hba(phba);

		i = 0;
		pring = &psli->ring[psli->fcp_ring];
		while (pring->txcmplq.q_cnt) {
			if (i++ > 500) {	/* wait up to 5 seconds */
				break;
			}

			ELX_DRVR_UNLOCK(phba, iflag);
			mdelay(10);
			ELX_DRVR_LOCK(phba, iflag);
		}
		memset((void *)pmboxq, 0, sizeof (ELX_MBOXQ_t));
		lpfc_init_link(phba, pmboxq, clp[LPFC_CFG_TOPOLOGY].a_current,
			       clp[LPFC_CFG_LINK_SPEED].a_current);

		mbxstatus =
		    elx_sli_issue_mbox_wait(phba, pmboxq, plhba->fc_ratov * 2);
		if (mbxstatus == MBX_TIMEOUT) {
			/*
			 * Let SLI layer to release mboxq if mbox command completed after timeout.
			 */
			pmboxq->mbox_cmpl = 0;
		} else {
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmboxq);
		}
		elx_sched_continue_hba(phba);
	}
	memcpy(dm->fc_dataout, (char *)&mbxstatus, sizeof (uint16_t));

	return (rc);
}

int
lpfc_ioctl_outfcpio(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	ELXSCSILUN_t *lunp;
	ELXSCSITARGET_t *targetp;
	ELX_SCSI_BUF_t *elx_cmd;
	ELX_SLI_RING_t *pring;
	ELX_IOCBQ_t *iocb;
	ELX_IOCBQ_t *next_iocb;
	IOCB_t *cmd;
	uint32_t tgt, lun;
	struct out_fcp_devp *dp;
	int max;
	unsigned long iflag;
	LPFCHBA_t *plhba;
	ELX_SLI_t *psli;
	int rc = 0;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;
	pring = &psli->ring[psli->fcp_ring];

	memcpy(dm->fc_dataout, (char *)psli, sizeof (ELX_SLI_t));
	psli = (ELX_SLI_t *) dm->fc_dataout;

	/* Use sliIntr to count number of out_fcp_devp entries */
	psli->slistat.sliIntr = 0;
	dp = (struct out_fcp_devp *)(psli + 1);
	max = cip->elx_outsz - sizeof (ELX_SLI_t);
	max = (max / sizeof (struct out_fcp_devp));

	for (tgt = 0; tgt < MAX_FCP_TARGET; tgt++) {
		if ((targetp = plhba->device_queue_hash[tgt])) {
			lunp = (ELXSCSILUN_t *) targetp->lunlist.q_first;
			while (lunp) {
				lun = lunp->lun_id;
				if (psli->slistat.sliIntr++ >= max)
					goto outio;

				dp->target = tgt;
				dp->lun = (ushort) lun;

				dp->qfullcnt = lunp->qfullcnt;
				dp->qcmdcnt = lunp->qcmdcnt;
				dp->iodonecnt = lunp->iodonecnt;
				dp->errorcnt = lunp->errorcnt;

				dp->tx_count = 0;
				dp->txcmpl_count = 0;
				dp->delay_count = 0;
				dp->sched_count =
				    lunp->lunSched.commandList.q_cnt;
				dp->lun_qdepth = lunp->lunSched.maxOutstanding;
				dp->current_qdepth =
				    lunp->lunSched.currentOutstanding;

				ELX_SLI_LOCK(phba, iflag);

				/* Error matching iocb on txq or txcmplq 
				 * First check the txq.
				 */
				next_iocb = (ELX_IOCBQ_t *) pring->txq.q_f;
				while (next_iocb !=
				       (ELX_IOCBQ_t *) & pring->txq) {
					iocb = next_iocb;
					next_iocb = next_iocb->q_f;
					cmd = &iocb->iocb;

					/* Must be a FCP command */
					if ((cmd->ulpCommand !=
					     CMD_FCP_ICMND64_CR)
					    && (cmd->ulpCommand !=
						CMD_FCP_IWRITE64_CR)
					    && (cmd->ulpCommand !=
						CMD_FCP_IREAD64_CR)) {
						continue;
					}

					/* context1 MUST be a ELX_SCSI_BUF_t */
					elx_cmd =
					    (ELX_SCSI_BUF_t *) (iocb->context1);
					if ((elx_cmd == 0)
					    || (elx_cmd->scsi_target != tgt)
					    || (elx_cmd->scsi_lun != lun)) {
						continue;
					}
					dp->tx_count++;
				}

				/* Next check the txcmplq */
				next_iocb = (ELX_IOCBQ_t *) pring->txcmplq.q_f;
				while (next_iocb !=
				       (ELX_IOCBQ_t *) & pring->txcmplq) {
					iocb = next_iocb;
					next_iocb = next_iocb->q_f;
					cmd = &iocb->iocb;

					/* Must be a FCP command */
					if ((cmd->ulpCommand !=
					     CMD_FCP_ICMND64_CR)
					    && (cmd->ulpCommand !=
						CMD_FCP_IWRITE64_CR)
					    && (cmd->ulpCommand !=
						CMD_FCP_IREAD64_CR)) {
						continue;
					}

					/* context1 MUST be a ELX_SCSI_BUF_t */
					elx_cmd =
					    (ELX_SCSI_BUF_t *) (iocb->context1);
					if ((elx_cmd == 0)
					    || (elx_cmd->scsi_target != tgt)
					    || (elx_cmd->scsi_lun != lun)) {
						continue;
					}

					dp->txcmpl_count++;
				}
				ELX_SLI_UNLOCK(phba, iflag);
				dp++;

				lunp = lunp->pnextLun;
			}
		}
	}
      outio:
	cip->elx_outsz = (sizeof (ELX_SLI_t) +
			  (psli->slistat.sliIntr *
			   sizeof (struct out_fcp_devp)));

	return (rc);
}

int
lpfc_ioctl_send_scsi_fcp(elxHBA_t * phba,
			 ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{

	LPFCHBA_t *plhba;
	ELX_SLI_t *psli = &phba->sli;
	elxCfgParam_t *clp;
	int reqbfrcnt;
	int snsbfrcnt;
	int j = 0;
	HBA_WWN wwpn;
	FCP_CMND *fcpcmd;
	FCP_RSP *fcprsp;
	ULP_BDE64 *bpl;
	LPFC_NODELIST_t *pndl;
	ELX_SLI_RING_t *pring = &psli->ring[LPFC_FCP_RING];
	ELX_IOCBQ_t *cmdiocbq = 0;
	ELX_IOCBQ_t *rspiocbq = 0;
	DMABUFEXT_t *outdmp = 0;
	IOCB_t *cmd = 0;
	IOCB_t *rsp = 0;
	DMABUF_t *mp = 0;
	DMABUF_t *bmp = 0;
	int i0;
	char *outdta;
	uint32_t clear_count;
	int rc = 0;
	unsigned long iflag;

	struct {
		/* this rspcnt is really data buffer size */
		uint32_t rspcnt;
		/* this is sense count in case of LPFC_HBA_SEND_SCSI.
		 * It is fcp response size in case of LPFC_HBA_SEND_FCP
		 */
		uint32_t snscnt;
	} count;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	clp = &phba->config[0];

    /************************************************************************/

    /************************************************************************/
	reqbfrcnt = cip->elx_arg4;
	snsbfrcnt = cip->elx_flag;
	if ((reqbfrcnt + cip->elx_outsz) > (80 * 4096)) {
		/* lpfc_ioctl:error <idx> */
		elx_printf_log(phba->brd_no, &elx_msgBlk1604,	/* ptr to msg structure */
			       elx_mes1604,	/* ptr to msg */
			       elx_msgBlk1604.msgPreambleStr,	/* begin varargs */
			       0);	/* end varargs */
		rc = ERANGE;
		goto sndsczout;
	}

	ELX_DRVR_UNLOCK(phba, iflag);
	if (copy_from_user((uint8_t *) & wwpn, (uint8_t *) cip->elx_arg3,
			   (ulong) (sizeof (HBA_WWN)))) {
		rc = EIO;
		ELX_DRVR_LOCK(phba, iflag);
		goto sndsczout;
	}
	ELX_DRVR_LOCK(phba, iflag);

	pndl =
	    lpfc_findnode_wwpn(phba, NLP_SEARCH_MAPPED, (NAME_TYPE *) & wwpn);
	if (!pndl) {
		if (!(pndl = lpfc_findnode_wwpn(phba, NLP_SEARCH_UNMAPPED,
						(NAME_TYPE *) & wwpn))
		    || !(pndl->nlp_flag & NLP_TGT_NO_SCSIID)) {
			pndl = (LPFC_NODELIST_t *) 0;
		}
	}
	if (!pndl || !(psli->sliinit.sli_flag & ELX_SLI2_ACTIVE)) {
		rc = EACCES;
		goto sndsczout;
	}
	if (pndl->nlp_flag & NLP_ELS_SND_MASK) {
		rc = ENODEV;
		goto sndsczout;
	}
	/* Allocate buffer for command iocb */
	if ((cmdiocbq =
	     (ELX_IOCBQ_t *) elx_mem_get(phba, MEM_IOCB | MEM_PRI)) == 0) {
		rc = ENOMEM;
		goto sndsczout;
	}
	memset((void *)cmdiocbq, 0, sizeof (ELX_IOCBQ_t));
	cmd = &(cmdiocbq->iocb);

	/* Allocate buffer for response iocb */
	if ((rspiocbq =
	     (ELX_IOCBQ_t *) elx_mem_get(phba, MEM_IOCB | MEM_PRI)) == 0) {
		rc = ENOMEM;
		goto sndsczout;
	}
	memset((void *)rspiocbq, 0, sizeof (ELX_IOCBQ_t));
	rsp = &(rspiocbq->iocb);

	/* Allocate buffer for Buffer ptr list */
	if ((bmp = (DMABUF_t *) elx_mem_get(phba, MEM_BPL)) == 0) {
		rc = ENOMEM;
		goto sndsczout;
	}
	bpl = (ULP_BDE64 *) bmp->virt;

	/* Allocate buffer for FCP CMND / FCP RSP */
	if ((mp = (DMABUF_t *) elx_mem_get(phba, MEM_BUF | MEM_PRI)) == 0) {
		rc = ENOMEM;
		goto sndsczout;
	}
	fcpcmd = (FCP_CMND *) mp->virt;
	fcprsp = (FCP_RSP *) ((uint8_t *) mp->virt + sizeof (FCP_CMND));
	memset((void *)fcpcmd, 0, sizeof (FCP_CMND) + sizeof (FCP_RSP));

	/* Setup FCP CMND and FCP RSP */
	bpl->addrHigh = PCIMEM_LONG(putPaddrHigh(mp->phys));
	bpl->addrLow = PCIMEM_LONG(putPaddrLow(mp->phys));
	bpl->tus.f.bdeSize = sizeof (FCP_CMND);
	bpl->tus.f.bdeFlags = BUFF_USE_CMND;
	bpl->tus.w = PCIMEM_LONG(bpl->tus.w);
	bpl++;
	bpl->addrHigh = PCIMEM_LONG(putPaddrHigh(mp->phys + sizeof (FCP_CMND)));
	bpl->addrLow = PCIMEM_LONG(putPaddrLow(mp->phys + sizeof (FCP_CMND)));
	bpl->tus.f.bdeSize = sizeof (FCP_RSP);
	bpl->tus.f.bdeFlags = (BUFF_USE_CMND | BUFF_USE_RCV);
	bpl->tus.w = PCIMEM_LONG(bpl->tus.w);
	bpl++;

    /************************************************************************/
	/* Copy user data into fcpcmd buffer at this point to see if its a read */
	/*  or a write.                                                         */
    /************************************************************************/
	ELX_DRVR_UNLOCK(phba, iflag);
	if (copy_from_user((uint8_t *) fcpcmd, (uint8_t *) cip->elx_arg1,
			   (ulong) (reqbfrcnt))) {
		rc = EIO;
		ELX_DRVR_LOCK(phba, iflag);
		goto sndsczout;
	}
	ELX_DRVR_LOCK(phba, iflag);

	outdta = (fcpcmd->fcpCntl3 == WRITE_DATA ? cip->elx_dataout : 0);

	/* Allocate data buffer, and fill it if its a write */
	if (cip->elx_outsz == 0) {
		outdmp = dfc_cmd_data_alloc(phba, outdta, bpl, 512);
	} else {
		outdmp = dfc_cmd_data_alloc(phba, outdta, bpl, cip->elx_outsz);
	}
	if (outdmp == 0) {
		rc = ENOMEM;
		goto sndsczout;
	}

	cmd->un.fcpi64.bdl.ulpIoTag32 = 0;
	cmd->un.fcpi64.bdl.addrHigh = putPaddrHigh(bmp->phys);
	cmd->un.fcpi64.bdl.addrLow = putPaddrLow(bmp->phys);
	cmd->un.fcpi64.bdl.bdeSize = (3 * sizeof (ULP_BDE64));
	cmd->un.fcpi64.bdl.bdeFlags = BUFF_TYPE_BDL;
	cmd->ulpBdeCount = 1;
	cmd->ulpContext = pndl->nle.nlp_rpi;
	cmd->ulpIoTag = elx_sli_next_iotag(phba, pring);
	cmd->ulpClass = pndl->nle.nlp_fcp_info & 0x0f;
	cmd->ulpOwner = OWN_CHIP;
	cmd->ulpTimeout =
	    clp[LPFC_CFG_SCSI_REQ_TMO].a_current + phba->fcp_timeout_offset;
	cmd->ulpLe = 1;
	if (pndl->nle.nlp_fcp_info & NLP_FCP_2_DEVICE) {
		cmd->ulpFCP2Rcvy = 1;
	}
	switch (fcpcmd->fcpCntl3) {
	case READ_DATA:	/* Set up for SCSI read */
		cmd->ulpCommand = CMD_FCP_IREAD64_CR;
		cmd->ulpPU = PARM_READ_CHECK;
		cmd->un.fcpi.fcpi_parm = cip->elx_outsz;
		cmd->un.fcpi64.bdl.bdeSize =
		    ((outdmp->flag + 2) * sizeof (ULP_BDE64));
		break;
	case WRITE_DATA:	/* Set up for SCSI write */
		cmd->ulpCommand = CMD_FCP_IWRITE64_CR;
		cmd->un.fcpi64.bdl.bdeSize =
		    ((outdmp->flag + 2) * sizeof (ULP_BDE64));
		break;
	default:		/* Set up for SCSI command */
		cmd->ulpCommand = CMD_FCP_ICMND64_CR;
		cmd->un.fcpi64.bdl.bdeSize = (2 * sizeof (ULP_BDE64));
		break;
	}
	cmdiocbq->context1 = (uint8_t *) 0;
	cmdiocbq->iocb_flag |= ELX_IO_IOCTL;

    /************************************************************************/
	/* send scsi command, retry 3 times on getting IOCB_BUSY, or            */
	/*  or IOCB_TIMEOUT frm issue_iocb                                      */
    /************************************************************************/
	for (rc = -1, i0 = 0; i0 < 4 && rc != IOCB_SUCCESS; i0++) {
		rc = elx_sli_issue_iocb_wait(phba, pring, cmdiocbq,
					     SLI_IOCB_USE_TXQ, rspiocbq,
					     clp[LPFC_CFG_SCSI_REQ_TMO].
					     a_current +
					     phba->fcp_timeout_offset +
					     ELX_DRVR_TIMEOUT);
		if (rc == IOCB_ERROR) {
			rc = EACCES;
			break;
		}
	}

	if (rc != IOCB_SUCCESS) {
		rc = EACCES;
		goto sndsczout;
	}

	/* For LPFC_HBA_SEND_FCP, just return FCP_RSP unless we got
	 * an IOSTAT_LOCAL_REJECT.
	 *
	 * For SEND_FCP case, snscnt is really FCP_RSP length. In the
	 * switch statement below, the snscnt should not get destroyed.
	 */
	if (cmd->ulpCommand == CMD_FCP_IWRITE64_CX) {
		clear_count = (rsp->ulpStatus == IOSTAT_SUCCESS ? 1 : 0);
	} else {
		clear_count = cmd->un.fcpi.fcpi_parm;
	}
	if ((cip->elx_cmd == LPFC_HBA_SEND_FCP) &&
	    (rsp->ulpStatus != IOSTAT_LOCAL_REJECT)) {
		if (snsbfrcnt < sizeof (FCP_RSP)) {
			count.snscnt = snsbfrcnt;
		} else {
			count.snscnt = sizeof (FCP_RSP);
		}
		ELX_DRVR_UNLOCK(phba, iflag);
		if (copy_to_user((uint8_t *) cip->elx_arg2, (uint8_t *) fcprsp,
				 count.snscnt)) {
			rc = EIO;
			ELX_DRVR_LOCK(phba, iflag);
			goto sndsczout;
		}
		ELX_DRVR_LOCK(phba, iflag);
	}
	switch (rsp->ulpStatus) {
	case IOSTAT_SUCCESS:
	      cpdata:
		if (cip->elx_outsz < clear_count) {
			cip->elx_outsz = 0;
			rc = ERANGE;
			break;
		}
		cip->elx_outsz = clear_count;
		if (cip->elx_cmd == LPFC_HBA_SEND_SCSI) {
			count.rspcnt = cip->elx_outsz;
			count.snscnt = 0;
		} else {	/* For LPFC_HBA_SEND_FCP, snscnt is already set */
			count.rspcnt = cip->elx_outsz;
		}
		ELX_DRVR_UNLOCK(phba, iflag);
		/* Return data length */
		if (copy_to_user((uint8_t *) cip->elx_arg3, (uint8_t *) & count,
				 (2 * sizeof (uint32_t)))) {
			rc = EIO;
			ELX_DRVR_LOCK(phba, iflag);
			break;
		}
		ELX_DRVR_LOCK(phba, iflag);
		cip->elx_outsz = 0;
		if (count.rspcnt) {
			if (dfc_rsp_data_copy
			    (phba, (uint8_t *) cip->elx_dataout, outdmp,
			     count.rspcnt)) {
				rc = EIO;
				break;
			}
		}
		break;
	case IOSTAT_LOCAL_REJECT:
		cip->elx_outsz = 0;
		if (rsp->un.grsp.perr.statLocalError == IOERR_SEQUENCE_TIMEOUT) {
			rc = ETIMEDOUT;
			break;
		}
		rc = EFAULT;
		goto sndsczout;	/* count.rspcnt and count.snscnt is already 0 */
	case IOSTAT_FCP_RSP_ERROR:
		/* at this point, clear_count is the residual count. 
		 * We are changing it to the amount actually xfered.
		 */
		if (fcpcmd->fcpCntl3 == READ_DATA) {
			if ((fcprsp->rspStatus2 & RESID_UNDER)
			    && (fcprsp->rspStatus3 == SCSI_STAT_GOOD)) {
				goto cpdata;
			}
		} else {
			clear_count = 0;
		}
		count.rspcnt = (uint32_t) clear_count;
		cip->elx_outsz = 0;
		if (fcprsp->rspStatus2 & RSP_LEN_VALID) {
			j = SWAP_DATA(fcprsp->rspRspLen);
		}
		if (fcprsp->rspStatus2 & SNS_LEN_VALID) {
			if (cip->elx_cmd == LPFC_HBA_SEND_SCSI) {
				if (snsbfrcnt < SWAP_DATA(fcprsp->rspSnsLen))
					count.snscnt = snsbfrcnt;
				else
					count.snscnt =
					    SWAP_DATA(fcprsp->rspSnsLen);
				/* Return sense info from rsp packet */
				ELX_DRVR_UNLOCK(phba, iflag);
				if (copy_to_user
				    ((uint8_t *) cip->elx_arg2,
				     ((uint8_t *) & fcprsp->rspInfo0) + j,
				     count.snscnt)) {
					rc = EIO;
					ELX_DRVR_LOCK(phba, iflag);
					break;
				}
				ELX_DRVR_LOCK(phba, iflag);
			}
		} else {
			rc = EFAULT;
			break;
		}
		ELX_DRVR_UNLOCK(phba, iflag);
		if (copy_to_user(	/* return data length */
					(uint8_t *) cip->elx_arg3,
					(uint8_t *) & count,
					(2 * sizeof (uint32_t)))) {
			rc = EIO;
			ELX_DRVR_LOCK(phba, iflag);
			break;
		}
		ELX_DRVR_LOCK(phba, iflag);
		if (count.rspcnt) {	/* return data for read */
			if (dfc_rsp_data_copy
			    (phba, (uint8_t *) cip->elx_dataout, outdmp,
			     count.rspcnt)) {
				rc = EIO;
				break;
			}
		}
		break;
	default:
		cip->elx_outsz = 0;
		rc = EFAULT;
		break;
	}
      sndsczout:
	DFC_CMD_DATA_FREE(phba, outdmp);
	ELX_MEM_PUT(phba, MEM_BUF, (uint8_t *) mp);
	ELX_MEM_PUT(phba, MEM_BPL, (uint8_t *) bmp);
	ELX_MEM_PUT(phba, MEM_IOCB, (uint8_t *) cmdiocbq);
	ELX_MEM_PUT(phba, MEM_IOCB, (uint8_t *) rspiocbq);

	return (rc);
}

int
lpfc_ioctl_send_els(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{

	uint32_t did;
	uint32_t opcode;
	LPFC_BINDLIST_t *blp;
	LPFCHBA_t *plhba;
	LPFC_NODELIST_t *pndl;
	int rc = 0;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	did = (uint32_t) ((ulong) cip->elx_arg1);
	opcode = (uint32_t) ((ulong) cip->elx_arg2);
	did = (did & Mask_DID);

	if (((pndl = lpfc_findnode_did(phba, NLP_SEARCH_ALL, did))) == 0) {
		if ((pndl = (LPFC_NODELIST_t *) elx_mem_get(phba, MEM_NLP))) {
			memset((void *)pndl, 0, sizeof (LPFC_NODELIST_t));
			pndl->nlp_DID = did;
			pndl->nlp_state = NLP_STE_UNUSED_NODE;
			blp = pndl->nlp_listp_bind;
			if (blp) {
				lpfc_nlp_bind(phba, blp);
			}
		} else {
			rc = ENOMEM;
			return (rc);
		}
	}

	switch (opcode) {
	case ELS_CMD_PLOGI:
		lpfc_issue_els_plogi(phba, pndl, 0);
		break;
	case ELS_CMD_LOGO:
		lpfc_issue_els_logo(phba, pndl, 0);
		break;
	case ELS_CMD_ADISC:
		lpfc_issue_els_adisc(phba, pndl, 0);
		break;
	default:
		break;
	}

	return (rc);
}

int
lpfc_ioctl_send_mgmt_rsp(elxHBA_t * phba,
			 ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	ULP_BDE64 *bpl;
	DMABUF_t *bmp;
	DMABUFEXT_t *indmp;
	LPFCHBA_t *plhba;
	uint32_t tag;
	int reqbfrcnt;
	int rc = 0;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	tag = (uint32_t) cip->elx_flag;	/* XRI for XMIT_SEQUENCE */
	reqbfrcnt = (uint32_t) ((ulong) cip->elx_arg2);

	if ((reqbfrcnt == 0) || (reqbfrcnt > (80 * 4096))) {
		rc = ERANGE;
		return (rc);
	}

	/* Allocate buffer for Buffer ptr list */
	if ((bmp = (DMABUF_t *) elx_mem_get(phba, MEM_BPL)) == 0) {
		rc = ENOMEM;
		return (rc);
	}
	bpl = (ULP_BDE64 *) bmp->virt;

	if ((indmp =
	     dfc_cmd_data_alloc(phba, (char *)cip->elx_arg1, bpl,
				reqbfrcnt)) == 0) {
		elx_mem_put(phba, MEM_BPL, (uint8_t *) bmp);
		rc = ENOMEM;
		return (rc);
	}
	/* flag contains total number of BPLs for xmit */
	if ((rc = lpfc_issue_ct_rsp(phba, tag, bmp, indmp))) {
		if (rc == IOCB_TIMEDOUT)
			rc = ETIMEDOUT;
		else if (rc == IOCB_ERROR)
			rc = EACCES;
	}

	dfc_cmd_data_free(phba, indmp);
	elx_mem_put(phba, MEM_BPL, (uint8_t *) bmp);

	return (rc);
}

int
lpfc_ioctl_send_mgmt_cmd(elxHBA_t * phba,
			 ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	LPFCHBA_t *plhba;
	LPFC_NODELIST_t *pndl;
	ULP_BDE64 *bpl;
	HBA_WWN findwwn;
	uint32_t finddid;
	ELX_IOCBQ_t *cmdiocbq = 0;	/* Initialize the command iocb queue to a NULL default. */
	ELX_IOCBQ_t *rspiocbq = 0;	/* Initialize the response iocb queue to a NULL default. */
	DMABUFEXT_t *indmp = 0;
	DMABUFEXT_t *outdmp = 0;
	IOCB_t *cmd = 0;
	IOCB_t *rsp = 0;
	DMABUF_t *mp = 0;
	DMABUF_t *bmp = 0;
	ELX_SLI_t *psli = &phba->sli;
	ELX_SLI_RING_t *pring = &psli->ring[LPFC_ELS_RING];	/* els ring */
	int i0, rc = 0;
	int reqbfrcnt;
	int snsbfrcnt;
	uint32_t timeout;
	unsigned long iflag;

	plhba = (LPFCHBA_t *) phba->pHbaProto;

	reqbfrcnt = cip->elx_arg4;
	snsbfrcnt = cip->elx_arg5;

	if (!(reqbfrcnt) || !(snsbfrcnt)
	    || (reqbfrcnt + snsbfrcnt) > (80 * 4096)) {
		rc = ERANGE;
		goto sndmgtqwt;
	}

	if (cip->elx_cmd == LPFC_HBA_SEND_MGMT_CMD) {

		ELX_DRVR_UNLOCK(phba, iflag);
		if (copy_from_user
		    ((uint8_t *) & findwwn, (uint8_t *) cip->elx_arg3,
		     (ulong) (sizeof (HBA_WWN)))) {
			rc = EIO;
			ELX_DRVR_LOCK(phba, iflag);
			goto sndmgtqwt;
		}
		ELX_DRVR_LOCK(phba, iflag);

		pndl =
		    lpfc_findnode_wwpn(phba,
				       NLP_SEARCH_MAPPED | NLP_SEARCH_UNMAPPED,
				       (NAME_TYPE *) & findwwn);
		if (!pndl) {
			rc = ENODEV;
			goto sndmgtqwt;
		}
	} else {
		finddid = (uint32_t) ((unsigned long)cip->elx_arg3);
		if (!(pndl = lpfc_findnode_did(phba,
					       NLP_SEARCH_MAPPED |
					       NLP_SEARCH_UNMAPPED, finddid))) {
			rc = ENODEV;
			goto sndmgtqwt;
		}
	}
	if (!pndl || !(psli->sliinit.sli_flag & ELX_SLI2_ACTIVE)) {
		rc = EACCES;
		goto sndmgtqwt;
	}
	if (pndl->nlp_flag & NLP_ELS_SND_MASK) {
		rc = ENODEV;
		goto sndmgtqwt;
	}
	if ((cmdiocbq =
	     (ELX_IOCBQ_t *) elx_mem_get(phba, MEM_IOCB | MEM_PRI)) == 0) {
		rc = ENOMEM;
		goto sndmgtqwt;
	}
	memset((void *)cmdiocbq, 0, sizeof (ELX_IOCBQ_t));
	cmd = &(cmdiocbq->iocb);

	if ((rspiocbq =
	     (ELX_IOCBQ_t *) elx_mem_get(phba, MEM_IOCB | MEM_PRI)) == 0) {
		rc = ENOMEM;
		goto sndmgtqwt;
	}
	memset((void *)rspiocbq, 0, sizeof (ELX_IOCBQ_t));
	rsp = &(rspiocbq->iocb);

	if ((bmp = (DMABUF_t *) elx_mem_get(phba, MEM_BPL)) == 0) {
		rc = ENOMEM;
		goto sndmgtqwt;
	}
	bpl = (ULP_BDE64 *) bmp->virt;
	if ((indmp = dfc_cmd_data_alloc(phba, (char *)cip->elx_arg1, bpl,
					reqbfrcnt)) == 0) {
		elx_mem_put(phba, MEM_BPL, (uint8_t *) bmp);
		rc = ENOMEM;
		goto sndmgtqwt;
	}
	bpl += indmp->flag;	/* flag contains total number of BPLs for xmit */
	if ((outdmp = dfc_cmd_data_alloc(phba, 0, bpl, snsbfrcnt)) == 0) {
		dfc_cmd_data_free(phba, indmp);
		elx_mem_put(phba, MEM_BPL, (uint8_t *) bmp);
		rc = ENOMEM;
		goto sndmgtqwt;
	}

	cmd->un.genreq64.bdl.ulpIoTag32 = 0;
	cmd->un.genreq64.bdl.addrHigh = putPaddrHigh(bmp->phys);
	cmd->un.genreq64.bdl.addrLow = putPaddrLow(bmp->phys);
	cmd->un.genreq64.bdl.bdeFlags = BUFF_TYPE_BDL;
	cmd->un.genreq64.bdl.bdeSize =
	    (outdmp->flag + indmp->flag) * sizeof (ULP_BDE64);
	cmd->ulpCommand = CMD_GEN_REQUEST64_CR;
	cmd->un.genreq64.w5.hcsw.Fctl = (SI | LA);
	cmd->un.genreq64.w5.hcsw.Dfctl = 0;
	cmd->un.genreq64.w5.hcsw.Rctl = FC_UNSOL_CTL;
	cmd->un.genreq64.w5.hcsw.Type = FC_COMMON_TRANSPORT_ULP;
	cmd->ulpIoTag = elx_sli_next_iotag(phba, pring);
	cmd->ulpTimeout = cip->elx_flag;
	cmd->ulpBdeCount = 1;
	cmd->ulpLe = 1;
	cmd->ulpClass = CLASS3;
	cmd->ulpContext = pndl->nle.nlp_rpi;
	cmd->ulpOwner = OWN_CHIP;
	cmdiocbq->context1 = (uint8_t *) 0;
	cmdiocbq->context2 = (uint8_t *) 0;
	cmdiocbq->iocb_flag |= ELX_IO_IOCTL;

	if (cip->elx_flag < (plhba->fc_ratov * 2 + ELX_DRVR_TIMEOUT)) {
		timeout = plhba->fc_ratov * 2 + ELX_DRVR_TIMEOUT;
	} else {
		timeout = cip->elx_flag;
	}

	for (rc = -1, i0 = 0; i0 < 4 && rc != IOCB_SUCCESS; i0++) {
		rc = elx_sli_issue_iocb_wait(phba, pring, cmdiocbq,
					     SLI_IOCB_USE_TXQ, rspiocbq,
					     timeout);
		if (rc == IOCB_ERROR) {
			rc = EACCES;
			goto sndmgtqwt;
		}
	}

	if (rc != IOCB_SUCCESS) {
		goto sndmgtqwt;
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

			goto sndmgtqwt;
		}
	} else {
		outdmp->flag = rsp->un.genreq64.bdl.bdeSize;
	}
	if (outdmp->flag > snsbfrcnt) {	/* copy back response data */
		rc = ERANGE;	/* C_CT Request error */
		elx_printf_log(phba->brd_no, &elx_msgBlk1208,	/* ptr to msg structure */
			       elx_mes1208,	/* ptr to msg */
			       elx_msgBlk1208.msgPreambleStr,	/* begin varargs */
			       outdmp->flag, 4096);	/* end varargs */
		goto sndmgtqwt;
	}
	/* copy back size of response, and response itself */
	memcpy(dm->fc_dataout, (char *)&outdmp->flag, sizeof (int));
	if (dfc_rsp_data_copy
	    (phba, (uint8_t *) cip->elx_arg2, outdmp, outdmp->flag)) {
		rc = EIO;
		goto sndmgtqwt;
	}
      sndmgtqwt:
	DFC_CMD_DATA_FREE(phba, indmp);
	DFC_CMD_DATA_FREE(phba, outdmp);
	ELX_MEM_PUT(phba, MEM_BUF, (uint8_t *) mp);
	ELX_MEM_PUT(phba, MEM_BPL, (uint8_t *) bmp);
	ELX_MEM_PUT(phba, MEM_IOCB, (uint8_t *) cmdiocbq);
	ELX_MEM_PUT(phba, MEM_IOCB, (uint8_t *) rspiocbq);

	return (rc);
}

int
lpfc_ioctl_mbox(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	MAILBOX_t *pmbox;
	uint32_t size;
	elx_dma_addr_t lptr;
	struct dfc_info *di;
	LPFCHBA_t *plhba = (LPFCHBA_t *) phba->pHbaProto;
	ELX_MBOXQ_t *pmboxq;
	DMABUF_t *pbfrnfo;
	unsigned long iflag;
	int count = 0;
	int rc = 0;
	int mbxstatus = 0;

	/* Allocate mbox structure */
	if ((pmbox =
	     (MAILBOX_t *) elx_mem_get(phba, (MEM_MBOX | MEM_PRI))) == 0) {
		return (1);
	}

	/* Allocate mboxq structure */
	if ((pmboxq =
	     (ELX_MBOXQ_t *) elx_mem_get(phba, (MEM_MBOX | MEM_PRI))) == 0) {
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmbox);
		return (1);
	}

	/* Allocate mbuf structure */
	if ((pbfrnfo = (DMABUF_t *) elx_mem_get(phba, MEM_BUF)) == 0) {
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmbox);
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmboxq);
		return (1);
	}

	di = &dfc.dfc_info[cip->elx_brd];

	ELX_DRVR_UNLOCK(phba, iflag);
	if (copy_from_user((uint8_t *) pmbox, (uint8_t *) cip->elx_arg1,
			   MAILBOX_CMD_WSIZE * sizeof (uint32_t))) {
		ELX_DRVR_LOCK(phba, iflag);
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmbox);
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmboxq);
		elx_mem_put(phba, MEM_BUF, (uint8_t *) pbfrnfo);
		rc = EIO;

		return (rc);
	}
	ELX_DRVR_LOCK(phba, iflag);

	while (di->fc_flag & DFC_MBOX_ACTIVE) {
		ELX_DRVR_UNLOCK(phba, iflag);
		elx_sleep_ms(phba, 5);
		ELX_DRVR_LOCK(phba, iflag);
		if (count++ == 200)
			break;
	}

	if (count >= 200) {
		pmbox->mbxStatus = MBXERR_ERROR;
		rc = EBUSY;
		goto mbout_err;
	} else {
#ifdef _LP64
		if ((pmbox->mbxCommand == MBX_READ_SPARM) ||
		    (pmbox->mbxCommand == MBX_READ_RPI) ||
		    (pmbox->mbxCommand == MBX_REG_LOGIN) ||
		    (pmbox->mbxCommand == MBX_READ_LA)) {
			/* Must use 64 bit versions of these mbox cmds */
			pmbox->mbxStatus = MBXERR_ERROR;
			rc = ENODEV;
			goto mbout_err;
		}
#endif
		di->fc_flag |= DFC_MBOX_ACTIVE;
		lptr = 0;
		size = 0;
		switch (pmbox->mbxCommand) {
			/* Offline only */
		case MBX_WRITE_NV:
		case MBX_INIT_LINK:
		case MBX_DOWN_LINK:
		case MBX_CONFIG_LINK:
		case MBX_CONFIG_RING:
		case MBX_RESET_RING:
		case MBX_UNREG_LOGIN:
		case MBX_CLEAR_LA:
		case MBX_DUMP_CONTEXT:
		case MBX_RUN_DIAGS:
		case MBX_RESTART:
		case MBX_FLASH_WR_ULA:
		case MBX_SET_MASK:
		case MBX_SET_SLIM:
		case MBX_SET_DEBUG:
			if (!(plhba->fc_flag & FC_OFFLINE_MODE)) {
				pmbox->mbxStatus = MBXERR_ERROR;
				di->fc_flag &= ~DFC_MBOX_ACTIVE;
				goto mbout_err;
			}
			break;

			/* Online / Offline */
		case MBX_LOAD_SM:
		case MBX_READ_NV:
		case MBX_READ_CONFIG:
		case MBX_READ_RCONFIG:
		case MBX_READ_STATUS:
		case MBX_READ_XRI:
		case MBX_READ_REV:
		case MBX_READ_LNK_STAT:
		case MBX_DUMP_MEMORY:
		case MBX_DOWN_LOAD:
		case MBX_UPDATE_CFG:
		case MBX_LOAD_AREA:
		case MBX_LOAD_EXP_ROM:
			break;

			/* Online / Offline - with DMA */
		case MBX_READ_SPARM64:
			lptr =
			    (elx_dma_addr_t) getPaddr(pmbox->un.varRdSparm.un.
						      sp64.addrHigh,
						      pmbox->un.varRdSparm.un.
						      sp64.addrLow);
			size = (int)pmbox->un.varRdSparm.un.sp64.tus.f.bdeSize;
			if (lptr) {
				pmbox->un.varRdSparm.un.sp64.addrHigh =
				    putPaddrHigh(pbfrnfo->phys);
				pmbox->un.varRdSparm.un.sp64.addrLow =
				    putPaddrLow(pbfrnfo->phys);
			}
			break;

		case MBX_READ_RPI64:
			/* This is only allowed when online is SLI2 mode */
			lptr =
			    (elx_dma_addr_t) getPaddr(pmbox->un.varRdRPI.un.
						      sp64.addrHigh,
						      pmbox->un.varRdRPI.un.
						      sp64.addrLow);
			size = (int)pmbox->un.varRdRPI.un.sp64.tus.f.bdeSize;
			if (lptr) {
				pmbox->un.varRdRPI.un.sp64.addrHigh =
				    putPaddrHigh(pbfrnfo->phys);
				pmbox->un.varRdRPI.un.sp64.addrLow =
				    putPaddrLow(pbfrnfo->phys);
			}
			break;

		case MBX_READ_LA:
		case MBX_READ_LA64:
		case MBX_REG_LOGIN:
		case MBX_REG_LOGIN64:
		case MBX_CONFIG_PORT:
		case MBX_RUN_BIU_DIAG:
			/* Do not allow SLI-2 commands */
			pmbox->mbxStatus = MBXERR_ERROR;
			di->fc_flag &= ~DFC_MBOX_ACTIVE;
			goto mbout_err;

		default:
			/* Offline only
			 * Let firmware return error for unsupported commands
			 */
			if (!(plhba->fc_flag & FC_OFFLINE_MODE)) {
				pmbox->mbxStatus = MBXERR_ERROR;
				di->fc_flag &= ~DFC_MBOX_ACTIVE;
				goto mbout_err;
			}
			break;
		}		/* switch pmbox->command */

		{
			MAILBOX_t *pmb = &pmboxq->mb;

			memset((void *)pmboxq, 0, sizeof (ELX_MBOXQ_t));
			pmb->mbxCommand = pmbox->mbxCommand;
			pmb->mbxOwner = pmbox->mbxOwner;
			pmb->un = pmbox->un;
			pmb->us = pmbox->us;
			pmboxq->context1 = (uint8_t *) 0;
			if (plhba->fc_flag & FC_OFFLINE_MODE) {
				ELX_DRVR_UNLOCK(phba, iflag);
				mbxstatus =
				    elx_sli_issue_mbox(phba, pmboxq, MBX_POLL);
				ELX_DRVR_LOCK(phba, iflag);
			} else
				mbxstatus =
				    elx_sli_issue_mbox_wait(phba, pmboxq,
							    ELX_MBOX_TMO);
			di->fc_flag &= ~DFC_MBOX_ACTIVE;
			if (mbxstatus != MBX_SUCCESS) {
				/* Not successful */
				goto mbout;
			}

		}

		if (lptr) {
			ELX_DRVR_UNLOCK(phba, iflag);
			if ((copy_to_user
			     ((uint8_t *) & lptr, (uint8_t *) pbfrnfo->virt,
			      (ulong) size))) {
				rc = EIO;
			}
			ELX_DRVR_LOCK(phba, iflag);
		}
	}

      mbout:
	{
		MAILBOX_t *pmb = &pmboxq->mb;

		memcpy(dm->fc_dataout, (char *)pmb,
		       MAILBOX_CMD_WSIZE * sizeof (uint32_t));
	}

	goto mbout_freemem;

      mbout_err:
	{
		/* Jump here only if there is an error and copy the status */
		memcpy(dm->fc_dataout, (char *)pmbox,
		       MAILBOX_CMD_WSIZE * sizeof (uint32_t));
	}

      mbout_freemem:
	/* Free allocated mbox memory */
	if (pmbox)
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmbox);

	/* Free allocated mboxq memory */
	if (pmboxq) {
		if (mbxstatus == MBX_TIMEOUT) {
			/*
			 * Let SLI layer to release mboxq if mbox command completed after timeout.
			 */
			pmboxq->mbox_cmpl = 0;
		} else {
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmboxq);
		}
	}

	/* Free allocated mbuf memory */
	if (pbfrnfo)
		elx_mem_put(phba, MEM_BUF, (uint8_t *) pbfrnfo);

	return (rc);
}

int
lpfc_ioctl_display_pci_all(elxHBA_t * phba,
			   ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	int cnt, offset, rc = 0;

	cnt = 256;
	offset = 0;
	elx_cnt_read_pci(phba, offset, cnt, (uint32_t *) dm->fc_dataout);
	return (rc);
}

int
lpfc_ioctl_write_hc(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	uint32_t incr;
	ELX_SLI_t *psli = &phba->sli;
	int rc = 0;

	incr = (uint32_t) ((ulong) cip->elx_arg1);
	(psli->sliinit.elx_sli_write_HC) (phba, incr);

	return (rc);
}

int
lpfc_ioctl_read_hc(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	uint32_t offset;
	ELX_SLI_t *psli = &phba->sli;
	int rc = 0;

	offset = (psli->sliinit.elx_sli_read_HC) (phba);
	*((uint32_t *) dm->fc_dataout) = offset;

	return (rc);
}

int
lpfc_ioctl_write_hs(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	uint32_t incr;
	ELX_SLI_t *psli = &phba->sli;
	int rc = 0;

	incr = (uint32_t) ((ulong) cip->elx_arg1);
	(psli->sliinit.elx_sli_write_HS) (phba, incr);

	return (rc);
}

int
lpfc_ioctl_read_hs(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	uint32_t offset;
	ELX_SLI_t *psli = &phba->sli;
	int rc = 0;

	offset = (psli->sliinit.elx_sli_read_HS) (phba);
	*((uint32_t *) dm->fc_dataout) = offset;

	return (rc);
}

int
lpfc_ioctl_write_ha(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	uint32_t incr;
	ELX_SLI_t *psli = &phba->sli;
	int rc = 0;

	incr = (uint32_t) ((ulong) cip->elx_arg1);
	(psli->sliinit.elx_sli_write_HA) (phba, incr);

	return (rc);
}

int
lpfc_ioctl_read_ha(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	uint32_t offset;
	ELX_SLI_t *psli = &phba->sli;
	int rc = 0;

	offset = (psli->sliinit.elx_sli_read_HA) (phba);
	*((uint32_t *) dm->fc_dataout) = offset;

	return (rc);
}

int
lpfc_ioctl_write_ca(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	uint32_t incr;
	ELX_SLI_t *psli = &phba->sli;
	int rc = 0;

	incr = (uint32_t) ((ulong) cip->elx_arg1);
	(psli->sliinit.elx_sli_write_CA) (phba, incr);

	return (rc);
}

int
lpfc_ioctl_read_ca(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	uint32_t offset;
	ELX_SLI_t *psli = &phba->sli;
	int rc = 0;

	offset = (psli->sliinit.elx_sli_read_CA) (phba);
	*((uint32_t *) dm->fc_dataout) = offset;

	return (rc);
}

int
lpfc_ioctl_read_mb(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	ELX_SLI_t *psli = &phba->sli;
	int rc = 0;
	MAILBOX_t *pmbox;
	struct dfc_mem mbox_dm;

	/* Allocate mboxq structure */
	mbox_dm.fc_dataout = NULL;
	if ((rc = dfc_data_alloc(phba, &mbox_dm, sizeof (MAILBOX_t))))
		return (rc);
	else
		pmbox = (MAILBOX_t *) mbox_dm.fc_dataout;

	if (psli->sliinit.sli_flag & ELX_SLI2_ACTIVE) {
		/* copy results back to user */
		elx_sli_pcimem_bcopy((uint32_t *) pmbox, dm->fc_dataout,
				     (sizeof (uint32_t) * (MAILBOX_CMD_WSIZE)));
		elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
				 sizeof (MAILBOX_t), ELX_DMA_SYNC_FORDEV);
	} else {
		/* First copy command data */
		(psli->sliinit.elx_sli_read_slim) ((void *)phba,
						   (void *)dm->fc_dataout, 0,
						   MAILBOX_CMD_WSIZE);
	}

	/* Free allocated mbox memory */
	if (pmbox)
		dfc_data_free(phba, &mbox_dm);

	return (rc);
}

int
lpfc_ioctl_dbg(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	uint32_t offset;
	int rc = 0;

	offset = (uint32_t) ((ulong) cip->elx_arg1);
	switch (offset) {
	case 0xffffffff:
		break;
	default:
		fc_dbg_flag = offset;
		break;
	}

	memcpy(dm->fc_dataout, (uint8_t *) & fc_dbg_flag, sizeof (uint32_t));
	return (rc);
}

int
lpfc_ioctl_inst(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	int rc = 0;

	memcpy(dm->fc_dataout, (uint8_t *) & lpfc_instcnt, sizeof (int));
	memcpy(((uint8_t *) dm->fc_dataout) + sizeof (int),
	       (uint8_t *) lpfc_instance, sizeof (int) * MAX_ELX_BRDS);
	return (rc);
}

int
lpfc_ioctl_listn(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{

	LPFC_BINDLIST_t *bpp;
	LPFC_BINDLIST_t *blp;
	LPFC_NODELIST_t *npp;
	LPFC_NODELIST_t *pndl;
	LPFCHBA_t *plhba;
	uint32_t offset;
	ulong lcnt;
	ulong *lcntp;
	int rc = 0;
	uint32_t total_mem = dm->fc_outsz;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	offset = (uint32_t) ((ulong) cip->elx_arg1);
	/* If the value of offset is 1, the driver is handling
	 * the bindlist.  Correct the total memory to account for the 
	 * bindlist's different size 
	 */
	if (offset == 1) {
		total_mem -= sizeof (LPFC_BINDLIST_t);
	} else {
		total_mem -= sizeof (LPFC_NODELIST_t);
	}

	lcnt = 0;
	switch (offset) {
	case 1:		/* bind */
		lcntp = dm->fc_dataout;
		memcpy(dm->fc_dataout, (uint8_t *) & lcnt, sizeof (ulong));
		bpp =
		    (LPFC_BINDLIST_t *) ((uint8_t *) (dm->fc_dataout) +
					 sizeof (ulong));
		blp = plhba->fc_nlpbind_start;
		while ((blp != (LPFC_BINDLIST_t *) & plhba->fc_nlpbind_start)
		       && (total_mem > 0)) {
			memcpy(bpp, (uint8_t *) blp,
			       (sizeof (LPFC_BINDLIST_t)));
			total_mem -= sizeof (LPFC_BINDLIST_t);
			bpp++;
			lcnt++;
			blp = (LPFC_BINDLIST_t *) blp->nlp_listp_next;
		}
		*lcntp = lcnt;
		break;
	case 2:		/* unmap */
		lcntp = dm->fc_dataout;
		memcpy(dm->fc_dataout, (uint8_t *) & lcnt, sizeof (ulong));
		npp =
		    (LPFC_NODELIST_t *) ((uint8_t *) (dm->fc_dataout) +
					 sizeof (ulong));
		pndl = plhba->fc_nlpunmap_start;
		while ((pndl != (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start)
		       && (total_mem > 0)) {
			memcpy(npp, (uint8_t *) pndl,
			       (sizeof (LPFC_NODELIST_t)));
			total_mem -= sizeof (LPFC_NODELIST_t);
			npp++;
			lcnt++;
			pndl = (LPFC_NODELIST_t *) pndl->nle.nlp_listp_next;
		}
		*lcntp = lcnt;
		break;
	case 3:		/* map */
		lcntp = dm->fc_dataout;
		memcpy(dm->fc_dataout, (uint8_t *) & lcnt, sizeof (ulong));
		npp =
		    (LPFC_NODELIST_t *) ((uint8_t *) (dm->fc_dataout) +
					 sizeof (ulong));
		pndl = plhba->fc_nlpmap_start;
		while ((pndl != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start)
		       && (total_mem > 0)) {
			memcpy(npp, (uint8_t *) pndl,
			       (sizeof (LPFC_NODELIST_t)));
			total_mem -= sizeof (LPFC_NODELIST_t);
			npp++;
			lcnt++;
			pndl = (LPFC_NODELIST_t *) pndl->nle.nlp_listp_next;
		}
		*lcntp = lcnt;
		break;
	case 4:		/* plogi */
		lcntp = dm->fc_dataout;
		memcpy(dm->fc_dataout, (uint8_t *) & lcnt, sizeof (ulong));
		npp =
		    (LPFC_NODELIST_t *) ((uint8_t *) (dm->fc_dataout) +
					 sizeof (ulong));
		pndl = plhba->fc_plogi_start;
		while ((pndl != (LPFC_NODELIST_t *) & plhba->fc_plogi_start)
		       && (total_mem > 0)) {
			memcpy(npp, (uint8_t *) pndl,
			       (sizeof (LPFC_NODELIST_t)));
			total_mem -= sizeof (LPFC_NODELIST_t);
			npp++;
			lcnt++;
			pndl = (LPFC_NODELIST_t *) pndl->nle.nlp_listp_next;
		}
		*lcntp = lcnt;
		break;
	case 5:		/* adisc */
		lcntp = dm->fc_dataout;
		memcpy(dm->fc_dataout, (uint8_t *) & lcnt, sizeof (ulong));
		npp =
		    (LPFC_NODELIST_t *) ((uint8_t *) (dm->fc_dataout) +
					 sizeof (ulong));
		pndl = plhba->fc_adisc_start;
		while ((pndl != (LPFC_NODELIST_t *) & plhba->fc_adisc_start)
		       && (total_mem > 0)) {
			memcpy(npp, (uint8_t *) pndl,
			       (sizeof (LPFC_NODELIST_t)));
			total_mem -= sizeof (LPFC_NODELIST_t);
			npp++;
			lcnt++;
			pndl = (LPFC_NODELIST_t *) pndl->nle.nlp_listp_next;
		}
		*lcntp = lcnt;
		break;
	case 6:		/* all except bind list */
		lcntp = dm->fc_dataout;
		memcpy(dm->fc_dataout, (uint8_t *) & lcnt, sizeof (ulong));
		npp =
		    (LPFC_NODELIST_t *) ((uint8_t *) (dm->fc_dataout) +
					 sizeof (ulong));
		pndl = plhba->fc_plogi_start;
		while ((pndl != (LPFC_NODELIST_t *) & plhba->fc_plogi_start)
		       && (total_mem > 0)) {
			memcpy(npp, (uint8_t *) pndl,
			       (sizeof (LPFC_NODELIST_t)));
			total_mem -= sizeof (LPFC_NODELIST_t);
			npp++;
			lcnt++;
			pndl = (LPFC_NODELIST_t *) pndl->nle.nlp_listp_next;
		}
		pndl = plhba->fc_adisc_start;
		while ((pndl != (LPFC_NODELIST_t *) & plhba->fc_adisc_start)
		       && (total_mem > 0)) {
			memcpy(npp, (uint8_t *) pndl,
			       (sizeof (LPFC_NODELIST_t)));
			total_mem -= sizeof (LPFC_NODELIST_t);
			npp++;
			lcnt++;
			pndl = (LPFC_NODELIST_t *) pndl->nle.nlp_listp_next;
		}
		pndl = plhba->fc_nlpunmap_start;
		while ((pndl != (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start)
		       && (total_mem > 0)) {
			memcpy(npp, (uint8_t *) pndl,
			       (sizeof (LPFC_NODELIST_t)));
			total_mem -= sizeof (LPFC_NODELIST_t);
			npp++;
			lcnt++;
			pndl = (LPFC_NODELIST_t *) pndl->nle.nlp_listp_next;
		}
		pndl = plhba->fc_nlpmap_start;
		while ((pndl != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start)
		       && (total_mem > 0)) {
			memcpy(npp, (uint8_t *) pndl,
			       (sizeof (LPFC_NODELIST_t)));
			total_mem -= sizeof (LPFC_NODELIST_t);
			npp++;
			lcnt++;
			pndl = (LPFC_NODELIST_t *) pndl->nle.nlp_listp_next;
		}
		*lcntp = lcnt;
		break;
	default:
		rc = ERANGE;
		break;
	}
	cip->elx_outsz = (sizeof (ulong) + (lcnt * sizeof (LPFC_NODELIST_t)));

	return (rc);
}

int
lpfc_ioctl_read_bplist(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	ELX_SLI_RING_t *rp;
	ELX_SLINK_t *dlp;
	DMABUF_t *mm;
	uint32_t *lptr;
	ELX_SLI_t *psli;
	int rc = 0;
	uint32_t total_mem = dm->fc_outsz;

	psli = &phba->sli;
	rp = &psli->ring[LPFC_ELS_RING];	/* RING 0 */
	dlp = &rp->postbufq;
	mm = (DMABUF_t *) dlp->q_first;
	lptr = (uint32_t *) dm->fc_dataout;
	total_mem -= (3 * sizeof (ulong));
	while ((mm) && (total_mem > 0)) {
		if ((cip->elx_ring == LPFC_ELS_RING)
		    || (cip->elx_ring == LPFC_FCP_NEXT_RING)) {
			*lptr++ = (uint32_t) ((ulong) mm);
			*lptr++ = (uint32_t) ((ulong) mm->virt);
			*lptr++ = (uint32_t) ((ulong) mm->phys);
			mm = (DMABUF_t *) mm->next;
		}
		total_mem -= (3 * sizeof (ulong));
	}
	*lptr++ = 0;

	cip->elx_outsz = ((uint8_t *) lptr - (uint8_t *) (dm->fc_dataout));

	return (rc);
}

int
lpfc_ioctl_reset(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	uint32_t status;
	uint32_t offset;
	ELX_SLI_t *psli;
	int rc = 0;

	psli = &phba->sli;
	offset = (uint32_t) ((ulong) cip->elx_arg1);
	switch (offset) {
	case 1:		/* hba */
		phba->hba_state = 0;	/* Don't skip post */
		elx_sli_brdreset(phba);
		phba->hba_state = ELX_INIT_START;
		mdelay(2500);
		/* Read the HBA Host Status Register */
		status = (psli->sliinit.elx_sli_read_HS) (phba);
		break;

	case 3:		/* target */
		lpfc_fcp_abort(phba, TARGET_RESET, (int)((ulong) cip->elx_arg2),
			       -1);
		break;
	case 4:		/* lun */
		lpfc_fcp_abort(phba, LUN_RESET, (int)((ulong) cip->elx_arg2),
			       (int)((ulong) cip->elx_arg3));
		break;
	case 5:		/* task set */
		lpfc_fcp_abort(phba, ABORT_TASK_SET,
			       (int)((ulong) cip->elx_arg2),
			       (int)((ulong) cip->elx_arg3));
		break;
	case 6:		/* bus */
		lpfc_fcp_abort(phba, BUS_RESET, -1, -1);
		break;

	default:
		rc = ERANGE;
		break;
	}
	return (rc);
}

int
lpfc_ioctl_read_hba(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{

	ELX_SLI_t *psli;
	int rc = 0;
	int cnt = 0;
	unsigned long iflag;

	psli = &phba->sli;
	if (cip->elx_arg1) {

		if (psli->sliinit.sli_flag & ELX_SLI2_ACTIVE) {
			/* The SLIM2 size is stored in the next field */
			cnt = (uint32_t) (unsigned long)phba->slim2p.next;
		} else {
			cnt = 4096;
		}

		if (psli->sliinit.sli_flag & ELX_SLI2_ACTIVE) {
			/* copy results back to user */
			elx_sli_pcimem_bcopy((uint32_t *) psli->MBhostaddr,
					     (uint32_t *) dm->fc_dataout, cnt);
		} else {
			/* First copy command data */
			(psli->sliinit.elx_sli_read_slim) ((void *)phba,
							   (void *)dm->
							   fc_dataout, 0, cnt);
		}

		ELX_DRVR_UNLOCK(phba, iflag);
		if (copy_to_user
		    ((uint8_t *) cip->elx_arg1, (uint8_t *) dm->fc_dataout,
		     cnt)) {
			rc = EIO;
			ELX_DRVR_LOCK(phba, iflag);
			return (rc);
		}
		ELX_DRVR_LOCK(phba, iflag);
	}
	memcpy(dm->fc_dataout, phba, sizeof (elxHBA_t));
	return (rc);
}

int
lpfc_ioctl_stat(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	LPFCHBA_t *plhba;
	int rc = 0;

	if ((unsigned long)cip->elx_arg1 == 1) {
		memcpy(dm->fc_dataout, phba, sizeof (elxHBA_t));
	}

	/* Copy LPFC_STAT_t struct from LPFCHBA_t */
	if ((unsigned long)cip->elx_arg1 == 2) {
		plhba = (LPFCHBA_t *) phba->pHbaProto;
		memcpy(dm->fc_dataout, &(plhba->fc_stat), sizeof (LPFC_STAT_t));
	}

	return (rc);
}

int
lpfc_ioctl_read_lhba(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	LPFCHBA_t *plhba;
	int rc = 0;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	memcpy(dm->fc_dataout, plhba, sizeof (LPFCHBA_t));
	return (rc);
}

int
lpfc_ioctl_read_lxhba(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	uint8_t *lp;
	struct dfc_mem lp_dm;
	int rc = 0;

	/* Allocate mboxq structure */
	lp_dm.fc_dataout = NULL;
	if ((rc = dfc_data_alloc(phba, &lp_dm, 256)))
		return (rc);
	else
		lp = (uint8_t *) lp_dm.fc_dataout;

	lp = lpfc_get_lpfchba_info(phba, lp);
	memcpy(dm->fc_dataout, lp, 256);

	/* Free allocated block of memory */
	if (lp)
		dfc_data_free(phba, &lp_dm);

	return (rc);
}

int
lpfc_ioctl_devp(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	LPFCHBA_t *plhba;
	uint32_t offset, cnt;
	ELXSCSILUN_t *dev_ptr;
	LPFC_NODELIST_t *pndl;
	ELXSCSITARGET_t *node_ptr;
	int rc = 0;

	cnt = 0;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	offset = (uint32_t) ((ulong) cip->elx_arg1);
	cnt = (uint32_t) ((ulong) cip->elx_arg2);
	if ((offset >= (MAX_FCP_TARGET)) || (cnt >= 128)) {
		rc = ERANGE;
		return (rc);
	}
	node_ptr = 0;
	dev_ptr = 0;
	pndl = 0;
	memset(dm->fc_dataout, 0,
	       (sizeof (ELXSCSITARGET_t) + sizeof (ELXSCSILUN_t) +
		sizeof (LPFC_NODELIST_t)));
	rc = ENODEV;
	node_ptr = plhba->device_queue_hash[offset];
	if (node_ptr) {
		rc = 0;
		cip->elx_outsz = sizeof (ELXSCSITARGET_t);
		memcpy((uint8_t *) dm->fc_dataout, (uint8_t *) node_ptr,
		       (sizeof (ELXSCSITARGET_t)));

		dev_ptr = (ELXSCSILUN_t *) node_ptr->lunlist.q_first;
		while ((dev_ptr != 0)) {
			if (dev_ptr->lun_id == (uint64_t) cnt)
				break;
			dev_ptr = dev_ptr->pnextLun;
		}
		if (dev_ptr) {
			cip->elx_outsz += sizeof (ELXSCSILUN_t);
			memcpy(((uint8_t *) dm->fc_dataout +
				sizeof (ELXSCSITARGET_t)), (uint8_t *) dev_ptr,
			       (sizeof (ELXSCSILUN_t)));
			pndl = (LPFC_NODELIST_t *) node_ptr->pcontext;
			if (pndl) {
				cip->elx_outsz += sizeof (LPFC_NODELIST_t);
				memcpy(((uint8_t *) dm->fc_dataout +
					sizeof (ELXSCSILUN_t) +
					sizeof (ELXSCSITARGET_t)),
				       (uint8_t *) pndl,
				       (sizeof (LPFC_NODELIST_t)));
			}
		}
	}
	return (rc);
}

int
lpfc_ioctl_linkinfo(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	LPFCHBA_t *plhba;
	LinkInfo *linkinfo;
	int rc = 0;

	plhba = (LPFCHBA_t *) phba->pHbaProto;

	linkinfo = (LinkInfo *) dm->fc_dataout;
	linkinfo->a_linkEventTag = plhba->fc_eventTag;
	linkinfo->a_linkUp = plhba->fc_stat.LinkUp;
	linkinfo->a_linkDown = plhba->fc_stat.LinkDown;
	linkinfo->a_linkMulti = plhba->fc_stat.LinkMultiEvent;
	linkinfo->a_DID = plhba->fc_myDID;
	if (plhba->fc_topology == TOPOLOGY_LOOP) {
		if (plhba->fc_flag & FC_PUBLIC_LOOP) {
			linkinfo->a_topology = LNK_PUBLIC_LOOP;
			memcpy((uint8_t *) linkinfo->a_alpaMap,
			       (uint8_t *) plhba->alpa_map, 128);
			linkinfo->a_alpaCnt = plhba->alpa_map[0];
		} else {
			linkinfo->a_topology = LNK_LOOP;
			memcpy((uint8_t *) linkinfo->a_alpaMap,
			       (uint8_t *) plhba->alpa_map, 128);
			linkinfo->a_alpaCnt = plhba->alpa_map[0];
		}
	} else {
		memset((uint8_t *) linkinfo->a_alpaMap, 0, 128);
		linkinfo->a_alpaCnt = 0;
		if (plhba->fc_flag & FC_FABRIC) {
			linkinfo->a_topology = LNK_FABRIC;
		} else {
			linkinfo->a_topology = LNK_PT2PT;
		}
	}
	linkinfo->a_linkState = 0;
	switch (phba->hba_state) {
	case ELX_INIT_START:

	case ELX_LINK_DOWN:
		linkinfo->a_linkState = LNK_DOWN;
		memset((uint8_t *) linkinfo->a_alpaMap, 0, 128);
		linkinfo->a_alpaCnt = 0;
		break;
	case ELX_LINK_UP:

	case ELX_LOCAL_CFG_LINK:
		linkinfo->a_linkState = LNK_UP;
		break;
	case ELX_FLOGI:
		linkinfo->a_linkState = LNK_FLOGI;
		break;
	case ELX_DISC_AUTH:
	case ELX_FABRIC_CFG_LINK:
	case ELX_NS_REG:
	case ELX_NS_QRY:

	case ELX_CLEAR_LA:
		linkinfo->a_linkState = LNK_DISCOVERY;
		break;
	case ELX_HBA_READY:
		linkinfo->a_linkState = LNK_READY;
		break;
	}
	linkinfo->a_alpa = (uint8_t) (plhba->fc_myDID & 0xff);
	memcpy((uint8_t *) linkinfo->a_wwpName,
	       (uint8_t *) & plhba->fc_portname, 8);
	memcpy((uint8_t *) linkinfo->a_wwnName,
	       (uint8_t *) & plhba->fc_nodename, 8);

	return (rc);
}

int
lpfc_ioctl_ioinfo(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{

	IOinfo *ioinfo;
	ELX_SLI_t *psli;
	LPFCHBA_t *plhba;
	int rc = 0;

	psli = &phba->sli;
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	ioinfo = (IOinfo *) dm->fc_dataout;
	ioinfo->a_mbxCmd = psli->slistat.mboxCmd;
	ioinfo->a_mboxCmpl = psli->slistat.mboxEvent;
	ioinfo->a_mboxErr = psli->slistat.mboxStatErr;
	ioinfo->a_iocbCmd = psli->slistat.iocbCmd[cip->elx_ring];
	ioinfo->a_iocbRsp = psli->slistat.iocbRsp[cip->elx_ring];
	ioinfo->a_adapterIntr = (psli->slistat.linkEvent +
				 psli->slistat.iocbRsp[cip->elx_ring] +
				 psli->slistat.mboxEvent);
	ioinfo->a_fcpCmd = plhba->fc_stat.fcpCmd;
	ioinfo->a_fcpCmpl = plhba->fc_stat.fcpCmpl;
	ioinfo->a_fcpErr = plhba->fc_stat.fcpRspErr +
	    plhba->fc_stat.fcpRemoteStop + plhba->fc_stat.fcpPortRjt +
	    plhba->fc_stat.fcpPortBusy + plhba->fc_stat.fcpError +
	    plhba->fc_stat.fcpLocalErr;
	ioinfo->a_bcastRcv = plhba->fc_stat.frameRcvBcast;
	ioinfo->a_RSCNRcv = plhba->fc_stat.elsRcvRSCN;
	ioinfo->a_cnt1 = 0;
	ioinfo->a_cnt2 = 0;
	ioinfo->a_cnt3 = 0;
	ioinfo->a_cnt4 = 0;

	return (rc);
}

int
lpfc_ioctl_nodeinfo(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{

	NodeInfo *np;
	LPFC_NODELIST_t *pndl;
	LPFC_BINDLIST_t *pbdl;
	LPFCHBA_t *plhba;
	uint32_t cnt;
	int rc = 0;
	uint32_t total_mem = dm->fc_outsz;

	np = (NodeInfo *) dm->fc_dataout;
	cnt = 0;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	/* Since the size of bind & others are different,
	   get the node list of bind first
	 */
	total_mem -= sizeof (LPFC_BINDLIST_t);
	pbdl = plhba->fc_nlpbind_start;
	while ((pbdl != (LPFC_BINDLIST_t *) & plhba->fc_nlpbind_start)
	       && (total_mem > 0)) {
		memset((uint8_t *) np, 0, sizeof (LPFC_BINDLIST_t));
		if (pbdl->nlp_bind_type & FCP_SEED_WWPN)
			np->a_flag |= NODE_SEED_WWPN;
		if (pbdl->nlp_bind_type & FCP_SEED_WWNN)
			np->a_flag |= NODE_SEED_WWNN;
		if (pbdl->nlp_bind_type & FCP_SEED_DID)
			np->a_flag |= NODE_SEED_DID;
		if (pbdl->nlp_bind_type & FCP_SEED_AUTO)
			np->a_flag |= NODE_AUTOMAP;
		np->a_state = NODE_SEED;
		np->a_did = pbdl->nlp_DID;
		np->a_targetid = FC_SCSID(pbdl->nlp_pan, pbdl->nlp_sid);
		memcpy(np->a_wwpn, &pbdl->nlp_portname, 8);
		memcpy(np->a_wwnn, &pbdl->nlp_nodename, 8);
		total_mem -= sizeof (LPFC_BINDLIST_t);
		np++;
		cnt++;
		pbdl = (LPFC_BINDLIST_t *) pbdl->nlp_listp_next;
	}

	/* Get the node list of unmap, map, plogi and adisc
	 */
	total_mem -= sizeof (LPFC_NODELIST_t);
	pndl = plhba->fc_plogi_start;
	if (pndl == (LPFC_NODELIST_t *) & plhba->fc_plogi_start) {
		pndl = plhba->fc_adisc_start;
		if (pndl == (LPFC_NODELIST_t *) & plhba->fc_adisc_start) {
			pndl = plhba->fc_nlpunmap_start;
			if (pndl ==
			    (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start) {
				pndl = plhba->fc_nlpmap_start;
			}
		}
	}
	while ((pndl != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start)
	       && (total_mem > 0)) {
		memset((uint8_t *) np, 0, sizeof (LPFC_NODELIST_t));
		if (pndl->nlp_flag & NLP_ADISC_LIST) {
			np->a_flag |= NODE_ADDR_AUTH;
			np->a_state = NODE_LIMBO;
		}
		if (pndl->nlp_flag & NLP_PLOGI_LIST) {
			np->a_state = NODE_PLOGI;
		}
		if (pndl->nlp_flag & NLP_MAPPED_LIST) {
			np->a_state = NODE_ALLOC;
		}
		if (pndl->nlp_flag & NLP_UNMAPPED_LIST) {
			np->a_state = NODE_PRLI;
		}
		if (pndl->nle.nlp_type & NLP_FABRIC)
			np->a_flag |= NODE_FABRIC;
		if (pndl->nle.nlp_type & NLP_FCP_TARGET)
			np->a_flag |= NODE_FCP_TARGET;
		if (pndl->nle.nlp_type & NLP_IP_NODE)
			np->a_flag |= NODE_IP_NODE;
		if (pndl->nlp_flag & NLP_ELS_SND_MASK)	/* Sent ELS mask  -- Check this */
			np->a_flag |= NODE_REQ_SND;
		if (pndl->nlp_flag & NLP_FARP_SND)
			np->a_flag |= NODE_FARP_SND;
		if (pndl->nlp_flag & NLP_SEED_WWPN)
			np->a_flag |= NODE_SEED_WWPN;
		if (pndl->nlp_flag & NLP_SEED_WWNN)
			np->a_flag |= NODE_SEED_WWNN;
		if (pndl->nlp_flag & NLP_SEED_DID)
			np->a_flag |= NODE_SEED_DID;
		if (pndl->nlp_flag & NLP_AUTOMAP)
			np->a_flag |= NODE_AUTOMAP;
		np->a_did = pndl->nlp_DID;
		np->a_targetid = FC_SCSID(pndl->nlp_pan, pndl->nlp_sid);
		memcpy(np->a_wwpn, &pndl->nlp_portname, 8);
		memcpy(np->a_wwnn, &pndl->nlp_nodename, 8);
		total_mem -= sizeof (LPFC_NODELIST_t);
		np++;
		cnt++;

		pndl = (LPFC_NODELIST_t *) pndl->nle.nlp_listp_next;
		if (pndl == (LPFC_NODELIST_t *) & plhba->fc_plogi_start) {
			pndl = plhba->fc_adisc_start;
		}
		if (pndl == (LPFC_NODELIST_t *) & plhba->fc_adisc_start) {
			pndl = plhba->fc_nlpunmap_start;
		}
		if (pndl == (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start) {
			pndl = plhba->fc_nlpmap_start;
		}
	}
	if (cnt) {
		cip->elx_outsz = (uint32_t) (cnt * sizeof (NodeInfo));
	}

	return (rc);
}

int
lpfc_ioctl_hba_adapterattributes(elxHBA_t * phba,
				 ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	HBA_ADAPTERATTRIBUTES *ha;
	char *pNodeSymbolicName;
	char fwrev[32];
	struct dfc_mem NodeSymbolicName_dm;
	LPFCHBA_t *plhba;
	uint32_t incr;
	struct dfc_info *di;
	elx_vpd_t *vp;
	int rc = 0;
	int i, j = 0;		/* loop index */

	/* Allocate mboxq structure */
	NodeSymbolicName_dm.fc_dataout = NULL;
	if ((rc = dfc_data_alloc(phba, &NodeSymbolicName_dm, 256)))
		return (rc);
	else
		pNodeSymbolicName = (char *)NodeSymbolicName_dm.fc_dataout;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	di = &dfc.dfc_info[cip->elx_brd];
	vp = &phba->vpd;
	ha = (HBA_ADAPTERATTRIBUTES *) dm->fc_dataout;
	memset(dm->fc_dataout, 0, (sizeof (HBA_ADAPTERATTRIBUTES)));
	ha->NumberOfPorts = 1;
	ha->VendorSpecificID = di->fc_ba.a_pci;
	memcpy(ha->DriverVersion, di->fc_ba.a_drvrid, 16);
	lpfc_decode_firmware_rev(phba, fwrev, 1);
	memcpy(ha->FirmwareVersion, fwrev, 32);
	memcpy((uint8_t *) & ha->NodeWWN,
	       (uint8_t *) & plhba->fc_sparam.nodeName, sizeof (HBA_WWN));
	memcpy(ha->Manufacturer, "Emulex Corporation", 20);

	lpfc_get_hba_model_desc(phba, (uint8_t *) ha->Model,
				(uint8_t *) ha->ModelDescription);

	memcpy(ha->DriverName, "lpfcdd", 7);
	memcpy(ha->SerialNumber, phba->SerialNumber, 32);
	memcpy(ha->OptionROMVersion, phba->OptionROMVersion, 32);
	/* Convert JEDEC ID to ascii for hardware version */
	incr = vp->rev.biuRev;
	for (i = 0; i < 8; i++) {
		j = (incr & 0xf);
		if (j <= 9)
			ha->HardwareVersion[7 - i] =
			    (char)((uint8_t) 0x30 + (uint8_t) j);
		else
			ha->HardwareVersion[7 - i] =
			    (char)((uint8_t) 0x61 + (uint8_t) (j - 10));
		incr = (incr >> 4);
	}
	ha->HardwareVersion[8] = 0;

	lpfc_get_hba_SymbNodeName(phba, (uint8_t *) pNodeSymbolicName);
	memcpy(ha->NodeSymbolicName, pNodeSymbolicName, 256);

	/* Free allocated block of memory */
	if (pNodeSymbolicName)
		dfc_data_free(phba, &NodeSymbolicName_dm);

	return (rc);
}

int
lpfc_ioctl_hba_portattributes(elxHBA_t * phba,
			      ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{

	HBA_PORTATTRIBUTES *hp;
	LPFCHBA_t *plhba;
	SERV_PARM *hsp;
	HBA_OSDN *osdn;
	elx_vpd_t *vp;
	uint32_t cnt;
	elxCfgParam_t *clp;
	int rc = 0;

	cnt = 0;
	clp = &phba->config[0];
	vp = &phba->vpd;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	hsp = (SERV_PARM *) (&plhba->fc_sparam);
	hp = (HBA_PORTATTRIBUTES *) dm->fc_dataout;

	memset(dm->fc_dataout, 0, (sizeof (HBA_PORTATTRIBUTES)));
	memcpy((uint8_t *) & hp->NodeWWN,
	       (uint8_t *) & plhba->fc_sparam.nodeName, sizeof (HBA_WWN));
	memcpy((uint8_t *) & hp->PortWWN,
	       (uint8_t *) & plhba->fc_sparam.portName, sizeof (HBA_WWN));
	if (plhba->fc_linkspeed == LA_2GHZ_LINK)
		hp->PortSpeed = HBA_PORTSPEED_2GBIT;
	else
		hp->PortSpeed = HBA_PORTSPEED_1GBIT;

	if (FC_JEDEC_ID(vp->rev.biuRev) == VIPER_JEDEC_ID)
		hp->PortSupportedSpeed = HBA_PORTSPEED_10GBIT;
	else if ((FC_JEDEC_ID(vp->rev.biuRev) == CENTAUR_2G_JEDEC_ID) ||
		 (FC_JEDEC_ID(vp->rev.biuRev) == PEGASUS_JEDEC_ID) ||
		 (FC_JEDEC_ID(vp->rev.biuRev) == THOR_JEDEC_ID))
		hp->PortSupportedSpeed = HBA_PORTSPEED_2GBIT;
	else
		hp->PortSupportedSpeed = HBA_PORTSPEED_1GBIT;

	hp->PortFcId = plhba->fc_myDID;
	hp->PortType = HBA_PORTTYPE_UNKNOWN;
	if (plhba->fc_topology == TOPOLOGY_LOOP) {
		if (plhba->fc_flag & FC_PUBLIC_LOOP) {
			hp->PortType = HBA_PORTTYPE_NLPORT;
			memcpy((uint8_t *) & hp->FabricName,
			       (uint8_t *) & plhba->fc_fabparam.nodeName,
			       sizeof (HBA_WWN));
		} else {
			hp->PortType = HBA_PORTTYPE_LPORT;
		}
	} else {
		if (plhba->fc_flag & FC_FABRIC) {
			hp->PortType = HBA_PORTTYPE_NPORT;
			memcpy((uint8_t *) & hp->FabricName,
			       (uint8_t *) & plhba->fc_fabparam.nodeName,
			       sizeof (HBA_WWN));
		} else {
			hp->PortType = HBA_PORTTYPE_PTP;
		}
	}

	if (plhba->fc_flag & FC_BYPASSED_MODE) {
		hp->PortState = HBA_PORTSTATE_BYPASSED;
	} else if (plhba->fc_flag & FC_OFFLINE_MODE) {
		hp->PortState = HBA_PORTSTATE_DIAGNOSTICS;
	} else {
		switch (phba->hba_state) {
		case ELX_INIT_START:
		case ELX_INIT_MBX_CMDS:
			hp->PortState = HBA_PORTSTATE_UNKNOWN;
			break;
		case ELX_LINK_DOWN:
		case ELX_LINK_UP:
		case ELX_LOCAL_CFG_LINK:
		case ELX_FLOGI:
		case ELX_FABRIC_CFG_LINK:
		case ELX_NS_REG:
		case ELX_NS_QRY:
		case ELX_BUILD_DISC_LIST:
		case ELX_DISC_AUTH:
		case ELX_CLEAR_LA:
			hp->PortState = HBA_PORTSTATE_LINKDOWN;
			break;
		case ELX_HBA_READY:
			hp->PortState = HBA_PORTSTATE_ONLINE;
			break;
		case ELX_HBA_ERROR:
		default:
			hp->PortState = HBA_PORTSTATE_ERROR;
			break;
		}
	}
	cnt = plhba->fc_map_cnt + plhba->fc_unmap_cnt;
	hp->NumberofDiscoveredPorts = cnt;
	if (hsp->cls1.classValid) {
		hp->PortSupportedClassofService |= 1;	/* bit 1 */
	}
	if (hsp->cls2.classValid) {
		hp->PortSupportedClassofService |= 2;	/* bit 2 */
	}
	if (hsp->cls3.classValid) {
		hp->PortSupportedClassofService |= 4;	/* bit 3 */
	}
	hp->PortMaxFrameSize = (((uint32_t) hsp->cmn.bbRcvSizeMsb) << 8) |
	    (uint32_t) hsp->cmn.bbRcvSizeLsb;

	hp->PortSupportedFc4Types.bits[2] = 0x1;
	hp->PortSupportedFc4Types.bits[3] = 0x20;
	hp->PortSupportedFc4Types.bits[7] = 0x1;
	hp->PortActiveFc4Types.bits[2] = 0x1;

	if (clp[LPFC_CFG_NETWORK_ON].a_current) {
		hp->PortActiveFc4Types.bits[3] = 0x20;
	}
	hp->PortActiveFc4Types.bits[7] = 0x1;

	/* OSDeviceName is the device info filled into the HBA_OSDN structure */
	osdn = (HBA_OSDN *) & hp->OSDeviceName[0];
	memcpy(osdn->drvname, "lpfc", 4);
	osdn->instance = lpfc_instance[phba->brd_no];
	osdn->target = (uint32_t) (-1);
	osdn->lun = (uint32_t) (-1);

	return (rc);
}

int
lpfc_ioctl_hba_portstatistics(elxHBA_t * phba,
			      ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{

	HBA_PORTSTATISTICS *hs;
	LPFCHBA_t *plhba;
	ELX_MBOXQ_t *pmboxq;
	MAILBOX_t *pmb;
	unsigned long iflag;
	int rc = 0;

	if ((pmboxq =
	     (ELX_MBOXQ_t *) elx_mem_get(phba, MEM_MBOX | MEM_PRI)) == 0) {
		return ENOMEM;
	}

	pmb = &pmboxq->mb;

	plhba = (LPFCHBA_t *) phba->pHbaProto;

	hs = (HBA_PORTSTATISTICS *) dm->fc_dataout;
	memset(dm->fc_dataout, 0, (sizeof (HBA_PORTSTATISTICS)));
	memset((void *)pmboxq, 0, sizeof (ELX_MBOXQ_t));
	pmb->mbxCommand = MBX_READ_STATUS;
	pmb->mbxOwner = OWN_HOST;
	pmboxq->context1 = (uint8_t *) 0;

	if (plhba->fc_flag & FC_OFFLINE_MODE) {
		ELX_DRVR_UNLOCK(phba, iflag);
		rc = elx_sli_issue_mbox(phba, pmboxq, MBX_POLL);
		ELX_DRVR_LOCK(phba, iflag);
	} else
		rc = elx_sli_issue_mbox_wait(phba, pmboxq, plhba->fc_ratov * 2);
	if (rc != MBX_SUCCESS) {
		if (pmboxq) {
			if (rc == MBX_TIMEOUT) {
				/*
				 * Let SLI layer to release mboxq if mbox command completed after timeout.
				 */
				pmboxq->mbox_cmpl = 0;
			} else {
				elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmboxq);
			}
		}
		rc = ENODEV;
		return (rc);
	}
	hs->TxFrames = pmb->un.varRdStatus.xmitFrameCnt;
	hs->RxFrames = pmb->un.varRdStatus.rcvFrameCnt;
	/* Convert KBytes to words */
	hs->TxWords = (pmb->un.varRdStatus.xmitByteCnt * 256);
	hs->RxWords = (pmb->un.varRdStatus.rcvByteCnt * 256);
	memset((void *)pmboxq, 0, sizeof (ELX_MBOXQ_t));
	pmb->mbxCommand = MBX_READ_LNK_STAT;
	pmb->mbxOwner = OWN_HOST;
	pmboxq->context1 = (uint8_t *) 0;

	if (plhba->fc_flag & FC_OFFLINE_MODE) {
		ELX_DRVR_UNLOCK(phba, iflag);
		rc = elx_sli_issue_mbox(phba, pmboxq, MBX_POLL);
		ELX_DRVR_LOCK(phba, iflag);
	} else
		rc = elx_sli_issue_mbox_wait(phba, pmboxq, plhba->fc_ratov * 2);
	if (rc != MBX_SUCCESS) {
		if (pmboxq) {
			if (rc == MBX_TIMEOUT) {
				/*
				 * Let SLI layer to release mboxq if mbox command completed after timeout.
				 */
				pmboxq->mbox_cmpl = 0;
			} else {
				elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmboxq);
			}
		}
		rc = ENODEV;
		return (rc);
	}
	hs->LinkFailureCount = pmb->un.varRdLnk.linkFailureCnt;
	hs->LossOfSyncCount = pmb->un.varRdLnk.lossSyncCnt;
	hs->LossOfSignalCount = pmb->un.varRdLnk.lossSignalCnt;
	hs->PrimitiveSeqProtocolErrCount = pmb->un.varRdLnk.primSeqErrCnt;
	hs->InvalidTxWordCount = pmb->un.varRdLnk.invalidXmitWord;
	hs->InvalidCRCCount = pmb->un.varRdLnk.crcCnt;
	hs->ErrorFrames = pmb->un.varRdLnk.crcCnt;

	if (plhba->fc_topology == TOPOLOGY_LOOP) {
		hs->LIPCount = (plhba->fc_eventTag >> 1);
		hs->NOSCount = -1;
	} else {
		hs->LIPCount = -1;
		hs->NOSCount = (plhba->fc_eventTag >> 1);
	}

	hs->DumpedFrames = -1;

	hs->SecondsSinceLastReset = elxDRVR.elx_clock_info.ticks;

	/* Free allocated mboxq memory */
	if (pmboxq) {
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmboxq);
	}

	return (rc);
}

int
lpfc_ioctl_hba_wwpnportattributes(elxHBA_t * phba,
				  ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	HBA_WWN findwwn;
	LPFC_NODELIST_t *pndl;
	HBA_PORTATTRIBUTES *hp;
	LPFCHBA_t *plhba;
	elx_vpd_t *vp;
	MAILBOX_t *pmbox;
	struct dfc_mem mbox_dm;
	int rc = 0;
	unsigned long iflag;

	/* Allocate mboxq structure */
	mbox_dm.fc_dataout = NULL;
	if ((rc = dfc_data_alloc(phba, &mbox_dm, sizeof (MAILBOX_t))))
		return (rc);
	else
		pmbox = (MAILBOX_t *) mbox_dm.fc_dataout;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	hp = (HBA_PORTATTRIBUTES *) dm->fc_dataout;
	vp = &phba->vpd;
	memset(dm->fc_dataout, 0, (sizeof (HBA_PORTATTRIBUTES)));

	ELX_DRVR_UNLOCK(phba, iflag);
	if (copy_from_user((uint8_t *) & findwwn, (uint8_t *) cip->elx_arg1,
			   (ulong) (sizeof (HBA_WWN)))) {
		ELX_DRVR_LOCK(phba, iflag);
		rc = EIO;
		/* Free allocated mbox memory */
		if (pmbox)
			dfc_data_free(phba, &mbox_dm);
		return (rc);
	}
	ELX_DRVR_LOCK(phba, iflag);

	/* First Mapped ports, then unMapped ports */
	pndl = plhba->fc_nlpmap_start;
	if (pndl == (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start)
		pndl = plhba->fc_nlpunmap_start;
	while (pndl != (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start) {
		if (lpfc_geportname
		    (&pndl->nlp_portname, (NAME_TYPE *) & findwwn) == 2) {
			/* handle found port */
			rc = lpfc_ioctl_found_port(phba, plhba, pndl, dm, pmbox,
						   hp);
			/* Free allocated mbox memory */
			if (pmbox)
				dfc_data_free(phba, &mbox_dm);
			return (rc);
		}
		pndl = (LPFC_NODELIST_t *) pndl->nle.nlp_listp_next;
		if (pndl == (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start)
			pndl = plhba->fc_nlpunmap_start;
	}

	/* Free allocated mbox memory */
	if (pmbox)
		dfc_data_free(phba, &mbox_dm);

	rc = ERANGE;
	return (rc);
}

int
lpfc_ioctl_hba_discportattributes(elxHBA_t * phba,
				  ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	HBA_PORTATTRIBUTES *hp;
	LPFCHBA_t *plhba;
	LPFC_NODELIST_t *pndl;
	elx_vpd_t *vp;
	ELX_SLI_t *psli;
	uint32_t refresh, offset, cnt;
	MAILBOX_t *pmbox;
	struct dfc_mem mbox_dm;
	int rc = 0;

	/* Allocate mboxq structure */
	mbox_dm.fc_dataout = NULL;
	if ((rc = dfc_data_alloc(phba, &mbox_dm, sizeof (MAILBOX_t))))
		return (rc);
	else
		pmbox = (MAILBOX_t *) mbox_dm.fc_dataout;

	psli = &phba->sli;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	hp = (HBA_PORTATTRIBUTES *) dm->fc_dataout;
	vp = &phba->vpd;
	memset(dm->fc_dataout, 0, (sizeof (HBA_PORTATTRIBUTES)));
	offset = (uint32_t) ((ulong) cip->elx_arg2);
	refresh = (uint32_t) ((ulong) cip->elx_arg3);
	if (refresh != plhba->nlptimer) {
		/* This is an error, need refresh, just return zero'ed out
		 * portattr and FcID as -1.
		 */
		hp->PortFcId = 0xffffffff;
		return (rc);
	}
	cnt = 0;
	/* First Mapped ports, then unMapped ports */
	pndl = plhba->fc_nlpmap_start;
	if (pndl == (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start)
		pndl = plhba->fc_nlpunmap_start;
	while (pndl != (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start) {
		if (cnt == offset) {
			/* handle found port */
			rc = lpfc_ioctl_found_port(phba, plhba, pndl, dm, pmbox,
						   hp);
			/* Free allocated mbox memory */
			if (pmbox)
				dfc_data_free(phba, &mbox_dm);
			return (rc);
		}
		cnt++;
		pndl = (LPFC_NODELIST_t *) pndl->nle.nlp_listp_next;
		if (pndl == (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start)
			pndl = plhba->fc_nlpunmap_start;
	}
	rc = ERANGE;

	/* Free allocated mbox memory */
	if (pmbox)
		dfc_data_free(phba, &mbox_dm);
	return (rc);
}

int
lpfc_ioctl_hba_indexportattributes(elxHBA_t * phba,
				   ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	HBA_PORTATTRIBUTES *hp;
	elx_vpd_t *vp;
	LPFC_NODELIST_t *pndl;
	uint32_t refresh, offset, cnt;
	MAILBOX_t *pmbox;
	struct dfc_mem mbox_dm;
	LPFCHBA_t *plhba;
	int rc = 0;

	/* Allocate mboxq structure */
	mbox_dm.fc_dataout = NULL;
	if ((rc = dfc_data_alloc(phba, &mbox_dm, sizeof (MAILBOX_t))))
		return (rc);
	else
		pmbox = (MAILBOX_t *) mbox_dm.fc_dataout;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	vp = &phba->vpd;
	hp = (HBA_PORTATTRIBUTES *) dm->fc_dataout;
	memset(dm->fc_dataout, 0, (sizeof (HBA_PORTATTRIBUTES)));
	offset = (uint32_t) ((ulong) cip->elx_arg2);
	refresh = (uint32_t) ((ulong) cip->elx_arg3);
	if (refresh != plhba->nlptimer) {
		/* This is an error, need refresh, just return zero'ed out
		 * portattr and FcID as -1.
		 */
		hp->PortFcId = 0xffffffff;

		/* Free allocated mbox memory */
		if (pmbox)
			dfc_data_free(phba, &mbox_dm);

		return (rc);
	}
	cnt = 0;
	/* Mapped NPorts only */
	pndl = plhba->fc_nlpmap_start;
	while (pndl != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start) {
		if (cnt == offset) {
			/* handle found port */
			rc = lpfc_ioctl_found_port(phba, plhba, pndl, dm, pmbox,
						   hp);
			/* Free allocated mbox memory */
			if (pmbox)
				dfc_data_free(phba, &mbox_dm);
			return (rc);
		}
		cnt++;
		pndl = (LPFC_NODELIST_t *) pndl->nle.nlp_listp_next;
	}

	/* Free allocated mbox memory */
	if (pmbox)
		dfc_data_free(phba, &mbox_dm);

	rc = ERANGE;
	return (rc);
}

int
lpfc_ioctl_hba_setmgmtinfo(elxHBA_t * phba,
			   ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{

	HBA_MGMTINFO *mgmtinfo;
	LPFCHBA_t *plhba = (LPFCHBA_t *) phba->pHbaProto;
	int rc = 0;
	unsigned long iflag;
	struct dfc_mem dm_buf;

	dm_buf.fc_dataout = NULL;

	if ((rc = dfc_data_alloc(phba, &dm_buf, 4096))) {
		return (rc);
	} else {
		mgmtinfo = (HBA_MGMTINFO *) dm_buf.fc_dataout;
	}

	ELX_DRVR_UNLOCK(phba, iflag);
	if (copy_from_user
	    ((uint8_t *) mgmtinfo, (uint8_t *) cip->elx_arg1,
	     sizeof (HBA_MGMTINFO))) {
		ELX_DRVR_LOCK(phba, iflag);
		rc = EIO;
		dfc_data_free(phba, &dm_buf);
		return (rc);
	}
	ELX_DRVR_LOCK(phba, iflag);

	/* Can ONLY set UDP port and IP Address */
	plhba->ipVersion = mgmtinfo->IPVersion;
	plhba->UDPport = mgmtinfo->UDPPort;
	if (plhba->ipVersion == RNID_IPV4) {
		memcpy((uint8_t *) & plhba->ipAddr[0],
		       (uint8_t *) & mgmtinfo->IPAddress[0], 4);
	} else {
		memcpy((uint8_t *) & plhba->ipAddr[0],
		       (uint8_t *) & mgmtinfo->IPAddress[0], 16);
	}

	dfc_data_free(phba, &dm_buf);
	return (rc);
}

int
lpfc_ioctl_hba_getmgmtinfo(elxHBA_t * phba,
			   ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{

	HBA_MGMTINFO *mgmtinfo;
	LPFCHBA_t *plhba = (LPFCHBA_t *) phba->pHbaProto;
	int rc = 0;

	mgmtinfo = (HBA_MGMTINFO *) dm->fc_dataout;
	memcpy((uint8_t *) & mgmtinfo->wwn, (uint8_t *) & plhba->fc_nodename,
	       8);
	mgmtinfo->unittype = RNID_HBA;
	mgmtinfo->PortId = plhba->fc_myDID;
	mgmtinfo->NumberOfAttachedNodes = 0;
	mgmtinfo->TopologyDiscoveryFlags = 0;
	mgmtinfo->IPVersion = plhba->ipVersion;
	mgmtinfo->UDPPort = plhba->UDPport;
	if (plhba->ipVersion == RNID_IPV4) {
		memcpy((void *)&mgmtinfo->IPAddress[0],
		       (void *)&plhba->ipAddr[0], 4);
	} else {
		memcpy((void *)&mgmtinfo->IPAddress[0],
		       (void *)&plhba->ipAddr[0], 16);
	}

	return (rc);
}

int
lpfc_ioctl_hba_refreshinfo(elxHBA_t * phba,
			   ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	uint32_t *lptr;
	LPFCHBA_t *plhba;
	int rc = 0;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	lptr = (uint32_t *) dm->fc_dataout;
	*lptr = plhba->nlptimer;

	return (rc);
}

int
lpfc_ioctl_hba_rnid(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{

	HBA_WWN idn;
	ELX_SLI_t *psli;
	ELX_IOCBQ_t *cmdiocbq = 0;
	ELX_IOCBQ_t *rspiocbq = 0;
	RNID *prsp;
	uint32_t *pcmd;
	uint32_t *psta;
	IOCB_t *rsp;
	ELX_SLI_RING_t *pring;
	void *context2;		/* both prep_iocb and iocb_wait use this */
	int i0;
	uint16_t siz;
	unsigned long iflag;
	int rtnbfrsiz;
	LPFC_NODELIST_t *pndl;
	LPFCHBA_t *plhba;
	int rc = 0;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	ELX_DRVR_UNLOCK(phba, iflag);
	if (copy_from_user((uint8_t *) & idn, (uint8_t *) cip->elx_arg1,
			   (ulong) (sizeof (HBA_WWN)))) {
		rc = EIO;
		ELX_DRVR_LOCK(phba, iflag);
		return (rc);
	}
	ELX_DRVR_LOCK(phba, iflag);

	if (cip->elx_flag == NODE_WWN) {
		pndl =
		    lpfc_findnode_wwnn(phba,
				       NLP_SEARCH_MAPPED | NLP_SEARCH_UNMAPPED,
				       (NAME_TYPE *) & idn);
	} else {
		pndl =
		    lpfc_findnode_wwpn(phba,
				       NLP_SEARCH_MAPPED | NLP_SEARCH_UNMAPPED,
				       (NAME_TYPE *) & idn);
	}
	if (!pndl) {
		rc = ENODEV;
		goto sndrndqwt;
	}
	for (i0 = 0;
	     i0 < 10 && (pndl->nlp_flag & NLP_ELS_SND_MASK) == NLP_RNID_SND;
	     i0++) {
		iflag = phba->iflag;
		ELX_DRVR_UNLOCK(phba, iflag);
		mdelay(1000);
		ELX_DRVR_LOCK(phba, iflag);
	}
	if (i0 == 10) {
		rc = EACCES;
		pndl->nlp_flag &= ~NLP_RNID_SND;
		goto sndrndqwt;
	}

	siz = 2 * sizeof (uint32_t);
	/*  lpfc_prep_els_iocb sets the following: */

	if (!
	    (cmdiocbq =
	     lpfc_prep_els_iocb(phba, 1, siz, 0, pndl, ELS_CMD_RNID))) {
		rc = ENOMEM;
		goto sndrndqwt;
	}
    /************************************************************************/
	/*  context2 is used by prep/free to locate cmd and rsp buffers,   */
	/*  but context2 is also used by iocb_wait to hold a rspiocb ptr, so    */
	/*  the rsp iocbq can be returned from the completion routine for       */
	/*  iocb_wait, so, save the prep/free value locally ... it will be      */
	/*  restored after returning from iocb_wait.                            */
    /************************************************************************/
	context2 = cmdiocbq->context2;	/* needed to use lpfc_els_free_iocb */
	if ((rspiocbq =
	     (ELX_IOCBQ_t *) elx_mem_get(phba, MEM_IOCB | MEM_PRI)) == 0) {
		rc = ENOMEM;
		goto sndrndqwt;
	}
	memset((void *)rspiocbq, 0, sizeof (ELX_IOCBQ_t));
	rsp = &(rspiocbq->iocb);

	pcmd = (uint32_t *) (((DMABUF_t *) cmdiocbq->context2)->virt);
	*pcmd++ = ELS_CMD_RNID;
	memset((void *)pcmd, 0, sizeof (RNID));	/* fill in RNID payload */
	((RNID *) pcmd)->Format = 0;	/* following makes it more interesting */
	((RNID *) pcmd)->Format = RNID_TOPOLOGY_DISC;
	cmdiocbq->context1 = (uint8_t *) 0;
	cmdiocbq->context2 = (uint8_t *) 0;
	cmdiocbq->iocb_flag |= ELX_IO_IOCTL;
	for (rc = -1, i0 = 0; i0 < 4 && rc != IOCB_SUCCESS; i0++) {
		pndl->nlp_flag |= NLP_RNID_SND;
		rc = elx_sli_issue_iocb_wait(phba, pring, cmdiocbq,
					     SLI_IOCB_USE_TXQ, rspiocbq,
					     (plhba->fc_ratov * 2) +
					     ELX_DRVR_TIMEOUT);
		pndl->nlp_flag &= ~NLP_RNID_SND;
		cmdiocbq->context2 = context2;
		if (rc == IOCB_ERROR) {
			rc = EACCES;
			goto sndrndqwt;
		}
	}
	if (rc != IOCB_SUCCESS) {
		goto sndrndqwt;
	}

	if (rsp->ulpStatus) {
		rc = EACCES;
	} else {
		psta =
		    (uint32_t
		     *) ((DMABUF_t *) (((DMABUF_t *) cmdiocbq->context2)->
				       next)->virt);
		prsp = (RNID *) (psta + 1);	/*  then rnid response data */
		if (*psta != ELS_CMD_ACC) {
			rc = EFAULT;
			goto sndrndqwt;
		}
		rtnbfrsiz = prsp->CommonLen + prsp->SpecificLen;
		memcpy((uint8_t *) dm->fc_dataout, (uint8_t *) prsp, rtnbfrsiz);
		ELX_DRVR_UNLOCK(phba, iflag);
		if (copy_to_user
		    ((uint8_t *) cip->elx_arg2, (uint8_t *) & rtnbfrsiz,
		     sizeof (int)))
			rc = EIO;
		ELX_DRVR_LOCK(phba, iflag);
	}
      sndrndqwt:
	if (cmdiocbq)
		lpfc_els_free_iocb(phba, cmdiocbq);

	return (rc);
}

int
lpfc_ioctl_hba_getevent(elxHBA_t * phba,
			ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{

	uint32_t outsize, size;
	HBAEVT_t *rec;
	HBAEVT_t *recout;
	LPFCHBA_t *plhba;
	int j, rc = 0;
	unsigned long iflag;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	size = (uint32_t) ((ulong) cip->elx_arg1);	/* size is number of event entries */

	recout = (HBAEVT_t *) dm->fc_dataout;
	for (j = 0; j < MAX_HBAEVT; j++) {
		if ((j == (int)size) ||
		    (plhba->hba_event_get == plhba->hba_event_put))
			return (rc);
		rec = &plhba->hbaevt[plhba->hba_event_get];
		memcpy((uint8_t *) recout, (uint8_t *) rec, sizeof (HBAEVT_t));
		recout++;
		plhba->hba_event_get++;
		if (plhba->hba_event_get >= MAX_HBAEVT) {
			plhba->hba_event_get = 0;
		}
	}
	outsize = j;

	ELX_DRVR_UNLOCK(phba, iflag);
	/* copy back size of response */
	if (copy_to_user((uint8_t *) cip->elx_arg2, (uint8_t *) & outsize,
			 sizeof (uint32_t))) {
		rc = EIO;
		ELX_DRVR_LOCK(phba, iflag);
		return (rc);
	}

	/* copy back number of missed records */
	if (copy_to_user
	    ((uint8_t *) cip->elx_arg3, (uint8_t *) & plhba->hba_event_missed,
	     sizeof (uint32_t))) {
		rc = EIO;
		ELX_DRVR_LOCK(phba, iflag);
		return (rc);
	}
	ELX_DRVR_LOCK(phba, iflag);

	plhba->hba_event_missed = 0;
	cip->elx_outsz = (uint32_t) (outsize * sizeof (HBA_EVENTINFO));

	return (rc);
}

int
lpfc_ioctl_hba_fcptargetmapping(elxHBA_t * phba,
				ELXCMDINPUT_t * cip,
				struct dfc_mem *dm, int *do_cp)
{

	uint32_t room = (uint32_t) ((ulong) cip->elx_arg1);
	uint32_t total = 0;
	uint64_t lunidx;
	uint64_t lunidn;
	uint32_t lunttl;
	int rptlunlen;
	int count = 0;
	int pansid;
	HBA_FCPTARGETMAPPING *hf = (HBA_FCPTARGETMAPPING *) dm->fc_dataout;
	HBA_FCPSCSIENTRY *ep = &hf->entry[0];
	ELXSCSILUN_t *plun;
	ELXSCSITARGET_t *pscznod;
	LPFCHBA_t *plhba;
	MBUF_INFO_t *pbfrnfo;
	LPFC_NODELIST_t *pndl;
	int rc = 0;
	struct dfc_mem mbuf_dm;

	/* Allocate mbuf structure */
	mbuf_dm.fc_dataout = NULL;
	if ((rc = dfc_data_alloc(phba, &mbuf_dm, sizeof (MBUF_INFO_t))))
		return (rc);
	else
		pbfrnfo = (MBUF_INFO_t *) mbuf_dm.fc_dataout;

	plhba = (LPFCHBA_t *) phba->pHbaProto;

	/* Mapped ports only */
	memset(pbfrnfo, 0, sizeof (MBUF_INFO_t));
	pndl = plhba->fc_nlpmap_start;
	while (pndl != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start) {
		pansid = FC_SCSID(pndl->nlp_pan, pndl->nlp_sid);
		if (pansid > MAX_FCP_TARGET) {
			pndl = (LPFC_NODELIST_t *) pndl->nle.nlp_listp_next;
			continue;
		}
		pscznod = plhba->device_queue_hash[pansid];
		if (!pscznod) {
			continue;
		}

		/* Detect a config change per device by issuing a REPORT_LUN for all 
		 * devices on the map list. 
		 */
		if (lpfc_issue_rptlun(phba, pbfrnfo, pscznod)) {
			ELX_FREE(phba, pbfrnfo);
			pndl = (LPFC_NODELIST_t *) pndl->nle.nlp_listp_next;
			continue;
		}
		rptlunlen = SWAP_DATA(*((uint32_t *) pbfrnfo->virt));
		lunttl = (rptlunlen > 8) ? rptlunlen / 8 : 1;
		for (lunidx = 0; lunidx < lunttl; lunidx++) {
			lunidn = dfc_getLunId(pbfrnfo,
					      (ELXSCSILUN_t *) & pscznod->
					      lunlist.q_first, lunidx);
			plun =
			    lpfc_find_lun(phba, pansid, (uint64_t) lunidn, 0);
			if (!plun || (plun && !plun->failMask)) {
				if (count < room) {
					HBA_OSDN *osdn;
					uint32_t fcpLun[2];

					memset((void *)ep->ScsiId.OSDeviceName,
					       0, 256);
					/* OSDeviceName is device info filled into HBA_OSDN */
					osdn =
					    (HBA_OSDN *) & ep->ScsiId.
					    OSDeviceName[0];
					memcpy(osdn->drvname, "lpfc", 4);
					osdn->instance =
					    lpfc_instance[phba->brd_no];
					osdn->target = pansid;
					osdn->lun = (uint32_t) (lunidn);
					osdn->flags = 0;
					ep->ScsiId.ScsiTargetNumber = pansid;
					ep->ScsiId.ScsiOSLun =
					    (uint32_t) (lunidn);
					ep->ScsiId.ScsiBusNumber = 0;
					ep->FcpId.FcId = pndl->nlp_DID;
					memset((char *)fcpLun, 0,
					       sizeof (HBA_UINT64));
					fcpLun[0] = (lunidn << FC_LUN_SHIFT);
					if (pscznod->addrMode ==
					    VOLUME_SET_ADDRESSING) {
						fcpLun[0] |=
						    SWAP_DATA(0x40000000);
					}
					memcpy((char *)&ep->FcpId.FcpLun,
					       (char *)&fcpLun[0],
					       sizeof (HBA_UINT64));
					memcpy((uint8_t *) & ep->FcpId.PortWWN,
					       &pndl->nlp_portname,
					       sizeof (HBA_WWN));
					memcpy((uint8_t *) & ep->FcpId.NodeWWN,
					       &pndl->nlp_nodename,
					       sizeof (HBA_WWN));
					count++;
					ep++;
				}
				total++;
			}
		}
		ELX_FREE(phba, pbfrnfo);
		pndl = (LPFC_NODELIST_t *) pndl->nle.nlp_listp_next;
	}
	hf->NumberOfEntries = (uint32_t) total;
	cip->elx_outsz = count * sizeof (HBA_FCPSCSIENTRY) + sizeof (ulong);
	if (total > room) {
		rc = ERANGE;
		*do_cp = 1;
	}

	/* Free allocated mbuf memory */
	if (pbfrnfo)
		dfc_data_free(phba, &mbuf_dm);

	return (rc);
}

int
lpfc_ioctl_hba_fcpbinding(elxHBA_t * phba,
			  ELXCMDINPUT_t * cip, struct dfc_mem *dm, int *do_cp)
{
	struct lpfc_fcpbinding_context {

		uint32_t room;
		uint32_t ttl;
		uint32_t lunidx;
		uint32_t lunidn;
		uint32_t lunttl;
		uint32_t pansid;
		uint32_t fcpLun[2];
		int rptlunlen;
		int memsz, cnt;
		MBUF_INFO_t bfrnfo;
	};

	int rc;
	char *appPtr = ((char *)cip->elx_dataout) + sizeof (ulong);
	HBA_FCPBINDING *hb = (HBA_FCPBINDING *) dm->fc_dataout;
	HBA_FCPBINDINGENTRY *ep = &hb->entry[0];
	ELXSCSILUN_t *plun;
	ELXSCSITARGET_t *pscznod;
	LPFC_BINDLIST_t *pbdl;
	LPFCHBA_t *plhba;
	MBUF_INFO_t *pbfrnfo;
	LPFC_NODELIST_t *pndl;
	HBA_OSDN *osdn;
	struct lpfc_fcpbinding_context *ctxt;
	struct dfc_mem dfc_mem_struct;
	uint32_t total_mem = dm->fc_outsz;
	unsigned long iflag;

	dfc_mem_struct.fc_dataout = 0;

	if ((rc =
	     dfc_data_alloc(phba, &dfc_mem_struct,
			    sizeof (struct lpfc_fcpbinding_context))))
		return (rc);
	else
		ctxt =
		    (struct lpfc_fcpbinding_context *)dfc_mem_struct.fc_dataout;

	ctxt->memsz = 0;
	ctxt->cnt = 0;
	ctxt->room = (uint32_t) ((ulong) cip->elx_arg1);
	ctxt->ttl = 0;

	pbfrnfo = &(ctxt->bfrnfo);
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	/* First Mapped ports */
	memset(pbfrnfo, 0, sizeof (MBUF_INFO_t));
	pndl = plhba->fc_nlpmap_start;
	while (pndl != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start) {
		if (pndl->nlp_flag & NLP_SEED_MASK) {
			ctxt->pansid = FC_SCSID(pndl->nlp_pan, pndl->nlp_sid);
			if (ctxt->pansid > MAX_FCP_TARGET) {
				pndl =
				    (LPFC_NODELIST_t *) pndl->nle.
				    nlp_listp_next;
				continue;
			}
			pscznod = plhba->device_queue_hash[ctxt->pansid];
			if (!pscznod) {
				continue;
			}
	  /******************************************************************/
			/* do report lun scsi command in order to get fresh data on lun   */
			/*  configuration for the particular target of interest;  such    */
			/*  could have changed after driver discovery on certain arrays.  */
	  /******************************************************************/
			if ((lpfc_issue_rptlun(phba, pbfrnfo, pscznod))) {
				ELX_FREE(phba, pbfrnfo);
				pndl =
				    (LPFC_NODELIST_t *) pndl->nle.
				    nlp_listp_next;
				continue;
			}
			ctxt->rptlunlen =
			    SWAP_DATA(*((uint32_t *) pbfrnfo->virt));
			ctxt->lunttl =
			    (ctxt->rptlunlen >
			     8 ? SWAP_DATA(*(uint32_t *) pbfrnfo->virt) /
			     8 : 1);
			for (ctxt->lunidx = 0; ctxt->lunidx < ctxt->lunttl;
			     ctxt->lunidx++, ctxt->ttl++) {
				ctxt->lunidn =
				    dfc_getLunId(pbfrnfo,
						 (ELXSCSILUN_t *) & pscznod->
						 lunlist.q_first, ctxt->lunidx);
				plun =
				    lpfc_find_lun(phba, ctxt->pansid,
						  (uint64_t) ctxt->lunidn, 0);
				if (ctxt->cnt < ctxt->room) {
					/* if not enuf space left then we have to copy what we 
					 *  have now back to application space, before reusing 
					 *  this buffer again.
					 */
					if (total_mem - ctxt->memsz <
					    sizeof (HBA_FCPBINDINGENTRY)) {
						ELX_DRVR_UNLOCK(phba, iflag);
						if (copy_to_user
						    ((uint8_t *) appPtr,
						     (uint8_t *) (&hb->
								  entry[0]),
						     ctxt->memsz)) {
							ELX_DRVR_LOCK(phba,
								      iflag);
							return EIO;
						}
						ELX_DRVR_LOCK(phba, iflag);
						appPtr = appPtr + ctxt->memsz;
						ep = &hb->entry[0];
						ctxt->memsz = 0;
					}
					memset((void *)ep->ScsiId.OSDeviceName,
					       0, 256);
					/* OSDeviceName is device info filled into HBA_OSDN */
					osdn =
					    (HBA_OSDN *) & ep->ScsiId.
					    OSDeviceName[0];
					memcpy(osdn->drvname, "lpfc", 4);
					osdn->instance =
					    lpfc_instance[phba->brd_no];
					osdn->target = ctxt->pansid;
					osdn->lun = (uint32_t) (ctxt->lunidn);
					ep->ScsiId.ScsiTargetNumber =
					    ctxt->pansid;
					ep->ScsiId.ScsiOSLun =
					    (uint32_t) (ctxt->lunidn);
					ep->ScsiId.ScsiBusNumber = 0;
					memset((char *)ctxt->fcpLun, 0,
					       sizeof (HBA_UINT64));
					ctxt->fcpLun[0] =
					    (ctxt->lunidn << FC_LUN_SHIFT);
					if (pscznod->addrMode ==
					    VOLUME_SET_ADDRESSING) {
						ctxt->fcpLun[0] |=
						    SWAP_DATA(0x40000000);
					}
					memcpy((char *)&ep->FcpId.FcpLun,
					       (char *)&ctxt->fcpLun[0],
					       sizeof (HBA_UINT64));

					if (pndl->nlp_flag & NLP_SEED_DID) {
						ep->type = TO_D_ID;
						ep->FcpId.FcId = pndl->nlp_DID;
						ep->FcId = pndl->nlp_DID;
						memset((uint8_t *) & ep->FcpId.
						       PortWWN, 0,
						       sizeof (HBA_WWN));
						memset((uint8_t *) & ep->FcpId.
						       NodeWWN, 0,
						       sizeof (HBA_WWN));
					} else {
						ep->type = TO_WWN;
						ep->FcId = 0;
						ep->FcpId.FcId = 0;
						if (pndl->
						    nlp_flag & NLP_SEED_WWPN) {
							memcpy((uint8_t *) &
							       ep->FcpId.
							       PortWWN,
							       &pndl->
							       nlp_portname,
							       sizeof
							       (HBA_WWN));
						} else {
							memcpy((uint8_t *) &
							       ep->FcpId.
							       NodeWWN,
							       &pndl->
							       nlp_nodename,
							       sizeof
							       (HBA_WWN));
						}
					}
					ep->FcpId.FcId = pndl->nlp_DID;
					memcpy((uint8_t *) & ep->FcpId.PortWWN,
					       &pndl->nlp_portname,
					       sizeof (HBA_WWN));
					memcpy((uint8_t *) & ep->FcpId.NodeWWN,
					       &pndl->nlp_nodename,
					       sizeof (HBA_WWN));
					ep++;
					ctxt->cnt++;
					ctxt->memsz =
					    ctxt->memsz +
					    sizeof (HBA_FCPBINDINGENTRY);
				}
			}
		}
		ELX_FREE(phba, pbfrnfo);
		pndl = (LPFC_NODELIST_t *) pndl->nle.nlp_listp_next;
	}			/* end searching mapped list */

	/* then unmapped ports */
	pndl = plhba->fc_nlpunmap_start;
	while (pndl != (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start) {
		if (pndl->nlp_flag & NLP_SEED_MASK) {
			ctxt->pansid = FC_SCSID(pndl->nlp_pan, pndl->nlp_sid);
			if (ctxt->pansid > MAX_FCP_TARGET) {
				continue;
			}
			pscznod = plhba->device_queue_hash[ctxt->pansid];
			if (!pscznod) {
				continue;
			}
			for (plun = (ELXSCSILUN_t *) & pscznod->lunlist.q_first;
			     plun != 0; plun = plun->pnextLun, ctxt->ttl++) {
				ctxt->lunidn = plun->lun_id;
				if (ctxt->cnt < ctxt->room) {
					/* if not enuf space left then we have to copy what we 
					 *  have now back to application space, before reusing 
					 *  this buffer again.
					 */
					if (total_mem - ctxt->memsz <
					    sizeof (HBA_FCPBINDINGENTRY)) {
						ELX_DRVR_UNLOCK(phba, iflag);
						if (copy_to_user
						    ((uint8_t *) appPtr,
						     (uint8_t *) (&hb->
								  entry[0]),
						     ctxt->memsz)) {
							ELX_DRVR_LOCK(phba,
								      iflag);
							return EIO;
						}
						ELX_DRVR_LOCK(phba, iflag);
						appPtr = appPtr + ctxt->memsz;
						ep = &hb->entry[0];
						ctxt->memsz = 0;
					}
					memset((void *)ep->ScsiId.OSDeviceName,
					       0, 256);
					/* OSDeviceName is device info filled into HBA_OSDN */
					osdn =
					    (HBA_OSDN *) & ep->ScsiId.
					    OSDeviceName[0];
					memcpy(osdn->drvname, "lpfc", 4);
					osdn->instance =
					    lpfc_instance[phba->brd_no];
					osdn->target = ctxt->pansid;
					osdn->lun = (uint32_t) (ctxt->lunidn);
					ep->ScsiId.ScsiTargetNumber =
					    ctxt->pansid;
					ep->ScsiId.ScsiOSLun =
					    (uint32_t) (ctxt->lunidn);
					ep->ScsiId.ScsiBusNumber = 0;
					memset((char *)ctxt->fcpLun, 0,
					       sizeof (HBA_UINT64));
					ctxt->fcpLun[0] =
					    (ctxt->lunidn << FC_LUN_SHIFT);
					if (pscznod->addrMode ==
					    VOLUME_SET_ADDRESSING) {
						ctxt->fcpLun[0] |=
						    SWAP_DATA(0x40000000);
					}
					memcpy((char *)&ep->FcpId.FcpLun,
					       (char *)&ctxt->fcpLun[0],
					       sizeof (HBA_UINT64));

					if (pndl->nlp_flag & NLP_SEED_DID) {
						ep->type = TO_D_ID;
						ep->FcpId.FcId = pndl->nlp_DID;
						ep->FcId = pndl->nlp_DID;
						memset((uint8_t *) & ep->FcpId.
						       PortWWN, 0,
						       sizeof (HBA_WWN));
						memset((uint8_t *) & ep->FcpId.
						       NodeWWN, 0,
						       sizeof (HBA_WWN));
					} else {
						ep->type = TO_WWN;
						ep->FcId = 0;
						ep->FcpId.FcId = 0;
						if (pndl->
						    nlp_flag & NLP_SEED_WWPN) {
							memcpy((uint8_t *) &
							       ep->FcpId.
							       PortWWN,
							       &pndl->
							       nlp_portname,
							       sizeof
							       (HBA_WWN));
						} else {
							memcpy((uint8_t *) &
							       ep->FcpId.
							       NodeWWN,
							       &pndl->
							       nlp_nodename,
							       sizeof
							       (HBA_WWN));
						}
					}
					ep->FcpId.FcId = pndl->nlp_DID;
					memcpy((uint8_t *) & ep->FcpId.PortWWN,
					       &pndl->nlp_portname,
					       sizeof (HBA_WWN));
					memcpy((uint8_t *) & ep->FcpId.NodeWWN,
					       &pndl->nlp_nodename,
					       sizeof (HBA_WWN));
					ep++;
					ctxt->cnt++;
					ctxt->memsz =
					    ctxt->memsz +
					    sizeof (HBA_FCPBINDINGENTRY);
				}
			}
		}
		pndl = (LPFC_NODELIST_t *) pndl->nle.nlp_listp_next;
	}			/* end searching unmapped list */

	/* search binding list */
	pbdl = plhba->fc_nlpbind_start;
	while (pbdl != (LPFC_BINDLIST_t *) & plhba->fc_nlpbind_start) {
		if (pbdl->nlp_bind_type & FCP_SEED_MASK) {
			ctxt->pansid = FC_SCSID(pbdl->nlp_pan, pbdl->nlp_sid);
			if (ctxt->pansid > MAX_FCP_TARGET) {
				pbdl = pbdl->nlp_listp_next;
				continue;
			}
			if (ctxt->cnt < ctxt->room) {
				/* if not enough space left then we have to copy what we 
				 *  have now back to application space, before reusing 
				 *  this buffer again.
				 */
				if (total_mem - ctxt->memsz <
				    sizeof (HBA_FCPBINDINGENTRY)) {
					ELX_DRVR_UNLOCK(phba, iflag);
					if (copy_to_user
					    ((uint8_t *) appPtr,
					     (uint8_t *) (&hb->entry[0]),
					     ctxt->memsz)) {
						ELX_DRVR_LOCK(phba, iflag);
						return EIO;
					}
					ELX_DRVR_LOCK(phba, iflag);
					appPtr = appPtr + ctxt->memsz;
					ep = &hb->entry[0];
					ctxt->memsz = 0;
				}
				memset((void *)ep->ScsiId.OSDeviceName, 0, 256);
				ep->ScsiId.ScsiTargetNumber = ctxt->pansid;
				ep->ScsiId.ScsiBusNumber = 0;
				memset((char *)ctxt->fcpLun, 0,
				       sizeof (HBA_UINT64));
				if (pbdl->nlp_bind_type & FCP_SEED_DID) {
					ep->type = TO_D_ID;
					ep->FcpId.FcId = pbdl->nlp_DID;
					ep->FcId = pbdl->nlp_DID;
					memset((uint8_t *) & ep->FcpId.PortWWN,
					       0, sizeof (HBA_WWN));
					memset((uint8_t *) & ep->FcpId.NodeWWN,
					       0, sizeof (HBA_WWN));
				} else {
					ep->type = TO_WWN;
					ep->FcId = 0;
					ep->FcpId.FcId = 0;
					if (pbdl->nlp_bind_type & FCP_SEED_WWPN) {
						memcpy((uint8_t *) & ep->FcpId.
						       PortWWN,
						       &pbdl->nlp_portname,
						       sizeof (HBA_WWN));
					} else {
						memcpy((uint8_t *) & ep->FcpId.
						       NodeWWN,
						       &pbdl->nlp_nodename,
						       sizeof (HBA_WWN));
					}
				}
				ep->FcpId.FcId = pbdl->nlp_DID;
				memcpy((uint8_t *) & ep->FcpId.PortWWN,
				       &pbdl->nlp_portname, sizeof (HBA_WWN));
				memcpy((uint8_t *) & ep->FcpId.NodeWWN,
				       &pbdl->nlp_nodename, sizeof (HBA_WWN));
				ep++;
				ctxt->cnt++;
				ctxt->memsz =
				    ctxt->memsz + sizeof (HBA_FCPBINDINGENTRY);
			}
			ctxt->ttl++;
		}
		pbdl = pbdl->nlp_listp_next;
	}			/* end searching bindlist */
	ELX_DRVR_UNLOCK(phba, iflag);
	if (copy_to_user
	    ((uint8_t *) appPtr, (uint8_t *) (&hb->entry[0]), ctxt->memsz)) {
		ELX_DRVR_LOCK(phba, iflag);
		return EIO;
	}
	hb->NumberOfEntries = (uint32_t) ctxt->ttl;
	if (copy_to_user
	    ((uint8_t *) cip->elx_dataout, (uint8_t *) (&hb->NumberOfEntries),
	     sizeof (ulong))) {
		ELX_DRVR_LOCK(phba, iflag);
		return EIO;
	}
	ELX_DRVR_LOCK(phba, iflag);
	cip->elx_outsz = 0;	/* no more copy needed */
	if (ctxt->ttl > ctxt->room) {
		rc = ERANGE;
		*do_cp = 1;
	}
	dfc_data_free(phba, &dfc_mem_struct);
	return (rc);
}

int
lpfc_ioctl_getcfg(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{

	CfgParam *cp;
	iCfgParam *icp;
	uint32_t cnt;
	elxCfgParam_t *clp;
	int i, rc = 0;

	clp = &phba->config[0];
	/* First uint32_t word will be count */
	cp = (CfgParam *) dm->fc_dataout;
	cnt = 0;
	for (i = 0; i < LPFC_TOTAL_NUM_OF_CFG_PARAM; i++) {
		icp = (iCfgParam *) & clp[i];
		if (!(icp->a_flag & CFG_EXPORT))
			continue;
		cp->a_low = icp->a_low;
		cp->a_hi = icp->a_hi;
		cp->a_flag = icp->a_flag;
		cp->a_default = icp->a_default;
		if ((i == LPFC_CFG_FCP_CLASS) || (i == LPFC_CFG_IP_CLASS)) {
			switch (icp->a_current) {
			case CLASS1:
				cp->a_current = 1;
				break;
			case CLASS2:
				cp->a_current = 2;
				break;
			case CLASS3:
				cp->a_current = 3;
				break;
			}
		} else {
			cp->a_current = icp->a_current;
		}
		cp->a_changestate = icp->a_changestate;
		memcpy(cp->a_string, icp->a_string, 32);
		memcpy(cp->a_help, icp->a_help, 80);
		cp++;
		cnt++;
	}
	if (cnt) {
		cip->elx_outsz = (uint32_t) (cnt * sizeof (CfgParam));
	}

	return (rc);
}

int
lpfc_ioctl_setcfg(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{

	iCfgParam *icp;
	uint32_t offset, cnt;
	elxCfgParam_t *clp;
	ELX_SLI_t *psli;
	int rc = 0;
	int i, j;

	psli = &phba->sli;
	clp = &phba->config[0];
	offset = (uint32_t) ((ulong) cip->elx_arg1);
	cnt = (uint32_t) ((ulong) cip->elx_arg2);
	if (offset >= LPFC_TOTAL_NUM_OF_CFG_PARAM) {
		rc = ERANGE;
		return (rc);
	}
	j = offset;
	for (i = 0; i < LPFC_TOTAL_NUM_OF_CFG_PARAM; i++) {
		icp = (iCfgParam *) & clp[i];
		if (!(icp->a_flag & CFG_EXPORT))
			continue;
		if (j == 0)
			break;
		j--;
	}
	if (icp->a_changestate != CFG_DYNAMIC) {
		rc = EPERM;
		return (rc);
	}
	if (((icp->a_low != 0) && (cnt < icp->a_low)) || (cnt > icp->a_hi)) {
		rc = ERANGE;
		return (rc);
	}
	if (!(icp->a_flag & CFG_EXPORT)) {
		rc = EPERM;
		return (rc);
	}
	switch (offset) {
	case LPFC_CFG_FCP_CLASS:
		switch (cnt) {
		case 1:
			clp[LPFC_CFG_FCP_CLASS].a_current = CLASS1;
			break;
		case 2:
			clp[LPFC_CFG_FCP_CLASS].a_current = CLASS2;
			break;
		case 3:
			clp[LPFC_CFG_FCP_CLASS].a_current = CLASS3;
			break;
		}
		icp->a_current = cnt;
		break;

	case LPFC_CFG_IP_CLASS:
		switch (cnt) {
		case 1:
			clp[LPFC_CFG_IP_CLASS].a_current = CLASS1;
			break;
		case 2:
			clp[LPFC_CFG_IP_CLASS].a_current = CLASS2;
			break;
		case 3:
			clp[LPFC_CFG_IP_CLASS].a_current = CLASS3;
			break;
		}
		icp->a_current = cnt;
		break;

	case ELX_CFG_LINKDOWN_TMO:
		icp->a_current = cnt;
		break;

	default:
		icp->a_current = cnt;
	}

	return (rc);
}

int
lpfc_ioctl_hba_get_event(elxHBA_t * phba,
			 ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	fcEVT_t *ep;
	fcEVT_t *oep;
	fcEVTHDR_t *ehp;
	uint8_t *cp;
	DMABUF_t *omm;
	void *type;
	uint32_t offset, incr, size, cnt, i, gstype;
	DMABUF_t *mm;
	struct dfc_info *di;
	LPFCHBA_t *plhba;
	int no_more;
	int rc = 0;
	uint32_t total_mem = dm->fc_outsz;
	unsigned long iflag;

	di = &dfc.dfc_info[cip->elx_brd];
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	no_more = 1;

	offset = ((uint32_t) ((ulong) cip->elx_arg3) &	/* event mask */
		  FC_REG_EVENT_MASK);	/* event mask */
	incr = (uint32_t) cip->elx_flag;	/* event id   */
	size = (uint32_t) cip->elx_iocb;	/* process requesting evt  */

	type = 0;
	switch (offset) {
	case FC_REG_CT_EVENT:
		ELX_DRVR_UNLOCK(phba, iflag);
		if (copy_from_user
		    ((uint8_t *) & gstype, (uint8_t *) cip->elx_arg2,
		     (ulong) (sizeof (uint32_t)))) {
			rc = EIO;
			ELX_DRVR_LOCK(phba, iflag);
			return (rc);
		}
		ELX_DRVR_LOCK(phba, iflag);
		type = (void *)(ulong) gstype;
		break;
	}

	ehp = (fcEVTHDR_t *) plhba->fc_evt_head;

	while (ehp) {
		if ((ehp->e_mask == offset) && (ehp->e_type == type))
			break;
		ehp = (fcEVTHDR_t *) ehp->e_next_header;
	}

	if (!ehp) {
		rc = ENOENT;
		return (rc);
	}

	ep = ehp->e_head;
	oep = 0;
	while (ep) {
		/* Find an event that matches the event mask */
		if (ep->evt_sleep == 0) {
			/* dequeue event from event list */
			if (oep == 0) {
				ehp->e_head = ep->evt_next;
			} else {
				oep->evt_next = ep->evt_next;
			}
			if (ehp->e_tail == ep)
				ehp->e_tail = oep;

			switch (offset) {
			case FC_REG_LINK_EVENT:
				break;
			case FC_REG_RSCN_EVENT:
				/* Return data length */
				cnt = sizeof (uint32_t);
				ELX_DRVR_UNLOCK(phba, iflag);
				if (copy_to_user
				    ((uint8_t *) cip->elx_arg1,
				     (uint8_t *) & cnt, sizeof (uint32_t))) {
					rc = EIO;
				}
				ELX_DRVR_LOCK(phba, iflag);
				memcpy(dm->fc_dataout, (char *)&ep->evt_data0,
				       cnt);
				cip->elx_outsz = (uint32_t) cnt;
				break;
			case FC_REG_CT_EVENT:
				/* Return data length */
				cnt = (ulong) (ep->evt_data2);
				ELX_DRVR_UNLOCK(phba, iflag);
				if (copy_to_user
				    ((uint8_t *) cip->elx_arg1,
				     (uint8_t *) & cnt, sizeof (uint32_t))) {
					rc = EIO;
				} else {
					if (copy_to_user
					    ((uint8_t *) cip->elx_arg2,
					     (uint8_t *) & ep->evt_data0,
					     sizeof (uint32_t))) {
						rc = EIO;
					}
				}
				ELX_DRVR_LOCK(phba, iflag);

				cip->elx_outsz = (uint32_t) cnt;
				i = cnt;
				mm = (DMABUF_t *) ep->evt_data1;
				cp = (uint8_t *) dm->fc_dataout;
				while (mm) {

					if (cnt > FCELSSIZE)
						i = FCELSSIZE;
					else
						i = cnt;

					if (total_mem > 0) {
						memcpy(cp, (char *)mm->virt, i);
						total_mem -= i;
					}

					omm = mm;
					mm = (DMABUF_t *) mm->next;
					cp += i;
					elx_mem_put(phba, MEM_BUF,
						    (uint8_t *) omm);
				}
				break;
			}

			if ((offset == FC_REG_CT_EVENT) && (ep->evt_next) &&
			    (((fcEVT_t *) (ep->evt_next))->evt_sleep == 0)) {
				ep->evt_data0 |= 0x80000000;	/* More event r waiting */
				ELX_DRVR_UNLOCK(phba, iflag);
				if (copy_to_user
				    ((uint8_t *) cip->elx_arg2,
				     (uint8_t *) & ep->evt_data0,
				     sizeof (uint32_t))) {
					rc = EIO;
				}
				ELX_DRVR_LOCK(phba, iflag);
				no_more = 0;
			}

			/* Requeue event entry */
			ep->evt_next = 0;
			ep->evt_data0 = 0;
			ep->evt_data1 = 0;
			ep->evt_data2 = 0;
			ep->evt_sleep = 1;
			ep->evt_flags = 0;

			if (ehp->e_head == 0) {
				ehp->e_head = ep;
				ehp->e_tail = ep;
			} else {
				ehp->e_tail->evt_next = ep;
				ehp->e_tail = ep;
			}

			if (offset == FC_REG_LINK_EVENT) {
				ehp->e_flag &= ~E_GET_EVENT_ACTIVE;
				rc = lpfc_ioctl_linkinfo(phba, cip, dm);
				return (rc);
			}

			if (no_more)
				ehp->e_flag &= ~E_GET_EVENT_ACTIVE;
			return (rc);
			/*
			   break;
			 */
		}
		oep = ep;
		ep = ep->evt_next;
	}
	if (ep == 0) {
		/* No event found */
		rc = ENOENT;
	}

	return (rc);
}

int
lpfc_ioctl_hba_set_event(elxHBA_t * phba,
			 ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{
	fcEVT_t *evp;
	fcEVT_t *ep;
	fcEVT_t *oep;
	fcEVTHDR_t *ehp;
	fcEVTHDR_t *oehp;
	int found;
	void *type;
	uint32_t offset, incr;
	LPFCHBA_t *plhba;
	MBUF_INFO_t bfrnfo, *pbfrnfo = &bfrnfo;
	int rc = 0;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	offset = ((uint32_t) ((ulong) cip->elx_arg3) &	/* event mask */
		  FC_REG_EVENT_MASK);
	incr = (uint32_t) cip->elx_flag;	/* event id   */
	switch (offset) {
	case FC_REG_CT_EVENT:
		type = (void *)cip->elx_arg2;
		found = LPFC_MAX_EVENT;	/* Number of events we can queue up + 1, before
					 * dropping events for this event id.  */
		break;
	case FC_REG_RSCN_EVENT:
		type = (void *)0;
		found = LPFC_MAX_EVENT;	/* Number of events we can queue up + 1, before
					 * dropping events for this event id.  */
		break;
	case FC_REG_LINK_EVENT:
		type = (void *)0;
		found = 2;	/* Number of events we can queue up + 1, before
				 * dropping events for this event id.  */
		break;
	default:
		found = 0;
		rc = EINTR;
		return (rc);
	}

	/*
	 * find the fcEVT_t header for this Event, allocate a header
	 * if not found.
	 */
	oehp = 0;
	ehp = (fcEVTHDR_t *) plhba->fc_evt_head;
	while (ehp) {
		if ((ehp->e_mask == offset) && (ehp->e_type == type)) {
			found = 0;
			break;
		}
		oehp = ehp;
		ehp = (fcEVTHDR_t *) ehp->e_next_header;
	}

	if (!ehp) {
		pbfrnfo->virt = 0;
		pbfrnfo->phys = 0;
		pbfrnfo->flags = ELX_MBUF_VIRT;
		pbfrnfo->align = sizeof (void *);
		pbfrnfo->size = sizeof (fcEVTHDR_t);
		pbfrnfo->dma_handle = 0;
		elx_malloc(phba, pbfrnfo);
		if (pbfrnfo->virt == NULL) {
			rc = EINTR;
			return (rc);
		}
		ehp = (fcEVTHDR_t *) (pbfrnfo->virt);
		memset((char *)ehp, 0, sizeof (fcEVTHDR_t));
		if (plhba->fc_evt_head == 0) {
			plhba->fc_evt_head = ehp;
			plhba->fc_evt_tail = ehp;
		} else {
			((fcEVTHDR_t *) (plhba->fc_evt_tail))->e_next_header =
			    ehp;
			plhba->fc_evt_tail = (void *)ehp;
		}
		ehp->e_handle = incr;
		ehp->e_mask = offset;
		ehp->e_type = type;
		ehp->e_refcnt++;
	} else {
		ehp->e_refcnt++;
	}

	while (found) {
		/* Save event id for C_GET_EVENT */
		pbfrnfo->virt = 0;
		pbfrnfo->phys = 0;
		pbfrnfo->flags = (ELX_MBUF_VIRT);
		pbfrnfo->align = sizeof (void *);
		pbfrnfo->size = sizeof (fcEVT_t);
		pbfrnfo->dma_handle = 0;
		elx_malloc(phba, pbfrnfo);
		if (pbfrnfo->virt == NULL) {
			rc = EINTR;
			break;
		}
		oep = (fcEVT_t *) (pbfrnfo->virt);
		memset((char *)oep, 0, sizeof (fcEVT_t));

		oep->evt_sleep = 1;
		oep->evt_handle = incr;
		oep->evt_mask = offset;
		oep->evt_type = type;

		if (ehp->e_head == 0) {
			ehp->e_head = oep;
			ehp->e_tail = oep;
		} else {
			ehp->e_tail->evt_next = (void *)oep;
			ehp->e_tail = oep;
		}
		oep->evt_next = 0;
		found--;
	}

	switch (offset) {
	case FC_REG_CT_EVENT:
	case FC_REG_RSCN_EVENT:
	case FC_REG_LINK_EVENT:

		if (rc || lpfc_sleep(phba, ehp)) {
			rc = EINTR;
			ehp->e_mode &= ~E_SLEEPING_MODE;
			ehp->e_refcnt--;
			if (ehp->e_refcnt) {
				goto setout;
			}
			/* Remove all eventIds from queue */
			ep = ehp->e_head;
			oep = 0;
			found = 0;
			while (ep) {
				if (ep->evt_handle == incr) {
					/* dequeue event from event list */
					if (oep == 0) {
						ehp->e_head = ep->evt_next;
					} else {
						oep->evt_next = ep->evt_next;
					}
					if (ehp->e_tail == ep)
						ehp->e_tail = oep;
					evp = ep;
					ep = ep->evt_next;
					pbfrnfo->virt = (uint8_t *) evp;
					pbfrnfo->phys = 0;
					pbfrnfo->size = sizeof (fcEVT_t);
					pbfrnfo->flags = ELX_MBUF_VIRT;
					pbfrnfo->align = 0;
					pbfrnfo->dma_handle = 0;
					elx_free(phba, pbfrnfo);
				} else {
					oep = ep;
					ep = ep->evt_next;
				}
			}

			/*
			 * No more fcEVT_t pointer under this fcEVTHDR_t
			 * Free the fcEVTHDR_t
			 */
			if (ehp->e_head == 0) {
				oehp = 0;
				ehp = (fcEVTHDR_t *) plhba->fc_evt_head;
				while (ehp) {
					if ((ehp->e_mask == offset) &&
					    (ehp->e_type == type)) {
						found = 0;
						break;
					}
					oehp = ehp;
					ehp = (fcEVTHDR_t *) ehp->e_next_header;
				}
				if (oehp == 0) {
					plhba->fc_evt_head = ehp->e_next_header;
				} else {
					oehp->e_next_header =
					    ehp->e_next_header;
				}
				if (plhba->fc_evt_tail == ehp)
					plhba->fc_evt_tail = oehp;

				pbfrnfo->virt = (uint8_t *) ehp;
				pbfrnfo->size = sizeof (fcEVTHDR_t);
				pbfrnfo->phys = 0;
				pbfrnfo->flags = ELX_MBUF_VIRT;
				pbfrnfo->align = 0;
				pbfrnfo->dma_handle = 0;
				elx_free(phba, pbfrnfo);
			}
			goto setout;
		}
		ehp->e_refcnt--;
		break;
	}
      setout:
	return (rc);
}

int
lpfc_ioctl_add_bind(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{

	bind_ctl_t bind_ctl;
	void *bind_id = 0;
	uint8_t bind_type = FCP_SEED_WWNN;
	int rc = 0;
	unsigned long iflag;

	ELX_DRVR_UNLOCK(phba, iflag);
	if (copy_from_user((uint8_t *) & bind_ctl, (uint8_t *) cip->elx_arg1,
			   (ulong) (sizeof (bind_ctl)))) {
		rc = EIO;
		ELX_DRVR_LOCK(phba, iflag);
		return rc;
	}
	ELX_DRVR_LOCK(phba, iflag);

	switch (bind_ctl.bind_type) {
	case ELX_WWNN_BIND:
		bind_type = FCP_SEED_WWNN;
		bind_id = &bind_ctl.wwnn[0];
		break;
	case ELX_WWPN_BIND:
		bind_type = FCP_SEED_WWPN;
		bind_id = &bind_ctl.wwpn[0];
		break;
	case ELX_DID_BIND:
		bind_type = FCP_SEED_DID;
		bind_id = &bind_ctl.did;
		break;
	default:
		rc = EIO;
		break;
	}

	if (rc)
		return rc;

	rc = lpfc_add_bind(phba, bind_type, bind_id, bind_ctl.scsi_id);
	return rc;
}

int
lpfc_ioctl_del_bind(elxHBA_t * phba, ELXCMDINPUT_t * cip, struct dfc_mem *dm)
{

	bind_ctl_t bind_ctl;
	void *bind_id = 0;
	uint8_t bind_type = FCP_SEED_WWNN;
	int rc = 0;
	unsigned long iflag;

	ELX_DRVR_UNLOCK(phba, iflag);
	if (copy_from_user((uint8_t *) & bind_ctl, (uint8_t *) cip->elx_arg1,
			   (ulong) (sizeof (bind_ctl)))) {
		ELX_DRVR_LOCK(phba, iflag);
		rc = EIO;
		return rc;
	}
	ELX_DRVR_LOCK(phba, iflag);

	switch (bind_ctl.bind_type) {

	case ELX_WWNN_BIND:
		bind_type = FCP_SEED_WWNN;
		bind_id = &bind_ctl.wwnn[0];
		break;

	case ELX_WWPN_BIND:
		bind_type = FCP_SEED_WWPN;
		bind_id = &bind_ctl.wwpn[0];
		break;

	case ELX_DID_BIND:
		bind_type = FCP_SEED_DID;
		bind_id = &bind_ctl.did;
		break;

	case ELX_SCSI_ID:
		bind_id = 0;
		break;

	default:
		rc = EIO;
		break;
	}

	if (rc)
		return rc;

	rc = lpfc_del_bind(phba, bind_type, bind_id, bind_ctl.scsi_id);

	return rc;
}

int
lpfc_ioctl_list_bind(elxHBA_t * phba,
		     ELXCMDINPUT_t * cip, struct dfc_mem *dm, int *do_cp)
{

	unsigned long next_index = 0;
	unsigned long max_index = (unsigned long)cip->elx_arg1;
	HBA_BIND_LIST *bind_list;
	HBA_BIND_ENTRY *bind_array;
	LPFC_BINDLIST_t *pbdl;
	LPFCHBA_t *plhba;
	LPFC_NODELIST_t *pndl;
	int rc;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	bind_list = (HBA_BIND_LIST *) dm->fc_dataout;
	bind_array = &bind_list->entry[0];

	/* Iterate through the mapped list */
	pndl = plhba->fc_nlpmap_start;
	while (pndl != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start) {
		if (next_index >= max_index) {
			rc = ERANGE;
			*do_cp = 0;
			return (rc);
		}
		memset(&bind_array[next_index], 0, sizeof (HBA_BIND_ENTRY));
		bind_array[next_index].scsi_id = pndl->nlp_sid;
		bind_array[next_index].did = pndl->nlp_DID;
		memcpy(&bind_array[next_index].wwpn, &pndl->nlp_portname,
		       sizeof (HBA_WWN));
		memcpy(&bind_array[next_index].wwnn, &pndl->nlp_nodename,
		       sizeof (HBA_WWN));
		if (pndl->nlp_flag & NLP_AUTOMAP)
			bind_array[next_index].flags |= HBA_BIND_AUTOMAP;
		if (pndl->nlp_flag & NLP_SEED_WWNN)
			bind_array[next_index].bind_type = BIND_WWNN;
		if (pndl->nlp_flag & NLP_SEED_WWPN)
			bind_array[next_index].bind_type = BIND_WWPN;
		if (pndl->nlp_flag & NLP_SEED_ALPA)
			bind_array[next_index].bind_type = BIND_ALPA;
		else if (pndl->nlp_flag & NLP_SEED_DID)
			bind_array[next_index].bind_type = BIND_DID;
		bind_array[next_index].flags |= HBA_BIND_MAPPED;
		if (pndl->nlp_flag & NLP_NODEV_TMO)
			bind_array[next_index].flags |= HBA_BIND_NODEVTMO;
		if (pndl && pndl->nlp_Target &&
		    (pndl->nlp_Target->rptLunState == REPORT_LUN_COMPLETE))
			bind_array[next_index].flags |= HBA_BIND_RPTLUNST;
		next_index++;
		pndl = (LPFC_NODELIST_t *) pndl->nle.nlp_listp_next;
	}

	/* Iterate through the unmapped list */
	pndl = plhba->fc_nlpunmap_start;
	while (pndl != (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start) {
		if (next_index >= max_index) {
			rc = ERANGE;
			*do_cp = 0;
			return (rc);
		}
		memset(&bind_array[next_index], 0, sizeof (HBA_BIND_ENTRY));
		bind_array[next_index].did = pndl->nlp_DID;
		memcpy(&bind_array[next_index].wwpn, &pndl->nlp_portname,
		       sizeof (HBA_WWN));
		memcpy(&bind_array[next_index].wwnn, &pndl->nlp_nodename,
		       sizeof (HBA_WWN));
		bind_array[next_index].flags |= HBA_BIND_UNMAPPED;
		if (pndl->nlp_flag & NLP_TGT_NO_SCSIID)
			bind_array[next_index].flags |= HBA_BIND_NOSCSIID;
		if (pndl->nlp_flag & NLP_NODEV_TMO)
			bind_array[next_index].flags |= HBA_BIND_NODEVTMO;
		if (pndl && pndl->nlp_Target &&
		    (pndl->nlp_Target->rptLunState == REPORT_LUN_COMPLETE))
			bind_array[next_index].flags |= HBA_BIND_RPTLUNST;

		next_index++;
		pndl = (LPFC_NODELIST_t *) pndl->nle.nlp_listp_next;
	}

	/* Iterate through the bind list */
	pbdl = plhba->fc_nlpbind_start;
	while (pbdl != (LPFC_BINDLIST_t *) & plhba->fc_nlpbind_start) {
		if (next_index >= max_index) {
			rc = ERANGE;
			*do_cp = 0;
			return (rc);
		}
		memset(&bind_array[next_index], 0, sizeof (HBA_BIND_ENTRY));
		bind_array[next_index].scsi_id = pbdl->nlp_sid;

		if (pbdl->nlp_bind_type & FCP_SEED_DID) {
			bind_array[next_index].bind_type = BIND_DID;
			bind_array[next_index].did = pbdl->nlp_DID;

		}

		if (pbdl->nlp_bind_type & FCP_SEED_WWPN) {
			bind_array[next_index].bind_type = BIND_WWPN;
			memcpy((uint8_t *) & bind_array[next_index].wwpn,
			       &pbdl->nlp_portname, sizeof (HBA_WWN));
		}

		if (pbdl->nlp_bind_type & FCP_SEED_WWNN) {
			bind_array[next_index].bind_type = BIND_WWNN;
			memcpy((uint8_t *) & bind_array[next_index].wwnn,
			       &pbdl->nlp_nodename, sizeof (HBA_WWN));
		}
		bind_array[next_index].flags |= HBA_BIND_BINDLIST;
		pbdl = pbdl->nlp_listp_next;
		next_index++;
	}
	bind_list->NumberOfEntries = next_index;
	return 0;
}

int
lpfc_diag_ioctl(elxHBA_t * phba, ELXCMDINPUT_t * cip)
{
	int rc = 0;
	int do_cp = 0;		/* copy data into user space even rc != 0 */
	uint32_t cnt;
	uint32_t outshift;
	uint32_t total_mem;
	LPFCHBA_t *plhba = (LPFCHBA_t *) phba->pHbaProto;
	struct dfc_info *di;
	struct dfc_mem *dm;
	struct dfc_mem dmdata;
	unsigned long iflag;

	cnt = phba->brd_no;
	di = &dfc.dfc_info[cip->elx_brd];
	dm = &dmdata;
	memset((void *)dm, 0, sizeof (struct dfc_mem));
	/* dfc_ioctl entry */
	elx_printf_log(phba->brd_no, &elx_msgBlk1600,	/* ptr to msg structure */
		       elx_mes1600,	/* ptr to msg */
		       elx_msgBlk1600.msgPreambleStr,	/* begin varargs */
		       cip->elx_cmd, (uint32_t) ((ulong) cip->elx_arg1), (uint32_t) ((ulong) cip->elx_arg2), cip->elx_outsz);	/* end varargs */
	outshift = 0;
	if (cip->elx_outsz) {

		/* Allocate memory for ioctl data. If buffer is bigger than 64k, then we
		 * allocate 64k and re-use that buffer over and over to xfer the whole 
		 * block. This is because Linux kernel has a problem allocating more than
		 * 120k of kernel space memory. Saw problem with GET_FCPTARGETMAPPING...
		 */
		if (cip->elx_outsz <= (64 * 1024))
			total_mem = cip->elx_outsz;
		else
			total_mem = 64 * 1024;

		dm->fc_dataout = NULL;
		if (dfc_data_alloc(phba, dm, total_mem)) {
			return (ENOMEM);
		}
	} else {
		/* Allocate memory for ioctl data */
		dm->fc_dataout = NULL;
		if (dfc_data_alloc(phba, dm, 4096)) {
			return (ENOMEM);
		}
	}

	/* Make sure driver instance is attached */
	if (elxDRVR.pHba[cnt] != phba) {
		return (ENODEV);
	}
	di->fc_refcnt++;

	switch (cip->elx_cmd) {

		/* Diagnostic Interface Library Support */

	case ELX_WRITE_PCI:
		rc = lpfc_ioctl_write_pci(phba, cip, dm);
		break;

	case ELX_READ_PCI:
		rc = lpfc_ioctl_read_pci(phba, cip, dm);
		break;

	case ELX_WRITE_MEM:
		rc = lpfc_ioctl_write_mem(phba, cip, dm);
		break;

	case ELX_READ_MEM:
		rc = lpfc_ioctl_read_mem(phba, cip, dm);
		break;

	case ELX_WRITE_CTLREG:
		rc = lpfc_ioctl_write_ctlreg(phba, cip, dm);
		break;

	case ELX_READ_CTLREG:
		rc = lpfc_ioctl_read_ctlreg(phba, cip, dm);
		break;

	case ELX_INITBRDS:
		ELX_DRVR_UNLOCK(phba, iflag);
		if (copy_from_user
		    ((uint8_t *) & di->fc_ba, (uint8_t *) cip->elx_dataout,
		     sizeof (brdinfo))) {
			rc = EIO;
			ELX_DRVR_LOCK(phba, iflag);
			break;
		}
		ELX_DRVR_LOCK(phba, iflag);

		if (elx_initpci(di, phba)) {
			rc = EIO;
			break;
		}
		if (plhba->fc_flag & FC_OFFLINE_MODE)
			di->fc_ba.a_offmask |= OFFDI_OFFLINE;

		memcpy(dm->fc_dataout, (uint8_t *) & di->fc_ba,
		       sizeof (brdinfo));
		cip->elx_outsz = sizeof (brdinfo);
		break;

	case ELX_READ_MEMSEG:
		memcpy(dm->fc_dataout, (uint8_t *) & phba->memseg,
		       (sizeof (MEMSEG_t) * ELX_MAX_SEG));
		cip->elx_outsz = sizeof (MEMSEG_t) * ELX_MAX_SEG;
		break;

	case ELX_SETDIAG:
		rc = lpfc_ioctl_setdiag(phba, cip, dm);
		break;

	case LPFC_LIP:
		rc = lpfc_ioctl_lip(phba, cip, dm);
		break;

	case LPFC_RESET_QDEPTH:
		rc = lpfc_reset_dev_q_depth(phba);
		break;

	case LPFC_OUTFCPIO:
		rc = lpfc_ioctl_outfcpio(phba, cip, dm);
		break;

	case LPFC_HBA_SEND_SCSI:
	case LPFC_HBA_SEND_FCP:
		rc = lpfc_ioctl_send_scsi_fcp(phba, cip, dm);
		break;

	case LPFC_SEND_ELS:
		rc = lpfc_ioctl_send_els(phba, cip, dm);
		break;

	case LPFC_HBA_SEND_MGMT_RSP:
		rc = lpfc_ioctl_send_mgmt_rsp(phba, cip, dm);
		break;

	case LPFC_HBA_SEND_MGMT_CMD:
	case LPFC_CT:
		rc = lpfc_ioctl_send_mgmt_cmd(phba, cip, dm);
		break;

	case ELX_MBOX:
		rc = lpfc_ioctl_mbox(phba, cip, dm);
		break;

	case ELX_DISPLAY_PCI_ALL:
		rc = lpfc_ioctl_display_pci_all(phba, cip, dm);
		break;

	case ELX_WRITE_HC:
		rc = lpfc_ioctl_write_hc(phba, cip, dm);
		/* drop thru to read */
	case ELX_READ_HC:
		rc = lpfc_ioctl_read_hc(phba, cip, dm);
		break;

	case ELX_WRITE_HS:
		rc = lpfc_ioctl_write_hs(phba, cip, dm);
		/* drop thru to read */
	case ELX_READ_HS:
		/* Read the HBA Host Status Register */
		rc = lpfc_ioctl_read_hs(phba, cip, dm);
		break;

	case ELX_WRITE_HA:
		/* Write the HBA Host Attention Register */
		rc = lpfc_ioctl_write_ha(phba, cip, dm);
		/* drop thru to read */
	case ELX_READ_HA:
		/* Read the HBA Host Attention Register */
		rc = lpfc_ioctl_read_ha(phba, cip, dm);
		break;

	case ELX_WRITE_CA:
		rc = lpfc_ioctl_write_ca(phba, cip, dm);
		/* drop thru to read */
	case ELX_READ_CA:
		rc = lpfc_ioctl_read_ca(phba, cip, dm);
		break;

	case ELX_READ_MB:
		rc = lpfc_ioctl_read_mb(phba, cip, dm);
		break;

	case ELX_DBG:
		rc = lpfc_ioctl_dbg(phba, cip, dm);
		break;

	case ELX_INST:
		rc = lpfc_ioctl_inst(phba, cip, dm);
		break;

	case LPFC_LISTN:
		rc = lpfc_ioctl_listn(phba, cip, dm);
		break;

	case ELX_READ_BPLIST:
		rc = lpfc_ioctl_read_bplist(phba, cip, dm);
		break;

	case ELX_RESET:
		rc = lpfc_ioctl_reset(phba, cip, dm);
		break;

	case ELX_READ_HBA:
		rc = lpfc_ioctl_read_hba(phba, cip, dm);
		break;

	case LPFC_STAT:
		rc = lpfc_ioctl_stat(phba, cip, dm);
		break;

	case ELX_READ_LHBA:
		rc = lpfc_ioctl_read_lhba(phba, cip, dm);
		break;

	case ELX_READ_LXHBA:
		rc = lpfc_ioctl_read_lxhba(phba, cip, dm);
		break;

	case ELX_DEVP:
		rc = lpfc_ioctl_devp(phba, cip, dm);
		break;

	case ELX_LINKINFO:
		rc = lpfc_ioctl_linkinfo(phba, cip, dm);
		break;

	case ELX_IOINFO:
		rc = lpfc_ioctl_ioinfo(phba, cip, dm);
		break;

	case ELX_NODEINFO:
		rc = lpfc_ioctl_nodeinfo(phba, cip, dm);
		break;

	case LPFC_HBA_ADAPTERATTRIBUTES:
		rc = lpfc_ioctl_hba_adapterattributes(phba, cip, dm);
		break;

	case LPFC_HBA_PORTATTRIBUTES:
		rc = lpfc_ioctl_hba_portattributes(phba, cip, dm);
		break;

	case LPFC_HBA_PORTSTATISTICS:
		rc = lpfc_ioctl_hba_portstatistics(phba, cip, dm);
		break;

	case LPFC_HBA_WWPNPORTATTRIBUTES:
		rc = lpfc_ioctl_hba_wwpnportattributes(phba, cip, dm);
		break;

	case LPFC_HBA_DISCPORTATTRIBUTES:
		rc = lpfc_ioctl_hba_discportattributes(phba, cip, dm);
		break;

	case LPFC_HBA_INDEXPORTATTRIBUTES:
		rc = lpfc_ioctl_hba_indexportattributes(phba, cip, dm);
		break;

	case LPFC_HBA_SETMGMTINFO:
		rc = lpfc_ioctl_hba_setmgmtinfo(phba, cip, dm);
		break;

	case LPFC_HBA_GETMGMTINFO:
		rc = lpfc_ioctl_hba_getmgmtinfo(phba, cip, dm);
		break;

	case LPFC_HBA_REFRESHINFO:
		rc = lpfc_ioctl_hba_refreshinfo(phba, cip, dm);
		break;

	case LPFC_HBA_RNID:
		rc = lpfc_ioctl_hba_rnid(phba, cip, dm);
		break;

	case LPFC_HBA_GETEVENT:
		rc = lpfc_ioctl_hba_getevent(phba, cip, dm);
		break;

	case LPFC_HBA_FCPTARGETMAPPING:
		rc = lpfc_ioctl_hba_fcptargetmapping(phba, cip, dm, &do_cp);
		break;

	case LPFC_HBA_FCPBINDING:
		rc = lpfc_ioctl_hba_fcpbinding(phba, cip, dm, &do_cp);
		break;

	case LPFC_GETCFG:
		rc = lpfc_ioctl_getcfg(phba, cip, dm);
		break;

	case LPFC_SETCFG:
		rc = lpfc_ioctl_setcfg(phba, cip, dm);
		break;

	case LPFC_HBA_GET_EVENT:
		rc = lpfc_ioctl_hba_get_event(phba, cip, dm);
		break;

	case LPFC_HBA_SET_EVENT:
		rc = lpfc_ioctl_hba_set_event(phba, cip, dm);
		break;

	case ELX_ADD_BIND:
		rc = lpfc_ioctl_add_bind(phba, cip, dm);
		break;

	case ELX_DEL_BIND:
		rc = lpfc_ioctl_del_bind(phba, cip, dm);
		break;

	case ELX_LIST_BIND:
		rc = lpfc_ioctl_list_bind(phba, cip, dm, &do_cp);
		break;

	default:
		rc = EINVAL;
		break;
	}

	/* dfc_ioctl exit */
	elx_printf_log(phba->brd_no, &elx_msgBlk1601,	/* ptr to msg structure */
		       elx_mes1601,	/* ptr to msg */
		       elx_msgBlk1601.msgPreambleStr,	/* begin varargs */
		       rc, cip->elx_outsz, (uint32_t) ((ulong) cip->elx_dataout));	/* end varargs */

	di->fc_refcnt--;

	/* Copy data to user space config method */
	if ((rc == 0) || (do_cp == 1)) {
		if (cip->elx_outsz) {
			ELX_DRVR_UNLOCK(phba, iflag);
			if (copy_to_user
			    ((uint8_t *) cip->elx_dataout,
			     (uint8_t *) dm->fc_dataout, (int)cip->elx_outsz)) {
				rc = EIO;
			}
			ELX_DRVR_LOCK(phba, iflag);
		}
	}

	dfc_data_free(phba, dm);
	return (rc);
}

uint64_t
dfc_getLunId(MBUF_INFO_t * pbfrnfo, ELXSCSILUN_t * plun, uint64_t lunidx)
{
	uint64_t lun;
	int i;
	FCP_CMND *tmp;		/* tmp is not really an FCP_CMD. We jus need
				 * to access fcpLunMsl field */

	tmp = (FCP_CMND *) pbfrnfo->virt;

	/* if there's no rptLunData, then we just return lunid from 
	 * first dev_ptr.
	 */
	if (tmp == 0) {
		lun = plun->lun_id;
	} else {
		i = (lunidx + 1) * 8;
		tmp = (FCP_CMND *) (((uint8_t *) pbfrnfo->virt) + i);
		lun = ((tmp->fcpLunMsl >> FC_LUN_SHIFT) & 0xff);
	}
	return lun;
}

int
dfc_issue_mbox(elxHBA_t * phba, MAILBOX_t * pmbox)
{
	int j;
	MAILBOX_t *pmboxlcl;
	volatile uint32_t word0;
	uint32_t ha_copy;
	ulong iflag;
	uint32_t dly = 1;
	ELX_SLI_t *psli = &phba->sli;

	if (phba->hba_state == ELX_HBA_ERROR) {
		pmbox->mbxStatus = MBXERR_ERROR;
		return (1);
	}
	j = 0;
	while (psli->sliinit.sli_flag & ELX_SLI_MBOX_ACTIVE) {
		if (j < 10) {
			mdelay(1);
		} else {
			mdelay(50);
		}
		if (j++ >= 600) {
			pmbox->mbxStatus = MBXERR_ERROR;
			return (1);
		}
	}
	ELX_SLI_LOCK(phba, iflag);

      retrycmd:
	psli->sliinit.sli_flag |= ELX_SLI_MBOX_ACTIVE;
	pmbox->mbxOwner = OWN_CHIP;
	if (psli->sliinit.sli_flag & ELX_SLI2_ACTIVE) {	/* SLI2 mode */
		elx_sli_pcimem_bcopy((uint32_t *) pmbox,
				     (uint32_t *) psli->MBhostaddr,
				     (sizeof (uint32_t) * (MAILBOX_CMD_WSIZE)));
		elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
				 sizeof (MAILBOX_t), ELX_DMA_SYNC_FORDEV);
	} else {		/* NOT SLI2 mode */
		/* First copy mbox command data to HBA SLIM, skip past first word */
		(psli->sliinit.elx_sli_write_slim) ((void *)phba,
						    (void *)&pmbox->un.
						    varWords[0],
						    sizeof (uint32_t),
						    ((MAILBOX_CMD_WSIZE -
						      1) * sizeof (uint32_t)));
		/* Next copy over first word, with mbxOwner set */
		word0 = *((volatile uint32_t *)pmbox);
		(psli->sliinit.elx_sli_write_slim) ((void *)phba,
						    (void *)&word0, 0,
						    sizeof (uint32_t));
	}
	/* interrupt board to doit right away */
	(psli->sliinit.elx_sli_write_CA) (phba, CA_MBATT);
	psli->slistat.mboxCmd++;

	if (psli->sliinit.sli_flag & ELX_SLI2_ACTIVE) {	/* SLI2 mode */
		elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
				 sizeof (MAILBOX_t), ELX_DMA_SYNC_FORCPU);
		/* First read mbox status word */
		word0 =
		    PCIMEM_LONG(*
				((volatile uint32_t *)(MAILBOX_t *) psli->
				 MBhostaddr));
	} else {		/* NOT SLI2 mode */
		/* First read mbox status word */
		(psli->sliinit.elx_sli_read_slim) ((void *)phba, (void *)&word0,
						   0, sizeof (uint32_t));
	}
	/* Read the HBA Host Attention Register */
	ha_copy = (psli->sliinit.elx_sli_read_HA) (phba);

	/* Wait for command to complete */
	while (((word0 & OWN_CHIP) == OWN_CHIP) || !(ha_copy & HA_MBATT)) {
		if (j > 20 || pmbox->mbxCommand == MBX_INIT_LINK) {
			dly = 50;
		}
		ELX_SLI_UNLOCK(phba, iflag);
		mdelay(dly);
		ELX_SLI_LOCK(phba, iflag);
		if (j++ >= 600) {
			pmbox->mbxStatus = MBXERR_ERROR;
			psli->sliinit.sli_flag &= ~ELX_SLI_MBOX_ACTIVE;
			ELX_SLI_UNLOCK(phba, iflag);
			return (1);
		}

		if (psli->sliinit.sli_flag & ELX_SLI2_ACTIVE) {	/* SLI2 mode */
			elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
					 sizeof (MAILBOX_t),
					 ELX_DMA_SYNC_FORCPU);
			/* First copy command data */
			word0 = PCIMEM_LONG(*((volatile uint32_t *)(MAILBOX_t *)
					      psli->MBhostaddr));
		} else {
			/* First read mbox status word */
			(psli->sliinit.elx_sli_read_slim) ((void *)phba,
							   (void *)&word0, 0,
							   sizeof (uint32_t));
		}
		ha_copy = HA_MBATT;
	}

	pmboxlcl = (MAILBOX_t *) & word0;
	if (pmboxlcl->mbxCommand != pmbox->mbxCommand) {
		j++;
		if (pmbox->mbxCommand == MBX_INIT_LINK) {
			/* Do not retry init_link's */
			pmbox->mbxStatus = 0;
			psli->sliinit.sli_flag &= ~ELX_SLI_MBOX_ACTIVE;
			ELX_SLI_UNLOCK(phba, iflag);
			return (1);
		}
		goto retrycmd;
	}

	/* copy results back to user */
	if (psli->sliinit.sli_flag & ELX_SLI2_ACTIVE) {	/* SLI2 mode */
		elx_sli_pcimem_bcopy((uint32_t *) psli->MBhostaddr,
				     (uint32_t *) pmbox,
				     (sizeof (uint32_t) * MAILBOX_CMD_WSIZE));
	} else {		/* NOT SLI2 mode */
		(psli->sliinit.elx_sli_read_slim) ((void *)phba, (void *)pmbox,
						   0,
						   sizeof (uint32_t) *
						   MAILBOX_CMD_WSIZE);
	}

	(psli->sliinit.elx_sli_write_HA) (phba, HA_MBATT);
	psli->sliinit.sli_flag &= ~ELX_SLI_MBOX_ACTIVE;
	ELX_SLI_UNLOCK(phba, iflag);
	return (0);
}

int
dfc_put_event(elxHBA_t * phba,
	      uint32_t evcode, uint32_t evdata0, void *evdata1, void *evdata2)
{
	fcEVT_t *ep;
	fcEVT_t *oep;
	fcEVTHDR_t *ehp = NULL;
	int found;
	DMABUF_t *mp;
	void *fstype;
	SLI_CT_REQUEST *ctp;
	LPFCHBA_t *plhba = (LPFCHBA_t *) phba->pHbaProto;

	ehp = (fcEVTHDR_t *) plhba->fc_evt_head;
	fstype = 0;
	switch (evcode) {
	case FC_REG_CT_EVENT:
		mp = (DMABUF_t *) evdata1;
		ctp = (SLI_CT_REQUEST *) mp->virt;
		fstype = (void *)(ulong) (ctp->FsType);
		break;
	}

	while (ehp) {
		if ((ehp->e_mask == evcode) && (ehp->e_type == fstype))
			break;
		ehp = (fcEVTHDR_t *) ehp->e_next_header;
	}

	if (!ehp) {
		return (0);
	}

	ep = ehp->e_head;
	oep = 0;
	found = 0;

	while (ep && !(found)) {
		if (ep->evt_sleep) {
			switch (evcode) {
			case FC_REG_CT_EVENT:
				if ((ep->evt_type ==
				     (void *)(ulong) FC_FSTYPE_ALL)
				    || (ep->evt_type == fstype)) {
					found++;
					ep->evt_data0 = evdata0;	/* tag */
					ep->evt_data1 = evdata1;	/* buffer ptr */
					ep->evt_data2 = evdata2;	/* count */
					ep->evt_sleep = 0;
					if (ehp->e_mode & E_SLEEPING_MODE) {
						ehp->e_flag |=
						    E_GET_EVENT_ACTIVE;
						lpfc_wakeup(phba, ehp);
					}
					/* For FC_REG_CT_EVENT just give it to first one found */
				}
				break;
			default:
				found++;
				ep->evt_data0 = evdata0;
				ep->evt_data1 = evdata1;
				ep->evt_data2 = evdata2;
				ep->evt_sleep = 0;
				if ((ehp->e_mode & E_SLEEPING_MODE)
				    && !(ehp->e_flag & E_GET_EVENT_ACTIVE)) {
					ehp->e_flag |= E_GET_EVENT_ACTIVE;
					lpfc_wakeup(phba, ehp);
				}
				/* For all other events, give it to every one waiting */
				break;
			}
		}
		oep = ep;
		ep = ep->evt_next;
	}
	return (found);
}

int
dfc_hba_put_event(elxHBA_t * phba,
		  uint32_t evcode,
		  uint32_t evdata1,
		  uint32_t evdata2, uint32_t evdata3, uint32_t evdata4)
{
	HBAEVT_t *rec;
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	rec = &plhba->hbaevt[plhba->hba_event_put];
	rec->fc_eventcode = evcode;

	rec->fc_evdata1 = evdata1;
	rec->fc_evdata2 = evdata2;
	rec->fc_evdata3 = evdata3;
	rec->fc_evdata4 = evdata4;
	plhba->hba_event_put++;
	if (plhba->hba_event_put >= MAX_HBAEVT) {
		plhba->hba_event_put = 0;
	}
	if (plhba->hba_event_put == plhba->hba_event_get) {
		plhba->hba_event_missed++;
		plhba->hba_event_get++;
		if (plhba->hba_event_get >= MAX_HBAEVT) {
			plhba->hba_event_get = 0;
		}
	}

	return (0);
}

int
dfc_data_alloc(elxHBA_t * phba, struct dfc_mem *dm, uint32_t size)
{
	MBUF_INFO_t bfrnfo, *pbfrnfo = &bfrnfo;

	if (dm->fc_dataout)
		return (EACCES);

	size = ((size + 0xfff) & 0xfffff000);
	pbfrnfo->virt = 0;
	pbfrnfo->phys = 0;
	pbfrnfo->flags = ELX_MBUF_VIRT;
	pbfrnfo->align = sizeof (void *);
	pbfrnfo->size = (int)size;
	pbfrnfo->dma_handle = 0;
	elx_malloc(phba, pbfrnfo);
	if (pbfrnfo->virt == NULL) {
		return (ENOMEM);
	}
	dm->fc_dataout = pbfrnfo->virt;
	dm->fc_outsz = size;
	/* dfc_data_alloc */
	elx_printf_log(phba->brd_no, &elx_msgBlk1602,	/* ptr to msg structure */
		       elx_mes1602,	/* ptr to msg */
		       elx_msgBlk1602.msgPreambleStr,	/* begin varargs */
		       (uint32_t) ((ulong) dm->fc_dataout), dm->fc_outsz);	/* end varargs */

	return (0);
}

int
dfc_data_free(elxHBA_t * phba, struct dfc_mem *dm)
{
	MBUF_INFO_t bfrnfo, *pbfrnfo = &bfrnfo;

	/* dfc_data_free */
	elx_printf_log(phba->brd_no, &elx_msgBlk1603,	/* ptr to msg structure */
		       elx_mes1603,	/* ptr to msg */
		       elx_msgBlk1603.msgPreambleStr,	/* begin varargs */
		       (uint32_t) ((ulong) dm->fc_dataout), dm->fc_outsz);	/* end varargs */
	if (dm->fc_dataout == 0)
		return (EACCES);

	pbfrnfo->virt = dm->fc_dataout;
	pbfrnfo->size = dm->fc_outsz;
	pbfrnfo->phys = 0;
	pbfrnfo->flags = ELX_MBUF_VIRT;
	pbfrnfo->align = 0;
	pbfrnfo->dma_handle = 0;
	elx_free(phba, pbfrnfo);
	dm->fc_dataout = 0;
	dm->fc_outsz = 0;

	return (0);
}

DMABUFEXT_t *
dfc_cmd_data_alloc(elxHBA_t * phba,
		   char *indataptr, ULP_BDE64 * bpl, uint32_t size)
{
	MBUF_INFO_t bfrnfo, *pbfrnfo = &bfrnfo;
	DMABUFEXT_t *mlist = 0;
	DMABUFEXT_t *mlast = 0;
	DMABUFEXT_t *dmp;
	int cnt, offset = 0, i = 0;
	unsigned long iflag;

	while (size) {
		/* We get chucks of 4K */
		if (size > 4096)
			cnt = 4096;
		else
			cnt = size;

		/* allocate DMABUFEXT_t buffer header */
		pbfrnfo->virt = 0;
		pbfrnfo->phys = 0;
		pbfrnfo->flags = ELX_MBUF_VIRT;
		pbfrnfo->align = (int)sizeof (long);
		pbfrnfo->size = (int)sizeof (DMABUFEXT_t);
		pbfrnfo->dma_handle = 0;
		elx_malloc(phba, pbfrnfo);
		if (pbfrnfo->virt == 0) {
			goto out;
		}
		dmp = pbfrnfo->virt;
		dmp->dma.next = 0;
		dmp->dma.virt = 0;

		/* Queue it to a linked list */
		if (mlast == 0) {
			mlist = dmp;
			mlast = dmp;
		} else {
			mlast->dma.next = (DMABUF_t *) dmp;
			mlast = dmp;
		}
		dmp->dma.next = 0;

		/* allocate buffer */
		pbfrnfo->virt = 0;
		pbfrnfo->phys = 0;
		pbfrnfo->flags = ELX_MBUF_DMA;
		pbfrnfo->align = (int)4096;
		pbfrnfo->size = (int)cnt;
		pbfrnfo->dma_handle = 0;
		elx_malloc(phba, pbfrnfo);
		if (pbfrnfo->phys == 0) {
			goto out;
		}
		dmp->dma.virt = pbfrnfo->virt;
		if (pbfrnfo->dma_handle) {
			dmp->dma.dma_handle = pbfrnfo->dma_handle;
			dmp->dma.data_handle = pbfrnfo->data_handle;
		}
		dmp->dma.phys = pbfrnfo->phys;
		dmp->size = cnt;

		if (indataptr) {
			/* Copy data from user space in */
			ELX_DRVR_UNLOCK(phba, iflag);
			if (copy_from_user
			    ((uint8_t *) dmp->dma.virt,
			     (uint8_t *) (indataptr + offset), (ulong) cnt)) {
				ELX_DRVR_LOCK(phba, iflag);
				goto out;
			}
			ELX_DRVR_LOCK(phba, iflag);
			bpl->tus.f.bdeFlags = 0;
			elx_pci_dma_sync((void *)phba, (void *)&dmp->dma, 0,
					 ELX_DMA_SYNC_FORDEV);
		} else {
			bpl->tus.f.bdeFlags = BUFF_USE_RCV;
		}

		/* build buffer ptr list for IOCB */
		bpl->addrLow = PCIMEM_LONG(putPaddrLow(dmp->dma.phys));
		bpl->addrHigh = PCIMEM_LONG(putPaddrHigh(dmp->dma.phys));
		bpl->tus.f.bdeSize = (ushort) cnt;
		bpl->tus.w = PCIMEM_LONG(bpl->tus.w);
		bpl++;

		i++;
		offset += cnt;
		size -= cnt;
	}

	mlist->flag = i;
	return (mlist);
      out:
	dfc_cmd_data_free(phba, mlist);
	return (0);
}

int
dfc_rsp_data_copy(elxHBA_t * phba,
		  uint8_t * outdataptr, DMABUFEXT_t * mlist, uint32_t size)
{
	DMABUFEXT_t *mlast = 0;
	int cnt, offset = 0;
	unsigned long iflag;

	while (mlist && size) {
		/* We copy chucks of 4K */
		if (size > 4096)
			cnt = 4096;
		else
			cnt = size;

		mlast = mlist;
		mlist = (DMABUFEXT_t *) mlist->dma.next;

		if (outdataptr) {
			elx_pci_dma_sync((void *)phba, (void *)&mlast->dma, 0,
					 ELX_DMA_SYNC_FORDEV);
			/* Copy data to user space */
			ELX_DRVR_UNLOCK(phba, iflag);
			if (copy_to_user
			    ((uint8_t *) (outdataptr + offset),
			     (uint8_t *) mlast->dma.virt, (ulong) cnt)) {
				ELX_DRVR_LOCK(phba, iflag);
				return (1);
			}
			ELX_DRVR_LOCK(phba, iflag);
		}
		offset += cnt;
		size -= cnt;
	}
	return (0);
}

int
dfc_cmd_data_free(elxHBA_t * phba, DMABUFEXT_t * mlist)
{
	MBUF_INFO_t bfrnfo, *pbfrnfo = &bfrnfo;
	DMABUFEXT_t *mlast;

	while (mlist) {
		mlast = mlist;
		mlist = (DMABUFEXT_t *) mlist->dma.next;
		if (mlast->dma.virt) {
			pbfrnfo->size = mlast->size;
			pbfrnfo->virt = (uint32_t *) mlast->dma.virt;
			pbfrnfo->phys = mlast->dma.phys;
			pbfrnfo->flags = ELX_MBUF_DMA;
			if (mlast->dma.dma_handle) {
				pbfrnfo->dma_handle = mlast->dma.dma_handle;
				pbfrnfo->data_handle = mlast->dma.data_handle;
			}
			elx_free(phba, pbfrnfo);
		}
		pbfrnfo->flags = ELX_MBUF_VIRT;
		pbfrnfo->size = (int)sizeof (DMABUFEXT_t);
		pbfrnfo->virt = (uint32_t *) mlast;
		pbfrnfo->phys = 0;
		pbfrnfo->dma_handle = 0;
		pbfrnfo->data_handle = 0;
		elx_free(phba, pbfrnfo);
	}
	return (0);
}

void
lpfc_decode_firmware_rev(elxHBA_t * phba, char *fwrevision, int flag)
{
	ELX_SLI_t *psli;
	elx_vpd_t *vp;
	uint32_t b1, b2, b3, b4, ldata;
	char c;
	uint32_t i, rev;
	uint32_t *ptr, str[4];

	psli = &phba->sli;
	vp = &phba->vpd;
	if (vp->rev.rBit) {
		if (psli->sliinit.sli_flag & ELX_SLI2_ACTIVE)
			rev = vp->rev.sli2FwRev;
		else
			rev = vp->rev.sli1FwRev;

		b1 = (rev & 0x0000f000) >> 12;
		b2 = (rev & 0x00000f00) >> 8;
		b3 = (rev & 0x000000c0) >> 6;
		b4 = (rev & 0x00000030) >> 4;

		switch (b4) {
		case 0:
			c = 'N';
			break;
		case 1:
			c = 'A';
			break;
		case 2:
			c = 'B';
			break;
		case 3:
		default:
			c = 0;
			break;
		}
		b4 = (rev & 0x0000000f);

		if (psli->sliinit.sli_flag & ELX_SLI2_ACTIVE) {
			for (i = 0; i < 16; i++) {
				if (vp->rev.sli2FwName[i] == 0x20) {
					vp->rev.sli2FwName[i] = 0;
				}
			}
			ptr = (uint32_t *) vp->rev.sli2FwName;
		} else {
			for (i = 0; i < 16; i++) {
				if (vp->rev.sli1FwName[i] == 0x20) {
					vp->rev.sli1FwName[i] = 0;
				}
			}
			ptr = (uint32_t *) vp->rev.sli1FwName;
		}
		for (i = 0; i < 3; i++) {
			ldata = *ptr++;
			ldata = SWAP_DATA(ldata);
			str[i] = ldata;
		}

		if (c == 0) {
			if (flag)
				elx_str_sprintf(fwrevision, "%d.%d%d (%s)",
						(int)b1, (int)b2, (int)b3,
						(char *)str);
			else
				elx_str_sprintf(fwrevision, "%d.%d%d", (int)b1,
						(int)b2, (int)b3);
		} else {
			if (flag)
				elx_str_sprintf(fwrevision, "%d.%d%d%c%d (%s)",
						(int)b1, (int)b2, (int)b3, c,
						(int)b4, (char *)str);
			else
				elx_str_sprintf(fwrevision, "%d.%d%d%c%d",
						(int)b1, (int)b2, (int)b3, c,
						(int)b4);
		}
	} else {
		rev = vp->rev.smFwRev;

		b1 = (rev & 0xff000000) >> 24;
		b2 = (rev & 0x00f00000) >> 20;
		b3 = (rev & 0x000f0000) >> 16;
		c = (char)((rev & 0x0000ff00) >> 8);
		b4 = (rev & 0x000000ff);

		if (flag)
			elx_str_sprintf(fwrevision, "%d.%d%d%c%d ", (int)b1,
					(int)b2, (int)b3, c, (int)b4);
		else
			elx_str_sprintf(fwrevision, "%d.%d%d%c%d ", (int)b1,
					(int)b2, (int)b3, c, (int)b4);
	}
	return;
}

int
lpfc_issue_rptlun(elxHBA_t * phba,
		  MBUF_INFO_t * pbfrnfo, ELXSCSITARGET_t * pscznod)
{

	ELX_SLI_t *psli = &phba->sli;
	ELX_SLI_RING_t *pring = &psli->ring[LPFC_FCP_RING];
	ELX_IOCBQ_t *rspiocbq = 0;
	IOCB_t *cmd;
	IOCB_t *rsp;
	DMABUF_t *mp = 0;
	FCP_RSP *fcprsp;
	int rtnsta = 0;
	int rc;
	ELX_SCSI_BUF_t *elx_cmd = NULL;
	ELX_IOCBQ_t *piocbq;
	LPFC_NODELIST_t *nlp = (LPFC_NODELIST_t *) pscznod->pcontext;

	if (phba->hba_state == ELX_INIT_START) {
		rtnsta = 1;
		goto rptlunxit;
	}
	elx_cmd = lpfc_build_scsi_cmd(phba, nlp, FCP_SCSI_REPORT_LUNS, 0);
	if (elx_cmd) {
		piocbq = &elx_cmd->cur_iocbq;
		piocbq->iocb_cmpl = 0;
		cmd = &(piocbq->iocb);
		mp = (DMABUF_t *) (piocbq->context2);
		piocbq->iocb_flag |= ELX_IO_IOCTL;

		/* Allocate buffer for response iocb */
		if ((rspiocbq =
		     (ELX_IOCBQ_t *) elx_mem_get(phba,
						 MEM_IOCB | MEM_PRI)) == 0) {
			rtnsta = 3;
			goto rptlunxit;
		}
		memset((void *)rspiocbq, 0, sizeof (ELX_IOCBQ_t));
		rsp = &(rspiocbq->iocb);
		piocbq->context2 = NULL;
		piocbq->context1 = NULL;

		memset(pbfrnfo, 0, sizeof (MBUF_INFO_t));
		pbfrnfo->size = 4096;
		pbfrnfo->flags = ELX_MBUF_DMA;
		pbfrnfo->align = (int)4096;
		pbfrnfo->dma_handle = 0;
		elx_malloc(phba, pbfrnfo);
		if (pbfrnfo->virt == 0) {
			rtnsta = 6;
			goto rptlunxit;
		}

		rc = elx_sli_issue_iocb_wait(phba, pring, piocbq,
					     SLI_IOCB_USE_TXQ, rspiocbq,
					     30 + phba->fcp_timeout_offset +
					     ELX_DRVR_TIMEOUT);
		if (rc != IOCB_SUCCESS) {
			rtnsta = 7;
			goto rptlunxit;
		}

		fcprsp = elx_cmd->fcp_rsp;

		elx_printf_log(phba->brd_no, &elx_msgBlk1605,	/* ptr to msg structure */
			       elx_mes1605,	/* ptr to msg */
			       elx_msgBlk1605.msgPreambleStr,	/* begin varargs */
			       nlp->nlp_DID, fcprsp->rspStatus2, fcprsp->rspStatus3, rsp->ulpStatus);	/* end varargs */

		if (rsp->ulpStatus == IOSTAT_FCP_RSP_ERROR) {
			if ((fcprsp->rspStatus2 & RESID_UNDER) &&
			    (fcprsp->rspStatus3 == SCSI_STAT_GOOD)) {
				rtnsta = 0;
				goto rptlunxit;
			}

			if ((fcprsp->rspStatus2 & SNS_LEN_VALID) &&
			    (fcprsp->rspStatus3 == SCSI_STAT_CHECK_COND)) {
				uint8_t *SnsInfo =
				    ((uint8_t *) & fcprsp->rspInfo0)
				    + SWAP_DATA(fcprsp->rspRspLen);

				/* some disks return check condition (sense = ILLEGAL REQUEST) on a SCSI */
				/* REPORT_LUNS command. this isnt an error condition, report success */

				if (((SnsInfo[2] & 0x0f) == SNS_ILLEGAL_REQ)
				    && (SnsInfo[12] == SNSCOD_BADCMD)) {
					/* zero out the data buffer */
					memset((uint8_t *) mp->virt, 0, 1024);
					rtnsta = 0;
				} else {
					rtnsta = 8;
				}
			} else {
				rtnsta = 9;
			}
		}

		if (!rtnsta) {
			if ((*((uint8_t *) mp->virt + 8) & 0xc0) == 0x40) {
				pscznod->addrMode = VOLUME_SET_ADDRESSING;
			}
		}
	} else
		rtnsta = 10;
      rptlunxit:

	if (elx_cmd) {
		if (rtnsta == 0) {
			memcpy(pbfrnfo->virt, mp->virt, 1024);
		}
		elx_mem_put(phba, MEM_IOCB, (uint8_t *) rspiocbq);
		elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
		elx_free_scsi_buf(elx_cmd);
	}
	return (rtnsta);
}

int
lpfc_reset_dev_q_depth(elxHBA_t * phba)
{
	ELXSCSITARGET_t *targetp;
	ELXSCSILUN_t *dev_ptr;
	int i;
	LPFCHBA_t *plhba = (LPFCHBA_t *) phba->pHbaProto;
	elxCfgParam_t *clp = &phba->config[0];

	/*
	 * Find the target and set it to default. 
	 */

	clp = &phba->config[0];
	for (i = 0; i < MAX_FCP_TARGET; ++i) {
		targetp = plhba->device_queue_hash[i];
		if (targetp) {
			dev_ptr = (ELXSCSILUN_t *) targetp->lunlist.q_first;
			while ((dev_ptr != 0)) {
				dev_ptr->lunSched.maxOutstanding =
				    (ushort) clp[LPFC_DFT_LUN_Q_DEPTH].
				    a_current;
				dev_ptr = dev_ptr->pnextLun;
			}
		}
	}
	return (0);
}

int
lpfc_fcp_abort(elxHBA_t * phba, int cmd, int target, int lun)
{
	ELX_SCSI_BUF_t *elx_cmd;
	LPFC_NODELIST_t *pndl;
	uint32_t flag;
	int ret = 0;
	int i = 0;

	flag = ELX_EXTERNAL_RESET;
	switch (cmd) {
	case BUS_RESET:

		{
			for (i = 0; i < MAX_FCP_TARGET; i++) {
				pndl = lpfc_findnode_scsiid(phba, i);
				if (pndl) {
					elx_cmd = elx_get_scsi_buf(phba);
					if (elx_cmd) {

						elx_cmd->scsi_hba = phba;
						elx_cmd->scsi_bus = 0;
						elx_cmd->scsi_target = i;
						elx_cmd->scsi_lun = 0;
						ret =
						    elx_scsi_tgt_reset(elx_cmd,
								       phba, 0,
								       i, flag);
						elx_free_scsi_buf(elx_cmd);
						elx_cmd = 0;
					}
				}
			}
		}
		break;
	case TARGET_RESET:
		{
			/* Obtain node ptr */
			pndl = lpfc_findnode_scsiid(phba, target);
			if (pndl) {
				elx_cmd = elx_get_scsi_buf(phba);
				if (elx_cmd) {

					elx_cmd->scsi_hba = phba;
					elx_cmd->scsi_bus = 0;
					elx_cmd->scsi_target = target;
					elx_cmd->scsi_lun = 0;

					ret =
					    elx_scsi_tgt_reset(elx_cmd, phba, 0,
							       target, flag);
					elx_free_scsi_buf(elx_cmd);
					elx_cmd = 0;
				}
			}
		}
		break;
	case LUN_RESET:
		{
			/* Obtain node ptr */
			pndl = lpfc_findnode_scsiid(phba, target);
			if (pndl) {
				elx_cmd = elx_get_scsi_buf(phba);
				if (elx_cmd) {
					elx_cmd->scsi_hba = phba;
					elx_cmd->scsi_bus = 0;
					elx_cmd->scsi_target = target;
					elx_cmd->scsi_lun = lun;
					ret =
					    elx_scsi_lun_reset(elx_cmd, phba, 0,
							       target, lun,
							       (flag |
								ELX_ISSUE_LUN_RESET));
					elx_free_scsi_buf(elx_cmd);
					elx_cmd = 0;
				}
			}
		}
		break;
	case ABORT_TASK_SET:
		{
			/* Obtain node ptr */
			pndl = lpfc_findnode_scsiid(phba, target);
			if (pndl) {
				elx_cmd = elx_get_scsi_buf(phba);
				if (elx_cmd) {
					elx_cmd->scsi_hba = phba;
					elx_cmd->scsi_bus = 0;
					elx_cmd->scsi_target = target;
					elx_cmd->scsi_lun = lun;
					ret =
					    elx_scsi_lun_reset(elx_cmd, phba, 0,
							       target, lun,
							       (flag |
								ELX_ISSUE_ABORT_TSET));
					elx_free_scsi_buf(elx_cmd);
					elx_cmd = 0;
				}
			}
		}
		break;
	}

	return (ret);
}

void
lpfc_get_hba_model_desc(elxHBA_t * phba, uint8_t * mdp, uint8_t * descp)
{
	LPFCHBA_t *plhba;
	elx_vpd_t *vp;
	uint32_t id;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	vp = &phba->vpd;

	id = elx_read_pci(phba, PCI_VENDOR_ID_REGISTER);

	switch ((id >> 16) & 0xffff) {
	case PCI_DEVICE_ID_SUPERFLY:
		if ((vp->rev.biuRev == 1) ||
		    (vp->rev.biuRev == 2) || (vp->rev.biuRev == 3)) {
			if (mdp) {
				memcpy(mdp, "LP7000", 8);
			}
			if (descp) {
				memcpy(descp,
				       "Emulex LightPulse LP7000 1 Gigabit PCI Fibre Channel Adapter",
				       62);
			}
		} else {
			if (mdp) {
				memcpy(mdp, "LP7000E", 9);
			}
			if (descp) {
				memcpy(descp,
				       "Emulex LightPulse LP7000E 1 Gigabit PCI Fibre Channel Adapter",
				       62);
			}
		}
		break;
	case PCI_DEVICE_ID_DRAGONFLY:
		if (mdp) {
			memcpy(mdp, "LP8000", 8);
		}
		if (descp) {
			memcpy(descp,
			       "Emulex LightPulse LP8000 1 Gigabit PCI Fibre Channel Adapter",
			       62);
		}
		break;
	case PCI_DEVICE_ID_CENTAUR:
		if (FC_JEDEC_ID(vp->rev.biuRev) == CENTAUR_2G_JEDEC_ID) {
			if (mdp) {
				memcpy(mdp, "LP9002", 8);
			}
			if (descp) {
				memcpy(descp,
				       "Emulex LightPulse LP9002 2 Gigabit PCI Fibre Channel Adapter",
				       62);
			}
		} else {
			if (mdp) {
				memcpy(mdp, "LP9000", 8);
			}
			if (descp) {
				memcpy(descp,
				       "Emulex LightPulse LP9000 1 Gigabit PCI Fibre Channel Adapter",
				       62);
			}
		}
		break;
	case PCI_DEVICE_ID_RFLY:
		{
			if (mdp) {
				memcpy(mdp, "LP952", 7);
			}
			if (descp) {
				memcpy(descp,
				       "Emulex LightPulse LP952 2 Gigabit PCI Fibre Channel Adapter",
				       62);
			}
		}
		break;
	case PCI_DEVICE_ID_PEGASUS:
		if (mdp) {
			memcpy(mdp, "LP9802", 8);
		}
		if (descp) {
			memcpy(descp,
			       "Emulex LightPulse LP9802 2 Gigabit PCI Fibre Channel Adapter",
			       62);
		}
		break;
	case PCI_DEVICE_ID_THOR:
		if (mdp) {
			memcpy(mdp, "LP10000", 9);
		}
		if (descp) {
			memcpy(descp,
			       "Emulex LightPulse LP10000 2 Gigabit PCI Fibre Channel Adapter",
			       63);
		}
		break;
	case PCI_DEVICE_ID_VIPER:
		if (mdp) {
			memcpy(mdp, "LPX1000", 9);
		}
		if (descp) {
			memcpy(descp,
			       "Emulex LightPulse LPX1000 10 Gigabit PCI Fibre Channel Adapter",
			       63);
		}
		break;
	case PCI_DEVICE_ID_PFLY:
		if (mdp) {
			memcpy(mdp, "LP982", 7);
		}
		if (descp) {
			memcpy(descp,
			       "Emulex LightPulse LP982 2 Gigabit PCI Fibre Channel Adapter",
			       62);
		}
		break;
	case PCI_DEVICE_ID_TFLY:
		if (mdp) {
			memcpy(mdp, "LP1050", 8);
		}
		if (descp) {
			memcpy(descp,
			       "Emulex LightPulse LP1050 2 Gigabit PCI Fibre Channel Adapter",
			       63);
		}
		break;
	case PCI_DEVICE_ID_LP101:
		if (mdp) {
			memcpy(mdp, "LP101", 7);
		}
		if (descp) {
			memcpy(descp,
			       "Emulex LightPulse LP101 2 Gigabit PCI Fibre Channel Adapter",
			       62);
		}
		break;
	}
}

void
lpfc_get_hba_SymbNodeName(elxHBA_t * phba, uint8_t * symbp)
{
	uint8_t buf[16];
	char fwrev[16];

	lpfc_decode_firmware_rev(phba, fwrev, 0);
	lpfc_get_hba_model_desc(phba, buf, NULL);
	elx_str_sprintf(symbp, "Emulex %s FV%s DV%s",
			buf, fwrev, lpfc_release_version);
}

#include "lpfc_ip.h"

extern int lpfc_nethdr;

static uint8_t fcbroadcastaddr[FC_MAC_ADDRLEN] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

void
lpfc_free_ip_buf_list(elxHBA_t * phba, DMABUFIP_t * pktp)
{

	DMABUFIP_t *tmp, *to_free;
	tmp = pktp;
	while (tmp) {
		to_free = tmp;
		tmp = (DMABUFIP_t *) tmp->dma.next;
		elx_mem_put(phba, MEM_IP_RCV_BUF, (uint8_t *) to_free);
	}
}

void
lpfc_ip_unsol_event(elxHBA_t * phba,
		    ELX_SLI_RING_t * pring, ELX_IOCBQ_t * piocbq)
{
	LPFCHBA_t *plhba;
	ELX_SLI_t *psli;
	IOCB_t *icmd;
	DMABUFIP_t *pktp, *tmp_pkt;
	DMABUFIP_t *pktp_end;
	int i, cnt, pktsize;
	IOCB_t *savecmd;
	LPFC_NETHDR_t *net_hdr;
	LPFC_NODELIST_t *ndlp;
	LPFC_BINDLIST_t *blp;
	ELX_MBOXQ_t *mb;

	psli = &phba->sli;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	icmd = &piocbq->iocb;
	savecmd = icmd;

	if (++plhba->ip_stat->lpfn_recvintr_lsw == 0) {
		plhba->ip_stat->lpfn_recvintr_msw++;
	}

	if (icmd->ulpStatus) {
		/* Handle error status */
		if ((icmd->ulpStatus == IOSTAT_LOCAL_REJECT) &&
		    ((icmd->un.ulpWord[4] & 0xff) ==
		     IOERR_RCV_BUFFER_WAITING)) {

			if (!(plhba->fc_flag & FC_NO_RCV_BUF)) {
				plhba->ip_stat->lpfn_NoRcvBuf++;
				/* IP Response Ring <num> out of posted buffers */
				elx_printf_log(phba->brd_no, &elx_msgBlk0602,	/* ptr to msg struct */
					       elx_mes0602,	/* ptr to msg */
					       elx_msgBlk0602.msgPreambleStr,	/* begin varargs */
					       pring->ringno, pring->missbufcnt, plhba->ip_stat->lpfn_NoRcvBuf);	/* end varargs */
			}
		} else
			plhba->ip_stat->lpfn_ierrors++;

		plhba->fc_flag |= FC_NO_RCV_BUF;
		lpfc_ip_post_buffer(phba, &psli->ring[LPFC_IP_RING], 0);
		/* Update dropped packet statistics here . */
		plhba->ip_stat->lpfn_rx_dropped++;
		return;
	}

	/* piocbq is a ptr to the first rcv IOCB in a chain of IOCBs.
	 * The first being a CMD_RCV_SEQUENCE64_CX followed by a bunch of
	 * CMD_IOCB_CONTINUE64_CN. The IOCBs are chained by piocbq->q_f
	 * in a single linked list. CMD_RCV_SEQUENCE64_CX hold one BDE while
	 * CMD_IOCB_CONTINUE64_CN holds up to 2 BDEs. Each BDE represents
	 * a buffer that was posted to the IP ring.
	 */
	pktp = 0;
	pktp_end = 0;
	pktsize = 0;
	cnt = 0;

	/* Now lets step thru all the CMD_IOCB_CONTINUE64_CN IOCBs, if any */
	while (piocbq) {
		icmd = &piocbq->iocb;
		for (i = 0; i < (int)icmd->ulpBdeCount; i++) {
			if (pktp_end) {
				pktp_end->dma.next =
				    elx_sli_ringpostbuf_get(phba, pring,
							    (elx_dma_addr_t)
							    getPaddr(icmd->un.
								     cont64[i].
								     addrHigh,
								     icmd->un.
								     cont64[i].
								     addrLow));
				if (pktp_end->dma.next == 0) {
					/* Error, cannot match physical address to posted buffer
					 * Clean up pktp chain.
					 */
					lpfc_free_ip_buf_list(phba, pktp);
					plhba->ip_stat->lpfn_rx_dropped++;
					return;
				}
				pktp_end = (DMABUFIP_t *) pktp_end->dma.next;
			} else {
				pktp =
				    (DMABUFIP_t *) elx_sli_ringpostbuf_get(phba,
									   pring,
									   (elx_dma_addr_t)
									   getPaddr
									   (icmd->
									    un.
									    cont64
									    [i].
									    addrHigh,
									    icmd->
									    un.
									    cont64
									    [i].
									    addrLow));
				pktp_end = pktp;
				if (pktp_end == 0) {
					/* Error, cannot match physical address to posted buffer
					 * Clean up pktp chain.
					 */
					plhba->ip_stat->lpfn_rx_dropped++;
					return;
				}
			}
			pktp_end->dma.next = 0;
			lpfc_set_pkt_len(pktp_end->ipbuf,
					 icmd->un.cont64[i].tus.f.bdeSize);
			pktsize += icmd->un.cont64[i].tus.f.bdeSize;

			cnt++;
		}
		piocbq = piocbq->q_f;
	}

	if (++plhba->ip_stat->lpfn_ipackets_lsw == 0)
		plhba->ip_stat->lpfn_ipackets_msw++;

	plhba->ip_stat->lpfn_rcvbytes_lsw += pktsize;
	if (plhba->ip_stat->lpfn_rcvbytes_lsw < pktsize)
		plhba->ip_stat->lpfn_rcvbytes_msw++;

	/* repost new buffers to the HBA to replace these buffers.
	 * When the upper layer is done processing the buffer in the pktp chain,
	 * they should be put back in the MEM_IP_RCV_BUF pool.
	 */
	lpfc_ip_post_buffer(phba, &psli->ring[LPFC_IP_RING], cnt);

	net_hdr = (LPFC_NETHDR_t *) lpfc_get_pkt_data(pktp->ipbuf);

	/* If this is first broadcast received from that address */
	if (savecmd->un.xrseq.w5.hcsw.Fctl & BC) {
	      bcst:
		if (++plhba->ip_stat->lpfn_brdcstrcv_lsw == 0) {
			plhba->ip_stat->lpfn_brdcstrcv_msw++;
		}
		memcpy(net_hdr->fc_destname.IEEE, (char *)fcbroadcastaddr,
		       FC_MAC_ADDRLEN);

		if ((ndlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL,
					      (uint32_t) savecmd->un.xrseq.
					      xrsqRo)) == 0) {

			/* Need to cache the did / portname */
			if ((ndlp =
			     (LPFC_NODELIST_t *) elx_mem_get(phba, MEM_NLP))) {
				memset((void *)ndlp, 0,
				       sizeof (LPFC_NODELIST_t));
				ndlp->nlp_DID = savecmd->un.xrseq.xrsqRo;
				memcpy(&ndlp->nlp_portname,
				       &net_hdr->fc_srcname,
				       sizeof (NAME_TYPE));
				ndlp->nlp_state = NLP_MAPPED_LIST;
				blp = ndlp->nlp_listp_bind;
				if (blp != NULL)
					lpfc_nlp_bind(phba, blp);
			} else {
			      dropout:
				lpfc_free_ip_buf_list(phba, pktp);
				plhba->ip_stat->lpfn_rx_dropped++;
				/* Update the statistics for dropped packets here. */
				return;
			}
		}
	} else {
		if ((ndlp = lpfc_findnode_rpi(phba, savecmd->ulpIoTag)) == 0) {
			if (net_hdr->fc_destname.IEEE[0] == 0xff) {
				if ((net_hdr->fc_destname.IEEE[1] == 0xff) &&
				    (net_hdr->fc_destname.IEEE[2] == 0xff) &&
				    (net_hdr->fc_destname.IEEE[3] == 0xff) &&
				    (net_hdr->fc_destname.IEEE[4] == 0xff) &&
				    (net_hdr->fc_destname.IEEE[5] == 0xff)) {
					goto bcst;
				}
			}
			/* Need to send LOGOUT for this RPI */
			if ((mb = (ELX_MBOXQ_t *) elx_mem_get(phba, MEM_MBOX))) {
				lpfc_read_rpi(phba,
					      (uint32_t) savecmd->ulpIoTag,
					      (ELX_MBOXQ_t *) mb,
					      (uint32_t) ELS_CMD_LOGO);
				if (elx_sli_issue_mbox
				    (phba, (ELX_MBOXQ_t *) mb,
				     MBX_NOWAIT) != MBX_BUSY) {
					elx_mem_put(phba, MEM_MBOX,
						    (uint8_t *) mb);
				}
			}
			goto dropout;
		}
	}

	if ((plhba->lpfn_ip_rcv)) {
		(plhba->lpfn_ip_rcv) (phba, pktp, pktsize);
	}

	tmp_pkt = pktp;

	/* Do not free the message block. Free only its DMA mapping. */
	while (tmp_pkt) {
		tmp_pkt->ipbuf = NULL;
		tmp_pkt = (DMABUFIP_t *) tmp_pkt->dma.next;
	}
	lpfc_free_ip_buf_list(phba, pktp);

}

LPFC_IP_BUF_t *
lpfc_get_ip_buf(elxHBA_t * phba)
{
	LPFC_IP_BUF_t *pib;
	DMABUF_t *pdma;
	ULP_BDE64 *bpl;
	IOCB_t *cmd;
	uint8_t *ptr;
	uint32_t cnt;
	elx_dma_addr_t dma;

	/* Get a IP buffer for an I/O */
	if ((pib = (LPFC_IP_BUF_t *) elx_mem_get(phba, MEM_IP_BUF)) == 0) {
		return (0);
	}
	memset(pib, 0, sizeof (LPFC_IP_BUF_t));

	/* Get a IP DMA extention for an I/O */
	/*
	 * The DMA buffer for FC Network Header and BPL use MEM_IP_DMA_EXT
	 *  memory segment.
	 *
	 *    The size of MEM_BPL   = 1024 bytes.
	 *
	 *    The size of FC Header  = 24 bytes + 8 extra.
	 *    The size of ULP_BDE64 = 12 bytes and driver can only support
	 *       LPFC_IP_INITIAL_BPL_SIZE (80) S/G segments.
	 *
	 *    Total usage for each I/O use 992 bytes.
	 */
	if ((pdma = (DMABUF_t *) elx_mem_get(phba, MEM_IP_DMA_EXT)) == 0) {
		elx_mem_put(phba, MEM_IP_BUF, (uint8_t *) pib);
		return (0);
	}
	/* Save DMABUF ptr for put routine */
	pib->dma_ext = pdma;

	/* This is used to save extra BPLs that are chained to pdma.
	 * Only used if I/O has more then 80 data segments.
	 */
	pdma->next = 0;

	/* Save virtual ptr to BPL and phba */
	cnt = 0;
	ptr = (uint8_t *) pdma->virt;
	memset(ptr, 0, sizeof (LPFC_IPHDR_t));
	ptr += (sizeof (LPFC_IPHDR_t) + 0x8);	/* extra 8 to be safe */
	pib->ip_bpl = (ULP_BDE64 *) ptr;
	pib->ip_hba = phba;

	dma = pdma->phys;
	if (lpfc_nethdr == 0) {
		pib->net_hdr = (LPFC_IPHDR_t *) pdma->virt;
		bpl = pib->ip_bpl;

		/* Setup FC Network Header */
		bpl->addrHigh = PCIMEM_LONG(putPaddrHigh(dma));
		bpl->addrLow = PCIMEM_LONG(putPaddrLow(dma));
		bpl->tus.f.bdeSize = sizeof (LPFC_IPHDR_t);
		bpl->tus.f.bdeFlags = BDE64_SIZE_WORD;
		bpl->tus.w = PCIMEM_LONG(bpl->tus.w);
		bpl++;
		cnt++;
		pib->ip_bpl++;
	}

	dma += (sizeof (LPFC_IPHDR_t) + 0x8);

	cmd = &pib->cur_iocbq.iocb;
	cmd->un.xseq64.bdl.ulpIoTag32 = 0;
	cmd->un.xseq64.bdl.addrHigh = putPaddrHigh(dma);
	cmd->un.xseq64.bdl.addrLow = putPaddrLow(dma);
	cmd->un.xseq64.bdl.bdeSize = (cnt * sizeof (ULP_BDE64));
	cmd->un.xseq64.bdl.bdeFlags = BUFF_TYPE_BDL;
	cmd->ulpBdeCount = 1;
	cmd->ulpOwner = OWN_CHIP;
	return (pib);
}

void
lpfc_free_ip_buf(LPFC_IP_BUF_t * pib)
{
	elxHBA_t *phba;
	DMABUF_t *pdma;
	DMABUF_t *pbpl;
	DMABUF_t *pnext;

	if (pib) {
		phba = pib->ip_hba;
		if ((pdma = pib->dma_ext)) {
			/* Check to see if there were any extra buffers used to chain BPLs */
			pbpl = pdma->next;
			while (pbpl) {
				pnext = pbpl->next;
				elx_mem_put(phba, MEM_BPL, (uint8_t *) pbpl);
				pbpl = pnext;
			}
			elx_mem_put(phba, MEM_IP_DMA_EXT, (uint8_t *) pdma);
		}
		elx_mem_put(phba, MEM_IP_BUF, (uint8_t *) pib);
	}
	return;
}

void
lpfc_ip_finish_cmd(elxHBA_t * phba,
		   ELX_IOCBQ_t * cmdiocb, ELX_IOCBQ_t * rspiocb)
{
	LPFCHBA_t *plhba;
	LPFC_IP_BUF_t *pib;
	IOCB_t *irsp;
	LPFC_NODELIST_t *ndlp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	irsp = &rspiocb->iocb;
	ndlp = (LPFC_NODELIST_t *) cmdiocb->context1;
	pib = (LPFC_IP_BUF_t *) cmdiocb->context2;

	if (++plhba->ip_stat->lpfn_xmitintr_lsw == 0) {
		plhba->ip_stat->lpfn_xmitintr_msw++;
	}

	plhba->ip_stat->lpfn_xmitque_cur--;

	/* If the IP packet, XMIT_SEQUENCE64 returns an error, a new XRI
	 * must be created for subsequent requests.
	 */
	if (irsp->ulpStatus) {
		plhba->ip_stat->lpfn_oerrors++;
		/* Xmit Sequence completion error */
		elx_printf_log(phba->brd_no, &elx_msgBlk0603,	/* ptr to msg struct */
			       elx_mes0603,	/* ptr to msg */
			       elx_msgBlk0603.msgPreambleStr,	/* begin varargs */
			       irsp->ulpStatus, irsp->ulpIoTag, irsp->un.ulpWord[4], ndlp->nlp_DID);	/* end varargs */

		if ((irsp->ulpContext == ndlp->nlp_xri) &&
		    (!(ndlp->nlp_flag & NLP_CREATE_XRI_INP))) {
			ndlp->nlp_xri = 0;
			lpfc_ip_create_xri(phba, 0, ndlp);
		}
	} else {
		/* Increment number of packets txmited */
		if (++plhba->ip_stat->lpfn_opackets_lsw == 0)
			plhba->ip_stat->lpfn_opackets_msw++;
		/* Increment number of bytes txmited */

	}
	if (pib) {
		lpfc_ip_unprep_io(phba, pib, 1);
		lpfc_free_ip_buf(pib);
	}
	return;
}

int
lpfc_ip_xri_wait(elxHBA_t * phba, LPFC_IP_BUF_t * pib, LPFC_NODELIST_t * ndlp)
{
	int ret_val;
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	ret_val = 0;

	if (!(ndlp->nlp_flag & NLP_CREATE_XRI_INP)) {
		/* pib, the initial packet, will be sent when create_xri completes */
		ret_val = lpfc_ip_create_xri(phba, pib, ndlp);
		if (ret_val) {
			return (ret_val);
		}
	} else {
		/* The XRI was sent previously.  Since the lpfc_ip_create_xri routine queues the 
		 * first ip buffer into the xri create iocb command, queue this ip
		 * buffer into the ndlp list.
		 */
		elx_tqs_enqueue(&ndlp->nlp_listp_ipbuf, pib, ip_buf_next);
	}

	return (0);
}

int
lpfc_ip_xmit(LPFC_IP_BUF_t * pib)
{
	elxHBA_t *phba;
	ELX_SLI_t *psli;
	ELX_IOCBQ_t *piocbq;
	IOCB_t *piocb;
	LPFC_NODELIST_t *ndlp;
	LPFCHBA_t *plhba;
	ELX_SLI_RING_t *pring;
	LPFC_NODE_FARP_PEND_t *pndlpfarp;
	int room;
	int farp_ret = 0;
	uint8_t issue_farp = 0;

	ndlp = lpfc_ip_find_device(pib, &phba);
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	/* various error checks: HBA online (cable plugged), this target
	   not in error recovery of some sort */
	if ((ndlp != 0) && (ndlp->nle.nlp_failMask & ELX_DEV_FATAL_ERROR)) {
		plhba->ip_stat->lpfn_tx_dropped++;
		return (ENXIO);
	}

	/* If a node was not found and there was no error, set the issue_farp flag
	 * so that the routine properly initializes the iocb.
	 */
	if (ndlp == NULL)
		issue_farp = 1;

	/* allocate an iocb command */
	piocbq = &(pib->cur_iocbq);
	piocb = &piocbq->iocb;
	psli = &phba->sli;

	if (issue_farp == 0)
		pib->ndlp = ndlp;

	if (lpfc_ip_prep_io(phba, pib) == 1) {

		plhba->ip_stat->lpfn_tx_dropped++;
		return (ENXIO);
	}

	/* Setup fibre channel header information */
	piocb->un.xrseq.w5.hcsw.Fctl = 0;
	piocb->un.xrseq.w5.hcsw.Dfctl = FC_NET_HDR;	/* network headers */
	piocb->un.xrseq.w5.hcsw.Rctl = FC_UNSOL_DATA;
	piocb->un.xrseq.w5.hcsw.Type = FC_LLC_SNAP;

	/* Get an iotag and finish setup of IOCB  */
	piocb->ulpIoTag =
	    elx_sli_next_iotag(phba, &phba->sli.ring[psli->ip_ring]);

	/* Set the timeout value */
	piocb->ulpTimeout = LPFC_IP_TOV;
	piocbq->drvrTimeout = LPFC_IP_TOV + ELX_DRVR_TIMEOUT;

	if (issue_farp == 0) {
		piocb->ulpContext = ndlp->nle.nlp_rpi;
		if (ndlp->nle.nlp_ip_info & NLP_FCP_2_DEVICE) {
			piocb->ulpFCP2Rcvy = 1;
		}
	}

	/* set up iocb return path by setting the context2 field to pib
	 * and the completion function to lpfc_ip_finish_cmd function
	 */
	piocbq->iocb_cmpl = lpfc_ip_finish_cmd;

	if (issue_farp == 0)
		piocbq->context1 = ndlp;

	piocbq->context2 = pib;

	if (issue_farp == 0) {
		if (ndlp->nlp_DID == Bcast_DID) {
			piocb->ulpCommand = CMD_XMIT_BCAST64_CN;
		} else {
			piocb->ulpCommand = CMD_XMIT_SEQUENCE64_CX;
			piocb->ulpClass = (ndlp->nle.nlp_ip_info & 0x0f);
			if (ndlp->nlp_xri == 0) {
				/* If we don't have an XRI, we must first create one */
				if (lpfc_ip_xri_wait(phba, pib, ndlp)) {
					plhba->ip_stat->lpfn_tx_dropped++;
					return (ENXIO);
				}
				return (0);
			}
			piocb->ulpContext = ndlp->nlp_xri;
		}
	}

	pring = &phba->sli.ring[psli->ip_ring];
	room = pring->txq.q_max - plhba->ip_stat->lpfn_xmitque_cur;

	if ((pring->txq.q_max > 0) && (room < 0)) {
		/* No room on IP xmit queue */
		elx_printf_log(phba->brd_no, &elx_msgBlk0605,	/* ptr to msg struct */
			       elx_mes0605,	/* ptr to msg */
			       elx_msgBlk0605.msgPreambleStr,	/* begin varargs */
			       pring->missbufcnt);	/* end varargs */
		plhba->ip_stat->lpfn_tx_dropped++;
		lpfc_ip_unprep_io(phba, pib, 0);
		return (ENXIO);
	}

	/* Either issue a FARP Request or just issue the IOCB. */
	if (issue_farp == 1) {
		/* Allocate a new NODE_FARP_PEND structure, initialize it, and
		 * issue the farp.  
		 */
		pndlpfarp =
		    (LPFC_NODE_FARP_PEND_t *)
		    elx_kmem_zalloc(sizeof (LPFC_NODE_FARP_PEND_t),
				    ELX_MEM_NDELAY);
		if (pndlpfarp == NULL)
			return ENXIO;

		memcpy((void *)&pndlpfarp->rnode_addr,
		       (void *)&pib->net_hdr->fcnet.fc_destname,
		       sizeof (NAME_TYPE));
		elx_tqs_enqueue(&pndlpfarp->fc_ipbuf_list_farp_wait, pib,
				ip_buf_next);

		pndlpfarp->fc_ipfarp_tmo =
		    elx_clk_set(phba, plhba->fc_ipfarp_timeout,
				lpfc_ipfarp_timeout,
				(void *)&pndlpfarp->rnode_addr, 0);
		if (pndlpfarp->fc_ipfarp_tmo == NULL) {
			farp_ret = ENXIO;
			elx_kmem_free(pndlpfarp,
				      sizeof (LPFC_NODE_FARP_PEND_t));
			return (farp_ret);
		}

		/* Send a FARP to resolve this node's WWPN to Port ID and generate a PLOGI. */
		farp_ret = 0;
		farp_ret = lpfc_issue_els_farp(phba,
					       (uint8_t *) & pib->net_hdr->
					       fcnet.fc_destname,
					       LPFC_FARP_BY_WWPN);
		if (farp_ret != 0) {
			farp_ret = ENXIO;
			elx_clk_can(phba, pndlpfarp->fc_ipfarp_tmo);
			pib =
			    elx_tqs_dequeuefirst(&pndlpfarp->
						 fc_ipbuf_list_farp_wait,
						 ip_buf_next);
			elx_kmem_free(pndlpfarp,
				      sizeof (LPFC_NODE_FARP_PEND_t));
		} else {
			elx_tqs_enqueue(&plhba->fc_node_farp_list, pndlpfarp,
					pnext);
		}

		return (farp_ret);
	}

	if (elx_sli_issue_iocb(phba, pring,
			       piocbq, SLI_IOCB_USE_TXQ) == IOCB_ERROR) {
		plhba->ip_stat->lpfn_tx_dropped++;
		lpfc_ip_unprep_io(phba, pib, 0);
		return (ENXIO);
	}

	plhba->ip_stat->lpfn_xmitque_cur++;
	return (0);
}

LPFC_NODELIST_t *
lpfc_ip_find_device(LPFC_IP_BUF_t * pib, elxHBA_t ** pphba)
{
	elxHBA_t *phba;
	LPFC_NODELIST_t *ndlp;
	LPFCHBA_t *plhba;
	uint32_t j;
	uint8_t *addr;

	phba = pib->ip_hba;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	*pphba = phba;
	addr = (uint8_t *) pib->net_hdr;
	addr += 2;

	ndlp = plhba->fc_nlpunmap_start;
	if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start)
		ndlp = plhba->fc_nlpmap_start;
	while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start) {

		/* IF portname matches IEEE address, return LPFC_NODELIST_t entry */
		if ((ndlp->nlp_portname.IEEE[0] == addr[0])) {
			if ((ndlp->nlp_state < NLP_STE_REG_LOGIN_ISSUE) ||
			    ((ndlp->nlp_DID != Bcast_DID) &&
			     ((ndlp->nlp_DID & CT_DID_MASK) == CT_DID_MASK))) {
				ndlp =
				    (LPFC_NODELIST_t *) ndlp->nle.
				    nlp_listp_next;
				if (ndlp ==
				    (LPFC_NODELIST_t *) & plhba->
				    fc_nlpunmap_start)
					ndlp = plhba->fc_nlpmap_start;
				continue;
			}

			/*
			 * Well-Known address ports have the same MAC address
			 * as one of the IP port, skip all the well-known ports.
			 */
			if (((ndlp->nlp_DID & WELL_KNOWN_DID_MASK) ==
			     WELL_KNOWN_DID_MASK)
			    && (ndlp->nlp_DID != Bcast_DID)) {
				continue;
			}

			/* check rest of bytes in address / portname */
			for (j = 1; j < FC_MAC_ADDRLEN; j++) {
				if (ndlp->nlp_portname.IEEE[j] != addr[j])
					break;
			}

			if (j == FC_MAC_ADDRLEN) {
				return (ndlp);
			}
		}
		ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
		if (ndlp == (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start)
			ndlp = plhba->fc_nlpmap_start;
	}

	/* Not in unmap or map list. */
	ndlp = 0;
	if (ndlp == 0) {
		/* If high bit is set, its either broadcast or multicast */
		if (pib->net_hdr->fcnet.fc_destname.IEEE[0] & 0x80) {
			ndlp = &plhba->fc_nlp_bcast;
		}
	}

	return (ndlp);
}

/************************************************************************/
/* lpfc_ip_create_xri: create an exchange for the IP traffice with the  */
/* remote node                                                          */
/************************************************************************/
int
lpfc_ip_create_xri(elxHBA_t * phba, LPFC_IP_BUF_t * pib, LPFC_NODELIST_t * ndlp)
{
	ELX_SLI_t *psli;
	ELX_SLI_RING_t *pring;
	IOCB_t *icmd;
	ELX_IOCBQ_t *iocb;
	LPFCHBA_t *plhba;

	psli = &phba->sli;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	pring = &psli->ring[LPFC_IP_RING];	/* IP ring */

	/* Allocate buffer for  command iocb */
	if ((iocb = (ELX_IOCBQ_t *) elx_mem_get(phba, MEM_IOCB | MEM_PRI)) == 0) {
		return (1);
	}
	memset((void *)iocb, 0, sizeof (ELX_IOCBQ_t));
	ndlp->nlp_flag |= NLP_CREATE_XRI_INP;
	ndlp->nle.nlp_type |= NLP_IP_NODE;
	icmd = &iocb->iocb;

	/* set up an iotag so we can match the completion to an iocb/mbuf */
	icmd->ulpIoTag = elx_sli_next_iotag(phba, pring);
	icmd->ulpContext = ndlp->nle.nlp_rpi;
	icmd->ulpLe = 1;
	icmd->ulpCommand = CMD_CREATE_XRI_CR;
	icmd->ulpOwner = OWN_CHIP;

	iocb->context1 = (uint8_t *) ndlp;
	iocb->context2 = (uint8_t *) pib;
	iocb->iocb_cmpl = lpfc_ip_xri_cmpl;

	/* The XRI create is on its way.  Start a timer that will flush the
	 * pending IP buffers if the XRI is still 0.
	 */
	ndlp->nlp_xri_tmofunc =
	    elx_clk_set(phba, plhba->fc_ipxri_timeout, lpfc_ip_xri_timeout,
			(void *)iocb, 0);
	if (ndlp->nlp_xri_tmofunc == NULL) {
		return (1);
	}

	elx_printf_log(phba->brd_no, &elx_msgBlk0606, elx_mes0606, elx_msgBlk0606.msgPreambleStr,	/* begin varargs */
		       ndlp->nlp_DID);	/* end varargs */

	if (elx_sli_issue_iocb(phba, pring, iocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		elx_mem_put(phba, MEM_IOCB, (uint8_t *) iocb);
	}
	return (0);
}

void
lpfc_ip_xri_cmpl(elxHBA_t * phba, ELX_IOCBQ_t * cmdiocb, ELX_IOCBQ_t * rspiocb)
{
	IOCB_t *irsp;
	ELX_SLI_t *psli;
	LPFC_NODELIST_t *ndlp;
	LPFC_NODE_FARP_PEND_t *pndlpfarp;
	LPFC_IP_BUF_t *pib;
	ELX_IOCBQ_t *piocbq;
	IOCB_t *piocb;
	LPFCHBA_t *plhba;
	uint16_t loop_cnt;

	pndlpfarp = NULL;
	psli = &phba->sli;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	irsp = &rspiocb->iocb;
	ndlp = (LPFC_NODELIST_t *) cmdiocb->context1;
	ndlp->nlp_flag &= ~NLP_CREATE_XRI_INP;

	/* Cancel the timer callback.  This routine will handle the completion
	 * and pending ip buffers.
	 */
	if (ndlp->nlp_xri_tmofunc) {
		elx_clk_can(phba, ndlp->nlp_xri_tmofunc);
		ndlp->nlp_xri_tmofunc = 0;
	}

	elx_printf_log(phba->brd_no, &elx_msgBlk0607, elx_mes0607, elx_msgBlk0607.msgPreambleStr,	/* begin varargs */
		       ndlp->nlp_DID, irsp->ulpStatus, irsp->ulpContext);	/* end varargs */

	/* Look to see if this node depended on a FARP and has buffers pending in that list. 
	 * The driver needs that information whether or not the XRI completed successfully.
	 */
	if (elx_tqs_getcount(&plhba->fc_node_farp_list)) {
		pndlpfarp =
		    elx_tqs_dequeuefirst(&plhba->fc_node_farp_list, pnext);
		loop_cnt = 0;
		while (pndlpfarp != NULL) {
			if ((lpfc_geportname
			     (&pndlpfarp->rnode_addr, &ndlp->nlp_portname) != 2)
			    &&
			    (lpfc_geportname
			     (&pndlpfarp->rnode_addr,
			      &ndlp->nlp_nodename) != 2)) {
				elx_tqs_enqueue(&plhba->fc_node_farp_list,
						pndlpfarp, pnext);
				pndlpfarp =
				    elx_tqs_dequeuefirst(&plhba->
							 fc_node_farp_list,
							 pnext);
			} else
				break;

			loop_cnt += 1;
			if (loop_cnt > plhba->fc_node_farp_list.q_cnt)
				pndlpfarp = NULL;
		}
	}

	if (irsp->ulpStatus) {
		if (pndlpfarp) {
			/* Free all ip buffers from the plhba list. */
			pib =
			    elx_tqs_dequeuefirst(&pndlpfarp->
						 fc_ipbuf_list_farp_wait,
						 ip_buf_next);
			while (pib != NULL) {
				lpfc_free_ip_buf(pib);
				pib =
				    elx_tqs_dequeuefirst(&pndlpfarp->
							 fc_ipbuf_list_farp_wait,
							 ip_buf_next);
			}
			/* The FARP is now done.  Free all resources allocated to it. */
			elx_kmem_free(pndlpfarp,
				      sizeof (LPFC_NODE_FARP_PEND_t));
		}

		/* If the response iocb has a ULP error status, free all pending IP buffers
		 * starting with the xri command iocb. 
		 */
		pib = (LPFC_IP_BUF_t *) cmdiocb->context2;
		if (pib) {
			lpfc_free_ip_buf(pib);
		}

		/* Now free all ip buffers staged in the node's ip buffer linked list.  */
		pib = elx_tqs_dequeuefirst(&ndlp->nlp_listp_ipbuf, ip_buf_next);
		while (pib != NULL) {
			lpfc_free_ip_buf(pib);
			pib =
			    elx_tqs_dequeuefirst(&ndlp->nlp_listp_ipbuf,
						 ip_buf_next);
		}

		/* Reinitialize the list as empty and free the cmd iocb. */
		ndlp->nlp_listp_ipbuf.q_first = ndlp->nlp_listp_ipbuf.q_last =
		    NULL;
		elx_mem_put(phba, MEM_IOCB, (uint8_t *) cmdiocb);
		return;
	}

	ndlp->nlp_xri = irsp->ulpContext;

	/* See if the driver has packets initially pending from a FARP request
	 * and send them. 
	 */
	if (pndlpfarp) {
		if (elx_tqs_getcount(&pndlpfarp->fc_ipbuf_list_farp_wait)) {
			pib =
			    elx_tqs_dequeuefirst(&pndlpfarp->
						 fc_ipbuf_list_farp_wait,
						 ip_buf_next);
			while (pib != NULL) {
				pib->ndlp = ndlp;
				piocbq = &(pib->cur_iocbq);
				piocb = &piocbq->iocb;

				/* IOCBs for IP packets pended while the FARP process completes need more
				 * initialization prior to issuing to the SLI because there was no node
				 * information at the time of the transmit.
				 */
				piocbq->context1 = ndlp;
				if (ndlp->nle.nlp_ip_info & NLP_FCP_2_DEVICE)
					piocb->ulpFCP2Rcvy = 1;

				if (ndlp->nlp_DID == Bcast_DID) {
					piocb->ulpCommand = CMD_XMIT_BCAST64_CN;
					piocb->ulpContext = ndlp->nle.nlp_rpi;
				} else {
					piocb->ulpCommand =
					    CMD_XMIT_SEQUENCE64_CX;
					piocb->ulpClass =
					    (ndlp->nle.nlp_ip_info & 0x0f);
					piocb->ulpContext = ndlp->nlp_xri;
				}

				elx_sli_issue_iocb(phba,
						   &psli->ring[psli->ip_ring],
						   piocbq, SLI_IOCB_USE_TXQ);
				pib =
				    elx_tqs_dequeuefirst(&pndlpfarp->
							 fc_ipbuf_list_farp_wait,
							 ip_buf_next);
			}
		}
	}

	/* Send the initial packet stored in the completed XRI_CREATE IOCB command. */
	pib = (LPFC_IP_BUF_t *) cmdiocb->context2;
	if (pib) {
		piocbq = &(pib->cur_iocbq);
		piocb = &piocbq->iocb;
		piocb->ulpContext = ndlp->nlp_xri;
		elx_sli_issue_iocb(phba, &psli->ring[psli->ip_ring], piocbq,
				   SLI_IOCB_USE_TXQ);
	}

	/* Send all subsequent packets waiting for CREATE_XRI to finish.  These packets
	 * are linked in the node's linked list of ip buffers to preserve any ordering
	 * constraints. 
	 */
	pib = elx_tqs_dequeuefirst(&ndlp->nlp_listp_ipbuf, ip_buf_next);
	while (pib != NULL) {
		piocbq = &(pib->cur_iocbq);
		piocb = &piocbq->iocb;
		piocb->ulpContext = ndlp->nlp_xri;
		elx_sli_issue_iocb(phba, &psli->ring[psli->ip_ring], piocbq,
				   SLI_IOCB_USE_TXQ);
		pib = elx_tqs_dequeuefirst(&ndlp->nlp_listp_ipbuf, ip_buf_next);
	}

	/* Recover the IOCB that carried the xri create request.  The command
	 * has completed.
	 */
	elx_mem_put(phba, MEM_IOCB, (uint8_t *) cmdiocb);
	return;
}

int
lpfc_ip_flush_iocb(elxHBA_t * phba,
		   ELX_SLI_RING_t * pring,
		   LPFC_NODELIST_t * ndlp, LPFC_IP_FLUSH_EVENT flush_event)
{
	ELX_SLI_t *psli;
	psli = &phba->sli;

	if (flush_event == FLUSH_RING) {
		/* This call aborts all iocbs on the specified ring. */
		elx_sli_abort_iocb_ring(phba, &phba->sli.ring[psli->ip_ring],
					ELX_SLI_ABORT_IMED);
	} else if (flush_event == FLUSH_NODE) {
		/* This call aborts all iocbs on the specified ring matching the given
		 * ndlp.
		 */
		elx_sli_abort_iocb_context1(phba,
					    &phba->sli.ring[psli->ip_ring],
					    ndlp);
	}

	return (0);
}

int
lpfc_ip_post_buffer(elxHBA_t * phba, ELX_SLI_RING_t * pring, int cnt)
{
	IOCB_t *icmd;
	ELX_IOCBQ_t *iocb;
	DMABUFIP_t *mp1, *mp2;

	cnt += pring->missbufcnt;
	/* While there are buffers to post */
	while (cnt > 0) {
		/* Allocate buffer for  command iocb */
		if ((iocb =
		     (ELX_IOCBQ_t *) elx_mem_get(phba,
						 MEM_IOCB | MEM_PRI)) == 0) {
			pring->missbufcnt = cnt;
			goto out;
		}
		memset((void *)iocb, 0, sizeof (ELX_IOCBQ_t));
		icmd = &iocb->iocb;

		/* 2 buffers can be posted per command */
		/* Allocate buffer to post */
		if ((mp1 =
		     (DMABUFIP_t *) elx_mem_get(phba, MEM_IP_RCV_BUF)) == 0) {
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) iocb);
			pring->missbufcnt = cnt;
			goto out;
		}
		/* Allocate buffer to post */
		if (cnt > 1) {
			if ((mp2 =
			     (DMABUFIP_t *) elx_mem_get(phba,
							MEM_IP_RCV_BUF)) == 0) {
				elx_mem_put(phba, MEM_IP_RCV_BUF,
					    (uint8_t *) mp1);
				elx_mem_put(phba, MEM_IOCB, (uint8_t *) iocb);
				pring->missbufcnt = cnt;
				goto out;
			}
		} else {
			mp2 = 0;
		}
		icmd->un.cont64[0].addrHigh = putPaddrHigh(mp1->dma.phys);
		icmd->un.cont64[0].addrLow = putPaddrLow(mp1->dma.phys);
		icmd->un.cont64[0].tus.f.bdeSize = LPFC_IP_RCV_BUF_SIZE;
		icmd->ulpBdeCount = 1;
		cnt--;
		if (mp2) {
			icmd->un.cont64[1].addrHigh =
			    putPaddrHigh(mp2->dma.phys);
			icmd->un.cont64[1].addrLow = putPaddrLow(mp2->dma.phys);

			icmd->un.cont64[1].tus.f.bdeSize = LPFC_IP_RCV_BUF_SIZE;
			cnt--;
			icmd->ulpBdeCount = 2;
		}

		icmd->ulpCommand = CMD_QUE_RING_BUF64_CN;
		icmd->ulpIoTag = elx_sli_next_iotag(phba, pring);
		icmd->ulpLe = 1;
		icmd->ulpOwner = OWN_CHIP;

		if (elx_sli_issue_iocb(phba, pring, iocb, SLI_IOCB_USE_TXQ) ==
		    IOCB_ERROR) {
			elx_mem_put(phba, MEM_IP_RCV_BUF, (uint8_t *) mp1);
			if (mp2) {
				elx_mem_put(phba, MEM_IP_RCV_BUF,
					    (uint8_t *) mp2);
			}
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) iocb);
			pring->missbufcnt = cnt;
			goto out;;
		}

		elx_sli_ringpostbuf_put(phba, pring, &mp1->dma);
		if (mp2) {
			elx_sli_ringpostbuf_put(phba, pring, &mp2->dma);
		}
	}
	pring->missbufcnt = 0;
	return (0);
      out:
	/* Post buffer for IP ring <num> failed */
	elx_printf_log(phba->brd_no, &elx_msgBlk0604,	/* ptr to msg struct */
		       elx_mes0604,	/* ptr to msg */
		       elx_msgBlk0604.msgPreambleStr,	/* begin varargs */
		       pring->ringno, pring->missbufcnt);	/* end varargs */
	return (cnt);

}

void
lpfc_ipfarp_timeout(elxHBA_t * phba, void *l1, void *l2)
{
	LPFC_NODELIST_t *ndlp;
	LPFC_IP_BUF_t *pib;
	LPFCHBA_t *plhba;
	LPFC_NODE_FARP_PEND_t *pndlpfarp;
	NAME_TYPE rnode_addr;
	uint16_t loop_cnt;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	ndlp = NULL;
	memcpy((void *)&rnode_addr, l1, sizeof (NAME_TYPE));

	ndlp = lpfc_findnode_wwpn(phba, NLP_SEARCH_UNMAPPED, &rnode_addr);
	if (ndlp == NULL) {
		/* Look for an NDLP matching this remote node name.  Also look for a farp entry pending
		 *  on the same remote node name.  Update all lists with the results.
		 */
		pndlpfarp =
		    elx_tqs_dequeuefirst(&plhba->fc_node_farp_list, pnext);
		loop_cnt = 0;
		while (pndlpfarp != NULL) {
			if (lpfc_geportname(&pndlpfarp->rnode_addr, &rnode_addr)
			    != 2) {
				elx_tqs_enqueue(&plhba->fc_node_farp_list,
						pndlpfarp, pnext);
				pndlpfarp =
				    elx_tqs_dequeuefirst(&plhba->
							 fc_node_farp_list,
							 pnext);
			} else
				break;

			loop_cnt += 1;
			if (loop_cnt > plhba->fc_node_farp_list.q_cnt)
				pndlpfarp = NULL;
		}

		if (pndlpfarp) {
			pib =
			    elx_tqs_dequeuefirst(&pndlpfarp->
						 fc_ipbuf_list_farp_wait,
						 ip_buf_next);
			while (pib != NULL) {
				lpfc_free_ip_buf(pib);
				pib =
				    elx_tqs_dequeuefirst(&pndlpfarp->
							 fc_ipbuf_list_farp_wait,
							 ip_buf_next);
			}
		}
	}

	return;
}

void
lpfc_ip_xri_timeout(elxHBA_t * phba, void *l1, void *l2)
{
	LPFC_NODELIST_t *ndlp;
	LPFC_IP_BUF_t *pib;
	ELX_IOCBQ_t *iocb;
	ELX_SLI_t *psli;

	iocb = (ELX_IOCBQ_t *) l1;
	ndlp = (LPFC_NODELIST_t *) iocb->context1;
	psli = &phba->sli;

	/* Just examine the node's xri value.  If it is nonzero, the exchange has
	 * been created.  Just exit.  If not, flush the els command and flush the
	 * pending ip buffers.
	 */
	if (ndlp->nlp_xri == 0) {
		/* First, free the pib contained in the iocb. */
		pib = (LPFC_IP_BUF_t *) iocb->context2;
		lpfc_free_ip_buf(pib);

		/* Flush the iocb from SLI. */
		(void)lpfc_ip_flush_iocb(phba, &phba->sli.ring[psli->ip_ring],
					 ndlp, FLUSH_NODE);

		/* Now free all remaining IP buffers in the node's ipbuf list. */
		pib = elx_tqs_dequeuefirst(&ndlp->nlp_listp_ipbuf, ip_buf_next);
		while (pib != NULL) {
			lpfc_free_ip_buf(pib);
			pib =
			    elx_tqs_dequeuefirst(&ndlp->nlp_listp_ipbuf,
						 ip_buf_next);
		}
	}

	return;
}

void
lpfc_ip_timeout_handler(elxHBA_t * phba, void *arg1, void *arg2)
{
	ELX_SLI_t *psli;
	ELX_SLI_RING_t *pring;
	ELX_IOCBQ_t *next_iocb;
	ELX_IOCBQ_t *piocb;
	IOCB_t *cmd = NULL;
	LPFCHBA_t *plhba;
	uint32_t timeout;
	uint32_t next_timeout;
	LPFC_NODELIST_t *ndlp;
	uint32_t nlp_DID;

	psli = &phba->sli;
	pring = &psli->ring[psli->ip_ring];
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	timeout = (uint32_t) (unsigned long)arg1;
	next_timeout = timeout;

	next_iocb = (ELX_IOCBQ_t *) pring->txcmplq.q_f;
	while (next_iocb != (ELX_IOCBQ_t *) & pring->txcmplq) {
		piocb = next_iocb;
		next_iocb = next_iocb->q_f;
		cmd = &piocb->iocb;

		if (piocb->drvrTimeout) {
			if (piocb->drvrTimeout > timeout)
				piocb->drvrTimeout -= timeout;
			else
				piocb->drvrTimeout = 0;

			continue;
		}
		/*
		 * The iocb has timed out; abort it.
		 */
		ndlp = (LPFC_NODELIST_t *) piocb->context1;
		if (ndlp)
			nlp_DID = ndlp->nlp_DID;
		else
			nlp_DID = 0;

		/* IP packet timed out */
		elx_printf_log(phba->brd_no, &elx_msgBlk0608,	/* ptr to msg struct */
			       elx_mes0608,	/* ptr to msg */
			       elx_msgBlk0608.msgPreambleStr,	/* begin varargs */
			       nlp_DID);	/* end varargs */

		/*
		 * If abort times out, simple throw away the iocb
		 */

		if (cmd->un.acxri.abortType == ABORT_TYPE_ABTS) {
			elx_deque(piocb);
			pring->txcmplq.q_cnt--;
			(piocb->iocb_cmpl) ((void *)phba, piocb, piocb);
		} else
			elx_sli_abort_iocb(phba, pring, piocb);
	}

	phba->ip_tmofunc =
	    elx_clk_set(phba, next_timeout, lpfc_ip_timeout_handler,
			(void *)(unsigned long)next_timeout, 0);
}

/**********************************************/

/*                mailbox command             */
/**********************************************/
void
lpfc_dump_mem(elxHBA_t * phba, ELX_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	/* Setup to dump VPD region */
	memset((void *)pmb, 0, sizeof (ELX_MBOXQ_t));
	mb->mbxCommand = MBX_DUMP_MEMORY;
	mb->un.varDmp.cv = 1;
	mb->un.varDmp.type = DMP_NV_PARAMS;
	mb->un.varDmp.region_id = DMP_REGION_VPD;
	mb->un.varDmp.word_cnt = (DMP_VPD_SIZE / sizeof (uint32_t));

	mb->un.varDmp.co = 0;
	mb->un.varDmp.resp_offset = 0;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**********************************************/
/*  lpfc_read_nv  Issue a READ NVPARAM        */
/*                mailbox command             */
/**********************************************/
void
lpfc_read_nv(elxHBA_t * phba, ELX_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset((void *)pmb, 0, sizeof (ELX_MBOXQ_t));
	mb->mbxCommand = MBX_READ_NV;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**********************************************/
/*  lpfc_read_la  Issue a READ LA             */
/*                mailbox command             */
/**********************************************/
int
lpfc_read_la(elxHBA_t * phba, ELX_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;
	DMABUF_t *mp;
	ELX_SLI_t *psli;

	psli = &phba->sli;
	mb = &pmb->mb;
	memset((void *)pmb, 0, sizeof (ELX_MBOXQ_t));

	/* Get a buffer to hold the loop map */
	if ((mp = (DMABUF_t *) elx_mem_get(phba, MEM_BUF)) == 0) {
		mb->mbxCommand = MBX_READ_LA64;
		/* READ_LA: no buffers */
		elx_printf_log(phba->brd_no, &elx_msgBlk0300,	/* ptr to msg structure */
			       elx_mes0300,	/* ptr to msg */
			       elx_msgBlk0300.msgPreambleStr);	/* begin & end varargs */
		return (1);
	}

	mb->mbxCommand = MBX_READ_LA64;
	mb->un.varReadLA.un.lilpBde64.tus.f.bdeSize = 128;
	mb->un.varReadLA.un.lilpBde64.addrHigh = putPaddrHigh(mp->phys);
	mb->un.varReadLA.un.lilpBde64.addrLow = putPaddrLow(mp->phys);

	/* Sync the mailbox data with its PCI memory address now. */
	elx_pci_dma_sync((void *)phba, (void *)mp, 0, ELX_DMA_SYNC_FORDEV);

	/* Save address for later completion and set the owner to host so that
	 * the FW knows this mailbox is available for processing. 
	 */
	pmb->context1 = (uint8_t *) mp;
	mb->mbxOwner = OWN_HOST;
	return (0);
}

/**********************************************/
/*  lpfc_clear_la  Issue a CLEAR LA           */
/*                 mailbox command            */
/**********************************************/
void
lpfc_clear_la(elxHBA_t * phba, ELX_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	mb = &pmb->mb;
	memset((void *)pmb, 0, sizeof (ELX_MBOXQ_t));

	mb->un.varClearLA.eventTag = plhba->fc_eventTag;
	mb->mbxCommand = MBX_CLEAR_LA;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**************************************************/
/*  lpfc_config_link  Issue a CONFIG LINK         */
/*                    mailbox command             */
/**************************************************/
void
lpfc_config_link(elxHBA_t * phba, ELX_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;
	LPFCHBA_t *plhba;
	elxCfgParam_t *clp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	clp = &phba->config[0];
	mb = &pmb->mb;
	memset((void *)pmb, 0, sizeof (ELX_MBOXQ_t));

	/* NEW_FEATURE
	 * SLI-2, Coalescing Response Feature. 
	 */
	if (clp[LPFC_CFG_CR_DELAY].a_current) {
		mb->un.varCfgLnk.cr = 1;
		mb->un.varCfgLnk.ci = 1;
		mb->un.varCfgLnk.cr_delay = clp[LPFC_CFG_CR_DELAY].a_current;
		mb->un.varCfgLnk.cr_count = clp[LPFC_CFG_CR_COUNT].a_current;
	}

	mb->un.varCfgLnk.myId = plhba->fc_myDID;
	mb->un.varCfgLnk.edtov = plhba->fc_edtov;
	mb->un.varCfgLnk.arbtov = plhba->fc_arbtov;
	mb->un.varCfgLnk.ratov = plhba->fc_ratov;
	mb->un.varCfgLnk.rttov = plhba->fc_rttov;
	mb->un.varCfgLnk.altov = plhba->fc_altov;
	mb->un.varCfgLnk.crtov = plhba->fc_crtov;
	mb->un.varCfgLnk.citov = plhba->fc_citov;

	if (clp[LPFC_CFG_ACK0].a_current)
		mb->un.varCfgLnk.ack0_enable = 1;

	mb->mbxCommand = MBX_CONFIG_LINK;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**********************************************/
/*  lpfc_init_link  Issue an INIT LINK        */
/*                  mailbox command           */
/**********************************************/
void
lpfc_init_link(elxHBA_t * phba,
	       ELX_MBOXQ_t * pmb, uint32_t topology, uint32_t linkspeed)
{
	elx_vpd_t *vpd;
	ELX_SLI_t *psli;
	MAILBOX_t *mb;
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	mb = &pmb->mb;
	memset((void *)pmb, 0, sizeof (ELX_MBOXQ_t));

	psli = &phba->sli;
	switch (topology) {
	case FLAGS_TOPOLOGY_MODE_LOOP_PT:
		mb->un.varInitLnk.link_flags = FLAGS_TOPOLOGY_MODE_LOOP;
		mb->un.varInitLnk.link_flags |= FLAGS_TOPOLOGY_FAILOVER;
		break;
	case FLAGS_TOPOLOGY_MODE_PT_PT:
		mb->un.varInitLnk.link_flags = FLAGS_TOPOLOGY_MODE_PT_PT;
		break;
	case FLAGS_TOPOLOGY_MODE_LOOP:
		mb->un.varInitLnk.link_flags = FLAGS_TOPOLOGY_MODE_LOOP;
		break;
	case FLAGS_TOPOLOGY_MODE_PT_LOOP:
		mb->un.varInitLnk.link_flags = FLAGS_TOPOLOGY_MODE_PT_PT;
		mb->un.varInitLnk.link_flags |= FLAGS_TOPOLOGY_FAILOVER;
		break;
	}

	/* NEW_FEATURE
	 * Setting up the link speed
	 */
	vpd = &phba->vpd;
	if (plhba->fc_flag & FC_2G_CAPABLE) {
		if ((vpd->rev.feaLevelHigh >= 0x02) && (linkspeed > 0)) {
			mb->un.varInitLnk.link_flags |= FLAGS_LINK_SPEED;
			mb->un.varInitLnk.link_speed = linkspeed;
		}
	}

	mb->mbxCommand = (volatile uint8_t)MBX_INIT_LINK;
	mb->mbxOwner = OWN_HOST;
	mb->un.varInitLnk.fabric_AL_PA = plhba->fc_pref_ALPA;
	return;
}

/**********************************************/
/*  lpfc_read_sparam  Issue a READ SPARAM     */
/*                    mailbox command         */
/**********************************************/
int
lpfc_read_sparam(elxHBA_t * phba, ELX_MBOXQ_t * pmb)
{
	DMABUF_t *mp;
	MAILBOX_t *mb;
	ELX_SLI_t *psli;

	psli = &phba->sli;
	mb = &pmb->mb;
	memset((void *)pmb, 0, sizeof (ELX_MBOXQ_t));

	mb->mbxOwner = OWN_HOST;

	/* Get a buffer to hold the HBAs Service Parameters */
	if ((mp = (DMABUF_t *) elx_mem_get(phba, MEM_BUF)) == 0) {
		mb->mbxCommand = MBX_READ_SPARM64;
		/* READ_SPARAM: no buffers */
		elx_printf_log(phba->brd_no, &elx_msgBlk0301,	/* ptr to msg structure */
			       elx_mes0301,	/* ptr to msg */
			       elx_msgBlk0301.msgPreambleStr);	/* begin & end varargs */
		return (1);
	}

	mb->mbxCommand = MBX_READ_SPARM64;
	mb->un.varRdSparm.un.sp64.tus.f.bdeSize = sizeof (SERV_PARM);
	mb->un.varRdSparm.un.sp64.addrHigh = putPaddrHigh(mp->phys);
	mb->un.varRdSparm.un.sp64.addrLow = putPaddrLow(mp->phys);

	elx_pci_dma_sync((void *)phba, (void *)mp, 0, ELX_DMA_SYNC_FORDEV);

	/* save address for completion */
	pmb->context1 = (void *)mp;

	return (0);
}

/**********************************************/
/*  lpfc_read_rpi    Issue a READ RPI         */
/*                   mailbox command          */
/**********************************************/
int
lpfc_read_rpi(elxHBA_t * phba, uint32_t rpi, ELX_MBOXQ_t * pmb, uint32_t flag)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset((void *)pmb, 0, sizeof (ELX_MBOXQ_t));

	mb->un.varRdRPI.reqRpi = (volatile uint16_t)rpi;

	mb->mbxCommand = MBX_READ_RPI64;
	mb->mbxOwner = OWN_HOST;

	mb->un.varWords[30] = flag;	/* Set flag to issue action on cmpl */

	return (0);
}

/********************************************/
/*  lpfc_unreg_did  Issue a UNREG_DID       */
/*                  mailbox command         */
/********************************************/
void
lpfc_unreg_did(elxHBA_t * phba, uint32_t did, ELX_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset((void *)pmb, 0, sizeof (ELX_MBOXQ_t));

	mb->un.varUnregDID.did = did;

	mb->mbxCommand = MBX_UNREG_D_ID;
	mb->mbxOwner = OWN_HOST;
	return;
}

/***********************************************/

/*                  command to write slim      */
/***********************************************/
void
lpfc_set_slim(elxHBA_t * phba, ELX_MBOXQ_t * pmb, uint32_t addr, uint32_t value)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset((void *)pmb, 0, sizeof (ELX_MBOXQ_t));

	/* addr = 0x090597 is AUTO ABTS disable for ELS commands */
	/* addr = 0x052198 is DELAYED ABTS enable for ELS commands */

	/*
	 * Always turn on DELAYED ABTS for ELS timeouts 
	 */
	if ((addr == 0x052198) && (value == 0))
		value = 1;

	mb->un.varWords[0] = addr;
	mb->un.varWords[1] = value;

	mb->mbxCommand = MBX_SET_SLIM;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**********************************************/
/*  lpfc_config_farp  Issue a CONFIG FARP     */
/*                    mailbox command         */
/**********************************************/
void
lpfc_config_farp(elxHBA_t * phba, ELX_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	mb = &pmb->mb;
	memset((void *)pmb, 0, sizeof (ELX_MBOXQ_t));

	mb->un.varCfgFarp.filterEnable = 1;
	mb->un.varCfgFarp.portName = 1;
	mb->un.varCfgFarp.nodeName = 1;

	memcpy((uint8_t *) & mb->un.varCfgFarp.portname,
	       (uint8_t *) & plhba->fc_portname, sizeof (NAME_TYPE));
	memcpy((uint8_t *) & mb->un.varCfgFarp.nodename,
	       (uint8_t *) & plhba->fc_portname, sizeof (NAME_TYPE));
	mb->mbxCommand = MBX_CONFIG_FARP;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**********************************************/
/*  lpfc_read_nv  Issue a READ CONFIG         */
/*                mailbox command             */
/**********************************************/
void
lpfc_read_config(elxHBA_t * phba, ELX_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset((void *)pmb, 0, sizeof (ELX_MBOXQ_t));

	mb->mbxCommand = MBX_READ_CONFIG;
	mb->mbxOwner = OWN_HOST;
	return;
}

/********************************************/
/*  lpfc_reg_login  Issue a REG_LOGIN       */
/*                  mailbox command         */
/********************************************/
int
lpfc_reg_login(elxHBA_t * phba,
	       uint32_t did, uint8_t * param, ELX_MBOXQ_t * pmb, uint32_t flag)
{
	uint8_t *sparam;
	DMABUF_t *mp;
	MAILBOX_t *mb;
	ELX_SLI_t *psli;

	psli = &phba->sli;
	mb = &pmb->mb;
	memset((void *)pmb, 0, sizeof (ELX_MBOXQ_t));

	mb->un.varRegLogin.rpi = 0;
	mb->un.varRegLogin.did = did;
	mb->un.varWords[30] = flag;	/* Set flag to issue action on cmpl */

	mb->mbxOwner = OWN_HOST;

	/* Get a buffer to hold NPorts Service Parameters */
	if ((mp = (DMABUF_t *) elx_mem_get(phba, MEM_BUF)) == 0) {

		mb->mbxCommand = MBX_REG_LOGIN64;
		/* REG_LOGIN: no buffers */
		elx_printf_log(phba->brd_no, &elx_msgBlk0302,	/* ptr to msg structure */
			       elx_mes0302,	/* ptr to msg */
			       elx_msgBlk0302.msgPreambleStr,	/* begin varargs */
			       (uint32_t) did, (uint32_t) flag);	/* end varargs */
		return (1);
	}

	sparam = mp->virt;

	/* Copy param's into a new buffer */
	memcpy((void *)sparam, (void *)param, sizeof (SERV_PARM));

	elx_pci_dma_sync((void *)phba, (void *)mp, 0, ELX_DMA_SYNC_FORDEV);

	/* save address for completion */
	pmb->context1 = (uint8_t *) mp;

	mb->mbxCommand = MBX_REG_LOGIN64;
	mb->un.varRegLogin.un.sp64.tus.f.bdeSize = sizeof (SERV_PARM);
	mb->un.varRegLogin.un.sp64.addrHigh = putPaddrHigh(mp->phys);
	mb->un.varRegLogin.un.sp64.addrLow = putPaddrLow(mp->phys);

	return (0);
}

/**********************************************/
/*  lpfc_unreg_login  Issue a UNREG_LOGIN     */
/*                    mailbox command         */
/**********************************************/
void
lpfc_unreg_login(elxHBA_t * phba, uint32_t rpi, ELX_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset((void *)pmb, 0, sizeof (ELX_MBOXQ_t));

	mb->un.varUnregLogin.rpi = (uint16_t) rpi;
	mb->un.varUnregLogin.rsvd1 = 0;

	mb->mbxCommand = MBX_UNREG_LOGIN;
	mb->mbxOwner = OWN_HOST;
	return;
}

/***********************************************/
/*  lpfc_config_pcb_setup  Issue a CONFIG_PORT */
/*                   mailbox command           */
/***********************************************/
uint32_t *
lpfc_config_pcb_setup(elxHBA_t * phba)
{
	ELX_SLI_t *psli;
	ELX_SLI_RING_t *pring;
	ELX_RING_INIT_t *pringinit;
	PCB_t *pcbp;
	SLI2_SLIM_t *slim2p_virt;
	elx_dma_addr_t pdma_addr;
	uint32_t offset;
	uint32_t iocbCnt;
	int i;
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;

	slim2p_virt = ((SLI2_SLIM_t *) phba->slim2p.virt);
	pcbp = &slim2p_virt->un.slim.pcb;
	psli->MBhostaddr = (uint32_t *) (&slim2p_virt->un.slim.mbx);

	pcbp->maxRing = (psli->sliinit.num_rings - 1);

	iocbCnt = 0;
	for (i = 0; i < psli->sliinit.num_rings; i++) {
		pringinit = &psli->sliinit.ringinit[i];
		pring = &psli->ring[i];
		/* A ring MUST have both cmd and rsp entries defined to be valid */
		if ((pringinit->numCiocb == 0) || (pringinit->numRiocb == 0)) {
			pcbp->rdsc[i].cmdEntries = 0;
			pcbp->rdsc[i].rspEntries = 0;
			pcbp->rdsc[i].cmdAddrHigh = 0;
			pcbp->rdsc[i].rspAddrHigh = 0;
			pcbp->rdsc[i].cmdAddrLow = 0;
			pcbp->rdsc[i].rspAddrLow = 0;
			pring->cmdringaddr = (void *)0;
			pring->rspringaddr = (void *)0;
			continue;
		}
		/* Command ring setup for ring */
		pring->cmdringaddr =
		    (void *)&slim2p_virt->un.slim.IOCBs[iocbCnt];
		pcbp->rdsc[i].cmdEntries = pringinit->numCiocb;

		offset =
		    (uint8_t *) & slim2p_virt->un.slim.IOCBs[iocbCnt] -
		    (uint8_t *) slim2p_virt;
		pdma_addr = phba->slim2p.phys + offset;
		pcbp->rdsc[i].cmdAddrHigh = putPaddrHigh(pdma_addr);
		pcbp->rdsc[i].cmdAddrLow = putPaddrLow(pdma_addr);
		iocbCnt += pringinit->numCiocb;

		/* Response ring setup for ring */
		pring->rspringaddr =
		    (void *)&slim2p_virt->un.slim.IOCBs[iocbCnt];

		pcbp->rdsc[i].rspEntries = pringinit->numRiocb;
		offset =
		    (uint8_t *) & slim2p_virt->un.slim.IOCBs[iocbCnt] -
		    (uint8_t *) slim2p_virt;
		pdma_addr = phba->slim2p.phys + offset;
		pcbp->rdsc[i].rspAddrHigh = putPaddrHigh(pdma_addr);
		pcbp->rdsc[i].rspAddrLow = putPaddrLow(pdma_addr);
		iocbCnt += pringinit->numRiocb;
	}

	elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
			 ELX_SLIM2_PAGE_AREA, ELX_DMA_SYNC_FORDEV);

#ifndef powerpc
	if ((((phba->pci_id >> 16) & 0xffff) == PCI_DEVICE_ID_TFLY) ||
	    (((phba->pci_id >> 16) & 0xffff) == PCI_DEVICE_ID_PFLY) ||
	    (((phba->pci_id >> 16) & 0xffff) == PCI_DEVICE_ID_LP101) ||
	    (((phba->pci_id >> 16) & 0xffff) == PCI_DEVICE_ID_RFLY)) {
#else
	if (((__swab16(phba->pci_id) & 0xffff) == PCI_DEVICE_ID_TFLY) ||
	    ((__swab16(phba->pci_id) & 0xffff) == PCI_DEVICE_ID_PFLY) ||
	    ((__swab16(phba->pci_id) & 0xffff) == PCI_DEVICE_ID_RFLY)) {
#endif

		lpfc_hba_init(phba);
		return (plhba->hbainitEx);
	} else
		return (NULL);
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
/* For UNUSED_NODE state, the node has just been allocated from the MEM_NLP
 * driver memory pool. For PLOGI_ISSUE and REG_LOGIN_ISSUE, the node is on
 * the PLOGI list. For REG_LOGIN_COMPL, the node is taken off the PLOGI list
 * and put on the unmapped list. For ADISC processing, the node is taken off 
 * the ADISC list and placed on either the mapped or unmapped list (depending
 * on its previous state). Once on the unmapped list, a PRLI is issued and the
 * state changed to PRLI_ISSUE. When the PRLI completion occurs, the state is
 * changed to PRLI_COMPL. If the completion indicates a mapped
 * node, the node is taken off the unmapped list. The binding list is checked
 * for a valid binding, or a binding is automatically assigned. If binding
 * assignment is unsuccessful, the node is left on the unmapped list. If
 * binding assignment is successful, the associated binding list entry (if
 * any) is removed, and the node is placed on the mapped list. 
 */
/*
 * For a Link Down, all nodes on the ADISC, PLOGI, unmapped or mapped
 * lists will receive a DEVICE_UNK event. If the linkdown or nodev timers
 * expire, all effected nodes will receive a DEVICE_RM event.
 */
/*
 * For a Link Up or RSCN, all nodes will move from the mapped / unmapped 
 * lists to either the ADISC or PLOGI list.  After a Nameserver
 * query or ALPA loopmap check, additional nodes may be added (DEVICE_ADD)
 * or removed (DEVICE_RM) to / from the PLOGI or ADISC lists. Once the PLOGI
 * and ADISC lists are populated, we will first process the ADISC list.
 * 32 entries are processed initially and ADISC is initited for each one.
 * Completions / Events for each node are funnelled thru the state machine.
 * As each node finishes ADISC processing, it starts ADISC for any nodes
 * waiting for ADISC processing. If no nodes are waiting, and the ADISC
 * list count is identically 0, then we are done. For Link Up discovery, since all nodes
 * on the PLOGI list are UNREG_LOGIN'ed, we can issue a CLEAR_LA and reenable
 * Link Events. Next we will process the PLOGI list.
 * 32 entries are processed initially and PLOGI is initited for each one.
 * Completions / Events for each node are funnelled thru the state machine.
 * As each node finishes PLOGI processing, it starts PLOGI for any nodes
 * waiting for PLOGI processing. If no nodes are waiting, and the PLOGI
 * list count is indentically 0, then we are done. We have now completed discovery /
 * RSCN handling. Upon completion, ALL nodes should be on either the mapped
 * or unmapped lists.
 */

void *lpfc_disc_action[NLP_STE_MAX_STATE * NLP_EVT_MAX_EVENT] = {
	/* Action routine                            Event       Current State   */
	(void *)lpfc_rcv_plogi_unused_node,	/* RCV_PLOGI   UNUSED_NODE     */
	(void *)lpfc_rcv_els_unused_node,	/* RCV_PRLI        */
	(void *)lpfc_rcv_logo_unused_node,	/* RCV_LOGO        */
	(void *)lpfc_rcv_els_unused_node,	/* RCV_ADISC       */
	(void *)lpfc_rcv_els_unused_node,	/* RCV_PDISC       */
	(void *)lpfc_rcv_els_unused_node,	/* RCV_PRLO        */
	(void *)lpfc_cmpl_els_unused_node,	/* CMPL_PLOGI      */
	(void *)lpfc_cmpl_els_unused_node,	/* CMPL_PRLI       */
	(void *)lpfc_cmpl_els_unused_node,	/* CMPL_LOGO       */
	(void *)lpfc_cmpl_els_unused_node,	/* CMPL_ADISC      */
	(void *)lpfc_cmpl_reglogin_unused_node,	/* CMPL_REG_LOGIN  */
	(void *)lpfc_device_rm_unused_node,	/* DEVICE_RM       */
	(void *)lpfc_device_add_unused_node,	/* DEVICE_ADD      */
	(void *)lpfc_device_unk_unused_node,	/* DEVICE_UNK      */
	(void *)lpfc_rcv_plogi_plogi_issue,	/* RCV_PLOGI   PLOGI_ISSUE     */
	(void *)lpfc_rcv_prli_plogi_issue,	/* RCV_PRLI        */
	(void *)lpfc_rcv_logo_plogi_issue,	/* RCV_LOGO        */
	(void *)lpfc_rcv_els_plogi_issue,	/* RCV_ADISC       */
	(void *)lpfc_rcv_els_plogi_issue,	/* RCV_PDISC       */
	(void *)lpfc_rcv_els_plogi_issue,	/* RCV_PRLO        */
	(void *)lpfc_cmpl_plogi_plogi_issue,	/* CMPL_PLOGI      */
	(void *)lpfc_cmpl_prli_plogi_issue,	/* CMPL_PRLI       */
	(void *)lpfc_cmpl_logo_plogi_issue,	/* CMPL_LOGO       */
	(void *)lpfc_cmpl_adisc_plogi_issue,	/* CMPL_ADISC      */
	(void *)lpfc_cmpl_reglogin_plogi_issue,	/* CMPL_REG_LOGIN  */
	(void *)lpfc_device_rm_plogi_issue,	/* DEVICE_RM       */
	(void *)lpfc_disc_nodev,	/* DEVICE_ADD      */
	(void *)lpfc_device_unk_plogi_issue,	/* DEVICE_UNK      */
	(void *)lpfc_rcv_plogi_reglogin_issue,	/* RCV_PLOGI   REG_LOGIN_ISSUE */
	(void *)lpfc_rcv_prli_reglogin_issue,	/* RCV_PLOGI       */
	(void *)lpfc_rcv_logo_reglogin_issue,	/* RCV_LOGO        */
	(void *)lpfc_rcv_padisc_reglogin_issue,	/* RCV_ADISC       */
	(void *)lpfc_rcv_padisc_reglogin_issue,	/* RCV_PDISC       */
	(void *)lpfc_rcv_prlo_reglogin_issue,	/* RCV_PRLO        */
	(void *)lpfc_disc_neverdev,	/* CMPL_PLOGI      */
	(void *)lpfc_disc_neverdev,	/* CMPL_PRLI       */
	(void *)lpfc_cmpl_logo_reglogin_issue,	/* CMPL_LOGO       */
	(void *)lpfc_cmpl_adisc_reglogin_issue,	/* CMPL_ADISC      */
	(void *)lpfc_cmpl_reglogin_reglogin_issue,	/* CMPL_REG_LOGIN  */
	(void *)lpfc_device_rm_reglogin_issue,	/* DEVICE_RM       */
	(void *)lpfc_disc_nodev,	/* DEVICE_ADD      */
	(void *)lpfc_device_unk_reglogin_issue,	/* DEVICE_UNK      */
	(void *)lpfc_rcv_plogi_prli_issue,	/* RCV_PLOGI   PRLI_ISSUE      */
	(void *)lpfc_rcv_prli_prli_issue,	/* RCV_PRLI        */
	(void *)lpfc_rcv_logo_prli_issue,	/* RCV_LOGO        */
	(void *)lpfc_rcv_padisc_prli_issue,	/* RCV_ADISC       */
	(void *)lpfc_rcv_padisc_prli_issue,	/* RCV_PDISC       */
	(void *)lpfc_rcv_prlo_prli_issue,	/* RCV_PRLO        */
	(void *)lpfc_disc_neverdev,	/* CMPL_PLOGI      */
	(void *)lpfc_cmpl_prli_prli_issue,	/* CMPL_PRLI       */
	(void *)lpfc_cmpl_logo_prli_issue,	/* CMPL_LOGO       */
	(void *)lpfc_cmpl_adisc_prli_issue,	/* CMPL_ADISC      */
	(void *)lpfc_cmpl_reglogin_prli_issue,	/* CMPL_REG_LOGIN  */
	(void *)lpfc_device_rm_prli_issue,	/* DEVICE_RM       */
	(void *)lpfc_device_add_prli_issue,	/* DEVICE_ADD      */
	(void *)lpfc_device_unk_prli_issue,	/* DEVICE_UNK      */
	(void *)lpfc_rcv_plogi_prli_compl,	/* RCV_PLOGI   PRLI_COMPL      */
	(void *)lpfc_rcv_prli_prli_compl,	/* RCV_PRLI        */
	(void *)lpfc_rcv_logo_prli_compl,	/* RCV_LOGO        */
	(void *)lpfc_rcv_padisc_prli_compl,	/* RCV_ADISC       */
	(void *)lpfc_rcv_padisc_prli_compl,	/* RCV_PDISC       */
	(void *)lpfc_rcv_prlo_prli_compl,	/* RCV_PRLO        */
	(void *)lpfc_disc_neverdev,	/* CMPL_PLOGI      */
	(void *)lpfc_disc_neverdev,	/* CMPL_PRLI       */
	(void *)lpfc_cmpl_logo_prli_compl,	/* CMPL_LOGO       */
	(void *)lpfc_cmpl_adisc_prli_compl,	/* CMPL_ADISC      */
	(void *)lpfc_cmpl_reglogin_prli_compl,	/* CMPL_REG_LOGIN  */
	(void *)lpfc_device_rm_prli_compl,	/* DEVICE_RM       */
	(void *)lpfc_device_add_prli_compl,	/* DEVICE_ADD      */
	(void *)lpfc_device_unk_prli_compl,	/* DEVICE_UNK      */
	(void *)lpfc_rcv_plogi_mapped_node,	/* RCV_PLOGI   MAPPED_NODE     */
	(void *)lpfc_rcv_prli_mapped_node,	/* RCV_PRLI        */
	(void *)lpfc_rcv_logo_mapped_node,	/* RCV_LOGO        */
	(void *)lpfc_rcv_padisc_mapped_node,	/* RCV_ADISC       */
	(void *)lpfc_rcv_padisc_mapped_node,	/* RCV_PDISC       */
	(void *)lpfc_rcv_prlo_mapped_node,	/* RCV_PRLO        */
	(void *)lpfc_disc_neverdev,	/* CMPL_PLOGI      */
	(void *)lpfc_disc_neverdev,	/* CMPL_PRLI       */
	(void *)lpfc_cmpl_logo_mapped_node,	/* CMPL_LOGO       */
	(void *)lpfc_cmpl_adisc_mapped_node,	/* CMPL_ADISC      */
	(void *)lpfc_cmpl_reglogin_mapped_node,	/* CMPL_REG_LOGIN  */
	(void *)lpfc_device_rm_mapped_node,	/* DEVICE_RM       */
	(void *)lpfc_device_add_mapped_node,	/* DEVICE_ADD      */
	(void *)lpfc_device_unk_mapped_node,	/* DEVICE_UNK      */
};

int lpfc_check_adisc(elxHBA_t *, LPFC_NODELIST_t *, NAME_TYPE *, NAME_TYPE *);
int lpfc_geportname(NAME_TYPE *, NAME_TYPE *);
LPFC_BINDLIST_t *lpfc_assign_scsid(elxHBA_t *, LPFC_NODELIST_t *);

int
lpfc_disc_state_machine(elxHBA_t * phba,
			LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	uint32_t cur_state, rc;
	uint32_t(*func) (elxHBA_t *, LPFC_NODELIST_t *, void *, uint32_t);

	cur_state = ndlp->nlp_state;

	/* DSM in event <evt> on NPort <nlp_DID> in state <cur_state> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0211,	/* ptr to msg structure */
		       elx_mes0211,	/* ptr to msg */
		       elx_msgBlk0211.msgPreambleStr,	/* begin varargs */
		       evt, ndlp->nlp_DID, cur_state, ndlp->nlp_flag);	/* end varargs */

	func = (uint32_t(*)(elxHBA_t *, LPFC_NODELIST_t *, void *, uint32_t))
	    lpfc_disc_action[(cur_state * NLP_EVT_MAX_EVENT) + evt];
	rc = (func) (phba, ndlp, arg, evt);

	/* DSM out state <rc> on NPort <nlp_DID> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0212,	/* ptr to msg structure */
		       elx_mes0212,	/* ptr to msg */
		       elx_msgBlk0212.msgPreambleStr,	/* begin varargs */
		       rc, ndlp->nlp_DID, ndlp->nlp_flag);	/* end varargs */

	if (rc == NLP_STE_FREED_NODE)
		return (NLP_STE_FREED_NODE);
	ndlp->nlp_state = rc;
	return (rc);
}

uint32_t
lpfc_rcv_plogi_unused_node(elxHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFCHBA_t *plhba;
	ELX_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	uint32_t *lp;
	IOCB_t *icmd;
	SERV_PARM *sp;
	ELX_MBOXQ_t *mbox;
	elxCfgParam_t *clp;
	LS_RJT stat;
	LPFC_NODE_FARP_PEND_t *pndlpfarp;
	FARP *pfarp;
	uint16_t loop_cnt;

	plhba = phba->pHbaProto;
	clp = &phba->config[0];
	cmdiocb = (ELX_IOCBQ_t *) arg;
	icmd = &cmdiocb->iocb;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	sp = (SERV_PARM *) ((uint8_t *) lp + sizeof (uint32_t));

	if ((phba->hba_state <= ELX_FLOGI) ||
	    ((lpfc_check_sparm(phba, ndlp, sp, CLASS3) == 0))) {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		if (phba->hba_state <= ELX_FLOGI) {
			stat.un.b.lsRjtRsnCodeExp = LSRJT_LOGICAL_BSY;
		} else {
			stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		}
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	} else {
		/* PLOGI chkparm OK */
		elx_printf_log(phba->brd_no, &elx_msgBlk0114,	/* ptr to msg structure */
			       elx_mes0114,	/* ptr to msg */
			       elx_msgBlk0114.msgPreambleStr,	/* begin varargs */
			       ndlp->nlp_DID, ndlp->nlp_state, ndlp->nlp_flag, ndlp->nle.nlp_rpi);	/* end varargs */

		if ((clp[LPFC_CFG_FCP_CLASS].a_current == CLASS2) &&
		    (sp->cls2.classValid)) {
			ndlp->nle.nlp_fcp_info |= CLASS2;
		} else {
			ndlp->nle.nlp_fcp_info |= CLASS3;
		}

		if ((clp[LPFC_CFG_IP_CLASS].a_current == CLASS2) &&
		    (sp->cls2.classValid)) {
			ndlp->nle.nlp_ip_info = CLASS2;
		} else {
			ndlp->nle.nlp_ip_info = CLASS3;
		}

		if ((mbox =
		     (ELX_MBOXQ_t *) elx_mem_get(phba,
						 MEM_MBOX | MEM_PRI)) == 0) {
			goto out;
		}
		if ((plhba->fc_flag & FC_PT2PT)
		    && !(plhba->fc_flag & FC_PT2PT_PLOGI)) {
			/* The rcv'ed PLOGI determines what our NPortId will be */
			plhba->fc_myDID = icmd->un.rcvels.parmRo;
			lpfc_config_link(phba, mbox);
			if (elx_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				elx_mem_put(phba, MEM_MBOX, (uint8_t *) mbox);
				goto out;
			}
			if ((mbox =
			     (ELX_MBOXQ_t *) elx_mem_get(phba,
							 MEM_MBOX | MEM_PRI)) ==
			    0) {
				goto out;
			}
		}
		if (lpfc_reg_login(phba, icmd->un.rcvels.remoteID,
				   (uint8_t *) sp, mbox, 0) == 0) {
			/* set_slim mailbox command needs to execute first,
			 * queue this command to be processed later.
			 */
			pfarp = (FARP *) pCmd;
			if ((pfarp->Rflags == FARP_REQUEST_PLOGI)
			    && (pfarp->Mflags == FARP_MATCH_PORT)) {
				pndlpfarp =
				    elx_tqs_dequeuefirst(&plhba->
							 fc_node_farp_list,
							 pnext);
				loop_cnt = 0;
				while (pndlpfarp != NULL) {
					if ((lpfc_geportname
					     (&pndlpfarp->rnode_addr,
					      &ndlp->nlp_portname) != 2)
					    &&
					    (lpfc_geportname
					     (&pndlpfarp->rnode_addr,
					      &ndlp->nlp_nodename) != 2)) {
						elx_tqs_enqueue(&plhba->
								fc_node_farp_list,
								pndlpfarp,
								pnext);
						pndlpfarp =
						    elx_tqs_dequeuefirst
						    (&plhba->fc_node_farp_list,
						     pnext);
					} else
						break;

					loop_cnt += 1;
					if (loop_cnt >
					    plhba->fc_node_farp_list.q_cnt)
						pndlpfarp = NULL;
				}

				if (pndlpfarp->fc_ipfarp_tmo) {
					elx_clk_can(phba,
						    pndlpfarp->fc_ipfarp_tmo);
					pndlpfarp->fc_ipfarp_tmo = 0;
				}
			}

			mbox->mbox_cmpl = lpfc_mbx_cmpl_reg_login;
			mbox->context2 = (void *)ndlp;
			ndlp->nlp_state = NLP_STE_REG_LOGIN_ISSUE;
			if (elx_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
			    != MBX_NOT_FINISHED) {
				lpfc_nlp_plogi(phba, ndlp);
				lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb,
						 ndlp, 0);
				return (ndlp->nlp_state);	/* HAPPY PATH */
			}
			/* NOTE: we should have messages for unsuccessful reglogin */
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) mbox);
		} else {
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) mbox);
		}
	}
      out:
	lpfc_freenode(phba, ndlp);
	elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_rcv_els_unused_node(elxHBA_t * phba,
			 LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	lpfc_issue_els_logo(phba, ndlp, 0);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_logo_unused_node(elxHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0);
	lpfc_freenode(phba, ndlp);
	elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_cmpl_els_unused_node(elxHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	lpfc_freenode(phba, ndlp);
	elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_cmpl_reglogin_unused_node(elxHBA_t * phba,
			       LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* dequeue, cancel timeout, unreg login */
	lpfc_freenode(phba, ndlp);
	elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_device_rm_unused_node(elxHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* dequeue, cancel timeout, unreg login */
	lpfc_freenode(phba, ndlp);
	elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
	return (NLP_STE_UNUSED_NODE);
}

uint32_t
lpfc_device_add_unused_node(elxHBA_t * phba,
			    LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	if (ndlp->nlp_tmofunc) {
		ndlp->nlp_flag &= ~(NLP_NODEV_TMO | NLP_DELAY_TMO);
		ndlp->nle.nlp_rflag &= ~NLP_NPR_ACTIVE;
		elx_clk_can(phba, ndlp->nlp_tmofunc);
		ndlp->nlp_tmofunc = 0;
	}
	ndlp->nlp_state = NLP_STE_UNUSED_NODE;
	lpfc_nlp_plogi(phba, ndlp);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_device_unk_unused_node(elxHBA_t * phba,
			    LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* dequeue, cancel timeout, unreg login */
	lpfc_freenode(phba, ndlp);
	elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
	return (NLP_STE_UNUSED_NODE);
}

uint32_t
lpfc_rcv_plogi_plogi_issue(elxHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFCHBA_t *plhba;
	ELX_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	uint32_t *lp;
	IOCB_t *icmd;
	SERV_PARM *sp;
	ELX_MBOXQ_t *mbox;
	elxCfgParam_t *clp;
	LS_RJT stat;
	int port_cmp;

	plhba = phba->pHbaProto;
	clp = &phba->config[0];
	cmdiocb = (ELX_IOCBQ_t *) arg;
	icmd = &cmdiocb->iocb;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	sp = (SERV_PARM *) ((uint8_t *) lp + sizeof (uint32_t));

	/* For a PLOGI, we only accept if our portname is less
	 * than the remote portname. 
	 */
	plhba->fc_stat.elsLogiCol++;
	port_cmp = lpfc_geportname((NAME_TYPE *) & plhba->fc_portname,
				   (NAME_TYPE *) & sp->portName);

	if (!port_cmp) {
		if (lpfc_check_sparm(phba, ndlp, sp, CLASS3) == 0) {
			/* Reject this request because invalid parameters */
			stat.un.b.lsRjtRsvd0 = 0;
			stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
			stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
			stat.un.b.vendorUnique = 0;
			lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb,
					    ndlp);
		} else {
			/* PLOGI chkparm OK */
			elx_printf_log(phba->brd_no, &elx_msgBlk0120,	/* ptr to msg structure */
				       elx_mes0120,	/* ptr to msg */
				       elx_msgBlk0120.msgPreambleStr,	/* begin varargs */
				       ndlp->nlp_DID, ndlp->nlp_state, ndlp->nlp_flag, ((ELX_NODELIST_t *) ndlp)->nlp_rpi);	/* end varargs */

			if ((clp[LPFC_CFG_FCP_CLASS].a_current == CLASS2) &&
			    (sp->cls2.classValid)) {
				ndlp->nle.nlp_fcp_info |= CLASS2;
			} else {
				ndlp->nle.nlp_fcp_info |= CLASS3;
			}

			if ((clp[LPFC_CFG_IP_CLASS].a_current == CLASS2) &&
			    (sp->cls2.classValid)) {
				ndlp->nle.nlp_ip_info = CLASS2;
			} else {
				ndlp->nle.nlp_ip_info = CLASS3;
			}

			if ((mbox =
			     (ELX_MBOXQ_t *) elx_mem_get(phba,
							 MEM_MBOX | MEM_PRI))) {
				if (lpfc_reg_login
				    (phba, icmd->un.rcvels.remoteID,
				     (uint8_t *) sp, mbox, 0) == 0) {
					mbox->mbox_cmpl =
					    lpfc_mbx_cmpl_reg_login;
					mbox->context2 = (void *)ndlp;
					ndlp->nlp_flag |= NLP_ACC_REGLOGIN;	/* Issue Reg Login after successful ACC */

					if (port_cmp != 2) {
						/* Abort outstanding PLOGI */
						lpfc_driver_abort(phba, ndlp);
					}
					lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI,
							 cmdiocb, ndlp, mbox);
					return (ndlp->nlp_state);

				} else {
					elx_mem_put(phba, MEM_MBOX,
						    (uint8_t *) mbox);
				}
			}
		}		/* if valid sparm */
	} /* if our portname was less */
	else {
		/* Reject this request because the remote node will accept ours */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_CMD_IN_PROGRESS;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	}
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_prli_plogi_issue(elxHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* software abort outstanding plogi, then send logout */
	if (ndlp->nlp_tmofunc) {
		ndlp->nlp_flag &= ~(NLP_NODEV_TMO | NLP_DELAY_TMO);
		ndlp->nle.nlp_rflag &= ~NLP_NPR_ACTIVE;
		elx_clk_can(phba, ndlp->nlp_tmofunc);
		ndlp->nlp_tmofunc = 0;
	} else {
		lpfc_driver_abort(phba, ndlp);
	}
	lpfc_issue_els_logo(phba, ndlp, 0);

	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_logo_plogi_issue(elxHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	ELXSCSITARGET_t *targetp;
	elxCfgParam_t *clp;

	clp = &phba->config[0];
	cmdiocb = (ELX_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	/* software abort outstanding plogi before sending acc */
	if (ndlp->nlp_tmofunc) {
		ndlp->nlp_flag &= ~(NLP_NODEV_TMO | NLP_DELAY_TMO);
		ndlp->nle.nlp_rflag &= ~NLP_NPR_ACTIVE;
		elx_clk_can(phba, ndlp->nlp_tmofunc);
		ndlp->nlp_tmofunc = 0;
	} else {
		lpfc_driver_abort(phba, ndlp);
	}
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0);

	/* resend plogi after 1 sec delay */
	targetp = ndlp->nlp_Target;
	if (targetp) {
		targetp->targetFlags |= FC_NPR_ACTIVE;
		if (targetp->tmofunc) {
			elx_clk_can(phba, targetp->tmofunc);
		}
		if (clp[ELX_CFG_NODEV_TMO].a_current >
		    clp[ELX_CFG_LINKDOWN_TMO].a_current) {
			targetp->tmofunc =
			    elx_clk_set(phba, clp[ELX_CFG_NODEV_TMO].a_current,
					lpfc_npr_timeout, (void *)targetp,
					(void *)0);
		} else {
			targetp->tmofunc = elx_clk_set(phba,
						       clp
						       [ELX_CFG_LINKDOWN_TMO].
						       a_current,
						       lpfc_npr_timeout,
						       (void *)targetp,
						       (void *)0);
		}
	}
	ndlp->nle.nlp_rflag |= NLP_NPR_ACTIVE;
	ndlp->nlp_flag |= NLP_DELAY_TMO;
	ndlp->nlp_retry = 0;
	ndlp->nlp_tmofunc = elx_clk_set(phba, 1,
					lpfc_els_retry_delay,
					(void *)((unsigned long)ndlp->nlp_DID),
					(void *)((unsigned long)ELS_CMD_PLOGI));
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_els_plogi_issue(elxHBA_t * phba,
			 LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* software abort outstanding plogi, then send logout */
	if (ndlp->nlp_tmofunc) {
		ndlp->nlp_flag &= ~(NLP_NODEV_TMO | NLP_DELAY_TMO);
		ndlp->nle.nlp_rflag &= ~NLP_NPR_ACTIVE;
		elx_clk_can(phba, ndlp->nlp_tmofunc);
		ndlp->nlp_tmofunc = 0;
	} else {
		lpfc_driver_abort(phba, ndlp);
	}
	lpfc_issue_els_logo(phba, ndlp, 0);
	return (ndlp->nlp_state);
}

/*! lpfc_cmpl_plogi_plogi_issue
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
  *     This routine is envoked when we rcv a PLOGI completion from a node
  *     we tried to log into. We check the CSPs, and the ulpStatus. If successful
  *     change the state to REG_LOGIN_ISSUE and issue a REG_LOGIN. For failure, we
  *     free the nodelist entry.
  */

uint32_t
lpfc_cmpl_plogi_plogi_issue(elxHBA_t * phba,
			    LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFCHBA_t *plhba;
	ELX_IOCBQ_t *cmdiocb, *rspiocb;
	DMABUF_t *pCmd, *pRsp;
	uint32_t *lp;
	IOCB_t *irsp;
	SERV_PARM *sp;
	ELX_MBOXQ_t *mbox;
	elxCfgParam_t *clp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	clp = &phba->config[0];
	cmdiocb = (ELX_IOCBQ_t *) arg;
	rspiocb = cmdiocb->q_f;

	if (ndlp->nlp_flag & NLP_ACC_REGLOGIN) {
		return (ndlp->nlp_state);
	}

	irsp = &rspiocb->iocb;

	if (irsp->ulpStatus == 0) {
		pCmd = (DMABUF_t *) cmdiocb->context2;
		pRsp = (DMABUF_t *) pCmd->next;
		lp = (uint32_t *) pRsp->virt;

		elx_pci_dma_sync((void *)phba, (void *)pRsp, 0,
				 ELX_DMA_SYNC_FORCPU);

		sp = (SERV_PARM *) ((uint8_t *) lp + sizeof (uint32_t));
		if ((lpfc_check_sparm(phba, ndlp, sp, CLASS3))) {
			/* PLOGI chkparm OK */
			elx_printf_log(phba->brd_no, &elx_msgBlk0121,	/* ptr to msg structure */
				       elx_mes0121,	/* ptr to msg */
				       elx_msgBlk0121.msgPreambleStr,	/* begin varargs */
				       ndlp->nlp_DID, ndlp->nlp_state, ndlp->nlp_flag, ((ELX_NODELIST_t *) ndlp)->nlp_rpi);	/* end varargs */

			if ((clp[LPFC_CFG_FCP_CLASS].a_current == CLASS2) &&
			    (sp->cls2.classValid)) {
				ndlp->nle.nlp_fcp_info |= CLASS2;
			} else {
				ndlp->nle.nlp_fcp_info |= CLASS3;
			}

			if ((clp[LPFC_CFG_IP_CLASS].a_current == CLASS2) &&
			    (sp->cls2.classValid)) {
				ndlp->nle.nlp_ip_info = CLASS2;
			} else {
				ndlp->nle.nlp_ip_info = CLASS3;
			}

			if ((mbox =
			     (ELX_MBOXQ_t *) elx_mem_get(phba,
							 MEM_MBOX | MEM_PRI))) {
				if (lpfc_reg_login
				    (phba, irsp->un.elsreq64.remoteID,
				     (uint8_t *) sp, mbox, 0) == 0) {
					/* set_slim mailbox command needs to execute first,
					 * queue this command to be processed later.
					 */
					if (ndlp->nlp_DID == NameServer_DID) {
						mbox->mbox_cmpl =
						    lpfc_mbx_cmpl_ns_reg_login;
					} else if (ndlp->nlp_DID == FDMI_DID) {
						mbox->mbox_cmpl =
						    lpfc_mbx_cmpl_fdmi_reg_login;
					} else {
						mbox->mbox_cmpl =
						    lpfc_mbx_cmpl_reg_login;
					}
					mbox->context2 = (void *)ndlp;
					ndlp->nlp_state =
					    NLP_STE_REG_LOGIN_ISSUE;
					if (elx_sli_issue_mbox
					    (phba, mbox,
					     (MBX_NOWAIT | MBX_STOP_IOCB))
					    != MBX_NOT_FINISHED) {
						return (ndlp->nlp_state);
					}
					elx_mem_put(phba, MEM_MBOX,
						    (uint8_t *) mbox);
				} else {
					elx_mem_put(phba, MEM_MBOX,
						    (uint8_t *) mbox);
				}
			}
		}
	}

	/* If we are in the middle of discovery,
	 * take necessary actions to finish up.
	 */
	if (ndlp->nlp_DID == NameServer_DID) {
		/* Link up / RSCN discovery */
		lpfc_disc_start(phba);
	}
	ndlp->nlp_state = NLP_STE_UNUSED_NODE;

	/* Free this node since the driver cannot login or has the wrong sparm */
	lpfc_freenode(phba, ndlp);
	elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_cmpl_prli_plogi_issue(elxHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;

	/* First ensure ndlp is on the plogi list */
	if (ndlp->nlp_flag & NLP_LIST_MASK) {
		lpfc_findnode_did(phba, (NLP_SEARCH_ALL | NLP_SEARCH_DEQUE),
				  ndlp->nlp_DID);
	}
	lpfc_nlp_plogi(phba, ndlp);

	/* If a PLOGI is not already pending, issue one */
	if (!(ndlp->nlp_flag & NLP_PLOGI_SND)) {
		ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
		lpfc_issue_els_plogi(phba, ndlp, 0);
		ndlp->nlp_flag |= NLP_DISC_NODE;
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_logo_plogi_issue(elxHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	if (!(ndlp->nlp_flag & NLP_PLOGI_SND)) {

		ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
		lpfc_issue_els_plogi(phba, ndlp, 0);
	}
	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_adisc_plogi_issue(elxHBA_t * phba,
			    LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;

	/* First ensure ndlp is on the plogi list */
	if (ndlp->nlp_flag & NLP_LIST_MASK) {
		lpfc_findnode_did(phba, (NLP_SEARCH_ALL | NLP_SEARCH_DEQUE),
				  ndlp->nlp_DID);
	}
	lpfc_nlp_plogi(phba, ndlp);

	/* If a PLOGI is not already pending, issue one */
	if (!(ndlp->nlp_flag & NLP_PLOGI_SND)) {
		ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
		lpfc_issue_els_plogi(phba, ndlp, 0);
		ndlp->nlp_flag |= NLP_DISC_NODE;
	}
	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_reglogin_plogi_issue(elxHBA_t * phba,
			       LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_MBOXQ_t *pmb, *mbox;
	MAILBOX_t *mb;
	uint32_t ldata;
	uint16_t rpi;

	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	pmb = (ELX_MBOXQ_t *) arg;
	mb = &pmb->mb;
	ldata = mb->un.varWords[0];	/* rpi */
	rpi = (uint16_t) (PCIMEM_LONG(ldata) & 0xFFFF);

	/* first unreg node's rpi */
	if ((mbox = (ELX_MBOXQ_t *) elx_mem_get(phba, MEM_MBOX | MEM_PRI))) {
		/* now unreg rpi just got back from reg_login */
		lpfc_unreg_login(phba, rpi, mbox);
		if (elx_sli_issue_mbox(phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) mbox);
		}
	}

	/* software abort outstanding plogi */
	lpfc_driver_abort(phba, ndlp);
	/* send a new plogi */
	ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
	lpfc_issue_els_plogi(phba, ndlp, 0);

	return (ndlp->nlp_state);
}

/*! lpfc_device_rm_plogi_issue
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
  *     This routine is envoked when we a request to remove a nport we are in the
  *     process of PLOGIing. We should issue a software abort on the outstanding 
  *     PLOGI request, then issue a LOGO request. Change node state to
  *     UNUSED_NODE so it can be freed when LOGO completes.
  *
  */

uint32_t
lpfc_device_rm_plogi_issue(elxHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	/* software abort outstanding plogi, before sending LOGO */
	lpfc_driver_abort(phba, ndlp);

	/* If discovery processing causes us to remove a device, it is important
	 * that nothing gets sent to the device (soft zoning issues).
	 */
	lpfc_freenode(phba, ndlp);
	ndlp->nlp_state = NLP_STE_UNUSED_NODE;
	elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_device_unk_plogi_issue(elxHBA_t * phba,
			    LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* software abort outstanding plogi */
	lpfc_driver_abort(phba, ndlp);

	/* dequeue, cancel timeout, unreg login */
	lpfc_freenode(phba, ndlp);
	elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
	return (NLP_STE_UNUSED_NODE);
}

uint32_t
lpfc_rcv_plogi_reglogin_issue(elxHBA_t * phba,
			      LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	uint32_t *lp;
	SERV_PARM *sp;
	LS_RJT stat;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	sp = (SERV_PARM *) ((uint8_t *) lp + sizeof (uint32_t));

	if ((lpfc_check_sparm(phba, ndlp, sp, CLASS3) == 0)) {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	} else {
		/* PLOGI chkparm OK */
		elx_printf_log(phba->brd_no, &elx_msgBlk0122,	/* ptr to msg structure */
			       elx_mes0122,	/* ptr to msg */
			       elx_msgBlk0122.msgPreambleStr,	/* begin varargs */
			       ndlp->nlp_DID, ndlp->nlp_state, ndlp->nlp_flag, ((ELX_NODELIST_t *) ndlp)->nlp_rpi);	/* end varargs */

		lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp, 0);
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_prli_reglogin_issue(elxHBA_t * phba,
			     LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	lpfc_els_rsp_prli_acc(phba, cmdiocb, ndlp);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_logo_reglogin_issue(elxHBA_t * phba,
			     LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0);

	/* resend plogi */
	ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
	lpfc_issue_els_plogi(phba, ndlp, 0);

	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_padisc_reglogin_issue(elxHBA_t * phba,
			       LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	uint32_t *lp;
	SERV_PARM *sp;
	LS_RJT stat;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	sp = (SERV_PARM *) ((uint8_t *) lp + sizeof (uint32_t));

	if ((lpfc_check_sparm(phba, ndlp, sp, CLASS3) == 0)) {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	} else {
		/* PLOGI chkparm OK */
		elx_printf_log(phba->brd_no, &elx_msgBlk0123,	/* ptr to msg structure */
			       elx_mes0123,	/* ptr to msg */
			       elx_msgBlk0123.msgPreambleStr,	/* begin varargs */
			       ndlp->nlp_DID, ndlp->nlp_state, ndlp->nlp_flag, ((ELX_NODELIST_t *) ndlp)->nlp_rpi);	/* end varargs */

		lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp, 0);
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_prlo_reglogin_issue(elxHBA_t * phba,
			     LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_logo_reglogin_issue(elxHBA_t * phba,
			      LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* 
	 * don't really want to do anything since reglogin has not finished,
	 * and we won't let any els happen until the mb is finished. 
	 */
	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_adisc_reglogin_issue(elxHBA_t * phba,
			       LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;

	/* First ensure ndlp is on the plogi list */
	if (ndlp->nlp_flag & NLP_LIST_MASK) {
		lpfc_findnode_did(phba, (NLP_SEARCH_ALL | NLP_SEARCH_DEQUE),
				  ndlp->nlp_DID);
	}
	lpfc_nlp_plogi(phba, ndlp);

	return (ndlp->nlp_state);
}

/*! lpfc_cmpl_reglogin_reglogin_issue
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
  *     This routine is envoked when the REG_LOGIN completes. If unsuccessful,
  *     we should send a LOGO ELS request and free the node entry. If successful,
  *     save the RPI assigned, then issue a PRLI request. The nodelist entry should
  *     be moved to the unmapped list.  If the NPortID indicates a Fabric entity,
  *     don't issue PRLI, just go straight into PRLI_COMPL.
  *              PRLI_COMPL - for fabric entity
  */
uint32_t
lpfc_cmpl_reglogin_reglogin_issue(elxHBA_t * phba,
				  LPFC_NODELIST_t * ndlp,
				  void *arg, uint32_t evt)
{
	ELX_MBOXQ_t *pmb;
	MAILBOX_t *mb;
	LPFCHBA_t *plhba;
	LPFC_NODELIST_t *plogi_ndlp;
	uint32_t did;

	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	pmb = (ELX_MBOXQ_t *) arg;
	mb = &pmb->mb;
	did = mb->un.varWords[1];
	if (mb->mbxStatus ||
	    ((plogi_ndlp = lpfc_findnode_did(phba,
					     (NLP_SEARCH_PLOGI |
					      NLP_SEARCH_DEQUE), did)) == 0)
	    || (ndlp != plogi_ndlp)) {
		/* RegLogin failed */
		elx_printf_log(phba->brd_no, &elx_msgBlk0246,	/* ptr to msg structure */
			       elx_mes0246,	/* ptr to msg */
			       elx_msgBlk0246.msgPreambleStr,	/* begin varargs */
			       did, mb->mbxStatus, phba->hba_state);	/* end varargs */

		if (ndlp->nlp_flag & NLP_LIST_MASK) {
			lpfc_findnode_did(phba,
					  (NLP_SEARCH_ALL | NLP_SEARCH_DEQUE),
					  ndlp->nlp_DID);
		}
		ndlp->nlp_state = NLP_STE_UNUSED_NODE;
		lpfc_freenode(phba, ndlp);
		elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
		return (NLP_STE_FREED_NODE);
	}

	if (ndlp->nle.nlp_rpi != 0)
		lpfc_findnode_remove_rpi(phba, ndlp->nle.nlp_rpi);

	ndlp->nle.nlp_rpi = mb->un.varWords[0];
	lpfc_addnode_rpi(phba, ndlp, ndlp->nle.nlp_rpi);
	lpfc_nlp_unmapped(phba, ndlp);

	/* Only if we are not a fabric nport do we issue PRLI */
	if (!(ndlp->nle.nlp_type & NLP_FABRIC)) {
		lpfc_issue_els_prli(phba, ndlp, 0);
		ndlp->nlp_state = NLP_STE_PRLI_ISSUE;
	} else {
		ndlp->nlp_state = NLP_STE_PRLI_COMPL;
	}

	return (ndlp->nlp_state);
}

/*! lpfc_device_rm_reglogin_issue
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
  *     This routine is envoked when we a request to remove a nport we are in the
  *     process of REG_LOGINing. We should issue a UNREG_LOGIN by did, then
  *     issue a LOGO request. Change node state to NODE_UNUSED, so it will be
  *     freed when LOGO completes.
  *
  */

uint32_t
lpfc_device_rm_reglogin_issue(elxHBA_t * phba,
			      LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_MBOXQ_t *mbox;
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	if (ndlp->nle.nlp_rpi) {
		if ((mbox =
		     (ELX_MBOXQ_t *) elx_mem_get(phba, MEM_MBOX | MEM_PRI))) {
			lpfc_unreg_login(phba, ndlp->nle.nlp_rpi, mbox);
			if (elx_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				elx_mem_put(phba, MEM_MBOX, (uint8_t *) mbox);
			}
		}
		lpfc_findnode_remove_rpi(phba, ndlp->nle.nlp_rpi);
		lpfc_no_rpi(phba, ndlp);
		ndlp->nle.nlp_rpi = 0;
	}

	/* If discovery processing causes us to remove a device, it is important
	 * that nothing gets sent to the device (soft zoning issues).
	 */
	lpfc_freenode(phba, ndlp);
	ndlp->nlp_state = NLP_STE_UNUSED_NODE;
	elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
	return (ndlp->nlp_state);
}

/* DEVICE_ADD for REG_LOGIN_ISSUE is nodev */

uint32_t
lpfc_device_unk_reglogin_issue(elxHBA_t * phba,
			       LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* dequeue, cancel timeout, unreg login */
	lpfc_freenode(phba, ndlp);
	elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
	return (NLP_STE_UNUSED_NODE);
}

uint32_t
lpfc_rcv_plogi_prli_issue(elxHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	uint32_t *lp;
	SERV_PARM *sp;
	LS_RJT stat;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	sp = (SERV_PARM *) ((uint8_t *) lp + sizeof (uint32_t));

	if ((lpfc_check_sparm(phba, ndlp, sp, CLASS3) == 0)) {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	} else {
		/* PLOGI chkparm OK */
		elx_printf_log(phba->brd_no, &elx_msgBlk0124,	/* ptr to msg structure */
			       elx_mes0124,	/* ptr to msg */
			       elx_msgBlk0124.msgPreambleStr,	/* begin varargs */
			       ndlp->nlp_DID, ndlp->nlp_state, ndlp->nlp_flag, ((ELX_NODELIST_t *) ndlp)->nlp_rpi);	/* end varargs */

		lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp, 0);
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_prli_prli_issue(elxHBA_t * phba,
			 LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	lpfc_els_rsp_prli_acc(phba, cmdiocb, ndlp);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_logo_prli_issue(elxHBA_t * phba,
			 LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	/* Software abort outstanding prli before sending acc */
	lpfc_driver_abort(phba, ndlp);

	/* Only call LOGO ACC for first LOGO, this avoids sending unnecessary
	 * PLOGIs during LOGO storms from a device.
	 */
	if (ndlp->nlp_flag & NLP_LOGO_ACC) {
		ndlp->nlp_flag &= ~NLP_LOGO_ACC;
		lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0);
		ndlp->nlp_flag |= NLP_LOGO_ACC;
	} else {
		ndlp->nlp_flag |= NLP_LOGO_ACC;
		lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0);
	}

	/* The driver has to wait until the ACC completes before it continues
	 * processing the LOGO.  The action will resume in lpfc_cmpl_els_logo_acc
	 * routine. Since part of processing includes an unreg_login, the driver waits
	 * so the ACC does not get aborted.
	 */
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_padisc_prli_issue(elxHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	SERV_PARM *sp;		/* used for PDISC */
	ADISC *ap;		/* used for ADISC */
	uint32_t *lp;
	uint32_t cmd;
	NAME_TYPE *pnn, *ppn;
	LS_RJT stat;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;
	cmd = *lp++;

	if (cmd == ELS_CMD_ADISC) {
		ap = (ADISC *) lp;
		pnn = (NAME_TYPE *) & ap->nodeName;
		ppn = (NAME_TYPE *) & ap->portName;
	} else {
		sp = (SERV_PARM *) lp;
		pnn = (NAME_TYPE *) & sp->nodeName;
		ppn = (NAME_TYPE *) & sp->portName;
	}

	if (lpfc_check_adisc(phba, ndlp, pnn, ppn)) {
		if (cmd == ELS_CMD_ADISC) {
			lpfc_els_rsp_adisc_acc(phba, cmdiocb, ndlp);
		} else {
			lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp, 0);
		}
	} else {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	}

	return (ndlp->nlp_state);
}

/* This routine is envoked when we rcv a PRLO request from a nport
 * we are logged into.  We should send back a PRLO rsp setting the
 * appropriate bits.
 * NEXT STATE = PRLI_ISSUE
 */
uint32_t
lpfc_rcv_prlo_prli_issue(elxHBA_t * phba,
			 LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_prli_prli_issue(elxHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFCHBA_t *plhba;
	ELX_IOCBQ_t *cmdiocb, *rspiocb;
	DMABUF_t *pCmd, *pRsp;
	uint32_t *lp;
	IOCB_t *irsp;
	PRLI *npr;
	LPFC_BINDLIST_t *blp;
	ELXSCSILUN_t *lunp;
	ELXSCSITARGET_t *targetp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	cmdiocb = (ELX_IOCBQ_t *) arg;
	rspiocb = cmdiocb->q_f;
	irsp = &rspiocb->iocb;
	if (irsp->ulpStatus) {
		ndlp->nlp_state = NLP_STE_PRLI_COMPL;
		lpfc_set_failmask(phba, ndlp, ELX_DEV_RPTLUN, ELX_CLR_BITMASK);
		return (ndlp->nlp_state);
	}
	pCmd = (DMABUF_t *) cmdiocb->context2;
	pRsp = (DMABUF_t *) pCmd->next;
	lp = (uint32_t *) pRsp->virt;

	elx_pci_dma_sync((void *)phba, (void *)pRsp, 0, ELX_DMA_SYNC_FORCPU);

	npr = (PRLI *) ((uint8_t *) lp + sizeof (uint32_t));

	/* Check out PRLI rsp */
	if ((npr->acceptRspCode != PRLI_REQ_EXECUTED) ||
	    (npr->prliType != PRLI_FCP_TYPE) || (npr->targetFunc != 1)) {
		ndlp->nlp_state = NLP_STE_PRLI_COMPL;
		lpfc_set_failmask(phba, ndlp, ELX_DEV_RPTLUN, ELX_CLR_BITMASK);
		return (ndlp->nlp_state);
	}
	if (npr->Retry == 1) {
		ndlp->nle.nlp_fcp_info |= NLP_FCP_2_DEVICE;
	}

	/* Can we assign a SCSI Id to this NPort */
	if ((blp = lpfc_assign_scsid(phba, ndlp))) {
		lpfc_nlp_mapped(phba, ndlp, blp);
		ndlp->nle.nlp_failMask = 0;
		targetp = ndlp->nlp_Target;
		if (targetp) {
			lunp = (ELXSCSILUN_t *) (targetp->lunlist.q_first);
			while (lunp) {
				lunp->failMask = 0;
				lunp = lunp->pnextLun;
			}
			if (targetp->tmofunc) {
				elx_clk_can(phba, targetp->tmofunc);
				targetp->tmofunc = 0;
			}
		} else {
			/* new target to driver, allocate space to target <sid> lun 0 */
			if (blp->nlp_Target == 0) {
				lpfc_find_lun(phba, blp->nlp_sid, 0, 1);
				blp->nlp_Target =
				    plhba->device_queue_hash[blp->nlp_sid];
			}
		}
		lpfc_set_failmask(phba, ndlp, ELX_DEV_RPTLUN, ELX_SET_BITMASK);

		ndlp->nlp_state = NLP_STE_MAPPED_NODE;
		lpfc_disc_issue_rptlun(phba, ndlp);
	} else {
		ndlp->nlp_state = NLP_STE_PRLI_COMPL;
		ndlp->nlp_flag |= NLP_TGT_NO_SCSIID;
		lpfc_set_failmask(phba, ndlp, ELX_DEV_RPTLUN, ELX_CLR_BITMASK);
	}
	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_logo_prli_issue(elxHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* dequeue, cancel timeout, unreg login */
	lpfc_freenode(phba, ndlp);

	/* software abort outstanding prli, then send logout */
	lpfc_driver_abort(phba, ndlp);
	lpfc_issue_els_logo(phba, ndlp, 0);

	elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
	return (NLP_STE_UNUSED_NODE);
}

uint32_t
lpfc_cmpl_adisc_prli_issue(elxHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;

	/* First ensure ndlp is on the unmap list */
	if (ndlp->nlp_flag & NLP_LIST_MASK) {
		lpfc_findnode_did(phba, (NLP_SEARCH_ALL | NLP_SEARCH_DEQUE),
				  ndlp->nlp_DID);
	}
	lpfc_set_failmask(phba, ndlp, ELX_DEV_RPTLUN, ELX_CLR_BITMASK);
	lpfc_nlp_unmapped(phba, ndlp);

	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_reglogin_prli_issue(elxHBA_t * phba,
			      LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_MBOXQ_t *pmb, *mbox;
	MAILBOX_t *mb;
	uint32_t ldata;
	uint16_t rpi;

	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	pmb = (ELX_MBOXQ_t *) arg;
	mb = &pmb->mb;
	ldata = mb->un.varWords[0];	/* rpi */
	rpi = (uint16_t) (PCIMEM_LONG(ldata) & 0xFFFF);

	if (ndlp->nle.nlp_rpi != rpi) {
		/* first unreg node's rpi */
		if ((mbox =
		     (ELX_MBOXQ_t *) elx_mem_get(phba, MEM_MBOX | MEM_PRI))) {
			lpfc_unreg_login(phba, ndlp->nle.nlp_rpi, mbox);
			if (elx_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				elx_mem_put(phba, MEM_MBOX, (uint8_t *) mbox);
			}
		}
		lpfc_findnode_remove_rpi(phba, ndlp->nle.nlp_rpi);
		lpfc_no_rpi(phba, ndlp);
		ndlp->nle.nlp_rpi = 0;

		/* now unreg rpi just got back from reg_login */
		lpfc_unreg_login(phba, rpi, mbox);
		if (elx_sli_issue_mbox(phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) mbox);
		}

		/* software abort outstanding prli */
		lpfc_driver_abort(phba, ndlp);

		/* send logout and put this node on plogi list */
		lpfc_issue_els_logo(phba, ndlp, 0);
		ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
		lpfc_nlp_plogi(phba, ndlp);
	}

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
uint32_t
lpfc_device_rm_prli_issue(elxHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	/* software abort outstanding prli */
	lpfc_driver_abort(phba, ndlp);

	/* dequeue, cancel timeout, unreg login */
	lpfc_freenode(phba, ndlp);

	ndlp->nlp_state = NLP_STE_UNUSED_NODE;
	elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_device_add_prli_issue(elxHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	if (ndlp->nlp_tmofunc) {
		ndlp->nlp_flag &= ~(NLP_NODEV_TMO | NLP_DELAY_TMO);
		ndlp->nle.nlp_rflag &= ~NLP_NPR_ACTIVE;
		elx_clk_can(phba, ndlp->nlp_tmofunc);
		ndlp->nlp_tmofunc = 0;
	}
	/* software abort outstanding prli */
	lpfc_driver_abort(phba, ndlp);

	lpfc_nlp_adisc(phba, ndlp);
	return (ndlp->nlp_state);
}

/*! lpfc_device_unk_prli_issue
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
uint32_t
lpfc_device_unk_prli_issue(elxHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* software abort outstanding prli */
	lpfc_driver_abort(phba, ndlp);

	/* dequeue, cancel timeout, unreg login */
	lpfc_freenode(phba, ndlp);
	elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
	return (NLP_STE_UNUSED_NODE);
}

uint32_t
lpfc_rcv_plogi_prli_compl(elxHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	uint32_t *lp;
	SERV_PARM *sp;
	LS_RJT stat;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	sp = (SERV_PARM *) ((uint8_t *) lp + sizeof (uint32_t));

	if ((phba->hba_state <= ELX_FLOGI) ||
	    ((lpfc_check_sparm(phba, ndlp, sp, CLASS3) == 0))) {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		if (phba->hba_state <= ELX_FLOGI) {
			stat.un.b.lsRjtRsnCodeExp = LSRJT_LOGICAL_BSY;
		} else {
			stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		}
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	} else {
		/* PLOGI chkparm OK */
		elx_printf_log(phba->brd_no, &elx_msgBlk0125,	/* ptr to msg structure */
			       elx_mes0125,	/* ptr to msg */
			       elx_msgBlk0125.msgPreambleStr,	/* begin varargs */
			       ndlp->nlp_DID, ndlp->nlp_state, ndlp->nlp_flag, ((ELX_NODELIST_t *) ndlp)->nlp_rpi);	/* end varargs */

		lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp, 0);
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_prli_prli_compl(elxHBA_t * phba,
			 LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	lpfc_els_rsp_prli_acc(phba, cmdiocb, ndlp);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_logo_prli_compl(elxHBA_t * phba,
			 LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	/* software abort outstanding adisc before sending acc */
	if (ndlp->nlp_flag & NLP_ADISC_SND) {
		lpfc_driver_abort(phba, ndlp);
	}
	/* Only call LOGO ACC for first LOGO, this avoids sending unnecessary
	 * PLOGIs during LOGO storms from a device.
	 */
	if (ndlp->nlp_flag & NLP_LOGO_ACC) {
		ndlp->nlp_flag &= ~NLP_LOGO_ACC;
		lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0);
		ndlp->nlp_flag |= NLP_LOGO_ACC;
	} else {
		ndlp->nlp_flag |= NLP_LOGO_ACC;
		lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0);
	}

	/* The driver has to wait until the ACC completes before we can continue
	 * processing the LOGO, the action will resume in lpfc_cmpl_els_logo_acc.
	 * Since part of processing includes an unreg_login, the driver waits
	 * so the ACC does not get aborted.
	 */
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_padisc_prli_compl(elxHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	SERV_PARM *sp;		/* used for PDISC */
	ADISC *ap;		/* used for ADISC */
	uint32_t *lp;
	uint32_t cmd;
	NAME_TYPE *pnn, *ppn;
	LS_RJT stat;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;
	cmd = *lp++;

	if (cmd == ELS_CMD_ADISC) {
		ap = (ADISC *) lp;
		pnn = (NAME_TYPE *) & ap->nodeName;
		ppn = (NAME_TYPE *) & ap->portName;
	} else {
		sp = (SERV_PARM *) lp;
		pnn = (NAME_TYPE *) & sp->nodeName;
		ppn = (NAME_TYPE *) & sp->portName;
	}

	if (lpfc_check_adisc(phba, ndlp, pnn, ppn)) {
		if (cmd == ELS_CMD_ADISC) {
			lpfc_els_rsp_adisc_acc(phba, cmdiocb, ndlp);
		} else {
			lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp, 0);
		}
	} else {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_prlo_prli_compl(elxHBA_t * phba,
			 LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_logo_prli_compl(elxHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* dequeue, cancel timeout, unreg login */
	lpfc_freenode(phba, ndlp);

	if (ndlp->nlp_flag & NLP_ADISC_SND) {
		/* software abort outstanding adisc */
		lpfc_driver_abort(phba, ndlp);
	}

	ndlp->nlp_state = NLP_STE_UNUSED_NODE;
	elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
	return (NLP_STE_UNUSED_NODE);
}

uint32_t
lpfc_cmpl_adisc_prli_compl(elxHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFCHBA_t *plhba;
	ELX_IOCBQ_t *cmdiocb, *rspiocb;
	DMABUF_t *pCmd, *pRsp;
	uint32_t *lp;
	IOCB_t *irsp;
	ADISC *ap;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	cmdiocb = (ELX_IOCBQ_t *) arg;
	rspiocb = cmdiocb->q_f;
	irsp = &rspiocb->iocb;

	/* First remove the ndlp from any list */
	if (ndlp->nlp_flag & NLP_LIST_MASK) {
		lpfc_findnode_did(phba, (NLP_SEARCH_ALL | NLP_SEARCH_DEQUE),
				  ndlp->nlp_DID);
	}

	if (irsp->ulpStatus) {
		lpfc_freenode(phba, ndlp);
		ndlp->nlp_state = NLP_STE_UNUSED_NODE;
		lpfc_nlp_plogi(phba, ndlp);
		return (ndlp->nlp_state);
	}

	pCmd = (DMABUF_t *) cmdiocb->context2;
	pRsp = (DMABUF_t *) pCmd->next;
	lp = (uint32_t *) pRsp->virt;

	elx_pci_dma_sync((void *)phba, (void *)pRsp, 0, ELX_DMA_SYNC_FORCPU);

	ap = (ADISC *) ((uint8_t *) lp + sizeof (uint32_t));

	/* Check out ADISC rsp */
	if ((lpfc_check_adisc(phba, ndlp, &ap->nodeName, &ap->portName) == 0)) {
		lpfc_freenode(phba, ndlp);
		ndlp->nlp_state = NLP_STE_UNUSED_NODE;
		lpfc_nlp_plogi(phba, ndlp);
		return (ndlp->nlp_state);
	}
	lpfc_set_failmask(phba, ndlp, ELX_DEV_RPTLUN, ELX_CLR_BITMASK);
	lpfc_nlp_unmapped(phba, ndlp);

	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_reglogin_prli_compl(elxHBA_t * phba,
			      LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_MBOXQ_t *pmb, *mbox;
	MAILBOX_t *mb;
	uint32_t ldata;
	uint16_t rpi;

	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	pmb = (ELX_MBOXQ_t *) arg;
	mb = &pmb->mb;
	ldata = mb->un.varWords[0];	/* rpi */
	rpi = (uint16_t) (PCIMEM_LONG(ldata) & 0xFFFF);

	if (ndlp->nle.nlp_rpi != rpi) {
		/* first unreg node's rpi */
		if ((mbox =
		     (ELX_MBOXQ_t *) elx_mem_get(phba, MEM_MBOX | MEM_PRI))) {
			lpfc_unreg_login(phba, ndlp->nle.nlp_rpi, mbox);
			if (elx_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				elx_mem_put(phba, MEM_MBOX, (uint8_t *) mbox);
			}
		}
		lpfc_findnode_remove_rpi(phba, ndlp->nle.nlp_rpi);
		lpfc_no_rpi(phba, ndlp);
		ndlp->nle.nlp_rpi = 0;

		/* now unreg rpi just got back from reg_login */
		lpfc_unreg_login(phba, rpi, mbox);
		if (elx_sli_issue_mbox(phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) mbox);
		}

		if (ndlp->nlp_flag & NLP_ADISC_SND) {
			/* software abort outstanding adisc */
			lpfc_driver_abort(phba, ndlp);
		}

		/* send logout and put this node on plogi list */
		lpfc_issue_els_logo(phba, ndlp, 0);
		ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
		lpfc_nlp_plogi(phba, ndlp);
	}

	return (ndlp->nlp_state);
}

/*! lpfc_device_rm_prli_compl
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
  *    This routine is envoked when we a request to remove a nport.
  *    It could be called when linkdown or nodev timer expires.
  *    If nodev timer is still running, we just want to exit.
  *    If this node timed out, we want to abort outstanding ADISC,
  *    unreg login, send logout, change state to UNUSED_NODE and
  *    place node on plogi list so it can be freed when LOGO completes.
  *
  */
uint32_t
lpfc_device_rm_prli_compl(elxHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	/* if nodev timer is running, then we just exit */
	if (!(ndlp->nlp_flag & NLP_NODEV_TMO)) {
		lpfc_set_failmask(phba, ndlp, ELX_DEV_DISCONNECTED,
				  ELX_SET_BITMASK);
		if (ndlp->nlp_flag & NLP_ADISC_SND) {
			/* software abort outstanding adisc */
			lpfc_driver_abort(phba, ndlp);
		}
		/* dequeue, cancel timeout, unreg login */
		lpfc_freenode(phba, ndlp);

		/* If discovery processing causes us to remove a device, it is important
		 * that nothing gets sent to the device (soft zoning issues).
		 */
		ndlp->nlp_state = NLP_STE_UNUSED_NODE;
		elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
		return (NLP_STE_UNUSED_NODE);
	}
	lpfc_set_failmask(phba, ndlp, ELX_DEV_DISAPPEARED, ELX_SET_BITMASK);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_device_add_prli_compl(elxHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	if (ndlp->nlp_tmofunc) {
		ndlp->nlp_flag &= ~(NLP_NODEV_TMO | NLP_DELAY_TMO);
		ndlp->nle.nlp_rflag &= ~NLP_NPR_ACTIVE;
		elx_clk_can(phba, ndlp->nlp_tmofunc);
		ndlp->nlp_tmofunc = 0;
	}
	lpfc_nlp_adisc(phba, ndlp);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_device_unk_prli_compl(elxHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFCHBA_t *plhba;
	elxCfgParam_t *clp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	clp = &phba->config[0];

	/* If we are in pt2pt mode, free node */
	if (plhba->fc_flag & FC_PT2PT) {
		/* dequeue, cancel timeout, unreg login */
		lpfc_freenode(phba, ndlp);
		elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
		return (NLP_STE_UNUSED_NODE);
	}

	/* linkdown timer should be running at this time.  Check to 
	 * see if the driver needs to start a nodev timer
	 */
	if ((clp[ELX_CFG_NODEV_TMO].a_current) &&
	    (clp[ELX_CFG_HOLDIO].a_current == 0)) {
		/* if some timer's running, cancel it whether it's nodev timer
		 * or delay timer
		 */
		if (ndlp->nlp_tmofunc) {
			ndlp->nlp_flag &= ~(NLP_NODEV_TMO | NLP_DELAY_TMO);
			ndlp->nle.nlp_rflag &= ~NLP_NPR_ACTIVE;
			elx_clk_can(phba, ndlp->nlp_tmofunc);
			ndlp->nlp_tmofunc = 0;
		}

		/* now start a nodev timer */
		ndlp->nlp_flag |= NLP_NODEV_TMO;
		ndlp->nle.nlp_rflag |= NLP_NPR_ACTIVE;
		if (clp[ELX_CFG_NODEV_TMO].a_current >
		    clp[ELX_CFG_LINKDOWN_TMO].a_current) {
			ndlp->nlp_tmofunc =
			    elx_clk_set(phba, clp[ELX_CFG_NODEV_TMO].a_current,
					lpfc_nodev_timeout, (void *)ndlp,
					(void *)0);
		} else {
			ndlp->nlp_tmofunc =
			    elx_clk_set(phba,
					clp[ELX_CFG_LINKDOWN_TMO].a_current,
					lpfc_nodev_timeout, (void *)ndlp,
					(void *)0);
		}

		/* Start nodev timer */
		elx_printf_log(phba->brd_no, &elx_msgBlk0706,	/* ptr to msg structure */
			       elx_mes0706,	/* ptr to msg */
			       elx_msgBlk0706.msgPreambleStr,	/* begin varargs */
			       ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state, ndlp);	/* end varargs */
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_plogi_mapped_node(elxHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	uint32_t *lp;
	SERV_PARM *sp;
	LS_RJT stat;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	sp = (SERV_PARM *) ((uint8_t *) lp + sizeof (uint32_t));

	if ((phba->hba_state <= ELX_FLOGI) ||
	    ((lpfc_check_sparm(phba, ndlp, sp, CLASS3) == 0))) {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		if (phba->hba_state <= ELX_FLOGI) {
			stat.un.b.lsRjtRsnCodeExp = LSRJT_LOGICAL_BSY;
		} else {
			stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		}
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	} else {
		/* PLOGI chkparm OK */
		elx_printf_log(phba->brd_no, &elx_msgBlk0126,	/* ptr to msg structure */
			       elx_mes0126,	/* ptr to msg */
			       elx_msgBlk0126.msgPreambleStr,	/* begin varargs */
			       ndlp->nlp_DID, ndlp->nlp_state, ndlp->nlp_flag, ((ELX_NODELIST_t *) ndlp)->nlp_rpi);	/* end varargs */

		lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp, 0);
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_prli_mapped_node(elxHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	lpfc_els_rsp_prli_acc(phba, cmdiocb, ndlp);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_logo_mapped_node(elxHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	/* software abort outstanding adisc before sending acc */
	if (ndlp->nlp_flag & NLP_ADISC_SND) {
		lpfc_driver_abort(phba, ndlp);
	}
	/* Only call LOGO ACC for first LOGO, this avoids sending unnecessary
	 * PLOGIs during LOGO storms from a device.
	 */
	if (ndlp->nlp_flag & NLP_LOGO_ACC) {
		ndlp->nlp_flag &= ~NLP_LOGO_ACC;
		lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0);
		ndlp->nlp_flag |= NLP_LOGO_ACC;
	} else {
		ndlp->nlp_flag |= NLP_LOGO_ACC;
		lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0);
	}

	/* The driver has to wait until the ACC completes before we can continue
	 * processing the LOGO, the action will resume in lpfc_cmpl_els_logo_acc.
	 * Since part of processing includes an unreg_login, the driver waits
	 * so the ACC does not get aborted.
	 */
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_padisc_mapped_node(elxHBA_t * phba,
			    LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	SERV_PARM *sp;		/* used for PDISC */
	ADISC *ap;		/* used for ADISC */
	uint32_t *lp;
	uint32_t cmd;
	NAME_TYPE *pnn, *ppn;
	LS_RJT stat;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;
	cmd = *lp++;

	if (cmd == ELS_CMD_ADISC) {
		ap = (ADISC *) lp;
		pnn = (NAME_TYPE *) & ap->nodeName;
		ppn = (NAME_TYPE *) & ap->portName;
	} else {
		sp = (SERV_PARM *) lp;
		pnn = (NAME_TYPE *) & sp->nodeName;
		ppn = (NAME_TYPE *) & sp->portName;
	}

	if (lpfc_check_adisc(phba, ndlp, pnn, ppn)) {
		if (cmd == ELS_CMD_ADISC) {
			lpfc_els_rsp_adisc_acc(phba, cmdiocb, ndlp);
		} else {
			lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp, 0);
		}
	} else {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_prlo_mapped_node(elxHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;

	cmdiocb = (ELX_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;

	elx_pci_dma_sync((void *)phba, (void *)pCmd, 0, ELX_DMA_SYNC_FORCPU);

	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0);

	/* save binding on binding list */
	if (ndlp->nlp_listp_bind) {
		lpfc_nlp_bind(phba, ndlp->nlp_listp_bind);

		elx_sched_flush_target(phba, ndlp->nlp_Target,
				       IOSTAT_DRIVER_REJECT, IOERR_SLI_ABORTED);
		ndlp->nlp_listp_bind = 0;
		ndlp->nlp_pan = 0;
		ndlp->nlp_sid = 0;
		ndlp->nlp_Target = 0;
		ndlp->nlp_flag &= ~NLP_SEED_MASK;
	}

	ndlp->nlp_state = NLP_STE_PRLI_COMPL;
	lpfc_nlp_unmapped(phba, ndlp);

	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_logo_mapped_node(elxHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELXSCSITARGET_t *targetp;

	/* save binding on binding list */
	if (ndlp->nlp_listp_bind) {
		lpfc_nlp_bind(phba, ndlp->nlp_listp_bind);

		if ((targetp = ndlp->nlp_Target)) {
			elx_sched_flush_target(phba, targetp,
					       IOSTAT_DRIVER_REJECT,
					       IOERR_SLI_ABORTED);
		}
		ndlp->nlp_listp_bind = 0;
		ndlp->nlp_pan = 0;
		ndlp->nlp_sid = 0;
		ndlp->nlp_Target = 0;
		ndlp->nlp_flag &= ~NLP_SEED_MASK;
	}

	/* dequeue, cancel timeout, unreg login */
	lpfc_freenode(phba, ndlp);

	/* software abort outstanding adisc */
	if (ndlp->nlp_flag & NLP_ADISC_SND) {
		lpfc_driver_abort(phba, ndlp);
	}

	elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
	return (NLP_STE_UNUSED_NODE);
}

uint32_t
lpfc_cmpl_adisc_mapped_node(elxHBA_t * phba,
			    LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFCHBA_t *plhba;
	ELX_IOCBQ_t *cmdiocb, *rspiocb;
	DMABUF_t *pCmd, *pRsp;
	uint32_t *lp;
	IOCB_t *irsp;
	LPFC_BINDLIST_t *blp;
	ELXSCSILUN_t *lunp;
	ELXSCSITARGET_t *targetp;
	ADISC *ap;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	cmdiocb = (ELX_IOCBQ_t *) arg;
	rspiocb = cmdiocb->q_f;
	irsp = &rspiocb->iocb;

	/* First remove the ndlp from any list */
	if (ndlp->nlp_flag & NLP_LIST_MASK) {
		lpfc_findnode_did(phba, (NLP_SEARCH_ALL | NLP_SEARCH_DEQUE),
				  ndlp->nlp_DID);
	}
	if (irsp->ulpStatus) {
		/* If this is not a driver aborted ADISC, handle the recovery here */
		if (irsp->ulpStatus != IOSTAT_DRIVER_REJECT) {
			lpfc_freenode(phba, ndlp);
			ndlp->nlp_state = NLP_STE_UNUSED_NODE;
			lpfc_nlp_plogi(phba, ndlp);
		}
		return (ndlp->nlp_state);
	}

	pCmd = (DMABUF_t *) cmdiocb->context2;
	pRsp = (DMABUF_t *) pCmd->next;
	lp = (uint32_t *) pRsp->virt;

	elx_pci_dma_sync((void *)phba, (void *)pRsp, 0, ELX_DMA_SYNC_FORCPU);

	ap = (ADISC *) ((uint8_t *) lp + sizeof (uint32_t));

	/* Check out ADISC rsp */
	if ((lpfc_check_adisc(phba, ndlp, &ap->nodeName, &ap->portName) == 0)) {
		/* This is not a driver aborted ADISC, so handle the recovery here */
		lpfc_freenode(phba, ndlp);
		ndlp->nlp_state = NLP_STE_UNUSED_NODE;
		lpfc_nlp_plogi(phba, ndlp);
		return (ndlp->nlp_state);
	}

	/* Can we reassign a SCSI Id to this NPort */
	if ((blp = lpfc_assign_scsid(phba, ndlp))) {
		lpfc_nlp_mapped(phba, ndlp, blp);

		ndlp->nlp_state = NLP_STE_MAPPED_NODE;
		targetp = ndlp->nlp_Target;
		if (targetp) {
			targetp->targetFlags &= ~FC_NPR_ACTIVE;
			if (targetp->tmofunc) {
				elx_clk_can(phba, targetp->tmofunc);
				targetp->tmofunc = 0;
			}
			lunp = (ELXSCSILUN_t *) targetp->lunlist.q_first;
			while (lunp) {
				lunp->pnode = (ELX_NODELIST_t *) ndlp;
				lunp = lunp->pnextLun;
			}
		} else {
			/* new target to driver, allocate space to target <sid> lun 0 */
			if (blp->nlp_Target == 0) {
				lpfc_find_lun(phba, blp->nlp_sid, 0, 1);
				blp->nlp_Target =
				    plhba->device_queue_hash[blp->nlp_sid];
			}
		}
		lpfc_set_failmask(phba, ndlp, ELX_DEV_ALL_BITS,
				  ELX_CLR_BITMASK);
	} else {
		lpfc_nlp_unmapped(phba, ndlp);
		ndlp->nlp_state = NLP_STE_PRLI_COMPL;
		ndlp->nlp_flag |= NLP_TGT_NO_SCSIID;
		lpfc_set_failmask(phba, ndlp, ELX_DEV_RPTLUN, ELX_CLR_BITMASK);
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_reglogin_mapped_node(elxHBA_t * phba,
			       LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	ELX_MBOXQ_t *pmb, *mbox;
	ELX_SLI_t *psli;
	MAILBOX_t *mb;
	uint32_t ldata;
	uint16_t rpi;

	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	pmb = (ELX_MBOXQ_t *) arg;
	mb = &pmb->mb;
	ldata = mb->un.varWords[0];	/* rpi */
	rpi = (uint16_t) (PCIMEM_LONG(ldata) & 0xFFFF);

	if (ndlp->nle.nlp_rpi != rpi) {
		/* first unreg node's rpi */
		if ((mbox =
		     (ELX_MBOXQ_t *) elx_mem_get(phba, MEM_MBOX | MEM_PRI))) {
			lpfc_unreg_login(phba, ndlp->nle.nlp_rpi, mbox);
			if (elx_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				elx_mem_put(phba, MEM_MBOX, (uint8_t *) mbox);
			}
		}
		lpfc_findnode_remove_rpi(phba, ndlp->nle.nlp_rpi);
		lpfc_no_rpi(phba, ndlp);
		ndlp->nle.nlp_rpi = 0;

		/* now unreg rpi just got back from reg_login */
		lpfc_unreg_login(phba, rpi, mbox);
		if (elx_sli_issue_mbox(phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) mbox);
		}

		/* save binding on binding list */
		if (ndlp->nlp_listp_bind) {
			lpfc_nlp_bind(phba, ndlp->nlp_listp_bind);

			elx_sched_flush_target(phba, ndlp->nlp_Target,
					       IOSTAT_DRIVER_REJECT,
					       IOERR_SLI_ABORTED);
			ndlp->nlp_listp_bind = 0;
			ndlp->nlp_pan = 0;
			ndlp->nlp_sid = 0;
			ndlp->nlp_Target = 0;
			ndlp->nlp_flag &= ~NLP_SEED_MASK;
		}

		/* If this node is running IPFC, flush any pending IP bufs.  An explicit
		 * call is made since the node is not getting returned to the free list.
		 */
		psli = &phba->sli;
		if (ndlp->nle.nlp_type & NLP_IP_NODE) {
			lpfc_ip_flush_iocb(phba, &psli->ring[psli->ip_ring],
					   ndlp, FLUSH_NODE);
		}

		/* software abort outstanding adisc */
		if (ndlp->nlp_flag & NLP_ADISC_SND) {
			lpfc_driver_abort(phba, ndlp);
		}

		/* send logout and put this node on plogi list */
		lpfc_issue_els_logo(phba, ndlp, 0);
		ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
		lpfc_nlp_plogi(phba, ndlp);
	}

	return (ndlp->nlp_state);
}

/*! lpfc_device_rm_mapped_node
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
  *    This routine is envoked when we a request to remove a nport.
  *    It could be called when linkdown or nodev timer expires.
  *    If nodev timer is still running, we just want to exit.
  *    If this node timed out, we want to abort outstanding ADISC,
  *    save its binding, unreg login, send logout, change state to 
  *    UNUSED_NODE and place node on plogi list so it can be freed 
  *    when LOGO completes.
  *
  */
uint32_t
lpfc_device_rm_mapped_node(elxHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFCHBA_t *plhba;
	ELXSCSITARGET_t *targetp;
	elxCfgParam_t *clp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	clp = &phba->config[0];
	/* if nodev timer is running, then we just exit */
	if (!(ndlp->nlp_flag & NLP_NODEV_TMO)) {
		lpfc_set_failmask(phba, ndlp, ELX_DEV_DISCONNECTED,
				  ELX_SET_BITMASK);

		if (ndlp->nlp_flag & NLP_ADISC_SND) {
			/* software abort outstanding adisc */
			lpfc_driver_abort(phba, ndlp);
		}

		/* save binding info */
		if (ndlp->nlp_listp_bind) {
			targetp = ndlp->nlp_Target;
			lpfc_nlp_bind(phba, ndlp->nlp_listp_bind);
			if (targetp) {
				targetp->targetFlags &= ~FC_NPR_ACTIVE;
				if (targetp->tmofunc) {
					elx_clk_can(phba, targetp->tmofunc);
					targetp->tmofunc = 0;
				}
				elx_sched_flush_target(phba, targetp,
						       IOSTAT_DRIVER_REJECT,
						       IOERR_SLI_ABORTED);
			}

			ndlp->nlp_listp_bind = 0;
			ndlp->nlp_pan = 0;
			ndlp->nlp_sid = 0;
			ndlp->nlp_Target = 0;
			ndlp->nlp_flag &= ~NLP_SEED_MASK;
		}

		/* dequeue, cancel timeout, unreg login */
		lpfc_freenode(phba, ndlp);

		/* If discovery processing causes us to remove a device, it is important
		 * that nothing gets sent to the device (soft zoning issues).
		 */
		ndlp->nlp_state = NLP_STE_UNUSED_NODE;
		elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
		return (NLP_STE_UNUSED_NODE);
	}
	targetp = ndlp->nlp_Target;
	if (targetp) {
		targetp->targetFlags |= FC_NPR_ACTIVE;
		if (targetp->tmofunc) {
			elx_clk_can(phba, targetp->tmofunc);
		}
		if (clp[ELX_CFG_NODEV_TMO].a_current >
		    clp[ELX_CFG_LINKDOWN_TMO].a_current) {
			targetp->tmofunc =
			    elx_clk_set(phba, clp[ELX_CFG_NODEV_TMO].a_current,
					lpfc_npr_timeout, (void *)targetp,
					(void *)0);
		} else {
			targetp->tmofunc = elx_clk_set(phba,
						       clp
						       [ELX_CFG_LINKDOWN_TMO].
						       a_current,
						       lpfc_npr_timeout,
						       (void *)targetp,
						       (void *)0);
		}
	}
	lpfc_set_failmask(phba, ndlp, ELX_DEV_DISAPPEARED, ELX_SET_BITMASK);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_device_add_mapped_node(elxHBA_t * phba,
			    LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	if (ndlp->nlp_tmofunc) {
		ndlp->nlp_flag &= ~(NLP_NODEV_TMO | NLP_DELAY_TMO);
		ndlp->nle.nlp_rflag &= ~NLP_NPR_ACTIVE;
		elx_clk_can(phba, ndlp->nlp_tmofunc);
		ndlp->nlp_tmofunc = 0;
	}
	lpfc_nlp_adisc(phba, ndlp);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_device_unk_mapped_node(elxHBA_t * phba,
			    LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFCHBA_t *plhba;
	ELXSCSITARGET_t *targetp;
	elxCfgParam_t *clp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	clp = &phba->config[0];

	/* If we are in pt2pt mode, free node */
	if (plhba->fc_flag & FC_PT2PT) {
		/* dequeue, cancel timeout, unreg login */
		lpfc_freenode(phba, ndlp);
		elx_mem_put(phba, MEM_NLP, (uint8_t *) ndlp);
		return (NLP_STE_UNUSED_NODE);
	}

	targetp = ndlp->nlp_Target;
	if (targetp) {
		targetp->targetFlags |= FC_NPR_ACTIVE;
		if (targetp->tmofunc) {
			elx_clk_can(phba, targetp->tmofunc);
		}
		if (clp[ELX_CFG_NODEV_TMO].a_current >
		    clp[ELX_CFG_LINKDOWN_TMO].a_current) {
			targetp->tmofunc =
			    elx_clk_set(phba, clp[ELX_CFG_NODEV_TMO].a_current,
					lpfc_npr_timeout, (void *)targetp,
					(void *)0);
		} else {
			targetp->tmofunc = elx_clk_set(phba,
						       clp
						       [ELX_CFG_LINKDOWN_TMO].
						       a_current,
						       lpfc_npr_timeout,
						       (void *)targetp,
						       (void *)0);
		}
	}

	ndlp->nle.nlp_rflag |= NLP_NPR_ACTIVE;
	/* linkdown timer should be running at this time.  Check to 
	 * see if the driver has to start a nodev timer
	 */
	if ((clp[ELX_CFG_NODEV_TMO].a_current) &&
	    (clp[ELX_CFG_HOLDIO].a_current == 0)) {
		/* if some timer's running, cancel it whether it's nodev timer
		 * or delay timer
		 */
		if (ndlp->nlp_tmofunc) {
			ndlp->nlp_flag &= ~(NLP_NODEV_TMO | NLP_DELAY_TMO);
			elx_clk_can(phba, ndlp->nlp_tmofunc);
			ndlp->nlp_tmofunc = 0;
		}

		/* now start a nodev timer */
		ndlp->nlp_flag |= NLP_NODEV_TMO;
		if (clp[ELX_CFG_NODEV_TMO].a_current >
		    clp[ELX_CFG_LINKDOWN_TMO].a_current) {
			ndlp->nlp_tmofunc =
			    elx_clk_set(phba, clp[ELX_CFG_NODEV_TMO].a_current,
					lpfc_nodev_timeout, (void *)ndlp,
					(void *)0);
		} else {
			ndlp->nlp_tmofunc =
			    elx_clk_set(phba,
					clp[ELX_CFG_LINKDOWN_TMO].a_current,
					lpfc_nodev_timeout, (void *)ndlp,
					(void *)0);
		}

		/* Start nodev timer */
		elx_printf_log(phba->brd_no, &elx_msgBlk0700,	/* ptr to msg structure */
			       elx_mes0700,	/* ptr to msg */
			       elx_msgBlk0700.msgPreambleStr,	/* begin varargs */
			       ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state, ndlp);	/* end varargs */
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_disc_nodev(elxHBA_t * phba,
		LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* This routine does nothing, just return the current state */
	return (ndlp->nlp_state);
}

uint32_t
lpfc_disc_neverdev(elxHBA_t * phba,
		   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* This routine does nothing, just return the current state */
	return (ndlp->nlp_state);
}

int
lpfc_geportname(NAME_TYPE * pn1, NAME_TYPE * pn2)
{
	int i;
	uint8_t *cp1, *cp2;

	i = sizeof (NAME_TYPE);
	cp1 = (uint8_t *) pn1;
	cp2 = (uint8_t *) pn2;
	while (i--) {
		if (*cp1 < *cp2) {
			return (0);
		}
		if (*cp1 > *cp2) {
			return (1);
		}
		cp1++;
		cp2++;
	}

	return (2);		/* equal */
}

int
lpfc_check_sparm(elxHBA_t * phba,
		 LPFC_NODELIST_t * ndlp, SERV_PARM * sp, uint32_t class)
{
	volatile SERV_PARM *hsp;
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	hsp = &plhba->fc_sparam;
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
	memcpy(&ndlp->nlp_nodename, &sp->nodeName, sizeof (NAME_TYPE));
	memcpy(&ndlp->nlp_portname, &sp->portName, sizeof (NAME_TYPE));
	return (1);
}

int
lpfc_check_adisc(elxHBA_t * phba,
		 LPFC_NODELIST_t * ndlp, NAME_TYPE * nn, NAME_TYPE * pn)
{
	if (lpfc_geportname((NAME_TYPE *) nn, &ndlp->nlp_nodename) != 2) {
		return (0);
	}

	if (lpfc_geportname((NAME_TYPE *) pn, &ndlp->nlp_portname) != 2) {
		return (0);
	}

	return (1);
}

int
lpfc_binding_found(LPFC_BINDLIST_t * blp, LPFC_NODELIST_t * ndlp)
{
	uint16_t bindtype;

	bindtype = blp->nlp_bind_type;
	if ((bindtype & FCP_SEED_DID) && (ndlp->nlp_DID == blp->nlp_DID)) {
		return (1);
	} else if ((bindtype & FCP_SEED_WWPN) &&
		   (lpfc_geportname(&ndlp->nlp_portname, &blp->nlp_portname) ==
		    2)) {
		return (1);
	} else if ((bindtype & FCP_SEED_WWNN) &&
		   (lpfc_geportname(&ndlp->nlp_nodename, &blp->nlp_nodename) ==
		    2)) {
		return (1);
	}
	return (0);
}

int
lpfc_binding_useid(elxHBA_t * phba, uint16_t pan, uint16_t sid)
{
	LPFCHBA_t *plhba;
	LPFC_BINDLIST_t *blp;
	unsigned long iflag;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	ELX_DISC_LOCK(phba, iflag);
	blp = plhba->fc_nlpbind_start;
	while ((blp) && (blp != (LPFC_BINDLIST_t *) & plhba->fc_nlpbind_start)) {
		if ((blp->nlp_pan == pan) && (blp->nlp_sid == sid)) {
			ELX_DISC_UNLOCK(phba, iflag);
			return (1);
		}
		blp = blp->nlp_listp_next;
	}
	ELX_DISC_UNLOCK(phba, iflag);
	return (0);
}

int
lpfc_mapping_useid(elxHBA_t * phba, uint16_t pan, uint16_t sid)
{
	LPFCHBA_t *plhba;
	LPFC_NODELIST_t *mapnode;
	LPFC_BINDLIST_t *blp;
	unsigned long iflag;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	ELX_DISC_LOCK(phba, iflag);
	mapnode = plhba->fc_nlpmap_start;

	while ((mapnode)
	       && (mapnode != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start)) {
		blp = mapnode->nlp_listp_bind;
		if ((blp->nlp_pan == pan) && (blp->nlp_sid == sid)) {
			ELX_DISC_UNLOCK(phba, iflag);
			return (1);
		}
		mapnode = (LPFC_NODELIST_t *) (mapnode->nle.nlp_listp_next);
	}
	ELX_DISC_UNLOCK(phba, iflag);
	return (0);
}

LPFC_BINDLIST_t *
lpfc_create_binding(elxHBA_t * phba,
		    LPFC_NODELIST_t * ndlp, uint16_t index, uint16_t bindtype)
{
	LPFC_BINDLIST_t *blp;
	LPFCHBA_t *plhba;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	if ((blp = (LPFC_BINDLIST_t *) elx_mem_get(phba, MEM_BIND))) {
		memset((void *)blp, 0, sizeof (LPFC_BINDLIST_t));
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
		blp->nlp_sid = DEV_SID(index);
		blp->nlp_pan = DEV_PAN(index);
		blp->nlp_DID = ndlp->nlp_DID;
		blp->nlp_Target = plhba->device_queue_hash[index];
		memcpy(&blp->nlp_nodename, &ndlp->nlp_nodename,
		       sizeof (NAME_TYPE));
		memcpy(&blp->nlp_portname, &ndlp->nlp_portname,
		       sizeof (NAME_TYPE));

		return (blp);
	}

	return (0);
}

uint32_t
lpfc_add_bind(elxHBA_t * phba, uint8_t bind_type,	/* NN/PN/DID */
	      void *bind_id,	/* pointer to the bind id value */
	      uint32_t scsi_id)
{
	LPFC_NODELIST_t *ndlp;
	LPFCHBA_t *plhba;
	LPFC_BINDLIST_t *blp;
	ELXSCSITARGET_t *targetp;
	ELXSCSILUN_t *lunp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	/* Check if the SCSI ID is currently mapped */
	ndlp = lpfc_findnode_scsiid(phba, scsi_id);
	if (ndlp && (ndlp != &plhba->fc_fcpnodev)) {
		return ENOENT;
	}
	/* Check if the SCSI ID is currently in the bind list. */
	blp = plhba->fc_nlpbind_start;
	while ((blp) && (blp != (LPFC_BINDLIST_t *) & plhba->fc_nlpbind_start)) {
		if (blp->nlp_sid == scsi_id) {
			return ENOENT;
		}
		switch (bind_type) {
		case FCP_SEED_WWPN:
			if ((blp->nlp_bind_type & FCP_SEED_WWPN) &&
			    (lpfc_geportname(bind_id, &blp->nlp_portname) ==
			     2)) {
				return EBUSY;
			}
			break;
		case FCP_SEED_WWNN:
			if ((blp->nlp_bind_type & FCP_SEED_WWNN) &&
			    (lpfc_geportname(bind_id, &blp->nlp_nodename) ==
			     2)) {
				return EBUSY;
			}
			break;
		case FCP_SEED_DID:
			if ((blp->nlp_bind_type & FCP_SEED_DID) &&
			    (*((uint32_t *) bind_id) == blp->nlp_DID)) {
				return EBUSY;
			}
			break;
		}

		blp = (LPFC_BINDLIST_t *) blp->nlp_listp_next;
	}
	if (plhba->fcp_mapping != bind_type) {
		return EINVAL;
	}
	switch (bind_type) {
	case FCP_SEED_WWNN:
		{
			/* Check if the node name present in the mapped list */
			ndlp =
			    lpfc_findnode_wwnn(phba, NLP_SEARCH_MAPPED,
					       bind_id);
			if (ndlp) {
				return EBUSY;
			}
			ndlp =
			    lpfc_findnode_wwnn(phba, NLP_SEARCH_UNMAPPED,
					       bind_id);
			break;
		}
	case FCP_SEED_WWPN:
		{
			/* Check if the port name present in the mapped list */
			ndlp =
			    lpfc_findnode_wwpn(phba, NLP_SEARCH_MAPPED,
					       bind_id);
			if (ndlp)
				return EBUSY;
			ndlp =
			    lpfc_findnode_wwpn(phba, NLP_SEARCH_UNMAPPED,
					       bind_id);
			break;
		}
	case FCP_SEED_DID:
		{
			/* Check if the DID present in the mapped list */
			ndlp =
			    lpfc_findnode_did(phba, NLP_SEARCH_MAPPED,
					      *((uint32_t *) bind_id));
			if (ndlp)
				return EBUSY;
			ndlp =
			    lpfc_findnode_did(phba, NLP_SEARCH_UNMAPPED,
					      *((uint32_t *) bind_id));
			break;
		}
	}

	/* Add to the bind list */
	if ((blp = (LPFC_BINDLIST_t *) elx_mem_get(phba, MEM_BIND)) == NULL) {
		return EIO;
	}
	memset((void *)blp, 0, sizeof (LPFC_BINDLIST_t));
	blp->nlp_bind_type = bind_type;
	blp->nlp_sid = (scsi_id & 0xff);

	switch (bind_type) {
	case FCP_SEED_WWNN:
		memcpy(&blp->nlp_nodename, (uint8_t *) bind_id,
		       sizeof (NAME_TYPE));
		break;

	case FCP_SEED_WWPN:
		memcpy(&blp->nlp_portname, (uint8_t *) bind_id,
		       sizeof (NAME_TYPE));
		break;

	case FCP_SEED_DID:
		blp->nlp_DID = *((uint32_t *) bind_id);
		break;

	}

	lpfc_nlp_bind(phba, blp);
	/* 
	   If the newly added node is in the unmapped list, assign a
	   SCSI ID to the node.
	 */

	if (ndlp) {
		if ((blp = lpfc_assign_scsid(phba, ndlp))) {
			lpfc_nlp_mapped(phba, ndlp, blp);
			ndlp->nle.nlp_failMask = 0;
			targetp = ndlp->nlp_Target;
			if (targetp) {
				lunp =
				    (ELXSCSILUN_t *) (targetp->lunlist.q_first);
				while (lunp) {
					lunp->failMask = 0;
					lunp = lunp->pnextLun;
				}
				if (targetp->tmofunc) {
					elx_clk_can(phba, targetp->tmofunc);
					targetp->tmofunc = 0;
				}
			} else {
				/* new target to driver, allocate space to target <sid> lun 0 */
				if (blp->nlp_Target == 0) {
					lpfc_find_lun(phba, blp->nlp_sid, 0, 1);
					blp->nlp_Target =
					    plhba->device_queue_hash[blp->
								     nlp_sid];
				}
			}
			lpfc_set_failmask(phba, ndlp, ELX_DEV_RPTLUN,
					  ELX_SET_BITMASK);
			ndlp->nlp_state = NLP_STE_MAPPED_NODE;
			lpfc_disc_issue_rptlun(phba, ndlp);
		}
	}
	return (0);
}

uint32_t
lpfc_del_bind(elxHBA_t * phba, uint8_t bind_type,	/* NN/PN/DID */
	      void *bind_id,	/* pointer to the bind id value */
	      uint32_t scsi_id)
{
	LPFC_BINDLIST_t *blp;
	LPFCHBA_t *plhba;
	uint32_t found = 0;
	unsigned long iflag;
	LPFC_NODELIST_t *ndlp = 0;

	plhba = (LPFCHBA_t *) phba->pHbaProto;

	/* Search the mapped list for the bind_id */
	if (!bind_id) {
		ndlp = lpfc_findnode_scsiid(phba, scsi_id);
		if ((ndlp == &plhba->fc_fcpnodev) ||
		    (ndlp && (!(ndlp->nlp_flag & NLP_MAPPED_LIST))))
			ndlp = NULL;
	} else {

		if (bind_type != plhba->fcp_mapping)
			return EINVAL;

		switch (bind_type) {
		case FCP_SEED_WWNN:
			ndlp =
			    lpfc_findnode_wwnn(phba, NLP_SEARCH_MAPPED,
					       bind_id);
			break;

		case FCP_SEED_WWPN:
			ndlp =
			    lpfc_findnode_wwpn(phba, NLP_SEARCH_MAPPED,
					       bind_id);
			break;

		case FCP_SEED_DID:
			ndlp =
			    lpfc_findnode_did(phba, NLP_SEARCH_MAPPED,
					      *((uint32_t *) bind_id));
			break;
		}
	}

	/* If there is a mapped target for this bing unmap it */
	if (ndlp) {
		return EBUSY;
	}

	/* check binding list */
	blp = plhba->fc_nlpbind_start;

	/* Search the bind list for the bind_id */
	while ((blp) && (blp != (LPFC_BINDLIST_t *) & plhba->fc_nlpbind_start)) {
		if (!bind_id) {
			/* Search binding based on SCSI ID */
			if (blp->nlp_sid == scsi_id) {
				found = 1;
				break;
			} else {
				blp = blp->nlp_listp_next;
				continue;
			}
		}

		switch (bind_type) {
		case FCP_SEED_WWPN:
			if ((blp->nlp_bind_type & FCP_SEED_WWPN) &&
			    (lpfc_geportname(bind_id, &blp->nlp_portname) ==
			     2)) {
				found = 1;
			}
			break;
		case FCP_SEED_WWNN:
			if ((blp->nlp_bind_type & FCP_SEED_WWNN) &&
			    (lpfc_geportname(bind_id, &blp->nlp_nodename) ==
			     2)) {
				found = 1;
			}
			break;
		case FCP_SEED_DID:
			if ((blp->nlp_bind_type & FCP_SEED_DID) &&
			    (*((uint32_t *) bind_id) == blp->nlp_DID)) {
				found = 1;
			}
			break;
		}
		if (found)
			break;

		blp = blp->nlp_listp_next;
	}

	if (found) {
		/* take it off the bind list */
		ELX_DISC_LOCK(phba, iflag);
		plhba->fc_bind_cnt--;
		elx_deque(blp);
		ELX_DISC_UNLOCK(phba, iflag);

		return 0;
	}

	return ENOENT;
}

LPFC_BINDLIST_t *
lpfc_assign_scsid(elxHBA_t * phba, LPFC_NODELIST_t * ndlp)
{
	LPFCHBA_t *plhba;
	LPFC_BINDLIST_t *blp;
	elxCfgParam_t *clp;
	uint16_t index;
	unsigned long iflag;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	clp = &phba->config[0];

	/* check binding list */
	blp = plhba->fc_nlpbind_start;
	while ((blp) && (blp != (LPFC_BINDLIST_t *) & plhba->fc_nlpbind_start)) {
		if (lpfc_binding_found(blp, ndlp)) {
			ndlp->nlp_pan = blp->nlp_pan;
			ndlp->nlp_sid = blp->nlp_sid;
			ndlp->nlp_Target = blp->nlp_Target;
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

			/* take it off the binding list */
			ELX_DISC_LOCK(phba, iflag);
			plhba->fc_bind_cnt--;
			elx_deque(blp);
			ELX_DISC_UNLOCK(phba, iflag);

			/* Reassign scsi id <sid> to NPort <nlp_DID> */
			elx_printf_log(phba->brd_no, &elx_msgBlk0213,	/* ptr to msg structure */
				       elx_mes0213,	/* ptr to msg */
				       elx_msgBlk0213.msgPreambleStr,	/* begin varargs */
				       blp->nlp_sid, ndlp->nlp_DID, blp->nlp_bind_type, ndlp->nlp_flag, ndlp->nlp_state, ndlp->nle.nlp_rpi);	/* end varargs */

			return (blp);
		}

		blp = blp->nlp_listp_next;
	}

	/* NOTE: if scan-down = 2 and we have private loop, then we use
	 * AlpaArray to determine sid/pan.
	 */
	if ((clp[LPFC_CFG_BINDMETHOD].a_current == 4) &&
	    ((plhba->fc_flag & (FC_PUBLIC_LOOP | FC_FABRIC)) ||
	     (plhba->fc_topology != TOPOLOGY_LOOP))) {
		/* Log message: ALPA based binding used on a non loop topology */
		elx_printf_log(phba->brd_no, &elx_msgBlk0245,	/* ptr to msg structure */
			       elx_mes0245,	/* ptr to msg */
			       elx_msgBlk0245.msgPreambleStr,	/* begin varargs */
			       plhba->fc_topology);	/* end varargs */
	}

	if ((clp[LPFC_CFG_BINDMETHOD].a_current == 4) &&
	    !(plhba->fc_flag & (FC_PUBLIC_LOOP | FC_FABRIC)) &&
	    (plhba->fc_topology == TOPOLOGY_LOOP)) {
		for (index = 0; index < FC_MAXLOOP; index++) {
			if (ndlp->nlp_DID == (uint32_t) lpfcAlpaArray[index]) {
				if ((blp =
				     lpfc_create_binding(phba, ndlp, index,
							 FCP_SEED_DID))) {

					ndlp->nlp_pan = DEV_PAN(index);
					ndlp->nlp_sid = DEV_SID(index);
					ndlp->nlp_Target =
					    plhba->device_queue_hash[index];
					ndlp->nlp_flag &= ~NLP_SEED_MASK;
					ndlp->nlp_flag |= NLP_SEED_DID;
					ndlp->nlp_flag |= NLP_SEED_ALPA;

					/* Assign scandown scsi id <sid> to NPort <nlp_DID> */
					elx_printf_log(phba->brd_no, &elx_msgBlk0216,	/* ptr to msg structure */
						       elx_mes0216,	/* ptr to msg */
						       elx_msgBlk0216.msgPreambleStr,	/* begin varargs */
						       blp->nlp_sid, ndlp->nlp_DID, blp->nlp_bind_type, ndlp->nlp_flag, ndlp->nlp_state, ndlp->nle.nlp_rpi);	/* end varargs */

					return (blp);
				}
				goto errid;
			}
		}
	}

	if (clp[LPFC_CFG_AUTOMAP].a_current) {
		while (1) {
			if ((lpfc_binding_useid
			     (phba, plhba->pan_cnt, plhba->sid_cnt))
			    ||
			    (lpfc_mapping_useid
			     (phba, plhba->pan_cnt, plhba->sid_cnt))) {

				plhba->sid_cnt++;
				if (plhba->sid_cnt > LPFC_MAX_SCSI_ID_PER_PAN) {
					plhba->sid_cnt = 0;
					plhba->pan_cnt++;
				}
			} else {
				if ((blp =
				     lpfc_create_binding(phba, ndlp,
							 plhba->sid_cnt,
							 plhba->fcp_mapping))) {
					ndlp->nlp_pan = blp->nlp_pan;
					ndlp->nlp_sid = blp->nlp_sid;
					ndlp->nlp_Target = blp->nlp_Target;
					ndlp->nlp_flag &= ~NLP_SEED_MASK;
					switch ((blp->
						 nlp_bind_type & FCP_SEED_MASK))
					{
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
					blp->nlp_bind_type |= FCP_SEED_AUTO;
					ndlp->nlp_flag |= NLP_AUTOMAP;

					plhba->sid_cnt++;
					if (plhba->sid_cnt >
					    LPFC_MAX_SCSI_ID_PER_PAN) {
						plhba->sid_cnt = 0;
						plhba->pan_cnt++;
					}

					/* Assign scsi id <sid> to NPort <nlp_DID> */
					elx_printf_log(phba->brd_no, &elx_msgBlk0229,	/* ptr to msg structure */
						       elx_mes0229,	/* ptr to msg */
						       elx_msgBlk0229.msgPreambleStr,	/* begin varargs */
						       blp->nlp_sid, ndlp->nlp_DID, blp->nlp_bind_type, ndlp->nlp_flag, ndlp->nlp_state, ndlp->nle.nlp_rpi);	/* end varargs */

					return (blp);
				}
				goto errid;
			}
		}
	}
	/* if automap on */
      errid:
	/* Cannot assign scsi id on NPort <nlp_DID> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0230,	/* ptr to msg structure */
		       elx_mes0230,	/* ptr to msg */
		       elx_msgBlk0230.msgPreambleStr,	/* begin varargs */
		       ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state, ndlp->nle.nlp_rpi);	/* end varargs */

	return (0);
}

void
lpfc_qthrottle_up(elxHBA_t * phba, void *n1, void *n2)
{
	LPFCHBA_t *plhba;
	LPFC_NODELIST_t *ndlp;
	ELXSCSITARGET_t *ptarget;
	ELXSCSILUN_t *plun;
	elxCfgParam_t *clp;
	int reset_clock = 0;

	clp = &phba->config[0];
	if (clp[ELX_CFG_DFT_LUN_Q_DEPTH].a_current <= ELX_MIN_QFULL) {
		return;
	}

	if (phba->hba_state != ELX_HBA_READY) {
		plhba = (LPFCHBA_t *) phba->pHbaProto;
		ndlp = plhba->fc_nlpmap_start;
		while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start) {
			ptarget = ndlp->nlp_Target;
			if (ptarget) {
				plun =
				    (ELXSCSILUN_t *) ptarget->lunlist.q_first;
				while (plun) {
					plun->lunSched.maxOutstanding =
					    plun->fcp_lun_queue_depth;
					plun->stop_send_io = 0;
					plun = plun->pnextLun;
				}
			}
			ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
		}
		return;
	}

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	ndlp = plhba->fc_nlpmap_start;
	while (ndlp != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start) {
		ptarget = ndlp->nlp_Target;
		if (ptarget) {
			plun = (ELXSCSILUN_t *) ptarget->lunlist.q_first;
			while (plun) {
				if ((plun->stop_send_io == 0) &&
				    (plun->lunSched.maxOutstanding <
				     plun->fcp_lun_queue_depth)) {
					/* 
					 * update lun q throttle 
					 */
					plun->lunSched.maxOutstanding +=
					    clp[ELX_CFG_DQFULL_THROTTLE_UP_INC].
					    a_current;
					if (plun->lunSched.maxOutstanding >
					    plun->fcp_lun_queue_depth) {
						plun->lunSched.maxOutstanding =
						    plun->fcp_lun_queue_depth;
					}
					elx_printf
					    ("lpfc_qthrottle_up: maxOutstanding=%d %x %x %x",
					     plun->lunSched.maxOutstanding, 0,
					     0, 0);
					reset_clock = 1;
				} else {
					/* 
					 * Try to reset stop_send_io 
					 */
					if (plun->stop_send_io) {
						plun->stop_send_io--;
						reset_clock = 1;
					}
				}
				plun = plun->pnextLun;
			}
		}
		ndlp = (LPFC_NODELIST_t *) ndlp->nle.nlp_listp_next;
	}

	if (reset_clock) {
		phba->dqfull_clk = elx_clk_set(phba,
					       clp
					       [ELX_CFG_DQFULL_THROTTLE_UP_TIME].
					       a_current, lpfc_qthrottle_up, 0,
					       0);
	}

	return;
}

void
lpfc_npr_timeout(elxHBA_t * phba, void *l1, void *l2)
{
	ELXSCSITARGET_t *targetp;

	targetp = (ELXSCSITARGET_t *) l1;
	targetp->targetFlags &= ~FC_NPR_ACTIVE;
	targetp->tmofunc = 0;
	elx_sched_flush_target(phba, targetp, IOSTAT_DRIVER_REJECT,
			       IOERR_SLI_ABORTED);
	return;
}

int
lpfc_scsi_hba_reset(elxHBA_t * phba, ELX_SCSI_BUF_t * elx_cmd)
{
	ELXSCSITARGET_t *ptarget;
	LPFCHBA_t *plhba;
	int ret;
	int i;

	plhba = (LPFCHBA_t *) phba->pHbaProto;

	for (i = 0; i < MAX_FCP_TARGET; i++) {
		ptarget = plhba->device_queue_hash[i];
		if (ptarget) {
			elx_cmd->scsi_hba = phba;
			elx_cmd->scsi_bus = 0;
			elx_cmd->scsi_target = i;
			elx_cmd->scsi_lun = 0;

			ret = elx_scsi_tgt_reset(elx_cmd,
						 phba,
						 0, i, ELX_EXTERNAL_RESET);
			if (!ret) {
				return (0);
			}
		}
	}

	return (1);
}

ELX_SCSI_BUF_t *
lpfc_build_scsi_cmd(elxHBA_t * phba,
		    LPFC_NODELIST_t * nlp, uint32_t scsi_cmd, uint64_t lun)
{
	ELX_SLI_t *psli;
	LPFCHBA_t *plhba;
	ELXSCSITARGET_t *targetp;
	ELXSCSILUN_t *lunp;
	DMABUF_t *mp;
	ELX_SCSI_BUF_t *elx_cmd;
	ELX_IOCBQ_t *piocbq;
	IOCB_t *piocb;
	FCP_CMND *fcpCmnd;
	ULP_BDE64 *bpl;
	uint32_t tgt;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	tgt = FC_SCSID(nlp->nlp_pan, nlp->nlp_sid);
	lunp = lpfc_find_lun(phba, tgt, lun, 1);
	elx_cmd = 0;
	/* First see if the SCSI ID has an allocated ELXSCSITARGET_t */
	if (lunp && lunp->pTarget) {
		targetp = lunp->pTarget;
		psli = &phba->sli;

		/* Get a buffer to hold SCSI data */
		if ((mp = (DMABUF_t *) elx_mem_get(phba, MEM_BUF)) == 0) {
			return (0);
		}
		/* Get resources to send a SCSI command */
		elx_cmd = elx_get_scsi_buf(phba);
		if (elx_cmd == 0) {
			elx_mem_put(phba, MEM_BUF, (uint8_t *) mp);
			return (0);
		}
		elx_cmd->pLun = lunp;
		elx_cmd->scsi_target = tgt;
		elx_cmd->scsi_lun = lun;
		elx_cmd->timeout = 30 + phba->fcp_timeout_offset;

		/* Finish building BPL with the I/O dma ptrs.
		 * setup FCP CMND, and setup IOCB.
		 */

		fcpCmnd = elx_cmd->fcp_cmnd;

		putLunHigh(fcpCmnd->fcpLunMsl, lun);	/* LUN */
		putLunLow(fcpCmnd->fcpLunLsl, lun);	/* LUN */

		switch (scsi_cmd) {
		case FCP_SCSI_REPORT_LUNS:
			fcpCmnd->fcpCdb[0] = scsi_cmd;
			fcpCmnd->fcpCdb[8] = 0x04;	/* 0x400 = ELX_SCSI_BUF_SZ */
			fcpCmnd->fcpCdb[9] = 0x00;
			fcpCmnd->fcpCntl3 = READ_DATA;
			fcpCmnd->fcpDl = SWAP_DATA(ELX_SCSI_BUF_SZ);

			break;
		case FCP_SCSI_INQUIRY:
			fcpCmnd->fcpCdb[0] = scsi_cmd;	/* SCSI Inquiry Command */
			fcpCmnd->fcpCdb[4] = 0xff;	/* allocation length */
			fcpCmnd->fcpCntl3 = READ_DATA;
			fcpCmnd->fcpDl = SWAP_DATA(ELX_SCSI_BUF_SZ);
			break;
		}

		bpl = elx_cmd->fcp_bpl;
		bpl += 2;	/* Bump past FCP CMND and FCP RSP */

		/* no scatter-gather list case */
		bpl->addrLow = PCIMEM_LONG(putPaddrLow(mp->phys));
		bpl->addrHigh = PCIMEM_LONG(putPaddrHigh(mp->phys));
		bpl->tus.f.bdeSize = ELX_SCSI_BUF_SZ;
		bpl->tus.f.bdeFlags = BUFF_USE_RCV;
		bpl->tus.w = PCIMEM_LONG(bpl->tus.w);
		bpl++;
		bpl->addrHigh = 0;
		bpl->addrLow = 0;
		bpl->tus.w = 0;

		piocbq = &elx_cmd->cur_iocbq;
		piocb = &piocbq->iocb;
		piocb->ulpCommand = CMD_FCP_IREAD64_CR;
		piocb->ulpPU = PARM_READ_CHECK;
		piocb->un.fcpi.fcpi_parm = ELX_SCSI_BUF_SZ;
		piocb->un.fcpi64.bdl.bdeSize += sizeof (ULP_BDE64);
		piocb->ulpBdeCount = 1;
		piocb->ulpLe = 1;	/* Set the LE bit in the iocb */

		/* Get an iotag and finish setup of IOCB  */
		piocb->ulpIoTag = elx_sli_next_iotag(phba,
						     &phba->sli.ring[psli->
								     fcp_ring]);
		piocb->ulpContext = nlp->nle.nlp_rpi;
		if (nlp->nle.nlp_fcp_info & NLP_FCP_2_DEVICE) {
			piocb->ulpFCP2Rcvy = 1;
		}
		piocb->ulpClass = (nlp->nle.nlp_fcp_info & 0x0f);

		/* ulpTimeout is only one byte */
		if (elx_cmd->timeout > 0xff) {
			/*
			 * Do not timeout the command at the firmware level.
			 * The driver will provide the timeout mechanism.
			 */
			piocb->ulpTimeout = 0;
		} else {
			piocb->ulpTimeout = elx_cmd->timeout;
		}

		/*
		 * Setup driver timeout, in case the command does not complete
		 * Driver timeout should be greater than ulpTimeout
		 */

		piocbq->drvrTimeout = elx_cmd->timeout + ELX_DRVR_TIMEOUT;

		/* set up iocb return path by setting the context fields
		 * and the completion function.
		 */
		piocbq->context1 = elx_cmd;
		piocbq->context2 = mp;

	}
	return (elx_cmd);
}

void
lpfc_scsi_timeout_handler(elxHBA_t * phba, void *arg1, void *arg2)
{
	ELX_SLI_t *psli;
	ELX_SLI_RING_t *pring;
	ELX_IOCBQ_t *next_iocb;
	ELX_IOCBQ_t *piocb;
	IOCB_t *cmd = NULL;
	LPFCHBA_t *plhba;
	ELX_SCSI_BUF_t *elx_cmd;
	uint32_t timeout;
	uint32_t next_timeout;

	psli = &phba->sli;
	pring = &psli->ring[psli->fcp_ring];
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	timeout = (uint32_t) (unsigned long)arg1;
	next_timeout = (plhba->fc_ratov << 1) > 5 ? (plhba->fc_ratov << 1) : 5;

	next_iocb = (ELX_IOCBQ_t *) pring->txcmplq.q_f;
	while (next_iocb != (ELX_IOCBQ_t *) & pring->txcmplq) {
		piocb = next_iocb;
		next_iocb = next_iocb->q_f;
		cmd = &piocb->iocb;
		elx_cmd = (ELX_SCSI_BUF_t *) piocb->context1;

		if (piocb->iocb_flag & (ELX_IO_IOCTL | ELX_IO_POLL)) {
			continue;
		}

		if (piocb->drvrTimeout) {
			if (piocb->drvrTimeout > timeout)
				piocb->drvrTimeout -= timeout;
			else
				piocb->drvrTimeout = 0;

			continue;
		}

		/*
		 * The iocb has timed out; abort it.
		 */

		if (cmd->un.acxri.abortType == ABORT_TYPE_ABTS) {
			/*
			 * If abort times out, simply throw away the iocb
			 */

			elx_deque(piocb);
			pring->txcmplq.q_cnt--;
			(piocb->iocb_cmpl) ((void *)phba, piocb, piocb);
		} else {
			elx_printf_log(phba->brd_no, &elx_msgBlk0754,	/* ptr to msg structure */
				       elx_mes0754,	/* ptr to msg */
				       elx_msgBlk0754.msgPreambleStr,	/* begin varargs */
				       elx_cmd->pLun->pTarget->un.dev_did, elx_cmd->pLun->pTarget->scsi_id, elx_cmd->fcp_cmnd->fcpCdb[0], cmd->ulpIoTag);	/* end varargs */

			elx_sli_abort_iocb(phba, pring, piocb);
		}
	}

	phba->scsi_tmofunc =
	    elx_clk_set(phba, next_timeout, lpfc_scsi_timeout_handler,
			(void *)(unsigned long)next_timeout, 0);
}
