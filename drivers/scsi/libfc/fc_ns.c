/*
 * Copyright(c) 2007 - 2008 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained at www.Open-FCoE.org
 */

/*
 * Target Discovery
 * Actually, this discovers all FC-4 remote ports, including FCP initiators.
 */

#include <linux/timer.h>
#include <linux/err.h>
#include <asm/unaligned.h>

#include <scsi/fc/fc_gs.h>

#include <scsi/libfc/libfc.h>

#define FC_NS_RETRY_LIMIT	3	/* max retries */
#define FC_NS_RETRY_DELAY	500UL	/* (msecs) delay */

int fc_ns_debug;

static void fc_ns_gpn_ft_req(struct fc_lport *);
static void fc_ns_gpn_ft_resp(struct fc_seq *, struct fc_frame *, void *);
static int fc_ns_new_target(struct fc_lport *, struct fc_rport *,
			    struct fc_rport_identifiers *);
static void fc_ns_del_target(struct fc_lport *, struct fc_rport *);
static void fc_ns_disc_done(struct fc_lport *);
static void fcdt_ns_error(struct fc_lport *, struct fc_frame *);
static void fc_ns_timeout(struct work_struct *);

/**
 * struct fc_ns_port - temporary discovery port to hold rport identifiers
 * @lp: Fibre Channel host port instance
 * @peers: node for list management during discovery and RSCN processing
 * @ids: identifiers structure to pass to fc_remote_port_add()
 */
struct fc_ns_port {
	struct fc_lport *lp;
	struct list_head peers;
	struct fc_rport_identifiers ids;
};

static int fc_ns_gpn_id_req(struct fc_lport *, struct fc_ns_port *);
static void fc_ns_gpn_id_resp(struct fc_seq *, struct fc_frame *, void *);
static void fc_ns_gpn_id_error(struct fc_ns_port *rp, struct fc_frame *fp);

static int fc_ns_gnn_id_req(struct fc_lport *, struct fc_ns_port *);
static void fc_ns_gnn_id_resp(struct fc_seq *, struct fc_frame *, void *);
static void fc_ns_gnn_id_error(struct fc_ns_port *, struct fc_frame *);
static void fc_ns_enter_reg_pn(struct fc_lport *lp);
static void fc_ns_error(struct fc_lport *lp, struct fc_frame *fp);
static void fc_lport_fill_dns_hdr(struct fc_lport *lp, struct fc_ct_hdr *ct,
				  unsigned int op, unsigned int req_size);
static void fc_ns_resp(struct fc_seq *sp, struct fc_frame *fp,
		       void *lp_arg);
static void fc_ns_retry(struct fc_lport *lp);
static void fc_ns_single(struct fc_lport *, struct fc_ns_port *);
static int fc_ns_restart(struct fc_lport *);


/**
 * fc_ns_rscn_req - Handle Registered State Change Notification (RSCN)
 * @sp: Current sequence of the RSCN exchange
 * @fp: RSCN Frame
 * @lp: Fibre Channel host port instance
 */
static void fc_ns_rscn_req(struct fc_seq *sp, struct fc_frame *fp,
			   struct fc_lport *lp)
{
	struct fc_els_rscn *rp;
	struct fc_els_rscn_page *pp;
	struct fc_seq_els_data rjt_data;
	unsigned int len;
	int redisc = 0;
	enum fc_els_rscn_ev_qual ev_qual;
	enum fc_els_rscn_addr_fmt fmt;
	LIST_HEAD(disc_list);
	struct fc_ns_port *dp, *next;

	rp = fc_frame_payload_get(fp, sizeof(*rp));

	if (!rp || rp->rscn_page_len != sizeof(*pp))
		goto reject;

	len = ntohs(rp->rscn_plen);
	if (len < sizeof(*rp))
		goto reject;
	len -= sizeof(*rp);

	for (pp = (void *)(rp + 1); len; len -= sizeof(*pp), pp++) {
		ev_qual = pp->rscn_page_flags >> ELS_RSCN_EV_QUAL_BIT;
		ev_qual &= ELS_RSCN_EV_QUAL_MASK;
		fmt = pp->rscn_page_flags >> ELS_RSCN_ADDR_FMT_BIT;
		fmt &= ELS_RSCN_ADDR_FMT_MASK;
		/*
		 * if we get an address format other than port
		 * (area, domain, fabric), then do a full discovery
		 */
		switch (fmt) {
		case ELS_ADDR_FMT_PORT:
			dp = kzalloc(sizeof(*dp), GFP_KERNEL);
			if (!dp) {
				redisc = 1;
				break;
			}
			dp->lp = lp;
			dp->ids.port_id = ntoh24(pp->rscn_fid);
			dp->ids.port_name = -1;
			dp->ids.node_name = -1;
			dp->ids.roles = FC_RPORT_ROLE_UNKNOWN;
			list_add_tail(&dp->peers, &disc_list);
			break;
		case ELS_ADDR_FMT_AREA:
		case ELS_ADDR_FMT_DOM:
		case ELS_ADDR_FMT_FAB:
		default:
			redisc = 1;
			break;
		}
	}
	lp->tt.seq_els_rsp_send(sp, ELS_LS_ACC, NULL);
	if (redisc) {
		if (fc_ns_debug)
			FC_DBG("RSCN received: rediscovering\n");
		list_for_each_entry_safe(dp, next, &disc_list, peers) {
			list_del(&dp->peers);
			kfree(dp);
		}
		fc_ns_restart(lp);
	} else {
		if (fc_ns_debug)
			FC_DBG("RSCN received: not rediscovering. "
				"redisc %d state %d in_prog %d\n",
				redisc, lp->state, lp->ns_disc_pending);
		list_for_each_entry_safe(dp, next, &disc_list, peers) {
			list_del(&dp->peers);
			fc_ns_single(lp, dp);
		}
	}
	fc_frame_free(fp);
	return;
reject:
	rjt_data.fp = NULL;
	rjt_data.reason = ELS_RJT_LOGIC;
	rjt_data.explan = ELS_EXPL_NONE;
	lp->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &rjt_data);
	fc_frame_free(fp);
}

