/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2007-2008 Emulex.  All rights reserved.                *
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

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport_fc.h>

#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_version.h"
#include "lpfc_compat.h"
#include "lpfc_crtn.h"
#include "lpfc_vport.h"

#define MENLO_CMD_FW_DOWNLOAD                   0x00000002
#define MENLO_CMD_LOOPBACK                   	0x00000014

static void lpfc_menlo_iocb_timeout_cmpl(struct lpfc_hba *,
			struct lpfc_iocbq *, struct lpfc_iocbq *);

extern int
__dfc_cmd_data_free(struct lpfc_hba * phba, struct lpfc_dmabufext * mlist);

extern struct lpfc_dmabufext *
__dfc_cmd_data_alloc(struct lpfc_hba * phba,
		   char *indataptr, struct ulp_bde64 * bpl, uint32_t size,
		   int nocopydata);
/*
 * The size for the menlo interface is set at 336k because it only uses
 * one bpl. A bpl can contain 85 BDE descriptors. Each BDE can represent
 * up to 4k. I used 84 BDE entries to do this calculation because the
 * 1st sysfs_menlo_write is for just the cmd header which is 12 bytes.
 * size = PAGE_SZ * (sizeof(bpl) / sizeof(BDE)) -1;
 */
#define SYSFS_MENLO_ATTR_SIZE 344064
typedef struct menlo_get_cmd
{
	uint32_t code;          /* Command code */
	uint32_t context;       /* Context */
	uint32_t length;        /* Max response length */
} menlo_get_cmd_t;

typedef struct menlo_init_rsp
{
	uint32_t code;
	uint32_t bb_credit;     /* Menlo FC BB Credit */
	uint32_t frame_size;    /* Menlo FC receive frame size */
	uint32_t fw_version;    /* Menlo firmware version   */
	uint32_t reset_status;  /* Reason for previous reset */

#define MENLO_RESET_STATUS_NORMAL               0
#define MENLO_RESET_STATUS_PANIC                1

	uint32_t maint_status;  /* Menlo Maintenance Mode status at link up */


#define MENLO_MAINTENANCE_MODE_DISABLE  0
#define MENLO_MAINTENANCE_MODE_ENABLE   1
	uint32_t fw_type;
	uint32_t fru_data_valid; /* 0=invalid, 1=valid */
} menlo_init_rsp_t;

#define MENLO_CMD_GET_INIT 0x00000007
#define MENLO_FW_TYPE_OPERATIONAL 0xABCD0001
#define MENLO_FW_TYPE_GOLDEN    0xABCD0002
#define MENLO_FW_TYPE_DIAG      0xABCD0003

void
BE_swap32_buffer(void *srcp, uint32_t cnt)
{
	uint32_t *src = srcp;
	uint32_t *dest = srcp;
	uint32_t ldata;
	int i;

	for (i = 0; i < (int)cnt; i += sizeof (uint32_t)) {
		ldata = *src;
		ldata = cpu_to_le32(ldata);
		*dest = ldata;
		src++;
		dest++;
	}
}


