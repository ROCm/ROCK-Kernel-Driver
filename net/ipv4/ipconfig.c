/*
 *  $Id: ipconfig.c,v 1.34 2000/07/26 01:04:18 davem Exp $
 *
 *  Automatic Configuration of IP -- use BOOTP or RARP or user-supplied
 *  information to configure own IP address and routes.
 *
 *  Copyright (C) 1996--1998 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *  Derived from network configuration code in fs/nfs/nfsroot.c,
 *  originally Copyright (C) 1995, 1996 Gero Kuhlmann and me.
 *
 *  BOOTP rewritten to construct and analyse packets itself instead
 *  of misusing the IP layer. num_bugs_causing_wrong_arp_replies--;
 *					     -- MJ, December 1998
 *  
 *  Fixed ip_auto_config_setup calling at startup in the new "Linker Magic"
 *  initialization scheme.
 *	- Arnaldo Carvalho de Melo <acme@conectiva.com.br>, 08/11/1999
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/utsname.h>
#include <linux/in.h>
#include <linux/if.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/socket.h>
#include <linux/route.h>
#include <linux/udp.h>
#include <net/arp.h>
#include <net/ip.h>
#include <net/ipconfig.h>

#include <asm/uaccess.h>
#include <asm/checksum.h>

/* Define this to allow debugging output */
#undef IPCONFIG_DEBUG

#ifdef IPCONFIG_DEBUG
#define DBG(x) printk x
#else
#define DBG(x) do { } while(0)
#endif

/* Define the timeout for waiting for a RARP/BOOTP reply */
#define CONF_BASE_TIMEOUT	(HZ*5)	/* Initial timeout: 5 seconds */
#define CONF_RETRIES	 	10	/* 10 retries */
#define CONF_TIMEOUT_RANDOM	(HZ)	/* Maximum amount of randomization */
#define CONF_TIMEOUT_MULT	*5/4	/* Rate of timeout growth */
#define CONF_TIMEOUT_MAX	(HZ*30)	/* Maximum allowed timeout */

/* IP configuration */
static char user_dev_name[IFNAMSIZ] __initdata = { 0, };/* Name of user-selected boot device */
u32 ic_myaddr __initdata = INADDR_NONE;		/* My IP address */
u32 ic_servaddr __initdata = INADDR_NONE;	/* Server IP address */
u32 ic_gateway __initdata = INADDR_NONE;	/* Gateway IP address */
u32 ic_netmask __initdata = INADDR_NONE;	/* Netmask for local subnet */
int ic_enable __initdata = 1;			/* Automatic IP configuration enabled */
int ic_host_name_set __initdata = 0;		/* Host name configured manually */
int ic_set_manually __initdata = 0;		/* IPconfig parameters set manually */

u32 root_server_addr __initdata = INADDR_NONE;		/* Address of boot server */
u8 root_server_path[256] __initdata = { 0, };		/* Path to mount as root */

#if defined(CONFIG_IP_PNP_BOOTP) || defined(CONFIG_IP_PNP_RARP)

#define CONFIG_IP_PNP_DYNAMIC

int ic_proto_enabled __initdata = 0			/* Protocols enabled */
#ifdef CONFIG_IP_PNP_BOOTP
			| IC_BOOTP
#endif
#ifdef CONFIG_IP_PNP_RARP
			| IC_RARP
#endif
			;
static int ic_got_reply __initdata = 0;				/* Protocol(s) we got reply from */

#else

static int ic_proto_enabled __initdata = 0;

#endif

static int ic_proto_have_if __initdata = 0;

/*
 *	Network devices
 */

struct ic_device {
	struct ic_device *next;
	struct net_device *dev;
	unsigned short flags;
	int able;
};

static struct ic_device *ic_first_dev __initdata = NULL;/* List of open device */
static struct net_device *ic_dev __initdata = NULL;		/* Selected device */

