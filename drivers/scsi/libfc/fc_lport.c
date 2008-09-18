/*
 * Copyright(c) 2007 Intel Corporation. All rights reserved.
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
 * Logical interface support.
 */

#include <linux/timer.h>
#include <asm/unaligned.h>

#include <scsi/fc/fc_gs.h>

#include <scsi/libfc/libfc.h>

/* Fabric IDs to use for point-to-point mode, chosen on whims. */
#define FC_LOCAL_PTP_FID_LO   0x010101
#define FC_LOCAL_PTP_FID_HI   0x010102

#define	DNS_DELAY	      3 /* Discovery delay after RSCN (in seconds)*/

static int fc_lport_debug;

static void fc_lport_enter_flogi(struct fc_lport *);
static void fc_lport_enter_logo(struct fc_lport *);

static const char *fc_lport_state_names[] = {
	[LPORT_ST_NONE] =     "none",
	[LPORT_ST_FLOGI] =    "FLOGI",
	[LPORT_ST_DNS] =      "dNS",
	[LPORT_ST_REG_PN] =   "REG_PN",
	[LPORT_ST_REG_FT] =   "REG_FT",
	[LPORT_ST_SCR] =      "SCR",
	[LPORT_ST_READY] =    "ready",
	[LPORT_ST_DNS_STOP] = "stop",
	[LPORT_ST_LOGO] =     "LOGO",
	[LPORT_ST_RESET] =    "reset",
};

static int fc_frame_drop(struct fc_lport *lp, struct fc_frame *fp)
{
	fc_frame_free(fp);
	return 0;
}

static const char *fc_lport_state(struct fc_lport *lp)
{
	const char *cp;

	cp = fc_lport_state_names[lp->state];
	if (!cp)
		cp = "unknown";
	return cp;
}

static void fc_lport_ptp_setup(struct fc_lport *lp,
			       u32 remote_fid, u64 remote_wwpn,
			       u64 remote_wwnn)
{
	struct fc_rport *rport;
	struct fc_rport_identifiers ids = {
		.port_id = remote_fid,
		.port_name = remote_wwpn,
		.node_name = remote_wwnn,
	};

	/*
	 * if we have to create a rport the fc class can sleep so we must
	 * drop the lock here
	 */
	fc_lport_unlock(lp);
	rport = lp->tt.rport_lookup(lp, ids.port_id); /* lookup and hold */
	if (rport == NULL)
		rport = lp->tt.rport_create(lp, &ids); /* create and hold */
	fc_lport_lock(lp);
	if (rport) {
		if (lp->ptp_rp)
			fc_remote_port_delete(lp->ptp_rp);
		lp->ptp_rp = rport;
		fc_lport_state_enter(lp, LPORT_ST_READY);
	}
}

static void fc_lport_ptp_clear(struct fc_lport *lp)
{
	if (lp->ptp_rp) {
		fc_remote_port_delete(lp->ptp_rp);
		lp->ptp_rp = NULL;
	}
}

/*
 * Routines to support struct fc_function_template
 */
void fc_get_host_port_state(struct Scsi_Host *shost)
{
	struct fc_lport *lp = shost_priv(shost);

	if ((lp->link_status & FC_LINK_UP) == FC_LINK_UP)
		fc_host_port_state(shost) = FC_PORTSTATE_ONLINE;
	else
		fc_host_port_state(shost) = FC_PORTSTATE_OFFLINE;
}
EXPORT_SYMBOL(fc_get_host_port_state);

/*
 * Fill in FLOGI command for request.
 */
static void
fc_lport_flogi_fill(struct fc_lport *lp,
		    struct fc_els_flogi *flogi, unsigned int op)
{
	struct fc_els_csp *sp;
	struct fc_els_cssp *cp;

