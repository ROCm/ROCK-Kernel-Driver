/*
 * Linux ISDN subsystem, Network interface configuration
 *
 * Copyright 1994-1998  by Fritz Elfert (fritz@isdn4linux.de)
 *           1995,96    by Thinking Objects Software GmbH Wuerzburg
 *           1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *           1999-2002  by Kai Germaschewski <kai@germaschewski.name>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/capability.h>
#include <linux/rtnetlink.h>
#include "isdn_common.h"
#include "isdn_net.h"
#include "isdn_ppp.h"
#include "isdn_fsm.h"

#define ISDN_NET_TX_TIMEOUT (20*HZ) 

/* All of this configuration code is globally serialized */

static DECLARE_MUTEX(sem);
LIST_HEAD(isdn_net_devs); /* Linked list of isdn_net_dev's */ // FIXME static

int isdn_net_handle_event(isdn_net_dev *idev, int pr, void *arg); /* FIXME */

static void isdn_net_tasklet(unsigned long data);
static void isdn_net_dial_timer(unsigned long data);
static int isdn_init_netif(struct net_device *ndev);
static void isdn_net_dev_debug(struct fsm_inst *fi, char *fmt, ...);

static struct fsm isdn_net_fsm;

enum {
	ST_NULL,
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
	EV_TIMER_INCOMING,
	EV_TIMER_DIAL,
	EV_TIMER_DIAL_WAIT,
	EV_TIMER_CB_OUT,
	EV_TIMER_CB_IN,
	EV_TIMER_HUP,
	EV_STAT_DCONN,
	EV_STAT_BCONN,
	EV_STAT_DHUP,
	EV_STAT_BHUP,
	EV_STAT_CINF,
	EV_STAT_BSENT,
	EV_CMD_DIAL,
};

