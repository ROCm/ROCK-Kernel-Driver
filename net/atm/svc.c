/* net/atm/svc.c - ATM SVC sockets */

/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */


#include <linux/string.h>
#include <linux/net.h>		/* struct socket, struct net_proto,
				   struct proto_ops */
#include <linux/errno.h>	/* error codes */
#include <linux/kernel.h>	/* printk */
#include <linux/skbuff.h>
#include <linux/wait.h>
#include <linux/sched.h>	/* jiffies and HZ */
#include <linux/fcntl.h>	/* O_NONBLOCK */
#include <linux/init.h>
#include <linux/atm.h>		/* ATM stuff */
#include <linux/atmsap.h>
#include <linux/atmsvc.h>
#include <linux/atmdev.h>
#include <linux/bitops.h>
#include <net/sock.h>		/* for sock_no_* */
#include <asm/uaccess.h>

#include "resources.h"
#include "common.h"		/* common for PVCs and SVCs */
#include "signaling.h"
#include "addr.h"


#if 0
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif


static int svc_create(struct socket *sock,int protocol);


/*
 * Note: since all this is still nicely synchronized with the signaling demon,
 *       there's no need to protect sleep loops with clis. If signaling is
 *       moved into the kernel, that would change.
 */


void svc_callback(struct atm_vcc *vcc)
{
	wake_up(&vcc->sleep);
}




static int svc_shutdown(struct socket *sock,int how)
{
	return 0;
}


static void svc_disconnect(struct atm_vcc *vcc)
{
	DECLARE_WAITQUEUE(wait,current);
	struct sk_buff *skb;

	DPRINTK("svc_disconnect %p\n",vcc);
	if (test_bit(ATM_VF_REGIS,&vcc->flags)) {
		sigd_enq(vcc,as_close,NULL,NULL,NULL);
		add_wait_queue(&vcc->sleep,&wait);
		while (!test_bit(ATM_VF_RELEASED,&vcc->flags) && sigd) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule();
		}
		remove_wait_queue(&vcc->sleep,&wait);
	}
	/* beware - socket is still in use by atmsigd until the last
	   as_indicate has been answered */
	while ((skb = skb_dequeue(&vcc->listenq))) {
		DPRINTK("LISTEN REL\n");
		sigd_enq2(NULL,as_reject,vcc,NULL,NULL,&vcc->qos,0);
		dev_kfree_skb(skb);
	}
	clear_bit(ATM_VF_REGIS,&vcc->flags);
	clear_bit(ATM_VF_RELEASED,&vcc->flags);
	clear_bit(ATM_VF_CLOSE,&vcc->flags);
	/* ... may retry later */
}


static int svc_release(struct socket *sock)
{
	struct atm_vcc *vcc;

	if (!sock->sk) return 0;
	vcc = ATM_SD(sock);
	DPRINTK("svc_release %p\n",vcc);
	clear_bit(ATM_VF_READY,&vcc->flags);
	atm_release_vcc_sk(sock->sk,0);
	svc_disconnect(vcc);
	    /* VCC pointer is used as a reference, so we must not free it
	       (thereby subjecting it to re-use) before all pending connections
	        are closed */
	free_atm_vcc_sk(sock->sk);
	return 0;
}


static int svc_bind(struct socket *sock,struct sockaddr *sockaddr,
    int sockaddr_len)
{
	DECLARE_WAITQUEUE(wait,current);
	struct sockaddr_atmsvc *addr;
	struct atm_vcc *vcc;

	if (sockaddr_len != sizeof(struct sockaddr_atmsvc)) return -EINVAL;
	if (sock->state == SS_CONNECTED) return -EISCONN;
	if (sock->state != SS_UNCONNECTED) return -EINVAL;
	vcc = ATM_SD(sock);
	if (test_bit(ATM_VF_SESSION,&vcc->flags)) return -EINVAL;
	addr = (struct sockaddr_atmsvc *) sockaddr;
	if (addr->sas_family != AF_ATMSVC) return -EAFNOSUPPORT;
	clear_bit(ATM_VF_BOUND,&vcc->flags);
	    /* failing rebind will kill old binding */
	/* @@@ check memory (de)allocation on rebind */
	if (!test_bit(ATM_VF_HASQOS,&vcc->flags)) return -EBADFD;
	vcc->local = *addr;
	vcc->reply = WAITING;
	sigd_enq(vcc,as_bind,NULL,NULL,&vcc->local);
	add_wait_queue(&vcc->sleep,&wait);
	while (vcc->reply == WAITING && sigd) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule();
	}
	remove_wait_queue(&vcc->sleep,&wait);
	clear_bit(ATM_VF_REGIS,&vcc->flags); /* doesn't count */
	if (!sigd) return -EUNATCH;
        if (!vcc->reply) set_bit(ATM_VF_BOUND,&vcc->flags);
	return vcc->reply;
}


