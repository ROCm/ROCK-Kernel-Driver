/*
 * llc_sock.c - LLC User Interface SAPs
 * Description:
 *   Functions in this module are implementation of socket based llc
 *   communications for the Linux operating system. Support of llc class
 *   one and class two is provided via SOCK_DGRAM and SOCK_STREAM
 *   respectively.
 *
 *   An llc2 connection is (mac + sap), only one llc2 sap connection
 *   is allowed per mac. Though one sap may have multiple mac + sap
 *   connections.
 *
 * Copyright (c) 2001 by Jay Schulist <jschlst@samba.org>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <asm/uaccess.h>
#include <asm/ioctls.h>
#include <linux/proc_fs.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/errno.h>
#include <net/sock.h>
#include <net/llc_if.h>
#include <net/llc_sap.h>
#include <net/llc_pdu.h>
#include <net/llc_conn.h>
#include <linux/llc.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/init.h>

/* remember: uninitialized global data is zeroed because its in .bss */
static u16 llc_ui_sap_last_autoport = LLC_SAP_DYN_START;
static u16 llc_ui_sap_link_no_max[256];
static u8 llc_ui_addrany[IFHWADDRLEN];
static struct sockaddr_llc llc_ui_addrnull;
static struct proto_ops llc_ui_ops;
static struct sock *llc_ui_sockets;
static rwlock_t llc_ui_sockets_lock = RW_LOCK_UNLOCKED;

static int llc_ui_indicate(struct llc_prim_if_block *prim);
static int llc_ui_confirm(struct llc_prim_if_block *prim);
static int llc_ui_wait_for_conn(struct sock *sk, int seconds);
static int llc_ui_wait_for_disc(struct sock *sk, int seconds);

/**
 *	llc_ui_next_link_no - return the next unused link number for a sap
 *	@sap: Address of sap to get link number from.
 *
 *	Return the next unused link number for a given sap.
 */
static __inline__ u16 llc_ui_next_link_no(int sap)
{
	return llc_ui_sap_link_no_max[sap]++;
}

/**
 *	llc_ui_mac_match - determines if two mac addresses are the same
 *	@mac1: First mac address to compare.
 *	@mac2: Second mac address to compare.
 *
 *	Determines if two given mac address are the same.  Returns 0 if there
 *	is not a complete match up to len, 1 if a complete match up to len is
 *	found.
 */
static __inline__ u8 llc_ui_mac_match(u8 *mac1, u8 *mac2)
{
	return !memcmp(mac1, mac2, IFHWADDRLEN);
}

/**
 *	llc_ui_mac_null - determines if a address is a null mac address
 *	@mac: Mac address to test if null.
 *
 *	Determines if a given address is a null mac address.  Returns 0 if the
 *	address is not a null mac, 1 if the address is a null mac.
 */
static __inline__ u8 llc_ui_mac_null(u8 *mac)
{
	return !memcmp(mac, llc_ui_addrany, IFHWADDRLEN);
}

/**
 *	llc_ui_addr_null - determines if a address structure is null
 *	@addr: Address to test if null.
 */
static __inline__ u8 llc_ui_addr_null(struct sockaddr_llc *addr)
{
	return !memcmp(addr, &llc_ui_addrnull, sizeof(*addr));
}

/**
 *	llc_ui_protocol_type - return eth protocol for ARP header type
 *	@arphrd: ARP header type.
 *
 *	Given an ARP header type return the corresponding ethernet protocol.
 *	Returns  0 if ARP header type not supported or the corresponding
 *	ethernet protocol type.
 */
static __inline__ u16 llc_ui_protocol_type(u16 arphrd)
{
	u16 rc = htons(ETH_P_802_2);

	if (arphrd == ARPHRD_IEEE802_TR)
		rc = htons(ETH_P_TR_802_2);
	return rc;
}

/**
 *	llc_ui_header_len - return length of llc header based on operation
 *	@sk: Socket which contains a valid llc socket type.
 *	@addr: Complete sockaddr_llc structure received from the user.
 *
 *	Provide the length of the llc header depending on what kind of
 *	operation the user would like to perform and the type of socket.
 *	Returns the correct llc header length.
 */
static __inline__ u8 llc_ui_header_len(struct sock *sk,
				       struct sockaddr_llc *addr)
{
	u8 rc = LLC_PDU_LEN_U;

	if (addr->sllc_test || addr->sllc_xid)
		rc = LLC_PDU_LEN_U;
	else if (sk->type == SOCK_STREAM)
		rc = LLC_PDU_LEN_I;
	return rc;
}

/**
 *	llc_ui_send_conn - send connect command for new llc2 connection
 *	@sap : Sap the socket is bound to.
 *	@addr: Source and destination fields provided by the user.
 *	@dev : Device which this connection should use.
 *	@link: Link number to assign to this connection.
 *
 *	Send a connect command to the llc layer for a new llc2 connection.
 *	Returns 0 upon success, non-zero if action didn't succeed.
 */
static int llc_ui_send_conn(struct sock *sk, struct llc_sap *sap,
			    struct sockaddr_llc *addr,
			    struct net_device *dev, int link)
{
	struct llc_ui_opt *llc_ui = llc_ui_sk(sk);
	union llc_u_prim_data prim_data;
	struct llc_prim_if_block prim;

	prim.data		= &prim_data;
	prim.sap		= sap;
	prim.prim		= LLC_CONN_PRIM;
	prim_data.conn.dev	= dev;
	prim_data.conn.link	= link;
	prim_data.conn.sk	= NULL;
	prim_data.conn.handler	= sk;
	prim_data.conn.pri	= 0;
	prim_data.conn.saddr.lsap = llc_ui->addr.sllc_ssap;
	prim_data.conn.daddr.lsap = addr->sllc_dsap;
	memcpy(prim_data.conn.saddr.mac, dev->dev_addr, IFHWADDRLEN);
	memcpy(prim_data.conn.daddr.mac, addr->sllc_dmac, IFHWADDRLEN);
	return sap->req(&prim);
}

/**
 *	llc_ui_send_disc - send disc command to llc layer
 *	@sk: Socket with valid llc information.
 *
 *	Send a disconnect command to the llc layer for an established
 *	llc2 connection.
 *	Returns 0 upon success, non-zero if action did not succeed.
 */
static int llc_ui_send_disc(struct sock *sk)
{
	struct llc_ui_opt *llc_ui = llc_ui_sk(sk);
	union llc_u_prim_data prim_data;
	struct llc_prim_if_block prim;
	int rc = 0;

	if (sk->type != SOCK_STREAM || sk->state != TCP_ESTABLISHED)
		goto out;
	sk->state	    = TCP_CLOSING;
	prim.data	    = &prim_data;
	prim.sap	    = llc_ui->sap;
	prim.prim	    = LLC_DISC_PRIM;
	prim_data.disc.sk   = llc_ui->core_sk;
	prim_data.disc.link = llc_ui->link;
	rc = llc_ui->sap->req(&prim);
out:
	return rc;
}

/**
 *	llc_ui_send_data - send data via reliable llc2 connection
 *	@sap: Sap the socket is bound to.
 *	@sk: Connection the socket is using.
 *	@skb: Data the user wishes to send.
 *	@addr: Source and destination fields provided by the user.
 *
 *	Send data via reliable llc2 connection.
 *	Returns 0 upon success,  non-zero if action did not succeed.
 */
static int llc_ui_send_data(struct llc_sap *sap, struct sock* sk,
			    struct sk_buff *skb, struct sockaddr_llc *addr)
{
	union llc_u_prim_data prim_data;
	struct llc_prim_if_block prim;
	struct llc_ui_opt* llc_ui = llc_ui_sk(sk);
	struct llc_opt* llc_core = llc_sk(llc_ui->core_sk);
	int rc;

