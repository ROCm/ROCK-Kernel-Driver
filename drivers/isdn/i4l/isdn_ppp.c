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
#include "isdn_net.h"

struct ipppd {
	int state;
	struct sk_buff_head rq;
	wait_queue_head_t wq;
	struct isdn_net_dev_s *idev;
	int unit;
	int minor;
	unsigned long debug;
};

/* Prototypes */
static int isdn_ppp_fill_rq(unsigned char *buf, int len, int proto, int slot);
static void isdn_ppp_closewait(isdn_net_dev *idev);
static void isdn_ppp_push_higher(isdn_net_local *lp, isdn_net_dev *idev,
				 struct sk_buff *skb, int proto);
static int isdn_ppp_if_get_unit(char *namebuf);
static int isdn_ppp_set_compressor(struct ipppd *is,struct isdn_ppp_comp_data *);

static void
isdn_ppp_receive_ccp(isdn_net_dev * net_dev, isdn_net_local * lp,
		     struct sk_buff *skb,int proto);

static void
isdn_ppp_send_ccp(isdn_net_dev *net_dev, isdn_net_local *lp,
		  struct sk_buff *skb);

/* New CCP stuff */
static void
isdn_ppp_ccp_kick_up(void *priv, unsigned int flags);

static void
isdn_ppp_ccp_xmit_reset(void *priv, int proto, unsigned char code,
			unsigned char id, unsigned char *data, int len);

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

#define NR_IPPPDS 64

static spinlock_t ipppds_lock = SPIN_LOCK_UNLOCKED;
static struct ipppd *ipppds[NR_IPPPDS];

static inline struct ipppd *
ipppd_get(int slot)
{
	return ipppds[slot];
}

static inline void 
ipppd_put(struct ipppd *ipppd)
{
}

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

/*
 * unbind isdn_net_local <=> ippp-device
 * note: it can happen, that we hangup/free the master before the slaves
 *       in this case we bind another lp to the master device
 */
static void
isdn_ppp_free(isdn_net_dev *idev)
{
	unsigned long flags;
	struct ipppd *is;
	
	// FIXME much of this wants to rather happen when disconnected()

	if (idev->ppp_slot < 0 || idev->ppp_slot > ISDN_MAX_CHANNELS) {
		printk(KERN_ERR "%s: ppp_slot(%d) out of range\n",
		       __FUNCTION__ , idev->ppp_slot);
		return;
	}

	save_flags(flags);
	cli();

#ifdef CONFIG_ISDN_MPP
	spin_lock(&idev->pb->lock);
#endif
#ifdef CONFIG_ISDN_MPP
	if (lp->netdev->pb->ref_ct == 1)	/* last link in queue? */
		isdn_ppp_mp_cleanup(lp);

	lp->netdev->pb->ref_ct--;
	spin_unlock(&lp->netdev->pb->lock);
#endif /* CONFIG_ISDN_MPP */
	if (idev->ppp_slot < 0 || idev->ppp_slot > ISDN_MAX_CHANNELS) {
		printk(KERN_ERR "%s: ppp_slot(%d) now invalid\n",
		       __FUNCTION__ , idev->ppp_slot);
		restore_flags(flags);
		return;
	}
	is = ipppd_get(idev->ppp_slot);
	if (!is)
		return;

	if (is->state & IPPP_CONNECT)
		isdn_ppp_closewait(idev);	/* force wakeup on ippp device */
	else if (is->state & IPPP_ASSIGNED)
		is->state = IPPP_OPEN;	/* fallback to 'OPEN but not ASSIGNED' state */

	if (is->debug & 0x1)
		printk(KERN_DEBUG "isdn_ppp_free %d %p\n", idev->ppp_slot, is->idev);

	is->idev = NULL;          /* link is down .. set lp to NULL */
	idev->ppp_slot = -1;      /* is this OK ?? */
	ipppd_put(is);

	ippp_ccp_free(idev->ccp);

	restore_flags(flags);
	return;
}

/*
 * bind isdn_net_local <=> ippp-device
 */
