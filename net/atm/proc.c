/* net/atm/proc.c - ATM /proc interface */

/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */

/*
 * The mechanism used here isn't designed for speed but rather for convenience
 * of implementation. We only return one entry per read system call, so we can
 * be reasonably sure not to overrun the page and race conditions may lead to
 * the addition or omission of some lines but never to any corruption of a
 * line's internal structure.
 *
 * Making the whole thing slightly more efficient is left as an exercise to the
 * reader. (Suggestions: wrapper which loops to get several entries per system
 * call; or make --left slightly more clever to avoid O(n^2) characteristics.)
 * I find it fast enough on my unloaded 266 MHz Pentium 2 :-)
 */


#include <linux/config.h>
#include <linux/module.h> /* for EXPORT_SYMBOL */
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/errno.h>
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/netdevice.h>
#include <linux/atmclip.h>
#include <linux/atmarp.h>
#include <linux/if_arp.h>
#include <linux/init.h> /* for __init */
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <asm/param.h> /* for HZ */
#include "resources.h"
#include "common.h" /* atm_proc_init prototype */
#include "signaling.h" /* to get sigd - ugly too */

#if defined(CONFIG_ATM_CLIP) || defined(CONFIG_ATM_CLIP_MODULE)
#include <net/atmclip.h>
#include "ipcommon.h"
#endif

#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)
#include "lec.h"
#include "lec_arpc.h"
#endif

static ssize_t proc_dev_atm_read(struct file *file,char *buf,size_t count,
    loff_t *pos);
static ssize_t proc_spec_atm_read(struct file *file,char *buf,size_t count,
    loff_t *pos);

static struct file_operations proc_dev_atm_operations = {
	.owner =	THIS_MODULE,
	.read =		proc_dev_atm_read,
};

static struct file_operations proc_spec_atm_operations = {
	.owner =	THIS_MODULE,
	.read =		proc_spec_atm_read,
};

static void add_stats(struct seq_file *seq, const char *aal,
  const struct k_atm_aal_stats *stats)
{
	seq_printf(seq, "%s ( %d %d %d %d %d )", aal,
	    atomic_read(&stats->tx),atomic_read(&stats->tx_err),
	    atomic_read(&stats->rx),atomic_read(&stats->rx_err),
	    atomic_read(&stats->rx_drop));
}

static void atm_dev_info(struct seq_file *seq, const struct atm_dev *dev)
{
	int i;

	seq_printf(seq, "%3d %-8s", dev->number, dev->type);
	for (i = 0; i < ESI_LEN; i++)
		seq_printf(seq, "%02x", dev->esi[i]);
	seq_puts(seq, "  ");
	add_stats(seq, "0", &dev->stats.aal0);
	seq_puts(seq, "  ");
	add_stats(seq, "5", &dev->stats.aal5);
	seq_printf(seq, "\t[%d]", atomic_read(&dev->refcnt));
	seq_putc(seq, '\n');
}

#if defined(CONFIG_ATM_CLIP) || defined(CONFIG_ATM_CLIP_MODULE)

static void svc_addr(struct seq_file *seq, struct sockaddr_atmsvc *addr)
{
	static int code[] = { 1,2,10,6,1,0 };
	static int e164[] = { 1,8,4,6,1,0 };

	if (*addr->sas_addr.pub) {
		seq_printf(seq, "%s", addr->sas_addr.pub);
		if (*addr->sas_addr.prv)
			seq_putc(seq, '+');
	} else if (!*addr->sas_addr.prv) {
		seq_printf(seq, "%s", "(none)");
		return;
	}
	if (*addr->sas_addr.prv) {
		unsigned char *prv = addr->sas_addr.prv;
		int *fields;
		int i, j;

		fields = *prv == ATM_AFI_E164 ? e164 : code;
		for (i = 0; fields[i]; i++) {
			for (j = fields[i]; j; j--)
				seq_printf(seq, "%02X", *prv++);
			if (fields[i+1])
				seq_putc(seq, '.');
		}
	}
}