	memset(flogi, 0, sizeof(*flogi));
	flogi->fl_cmd = (u8) op;
	put_unaligned_be64(lp->wwpn, &flogi->fl_wwpn);
	put_unaligned_be64(lp->wwnn, &flogi->fl_wwnn);
	sp = &flogi->fl_csp;
	sp->sp_hi_ver = 0x20;
	sp->sp_lo_ver = 0x20;
	sp->sp_bb_cred = htons(10);	/* this gets set by gateway */
	sp->sp_bb_data = htons((u16) lp->mfs);
	cp = &flogi->fl_cssp[3 - 1];	/* class 3 parameters */
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

/*
 * Set the fid. This indicates that we have a new connection to the
 * fabric so we should reset our list of fc_rports. Passing a fid of
 * 0 will also reset the rport list regardless of the previous fid.
 */
static void fc_lport_set_fid(struct fc_lport *lp, u32 fid)
{
	if (fid != 0 && lp->fid == fid)
		return;

	if (fc_lport_debug)
		FC_DBG("changing local port fid from %x to %x\n",
		       lp->fid, fid);
	lp->fid = fid;
	lp->tt.rport_reset_list(lp);
}

/*
 * Add a supported FC-4 type.
 */
static void fc_lport_add_fc4_type(struct fc_lport *lp, enum fc_fh_type type)
{
	__be32 *mp;

	mp = &lp->fcts.ff_type_map[type / FC_NS_BPW];
	*mp = htonl(ntohl(*mp) | 1UL << (type % FC_NS_BPW));
}

/*
 * Handle received RLIR - registered link incident report.
 */
static void fc_lport_rlir_req(struct fc_seq *sp, struct fc_frame *fp,
			      struct fc_lport *lp)
{
	lp->tt.seq_els_rsp_send(sp, ELS_LS_ACC, NULL);
	fc_frame_free(fp);
}

/*
 * Handle received ECHO.
 */
static void fc_lport_echo_req(struct fc_seq *sp, struct fc_frame *in_fp,
			      struct fc_lport *lp)
{
	struct fc_frame *fp;
	unsigned int len;
	void *pp;
	void *dp;
	u32 f_ctl;

	len = fr_len(in_fp) - sizeof(struct fc_frame_header);
	pp = fc_frame_payload_get(in_fp, len);

	if (len < sizeof(__be32))
		len = sizeof(__be32);
	fp = fc_frame_alloc(lp, len);
	if (fp) {
		dp = fc_frame_payload_get(fp, len);
		memcpy(dp, pp, len);
		*((u32 *)dp) = htonl(ELS_LS_ACC << 24);
		sp = lp->tt.seq_start_next(sp);
		f_ctl = FC_FC_LAST_SEQ | FC_FC_END_SEQ;
		fc_frame_setup(fp, FC_RCTL_ELS_REP, FC_TYPE_ELS);
		lp->tt.seq_send(lp, sp, fp, f_ctl);
	}
	fc_frame_free(in_fp);
}

/*
 * Handle received RNID.
 */
static void fc_lport_rnid_req(struct fc_seq *sp, struct fc_frame *in_fp,
			      struct fc_lport *lp)
{
	struct fc_frame *fp;
	struct fc_els_rnid *req;
	struct {
		struct fc_els_rnid_resp rnid;
		struct fc_els_rnid_cid cid;
		struct fc_els_rnid_gen gen;
	} *rp;
	struct fc_seq_els_data rjt_data;
	u8 fmt;
	size_t len;
	u32 f_ctl;