static int __init ic_open_devs(void)
{
	struct ic_device *d, **last;
	struct net_device *dev;
	unsigned short oflags;

	last = &ic_first_dev;
	rtnl_shlock();
	for (dev = dev_base; dev; dev = dev->next) {
		if (user_dev_name[0] ? !strcmp(dev->name, user_dev_name) :
		    (!(dev->flags & IFF_LOOPBACK) &&
		     (dev->flags & (IFF_POINTOPOINT|IFF_BROADCAST)) &&
		     strncmp(dev->name, "dummy", 5))) {
			int able = 0;
			if (dev->mtu >= 364)
				able |= IC_BOOTP;
			else
				printk(KERN_WARNING "BOOTP: Ignoring device %s, MTU %d too small", dev->name, dev->mtu);
			if (!(dev->flags & IFF_NOARP))
				able |= IC_RARP;
			able &= ic_proto_enabled;
			if (ic_proto_enabled && !able)
				continue;
			oflags = dev->flags;
			if (dev_change_flags(dev, oflags | IFF_UP) < 0) {
				printk(KERN_ERR "IP-Config: Failed to open %s\n", dev->name);
				continue;
			}
			if (!(d = kmalloc(sizeof(struct ic_device), GFP_KERNEL)))
				return -1;
			d->dev = dev;
			*last = d;
			last = &d->next;
			d->flags = oflags;
			d->able = able;
			ic_proto_have_if |= able;
			DBG(("IP-Config: Opened %s (able=%d)\n", dev->name, able));
		}
	}
	rtnl_shunlock();

	*last = NULL;

	if (!ic_first_dev) {
		if (user_dev_name[0])
			printk(KERN_ERR "IP-Config: Device `%s' not found.\n", user_dev_name);
		else
			printk(KERN_ERR "IP-Config: No network devices available.\n");
		return -1;
	}
	return 0;
}

static void __init ic_close_devs(void)
{
	struct ic_device *d, *next;
	struct net_device *dev;

	rtnl_shlock();
	next = ic_first_dev;
	while ((d = next)) {
		next = d->next;
		dev = d->dev;
		if (dev != ic_dev) {
			DBG(("IP-Config: Downing %s\n", dev->name));
			dev_change_flags(dev, d->flags);
		}
		kfree(d);
	}
	rtnl_shunlock();
}

/*
 *	Interface to various network functions.
 */

static inline void
set_sockaddr(struct sockaddr_in *sin, u32 addr, u16 port)
{
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = addr;
	sin->sin_port = port;
}

static int __init ic_dev_ioctl(unsigned int cmd, struct ifreq *arg)
{
	int res;

	mm_segment_t oldfs = get_fs();
	set_fs(get_ds());
	res = devinet_ioctl(cmd, arg);
	set_fs(oldfs);
	return res;
}

static int __init ic_route_ioctl(unsigned int cmd, struct rtentry *arg)
{
	int res;

	mm_segment_t oldfs = get_fs();
	set_fs(get_ds());
	res = ip_rt_ioctl(cmd, arg);
	set_fs(oldfs);
	return res;
}

/*
 *	Set up interface addresses and routes.
 */

static int __init ic_setup_if(void)
{
	struct ifreq ir;
	struct sockaddr_in *sin = (void *) &ir.ifr_ifru.ifru_addr;
	int err;

	memset(&ir, 0, sizeof(ir));
	strcpy(ir.ifr_ifrn.ifrn_name, ic_dev->name);
	set_sockaddr(sin, ic_myaddr, 0);
	if ((err = ic_dev_ioctl(SIOCSIFADDR, &ir)) < 0) {
		printk(KERN_ERR "IP-Config: Unable to set interface address (%d).\n", err);
		return -1;
	}
	set_sockaddr(sin, ic_netmask, 0);
	if ((err = ic_dev_ioctl(SIOCSIFNETMASK, &ir)) < 0) {
		printk(KERN_ERR "IP-Config: Unable to set interface netmask (%d).\n", err);
		return -1;
	}
	set_sockaddr(sin, ic_myaddr | ~ic_netmask, 0);
	if ((err = ic_dev_ioctl(SIOCSIFBRDADDR, &ir)) < 0) {
		printk(KERN_ERR "IP-Config: Unable to set interface broadcast address (%d).\n", err);
		return -1;
	}
	return 0;
}