static char *isdn_net_ev_str[] = {
	"EV_NET_TIMER_INCOMING",
	"EV_NET_TIMER_DIAL",
	"EV_NET_TIMER_DIAL_WAIT",
	"EV_NET_TIMER_CB_OUT",
	"EV_NET_TIMER_CB_IN",
	"EV_NET_TIMER_HUP",
	"EV_STAT_DCONN",
	"EV_STAT_BCONN",
	"EV_STAT_DHUP",
	"EV_STAT_BHUP",
	"EV_STAT_CINF",
	"EV_STAT_BSENT",
	"EV_CMD_DIAL",
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

	if (encap < 0 || encap >= ISDN_NET_ENCAP_NR) {
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
	int i, retval;
	int drvidx = -1;
	int chidx = -1;
	char drvid[25];

	strncpy(drvid, cfg->drvid, 24);
	drvid[24] = 0;

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

		for (i = 0; i < ISDN_MAX_DRIVERS; i++) {
			/* Lookup driver-Id in array */
			if (!strcmp(dev->drvid[i], drvid)) {
				drvidx = i;
				break;
			}
		}
		if (drvidx == -1 || chidx == -1) {
			/* Either driver-Id or channel-number invalid */
			retval = -ENODEV;
			goto out;
		}
	}
	if (cfg->exclusive == (idev->exclusive >= 0) &&
	    drvidx == idev->pre_device && chidx == idev->pre_channel) {
		/* no change */
		retval = 0;
		goto out;
	}
	if (idev->exclusive >= 0) {
		isdn_unexclusive_channel(idev->pre_device, idev->pre_channel);
		isdn_free_channel(idev->pre_device, idev->pre_channel, ISDN_USAGE_NET);
		idev->exclusive = -1;
	}
	if (cfg->exclusive) {
		/* If binding is exclusive, try to grab the channel */
		idev->exclusive = isdn_get_free_slot(ISDN_USAGE_NET, mlp->l2_proto, 
						     mlp->l3_proto, drvidx, chidx, cfg->eaz);
		if (idev->exclusive < 0) {
			/* Grab failed, because desired channel is in use */
			retval = -EBUSY;
			goto out;
		}
		/* All went ok, so update isdninfo */
		isdn_slot_set_usage(idev->exclusive, ISDN_USAGE_EXCLUSIVE);
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
	spin_lock_init(&idev->xmit_lock);
	skb_queue_head_init(&idev->super_tx_queue);

	idev->isdn_slot = -1;
	idev->pre_device = -1;
	idev->pre_channel = -1;
	idev->exclusive = -1;

	idev->ppp_slot = -1;
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

		mlp->p_encap = -1;
		isdn_net_set_encap(mlp, ISDN_NET_ENCAP_RAWIP);

		mlp->l2_proto = ISDN_PROTO_L2_X75I;
		mlp->l3_proto = ISDN_PROTO_L3_TRANS;
		mlp->triggercps = 6000;
		mlp->slavedelay = 10 * HZ;
		mlp->hupflags = ISDN_INHUP;
		mlp->onhtime = 10;
		mlp->dialmax = 1;
		mlp->flags = ISDN_NET_CBHUP | ISDN_NET_DM_MANUAL;
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

	if (netif_running(&mlp->dev))
		return -EBUSY;

	retval = isdn_net_addif(p, mlp);
	
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

	if (idev->exclusive >= 0)
		isdn_unexclusive_channel(idev->pre_device, idev->pre_channel);

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
	ulong features;
	int i, retval;

	if (!idev)
		return -ENODEV;

	mlp = idev->mlp;

	rtnl_lock();

	if (netif_running(&mlp->dev)) {
		retval = -EBUSY;
		goto out;
	}
	/* See if any registered driver supports the features we want */
	features = ((1 << cfg->l2_proto) << ISDN_FEATURE_L2_SHIFT) |
		   ((1 << cfg->l3_proto) << ISDN_FEATURE_L3_SHIFT);
	for (i = 0; i < ISDN_MAX_DRIVERS; i++)
		if (dev->drv[i] &&
		    (dev->drv[i]->interface->features & features) == features)
				break;

	if (i == ISDN_MAX_DRIVERS) {
		printk(KERN_WARNING "isdn_net: No driver with selected features\n");
		retval = -ENODEV;
		goto out;
	}

	retval = isdn_net_set_encap(mlp, cfg->p_encap);
	if (retval)
		goto out;

	retval = isdn_net_bind(idev, cfg);
	if (retval)
		goto out;

	strncpy(mlp->msn, cfg->eaz, ISDN_MSNLEN-1);
	mlp->msn[ISDN_MSNLEN-1] = 0;
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
	cfg->exclusive = idev->exclusive >= 0;
	if (idev->pre_device >= 0) {
		sprintf(cfg->drvid, "%s,%d", dev->drvid[idev->pre_device],
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
	int count = 0;
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

/*
 * Force a hangup of a network-interface.
 */
static int
isdn_net_force_hangup(char *name) // FIXME rename?
{
	isdn_net_dev *idev = isdn_net_findif(name);

	if (!idev)
		return -ENODEV;

	if (idev->isdn_slot < 0)
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
	int idx;

	if (!idev)
		return -ENODEV;
	/* FIXME
	 * Theoretical race: while this executes, the remote number might
	 * become invalid (hang up) or change (new connection), resulting
         * in (partially) wrong number copied to user. This race
	 * currently ignored.
	 */
	idx = idev->isdn_slot;
	if (idx < 0)
		return -ENOTCONN;
	/* for pre-bound channels, we need this extra check */
	if (strncmp(isdn_slot_num(idx), "???", 3) == 0 )
		return -ENOTCONN;

	strncpy(phone->phone, isdn_slot_num(idx), ISDN_MSNLEN);
	phone->outgoing = USG_OUTGOING(isdn_slot_usage(idx));

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
#ifdef CONFIG_ISDN_PPP
	case IIOCNETALN: /* Add link */
		if (copy_from_user(name, (char *) arg, sizeof(name))) {
			retval = -EFAULT;
			break;
		}
		retval = isdn_ppp_dial_slave(name);
		break;
	case IIOCNETDLN: /* Delete link */
		if (copy_from_user(name, (char *) arg, sizeof(name))) {
			retval = -EFAULT;
			break;
		}
		retval = isdn_ppp_hangup_slave(name);
		break;
#endif
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
isdn_net_exit(void)
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

	// FIXME
	isdn_net_lib_exit();
}

/* ====================================================================== */
/* interface to network layer                                             */
/* ====================================================================== */

/* 
 * Open/initialize the board.
 */
static int
isdn_net_open(struct net_device *dev)
{
	isdn_net_local *lp = dev->priv;
	int retval = 0;

	if (!lp->ops)
		return -ENODEV;

	if (lp->ops->open)
		retval = lp->ops->open(lp);

	if (!retval)
		return retval;
	
	netif_start_queue(dev);
	return 0;
}

/*
 * Shutdown a net-interface.
 */
// FIXME share?
static int
isdn_net_close(struct net_device *dev)
{
	isdn_net_local *lp = dev->priv;
	struct list_head *l, *n;
	isdn_net_dev *sdev;

	if (lp->ops->close)
		lp->ops->close(lp);

	netif_stop_queue(dev);

	list_for_each_safe(l, n, &lp->online) {
		sdev = list_entry(l, isdn_net_dev, online);
		isdn_net_hangup(sdev);
	}
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

static void
isdn_net_tasklet(unsigned long data)
{
	isdn_net_dev *idev = (isdn_net_dev *) data;
	struct sk_buff *skb;

	spin_lock_bh(&idev->xmit_lock);
	while (!isdn_net_dev_busy(idev)) {
		skb = skb_dequeue(&idev->super_tx_queue);
		if (!skb)
			break;
		isdn_net_writebuf_skb(idev, skb);
	}
	spin_unlock_bh(&idev->xmit_lock);
}

/* ====================================================================== */
/* call control state machine                                             */
/* ====================================================================== */

// FIXME
int isdn_net_online(isdn_net_dev *idev)
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
 * Assign an ISDN-channel to a net-interface
 */
int
isdn_net_bind_channel(isdn_net_dev *idev, int slot)
{
	isdn_net_local *mlp = idev->mlp;
	int retval = 0;
	unsigned long flags;

	save_flags(flags);
	cli();

	idev->isdn_slot = slot;
	isdn_slot_set_idev(idev->isdn_slot, idev);

	if (mlp->ops->bind)
		retval = mlp->ops->bind(idev);

	if (retval < 0)
		isdn_net_unbind_channel(idev);

	restore_flags(flags);
	return retval;
}

/*
 * Unbind a net-interface
 */
void
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

	fsm_change_state(&idev->fi, ST_NULL);

	isdn_slot_set_idev(idev->isdn_slot, NULL);
	isdn_slot_free(idev->isdn_slot, ISDN_USAGE_NET);

	idev->isdn_slot = -1;

	restore_flags(flags);
}

int
isdn_net_dial(isdn_net_dev *idev)
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
	fsm_event(&idev->fi, EV_CMD_DIAL, NULL);
	return 0;

 err:
	return -EAGAIN;
}

void
isdn_net_accept(isdn_net_dev *idev, int slot, char *nr)
{
	isdn_net_local *mlp = idev->mlp;
	isdn_ctrl cmd;

	strcpy(isdn_slot_num(slot), nr);
	isdn_slot_set_usage(slot, (isdn_slot_usage(slot) & ISDN_USAGE_EXCLUSIVE) | ISDN_USAGE_NET);
	
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
	
	idev->dial_timer.expires = jiffies + mlp->dialtimeout;
	idev->dial_event = EV_TIMER_INCOMING;
	add_timer(&idev->dial_timer);
	fsm_change_state(&idev->fi, ST_IN_WAIT_DCONN);
}

int
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
	idev->dial_event = EV_TIMER_CB_IN;
	add_timer(&idev->dial_timer);
	fsm_change_state(&idev->fi, ST_WAIT_BEFORE_CB);

	/* Initiate dialing by returning 2 or 4 */
	return (mlp->flags & ISDN_NET_CBHUP) ? 2 : 4;

 err:
	return 0;
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

static void dialout_next(struct fsm_inst *fi, int pr, void *arg);

/* Initiate dialout. */

static void
dialout_first(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;
	isdn_net_local *mlp = idev->mlp;

	if (ISDN_NET_DIALMODE(*mlp) == ISDN_NET_DM_OFF) {
		isdn_net_unbind_channel(idev);
		return;
	}
	if (list_empty(&mlp->phone[1])) {
		isdn_net_unbind_channel(idev);
		return;
	}

	idev->dial = 0;
	idev->dialretry = 0;
	dialout_next(fi, pr, arg);
}

/* Try dialing the next number. */

static void
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
		idev->dial_event = EV_TIMER_CB_OUT;
	} else {
		idev->dial_timer.expires = jiffies + mlp->dialtimeout;
		idev->dial_event = EV_TIMER_DIAL;
	}
	fsm_change_state(&idev->fi, ST_OUT_WAIT_DCONN);
	add_timer(&idev->dial_timer);

	/* Dial */
	isdn_slot_dial(idev->isdn_slot, &dial);
}

/* If we didn't connect within dialtimeout, we give up for now
 * and wait for dialwait jiffies before trying again.
 */
static void
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
		return;
	}
	idev->dial_event = EV_TIMER_DIAL_WAIT;
	mod_timer(&idev->dial_timer, jiffies + mlp->dialwait);
}

