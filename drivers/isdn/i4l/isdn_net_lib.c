/* Linux ISDN subsystem, network interface support code
 *
 * Copyright 1994-1998  by Fritz Elfert (fritz@isdn4linux.de)
 *           1995,96    by Thinking Objects Software GmbH Wuerzburg
 *           1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *           1999-2002  by Kai Germaschewski <kai@germaschewski.name>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

/*
 * Data Over Voice (DOV) support added - Guy Ellis 23-Mar-02 
 *                                       guy@traverse.com.au
 * Outgoing calls - looks for a 'V' in first char of dialed number
 * Incoming calls - checks first character of eaz as follows:
 *   Numeric - accept DATA only - original functionality
 *   'V'     - accept VOICE (DOV) only
 *   'B'     - accept BOTH DATA and DOV types
 *
 */

/* Locking works as follows: 
 *
 * The configuration of isdn_net_devs works via ioctl on
 * /dev/isdnctrl (for legacy reasons).
 * All configuration accesses are globally serialized by means of
 * the global semaphore &sem.
 * 
 * All other uses of isdn_net_dev will only happen when the corresponding
 * struct net_device has been opened. So in the non-config code we can
 * rely on the config data not changing under us.
 *
 * To achieve this, in the "writing" ioctls, that is those which may change
 * data, additionally grep the rtnl semaphore and check to make sure
 * that the net_device has not been openend ("netif_running()")
 *
 * isdn_net_dev's are added to the global list "isdn_net_devs" in the
 * configuration ioctls, so accesses to that list are protected by
 * &sem as well.
 *
 * Incoming calls are signalled in IRQ context, so we cannot take &sem
 * while walking the list of devices. To handle this, we put devices
 * onto a "running" list, which is protected by a spin lock and can thus
 * be traversed in IRQ context. If a matching isdn_net_dev is found,
 * it's ref count shall be incremented, to make sure no racing
 * net_device::close() can take it away under us. 
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/capability.h>
#include <linux/rtnetlink.h>
#include "isdn_common.h"
#include "isdn_net_lib.h"
#include "isdn_net.h"
#include "isdn_ppp.h"
#include "isdn_ciscohdlck.h"
#include "isdn_concap.h"

#define ISDN_NET_TX_TIMEOUT (20*HZ) 

/* All of this configuration code is globally serialized */

static DECLARE_MUTEX(sem);
LIST_HEAD(isdn_net_devs); /* Linked list of isdn_net_dev's */ // FIXME static

/* Reference counting for net devices (they work on isdn_net_local *,
 * but count references to the related isdn_net_dev's as well.
 * Basic rule: When state of isdn_net_dev changes from ST_NULL -> sth,
 * get a reference, when it changes back to ST_NULL, put it
 */ 

static inline void
lp_get(isdn_net_local *lp)
{
	if (atomic_read(&lp->refcnt) < 1)
		isdn_BUG();

	atomic_inc(&lp->refcnt);
}

static inline void
lp_put(isdn_net_local *lp)
{
	atomic_dec(&lp->refcnt);

	/* the last reference, the list should always remain */
	if (atomic_read(&lp->refcnt) < 1)
		isdn_BUG();
}

static int isdn_net_handle_event(isdn_net_dev *idev, int pr, void *arg);
static void isdn_net_tasklet(unsigned long data);
static void isdn_net_dial_timer(unsigned long data);
static int isdn_init_netif(struct net_device *ndev);
static void isdn_net_dev_debug(struct fsm_inst *fi, char *fmt, ...);
static int isdn_net_dial(isdn_net_dev *idev);
static int isdn_net_bsent(isdn_net_dev *idev, isdn_ctrl *c);

static struct fsm isdn_net_fsm;

enum {
	ST_NULL,
	ST_OUT_BOUND,
	ST_OUT_WAIT_DCONN,
	ST_OUT_WAIT_BCONN,
	ST_IN_WAIT_DCONN,
	ST_IN_WAIT_BCONN,
	ST_ACTIVE,
	ST_WAIT_DHUP,
	ST_WAIT_BEFORE_CB,
	ST_OUT_DIAL_WAIT,
};

static char *isdn_net_st_str[] = {
	"ST_NULL",
	"ST_OUT_BOUND",
	"ST_OUT_WAIT_DCONN",
	"ST_OUT_WAIT_BCONN",
	"ST_IN_WAIT_DCONN",
	"ST_IN_WAIT_BCONN",
	"ST_ACTIVE",
	"ST_WAIT_DHUP",
	"ST_WAIT_BEFORE_CB",
	"ST_OUT_DIAL_WAIT",
};

enum {
	EV_NET_TIMER_INCOMING,
	EV_NET_TIMER_DIAL,
	EV_NET_TIMER_DIAL_WAIT,
	EV_NET_TIMER_CB_OUT,
	EV_NET_TIMER_CB_IN,
	EV_NET_TIMER_HUP,
	EV_NET_STAT_DCONN,
	EV_NET_STAT_BCONN,
	EV_NET_STAT_DHUP,
	EV_NET_STAT_BHUP,
	EV_NET_STAT_CINF,
	EV_NET_STAT_BSENT,
	EV_NET_DO_DIAL,
	EV_NET_DO_CALLBACK,
	EV_NET_DO_ACCEPT,
};

static char *isdn_net_ev_str[] = {
	"EV_NET_TIMER_INCOMING",
	"EV_NET_TIMER_DIAL",
	"EV_NET_TIMER_DIAL_WAIT",
	"EV_NET_TIMER_CB_OUT",
	"EV_NET_TIMER_CB_IN",
	"EV_NET_TIMER_HUP",
	"EV_NET_STAT_DCONN",
	"EV_NET_STAT_BCONN",
	"EV_NET_STAT_DHUP",
	"EV_NET_STAT_BHUP",
	"EV_NET_STAT_CINF",
	"EV_NET_STAT_BSENT",
	"EV_NET_DO_DIAL",
	"EV_NET_DO_CALLBACK",
	"EV_NET_DO_ACCEPT",
};

/* Definitions for hupflags: */

#define ISDN_CHARGEHUP   4      /* We want to use the charge mechanism      */
#define ISDN_INHUP       8      /* Even if incoming, close after huptimeout */
#define ISDN_MANCHARGE  16      /* Charge Interval manually set             */

enum {
	ST_CHARGE_NULL,
	ST_CHARGE_GOT_CINF,  /* got a first charge info */
	ST_CHARGE_HAVE_CINT, /* got a second chare info and thus the timing */
};

/* ====================================================================== */
/* Registration of ISDN network interface types                           */
/* ====================================================================== */

static struct isdn_netif_ops *isdn_netif_ops[ISDN_NET_ENCAP_NR];

int
register_isdn_netif(int encap, struct isdn_netif_ops *ops)
{
	if (encap < 0 || encap >= ISDN_NET_ENCAP_NR)
		return -EINVAL;

	if (isdn_netif_ops[encap])
		return -EBUSY;

	isdn_netif_ops[encap] = ops;

	return 0;
}

/* ====================================================================== */
/* Helpers                                                                */
/* ====================================================================== */

/* Search list of net-interfaces for an interface with given name. */

static isdn_net_dev *
isdn_net_findif(char *name)
{
	isdn_net_dev *idev;

	list_for_each_entry(idev, &isdn_net_devs, global_list) {
		if (!strcmp(idev->name, name))
			return idev;
	}
	return NULL;
}

/* Set up a certain encapsulation */

