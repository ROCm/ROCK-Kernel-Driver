/* Linux ISDN subsystem, PPP CCP support
 *
 * Copyright 1994-1998  by Fritz Elfert (fritz@isdn4linux.de)
 *           1995,96    by Thinking Objects Software GmbH Wuerzburg
 *           1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *           1999-2002  by Kai Germaschewski <kai@germaschewski.name>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include "isdn_ppp_ccp.h"
#include "isdn_common.h"
#include "isdn_net_lib.h"
#include "isdn_ppp.h"
#include <linux/ppp-comp.h>

/* ====================================================================== */                                                                       
enum ippp_ccp_reset_states {
	CCPResetIdle,
	CCPResetSentReq,
	CCPResetRcvdReq,
	CCPResetSentAck,
	CCPResetRcvdAck
};

struct ippp_ccp_reset_state {
	enum ippp_ccp_reset_states state;/* State of this transaction */
	struct ippp_ccp *ccp;            /* Backlink */
	unsigned char id;		 /* id index */
	unsigned char ta:1;		 /* The timer is active (flag) */
	unsigned char expra:1;		 /* We expect a ResetAck at all */
	int dlen;			 /* Databytes stored in data */
	struct timer_list timer;	 /* For timeouts/retries */
	/* This is a hack but seems sufficient for the moment. We do not want
	   to have this be yet another allocation for some bytes, it is more
	   memory management overhead than the whole mess is worth. */
	unsigned char data[IPPP_RESET_MAXDATABYTES];
};

/* The data structure keeping track of the currently outstanding CCP Reset
   transactions. */
struct ippp_ccp_reset {
	struct ippp_ccp_reset_state *rs[256];	/* One per possible id */
	unsigned char lastid;			/* Last id allocated */
};

/* In-kernel handling of CCP Reset-Request and Reset-Ack is necessary,
   but absolutely nontrivial. The most abstruse problem we are facing is
   that the generation, reception and all the handling of timeouts and
   resends including proper request id management should be entirely left
   to the (de)compressor, but indeed is not covered by the current API to
   the (de)compressor. The API is a prototype version from PPP where only
   some (de)compressors have yet been implemented and all of them are
   rather simple in their reset handling. Especially, their is only one
   outstanding ResetAck at a time with all of them and ResetReq/-Acks do
   not have parameters. For this very special case it was sufficient to
   just return an error code from the decompressor and have a single
   reset() entry to communicate all the necessary information between
   the framework and the (de)compressor. Bad enough, LZS is different
   (and any other compressor may be different, too). It has multiple
   histories (eventually) and needs to Reset each of them independently
   and thus uses multiple outstanding Acks and history numbers as an
   additional parameter to Reqs/Acks.
   All that makes it harder to port the reset state engine into the
   kernel because it is not just the same simple one as in (i)pppd but
   it must be able to pass additional parameters and have multiple out-
   standing Acks. We are trying to achieve the impossible by handling
   reset transactions independent by their id. The id MUST change when
   the data portion changes, thus any (de)compressor who uses more than
   one resettable state must provide and recognize individual ids for
   each individual reset transaction. The framework itself does _only_
   differentiate them by id, because it has no other semantics like the
   (de)compressor might.
   This looks like a major redesign of the interface would be nice,
   but I don't have an idea how to do it better. */

/* ====================================================================== */

/* Free a given state and clear everything up for later reallocation */
static void
ippp_ccp_reset_free_state(struct ippp_ccp *ccp, unsigned char id)
{
	struct ippp_ccp_reset_state *rs	= ccp->reset->rs[id];

	if (!rs)
		return;
	
	if (rs->ta) // FIXME?
		del_timer_sync(&rs->timer);

	kfree(rs);
	ccp->reset->rs[id] = NULL;
}

static void
do_xmit_reset(struct ippp_ccp *ccp, unsigned char code, unsigned char id,
	      unsigned char *data, int len)
{
	struct sk_buff *skb;
	unsigned char *p;
	u16 proto = ccp->proto == PPP_COMP ? PPP_CCP : PPP_CCPFRAG;

	skb = ccp->alloc_skb(ccp->priv, 4 + len, GFP_ATOMIC);

	p = skb_put(skb, 4);
	p += put_u8 (p, code);
	p += put_u8 (p, id);
	p += put_u16(p, len + 4);

	if (len)
		memcpy(skb_put(skb, len), data, len);

	isdn_ppp_frame_log("ccp-xmit", skb->data, skb->len, 32, -1, -1);

	ccp->xmit(ccp->priv, skb, proto);
}

