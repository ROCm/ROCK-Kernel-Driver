/*
 *	NET3:	Implementation of the ICMP protocol layer. 
 *	
 *		Alan Cox, <alan@redhat.com>
 *
 *	Version: $Id: icmp.c,v 1.71 2000/08/02 06:01:48 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Some of the function names and the icmp unreach table for this
 *	module were derived from [icmp.c 1.0.11 06/02/93] by
 *	Ross Biro, Fred N. van Kempen, Mark Evans, Alan Cox, Gerhard Koerting.
 *	Other than that this module is a complete rewrite.
 *
 *	Fixes:
 *		Mike Shaver	:	RFC1122 checks.
 *		Alan Cox	:	Multicast ping reply as self.
 *		Alan Cox	:	Fix atomicity lockup in ip_build_xmit 
 *					call.
 *		Alan Cox	:	Added 216,128 byte paths to the MTU 
 *					code.
 *		Martin Mares	:	RFC1812 checks.
 *		Martin Mares	:	Can be configured to follow redirects 
 *					if acting as a router _without_ a
 *					routing protocol (RFC 1812).
 *		Martin Mares	:	Echo requests may be configured to 
 *					be ignored (RFC 1812).
 *		Martin Mares	:	Limitation of ICMP error message 
 *					transmit rate (RFC 1812).
 *		Martin Mares	:	TOS and Precedence set correctly 
 *					(RFC 1812).
 *		Martin Mares	:	Now copying as much data from the 
 *					original packet as we can without
 *					exceeding 576 bytes (RFC 1812).
 *	Willy Konynenberg	:	Transparent proxying support.
 *		Keith Owens	:	RFC1191 correction for 4.2BSD based 
 *					path MTU bug.
 *		Thomas Quinot	:	ICMP Dest Unreach codes up to 15 are
 *					valid (RFC 1812).
 *		Andi Kleen	:	Check all packet lengths properly
 *					and moved all kfree_skb() up to
 *					icmp_rcv.
 *		Andi Kleen	:	Move the rate limit bookkeeping
 *					into the dest entry and use a token
 *					bucket filter (thanks to ANK). Make
 *					the rates sysctl configurable.
 *		Yu Tianli	:	Fixed two ugly bugs in icmp_send
 *					- IP option length was accounted wrongly
 *					- ICMP header length was not accounted at all.
 *              Tristan Greaves :       Added sysctl option to ignore bogus broadcast
 *                                      responses from broken routers.
 *
 * To Fix:
 *
 *	- Should use skb_pull() instead of all the manual checking.
 *	  This would also greatly simply some upper layer error handlers. --AK
 *
 * RFC1122 (Host Requirements -- Comm. Layer) Status:
 * (boy, are there a lot of rules for ICMP)
 *  3.2.2 (Generic ICMP stuff)
 *   MUST discard messages of unknown type. (OK)
 *   MUST copy at least the first 8 bytes from the offending packet
 *     when sending ICMP errors. (OBSOLETE -- see RFC1812)
 *   MUST pass received ICMP errors up to protocol level. (OK)
 *   SHOULD send ICMP errors with TOS == 0. (OBSOLETE -- see RFC1812)
 *   MUST NOT send ICMP errors in reply to:
 *     ICMP errors (OK)
 *     Broadcast/multicast datagrams (OK)
 *     MAC broadcasts (OK)
 *     Non-initial fragments (OK)
 *     Datagram with a source address that isn't a single host. (OK)
 *  3.2.2.1 (Destination Unreachable)
 *   All the rules govern the IP layer, and are dealt with in ip.c, not here.
 *  3.2.2.2 (Redirect)
 *   Host SHOULD NOT send ICMP_REDIRECTs.  (OK)
 *   MUST update routing table in response to host or network redirects.
 *     (host OK, network OBSOLETE)
 *   SHOULD drop redirects if they're not from directly connected gateway
 *     (OK -- we drop it if it's not from our old gateway, which is close
 *      enough)
 * 3.2.2.3 (Source Quench)
 *   MUST pass incoming SOURCE_QUENCHs to transport layer (OK)
 *   Other requirements are dealt with at the transport layer.
 * 3.2.2.4 (Time Exceeded)
 *   MUST pass TIME_EXCEEDED to transport layer (OK)
 *   Other requirements dealt with at IP (generating TIME_EXCEEDED).
 * 3.2.2.5 (Parameter Problem)
 *   SHOULD generate these (OK)
 *   MUST pass received PARAMPROBLEM to transport layer (NOT YET)
 *   	[Solaris 2.X seems to assert EPROTO when this occurs] -- AC
 * 3.2.2.6 (Echo Request/Reply)
 *   MUST reply to ECHO_REQUEST, and give app to do ECHO stuff (OK, OK)
 *   MAY discard broadcast ECHO_REQUESTs. (Configurable with a sysctl.)
 *   MUST reply using same source address as the request was sent to.
 *     We're OK for unicast ECHOs, and it doesn't say anything about
 *     how to handle broadcast ones, since it's optional.
 *   MUST copy data from REQUEST to REPLY (OK)
 *     unless it would require illegal fragmentation (OK)
 *   MUST pass REPLYs to transport/user layer (OK)
 *   MUST use any provided source route (reversed) for REPLY. (NOT YET)
 * 3.2.2.7 (Information Request/Reply)
 *   MUST NOT implement this. (I guess that means silently discard...?) (OK)
 * 3.2.2.8 (Timestamp Request/Reply)
 *   MAY implement (OK)
 *   SHOULD be in-kernel for "minimum variability" (OK)
 *   MAY discard broadcast REQUESTs.  (OK, but see source for inconsistency)
 *   MUST reply using same source address as the request was sent to. (OK)
 *   MUST reverse source route, as per ECHO (NOT YET)
 *   MUST pass REPLYs to transport/user layer (requires RAW, just like 
 *	ECHO) (OK)
 *   MUST update clock for timestamp at least 15 times/sec (OK)
 *   MUST be "correct within a few minutes" (OK)
 * 3.2.2.9 (Address Mask Request/Reply)
 *   MAY implement (OK)
 *   MUST send a broadcast REQUEST if using this system to set netmask
 *     (OK... we don't use it)
 *   MUST discard received REPLYs if not using this system (OK)
 *   MUST NOT send replies unless specifically made agent for this sort
 *     of thing. (OK)
 *
 *
 * RFC 1812 (IPv4 Router Requirements) Status (even longer):
 *  4.3.2.1 (Unknown Message Types)
 *   MUST pass messages of unknown type to ICMP user iface or silently discard
 *     them (OK)
 *  4.3.2.2 (ICMP Message TTL)
 *   MUST initialize TTL when originating an ICMP message (OK)
 *  4.3.2.3 (Original Message Header)
 *   SHOULD copy as much data from the offending packet as possible without
 *     the length of the ICMP datagram exceeding 576 bytes (OK)
 *   MUST leave original IP header of the offending packet, but we're not
 *     required to undo modifications made (OK)
 *  4.3.2.4 (Original Message Source Address)
 *   MUST use one of addresses for the interface the orig. packet arrived as
 *     source address (OK)
 *  4.3.2.5 (TOS and Precedence)
 *   SHOULD leave TOS set to the same value unless the packet would be 
 *     discarded for that reason (OK)
 *   MUST use TOS=0 if not possible to leave original value (OK)
 *   MUST leave IP Precedence for Source Quench messages (OK -- not sent 
 *	at all)
 *   SHOULD use IP Precedence = 6 (Internetwork Control) or 7 (Network Control)
 *     for all other error messages (OK, we use 6)
 *   MAY allow configuration of IP Precedence (OK -- not done)
 *   MUST leave IP Precedence and TOS for reply messages (OK)
 *  4.3.2.6 (Source Route)
 *   SHOULD use reverse source route UNLESS sending Parameter Problem on source
 *     routing and UNLESS the packet would be immediately discarded (NOT YET)
 *  4.3.2.7 (When Not to Send ICMP Errors)
 *   MUST NOT send ICMP errors in reply to:
 *     ICMP errors (OK)
 *     Packets failing IP header validation tests unless otherwise noted (OK)
 *     Broadcast/multicast datagrams (OK)
 *     MAC broadcasts (OK)
 *     Non-initial fragments (OK)
 *     Datagram with a source address that isn't a single host. (OK)
 *  4.3.2.8 (Rate Limiting)
 *   SHOULD be able to limit error message rate (OK)
 *   SHOULD allow setting of rate limits (OK, in the source)
 *  4.3.3.1 (Destination Unreachable)
 *   All the rules govern the IP layer, and are dealt with in ip.c, not here.
 *  4.3.3.2 (Redirect)
 *   MAY ignore ICMP Redirects if running a routing protocol or if forwarding
 *     is enabled on the interface (OK -- ignores)
 *  4.3.3.3 (Source Quench)
 *   SHOULD NOT originate SQ messages (OK)
 *   MUST be able to limit SQ rate if originates them (OK as we don't 
 *	send them)
 *   MAY ignore SQ messages it receives (OK -- we don't)
 *  4.3.3.4 (Time Exceeded)
 *   Requirements dealt with at IP (generating TIME_EXCEEDED).
 *  4.3.3.5 (Parameter Problem)
 *   MUST generate these for all errors not covered by other messages (OK)
 *   MUST include original value of the value pointed by (OK)
 *  4.3.3.6 (Echo Request)
 *   MUST implement echo server function (OK)
 *   MUST process at ER of at least max(576, MTU) (OK)
 *   MAY reject broadcast/multicast ER's (We don't, but that's OK)
 *   SHOULD have a config option for silently ignoring ER's (OK)
 *   MUST have a default value for the above switch = NO (OK)
 *   MUST have application layer interface for Echo Request/Reply (OK)
 *   MUST reply using same source address as the request was sent to.
 *     We're OK for unicast ECHOs, and it doesn't say anything about
 *     how to handle broadcast ones, since it's optional.
 *   MUST copy data from Request to Reply (OK)
 *   SHOULD update Record Route / Timestamp options (??)
 *   MUST use reversed Source Route for Reply if possible (NOT YET)
 *  4.3.3.7 (Information Request/Reply)
 *   SHOULD NOT originate or respond to these (OK)
 *  4.3.3.8 (Timestamp / Timestamp Reply)
 *   MAY implement (OK)
 *   MUST reply to every Timestamp message received (OK)
 *   MAY discard broadcast REQUESTs.  (OK, but see source for inconsistency)
 *   MUST reply using same source address as the request was sent to. (OK)
 *   MUST use reversed Source Route if possible (NOT YET)
 *   SHOULD update Record Route / Timestamp options (??)
 *   MUST pass REPLYs to transport/user layer (requires RAW, just like 
 *	ECHO) (OK)
 *   MUST update clock for timestamp at least 16 times/sec (OK)
 *   MUST be "correct within a few minutes" (OK)
 * 4.3.3.9 (Address Mask Request/Reply)
 *   MUST have support for receiving AMRq and responding with AMRe (OK, 
 *	but only as a compile-time option)
 *   SHOULD have option for each interface for AMRe's, MUST default to 
 *	NO (NOT YET)
 *   MUST NOT reply to AMRq before knows the correct AM (OK)
 *   MUST NOT respond to AMRq with source address 0.0.0.0 on physical
 *    	interfaces having multiple logical i-faces with different masks
 *	(NOT YET)
 *   SHOULD examine all AMRe's it receives and check them (NOT YET)
 *   SHOULD log invalid AMRe's (AM+sender) (NOT YET)
 *   MUST NOT use contents of AMRe to determine correct AM (OK)
 *   MAY broadcast AMRe's after having configured address masks (OK -- doesn't)
 *   MUST NOT do broadcast AMRe's if not set by extra option (OK, no option)
 *   MUST use the { <NetPrefix>, -1 } form of broadcast addresses (OK)
 * 4.3.3.10 (Router Advertisement and Solicitations)
 *   MUST support router part of Router Discovery Protocol on all networks we
 *     support broadcast or multicast addressing. (OK -- done by gated)
 *   MUST have all config parameters with the respective defaults (OK)
 * 5.2.7.1 (Destination Unreachable)
 *   MUST generate DU's (OK)
 *   SHOULD choose a best-match response code (OK)
 *   SHOULD NOT generate Host Isolated codes (OK)
 *   SHOULD use Communication Administratively Prohibited when administratively
 *     filtering packets (NOT YET -- bug-to-bug compatibility)
 *   MAY include config option for not generating the above and silently
 *	discard the packets instead (OK)
 *   MAY include config option for not generating Precedence Violation and
 *     Precedence Cutoff messages (OK as we don't generate them at all)
 *   MUST use Host Unreachable or Dest. Host Unknown codes whenever other hosts
 *     on the same network might be reachable (OK -- no net unreach's at all)
 *   MUST use new form of Fragmentation Needed and DF Set messages (OK)
 * 5.2.7.2 (Redirect)
 *   MUST NOT generate network redirects (OK)
 *   MUST be able to generate host redirects (OK)
 *   SHOULD be able to generate Host+TOS redirects (NO as we don't use TOS)
 *   MUST have an option to use Host redirects instead of Host+TOS ones (OK as
 *     no Host+TOS Redirects are used)
 *   MUST NOT generate redirects unless forwarding to the same i-face and the
 *     dest. address is on the same subnet as the src. address and no source
 *     routing is in use. (OK)
 *   MUST NOT follow redirects when using a routing protocol (OK)
 *   MAY use redirects if not using a routing protocol (OK, compile-time option)
 *   MUST comply to Host Requirements when not acting as a router (OK)
 *  5.2.7.3 (Time Exceeded)
 *   MUST generate Time Exceeded Code 0 when discarding packet due to TTL=0 (OK)
 *   MAY have a per-interface option to disable origination of TE messages, but
 *     it MUST default to "originate" (OK -- we don't support it)
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/netfilter_ipv4.h>
#include <net/snmp.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/protocol.h>
#include <net/icmp.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/raw.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <net/checksum.h>

#define min(a,b)	((a)<(b)?(a):(b))

/*
 *	Statistics
 */
 
