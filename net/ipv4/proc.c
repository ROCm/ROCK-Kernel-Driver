/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		This file implements the various access functions for the
 *		PROC file system.  It is mainly used for debugging and
 *		statistics.
 *
 * Version:	$Id: proc.c,v 1.45 2001/05/16 16:45:35 davem Exp $
 *
 * Authors:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Gerald J. Heim, <heim@peanuts.informatik.uni-tuebingen.de>
 *		Fred Baumgarten, <dc6iq@insu1.etec.uni-karlsruhe.de>
 *		Erik Schoenfelder, <schoenfr@ibr.cs.tu-bs.de>
 *
 * Fixes:
 *		Alan Cox	:	UDP sockets show the rxqueue/txqueue
 *					using hint flag for the netinfo.
 *	Pauline Middelink	:	identd support
 *		Alan Cox	:	Make /proc safer.
 *	Erik Schoenfelder	:	/proc/net/snmp
 *		Alan Cox	:	Handle dead sockets properly.
 *	Gerhard Koerting	:	Show both timers
 *		Alan Cox	:	Allow inode to be NULL (kernel socket)
 *	Andi Kleen		:	Add support for open_requests and 
 *					split functions for more readibility.
 *	Andi Kleen		:	Add support for /proc/net/netstat
 *	Arnaldo C. Melo		:	Convert to seq_file
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <asm/system.h>
#include <linux/sched.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/un.h>
#include <linux/in.h>
#include <linux/param.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/sock.h>
#include <net/raw.h>

static int fold_prot_inuse(struct proto *proto)
{
	int res = 0;
	int cpu;

	for (cpu=0; cpu<NR_CPUS; cpu++)
		res += proto->stats[cpu].inuse;

	return res;
}

/*
 *	Report socket allocation statistics [mea@utu.fi]
 */
int afinet_get_info(char *buffer, char **start, off_t offset, int length)
{
	/* From  net/socket.c  */
	extern int socket_get_info(char *, char **, off_t, int);

	int len  = socket_get_info(buffer,start,offset,length);

	len += sprintf(buffer+len,"TCP: inuse %d orphan %d tw %d alloc %d mem %d\n",
		       fold_prot_inuse(&tcp_prot),
		       atomic_read(&tcp_orphan_count), tcp_tw_count,
		       atomic_read(&tcp_sockets_allocated),
		       atomic_read(&tcp_memory_allocated));
	len += sprintf(buffer+len,"UDP: inuse %d\n",
		       fold_prot_inuse(&udp_prot));
	len += sprintf(buffer+len,"RAW: inuse %d\n",
		       fold_prot_inuse(&raw_prot));
	len += sprintf(buffer+len, "FRAG: inuse %d memory %d\n",
		       ip_frag_nqueues, atomic_read(&ip_frag_mem));
	if (offset >= len)
	{
		*start = buffer;
		return 0;
	}
	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	if (len < 0)
		len = 0;
	return len;
}

static unsigned long fold_field(unsigned long *begin, int sz, int nr)
{
	unsigned long res = 0;
	int i;

	sz /= sizeof(unsigned long);

	for (i=0; i<NR_CPUS; i++) {
		res += begin[2*i*sz + nr];
		res += begin[(2*i+1)*sz + nr];
	}
	return res;
}

/* 
 *	Called from the PROCfs module. This outputs /proc/net/snmp.
 */
static int snmp_seq_show(struct seq_file *seq, void *v)
{
	extern int sysctl_ip_default_ttl;
	int i;

	seq_printf(seq, "Ip: Forwarding DefaultTTL InReceives InHdrErrors "
			"InAddrErrors ForwDatagrams InUnknownProtos "
			"InDiscards InDelivers OutRequests OutDiscards "
			"OutNoRoutes ReasmTimeout ReasmReqds ReasmOKs "
			"ReasmFails FragOKs FragFails FragCreates\nIp: %d %d",
			ipv4_devconf.forwarding ? 1 : 2, sysctl_ip_default_ttl);

	for (i = 0;
	     i < offsetof(struct ip_mib, __pad) / sizeof(unsigned long); i++)
		seq_printf(seq, " %lu",
			   fold_field((unsigned long *)ip_statistics,
				      sizeof(struct ip_mib), i));

	seq_printf(seq, "\nIcmp: InMsgs InErrors InDestUnreachs InTimeExcds "
			"InParmProbs InSrcQuenchs InRedirects InEchos "
			"InEchoReps InTimestamps InTimestampReps InAddrMasks "
			"InAddrMaskReps OutMsgs OutErrors OutDestUnreachs "
			"OutTimeExcds OutParmProbs OutSrcQuenchs OutRedirects "
			"OutEchos OutEchoReps OutTimestamps OutTimestampReps "
			"OutAddrMasks OutAddrMaskReps\nIcmp:");

	for (i = 0;
	     i < offsetof(struct icmp_mib, dummy) / sizeof(unsigned long); i++)
		seq_printf(seq, " %lu",
			   fold_field((unsigned long *)icmp_statistics,
				      sizeof(struct icmp_mib), i));

	seq_printf(seq, "\nTcp: RtoAlgorithm RtoMin RtoMax MaxConn ActiveOpens "
			"PassiveOpens AttemptFails EstabResets CurrEstab "
			"InSegs OutSegs RetransSegs InErrs OutRsts\nTcp:");

	for (i = 0;
	     i < offsetof(struct tcp_mib, __pad) / sizeof(unsigned long); i++)
		seq_printf(seq, " %lu",
			   fold_field((unsigned long *)tcp_statistics,
				      sizeof(struct tcp_mib), i));

	seq_printf(seq, "\nUdp: InDatagrams NoPorts InErrors OutDatagrams\n"
			"Udp:");
	
	for (i = 0;
	     i < offsetof(struct udp_mib, __pad) / sizeof(unsigned long); i++)
		seq_printf(seq, " %lu",
			   fold_field((unsigned long *)udp_statistics,
				      sizeof(struct udp_mib), i));

	seq_putc(seq, '\n');
	return 0;
}

