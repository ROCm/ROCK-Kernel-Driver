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
#include <linux/blkdev.h>
#include <linux/pci.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>	/* workqueue stuff, HZ */
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi_cmnd.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/security.h>
#include <net/sock.h>
#include <net/netlink.h>

#include <scsi/scsi.h>

#include "lpfc_hw4.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_sli4.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_vport.h"
#include "lpfc_auth_access.h"

/* fc security */
struct workqueue_struct *security_work_q;
struct list_head fc_security_user_list;
int fc_service_state = FC_SC_SERVICESTATE_UNKNOWN;
static int fc_service_pid;
DEFINE_SPINLOCK(fc_security_user_lock);

static inline struct lpfc_vport *
lpfc_fc_find_vport(unsigned long host_no)
{
	struct lpfc_vport *vport;
	struct Scsi_Host *shost;

	list_for_each_entry(vport, &fc_security_user_list, sc_users) {
		shost = lpfc_shost_from_vport(vport);
		if (shost && (shost->host_no == host_no))
			return vport;
	}

	return NULL;
}


/**
 * lpfc_fc_sc_add_timer
 *
 *
 **/

void
lpfc_fc_sc_add_timer(struct fc_security_request *req, int timeout,
		    void (*complete)(struct fc_security_request *))
{

	init_timer(&req->timer);


	req->timer.data = (unsigned long)req;
	req->timer.expires = jiffies + timeout;
	req->timer.function = (void (*)(unsigned long)) complete;

	add_timer(&req->timer);
}
/**
 * lpfc_fc_sc_req_times_out
 *
 *
 **/

void
lpfc_fc_sc_req_times_out(struct fc_security_request *req)
{

	unsigned long flags;
	int found = 0;
	struct fc_security_request *fc_sc_req;
	struct lpfc_vport *vport = req->vport;
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);

	if (!req)
		return;

	spin_lock_irqsave(shost->host_lock, flags);

	/* To avoid a completion race check to see if request is on the list */

	list_for_each_entry(fc_sc_req, &vport->sc_response_wait_queue, rlist)
		if (fc_sc_req == req) {
			found = 1;
			break;
		}

	if (!found) {
		spin_unlock_irqrestore(shost->host_lock, flags);
		return;
		}

	list_del(&fc_sc_req->rlist);

	spin_unlock_irqrestore(shost->host_lock, flags);

	lpfc_printf_vlog(vport, KERN_WARNING, LOG_SECURITY,
			 "1019 Request tranid %d timed out\n",
			 fc_sc_req->tran_id);

	switch (fc_sc_req->req_type) {

	case FC_NL_SC_GET_CONFIG_REQ:
		lpfc_security_config(shost, -ETIMEDOUT,
			fc_sc_req->data);
		break;

	case FC_NL_SC_DHCHAP_MAKE_CHALLENGE_REQ:
		lpfc_dhchap_make_challenge(shost, -ETIMEDOUT,
			fc_sc_req->data, 0);
		break;

	case FC_NL_SC_DHCHAP_MAKE_RESPONSE_REQ:
		lpfc_dhchap_make_response(shost, -ETIMEDOUT,
			fc_sc_req->data, 0);
		break;

	case FC_NL_SC_DHCHAP_AUTHENTICATE_REQ:
		lpfc_dhchap_authenticate(shost, -ETIMEDOUT, fc_sc_req->data, 0);
		break;
	}

	kfree(fc_sc_req);

}


static inline struct fc_security_request *
lpfc_fc_find_sc_request(u32 tran_id, u32 type, struct lpfc_vport *vport)
{
	struct fc_security_request *fc_sc_req;

	list_for_each_entry(fc_sc_req, &vport->sc_response_wait_queue, rlist)
		if (fc_sc_req->tran_id == tran_id &&
			fc_sc_req->req_type == type)
			return fc_sc_req;
	return NULL;
}



/**
 * lpfc_fc_sc_request
 *
 *
 **/

