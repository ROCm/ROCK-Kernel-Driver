/*
 * Generic PPP layer for Linux.
 *
 * Copyright 1999-2000 Paul Mackerras.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 * The generic PPP layer handles the PPP network interfaces, the
 * /dev/ppp device, packet and VJ compression, and multilink.
 * It talks to PPP `channels' via the interface defined in
 * include/linux/ppp_channel.h.  Channels provide the basic means for
 * sending and receiving PPP frames on some kind of communications
 * channel.
 *
 * Part of the code in this driver was inspired by the old async-only
 * PPP driver, written by Michael Callahan and Al Longyear, and
 * subsequently hacked by Paul Mackerras.
 *
 * ==FILEVERSION 20000417==
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/netdevice.h>
#include <linux/poll.h>
#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>
#include <linux/ppp_channel.h>
#include <linux/ppp-comp.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <net/slhc_vj.h>
#include <asm/atomic.h>

#define PPP_VERSION	"2.4.1"

/*
 * Network protocols we support.
 */
#define NP_IP	0		/* Internet Protocol V4 */
#define NP_IPV6	1		/* Internet Protocol V6 */
#define NP_IPX	2		/* IPX protocol */
#define NP_AT	3		/* Appletalk protocol */
#define NUM_NP	4		/* Number of NPs. */

#define MPHDRLEN	6	/* multilink protocol header length */
#define MPHDRLEN_SSN	4	/* ditto with short sequence numbers */
#define MIN_FRAG_SIZE	64

/*
 * An instance of /dev/ppp can be associated with either a ppp
 * interface unit or a ppp channel.  In both cases, file->private_data
 * points to one of these.
 */
struct ppp_file {
	enum {
		INTERFACE=1, CHANNEL
	}		kind;
	struct sk_buff_head xq;		/* pppd transmit queue */
	struct sk_buff_head rq;		/* receive queue for pppd */
	wait_queue_head_t rwait;	/* for poll on reading /dev/ppp */
	atomic_t	refcnt;		/* # refs (incl /dev/ppp attached) */
	int		hdrlen;		/* space to leave for headers */
	struct list_head list;		/* link in all_* list */
	int		index;		/* interface unit / channel number */
};

#define PF_TO_X(pf, X)	((X *)((char *)(pf)-(unsigned long)(&((X *)0)->file)))

#define PF_TO_PPP(pf)		PF_TO_X(pf, struct ppp)
#define PF_TO_CHANNEL(pf)	PF_TO_X(pf, struct channel)

#define ROUNDUP(n, x)		(((n) + (x) - 1) / (x))

/*
 * Data structure describing one ppp unit.
 * A ppp unit corresponds to a ppp network interface device
 * and represents a multilink bundle.
 * It can have 0 or more ppp channels connected to it.
 */
struct ppp {
	struct ppp_file	file;		/* stuff for read/write/poll */
	struct list_head channels;	/* list of attached channels */
	int		n_channels;	/* how many channels are attached */
	spinlock_t	rlock;		/* lock for receive side */
	spinlock_t	wlock;		/* lock for transmit side */
	int		mru;		/* max receive unit */
	unsigned int	flags;		/* control bits */
	unsigned int	xstate;		/* transmit state bits */
	unsigned int	rstate;		/* receive state bits */
	int		debug;		/* debug flags */
	struct slcompress *vj;		/* state for VJ header compression */
	enum NPmode	npmode[NUM_NP];	/* what to do with each net proto */
	struct sk_buff	*xmit_pending;	/* a packet ready to go out */
	struct compressor *xcomp;	/* transmit packet compressor */
	void		*xc_state;	/* its internal state */
	struct compressor *rcomp;	/* receive decompressor */
	void		*rc_state;	/* its internal state */
	unsigned long	last_xmit;	/* jiffies when last pkt sent */
	unsigned long	last_recv;	/* jiffies when last pkt rcvd */
	struct net_device *dev;		/* network interface device */
#ifdef CONFIG_PPP_MULTILINK
	int		nxchan;		/* next channel to send something on */
	u32		nxseq;		/* next sequence number to send */
	int		mrru;		/* MP: max reconst. receive unit */
	u32		nextseq;	/* MP: seq no of next packet */
	u32		minseq;		/* MP: min of most recent seqnos */
	struct sk_buff_head mrq;	/* MP: receive reconstruction queue */
#endif /* CONFIG_PPP_MULTILINK */
	struct net_device_stats stats;	/* statistics */
};

/*
 * Bits in flags: SC_NO_TCP_CCID, SC_CCP_OPEN, SC_CCP_UP, SC_LOOP_TRAFFIC,
 * SC_MULTILINK, SC_MP_SHORTSEQ, SC_MP_XSHORTSEQ, SC_COMP_TCP, SC_REJ_COMP_TCP.
 * Bits in rstate: SC_DECOMP_RUN, SC_DC_ERROR, SC_DC_FERROR.
 * Bits in xstate: SC_COMP_RUN
 */
#define SC_FLAG_BITS	(SC_NO_TCP_CCID|SC_CCP_OPEN|SC_CCP_UP|SC_LOOP_TRAFFIC \
			 |SC_MULTILINK|SC_MP_SHORTSEQ|SC_MP_XSHORTSEQ \
			 |SC_COMP_TCP|SC_REJ_COMP_TCP)

/*
 * Private data structure for each channel.
 * This includes the data structure used for multilink.
 */
struct channel {
	struct ppp_file	file;		/* stuff for read/write/poll */
	struct ppp_channel *chan;	/* public channel data structure */
	spinlock_t	downl;		/* protects `chan', file.xq dequeue */
	struct ppp	*ppp;		/* ppp unit we're connected to */
	struct list_head clist;		/* link in list of channels per unit */
	rwlock_t	upl;		/* protects `ppp' and `ulist' */
#ifdef CONFIG_PPP_MULTILINK
	u8		avail;		/* flag used in multilink stuff */
	u8		had_frag;	/* >= 1 fragments have been sent */
	u32		lastseq;	/* MP: last sequence # received */
#endif /* CONFIG_PPP_MULTILINK */
};

/*
 * SMP locking issues:
 * Both the ppp.rlock and ppp.wlock locks protect the ppp.channels
 * list and the ppp.n_channels field, you need to take both locks
 * before you modify them.
 * The lock ordering is: channel.upl -> ppp.wlock -> ppp.rlock ->
 * channel.downl.
 */

/*
 * all_ppp_lock protects the all_ppp_units.
 * It also ensures that finding a ppp unit in the all_ppp_units list
 * and updating its file.refcnt field is atomic.
 */
static spinlock_t all_ppp_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(all_ppp_units);

/*
 * all_channels_lock protects all_channels and last_channel_index,
 * and the atomicity of find a channel and updating its file.refcnt
 * field.
 */
static spinlock_t all_channels_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(all_channels);
static int last_channel_index;

/* Get the PPP protocol number from a skb */
#define PPP_PROTO(skb)	(((skb)->data[0] << 8) + (skb)->data[1])

/* We limit the length of ppp->file.rq to this (arbitrary) value */
#define PPP_MAX_RQLEN	32

/*
 * Maximum number of multilink fragments queued up.
 * This has to be large enough to cope with the maximum latency of
 * the slowest channel relative to the others.  Strictly it should
 * depend on the number of channels and their characteristics.
 */
#define PPP_MP_MAX_QLEN	128

/* Multilink header bits. */
#define B	0x80		/* this fragment begins a packet */
#define E	0x40		/* this fragment ends a packet */

/* Compare multilink sequence numbers (assumed to be 32 bits wide) */
#define seq_before(a, b)	((s32)((a) - (b)) < 0)
#define seq_after(a, b)		((s32)((a) - (b)) > 0)

/* Prototypes. */
static ssize_t ppp_file_read(struct ppp_file *pf, struct file *file,
			     char *buf, size_t count);
static ssize_t ppp_file_write(struct ppp_file *pf, const char *buf,
			      size_t count);
static int ppp_unattached_ioctl(struct ppp_file *pf, struct file *file,
				unsigned int cmd, unsigned long arg);
static void ppp_xmit_process(struct ppp *ppp);
static void ppp_send_frame(struct ppp *ppp, struct sk_buff *skb);
static void ppp_push(struct ppp *ppp);
static void ppp_channel_push(struct channel *pch);
static void ppp_receive_frame(struct ppp *ppp, struct sk_buff *skb,
			      struct channel *pch);
static void ppp_receive_error(struct ppp *ppp);
static void ppp_receive_nonmp_frame(struct ppp *ppp, struct sk_buff *skb);
static struct sk_buff *ppp_decompress_frame(struct ppp *ppp,
					    struct sk_buff *skb);
#ifdef CONFIG_PPP_MULTILINK
static void ppp_receive_mp_frame(struct ppp *ppp, struct sk_buff *skb,
				struct channel *pch);
static void ppp_mp_insert(struct ppp *ppp, struct sk_buff *skb);
static struct sk_buff *ppp_mp_reconstruct(struct ppp *ppp);
static int ppp_mp_explode(struct ppp *ppp, struct sk_buff *skb);
#endif /* CONFIG_PPP_MULTILINK */
static int ppp_set_compress(struct ppp *ppp, unsigned long arg);
static void ppp_ccp_peek(struct ppp *ppp, struct sk_buff *skb, int inbound);
static void ppp_ccp_closed(struct ppp *ppp);
static struct compressor *find_compressor(int type);
static void ppp_get_stats(struct ppp *ppp, struct ppp_stats *st);
static struct ppp *ppp_create_interface(int unit, int *retp);
static void init_ppp_file(struct ppp_file *pf, int kind);
static void ppp_destroy_interface(struct ppp *ppp);
static struct ppp *ppp_find_unit(int unit);
static struct channel *ppp_find_channel(int unit);
static int ppp_connect_channel(struct channel *pch, int unit);
static int ppp_disconnect_channel(struct channel *pch);
static void ppp_destroy_channel(struct channel *pch);

