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
 * $Id: lpfc_fcp.c 1.406 2004/10/18 20:30:03EDT sf_support Exp  $
 */

#include <linux/version.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/utsname.h>

#include <asm/byteorder.h>

#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_transport_fc.h>

#include "lpfc_sli.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_fcp.h"
#include "lpfc_hw.h"
#include "lpfc_logmsg.h"
#include "lpfc_mem.h"
#include "lpfc_version.h"
#include "lpfc_compat.h"
#include "lpfc_crtn.h"

static char *lpfc_drvr_name = LPFC_DRIVER_NAME;

static struct scsi_transport_template *lpfc_transport_template = NULL;

static struct list_head lpfc_hba_list = LIST_HEAD_INIT(lpfc_hba_list);

static const char *
lpfc_info(struct Scsi_Host *host)
{
	struct lpfc_hba    *phba = (struct lpfc_hba *) host->hostdata[0];
	int len;
	static char  lpfcinfobuf[128];

	memset(lpfcinfobuf,0,128);
	if (phba && phba->pcidev){
	        lpfc_get_hba_model_desc(phba, NULL, lpfcinfobuf);
		len = strlen(lpfcinfobuf);
		snprintf(lpfcinfobuf + len,
			128-len,
	       		" on PCI bus %02x device %02x irq %d",
			phba->pcidev->bus->number,
		 	phba->pcidev->devfn,
			phba->pcidev->irq);
	}
	return lpfcinfobuf;
}

static void
lpfc_jedec_to_ascii(int incr, char hdw[])
{
	int i, j;
	for (i = 0; i < 8; i++) {
		j = (incr & 0xf);
		if (j <= 9)
			hdw[7 - i] = 0x30 +  j;
		 else
			hdw[7 - i] = 0x61 + j - 10;
		incr = (incr >> 4);
	}
	hdw[8] = 0;
	return;
}

static ssize_t
lpfc_drvr_version_show(struct class_device *cdev, char *buf)
{
	return snprintf(buf, PAGE_SIZE, LPFC_MODULE_DESC "\n");
}

static ssize_t
management_version_show(struct class_device *cdev, char *buf)
{
	return snprintf(buf, PAGE_SIZE, DFC_API_VERSION "\n");
}

static ssize_t
lpfc_info_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	return snprintf(buf, PAGE_SIZE, "%s\n",lpfc_info(host));
}

static ssize_t
lpfc_serialnum_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	return snprintf(buf, PAGE_SIZE, "%s\n",phba->SerialNumber);
}

static ssize_t
lpfc_fwrev_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	char fwrev[32];
	lpfc_decode_firmware_rev(phba, fwrev, 1);
	return snprintf(buf, PAGE_SIZE, "%s\n",fwrev);
}

static ssize_t
lpfc_hdw_show(struct class_device *cdev, char *buf)
{
	char hdw[9];
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	lpfc_vpd_t *vp = &phba->vpd;
	lpfc_jedec_to_ascii(vp->rev.biuRev, hdw);
	return snprintf(buf, PAGE_SIZE, "%s\n", hdw);
}
static ssize_t
lpfc_option_rom_version_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	return snprintf(buf, PAGE_SIZE, "%s\n", phba->OptionROMVersion);
}
static ssize_t
lpfc_state_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	int len = 0;
	switch (phba->hba_state) {
	case LPFC_INIT_START:
	case LPFC_INIT_MBX_CMDS:
	case LPFC_LINK_DOWN:
		len += snprintf(buf + len, PAGE_SIZE-len, "Link Down\n");
		break;
	case LPFC_LINK_UP:
	case LPFC_LOCAL_CFG_LINK:
		len += snprintf(buf + len, PAGE_SIZE-len, "Link Up\n");
		break;
	case LPFC_FLOGI:
	case LPFC_FABRIC_CFG_LINK:
	case LPFC_NS_REG:
	case LPFC_NS_QRY:
	case LPFC_BUILD_DISC_LIST:
	case LPFC_DISC_AUTH:
	case LPFC_CLEAR_LA:
		len += snprintf(buf + len, PAGE_SIZE-len,
				"Link Up - Discovery\n");
		break;
	case LPFC_HBA_READY:
		len += snprintf(buf + len, PAGE_SIZE-len,
				"Link Up - Ready:\n");
		if (phba->fc_topology == TOPOLOGY_LOOP) {
			if (phba->fc_flag & FC_PUBLIC_LOOP)
				len += snprintf(buf + len, PAGE_SIZE-len,
						"   Public Loop\n");
			else
				len += snprintf(buf + len, PAGE_SIZE-len,
						"   Private Loop\n");
		} else {
			if (phba->fc_flag & FC_FABRIC)
				len += snprintf(buf + len, PAGE_SIZE-len,
						"   Fabric\n");
			else
				len += snprintf(buf + len, PAGE_SIZE-len,
						"   Point-2-Point\n");
		}
	}
	return len;
}

static ssize_t
lpfc_num_discovered_ports_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	return snprintf(buf, PAGE_SIZE, "%d\n", phba->fc_map_cnt +
							phba->fc_unmap_cnt);
}

#ifndef FC_TRANS_VER2
/*
 * These are replaced by Generic FC transport attributes
 */
static ssize_t
lpfc_speed_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	int len = 0;
	if (phba->fc_linkspeed == LA_2GHZ_LINK)
		len += snprintf(buf + len, PAGE_SIZE-len, "2 Gigabit\n");
	else
		len += snprintf(buf + len, PAGE_SIZE-len, "1 Gigabit\n");
	return len;
}

static ssize_t
lpfc_node_name_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	uint64_t node_name = 0;
	memcpy (&node_name, &phba->fc_nodename, sizeof (struct lpfc_name));
	return snprintf(buf, PAGE_SIZE, "0x%llx\n", be64_to_cpu(node_name));
}
static ssize_t
lpfc_port_name_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	uint64_t port_name = 0;
	memcpy (&port_name, &phba->fc_portname, sizeof (struct lpfc_name));
	return snprintf(buf, PAGE_SIZE, "0x%llx\n", be64_to_cpu(port_name));
}
static ssize_t
lpfc_did_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	return snprintf(buf, PAGE_SIZE, "0x%x\n", phba->fc_myDID);
}

static ssize_t
lpfc_port_type_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];

	size_t retval = 0;

	if (phba->fc_topology == TOPOLOGY_LOOP) {
		if (phba->fc_flag & FC_PUBLIC_LOOP)
			retval = snprintf(buf, PAGE_SIZE, "NL_Port\n");
		else
		        retval = snprintf(buf, PAGE_SIZE, "L_Port\n");
	} else {
		if (phba->fc_flag & FC_FABRIC)
			retval = snprintf(buf, PAGE_SIZE, "N_Port\n");
		else
			retval = snprintf(buf, PAGE_SIZE,
					  "Point-to-Point N_Port\n");
	}

	return retval;
}

static ssize_t
lpfc_fabric_name_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	uint64_t node_name = 0;
	memcpy (&node_name, &phba->fc_nodename, sizeof (struct lpfc_name));

	if ((phba->fc_flag & FC_FABRIC) ||
	    ((phba->fc_topology == TOPOLOGY_LOOP) &&
	     (phba->fc_flag & FC_PUBLIC_LOOP))) {
			memcpy(&node_name,
			       & phba->fc_fabparam.nodeName,
			       sizeof (struct lpfc_name));
	}

	return snprintf(buf, PAGE_SIZE, "0x%08llx\n", be64_to_cpu(node_name));
}
#endif /* not FC_TRANS_VER2 */

static ssize_t
lpfc_events_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	int i = 0, len = 0, get = phba->hba_event_put;
	struct lpfc_hba_event *rec;

	if (get == phba->hba_event_get)
		 return snprintf(buf, PAGE_SIZE, "None\n");

	for (i = 0; i < MAX_HBAEVT; i++) {
		if (get == 0)
			get = MAX_HBAEVT;
		get--;
		rec = &phba->hbaevt[get];
		switch (rec->fc_eventcode) {
			case 0:
				len += snprintf(buf+len, PAGE_SIZE-len,
						"---------");
				break;
			case HBA_EVENT_RSCN:
				len += snprintf(buf+len, PAGE_SIZE-len,
						"RSCN     ");
				break;
			case HBA_EVENT_LINK_UP:
				len += snprintf(buf+len, PAGE_SIZE-len,
						 "LINK UP  ");
				break;
			case HBA_EVENT_LINK_DOWN:
				len += snprintf(buf+len, PAGE_SIZE-len,
							"LINK DOWN");
				break;
			default:
				len += snprintf(buf+len, PAGE_SIZE-len,
						"?????????");
				break;

		}
		len += snprintf(buf+len, PAGE_SIZE-len, " %d,%d,%d,%d\n",
				 rec->fc_evdata1, rec->fc_evdata2,
				 rec->fc_evdata3, rec->fc_evdata4);
	}
	return len;
}

static ssize_t
lpfc_issue_lip (struct class_device *cdev, const char *buf, size_t count)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
 	int val=0;
	struct lpfc_sli *psli;
	struct lpfc_sli_ring *pring;
	LPFC_MBOXQ_t *pmboxq;
	int mbxstatus;
	int i;
	unsigned long iflag;

	if (!phba) return 0;

 	if (sscanf(buf, "%d", &val) != 1)
		return 0;

	if (!val) return 0;

	if (phba->fc_flag & FC_OFFLINE_MODE)
		return 0;

	psli = &phba->sli;

	mbxstatus = MBXERR_ERROR;

	if (phba->hba_state != LPFC_HBA_READY ||
	    (pmboxq = mempool_alloc(phba->mbox_mem_pool,GFP_KERNEL)) == NULL)
		return 0;

	memset((void *)pmboxq, 0, sizeof (LPFC_MBOXQ_t));

	/*
	 * Stop the midlayer from sending IO since the driver is
	 * about the issue a lip.
	 */
	scsi_block_requests(phba->host);

	i = 0;
	pring = &psli->ring[psli->fcp_ring];

	spin_lock_irqsave (phba->host->host_lock, iflag);

	while (pring->txcmplq_cnt && (i++ < 500)) {
		spin_unlock_irqrestore(phba->host->host_lock, iflag);
		mdelay(10);
		spin_lock_irqsave(phba->host->host_lock, iflag);
	}

	lpfc_init_link(phba, pmboxq, phba->cfg_topology, phba->cfg_link_speed);
	spin_unlock_irqrestore(phba->host->host_lock, iflag);

	mbxstatus = lpfc_sli_issue_mbox_wait(phba, pmboxq,
						phba->fc_ratov * 2);

	if (mbxstatus == MBX_TIMEOUT) {
		/*
		 * Let SLI layer release mboxq if mbox command completed after
		 * timeout.
		 */
		pmboxq->mbox_cmpl = 0;
	} else {
		mempool_free( pmboxq, phba->mbox_mem_pool);
	}

	/*
	 * Tell the midlayer to start sending IO since the driver is
	 * now ready.
	 */
	scsi_unblock_requests(phba->host);

	if (mbxstatus == MBXERR_ERROR)
		return 0;

	return strlen(buf);
}

static ssize_t
lpfc_nport_evt_cnt_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	return snprintf(buf, PAGE_SIZE, "%d\n", phba->nport_event_cnt);
}

static ssize_t
lpfc_board_online_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];

	if (!phba) return 0;

	if (phba->fc_flag & FC_OFFLINE_MODE)
		return snprintf(buf, PAGE_SIZE, "0\n");
	else
		return snprintf(buf, PAGE_SIZE, "1\n");
}

static ssize_t
lpfc_board_online_store(struct class_device *cdev, const char *buf,
								size_t count)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
 	int val=0;

	if (!phba) return 0;

 	if (sscanf(buf, "%d", &val) != 1)
		return 0;

	if (val && (phba->fc_flag & FC_OFFLINE_MODE)) {
		lpfc_online(phba);
	}
	else if (!val && !(phba->fc_flag & FC_OFFLINE_MODE)) {
		lpfc_offline(phba);
	}

	return strlen(buf);
}

static int
lpfc_disc_ndlp_show(struct lpfc_hba * phba, struct lpfc_nodelist *ndlp,
			char *buf, int offset)
{
	int len = 0, pgsz = PAGE_SIZE;
	uint8_t name[sizeof (struct lpfc_name)];

	buf += offset;
	pgsz -= offset;
	len += snprintf(buf + len, pgsz -len,
			"DID %06x WWPN ", ndlp->nlp_DID);

	/* A Fibre Channel node or port name is 8 octets
	 * long and delimited by colons.
	 */
	memcpy (&name[0], &ndlp->nlp_portname,
		sizeof (struct lpfc_name));
	len += snprintf(buf + len, pgsz-len,
			"%02x:%02x:%02x:%02x:%02x:%02x:"
			"%02x:%02x",
			name[0], name[1], name[2],
			name[3], name[4], name[5],
			name[6], name[7]);

	len += snprintf(buf + len, pgsz-len,
			" WWNN ");
	memcpy (&name[0], &ndlp->nlp_nodename,
		sizeof (struct lpfc_name));
	len += snprintf(buf + len, pgsz-len,
			"%02x:%02x:%02x:%02x:%02x:%02x:"
			"%02x:%02x\n",
			name[0], name[1], name[2],
			name[3], name[4], name[5],
			name[6], name[7]);
	len += snprintf(buf + len, pgsz-len,
			"    INFO %02x:%08x:%02x:%02x:%02x:%02x:"
			"%02x:%02x:%02x\n",
			ndlp->nlp_state, ndlp->nlp_flag, ndlp->nlp_type,
			ndlp->nlp_rpi, ndlp->nlp_sid, ndlp->nlp_failMask,
			ndlp->nlp_retry, ndlp->nlp_disc_refcnt,
			ndlp->nlp_fcp_info);
	return len;
}

#define LPFC_MAX_SYS_DISC_ENTRIES 35

static ssize_t
lpfc_disc_npr_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	struct lpfc_nodelist  *ndlp, *next_ndlp;
	struct list_head *listp;
	unsigned long iflag;
	int i = 0, len = 0;

	if (!phba) return 0;

	spin_lock_irqsave(phba->host->host_lock, iflag);
	listp = &phba->fc_npr_list;
	if (list_empty(listp)) {
		spin_unlock_irqrestore(phba->host->host_lock, iflag);
		return snprintf(buf, PAGE_SIZE, "NPR    list: Empty\n");
	}

	len += snprintf(buf+len, PAGE_SIZE-len, "NPR    list: %d Entries\n",
		phba->fc_npr_cnt);
	list_for_each_entry_safe(ndlp, next_ndlp, listp, nlp_listp) {
		i++;
		if(i > LPFC_MAX_SYS_DISC_ENTRIES) {
			len += snprintf(buf+len, PAGE_SIZE-len,
			"Missed %d entries - sysfs %ld limit exceeded\n",
			(phba->fc_npr_cnt - i + 1), PAGE_SIZE);
			break;
		}
		if(len > (PAGE_SIZE-1))  /* double check */
			break;
		len += lpfc_disc_ndlp_show(phba, ndlp, buf, len);
	}
	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	return len;
}

