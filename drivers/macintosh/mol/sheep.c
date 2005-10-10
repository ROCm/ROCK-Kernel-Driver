/*
 *  sheep_net.c - Linux driver for SheepShaver/Basilisk II networking (access to raw Ethernet packets)
 *
 *  SheepShaver (C) 1997-1999 Mar"c" Hellwig and Christian Bauer
 *  Basilisk II (C) 1997-1999 Christian Bauer
 *
 *  Ported to 2.4 and reworked, Samuel Rydh 1999-2003
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/miscdevice.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <net/arp.h>
#include <net/ip.h>
#include <linux/in.h>
#include <linux/wait.h>

MODULE_LICENSE("GPL");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#define LINUX_26
#endif

#define DEBUG 0

#define bug printk
#if DEBUG
#define D(x) (x);
#else
#define D(x) ;
#endif

#define SHEEP_NET_MINOR 		198	// Driver minor number
#define MAX_QUEUE 			32	// Maximum number of packets in queue
#define PROT_MAGIC	 		1520	// Our "magic" protocol type

#define ETH_ADDR_MULTICAST		0x1
#define ETH_ADDR_LOCALLY_DEFINED	0x2

#define SIOC_MOL_GET_IPFILTER		SIOCDEVPRIVATE
#define SIOC_MOL_SET_IPFILTER		(SIOCDEVPRIVATE + 1)

struct SheepVars {
	/* IMPORTANT: the packet_type struct must go first. It no longer (2.6) contains
	 * a data field so we typecast to get the SheepVars struct
	 */
	struct packet_type 	pt;		// Receiver packet type
	struct net_device	*ether;		// The Ethernet device we're attached to
	struct sock		*skt;		// Socket for communication with Ethernet card
	struct sk_buff_head 	queue;		// Receiver packet queue
	wait_queue_head_t 	wait;		// Wait queue for blocking read operations
	unsigned long		ipfilter;	// only receive ip packets destined for this address 
	char			fake_addr[6];
};

/*
 * How various hosts address MOL
 *
 * External hosts:	eth_addr,	MOL_IP
 * Local host:		fake_addr,	MOL_IP
 * MOL:			fake_addr,	MOL_IP
 */

static struct proto sheep_proto = {
	.name = "SHEEP",
	.owner = THIS_MODULE,
	.obj_size = sizeof(struct sock),
};

#ifdef LINUX_26
#define compat_sk_alloc(a,b,c)	sk_alloc( (a), (b), &sheep_proto, (c))
#define skt_set_dead(skt)	do {} while(0)
#define wmem_alloc		sk_wmem_alloc
#else
#define compat_sk_alloc		sk_alloc
#define skt_set_dead(skt)	(skt)->dead = 1
#endif

/************************************************************************/
/*	ethernet address masquerading					*/
/************************************************************************/

static inline int
addrcmp( const char *a1, const char *a2 )
{
	if( *(u32*)a1 != *(u32*)a2 )
		return 1;
	return *((u16*)a1+2) != *((u16*)a2+2);
}

/* Outgoing packet. Replace the fake enet addr with the real one. */
static inline void
cpyaddr( char *d, const char *s ) 
{
	*(u32*)d = *(u32*)s;
	*(u16*)&d[4] = *(u16*)&s[4];
}

static void 
demasquerade( struct sk_buff *skb, struct SheepVars *v )
{
	const char *local_addr = v->ether->dev_addr;
	const char *fake_addr = v->fake_addr;
	char *p = skb->mac.raw;
	int proto = *(short*)&p[12];
	
	cpyaddr( &p[6], local_addr );		// Source address

	// Need to fix ARP packets
	if( proto == htons(ETH_P_ARP) )
		if( !addrcmp(&p[14+8], fake_addr) )	// sender HW-addr
			cpyaddr( &p[14+8], local_addr );

	// ...and AARPs (snap code: 0x00,0x00,0x00,0x80,0xF3)
	if( !p[17] && *(u32*)&p[18] == 0x000080F3 ){
		// XXX: we should perhaps look for the 802 frame too
		if( !addrcmp(&p[30], fake_addr) )
			cpyaddr( &p[30], local_addr );	// sender HW-addr
	}
}


/************************************************************************/
/*	receive filter (also intercepts outgoing packets)		*/
/************************************************************************/

/* This function filters both outgoing and incoming traffic.
 *
 * - Outgoing PROT_MAGIC packets are outgoing mol packets
 *  addressed to the world (not to the local host).
 *
 * - Outgoing packets addressed to the fake address
 * are incoming MOL packets (from the local host).
 * These packets will be seen on the wire, since we can't
 * block them...
 *
 * - Incoming packets which originate from the fake address
 * are MOL packets addressed to the local host.
 *
 * - Incomming external traffic to the MOL IP address are incoming
 * MOL packets. Linux will see these packets too. (Hmm... if
 * we change protocol to PROT_MAGIC then linux ought to ignore
 * them; currently linux responds to ICMP packets even though
 * the IP address is wrong.)
 */