static void fc_ns_recv_req(struct fc_seq *sp, struct fc_frame *fp,
			   struct fc_lport *lp)
{
	switch (fc_frame_payload_op(fp)) {
	case ELS_RSCN:
		fc_ns_rscn_req(sp, fp, lp);
		break;
	default:
		FC_DBG("fc_ns recieved an unexpected request\n");
		break;
	}
}

/**
 * fc_ns_scr_resp - Handle response to State Change Register (SCR) request
 * @sp: current sequence in SCR exchange
 * @fp: response frame
 * @lp_arg: Fibre Channel host port instance
 */
static void fc_ns_scr_resp(struct fc_seq *sp, struct fc_frame *fp,
			   void *lp_arg)
{
	struct fc_lport *lp = lp_arg;
	int err;

	if (IS_ERR(fp))
		fc_ns_error(lp, fp);
	else {
		fc_lport_lock(lp);
		fc_lport_state_enter(lp, LPORT_ST_READY);
		fc_lport_unlock(lp);
		err = lp->tt.disc_start(lp);
		if (err)
			FC_DBG("target discovery start error\n");
		fc_frame_free(fp);
	}
}

/**
 * fc_ns_enter scr - Send a State Change Register (SCR) request
 * @lp: Fibre Channel host port instance
 */
static void fc_ns_enter_scr(struct fc_lport *lp)
{
	struct fc_frame *fp;
	struct fc_els_scr *scr;

	if (fc_ns_debug)
		FC_DBG("Processing SCR state\n");

	fc_lport_state_enter(lp, LPORT_ST_SCR);

	fp = fc_frame_alloc(lp, sizeof(*scr));
	if (fp) {
		scr = fc_frame_payload_get(fp, sizeof(*scr));
		memset(scr, 0, sizeof(*scr));
		scr->scr_cmd = ELS_SCR;
		scr->scr_reg_func = ELS_SCRF_FULL;
	}
	fc_frame_setup(fp, FC_RCTL_ELS_REQ, FC_TYPE_ELS);
	fc_frame_set_offset(fp, 0);

	lp->tt.exch_seq_send(lp, fp,
			     fc_ns_scr_resp,
			     lp, lp->e_d_tov,
			     lp->fid, FC_FID_FCTRL,
			     FC_FC_SEQ_INIT | FC_FC_END_SEQ);
}

/**
 * fc_ns_enter_reg_ft - Register FC4-types with the name server
 * @lp: Fibre Channel host port instance
 */
static void fc_ns_enter_reg_ft(struct fc_lport *lp)
{
	struct fc_frame *fp;
	struct req {
		struct fc_ct_hdr ct;
		struct fc_ns_fid fid;	/* port ID object */
		struct fc_ns_fts fts;	/* FC4-types object */
	} *req;
	struct fc_ns_fts *lps;
	int i;

	if (fc_ns_debug)
		FC_DBG("Processing REG_FT state\n");

	fc_lport_state_enter(lp, LPORT_ST_REG_FT);

	lps = &lp->fcts;
	i = sizeof(lps->ff_type_map) / sizeof(lps->ff_type_map[0]);
	while (--i >= 0)
		if (ntohl(lps->ff_type_map[i]) != 0)
			break;
	if (i >= 0) {
		fp = fc_frame_alloc(lp, sizeof(*req));
		if (fp) {
			req = fc_frame_payload_get(fp, sizeof(*req));
			fc_lport_fill_dns_hdr(lp, &req->ct,
					      FC_NS_RFT_ID,
					      sizeof(*req) -
					      sizeof(struct fc_ct_hdr));
			hton24(req->fid.fp_fid, lp->fid);
			req->fts = *lps;
			fc_frame_setup(fp, FC_RCTL_DD_UNSOL_CTL, FC_TYPE_CT);
			if (!lp->tt.exch_seq_send(lp, fp,
						  fc_ns_resp, lp,
						  lp->e_d_tov,
						  lp->fid,
						  lp->dns_rp->port_id,
						  FC_FC_SEQ_INIT |
						  FC_FC_END_SEQ))
				fc_ns_retry(lp);
		} else {
			fc_ns_retry(lp);
		}
	} else {
		fc_ns_enter_scr(lp);
	}
}

/*
 * enter next state for handling an exchange reject or retry exhaustion
 * in the current state.
 */