int
isdn_ppp_bind(isdn_net_dev *idev)
{
	int i;
	int unit = 0;
	unsigned long flags;
	int retval = 0;

	spin_lock_irqsave(&ipppds_lock, flags);
	if (idev->pppbind < 0) {  /* device bound to ippp device ? */
		struct list_head *l;
		char exclusive[ISDN_MAX_CHANNELS];	/* exclusive flags */
		memset(exclusive, 0, ISDN_MAX_CHANNELS);
		/* step through net devices to find exclusive minors */
		list_for_each(l, &isdn_net_devs) {
			isdn_net_dev *p = list_entry(l, isdn_net_dev, global_list);
			if (p->pppbind >= 0)
				exclusive[p->pppbind] = 1;
		}
		/*
		 * search a free device / slot
		 */
		for (i = 0; i < NR_IPPPDS; i++) {
			if (!ipppds[i])
				continue;
			if (ipppds[i]->state != IPPP_OPEN)
				continue;
			if (!exclusive[ipppds[i]->minor])
				break;
			break;
		}
	} else {
		for (i = 0; i < NR_IPPPDS; i++) {
			if (!ipppds[i])
				continue;
			if (ipppds[i]->state != IPPP_OPEN)
				continue;
			if (ipppds[i]->minor == idev->pppbind)
				break;
		}
	}

	if (i >= NR_IPPPDS) {
		printk(KERN_INFO "isdn_ppp_bind: no ipppd\n");
		retval = -ESRCH;
		goto err;
	}
	unit = isdn_ppp_if_get_unit(idev->name);	/* get unit number from interface name .. ugly! */
	if (unit < 0) {
		printk(KERN_INFO "isdn_ppp_bind: illegal interface name %s.\n", idev->name);
		retval = -ENODEV;
		goto err;
	}
	
	ipppds[i]->state = IPPP_OPEN | IPPP_ASSIGNED;	/* assigned to a netdevice but not connected */

	spin_unlock_irqrestore(&ipppds_lock, flags);

	ipppds[i]->idev = idev;
	ipppds[i]->unit = unit;

	idev->ppp_slot = i;
	idev->pppcfg = 0;         /* config flags */
	/* seq no last seen, maybe set to bundle min, when joining? */
	idev->pppseq = -1;

	idev->ccp = ippp_ccp_alloc(PPP_COMPFRAG, idev, isdn_ppp_ccp_xmit_reset,
				   isdn_ppp_ccp_kick_up);
	if (!idev->ccp) {
		retval = -ENOMEM;
		goto out;
	}

#ifdef CONFIG_ISDN_MPP
	retval = isdn_ppp_mp_init(lp, NULL);
#endif /* CONFIG_ISDN_MPP */
 out:
	if (retval)
		ipppds[i]->state = IPPP_OPEN;

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
isdn_ppp_wakeup_daemon(isdn_net_dev *idev)
{
	struct ipppd *ipppd = ipppd_get(idev->ppp_slot);

	if (!ipppd)
		return;
	
	ipppd->state = IPPP_OPEN | IPPP_CONNECT | IPPP_NOBLOCK;
	wake_up(&ipppd->wq);
	ipppd_put(ipppd);
}

/*
 * there was a hangup on the netdevice
 * force wakeup of the ippp device
 * go into 'device waits for release' state
 */
static void
isdn_ppp_closewait(isdn_net_dev *idev)
{
	struct ipppd *ipppd = ipppd_get(idev->ppp_slot);

	if (!ipppd)
		return;
	
	wake_up(&ipppd->wq);
	ipppd->state = IPPP_CLOSEWAIT;
	ipppd_put(ipppd);
}

/*
 * isdn_ppp_get_slot
 */

static int
isdn_ppp_get_slot(void)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&ipppds_lock, flags);
	for (i = 0; i < NR_IPPPDS; i++) {
		if (ipppds[i]->state == 0) {
			ipppds[i]->state = IPPP_OPEN;
			break;
		}
	}
	spin_unlock_irqrestore(&ipppds_lock, flags);
	return (i < NR_IPPPDS) ? i : -1;
}

/*
 * ipppd_open
 */

static int
ipppd_open(struct inode *ino, struct file *file)
{
	uint minor = minor(ino->i_rdev) - ISDN_MINOR_PPP;
	int slot;
	struct ipppd *is;

	slot = isdn_ppp_get_slot();
	if (slot < 0)
		return -EBUSY;

	is = ipppds[slot];
	file->private_data = is;

	printk(KERN_DEBUG "ippp, open, slot: %d, minor: %d, state: %04x\n", slot, minor, is->state);

	is->idev = NULL;
	is->unit = -1;          /* set, when we have our interface */
	init_waitqueue_head(&is->wq);
	is->minor = minor;

	isdn_lock_drivers();

	return 0;
}

/*
 * release ippp device
 */
static int
ipppd_release(struct inode *ino, struct file *file)
{
	uint minor = minor(ino->i_rdev) - ISDN_MINOR_PPP;
	struct ipppd *is;

	lock_kernel();

	is = file->private_data;

	if (is->debug & 0x1)
		printk(KERN_DEBUG "ippp: release, minor: %d %p\n", minor, is->idev);

	if (is->idev) {           /* a lp address says: this link is still up */
		/*
		 * isdn_net_hangup() calls isdn_ppp_free()
		 * isdn_ppp_free() sets is->lp to NULL and lp->ppp_slot to -1
		 * removing the IPPP_CONNECT flag omits calling of isdn_ppp_wakeup_daemon()
		 */
		is->state &= ~IPPP_CONNECT;
		isdn_net_hangup(is->idev);
	}
	skb_queue_purge(&is->rq);

	/* this slot is ready for new connections */
	is->state = 0;

	isdn_unlock_drivers();
	
	unlock_kernel();
	return 0;
}

/*
 * get_arg .. ioctl helper
 */
static int
get_arg(void *b, void *val, int len)
{
	if (len <= 0)
		len = sizeof(void *);
	if (copy_from_user((void *) val, b, len))
		return -EFAULT;
	return 0;
}

