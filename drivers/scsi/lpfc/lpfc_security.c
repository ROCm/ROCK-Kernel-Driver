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
#include <linux/pci.h>
#include <linux/interrupt.h>

#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport_fc.h>

#include "lpfc_hw4.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_sli4.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_crtn.h"
#include "lpfc_logmsg.h"
#include "lpfc_security.h"
#include "lpfc_auth_access.h"
#include "lpfc_vport.h"

void
lpfc_security_service_online(struct Scsi_Host *shost)
{
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;

	vport->security_service_state = SECURITY_ONLINE;
	if (vport->cfg_enable_auth &&
	    vport->auth.auth_mode == FC_AUTHMODE_UNKNOWN &&
	    vport->phba->link_state == LPFC_HBA_ERROR)
		lpfc_selective_reset(vport->phba);
}

void
lpfc_security_service_offline(struct Scsi_Host *shost)
{
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;

	vport->security_service_state = SECURITY_OFFLINE;
}

void
lpfc_security_config(struct Scsi_Host *shost, int status, void *rsp)
{
	struct fc_auth_rsp *auth_rsp = (struct fc_auth_rsp *)rsp;
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;
	struct lpfc_nodelist *ndlp;
	uint32_t old_interval, new_interval;
	unsigned long new_jiffies, temp_jiffies;
	uint8_t last_auth_mode;

	if (status)
		return;
	ndlp = lpfc_findnode_did(vport, Fabric_DID);
	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp))
		return;

	vport->auth.bidirectional =
		auth_rsp->u.dhchap_security_config.bidirectional;
	memcpy(&vport->auth.hash_priority[0],
	       &auth_rsp->u.dhchap_security_config.hash_priority[0],
	       sizeof(vport->auth.hash_priority));
	vport->auth.hash_len = auth_rsp->u.dhchap_security_config.hash_len;
	memcpy(&vport->auth.dh_group_priority[0],
	       &auth_rsp->u.dhchap_security_config.
	       dh_group_priority[0],
	       sizeof(vport->auth.dh_group_priority));
	vport->auth.dh_group_len =
		auth_rsp->u.dhchap_security_config.dh_group_len;
	old_interval = vport->auth.reauth_interval;
	vport->auth.reauth_interval =
		auth_rsp->u.dhchap_security_config.reauth_interval;
	new_interval = vport->auth.reauth_interval;
	/*
	 * If interval changed we need to adjust the running timer
	 *  If enabled then start timer now.
	 *  If disabled then stop the timer.
	 *  If changed to chorter then elapsed time, then set to fire now
	 *  If changed to longer than elapsed time, extend the timer.
	 */
	if (old_interval != new_interval &&
	    vport->auth.auth_state == LPFC_AUTH_SUCCESS) {
		new_jiffies = msecs_to_jiffies(new_interval * 60000);
		del_timer_sync(&ndlp->nlp_reauth_tmr);
		if (old_interval == 0)
			temp_jiffies = jiffies + new_jiffies;
		if (new_interval == 0)
			temp_jiffies = 0;
		else if (new_jiffies < (jiffies - vport->auth.last_auth))
			temp_jiffies = jiffies + msecs_to_jiffies(1);
		else
			temp_jiffies = jiffies + (new_jiffies -
				(jiffies - vport->auth.last_auth));
		if (temp_jiffies)
			mod_timer(&ndlp->nlp_reauth_tmr, temp_jiffies);
	}
	last_auth_mode = vport->auth.auth_mode;
	vport->auth.auth_mode =
		auth_rsp->u.dhchap_security_config.auth_mode;
	lpfc_printf_vlog(vport, KERN_INFO, LOG_SECURITY,
		"1025 Received security config local_wwpn:"
		 "%llX remote_wwpn:%llX mode:0x%x "
		 "hash(%d):%x:%x:%x:%x bidir:0x%x "
		 "dh_group(%d):%x:%x:%x:%x:%x:%x:%x:%x "
		 "reauth_interval:0x%x\n",
		 (unsigned long long)auth_rsp->local_wwpn,
		 (unsigned long long)auth_rsp->remote_wwpn,
		 auth_rsp->u.dhchap_security_config.auth_mode,
		 auth_rsp->u.dhchap_security_config.hash_len,
		 auth_rsp->u.dhchap_security_config.hash_priority[0],
		 auth_rsp->u.dhchap_security_config.hash_priority[1],
		 auth_rsp->u.dhchap_security_config.hash_priority[2],
		 auth_rsp->u.dhchap_security_config.hash_priority[3],
		 auth_rsp->u.dhchap_security_config.bidirectional,
		 auth_rsp->u.dhchap_security_config.dh_group_len,
		 auth_rsp->u.dhchap_security_config.dh_group_priority[0],
		 auth_rsp->u.dhchap_security_config.dh_group_priority[1],
		 auth_rsp->u.dhchap_security_config.dh_group_priority[2],
		 auth_rsp->u.dhchap_security_config.dh_group_priority[3],
		 auth_rsp->u.dhchap_security_config.dh_group_priority[4],
		 auth_rsp->u.dhchap_security_config.dh_group_priority[5],
		 auth_rsp->u.dhchap_security_config.dh_group_priority[6],
		 auth_rsp->u.dhchap_security_config.dh_group_priority[7],
		 auth_rsp->u.dhchap_security_config.reauth_interval);
	kfree(auth_rsp);
	if (vport->auth.auth_mode == FC_AUTHMODE_ACTIVE)
		vport->auth.security_active = 1;
	else if (vport->auth.auth_mode == FC_AUTHMODE_PASSIVE) {
		if (ndlp->nlp_flag & NLP_SC_REQ)
			vport->auth.security_active = 1;
		else {
			lpfc_printf_vlog(vport, KERN_INFO, LOG_SECURITY,
					 "1038 Authentication not "
					 "required by the fabric. "
					 "Disabled.\n");
			vport->auth.security_active = 0;
		}
	} else {
		vport->auth.security_active = 0;
		/*
		* If switch require authentication and authentication
		* is disabled for this HBA/Fabric port, fail the
		* discovery.
		*/
		if (ndlp->nlp_flag & NLP_SC_REQ) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
					 "1050 Authentication mode is "
					 "disabled, but is required by "
					 "the fabric.\n");
			lpfc_vport_set_state(vport, FC_VPORT_FAILED);
			/* Cancel discovery timer */
			lpfc_can_disctmo(vport);
		}
	}

	if (last_auth_mode == FC_AUTHMODE_UNKNOWN) {
		if (vport->auth.security_active)
			lpfc_start_authentication(vport, ndlp);
		else
			lpfc_start_discovery(vport);
	}
}