static int __init ic_setup_routes(void)
{
	/* No need to setup device routes, only the default route... */

	if (ic_gateway != INADDR_NONE) {
		struct rtentry rm;
		int err;

		memset(&rm, 0, sizeof(rm));
		if ((ic_gateway ^ ic_myaddr) & ic_netmask) {
			printk(KERN_ERR "IP-Config: Gateway not on directly connected network.\n");
			return -1;
		}
		set_sockaddr((struct sockaddr_in *) &rm.rt_dst, 0, 0);
		set_sockaddr((struct sockaddr_in *) &rm.rt_genmask, 0, 0);
		set_sockaddr((struct sockaddr_in *) &rm.rt_gateway, ic_gateway, 0);
		rm.rt_flags = RTF_UP | RTF_GATEWAY;
		if ((err = ic_route_ioctl(SIOCADDRT, &rm)) < 0) {
			printk(KERN_ERR "IP-Config: Cannot add default route (%d).\n", err);
			return -1;
		}
	}

	return 0;
}

/*
 *	Fill in default values for all missing parameters.
 */

static int __init ic_defaults(void)
{
	/*
	 *	At this point we have no userspace running so need not
	 *	claim locks on system_utsname
	 */
	 
	if (!ic_host_name_set)
		strcpy(system_utsname.nodename, in_ntoa(ic_myaddr));

	if (root_server_addr == INADDR_NONE)
		root_server_addr = ic_servaddr;

	if (ic_netmask == INADDR_NONE) {
		if (IN_CLASSA(ntohl(ic_myaddr)))
			ic_netmask = htonl(IN_CLASSA_NET);
		else if (IN_CLASSB(ntohl(ic_myaddr)))
			ic_netmask = htonl(IN_CLASSB_NET);
		else if (IN_CLASSC(ntohl(ic_myaddr)))
			ic_netmask = htonl(IN_CLASSC_NET);
		else {
			printk(KERN_ERR "IP-Config: Unable to guess netmask for address %u.%u.%u.%u\n",
				NIPQUAD(ic_myaddr));
			return -1;
		}
		printk("IP-Config: Guessing netmask %u.%u.%u.%u\n", NIPQUAD(ic_netmask));
	}

	return 0;
}

/*
 *	RARP support.
 */

#ifdef CONFIG_IP_PNP_RARP

static int ic_rarp_recv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt);

static struct packet_type rarp_packet_type __initdata = {
	__constant_htons(ETH_P_RARP),
	NULL,			/* Listen to all devices */
	ic_rarp_recv,
	NULL,
	NULL
};

static inline void ic_rarp_init(void)
{
	dev_add_pack(&rarp_packet_type);
}

static inline void ic_rarp_cleanup(void)
{
	dev_remove_pack(&rarp_packet_type);
}

/*
 *  Process received RARP packet.
 */
static int __init
ic_rarp_recv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt)
{
	struct arphdr *rarp = (struct arphdr *)skb->h.raw;
	unsigned char *rarp_ptr = (unsigned char *) (rarp + 1);
	unsigned long sip, tip;
	unsigned char *sha, *tha;		/* s for "source", t for "target" */

	/* If we already have a reply, just drop the packet */
	if (ic_got_reply)
		goto drop;

	/* If this test doesn't pass, it's not IP, or we should ignore it anyway */
	if (rarp->ar_hln != dev->addr_len || dev->type != ntohs(rarp->ar_hrd))
		goto drop;

	/* If it's not a RARP reply, delete it. */
	if (rarp->ar_op != htons(ARPOP_RREPLY))
		goto drop;

	/* If it's not Ethernet, delete it. */
	if (rarp->ar_pro != htons(ETH_P_IP))
		goto drop;

	/* Extract variable-width fields */
	sha = rarp_ptr;
	rarp_ptr += dev->addr_len;
	memcpy(&sip, rarp_ptr, 4);
	rarp_ptr += 4;
	tha = rarp_ptr;
	rarp_ptr += dev->addr_len;
	memcpy(&tip, rarp_ptr, 4);

	/* Discard packets which are not meant for us. */
	if (memcmp(tha, dev->dev_addr, dev->addr_len))
		goto drop;

	/* Discard packets which are not from specified server. */
	if (ic_servaddr != INADDR_NONE && ic_servaddr != sip)
		goto drop;

	/* Victory! The packet is what we were looking for! */
	if (!ic_got_reply) {
		ic_got_reply = IC_RARP;
		ic_dev = dev;
		if (ic_myaddr == INADDR_NONE)
			ic_myaddr = tip;
		ic_servaddr = sip;
	}

	/* And throw the packet out... */
drop:
	kfree_skb(skb);
	return 0;
}