static int svc_connect(struct socket *sock,struct sockaddr *sockaddr,
    int sockaddr_len,int flags)
{
	DECLARE_WAITQUEUE(wait,current);
	struct sockaddr_atmsvc *addr;
	struct atm_vcc *vcc = ATM_SD(sock);
	int error;

	DPRINTK("svc_connect %p\n",vcc);
	if (sockaddr_len != sizeof(struct sockaddr_atmsvc)) return -EINVAL;
	if (sock->state == SS_CONNECTED) return -EISCONN;
	if (sock->state == SS_CONNECTING) {
		if (vcc->reply == WAITING) return -EALREADY;
		sock->state = SS_UNCONNECTED;
		if (vcc->reply) return vcc->reply;
	}
	else {
		int error;

		if (sock->state != SS_UNCONNECTED) return -EINVAL;
		if (test_bit(ATM_VF_SESSION,&vcc->flags)) return -EINVAL;
		addr = (struct sockaddr_atmsvc *) sockaddr;
		if (addr->sas_family != AF_ATMSVC) return -EAFNOSUPPORT;
		if (!test_bit(ATM_VF_HASQOS,&vcc->flags)) return -EBADFD;
		if (vcc->qos.txtp.traffic_class == ATM_ANYCLASS ||
		    vcc->qos.rxtp.traffic_class == ATM_ANYCLASS)
			return -EINVAL;
		if (!vcc->qos.txtp.traffic_class &&
		    !vcc->qos.rxtp.traffic_class) return -EINVAL;
		vcc->remote = *addr;
		vcc->reply = WAITING;
		sigd_enq(vcc,as_connect,NULL,NULL,&vcc->remote);
		if (flags & O_NONBLOCK) {
			sock->state = SS_CONNECTING;
			return -EINPROGRESS;
		}
		add_wait_queue(&vcc->sleep,&wait);
		error = 0;
		while (vcc->reply == WAITING && sigd) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			if (!signal_pending(current)) continue;
			DPRINTK("*ABORT*\n");
			/*
			 * This is tricky:
			 *   Kernel ---close--> Demon
			 *   Kernel <--close--- Demon
		         * or
			 *   Kernel ---close--> Demon
			 *   Kernel <--error--- Demon
			 * or
			 *   Kernel ---close--> Demon
			 *   Kernel <--okay---- Demon
			 *   Kernel <--close--- Demon
			 */
			sigd_enq(vcc,as_close,NULL,NULL,NULL);
			while (vcc->reply == WAITING && sigd) {
				set_current_state(TASK_UNINTERRUPTIBLE);
				schedule();
			}
			if (!vcc->reply)
				while (!test_bit(ATM_VF_RELEASED,&vcc->flags)
				    && sigd) {
					set_current_state(TASK_UNINTERRUPTIBLE);
					schedule();
				}
			clear_bit(ATM_VF_REGIS,&vcc->flags);
			clear_bit(ATM_VF_RELEASED,&vcc->flags);
			clear_bit(ATM_VF_CLOSE,&vcc->flags);
			    /* we're gone now but may connect later */
			error = -EINTR;
			break;
		}
		remove_wait_queue(&vcc->sleep,&wait);
		if (error) return error;
		if (!sigd) return -EUNATCH;
		if (vcc->reply) return vcc->reply;
	}
