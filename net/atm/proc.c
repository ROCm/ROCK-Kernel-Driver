/* net/atm/proc.c - ATM /proc interface
 *
 * Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA
 *
 * seq_file api usage by romieu@fr.zoreil.com
 *
 * Evaluating the efficiency of the whole thing if left as an exercise to
 * the reader.
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
#include <linux/init.h> /* for __init */
#include <net/atmclip.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <asm/param.h> /* for HZ */
#include "resources.h"
#include "common.h" /* atm_proc_init prototype */
#include "signaling.h" /* to get sigd - ugly too */

#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)
#include "lec.h"
#include "lec_arpc.h"
#endif

static ssize_t proc_dev_atm_read(struct file *file,char *buf,size_t count,
    loff_t *pos);

static struct file_operations proc_atm_dev_ops = {
	.owner =	THIS_MODULE,
	.read =		proc_dev_atm_read,
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

struct vcc_state {
	struct sock *sk;
	int family;
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

static void pvc_info(struct seq_file *seq, struct atm_vcc *vcc)
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
	if (test_bit(ATM_VF_IS_CLIP, &vcc->flags)) {
		struct clip_vcc *clip_vcc = CLIP_VCC(vcc);
		struct net_device *dev;

		dev = clip_vcc->entry ? clip_vcc->entry->neigh->dev : NULL;
		seq_printf(seq, "CLIP, Itf:%s, Encap:",
		    dev ? dev->name : "none?");
		seq_printf(seq, "%s", clip_vcc->encap ? "LLC/SNAP" : "None");
	}
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

static char* lec_arp_get_status_string(unsigned char status)
{
	static char *lec_arp_status_string[] = {
		"ESI_UNKNOWN       ",
		"ESI_ARP_PENDING   ",
		"ESI_VC_PENDING    ",
		"<Unknown>         ",
		"ESI_FLUSH_PENDING ",
		"ESI_FORWARD_DIRECT",
		"<Undefined>"
	};

	if (status > ESI_FORWARD_DIRECT)
		status = ESI_FORWARD_DIRECT + 1;
	return lec_arp_status_string[status];
}

static void lec_info(struct seq_file *seq, struct lec_arp_table *entry)
{
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		seq_printf(seq, "%2.2x", entry->mac_addr[i] & 0xff);
	seq_printf(seq, " ");
	for (i = 0; i < ATM_ESA_LEN; i++)
		seq_printf(seq, "%2.2x", entry->atm_addr[i] & 0xff);
	seq_printf(seq, " %s %4.4x", lec_arp_get_status_string(entry->status),
		   entry->flags & 0xffff);
	if (entry->vcc)
		seq_printf(seq, "%3d %3d ", entry->vcc->vpi, entry->vcc->vci);
	else
	        seq_printf(seq, "        ");
	if (entry->recv_vcc) {
		seq_printf(seq, "     %3d %3d", entry->recv_vcc->vpi,
			   entry->recv_vcc->vci);
        }
        seq_putc(seq, '\n');
}

#endif /* CONFIG_ATM_LANE */

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

		pvc_info(seq, vcc);
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

#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)

struct lec_state {
	unsigned long flags;
	struct lec_priv *locked;
	struct lec_arp_table *entry;
	struct net_device *dev;
	int itf;
	int arp_table;
	int misc_table;
};

static void *lec_tbl_walk(struct lec_state *state, struct lec_arp_table *tbl,
			  loff_t *l)
{
	struct lec_arp_table *e = state->entry;

	if (!e)
		e = tbl;
	if (e == (void *)1) {
		e = tbl;
		--*l;
	}
	for (; e; e = e->next) {
		if (--*l < 0)
			break;
	}
	state->entry = e;
	return (*l < 0) ? state : NULL;
}

static void *lec_arp_walk(struct lec_state *state, loff_t *l,
			      struct lec_priv *priv)
{
	void *v = NULL;
	int p;

	for (p = state->arp_table; p < LEC_ARP_TABLE_SIZE; p++) {
		v = lec_tbl_walk(state, priv->lec_arp_tables[p], l);
		if (v)
			break;
	}
	state->arp_table = p;
	return v;
}

static void *lec_misc_walk(struct lec_state *state, loff_t *l,
			   struct lec_priv *priv)
{
	struct lec_arp_table *lec_misc_tables[] = {
		priv->lec_arp_empty_ones,
		priv->lec_no_forward,
		priv->mcast_fwds
	};
	void *v = NULL;
	int q;

	for (q = state->misc_table; q < ARRAY_SIZE(lec_misc_tables); q++) {
		v = lec_tbl_walk(state, lec_misc_tables[q], l);
		if (v)
			break;
	}
	state->misc_table = q;
	return v;
}

static void *lec_priv_walk(struct lec_state *state, loff_t *l,
			   struct lec_priv *priv)
{
	if (!state->locked) {
		state->locked = priv;
		spin_lock_irqsave(&priv->lec_arp_lock, state->flags);
	}
	if (!lec_arp_walk(state, l, priv) &&
	    !lec_misc_walk(state, l, priv)) {
		spin_unlock_irqrestore(&priv->lec_arp_lock, state->flags);
		state->locked = NULL;
		/* Partial state reset for the next time we get called */
		state->arp_table = state->misc_table = 0;
	}
	return state->locked;
}

static void *lec_itf_walk(struct lec_state *state, loff_t *l)
{
	struct net_device *dev;
	void *v;

	dev = state->dev ? state->dev : atm_lane_ops->get_lec(state->itf);
	v = (dev && dev->priv) ? lec_priv_walk(state, l, dev->priv) : NULL;
	if (!v && dev) {
		dev_put(dev);
		/* Partial state reset for the next time we get called */
		dev = NULL;
	}
	state->dev = dev;
	return v;
}

static void *lec_get_idx(struct lec_state *state, loff_t l)
{
	void *v = NULL;

	for (; state->itf < MAX_LEC_ITF; state->itf++) {
		v = lec_itf_walk(state, &l);
		if (v)
			break;
	}
	return v; 
}

static void *lec_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct lec_state *state = seq->private;

