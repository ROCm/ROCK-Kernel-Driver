#include "linux/kernel.h"
#include "linux/stddef.h"
#include "linux/init.h"
#include "linux/netdevice.h"
#include "linux/if_arp.h"
#include "net_kern.h"
#include "net_user.h"
#include "kern.h"
#include "slip.h"
#include "slip_kern.h"

struct slip_data slip_priv[MAX_UML_NETDEV] = {
	[ 0 ... MAX_UML_NETDEV - 1 ] =
	{
		addr:		NULL,
		gate_addr:	NULL,
		slave:		-1,
		buf: 		{ 0 },
		pos:		0,
		esc:		0,
	}
};

void slip_init(struct net_device *dev, int index)
{
	struct uml_net_private *private;
	struct slip_data *spri;

	private = dev->priv;
	spri = (struct slip_data *) private->user;
	*spri = slip_priv[index];
	strncpy(dev->name, "umn", IFNAMSIZ);
	dev->init = NULL;
	dev->hard_header_len = 0;
	dev->addr_len = 4;
	dev->type = ARPHRD_ETHER;
	dev->tx_queue_len = 256;
	dev->flags = IFF_NOARP;
	if(register_netdev(dev))
		printk(KERN_ERR "Couldn't initialize umn\n");
	printk("SLIP backend - SLIP IP = %s\n", spri->gate_addr);
}

static unsigned short slip_protocol(struct sk_buff *skbuff)
{
	return(htons(ETH_P_IP));
}

static int slip_read(int fd, struct sk_buff **skb, 
		       struct uml_net_private *lp)
{
	return(slip_user_read(fd, (*skb)->mac.raw, (*skb)->dev->mtu, 
			      (struct slip_data *) &lp->user));
}

static int slip_write(int fd, struct sk_buff **skb,
		      struct uml_net_private *lp)
{
	return(slip_user_write(fd, (*skb)->data, (*skb)->len, 
			       (struct slip_data *) &lp->user));
}

struct net_kern_info slip_kern_info = {
	init:			slip_init,
	protocol:		slip_protocol,
	read:			slip_read,
	write:			slip_write,
};

static int slip_count = 0;

int slip_setup(char *str, struct uml_net *dev)
{
	int n = slip_count;

	dev->user = &slip_user_info;
	dev->kern = &slip_kern_info;
	dev->private_size = sizeof(struct slip_data);
	dev->transport_index = slip_count++;
	if(*str != ',') return(0);
	str++;
	if(str[0] != '\0') slip_priv[n].gate_addr = str;
	return(0);
}

static struct transport slip_transport = {
	list :	LIST_HEAD_INIT(slip_transport.list),
	name :	"slip",
	setup : slip_setup
};

static int register_slip(void)
{
	register_transport(&slip_transport);
	return(1);
}

__initcall(register_slip);

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