/*
 *  Send RARP request packet over all devices which allow RARP.
 */
static void __init ic_rarp_send(void)
{
	struct ic_device *d;

	for (d=ic_first_dev; d; d=d->next)
		if (d->able & IC_RARP) {
			struct net_device *dev = d->dev;
			arp_send(ARPOP_RREQUEST, ETH_P_RARP, 0, dev, 0, NULL,
				 dev->dev_addr, dev->dev_addr);
		}
}

#endif

/*
 *	BOOTP support.
 */

#ifdef CONFIG_IP_PNP_BOOTP

struct bootp_pkt {		/* BOOTP packet format */
	struct iphdr iph;	/* IP header */
	struct udphdr udph;	/* UDP header */
	u8 op;			/* 1=request, 2=reply */
	u8 htype;		/* HW address type */
	u8 hlen;		/* HW address length */
	u8 hops;		/* Used only by gateways */
	u32 xid;		/* Transaction ID */
	u16 secs;		/* Seconds since we started */
	u16 flags;		/* Just what it says */
	u32 client_ip;		/* Client's IP address if known */
	u32 your_ip;		/* Assigned IP address */
	u32 server_ip;		/* Server's IP address */
	u32 relay_ip;		/* IP address of BOOTP relay */
	u8 hw_addr[16];		/* Client's HW address */
	u8 serv_name[64];	/* Server host name */
	u8 boot_file[128];	/* Name of boot file */
	u8 vendor_area[128];	/* Area for extensions */
};

#define BOOTP_REQUEST 1
#define BOOTP_REPLY 2

static u32 ic_bootp_xid;

static int ic_bootp_recv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt);

static struct packet_type bootp_packet_type __initdata = {
	__constant_htons(ETH_P_IP),
	NULL,			/* Listen to all devices */
	ic_bootp_recv,
	NULL,
	NULL
};


/*
 *  Initialize BOOTP extension fields in the request.
 */
static void __init ic_bootp_init_ext(u8 *e)
{
	*e++ = 99;		/* RFC1048 Magic Cookie */
	*e++ = 130;
	*e++ = 83;
	*e++ = 99;
	*e++ = 1;		/* Subnet mask request */
	*e++ = 4;
	e += 4;
	*e++ = 3;		/* Default gateway request */
	*e++ = 4;
	e += 4;
	*e++ = 12;		/* Host name request */
	*e++ = 32;
	e += 32;
	*e++ = 40;		/* NIS Domain name request */
	*e++ = 32;
	e += 32;
	*e++ = 17;		/* Boot path */
	*e++ = 32;
	e += 32;
	*e = 255;		/* End of the list */
}


/*
 *  Initialize the BOOTP mechanism.
 */
static inline void ic_bootp_init(void)
{
	get_random_bytes(&ic_bootp_xid, sizeof(u32));
	DBG(("BOOTP: XID=%08x\n", ic_bootp_xid));
	dev_add_pack(&bootp_packet_type);
}


/*
 *  BOOTP cleanup.
 */
static inline void ic_bootp_cleanup(void)
{
	dev_remove_pack(&bootp_packet_type);
}


/*
 *  Send BOOTP request to single interface.
 */