static void fc_ns_enter_reject(struct fc_lport *lp)
{
	switch (lp->state) {
	case LPORT_ST_NONE:
	case LPORT_ST_READY:
	case LPORT_ST_RESET:
	case LPORT_ST_FLOGI:
	case LPORT_ST_LOGO:
		WARN_ON(1);
		break;
	case LPORT_ST_REG_PN:
		fc_ns_enter_reg_ft(lp);
		break;
	case LPORT_ST_REG_FT:
		fc_ns_enter_scr(lp);
		break;
	case LPORT_ST_SCR:
	case LPORT_ST_DNS_STOP:
		lp->tt.disc_stop(lp);
		break;
	case LPORT_ST_DNS:
		lp->tt.lport_reset(lp);
		break;
	}
}

static void fc_ns_enter_retry(struct fc_lport *lp)
{
	switch (lp->state) {
	case LPORT_ST_NONE:
	case LPORT_ST_RESET:
	case LPORT_ST_READY:
	case LPORT_ST_FLOGI:
	case LPORT_ST_LOGO:
		WARN_ON(1);
		break;
	case LPORT_ST_DNS:
		lp->tt.dns_register(lp);
		break;
	case LPORT_ST_DNS_STOP:
		lp->tt.disc_stop(lp);
		break;
	case LPORT_ST_REG_PN:
		fc_ns_enter_reg_pn(lp);
		break;
	case LPORT_ST_REG_FT:
		fc_ns_enter_reg_ft(lp);
		break;
	case LPORT_ST_SCR:
		fc_ns_enter_scr(lp);
		break;
	}
}

/*
 * Refresh target discovery, perhaps due to an RSCN.
 * A configurable delay is introduced to collect any subsequent RSCNs.
 */
static int fc_ns_restart(struct fc_lport *lp)
{
	fc_lport_lock(lp);
	if (!lp->ns_disc_requested && !lp->ns_disc_pending) {
		schedule_delayed_work(&lp->ns_disc_work,
				msecs_to_jiffies(lp->ns_disc_delay * 1000));
	}
	lp->ns_disc_requested = 1;
	fc_lport_unlock(lp);
	return 0;
}

/* unlocked varient of scsi_target_block from scsi_lib.c */
#include "../scsi_priv.h"

static void __device_block(struct scsi_device *sdev, void *data)
{
	scsi_internal_device_block(sdev);
}

static int __target_block(struct device *dev, void *data)
{
	if (scsi_is_target_device(dev))
		__starget_for_each_device(to_scsi_target(dev),
					  NULL, __device_block);
	return 0;
}

static void __scsi_target_block(struct device *dev)
{
	if (scsi_is_target_device(dev))
		__starget_for_each_device(to_scsi_target(dev),
					  NULL, __device_block);
	else
		device_for_each_child(dev, NULL, __target_block);
}

static void fc_block_rports(struct fc_lport *lp)
{
	struct Scsi_Host *shost = lp->host;
	struct fc_rport *rport;
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);
	list_for_each_entry(rport, &fc_host_rports(shost), peers) {
		/* protect the name service remote port */
		if (rport == lp->dns_rp)
			continue;
		if (rport->port_state != FC_PORTSTATE_ONLINE)
			continue;
		rport->port_state = FC_PORTSTATE_BLOCKED;
		rport->flags |= FC_RPORT_DEVLOSS_PENDING;
		__scsi_target_block(&rport->dev);
	}
	spin_unlock_irqrestore(shost->host_lock, flags);
}

/*
 * Fibre Channel Target discovery.
 *
 * Returns non-zero if discovery cannot be started.
 *
 * Callback is called for each target remote port found in discovery.
 * When discovery is complete, the callback is called with a NULL remote port.
 * Discovery may be restarted after an RSCN is received, causing the
 * callback to be called after discovery complete is indicated.
 */
int fc_ns_disc_start(struct fc_lport *lp)
{
	struct fc_rport *rport;
	int error;
	struct fc_rport_identifiers ids;

	fc_lport_lock(lp);

	/*
	 * If not ready, or already running discovery, just set request flag.
	 */
	if (!fc_lport_test_ready(lp) || lp->ns_disc_pending) {
		lp->ns_disc_requested = 1;
		fc_lport_unlock(lp);
		return 0;
	}
	lp->ns_disc_pending = 1;
	lp->ns_disc_requested = 0;
	lp->ns_disc_retry_count = 0;

	/*
	 * Handle point-to-point mode as a simple discovery
	 * of the remote port.
	 */
	rport = lp->ptp_rp;
	if (rport) {
		ids.port_id = rport->port_id;
		ids.port_name = rport->port_name;
		ids.node_name = rport->node_name;
		ids.roles = FC_RPORT_ROLE_UNKNOWN;
		get_device(&rport->dev);
		fc_lport_unlock(lp);
		error = fc_ns_new_target(lp, rport, &ids);
		put_device(&rport->dev);
		if (!error)
			fc_ns_disc_done(lp);
	} else {
		fc_lport_unlock(lp);
		fc_block_rports(lp);
		fc_ns_gpn_ft_req(lp);	/* get ports by FC-4 type */
		error = 0;
	}
	return error;
}

/*
 * Handle resource allocation problem by retrying in a bit.
 */
static void fc_ns_retry(struct fc_lport *lp)
{
	if (lp->retry_count == 0)
		FC_DBG("local port %6x alloc failure "
		       "- will retry\n", lp->fid);
	if (lp->retry_count < lp->max_retry_count) {
		lp->retry_count++;
		mod_timer(&lp->state_timer,
			  jiffies + msecs_to_jiffies(lp->e_d_tov));
	} else {
		FC_DBG("local port %6x alloc failure "
		       "- retries exhausted\n", lp->fid);
		fc_ns_enter_reject(lp);
	}
}