static int
lpfc_alloc_menlo_genrequest64(struct lpfc_hba * phba,
			struct lpfc_menlo_genreq64 *sysfs_menlo,
			struct lpfc_sysfs_menlo_hdr *cmdhdr)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(phba->pport);
	struct ulp_bde64 *bpl = NULL;
	IOCB_t *cmd = NULL, *rsp = NULL;
	struct lpfc_sli *psli = NULL;
	struct lpfc_sli_ring *pring = NULL;
	int rc = 0;
	uint32_t cmdsize;
	uint32_t rspsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];

	if (!(psli->sli_flag & LPFC_SLI2_ACTIVE)) {
		rc = EACCES;
		goto send_menlomgmt_cmd_exit;
	}

	if (!sysfs_menlo) {
		rc = EINVAL;
		goto send_menlomgmt_cmd_exit;
	}

	cmdsize = cmdhdr->cmdsize;
	rspsize = cmdhdr->rspsize;

	if (!cmdsize || !rspsize || (cmdsize + rspsize > 80 * BUF_SZ_4K)) {
		rc = ERANGE;
		goto send_menlomgmt_cmd_exit;
	}

	spin_lock_irq(shost->host_lock);
	sysfs_menlo->cmdiocbq = lpfc_sli_get_iocbq(phba);
	if (!sysfs_menlo->cmdiocbq) {
		rc = ENOMEM;
		spin_unlock_irq(shost->host_lock);
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"1202 alloc_menlo_genreq64: couldn't alloc cmdiocbq\n");
		goto send_menlomgmt_cmd_exit;
	}
	cmd = &sysfs_menlo->cmdiocbq->iocb;

	sysfs_menlo->rspiocbq = lpfc_sli_get_iocbq(phba);
	if (!sysfs_menlo->rspiocbq) {
		rc = ENOMEM;
		spin_unlock_irq(shost->host_lock);
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"1203 alloc_menlo_genreq64: couldn't alloc rspiocbq\n");
		goto send_menlomgmt_cmd_exit;
	}
	spin_unlock_irq(shost->host_lock);

	rsp = &sysfs_menlo->rspiocbq->iocb;


	sysfs_menlo->bmp = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);
	if (!sysfs_menlo->bmp) {
		rc = ENOMEM;
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"1204 alloc_menlo_genreq64: couldn't alloc bmp\n");
		goto send_menlomgmt_cmd_exit;
	}

	spin_lock_irq(shost->host_lock);
	sysfs_menlo->bmp->virt = lpfc_mbuf_alloc(phba, 0,
					&sysfs_menlo->bmp->phys);
	if (!sysfs_menlo->bmp->virt) {
		rc = ENOMEM;
		spin_unlock_irq(shost->host_lock);
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"1205 alloc_menlo_genreq64: couldn't alloc bpl\n");
		goto send_menlomgmt_cmd_exit;
	}
	spin_unlock_irq(shost->host_lock);

	INIT_LIST_HEAD(&sysfs_menlo->bmp->list);
	bpl = (struct ulp_bde64 *) sysfs_menlo->bmp->virt;
	memset((uint8_t*)bpl, 0 , 1024);
	sysfs_menlo->indmp = __dfc_cmd_data_alloc(phba, NULL, bpl, cmdsize, 1);
	if (!sysfs_menlo->indmp) {
		rc = ENOMEM;
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"1206 alloc_menlo_genreq64: couldn't alloc cmdbuf\n");
		goto send_menlomgmt_cmd_exit;
	}
	sysfs_menlo->cmdbpl = bpl;
	INIT_LIST_HEAD(&sysfs_menlo->inhead);
	list_add_tail(&sysfs_menlo->inhead, &sysfs_menlo->indmp->dma.list);

	/* flag contains total number of BPLs for xmit */

	bpl += sysfs_menlo->indmp->flag;

	sysfs_menlo->outdmp = __dfc_cmd_data_alloc(phba, NULL, bpl, rspsize, 0);
	if (!sysfs_menlo->outdmp) {
		rc = ENOMEM;
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"1207 alloc_menlo_genreq64: couldn't alloc rspbuf\n");
		goto send_menlomgmt_cmd_exit;
	}
	INIT_LIST_HEAD(&sysfs_menlo->outhead);
	list_add_tail(&sysfs_menlo->outhead, &sysfs_menlo->outdmp->dma.list);

	cmd->un.genreq64.bdl.ulpIoTag32 = 0;
	cmd->un.genreq64.bdl.addrHigh = putPaddrHigh(sysfs_menlo->bmp->phys);
	cmd->un.genreq64.bdl.addrLow = putPaddrLow(sysfs_menlo->bmp->phys);
	cmd->un.genreq64.bdl.bdeFlags = BUFF_TYPE_BLP_64;
	cmd->un.genreq64.bdl.bdeSize =
	    (sysfs_menlo->outdmp->flag + sysfs_menlo->indmp->flag)
		* sizeof(struct ulp_bde64);
	cmd->ulpCommand = CMD_GEN_REQUEST64_CR;
	cmd->un.genreq64.w5.hcsw.Fctl = (SI | LA);
	cmd->un.genreq64.w5.hcsw.Dfctl = 0;
	cmd->un.genreq64.w5.hcsw.Rctl = FC_FCP_CMND;
	cmd->un.genreq64.w5.hcsw.Type = MENLO_TRANSPORT_TYPE; /* 0xfe */
	cmd->un.ulpWord[4] = MENLO_DID; /* 0x0000FC0E */
	cmd->ulpBdeCount = 1;
	cmd->ulpClass = CLASS3;
	cmd->ulpContext = MENLO_CONTEXT; /* 0 */
	cmd->ulpOwner = OWN_CHIP;
	cmd->ulpPU = MENLO_PU; /* 3 */
	cmd->ulpLe = 1; /* Limited Edition */
	sysfs_menlo->cmdiocbq->vport = phba->pport;
	sysfs_menlo->cmdiocbq->context1 = NULL;
	sysfs_menlo->cmdiocbq->iocb_flag |= LPFC_IO_LIBDFC;
	/* We want the firmware to timeout before we do */
	cmd->ulpTimeout = MENLO_TIMEOUT - 5;

	sysfs_menlo->timeout = cmd->ulpTimeout;

send_menlomgmt_cmd_exit:
	return rc;
}

void
sysfs_menlo_genreq_free(struct lpfc_hba *phba,
		struct lpfc_menlo_genreq64 *sysfs_menlo)
{
	if ( !list_empty(&sysfs_menlo->outhead))
		list_del_init( &sysfs_menlo->outhead);

	if (!list_empty(&sysfs_menlo->inhead))
		list_del_init( &sysfs_menlo->inhead);

	if (sysfs_menlo->outdmp) {
		__dfc_cmd_data_free(phba, sysfs_menlo->outdmp);
		sysfs_menlo->outdmp = NULL;
	}
	if (sysfs_menlo->indmp) {
		__dfc_cmd_data_free(phba, sysfs_menlo->indmp);
		sysfs_menlo->indmp = NULL;
	}
	if (sysfs_menlo->bmp) {
		lpfc_mbuf_free(phba, sysfs_menlo->bmp->virt,
				sysfs_menlo->bmp->phys);
		kfree(sysfs_menlo->bmp);
		sysfs_menlo->bmp = NULL;
	}
	if (sysfs_menlo->rspiocbq) {
		lpfc_sli_release_iocbq(phba, sysfs_menlo->rspiocbq);
		sysfs_menlo->rspiocbq = NULL;
	}

	if (sysfs_menlo->cmdiocbq) {
		lpfc_sli_release_iocbq(phba, sysfs_menlo->cmdiocbq);
		sysfs_menlo->cmdiocbq = NULL;
	}
}

