/*
 * IPv6 Connection Tracking
 * Linux INET6 implementation
 *
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on: net/ipv4/netfilter/ip_conntrack_standalone.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/* This file contains all the functions required for the standalone
   ip6_conntrack module.

   These are not required by the compatibility layer.
*/

/* (c) 1999 Paul `Rusty' Russell.  Licenced under the GNU General
   Public Licence. */

#include <linux/types.h>
#include <linux/ipv6.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/version.h>
#include <net/checksum.h>

#define ASSERT_READ_LOCK(x) MUST_BE_READ_LOCKED(&ip6_conntrack_lock)
#define ASSERT_WRITE_LOCK(x) MUST_BE_WRITE_LOCKED(&ip6_conntrack_lock)

#include <linux/netfilter_ipv6/ip6_conntrack.h>
#include <linux/netfilter_ipv6/ip6_conntrack_protocol.h>
#include <linux/netfilter_ipv6/ip6_conntrack_core.h>
#include <linux/netfilter_ipv6/ip6_conntrack_helper.h>
#include <linux/netfilter_ipv6/ip6_conntrack_reasm.h>
#include <linux/netfilter_ipv4/listhelp.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

MODULE_LICENSE("GPL");

static int kill_proto(const struct ip6_conntrack *i, void *data)
{
	return (i->tuplehash[IP6_CT_DIR_ORIGINAL].tuple.dst.protonum == 
			*((u_int8_t *) data));
}

static unsigned int
print_tuple(char *buffer, const struct ip6_conntrack_tuple *tuple,
	    struct ip6_conntrack_protocol *proto)
{
	int len;

	len = sprintf(buffer, "src=%x:%x:%x:%x:%x:%x:%x:%x dst=%x:%x:%x:%x:%x:%x:%x:%x ",
		      NIP6(tuple->src.ip), NIP6(tuple->dst.ip));

	len += proto->print_tuple(buffer + len, tuple);

	return len;
}

/* FIXME: Don't print source proto part. --RR */
static unsigned int
print_expect(char *buffer, const struct ip6_conntrack_expect *expect)
{
	unsigned int len;

	if (expect->expectant->helper->timeout)
		len = sprintf(buffer, "EXPECTING: %lu ",
			      timer_pending(&expect->timeout)
			      ? (expect->timeout.expires - jiffies)/HZ : 0);
	else
		len = sprintf(buffer, "EXPECTING: - ");
	len += sprintf(buffer + len, "use=%u proto=%u ",
		      atomic_read(&expect->use), expect->tuple.dst.protonum);
	len += print_tuple(buffer + len, &expect->tuple,
			   __ip6_ct_find_proto(expect->tuple.dst.protonum));
	len += sprintf(buffer + len, "\n");
	return len;
}

static unsigned int
print_conntrack(char *buffer, struct ip6_conntrack *conntrack)
{
	unsigned int len;
	struct ip6_conntrack_protocol *proto
		= __ip6_ct_find_proto(conntrack->tuplehash[IP6_CT_DIR_ORIGINAL]
			       .tuple.dst.protonum);

	len = sprintf(buffer, "%-8s %u %lu ",
		      proto->name,
		      conntrack->tuplehash[IP6_CT_DIR_ORIGINAL]
		      .tuple.dst.protonum,
		      timer_pending(&conntrack->timeout)
		      ? (conntrack->timeout.expires - jiffies)/HZ : 0);

	len += proto->print_conntrack(buffer + len, conntrack);
	len += print_tuple(buffer + len,
			   &conntrack->tuplehash[IP6_CT_DIR_ORIGINAL].tuple,
			   proto);
	if (!(test_bit(IP6S_SEEN_REPLY_BIT, &conntrack->status)))
		len += sprintf(buffer + len, "[UNREPLIED] ");
	len += print_tuple(buffer + len,
			   &conntrack->tuplehash[IP6_CT_DIR_REPLY].tuple,
			   proto);
	if (test_bit(IP6S_ASSURED_BIT, &conntrack->status))
		len += sprintf(buffer + len, "[ASSURED] ");
	len += sprintf(buffer + len, "use=%u ",
		       atomic_read(&conntrack->ct_general.use));
	len += sprintf(buffer + len, "\n");

	return len;
}