/*
 * Handle errors on local port requests.
 * Don't get locks if in RESET state.
 * The only possible errors so far are exchange TIMEOUT and CLOSED (reset).
 */
static void fc_ns_error(struct fc_lport *lp, struct fc_frame *fp)
{
	if (lp->state == LPORT_ST_RESET)
		return;

	fc_lport_lock(lp);
	if (PTR_ERR(fp) == -FC_EX_TIMEOUT) {
		if (lp->retry_count < lp->max_retry_count) {
			lp->retry_count++;
			fc_ns_enter_retry(lp);
		} else {
			fc_ns_enter_reject(lp);
		}
	}
	if (fc_ns_debug)
		FC_DBG("error %ld retries %d limit %d\n",
		       PTR_ERR(fp), lp->retry_count, lp->max_retry_count);
	fc_lport_unlock(lp);
}

/*
 * Restart discovery after a delay due to resource shortages.
 * If the error persists, the discovery will be abandoned.
 */
static void fcdt_ns_retry(struct fc_lport *lp)
{
	unsigned long delay = FC_NS_RETRY_DELAY;

	if (!lp->ns_disc_retry_count)
		delay /= 4;	/* timeout faster first time */
	if (lp->ns_disc_retry_count++ < FC_NS_RETRY_LIMIT)
		schedule_delayed_work(&lp->ns_disc_work,
				      msecs_to_jiffies(delay));
	else
		fc_ns_disc_done(lp);
}

/*
 * Test for dNS accept in response payload.
 */
static int fc_lport_dns_acc(struct fc_frame *fp)
{
	struct fc_frame_header *fh;
	struct fc_ct_hdr *ct;
	int rc = 0;

	fh = fc_frame_header_get(fp);
	ct = fc_frame_payload_get(fp, sizeof(*ct));
	if (fh && ct && fh->fh_type == FC_TYPE_CT &&
	    ct->ct_fs_type == FC_FST_DIR &&
	    ct->ct_fs_subtype == FC_NS_SUBTYPE &&
	    ntohs(ct->ct_cmd) == FC_FS_ACC) {
		rc = 1;
	}
	return rc;
}

/*
 * Handle response from name server.
 */
static void
fc_ns_resp(struct fc_seq *sp, struct fc_frame *fp, void *lp_arg)
{
	struct fc_lport *lp = lp_arg;

	if (!IS_ERR(fp)) {
		fc_lport_lock(lp);
		del_timer(&lp->state_timer);
		if (fc_lport_dns_acc(fp)) {
			if (lp->state == LPORT_ST_REG_PN)
				fc_ns_enter_reg_ft(lp);
			else
				fc_ns_enter_scr(lp);

		} else {
			fc_ns_retry(lp);
		}
		fc_lport_unlock(lp);
		fc_frame_free(fp);
	} else
		fc_ns_error(lp, fp);
}

/*
 * Handle new target found by discovery.
 * Create remote port and session if needed.
 * Ignore returns of our own FID & WWPN.
 *
 * If a non-NULL rp is passed in, it is held for the caller, but not for us.
 *
 * Events delivered are:
 *  FC_EV_READY, when remote port is rediscovered.
 */
static int fc_ns_new_target(struct fc_lport *lp,
			    struct fc_rport *rport,
			    struct fc_rport_identifiers *ids)
{
	struct fc_rport_libfc_priv *rp;
	int error = 0;

	if (rport && ids->port_name) {
		if (rport->port_name == -1) {
			/*
			 * Set WWN and fall through to notify of create.
			 */
			fc_rport_set_name(rport, ids->port_name,
					  rport->node_name);
		} else if (rport->port_name != ids->port_name) {
			/*
			 * This is a new port with the same FCID as
			 * a previously-discovered port.  Presumably the old
			 * port logged out and a new port logged in and was
			 * assigned the same FCID.  This should be rare.
			 * Delete the old one and fall thru to re-create.
			 */
			fc_ns_del_target(lp, rport);
			rport = NULL;
		}
	}
	if (((ids->port_name != -1) || (ids->port_id != -1)) &&
	    ids->port_id != lp->fid && ids->port_name != lp->wwpn) {
		if (!rport) {
			rport = lp->tt.rport_lookup(lp, ids->port_id);
			if (rport == NULL)
				rport = lp->tt.rport_create(lp, ids);
			if (!rport)
				error = ENOMEM;
		}
		if (rport) {
			rp = rport->dd_data;
			rp->rp_state = RPORT_ST_INIT;
			lp->tt.rport_login(rport);
		}
	}
	return error;
}

/*
 * Delete the remote port.
 */
static void fc_ns_del_target(struct fc_lport *lp, struct fc_rport *rport)
{
	lp->tt.rport_reset(rport);
	fc_remote_port_delete(rport);	/* release hold from create */
}

/*
 * Done with discovery
 */
static void fc_ns_disc_done(struct fc_lport *lp)
{
	lp->ns_disc_done = 1;
	lp->ns_disc_pending = 0;
	if (lp->ns_disc_requested)
		lp->tt.disc_start(lp);
}