static void
sysfs_menlo_idle(struct lpfc_hba *phba,
		struct lpfc_sysfs_menlo *sysfs_menlo)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(phba->pport);

	spin_lock_irq(&phba->hbalock);
	list_del_init(&sysfs_menlo->list);
	spin_unlock_irq(&phba->hbalock);
	spin_lock_irq(shost->host_lock);

	if (sysfs_menlo->cr.cmdiocbq)
		sysfs_menlo_genreq_free(phba, &sysfs_menlo->cr);
	if (sysfs_menlo->cx.cmdiocbq)
		sysfs_menlo_genreq_free(phba, &sysfs_menlo->cx);

	spin_unlock_irq(shost->host_lock);
	kfree(sysfs_menlo);
}

static void
lpfc_menlo_iocb_timeout_cmpl(struct lpfc_hba *phba,
					struct lpfc_iocbq *cmdq,
					struct lpfc_iocbq *rspq)
{
	lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
		"1241 Menlo IOCB timeout: deleting %p\n",
		cmdq->context3);
	sysfs_menlo_idle(phba, (struct lpfc_sysfs_menlo *)cmdq->context3);
}

static void
lpfc_menlo_iocb_cmpl(struct lpfc_hba *phba,
					struct lpfc_iocbq *cmdq,
					struct lpfc_iocbq *rspq)
{
	struct lpfc_sysfs_menlo * sysfs_menlo =
		(struct lpfc_sysfs_menlo *)cmdq->context2;
	struct lpfc_dmabufext *mlast = NULL;
	IOCB_t *rsp = NULL;
	IOCB_t *cmd = NULL;
	uint32_t * tmpptr = NULL;
	menlo_init_rsp_t *mlorsp = NULL;

	lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
		"1254 Menlo IOCB complete: %p\n",
		cmdq->context2);
	rsp = &rspq->iocb;
	cmd = &cmdq->iocb;
	if ( !sysfs_menlo ) {
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"1255 Menlo IOCB complete:NULL CTX \n");
		return;
	}
	if ( rsp->ulpStatus ) {
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"1242 iocb async cmpl: ulpStatus 0x%x "
			"ulpWord[4] 0x%x\n",
			rsp->ulpStatus, rsp->un.ulpWord[4]);
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"1260 cr:%.08x %.08x %.08x %.08x "
			"%.08x %.08x %.08x %.08x\n",
			cmd->un.ulpWord[0], cmd->un.ulpWord[1],
			cmd->un.ulpWord[2], cmd->un.ulpWord[3],
			cmd->un.ulpWord[4], cmd->un.ulpWord[5],
			*(uint32_t *)&cmd->un1, *((uint32_t *)&cmd->un1 + 1));
		mlast = list_get_first(&sysfs_menlo->cr.inhead,
				struct lpfc_dmabufext,
				dma.list);
		if (!mlast) {
			lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
				"1231 bad bpl:\n");
			goto lpfc_menlo_iocb_cmpl_ext;
		}
		tmpptr = ( uint32_t *) mlast->dma.virt;
		BE_swap32_buffer ((uint8_t *) tmpptr,
			sizeof( menlo_get_cmd_t));
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"1261 cmd:%.08x %.08x %.08x\n",
			*tmpptr, *(tmpptr+1), *(tmpptr+2));
		goto lpfc_menlo_iocb_cmpl_ext;
	}

	mlast = list_get_first(&sysfs_menlo->cr.outhead,
				struct lpfc_dmabufext,
				dma.list);
	if (!mlast) {
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"1256 bad bpl:\n");
		goto lpfc_menlo_iocb_cmpl_ext;
	}
	mlorsp = ( menlo_init_rsp_t *) mlast->dma.virt;
	BE_swap32_buffer ((uint8_t *) mlorsp,
		sizeof( menlo_init_rsp_t));

	if (mlorsp->code != 0) {
		lpfc_printf_log (phba, KERN_ERR, LOG_LINK_EVENT,
			"1243 Menlo command error. code=%d.\n", mlorsp->code);
		goto lpfc_menlo_iocb_cmpl_ext;

	}

	switch (mlorsp->fw_type)
	{
	case MENLO_FW_TYPE_OPERATIONAL:	/* Menlo Operational */
		break;
	case MENLO_FW_TYPE_GOLDEN:	/* Menlo Golden */
		lpfc_printf_log (phba, KERN_ERR, LOG_LINK_EVENT,
			"1246 FCoE chip is running golden firmware. "
			"Update FCoE chip firmware immediately %x\n",
			mlorsp->fw_type);
		break;
	case MENLO_FW_TYPE_DIAG:	/* Menlo Diag */
		lpfc_printf_log (phba, KERN_ERR, LOG_LINK_EVENT,
			"1247 FCoE chip is running diagnostic "
			"firmware. Operational use suspended. %x\n",
			mlorsp->fw_type);
		break;
	default:
		lpfc_printf_log (phba, KERN_ERR, LOG_LINK_EVENT,
			"1248 FCoE chip is running unknown "
			"firmware x%x.\n", mlorsp->fw_type);
		break;
	}
	if (!mlorsp->fru_data_valid
		&& (mlorsp->fw_type == MENLO_FW_TYPE_OPERATIONAL)
		&& (!mlorsp->maint_status))
		lpfc_printf_log (phba, KERN_ERR, LOG_LINK_EVENT,
			"1249 Invalid FRU data found on adapter."
			"Return adapter to Emulex for repair\n");

lpfc_menlo_iocb_cmpl_ext:
	sysfs_menlo_idle(phba, (struct lpfc_sysfs_menlo *)cmdq->context2);
}