static void __init ic_bootp_send_if(struct ic_device *d, u32 jiffies)
{
	struct net_device *dev = d->dev;
	struct sk_buff *skb;
	struct bootp_pkt *b;
	int hh_len = (dev->hard_header_len + 15) & ~15;
	struct iphdr *h;

	/* Allocate packet */
	skb = alloc_skb(sizeof(struct bootp_pkt) + hh_len + 15, GFP_KERNEL);
	if (!skb)
		return;
	skb_reserve(skb, hh_len);
	b = (struct bootp_pkt *) skb_put(skb, sizeof(struct bootp_pkt));
	memset(b, 0, sizeof(struct bootp_pkt));

	/* Construct IP header */
	skb->nh.iph = h = &b->iph;
	h->version = 4;
	h->ihl = 5;
	h->tot_len = htons(sizeof(struct bootp_pkt));
	h->frag_off = htons(IP_DF);
	h->ttl = 64;
	h->protocol = IPPROTO_UDP;
	h->daddr = INADDR_BROADCAST;
	h->check = ip_fast_csum((unsigned char *) h, h->ihl);

	/* Construct UDP header */
	b->udph.source = htons(68);
	b->udph.dest = htons(67);
	b->udph.len = htons(sizeof(struct bootp_pkt) - sizeof(struct iphdr));
	/* UDP checksum not calculated -- explicitly allowed in BOOTP RFC */

	/* Construct BOOTP header */
	b->op = BOOTP_REQUEST;
	if (dev->type < 256) /* check for false types */
		b->htype = dev->type;
	else if (dev->type == ARPHRD_IEEE802_TR) /* fix for token ring */
		b->htype = ARPHRD_IEEE802;
	else {
		printk("Unknown ARP type 0x%04x for device %s\n", dev->type, dev->name);
		b->htype = dev->type; /* can cause undefined behavior */
	}
	b->hlen = dev->addr_len;
	memcpy(b->hw_addr, dev->dev_addr, dev->addr_len);
	b->secs = htons(jiffies / HZ);
	b->xid = ic_bootp_xid;
	ic_bootp_init_ext(b->vendor_area);

	/* Chain packet down the line... */
	skb->dev = dev;
	skb->protocol = __constant_htons(ETH_P_IP);
	if ((dev->hard_header &&
	     dev->hard_header(skb, dev, ntohs(skb->protocol), dev->broadcast, dev->dev_addr, skb->len) < 0) ||
	    dev_queue_xmit(skb) < 0)
		printk("E");
}


/*
 *  Send BOOTP requests to all interfaces.
 */
static void __init ic_bootp_send(u32 jiffies)
{
	struct ic_device *d;

	for(d=ic_first_dev; d; d=d->next)
		if (d->able & IC_BOOTP)
			ic_bootp_send_if(d, jiffies);
}


/*
 *  Copy BOOTP-supplied string if not already set.
 */
static int __init ic_bootp_string(char *dest, char *src, int len, int max)
{
	if (!len)
		return 0;
	if (len > max-1)
		len = max-1;
	strncpy(dest, src, len);
	dest[len] = '\0';
	return 1;
}


/*
 *  Process BOOTP extension.
 */
static void __init ic_do_bootp_ext(u8 *ext)
{
#ifdef IPCONFIG_DEBUG
	u8 *c;

	printk("BOOTP: Got extension %02x",*ext);
	for(c=ext+2; c<ext+2+ext[1]; c++)
		printk(" %02x", *c);
	printk("\n");
#endif

	switch (*ext++) {
		case 1:		/* Subnet mask */
			if (ic_netmask == INADDR_NONE)
				memcpy(&ic_netmask, ext+1, 4);
			break;
		case 3:		/* Default gateway */
			if (ic_gateway == INADDR_NONE)
				memcpy(&ic_gateway, ext+1, 4);
			break;
		case 12:	/* Host name */
			ic_bootp_string(system_utsname.nodename, ext+1, *ext, __NEW_UTS_LEN);
			ic_host_name_set = 1;
			break;
		case 40:	/* NIS Domain name */
			ic_bootp_string(system_utsname.domainname, ext+1, *ext, __NEW_UTS_LEN);
			break;
		case 17:	/* Root path */
			if (!root_server_path[0])
				ic_bootp_string(root_server_path, ext+1, *ext, sizeof(root_server_path));
			break;
	}
}


