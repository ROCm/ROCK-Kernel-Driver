/* $Id: isdn_ppp.c,v 1.85.6.9 2001/11/06 20:58:28 kai Exp $
 *
 * Linux ISDN subsystem, functions for synchronous PPP (linklevel).
 *
 * Copyright 1995,96 by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/module.h>
#include <linux/isdn.h>
#include <linux/smp_lock.h>
#include <linux/poll.h>
#include <linux/ppp-comp.h>
#include <linux/if_arp.h>

#include "isdn_common.h"
#include "isdn_ppp.h"
#include "isdn_ppp_ccp.h"
#include "isdn_ppp_vj.h"
#include "isdn_net.h"

static int
isdn_ppp_set_compressor(isdn_net_dev *idev, struct isdn_ppp_comp_data *);

/* ====================================================================== */
/* IPPPD handling                                                         */
/* ====================================================================== */

/* We use reference counting for struct ipppd. It is alloced on
 * open() on /dev/ipppX and saved into file->private, making for one
 * reference. release() will release this reference, after all other
 * references are gone, the destructor frees it.
 *
 * Another reference is taken by isdn_ppp_bind() and freed by
 * isdn_ppp_unbind(). The callbacks from isdn_net_lib.c happen only
 * between isdn_ppp_bind() and isdn_ppp_unbind(), i.e. access to 
 * idev->ipppd is safe without further locking.
 */

#define IPPPD_DEBUG