static void atmarp_info(struct seq_file *seq, struct net_device *dev,
			struct atmarp_entry *entry, struct clip_vcc *clip_vcc)
{
	char buf[17];
	int svc, off;

	svc = !clip_vcc || clip_vcc->vcc->sk->sk_family == AF_ATMSVC;
	seq_printf(seq, "%-6s%-4s%-4s%5ld ", dev->name, svc ? "SVC" : "PVC",
	    !clip_vcc || clip_vcc->encap ? "LLC" : "NULL",
	    (jiffies-(clip_vcc ? clip_vcc->last_use : entry->neigh->used))/HZ);

	off = snprintf(buf, sizeof(buf) - 1, "%d.%d.%d.%d", NIPQUAD(entry->ip));
	while (off < 16)
		buf[off++] = ' ';
	buf[off] = '\0';
	seq_printf(seq, "%s", buf);

	if (!clip_vcc) {
		if (time_before(jiffies, entry->expires))
			seq_printf(seq, "(resolving)\n");
		else
			seq_printf(seq, "(expired, ref %d)\n",
				   atomic_read(&entry->neigh->refcnt));
	} else if (!svc) {
		seq_printf(seq, "%d.%d.%d\n", clip_vcc->vcc->dev->number,
			   clip_vcc->vcc->vpi, clip_vcc->vcc->vci);
	} else {
		svc_addr(seq, &clip_vcc->vcc->remote);
		seq_putc(seq, '\n');
	}
}

#endif /* CONFIG_ATM_CLIP */

struct vcc_state {
	struct sock *sk;
	int family;
	int clip_info;
};

static inline int compare_family(struct sock *sk, int family)
{
	struct atm_vcc *vcc = atm_sk(sk);

	return !family || (vcc->sk->sk_family == family);
}

static int __vcc_walk(struct sock **sock, int family, loff_t l)
{
	struct sock *sk = *sock;

	if (sk == (void *)1) {
		sk = hlist_empty(&vcc_sklist) ? NULL : __sk_head(&vcc_sklist);
		l--;
	} 
	for (; sk; sk = sk_next(sk)) {
		l -= compare_family(sk, family);
		if (l < 0)
			goto out;
	}
	sk = (void *)1;
out:
	*sock = sk;
	return (l < 0);
}

static inline void *vcc_walk(struct vcc_state *state, loff_t l)
{
	return __vcc_walk(&state->sk, state->family, l) ?
	       state : NULL;
}

static int __vcc_seq_open(struct inode *inode, struct file *file,
	int family, struct seq_operations *ops)
{
	struct vcc_state *state;
	struct seq_file *seq;
	int rc = -ENOMEM;

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		goto out;

	rc = seq_open(file, ops);
	if (rc)
		goto out_kfree;

	state->family = family;
	state->clip_info = try_atm_clip_ops();

	seq = file->private_data;
	seq->private = state;
out:
	return rc;
out_kfree:
	kfree(state);
	goto out;
}

static int vcc_seq_release(struct inode *inode, struct file *file)
{
#if defined(CONFIG_ATM_CLIP) || defined(CONFIG_ATM_CLIP_MODULE)
	struct seq_file *seq = file->private_data;
	struct vcc_state *state = seq->private;

	if (state->clip_info)
		module_put(atm_clip_ops->owner);
#endif
	return seq_release_private(inode, file);
}

static void *vcc_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct vcc_state *state = seq->private;
	loff_t left = *pos;

	read_lock(&vcc_sklist_lock);
	state->sk = (void *)1;
	return left ? vcc_walk(state, left) : (void *)1;
}

static void vcc_seq_stop(struct seq_file *seq, void *v)
{
	read_unlock(&vcc_sklist_lock);
}

static void *vcc_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct vcc_state *state = seq->private;

	v = vcc_walk(state, 1);
	*pos += !!PTR_ERR(v);
	return v;
}

