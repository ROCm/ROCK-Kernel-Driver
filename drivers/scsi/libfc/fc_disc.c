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
 *
 * This block discovers all FC-4 remote ports, including FCP initiators. It
 * also handles RSCN events and re-discovery if necessary.
 */

#include <linux/timer.h>
#include <linux/err.h>
#include <asm/unaligned.h>

#include <scsi/fc/fc_gs.h>

#include <scsi/libfc/libfc.h>

#define FC_DISC_RETRY_LIMIT	3	/* max retries */
#define FC_DISC_RETRY_DELAY	500UL	/* (msecs) delay */

static int fc_disc_debug;

#define FC_DEBUG_DISC(fmt...)			\
	do {					\
		if (fc_disc_debug)		\
			FC_DBG(fmt);		\
	} while (0)

static void fc_disc_gpn_ft_req(struct fc_lport *);
static void fc_disc_gpn_ft_resp(struct fc_seq *, struct fc_frame *, void *);
static int fc_disc_new_target(struct fc_lport *, struct fc_rport *,
			      struct fc_rport_identifiers *);
static void fc_disc_del_target(struct fc_lport *, struct fc_rport *);
static void fc_disc_done(struct fc_lport *);
static void fc_disc_error(struct fc_lport *, struct fc_frame *);
static void fc_disc_timeout(struct work_struct *);
static void fc_disc_single(struct fc_lport *, struct fc_disc_port *);
static int fc_disc_restart(struct fc_lport *);

/**
 * fc_disc_recv_rscn_req - Handle Registered State Change Notification (RSCN)
 * @sp: Current sequence of the RSCN exchange
 * @fp: RSCN Frame
 * @lport: Fibre Channel host port instance
 */
static void fc_disc_recv_rscn_req(struct fc_seq *sp, struct fc_frame *fp,
				  struct fc_lport *lport)
{
	struct fc_els_rscn *rp;
	struct fc_els_rscn_page *pp;
	struct fc_seq_els_data rjt_data;
	unsigned int len;
	int redisc = 0;
	enum fc_els_rscn_ev_qual ev_qual;
	enum fc_els_rscn_addr_fmt fmt;
	LIST_HEAD(disc_list);
	struct fc_disc_port *dp, *next;

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
			FC_DEBUG_DISC("Port address format for port (%6x)\n",
				      ntoh24(pp->rscn_fid));
			dp = kzalloc(sizeof(*dp), GFP_KERNEL);
			if (!dp) {
				redisc = 1;
				break;
			}
			dp->lp = lport;
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
			FC_DEBUG_DISC("Address format is (%d)\n", fmt);
			redisc = 1;
			break;
		}
	}
	lport->tt.seq_els_rsp_send(sp, ELS_LS_ACC, NULL);
	if (redisc) {
		FC_DEBUG_DISC("RSCN received: rediscovering\n");
		list_for_each_entry_safe(dp, next, &disc_list, peers) {
			list_del(&dp->peers);
			kfree(dp);
		}
		fc_disc_restart(lport);
	} else {
		FC_DEBUG_DISC("RSCN received: not rediscovering. "
			      "redisc %d state %d in_prog %d\n",
			      redisc, lport->state, lport->disc_pending);
		list_for_each_entry_safe(dp, next, &disc_list, peers) {
			list_del(&dp->peers);
			fc_disc_single(lport, dp);
		}
	}
	fc_frame_free(fp);
	return;
reject:
	rjt_data.fp = NULL;
	rjt_data.reason = ELS_RJT_LOGIC;
	rjt_data.explan = ELS_EXPL_NONE;
	lport->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &rjt_data);
	fc_frame_free(fp);
}

/**
 * fc_disc_recv_req - Handle incoming requests
 * @sp: Current sequence of the request exchange
 * @fp: The frame
 * @lport: The FC local port
 */
static void fc_disc_recv_req(struct fc_seq *sp, struct fc_frame *fp,
			     struct fc_lport *lport)
{
	u8 op;

	op = fc_frame_payload_op(fp);
	switch (op) {
	case ELS_RSCN:
		fc_disc_recv_rscn_req(sp, fp, lport);
		break;
	default:
		FC_DBG("Received an unsupported request. opcode (%x)\n", op);
		break;
	}
}

/**
 * fc_disc_restart - Restart discovery
 * @lport: FC local port
 */
