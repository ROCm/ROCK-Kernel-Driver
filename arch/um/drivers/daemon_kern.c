/*
 * Copyright (C) 2001 Lennert Buytenhek (buytenh@gnu.org) and 
 * James Leu (jleu@mindspring.net).
 * Copyright (C) 2001 by various other people who didn't put their name here.
 * Licensed under the GPL.
 */

#include "linux/kernel.h"
#include "linux/init.h"
#include "linux/netdevice.h"
#include "linux/etherdevice.h"
#include "net_kern.h"
#include "net_user.h"
#include "daemon.h"
#include "daemon_kern.h"

struct daemon_data daemon_priv[MAX_UML_NETDEV] = {
	[ 0 ... MAX_UML_NETDEV - 1 ] =
	{
		sock_type :	"unix",
		ctl_sock :	"/tmp/uml.ctl",
		ctl_addr :	NULL,
		data_addr :	NULL,
		local_addr :	NULL,
		fd :		-1,
		control :	-1,
		dev :		NULL,
	}
};

void daemon_init(struct net_device *dev, int index)
{
	struct uml_net_private *pri;
	struct daemon_data *dpri;

	init_etherdev(dev, 0);
	pri = dev->priv;
	dpri = (struct daemon_data *) pri->user;
	*dpri = daemon_priv[index];
	printk("daemon backend (uml_switch version %d) - %s:%s", 
	       SWITCH_VERSION, dpri->sock_type, dpri->ctl_sock);
	printk("\n");
}

static unsigned short daemon_protocol(struct sk_buff *skb)
{
	return(eth_type_trans(skb, skb->dev));
}

static int daemon_read(int fd, struct sk_buff **skb, 
		       struct uml_net_private *lp)
{
	*skb = ether_adjust_skb(*skb, ETH_HEADER_OTHER);
	if(*skb == NULL) return(-ENOMEM);
	return(net_recvfrom(fd, (*skb)->mac.raw, 
			    (*skb)->dev->mtu + ETH_HEADER_OTHER));
}

static int daemon_write(int fd, struct sk_buff **skb,
			struct uml_net_private *lp)
{
	return(daemon_user_write(fd, (*skb)->data, (*skb)->len, 
				 (struct daemon_data *) &lp->user));
}

static struct net_kern_info daemon_kern_info = {
	init:			daemon_init,
	protocol:		daemon_protocol,
	read:			daemon_read,
	write:			daemon_write,
};

static int daemon_count = 0;

int daemon_setup(char *str, struct uml_net *dev)
{
	int err, n = daemon_count;

	dev->user = &daemon_user_info;
	dev->kern = &daemon_kern_info;
	dev->private_size = sizeof(struct daemon_data);
	dev->transport_index = daemon_count++;
	if(*str != ',') return(0);
	str++;
 	if(*str != ','){
 		err = setup_etheraddr(str, dev->mac);
 		if(!err) dev->have_mac = 1;
 	}
	str = strchr(str, ',');
	if(str == NULL) return(0);
	*str++ = '\0';
	if(*str != ',') daemon_priv[n].sock_type = str;
	str = strchr(str, ',');
	if(str == NULL) return(0);
	*str++ = '\0';
	if(*str != ',') daemon_priv[n].ctl_sock = str;
	str = strchr(str, ',');
	if(str == NULL) return(0);
	*str = '\0';
	printk(KERN_WARNING "daemon_setup : Ignoring data socket "
	       "specification\n");
	return(0);
}

static struct transport daemon_transport = {
	list :	LIST_HEAD_INIT(daemon_transport.list),
	name :	"daemon",
	setup : daemon_setup
};

static int register_daemon(void)
{
	register_transport(&daemon_transport);
	return(1);
}

__initcall(register_daemon);
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