/*
 * Not supported yet
 *
 * #ifndef CONFIG_SINGLE_SIGITF
 */
	vcc->qos.txtp.max_pcr = SELECT_TOP_PCR(vcc->qos.txtp);
	vcc->qos.txtp.pcr = 0;
	vcc->qos.txtp.min_pcr = 0;
/*
 * #endif
 */
	if (!(error = atm_connect(sock,vcc->itf,vcc->vpi,vcc->vci)))
		sock->state = SS_CONNECTED;
	else (void) svc_disconnect(vcc);
	return error;
}


static int svc_listen(struct socket *sock,int backlog)
{
	DECLARE_WAITQUEUE(wait,current);
	struct atm_vcc *vcc = ATM_SD(sock);

	DPRINTK("svc_listen %p\n",vcc);
	/* let server handle listen on unbound sockets */
	if (test_bit(ATM_VF_SESSION,&vcc->flags)) return -EINVAL;
	vcc->reply = WAITING;
	sigd_enq(vcc,as_listen,NULL,NULL,&vcc->local);
	add_wait_queue(&vcc->sleep,&wait);
	while (vcc->reply == WAITING && sigd) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule();
	}
	remove_wait_queue(&vcc->sleep,&wait);
	if (!sigd) return -EUNATCH;
	set_bit(ATM_VF_LISTEN,&vcc->flags);
	vcc->backlog_quota = backlog > 0 ? backlog : ATM_BACKLOG_DEFAULT;
	return vcc->reply;
}


static int svc_accept(struct socket *sock,struct socket *newsock,int flags)
{
	struct sk_buff *skb;
	struct atmsvc_msg *msg;
	struct atm_vcc *old_vcc = ATM_SD(sock);
	struct atm_vcc *new_vcc;
	int error;

	error = svc_create(newsock,0);
	if (error)
		return error;

	new_vcc = ATM_SD(newsock);

	DPRINTK("svc_accept %p -> %p\n",old_vcc,new_vcc);
	while (1) {
		DECLARE_WAITQUEUE(wait,current);

		add_wait_queue(&old_vcc->sleep,&wait);
		while (!(skb = skb_dequeue(&old_vcc->listenq)) && sigd) {
			if (test_bit(ATM_VF_RELEASED,&old_vcc->flags)) break;
			if (test_bit(ATM_VF_CLOSE,&old_vcc->flags)) {
				error = old_vcc->reply;
				break;
			}
			if (flags & O_NONBLOCK) {
				error = -EAGAIN;
				break;
			}
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			if (signal_pending(current)) {
				error = -ERESTARTSYS;
				break;
			}
		}
		remove_wait_queue(&old_vcc->sleep,&wait);
		if (error) return error;
		if (!skb) return -EUNATCH;
		msg = (struct atmsvc_msg *) skb->data;
		new_vcc->qos = msg->qos;
		set_bit(ATM_VF_HASQOS,&new_vcc->flags);
		new_vcc->remote = msg->svc;
		new_vcc->local = msg->local;
		new_vcc->sap = msg->sap;
		error = atm_connect(newsock,msg->pvc.sap_addr.itf,
		    msg->pvc.sap_addr.vpi,msg->pvc.sap_addr.vci);
		dev_kfree_skb(skb);
		old_vcc->backlog_quota++;
		if (error) {
			sigd_enq2(NULL,as_reject,old_vcc,NULL,NULL,
			    &old_vcc->qos,error);
			return error == -EAGAIN ? -EBUSY : error;
		}
		/* wait should be short, so we ignore the non-blocking flag */
		new_vcc->reply = WAITING;
		sigd_enq(new_vcc,as_accept,old_vcc,NULL,NULL);
		add_wait_queue(&new_vcc->sleep,&wait);
		while (new_vcc->reply == WAITING && sigd) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule();
		}
		remove_wait_queue(&new_vcc->sleep,&wait);
		if (!sigd) return -EUNATCH;
		if (!new_vcc->reply) break;
		if (new_vcc->reply != -ERESTARTSYS) return new_vcc->reply;
	}
	newsock->state = SS_CONNECTED;
	return 0;
}


