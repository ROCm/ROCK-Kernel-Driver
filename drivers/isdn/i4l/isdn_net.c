/* $Id: isdn_net.c,v 1.140.6.11 2001/11/06 20:58:28 kai Exp $
 *
 * Linux ISDN subsystem, network interfaces and related functions (linklevel).
 *
 * Copyright 1994-1998  by Fritz Elfert (fritz@isdn4linux.de)
 *           1995,96    by Thinking Objects Software GmbH Wuerzburg
 *           1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *           1999-2002  by Kai Germaschewski <kai@germaschewski.name>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Data Over Voice (DOV) support added - Guy Ellis 23-Mar-02 
 *                                       guy@traverse.com.au
 * Outgoing calls - looks for a 'V' in first char of dialed number
 * Incoming calls - checks first character of eaz as follows:
 *   Numeric - accept DATA only - original functionality
 *   'V'     - accept VOICE (DOV) only
 *   'B'     - accept BOTH DATA and DOV types
 *
 */

#include <linux/config.h>
#include <linux/isdn.h>
#include <net/arp.h>
#include <net/dst.h>
#include <net/pkt_sched.h>
#include <linux/inetdevice.h>
#include "isdn_common.h"
#include "isdn_net.h"
#include "isdn_ppp.h"
#include <linux/concap.h>
#include "isdn_concap.h"
#include "isdn_ciscohdlck.h"

enum {
	ST_NULL,
	ST_OUT_WAIT_DCONN,
	ST_OUT_WAIT_BCONN,
	ST_IN_WAIT_DCONN,
	ST_IN_WAIT_BCONN,
	ST_ACTIVE,
	ST_WAIT_BEFORE_CB,
	ST_OUT_DIAL_WAIT,
};

/* keep clear of ISDN_CMD_* and ISDN_STAT_* */
enum {
	EV_NET_DIAL            = 0x200,
	EV_NET_TIMER_INCOMING  = 0x201,
	EV_NET_TIMER_DIAL      = 0x203,
	EV_NET_TIMER_DIAL_WAIT = 0x204,
	EV_NET_TIMER_CB_OUT    = 0x205,
	EV_NET_TIMER_CB_IN     = 0x206,
	EV_NET_TIMER_HUP       = 0x207,
};

/*
 * Outline of new tbusy handling: 
 *
 * Old method, roughly spoken, consisted of setting tbusy when entering
 * isdn_net_start_xmit() and at several other locations and clearing
 * it from isdn_net_start_xmit() thread when sending was successful.
 *
 * With 2.3.x multithreaded network core, to prevent problems, tbusy should
 * only be set by the isdn_net_start_xmit() thread and only when a tx-busy
 * condition is detected. Other threads (in particular isdn_net_stat_callb())
 * are only allowed to clear tbusy.
 *
 * -HE
 */

/*
 * About SOFTNET:
 * Most of the changes were pretty obvious and basically done by HE already.
 *
 * One problem of the isdn net device code is that is uses struct net_device
 * for masters and slaves. However, only master interface are registered to 
 * the network layer, and therefore, it only makes sense to call netif_* 
 * functions on them.
 *
 * --KG
 */

/* 
 * Find out if the netdevice has been ifup-ed yet.
 */
static inline int
isdn_net_device_started(isdn_net_dev *idev)
{
	return netif_running(&idev->mlp->dev);
}

/*
 * stop the network -> net_device queue.
 */
static inline void
isdn_net_dev_stop_queue(isdn_net_dev *idev)
{
	netif_stop_queue(&idev->mlp->dev);
}

/*
 * find out if the net_device which this lp belongs to (lp can be
 * master or slave) is busy. It's busy iff all (master and slave) 
 * queues are busy
 */
static inline int
isdn_net_device_busy(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;
	unsigned long flags;
	int retval = 1;

	if (!isdn_net_dev_busy(idev))
		return 0;

	spin_lock_irqsave(&mlp->online_lock, flags);
	list_for_each_entry(idev, &mlp->online, online) {
		if (!isdn_net_dev_busy(idev)) {
			retval = 0;
			break;
		}
	}
	spin_unlock_irqrestore(&mlp->online_lock, flags);
	return retval;
}

static inline
void isdn_net_inc_frame_cnt(isdn_net_dev *idev)
{
	atomic_inc(&idev->frame_cnt);
	if (isdn_net_device_busy(idev))
		isdn_net_dev_stop_queue(idev);
}

static inline void
isdn_net_dec_frame_cnt(isdn_net_dev *idev)
{
	atomic_dec(&idev->frame_cnt);

	if (!isdn_net_device_busy(idev)) {
		if (!skb_queue_empty(&idev->super_tx_queue))
			tasklet_schedule(&idev->tlet);
		else
			isdn_net_dev_wake_queue(idev);
       }                                                                      
}

static inline
void isdn_net_zero_frame_cnt(isdn_net_dev *idev)
{
	atomic_set(&idev->frame_cnt, 0);
}

/* For 2.2.x we leave the transmitter busy timeout at 2 secs, just 
 * to be safe.
 * For 2.3.x we push it up to 20 secs, because call establishment
 * (in particular callback) may take such a long time, and we 
 * don't want confusing messages in the log. However, there is a slight
 * possibility that this large timeout will break other things like MPPP,
 * which might rely on the tx timeout. If so, we'll find out this way...
 */