/* The timer callback function which is called when a ResetReq has timed out,
   aka has never been answered by a ResetAck */
static void
isdn_ppp_ccp_timer_callback(unsigned long data)
{
	struct ippp_ccp_reset_state *rs = (struct ippp_ccp_reset_state *) data;

	if (!rs->ta) {
		isdn_BUG();
		return;
	}
	if (rs->state != CCPResetSentReq) {
		printk(KERN_WARNING "ippp_ccp: timer cb in wrong state %d\n",
		       rs->state);
		rs->ta = 0;
		return;
	}
	/* We are correct here */
	if (!rs->expra) {
		/* Hmm, there is no Ack really expected. We can clean
		   up the state now, it will be reallocated if the
		   decompressor insists on another reset */
		rs->ta = 0;
		ippp_ccp_reset_free_state(rs->ccp, rs->id);
		return;
	}
	printk(KERN_DEBUG "ippp_ccp: CCP Reset timed out for id %d\n",
	       rs->id);
	/* Push it again */
	do_xmit_reset(rs->ccp, CCP_RESETREQ, rs->id, rs->data, rs->dlen);

	mod_timer(&rs->timer, jiffies + 5 * HZ);
}

/* Allocate a new reset transaction state */
static struct ippp_ccp_reset_state *
ippp_ccp_reset_alloc_state(struct ippp_ccp *ccp, unsigned char id)
{
	struct ippp_ccp_reset_state *rs;

	rs = kmalloc(sizeof(struct ippp_ccp_reset_state), GFP_KERNEL);
	if(!rs)
		return NULL;
	memset(rs, 0, sizeof(struct ippp_ccp_reset_state));
	rs->state = CCPResetIdle;
	rs->ccp = ccp;
	rs->id = id;
	init_timer(&rs->timer);
	rs->timer.data = (unsigned long)rs;
	rs->timer.function = isdn_ppp_ccp_timer_callback;

	ccp->reset->rs[id] = rs;
	return rs;
}

/* A decompressor wants a reset with a set of parameters - do what is
   necessary to fulfill it */
static void
ippp_ccp_reset_xmit(struct ippp_ccp *ccp,
		    struct isdn_ppp_resetparams *rp)
{
	struct ippp_ccp_reset_state *rs;
	int id;

	if (rp->valid) {
		/* The decompressor defines parameters by itself */
		if (!rp->rsend)
			return;

		/* And it wants us to send a request */
		if (!rp->idval) {
			isdn_BUG();
			return;
		}
		id = rp->id;
	} else {
		/* The reset params are invalid. The decompressor does not
		   care about them, so we just send the minimal requests
		   and increase ids only when an Ack is received for a
		   given id */
		id = ccp->reset->lastid++;
		/* We always expect an Ack if the decompressor doesn't
		   know	better */
		rp->expra = 1;
		rp->dtval = 0;
	}
	rs = ccp->reset->rs[id];
	if (rs) {
		printk(KERN_INFO "ippp_ccp: reset xmit in wrong state %d "
		       "for id %d (%d)\n", rs->state, id, rs->ta);
		return;
	}
	/* Ok, this is a new transaction */
	printk(KERN_DEBUG "ippp_ccp: new xmit for id %d\n", id);
	rs = ippp_ccp_reset_alloc_state(ccp, id);
	if(!rs) {
		printk(KERN_INFO "ippp_ccp: out of mem allocing ccp trans\n");
		return;
	}
	rs->expra = rp->expra;
	rs->id = id;
	if (rp->dtval) {
		rs->dlen = rp->dlen;
		memcpy(rs->data, rp->data, rp->dlen);
	} else {
		rs->dlen = 0;
	}

	rs->state = CCPResetSentReq;
	do_xmit_reset(rs->ccp, CCP_RESETREQ, rs->id, rs->data, rs->dlen);

	/* Start the timer */
	rs->timer.expires = jiffies + 5*HZ;
	add_timer(&rs->timer);
	rs->ta = 1;
}

/* ====================================================================== */

struct ippp_ccp *
ippp_ccp_alloc(void)
{
	struct ippp_ccp *ccp;

	ccp = kmalloc(sizeof(*ccp), GFP_ATOMIC); // FIXME
	if (!ccp)
		return NULL;
	memset(ccp, 0, sizeof(*ccp));
	ccp->mru = 1524;      /* MRU, default 1524 */
	ccp->reset = kmalloc(sizeof(*ccp->reset), GFP_ATOMIC); // FIXME alloc together?
	if (!ccp->reset) {
		kfree(ccp);
		return NULL;
	}
	memset(ccp->reset, 0, sizeof(*ccp->reset));
	return ccp;
}