/* Translates a PPP protocol number to a NP index (NP == network protocol) */
static inline int proto_to_npindex(int proto)
{
	switch (proto) {
	case PPP_IP:
		return NP_IP;
	case PPP_IPV6:
		return NP_IPV6;
	case PPP_IPX:
		return NP_IPX;
	case PPP_AT:
		return NP_AT;
	}
	return -EINVAL;
}

/* Translates an NP index into a PPP protocol number */
static const int npindex_to_proto[NUM_NP] = {
	PPP_IP,
	PPP_IPV6,
	PPP_IPX,
	PPP_AT,
};
	
/* Translates an ethertype into an NP index */
static inline int ethertype_to_npindex(int ethertype)
{
	switch (ethertype) {
	case ETH_P_IP:
		return NP_IP;
	case ETH_P_IPV6:
		return NP_IPV6;
	case ETH_P_IPX:
		return NP_IPX;
	case ETH_P_PPPTALK:
	case ETH_P_ATALK:
		return NP_AT;
	}
	return -1;
}

/* Translates an NP index into an ethertype */
static const int npindex_to_ethertype[NUM_NP] = {
	ETH_P_IP,
	ETH_P_IPV6,
	ETH_P_IPX,
	ETH_P_PPPTALK,
};

/*
 * Locking shorthand.
 */
#define ppp_xmit_lock(ppp)	spin_lock_bh(&(ppp)->wlock)
#define ppp_xmit_unlock(ppp)	spin_unlock_bh(&(ppp)->wlock)
#define ppp_recv_lock(ppp)	spin_lock_bh(&(ppp)->rlock)
#define ppp_recv_unlock(ppp)	spin_unlock_bh(&(ppp)->rlock)
#define ppp_lock(ppp)		do { ppp_xmit_lock(ppp); \
				     ppp_recv_lock(ppp); } while (0)
#define ppp_unlock(ppp)		do { ppp_recv_unlock(ppp); \
				     ppp_xmit_unlock(ppp); } while (0)


/*
 * /dev/ppp device routines.
 * The /dev/ppp device is used by pppd to control the ppp unit.
 * It supports the read, write, ioctl and poll functions.
 * Open instances of /dev/ppp can be in one of three states:
 * unattached, attached to a ppp unit, or attached to a ppp channel.
 */
static int ppp_open(struct inode *inode, struct file *file)
{
	/*
	 * This could (should?) be enforced by the permissions on /dev/ppp.
	 */
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;
	return 0;
}

static int ppp_release(struct inode *inode, struct file *file)
{
	struct ppp_file *pf = (struct ppp_file *) file->private_data;

	lock_kernel();
	if (pf != 0) {
		file->private_data = 0;
		if (atomic_dec_and_test(&pf->refcnt)) {
			switch (pf->kind) {
			case INTERFACE:
				ppp_destroy_interface(PF_TO_PPP(pf));
				break;
			case CHANNEL:
				ppp_destroy_channel(PF_TO_CHANNEL(pf));
				break;
			}
		}
	}
	unlock_kernel();
	return 0;
}

static ssize_t ppp_read(struct file *file, char *buf,
			size_t count, loff_t *ppos)
{
	struct ppp_file *pf = (struct ppp_file *) file->private_data;

	return ppp_file_read(pf, file, buf, count);
}

static ssize_t ppp_file_read(struct ppp_file *pf, struct file *file,
			     char *buf, size_t count)
{
	DECLARE_WAITQUEUE(wait, current);
	ssize_t ret;
	struct sk_buff *skb = 0;

	ret = -ENXIO;
	if (pf == 0)
		goto out;		/* not currently attached */

	add_wait_queue(&pf->rwait, &wait);
	current->state = TASK_INTERRUPTIBLE;
	for (;;) {
		skb = skb_dequeue(&pf->rq);
		if (skb)
			break;
		ret = 0;
		if (pf->kind == CHANNEL && PF_TO_CHANNEL(pf)->chan == 0)
			break;
		ret = -EAGAIN;
		if (file->f_flags & O_NONBLOCK)
			break;
		ret = -ERESTARTSYS;
		if (signal_pending(current))
			break;
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&pf->rwait, &wait);

	if (skb == 0)
		goto out;

	ret = -EOVERFLOW;
	if (skb->len > count)
		goto outf;
	ret = -EFAULT;
	if (copy_to_user(buf, skb->data, skb->len))
		goto outf;
	ret = skb->len;

 outf:
	kfree_skb(skb);
 out:
	return ret;
}

static ssize_t ppp_write(struct file *file, const char *buf,
			 size_t count, loff_t *ppos)
{
	struct ppp_file *pf = (struct ppp_file *) file->private_data;

	return ppp_file_write(pf, buf, count);
}

static ssize_t ppp_file_write(struct ppp_file *pf, const char *buf,
			      size_t count)
{
	struct sk_buff *skb;
	ssize_t ret;

	ret = -ENXIO;
	if (pf == 0)
		goto out;

	ret = -ENOMEM;
	skb = alloc_skb(count + pf->hdrlen, GFP_KERNEL);
	if (skb == 0)
		goto out;
	skb_reserve(skb, pf->hdrlen);
	ret = -EFAULT;
	if (copy_from_user(skb_put(skb, count), buf, count)) {
		kfree_skb(skb);
		goto out;
	}

	skb_queue_tail(&pf->xq, skb);

	switch (pf->kind) {
	case INTERFACE:
		ppp_xmit_process(PF_TO_PPP(pf));
		break;
	case CHANNEL:
		ppp_channel_push(PF_TO_CHANNEL(pf));
		break;
	}

	ret = count;

 out:
	return ret;
}

/* No kernel lock - fine */
static unsigned int ppp_poll(struct file *file, poll_table *wait)
{
	struct ppp_file *pf = (struct ppp_file *) file->private_data;
	unsigned int mask;

	if (pf == 0)
		return 0;
	poll_wait(file, &pf->rwait, wait);
	mask = POLLOUT | POLLWRNORM;
	if (skb_peek(&pf->rq) != 0)
		mask |= POLLIN | POLLRDNORM;
	if (pf->kind == CHANNEL) {
		struct channel *pch = PF_TO_CHANNEL(pf);
		if (pch->chan == 0)
			mask |= POLLHUP;
	}
	return mask;
}

static int ppp_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	struct ppp_file *pf = (struct ppp_file *) file->private_data;
	struct ppp *ppp;
	int err = -EFAULT, val, val2, i;
	struct ppp_idle idle;
	struct npioctl npi;
	int unit;
	struct slcompress *vj;

	if (pf == 0)
		return ppp_unattached_ioctl(pf, file, cmd, arg);

	if (pf->kind == CHANNEL) {
		struct channel *pch = PF_TO_CHANNEL(pf);
		struct ppp_channel *chan;

		switch (cmd) {
		case PPPIOCCONNECT:
			if (get_user(unit, (int *) arg))
				break;
			err = ppp_connect_channel(pch, unit);
			break;

		case PPPIOCDISCONN:
			err = ppp_disconnect_channel(pch);
			break;

		case PPPIOCDETACH:
			file->private_data = 0;
			if (atomic_dec_and_test(&pf->refcnt))
				ppp_destroy_channel(pch);
			err = 0;
			break;

		default:
			spin_lock_bh(&pch->downl);
			chan = pch->chan;
			err = -ENOTTY;
			if (chan && chan->ops->ioctl)
				err = chan->ops->ioctl(chan, cmd, arg);
			spin_unlock_bh(&pch->downl);
		}
		return err;
	}

	if (pf->kind != INTERFACE) {
		/* can't happen */
		printk(KERN_ERR "PPP: not interface or channel??\n");
		return -EINVAL;
	}

	ppp = PF_TO_PPP(pf);
	switch (cmd) {
	case PPPIOCDETACH:
		file->private_data = 0;
		if (atomic_dec_and_test(&pf->refcnt))
			ppp_destroy_interface(ppp);
		err = 0;
		break;

	case PPPIOCSMRU:
		if (get_user(val, (int *) arg))
			break;
		ppp->mru = val;
		err = 0;
		break;

	case PPPIOCSFLAGS:
		if (get_user(val, (int *) arg))
			break;
		ppp_lock(ppp);
		if (ppp->flags & ~val & SC_CCP_OPEN)
			ppp_ccp_closed(ppp);
		ppp->flags = val & SC_FLAG_BITS;
		ppp_unlock(ppp);
		err = 0;
		break;

	case PPPIOCGFLAGS:
		val = ppp->flags | ppp->xstate | ppp->rstate;
		if (put_user(val, (int *) arg))
			break;
		err = 0;
		break;

	case PPPIOCSCOMPRESS:
		err = ppp_set_compress(ppp, arg);
		break;

	case PPPIOCGUNIT:
		if (put_user(ppp->file.index, (int *) arg))
			break;
		err = 0;
		break;

	case PPPIOCSDEBUG:
		if (get_user(val, (int *) arg))
			break;
		ppp->debug = val;
		err = 0;
		break;

	case PPPIOCGDEBUG:
		if (put_user(ppp->debug, (int *) arg))
			break;
		err = 0;
		break;

	case PPPIOCGIDLE:
		idle.xmit_idle = (jiffies - ppp->last_xmit) / HZ;
		idle.recv_idle = (jiffies - ppp->last_recv) / HZ;
		if (copy_to_user((void *) arg, &idle, sizeof(idle)))
			break;
		err = 0;
		break;

	case PPPIOCSMAXCID:
		if (get_user(val, (int *) arg))
			break;
		val2 = 15;
		if ((val >> 16) != 0) {
			val2 = val >> 16;
			val &= 0xffff;
		}
		vj = slhc_init(val2+1, val+1);
		if (vj == 0) {
			printk(KERN_ERR "PPP: no memory (VJ compressor)\n");
			err = -ENOMEM;
			break;
		}
		ppp_lock(ppp);
		if (ppp->vj != 0)
			slhc_free(ppp->vj);
		ppp->vj = vj;
		ppp_unlock(ppp);
		err = 0;
		break;

	case PPPIOCGNPMODE:
	case PPPIOCSNPMODE:
		if (copy_from_user(&npi, (void *) arg, sizeof(npi)))
			break;
		err = proto_to_npindex(npi.protocol);
		if (err < 0)
			break;
		i = err;
		if (cmd == PPPIOCGNPMODE) {
			err = -EFAULT;
			npi.mode = ppp->npmode[i];
			if (copy_to_user((void *) arg, &npi, sizeof(npi)))
				break;
		} else {
			ppp->npmode[i] = npi.mode;
			/* we may be able to transmit more packets now (??) */
			netif_wake_queue(ppp->dev);
		}
		err = 0;
		break;

#ifdef CONFIG_PPP_MULTILINK
	case PPPIOCSMRRU:
		if (get_user(val, (int *) arg))
			break;
		ppp_recv_lock(ppp);
		ppp->mrru = val;
		ppp_recv_unlock(ppp);
		err = 0;
		break;
#endif /* CONFIG_PPP_MULTILINK */

	default:
		err = -ENOTTY;
	}
	return err;
}

