/* Linux ISDN subsystem, functions for synchronous PPP (linklevel).
 *
 * Copyright 1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *           1999-2002  by Kai Germaschewski <kai@germaschewski.name>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <linux/module.h>
#include <linux/isdn.h>
#include <linux/smp_lock.h>
#include <linux/poll.h>
#include <linux/ppp-comp.h>
#include <linux/if_arp.h>

#include "isdn_common.h"
#include "isdn_net_lib.h"
#include "isdn_ppp.h"
#include "isdn_ppp_ccp.h"
#include "isdn_ppp_vj.h"
#include "isdn_ppp_mp.h"

/* ====================================================================== */

#define IPPP_MAX_RQ_LEN 8 /* max #frames queued for ipppd to read */

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

#undef IPPPD_DEBUG

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
	unsigned int minor = iminor(ino) - ISDN_MINOR_PPP;
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

	spin_lock_irqsave(&ipppds_lock, flags);
	list_add(&ipppd->ipppds, &ipppds);
	spin_unlock_irqrestore(&ipppds_lock, flags);
	
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

	spin_lock_irqsave(&ipppds_lock, flags);
	list_del(&ipppd->ipppds);
	spin_unlock_irqrestore(&ipppds_lock, flags);

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
	struct inl_ppp *inl_ppp;
	struct ind_ppp *ind_ppp;
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
	ind_ppp = idev->ind_priv;
	inl_ppp = idev->mlp->inl_priv;
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
		ippp_ccp_send_ccp(inl_ppp->ccp, skb);
		break;
	case PPP_CCPFRAG:
		ippp_ccp_send_ccp(ind_ppp->ccp, skb);
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
	struct ind_ppp *ind_ppp = NULL;
	struct inl_ppp *inl_ppp = NULL;
	unsigned long val;
	int r;
	struct ipppd *is;
	struct isdn_ppp_comp_data data;
	unsigned int cfg;

	is = file->private_data;

	ipppd_debug(is, "cmd %#x", cmd);

	// FIXME that needs locking?
	idev = is->idev;
	if (idev) {
		ind_ppp = idev->ind_priv;
		inl_ppp = idev->mlp->inl_priv;
	}
	switch (cmd) {
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
			ind_ppp->debug = val;
			inl_ppp->debug = val;
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
	case PPPIOCBUNDLE:
		r = get_arg(arg, &val, sizeof(val));
		if (r)
			break;

		r = ippp_mp_bundle(idev, val);
		break;
	case PPPIOCGIFNAME:
		r = set_arg(arg, idev->name, strlen(idev->name)+1);
		break;
	case PPPIOCGMPFLAGS:	/* get configuration flags */
		r = set_arg(arg, &inl_ppp->mp_cfg, sizeof(inl_ppp->mp_cfg));
		break;
	case PPPIOCSMPFLAGS:	/* set configuration flags */
		r = get_arg(arg, &val, sizeof(val));
		if (r)
			break;
		inl_ppp->mp_cfg = val;
		break;
	case PPPIOCGFLAGS:	/* get configuration flags */
		cfg = ind_ppp->pppcfg | ippp_ccp_get_flags(ind_ppp->ccp);
		r = set_arg(arg, &cfg, sizeof(cfg));
		break;
	case PPPIOCSFLAGS:	/* set configuration flags */
		r = get_arg(arg, &val, sizeof(val));
		if (r)
			break;
		if ((val & SC_ENABLE_IP) && !(ind_ppp->pppcfg & SC_ENABLE_IP)) {
			ind_ppp->pppcfg = val;
			/* OK .. we are ready to send buffers */
			isdn_net_online(idev);
			break;
		}
		ind_ppp->pppcfg = val;
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
		r = ippp_ccp_set_mru(ind_ppp->ccp, val);
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
		strlcpy(pci.local_num, mlp->msn, sizeof(pci.local_num));
		i = 0;
		list_for_each_entry(phone, &mlp->phone[1], list) {
			if (i++ == idev->dial) {
				strlcpy(pci.remote_num,phone->num,sizeof(pci.remote_num));
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
static int
isdn_ppp_if_get_unit(char *namebuf);

static void
isdn_ppp_dev_xmit(void *priv, struct sk_buff *skb, u16 proto);

static struct sk_buff *
isdn_ppp_lp_alloc_skb(void *priv, int len, int gfp_mask);

/* New CCP stuff */
static void
isdn_ppp_dev_kick_up(void *priv);

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

void
ippp_push_proto(struct ind_ppp *ind_ppp, struct sk_buff *skb, u16 proto)
{
	if (skb_headroom(skb) < 2) {
		isdn_BUG();
		return;
	}
	if ((ind_ppp->pppcfg & SC_COMP_PROT) && proto <= 0xff)
		put_u8(skb_push(skb, 1), proto);
	else
		put_u16(skb_push(skb, 2), proto);

}

static void
ippp_push_ac(struct ind_ppp *ind_ppp, struct sk_buff *skb)
{
	unsigned char *p;

	if (skb_headroom(skb) < 2) {
		isdn_BUG();
		return;
	}
	if (ind_ppp->pppcfg & SC_COMP_AC)
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
	struct ind_ppp *ind_ppp = idev->ind_priv;
	struct ipppd *is = ind_ppp->ipppd;
	
	if (!is) {
		isdn_BUG();
		return;
	}
	ipppd_debug(is, "");

	if (is->state != IPPPD_ST_ASSIGNED)
		isdn_BUG();

	is->state = IPPPD_ST_OPEN;

	/* is->idev will be invalid shortly */
	ippp_ccp_free(ind_ppp->ccp);

	is->idev = NULL;
	/* lose the reference we took on isdn_ppp_bind */
	ipppd_put(is); 
	ind_ppp->ipppd = NULL;

	kfree(ind_ppp);
	idev->ind_priv = NULL;

	return;
}

/*
 * bind isdn_net_local <=> ippp-device
 */
int
isdn_ppp_bind(isdn_net_dev *idev)
{
	struct ind_ppp *ind_ppp;
	int unit = 0;
	unsigned long flags;
	int retval = 0;
	struct ipppd *ipppd;

	if (idev->ind_priv) {
		isdn_BUG();
		return -EIO;
	}
	ind_ppp = kmalloc(sizeof(struct ind_ppp), GFP_KERNEL);
	if (!ind_ppp)
		return -ENOMEM;

	spin_lock_irqsave(&ipppds_lock, flags);
	if (idev->pppbind < 0) {  /* device not bound to ippp device ? */
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
		printk(KERN_INFO "isdn_ppp_bind: invalid interface name %s.\n", idev->name);
		retval = -ENODEV;
		goto err;
	}
	
	ipppd->unit = unit;
	ipppd->state = IPPPD_ST_ASSIGNED;
	ipppd->idev = idev;
	/* we hold a reference until isdn_ppp_unbind() */
	ipppd_get(ipppd);
	spin_unlock_irqrestore(&ipppds_lock, flags);

	idev->ind_priv = ind_ppp;
	ind_ppp->pppcfg = 0;         /* config flags */
	ind_ppp->ipppd = ipppd;
	ind_ppp->ccp = ippp_ccp_alloc();
	if (!ind_ppp->ccp) {
		retval = -ENOMEM;
		goto out;
	}
	ind_ppp->ccp->proto       = PPP_COMPFRAG;
	ind_ppp->ccp->priv        = idev;
	ind_ppp->ccp->alloc_skb   = isdn_ppp_dev_alloc_skb;
	ind_ppp->ccp->xmit        = isdn_ppp_dev_xmit;
	ind_ppp->ccp->kick_up     = isdn_ppp_dev_kick_up;

	retval = ippp_mp_bind(idev);
	if (retval)
		goto out;
	
	return 0;

 out:
	ipppd->state = IPPPD_ST_OPEN;
	ipppd_put(ipppd);
	ind_ppp->ipppd = NULL;
	kfree(ind_ppp);
	idev->ind_priv = NULL;
	return retval;

 err:
	spin_unlock_irqrestore(&ipppds_lock, flags);
	kfree(ind_ppp);
	return retval;
}

/*
 * kick the ipppd on the device
 * (wakes up daemon after B-channel connect)
 */

static void
isdn_ppp_connected(isdn_net_dev *idev)
{
	struct ind_ppp *ind_ppp = idev->ind_priv;
	struct ipppd *ipppd = ind_ppp->ipppd;

	ipppd_debug(ipppd, "");

	ipppd->state  = IPPPD_ST_CONNECTED;
	ipppd->flags |= IPPPD_FL_WAKEUP;
	wake_up(&ipppd->wq);
}

static void
isdn_ppp_disconnected(isdn_net_dev *idev)
{
	struct ind_ppp *ind_ppp = idev->ind_priv;
	struct ipppd *ipppd = ind_ppp->ipppd;

	ipppd_debug(ipppd, "");

	if (ind_ppp->pppcfg & SC_ENABLE_IP)
		isdn_net_offline(idev);

	if (ipppd->state != IPPPD_ST_CONNECTED)
		isdn_BUG();
	
	ipppd->state  = IPPPD_ST_ASSIGNED;
	ipppd->flags |= IPPPD_FL_HUP;
	wake_up(&ipppd->wq);

	ippp_mp_disconnected(idev);
}

/*
 * init memory, structures etc.
 */

int
isdn_ppp_init(void)
{
	return 0;
}

void
isdn_ppp_cleanup(void)
{
}

/*
 * check for address/control field and skip if allowed
 * retval != 0 -> discard packet silently
 */
static int
isdn_ppp_skip_ac(struct ind_ppp *ind_ppp, struct sk_buff *skb) 
{
	u8 val;

	if (skb->len < 1)
		return -EINVAL;

	get_u8(skb->data, &val);
	if (val != PPP_ALLSTATIONS) {
		/* if AC compression was not negotiated, but no AC present,
		   discard packet */
		if (ind_ppp->pppcfg & SC_REJ_COMP_AC)
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
			return -EINVAL;
		get_u16(skb->data, proto);
		skb_pull(skb, 2);
	}
	return 0;
}

/*
 * handler for incoming packets on a syncPPP interface
 */
static void
isdn_ppp_receive(isdn_net_local *lp, isdn_net_dev *idev, struct sk_buff *skb)
{
	struct ind_ppp *ind_ppp = idev->ind_priv;
	struct ipppd *is = ind_ppp->ipppd;
	u16 proto;

	if (!is) 
		goto err;

	if (is->debug & 0x4) {
		printk(KERN_DEBUG "ippp_receive: is:%p lp:%p unit:%d len:%d\n",
		       is, lp, is->unit, skb->len);
		isdn_ppp_frame_log("receive", skb->data, skb->len, 32,is->unit,-1);
	}

 	if (isdn_ppp_skip_ac(ind_ppp, skb) < 0)
		goto err;

  	if (isdn_ppp_strip_proto(skb, &proto))
		goto err;

	ippp_mp_receive(idev, skb, proto);
	return;

 err:
	lp->stats.rx_dropped++;
	kfree_skb(skb);
}

/*
 * address/control and protocol have been stripped from the skb
 */
void
ippp_receive(isdn_net_dev *idev, struct sk_buff *skb, u16 proto)
{
	isdn_net_local *lp = idev->mlp;
	struct inl_ppp *inl_ppp = lp->inl_priv;
	struct ind_ppp *ind_ppp = idev->ind_priv;
 	struct ipppd *is = ind_ppp->ipppd;

	if (is->debug & 0x10) {
		printk(KERN_DEBUG "push, skb %d %04x\n", (int) skb->len, proto);
		isdn_ppp_frame_log("rpush", skb->data, skb->len, 256, is->unit, -1);
	}
	/* all packets need to be passed through the compressor */
	skb = ippp_ccp_decompress(inl_ppp->ccp, skb, &proto);
	if (!skb) /* decompression error */
		goto error;

	switch (proto) {
		case PPP_IPX:  /* untested */
			if (is->debug & 0x20)
				printk(KERN_DEBUG "isdn_ppp: IPX\n");
			isdn_netif_rx(idev, skb, htons(ETH_P_IPX));
			break;
		case PPP_IP:
			if (is->debug & 0x20)
				printk(KERN_DEBUG "isdn_ppp: IP\n");
			isdn_netif_rx(idev, skb, htons(ETH_P_IP));
			break;
		case PPP_COMP:
		case PPP_COMPFRAG:
			printk(KERN_INFO "isdn_ppp: unexpected compressed frame dropped\n");
			goto drop;
		case PPP_VJC_UNCOMP:
		case PPP_VJC_COMP:
			ippp_vj_decompress(idev, skb, proto);
			break;
		case PPP_CCPFRAG:
			ippp_ccp_receive_ccp(ind_ppp->ccp, skb);
			goto ccp;
		case PPP_CCP:
			ippp_ccp_receive_ccp(inl_ppp->ccp, skb);
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
	return;

 drop:
	lp->stats.rx_dropped++;
 free:
	kfree_skb(skb);
	return;

 error:
	lp->stats.rx_dropped++;
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
	struct inl_ppp *inl_ppp = mlp->inl_priv;
	struct ind_ppp *ind_ppp;
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
			printk(KERN_INFO "isdn_ppp: skipped unsupported protocol: %#x.\n", 
			       skb->protocol);
			goto drop;
	}

	idev = isdn_net_get_xmit_dev(mlp);
	if (!idev) {
		printk(KERN_INFO "%s: IP frame delayed.\n", ndev->name);
		goto stop;
	}
	ind_ppp = idev->ind_priv;
	if (!(ind_ppp->pppcfg & SC_ENABLE_IP)) {	/* PPP connected ? */
		isdn_BUG();
		goto stop;
	}
	ipppd = ind_ppp->ipppd;
	idev->huptimer = 0;

        if (ipppd->debug & 0x40)
                isdn_ppp_frame_log("xmit0", skb->data, skb->len, 256, ipppd->unit, -1);

	/* after this line,requeueing is no longer allowed! */
	skb = ippp_vj_compress(idev, skb, &proto);

	/* normal (single link) or bundle compression */
	skb = ippp_ccp_compress(inl_ppp->ccp, skb, &proto);

	if (ipppd->debug & 0x40)
                isdn_ppp_frame_log("xmit1", skb->data, skb->len, 32, ipppd->unit, -1);

	ippp_push_proto(ind_ppp, skb, proto);
	ippp_mp_xmit(idev, skb);
	return 0;

 drop:
	kfree_skb(skb);
	mlp->stats.tx_dropped++;
	return 0;

 stop:
	netif_stop_queue(ndev);
	return 1;
}

void
ippp_xmit(isdn_net_dev *idev, struct sk_buff *skb)
{
	struct ind_ppp *ind_ppp = idev->ind_priv;
	struct ipppd *ipppd = ind_ppp->ipppd;

	ippp_push_ac(ind_ppp, skb);

	if (ipppd->debug & 0x40) {
		isdn_ppp_frame_log("xmit3", skb->data, skb->len, 32, ipppd->unit, -1);
	}
	
	isdn_net_writebuf_skb(idev, skb);
}

/*
 * network device ioctl handlers
 */

static int
isdn_ppp_dev_ioctl_stats(struct ifreq *ifr, struct net_device *dev)
{
	struct ppp_stats *res, t;
	isdn_net_local *lp = (isdn_net_local *) dev->priv;
	struct inl_ppp *inl_ppp = lp->inl_priv;
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
		slcomp = inl_ppp->slcomp;
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

/*
 * PPP compression stuff
 */


/* Push an empty CCP Data Frame up to the daemon to wake it up and let it
   generate a CCP Reset-Request or tear down CCP altogether */

static void isdn_ppp_dev_kick_up(void *priv)
{
	isdn_net_dev *idev = priv;
	struct ind_ppp *ind_ppp = idev->ind_priv;

	ipppd_queue_read(ind_ppp->ipppd, PPP_COMPFRAG, NULL, 0);
}

static void isdn_ppp_lp_kick_up(void *priv)
{
	isdn_net_local *lp = priv;
	isdn_net_dev *idev;
	struct ind_ppp *ind_ppp;

	if (list_empty(&lp->online)) {
		isdn_BUG();
		return;
	}
	idev = list_entry(lp->online.next, isdn_net_dev, online);
	ind_ppp = idev->ind_priv;
	ipppd_queue_read(ind_ppp->ipppd, PPP_COMP, NULL, 0);
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
isdn_ppp_dev_xmit(void *priv, struct sk_buff *skb, u16 proto)
{
	isdn_net_dev *idev = priv;
	struct ind_ppp *ind_ppp = idev->ind_priv;

	ippp_push_proto(ind_ppp, skb, proto);
	ippp_push_ac(ind_ppp, skb);
	isdn_net_write_super(idev, skb);
}

static void
isdn_ppp_lp_xmit(void *priv, struct sk_buff *skb, u16 proto)
{
	isdn_net_local *lp = priv;
	isdn_net_dev *idev;
	struct ind_ppp *ind_ppp;

	if (list_empty(&lp->online)) {
		isdn_BUG();
		return;
	}
	idev = list_entry(lp->online.next, isdn_net_dev, online);
	ind_ppp = idev->ind_priv;
	ippp_push_proto(ind_ppp, skb, proto);
	ippp_push_ac(ind_ppp, skb);
	isdn_net_write_super(idev, skb);
}

static int
isdn_ppp_set_compressor(isdn_net_dev *idev, struct isdn_ppp_comp_data *data)
{
	struct ippp_ccp *ccp;
	struct inl_ppp *inl_ppp = idev->mlp->inl_priv;
	struct ind_ppp *ind_ppp = idev->ind_priv;

	if (data->flags & IPPP_COMP_FLAG_LINK)
		ccp = ind_ppp->ccp;
	else
		ccp = inl_ppp->ccp;

	return ippp_ccp_set_compressor(ccp, ind_ppp->ipppd->unit, data);
}

// ISDN_NET_ENCAP_SYNCPPP
// ======================================================================

static int
isdn_ppp_open(isdn_net_local *lp)
{
	struct inl_ppp *inl_ppp;

	inl_ppp = kmalloc(sizeof(*inl_ppp), GFP_KERNEL);
	if (!inl_ppp)
		return -ENOMEM;

	lp->inl_priv = inl_ppp;

	inl_ppp->slcomp = ippp_vj_alloc();
	if (!inl_ppp->slcomp)
		goto err;

	inl_ppp->ccp = ippp_ccp_alloc();
	if (!inl_ppp->ccp)
		goto err_vj;

	inl_ppp->ccp->proto       = PPP_COMP;
	inl_ppp->ccp->priv        = lp;
	inl_ppp->ccp->alloc_skb   = isdn_ppp_lp_alloc_skb;
	inl_ppp->ccp->xmit        = isdn_ppp_lp_xmit;
	inl_ppp->ccp->kick_up     = isdn_ppp_lp_kick_up;
	
	return 0;

 err_vj:
	ippp_vj_free(inl_ppp->slcomp);
 err:
	kfree(inl_ppp);
	lp->inl_priv = NULL;
	return -ENOMEM;
}

static void
isdn_ppp_close(isdn_net_local *lp)
{
	struct inl_ppp *inl_ppp = lp->inl_priv;
	
	ippp_ccp_free(inl_ppp->ccp);
	ippp_vj_free(inl_ppp->slcomp);

	kfree(inl_ppp);
	lp->inl_priv = NULL;
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