static int 
sheep_net_receiver( struct sk_buff *skb, struct net_device *dev, struct packet_type *pt )
{
	int multicast = (eth_hdr(skb)->h_dest[0] & ETH_ADDR_MULTICAST);
	const char *laddr = dev->dev_addr;
	struct sk_buff *skb2;
	struct SheepVars *v = (struct SheepVars*)pt;
	
	D(bug("sheep_net: packet received\n"));

	if( skb->pkt_type == PACKET_OUTGOING ) {
		// Is this an MOL packet to the world?
		if( skb->protocol == PROT_MAGIC )
			goto drop;

		if( !multicast ) {
			// Drop, unless this is a localhost -> MOL transmission */
			if( addrcmp((char*)&eth_hdr(skb)->h_dest, v->fake_addr) )
				goto drop;

			/* XXX: If it were possible, we would prevent the packet from beeing sent out
			 * on the wire (after having put it on our packet reception queue).
			 * A transmission to a non-existent mac address will unfortunately
			 * be subnet-visible (having a switched network doesn't help). As a
			 * workaround, we change the destination address to the address of
			 * the controller. This way, the packet ought to be discarded by
			 * switches.
			 */
			cpyaddr( &eth_hdr(skb)->h_dest[0], laddr );
		}
	} else {
		// is this a packet to the local host from MOL?
		if( !addrcmp((char*)&eth_hdr(skb)->h_source, v->fake_addr) )
			goto drop;
		
		if( !multicast ) {
			// if the packet is not meant for this host, discard it
			if( addrcmp((char*)&eth_hdr(skb)->h_dest, laddr) )
				goto drop;

			// filter IP-traffic
			if( (skb->protocol == htons(ETH_P_IP)) ) {
				// drop if not addreesed to MOL?
				if( !v->ipfilter || (skb->h.ipiph->daddr != v->ipfilter) )
					goto drop;
				// we don't want this packet interpreted by linux...
				skb->protocol = PROT_MAGIC;
			}
		}
	}
	// Discard packets if queue gets too full
	if( skb_queue_len(&v->queue) > MAX_QUEUE )
		goto drop;

	/* masquerade. The skb is typically has a refcount != 1 so we play safe
	 * and make a copy before modifying it. This also takes care of fragmented
	 * skbuffs (we might receive those if we are attached to a device with support
	 * for it)
	 */
	if( !(skb2=skb_copy(skb, GFP_ATOMIC)) )
		goto drop;
	kfree_skb( skb );
	skb = skb2;

	if( !multicast )
		cpyaddr( &eth_hdr(skb)->h_dest[0], v->fake_addr );

	// We also want the Ethernet header
	skb_push( skb, skb->data - skb->mac.raw );

	// Enqueue packet
	skb_queue_tail( &v->queue, skb );

	// Unblock blocked read
	wake_up_interruptible( &v->wait );
	return 0;

drop:
	kfree_skb( skb );
	return 0;
}


/************************************************************************/
/*	misc device ops							*/
/************************************************************************/

static int 
sheep_net_open( struct inode *inode, struct file *f )
{
	static char fake_addr_[6] = { 0xFE, 0xFD, 0xDE, 0xAD, 0xBE, 0xEF };
	struct SheepVars *v;
	int rc;
	D(bug("sheep_net: open\n"));

	// Must be opened with read permissions
	if( (f->f_flags & O_ACCMODE) == O_WRONLY )
		return -EPERM;

	rc = proto_register(&sheep_proto, 0);
	if (rc) {
		printk(KERN_INFO "Unable to register mol sheep protocol type: %d\n", rc);
		return rc;
	}

	// Allocate private variables
	if( !(v=f->private_data=kmalloc(sizeof(*v), GFP_USER)) )
		return -ENOMEM;
	memset( v, 0, sizeof(*v) );
	memcpy( v->fake_addr, fake_addr_, 6 );

	skb_queue_head_init( &v->queue );
	init_waitqueue_head( &v->wait );
	return 0;
}


static int 
sheep_net_release( struct inode *inode, struct file *f )
{
	struct SheepVars *v = (struct SheepVars *)f->private_data;
	struct sk_buff *skb;
	D(bug("sheep_net: close\n"));

	// Detach from Ethernet card
	if( v->ether ) {
		dev_remove_pack( &v->pt );
		sk_free( v->skt );
		v->skt = NULL;
		dev_put( v->ether );
		v->ether = NULL;
	}

	// Empty packet queue
	while( (skb=skb_dequeue(&v->queue)) )
		kfree_skb(skb);

	proto_unregister(&sheep_proto);

	// Free private variables
	kfree(v);
	return 0;
}