void
ippp_ccp_free(struct ippp_ccp *ccp)
{
	int id;

	if (ccp->comp_stat) {
		ccp->compressor->free(ccp->comp_stat);
		module_put(ccp->compressor->owner);
	}
	if (ccp->decomp_stat) {
		ccp->decompressor->free(ccp->decomp_stat);
		module_put(ccp->decompressor->owner);
	}
	for (id = 0; id < 256; id++) {
		if (ccp->reset->rs[id])
			ippp_ccp_reset_free_state(ccp, id);
	}
	kfree(ccp->reset);
	kfree(ccp);
}

int
ippp_ccp_set_mru(struct ippp_ccp *ccp, unsigned int mru)
{
	ccp->mru = mru;
	return 0;
}

unsigned int
ippp_ccp_get_flags(struct ippp_ccp *ccp)
{
	return ccp->compflags & (SC_DC_ERROR|SC_DC_FERROR);
}

/*
 * compress a frame 
 * returns original skb if we did not compress the frame
 * and a new skb otherwise
 */
struct sk_buff *
ippp_ccp_compress(struct ippp_ccp *ccp, struct sk_buff *skb_in, u16 *proto)
{
	struct sk_buff *skb;

	if (!(ccp->compflags & (SC_COMP_ON|SC_DECOMP_ON))) {
		/* We send compressed only if both down- und upstream
		   compression is negotiated, that means, CCP is up */
		return skb_in;
	}
	/* we do not compress control protocols */
	if (*proto > 0x3fff) {
		return skb_in;
	}
	if (!ccp->compressor || !ccp->comp_stat) {
		isdn_BUG();
		return skb_in;
	}
	/* Allow for at least 150 % expansion (for now) */
	skb = alloc_skb(skb_in->len*2 + skb_headroom(skb_in), GFP_ATOMIC);
	if (!skb)
		return skb_in;

	skb_reserve(skb, skb_headroom(skb_in));
	if (!ccp->compressor->compress(ccp->comp_stat, skb_in, skb, *proto)) {
		dev_kfree_skb(skb);
		return skb_in;
	}
	isdn_ppp_frame_log("comp  in:", skb_in->data, skb_in->len, 20, -1, -1);
	isdn_ppp_frame_log("comp out:", skb->data, skb->len, 20, -1, -1);
	dev_kfree_skb(skb_in);
	*proto = ccp->proto;
	return skb;
}

/* 
 * decompress packet
 *
 * proto is updated to protocol field of uncompressed packet.
 * retval: decompressed packet,
 *         same packet if uncompressed,
 *	   NULL if decompression error
 */

struct sk_buff *
ippp_ccp_decompress(struct ippp_ccp *ccp, struct sk_buff *skb_in, u16 *proto)
{
	struct sk_buff *skb;
	struct isdn_ppp_resetparams rsparm;
	unsigned char rsdata[IPPP_RESET_MAXDATABYTES];
	int len;

	if (!(ccp->compflags & SC_DECOMP_ON)) {
		return skb_in;
	}
	if (!ccp->decompressor || !ccp->decomp_stat) {
		isdn_BUG();
		return skb_in;
	}
	if (*proto != ccp->proto) {
		/* uncompressed packets are fed through the decompressor to
		 * update the decompressor state */
		ccp->decompressor->incomp(ccp->decomp_stat, skb_in, *proto);
		return skb_in;
	}
	skb = dev_alloc_skb(ccp->mru + PPP_HDRLEN); // FIXME oom?

	// Set up reset params for the decompressor
	memset(&rsparm, 0, sizeof(rsparm));
	rsparm.data = rsdata;
	rsparm.maxdlen = IPPP_RESET_MAXDATABYTES;

	len = ccp->decompressor->decompress(ccp->decomp_stat, skb_in, skb,
					    &rsparm);
	isdn_ppp_frame_log("deco  in:", skb_in->data, skb_in->len, 20, -1, -1);
	isdn_ppp_frame_log("deco out:", skb->data, skb->len, 20, -1, -1);
	kfree_skb(skb_in);

	if (len <= 0) {
		switch(len) {
		case DECOMP_ERROR:
			printk(KERN_INFO "ippp: decomp wants reset with%s params\n",
			       rsparm.valid ? "" : "out");
			
			ippp_ccp_reset_xmit(ccp, &rsparm);
			break;
		case DECOMP_FATALERROR:
			ccp->compflags |= SC_DC_FERROR;
			/* Kick ipppd to recognize the error */
			ccp->kick_up(ccp->priv);
			break;
		}
		kfree_skb(skb);
		return NULL;
	}
	if (isdn_ppp_strip_proto(skb, proto)) {
		kfree_skb(skb);
		return NULL;
	}
	return skb;
}

