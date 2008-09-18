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
 * Remote Port support.
 *
 * A remote port structure contains information about an N port to which we
 * will create sessions.
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/rcupdate.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <asm/unaligned.h>

#include <scsi/libfc/libfc.h>

static int fc_rp_debug;

/*
 * static functions.
 */
static void fc_rport_enter_start(struct fc_rport *);
static void fc_rport_enter_plogi(struct fc_rport *);
static void fc_rport_enter_prli(struct fc_rport *);
static void fc_rport_enter_rtv(struct fc_rport *);
static void fc_rport_enter_logo(struct fc_rport *);
static void fc_rport_recv_plogi_req(struct fc_rport *,
				    struct fc_seq *, struct fc_frame *);
static void fc_rport_recv_prli_req(struct fc_rport *,
				   struct fc_seq *, struct fc_frame *);
static void fc_rport_recv_prlo_req(struct fc_rport *,
				   struct fc_seq *, struct fc_frame *);
static void fc_rport_recv_logo_req(struct fc_rport *,
				   struct fc_seq *, struct fc_frame *);
static void fc_rport_timeout(struct work_struct *);

static struct fc_rport *fc_remote_port_create(struct fc_lport *,
					      struct fc_rport_identifiers *);

/**
 * fc_rport_lookup - lookup a remote port by port_id
 * @lp: Fibre Channel host port instance
 * @fid: remote port port_id to match
 */
struct fc_rport *fc_rport_lookup(const struct fc_lport *lp, u32 fid)
{
	struct Scsi_Host *shost = lp->host;
	struct fc_rport *rport, *found;
	unsigned long flags;

	found = NULL;
	spin_lock_irqsave(shost->host_lock, flags);
	list_for_each_entry(rport, &fc_host_rports(shost), peers)
		if (rport->port_id == fid &&
		    rport->port_state == FC_PORTSTATE_ONLINE) {
			found = rport;
			get_device(&found->dev);
			break;
		}
	spin_unlock_irqrestore(shost->host_lock, flags);
	return found;
}

/**
 * fc_remote_port_create - create a remote port
 * @lp: Fibre Channel host port instance
 * @ids: remote port identifiers (port_id, port_name, and node_name must be set)
 */
static struct fc_rport *fc_remote_port_create(struct fc_lport *lp,
					      struct fc_rport_identifiers *ids)
{
	struct fc_rport_libfc_priv *rp;
	struct fc_rport *rport;

	rport = fc_remote_port_add(lp->host, 0, ids);
	if (!rport)
		return NULL;

	rp = rport->dd_data;
	rp->local_port = lp;

	/* default value until service parameters are exchanged in PLOGI */
	rport->maxframe_size = FC_MIN_MAX_PAYLOAD;

	spin_lock_init(&rp->rp_lock);
	rp->rp_state = RPORT_ST_INIT;
	rp->local_port = lp;
	rp->e_d_tov = lp->e_d_tov;
	rp->r_a_tov = lp->r_a_tov;
	rp->flags = FC_RP_FLAGS_REC_SUPPORTED;
	INIT_DELAYED_WORK(&rp->retry_work, fc_rport_timeout);

	return rport;
}

static inline void fc_rport_lock(struct fc_rport *rport)
{
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	spin_lock_bh(&rp->rp_lock);
}

static inline void fc_rport_unlock(struct fc_rport *rport)
{
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	spin_unlock_bh(&rp->rp_lock);
}

/**
 * fc_plogi_get_maxframe - Get max payload from the common service parameters
 * @flp: FLOGI payload structure
 * @maxval: upper limit, may be less than what is in the service parameters
 */
static unsigned int
fc_plogi_get_maxframe(struct fc_els_flogi *flp, unsigned int maxval)
{
	unsigned int mfs;

	/*
	 * Get max payload from the common service parameters and the
	 * class 3 receive data field size.
	 */
	mfs = ntohs(flp->fl_csp.sp_bb_data) & FC_SP_BB_DATA_MASK;
	if (mfs >= FC_SP_MIN_MAX_PAYLOAD && mfs < maxval)
		maxval = mfs;
	mfs = ntohs(flp->fl_cssp[3 - 1].cp_rdfs);
	if (mfs >= FC_SP_MIN_MAX_PAYLOAD && mfs < maxval)
		maxval = mfs;
	return maxval;
}

/**
 * fc_lport_plogi_fill - Fill in PLOGI command for request
 * @lp: Fibre Channel host port instance
 * @plogi: PLOGI command structure to fill (same structure as FLOGI)
 * @op: either ELS_PLOGI for a localy generated request, or ELS_LS_ACC
 */
static void
fc_lport_plogi_fill(struct fc_lport *lp,
		    struct fc_els_flogi *plogi, unsigned int op)
{
	struct fc_els_csp *sp;
	struct fc_els_cssp *cp;

	memset(plogi, 0, sizeof(*plogi));
	plogi->fl_cmd = (u8) op;
	put_unaligned_be64(lp->wwpn, &plogi->fl_wwpn);
	put_unaligned_be64(lp->wwnn, &plogi->fl_wwnn);

	sp = &plogi->fl_csp;
	sp->sp_hi_ver = 0x20;
	sp->sp_lo_ver = 0x20;
	sp->sp_bb_cred = htons(10);	/* this gets set by gateway */
	sp->sp_bb_data = htons((u16) lp->mfs);
	cp = &plogi->fl_cssp[3 - 1];	/* class 3 parameters */
	cp->cp_class = htons(FC_CPC_VALID | FC_CPC_SEQ);
	if (op != ELS_FLOGI) {
		sp->sp_features = htons(FC_SP_FT_CIRO);
		sp->sp_tot_seq = htons(255);	/* seq. we accept */
		sp->sp_rel_off = htons(0x1f);
		sp->sp_e_d_tov = htonl(lp->e_d_tov);

		cp->cp_rdfs = htons((u16) lp->mfs);
		cp->cp_con_seq = htons(255);
		cp->cp_open_seq = 1;
	}
}