/* Returns true when finished. */
static inline int
conntrack_iterate(const struct ip6_conntrack_tuple_hash *hash,
		  char *buffer, off_t offset, off_t *upto,
		  unsigned int *len, unsigned int maxlen)
{
	unsigned int newlen;
	IP6_NF_ASSERT(hash->ctrack);

	MUST_BE_READ_LOCKED(&ip6_conntrack_lock);

	/* Only count originals */
	if (DIRECTION(hash))
		return 0;

	if ((*upto)++ < offset)
		return 0;

	newlen = print_conntrack(buffer + *len, hash->ctrack);
	if (*len + newlen > maxlen)
		return 1;
	else *len += newlen;

	return 0;
}

static int
list_conntracks(char *buffer, char **start, off_t offset, int length)
{
	unsigned int i;
	unsigned int len = 0;
	off_t upto = 0;
	struct list_head *e;

	READ_LOCK(&ip6_conntrack_lock);
	/* Traverse hash; print originals then reply. */
	for (i = 0; i < ip6_conntrack_htable_size; i++) {
		if (LIST_FIND(&ip6_conntrack_hash[i], conntrack_iterate,
			      struct ip6_conntrack_tuple_hash *,
			      buffer, offset, &upto, &len, length))
			goto finished;
	}

	/* Now iterate through expecteds. */
	for (e = ip6_conntrack_expect_list.next; 
	     e != &ip6_conntrack_expect_list; e = e->next) {
		unsigned int last_len;
		struct ip6_conntrack_expect *expect
			= (struct ip6_conntrack_expect *)e;
		if (upto++ < offset) continue;

		last_len = len;
		len += print_expect(buffer + len, expect);
		if (len > length) {
			len = last_len;
			goto finished;
		}
	}

 finished:
	READ_UNLOCK(&ip6_conntrack_lock);

	/* `start' hack - see fs/proc/generic.c line ~165 */
	*start = (char *)((unsigned int)upto - offset);
	return len;
}

static unsigned int ip6_confirm(unsigned int hooknum,
			       struct sk_buff **pskb,
			       const struct net_device *in,
			       const struct net_device *out,
				int (*okfn)(struct sk_buff *));
static unsigned int ip6_conntrack_out(unsigned int hooknum,
			       struct sk_buff **pskb,
			       const struct net_device *in,
			       const struct net_device *out,
				int (*okfn)(struct sk_buff *));
static unsigned int ip6_conntrack_reasm(unsigned int hooknum,
			       struct sk_buff **pskb,
			       const struct net_device *in,
			       const struct net_device *out,
				int (*okfn)(struct sk_buff *));
static unsigned int ip6_conntrack_local(unsigned int hooknum,
			       struct sk_buff **pskb,
			       const struct net_device *in,
			       const struct net_device *out,
				int (*okfn)(struct sk_buff *));

/* Connection tracking may drop packets, but never alters them, so
   make it the first hook. */
static struct nf_hook_ops ip6_conntrack_in_ops = {
	/* Don't forget to change .hook to "ip6_conntrack_input". - zak */
	.hook		= ip6_conntrack_reasm,
	.owner		= THIS_MODULE,
	.pf		= PF_INET6,
	.hooknum	= NF_IP6_PRE_ROUTING,
	.priority	= NF_IP6_PRI_CONNTRACK,
};

static struct nf_hook_ops ip6_conntrack_local_out_ops = {
	.hook		= ip6_conntrack_local,
	.owner		= THIS_MODULE,
	.pf		= PF_INET6,
	.hooknum	= NF_IP6_LOCAL_OUT,
	.priority	= NF_IP6_PRI_CONNTRACK,
};

/* Refragmenter; last chance. */
static struct nf_hook_ops ip6_conntrack_out_ops = {
	.hook		= ip6_conntrack_out,
	.owner		= THIS_MODULE,
	.pf		= PF_INET6,
	.hooknum	= NF_IP6_POST_ROUTING,
	.priority	= NF_IP6_PRI_LAST,
};

static struct nf_hook_ops ip6_conntrack_local_in_ops = {
	.hook		= ip6_confirm,
	.owner		= THIS_MODULE,
	.pf		= PF_INET6,
	.hooknum	= NF_IP6_LOCAL_IN,
	.priority	= NF_IP6_PRI_LAST-1,
};