static int ppp_unattached_ioctl(struct ppp_file *pf, struct file *file,
				unsigned int cmd, unsigned long arg)
{
	int unit, err = -EFAULT;
	struct ppp *ppp;
	struct channel *chan;

	switch (cmd) {
	case PPPIOCNEWUNIT:
		/* Create a new ppp unit */
		if (get_user(unit, (int *) arg))
			break;
		ppp = ppp_create_interface(unit, &err);
		if (ppp == 0)
			break;
		file->private_data = &ppp->file;
		err = -EFAULT;
		if (put_user(ppp->file.index, (int *) arg))
			break;
		err = 0;
		break;

	case PPPIOCATTACH:
		/* Attach to an existing ppp unit */
		if (get_user(unit, (int *) arg))
			break;
		spin_lock(&all_ppp_lock);
		ppp = ppp_find_unit(unit);
		if (ppp != 0)
			atomic_inc(&ppp->file.refcnt);
		spin_unlock(&all_ppp_lock);
		err = -ENXIO;
		if (ppp == 0)
			break;
		file->private_data = &ppp->file;
		err = 0;
		break;

	case PPPIOCATTCHAN:
		if (get_user(unit, (int *) arg))
			break;
		spin_lock_bh(&all_channels_lock);
		chan = ppp_find_channel(unit);
		if (chan != 0)
			atomic_inc(&chan->file.refcnt);
		spin_unlock_bh(&all_channels_lock);
		err = -ENXIO;
		if (chan == 0)
			break;
		file->private_data = &chan->file;
		err = 0;
		break;

	default:
		err = -ENOTTY;
	}
	return err;
}

static struct file_operations ppp_device_fops = {
	owner:		THIS_MODULE,
	read:		ppp_read,
	write:		ppp_write,
	poll:		ppp_poll,
	ioctl:		ppp_ioctl,
	open:		ppp_open,
	release:	ppp_release
};

#define PPP_MAJOR	108

static devfs_handle_t devfs_handle;

/* Called at boot time if ppp is compiled into the kernel,
   or at module load time (from init_module) if compiled as a module. */
int __init ppp_init(void)
{
	int err;

	printk(KERN_INFO "PPP generic driver version " PPP_VERSION "\n");
	err = devfs_register_chrdev(PPP_MAJOR, "ppp", &ppp_device_fops);
	if (err)
		printk(KERN_ERR "failed to register PPP device (%d)\n", err);
	devfs_handle = devfs_register(NULL, "ppp", DEVFS_FL_DEFAULT,
				      PPP_MAJOR, 0,
				      S_IFCHR | S_IRUSR | S_IWUSR,
				      &ppp_device_fops, NULL);

	return 0;
}

/*
 * Network interface unit routines.
 */
static int
ppp_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ppp *ppp = (struct ppp *) dev->priv;
	int npi, proto;
	unsigned char *pp;

	npi = ethertype_to_npindex(ntohs(skb->protocol));
	if (npi < 0)
		goto outf;

	/* Drop, accept or reject the packet */
	switch (ppp->npmode[npi]) {
	case NPMODE_PASS:
		break;
	case NPMODE_QUEUE:
		/* it would be nice to have a way to tell the network
		   system to queue this one up for later. */
		goto outf;
	case NPMODE_DROP:
	case NPMODE_ERROR:
		goto outf;
	}

	/* Put the 2-byte PPP protocol number on the front,
	   making sure there is room for the address and control fields. */
	if (skb_headroom(skb) < PPP_HDRLEN) {
		struct sk_buff *ns;

		ns = alloc_skb(skb->len + dev->hard_header_len, GFP_ATOMIC);
		if (ns == 0)
			goto outf;
		skb_reserve(ns, dev->hard_header_len);
		memcpy(skb_put(ns, skb->len), skb->data, skb->len);
		kfree_skb(skb);
		skb = ns;
	}
	pp = skb_push(skb, 2);
	proto = npindex_to_proto[npi];
	pp[0] = proto >> 8;
	pp[1] = proto;

	netif_stop_queue(dev);
	skb_queue_tail(&ppp->file.xq, skb);
	ppp_xmit_process(ppp);
	return 0;

 outf:
	kfree_skb(skb);
	++ppp->stats.tx_dropped;
	return 0;
}

static struct net_device_stats *
ppp_net_stats(struct net_device *dev)
{
	struct ppp *ppp = (struct ppp *) dev->priv;

	return &ppp->stats;
}

static int
ppp_net_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct ppp *ppp = dev->priv;
	int err = -EFAULT;
	void *addr = (void *) ifr->ifr_ifru.ifru_data;
	struct ppp_stats stats;
	struct ppp_comp_stats cstats;
	char *vers;

	switch (cmd) {
	case SIOCGPPPSTATS:
		ppp_get_stats(ppp, &stats);
		if (copy_to_user(addr, &stats, sizeof(stats)))
			break;
		err = 0;
		break;

	case SIOCGPPPCSTATS:
		memset(&cstats, 0, sizeof(cstats));
		if (ppp->xc_state != 0)
			ppp->xcomp->comp_stat(ppp->xc_state, &cstats.c);
		if (ppp->rc_state != 0)
			ppp->rcomp->decomp_stat(ppp->rc_state, &cstats.d);
		if (copy_to_user(addr, &cstats, sizeof(cstats)))
			break;
		err = 0;
		break;

	case SIOCGPPPVER:
		vers = PPP_VERSION;
		if (copy_to_user(addr, vers, strlen(vers) + 1))
			break;
		err = 0;
		break;

	default:
		err = -EINVAL;
	}

	return err;
}

int
ppp_net_init(struct net_device *dev)
{
	dev->hard_header_len = PPP_HDRLEN;
	dev->mtu = PPP_MTU;
	dev->hard_start_xmit = ppp_start_xmit;
	dev->get_stats = ppp_net_stats;
	dev->do_ioctl = ppp_net_ioctl;
	dev->addr_len = 0;
	dev->tx_queue_len = 3;
	dev->type = ARPHRD_PPP;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;

	dev_init_buffers(dev);
	return 0;
}

/*
 * Transmit-side routines.
 */

/*
 * Called to do any work queued up on the transmit side
 * that can now be done.
 */
static void
ppp_xmit_process(struct ppp *ppp)
{
	struct sk_buff *skb;

	ppp_xmit_lock(ppp);
	ppp_push(ppp);
	while (ppp->xmit_pending == 0
	       && (skb = skb_dequeue(&ppp->file.xq)) != 0)
		ppp_send_frame(ppp, skb);
	/* If there's no work left to do, tell the core net
	   code that we can accept some more. */
	if (ppp->xmit_pending == 0 && skb_peek(&ppp->file.xq) == 0
	    && ppp->dev != 0)
		netif_wake_queue(ppp->dev);
	ppp_xmit_unlock(ppp);
}

/*
 * Compress and send a frame.
 * The caller should have locked the xmit path,
 * and xmit_pending should be 0.
 */