int isdn_net_online(isdn_net_dev *idev)
{
	return idev->dialstate == ST_ACTIVE;
}

/* Prototypes */

static int do_dialout(isdn_net_dev *idev);
int isdn_net_handle_event(isdn_net_dev *idev, int pr, void *arg);

char *isdn_net_revision = "$Revision: 1.140.6.11 $";

 /*
  * Code for raw-networking over ISDN
  */

static void
isdn_net_unreachable(struct net_device *dev, struct sk_buff *skb, char *reason)
{
	u_short proto = ntohs(skb->protocol);
	
	printk(KERN_DEBUG "isdn_net: %s: %s, signalling dst_link_failure %s\n",
	       dev->name,
	       (reason != NULL) ? reason : "unknown",
	       (proto != ETH_P_IP) ? "Protocol != ETH_P_IP" : "");
	
	dst_link_failure(skb);
}

/*
 * unbind a net-interface (resets interface after an error)
 */
static void
isdn_net_unbind_channel(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;
	ulong flags;

	save_flags(flags);
	cli();

	if (idev->isdn_slot < 0) {
		isdn_BUG();
		return;
	}

	if (mlp->ops->unbind)
		mlp->ops->unbind(idev);

	skb_queue_purge(&idev->super_tx_queue);

	idev->dialstate = ST_NULL;

	isdn_slot_set_idev(idev->isdn_slot, NULL);
	isdn_slot_free(idev->isdn_slot, ISDN_USAGE_NET);

	idev->isdn_slot = -1;

	restore_flags(flags);
}

/*
 * Assign an ISDN-channel to a net-interface
 */
static int
isdn_net_bind_channel(isdn_net_dev *idev, int idx)
{
	isdn_net_local *mlp = idev->mlp;
	int retval = 0;
	unsigned long flags;

	save_flags(flags);
	cli();

	idev->isdn_slot = idx;
	isdn_slot_set_idev(idev->isdn_slot, idev);

	if (mlp->ops->bind)
		retval = mlp->ops->bind(idev);

	if (retval < 0)
		isdn_net_unbind_channel(idev);

	restore_flags(flags);
	return retval;
}

/*
 * Handle status-messages from ISDN-interfacecard.
 * This function is called from within the main-status-dispatcher
 * isdn_status_callback, which itself is called from the low-level driver.
 * Return: 1 = Event handled, 0 = not for us or unknown Event.
 */
int
isdn_net_stat_callback(int idx, isdn_ctrl *c)
{
	isdn_net_dev *idev = isdn_slot_idev(idx);

	if (!idev) {
		HERE;
		return 0;
	}

	return isdn_net_handle_event(idev, c->command, c);
}

/* ---------------------------------------------------------------------- */
/* call management                                                        */
/* ---------------------------------------------------------------------- */

/* Find the idev->dial'th outgoing number. */

static struct isdn_net_phone *
get_outgoing_phone(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;
	struct isdn_net_phone *phone;
	int i = 0;

	list_for_each_entry(phone, &mlp->phone[1], list) {
		if (i++ == idev->dial)
			return phone;
	}
	return NULL;
}

/* Initiate dialout. */

static int
init_dialout(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;

	if (ISDN_NET_DIALMODE(*mlp) == ISDN_NET_DM_OFF)
		return 1;
	
	if (list_empty(&mlp->phone[1]))
		return 1;

	idev->dial = 0;
	idev->dialretry = 0;
	return do_dialout(idev);
}

/* Try dialing the next number. */

static int
do_dialout(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;
	struct dial_info dial = {
		.l2_proto = mlp->l2_proto,
		.l3_proto = mlp->l3_proto,
		.si1      = 7,
		.si2      = 0,
		.msn      = mlp->msn,
		.phone    = get_outgoing_phone(idev)->num,
	};

	/* next time, try next number */
	idev->dial++;

	idev->outgoing = 1;
	if (idev->chargeint)
		idev->charge_state = ST_CHARGE_HAVE_CINT;
	else
		idev->charge_state = ST_CHARGE_NULL;

	/* For outgoing callback, use cbdelay instead of dialtimeout */
	if (mlp->cbdelay && (mlp->flags & ISDN_NET_CBOUT)) {
		idev->dial_timer.expires = jiffies + mlp->cbdelay;
		idev->dial_event = EV_NET_TIMER_CB_OUT;
	} else {
		idev->dial_timer.expires = jiffies + mlp->dialtimeout;
		idev->dial_event = EV_NET_TIMER_DIAL;
	}
	idev->dialstate = ST_OUT_WAIT_DCONN;
	add_timer(&idev->dial_timer);

	/* Dial */
	isdn_slot_dial(idev->isdn_slot, &dial);
	return 1;
}

/* If we didn't connect within dialtimeout, we give up for now
 * and wait for dialwait jiffies before trying again.
 */
static int
dial_timeout(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;
	isdn_ctrl cmd;

	idev->dialstate = ST_OUT_DIAL_WAIT;
	isdn_slot_command(idev->isdn_slot, ISDN_CMD_HANGUP, &cmd);
	
	/* get next phone number */
	if (!get_outgoing_phone(idev)) {
		/* otherwise start over at first entry */
		idev->dial = 0;
		idev->dialretry++;
	}
	if (idev->dialretry >= mlp->dialmax) {
		isdn_net_hangup(idev);
		return 1;
	}
	idev->dial_event = EV_NET_TIMER_DIAL_WAIT;
	mod_timer(&idev->dial_timer, jiffies + mlp->dialwait);
	return 1;
}

