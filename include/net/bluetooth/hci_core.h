/* 
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES 
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN 
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF 
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS, 
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS 
   SOFTWARE IS DISCLAIMED.
*/

/* 
 * $Id: hci_core.h,v 1.1 2001/06/01 08:12:11 davem Exp $ 
 */

#ifndef __IF_HCI_CORE_H
#define __IF_HCI_CORE_H

#include "hci.h"

/* HCI upper protocols */
#define HCI_MAX_PROTO 	1
#define HCI_PROTO_L2CAP	0

#define HCI_INIT_TIMEOUT (HZ * 10)

/* ----- Inquiry cache ----- */
#define INQUIRY_CACHE_AGE_MAX   (HZ*5)    // 5 seconds
#define INQUIRY_ENTRY_AGE_MAX   (HZ*60)   // 60 seconds

struct inquiry_entry {
	struct inquiry_entry 	*next;
	__u32			timestamp;
	inquiry_info		info;
};

struct inquiry_cache {
	spinlock_t 		lock;
	__u32			timestamp;
	struct inquiry_entry 	*list;
};

static __inline__ void inquiry_cache_init(struct inquiry_cache *cache)
{
	spin_lock_init(&cache->lock);
	cache->list = NULL;
}

static __inline__ void inquiry_cache_lock(struct inquiry_cache *cache)
{
	spin_lock(&cache->lock);
}

static __inline__ void inquiry_cache_unlock(struct inquiry_cache *cache)
{
	spin_unlock(&cache->lock);
}

static __inline__ void inquiry_cache_lock_bh(struct inquiry_cache *cache)
{
	spin_lock_bh(&cache->lock);
}

static __inline__ void inquiry_cache_unlock_bh(struct inquiry_cache *cache)
{
	spin_unlock_bh(&cache->lock);
}

static __inline__ long inquiry_cache_age(struct inquiry_cache *cache)
{
	return jiffies - cache->timestamp;
}

static __inline__ long inquiry_entry_age(struct inquiry_entry *e)
{
	return jiffies - e->timestamp;
}
extern void inquiry_cache_flush(struct inquiry_cache *cache);

/* ----- Connection hash ----- */
#define HCI_MAX_CONN 	10

/* FIXME:
 * We assume that handle is a number - 0 ... HCI_MAX_CONN.
 */
struct conn_hash {
	spinlock_t 	lock;
	unsigned int	num;
	void 		*conn[HCI_MAX_CONN];
};

static __inline__ void conn_hash_init(struct conn_hash *h)
{
	memset(h, 0, sizeof(struct conn_hash));
	spin_lock_init(&h->lock);
}

static __inline__ void conn_hash_lock(struct conn_hash *h)
{
	spin_lock(&h->lock);
}

static __inline__ void conn_hash_unlock(struct conn_hash *h)
{
	spin_unlock(&h->lock);
}

static __inline__ void *__conn_hash_add(struct conn_hash *h, __u16 handle, void *conn)
{
	if (!h->conn[handle]) {
		h->conn[handle] = conn;
		h->num++;
		return conn;
	} else
		return NULL;
}

static __inline__ void *conn_hash_add(struct conn_hash *h, __u16 handle, void *conn)
{
	if (handle >= HCI_MAX_CONN)
		return NULL;

	conn_hash_lock(h);
	conn = __conn_hash_add(h, handle, conn);
	conn_hash_unlock(h);

	return conn;
}

static __inline__ void *__conn_hash_del(struct conn_hash *h, __u16 handle)
{
	void *conn = h->conn[handle];

	if (conn) {
		h->conn[handle] = NULL;
		h->num--;
		return conn;
	} else
		return NULL;
}

static __inline__ void *conn_hash_del(struct conn_hash *h, __u16 handle)
{
	void *conn;

	if (handle >= HCI_MAX_CONN)
		return NULL;
	conn_hash_lock(h);
	conn = __conn_hash_del(h, handle); 
	conn_hash_unlock(h);

	return conn;
}

static __inline__ void *__conn_hash_lookup(struct conn_hash *h, __u16 handle)
{
	return h->conn[handle];
}

static __inline__ void *conn_hash_lookup(struct conn_hash *h, __u16 handle)
{
	void *conn;

	if (handle >= HCI_MAX_CONN)
		return NULL;

	conn_hash_lock(h);
	conn = __conn_hash_lookup(h, handle);
	conn_hash_unlock(h);

	return conn;
}

struct hci_dev;

/* ----- HCI Connections ----- */
struct hci_conn {
	bdaddr_t	dst;
	__u16		handle;

	unsigned int 	acl_sent;
	unsigned int 	sco_sent;

	struct hci_dev 	*hdev;
	void		*l2cap_data;
	void		*priv;

	struct sk_buff_head	acl_q;
	struct sk_buff_head	sco_q;
};

/* ----- HCI Devices ----- */
struct hci_dev {
	atomic_t 	refcnt;

	char		name[8];
	__u32	 	flags;
	__u16		id;
	__u8	 	type;
	bdaddr_t	bdaddr;