static struct lpfc_sysfs_menlo *
lpfc_get_sysfs_menlo(struct lpfc_hba *phba, uint8_t create)
{
	struct lpfc_sysfs_menlo *sysfs_menlo;
	pid_t pid;

	pid = current->pid;

	spin_lock_irq(&phba->hbalock);
	list_for_each_entry(sysfs_menlo, &phba->sysfs_menlo_list, list) {
		if (sysfs_menlo->pid == pid) {
			spin_unlock_irq(&phba->hbalock);
			return sysfs_menlo;
		}
	}
	if (!create) {
		spin_unlock_irq(&phba->hbalock);
		return NULL;
	}
	spin_unlock_irq(&phba->hbalock);
	sysfs_menlo = kzalloc(sizeof(struct lpfc_sysfs_menlo),
			GFP_KERNEL);
	if (!sysfs_menlo)
		return NULL;
	sysfs_menlo->state = SMENLO_IDLE;
	sysfs_menlo->pid = pid;
	spin_lock_irq(&phba->hbalock);
	list_add_tail(&sysfs_menlo->list, &phba->sysfs_menlo_list);

	spin_unlock_irq(&phba->hbalock);
	return sysfs_menlo;

}

static ssize_t
lpfc_menlo_write(struct lpfc_hba *phba,
		 char *buf, loff_t off, size_t count)
{
	struct lpfc_sysfs_menlo *sysfs_menlo;
	struct lpfc_dmabufext *mlast = NULL;
	struct lpfc_sysfs_menlo_hdr cmdhdrCR;
	struct lpfc_menlo_genreq64 *genreq = NULL;
	loff_t temp_off = 0;
	struct ulp_bde64 *bpl = NULL;
	int mlastcnt = 0;
	uint32_t * tmpptr = NULL;
	uint32_t addr_high = 0;
	uint32_t addr_low = 0;
	int hdr_offset = sizeof(struct lpfc_sysfs_menlo_hdr);

	if (off % 4 ||  count % 4 || (unsigned long)buf % 4)
		return -EINVAL;

	if (count == 0)
		return 0;

	if (off == 0) {
		ssize_t rc;
		struct lpfc_sysfs_menlo_hdr *cmdhdr =
			(struct lpfc_sysfs_menlo_hdr *)buf;
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"1208 menlo_write: cmd %x cmdsz %d rspsz %d\n",
				cmdhdr->cmd, cmdhdr->cmdsize,
				cmdhdr->rspsize);
		if (count != sizeof(struct lpfc_sysfs_menlo_hdr)) {
			lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
				"1210 Invalid cmd size: cmd %x "
				"cmdsz %d rspsz %d\n",
				cmdhdr->cmd, cmdhdr->cmdsize,
				cmdhdr->rspsize);
			return -EINVAL;
		}

		sysfs_menlo = lpfc_get_sysfs_menlo(phba, 1);
		if (!sysfs_menlo)
			return -ENOMEM;
		sysfs_menlo->cmdhdr = *cmdhdr;
		if (cmdhdr->cmd == MENLO_CMD_FW_DOWNLOAD) {
			sysfs_menlo->cmdhdr.cmdsize
				-= sizeof(struct lpfc_sysfs_menlo_hdr);

			rc = lpfc_alloc_menlo_genrequest64(phba,
					&sysfs_menlo->cx,
					&sysfs_menlo->cmdhdr);
			if (rc != 0) {
				lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
					"1211 genreq alloc failed: %d\n",
					(int) rc);
				sysfs_menlo_idle(phba,sysfs_menlo);
				return -ENOMEM;
			}
			cmdhdrCR.cmd = cmdhdr->cmd;
			cmdhdrCR.cmdsize = sizeof(struct lpfc_sysfs_menlo_hdr);
			cmdhdrCR.rspsize = 4;
		} else
			cmdhdrCR = *cmdhdr;

		rc = lpfc_alloc_menlo_genrequest64(phba,
				&sysfs_menlo->cr,&cmdhdrCR);
		if (rc != 0) {
			lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
				"1223 menlo_write: couldn't alloc genreq %d\n",
				(int) rc);
			sysfs_menlo_idle(phba,sysfs_menlo);
			return -ENOMEM;
		}
	} else {
		sysfs_menlo = lpfc_get_sysfs_menlo(phba, 0);
		if (!sysfs_menlo)
			return -EAGAIN;
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"1212 menlo_write: sysfs_menlo %p cmd %x cmdsz %d"
			" rspsz %d cr-off %d cx-off %d count %d\n",
			sysfs_menlo,
			sysfs_menlo->cmdhdr.cmd,
			sysfs_menlo->cmdhdr.cmdsize,
			sysfs_menlo->cmdhdr.rspsize,
			(int)sysfs_menlo->cr.offset,
			(int)sysfs_menlo->cx.offset,
			(int)count);
	}

	if ((count + sysfs_menlo->cr.offset) > sysfs_menlo->cmdhdr.cmdsize) {
		if ( sysfs_menlo->cmdhdr.cmdsize != 4) {
		lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
			"1213 FCoE cmd overflow: off %d + cnt %d > cmdsz %d\n",
			(int)sysfs_menlo->cr.offset,
			(int)count,
			(int)sysfs_menlo->cmdhdr.cmdsize);
		sysfs_menlo_idle(phba, sysfs_menlo);
		return -ERANGE;
		}
	}

	spin_lock_irq(&phba->hbalock);
	if (sysfs_menlo->cmdhdr.cmd == MENLO_CMD_FW_DOWNLOAD)
		genreq = &sysfs_menlo->cx;
	else
		genreq = &sysfs_menlo->cr;

	if (off == 0) {
		if (sysfs_menlo->cmdhdr.cmd == MENLO_CMD_FW_DOWNLOAD) {
			tmpptr = NULL;
			genreq = &sysfs_menlo->cr;

			if (!mlast) {
			 mlast = list_get_first(&genreq->inhead,
						struct lpfc_dmabufext,
						dma.list);
			}
			if (mlast) {
				bpl = genreq->cmdbpl;
				memcpy((uint8_t *) mlast->dma.virt, buf, count);
				genreq->offset += count;
				tmpptr = (uint32_t *)mlast->dma.virt;
				lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
					"1258 cmd %x cmdsz %d rspsz %d "
					"copied %d addrL:%x addrH:%x\n",
					*tmpptr,
					*(tmpptr+1),
					*(tmpptr+2),
					(int)count,
					bpl->addrLow,bpl->addrHigh);
			} else {
				lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
					"1230 Could not find buffer for FCoE"
					" cmd:off %d indmp %p %d\n", (int)off,
					genreq->indmp,(int)count);
			}
		}

		sysfs_menlo->state = SMENLO_WRITING;
		spin_unlock_irq(&phba->hbalock);
		return count;
	} else {
		ssize_t adj_off = off - sizeof(struct lpfc_sysfs_menlo_hdr);
		int found = 0;
		if (sysfs_menlo->state  != SMENLO_WRITING ||
		    genreq->offset != adj_off) {
			spin_unlock_irq(&phba->hbalock);
			sysfs_menlo_idle(phba, sysfs_menlo);
			return -EAGAIN;
		}
		mlast = NULL;
		temp_off = sizeof(struct lpfc_sysfs_menlo_hdr);
		if (genreq->indmp) {
			list_for_each_entry(mlast,
				&genreq->inhead, dma.list) {
				if (temp_off == off)
					break;
				else
					temp_off += BUF_SZ_4K;
				mlastcnt++;
			}
		}
		addr_low = le32_to_cpu( putPaddrLow(mlast->dma.phys) );
		addr_high = le32_to_cpu( putPaddrHigh(mlast->dma.phys) );
		bpl = genreq->cmdbpl;
		bpl += mlastcnt;
		if (bpl->addrLow != addr_low ||  bpl->addrHigh != addr_high) {
			mlast = NULL;
			list_for_each_entry(mlast,
				&genreq->inhead, dma.list) {

				addr_low = le32_to_cpu(
					putPaddrLow(mlast->dma.phys) );
				addr_high = le32_to_cpu(
					putPaddrHigh(mlast->dma.phys) );
				if (bpl->addrLow == addr_low
					&&  bpl->addrHigh == addr_high) {
					found = 1;
					break;
				}
			if ( mlastcnt < 3 )
				lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
					"1234 menlo_write: off:%d "
					" mlastcnt:%d addl:%x addl:%x "
					" addrh:%x addrh:%x mlast:%p\n",
					(int)genreq->offset,
					mlastcnt,
					bpl->addrLow,
					addr_low,
					bpl->addrHigh,
					addr_high,mlast);
			}
		} else
			found = 1;

		if (!found) {
			lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
				"1235 Could not find buffer for FCoE"
				" cmd: off:%d  poff:%d cnt:%d"
				" mlastcnt:%d addl:%x addh:%x mdsz:%d \n",
				(int)genreq->offset,
				(int)off,
				(int)count,
				mlastcnt,
				bpl->addrLow,
				bpl->addrHigh,
				(int)sysfs_menlo->cmdhdr.cmdsize);
			mlast = NULL;
		}

	}

	if (mlast) {
		if (sysfs_menlo->cmdhdr.cmd == MENLO_CMD_FW_DOWNLOAD ) {
			bpl = genreq->cmdbpl;
			bpl += mlastcnt;
			tmpptr = (uint32_t *)mlast->dma.virt;
			if ( genreq->offset < hdr_offset ) {
				memcpy((uint8_t *) mlast->dma.virt,
					 buf+hdr_offset,
					 count-hdr_offset);
				bpl->tus.f.bdeSize = (ushort)count-hdr_offset;
				mlast->size = (ushort)count-hdr_offset;
			} else {
				memcpy((uint8_t *) mlast->dma.virt, buf, count);
				bpl->tus.f.bdeSize = (ushort)count;
				mlast->size = (ushort)count;
			}
			bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64;
			bpl->tus.w = le32_to_cpu(bpl->tus.w);

		} else
			memcpy((uint8_t *) mlast->dma.virt, buf, count);

		if (sysfs_menlo->cmdhdr.cmd == MENLO_CMD_LOOPBACK) {
			if (mlast) {
				tmpptr = (uint32_t *)mlast->dma.virt;
				if (*(tmpptr+2))
					phba->link_flag |= LS_LOOPBACK_MODE;
				else
					phba->link_flag &= ~LS_LOOPBACK_MODE;
			}
		}

		if (sysfs_menlo->cmdhdr.cmd == MENLO_CMD_FW_DOWNLOAD
			&& genreq->offset < hdr_offset) {
			if (sysfs_menlo->cr.indmp
				&& sysfs_menlo->cr.indmp->dma.virt) {
				mlast = sysfs_menlo->cr.indmp;
				memcpy((uint8_t *) mlast->dma.virt,
					buf, hdr_offset);
				tmpptr = (uint32_t *)mlast->dma.virt;
				lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
					"1237 cmd %x cmd1 %x cmd2 %x "
					"copied %d\n",
					*tmpptr,
					*(tmpptr+1),
					*(tmpptr+2),
					hdr_offset);
			}
		}
		genreq->offset += count;
	} else {
		spin_unlock_irq(&phba->hbalock);
		sysfs_menlo_idle(phba,sysfs_menlo);
		return -ERANGE;
	}

	spin_unlock_irq(&phba->hbalock);
	return count;

}