static void fc_rport_state_enter(struct fc_rport *rport,
				 enum fc_rport_state new)
{
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	if (rp->rp_state != new)
		rp->retries = 0;
	rp->rp_state = new;
}

/**
 * fc_rport_login - Start the remote port login state machine
 * @rport: Fibre Channel remote port
 */
int fc_rport_login(struct fc_rport *rport)
{
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	struct fc_lport *lp = rp->local_port;

	fc_rport_lock(rport);
	if (rp->rp_state == RPORT_ST_INIT) {
		fc_rport_unlock(rport);
		fc_rport_enter_start(rport);
	} else if (rp->rp_state == RPORT_ST_ERROR) {
		fc_rport_state_enter(rport, RPORT_ST_INIT);
		fc_rport_unlock(rport);
		if (fc_rp_debug)
			FC_DBG("remote %6x closed\n", rport->port_id);

		if (rport == lp->dns_rp &&
		    lp->state != LPORT_ST_RESET) {
			fc_lport_lock(lp);
			del_timer(&lp->state_timer);
			lp->dns_rp = NULL;

			if (lp->state == LPORT_ST_DNS_STOP) {
				fc_lport_unlock(lp);
				lp->tt.lport_logout(lp);
			} else {
				lp->tt.lport_login(lp);
				fc_lport_unlock(lp);
			}
			fc_remote_port_delete(rport);
		}
	} else
		fc_rport_unlock(rport);

	return 0;
}

/*
 * Stop the session - log it off.
 */
int fc_rport_logout(struct fc_rport *rport)
{
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	struct fc_lport *lp = rp->local_port;

	fc_rport_lock(rport);
	switch (rp->rp_state) {
	case RPORT_ST_PRLI:
	case RPORT_ST_RTV:
	case RPORT_ST_READY:
		fc_rport_enter_logo(rport);
		fc_rport_unlock(rport);
		break;
	default:
		fc_rport_state_enter(rport, RPORT_ST_INIT);
		fc_rport_unlock(rport);
		if (fc_rp_debug)
			FC_DBG("remote %6x closed\n", rport->port_id);
		if (rport == lp->dns_rp &&
		    lp->state != LPORT_ST_RESET) {
			fc_lport_lock(lp);
			del_timer(&lp->state_timer);
			lp->dns_rp = NULL;

			if (lp->state == LPORT_ST_DNS_STOP) {
				fc_lport_unlock(lp);
				lp->tt.lport_logout(lp);
			} else {
				lp->tt.lport_login(lp);
				fc_lport_unlock(lp);
			}

			fc_remote_port_delete(rport);
		}
		break;
	}

	return 0;
}

/*
 * Reset the session - assume it is logged off.	 Used after fabric logoff.
 * The local port code takes care of resetting the exchange manager.
 */
void fc_rport_reset(struct fc_rport *rport)
{
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	struct fc_lport *lp;

	if (fc_rp_debug)
		FC_DBG("sess to %6x reset\n", rport->port_id);
	fc_rport_lock(rport);

	lp = rp->local_port;
	fc_rport_state_enter(rport, RPORT_ST_INIT);
	fc_rport_unlock(rport);

	if (fc_rp_debug)
		FC_DBG("remote %6x closed\n", rport->port_id);
	if (rport == lp->dns_rp &&
	    lp->state != LPORT_ST_RESET) {
		fc_lport_lock(lp);
		del_timer(&lp->state_timer);
		lp->dns_rp = NULL;
		if (lp->state == LPORT_ST_DNS_STOP) {
			fc_lport_unlock(lp);
			lp->tt.lport_logout(lp);
		} else {
			lp->tt.lport_login(lp);
			fc_lport_unlock(lp);
		}
		fc_remote_port_delete(rport);
	}
}

/*
 * Reset all sessions for a local port session list.
 */
void fc_rport_reset_list(struct fc_lport *lp)
{
	struct Scsi_Host *shost = lp->host;
	struct fc_rport *rport;
	struct fc_rport *next;
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);
	list_for_each_entry_safe(rport, next, &fc_host_rports(shost), peers) {
		lp->tt.rport_reset(rport);
	}
	spin_unlock_irqrestore(shost->host_lock, flags);
}

static void fc_rport_enter_start(struct fc_rport *rport)
{
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	struct fc_lport *lp = rp->local_port;

	/*
	 * If the local port is already logged on, advance to next state.
	 * Otherwise the local port will be logged on by fc_rport_unlock().
	 */
	fc_rport_state_enter(rport, RPORT_ST_STARTED);

	if (rport == lp->dns_rp || fc_lport_test_ready(lp))
		fc_rport_enter_plogi(rport);
}

/*
 * Handle exchange reject or retry exhaustion in various states.
 */