static int
isdn_net_connect_failure(isdn_net_dev *idev)
{
	del_timer(&idev->dial_timer);
	isdn_slot_all_eaz(idev->isdn_slot);
	printk(KERN_INFO "%s: connection failed\n", idev->name);
	isdn_net_unbind_channel(idev);
	return 1;
}

static int
isdn_net_out_dconn(isdn_net_dev *idev)
{
	isdn_ctrl cmd;

	idev->dialstate = ST_OUT_WAIT_BCONN;
	isdn_slot_command(idev->isdn_slot, ISDN_CMD_ACCEPTB, &cmd);
	return 1;
}

static int
isdn_net_in_dconn(isdn_net_dev *idev)
{
	isdn_ctrl cmd;

	idev->dialstate = ST_IN_WAIT_BCONN;
	isdn_slot_command(idev->isdn_slot, ISDN_CMD_ACCEPTB, &cmd);
	return 1;
}

static int
isdn_net_bconn(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;

	idev->dialstate = ST_ACTIVE;

	if (mlp->onhtime) {
		idev->huptimer = 0;
		idev->dial_event = EV_NET_TIMER_HUP;
		mod_timer(&idev->dial_timer, jiffies + HZ);
	} else {
		del_timer(&idev->dial_timer);
	}

	isdn_net_add_to_bundle(mlp, idev);

	printk(KERN_INFO "isdn_net: %s connected\n", idev->name);
	/* If first Chargeinfo comes before B-Channel connect,
	 * we correct the timestamp here.
	 */
	idev->chargetime = jiffies;
	
	idev->transcount = 0;
	idev->cps = 0;
	idev->last_jiffies = jiffies;

	if (mlp->ops->connected)
		mlp->ops->connected(idev);
	else
		isdn_net_dev_wake_queue(idev);

	return 1;
}

/* Check if it's time for idle hang-up */

static int
isdn_net_check_hup(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;

	dbg_net_dial("%s: huptimer %d onhtime %d chargetime %ld chargeint %d\n",
		     idev->name, idev->huptimer, mlp->onhtime, idev->chargetime, idev->chargeint);

	if (idev->huptimer++ <= mlp->onhtime)
		goto mod_timer;

	if (mlp->hupflags & ISDN_CHARGEHUP &&
	    idev->charge_state == ST_CHARGE_HAVE_CINT) {
		if (!time_after(jiffies, idev->chargetime 
				+ idev->chargeint - 2 * HZ))
			goto mod_timer;
	}
	if (idev->outgoing || mlp->hupflags & ISDN_INHUP)
		return isdn_net_hangup(idev);

 mod_timer:
	mod_timer(&idev->dial_timer, idev->dial_timer.expires + HZ);
	return 1;
}

/* A packet has successfully been sent out. */

static int
isdn_net_bsent(isdn_net_dev *idev, isdn_ctrl *c)
{
	isdn_net_local *mlp = idev->mlp;

	isdn_net_dec_frame_cnt(idev);
	mlp->stats.tx_packets++;
	mlp->stats.tx_bytes += c->parm.length;
	return 1;
}

/* Charge-info from TelCo. */

static int
isdn_net_cinf(isdn_net_dev *idev)
{
	idev->charge++;
	switch (idev->charge_state) {
	case ST_CHARGE_NULL:
		idev->charge_state = ST_CHARGE_GOT_CINF;
		break;
	case ST_CHARGE_GOT_CINF:
		idev->charge_state = ST_CHARGE_HAVE_CINT;
		/* fall through */
	case ST_CHARGE_HAVE_CINT:
		idev->chargeint = jiffies - idev->chargetime;
		break;
	}
	idev->chargetime = jiffies;
	dbg_net_dial("%s: got CINF\n", idev->name);
	return 1;
}

static int
isdn_net_disconnected(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;

	del_timer(&idev->dial_timer);
	if (mlp->ops->disconnected)
		mlp->ops->disconnected(idev);
	
	isdn_net_rm_from_bundle(idev);

	printk(KERN_INFO "%s: disconnected\n", idev->name);
	printk(KERN_INFO "%s: Chargesum is %d\n", idev->name,
	       idev->charge);
	isdn_slot_all_eaz(idev->isdn_slot);
	isdn_net_unbind_channel(idev);

	return 1;
}

/* Perform hangup for a net-interface. */

int
isdn_net_hangup(isdn_net_dev *idev)
{
	isdn_ctrl cmd;

	del_timer(&idev->dial_timer);
	if (!isdn_net_bound(idev)) {
		isdn_BUG();
		return 1;
	}
	printk(KERN_INFO "%s: local hangup\n", idev->name);
	isdn_slot_command(idev->isdn_slot, ISDN_CMD_HANGUP, &cmd);
	return 1;
}


static int
isdn_net_event_out_wait_dconn(isdn_net_dev *idev, int pr, void *arg)
{
	switch (pr) {
	case EV_NET_TIMER_DIAL:
		return dial_timeout(idev);
	case EV_NET_TIMER_CB_OUT:
		return isdn_net_hangup(idev);
	case ISDN_STAT_DCONN:
		return isdn_net_out_dconn(idev);
	case ISDN_STAT_DHUP:
		return isdn_net_connect_failure(idev);
	}
	isdn_BUG();
	return 0;
}