/*
 * set arg .. ioctl helper
 */
static int
set_arg(void *b, void *val,int len)
{
	if (copy_to_user(b, (void *) val, len))
		return -EFAULT;
	return 0;
}

/*
 * ippp device ioctl
 */
static int
ipppd_ioctl(struct inode *ino, struct file *file, unsigned int cmd, unsigned long arg)
{
	isdn_net_dev *idev;
	unsigned long val;
	int r;
	struct ipppd *is;
	struct isdn_ppp_comp_data data;

	is = (struct ipppd *) file->private_data;
	idev = is->idev;

	if (is->debug & 0x1)
		printk(KERN_DEBUG "isdn_ppp_ioctl: minor: %d cmd: %x state: %x\n", is->minor, cmd, is->state);

	if (!(is->state & IPPP_OPEN))
		return -EINVAL;

	switch (cmd) {
		case PPPIOCBUNDLE:
#ifdef CONFIG_ISDN_MPP
			if (!(is->state & IPPP_CONNECT))
				return -EINVAL;
			if ((r = get_arg((void *) arg, &val, sizeof(val) )))
				return r;
			printk(KERN_DEBUG "iPPP-bundle: minor: %d, slave unit: %d, master unit: %d\n",
			       (int) is->minor, (int) is->unit, (int) val);
			return isdn_ppp_bundle(is, val);
#else
			return -1;
#endif
			break;
		case PPPIOCGUNIT:	/* get ppp/isdn unit number */
			if ((r = set_arg((void *) arg, &is->unit, sizeof(is->unit) )))
				return r;
			break;
		case PPPIOCGIFNAME:
			if(!idev)
				return -EINVAL;
			if ((r = set_arg((void *) arg, idev->name, strlen(idev->name))))
				return r;
			break;
		case PPPIOCGMPFLAGS:	/* get configuration flags */
			if (!idev)
				return -ENODEV;
			if ((r = set_arg((void *) arg, &idev->mlp->mpppcfg, sizeof(idev->mlp->mpppcfg) )))
				return r;
			break;
		case PPPIOCSMPFLAGS:	/* set configuration flags */
			if (!idev)
				return -ENODEV;
			if ((r = get_arg((void *) arg, &val, sizeof(val) )))
				return r;
			idev->mlp->mpppcfg = val;
			break;
		case PPPIOCGFLAGS:	/* get configuration flags */
			if (!idev)
				return -ENODEV;
			if ((r = set_arg((void *) arg, &idev->pppcfg, sizeof(idev->pppcfg) )))
				return r;
			break;
		case PPPIOCSFLAGS:	/* set configuration flags */
			if (!idev)
				return -ENODEV;
			if ((r = get_arg((void *) arg, &val, sizeof(val) ))) {
				return r;
			}
			if ((val & SC_ENABLE_IP) && !(idev->pppcfg & SC_ENABLE_IP)) {
				/* OK .. we are ready to send buffers */
				isdn_net_online(idev);
			}
			idev->pppcfg = val;
			break;
		case PPPIOCGIDLE:	/* get idle time information */
			if (idev) {
				struct ppp_idle pidle;
				pidle.xmit_idle = pidle.recv_idle = idev->huptimer;
				if ((r = set_arg((void *) arg, &pidle,sizeof(struct ppp_idle))))
					 return r;
			}
			break;
		case PPPIOCSMRU:	/* set receive unit size for PPP */
			if (!idev)
				return -ENODEV;
			if ((r = get_arg((void *) arg, &val, sizeof(val) )))
				return r;
			return ippp_ccp_set_mru(idev->ccp, val);
		case PPPIOCSMPMRU:
			break;
		case PPPIOCSMPMTU:
			break;
#ifdef CONFIG_ISDN_PPP_VJ
		case PPPIOCSMAXCID:	/* set the maximum compression slot id */
		{
			struct slcompress *sltmp;
			
			if (!idev)
				return -ENODEV;
			if ((r = get_arg((void *) arg, &val, sizeof(val) )))
				return r;
			val++;
			if (is->debug & 0x1)
				printk(KERN_DEBUG "ippp, ioctl: changed MAXCID to %ld\n", val);
			sltmp = slhc_init(16, val);
			if (!sltmp) {
				printk(KERN_ERR "ippp, can't realloc slhc struct\n");
				return -ENOMEM;
			}
			if (idev->mlp->slcomp)
				slhc_free(idev->mlp->slcomp);
			idev->mlp->slcomp = sltmp;
			break;
		}
#endif
		case PPPIOCGDEBUG:
			if ((r = set_arg((void *) arg, &is->debug, sizeof(is->debug) )))
				return r;
			break;
		case PPPIOCSDEBUG:
			if ((r = get_arg((void *) arg, &val, sizeof(val) )))
				return r;
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
				if ((r = set_arg((void *) arg,protos,8*sizeof(long) )))
					return r;
			}
			break;
		case PPPIOCSCOMPRESSOR:
			if ((r = get_arg((void *) arg, &data, sizeof(struct isdn_ppp_comp_data))))
				return r;
			return isdn_ppp_set_compressor(is, &data);
		case PPPIOCGCALLINFO:
			{
				isdn_net_local *mlp;
				struct isdn_net_phone *phone;
				struct pppcallinfo pci;
				int i;
				memset((char *) &pci,0,sizeof(struct pppcallinfo));
				if(idev) {
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
					if(idev->outgoing)
						pci.calltype = CALLTYPE_OUTGOING;
					else
						pci.calltype = CALLTYPE_INCOMING;
					if(mlp->flags & ISDN_NET_CALLBACK)
						pci.calltype |= CALLTYPE_CALLBACK;
				}
				return set_arg((void *)arg,&pci,sizeof(struct pppcallinfo));
			}
		default:
			break;
	}
	return 0;
}