static void fc_rport_reject(struct fc_rport *rport)
{
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	struct fc_lport *lp = rp->local_port;
	switch (rp->rp_state) {
	case RPORT_ST_PLOGI:
	case RPORT_ST_PRLI:
		fc_rport_state_enter(rport, RPORT_ST_ERROR);
		if (rport == lp->dns_rp &&
		    lp->state != LPORT_ST_RESET) {
			fc_lport_lock(lp);
			del_timer(&lp->state_timer);
			lp->dns_rp = NULL;
			if (lp->state == LPORT_ST_DNS_STOP) {
				fc_lport_unlock(lp);
				lp->tt.lport_logout(lp);
			} else {
				lp->tt.lport_login(lp);
				fc_lport_unlock(lp);
			}
			fc_remote_port_delete(rport);
		}
		break;
	case RPORT_ST_RTV:
		fc_rport_state_enter(rport, RPORT_ST_READY);
		if (fc_rp_debug)
			FC_DBG("remote %6x ready\n", rport->port_id);
		if (rport == lp->dns_rp &&
		    lp->state == LPORT_ST_DNS) {
			fc_lport_lock(lp);
			del_timer(&lp->state_timer);
			lp->tt.dns_register(lp);
			fc_lport_unlock(lp);
		}
		break;
	case RPORT_ST_LOGO:
		fc_rport_state_enter(rport, RPORT_ST_INIT);
		if (fc_rp_debug)
			FC_DBG("remote %6x closed\n", rport->port_id);
		if (rport == lp->dns_rp &&
		    lp->state != LPORT_ST_RESET) {
			fc_lport_lock(lp);
			del_timer(&lp->state_timer);
			lp->dns_rp = NULL;
			if (lp->state == LPORT_ST_DNS_STOP) {
				fc_lport_unlock(lp);
				lp->tt.lport_logout(lp);
			} else {
				lp->tt.lport_login(lp);
				fc_lport_unlock(lp);
			}
			fc_remote_port_delete(rport);
		}
		break;
	case RPORT_ST_NONE:
	case RPORT_ST_READY:
	case RPORT_ST_ERROR:
	case RPORT_ST_PLOGI_RECV:
	case RPORT_ST_STARTED:
	case RPORT_ST_INIT:
		BUG();
		break;
	}
	return;
}

/*
 * Timeout handler for retrying after allocation failures or exchange timeout.
 */
static void fc_rport_timeout(struct work_struct *work)
{
	struct fc_rport_libfc_priv *rp =
		container_of(work, struct fc_rport_libfc_priv, retry_work.work);
	struct fc_rport *rport = (((void *)rp) - sizeof(struct fc_rport));

	switch (rp->rp_state) {
	case RPORT_ST_PLOGI:
		fc_rport_enter_plogi(rport);
		break;
	case RPORT_ST_PRLI:
		fc_rport_enter_prli(rport);
		break;
	case RPORT_ST_RTV:
		fc_rport_enter_rtv(rport);
		break;
	case RPORT_ST_LOGO:
		fc_rport_enter_logo(rport);
		break;
	case RPORT_ST_READY:
	case RPORT_ST_ERROR:
	case RPORT_ST_INIT:
		break;
	case RPORT_ST_NONE:
	case RPORT_ST_PLOGI_RECV:
	case RPORT_ST_STARTED:
		BUG();
		break;
	}
	put_device(&rport->dev);
}

/*
 * Handle retry for allocation failure via timeout.
 */
static void fc_rport_retry(struct fc_rport *rport)
{
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	struct fc_lport *lp = rp->local_port;

	if (rp->retries < lp->max_retry_count) {
		rp->retries++;
		get_device(&rport->dev);
		schedule_delayed_work(&rp->retry_work,
				      msecs_to_jiffies(rp->e_d_tov));
	} else {
		FC_DBG("sess %6x alloc failure in state %d, "
		       "retries exhausted\n",
		       rport->port_id, rp->rp_state);
		fc_rport_reject(rport);
	}
}

/*
 * Handle error from a sequence issued by the rport state machine.
 */
static void fc_rport_error(struct fc_rport *rport, struct fc_frame *fp)
{
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	fc_rport_lock(rport);
	if (fc_rp_debug)
		FC_DBG("state %d error %ld retries %d\n",
		       rp->rp_state, PTR_ERR(fp), rp->retries);

	if (PTR_ERR(fp) == -FC_EX_TIMEOUT &&
	    rp->retries++ >= rp->local_port->max_retry_count) {
		get_device(&rport->dev);
		schedule_delayed_work(&rp->retry_work, 0);
	} else
		fc_rport_reject(rport);

	fc_rport_unlock(rport);
}

/**
 * fc_rport_plpogi_recv_resp - Handle incoming ELS PLOGI response
 * @sp: current sequence in the PLOGI exchange
 * @fp: response frame
 * @rp_arg: Fibre Channel remote port
 */
static void fc_rport_plogi_resp(struct fc_seq *sp, struct fc_frame *fp,
				void *rp_arg)
{
	struct fc_els_ls_rjt *rjp;
	struct fc_els_flogi *plp;
	u64 wwpn, wwnn;
	unsigned int tov;
	u16 csp_seq;
	u16 cssp_seq;
	u8 op;
	struct fc_rport *rport = rp_arg;
	struct fc_rport_libfc_priv *rp = rport->dd_data;

	if (!IS_ERR(fp)) {
		op = fc_frame_payload_op(fp);
		fc_rport_lock(rport);
		if (op == ELS_LS_ACC &&
		    (plp = fc_frame_payload_get(fp, sizeof(*plp))) != NULL) {
			wwpn = get_unaligned_be64(&plp->fl_wwpn);
			wwnn = get_unaligned_be64(&plp->fl_wwnn);

			fc_rport_set_name(rport, wwpn, wwnn);
			tov = ntohl(plp->fl_csp.sp_e_d_tov);
			if (ntohs(plp->fl_csp.sp_features) & FC_SP_FT_EDTR)
				tov /= 1000;
			if (tov > rp->e_d_tov)
				rp->e_d_tov = tov;
			csp_seq = ntohs(plp->fl_csp.sp_tot_seq);
			cssp_seq = ntohs(plp->fl_cssp[3 - 1].cp_con_seq);
			if (cssp_seq < csp_seq)
				csp_seq = cssp_seq;
			rp->max_seq = csp_seq;
			rport->maxframe_size =
				fc_plogi_get_maxframe(plp, rp->local_port->mfs);
			if (rp->rp_state == RPORT_ST_PLOGI)
				fc_rport_enter_prli(rport);
		} else {
			if (fc_rp_debug)
				FC_DBG("bad PLOGI response\n");

			rjp = fc_frame_payload_get(fp, sizeof(*rjp));
			if (op == ELS_LS_RJT && rjp != NULL &&
			    rjp->er_reason == ELS_RJT_INPROG)
				fc_rport_retry(rport);    /* try again */
			else
				fc_rport_reject(rport);   /* error */
		}
		fc_rport_unlock(rport);
		fc_frame_free(fp);
	} else {
		fc_rport_error(rport, fp);
	}
}

