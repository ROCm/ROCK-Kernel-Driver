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

#ifdef CONFIG_ATM_CLIP
#include <net/atmclip.h>
#include "ipcommon.h"
extern void clip_push(struct atm_vcc *vcc,struct sk_buff *skb);
#endif

#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)
#include "lec.h"
#include "lec_arpc.h"
extern struct atm_lane_ops atm_lane_ops; /* in common.c */
#endif

static ssize_t proc_dev_atm_read(struct file *file,char *buf,size_t count,
    loff_t *pos);
static ssize_t proc_spec_atm_read(struct file *file,char *buf,size_t count,
    loff_t *pos);

static struct file_operations proc_dev_atm_operations = {
	read:		proc_dev_atm_read,
};

static struct file_operations proc_spec_atm_operations = {
	read:		proc_spec_atm_read,
};

static void add_stats(char *buf,const char *aal,
  const struct k_atm_aal_stats *stats)
{
	sprintf(strchr(buf,0),"%s ( %d %d %d %d %d )",aal,
	    atomic_read(&stats->tx),atomic_read(&stats->tx_err),
	    atomic_read(&stats->rx),atomic_read(&stats->rx_err),
	    atomic_read(&stats->rx_drop));
}


static void dev_info(const struct atm_dev *dev,char *buf)
{
	int off,i;

	off = sprintf(buf,"%3d %-8s",dev->number,dev->type);
	for (i = 0; i < ESI_LEN; i++)
		off += sprintf(buf+off,"%02x",dev->esi[i]);
	strcat(buf,"  ");
	add_stats(buf,"0",&dev->stats.aal0);
	strcat(buf,"  ");
	add_stats(buf,"5",&dev->stats.aal5);
	strcat(buf,"\n");
}


#ifdef CONFIG_ATM_CLIP


static int svc_addr(char *buf,struct sockaddr_atmsvc *addr)
{
	static int code[] = { 1,2,10,6,1,0 };
	static int e164[] = { 1,8,4,6,1,0 };
	int *fields;
	int len,i,j,pos;

	len = 0;
	if (*addr->sas_addr.pub) {
		strcpy(buf,addr->sas_addr.pub);
		len = strlen(addr->sas_addr.pub);
		buf += len;
		if (*addr->sas_addr.prv) {
			*buf++ = '+';
			len++;
		}
	}
	else if (!*addr->sas_addr.prv) {
			strcpy(buf,"(none)");
			return strlen(buf);
		}
	if (*addr->sas_addr.prv) {
		len += 44;
		pos = 0;
		fields = *addr->sas_addr.prv == ATM_AFI_E164 ? e164 : code;
		for (i = 0; fields[i]; i++) {
			for (j = fields[i]; j; j--) {
				sprintf(buf,"%02X",addr->sas_addr.prv[pos++]);
				buf += 2;
			}
			if (fields[i+1]) *buf++ = '.';
		}
	}
	return len;
}


static void atmarp_info(struct net_device *dev,struct atmarp_entry *entry,
    struct clip_vcc *clip_vcc,char *buf)
{
	unsigned char *ip;
	int svc,off,ip_len;

	svc = !clip_vcc || clip_vcc->vcc->family == AF_ATMSVC;
	off = sprintf(buf,"%-6s%-4s%-4s%5ld ",dev->name,svc ? "SVC" : "PVC",
	    !clip_vcc || clip_vcc->encap ? "LLC" : "NULL",
	    (jiffies-(clip_vcc ? clip_vcc->last_use : entry->neigh->used))/
	    HZ);
	ip = (unsigned char *) &entry->ip;
	ip_len = sprintf(buf+off,"%d.%d.%d.%d",ip[0],ip[1],ip[2],ip[3]);
	off += ip_len;
	while (ip_len++ < 16) buf[off++] = ' ';
	if (!clip_vcc)
		if (time_before(jiffies, entry->expires))
			strcpy(buf+off,"(resolving)\n");
		else sprintf(buf+off,"(expired, ref %d)\n",
			    atomic_read(&entry->neigh->refcnt));
	else if (!svc)
			sprintf(buf+off,"%d.%d.%d\n",clip_vcc->vcc->dev->number,
			    clip_vcc->vcc->vpi,clip_vcc->vcc->vci);
		else {
			off += svc_addr(buf+off,&clip_vcc->vcc->remote);
			strcpy(buf+off,"\n");
		}
}


