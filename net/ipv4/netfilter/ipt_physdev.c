/* Kernel module to match the bridge port in and
 * out device for IP packets coming into contact with a bridge. */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter_ipv4/ipt_physdev.h>
#include <linux/netfilter_ipv4/ip_tables.h>

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      const void *hdr,
      u_int16_t datalen,
      int *hotdrop)
{
	int i;
	static const char nulldevname[IFNAMSIZ] = { 0 };
	const struct ipt_physdev_info *info = matchinfo;
	unsigned long ret;
	const char *indev, *outdev;
	struct nf_bridge_info *nf_bridge;

	/* Not a bridged IP packet or no info available yet:
	 * LOCAL_OUT/mangle and LOCAL_OUT/nat don't know if
	 * the destination device will be a bridge. */
	if (!(nf_bridge = skb->nf_bridge))
		return 1;

	indev = nf_bridge->physindev ? nf_bridge->physindev->name : nulldevname;
	outdev = nf_bridge->physoutdev ?
		 nf_bridge->physoutdev->name : nulldevname;

	for (i = 0, ret = 0; i < IFNAMSIZ/sizeof(unsigned long); i++) {
		ret |= (((const unsigned long *)indev)[i]
			^ ((const unsigned long *)info->physindev)[i])
			& ((const unsigned long *)info->in_mask)[i];
	}

	if ((ret == 0) ^ !(info->invert & IPT_PHYSDEV_OP_MATCH_IN))
		return 0;

	for (i = 0, ret = 0; i < IFNAMSIZ/sizeof(unsigned long); i++) {
		ret |= (((const unsigned long *)outdev)[i]
			^ ((const unsigned long *)info->physoutdev)[i])
			& ((const unsigned long *)info->out_mask)[i];
	}

	return (ret != 0) ^ !(info->invert & IPT_PHYSDEV_OP_MATCH_OUT);
}

static int
checkentry(const char *tablename,
		       const struct ipt_ip *ip,
		       void *matchinfo,
		       unsigned int matchsize,
		       unsigned int hook_mask)
{
	if (matchsize != IPT_ALIGN(sizeof(struct ipt_physdev_info)))
		return 0;

	return 1;
}

static struct ipt_match physdev_match = {
	.name		= "physdev",
	.match		= &match,
	.checkentry	= &checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ipt_register_match(&physdev_match);
}

static void __exit fini(void)
{
	ipt_unregister_match(&physdev_match);
}

module_init(init);
module_exit(fini);
MODULE_LICENSE("GPL");