#ifdef IPPPD_DEBUG
#define ipppd_debug(i, fmt, arg...) \
        printk(KERN_DEBUG "ipppd %p minor %d state %#x %s: " fmt "\n", (i), \
               (i)->minor, (i)->state, __FUNCTION__ , ## arg)
#else
#define ipppd_debug(...) do { } while (0)
#endif

/* ipppd::flags */
enum {
	IPPPD_FL_HUP    = 0x01,
	IPPPD_FL_WAKEUP = 0x02,
};

/* ipppd::state */
enum {
	IPPPD_ST_OPEN,
	IPPPD_ST_ASSIGNED,
	IPPPD_ST_CONNECTED,
};

struct ipppd {
	struct list_head ipppds;
	int state;
	int flags;
	struct sk_buff_head rq;
	wait_queue_head_t wq;
	struct isdn_net_dev_s *idev;
	int unit;
	int minor;
	unsigned long debug;
	atomic_t refcnt;
};

/* ====================================================================== */

static spinlock_t ipppds_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(ipppds);

static void
ipppd_destroy(struct ipppd *ipppd)
{
	HERE;

	skb_queue_purge(&ipppd->rq);
	kfree(ipppd);
}

static inline struct ipppd *
ipppd_get(struct ipppd *ipppd)
{
	atomic_inc(&ipppd->refcnt);
	printk("%s: %d\n", __FUNCTION__, atomic_read(&ipppd->refcnt));
	return ipppd;
}

static inline void 
ipppd_put(struct ipppd *ipppd)
{
	printk("%s: %d\n", __FUNCTION__, atomic_read(&ipppd->refcnt));

	if (atomic_dec_and_test(&ipppd->refcnt))
		ipppd_destroy(ipppd);
}

/* ====================================================================== */
/* char dev ops                                                           */

/* --- open ------------------------------------------------------------- */

static int
ipppd_open(struct inode *ino, struct file *file)
{
	unsigned long flags;
	unsigned int minor = minor(ino->i_rdev) - ISDN_MINOR_PPP;
	struct ipppd *ipppd;

	ipppd = kmalloc(sizeof(*ipppd), GFP_KERNEL);
	if (!ipppd)
		return -ENOMEM;

	memset(ipppd, 0, sizeof(*ipppd));
	atomic_set(&ipppd->refcnt, 0);
	
	/* file->private_data holds a reference */
	file->private_data = ipppd_get(ipppd);

	ipppd->unit = -1;          /* set by isdn_ppp_bind */
	ipppd->minor = minor;
	ipppd->state = IPPPD_ST_OPEN;
	init_waitqueue_head(&ipppd->wq);
	skb_queue_head_init(&ipppd->rq);

	spin_lock_irqsave(&ipppds, flags);
	list_add(&ipppd->ipppds, &ipppds);
	spin_unlock_irqrestore(&ipppds, flags);
	
	ipppd_debug(ipppd, "minor %d", minor);

	return 0;
}

/* --- release  --------------------------------------------------------- */

static int
ipppd_release(struct inode *ino, struct file *file)
{
	unsigned long flags;
	struct ipppd *ipppd = file->private_data;

	ipppd_debug(ipppd, "");

	if (ipppd->state == IPPPD_ST_CONNECTED)
		isdn_net_hangup(ipppd->idev);

	spin_lock_irqsave(&ipppds, flags);
	list_del(&ipppd->ipppds);
	spin_unlock_irqrestore(&ipppds, flags);

	ipppd_put(ipppd);

	return 0;
}

/* --- read ------------------------------------------------------------- */

/* read() is always non blocking */
static ssize_t
ipppd_read(struct file *file, char *buf, size_t count, loff_t *off)
{
	struct ipppd *is;
	struct sk_buff *skb;
	int retval;

	if (off != &file->f_pos)
		return -ESPIPE;
	
	is = file->private_data;

	skb = skb_dequeue(&is->rq);
	if (!skb) {
		retval = -EAGAIN;
		goto out;
	}
	if (skb->len > count) {
		retval = -EMSGSIZE;
		goto out_free;
	}
	if (copy_to_user(buf, skb->data, skb->len)) {
		retval = -EFAULT;
		goto out_free;
	}
	retval = skb->len;

 out_free:
	dev_kfree_skb(skb);
 out:
	return retval;
}

/* --- write ------------------------------------------------------------ */

/* write() is always non blocking */
static ssize_t
ipppd_write(struct file *file, const char *buf, size_t count, loff_t *off)
{
	isdn_net_dev *idev;
	struct ipppd *ipppd;
	struct sk_buff *skb;
	char *p;
	int retval;
	u16 proto;

	if (off != &file->f_pos)
		return -ESPIPE;

	ipppd = file->private_data;
	ipppd_debug(ipppd, "count = %d", count);

	if (ipppd->state != IPPPD_ST_CONNECTED) {
		retval = -ENOTCONN;
		goto out;
	}

	idev = ipppd->idev;
	if (!idev) {
		isdn_BUG();
		retval = -ENODEV;
		goto out;
	}
	/* Daemon needs to send at least full header, AC + proto */
	if (count < 4) {
		retval = -EMSGSIZE;
		goto out;
	}
	skb = isdn_ppp_dev_alloc_skb(idev, count, GFP_KERNEL);
	if (!skb) {
		retval = -ENOMEM;
		goto out;
	}
	p = skb_put(skb, count);
	if (copy_from_user(p, buf, count)) {
		kfree_skb(skb);
		retval = -EFAULT;
		goto out;
	}
	/* Don't reset huptimer for LCP packets. (Echo requests). */
	proto = PPP_PROTOCOL(p);
	if (proto != PPP_LCP)
		idev->huptimer = 0;
	
	/* Keeps CCP/compression states in sync */
	switch (proto) {
	case PPP_CCP:
		ippp_ccp_send_ccp(idev->mlp->ccp, skb);
		break;
	case PPP_CCPFRAG:
		ippp_ccp_send_ccp(idev->ccp, skb);
		break;
	}
	/* FIXME: Somewhere we need protection against the
	 * queue growing too large */
	isdn_net_write_super(idev, skb);

	retval = count;
	
 out:
	return retval;
}

/* --- poll ------------------------------------------------------------- */

static unsigned int
ipppd_poll(struct file *file, poll_table * wait)
{
	unsigned int mask;
	struct ipppd *is;

	is = file->private_data;

	ipppd_debug(is, "");

	/* just registers wait_queue hook. This doesn't really wait. */
	poll_wait(file, &is->wq, wait);

	if (is->flags & IPPPD_FL_HUP) {
		mask = POLLHUP;
		goto out;
	}
	/* we're always ready to send .. */
	mask = POLLOUT | POLLWRNORM;

	/*
	 * if IPPP_FL_WAKEUP is set we return even if we have nothing to read
	 */
	if (!skb_queue_empty(&is->rq) || is->flags & IPPPD_FL_WAKEUP) {
		is->flags &= ~IPPPD_FL_WAKEUP;
		mask |= POLLIN | POLLRDNORM;
		set_current_state(TASK_INTERRUPTIBLE); // FIXME
		schedule_timeout(HZ);
	}

 out:
	return mask;
}

/* --- ioctl ------------------------------------------------------------ */

/* get_arg .. ioctl helper */
static int
get_arg(unsigned long arg, void *val, int len)
{
	if (copy_from_user((void *) val, (void *) arg, len))
		return -EFAULT;
	return 0;
}

/* set arg .. ioctl helper */
static int
set_arg(unsigned long arg, void *val,int len)
{
	if (copy_to_user((void *) arg, (void *) val, len))
		return -EFAULT;
	return 0;
}

static int
ipppd_ioctl(struct inode *ino, struct file *file, unsigned int cmd,
	    unsigned long arg)
{
	isdn_net_dev *idev;
	unsigned long val;
	int r;
	struct ipppd *is;
	struct isdn_ppp_comp_data data;
	unsigned int cfg;

	is = file->private_data;
	idev = is->idev;

	ipppd_debug(is, "cmd %#x", cmd);

	switch (cmd) {
	case PPPIOCBUNDLE:
#ifdef CONFIG_ISDN_MPP
		if (is->state != IPPPD_ST_CONNECTED) {
			r = -EINVAL;
			break;
		}
		r = get_arg(arg, &val, sizeof(val));
		if (r)
			break;

		printk(KERN_DEBUG "iPPP-bundle: minor: %d, slave unit: %d, master unit: %d\n",
		       is->minor, is->unit, val);
		r = isdn_ppp_bundle(is, val);
#else
		r = -EINVAL;
#endif
		break;
	case PPPIOCGUNIT:	/* get ppp/isdn unit number */
		r = set_arg(arg, &is->unit, sizeof(is->unit));
		break;
	case PPPIOCGDEBUG:
		r = set_arg(arg, &is->debug, sizeof(is->debug));
		break;
	case PPPIOCSDEBUG:
		r = get_arg(arg, &val, sizeof(val));
		if (r)
			break;
		is->debug = val;
		if (idev) {
			idev->debug = val;
			idev->mlp->debug = val;
		}
		break;
	case PPPIOCGCOMPRESSORS:
	{
		unsigned long protos[8];
		ippp_ccp_get_compressors(protos);
		r = set_arg(arg, protos, sizeof(protos));
		break;
	}
	default:
		r = -ENOTTY;
		break;
	}

	if (r != -ENOTTY)
		goto out;

	if (!idev) {
		r = -ENODEV;
		goto out;
	}

	switch (cmd) {
	case PPPIOCGIFNAME:
		r = set_arg(arg, idev->name, strlen(idev->name)+1);
		break;
	case PPPIOCGMPFLAGS:	/* get configuration flags */
		r = set_arg(arg, &idev->mlp->mpppcfg, sizeof(idev->mlp->mpppcfg));
		break;
	case PPPIOCSMPFLAGS:	/* set configuration flags */
		r = get_arg(arg, &val, sizeof(val));
		if (r)
			break;
		idev->mlp->mpppcfg = val;
		break;
	case PPPIOCGFLAGS:	/* get configuration flags */
		cfg = idev->pppcfg | ippp_ccp_get_flags(idev->ccp);
		r = set_arg(arg, &cfg, sizeof(cfg));
		break;
	case PPPIOCSFLAGS:	/* set configuration flags */
		r = get_arg(arg, &val, sizeof(val));
		if (r)
			break;
		if ((val & SC_ENABLE_IP) && !(idev->pppcfg & SC_ENABLE_IP)) {
			idev->pppcfg = val;
			/* OK .. we are ready to send buffers */
			isdn_net_online(idev);
			break;
		}
		idev->pppcfg = val;
		break;
	case PPPIOCGIDLE:	/* get idle time information */
	{
		struct ppp_idle pidle;
		pidle.xmit_idle = pidle.recv_idle = idev->huptimer;
		r = set_arg(arg, &pidle,sizeof(pidle));
		break;
	}
	case PPPIOCSMRU:	/* set receive unit size for PPP */
		r = get_arg(arg, &val, sizeof(val));
		if (r)
			break;
		r = ippp_ccp_set_mru(idev->ccp, val);
		break;
	case PPPIOCSMPMRU:
		break;
	case PPPIOCSMPMTU:
		break;
	case PPPIOCSMAXCID:	/* set the maximum compression slot id */
		r = get_arg(arg, &val, sizeof(val));
		if (r)
			break;
		r = ippp_vj_set_maxcid(idev, val);
		break;
	case PPPIOCSCOMPRESSOR:
		r = get_arg(arg, &data, sizeof(data));
		if (r)
			break;
		r = isdn_ppp_set_compressor(idev, &data);
		break;
	case PPPIOCGCALLINFO:
	{
		isdn_net_local *mlp;
		struct isdn_net_phone *phone;
		struct pppcallinfo pci;
		int i;
		memset(&pci, 0, sizeof(pci));

		mlp = idev->mlp;
		strncpy(pci.local_num, mlp->msn, 63);
		i = 0;
		list_for_each_entry(phone, &mlp->phone[1], list) {
			if (i++ == idev->dial) {
				strncpy(pci.remote_num,phone->num,63);
				break;
			}
		}
		pci.charge_units = idev->charge;
		if (idev->outgoing)
			pci.calltype = CALLTYPE_OUTGOING;
		else
			pci.calltype = CALLTYPE_INCOMING;
		if (mlp->flags & ISDN_NET_CALLBACK)
			pci.calltype |= CALLTYPE_CALLBACK;
		r = set_arg(arg, &pci, sizeof(pci));
		break;
	}
	default:
		r = -ENOTTY;
		break;
	}
 out:
	return r;
}

/* --- fops ------------------------------------------------------------- */

struct file_operations isdn_ppp_fops =
{
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= ipppd_read,
	.write		= ipppd_write,
	.poll		= ipppd_poll,
	.ioctl		= ipppd_ioctl,
	.open		= ipppd_open,
	.release	= ipppd_release,
};

/* --- ipppd_queue_read ------------------------------------------------- */

/* Queue packets for ipppd to read(). */

static int
ipppd_queue_read(struct ipppd *is, u16 proto, unsigned char *buf, int len)
{
	struct sk_buff *skb;
	unsigned char *p;
	int retval;

	if (is->state != IPPPD_ST_CONNECTED) {
		printk(KERN_DEBUG "ippp: device not connected.\n");
		retval = -ENOTCONN;
		goto out;
	}
	if (skb_queue_len(&is->rq) > IPPP_MAX_RQ_LEN) {
		printk(KERN_WARNING "ippp: Queue is full\n");
		retval = -EBUSY;
		goto out;
	}
	skb = dev_alloc_skb(len + 4);
	if (!skb) {
		printk(KERN_WARNING "ippp: Can't alloc buf\n");
		retval = -ENOMEM;
		goto out;
	}
	p = skb_put(skb, 4);
	p += put_u8(p, PPP_ALLSTATIONS);
	p += put_u8(p, PPP_UI);
	p += put_u16(p, proto);
	memcpy(skb_put(skb, len), buf, len);

	skb_queue_tail(&is->rq, skb);
	wake_up(&is->wq);

	retval = len;
 out:
	return retval;
}

/* ====================================================================== */
/* interface to isdn_net_lib                                            */
/* ====================================================================== */


/* Prototypes */
static void
isdn_ppp_push_higher(isdn_net_local *lp, isdn_net_dev *idev,
		     struct sk_buff *skb, u16 proto);

static int
isdn_ppp_if_get_unit(char *namebuf);

static void
isdn_ppp_dev_push_header(void *priv, struct sk_buff *skb, u16 proto);

static void
isdn_ppp_dev_xmit(void *priv, struct sk_buff *skb);

static struct sk_buff *
isdn_ppp_lp_alloc_skb(void *priv, int len, int gfp_mask);

static void
isdn_ppp_lp_push_header(void *priv, struct sk_buff *skb, u16 proto);

/* New CCP stuff */
static void
isdn_ppp_dev_kick_up(void *priv);

#ifdef CONFIG_ISDN_MPP
static ippp_bundle * isdn_ppp_bundle_arr = NULL;
 
static int isdn_ppp_mp_bundle_array_init(void);
static int isdn_ppp_mp_init(isdn_net_local *lp, ippp_bundle *add_to);
static void isdn_ppp_mp_receive(isdn_net_local *lp, isdn_net_dev *idev, 
				struct sk_buff *skb);
static void isdn_ppp_mp_cleanup(isdn_net_local *lp );

static int isdn_ppp_bundle(struct ipppd *, int unit);
#endif	/* CONFIG_ISDN_MPP */
  
char *isdn_ppp_revision = "$Revision: 1.85.6.9 $";

/*
 * frame log (debug)
 */
void
isdn_ppp_frame_log(char *info, char *data, int len, int maxlen,int unit,int slot)
{
	int cnt,
	 j,
	 i;
	char buf[80];

	if (len < maxlen)
		maxlen = len;

	for (i = 0, cnt = 0; cnt < maxlen; i++) {
		for (j = 0; j < 16 && cnt < maxlen; j++, cnt++)
			sprintf(buf + j * 3, "%02x ", (unsigned char) data[cnt]);
		printk(KERN_DEBUG "[%d/%d].%s[%d]: %s\n",unit,slot, info, i, buf);
	}
}


static void
isdn_ppp_push_header(isdn_net_dev *idev, struct sk_buff *skb, u16 proto)
{
	unsigned char *p;

	if (skb_headroom(skb) < 4) {
		isdn_BUG();
		return;
	}

	if ((idev->pppcfg & SC_COMP_PROT) && proto <= 0xff)
		put_u8(skb_push(skb, 1), proto);
	else
		put_u16(skb_push(skb, 2), proto);

	if (idev->pppcfg & SC_COMP_AC)
		return;

	p = skb_push(skb, 2);	
	p += put_u8(p, PPP_ALLSTATIONS);
	p += put_u8(p, PPP_UI);
}

/*
 * unbind isdn_net_local <=> ippp-device
 * note: it can happen, that we hangup/free the master before the slaves
 *       in this case we bind another lp to the master device
 */
static void
isdn_ppp_unbind(isdn_net_dev *idev)
{
	struct ipppd *is = idev->ipppd;
	
	if (!is) {
		isdn_BUG();
		return;
	}

	ipppd_debug(is, "");

	if (is->state != IPPPD_ST_ASSIGNED)
		isdn_BUG();

	is->state = IPPPD_ST_OPEN;

	/* is->idev will be invalid shortly */
	ippp_ccp_free(idev->ccp);

	is->idev = NULL;
	/* lose the reference we took on isdn_ppp_bind */
	ipppd_put(is); 
	idev->ipppd = NULL;

	return;
}

/*
 * bind isdn_net_local <=> ippp-device
 */
int
isdn_ppp_bind(isdn_net_dev *idev)
{
	int unit = 0;
	unsigned long flags;
	int retval = 0;
	struct ipppd *ipppd;

	if (idev->ipppd) {
		isdn_BUG();
		return 0;
	}

	spin_lock_irqsave(&ipppds_lock, flags);
	if (idev->pppbind < 0) {  /* device bound to ippp device ? */
		struct list_head *l;
		char exclusive[ISDN_MAX_CHANNELS];	/* exclusive flags */
		memset(exclusive, 0, ISDN_MAX_CHANNELS);
		/* step through net devices to find exclusive minors */
		list_for_each(l, &isdn_net_devs) {
			isdn_net_dev *p = list_entry(l, isdn_net_dev, global_list);
			if (p->pppbind >= 0 && p->pppbind < ISDN_MAX_CHANNELS)
				exclusive[p->pppbind] = 1;
		}
		/*
		 * search a free device / slot
		 */
		list_for_each_entry(ipppd, &ipppds, ipppds) {
			if (!ipppd)
				continue;
			if (ipppd->state != IPPPD_ST_OPEN)
				continue;
			if (!exclusive[ipppd->minor])
				break;
			goto found;
		}
	} else {
		list_for_each_entry(ipppd, &ipppds, ipppds) {
			if (!ipppd)
				continue;
			if (ipppd->state != IPPPD_ST_OPEN)
				continue;
			if (ipppd->minor == idev->pppbind)
				goto found;
		}
	}

	printk(KERN_INFO "isdn_ppp_bind: no ipppd\n");
	retval = -ESRCH;
	goto err;

 found:
	unit = isdn_ppp_if_get_unit(idev->name);	/* get unit number from interface name .. ugly! */
	if (unit < 0) {
		printk(KERN_INFO "isdn_ppp_bind: illegal interface name %s.\n", idev->name);
		retval = -ENODEV;
		goto err;
	}
	
	ipppd->unit = unit;
	ipppd->state = IPPPD_ST_ASSIGNED;
	ipppd->idev = idev;
	/* we hold a reference until isdn_ppp_unbind() */
	idev->ipppd = ipppd_get(ipppd);
	spin_unlock_irqrestore(&ipppds_lock, flags);

	idev->pppcfg = 0;         /* config flags */
	/* seq no last seen, maybe set to bundle min, when joining? */
	idev->pppseq = -1;

	idev->ccp = ippp_ccp_alloc();
	if (!idev->ccp) {
		retval = -ENOMEM;
		goto out;
	}
	idev->ccp->proto       = PPP_COMPFRAG;
	idev->ccp->priv        = idev;
	idev->ccp->alloc_skb   = isdn_ppp_dev_alloc_skb;
	idev->ccp->push_header = isdn_ppp_dev_push_header;
	idev->ccp->xmit        = isdn_ppp_dev_xmit;
	idev->ccp->kick_up     = isdn_ppp_dev_kick_up;

#ifdef CONFIG_ISDN_MPP
	retval = isdn_ppp_mp_init(lp, NULL);
#endif /* CONFIG_ISDN_MPP */
 out:
	if (retval) {
		idev->ipppd->state = IPPPD_ST_OPEN;
		ipppd_put(idev->ipppd);
		idev->ipppd = NULL;
	}

	return retval;

 err:
	spin_unlock_irqrestore(&ipppds_lock, flags);
	return retval;
}

/*
 * kick the ipppd on the device
 * (wakes up daemon after B-channel connect)
 */

static void
isdn_ppp_connected(isdn_net_dev *idev)
{
	struct ipppd *ipppd = idev->ipppd;

	ipppd_debug(ipppd, "");

	ipppd->state  = IPPPD_ST_CONNECTED;
	ipppd->flags |= IPPPD_FL_WAKEUP;
	wake_up(&ipppd->wq);
}

static void
isdn_ppp_disconnected(isdn_net_dev *idev)
{
	struct ipppd *ipppd = idev->ipppd;

	ipppd_debug(ipppd, "");

	if (idev->pppcfg & SC_ENABLE_IP)
		isdn_net_offline(idev);

	if (ipppd->state != IPPPD_ST_CONNECTED)
		isdn_BUG();
	
	ipppd->state  = IPPPD_ST_ASSIGNED;
	ipppd->flags |= IPPPD_FL_HUP;
	wake_up(&ipppd->wq);

#ifdef CONFIG_ISDN_MPP
	spin_lock(&idev->pb->lock);
	if (lp->netdev->pb->ref_ct == 1)	/* last link in queue? */
		isdn_ppp_mp_cleanup(lp);

	lp->netdev->pb->ref_ct--;
	spin_unlock(&lp->netdev->pb->lock);
#endif /* CONFIG_ISDN_MPP */

}

/*
 * init memory, structures etc.
 */

int
isdn_ppp_init(void)
{
#ifdef CONFIG_ISDN_MPP
	if( isdn_ppp_mp_bundle_array_init() < 0 )
		return -ENOMEM;
#endif /* CONFIG_ISDN_MPP */

	return 0;
}

void
isdn_ppp_cleanup(void)
{
#ifdef CONFIG_ISDN_MPP
	if (isdn_ppp_bundle_arr)
		kfree(isdn_ppp_bundle_arr);
#endif /* CONFIG_ISDN_MPP */

}

/*
 * check for address/control field and skip if allowed
 * retval != 0 -> discard packet silently
 */
static int
isdn_ppp_skip_ac(isdn_net_dev *idev, struct sk_buff *skb) 
{
	u8 val;

	if (skb->len < 1)
		return -EINVAL;

	get_u8(skb->data, &val);
	if (val != PPP_ALLSTATIONS) {
		/* if AC compression was not negotiated, but no AC present,
		   discard packet */
		if (idev->pppcfg & SC_REJ_COMP_AC)
			return -EINVAL;

		return 0;
	}
	if (skb->len < 2)
		return -EINVAL;

	get_u8(skb->data + 1, &val);
	if (val != PPP_UI)
		return -EINVAL;

	/* skip address/control (AC) field */
	skb_pull(skb, 2);
	return 0;
}

/*
 * get the PPP protocol header and pull skb
 * retval < 0 -> discard packet silently
 */
int
isdn_ppp_strip_proto(struct sk_buff *skb, u16 *proto) 
{
	u8 val;

	if (skb->len < 1)
		return -EINVAL;

	get_u8(skb->data, &val);
	if (val & 0x1) {
		/* protocol field is compressed */
		*proto = val;
		skb_pull(skb, 1);
	} else {
		if (skb->len < 2)
			return -1;
		get_u16(skb->data, proto);
		skb_pull(skb, 2);
	}
	return 0;
}

/*
 * handler for incoming packets on a syncPPP interface
 */
static void isdn_ppp_receive(isdn_net_local *lp, isdn_net_dev *idev, 
			     struct sk_buff *skb)
{
	struct ipppd *is;
	u16 proto;

	is = idev->ipppd;
	if (!is) 
		goto err;

	if (is->debug & 0x4) {
		printk(KERN_DEBUG "ippp_receive: is:%p lp:%p unit:%d len:%d\n",
		       is, lp, is->unit, skb->len);
		isdn_ppp_frame_log("receive", skb->data, skb->len, 32,is->unit,-1);
	}

 	if (isdn_ppp_skip_ac(idev, skb) < 0)
		goto err;

  	if (isdn_ppp_strip_proto(skb, &proto))
		goto err;

	/* Don't reset huptimer on LCP packets. */
	if (proto != PPP_LCP)
		idev->huptimer = 0;
  
#ifdef CONFIG_ISDN_MPP
 	if (is->compflags & SC_LINK_DECOMP_ON) {
 		skb = isdn_ppp_decompress(skb, is, NULL, &proto);
 		if (!skb) // decompression error
			goto put;
 	}
	
 	if (!(is->mpppcfg & SC_REJ_MP_PROT)) { // we agreed to receive MPPP
  		if (proto == PPP_MP) {
  			isdn_ppp_mp_receive(lp, idev, skb);
			goto put;
 		}
 	} 
 	isdn_ppp_push_higher(lp, idev, skb, proto);
 put:
#else
 	isdn_ppp_push_higher(lp, idev, skb, proto);
#endif
	return;

 err:
	kfree_skb(skb);
}

/*
 * we receive a reassembled frame, MPPP has been taken care of before.
 * address/control and protocol have been stripped from the skb
 * note: net_dev has to be master net_dev
 */
static void
isdn_ppp_push_higher(isdn_net_local *lp, isdn_net_dev *idev,
		     struct sk_buff *skb, u16 proto)
{
	struct net_device *dev = &lp->dev;
 	struct ipppd *is = idev->ipppd;

	if (is->debug & 0x10) {
		printk(KERN_DEBUG "push, skb %d %04x\n", (int) skb->len, proto);
		isdn_ppp_frame_log("rpush", skb->data, skb->len, 32,is->unit, -1);
	}
	skb = ippp_ccp_decompress(lp->ccp, skb, &proto);
	if (!skb) // decompression error
		goto out;

	switch (proto) {
		case PPP_IPX:  /* untested */
			if (is->debug & 0x20)
				printk(KERN_DEBUG "isdn_ppp: IPX\n");
			skb->protocol = htons(ETH_P_IPX);
			break;
		case PPP_IP:
			if (is->debug & 0x20)
				printk(KERN_DEBUG "isdn_ppp: IP\n");
			skb->protocol = htons(ETH_P_IP);
			break;
		case PPP_COMP:
		case PPP_COMPFRAG:
			printk(KERN_INFO "isdn_ppp: unexpected compressed frame dropped\n");
			goto drop;
		case PPP_VJC_UNCOMP:
		case PPP_VJC_COMP:
			skb = ippp_vj_decompress(lp->slcomp, skb, proto);
			if (!skb) {
				lp->stats.rx_dropped++;
				goto out;
			}
			break;
		case PPP_CCPFRAG:
			ippp_ccp_receive_ccp(idev->ccp, skb);
			goto ccp;
		case PPP_CCP:
			ippp_ccp_receive_ccp(lp->ccp, skb);
	ccp:
			/* Dont pop up ResetReq/Ack stuff to the daemon any
			   longer - the job is done already */
			if(skb->data[0] == CCP_RESETREQ ||
			   skb->data[0] == CCP_RESETACK)
				goto free;
			/* fall through */
		default:
			// FIXME use skb directly
			ipppd_queue_read(is, proto, skb->data, skb->len);
			goto free;
	}

 	/* Reset hangup-timer */
 	idev->huptimer = 0;

	skb->dev = dev;
	netif_rx(skb);
	/* net_dev->local->stats.rx_packets++; done in isdn_net.c */
 out:
	return;

 drop:
	lp->stats.rx_dropped++;
 free:
	kfree_skb(skb);
}

/*
 * send ppp frame .. we expect a PIDCOMPressable proto --
 *  (here: currently always PPP_IP,PPP_VJC_COMP,PPP_VJC_UNCOMP)
 *
 * VJ compression may change skb pointer!!! .. requeue with old
 * skb isn't allowed!!
 */

static int
isdn_ppp_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	isdn_net_local *mlp = ndev->priv;
	isdn_net_dev *idev = list_entry(mlp->online.next, isdn_net_dev, online);
	u16 proto = PPP_IP;     /* 0x21 */
	struct ipppd *ipppd;

	ndev->trans_start = jiffies;

	if (list_empty(&mlp->online))
		return isdn_net_autodial(skb, ndev);

	switch (ntohs(skb->protocol)) {
		case ETH_P_IP:
			proto = PPP_IP;
			break;
		case ETH_P_IPX:
			proto = PPP_IPX;	/* untested */
			break;
		default:
			printk(KERN_ERR "isdn_ppp: skipped unsupported protocol: %#x.\n", 
			       skb->protocol);
			dev_kfree_skb(skb);
			goto out;
	}

	idev = isdn_net_get_xmit_dev(mlp);
	if (!idev) {
		printk(KERN_INFO "%s: IP frame delayed.\n", ndev->name);
		goto stop;
	}
	if (!(idev->pppcfg & SC_ENABLE_IP)) {	/* PPP connected ? */
		isdn_BUG();
		goto stop;
	}
	ipppd = idev->ipppd;
	idev->huptimer = 0;

	if (ipppd->debug & 0x4)
		printk(KERN_DEBUG "xmit skb, len %d\n", (int) skb->len);
        if (ipppd->debug & 0x40)
                isdn_ppp_frame_log("xmit0", skb->data, skb->len, 32, ipppd->unit, -1);

	/*
	 * after this line .. requeueing in the device queue is no longer allowed!!!
	 */

	skb = ippp_vj_compress(idev, skb, &proto);

	/*
	 * normal (single link) or bundle compression
	 */
	skb = ippp_ccp_compress(mlp->ccp, skb, &proto);

	if (ipppd->debug & 0x24)
		printk(KERN_DEBUG "xmit2 skb, len %d, proto %04x\n", (int) skb->len, proto);

#ifdef CONFIG_ISDN_MPP
	if (ipt->mpppcfg & SC_MP_PROT) {
		/* we get mp_seqno from static isdn_net_local */
		long mp_seqno = ipts->mp_seqno;
		ipts->mp_seqno++;
		if (ipt->mpppcfg & SC_OUT_SHORT_SEQ) {
			unsigned char *data = isdn_ppp_skb_push(&skb, 3);
			if(!data)
				goto unlock;
			mp_seqno &= 0xfff;
			data[0] = MP_BEGIN_FRAG | MP_END_FRAG | ((mp_seqno >> 8) & 0xf);	/* (B)egin & (E)ndbit .. */
			data[1] = mp_seqno & 0xff;
			data[2] = proto;	/* PID compression */
		} else {
			unsigned char *data = isdn_ppp_skb_push(&skb, 5);
			if(!data)
				goto unlock;
			data[0] = MP_BEGIN_FRAG | MP_END_FRAG;	/* (B)egin & (E)ndbit .. */
			data[1] = (mp_seqno >> 16) & 0xff;	/* sequence number: 24bit */
			data[2] = (mp_seqno >> 8) & 0xff;
			data[3] = (mp_seqno >> 0) & 0xff;
			data[4] = proto;	/* PID compression */
		}
		proto = PPP_MP; /* MP Protocol, 0x003d */
	}
#endif

#if 0
	/*
	 * 'link in bundle' compression  ...
	 */
	if (ipt->compflags & SC_LINK_COMP_ON)
		skb = isdn_ppp_compress(skb,&proto,ipt,ipts,1);
#endif

	isdn_ppp_push_header(idev, skb, proto);

	if (ipppd->debug & 0x40) {
		printk(KERN_DEBUG "skb xmit: len: %d\n", (int) skb->len);
		isdn_ppp_frame_log("xmit", skb->data, skb->len, 32, ipppd->unit, -1);
	}
	
	isdn_net_writebuf_skb(idev, skb);

 out:
	return 0;

 stop:
	netif_stop_queue(ndev);
	return 1;
}

