/* 
 * Copyright (C) 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/stddef.h"
#include "linux/netdevice.h"
#include "linux/etherdevice.h"
#include "linux/skbuff.h"
#include "linux/init.h"
#include "asm/errno.h"
#include "net_kern.h"
#include "net_user.h"
#include "tuntap.h"

struct tuntap_setup {
	char *dev_name;
	char *gate_addr;
};

struct tuntap_setup tuntap_priv[MAX_UML_NETDEV] = { 
	[ 0 ... MAX_UML_NETDEV - 1 ] =
	{
		dev_name:	NULL,
		gate_addr:	NULL,
	}
};

static void tuntap_init(struct net_device *dev, int index)
{
	struct uml_net_private *pri;
	struct tuntap_data *tpri;

	init_etherdev(dev, 0);
	pri = dev->priv;
	tpri = (struct tuntap_data *) pri->user;
	tpri->dev_name = tuntap_priv[index].dev_name;
	tpri->fixed_config = (tpri->dev_name != NULL);
	tpri->gate_addr = tuntap_priv[index].gate_addr;
	printk("TUN/TAP backend - ");
	if(tpri->gate_addr != NULL) 
		printk("IP = %s", tpri->gate_addr);
	printk("\n");
	tpri->fd = -1;
}

static unsigned short tuntap_protocol(struct sk_buff *skb)
{
	return(eth_type_trans(skb, skb->dev));
}

static int tuntap_read(int fd, struct sk_buff **skb, 
		       struct uml_net_private *lp)
{
	*skb = ether_adjust_skb(*skb, ETH_HEADER_OTHER);
	if(*skb == NULL) return(-ENOMEM);
	return(net_read(fd, (*skb)->mac.raw, 
			(*skb)->dev->mtu + ETH_HEADER_OTHER));
}

static int tuntap_write(int fd, struct sk_buff **skb, 
			struct uml_net_private *lp)
{
	return(net_write(fd, (*skb)->data, (*skb)->len));
}

struct net_kern_info tuntap_kern_info = {
	init:			tuntap_init,
	protocol:		tuntap_protocol,
	read:			tuntap_read,
	write: 			tuntap_write,
};

static int tuntap_count = 0;

int tuntap_setup(char *str, struct uml_net *dev)
{
	struct tuntap_setup *pri;
	int err;

	pri = &tuntap_priv[tuntap_count];
	err = tap_setup_common(str, "tuntap", &pri->dev_name, dev->mac,  
			       &dev->have_mac, &pri->gate_addr);
	if(err) return(err);

	dev->user = &tuntap_user_info;
	dev->kern = &tuntap_kern_info;
	dev->private_size = sizeof(struct tuntap_data);
	dev->transport_index = tuntap_count++;
	return(0);
}

static struct transport tuntap_transport = {
	list :	LIST_HEAD_INIT(tuntap_transport.list),
	name :	"tuntap",
	setup : tuntap_setup
};

static int register_tuntap(void)
{
	register_transport(&tuntap_transport);
	return(1);
}

__initcall(register_tuntap);

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