/*
 *  Receive BOOTP reply.
 */
static int __init ic_bootp_recv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt)
{
	struct bootp_pkt *b = (struct bootp_pkt *) skb->nh.iph;
	struct iphdr *h = &b->iph;
	int len;

	/* If we already have a reply, just drop the packet */
	if (ic_got_reply)
		goto drop;

	/* Check whether it's a BOOTP packet */
	if (skb->pkt_type == PACKET_OTHERHOST ||
	    skb->len < sizeof(struct udphdr) + sizeof(struct iphdr) ||
	    h->ihl != 5 ||
	    h->version != 4 ||
	    ip_fast_csum((char *) h, h->ihl) != 0 ||
	    skb->len < ntohs(h->tot_len) ||
	    h->protocol != IPPROTO_UDP ||
	    b->udph.source != htons(67) ||
	    b->udph.dest != htons(68) ||
	    ntohs(h->tot_len) < ntohs(b->udph.len) + sizeof(struct iphdr))
		goto drop;

	/* Fragments are not supported */
	if (h->frag_off & htons(IP_OFFSET|IP_MF)) {
		printk(KERN_ERR "BOOTP: Ignoring fragmented reply.\n");
		goto drop;
	}

	/* Is it a reply to our BOOTP request? */
	len = ntohs(b->udph.len) - sizeof(struct udphdr);
	if (len < 300 ||				    /* See RFC 951:2.1 */
	    b->op != BOOTP_REPLY ||
	    b->xid != ic_bootp_xid) {
		printk("?");
		goto drop;
	}

	/* Extract basic fields */
	ic_myaddr = b->your_ip;
	ic_servaddr = b->server_ip;
	ic_got_reply = IC_BOOTP;
	ic_dev = dev;

	/* Parse extensions */
	if (b->vendor_area[0] == 99 &&	/* Check magic cookie */
	    b->vendor_area[1] == 130 &&
	    b->vendor_area[2] == 83 &&
	    b->vendor_area[3] == 99) {
		u8 *ext = &b->vendor_area[4];
                u8 *end = (u8 *) b + ntohs(b->iph.tot_len);
		while (ext < end && *ext != 0xff) {
			if (*ext == 0)		/* Padding */
				ext++;
			else {
				u8 *opt = ext;
				ext += ext[1] + 2;
				if (ext <= end)
					ic_do_bootp_ext(opt);
			}
		}
	}

	if (ic_gateway == INADDR_NONE && b->relay_ip)
		ic_gateway = b->relay_ip;

drop:
	kfree_skb(skb);
	return 0;
}	


#endif


/*
 *	Dynamic IP configuration -- BOOTP and RARP.
 */

#ifdef CONFIG_IP_PNP_DYNAMIC