/**
 * fc_rport_enter_plogi - Send Port Login (PLOGI) request to peer
 * @rport: Fibre Channel remote port to send PLOGI to
 */
static void fc_rport_enter_plogi(struct fc_rport *rport)
{
	struct fc_frame *fp;
	struct fc_els_flogi *plogi;
	struct fc_lport *lp;
	struct fc_rport_libfc_priv *rp = rport->dd_data;

	lp = rp->local_port;
	fc_rport_state_enter(rport, RPORT_ST_PLOGI);
	rport->maxframe_size = FC_MIN_MAX_PAYLOAD;
	fp = fc_frame_alloc(lp, sizeof(*plogi));
	if (!fp)
		return fc_rport_retry(rport);
	plogi = fc_frame_payload_get(fp, sizeof(*plogi));
	WARN_ON(!plogi);
	fc_lport_plogi_fill(rp->local_port, plogi, ELS_PLOGI);
	rp->e_d_tov = lp->e_d_tov;
	fc_frame_setup(fp, FC_RCTL_ELS_REQ, FC_TYPE_ELS);
	if (!lp->tt.exch_seq_send(lp, fp,
				  fc_rport_plogi_resp,
				  rport, lp->e_d_tov,
				  rp->local_port->fid,
				  rport->port_id,
				  FC_FC_SEQ_INIT | FC_FC_END_SEQ))
		fc_rport_retry(rport);
}

/**
 * fc_rport_prli_resp - Process Login (PRLI) response handler
 * @sp: current sequence in the PRLI exchange
 * @fp: response frame
 * @rp_arg: Fibre Channel remote port
 */
static void fc_rport_prli_resp(struct fc_seq *sp, struct fc_frame *fp,
			       void *rp_arg)
{
	struct fc_rport *rport = rp_arg;
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	struct fc_lport *lp = rp->local_port;
	struct {
		struct fc_els_prli prli;
		struct fc_els_spp spp;
	} *pp;
	u32 roles = FC_RPORT_ROLE_UNKNOWN;
	u32 fcp_parm = 0;
	u8 op;

	if (IS_ERR(fp)) {
		fc_rport_error(rport, fp);
		return;
	}

	fc_rport_lock(rport);
	op = fc_frame_payload_op(fp);
	if (op == ELS_LS_ACC) {
		pp = fc_frame_payload_get(fp, sizeof(*pp));
		if (pp && pp->prli.prli_spp_len >= sizeof(pp->spp)) {
			fcp_parm = ntohl(pp->spp.spp_params);
			if (fcp_parm & FCP_SPPF_RETRY)
				rp->flags |= FC_RP_FLAGS_RETRY;
		}

		rport->supported_classes = FC_COS_CLASS3;
		if (fcp_parm & FCP_SPPF_INIT_FCN)
			roles |= FC_RPORT_ROLE_FCP_INITIATOR;
		if (fcp_parm & FCP_SPPF_TARG_FCN)
			roles |= FC_RPORT_ROLE_FCP_TARGET;

		fc_rport_enter_rtv(rport);
		fc_rport_unlock(rport);
		fc_remote_port_rolechg(rport, roles);
	} else {
		FC_DBG("bad ELS response\n");
		fc_rport_state_enter(rport, RPORT_ST_ERROR);
		fc_rport_unlock(rport);
		if (rport == lp->dns_rp && lp->state != LPORT_ST_RESET) {
			fc_lport_lock(lp);
			del_timer(&lp->state_timer);
			lp->dns_rp = NULL;
			if (lp->state == LPORT_ST_DNS_STOP) {
				fc_lport_unlock(lp);
				lp->tt.lport_logout(lp);
			} else {
				lp->tt.lport_login(lp);
				fc_lport_unlock(lp);
			}
			fc_remote_port_delete(rport);
		}
	}

	fc_frame_free(fp);
}

/**
 * fc_rport_logo_resp - Logout (LOGO) response handler
 * @sp: current sequence in the LOGO exchange
 * @fp: response frame
 * @rp_arg: Fibre Channel remote port
 */
static void fc_rport_logo_resp(struct fc_seq *sp, struct fc_frame *fp,
			       void *rp_arg)
{
	struct fc_rport *rport = rp_arg;
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	struct fc_lport *lp = rp->local_port;
	u8 op;

	if (IS_ERR(fp)) {
		fc_rport_error(rport, fp);
		return;
	}

	fc_rport_lock(rport);
	op = fc_frame_payload_op(fp);
	if (op == ELS_LS_ACC) {
		fc_rport_enter_rtv(rport);
		fc_rport_unlock(rport);
	} else {
		FC_DBG("bad ELS response\n");
		fc_rport_state_enter(rport, RPORT_ST_ERROR);
		fc_rport_unlock(rport);
		if (rport == lp->dns_rp && lp->state != LPORT_ST_RESET) {
			fc_lport_lock(lp);
			del_timer(&lp->state_timer);
			lp->dns_rp = NULL;
			if (lp->state == LPORT_ST_DNS_STOP) {
				fc_lport_unlock(lp);
				lp->tt.lport_logout(lp);
			} else {
				lp->tt.lport_login(lp);
				fc_lport_unlock(lp);
			}
			fc_remote_port_delete(rport);
		}
	}

	fc_frame_free(fp);
}

