/*
 *	Mobile IPv6 Mobility Header Functions for Mobile Node
 *
 *	Authors:
 *	Antti Tuominen	<ajtuomin@tml.hut.fi>
 *	Niklas Kämpe	<nhkampe@cc.hut.fi>
 *	Henrik Petander	<henrik.petander@hut.fi>
 *
 *	$Id:$
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 *
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <net/ipv6.h>
#include <net/addrconf.h>
#include <net/mipv6.h>

#include "mobhdr.h"
#include "mn.h"
#include "bul.h"
#include "rr_crypto.h"
#include "debug.h"
#include "util.h"
#include "stats.h"

int rr_configured = 1;

/* Return value of mipv6_rr_state() */
#define NO_RR			0
#define DO_RR			1
#define RR_FOR_COA		2
#define INPROGRESS_RR		3

/** 
 * send_bu_msg - sends a Binding Update 
 * @bulentry : BUL entry with the information for building a BU
 *
 * Function builds a BU msg based on the contents of a bul entry.
 * Does not change the bul entry.
 **/
static int send_bu_msg(struct mipv6_bul_entry *binding)
{ 
	int auth = 0; /* Use auth */
	int ret = 0;
	struct mipv6_auth_parm parm;
	struct mipv6_mh_bu bu;

	DEBUG_FUNC();

	if (!binding) {
		DEBUG(DBG_ERROR, "called with a null bul entry");
		return -1;
	}
	
	memset(&parm, 0, sizeof(parm));
	if (mipv6_prefix_compare(&binding->coa, &binding->home_addr, 64))
		parm.coa = &binding->home_addr;
	else
		parm.coa = &binding->coa;
	parm.cn_addr = &binding->cn_addr;

	if (binding->rr && binding->rr->kbu) {
		DEBUG(DBG_INFO, "Binding with key");
		auth = 1;
		parm.k_bu = binding->rr->kbu;
	}
	memset(&bu, 0, sizeof(bu));
	bu.flags = binding->flags;
	bu.sequence = htons(binding->seq);
	bu.lifetime = htons(binding->lifetime >> 2);
	bu.reserved = 0;

	ret = send_mh(&binding->cn_addr, &binding->home_addr,
		      MIPV6_MH_BU, sizeof(bu), (u8 *)&bu, 
		      &binding->home_addr, NULL, 
		      binding->ops, &parm);

	if (ret == 0)
		MIPV6_INC_STATS(n_bu_sent);

	return ret;
}

/**
 * mipv6_send_addr_test_init - send a HoTI or CoTI message
 * @saddr: source address for H/CoTI
 * @daddr: destination address for H/CoTI
 * @msg_type: Identifies whether HoTI or CoTI
 * @init_cookie: the HoTi or CoTi init cookie
 *
 * The message will be retransmitted till we get a HoT or CoT message, since 
 * our caller (mipv6_RR_start) has entered this message in the BUL with
 * exponential backoff retramission set.
 */
static int mipv6_send_addr_test_init(struct in6_addr *saddr,
				     struct in6_addr *daddr,
				     u8 msg_type,
				     u8 *init_cookie)
{
	struct mipv6_mh_addr_ti ti;
	struct mipv6_mh_opt *ops = NULL;
	int ret = 0;

	/* Set reserved and copy the cookie from address test init msg */
	ti.reserved = 0;
	memcpy(ti.init_cookie, init_cookie, MIPV6_RR_COOKIE_LENGTH);

	ret = send_mh(daddr, saddr, msg_type, sizeof(ti), (u8 *)&ti,
		      NULL, NULL, ops, NULL);
	if (ret == 0) {
		if (msg_type == MIPV6_MH_HOTI) {
			MIPV6_INC_STATS(n_hoti_sent);
		} else {
			MIPV6_INC_STATS(n_coti_sent);
		}
	}

	return ret;
}

/*
 *
 * Callback handlers for binding update list
 *
 */

/* Return value 0 means keep entry, non-zero means discard entry. */

/* Callback for BUs not requiring acknowledgement
 */
static int bul_expired(struct mipv6_bul_entry *bulentry)
{
	/* Lifetime expired, delete entry. */
	DEBUG(DBG_INFO, "bul entry 0x%x lifetime expired, deleting entry", (int) bulentry);
	return 1;
}

/* Callback for BUs requiring acknowledgement with exponential resending
 * scheme */