	prim.data	   = &prim_data;
	prim.sap	   = sap;
	prim.prim	   = LLC_DATA_PRIM;
	prim_data.data.skb = skb;
	prim_data.data.pri = 0;
	prim_data.data.sk  = llc_ui->core_sk;
	skb->protocol	   = llc_ui_protocol_type(addr->sllc_arphrd);
	sock_hold(sk);
try:
	rc = sap->req(&prim);
	if (rc != -EBUSY)
		goto out;
	rc = wait_event_interruptible(sk->socket->wait, !llc_ui->core_sk ||
				      !llc_core->failed_data_req);
	if (!rc)
		goto try;
	if (!llc_ui->core_sk)
		rc = -ENOTCONN;
out:
	sock_put(sk);
	return rc;
}

/**
 *	llc_ui_send_llc1 - send llc1 prim data block to llc layer.
 *	@sap      : Sap the socket is bound to.
 *	@skb      : Data the user wishes to send.
 *	@addr     : Source and destination fields provided by the user.
 *	@primitive: Action the llc layer should perform.
 *
 *	Send an llc1 primitive data block to the llc layer for processing.
 *	This function is used for test, xid and unit_data messages.
 *	Returns 0 upon success, non-zero if action did not succeed.
 */
static int llc_ui_send_llc1(struct llc_sap *sap, struct sk_buff *skb,
			    struct sockaddr_llc *addr, int primitive)
{
	union llc_u_prim_data prim_data;
	struct llc_prim_if_block prim;

	prim.data 		  = &prim_data;
	prim.sap 		  = sap;
	prim.prim		  = primitive;
	prim_data.test.skb 	  = skb;
	prim_data.test.saddr.lsap = sap->laddr.lsap;
	prim_data.test.daddr.lsap = addr->sllc_dsap;
	skb->protocol = llc_ui_protocol_type(addr->sllc_arphrd);
	memcpy(prim_data.test.saddr.mac, skb->dev->dev_addr, IFHWADDRLEN);
	memcpy(prim_data.test.daddr.mac, addr->sllc_dmac, IFHWADDRLEN);
	return sap->req(&prim);
}

/**
 *	llc_ui_find_sap - returns sap struct that matches sap number specified
 *	@sap: Sap number to search for.
 *
 *	Search the local socket list and return the first instance of the sap
 *	structure which matches the sap number the user specified.
 *	Returns llc_sap upon match, %NULL otherwise.
 */
static __inline__ struct llc_sap *llc_ui_find_sap(u8 sap)
{
	struct sock *sk;
	struct llc_sap *s = NULL;

	read_lock_bh(&llc_ui_sockets_lock);
	for (sk = llc_ui_sockets; sk; sk = sk->next) {
		struct llc_ui_opt *llc_ui = llc_ui_sk(sk);

		if (!llc_ui->sap)
			continue;
		if (llc_ui->sap->laddr.lsap == sap) {
			s = llc_ui->sap;
			break;
		}
	}
	read_unlock_bh(&llc_ui_sockets_lock);
	return s;
}

static struct sock *__llc_ui_find_sk_by_exact(struct llc_addr *laddr,
					      struct llc_addr *daddr)
{
	struct sock *sk;

	for (sk = llc_ui_sockets; sk; sk = sk->next) {
		struct llc_ui_opt *llc_ui = llc_ui_sk(sk);

		if (llc_ui->addr.sllc_ssap == laddr->lsap &&
		    llc_ui->addr.sllc_dsap == daddr->lsap &&
		    llc_ui_mac_null(llc_ui->addr.sllc_mmac) &&
		    llc_ui_mac_match(llc_ui->addr.sllc_smac, laddr->mac) &&
		    llc_ui_mac_match(llc_ui->addr.sllc_dmac, daddr->mac))
			break;
	}
	return sk;
}

/**
 *	__llc_ui_find_sk_by_addr - return socket matching local mac + sap.
 *	@addr: Local address to match.
 *
 *	Search the local socket list and return the socket which has a matching
 *	local (mac + sap) address (allows null mac). This search will work on
 *	unconnected and connected sockets, though find_by_link_no is recommend
 *	for connected sockets.
 *	Returns sock upon match, %NULL otherwise.
 */
static struct sock *__llc_ui_find_sk_by_addr(struct llc_addr *laddr,
					     struct llc_addr *daddr,
					     struct net_device *dev)
{
	struct sock *sk, *tmp_sk;

	for (sk = llc_ui_sockets; sk; sk = sk->next) {
		struct llc_ui_opt *llc_ui = llc_ui_sk(sk);

		if (llc_ui->addr.sllc_ssap != laddr->lsap)
			continue;
		if (llc_ui_mac_null(llc_ui->addr.sllc_smac)) {
			if (!llc_ui_mac_null(llc_ui->addr.sllc_mmac) &&
			    !llc_ui_mac_match(llc_ui->addr.sllc_mmac,
				    	      laddr->mac))
				continue;
			break;
		}
		if (dev && !llc_ui_mac_null(llc_ui->addr.sllc_mmac) &&
		    llc_ui_mac_match(llc_ui->addr.sllc_mmac, laddr->mac) &&
		    llc_ui_mac_match(llc_ui->addr.sllc_smac, dev->dev_addr))
			break;
		if (dev->flags & IFF_LOOPBACK)
			break;
		if (!llc_ui_mac_match(llc_ui->addr.sllc_smac, laddr->mac))
			continue;
		tmp_sk = __llc_ui_find_sk_by_exact(laddr, daddr);
		if (tmp_sk) {
			sk = tmp_sk;
			break;
		}
		if (llc_ui_mac_null(llc_ui->addr.sllc_dmac))
			break;
	}
	return sk;
}

static struct sock *llc_ui_find_sk_by_addr(struct llc_addr *addr,
					   struct llc_addr *daddr,
					   struct net_device *dev)
{
	struct sock *sk;

	read_lock(&llc_ui_sockets_lock);
	sk = __llc_ui_find_sk_by_addr(addr, daddr, dev);
	if (sk)
		sock_hold(sk);
	read_unlock(&llc_ui_sockets_lock);
	return sk;
}

static struct sock *llc_ui_bh_find_sk_by_addr(struct llc_addr *addr,
					      struct llc_addr *daddr,
					      struct net_device *dev)
{
	struct sock *sk;

	read_lock_bh(&llc_ui_sockets_lock);
	sk = __llc_ui_find_sk_by_addr(addr, daddr, dev);
	if (sk)
		sock_hold(sk);
	read_unlock_bh(&llc_ui_sockets_lock);
	return sk;
}

/**
 *	llc_ui_insert_socket - insert socket into list
 *	@sk: Socket to insert.
 *
 *	Insert a socket into the local llc socket list.
 */
static __inline__ void llc_ui_insert_socket(struct sock *sk)
{
	write_lock_bh(&llc_ui_sockets_lock);
	sk->next = llc_ui_sockets;
	if (sk->next)
		llc_ui_sockets->pprev = &sk->next;
	llc_ui_sockets = sk;
	sk->pprev = &llc_ui_sockets;
	sock_hold(sk);
	write_unlock_bh(&llc_ui_sockets_lock);
}

/**
 *	llc_ui_remove_socket - remove socket from list
 *	@sk: Socket to remove.
 *
 *	Remove a socket from the local llc socket list.
 */
static __inline__ void llc_ui_remove_socket(struct sock *sk)
{
	write_lock_bh(&llc_ui_sockets_lock);
	if (sk->pprev) {
		if (sk->next)
			sk->next->pprev = sk->pprev;
		*sk->pprev = sk->next;
		sk->pprev = NULL;
		/* this only makes sense if the socket was inserted on the
		 * list, if sk->pprev is NULL it wasn't
		 */
		sock_put(sk);
	}
	write_unlock_bh(&llc_ui_sockets_lock);
}