struct icmp_mib icmp_statistics[NR_CPUS*2];

/* An array of errno for error messages from dest unreach. */
/* RFC 1122: 3.2.2.1 States that NET_UNREACH, HOS_UNREACH and SR_FAIELD MUST be considered 'transient errs'. */

struct icmp_err icmp_err_convert[] = {
  { ENETUNREACH,	0 },	/*	ICMP_NET_UNREACH	*/
  { EHOSTUNREACH,	0 },	/*	ICMP_HOST_UNREACH	*/
  { ENOPROTOOPT,	1 },	/*	ICMP_PROT_UNREACH	*/
  { ECONNREFUSED,	1 },	/*	ICMP_PORT_UNREACH	*/
  { EMSGSIZE,		0 },	/*	ICMP_FRAG_NEEDED	*/
  { EOPNOTSUPP,		0 },	/*	ICMP_SR_FAILED		*/
  { ENETUNREACH,	1 },	/* 	ICMP_NET_UNKNOWN	*/
  { EHOSTDOWN,		1 },	/*	ICMP_HOST_UNKNOWN	*/
  { ENONET,		1 },	/*	ICMP_HOST_ISOLATED	*/
  { ENETUNREACH,	1 },	/*	ICMP_NET_ANO		*/
  { EHOSTUNREACH,	1 },	/*	ICMP_HOST_ANO		*/
  { ENETUNREACH,	0 },	/*	ICMP_NET_UNR_TOS	*/
  { EHOSTUNREACH,	0 },	/*	ICMP_HOST_UNR_TOS	*/
  { EHOSTUNREACH,	1 },	/*	ICMP_PKT_FILTERED	*/
  { EHOSTUNREACH,	1 },	/*	ICMP_PREC_VIOLATION	*/
  { EHOSTUNREACH,	1 }	/*	ICMP_PREC_CUTOFF	*/
};

