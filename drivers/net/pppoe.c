/** -*- linux-c -*- ***********************************************************
 * Linux PPP over Ethernet (PPPoX/PPPoE) Sockets
 *
 * PPPoX --- Generic PPP encapsulation socket family
 * PPPoE --- PPP over Ethernet (RFC 2516)
 *
 *
 * Version:    0.6.4
 *
 * 030700 :     Fixed connect logic to allow for disconnect.
 * 270700 :	Fixed potential SMP problems; we must protect against 
 *		simultaneous invocation of ppp_input 
 *		and ppp_unregister_channel.
 * 040800 :	Respect reference count mechanisms on net-devices.
 * 200800 :     fix kfree(skb) in pppoe_rcv (acme)
 *		Module reference count is decremented in the right spot now,
 *		guards against sock_put not actually freeing the sk 
 *		in pppoe_release.
 * 051000 :	Initialization cleanup.
 * 111100 :	Fix recvmsg.
 *
 * Author:	Michal Ostrowski <mostrows@styx.uwaterloo.ca>
 * Contributors:
 * 		Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * License:
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 */

#include <linux/string.h>
#include <linux/module.h>

#include <asm/uaccess.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/errno.h>

#include <linux/netdevice.h>
#include <linux/net.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/if_ether.h>
#include <linux/if_pppox.h>
#include <net/sock.h>
#include <linux/ppp_channel.h>
#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>
#include <linux/if_pppvar.h>
#include <linux/notifier.h>
#include <linux/file.h>
#include <linux/proc_fs.h>



static int __attribute__((unused)) pppoe_debug = 7;
#define PPPOE_HASH_BITS 4
#define PPPOE_HASH_SIZE (1<<PPPOE_HASH_BITS)

int pppoe_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg);
int pppoe_xmit(struct ppp_channel *chan, struct sk_buff *skb);
int __pppoe_xmit(struct sock *sk, struct sk_buff *skb);

struct proto_ops pppoe_ops;


#if 0
#define CHECKPTR(x,y) { if (!(x) && pppoe_debug &7 ){ printk(KERN_CRIT "PPPoE Invalid pointer : %s , %p\n",#x,(x)); error=-EINVAL; goto y; }}
#define DEBUG(s,args...) if( pppoe_debug & (s) ) printk(KERN_CRIT args );
#else
#define CHECKPTR(x,y) do {} while (0);
#define DEBUG(s,args...) do { } while (0);
#endif



static rwlock_t pppoe_hash_lock = RW_LOCK_UNLOCKED;


static inline int cmp_2_addr(struct pppoe_addr *a, struct pppoe_addr *b)
{
	return (a->sid == b->sid &&
		(memcmp(a->remote, b->remote, ETH_ALEN) == 0));
}

static inline int cmp_addr(struct pppoe_addr *a, unsigned long sid, char *addr)
{
	return (a->sid == sid &&
		(memcmp(a->remote,addr,ETH_ALEN) == 0));
}

static int hash_item(unsigned long sid, unsigned char *addr)
{
	char hash=0;
	int i,j;
	for (i = 0; i < ETH_ALEN ; ++i){
		for (j = 0; j < 8/PPPOE_HASH_BITS ; ++j){
			hash ^= addr[i] >> ( j * PPPOE_HASH_BITS );
		}
	}

	for (i = 0; i < (sizeof(unsigned long)*8) / PPPOE_HASH_BITS ; ++i)
		hash ^= sid >> (i*PPPOE_HASH_BITS);

	return hash & ( PPPOE_HASH_SIZE - 1 );
}	

static struct pppox_opt *item_hash_table[PPPOE_HASH_SIZE] = { 0, };

/**********************************************************************
 *
 *  Set/get/delete/rehash items  (internal versions)
 *
 **********************************************************************/
static struct pppox_opt *__get_item(unsigned long sid, unsigned char *addr)
{
	int hash = hash_item(sid, addr);
	struct pppox_opt *ret;