static int bul_resend_exp(struct mipv6_bul_entry *bulentry)
{
	unsigned long now = jiffies;
	
	DEBUG(DBG_INFO, "(0x%x) resending bu", (int) bulentry);

	
	/* If sending a de-registration, do not care about the
	 * lifetime value, as de-registrations are normally sent with
	 * a zero lifetime value. If the entry is a home entry get the 
	 * current lifetime. 
	 */

	if (bulentry->lifetime != 0) {
		bulentry->lifetime = mipv6_mn_get_bulifetime(
			&bulentry->home_addr, &bulentry->coa, bulentry->flags);

		bulentry->expire = now + bulentry->lifetime * HZ;
	} else {
		bulentry->expire = now + HOME_RESEND_EXPIRE * HZ; 
	}
	if (bulentry->rr) {
		/* Redo RR, if cookies have expired */
		if (time_after(jiffies, (unsigned long)(bulentry->rr->home_time + MAX_NONCE_LIFE * HZ)))
			bulentry->rr->rr_state |= RR_WAITH;
		if (time_after(jiffies, (unsigned long)(bulentry->rr->careof_time + MAX_NONCE_LIFE * HZ)))
			bulentry->rr->rr_state |= RR_WAITC;

		if (bulentry->rr->rr_state & RR_WAITH) {
				/* Resend HoTI directly */
			mipv6_send_addr_test_init(&bulentry->home_addr, 
						  &bulentry->cn_addr, MIPV6_MH_HOTI,
						  bulentry->rr->hot_cookie);
		}
		if (bulentry->rr->rr_state & RR_WAITC) {
				/* Resend CoTI directly */
				mipv6_send_addr_test_init(&bulentry->coa, 
							  &bulentry->cn_addr, MIPV6_MH_COTI,
							  bulentry->rr->cot_cookie);
			}
		goto out;
	}
	
	bulentry->seq++;

	if (send_bu_msg(bulentry) < 0)
		DEBUG(DBG_ERROR, "Resending of BU failed");

out:
	/* Schedule next retransmission */
	if (bulentry->delay < bulentry->maxdelay) {
		bulentry->delay = 2 * bulentry->delay;
		if (bulentry->delay > bulentry->maxdelay) {
			/* can happen if maxdelay is not power(mindelay, 2) */
			bulentry->delay = bulentry->maxdelay;
		}
	} else if (bulentry->flags & MIPV6_BU_F_HOME) {
		/* Home registration - continue sending BU at maxdelay rate */
		DEBUG(DBG_INFO, "Sending BU to HA after max ack wait time "
		      "reached(0x%x)", (int) bulentry);
		bulentry->delay = bulentry->maxdelay;
	} else if (!(bulentry->flags & MIPV6_BU_F_HOME)) {
		/* Failed to get BA from a CN */
		bulentry->callback_time = now;
		return -1;
	}
	
	bulentry->callback_time = now + bulentry->delay * HZ;
	return 0;
}



/* Callback for sending a registration refresh BU
 */
static int bul_refresh(struct mipv6_bul_entry *bulentry)
{
	unsigned long now = jiffies;
	
	/* Refresh interval passed, send new BU */
	DEBUG(DBG_INFO, "bul entry 0x%x refresh interval passed, sending new BU", (int) bulentry);
	if (bulentry->lifetime == 0)
		return 0;

	/* Set new maximum lifetime and expiration time */
	bulentry->lifetime = mipv6_mn_get_bulifetime(&bulentry->home_addr, 
						     &bulentry->coa, 
						     bulentry->flags);
	bulentry->expire = now + bulentry->lifetime * HZ;
	bulentry->seq++;
	/* Send update */
	if (send_bu_msg(bulentry) < 0)
		DEBUG(DBG_ERROR, "Resending of BU failed");
	
	if (bulentry->expire <= now) {
		/* Sanity check */
		DEBUG(DBG_ERROR, "bul entry expire time in history - setting expire to %u secs", ERROR_DEF_LIFETIME);
		bulentry->lifetime = ERROR_DEF_LIFETIME;
		bulentry->expire = now + ERROR_DEF_LIFETIME*HZ;
	}

	/* Set up retransmission */
	bulentry->state = RESEND_EXP;
	bulentry->callback = bul_resend_exp;
	bulentry->callback_time = now + INITIAL_BINDACK_TIMEOUT*HZ;
	bulentry->delay = INITIAL_BINDACK_TIMEOUT;
	bulentry->maxdelay = MAX_BINDACK_TIMEOUT;

	return 0;
}

static int mipv6_send_RR_bu(struct mipv6_bul_entry *bulentry)
{
	int ret;
	int ops_len = 0;
	u16 nonces[2];

	DEBUG(DBG_INFO, "Sending BU to CN  %x:%x:%x:%x:%x:%x:%x:%x "
	      "for home address %x:%x:%x:%x:%x:%x:%x:%x", 
	      NIPV6ADDR(&bulentry->cn_addr), NIPV6ADDR(&bulentry->home_addr));
	nonces[0] = bulentry->rr->home_nonce_index;
	nonces[1] = bulentry->rr->careof_nonce_index;
	ops_len = sizeof(struct mipv6_mo_bauth_data) + MIPV6_RR_MAC_LENGTH + 
			sizeof(struct mipv6_mo_nonce_indices);
	if (bulentry->ops) {
		DEBUG(DBG_WARNING, "Bul entry had existing mobility options, freeing them");
		kfree(bulentry->ops);
	}
	bulentry->ops = alloc_mh_opts(ops_len);

	if (!bulentry->ops)
		return -ENOMEM;
	if (append_mh_opt(bulentry->ops, MIPV6_OPT_NONCE_INDICES, 
			  sizeof(struct mipv6_mo_nonce_indices) - 2, nonces) < 0)
		return -ENOMEM;

	if (append_mh_opt(bulentry->ops, MIPV6_OPT_AUTH_DATA,
			  MIPV6_RR_MAC_LENGTH, NULL) < 0)
		return -ENOMEM;
	/* RR procedure is over, send a BU */
	if (!(bulentry->flags & MIPV6_BU_F_ACK)) {
		DEBUG(DBG_INFO, "Setting bul callback to bul_expires");
		bulentry->state = ACK_OK;
		bulentry->callback = bul_expired;
		bulentry->callback_time = jiffies + HZ * bulentry->lifetime;
		bulentry->expire = jiffies + HZ *  bulentry->lifetime;
	}
	else {
		bulentry->callback_time = jiffies + HZ;
		bulentry->expire = jiffies + HZ *  bulentry->lifetime;
	}

	ret  = send_bu_msg(bulentry);
	mipv6_bul_reschedule(bulentry);
	return ret;
}