static void
connect_fail(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;

	del_timer(&idev->dial_timer);
	isdn_slot_all_eaz(idev->isdn_slot);
	printk(KERN_INFO "%s: connection failed\n", idev->name);
	isdn_net_unbind_channel(idev);
}

static void
out_dconn(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;
	isdn_ctrl cmd;

	fsm_change_state(&idev->fi, ST_OUT_WAIT_BCONN);
	isdn_slot_command(idev->isdn_slot, ISDN_CMD_ACCEPTB, &cmd);
}

static void
in_dconn(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;
	isdn_ctrl cmd;

	fsm_change_state(&idev->fi, ST_IN_WAIT_BCONN);
	isdn_slot_command(idev->isdn_slot, ISDN_CMD_ACCEPTB, &cmd);
}

static void
bconn(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;
	isdn_net_local *mlp = idev->mlp;

	fsm_change_state(&idev->fi, ST_ACTIVE);

	if (mlp->onhtime) {
		idev->huptimer = 0;
		idev->dial_event = EV_TIMER_HUP;
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
}

static void
bhup(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;
	isdn_net_local *mlp = idev->mlp;

	del_timer(&idev->dial_timer);
	if (mlp->ops->disconnected)
		mlp->ops->disconnected(idev);

	printk(KERN_INFO "%s: disconnected\n", idev->name);
	fsm_change_state(fi, ST_WAIT_DHUP);
	isdn_net_rm_from_bundle(idev);
}

static void
dhup(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;

	printk(KERN_INFO "%s: Chargesum is %d\n", idev->name, idev->charge);
	isdn_slot_all_eaz(idev->isdn_slot);
	isdn_net_unbind_channel(idev);
}

/* Check if it's time for idle hang-up */

static void
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
		return;
	}
 mod_timer:
	mod_timer(&idev->dial_timer, idev->dial_timer.expires + HZ);
}

