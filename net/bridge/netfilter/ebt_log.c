/*
 *  ebt_log
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *
 *  April, 2002
 *
 */

#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_log.h>
#include <linux/module.h>
#include <linux/ip.h>
#include <linux/if_arp.h>
#include <linux/spinlock.h>

static spinlock_t ebt_log_lock = SPIN_LOCK_UNLOCKED;

static int ebt_log_check(const char *tablename, unsigned int hookmask,
   const struct ebt_entry *e, void *data, unsigned int datalen)
{
	struct ebt_log_info *info = (struct ebt_log_info *)data;

	if (datalen != sizeof(struct ebt_log_info))
		return -EINVAL;
	if (info->bitmask & ~EBT_LOG_MASK)
		return -EINVAL;
	if (info->loglevel >= 8)
		return -EINVAL;
	info->prefix[EBT_LOG_PREFIX_SIZE - 1] = '\0';
	return 0;
}

struct tcpudphdr
{
	uint16_t src;
	uint16_t dst;
};

struct arppayload
{
	unsigned char mac_src[ETH_ALEN];
	unsigned char ip_src[4];
	unsigned char mac_dst[ETH_ALEN];
	unsigned char ip_dst[4];
};

static void print_MAC(unsigned char *p)
{
	int i;

	for (i = 0; i < ETH_ALEN; i++, p++)
		printk("%02x%c", *p, i == ETH_ALEN - 1 ? ' ':':');
}

#define myNIPQUAD(a) a[0], a[1], a[2], a[3]
static void ebt_log(const struct sk_buff *skb, const struct net_device *in,
   const struct net_device *out, const void *data, unsigned int datalen)
{
	struct ebt_log_info *info = (struct ebt_log_info *)data;
	char level_string[4] = "< >";
	union {struct iphdr iph; struct tcpudphdr ports;
	       struct arphdr arph; struct arppayload arpp;} u;

	level_string[1] = '0' + info->loglevel;
	spin_lock_bh(&ebt_log_lock);
	printk(level_string);
	printk("%s IN=%s OUT=%s ", info->prefix, in ? in->name : "",
	   out ? out->name : "");

	printk("MAC source = ");
	print_MAC((skb->mac.ethernet)->h_source);
	printk("MAC dest = ");
	print_MAC((skb->mac.ethernet)->h_dest);

	printk("proto = 0x%04x", ntohs(((*skb).mac.ethernet)->h_proto));

	if ((info->bitmask & EBT_LOG_IP) && skb->mac.ethernet->h_proto ==
	   htons(ETH_P_IP)){
		if (skb_copy_bits(skb, 0, &u.iph, sizeof(u.iph))) {
			printk(" INCOMPLETE IP header");
			goto out;
		}
		printk(" IP SRC=%u.%u.%u.%u IP DST=%u.%u.%u.%u,",
		   NIPQUAD(u.iph.saddr), NIPQUAD(u.iph.daddr));
		printk(" IP tos=0x%02X, IP proto=%d", u.iph.tos,
		       u.iph.protocol);
		if (u.iph.protocol == IPPROTO_TCP ||
		    u.iph.protocol == IPPROTO_UDP) {
			if (skb_copy_bits(skb, u.iph.ihl*4, &u.ports,
			    sizeof(u.ports))) {
				printk(" INCOMPLETE TCP/UDP header");
				goto out;
			}
			printk(" SPT=%u DPT=%u", ntohs(u.ports.src),
			   ntohs(u.ports.dst));
		}
		goto out;
	}

	if ((info->bitmask & EBT_LOG_ARP) &&
	    ((skb->mac.ethernet->h_proto == __constant_htons(ETH_P_ARP)) ||
	    (skb->mac.ethernet->h_proto == __constant_htons(ETH_P_RARP)))) {
		if (skb_copy_bits(skb, 0, &u.arph, sizeof(u.arph))) {
			printk(" INCOMPLETE ARP header");
			goto out;
		}
		printk(" ARP HTYPE=%d, PTYPE=0x%04x, OPCODE=%d",
		       ntohs(u.arph.ar_hrd), ntohs(u.arph.ar_pro),
		       ntohs(u.arph.ar_op));

		/* If it's for Ethernet and the lengths are OK,
		 * then log the ARP payload */
		if (u.arph.ar_hrd == __constant_htons(1) &&
		    u.arph.ar_hln == ETH_ALEN &&
		    u.arph.ar_pln == sizeof(uint32_t)) {
			if (skb_copy_bits(skb, sizeof(u.arph), &u.arpp,
			    sizeof(u.arpp))) {
				printk(" INCOMPLETE ARP payload");
				goto out;
			}
			printk(" ARP MAC SRC=");
			print_MAC(u.arpp.mac_src);
			printk(" ARP IP SRC=%u.%u.%u.%u",
			       myNIPQUAD(u.arpp.ip_src));
			printk(" ARP MAC DST=");
			print_MAC(u.arpp.mac_dst);
			printk(" ARP IP DST=%u.%u.%u.%u",
			       myNIPQUAD(u.arpp.ip_dst));
		}
	}
out:
	printk("\n");
	spin_unlock_bh(&ebt_log_lock);
}

static struct ebt_watcher log =
{
	.name		= EBT_LOG_WATCHER,
	.watcher	= ebt_log,
	.check		= ebt_log_check,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ebt_register_watcher(&log);
}

static void __exit fini(void)
{
	ebt_unregister_watcher(&log);
}

module_init(init);
module_exit(fini);
MODULE_LICENSE("GPL");