/* Control parameters for ECHO relies. */
int sysctl_icmp_echo_ignore_all = 0;
int sysctl_icmp_echo_ignore_broadcasts = 0;

/* Control parameter - ignore bogus broadcast responses? */
int sysctl_icmp_ignore_bogus_error_responses =0;

/*
 *	ICMP control array. This specifies what to do with each ICMP.
 */

struct icmp_control
{
	unsigned long *output;		/* Address to increment on output */
	unsigned long *input;		/* Address to increment on input */
	void (*handler)(struct icmphdr *icmph, struct sk_buff *skb, int len);
	short	error;		/* This ICMP is classed as an error message */
	int *timeout; /* Rate limit */
};

static struct icmp_control icmp_pointers[NR_ICMP_TYPES+1];

/*
 *	The ICMP socket. This is the most convenient way to flow control
 *	our ICMP output as well as maintain a clean interface throughout
 *	all layers. All Socketless IP sends will soon be gone.
 */
	
struct inode icmp_inode;
struct socket *icmp_socket=&icmp_inode.u.socket_i;

/* ICMPv4 socket is only a bit non-reenterable (unlike ICMPv6,
   which is strongly non-reenterable). A bit later it will be made
   reenterable and the lock may be removed then.
 */

static int icmp_xmit_holder = -1;