	req = fc_frame_payload_get(in_fp, sizeof(*req));
	if (!req) {
		rjt_data.fp = NULL;
		rjt_data.reason = ELS_RJT_LOGIC;
		rjt_data.explan = ELS_EXPL_NONE;
		lp->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &rjt_data);
	} else {
		fmt = req->rnid_fmt;
		len = sizeof(*rp);
		if (fmt != ELS_RNIDF_GEN ||
		    ntohl(lp->rnid_gen.rnid_atype) == 0) {
			fmt = ELS_RNIDF_NONE;	/* nothing to provide */
			len -= sizeof(rp->gen);
		}
		fp = fc_frame_alloc(lp, len);
		if (fp) {
			rp = fc_frame_payload_get(fp, len);
			memset(rp, 0, len);
			rp->rnid.rnid_cmd = ELS_LS_ACC;
			rp->rnid.rnid_fmt = fmt;
			rp->rnid.rnid_cid_len = sizeof(rp->cid);
			rp->cid.rnid_wwpn = htonll(lp->wwpn);
			rp->cid.rnid_wwnn = htonll(lp->wwnn);
			if (fmt == ELS_RNIDF_GEN) {
				rp->rnid.rnid_sid_len = sizeof(rp->gen);
				memcpy(&rp->gen, &lp->rnid_gen,
				       sizeof(rp->gen));
			}
			sp = lp->tt.seq_start_next(sp);
			f_ctl = FC_FC_SEQ_INIT | FC_FC_LAST_SEQ | FC_FC_END_SEQ;
			fc_frame_setup(fp, FC_RCTL_ELS_REP, FC_TYPE_ELS);
			lp->tt.seq_send(lp, sp, fp, f_ctl);
		}
	}
	fc_frame_free(in_fp);
}

/*
 * Handle received fabric logout request.
 */
static void fc_lport_recv_logo_req(struct fc_seq *sp, struct fc_frame *fp,
				   struct fc_lport *lp)
{
	lp->tt.seq_els_rsp_send(sp, ELS_LS_ACC, NULL);
	fc_lport_enter_reset(lp);
	fc_frame_free(fp);
}

/*
 * Receive request frame
 */

int fc_fabric_login(struct fc_lport *lp)
{
	int rc = -1;

	if (lp->state == LPORT_ST_NONE) {
		fc_lport_lock(lp);
		fc_lport_enter_reset(lp);
		fc_lport_unlock(lp);
		rc = 0;
	}
	return rc;
}
EXPORT_SYMBOL(fc_fabric_login);

/**
 * fc_linkup -	link up notification
 * @dev:      Pointer to fc_lport .
 **/
void fc_linkup(struct fc_lport *lp)
{
	if ((lp->link_status & FC_LINK_UP) != FC_LINK_UP) {
		lp->link_status |= FC_LINK_UP;
		fc_lport_lock(lp);
		if (lp->state == LPORT_ST_RESET)
			lp->tt.lport_login(lp);
		fc_lport_unlock(lp);
	}
}
EXPORT_SYMBOL(fc_linkup);

/**
 * fc_linkdown -  link down notification
 * @dev:      Pointer to fc_lport .
 **/
void fc_linkdown(struct fc_lport *lp)
{
	if ((lp->link_status & FC_LINK_UP) == FC_LINK_UP) {
		lp->link_status &= ~(FC_LINK_UP);
		fc_lport_enter_reset(lp);
		lp->tt.scsi_cleanup(lp);
	}
}
EXPORT_SYMBOL(fc_linkdown);

void fc_pause(struct fc_lport *lp)
{
	lp->link_status |= FC_PAUSE;
}
EXPORT_SYMBOL(fc_pause);

void fc_unpause(struct fc_lport *lp)
{
	lp->link_status &= ~(FC_PAUSE);
}
EXPORT_SYMBOL(fc_unpause);

int fc_fabric_logoff(struct fc_lport *lp)
{
	fc_lport_lock(lp);
	switch (lp->state) {
	case LPORT_ST_NONE:
		break;
	case LPORT_ST_FLOGI:
	case LPORT_ST_LOGO:
	case LPORT_ST_RESET:
		fc_lport_enter_reset(lp);
		break;
	case LPORT_ST_DNS:
	case LPORT_ST_DNS_STOP:
		fc_lport_enter_logo(lp);
		break;
	case LPORT_ST_REG_PN:
	case LPORT_ST_REG_FT:
	case LPORT_ST_SCR:
	case LPORT_ST_READY:
		lp->tt.disc_stop(lp);
		break;
	}
	fc_lport_unlock(lp);
	lp->tt.scsi_cleanup(lp);

	return 0;
}
EXPORT_SYMBOL(fc_fabric_logoff);