/**
 * fc_rport_enter_prli - Send Process Login (PRLI) request to peer
 * @rport: Fibre Channel remote port to send PRLI to
 */
static void fc_rport_enter_prli(struct fc_rport *rport)
{
	struct {
		struct fc_els_prli prli;
		struct fc_els_spp spp;
	} *pp;
	struct fc_frame *fp;
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	struct fc_lport *lp = rp->local_port;

	fc_rport_state_enter(rport, RPORT_ST_PRLI);

	/*
	 * Special case if session is for name server or any other
	 * well-known address:	Skip the PRLI step.
	 * This should be made more general, possibly moved to the FCP layer.
	 */
	if (rport->port_id >= FC_FID_DOM_MGR) {
		fc_rport_state_enter(rport, RPORT_ST_READY);
		if (fc_rp_debug)
			FC_DBG("remote %6x ready\n", rport->port_id);
		if (rport == lp->dns_rp &&
		    lp->state == LPORT_ST_DNS) {
			fc_lport_lock(lp);
			del_timer(&lp->state_timer);
			lp->tt.dns_register(lp);
			fc_lport_unlock(lp);
		}
		return;
	}
	fp = fc_frame_alloc(lp, sizeof(*pp));
	if (!fp)
		return fc_rport_retry(rport);
	pp = fc_frame_payload_get(fp, sizeof(*pp));
	WARN_ON(!pp);
	memset(pp, 0, sizeof(*pp));
	pp->prli.prli_cmd = ELS_PRLI;
	pp->prli.prli_spp_len = sizeof(struct fc_els_spp);
	pp->prli.prli_len = htons(sizeof(*pp));
	pp->spp.spp_type = FC_TYPE_FCP;
	pp->spp.spp_flags = FC_SPP_EST_IMG_PAIR;
	pp->spp.spp_params = htonl(rp->local_port->service_params);
	fc_frame_setup(fp, FC_RCTL_ELS_REQ, FC_TYPE_ELS);
	if (!lp->tt.exch_seq_send(lp, fp,
				  fc_rport_prli_resp,
				  rport, lp->e_d_tov,
				  rp->local_port->fid,
				  rport->port_id,
				  FC_FC_SEQ_INIT | FC_FC_END_SEQ))
		fc_rport_retry(rport);
}

/**
 * fc_rport_els_rtv_resp - Request Timeout Value response handler
 * @sp: current sequence in the RTV exchange
 * @fp: response frame
 * @rp_arg: Fibre Channel remote port
 *
 * Many targets don't seem to support this.
 */
static void fc_rport_rtv_resp(struct fc_seq *sp, struct fc_frame *fp,
			      void *rp_arg)
{
	struct fc_rport *rport = rp_arg;
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	struct fc_lport *lp = rp->local_port;
	u8 op;

	if (IS_ERR(fp)) {
		fc_rport_error(rport, fp);
		return;
	}

	fc_rport_lock(rport);
	op = fc_frame_payload_op(fp);
	if (op == ELS_LS_ACC) {
		struct fc_els_rtv_acc *rtv;
		u32 toq;
		u32 tov;

		rtv = fc_frame_payload_get(fp, sizeof(*rtv));
		if (rtv) {
			toq = ntohl(rtv->rtv_toq);
			tov = ntohl(rtv->rtv_r_a_tov);
			if (tov == 0)
				tov = 1;
			rp->r_a_tov = tov;
			tov = ntohl(rtv->rtv_e_d_tov);
			if (toq & FC_ELS_RTV_EDRES)
				tov /= 1000000;
			if (tov == 0)
				tov = 1;
			rp->e_d_tov = tov;
		}
	}
	fc_rport_state_enter(rport, RPORT_ST_READY);
	fc_rport_unlock(rport);
	if (fc_rp_debug)
		FC_DBG("remote %6x ready\n", rport->port_id);
	if (rport == lp->dns_rp &&
	    lp->state == LPORT_ST_DNS) {
		fc_lport_lock(lp);
		del_timer(&lp->state_timer);
		lp->tt.dns_register(lp);
		fc_lport_unlock(lp);
	}
	fc_frame_free(fp);
}

/**
 * fc_rport_enter_rtv - Send Request Timeout Value (RTV) request to peer
 * @rport: Fibre Channel remote port to send RTV to
 */
static void fc_rport_enter_rtv(struct fc_rport *rport)
{
	struct fc_els_rtv *rtv;
	struct fc_frame *fp;
	struct fc_lport *lp;
	struct fc_rport_libfc_priv *rp = rport->dd_data;

	lp = rp->local_port;
	fc_rport_state_enter(rport, RPORT_ST_RTV);

	fp = fc_frame_alloc(lp, sizeof(*rtv));
	if (!fp)
		return fc_rport_retry(rport);
	rtv = fc_frame_payload_get(fp, sizeof(*rtv));
	WARN_ON(!rtv);
	memset(rtv, 0, sizeof(*rtv));
	rtv->rtv_cmd = ELS_RTV;
	fc_frame_setup(fp, FC_RCTL_ELS_REQ, FC_TYPE_ELS);
	if (!lp->tt.exch_seq_send(lp, fp,
				  fc_rport_rtv_resp,
				  rport, lp->e_d_tov,
				  rp->local_port->fid,
				  rport->port_id,
				  FC_FC_SEQ_INIT | FC_FC_END_SEQ))
		fc_rport_retry(rport);
}