#ifdef CONFIG_ISDN_MPP

/* this is _not_ rfc1990 header, but something we convert both short and long
 * headers to for convinience's sake:
 * 	byte 0 is flags as in rfc1990
 *	bytes 1...4 is 24-bit seqence number converted to host byte order 
 */
#define MP_HEADER_LEN	5

#define MP_LONGSEQ_MASK		0x00ffffff
#define MP_SHORTSEQ_MASK	0x00000fff
#define MP_LONGSEQ_MAX		MP_LONGSEQ_MASK
#define MP_SHORTSEQ_MAX		MP_SHORTSEQ_MASK
#define MP_LONGSEQ_MAXBIT	((MP_LONGSEQ_MASK+1)>>1)
#define MP_SHORTSEQ_MAXBIT	((MP_SHORTSEQ_MASK+1)>>1)

/* sequence-wrap safe comparisions (for long sequence)*/ 
#define MP_LT(a,b)	((a-b)&MP_LONGSEQ_MAXBIT)
#define MP_LE(a,b) 	!((b-a)&MP_LONGSEQ_MAXBIT)
#define MP_GT(a,b) 	((b-a)&MP_LONGSEQ_MAXBIT)
#define MP_GE(a,b)	!((a-b)&MP_LONGSEQ_MAXBIT)