/**
 * fc_ns_fill_dns_hdr - Fill in a name service request header
 * @lp: Fibre Channel host port instance
 * @ct: Common Transport (CT) header structure
 * @op: Name Service request code
 * @req_size: Full size of Name Service request
 */
static void fc_ns_fill_dns_hdr(struct fc_lport *lp, struct fc_ct_hdr *ct,
			       unsigned int op, unsigned int req_size)
{
	memset(ct, 0, sizeof(*ct) + req_size);
	ct->ct_rev = FC_CT_REV;
	ct->ct_fs_type = FC_FST_DIR;
	ct->ct_fs_subtype = FC_NS_SUBTYPE;
	ct->ct_cmd = htons((u16) op);
}

/**
 * fc_ns_gpn_ft_req - Send Get Port Names by FC-4 type (GPN_FT) request
 * @lp: Fibre Channel host port instance
 */
static void fc_ns_gpn_ft_req(struct fc_lport *lp)
{
	struct fc_frame *fp;
	struct fc_seq *sp = NULL;
	struct req {
		struct fc_ct_hdr ct;
		struct fc_ns_gid_ft gid;
	} *rp;
	int error = 0;

	lp->ns_disc_buf_len = 0;
	lp->ns_disc_seq_count = 0;
	fp = fc_frame_alloc(lp, sizeof(*rp));
	if (fp == NULL) {
		error = ENOMEM;
	} else {
		rp = fc_frame_payload_get(fp, sizeof(*rp));
		fc_ns_fill_dns_hdr(lp, &rp->ct, FC_NS_GPN_FT, sizeof(rp->gid));
		rp->gid.fn_fc4_type = FC_TYPE_FCP;

		WARN_ON(!fc_lport_test_ready(lp));

		fc_frame_setup(fp, FC_RCTL_DD_UNSOL_CTL, FC_TYPE_CT);
		sp = lp->tt.exch_seq_send(lp, fp,
					  fc_ns_gpn_ft_resp,
					  lp, lp->e_d_tov,
					  lp->fid,
					  lp->dns_rp->port_id,
					  FC_FC_SEQ_INIT | FC_FC_END_SEQ);
	}
	if (error || sp == NULL)
		fcdt_ns_retry(lp);
}

/*
 * Handle error on dNS request.
 */
static void fcdt_ns_error(struct fc_lport *lp, struct fc_frame *fp)
{
	int err = PTR_ERR(fp);

	switch (err) {
	case -FC_EX_TIMEOUT:
		if (lp->ns_disc_retry_count++ < FC_NS_RETRY_LIMIT) {
			fc_ns_gpn_ft_req(lp);
		} else {
			FC_DBG("err %d - ending\n", err);
			fc_ns_disc_done(lp);
		}
		break;
	default:
		FC_DBG("err %d - ending\n", err);
		fc_ns_disc_done(lp);
		break;
	}
}

/**
 * fc_ns_gpn_ft_parse - Parse the list of IDs and names resulting from a request
 * @lp: Fibre Channel host port instance
 * @buf: GPN_FT response buffer
 * @len: size of response buffer
 */
static int fc_ns_gpn_ft_parse(struct fc_lport *lp, void *buf, size_t len)
{
	struct fc_gpn_ft_resp *np;
	char *bp;
	size_t plen;
	size_t tlen;
	int error = 0;
	struct fc_ns_port *dp;

	/*
	 * Handle partial name record left over from previous call.
	 */
	bp = buf;
	plen = len;
	np = (struct fc_gpn_ft_resp *)bp;
	tlen = lp->ns_disc_buf_len;
	if (tlen) {
		WARN_ON(tlen >= sizeof(*np));
		plen = sizeof(*np) - tlen;
		WARN_ON(plen <= 0);
		WARN_ON(plen >= sizeof(*np));
		if (plen > len)
			plen = len;
		np = &lp->ns_disc_buf;
		memcpy((char *)np + tlen, bp, plen);

		/*
		 * Set bp so that the loop below will advance it to the
		 * first valid full name element.
		 */
		bp -= tlen;
		len += tlen;
		plen += tlen;
		lp->ns_disc_buf_len = (unsigned char) plen;
		if (plen == sizeof(*np))
			lp->ns_disc_buf_len = 0;
	}

	/*
	 * Handle full name records, including the one filled from above.
	 * Normally, np == bp and plen == len, but from the partial case above,
	 * bp, len describe the overall buffer, and np, plen describe the
	 * partial buffer, which if would usually be full now.
	 * After the first time through the loop, things return to "normal".
	 */
	while (plen >= sizeof(*np)) {
		dp = kzalloc(sizeof(*dp), GFP_KERNEL);
		if (!dp)
			break;
		dp->lp = lp;
		dp->ids.port_id = ntoh24(np->fp_fid);
		dp->ids.port_name = ntohll(np->fp_wwpn);
		dp->ids.node_name = -1;
		dp->ids.roles = FC_RPORT_ROLE_UNKNOWN;
		error = fc_ns_gnn_id_req(lp, dp);
		if (error)
			break;
		if (np->fp_flags & FC_NS_FID_LAST) {
			fc_ns_disc_done(lp);
			len = 0;
			break;
		}
		len -= sizeof(*np);
		bp += sizeof(*np);
		np = (struct fc_gpn_ft_resp *)bp;
		plen = len;
	}

	/*
	 * Save any partial record at the end of the buffer for next time.
	 */
	if (error == 0 && len > 0 && len < sizeof(*np)) {
		if (np != &lp->ns_disc_buf)
			memcpy(&lp->ns_disc_buf, np, len);
		lp->ns_disc_buf_len = (unsigned char) len;
	} else {
		lp->ns_disc_buf_len = 0;
	}
	return error;
}