/* Charge-info from TelCo. */

static void
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

/*
 * Handle status-messages from ISDN-interfacecard.
 * This function is called from within the main-status-dispatcher
 * isdn_status_callback, which itself is called from the low-level driver.
 * Return: 1 = event handled, 0 = not handled
 */
int
isdn_net_stat_callback(int idx, isdn_ctrl *c)
{
	isdn_net_dev *idev = isdn_slot_idev(idx);

	if (!idev) {
		HERE;
		return 0;
	}
	switch (c->command) {
	case ISDN_STAT_DCONN:
		return fsm_event(&idev->fi, EV_STAT_DCONN, c);
	case ISDN_STAT_BCONN:
		return fsm_event(&idev->fi, EV_STAT_BCONN, c);
	case ISDN_STAT_BHUP:
		return fsm_event(&idev->fi, EV_STAT_BHUP, c);
	case ISDN_STAT_DHUP:
		return fsm_event(&idev->fi, EV_STAT_DHUP, c);
	case ISDN_STAT_CINF:
		return fsm_event(&idev->fi, EV_STAT_CINF, c);
	case ISDN_STAT_BSENT:
		return fsm_event(&idev->fi, EV_STAT_BSENT, c);
	default:
		printk("unknown stat %d\n", c->command);
		return 0;
	}
}