static inline int
get_iovsize( const struct iovec *iv, int count )
{
	int s;
	for( s=0; count-- ; iv++ )
		s += iv->iov_len;
	return s;
}

static int
memcpy_tov( const struct iovec *iv, const char *buf, int s )
{
	while( s > 0 ) {
		int len = min_t( unsigned int, iv->iov_len, s );
		
		if( copy_to_user(iv->iov_base, buf, len) )
			return -EFAULT;
		s -= len;
		buf += len;
		iv++;
	}
	return 0;
}

static int
memcpy_fromv( char *buf, const struct iovec *iv, int s )
{
	while( s > 0 ) {
		int len = min_t( unsigned int, iv->iov_len, s );
		
		if( copy_from_user(buf, iv->iov_base, len) )
			return -EFAULT;
		s -= len;
		buf += len;
		iv++;
	}
	return 0;
}

static ssize_t 
sheep_net_readv( struct file *f, const struct iovec *iv, unsigned long count, loff_t *pos )
{
	struct SheepVars *v = (struct SheepVars *)f->private_data;
	struct sk_buff *skb;
	int size = get_iovsize( iv, count );

	D(bug("sheep_net: read\n"));

	while( !(skb=skb_dequeue(&v->queue)) ) {
		// wait around...
		if( (f->f_flags & O_NONBLOCK))
			return -EAGAIN;

		interruptible_sleep_on( &v->wait );

		if( signal_pending(current) )
			return -EINTR;
	}

	// Pass packet to caller
	if( size > skb->len )
		size = skb->len;
	if( memcpy_tov(iv, skb->data, size) )
		size = -EFAULT;

	kfree_skb( skb );
	return size;
}

static ssize_t 
sheep_net_writev( struct file *f, const struct iovec *iv, unsigned long count, loff_t *off )
{
	struct SheepVars *v = (struct SheepVars *)f->private_data;
	struct sk_buff *skb;
	int size = get_iovsize( iv, count );
	char *p, *laddr;
	D(bug("sheep_net: write\n"));

	// Check packet size
	if( size < sizeof(struct ethhdr) )
		return -EINVAL;
	if( size > 1514 ) {
		printk("sheep_net_write: packet > 1514!\n");
		size = 1514;
	}

	// Interface active?
	if( !v->ether )
		return size;
	laddr = v->ether->dev_addr;

	// Allocate buffer for packet
	if( !(skb=dev_alloc_skb(size)) )
		return -ENOBUFS;

	// Stuff packet in buffer
	p = skb_put( skb, size );
	if( memcpy_fromv(p, iv, size) ) {
		kfree_skb(skb);
		return -EFAULT;
	}

	// Transmit packet
	atomic_add( skb->truesize, &v->skt->wmem_alloc );
	skb->sk = v->skt;
	skb->dev = v->ether;
	skb->priority = 0;
	skb->nh.raw = skb->h.raw = skb->data + v->ether->hard_header_len;
	skb->mac.raw = skb->data;

	// Base the IP-filter on the IP address of outgoing ARPs
	if( eth_hdr(skb)->h_proto == htons(ETH_P_ARP) ) {
		char *s = &skb->data[14+14];	/* source IP-address */
		int n[4];
		if( *(long*)s != v->ipfilter ) {
			v->ipfilter = *(long*)s;
			n[0]=s[0], n[1]=s[1], n[2]=s[2], n[3]=s[3];
			printk("IP-filter: %d.%d.%d.%d\n", n[0], n[1], n[2], n[3] );
		}
	}

	// Is this package addressed solely to the local host?
	if( !addrcmp(skb->data, laddr) && !(skb->data[0] & ETH_ADDR_MULTICAST) ) {
		skb->protocol = eth_type_trans( skb, v->ether );
		netif_rx_ni( skb );
		return size;
	}
	if( skb->data[0] & ETH_ADDR_MULTICAST ) {
		// We can't clone the skb since we will manipulate the data below
		struct sk_buff *lskb = skb_copy( skb, GFP_ATOMIC );
		if( lskb ) {
			lskb->protocol = eth_type_trans( lskb, v->ether );
			netif_rx_ni( lskb );
		}
	}
	// Outgoing packet (will be seen on the wire)
	demasquerade( skb, v );

	skb->protocol = PROT_MAGIC;	// Magic value (we can recognize the packet in sheep_net_receiver) 
	dev_queue_xmit( skb );
	return size;
}

static ssize_t
sheep_net_read( struct file *f, char *buf, size_t count, loff_t *off )
{
	struct iovec iv;
	iv.iov_base = buf;
	iv.iov_len = count;
	return sheep_net_readv( f, &iv, 1, off );
}

