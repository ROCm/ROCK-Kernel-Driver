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
 * $Id: lpfc_init.c 1.160 2004/10/15 02:06:30EDT sf_support Exp  $
 */

#include <linux/version.h>
#include <linux/blkdev.h>
#include <linux/ctype.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/spinlock.h>

#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include "lpfc_sli.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_crtn.h"
#include "lpfc_hw.h"
#include "lpfc_logmsg.h"
#include "lpfc_mem.h"
#include "lpfc_version.h"
#include "lpfc_compat.h"

static int lpfc_parse_vpd(struct lpfc_hba *, uint8_t *);
static int lpfc_post_rcv_buf(struct lpfc_hba *);
static int lpfc_rdrev_wd30 = 0;

/************************************************************************/
/*                                                                      */
/*    lpfc_config_port_prep                                             */
/*    This routine will do LPFC initialization prior to the             */
/*    CONFIG_PORT mailbox command. This will be initialized             */
/*    as a SLI layer callback routine.                                  */
/*    This routine returns 0 on success or -ERESTART if it wants        */
/*    the SLI layer to reset the HBA and try again. Any                 */
/*    other return value indicates an error.                            */
/*                                                                      */
/************************************************************************/
int
lpfc_config_port_prep(struct lpfc_hba * phba)
{
	lpfc_vpd_t *vp = &phba->vpd;
	int i = 0;
	LPFC_MBOXQ_t *pmb;
	MAILBOX_t *mb;


	/* Get a Mailbox buffer to setup mailbox commands for HBA
	   initialization */
	pmb = mempool_alloc(phba->mbox_mem_pool, GFP_ATOMIC);
	if (!pmb) {
		phba->hba_state = LPFC_HBA_ERROR;
		return -ENOMEM;
	}

	mb = &pmb->mb;
	phba->hba_state = LPFC_INIT_MBX_CMDS;

	/* special handling for LC HBAs */
	if (lpfc_is_LC_HBA(phba->pcidev->device)) {
		char licensed[56] =
		    "key unlock for use with gnu public licensed code only\0";
		uint32_t *ptext = (uint32_t *) licensed;

		for (i = 0; i < 56; i += sizeof (uint32_t), ptext++)
			*ptext = cpu_to_be32(*ptext);

		/* Setup and issue mailbox READ NVPARAMS command */
		lpfc_read_nv(phba, pmb);
		memset((char*)mb->un.varRDnvp.rsvd3, 0,
			sizeof (mb->un.varRDnvp.rsvd3));
		memcpy((char*)mb->un.varRDnvp.rsvd3, licensed,
			 sizeof (licensed));

		if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
			/* Adapter initialization error, mbxCmd <cmd>
			   READ_NVPARM, mbxStatus <status> */
			lpfc_printf_log(phba,
					KERN_ERR,
					LOG_MBOX,
					"%d:0324 Config Port initialization "
					"error, mbxCmd x%x READ_NVPARM, "
					"mbxStatus x%x\n",
					phba->brd_no,
					mb->mbxCommand, mb->mbxStatus);
			return -ERESTART;
		}
		memcpy(phba->wwnn, (char *)mb->un.varRDnvp.nodename,
		       sizeof (mb->un.varRDnvp.nodename));
	}

	/* Setup and issue mailbox READ REV command */
	lpfc_read_rev(phba, pmb);
	if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		/* Adapter failed to init, mbxCmd <mbxCmd> READ_REV, mbxStatus
		   <status> */
		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_INIT,
				"%d:0439 Adapter failed to init, mbxCmd x%x "
				"READ_REV, mbxStatus x%x\n",
				phba->brd_no,
				mb->mbxCommand, mb->mbxStatus);
		mempool_free( pmb, phba->mbox_mem_pool);
		return -ERESTART;
	}

	/* The HBA's current state is provided by the ProgType and rr fields.
	 * Read and check the value of these fields before continuing to config
	 * this port.
	 */
	if (mb->un.varRdRev.rr == 0 || mb->un.varRdRev.un.b.ProgType != 2) {
		/* Old firmware */
		vp->rev.rBit = 0;
		/* Adapter failed to init, mbxCmd <cmd> READ_REV detected
		   outdated firmware */
		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_INIT,
				"%d:0440 Adapter failed to init, mbxCmd x%x "
				"READ_REV detected outdated firmware"
				"Data: x%x\n",
				phba->brd_no,
				mb->mbxCommand, 0);
		mempool_free(pmb, phba->mbox_mem_pool);
		return -ERESTART;
	} else {
		vp->rev.rBit = 1;
		vp->rev.sli1FwRev = mb->un.varRdRev.sli1FwRev;
		memcpy(vp->rev.sli1FwName,
			(char*)mb->un.varRdRev.sli1FwName, 16);
		vp->rev.sli2FwRev = mb->un.varRdRev.sli2FwRev;
		memcpy(vp->rev.sli2FwName,
			(char *)mb->un.varRdRev.sli2FwName, 16);
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

	if (lpfc_is_LC_HBA(phba->pcidev->device))
		memcpy(phba->RandomData, (char *)&mb->un.varWords[24],
			sizeof (phba->RandomData));

	/* Get adapter VPD information */
	lpfc_dump_mem(phba, pmb);
	if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		/* Let it go through even if failed. */
		/* Adapter failed to init, mbxCmd <cmd> DUMP VPD,
		   mbxStatus <status> */
		lpfc_printf_log(phba,
				KERN_INFO,
				LOG_INIT,
				"%d:0441 VPD not present on adapter, mbxCmd "
				"x%x DUMP VPD, mbxStatus x%x\n",
				phba->brd_no,
				mb->mbxCommand, mb->mbxStatus);
	} else if (mb->un.varDmp.ra == 1) {
		lpfc_parse_vpd(phba, (uint8_t *)&mb->un.varDmp.resp_offset);
	}
	mempool_free(pmb, phba->mbox_mem_pool);
	return 0;
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
lpfc_config_port_post(struct lpfc_hba * phba)
{
	LPFC_MBOXQ_t *pmb;
	MAILBOX_t *mb;
	struct lpfc_dmabuf *mp;
	struct lpfc_sli *psli = &phba->sli;
	uint32_t status, timeout;
	int i, j, flogi_sent;
	unsigned long isr_cnt, clk_cnt;


	/* Get a Mailbox buffer to setup mailbox commands for HBA
	   initialization */
	pmb = mempool_alloc(phba->mbox_mem_pool, GFP_ATOMIC);
	if (!pmb) {
		phba->hba_state = LPFC_HBA_ERROR;
		return -ENOMEM;
	}
	mb = &pmb->mb;

	/* Setup link timers */
	lpfc_config_link(phba, pmb);
	if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_INIT,
				"%d:0447 Adapter failed init, mbxCmd x%x "
				"CONFIG_LINK mbxStatus x%x\n",
				phba->brd_no,
				mb->mbxCommand, mb->mbxStatus);
		phba->hba_state = LPFC_HBA_ERROR;
		mempool_free( pmb, phba->mbox_mem_pool);
		return -EIO;
	}

	/* Get login parameters for NID.  */
	lpfc_read_sparam(phba, pmb);
	if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_INIT,
				"%d:0448 Adapter failed init, mbxCmd x%x "
				"READ_SPARM mbxStatus x%x\n",
				phba->brd_no,
				mb->mbxCommand, mb->mbxStatus);
		phba->hba_state = LPFC_HBA_ERROR;
		mempool_free( pmb, phba->mbox_mem_pool);
		return -EIO;
	}

	mp = (struct lpfc_dmabuf *) pmb->context1;

	/* The mailbox was populated by the HBA.  Flush it to main store for the
	 * driver.  Note that all context buffers are from the driver's
	 * dma pool and have length LPFC_BPL_SIZE.
	 */
	 pci_dma_sync_single_for_cpu(phba->pcidev, mp->phys, LPFC_BPL_SIZE,
		PCI_DMA_FROMDEVICE);

	memcpy(&phba->fc_sparam, mp->virt, sizeof (struct serv_parm));
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	pmb->context1 = NULL;

	memcpy(&phba->fc_nodename, &phba->fc_sparam.nodeName,
	       sizeof (struct lpfc_name));
	memcpy(&phba->fc_portname, &phba->fc_sparam.portName,
	       sizeof (struct lpfc_name));
	/* If no serial number in VPD data, use low 6 bytes of WWNN */
	/* This should be consolidated into parse_vpd ? - mr */
	if (phba->SerialNumber[0] == 0) {
		uint8_t *outptr;

		outptr = (uint8_t *) & phba->fc_nodename.IEEE[0];
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
	if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		phba->hba_state = LPFC_HBA_ERROR;
		mempool_free( pmb, phba->mbox_mem_pool);
		return -EIO;
	}


	lpfc_read_config(phba, pmb);
	if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_INIT,
				"%d:0453 Adapter failed to init, mbxCmd x%x "
				"READ_CONFIG, mbxStatus x%x\n",
				phba->brd_no,
				mb->mbxCommand, mb->mbxStatus);
		phba->hba_state = LPFC_HBA_ERROR;
		mempool_free( pmb, phba->mbox_mem_pool);
		return -EIO;
	}

	/* Reset the DFT_HBA_Q_DEPTH to the max xri  */
	if (phba->cfg_hba_queue_depth > (mb->un.varRdConfig.max_xri+1))
		phba->cfg_hba_queue_depth =
			mb->un.varRdConfig.max_xri + 1;

	if (mb->un.varRdConfig.lmt & LMT_2125_10bit) {
		/* HBA is 2G capable */
		phba->fc_flag |= FC_2G_CAPABLE;
	} else {
		/* If the HBA is not 2G capable, don't let link speed ask for
		   it */
		if (phba->cfg_link_speed > 1) {
			lpfc_printf_log(phba,
					KERN_WARNING,
					LOG_LINK_EVENT,
					"%d:1302 Reset link speed to auto. 1G "
					"HBA cfg'd for 2G Data: x%x\n",
					phba->brd_no,
					phba->cfg_link_speed);
			phba->cfg_link_speed = LINK_SPEED_AUTO;
		}
	}

	if (!phba->intr_inited) {
		/* Add our interrupt routine to kernel's interrupt chain &
		   enable it */

		if (request_irq(phba->pcidev->irq,
				lpfc_intr_handler,
				SA_SHIRQ,
				LPFC_DRIVER_NAME,
				phba) != 0) {
			/* Enable interrupt handler failed */
			lpfc_printf_log(phba,
					KERN_ERR,
					LOG_INIT,
					"%d:0451 Enable interrupt handler "
					"failed\n",
					phba->brd_no);
			phba->hba_state = LPFC_HBA_ERROR;
			mempool_free(pmb, phba->mbox_mem_pool);
			return -EIO;
		}
		phba->intr_inited =
			(HC_MBINT_ENA | HC_ERINT_ENA | HC_LAINT_ENA);
	}

	phba->hba_state = LPFC_LINK_DOWN;

	/* Only process IOCBs on ring 0 till hba_state is READY */
	if (psli->ring[psli->ip_ring].cmdringaddr)
		psli->ring[psli->ip_ring].flag |= LPFC_STOP_IOCB_EVENT;
	if (psli->ring[psli->fcp_ring].cmdringaddr)
		psli->ring[psli->fcp_ring].flag |= LPFC_STOP_IOCB_EVENT;
	if (psli->ring[psli->next_ring].cmdringaddr)
		psli->ring[psli->next_ring].flag |= LPFC_STOP_IOCB_EVENT;

	/* Post receive buffers for desired rings */
	lpfc_post_rcv_buf(phba);

	/* Enable appropriate host interrupts */
	status = readl(phba->HCregaddr);
	status |= phba->intr_inited;
	if (psli->sliinit.num_rings > 0)
		status |= HC_R0INT_ENA;
	if (psli->sliinit.num_rings > 1)
		status |= HC_R1INT_ENA;
	if (psli->sliinit.num_rings > 2)
		status |= HC_R2INT_ENA;
	if (psli->sliinit.num_rings > 3)
		status |= HC_R3INT_ENA;

	writel(status, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */

	/* Setup and issue mailbox INITIALIZE LINK command */
	lpfc_init_link(phba, pmb, phba->cfg_topology,
		       phba->cfg_link_speed);

	isr_cnt = psli->slistat.sliIntr;
	clk_cnt = jiffies;

	if (lpfc_sli_issue_mbox(phba, pmb, MBX_NOWAIT) != MBX_SUCCESS) {
		lpfc_printf_log(phba,
				KERN_ERR,
				LOG_INIT,
				"%d:0454 Adapter failed to init, mbxCmd x%x "
				"INIT_LINK, mbxStatus x%x\n",
				phba->brd_no,
				mb->mbxCommand, mb->mbxStatus);

		/* Clear all interrupt enable conditions */
		writel(0, phba->HCregaddr);
		readl(phba->HCregaddr); /* flush */
		/* Clear all pending interrupts */
		writel(0xffffffff, phba->HAregaddr);
		readl(phba->HAregaddr); /* flush */

		free_irq(phba->pcidev->irq, phba);
		phba->hba_state = LPFC_HBA_ERROR;
		mempool_free(pmb, phba->mbox_mem_pool);
		return -EIO;
	}
	/* MBOX buffer will be freed in mbox compl */

	/*
	 * Setup the ring 0 (els)  timeout handler
	 */
	timeout = phba->fc_ratov << 1;

	phba->els_tmofunc.expires = jiffies + HZ * timeout;
	add_timer(&phba->els_tmofunc);

	phba->fc_prevDID = Mask_DID;
	flogi_sent = 0;
	i = 0;
	while ((phba->hba_state != LPFC_HBA_READY) ||
	       (phba->num_disc_nodes) || (phba->fc_prli_sent) ||
	       ((phba->fc_map_cnt == 0) && (i<2)) ||
	       (psli->sliinit.sli_flag & LPFC_SLI_MBOX_ACTIVE)) {
		/* Check every second for 30 retries. */
		i++;
		if (i > 30) {
			break;
		}
		if ((i >= 15) && (phba->hba_state <= LPFC_LINK_DOWN)) {
			/* The link is down.  Set linkdown timeout */
			break;
		}

		/* Delay for 1 second to give discovery time to complete. */
		for (j = 0; j < 20; j++) {
			/* On some systems, the driver's attach/detect routines
			 * are uninterruptible.  Since the driver cannot predict
			 * when this is true, just manually call the ISR every
			 * 50 ms to service any interrupts.
			 */
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(msecs_to_jiffies(50));
			if (isr_cnt == psli->slistat.sliIntr) {
				lpfc_sli_intr(phba);
				isr_cnt = psli->slistat.sliIntr;
			}
		}
		isr_cnt = psli->slistat.sliIntr;

		if (clk_cnt == jiffies) {
			/* REMOVE: IF THIS HAPPENS, SYSTEM CLOCK IS NOT RUNNING.
			 * WE HAVE TO MANUALLY CALL OUR TIMEOUT ROUTINES.
			 */
			clk_cnt = jiffies;
		}
	}

	/* Since num_disc_nodes keys off of PLOGI, delay a bit to let
	 * any potential PRLIs to flush thru the SLI sub-system.
	 */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(50));
	if (isr_cnt == psli->slistat.sliIntr) {
		lpfc_sli_intr(phba);
	}

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
lpfc_hba_down_prep(struct lpfc_hba * phba)
{
	struct lpfc_sli *psli;
	struct lpfc_sli_ring *pring;
	struct lpfc_dmabuf *mp, *next_mp;

	psli = &phba->sli;
	/* Disable interrupts */
	writel(0, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */

	/* Cleanup potential discovery resources */
	lpfc_els_flush_rscn(phba);
	lpfc_els_flush_cmd(phba);
	lpfc_disc_flush_list(phba);

	/* Now cleanup posted buffers on each ring */
	pring = &psli->ring[LPFC_ELS_RING];
	list_for_each_entry_safe(mp, next_mp, &pring->postbufq, list) {
		list_del(&mp->list);
		pring->postbufq_cnt--;
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
	}

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
lpfc_handle_eratt(struct lpfc_hba * phba, uint32_t status)
{
	struct lpfc_sli *psli;
	struct lpfc_sli_ring  *pring;
	struct lpfc_iocbq     *iocb, *next_iocb;
	IOCB_t          *icmd = NULL, *cmd = NULL;
	struct lpfc_scsi_buf  *lpfc_cmd;
	volatile uint32_t status1, status2;
	void *from_slim;
	unsigned long iflag;

	psli = &phba->sli;
	from_slim = ((uint8_t *)phba->MBslimaddr + 0xa8);
	status1 = readl( from_slim);
	from_slim =  ((uint8_t *)phba->MBslimaddr + 0xac);
	status2 = readl( from_slim);

	if (status & HS_FFER6) {
		/* Re-establishing Link */
		spin_lock_irqsave(phba->host->host_lock, iflag);
		lpfc_printf_log(phba, KERN_INFO, LOG_LINK_EVENT,
				"%d:1301 Re-establishing Link "
				"Data: x%x x%x x%x\n",
				phba->brd_no, status, status1, status2);
		phba->fc_flag |= FC_ESTABLISH_LINK;

		/*
		* Firmware stops when it triggled erratt with HS_FFER6.
		* That could cause the I/Os dropped by the firmware.
		* Error iocb (I/O) on txcmplq and let the SCSI layer
		* retry it after re-establishing link.
		*/
		pring = &psli->ring[psli->fcp_ring];

		list_for_each_entry_safe(iocb, next_iocb, &pring->txcmplq,
					 list) {
			cmd = &iocb->iocb;

			/* Must be a FCP command */
			if ((cmd->ulpCommand != CMD_FCP_ICMND64_CR) &&
				(cmd->ulpCommand != CMD_FCP_IWRITE64_CR) &&
				(cmd->ulpCommand != CMD_FCP_IREAD64_CR)) {
				continue;
				}

			/* context1 MUST be a struct lpfc_scsi_buf */
			lpfc_cmd = (struct lpfc_scsi_buf *)(iocb->context1);
			if (lpfc_cmd == 0) {
				continue;
			}

			list_del(&iocb->list);
			pring->txcmplq_cnt--;

			if (iocb->iocb_cmpl) {
				icmd = &iocb->iocb;
				icmd->ulpStatus = IOSTAT_LOCAL_REJECT;
				icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
				(iocb->iocb_cmpl)(phba, iocb, iocb);
			} else {
				mempool_free( iocb, phba->iocb_mem_pool);
			}
		}

		/*
		 * There was a firmware error.  Take the hba offline and then
		 * attempt to restart it.
		 */
		spin_unlock_irqrestore(phba->host->host_lock, iflag);
		lpfc_offline(phba);
		if (lpfc_online(phba) == 0) {	/* Initialize the HBA */
			mod_timer(&phba->fc_estabtmo, jiffies + HZ * 60);
			return;
		}
	} else {
		/* The if clause above forces this code path when the status
		 * failure is a value other than FFER6.  Do not call the offline
		 *  twice. This is the adapter hardware error path.
		 */
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"%d:0457 Adapter Hardware Error "
				"Data: x%x x%x x%x\n",
				phba->brd_no, status, status1, status2);

		lpfc_offline(phba);

		/*
		 * Restart all traffic to this host.  Since the fc_transport
		 * block functions (future) were not called in lpfc_offline,
		 * don't call them here.
		 */
		scsi_unblock_requests(phba->host);
	}
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
lpfc_handle_latt(struct lpfc_hba * phba)
{
	struct lpfc_sli *psli;
	LPFC_MBOXQ_t *pmb;
	volatile uint32_t control;
	unsigned long iflag;


	spin_lock_irqsave(phba->host->host_lock, iflag);

	/* called from host_interrupt, to process LATT */
	psli = &phba->sli;
	psli->slistat.linkEvent++;

	/* Get a buffer which will be used for mailbox commands */
	if ((pmb = (LPFC_MBOXQ_t *) mempool_alloc(phba->mbox_mem_pool,
						  GFP_ATOMIC))) {
		if (lpfc_read_la(phba, pmb) == 0) {
			pmb->mbox_cmpl = lpfc_mbx_cmpl_read_la;
			if (lpfc_sli_issue_mbox
			    (phba, pmb, (MBX_NOWAIT | MBX_STOP_IOCB))
			    != MBX_NOT_FINISHED) {
				/* Turn off Link Attention interrupts until
				   CLEAR_LA done */
				psli->sliinit.sli_flag &= ~LPFC_PROCESS_LA;
				control = readl(phba->HCregaddr);
				control &= ~HC_LAINT_ENA;
				writel(control, phba->HCregaddr);
				readl(phba->HCregaddr); /* flush */

				/* Clear Link Attention in HA REG */
				writel(HA_LATT, phba->HAregaddr);
				readl(phba->HAregaddr); /* flush */
				spin_unlock_irqrestore(phba->host->host_lock,
						       iflag);
				return;
			} else {
				mempool_free(pmb, phba->mbox_mem_pool);
			}
		} else {
			mempool_free(pmb, phba->mbox_mem_pool);
		}
	}

	/* Clear Link Attention in HA REG */
	writel(HA_LATT, phba->HAregaddr);
	readl(phba->HAregaddr); /* flush */
	lpfc_linkdown(phba);
	phba->hba_state = LPFC_HBA_ERROR;
	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	return;
}

/************************************************************************/
/*                                                                      */
/*   lpfc_parse_vpd                                                     */
/*   This routine will parse the VPD data                               */
/*                                                                      */
/************************************************************************/
static int
lpfc_parse_vpd(struct lpfc_hba * phba, uint8_t * vpd)
{
	uint8_t lenlo, lenhi;
	uint8_t *Length;
	int i, j;
	int finished = 0;
	int index = 0;

	/* Vital Product */
	lpfc_printf_log(phba,
			KERN_INFO,
			LOG_INIT,
			"%d:0455 Vital Product Data: x%x x%x x%x x%x\n",
			phba->brd_no,
			(uint32_t) vpd[0], (uint32_t) vpd[1], (uint32_t) vpd[2],
			(uint32_t) vpd[3]);
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
lpfc_post_buffer(struct lpfc_hba * phba, struct lpfc_sli_ring * pring, int cnt,
		 int type)
{
	IOCB_t *icmd;
	struct lpfc_iocbq *iocb;
	struct lpfc_dmabuf *mp1, *mp2;

	cnt += pring->missbufcnt;

	/* While there are buffers to post */
	while (cnt > 0) {
		/* Allocate buffer for  command iocb */
		if ((iocb = mempool_alloc(phba->iocb_mem_pool, GFP_ATOMIC))
		    == 0) {
			pring->missbufcnt = cnt;
			return (cnt);
		}
		memset(iocb, 0, sizeof (struct lpfc_iocbq));
		icmd = &iocb->iocb;

		/* 2 buffers can be posted per command */
		/* Allocate buffer to post */
		mp1 = kmalloc(sizeof (struct lpfc_dmabuf), GFP_ATOMIC);
		if (mp1)
		    mp1->virt = lpfc_mbuf_alloc(phba, MEM_PRI,
						&mp1->phys);
		if (mp1 == 0 || mp1->virt == 0) {
			if (mp1)
				kfree(mp1);

			mempool_free( iocb, phba->iocb_mem_pool);
			pring->missbufcnt = cnt;
			return (cnt);
		}

		INIT_LIST_HEAD(&mp1->list);
		/* Allocate buffer to post */
		if (cnt > 1) {
			mp2 = kmalloc(sizeof (struct lpfc_dmabuf), GFP_KERNEL);
			if (mp2)
				mp2->virt = lpfc_mbuf_alloc(phba, MEM_PRI,
							    &mp2->phys);
			if (mp2 == 0 || mp2->virt == 0) {
				if (mp2)
					kfree(mp2);
				lpfc_mbuf_free(phba, mp1->virt, mp1->phys);
				kfree(mp1);
				mempool_free( iocb, phba->iocb_mem_pool);
				pring->missbufcnt = cnt;
				return (cnt);
			}

			INIT_LIST_HEAD(&mp2->list);
		} else {
			mp2 = NULL;
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
		icmd->ulpIoTag = lpfc_sli_next_iotag(phba, pring);
		icmd->ulpLe = 1;

		if (lpfc_sli_issue_iocb(phba, pring, iocb, 0) == IOCB_ERROR) {
			lpfc_mbuf_free(phba, mp1->virt, mp1->phys);
			kfree(mp1);
			if (mp2) {
				lpfc_mbuf_free(phba, mp2->virt, mp2->phys);
				kfree(mp2);
			}
			mempool_free( iocb, phba->iocb_mem_pool);
			pring->missbufcnt = cnt;
			return (cnt);
		}
		lpfc_sli_ringpostbuf_put(phba, pring, mp1);
		if (mp2) {
			lpfc_sli_ringpostbuf_put(phba, pring, mp2);
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
static int
lpfc_post_rcv_buf(struct lpfc_hba * phba)
{
	struct lpfc_sli *psli = &phba->sli;

	/* Ring 0, ELS / CT buffers */
	lpfc_post_buffer(phba, &psli->ring[LPFC_ELS_RING], LPFC_BUF_RING0, 1);
	/* Ring 2 - FCP no buffers needed */

	return 0;
}

#define S(N,V) (((V)<<(N))|((V)>>(32-(N))))

/************************************************************************/
/*                                                                      */
/*   lpfc_sha_init                                                      */
/*                                                                      */
/************************************************************************/
static void
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
static void
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
static void
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
lpfc_hba_init(struct lpfc_hba *phba, uint32_t *hbainit)
{
	int t;
	uint32_t HashWorking[80];
	uint32_t *pwwnn;

	pwwnn = phba->wwnn;
	memset(HashWorking, 0, sizeof (HashWorking));
	HashWorking[0] = HashWorking[78] = *pwwnn++;
	HashWorking[1] = HashWorking[79] = *pwwnn;
	for (t = 0; t < 7; t++) {
		lpfc_challenge_key(phba->RandomData + t, HashWorking + t);
	}
	lpfc_sha_init(hbainit);
	lpfc_sha_iterate(hbainit, HashWorking);
}

static void
lpfc_consistent_bind_cleanup(struct lpfc_hba * phba)
{
	struct lpfc_bindlist *bdlp, *next_bdlp;

	list_for_each_entry_safe(bdlp, next_bdlp,
				 &phba->fc_nlpbind_list, nlp_listp) {
		list_del(&bdlp->nlp_listp);
		mempool_free( bdlp, phba->bind_mem_pool);
	}
	phba->fc_bind_cnt = 0;
}

void
lpfc_cleanup(struct lpfc_hba * phba, uint32_t save_bind)
{
	struct lpfc_nodelist *ndlp, *next_ndlp;

	/* clean up phba - lpfc specific */
	lpfc_can_disctmo(phba);
	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_nlpunmap_list,
				nlp_listp) {
		lpfc_nlp_remove(phba, ndlp);
	}

	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_nlpmap_list,
				 nlp_listp) {
		lpfc_nlp_remove(phba, ndlp);
	}

	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_unused_list,
				nlp_listp) {
		lpfc_nlp_list(phba, ndlp, NLP_NO_LIST);
	}

	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_plogi_list,
				nlp_listp) {
		lpfc_nlp_remove(phba, ndlp);
	}

	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_adisc_list,
				nlp_listp) {
		lpfc_nlp_remove(phba, ndlp);
	}

	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_reglogin_list,
				nlp_listp) {
		lpfc_nlp_remove(phba, ndlp);
	}

	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_prli_list,
				nlp_listp) {
		lpfc_nlp_remove(phba, ndlp);
	}

	list_for_each_entry_safe(ndlp, next_ndlp, &phba->fc_npr_list,
				nlp_listp) {
		lpfc_nlp_remove(phba, ndlp);
	}

	if (save_bind == 0) {
		lpfc_consistent_bind_cleanup(phba);
	}

	INIT_LIST_HEAD(&phba->fc_nlpmap_list);
	INIT_LIST_HEAD(&phba->fc_nlpunmap_list);
	INIT_LIST_HEAD(&phba->fc_unused_list);
	INIT_LIST_HEAD(&phba->fc_plogi_list);
	INIT_LIST_HEAD(&phba->fc_adisc_list);
	INIT_LIST_HEAD(&phba->fc_reglogin_list);
	INIT_LIST_HEAD(&phba->fc_prli_list);
	INIT_LIST_HEAD(&phba->fc_npr_list);

	phba->fc_map_cnt   = 0;
	phba->fc_unmap_cnt = 0;
	phba->fc_plogi_cnt = 0;
	phba->fc_adisc_cnt = 0;
	phba->fc_reglogin_cnt = 0;
	phba->fc_prli_cnt  = 0;
	phba->fc_npr_cnt   = 0;
	phba->fc_unused_cnt= 0;
	return;
}