#endif


static void pvc_info(struct atm_vcc *vcc,char *buf)
{
	static const char *class_name[] = { "off","UBR","CBR","VBR","ABR" };
	static const char *aal_name[] = {
		"---",	"1",	"2",	"3/4",	/*  0- 3 */
		"???",	"5",	"???",	"???",	/*  4- 7 */
		"???",	"???",	"???",	"???",	/*  8-11 */
		"???",	"0",	"???",	"???"};	/* 12-15 */
	int off;

	off = sprintf(buf,"%3d %3d %5d %-3s %7d %-5s %7d %-6s",
	    vcc->dev->number,vcc->vpi,vcc->vci,
	    vcc->qos.aal >= sizeof(aal_name)/sizeof(aal_name[0]) ? "err" :
	    aal_name[vcc->qos.aal],vcc->qos.rxtp.min_pcr,
	    class_name[vcc->qos.rxtp.traffic_class],vcc->qos.txtp.min_pcr,
	    class_name[vcc->qos.txtp.traffic_class]);
#ifdef CONFIG_ATM_CLIP
	if (vcc->push == clip_push) {
		struct clip_vcc *clip_vcc = CLIP_VCC(vcc);
		struct net_device *dev;

		dev = clip_vcc->entry ? clip_vcc->entry->neigh->dev : NULL;
		off += sprintf(buf+off,"CLIP, Itf:%s, Encap:",
		    dev ? dev->name : "none?");
		if (clip_vcc->encap) off += sprintf(buf+off,"LLC/SNAP");
		else off += sprintf(buf+off,"None");
	}
#endif
	strcpy(buf+off,"\n");
}


static const char *vcc_state(struct atm_vcc *vcc)
{
	static const char *map[] = { ATM_VS2TXT_MAP };

	return map[ATM_VF2VS(vcc->flags)];
}


static void vc_info(struct atm_vcc *vcc,char *buf)
{
	char *here;

	here = buf+sprintf(buf,"%p ",vcc);
	if (!vcc->dev) here += sprintf(here,"Unassigned    ");
	else here += sprintf(here,"%3d %3d %5d ",vcc->dev->number,vcc->vpi,
		    vcc->vci);
	switch (vcc->family) {
		case AF_ATMPVC:
			here += sprintf(here,"PVC");
			break;
		case AF_ATMSVC:
			here += sprintf(here,"SVC");
			break;
		default:
			here += sprintf(here,"%3d",vcc->family);
	}
	here += sprintf(here," %04lx  %5d %7d/%7d %7d/%7d\n",vcc->flags.bits,
	    vcc->reply,
	    atomic_read(&vcc->tx_inuse),vcc->sk->sndbuf,
	    atomic_read(&vcc->rx_inuse),vcc->sk->rcvbuf);
}