static int mipv6_rr_state(struct mipv6_bul_entry *bul, struct in6_addr *saddr,
			  struct in6_addr *coa, __u8 flags)
{
	if (!rr_configured)
		return NO_RR;
       	if (flags & MIPV6_BU_F_HOME) {
		/* We don't need RR, this is a Home Registration */
		return NO_RR;
	}
	if (!bul || !bul->rr) {
		/* First time BU to CN, need RR */
		return DO_RR;
	}

	switch (bul->rr->rr_state) {
	case RR_INIT:
		/* Need RR if first BU to CN */
		return DO_RR;
	case RR_DONE:
		/* If MN moves to a new coa, do RR for it */
		if (!ipv6_addr_cmp(&bul->coa, coa))  
			return NO_RR; 
		else
			return DO_RR;
	default:
		/*
		 * We are in the middle of RR, the HoTI and CoTI have been
		 * sent. But we haven't got HoT and CoT from the CN, so
		 * don't do anything more at this time.
		 */
		return INPROGRESS_RR;
	}
}

/**
 * mipv6_RR_start - Start Return Routability procedure
 * @home_addr: home address
 * @cn_addr: correspondent address
 * @coa: care-of address
 * @entry: binding update list entry (if any)
 * @initdelay: initial ack timeout
 * @maxackdelay: maximum ack timeout
 * @flags: flags
 * @lifetime: lifetime of binding
 * @ops: mobility options
 *
 * Caller must hold @bul_lock (write).
 **/
static int mipv6_RR_start(struct in6_addr *home_addr, struct in6_addr *cn_addr,
			  struct in6_addr *coa, struct mipv6_bul_entry *entry,
			  __u32 initdelay, __u32 maxackdelay, __u8 flags, 
			  __u32 lifetime, struct mipv6_mh_opt *ops)
{
	int ret = -1;
	struct mipv6_bul_entry *bulentry = entry;
	struct mipv6_rr_info *rr = NULL;
	int seq = 0;
	DEBUG_FUNC();
	
	/* Do RR procedure only for care-of address after handoff, 
	   if home cookie is still valid */
	if (bulentry && bulentry->rr) {
		if (time_before(jiffies, (unsigned long)(bulentry->rr->home_time + MAX_NONCE_LIFE * HZ)) &&
		    lifetime && !(ipv6_addr_cmp(home_addr, coa) == 0)) { 
			mipv6_rr_mn_cookie_create(bulentry->rr->cot_cookie); 
			DEBUG(DBG_INFO, "Bul entry and rr info exist, only doing RR for CoA");
			ipv6_addr_copy(&bulentry->coa, coa);
			bulentry->rr->rr_state |= RR_WAITC;
		} else if (!lifetime) { /* Send only HoTi when returning home */
			mipv6_rr_mn_cookie_create(bulentry->rr->hot_cookie); 
			DEBUG(DBG_INFO, "Bul entry and rr info exist, only doing RR for HoA");
			ipv6_addr_copy(&bulentry->coa, coa); /* Home address as CoA */
			bulentry->rr->rr_state |= RR_WAITH;
		}
	} else {
		DEBUG(DBG_INFO, "Doing RR for both HoA and CoA");
		rr = kmalloc(sizeof(*rr), GFP_ATOMIC);
		memset(rr, 0, sizeof(*rr));
		mipv6_rr_mn_cookie_create(rr->cot_cookie);
		mipv6_rr_mn_cookie_create(rr->hot_cookie);
		rr->rr_state = RR_WAITHC;
	} 

	if (bulentry) 
		seq = bulentry->seq + 1;
	else
		seq = 0;
	/* Save the info in the BUL to retransmit the BU after RR is done */
	/* Caller must hold bul_lock (write) since we don't */
       
	if ((bulentry = mipv6_bul_add(cn_addr, home_addr, coa, 
				      min_t(__u32, lifetime, MAX_RR_BINDING_LIFE),
				      seq, flags, bul_resend_exp, initdelay, 
				      RESEND_EXP, initdelay, 
				      maxackdelay, ops, 
				      rr)) == NULL) {
		DEBUG(DBG_INFO, "couldn't update BUL for HoTi");
		goto out;
	}
	rr = bulentry->rr; 
	mipv6_send_addr_test_init(home_addr, cn_addr, MIPV6_MH_HOTI, 
				  rr->hot_cookie);
	if (ipv6_addr_cmp(home_addr, coa) && lifetime) 
		mipv6_send_addr_test_init(coa, cn_addr, MIPV6_MH_COTI, rr->cot_cookie);
	else {
		bulentry->rr->rr_state &= ~RR_WAITC;
	}
	ret = 0;
out:
	return ret;
}

/*
 * Status codes for mipv6_ba_rcvd()
 */
#define STATUS_UPDATE 0
#define STATUS_REMOVE 1