/* An Ack was received for this id. This means we stop the timer and clean
   up the state prior to calling the decompressors reset routine. */
static void
isdn_ppp_ccp_reset_ack_rcvd(struct ippp_ccp *ccp, unsigned char id)
{
	struct ippp_ccp_reset_state *rs = ccp->reset->rs[id];

	if (!rs) {
		printk(KERN_INFO "ippp_ccp: ResetAck received for unknown id"
		       " %d\n", id);
		return;
	}

	if (rs->ta && rs->state == CCPResetSentReq) {
		/* Great, we are correct */
		if(!rs->expra)
			printk(KERN_DEBUG "ippp_ccp: ResetAck received"
			       " for id %d but not expected\n", id);
	} else {
		printk(KERN_INFO "ippp_ccp: ResetAck received out of"
		       "sync for id %d\n", id);
	}
	if(rs->ta) {
		rs->ta = 0;
		del_timer(&rs->timer);
	}
	ippp_ccp_reset_free_state(ccp, id);
}

void
ippp_ccp_receive_ccp(struct ippp_ccp *ccp, struct sk_buff *skb)
{
	int len;
	struct isdn_ppp_resetparams rsparm;
	unsigned char rsdata[IPPP_RESET_MAXDATABYTES];	

	isdn_ppp_frame_log("ccp-recv", skb->data, skb->len, 32, -1, -1);

	switch(skb->data[0]) {
	case CCP_CONFREQ:
		if (ccp->debug & 0x10)
			printk(KERN_DEBUG "Disable compression here!\n");

		ccp->compflags &= ~SC_COMP_ON;		
		break;
	case CCP_TERMREQ:
	case CCP_TERMACK:
		if (ccp->debug & 0x10)
			printk(KERN_DEBUG "Disable (de)compression here!\n");

		ccp->compflags &= ~(SC_DECOMP_ON|SC_COMP_ON);		
		break;
	case CCP_CONFACK:
		/* if we RECEIVE an ackowledge we enable the decompressor */
		if (ccp->debug & 0x10)
			printk(KERN_DEBUG "Enable decompression here!\n");

		if (!ccp->decomp_stat)
			break;
		ccp->compflags |= SC_DECOMP_ON;
		break;
	case CCP_RESETACK:
		printk(KERN_DEBUG "Received ResetAck from peer\n");
		len = (skb->data[2] << 8) | skb->data[3];
		len -= 4;

		/* If a reset Ack was outstanding for this id, then
		   clean up the state engine */
		isdn_ppp_ccp_reset_ack_rcvd(ccp, skb->data[1]);
		if (ccp->decomp_stat)
			ccp->decompressor->reset(ccp->decomp_stat,
						 skb->data[0], skb->data[1],
						 len ? &skb->data[4] : NULL,
						 len, NULL);
		/* TODO: This is not easy to decide here */
		ccp->compflags &= ~SC_DECOMP_DISCARD;
		break;
	case CCP_RESETREQ:
		printk(KERN_DEBUG "Received ResetReq from peer\n");
		/* Receiving a ResetReq means we must reset our compressor */
		/* Set up reset params for the reset entry */
		memset(&rsparm, 0, sizeof(rsparm));
		rsparm.data = rsdata;
		rsparm.maxdlen = IPPP_RESET_MAXDATABYTES; 
		/* Isolate data length */
		len = (skb->data[2] << 8) | skb->data[3];
		len -= 4;
		if (ccp->comp_stat)
			ccp->compressor->reset(ccp->comp_stat,
					       skb->data[0], skb->data[1],
					       len ? &skb->data[4] : NULL,
					       len, &rsparm);
		/* Ack the Req as specified by rsparm */
		if (rsparm.valid) {
			/* Compressor reset handler decided how to answer */
			if (!rsparm.rsend) {
				printk(KERN_DEBUG "ResetAck suppressed\n");
				return;
			}
			/* We should send a Frame */
			do_xmit_reset(ccp, CCP_RESETACK,
				      rsparm.idval ? rsparm.id : skb->data[1],
				      rsparm.data,
				      rsparm.dtval ? rsparm.dlen : 0);
			return;
		}
		/* We answer with a straight reflected Ack */
		do_xmit_reset(ccp, CCP_RESETACK, skb->data[1], 
			      skb->data + 4, len);
	}
}