/**
 * fc_lport_destroy - unregister a fc_lport
 * @lp:	   fc_lport pointer to unregister
 *
 * Return value:
 *	None
 * Note:
 * exit routine for fc_lport instance
 * clean-up all the allocated memory
 * and free up other system resources.
 *
 **/
int fc_lport_destroy(struct fc_lport *lp)
{
	fc_lport_lock(lp);
	fc_lport_state_enter(lp, LPORT_ST_LOGO);
	fc_lport_unlock(lp);

	cancel_delayed_work_sync(&lp->ns_disc_work);

	lp->tt.scsi_abort_io(lp);

	lp->tt.frame_send = fc_frame_drop;

	lp->tt.exch_mgr_reset(lp->emp, 0, 0);

	return 0;
}
EXPORT_SYMBOL(fc_lport_destroy);

int fc_set_mfs(struct fc_lport *lp, u32 mfs)
{
	unsigned int old_mfs;
	int rc = -1;

	old_mfs = lp->mfs;

	if (mfs >= FC_MIN_MAX_FRAME) {
		mfs &= ~3;
		WARN_ON((size_t) mfs < FC_MIN_MAX_FRAME);
		if (mfs > FC_MAX_FRAME)
			mfs = FC_MAX_FRAME;
		mfs -= sizeof(struct fc_frame_header);
		lp->mfs = mfs;
		rc = 0;
	}

	if (!rc && mfs < old_mfs) {
		lp->ns_disc_done = 0;
		fc_lport_enter_reset(lp);
	}
	return rc;
}
EXPORT_SYMBOL(fc_set_mfs);

/*
 * re-enter state for retrying a request after a timeout or alloc failure.
 */
static void fc_lport_enter_retry(struct fc_lport *lp)
{
	switch (lp->state) {
	case LPORT_ST_NONE:
	case LPORT_ST_READY:
	case LPORT_ST_RESET:
	case LPORT_ST_DNS:
	case LPORT_ST_DNS_STOP:
	case LPORT_ST_REG_PN:
	case LPORT_ST_REG_FT:
	case LPORT_ST_SCR:
		WARN_ON(1);
		break;
	case LPORT_ST_FLOGI:
		fc_lport_enter_flogi(lp);
		break;
	case LPORT_ST_LOGO:
		fc_lport_enter_logo(lp);
		break;
	}
}

/*
 * enter next state for handling an exchange reject or retry exhaustion
 * in the current state.
 */
static void fc_lport_enter_reject(struct fc_lport *lp)
{
	switch (lp->state) {
	case LPORT_ST_NONE:
	case LPORT_ST_READY:
	case LPORT_ST_RESET:
	case LPORT_ST_REG_PN:
	case LPORT_ST_REG_FT:
	case LPORT_ST_SCR:
	case LPORT_ST_DNS_STOP:
	case LPORT_ST_DNS:
		WARN_ON(1);
		break;
	case LPORT_ST_FLOGI:
		fc_lport_enter_flogi(lp);
		break;
	case LPORT_ST_LOGO:
		fc_lport_enter_reset(lp);
		break;
	}
}

/*
 * Handle resource allocation problem by retrying in a bit.
 */
static void fc_lport_retry(struct fc_lport *lp)
{
	if (lp->retry_count == 0)
		FC_DBG("local port %6x alloc failure in state %s "
		       "- will retry\n", lp->fid, fc_lport_state(lp));
	if (lp->retry_count < lp->max_retry_count) {
		lp->retry_count++;
		mod_timer(&lp->state_timer,
			  jiffies + msecs_to_jiffies(lp->e_d_tov));
	} else {
		FC_DBG("local port %6x alloc failure in state %s "
		       "- retries exhausted\n", lp->fid,
		       fc_lport_state(lp));
		fc_lport_enter_reject(lp);
	}
}