static int fc_disc_restart(struct fc_lport *lport)
{
	if (!lport->disc_requested && !lport->disc_pending) {
		schedule_delayed_work(&lport->disc_work,
				      msecs_to_jiffies(lport->disc_delay * 1000));
	}
	lport->disc_requested = 1;
	return 0;
}

/**
 * fc_disc_start - Fibre Channel Target discovery
 * @lport: FC local port
 *
 * Returns non-zero if discovery cannot be started.
 */
static int fc_disc_start(struct fc_lport *lport)
{
	struct fc_rport *rport;
	int error;
	struct fc_rport_identifiers ids;

	/*
	 * If not ready, or already running discovery, just set request flag.
	 */
	if (!fc_lport_test_ready(lport) || lport->disc_pending) {
		lport->disc_requested = 1;

		return 0;
	}
	lport->disc_pending = 1;
	lport->disc_requested = 0;
	lport->disc_retry_count = 0;

	/*
	 * Handle point-to-point mode as a simple discovery
	 * of the remote port.
	 */
	rport = lport->ptp_rp;
	if (rport) {
		ids.port_id = rport->port_id;
		ids.port_name = rport->port_name;
		ids.node_name = rport->node_name;
		ids.roles = FC_RPORT_ROLE_UNKNOWN;
		get_device(&rport->dev);

		error = fc_disc_new_target(lport, rport, &ids);
		put_device(&rport->dev);
		if (!error)
			fc_disc_done(lport);
	} else {
		fc_disc_gpn_ft_req(lport);	/* get ports by FC-4 type */
		error = 0;
	}
	return error;
}

/**
 * fc_disc_retry - Retry discovery
 * @lport: FC local port
 */
static void fc_disc_retry(struct fc_lport *lport)
{
	unsigned long delay = FC_DISC_RETRY_DELAY;

	if (!lport->disc_retry_count)
		delay /= 4;	/* timeout faster first time */
	if (lport->disc_retry_count++ < FC_DISC_RETRY_LIMIT)
		schedule_delayed_work(&lport->disc_work,
				      msecs_to_jiffies(delay));
	else
		fc_disc_done(lport);
}

/**
 * fc_disc_new_target - Handle new target found by discovery
 * @lport: FC local port
 * @rport: The previous FC remote port (NULL if new remote port)
 * @ids: Identifiers for the new FC remote port
 */
static int fc_disc_new_target(struct fc_lport *lport,
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
			fc_disc_del_target(lport, rport);
			rport = NULL;
		}
	}
	if (((ids->port_name != -1) || (ids->port_id != -1)) &&
	    ids->port_id != fc_host_port_id(lport->host) &&
	    ids->port_name != lport->wwpn) {
		if (!rport) {
			rport = lport->tt.rport_lookup(lport, ids->port_id);
			if (!rport) {
				struct fc_disc_port dp;
				dp.lp = lport;
				dp.ids.port_id = ids->port_id;
				dp.ids.port_name = ids->port_name;
				dp.ids.node_name = ids->node_name;
				dp.ids.roles = ids->roles;
				rport = fc_rport_rogue_create(&dp);
			}
			if (!rport)
				error = ENOMEM;
		}
		if (rport) {
			rp = rport->dd_data;
			rp->event_callback = lport->tt.event_callback;
			rp->rp_state = RPORT_ST_INIT;
			lport->tt.rport_login(rport);
		}
	}
	return error;
}

/**
 * fc_disc_del_target - Delete a target
 * @lport: FC local port
 * @rport: The remote port to be removed
 */
static void fc_disc_del_target(struct fc_lport *lport, struct fc_rport *rport)
{
	lport->tt.rport_stop(rport);
}

/**
 * fc_disc_done - Discovery has been completed
 * @lport: FC local port
 */
static void fc_disc_done(struct fc_lport *lport)
{
	lport->disc_done = 1;
	lport->disc_pending = 0;
	if (lport->disc_requested)
		lport->tt.disc_start(lport);
}

/**
 * fc_disc_gpn_ft_req - Send Get Port Names by FC-4 type (GPN_FT) request
 * @lport: FC local port
 */
