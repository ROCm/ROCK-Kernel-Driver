/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		This file implements the various access functions for the
 *		PROC file system.  This is very similar to the IPv4 version,
 *		except it reports the sockets in the INET6 address family.
 *
 * Version:	$Id: proc.c,v 1.17 2002/02/01 22:01:04 davem Exp $
 *
 * Authors:	David S. Miller (davem@caip.rutgers.edu)
 * 		YOSHIFUJI Hideaki <yoshfuji@linux-ipv6.org>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/ipv6.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stddef.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/transp_v6.h>
#include <net/ipv6.h>

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *proc_net_devsnmp6;

static int fold_prot_inuse(struct proto *proto)
{
	int res = 0;
	int cpu;

	for (cpu=0; cpu<NR_CPUS; cpu++)
		res += proto->stats[cpu].inuse;

	return res;
}

static int sockstat6_seq_show(struct seq_file *seq, void *v)
{
	seq_printf(seq, "TCP6: inuse %d\n",
		       fold_prot_inuse(&tcpv6_prot));
	seq_printf(seq, "UDP6: inuse %d\n",
		       fold_prot_inuse(&udpv6_prot));
	seq_printf(seq, "RAW6: inuse %d\n",
		       fold_prot_inuse(&rawv6_prot));
	seq_printf(seq, "FRAG6: inuse %d memory %d\n",
		       ip6_frag_nqueues, atomic_read(&ip6_frag_mem));
	return 0;
}

struct snmp6_item
{
	char *name;
	int   offset;
};
#define SNMP6_SENTINEL	{ .name = NULL, .offset = 0 }

static struct snmp6_item snmp6_ipv6_list[] = {
/* ipv6 mib according to RFC 2465 */
#define SNMP6_GEN(x) { .name = #x , .offset = offsetof(struct ipv6_mib, x) }
	SNMP6_GEN(Ip6InReceives),
	SNMP6_GEN(Ip6InHdrErrors),
	SNMP6_GEN(Ip6InTooBigErrors),
	SNMP6_GEN(Ip6InNoRoutes),
	SNMP6_GEN(Ip6InAddrErrors),
	SNMP6_GEN(Ip6InUnknownProtos),
	SNMP6_GEN(Ip6InTruncatedPkts),
	SNMP6_GEN(Ip6InDiscards),
	SNMP6_GEN(Ip6InDelivers),
	SNMP6_GEN(Ip6OutForwDatagrams),
	SNMP6_GEN(Ip6OutRequests),
	SNMP6_GEN(Ip6OutDiscards),
	SNMP6_GEN(Ip6OutNoRoutes),
	SNMP6_GEN(Ip6ReasmTimeout),
	SNMP6_GEN(Ip6ReasmReqds),
	SNMP6_GEN(Ip6ReasmOKs),
	SNMP6_GEN(Ip6ReasmFails),
	SNMP6_GEN(Ip6FragOKs),
	SNMP6_GEN(Ip6FragFails),
	SNMP6_GEN(Ip6FragCreates),
	SNMP6_GEN(Ip6InMcastPkts),
	SNMP6_GEN(Ip6OutMcastPkts),
#undef SNMP6_GEN
	SNMP6_SENTINEL
};

static struct snmp6_item snmp6_icmp6_list[] = {
/* icmpv6 mib according to RFC 2466

   Exceptions:  {In|Out}AdminProhibs are removed, because I see
                no good reasons to account them separately
		of another dest.unreachs.
		OutErrs is zero identically.
		OutEchos too.
		OutRouterAdvertisements too.
		OutGroupMembQueries too.
 */
#define SNMP6_GEN(x) { .name = #x , .offset = offsetof(struct icmpv6_mib, x) }
	SNMP6_GEN(Icmp6InMsgs),
	SNMP6_GEN(Icmp6InErrors),
	SNMP6_GEN(Icmp6InDestUnreachs),
	SNMP6_GEN(Icmp6InPktTooBigs),
	SNMP6_GEN(Icmp6InTimeExcds),
	SNMP6_GEN(Icmp6InParmProblems),
	SNMP6_GEN(Icmp6InEchos),
	SNMP6_GEN(Icmp6InEchoReplies),
	SNMP6_GEN(Icmp6InGroupMembQueries),
	SNMP6_GEN(Icmp6InGroupMembResponses),
	SNMP6_GEN(Icmp6InGroupMembReductions),
	SNMP6_GEN(Icmp6InRouterSolicits),
	SNMP6_GEN(Icmp6InRouterAdvertisements),
	SNMP6_GEN(Icmp6InNeighborSolicits),
	SNMP6_GEN(Icmp6InNeighborAdvertisements),
	SNMP6_GEN(Icmp6InRedirects),
	SNMP6_GEN(Icmp6OutMsgs),
	SNMP6_GEN(Icmp6OutDestUnreachs),
	SNMP6_GEN(Icmp6OutPktTooBigs),
	SNMP6_GEN(Icmp6OutTimeExcds),
	SNMP6_GEN(Icmp6OutParmProblems),
	SNMP6_GEN(Icmp6OutEchoReplies),
	SNMP6_GEN(Icmp6OutRouterSolicits),
	SNMP6_GEN(Icmp6OutNeighborSolicits),
	SNMP6_GEN(Icmp6OutNeighborAdvertisements),
	SNMP6_GEN(Icmp6OutRedirects),
	SNMP6_GEN(Icmp6OutGroupMembResponses),
	SNMP6_GEN(Icmp6OutGroupMembReductions),
#undef SNMP6_GEN
	SNMP6_SENTINEL
};