	state->itf = 0;
	state->dev = NULL;
	state->locked = NULL;
	state->arp_table = 0;
	state->misc_table = 0;
	state->entry = (void *)1;

	return *pos ? lec_get_idx(state, *pos) : (void*)1;
}

static void lec_seq_stop(struct seq_file *seq, void *v)
{
	struct lec_state *state = seq->private;

	if (state->dev) {
		spin_unlock_irqrestore(&state->locked->lec_arp_lock,
				       state->flags);
		dev_put(state->dev);
	}
}

static void *lec_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct lec_state *state = seq->private;

	v = lec_get_idx(state, 1);
	*pos += !!PTR_ERR(v);
	return v;
}

static int lec_seq_show(struct seq_file *seq, void *v)
{
	static char lec_banner[] = "Itf  MAC          ATM destination" 
		"                          Status            Flags "
		"VPI/VCI Recv VPI/VCI\n";

	if (v == (void *)1)
		seq_puts(seq, lec_banner);
	else {
		struct lec_state *state = seq->private;
		struct net_device *dev = state->dev; 

		seq_printf(seq, "%s ", dev->name);
		lec_info(seq, state->entry);
	}
	return 0;
}

static struct seq_operations lec_seq_ops = {
	.start	= lec_seq_start,
	.next	= lec_seq_next,
	.stop	= lec_seq_stop,
	.show	= lec_seq_show,
};

static int lec_seq_open(struct inode *inode, struct file *file)
{
	struct lec_state *state;
	struct seq_file *seq;
	int rc = -EAGAIN;

	if (!try_atm_lane_ops())
		goto out;

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (!state) {
		rc = -ENOMEM;
		goto out;
	}

	rc = seq_open(file, &lec_seq_ops);
	if (rc)
		goto out_kfree;
	seq = file->private_data;
	seq->private = state;
out:
	return rc;
out_kfree:
	kfree(state);
	goto out;
}

static int lec_seq_release(struct inode *inode, struct file *file)
{
	module_put(atm_lane_ops->owner);
	return seq_release_private(inode, file);
}

static struct file_operations lec_seq_fops = {
	.open		= lec_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= lec_seq_release,
};

#endif /* CONFIG_ATM_LANE */

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
		goto err_out;
	sprintf(dev->proc_name,"%s:%d",dev->type, dev->number);

	dev->proc_entry = create_proc_entry(dev->proc_name, 0, atm_proc_root);
	if (!dev->proc_entry)
		goto err_free_name;
	dev->proc_entry->data = dev;
	dev->proc_entry->proc_fops = &proc_atm_dev_ops;
	dev->proc_entry->owner = THIS_MODULE;
	return 0;
err_free_name:
	kfree(dev->proc_name);
err_out:
	return error;
}


void atm_proc_dev_deregister(struct atm_dev *dev)
{
	if (!dev->ops->proc_read)
		return;

	remove_proc_entry(dev->proc_name, atm_proc_root);
	kfree(dev->proc_name);
}

static struct atm_proc_entry {
	char *name;
	struct file_operations *proc_fops;
	struct proc_dir_entry *dirent;
} atm_proc_ents[] = {
	{ .name = "devices",	.proc_fops = &devices_seq_fops },
	{ .name = "pvc",	.proc_fops = &pvc_seq_fops },
	{ .name = "svc",	.proc_fops = &svc_seq_fops },
	{ .name = "vc",		.proc_fops = &vcc_seq_fops },
#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)
	{ .name = "lec",	.proc_fops = &lec_seq_fops },
#endif
	{ .name = NULL,		.proc_fops = NULL }
};

static void atm_proc_dirs_remove(void)
{
	static struct atm_proc_entry *e;

	for (e = atm_proc_ents; e->name; e++) {
		if (e->dirent) 
			remove_proc_entry(e->name, atm_proc_root);
	}
	remove_proc_entry("net/atm", NULL);
}

int __init atm_proc_init(void)
{
	static struct atm_proc_entry *e;
	int ret;

	atm_proc_root = proc_mkdir("net/atm",NULL);
	if (!atm_proc_root)
		goto err_out;
	for (e = atm_proc_ents; e->name; e++) {
		struct proc_dir_entry *dirent;

		dirent = create_proc_entry(e->name, S_IRUGO, atm_proc_root);
		if (!dirent)
			goto err_out_remove;
		dirent->proc_fops = e->proc_fops;
		dirent->owner = THIS_MODULE;
		e->dirent = dirent;
	}
	ret = 0;
out:
	return ret;

err_out_remove:
	atm_proc_dirs_remove();
err_out:
	ret = -ENOMEM;
	goto out;
}

void __exit atm_proc_exit(void)
{
	atm_proc_dirs_remove();
}