/**
 * fc_rport_enter_logo - Send Logout (LOGO) request to peer
 * @rport: Fibre Channel remote port to send LOGO to
 */
static void fc_rport_enter_logo(struct fc_rport *rport)
{
	struct fc_frame *fp;
	struct fc_els_logo *logo;
	struct fc_lport *lp;
	struct fc_rport_libfc_priv *rp = rport->dd_data;

	fc_rport_state_enter(rport, RPORT_ST_LOGO);

	lp = rp->local_port;
	fp = fc_frame_alloc(lp, sizeof(*logo));
	if (!fp)
		return fc_rport_retry(rport);
	logo = fc_frame_payload_get(fp, sizeof(*logo));
	memset(logo, 0, sizeof(*logo));
	logo->fl_cmd = ELS_LOGO;
	hton24(logo->fl_n_port_id, lp->fid);
	logo->fl_n_port_wwn = htonll(lp->wwpn);

	fc_frame_setup(fp, FC_RCTL_ELS_REQ, FC_TYPE_ELS);
	if (!lp->tt.exch_seq_send(lp, fp,
				  fc_rport_logo_resp,
				  rport, lp->e_d_tov,
				  rp->local_port->fid,
				  rport->port_id,
				  FC_FC_SEQ_INIT | FC_FC_END_SEQ))
		fc_rport_retry(rport);
}

/*
 * Handle a request received by the exchange manager for the session.
 * This may be an entirely new session, or a PLOGI or LOGO for an existing one.
 * This will free the frame.
 */
void fc_rport_recv_req(struct fc_seq *sp, struct fc_frame *fp,
		       struct fc_rport *rport)
{
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	struct fc_frame_header *fh;
	struct fc_lport *lp = rp->local_port;
	struct fc_seq_els_data els_data;
	u8 op;

	els_data.fp = NULL;
	els_data.explan = ELS_EXPL_NONE;
	els_data.reason = ELS_RJT_NONE;

	fh = fc_frame_header_get(fp);

	if (fh->fh_r_ctl == FC_RCTL_ELS_REQ && fh->fh_type == FC_TYPE_ELS) {
		op = fc_frame_payload_op(fp);
		switch (op) {
		case ELS_PLOGI:
			fc_rport_recv_plogi_req(rport, sp, fp);
			break;
		case ELS_PRLI:
			fc_rport_recv_prli_req(rport, sp, fp);
			break;
		case ELS_PRLO:
			fc_rport_recv_prlo_req(rport, sp, fp);
			break;
		case ELS_LOGO:
			fc_rport_recv_logo_req(rport, sp, fp);
			break;
		case ELS_RRQ:
			els_data.fp = fp;
			lp->tt.seq_els_rsp_send(sp, ELS_RRQ, &els_data);
			break;
		case ELS_REC:
			els_data.fp = fp;
			lp->tt.seq_els_rsp_send(sp, ELS_REC, &els_data);
			break;
		default:
			els_data.reason = ELS_RJT_UNSUP;
			lp->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &els_data);
			fc_frame_free(fp);
			break;
		}
	} else {
		fc_frame_free(fp);
	}
}

/**
 * fc_rport_recv_plogi_req - Handle incoming Port Login (PLOGI) request
 * @rport: Fibre Channel remote port that initiated PLOGI
 * @sp: current sequence in the PLOGI exchange
 * @fp: PLOGI request frame
 */
static void fc_rport_recv_plogi_req(struct fc_rport *rport,
				    struct fc_seq *sp, struct fc_frame *rx_fp)
{
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	struct fc_frame *fp = rx_fp;
	struct fc_frame_header *fh;
	struct fc_lport *lp;
	struct fc_els_flogi *pl;
	struct fc_seq_els_data rjt_data;
	u32 sid;
	u64 wwpn;
	u64 wwnn;
	enum fc_els_rjt_reason reject = 0;
	u32 f_ctl;

	rjt_data.fp = NULL;
	fh = fc_frame_header_get(fp);
	sid = ntoh24(fh->fh_s_id);
	pl = fc_frame_payload_get(fp, sizeof(*pl));
	if (!pl) {
		FC_DBG("incoming PLOGI from %x too short\n", sid);
		WARN_ON(1);
		/* XXX TBD: send reject? */
		fc_frame_free(fp);
		return;
	}
	wwpn = get_unaligned_be64(&pl->fl_wwpn);
	wwnn = get_unaligned_be64(&pl->fl_wwnn);
	fc_rport_lock(rport);
	lp = rp->local_port;

	/*
	 * If the session was just created, possibly due to the incoming PLOGI,
	 * set the state appropriately and accept the PLOGI.
	 *
	 * If we had also sent a PLOGI, and if the received PLOGI is from a
	 * higher WWPN, we accept it, otherwise an LS_RJT is sent with reason
	 * "command already in progress".
	 *
	 * XXX TBD: If the session was ready before, the PLOGI should result in
	 * all outstanding exchanges being reset.
	 */
	switch (rp->rp_state) {
	case RPORT_ST_INIT:
		if (fc_rp_debug)
			FC_DBG("incoming PLOGI from %6x wwpn %llx state INIT "
			       "- reject\n", sid, wwpn);
		reject = ELS_RJT_UNSUP;
		break;
	case RPORT_ST_STARTED:
		/*
		 * we'll only accept a login if the port name
		 * matches or was unknown.
		 */
		if (rport->port_name != -1 &&
		    rport->port_name != wwpn) {
			FC_DBG("incoming PLOGI from name %llx expected %llx\n",
			       wwpn, rport->port_name);
			reject = ELS_RJT_UNAB;
		}
		break;
	case RPORT_ST_PLOGI:
		if (fc_rp_debug)
			FC_DBG("incoming PLOGI from %x in PLOGI state %d\n",
			       sid, rp->rp_state);
		if (wwpn < lp->wwpn)
			reject = ELS_RJT_INPROG;
		break;
	case RPORT_ST_PRLI:
	case RPORT_ST_ERROR:
	case RPORT_ST_READY:
		if (fc_rp_debug)
			FC_DBG("incoming PLOGI from %x in logged-in state %d "
			       "- ignored for now\n", sid, rp->rp_state);
		/* XXX TBD - should reset */
		break;
	case RPORT_ST_NONE:
	default:
		if (fc_rp_debug)
			FC_DBG("incoming PLOGI from %x in unexpected "
			       "state %d\n", sid, rp->rp_state);
		break;
	}

