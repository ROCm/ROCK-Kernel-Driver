/*
 *	IPv6 over IPv6 tunnel device
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Ville Nuorvala		<vnuorval@tcs.hut.fi>	
 *
 *	$Id: s.ipv6_tunnel.c 1.41 03/09/23 15:34:20+03:00 vnuorval@amber.hut.mediapoli.com $
 *
 *      Based on:
 *      linux/net/ipv6/sit.c
 *
 *      RFC 2473
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/if.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/if_tunnel.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/icmpv6.h>
#include <linux/init.h>
#include <linux/route.h>
#include <linux/rtnetlink.h>
#include <linux/workqueue.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>

#include <asm/uaccess.h>
#include <asm/atomic.h>

#include <net/sock.h>
#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/ipv6_tunnel.h>

MODULE_AUTHOR("Ville Nuorvala");
MODULE_DESCRIPTION("IPv6-in-IPv6 tunnel");
MODULE_LICENSE("GPL");

#define IPV6_TLV_TEL_DST_SIZE 8

#ifdef IP6_TNL_DEBUG
#define IP6_TNL_TRACE(x...) printk(KERN_DEBUG "%s:" x "\n", __FUNCTION__)
#else
#define IP6_TNL_TRACE(x...) do {;} while(0)
#endif

#define IPV6_TCLASS_MASK (IPV6_FLOWINFO_MASK & ~IPV6_FLOWLABEL_MASK)

#define HASH_SIZE  32

#define HASH(addr) (((addr)->s6_addr32[0] ^ (addr)->s6_addr32[1] ^ \
	             (addr)->s6_addr32[2] ^ (addr)->s6_addr32[3]) & \
                    (HASH_SIZE - 1))

static int ip6ip6_fb_tnl_dev_init(struct net_device *dev);
static int ip6ip6_tnl_dev_init(struct net_device *dev);

/* the IPv6 IPv6 tunnel fallback device */
static struct net_device ip6ip6_fb_tnl_dev = {
	name: "ip6tnl0",
	init: ip6ip6_fb_tnl_dev_init
};

/* the IPv6 IPv6 fallback tunnel */
static struct ip6_tnl ip6ip6_fb_tnl = {
	owner: THIS_MODULE,
	dev: &ip6ip6_fb_tnl_dev,
	parms:{name: "ip6tnl0", proto: IPPROTO_IPV6}
};

/* lists for storing tunnels in use */
static struct ip6_tnl *tnls_r_l[HASH_SIZE];
static struct ip6_tnl *tnls_wc[1];
static struct ip6_tnl **tnls[2] = { tnls_wc, tnls_r_l };

/* list for unused cached kernel tunnels */
static struct ip6_tnl *tnls_kernel[1];
/* maximum number of cached kernel tunnels */
static unsigned int max_kdev_count = 0;
/* minimum number of cached kernel tunnels */
static unsigned int min_kdev_count = 0;
/* current number of cached kernel tunnels */
static unsigned int kdev_count = 0;

/* lists for tunnel hook functions */
static struct list_head hooks[IP6_TNL_MAXHOOKS];

/* locks for the different lists */
static rwlock_t ip6ip6_lock = RW_LOCK_UNLOCKED;
static rwlock_t ip6ip6_kernel_lock = RW_LOCK_UNLOCKED;
static rwlock_t ip6ip6_hook_lock = RW_LOCK_UNLOCKED;

/* flag indicating if the module is being removed */
static int shutdown = 0;

/**
 * ip6ip6_tnl_lookup - fetch tunnel matching the end-point addresses
 *   @remote: the address of the tunnel exit-point 
 *   @local: the address of the tunnel entry-point 
 *
 * Return:  
 *   tunnel matching given end-points if found,
 *   else fallback tunnel if its device is up, 
 *   else %NULL
 **/

struct ip6_tnl *
ip6ip6_tnl_lookup(struct in6_addr *remote, struct in6_addr *local)
{
	unsigned h0 = HASH(remote);
	unsigned h1 = HASH(local);
	struct ip6_tnl *t;

	for (t = tnls_r_l[h0 ^ h1]; t; t = t->next) {
		if (!ipv6_addr_cmp(local, &t->parms.laddr) &&
		    !ipv6_addr_cmp(remote, &t->parms.raddr) &&
		    (t->dev->flags & IFF_UP))
			return t;
	}
	if ((t = tnls_wc[0]) != NULL && (t->dev->flags & IFF_UP))
		return t;

	return NULL;
}

/**
 * ip6ip6_bucket - get head of list matching given tunnel parameters
 *   @p: parameters containing tunnel end-points 
 *
 * Description:
 *   ip6ip6_bucket() returns the head of the list matching the 
 *   &struct in6_addr entries laddr and raddr in @p.
 *
 * Return: head of IPv6 tunnel list 
 **/

static struct ip6_tnl **
ip6ip6_bucket(struct ip6_tnl_parm *p)
{
	struct in6_addr *remote = &p->raddr;
	struct in6_addr *local = &p->laddr;
	unsigned h = 0;
	int prio = 0;

	if (!ipv6_addr_any(remote) || !ipv6_addr_any(local)) {
		prio = 1;
		h = HASH(remote) ^ HASH(local);
	}
	return &tnls[prio][h];
}

/**
 * ip6ip6_kernel_tnl_link - add new kernel tunnel to cache
 *   @t: kernel tunnel
 *
 * Note:
 *   %IP6_TNL_F_KERNEL_DEV is assumed to be raised in t->parms.flags. 
 *   See the comments on ip6ip6_kernel_tnl_add() for more information.
 **/

static inline void
ip6ip6_kernel_tnl_link(struct ip6_tnl *t)
{
	write_lock_bh(&ip6ip6_kernel_lock);
	t->next = tnls_kernel[0];
	tnls_kernel[0] = t;
	kdev_count++;
	write_unlock_bh(&ip6ip6_kernel_lock);
}

/**
 * ip6ip6_kernel_tnl_unlink - remove first kernel tunnel from cache
 *
 * Return: first free kernel tunnel
 *
 * Note:
 *   See the comments on ip6ip6_kernel_tnl_add() for more information.
 **/

static inline struct ip6_tnl *
ip6ip6_kernel_tnl_unlink(void)
{
	struct ip6_tnl *t;

	write_lock_bh(&ip6ip6_kernel_lock);
	if ((t = tnls_kernel[0]) != NULL) {
		tnls_kernel[0] = t->next;
		kdev_count--;
	}
	write_unlock_bh(&ip6ip6_kernel_lock);
	return t;
}

/**
 * ip6ip6_tnl_link - add tunnel to hash table
 *   @t: tunnel to be added
 **/

static void
ip6ip6_tnl_link(struct ip6_tnl *t)
{
	struct ip6_tnl **tp = ip6ip6_bucket(&t->parms);

	write_lock_bh(&ip6ip6_lock);
	t->next = *tp;
	*tp = t;
	write_unlock_bh(&ip6ip6_lock);
}