static ssize_t
sysfs_menlo_write(struct kobject *kobj, struct bin_attribute *bin_attr,
		 char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return lpfc_menlo_write(phba, buf, off, count);
}


static ssize_t
sysfs_menlo_issue_iocb_wait(struct lpfc_hba *phba,
		struct lpfc_menlo_genreq64 *req,
		struct lpfc_sysfs_menlo *sysfs_menlo)
{
	struct lpfc_sli *psli = NULL;
	struct lpfc_sli_ring *pring = NULL;
	int rc = 0;
	IOCB_t *rsp = NULL;
	struct lpfc_iocbq *cmdiocbq = NULL;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];
	rsp = &req->rspiocbq->iocb;
	cmdiocbq = req->cmdiocbq;

	rc = lpfc_sli_issue_iocb_wait(phba, pring, req->cmdiocbq, req->rspiocbq,
			req->timeout);

	if (rc == IOCB_TIMEDOUT) {

		cmdiocbq->context2 = NULL;
		cmdiocbq->context3 = sysfs_menlo;
		cmdiocbq->iocb_cmpl = lpfc_menlo_iocb_timeout_cmpl;
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
			"1227 FCoE IOCB TMO: handler set for %p\n",
			cmdiocbq->context3);
		return -EACCES;
	}

	if (rc != IOCB_SUCCESS) {
		rc =  -EFAULT;
		lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
			"1216 FCoE IOCB failed: off %d rc=%d \n",
			(int)req->offset, rc);
		goto sysfs_menlo_issue_iocb_wait_exit;
	}

	if (rsp->ulpStatus) {
		if (rsp->ulpStatus == IOSTAT_LOCAL_REJECT) {
			switch (rsp->un.ulpWord[4] & 0xff) {
			case IOERR_SEQUENCE_TIMEOUT:
				rc = -ETIMEDOUT;
				break;
			case IOERR_INVALID_RPI:
				rc = -EFAULT;
				break;
			default:
				rc = -EFAULT;
				break;
			}
			lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
				"1217 mlo_issueIocb:2 off %d rc=%d "
				"ulpWord[4] 0x%x\n",
				(int)req->offset, rc, rsp->un.ulpWord[4]);
		}
	}