/**
 * mipv6_ba_rcvd - Update BUL for this Binding Acknowledgement
 * @ifindex: interface BA came from
 * @cnaddr: sender IPv6 address
 * @home_addr: home address
 * @sequence: sequence number
 * @lifetime: lifetime granted by Home Agent in seconds
 * @refresh: recommended resend interval
 * @status: %STATUS_UPDATE (ack) or %STATUS_REMOVE (nack)
 *
 * This function must be called to notify the module of the receipt of
 * a binding acknowledgement so that it can cease retransmitting the
 * option. The caller must have validated the acknowledgement before calling
 * this function. 'status' can be either STATUS_UPDATE in which case the
 * binding acknowledgement is assumed to be valid and the corresponding
 * binding update list entry is updated, or STATUS_REMOVE in which case
 * the corresponding binding update list entry is removed (this can be
 * used upon receiving a negative acknowledgement).
 * Returns 0 if a matching binding update has been sent or non-zero if
 * not.
 */
static int mipv6_ba_rcvd(int ifindex, struct in6_addr *cnaddr, 
			 struct in6_addr *home_addr, 
			 u16 sequence, u32 lifetime, 
			 u32 refresh, int status)
{
	struct mipv6_bul_entry *bulentry;
	unsigned long now = jiffies;
	struct in6_addr coa;

	DEBUG(DBG_INFO, "BA received with sequence number 0x%x, status: %d",
	      (int) sequence, status);

	/* Find corresponding entry in binding update list. */
	write_lock(&bul_lock);
	if ((bulentry = mipv6_bul_get(cnaddr, home_addr)) == NULL) {
		DEBUG(DBG_INFO, "- discarded, no entry in bul matches BA source address");
		write_unlock(&bul_lock);
		return -1;
	}
	
	ipv6_addr_copy(&coa, &bulentry->coa); 
	if (status != 0) {
		DEBUG(DBG_WARNING, "- NACK - BA status:  %d, deleting bul entry", status);
		if (bulentry->flags & MIPV6_BU_F_HOME) {
			DEBUG(DBG_ERROR, "Home registration failed: BA status:  %d, deleting bul entry", status);
			mipv6_mn_set_home_reg(home_addr, 0);
		}
		write_unlock(&bul_lock);
		return mipv6_bul_delete(cnaddr, home_addr);
	}

	/* Check that sequence numbers match */
	if (sequence != bulentry->seq) {
		/* retransmission handles bad seq number if needed */
		DEBUG(DBG_INFO, "BA discarded, seq number mismatch");
		write_unlock(&bul_lock);
		return -1;
	}
	bulentry->state = ACK_OK;

	if (bulentry->flags & MIPV6_BU_F_HOME && lifetime > 0) {
		/* For home registrations: schedule a refresh binding update.
		 * Use the refresh interval given by home agent or 80%
		 * of lifetime, whichever is less.
		 *
		 * Adjust binding lifetime if 'granted' lifetime
		 * (lifetime value in received binding acknowledgement)
		 * is shorter than 'requested' lifetime (lifetime
		 * value sent in corresponding binding update).
		 * max((L_remain - (L_update - L_ack)), 0)
		 */
		if (lifetime * HZ < (bulentry->expire - bulentry->lastsend)) {
			bulentry->expire = 
				max_t(__u32, bulentry->expire - 
				      ((bulentry->expire - bulentry->lastsend) - 
				       lifetime * HZ), jiffies + 
				      ERROR_DEF_LIFETIME * HZ);
		}
		if (refresh > lifetime || refresh == 0)
			refresh = 4 * lifetime / 5;
			DEBUG(DBG_INFO, "setting callback for expiration of"
			      " a Home Registration: lifetime:%d, refresh:%d",
			      lifetime, refresh);
		bulentry->callback = bul_refresh;
		bulentry->callback_time = now + refresh * HZ;
		bulentry->expire = now + lifetime * HZ;
		bulentry->lifetime = lifetime;
		if (bulentry->expire <= jiffies) {
			/* Sanity check */
			DEBUG(DBG_ERROR, "bul entry expire time in history - setting expire to %u secs",
			      ERROR_DEF_LIFETIME);
			bulentry->expire = jiffies + ERROR_DEF_LIFETIME * HZ;
		}
		mipv6_mn_set_home_reg(home_addr, 1);
		mipv6_bul_iterate(mn_cn_handoff, &coa);
	} else if ((bulentry->flags & MIPV6_BU_F_HOME) && bulentry->lifetime == 0) {
		write_unlock(&bul_lock);
		DEBUG(DBG_INFO, "Got BA for deregistration BU");
		mipv6_mn_set_home_reg(home_addr, 0);
		mipv6_bul_delete(cnaddr, home_addr);
		mipv6_mn_send_home_na(home_addr);

		write_lock_bh(&bul_lock);
		mipv6_bul_iterate(mn_cn_handoff, &coa);
		write_unlock_bh(&bul_lock);
 		return 0;
	}

	mipv6_bul_reschedule(bulentry);
	write_unlock(&bul_lock);

	return 0;
}

static int mipv6_handle_mh_HC_test(struct in6_addr *saddr,
				   struct in6_addr *coa,
				   struct in6_addr *cn,
				   struct in6_addr *unused,
				   struct mipv6_mh *mh)
{
	int ret = 0;
	int msg_len = (mh->length << 3) + 2;
	struct mipv6_mh_addr_test *tm = (struct mipv6_mh_addr_test *)mh->data;
	struct mipv6_bul_entry *bulentry;
	
	DEBUG_FUNC();

	if (msg_len < sizeof(*tm)) {
		DEBUG(DBG_INFO, "Mobility Header length less than H/C Test");
		return -1;
	}

#if 0 /* TODO: Use fcoa here */
	if (hao) {
		DEBUG(DBG_INFO, "H/C Test has HAO, dropped.");
		return -1;
	}
#endif