static int icmp_xmit_lock_bh(void)
{
	if (!spin_trylock(&icmp_socket->sk->lock.slock)) {
		if (icmp_xmit_holder == smp_processor_id())
			return -EAGAIN;
		spin_lock(&icmp_socket->sk->lock.slock);
	}
	icmp_xmit_holder = smp_processor_id();
	return 0;
}

static __inline__ int icmp_xmit_lock(void)
{
	int ret;
	local_bh_disable();
	ret = icmp_xmit_lock_bh();
	if (ret)
		local_bh_enable();
	return ret;
}

static void icmp_xmit_unlock_bh(void)
{
	icmp_xmit_holder = -1;
	spin_unlock(&icmp_socket->sk->lock.slock);
}

static __inline__ void icmp_xmit_unlock(void)
{
	icmp_xmit_unlock_bh();
	local_bh_enable();
}


/*
 *	Send an ICMP frame.
 */

/*
 *	Check transmit rate limitation for given message.
 *	The rate information is held in the destination cache now.
 *	This function is generic and could be used for other purposes
 *	too. It uses a Token bucket filter as suggested by Alexey Kuznetsov.
 *
 *	Note that the same dst_entry fields are modified by functions in 
 *	route.c too, but these work for packet destinations while xrlim_allow
 *	works for icmp destinations. This means the rate limiting information
 *	for one "ip object" is shared.
 *
 *	Note that the same dst_entry fields are modified by functions in 
 *	route.c too, but these work for packet destinations while xrlim_allow
 *	works for icmp destinations. This means the rate limiting information
 *	for one "ip object" is shared - and these ICMPs are twice limited:
 *	by source and by destination.
 *
 *	RFC 1812: 4.3.2.8 SHOULD be able to limit error message rate
 *			  SHOULD allow setting of rate limits 
 *
 * 	Shared between ICMPv4 and ICMPv6.
 */
#define XRLIM_BURST_FACTOR 6
int xrlim_allow(struct dst_entry *dst, int timeout)
{
	unsigned long now;

	now = jiffies;
	dst->rate_tokens += now - dst->rate_last;
	dst->rate_last = now;
	if (dst->rate_tokens > XRLIM_BURST_FACTOR*timeout)
		dst->rate_tokens = XRLIM_BURST_FACTOR*timeout;
	if (dst->rate_tokens >= timeout) {
		dst->rate_tokens -= timeout;
		return 1;
	}
	return 0; 
}

static inline int icmpv4_xrlim_allow(struct rtable *rt, int type, int code)
{
	struct dst_entry *dst = &rt->u.dst; 

	if (type > NR_ICMP_TYPES || !icmp_pointers[type].timeout)
		return 1;

	/* Don't limit PMTU discovery. */
	if (type == ICMP_DEST_UNREACH && code == ICMP_FRAG_NEEDED)
		return 1;

	/* Redirect has its own rate limit mechanism */
	if (type == ICMP_REDIRECT)
		return 1;

	/* No rate limit on loopback */
	if (dst->dev && (dst->dev->flags&IFF_LOOPBACK))
 		return 1;

	return xrlim_allow(dst, *(icmp_pointers[type].timeout));
}

/*
 *	Maintain the counters used in the SNMP statistics for outgoing ICMP
 */
 
static void icmp_out_count(int type)
{
	if (type>NR_ICMP_TYPES)
		return;
	(icmp_pointers[type].output)[(smp_processor_id()*2+!in_softirq())*sizeof(struct icmp_mib)/sizeof(unsigned long)]++;
	ICMP_INC_STATS(IcmpOutMsgs);
}
 
/*
 *	Checksum each fragment, and on the first include the headers and final checksum.
 */
 
static int icmp_glue_bits(const void *p, char *to, unsigned int offset, unsigned int fraglen)
{
	struct icmp_bxm *icmp_param = (struct icmp_bxm *)p;
	struct icmphdr *icmph;
	unsigned long csum;

	if (offset) {
		icmp_param->csum=csum_partial_copy_nocheck(icmp_param->data_ptr+offset-sizeof(struct icmphdr), 
				to, fraglen,icmp_param->csum);
		return 0;
	}

	/*
	 *	First fragment includes header. Note that we've done
	 *	the other fragments first, so that we get the checksum
	 *	for the whole packet here.
	 */
	csum = csum_partial_copy_nocheck((void *)&icmp_param->icmph,
		to, sizeof(struct icmphdr), 
		icmp_param->csum);
	csum = csum_partial_copy_nocheck(icmp_param->data_ptr,
		to+sizeof(struct icmphdr),
		fraglen-sizeof(struct icmphdr), csum);
	icmph=(struct icmphdr *)to;
	icmph->checksum = csum_fold(csum);
	return 0;
}
 
/*
 *	Driving logic for building and sending ICMP messages.
 */

void icmp_reply(struct icmp_bxm *icmp_param, struct sk_buff *skb)
{
	struct sock *sk=icmp_socket->sk;
	struct ipcm_cookie ipc;
	struct rtable *rt = (struct rtable*)skb->dst;
	u32 daddr;

	if (ip_options_echo(&icmp_param->replyopts, skb))
		return;

	if (icmp_xmit_lock_bh())
		return;

	icmp_param->icmph.checksum=0;
	icmp_param->csum=0;
	icmp_out_count(icmp_param->icmph.type);

	sk->protinfo.af_inet.tos = skb->nh.iph->tos;
	daddr = ipc.addr = rt->rt_src;
	ipc.opt = NULL;
	if (icmp_param->replyopts.optlen) {
		ipc.opt = &icmp_param->replyopts;
		if (ipc.opt->srr)
			daddr = icmp_param->replyopts.faddr;
	}
	if (ip_route_output(&rt, daddr, rt->rt_spec_dst, RT_TOS(skb->nh.iph->tos), 0))
		goto out;
	if (icmpv4_xrlim_allow(rt, icmp_param->icmph.type, 
			       icmp_param->icmph.code)) { 
		ip_build_xmit(sk, icmp_glue_bits, icmp_param, 
			      icmp_param->data_len+sizeof(struct icmphdr),
			      &ipc, rt, MSG_DONTWAIT);
	}
	ip_rt_put(rt);
out:
	icmp_xmit_unlock_bh();
}


