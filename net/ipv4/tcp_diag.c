/*
 * tcp_diag.c	Module for monitoring TCP sockets.
 *
 * Version:	$Id: tcp_diag.c,v 1.3 2002/02/01 22:01:04 davem Exp $
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/random.h>
#include <linux/cache.h>
#include <linux/init.h>

#include <net/icmp.h>
#include <net/tcp.h>
#include <net/ipv6.h>
#include <net/inet_common.h>

#include <linux/inet.h>
#include <linux/stddef.h>

#include <linux/tcp_diag.h>

static struct sock *tcpnl;


#define TCPDIAG_PUT(skb, attrtype, attrlen) \
({ int rtalen = RTA_LENGTH(attrlen);        \
   struct rtattr *rta;                      \
   if (skb_tailroom(skb) < RTA_ALIGN(rtalen)) goto nlmsg_failure; \
   rta = (void*)__skb_put(skb, RTA_ALIGN(rtalen)); \
   rta->rta_type = attrtype;                \
   rta->rta_len = rtalen;                   \
   RTA_DATA(rta); })

static int tcpdiag_fill(struct sk_buff *skb, struct sock *sk,
			int ext, u32 pid, u32 seq)
{
	struct inet_opt *inet = inet_sk(sk);
	struct tcp_opt *tp = tcp_sk(sk);
	struct tcpdiagmsg *r;
	struct nlmsghdr  *nlh;
	struct tcp_info  *info = NULL;
	struct tcpdiag_meminfo  *minfo = NULL;
	struct tcpvegas_info *vinfo = NULL;
	unsigned char	 *b = skb->tail;

	nlh = NLMSG_PUT(skb, pid, seq, TCPDIAG_GETSOCK, sizeof(*r));
	r = NLMSG_DATA(nlh);
	if (sk->sk_state != TCP_TIME_WAIT) {
		if (ext & (1<<(TCPDIAG_MEMINFO-1)))
			minfo = TCPDIAG_PUT(skb, TCPDIAG_MEMINFO, sizeof(*minfo));
		if (ext & (1<<(TCPDIAG_INFO-1)))
			info = TCPDIAG_PUT(skb, TCPDIAG_INFO, sizeof(*info));
		
		if (tcp_is_vegas(tp) && (ext & (1<<(TCPDIAG_VEGASINFO-1))))
			vinfo = TCPDIAG_PUT(skb, TCPDIAG_VEGASINFO, sizeof(*vinfo));
	}
	r->tcpdiag_family = sk->sk_family;
	r->tcpdiag_state = sk->sk_state;
	r->tcpdiag_timer = 0;
	r->tcpdiag_retrans = 0;

	r->id.tcpdiag_if = sk->sk_bound_dev_if;
	r->id.tcpdiag_cookie[0] = (u32)(unsigned long)sk;
	r->id.tcpdiag_cookie[1] = (u32)(((unsigned long)sk >> 31) >> 1);

	if (r->tcpdiag_state == TCP_TIME_WAIT) {
		struct tcp_tw_bucket *tw = (struct tcp_tw_bucket*)sk;
		long tmo = tw->tw_ttd - jiffies;
		if (tmo < 0)
			tmo = 0;

		r->id.tcpdiag_sport = tw->tw_sport;
		r->id.tcpdiag_dport = tw->tw_dport;
		r->id.tcpdiag_src[0] = tw->tw_rcv_saddr;
		r->id.tcpdiag_dst[0] = tw->tw_daddr;
		r->tcpdiag_state = tw->tw_substate;
		r->tcpdiag_timer = 3;
		r->tcpdiag_expires = (tmo*1000+HZ-1)/HZ;
		r->tcpdiag_rqueue = 0;
		r->tcpdiag_wqueue = 0;
		r->tcpdiag_uid = 0;
		r->tcpdiag_inode = 0;
#ifdef CONFIG_IPV6
		if (r->tcpdiag_family == AF_INET6) {
			ipv6_addr_copy((struct in6_addr *)r->id.tcpdiag_src,
				       &tw->tw_v6_rcv_saddr);
			ipv6_addr_copy((struct in6_addr *)r->id.tcpdiag_dst,
				       &tw->tw_v6_daddr);
		}
#endif
		nlh->nlmsg_len = skb->tail - b;
		return skb->len;
	}

	r->id.tcpdiag_sport = inet->sport;
	r->id.tcpdiag_dport = inet->dport;
	r->id.tcpdiag_src[0] = inet->rcv_saddr;
	r->id.tcpdiag_dst[0] = inet->daddr;

#ifdef CONFIG_IPV6
	if (r->tcpdiag_family == AF_INET6) {
		struct ipv6_pinfo *np = inet6_sk(sk);

		ipv6_addr_copy((struct in6_addr *)r->id.tcpdiag_src,
			       &np->rcv_saddr);
		ipv6_addr_copy((struct in6_addr *)r->id.tcpdiag_dst,
			       &np->daddr);
	}
#endif

#define EXPIRES_IN_MS(tmo)  ((tmo-jiffies)*1000+HZ-1)/HZ

	if (tp->pending == TCP_TIME_RETRANS) {
		r->tcpdiag_timer = 1;
		r->tcpdiag_retrans = tp->retransmits;
		r->tcpdiag_expires = EXPIRES_IN_MS(tp->timeout);
	} else if (tp->pending == TCP_TIME_PROBE0) {
		r->tcpdiag_timer = 4;
		r->tcpdiag_retrans = tp->probes_out;
		r->tcpdiag_expires = EXPIRES_IN_MS(tp->timeout);
	} else if (timer_pending(&sk->sk_timer)) {
		r->tcpdiag_timer = 2;
		r->tcpdiag_retrans = tp->probes_out;
		r->tcpdiag_expires = EXPIRES_IN_MS(sk->sk_timer.expires);
	} else {
		r->tcpdiag_timer = 0;
		r->tcpdiag_expires = 0;
	}
#undef EXPIRES_IN_MS

	r->tcpdiag_rqueue = tp->rcv_nxt - tp->copied_seq;
	r->tcpdiag_wqueue = tp->write_seq - tp->snd_una;
	r->tcpdiag_uid = sock_i_uid(sk);
	r->tcpdiag_inode = sock_i_ino(sk);

	if (minfo) {
		minfo->tcpdiag_rmem = atomic_read(&sk->sk_rmem_alloc);
		minfo->tcpdiag_wmem = sk->sk_wmem_queued;
		minfo->tcpdiag_fmem = sk->sk_forward_alloc;
		minfo->tcpdiag_tmem = atomic_read(&sk->sk_wmem_alloc);
	}

	if (info) {
		u32 now = tcp_time_stamp;

		info->tcpi_state = sk->sk_state;
		info->tcpi_ca_state = tp->ca_state;
		info->tcpi_retransmits = tp->retransmits;
		info->tcpi_probes = tp->probes_out;
		info->tcpi_backoff = tp->backoff;
		info->tcpi_options = 0;
		if (tp->tstamp_ok)
			info->tcpi_options |= TCPI_OPT_TIMESTAMPS;
		if (tp->sack_ok)
			info->tcpi_options |= TCPI_OPT_SACK;
		if (tp->wscale_ok) {
			info->tcpi_options |= TCPI_OPT_WSCALE;
			info->tcpi_snd_wscale = tp->snd_wscale;
			info->tcpi_rcv_wscale = tp->rcv_wscale;
		} else {
			info->tcpi_snd_wscale = 0;
			info->tcpi_rcv_wscale = 0;
		}
		if (tp->ecn_flags&TCP_ECN_OK)
			info->tcpi_options |= TCPI_OPT_ECN;

		info->tcpi_rto = (1000000*tp->rto)/HZ;
		info->tcpi_ato = (1000000*tp->ack.ato)/HZ;
		info->tcpi_snd_mss = tp->mss_cache;
		info->tcpi_rcv_mss = tp->ack.rcv_mss;

		info->tcpi_unacked = tp->packets_out;
		info->tcpi_sacked = tp->sacked_out;
		info->tcpi_lost = tp->lost_out;
		info->tcpi_retrans = tp->retrans_out;
		info->tcpi_fackets = tp->fackets_out;

		info->tcpi_last_data_sent = ((now - tp->lsndtime)*1000)/HZ;
		info->tcpi_last_ack_sent = 0;
		info->tcpi_last_data_recv = ((now - tp->ack.lrcvtime)*1000)/HZ;
		info->tcpi_last_ack_recv = ((now - tp->rcv_tstamp)*1000)/HZ;

		info->tcpi_pmtu = tp->pmtu_cookie;
		info->tcpi_rcv_ssthresh = tp->rcv_ssthresh;
		info->tcpi_rtt = ((1000000*tp->srtt)/HZ)>>3;
		info->tcpi_rttvar = ((1000000*tp->mdev)/HZ)>>2;
		info->tcpi_snd_ssthresh = tp->snd_ssthresh;
		info->tcpi_snd_cwnd = tp->snd_cwnd;
		info->tcpi_advmss = tp->advmss;
		info->tcpi_reordering = tp->reordering;
	}

	if (vinfo) {
		vinfo->tcpv_enabled = tp->vegas.doing_vegas_now;
		vinfo->tcpv_rttcnt = tp->vegas.cntRTT;
		vinfo->tcpv_rtt = tp->vegas.baseRTT;
		vinfo->tcpv_minrtt = tp->vegas.minRTT;
	}

	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

extern struct sock *tcp_v4_lookup(u32 saddr, u16 sport, u32 daddr, u16 dport, int dif);
#ifdef CONFIG_IPV6
extern struct sock *tcp_v6_lookup(struct in6_addr *saddr, u16 sport,
				  struct in6_addr *daddr, u16 dport,
				  int dif);
#endif

static int tcpdiag_get_exact(struct sk_buff *in_skb, const struct nlmsghdr *nlh)
{
	int err;
	struct sock *sk;
	struct tcpdiagreq *req = NLMSG_DATA(nlh);
	struct sk_buff *rep;

	if (req->tcpdiag_family == AF_INET) {
		sk = tcp_v4_lookup(req->id.tcpdiag_dst[0], req->id.tcpdiag_dport,
				   req->id.tcpdiag_src[0], req->id.tcpdiag_sport,
				   req->id.tcpdiag_if);
	}
#ifdef CONFIG_IPV6
	else if (req->tcpdiag_family == AF_INET6) {
		sk = tcp_v6_lookup((struct in6_addr*)req->id.tcpdiag_dst, req->id.tcpdiag_dport,
				   (struct in6_addr*)req->id.tcpdiag_src, req->id.tcpdiag_sport,
				   req->id.tcpdiag_if);
	}
#endif
	else {
		return -EINVAL;
	}

	if (sk == NULL)
		return -ENOENT;

	err = -ESTALE;
	if ((req->id.tcpdiag_cookie[0] != TCPDIAG_NOCOOKIE ||
	     req->id.tcpdiag_cookie[1] != TCPDIAG_NOCOOKIE) &&
	    ((u32)(unsigned long)sk != req->id.tcpdiag_cookie[0] ||
	     (u32)((((unsigned long)sk) >> 31) >> 1) != req->id.tcpdiag_cookie[1]))
		goto out;

	err = -ENOMEM;
	rep = alloc_skb(NLMSG_SPACE(sizeof(struct tcpdiagmsg)+
				    sizeof(struct tcpdiag_meminfo)+
				    sizeof(struct tcp_info)+64), GFP_KERNEL);
	if (!rep)
		goto out;

	if (tcpdiag_fill(rep, sk, req->tcpdiag_ext,
			 NETLINK_CB(in_skb).pid,
			 nlh->nlmsg_seq) <= 0)
		BUG();

	err = netlink_unicast(tcpnl, rep, NETLINK_CB(in_skb).pid, MSG_DONTWAIT);
	if (err > 0)
		err = 0;

out:
	if (sk) {
		if (sk->sk_state == TCP_TIME_WAIT)
			tcp_tw_put((struct tcp_tw_bucket*)sk);
		else
			sock_put(sk);
	}
	return err;
}

static int bitstring_match(const u32 *a1, const u32 *a2, int bits)
{
	int words = bits >> 5;

	bits &= 0x1f;

	if (words) {
		if (memcmp(a1, a2, words << 2))
			return 0;
	}
	if (bits) {
		__u32 w1, w2;
		__u32 mask;

		w1 = a1[words];
		w2 = a2[words];

		mask = htonl((0xffffffff) << (32 - bits));

		if ((w1 ^ w2) & mask)
			return 0;
	}

	return 1;
}


static int tcpdiag_bc_run(const void *bc, int len, struct sock *sk)
{
	while (len > 0) {
		int yes = 1;
		struct inet_opt *inet = inet_sk(sk);
		const struct tcpdiag_bc_op *op = bc;

		switch (op->code) {
		case TCPDIAG_BC_NOP:
			break;
		case TCPDIAG_BC_JMP:
			yes = 0;
			break;
		case TCPDIAG_BC_S_GE:
			yes = inet->num >= op[1].no;
			break;
		case TCPDIAG_BC_S_LE:
			yes = inet->num <= op[1].no;
			break;
		case TCPDIAG_BC_D_GE:
			yes = ntohs(inet->dport) >= op[1].no;
			break;
		case TCPDIAG_BC_D_LE:
			yes = ntohs(inet->dport) <= op[1].no;
			break;
		case TCPDIAG_BC_AUTO:
			yes = !(sk->sk_userlocks & SOCK_BINDPORT_LOCK);
			break;
		case TCPDIAG_BC_S_COND:
		case TCPDIAG_BC_D_COND:
		{
			struct tcpdiag_hostcond *cond = (struct tcpdiag_hostcond*)(op+1);
			u32 *addr;

			if (cond->port != -1 &&
			    cond->port != (op->code == TCPDIAG_BC_S_COND ?
					     inet->num : ntohs(inet->dport))) {
				yes = 0;
				break;
			}
			
			if (cond->prefix_len == 0)
				break;

#ifdef CONFIG_IPV6
			if (sk->sk_family == AF_INET6) {
				struct ipv6_pinfo *np = inet6_sk(sk);

				if (op->code == TCPDIAG_BC_S_COND)
					addr = (u32*)&np->rcv_saddr;
				else
					addr = (u32*)&np->daddr;
			} else
#endif
			{
				if (op->code == TCPDIAG_BC_S_COND)
					addr = &inet->rcv_saddr;
				else
					addr = &inet->daddr;
			}

			if (bitstring_match(addr, cond->addr, cond->prefix_len))
				break;
			if (sk->sk_family == AF_INET6 &&
			    cond->family == AF_INET) {
				if (addr[0] == 0 && addr[1] == 0 &&
				    addr[2] == htonl(0xffff) &&
				    bitstring_match(addr+3, cond->addr, cond->prefix_len))
					break;
			}
			yes = 0;
			break;
		}
		}

		if (yes) { 
			len -= op->yes;
			bc += op->yes;
		} else {
			len -= op->no;
			bc += op->no;
		}
	}
	return (len == 0);
}

static int valid_cc(const void *bc, int len, int cc)
{
	while (len >= 0) {
		const struct tcpdiag_bc_op *op = bc;

		if (cc > len)
			return 0;
		if (cc == len)
			return 1;
		if (op->yes < 4)
			return 0;
		len -= op->yes;
		bc  += op->yes;
	}
	return 0;
}

static int tcpdiag_bc_audit(const void *bytecode, int bytecode_len)
{
	const unsigned char *bc = bytecode;
	int  len = bytecode_len;

	while (len > 0) {
		struct tcpdiag_bc_op *op = (struct tcpdiag_bc_op*)bc;

//printk("BC: %d %d %d {%d} / %d\n", op->code, op->yes, op->no, op[1].no, len);
		switch (op->code) {
		case TCPDIAG_BC_AUTO:
		case TCPDIAG_BC_S_COND:
		case TCPDIAG_BC_D_COND:
		case TCPDIAG_BC_S_GE:
		case TCPDIAG_BC_S_LE:
		case TCPDIAG_BC_D_GE:
		case TCPDIAG_BC_D_LE:
			if (op->yes < 4 || op->yes > len+4)
				return -EINVAL;
		case TCPDIAG_BC_JMP:
			if (op->no < 4 || op->no > len+4)
				return -EINVAL;
			if (op->no < len &&
			    !valid_cc(bytecode, bytecode_len, len-op->no))
				return -EINVAL;
			break;
		case TCPDIAG_BC_NOP:
			if (op->yes < 4 || op->yes > len+4)
				return -EINVAL;
			break;
		default:
			return -EINVAL;
		}
		bc += op->yes;
		len -= op->yes;
	}
	return len == 0 ? 0 : -EINVAL;
}


static int tcpdiag_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	int i, num;
	int s_i, s_num;
	struct tcpdiagreq *r = NLMSG_DATA(cb->nlh);
	struct rtattr *bc = NULL;

	if (cb->nlh->nlmsg_len > 4+NLMSG_SPACE(sizeof(struct tcpdiagreq)))
		bc = (struct rtattr*)(r+1);

	s_i = cb->args[1];
	s_num = num = cb->args[2];

	if (cb->args[0] == 0) {
		if (!(r->tcpdiag_states&(TCPF_LISTEN|TCPF_SYN_RECV)))
			goto skip_listen_ht;
		tcp_listen_lock();
		for (i = s_i; i < TCP_LHTABLE_SIZE; i++) {
			struct sock *sk;
			struct hlist_node *node;

			if (i > s_i)
				s_num = 0;

			num = 0;
			sk_for_each(sk, node, &tcp_listening_hash[i]) {
				struct inet_opt *inet = inet_sk(sk);
				if (num < s_num)
					continue;
				if (!(r->tcpdiag_states&TCPF_LISTEN) ||
				    r->id.tcpdiag_dport)
					continue;
				if (r->id.tcpdiag_sport != inet->sport &&
				    r->id.tcpdiag_sport)
					continue;
				if (bc && !tcpdiag_bc_run(RTA_DATA(bc), RTA_PAYLOAD(bc), sk))
					continue;
				if (tcpdiag_fill(skb, sk, r->tcpdiag_ext,
						 NETLINK_CB(cb->skb).pid,
						 cb->nlh->nlmsg_seq) <= 0) {
					tcp_listen_unlock();
					goto done;
				}
				++num;
			}
		}
		tcp_listen_unlock();
skip_listen_ht:
		cb->args[0] = 1;
		s_i = num = s_num = 0;
	}

	if (!(r->tcpdiag_states&~(TCPF_LISTEN|TCPF_SYN_RECV)))
		return skb->len;

	for (i = s_i; i < tcp_ehash_size; i++) {
		struct tcp_ehash_bucket *head = &tcp_ehash[i];
		struct sock *sk;
		struct hlist_node *node;

		if (i > s_i)
			s_num = 0;

		read_lock_bh(&head->lock);

		num = 0;
		sk_for_each(sk, node, &head->chain) {
			struct inet_opt *inet = inet_sk(sk);

			if (num < s_num)
				continue;
			if (!(r->tcpdiag_states & (1 << sk->sk_state)))
				continue;
			if (r->id.tcpdiag_sport != inet->sport &&
			    r->id.tcpdiag_sport)
				continue;
			if (r->id.tcpdiag_dport != inet->dport && r->id.tcpdiag_dport)
				continue;
			if (bc && !tcpdiag_bc_run(RTA_DATA(bc), RTA_PAYLOAD(bc), sk))
				continue;
			if (tcpdiag_fill(skb, sk, r->tcpdiag_ext,
					 NETLINK_CB(cb->skb).pid,
					 cb->nlh->nlmsg_seq) <= 0) {
				read_unlock_bh(&head->lock);
				goto done;
			}
			++num;
		}

		if (r->tcpdiag_states&TCPF_TIME_WAIT) {
			sk_for_each(sk, node,
				    &tcp_ehash[i + tcp_ehash_size].chain) {
				struct inet_opt *inet = inet_sk(sk);

				if (num < s_num)
					continue;
				if (!(r->tcpdiag_states & (1 << sk->sk_zapped)))
					continue;
				if (r->id.tcpdiag_sport != inet->sport &&
				    r->id.tcpdiag_sport)
					continue;
				if (r->id.tcpdiag_dport != inet->dport &&
				    r->id.tcpdiag_dport)
					continue;
				if (bc && !tcpdiag_bc_run(RTA_DATA(bc), RTA_PAYLOAD(bc), sk))
					continue;
				if (tcpdiag_fill(skb, sk, r->tcpdiag_ext,
						 NETLINK_CB(cb->skb).pid,
						 cb->nlh->nlmsg_seq) <= 0) {
					read_unlock_bh(&head->lock);
					goto done;
				}
				++num;
			}
		}
		read_unlock_bh(&head->lock);
	}

done:
	cb->args[1] = i;
	cb->args[2] = num;
	return skb->len;
}

static int tcpdiag_dump_done(struct netlink_callback *cb)
{
	return 0;
}


static __inline__ int
tcpdiag_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	if (!(nlh->nlmsg_flags&NLM_F_REQUEST))
		return 0;

	if (nlh->nlmsg_type != TCPDIAG_GETSOCK)
		goto err_inval;

	if (NLMSG_LENGTH(sizeof(struct tcpdiagreq)) > skb->len)
		goto err_inval;

	if (nlh->nlmsg_flags&NLM_F_DUMP) {
		if (nlh->nlmsg_len > 4 + NLMSG_SPACE(sizeof(struct tcpdiagreq))) {
			struct rtattr *rta = (struct rtattr*)(NLMSG_DATA(nlh) + sizeof(struct tcpdiagreq));
			if (rta->rta_type != TCPDIAG_REQ_BYTECODE ||
			    rta->rta_len < 8 ||
			    rta->rta_len > nlh->nlmsg_len - NLMSG_SPACE(sizeof(struct tcpdiagreq)))
				goto err_inval;
			if (tcpdiag_bc_audit(RTA_DATA(rta), RTA_PAYLOAD(rta)))
				goto err_inval;
		}
		return netlink_dump_start(tcpnl, skb, nlh,
					  tcpdiag_dump,
					  tcpdiag_dump_done);
	} else {
		return tcpdiag_get_exact(skb, nlh);
	}

err_inval:
	return -EINVAL;
}


static inline void tcpdiag_rcv_skb(struct sk_buff *skb)
{
	int err;
	struct nlmsghdr * nlh;

	if (skb->len >= NLMSG_SPACE(0)) {
		nlh = (struct nlmsghdr *)skb->data;
		if (nlh->nlmsg_len < sizeof(*nlh) || skb->len < nlh->nlmsg_len)
			return;
		err = tcpdiag_rcv_msg(skb, nlh);
		if (err || nlh->nlmsg_flags & NLM_F_ACK) 
			netlink_ack(skb, nlh, err);
	}
}

static void tcpdiag_rcv(struct sock *sk, int len)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&sk->sk_receive_queue)) != NULL) {
		tcpdiag_rcv_skb(skb);
		kfree_skb(skb);
	}
}

void __init tcpdiag_init(void)
{
	tcpnl = netlink_kernel_create(NETLINK_TCPDIAG, tcpdiag_rcv);
	if (tcpnl == NULL)
		panic("tcpdiag_init: Cannot create netlink socket.");
}
