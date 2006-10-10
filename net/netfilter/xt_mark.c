/* Kernel module to match NFMARK values. */

/* (C) 1999-2001 Marc Boucher <marc@mbsi.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/netfilter/xt_mark.h>
#include <linux/netfilter/x_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marc Boucher <marc@mbsi.ca>");
MODULE_DESCRIPTION("iptables mark matching module");
MODULE_ALIAS("ipt_mark");
MODULE_ALIAS("ip6t_mark");

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	const struct xt_mark_info *info = matchinfo;

	return ((skb->nfmark & info->mask) == info->mark) ^ info->invert;
}

static int
checkentry(const char *tablename,
           const void *entry,
	   const struct xt_match *match,
           void *matchinfo,
           unsigned int matchsize,
           unsigned int hook_mask)
{
	const struct xt_mark_info *minfo = matchinfo;

	if (minfo->mark > 0xffffffff || minfo->mask > 0xffffffff) {
		printk(KERN_WARNING "mark: only supports 32bit mark\n");
		return 0;
	}
	return 1;
}

#ifdef CONFIG_COMPAT
struct compat_xt_mark_info {
	compat_ulong_t	mark, mask;
	u_int8_t	invert;
	u_int8_t	__pad1;
	u_int16_t	__pad2;
};

static void compat_from_user(void *dst, void *src)
{
	struct compat_xt_mark_info *cm = src;
	struct xt_mark_info m = {
		.mark	= cm->mark,
		.mask	= cm->mask,
		.invert	= cm->invert,
	};
	memcpy(dst, &m, sizeof(m));
}

static int compat_to_user(void __user *dst, void *src)
{
	struct xt_mark_info *m = src;
	struct compat_xt_mark_info cm = {
		.mark	= m->mark,
		.mask	= m->mask,
		.invert	= m->invert,
	};
	return copy_to_user(dst, &cm, sizeof(cm)) ? -EFAULT : 0;
}
#endif /* CONFIG_COMPAT */

static struct xt_match mark_match = {
	.name		= "mark",
	.match		= match,
	.matchsize	= sizeof(struct xt_mark_info),
	.checkentry	= checkentry,
#ifdef CONFIG_COMPAT
		.compatsize	= sizeof(struct compat_xt_mark_info),
		.compat_from_user = compat_from_user,
		.compat_to_user	= compat_to_user,
#endif
	.family		= AF_INET,
	.me		= THIS_MODULE,
};

static struct xt_match mark6_match = {
	.name		= "mark",
	.match		= match,
	.matchsize	= sizeof(struct xt_mark_info),
	.checkentry	= checkentry,
	.family		= AF_INET6,
	.me		= THIS_MODULE,
};

static int __init xt_mark_init(void)
{
	int ret;
	ret = xt_register_match(&mark_match);
	if (ret)
		return ret;

	ret = xt_register_match(&mark6_match);
	if (ret)
		xt_unregister_match(&mark_match);

	return ret;
}

static void __exit xt_mark_fini(void)
{
	xt_unregister_match(&mark_match);
	xt_unregister_match(&mark6_match);
}

module_init(xt_mark_init);
module_exit(xt_mark_fini);