	write_lock(&bul_lock);

	/* We need to get the home address, since CoT only has the CoA*/
	if (mh->type == MIPV6_MH_COT) {
		if ((bulentry = mipv6_bul_get_by_ccookie(cn, tm->init_cookie)) == NULL) {
			DEBUG(DBG_ERROR, "has no BUL or RR state for "
			      "source:%x:%x:%x:%x:%x:%x:%x:%x",
			      NIPV6ADDR(cn));
			write_unlock(&bul_lock);
			return -1;
		}
	} else { /* HoT has the home address */
		if (((bulentry = mipv6_bul_get(cn, saddr)) == NULL) || !bulentry->rr) {
			DEBUG(DBG_ERROR, "has no BUL or RR state for "
			      "source:%x:%x:%x:%x:%x:%x:%x:%x "
			      "dest:%x:%x:%x:%x:%x:%x:%x:%x",
			      NIPV6ADDR(cn), NIPV6ADDR(saddr));
			write_unlock(&bul_lock);
			return -1;
		}
	}

	switch (mh->type) {
	case MIPV6_MH_HOT:
		if ((bulentry->rr->rr_state & RR_WAITH) == 0) {
			DEBUG(DBG_ERROR, "Not waiting for a Home Test message");
			goto out;
		}
#if 0
		/* Check for non-tunneled packet. TODO: How can one do this
		 * now since skb is not passed ? */
		if (!(skb->security & RCV_TUNNEL)) {
			DEBUG(DBG_ERROR, "Received a untunneled packet");
			goto out;
		}
#endif
		/*
		 * Make sure no home cookies have been received yet.
		 * TODO: Check not being put in at this time since subsequent
		 * BU's after this time will have home cookie stored.
		 */
	
		/* Check if the cookie received is the right one */
		if (!mipv6_equal_cookies(tm->init_cookie,
					 bulentry->rr->hot_cookie)) {
			/* Invalid cookie, might be an old cookie */
			DEBUG(DBG_WARNING, "Received HoT cookie does not match stored cookie");
			goto out;
		}
		DEBUG(DBG_INFO, "Got Care-of Test message");
		bulentry->rr->rr_state &= ~RR_WAITH;
		memcpy(bulentry->rr->home_cookie, tm->kgen_token, MIPV6_COOKIE_LEN);
		bulentry->rr->home_nonce_index = tm->nonce_index;
		bulentry->rr->home_time = jiffies;
		ret = 1;
		break;

	case MIPV6_MH_COT:
		if ((bulentry->rr->rr_state & RR_WAITC) == 0) {
			DEBUG(DBG_ERROR, "Not waiting for a Home Test message");
			goto out;
		}
		/*
		 * Make sure no home cookies have been received yet.
		 * TODO: Check not being put in at this time since subsequent
		 * BU's at this time will have careof cookie stored.
		 */
	
		/* Check if the cookie received is the right one */
		if (!mipv6_equal_cookies(tm->init_cookie,
					 bulentry->rr->cot_cookie)) {
			DEBUG(DBG_INFO, "Received CoT cookie does not match stored cookie");
			goto out;
		}
		bulentry->rr->rr_state &= ~RR_WAITC;
		memcpy(bulentry->rr->careof_cookie, tm->kgen_token, MIPV6_COOKIE_LEN);
		bulentry->rr->careof_nonce_index = tm->nonce_index;
		bulentry->rr->careof_time = jiffies;
		ret = 1;
		break;
	default:
		/* Impossible to get here */
		break;
	}
out:
	if (bulentry->rr->rr_state == RR_DONE) {
		if (bulentry->rr->kbu) /* First free any old keys */
			kfree(bulentry->rr->kbu);
		/* Store the session key to be used in BU's */
		if (ipv6_addr_cmp(&bulentry->coa, &bulentry->home_addr) && bulentry->lifetime)
			bulentry->rr->kbu = mipv6_rr_key_calc(bulentry->rr->home_cookie,
							      bulentry->rr->careof_cookie);
		else 
			bulentry->rr->kbu = mipv6_rr_key_calc(bulentry->rr->home_cookie,
							      NULL);
		/* RR procedure is over, send a BU */
		mipv6_send_RR_bu(bulentry);
	}
	write_unlock(&bul_lock);
	return ret;
}

/**
 * mipv6_handle_mh_brr - Binding Refresh Request handler
 * @home: home address
 * @coa: care-of address
 * @cn: source of this packet
 * @mh: pointer to the beginning of the Mobility Header
 *
 * Handles Binding Refresh Request.  Packet and offset to option are
 * passed.  Returns 0 on success, otherwise negative.
 **/
static int mipv6_handle_mh_brr(struct in6_addr *home,
			       struct in6_addr *coa,
			       struct in6_addr *cn,
			       struct in6_addr *unused,
			       struct mipv6_mh *mh)
{
	struct mipv6_mh_brr *brr = (struct mipv6_mh_brr *)mh->data;
	struct mipv6_bul_entry *binding;
	int msg_len = (mh->length << 3) + 2;

	if (msg_len < sizeof(*brr)) {
		DEBUG(DBG_WARNING, "Mobility Header length less than BRR");
		MIPV6_INC_STATS(n_brr_drop.invalid);
		return -1;
	}