/*
 * A received FLOGI request indicates a point-to-point connection.
 * Accept it with the common service parameters indicating our N port.
 * Set up to do a PLOGI if we have the higher-number WWPN.
 */
static void fc_lport_recv_flogi_req(struct fc_seq *sp_in,
				    struct fc_frame *rx_fp,
				    struct fc_lport *lp)
{
	struct fc_frame *fp;
	struct fc_frame_header *fh;
	struct fc_seq *sp;
	struct fc_els_flogi *flp;
	struct fc_els_flogi *new_flp;
	u64 remote_wwpn;
	u32 remote_fid;
	u32 local_fid;
	u32 f_ctl;

	fh = fc_frame_header_get(rx_fp);
	remote_fid = ntoh24(fh->fh_s_id);
	flp = fc_frame_payload_get(rx_fp, sizeof(*flp));
	if (!flp)
		goto out;
	remote_wwpn = get_unaligned_be64(&flp->fl_wwpn);
	if (remote_wwpn == lp->wwpn) {
		FC_DBG("FLOGI from port with same WWPN %llx "
		       "possible configuration error\n", remote_wwpn);
		goto out;
	}
	FC_DBG("FLOGI from port WWPN %llx\n", remote_wwpn);
	fc_lport_lock(lp);

	/*
	 * XXX what is the right thing to do for FIDs?
	 * The originator might expect our S_ID to be 0xfffffe.
	 * But if so, both of us could end up with the same FID.
	 */
	local_fid = FC_LOCAL_PTP_FID_LO;
	if (remote_wwpn < lp->wwpn) {
		local_fid = FC_LOCAL_PTP_FID_HI;
		if (!remote_fid || remote_fid == local_fid)
			remote_fid = FC_LOCAL_PTP_FID_LO;
	} else if (!remote_fid) {
		remote_fid = FC_LOCAL_PTP_FID_HI;
	}
	fc_lport_set_fid(lp, local_fid);

	fp = fc_frame_alloc(lp, sizeof(*flp));
	if (fp) {
		sp = lp->tt.seq_start_next(fr_seq(rx_fp));
		new_flp = fc_frame_payload_get(fp, sizeof(*flp));
		fc_lport_flogi_fill(lp, new_flp, ELS_FLOGI);
		new_flp->fl_cmd = (u8) ELS_LS_ACC;

		/*
		 * Send the response.  If this fails, the originator should
		 * repeat the sequence.
		 */
		f_ctl = FC_FC_LAST_SEQ | FC_FC_END_SEQ;
		fc_frame_setup(fp, FC_RCTL_ELS_REP, FC_TYPE_ELS);
		lp->tt.seq_send(lp, sp, fp, f_ctl);

	} else {
		fc_lport_retry(lp);
	}
	fc_lport_ptp_setup(lp, remote_fid, remote_wwpn,
			   get_unaligned_be64(&flp->fl_wwnn));
	fc_lport_unlock(lp);
	if (lp->tt.disc_start(lp))
		FC_DBG("target discovery start error\n");
out:
	sp = fr_seq(rx_fp);
	fc_frame_free(rx_fp);
}

static void fc_lport_recv(struct fc_lport *lp, struct fc_seq *sp,
			  struct fc_frame *fp)
{
	struct fc_frame_header *fh = fc_frame_header_get(fp);
	void (*recv) (struct fc_seq *, struct fc_frame *, struct fc_lport *);
	struct fc_rport *rport;
	u32 s_id;
	u32 d_id;
	struct fc_seq_els_data rjt_data;