/**
 * ip6ip6_tnl_unlink - remove tunnel from hash table
 *   @t: tunnel to be removed
 **/

static void
ip6ip6_tnl_unlink(struct ip6_tnl *t)
{
	struct ip6_tnl **tp;
	
	write_lock_bh(&ip6ip6_lock);
	for (tp = ip6ip6_bucket(&t->parms); *tp; tp = &(*tp)->next) {
		if (t == *tp) {
			*tp = t->next;
			break;
		}
	}
	write_unlock_bh(&ip6ip6_lock);
}

/**
 * ip6ip6_tnl_create() - create a new tunnel
 *   @p: tunnel parameters
 *   @pt: pointer to new tunnel
 *
 * Description:
 *   Create tunnel matching given parameters. New kernel managed devices are 
 *   not put in the normal hash structure, but are instead cached for later
 *   use.
 * 
 * Return: 
 *   0 on success
 **/


static int __ip6ip6_tnl_create(struct ip6_tnl_parm *p,
			       struct ip6_tnl **pt,
			       int kernel_list)
{
	struct net_device *dev;
	int err = -ENOBUFS;
	struct ip6_tnl *t;

	dev = kmalloc(sizeof (*dev) + sizeof (*t), GFP_KERNEL);
	if (!dev) {
		return err;
	}
	memset(dev, 0, sizeof (*dev) + sizeof (*t));
	dev->priv = (void *) (dev + 1);
	t = (struct ip6_tnl *) dev->priv;
	t->dev = dev;
	dev->init = ip6ip6_tnl_dev_init;
	if (kernel_list) {
		memcpy(t->parms.name, p->name, IFNAMSIZ - 1);
		t->parms.proto = IPPROTO_IPV6;
		t->parms.flags = IP6_TNL_F_KERNEL_DEV;
	} else {
		memcpy(&t->parms, p, sizeof (*p));
	}
	t->parms.name[IFNAMSIZ - 1] = '\0';
	strcpy(dev->name, t->parms.name);
	if (!dev->name[0]) {
		int i;
		for (i = 0; i < IP6_TNL_MAX; i++) {
			sprintf(dev->name, "ip6tnl%d", i);
			if (__dev_get_by_name(dev->name) == NULL)
				break;
		}

		if (i == IP6_TNL_MAX) {
			goto failed;
		}
		memcpy(t->parms.name, dev->name, IFNAMSIZ);
	}
	if ((err = register_netdevice(dev)) < 0) {
		goto failed;
	}
	dev_hold(dev);
	if (kernel_list) {
		ip6ip6_kernel_tnl_link(t);
	} else {
		ip6ip6_tnl_link(t);
	}
	*pt = t;
	return 0;
failed:
	kfree(dev);
	return err;
}


int ip6ip6_tnl_create(struct ip6_tnl_parm *p, struct ip6_tnl **pt)
{
	return __ip6ip6_tnl_create(p, pt, 0);
}


static void manage_kernel_tnls(void *foo);

DECLARE_WORK(kernel_tnl_manager, manage_kernel_tnls, NULL);

/**
 * manage_kernel_tnls() - create and destroy kernel tunnels
 *
 * Description:
 *   manage_kernel_tnls() creates new kernel devices if there
 *   are less than $min_kdev_count of them and deletes old ones if
 *   there are less than $max_kdev_count of them in the cache
 *
 * Note:
 *   Schedules itself to be run later in process context if called from 
 *   interrupt. Therefore only works synchronously when called from process 
 *   context.
 **/

static void
manage_kernel_tnls(void *foo)
{
	struct ip6_tnl *t = NULL;
	struct ip6_tnl_parm parm;

	/* We can't do this processing in interrupt 
	   context so schedule it for later */
	if (in_interrupt()) {
		read_lock(&ip6ip6_kernel_lock);
		if (!shutdown &&
		    (kdev_count < min_kdev_count ||
		     kdev_count > max_kdev_count)) {
			schedule_work(&kernel_tnl_manager);
		}
		read_unlock(&ip6ip6_kernel_lock);
		return;
	}

	rtnl_lock();
	read_lock_bh(&ip6ip6_kernel_lock);
	memset(&parm, 0, sizeof (parm));
	parm.flags = IP6_TNL_F_KERNEL_DEV;
	/* Create tunnels until there are at least min_kdev_count */
	while (kdev_count < min_kdev_count) {
		read_unlock_bh(&ip6ip6_kernel_lock);
		if (!__ip6ip6_tnl_create(&parm, &t, 1)) {
			dev_open(t->dev);
		} else {
			goto err;
		}
		read_lock_bh(&ip6ip6_kernel_lock);
	}

	/* Destroy tunnels until there are at most max_kdev_count */
	while (kdev_count > max_kdev_count) {
		read_unlock_bh(&ip6ip6_kernel_lock);
		if ((t = ip6ip6_kernel_tnl_unlink()) != NULL) {
			// KK ipv6_tnl_destroy(t); DEVEL code has this.
			unregister_netdevice(t->dev);
		} else {
			goto err;
		}
		read_lock_bh(&ip6ip6_kernel_lock);
	}
	read_unlock_bh(&ip6ip6_kernel_lock);
err:
	rtnl_unlock();
}

/**
 * ip6ip6_tnl_inc_max_kdev_count() - increase max kernel dev cache size
 *   @n: size increase
 * Description:
 *   Increase the upper limit for the number of kernel devices allowed in the 
 *   cache at any on time.
 **/

unsigned int
ip6ip6_tnl_inc_max_kdev_count(unsigned int n)
{
	write_lock_bh(&ip6ip6_kernel_lock);
	max_kdev_count += n;
	write_unlock_bh(&ip6ip6_kernel_lock);
	manage_kernel_tnls(NULL);
	return max_kdev_count;
}

/**
 * ip6ip6_tnl_dec_max_kdev_count() -  decrease max kernel dev cache size
 *   @n: size decrement
 * Description:
 *   Decrease the upper limit for the number of kernel devices allowed in the 
 *   cache at any on time.
 **/

unsigned int
ip6ip6_tnl_dec_max_kdev_count(unsigned int n)
{
	write_lock_bh(&ip6ip6_kernel_lock);
	max_kdev_count -= min(max_kdev_count, n);
	if (max_kdev_count < min_kdev_count)
		min_kdev_count = max_kdev_count;
	write_unlock_bh(&ip6ip6_kernel_lock);
	manage_kernel_tnls(NULL);
	return max_kdev_count;
}

/**
 * ip6ip6_tnl_inc_min_kdev_count() - increase min kernel dev cache size
 *   @n: size increase
 * Description:
 *   Increase the lower limit for the number of kernel devices allowed in the 
 *   cache at any on time.
 **/