static int
isdn_net_event_out_wait_bconn(isdn_net_dev *idev, int pr, void *arg)
{
	switch (pr) {
	case EV_NET_TIMER_DIAL:
		return dial_timeout(idev);
	case ISDN_STAT_BCONN:
		return isdn_net_bconn(idev);
	case ISDN_STAT_DHUP:
		return isdn_net_connect_failure(idev);
	}
	isdn_BUG();
	return 0;
}

static int
isdn_net_event_in_wait_dconn(isdn_net_dev *idev, int pr, void *arg)
{
	switch (pr) {
	case EV_NET_TIMER_INCOMING:
		return isdn_net_hangup(idev);
	case ISDN_STAT_DCONN:
		return isdn_net_in_dconn(idev);
	case ISDN_STAT_DHUP:
		return isdn_net_connect_failure(idev);
	}
	isdn_BUG();
	return 0;
}

static int
isdn_net_event_in_wait_bconn(isdn_net_dev *idev, int pr, void *arg)
{
	switch (pr) {
	case EV_NET_TIMER_INCOMING:
		return isdn_net_hangup(idev);
	case ISDN_STAT_BCONN:
		return isdn_net_bconn(idev);
	case ISDN_STAT_DHUP:
		return isdn_net_connect_failure(idev);
	}
	isdn_BUG();
	return 0;
}

static int
isdn_net_event_wait_before_cb(isdn_net_dev *idev, int pr, void *arg)
{
	switch (pr) {
	case EV_NET_TIMER_CB_IN:
		return init_dialout(idev);
	}
	isdn_BUG();
	return 0;
}

static int
isdn_net_event_active(isdn_net_dev *idev, int pr, void *arg)
{
	switch (pr) {
	case EV_NET_TIMER_HUP:
		return isdn_net_check_hup(idev);
	case ISDN_STAT_BSENT:
		return isdn_net_bsent(idev, arg);
	case ISDN_STAT_BHUP:
	case ISDN_STAT_DHUP:
		return isdn_net_disconnected(idev);
	case ISDN_STAT_CINF:
		return isdn_net_cinf(idev);
	}
	isdn_BUG();
	return 0;
}

static int
isdn_net_event_dial_wait(isdn_net_dev *idev, int pr, void *arg)
{
	switch (pr) {
	case EV_NET_TIMER_DIAL_WAIT:
		return do_dialout(idev);
	}
	isdn_BUG();
	return 0;
}

/*
 * For ISDN_STAT_*, returns 1 if event was for us 
 */
int
isdn_net_handle_event(isdn_net_dev *idev, int pr, void *arg)
{
	dbg_net_dial("%s: dialstate=%d pr=%#x\n", idev->name, 
		     idev->dialstate, pr);

	switch (idev->dialstate) {
	case ST_ACTIVE:
		return isdn_net_event_active(idev, pr, arg);
	case ST_OUT_WAIT_DCONN:
		return isdn_net_event_out_wait_dconn(idev, pr, arg);
	case ST_OUT_WAIT_BCONN:
		return isdn_net_event_out_wait_bconn(idev, pr, arg);
	case ST_IN_WAIT_DCONN:
		return isdn_net_event_in_wait_dconn(idev, pr, arg);
	case ST_IN_WAIT_BCONN:
		return isdn_net_event_in_wait_bconn(idev, pr, arg);
	case ST_WAIT_BEFORE_CB:
		return isdn_net_event_wait_before_cb(idev, pr, arg);
	case ST_OUT_DIAL_WAIT:
		return isdn_net_event_dial_wait(idev, pr, arg);
	default:
		isdn_BUG();
		return 0;
	}
}

static void
isdn_net_log_skb(struct sk_buff *skb, isdn_net_dev *idev)
{
	unsigned char *p = skb->nh.raw; /* hopefully, this was set correctly */
	unsigned short proto = ntohs(skb->protocol);
	int data_ofs;
	struct ip_ports {
		unsigned short source;
		unsigned short dest;
	} *ipp;
	char addinfo[100];

	data_ofs = ((p[0] & 15) * 4);
	switch (proto) {
	case ETH_P_IP:
		switch (p[9]) {
		case IPPROTO_ICMP:
			strcpy(addinfo, "ICMP");
			break;
		case IPPROTO_TCP:
		case IPPROTO_UDP:
			ipp = (struct ip_ports *) (&p[data_ofs]);
			sprintf(addinfo, "%s, port: %d -> %d",
				p[9] == IPPROTO_TCP ? "TCP" : "UDP",
				ntohs(ipp->source), ntohs(ipp->dest));
			break;
		default:
			sprintf(addinfo, "type %d", p[9]);
		}
		printk(KERN_INFO
		       "OPEN: %u.%u.%u.%u -> %u.%u.%u.%u %s\n",
		       
		       NIPQUAD(*(u32 *)(p + 12)), NIPQUAD(*(u32 *)(p + 16)),
		       addinfo);
		break;
	case ETH_P_ARP:
		printk(KERN_INFO
		       "OPEN: ARP %d.%d.%d.%d -> *.*.*.* ?%d.%d.%d.%d\n",
		       NIPQUAD(*(u32 *)(p + 14)), NIPQUAD(*(u32 *)(p + 24)));
		break;
	default:
		printk(KERN_INFO "OPEN: unknown proto %#x\n", proto);
	}
}