static ssize_t 
sheep_net_write( struct file *f, const char *buf, size_t count, loff_t *off )
{
	struct iovec iv;
	iv.iov_len = count;
	iv.iov_base = (char *)buf;
	return sheep_net_writev( f, &iv, 1, off );
}

static unsigned int
sheep_net_poll( struct file *f, struct poll_table_struct *wait )
{
	struct SheepVars *v = (struct SheepVars *)f->private_data;
	D(bug("sheep_net: poll\n"));

	poll_wait( f, &v->wait, wait );

	if( !skb_queue_empty(&v->queue)  )
		return POLLIN | POLLRDNORM;
	return 0;
}

static int 
sheep_net_ioctl( struct inode *inode, struct file *f, unsigned int code, unsigned long arg )
{
	struct SheepVars *v = (struct SheepVars *)f->private_data;
	D(bug("sheep_net: ioctl %04x\n", code));

	switch( code ) {
	// Attach to Ethernet card
	// arg: pointer to name of Ethernet device (char[20])
	case SIOCSIFLINK: {
		char name[20];
		int err;

		// Already attached?
		if( v->ether )
			return -EBUSY;

		// Get Ethernet card name
		if( copy_from_user(name, (void *)arg, 20) )
			return -EFAULT;
		name[19] = 0;

		// Find card
		if( !(v->ether=dev_get_by_name(name)) )
			return -ENODEV;

		// Is it Ethernet?
		if( v->ether->type != ARPHRD_ETHER) {
			err = -EINVAL;
			goto error;
		}

		// Allocate socket
		if( !(v->skt=compat_sk_alloc(0, GFP_USER, 1)) ) {
			err = -ENOMEM;
			goto error;
		}
		skt_set_dead( v->skt );

		// Attach packet handler
		v->pt.type = htons(ETH_P_ALL);
		v->pt.dev = v->ether;
		v->pt.func = sheep_net_receiver;
		//v->pt.data = v;
		dev_add_pack( &v->pt );
		return 0;
error:
		if( v->ether )
			dev_put( v->ether );
		v->ether = NULL;
		return err;
	}

	// Get hardware address of Ethernet card
	// arg: pointer to buffer (6 bytes) to store address
	case SIOCGIFADDR:
		if( copy_to_user((void *)arg, v->fake_addr, 6))
			return -EFAULT;
		return 0;

		// Set the fake HW-address the client will see
	case SIOCSIFADDR:
		if( copy_from_user(v->fake_addr, (void*)arg, 6 ))
			return -EFAULT;
		return 0;

	// Add multicast address
	// arg: pointer to address (6 bytes)
	case SIOCADDMULTI: {
		char addr[6];
		int ret;
		if( !v->ether )
			return -ENODEV;
		if( copy_from_user(addr, (void *)arg, 6))
			return -EFAULT;
		ret = dev_mc_add(v->ether, addr, 6, 0);
		return ret;
	}

	// Remove multicast address
	// arg: pointer to address (6 bytes)
	case SIOCDELMULTI: {
		char addr[6];
		if( !v->ether )
			return -ENODEV;
		if( copy_from_user(addr, (void *)arg, 6))
			return -EFAULT;
		return dev_mc_delete(v->ether, addr, 6, 0);
	}

#if 0
	// Return size of first packet in queue
	case FIONREAD: {
		int count = 0;
		struct sk_buff *skb;
		long flags;
		spin_lock_irqsave(&v->queue.lock, flags );

		skb = skb_peek(&v->queue);
		if( skb )
			count = skb->len;

		spin_unlock_irqrestore(&v->queue.lock, flags );
		return put_user(count, (int *)arg);
	}
#endif
	case SIOC_MOL_GET_IPFILTER:
		return put_user(v->ipfilter, (int *)arg );

	case SIOC_MOL_SET_IPFILTER:
		v->ipfilter = arg;
		return 0;
	}
	return -ENOIOCTLCMD;	
}


/************************************************************************/
/*	init / cleanup							*/
/************************************************************************/

static struct file_operations sheep_net_fops = {
	.owner		= THIS_MODULE,
	.read		= sheep_net_read,
	.write		= sheep_net_write,
	.readv		= sheep_net_readv,
	.writev		= sheep_net_writev,
	.poll		= sheep_net_poll,
	.ioctl		= sheep_net_ioctl,
	.open		= sheep_net_open,
	.release	= sheep_net_release,
};

static struct miscdevice sheep_net_device = {
	.minor		= SHEEP_NET_MINOR,
	.name		= "sheep_net",
	.fops		= &sheep_net_fops
};

int 
init_module( void )
{
	return misc_register( &sheep_net_device );
}

void 
cleanup_module( void )
{
	(void) misc_deregister( &sheep_net_device );
}