/*
 * Handle retry of memory allocation for remote ports.
 */
static void fc_ns_timeout(struct work_struct *work)
{
	struct fc_lport *lp;

	lp = container_of(work, struct fc_lport, ns_disc_work.work);

	if (lp->ns_disc_pending)
		fc_ns_gpn_ft_req(lp);
	else
		lp->tt.disc_start(lp);
}

/**
 * fc_ns_gpn_ft_resp - Handle a response frame from Get Port Names (GPN_FT)
 * @sp: Current sequence of GPN_FT exchange
 * @fp: response frame
 * @lp_arg: Fibre Channel host port instance
 *
 * The response may be in multiple frames
 */
static void fc_ns_gpn_ft_resp(struct fc_seq *sp, struct fc_frame *fp,
			      void *lp_arg)
{
	struct fc_lport *lp = lp_arg;
	struct fc_ct_hdr *cp;
	struct fc_frame_header *fh;
	unsigned int seq_cnt;
	void *buf = NULL;
	unsigned int len;
	int error;

	if (IS_ERR(fp)) {
		fcdt_ns_error(lp, fp);
		return;
	}

	WARN_ON(!fc_frame_is_linear(fp));	/* buffer must be contiguous */
	fh = fc_frame_header_get(fp);
	len = fr_len(fp) - sizeof(*fh);
	seq_cnt = ntohs(fh->fh_seq_cnt);
	if (fr_sof(fp) == FC_SOF_I3 && seq_cnt == 0 &&
	    lp->ns_disc_seq_count == 0) {
		cp = fc_frame_payload_get(fp, sizeof(*cp));
		if (cp == NULL) {
			FC_DBG("GPN_FT response too short, len %d\n",
			       fr_len(fp));
		} else if (ntohs(cp->ct_cmd) == FC_FS_ACC) {

			/*
			 * Accepted.  Parse response.
			 */
			buf = cp + 1;
			len -= sizeof(*cp);
		} else if (ntohs(cp->ct_cmd) == FC_FS_RJT) {
			FC_DBG("GPN_FT rejected reason %x exp %x "
			       "(check zoning)\n", cp->ct_reason,
			       cp->ct_explan);
			fc_ns_disc_done(lp);
		} else {
			FC_DBG("GPN_FT unexpected response code %x\n",
			       ntohs(cp->ct_cmd));
		}
	} else if (fr_sof(fp) == FC_SOF_N3 &&
		   seq_cnt == lp->ns_disc_seq_count) {
		buf = fh + 1;
	} else {
		FC_DBG("GPN_FT unexpected frame - out of sequence? "
		       "seq_cnt %x expected %x sof %x eof %x\n",
		       seq_cnt, lp->ns_disc_seq_count, fr_sof(fp), fr_eof(fp));
	}
	if (buf) {
		error = fc_ns_gpn_ft_parse(lp, buf, len);
		if (error)
			fcdt_ns_retry(lp);
		else
			lp->ns_disc_seq_count++;
	}
	fc_frame_free(fp);
}

/*
 * Discover the directory information for a single target.
 * This could be from an RSCN that reported a change for the target.
 */
static void fc_ns_single(struct fc_lport *lp, struct fc_ns_port *dp)
{
	struct fc_rport *rport;

	if (dp->ids.port_id == lp->fid)
		goto out;

	rport = lp->tt.rport_lookup(lp, dp->ids.port_id);
	if (rport) {
		fc_ns_del_target(lp, rport);
		put_device(&rport->dev); /* hold from lookup */
	}

	if (fc_ns_gpn_id_req(lp, dp) != 0)
		goto error;
	return;
error:
	fc_ns_restart(lp);
out:
	kfree(dp);
}

/**
 * fc_ns_gpn_id_req - Send Get Port Name by ID (GPN_ID) request
 * @lp: Fibre Channel host port instance
 * @dp: Temporary discovery port for holding IDs and world wide names
 *
 * The remote port is held by the caller for us.
 */
static int fc_ns_gpn_id_req(struct fc_lport *lp, struct fc_ns_port *dp)
{
	struct fc_frame *fp;
	struct req {
		struct fc_ct_hdr ct;
		struct fc_ns_fid fid;
	} *cp;
	int error = 0;

	fp = fc_frame_alloc(lp, sizeof(*cp));
	if (fp == NULL)
		return -ENOMEM;

	cp = fc_frame_payload_get(fp, sizeof(*cp));
	fc_ns_fill_dns_hdr(lp, &cp->ct, FC_NS_GPN_ID, sizeof(cp->fid));
	hton24(cp->fid.fp_fid, dp->ids.port_id);

	WARN_ON(!fc_lport_test_ready(lp));

	fc_frame_setup(fp, FC_RCTL_DD_UNSOL_CTL, FC_TYPE_CT);
	if (!lp->tt.exch_seq_send(lp, fp,
				  fc_ns_gpn_id_resp,
				  dp, lp->e_d_tov,
				  lp->fid,
				  lp->dns_rp->port_id,
				  FC_FC_SEQ_INIT | FC_FC_END_SEQ))
		error = -ENOMEM;

	return error;
}