int
lpfc_fc_sc_request(struct lpfc_vport *vport,
	      u32 msg_type,
	      struct fc_auth_req *auth_req,
	      u32 auth_req_len, /* includes length of struct fc_auth_req */
	      struct fc_auth_rsp *auth_rsp,
	      u32 auth_rsp_len)	/* includes length of struct fc_auth_rsp */
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct fc_security_request *fc_sc_req;
	struct fc_nl_sc_message *fc_nl_sc_msg;
	unsigned long flags;
	u32 len;
	u32 seq = ++vport->sc_tran_id;

	if (fc_service_state != FC_SC_SERVICESTATE_ONLINE)
		return -EINVAL;

	if (vport->port_state == FC_PORTSTATE_DELETED)
		return -EINVAL;

	fc_sc_req = kzalloc(sizeof(struct fc_security_request), GFP_KERNEL);
	if (!fc_sc_req)
		return -ENOMEM;

	fc_sc_req->req_type = msg_type;
	fc_sc_req->data = auth_rsp;
	fc_sc_req->data_len = auth_rsp_len;
	fc_sc_req->vport = vport;
	fc_sc_req->tran_id = seq;

	len = sizeof(struct fc_nl_sc_message) + auth_req_len;
	fc_nl_sc_msg = kzalloc(len, GFP_KERNEL);
	if (!fc_nl_sc_msg) {
		kfree(fc_sc_req);
		return -ENOMEM;
	}
	fc_nl_sc_msg->msgtype = msg_type;
	fc_nl_sc_msg->data_len = auth_req_len;
	memcpy(fc_nl_sc_msg->data, auth_req, auth_req_len);
	fc_nl_sc_msg->tran_id = seq;

	lpfc_fc_sc_add_timer(fc_sc_req, FC_SC_REQ_TIMEOUT,
			     lpfc_fc_sc_req_times_out);

	spin_lock_irqsave(shost->host_lock, flags);
	list_add_tail(&fc_sc_req->rlist, &vport->sc_response_wait_queue);
	spin_unlock_irqrestore(shost->host_lock, flags);
	scsi_nl_send_vendor_msg(fc_service_pid, shost->host_no,
				(SCSI_NL_VID_TYPE_PCI | PCI_VENDOR_ID_EMULEX),
				(char *) fc_nl_sc_msg, len);
	kfree(fc_nl_sc_msg);
	return 0;
}

/**
 * lpfc_fc_security_get_config
 *
 *
 **/

int
lpfc_fc_security_get_config(struct Scsi_Host *shost,
			struct fc_auth_req *auth_req,
			u32 auth_req_len,
			struct fc_auth_rsp *auth_rsp,
			u32 auth_rsp_len)
{

	return lpfc_fc_sc_request((struct lpfc_vport *) shost->hostdata,
				  FC_NL_SC_GET_CONFIG_REQ, auth_req,
				  auth_req_len, auth_rsp, auth_rsp_len);

}
EXPORT_SYMBOL(lpfc_fc_security_get_config);

/**
 * lpfc_fc_security_dhchap_make_challenge
 *
 *
 **/

int
lpfc_fc_security_dhchap_make_challenge(struct Scsi_Host *shost,
				  struct fc_auth_req *auth_req,
				  u32 auth_req_len,
				  struct fc_auth_rsp *auth_rsp,
				  u32 auth_rsp_len)
{

	return lpfc_fc_sc_request((struct lpfc_vport *) shost->hostdata,
				  FC_NL_SC_DHCHAP_MAKE_CHALLENGE_REQ,
				  auth_req, auth_req_len,
				  auth_rsp, auth_rsp_len);

}
EXPORT_SYMBOL(lpfc_fc_security_dhchap_make_challenge);

/**
 * lpfc_fc_security_dhchap_make_response
 *
 *
 **/

int
lpfc_fc_security_dhchap_make_response(struct Scsi_Host *shost,
			struct fc_auth_req *auth_req,
			u32 auth_req_len,
			struct fc_auth_rsp *auth_rsp,
			u32 auth_rsp_len)
{

	return lpfc_fc_sc_request((struct lpfc_vport *) shost->hostdata,
				  FC_NL_SC_DHCHAP_MAKE_RESPONSE_REQ,
				  auth_req, auth_req_len,
				  auth_rsp, auth_rsp_len);

}
EXPORT_SYMBOL(lpfc_fc_security_dhchap_make_response);


/**
 * lpfc_fc_security_dhchap_authenticate
 *
 *
 **/

int
lpfc_fc_security_dhchap_authenticate(struct Scsi_Host *shost,
			struct fc_auth_req *auth_req,
			u32 auth_req_len,
			struct fc_auth_rsp *auth_rsp,
			u32 auth_rsp_len)
{

	return lpfc_fc_sc_request((struct lpfc_vport *) shost->hostdata,
				  FC_NL_SC_DHCHAP_AUTHENTICATE_REQ,
				  auth_req, auth_req_len,
				  auth_rsp, auth_rsp_len);

}
EXPORT_SYMBOL(lpfc_fc_security_dhchap_authenticate);