static void pvc_info(struct seq_file *seq, struct atm_vcc *vcc, int clip_info)
{
	static const char *class_name[] = { "off","UBR","CBR","VBR","ABR" };
	static const char *aal_name[] = {
		"---",	"1",	"2",	"3/4",	/*  0- 3 */
		"???",	"5",	"???",	"???",	/*  4- 7 */
		"???",	"???",	"???",	"???",	/*  8-11 */
		"???",	"0",	"???",	"???"};	/* 12-15 */

	seq_printf(seq, "%3d %3d %5d %-3s %7d %-5s %7d %-6s",
	    vcc->dev->number,vcc->vpi,vcc->vci,
	    vcc->qos.aal >= sizeof(aal_name)/sizeof(aal_name[0]) ? "err" :
	    aal_name[vcc->qos.aal],vcc->qos.rxtp.min_pcr,
	    class_name[vcc->qos.rxtp.traffic_class],vcc->qos.txtp.min_pcr,
	    class_name[vcc->qos.txtp.traffic_class]);
#if defined(CONFIG_ATM_CLIP) || defined(CONFIG_ATM_CLIP_MODULE)
	if (clip_info && (vcc->push == atm_clip_ops->clip_push)) {
		struct clip_vcc *clip_vcc = CLIP_VCC(vcc);
		struct net_device *dev;

		dev = clip_vcc->entry ? clip_vcc->entry->neigh->dev : NULL;
		seq_printf(seq, "CLIP, Itf:%s, Encap:",
		    dev ? dev->name : "none?");
		seq_printf(seq, "%s", clip_vcc->encap ? "LLC/SNAP" : "None");
	}
#endif
	seq_putc(seq, '\n');
}

static const char *vcc_state(struct atm_vcc *vcc)
{
	static const char *map[] = { ATM_VS2TXT_MAP };

	return map[ATM_VF2VS(vcc->flags)];
}

static void vcc_info(struct seq_file *seq, struct atm_vcc *vcc)
{
	seq_printf(seq, "%p ", vcc);
	if (!vcc->dev)
		seq_printf(seq, "Unassigned    ");
	else 
		seq_printf(seq, "%3d %3d %5d ", vcc->dev->number, vcc->vpi,
			vcc->vci);
	switch (vcc->sk->sk_family) {
		case AF_ATMPVC:
			seq_printf(seq, "PVC");
			break;
		case AF_ATMSVC:
			seq_printf(seq, "SVC");
			break;
		default:
			seq_printf(seq, "%3d", vcc->sk->sk_family);
	}
	seq_printf(seq, " %04lx  %5d %7d/%7d %7d/%7d\n", vcc->flags, vcc->sk->sk_err,
		atomic_read(&vcc->sk->sk_wmem_alloc),vcc->sk->sk_sndbuf,
		atomic_read(&vcc->sk->sk_rmem_alloc),vcc->sk->sk_rcvbuf);
}

static void svc_info(struct seq_file *seq, struct atm_vcc *vcc)
{
	if (!vcc->dev)
		seq_printf(seq, sizeof(void *) == 4 ?
			   "N/A@%p%10s" : "N/A@%p%2s", vcc, "");
	else
		seq_printf(seq, "%3d %3d %5d         ",
			   vcc->dev->number, vcc->vpi, vcc->vci);
	seq_printf(seq, "%-10s ", vcc_state(vcc));
	seq_printf(seq, "%s%s", vcc->remote.sas_addr.pub,
	    *vcc->remote.sas_addr.pub && *vcc->remote.sas_addr.prv ? "+" : "");
	if (*vcc->remote.sas_addr.prv) {
		int i;

		for (i = 0; i < ATM_ESA_LEN; i++)
			seq_printf(seq, "%02x", vcc->remote.sas_addr.prv[i]);
	}
	seq_putc(seq, '\n');
}

#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)

static char*
lec_arp_get_status_string(unsigned char status)
{
  switch(status) {
  case ESI_UNKNOWN:
    return "ESI_UNKNOWN       ";
  case ESI_ARP_PENDING:
    return "ESI_ARP_PENDING   ";
  case ESI_VC_PENDING:
    return "ESI_VC_PENDING    ";
  case ESI_FLUSH_PENDING:
    return "ESI_FLUSH_PENDING ";
  case ESI_FORWARD_DIRECT:
    return "ESI_FORWARD_DIRECT";
  default:
    return "<Unknown>         ";
  }
}

