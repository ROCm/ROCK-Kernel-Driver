/*
 * user-mode-linux networking multicast transport
 * Copyright (C) 2001 by Harald Welte <laforge@gnumonks.org>
 *
 * based on the existing uml-networking code, which is
 * Copyright (C) 2001 Lennert Buytenhek (buytenh@gnu.org) and 
 * James Leu (jleu@mindspring.net).
 * Copyright (C) 2001 by various other people who didn't put their name here.
 *
 * Licensed under the GPL.
 */

#include "linux/kernel.h"
#include "linux/init.h"
#include "linux/netdevice.h"
#include "linux/etherdevice.h"
#include "linux/in.h"
#include "linux/inet.h"
#include "net_kern.h"
#include "net_user.h"
#include "mcast.h"
#include "mcast_kern.h"

struct mcast_data mcast_priv[MAX_UML_NETDEV] = {
	[ 0 ... MAX_UML_NETDEV - 1 ] =
	{
		addr:		"239.192.168.1",
		port:		1102,
		ttl:		1,
	}
};

void mcast_init(struct net_device *dev, int index)
{
	struct uml_net_private *pri;
	struct mcast_data *dpri;

	init_etherdev(dev, 0);
	pri = dev->priv;
	dpri = (struct mcast_data *) pri->user;
	*dpri = mcast_priv[index];
	printk("mcast backend ");
	printk("multicast adddress: %s:%u, TTL:%u ",
	       dpri->addr, dpri->port, dpri->ttl);

	printk("\n");
}

static unsigned short mcast_protocol(struct sk_buff *skb)
{
	return eth_type_trans(skb, skb->dev);
}

static int mcast_read(int fd, struct sk_buff **skb, struct uml_net_private *lp)
{
	*skb = ether_adjust_skb(*skb, ETH_HEADER_OTHER);
	if(*skb == NULL) return(-ENOMEM);
	return(net_recvfrom(fd, (*skb)->mac.raw, 
			    (*skb)->dev->mtu + ETH_HEADER_OTHER));
}

static int mcast_write(int fd, struct sk_buff **skb,
			struct uml_net_private *lp)
{
	return mcast_user_write(fd, (*skb)->data, (*skb)->len, 
				 (struct mcast_data *) &lp->user);
}

static struct net_kern_info mcast_kern_info = {
	init:			mcast_init,
	protocol:		mcast_protocol,
	read:			mcast_read,
	write:			mcast_write,
};

static int mcast_count = 0;

int mcast_setup(char *str, struct uml_net *dev)
{
	int err, n = mcast_count;
	int num = 0;
	char *p1, *p2;

	dev->user = &mcast_user_info;
	dev->kern = &mcast_kern_info;
	dev->private_size = sizeof(struct mcast_data);
	dev->transport_index = mcast_count++;

	/* somewhat more sophisticated parser, needed for in_aton */

	p1 = str;
	if (*str == ',')
		p1++;
	while (p1 && *p1) {
		if ((p2 = strchr(p1, ',')))
			*p2++ = '\0';
		if (strlen(p1) > 0) {
			switch (num) {
			case 0:
				/* First argument: Ethernet address */
				err = setup_etheraddr(p1, dev->mac);
				if (!err) 
					dev->have_mac = 1;
				break;
			case 1:
				/* Second argument: Multicast group */
				mcast_priv[n].addr = p1;
				break;
			case 2:
				/* Third argument: Port number */
				mcast_priv[n].port = 
					htons(simple_strtoul(p1, NULL, 10));
				break;
			case 3:
				/* Fourth argument: TTL */
				mcast_priv[n].ttl = 
						simple_strtoul(p1, NULL, 10);
				break;
			}
		}
		p1 = p2;
		num++;
	}

	printk(KERN_INFO "Configured mcast device: %s:%u-%u\n",
		mcast_priv[n].addr, mcast_priv[n].port,
		mcast_priv[n].ttl);

	return(0);
}

static struct transport mcast_transport = {
	list :	LIST_HEAD_INIT(mcast_transport.list),
	name :	"mcast",
	setup : mcast_setup
};

static int register_mcast(void)
{
	register_transport(&mcast_transport);
	return(1);
}

__initcall(register_mcast);
/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