static void
ppp_send_frame(struct ppp *ppp, struct sk_buff *skb)
{
	int proto = PPP_PROTO(skb);
	struct sk_buff *new_skb;
	int len;
	unsigned char *cp;

	++ppp->stats.tx_packets;
	ppp->stats.tx_bytes += skb->len - 2;

	switch (proto) {
	case PPP_IP:
		if (ppp->vj == 0 || (ppp->flags & SC_COMP_TCP) == 0)
			break;
		/* try to do VJ TCP header compression */
		new_skb = alloc_skb(skb->len + ppp->dev->hard_header_len - 2,
				    GFP_ATOMIC);
		if (new_skb == 0) {
			printk(KERN_ERR "PPP: no memory (VJ comp pkt)\n");
			goto drop;
		}
		skb_reserve(new_skb, ppp->dev->hard_header_len - 2);
		cp = skb->data + 2;
		len = slhc_compress(ppp->vj, cp, skb->len - 2,
				    new_skb->data + 2, &cp,
				    !(ppp->flags & SC_NO_TCP_CCID));
		if (cp == skb->data + 2) {
			/* didn't compress */
			kfree_skb(new_skb);
		} else {
			if (cp[0] & SL_TYPE_COMPRESSED_TCP) {
				proto = PPP_VJC_COMP;
				cp[0] &= ~SL_TYPE_COMPRESSED_TCP;
			} else {
				proto = PPP_VJC_UNCOMP;
				cp[0] = skb->data[2];
			}
			kfree_skb(skb);
			skb = new_skb;
			cp = skb_put(skb, len + 2);
			cp[0] = 0;
			cp[1] = proto;
		}
		break;

	case PPP_CCP:
		/* peek at outbound CCP frames */
		ppp_ccp_peek(ppp, skb, 0);
		break;
	}

	/* try to do packet compression */
	if ((ppp->xstate & SC_COMP_RUN) && ppp->xc_state != 0
	    && proto != PPP_LCP && proto != PPP_CCP) {
		new_skb = alloc_skb(ppp->dev->mtu + ppp->dev->hard_header_len,
				    GFP_ATOMIC);
		if (new_skb == 0) {
			printk(KERN_ERR "PPP: no memory (comp pkt)\n");
			goto drop;
		}
		if (ppp->dev->hard_header_len > PPP_HDRLEN)
			skb_reserve(new_skb,
				    ppp->dev->hard_header_len - PPP_HDRLEN);

		/* compressor still expects A/C bytes in hdr */
		len = ppp->xcomp->compress(ppp->xc_state, skb->data - 2,
					   new_skb->data, skb->len + 2,
					   ppp->dev->mtu + PPP_HDRLEN);
		if (len > 0 && (ppp->flags & SC_CCP_UP)) {
			kfree_skb(skb);
			skb = new_skb;
			skb_put(skb, len);
			skb_pull(skb, 2);	/* pull off A/C bytes */
		} else {
			/* didn't compress, or CCP not up yet */
			kfree_skb(new_skb);
		}
	}

	/* for data packets, record the time */
	if (proto < 0x8000)
		ppp->last_xmit = jiffies;

	/*
	 * If we are waiting for traffic (demand dialling),
	 * queue it up for pppd to receive.
	 */
	if (ppp->flags & SC_LOOP_TRAFFIC) {
		if (ppp->file.rq.qlen > PPP_MAX_RQLEN)
			goto drop;
		skb_queue_tail(&ppp->file.rq, skb);
		wake_up_interruptible(&ppp->file.rwait);
		return;
	}

	ppp->xmit_pending = skb;
	ppp_push(ppp);
	return;

 drop:
	kfree_skb(skb);
	++ppp->stats.tx_errors;
}

/*
 * Try to send the frame in xmit_pending.
 * The caller should have the xmit path locked.
 */
static void
ppp_push(struct ppp *ppp)
{
	struct list_head *list;
	struct channel *pch;
	struct sk_buff *skb = ppp->xmit_pending;

	if (skb == 0)
		return;

	list = &ppp->channels;
	if (list_empty(list)) {
		/* nowhere to send the packet, just drop it */
		ppp->xmit_pending = 0;
		kfree_skb(skb);
		return;
	}

	if ((ppp->flags & SC_MULTILINK) == 0) {
		/* not doing multilink: send it down the first channel */
		list = list->next;
		pch = list_entry(list, struct channel, clist);

		spin_lock_bh(&pch->downl);
		if (pch->chan) {
			if (pch->chan->ops->start_xmit(pch->chan, skb))
				ppp->xmit_pending = 0;
		} else {
			/* channel got unregistered */
			kfree_skb(skb);
			ppp->xmit_pending = 0;
		}
		spin_unlock_bh(&pch->downl);
		return;
	}

#ifdef CONFIG_PPP_MULTILINK
	/* Multilink: fragment the packet over as many links
	   as can take the packet at the moment. */
	if (!ppp_mp_explode(ppp, skb))
		return;
#endif /* CONFIG_PPP_MULTILINK */

	ppp->xmit_pending = 0;
	kfree_skb(skb);
}

#ifdef CONFIG_PPP_MULTILINK
/*
 * Divide a packet to be transmitted into fragments and
 * send them out the individual links.
 */
static int ppp_mp_explode(struct ppp *ppp, struct sk_buff *skb)
{
	int nch, len, fragsize;
	int i, bits, hdrlen, mtu;
	int flen, fnb;
	unsigned char *p, *q;
	struct list_head *list;
	struct channel *pch;
	struct sk_buff *frag;
	struct ppp_channel *chan;

	nch = 0;
	hdrlen = (ppp->flags & SC_MP_XSHORTSEQ)? MPHDRLEN_SSN: MPHDRLEN;
	list = &ppp->channels;
	while ((list = list->next) != &ppp->channels) {
		pch = list_entry(list, struct channel, clist);
		nch += pch->avail = (skb_queue_len(&pch->file.xq) == 0);
		/*
		 * If a channel hasn't had a fragment yet, it has to get
		 * one before we send any fragments on later channels.
		 * If it can't take a fragment now, don't give any
		 * to subsequent channels.
		 */
		if (!pch->had_frag && !pch->avail) {
			while ((list = list->next) != &ppp->channels) {
				pch = list_entry(list, struct channel, clist);
				pch->avail = 0;
			}
			break;
		}
	}
	if (nch == 0)
		return 0;	/* can't take now, leave it in xmit_pending */

	/* Do protocol field compression (XXX this should be optional) */
	p = skb->data;
	len = skb->len;
	if (*p == 0) {
		++p;
		--len;
	}

	/* decide on fragment size */
	fragsize = len;
	if (nch > 1) {
		int maxch = ROUNDUP(len, MIN_FRAG_SIZE);
		if (nch > maxch)
			nch = maxch;
		fragsize = ROUNDUP(fragsize, nch);
	}

	/* skip to the channel after the one we last used
	   and start at that one */
	for (i = 0; i < ppp->nxchan; ++i) {
		list = list->next;
		if (list == &ppp->channels) {
			i = 0;
			break;
		}
	}

	/* create a fragment for each channel */
	bits = B;
	do {
		list = list->next;
		if (list == &ppp->channels) {
			i = 0;
			continue;
		}
		pch = list_entry(list, struct channel, clist);
		++i;
		if (!pch->avail)
			continue;

		/* check the channel's mtu and whether it is still attached. */
		spin_lock_bh(&pch->downl);
		if (pch->chan == 0 || (mtu = pch->chan->mtu) < hdrlen) {
			/* can't use this channel */
			spin_unlock_bh(&pch->downl);
			pch->avail = 0;
			if (--nch == 0)
				break;
			continue;
		}

		/*
		 * We have to create multiple fragments for this channel
		 * if fragsize is greater than the channel's mtu.
		 */
		if (fragsize > len)
			fragsize = len;
		for (flen = fragsize; flen > 0; flen -= fnb) {
			fnb = flen;
			if (fnb > mtu + 2 - hdrlen)
				fnb = mtu + 2 - hdrlen;
			if (fnb >= len)
				bits |= E;
			frag = alloc_skb(fnb + hdrlen, GFP_ATOMIC);
			if (frag == 0)
				goto noskb;
			q = skb_put(frag, fnb + hdrlen);
			/* make the MP header */
			q[0] = PPP_MP >> 8;
			q[1] = PPP_MP;
			if (ppp->flags & SC_MP_XSHORTSEQ) {
				q[2] = bits + ((ppp->nxseq >> 8) & 0xf);
				q[3] = ppp->nxseq;
			} else {
				q[2] = bits;
				q[3] = ppp->nxseq >> 16;
				q[4] = ppp->nxseq >> 8;
				q[5] = ppp->nxseq;
			}

			/* copy the data in */
			memcpy(q + hdrlen, p, fnb);

			/* try to send it down the channel */
			chan = pch->chan;
			if (!chan->ops->start_xmit(chan, frag))
				skb_queue_tail(&pch->file.xq, frag);
			pch->had_frag = 1;
			p += fnb;
			len -= fnb;
			++ppp->nxseq;
			bits = 0;
		}
		spin_unlock_bh(&pch->downl);
	} while (len > 0);
	ppp->nxchan = i;

	return 1;

 noskb:
	spin_unlock_bh(&pch->downl);
	if (ppp->debug & 1)
		printk(KERN_ERR "PPP: no memory (fragment)\n");
	++ppp->stats.tx_errors;
	++ppp->nxseq;
	return 1;	/* abandon the frame */
}
#endif /* CONFIG_PPP_MULTILINK */

/*
 * Try to send data out on a channel.
 */
static void
ppp_channel_push(struct channel *pch)
{
	struct sk_buff *skb;
	struct ppp *ppp;

	spin_lock_bh(&pch->downl);
	if (pch->chan != 0) {
		while (skb_queue_len(&pch->file.xq) > 0) {
			skb = skb_dequeue(&pch->file.xq);
			if (!pch->chan->ops->start_xmit(pch->chan, skb)) {
				/* put the packet back and try again later */
				skb_queue_head(&pch->file.xq, skb);
				break;
			}
		}
	} else {
		/* channel got deregistered */
		skb_queue_purge(&pch->file.xq);
	}
	spin_unlock_bh(&pch->downl);
	/* see if there is anything from the attached unit to be sent */
	if (skb_queue_len(&pch->file.xq) == 0) {
		read_lock_bh(&pch->upl);
		ppp = pch->ppp;
		if (ppp != 0)
			ppp_xmit_process(ppp);
		read_unlock_bh(&pch->upl);
	}
}