	ret = item_hash_table[hash];

	while (ret && !cmp_addr(&ret->pppoe_pa, sid, addr))
		ret = ret->next;

	return ret;
}

static int __set_item(struct pppox_opt *po)
{
	int hash = hash_item(po->pppoe_pa.sid, po->pppoe_pa.remote);
	struct pppox_opt *ret;

	ret = item_hash_table[hash];
	while (ret) {
		if (cmp_2_addr(&ret->pppoe_pa, &po->pppoe_pa))
			return -EALREADY;

		ret = ret->next;
	}

	if (!ret) {
		po->next = item_hash_table[hash];
		item_hash_table[hash] = po;
	}

	return 0;
}

static struct pppox_opt *__delete_item(unsigned long sid, char *addr)
{
	int hash = hash_item(sid, addr);
	struct pppox_opt *ret, **src;

	ret = item_hash_table[hash];
	src = &item_hash_table[hash];

	while (ret) {
		if (cmp_addr(&ret->pppoe_pa, sid, addr)) {
			*src = ret->next;
			break;
		}

		src = &ret->next;
		ret = ret->next;
	}

	return ret;
}

/**********************************************************************
 *
 *  Set/get/delete/rehash items
 *
 **********************************************************************/
static inline struct pppox_opt *get_item(unsigned long sid,
					 unsigned char *addr)
{
	struct pppox_opt *po;

	read_lock_bh(&pppoe_hash_lock);
	po = __get_item(sid, addr);
	if(po)
		sock_hold(po->sk);
	read_unlock_bh(&pppoe_hash_lock);

	return po;
}

static inline struct pppox_opt *get_item_by_addr(struct sockaddr_pppox *sp)
{
	return get_item(sp->sa_addr.pppoe.sid, sp->sa_addr.pppoe.remote);
}

static inline int set_item(struct pppox_opt *po)
{
	int i;

	if (!po)
		return -EINVAL;

	write_lock_bh(&pppoe_hash_lock);
	i = __set_item(po);
	write_unlock_bh(&pppoe_hash_lock);

	return i;
}

static inline struct pppox_opt *delete_item(unsigned long sid, char *addr)
{
	struct pppox_opt *ret;

	write_lock_bh(&pppoe_hash_lock);
	ret = __delete_item(sid, addr);
	write_unlock_bh(&pppoe_hash_lock);

	return ret;
}



/***************************************************************************
 *
 *  Handler for device events.
 *  Certain device events require that sockets be unconnected.
 *
 **************************************************************************/
static int pppoe_device_event(struct notifier_block *this,
			      unsigned long event, void *ptr)
{
	int error = NOTIFY_DONE;
	struct net_device *dev = (struct net_device *) ptr;
	struct pppox_opt *po = NULL;
	int hash = 0;
	
	/* Only look at sockets that are using this specific device. */
	switch (event) {
	case NETDEV_CHANGEMTU:
	    /* A change in mtu is a bad thing, requiring
	     * LCP re-negotiation.
	     */
	case NETDEV_GOING_DOWN:
	case NETDEV_DOWN:

		/* Find every socket on this device and kill it. */
		read_lock_bh(&pppoe_hash_lock);

		while (!po && hash < PPPOE_HASH_SIZE){
			po = item_hash_table[hash];
			++hash;
		}
	
		while (po && hash < PPPOE_HASH_SIZE){
			if(po->pppoe_dev == dev){
				lock_sock(po->sk);
				if (po->sk->state & (PPPOX_CONNECTED|PPPOX_BOUND)){
					pppox_unbind_sock(po->sk);
				
					dev_put(po->pppoe_dev);
					po->pppoe_dev = NULL;

					po->sk->state = PPPOX_DEAD;
					po->sk->state_change(po->sk);
				}
 				release_sock(po->sk);
			}
			if (po->next) {
				po = po->next;
			} else {
				po = NULL;
				while (!po && hash < PPPOE_HASH_SIZE){
					po = item_hash_table[hash];
					++hash;
				}
			}
		}
		read_unlock_bh(&pppoe_hash_lock);
		break;
	default:
		break;
	};

	return error;
}