/*
 * this function is used to send supervisory data, i.e. data which was
 * not received from the network layer, but e.g. frames from ipppd, CCP
 * reset frames etc.
 */
void
isdn_net_write_super(isdn_net_dev *idev, struct sk_buff *skb)
{
	if (in_irq()) {
		// we can't grab the lock from irq context, 
		// so we just queue the packet
		skb_queue_tail(&idev->super_tx_queue, skb); 
		tasklet_schedule(&idev->tlet);
		return;
	}

	spin_lock_bh(&idev->xmit_lock);
	if (!isdn_net_dev_busy(idev)) {
		isdn_net_writebuf_skb(idev, skb);
	} else {
		skb_queue_tail(&idev->super_tx_queue, skb);
	}
	spin_unlock_bh(&idev->xmit_lock);
}

/* 
 * all frames sent from the (net) LL to a HL driver should go via this function
 * it's serialized by the caller holding the idev->xmit_lock spinlock
 */
void isdn_net_writebuf_skb(isdn_net_dev *idev, struct sk_buff *skb)
{
	isdn_net_local *mlp = idev->mlp;
	int ret;
	int len = skb->len;     /* save len */

	/* before obtaining the lock the caller should have checked that
	   the lp isn't busy */
	if (isdn_net_dev_busy(idev)) {
		isdn_BUG();
		goto error;
	}

	if (!isdn_net_online(idev)) {
		isdn_BUG();
		goto error;
	}
	ret = isdn_slot_write(idev->isdn_slot, skb);
	if (ret != len) {
		/* we should never get here */
		printk(KERN_WARNING "%s: HL driver queue full\n", idev->name);
		goto error;
	}
	
	idev->transcount += len;
	isdn_net_inc_frame_cnt(idev);
	return;

 error:
	dev_kfree_skb(skb);
	mlp->stats.tx_errors++;
}

static void
isdn_net_dial_slave(isdn_net_local *mlp)
{
	isdn_net_dev *idev;

	list_for_each_entry(idev, &mlp->slaves, slaves) {
		if (!isdn_net_bound(idev)) {
			isdn_net_dev_dial(idev);
			break;
		}
	}
}

/*
 *  Based on cps-calculation, check if device is overloaded.
 *  If so, and if a slave exists, trigger dialing for it.
 *  If any slave is online, deliver packets using a simple round robin
 *  scheme.
 *
 *  Return: 0 on success, !0 on failure.
 */

int
isdn_net_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	isdn_net_dev *idev;
	isdn_net_local *mlp = ndev->priv;

	ndev->trans_start = jiffies;

	if (list_empty(&mlp->online))
		return isdn_net_autodial(skb, ndev);

	idev = isdn_net_get_locked_dev(mlp);
	if (!idev) {
		printk(KERN_WARNING "%s: all channels busy - requeuing!\n", ndev->name);
		netif_stop_queue(ndev);
		return 1;
	}
	/* we have our idev locked from now on */

	isdn_net_writebuf_skb(idev, skb);
	spin_unlock_bh(&idev->xmit_lock);

	/* the following stuff is here for backwards compatibility.
	 * in future, start-up and hangup of slaves (based on current load)
	 * should move to userspace and get based on an overall cps
	 * calculation
	 */
	if (jiffies != idev->last_jiffies) {
		idev->cps = idev->transcount * HZ / (jiffies - idev->last_jiffies);
		idev->last_jiffies = jiffies;
		idev->transcount = 0;
	}
	if (dev->net_verbose > 3)
		printk(KERN_DEBUG "%s: %d bogocps\n", idev->name, idev->cps);

	if (idev->cps > mlp->triggercps) {
		if (!idev->sqfull) {
			/* First time overload: set timestamp only */
			idev->sqfull = 1;
			idev->sqfull_stamp = jiffies;
		} else {
			/* subsequent overload: if slavedelay exceeded, start dialing */
			if (time_after(jiffies, idev->sqfull_stamp + mlp->slavedelay)) {
				isdn_net_dial_slave(mlp);
			}
		}
	} else {
		if (idev->sqfull && time_after(jiffies, idev->sqfull_stamp + mlp->slavedelay + 10 * HZ)) {
			idev->sqfull = 0;
		}
		/* this is a hack to allow auto-hangup for slaves on moderate loads */
		list_del(&mlp->online);
		list_add_tail(&mlp->online, &idev->online);
	}

	return 0;
}

int
isdn_net_autodial(struct sk_buff *skb, struct net_device *ndev)
{
	isdn_net_local *mlp = ndev->priv;
	isdn_net_dev *idev = list_entry(mlp->slaves.next, isdn_net_dev, slaves);

	/* are we dialing already? */
	if (isdn_net_bound(idev))
		goto stop_queue;

	if (ISDN_NET_DIALMODE(*mlp) != ISDN_NET_DM_AUTO)
		goto discard;

	if (isdn_net_dev_dial(idev) < 0)
		goto discard;

	/* Log packet, which triggered dialing */
	if (dev->net_verbose)
		isdn_net_log_skb(skb, idev);

 stop_queue:
	netif_stop_queue(ndev);
	return 1;

 discard:
	isdn_net_unreachable(ndev, skb, "dial rejected");
	dev_kfree_skb(skb);
	return 0;
}