#define MP_SEQ(f)	((*(u32*)(f->data+1)))
#define MP_FLAGS(f)	(f->data[0])

static int isdn_ppp_mp_bundle_array_init(void)
{
	int i;
	int sz = ISDN_MAX_CHANNELS*sizeof(ippp_bundle);
	if( (isdn_ppp_bundle_arr = (ippp_bundle*)kmalloc(sz, 
							GFP_KERNEL)) == NULL )
		return -ENOMEM;
	memset(isdn_ppp_bundle_arr, 0, sz);
	for( i = 0; i < ISDN_MAX_CHANNELS; i++ )
		spin_lock_init(&isdn_ppp_bundle_arr[i].lock);
	return 0;
}

static ippp_bundle * isdn_ppp_mp_bundle_alloc(void)
{
	int i;
	for( i = 0; i < ISDN_MAX_CHANNELS; i++ )
		if (isdn_ppp_bundle_arr[i].ref_ct <= 0)
			return (isdn_ppp_bundle_arr + i);
	return NULL;
}

static int isdn_ppp_mp_init( isdn_net_local * lp, ippp_bundle * add_to )
{
	isdn_net_dev *idev = lp->netdev;
	struct ipppd * is;

	if (idev->ppp_slot < 0) {
		printk(KERN_ERR "%s: >ppp_slot(%d) out of range\n",
		       __FUNCTION__ , idev->ppp_slot);
		return -EINVAL;
	}

	is = ippp_table[idev->ppp_slot];
	if (add_to) {
		if( lp->netdev->pb )
			lp->netdev->pb->ref_ct--;
		lp->netdev->pb = add_to;
	} else {		/* first link in a bundle */
		is->mp_seqno = 0;
		if ((lp->netdev->pb = isdn_ppp_mp_bundle_alloc()) == NULL)
			return -ENOMEM;

		lp->netdev->pb->frags = NULL;
		lp->netdev->pb->frames = 0;
		lp->netdev->pb->seq = LONG_MAX;
	}
	lp->netdev->pb->ref_ct++;
	
	is->pppseq = 0;
	return 0;
}