void
ippp_ccp_send_ccp(struct ippp_ccp *ccp, struct sk_buff *skb)
{
	isdn_ppp_frame_log("ccp-xmit", skb->data, skb->len, 32, -1, -1);

        switch (skb->data[2]) {
	case CCP_CONFREQ:
		if (ccp->debug & 0x10)
			printk(KERN_DEBUG "Disable decompression here!\n");

		ccp->compflags &= ~SC_DECOMP_ON;
		break;
	case CCP_TERMREQ:
	case CCP_TERMACK:
		if (ccp->debug & 0x10)
			printk(KERN_DEBUG "Disable (de)compression here!\n");

		ccp->compflags &= ~(SC_DECOMP_ON|SC_COMP_ON);
		break;
	case CCP_CONFACK:
		/* if we SEND an ackowledge we can/must enable the compressor */
		if (ccp->debug & 0x10)
			printk(KERN_DEBUG "Enable compression here!\n");

		if (!ccp->compressor)
			break;

		ccp->compflags |= SC_COMP_ON;
		break;
	case CCP_RESETACK:
		/* If we send a ACK we should reset our compressor */
		if (ccp->debug & 0x10)
			printk(KERN_DEBUG "Reset decompression state here!\n");

		printk(KERN_DEBUG "ResetAck from daemon passed by\n");

		if (!ccp->comp_stat)
			break;

		ccp->compressor->reset(ccp->comp_stat, 0, 0, NULL, 0, NULL);
		ccp->compflags &= ~SC_COMP_DISCARD;	
		break;
	case CCP_RESETREQ:
		/* Just let it pass by */
		printk(KERN_DEBUG "ResetReq from daemon passed by\n");
		break;
	}
}

static LIST_HEAD(ipc_head);
static spinlock_t ipc_head_lock;

int
ippp_ccp_set_compressor(struct ippp_ccp *ccp, int unit,
			struct isdn_ppp_comp_data *data)
{
	struct isdn_ppp_compressor *ipc;
	int ret;
	void *stat;
	int num = data->num;

	if (ccp->debug & 0x10)
		printk(KERN_DEBUG "[%d] Set %scompressor type %d\n", unit,
		       data->flags & IPPP_COMP_FLAG_XMIT ? "" : "de", num);

	spin_lock(&ipc_head_lock);
	list_for_each_entry(ipc, &ipc_head, list) {
		if (ipc->num == num &&
		    try_module_get(ipc->owner))
			goto found;
	}
	spin_unlock(&ipc_head_lock);
	return -EINVAL;

 found:
	spin_unlock(&ipc_head_lock);

	stat = ipc->alloc(data);
	if (!stat) {
		printk(KERN_ERR "Can't alloc (de)compression!\n");
		goto err;
	}
	ret = ipc->init(stat, data, unit, 0);
	if(!ret) {
		printk(KERN_ERR "Can't init (de)compression!\n");
		ipc->free(stat);
		goto err;
	}
	if (data->flags & IPPP_COMP_FLAG_XMIT) {
		if (ccp->comp_stat) {
			ccp->compressor->free(ccp->comp_stat);
			module_put(ccp->compressor->owner);
		}
			ccp->comp_stat = stat;
			ccp->compressor = ipc;
	} else {
		if (ccp->decomp_stat) {
			ccp->decompressor->free(ccp->decomp_stat);
			module_put(ccp->decompressor->owner);
		}
		ccp->decomp_stat = stat;
		ccp->decompressor = ipc;
	}
	return 0;

 err:
	module_put(ipc->owner);
	return -EINVAL;
}

void
ippp_ccp_get_compressors(unsigned long protos[8])
{
	struct isdn_ppp_compressor *ipc;
	int i, j;

	memset(protos, 0, sizeof(unsigned long) * 8);

	spin_lock(&ipc_head_lock);
	list_for_each_entry(ipc, &ipc_head, list) {
		j = ipc->num / (sizeof(long)*8);
		i = ipc->num % (sizeof(long)*8);
		if (j < 8)
			protos[j] |= 1 << i;
	}
	spin_unlock(&ipc_head_lock);
}

int
isdn_ppp_register_compressor(struct isdn_ppp_compressor *ipc)
{
	spin_lock(&ipc_head_lock);
	list_add_tail(&ipc->list, &ipc_head);
	spin_unlock(&ipc_head_lock);

	return 0;
}

int
isdn_ppp_unregister_compressor(struct isdn_ppp_compressor *ipc)
{
	spin_lock(&ipc_head_lock);
	list_del(&ipc->list);
	spin_unlock(&ipc_head_lock);

	return 0;
}