static void 
lec_info(struct lec_arp_table *entry, char *buf)
{
        int j, offset=0;

        for(j=0;j<ETH_ALEN;j++) {
                offset+=sprintf(buf+offset,"%2.2x",0xff&entry->mac_addr[j]);
        }
        offset+=sprintf(buf+offset, " ");
        for(j=0;j<ATM_ESA_LEN;j++) {
                offset+=sprintf(buf+offset,"%2.2x",0xff&entry->atm_addr[j]);
        }
        offset+=sprintf(buf+offset, " %s %4.4x",
                        lec_arp_get_status_string(entry->status),
                        entry->flags&0xffff);
        if (entry->vcc) {
                offset+=sprintf(buf+offset, "%3d %3d ", entry->vcc->vpi, 
                                entry->vcc->vci);                
        } else
                offset+=sprintf(buf+offset, "        ");
        if (entry->recv_vcc) {
                offset+=sprintf(buf+offset, "     %3d %3d", 
                                entry->recv_vcc->vpi, entry->recv_vcc->vci);
        }

        sprintf(buf+offset,"\n");
}

#endif

static int atm_dev_seq_show(struct seq_file *seq, void *v)
{
	static char atm_dev_banner[] =
		"Itf Type    ESI/\"MAC\"addr "
		"AAL(TX,err,RX,err,drop) ...               [refcnt]\n";
 
	if (v == (void *)1)
		seq_puts(seq, atm_dev_banner);
	else {
		struct atm_dev *dev = list_entry(v, struct atm_dev, dev_list);

		atm_dev_info(seq, dev);
	}
 	return 0;
}
 
static struct seq_operations atm_dev_seq_ops = {
	.start	= atm_dev_seq_start,
	.next	= atm_dev_seq_next,
	.stop	= atm_dev_seq_stop,
	.show	= atm_dev_seq_show,
};
 
static int atm_dev_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &atm_dev_seq_ops);
}
 
static struct file_operations devices_seq_fops = {
	.open		= atm_dev_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int pvc_seq_show(struct seq_file *seq, void *v)
{
	static char atm_pvc_banner[] = 
		"Itf VPI VCI   AAL RX(PCR,Class) TX(PCR,Class)\n";

	if (v == (void *)1)
		seq_puts(seq, atm_pvc_banner);
	else {
		struct vcc_state *state = seq->private;
		struct atm_vcc *vcc = atm_sk(state->sk);

		pvc_info(seq, vcc, state->clip_info);
	}
	return 0;
}

static struct seq_operations pvc_seq_ops = {
	.start	= vcc_seq_start,
	.next	= vcc_seq_next,
	.stop	= vcc_seq_stop,
	.show	= pvc_seq_show,
};

static int pvc_seq_open(struct inode *inode, struct file *file)
{
	return __vcc_seq_open(inode, file, PF_ATMPVC, &pvc_seq_ops);
}

static struct file_operations pvc_seq_fops = {
	.open		= pvc_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= vcc_seq_release,
};

static int vcc_seq_show(struct seq_file *seq, void *v)
{
 	if (v == (void *)1) {
 		seq_printf(seq, sizeof(void *) == 4 ? "%-8s%s" : "%-16s%s",
 			"Address ", "Itf VPI VCI   Fam Flags Reply "
 			"Send buffer     Recv buffer\n");
 	} else {
 		struct vcc_state *state = seq->private;
 		struct atm_vcc *vcc = atm_sk(state->sk);
  
 		vcc_info(seq, vcc);
 	}
  	return 0;
}
  
static struct seq_operations vcc_seq_ops = {
 	.start	= vcc_seq_start,
 	.next	= vcc_seq_next,
 	.stop	= vcc_seq_stop,
 	.show	= vcc_seq_show,
};
 
static int vcc_seq_open(struct inode *inode, struct file *file)
{
 	return __vcc_seq_open(inode, file, 0, &vcc_seq_ops);
}
 
static struct file_operations vcc_seq_fops = {
	.open		= vcc_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= vcc_seq_release,
};

static int svc_seq_show(struct seq_file *seq, void *v)
{
	static char atm_svc_banner[] = 
		"Itf VPI VCI           State      Remote\n";

	if (v == (void *)1)
		seq_puts(seq, atm_svc_banner);
	else {
		struct vcc_state *state = seq->private;
		struct atm_vcc *vcc = atm_sk(state->sk);

		svc_info(seq, vcc);
	}
	return 0;
}

static struct seq_operations svc_seq_ops = {
	.start	= vcc_seq_start,
	.next	= vcc_seq_next,
	.stop	= vcc_seq_stop,
	.show	= svc_seq_show,
};

static int svc_seq_open(struct inode *inode, struct file *file)
{
	return __vcc_seq_open(inode, file, PF_ATMSVC, &svc_seq_ops);
}

static struct file_operations svc_seq_fops = {
	.open		= svc_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= vcc_seq_release,
};

#if defined(CONFIG_ATM_CLIP) || defined(CONFIG_ATM_CLIP_MODULE)

struct arp_state {
	int bucket;
  	struct neighbour *n;
	struct clip_vcc *vcc;
};
  
static void *arp_vcc_walk(struct arp_state *state,
			  struct atmarp_entry *e, loff_t *l)
{
	struct clip_vcc *vcc = state->vcc;