static int svc_getname(struct socket *sock,struct sockaddr *sockaddr,
    int *sockaddr_len,int peer)
{
	struct sockaddr_atmsvc *addr;

	*sockaddr_len = sizeof(struct sockaddr_atmsvc);
	addr = (struct sockaddr_atmsvc *) sockaddr;
	memcpy(addr,peer ? &ATM_SD(sock)->remote : &ATM_SD(sock)->local,
	    sizeof(struct sockaddr_atmsvc));
	return 0;
}


int svc_change_qos(struct atm_vcc *vcc,struct atm_qos *qos)
{
	DECLARE_WAITQUEUE(wait,current);

	vcc->reply = WAITING;
	sigd_enq2(vcc,as_modify,NULL,NULL,&vcc->local,qos,0);
	add_wait_queue(&vcc->sleep,&wait);
	while (vcc->reply == WAITING && !test_bit(ATM_VF_RELEASED,&vcc->flags)
	    && sigd) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule();
	}
	remove_wait_queue(&vcc->sleep,&wait);
	if (!sigd) return -EUNATCH;
	return vcc->reply;
}


static int svc_setsockopt(struct socket *sock,int level,int optname,
    char *optval,int optlen)
{
	struct atm_vcc *vcc;

	if (!__SO_LEVEL_MATCH(optname, level) || optname != SO_ATMSAP ||
	    optlen != sizeof(struct atm_sap))
		return atm_setsockopt(sock,level,optname,optval,optlen);
	vcc = ATM_SD(sock);
	if (copy_from_user(&vcc->sap,optval,optlen)) return -EFAULT;
	set_bit(ATM_VF_HASSAP,&vcc->flags);
	return 0;
}


static int svc_getsockopt(struct socket *sock,int level,int optname,
    char *optval,int *optlen)
{
	int len;

	if (!__SO_LEVEL_MATCH(optname, level) || optname != SO_ATMSAP)
		return atm_getsockopt(sock,level,optname,optval,optlen);
	if (get_user(len,optlen)) return -EFAULT;
	if (len != sizeof(struct atm_sap)) return -EINVAL;
	return copy_to_user(optval,&ATM_SD(sock)->sap,sizeof(struct atm_sap)) ?
	    -EFAULT : 0;
}


static struct proto_ops SOCKOPS_WRAPPED(svc_proto_ops) = {
	family:		PF_ATMSVC,

	release:	svc_release,
	bind:		svc_bind,
	connect:	svc_connect,
	socketpair:	sock_no_socketpair,
	accept:		svc_accept,
	getname:	svc_getname,
	poll:		atm_poll,
	ioctl:		atm_ioctl,
	listen:		svc_listen,
	shutdown:	svc_shutdown,
	setsockopt:	svc_setsockopt,
	getsockopt:	svc_getsockopt,
	sendmsg:	atm_sendmsg,
	recvmsg:	atm_recvmsg,
	mmap:		sock_no_mmap,
};


#include <linux/smp_lock.h>
SOCKOPS_WRAP(svc_proto, PF_ATMSVC);

static int svc_create(struct socket *sock,int protocol)
{
	int error;

	sock->ops = &svc_proto_ops;
	error = atm_create(sock,protocol,AF_ATMSVC);
	if (error) return error;
	ATM_SD(sock)->callback = svc_callback;
	ATM_SD(sock)->local.sas_family = AF_ATMSVC;
	ATM_SD(sock)->remote.sas_family = AF_ATMSVC;
	return 0;
}


static struct net_proto_family svc_family_ops = {
	PF_ATMSVC,
	svc_create,
	0,			/* no authentication */
	0,			/* no encryption */
	0			/* no encrypt_net */
};


/*
 *	Initialize the ATM SVC protocol family
 */

static int __init atmsvc_init(void)
{
	if (sock_register(&svc_family_ops) < 0) {
		printk(KERN_ERR "ATMSVC: can't register");
		return -1;
	}
	return 0;
}

module_init(atmsvc_init);