/**
 * lpfc_fc_queue_security_work - Queue work to the fc_host security workqueue.
 * @shost:	Pointer to Scsi_Host bound to fc_host.
 * @work:	Work to queue for execution.
 *
 * Return value:
 *	1 - work queued for execution
 *	0 - work is already queued
 *	-EINVAL - work queue doesn't exist
 **/
int
lpfc_fc_queue_security_work(struct lpfc_vport *vport, struct work_struct *work)
{
	if (unlikely(!security_work_q)) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
			"1021 ERROR: attempted to queue security work, "
			"when no workqueue created.\n");
		dump_stack();

		return -EINVAL;
	}

	return queue_work(security_work_q, work);

}



 /**
 * lpfc_fc_sc_schedule_notify_all
 *
 *
 **/

void
lpfc_fc_sc_schedule_notify_all(int message)
{
	struct lpfc_vport *vport;
	unsigned long flags;

	spin_lock_irqsave(&fc_security_user_lock, flags);
	list_for_each_entry(vport, &fc_security_user_list, sc_users) {
		switch (message) {
		case FC_NL_SC_REG:
			lpfc_fc_queue_security_work(vport,
						    &vport->sc_online_work);
			break;
		case FC_NL_SC_DEREG:
			lpfc_fc_queue_security_work(vport,
						    &vport->sc_offline_work);
			break;
		}
	}
	spin_unlock_irqrestore(&fc_security_user_lock, flags);
}



/**
 * lpfc_fc_sc_security_online
 *
 *
 **/

void
lpfc_fc_sc_security_online(struct work_struct *work)
{
	struct lpfc_vport *vport = container_of(work, struct lpfc_vport,
						sc_online_work);
	lpfc_security_service_online(lpfc_shost_from_vport(vport));
	return;
}

/**
 * lpfc_fc_sc_security_offline
 *
 *
 **/
void
lpfc_fc_sc_security_offline(struct work_struct *work)
{
	struct lpfc_vport *vport = container_of(work, struct lpfc_vport,
						sc_offline_work);
	lpfc_security_service_offline(lpfc_shost_from_vport(vport));
	return;
}


/**
 * lpfc_fc_sc_process_msg
 *
 *
 **/
static void
lpfc_fc_sc_process_msg(struct work_struct *work)
{
	struct fc_sc_msg_work_q_wrapper *wqw =
		container_of(work, struct fc_sc_msg_work_q_wrapper, work);

	switch (wqw->msgtype) {

	case FC_NL_SC_GET_CONFIG_RSP:
		lpfc_security_config(lpfc_shost_from_vport(wqw->fc_sc_req->
				vport), wqw->status,
				wqw->fc_sc_req->data);
		break;

	case FC_NL_SC_DHCHAP_MAKE_CHALLENGE_RSP:
		lpfc_dhchap_make_challenge(lpfc_shost_from_vport(wqw->
					fc_sc_req->vport), wqw->status,
					wqw->fc_sc_req->data, wqw->data_len);
		break;

	case FC_NL_SC_DHCHAP_MAKE_RESPONSE_RSP:
		lpfc_dhchap_make_response(lpfc_shost_from_vport(wqw->
					fc_sc_req->vport), wqw->status,
					wqw->fc_sc_req->data, wqw->data_len);
		break;

	case FC_NL_SC_DHCHAP_AUTHENTICATE_RSP:
		lpfc_dhchap_authenticate(lpfc_shost_from_vport(wqw->fc_sc_req->
					vport),
					wqw->status,
					wqw->fc_sc_req->data, wqw->data_len);
		break;
	}

	kfree(wqw->fc_sc_req);
	kfree(wqw);

	return;
}


/**
 * lpfc_fc_sc_schedule_msg
 *
 *
 **/

int
lpfc_fc_sc_schedule_msg(struct Scsi_Host *shost,
			struct fc_nl_sc_message *fc_nl_sc_msg, int rcvlen)
{
	struct fc_security_request *fc_sc_req;
	u32 req_type;
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	int err = 0;
	struct fc_sc_msg_work_q_wrapper *wqw;
	unsigned long flags;

	if (vport->port_state == FC_PORTSTATE_DELETED) {
		printk(KERN_WARNING
		"%s: Host being deleted.\n", __func__);
		return -EBADR;
	}

	wqw = kzalloc(sizeof(struct fc_sc_msg_work_q_wrapper), GFP_KERNEL);