int
lpfc_get_security_enabled(struct Scsi_Host *shost)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;

	return vport->cfg_enable_auth;
}

int
lpfc_security_wait(struct lpfc_vport *vport)
{
	int i = 0;
	if (vport->security_service_state == SECURITY_ONLINE)
		return 0;
	lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
			"1058 Waiting for authentication service...\n");
	while (vport->security_service_state == SECURITY_OFFLINE) {
		i++;
		if (i > SECURITY_WAIT_TMO * 2)
			return -ETIMEDOUT;
		/* Delay for half of a second */
		msleep(500);
	}
	lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
			"1059 Authentication service online.\n");
	return 0;
}

int
lpfc_security_config_wait(struct lpfc_vport *vport)
{
	int i = 0;

	while (vport->auth.auth_mode == FC_AUTHMODE_UNKNOWN) {
		i++;
		if (i > 120)
			return -ETIMEDOUT;
		/* Delay for half of a second */
		msleep(500);
	}
	return 0;
}

void
lpfc_reauth_node(unsigned long ptr)
{
	struct lpfc_nodelist *ndlp = (struct lpfc_nodelist *) ptr;
	struct lpfc_vport *vport = ndlp->vport;
	struct lpfc_hba   *phba = vport->phba;
	unsigned long flags;
	struct lpfc_work_evt  *evtp = &ndlp->els_reauth_evt;

	ndlp = (struct lpfc_nodelist *) ptr;
	phba = ndlp->phba;

	spin_lock_irqsave(&phba->hbalock, flags);
	if (!list_empty(&evtp->evt_listp)) {
		spin_unlock_irqrestore(&phba->hbalock, flags);
		return;
	}

	/* We need to hold the node resource by incrementing the reference
	 * count until this queued work is done
	 */
	evtp->evt_arg1 = lpfc_nlp_get(ndlp);
	if (evtp->evt_arg1) {
		evtp->evt = LPFC_EVT_REAUTH;
		list_add_tail(&evtp->evt_listp, &phba->work_list);
		lpfc_worker_wake_up(phba);
	}
	spin_unlock_irqrestore(&phba->hbalock, flags);
	return;
}

void
lpfc_reauthentication_handler(struct lpfc_nodelist *ndlp)
{
	struct lpfc_vport *vport = ndlp->vport;
	if (vport->auth.auth_msg_state != LPFC_DHCHAP_SUCCESS)
		return;

	if (lpfc_start_node_authentication(ndlp)) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
				 "1029 Reauthentication Failure\n");
		if (vport->auth.auth_state == LPFC_AUTH_SUCCESS)
			lpfc_port_auth_failed(ndlp, LPFC_AUTH_FAIL);
	}
}

/*
 * This function will kick start authentication for a node.
 * This is used for re-authentication of a node or a user
 * initiated node authentication.
 */
int
lpfc_start_node_authentication(struct lpfc_nodelist *ndlp)
{
	struct lpfc_vport *vport;
	int ret;

	vport = ndlp->vport;
	/* If there is authentication timer cancel the timer */
	del_timer_sync(&ndlp->nlp_reauth_tmr);
	ret = lpfc_get_auth_config(vport, ndlp);
	if (ret)
		return ret;
	ret = lpfc_security_config_wait(vport);
	if (ret) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
				 "1032 Start Authentication: get config "
				 "timed out.\n");
		return ret;
	}
	return 0;
}

int
lpfc_get_auth_config(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp)
{
	struct fc_auth_req auth_req;
	struct fc_auth_rsp *auth_rsp;
	struct Scsi_Host   *shost;
	int ret;

	shost = lpfc_shost_from_vport(vport);

	auth_req.local_wwpn = wwn_to_u64(vport->fc_portname.u.wwn);
	if ((ndlp == NULL) || (ndlp->nlp_type & NLP_FABRIC))
		auth_req.remote_wwpn = AUTH_FABRIC_WWN;
	else
		auth_req.remote_wwpn = wwn_to_u64(ndlp->nlp_portname.u.wwn);
	if (vport->security_service_state == SECURITY_OFFLINE) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
				 "1053 Start Authentication: "
				 "Security service offline.\n");
		return -EINVAL;
	}
	auth_rsp = kmalloc(sizeof(struct fc_auth_rsp), GFP_KERNEL);
	if (!auth_rsp) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
				 "1028 Start Authentication: No buffers\n");
		return -ENOMEM;
	}
	vport->auth.auth_mode = FC_AUTHMODE_UNKNOWN;
	ret = lpfc_fc_security_get_config(shost, &auth_req,
					  sizeof(struct fc_auth_req),
					  auth_rsp,
					  sizeof(struct fc_auth_rsp));
	if (ret) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
				 "1031 Start Authentication: Get config "
				 "failed.\n");
		kfree(auth_rsp);
		return ret;
	}
	return 0;
}