/*
 * Receive-side routines.
 */

/* misuse a few fields of the skb for MP reconstruction */
#define sequence	priority
#define BEbits		cb[0]

static inline void
ppp_do_recv(struct ppp *ppp, struct sk_buff *skb, struct channel *pch)
{
	ppp_recv_lock(ppp);
	/* ppp->dev == 0 means interface is closing down */
	if (ppp->dev != 0)
		ppp_receive_frame(ppp, skb, pch);
	else
		kfree_skb(skb);
	ppp_recv_unlock(ppp);
}

void
ppp_input(struct ppp_channel *chan, struct sk_buff *skb)
{
	struct channel *pch = chan->ppp;
	int proto;

	if (pch == 0 || skb->len == 0) {
		kfree_skb(skb);
		return;
	}

	proto = PPP_PROTO(skb);
	read_lock_bh(&pch->upl);
	if (pch->ppp == 0 || proto >= 0xc000 || proto == PPP_CCPFRAG) {
		/* put it on the channel queue */
		skb_queue_tail(&pch->file.rq, skb);
		/* drop old frames if queue too long */
		while (pch->file.rq.qlen > PPP_MAX_RQLEN
		       && (skb = skb_dequeue(&pch->file.rq)) != 0)
			kfree_skb(skb);
		wake_up_interruptible(&pch->file.rwait);
	} else {
		ppp_do_recv(pch->ppp, skb, pch);
	}
	read_unlock_bh(&pch->upl);
}

/* Put a 0-length skb in the receive queue as an error indication */
void
ppp_input_error(struct ppp_channel *chan, int code)
{
	struct channel *pch = chan->ppp;
	struct sk_buff *skb;

	if (pch == 0)
		return;

	read_lock_bh(&pch->upl);
	if (pch->ppp != 0) {
		skb = alloc_skb(0, GFP_ATOMIC);
		if (skb != 0) {
			skb->len = 0;		/* probably unnecessary */
			skb->cb[0] = code;
			ppp_do_recv(pch->ppp, skb, pch);
		}
	}
	read_unlock_bh(&pch->upl);
}

/*
 * We come in here to process a received frame.
 * The receive side of the ppp unit is locked.
 */
static void
ppp_receive_frame(struct ppp *ppp, struct sk_buff *skb, struct channel *pch)
{
	if (skb->len >= 2) {
#ifdef CONFIG_PPP_MULTILINK
		/* XXX do channel-level decompression here */
		if (PPP_PROTO(skb) == PPP_MP)
			ppp_receive_mp_frame(ppp, skb, pch);
		else
#endif /* CONFIG_PPP_MULTILINK */
			ppp_receive_nonmp_frame(ppp, skb);
		return;
	}

	if (skb->len > 0)
		/* note: a 0-length skb is used as an error indication */
		++ppp->stats.rx_length_errors;

	kfree_skb(skb);
	ppp_receive_error(ppp);
}

static void
ppp_receive_error(struct ppp *ppp)
{
	++ppp->stats.rx_errors;
	if (ppp->vj != 0)
		slhc_toss(ppp->vj);
}

static void
ppp_receive_nonmp_frame(struct ppp *ppp, struct sk_buff *skb)
{
	struct sk_buff *ns;
	int proto, len, npi;

	/*
	 * Decompress the frame, if compressed.
	 * Note that some decompressors need to see uncompressed frames
	 * that come in as well as compressed frames.
	 */
	if (ppp->rc_state != 0 && (ppp->rstate & SC_DECOMP_RUN)
	    && (ppp->rstate & (SC_DC_FERROR | SC_DC_ERROR)) == 0)
		skb = ppp_decompress_frame(ppp, skb);

	proto = PPP_PROTO(skb);
	switch (proto) {
	case PPP_VJC_COMP:
		/* decompress VJ compressed packets */
		if (ppp->vj == 0 || (ppp->flags & SC_REJ_COMP_TCP))
			goto err;
		if (skb_tailroom(skb) < 124) {
			/* copy to a new sk_buff with more tailroom */
			ns = dev_alloc_skb(skb->len + 128);
			if (ns == 0) {
				printk(KERN_ERR"PPP: no memory (VJ decomp)\n");
				goto err;
			}
			skb_reserve(ns, 2);
			memcpy(skb_put(ns, skb->len), skb->data, skb->len);
			kfree_skb(skb);
			skb = ns;
		}
		len = slhc_uncompress(ppp->vj, skb->data + 2, skb->len - 2);
		if (len <= 0) {
			printk(KERN_DEBUG "PPP: VJ decompression error\n");
			goto err;
		}
		len += 2;
		if (len > skb->len)
			skb_put(skb, len - skb->len);
		else if (len < skb->len)
			skb_trim(skb, len);
		proto = PPP_IP;
		break;

	case PPP_VJC_UNCOMP:
		if (ppp->vj == 0 || (ppp->flags & SC_REJ_COMP_TCP))
			goto err;
		if (slhc_remember(ppp->vj, skb->data + 2, skb->len - 2) <= 0) {
			printk(KERN_ERR "PPP: VJ uncompressed error\n");
			goto err;
		}
		proto = PPP_IP;
		break;

	case PPP_CCP:
		ppp_ccp_peek(ppp, skb, 1);
		break;
	}

	++ppp->stats.rx_packets;
	ppp->stats.rx_bytes += skb->len - 2;

	npi = proto_to_npindex(proto);
	if (npi < 0) {
		/* control or unknown frame - pass it to pppd */
		skb_queue_tail(&ppp->file.rq, skb);
		/* limit queue length by dropping old frames */
		while (ppp->file.rq.qlen > PPP_MAX_RQLEN
		       && (skb = skb_dequeue(&ppp->file.rq)) != 0)
			kfree_skb(skb);
		/* wake up any process polling or blocking on read */
		wake_up_interruptible(&ppp->file.rwait);

	} else {
		/* network protocol frame - give it to the kernel */
		ppp->last_recv = jiffies;
		if ((ppp->dev->flags & IFF_UP) == 0
		    || ppp->npmode[npi] != NPMODE_PASS) {
			kfree_skb(skb);
		} else {
			skb_pull(skb, 2);	/* chop off protocol */
			skb->dev = ppp->dev;
			skb->protocol = htons(npindex_to_ethertype[npi]);
			skb->mac.raw = skb->data;
			netif_rx(skb);
		}
	}
	return;

 err:
	kfree_skb(skb);
	ppp_receive_error(ppp);
}

static struct sk_buff *
ppp_decompress_frame(struct ppp *ppp, struct sk_buff *skb)
{
	int proto = PPP_PROTO(skb);
	struct sk_buff *ns;
	int len;

	if (proto == PPP_COMP) {
		ns = dev_alloc_skb(ppp->mru + PPP_HDRLEN);
		if (ns == 0) {
			printk(KERN_ERR "ppp_decompress_frame: no memory\n");
			goto err;
		}
		/* the decompressor still expects the A/C bytes in the hdr */
		len = ppp->rcomp->decompress(ppp->rc_state, skb->data - 2,
				skb->len + 2, ns->data, ppp->mru + PPP_HDRLEN);
		if (len < 0) {
			/* Pass the compressed frame to pppd as an
			   error indication. */
			if (len == DECOMP_FATALERROR)
				ppp->rstate |= SC_DC_FERROR;
			goto err;
		}

		kfree_skb(skb);
		skb = ns;
		skb_put(skb, len);
		skb_pull(skb, 2);	/* pull off the A/C bytes */

	} else {
		/* Uncompressed frame - pass to decompressor so it
		   can update its dictionary if necessary. */
		if (ppp->rcomp->incomp)
			ppp->rcomp->incomp(ppp->rc_state, skb->data - 2,
					   skb->len + 2);
	}

	return skb;

 err:
	ppp->rstate |= SC_DC_ERROR;
	ppp_receive_error(ppp);
	return skb;
}

#ifdef CONFIG_PPP_MULTILINK
/*
 * Receive a multilink frame.
 * We put it on the reconstruction queue and then pull off
 * as many completed frames as we can.
 */