static struct notifier_block pppoe_notifier = {
	notifier_call: pppoe_device_event,
};




/************************************************************************
 *
 * Do the real work of receiving a PPPoE Session frame.
 *
 ***********************************************************************/
int pppoe_rcv_core(struct sock *sk, struct sk_buff *skb){
	struct pppox_opt  *po=sk->protinfo.pppox;
	struct pppox_opt *relay_po = NULL;

	if (sk->state & PPPOX_BOUND) {
		skb_pull(skb, sizeof(struct pppoe_hdr));
		
		ppp_input(&po->chan, skb);
	} else if( sk->state & PPPOX_RELAY ){

		relay_po = get_item_by_addr( &po->pppoe_relay );

		if( relay_po == NULL  ||
		    !( relay_po->sk->state & PPPOX_CONNECTED ) ){
			goto abort;
		}
		
		skb_pull(skb, sizeof(struct pppoe_hdr));
		if( !__pppoe_xmit( relay_po->sk , skb) ){
			goto abort;
		}
	} else {
		sock_queue_rcv_skb(sk, skb);
	}
	return 1;
abort:
	if(relay_po)
		sock_put(relay_po->sk);
	return 0;

}




/************************************************************************
 *
 * Receive wrapper called in BH context.
 *
 ***********************************************************************/
static int pppoe_rcv(struct sk_buff *skb,
		      struct net_device *dev,
		      struct packet_type *pt)

{
	struct pppoe_hdr *ph = (struct pppoe_hdr *) skb->nh.raw;
	struct pppox_opt *po;
	struct sock *sk ;
	int ret;

	po = get_item((unsigned long) ph->sid, skb->mac.ethernet->h_source);

	if(!po){
		kfree_skb(skb);
		return 0;
	}

	sk = po->sk;
        bh_lock_sock(sk);

	/* Socket state is unknown, must put skb into backlog. */
	if( sk->lock.users != 0 ){
		sk_add_backlog( sk, skb);
		ret = 1;
	}else{
		ret = pppoe_rcv_core(sk, skb);
	}
	
	bh_unlock_sock(sk);
	sock_put(sk);
	return ret;
}


/************************************************************************
 *
 * Receive wrapper called in process context.
 *
 ***********************************************************************/
int pppoe_backlog_rcv(struct sock *sk, struct sk_buff *skb)
{
	lock_sock(sk);
	pppoe_rcv_core(sk, skb);
	release_sock(sk);
	return 0;
}



/************************************************************************
 *
 * Receive a PPPoE Discovery frame.
 * This is solely for detection of PADT frames
 *
 ***********************************************************************/
static int pppoe_disc_rcv(struct sk_buff *skb,
			  struct net_device *dev,
			  struct packet_type *pt)

{
	struct pppoe_hdr *ph = (struct pppoe_hdr *) skb->nh.raw;
	struct pppox_opt *po;
	struct sock *sk = NULL;

	if (ph->code != PADT_CODE)
		goto abort;

	po = get_item((unsigned long) ph->sid, skb->mac.ethernet->h_source);

	if (!po)
		goto abort_put;

	sk = po->sk;

	pppox_unbind_sock(sk);

 abort_put:
	sock_put(sk);
 abort:
	kfree_skb(skb);
	return 0;
}




struct packet_type pppoes_ptype = {
	__constant_htons(ETH_P_PPP_SES),
	NULL,
	pppoe_rcv,
	NULL,
	NULL
};

struct packet_type pppoed_ptype = {
	__constant_htons(ETH_P_PPP_DISC),
	NULL,
	pppoe_disc_rcv,
	NULL,
	NULL
};

/***********************************************************************
 *
 * Really kill the socket. (Called from sock_put if refcnt == 0.)
 *
 **********************************************************************/
