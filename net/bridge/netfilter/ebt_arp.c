/*
 *  ebt_arp
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *	Tim Gardner <timg@tpi.com>
 *
 *  April, 2002
 *
 */

#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_arp.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/module.h>

static int ebt_filter_arp(const struct sk_buff *skb, const struct net_device *in,
   const struct net_device *out, const void *data, unsigned int datalen)
{
	struct ebt_arp_info *info = (struct ebt_arp_info *)data;
	struct arphdr arph;

	if (skb_copy_bits(skb, 0, &arph, sizeof(arph)))
		return EBT_NOMATCH;
	if (info->bitmask & EBT_ARP_OPCODE && FWINV(info->opcode !=
	   arph.ar_op, EBT_ARP_OPCODE))
		return EBT_NOMATCH;
	if (info->bitmask & EBT_ARP_HTYPE && FWINV(info->htype !=
	   arph.ar_hrd, EBT_ARP_HTYPE))
		return EBT_NOMATCH;
	if (info->bitmask & EBT_ARP_PTYPE && FWINV(info->ptype !=
	   arph.ar_pro, EBT_ARP_PTYPE))
		return EBT_NOMATCH;

	if (info->bitmask & (EBT_ARP_SRC_IP | EBT_ARP_DST_IP)) {
		uint32_t addr;

		/* IPv4 addresses are always 4 bytes */
		if (arph.ar_pln != sizeof(uint32_t))
			return EBT_NOMATCH;
		if (info->bitmask & EBT_ARP_SRC_IP) {
			if (skb_copy_bits(skb, sizeof(struct arphdr) +
			    arph.ar_hln, &addr, sizeof(addr)))
				return EBT_NOMATCH;
			if (FWINV(info->saddr != (addr & info->smsk),
			   EBT_ARP_SRC_IP))
				return EBT_NOMATCH;
		}

		if (info->bitmask & EBT_ARP_DST_IP) {
			if (skb_copy_bits(skb, sizeof(struct arphdr) +
			    2*arph.ar_hln + sizeof(uint32_t), &addr,
			    sizeof(addr)))
				return EBT_NOMATCH;
			if (FWINV(info->daddr != (addr & info->dmsk),
			   EBT_ARP_DST_IP))
				return EBT_NOMATCH;
		}
	}

	if (info->bitmask & (EBT_ARP_SRC_MAC | EBT_ARP_DST_MAC)) {
		unsigned char mac[ETH_ALEN];
		uint8_t verdict, i;

		/* MAC addresses are 6 bytes */
		if (arph.ar_hln != ETH_ALEN)
			return EBT_NOMATCH;
		if (info->bitmask & EBT_ARP_SRC_MAC) {
			if (skb_copy_bits(skb, sizeof(struct arphdr), &mac,
			    ETH_ALEN))
				return EBT_NOMATCH;
			verdict = 0;
			for (i = 0; i < 6; i++)
				verdict |= (mac[i] ^ info->smaddr[i]) &
				       info->smmsk[i];
			if (FWINV(verdict != 0, EBT_ARP_SRC_MAC))
				return EBT_NOMATCH;
		}

		if (info->bitmask & EBT_ARP_DST_MAC) {
			if (skb_copy_bits(skb, sizeof(struct arphdr) +
			    arph.ar_hln + arph.ar_pln, &mac, ETH_ALEN))
				return EBT_NOMATCH;
			verdict = 0;
			for (i = 0; i < 6; i++)
				verdict |= (mac[i] ^ info->dmaddr[i]) &
					info->dmmsk[i];
			if (FWINV(verdict != 0, EBT_ARP_DST_MAC))
				return EBT_NOMATCH;
		}
	}

	return EBT_MATCH;
}

static int ebt_arp_check(const char *tablename, unsigned int hookmask,
   const struct ebt_entry *e, void *data, unsigned int datalen)
{
	struct ebt_arp_info *info = (struct ebt_arp_info *)data;

	if (datalen != EBT_ALIGN(sizeof(struct ebt_arp_info)))
		return -EINVAL;
	if ((e->ethproto != __constant_htons(ETH_P_ARP) &&
	   e->ethproto != __constant_htons(ETH_P_RARP)) ||
	   e->invflags & EBT_IPROTO)
		return -EINVAL;
	if (info->bitmask & ~EBT_ARP_MASK || info->invflags & ~EBT_ARP_MASK)
		return -EINVAL;
	return 0;
}

static struct ebt_match filter_arp =
{
	.name		= EBT_ARP_MATCH,
	.match		= ebt_filter_arp,
	.check		= ebt_arp_check,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ebt_register_match(&filter_arp);
}

static void __exit fini(void)
{
	ebt_unregister_match(&filter_arp);
}

module_init(init);
module_exit(fini);
MODULE_LICENSE("GPL");
