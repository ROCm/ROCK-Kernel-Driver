/* Linux ISDN subsystem, protocol encapsulation
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

/* Stuff to support the concap_proto by isdn4linux. isdn4linux - specific
 * stuff goes here. Stuff that depends only on the concap protocol goes to
 * another -- protocol specific -- source file.
 */


#include <linux/isdn.h>
#include "isdn_x25iface.h"
#include "isdn_net.h"
#include <linux/concap.h>
#include "isdn_concap.h"
#include <linux/if_arp.h>

/* The following set of device service operations are for encapsulation
   protocols that require for reliable datalink semantics. That means:

   - before any data is to be submitted the connection must explicitly
     be set up.
   - after the successful set up of the connection is signalled the
     connection is considered to be reliably up.

   Auto-dialing ist not compatible with this requirements. Thus, auto-dialing 
   is completely bypassed.

   It might be possible to implement a (non standardized) datalink protocol
   that provides a reliable data link service while using some auto dialing
   mechanism. Such a protocol would need an auxiliary channel (i.e. user-user-
   signaling on the D-channel) while the B-channel is down.
   */


static int
isdn_concap_dl_data_req(struct concap_proto *concap, struct sk_buff *skb)
{
	struct net_device *ndev = concap -> net_dev;
	isdn_net_dev *nd = ((isdn_net_local *) ndev->priv)->netdev;
	isdn_net_local *lp = isdn_net_get_locked_lp(nd);

	IX25DEBUG( "isdn_concap_dl_data_req: %s \n", concap->net_dev->name);
	if (!lp) {
		IX25DEBUG( "isdn_concap_dl_data_req: %s : isdn_net_send_skb returned %d\n", concap -> net_dev -> name, 1);
		return 1;
	}
	lp->huptimer = 0;
	isdn_net_writebuf_skb(lp, skb);
	spin_unlock_bh(&lp->xmit_lock);
	IX25DEBUG( "isdn_concap_dl_data_req: %s : isdn_net_send_skb returned %d\n", concap -> net_dev -> name, 0);
	return 0;
}


static int
isdn_concap_dl_connect_req(struct concap_proto *concap)
{
	struct net_device *ndev = concap -> net_dev;
	isdn_net_local *lp = (isdn_net_local *) ndev->priv;
	int ret;
	IX25DEBUG( "isdn_concap_dl_connect_req: %s \n", ndev -> name);

	/* dial ... */
	ret = isdn_net_dial_req( lp );
	if ( ret ) IX25DEBUG("dialing failed\n");
	return ret;
}

static int
isdn_concap_dl_disconn_req(struct concap_proto *concap)
{
	IX25DEBUG( "isdn_concap_dl_disconn_req: %s \n", concap -> net_dev -> name);

	isdn_net_hangup( concap -> net_dev );
	return 0;
}

struct concap_device_ops isdn_concap_reliable_dl_dops = {
	&isdn_concap_dl_data_req,
	&isdn_concap_dl_connect_req,
	&isdn_concap_dl_disconn_req
};

struct concap_device_ops isdn_concap_demand_dial_dops = {
	NULL, /* set this first entry to something like &isdn_net_start_xmit,
		 but the entry part of the current isdn_net_start_xmit must be
		 separated first. */
	/* no connection control for demand dial semantics */
	NULL,
	NULL,
};

/* The following should better go into a dedicated source file such that
   this sourcefile does not need to include any protocol specific header
   files. For now:
   */
struct concap_proto *
isdn_concap_new( int encap )
{
	switch ( encap ) {
	case ISDN_NET_ENCAP_X25IFACE:
		return isdn_x25iface_proto_new();
	}
	return NULL;
}

static int
isdn_x25_open(isdn_net_local *lp)
{
	struct net_device * dev = & lp -> netdev -> dev;
	struct concap_proto * cprot = lp -> netdev -> ind_priv;
	struct concap_proto * dops = lp -> inl_priv;
	unsigned long flags;

	save_flags(flags);
	cli();                  /* Avoid glitch on writes to CMD regs */
	if( cprot -> pops && dops )
		cprot -> pops -> restart ( cprot, dev, dops );
	restore_flags(flags);
	return 0;
}