/**
 * fc_ns_gpn_id_resp - Handle response to GPN_ID
 * @sp: Current sequence of GPN_ID exchange
 * @fp: response frame
 * @dp_arg: Temporary discovery port for holding IDs and world wide names
 */
static void fc_ns_gpn_id_resp(struct fc_seq *sp, struct fc_frame *fp,
			      void *dp_arg)
{
	struct fc_ns_port *dp = dp_arg;
	struct fc_lport *lp;
	struct resp {
		struct fc_ct_hdr ct;
		__be64 wwn;
	} *cp;
	unsigned int cmd;

	if (IS_ERR(fp)) {
		fc_ns_gpn_id_error(dp, fp);
		return;
	}

	lp = dp->lp;
	WARN_ON(!fc_frame_is_linear(fp));	/* buffer must be contiguous */

	cp = fc_frame_payload_get(fp, sizeof(cp->ct));
	if (cp == NULL) {
		FC_DBG("GPN_ID response too short, len %d\n", fr_len(fp));
		return;
	}
	cmd = ntohs(cp->ct.ct_cmd);
	switch (cmd) {
	case FC_FS_ACC:
		cp = fc_frame_payload_get(fp, sizeof(*cp));
		if (cp == NULL) {
			FC_DBG("GPN_ID response payload too short, len %d\n",
			       fr_len(fp));
			break;
		}
		dp->ids.port_name = ntohll(cp->wwn);
		fc_ns_gnn_id_req(lp, dp);
		break;
	case FC_FS_RJT:
		fc_ns_restart(lp);
		break;
	default:
		FC_DBG("GPN_ID unexpected CT response cmd %x\n", cmd);
		break;
	}
	fc_frame_free(fp);
}

/**
 * fc_ns_gpn_id_error - Handle error from GPN_ID
 * @dp: Temporary discovery port for holding IDs and world wide names
 * @fp: response frame
 */
static void fc_ns_gpn_id_error(struct fc_ns_port *dp, struct fc_frame *fp)
{
	struct fc_lport *lp = dp->lp;

	switch (PTR_ERR(fp)) {
	case -FC_EX_TIMEOUT:
		fc_ns_restart(lp);
		break;
	case -FC_EX_CLOSED:
	default:
		break;
	}
	kfree(dp);
}

/*
 * Setup session to dNS if not already set up.
 */
static void fc_ns_enter_dns(struct fc_lport *lp)
{
	struct fc_rport *rport;
	struct fc_rport_libfc_priv *rp;
	struct fc_rport_identifiers ids = {
		.port_id = FC_FID_DIR_SERV,
		.port_name = -1,
		.node_name = -1,
		.roles = FC_RPORT_ROLE_UNKNOWN,
	};

	if (fc_ns_debug)
		FC_DBG("Processing DNS state\n");

	fc_lport_state_enter(lp, LPORT_ST_DNS);

	if (!lp->dns_rp) {
		/*
		 * Set up remote port to directory server.
		 */

		/*
		 * we are called with the state_lock, but if rport_lookup_create
		 * needs to create a rport then it will sleep.
		 */
		fc_lport_unlock(lp);
		rport = lp->tt.rport_lookup(lp, ids.port_id);
		if (rport == NULL)
			rport = lp->tt.rport_create(lp, &ids);
		fc_lport_lock(lp);
		if (!rport)
			goto err;
		lp->dns_rp = rport;
	}

	rport = lp->dns_rp;
	rp = rport->dd_data;

	/*
	 * If dNS session isn't ready, start its logon.
	 */
	if (rp->rp_state != RPORT_ST_READY) {
		lp->tt.rport_login(rport);
	} else {
		del_timer(&lp->state_timer);
		fc_ns_enter_reg_pn(lp);
	}
	return;

	/*
	 * Resource allocation problem (malloc).  Try again in 500 mS.
	 */
err:
	fc_ns_retry(lp);
}

/*
 * Logoff DNS session.
 * We should get an event call when the session has been logged out.
 */
static void fc_ns_enter_dns_stop(struct fc_lport *lp)
{
	struct fc_rport *rport = lp->dns_rp;

	if (fc_ns_debug)
		FC_DBG("Processing DNS_STOP state\n");

	fc_lport_state_enter(lp, LPORT_ST_DNS_STOP);

	if (rport)
		lp->tt.rport_logout(rport);
	else
		lp->tt.lport_logout(lp);
}

/*
 * Fill in dNS request header.
 */
static void
fc_lport_fill_dns_hdr(struct fc_lport *lp, struct fc_ct_hdr *ct,
		      unsigned int op, unsigned int req_size)
{
	memset(ct, 0, sizeof(*ct) + req_size);
	ct->ct_rev = FC_CT_REV;
	ct->ct_fs_type = FC_FST_DIR;
	ct->ct_fs_subtype = FC_NS_SUBTYPE;
	ct->ct_cmd = htons(op);
}

/*
 * Register port name with name server.
 */