void pppoe_sock_destruct(struct sock *sk)
{
	if (sk->protinfo.destruct_hook)
		kfree(sk->protinfo.destruct_hook);
	MOD_DEC_USE_COUNT;
}


/***********************************************************************
 *
 * Initialize a new struct sock.
 *
 **********************************************************************/
static int pppoe_create(struct socket *sock)
{
	int error = 0;
	struct sock *sk;
	
	MOD_INC_USE_COUNT;
	
	sk = sk_alloc(PF_PPPOX, GFP_KERNEL, 1);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);

	sock->state = SS_UNCONNECTED;
	sock->ops   = &pppoe_ops;

	sk->protocol = PX_PROTO_OE;
	sk->family = PF_PPPOX;

	sk->backlog_rcv = pppoe_backlog_rcv;
	sk->next = NULL;
	sk->pprev = NULL;
	sk->state = PPPOX_NONE;
	sk->type = SOCK_STREAM;
	sk->destruct = pppoe_sock_destruct;

	sk->protinfo.pppox = kmalloc(sizeof(struct pppox_opt), GFP_KERNEL);
	if (!sk->protinfo.pppox) {
		error = -ENOMEM;
		goto free_sk;
	}

	memset((void *) sk->protinfo.pppox, 0, sizeof(struct pppox_opt));
	sk->protinfo.pppox->sk = sk;

	/* Delete the protinfo when it is time to do so. */
	sk->protinfo.destruct_hook = sk->protinfo.pppox;
	sock->sk = sk;

	return 0;

free_sk:
	sk_free(sk);
	return error;
}

int pppoe_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct pppox_opt *po;
	int error = 0;

	if (!sk)
		return 0;

	if (sk->dead != 0)
		return -EBADF;

	pppox_unbind_sock(sk);

	/* Signal the death of the socket. */
	sk->state = PPPOX_DEAD;

	po = sk->protinfo.pppox;
	if (po->pppoe_pa.sid)
		delete_item(po->pppoe_pa.sid, po->pppoe_pa.remote);
		
	if (po->pppoe_dev)
	    dev_put(po->pppoe_dev);

	sock_orphan(sk);
	sock->sk = NULL;

	skb_queue_purge(&sk->receive_queue);
	sock_put(sk);

	return error;
}


int pppoe_connect(struct socket *sock, struct sockaddr *uservaddr,
		  int sockaddr_len, int flags)
{
	struct sock *sk = sock->sk;
	struct net_device *dev = NULL;
	struct sockaddr_pppox *sp = (struct sockaddr_pppox *) uservaddr;
	struct pppox_opt  *po=sk->protinfo.pppox;
	int error;

	lock_sock(sk);

	error = -EINVAL;
	if (sp->sa_protocol != PX_PROTO_OE)
		goto end;

	/* Check for already bound sockets */
	error = -EBUSY;
	if ((sk->state & PPPOX_CONNECTED) && sp->sa_addr.pppoe.sid)
		goto end;

	/* Check for already disconnected sockets,
	   on attempts to disconnect */
	error = -EALREADY;
	if((sk->state & PPPOX_DEAD) && !sp->sa_addr.pppoe.sid )
		goto end;

	error = 0;
	if (po->pppoe_pa.sid) {
		pppox_unbind_sock(sk);

		/* Delete the old binding */
		delete_item(po->pppoe_pa.sid,po->pppoe_pa.remote);

		dev_put(po->pppoe_dev);

		memset(po, 0, sizeof(struct pppox_opt));
		po->sk = sk;

		sk->state = PPPOX_NONE;
	}

	/* Don't re-bind if sid==0 */
	if (sp->sa_addr.pppoe.sid != 0) {
		dev = dev_get_by_name(sp->sa_addr.pppoe.dev);

		error = -ENODEV;
		if (!dev)
			goto end;

		if( ! (dev->flags & IFF_UP) )
			goto end;
		memcpy(&po->pppoe_pa,
		       &sp->sa_addr.pppoe,
		       sizeof(struct pppoe_addr));

		error = set_item(po);
		if (error < 0)
			goto end;

		po->pppoe_dev = dev;

		po->chan.hdrlen = (sizeof(struct pppoe_hdr) +
				   dev->hard_header_len);

		po->chan.private = sk;
		po->chan.ops = &pppoe_chan_ops;

		error = ppp_register_channel(&po->chan);

		sk->state = PPPOX_CONNECTED;
	}

	sk->num = sp->sa_addr.pppoe.sid;

 end:
	release_sock(sk);
	return error;
}