	/* check we know src, else drop */
	write_lock(&bul_lock);
	if ((binding = mipv6_bul_get(cn, home)) == NULL) {
		MIPV6_INC_STATS(n_brr_drop.misc);
		write_unlock(&bul_lock);
		return MH_UNKNOWN_CN;
	}

	MIPV6_INC_STATS(n_brr_rcvd);

	if (msg_len > sizeof(*brr)) {
		struct mobopt opts;
		memset(&opts, 0, sizeof(opts));
		if (parse_mo_tlv(brr + 1, msg_len - sizeof(*brr), &opts) < 0) {
			write_unlock(&bul_lock);
			return -1;
		}
		/*
		 * MIPV6_OPT_AUTH_DATA
		 */
	}

	/* must hold bul_lock (write) */
	mipv6_RR_start(home, cn, coa, binding, binding->delay, 
		       binding->maxdelay, binding->flags,
		       binding->lifetime, binding->ops);

	write_unlock(&bul_lock);
	/* MAY also decide to delete binding and send zero lifetime BU
           with alt-coa set to home address */

	return 0;
}

/**
 * mipv6_handle_mh_ba - Binding Acknowledgement handler
 * @src: source of this packet
 * @coa: care-of address
 * @home: home address
 * @mh: pointer to the beginning of the Mobility Header
 *
 **/
static int mipv6_handle_mh_ba(struct in6_addr *home,
			      struct in6_addr *coa,
			      struct in6_addr *src,
			      struct in6_addr *unused,
			      struct mipv6_mh *mh)
{
	struct mipv6_mh_ba *ba = (struct mipv6_mh_ba *)mh->data;
	struct mipv6_bul_entry *binding = NULL;
	struct mobopt opts;
	int msg_len = (mh->length << 3)  + 2;
	int auth = 1, req_auth = 1, refresh = -1, ifindex = 0;
	u32 lifetime, sequence;

	if (msg_len < sizeof(*ba)) {
		DEBUG(DBG_WARNING, "Mobility Header length less than BA");
		MIPV6_INC_STATS(n_ba_drop.invalid);
		return -1;
	}

	lifetime = ntohs(ba->lifetime) << 2;
	sequence = ntohs(ba->sequence);

	if (msg_len > sizeof(*ba)) {
		memset(&opts, 0, sizeof(opts));
		if (parse_mo_tlv(ba + 1, msg_len - sizeof(*ba), &opts) < 0)
			return -1;
		/*
		 * MIPV6_OPT_AUTH_DATA, MIPV6_OPT_BR_ADVICE
		 */
		if (opts.br_advice)
			refresh = ntohs(opts.br_advice->refresh_interval);
	}

	if (ba->status >= EXPIRED_HOME_NONCE_INDEX && 
	    ba->status <= EXPIRED_NONCES) 
		req_auth = 0;
	
	write_lock(&bul_lock);
	binding = mipv6_bul_get(src, home);
	if (!binding) {
		DEBUG(DBG_INFO, "No binding, BA dropped.");
		write_unlock(&bul_lock);
		return -1;
	}

	if (opts.auth_data && binding->rr && (mipv6_auth_check(
		src, coa, (__u8 *)mh, msg_len, opts.auth_data, binding->rr->kbu) == 0))
		auth = 1;

	if (req_auth && binding->rr && !auth) {
		DEBUG(DBG_INFO, "BA Authentication failed.");
		MIPV6_INC_STATS(n_ba_drop.auth);
		write_unlock(&bul_lock);
		return MH_AUTH_FAILED;
	}

	if (ba->status == SEQUENCE_NUMBER_OUT_OF_WINDOW) {
		DEBUG(DBG_INFO,
		      "Sequence number out of window, setting seq to %d",
		      sequence);
		binding->seq = sequence + 1;
		MIPV6_INC_STATS(n_ban_rcvd);
		send_bu_msg(binding);
		
		write_unlock(&bul_lock);
		return 1;
	}

	if (binding->seq != sequence) {
		DEBUG(DBG_INFO, "BU/BA Sequence Number mismatch %d != %d",
		      binding->seq, sequence);
		MIPV6_INC_STATS(n_ba_drop.invalid);
		write_unlock(&bul_lock);
		return MH_SEQUENCE_MISMATCH;
	}
	if (ba->status == EXPIRED_HOME_NONCE_INDEX || ba->status == EXPIRED_NONCES) {
		if (binding->rr) {
			/* Need to resend home test init to CN */
			binding->rr->rr_state |= RR_WAITH;
			mipv6_send_addr_test_init(&binding->home_addr, 
						  &binding->cn_addr, 
						  MIPV6_MH_HOTI,
						  binding->rr->hot_cookie);
			MIPV6_INC_STATS(n_ban_rcvd);
		} else {
			DEBUG(DBG_WARNING, "Got BA with status EXPIRED_HOME_NONCE_INDEX"
			      "for non-RR BU");
			MIPV6_INC_STATS(n_ba_drop.invalid);
		}
		write_unlock(&bul_lock);
		return 0;
	} 
	if (ba->status == EXPIRED_CAREOF_NONCE_INDEX || ba->status == EXPIRED_NONCES) {
		if (binding->rr) { 
			/* Need to resend care-of test init to CN */
			binding->rr->rr_state |= RR_WAITC;
			mipv6_send_addr_test_init(&binding->coa, 
						  &binding->cn_addr, 
						  MIPV6_MH_COTI,
						  binding->rr->cot_cookie);
			MIPV6_INC_STATS(n_ban_rcvd);
		} else  {
			DEBUG(DBG_WARNING, "Got BA with status EXPIRED_HOME_CAREOF_INDEX"
			      "for non-RR BU");
			MIPV6_INC_STATS(n_ba_drop.invalid);
		}
		write_unlock(&bul_lock);
		return 0;
	}
	write_unlock(&bul_lock);
	