static int __init ic_dynamic(void)
{
	int retries;
	unsigned long timeout, jiff;
	unsigned long start_jiffies;
	int do_rarp = ic_proto_have_if & IC_RARP;
	int do_bootp = ic_proto_have_if & IC_BOOTP;

	/*
	 * If neither BOOTP nor RARP was selected, return with an error. This
	 * routine gets only called when some pieces of information are mis-
	 * sing, and without BOOTP and RARP we are not able to get that in-
	 * formation.
	 */
	if (!ic_proto_enabled) {
		printk(KERN_ERR "IP-Config: Incomplete network configuration information.\n");
		return -1;
	}

#ifdef CONFIG_IP_PNP_BOOTP
	if ((ic_proto_enabled ^ ic_proto_have_if) & IC_BOOTP)
		printk(KERN_ERR "BOOTP: No suitable device found.\n");
#endif

#ifdef CONFIG_IP_PNP_RARP
	if ((ic_proto_enabled ^ ic_proto_have_if) & IC_RARP)
		printk(KERN_ERR "RARP: No suitable device found.\n");
#endif

	if (!ic_proto_have_if)
		/* Error message already printed */
		return -1;

	/*
	 * Setup RARP and BOOTP protocols
	 */
#ifdef CONFIG_IP_PNP_RARP
	if (do_rarp)
		ic_rarp_init();
#endif
#ifdef CONFIG_IP_PNP_BOOTP
	if (do_bootp)
		ic_bootp_init();
#endif

	/*
	 * Send requests and wait, until we get an answer. This loop
	 * seems to be a terrible waste of CPU time, but actually there is
	 * only one process running at all, so we don't need to use any
	 * scheduler functions.
	 * [Actually we could now, but the nothing else running note still 
	 *  applies.. - AC]
	 */
	printk(KERN_NOTICE "Sending %s%s%s requests...",
	        do_bootp ? "BOOTP" : "",
		do_bootp && do_rarp ? " and " : "",
		do_rarp ? "RARP" : "");
	start_jiffies = jiffies;
	retries = CONF_RETRIES;
	get_random_bytes(&timeout, sizeof(timeout));
	timeout = CONF_BASE_TIMEOUT + (timeout % (unsigned) CONF_TIMEOUT_RANDOM);
	for(;;) {
#ifdef CONFIG_IP_PNP_BOOTP
		if (do_bootp)
			ic_bootp_send(jiffies - start_jiffies);
#endif
#ifdef CONFIG_IP_PNP_RARP
		if (do_rarp)
			ic_rarp_send();
#endif
		printk(".");
		jiff = jiffies + timeout;
		while (jiffies < jiff && !ic_got_reply)
			barrier();
		if (ic_got_reply) {
			printk(" OK\n");
			break;
		}
		if (! --retries) {
			printk(" timed out!\n");
			break;
		}
		timeout = timeout CONF_TIMEOUT_MULT;
		if (timeout > CONF_TIMEOUT_MAX)
			timeout = CONF_TIMEOUT_MAX;
	}

#ifdef CONFIG_IP_PNP_RARP
	if (do_rarp)
		ic_rarp_cleanup();
#endif
#ifdef CONFIG_IP_PNP_BOOTP
	if (do_bootp)
		ic_bootp_cleanup();
#endif

	if (!ic_got_reply)
		return -1;

	printk("IP-Config: Got %s answer from %u.%u.%u.%u, ",
		(ic_got_reply & IC_BOOTP) ? "BOOTP" : "RARP",
		NIPQUAD(ic_servaddr));
	printk("my address is %u.%u.%u.%u\n", NIPQUAD(ic_myaddr));

	return 0;
}

#endif

/*
 *	IP Autoconfig dispatcher.
 */

static int __init ip_auto_config(void)
{
	if (!ic_enable)
		return 0;

	DBG(("IP-Config: Entered.\n"));

	/* Setup all network devices */
	if (ic_open_devs() < 0)
		return -1;

	/*
	 * If the config information is insufficient (e.g., our IP address or
	 * IP address of the boot server is missing or we have multiple network
	 * interfaces and no default was set), use BOOTP or RARP to get the
	 * missing values.
	 */
	if (ic_myaddr == INADDR_NONE ||
#ifdef CONFIG_ROOT_NFS
	    (root_server_addr == INADDR_NONE && ic_servaddr == INADDR_NONE) ||
#endif
	    ic_first_dev->next) {
#ifdef CONFIG_IP_PNP_DYNAMIC
		if (ic_dynamic() < 0) {
			printk(KERN_ERR "IP-Config: Auto-configuration of network failed.\n");
			ic_close_devs();
			return -1;
		}
#else
		printk(KERN_ERR "IP-Config: Incomplete network configuration information.\n");
		ic_close_devs();
		return -1;
#endif
	} else {
		ic_dev = ic_first_dev->dev;	/* Device selected manually or only one device -> use it */
	}

	/*
	 * Use defaults whereever applicable.
	 */
	if (ic_defaults() < 0)
		return -1;

	/*
	 * Close all network devices except the device we've
	 * autoconfigured and set up routes.
	 */
	ic_close_devs();
	if (ic_setup_if() < 0 || ic_setup_routes() < 0)
		return -1;

	DBG(("IP-Config: device=%s, local=%08x, server=%08x, boot=%08x, gw=%08x, mask=%08x\n",
	    ic_dev->name, ic_myaddr, ic_servaddr, root_server_addr, ic_gateway, ic_netmask));
	DBG(("IP-Config: host=%s, domain=%s, path=`%s'\n", system_utsname.nodename,
	    system_utsname.domainname, root_server_path));
	return 0;
}