int
isdn_net_handle_event(isdn_net_dev *idev, int pr, void *arg)
{
	fsm_event(&idev->fi, pr, arg);
}

static void
hang_up(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;

	isdn_net_hangup(idev);
}

static void
got_bsent(struct fsm_inst *fi, int pr, void *arg)
{
	isdn_net_dev *idev = fi->userdata;
	isdn_ctrl *c = arg;
	
	isdn_net_bsent(idev, c);
}

static struct fsm_node isdn_net_fn_tbl[] = {
	{ ST_NULL,           EV_CMD_DIAL,        dialout_first },

	{ ST_OUT_WAIT_DCONN, EV_TIMER_DIAL,      dial_timeout  },
	{ ST_OUT_WAIT_DCONN, EV_STAT_DCONN,      out_dconn     },
	{ ST_OUT_WAIT_DCONN, EV_STAT_DHUP,       connect_fail  },
	{ ST_OUT_WAIT_DCONN, EV_TIMER_CB_OUT,    hang_up       },

	{ ST_OUT_WAIT_BCONN, EV_TIMER_DIAL,      dial_timeout  },
	{ ST_OUT_WAIT_BCONN, EV_STAT_BCONN,      bconn         },
	{ ST_OUT_WAIT_BCONN, EV_STAT_DHUP,       connect_fail  },

	{ ST_IN_WAIT_DCONN,  EV_TIMER_INCOMING,  hang_up       },
	{ ST_IN_WAIT_DCONN,  EV_STAT_DCONN,      in_dconn      },
	{ ST_IN_WAIT_DCONN,  EV_STAT_DHUP,       connect_fail  },

	{ ST_IN_WAIT_BCONN,  EV_TIMER_INCOMING,  hang_up       },
	{ ST_IN_WAIT_BCONN,  EV_STAT_BCONN,      bconn         },
	{ ST_IN_WAIT_BCONN,  EV_STAT_DHUP,       connect_fail  },

	{ ST_ACTIVE,         EV_TIMER_HUP,       check_hup     },
	{ ST_ACTIVE,         EV_STAT_BHUP,       bhup          },
	{ ST_ACTIVE,         EV_STAT_CINF,       got_cinf      },
	{ ST_ACTIVE,         EV_STAT_BSENT,      got_bsent     },

	{ ST_WAIT_DHUP,      EV_STAT_DHUP,       dhup          },

	{ ST_WAIT_BEFORE_CB, EV_TIMER_CB_IN,     dialout_first },

	{ ST_OUT_DIAL_WAIT,  EV_TIMER_DIAL_WAIT, dialout_next  },
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

void
isdn_net_lib_init(void)
{
	fsm_new(&isdn_net_fsm);
}

void
isdn_net_lib_exit(void)
{
	fsm_free(&isdn_net_fsm);
}