static unsigned int ip6_confirm(unsigned int hooknum,
			       struct sk_buff **pskb,
			       const struct net_device *in,
			       const struct net_device *out,
			       int (*okfn)(struct sk_buff *))
{
	int ret;

	ret = ip6_conntrack_confirm(*pskb);

	return ret;
}

static unsigned int ip6_conntrack_out(unsigned int hooknum,
			      struct sk_buff **pskb,
			      const struct net_device *in,
			      const struct net_device *out,
			      int (*okfn)(struct sk_buff *))
{

	if (ip6_conntrack_confirm(*pskb) != NF_ACCEPT)
                return NF_DROP;

	return NF_ACCEPT;
}

static unsigned int ip6_conntrack_reasm(unsigned int hooknum,
					      struct sk_buff **pskb,
					      const struct net_device *in,
					      const struct net_device *out,
					      int (*okfn)(struct sk_buff *))
{
	struct sk_buff *skb = *pskb;
	struct sk_buff **rsmd_pskb = &skb;
	int fragd = 0;
	int ret;

	skb->nfcache |= NFC_UNKNOWN;

	/*
	 * Previously seen (loopback)?  Ignore.  Do this before
	 * fragment check.
	 */
	if (skb->nfct) {
		DEBUGP("previously seen\n");
		return NF_ACCEPT;
	}

	skb = ip6_ct_gather_frags(skb);

	/* queued */
	if (skb == NULL)
		return NF_STOLEN;

	if (skb != (*pskb))
		fragd = 1;

	ret = ip6_conntrack_in(hooknum, rsmd_pskb, in, out, okfn);

	if (!fragd)
		return ret;

	if (ret == NF_DROP) {
		ip6_ct_kfree_frags(skb);
	}else{
		struct nf_info info;

		info.pf = PF_INET6;
		info.hook = hooknum;
		info.indev = in;
		info.outdev = out;
		info.okfn = okfn;
		switch (hooknum) {
		case NF_IP6_PRE_ROUTING:
			info.elem = &ip6_conntrack_in_ops;
			break;
		case NF_IP6_LOCAL_OUT:
			info.elem = &ip6_conntrack_local_out_ops;
			break;
		}

		if (ip6_ct_output_frags(skb, &info) <0)
			DEBUGP("Can't output fragments\n");

	}

	return NF_STOLEN;
}

static unsigned int ip6_conntrack_local(unsigned int hooknum,
				       struct sk_buff **pskb,
				       const struct net_device *in,
				       const struct net_device *out,
				       int (*okfn)(struct sk_buff *))
{
	unsigned int ret;

	/* root is playing with raw sockets. */
	if ((*pskb)->len < sizeof(struct ipv6hdr)) {
		if (net_ratelimit())
			printk("ip6t_hook: IPv6 header is too short.\n");
		return NF_ACCEPT;
	}

	ret = ip6_conntrack_reasm(hooknum, pskb, in, out, okfn);

	return ret;
}

static int init_or_cleanup(int init)
{
	struct proc_dir_entry *proc;
	int ret = 0;

	if (!init) goto cleanup;

	ret = ip6_ct_frags_init();
	if (ret < 0)
		goto cleanup_reasm;

	ret = ip6_conntrack_init();
	if (ret < 0)
		goto cleanup_nothing;

	proc = proc_net_create("ip6_conntrack",0,list_conntracks);
	if (!proc) goto cleanup_init;
	proc->owner = THIS_MODULE;

	ret = nf_register_hook(&ip6_conntrack_in_ops);
	if (ret < 0) {
		printk("ip6_conntrack: can't register pre-routing hook.\n");
		goto cleanup_proc;
	}
	ret = nf_register_hook(&ip6_conntrack_local_out_ops);
	if (ret < 0) {
		printk("ip6_conntrack: can't register local out hook.\n");
		goto cleanup_inops;
	}
	ret = nf_register_hook(&ip6_conntrack_out_ops);
	if (ret < 0) {
		printk("ip6_conntrack: can't register post-routing hook.\n");
		goto cleanup_inandlocalops;
	}
	ret = nf_register_hook(&ip6_conntrack_local_in_ops);
	if (ret < 0) {
		printk("ip6_conntrack: can't register local in hook.\n");
		goto cleanup_inoutandlocalops;
	}

	return ret;

 cleanup:
	nf_unregister_hook(&ip6_conntrack_local_in_ops);
 cleanup_inoutandlocalops:
	nf_unregister_hook(&ip6_conntrack_out_ops);
 cleanup_inandlocalops:
	nf_unregister_hook(&ip6_conntrack_local_out_ops);
 cleanup_inops:
	nf_unregister_hook(&ip6_conntrack_in_ops);
 cleanup_proc:
	proc_net_remove("ip6_conntrack");
 cleanup_init:
	ip6_conntrack_cleanup();
 cleanup_reasm:
	ip6_ct_frags_cleanup();
 cleanup_nothing:
	return ret;
}