void
lpfc_establish_link_tmo(unsigned long ptr)
{
	struct lpfc_hba *phba = (struct lpfc_hba *)ptr;
	unsigned long iflag;

	spin_lock_irqsave(phba->host->host_lock, iflag);

	/* Re-establishing Link, timer expired */
	lpfc_printf_log(phba, KERN_ERR, LOG_LINK_EVENT,
			"%d:1300 Re-establishing Link, timer expired "
			"Data: x%x x%x\n",
			phba->brd_no, phba->fc_flag, phba->hba_state);
	phba->fc_flag &= ~FC_ESTABLISH_LINK;
	spin_unlock_irqrestore(phba->host->host_lock, iflag);
}

int
lpfc_online(struct lpfc_hba * phba)
{
	if (!phba)
		return 0;

	if (!(phba->fc_flag & FC_OFFLINE_MODE))
		return 0;

	lpfc_printf_log(phba,
		       KERN_WARNING,
		       LOG_INIT,
		       "%d:0458 Bring Adapter online\n",
		       phba->brd_no);

	if (!lpfc_sli_queue_setup(phba))
		return 1;

	if (lpfc_sli_hba_setup(phba))	/* Initialize the HBA */
		return 1;

	phba->fc_flag &= ~FC_OFFLINE_MODE;

	/*
	 * Restart all traffic to this host.  Since the fc_transport block
	 * functions (future) were not called in lpfc_offline, don't call them
	 * here.
	 */
	scsi_unblock_requests(phba->host);
	return 0;
}