static void fc_disc_gpn_ft_req(struct fc_lport *lport)
{
	struct fc_frame *fp;
	struct fc_seq *sp = NULL;
	struct req {
		struct fc_ct_hdr ct;
		struct fc_ns_gid_ft gid;
	} *rp;
	int error = 0;

	lport->disc_buf_len = 0;
	lport->disc_seq_count = 0;
	fp = fc_frame_alloc(lport, sizeof(*rp));
	if (!fp) {
		error = ENOMEM;
	} else {
		rp = fc_frame_payload_get(fp, sizeof(*rp));
		fc_fill_dns_hdr(lport, &rp->ct, FC_NS_GPN_FT, sizeof(rp->gid));
		rp->gid.fn_fc4_type = FC_TYPE_FCP;

		WARN_ON(!fc_lport_test_ready(lport));

		fc_frame_setup(fp, FC_RCTL_DD_UNSOL_CTL, FC_TYPE_CT);
		sp = lport->tt.exch_seq_send(lport, fp,
					     fc_disc_gpn_ft_resp, NULL,
					     lport, lport->e_d_tov,
					     fc_host_port_id(lport->host),
					     FC_FID_DIR_SERV,
					     FC_FC_SEQ_INIT | FC_FC_END_SEQ);
	}
	if (error || !sp)
		fc_disc_retry(lport);
}

/**
 * fc_disc_error - Handle error on dNS request
 * @lport: FC local port
 * @fp: The frame pointer
 */
static void fc_disc_error(struct fc_lport *lport, struct fc_frame *fp)
{
	long err = PTR_ERR(fp);

	FC_DEBUG_DISC("Error %ld, retries %d/%d\n", PTR_ERR(fp),
		      lport->retry_count, FC_DISC_RETRY_LIMIT);
    
	switch (err) {
	case -FC_EX_TIMEOUT:
		if (lport->disc_retry_count++ < FC_DISC_RETRY_LIMIT) {
			fc_disc_gpn_ft_req(lport);
		} else {
			fc_disc_done(lport);
		}
		break;
	default:
		FC_DBG("Error code %ld not supported\n", err);
		fc_disc_done(lport);
		break;
	}
}

/**
 * fc_disc_gpn_ft_parse - Parse the list of IDs and names resulting from a request
 * @lport: Fibre Channel host port instance
 * @buf: GPN_FT response buffer
 * @len: size of response buffer
 */