static unsigned int
ipppd_poll(struct file *file, poll_table * wait)
{
	unsigned int mask;
	struct ipppd *is;

	is = file->private_data;

	if (is->debug & 0x2)
		printk(KERN_DEBUG "isdn_ppp_poll: minor: %d\n",
				minor(file->f_dentry->d_inode->i_rdev));

	/* just registers wait_queue hook. This doesn't really wait. */
	poll_wait(file, &is->wq, wait);

	if (!(is->state & IPPP_OPEN)) {
		if(is->state == IPPP_CLOSEWAIT) {
			mask = POLLHUP;
			goto out;
		}
		printk(KERN_DEBUG "isdn_ppp: device not open\n");
		mask = POLLERR;
		goto out;
	}
	/* we're always ready to send .. */
	mask = POLLOUT | POLLWRNORM;

	/*
	 * if IPPP_NOBLOCK is set we return even if we have nothing to read
	 */
	if (!skb_queue_empty(&is->rq) || is->state & IPPP_NOBLOCK) {
		is->state &= ~IPPP_NOBLOCK;
		mask |= POLLIN | POLLRDNORM;
		set_current_state(TASK_INTERRUPTIBLE); // FIXME
		schedule_timeout(HZ);
	}

 out:
	return mask;
}

/*
 *  fill up isdn_ppp_read() queue ..
 */