int pppoe_getname(struct socket *sock, struct sockaddr *uaddr,
		  int *usockaddr_len, int peer)
{
	int len = sizeof(struct sockaddr_pppox);
	struct sockaddr_pppox sp;

	sp.sa_family	= AF_PPPOX;
	sp.sa_protocol	= PX_PROTO_OE;
	memcpy(&sp.sa_addr.pppoe, &sock->sk->protinfo.pppox->pppoe_pa,
	       sizeof(struct pppoe_addr));

	memcpy(uaddr, &sp, len);

	*usockaddr_len = len;

	return 0;
}


int pppoe_ioctl(struct socket *sock, unsigned int cmd,
		unsigned long arg)
{
	struct sock *sk = sock->sk;
	struct pppox_opt *po;
	int val = 0;
	int err = 0;

	po = sk->protinfo.pppox;
	switch (cmd) {
	case PPPIOCGMRU:
		err = -ENXIO;

		if (!(sk->state & PPPOX_CONNECTED))
			break;

		err = -EFAULT;
		if (put_user(po->pppoe_dev->mtu -
			     sizeof(struct pppoe_hdr) -
			     PPP_HDRLEN,
			     (int *) arg))
			break;
		err = 0;
		break;

	case PPPIOCSMRU:
		err = -ENXIO;
		if (!(sk->state & PPPOX_CONNECTED))
			break;

		err = -EFAULT;
		if (get_user(val,(int *) arg))
			break;

		if (val < (po->pppoe_dev->mtu
			   - sizeof(struct pppoe_hdr)
			   - PPP_HDRLEN))
			err = 0;
		else
			err = -EINVAL;
		break;

	case PPPIOCSFLAGS:
		err = -EFAULT;
		if (get_user(val, (int *) arg))
			break;
		err = 0;
		break;

	case PPPOEIOCSFWD:
	{
		struct pppox_opt *relay_po;

		err = -EBUSY;
		if (sk->state & PPPOX_BOUND)
			break;

		err = -ENOTCONN;
		if (!(sk->state & PPPOX_CONNECTED))
			break;

		/* PPPoE address from the user specifies an outbound
		   PPPoE address to which frames are forwarded to */
		err = -EFAULT;
		if( copy_from_user(&po->pppoe_relay,
				   (void*)arg,
				   sizeof(struct sockaddr_pppox)))
			break;

		err = -EINVAL;
		if (po->pppoe_relay.sa_family != AF_PPPOX ||
		    po->pppoe_relay.sa_protocol!= PX_PROTO_OE)
			break;

		/* Check that the socket referenced by the address
		   actually exists. */
		relay_po = get_item_by_addr(&po->pppoe_relay);

		if (!relay_po)
			break;

		sock_put(relay_po->sk);
		sk->state |= PPPOX_RELAY;
		err = 0;
		break;
	}

	case PPPOEIOCDFWD:
		err = -EALREADY;
		if (!(sk->state & PPPOX_RELAY))
			break;

		sk->state &= ~PPPOX_RELAY;
		err = 0;
		break;

	default:
	};

	return err;
}