static int snmp_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, snmp_seq_show, NULL);
}

static struct file_operations snmp_seq_fops = {
	.open	 = snmp_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release,
};

/* 
 *	Output /proc/net/netstat
 */
static int netstat_seq_show(struct seq_file *seq, void *v)
{
	int i;

	seq_puts(seq, "TcpExt: SyncookiesSent SyncookiesRecv SyncookiesFailed"
		      " EmbryonicRsts PruneCalled RcvPruned OfoPruned"
		      " OutOfWindowIcmps LockDroppedIcmps ArpFilter"
		      " TW TWRecycled TWKilled"
		      " PAWSPassive PAWSActive PAWSEstab"
		      " DelayedACKs DelayedACKLocked DelayedACKLost"
		      " ListenOverflows ListenDrops"
		      " TCPPrequeued TCPDirectCopyFromBacklog"
		      " TCPDirectCopyFromPrequeue TCPPrequeueDropped"
		      " TCPHPHits TCPHPHitsToUser"
		      " TCPPureAcks TCPHPAcks"
		      " TCPRenoRecovery TCPSackRecovery"
		      " TCPSACKReneging"
		      " TCPFACKReorder TCPSACKReorder TCPRenoReorder"
		      " TCPTSReorder"
		      " TCPFullUndo TCPPartialUndo TCPDSACKUndo TCPLossUndo"
		      " TCPLoss TCPLostRetransmit"
		      " TCPRenoFailures TCPSackFailures TCPLossFailures"
		      " TCPFastRetrans TCPForwardRetrans TCPSlowStartRetrans"
		      " TCPTimeouts"
		      " TCPRenoRecoveryFail TCPSackRecoveryFail"
		      " TCPSchedulerFailed TCPRcvCollapsed"
		      " TCPDSACKOldSent TCPDSACKOfoSent TCPDSACKRecv"
		      " TCPDSACKOfoRecv"
		      " TCPAbortOnSyn TCPAbortOnData TCPAbortOnClose"
		      " TCPAbortOnMemory TCPAbortOnTimeout TCPAbortOnLinger"
		      " TCPAbortFailed TCPMemoryPressures\n"
		      "TcpExt:");
	for (i = 0;
	     i < offsetof(struct linux_mib, __pad) / sizeof(unsigned long); i++)
		seq_printf(seq, " %lu",
			   fold_field((unsigned long *)net_statistics,
				      sizeof(struct linux_mib), i));
	seq_putc(seq, '\n');
	return 0;
}

static int netstat_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, netstat_seq_show, NULL);
}

static struct file_operations netstat_seq_fops = {
	.open	 = netstat_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release,
};

int __init ip_misc_proc_init(void)
{
	int rc = 0;
	struct proc_dir_entry *p;
		
	p = create_proc_entry("netstat", S_IRUGO, proc_net);
	if (!p)
		goto out_netstat;
	p->proc_fops = &netstat_seq_fops;

	p = create_proc_entry("snmp", S_IRUGO, proc_net);
	if (!p)
		goto out_snmp;
	p->proc_fops = &snmp_seq_fops;

	if (!proc_net_create("sockstat", 0, afinet_get_info))
		goto out_sockstat;
out:
	return rc;
out_sockstat:
	remove_proc_entry("snmp", proc_net);
out_snmp:
	remove_proc_entry("netstat", proc_net);
out_netstat:
	rc = -ENOMEM;
	goto out;
}

int ip_seq_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = (struct seq_file *)file->private_data;

	kfree(seq->private);
	seq->private = NULL;
	return seq_release(inode, file);
}