	if (!vcc)
		vcc = e->vccs;
	if (vcc == (void *)1) {
		vcc = e->vccs;
		--*l;
  	}
	for (; vcc; vcc = vcc->next) {
		if (--*l < 0)
			break;
	}
	state->vcc = vcc;
	return (*l < 0) ? state : NULL;
}
  
static void *arp_get_idx(struct arp_state *state, loff_t l)
{
	void *v = NULL;

	for (; state->bucket <= NEIGH_HASHMASK; state->bucket++) {
		for (; state->n; state->n = state->n->next) {
			v = arp_vcc_walk(state, NEIGH2ENTRY(state->n), &l);
			if (v)
				goto done;
  		}
		state->n = clip_tbl_hook->hash_buckets[state->bucket + 1];
	}
done:
	return v;
}

static void *arp_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct arp_state *state = seq->private;
	void *ret = (void *)1;

	if (!clip_tbl_hook) {
		state->bucket = -1;
		goto out;
	}

	read_lock_bh(&clip_tbl_hook->lock);
	state->bucket = 0;
	state->n = clip_tbl_hook->hash_buckets[0];
	state->vcc = (void *)1;
	if (*pos)
		ret = arp_get_idx(state, *pos);
out:
	return ret;
}

static void arp_seq_stop(struct seq_file *seq, void *v)
{
	struct arp_state *state = seq->private;

	if (state->bucket != -1)
		read_unlock_bh(&clip_tbl_hook->lock);
}

static void *arp_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct arp_state *state = seq->private;

	v = arp_get_idx(state, 1);
	*pos += !!PTR_ERR(v);
	return v;
}

static int arp_seq_show(struct seq_file *seq, void *v)
{
	static char atm_arp_banner[] = 
		"IPitf TypeEncp Idle IP address      ATM address\n";

	if (v == (void *)1)
		seq_puts(seq, atm_arp_banner);
	else {
		struct arp_state *state = seq->private;
		struct neighbour *n = state->n;	
		struct clip_vcc *vcc = state->vcc;

		atmarp_info(seq, n->dev, NEIGH2ENTRY(n), vcc);
	}
  	return 0;
}

static struct seq_operations arp_seq_ops = {
	.start	= arp_seq_start,
	.next	= arp_seq_next,
	.stop	= arp_seq_stop,
	.show	= arp_seq_show,
};