unsigned int
ip6ip6_tnl_inc_min_kdev_count(unsigned int n)
{
	write_lock_bh(&ip6ip6_kernel_lock);
	min_kdev_count += n;
	if (min_kdev_count > max_kdev_count)
		max_kdev_count = min_kdev_count;
	write_unlock_bh(&ip6ip6_kernel_lock);
	manage_kernel_tnls(NULL);
	return min_kdev_count;
}

/**
 * ip6ip6_tnl_dec_min_kdev_count() -  decrease min kernel dev cache size
 *   @n: size decrement
 * Description:
 *   Decrease the lower limit for the number of kernel devices allowed in the 
 *   cache at any on time.
 **/

unsigned int
ip6ip6_tnl_dec_min_kdev_count(unsigned int n)
{
	write_lock_bh(&ip6ip6_kernel_lock);
	min_kdev_count -= min(min_kdev_count, n);
	write_unlock_bh(&ip6ip6_kernel_lock);
	manage_kernel_tnls(NULL);
	return min_kdev_count;
}

/**
 * ip6ip6_tnl_locate - find or create tunnel matching given parameters
 *   @p: tunnel parameters 
 *   @create: != 0 if allowed to create new tunnel if no match found
 *
 * Description:
 *   ip6ip6_tnl_locate() first tries to locate an existing tunnel
 *   based on @parms. If this is unsuccessful, but @create is set a new
 *   tunnel device is created and registered for use.
 *
 * Return:
 *   0 if tunnel located or created,
 *   -EINVAL if parameters incorrect,
 *   -ENODEV if no matching tunnel available
 **/

int ip6ip6_tnl_locate(struct ip6_tnl_parm *p, struct ip6_tnl **pt, int create)
{
	struct in6_addr *remote = &p->raddr;
	struct in6_addr *local = &p->laddr;
	struct ip6_tnl *t;

	if (p->proto != IPPROTO_IPV6)
		return -EINVAL;

	for (t = *ip6ip6_bucket(p); t; t = t->next) {
		if (!ipv6_addr_cmp(local, &t->parms.laddr) &&
		    !ipv6_addr_cmp(remote, &t->parms.raddr)) {
			*pt = t;
			return (create ? -EEXIST : 0);
		}
	}
	return ip6ip6_tnl_create(p, pt);
}

/**
 * ip6ip6_tnl_dev_destructor - tunnel device destructor
 *   @dev: the device to be destroyed
 **/

static void
ip6ip6_tnl_dev_destructor(struct net_device *dev)
{
}

/**
 * ip6ip6_tnl_dev_uninit - tunnel device uninitializer
 *   @dev: the device to be destroyed
 *   
 * Description:
 *   ip6ip6_tnl_dev_uninit() removes tunnel from its list
 **/

static void
ip6ip6_tnl_dev_uninit(struct net_device *dev)
{
	struct ip6_tnl *t = (struct ip6_tnl *) dev->priv;

	if (dev == &ip6ip6_fb_tnl_dev) {
		write_lock_bh(&ip6ip6_lock);
		tnls_wc[0] = NULL;
		write_unlock_bh(&ip6ip6_lock);
	} else {
		ip6ip6_tnl_unlink(t);
	}
	sock_release(t->sock);
	dev_put(dev);
}

/**
 * parse_tvl_tnl_enc_lim - handle encapsulation limit option
 *   @skb: received socket buffer
 *
 * Return: 
 *   0 if none was found, 
 *   else index to encapsulation limit
 **/

static __u16
parse_tlv_tnl_enc_lim(struct sk_buff *skb, __u8 * raw)
{
	struct ipv6hdr *ipv6h = (struct ipv6hdr *) raw;
	__u8 nexthdr = ipv6h->nexthdr;
	__u16 off = sizeof (*ipv6h);

	while (ipv6_ext_hdr(nexthdr) && nexthdr != NEXTHDR_NONE) {
		__u16 optlen = 0;
		struct ipv6_opt_hdr *hdr;
		if (raw + off + sizeof (*hdr) > skb->data &&
		    !pskb_may_pull(skb, raw - skb->data + off + sizeof (*hdr)))
			break;

		hdr = (struct ipv6_opt_hdr *) (raw + off);
		if (nexthdr == NEXTHDR_FRAGMENT) {
			struct frag_hdr *frag_hdr = (struct frag_hdr *) hdr;
			if (frag_hdr->frag_off)
				break;
			optlen = 8;
		} else if (nexthdr == NEXTHDR_AUTH) {
			optlen = (hdr->hdrlen + 2) << 2;
		} else {
			optlen = ipv6_optlen(hdr);
		}
		if (nexthdr == NEXTHDR_DEST) {
			__u16 i = off + 2;
			while (1) {
				struct ipv6_tlv_tnl_enc_lim *tel;

				/* No more room for encapsulation limit */
				if (i + sizeof (*tel) > off + optlen)
					break;

				tel = (struct ipv6_tlv_tnl_enc_lim *) &raw[i];
				/* return index of option if found and valid */
				if (tel->type == IPV6_TLV_TNL_ENCAP_LIMIT &&
				    tel->length == 1)
					return i;
				/* else jump to next option */
				if (tel->type)
					i += tel->length + 2;
				else
					i++;
			}
		}
		nexthdr = hdr->nexthdr;
		off += optlen;
	}
	return 0;
}

/**
 * ip6ip6_err - tunnel error handler
 *
 * Description:
 *   ip6ip6_err() should handle errors in the tunnel according
 *   to the specifications in RFC 2473.
 **/