static int
isdn_net_set_encap(isdn_net_local *lp, int encap)
{
	int retval = 0;

	if (lp->p_encap == encap){
		/* nothing to do */
		retval = 0;
		goto out;
	}
	if (netif_running(&lp->dev)) {
		retval = -EBUSY;
		goto out;
	}
	if (lp->ops && lp->ops->cleanup)
		lp->ops->cleanup(lp);

	if (encap < 0 || encap >= ISDN_NET_ENCAP_NR ||
	    !isdn_netif_ops[encap]) {
		lp->p_encap = -1;
		lp->ops = NULL;
		retval = -EINVAL;
		goto out;
	}

	lp->p_encap = encap;
	lp->ops = isdn_netif_ops[encap];

	lp->dev.hard_start_xmit     = lp->ops->hard_start_xmit;
	lp->dev.hard_header         = lp->ops->hard_header;
	lp->dev.do_ioctl            = lp->ops->do_ioctl;
	lp->dev.flags               = lp->ops->flags;
	lp->dev.type                = lp->ops->type;
	lp->dev.addr_len            = lp->ops->addr_len;
	if (lp->ops->init)
		retval = lp->ops->init(lp);

	if (retval != 0) {
		lp->p_encap = -1;
		lp->ops = NULL;
	}
 out:
	return retval;
}

static int
isdn_net_bind(isdn_net_dev *idev, isdn_net_ioctl_cfg *cfg)
{
	isdn_net_local *mlp = idev->mlp;
	int retval;
	int drvidx = -1;
	int chidx = -1;
	char drvid[25];

	strlcpy(drvid, cfg->drvid, sizeof(drvid));

	if (cfg->exclusive && !strlen(drvid)) {
		/* If we want to bind exclusively, need to specify drv/chan */
		retval = -ENODEV;
		goto out;
	}
	if (strlen(drvid)) {
		/* A bind has been requested ... */
		char *c = strchr(drvid, ',');
		if (!c) {
			retval = -ENODEV;
			goto out;
		}
		/* The channel-number is appended to the driver-Id with a comma */
		*c = 0;
		chidx = simple_strtol(c + 1, NULL, 10);
		drvidx = isdn_drv_lookup(drvid);
		if (drvidx == -1 || chidx == -1) {
			/* Either driver-Id or channel-number invalid */
			retval = -ENODEV;
			goto out;
		}
	}
	if (cfg->exclusive == !!idev->exclusive &&
	    drvidx == idev->pre_device && chidx == idev->pre_channel) {
		/* no change */
		retval = 0;
		goto out;
	}
	if (idev->exclusive) {
		isdn_slot_free(idev->exclusive);
		idev->exclusive = NULL;
	}
	if (cfg->exclusive) {
		/* If binding is exclusive, try to grab the channel */
		idev->exclusive = isdn_get_free_slot(ISDN_USAGE_NET | ISDN_USAGE_EXCLUSIVE, 
						     mlp->l2_proto, mlp->l3_proto, drvidx, chidx, cfg->eaz);
		if (!idev->exclusive) {
			/* Grab failed, because desired channel is in use */
			retval = -EBUSY;
			goto out;
		}
	}
	idev->pre_device = drvidx;
	idev->pre_channel = chidx;
	retval = 0;
 out:
	return retval;
}

/*
 * Delete all phone-numbers of an interface.
 */
static void
isdn_net_rmallphone(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;
	struct isdn_net_phone *n;
	int i;

	for (i = 0; i < 2; i++) {
		while (!list_empty(&mlp->phone[i])) {
			n = list_entry(mlp->phone[i].next, struct isdn_net_phone, list);
			list_del(&n->list);
			kfree(n);
		}
	}
}

/* ====================================================================== */
/* /dev/isdnctrl net ioctl interface                                      */
/* ====================================================================== */

/*
 * Allocate a new network-interface and initialize its data structures
 */
static int
isdn_net_addif(char *name, isdn_net_local *mlp)
{
	int retval;
	struct net_device *dev = NULL;
	isdn_net_dev *idev;

	/* Avoid creating an existing interface */
	if (isdn_net_findif(name))
		return -EEXIST;

	idev = kmalloc(sizeof(*idev), GFP_KERNEL);
	if (!idev)
		return -ENOMEM;

	memset(idev, 0, sizeof(*idev));
	strcpy(idev->name, name);

	tasklet_init(&idev->tlet, isdn_net_tasklet, (unsigned long) idev);
	skb_queue_head_init(&idev->super_tx_queue);

	idev->isdn_slot = NULL;
	idev->pre_device = -1;
	idev->pre_channel = -1;
	idev->exclusive = NULL;

	idev->pppbind = -1;

	init_timer(&idev->dial_timer);
	idev->dial_timer.data = (unsigned long) idev;
	idev->dial_timer.function = isdn_net_dial_timer;

	idev->fi.fsm = &isdn_net_fsm;
	idev->fi.state = ST_NULL;
	idev->fi.debug = 1;
	idev->fi.userdata = idev;
	idev->fi.printdebug = isdn_net_dev_debug;

	if (!mlp) {
		/* Device shall be a master */
		mlp = kmalloc(sizeof(*mlp), GFP_KERNEL);
		if (!mlp)
			return -ENOMEM;
		
		memset(mlp, 0, sizeof(*mlp));

		mlp->magic = ISDN_NET_MAGIC;
		INIT_LIST_HEAD(&mlp->slaves);
		INIT_LIST_HEAD(&mlp->online);
		spin_lock_init(&mlp->xmit_lock);

		mlp->p_encap = -1;
		isdn_net_set_encap(mlp, ISDN_NET_ENCAP_RAWIP);

		mlp->l2_proto = ISDN_PROTO_L2_X75I;
		mlp->l3_proto = ISDN_PROTO_L3_TRANS;
		mlp->triggercps = 6000;
		mlp->slavedelay = 10 * HZ;
		mlp->hupflags = ISDN_INHUP;
		mlp->onhtime = 10;
		mlp->dialmax = 1;
		mlp->flags = ISDN_NET_CBHUP | ISDN_NET_DM_MANUAL | ISDN_NET_SECURE;
		mlp->cbdelay = 5 * HZ;	   /* Wait 5 secs before call-back  */
		mlp->dialtimeout = 60 * HZ;/* Wait 1 min for connection     */
		mlp->dialwait = 5 * HZ;    /* Wait 5 sec. after failed dial */
		INIT_LIST_HEAD(&mlp->phone[0]);
		INIT_LIST_HEAD(&mlp->phone[1]);
		dev = &mlp->dev;
	}
	idev->mlp = mlp;
	list_add_tail(&idev->slaves, &mlp->slaves);

	if (dev) {
		strcpy(dev->name, name);
		dev->priv = mlp;
		dev->init = isdn_init_netif;
		SET_MODULE_OWNER(dev);
		retval = register_netdev(dev);
		if (retval) {
			kfree(mlp);
			kfree(idev);
			return retval;
		}
	}
	list_add(&idev->global_list, &isdn_net_devs);

	return 0;
}

/*
 * Add a new slave interface to an existing one
 */
static int
isdn_net_addslave(char *parm)
{
	char *p = strchr(parm, ',');
	isdn_net_dev *idev;
	isdn_net_local *mlp;
	int retval;

	/* get slave name */
	if (!p || !p[1])
		return -EINVAL;

	*p++ = 0;

	/* find master */
	idev = isdn_net_findif(parm);
	if (!idev)
		return -ESRCH;

	mlp = idev->mlp;

	rtnl_lock();

	if (netif_running(&mlp->dev)) {
		retval = -EBUSY;
		goto out;
	}
	retval = isdn_net_addif(p, mlp);
 out:	
	rtnl_unlock();
	return retval;
}

/*
 * Delete a single network-interface
 */
static int
isdn_net_dev_delete(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;
	int retval;

	rtnl_lock();
	
	if (netif_running(&mlp->dev)) {
		retval = -EBUSY;
		goto unlock;
	}
	isdn_net_set_encap(mlp, -1);
	isdn_net_rmallphone(idev);

	if (idev->exclusive)
		isdn_slot_free(idev->exclusive);

	list_del(&idev->slaves);
	
	rtnl_unlock();

	if (list_empty(&mlp->slaves)) {
		unregister_netdev(&mlp->dev);
		kfree(mlp);
	}

	list_del(&idev->global_list);
	kfree(idev);
	return 0;

 unlock:
	rtnl_unlock();
	return retval;
}

/*
 * Delete a single network-interface
 */