	if (ba->status >= REASON_UNSPECIFIED) {
		DEBUG(DBG_INFO, "Binding Ack status : %d indicates error", ba->status);
		mipv6_ba_rcvd(ifindex, src, home, ntohs(ba->sequence), lifetime,
			      refresh, ba->status);
		MIPV6_INC_STATS(n_ban_rcvd);
		return 0;
	}

	if (ba->status != 0) {
		/* Unknown BA status */
		MIPV6_INC_STATS(n_ba_drop.invalid);
		return 0;
	}
	
	MIPV6_INC_STATS(n_ba_rcvd);
	if (mipv6_ba_rcvd(ifindex, src, home, ntohs(ba->sequence), lifetime,
			  refresh, ba->status)) {
		DEBUG(DBG_WARNING, "mipv6_ba_rcvd failed");
	}
	
	return 0;
}

/**
 * mipv6_handle_mh_be - Binding Error handler
 * @cn: source of this packet
 * @coa: care-of address
 * @home: home address
 * @mh: pointer to the beginning of the Mobility Header
 *
 **/
static int mipv6_handle_mh_be(struct in6_addr *home,
			      struct in6_addr *coa,
			      struct in6_addr *cn,
			      struct in6_addr *unused,
			      struct mipv6_mh *mh)
{
	struct mipv6_mh_be *be = (struct mipv6_mh_be *)mh->data;
	int msg_len = (mh->length << 3)  + 2;

	if (msg_len < sizeof(*be)) {
		DEBUG(DBG_WARNING, "Mobility Header length less than BE");
		MIPV6_INC_STATS(n_be_drop.invalid);
		return -1;
	}

	/* check we know src, else drop */
	if (!mipv6_bul_exists(cn, home)) {
		MIPV6_INC_STATS(n_be_drop.misc);
		return MH_UNKNOWN_CN;
	}

	if (msg_len > sizeof(*be)) {
		/* no valid Mobility Options this time, just ignore */
	}

	MIPV6_INC_STATS(n_be_rcvd);
	switch (be->status) {
	case 1: /* Home Address Option used without a binding */
		/* Get ULP information about CN-MN communication.  If
                   nothing in progress, MUST delete.  Otherwise MAY
                   ignore. */
		mipv6_bul_delete(cn, home);
		break;
	case 2: /* Received unknown MH type */
		/* If not expecting ack, SHOULD ignore.  If MH
                   extension in use, stop it.  If not, stop RO for
                   this CN. */
		break;
	}

	return 0;
}

/*
 * mipv6_bu_rate_limit() : Takes a bulentry, a COA and 'flags' to check
 * whether BU being sent is for Home Registration or not.
 *
 * If the number of BU's sent is fewer than MAX_FAST_UPDATES, this BU
 * is allowed to be sent at the MAX_UPDATE_RATE.
 * If the number of BU's sent is greater than or equal to MAX_FAST_UPDATES,
 * this BU is allowed to be sent at the SLOW_UPDATE_RATE.
 *
 * Assumption : This function is not re-entrant. and the caller holds the
 * bulentry lock (by calling mipv6_bul_get()) to stop races with other
 * CPU's executing this same function.
 *
 * Side-Effects. Either of the following could  on success :
 *	1. Sets consecutive_sends to 1 if the entry is a Home agent
 *	   registration or the COA has changed.
 *	2. Increments consecutive_sends if the number of BU's sent so
 *	   far is less than MAX_FAST_UPDATES, and this BU is being sent
 *	   atleast MAX_UPDATE_RATE after previous one.
 * 
 * Return Value : 0 on Success, -1 on Failure
 */
static int mipv6_bu_rate_limit(struct mipv6_bul_entry *bulentry, 
			       struct in6_addr *coa, __u8 flags)
{
	if ((flags & MIPV6_BU_F_HOME) || ipv6_addr_cmp(&bulentry->coa, coa)) {
		/* Home Agent Registration or different COA - restart from 1 */
		bulentry->consecutive_sends = 1;
		return 0;
	}

	if (bulentry->consecutive_sends < MAX_FAST_UPDATES) {
		/* First MAX_FAST_UPDATES can be sent at MAX_UPDATE_RATE */
		if (jiffies - bulentry->lastsend < MAX_UPDATE_RATE * HZ) {
			return -1;
		}
		bulentry->consecutive_sends ++;
	} else {
		/* Remaining updates SHOULD be sent at SLOW_UPDATE_RATE */
		if (jiffies - bulentry->lastsend < SLOW_UPDATE_RATE * HZ) {
			return -1;
		}
		/* Don't inc 'consecutive_sends' to avoid overflow to zero */
	}
	/* OK to send a BU */
	return 0;
}