/**
 *	llc_ui_destroy_sk - destroy socket
 *	@data: Socket which is to be destroyed.
 *
 *	Really destroy the socket.
 */
static void llc_ui_destroy_sk(struct sock *sk)
{
	skb_queue_purge(&sk->receive_queue);
	skb_queue_purge(&sk->write_queue);
	sock_put(sk);
	MOD_DEC_USE_COUNT;
}

/**
 *	llc_ui_destroy_timer - try to destroy socket again
 *	@data: Socket which is to be destroyed.
 *
 *	Attempt to destroy a socket which was previously destroyed but
 *	was still in use at the time.
 */
static void llc_ui_destroy_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;

	if (!atomic_read(&sk->wmem_alloc) &&
	    !atomic_read(&sk->rmem_alloc) && sk->dead)
		llc_ui_destroy_sk(sk);
	else {
		sk->timer.expires = jiffies + SOCK_DESTROY_TIME;
		add_timer(&sk->timer);
	}
}

/**
 *	llc_ui_create - alloc and init a new llc_ui socket
 *	@sock: Socket to initialize and attach allocated sk to.
 *	@protocol: Unused.
 *
 *	Allocate and initialize a new llc_ui socket, validate the user wants a
 *	socket type we have available.
 *	Returns 0 upon success, negative upon failure.
 */
static int llc_ui_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	struct llc_ui_opt *llc_ui;
	int rc = -ESOCKTNOSUPPORT;

	MOD_INC_USE_COUNT;
	if (sock->type != SOCK_DGRAM && sock->type != SOCK_STREAM)
		goto decmod;
	rc = -ENOMEM;
	sk = sk_alloc(PF_LLC, GFP_KERNEL, 1, NULL);
	if (!sk)
		goto decmod;
	llc_ui = kmalloc(sizeof(*llc_ui), GFP_KERNEL);
	if (!llc_ui)
		goto outsk;
	memset(llc_ui, 0, sizeof(*llc_ui));
	rc = 0;
	sock_init_data(sock, sk);
	llc_ui_sk(sk) = llc_ui;
	sock->ops = &llc_ui_ops;
out:
	return rc;
outsk:
	sk_free(sk);
decmod:
	MOD_DEC_USE_COUNT;
	goto out;
}

/**
 *	llc_ui_release - shutdown socket
 *	@sock: Socket to release.
 *
 *	Shutdown and deallocate an existing socket.
 */
static int llc_ui_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct llc_ui_opt *llc_ui;

	if (!sk)
		goto out;
	llc_ui = llc_ui_sk(sk);
	if (llc_ui->core_sk && !llc_ui_send_disc(sk))
		llc_ui_wait_for_disc(sk, 255);
	llc_ui_remove_socket(sk);
	if (llc_ui->sap && !llc_ui_find_sap(llc_ui->sap->laddr.lsap))
		llc_sap_close(llc_ui->sap);
	sock_orphan(sk);
	sock->sk = NULL;
	if (!atomic_read(&sk->wmem_alloc) &&
	    !atomic_read(&sk->rmem_alloc) && sk->dead)
		llc_ui_destroy_sk(sk);
	else {
		init_timer(&sk->timer);
		sk->timer.expires = jiffies + SOCK_DESTROY_TIME;
		sk->timer.function = llc_ui_destroy_timer;
		sk->timer.data = (unsigned long)sk;
		add_timer(&sk->timer);
	}
out:
	return 0;
}

/**
 *	llc_ui_autoport - provide dynamicly allocate SAP number
 *
 *	Provide the caller with a dynamicly allocated SAP number according
 *	to the rules that are set in this function. Returns: 0, upon failure,
 *	SAP number otherwise.
 */
static int llc_ui_autoport(void)
{
	struct llc_sap *sap;
	int i, tries = 0;

	while (tries < LLC_SAP_DYN_TRIES) {
		for (i = llc_ui_sap_last_autoport;
		     i < LLC_SAP_DYN_STOP; i += 2) {
			sap = llc_ui_find_sap(i);
			if (!sap) {
				llc_ui_sap_last_autoport = i + 2;
				goto out;
			}
		}
		llc_ui_sap_last_autoport = LLC_SAP_DYN_START;
		tries++;
	}
	i = 0;
out:
	return i;
}

/**
 *	llc_ui_autobind - Bind a socket to a specific address.
 *	@sk: Socket to bind an address to.
 *	@addr: Address the user wants the socket bound to.
 *
 *	Bind a socket to a specific address. For llc a user is able to bind to
 *	a specific sap only or mac + sap. If the user only specifies a sap and
 *	a null dmac (all zeros) the user is attempting to bind to an entire
 *	sap. This will stop anyone else on the local system from using that
 *	sap.  If someone else has a mac + sap open the bind to null + sap will
 *	fail.
 *	If the user desires to bind to a specific mac + sap, it is possible to
 *	have multiple sap connections via multiple macs.
 *	Bind and autobind for that matter must enforce the correct sap usage
 *	otherwise all hell will break loose.
 *	Returns: 0 upon success, negative otherwise.
 */
static int llc_ui_autobind(struct socket *sock, struct sockaddr_llc *addr)
{
	struct sock *sk = sock->sk;
	struct llc_ui_opt *llc_ui = llc_ui_sk(sk);
	struct llc_sap *sap;
	struct net_device *dev = NULL;
	int rc = -EINVAL;

	if (!sk->zapped)
		goto out;
	/* bind to a specific mac, optional. */
	if (!llc_ui_mac_null(addr->sllc_smac)) {
		rtnl_lock();
		dev = dev_getbyhwaddr(addr->sllc_arphrd, addr->sllc_smac);
		rtnl_unlock();
		rc = -ENETUNREACH;
		if (!dev)
			goto out;
		llc_ui->dev = dev;
	}
	/* bind to a specific sap, optional. */
	if (!addr->sllc_ssap) {
		rc = -EUSERS;
		addr->sllc_ssap = llc_ui_autoport();
		if (!addr->sllc_ssap)
			goto out;
	}
	sap = llc_ui_find_sap(addr->sllc_ssap);
	if (!sap) {
		sap = llc_sap_open(llc_ui_indicate, llc_ui_confirm,
				   addr->sllc_ssap);
		rc = -EBUSY; /* some other network layer is using the sap */
		if (!sap)
			goto out;
	} else {
		struct llc_addr laddr, daddr;
		struct sock *ask;

		rc = -EUSERS; /* can't get exclusive use of sap */
		if (!dev && llc_ui_mac_null(addr->sllc_mmac))
			goto out;
		memset(&laddr, 0, sizeof(laddr));
		memset(&daddr, 0, sizeof(daddr));
		if (!llc_ui_mac_null(addr->sllc_mmac)) {
			if (sk->type != SOCK_DGRAM) {
				rc = -EOPNOTSUPP;
				goto out;
			}
			memcpy(laddr.mac, addr->sllc_mmac, IFHWADDRLEN);
		} else
			memcpy(laddr.mac, addr->sllc_smac, IFHWADDRLEN);
		laddr.lsap = addr->sllc_ssap;
		rc = -EADDRINUSE; /* mac + sap clash. */
		ask = llc_ui_bh_find_sk_by_addr(&laddr, &daddr, dev);
		if (ask) {
			sock_put(ask);
			goto out;
		}
	}
	memcpy(&llc_ui->addr, addr, sizeof(*addr));
	llc_ui->sap = sap;
	rc = sk->zapped = 0;
	llc_ui_insert_socket(sk);
out:
	return rc;
}