static int
isdn_net_delif(char *name)
{
	/* FIXME: For compatibility, if a master isdn_net_dev is rm'ed,
	 * kill all slaves, too */

	isdn_net_dev *idev = isdn_net_findif(name);

	if (!idev)
		return -ENODEV;

	return isdn_net_dev_delete(idev);
}

/*
 * Set interface-parameters.
 * Always set all parameters, so the user-level application is responsible
 * for not overwriting existing setups. It has to get the current
 * setup first, if only selected parameters are to be changed.
 */
static int
isdn_net_setcfg(isdn_net_ioctl_cfg *cfg)
{
	isdn_net_dev *idev = isdn_net_findif(cfg->name);
	isdn_net_local *mlp;
	int retval;

	if (!idev)
		return -ENODEV;

	mlp = idev->mlp;

	rtnl_lock();

	if (netif_running(&mlp->dev)) {
		retval = -EBUSY;
		goto out;
	}

	retval = isdn_net_set_encap(mlp, cfg->p_encap);
	if (retval)
		goto out;

	retval = isdn_net_bind(idev, cfg);
	if (retval)
		goto out;

	strlcpy(mlp->msn, cfg->eaz, sizeof(mlp->msn));
	mlp->onhtime = cfg->onhtime;
	idev->charge = cfg->charge;
	mlp->l2_proto = cfg->l2_proto;
	mlp->l3_proto = cfg->l3_proto;
	mlp->cbdelay = cfg->cbdelay * HZ / 5;
	mlp->dialmax = cfg->dialmax;
	mlp->triggercps = cfg->triggercps;
	mlp->slavedelay = cfg->slavedelay * HZ;
	idev->pppbind = cfg->pppbind;
	mlp->dialtimeout = cfg->dialtimeout >= 0 ? cfg->dialtimeout * HZ : -1;
	mlp->dialwait = cfg->dialwait * HZ;
	if (cfg->secure)
		mlp->flags |= ISDN_NET_SECURE;
	else
		mlp->flags &= ~ISDN_NET_SECURE;
	if (cfg->cbhup)
		mlp->flags |= ISDN_NET_CBHUP;
	else
		mlp->flags &= ~ISDN_NET_CBHUP;
	switch (cfg->callback) {
	case 0:
		mlp->flags &= ~(ISDN_NET_CALLBACK | ISDN_NET_CBOUT);
		break;
	case 1:
		mlp->flags |= ISDN_NET_CALLBACK;
		mlp->flags &= ~ISDN_NET_CBOUT;
		break;
	case 2:
		mlp->flags |= ISDN_NET_CBOUT;
		mlp->flags &= ~ISDN_NET_CALLBACK;
		break;
	}
	mlp->flags &= ~ISDN_NET_DIALMODE_MASK;	/* first all bits off */
	if (cfg->dialmode && !(cfg->dialmode & ISDN_NET_DIALMODE_MASK)) {
		retval = -EINVAL;
		goto out;
	}

	mlp->flags |= cfg->dialmode;  /* turn on selected bits */
	if (mlp->flags & ISDN_NET_DM_OFF)
		isdn_net_hangup(idev);

	if (cfg->chargehup)
		mlp->hupflags |= ISDN_CHARGEHUP;
	else
		mlp->hupflags &= ~ISDN_CHARGEHUP;

	if (cfg->ihup)
		mlp->hupflags |= ISDN_INHUP;
	else
		mlp->hupflags &= ~ISDN_INHUP;

	if (cfg->chargeint > 10) {
		idev->chargeint = cfg->chargeint * HZ;
		idev->charge_state = ST_CHARGE_HAVE_CINT;
		mlp->hupflags |= ISDN_MANCHARGE;
	}
	retval = 0;

 out:
	rtnl_unlock();
	
	return retval;
}

/*
 * Perform get-interface-parameters.ioctl
 */
static int
isdn_net_getcfg(isdn_net_ioctl_cfg *cfg)
{
	isdn_net_dev *idev = isdn_net_findif(cfg->name);
	isdn_net_local *mlp;
		
	if (!idev)
		return -ENODEV;

	mlp = idev->mlp;

	strcpy(cfg->eaz, mlp->msn);
	cfg->exclusive = !!idev->exclusive;
	if (idev->pre_device >= 0) {
		sprintf(cfg->drvid, "%s,%d", isdn_drv_drvid(idev->pre_device),
			idev->pre_channel);
	} else {
		cfg->drvid[0] = '\0';
	}
	cfg->onhtime = mlp->onhtime;
	cfg->charge = idev->charge;
	cfg->l2_proto = mlp->l2_proto;
	cfg->l3_proto = mlp->l3_proto;
	cfg->p_encap = mlp->p_encap;
	cfg->secure = (mlp->flags & ISDN_NET_SECURE) ? 1 : 0;
	cfg->callback = 0;
	if (mlp->flags & ISDN_NET_CALLBACK)
		cfg->callback = 1;
	if (mlp->flags & ISDN_NET_CBOUT)
		cfg->callback = 2;
	cfg->cbhup = (mlp->flags & ISDN_NET_CBHUP) ? 1 : 0;
	cfg->dialmode = mlp->flags & ISDN_NET_DIALMODE_MASK;
	cfg->chargehup = (mlp->hupflags & ISDN_CHARGEHUP) ? 1 : 0;
	cfg->ihup = (mlp->hupflags & ISDN_INHUP) ? 1 : 0;
	cfg->cbdelay = mlp->cbdelay * 5 / HZ;
	cfg->dialmax = mlp->dialmax;
	cfg->triggercps = mlp->triggercps;
	cfg->slavedelay = mlp->slavedelay / HZ;
	cfg->chargeint = (mlp->hupflags & ISDN_CHARGEHUP) ?
		(idev->chargeint / HZ) : 0;
	cfg->pppbind = idev->pppbind;
	cfg->dialtimeout = mlp->dialtimeout >= 0 ? mlp->dialtimeout / HZ : -1;
	cfg->dialwait = mlp->dialwait / HZ;

	if (idev->slaves.next != &mlp->slaves)
		strcpy(cfg->slave, list_entry(idev->slaves.next, isdn_net_dev, slaves)->name);
	else
		cfg->slave[0] = '\0';
	if (strcmp(mlp->dev.name, idev->name))
		strcpy(cfg->master, mlp->dev.name);
	else
		cfg->master[0] = '\0';

	return 0;
}

/*
 * Add a phone-number to an interface.
 */
static int
isdn_net_addphone(isdn_net_ioctl_phone *phone)
{
	isdn_net_dev *idev = isdn_net_findif(phone->name);
	struct isdn_net_phone *n;
	int retval = 0;

	if (!idev)
		return -ENODEV;

	rtnl_lock();

	if (netif_running(&idev->mlp->dev)) {
		retval = -EBUSY;
		goto out;
	}
	n = kmalloc(sizeof(*n), GFP_KERNEL);
	if (!n) {
		retval = -ENOMEM;
		goto out;
	}
	strcpy(n->num, phone->phone);
	list_add_tail(&n->list, &idev->mlp->phone[phone->outgoing & 1]);

 out:
	rtnl_unlock();
	return retval;
}

/*
 * Delete a phone-number from an interface.
 */
static int
isdn_net_delphone(isdn_net_ioctl_phone *phone)
{
	isdn_net_dev *idev = isdn_net_findif(phone->name);
	struct isdn_net_phone *n;
	int retval;

	if (!idev)
		return -ENODEV;

	rtnl_lock();

	if (netif_running(&idev->mlp->dev)) {
		retval = -EBUSY;
		goto out;
	}
	retval = -EINVAL;
	list_for_each_entry(n, &idev->mlp->phone[phone->outgoing & 1], list) {
		if (!strcmp(n->num, phone->phone)) {
			list_del(&n->list);
			kfree(n);
			retval = 0;
			break;
		}
	}
 out:
	rtnl_unlock();
	return retval;
}

/*
 * Copy a string of all phone-numbers of an interface to user space.
 */