/*
 * Got a packet from ISDN-Channel.
 */
static void
isdn_net_receive(isdn_net_dev *idev, struct sk_buff *skb)
{
	isdn_net_local *mlp = idev->mlp;

	idev->transcount += skb->len;

	mlp->stats.rx_packets++;
	mlp->stats.rx_bytes += skb->len;
	skb->dev = &mlp->dev;
	skb->pkt_type = PACKET_HOST;
	skb->mac.raw = skb->data;
	isdn_dumppkt("R:", skb->data, skb->len, 40);

	mlp->ops->receive(mlp, idev, skb);
}

/*
 * A packet arrived via ISDN. Search interface-chain for a corresponding
 * interface. If found, deliver packet to receiver-function and return 1,
 * else return 0.
 */
int
isdn_net_rcv_skb(int idx, struct sk_buff *skb)
{
	isdn_net_dev *idev = isdn_slot_idev(idx);

	if (!idev) {
		HERE;
		return 0;
	}
	if (!isdn_net_online(idev))
		return 0;

	isdn_net_receive(idev, skb);
	return 0;
}

static int
isdn_net_do_callback(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;
	int slot;
	/*
	 * Is the state MANUAL?
	 * If so, no callback can be made,
	 * so reject actively.
	 */
	if (ISDN_NET_DIALMODE(*mlp) == ISDN_NET_DM_OFF) {
		printk(KERN_INFO "incoming call for callback, interface %s `off' -> rejected\n",
		       idev->name);
		return 3;
	}
	printk(KERN_DEBUG "%s: start callback\n", idev->name);
	
	/* Grab a free ISDN-Channel */
	slot = isdn_get_free_slot(ISDN_USAGE_NET, mlp->l2_proto, mlp->l3_proto,
				  idev->pre_device, idev->pre_channel, mlp->msn);
	if (slot < 0)
		goto err;

	isdn_slot_set_usage(slot, isdn_slot_usage(slot) | ISDN_USAGE_OUTGOING);

	if (isdn_net_bind_channel(idev, slot) < 0)
		goto err;

	/* Setup dialstate. */
	idev->dial_timer.expires = jiffies + mlp->cbdelay;
	idev->dial_event = EV_NET_TIMER_CB_IN;
	add_timer(&idev->dial_timer);
	idev->dialstate = ST_WAIT_BEFORE_CB;

	/* Initiate dialing by returning 2 or 4 */
	return (mlp->flags & ISDN_NET_CBHUP) ? 2 : 4;

 err:
	return 0;
}

/*
 * An incoming call-request has arrived.
 * Search the interface-chain for an appropriate interface.
 * If found, connect the interface to the ISDN-channel and initiate
 * D- and B-Channel-setup. If secure-flag is set, accept only
 * configured phone-numbers. If callback-flag is set, initiate
 * callback-dialing.
 *
 * Return-Value: 0 = No appropriate interface for this call.
 *               1 = Call accepted
 *               2 = Reject call, wait cbdelay, then call back
 *               3 = Reject call
 *               4 = Wait cbdelay, then call back
 *               5 = No appropriate interface for this call,
 *                   would eventually match if CID was longer.
 */