/* FIXME: Allow NULL functions and sub in pointers to generic for
   them. --RR */
int ip6_conntrack_protocol_register(struct ip6_conntrack_protocol *proto)
{
	int ret = 0;
	struct list_head *i;

	WRITE_LOCK(&ip6_conntrack_lock);
	for (i = ip6_protocol_list.next; i != &ip6_protocol_list; i = i->next) {
		if (((struct ip6_conntrack_protocol *)i)->proto
		    == proto->proto) {
			ret = -EBUSY;
			goto out;
		}
	}

	list_prepend(&ip6_protocol_list, proto);

 out:
	WRITE_UNLOCK(&ip6_conntrack_lock);
	return ret;
}

void ip6_conntrack_protocol_unregister(struct ip6_conntrack_protocol *proto)
{
	WRITE_LOCK(&ip6_conntrack_lock);

	/* ip_ct_find_proto() returns proto_generic in case there is no protocol 
	 * helper. So this should be enough - HW */
	LIST_DELETE(&ip6_protocol_list, proto);
	WRITE_UNLOCK(&ip6_conntrack_lock);

	/* Somebody could be still looking at the proto in bh. */
	synchronize_net();

	/* Remove all contrack entries for this protocol */
	ip6_ct_selective_cleanup(kill_proto, &proto->proto);
}

static int __init init(void)
{
	return init_or_cleanup(1);
}

static void __exit fini(void)
{
	init_or_cleanup(0);
}

module_init(init);
module_exit(fini);

/* Some modules need us, but don't depend directly on any symbol.
   They should call this. */
void need_ip6_conntrack(void)
{
}

EXPORT_SYMBOL(ip6_conntrack_protocol_register);
EXPORT_SYMBOL(ip6_conntrack_protocol_unregister);
EXPORT_SYMBOL(ip6_invert_tuplepr);
EXPORT_SYMBOL(ip6_conntrack_alter_reply);
EXPORT_SYMBOL(ip6_conntrack_destroyed);
EXPORT_SYMBOL(ip6_conntrack_get);
EXPORT_SYMBOL(need_ip6_conntrack);
EXPORT_SYMBOL(ip6_conntrack_helper_register);
EXPORT_SYMBOL(ip6_conntrack_helper_unregister);
EXPORT_SYMBOL(ip6_ct_selective_cleanup);
EXPORT_SYMBOL(ip6_ct_refresh);
EXPORT_SYMBOL(ip6_ct_find_proto);
EXPORT_SYMBOL(__ip6_ct_find_proto);
EXPORT_SYMBOL(ip6_ct_find_helper);
EXPORT_SYMBOL(ip6_conntrack_expect_related);
EXPORT_SYMBOL(ip6_conntrack_unexpect_related);
EXPORT_SYMBOL_GPL(ip6_conntrack_expect_find_get);
EXPORT_SYMBOL_GPL(ip6_conntrack_expect_put);
EXPORT_SYMBOL(ip6_conntrack_tuple_taken);
EXPORT_SYMBOL(ip6_conntrack_htable_size);
EXPORT_SYMBOL(ip6_conntrack_expect_list);
EXPORT_SYMBOL(ip6_conntrack_lock);
EXPORT_SYMBOL_GPL(ip6_conntrack_find_get);
EXPORT_SYMBOL_GPL(ip6_conntrack_put);