static int arp_seq_open(struct inode *inode, struct file *file)
{
	struct arp_state *state;
	struct seq_file *seq;
	int rc = -EAGAIN;

	if (!try_atm_clip_ops())
		goto out;

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (!state) {
		rc = -ENOMEM;
		goto out_put;
	}

	rc = seq_open(file, &arp_seq_ops);
	if (rc)
		goto out_kfree;

	seq = file->private_data;
	seq->private = state;
out:
	return rc;

out_put:
	module_put(atm_clip_ops->owner);
out_kfree:
	kfree(state);
	goto out;
}

static int arp_seq_release(struct inode *inode, struct file *file)
{
	module_put(atm_clip_ops->owner);
	return seq_release_private(inode, file);
}

static struct file_operations arp_seq_fops = {
	.open		= arp_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= arp_seq_release,
};

#endif /* CONFIG_ATM_CLIP */

#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)
static int atm_lec_info(loff_t pos,char *buf)
{
	unsigned long flags;
	struct lec_priv *priv;
	struct lec_arp_table *entry;
	int i, count, d, e;
	struct net_device *dev;

	if (!pos) {
		return sprintf(buf,"Itf  MAC          ATM destination"
		    "                          Status            Flags "
		    "VPI/VCI Recv VPI/VCI\n");
	}
	if (!try_atm_lane_ops())
		return 0; /* the lane module is not there yet */

	count = pos;
	for(d = 0; d < MAX_LEC_ITF; d++) {
		dev = atm_lane_ops->get_lec(d);
		if (!dev || !(priv = (struct lec_priv *) dev->priv))
			continue;
		spin_lock_irqsave(&priv->lec_arp_lock, flags);
		for(i = 0; i < LEC_ARP_TABLE_SIZE; i++) {
			for(entry = priv->lec_arp_tables[i]; entry; entry = entry->next) {
				if (--count)
					continue;
				e = sprintf(buf,"%s ", dev->name);
				lec_info(entry, buf+e);
				spin_unlock_irqrestore(&priv->lec_arp_lock, flags);
				dev_put(dev);
				module_put(atm_lane_ops->owner);
				return strlen(buf);
			}
		}
		for(entry = priv->lec_arp_empty_ones; entry; entry = entry->next) {
			if (--count)
				continue;
			e = sprintf(buf,"%s ", dev->name);
			lec_info(entry, buf+e);
			spin_unlock_irqrestore(&priv->lec_arp_lock, flags);
			dev_put(dev);
			module_put(atm_lane_ops->owner);
			return strlen(buf);
		}
		for(entry = priv->lec_no_forward; entry; entry=entry->next) {
			if (--count)
				continue;
			e = sprintf(buf,"%s ", dev->name);
			lec_info(entry, buf+e);
			spin_unlock_irqrestore(&priv->lec_arp_lock, flags);
			dev_put(dev);
			module_put(atm_lane_ops->owner);
			return strlen(buf);
		}
		for(entry = priv->mcast_fwds; entry; entry = entry->next) {
			if (--count)
				continue;
			e = sprintf(buf,"%s ", dev->name);
			lec_info(entry, buf+e);
			spin_unlock_irqrestore(&priv->lec_arp_lock, flags);
			dev_put(dev);
			module_put(atm_lane_ops->owner);
			return strlen(buf);
		}
		spin_unlock_irqrestore(&priv->lec_arp_lock, flags);
		dev_put(dev);
	}
	module_put(atm_lane_ops->owner);
	return 0;
}
#endif


static ssize_t proc_dev_atm_read(struct file *file,char *buf,size_t count,
    loff_t *pos)
{
	struct atm_dev *dev;
	unsigned long page;
	int length;

	if (count == 0) return 0;
	page = get_zeroed_page(GFP_KERNEL);
	if (!page) return -ENOMEM;
	dev = PDE(file->f_dentry->d_inode)->data;
	if (!dev->ops->proc_read)
		length = -EINVAL;
	else {
		length = dev->ops->proc_read(dev,pos,(char *) page);
		if (length > count) length = -EINVAL;
	}
	if (length >= 0) {
		if (copy_to_user(buf,(char *) page,length)) length = -EFAULT;
		(*pos)++;
	}
	free_page(page);
	return length;
}