static void
ppp_receive_mp_frame(struct ppp *ppp, struct sk_buff *skb, struct channel *pch)
{
	u32 mask, seq;
	struct list_head *l;
	int mphdrlen = (ppp->flags & SC_MP_SHORTSEQ)? MPHDRLEN_SSN: MPHDRLEN;

	if (skb->len < mphdrlen + 1 || ppp->mrru == 0)
		goto err;		/* no good, throw it away */

	/* Decode sequence number and begin/end bits */
	if (ppp->flags & SC_MP_SHORTSEQ) {
		seq = ((skb->data[2] & 0x0f) << 8) | skb->data[3];
		mask = 0xfff;
	} else {
		seq = (skb->data[3] << 16) | (skb->data[4] << 8)| skb->data[5];
		mask = 0xffffff;
	}
	skb->BEbits = skb->data[2];
	skb_pull(skb, mphdrlen);	/* pull off PPP and MP headers */

	/*
	 * Do protocol ID decompression on the first fragment of each packet.
	 */
	if ((skb->BEbits & B) && (skb->data[0] & 1))
		*skb_push(skb, 1) = 0;

	/*
	 * Expand sequence number to 32 bits, making it as close
	 * as possible to ppp->minseq.
	 */
	seq |= ppp->minseq & ~mask;
	if ((int)(ppp->minseq - seq) > (int)(mask >> 1))
		seq += mask + 1;
	else if ((int)(seq - ppp->minseq) > (int)(mask >> 1))
		seq -= mask + 1;	/* should never happen */
	skb->sequence = seq;
	pch->lastseq = seq;

	/*
	 * If this packet comes before the next one we were expecting,
	 * drop it.
	 */
	if (seq_before(seq, ppp->nextseq)) {
		kfree_skb(skb);
		++ppp->stats.rx_dropped;
		ppp_receive_error(ppp);
		return;
	}

	/*
	 * Reevaluate minseq, the minimum over all channels of the
	 * last sequence number received on each channel.  Because of
	 * the increasing sequence number rule, we know that any fragment
	 * before `minseq' which hasn't arrived is never going to arrive.
	 * The list of channels can't change because we have the receive
	 * side of the ppp unit locked.
	 */
	for (l = ppp->channels.next; l != &ppp->channels; l = l->next) {
		struct channel *ch = list_entry(l, struct channel, clist);
		if (seq_before(ch->lastseq, seq))
			seq = ch->lastseq;
	}
	if (seq_before(ppp->minseq, seq))
		ppp->minseq = seq;

	/* Put the fragment on the reconstruction queue */
	ppp_mp_insert(ppp, skb);

	/* If the queue is getting long, don't wait any longer for packets
	   before the start of the queue. */
	if (skb_queue_len(&ppp->mrq) >= PPP_MP_MAX_QLEN
	    && seq_before(ppp->minseq, ppp->mrq.next->sequence))
		ppp->minseq = ppp->mrq.next->sequence;

	/* Pull completed packets off the queue and receive them. */
	while ((skb = ppp_mp_reconstruct(ppp)) != 0)
		ppp_receive_nonmp_frame(ppp, skb);

	return;

 err:
	kfree_skb(skb);
	ppp_receive_error(ppp);
}

/*
 * Insert a fragment on the MP reconstruction queue.
 * The queue is ordered by increasing sequence number.
 */
static void
ppp_mp_insert(struct ppp *ppp, struct sk_buff *skb)
{
	struct sk_buff *p;
	struct sk_buff_head *list = &ppp->mrq;
	u32 seq = skb->sequence;

	/* N.B. we don't need to lock the list lock because we have the
	   ppp unit receive-side lock. */
	for (p = list->next; p != (struct sk_buff *)list; p = p->next)
		if (seq_before(seq, p->sequence))
			break;
	__skb_insert(skb, p->prev, p, list);
}

/*
 * Reconstruct a packet from the MP fragment queue.
 * We go through increasing sequence numbers until we find a
 * complete packet, or we get to the sequence number for a fragment
 * which hasn't arrived but might still do so.
 */
struct sk_buff *
ppp_mp_reconstruct(struct ppp *ppp)
{
	u32 seq = ppp->nextseq;
	u32 minseq = ppp->minseq;
	struct sk_buff_head *list = &ppp->mrq;
	struct sk_buff *p, *next;
	struct sk_buff *head, *tail;
	struct sk_buff *skb = NULL;
	int lost = 0, len = 0;

	if (ppp->mrru == 0)	/* do nothing until mrru is set */
		return NULL;
	head = list->next;
	tail = NULL;
	for (p = head; p != (struct sk_buff *) list; p = next) {
		next = p->next;
		if (seq_before(p->sequence, seq)) {
			/* this can't happen, anyway ignore the skb */
			printk(KERN_ERR "ppp_mp_reconstruct bad seq %u < %u\n",
			       p->sequence, seq);
			head = next;
			continue;
		}
		if (p->sequence != seq) {
			/* Fragment `seq' is missing.  If it is after
			   minseq, it might arrive later, so stop here. */
			if (seq_after(seq, minseq))
				break;
			/* Fragment `seq' is lost, keep going. */
			lost = 1;
			seq = seq_before(minseq, p->sequence)?
				minseq + 1: p->sequence;
			next = p;
			continue;
		}

		/*
		 * At this point we know that all the fragments from
		 * ppp->nextseq to seq are either present or lost.
		 * Also, there are no complete packets in the queue
		 * that have no missing fragments and end before this
		 * fragment.
		 */

		/* B bit set indicates this fragment starts a packet */
		if (p->BEbits & B) {
			head = p;
			lost = 0;
			len = 0;
		}

		len += p->len;

		/* Got a complete packet yet? */
		if (lost == 0 && (p->BEbits & E) && (head->BEbits & B)) {
			if (len > ppp->mrru + 2) {
				++ppp->stats.rx_length_errors;
				printk(KERN_DEBUG "PPP: reconstructed packet"
				       " is too long (%d)\n", len);
			} else if (p == head) {
				/* fragment is complete packet - reuse skb */
				tail = p;
				skb = skb_get(p);
				break;
			} else if ((skb = dev_alloc_skb(len)) == NULL) {
				++ppp->stats.rx_missed_errors;
				printk(KERN_DEBUG "PPP: no memory for "
				       "reconstructed packet");
			} else {
				tail = p;
				break;
			}
			ppp->nextseq = seq + 1;
		}

		/*
		 * If this is the ending fragment of a packet,
		 * and we haven't found a complete valid packet yet,
		 * we can discard up to and including this fragment.
		 */
		if (p->BEbits & E)
			head = next;

		++seq;
	}

	/* If we have a complete packet, copy it all into one skb. */
	if (tail != NULL) {
		/* If we have discarded any fragments,
		   signal a receive error. */
		if (head->sequence != ppp->nextseq) {
			if (ppp->debug & 1)
				printk(KERN_DEBUG "  missed pkts %u..%u\n",
				       ppp->nextseq, head->sequence-1);
			++ppp->stats.rx_dropped;
			ppp_receive_error(ppp);
		}

		if (head != tail)
			/* copy to a single skb */
			for (p = head; p != tail->next; p = p->next)
				memcpy(skb_put(skb, p->len), p->data, p->len);
		ppp->nextseq = tail->sequence + 1;
		head = tail->next;
	}

	/* Discard all the skbuffs that we have copied the data out of
	   or that we can't use. */
	while ((p = list->next) != head) {
		__skb_unlink(p, list);
		kfree_skb(p);
	}

	return skb;
}
#endif /* CONFIG_PPP_MULTILINK */

/*
 * Channel interface.
 */

/*
 * Create a new, unattached ppp channel.
 */
