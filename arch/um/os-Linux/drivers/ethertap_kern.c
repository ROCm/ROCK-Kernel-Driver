/*
 * Copyright (C) 2001 Lennert Buytenhek (buytenh@gnu.org) and 
 * James Leu (jleu@mindspring.net).
 * Copyright (C) 2001 by various other people who didn't put their name here.
 * Licensed under the GPL.
 */

#include "linux/init.h"
#include "linux/netdevice.h"
#include "linux/etherdevice.h"
#include "linux/init.h"
#include "net_kern.h"
#include "net_user.h"
#include "etap.h"
#include "etap_kern.h"

struct ethertap_setup {
	char *dev_name;
	char *gate_addr;
};

struct ethertap_setup ethertap_priv[MAX_UML_NETDEV] = { 
	[ 0 ... MAX_UML_NETDEV - 1 ] =
	{
		dev_name:	NULL,
		gate_addr:	NULL,
	}
};

static void etap_init(struct net_device *dev, int index)
{
	struct uml_net_private *pri;
	struct ethertap_data *epri;

	init_etherdev(dev, 0);
	pri = dev->priv;
	epri = (struct ethertap_data *) pri->user;
	epri->dev_name = ethertap_priv[index].dev_name;
	epri->gate_addr = ethertap_priv[index].gate_addr;
	printk("ethertap backend - %s", epri->dev_name);
	if(epri->gate_addr != NULL) 
		printk(", IP = %s", epri->gate_addr);
	printk("\n");
	epri->data_fd = -1;
	epri->control_fd = -1;
}

static unsigned short etap_protocol(struct sk_buff *skb)
{
	return(eth_type_trans(skb, skb->dev));
}

static int etap_read(int fd, struct sk_buff **skb, struct uml_net_private *lp)
{
	int len;

	*skb = ether_adjust_skb(*skb, ETH_HEADER_ETHERTAP);
	if(*skb == NULL) return(-ENOMEM);
	len = net_recvfrom(fd, (*skb)->mac.raw, 
			   (*skb)->dev->mtu + 2 * ETH_HEADER_ETHERTAP);
	if(len <= 0) return(len);
	skb_pull(*skb, 2);
	len -= 2;
	return(len);
}

static int etap_write(int fd, struct sk_buff **skb, struct uml_net_private *lp)
{
	if(skb_headroom(*skb) < 2){
	  	struct sk_buff *skb2;

		skb2 = skb_realloc_headroom(*skb, 2);
		dev_kfree_skb(*skb);
		if (skb2 == NULL) return(-ENOMEM);
		*skb = skb2;
	}
	skb_push(*skb, 2);
	return(net_send(fd, (*skb)->data, (*skb)->len));
}

struct net_kern_info ethertap_kern_info = {
	init:			etap_init,
	protocol:		etap_protocol,
	read:			etap_read,
	write: 			etap_write,
};

static int ethertap_count = 0;

int ethertap_setup(char *str, struct uml_net *dev)
{
	struct ethertap_setup *pri;
	int err;

	pri = &ethertap_priv[ethertap_count];
	err = tap_setup_common(str, "ethertap", &pri->dev_name, dev->mac,
			       &dev->have_mac, &pri->gate_addr);
	if(err) return(err);
	if(pri->dev_name == NULL){
		printk("ethertap_setup : Missing tap device name\n");
		return(1);
	}

	dev->user = &ethertap_user_info;
	dev->kern = &ethertap_kern_info;
	dev->private_size = sizeof(struct ethertap_data);
	dev->transport_index = ethertap_count++;
	return(0);
}

static struct transport ethertap_transport = {
	list :	LIST_HEAD_INIT(ethertap_transport.list),
	name :	"ethertap",
	setup : ethertap_setup
};

static int register_ethertap(void)
{
	register_transport(&ethertap_transport);
	return(1);
}

__initcall(register_ethertap);

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