void ip6ip6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
		   int type, int code, int offset, __u32 info)
{
	struct ipv6hdr *ipv6h = (struct ipv6hdr *) skb->data;
	struct ip6_tnl *t;
	int rel_msg = 0;
	int rel_type = ICMPV6_DEST_UNREACH;
	int rel_code = ICMPV6_ADDR_UNREACH;
	__u32 rel_info = 0;
	__u16 len;

	/* If the packet doesn't contain the original IPv6 header we are 
	   in trouble since we might need the source address for furter 
	   processing of the error. */

	read_lock(&ip6ip6_lock);
	if ((t = ip6ip6_tnl_lookup(&ipv6h->daddr, &ipv6h->saddr)) == NULL)
		goto out;

	switch (type) {
		__u32 teli;
		struct ipv6_tlv_tnl_enc_lim *tel;
		__u32 mtu;
	case ICMPV6_DEST_UNREACH:
		if (net_ratelimit())
			printk(KERN_WARNING
			       "%s: Path to destination invalid "
			       "or inactive!\n", t->parms.name);
		rel_msg = 1;
		break;
	case ICMPV6_TIME_EXCEED:
		if (code == ICMPV6_EXC_HOPLIMIT) {
			if (net_ratelimit())
				printk(KERN_WARNING
				       "%s: Too small hop limit or "
				       "routing loop in tunnel!\n", 
				       t->parms.name);
			rel_msg = 1;
		}
		break;
	case ICMPV6_PARAMPROB:
		/* ignore if parameter problem not caused by a tunnel
		   encapsulation limit sub-option */
		if (code != ICMPV6_HDR_FIELD) {
			break;
		}
		teli = parse_tlv_tnl_enc_lim(skb, skb->data);

		if (teli && teli == ntohl(info) - 2) {
			tel = (struct ipv6_tlv_tnl_enc_lim *) &skb->data[teli];
			if (tel->encap_limit == 0) {
				if (net_ratelimit())
					printk(KERN_WARNING
					       "%s: Too small encapsulation "
					       "limit or routing loop in "
					       "tunnel!\n", t->parms.name);
				rel_msg = 1;
			}
		}
		break;
	case ICMPV6_PKT_TOOBIG:
		mtu = ntohl(info) - offset;
		if (mtu < IPV6_MIN_MTU)
			mtu = IPV6_MIN_MTU;
		t->dev->mtu = mtu;

		if ((len = sizeof (*ipv6h) + ipv6h->payload_len) > mtu) {
			rel_type = ICMPV6_PKT_TOOBIG;
			rel_code = 0;
			rel_info = mtu;
			rel_msg = 1;
		}
		break;
	}
	if (rel_msg && pskb_may_pull(skb, offset + sizeof (*ipv6h))) {
		struct rt6_info *rt;
		struct sk_buff *skb2 = skb_clone(skb, GFP_ATOMIC);
		if (!skb2)
			goto out;

		dst_release(skb2->dst);
		skb2->dst = NULL;
		skb_pull(skb2, offset);
		skb2->nh.raw = skb2->data;

		/* Try to guess incoming interface */
		rt = rt6_lookup(&skb2->nh.ipv6h->saddr, NULL, 0, 0);

		if (rt && rt->rt6i_dev)
			skb2->dev = rt->rt6i_dev;

		icmpv6_send(skb2, rel_type, rel_code, rel_info, skb2->dev);

		if (rt)
			dst_release(&rt->u.dst);

		kfree_skb(skb2);
	}
out:
	read_unlock(&ip6ip6_lock);
}

/**
 * call_hooks - call ipv6 tunnel hooks
 *   @hooknum: hook number, either %IP6_TNL_PRE_ENCAP, or 
 *   %IP6_TNL_PRE_DECAP
 *   @t: the current tunnel
 *   @skb: the tunneled packet
 *
 * Description:
 *   Pass packet to all the hook functions until %IP6_TNL_DROP
 *
 * Return:
 *   %IP6_TNL_ACCEPT or %IP6_TNL_DROP
 **/

static inline int
call_hooks(unsigned int hooknum, struct ip6_tnl *t, struct sk_buff *skb)
{
	struct ip6_tnl_hook_ops *h;
	int accept = IP6_TNL_ACCEPT;

	if (hooknum < IP6_TNL_MAXHOOKS) {
		struct list_head *i;
		read_lock(&ip6ip6_hook_lock);
		for (i = hooks[hooknum].next; i != &hooks[hooknum]; i = i->next) {
			h = (struct ip6_tnl_hook_ops *) i;

			if (h->hook) {
				accept = h->hook(t, skb);

				if (accept != IP6_TNL_ACCEPT)
					break;
			}
		}
		read_unlock(&ip6ip6_hook_lock);
	}
	return accept;
}

/**
 * ip6ip6_rcv - decapsulate IPv6 packet and retransmit it locally
 *   @skb: received socket buffer
 *
 * Return: 0
 **/

int ip6ip6_rcv(struct sk_buff **skbp, unsigned int *nhoffp)
{
	struct ipv6hdr *ipv6h;
	struct ip6_tnl *t;
	struct sk_buff *skb = *skbp;

	if (!pskb_may_pull(skb, sizeof (*ipv6h)))
		goto discard;

	ipv6h = skb->nh.ipv6h;

	read_lock(&ip6ip6_lock);

	if ((t = ip6ip6_tnl_lookup(&ipv6h->saddr, &ipv6h->daddr)) != NULL) {
		if (!(t->parms.flags & IP6_TNL_F_CAP_RCV) ||
		    call_hooks(IP6_TNL_PRE_DECAP, t, skb) != IP6_TNL_ACCEPT) {
			t->stat.rx_dropped++;
			read_unlock(&ip6ip6_lock);
			goto discard;
		}
		skb->mac.raw = skb->nh.raw;
		skb->nh.raw = skb->data;
		skb->protocol = htons(ETH_P_IPV6);
		skb->pkt_type = PACKET_HOST;
		memset(skb->cb, 0, sizeof(struct inet6_skb_parm));
		skb->dev = t->dev;
		dst_release(skb->dst);
		skb->dst = NULL;
		t->stat.rx_packets++;
		t->stat.rx_bytes += skb->len;
		netif_rx(skb);
		read_unlock(&ip6ip6_lock);
		return 0;
	}
	read_unlock(&ip6ip6_lock);
	icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_ADDR_UNREACH, 0, skb->dev);
discard:
	kfree_skb(skb);
	return 0;
}

static inline struct ipv6_txoptions *create_tel(__u8 encap_limit)
{
	struct ipv6_tlv_tnl_enc_lim *tel;
	struct ipv6_txoptions *opt;
	__u8 *raw;

	int opt_len = sizeof(*opt) + IPV6_TLV_TEL_DST_SIZE;

	if (!(opt = kmalloc(opt_len, GFP_ATOMIC))) {
		return NULL;
	}
	memset(opt, 0, opt_len);
	opt->tot_len = opt_len;
	opt->dst0opt = (struct ipv6_opt_hdr *) (opt + 1);
	opt->opt_nflen = 8;

	tel = (struct ipv6_tlv_tnl_enc_lim *) (opt->dst0opt + 1);
	tel->type = IPV6_TLV_TNL_ENCAP_LIMIT;
	tel->length = 1;
	tel->encap_limit = encap_limit;

	raw = (__u8 *) opt->dst0opt;
	raw[5] = IPV6_TLV_PADN;
	raw[6] = 1;

	return opt;
}

static int
ip6ip6_getfrag(const void *data, struct in6_addr *addr,
		  char *buff, unsigned int offset, unsigned int len)
{
	memcpy(buff, data + offset, len);
	return 0;
}

/**
 * ip6ip6_tnl_addr_conflict - compare packet addresses to tunnel's own
 *   @t: the outgoing tunnel device
 *   @hdr: IPv6 header from the incoming packet 
 *
 * Description:
 *   Avoid trivial tunneling loop by checking that tunnel exit-point 
 *   doesn't match source of incoming packet.
 *
 * Return: 
 *   1 if conflict,
 *   0 else
 **/

static inline int
ip6ip6_tnl_addr_conflict(struct ip6_tnl *t, struct ipv6hdr *hdr)
{
	return !ipv6_addr_cmp(&t->parms.raddr, &hdr->saddr);
}