int
isdn_net_find_icall(int di, int ch, int idx, setup_parm *setup)
{
	char *eaz;
	unsigned char si1, si2;
	int match_more = 0;
	int retval;
	struct list_head *l;
	struct isdn_net_phone *n;
	ulong flags;
	char nr[32];
	char *my_eaz;
	isdn_ctrl cmd;

	int slot = isdn_dc2minor(di, ch);
	/* Search name in netdev-chain */
	save_flags(flags);
	cli();
	if (!setup->phone[0]) {
		nr[0] = '0';
		nr[1] = '\0';
		printk(KERN_INFO "isdn_net: Incoming call without OAD, assuming '0'\n");
	} else {
		strcpy(nr, setup->phone);
	}
	si1 = setup->si1;
	si2 = setup->si2;
	if (!setup->eazmsn[0]) {
		printk(KERN_WARNING "isdn_net: Incoming call without CPN, assuming '0'\n");
		eaz = "0";
	} else {
		eaz = setup->eazmsn;
	}
	if (dev->net_verbose > 1)
		printk(KERN_INFO "isdn_net: call from %s,%d,%d -> %s\n", nr, si1, si2, eaz);
        /* Accept DATA and VOICE calls at this stage
        local eaz is checked later for allowed call types */
        if ((si1 != 7) && (si1 != 1)) {
                restore_flags(flags);
                if (dev->net_verbose > 1)
                        printk(KERN_INFO "isdn_net: Service-Indicator not 1 or 7, ignored\n");
                return 0;
        }

	n = NULL;
	dbg_net_icall("n_fi: di=%d ch=%d idx=%d usg=%d\n", di, ch, idx,
		      isdn_slot_usage(idx));

	list_for_each(l, &isdn_net_devs) {
		isdn_net_dev *idev = list_entry(l, isdn_net_dev, global_list);
		isdn_net_local *mlp = idev->mlp;

                /* check acceptable call types for DOV */
		dbg_net_icall("n_fi: if='%s', l.msn=%s, l.flags=%#x, l.dstate=%d\n",
			      idev->name, mlp->msn, mlp->flags, idev->dialstate);

                my_eaz = isdn_slot_map_eaz2msn(slot, mlp->msn);
                if (si1 == 1) { /* it's a DOV call, check if we allow it */
                        if (*my_eaz == 'v' || *my_eaz == 'V' ||
			    *my_eaz == 'b' || *my_eaz == 'B')
                                my_eaz++; /* skip to allow a match */
                        else
                                continue; /* force non match */
                } else { /* it's a DATA call, check if we allow it */
                        if (*my_eaz == 'b' || *my_eaz == 'B')
                                my_eaz++; /* skip to allow a match */
                }

		switch (isdn_msncmp(eaz, my_eaz)) {
		case 1:
			continue;
		case 2:
			match_more = 1;
			continue;
		}

		if (isdn_net_bound(idev))
			continue;
		
		if (!USG_NONE(isdn_slot_usage(idx)))
			continue;

		dbg_net_icall("n_fi: match1, pdev=%d pch=%d\n",
			      idev->pre_device, idev->pre_channel);

		if (isdn_slot_usage(idx) & ISDN_USAGE_EXCLUSIVE &&
		    (idev->pre_channel != ch || idev->pre_device != di)) {
			dbg_net_icall("n_fi: excl check failed\n");
			continue;
		}
		dbg_net_icall("n_fi: match2\n");
		if (mlp->flags & ISDN_NET_SECURE) {
			list_for_each_entry(n, &mlp->phone[0], list) {
				if (!isdn_msncmp(nr, n->num)) {
					goto found;
				}
			}
			continue;
		}
	found:
		dbg_net_icall("n_fi: match3\n");
		/* matching interface found */
		
		/*
		 * Is the state STOPPED?
		 * If so, no dialin is allowed,
		 * so reject actively.
		 * */
		if (ISDN_NET_DIALMODE(*mlp) == ISDN_NET_DM_OFF) {
			restore_flags(flags);
			printk(KERN_INFO "incoming call, interface %s `stopped' -> rejected\n",
			       idev->name);
			return 3;
		}
		/*
		 * Is the interface up?
		 * If not, reject the call actively.
		 */
		if (!isdn_net_device_started(idev)) {
			restore_flags(flags);
			printk(KERN_INFO "%s: incoming call, interface down -> rejected\n",
			       idev->name);
			return 3;
		}
                if (mlp->flags & ISDN_NET_CALLBACK) {
                        retval = isdn_net_do_callback(idev);
                        restore_flags(flags);
                        return retval;
                }
		printk(KERN_DEBUG "%s: call from %s -> %s accepted\n",
		       idev->name, nr, eaz);

		strcpy(isdn_slot_num(idx), nr);
		isdn_slot_set_usage(idx, (isdn_slot_usage(idx) & ISDN_USAGE_EXCLUSIVE) | ISDN_USAGE_NET);

		isdn_net_bind_channel(idev, idx);
		
		idev->outgoing = 0;
		idev->charge_state = ST_CHARGE_NULL;
		/* Got incoming Call, setup L2 and L3 protocols,
		 * then wait for D-Channel-connect
		 */
		cmd.arg = mlp->l2_proto << 8;
		isdn_slot_command(idev->isdn_slot, ISDN_CMD_SETL2, &cmd);
		cmd.arg = mlp->l3_proto << 8;
		isdn_slot_command(idev->isdn_slot, ISDN_CMD_SETL3, &cmd);
		
		idev->dial_timer.expires = jiffies + mlp->dialtimeout;
		idev->dial_event = EV_NET_TIMER_INCOMING;
		add_timer(&idev->dial_timer);
		idev->dialstate = ST_IN_WAIT_DCONN;
		
		restore_flags(flags);
		return 1;
	}
	if (dev->net_verbose)
		printk(KERN_INFO "isdn_net: call from %s -> %d %s ignored\n", nr, slot, eaz);
	restore_flags(flags);
	return (match_more == 2) ? 5:0;
}

/*
 * Trigger dialing out
 */
int
isdn_net_dev_dial(isdn_net_dev *idev)
{
	int slot;
	isdn_net_local *mlp = idev->mlp;

	if (isdn_net_bound(idev))
		return -EBUSY;

	if (idev->exclusive >= 0)
		slot = idev->exclusive;
	else
		slot = isdn_get_free_slot(ISDN_USAGE_NET, mlp->l2_proto,
					  mlp->l3_proto, idev->pre_device, 
					  idev->pre_channel, mlp->msn);
	if (slot < 0)
		goto err;

	isdn_slot_set_usage(slot, isdn_slot_usage(slot) | ISDN_USAGE_OUTGOING);

	if (isdn_net_bind_channel(idev, slot) < 0)
		goto err;

	/* Initiate dialing */
	init_dialout(idev);

	return 0;

 err:
	return -EAGAIN;
}

/*
 * This is called from certain upper protocol layers (multilink ppp
 * and x25iface encapsulation module) that want to initiate dialing
 * themselves.
 */
int
isdn_net_dial_req(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;
	/* is there a better error code? */
	if (ISDN_NET_DIALMODE(*mlp) != ISDN_NET_DM_AUTO)
		return -EBUSY;

	return isdn_net_dev_dial(idev);
}