	/*
	 * Handle special ELS cases like FLOGI, LOGO, and
	 * RSCN here.  These don't require a session.
	 * Even if we had a session, it might not be ready.
	 */
	if (fh->fh_type == FC_TYPE_ELS && fh->fh_r_ctl == FC_RCTL_ELS_REQ) {
		/*
		 * Check opcode.
		 */
		recv = NULL;
		switch (fc_frame_payload_op(fp)) {
		case ELS_FLOGI:
			recv = fc_lport_recv_flogi_req;
			break;
		case ELS_LOGO:
			fh = fc_frame_header_get(fp);
			if (ntoh24(fh->fh_s_id) == FC_FID_FLOGI)
				recv = fc_lport_recv_logo_req;
			break;
		case ELS_RSCN:
			recv = lp->tt.disc_recv_req;
			break;
		case ELS_ECHO:
			recv = fc_lport_echo_req;
			break;
		case ELS_RLIR:
			recv = fc_lport_rlir_req;
			break;
		case ELS_RNID:
			recv = fc_lport_rnid_req;
			break;
		}

		if (recv)
			recv(sp, fp, lp);
		else {
			/*
			 * Find session.
			 * If this is a new incoming PLOGI, we won't find it.
			 */
			s_id = ntoh24(fh->fh_s_id);
			d_id = ntoh24(fh->fh_d_id);

			rport = lp->tt.rport_lookup(lp, s_id);
			if (rport) {
				lp->tt.rport_recv_req(sp, fp, rport);
				put_device(&rport->dev); /* hold from lookup */
			} else {
				rjt_data.fp = NULL;
				rjt_data.reason = ELS_RJT_UNAB;
				rjt_data.explan = ELS_EXPL_NONE;
				lp->tt.seq_els_rsp_send(sp,
							ELS_LS_RJT, &rjt_data);
				fc_frame_free(fp);
			}
		}
	} else {
		FC_DBG("dropping invalid frame (eof %x)\n", fr_eof(fp));
		fc_frame_free(fp);
	}

	/*
	 *  The common exch_done for all request may not be good
	 *  if any request requires longer hold on exhange. XXX
	 */
	lp->tt.exch_done(sp);
}

/*
 * Put the local port back into the initial state.  Reset all sessions.
 * This is called after a SCSI reset or the driver is unloading
 * or the program is exiting.
 */
int fc_lport_enter_reset(struct fc_lport *lp)
{
	if (fc_lport_debug)
		FC_DBG("Processing RESET state\n");

	if (lp->dns_rp) {
		fc_remote_port_delete(lp->dns_rp);
		lp->dns_rp = NULL;
	}
	fc_lport_ptp_clear(lp);

	/*
	 * Setting state RESET keeps fc_lport_error() callbacks
	 * by exch_mgr_reset() from recursing on the lock.
	 * It also causes fc_lport_sess_event() to ignore events.
	 * The lock is held for the duration of the time in RESET state.
	 */
	fc_lport_state_enter(lp, LPORT_ST_RESET);
	lp->tt.exch_mgr_reset(lp->emp, 0, 0);
	fc_lport_set_fid(lp, 0);
	if ((lp->link_status & FC_LINK_UP) == FC_LINK_UP)
		fc_lport_enter_flogi(lp);
	return 0;
}
EXPORT_SYMBOL(fc_lport_enter_reset);

/*
 * Handle errors on local port requests.
 * Don't get locks if in RESET state.
 * The only possible errors so far are exchange TIMEOUT and CLOSED (reset).
 */
static void fc_lport_error(struct fc_lport *lp, struct fc_frame *fp)
{
	if (lp->state == LPORT_ST_RESET)
		return;

	fc_lport_lock(lp);
	if (PTR_ERR(fp) == -FC_EX_TIMEOUT) {
		if (lp->retry_count < lp->max_retry_count) {
			lp->retry_count++;
			fc_lport_enter_retry(lp);
		} else {
			fc_lport_enter_reject(lp);

		}
	}
	if (fc_lport_debug)
		FC_DBG("error %ld retries %d limit %d\n",
		       PTR_ERR(fp), lp->retry_count, lp->max_retry_count);
	fc_lport_unlock(lp);
}