/*
 *	Send an ICMP message in response to a situation
 *
 *	RFC 1122: 3.2.2	MUST send at least the IP header and 8 bytes of header. MAY send more (we do).
 *			MUST NOT change this header information.
 *			MUST NOT reply to a multicast/broadcast IP address.
 *			MUST NOT reply to a multicast/broadcast MAC address.
 *			MUST reply to only the first fragment.
 */

void icmp_send(struct sk_buff *skb_in, int type, int code, unsigned long info)
{
	struct iphdr *iph;
	struct icmphdr *icmph;
	int room;
	struct icmp_bxm icmp_param;
	struct rtable *rt = (struct rtable*)skb_in->dst;
	struct ipcm_cookie ipc;
	u32 saddr;
	u8  tos;

	if (!rt)
		return;

	/*
	 *	Find the original header
	 */
	iph = skb_in->nh.iph;

	/*
	 *	No replies to physical multicast/broadcast
	 */
	if (skb_in->pkt_type!=PACKET_HOST)
		return;

	/*
	 *	Now check at the protocol level
	 */
	if (rt->rt_flags&(RTCF_BROADCAST|RTCF_MULTICAST))
		return;

	/*
	 *	Only reply to fragment 0. We byte re-order the constant
	 *	mask for efficiency.
	 */
	if (iph->frag_off&htons(IP_OFFSET))
		return;

	/* 
	 *	If we send an ICMP error to an ICMP error a mess would result..
	 */
	if (icmp_pointers[type].error) {
		/*
		 *	We are an error, check if we are replying to an ICMP error
		 */
		if (iph->protocol==IPPROTO_ICMP) {
			icmph = (struct icmphdr *)((char *)iph + (iph->ihl<<2));
			/*
			 *	Assume any unknown ICMP type is an error. This isn't
			 *	specified by the RFC, but think about it..
			 */
			if (icmph->type>NR_ICMP_TYPES || icmp_pointers[icmph->type].error)
				return;
		}
	}


	if (icmp_xmit_lock())
		return;

	/*
	 *	Construct source address and options.
	 */

#ifdef CONFIG_IP_ROUTE_NAT	
	/*
	 *	Restore original addresses if packet has been translated.
	 */
	if (rt->rt_flags&RTCF_NAT && IPCB(skb_in)->flags&IPSKB_TRANSLATED) {
		iph->daddr = rt->key.dst;
		iph->saddr = rt->key.src;
	}
#endif

	saddr = iph->daddr;
	if (!(rt->rt_flags & RTCF_LOCAL))
		saddr = 0;

	tos = icmp_pointers[type].error ?
		((iph->tos & IPTOS_TOS_MASK) | IPTOS_PREC_INTERNETCONTROL) :
			iph->tos;

	/* XXX: use a more aggressive expire for routes created by 
	 * this call (not longer than the rate limit timeout). 
	 * It could be also worthwhile to not put them into ipv4
	 * fast routing cache at first. Otherwise an attacker can
	 * grow the routing table.
	 */
	if (ip_route_output(&rt, iph->saddr, saddr, RT_TOS(tos), 0))
		goto out;

	if (ip_options_echo(&icmp_param.replyopts, skb_in)) 
		goto ende;


	/*
	 *	Prepare data for ICMP header.
	 */

	icmp_param.icmph.type=type;
	icmp_param.icmph.code=code;
	icmp_param.icmph.un.gateway = info;
	icmp_param.icmph.checksum=0;
	icmp_param.csum=0;
	icmp_param.data_ptr=iph;
	icmp_out_count(icmp_param.icmph.type);
	icmp_socket->sk->protinfo.af_inet.tos = tos;
	ipc.addr = iph->saddr;
	ipc.opt = &icmp_param.replyopts;
	if (icmp_param.replyopts.srr) {
		ip_rt_put(rt);
		if (ip_route_output(&rt, icmp_param.replyopts.faddr, saddr, RT_TOS(tos), 0))
			goto out;
	}

	if (!icmpv4_xrlim_allow(rt, type, code))
		goto ende;

	/* RFC says return as much as we can without exceeding 576 bytes. */

	room = rt->u.dst.pmtu;
	if (room > 576)
		room = 576;
	room -= sizeof(struct iphdr) + icmp_param.replyopts.optlen;
	room -= sizeof(struct icmphdr);

	icmp_param.data_len=(skb_in->tail-(u8*)iph);
	if (icmp_param.data_len > room)
		icmp_param.data_len = room;
	
	ip_build_xmit(icmp_socket->sk, icmp_glue_bits, &icmp_param, 
		icmp_param.data_len+sizeof(struct icmphdr),
		&ipc, rt, MSG_DONTWAIT);

ende:
	ip_rt_put(rt);
out:
	icmp_xmit_unlock();
}


/* 
 *	Handle ICMP_DEST_UNREACH, ICMP_TIME_EXCEED, and ICMP_QUENCH. 
 */