sysfs_menlo_issue_iocb_wait_exit:
	return rc;
}


static ssize_t
sysfs_menlo_issue_iocb(struct lpfc_hba *phba, struct lpfc_menlo_genreq64 *req,
		struct lpfc_sysfs_menlo *sysfs_menlo)
{
	struct lpfc_sli *psli = NULL;
	struct lpfc_sli_ring *pring = NULL;
	int rc = 0;
	IOCB_t *rsp = NULL;
	struct lpfc_iocbq *cmdiocbq = NULL;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];
	rsp = &req->rspiocbq->iocb;
	cmdiocbq = req->cmdiocbq;
	cmdiocbq->context2 = sysfs_menlo;
	cmdiocbq->iocb_cmpl = lpfc_menlo_iocb_cmpl;
	lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
		"1257 lpfc_menlo_issue_iocb: handler set for %p\n",
		cmdiocbq->context3);

	rc = lpfc_sli_issue_iocb(phba, pring, req->cmdiocbq, 0);

	if (rc == IOCB_TIMEDOUT) {

		cmdiocbq->context2 = NULL;
		cmdiocbq->context3 = sysfs_menlo;
		cmdiocbq->iocb_cmpl = lpfc_menlo_iocb_timeout_cmpl;
		lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
			"1228 FCoE IOCB TMO: handler set for %p\n",
			cmdiocbq->context3);
		return -EACCES;
	}

	if (rc != IOCB_SUCCESS) {
		rc =  -EFAULT;
		lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
			"1238 FCoE IOCB failed: off %d rc=%d \n",
			(int)req->offset, rc);
		goto sysfs_menlo_issue_iocb_exit;
	}

	if (rsp->ulpStatus) {
		if (rsp->ulpStatus == IOSTAT_LOCAL_REJECT) {
			switch (rsp->un.ulpWord[4] & 0xff) {
			case IOERR_SEQUENCE_TIMEOUT:
				rc = -ETIMEDOUT;
				break;
			case IOERR_INVALID_RPI:
				rc = -EFAULT;
				break;
			default:
				rc = -EFAULT;
				break;
			}
			lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
				"1239 mlo_issueIocb:2 off %d rc=%d "
				"ulpWord[4] 0x%x\n",
				(int)req->offset, rc, rsp->un.ulpWord[4]);
		}
	}
sysfs_menlo_issue_iocb_exit:
	return rc;
}