static void fc_lport_timeout(unsigned long lp_arg)
{
	struct fc_lport *lp = (struct fc_lport *)lp_arg;

	fc_lport_lock(lp);
	fc_lport_enter_retry(lp);
	fc_lport_unlock(lp);
}

static void fc_lport_logo_resp(struct fc_seq *sp, struct fc_frame *fp,
			       void *lp_arg)
{
	struct fc_lport *lp = lp_arg;

	if (IS_ERR(fp))
		fc_lport_error(lp, fp);
	else {
		fc_frame_free(fp);
		fc_lport_lock(lp);
		fc_lport_enter_reset(lp);
		fc_lport_unlock(lp);
	}
}

/* Logout of the FC fabric */
static void fc_lport_enter_logo(struct fc_lport *lp)
{
	struct fc_frame *fp;
	struct fc_els_logo *logo;

	if (fc_lport_debug)
		FC_DBG("Processing LOGO state\n");

	fc_lport_state_enter(lp, LPORT_ST_LOGO);

	/* DNS session should be closed so we can release it here */
	if (lp->dns_rp) {
		fc_remote_port_delete(lp->dns_rp);
		lp->dns_rp = NULL;
	}

	fp = fc_frame_alloc(lp, sizeof(*logo));
	if (!fp) {
		FC_DBG("failed to allocate frame\n");
		return;
	}

	logo = fc_frame_payload_get(fp, sizeof(*logo));
	memset(logo, 0, sizeof(*logo));
	logo->fl_cmd = ELS_LOGO;
	hton24(logo->fl_n_port_id, lp->fid);
	logo->fl_n_port_wwn = htonll(lp->wwpn);

	fc_frame_setup(fp, FC_RCTL_ELS_REQ, FC_TYPE_ELS);
	fc_frame_set_offset(fp, 0);

	lp->tt.exch_seq_send(lp, fp,
			      fc_lport_logo_resp,
			      lp, lp->e_d_tov,
			      lp->fid, FC_FID_FLOGI,
			      FC_FC_SEQ_INIT | FC_FC_END_SEQ);
}

static int fc_lport_logout(struct fc_lport *lp)
{
	fc_lport_lock(lp);
	if (lp->state != LPORT_ST_LOGO)
		fc_lport_enter_logo(lp);
	fc_lport_unlock(lp);
	return 0;
}

/*
 * Handle incoming ELS FLOGI response.
 * Save parameters of remote switch.  Finish exchange.
 */