static void icmp_unreach(struct icmphdr *icmph, struct sk_buff *skb, int len)
{
	struct iphdr *iph;
	int hash;
	struct inet_protocol *ipprot;
	unsigned char *dp;
	struct sock *raw_sk;
	
	/*
	 *	Incomplete header ?
	 * 	Only checks for the IP header, there should be an
	 *	additional check for longer headers in upper levels.
	 */

	if(len<sizeof(struct iphdr)) {
		ICMP_INC_STATS_BH(IcmpInErrors);
		return;
	}
		
	iph = (struct iphdr *) (icmph + 1);
	dp = (unsigned char*)iph;
	
	if(icmph->type==ICMP_DEST_UNREACH) {
		switch(icmph->code & 15) {
			case ICMP_NET_UNREACH:
				break;
			case ICMP_HOST_UNREACH:
				break;
			case ICMP_PROT_UNREACH:
				break;
			case ICMP_PORT_UNREACH:
				break;
			case ICMP_FRAG_NEEDED:
				if (ipv4_config.no_pmtu_disc) {
					if (net_ratelimit())
						printk(KERN_INFO "ICMP: %u.%u.%u.%u: fragmentation needed and DF set.\n",
						       NIPQUAD(iph->daddr));
				} else {
					unsigned short new_mtu;
					new_mtu = ip_rt_frag_needed(iph, ntohs(icmph->un.frag.mtu));
					if (!new_mtu) 
						return;
					icmph->un.frag.mtu = htons(new_mtu);
				}
				break;
			case ICMP_SR_FAILED:
				if (net_ratelimit())
					printk(KERN_INFO "ICMP: %u.%u.%u.%u: Source Route Failed.\n", NIPQUAD(iph->daddr));
				break;
			default:
				break;
		}
		if (icmph->code>NR_ICMP_UNREACH) 
			return;
	}
	
	/*
	 *	Throw it at our lower layers
	 *
	 *	RFC 1122: 3.2.2 MUST extract the protocol ID from the passed header.
	 *	RFC 1122: 3.2.2.1 MUST pass ICMP unreach messages to the transport layer.
	 *	RFC 1122: 3.2.2.2 MUST pass ICMP time expired messages to transport layer.
	 */
	 
	/*
	 *	Check the other end isnt violating RFC 1122. Some routers send
	 *	bogus responses to broadcast frames. If you see this message
	 *	first check your netmask matches at both ends, if it does then
	 *	get the other vendor to fix their kit.
	 */

	if (!sysctl_icmp_ignore_bogus_error_responses)
	{
	
		if (inet_addr_type(iph->daddr) == RTN_BROADCAST)
		{
			if (net_ratelimit())
				printk(KERN_WARNING "%u.%u.%u.%u sent an invalid ICMP error to a broadcast.\n",
			       	NIPQUAD(skb->nh.iph->saddr));
			return; 
		}
	}

	/*
	 *	Deliver ICMP message to raw sockets. Pretty useless feature?
	 */

	/* Note: See raw.c and net/raw.h, RAWV4_HTABLE_SIZE==MAX_INET_PROTOS */
	hash = iph->protocol & (MAX_INET_PROTOS - 1);
	read_lock(&raw_v4_lock);
	if ((raw_sk = raw_v4_htable[hash]) != NULL) 
	{
		while ((raw_sk = __raw_v4_lookup(raw_sk, iph->protocol, iph->saddr,
						 iph->daddr, skb->dev->ifindex)) != NULL) {
			raw_err(raw_sk, skb);
			raw_sk = raw_sk->next;
		}
	}
	read_unlock(&raw_v4_lock);

	/*
	 *	This can't change while we are doing it. 
	 *	Callers have obtained BR_NETPROTO_LOCK so
	 *	we are OK.
	 */

	ipprot = (struct inet_protocol *) inet_protos[hash];
	while(ipprot != NULL) {
		struct inet_protocol *nextip;

		nextip = (struct inet_protocol *) ipprot->next;
	
		/* 
		 *	Pass it off to everyone who wants it. 
		 */

		/* RFC1122: OK. Passes appropriate ICMP errors to the */
		/* appropriate protocol layer (MUST), as per 3.2.2. */

		if (iph->protocol == ipprot->protocol && ipprot->err_handler)
 			ipprot->err_handler(skb, dp, len);

		ipprot = nextip;
  	}
}


/*
 *	Handle ICMP_REDIRECT. 
 */

static void icmp_redirect(struct icmphdr *icmph, struct sk_buff *skb, int len)
{
	struct iphdr *iph;
	unsigned long ip;

	if (len < sizeof(struct iphdr)) {
		ICMP_INC_STATS_BH(IcmpInErrors);
		return; 
	}
		
	/*
	 *	Get the copied header of the packet that caused the redirect
	 */
	 
	iph = (struct iphdr *) (icmph + 1);
	ip = iph->daddr;

	switch(icmph->code & 7) {
		case ICMP_REDIR_NET:
		case ICMP_REDIR_NETTOS:
			/*
			 *	As per RFC recommendations now handle it as
			 *	a host redirect.
			 */
			 
		case ICMP_REDIR_HOST:
		case ICMP_REDIR_HOSTTOS:
			ip_rt_redirect(skb->nh.iph->saddr, ip, icmph->un.gateway, iph->saddr, iph->tos, skb->dev);
			break;
		default:
			break;
  	}
}

/*
 *	Handle ICMP_ECHO ("ping") requests. 
 *
 *	RFC 1122: 3.2.2.6 MUST have an echo server that answers ICMP echo requests.
 *	RFC 1122: 3.2.2.6 Data received in the ICMP_ECHO request MUST be included in the reply.
 *	RFC 1812: 4.3.3.6 SHOULD have a config option for silently ignoring echo requests, MUST have default=NOT.
 *	See also WRT handling of options once they are done and working.
 */

static void icmp_echo(struct icmphdr *icmph, struct sk_buff *skb, int len)
{
	if (!sysctl_icmp_echo_ignore_all) {
		struct icmp_bxm icmp_param;

		icmp_param.icmph=*icmph;
		icmp_param.icmph.type=ICMP_ECHOREPLY;
		icmp_param.data_ptr=(icmph+1);
		icmp_param.data_len=len;
		icmp_reply(&icmp_param, skb);
	}
}