	if (reject) {
		rjt_data.reason = reject;
		rjt_data.explan = ELS_EXPL_NONE;
		lp->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &rjt_data);
		fc_frame_free(fp);
	} else {
		fp = fc_frame_alloc(lp, sizeof(*pl));
		if (fp == NULL) {
			fp = rx_fp;
			rjt_data.reason = ELS_RJT_UNAB;
			rjt_data.explan = ELS_EXPL_NONE;
			lp->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &rjt_data);
			fc_frame_free(fp);
		} else {
			sp = lp->tt.seq_start_next(sp);
			WARN_ON(!sp);
			fc_rport_set_name(rport, wwpn, wwnn);

			/*
			 * Get session payload size from incoming PLOGI.
			 */
			rport->maxframe_size =
				fc_plogi_get_maxframe(pl, lp->mfs);
			fc_frame_free(rx_fp);
			pl = fc_frame_payload_get(fp, sizeof(*pl));
			WARN_ON(!pl);
			fc_lport_plogi_fill(lp, pl, ELS_LS_ACC);

			/*
			 * Send LS_ACC.	 If this fails,
			 * the originator should retry.
			 */
			f_ctl = FC_FC_SEQ_INIT | FC_FC_LAST_SEQ | FC_FC_END_SEQ;
			fc_frame_setup(fp, FC_RCTL_ELS_REP, FC_TYPE_ELS);
			lp->tt.seq_send(lp, sp, fp, f_ctl);
			if (rp->rp_state == RPORT_ST_PLOGI)
				fc_rport_enter_prli(rport);
			else
				fc_rport_state_enter(rport,
						     RPORT_ST_PLOGI_RECV);
		}
	}
	fc_rport_unlock(rport);
}

/**
 * fc_rport_recv_prli_req - Handle incoming Process Login (PRLI) request
 * @rport: Fibre Channel remote port that initiated PRLI
 * @sp: current sequence in the PRLI exchange
 * @fp: PRLI request frame
 */
static void fc_rport_recv_prli_req(struct fc_rport *rport,
				   struct fc_seq *sp, struct fc_frame *rx_fp)
{
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	struct fc_frame *fp;
	struct fc_frame_header *fh;
	struct fc_lport *lp;
	struct {
		struct fc_els_prli prli;
		struct fc_els_spp spp;
	} *pp;
	struct fc_els_spp *rspp;	/* request service param page */
	struct fc_els_spp *spp;	/* response spp */
	unsigned int len;
	unsigned int plen;
	enum fc_els_rjt_reason reason = ELS_RJT_UNAB;
	enum fc_els_rjt_explan explan = ELS_EXPL_NONE;
	enum fc_els_spp_resp resp;
	struct fc_seq_els_data rjt_data;
	u32 f_ctl;
	u32 fcp_parm;
	u32 roles = FC_RPORT_ROLE_UNKNOWN;