static ssize_t proc_spec_atm_read(struct file *file,char *buf,size_t count,
    loff_t *pos)
{
	unsigned long page;
	int length;
	int (*info)(loff_t,char *);
	info = PDE(file->f_dentry->d_inode)->data;

	if (count == 0) return 0;
	page = get_zeroed_page(GFP_KERNEL);
	if (!page) return -ENOMEM;
	length = (*info)(*pos,(char *) page);
	if (length > count) length = -EINVAL;
	if (length >= 0) {
		if (copy_to_user(buf,(char *) page,length)) length = -EFAULT;
		(*pos)++;
	}
	free_page(page);
	return length;
}


struct proc_dir_entry *atm_proc_root;
EXPORT_SYMBOL(atm_proc_root);


int atm_proc_dev_register(struct atm_dev *dev)
{
	int digits,num;
	int error;

	/* No proc info */
	if (!dev->ops->proc_read)
		return 0;

	error = -ENOMEM;
	digits = 0;
	for (num = dev->number; num; num /= 10) digits++;
	if (!digits) digits++;

	dev->proc_name = kmalloc(strlen(dev->type) + digits + 2, GFP_KERNEL);
	if (!dev->proc_name)
		goto fail1;
	sprintf(dev->proc_name,"%s:%d",dev->type, dev->number);

	dev->proc_entry = create_proc_entry(dev->proc_name, 0, atm_proc_root);
	if (!dev->proc_entry)
		goto fail0;
	dev->proc_entry->data = dev;
	dev->proc_entry->proc_fops = &proc_dev_atm_operations;
	dev->proc_entry->owner = THIS_MODULE;
	return 0;
fail0:
	kfree(dev->proc_name);
fail1:
	return error;
}


void atm_proc_dev_deregister(struct atm_dev *dev)
{
	if (!dev->ops->proc_read)
		return;

	remove_proc_entry(dev->proc_name, atm_proc_root);
	kfree(dev->proc_name);
}

#define CREATE_SEQ_ENTRY(name) \
	do { \
		name = create_proc_entry(#name, S_IRUGO, atm_proc_root); \
		if (!name) \
			goto cleanup; \
		name->proc_fops = & name##_seq_fops; \
		name->owner = THIS_MODULE; \
	} while (0)

#define CREATE_ENTRY(name) \
    name = create_proc_entry(#name,0,atm_proc_root); \
    if (!name) goto cleanup; \
    name->data = atm_##name##_info; \
    name->proc_fops = &proc_spec_atm_operations; \
    name->owner = THIS_MODULE

static struct proc_dir_entry *devices = NULL, *pvc = NULL,
		*svc = NULL, *arp = NULL, *lec = NULL, *vcc = NULL;

static void atm_proc_cleanup(void)
{
	if (devices)
		remove_proc_entry("devices",atm_proc_root);
	if (pvc)
		remove_proc_entry("pvc",atm_proc_root);
	if (svc)
		remove_proc_entry("svc",atm_proc_root);
	if (arp)
		remove_proc_entry("arp",atm_proc_root);
	if (lec)
		remove_proc_entry("lec",atm_proc_root);
	if (vcc)
		remove_proc_entry("vcc",atm_proc_root);
	remove_proc_entry("net/atm",NULL);
}

int __init atm_proc_init(void)
{
	atm_proc_root = proc_mkdir("net/atm",NULL);
	if (!atm_proc_root)
		return -ENOMEM;
	CREATE_SEQ_ENTRY(devices);
	CREATE_SEQ_ENTRY(pvc);
	CREATE_SEQ_ENTRY(svc);
	CREATE_SEQ_ENTRY(vcc);
#if defined(CONFIG_ATM_CLIP) || defined(CONFIG_ATM_CLIP_MODULE)
	CREATE_SEQ_ENTRY(arp);
#endif
#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)
	CREATE_ENTRY(lec);
#endif
	return 0;

cleanup:
	atm_proc_cleanup();
	return -ENOMEM;
}

void atm_proc_exit(void)
{
	atm_proc_cleanup();
}