	atomic_t 	cmd_cnt;
	unsigned int 	acl_cnt;
	unsigned int 	sco_cnt;

	unsigned int	acl_mtu;
	unsigned int 	sco_mtu;
	unsigned int	acl_max;
	unsigned int	sco_max;

	void		*driver_data;
	void		*l2cap_data;
	void		*priv;

	struct tasklet_struct 	cmd_task;
	struct tasklet_struct	rx_task;
	struct tasklet_struct 	tx_task;

	struct sk_buff_head	rx_q;
	struct sk_buff_head 	raw_q;
	struct sk_buff_head 	cmd_q;
	struct sk_buff     	*cmd_sent;

	struct semaphore	req_lock;
	wait_queue_head_t	req_wait_q;
	__u32			req_status;
	__u32			req_result;

	struct inquiry_cache 	inq_cache;

	struct conn_hash 	conn_hash;

	struct hci_dev_stats 	stat;

	int (*open)(struct hci_dev *hdev);
	int (*close)(struct hci_dev *hdev);
	int (*flush)(struct hci_dev *hdev);
	int (*send)(struct sk_buff *skb);
};

static __inline__ void hci_dev_hold(struct hci_dev *hdev)
{
	atomic_inc(&hdev->refcnt);
}

static __inline__ void hci_dev_put(struct hci_dev *hdev)
{
	atomic_dec(&hdev->refcnt);
}

extern struct hci_dev *hci_dev_get(int index);

#define SENT_CMD_PARAM(X)	(((X->cmd_sent->data) + HCI_COMMAND_HDR_SIZE))

extern int hci_register_dev(struct hci_dev *hdev);
extern int hci_unregister_dev(struct hci_dev *hdev);
extern int hci_dev_open(__u16 dev);
extern int hci_dev_close(__u16 dev);
extern int hci_dev_reset(__u16 dev);
extern int hci_dev_reset_stat(__u16 dev);
extern int hci_dev_info(unsigned long arg);
extern int hci_dev_list(unsigned long arg);
extern int hci_dev_setscan(unsigned long arg);
extern int hci_dev_setauth(unsigned long arg);
extern int hci_inquiry(unsigned long arg);

extern __u32 hci_dev_setmode(struct hci_dev *hdev, __u32 mode);
extern __u32 hci_dev_getmode(struct hci_dev *hdev);

extern int hci_recv_frame(struct sk_buff *skb);

/* ----- HCI tasks ----- */
static __inline__ void hci_sched_cmd(struct hci_dev *hdev)
{
	tasklet_schedule(&hdev->cmd_task);
}

static __inline__ void hci_sched_rx(struct hci_dev *hdev)
{
	tasklet_schedule(&hdev->rx_task);
}

static __inline__ void hci_sched_tx(struct hci_dev *hdev)
{
	tasklet_schedule(&hdev->tx_task);
}

/* ----- HCI protocols ----- */
struct hci_proto {
	char 		*name;
	__u32		id;
	__u32		flags;

	void		*priv;

	int (*connect_ind) 	(struct hci_dev *hdev, bdaddr_t *bdaddr);
	int (*connect_cfm)	(struct hci_dev *hdev, bdaddr_t *bdaddr, __u8 status, struct hci_conn *conn);
	int (*disconn_ind)	(struct hci_conn *conn, __u8 reason);
	int (*recv_acldata)	(struct hci_conn *conn, struct sk_buff *skb , __u16 flags);
	int (*recv_scodata)	(struct hci_conn *conn, struct sk_buff *skb);
};

extern int hci_register_proto(struct hci_proto *hproto);
extern int hci_unregister_proto(struct hci_proto *hproto);
extern int hci_register_notifier(struct notifier_block *nb);
extern int hci_unregister_notifier(struct notifier_block *nb);
extern int hci_connect(struct hci_dev * hdev, bdaddr_t * bdaddr);
extern int hci_disconnect(struct hci_conn *conn, __u8 reason);
extern int hci_send_cmd(struct hci_dev *hdev, __u16 ogf, __u16 ocf, __u32 plen, void * param);
extern int hci_send_raw(struct sk_buff *skb);
extern int hci_send_acl(struct hci_conn *conn, struct sk_buff *skb, __u16 flags);
extern int hci_send_sco(struct hci_conn *conn, struct sk_buff *skb);

/* ----- HCI Sockets ----- */
extern void hci_send_to_sock(struct hci_dev *hdev, struct sk_buff *skb);

/* HCI info for socket */
#define hci_pi(sk)	((struct hci_pinfo *) &sk->protinfo)
struct hci_pinfo {
	struct hci_dev 	*hdev;
	__u32 	   	cmsg_flags;
	__u32 	   	mask;
};

/* ----- HCI requests ----- */
#define HCI_REQ_DONE	  0
#define HCI_REQ_PEND	  1
#define HCI_REQ_CANCELED  2

#endif /* __IF_HCI_CORE_H */