	if (!wqw)
		return -ENOMEM;

	switch (fc_nl_sc_msg->msgtype) {
	case FC_NL_SC_GET_CONFIG_RSP:
		req_type = FC_NL_SC_GET_CONFIG_REQ;
		break;

	case FC_NL_SC_DHCHAP_MAKE_CHALLENGE_RSP:
		req_type = FC_NL_SC_DHCHAP_MAKE_CHALLENGE_REQ;
		break;

	case FC_NL_SC_DHCHAP_MAKE_RESPONSE_RSP:
		req_type = FC_NL_SC_DHCHAP_MAKE_RESPONSE_REQ;
		break;

	case FC_NL_SC_DHCHAP_AUTHENTICATE_RSP:
		req_type = FC_NL_SC_DHCHAP_AUTHENTICATE_REQ;
		break;

	default:
		kfree(wqw);
		return -EINVAL;
	}

	spin_lock_irqsave(shost->host_lock, flags);

	fc_sc_req = lpfc_fc_find_sc_request(fc_nl_sc_msg->tran_id,
				req_type, vport);

	if (!fc_sc_req) {
		spin_unlock_irqrestore(shost->host_lock, flags);
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_SECURITY,
				 "1022 Security request does not exist.\n");
		kfree(wqw);
		return -EBADR;
	}

	list_del(&fc_sc_req->rlist);

	spin_unlock_irqrestore(shost->host_lock, flags);

	del_singleshot_timer_sync(&fc_sc_req->timer);

	wqw->status = 0;
	wqw->fc_sc_req = fc_sc_req;
	wqw->data_len = rcvlen;
	wqw->msgtype = fc_nl_sc_msg->msgtype;

	if (!fc_sc_req->data ||
		(fc_sc_req->data_len < fc_nl_sc_msg->data_len)) {
		wqw->status = -ENOBUFS;
		wqw->data_len = 0;
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_SECURITY,
				 "1023 Warning - data may have been truncated. "
				 "data:%p reqdl:%x mesdl:%x\n",
				 fc_sc_req->data,
				 fc_sc_req->data_len, fc_nl_sc_msg->data_len);
	} else {
		memcpy(fc_sc_req->data, fc_nl_sc_msg->data,
			fc_nl_sc_msg->data_len);
	}

	INIT_WORK(&wqw->work, lpfc_fc_sc_process_msg);
	lpfc_fc_queue_security_work(vport, &wqw->work);

	return err;
}

int
lpfc_rcv_nl_msg(struct Scsi_Host *shost, void *payload,
		uint32_t len, uint32_t pid)
{
	struct fc_nl_sc_message *msg = (struct fc_nl_sc_message *)payload;
	int err = 0;
printk("%s %d - msgtype:%x\n", __func__, __LINE__, msg->msgtype);
	switch (msg->msgtype) {
	case FC_NL_SC_REG:
		fc_service_pid = pid;
		fc_service_state = FC_SC_SERVICESTATE_ONLINE;
		lpfc_fc_sc_schedule_notify_all(FC_NL_SC_REG);
		break;
	case FC_NL_SC_DEREG:
		fc_service_pid = pid;
		fc_service_state = FC_SC_SERVICESTATE_OFFLINE;
		lpfc_fc_sc_schedule_notify_all(FC_NL_SC_DEREG);
		break;
	case FC_NL_SC_GET_CONFIG_RSP:
	case FC_NL_SC_DHCHAP_MAKE_CHALLENGE_RSP:
	case FC_NL_SC_DHCHAP_MAKE_RESPONSE_RSP:
	case FC_NL_SC_DHCHAP_AUTHENTICATE_RSP:
		err = lpfc_fc_sc_schedule_msg(shost, msg, len);
		break;
	default:
		printk(KERN_WARNING "%s: unknown msg type 0x%x len %d\n",
		       __func__, msg->msgtype, len);
		break;
	}
	return err;
}

void
lpfc_rcv_nl_event(struct notifier_block *this,
		  unsigned long event,
		  void *ptr)
{
	struct netlink_notify *n = ptr;

	if ((event == NETLINK_URELEASE) &&
	    (n->protocol == NETLINK_SCSITRANSPORT) && (n->pid)) {
		printk(KERN_WARNING "Warning - Security Service Offline\n");
		fc_service_state = FC_SC_SERVICESTATE_OFFLINE;
		lpfc_fc_sc_schedule_notify_all(FC_NL_SC_DEREG);
	}
}