static ssize_t
lpfc_disc_map_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	struct lpfc_nodelist  *ndlp, *next_ndlp;
	struct list_head *listp;
	unsigned long iflag;
	int i = 0, len = 0;

	if (!phba) return 0;

	spin_lock_irqsave(phba->host->host_lock, iflag);
	listp = &phba->fc_nlpmap_list;
	if (list_empty(listp)) {
		spin_unlock_irqrestore(phba->host->host_lock, iflag);
		return snprintf(buf, PAGE_SIZE, "Map    list: Empty\n");
	}

	len += snprintf(buf+len, PAGE_SIZE-len, "Map    list: %d Entries\n",
		phba->fc_map_cnt);
	list_for_each_entry_safe(ndlp, next_ndlp, listp, nlp_listp) {
		i++;
		if(i > LPFC_MAX_SYS_DISC_ENTRIES) {
			len += snprintf(buf+len, PAGE_SIZE-len,
			"Missed %d entries - sysfs %ld limit exceeded\n",
			(phba->fc_map_cnt - i + 1), PAGE_SIZE);
			break;
		}
		if(len > (PAGE_SIZE-1))  /* double check */
			break;
		len += lpfc_disc_ndlp_show(phba, ndlp, buf, len);
	}
	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	return len;
}

static ssize_t
lpfc_disc_unmap_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	struct lpfc_nodelist  *ndlp, *next_ndlp;
	struct list_head *listp;
	unsigned long iflag;
	int i = 0, len = 0;

	if (!phba) return 0;

	spin_lock_irqsave(phba->host->host_lock, iflag);
	listp = &phba->fc_nlpunmap_list;
	if (list_empty(listp)) {
		spin_unlock_irqrestore(phba->host->host_lock, iflag);
		return snprintf(buf, PAGE_SIZE, "Unmap  list: Empty\n");
	}

	len += snprintf(buf+len, PAGE_SIZE-len, "Unmap  list: %d Entries\n",
		phba->fc_unmap_cnt);
	list_for_each_entry_safe(ndlp, next_ndlp, listp, nlp_listp) {
		i++;
		if(i > LPFC_MAX_SYS_DISC_ENTRIES) {
			len += snprintf(buf+len, PAGE_SIZE-len,
			"Missed %d entries - sysfs %ld limit exceeded\n",
			(phba->fc_unmap_cnt - i + 1), PAGE_SIZE);
			break;
		}
		if(len > (PAGE_SIZE-1))  /* double check */
			break;
		len += lpfc_disc_ndlp_show(phba, ndlp, buf, len);
	}
	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	return len;
}

static ssize_t
lpfc_disc_prli_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	struct lpfc_nodelist  *ndlp, *next_ndlp;
	struct list_head *listp;
	unsigned long iflag;
	int i = 0, len = 0;

	if (!phba) return 0;

	spin_lock_irqsave(phba->host->host_lock, iflag);
	listp = &phba->fc_prli_list;
	if (list_empty(listp)) {
		spin_unlock_irqrestore(phba->host->host_lock, iflag);
		return snprintf(buf, PAGE_SIZE, "PRLI   list: Empty\n");
	}

	len += snprintf(buf+len, PAGE_SIZE-len, "PRLI   list: %d Entries\n",
		phba->fc_prli_cnt);
	list_for_each_entry_safe(ndlp, next_ndlp, listp, nlp_listp) {
		i++;
		if(i > LPFC_MAX_SYS_DISC_ENTRIES) {
			len += snprintf(buf+len, PAGE_SIZE-len,
			"Missed %d entries - sysfs %ld limit exceeded\n",
			(phba->fc_prli_cnt - i + 1), PAGE_SIZE);
			break;
		}
		if(len > (PAGE_SIZE-1))  /* double check */
			break;
		len += lpfc_disc_ndlp_show(phba, ndlp, buf, len);
	}
	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	return len;
}

static ssize_t
lpfc_disc_reglgn_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	struct lpfc_nodelist  *ndlp, *next_ndlp;
	struct list_head *listp;
	unsigned long iflag;
	int i = 0, len = 0;

	if (!phba) return 0;

	spin_lock_irqsave(phba->host->host_lock, iflag);
	listp = &phba->fc_reglogin_list;
	if (list_empty(listp)) {
		spin_unlock_irqrestore(phba->host->host_lock, iflag);
		return snprintf(buf, PAGE_SIZE, "RegLgn list: Empty\n");
	}

	len += snprintf(buf+len, PAGE_SIZE-len, "RegLgn list: %d Entries\n",
		phba->fc_reglogin_cnt);
	list_for_each_entry_safe(ndlp, next_ndlp, listp, nlp_listp) {
		i++;
		if(i > LPFC_MAX_SYS_DISC_ENTRIES) {
			len += snprintf(buf+len, PAGE_SIZE-len,
			"Missed %d entries - sysfs %ld limit exceeded\n",
			(phba->fc_reglogin_cnt - i + 1), PAGE_SIZE);
			break;
		}
		if(len > (PAGE_SIZE-1))  /* double check */
			break;
		len += lpfc_disc_ndlp_show(phba, ndlp, buf, len);
	}
	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	return len;
}

static ssize_t
lpfc_disc_adisc_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	struct lpfc_nodelist  *ndlp, *next_ndlp;
	struct list_head *listp;
	unsigned long iflag;
	int i = 0, len = 0;

	if (!phba) return 0;

	spin_lock_irqsave(phba->host->host_lock, iflag);
	listp = &phba->fc_adisc_list;
	if (list_empty(listp)) {
		spin_unlock_irqrestore(phba->host->host_lock, iflag);
		return snprintf(buf, PAGE_SIZE, "ADISC  list: Empty\n");
	}

	len += snprintf(buf+len, PAGE_SIZE-len, "ADISC  list: %d Entries\n",
		phba->fc_adisc_cnt);
	list_for_each_entry_safe(ndlp, next_ndlp, listp, nlp_listp) {
		i++;
		if(i > LPFC_MAX_SYS_DISC_ENTRIES) {
			len += snprintf(buf+len, PAGE_SIZE-len,
			"Missed %d entries - sysfs %ld limit exceeded\n",
			(phba->fc_adisc_cnt - i + 1), PAGE_SIZE);
			break;
		}
		if(len > (PAGE_SIZE-1))  /* double check */
			break;
		len += lpfc_disc_ndlp_show(phba, ndlp, buf, len);
	}
	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	return len;
}

static ssize_t
lpfc_disc_plogi_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	struct lpfc_nodelist  *ndlp, *next_ndlp;
	struct list_head *listp;
	unsigned long iflag;
	int i = 0, len = 0;

	if (!phba) return 0;

	spin_lock_irqsave(phba->host->host_lock, iflag);
	listp = &phba->fc_plogi_list;
	if (list_empty(listp)) {
		spin_unlock_irqrestore(phba->host->host_lock, iflag);
		return snprintf(buf, PAGE_SIZE, "PLOGI  list: Empty\n");
	}

	len += snprintf(buf+len, PAGE_SIZE-len, "PLOGI  list: %d Entries\n",
		phba->fc_plogi_cnt);
	list_for_each_entry_safe(ndlp, next_ndlp, listp, nlp_listp) {
		i++;
		if(i > LPFC_MAX_SYS_DISC_ENTRIES) {
			len += snprintf(buf+len, PAGE_SIZE-len,
			"Missed %d entries - sysfs %ld limit exceeded\n",
			(phba->fc_plogi_cnt - i + 1), PAGE_SIZE);
			break;
		}
		if(len > (PAGE_SIZE-1))  /* double check */
			break;
		len += lpfc_disc_ndlp_show(phba, ndlp, buf, len);
	}
	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	return len;
}

static ssize_t
lpfc_disc_unused_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	struct lpfc_nodelist  *ndlp, *next_ndlp;
	struct list_head *listp;
	unsigned long iflag;
	int i = 0, len = 0;

	if (!phba) return 0;

	spin_lock_irqsave(phba->host->host_lock, iflag);
	listp = &phba->fc_unused_list;
	if (list_empty(listp)) {
		spin_unlock_irqrestore(phba->host->host_lock, iflag);
		return snprintf(buf, PAGE_SIZE, "Unused list: Empty\n");
	}

	len += snprintf(buf+len, PAGE_SIZE-len, "Unused list: %d Entries\n",
		phba->fc_unused_cnt);
	list_for_each_entry_safe(ndlp, next_ndlp, listp, nlp_listp) {
		i++;
		if(i > LPFC_MAX_SYS_DISC_ENTRIES) {
			len += snprintf(buf+len, PAGE_SIZE-len,
			"Missed %d entries - sysfs %ld limit exceeded\n",
			(phba->fc_unused_cnt - i + 1), PAGE_SIZE);
			break;
		}
		if(len > (PAGE_SIZE-1))  /* double check */
			break;
		len += lpfc_disc_ndlp_show(phba, ndlp, buf, len);
	}
	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	return len;
}

#define LPFC_MAX_SYS_OUTFCPIO_ENTRIES 50

static ssize_t
lpfc_outfcpio_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	struct lpfc_sli      *psli;
	struct lpfc_sli_ring *pring;
	struct lpfc_target   *targetp;
	struct lpfc_nodelist *ndlp;
	struct lpfc_scsi_buf *lpfc_cmd;
	struct list_head *curr, *next;
	struct lpfc_iocbq *iocb;
	struct lpfc_iocbq *next_iocb;
	IOCB_t *cmd;
	unsigned long iflag;
	int i = 0, len = 0;
	int cnt = 0, unused = 0, total = 0;
	int tx_count, txcmpl_count;

	if (!phba) return 0;
	psli = &phba->sli;
	pring = &psli->ring[psli->fcp_ring];


	spin_lock_irqsave(phba->host->host_lock, iflag);

	for(i=0;i<LPFC_MAX_TARGET;i++) {
		targetp = phba->device_queue_hash[i];
		if(targetp) {
			if(cnt >= LPFC_MAX_SYS_OUTFCPIO_ENTRIES) {
				unused++;
				continue;
			}
			cnt++;
			len += snprintf(buf+len, PAGE_SIZE-len,
				"ID %03d:qcmd %08x done %08x err %08x ",
				targetp->scsi_id, targetp->qcmdcnt,
				targetp->iodonecnt, targetp->errorcnt);
			total += (targetp->qcmdcnt - targetp->iodonecnt);

			tx_count = 0;
			txcmpl_count = 0;

			/* Count I/Os on txq and txcmplq. */
			list_for_each_safe(curr, next, &pring->txq) {
				next_iocb = list_entry(curr, struct lpfc_iocbq,
					list);
				iocb = next_iocb;
				cmd = &iocb->iocb;

				/* Must be a FCP command */
				if ((cmd->ulpCommand != CMD_FCP_ICMND64_CR) &&
				    (cmd->ulpCommand != CMD_FCP_IWRITE64_CR) &&
				    (cmd->ulpCommand != CMD_FCP_IREAD64_CR)) {
					continue;
				}

				/* context1 MUST be a struct lpfc_scsi_buf */
				lpfc_cmd =
				    (struct lpfc_scsi_buf *) (iocb->context1);
				if ((lpfc_cmd == 0)
				    || (lpfc_cmd->pCmd->device->id !=
					targetp->scsi_id)) {
					continue;
				}
				tx_count++;
			}

			/* Next check the txcmplq */
			list_for_each_safe(curr, next, &pring->txcmplq) {
				next_iocb = list_entry(curr, struct lpfc_iocbq,
					list);
				iocb = next_iocb;
				cmd = &iocb->iocb;

				/* Must be a FCP command */
				if ((cmd->ulpCommand != CMD_FCP_ICMND64_CR) &&
				    (cmd->ulpCommand != CMD_FCP_IWRITE64_CR) &&
				    (cmd->ulpCommand != CMD_FCP_IREAD64_CR)) {
					continue;
				}

				/* context1 MUST be a struct lpfc_scsi_buf */
				lpfc_cmd =
				    (struct lpfc_scsi_buf *) (iocb->context1);
				if ((lpfc_cmd == 0)
				    || (lpfc_cmd->pCmd->device->id !=
					targetp->scsi_id)) {
					continue;
				}

				txcmpl_count++;
			}
			len += snprintf(buf+len, PAGE_SIZE-len,
				"tx %04x txcmpl %04x ",
				tx_count, txcmpl_count);

			ndlp = targetp->pnode;
			if(ndlp == NULL) {
				len += snprintf(buf+len, PAGE_SIZE-len,
					"DISAPPERED\n");
			}
			else {
				if(ndlp->nlp_state == NLP_STE_MAPPED_NODE) {
					len += snprintf(buf+len, PAGE_SIZE-len,
						"MAPPED\n");
				}
				else {
					len += snprintf(buf+len, PAGE_SIZE-len,
						"RECOVERY (%d)\n",
						ndlp->nlp_state);
				}
			}
		}
		if(len > (PAGE_SIZE-1))  /* double check */
			break;
	}
	if(unused) {
		len += snprintf(buf+len, PAGE_SIZE-len,
		"Missed x%x entries - sysfs %ld limit exceeded\n",
		unused, PAGE_SIZE);
	}
	len += snprintf(buf+len, PAGE_SIZE-len,
		"x%x total I/Os outstanding\n", total);

	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	return len;
}

#define lpfc_param_show(attr)	\
static ssize_t \
lpfc_##attr##_show(struct class_device *cdev, char *buf) \
{ \
 	struct Scsi_Host *host = class_to_shost(cdev);\
 	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];\
 	int val = 0;\
 	if (phba){\
 		val = phba->cfg_##attr;\
 		return snprintf(buf, PAGE_SIZE, "%d\n",\
 				phba->cfg_##attr);\
 	}\
 	return 0;\
}

#define lpfc_param_store(attr, minval, maxval)	\
static ssize_t \
lpfc_##attr##_store(struct class_device *cdev, const char *buf, size_t count) \
{ \
 	struct Scsi_Host *host = class_to_shost(cdev);\
 	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];\
 	int val=0;\
 	if (sscanf(buf, "%d", &val) != 1)\
 		return 0;\
 	if (phba){\
 		if (val >= minval && val <= maxval) {\
 			phba->cfg_##attr = val;\
 			return strlen(buf);\
 		}\
 	}\
 	return 0;\
}