static ssize_t
lpfc_menlo_read(struct lpfc_hba *phba, char *buf, loff_t off, size_t count,
	int wait)
{
	struct lpfc_sli *psli = NULL;
	struct lpfc_sli_ring *pring = NULL;
	int rc = 0;
	struct lpfc_sysfs_menlo *sysfs_menlo;
	struct lpfc_dmabufext *mlast = NULL;
	loff_t temp_off = 0;
	struct lpfc_menlo_genreq64 *genreq = NULL;
	IOCB_t *cmd = NULL, *rsp = NULL;
	uint32_t * uptr = NULL;


	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];

	if (off > SYSFS_MENLO_ATTR_SIZE)
		return -ERANGE;

	if ((count + off) > SYSFS_MENLO_ATTR_SIZE)
		count =  SYSFS_MENLO_ATTR_SIZE - off;

	if (off % 4 ||  count % 4 || (unsigned long)buf % 4)
		return -EINVAL;

	if (off && count == 0)
		return 0;

	sysfs_menlo = lpfc_get_sysfs_menlo(phba, 0);

	if (!sysfs_menlo)
		return -EPERM;

	if (!(psli->sli_flag & LPFC_SLI2_ACTIVE)) {
		sysfs_menlo_idle(phba, sysfs_menlo);
		lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
			"1214 Can not issue FCoE cmd,"
			" SLI not active: off %d rc= -EACCESS\n",
			(int)off);
		return -EACCES;
	}


	if ((phba->link_state < LPFC_LINK_UP)
		&& !(psli->sli_flag & LPFC_MENLO_MAINT)
		&& wait) {
		rc =  -EPERM;
		lpfc_printf_log(phba, KERN_ERR, LOG_LIBDFC,
			"1215 Can not issue FCoE cmd:"
			" not ready or not in maint mode"
			" off %d rc=%d \n",
			(int)off, rc);
		spin_lock_irq(&phba->hbalock);
		goto lpfc_menlo_read_err_exit;
	}

	if (off == 0 && sysfs_menlo->state == SMENLO_WRITING) {
		if (sysfs_menlo->cmdhdr.cmd == MENLO_CMD_FW_DOWNLOAD) {
			spin_lock_irq(&phba->hbalock);
			genreq = &sysfs_menlo->cr;
			spin_unlock_irq(&phba->hbalock);
		}
		if ( wait )
			rc = sysfs_menlo_issue_iocb_wait(phba,
							&sysfs_menlo->cr,
							sysfs_menlo);
		else {
			rc = sysfs_menlo_issue_iocb(phba,
							&sysfs_menlo->cr,
							sysfs_menlo);
			return rc;
		}

		spin_lock_irq(&phba->hbalock);
		if (rc < 0) {
			lpfc_printf_log(phba, KERN_WARNING, LOG_LIBDFC,
				"1224 FCoE iocb failed: off %d rc=%d \n",
				(int)off, rc);
			if (rc != -EACCES)
				goto lpfc_menlo_read_err_exit;
			else {
				spin_unlock_irq(&phba->hbalock);
				return rc;
			}
		}

		if (sysfs_menlo->cmdhdr.cmd == MENLO_CMD_FW_DOWNLOAD) {
			cmd = &sysfs_menlo->cx.cmdiocbq->iocb;
			rsp = &sysfs_menlo->cr.rspiocbq->iocb;
			mlast = list_get_first(&sysfs_menlo->cr.outhead,
				struct lpfc_dmabufext,
				dma.list);
			if ( *((uint32_t *) mlast->dma.virt) != 0 ) {
				memcpy(buf,(uint8_t *) mlast->dma.virt, count);
				goto lpfc_menlo_read_err_exit;
			}
			mlast = NULL;

			cmd->ulpCommand = CMD_GEN_REQUEST64_CX;
			cmd->ulpContext = rsp->ulpContext;
			cmd->ulpPU = 1;  /* RelOffset */
			cmd->un.ulpWord[4] = 0; /* offset 0 */

			spin_unlock_irq(&phba->hbalock);
			rc = sysfs_menlo_issue_iocb_wait(phba, &sysfs_menlo->cx,
					sysfs_menlo);
			spin_lock_irq(&phba->hbalock);
			if (rc < 0) {
				uptr = (uint32_t *) rsp;

				lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
					"1225 menlo_read: off %d rc=%d "
					"rspxri %d cmdxri %d \n",
					(int)off, rc, rsp->ulpContext,
					cmd->ulpContext);
				uptr = (uint32_t *)
					&sysfs_menlo->cr.cmdiocbq->iocb;
				lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
					"1236 cr:%.08x %.08x %.08x %.08x "
					"%.08x %.08x %.08x %.08x %.08x\n",
					*uptr, *(uptr+1), *(uptr+2),
					*(uptr+3), *(uptr+4), *(uptr+5),
					*(uptr+6), *(uptr+7), *(uptr+8));
				uptr = (uint32_t *)rsp;
				lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
					"1232 cr-rsp:%.08x %.08x %.08x %.08x "
					"%.08x %.08x %.08x %.08x %.08x\n",
					*uptr, *(uptr+1), *(uptr+2),
					*(uptr+3), *(uptr+4), *(uptr+5),
					*(uptr+6), *(uptr+7), *(uptr+8));
				uptr = (uint32_t *)cmd;
				lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
					"1233 cx:%.08x %.08x %.08x %.08x "
					"%.08x %.08x %.08x %.08x %.08x\n",
					*uptr, *(uptr+1), *(uptr+2),
					*(uptr+3), *(uptr+4), *(uptr+5),
					*(uptr+6), *(uptr+7), *(uptr+8));
				if (rc != -EACCES)
					goto lpfc_menlo_read_err_exit;
				else {
					spin_unlock_irq(&phba->hbalock);
					return rc;
				}
			}
		}
		sysfs_menlo->state = SMENLO_READING;
		sysfs_menlo->cr.offset = 0;

	} else
		spin_lock_irq(&phba->hbalock);

	if (sysfs_menlo->cmdhdr.cmd == MENLO_CMD_FW_DOWNLOAD)
		genreq = &sysfs_menlo->cx;
	else
		genreq = &sysfs_menlo->cr;

	/* Copy back response data */
	if (sysfs_menlo->cmdhdr.rspsize > count) {
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
				"1218 MloMgnt Rqst err Data: x%x %d %d %d %d\n",
				genreq->outdmp->flag,
				sysfs_menlo->cmdhdr.rspsize,
				(int)count, (int)off, (int)genreq->offset);
	}

	if (phba->sli.sli_flag & LPFC_BLOCK_MGMT_IO) {
		rc =   -EAGAIN;
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"1219 menlo_read:4 off %d rc=%d \n",
			(int)off, rc);
		goto lpfc_menlo_read_err_exit;
	}
	else if ( sysfs_menlo->state  != SMENLO_READING) {
		rc =  -EAGAIN;
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"1220 menlo_read:5 off %d reg off %d rc=%d state %x\n",
			(int)off,(int)genreq->offset, sysfs_menlo->state, rc);
		goto lpfc_menlo_read_err_exit;
	}
	temp_off = 0;
	mlast = NULL;
	list_for_each_entry(mlast, &genreq->outhead, dma.list) {
		if (temp_off == off)
			break;
		else
			temp_off += BUF_SZ_4K;
	}
	if (mlast)
		memcpy(buf,(uint8_t *) mlast->dma.virt, count);
	else {
		rc = -ERANGE;
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"1221 menlo_read:6 off %d rc=%d \n",
			(int)off, rc);
		goto lpfc_menlo_read_err_exit;
	}
	genreq->offset += count;


	if (genreq->offset >= sysfs_menlo->cmdhdr.rspsize) {
		lpfc_printf_log(phba, KERN_INFO, LOG_LIBDFC,
			"1222 menlo_read: done off %d rc=%d"
			" cnt %d rsp_code %x\n",
			(int)off, rc, (int)count,*((uint32_t *)buf));
		rc = count;
		goto lpfc_menlo_read_err_exit;
	}

	if (count >= sysfs_menlo->cmdhdr.rspsize)
		rc = sysfs_menlo->cmdhdr.rspsize;
	else /* Can there be a > 4k response */
		rc = count;
	if (genreq->offset < sysfs_menlo->cmdhdr.rspsize) {
		spin_unlock_irq(&phba->hbalock);
		return rc;
	}