/**
 * mipv6_send_bu - send a Binding Update 
 * @saddr: source address for BU
 * @daddr: destination address for BU
 * @coa: care-of address for MN
 * @initdelay: initial BA wait timeout
 * @maxackdelay: maximum BA wait timeout
 * @exp: exponention back off
 * @flags: flags for BU
 * @lifetime: granted lifetime for binding
 * @ops: mobility options
 *
 * Send a binding update.  'flags' may contain any of %MIPV6_BU_F_ACK,
 * %MIPV6_BU_F_HOME, %MIPV6_BU_F_ROUTER bitwise ORed.  If
 * %MIPV6_BU_F_ACK is included retransmission will be attempted until
 * the update has been acknowledged.  Retransmission is done if no
 * acknowledgement is received within @initdelay seconds.  @exp
 * specifies whether to use exponential backoff (@exp != 0) or linear
 * backoff (@exp == 0).  For exponential backoff the time to wait for
 * an acknowledgement is doubled on each retransmission until a delay
 * of @maxackdelay, after which retransmission is no longer attempted.
 * For linear backoff the delay is kept constant and @maxackdelay
 * specifies the maximum number of retransmissions instead.  If
 * sub-options are present ops must contain all sub-options to be
 * added.  On a mobile node, use the mobile node's home address for
 * @saddr.  Returns 0 on success, non-zero on failure.
 *
 * Caller may not hold @bul_lock.
 **/
int mipv6_send_bu(struct in6_addr *saddr, struct in6_addr *daddr,
		  struct in6_addr *coa, u32 initdelay, 
		  u32 maxackdelay, u8 exp, u8 flags, u32 lifetime,
		  struct mipv6_mh_opt *ops)
{
	int ret;
	__u8 state;
	 __u16 seq = 0;
	int (*callback)(struct mipv6_bul_entry *);
	__u32 callback_time;
	struct mipv6_bul_entry *bulentry;
	
	/* First a sanity check: don't send BU to local addresses */
	if(ipv6_chk_addr(daddr, NULL, 0)) {
		DEBUG(DBG_ERROR, "BUG: Trying to send BU to local address");
		return -1;
	}
	DEBUG(DBG_INFO, "Sending BU to CN  %x:%x:%x:%x:%x:%x:%x:%x "
	      "for home address %x:%x:%x:%x:%x:%x:%x:%x", 
	      NIPV6ADDR(daddr), NIPV6ADDR(saddr));

	if ((bulentry = mipv6_bul_get(daddr, saddr)) != NULL) {
		if (bulentry->state == ACK_ERROR) {
			/*
			 * Don't send any more BU's to nodes which don't
			 * understanding one. 
			 */
			DEBUG(DBG_INFO, "Not sending BU to node which doesn't"
			      " understand one");
			return -1;
		}
		if (mipv6_bu_rate_limit(bulentry, coa, flags) < 0) {
			DEBUG(DBG_DATADUMP, "Limiting BU sent.");
			return 0;
		}
	}

	switch (mipv6_rr_state(bulentry, saddr, coa, flags)) {
	case INPROGRESS_RR:
		/* We are already doing RR, don't do BU at this time, it is
		 * done automatically later */
		DEBUG(DBG_INFO, "RR in progress not sending BU");
		return 0;

	case DO_RR:
		/* Just do RR and return, BU is done automatically later */
		DEBUG(DBG_INFO, "starting RR" );
		mipv6_RR_start(saddr, daddr, coa, bulentry, initdelay,
			       maxackdelay, flags, lifetime, ops);
		return 0;
		
	case NO_RR:
		DEBUG(DBG_DATADUMP, "No RR necessary" );
	default:
		break;
	}

	if (bulentry)
		seq = bulentry->seq + 1;
	
	/* Add to binding update list */
	
	if (flags & MIPV6_BU_F_ACK) {
		DEBUG(DBG_INFO, "Setting bul callback to bul_resend_exp");
		/* Send using exponential backoff */
		state = RESEND_EXP;
		callback = bul_resend_exp;
		callback_time = initdelay;
	} else {
		DEBUG(DBG_INFO, "Setting bul callback to bul_expired");
		/* No acknowledgement/resending required */
		state = ACK_OK;	/* pretend we got an ack */
		callback = bul_expired;
		callback_time = lifetime;
	}

	/* BU only for the home address */
	/* We must hold bul_lock (write) while calling add */
	if ((bulentry = mipv6_bul_add(daddr, saddr, coa, lifetime, seq,
				      flags, callback, callback_time, 
				      state, initdelay, maxackdelay, ops, 
				      NULL)) == NULL) {
		DEBUG(DBG_INFO, "couldn't update BUL");
		return 0;
	}
	ret = send_bu_msg(bulentry);

	return ret;
}

int __init mipv6_mh_mn_init(void)
{
	mipv6_mh_register(MIPV6_MH_HOT, mipv6_handle_mh_HC_test);
	mipv6_mh_register(MIPV6_MH_COT, mipv6_handle_mh_HC_test);
	mipv6_mh_register(MIPV6_MH_BA, mipv6_handle_mh_ba);
	mipv6_mh_register(MIPV6_MH_BRR, mipv6_handle_mh_brr);
	mipv6_mh_register(MIPV6_MH_BE, mipv6_handle_mh_be);

	return 0;
}

void __exit mipv6_mh_mn_exit(void)
{
	mipv6_mh_unregister(MIPV6_MH_HOT);
	mipv6_mh_unregister(MIPV6_MH_COT);
	mipv6_mh_unregister(MIPV6_MH_BA);
	mipv6_mh_unregister(MIPV6_MH_BRR);
	mipv6_mh_unregister(MIPV6_MH_BE);
}