static int
isdn_ppp_fill_rq(unsigned char *buf, int len, int proto, int slot)
{
	struct sk_buff *skb;
	unsigned char *p;
	struct ipppd *is;
	int retval;

	is = ipppd_get(slot);
	if (!is)
		return -ENODEV;

	if (!(is->state & IPPP_CONNECT)) {
		printk(KERN_DEBUG "ippp: device not activated.\n");
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
	ipppd_put(is);
	return retval;
}

/*
 * read() .. non-blocking: ipppd calls it only after select()
 *           reports, that there is data
 */

static ssize_t
ipppd_read(struct file *file, char *buf, size_t count, loff_t *off)
{
	struct ipppd *is;
	struct sk_buff *skb;
	int retval;

	if (off != &file->f_pos)
		return -ESPIPE;
	
	is = file->private_data;

	if (!(is->state & IPPP_OPEN)) {
		retval = 0;
		goto out;
	}
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

/*
 * ipppd wanna write a packet to the card .. non-blocking
 */

static ssize_t
ipppd_write(struct file *file, const char *buf, size_t count, loff_t *off)
{
	isdn_net_dev *idev;
	struct ipppd *is;
	int proto;
	unsigned char protobuf[4];
	int retval;

	if (off != &file->f_pos)
		return -ESPIPE;

	lock_kernel();

	is = file->private_data;

	if (!(is->state & IPPP_CONNECT)) {
		retval = 0;
		goto out;
	}

	/* -> push it directly to the lowlevel interface */

	idev = is->idev;
	if (!idev)
		printk(KERN_DEBUG "isdn_ppp_write: idev == NULL\n");
	else {
		/*
		 * Don't reset huptimer for
		 * LCP packets. (Echo requests).
		 */
		if (copy_from_user(protobuf, buf, 4)) {
			retval = -EFAULT;
			goto out;
		}
		proto = PPP_PROTOCOL(protobuf);
		if (proto != PPP_LCP)
			idev->huptimer = 0;

		if (idev->isdn_slot < 0) {
			retval = 0;
			goto out;
		}
		if ((dev->drv[isdn_slot_driver(idev->isdn_slot)]->flags & DRV_FLAG_RUNNING)) {
			unsigned short hl;
			struct sk_buff *skb;
			/*
			 * we need to reserve enought space in front of
			 * sk_buff. old call to dev_alloc_skb only reserved
			 * 16 bytes, now we are looking what the driver want
			 */
			hl = isdn_slot_hdrlen(idev->isdn_slot);
			skb = alloc_skb(hl+count, GFP_ATOMIC);
			if (!skb) {
				printk(KERN_WARNING "isdn_ppp_write: out of memory!\n");
				retval = count;
				goto out;
			}
			skb_reserve(skb, hl);
			if (copy_from_user(skb_put(skb, count), buf, count))
			{
				kfree_skb(skb);
				retval = -EFAULT;
				goto out;
			}
			if (is->debug & 0x40) {
				printk(KERN_DEBUG "ppp xmit: len %d\n", (int) skb->len);
				isdn_ppp_frame_log("xmit", skb->data, skb->len, 32,is->unit,idev->ppp_slot);
			}

			isdn_ppp_send_ccp(idev,idev->mlp,skb); /* keeps CCP/compression states in sync */

			isdn_net_write_super(idev, skb);
		}
	}
	retval = count;
	
 out:
	unlock_kernel();
	return retval;
}

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

/*
 * init memory, structures etc.
 */

int
isdn_ppp_init(void)
{
	int i;
	 
#ifdef CONFIG_ISDN_MPP
	if( isdn_ppp_mp_bundle_array_init() < 0 )
		return -ENOMEM;
#endif /* CONFIG_ISDN_MPP */

	for (i = 0; i < NR_IPPPDS; i++) {
		ipppds[i] = kmalloc(sizeof(struct ipppd), GFP_KERNEL);
		if (!ipppds[i]) {
			printk(KERN_WARNING "isdn_ppp_init: Could not alloc ippp_table\n");
			for (i--; i >= 0; i++)
				kfree(ipppds[i]);
			return -ENOMEM;
		}
		memset(ipppds[i], 0, sizeof(struct ipppd));
		ipppds[i]->state = 0;
		skb_queue_head_init(&ipppds[i]->rq);
	}
	return 0;
}

void
isdn_ppp_cleanup(void)
{
	int i;

	for (i = 0; i < NR_IPPPDS; i++)
		kfree(ipppds[i]);

#ifdef CONFIG_ISDN_MPP
	if (isdn_ppp_bundle_arr)
		kfree(isdn_ppp_bundle_arr);
#endif /* CONFIG_ISDN_MPP */

}

/*
 * check for address/control field and skip if allowed
 * retval != 0 -> discard packet silently
 */
static int isdn_ppp_skip_ac(isdn_net_dev *idev, struct sk_buff *skb) 
{
	if (skb->len < 1)
		return -1;

	if (skb->data[0] == 0xff) {
		if (skb->len < 2)
			return -1;

		if (skb->data[1] != 0x03)
			return -1;

		// skip address/control (AC) field
		skb_pull(skb, 2);
	} else { 
		if (idev->pppcfg & SC_REJ_COMP_AC)
			// if AC compression was not negotiated, but used, discard packet
			return -1;
	}
	return 0;
}

/*
 * get the PPP protocol header and pull skb
 * retval < 0 -> discard packet silently
 */
int isdn_ppp_strip_proto(struct sk_buff *skb) 
{
	int proto;
	
	if (skb->len < 1)
		return -1;

	if (skb->data[0] & 0x1) {
		// protocol field is compressed
		proto = skb->data[0];
		skb_pull(skb, 1);
	} else {
		if (skb->len < 2)
			return -1;
		proto = ((int) skb->data[0] << 8) + skb->data[1];
		skb_pull(skb, 2);
	}
	return proto;
}


/*
 * handler for incoming packets on a syncPPP interface
 */
static void isdn_ppp_receive(isdn_net_local *lp, isdn_net_dev *idev, 
			     struct sk_buff *skb)
{
	struct ipppd *is;
	int proto;

	/*
	 * If encapsulation is syncppp, don't reset
	 * huptimer on LCP packets.
	 */
	if (PPP_PROTOCOL(skb->data) != PPP_LCP)
		idev->huptimer = 0;

	is = ipppd_get(idev->ppp_slot);
	if (!is) 
		goto err;

	if (is->debug & 0x4) {
		printk(KERN_DEBUG "ippp_receive: is:%p lp:%p slot:%d unit:%d len:%d\n",
		       is,lp,idev->ppp_slot,is->unit,(int) skb->len);
		isdn_ppp_frame_log("receive", skb->data, skb->len, 32,is->unit,idev->ppp_slot);
	}

 	if (isdn_ppp_skip_ac(idev, skb) < 0)
		goto err_put;

  	proto = isdn_ppp_strip_proto(skb);
 	if (proto < 0)
		goto err_put;
  
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
	ipppd_put(is);
	return;

 err_put:
	ipppd_put(is);
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
		     struct sk_buff *skb, int proto)
{
	struct net_device *dev = &lp->dev;
 	struct ipppd *is;

	is = ipppd_get(idev->ppp_slot);
	if (!is)
		goto drop;
 	
	if (is->debug & 0x10) {
		printk(KERN_DEBUG "push, skb %d %04x\n", (int) skb->len, proto);
		isdn_ppp_frame_log("rpush", skb->data, skb->len, 32,is->unit, idev->ppp_slot);
	}
	skb = ippp_ccp_decompress(lp->ccp, skb, &proto);
	if (!skb) // decompression error
		goto put;

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
			goto drop_put;
#ifdef CONFIG_ISDN_PPP_VJ
		case PPP_VJC_UNCOMP:
			if (is->debug & 0x20)
				printk(KERN_DEBUG "isdn_ppp: VJC_UNCOMP\n");
			if (slhc_remember(lp->slcomp, skb->data, skb->len) <= 0) {
				printk(KERN_WARNING "isdn_ppp: received illegal VJC_UNCOMP frame!\n");
				goto drop_put;
			}
			skb->protocol = htons(ETH_P_IP);
			break;
		case PPP_VJC_COMP:
			if (is->debug & 0x20)
				printk(KERN_DEBUG "isdn_ppp: VJC_COMP\n");
			{
				struct sk_buff *skb_old = skb;
				int pkt_len;
				skb = dev_alloc_skb(skb_old->len + 128);

				if (!skb) {
					printk(KERN_WARNING "%s: Memory squeeze, dropping packet.\n", dev->name);
					skb = skb_old;
					goto drop_put;
				}
				skb_put(skb, skb_old->len + 128);
				memcpy(skb->data, skb_old->data, skb_old->len);
				pkt_len = slhc_uncompress(lp->slcomp,
						skb->data, skb_old->len);
				kfree_skb(skb_old);
				if (pkt_len < 0)
					goto drop_put;

				skb_trim(skb, pkt_len);
				skb->protocol = htons(ETH_P_IP);
			}
			break;
#endif
		case PPP_CCP:
		case PPP_CCPFRAG:
			isdn_ppp_receive_ccp(idev,lp,skb,proto);
			/* Dont pop up ResetReq/Ack stuff to the daemon any
			   longer - the job is done already */
			if(skb->data[0] == CCP_RESETREQ ||
			   skb->data[0] == CCP_RESETACK) {
				kfree_skb(skb);
				goto put;
			}
			/* fall through */
		default:
			isdn_ppp_fill_rq(skb->data, skb->len, proto, idev->ppp_slot);	/* push data to pppd device */
			kfree_skb(skb);
			goto put;
	}

 	/* Reset hangup-timer */
 	idev->huptimer = 0;

	skb->dev = dev;
	netif_rx(skb);
	/* net_dev->local->stats.rx_packets++; done in isdn_net.c */
 put:
	ipppd_put(is);
	return;

 drop_put:
	ipppd_put(is);
 drop:
	lp->stats.rx_dropped++;
	kfree_skb(skb);
}

/*
 * isdn_ppp_skb_push ..
 * checks whether we have enough space at the beginning of the skb
 * and allocs a new SKB if necessary
 */
static unsigned char *isdn_ppp_skb_push(struct sk_buff **skb_p,int len)
{
	struct sk_buff *skb = *skb_p;

	if(skb_headroom(skb) < len) {
		struct sk_buff *nskb = skb_realloc_headroom(skb, len);

		if (!nskb) {
			printk(KERN_ERR "isdn_ppp_skb_push: can't realloc headroom!\n");
			dev_kfree_skb(skb);
			return NULL;
		}
		printk(KERN_DEBUG "isdn_ppp_skb_push:under %d %d\n",skb_headroom(skb),len);
		dev_kfree_skb(skb);
		*skb_p = nskb;
		return skb_push(nskb, len);
	}
	return skb_push(skb,len);
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
	unsigned int proto = PPP_IP;     /* 0x21 */
	struct ipppd *ipt,*ipts;

	ndev->trans_start = jiffies;

	if (list_empty(&mlp->online))
		return isdn_net_autodial(skb, ndev);

	ipts = ipppd_get(idev->ppp_slot);
	if (!ipts) {
		isdn_BUG();
		goto err;
	}
		
	if (!(idev->pppcfg & SC_ENABLE_IP)) {	/* PPP connected ? */
		printk(KERN_INFO "%s: IP frame delayed.\n", ndev->name);
		goto err_put;
	}

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
			goto put;
	}

	idev = isdn_net_get_xmit_dev(mlp);
	if (!idev) {
		isdn_BUG();
		goto err_put;
	}

	ipt = ipppd_get(idev->ppp_slot);
	if (!ipt)
		goto put;

	idev->huptimer = 0;

	/*
	 * after this line .. requeueing in the device queue is no longer allowed!!!
	 */

	if (ipt->debug & 0x4)
		printk(KERN_DEBUG "xmit skb, len %d\n", (int) skb->len);
        if (ipts->debug & 0x40)
                isdn_ppp_frame_log("xmit0", skb->data, skb->len, 32,ipts->unit,idev->ppp_slot);

#ifdef CONFIG_ISDN_PPP_VJ
	if (proto == PPP_IP && idev->pppcfg & SC_COMP_TCP) {	/* ipts here? probably yes, but check this again */
		struct sk_buff *new_skb;
	        unsigned short hl;
		/*
		 * we need to reserve enought space in front of
		 * sk_buff. old call to dev_alloc_skb only reserved
		 * 16 bytes, now we are looking what the driver want.
		 */
		hl = isdn_slot_hdrlen(idev->isdn_slot) + IPPP_MAX_HEADER;;
		/* 
		 * Note: hl might still be insufficient because the method
		 * above does not account for a possibible MPPP slave channel
		 * which had larger HL header space requirements than the
		 * master.
		 */
		new_skb = alloc_skb(hl+skb->len, GFP_ATOMIC);
		if (new_skb) {
			u_char *buf;
			int pktlen;

			skb_reserve(new_skb, hl);
			new_skb->dev = skb->dev;
			skb_put(new_skb, skb->len);
			buf = skb->data;

			pktlen = slhc_compress(mlp->slcomp, skb->data, skb->len, new_skb->data,
				 &buf, !(idev->pppcfg & SC_NO_TCP_CCID));

			if (buf != skb->data) {	
				if (new_skb->data != buf)
					printk(KERN_ERR "isdn_ppp: FATAL error after slhc_compress!!\n");
				dev_kfree_skb(skb);
				skb = new_skb;
			} else {
				dev_kfree_skb(new_skb);
			}

			skb_trim(skb, pktlen);
			if (skb->data[0] & SL_TYPE_COMPRESSED_TCP) {	/* cslip? style -> PPP */
				proto = PPP_VJC_COMP;
				skb->data[0] ^= SL_TYPE_COMPRESSED_TCP;
			} else {
				if (skb->data[0] >= SL_TYPE_UNCOMPRESSED_TCP)
					proto = PPP_VJC_UNCOMP;
				skb->data[0] = (skb->data[0] & 0x0f) | 0x40;
			}
		}
	}
#endif

	/*
	 * normal (single link) or bundle compression
	 */
	skb = ippp_ccp_compress(mlp->ccp, skb, &proto);

	if (ipt->debug & 0x24)
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

	if ((idev->pppcfg & SC_COMP_PROT) && (proto <= 0xff)) {
		unsigned char *data = isdn_ppp_skb_push(&skb,1);
		if(!data)
			goto put2;
		data[0] = proto & 0xff;
	} else {
		unsigned char *data = isdn_ppp_skb_push(&skb,2);
		if(!data)
			goto put2;
		data[0] = (proto >> 8) & 0xff;
		data[1] = proto & 0xff;
	}
	if (!(idev->pppcfg & SC_COMP_AC)) {
		unsigned char *data = isdn_ppp_skb_push(&skb,2);
		if(!data)
			goto put2;
		data[0] = 0xff;    /* All Stations */
		data[1] = 0x03;    /* Unnumbered information */
	}

	/* tx-stats are now updated via BSENT-callback */

	if (ipts->debug & 0x40) {
		printk(KERN_DEBUG "skb xmit: len: %d\n", (int) skb->len);
		isdn_ppp_frame_log("xmit", skb->data, skb->len, 32,ipt->unit,idev->ppp_slot);
	}
	
	isdn_net_writebuf_skb(idev, skb);

 put2:
	ipppd_put(ipt);
 put:
	ipppd_put(ipts);
	return 0;

 err_put:
	ipppd_put(ipts);
 err:
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
isdn_ppp_dev_ioctl_stats(int slot, struct ifreq *ifr, struct net_device *dev)
{
	struct ppp_stats *res, t;
	struct ipppd *is;
	isdn_net_local *lp = (isdn_net_local *) dev->priv;
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
		is = ipppd_get(slot);
		if (is) {
			struct slcompress *slcomp = lp->slcomp;
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
			ipppd_put(is);
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
			if(copy_to_user(r, PPP_VERSION, len)) error = -EFAULT;
			break;
		case SIOCGPPPSTATS:
			error = isdn_ppp_dev_ioctl_stats(0, ifr, dev);
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

static void isdn_ppp_ccp_kick_up(void *priv, unsigned int flags)
{
	isdn_net_dev *idev = priv;

	idev->pppcfg |= flags;
	isdn_ppp_fill_rq(NULL, 0, PPP_COMP, idev->ppp_slot);
}

static void isdn_ppp_ccp_lp_kick_up(void *priv, unsigned int flags)
{
	isdn_net_local *lp = priv;
	isdn_net_dev *idev;

	if (list_empty(&lp->online)) {
		isdn_BUG();
		return;
	}
	idev = list_entry(lp->online.next, isdn_net_dev, online);
	idev->pppcfg |= flags;
	isdn_ppp_fill_rq(NULL, 0, PPP_COMP, idev->ppp_slot);
}

/* Send a CCP Reset-Request or Reset-Ack directly from the kernel. This is
   getting that lengthy because there is no simple "send-this-frame-out"
   function above but every wrapper does a bit different. Hope I guess
   correct in this hack... */

static void isdn_ppp_ccp_xmit_reset(void *priv, int proto,
				    unsigned char code, unsigned char id,
				    unsigned char *data, int len)
{
	isdn_net_dev *idev = priv;
	struct sk_buff *skb;
	unsigned char *p;
	int hl;
	int cnt = 0;

	/* Alloc large enough skb */
	hl = isdn_slot_hdrlen(idev->isdn_slot);
	skb = alloc_skb(len + hl + 16,GFP_ATOMIC);
	if(!skb) {
		printk(KERN_WARNING
		       "ippp: CCP cannot send reset - out of memory\n");
		return;
	}
	skb_reserve(skb, hl+16);

	/* We may need to stuff an address and control field first */
	if (!(idev->pppcfg & SC_COMP_AC)) {
		p = skb_put(skb, 2);
		*p++ = 0xff;
		*p++ = 0x03;
	}

	/* Stuff proto, code, id and length */
	p = skb_put(skb, 6);
	*p++ = (proto >> 8);
	*p++ = (proto & 0xff);
	*p++ = code;
	*p++ = id;
	cnt = 4 + len;
	*p++ = (cnt >> 8);
	*p++ = (cnt & 0xff);

	/* Now stuff remaining bytes */
	if(len) {
		p = skb_put(skb, len);
		memcpy(p, data, len);
	}

	/* skb is now ready for xmit */
	isdn_ppp_frame_log("ccp-xmit", skb->data, skb->len, 32, -1, idev->ppp_slot);

	isdn_net_write_super(idev, skb);
}

static void isdn_ppp_ccp_lp_xmit_reset(void *priv, int proto,
				       unsigned char code, unsigned char id,
				       unsigned char *data, int len)
{
	isdn_net_local *lp = priv;
	isdn_net_dev *idev;

	if (list_empty(&lp->online)) {
		isdn_BUG();
		return;
	}
	idev = list_entry(lp->online.next, isdn_net_dev, online);
	isdn_ppp_ccp_xmit_reset(idev, proto, code, id, data, len);
}


/*
 * we received a CCP frame .. 
 * not a clean solution, but we MUST handle a few cases in the kernel
 */
static void
isdn_ppp_receive_ccp(isdn_net_dev *idev, isdn_net_local *lp,
		     struct sk_buff *skb,int proto)
{
	if (proto == PPP_CCP)
		ippp_ccp_receive_ccp(lp->ccp, skb);
	else
		ippp_ccp_receive_ccp(idev->ccp, skb);
}


/*
 * Daemon sends a CCP frame ...
 */

static void isdn_ppp_send_ccp(isdn_net_dev *idev, isdn_net_local *lp, struct sk_buff *skb)
{
	struct ipppd *is;
	int proto;
	unsigned char *data;

	if (!skb || skb->len < 3) {
		isdn_BUG();
		return;
	}
	is = ipppd_get(idev->ppp_slot);
	if (!is) {
		isdn_BUG();
		return;
	}
	/* Daemon may send with or without address and control field comp */
	data = skb->data;
	if (data[0] == 0xff && data[1] == 0x03) {
		data += 2;
		if(skb->len < 5)
			return;
	}
	proto = ((int)data[0]<<8)+data[1];

	switch (proto) {
	case PPP_CCP:
		ippp_ccp_send_ccp(lp->ccp, skb);
		break;
	case PPP_CCPFRAG:
		ippp_ccp_send_ccp(idev->ccp, skb);
		break;
	}
}

static int
isdn_ppp_set_compressor(struct ipppd *is, struct isdn_ppp_comp_data *data)
{
	isdn_net_dev *idev = is->idev;
	isdn_net_local *lp;
	struct ippp_ccp *ccp;

	if (!idev)
		return -ENODEV;

	lp = idev->mlp;

	if (data->flags & IPPP_COMP_FLAG_LINK)
		ccp = idev->ccp;
	else
		ccp = lp->ccp;

	return ippp_ccp_set_compressor(ccp, is->unit, data);
}

// ISDN_NET_ENCAP_SYNCPPP
// ======================================================================

static int
isdn_ppp_open(isdn_net_local *lp)
{
	lp->mpppcfg = 0;        /* mppp configuration */
	lp->mp_seqno = 0;       /* MP sequence number */

#ifdef CONFIG_ISDN_PPP_VJ
	lp->slcomp = slhc_init(16, 16);
#endif
	lp->ccp = ippp_ccp_alloc(PPP_COMPFRAG, lp, isdn_ppp_ccp_lp_xmit_reset,
				   isdn_ppp_ccp_lp_kick_up);
	if (!lp->ccp)
		return -ENOMEM;

	return 0;
}

static void
isdn_ppp_close(isdn_net_local *lp)
{
#ifdef CONFIG_ISDN_PPP_VJ
	slhc_free(lp->slcomp);
	lp->slcomp = NULL;
#endif
	ippp_ccp_free(lp->ccp);
}

static void
isdn_ppp_disconnected(isdn_net_dev *idev)
{
	if (idev->pppcfg & SC_ENABLE_IP)
		isdn_net_offline(idev);
}

struct isdn_netif_ops isdn_ppp_ops = {
	.hard_start_xmit     = isdn_ppp_start_xmit,
	.do_ioctl            = isdn_ppp_dev_ioctl,
	.flags               = IFF_NOARP | IFF_POINTOPOINT,
	.type                = ARPHRD_PPP,
	.receive             = isdn_ppp_receive,
	.connected           = isdn_ppp_wakeup_daemon,
	.disconnected        = isdn_ppp_disconnected,
	.bind                = isdn_ppp_bind,
	.unbind              = isdn_ppp_free,
	.open                = isdn_ppp_open,
	.close               = isdn_ppp_close,
};