/**
 *	llc_ui_bind - bind a socket to a specific address.
 *	@sock: Socket to bind an address to.
 *	@uaddr: Address the user wants the socket bound to.
 *	@addrlen: Length of the uaddr structure.
 *
 *	Bind a socket to a specific address. For llc a user is able to bind to
 *	a specific sap only or mac + sap. If the user only specifies a sap and
 *	a null dmac (all zeros) the user is attempting to bind to an entire
 *	sap. This will stop anyone else on the local system from using that
 *	sap. If someone else has a mac + sap open the bind to null + sap will
 *	fail.
 *	If the user desires to bind to a specific mac + sap, it is possible to
 *	have multiple sap connections via multiple macs.
 *	Bind and autobind for that matter must enforce the correct sap usage
 *	otherwise all hell will break loose.
 *	Returns: 0 upon success, negative otherwise.
 */
static int llc_ui_bind(struct socket *sock, struct sockaddr *uaddr, int addrlen)
{
	struct sockaddr_llc *addr = (struct sockaddr_llc *)uaddr;
	struct sock *sk = sock->sk;
	int rc = -EINVAL;

	if (!sk->zapped || addrlen != sizeof(*addr))
		goto out;
	rc = -EAFNOSUPPORT;
	if (addr->sllc_family != AF_LLC)
		goto out;
	/* use autobind, to avoid code replication. */
	rc = llc_ui_autobind(sock, addr);
out:
	return rc;
}

/**
 *	llc_ui_shutdown - shutdown a connect llc2 socket.
 *	@sock: Socket to shutdown.
 *	@how: What part of the socket to shutdown.
 *
 *	Shutdown a connected llc2 socket. Currently this function only supports
 *	shutting down both sends and receives (2), we could probably make this
 *	function such that a user can shutdown only half the connection but not
 *	right now.
 *	Returns: 0 upon success, negative otherwise.
 */
static int llc_ui_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	int rc = -ENOTCONN;

	lock_sock(sk);
	if (sk->state != TCP_ESTABLISHED)
		goto out;
	rc = -EINVAL;
	if (how != 2)
		goto out;
	rc = llc_ui_send_disc(sk);
	if (!rc)
		llc_ui_wait_for_disc(sk, 255);
	/* Wake up anyone sleeping in poll */
	sk->state_change(sk);
out:
	release_sock(sk);
	return rc;
}

/**
 *	llc_ui_connect - Connect to a remote llc2 mac + sap.
 *	@sock: Socket which will be connected to the remote destination.
 *	@uaddr: Remote and possibly the local address of the new connection.
 *	@addrlen: Size of uaddr structure.
 *	@flags: Operational flags specified by the user.
 *
 *	Connect to a remote llc2 mac + sap. The caller must specify the
 *	destination mac and address to connect to. If the user previously
 *	called bind(2) with a smac the user does not need to specify the source
 *	address and mac.
 *	This function will autobind if user did not previously call bind.
 *	Returns: 0 upon success, negative otherwise.
 */
static int llc_ui_connect(struct socket *sock, struct sockaddr *uaddr,
			  int addrlen, int flags)
{
	struct sock *sk = sock->sk;
	struct llc_ui_opt *llc_ui = llc_ui_sk(sk);
	struct sockaddr_llc *addr = (struct sockaddr_llc *)uaddr;
	struct net_device *dev;
	int rc = -EINVAL;

	lock_sock(sk);
	if (addrlen != sizeof(*addr))
		goto out;
	rc = -EAFNOSUPPORT;
	if (addr->sllc_family != AF_LLC)
		goto out;
	/* bind connection to sap if user hasn't done it. */
	if (sk->zapped) {
		/* bind to sap with null dev, exclusive */
		rc = llc_ui_autobind(sock, addr);
		if (rc)
			goto out;
	}
	if (!llc_ui->dev) {
		rtnl_lock();
		dev = dev_getbyhwaddr(addr->sllc_arphrd, addr->sllc_smac);
		rtnl_unlock();
		if (!dev)
			goto out;
	} else
		dev = llc_ui->dev;
	if (sk->type != SOCK_STREAM)
		goto out;
	rc = -EALREADY;
	if (sock->state == SS_CONNECTING)
		goto out;
	sock->state = SS_CONNECTING;
	sk->state   = TCP_SYN_SENT;
	llc_ui->link   = llc_ui_next_link_no(llc_ui->sap->laddr.lsap);
	rc = llc_ui_send_conn(sk, llc_ui->sap, addr, dev, llc_ui->link);
	if (rc) {
		sock->state = SS_UNCONNECTED;
		sk->state   = TCP_CLOSE;
		goto out;
	}
	rc = llc_ui_wait_for_conn(sk, 255);
out:
	release_sock(sk);
	return rc;
}

/**
 *	llc_ui_listen - allow a normal socket to accept incoming connections
 *	@sock: Socket to allow incoming connections on.
 *	@backlog: Number of connections to queue.
 *
 *	Allow a normal socket to accept incoming connections.
 *	Returns 0 upon success, negative otherwise.
 */
static int llc_ui_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	int rc = -EINVAL;

	lock_sock(sk);
	if (sock->state != SS_UNCONNECTED)
		goto out;
	rc = -EOPNOTSUPP;
	if (sk->type != SOCK_STREAM)
		goto out;
	rc = -EAGAIN;
	if (sk->zapped)
		goto out;
	rc = 0;
	if (!(unsigned)backlog)	/* BSDism */
		backlog = 1;
	sk->max_ack_backlog = backlog;
	if (sk->state != TCP_LISTEN) {
		sk->ack_backlog = 0;
		sk->state = TCP_LISTEN;
	}
	sk->socket->flags |= __SO_ACCEPTCON;
out:
	release_sock(sk);
	return rc;
}