#define LPFC_ATTR_R_NOINIT(name, desc) \
extern int lpfc_##name;\
module_param(lpfc_##name, int, 0);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_param_show(name)\
static CLASS_DEVICE_ATTR(lpfc_##name, S_IRUGO , lpfc_##name##_show, NULL)

#define LPFC_ATTR_R(name, defval, minval, maxval, desc) \
static int lpfc_##name = defval;\
module_param(lpfc_##name, int, 0);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_param_show(name)\
static CLASS_DEVICE_ATTR(lpfc_##name, S_IRUGO , lpfc_##name##_show, NULL)

#define LPFC_ATTR_RW(name, defval, minval, maxval, desc) \
static int lpfc_##name = defval;\
module_param(lpfc_##name, int, 0);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_param_show(name)\
lpfc_param_store(name, minval, maxval)\
static CLASS_DEVICE_ATTR(lpfc_##name, S_IRUGO | S_IWUSR,\
			 lpfc_##name##_show, lpfc_##name##_store)

static CLASS_DEVICE_ATTR(info, S_IRUGO, lpfc_info_show, NULL);
static CLASS_DEVICE_ATTR(serialnum, S_IRUGO, lpfc_serialnum_show, NULL);
static CLASS_DEVICE_ATTR(fwrev, S_IRUGO, lpfc_fwrev_show, NULL);
static CLASS_DEVICE_ATTR(hdw, S_IRUGO, lpfc_hdw_show, NULL);
static CLASS_DEVICE_ATTR(state, S_IRUGO, lpfc_state_show, NULL);
static CLASS_DEVICE_ATTR(option_rom_version, S_IRUGO,
					lpfc_option_rom_version_show, NULL);
static CLASS_DEVICE_ATTR(num_discovered_ports, S_IRUGO,
					lpfc_num_discovered_ports_show, NULL);
#ifndef FC_TRANS_VER2
static CLASS_DEVICE_ATTR(speed, S_IRUGO, lpfc_speed_show, NULL);
static CLASS_DEVICE_ATTR(node_name, S_IRUGO, lpfc_node_name_show, NULL);
static CLASS_DEVICE_ATTR(port_name, S_IRUGO, lpfc_port_name_show, NULL);
static CLASS_DEVICE_ATTR(portfcid, S_IRUGO, lpfc_did_show, NULL);
static CLASS_DEVICE_ATTR(port_type, S_IRUGO, lpfc_port_type_show, NULL);
static CLASS_DEVICE_ATTR(fabric_name, S_IRUGO, lpfc_fabric_name_show, NULL);
#endif /* FC_TRANS_VER2 */
static CLASS_DEVICE_ATTR(events, S_IRUGO, lpfc_events_show, NULL);
static CLASS_DEVICE_ATTR(nport_evt_cnt, S_IRUGO, lpfc_nport_evt_cnt_show, NULL);
static CLASS_DEVICE_ATTR(lpfc_drvr_version, S_IRUGO, lpfc_drvr_version_show,
			 NULL);
static CLASS_DEVICE_ATTR(management_version, S_IRUGO, management_version_show,
			 NULL);
static CLASS_DEVICE_ATTR(issue_lip, S_IWUSR, NULL, lpfc_issue_lip);
static CLASS_DEVICE_ATTR(board_online, S_IRUGO | S_IWUSR,
			 lpfc_board_online_show, lpfc_board_online_store);

static CLASS_DEVICE_ATTR(disc_npr, S_IRUGO, lpfc_disc_npr_show, NULL);
static CLASS_DEVICE_ATTR(disc_map, S_IRUGO, lpfc_disc_map_show, NULL);
static CLASS_DEVICE_ATTR(disc_unmap, S_IRUGO, lpfc_disc_unmap_show, NULL);
static CLASS_DEVICE_ATTR(disc_prli, S_IRUGO, lpfc_disc_prli_show, NULL);
static CLASS_DEVICE_ATTR(disc_reglgn, S_IRUGO, lpfc_disc_reglgn_show, NULL);
static CLASS_DEVICE_ATTR(disc_adisc, S_IRUGO, lpfc_disc_adisc_show, NULL);
static CLASS_DEVICE_ATTR(disc_plogi, S_IRUGO, lpfc_disc_plogi_show, NULL);
static CLASS_DEVICE_ATTR(disc_unused, S_IRUGO, lpfc_disc_unused_show, NULL);
static CLASS_DEVICE_ATTR(outfcpio, S_IRUGO, lpfc_outfcpio_show, NULL);

/*
# lpfc_log_verbose: Only turn this flag on if you are willing to risk being
# deluged with LOTS of information.
# You can set a bit mask to record specific types of verbose messages:
#
# LOG_ELS                       0x1        ELS events
# LOG_DISCOVERY                 0x2        Link discovery events
# LOG_MBOX                      0x4        Mailbox events
# LOG_INIT                      0x8        Initialization events
# LOG_LINK_EVENT                0x10       Link events
# LOG_IP                        0x20       IP traffic history
# LOG_FCP                       0x40       FCP traffic history
# LOG_NODE                      0x80       Node table events
# LOG_MISC                      0x400      Miscellaneous events
# LOG_SLI                       0x800      SLI events
# LOG_CHK_COND                  0x1000     FCP Check condition flag
# LOG_LIBDFC                    0x2000     LIBDFC events
# LOG_ALL_MSG                   0xffff     LOG all messages
*/
LPFC_ATTR_RW(log_verbose, 0x0, 0x0, 0xffff, "Verbose logging bit-mask");

/*
# lun_queue_depth:  This parameter is used to limit the number of outstanding
# commands per FCP LUN. Value range is [1,128]. Default value is 30.
*/
LPFC_ATTR_R(lun_queue_depth, 30, 1, 128,
	    "Max number of FCP commands we can queue to a specific LUN");

/*
# Some disk devices have a "select ID" or "select Target" capability.
# From a protocol standpoint "select ID" usually means select the
# Fibre channel "ALPA".  In the FC-AL Profile there is an "informative
# annex" which contains a table that maps a "select ID" (a number
# between 0 and 7F) to an ALPA.  By default, for compatibility with
# older drivers, the lpfc driver scans this table from low ALPA to high
# ALPA.
#
# Turning on the scan-down variable (on  = 1, off = 0) will
# cause the lpfc driver to use an inverted table, effectively
# scanning ALPAs from high to low. Value range is [0,1]. Default value is 1.
#
# (Note: This "select ID" functionality is a LOOP ONLY characteristic
# and will not work across a fabric. Also this parameter will take
# effect only in the case when ALPA map is not available.)
*/
LPFC_ATTR_RW(scan_down, 1, 0, 1,
	     "Start scanning for devices from highest ALPA to lowest");

/*
# lpfc_nodev_tmo: If set, it will hold all I/O errors on devices that disappear
# until the timer expires. Value range is [0,255]. Default value is 20.
# NOTE: this MUST be less then the SCSI Layer command timeout - 1.
*/
LPFC_ATTR_RW(nodev_tmo, 20, 0, 255,
	     "Seconds driver will hold I/O waiting for a device to come back");

/*
# lpfc_topology:  link topology for init link
#            0x0  = attempt loop mode then point-to-point
#            0x02 = attempt point-to-point mode only
#            0x04 = attempt loop mode only
#            0x06 = attempt point-to-point mode then loop
# Set point-to-point mode if you want to run as an N_Port.
# Set loop mode if you want to run as an NL_Port. Value range is [0,0x6].
# Default value is 0.
*/
LPFC_ATTR_R(topology, 0, 0, 6, "Select Fibre Channel topology");

/*
# lpfc_link_speed: Link speed selection for initializing the Fibre Channel
# connection.
#       0 = auto select (default)
#       1 = 1 Gigabaud
#       2 = 2 Gigabaud
#       4 = 10 Gigabaud
#       8 = 4 Gigabaud
# Value range is [0,8]. Default value is 0.
*/
LPFC_ATTR_R(link_speed, 0, 0, 8, "Select link speed");

/*
# lpfc_fcp_class:  Determines FC class to use for the FCP protocol.
# Value range is [2,3]. Default value is 3.
*/
LPFC_ATTR_R(fcp_class, 3, 2, 3,
	     "Select Fibre Channel class of service for FCP sequences");

/*
# lpfc_use_adisc: Use ADISC for FCP rediscovery instead of PLOGI. Value range
# is [0,1]. Default value is 0.
*/
LPFC_ATTR_RW(use_adisc, 0, 0, 1,
	     "Use ADISC on rediscovery to authenticate FCP devices");

/*
# lpfc_ack0: Use ACK0, instead of ACK1 for class 2 acknowledgement. Value
# range is [0,1]. Default value is 0.
*/
LPFC_ATTR_R(ack0, 0, 0, 1, "Enable ACK0 support");

/*
# If automap is set, SCSI IDs for all FCP nodes without
# consistent bindings will be automatically generated.
# If new FCP devices are added to the network when the system is down,
# there is no guarantee that these SCSI IDs will remain the same
# when the system is booted again.
# The bind method of the port is used as the binding method of
# automap devices to preserve SCSI IDs between link down and link up.
# If automap is 0, only devices with consistent bindings will be
# recognized by the system. User can change the automap property
# of port instance X by changing the value of lpfcX_automap parameter.
# Value range is [0,1]. Default value is 1.
*/
LPFC_ATTR_RW(automap, 1, 0, 1,
	    "Automatically bind FCP devices as they are discovered");

/*
# lpfc_fcp_bind_method: It specifies the method of binding to be used for each
# port. This  binding method is used for consistent binding and automaped
# binding. A value of 1 will force WWNN binding, value of 2 will force WWPN
# binding, value of 3 will force DID binding and value of 4 will force the
# driver to derive binding from ALPA. Any consistent binding whose type does
# not match with the bind method of the port will be ignored. Value range
# is [1,4]. Default value is 2.
*/
LPFC_ATTR_RW(fcp_bind_method, 2, 0, 4,
	    "Select the bind method to be used");

/*
# lpfc_cr_delay & lpfc_cr_count: Default values for I/O colaesing
# cr_delay (msec) or cr_count outstanding commands. cr_delay can take
# value [0,63]. cr_count can take value [0,255]. Default value of cr_delay
# is 0. Default value of cr_count is 0. The cr_count feature is disabled if
# cr_delay is set to 0.
*/
static int lpfc_cr_delay = 0;
module_param(lpfc_cr_delay, int , 0);
MODULE_PARM_DESC(lpfc_cr_delay, "A count of milliseconds after which an"
		"interrupt response is generated");

static int lpfc_cr_count = 1;
module_param(lpfc_cr_count, int, 0);
MODULE_PARM_DESC(lpfc_cr_count, "A count of I/O completions after which an"
		"interrupt response is generated");

/*
# lpfc_fdmi_on: controls FDMI support.
#       0 = no FDMI support
#       1 = support FDMI without attribute of hostname
#       2 = support FDMI with attribute of hostname
# Value range [0,2]. Default value is 0.
*/
LPFC_ATTR_RW(fdmi_on, 0, 0, 2, "Enable FDMI support");

/*
# Specifies the maximum number of ELS cmds we can have outstanding (for
# discovery). Value range is [1,64]. Default value = 1.
*/
static int lpfc_discovery_threads = 1;
module_param(lpfc_discovery_threads, int, 0);
MODULE_PARM_DESC(lpfc_discovery_threads, "Maximum number of ELS commands"
		 "during discovery");


static int
dfc_rsp_data_copy(struct lpfc_hba * phba,  uint8_t * outdataptr,
					 DMABUFEXT_t * mlist, uint32_t size)
{
	DMABUFEXT_t *mlast = NULL;
	int cnt, offset = 0;
	struct list_head head, *curr, *next;

	if (!mlist) /* FIX ME - fix the return values */
		return 0;

	list_add_tail(&head, &mlist->dma.list);

	list_for_each_safe(curr, next, &head) {
		mlast = list_entry(curr, DMABUFEXT_t , dma.list);
		if (!size)
			break;

		/* We copy chunks of 4K */
		cnt = size > 4096 ? 4096: size;

		if (outdataptr) {
			pci_dma_sync_single_for_device(phba->pcidev,
			    mlast->dma.phys, LPFC_BPL_SIZE, PCI_DMA_TODEVICE);

			 memcpy(outdataptr + offset,
						mlast->dma.virt, cnt);
		}
		offset += cnt;
		size -= cnt;
	}
	list_del(&head);
	return 0;
}

static int
dfc_cmd_data_free(struct lpfc_hba * phba, DMABUFEXT_t * mlist)
{
	DMABUFEXT_t *mlast;
	struct pci_dev *pcidev = phba->pcidev;
	struct list_head head, *curr, *next;

	if (!mlist) /* FIX ME - need different return value */
		return 0;

	list_add_tail(&head, &mlist->dma.list);

	list_for_each_safe(curr, next, &head) {
		mlast = list_entry(curr, DMABUFEXT_t , dma.list);
		if (mlast->dma.virt) {
			dma_free_coherent(&pcidev->dev,
					  mlast->size,
					  mlast->dma.virt,
					  mlast->dma.phys);

		}
		kfree(mlast);
	}
	return 0;
}

static DMABUFEXT_t *
dfc_cmd_data_alloc(struct lpfc_hba * phba, char *indataptr,
					 struct ulp_bde64 * bpl, uint32_t size)
{
	DMABUFEXT_t *mlist = NULL, *dmp;
	int cnt, offset = 0, i = 0, rc = 0;
	struct pci_dev *pcidev = phba->pcidev;

	while (size) {
		cnt = size > 4096 ? 4096: size;

		dmp = kmalloc(sizeof (DMABUFEXT_t), GFP_KERNEL);
		if (!dmp)
			goto dfc_cmd_data_alloc_exit;

		INIT_LIST_HEAD(&dmp->dma.list);

		if (mlist)
			list_add_tail(&dmp->dma.list, &mlist->dma.list);
		else
			mlist = dmp;

		dmp->dma.virt = dma_alloc_coherent(&pcidev->dev,
						   cnt,
						   &(dmp->dma.phys),
						   GFP_KERNEL);

		if (!dmp->dma.virt) /* FIX ME - who free's the list ?*/
			goto dfc_cmd_data_alloc_free_dmp;

		dmp->size = cnt;

		if (!indataptr) {
			bpl->tus.f.bdeFlags = BUFF_USE_RCV;
		} else {

			/*rc = copy_from_user(dmp->dma.virt, indataptr + offset,
									cnt);*/
			memcpy(dmp->dma.virt, indataptr+offset, cnt);
			if (rc) /* FIX ME - who free's the list ?*/
				goto dfc_cmd_data_alloc_free_dmp;
			bpl->tus.f.bdeFlags = 0;

			pci_dma_sync_single_for_device(phba->pcidev,
				dmp->dma.phys, LPFC_BPL_SIZE, PCI_DMA_TODEVICE);
		}

		/* build buffer ptr list for IOCB */
		bpl->addrLow = le32_to_cpu( putPaddrLow(dmp->dma.phys) );
		bpl->addrHigh = le32_to_cpu( putPaddrHigh(dmp->dma.phys) );
		bpl->tus.f.bdeSize = (ushort) cnt;
		bpl->tus.w = le32_to_cpu(bpl->tus.w);
		bpl++;

		i++;
		offset += cnt;
		size -= cnt;
	}

	mlist->flag = i;
	return (mlist);
dfc_cmd_data_alloc_free_dmp:
	kfree(dmp);
dfc_cmd_data_alloc_exit:
	dfc_cmd_data_free(phba, mlist);
	return NULL;
}

#ifdef DFC_DEBUG
static ssize_t
sysfs_ctpass_write(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct Scsi_Host *host = class_to_shost(container_of(kobj,
						struct class_device, kobj));
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	struct lpfc_nodelist *pndl;
	struct ulp_bde64 *bpl;
	struct lpfc_iocbq *cmdiocbq = NULL, *rspiocbq = NULL;
	DMABUFEXT_t *indmp = NULL, *outdmp = NULL;
	IOCB_t *cmd = NULL, *rsp = NULL;
	struct lpfc_dmabuf *bmp = NULL;
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring = &psli->ring[LPFC_ELS_RING];
	int i, rc = -4;
	int reqbfrcnt, snsbfrcnt;
	uint32_t timeout;
	unsigned long iflag;
	uint32_t portid;      /* Port to send this to. */
	typedef struct tagctpassthruinput {
		uint32_t portid;
		uint32_t reqsize;
		uint32_t rspsize;
	} ctpassthruinput_t;

	spin_lock_irqsave(phba->host->host_lock, iflag);

	reqbfrcnt = ((ctpassthruinput_t*)buf)->reqsize;
	snsbfrcnt = ((ctpassthruinput_t*)buf)->rspsize;
	portid = ((ctpassthruinput_t*)buf)->portid;

	if((portid & CT_DID_MASK) != CT_DID_MASK)
		goto ctpassthru_exit;

	if (!reqbfrcnt || !snsbfrcnt ||
		(reqbfrcnt > PAGE_SIZE - sizeof(ctpassthruinput_t)) ||
		(snsbfrcnt > PAGE_SIZE)) {
		rc = -ERANGE;
		goto ctpassthru_exit;
	}

	pndl = lpfc_findnode_did(phba, NLP_SEARCH_MAPPED | NLP_SEARCH_UNMAPPED,
								 portid);
	if(!pndl || pndl->nlp_flag & NLP_ELS_SND_MASK) {
		rc = -ENODEV;
		goto ctpassthru_exit;
	}

	if (!(psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE)) {
		rc = -EACCES;
		goto ctpassthru_exit;
	}

	cmdiocbq = mempool_alloc(phba->iocb_mem_pool, GFP_ATOMIC);
	if (!cmdiocbq) {
		rc = -ENOMEM;
		goto ctpassthru_exit;
	}
	memset(cmdiocbq, 0, sizeof (struct lpfc_iocbq));
	cmd = &cmdiocbq->iocb;

	rspiocbq = mempool_alloc(phba->iocb_mem_pool, GFP_ATOMIC);
	if (!rspiocbq) {
		rc = -ENOMEM;
		goto ctpassthru_freecmdiocbq;
	}
	memset(rspiocbq, 0, sizeof (struct lpfc_iocbq));
	rsp = &rspiocbq->iocb;

	bmp = kmalloc(sizeof (struct lpfc_dmabuf), GFP_ATOMIC);
	if (!bmp) {
		rc = -ENOMEM;
		goto ctpassthru_freerspiocbq;
	}

	bmp->virt = lpfc_mbuf_alloc(phba, 0, &bmp->phys);
	if (!bmp->virt) {
		rc = -ENOMEM;
		goto ctpassthru_freebmp;
	}

	INIT_LIST_HEAD(&bmp->list);
	bpl = (struct ulp_bde64 *) bmp->virt;
	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	indmp = dfc_cmd_data_alloc(phba, buf + sizeof(ctpassthruinput_t), bpl,
								reqbfrcnt);
	spin_lock_irqsave(phba->host->host_lock, iflag);
	if (!indmp) {
		rc = -ENOMEM;
		goto ctpassthru_freembuf;
	}

	bpl += indmp->flag; /* flag contains total number of BPLs for xmit */

	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	outdmp = dfc_cmd_data_alloc(phba, NULL, bpl, snsbfrcnt);
	spin_lock_irqsave(phba->host->host_lock, iflag);
	if (!outdmp) {
		rc = -ENOMEM;
		goto ctpassthru_free_indmp;
	}
	outdmp->data = snsbfrcnt;
	outdmp->uniqueid = current_thread_info()->task->pid;

	cmd->un.genreq64.bdl.ulpIoTag32 = 0;
	cmd->un.genreq64.bdl.addrHigh = putPaddrHigh(bmp->phys);
	cmd->un.genreq64.bdl.addrLow = putPaddrLow(bmp->phys);
	cmd->un.genreq64.bdl.bdeFlags = BUFF_TYPE_BDL;
	cmd->un.genreq64.bdl.bdeSize =
	    (outdmp->flag + indmp->flag) * sizeof (struct ulp_bde64);
	cmd->ulpCommand = CMD_GEN_REQUEST64_CR;
	cmd->un.genreq64.w5.hcsw.Fctl = (SI | LA);
	cmd->un.genreq64.w5.hcsw.Dfctl = 0;
	cmd->un.genreq64.w5.hcsw.Rctl = FC_UNSOL_CTL;
	cmd->un.genreq64.w5.hcsw.Type = FC_COMMON_TRANSPORT_ULP;
	cmd->ulpIoTag = lpfc_sli_next_iotag(phba, pring);
	cmd->ulpTimeout = 5;
	cmd->ulpBdeCount = 1;
	cmd->ulpLe = 1;
	cmd->ulpClass = CLASS3;
	cmd->ulpContext = pndl->nlp_rpi;
	cmd->ulpOwner = OWN_CHIP;
	cmdiocbq->context1 = NULL;
	cmdiocbq->context2 = NULL;
	cmdiocbq->iocb_flag |= LPFC_IO_LIBDFC;

	if (cmd->ulpTimeout < (phba->fc_ratov * 2 + LPFC_DRVR_TIMEOUT))
		timeout = phba->fc_ratov * 2 + LPFC_DRVR_TIMEOUT;
	else
		timeout = cmd->ulpTimeout;

	for (rc = -1, i = 0; i < 4 && rc != IOCB_SUCCESS; i++) {
		spin_unlock_irqrestore(phba->host->host_lock, iflag);
		rc = lpfc_sli_issue_iocb_wait(phba, pring, cmdiocbq,
					      rspiocbq, timeout);
		spin_lock_irqsave(phba->host->host_lock, iflag);
	}

	if (rc != IOCB_SUCCESS) {
		rc = -EACCES;
		goto ctpassthru_free_outdmp;
	}

	if (!rsp->ulpStatus) {
		outdmp->flag = rsp->un.genreq64.bdl.bdeSize;
	} else {
		if (rsp->ulpStatus == IOSTAT_LOCAL_REJECT) {
			switch (rsp->un.ulpWord[4] & 0xff) {
			case IOERR_SEQUENCE_TIMEOUT:
				rc = -ETIMEDOUT;
				break;
			case IOERR_INVALID_RPI:
				rc = -EFAULT;
				break;
			default:
				rc = -EACCES;
				break;
			}
			goto ctpassthru_free_outdmp;
		}
	}
	if (outdmp->flag > snsbfrcnt) {
		rc = -ERANGE; /* C_CT Request error */
		lpfc_printf_log(phba,
				KERN_INFO,
				LOG_LIBDFC,
			       "%d:1208 C_CT Request error Data: x%x x%x\n",
				phba->brd_no,
			       outdmp->flag, 4096);
		goto ctpassthru_free_outdmp;
	}
	list_add(&outdmp->list, &phba->ctrspbuflist);
	rc = reqbfrcnt;
	goto ctpassthru_free_indmp;

ctpassthru_free_outdmp:
	dfc_cmd_data_free(phba, outdmp);
ctpassthru_free_indmp:
	dfc_cmd_data_free(phba, indmp);
ctpassthru_freembuf:
	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
ctpassthru_freebmp:
	kfree(bmp);
ctpassthru_freerspiocbq:
	mempool_free(rspiocbq, phba->iocb_mem_pool);
ctpassthru_freecmdiocbq:
	mempool_free(cmdiocbq, phba->iocb_mem_pool);
ctpassthru_exit:
	spin_unlock_irqrestore(phba->host->host_lock, iflag); /* remove */
	return rc;
}

static ssize_t
sysfs_ctpass_read(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	int rc = -EIO, uniqueid;
	struct Scsi_Host *host = class_to_shost(container_of(kobj,
						struct class_device, kobj));
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	DMABUFEXT_t *outdmp = NULL, *tmpoutdmp;

	uniqueid = current_thread_info()->task->pid;

	list_for_each_entry_safe(outdmp, tmpoutdmp, &phba->ctrspbuflist, list){
		if (outdmp->uniqueid == uniqueid) {
			dfc_rsp_data_copy(phba, (uint8_t*)buf, outdmp,
								outdmp->data);

			rc = outdmp->flag;
			list_del(&outdmp->list);
			dfc_cmd_data_free(phba, outdmp);
			break;

		}
	}
	return rc;
}
static struct bin_attribute sysfs_ctpass_attr = {
	.attr = {
		.name = "ctpass",
		.mode = S_IRUSR | S_IWUSR,
		.owner = THIS_MODULE,
	},
	.size = 0,
	.read = sysfs_ctpass_read,
	.write = sysfs_ctpass_write,
};


static ssize_t
sysfs_sendrnid_write(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct Scsi_Host *host = class_to_shost(container_of(kobj,
						struct class_device, kobj));
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_sli_ring *pring = &psli->ring[LPFC_ELS_RING];
	struct lpfc_name idn;
	struct lpfc_iocbq *cmdiocbq = NULL, *rspiocbq = NULL;
	RNID *prsp;
	uint32_t *pcmd, *psta;
	IOCB_t *rsp;
	void *context2;
	unsigned long iflag;
	int rtnbfrsiz, i, rc = 0;
	struct lpfc_nodelist *pndl;
	uint32_t wwntype;
	struct rnidrsp *rspbuf;

	rspbuf = kmalloc(sizeof (rspbuf), GFP_KERNEL);
	if (!rspbuf)
		return -ENOMEM;

	memcpy(&idn, buf, sizeof(struct lpfc_name));
	sscanf(buf + sizeof(struct lpfc_name), "%d", &wwntype);

	spin_lock_irqsave(phba->host->host_lock, iflag); /* remove */

	if (wwntype)
		pndl = lpfc_findnode_wwpn(phba, NLP_SEARCH_MAPPED |
			 NLP_SEARCH_UNMAPPED, &idn);
	else
		pndl = lpfc_findnode_wwnn(phba, NLP_SEARCH_MAPPED |
			NLP_SEARCH_UNMAPPED, &idn);

	if (!pndl) {
		rc = -ENODEV;
		goto sendrnid_exit;
	}

	if ((pndl->nlp_flag & NLP_ELS_SND_MASK) == NLP_RNID_SND) {
		rc = -EACCES;
		goto sendrnid_exit;
	}

	cmdiocbq = lpfc_prep_els_iocb(phba, 1, 2 * sizeof (uint32_t), 0, pndl,
								ELS_CMD_RNID);
	if (!cmdiocbq) {
		rc = -ENOMEM;
		goto sendrnid_exit;
	}

	/*********************************************************************/
	/*  context2 is used by prep/free to locate cmd and rsp buffers,     */
	/*  but context2 is also used by iocb_wait to hold a rspiocb ptr, so */
	/*  the rsp iocbq can be returned from the completion routine for    */
	/*  iocb_wait, so, save the prep/free value locally ... it will be   */
	/*  restored after returning from iocb_wait.                         */
	/*********************************************************************/
	context2 = cmdiocbq->context2;	/* needed to use lpfc_els_free_iocb */

	rspiocbq = mempool_alloc(phba->iocb_mem_pool, GFP_ATOMIC);
	if (!rspiocbq) {
		rc = -ENOMEM;
		goto sendrnid_freecmdiocbq;
	}
	memset(rspiocbq, 0, sizeof (struct lpfc_iocbq));
	rsp = &rspiocbq->iocb;

	pcmd = ((struct lpfc_dmabuf *) cmdiocbq->context2)->virt;
	*pcmd++ = ELS_CMD_RNID;
	memset(pcmd, 0, sizeof (RNID));	/* fill in RNID payload */
	((RNID *)pcmd)->Format = RNID_TOPOLOGY_DISC;
	cmdiocbq->context1 = NULL;
	cmdiocbq->context2 = NULL;
	cmdiocbq->iocb_flag |= LPFC_IO_LIBDFC;

	for (rc = -1, i = 0; i < 4 && rc != IOCB_SUCCESS; i++) {
		pndl->nlp_flag |= NLP_RNID_SND;
		spin_unlock_irqrestore(phba->host->host_lock, iflag);
		rc = lpfc_sli_issue_iocb_wait(phba, pring, cmdiocbq,
					      rspiocbq,
					      (phba->fc_ratov * 2) +
					      LPFC_DRVR_TIMEOUT);
		spin_lock_irqsave(phba->host->host_lock, iflag);
		pndl->nlp_flag &= ~NLP_RNID_SND;
		cmdiocbq->context2 = context2;
		if (rc == IOCB_ERROR) {
			rc = -EACCES;
			goto sendrnid_freerspiocbq;
		}
	}

	if (rc != IOCB_SUCCESS) {
		rc = -EACCES;
		goto sendrnid_freerspiocbq;
	}

	if (rsp->ulpStatus) {
		rc = -EACCES;
	} else {
		struct lpfc_dmabuf *buf_ptr1, *buf_ptr;
		buf_ptr1 = (struct lpfc_dmabuf *)cmdiocbq->context2;
		buf_ptr = list_entry(buf_ptr1->list.next, struct lpfc_dmabuf,
									list);
		psta = (uint32_t*)buf_ptr->virt;
		if (*psta++ != ELS_CMD_ACC) {
			rc = -EFAULT;
			goto sendrnid_freerspiocbq;
		}
		prsp = (RNID*)psta;	/*  then rnid response data */
		rtnbfrsiz = prsp->CommonLen + prsp->SpecificLen;
		if (rtnbfrsiz > PAGE_SIZE) {
			rc = -EFAULT;
			goto sendrnid_freerspiocbq;
		}
		rspbuf->buf = kmalloc(rtnbfrsiz, GFP_ATOMIC);
		if (!rspbuf->buf) {
			rc = -ENOMEM;
			goto sendrnid_freerspiocbq;
		}

		memcpy(rspbuf->buf, prsp, rtnbfrsiz);
		rspbuf->data = rtnbfrsiz;
		rspbuf->uniqueid = current_thread_info()->task->pid;
		list_add(&rspbuf->list, &phba->rnidrspbuflist);
		rc = rtnbfrsiz;
		goto sendrnid_exit;
	}
sendrnid_freerspiocbq:
	mempool_free(rspiocbq, phba->iocb_mem_pool);
sendrnid_freecmdiocbq:
	lpfc_els_free_iocb(phba, cmdiocbq);
sendrnid_exit:
	spin_unlock_irqrestore(phba->host->host_lock, iflag);
	return rc;
}

static ssize_t
sysfs_sendrnid_read(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	int rc = -EIO, uniqueid;
	struct Scsi_Host *host = class_to_shost(container_of(kobj,
						struct class_device, kobj));
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	struct rnidrsp *outdmp = NULL, *tmpoutdmp;

	uniqueid = current_thread_info()->task->pid;

	list_for_each_entry_safe(outdmp, tmpoutdmp, &phba->rnidrspbuflist,
									list){
		if (outdmp->uniqueid == uniqueid) {
			memcpy(buf, outdmp->buf, outdmp->data);
			rc = outdmp->data;
			kfree(outdmp->buf);
			list_del(&outdmp->list);
			kfree(outdmp);
			break;

		}
	}

	return rc;

}


static struct bin_attribute sysfs_sendrnid_attr = {
	.attr = {
		.name = "sendrnid",
		.mode = S_IWUSR,
		.owner = THIS_MODULE,
	},
	.size = 0,
	.write = sysfs_sendrnid_write,
	.read = sysfs_sendrnid_read,
};

static ssize_t
sysfs_slimem_write(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	unsigned long iflag;
	struct Scsi_Host *host = class_to_shost(container_of(kobj,
						struct class_device, kobj));
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	size_t slim_size;

	if (phba->sli.sliinit.sli_flag & LPFC_SLI2_ACTIVE)
		slim_size = SLI2_SLIM_SIZE;
	else
		slim_size = SLI1_SLIM_SIZE;

	if ((count + off) > slim_size)
		return -ERANGE;

	if (count == 0) return 0;

	if (off % 4 ||  count % 4 || (unsigned long)buf % 4)
		return -EINVAL;

	spin_lock_irqsave(host->host_lock, iflag);

	if (!(phba->fc_flag & FC_OFFLINE_MODE)) {
		if (off >= 256 && off + count <= (256 + 128)) {
			spin_unlock_irqrestore(phba->host->host_lock, iflag);
			return -EPERM;
		}
	}

	if (phba->sli.sliinit.sli_flag & LPFC_SLI2_ACTIVE)
		lpfc_sli_pcimem_bcopy((uint32_t*)buf,
			(uint32_t *)((uint8_t *) phba->slim2p+off), count);
	else
		lpfc_memcpy_to_slim((uint8_t *) phba->slim_memmap_p + off,
				    (void *)buf, count);

	spin_unlock_irqrestore(host->host_lock, iflag);

	return count;
}

static ssize_t
sysfs_slimem_read(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	unsigned long iflag;
	struct Scsi_Host *host = class_to_shost(container_of(kobj,
						struct class_device, kobj));
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	size_t slim_size;

	if (phba->sli.sliinit.sli_flag & LPFC_SLI2_ACTIVE)
		slim_size = SLI2_SLIM_SIZE;
	else
		slim_size = SLI1_SLIM_SIZE;

	if (off > slim_size)
		return -ERANGE;

	if ((count + off) > slim_size)
		count = slim_size - off;

	if (count == 0) return 0;

	if (off % 4 ||  count % 4 || (unsigned long)buf % 4)
		return -EINVAL;

	spin_lock_irqsave(phba->host->host_lock, iflag);

	if (phba->sli.sliinit.sli_flag & LPFC_SLI2_ACTIVE)
		lpfc_sli_pcimem_bcopy((uint32_t *)((uint8_t *) phba->slim2p
		 + off), (uint32_t *)buf, count);
	else
		lpfc_memcpy_from_slim(buf, (uint8_t *)phba->slim_memmap_p + off,
									count);

	spin_unlock_irqrestore(phba->host->host_lock, iflag);

	return count;
}

static struct bin_attribute sysfs_slimem_attr = {
	.attr = {
		.name = "slimem",
		.mode = S_IRUSR | S_IWUSR,
		.owner = THIS_MODULE,
	},
	.size = SLI1_SLIM_SIZE,
	.read = sysfs_slimem_read,
	.write = sysfs_slimem_write,
};
#endif /* DFC_DEBUG */

static ssize_t
sysfs_ctlreg_write(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	unsigned long iflag;
	size_t buf_off;
	struct Scsi_Host *host = class_to_shost(container_of(kobj,
					     struct class_device, kobj));
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];

	if ((off + count) > FF_REG_AREA_SIZE)
		return -ERANGE;

	if (count == 0) return 0;

	if (off % 4 || count % 4 || (unsigned long)buf % 4)
		return -EINVAL;

	spin_lock_irqsave(phba->host->host_lock, iflag);

	if (!(phba->fc_flag & FC_OFFLINE_MODE)) {
		spin_unlock_irqrestore(phba->host->host_lock, iflag);
		return -EPERM;
	}

	for (buf_off = 0; buf_off < count; buf_off += sizeof(uint32_t))
		writel(*((uint32_t *)(buf + buf_off)),
		       (uint8_t *)phba->ctrl_regs_memmap_p + off + buf_off);

	spin_unlock_irqrestore(phba->host->host_lock, iflag);

	return count;
}

static ssize_t
sysfs_ctlreg_read(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	unsigned long iflag;
	size_t buf_off;
	uint32_t * tmp_ptr;
	struct Scsi_Host *host = class_to_shost(container_of(kobj,
					     struct class_device, kobj));
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];

	if (off > FF_REG_AREA_SIZE)
		return -ERANGE;

	if ((off + count) > FF_REG_AREA_SIZE)
		count = FF_REG_AREA_SIZE - off;

	if (count == 0) return 0;

	if (off % 4 || count % 4 || (unsigned long)buf % 4)
		return -EINVAL;

	spin_lock_irqsave(phba->host->host_lock, iflag);

	for (buf_off = 0; buf_off < count; buf_off += sizeof(uint32_t)) {
		tmp_ptr = (uint32_t *)(buf + buf_off);
		*tmp_ptr = readl((uint8_t *)(phba->ctrl_regs_memmap_p
					     + off + buf_off));
	}

	spin_unlock_irqrestore(phba->host->host_lock, iflag);

	return count;
}

static struct bin_attribute sysfs_ctlreg_attr = {
	.attr = {
		.name = "ctlreg",
		.mode = S_IRUSR | S_IWUSR,
		.owner = THIS_MODULE,
	},
	.size = 256,
	.read = sysfs_ctlreg_read,
	.write = sysfs_ctlreg_write,
};


#define MBOX_BUFF_SIZE (MAILBOX_CMD_WSIZE*sizeof(uint32_t))

static void
sysfs_mbox_idle (struct lpfc_hba * phba)
{
	phba->sysfs_mbox.state = SMBOX_IDLE;
	phba->sysfs_mbox.offset = 0;

	if (phba->sysfs_mbox.mbox) {
		mempool_free(phba->sysfs_mbox.mbox,
			     phba->mbox_mem_pool);
		phba->sysfs_mbox.mbox = NULL;
	}
}

static ssize_t
sysfs_mbox_write(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	unsigned long iflag;
	struct Scsi_Host * host =
		class_to_shost(container_of(kobj, struct class_device, kobj));
	struct lpfc_hba * phba = (struct lpfc_hba*)host->hostdata[0];
	struct lpfcMboxq * mbox = NULL;

	if ((count + off) > MBOX_BUFF_SIZE)
		return -ERANGE;

	if (off % 4 ||  count % 4 || (unsigned long)buf % 4)
		return -EINVAL;

	if (count == 0)
		return 0;

	if (off == 0) {
		mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
		if (!mbox)
			return -ENOMEM;

	}

	spin_lock_irqsave(host->host_lock, iflag);

	if (off == 0) {
		if (phba->sysfs_mbox.mbox)
			mempool_free(mbox, phba->mbox_mem_pool);
		else
			phba->sysfs_mbox.mbox = mbox;
		phba->sysfs_mbox.state = SMBOX_WRITING;
	}
	else {
		if (phba->sysfs_mbox.state  != SMBOX_WRITING ||
		    phba->sysfs_mbox.offset != off           ||
		    phba->sysfs_mbox.mbox   == NULL ) {
			sysfs_mbox_idle(phba);
			spin_unlock_irqrestore(host->host_lock, iflag);
			return -EINVAL;
		}
	}

	memcpy((uint8_t *) & phba->sysfs_mbox.mbox->mb + off,
	       buf, count);

	phba->sysfs_mbox.offset = off + count;

	spin_unlock_irqrestore(host->host_lock, iflag);

	return count;
}

static ssize_t
sysfs_mbox_read(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	unsigned long iflag;
	struct Scsi_Host *host =
		class_to_shost(container_of(kobj, struct class_device,
					    kobj));
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	int rc;

	if (off > sizeof(MAILBOX_t))
		return -ERANGE;

	if ((count + off) > sizeof(MAILBOX_t))
		count = sizeof(MAILBOX_t) - off;

	if (off % 4 ||  count % 4 || (unsigned long)buf % 4)
		return -EINVAL;

	if (off && count == 0)
		return 0;

	spin_lock_irqsave(phba->host->host_lock, iflag);

	if (off == 0 &&
	    phba->sysfs_mbox.state  == SMBOX_WRITING &&
	    phba->sysfs_mbox.offset >= 2 * sizeof(uint32_t)) {

		switch (phba->sysfs_mbox.mbox->mb.mbxCommand) {
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
			if (!(phba->fc_flag & FC_OFFLINE_MODE)) {
				printk(KERN_WARNING "mbox_read:Command 0x%x "
				       "is illegal in on-line state\n",
				       phba->sysfs_mbox.mbox->mb.mbxCommand);
				sysfs_mbox_idle(phba);
				spin_unlock_irqrestore(phba->host->host_lock,
						       iflag);
				return -EPERM;
			}
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
		case MBX_READ_SPARM64:
		case MBX_READ_LA:
		case MBX_READ_LA64:
		case MBX_REG_LOGIN:
		case MBX_REG_LOGIN64:
		case MBX_CONFIG_PORT:
		case MBX_RUN_BIU_DIAG:
			printk(KERN_WARNING "mbox_read: Illegal Command 0x%x\n",
			       phba->sysfs_mbox.mbox->mb.mbxCommand);
			sysfs_mbox_idle(phba);
			spin_unlock_irqrestore(phba->host->host_lock,
					       iflag);
			return -EPERM;
		default:
			printk(KERN_WARNING "mbox_read: Unknown Command 0x%x\n",
			       phba->sysfs_mbox.mbox->mb.mbxCommand);
			sysfs_mbox_idle(phba);
			spin_unlock_irqrestore(phba->host->host_lock,
					       iflag);
			return -EPERM;
		}

		if ((phba->fc_flag & FC_OFFLINE_MODE) ||
		    (!(phba->sli.sliinit.sli_flag & LPFC_SLI2_ACTIVE))){

			spin_unlock_irqrestore(phba->host->host_lock, iflag);
			rc = lpfc_sli_issue_mbox (phba,
						  phba->sysfs_mbox.mbox,
						  MBX_POLL);
			spin_lock_irqsave(phba->host->host_lock, iflag);

		} else {
			spin_unlock_irqrestore(phba->host->host_lock, iflag);
			rc = lpfc_sli_issue_mbox_wait (phba,
						       phba->sysfs_mbox.mbox,
						       phba->fc_ratov * 2);
			spin_lock_irqsave(phba->host->host_lock, iflag);
		}

		if (rc != MBX_SUCCESS) {
			sysfs_mbox_idle(phba);
			spin_unlock_irqrestore(host->host_lock, iflag);
			return -ENODEV;
		}
		phba->sysfs_mbox.state = SMBOX_READING;
	}
	else if (phba->sysfs_mbox.offset != off ||
		 phba->sysfs_mbox.state  != SMBOX_READING) {
		printk(KERN_WARNING  "mbox_read: Bad State\n");
		sysfs_mbox_idle(phba);
		spin_unlock_irqrestore(host->host_lock, iflag);
		return -EINVAL;
	}

	memcpy(buf, (uint8_t *) & phba->sysfs_mbox.mbox->mb + off, count);

	phba->sysfs_mbox.offset = off + count;

	if (phba->sysfs_mbox.offset == sizeof(MAILBOX_t))
		sysfs_mbox_idle(phba);

	spin_unlock_irqrestore(phba->host->host_lock, iflag);

	return count;
}

static struct bin_attribute sysfs_mbox_attr = {
	.attr = {
		.name = "mbox",
		.mode = S_IRUSR | S_IWUSR,
		.owner = THIS_MODULE,
	},
	.size = sizeof(MAILBOX_t),
	.read = sysfs_mbox_read,
	.write = sysfs_mbox_write,
};


#ifdef FC_TRANS_VER2		/* fc transport w/ statistics and attrs */

/*
 * Dynamic FC Host Attributes Support
 */

static void
lpfc_get_host_port_id(struct Scsi_Host *shost)
{
	struct lpfc_hba *phba = (struct lpfc_hba*)shost->hostdata[0];
	fc_host_port_id(shost) = be32_to_cpu(phba->fc_myDID);
}

static void
lpfc_get_host_port_type(struct Scsi_Host *shost)
{
	struct lpfc_hba *phba = (struct lpfc_hba*)shost->hostdata[0];
	unsigned long iflag = 0;

	spin_lock_irqsave(shost->host_lock, iflag);

	if (phba->hba_state == LPFC_HBA_READY) {
		if (phba->fc_topology == TOPOLOGY_LOOP) {
			if (phba->fc_flag & FC_PUBLIC_LOOP)
				fc_host_port_type(shost) = FC_PORTTYPE_NLPORT;
			else
				fc_host_port_type(shost) = FC_PORTTYPE_LPORT;
		} else {
			if (phba->fc_flag & FC_FABRIC)
				fc_host_port_type(shost) = FC_PORTTYPE_NPORT;
			else
				fc_host_port_type(shost) = FC_PORTTYPE_PTP;
		}
	} else
		fc_host_port_type(shost) = FC_PORTTYPE_UNKNOWN;

	spin_unlock_irqrestore(shost->host_lock, iflag);
}

static void
lpfc_get_host_port_state(struct Scsi_Host *shost)
{
	struct lpfc_hba *phba = (struct lpfc_hba*)shost->hostdata[0];
	unsigned long iflag = 0;

	spin_lock_irqsave(shost->host_lock, iflag);

	if (phba->fc_flag & FC_OFFLINE_MODE)
		fc_host_port_state(shost) = FC_PORTSTATE_OFFLINE;
	else {
		switch (phba->hba_state) {
		case LPFC_INIT_START:
		case LPFC_INIT_MBX_CMDS:
		case LPFC_LINK_DOWN:
			fc_host_port_state(shost) = FC_PORTSTATE_LINKDOWN;
			break;
		case LPFC_LINK_UP:
		case LPFC_LOCAL_CFG_LINK:
		case LPFC_FLOGI:
		case LPFC_FABRIC_CFG_LINK:
		case LPFC_NS_REG:
		case LPFC_NS_QRY:
		case LPFC_BUILD_DISC_LIST:
		case LPFC_DISC_AUTH:
		case LPFC_CLEAR_LA:
		case LPFC_HBA_READY:
			/* Links up, beyond this port_type reports state */
			fc_host_port_state(shost) = FC_PORTSTATE_ONLINE;
			break;
		case LPFC_HBA_ERROR:
			fc_host_port_state(shost) = FC_PORTSTATE_ERROR;
			break;
		default:
			fc_host_port_state(shost) = FC_PORTSTATE_UNKNOWN;
			break;
		}
	}

	spin_unlock_irqrestore(shost->host_lock, iflag);
}

static void
lpfc_get_host_speed(struct Scsi_Host *shost)
{
	struct lpfc_hba *phba = (struct lpfc_hba*)shost->hostdata[0];
	unsigned long iflag = 0;

	spin_lock_irqsave(shost->host_lock, iflag);

	if (phba->hba_state == LPFC_HBA_READY) {
		if (phba->fc_linkspeed == LA_2GHZ_LINK)
			fc_host_speed(shost) = FC_PORTSPEED_2GBIT;
		else
			fc_host_speed(shost) = FC_PORTSPEED_1GBIT;
	} else
		fc_host_speed(shost) = FC_PORTSPEED_UNKNOWN;

	spin_unlock_irqrestore(shost->host_lock, iflag);
}

static void
lpfc_get_host_fabric_name (struct Scsi_Host *shost)
{
	struct lpfc_hba *phba = (struct lpfc_hba*)shost->hostdata[0];
	unsigned long iflag = 0;
	u64 nodename;

	spin_lock_irqsave(shost->host_lock, iflag);

	if ((phba->fc_flag & FC_FABRIC) ||
	    ((phba->fc_topology == TOPOLOGY_LOOP) &&
	     (phba->fc_flag & FC_PUBLIC_LOOP)))
		memcpy(&nodename, &phba->fc_fabparam.nodeName, sizeof(u64));
	else
		/* fabric is local port if there is no F/FL_Port */
		memcpy(&nodename, &phba->fc_nodename, sizeof(u64));

	spin_unlock_irqrestore(shost->host_lock, iflag);

	fc_host_fabric_name(shost) = be64_to_cpu(nodename);
}


static struct fc_host_statistics *
lpfc_get_stats(struct Scsi_Host *shost)
{
	struct lpfc_hba *phba = (struct lpfc_hba *)shost->hostdata[0];
	struct lpfc_sli *psli = &phba->sli;
	struct fc_host_statistics *hs =
			(struct fc_host_statistics *)phba->link_stats;
	LPFC_MBOXQ_t *pmboxq;
	MAILBOX_t *pmb;
	int rc=0;

	pmboxq = mempool_alloc(phba->mbox_mem_pool, GFP_ATOMIC);
	if (!pmboxq)
		return NULL;
	memset(pmboxq, 0, sizeof (LPFC_MBOXQ_t));

	pmb = &pmboxq->mb;
	pmb->mbxCommand = MBX_READ_STATUS;
	pmb->mbxOwner = OWN_HOST;
	pmboxq->context1 = NULL;

	if ((phba->fc_flag & FC_OFFLINE_MODE) ||
	    (!(psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE))){
		rc = lpfc_sli_issue_mbox(phba, pmboxq, MBX_POLL);
	} else
		rc = lpfc_sli_issue_mbox_wait(phba, pmboxq, phba->fc_ratov * 2);

	if (rc != MBX_SUCCESS) {
		if (pmboxq) {
			if (rc == MBX_TIMEOUT)
				pmboxq->mbox_cmpl = NULL;
			else
				mempool_free( pmboxq, phba->mbox_mem_pool);
		}
		return NULL;
	}

	hs->tx_frames = pmb->un.varRdStatus.xmitFrameCnt;
	hs->tx_words = (pmb->un.varRdStatus.xmitByteCnt * 256);
	hs->rx_frames = pmb->un.varRdStatus.rcvFrameCnt;
	hs->rx_words = (pmb->un.varRdStatus.rcvByteCnt * 256);

	memset((void *)pmboxq, 0, sizeof (LPFC_MBOXQ_t));
	pmb->mbxCommand = MBX_READ_LNK_STAT;
	pmb->mbxOwner = OWN_HOST;
	pmboxq->context1 = NULL;

	if ((phba->fc_flag & FC_OFFLINE_MODE) ||
	    (!(psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE))) {
		rc = lpfc_sli_issue_mbox(phba, pmboxq, MBX_POLL);
	} else
		rc = lpfc_sli_issue_mbox_wait(phba, pmboxq, phba->fc_ratov * 2);

	if (rc != MBX_SUCCESS) {
		if (pmboxq) {
			if (rc == MBX_TIMEOUT)
				pmboxq->mbox_cmpl = NULL;
			else
				mempool_free( pmboxq, phba->mbox_mem_pool);
		}
		return NULL;
	}

	hs->link_failure_count = pmb->un.varRdLnk.linkFailureCnt;
	hs->loss_of_sync_count = pmb->un.varRdLnk.lossSyncCnt;
	hs->loss_of_signal_count = pmb->un.varRdLnk.lossSignalCnt;
	hs->prim_seq_protocol_err_count = pmb->un.varRdLnk.primSeqErrCnt;
	hs->invalid_tx_word_count = pmb->un.varRdLnk.invalidXmitWord;
	hs->invalid_crc_count = pmb->un.varRdLnk.crcCnt;
	hs->error_frames = pmb->un.varRdLnk.crcCnt;

	if (phba->fc_topology == TOPOLOGY_LOOP) {
		hs->lip_count = (phba->fc_eventTag >> 1);
		hs->nos_count = -1;
	} else {
		hs->lip_count = -1;
		hs->nos_count = (phba->fc_eventTag >> 1);
	}

	hs->dumped_frames = -1;

/* FIX ME */
	/*hs->SecondsSinceLastReset = (jiffies - lpfc_loadtime) / HZ;*/

	return hs;
}

#endif /* FC_TRANS_VER2 */

#ifdef  FC_TRANS_VER1
/*
 * The LPFC driver treats linkdown handling as target loss events so there
 * are no sysfs handlers for link_down_tmo.
 */
static void
lpfc_get_starget_port_id(struct scsi_target *starget)
{
	struct lpfc_nodelist *ndlp = NULL;
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct lpfc_hba *phba = (struct lpfc_hba *) shost->hostdata[0];
	uint16_t did = 0;

	/* Search the mapped list for this target ID */
	list_for_each_entry(ndlp, &phba->fc_nlpmap_list, nlp_listp) {
		if (starget->id == ndlp->nlp_sid) {
			did = ndlp->nlp_sid;
			break;
		}
	}

	fc_starget_port_id(starget) = did;
}

static void
lpfc_get_starget_node_name(struct scsi_target *starget)
{
	struct lpfc_nodelist *ndlp = NULL;
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct lpfc_hba *phba = (struct lpfc_hba *) shost->hostdata[0];
	uint64_t node_name = 0;

	/* Search the mapped list for this target ID */
	list_for_each_entry(ndlp, &phba->fc_nlpmap_list, nlp_listp) {
		if (starget->id == ndlp->nlp_sid) {
			memcpy(&node_name, &ndlp->nlp_nodename,
						sizeof(struct lpfc_name));
			break;
		}
	}

	fc_starget_node_name(starget) = be64_to_cpu(node_name);
}

static void
lpfc_get_starget_port_name(struct scsi_target *starget)
{
	struct lpfc_nodelist *ndlp = NULL;
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct lpfc_hba *phba = (struct lpfc_hba *) shost->hostdata[0];
	uint64_t port_name = 0;

	/* Search the mapped list for this target ID */
	list_for_each_entry(ndlp, &phba->fc_nlpmap_list, nlp_listp) {
		if (starget->id == ndlp->nlp_sid) {
			memcpy(&port_name, &ndlp->nlp_portname,
						sizeof(struct lpfc_name));
			break;
		}
	}

	fc_starget_port_name(starget) = be64_to_cpu(port_name);
}

static void
lpfc_get_starget_loss_tmo(struct scsi_target *starget)
{
	/*
	 * Return the driver's global value for device loss timeout plus
	 * five seconds to allow the driver's nodev timer to run.
	 */
	fc_starget_dev_loss_tmo(starget) = lpfc_nodev_tmo + 5;
}

static void
lpfc_set_starget_loss_tmo(struct scsi_target *starget, uint32_t timeout)
{
	/*
	 * The driver doesn't have a per-target timeout setting.  Set
	 * this value globally.
	 */
	lpfc_nodev_tmo = timeout;
}

#else /* not defined FC_TRANS_VER1 */

static void
lpfc_get_port_id(struct scsi_device *sdev)
{
	struct lpfc_target *target = sdev->hostdata;
	if (sdev->host->transportt && target->pnode)
		fc_port_id(sdev) = target->pnode->nlp_DID;
}

static void
lpfc_get_node_name(struct scsi_device *sdev)
{
	struct lpfc_target *target = sdev->hostdata;
	uint64_t node_name = 0;
	if (sdev->host->transportt && target->pnode)
		memcpy(&node_name, &target->pnode->nlp_nodename,
						sizeof(struct lpfc_name));
	fc_node_name(sdev) = be64_to_cpu(node_name);
}

static void
lpfc_get_port_name(struct scsi_device *sdev)
{
	struct lpfc_target *target = sdev->hostdata;
	uint64_t port_name = 0;
	if (sdev->host->transportt && target->pnode)
		memcpy(&port_name, &target->pnode->nlp_portname,
						sizeof(struct lpfc_name));
	fc_port_name(sdev) = be64_to_cpu(port_name);
}
#endif

static struct fc_function_template lpfc_transport_functions = {
#ifdef FC_TRANS_VER2		/* fc transport w/ statistics and attrs */

	/* fixed attributes the driver supports */
	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.show_host_supported_classes = 1,
	.show_host_supported_fc4s = 1,
	.show_host_symbolic_name = 1,
	.show_host_supported_speeds = 1,
	.show_host_maxframe_size = 1,

	/* dynamic attributes the driver supports */
	.get_host_port_id = lpfc_get_host_port_id,
	.show_host_port_id = 1,

	.get_host_port_type = lpfc_get_host_port_type,
	.show_host_port_type = 1,

	.get_host_port_state = lpfc_get_host_port_state,
	.show_host_port_state = 1,

	/* active_fc4s is shown but doesn't change (thus no get function) */
	.show_host_active_fc4s = 1,

	.get_host_speed = lpfc_get_host_speed,
	.show_host_speed = 1,

	.get_host_fabric_name = lpfc_get_host_fabric_name,
	.show_host_fabric_name = 1,

	/*
	 * The LPFC driver treats linkdown handling as target loss events
	 * so there are no sysfs handlers for link_down_tmo.
	 */

	.get_fc_host_stats = lpfc_get_stats,
	/* the LPFC driver doesn't support resetting stats yet */

#endif /* FC_TRANS_VER2 */

/* note: FC_TRANS_VER1 will set if FC_TRANS_VER2 is set */
#ifdef FC_TRANS_VER1
	.get_starget_port_id  = lpfc_get_starget_port_id,
	.show_starget_port_id = 1,

	.get_starget_node_name = lpfc_get_starget_node_name,
	.show_starget_node_name = 1,

	.get_starget_port_name = lpfc_get_starget_port_name,
	.show_starget_port_name = 1,

	.get_starget_dev_loss_tmo = lpfc_get_starget_loss_tmo,
	.set_starget_dev_loss_tmo = lpfc_set_starget_loss_tmo,
	.show_starget_dev_loss_tmo = 1,

#else
	.get_port_id  = lpfc_get_port_id,
	.show_port_id = 1,

	.get_node_name = lpfc_get_node_name,
	.show_node_name = 1,

	.get_port_name = lpfc_get_port_name,
	.show_port_name = 1,
#endif
};

static int
lpfc_proc_info(struct Scsi_Host *host,
	       char *buf, char **start, off_t offset, int count, int rw)
{
	struct lpfc_hba *phba = (struct lpfc_hba *)host->hostdata[0];
	struct lpfc_nodelist *ndlp;
	int len = 0;

	/* Sufficient bytes to hold a port or node name. */
	uint8_t name[sizeof (struct lpfc_name)];

	/* If rw = 0, then read info
	 * If rw = 1, then write info (NYI)
	 */
	if (rw)
		return -EINVAL;

	list_for_each_entry(ndlp, &phba->fc_nlpmap_list, nlp_listp) {
		if (ndlp->nlp_state == NLP_STE_MAPPED_NODE){
			len += snprintf(buf + len, PAGE_SIZE -len,
					"lpfc%dt%02x DID %06x WWPN ",
					phba->brd_no,
					ndlp->nlp_sid, ndlp->nlp_DID);

			memcpy (&name[0], &ndlp->nlp_portname,
				sizeof (struct lpfc_name));
			len += snprintf(buf + len, PAGE_SIZE-len,
					"%02x:%02x:%02x:%02x:%02x:%02x:"
					"%02x:%02x",
					name[0], name[1], name[2],
					name[3], name[4], name[5],
					name[6], name[7]);
			len += snprintf(buf + len, PAGE_SIZE-len, " WWNN ");
			memcpy (&name[0], &ndlp->nlp_nodename,
				sizeof (struct lpfc_name));
			len += snprintf(buf + len, PAGE_SIZE-len,
					"%02x:%02x:%02x:%02x:%02x:%02x:"
					"%02x:%02x\n",
					name[0], name[1], name[2],
					name[3], name[4], name[5],
					name[6], name[7]);
			}
		if (PAGE_SIZE - len < 90)
			break;
	}
	if (&ndlp->nlp_listp != &phba->fc_nlpmap_list)
		len += snprintf(buf+len, PAGE_SIZE-len, "...\n");

	return (len);
}

static int
lpfc_slave_alloc(struct scsi_device *scsi_devs)
{
	struct lpfc_hba *phba;
	struct lpfc_target *target;

	/*
	 * Store the lun pointer in the scsi_device hostdata pointer provided
	 * the driver has already discovered the target id.
	 */
	phba = (struct lpfc_hba *) scsi_devs->host->hostdata[0];
	target = lpfc_find_target(phba, scsi_devs->id, 0);
	if (target) {
		scsi_devs->hostdata = target;
		return 0;
	}

	/*
	 * The driver does not have a target id matching that in the scsi
	 * device.  Allocate a dummy target initialized to zero so that
	 * the driver's queuecommand entry correctly fails the call
	 * forcing the midlayer to call lpfc_slave_destroy.  This code
	 * will be removed in a subsequent kernel patch.
	 */

	target = kmalloc(sizeof (struct lpfc_target), GFP_KERNEL);
	if (!target)
		return 1;

	memset(target, 0, sizeof (struct lpfc_target));
	scsi_devs->hostdata = target;
	return 0;
}

static int
lpfc_slave_configure(struct scsi_device *sdev)
{
	struct lpfc_hba *phba = (struct lpfc_hba *) sdev->host->hostdata[0];

#if defined(FC_TRANS_VER1)
	struct lpfc_target *target = (struct lpfc_target *) sdev->hostdata;
#endif

	if (sdev->tagged_supported) {
		sdev->current_tag = 0;
		sdev->queue_depth = phba->cfg_lun_queue_depth;
	}

#ifdef FC_TRANS_VER1
	if ((target) && (sdev->sdev_target) && (target->pnode)) {
		/*
		 * Initialize the fc transport attributes for the target
		 * containing this scsi device.  Also note that the driver's
		 * target pointer is stored in the starget_data for the
		 * driver's sysfs entry point functions.
		 */
		target->pnode->starget = sdev->sdev_target;
		fc_starget_dev_loss_tmo(sdev->sdev_target) = lpfc_nodev_tmo + 5;
	}
#endif

	return 0;
}

static void
lpfc_slave_destroy(struct scsi_device *sdev)
{
	struct lpfc_hba *phba;
	struct lpfc_target *target;

	phba = (struct lpfc_hba *) sdev->host->hostdata[0];
	target = sdev->hostdata;

	if (target) {
		if (!target->pnode)
			kfree(target);
	}

	/*
	 * Set this scsi device's hostdata to NULL since it is going
	 * away.  Also, (future) don't set the starget_dev_loss_tmo
	 * this value is global to all targets managed by this
	 * host.
	 */
	sdev->hostdata = NULL;
}

static struct class_device_attribute *lpfc_host_attrs[] = {
	&class_device_attr_info,
	&class_device_attr_serialnum,
	&class_device_attr_fwrev,
	&class_device_attr_hdw,
	&class_device_attr_option_rom_version,
	&class_device_attr_state,
	&class_device_attr_num_discovered_ports,
#ifndef FC_TRANS_VER2
	&class_device_attr_speed,
	&class_device_attr_node_name,
	&class_device_attr_port_name,
	&class_device_attr_portfcid,
	&class_device_attr_port_type,
	&class_device_attr_fabric_name,
#endif /* FC_TRANS_VER2 */
	&class_device_attr_events,
	&class_device_attr_lpfc_drvr_version,
	&class_device_attr_lpfc_log_verbose,
	&class_device_attr_lpfc_lun_queue_depth,
	&class_device_attr_lpfc_nodev_tmo,
	&class_device_attr_lpfc_automap,
	&class_device_attr_lpfc_fcp_class,
	&class_device_attr_lpfc_use_adisc,
	&class_device_attr_lpfc_ack0,
	&class_device_attr_lpfc_topology,
	&class_device_attr_lpfc_scan_down,
	&class_device_attr_lpfc_link_speed,
	&class_device_attr_lpfc_fdmi_on,
	&class_device_attr_lpfc_fcp_bind_method,
	&class_device_attr_nport_evt_cnt,
	&class_device_attr_management_version,
	&class_device_attr_issue_lip,
	&class_device_attr_board_online,
	&class_device_attr_disc_npr,
	&class_device_attr_disc_map,
	&class_device_attr_disc_unmap,
	&class_device_attr_disc_prli,
	&class_device_attr_disc_reglgn,
	&class_device_attr_disc_adisc,
	&class_device_attr_disc_plogi,
	&class_device_attr_disc_unused,
	&class_device_attr_outfcpio,
	NULL,
};

static struct scsi_host_template driver_template = {
	.module			= THIS_MODULE,
	.name			= LPFC_DRIVER_NAME,
	.info			= lpfc_info,
	.queuecommand		= lpfc_queuecommand,
	.eh_abort_handler	= lpfc_abort_handler,
	.eh_device_reset_handler= lpfc_reset_lun_handler,
	.eh_bus_reset_handler	= lpfc_reset_bus_handler,
	.slave_alloc		= lpfc_slave_alloc,
	.slave_configure	= lpfc_slave_configure,
	.slave_destroy		= lpfc_slave_destroy,
	.proc_info		= lpfc_proc_info,
	.proc_name		= LPFC_DRIVER_NAME,
	.this_id		= -1,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= 30,
	.shost_attrs		= lpfc_host_attrs,
	.use_clustering		= ENABLE_CLUSTERING,
};

static int
lpfc_sli_setup(struct lpfc_hba * phba)
{
	int i, totiocb = 0;
	struct lpfc_sli *psli = &phba->sli;
	LPFC_RING_INIT_t *pring;

	psli->sliinit.num_rings = MAX_CONFIGURED_RINGS;
	psli->fcp_ring = LPFC_FCP_RING;
	psli->next_ring = LPFC_FCP_NEXT_RING;
	psli->ip_ring = LPFC_IP_RING;

	for (i = 0; i < psli->sliinit.num_rings; i++) {
		pring = &psli->sliinit.ringinit[i];
		switch (i) {
		case LPFC_FCP_RING:	/* ring 0 - FCP */
			/* numCiocb and numRiocb are used in config_port */
			pring->numCiocb = SLI2_IOCB_CMD_R0_ENTRIES;
			pring->numRiocb = SLI2_IOCB_RSP_R0_ENTRIES;
			pring->numCiocb += SLI2_IOCB_CMD_R1XTRA_ENTRIES;
			pring->numRiocb += SLI2_IOCB_RSP_R1XTRA_ENTRIES;
			pring->numCiocb += SLI2_IOCB_CMD_R3XTRA_ENTRIES;
			pring->numRiocb += SLI2_IOCB_RSP_R3XTRA_ENTRIES;
			pring->iotag_ctr = 0;
			pring->iotag_max =
			    (phba->cfg_hba_queue_depth * 2);
			pring->fast_iotag = pring->iotag_max;
			pring->num_mask = 0;
			break;
		case LPFC_IP_RING:	/* ring 1 - IP */
			/* numCiocb and numRiocb are used in config_port */
			pring->numCiocb = SLI2_IOCB_CMD_R1_ENTRIES;
			pring->numRiocb = SLI2_IOCB_RSP_R1_ENTRIES;
			pring->num_mask = 0;
			break;
		case LPFC_ELS_RING:	/* ring 2 - ELS / CT */
			/* numCiocb and numRiocb are used in config_port */
			pring->numCiocb = SLI2_IOCB_CMD_R2_ENTRIES;
			pring->numRiocb = SLI2_IOCB_RSP_R2_ENTRIES;
			pring->fast_iotag = 0;
			pring->iotag_ctr = 0;
			pring->iotag_max = 4096;
			pring->num_mask = 4;
			pring->prt[0].profile = 0;	/* Mask 0 */
			pring->prt[0].rctl = FC_ELS_REQ;
			pring->prt[0].type = FC_ELS_DATA;
			pring->prt[0].lpfc_sli_rcv_unsol_event =
			    lpfc_els_unsol_event;
			pring->prt[1].profile = 0;	/* Mask 1 */
			pring->prt[1].rctl = FC_ELS_RSP;
			pring->prt[1].type = FC_ELS_DATA;
			pring->prt[1].lpfc_sli_rcv_unsol_event =
			    lpfc_els_unsol_event;
			pring->prt[2].profile = 0;	/* Mask 2 */
			/* NameServer Inquiry */
			pring->prt[2].rctl = FC_UNSOL_CTL;
			/* NameServer */
			pring->prt[2].type = FC_COMMON_TRANSPORT_ULP;
			pring->prt[2].lpfc_sli_rcv_unsol_event =
			    lpfc_ct_unsol_event;
			pring->prt[3].profile = 0;	/* Mask 3 */
			/* NameServer response */
			pring->prt[3].rctl = FC_SOL_CTL;
			/* NameServer */
			pring->prt[3].type = FC_COMMON_TRANSPORT_ULP;
			pring->prt[3].lpfc_sli_rcv_unsol_event =
			    lpfc_ct_unsol_event;
			break;
		}
		totiocb += (pring->numCiocb + pring->numRiocb);
	}
	if (totiocb > MAX_SLI2_IOCB) {
		/* Too many cmd / rsp ring entries in SLI2 SLIM */
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"%d:0462 Too many cmd / rsp ring entries in "
				"SLI2 SLIM Data: x%x x%x\n",
				phba->brd_no, totiocb, MAX_SLI2_IOCB);
	}

#ifdef USE_HGP_HOST_SLIM
	psli->sliinit.sli_flag = LPFC_HGP_HOSTSLIM;
#else
	psli->sliinit.sli_flag = 0;
#endif

	return (0);
}

static int
lpfc_set_bind_type(struct lpfc_hba * phba)
{
	int bind_type = phba->cfg_fcp_bind_method;
	int ret = LPFC_BIND_WW_NN_PN;

	switch (bind_type) {
	case 1:
		phba->fcp_mapping = FCP_SEED_WWNN;
		break;

	case 2:
		phba->fcp_mapping = FCP_SEED_WWPN;
		break;

	case 3:
		phba->fcp_mapping = FCP_SEED_DID;
		ret = LPFC_BIND_DID;
		break;

	case 4:
		phba->fcp_mapping = FCP_SEED_DID;
		ret = LPFC_BIND_DID;
		break;
	}

	return (ret);
}

static void
lpfc_get_cfgparam(struct lpfc_hba *phba)
{
	phba->cfg_log_verbose = lpfc_log_verbose;
	phba->cfg_automap = lpfc_automap;
	phba->cfg_fcp_bind_method = lpfc_fcp_bind_method;
	phba->cfg_cr_delay = lpfc_cr_delay;
	phba->cfg_cr_count = lpfc_cr_count;
	phba->cfg_lun_queue_depth = lpfc_lun_queue_depth;
	phba->cfg_fcp_class = lpfc_fcp_class;
	phba->cfg_use_adisc = lpfc_use_adisc;
	phba->cfg_ack0 = lpfc_ack0;
	phba->cfg_topology = lpfc_topology;
	phba->cfg_scan_down = lpfc_scan_down;
	phba->cfg_nodev_tmo = lpfc_nodev_tmo;
	phba->cfg_link_speed = lpfc_link_speed;
	phba->cfg_fdmi_on = lpfc_fdmi_on;
	phba->cfg_discovery_threads = lpfc_discovery_threads;

	if (phba->cfg_discovery_threads)
		if (phba->cfg_automap == 0)
			phba->cfg_discovery_threads = LPFC_MAX_DISC_THREADS;

	switch (phba->pcidev->device) {
	case PCI_DEVICE_ID_LP101:
		phba->cfg_hba_queue_depth = LPFC_LP101_HBA_Q_DEPTH;
		break;
	case PCI_DEVICE_ID_RFLY:
	case PCI_DEVICE_ID_PFLY:
	case PCI_DEVICE_ID_JFLY:
	case PCI_DEVICE_ID_ZFLY:
	case PCI_DEVICE_ID_TFLY:
		phba->cfg_hba_queue_depth = LPFC_LC_HBA_Q_DEPTH;
		break;
	default:
		phba->cfg_hba_queue_depth = LPFC_DFT_HBA_Q_DEPTH;
	}
	return;
}

static void
lpfc_consistent_bind_setup(struct lpfc_hba * phba)
{
	INIT_LIST_HEAD(&phba->fc_nlpbind_list);
	phba->fc_bind_cnt = 0;
}

static uint8_t
lpfc_get_brd_no(struct lpfc_hba * phba)
{
	uint8_t    brd, found = 1;

 	brd = 0;
	while(found) {
		phba = NULL;
		found = 0;
		list_for_each_entry(phba, &lpfc_hba_list, hba_list) {
			if (phba->brd_no == brd) {
				found = 1;
				brd++;
				break;
			}
		}
	}
	return (brd);
}


static int __devinit
lpfc_pci_probe_one(struct pci_dev *pdev, const struct pci_device_id *pid)
{
	struct Scsi_Host *host;
	struct lpfc_hba  *phba;
	struct lpfc_sli  *psli;
	unsigned long iflag;
	unsigned long bar0map_len, bar2map_len;
	int error = -ENODEV, retval;
#ifdef FC_TRANS_VER2		/* fc transport w/ statistics and attrs */
	u64 wwname;
#endif /* FC_TRANS_VER2 */

	if (pci_enable_device(pdev))
		goto out;
	if (pci_request_regions(pdev, LPFC_DRIVER_NAME))
		goto out_disable_device;

	/*
	 * Allocate space for adapter info structure
	 */
#ifdef FC_TRANS_VER2		/* fc transport w/ statistics and attrs */
	phba = kmalloc((sizeof(*phba) + sizeof(struct fc_host_statistics)),
			GFP_KERNEL);
#else /* not FC_TRANS_VER2 */
	phba = kmalloc(sizeof(*phba), GFP_KERNEL);
#endif /* not FC_TRANS_VER2 */
	if (!phba)
		goto out_release_regions;
	memset(phba, 0, sizeof (struct lpfc_hba));
#ifdef FC_TRANS_VER2		/* fc transport w/ statistics and attrs */
	phba->link_stats = (void *)&phba[1];
#endif /* FC_TRANS_VER2 */

	host = scsi_host_alloc(&driver_template, sizeof (unsigned long));
	if (!host) {
		printk (KERN_WARNING "%s: scsi_host_alloc failed.\n",
							 lpfc_drvr_name);
		error = -ENOMEM;
		goto out_kfree_phba;
	}
	host->max_id = LPFC_MAX_TARGET;
	host->max_lun = LPFC_MAX_LUN;
	host->this_id = -1;

	phba->pcidev = pdev;
	phba->host = host;

	INIT_LIST_HEAD(&phba->ctrspbuflist);
	INIT_LIST_HEAD(&phba->rnidrspbuflist);
	INIT_LIST_HEAD(&phba->freebufList);

	/* Initialize timers used by driver */
	init_timer(&phba->fc_estabtmo);
	phba->fc_estabtmo.function = lpfc_establish_link_tmo;
	phba->fc_estabtmo.data = (unsigned long)phba;
	init_timer(&phba->fc_disctmo);
	phba->fc_disctmo.function = lpfc_disc_timeout;
	phba->fc_disctmo.data = (unsigned long)phba;

	init_timer(&phba->fc_fdmitmo);
	phba->fc_fdmitmo.function = lpfc_fdmi_tmo;
	phba->fc_fdmitmo.data = (unsigned long)phba;
	init_timer(&phba->els_tmofunc);
	phba->els_tmofunc.function = lpfc_els_timeout_handler;
	phba->els_tmofunc.data = (unsigned long)phba;
	psli = &phba->sli;
	init_timer(&psli->mbox_tmo);
	psli->mbox_tmo.function = lpfc_mbox_timeout;
	psli->mbox_tmo.data = (unsigned long)phba;

	/* Assign an unused board number */
 	phba->brd_no = lpfc_get_brd_no(phba);
	host->unique_id = phba->brd_no;

	lpfc_get_cfgparam(phba);

	/* Add adapter structure to list */
	list_add_tail(&phba->hba_list, &lpfc_hba_list);

	/* Initialize all internally managed lists. */
	INIT_LIST_HEAD(&phba->fc_nlpmap_list);
	INIT_LIST_HEAD(&phba->fc_nlpunmap_list);
	INIT_LIST_HEAD(&phba->fc_unused_list);
	INIT_LIST_HEAD(&phba->fc_plogi_list);
	INIT_LIST_HEAD(&phba->fc_adisc_list);
	INIT_LIST_HEAD(&phba->fc_reglogin_list);
	INIT_LIST_HEAD(&phba->fc_prli_list);
	INIT_LIST_HEAD(&phba->fc_npr_list);
	lpfc_consistent_bind_setup(phba);

	init_waitqueue_head(&phba->linkevtwq);
	init_waitqueue_head(&phba->rscnevtwq);
	init_waitqueue_head(&phba->ctevtwq);

	pci_set_master(pdev);
	retval = pci_set_mwi(pdev);
	if (retval)
		dev_printk(KERN_WARNING, &pdev->dev,
			   "Warning: pci_set_mwi returned %d\n", retval);

	/* Configure DMA attributes. */
	if (dma_set_mask(&phba->pcidev->dev, 0xffffffffffffffffULL) &&
	    dma_set_mask(&phba->pcidev->dev, 0xffffffffULL))
		goto out_list_del;

	/*
	 * Get the physical address of Bar0 and Bar2 and the number of bytes
	 * required by each mapping.
	 */
	phba->pci_bar0_map = pci_resource_start(phba->pcidev, 0);
	bar0map_len        = pci_resource_len(phba->pcidev, 0);

	phba->pci_bar2_map = pci_resource_start(phba->pcidev, 2);
	bar2map_len        = pci_resource_len(phba->pcidev, 2);

	/* Map HBA SLIM and Control Registers to a kernel virtual address. */
	phba->slim_memmap_p      = ioremap(phba->pci_bar0_map, bar0map_len);
	phba->ctrl_regs_memmap_p = ioremap(phba->pci_bar2_map, bar2map_len);

	/*
	 * Allocate memory for SLI-2 structures
	 */
	phba->slim2p = dma_alloc_coherent(&phba->pcidev->dev, SLI2_SLIM_SIZE,
					  &phba->slim2p_mapping, GFP_KERNEL);
	if (!phba->slim2p)
		goto out_iounmap;


	lpfc_sli_setup(phba);	/* Setup SLI Layer to run over lpfc HBAs */
	lpfc_sli_queue_setup(phba);	/* Initialize the SLI Layer */

	error = lpfc_mem_alloc(phba);
	if (error)
		goto out_dec_nhbas;

	lpfc_set_bind_type(phba);

	/* Initialize HBA structure */
	phba->fc_edtov = FF_DEF_EDTOV;
	phba->fc_ratov = FF_DEF_RATOV;
	phba->fc_altov = FF_DEF_ALTOV;
	phba->fc_arbtov = FF_DEF_ARBTOV;

	INIT_LIST_HEAD(&phba->dpc_disc);
	init_completion(&phba->dpc_startup);
	init_completion(&phba->dpc_exiting);

	/*
	* Startup the kernel thread for this host adapter
	*/
	phba->dpc_kill = 0;
	phba->dpc_pid = kernel_thread(lpfc_do_dpc, phba, 0);
	if (phba->dpc_pid < 0) {
		error = phba->dpc_pid;
		goto out_free_mem;
	}
	wait_for_completion(&phba->dpc_startup);

	/* Call SLI to initialize the HBA. */
	error = lpfc_sli_hba_setup(phba);
	if (error)
		goto out_hba_down;

	/* We can rely on a queue depth attribute only after SLI HBA setup */
	host->can_queue = phba->cfg_hba_queue_depth - 10;

	/*
	 * Starting with 2.4.0 kernel, Linux can support commands longer
	 * than 12 bytes. However, scsi_register() always sets it to 12.
	 * For it to be useful to the midlayer, we have to set it here.
	 */
	host->max_cmd_len = 16;

	/*
	 * Queue depths per lun
	 */
	host->transportt = lpfc_transport_template;
	host->hostdata[0] = (unsigned long)phba;
	pci_set_drvdata(pdev, host);
	error = scsi_add_host(host, &pdev->dev);
	if (error)
		goto out_put_host;

#ifdef FC_TRANS_VER2
	/*
	 * set fixed host attributes
	 */

	memcpy(&wwname, &phba->fc_nodename, sizeof(u64));
	fc_host_node_name(host) = be64_to_cpu(wwname);
	memcpy(&wwname, &phba->fc_portname, sizeof(u64));
	fc_host_port_name(host) = be64_to_cpu(wwname);
	fc_host_supported_classes(host) = FC_COS_CLASS3;

	memset(fc_host_supported_fc4s(host), 0,
		sizeof(fc_host_supported_fc4s(host)));
	fc_host_supported_fc4s(host)[2] = 1;
	fc_host_supported_fc4s(host)[7] = 1;

	lpfc_get_hba_sym_node_name(phba, fc_host_symbolic_name(host));

	if (FC_JEDEC_ID(phba->vpd.rev.biuRev) == VIPER_JEDEC_ID)
		fc_host_supported_speeds(host) = FC_PORTSPEED_10GBIT;
	else if (FC_JEDEC_ID(phba->vpd.rev.biuRev) == HELIOS_JEDEC_ID)
		fc_host_supported_speeds(host) =
			(FC_PORTSPEED_1GBIT | FC_PORTSPEED_2GBIT |
			 FC_PORTSPEED_4GBIT);
	else if ((FC_JEDEC_ID(phba->vpd.rev.biuRev) ==
		  CENTAUR_2G_JEDEC_ID)
		 || (FC_JEDEC_ID(phba->vpd.rev.biuRev) ==
		     PEGASUS_JEDEC_ID)
		 || (FC_JEDEC_ID(phba->vpd.rev.biuRev) ==
		     THOR_JEDEC_ID))
		fc_host_supported_speeds(host) =
			(FC_PORTSPEED_1GBIT | FC_PORTSPEED_2GBIT);
	else
		fc_host_supported_speeds(host) = FC_PORTSPEED_1GBIT;

	fc_host_maxframe_size(host) = be32_to_cpu(
		((((uint32_t) phba->fc_sparam.cmn.bbRcvSizeMsb) << 8) |
		 (uint32_t) phba->fc_sparam.cmn.bbRcvSizeLsb));

	/* This value is also unchanging */
	memset(fc_host_active_fc4s(host), 0,
		sizeof(fc_host_active_fc4s(host)));
	fc_host_active_fc4s(host)[2] = 1;
	fc_host_active_fc4s(host)[7] = 1;
#endif /* FC_TRANS_VER2 */

#ifdef DFC_DEBUG
	sysfs_create_bin_file(&host->shost_classdev.kobj, &sysfs_ctpass_attr);
	sysfs_create_bin_file(&host->shost_classdev.kobj,
							&sysfs_sendrnid_attr);


	if (phba->sli.sliinit.sli_flag & LPFC_SLI2_ACTIVE)
		sysfs_slimem_attr.size = SLI2_SLIM_SIZE;
	else
		sysfs_slimem_attr.size = SLI1_SLIM_SIZE;

	sysfs_create_bin_file(&host->shost_classdev.kobj, &sysfs_slimem_attr);
#endif
	sysfs_create_bin_file(&host->shost_classdev.kobj, &sysfs_ctlreg_attr);
	sysfs_create_bin_file(&host->shost_classdev.kobj, &sysfs_mbox_attr);
	scsi_scan_host(host);
	return 0;

out_put_host:
	scsi_host_put(host);
out_hba_down:
	lpfc_sli_hba_down(phba);

	/* Stop any timers that were started during this attach. */
	spin_lock_irqsave(phba->host->host_lock, iflag);
	lpfc_stop_timer(phba);
	spin_unlock_irqrestore(phba->host->host_lock, iflag);

	/* Kill the kernel thread for this host */
	if (phba->dpc_pid >= 0) {
		phba->dpc_kill = 1;
		wmb();
		kill_proc(phba->dpc_pid, SIGHUP, 1);
		wait_for_completion(&phba->dpc_exiting);
	}

	free_irq(phba->pcidev->irq, phba);
out_free_mem:
	lpfc_mem_free(phba);
out_dec_nhbas:
	dma_free_coherent(&pdev->dev, SLI2_SLIM_SIZE,
			  phba->slim2p, phba->slim2p_mapping);
out_iounmap:
	iounmap(phba->ctrl_regs_memmap_p);
	iounmap(phba->slim_memmap_p);
out_list_del:
	list_del_init(&phba->hba_list);
out_kfree_phba:
	kfree(phba);
out_release_regions:
	pci_release_regions(pdev);
out_disable_device:
	pci_disable_device(pdev);
out:
	return error;
}

static void __devexit
lpfc_pci_remove_one(struct pci_dev *pdev)
{
	struct Scsi_Host *host = pci_get_drvdata(pdev);
	struct lpfc_hba  *phba = (struct lpfc_hba *)host->hostdata[0];
	unsigned long iflag;

	sysfs_remove_bin_file(&host->shost_classdev.kobj, &sysfs_mbox_attr);
	sysfs_remove_bin_file(&host->shost_classdev.kobj, &sysfs_ctlreg_attr);
#ifdef DFC_DEBUG
	sysfs_remove_bin_file(&host->shost_classdev.kobj, &sysfs_slimem_attr);
	sysfs_remove_bin_file(&host->shost_classdev.kobj,
							&sysfs_sendrnid_attr);
	sysfs_remove_bin_file(&host->shost_gendev.kobj, &sysfs_ctpass_attr);
#endif

/* FIX ME */

	/* In case we are offline or link is down */
	/*scsi_unblock_requests(phba->host);*/

	scsi_remove_host(phba->host);
	list_del(&phba->hba_list);

	/* detach the board */

	/* Kill the kernel thread for this host */
	if (phba->dpc_pid >= 0) {
		phba->dpc_kill = 1;
		wmb();
		kill_proc(phba->dpc_pid, SIGHUP, 1);
		wait_for_completion(&phba->dpc_exiting);
	}

	/*
	 * Bring down the SLI Layer. This step disable all interrupts,
	 * clears the rings, discards all mailbox commands, and resets
	 * the HBA.
	 */
	lpfc_sli_hba_down(phba);

	/* Release the irq reservation */
	free_irq(phba->pcidev->irq, phba);

	lpfc_cleanup(phba, 0);
	lpfc_scsi_free(phba);

	spin_lock_irqsave(phba->host->host_lock, iflag);
	lpfc_stop_timer(phba);
	spin_unlock_irqrestore(phba->host->host_lock, iflag);

	lpfc_mem_free(phba);

	/* Free resources associated with SLI2 interface */
	dma_free_coherent(&pdev->dev, SLI2_SLIM_SIZE,
			  phba->slim2p, phba->slim2p_mapping);

	/* unmap adapter SLIM and Control Registers */
	iounmap(phba->ctrl_regs_memmap_p);
	iounmap(phba->slim_memmap_p);

	pci_release_regions(phba->pcidev);
	pci_disable_device(phba->pcidev);

	scsi_host_put(phba->host);
	kfree(phba);

	pci_set_drvdata(pdev, NULL);
}

static struct pci_device_id lpfc_id_table[] = {
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_VIPER,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_THOR,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_PEGASUS,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_CENTAUR,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_DRAGONFLY,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_SUPERFLY,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_RFLY,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_PFLY,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_HELIOS,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_JFLY,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_ZEPHYR,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_ZFLY,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_TFLY,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_LP101,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, lpfc_id_table);


static struct pci_driver lpfc_driver = {
	.name		= LPFC_DRIVER_NAME,
	.id_table	= lpfc_id_table,
	.probe		= lpfc_pci_probe_one,
	.remove		= __devexit_p(lpfc_pci_remove_one),
};

static int __init
lpfc_init(void)
{
	int rc;

	printk(LPFC_MODULE_DESC "\n");


	lpfc_transport_template =
		fc_attach_transport(&lpfc_transport_functions);
	if (!lpfc_transport_template)
		return -ENODEV;
	rc = pci_module_init(&lpfc_driver);
	return rc;

}

static void __exit
lpfc_exit(void)
{
	pci_unregister_driver(&lpfc_driver);
	fc_release_transport(lpfc_transport_template);
}
module_init(lpfc_init);
module_exit(lpfc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(LPFC_MODULE_DESC);
MODULE_AUTHOR("Emulex Corporation - tech.support@emulex.com");
MODULE_VERSION("0:" LPFC_DRIVER_VERSION);