/*
 *	Handle ICMP Timestamp requests. 
 *	RFC 1122: 3.2.2.8 MAY implement ICMP timestamp requests.
 *		  SHOULD be in the kernel for minimum random latency.
 *		  MUST be accurate to a few minutes.
 *		  MUST be updated at least at 15Hz.
 */
 
static void icmp_timestamp(struct icmphdr *icmph, struct sk_buff *skb, int len)
{
	struct timeval tv;
	__u32 times[3];		/* So the new timestamp works on ALPHA's.. */
	struct icmp_bxm icmp_param;
	
	/*
	 *	Too short.
	 */
	 
	if(len<12) {
		ICMP_INC_STATS_BH(IcmpInErrors);
		return;
	}
	
	/*
	 *	Fill in the current time as ms since midnight UT: 
	 */
	 
	do_gettimeofday(&tv);
	times[1] = htonl((tv.tv_sec % 86400) * 1000 + tv.tv_usec / 1000);
	times[2] = times[1];
	memcpy((void *)&times[0], icmph+1, 4);		/* Incoming stamp */
	icmp_param.icmph=*icmph;
	icmp_param.icmph.type=ICMP_TIMESTAMPREPLY;
	icmp_param.icmph.code=0;
	icmp_param.data_ptr=&times;
	icmp_param.data_len=12;
	icmp_reply(&icmp_param, skb);
}


/* 
 *	Handle ICMP_ADDRESS_MASK requests.  (RFC950)
 *
 * RFC1122 (3.2.2.9).  A host MUST only send replies to 
 * ADDRESS_MASK requests if it's been configured as an address mask 
 * agent.  Receiving a request doesn't constitute implicit permission to 
 * act as one. Of course, implementing this correctly requires (SHOULD) 
 * a way to turn the functionality on and off.  Another one for sysctl(), 
 * I guess. -- MS
 *
 * RFC1812 (4.3.3.9).	A router MUST implement it.
 *			A router SHOULD have switch turning it on/off.
 *		      	This switch MUST be ON by default.
 *
 * Gratuitous replies, zero-source replies are not implemented,
 * that complies with RFC. DO NOT implement them!!! All the idea
 * of broadcast addrmask replies as specified in RFC950 is broken.
 * The problem is that it is not uncommon to have several prefixes
 * on one physical interface. Moreover, addrmask agent can even be
 * not aware of existing another prefixes.
 * If source is zero, addrmask agent cannot choose correct prefix.
 * Gratuitous mask announcements suffer from the same problem.
 * RFC1812 explains it, but still allows to use ADDRMASK,
 * that is pretty silly. --ANK
 *
 * All these rules are so bizarre, that I removed kernel addrmask
 * support at all. It is wrong, it is obsolete, nobody uses it in
 * any case. --ANK
 *
 * Furthermore you can do it with a usermode address agent program
 * anyway...
 */

static void icmp_address(struct icmphdr *icmph, struct sk_buff *skb, int len)
{
#if 0
	if (net_ratelimit())
		printk(KERN_DEBUG "a guy asks for address mask. Who is it?\n");
#endif		
}

/*
 * RFC1812 (4.3.3.9).	A router SHOULD listen all replies, and complain
 *			loudly if an inconsistency is found.
 */

static void icmp_address_reply(struct icmphdr *icmph, struct sk_buff *skb, int len)
{
	struct rtable *rt = (struct rtable*)skb->dst;
	struct net_device *dev = skb->dev;
	struct in_device *in_dev;
	struct in_ifaddr *ifa;
	u32 mask;

	if (len < 4 || !(rt->rt_flags&RTCF_DIRECTSRC))
		return;

	in_dev = in_dev_get(dev);
	if (!in_dev)
		return;
	read_lock(&in_dev->lock);
	if (in_dev->ifa_list &&
	    IN_DEV_LOG_MARTIANS(in_dev) &&
	    IN_DEV_FORWARD(in_dev)) {

		mask = *(u32*)&icmph[1];
		for (ifa=in_dev->ifa_list; ifa; ifa = ifa->ifa_next) {
			if (mask == ifa->ifa_mask && inet_ifa_match(rt->rt_src, ifa))
				break;
		}
		if (!ifa && net_ratelimit()) {
			printk(KERN_INFO "Wrong address mask %u.%u.%u.%u from %s/%u.%u.%u.%u\n",
			       NIPQUAD(mask), dev->name, NIPQUAD(rt->rt_src));
		}
	}
	read_unlock(&in_dev->lock);
	in_dev_put(in_dev);
}

static void icmp_discard(struct icmphdr *icmph, struct sk_buff *skb, int len)
{
}

/* 
 *	Deal with incoming ICMP packets.
 */
 
int icmp_rcv(struct sk_buff *skb, unsigned short len)
{
	struct icmphdr *icmph = skb->h.icmph;
	struct rtable *rt = (struct rtable*)skb->dst;

	ICMP_INC_STATS_BH(IcmpInMsgs);

	/*
	 *	18 is the highest 'known' ICMP type. Anything else is a mystery
	 *
	 *	RFC 1122: 3.2.2  Unknown ICMP messages types MUST be silently discarded.
	 */
	if(len < sizeof(struct icmphdr) ||
	   ip_compute_csum((unsigned char *) icmph, len) ||
	   icmph->type > NR_ICMP_TYPES)
		goto error;
	 
	/*
	 *	Parse the ICMP message 
	 */

 	if (rt->rt_flags&(RTCF_BROADCAST|RTCF_MULTICAST)) {
		/*
		 *	RFC 1122: 3.2.2.6 An ICMP_ECHO to broadcast MAY be
		 *	  silently ignored (we let user decide with a sysctl).
		 *	RFC 1122: 3.2.2.8 An ICMP_TIMESTAMP MAY be silently
		 *	  discarded if to broadcast/multicast.
		 */
		if (icmph->type == ICMP_ECHO &&
		    sysctl_icmp_echo_ignore_broadcasts) {
			goto error;
		}
		if (icmph->type != ICMP_ECHO &&
		    icmph->type != ICMP_TIMESTAMP &&
		    icmph->type != ICMP_ADDRESS &&
		    icmph->type != ICMP_ADDRESSREPLY) {
			goto error;
  		}
	}

	len -= sizeof(struct icmphdr);
	icmp_pointers[icmph->type].input[smp_processor_id()*2*sizeof(struct icmp_mib)/sizeof(unsigned long)]++;
	(icmp_pointers[icmph->type].handler)(icmph, skb, len);

drop:
	kfree_skb(skb);
	return 0;
error:
	ICMP_INC_STATS_BH(IcmpInErrors);
	goto drop;
}