// ISDN_NET_ENCAP_IPTYP
// ethernet type field
// ======================================================================

static int
isdn_iptyp_header(struct sk_buff *skb, struct net_device *dev,
		   unsigned short type, void *daddr, void *saddr, 
		   unsigned plen)
{
	put_u16(skb_push(skb, 2), type);
	return 2;
}

static void
isdn_iptyp_receive(isdn_net_local *lp, isdn_net_dev *idev, 
		   struct sk_buff *skb)
{
	idev->huptimer = 0;
	get_u16(skb->data, &skb->protocol);
	skb_pull(skb, 2);
	netif_rx(skb);
}

static struct isdn_netif_ops iptyp_ops = {
	.hard_start_xmit     = isdn_net_start_xmit,
	.hard_header         = isdn_iptyp_header,
	.flags               = IFF_NOARP | IFF_POINTOPOINT,
	.type                = ARPHRD_PPP,
	.addr_len            = 2,
	.receive             = isdn_iptyp_receive,
};

// ISDN_NET_ENCAP_UIHDLC
// HDLC with UI-Frames (for ispa with -h1 option) */
// ======================================================================

static int
isdn_uihdlc_header(struct sk_buff *skb, struct net_device *dev,
		   unsigned short type, void *daddr, void *saddr, 
		   unsigned plen)
{
	put_u16(skb_push(skb, 2), 0x0103);
	return 2;
}

static void
isdn_uihdlc_receive(isdn_net_local *lp, isdn_net_dev *idev, 
		    struct sk_buff *skb)
{
	idev->huptimer = 0;
	skb_pull(skb, 2);
	skb->protocol = htons(ETH_P_IP);
	netif_rx(skb);
}

static struct isdn_netif_ops uihdlc_ops = {
	.hard_start_xmit     = isdn_net_start_xmit,
	.hard_header         = isdn_uihdlc_header,
	.flags               = IFF_NOARP | IFF_POINTOPOINT,
	.type                = ARPHRD_HDLC,
	.addr_len            = 2,
	.receive             = isdn_uihdlc_receive,
};

// ISDN_NET_ENCAP_RAWIP
// RAW-IP without MAC-Header
// ======================================================================

static void
isdn_rawip_receive(isdn_net_local *lp, isdn_net_dev *idev, 
		   struct sk_buff *skb)
{
	idev->huptimer = 0;
	skb->protocol = htons(ETH_P_IP);
	netif_rx(skb);
}

static struct isdn_netif_ops rawip_ops = {
	.hard_start_xmit     = isdn_net_start_xmit,
	.flags               = IFF_NOARP | IFF_POINTOPOINT,
	.type                = ARPHRD_PPP,
	.receive             = isdn_rawip_receive,
};

// ISDN_NET_ENCAP_ETHER
// Ethernet over ISDN
// ======================================================================

static void
isdn_ether_receive(isdn_net_local *lp, isdn_net_dev *idev, 
		   struct sk_buff *skb)
{
	idev->huptimer = 0;
	skb->protocol = eth_type_trans(skb, skb->dev);
	netif_rx(skb);
}

static int
isdn_ether_open(isdn_net_local *lp)
{
	struct net_device *dev = &lp->dev;
	struct in_device *in_dev;
	int i;

	/* Fill in the MAC-level header ... */
	for (i = 0; i < ETH_ALEN; i++)
		dev->dev_addr[i] = 0xfc;
	in_dev = dev->ip_ptr;
	if (in_dev) {
		/* any address will do - we take the first */
		struct in_ifaddr *ifa = in_dev->ifa_list;
		if (ifa)
			memcpy(dev->dev_addr+2, &ifa->ifa_local, 4);
	}
	return 0;
}

static int
isdn_ether_init(isdn_net_local *lp)
{
	struct net_device *dev = &lp->dev;

	ether_setup(dev);
	dev->tx_queue_len = 10;
	dev->hard_header_len += isdn_hard_header_len();

	return 0;
}

static struct isdn_netif_ops ether_ops = {
	.hard_start_xmit     = isdn_net_start_xmit,
	.hard_header         = eth_header,
	.receive             = isdn_ether_receive,
	.init                = isdn_ether_init,
	.open                = isdn_ether_open,
};

// ======================================================================

void
isdn_net_init(void)
{
	register_isdn_netif(ISDN_NET_ENCAP_ETHER,      &ether_ops);
	register_isdn_netif(ISDN_NET_ENCAP_RAWIP,      &rawip_ops);
	register_isdn_netif(ISDN_NET_ENCAP_IPTYP,      &iptyp_ops);
	register_isdn_netif(ISDN_NET_ENCAP_UIHDLC,     &uihdlc_ops);
	register_isdn_netif(ISDN_NET_ENCAP_CISCOHDLC,  &ciscohdlck_ops);
	register_isdn_netif(ISDN_NET_ENCAP_CISCOHDLCK, &ciscohdlck_ops);
#ifdef CONFIG_ISDN_X25
	register_isdn_netif(ISDN_NET_ENCAP_X25IFACE,   &isdn_x25_ops);
#endif
#ifdef CONFIG_ISDN_PPP
	register_isdn_netif(ISDN_NET_ENCAP_SYNCPPP,    &isdn_ppp_ops);
#endif
}