static void
isdn_x25_close(isdn_net_local *lp)
{
	struct concap_proto * cprot = lp -> netdev -> ind_priv;

	if( cprot && cprot -> pops ) cprot -> pops -> close( cprot );
}

static void
isdn_x25_connected(isdn_net_local *lp)
{
	struct concap_proto *cprot = lp -> netdev -> ind_priv;
	struct concap_proto_ops *pops = cprot ? cprot -> pops : 0;

	/* try if there are generic concap receiver routines */
	if( pops )
		if( pops->connect_ind)
			pops->connect_ind(cprot);

	isdn_net_device_wake_queue(lp);
}

static void
isdn_x25_disconnected(isdn_net_local *lp)
{
	struct concap_proto *cprot = lp -> netdev -> ind_priv;
	struct concap_proto_ops *pops = cprot ? cprot -> pops : 0;

	/* try if there are generic encap protocol
	   receiver routines and signal the closure of
	   the link */
	if( pops  &&  pops -> disconn_ind )
		pops -> disconn_ind(cprot);
}

static int
isdn_x25_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
/* At this point hard_start_xmit() passes control to the encapsulation
   protocol (if present).
   For X.25 auto-dialing is completly bypassed because:
   - It does not conform with the semantics of a reliable datalink
     service as needed by X.25 PLP.
   - I don't want that the interface starts dialing when the network layer
     sends a message which requests to disconnect the lapb link (or if it
     sends any other message not resulting in data transmission).
   Instead, dialing will be initiated by the encapsulation protocol entity
   when a dl_establish request is received from the upper layer.
*/
	isdn_net_local *lp = (isdn_net_local *) dev->priv;
	struct concap_proto * cprot = lp -> netdev -> ind_priv;
	int ret = cprot -> pops -> encap_and_xmit ( cprot , skb);

	if (ret)
		netif_stop_queue(dev);
		
	return ret;
}

static void 
isdn_x25_receive(isdn_net_dev *p, isdn_net_local *olp, struct sk_buff *skb)
{
	isdn_net_local *lp = &p->local;
	struct concap_proto *cprot = lp -> netdev -> ind_priv;

	/* try if there are generic sync_device receiver routines */
	if(cprot) 
		if(cprot -> pops)
			if( cprot -> pops -> data_ind) {
				cprot -> pops -> data_ind(cprot,skb);
				return;
			}
}

static void
isdn_x25_init(struct net_device *dev)
{
	unsigned long flags;

	isdn_net_local *lp = dev->priv;

	/* ... ,  prepare for configuration of new one ... */
	switch ( lp->p_encap ){
	case ISDN_NET_ENCAP_X25IFACE:
		lp -> inl_priv = &isdn_concap_reliable_dl_dops;
	}
	/* ... and allocate new one ... */
	p -> cprot = isdn_concap_new( cfg -> p_encap );
	/* p -> cprot == NULL now if p_encap is not supported
	   by means of the concap_proto mechanism */
	if (!p->cprot)
		return -EINVAL;

	return 0;
}

static void
isdn_x25_cleanup(isdn_net_dev *p)
{
	isdn_net_local *lp = &p->local;
	struct concap_proto * cprot = p -> cprot;
	unsigned long flags;
	
	/* delete old encapsulation protocol if present ... */
	save_flags(flags);
	cli(); /* avoid races with incoming events trying to
		  call cprot->pops methods */
	if( cprot && cprot -> pops )
		cprot -> pops -> proto_del ( cprot );
	p -> cprot = NULL;
	lp -> inl_priv = NULL;
	restore_flags(flags);
}

struct isdn_netif_ops isdn_x25_ops = {
	.hard_start_xmit     = isdn_x25_start_xmit,
	.flags               = IFF_NOARP | IFF_POINTOPOINT,
	.type                = ARPHRD_X25,
	.receive             = isdn_x25_receive,
	.connected           = isdn_x25_connected,
	.disconnected        = isdn_x25_disconnected,
	.init                = isdn_x25_init,
	.cleanup             = isdn_x25_cleanup,
	.open                = isdn_x25_open,
	.close               = isdn_x25_close,
};