module_init(ip_auto_config);


/*
 *  Decode any IP configuration options in the "ip=" or "nfsaddrs=" kernel
 *  command line parameter. It consists of option fields separated by colons in
 *  the following order:
 *
 *  <client-ip>:<server-ip>:<gw-ip>:<netmask>:<host name>:<device>:<bootp|rarp>
 *
 *  Any of the fields can be empty which means to use a default value:
 *	<client-ip>	- address given by BOOTP or RARP
 *	<server-ip>	- address of host returning BOOTP or RARP packet
 *	<gw-ip>		- none, or the address returned by BOOTP
 *	<netmask>	- automatically determined from <client-ip>, or the
 *			  one returned by BOOTP
 *	<host name>	- <client-ip> in ASCII notation, or the name returned
 *			  by BOOTP
 *	<device>	- use all available devices
 *	<bootp|rarp|both|off> - use both protocols to determine my own address
 */
static int __init ic_proto_name(char *name)
{
	if (!strcmp(name, "off")) {
		ic_proto_enabled = 0;
		return 1;
	}
#ifdef CONFIG_IP_PNP_BOOTP
	else if (!strcmp(name, "bootp")) {
		ic_proto_enabled &= ~IC_RARP;
		return 1;
	}
#endif
#ifdef CONFIG_IP_PNP_RARP
	else if (!strcmp(name, "rarp")) {
		ic_proto_enabled &= ~IC_BOOTP;
		return 1;
	}
#endif
#ifdef CONFIG_IP_PNP_DYNAMIC
	else if (!strcmp(name, "both")) {
		return 1;
	}
#endif
	return 0;
}

static int __init ip_auto_config_setup(char *addrs)
{
	char *cp, *ip, *dp;
	int num = 0;

	ic_set_manually = 1;
	if (!strcmp(addrs, "off")) {
		ic_enable = 0;
		return 1;
	}
	if (ic_proto_name(addrs))
		return 1;

	/* Parse the whole string */
	ip = addrs;
	while (ip && *ip) {
		if ((cp = strchr(ip, ':')))
			*cp++ = '\0';
		if (strlen(ip) > 0) {
			DBG(("IP-Config: Parameter #%d: `%s'\n", num, ip));
			switch (num) {
			case 0:
				if ((ic_myaddr = in_aton(ip)) == INADDR_ANY)
					ic_myaddr = INADDR_NONE;
				break;
			case 1:
				if ((ic_servaddr = in_aton(ip)) == INADDR_ANY)
					ic_servaddr = INADDR_NONE;
				break;
			case 2:
				if ((ic_gateway = in_aton(ip)) == INADDR_ANY)
					ic_gateway = INADDR_NONE;
				break;
			case 3:
				if ((ic_netmask = in_aton(ip)) == INADDR_ANY)
					ic_netmask = INADDR_NONE;
				break;
			case 4:
				if ((dp = strchr(ip, '.'))) {
					*dp++ = '\0';
					strncpy(system_utsname.domainname, dp, __NEW_UTS_LEN);
					system_utsname.domainname[__NEW_UTS_LEN] = '\0';
				}
				strncpy(system_utsname.nodename, ip, __NEW_UTS_LEN);
				system_utsname.nodename[__NEW_UTS_LEN] = '\0';
				ic_host_name_set = 1;
				break;
			case 5:
				strncpy(user_dev_name, ip, IFNAMSIZ);
				user_dev_name[IFNAMSIZ-1] = '\0';
				break;
			case 6:
				ic_proto_name(ip);
				break;
			}
		}
		ip = cp;
		num++;
	}

	return 1;
}

static int __init nfsaddrs_config_setup(char *addrs)
{
	return ip_auto_config_setup(addrs);
}

__setup("ip=", ip_auto_config_setup);
__setup("nfsaddrs=", nfsaddrs_config_setup);