int
lpfc_offline(struct lpfc_hba * phba)
{
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	unsigned long iflag;
	int i = 0;

	if (!phba)
		return 0;

	if (phba->fc_flag & FC_OFFLINE_MODE)
		return 0;

	/*
	 * Don't call the fc_transport block api (future).  The device is
	 * going offline and causing a timer to fire in the midlayer is
	 * unproductive.  Just block all new requests until the driver
	 * comes back online.
	 */
	scsi_block_requests(phba->host);
	psli = &phba->sli;
	pring = &psli->ring[psli->fcp_ring];

	lpfc_linkdown(phba);

	/* The linkdown event takes 30 seconds to timeout. */
	while (pring->txcmplq_cnt) {
		mdelay(10);
		if (i++ > 3000)
			break;
	}

	/* stop all timers associated with this hba */
	spin_lock_irqsave(phba->host->host_lock, iflag);
	lpfc_stop_timer(phba);
	spin_unlock_irqrestore(phba->host->host_lock, iflag);

	lpfc_printf_log(phba,
		       KERN_WARNING,
		       LOG_INIT,
		       "%d:0460 Bring Adapter offline\n",
		       phba->brd_no);

	/* Bring down the SLI Layer and cleanup.  The HBA is offline
	   now.  */
	lpfc_sli_hba_down(phba);
	lpfc_cleanup(phba, 1);
	phba->fc_flag |= FC_OFFLINE_MODE;
	return 0;
}