// KK : Not used.
static inline void ip6_tnl_dst_store(struct ip6_tnl *t, struct dst_entry *dst)
{
	// KK struct rt6_info *rt = (struct rt6_info *) dst;

	// KK t->dst_cookie = rt->rt6i_node ? rt->rt6i_node->fn_sernum : 0;
	dst_release(t->dst_cache);
	t->dst_cache = dst;
}

extern int ip6_build_xmit(struct sock *sk, inet_getfrag_t getfrag,
		const void *data, struct flowi *fl, unsigned length,
		struct ipv6_txoptions *opt, int hlimit, int flags);

/**
 * ip6ip6_tnl_xmit - encapsulate packet and send 
 *   @skb: the outgoing socket buffer
 *   @dev: the outgoing tunnel device 
 *
 * Description:
 *   Build new header and do some sanity checks on the packet before sending
 *   it to ip6_append_data().
 *
 * Return: 
 *   0
 **/

int ip6ip6_tnl_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct net_device *tdev;
	struct ip6_tnl *t = (struct ip6_tnl *) dev->priv;
	struct net_device_stats *stats = &t->stat;
	struct ipv6hdr *ipv6h = skb->nh.ipv6h;
	struct ipv6_txoptions *opt = NULL;
	int encap_limit = -1;
	__u16 offset;
	struct flowi fl;
	int err = 0;
	struct dst_entry *dst;
	u8 proto;
	int max_headroom = sizeof(struct ipv6hdr);
	struct sock *sk = t->sock->sk;
	struct ipv6_pinfo *np = inet6_sk(sk);
	int mtu;

	if (t->recursion++) {
		stats->collisions++;
		goto tx_err;
	}
	if (skb->protocol != htons(ETH_P_IPV6) ||
	    !(t->parms.flags & IP6_TNL_F_CAP_XMIT) ||
	    ip6ip6_tnl_addr_conflict(t, ipv6h)) {
		goto tx_err;
	}
	if ((offset = parse_tlv_tnl_enc_lim(skb, skb->nh.raw)) > 0) {
		struct ipv6_tlv_tnl_enc_lim *tel;
		tel = (struct ipv6_tlv_tnl_enc_lim *) &skb->nh.raw[offset];
		if (tel->encap_limit == 0) {
			icmpv6_send(skb, ICMPV6_PARAMPROB,
				    ICMPV6_HDR_FIELD, offset + 2, skb->dev);
			goto tx_err;
		}
		encap_limit = tel->encap_limit - 1;
	} else if (!(t->parms.flags & IP6_TNL_F_IGN_ENCAP_LIMIT)) {
		encap_limit = t->parms.encap_limit;
	}
	if (call_hooks(IP6_TNL_PRE_ENCAP, t, skb) != IP6_TNL_ACCEPT)
		goto discard;
	memcpy(&fl, &t->fl, sizeof (fl));
	proto = fl.proto;

	if ((t->parms.flags & IP6_TNL_F_USE_ORIG_TCLASS))
		fl.fl6_flowlabel |= (*(__u32 *) ipv6h & IPV6_TCLASS_MASK);
	if ((t->parms.flags & IP6_TNL_F_USE_ORIG_FLOWLABEL))
		fl.fl6_flowlabel |= (*(__u32 *) ipv6h & IPV6_FLOWLABEL_MASK);

	if (encap_limit >= 0 && (opt = create_tel(encap_limit)) == NULL)
		goto tx_err;

#if 1
	dst = __sk_dst_check(sk, np->dst_cookie);
#else
	// KK : following is in 2.6.2 code.
	if ((dst = ip6_tnl_dst_check(t)) != NULL)
                dst_hold(dst);
        else
                dst = ip6_route_output(NULL, &fl);
#endif
	if (dst) {
		if (np->daddr_cache == NULL ||
		    ipv6_addr_cmp(&fl.fl6_dst, np->daddr_cache) ||
#ifdef CONFIG_IPV6_SUBTREES
		    np->saddr_cache == NULL ||
		    ipv6_addr_cmp(&fl.fl6_src, np->saddr_cache) ||
#endif
		    (fl.oif && fl.oif != dst->dev->ifindex)) {
			dst = NULL;
		} else {
			dst_hold(dst);
		}
	}
	if (dst == NULL) {
		dst = ip6_route_output(sk, &fl);
		if (dst->error) {
			stats->tx_carrier_errors++;
			dst_link_failure(skb);
			goto tx_err_dst_release;
		}
	}
	if (dst->error || xfrm_lookup(&dst, &fl, NULL, 0) < 0)
		goto tx_err_link_failure;

	tdev = dst->dev;
	if (tdev == dev) {
		stats->collisions++;
		if (net_ratelimit())
			printk(KERN_WARNING "%s: Local routing loop detected!\n",
				t->parms.name);
		goto tx_err_dst_release;
	}

	mtu = dst_pmtu(dst) - sizeof (*ipv6h);
	if (opt) {
		max_headroom += 8;
		mtu -= (opt->opt_nflen + opt->opt_flen);
	}
	if (mtu < IPV6_MIN_MTU)
		mtu = IPV6_MIN_MTU;
	if (skb->dst && mtu < dst_pmtu(skb->dst)) {
		struct rt6_info *rt = (struct rt6_info *) skb->dst;
		rt->rt6i_flags |= RTF_MODIFIED;
		rt->u.dst.metrics[RTAX_MTU-1] = mtu;
	}
	if (skb->len > mtu) {
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu, dev);
		goto tx_err_dst_release;
	}
	ip6_dst_store(sk, dst, &np->daddr, &np->saddr);
	skb->h.raw = skb->nh.raw;

	err = ip6_build_xmit(sk, ip6ip6_getfrag, (void *) skb->nh.raw,
			     &fl, skb->len, opt, t->parms.hop_limit,
			     MSG_DONTWAIT);

	if (err == NET_XMIT_SUCCESS || err == NET_XMIT_CN) {
		stats->tx_bytes += skb->len;
		stats->tx_packets++;
	} else {
		stats->tx_errors++;
		stats->tx_aborted_errors++;
	}
	if (opt)
		kfree(opt);
	kfree_skb(skb);
	t->recursion--;
	return 0;
tx_err_link_failure:
        stats->tx_carrier_errors++;
        dst_link_failure(skb);
tx_err_dst_release:
	dst_release(dst);
	if (opt)
		kfree(opt);

tx_err:
	stats->tx_errors++;
discard:
	stats->tx_dropped++;
	kfree_skb(skb);
	t->recursion--;
	return 0;
}