static int llc_ui_wait_for_disc(struct sock *sk, int seconds)
{
	DECLARE_WAITQUEUE(wait, current);
	int rc, timeout = seconds * HZ;

	add_wait_queue_exclusive(sk->sleep, &wait);
	for (;;) {
		__set_current_state(TASK_INTERRUPTIBLE);
		rc = 0;
		if (sk->state != TCP_CLOSE)
			timeout = schedule_timeout(timeout);
		else
			break;
		rc = -ERESTARTSYS;
		if (signal_pending(current))
			break;
		rc = -EAGAIN;
		if (!timeout)
			break;
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(sk->sleep, &wait);
	return rc;
}

static int llc_ui_wait_for_conn(struct sock *sk, int seconds)
{
	struct llc_ui_opt *llc_ui = llc_ui_sk(sk);
	DECLARE_WAITQUEUE(wait, current);
	int rc, timeout = seconds * HZ;

	add_wait_queue_exclusive(sk->sleep, &wait);
	for (;;) {
		__set_current_state(TASK_INTERRUPTIBLE);
		rc = 0;
		if (sk->state != TCP_ESTABLISHED)
			timeout = schedule_timeout(timeout);
		if (sk->state == TCP_ESTABLISHED) {
			if (!llc_ui->core_sk)
				rc = -EAGAIN;
			break;
		}
		rc = -EAGAIN;
		if (sk->state == TCP_CLOSE)
			break;
		rc = -ERESTARTSYS;
		if (signal_pending(current))
			break;
		rc = -EAGAIN;
		if (!timeout)
			break;
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(sk->sleep, &wait);
	return rc;
}

/**
 *	llc_ui_accept - accept a new incoming connection.
 *	@sock: Socket which connections arrive on.
 *	@newsock: Socket to move incoming connection to.
 *	@flags: User specified operational flags.
 *
 *	Accept a new incoming connection.
 *	Returns 0 upon success, negative otherwise.
 */
static int llc_ui_accept(struct socket *sock, struct socket *newsock, int flags)
{
	struct sock *sk = sock->sk, *newsk;
	struct llc_ui_opt *llc_ui, *newllc_ui;
	struct llc_opt *newllc_core;
	struct sk_buff *skb;
	int rc = -EOPNOTSUPP;

	lock_sock(sk);
	if (sk->type != SOCK_STREAM)
		goto out;
	rc = -EINVAL;
	if (sock->state != SS_UNCONNECTED || sk->state != TCP_LISTEN)
		goto out;
	/* wait for a connection to arrive. */
	do {
		skb = skb_dequeue(&sk->receive_queue);
		if (!skb) {
			rc = -EWOULDBLOCK;
			if (flags & O_NONBLOCK)
				goto out;
			interruptible_sleep_on(sk->sleep);
			rc = -ERESTARTSYS;
			if (signal_pending(current))
				goto out;
		}
	} while (!skb);

	rc = -EINVAL;
	if(!skb->sk)
		goto frees;
	/* attach connection to a new socket. */
	rc = llc_ui_create(newsock, sk->protocol);
	if (rc)
		goto frees;
	rc = 0;
	newsk			= newsock->sk;
	newsk->pair		= NULL;
	newsk->socket		= newsock;
	newsk->sleep		= &newsock->wait;
	newsk->zapped		= 0;
	newsk->state		= TCP_ESTABLISHED;
	newsock->state		= SS_CONNECTED;
	llc_ui			= llc_ui_sk(sk);
	newllc_ui		= llc_ui_sk(newsk);
	newllc_ui->sap		= llc_ui->sap;
	newllc_ui->dev		= llc_ui->dev;
	newllc_ui->core_sk	= skb->sk;
	newllc_core		= llc_sk(newllc_ui->core_sk);
	newllc_ui->link		= newllc_core->link;
	newllc_core->handler	= newsk;
	memcpy(&newllc_ui->addr, &llc_ui->addr, sizeof(newllc_ui->addr));
	memcpy(newllc_ui->addr.sllc_dmac, newllc_core->daddr.mac, IFHWADDRLEN);
	newllc_ui->addr.sllc_dsap	= newllc_core->daddr.lsap;

	/* put original socket back into a clean listen state. */
	sk->state = TCP_LISTEN;
	sk->ack_backlog--;
	llc_ui_insert_socket(newsk);
	skb->sk = NULL;
frees:
	kfree_skb(skb);
out:
	release_sock(sk);
	return rc;
}

/**
 *	llc_ui_recvmsg - copy received data to the socket user.
 *	@sock: Socket to copy data from.
 *	@msg: Various user space related information.
 *	@size: Size of user buffer.
 *	@flags: User specified flags.
 *	@scm: Unknown.
 *
 *	Copy received data to the socket user.
 *	Returns non-negative upon success, negative otherwise.
 */
static int llc_ui_recvmsg(struct socket *sock, struct msghdr *msg, int size,
			  int flags, struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	struct sockaddr_llc *uaddr = (struct sockaddr_llc *)msg->msg_name;
	struct sk_buff *skb;
	int rc = -ENOMEM, copied = 0;
	int noblock = flags & MSG_DONTWAIT;

	lock_sock(sk);
	skb = skb_recv_datagram(sk, flags, noblock, &rc);
	if (!skb)
		goto out;
	copied = skb->len;
	if (copied > size) {
		copied = size;
		msg->msg_flags |= MSG_TRUNC;
	}
	rc = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	if (rc)
		goto dgram_free;
	if (uaddr)
		memcpy(uaddr, llc_ui_skb_cb(skb), sizeof(*uaddr));
	msg->msg_namelen = sizeof(*uaddr);
dgram_free:
	skb_free_datagram(sk, skb); /* Free the datagram. */
out:
	release_sock(sk);
	return rc ? : copied;
}

/**
 *	llc_ui_sendmsg - Transmit data provided by the socket user.
 *	@sock: Socket to transmit data from.
 *	@msg: Various user related information.
 *	@len: Length of data to transmit.
 *	@scm: Unknown.
 *
 *	Transmit data provided by the socket user.
 *	Returns non-negative upon success, negative otherwise.
 */
static int llc_ui_sendmsg(struct socket *sock, struct msghdr *msg, int len,
			  struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	struct llc_ui_opt *llc_ui = llc_ui_sk(sk);
	struct sockaddr_llc *addr = (struct sockaddr_llc *)msg->msg_name;
	int flags = msg->msg_flags;
	struct net_device *dev;
	struct sk_buff *skb;
	int rc = -EOPNOTSUPP, size = 0;

	lock_sock(sk);
	if (flags & ~MSG_DONTWAIT)
		goto release;
	rc = -EINVAL;
	if (addr) {
		if (msg->msg_namelen < sizeof(*addr))
			goto release;
	} else {
		if (llc_ui_addr_null(&llc_ui->addr))
			goto release;
		addr = &llc_ui->addr;
	}
	/* must bind connection to sap if user hasn't done it. */
	if (sk->zapped) {
		/* bind to sap with null dev, exclusive. */
		rc = llc_ui_autobind(sock, addr);
		if (rc)
			goto release;
	}
	if (!llc_ui->dev) {
		rtnl_lock();
		dev = dev_getbyhwaddr(addr->sllc_arphrd, addr->sllc_smac);
		rtnl_unlock();
		rc = -ENETUNREACH;
		if (!dev)
			goto release;
	} else
		dev = llc_ui->dev;
	size = dev->hard_header_len + len + llc_ui_header_len(sk, addr);
	rc = -EMSGSIZE;
	if (size > dev->mtu)
		goto release;
	skb = sock_alloc_send_skb(sk, size, flags & MSG_DONTWAIT, &rc);
	if (!skb)
		goto release;
	skb->sk  = sk;
	skb->dev = dev;
	skb_reserve(skb, dev->hard_header_len + llc_ui_header_len(sk, addr));
	rc = memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len);
	if (rc)
		goto release;
	if (addr->sllc_test) {
		rc = llc_ui_send_llc1(llc_ui->sap, skb, addr, LLC_TEST_PRIM);
		goto out;
	}
	if (addr->sllc_xid) {
		rc = llc_ui_send_llc1(llc_ui->sap, skb, addr, LLC_XID_PRIM);
		goto out;
	}
	if (sk->type == SOCK_DGRAM || addr->sllc_ua) {
		rc = llc_ui_send_llc1(llc_ui->sap, skb, addr, LLC_DATAUNIT_PRIM);
		goto out;
	}
	rc = -ENOPROTOOPT;
	if (!(sk->type == SOCK_STREAM && !addr->sllc_ua))
		goto out;
	rc = -ENOTCONN;
	if (!llc_ui->core_sk)
		goto out;
	rc = llc_ui_send_data(llc_ui->sap, sk, skb, addr);
out:
	if (rc)
		skb_free_datagram(sk, skb);
release:
	release_sock(sk);
	return rc ? : len;
}

/**
 *	llc_ui_getname - return the address info of a socket
 *	@sock: Socket to get address of.
 *	@uaddr: Address structure to return information.
 *	@uaddrlen: Length of address structure.
 *	@peer: Does user want local or remote address information.
 *
 *	Return the address information of a socket.
 */
static int llc_ui_getname(struct socket *sock, struct sockaddr *uaddr,
			  int *uaddrlen, int peer)
{
	struct sockaddr_llc sllc;
	struct sock *sk = sock->sk;
	struct llc_ui_opt *llc_ui = llc_ui_sk(sk);
	int rc = 0;

	lock_sock(sk);
	if (sk->zapped)
		goto out;
	*uaddrlen = sizeof(sllc);
	memset(uaddr, 0, *uaddrlen);
	if (peer) {
		rc = -ENOTCONN;
		if (sk->state != TCP_ESTABLISHED)
			goto out;
		if(llc_ui->dev)
			sllc.sllc_arphrd = llc_ui->dev->type;
		sllc.sllc_dsap = llc_sk(llc_ui->core_sk)->daddr.lsap;
		memcpy(&sllc.sllc_dmac, &llc_sk(llc_ui->core_sk)->daddr.mac,
		       IFHWADDRLEN);
	} else {
		rc = -EINVAL;
		if (!llc_ui->sap)
			goto out;
		sllc.sllc_ssap = llc_ui->sap->laddr.lsap;

		if (llc_ui->dev) {
			sllc.sllc_arphrd = llc_ui->dev->type;
			memcpy(&sllc.sllc_smac, &llc_ui->dev->dev_addr,
			       IFHWADDRLEN);
		}
	}
	rc = 0;
	sllc.sllc_family = AF_LLC;
	memcpy(uaddr, &sllc, sizeof(sllc));
out:
	release_sock(sk);
	return rc;
}

/**
 *	llc_ui_ioctl - io controls for PF_LLC
 *	@sock: Socket to get/set info
 *	@cmd: command
 *	@arg: optional argument for cmd
 *
 *	get/set info on llc sockets
 */
static int llc_ui_ioctl(struct socket *sock, unsigned int cmd,
			unsigned long arg)
{
	return dev_ioctl(cmd, (void *)arg);
}

/**
 *	llc_ui_setsockopt - set various connection specific parameters.
 *	@sock: Socket to set options on.
 *	@level: Socket level user is requesting operations on.
 *	@optname: Operation name.
 *	@optval User provided operation data.
 *	@optlen: Length of optval.
 *
 *	Set various connection specific parameters.
 */
static int llc_ui_setsockopt(struct socket *sock, int level, int optname,
			     char *optval, int optlen)
{
	struct sock *sk = sock->sk;
	struct llc_ui_opt *llc_ui = llc_ui_sk(sk);
	struct llc_opt *llc_core;
	int rc = -EINVAL, opt;

	lock_sock(sk);
	if (level != SOL_LLC || optlen != sizeof(int))
		goto out;
	rc = -ENOTCONN;
	if (!llc_ui->core_sk)
		goto out;
	rc = get_user(opt, (int *)optval);
	if (rc)
		goto out;
	rc = -EINVAL;
	llc_core = llc_sk(llc_ui->core_sk);
	switch (optname) {
		case LLC_OPT_RETRY:
			if (opt > LLC_OPT_MAX_RETRY)
				goto out;
			llc_core->n2 = opt;
			break;
		case LLC_OPT_SIZE:
			if (opt > LLC_OPT_MAX_SIZE)
				goto out;
			llc_core->n1 = opt;
			break;
		case LLC_OPT_ACK_TMR_EXP:
			if (opt > LLC_OPT_MAX_ACK_TMR_EXP)
				goto out;
			llc_core->ack_timer.expire = opt;
			break;
		case LLC_OPT_P_TMR_EXP:
			if (opt > LLC_OPT_MAX_P_TMR_EXP)
				goto out;
			llc_core->pf_cycle_timer.expire = opt;
			break;
		case LLC_OPT_REJ_TMR_EXP:
			if (opt > LLC_OPT_MAX_REJ_TMR_EXP)
				goto out;
			llc_core->rej_sent_timer.expire = opt;
			break;
		case LLC_OPT_BUSY_TMR_EXP:
			if (opt > LLC_OPT_MAX_BUSY_TMR_EXP)
				goto out;
			llc_core->busy_state_timer.expire = opt;
			break;
		case LLC_OPT_TX_WIN:
			if (opt > LLC_OPT_MAX_WIN)
				goto out;
			llc_core->k = opt;
			break;
		case LLC_OPT_RX_WIN:
			if (opt > LLC_OPT_MAX_WIN)
				goto out;
			llc_core->rw = opt;
			break;
		default:
			rc = -ENOPROTOOPT;
			goto out;
	}
	rc = 0;
out:
	release_sock(sk);
	return rc;
}

/**
 *	llc_ui_getsockopt - get connection specific socket info
 *	@sock: Socket to get information from.
 *	@level: Socket level user is requesting operations on.
 *	@optname: Operation name.
 *	@optval: Variable to return operation data in.
 *	@optlen: Length of optval.
 *
 *	Get connection specific socket information.
 */
static int llc_ui_getsockopt(struct socket *sock, int level, int optname,
			     char *optval, int *optlen)
{
	struct sock *sk = sock->sk;
	struct llc_ui_opt *llc_ui = llc_ui_sk(sk);
	struct llc_opt *llc_core;
	int val = 0, len = 0, rc = -EINVAL;

	lock_sock(sk);
	if (level != SOL_LLC)
		goto out;
	rc = -ENOTCONN;
	if (!llc_ui->core_sk)
		goto out;
	rc = get_user(len, optlen);
	if (rc)
		goto out;
	rc = -EINVAL;
	if (len != sizeof(int))
		goto out;
	llc_core = llc_sk(llc_ui->core_sk);
	switch (optname) {
		case LLC_OPT_RETRY:
			val = llc_core->n2;				break;
		case LLC_OPT_SIZE:
			val = llc_core->n1;				break;
		case LLC_OPT_ACK_TMR_EXP:
			val = llc_core->ack_timer.expire;		break;
		case LLC_OPT_P_TMR_EXP:
			val = llc_core->pf_cycle_timer.expire;		break;
		case LLC_OPT_REJ_TMR_EXP:
			val = llc_core->rej_sent_timer.expire;		break;
		case LLC_OPT_BUSY_TMR_EXP:
			val = llc_core->busy_state_timer.expire;	break;
		case LLC_OPT_TX_WIN:
			val = llc_core->k;				break;
		case LLC_OPT_RX_WIN:
			val = llc_core->rw;				break;
		default:
			rc = -ENOPROTOOPT;
			goto out;
	}
	rc = 0;
	if (put_user(len, optlen) || copy_to_user(optval, &val, len))
		rc = -EFAULT;
out:
	release_sock(sk);
	return rc;
}

/**
 *	llc_ui_ind_test - handle TEST indication
 *	@prim: Primitive block provided by the llc layer.
 *
 *	handle TEST indication.
 */
static void llc_ui_ind_test(struct llc_prim_if_block *prim)
{
	struct llc_prim_test *prim_data = &prim->data->test;
	struct sk_buff *skb = prim_data->skb;
	struct sockaddr_llc *llc_ui = llc_ui_skb_cb(skb);
	struct sock *sk = llc_ui_find_sk_by_addr(&prim_data->daddr,
						 &prim_data->saddr, skb->dev);
	if (!sk)
		goto out;
	if (sk->state == TCP_LISTEN)
		goto out_put;
	/* save primitive for use by the user. */
	llc_ui->sllc_family = AF_LLC;
	llc_ui->sllc_arphrd = skb->dev->type;
	llc_ui->sllc_test   = 1;
	llc_ui->sllc_xid    = 0;
	llc_ui->sllc_ua     = 0;
	llc_ui->sllc_dsap = prim_data->daddr.lsap;
	memcpy(llc_ui->sllc_dmac, prim_data->daddr.mac, IFHWADDRLEN);
	llc_ui->sllc_ssap = prim_data->saddr.lsap;
	memcpy(llc_ui->sllc_smac, prim_data->saddr.mac, IFHWADDRLEN);
	/* queue skb to the user. */
	if (sock_queue_rcv_skb(sk, skb))
		kfree_skb(skb);
out_put:
	sock_put(sk);
out:;
}

/**
 *	llc_ui_ind_xid - handle XID indication
 *	@prim: Primitive block provided by the llc layer.
 *
 *	handle XID indication.
 */
static void llc_ui_ind_xid(struct llc_prim_if_block *prim)
{
	struct llc_prim_xid *prim_data = &prim->data->xid;
	struct sk_buff *skb = prim_data->skb;
	struct sockaddr_llc *llc_ui = llc_ui_skb_cb(skb);
	struct sock *sk = llc_ui_find_sk_by_addr(&prim_data->daddr,
						 &prim_data->saddr, skb->dev);
	if (!sk)
		goto out;
	if (sk->state == TCP_LISTEN)
		goto out_put;
	/* save primitive for use by the user. */
	llc_ui->sllc_family = AF_LLC;
	llc_ui->sllc_arphrd = 0;
	llc_ui->sllc_test   = 0;
	llc_ui->sllc_xid    = 1;
	llc_ui->sllc_ua     = 0;
	llc_ui->sllc_dsap = prim_data->daddr.lsap;
	memcpy(llc_ui->sllc_dmac, prim_data->daddr.mac, IFHWADDRLEN);
	llc_ui->sllc_ssap = prim_data->saddr.lsap;
	memcpy(llc_ui->sllc_smac, prim_data->saddr.mac, IFHWADDRLEN);
	/* queue skb to the user. */
	if (sock_queue_rcv_skb(sk, skb))
		kfree_skb(skb);
out_put:
	sock_put(sk);
out:;
}

/**
 *	llc_ui_ind_dataunit - handle DATAUNIT indication
 *	@prim: Primitive block provided by the llc layer.
 *
 *	handle DATAUNIT indication.
 */
static void llc_ui_ind_dataunit(struct llc_prim_if_block *prim)
{
	struct llc_prim_unit_data *prim_data = &prim->data->udata;
	struct sk_buff *skb = prim_data->skb;
	struct sockaddr_llc *llc_ui = llc_ui_skb_cb(skb);
	struct sock *sk = llc_ui_find_sk_by_addr(&prim_data->daddr,
						 &prim_data->saddr, skb->dev);
	if (!sk)
		goto out;
	if (sk->state == TCP_LISTEN)
		goto out_put;
	/* save primitive for use by the user. */
	llc_ui->sllc_family = AF_LLC;
	llc_ui->sllc_arphrd = skb->dev->type;
	llc_ui->sllc_test   = 0;
	llc_ui->sllc_xid    = 0;
	llc_ui->sllc_ua     = 1;
	llc_ui->sllc_dsap = prim_data->daddr.lsap;
	memcpy(llc_ui->sllc_dmac, prim_data->daddr.mac, IFHWADDRLEN);
	llc_ui->sllc_ssap = prim_data->saddr.lsap;
	memcpy(llc_ui->sllc_smac, prim_data->saddr.mac, IFHWADDRLEN);
	/* queue skb to the user. */
	if (sock_queue_rcv_skb(sk, skb))
		kfree_skb(skb);
out_put:
	sock_put(sk);
out:;
}

/**
 *	llc_ui_ind_conn - handle CONNECT indication
 *	@prim: Primitive block provided by the llc layer.
 *
 *	handle CONNECT indication.
 */
static void llc_ui_ind_conn(struct llc_prim_if_block *prim)
{
	struct llc_prim_conn *prim_data = &prim->data->conn;
	struct sock* sk;
	struct sk_buff *skb2;

	llc_sk(prim_data->sk)->laddr.lsap = prim->sap->laddr.lsap;
	sk = llc_ui_find_sk_by_addr(&llc_sk(prim_data->sk)->laddr,
				    &prim_data->saddr, prim_data->dev);
	if (!sk)
		goto out;
	if (sk->type != SOCK_STREAM || sk->state != TCP_LISTEN)
		goto out_put;
	if (prim->data->conn.status)
		goto out_put; /* bad status. */
	/* give this connection a link number. */
	llc_sk(prim_data->sk)->link =
			llc_ui_next_link_no(llc_sk(prim_data->sk)->laddr.lsap);
	skb2 = alloc_skb(0, GFP_ATOMIC);
	if (!skb2)
		goto out_put;
	skb2->sk = prim_data->sk;
	skb_queue_tail(&sk->receive_queue, skb2);
	sk->state_change(sk);
out_put:
	sock_put(sk);
out:;
}

/**
 *	llc_ui_ind_data - handle DATA indication
 *	@prim: Primitive block provided by the llc layer.
 *
 *	handle CONNECT indication.
 */
static void llc_ui_ind_data(struct llc_prim_if_block *prim)
{
	struct llc_prim_data *prim_data = &prim->data->data;
	struct sk_buff *skb = prim_data->skb;
	struct sockaddr_llc *llc_ui = llc_ui_skb_cb(skb);
	struct sock* sk = llc_sk(prim_data->sk)->handler;

	if (!sk)
		goto out;
	sock_hold(sk);
	if (sk->type != SOCK_STREAM || sk->state != TCP_ESTABLISHED)
		goto out_put;
	/* save primitive for use by the user. */
	llc_ui->sllc_family = AF_LLC;
	llc_ui->sllc_arphrd = skb->dev->type;
	llc_ui->sllc_test   = 0;
	llc_ui->sllc_xid    = 0;
	llc_ui->sllc_ua     = 0;
	llc_ui->sllc_dsap   = llc_ui_sk(sk)->sap->laddr.lsap;
	memcpy(llc_ui->sllc_dmac, llc_sk(prim_data->sk)->laddr.mac,
	       IFHWADDRLEN);
	llc_ui->sllc_ssap = llc_sk(prim_data->sk)->daddr.lsap;
	memcpy(llc_ui->sllc_smac, llc_sk(prim_data->sk)->daddr.mac,
	       IFHWADDRLEN);
	/* queue skb to the user. */
	if (sock_queue_rcv_skb(sk, skb))
		kfree_skb(skb);
out_put:
	sock_put(sk);
out:;
}

/**
 *	llc_ui_ind_disc - handle DISC indication
 *	@prim: Primitive block provided by the llc layer.
 *
 *	handle DISC indication.
 */
static void llc_ui_ind_disc(struct llc_prim_if_block *prim)
{
	struct llc_prim_disc *prim_data = &prim->data->disc;
	struct sock* sk = llc_sk(prim_data->sk)->handler;

	if (!sk)
		goto out;
	sock_hold(sk);
	if (sk->type != SOCK_STREAM || sk->state != TCP_ESTABLISHED)
		goto out_put;
	llc_ui_sk(sk)->core_sk = NULL;
	sk->shutdown	   = SHUTDOWN_MASK;
	sk->socket->state  = SS_UNCONNECTED;
	sk->state	   = TCP_CLOSE;
	if (!sk->dead) {
		sk->state_change(sk);
		sk->dead = 1;
	}
out_put:
	sock_put(sk);
out:;
}

/**
 *	llc_ui_indicate - LLC user interface hook into the LLC layer.
 *	@prim: Primitive block provided by the llc layer.
 *
 *	LLC user interface hook into the LLC layer, every llc_ui sap references
 *	this function as its indicate handler.
 *	Always returns 0 to indicate reception of primitive.
 */
static int llc_ui_indicate(struct llc_prim_if_block *prim)
{
	switch (prim->prim) {
		case LLC_TEST_PRIM:
			llc_ui_ind_test(prim);		break;
		case LLC_XID_PRIM:
			llc_ui_ind_xid(prim);		break;
		case LLC_DATAUNIT_PRIM:
			llc_ui_ind_dataunit(prim);	break;
		case LLC_CONN_PRIM:
			llc_ui_ind_conn(prim);		break;
		case LLC_DATA_PRIM:
			llc_ui_ind_data(prim);		break;
		case LLC_DISC_PRIM:
			llc_ui_ind_disc(prim);		break;
		case LLC_RESET_PRIM:
		case LLC_FLOWCONTROL_PRIM:
		default:				break;
	}
	return 0;
}

/**
 *	llc_ui_conf_conn - handle CONN confirm.
 *	@prim: Primitive block provided by the llc layer.
 *
 *	handle CONN confirm.
 */
static void llc_ui_conf_conn(struct llc_prim_if_block *prim)
{
	struct llc_prim_conn *prim_data = &prim->data->conn;
	struct llc_opt *llc_core = llc_sk(prim_data->sk);
	struct sock* sk = llc_core->handler;
	struct llc_ui_opt *llc_ui = llc_ui_sk(sk);

	if (!sk)
		goto out;
	sock_hold(sk);
	if (sk->type != SOCK_STREAM || sk->state != TCP_SYN_SENT)
		goto out_put;
	if (!prim->data->conn.status) {
		sk->socket->state = SS_CONNECTED;
		sk->state	  = TCP_ESTABLISHED;
		llc_ui->core_sk   = prim_data->sk;
	} else {
		sk->socket->state = SS_UNCONNECTED;
		sk->state	  = TCP_CLOSE;
		llc_ui->core_sk	  = NULL;
	}
	sk->state_change(sk);
out_put:
	sock_put(sk);
out:;
}

/**
 *	llc_ui_conf_data - handle DATA confirm.
 *	@prim: Primitive block provided by the llc layer.
 *
 *	handle DATA confirm.
 */
static void llc_ui_conf_data(struct llc_prim_if_block *prim)
{
	struct llc_prim_data *prim_data = &prim->data->data;
	struct sock* sk = llc_sk(prim_data->sk)->handler;

	if (sk)
		wake_up(sk->sleep);
}

/**
 *	llc_ui_conf_disc - handle DISC confirm.
 *	@prim: Primitive block provided by the llc layer.
 *
 *	handle DISC confirm.
 */
static void llc_ui_conf_disc(struct llc_prim_if_block *prim)
{
	struct llc_prim_disc *prim_data = &prim->data->disc;
	struct sock* sk = llc_sk(prim_data->sk)->handler;

	if (!sk)
		goto out;
	sock_hold(sk);
	if (sk->type != SOCK_STREAM || sk->state != TCP_CLOSING)
		goto out_put;
	llc_ui_sk(sk)->core_sk = NULL;
	sk->socket->state      = SS_UNCONNECTED;
	sk->state	       = TCP_CLOSE;
	sk->state_change(sk);
out_put:
	sock_put(sk);
out:;
}

/**
 *	llc_ui_confirm - LLC user interface hook into the LLC layer
 *	@prim: Primitive block provided by the llc layer.
 *
 *	LLC user interface hook into the LLC layer, every llc_ui sap references
 *	this function as its confirm handler.
 *	Always returns 0 to indicate reception of primitive.
 */
static int llc_ui_confirm(struct llc_prim_if_block *prim)
{
	switch (prim->prim) {
		case LLC_CONN_PRIM:
			llc_ui_conf_conn(prim);		break;
		case LLC_DATA_PRIM:
			llc_ui_conf_data(prim);		break;
		case LLC_DISC_PRIM:
			llc_ui_conf_disc(prim);		break;
		case LLC_RESET_PRIM:			break;
		default:
			printk(KERN_ERR "%s: unknown prim %d\n", __FUNCTION__,
			       prim->prim);
			break;
	}
	return 0;
}

#ifdef CONFIG_PROC_FS
#define MAC_FORMATTED_SIZE 17
static void llc_ui_format_mac(char *bf, unsigned char *mac)
{
	sprintf(bf, "%02X:%02X:%02X:%02X:%02X:%02X",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/**
 *	llc_ui_get_info - return info to procfs
 *	@buffer: where to put the formatted output
 *	@start: starting from
 *	@offset: offset into buffer.
 *	@length: size of the buffer
 *
 *	Get the output of the local llc ui socket list to the caller.
 *	Returns the length of data wrote to buffer.
 */
static int llc_ui_get_info(char *buffer, char **start, off_t offset, int length)
{
	off_t pos = 0;
	off_t begin = 0;
	struct sock *s;
	int len = sprintf(buffer, "SocketID SKt Mc local_mac_sap\t  "
				  "remote_mac_sap\t tx_queue rx_queue st uid "
				  "link_no\n");
	/* Output the LLC socket data for the /proc filesystem */
	read_lock_bh(&llc_ui_sockets_lock);
	for (s = llc_ui_sockets; s; s = s->next) {
		struct llc_ui_opt *llc_ui = llc_ui_sk(s);
		len += sprintf(buffer + len, "%p %02X  %02X ", s, s->type,
			       !llc_ui_mac_null(llc_ui->addr.sllc_mmac));
		if (llc_ui->sap) {
			if (llc_ui->dev &&
			    llc_ui_mac_null(llc_ui->addr.sllc_mmac))
				llc_ui_format_mac(buffer + len,
						  llc_ui->dev->dev_addr);
			else {
				if (!llc_ui_mac_null(llc_ui->addr.sllc_mmac))
					llc_ui_format_mac(buffer + len,
							llc_ui->addr.sllc_mmac);
				else
					sprintf(buffer + len,
						"00:00:00:00:00:00");
			}
			len += MAC_FORMATTED_SIZE;
			len += sprintf(buffer + len, "@%02X ",
					llc_ui->sap->laddr.lsap);
		} else
			len += sprintf(buffer + len, "00:00:00:00:00:00@00 ");
		llc_ui_format_mac(buffer + len, llc_ui->addr.sllc_dmac);
		len += MAC_FORMATTED_SIZE;
		len += sprintf(buffer + len,
				"@%02X %08d:%08d %02d %-3d ",
				llc_ui->addr.sllc_dsap,
				atomic_read(&s->wmem_alloc),
				atomic_read(&s->rmem_alloc), s->state,
				SOCK_INODE(s->socket)->i_uid);
		if (llc_ui->core_sk)
			len += sprintf(buffer + len, "%-7d\n",
					llc_sk(llc_ui->core_sk)->link);
		else
			len += sprintf(buffer + len, "no_link\n");
		/* Are we still dumping unwanted data then discard the record */
		pos = begin + len;

		if (pos < offset) {
			len = 0; /* Keep dumping into the buffer start */
			begin = pos;
		}
		if (pos > offset + length) /* We have dumped enough */
			break;
	}
	read_unlock_bh(&llc_ui_sockets_lock);

	/* The data in question runs from begin to begin + len */
	*start = buffer + offset - begin; /* Start of wanted data */
	len -= offset - begin; /* Remove unwanted header data from length */
	if (len > length)
		len = length; /* Remove unwanted tail data from length */
	return len;
}
#endif /* CONFIG_PROC_FS */

static struct net_proto_family llc_ui_family_ops = {
	.family = PF_LLC,
	.create = llc_ui_create,
};

static struct proto_ops SOCKOPS_WRAPPED(llc_ui_ops) = {
	.family	     = PF_LLC,
	.release     = llc_ui_release,
	.bind	     = llc_ui_bind,
	.connect     = llc_ui_connect,
	.socketpair  = sock_no_socketpair,
	.accept      = llc_ui_accept,
	.getname     = llc_ui_getname,
	.poll	     = datagram_poll,
	.ioctl       = llc_ui_ioctl,
	.listen      = llc_ui_listen,
	.shutdown    = llc_ui_shutdown,
	.setsockopt  = llc_ui_setsockopt,
	.getsockopt  = llc_ui_getsockopt,
	.sendmsg     = llc_ui_sendmsg,
	.recvmsg     = llc_ui_recvmsg,
	.mmap	     = sock_no_mmap,
	.sendpage    = sock_no_sendpage,
};

#include <linux/smp_lock.h>
SOCKOPS_WRAP(llc_ui, PF_LLC);

static char llc_ui_banner[] __initdata =
	KERN_INFO "NET4.0 IEEE 802.2 User Interface SAPs, Jay Schulist, 2001\n";

int __init llc_ui_init(void)
{
	llc_ui_sap_last_autoport = LLC_SAP_DYN_START;
	sock_register(&llc_ui_family_ops);
	proc_net_create("llc", 0, llc_ui_get_info);
	printk(llc_ui_banner);
	return 0;
}

void __exit llc_ui_exit(void)
{
	proc_net_remove("llc");
	sock_unregister(PF_LLC);
}