/******************************************************************************
* Function name : lpfc_scsi_free
*
* Description   : Called from fc_detach to free scsi tgt / lun resources
*
******************************************************************************/
int
lpfc_scsi_free(struct lpfc_hba * phba)
{
	struct lpfc_target *targetp;
	int i;

	for (i = 0; i < MAX_FCP_TARGET; i++) {
		targetp = phba->device_queue_hash[i];
		if (targetp) {
			kfree(targetp);
			phba->device_queue_hash[i] = NULL;
		}
	}
	return 0;
}

static void
lpfc_wakeup_event(struct lpfc_hba * phba, fcEVTHDR_t * ep)
{
	ep->e_mode &= ~E_SLEEPING_MODE;
	switch (ep->e_mask) {
	case FC_REG_LINK_EVENT:
		wake_up_interruptible(&phba->linkevtwq);
		break;
	case FC_REG_RSCN_EVENT:
		wake_up_interruptible(&phba->rscnevtwq);
		break;
	case FC_REG_CT_EVENT:
		wake_up_interruptible(&phba->ctevtwq);
		break;
	}
	return;
}

int
lpfc_put_event(struct lpfc_hba * phba, uint32_t evcode, uint32_t evdata0,
	       void * evdata1, uint32_t evdata2, uint32_t evdata3)
{
	fcEVT_t *ep;
	fcEVTHDR_t *ehp = phba->fc_evt_head;
	int found = 0;
	void *fstype = NULL;
	struct lpfc_dmabuf *mp;
	struct lpfc_sli_ct_request *ctp;
	struct lpfc_hba_event *rec;
	uint32_t evtype;

	switch (evcode) {
		case HBA_EVENT_RSCN:
			evtype = FC_REG_RSCN_EVENT;
			break;
		case HBA_EVENT_LINK_DOWN:
		case HBA_EVENT_LINK_UP:
			evtype = FC_REG_LINK_EVENT;
			break;
		default:
			evtype = FC_REG_CT_EVENT;
	}

	if (evtype == FC_REG_RSCN_EVENT || evtype == FC_REG_LINK_EVENT) {
		rec = &phba->hbaevt[phba->hba_event_put];
		rec->fc_eventcode = evcode;
		rec->fc_evdata1 = evdata0;
		rec->fc_evdata2 = (uint32_t)(unsigned long)evdata1;
		rec->fc_evdata3 = evdata2;
		rec->fc_evdata4 = evdata3;

		phba->hba_event_put++;
		if (phba->hba_event_put >= MAX_HBAEVT)
			phba->hba_event_put = 0;

		if (phba->hba_event_put == phba->hba_event_get) {
			phba->hba_event_missed++;
			phba->hba_event_get++;
			if (phba->hba_event_get >= MAX_HBAEVT)
				phba->hba_event_get = 0;
		}
	}

	if (evtype == FC_REG_CT_EVENT) {
		mp = (struct lpfc_dmabuf *) evdata1;
		ctp = (struct lpfc_sli_ct_request *) mp->virt;
		fstype = (void *)(ulong) (ctp->FsType);
	}

	while (ehp && ((ehp->e_mask != evtype) || (ehp->e_type != fstype)))
		ehp = (fcEVTHDR_t *) ehp->e_next_header;

	if (!ehp)
		return (0);

	ep = ehp->e_head;

	while (ep && !(found)) {
		if (ep->evt_sleep) {
			switch (evtype) {
			case FC_REG_CT_EVENT:
				if ((ep->evt_type ==
				     (void *)(ulong) FC_FSTYPE_ALL)
				    || (ep->evt_type == fstype)) {
					found++;
					ep->evt_data0 = evdata0; /* tag */
					ep->evt_data1 = evdata1; /* buffer
								    ptr */
					ep->evt_data2 = evdata2; /* count */
					ep->evt_sleep = 0;
					if (ehp->e_mode & E_SLEEPING_MODE) {
						ehp->e_flag |=
						    E_GET_EVENT_ACTIVE;
						lpfc_wakeup_event(phba, ehp);
					}
					/* For FC_REG_CT_EVENT just give it to
					   first one found */
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
					lpfc_wakeup_event(phba, ehp);
				}
				/* For all other events, give it to every one
				   waiting */
				break;
			}
		}
		ep = ep->evt_next;
	}
	if (evtype == FC_REG_LINK_EVENT)
		phba->nport_event_cnt++;

	return (found);
}

int
lpfc_stop_timer(struct lpfc_hba * phba)
{
	struct lpfc_sli *psli = &phba->sli;

	/* Instead of a timer, this has been converted to a
	 * deferred procedding list.
	 */
	while (!list_empty(&phba->freebufList)) {
		struct lpfc_dmabuf *mp;

		mp = (struct lpfc_dmabuf *)(phba->freebufList.next);
		if (mp) {
			lpfc_mbuf_free(phba, mp->virt, mp->phys);
			list_del(&mp->list);
			kfree((void *)mp);
		}
	}

	del_timer_sync(&phba->fc_estabtmo);
	del_timer_sync(&phba->fc_disctmo);
	del_timer_sync(&phba->fc_fdmitmo);
	del_timer_sync(&phba->els_tmofunc);
	psli = &phba->sli;
	del_timer_sync(&psli->mbox_tmo);
	return(1);
}