static void ip6_tnl_set_cap(struct ip6_tnl *t)
{
	struct ip6_tnl_parm *p = &t->parms;
	struct in6_addr *laddr = &p->laddr;
	struct in6_addr *raddr = &p->raddr;
	int ltype = ipv6_addr_type(laddr);
	int rtype = ipv6_addr_type(raddr);

	p->flags &= ~(IP6_TNL_F_CAP_XMIT|IP6_TNL_F_CAP_RCV);

	if (ltype != IPV6_ADDR_ANY && rtype != IPV6_ADDR_ANY &&
	    ((ltype|rtype) &
	     (IPV6_ADDR_UNICAST|
	      IPV6_ADDR_LOOPBACK|IPV6_ADDR_LINKLOCAL|
	      IPV6_ADDR_MAPPED|IPV6_ADDR_RESERVED)) == IPV6_ADDR_UNICAST) {
		struct net_device *ldev = NULL;
		int l_ok = 1;
		int r_ok = 1;

		if (p->link)
			ldev = dev_get_by_index(p->link);

		if ((ltype&IPV6_ADDR_UNICAST) && !ipv6_chk_addr(laddr, ldev, 0))
			l_ok = 0;

		if ((rtype&IPV6_ADDR_UNICAST) && ipv6_chk_addr(raddr, NULL, 0))
			r_ok = 0;

		if (l_ok && r_ok) {
			if (ltype&IPV6_ADDR_UNICAST)
				p->flags |= IP6_TNL_F_CAP_XMIT;
			if (rtype&IPV6_ADDR_UNICAST)
				p->flags |= IP6_TNL_F_CAP_RCV;
 		}
		if (ldev)
			dev_put(ldev);
 	}
}

static void ip6ip6_tnl_link_config(struct ip6_tnl *t)
{
	struct net_device *dev = t->dev;
	struct ip6_tnl_parm *p = &t->parms;
	struct flowi *fl = &t->fl;

	/* Set up flowi template */
	ipv6_addr_copy(&fl->fl6_src, &p->laddr);
	ipv6_addr_copy(&fl->fl6_dst, &p->raddr);
	fl->oif = p->link;
	fl->fl6_flowlabel = 0;

	if (!(p->flags&IP6_TNL_F_USE_ORIG_TCLASS))
		fl->fl6_flowlabel |= IPV6_TCLASS_MASK & htonl(p->flowinfo);
	if (!(p->flags&IP6_TNL_F_USE_ORIG_FLOWLABEL))
		fl->fl6_flowlabel |= IPV6_FLOWLABEL_MASK & htonl(p->flowinfo);

	ip6_tnl_set_cap(t);

	// KK From 2.6.1 latest code
	dev->iflink = p->link;

	if (p->flags&IP6_TNL_F_CAP_XMIT && p->flags&IP6_TNL_F_CAP_RCV)
		dev->flags |= IFF_POINTOPOINT;
	else
		dev->flags &= ~IFF_POINTOPOINT;

	if (p->flags & IP6_TNL_F_CAP_XMIT) {
		struct rt6_info *rt = rt6_lookup(&p->raddr, &p->laddr,
						 p->link, 0);
		
		if (rt == NULL)
			return;
		
		if (rt->rt6i_dev) {
			// KK : Above change
			// dev->iflink = rt->rt6i_dev->ifindex;

			dev->hard_header_len = rt->rt6i_dev->hard_header_len +
				sizeof (struct ipv6hdr);

			dev->mtu = rt->rt6i_dev->mtu - sizeof (struct ipv6hdr);

			if (dev->mtu < IPV6_MIN_MTU)
				dev->mtu = IPV6_MIN_MTU;
		}
		dst_release(&rt->u.dst);
	}
}

/**
 * __ip6ip6_tnl_change - update the tunnel parameters
 *   @t: tunnel to be changed
 *   @p: tunnel configuration parameters
 *
 * Description:
 *   __ip6ip6_tnl_change() updates the tunnel parameters
 **/

static void
__ip6ip6_tnl_change(struct ip6_tnl *t, struct ip6_tnl_parm *p)
{
	ipv6_addr_copy(&t->parms.laddr, &p->laddr);
	ipv6_addr_copy(&t->parms.raddr, &p->raddr);
	t->parms.flags = p->flags;
	t->parms.hop_limit = p->hop_limit;
	t->parms.encap_limit = p->encap_limit;
	t->parms.flowinfo = p->flowinfo;
	ip6ip6_tnl_link_config(t);
}

void ip6ip6_tnl_change(struct ip6_tnl *t, struct ip6_tnl_parm *p)
{
	ip6ip6_tnl_unlink(t);
	__ip6ip6_tnl_change(t, p);
	ip6ip6_tnl_link(t);
}

/**
 * ip6ip6_kernel_tnl_add - configure and add kernel tunnel to hash 
 *   @p: kernel tunnel configuration parameters
 *
 * Description:
 *   ip6ip6_kernel_tnl_add() fetches an unused kernel tunnel configures
 *   it according to @p and places it among the active tunnels.
 * 
 * Return:
 *   number of references to tunnel on success,
 *   %-EEXIST if there is already a device matching description
 *   %-EINVAL if p->flags doesn't have %IP6_TNL_F_KERNEL_DEV raised,
 *   %-ENODEV if there are no unused kernel tunnels available 
 * 
 * Note:
 *   The code for creating, opening, closing and destroying network devices
 *   must be called from process context, while the Mobile IP code, which 
 *   needs the tunnel devices, unfortunately runs in interrupt context. 
 *   
 *   The devices must be created and opened in advance, then placed in a 
 *   list where the kernel can fetch and ready them for use at a later time.
 *
 **/

int
ip6ip6_kernel_tnl_add(struct ip6_tnl_parm *p)
{
	struct ip6_tnl *t;

	if (!(p->flags & IP6_TNL_F_KERNEL_DEV))
		return -EINVAL;
	if ((t = ip6ip6_tnl_lookup(&p->raddr, &p->laddr)) != NULL &&
	    t != &ip6ip6_fb_tnl) {
		/* Handle duplicate tunnels by incrementing 
		   reference count */
		atomic_inc(&t->refcnt);
		goto out;
	}
	if ((t = ip6ip6_kernel_tnl_unlink()) == NULL)
		return -ENODEV;
	__ip6ip6_tnl_change(t, p);

	atomic_inc(&t->refcnt);

	ip6ip6_tnl_link(t);

	manage_kernel_tnls(NULL);
out:
	return atomic_read(&t->refcnt);
}

/**
 * ip6ip6_kernel_tnl_del - delete no longer needed kernel tunnel 
 *   @t: kernel tunnel to be removed from hash
 *
 * Description:
 *   ip6ip6_kernel_tnl_del() removes and deconfigures the tunnel @t
 *   and places it among the unused kernel devices.
 * 
 * Return:
 *   number of references on success,
 *   %-EINVAL if p->flags doesn't have %IP6_TNL_F_KERNEL_DEV raised,
 * 
 * Note:
 *   See the comments on ip6ip6_kernel_tnl_add() for more information.
 **/