lpfc_menlo_read_err_exit:
	spin_unlock_irq(&phba->hbalock);
	sysfs_menlo_idle(phba,sysfs_menlo);
	return rc;
}


static ssize_t
sysfs_menlo_read(struct kobject *kobj, struct bin_attribute *bin_attr,
		char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return lpfc_menlo_read(phba, buf, off, count, 1);
}
int need_non_blocking = 0;
void lpfc_check_menlo_cfg(struct lpfc_hba *phba)
{
	uint32_t cmd_size;
	uint32_t rsp_size;
	menlo_get_cmd_t *cmd = NULL;
	menlo_init_rsp_t *rsp = NULL;
	int rc = 0;

	lpfc_printf_log (phba, KERN_INFO, LOG_LINK_EVENT,
		"1253 Checking FCoE chip firmware.\n");
	if ( need_non_blocking ) /* Need non blocking issue_iocb */
		return;

	cmd_size = sizeof (menlo_get_cmd_t);
	cmd = kmalloc(cmd_size, GFP_KERNEL);
	if (!cmd ) {
		lpfc_printf_log (phba, KERN_ERR, LOG_LINK_EVENT,
		"1240 Unable to allocate command buffer memory.\n");
		return;
	}

	rsp_size = sizeof (menlo_init_rsp_t);
	rsp = kmalloc(rsp_size, GFP_KERNEL);
	if (!rsp ) {
		lpfc_printf_log (phba, KERN_ERR, LOG_LINK_EVENT,
		"1244 Unable to allocate response buffer memory.\n");
		kfree(rsp);
		return;
	}

	memset(cmd,0, cmd_size);
	memset(rsp,0, rsp_size);

	cmd->code = MENLO_CMD_GET_INIT;
	cmd->context = cmd_size;
	cmd->length = rsp_size;
	rc = lpfc_menlo_write (phba, (char *) cmd, 0, cmd_size);
	if ( rc != cmd_size ) {
		lpfc_printf_log (phba, KERN_ERR, LOG_LINK_EVENT,
			"1250 Menlo command error. code=%d.\n", rc);

		kfree (cmd);
		kfree (rsp);
		return;
	}
	cmd->code = MENLO_CMD_GET_INIT;
	cmd->context = 0;
	cmd->length = rsp_size;
	BE_swap32_buffer ((uint8_t *) cmd, cmd_size);
	rc = lpfc_menlo_write (phba, (char *) cmd, cmd_size, cmd_size);
	if ( rc != cmd_size ) {
		lpfc_printf_log (phba, KERN_ERR, LOG_LINK_EVENT,
			"1251 Menlo command error. code=%d.\n", rc);

		kfree (cmd);
		kfree (rsp);
		return;
	}
	rc = lpfc_menlo_read (phba, (char *) rsp, 0, rsp_size,0);
	if ( rc && rc != rsp_size ) {
		lpfc_printf_log (phba, KERN_ERR, LOG_LINK_EVENT,
			"1252 Menlo command error. code=%d.\n", rc);

	}
	kfree (cmd);
	kfree (rsp);
	return;
}

struct bin_attribute sysfs_menlo_attr = {
	.attr = {
		.name = "menlo",
		.mode = S_IRUSR | S_IWUSR,
		.owner = THIS_MODULE,
	},
	.size = SYSFS_MENLO_ATTR_SIZE,
	.read = sysfs_menlo_read,
	.write = sysfs_menlo_write,
};