static int
isdn_net_getphone(isdn_net_ioctl_phone * phone, char *phones)
{
	isdn_net_dev *idev = isdn_net_findif(phone->name);
	u_int count = 0;
	char *buf = (char *)__get_free_page(GFP_KERNEL);
	struct isdn_net_phone *n;

	if (!buf)
		return -ENOMEM;

	if (!idev) {
		count = -ENODEV;
		goto free;
	}
	list_for_each_entry(n, &idev->mlp->phone[phone->outgoing & 1], list) {
		strcpy(&buf[count], n->num);
		count += strlen(n->num);
		buf[count++] = ' ';
		if (count > PAGE_SIZE - ISDN_MSNLEN - 1)
			break;
	}
	if (!count) /* list was empty? */
		count++;

	buf[count-1] = 0;

	if (copy_to_user(phones, buf, count))
		count = -EFAULT;

 free:
	free_page((unsigned long)buf);
	return count;
}

/*
 * Force a net-interface to dial out.
 */
static int
isdn_net_dial_out(char *name)
{
	isdn_net_dev *idev = isdn_net_findif(name);

	if (!idev)
		return -ENODEV;

	return isdn_net_dial(idev);
}

static int
__isdn_net_dial_slave(isdn_net_local *mlp)
{
	isdn_net_dev *idev;

	list_for_each_entry(idev, &mlp->slaves, slaves) {
		if (isdn_net_dial(idev) == 0)
			return 0;
	}
	return -EBUSY;
}

static int
isdn_net_dial_slave(char *name)
{
	isdn_net_dev *idev = isdn_net_findif(name);

	if (!idev)
		return -ENODEV;

	return __isdn_net_dial_slave(idev->mlp);
}

/*
 * Force a hangup of a network-interface.
 */
static int
isdn_net_force_hangup(char *name) // FIXME rename?
{
	isdn_net_dev *idev = isdn_net_findif(name);

	if (!idev)
		return -ENODEV;

	if (idev->isdn_slot == NULL)
		return -ENOTCONN;

	isdn_net_hangup(idev);
	return 0;
}

/*
 * Copy a string containing the peer's phone number of a connected interface
 * to user space.
 */
static int
isdn_net_getpeer(isdn_net_ioctl_phone *phone, isdn_net_ioctl_phone *peer)
{
	isdn_net_dev *idev = isdn_net_findif(phone->name);
	struct isdn_slot *slot;

	if (!idev)
		return -ENODEV;

	if (idev->fi.state != ST_ACTIVE)
		return -ENOTCONN;

	slot = idev->isdn_slot;

	strlcpy(phone->phone, slot->num, sizeof(phone->phone));
	phone->outgoing = USG_OUTGOING(slot->usage);

	if (copy_to_user(peer, phone, sizeof(*peer)))
		return -EFAULT;

	return 0;
}

/*
 * ioctl on /dev/isdnctrl, used to configure ISDN net interfaces 
 */
int
isdn_net_ioctl(struct inode *ino, struct file *file, uint cmd, ulong arg)
{
	/* Save stack space */
	union {
		char name[10];
		char bname[20];
		isdn_net_ioctl_phone phone;
		isdn_net_ioctl_cfg cfg;
	} iocpar;
	int retval;

#define name  iocpar.name
#define bname iocpar.bname
#define phone iocpar.phone
#define cfg   iocpar.cfg

	name[sizeof(name)-1] = 0;
	bname[sizeof(bname)-1] = 0;

	down(&sem);
	
	switch (cmd) {
	case IIOCNETAIF: /* add an interface */
		if (copy_from_user(name, (char *) arg, sizeof(name) - 1)) {
			retval = -EFAULT;
			break;
		}
		retval = isdn_net_addif(name, NULL);
		break;
	case IIOCNETASL: /* add slave to an interface */
		if (copy_from_user(bname, (char *) arg, sizeof(bname) - 1)) {
			retval = -EFAULT;
			break;
		}
		retval = isdn_net_addslave(bname);
		break;
	case IIOCNETDIF: /* delete an interface */
		if (copy_from_user(name, (char *) arg, sizeof(name) - 1)) {
			retval = -EFAULT;
			break;
		}
		retval = isdn_net_delif(name);
		break;
	case IIOCNETSCF: /* set config */
		if (copy_from_user((char *) &cfg, (char *) arg, sizeof(cfg))) {
			retval = -EFAULT;
			break;
		}
		retval = isdn_net_setcfg(&cfg);
		break;
	case IIOCNETGCF: /* get config */
		if (copy_from_user((char *) &cfg, (char *) arg, sizeof(cfg))) {
			retval = -EFAULT;
			break;
		}
		retval = isdn_net_getcfg(&cfg);
		if (retval)
			break;
		if (copy_to_user((char *) arg, (char *) &cfg, sizeof(cfg)))
			retval = -EFAULT;
		break;
	case IIOCNETANM: /* add a phone number */
		if (copy_from_user((char *) &phone, (char *) arg, sizeof(phone))) {
			retval = -EFAULT;
			break;
		}
		retval = isdn_net_addphone(&phone);
		break;
	case IIOCNETGNM: /* get list of phone numbers */
		if (copy_from_user((char *) &phone, (char *) arg, sizeof(phone))) {
			retval = -EFAULT;
			break;
		}
		retval = isdn_net_getphone(&phone, (char *) arg);
		break;
	case IIOCNETDNM: /* delete a phone number */
		if (copy_from_user((char *) &phone, (char *) arg, sizeof(phone))) {
			retval = -EFAULT;
			break;
		}
		retval = isdn_net_delphone(&phone);
		break;
	case IIOCNETDIL: /* trigger dial-out */
		if (copy_from_user(name, (char *) arg, sizeof(name))) {
			retval = -EFAULT;
			break;
		}
		retval = isdn_net_dial_out(name);
		break;
	case IIOCNETHUP: /* hangup */
		if (copy_from_user(name, (char *) arg, sizeof(name))) {
			retval = -EFAULT;
			break;
		}
		retval = isdn_net_force_hangup(name);
		break;
	case IIOCNETGPN: /* Get peer phone number of a connected interface */
		if (copy_from_user((char *) &phone, (char *) arg, sizeof(phone))) {
			retval = -EFAULT;
		}
		retval = isdn_net_getpeer(&phone, (isdn_net_ioctl_phone *) arg);
		break;
	case IIOCNETALN: /* Add link */
		if (copy_from_user(name, (char *) arg, sizeof(name))) {
			retval = -EFAULT;
			break;
		}
		retval = isdn_net_dial_slave(name);
		break;
	case IIOCNETDLN: /* Delete link */
		if (copy_from_user(name, (char *) arg, sizeof(name))) {
			retval = -EFAULT;
			break;
		}
		retval = isdn_net_force_hangup(name);
		break;
	default:
		retval = -ENOTTY;
	}
	up(&sem);
	return retval;

#undef name
#undef bname
#undef iocts
#undef phone
#undef cfg
}

/*
 * Hang up all network-interfaces
 */
void
isdn_net_hangup_all(void)
{
	isdn_net_dev *idev;

	down(&sem);

	list_for_each_entry(idev, &isdn_net_devs, global_list)
		isdn_net_hangup(idev);

	up(&sem);
}

/*
 * Remove all network-interfaces
 */
void
isdn_net_cleanup(void)
{
	isdn_net_dev *idev;
	int retval;

	down(&sem);

	while (!list_empty(&isdn_net_devs)) {
		idev = list_entry(isdn_net_devs.next, isdn_net_dev, global_list);
		retval = isdn_net_dev_delete(idev);
		/* can only fail if an interface is still running.
		 * In this case, an elevated module use count should
		 * have prevented this function from being called in
		 * the first place */
		if (retval)
			isdn_BUG();
	}
	up(&sem);
}

/* ====================================================================== */
/* interface to network layer                                             */
/* ====================================================================== */

static spinlock_t running_devs_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(running_devs);

/* 
 * Open/initialize the board.
 */