static int fc_disc_gpn_ft_parse(struct fc_lport *lport, void *buf, size_t len)
{
	struct fc_gpn_ft_resp *np;
	char *bp;
	size_t plen;
	size_t tlen;
	int error = 0;
	struct fc_disc_port dp;
	struct fc_rport *rport;
	struct fc_rport_libfc_priv *rdata;

	/*
	 * Handle partial name record left over from previous call.
	 */
	bp = buf;
	plen = len;
	np = (struct fc_gpn_ft_resp *)bp;
	tlen = lport->disc_buf_len;
	if (tlen) {
		WARN_ON(tlen >= sizeof(*np));
		plen = sizeof(*np) - tlen;
		WARN_ON(plen <= 0);
		WARN_ON(plen >= sizeof(*np));
		if (plen > len)
			plen = len;
		np = &lport->disc_buf;
		memcpy((char *)np + tlen, bp, plen);

		/*
		 * Set bp so that the loop below will advance it to the
		 * first valid full name element.
		 */
		bp -= tlen;
		len += tlen;
		plen += tlen;
		lport->disc_buf_len = (unsigned char) plen;
		if (plen == sizeof(*np))
			lport->disc_buf_len = 0;
	}

	/*
	 * Handle full name records, including the one filled from above.
	 * Normally, np == bp and plen == len, but from the partial case above,
	 * bp, len describe the overall buffer, and np, plen describe the
	 * partial buffer, which if would usually be full now.
	 * After the first time through the loop, things return to "normal".
	 */
	while (plen >= sizeof(*np)) {
		dp.lp = lport;
		dp.ids.port_id = ntoh24(np->fp_fid);
		dp.ids.port_name = ntohll(np->fp_wwpn);
		dp.ids.node_name = -1;
		dp.ids.roles = FC_RPORT_ROLE_UNKNOWN;

		if ((dp.ids.port_id != fc_host_port_id(lport->host)) &&
		    (dp.ids.port_name != lport->wwpn)) {
			rport = fc_rport_rogue_create(&dp);
			if (rport) {
				rdata = rport->dd_data;
				rdata->event_callback = lport->tt.event_callback;
				rdata->local_port = lport;
				lport->tt.rport_login(rport);
			} else
				FC_DBG("Failed to allocate memory for "
				       "the newly discovered port (%6x)\n",
				       dp.ids.port_id);
		}

		if (np->fp_flags & FC_NS_FID_LAST) {
			fc_disc_done(lport);
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
		if (np != &lport->disc_buf)
			memcpy(&lport->disc_buf, np, len);
		lport->disc_buf_len = (unsigned char) len;
	} else {
		lport->disc_buf_len = 0;
	}
	return error;
}

/*
 * Handle retry of memory allocation for remote ports.
 */
static void fc_disc_timeout(struct work_struct *work)
{
	struct fc_lport *lport;

	lport = container_of(work, struct fc_lport, disc_work.work);

	if (lport->disc_pending)
		fc_disc_gpn_ft_req(lport);
	else
		lport->tt.disc_start(lport);
}

/**
 * fc_disc_gpn_ft_resp - Handle a response frame from Get Port Names (GPN_FT)
 * @sp: Current sequence of GPN_FT exchange
 * @fp: response frame
 * @lp_arg: Fibre Channel host port instance
 *
 * The response may be in multiple frames
 */
static void fc_disc_gpn_ft_resp(struct fc_seq *sp, struct fc_frame *fp,
				void *lp_arg)
{
	struct fc_lport *lport = lp_arg;
	struct fc_ct_hdr *cp;
	struct fc_frame_header *fh;
	unsigned int seq_cnt;
	void *buf = NULL;
	unsigned int len;
	int error;

	if (IS_ERR(fp)) {
		fc_disc_error(lport, fp);
		return;
	}

	WARN_ON(!fc_frame_is_linear(fp));	/* buffer must be contiguous */
	fh = fc_frame_header_get(fp);
	len = fr_len(fp) - sizeof(*fh);
	seq_cnt = ntohs(fh->fh_seq_cnt);
	if (fr_sof(fp) == FC_SOF_I3 && seq_cnt == 0 &&
	    lport->disc_seq_count == 0) {
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
			fc_disc_done(lport);
		} else {
			FC_DBG("GPN_FT unexpected response code %x\n",
			       ntohs(cp->ct_cmd));
		}
	} else if (fr_sof(fp) == FC_SOF_N3 &&
		   seq_cnt == lport->disc_seq_count) {
		buf = fh + 1;
	} else {
		FC_DBG("GPN_FT unexpected frame - out of sequence? "
		       "seq_cnt %x expected %x sof %x eof %x\n",
		       seq_cnt, lport->disc_seq_count, fr_sof(fp), fr_eof(fp));
	}
	if (buf) {
		error = fc_disc_gpn_ft_parse(lport, buf, len);
		if (error)
			fc_disc_retry(lport);
		else
			lport->disc_seq_count++;
	}
	fc_frame_free(fp);
}

/**
 * fc_disc_single - Discover the directory information for a single target
 * @lport: FC local port
 * @dp: The port to rediscover
 *
 * This could be from an RSCN that reported a change for the target.
 */
static void fc_disc_single(struct fc_lport *lport, struct fc_disc_port *dp)
{
	struct fc_rport *rport;
	struct fc_rport *new_rport;
	struct fc_rport_libfc_priv *rdata;

	if (dp->ids.port_id == fc_host_port_id(lport->host))
		goto out;

	rport = lport->tt.rport_lookup(lport, dp->ids.port_id);
	if (rport) {
		fc_disc_del_target(lport, rport);
		put_device(&rport->dev); /* hold from lookup */
	}

	new_rport = fc_rport_rogue_create(dp);
	if (new_rport) {
		rdata = new_rport->dd_data;
		rdata->event_callback = lport->tt.event_callback;
		kfree(dp);
		lport->tt.rport_login(new_rport);
	}
	return;
out:
	kfree(dp);
}

/**
 * fc_disc_init - Initialize the discovery block
 * @lport: FC local port
 */
int fc_disc_init(struct fc_lport *lport)
{
	INIT_DELAYED_WORK(&lport->disc_work, fc_disc_timeout);

	if (!lport->tt.disc_start)
		lport->tt.disc_start = fc_disc_start;

	if (!lport->tt.disc_recv_req)
		lport->tt.disc_recv_req = fc_disc_recv_req;

	return 0;
}
EXPORT_SYMBOL(fc_disc_init);