static void fc_ns_enter_reg_pn(struct fc_lport *lp)
{
	struct fc_frame *fp;
	struct req {
		struct fc_ct_hdr ct;
		struct fc_ns_rn_id rn;
	} *req;

	if (fc_ns_debug)
		FC_DBG("Processing REG_PN state\n");

	fc_lport_state_enter(lp, LPORT_ST_REG_PN);
	fp = fc_frame_alloc(lp, sizeof(*req));
	if (!fp) {
		fc_ns_retry(lp);
		return;
	}
	req = fc_frame_payload_get(fp, sizeof(*req));
	memset(req, 0, sizeof(*req));
	fc_lport_fill_dns_hdr(lp, &req->ct, FC_NS_RPN_ID, sizeof(req->rn));
	hton24(req->rn.fr_fid.fp_fid, lp->fid);
	put_unaligned_be64(lp->wwpn, &req->rn.fr_wwn);
	fc_frame_setup(fp, FC_RCTL_DD_UNSOL_CTL, FC_TYPE_CT);
	if (!lp->tt.exch_seq_send(lp, fp,
				  fc_ns_resp, lp,
				  lp->e_d_tov,
				  lp->fid,
				  lp->dns_rp->port_id,
				  FC_FC_SEQ_INIT | FC_FC_END_SEQ))
		fc_ns_retry(lp);
}

int fc_ns_init(struct fc_lport *lp)
{
	INIT_DELAYED_WORK(&lp->ns_disc_work, fc_ns_timeout);

	if (!lp->tt.disc_start)
		lp->tt.disc_start = fc_ns_disc_start;

	if (!lp->tt.disc_recv_req)
		lp->tt.disc_recv_req = fc_ns_recv_req;

	if (!lp->tt.dns_register)
		lp->tt.dns_register = fc_ns_enter_dns;

	if (!lp->tt.disc_stop)
		lp->tt.disc_stop = fc_ns_enter_dns_stop;

	return 0;
}
EXPORT_SYMBOL(fc_ns_init);

/**
 * fc_ns_gnn_id_req - Send Get Node Name by ID (GNN_ID) request
 * @lp: Fibre Channel host port instance
 * @dp: Temporary discovery port for holding IDs and world wide names
 *
 * The remote port is held by the caller for us.
 */
static int fc_ns_gnn_id_req(struct fc_lport *lp, struct fc_ns_port *dp)
{
	struct fc_frame *fp;
	struct req {
		struct fc_ct_hdr ct;
		struct fc_ns_fid fid;
	} *cp;
	int error = 0;

	fp = fc_frame_alloc(lp, sizeof(*cp));
	if (fp == NULL)
		return -ENOMEM;

	cp = fc_frame_payload_get(fp, sizeof(*cp));
	fc_ns_fill_dns_hdr(lp, &cp->ct, FC_NS_GNN_ID, sizeof(cp->fid));
	hton24(cp->fid.fp_fid, dp->ids.port_id);

	WARN_ON(!fc_lport_test_ready(lp));

	fc_frame_setup(fp, FC_RCTL_DD_UNSOL_CTL, FC_TYPE_CT);
	if (!lp->tt.exch_seq_send(lp, fp,
				  fc_ns_gnn_id_resp,
				  dp, lp->e_d_tov,
				  lp->fid,
				  lp->dns_rp->port_id,
				  FC_FC_SEQ_INIT | FC_FC_END_SEQ))
		error = -ENOMEM;

	return error;
}

/**
 * fc_ns_gnn_id_resp - Handle response to GNN_ID
 * @sp: Current sequence of GNN_ID exchange
 * @fp: response frame
 * @dp_arg: Temporary discovery port for holding IDs and world wide names
 */
static void fc_ns_gnn_id_resp(struct fc_seq *sp, struct fc_frame *fp,
			      void *dp_arg)
{
	struct fc_ns_port *dp = dp_arg;
	struct fc_lport *lp;
	struct resp {
		struct fc_ct_hdr ct;
		__be64 wwn;
	} *cp;
	unsigned int cmd;

	if (IS_ERR(fp)) {
		fc_ns_gnn_id_error(dp, fp);
		return;
	}

	lp = dp->lp;
	WARN_ON(!fc_frame_is_linear(fp));	/* buffer must be contiguous */

	cp = fc_frame_payload_get(fp, sizeof(cp->ct));
	if (cp == NULL) {
		FC_DBG("GNN_ID response too short, len %d\n", fr_len(fp));
		return;
	}
	cmd = ntohs(cp->ct.ct_cmd);
	switch (cmd) {
	case FC_FS_ACC:
		cp = fc_frame_payload_get(fp, sizeof(*cp));
		if (cp == NULL) {
			FC_DBG("GNN_ID response payload too short, len %d\n",
			       fr_len(fp));
			break;
		}
		dp->ids.node_name = ntohll(cp->wwn);
		fc_ns_new_target(lp, NULL, &dp->ids);
		break;
	case FC_FS_RJT:
		fc_ns_restart(lp);
		break;
	default:
		FC_DBG("GNN_ID unexpected CT response cmd %x\n", cmd);
		break;
	}
	kfree(dp);
	fc_frame_free(fp);
}

/**
 * fc_ns_gnn_id_error - Handle error from GNN_ID
 * @dp: Temporary discovery port for holding IDs and world wide names
 * @fp: response frame
 */
static void fc_ns_gnn_id_error(struct fc_ns_port *dp, struct fc_frame *fp)
{
	struct fc_lport *lp = dp->lp;

	switch (PTR_ERR(fp)) {
	case -FC_EX_TIMEOUT:
		fc_ns_restart(lp);
		break;
	case -FC_EX_CLOSED:
	default:
		break;
	}
	kfree(dp);
}