static int
isdn_net_open(struct net_device *dev)
{
	isdn_net_local *lp = dev->priv;
	unsigned long flags;
	int retval = 0;

	if (!lp->ops)
		return -ENODEV;

	if (lp->ops->open)
		retval = lp->ops->open(lp);

	if (retval)
		return retval;
	
	netif_start_queue(dev);

	atomic_set(&lp->refcnt, 1);
	spin_lock_irqsave(&running_devs_lock, flags);
	list_add(&lp->running_devs, &running_devs);
	spin_unlock_irqrestore(&running_devs_lock, flags);

	return 0;
}

/*
 * Shutdown a net-interface.
 */
static int
isdn_net_close(struct net_device *dev)
{
	isdn_net_local *lp = dev->priv;
	isdn_net_dev *sdev;
	struct list_head *l, *n;
	unsigned long flags;

	if (lp->ops->close)
		lp->ops->close(lp);

	netif_stop_queue(dev);

	list_for_each_safe(l, n, &lp->slaves) {
		sdev = list_entry(l, isdn_net_dev, slaves);
		isdn_net_hangup(sdev);
	}
	/* The hangup will make the refcnt drop back to
	 * 1 (referenced by list only) soon. */
	spin_lock_irqsave(&running_devs_lock, flags);
	while (atomic_read(&lp->refcnt) != 1) {
		spin_unlock_irqrestore(&running_devs_lock, flags);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ/10);
		spin_lock_irqsave(&running_devs_lock, flags);
	}
	/* We have the only reference and list lock, so
	 * nobody can get another reference. */
	list_del(&lp->running_devs);
	spin_unlock_irqrestore(&running_devs_lock, flags);

	return 0;
}

/*
 * Get statistics
 */
static struct net_device_stats *
isdn_net_get_stats(struct net_device *dev)
{
	isdn_net_local *lp = dev->priv;

	return &lp->stats;
}

/*
 * Transmit timeout
 */
static void
isdn_net_tx_timeout(struct net_device *dev)
{
	printk(KERN_WARNING "isdn_tx_timeout dev %s\n", dev->name);

	netif_wake_queue(dev);
}

/*
 * Interface-setup. (just after registering a new interface)
 */
static int
isdn_init_netif(struct net_device *ndev)
{
	/* Setup the generic properties */

	ndev->mtu = 1500;
	ndev->tx_queue_len = 10;
	ndev->open = &isdn_net_open;
	ndev->hard_header_len = ETH_HLEN + isdn_hard_header_len();
	ndev->stop = &isdn_net_close;
	ndev->get_stats = &isdn_net_get_stats;
	ndev->tx_timeout = isdn_net_tx_timeout;
	ndev->watchdog_timeo = ISDN_NET_TX_TIMEOUT;

	return 0;
}

/* ====================================================================== */
/* call control state machine                                             */
/* ====================================================================== */

// FIXME
static int
isdn_net_is_connected(isdn_net_dev *idev)
{
	return idev->fi.state == ST_ACTIVE;
}

static void
isdn_net_dial_timer(unsigned long data)
{
	isdn_net_dev *idev = (isdn_net_dev *) data;

	isdn_net_handle_event(idev, idev->dial_event, NULL);
}

/*
 * Unbind a net-interface
 */
static void
isdn_net_unbind_channel(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;

	if (idev->isdn_slot == NULL) {
		isdn_BUG();
		return;
	}

	if (mlp->ops->unbind)
		mlp->ops->unbind(idev);

	idev->isdn_slot->priv = NULL;
	idev->isdn_slot->event_cb = NULL;

	skb_queue_purge(&idev->super_tx_queue);

	if (idev->isdn_slot != idev->exclusive)
		isdn_slot_free(idev->isdn_slot);

	idev->isdn_slot = NULL;

	if (idev->fi.state != ST_NULL) {
		lp_put(mlp);
		fsm_change_state(&idev->fi, ST_NULL);
	}
}

static int isdn_net_event_callback(struct isdn_slot *slot, int pr, void *arg);

/*
 * Assign an ISDN-channel to a net-interface
 */
static int
isdn_net_bind_channel(isdn_net_dev *idev, struct isdn_slot *slot)
{
	isdn_net_local *mlp = idev->mlp;
	int retval = 0;

	if (mlp->ops->bind)
		retval = mlp->ops->bind(idev);

	if (retval < 0)
		goto out;

	idev->isdn_slot = slot;
	slot->priv = idev;
	slot->event_cb = isdn_net_event_callback;
	slot->usage |= ISDN_USAGE_NET;

 out:
	return retval;
}

static int
isdn_net_dial(isdn_net_dev *idev)
{
	int retval;

	lp_get(idev->mlp);
	retval = fsm_event(&idev->fi, EV_NET_DO_DIAL, NULL);
	if (retval == -ESRCH) /* event not handled in this state */
		retval = -EBUSY;

	if (retval)
		lp_put(idev->mlp);

	return retval;
}

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

	return isdn_net_dial(idev);
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

int
isdn_net_autodial(struct sk_buff *skb, struct net_device *ndev)
{
	isdn_net_local *mlp = ndev->priv;
	isdn_net_dev *idev = list_entry(mlp->slaves.next, isdn_net_dev, slaves);
	int retval;

	if (ISDN_NET_DIALMODE(*mlp) != ISDN_NET_DM_AUTO)
		goto discard;

	retval = isdn_net_dial(idev);
	if (retval == -ESRCH)
		goto stop_queue;

	if (retval < 0)
		goto discard;

	/* Log packet, which triggered dialing */
	if ((get_isdn_dev())->net_verbose)
		isdn_net_log_skb(skb, idev);

 stop_queue:
	netif_stop_queue(ndev);
	return 1;

 discard:
	isdn_net_unreachable(ndev, skb, "dial rejected");
	dev_kfree_skb(skb);
	return 0;
}

static int
accept_icall(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;
	isdn_net_local *mlp = idev->mlp;
	isdn_ctrl cmd;
	struct isdn_slot *slot = arg;

	isdn_net_bind_channel(idev, slot);
	
	idev->outgoing = 0;
	idev->charge_state = ST_CHARGE_NULL;
	/* Got incoming call, setup L2 and L3 protocols,
	 * then wait for D-Channel-connect
	 */
	cmd.arg = mlp->l2_proto << 8;
	isdn_slot_command(idev->isdn_slot, ISDN_CMD_SETL2, &cmd);
	cmd.arg = mlp->l3_proto << 8;
	isdn_slot_command(idev->isdn_slot, ISDN_CMD_SETL3, &cmd);
	isdn_slot_command(idev->isdn_slot, ISDN_CMD_ACCEPTD, &cmd);
	
	idev->dial_timer.expires = jiffies + mlp->dialtimeout;
	idev->dial_event = EV_NET_TIMER_INCOMING;
	add_timer(&idev->dial_timer);
	fsm_change_state(&idev->fi, ST_IN_WAIT_DCONN);
	return 0;
}

static int
do_callback(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;
	isdn_net_local *mlp = idev->mlp;

	printk(KERN_DEBUG "%s: start callback\n", idev->name);

	idev->dial_timer.expires = jiffies + mlp->cbdelay;
	idev->dial_event = EV_NET_TIMER_CB_IN;
	add_timer(&idev->dial_timer);
	fsm_change_state(&idev->fi, ST_WAIT_BEFORE_CB);

	return 0;
}

