/* This is a module which is used for setting the NFMARK field of an skb. */

/* (C) 1999-2001 Marc Boucher <marc@mbsi.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <net/checksum.h>

#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_MARK.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marc Boucher <marc@mbsi.ca>");
MODULE_DESCRIPTION("iptables MARK modification module");

static unsigned int
target(struct sk_buff **pskb,
       const struct net_device *in,
       const struct net_device *out,
       unsigned int hooknum,
       const void *targinfo,
       void *userinfo)
{
	const struct ipt_mark_target_info *markinfo = targinfo;

	if((*pskb)->nfmark != markinfo->mark) {
		(*pskb)->nfmark = markinfo->mark;
		(*pskb)->nfcache |= NFC_ALTERED;
	}
	return IPT_CONTINUE;
}

static int
checkentry(const char *tablename,
	   const struct ipt_entry *e,
           void *targinfo,
           unsigned int targinfosize,
           unsigned int hook_mask)
{
	if (targinfosize != IPT_ALIGN(sizeof(struct ipt_mark_target_info))) {
		printk(KERN_WARNING "MARK: targinfosize %u != %Zu\n",
		       targinfosize,
		       IPT_ALIGN(sizeof(struct ipt_mark_target_info)));
		return 0;
	}

	if (strcmp(tablename, "mangle") != 0) {
		printk(KERN_WARNING "MARK: can only be called from \"mangle\" table, not \"%s\"\n", tablename);
		return 0;
	}

	return 1;
}

static struct ipt_target ipt_mark_reg = {
	.name		= "MARK",
	.target		= target,
	.checkentry	= checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ipt_register_target(&ipt_mark_reg);
}

static void __exit fini(void)
{
	ipt_unregister_target(&ipt_mark_reg);
}

module_init(init);
module_exit(fini);