static u32 isdn_ppp_mp_get_seq( int short_seq, 
					struct sk_buff * skb, u32 last_seq );
static struct sk_buff * isdn_ppp_mp_discard( ippp_bundle * mp,
			struct sk_buff * from, struct sk_buff * to );
static void isdn_ppp_mp_reassembly( isdn_net_dev * net_dev, isdn_net_local * lp,
				struct sk_buff * from, struct sk_buff * to );
static void isdn_ppp_mp_free_skb( ippp_bundle * mp, struct sk_buff * skb );
static void isdn_ppp_mp_print_recv_pkt( int slot, struct sk_buff * skb );

static void isdn_ppp_mp_receive(isdn_net_local *lp, isdn_net_dev *dev, 
				struct sk_buff *skb)
{
	isdn_net_dev *idev = lp->netdev;
	struct ipppd *is;
	isdn_net_dev *qdev;
	ippp_bundle * mp;
	isdn_mppp_stats * stats;
	struct sk_buff * newfrag, * frag, * start, *nextf;
	u32 newseq, minseq, thisseq;
	unsigned long flags;
	int slot;

	spin_lock_irqsave(&lp->netdev->pb->lock, flags);
    	mp = lp->netdev->pb;
        stats = &mp->stats;
	slot = idev->ppp_slot;
	if (slot < 0 || slot > ISDN_MAX_CHANNELS) {
		printk(KERN_ERR "%s: ppp_slot(%d)\n",
		       __FUNCTION__, slot);
		stats->frame_drops++;
		dev_kfree_skb(skb);
		spin_unlock_irqrestore(&mp->lock, flags);
		return;
	}
	is = ippp_table[slot];
    	if( ++mp->frames > stats->max_queue_len )
		stats->max_queue_len = mp->frames;
	
	if (is->debug & 0x8)
		isdn_ppp_mp_print_recv_pkt(slot, skb);