/*
 *	A spare long used to speed up statistics updating
 */
 
static unsigned long dummy;

/* 
 * 	Configurable rate limits.
 *	Someone should check if these default values are correct.
 *	Note that these values interact with the routing cache GC timeout.
 *	If you chose them too high they won't take effect, because the
 *	dst_entry gets expired too early. The same should happen when
 *	the cache grows too big.
 */
int sysctl_icmp_destunreach_time = 1*HZ;
int sysctl_icmp_timeexceed_time = 1*HZ;
int sysctl_icmp_paramprob_time = 1*HZ;
int sysctl_icmp_echoreply_time = 0; /* don't limit it per default. */

/*
 *	This table is the definition of how we handle ICMP.
 */
 
static struct icmp_control icmp_pointers[NR_ICMP_TYPES+1] = {
/* ECHO REPLY (0) */
 { &icmp_statistics[0].IcmpOutEchoReps, &icmp_statistics[0].IcmpInEchoReps, icmp_discard, 0, &sysctl_icmp_echoreply_time},
 { &dummy, &icmp_statistics[0].IcmpInErrors, icmp_discard, 1, },
 { &dummy, &icmp_statistics[0].IcmpInErrors, icmp_discard, 1, },
/* DEST UNREACH (3) */
 { &icmp_statistics[0].IcmpOutDestUnreachs, &icmp_statistics[0].IcmpInDestUnreachs, icmp_unreach, 1, &sysctl_icmp_destunreach_time },
/* SOURCE QUENCH (4) */
 { &icmp_statistics[0].IcmpOutSrcQuenchs, &icmp_statistics[0].IcmpInSrcQuenchs, icmp_unreach, 1, },
/* REDIRECT (5) */
 { &icmp_statistics[0].IcmpOutRedirects, &icmp_statistics[0].IcmpInRedirects, icmp_redirect, 1, },
 { &dummy, &icmp_statistics[0].IcmpInErrors, icmp_discard, 1, },
 { &dummy, &icmp_statistics[0].IcmpInErrors, icmp_discard, 1, },
/* ECHO (8) */
 { &icmp_statistics[0].IcmpOutEchos, &icmp_statistics[0].IcmpInEchos, icmp_echo, 0, },
 { &dummy, &icmp_statistics[0].IcmpInErrors, icmp_discard, 1, },
 { &dummy, &icmp_statistics[0].IcmpInErrors, icmp_discard, 1, },
/* TIME EXCEEDED (11) */
 { &icmp_statistics[0].IcmpOutTimeExcds, &icmp_statistics[0].IcmpInTimeExcds, icmp_unreach, 1, &sysctl_icmp_timeexceed_time },
/* PARAMETER PROBLEM (12) */
 { &icmp_statistics[0].IcmpOutParmProbs, &icmp_statistics[0].IcmpInParmProbs, icmp_unreach, 1, &sysctl_icmp_paramprob_time },
/* TIMESTAMP (13) */
 { &icmp_statistics[0].IcmpOutTimestamps, &icmp_statistics[0].IcmpInTimestamps, icmp_timestamp, 0,  },
/* TIMESTAMP REPLY (14) */
 { &icmp_statistics[0].IcmpOutTimestampReps, &icmp_statistics[0].IcmpInTimestampReps, icmp_discard, 0, },
/* INFO (15) */
 { &dummy, &dummy, icmp_discard, 0, },
/* INFO REPLY (16) */
 { &dummy, &dummy, icmp_discard, 0, },
/* ADDR MASK (17) */
 { &icmp_statistics[0].IcmpOutAddrMasks, &icmp_statistics[0].IcmpInAddrMasks, icmp_address, 0,  },
/* ADDR MASK REPLY (18) */
 { &icmp_statistics[0].IcmpOutAddrMaskReps, &icmp_statistics[0].IcmpInAddrMaskReps, icmp_address_reply, 0, }
};

void __init icmp_init(struct net_proto_family *ops)
{
	int err;

	icmp_inode.i_mode = S_IFSOCK;
	icmp_inode.i_sock = 1;
	icmp_inode.i_uid = 0;
	icmp_inode.i_gid = 0;
	init_waitqueue_head(&icmp_inode.i_wait);
	init_waitqueue_head(&icmp_inode.u.socket_i.wait);

	icmp_socket->inode = &icmp_inode;
	icmp_socket->state = SS_UNCONNECTED;
	icmp_socket->type=SOCK_RAW;

	if ((err=ops->create(icmp_socket, IPPROTO_ICMP))<0)
		panic("Failed to create the ICMP control socket.\n");
	icmp_socket->sk->allocation=GFP_ATOMIC;
	icmp_socket->sk->sndbuf = SK_WMEM_MAX*2;
	icmp_socket->sk->protinfo.af_inet.ttl = MAXTTL;

	/* Unhash it so that IP input processing does not even
	 * see it, we do not wish this socket to see incoming
	 * packets.
	 */
	icmp_socket->sk->prot->unhash(icmp_socket->sk);
}