static struct snmp6_item snmp6_udp6_list[] = {
#define SNMP6_GEN(x) { .name = "Udp6" #x , .offset = offsetof(struct udp_mib, Udp##x) }
	SNMP6_GEN(InDatagrams),
	SNMP6_GEN(NoPorts),
	SNMP6_GEN(InErrors),
	SNMP6_GEN(OutDatagrams),
#undef SNMP6_GEN
	SNMP6_SENTINEL
};

static unsigned long
fold_field(void *mib[], int offt)
{
        unsigned long res = 0;
        int i;
 
        for (i = 0; i < NR_CPUS; i++) {
                if (!cpu_possible(i))
                        continue;
                res +=
                    *((unsigned long *) (((void *)per_cpu_ptr(mib[0], i)) +
                                         offt));
                res +=
                    *((unsigned long *) (((void *)per_cpu_ptr(mib[1], i)) +
                                         offt));
        }
        return res;
}

static inline void
snmp6_seq_show_item(struct seq_file *seq, void **mib, struct snmp6_item *itemlist)
{
	int i;
	for (i=0; itemlist[i].name; i++)
		seq_printf(seq, "%-32s\t%lu\n", itemlist[i].name, 
				fold_field(mib, itemlist[i].offset));
}

static int snmp6_seq_show(struct seq_file *seq, void *v)
{
	struct inet6_dev *idev = (struct inet6_dev *)seq->private;

	if (idev) {
		seq_printf(seq, "%-32s\t%u\n", "ifIndex", idev->dev->ifindex);
		snmp6_seq_show_item(seq, (void **)idev->stats.icmpv6, snmp6_icmp6_list);
	} else {
		snmp6_seq_show_item(seq, (void **)ipv6_statistics, snmp6_ipv6_list);
		snmp6_seq_show_item(seq, (void **)icmpv6_statistics, snmp6_icmp6_list);
		snmp6_seq_show_item(seq, (void **)udp_stats_in6, snmp6_udp6_list);
	}
	return 0;
}

static int sockstat6_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, sockstat6_seq_show, NULL);
}

static struct file_operations sockstat6_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = sockstat6_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release,
};

static int snmp6_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, snmp6_seq_show, PDE(inode)->data);
}

static struct file_operations snmp6_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = snmp6_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release,
};

int snmp6_register_dev(struct inet6_dev *idev)
{
	int err = -ENOMEM;
	struct proc_dir_entry *p;

	if (!idev || !idev->dev)
		return -EINVAL;

	if (snmp6_mib_init((void **)idev->stats.icmpv6, sizeof(struct icmpv6_mib),
			   __alignof__(struct icmpv6_mib)) < 0)
		goto err_icmp;

	if (!proc_net_devsnmp6) {
		err = -ENOENT;
		goto err_proc;
	}
	p = create_proc_entry(idev->dev->name, S_IRUGO, proc_net_devsnmp6);
	if (!p)
		goto err_proc;
	p->data = idev;
	p->proc_fops = &snmp6_seq_fops;

	idev->stats.proc_dir_entry = p;
	return 0;

err_proc:
	snmp6_mib_free((void **)idev->stats.icmpv6);
err_icmp:
	return err;
}

int snmp6_unregister_dev(struct inet6_dev *idev)
{
	if (!proc_net_devsnmp6)
		return -ENOENT;
	if (!idev || !idev->stats.proc_dir_entry)
		return -EINVAL;
	remove_proc_entry(idev->stats.proc_dir_entry->name,
			  proc_net_devsnmp6);
	snmp6_mib_free((void **)idev->stats.icmpv6);

	return 0;
}

int __init ipv6_misc_proc_init(void)
{
	int rc = 0;

	if (!proc_net_fops_create("snmp6", S_IRUGO, &snmp6_seq_fops))
		goto proc_snmp6_fail;

	proc_net_devsnmp6 = proc_mkdir("dev_snmp6", proc_net);
	if (!proc_net_devsnmp6)
		goto proc_dev_snmp6_fail;

	if (!proc_net_fops_create("sockstat6", S_IRUGO, &sockstat6_seq_fops))
		goto proc_sockstat6_fail;
out:
	return rc;

proc_sockstat6_fail:
	proc_net_remove("dev_snmp6");
proc_dev_snmp6_fail:
	proc_net_remove("snmp6");
proc_snmp6_fail:
	rc = -ENOMEM;
	goto out;
}

void ipv6_misc_proc_exit(void)
{
	proc_net_remove("sockstat6");
	proc_net_remove("dev_snmp6");
	proc_net_remove("snmp6");
}

#else	/* CONFIG_PROC_FS */


int snmp6_register_dev(struct inet6_dev *idev)
{
	int err = -ENOMEM;

	if (!idev || !idev->dev)
		return -EINVAL;

	if (snmp6_mib_init((void **)idev->stats.icmpv6, sizeof(struct icmpv6_mib),
			   __alignof__(struct icmpv6_mib)) < 0)
		goto err_icmp;

	return 0;

err_icmp:
	return err;
}

int snmp6_unregister_dev(struct inet6_dev *idev)
{
	snmp6_mib_free((void **)idev->stats.icmpv6);
	return 0;
}

#endif