	newseq = isdn_ppp_mp_get_seq(is->mpppcfg & SC_IN_SHORT_SEQ, 
						skb, is->pppseq);


	/* if this packet seq # is less than last already processed one,
	 * toss it right away, but check for sequence start case first 
	 */
	if( mp->seq > MP_LONGSEQ_MAX && (newseq & MP_LONGSEQ_MAXBIT) ) {
		mp->seq = newseq;	/* the first packet: required for
					 * rfc1990 non-compliant clients --
					 * prevents constant packet toss */
	} else if( MP_LT(newseq, mp->seq) ) {
		stats->frame_drops++;
		isdn_ppp_mp_free_skb(mp, skb);
		spin_unlock_irqrestore(&mp->lock, flags);
		return;
	}
	
	/* find the minimum received sequence number over all links */
	is->pppseq = minseq = newseq;
	list_for_each_entry(qdev, &lp->online, online) {
		slot = qdev->ppp_slot;
		if (slot < 0 || slot > ISDN_MAX_CHANNELS) {
			printk(KERN_ERR "%s: lpq->ppp_slot(%d)\n",
			       __FUNCTION__ ,slot);
		} else {
			u32 lls = ippp_table[slot]->pppseq;
			if (MP_LT(lls, minseq))
				minseq = lls;
		}
	}
	if (MP_LT(minseq, mp->seq))
		minseq = mp->seq;	/* can't go beyond already processed
					 * packets */
	newfrag = skb;

  	/* if this new fragment is before the first one, then enqueue it now. */
  	if ((frag = mp->frags) == NULL || MP_LT(newseq, MP_SEQ(frag))) {
		newfrag->next = frag;
    		mp->frags = frag = newfrag;
    		newfrag = NULL;
  	}

  	start = MP_FLAGS(frag) & MP_BEGIN_FRAG &&
				MP_SEQ(frag) == mp->seq ? frag : NULL;

	/* 
	 * main fragment traversing loop
	 *
	 * try to accomplish several tasks:
	 * - insert new fragment into the proper sequence slot (once that's done
	 *   newfrag will be set to NULL)
	 * - reassemble any complete fragment sequence (non-null 'start'
	 *   indicates there is a continguous sequence present)
	 * - discard any incomplete sequences that are below minseq -- due
	 *   to the fact that sender always increment sequence number, if there
	 *   is an incomplete sequence below minseq, no new fragments would
	 *   come to complete such sequence and it should be discarded
	 *
	 * loop completes when we accomplished the following tasks:
	 * - new fragment is inserted in the proper sequence ('newfrag' is 
	 *   set to NULL)
	 * - we hit a gap in the sequence, so no reassembly/processing is 
	 *   possible ('start' would be set to NULL)
	 *
	 * algorightm for this code is derived from code in the book
	 * 'PPP Design And Debugging' by James Carlson (Addison-Wesley)
	 */
  	while (start != NULL || newfrag != NULL) {

    		thisseq = MP_SEQ(frag);
    		nextf = frag->next;

    		/* drop any duplicate fragments */
    		if (newfrag != NULL && thisseq == newseq) {
      			isdn_ppp_mp_free_skb(mp, newfrag);
      			newfrag = NULL;
    		}

    		/* insert new fragment before next element if possible. */
    		if (newfrag != NULL && (nextf == NULL || 
						MP_LT(newseq, MP_SEQ(nextf)))) {
      			newfrag->next = nextf;
      			frag->next = nextf = newfrag;
      			newfrag = NULL;
    		}

    		if (start != NULL) {
	    		/* check for misplaced start */
      			if (start != frag && (MP_FLAGS(frag) & MP_BEGIN_FRAG)) {
				printk(KERN_WARNING"isdn_mppp(seq %d): new "
				      "BEGIN flag with no prior END", thisseq);
				stats->seqerrs++;
				stats->frame_drops++;
				start = isdn_ppp_mp_discard(mp, start,frag);
				nextf = frag->next;
      			}
    		} else if (MP_LE(thisseq, minseq)) {		
      			if (MP_FLAGS(frag) & MP_BEGIN_FRAG)
				start = frag;
      			else {
				if (MP_FLAGS(frag) & MP_END_FRAG)
	  				stats->frame_drops++;
				if( mp->frags == frag )
					mp->frags = nextf;	
				isdn_ppp_mp_free_skb(mp, frag);
				frag = nextf;
				continue;
      			}
		}
		
		/* if start is non-null and we have end fragment, then
		 * we have full reassembly sequence -- reassemble 
		 * and process packet now
		 */
    		if (start != NULL && (MP_FLAGS(frag) & MP_END_FRAG)) {
      			minseq = mp->seq = (thisseq+1) & MP_LONGSEQ_MASK;
      			/* Reassemble the packet then dispatch it */
			isdn_ppp_mp_reassembly(lp->netdev, lp, start, nextf);
      
      			start = NULL;
      			frag = NULL;

      			mp->frags = nextf;
    		}

		/* check if need to update start pointer: if we just
		 * reassembled the packet and sequence is contiguous
		 * then next fragment should be the start of new reassembly
		 * if sequence is contiguous, but we haven't reassembled yet,
		 * keep going.
		 * if sequence is not contiguous, either clear everyting
		 * below low watermark and set start to the next frag or
		 * clear start ptr.
		 */ 
    		if (nextf != NULL && 
		    ((thisseq+1) & MP_LONGSEQ_MASK) == MP_SEQ(nextf)) {
      			/* if we just reassembled and the next one is here, 
			 * then start another reassembly. */

      			if (frag == NULL) {
				if (MP_FLAGS(nextf) & MP_BEGIN_FRAG)
	  				start = nextf;
				else
				{
	  				printk(KERN_WARNING"isdn_mppp(seq %d):"
						" END flag with no following "
						"BEGIN", thisseq);
					stats->seqerrs++;
				}
			}

    		} else {
			if ( nextf != NULL && frag != NULL &&
						MP_LT(thisseq, minseq)) {
				/* we've got a break in the sequence
				 * and we not at the end yet
				 * and we did not just reassembled
				 *(if we did, there wouldn't be anything before)
				 * and we below the low watermark 
			 	 * discard all the frames below low watermark 
				 * and start over */
				stats->frame_drops++;
				mp->frags = isdn_ppp_mp_discard(mp,start,nextf);
			}
			/* break in the sequence, no reassembly */
      			start = NULL;
    		}
	  			
    		frag = nextf;
  	}	/* while -- main loop */
	
  	if (mp->frags == NULL)
    		mp->frags = frag;
		
	/* rather straighforward way to deal with (not very) possible 
	 * queue overflow */
	if (mp->frames > MP_MAX_QUEUE_LEN) {
		stats->overflows++;
		while (mp->frames > MP_MAX_QUEUE_LEN) {
			frag = mp->frags->next;
			isdn_ppp_mp_free_skb(mp, mp->frags);
			mp->frags = frag;
		}
	}
	spin_unlock_irqrestore(&mp->lock, flags);
}

static void isdn_ppp_mp_cleanup( isdn_net_local * lp )
{
	struct sk_buff * frag = lp->netdev->pb->frags;
	struct sk_buff * nextfrag;
    	while( frag ) {
		nextfrag = frag->next;
		isdn_ppp_mp_free_skb(lp->netdev->pb, frag);
		frag = nextfrag;
	}
	lp->netdev->pb->frags = NULL;
}

static u32 isdn_ppp_mp_get_seq( int short_seq, 
					struct sk_buff * skb, u32 last_seq )
{
	u32 seq;
	int flags = skb->data[0] & (MP_BEGIN_FRAG | MP_END_FRAG);
   