int
ppp_register_channel(struct ppp_channel *chan)
{
	struct channel *pch;

	pch = kmalloc(sizeof(struct channel), GFP_ATOMIC);
	if (pch == 0)
		return -ENOMEM;
	memset(pch, 0, sizeof(struct channel));
	pch->ppp = NULL;
	pch->chan = chan;
	chan->ppp = pch;
	init_ppp_file(&pch->file, CHANNEL);
	pch->file.hdrlen = chan->hdrlen;
#ifdef CONFIG_PPP_MULTILINK
	pch->lastseq = -1;
#endif /* CONFIG_PPP_MULTILINK */
	spin_lock_init(&pch->downl);
	pch->upl = RW_LOCK_UNLOCKED;
	spin_lock_bh(&all_channels_lock);
	pch->file.index = ++last_channel_index;
	list_add(&pch->file.list, &all_channels);
	spin_unlock_bh(&all_channels_lock);
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * Return the index of a channel.
 */
int ppp_channel_index(struct ppp_channel *chan)
{
	struct channel *pch = chan->ppp;

	return pch->file.index;
}

/*
 * Return the PPP unit number to which a channel is connected.
 */
int ppp_unit_number(struct ppp_channel *chan)
{
	struct channel *pch = chan->ppp;
	int unit = -1;

	if (pch != 0) {
		read_lock_bh(&pch->upl);
		if (pch->ppp != 0)
			unit = pch->ppp->file.index;
		read_unlock_bh(&pch->upl);
	}
	return unit;
}

/*
 * Disconnect a channel from the generic layer.
 * This can be called from mainline or BH/softirq level.
 */
void
ppp_unregister_channel(struct ppp_channel *chan)
{
	struct channel *pch = chan->ppp;

	if (pch == 0)
		return;		/* should never happen */
	chan->ppp = 0;

	/*
	 * This ensures that we have returned from any calls into the
	 * the channel's start_xmit or ioctl routine before we proceed.
	 */
	spin_lock_bh(&pch->downl);
	pch->chan = 0;
	spin_unlock_bh(&pch->downl);
	ppp_disconnect_channel(pch);
	wake_up_interruptible(&pch->file.rwait);
	spin_lock_bh(&all_channels_lock);
	list_del(&pch->file.list);
	spin_unlock_bh(&all_channels_lock);
	if (atomic_dec_and_test(&pch->file.refcnt))
		ppp_destroy_channel(pch);
	MOD_DEC_USE_COUNT;
}

/*
 * Callback from a channel when it can accept more to transmit.
 * This should be called at BH/softirq level, not interrupt level.
 */
void
ppp_output_wakeup(struct ppp_channel *chan)
{
	struct channel *pch = chan->ppp;

	if (pch == 0)
		return;
	ppp_channel_push(pch);
}

/*
 * This is basically temporary compatibility stuff.
 */
ssize_t
ppp_channel_read(struct ppp_channel *chan, struct file *file,
		 char *buf, size_t count)
{
	struct channel *pch = chan->ppp;

	if (pch == 0)
		return -ENXIO;
	return ppp_file_read(&pch->file, file, buf, count);
}

ssize_t
ppp_channel_write(struct ppp_channel *chan, const char *buf, size_t count)
{
	struct channel *pch = chan->ppp;

	if (pch == 0)
		return -ENXIO;
	return ppp_file_write(&pch->file, buf, count);
}

/* No kernel lock - fine */
unsigned int
ppp_channel_poll(struct ppp_channel *chan, struct file *file, poll_table *wait)
{
	unsigned int mask;
	struct channel *pch = chan->ppp;

	mask = POLLOUT | POLLWRNORM;
	if (pch != 0) {
		poll_wait(file, &pch->file.rwait, wait);
		if (skb_peek(&pch->file.rq) != 0)
			mask |= POLLIN | POLLRDNORM;
	}
	return mask;
}

int ppp_channel_ioctl(struct ppp_channel *chan, unsigned int cmd,
		      unsigned long arg)
{
	struct channel *pch = chan->ppp;
	int err = -ENOTTY;
	int unit;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;
	if (pch == 0)
		return -EINVAL;
	switch (cmd) {
	case PPPIOCATTACH:
		if (get_user(unit, (int *) arg))
			break;
		err = ppp_connect_channel(pch, unit);
		break;
	case PPPIOCDETACH:
		err = ppp_disconnect_channel(pch);
		break;
	}
	return err;
}

/*
 * Compression control.
 */

/* Process the PPPIOCSCOMPRESS ioctl. */
static int
ppp_set_compress(struct ppp *ppp, unsigned long arg)
{
	int err;
	struct compressor *cp;
	struct ppp_option_data data;
	void *state;
	unsigned char ccp_option[CCP_MAX_OPTION_LENGTH];
#ifdef CONFIG_KMOD
	char modname[32];
#endif

	err = -EFAULT;
	if (copy_from_user(&data, (void *) arg, sizeof(data))
	    || (data.length <= CCP_MAX_OPTION_LENGTH
		&& copy_from_user(ccp_option, data.ptr, data.length)))
		goto out;
	err = -EINVAL;
	if (data.length > CCP_MAX_OPTION_LENGTH
	    || ccp_option[1] < 2 || ccp_option[1] > data.length)
		goto out;

	cp = find_compressor(ccp_option[0]);
#ifdef CONFIG_KMOD
	if (cp == 0) {
		sprintf(modname, "ppp-compress-%d", ccp_option[0]);
		request_module(modname);
		cp = find_compressor(ccp_option[0]);
	}
#endif /* CONFIG_KMOD */
	if (cp == 0)
		goto out;

	err = -ENOBUFS;
	if (data.transmit) {
		ppp_xmit_lock(ppp);
		ppp->xstate &= ~SC_COMP_RUN;
		if (ppp->xc_state != 0) {
			ppp->xcomp->comp_free(ppp->xc_state);
			ppp->xc_state = 0;
		}
		ppp_xmit_unlock(ppp);

		state = cp->comp_alloc(ccp_option, data.length);
		if (state != 0) {
			ppp_xmit_lock(ppp);
			ppp->xcomp = cp;
			ppp->xc_state = state;
			ppp_xmit_unlock(ppp);
			err = 0;
		}

	} else {
		ppp_recv_lock(ppp);
		ppp->rstate &= ~SC_DECOMP_RUN;
		if (ppp->rc_state != 0) {
			ppp->rcomp->decomp_free(ppp->rc_state);
			ppp->rc_state = 0;
		}
		ppp_recv_unlock(ppp);

		state = cp->decomp_alloc(ccp_option, data.length);
		if (state != 0) {
			ppp_recv_lock(ppp);
			ppp->rcomp = cp;
			ppp->rc_state = state;
			ppp_recv_unlock(ppp);
			err = 0;
		}
	}

 out:
	return err;
}

/*
 * Look at a CCP packet and update our state accordingly.
 * We assume the caller has the xmit or recv path locked.
 */
static void
ppp_ccp_peek(struct ppp *ppp, struct sk_buff *skb, int inbound)
{
	unsigned char *dp = skb->data + 2;
	int len;

	if (skb->len < CCP_HDRLEN + 2
	    || skb->len < (len = CCP_LENGTH(dp)) + 2)
		return;		/* too short */

	switch (CCP_CODE(dp)) {
	case CCP_CONFREQ:
	case CCP_TERMREQ:
	case CCP_TERMACK:
		/*
		 * CCP is going down - disable compression.
		 */
		if (inbound)
			ppp->rstate &= ~SC_DECOMP_RUN;
		else
			ppp->xstate &= ~SC_COMP_RUN;
		break;

	case CCP_CONFACK:
		if ((ppp->flags & (SC_CCP_OPEN | SC_CCP_UP)) != SC_CCP_OPEN)
			break;
		dp += CCP_HDRLEN;
		len -= CCP_HDRLEN;
		if (len < CCP_OPT_MINLEN || len < CCP_OPT_LENGTH(dp))
			break;
		if (inbound) {
			/* we will start receiving compressed packets */
			if (ppp->rc_state == 0)
				break;
			if (ppp->rcomp->decomp_init(ppp->rc_state, dp, len,
					ppp->file.index, 0, ppp->mru, ppp->debug)) {
				ppp->rstate |= SC_DECOMP_RUN;
				ppp->rstate &= ~(SC_DC_ERROR | SC_DC_FERROR);
			}
		} else {
			/* we will soon start sending compressed packets */
			if (ppp->xc_state == 0)
				break;
			if (ppp->xcomp->comp_init(ppp->xc_state, dp, len,
					ppp->file.index, 0, ppp->debug))
				ppp->xstate |= SC_COMP_RUN;
		}
		break;

	case CCP_RESETACK:
		/* reset the [de]compressor */
		if ((ppp->flags & SC_CCP_UP) == 0)
			break;
		if (inbound) {
			if (ppp->rc_state && (ppp->rstate & SC_DECOMP_RUN)) {
				ppp->rcomp->decomp_reset(ppp->rc_state);
				ppp->rstate &= ~SC_DC_ERROR;
			}
		} else {
			if (ppp->xc_state && (ppp->xstate & SC_COMP_RUN))
				ppp->xcomp->comp_reset(ppp->xc_state);
		}
		break;
	}
}

/* Free up compression resources. */
static void
ppp_ccp_closed(struct ppp *ppp)
{
	ppp->flags &= ~(SC_CCP_OPEN | SC_CCP_UP);

	ppp->xstate &= ~SC_COMP_RUN;
	if (ppp->xc_state) {
		ppp->xcomp->comp_free(ppp->xc_state);
		ppp->xc_state = 0;
	}

	ppp->xstate &= ~SC_DECOMP_RUN;
	if (ppp->rc_state) {
		ppp->rcomp->decomp_free(ppp->rc_state);
		ppp->rc_state = 0;
	}
}

/* List of compressors. */
static LIST_HEAD(compressor_list);
static spinlock_t compressor_list_lock = SPIN_LOCK_UNLOCKED;

struct compressor_entry {
	struct list_head list;
	struct compressor *comp;
};

static struct compressor_entry *
find_comp_entry(int proto)
{
	struct compressor_entry *ce;
	struct list_head *list = &compressor_list;

	while ((list = list->next) != &compressor_list) {
		ce = list_entry(list, struct compressor_entry, list);
		if (ce->comp->compress_proto == proto)
			return ce;
	}
	return 0;
}

/* Register a compressor */
int
ppp_register_compressor(struct compressor *cp)
{
	struct compressor_entry *ce;
	int ret;

	spin_lock(&compressor_list_lock);
	ret = -EEXIST;
	if (find_comp_entry(cp->compress_proto) != 0)
		goto out;
	ret = -ENOMEM;
	ce = kmalloc(sizeof(struct compressor_entry), GFP_KERNEL);
	if (ce == 0)
		goto out;
	ret = 0;
	ce->comp = cp;
	list_add(&ce->list, &compressor_list);
 out:
	spin_unlock(&compressor_list_lock);
	return ret;
}

/* Unregister a compressor */
void
ppp_unregister_compressor(struct compressor *cp)
{
	struct compressor_entry *ce;

	spin_lock(&compressor_list_lock);
	ce = find_comp_entry(cp->compress_proto);
	if (ce != 0 && ce->comp == cp) {
		list_del(&ce->list);
		kfree(ce);
	}
	spin_unlock(&compressor_list_lock);
}

/* Find a compressor. */
static struct compressor *
find_compressor(int type)
{
	struct compressor_entry *ce;
	struct compressor *cp = 0;

	spin_lock(&compressor_list_lock);
	ce = find_comp_entry(type);
	if (ce != 0)
		cp = ce->comp;
	spin_unlock(&compressor_list_lock);
	return cp;
}

/*
 * Miscelleneous stuff.
 */

static void
ppp_get_stats(struct ppp *ppp, struct ppp_stats *st)
{
	struct slcompress *vj = ppp->vj;

	memset(st, 0, sizeof(*st));
	st->p.ppp_ipackets = ppp->stats.rx_packets;
	st->p.ppp_ierrors = ppp->stats.rx_errors;
	st->p.ppp_ibytes = ppp->stats.rx_bytes;
	st->p.ppp_opackets = ppp->stats.tx_packets;
	st->p.ppp_oerrors = ppp->stats.tx_errors;
	st->p.ppp_obytes = ppp->stats.tx_bytes;
	if (vj == 0)
		return;
	st->vj.vjs_packets = vj->sls_o_compressed + vj->sls_o_uncompressed;
	st->vj.vjs_compressed = vj->sls_o_compressed;
	st->vj.vjs_searches = vj->sls_o_searches;
	st->vj.vjs_misses = vj->sls_o_misses;
	st->vj.vjs_errorin = vj->sls_i_error;
	st->vj.vjs_tossed = vj->sls_i_tossed;
	st->vj.vjs_uncompressedin = vj->sls_i_uncompressed;
	st->vj.vjs_compressedin = vj->sls_i_compressed;
}

/*
 * Stuff for handling the lists of ppp units and channels
 * and for initialization.
 */

/*
 * Create a new ppp interface unit.  Fails if it can't allocate memory
 * or if there is already a unit with the requested number.
 * unit == -1 means allocate a new number.
 */
static struct ppp *
ppp_create_interface(int unit, int *retp)
{
	struct ppp *ppp;
	struct net_device *dev;
	struct list_head *list;
	int last_unit = -1;
	int ret = -EEXIST;
	int i;

	spin_lock(&all_ppp_lock);
	list = &all_ppp_units;
	while ((list = list->next) != &all_ppp_units) {
		ppp = list_entry(list, struct ppp, file.list);
		if ((unit < 0 && ppp->file.index > last_unit + 1)
		    || (unit >= 0 && unit < ppp->file.index))
			break;
		if (unit == ppp->file.index)
			goto out;	/* unit already exists */
		last_unit = ppp->file.index;
	}
	if (unit < 0)
		unit = last_unit + 1;

	/* Create a new ppp structure and link it before `list'. */
	ret = -ENOMEM;
	ppp = kmalloc(sizeof(struct ppp), GFP_KERNEL);
	if (ppp == 0)
		goto out;
	memset(ppp, 0, sizeof(struct ppp));
	dev = kmalloc(sizeof(struct net_device), GFP_KERNEL);
	if (dev == 0) {
		kfree(ppp);
		goto out;
	}
	memset(dev, 0, sizeof(struct net_device));

	ppp->file.index = unit;
	ppp->mru = PPP_MRU;
	init_ppp_file(&ppp->file, INTERFACE);
	for (i = 0; i < NUM_NP; ++i)
		ppp->npmode[i] = NPMODE_PASS;
	INIT_LIST_HEAD(&ppp->channels);
	spin_lock_init(&ppp->rlock);
	spin_lock_init(&ppp->wlock);
#ifdef CONFIG_PPP_MULTILINK
	ppp->minseq = -1;
	skb_queue_head_init(&ppp->mrq);
#endif /* CONFIG_PPP_MULTILINK */

	ppp->dev = dev;
	dev->init = ppp_net_init;
	sprintf(dev->name, "ppp%d", unit);
	dev->priv = ppp;
	dev->features |= NETIF_F_DYNALLOC;

	rtnl_lock();
	ret = register_netdevice(dev);
	rtnl_unlock();
	if (ret != 0) {
		printk(KERN_ERR "PPP: couldn't register device (%d)\n", ret);
		kfree(dev);
		kfree(ppp);
		goto out;
	}

	list_add(&ppp->file.list, list->prev);
 out:
	spin_unlock(&all_ppp_lock);
	*retp = ret;
	if (ret != 0)
		ppp = 0;
	return ppp;
}

/*
 * Initialize a ppp_file structure.
 */
static void
init_ppp_file(struct ppp_file *pf, int kind)
{
	pf->kind = kind;
	skb_queue_head_init(&pf->xq);
	skb_queue_head_init(&pf->rq);
	atomic_set(&pf->refcnt, 1);
	init_waitqueue_head(&pf->rwait);
}

/*
 * Free up all the resources used by a ppp interface unit.
 */
static void ppp_destroy_interface(struct ppp *ppp)
{
	struct net_device *dev;

	spin_lock(&all_ppp_lock);
	list_del(&ppp->file.list);

	/* Last fd open to this ppp unit is being closed or detached:
	   mark the interface down, free the ppp unit */
	ppp_lock(ppp);
	ppp_ccp_closed(ppp);
	if (ppp->vj) {
		slhc_free(ppp->vj);
		ppp->vj = 0;
	}
	skb_queue_purge(&ppp->file.xq);
	skb_queue_purge(&ppp->file.rq);
#ifdef CONFIG_PPP_MULTILINK
	skb_queue_purge(&ppp->mrq);
#endif /* CONFIG_PPP_MULTILINK */
	dev = ppp->dev;
	ppp->dev = 0;
	ppp_unlock(ppp);

	if (dev) {
		rtnl_lock();
		dev_close(dev);
		unregister_netdevice(dev);
		rtnl_unlock();
	}

	/*
	 * We can't acquire any new channels (since we have the
	 * all_ppp_lock) so if n_channels is 0, we can free the
	 * ppp structure.  Otherwise we leave it around until the
	 * last channel disconnects from it.
	 */
	if (ppp->n_channels == 0)
		kfree(ppp);

	spin_unlock(&all_ppp_lock);
}

/*
 * Locate an existing ppp unit.
 * The caller should have locked the all_ppp_lock.
 */
static struct ppp *
ppp_find_unit(int unit)
{
	struct ppp *ppp;
	struct list_head *list;

	list = &all_ppp_units;
	while ((list = list->next) != &all_ppp_units) {
		ppp = list_entry(list, struct ppp, file.list);
		if (ppp->file.index == unit)
			return ppp;
	}
	return 0;
}

/*
 * Locate an existing ppp channel.
 * The caller should have locked the all_channels_lock.
 */
static struct channel *
ppp_find_channel(int unit)
{
	struct channel *pch;
	struct list_head *list;

	list = &all_channels;
	while ((list = list->next) != &all_channels) {
		pch = list_entry(list, struct channel, file.list);
		if (pch->file.index == unit)
			return pch;
	}
	return 0;
}

/*
 * Connect a PPP channel to a PPP interface unit.
 */
static int
ppp_connect_channel(struct channel *pch, int unit)
{
	struct ppp *ppp;
	int ret = -ENXIO;
	int hdrlen;

	spin_lock(&all_ppp_lock);
	ppp = ppp_find_unit(unit);
	if (ppp == 0)
		goto out;
	write_lock_bh(&pch->upl);
	ret = -EINVAL;
	if (pch->ppp != 0)
		goto outw;
	ppp_lock(ppp);
	spin_lock_bh(&pch->downl);
	if (pch->chan == 0)		/* need to check this?? */
		goto outr;

	if (pch->file.hdrlen > ppp->file.hdrlen)
		ppp->file.hdrlen = pch->file.hdrlen;
	hdrlen = pch->file.hdrlen + 2;	/* for protocol bytes */
	if (ppp->dev && hdrlen > ppp->dev->hard_header_len)
		ppp->dev->hard_header_len = hdrlen;
	list_add_tail(&pch->clist, &ppp->channels);
	++ppp->n_channels;
	pch->ppp = ppp;
	ret = 0;

 outr:
	spin_unlock_bh(&pch->downl);
	ppp_unlock(ppp);
 outw:
	write_unlock_bh(&pch->upl);
 out:
	spin_unlock(&all_ppp_lock);
	return ret;
}

/*
 * Disconnect a channel from its ppp unit.
 */
static int
ppp_disconnect_channel(struct channel *pch)
{
	struct ppp *ppp;
	int err = -EINVAL;
	int dead;

	write_lock_bh(&pch->upl);
	ppp = pch->ppp;
	if (ppp != 0) {
		/* remove it from the ppp unit's list */
		pch->ppp = NULL;
		ppp_lock(ppp);
		list_del(&pch->clist);
		--ppp->n_channels;
		dead = ppp->dev == 0 && ppp->n_channels == 0;
		ppp_unlock(ppp);
		if (dead)
			/* Last disconnect from a ppp unit
			   that is already dead: free it. */
			kfree(ppp);
		err = 0;
	}
	write_unlock_bh(&pch->upl);
	return err;
}

/*
 * Free up the resources used by a ppp channel.
 */
static void ppp_destroy_channel(struct channel *pch)
{
	skb_queue_purge(&pch->file.xq);
	skb_queue_purge(&pch->file.rq);
	kfree(pch);
}

void __exit ppp_cleanup(void)
{
	/* should never happen */
	if (!list_empty(&all_ppp_units) || !list_empty(&all_channels))
		printk(KERN_ERR "PPP: removing module but units remain!\n");
	if (devfs_unregister_chrdev(PPP_MAJOR, "ppp") != 0)
		printk(KERN_ERR "PPP: failed to unregister PPP device\n");
	devfs_unregister(devfs_handle);
}

module_init(ppp_init);
module_exit(ppp_cleanup);

EXPORT_SYMBOL(ppp_register_channel);
EXPORT_SYMBOL(ppp_unregister_channel);
EXPORT_SYMBOL(ppp_channel_index);
EXPORT_SYMBOL(ppp_unit_number);
EXPORT_SYMBOL(ppp_input);
EXPORT_SYMBOL(ppp_input_error);
EXPORT_SYMBOL(ppp_output_wakeup);
EXPORT_SYMBOL(ppp_register_compressor);
EXPORT_SYMBOL(ppp_unregister_compressor);
EXPORT_SYMBOL(ppp_channel_read);
EXPORT_SYMBOL(ppp_channel_write);
EXPORT_SYMBOL(ppp_channel_poll);
EXPORT_SYMBOL(ppp_channel_ioctl);
EXPORT_SYMBOL(all_ppp_units); /* for debugging */
EXPORT_SYMBOL(all_channels); /* for debugging */