static int
isdn_net_dev_icall(isdn_net_dev *idev, struct isdn_slot *slot,
		   int si1, char *eaz, char *nr)
{
	isdn_net_local *mlp = idev->mlp;
	struct isdn_net_phone *ph;
	char *my_eaz;
	
	/* check acceptable call types for DOV */
	dbg_net_icall("n_fi: if='%s', l.msn=%s, l.flags=%#x, l.dstate=%d\n",
		      idev->name, mlp->msn, mlp->flags, idev->fi.state);
	
	my_eaz = isdn_slot_map_eaz2msn(slot, mlp->msn);
	if (si1 == 1) { /* it's a DOV call, check if we allow it */
		if (*my_eaz == 'v' || *my_eaz == 'V' ||
		    *my_eaz == 'b' || *my_eaz == 'B')
			my_eaz++; /* skip to allow a match */
		else
			return 0; /* no match */
	} else { /* it's a DATA call, check if we allow it */
		if (*my_eaz == 'b' || *my_eaz == 'B')
			my_eaz++; /* skip to allow a match */
	}
	/* check called number */
	switch (isdn_msncmp(eaz, my_eaz)) {
	case 1: /* no match */
		return 0;
	case 2: /* matches so far */
		return 5;
	}

	dbg_net_icall("%s: pdev=%d di=%d pch=%d ch = %d\n", idev->name,
		      idev->pre_device, slot->di, idev->pre_channel, slot->ch);
	
	/* check if exclusive */
	if ((slot->usage & ISDN_USAGE_EXCLUSIVE) &&
	    (idev->pre_channel != slot->ch || idev->pre_device != slot->di)) {
		dbg_net_icall("%s: excl check failed\n", idev->name);
		return 0;
	}
	
	/* check calling number */
	dbg_net_icall("%s: secure\n", idev->name);
	if (mlp->flags & ISDN_NET_SECURE) {
		list_for_each_entry(ph, &mlp->phone[0], list) {
			if (isdn_msncmp(nr, ph->num) == 0)
					goto found;
		}
		return 0;
	}
 found:
	/* check dial mode */
	if (ISDN_NET_DIALMODE(*mlp) == ISDN_NET_DM_OFF) {
		printk(KERN_INFO "%s: incoming call, stopped -> rejected\n",
		       idev->name);
		return 3;
	}
	lp_get(mlp);
	/* check callback */
	if (mlp->flags & ISDN_NET_CALLBACK) {
		if (fsm_event(&idev->fi, EV_NET_DO_CALLBACK, NULL)) {
			lp_put(mlp);
			return 0;
		}
		/* Initiate dialing by returning 2 or 4 */
		return (mlp->flags & ISDN_NET_CBHUP) ? 2 : 4;
	}
	printk(KERN_INFO "%s: call from %s -> %s accepted\n",
	       idev->name, nr, eaz);

	if (fsm_event(&idev->fi, EV_NET_DO_ACCEPT, slot)) {
		lp_put(mlp);
		return 0;
	}
	return 1; // accepted
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
isdn_net_find_icall(struct isdn_slot *slot, setup_parm *setup)
{
	isdn_net_local	*lp;
	isdn_net_dev	*idev;
	char		*nr, *eaz;
	unsigned char	si1, si2;
	int		retval;
	int		verbose = (get_isdn_dev())->net_verbose;
	unsigned long	flags;

	/* fix up calling number */
	if (!setup->phone[0]) {
		printk(KERN_INFO
		       "isdn_net: Incoming call without OAD, assuming '0'\n");
		nr = "0";
	} else {
		nr = setup->phone;
	}
	/* fix up called number */
	if (!setup->eazmsn[0]) {
		printk(KERN_INFO
		       "isdn_net: Incoming call without CPN, assuming '0'\n");
		eaz = "0";
	} else {
		eaz = setup->eazmsn;
	}
	si1 = setup->si1;
	si2 = setup->si2;
	if (verbose > 1)
		printk(KERN_INFO "isdn_net: call from %s,%d,%d -> %s\n", 
		       nr, si1, si2, eaz);
	/* check service indicator */
        /* Accept DATA and VOICE calls at this stage
	   local eaz is checked later for allowed call types */
        if ((si1 != 7) && (si1 != 1)) {
                if (verbose > 1)
                        printk(KERN_INFO "isdn_net: "
			       "Service-Indicator not 1 or 7, ignored\n");
                return 0;
        }

	dbg_net_icall("n_fi: di=%d ch=%d usg=%#x\n", slot->di, slot->ch,
		      slot->usage);

	retval = 0;
	spin_lock_irqsave(&running_devs_lock, flags);
	list_for_each_entry(lp, &running_devs, running_devs) {
		lp_get(lp);
		spin_unlock_irqrestore(&running_devs_lock, flags);

		list_for_each_entry(idev, &lp->slaves, slaves) {
			retval = isdn_net_dev_icall(idev, slot, si1, eaz, nr);
			if (retval > 0)
				break;
		}

		spin_lock_irqsave(&running_devs_lock, flags);
		lp_put(lp);
		if (retval > 0)
			break;
		
	}
	spin_unlock_irqrestore(&running_devs_lock, flags);
	if (!retval) {
		if (verbose)
			printk(KERN_INFO "isdn_net: call "
			       "from %s -> %s ignored\n", nr, eaz);
	}
	return retval;
}

/* ---------------------------------------------------------------------- */
/* callbacks in the state machine                                         */
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

static int dialout_next(struct fsm_inst *fi, int pr, void *arg);

/* Initiate dialout. */

static int
do_dial(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;
	isdn_net_local *mlp = idev->mlp;
	struct isdn_slot *slot;

	if (ISDN_NET_DIALMODE(*mlp) == ISDN_NET_DM_OFF)
		return -EPERM;

	if (list_empty(&mlp->phone[1])) /* no number to dial ? */
		return -EINVAL;

	if (idev->exclusive)
		slot = idev->exclusive;
	else
		slot = isdn_get_free_slot(ISDN_USAGE_NET, mlp->l2_proto,
					  mlp->l3_proto, idev->pre_device, 
					  idev->pre_channel, mlp->msn);
	if (!slot)
		return -EAGAIN;

	if (isdn_net_bind_channel(idev, slot) < 0) {
		/* has freed the slot as well */
		return -EAGAIN;
	}

	fsm_change_state(fi, ST_OUT_BOUND);

	idev->dial = 0;
	idev->dialretry = 0;

	dialout_next(fi, pr, arg);
	return 0;
}

/* Try dialing the next number. */

static int
dialout_next(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;
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
	fsm_change_state(&idev->fi, ST_OUT_WAIT_DCONN);
	add_timer(&idev->dial_timer);

	/* Dial */
	isdn_slot_dial(idev->isdn_slot, &dial);
	return 0;
}

/* If we didn't connect within dialtimeout, we give up for now
 * and wait for dialwait jiffies before trying again.
 */
static int
dial_timeout(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;
	isdn_net_local *mlp = idev->mlp;
	isdn_ctrl cmd;

	fsm_change_state(&idev->fi, ST_OUT_DIAL_WAIT);
	isdn_slot_command(idev->isdn_slot, ISDN_CMD_HANGUP, &cmd);
	
	/* get next phone number */
	if (!get_outgoing_phone(idev)) {
		/* otherwise start over at first entry */
		idev->dial = 0;
		idev->dialretry++;
	}
	if (idev->dialretry >= mlp->dialmax) {
		isdn_net_hangup(idev);
		return 0;
	}
	idev->dial_event = EV_NET_TIMER_DIAL_WAIT;
	mod_timer(&idev->dial_timer, jiffies + mlp->dialwait);
	return 0;
}

static int
connect_fail(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;

	del_timer(&idev->dial_timer);
	printk(KERN_INFO "%s: connection failed\n", idev->name);
	isdn_net_unbind_channel(idev);
	return 0;
}

static int
out_dconn(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;
	isdn_ctrl cmd;

	fsm_change_state(&idev->fi, ST_OUT_WAIT_BCONN);
	isdn_slot_command(idev->isdn_slot, ISDN_CMD_ACCEPTB, &cmd);
	return 0;
}

static int
in_dconn(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;
	isdn_ctrl cmd;

	fsm_change_state(&idev->fi, ST_IN_WAIT_BCONN);
	isdn_slot_command(idev->isdn_slot, ISDN_CMD_ACCEPTB, &cmd);
	return 0;
}

static int
bconn(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;
	isdn_net_local *mlp = idev->mlp;

	fsm_change_state(&idev->fi, ST_ACTIVE);

	if (mlp->onhtime) {
		idev->huptimer = 0;
		idev->dial_event = EV_NET_TIMER_HUP;
		mod_timer(&idev->dial_timer, jiffies + HZ);
	} else {
		del_timer(&idev->dial_timer);
	}

	printk(KERN_INFO "%s connected\n", idev->name);
	/* If first Chargeinfo comes before B-Channel connect,
	 * we correct the timestamp here.
	 */
	idev->chargetime = jiffies;
	idev->frame_cnt = 0;
	idev->transcount = 0;
	idev->cps = 0;
	idev->last_jiffies = jiffies;

	if (mlp->ops->connected)
		mlp->ops->connected(idev);
	else
		isdn_net_online(idev);
       
	return 0;
}

static int
bhup(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;
	isdn_net_local *mlp = idev->mlp;

	del_timer(&idev->dial_timer);
	if (mlp->ops->disconnected)
		mlp->ops->disconnected(idev);
	else 
		isdn_net_offline(idev);

	printk(KERN_INFO "%s: disconnected\n", idev->name);
	fsm_change_state(fi, ST_WAIT_DHUP);
	return 0;
}

static int
dhup(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;

	printk(KERN_INFO "%s: Chargesum is %d\n", idev->name, idev->charge);
	isdn_net_unbind_channel(idev);
	return 0;
}

/* Check if it's time for idle hang-up */

static int
check_hup(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;
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
	if (idev->outgoing || mlp->hupflags & ISDN_INHUP) {
		isdn_net_hangup(idev);
		return 0;
	}
 mod_timer:
	mod_timer(&idev->dial_timer, idev->dial_timer.expires + HZ);
	return 0;
}

/* Charge-info from TelCo. */

static int
got_cinf(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;

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
	return 0;
}

/* Perform hangup for a net-interface. */

int
isdn_net_hangup(isdn_net_dev *idev)
{
	isdn_ctrl cmd;

	del_timer(&idev->dial_timer);

	printk(KERN_INFO "%s: local hangup\n", idev->name);
	// FIXME via state machine
	if (idev->isdn_slot)
		isdn_slot_command(idev->isdn_slot, ISDN_CMD_HANGUP, &cmd);
	return 1;
}

static int isdn_net_rcv_skb(struct isdn_slot *slot, struct sk_buff *skb);

/*
 * Handle status-messages from ISDN-interfacecard.
 * This function is called from within the main-status-dispatcher
 * isdn_status_callback, which itself is called from the low-level driver.
 */
static int
isdn_net_event_callback(struct isdn_slot *slot, int pr, void *arg)
{
	isdn_net_dev *idev = slot->priv;

	if (!idev) {
		isdn_BUG();
		return 0;
	}
	switch (pr) {
	case EV_DATA_IND:
		return isdn_net_rcv_skb(slot, arg);
	case EV_STAT_DCONN:
		return fsm_event(&idev->fi, EV_NET_STAT_DCONN, arg);
	case EV_STAT_BCONN:
		return fsm_event(&idev->fi, EV_NET_STAT_BCONN, arg);
	case EV_STAT_BHUP:
		return fsm_event(&idev->fi, EV_NET_STAT_BHUP, arg);
	case EV_STAT_DHUP:
		return fsm_event(&idev->fi, EV_NET_STAT_DHUP, arg);
	case EV_STAT_CINF:
		return fsm_event(&idev->fi, EV_NET_STAT_CINF, arg);
	case EV_STAT_BSENT:
		return fsm_event(&idev->fi, EV_NET_STAT_BSENT, arg);
	default:
		printk("unknown pr %d\n", pr);
		return 0;
	}
}

static int
isdn_net_handle_event(isdn_net_dev *idev, int pr, void *arg)
{
	return fsm_event(&idev->fi, pr, arg);
}

static int
hang_up(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;

	isdn_net_hangup(idev);
	return 0;
}

static int
got_bsent(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;
	isdn_ctrl *c = arg;
	
	isdn_net_bsent(idev, c);
	return 0;
}

static struct fsm_node isdn_net_fn_tbl[] = {
	{ ST_NULL,           EV_NET_DO_DIAL,         do_dial       },
	{ ST_NULL,           EV_NET_DO_ACCEPT,       accept_icall  },
	{ ST_NULL,           EV_NET_DO_CALLBACK,     do_callback   },

	{ ST_OUT_WAIT_DCONN, EV_NET_TIMER_DIAL,      dial_timeout  },
	{ ST_OUT_WAIT_DCONN, EV_NET_STAT_DCONN,      out_dconn     },
	{ ST_OUT_WAIT_DCONN, EV_NET_STAT_DHUP,       connect_fail  },
	{ ST_OUT_WAIT_DCONN, EV_NET_TIMER_CB_OUT,    hang_up       },

	{ ST_OUT_WAIT_BCONN, EV_NET_TIMER_DIAL,      dial_timeout  },
	{ ST_OUT_WAIT_BCONN, EV_NET_STAT_BCONN,      bconn         },
	{ ST_OUT_WAIT_BCONN, EV_NET_STAT_DHUP,       connect_fail  },

	{ ST_IN_WAIT_DCONN,  EV_NET_TIMER_INCOMING,  hang_up       },
	{ ST_IN_WAIT_DCONN,  EV_NET_STAT_DCONN,      in_dconn      },
	{ ST_IN_WAIT_DCONN,  EV_NET_STAT_DHUP,       connect_fail  },

	{ ST_IN_WAIT_BCONN,  EV_NET_TIMER_INCOMING,  hang_up       },
	{ ST_IN_WAIT_BCONN,  EV_NET_STAT_BCONN,      bconn         },
	{ ST_IN_WAIT_BCONN,  EV_NET_STAT_DHUP,       connect_fail  },

	{ ST_ACTIVE,         EV_NET_TIMER_HUP,       check_hup     },
	{ ST_ACTIVE,         EV_NET_STAT_BHUP,       bhup          },
	{ ST_ACTIVE,         EV_NET_STAT_CINF,       got_cinf      },
	{ ST_ACTIVE,         EV_NET_STAT_BSENT,      got_bsent     },

	{ ST_WAIT_DHUP,      EV_NET_STAT_DHUP,       dhup          },

	{ ST_WAIT_BEFORE_CB, EV_NET_TIMER_CB_IN,     do_dial       },

	{ ST_OUT_DIAL_WAIT,  EV_NET_TIMER_DIAL_WAIT, dialout_next  },
};

static struct fsm isdn_net_fsm = {
	.st_cnt = ARRAY_SIZE(isdn_net_st_str),
	.st_str = isdn_net_st_str,
	.ev_cnt = ARRAY_SIZE(isdn_net_ev_str),
	.ev_str = isdn_net_ev_str,
	.fn_cnt = ARRAY_SIZE(isdn_net_fn_tbl),
	.fn_tbl = isdn_net_fn_tbl,
};

static void isdn_net_dev_debug(struct fsm_inst *fi, char *fmt, ...)
{
	va_list args;
	isdn_net_dev *idev = fi->userdata;
	char buf[128];
	char *p = buf;

	va_start(args, fmt);
	p += sprintf(p, "%s: ", idev->name);
	p += vsprintf(p, fmt, args);
	va_end(args);
	printk(KERN_DEBUG "%s\n", buf);
}

/* ====================================================================== */
/* xmit path                                                              */
/* ====================================================================== */

#define ISDN_NET_MAX_QUEUE_LENGTH 2

/*
 * is this particular channel busy?
 */
static inline int
isdn_net_dev_busy(isdn_net_dev *idev)
{
	return idev->frame_cnt >= ISDN_NET_MAX_QUEUE_LENGTH;
}

/*
 * find out if the net_device which this mlp is belongs to is busy.
 * It's busy iff all channels are busy.
 * must hold mlp->xmit_lock
 * FIXME: Use a mlp->frame_cnt instead of loop?
 */
static inline int
isdn_net_local_busy(isdn_net_local *mlp)
{
	isdn_net_dev *idev;

	list_for_each_entry(idev, &mlp->online, online) {
		if (!isdn_net_dev_busy(idev))
			return 0;
	}
	return 1;
}

/*
 * For the given net device, this will get a non-busy channel out of the
 * corresponding bundle.
 * must hold mlp->xmit_lock
 */
isdn_net_dev *
isdn_net_get_xmit_dev(isdn_net_local *mlp)
{
	isdn_net_dev *idev;

	list_for_each_entry(idev, &mlp->online, online) {
		if (!isdn_net_dev_busy(idev)) {
			/* point the head to next online channel */
			list_del(&mlp->online);
			list_add(&mlp->online, &idev->online);
			return idev;
		}
	}
	return NULL;
}

/* mlp->xmit_lock must be held */
static inline void
isdn_net_inc_frame_cnt(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;

	if (isdn_net_dev_busy(idev))
		isdn_BUG();
		
	idev->frame_cnt++;
	if (isdn_net_local_busy(mlp))
		netif_stop_queue(&mlp->dev);
}

/* mlp->xmit_lock must be held */
static inline void
isdn_net_dec_frame_cnt(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;

	idev->frame_cnt--;

	if (isdn_net_dev_busy(idev))
		isdn_BUG();

	if (!skb_queue_empty(&idev->super_tx_queue))
		tasklet_schedule(&idev->tlet);
	else
		netif_wake_queue(&mlp->dev);
}

static void
isdn_net_tasklet(unsigned long data)
{
	isdn_net_dev *idev = (isdn_net_dev *) data;
	isdn_net_local *mlp = idev->mlp;
	struct sk_buff *skb;
	unsigned long flags;

	spin_lock_irqsave(&mlp->xmit_lock, flags);
	while (!isdn_net_dev_busy(idev) &&
	       (skb = skb_dequeue(&idev->super_tx_queue))) {
		isdn_net_writebuf_skb(idev, skb);
	}
	spin_unlock_irqrestore(&mlp->xmit_lock, flags);
}

/* We're good to accept (IP/whatever) traffic now */

void
isdn_net_online(isdn_net_dev *idev)
{
	// FIXME check we're connected
	isdn_net_local *mlp = idev->mlp;
	unsigned long flags;

	spin_lock_irqsave(&mlp->xmit_lock, flags);
	list_add(&idev->online, &mlp->online);
	spin_unlock_irqrestore(&mlp->xmit_lock, flags);

	netif_wake_queue(&mlp->dev);
}

/* No more (IP/whatever) traffic over the net interface */

void
isdn_net_offline(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;
	unsigned long flags;

	spin_lock_irqsave(&mlp->xmit_lock, flags);
	list_del(&idev->online);
	spin_unlock_irqrestore(&mlp->xmit_lock, flags);
	
	skb_queue_purge(&idev->super_tx_queue);
}

/* 
 * all frames sent from the (net) LL to a HL driver should go via this function
 * must hold mlp->xmit_lock
 */
void
isdn_net_writebuf_skb(isdn_net_dev *idev, struct sk_buff *skb)
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

	if (!isdn_net_is_connected(idev)) {
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

/* A packet has successfully been sent out. */

static int
isdn_net_bsent(isdn_net_dev *idev, isdn_ctrl *c)
{
	isdn_net_local *mlp = idev->mlp;
	unsigned long flags;

	spin_lock_irqsave(&mlp->xmit_lock, flags);
	isdn_net_dec_frame_cnt(idev);
	spin_unlock_irqrestore(&mlp->xmit_lock, flags);
	mlp->stats.tx_packets++;
	mlp->stats.tx_bytes += c->parm.length;
	return 1;
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
	isdn_net_dev	*idev;
	isdn_net_local	*mlp = ndev->priv;
	unsigned long	flags;
	int		retval;

	ndev->trans_start = jiffies;

	spin_lock_irqsave(&mlp->xmit_lock, flags);

	if (list_empty(&mlp->online)) {
		retval = isdn_net_autodial(skb, ndev);
		goto out;
	}

	idev = isdn_net_get_xmit_dev(mlp);
	if (!idev) {
		printk(KERN_INFO "%s: all channels busy - requeuing!\n", ndev->name);
		netif_stop_queue(ndev);
		retval = 1;
		goto out;
	}

	isdn_net_writebuf_skb(idev, skb);

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
	if ((get_isdn_dev())->net_verbose > 3)
		printk(KERN_DEBUG "%s: %d bogocps\n", idev->name, idev->cps);

	if (idev->cps > mlp->triggercps) {
		if (!idev->sqfull) {
			/* First time overload: set timestamp only */
			idev->sqfull = 1;
			idev->sqfull_stamp = jiffies;
		} else {
			/* subsequent overload: if slavedelay exceeded, start dialing */
			if (time_after(jiffies, idev->sqfull_stamp + mlp->slavedelay)) {
				if (ISDN_NET_DIALMODE(*mlp) == ISDN_NET_DM_AUTO)
					__isdn_net_dial_slave(mlp);
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

	retval = 0;
 out:
	spin_unlock_irqrestore(&mlp->xmit_lock, flags);
	return retval;
}

/*
 * this function is used to send supervisory data, i.e. data which was
 * not received from the network layer, but e.g. frames from ipppd, CCP
 * reset frames etc.
 * must hold mlp->xmit_lock
 */
void
isdn_net_write_super(isdn_net_dev *idev, struct sk_buff *skb)
{
	if (!isdn_net_dev_busy(idev)) {
		isdn_net_writebuf_skb(idev, skb);
	} else {
		skb_queue_tail(&idev->super_tx_queue, skb);
	}
}

/* ====================================================================== */
/* receive path                                                           */
/* ====================================================================== */

/*
 * A packet arrived via ISDN. Search interface-chain for a corresponding
 * interface. If found, deliver packet to receiver-function and return 1,
 * else return 0.
 */
static int
isdn_net_rcv_skb(struct isdn_slot *slot, struct sk_buff *skb)
{
	isdn_net_dev *idev = slot->priv;
	isdn_net_local *mlp;

	if (!idev) {
		isdn_BUG();
		return 0;
	}
	if (!isdn_net_is_connected(idev)) {
		isdn_BUG();
		return 0;
	}

	mlp = idev->mlp;

	idev->transcount += skb->len;

	mlp->stats.rx_packets++;
	mlp->stats.rx_bytes += skb->len;
	skb->dev = &mlp->dev;
	skb->pkt_type = PACKET_HOST;
	isdn_dumppkt("R:", skb->data, skb->len, 40);

	mlp->ops->receive(mlp, idev, skb);

	return 1;
}

/*
 * After handling connection-type specific stuff, the receiver function
 * can use this function to pass the skb on to the network layer.
 */
void
isdn_netif_rx(isdn_net_dev *idev, struct sk_buff *skb, u16 protocol)
{
	idev->huptimer = 0;

	skb->protocol = protocol;
	skb->dev = &idev->mlp->dev;
	netif_rx(skb);
}

/* ====================================================================== */
/* init / exit                                                            */
/* ====================================================================== */

void
isdn_net_lib_init(void)
{
	fsm_new(&isdn_net_fsm);

#ifdef CONFIG_ISDN_NET_SIMPLE
	register_isdn_netif(ISDN_NET_ENCAP_ETHER,      &isdn_ether_ops);
	register_isdn_netif(ISDN_NET_ENCAP_RAWIP,      &isdn_rawip_ops);
	register_isdn_netif(ISDN_NET_ENCAP_IPTYP,      &isdn_iptyp_ops);
	register_isdn_netif(ISDN_NET_ENCAP_UIHDLC,     &isdn_uihdlc_ops);
#endif
#ifdef CONFIG_ISDN_NET_CISCO
	register_isdn_netif(ISDN_NET_ENCAP_CISCOHDLC,  &isdn_ciscohdlck_ops);
	register_isdn_netif(ISDN_NET_ENCAP_CISCOHDLCK, &isdn_ciscohdlck_ops);
#endif
#ifdef CONFIG_ISDN_X25
	register_isdn_netif(ISDN_NET_ENCAP_X25IFACE,   &isdn_x25_ops);
#endif
#ifdef CONFIG_ISDN_PPP
	register_isdn_netif(ISDN_NET_ENCAP_SYNCPPP,    &isdn_ppp_ops);
#endif
}

void
isdn_net_lib_exit(void)
{
	fsm_free(&isdn_net_fsm);
}