int pppoe_sendmsg(struct socket *sock, struct msghdr *m,
		  int total_len, struct scm_cookie *scm)
{
	struct sk_buff *skb = NULL;
	struct sock *sk = sock->sk;
	int error = 0;
	struct pppoe_hdr hdr;
	struct pppoe_hdr *ph;
	struct net_device *dev;
	char *start;

	if (sk->dead || !(sk->state & PPPOX_CONNECTED)) {
		error = -ENOTCONN;
		goto end;
	}

	hdr.ver = 1;
	hdr.type = 1;
	hdr.code = 0;
	hdr.sid = sk->num;

	lock_sock(sk);

	dev = sk->protinfo.pppox->pppoe_dev;

	error = -EMSGSIZE;
 	if(total_len > dev->mtu+dev->hard_header_len)
		goto end;


	skb = sock_wmalloc(sk, total_len + dev->hard_header_len + 32,
			   0, GFP_KERNEL);
	if (!skb) {
		error = -ENOMEM;
		goto end;
	}

	/* Reserve space for headers. */
	skb_reserve(skb, dev->hard_header_len);
	skb->nh.raw = skb->data;

	skb->dev = dev;

	skb->priority = sk->priority;
	skb->protocol = __constant_htons(ETH_P_PPP_SES);

	ph = (struct pppoe_hdr *) skb_put(skb, total_len + sizeof(struct pppoe_hdr));
	start = (char *) &ph->tag[0];

	error = memcpy_fromiovec( start, m->msg_iov, total_len);

	if (error < 0) {
		kfree_skb(skb);
		goto end;
	}

	error = total_len;
	dev->hard_header(skb, dev, ETH_P_PPP_SES,
			 sk->protinfo.pppox->pppoe_pa.remote,
			 NULL, total_len);

	memcpy(ph, &hdr, sizeof(struct pppoe_hdr));

	ph->length = htons(total_len);

	dev_queue_xmit(skb);

 end:
	release_sock(sk);
	return error;
}

/************************************************************************
 *
 * xmit function for internal use.
 *
 ***********************************************************************/
int __pppoe_xmit(struct sock *sk, struct sk_buff *skb)
{
	struct net_device *dev = sk->protinfo.pppox->pppoe_dev;
	struct pppoe_hdr hdr;
	struct pppoe_hdr *ph;
	int headroom = skb_headroom(skb);
	int data_len = skb->len;

	if (sk->dead  || !(sk->state & PPPOX_CONNECTED)) {
		goto abort;
	}

	hdr.ver	= 1;
	hdr.type = 1;
	hdr.code = 0;
	hdr.sid	= sk->num;
	hdr.length = htons(skb->len);

	if (!dev) {
		goto abort;
	}

	/* Copy the skb if there is no space for the header. */
	if (headroom < (sizeof(struct pppoe_hdr) + dev->hard_header_len)) {
		struct sk_buff *skb2;

		skb2 = dev_alloc_skb(32+skb->len +
				     sizeof(struct pppoe_hdr) +
				     dev->hard_header_len);

		if (skb2 == NULL)
			goto abort;

		skb_reserve(skb2, dev->hard_header_len + sizeof(struct pppoe_hdr));
		memcpy(skb_put(skb2, skb->len), skb->data, skb->len);

		skb_unlink(skb);
		kfree_skb(skb);
		skb = skb2;
	}

	ph = (struct pppoe_hdr *) skb_push(skb, sizeof(struct pppoe_hdr));
	memcpy(ph, &hdr, sizeof(struct pppoe_hdr));
	skb->protocol = __constant_htons(ETH_P_PPP_SES);

	skb->nh.raw = skb->data;

	skb->dev = dev;

	dev->hard_header(skb, dev, ETH_P_PPP_SES,
			 sk->protinfo.pppox->pppoe_pa.remote,
			 NULL, data_len);

	if (dev_queue_xmit(skb) < 0)
		goto abort;

	return 1;
 abort:
	return 0;
}


/************************************************************************
 *
 * xmit function called by generic PPP driver
 * sends PPP frame over PPPoE socket
 *
 ***********************************************************************/
int pppoe_xmit(struct ppp_channel *chan, struct sk_buff *skb)
{
	struct sock *sk = (struct sock *) chan->private;
	return __pppoe_xmit(sk, skb);
}