static void svc_info(struct atm_vcc *vcc,char *buf)
{
	char *here;
	int i;

	if (!vcc->dev)
		sprintf(buf,sizeof(void *) == 4 ? "N/A@%p%10s" : "N/A@%p%2s",
		    vcc,"");
	else sprintf(buf,"%3d %3d %5d         ",vcc->dev->number,vcc->vpi,
		    vcc->vci);
	here = strchr(buf,0);
	here += sprintf(here,"%-10s ",vcc_state(vcc));
	here += sprintf(here,"%s%s",vcc->remote.sas_addr.pub,
	    *vcc->remote.sas_addr.pub && *vcc->remote.sas_addr.prv ? "+" : "");
	if (*vcc->remote.sas_addr.prv)
		for (i = 0; i < ATM_ESA_LEN; i++)
			here += sprintf(here,"%02x",
			    vcc->remote.sas_addr.prv[i]);
	strcat(here,"\n");
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

static int atm_devices_info(loff_t pos,char *buf)
{
	struct atm_dev *dev;
	int left;

	if (!pos) {
		return sprintf(buf,"Itf Type    ESI/\"MAC\"addr "
		    "AAL(TX,err,RX,err,drop) ...\n");
	}
	left = pos-1;
	for (dev = atm_devs; dev && left; dev = dev->next) left--;
	if (!dev) return 0;
	dev_info(dev,buf);
	return strlen(buf);
}

/*
 * FIXME: it isn't safe to walk the VCC list without turning off interrupts.
 * What is really needed is some lock on the devices. Ditto for ATMARP.
 */

static int atm_pvc_info(loff_t pos,char *buf)
{
	struct atm_dev *dev;
	struct atm_vcc *vcc;
	int left;

	if (!pos) {
		return sprintf(buf,"Itf VPI VCI   AAL RX(PCR,Class) "
		    "TX(PCR,Class)\n");
	}
	left = pos-1;
	for (dev = atm_devs; dev; dev = dev->next)
		for (vcc = dev->vccs; vcc; vcc = vcc->next)
			if (vcc->family == PF_ATMPVC &&
			    vcc->dev && !left--) {
				pvc_info(vcc,buf);
				return strlen(buf);
			}
	return 0;
}


static int atm_vc_info(loff_t pos,char *buf)
{
	struct atm_dev *dev;
	struct atm_vcc *vcc;
	int left;

	if (!pos)
		return sprintf(buf,sizeof(void *) == 4 ? "%-8s%s" : "%-16s%s",
		    "Address"," Itf VPI VCI   Fam Flags Reply Send buffer"
		    "     Recv buffer\n");
	left = pos-1;
	for (dev = atm_devs; dev; dev = dev->next)
		for (vcc = dev->vccs; vcc; vcc = vcc->next)
			if (!left--) {
				vc_info(vcc,buf);
				return strlen(buf);
			}
	for (vcc = nodev_vccs; vcc; vcc = vcc->next)
		if (!left--) {
			vc_info(vcc,buf);
			return strlen(buf);
		}

	return 0;
}


static int atm_svc_info(loff_t pos,char *buf)
{
	struct atm_dev *dev;
	struct atm_vcc *vcc;
	int left;

	if (!pos)
		return sprintf(buf,"Itf VPI VCI           State      Remote\n");
	left = pos-1;
	for (dev = atm_devs; dev; dev = dev->next)
		for (vcc = dev->vccs; vcc; vcc = vcc->next)
			if (vcc->family == PF_ATMSVC && !left--) {
				svc_info(vcc,buf);
				return strlen(buf);
			}
	for (vcc = nodev_vccs; vcc; vcc = vcc->next)
		if (vcc->family == PF_ATMSVC && !left--) {
			svc_info(vcc,buf);
			return strlen(buf);
		}
	return 0;
}

#ifdef CONFIG_ATM_CLIP
static int atm_arp_info(loff_t pos,char *buf)
{
	struct neighbour *n;
	int i,count;

	if (!pos) {
		return sprintf(buf,"IPitf TypeEncp Idle IP address      "
		    "ATM address\n");
	}
	count = pos;
	read_lock_bh(&clip_tbl.lock);
	for (i = 0; i <= NEIGH_HASHMASK; i++)
		for (n = clip_tbl.hash_buckets[i]; n; n = n->next) {
			struct atmarp_entry *entry = NEIGH2ENTRY(n);
			struct clip_vcc *vcc;

			if (!entry->vccs) {
				if (--count) continue;
				atmarp_info(n->dev,entry,NULL,buf);
				read_unlock_bh(&clip_tbl.lock);
				return strlen(buf);
			}
			for (vcc = entry->vccs; vcc;
			    vcc = vcc->next) {
				if (--count) continue;
				atmarp_info(n->dev,entry,vcc,buf);
				read_unlock_bh(&clip_tbl.lock);
				return strlen(buf);
			}
		}
	read_unlock_bh(&clip_tbl.lock);
	return 0;
}
#endif

#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)
static int atm_lec_info(loff_t pos,char *buf)
{
	struct lec_priv *priv;
	struct lec_arp_table *entry;
	int i, count, d, e;
	struct net_device **dev_lec;

	if (!pos) {
		return sprintf(buf,"Itf  MAC          ATM destination"
		    "                          Status            Flags "
		    "VPI/VCI Recv VPI/VCI\n");
	}
	if (atm_lane_ops.get_lecs == NULL)
		return 0; /* the lane module is not there yet */
	else
		dev_lec = atm_lane_ops.get_lecs();

	count = pos;
	for(d=0;d<MAX_LEC_ITF;d++) {
		if (!dev_lec[d] || !(priv =
		    (struct lec_priv *) dev_lec[d]->priv)) continue;
		for(i=0;i<LEC_ARP_TABLE_SIZE;i++) {
			entry = priv->lec_arp_tables[i];
			for(;entry;entry=entry->next) {
				if (--count) continue;
				e=sprintf(buf,"%s ",
				    dev_lec[d]->name);
				lec_info(entry,buf+e);
				return strlen(buf);
			}
		}
		for(entry=priv->lec_arp_empty_ones; entry;
		    entry=entry->next) {
			if (--count) continue;
			e=sprintf(buf,"%s ",dev_lec[d]->name);
			lec_info(entry, buf+e);
			return strlen(buf);
		}
		for(entry=priv->lec_no_forward; entry;
		    entry=entry->next) {
			if (--count) continue;
			e=sprintf(buf,"%s ",dev_lec[d]->name);
			lec_info(entry, buf+e);
			return strlen(buf);
		}
		for(entry=priv->mcast_fwds; entry;
		    entry=entry->next) {
			if (--count) continue;
			e=sprintf(buf,"%s ",dev_lec[d]->name);
			lec_info(entry, buf+e);
			return strlen(buf);
		}
	}
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
	page = get_free_page(GFP_KERNEL);
	if (!page) return -ENOMEM;
	dev = ((struct proc_dir_entry *) file->f_dentry->d_inode->u.generic_ip)
	    ->data;
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
	info = ((struct proc_dir_entry *) file->f_dentry->d_inode->u.generic_ip)
	    ->data;

	if (count == 0) return 0;
	page = get_free_page(GFP_KERNEL);
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

	error = -ENOMEM;
	digits = 0;
	for (num = dev->number; num; num /= 10) digits++;
	if (!digits) digits++;
	dev->proc_name = kmalloc(strlen(dev->type)+digits+2,GFP_KERNEL);
	if (!dev->proc_name) goto fail1;
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
	remove_proc_entry(dev->proc_name, atm_proc_root);
	kfree(dev->proc_name);
}


#define CREATE_ENTRY(name) \
    name = create_proc_entry(#name,0,atm_proc_root); \
    if (!name) goto cleanup; \
    name->data = atm_##name##_info; \
    name->proc_fops = &proc_spec_atm_operations; \
    name->owner = THIS_MODULE


int __init atm_proc_init(void)
{
	struct proc_dir_entry *devices = NULL,*pvc = NULL,*svc = NULL;
	struct proc_dir_entry *arp = NULL,*lec = NULL,*vc = NULL;

	atm_proc_root = proc_mkdir("net/atm",NULL);
	if (!atm_proc_root)
		return -ENOMEM;
	CREATE_ENTRY(devices);
	CREATE_ENTRY(pvc);
	CREATE_ENTRY(svc);
	CREATE_ENTRY(vc);
#ifdef CONFIG_ATM_CLIP
	CREATE_ENTRY(arp);
#endif
#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)
	CREATE_ENTRY(lec);
#endif
	return 0;

cleanup:
	if (devices) remove_proc_entry("devices",atm_proc_root);
	if (pvc) remove_proc_entry("pvc",atm_proc_root);
	if (svc) remove_proc_entry("svc",atm_proc_root);
	if (arp) remove_proc_entry("arp",atm_proc_root);
	if (lec) remove_proc_entry("lec",atm_proc_root);
	if (vc) remove_proc_entry("vc",atm_proc_root);
	remove_proc_entry("net/atm",NULL);
	return -ENOMEM;
}
