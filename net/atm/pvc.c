/* net/atm/pvc.c - ATM PVC sockets */

/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */


#include <linux/config.h>
#include <linux/net.h>		/* struct socket, struct net_proto,
				   struct proto_ops */
#include <linux/atm.h>		/* ATM stuff */
#include <linux/atmdev.h>	/* ATM devices */
#include <linux/atmclip.h>	/* Classical IP over ATM */
#include <linux/errno.h>	/* error codes */
#include <linux/kernel.h>	/* printk */
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/bitops.h>
#include <net/sock.h>		/* for sock_no_* */
#ifdef CONFIG_ATM_CLIP
#include <net/atmclip.h>
#endif

#include "resources.h"		/* devs and vccs */
#include "common.h"		/* common for PVCs and SVCs */

#ifndef NULL
#define NULL 0
#endif


static int pvc_shutdown(struct socket *sock,int how)
{
	return 0;
}


static int pvc_bind(struct socket *sock,struct sockaddr *sockaddr,
    int sockaddr_len)
{
	struct sockaddr_atmpvc *addr;
	struct atm_vcc *vcc;

	if (sockaddr_len != sizeof(struct sockaddr_atmpvc)) return -EINVAL;
	addr = (struct sockaddr_atmpvc *) sockaddr;
	if (addr->sap_family != AF_ATMPVC) return -EAFNOSUPPORT;
	vcc = ATM_SD(sock);
	if (!test_bit(ATM_VF_HASQOS,&vcc->flags)) return -EBADFD;
	if (test_bit(ATM_VF_PARTIAL,&vcc->flags)) {
		if (vcc->vpi != ATM_VPI_UNSPEC) addr->sap_addr.vpi = vcc->vpi;
		if (vcc->vci != ATM_VCI_UNSPEC) addr->sap_addr.vci = vcc->vci;
	}
	return atm_connect(sock,addr->sap_addr.itf,addr->sap_addr.vpi,
	    addr->sap_addr.vci);
}


static int pvc_connect(struct socket *sock,struct sockaddr *sockaddr,
    int sockaddr_len,int flags)
{
	return pvc_bind(sock,sockaddr,sockaddr_len);
}


static int pvc_getname(struct socket *sock,struct sockaddr *sockaddr,
    int *sockaddr_len,int peer)
{
	struct sockaddr_atmpvc *addr;
	struct atm_vcc *vcc = ATM_SD(sock);

	if (!vcc->dev || !test_bit(ATM_VF_ADDR,&vcc->flags)) return -ENOTCONN;
        *sockaddr_len = sizeof(struct sockaddr_atmpvc);
	addr = (struct sockaddr_atmpvc *) sockaddr;
	addr->sap_family = AF_ATMPVC;
	addr->sap_addr.itf = vcc->dev->number;
	addr->sap_addr.vpi = vcc->vpi;
	addr->sap_addr.vci = vcc->vci;
	return 0;
}


static struct proto_ops SOCKOPS_WRAPPED(pvc_proto_ops) = {
	family:		PF_ATMPVC,

	release:	atm_release,
	bind:		pvc_bind,
	connect:	pvc_connect,
	socketpair:	sock_no_socketpair,
	accept:		sock_no_accept,
	getname:	pvc_getname,
	poll:		atm_poll,
	ioctl:		atm_ioctl,
	listen:		sock_no_listen,
	shutdown:	pvc_shutdown,
	setsockopt:	atm_setsockopt,
	getsockopt:	atm_getsockopt,
	sendmsg:	atm_sendmsg,
	recvmsg:	atm_recvmsg,
	mmap:		sock_no_mmap,
};


#include <linux/smp_lock.h>
SOCKOPS_WRAP(pvc_proto, PF_ATMPVC);


static int pvc_create(struct socket *sock,int protocol)
{
	sock->ops = &pvc_proto_ops;
	return atm_create(sock,protocol,PF_ATMPVC);
}


static struct net_proto_family pvc_family_ops = {
	PF_ATMPVC,
	pvc_create,
	0,			/* no authentication */
	0,			/* no encryption */
	0			/* no encrypt_net */
};


/*
 *	Initialize the ATM PVC protocol family
 */


static int __init atmpvc_init(void)
{
	int error;

	error = sock_register(&pvc_family_ops);
	if (error < 0) {
		printk(KERN_ERR "ATMPVC: can't register (%d)",error);
		return error;
	}
#ifdef CONFIG_ATM_CLIP
	atm_clip_init();
#endif
#ifdef CONFIG_PROC_FS
	error = atm_proc_init();
	if (error) printk("atm_proc_init fails with %d\n",error);
#endif
	return 0;
}

module_init(atmpvc_init);