struct ppp_channel_ops pppoe_chan_ops = { pppoe_xmit , NULL };

int pppoe_rcvmsg(struct socket *sock, struct msghdr *m, int total_len, int flags, struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb = NULL;
	int error = 0;
	int len;
	struct pppoe_hdr *ph = NULL;

	if (sk->state & PPPOX_BOUND) {
		error = -EIO;
		goto end;
	}

	skb = skb_recv_datagram(sk, flags, 0, &error);

	if (error < 0) {
		goto end;
	}

	m->msg_namelen = 0;

	if (skb) {
		error = 0;
		ph = (struct pppoe_hdr *) skb->nh.raw;
		len = ntohs(ph->length);

		error = memcpy_toiovec(m->msg_iov, (unsigned char *) &ph->tag[0], len);
		if (error < 0)
			goto do_skb_free;
		error = len;
	}

do_skb_free:
	if (skb)
		kfree_skb(skb);
end:
	return error;
}

int pppoe_proc_info(char *buffer, char **start, off_t offset, int length)
{
	struct pppox_opt *po;
	int len = 0;
	off_t pos = 0;
	off_t begin = 0;
	int size;
	int i;
	
	len += sprintf(buffer,
		       "Id       Address              Device\n");
	pos = len;

	write_lock_bh(&pppoe_hash_lock);

	for (i = 0; i < PPPOE_HASH_SIZE; i++) {
		po = item_hash_table[i];
		while (po) {
			char *dev = po->pppoe_pa.dev;

			size = sprintf(buffer + len,
				       "%08X %02X:%02X:%02X:%02X:%02X:%02X %8s\n",
				       po->pppoe_pa.sid,
				       po->pppoe_pa.remote[0],
				       po->pppoe_pa.remote[1],
				       po->pppoe_pa.remote[2],
				       po->pppoe_pa.remote[3],
				       po->pppoe_pa.remote[4],
				       po->pppoe_pa.remote[5],
				       dev);
			len += size;
			pos += size;
			if (pos < offset) {
				len = 0;
				begin = pos;
			}

			if (pos > offset + length)
				break;

			po = po->next;
		}

		if (po)
			break;
  	}
	write_unlock_bh(&pppoe_hash_lock);

  	*start = buffer + (offset - begin);
  	len -= (offset - begin);
  	if (len > length)
  		len = length;
	if (len < 0)
		len = 0;
  	return len;
}


struct proto_ops pppoe_ops = {
    family:		AF_PPPOX,
    release:		pppoe_release,
    bind:		sock_no_bind,
    connect:		pppoe_connect,
    socketpair:		sock_no_socketpair,
    accept:		sock_no_accept,
    getname:		pppoe_getname,
    poll:		datagram_poll,
    ioctl:		pppoe_ioctl,
    listen:		sock_no_listen,
    shutdown:		sock_no_shutdown,
    setsockopt:		sock_no_setsockopt,
    getsockopt:		sock_no_getsockopt,
    sendmsg:		pppoe_sendmsg,
    recvmsg:		pppoe_rcvmsg,
    mmap:		sock_no_mmap
};

struct pppox_proto pppoe_proto = {
    create:	pppoe_create,
    ioctl:	pppoe_ioctl
};


int __init pppoe_init(void)
{
 	int err = register_pppox_proto(PX_PROTO_OE, &pppoe_proto);

	if (err == 0) {
		printk(KERN_INFO "Registered PPPoE v0.6.4\n");

		dev_add_pack(&pppoes_ptype);
		register_netdevice_notifier(&pppoe_notifier);
		proc_net_create("pppoe", 0, pppoe_proc_info);
	}
	return err;
}

void __exit pppoe_exit(void)
{
	unregister_pppox_proto(PX_PROTO_OE);
	dev_remove_pack(&pppoes_ptype);
	unregister_netdevice_notifier(&pppoe_notifier);
	proc_net_remove("pppoe");
}

module_init(pppoe_init);
module_exit(pppoe_exit);