	rjt_data.fp = NULL;
	fh = fc_frame_header_get(rx_fp);
	lp = rp->local_port;
	switch (rp->rp_state) {
	case RPORT_ST_PLOGI_RECV:
	case RPORT_ST_PRLI:
	case RPORT_ST_READY:
		reason = ELS_RJT_NONE;
		break;
	default:
		break;
	}
	len = fr_len(rx_fp) - sizeof(*fh);
	pp = fc_frame_payload_get(rx_fp, sizeof(*pp));
	if (pp == NULL) {
		reason = ELS_RJT_PROT;
		explan = ELS_EXPL_INV_LEN;
	} else {
		plen = ntohs(pp->prli.prli_len);
		if ((plen % 4) != 0 || plen > len) {
			reason = ELS_RJT_PROT;
			explan = ELS_EXPL_INV_LEN;
		} else if (plen < len) {
			len = plen;
		}
		plen = pp->prli.prli_spp_len;
		if ((plen % 4) != 0 || plen < sizeof(*spp) ||
		    plen > len || len < sizeof(*pp)) {
			reason = ELS_RJT_PROT;
			explan = ELS_EXPL_INV_LEN;
		}
		rspp = &pp->spp;
	}
	if (reason != ELS_RJT_NONE ||
	    (fp = fc_frame_alloc(lp, len)) == NULL) {
		rjt_data.reason = reason;
		rjt_data.explan = explan;
		lp->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &rjt_data);
	} else {
		sp = lp->tt.seq_start_next(sp);
		WARN_ON(!sp);
		pp = fc_frame_payload_get(fp, len);
		WARN_ON(!pp);
		memset(pp, 0, len);
		pp->prli.prli_cmd = ELS_LS_ACC;
		pp->prli.prli_spp_len = plen;
		pp->prli.prli_len = htons(len);
		len -= sizeof(struct fc_els_prli);

		/*
		 * Go through all the service parameter pages and build
		 * response.  If plen indicates longer SPP than standard,
		 * use that.  The entire response has been pre-cleared above.
		 */
		spp = &pp->spp;
		while (len >= plen) {
			spp->spp_type = rspp->spp_type;
			spp->spp_type_ext = rspp->spp_type_ext;
			spp->spp_flags = rspp->spp_flags & FC_SPP_EST_IMG_PAIR;
			resp = FC_SPP_RESP_ACK;
			if (rspp->spp_flags & FC_SPP_RPA_VAL)
				resp = FC_SPP_RESP_NO_PA;
			switch (rspp->spp_type) {
			case 0:	/* common to all FC-4 types */
				break;
			case FC_TYPE_FCP:
				fcp_parm = ntohl(rspp->spp_params);
				if (fcp_parm * FCP_SPPF_RETRY)
					rp->flags |= FC_RP_FLAGS_RETRY;
				rport->supported_classes = FC_COS_CLASS3;
				if (fcp_parm & FCP_SPPF_INIT_FCN)
					roles |= FC_RPORT_ROLE_FCP_INITIATOR;
				if (fcp_parm & FCP_SPPF_TARG_FCN)
					roles |= FC_RPORT_ROLE_FCP_TARGET;
				fc_remote_port_rolechg(rport, roles);
				spp->spp_params =
					htonl(rp->local_port->service_params);
				break;
			default:
				resp = FC_SPP_RESP_INVL;
				break;
			}
			spp->spp_flags |= resp;
			len -= plen;
			rspp = (struct fc_els_spp *)((char *)rspp + plen);
			spp = (struct fc_els_spp *)((char *)spp + plen);
		}

		/*
		 * Send LS_ACC.	 If this fails, the originator should retry.
		 */
		f_ctl = FC_FC_SEQ_INIT | FC_FC_LAST_SEQ | FC_FC_END_SEQ;
		fc_frame_setup(fp, FC_RCTL_ELS_REP, FC_TYPE_ELS);
		lp->tt.seq_send(lp, sp, fp, f_ctl);

		/*
		 * Get lock and re-check state.
		 */
		fc_rport_lock(rport);
		switch (rp->rp_state) {
		case RPORT_ST_PLOGI_RECV:
		case RPORT_ST_PRLI:
			fc_rport_state_enter(rport, RPORT_ST_READY);
			if (fc_rp_debug)
				FC_DBG("remote %6x ready\n", rport->port_id);
			if (rport == lp->dns_rp &&
			    lp->state == LPORT_ST_DNS) {
				fc_lport_lock(lp);
				del_timer(&lp->state_timer);
				lp->tt.dns_register(lp);
				fc_lport_unlock(lp);
			}
			break;
		case RPORT_ST_READY:
			break;
		default:
			break;
		}
		fc_rport_unlock(rport);
	}
	fc_frame_free(rx_fp);
}

/**
 * fc_rport_recv_prlo_req - Handle incoming Process Logout (PRLO) request
 * @rport: Fibre Channel remote port that initiated PRLO
 * @sp: current sequence in the PRLO exchange
 * @fp: PRLO request frame
 */
static void fc_rport_recv_prlo_req(struct fc_rport *rport, struct fc_seq *sp,
				   struct fc_frame *fp)
{
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	struct fc_frame_header *fh;
	struct fc_lport *lp = rp->local_port;
	struct fc_seq_els_data rjt_data;

	fh = fc_frame_header_get(fp);
	FC_DBG("incoming PRLO from %x state %d\n",
	       ntoh24(fh->fh_s_id), rp->rp_state);
	rjt_data.fp = NULL;
	rjt_data.reason = ELS_RJT_UNAB;
	rjt_data.explan = ELS_EXPL_NONE;
	lp->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &rjt_data);
	fc_frame_free(fp);
}

/**
 * fc_rport_recv_logo_req - Handle incoming Logout (LOGO) request
 * @rport: Fibre Channel remote port that initiated LOGO
 * @sp: current sequence in the LOGO exchange
 * @fp: LOGO request frame
 */
static void fc_rport_recv_logo_req(struct fc_rport *rport, struct fc_seq *sp,
				   struct fc_frame *fp)
{
	struct fc_frame_header *fh;
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	struct fc_lport *lp = rp->local_port;

	fh = fc_frame_header_get(fp);
	fc_rport_lock(rport);
	fc_rport_state_enter(rport, RPORT_ST_INIT);
	fc_rport_unlock(rport);
	if (fc_rp_debug)
		FC_DBG("remote %6x closed\n", rport->port_id);
	if (rport == lp->dns_rp &&
	    lp->state != LPORT_ST_RESET) {
		fc_lport_lock(lp);
		del_timer(&lp->state_timer);
		lp->dns_rp = NULL;
		if (lp->state == LPORT_ST_DNS_STOP) {
			fc_lport_unlock(lp);
			lp->tt.lport_logout(lp);
		} else {
			lp->tt.lport_login(lp);
			fc_lport_unlock(lp);
		}
		fc_remote_port_delete(rport);
	}
	lp->tt.seq_els_rsp_send(sp, ELS_LS_ACC, NULL);
	fc_frame_free(fp);
}

int fc_rport_init(struct fc_lport *lp)
{
	if (!lp->tt.rport_login)
		lp->tt.rport_login = fc_rport_login;

	if (!lp->tt.rport_logout)
		lp->tt.rport_logout = fc_rport_logout;

	if (!lp->tt.rport_recv_req)
		lp->tt.rport_recv_req = fc_rport_recv_req;

	if (!lp->tt.rport_create)
		lp->tt.rport_create = fc_remote_port_create;

	if (!lp->tt.rport_lookup)
		lp->tt.rport_lookup = fc_rport_lookup;

	if (!lp->tt.rport_reset)
		lp->tt.rport_reset = fc_rport_reset;

	if (!lp->tt.rport_reset_list)
		lp->tt.rport_reset_list = fc_rport_reset_list;

	return 0;
}
EXPORT_SYMBOL(fc_rport_init);

