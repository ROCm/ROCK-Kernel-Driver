/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		ipv4 proc support
 *
 *		Arnaldo Carvalho de Melo <acme@conectiva.com.br>, 2002/10/10
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License as
 *		published by the Free Software Foundation; version 2 of the
 *		License
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <net/neighbour.h>
#include <net/arp.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>

extern int raw_get_info(char *, char **, off_t, int);
extern int snmp_get_info(char *, char **, off_t, int);
extern int netstat_get_info(char *, char **, off_t, int);
extern int afinet_get_info(char *, char **, off_t, int);
extern int tcp_get_info(char *, char **, off_t, int);
extern int udp_get_info(char *, char **, off_t, int);

#ifdef CONFIG_PROC_FS
#ifdef CONFIG_AX25
/*
 *	ax25 -> ASCII conversion
 */
char *ax2asc2(ax25_address *a, char *buf)
{
	char c, *s;
	int n;

	for (n = 0, s = buf; n < 6; n++) {
		c = (a->ax25_call[n] >> 1) & 0x7F;

		if (c != ' ') *s++ = c;
	}
	
	*s++ = '-';

	if ((n = ((a->ax25_call[6] >> 1) & 0x0F)) > 9) {
		*s++ = '1';
		n -= 10;
	}
	
	*s++ = n + '0';
	*s++ = '\0';

	if (*buf == '\0' || *buf == '-')
	   return "*";

	return buf;

}
#endif /* CONFIG_AX25 */

static void *arp_seq_start(struct seq_file *seq, loff_t *pos)
{
	return (void *)(unsigned long)++*pos;
}

static void *arp_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return (void *)(unsigned long)((++*pos) >=
				       (NEIGH_HASHMASK +
					PNEIGH_HASHMASK - 1) ? 0 : *pos);
}

static void arp_seq_stop(struct seq_file *seq, void *v)
{
}

#define HBUFFERLEN 30

static __inline__ void arp_format_neigh_table(struct seq_file *seq, int entry)
{
	char hbuffer[HBUFFERLEN];
	const char hexbuf[] = "0123456789ABCDEF";
	struct neighbour *n;
	int k, j;

	read_lock_bh(&arp_tbl.lock);
	for (n = arp_tbl.hash_buckets[entry]; n; n = n->next) {
		char tbuf[16];
		struct net_device *dev = n->dev;
		int hatype = dev->type;

		/* Do not confuse users "arp -a" with magic entries */
		if (!(n->nud_state & ~NUD_NOARP))
			continue;

		read_lock(&n->lock);
		/* Convert hardware address to XX:XX:XX:XX ... form. */
#ifdef CONFIG_AX25
		if (hatype == ARPHRD_AX25 || hatype == ARPHRD_NETROM)
			ax2asc2((ax25_address *)n->ha, hbuffer);
		else {
#endif
		for (k = 0, j = 0; k < HBUFFERLEN - 3 &&
				   j < dev->addr_len; j++) {
			hbuffer[k++] = hexbuf[(n->ha[j] >> 4) & 15];
			hbuffer[k++] = hexbuf[n->ha[j] & 15];
			hbuffer[k++] = ':';
		}
		hbuffer[--k] = 0;
#ifdef CONFIG_AX25
		}
#endif
		sprintf(tbuf, "%u.%u.%u.%u",
			NIPQUAD(*(u32*)n->primary_key));
		seq_printf(seq, "%-16s 0x%-10x0x%-10x%s"
				"     *        %s\n",
			   tbuf, hatype, arp_state_to_flags(n), 
			   hbuffer, dev->name);
		read_unlock(&n->lock);
	}
	read_unlock_bh(&arp_tbl.lock);
}

static __inline__ void arp_format_pneigh_table(struct seq_file *seq, int entry)
{
	struct pneigh_entry *n;

	for (n = arp_tbl.phash_buckets[entry]; n; n = n->next) {
		struct net_device *dev = n->dev;
		int hatype = dev ? dev->type : 0;
		char tbuf[16];

		sprintf(tbuf, "%u.%u.%u.%u", NIPQUAD(*(u32*)n->key));
		seq_printf(seq, "%-16s 0x%-10x0x%-10x%s"
				"     *        %s\n",
			   tbuf, hatype, ATF_PUBL | ATF_PERM,
			   "00:00:00:00:00:00",
			   dev ? dev->name : "*");
	}
}

static int arp_seq_show(struct seq_file *seq, void *v)
{
	unsigned long l = (unsigned long)v - 1;

	if (!l)
		seq_puts(seq, "IP address       HW type     Flags       "
			      "HW address            Mask     Device\n");

	if (l <= NEIGH_HASHMASK)
		arp_format_neigh_table(seq, l);
	else
		arp_format_pneigh_table(seq, l - NEIGH_HASHMASK);

	return 0;
}

struct seq_operations arp_seq_ops = {
	.start  = arp_seq_start,
	.next   = arp_seq_next,
	.stop   = arp_seq_stop,
	.show   = arp_seq_show,
};

static int arp_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &arp_seq_ops);
}

static struct file_operations arp_seq_fops = {
	.open           = arp_seq_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release	= seq_release,
};

int __init ipv4_proc_init(void)
{
	struct proc_dir_entry *p;
	int rc = 0;

	if (!proc_net_create("raw", 0, raw_get_info))
		goto out_raw;

	if (!proc_net_create("netstat", 0, netstat_get_info))
		goto out_netstat;

	if (!proc_net_create("snmp", 0, snmp_get_info))
		goto out_snmp;

	if (!proc_net_create("sockstat", 0, afinet_get_info))
		goto out_sockstat;

	if (!proc_net_create("tcp", 0, tcp_get_info))
		goto out_tcp;

	if (!proc_net_create("udp", 0, udp_get_info))
		goto out_udp;

	p = create_proc_entry("arp", S_IRUGO, proc_net);
	if (!p)
		goto out_arp;
	p->proc_fops = &arp_seq_fops;
out:
	return rc;
out_arp:
	proc_net_remove("udp");
out_udp:
	proc_net_remove("tcp");
out_tcp:
	proc_net_remove("sockstat");
out_sockstat:
	proc_net_remove("snmp");
out_snmp:
	proc_net_remove("netstat");
out_netstat:
	proc_net_remove("raw");
out_raw:
	rc = -ENOMEM;
	goto out;
}
#else /* CONFIG_PROC_FS */
int __init ipv4_proc_init(void)
{
	return 0;
}
#endif /* CONFIG_PROC_FS */