static void
fc_lport_flogi_resp(struct fc_seq *sp, struct fc_frame *fp, void *lp_arg)
{
	struct fc_lport *lp = lp_arg;
	struct fc_frame_header *fh;
	struct fc_els_flogi *flp;
	u32 did;
	u16 csp_flags;
	unsigned int r_a_tov;
	unsigned int e_d_tov;
	u16 mfs;

	if (IS_ERR(fp)) {
		fc_lport_error(lp, fp);
		return;
	}

	fh = fc_frame_header_get(fp);
	did = ntoh24(fh->fh_d_id);
	if (fc_frame_payload_op(fp) == ELS_LS_ACC && did != 0) {
		if (fc_lport_debug)
			FC_DBG("assigned fid %x\n", did);
		fc_lport_lock(lp);
		fc_lport_set_fid(lp, did);
		flp = fc_frame_payload_get(fp, sizeof(*flp));
		if (flp) {
			mfs = ntohs(flp->fl_csp.sp_bb_data) &
				FC_SP_BB_DATA_MASK;
			if (mfs >= FC_SP_MIN_MAX_PAYLOAD &&
			    mfs < lp->mfs)
				lp->mfs = mfs;
			csp_flags = ntohs(flp->fl_csp.sp_features);
			r_a_tov = ntohl(flp->fl_csp.sp_r_a_tov);
			e_d_tov = ntohl(flp->fl_csp.sp_e_d_tov);
			if (csp_flags & FC_SP_FT_EDTR)
				e_d_tov /= 1000000;
			if ((csp_flags & FC_SP_FT_FPORT) == 0) {
				if (e_d_tov > lp->e_d_tov)
					lp->e_d_tov = e_d_tov;
				lp->r_a_tov = 2 * e_d_tov;
				FC_DBG("point-to-point mode\n");
				fc_lport_ptp_setup(lp, ntoh24(fh->fh_s_id),
						   get_unaligned_be64(
							   &flp->fl_wwpn),
						   get_unaligned_be64(
							   &flp->fl_wwnn));
			} else {
				lp->e_d_tov = e_d_tov;
				lp->r_a_tov = r_a_tov;
				lp->tt.dns_register(lp);
			}
		}
		fc_lport_unlock(lp);
		if (flp) {
			csp_flags = ntohs(flp->fl_csp.sp_features);
			if ((csp_flags & FC_SP_FT_FPORT) == 0) {
				if (lp->tt.disc_start(lp))
					FC_DBG("target disc start error\n");
			}
		}
	} else {
		FC_DBG("bad FLOGI response\n");
	}
	fc_frame_free(fp);
}

/*
 * Send ELS (extended link service) FLOGI request to peer.
 */
static void fc_lport_flogi_send(struct fc_lport *lp)
{
	struct fc_frame *fp;
	struct fc_els_flogi *flp;

	fp = fc_frame_alloc(lp, sizeof(*flp));
	if (!fp)
		return fc_lport_retry(lp);

	flp = fc_frame_payload_get(fp, sizeof(*flp));
	fc_lport_flogi_fill(lp, flp, ELS_FLOGI);

	fc_frame_setup(fp, FC_RCTL_ELS_REQ, FC_TYPE_ELS);
	fc_frame_set_offset(fp, 0);

	if (!lp->tt.exch_seq_send(lp, fp,
				   fc_lport_flogi_resp,
				   lp, lp->e_d_tov,
				   0, FC_FID_FLOGI,
				   FC_FC_SEQ_INIT | FC_FC_END_SEQ))
		fc_lport_retry(lp);

}

void fc_lport_enter_flogi(struct fc_lport *lp)
{
	if (fc_lport_debug)
		FC_DBG("Processing FLOGI state\n");
	fc_lport_state_enter(lp, LPORT_ST_FLOGI);
	fc_lport_flogi_send(lp);
}

/* Configure a fc_lport */
int fc_lport_config(struct fc_lport *lp)
{
	setup_timer(&lp->state_timer, fc_lport_timeout, (unsigned long)lp);
	spin_lock_init(&lp->state_lock);

	fc_lport_lock(lp);
	fc_lport_state_enter(lp, LPORT_ST_NONE);
	fc_lport_unlock(lp);

	lp->ns_disc_delay = DNS_DELAY;

	fc_lport_add_fc4_type(lp, FC_TYPE_FCP);
	fc_lport_add_fc4_type(lp, FC_TYPE_CT);

	return 0;
}
EXPORT_SYMBOL(fc_lport_config);

int fc_lport_init(struct fc_lport *lp)
{
	if (!lp->tt.lport_recv)
		lp->tt.lport_recv = fc_lport_recv;

	if (!lp->tt.lport_login)
		lp->tt.lport_login = fc_lport_enter_reset;

	if (!lp->tt.lport_reset)
		lp->tt.lport_reset = fc_lport_enter_reset;

	if (!lp->tt.lport_logout)
		lp->tt.lport_logout = fc_lport_logout;

	return 0;
}
EXPORT_SYMBOL(fc_lport_init);