int
ip6ip6_kernel_tnl_del(struct ip6_tnl *t)
{
	if (!t)
		return -ENODEV;

	if (!(t->parms.flags & IP6_TNL_F_KERNEL_DEV))
		return -EINVAL;

	if (atomic_dec_and_test(&t->refcnt)) {
		struct ip6_tnl_parm p;
		ip6ip6_tnl_unlink(t);
		memset(&p, 0, sizeof (p));
		p.flags = IP6_TNL_F_KERNEL_DEV;

		__ip6ip6_tnl_change(t, &p);

		ip6ip6_kernel_tnl_link(t);

		manage_kernel_tnls(NULL);
	}
	return atomic_read(&t->refcnt);
}

/**
 * ip6ip6_tnl_ioctl - configure ipv6 tunnels from userspace 
 *   @dev: virtual device associated with tunnel
 *   @ifr: parameters passed from userspace
 *   @cmd: command to be performed
 *
 * Description:
 *   ip6ip6_tnl_ioctl() is used for managing IPv6 tunnels 
 *   from userspace. 
 *
 *   The possible commands are the following:
 *     %SIOCGETTUNNEL: get tunnel parameters for device
 *     %SIOCADDTUNNEL: add tunnel matching given tunnel parameters
 *     %SIOCCHGTUNNEL: change tunnel parameters to those given
 *     %SIOCDELTUNNEL: delete tunnel
 *
 *   The fallback device "ip6tnl0", created during module 
 *   initialization, can be used for creating other tunnel devices.
 *
 * Return:
 *   0 on success,
 *   %-EFAULT if unable to copy data to or from userspace,
 *   %-EPERM if current process hasn't %CAP_NET_ADMIN set or attempting
 *   to configure kernel devices from userspace, 
 *   %-EINVAL if passed tunnel parameters are invalid,
 *   %-EEXIST if changing a tunnel's parameters would cause a conflict
 *   %-ENODEV if attempting to change or delete a nonexisting device
 *
 * Note:
 *   See the comments on ip6ip6_kernel_tnl_add() for more information 
 *   about kernel tunnels.
 * **/

static int
ip6ip6_tnl_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	int err = 0;
	int create;
	struct ip6_tnl_parm p;
	struct ip6_tnl *t = NULL;

	switch (cmd) {
	case SIOCGETTUNNEL:
		if (dev == &ip6ip6_fb_tnl_dev) {
			if (copy_from_user(&p,
					   ifr->ifr_ifru.ifru_data,
					   sizeof (p))) {
				err = -EFAULT;
				break;
			}
			if ((err = ip6ip6_tnl_locate(&p, &t, 0)) == -ENODEV)
				t = (struct ip6_tnl *) dev->priv;
			else if (err)
				break;
		} else
			t = (struct ip6_tnl *) dev->priv;

		memcpy(&p, &t->parms, sizeof (p));
		if (copy_to_user(ifr->ifr_ifru.ifru_data, &p, sizeof (p))) {
			err = -EFAULT;
		}
		break;
	case SIOCADDTUNNEL:
	case SIOCCHGTUNNEL:
		err = -EPERM;
		create = (cmd == SIOCADDTUNNEL);
		if (!capable(CAP_NET_ADMIN))
			break;
		if (copy_from_user(&p, ifr->ifr_ifru.ifru_data, sizeof (p))) {
			err = -EFAULT;
			break;
		}
		if (p.flags & IP6_TNL_F_KERNEL_DEV) {
			break;
		}
		if (!create && dev != &ip6ip6_fb_tnl_dev) {
			t = (struct ip6_tnl *) dev->priv;
		}
		if (!t && (err = ip6ip6_tnl_locate(&p, &t, create))) {
			break;
		}
		if (cmd == SIOCCHGTUNNEL) {
			if (t->dev != dev) {
				err = -EEXIST;
				break;
			}
			if (t->parms.flags & IP6_TNL_F_KERNEL_DEV) {
				err = -EPERM;
				break;
			}
			ip6ip6_tnl_change(t, &p);
			netdev_state_change(dev);
		}
		if (copy_to_user(ifr->ifr_ifru.ifru_data,
				 &t->parms, sizeof (p))) {
			err = -EFAULT;
		} else {
			err = 0;
		}
		break;
	case SIOCDELTUNNEL:
		err = -EPERM;
		if (!capable(CAP_NET_ADMIN))
			break;

		if (dev == &ip6ip6_fb_tnl_dev) {
			if (copy_from_user(&p, ifr->ifr_ifru.ifru_data,
					   sizeof (p))) {
				err = -EFAULT;
				break;
			}
			err = ip6ip6_tnl_locate(&p, &t, 0);
			if (err)
				break;
			if (t == &ip6ip6_fb_tnl) {
				err = -EPERM;
				break;
			}
		} else {
			t = (struct ip6_tnl *) dev->priv;
		}
		if (t->parms.flags & IP6_TNL_F_KERNEL_DEV)
			err = -EPERM;
		else
			err = unregister_netdevice(t->dev);
		break;
	default:
		err = -EINVAL;
	}
	return err;
}

/**
 * ip6ip6_tnl_get_stats - return the stats for tunnel device 
 *   @dev: virtual device associated with tunnel
 *
 * Return: stats for device
 **/

static struct net_device_stats *
ip6ip6_tnl_get_stats(struct net_device *dev)
{
	return &(((struct ip6_tnl *) dev->priv)->stat);
}

/**
 * ip6ip6_tnl_change_mtu - change mtu manually for tunnel device
 *   @dev: virtual device associated with tunnel
 *   @new_mtu: the new mtu
 *
 * Return:
 *   0 on success,
 *   %-EINVAL if mtu too small
 **/

static int
ip6ip6_tnl_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu < IPV6_MIN_MTU) {
		return -EINVAL;
	}
	dev->mtu = new_mtu;
	return 0;
}

/**
 * ip6ip6_tnl_dev_init_gen - general initializer for all tunnel devices
 *   @dev: virtual device associated with tunnel
 *
 * Description:
 *   Set function pointers and initialize the &struct flowi template used
 *   by the tunnel.
 **/