   	if( !short_seq )
	{
		seq = ntohl(*(u32*)skb->data) & MP_LONGSEQ_MASK;
		skb_push(skb,1);
	}
	else
	{
		/* convert 12-bit short seq number to 24-bit long one 
	 	*/
		seq = ntohs(*(u16*)skb->data) & MP_SHORTSEQ_MASK;
	
		/* check for seqence wrap */
		if( !(seq &  MP_SHORTSEQ_MAXBIT) && 
		     (last_seq &  MP_SHORTSEQ_MAXBIT) && 
		     (unsigned long)last_seq <= MP_LONGSEQ_MAX )
			seq |= (last_seq + MP_SHORTSEQ_MAX+1) & 
					(~MP_SHORTSEQ_MASK & MP_LONGSEQ_MASK);
		else
			seq |= last_seq & (~MP_SHORTSEQ_MASK & MP_LONGSEQ_MASK);
		
		skb_push(skb, 3);	/* put converted seqence back in skb */
	}
	*(u32*)(skb->data+1) = seq; 	/* put seqence back in _host_ byte
					 * order */
	skb->data[0] = flags;	        /* restore flags */
	return seq;
}

struct sk_buff * isdn_ppp_mp_discard( ippp_bundle * mp,
			struct sk_buff * from, struct sk_buff * to )
{
	if( from )
		while (from != to) {
	  		struct sk_buff * next = from->next;
			isdn_ppp_mp_free_skb(mp, from);
	  		from = next;
		}
	return from;
}

void isdn_ppp_mp_reassembly( isdn_net_dev * net_dev, isdn_net_local * lp,
				struct sk_buff * from, struct sk_buff * to )
{
	isdn_net_dev *idev = lp->netdev;
	ippp_bundle * mp = net_dev->pb;
	int proto;
	struct sk_buff * skb;
	unsigned int tot_len;

	if (idev->ppp_slot < 0 || idev->ppp_slot > ISDN_MAX_CHANNELS) {
		printk(KERN_ERR "%s: ppp_slot(%d) out of range\n",
		       __FUNCTION__ , idev->ppp_slot);
		return;
	}
	if( MP_FLAGS(from) == (MP_BEGIN_FRAG | MP_END_FRAG) ) {
		if( ippp_table[idev->ppp_slot]->debug & 0x40 )
			printk(KERN_DEBUG "isdn_mppp: reassembly: frame %d, "
					"len %d\n", MP_SEQ(from), from->len );
		skb = from;
		skb_pull(skb, MP_HEADER_LEN);
		mp->frames--;	
	} else {
		struct sk_buff * frag;
		int n;

		for(tot_len=n=0, frag=from; frag != to; frag=frag->next, n++)
			tot_len += frag->len - MP_HEADER_LEN;

		if( ippp_table[idev->ppp_slot]->debug & 0x40 )
			printk(KERN_DEBUG"isdn_mppp: reassembling frames %d "
				"to %d, len %d\n", MP_SEQ(from), 
				(MP_SEQ(from)+n-1) & MP_LONGSEQ_MASK, tot_len );
		if( (skb = dev_alloc_skb(tot_len)) == NULL ) {
			printk(KERN_ERR "isdn_mppp: cannot allocate sk buff "
					"of size %d\n", tot_len);
			isdn_ppp_mp_discard(mp, from, to);
			return;
		}

		while( from != to ) {
			unsigned int len = from->len - MP_HEADER_LEN;

			memcpy(skb_put(skb,len), from->data+MP_HEADER_LEN, len);
			frag = from->next;
			isdn_ppp_mp_free_skb(mp, from);
			from = frag; 
		}
	}
   	proto = isdn_ppp_strip_proto(skb);
	isdn_ppp_push_higher(lp, idev, skb, proto);
}

static void isdn_ppp_mp_free_skb(ippp_bundle * mp, struct sk_buff * skb)
{
	dev_kfree_skb(skb);
	mp->frames--;
}

static void isdn_ppp_mp_print_recv_pkt( int slot, struct sk_buff * skb )
{
	printk(KERN_DEBUG "mp_recv: %d/%d -> %02x %02x %02x %02x %02x %02x\n", 
		slot, (int) skb->len, 
		(int) skb->data[0], (int) skb->data[1], (int) skb->data[2],
		(int) skb->data[3], (int) skb->data[4], (int) skb->data[5]);
}

static int
isdn_ppp_bundle(struct ipppd *is, int unit)
{
	char ifn[IFNAMSIZ + 1];
	isdn_net_dev *p;
	isdn_net_dev *idev, *nidev;
	int rc;
	unsigned long flags;

	sprintf(ifn, "ippp%d", unit);
	p = isdn_net_findif(ifn);
	if (!p) {
		printk(KERN_ERR "ippp_bundle: cannot find %s\n", ifn);
		return -EINVAL;
	}

    	spin_lock_irqsave(&p->pb->lock, flags);

	nidev = is->idev;
	idev = list_entry(p->mlp->online.next, isdn_net_dev, online);
	if( nidev->ppp_slot < 0 || nidev->ppp_slot >= ISDN_MAX_CHANNELS ||
	    idev ->ppp_slot < 0 || idev ->ppp_slot >= ISDN_MAX_CHANNELS ) {
		printk(KERN_ERR "ippp_bundle: binding to invalid slot %d\n",
			nidev->ppp_slot < 0 || nidev->ppp_slot >= ISDN_MAX_CHANNELS ? 
			nidev->ppp_slot : idev->ppp_slot );
		rc = -EINVAL;
		goto out;
 	}

	isdn_net_add_to_bundle(p->mlp, nidev);

	ippp_table[nidev->ppp_slot]->unit = ippp_table[idev->ppp_slot]->unit;

	/* maybe also SC_CCP stuff */
	ippp_table[nidev->ppp_slot]->pppcfg |= ippp_table[idev->ppp_slot]->pppcfg &
		(SC_ENABLE_IP | SC_NO_TCP_CCID | SC_REJ_COMP_TCP);
	ippp_table[nidev->ppp_slot]->mpppcfg |= ippp_table[idev->ppp_slot]->mpppcfg &
		(SC_MP_PROT | SC_REJ_MP_PROT | SC_OUT_SHORT_SEQ | SC_IN_SHORT_SEQ);
	rc = isdn_ppp_mp_init(nidev->mlp, p->pb);
out:
	spin_unlock_irqrestore(&p->pb->lock, flags);
	return rc;
}
  
#endif /* CONFIG_ISDN_MPP */
  
/*
 * network device ioctl handlers
 */

static int
isdn_ppp_dev_ioctl_stats(struct ifreq *ifr, struct net_device *dev)
{
	struct ppp_stats *res, t;
	isdn_net_local *lp = (isdn_net_local *) dev->priv;
	struct slcompress *slcomp;
	int err;

	res = (struct ppp_stats *) ifr->ifr_ifru.ifru_data;
	err = verify_area(VERIFY_WRITE, res, sizeof(struct ppp_stats));

	if (err)
		return err;

	/* build a temporary stat struct and copy it to user space */

	memset(&t, 0, sizeof(struct ppp_stats));
	if (dev->flags & IFF_UP) {
		t.p.ppp_ipackets = lp->stats.rx_packets;
		t.p.ppp_ibytes = lp->stats.rx_bytes;
		t.p.ppp_ierrors = lp->stats.rx_errors;
		t.p.ppp_opackets = lp->stats.tx_packets;
		t.p.ppp_obytes = lp->stats.tx_bytes;
		t.p.ppp_oerrors = lp->stats.tx_errors;
#ifdef CONFIG_ISDN_PPP_VJ
		slcomp = lp->slcomp;
		if (slcomp) {
			t.vj.vjs_packets = slcomp->sls_o_compressed + slcomp->sls_o_uncompressed;
			t.vj.vjs_compressed = slcomp->sls_o_compressed;
			t.vj.vjs_searches = slcomp->sls_o_searches;
			t.vj.vjs_misses = slcomp->sls_o_misses;
			t.vj.vjs_errorin = slcomp->sls_i_error;
			t.vj.vjs_tossed = slcomp->sls_i_tossed;
			t.vj.vjs_uncompressedin = slcomp->sls_i_uncompressed;
			t.vj.vjs_compressedin = slcomp->sls_i_compressed;
		}
#endif
	}
	if( copy_to_user(res, &t, sizeof(struct ppp_stats))) return -EFAULT;
	return 0;
}