static int
ip6ip6_tnl_dev_init_gen(struct net_device *dev)
{
	struct ip6_tnl *t = (struct ip6_tnl *) dev->priv;
	struct flowi *fl = &t->fl;
	int err;
	struct sock *sk;
	struct ipv6_pinfo *pinfo;

	if ((err = sock_create(PF_INET6, SOCK_RAW, IPPROTO_IPV6, &t->sock))) {
		printk(KERN_ERR
		       "Failed to create IPv6 tunnel socket (err %d).\n", err);
		return err;
	}
	sk = t->sock->sk;
	sk->sk_allocation = GFP_ATOMIC;

	pinfo = inet6_sk(sk);
	pinfo->hop_limit = 254;
	pinfo->mc_loop = 0;
	sk->sk_prot->unhash(sk);

	memset(fl, 0, sizeof (*fl));
	fl->proto = IPPROTO_IPV6;

	dev->destructor = ip6ip6_tnl_dev_destructor;
	dev->uninit = ip6ip6_tnl_dev_uninit;
	dev->hard_start_xmit = ip6ip6_tnl_xmit;
	dev->get_stats = ip6ip6_tnl_get_stats;
	dev->do_ioctl = ip6ip6_tnl_ioctl;
	dev->change_mtu = ip6ip6_tnl_change_mtu;

	dev->type = ARPHRD_TUNNEL6;
	dev->hard_header_len = LL_MAX_HEADER + sizeof (struct ipv6hdr);
	dev->mtu = ETH_DATA_LEN - sizeof (struct ipv6hdr);
	dev->flags |= IFF_NOARP;
	// KK : Above change.
	// dev->iflink = 0;
	/* Hmm... MAX_ADDR_LEN is 8, so the ipv6 addresses can't be 
	   copied to dev->dev_addr and dev->broadcast, like the ipv4
	   addresses were in ipip.c, ip_gre.c and sit.c. */
	dev->addr_len = 0;
	return 0;
}

/**
 * ip6ip6_tnl_dev_init - initializer for all non fallback tunnel devices
 *   @dev: virtual device associated with tunnel
 **/

static int
ip6ip6_tnl_dev_init(struct net_device *dev)
{
	struct ip6_tnl *t = (struct ip6_tnl *) dev->priv;
	ip6ip6_tnl_dev_init_gen(dev);
	ip6ip6_tnl_link_config(t);
	return 0;
}

#ifdef MODULE

/**
 * ip6ip6_fb_tnl_open - function called when fallback device opened
 *   @dev: fallback device
 *
 * Return: 0 
 **/

static int
ip6ip6_fb_tnl_open(struct net_device *dev)
{
	return 0;
}

/**
 * ip6ip6_fb_tnl_close - function called when fallback device closed
 *   @dev: fallback device
 *
 * Return: 0 
 **/

static int
ip6ip6_fb_tnl_close(struct net_device *dev)
{
	return 0;
}
#endif

/**
 * ip6ip6_fb_tnl_dev_init - initializer for fallback tunnel device
 *   @dev: fallback device
 *
 * Return: 0
 **/

int __init
ip6ip6_fb_tnl_dev_init(struct net_device *dev)
{
	ip6ip6_tnl_dev_init_gen(dev);
#ifdef MODULE
	dev->open = ip6ip6_fb_tnl_open;
	dev->stop = ip6ip6_fb_tnl_close;
#endif
	dev_hold(dev);
	tnls_wc[0] = &ip6ip6_fb_tnl;
	return 0;
}

/**
 * ip6ip6_tnl_register_hook - add hook for processing of tunneled packets
 *   @reg: hook function and its parameters
 * 
 * Description:
 *   Add a netfilter like hook function for special handling of tunneled 
 *   packets. The hook functions are called before encapsulation 
 *   (%IP6_TNL_PRE_ENCAP) and before decapsulation 
 *   (%IP6_TNL_PRE_DECAP). The possible return values by the hook 
 *   functions are %IP6_TNL_DROP, %IP6_TNL_ACCEPT and 
 *   %IP6_TNL_STOLEN (in case the hook function took care of the packet
 *   and it doesn't have to be processed any further).
 **/

void
ip6ip6_tnl_register_hook(struct ip6_tnl_hook_ops *reg)
{
	if (reg->hooknum < IP6_TNL_MAXHOOKS) {
		struct list_head *i;

		write_lock_bh(&ip6ip6_hook_lock);
		for (i = hooks[reg->hooknum].next;
		     i != &hooks[reg->hooknum]; i = i->next) {
			if (reg->priority <
			    ((struct ip6_tnl_hook_ops *) i)->priority) {
				break;
			}
		}
		list_add(&reg->list, i->prev);
		write_unlock_bh(&ip6ip6_hook_lock);
	}
}

/**
 * ip6ip6_tnl_unregister_hook - remove tunnel hook
 *   @reg: hook function and its parameters
 **/

void
ip6ip6_tnl_unregister_hook(struct ip6_tnl_hook_ops *reg)
{
	if (reg->hooknum < IP6_TNL_MAXHOOKS) {
		write_lock_bh(&ip6ip6_hook_lock);
		list_del(&reg->list);
		write_unlock_bh(&ip6ip6_hook_lock);
	}
}


/* the IPv6 over IPv6 protocol structure */
static struct inet6_protocol ip6ip6_protocol = {
	ip6ip6_rcv,		/* IPv6 handler         */
	ip6ip6_err,		/* IPv6 error control   */
	0,			/* flags		*/
};

/**
 * ip6ip6_tnl_init - register protocol and reserve needed resources
 *
 * Return: 0 on success
 **/

int __init
ip6ip6_tnl_init(void)
{
	int i, err;

	ip6ip6_fb_tnl_dev.priv = (void *) &ip6ip6_fb_tnl;

	for (i = 0; i < IP6_TNL_MAXHOOKS; i++) {
		INIT_LIST_HEAD(&hooks[i]);
	}
	if ((err = register_netdev(&ip6ip6_fb_tnl_dev)))
		return err;

	inet6_add_protocol(&ip6ip6_protocol, IPPROTO_IPV6);
	return 0;
}

#ifdef MODULE
/**
 * ip6ip6_tnl_exit - free resources and unregister protocol
 **/

void __exit
ip6ip6_tnl_exit(void)
{
	write_lock_bh(&ip6ip6_kernel_lock);
	shutdown = 1;
	write_unlock_bh(&ip6ip6_kernel_lock);
	flush_scheduled_work();
	manage_kernel_tnls(NULL);
	inet6_del_protocol(&ip6ip6_protocol, IPPROTO_IPV6);
	unregister_netdev(&ip6ip6_fb_tnl_dev);
}

#ifdef MODULE
module_init(ip6ip6_tnl_init);
module_exit(ip6ip6_tnl_exit);
#endif

#endif

EXPORT_SYMBOL(ip6ip6_kernel_tnl_add);
EXPORT_SYMBOL(ip6ip6_kernel_tnl_del);
EXPORT_SYMBOL(ip6ip6_tnl_change);
EXPORT_SYMBOL(ip6ip6_tnl_create);
EXPORT_SYMBOL(ip6ip6_tnl_dec_max_kdev_count);
EXPORT_SYMBOL(ip6ip6_tnl_dec_min_kdev_count);
EXPORT_SYMBOL(ip6ip6_tnl_inc_max_kdev_count);
EXPORT_SYMBOL(ip6ip6_tnl_inc_min_kdev_count);
EXPORT_SYMBOL(ip6ip6_tnl_lookup);
EXPORT_SYMBOL(ip6ip6_tnl_register_hook);
EXPORT_SYMBOL(ip6ip6_tnl_unregister_hook);