int
isdn_ppp_dev_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	int error=0;
	char *r;
	int len;
	isdn_net_local *lp = (isdn_net_local *) dev->priv;


	if (lp->p_encap != ISDN_NET_ENCAP_SYNCPPP)
		return -EINVAL;

	switch (cmd) {
	case SIOCGPPPVER:
		r = (char *) ifr->ifr_ifru.ifru_data;
		len = strlen(PPP_VERSION) + 1;
		if (copy_to_user(r, PPP_VERSION, len))
			error = -EFAULT;
		break;
	case SIOCGPPPSTATS:
		error = isdn_ppp_dev_ioctl_stats(ifr, dev);
		break;
	default:
		error = -EINVAL;
		break;
	}
	return error;
}

static int
isdn_ppp_if_get_unit(char *name)
{
	int len,
	 i,
	 unit = 0,
	 deci;

	len = strlen(name);

	if (strncmp("ippp", name, 4) || len > 8)
		return -1;

	for (i = 0, deci = 1; i < len; i++, deci *= 10) {
		char a = name[len - i - 1];
		if (a >= '0' && a <= '9')
			unit += (a - '0') * deci;
		else
			break;
	}
	if (!i || len - i != 4)
		unit = -1;

	return unit;
}


int
isdn_ppp_dial_slave(char *name)
{
#ifdef CONFIG_ISDN_MPP
	isdn_net_dev *idev;
	isdn_net_dev *sdev;

	idev = isdn_net_findif(name);
	if (!idev)
		return 1;

	if (!isdn_net_bound(idev))
		return 5;

	sdev = idev->slave;
	while (sdev) {
		if (!isdn_net_bound(sdev))
			break;
		sdev = sdev->slave;
	}
	if (!sdev)
		return 2;

	isdn_net_dial_req(sdev);
	return 0;
#else
	return -1;
#endif
}

int
isdn_ppp_hangup_slave(char *name)
{
#ifdef CONFIG_ISDN_MPP
	isdn_net_dev *idev, *sdev;

	idev = isdn_net_findif(name);
	if (!idev)
		return 1;

	if (!isdn_net_bound(idev))
		return 5;

	sdev = idev->slave;
	if (!sdev || !isdn_net_bound(sdev))
		return 2;

	isdn_net_hangup(sdev);
	return 0;
#else
	return -1;
#endif
}

/*
 * PPP compression stuff
 */


/* Push an empty CCP Data Frame up to the daemon to wake it up and let it
   generate a CCP Reset-Request or tear down CCP altogether */

static void isdn_ppp_dev_kick_up(void *priv)
{
	isdn_net_dev *idev = priv;

	ipppd_queue_read(idev->ipppd, PPP_COMPFRAG, NULL, 0);
}

static void isdn_ppp_lp_kick_up(void *priv)
{
	isdn_net_local *lp = priv;
	isdn_net_dev *idev;

	if (list_empty(&lp->online)) {
		isdn_BUG();
		return;
	}
	idev = list_entry(lp->online.next, isdn_net_dev, online);
	ipppd_queue_read(idev->ipppd, PPP_COMP, NULL, 0);
}

/* Send a CCP Reset-Request or Reset-Ack directly from the kernel. */

static struct sk_buff *
__isdn_ppp_alloc_skb(isdn_net_dev *idev, int len, unsigned int gfp_mask)
{
	int hl = IPPP_MAX_HEADER + isdn_slot_hdrlen(idev->isdn_slot); 
	struct sk_buff *skb;

	skb = alloc_skb(hl + len, gfp_mask);
	if (!skb)
		return NULL;

	skb_reserve(skb, hl);
	return skb;
}

struct sk_buff *
isdn_ppp_dev_alloc_skb(void *priv, int len, int gfp_mask)
{
	isdn_net_dev *idev = priv;

	return __isdn_ppp_alloc_skb(idev, len, gfp_mask);
}

static struct sk_buff *
isdn_ppp_lp_alloc_skb(void *priv, int len, int gfp_mask)
{
	isdn_net_local *lp = priv;
	isdn_net_dev *idev;

	if (list_empty(&lp->online)) {
		isdn_BUG();
		return NULL;
	}
	idev = list_entry(lp->online.next, isdn_net_dev, online);
	return __isdn_ppp_alloc_skb(idev, len, gfp_mask);
}

static void
isdn_ppp_dev_push_header(void *priv, struct sk_buff *skb, u16 proto)
{
	isdn_net_dev *idev = priv;

	isdn_ppp_push_header(idev, skb, proto);
}

static void
isdn_ppp_lp_push_header(void *priv, struct sk_buff *skb, u16 proto)
{
	isdn_net_local *lp = priv;
	isdn_net_dev *idev;

	if (list_empty(&lp->online)) {
		isdn_BUG();
		return;
	}
	idev = list_entry(lp->online.next, isdn_net_dev, online);
	isdn_ppp_push_header(idev, skb, proto);
}

static void
isdn_ppp_dev_xmit(void *priv, struct sk_buff *skb)
{
	isdn_net_dev *idev = priv;

	isdn_net_write_super(idev, skb);
}

static void
isdn_ppp_lp_xmit(void *priv, struct sk_buff *skb)
{
	isdn_net_local *lp = priv;
	isdn_net_dev *idev;

	if (list_empty(&lp->online)) {
		isdn_BUG();
		return;
	}
	idev = list_entry(lp->online.next, isdn_net_dev, online);
	isdn_net_write_super(idev, skb);
}

static int
isdn_ppp_set_compressor(isdn_net_dev *idev, struct isdn_ppp_comp_data *data)
{
	struct ippp_ccp *ccp;

	if (data->flags & IPPP_COMP_FLAG_LINK)
		ccp = idev->ccp;
	else
		ccp = idev->mlp->ccp;

	return ippp_ccp_set_compressor(ccp, idev->ipppd->unit, data);
}

// ISDN_NET_ENCAP_SYNCPPP
// ======================================================================

static int
isdn_ppp_open(isdn_net_local *lp)
{
	lp->mpppcfg = 0;        /* mppp configuration */
	lp->mp_seqno = 0;       /* MP sequence number */

	lp->slcomp = ippp_vj_alloc();
	if (!lp->slcomp)
		goto err;

	lp->ccp = ippp_ccp_alloc();
	if (!lp->ccp)
		goto err_vj;

	lp->ccp->proto       = PPP_COMP;
	lp->ccp->priv        = lp;
	lp->ccp->alloc_skb   = isdn_ppp_lp_alloc_skb;
	lp->ccp->push_header = isdn_ppp_lp_push_header;
	lp->ccp->xmit        = isdn_ppp_lp_xmit;
	lp->ccp->kick_up     = isdn_ppp_lp_kick_up;
	
	return 0;

 err_vj:
	ippp_vj_free(lp->slcomp);
	lp->slcomp = NULL;
 err:
	return -ENOMEM;
}

static void
isdn_ppp_close(isdn_net_local *lp)
{
	
	ippp_ccp_free(lp->ccp);
	lp->ccp = NULL;
	ippp_vj_free(lp->slcomp);
	lp->slcomp = NULL;
}

struct isdn_netif_ops isdn_ppp_ops = {
	.hard_start_xmit     = isdn_ppp_start_xmit,
	.do_ioctl            = isdn_ppp_dev_ioctl,
	.flags               = IFF_NOARP | IFF_POINTOPOINT,
	.type                = ARPHRD_PPP,
	.receive             = isdn_ppp_receive,
	.connected           = isdn_ppp_connected,
	.disconnected        = isdn_ppp_disconnected,
	.bind                = isdn_ppp_bind,
	.unbind              = isdn_ppp_unbind,
	.open                = isdn_ppp_open,
	.close               = isdn_ppp_close,
};

