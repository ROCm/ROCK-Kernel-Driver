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
 * BlueZ HCI Core.
 *
 * $Id: hci_core.c,v 1.2 2001/06/01 16:57:03 davem Exp $
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <net/sock.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/bluez.h>
#include <net/bluetooth/hci_core.h>

#ifndef HCI_CORE_DEBUG
#undef  DBG
#define DBG( A... )
#endif

static void hci_cmd_task(unsigned long arg);
static void hci_rx_task(unsigned long arg);
static void hci_tx_task(unsigned long arg);
static void hci_notify(struct hci_dev *hdev, int event);

static rwlock_t hci_task_lock = RW_LOCK_UNLOCKED;

/* HCI device list */
struct hci_dev *hdev_list[HCI_MAX_DEV];
spinlock_t hdev_list_lock;
#define GET_HDEV(a) (hdev_list[a])

/* HCI protocol list */
struct hci_proto *hproto_list[HCI_MAX_PROTO];
#define GET_HPROTO(a) (hproto_list[a])

/* HCI notifiers list */
struct notifier_block *hci_dev_notifier;

/* HCI device notifications */
int hci_register_notifier(struct notifier_block *nb)
{
	int err, i;
	struct hci_dev *hdev;

	if ((err = notifier_chain_register(&hci_dev_notifier, nb)))
		return err;

	/* Notify about already registered devices */
	spin_lock(&hdev_list_lock);
	for (i = 0; i < HCI_MAX_DEV; i++) {
		if (!(hdev = GET_HDEV(i)))
			continue;
		if (hdev->flags & HCI_UP)
			(*nb->notifier_call)(nb, HCI_DEV_UP, hdev);
	}
	spin_unlock(&hdev_list_lock);

	return 0;
}

int hci_unregister_notifier(struct notifier_block *nb)
{
	return notifier_chain_unregister(&hci_dev_notifier, nb);
}

static __inline__ void hci_notify(struct hci_dev *hdev, int event)
{
	notifier_call_chain(&hci_dev_notifier, event, hdev);
}

/* Get HCI device by index (device is locked on return)*/
struct hci_dev *hci_dev_get(int index)
{
	struct hci_dev *hdev;
	DBG("%d", index);

	if (index < 0 || index >= HCI_MAX_DEV)
		return NULL;

	spin_lock(&hdev_list_lock);
	if ((hdev = GET_HDEV(index)))
		hci_dev_hold(hdev);
	spin_unlock(&hdev_list_lock);

	return hdev;
}

/* Flush inquiry cache */
void inquiry_cache_flush(struct inquiry_cache *cache)
{
	struct inquiry_entry *next = cache->list, *e;

	DBG("cache %p", cache);

	cache->list = NULL;
	while ((e = next)) {
		next = e->next;
		kfree(e);
	}
}

/* Lookup by bdaddr.
 * Cache must be locked. */
static struct inquiry_entry * __inquiry_cache_lookup(struct inquiry_cache *cache, bdaddr_t *bdaddr)
{
	struct inquiry_entry *e;

	DBG("cache %p, %s", cache, batostr(bdaddr));

	for (e = cache->list; e; e = e->next)
		if (!bacmp(&e->info.bdaddr, bdaddr))
			break;

	return e;
}

static void inquiry_cache_update(struct inquiry_cache *cache, inquiry_info *info)
{
	struct inquiry_entry *e;

	DBG("cache %p, %s", cache, batostr(&info->bdaddr));

	inquiry_cache_lock(cache);

	if (!(e = __inquiry_cache_lookup(cache, &info->bdaddr))) {
		/* Entry not in the cache. Add new one. */
		if (!(e = kmalloc(sizeof(struct inquiry_entry), GFP_ATOMIC)))
			goto unlock;
		memset(e, 0, sizeof(struct inquiry_entry));
		e->next     = cache->list;
		cache->list = e;
	}

	memcpy(&e->info, info, sizeof(inquiry_info));
	e->timestamp = jiffies;
	cache->timestamp = jiffies;
unlock:
	inquiry_cache_unlock(cache);
}

static int inquiry_cache_dump(struct inquiry_cache *cache, int num, __u8 *buf)
{
	inquiry_info *info = (inquiry_info *) buf;
	struct inquiry_entry *e;
	int copied = 0;

	inquiry_cache_lock(cache);

	for (e = cache->list; e && copied < num; e = e->next, copied++)
		memcpy(info++, &e->info, sizeof(inquiry_info));

	inquiry_cache_unlock(cache);

	DBG("cache %p, copied %d", cache, copied);
	return copied;
}

/* --------- BaseBand connections --------- */
static struct hci_conn *hci_conn_add(struct hci_dev *hdev, __u16 handle, bdaddr_t *dst)
{
	struct hci_conn *conn;

	DBG("%s handle %d dst %s", hdev->name, handle, batostr(dst));

	if (handle > HCI_MAX_CONN) {
		ERR("%s BUG handle %d is to large", hdev->name, handle);
		return NULL;
	}

	if (!(conn = kmalloc(sizeof(struct hci_conn), GFP_ATOMIC)))
		return NULL;
	memset(conn, 0, sizeof(struct hci_conn));

	bacpy(&conn->dst, dst);
	conn->handle = handle;
	conn->hdev   = hdev;

	skb_queue_head_init(&conn->acl_q);
	skb_queue_head_init(&conn->sco_q);

	if (conn_hash_add(&hdev->conn_hash, handle, conn)) {
		hci_dev_hold(hdev);
		return conn;
	} else {
		kfree(conn);
		return NULL;
	}
}

static int hci_conn_del(struct hci_dev *hdev, struct hci_conn *conn)
{
	DBG("%s conn %p handle %d", hdev->name, conn, conn->handle);

	if (conn_hash_del(&hdev->conn_hash, conn->handle)) {
		hci_dev_put(hdev);

		/* Unacked frames */
		hdev->acl_cnt += conn->acl_sent;
	}

	bluez_skb_queue_purge(&conn->acl_q);
	bluez_skb_queue_purge(&conn->sco_q);

	kfree(conn);
	return 0;
}

/* Drop all connection on the device */
static void hci_conn_hash_flush(struct hci_dev *hdev)
{
	struct hci_proto *hp = GET_HPROTO(HCI_PROTO_L2CAP);
	struct conn_hash *h = &hdev->conn_hash;
	int i;

	DBG("hdev %s", hdev->name);

	for (i = 0; i < HCI_MAX_CONN; i++) {
		struct hci_conn *conn;

		if (!(conn = conn_hash_lookup(h, i)))
			continue;

		if (hp && hp->disconn_ind)
			hp->disconn_ind(conn, 0x16);
		
		hci_conn_del(hdev, conn);
	}
}

int hci_connect(struct hci_dev *hdev, bdaddr_t *bdaddr)
{
	struct inquiry_cache *cache = &hdev->inq_cache;
	struct inquiry_entry *e;
	create_conn_cp cc;
	__u16 clock_offset;

	DBG("%s bdaddr %s", hdev->name, batostr(bdaddr));

	if (!(hdev->flags & HCI_UP))
		return -ENODEV;

	inquiry_cache_lock_bh(cache);

	if (!(e = __inquiry_cache_lookup(cache, bdaddr)) || inquiry_entry_age(e) > INQUIRY_ENTRY_AGE_MAX) {
		cc.pscan_rep_mode = 0;
		cc.pscan_mode     = 0;
		clock_offset      = 0;
	} else {
		cc.pscan_rep_mode = e->info.pscan_rep_mode;
		cc.pscan_mode     = e->info.pscan_mode;
		clock_offset      = __le16_to_cpu(e->info.clock_offset) & 0x8000;
	}

	inquiry_cache_unlock_bh(cache);

	bacpy(&cc.bdaddr, bdaddr);
	cc.pkt_type	= __cpu_to_le16(HCI_DM1 | HCI_DM3 | HCI_DM5 | HCI_DH1 | HCI_DH3 | HCI_DH5);
	cc.clock_offset	= __cpu_to_le16(clock_offset);
	cc.role_switch	= 0;
	hci_send_cmd(hdev, OGF_LINK_CTL, OCF_CREATE_CONN, CREATE_CONN_CP_SIZE, &cc);

	return 0;
}

int hci_disconnect(struct hci_conn *conn, __u8 reason)
{
	disconnect_cp dc;

	DBG("conn %p handle %d", conn, conn->handle);

	dc.handle = __cpu_to_le16(conn->handle);
	dc.reason = reason;
	hci_send_cmd(conn->hdev, OGF_LINK_CTL, OCF_DISCONNECT, DISCONNECT_CP_SIZE, &dc);

	return 0;
}

/* --------- HCI request handling ------------ */
static __inline__ void hci_req_lock(struct hci_dev *hdev)
{
	down(&hdev->req_lock);
}

static __inline__ void hci_req_unlock(struct hci_dev *hdev)
{
	up(&hdev->req_lock);
}

static __inline__ void hci_req_complete(struct hci_dev *hdev, int result)
{
	DBG("%s result 0x%2.2x", hdev->name, result);

	if (hdev->req_status == HCI_REQ_PEND) {
		hdev->req_result = result;
		hdev->req_status = HCI_REQ_DONE;
		wake_up_interruptible(&hdev->req_wait_q);
	}
}

static __inline__ void hci_req_cancel(struct hci_dev *hdev, int err)
{
	DBG("%s err 0x%2.2x", hdev->name, err);

	if (hdev->req_status == HCI_REQ_PEND) {
		hdev->req_result = err;
		hdev->req_status = HCI_REQ_CANCELED;
		wake_up_interruptible(&hdev->req_wait_q);
	}
}

/* Execute request and wait for completion. */
static int __hci_request(struct hci_dev *hdev, void (*req)(struct hci_dev *hdev, unsigned long opt),
                         unsigned long opt, __u32 timeout)
{
	DECLARE_WAITQUEUE(wait, current);
	int err = 0;

	DBG("%s start", hdev->name);

	hdev->req_status = HCI_REQ_PEND;

	add_wait_queue(&hdev->req_wait_q, &wait);
	current->state = TASK_INTERRUPTIBLE;

	req(hdev, opt);
	schedule_timeout(timeout);

	current->state = TASK_RUNNING;
	remove_wait_queue(&hdev->req_wait_q, &wait);

	if (signal_pending(current))
		return -EINTR;

	switch (hdev->req_status) {
	case HCI_REQ_DONE:
		err = -bterr(hdev->req_result);
		break;

	case HCI_REQ_CANCELED:
		err = -hdev->req_result;
		break;

	default:
		err = -ETIMEDOUT;
		break;
	};

	hdev->req_status = hdev->req_result = 0;

	DBG("%s end: err %d", hdev->name, err);

	return err;
}

static __inline__ int hci_request(struct hci_dev *hdev, void (*req)(struct hci_dev *hdev, unsigned long opt),
                                  unsigned long opt, __u32 timeout)
{
	int ret;

	/* Serialize all requests */
	hci_req_lock(hdev);
	ret = __hci_request(hdev, req, opt, timeout);
	hci_req_unlock(hdev);

	return ret;
}

/* --------- HCI requests ---------- */
static void hci_reset_req(struct hci_dev *hdev, unsigned long opt)
{
	DBG("%s %ld", hdev->name, opt);

	/* Reset device */
	hci_send_cmd(hdev, OGF_HOST_CTL, OCF_RESET, 0, NULL);
}

static void hci_init_req(struct hci_dev *hdev, unsigned long opt)
{
	set_event_flt_cp ec;
	__u16 param;

	DBG("%s %ld", hdev->name, opt);

	/* Mandatory initialization */

	/* Read Buffer Size (ACL mtu, max pkt, etc.) */
	hci_send_cmd(hdev, OGF_INFO_PARAM, OCF_READ_BUFFER_SIZE, 0, NULL);

	/* Read BD Address */
	hci_send_cmd(hdev, OGF_INFO_PARAM, OCF_READ_BD_ADDR, 0, NULL);

	/* Optional initialization */

	/* Clear Event Filters */
	ec.flt_type  = FLT_CLEAR_ALL;
	ec.cond_type = 0;
	hci_send_cmd(hdev, OGF_HOST_CTL, OCF_SET_EVENT_FLT, SET_EVENT_FLT_CP_SIZE, &ec);

	/* Page timeout ~ 20 secs */
	param = __cpu_to_le16(0x8000);
	hci_send_cmd(hdev, OGF_HOST_CTL, OCF_WRITE_PG_TIMEOUT, 2, &param);

	/* Connection accept timeout ~20 secs */
	param = __cpu_to_le16(0x7d00);
	hci_send_cmd(hdev, OGF_HOST_CTL, OCF_WRITE_CA_TIMEOUT, 2, &param);
}

static void hci_scan_req(struct hci_dev *hdev, unsigned long opt)
{
	__u8 scan = opt;

	DBG("%s %x", hdev->name, scan);

	/* Inquiry and Page scans */
	hci_send_cmd(hdev, OGF_HOST_CTL, OCF_WRITE_SCAN_ENABLE, 1, &scan);
}

static void hci_auth_req(struct hci_dev *hdev, unsigned long opt)
{
	__u8 auth = opt;

	DBG("%s %x", hdev->name, auth);

	/* Authentication */
	hci_send_cmd(hdev, OGF_HOST_CTL, OCF_WRITE_AUTH_ENABLE, 1, &auth);
}

static void hci_inq_req(struct hci_dev *hdev, unsigned long opt)
{
	struct hci_inquiry_req *ir = (struct hci_inquiry_req *) opt;
	inquiry_cp ic;

	DBG("%s", hdev->name);

	/* Start Inquiry */
	memcpy(&ic.lap, &ir->lap, 3);
	ic.lenght  = ir->length;
	ic.num_rsp = ir->num_rsp;
	hci_send_cmd(hdev, OGF_LINK_CTL, OCF_INQUIRY, INQUIRY_CP_SIZE, &ic);
}

/* Open HCI device */
int hci_dev_open(__u16 dev)
{
	struct hci_dev *hdev;
	int ret = 0;

	if (!(hdev = hci_dev_get(dev)))
		return -ENODEV;

	DBG("%s %p", hdev->name, hdev);

	hci_req_lock(hdev);

	if (hdev->flags & HCI_UP) {
		ret = -EALREADY;
		goto done;
	}

	/* Initialize device */
	if (hdev->open(hdev)) {
		ret = -EIO;
		goto done;
	}

	atomic_set(&hdev->cmd_cnt, 1);
	hdev->cmd_sent= NULL;
	hdev->flags  |= HCI_INIT;

	__hci_request(hdev, hci_reset_req, 0, HZ);

	if (!(ret = __hci_request(hdev, hci_init_req, 0, HCI_INIT_TIMEOUT))) {
		hdev->flags |= HCI_UP;
		hci_notify(hdev, HCI_DEV_UP);
	}

	hdev->flags &= ~HCI_INIT;

done:
	hci_req_unlock(hdev);
	hci_dev_put(hdev);

	return ret;
}

/* Close HCI device */
int hci_dev_close(__u16 dev)
{
	struct hci_dev *hdev;

	if (!(hdev = hci_dev_get(dev)))
		return -ENODEV;

	DBG("%s %p", hdev->name, hdev);

	hci_req_cancel(hdev, ENODEV);
	hci_req_lock(hdev);

	if (!(hdev->flags & HCI_UP))
		goto done;

	/* Kill RX and TX tasks */
	tasklet_kill(&hdev->rx_task);
	tasklet_kill(&hdev->tx_task);

	inquiry_cache_flush(&hdev->inq_cache);

	hci_conn_hash_flush(hdev);

	/* Clear flags */
	hdev->flags &= (HCI_NORMAL | HCI_SOCK);

	hci_notify(hdev, HCI_DEV_DOWN);

	if (hdev->flush)
		hdev->flush(hdev);

	/* Reset device */
	bluez_skb_queue_purge(&hdev->cmd_q);
	atomic_set(&hdev->cmd_cnt, 1);
	hdev->flags |= HCI_INIT;
	__hci_request(hdev, hci_reset_req, 0, HZ);
	hdev->flags &= ~HCI_INIT;

	/* Kill cmd task */
	tasklet_kill(&hdev->cmd_task);

	/* Drop queues */
	bluez_skb_queue_purge(&hdev->rx_q);
	bluez_skb_queue_purge(&hdev->cmd_q);
	bluez_skb_queue_purge(&hdev->raw_q);

	/* Drop last sent command */
	if (hdev->cmd_sent) {
		bluez_skb_free(hdev->cmd_sent);
		hdev->cmd_sent = NULL;
	}

	/* After this point our queues are empty
	 * and no tasks are scheduled.
	 */
	hdev->close(hdev);

done:
	hci_req_unlock(hdev);
	hci_dev_put(hdev);

	return 0;
}

/* Interface to HCI drivers */

/* Register HCI device */
int hci_register_dev(struct hci_dev *hdev)
{
	int i;

	DBG("%p name %s type %d", hdev, hdev->name, hdev->type);

	/* Find free slot */
	spin_lock_bh(&hdev_list_lock);
	for (i = 0; i < HCI_MAX_DEV; i++) {
		if (!hdev_list[i]) {
			hdev_list[i] = hdev;

			sprintf(hdev->name, "hci%d", i);
			atomic_set(&hdev->refcnt, 0);
			hdev->id    = i;
			hdev->flags = HCI_NORMAL;

			tasklet_init(&hdev->cmd_task, hci_cmd_task, (unsigned long) hdev);
			tasklet_init(&hdev->rx_task, hci_rx_task, (unsigned long) hdev);
			tasklet_init(&hdev->tx_task, hci_tx_task, (unsigned long) hdev);

			skb_queue_head_init(&hdev->rx_q);
			skb_queue_head_init(&hdev->cmd_q);
			skb_queue_head_init(&hdev->raw_q);

			init_waitqueue_head(&hdev->req_wait_q);
			init_MUTEX(&hdev->req_lock);

			inquiry_cache_init(&hdev->inq_cache);

			conn_hash_init(&hdev->conn_hash);

			memset(&hdev->stat, 0, sizeof(struct hci_dev_stats));

			hci_notify(hdev, HCI_DEV_REG);

			MOD_INC_USE_COUNT;
			break;
		}
	}
	spin_unlock_bh(&hdev_list_lock);

	return (i == HCI_MAX_DEV) ? -1 : i;
}

/* Unregister HCI device */
int hci_unregister_dev(struct hci_dev *hdev)
{
	int i;

	DBG("%p name %s type %d", hdev, hdev->name, hdev->type);

	if (hdev->flags & HCI_UP)
		hci_dev_close(hdev->id);

	/* Find device slot */
	spin_lock(&hdev_list_lock);
	for (i = 0; i < HCI_MAX_DEV; i++) {
		if (hdev_list[i] == hdev) {
			hdev_list[i] = NULL;
			MOD_DEC_USE_COUNT;
			break;
		}
	}
	spin_unlock(&hdev_list_lock);

	hci_notify(hdev, HCI_DEV_UNREG);

	/* Sleep while device is in use */
	while (atomic_read(&hdev->refcnt)) {
		int sleep_cnt = 100;

		DBG("%s sleeping on lock %d", hdev->name, atomic_read(&hdev->refcnt));

		sleep_on_timeout(&hdev->req_wait_q, HZ*10);
		if (!(--sleep_cnt))
			break;
	}

	return 0;
}

/* Interface to upper protocols */

/* Register/Unregister protocols.
 * hci_task_lock is used to ensure that no tasks are running.
 */
int hci_register_proto(struct hci_proto *hproto)
{
	int err = 0;

	DBG("%p name %s", hproto, hproto->name);

	if (hproto->id >= HCI_MAX_PROTO)
		return -EINVAL;

	write_lock_bh(&hci_task_lock);

	if (!hproto_list[hproto->id])
		hproto_list[hproto->id] = hproto;
	else
		err = -1;

	write_unlock_bh(&hci_task_lock);

	return err;
}

int hci_unregister_proto(struct hci_proto *hproto)
{
	int err = 0;

	DBG("%p name %s", hproto, hproto->name);

	if (hproto->id > HCI_MAX_PROTO)
		return -EINVAL;

	write_lock_bh(&hci_task_lock);

	if (hproto_list[hproto->id])
		hproto_list[hproto->id] = NULL;
	else
		err = -ENOENT;

	write_unlock_bh(&hci_task_lock);

	return err;
}

static int hci_send_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;

	if (!hdev) {
		bluez_skb_free(skb);
		return -ENODEV;
	}

	DBG("%s type %d len %d", hdev->name, skb->pkt_type, skb->len);

	if (hdev->flags & HCI_SOCK)
		hci_send_to_sock(hdev, skb);

	/* Get rid of skb owner, prior to sending to the driver. */
	skb_orphan(skb);

	return hdev->send(skb);
}

/* ACL scheduler */
static __inline__ struct hci_conn *hci_low_acl_sent(struct hci_dev *hdev, int *quote)
{
	struct conn_hash *h = &hdev->conn_hash;
	struct hci_conn *conn = NULL;
	int i, num = 0, min = 0xffff;

	conn_hash_lock(h);
	for (i = 0; i < HCI_MAX_CONN; i++) {
		struct hci_conn *c;

		if (!(c = __conn_hash_lookup(h,i)) || !skb_queue_len(&c->acl_q))
			continue;
		num++;

		if (c->acl_sent < min) {
			min  = c->acl_sent;
			conn = c;
		}
	}
	conn_hash_unlock(h);

	if (conn) {
		int q = hdev->acl_cnt / num;
		*quote = q ? q : 1;
	} else
		*quote = 0;

	DBG("conn %p quote %d", conn, *quote);

	return conn;
}

static __inline__ void hci_sched_acl(struct hci_dev *hdev)
{
	struct hci_conn *conn;
	struct sk_buff *skb, *frag;
	int quote;

	DBG("%s", hdev->name);

	while (hdev->acl_cnt) {
		if (!(conn = hci_low_acl_sent(hdev, &quote)))
			break;

		while (quote && (skb = skb_peek(&conn->acl_q))) {
			if (bluez_skb_frags(skb)+1 > hdev->acl_cnt) {
				/* FIXME: Schedule next connection */
				goto done;
			}

			if (!(frag = bluez_skb_clone(skb, GFP_ATOMIC)))
				break;

			skb_unlink(skb);
			do {
				DBG("frag %p len %d", frag, frag->len);

				hci_send_frame(frag);

				conn->acl_sent++;
				hdev->acl_cnt--;
				quote--;
			} while ((frag = bluez_skb_get_frag(skb)));
			kfree_skb(skb);
		}
	}
done:
	return;
}

/* Schedule SCO */
static __inline__ void hci_sched_sco(struct hci_dev *hdev)
{
	struct conn_hash *h = &hdev->conn_hash;
	struct sk_buff *skb;
	int i;

	DBG("%s", hdev->name);

	conn_hash_lock(h);
	for (i = 0; i< HCI_MAX_CONN; i++) {
		struct hci_conn *conn;

		if (!(conn = __conn_hash_lookup(h, i)))
			continue;

		while (hdev->sco_cnt && (skb = skb_dequeue(&conn->sco_q))) {
			hci_send_frame(skb);
			conn->sco_sent++;
			hdev->sco_cnt--;
		}
	}
	conn_hash_unlock(h);
}

/* Send raw HCI frame */
int hci_send_raw(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;

	if (!hdev) {
		bluez_skb_free(skb);
		return -ENODEV;
	}

	DBG("%s type %d len %d", hdev->name, skb->pkt_type, skb->len);

	/* Queue frame according it's type */
	switch (skb->pkt_type) {
	case HCI_COMMAND_PKT:
		skb_queue_tail(&hdev->cmd_q, skb);
		hci_sched_cmd(hdev);
		break;

	case HCI_ACLDATA_PKT:
	case HCI_SCODATA_PKT:
		/* FIXME:
		 * Check header here and queue to aproptiate connection.
		 */
	default:
		skb_queue_tail(&hdev->raw_q, skb);
		hci_sched_tx(hdev);

		return 0;
	};

	return 0;
}

/* Send HCI command */
int hci_send_cmd(struct hci_dev *hdev, __u16 ogf, __u16 ocf, __u32 plen, void *param)
{
	int len = HCI_COMMAND_HDR_SIZE + plen;
	hci_command_hdr *hc;
	struct sk_buff *skb;

	DBG("%s ogf 0x%x ocf 0x%x plen %d", hdev->name, ogf, ocf, plen);

	if (!(skb = bluez_skb_alloc(len, GFP_ATOMIC))) {
		ERR("%s Can't allocate memory for HCI command", hdev->name);
		return -ENOMEM;
	}
	
	hc = (hci_command_hdr *) skb_put(skb, HCI_COMMAND_HDR_SIZE);
	hc->opcode = __cpu_to_le16(cmd_opcode_pack(ocf, ogf));
	hc->plen   = plen;

	if (plen)
		memcpy(skb_put(skb, plen), param, plen);

	DBG("skb len %d", skb->len);

	skb->pkt_type = HCI_COMMAND_PKT;
	skb->dev = (void *) hdev;
	skb_queue_tail(&hdev->cmd_q, skb);
	hci_sched_cmd(hdev);

	return 0;
}

/* Send ACL data */

static void hci_add_acl_hdr(struct sk_buff *skb, __u16 handle, __u16 flags)
{
	int len = skb->len;	
	hci_acl_hdr *ah;

	ah = (hci_acl_hdr *) skb_push(skb, HCI_ACL_HDR_SIZE);
	ah->handle = __cpu_to_le16(acl_handle_pack(handle, flags));
	ah->dlen   = __cpu_to_le16(len);

	skb->h.raw = (void *) ah;
}

int hci_send_acl(struct hci_conn *conn, struct sk_buff *skb, __u16 flags)
{
	struct hci_dev *hdev = conn->hdev;
	struct sk_buff *frag;
	int sent = 0;

	DBG("%s conn %p len %d flags 0x%x", hdev->name, conn, skb->len, flags);
	DBG("frags %d", bluez_skb_frags(skb));

	/* Add ACL header to all fragments */
	flags |= ACL_START;
	frag = skb;
	do {
		DBG("frag %p len %d", frag, frag->len);
		sent += frag->len;

		hci_add_acl_hdr(frag, conn->handle, flags);
		frag->pkt_type = HCI_ACLDATA_PKT;
		frag->dev = (void *) hdev;

		flags = ACL_CONT;
	} while ((frag = bluez_skb_next_frag(frag)));

	skb_queue_tail(&conn->acl_q, skb);
	hci_sched_tx(hdev);

	return sent;
}

/* Send SCO data */
int hci_send_sco(struct hci_conn *conn, struct sk_buff *skb)
{
	struct hci_dev *hdev = conn->hdev;
	hci_sco_hdr hs;

	DBG("%s len %d", hdev->name, skb->len);

	if (skb->len > hdev->sco_mtu) {
		bluez_skb_free(skb);
		return -EINVAL;
	}

	hs.handle = __cpu_to_le16(conn->handle);
	hs.dlen   = skb->len;

	skb->h.raw = skb_push(skb, HCI_SCO_HDR_SIZE);
	memcpy(skb->h.raw, &hs, HCI_SCO_HDR_SIZE);

	skb->dev = (void *) hdev;
	skb->pkt_type = HCI_SCODATA_PKT;
	skb_queue_tail(&conn->sco_q, skb);
	hci_sched_tx(hdev);

	return 0;
}

/* Handle HCI Event packets */

/* Command Complete OGF LINK_CTL  */
static void hci_cc_link_ctl(struct hci_dev *hdev, __u16 ocf, struct sk_buff *skb)
{
	DBG("%s ocf 0x%x", hdev->name, ocf);

	switch (ocf) {
	default:
		DBG("%s Command complete: ogf LINK_CTL ocf %x", hdev->name, ocf);
		break;
	};
}

/* Command Complete OGF LINK_POLICY  */
static void hci_cc_link_policy(struct hci_dev *hdev, __u16 ocf, struct sk_buff *skb)
{
	DBG("%s ocf 0x%x", hdev->name, ocf);

	switch (ocf) {
	default:
		DBG("%s: Command complete: ogf LINK_POLICY ocf %x", hdev->name, ocf);
		break;
	};
}

/* Command Complete OGF HOST_CTL  */
static void hci_cc_host_ctl(struct hci_dev *hdev, __u16 ocf, struct sk_buff *skb)
{
	__u8 status, param;

	DBG("%s ocf 0x%x", hdev->name, ocf);

	switch (ocf) {
	case OCF_RESET:
		status = *((__u8 *) skb->data);

		hci_req_complete(hdev, status);
		break;

	case OCF_SET_EVENT_FLT:
		status = *((__u8 *) skb->data);

		if (status) {
			DBG("%s SET_EVENT_FLT failed %d", hdev->name, status);
		} else {
			DBG("%s SET_EVENT_FLT succeseful", hdev->name);
		}
		break;

	case OCF_WRITE_AUTH_ENABLE:
		status = *((__u8 *) skb->data);
		param  = *(SENT_CMD_PARAM(hdev));

		if (!status) {
			if (param == AUTH_ENABLED)
				hdev->flags |= HCI_AUTH;
			else
				hdev->flags &= ~HCI_AUTH;
		}
		hci_req_complete(hdev, status);
		break;

	case OCF_WRITE_CA_TIMEOUT:
		status = *((__u8 *) skb->data);

		if (status) {
			DBG("%s OCF_WRITE_CA_TIMEOUT failed %d", hdev->name, status);
		} else {
			DBG("%s OCF_WRITE_CA_TIMEOUT succeseful", hdev->name);
		}
		break;

	case OCF_WRITE_PG_TIMEOUT:
		status = *((__u8 *) skb->data);

		if (status) {
			DBG("%s OCF_WRITE_PG_TIMEOUT failed %d", hdev->name, status);
		} else {
			DBG("%s: OCF_WRITE_PG_TIMEOUT succeseful", hdev->name);
		}
		break;

	case OCF_WRITE_SCAN_ENABLE:
		status = *((__u8 *) skb->data);
		param  = *(SENT_CMD_PARAM(hdev));

		if (!status) {
			switch (param) {
			case IS_ENA_PS_ENA:
				hdev->flags |=  HCI_PSCAN | HCI_ISCAN;
				break;

			case IS_ENA_PS_DIS:
				hdev->flags &= ~HCI_PSCAN;
				hdev->flags |=  HCI_ISCAN;
				break;

			case IS_DIS_PS_ENA:
				hdev->flags &= ~HCI_ISCAN;
				hdev->flags |=  HCI_PSCAN;
				break;

			default:
				hdev->flags &= ~(HCI_ISCAN | HCI_PSCAN);
				break;
			};
		}
		hci_req_complete(hdev, status);
		break;

	default:
		DBG("%s Command complete: ogf HOST_CTL ocf %x", hdev->name, ocf);
		break;
	};
}

/* Command Complete OGF INFO_PARAM  */
static void hci_cc_info_param(struct hci_dev *hdev, __u16 ocf, struct sk_buff *skb)
{
	read_buffer_size_rp *rsp;
	read_bd_addr_rp *rap;

	DBG("%s ocf 0x%x", hdev->name, ocf);

	switch (ocf) {
	case OCF_READ_BUFFER_SIZE:
		rsp = (read_buffer_size_rp *) skb->data;

		if (rsp->status) {
			DBG("%s READ_BUFFER_SIZE failed %d", hdev->name, rsp->status);
			break;
		}

		hdev->acl_mtu = __le16_to_cpu(rsp->acl_mtu);
		hdev->sco_mtu = rsp->sco_mtu;
		hdev->acl_max = hdev->acl_cnt = __le16_to_cpu(rsp->acl_max_pkt);
		hdev->sco_max = hdev->sco_cnt = __le16_to_cpu(rsp->sco_max_pkt);

		DBG("%s mtu: acl %d, sco %d max_pkt: acl %d, sco %d", hdev->name,
		    hdev->acl_mtu, hdev->sco_mtu, hdev->acl_max, hdev->sco_max);

		break;

	case OCF_READ_BD_ADDR:
		rap = (read_bd_addr_rp *) skb->data;

		if (!rap->status) {
			bacpy(&hdev->bdaddr, &rap->bdaddr);
		} else {
			DBG("%s: READ_BD_ADDR failed %d", hdev->name, rap->status);
		}

		hci_req_complete(hdev, rap->status);
		break;

	default:
		DBG("%s Command complete: ogf INFO_PARAM ocf %x", hdev->name, ocf);
		break;
	};
}

/* Command Status OGF LINK_CTL  */
static void hci_cs_link_ctl(struct hci_dev *hdev, __u16 ocf, __u8 status)
{
	struct hci_proto * hp;

	DBG("%s ocf 0x%x", hdev->name, ocf);

	switch (ocf) {
	case OCF_CREATE_CONN:
		if (status) {
			create_conn_cp *cc = (void *) SENT_CMD_PARAM(hdev);
			DBG("%s Create connection error: status 0x%x %s", hdev->name,
			    status, batostr(&cc->bdaddr));

			/* Notify upper protocols */
			if ((hp = GET_HPROTO(HCI_PROTO_L2CAP)) && hp->connect_cfm) {
				tasklet_disable(&hdev->tx_task);
				hp->connect_cfm(hdev, &cc->bdaddr, status, NULL);
				tasklet_enable(&hdev->tx_task);
			}
		}
		break;

	case OCF_INQUIRY:
		if (status) {
			DBG("%s Inquiry error: status 0x%x", hdev->name, status);
			hci_req_complete(hdev, status);
		}
		break;

	default:
		DBG("%s Command status: ogf LINK_CTL ocf %x", hdev->name, ocf);
		break;
	};
}

/* Command Status OGF LINK_POLICY */
static void hci_cs_link_policy(struct hci_dev *hdev, __u16 ocf, __u8 status)
{
	DBG("%s ocf 0x%x", hdev->name, ocf);

	switch (ocf) {
	default:
		DBG("%s Command status: ogf HOST_POLICY ocf %x", hdev->name, ocf);
		break;
	};
}

/* Command Status OGF HOST_CTL */
static void hci_cs_host_ctl(struct hci_dev *hdev, __u16 ocf, __u8 status)
{
	DBG("%s ocf 0x%x", hdev->name, ocf);

	switch (ocf) {
	default:
		DBG("%s Command status: ogf HOST_CTL ocf %x", hdev->name, ocf);
		break;
	};
}

/* Command Status OGF INFO_PARAM  */
static void hci_cs_info_param(struct hci_dev *hdev, __u16 ocf, __u8 status)
{
	DBG("%s: hci_cs_info_param: ocf 0x%x", hdev->name, ocf);

	switch (ocf) {
	default:
		DBG("%s Command status: ogf INFO_PARAM ocf %x", hdev->name, ocf);
		break;
	};
}

/* Inquiry Complete */
static void hci_inquiry_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);

	DBG("%s status %d", hdev->name, status);

	hci_req_complete(hdev, status);
}

/* Inquiry Result */
static void hci_inquiry_result_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	inquiry_info *info = (inquiry_info *) (skb->data + 1);
	int num_rsp = *((__u8 *) skb->data);

	DBG("%s num_rsp %d", hdev->name, num_rsp);

	for (; num_rsp; num_rsp--)
		inquiry_cache_update(&hdev->inq_cache, info++);
}

/* Connect Request */
static void hci_conn_request_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	evt_conn_request *cr = (evt_conn_request *) skb->data;
	struct hci_proto *hp;
	accept_conn_req_cp ac;
	int accept = 0;

	DBG("%s Connection request: %s type 0x%x", hdev->name, batostr(&cr->bdaddr), cr->type);

	/* Notify upper protocols */
	if ((hp = GET_HPROTO(HCI_PROTO_L2CAP)) && hp->connect_ind) {
		tasklet_disable(&hdev->tx_task);
		accept = hp->connect_ind(hdev, &cr->bdaddr);
		tasklet_enable(&hdev->tx_task);
	}

	if (accept) {
		/* Connection accepted by upper layer */
		bacpy(&ac.bdaddr, &cr->bdaddr);
		ac.role = 0x01; /* Remain slave */
		hci_send_cmd(hdev, OGF_LINK_CTL, OCF_ACCEPT_CONN_REQ, ACCEPT_CONN_REQ_CP_SIZE, &ac);
	} else {
		/* Connection rejected by upper layer */
		/* FIXME: 
		 * Should we use HCI reject here ?
		 */
		return;
	}
}

/* Connect Complete */
static void hci_conn_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	evt_conn_complete *cc = (evt_conn_complete *) skb->data;
	struct hci_conn *conn = NULL;
	struct hci_proto *hp;

	DBG("%s", hdev->name);

	tasklet_disable(&hdev->tx_task);

	if (!cc->status)
 		conn = hci_conn_add(hdev, __le16_to_cpu(cc->handle), &cc->bdaddr);

	/* Notify upper protocols */
	if ((hp = GET_HPROTO(HCI_PROTO_L2CAP)) && hp->connect_cfm)
		hp->connect_cfm(hdev, &cc->bdaddr, cc->status, conn);

	tasklet_enable(&hdev->tx_task);
}

/* Disconnect Complete */
static void hci_disconn_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	evt_disconn_complete *dc = (evt_disconn_complete *) skb->data;
	struct hci_conn *conn = NULL;
	struct hci_proto *hp;
	__u16 handle = __le16_to_cpu(dc->handle);

	DBG("%s", hdev->name);

	if (!dc->status && (conn = conn_hash_lookup(&hdev->conn_hash, handle))) {
		tasklet_disable(&hdev->tx_task);

		/* Notify upper protocols */
		if ((hp = GET_HPROTO(HCI_PROTO_L2CAP)) && hp->disconn_ind)
			hp->disconn_ind(conn, dc->reason);

		hci_conn_del(hdev, conn);

		tasklet_enable(&hdev->tx_task);
	}
}

/* Number of completed packets */
static void hci_num_comp_pkts_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	evt_num_comp_pkts *nc = (evt_num_comp_pkts *) skb->data;
	__u16 *ptr;
	int i;

	skb_pull(skb, EVT_NUM_COMP_PKTS_SIZE);

	DBG("%s num_hndl %d", hdev->name, nc->num_hndl);

	if (skb->len < nc->num_hndl * 4) {
		DBG("%s bad parameters", hdev->name);
		return;
	}

	tasklet_disable(&hdev->tx_task);

	for (i = 0, ptr = (__u16 *) skb->data; i < nc->num_hndl; i++) {
		struct hci_conn *conn;
		__u16 handle, count;

		handle = __le16_to_cpu(*ptr++);
		count  = __le16_to_cpu(*ptr++);

		hdev->acl_cnt += count;

		if ((conn = conn_hash_lookup(&hdev->conn_hash, handle)))
			conn->acl_sent -= count;
	}

	tasklet_enable(&hdev->tx_task);
	
	hci_sched_tx(hdev);
}

static __inline__ void hci_event_packet(struct hci_dev *hdev, struct sk_buff *skb)
{
	hci_event_hdr *he = (hci_event_hdr *) skb->data;
	evt_cmd_status *cs;
	evt_cmd_complete *ec;
	__u16 opcode, ocf, ogf;

	skb_pull(skb, HCI_EVENT_HDR_SIZE);

	DBG("%s evt 0x%x", hdev->name, he->evt);

	switch (he->evt) {
	case EVT_NUM_COMP_PKTS:
		hci_num_comp_pkts_evt(hdev, skb);
		break;

	case EVT_INQUIRY_COMPLETE:
		hci_inquiry_complete_evt(hdev, skb);
		break;

	case EVT_INQUIRY_RESULT:
		hci_inquiry_result_evt(hdev, skb);
		break;

	case EVT_CONN_REQUEST:
		hci_conn_request_evt(hdev, skb);
		break;

	case EVT_CONN_COMPLETE:
		hci_conn_complete_evt(hdev, skb);
		break;

	case EVT_DISCONN_COMPLETE:
		hci_disconn_complete_evt(hdev, skb);
		break;

	case EVT_CMD_STATUS:
		cs = (evt_cmd_status *) skb->data;
		skb_pull(skb, EVT_CMD_STATUS_SIZE);
				
		opcode = __le16_to_cpu(cs->opcode);
		ogf = cmd_opcode_ogf(opcode);
		ocf = cmd_opcode_ocf(opcode);

		if (cs->ncmd) {
			atomic_set(&hdev->cmd_cnt, 1);
			hci_sched_cmd(hdev);
		}

		switch (ogf) {
		case OGF_INFO_PARAM:
			hci_cs_info_param(hdev, ocf, cs->status);
			break;

		case OGF_HOST_CTL:
			hci_cs_host_ctl(hdev, ocf, cs->status);
			break;

		case OGF_LINK_CTL:
			hci_cs_link_ctl(hdev, ocf, cs->status);
			break;

		case OGF_LINK_POLICY:
			hci_cs_link_policy(hdev, ocf, cs->status);
			break;

		default:
			DBG("%s Command Status OGF %x", hdev->name, ogf);
			break;
		};

		break;

	case EVT_CMD_COMPLETE:
		ec = (evt_cmd_complete *) skb->data;
		skb_pull(skb, EVT_CMD_COMPLETE_SIZE);

		opcode = __le16_to_cpu(ec->opcode);
		ogf = cmd_opcode_ogf(opcode);
		ocf = cmd_opcode_ocf(opcode);

		if (ec->ncmd) {
			atomic_set(&hdev->cmd_cnt, 1);
			hci_sched_cmd(hdev);
		}

		switch (ogf) {
		case OGF_INFO_PARAM:
			hci_cc_info_param(hdev, ocf, skb);
			break;

		case OGF_HOST_CTL:
			hci_cc_host_ctl(hdev, ocf, skb);
			break;

		case OGF_LINK_CTL:
			hci_cc_link_ctl(hdev, ocf, skb);
			break;

		case OGF_LINK_POLICY:
			hci_cc_link_policy(hdev, ocf, skb);
			break;

		default:
			DBG("%s Command Completed OGF %x", hdev->name, ogf);
			break;
		};

		break;
	};

	bluez_skb_free(skb);
	hdev->stat.evt_rx++;
}

/* ACL data packet */
static __inline__ void hci_acldata_packet(struct hci_dev *hdev, struct sk_buff *skb)
{
	hci_acl_hdr *ah = (void *) skb->data;
	struct hci_conn *conn;
	__u16 handle, flags;

	skb_pull(skb, HCI_ACL_HDR_SIZE);

	handle = __le16_to_cpu(ah->handle);
	flags  = acl_flags(handle);
	handle = acl_handle(handle);

	DBG("%s len %d handle 0x%x flags 0x%x", hdev->name, skb->len, handle, flags);

	if ((conn = conn_hash_lookup(&hdev->conn_hash, handle))) {
		struct hci_proto *hp;

		/* Send to upper protocol */
		if ((hp = GET_HPROTO(HCI_PROTO_L2CAP)) && hp->recv_acldata)
			hp->recv_acldata(conn, skb, flags);
		else
			bluez_skb_free(skb);
	}

	hdev->stat.acl_rx++;
}

/* SCO data packet */
static __inline__ void hci_scodata_packet(struct hci_dev *hdev, struct sk_buff *skb)
{
	hci_sco_hdr *sh = (void *) skb->data;
	struct hci_conn *conn;

	skb_pull(skb, HCI_SCO_HDR_SIZE);

	DBG("%s len %d", hdev->name, skb->len);

	if ((conn = conn_hash_lookup(&hdev->conn_hash, __le16_to_cpu(sh->handle)))) {
		struct hci_proto *hp;

		/* Send to upper protocol */
		if ((hp = GET_HPROTO(HCI_PROTO_L2CAP)) && hp->recv_acldata)
			hp->recv_scodata(conn, skb);
		else
			bluez_skb_free(skb);
	}

	hdev->stat.sco_rx++;
}

/* ----- HCI tasks ----- */
void hci_rx_task(unsigned long arg)
{
	struct hci_dev *hdev = (struct hci_dev *) arg;
	struct sk_buff *skb;

	DBG("%s", hdev->name);

	read_lock(&hci_task_lock);

	while ((skb = skb_dequeue(&hdev->rx_q))) {
		if (hdev->flags & HCI_SOCK) {
			/* Send copy to the sockets */
			hci_send_to_sock(hdev, skb);
		}

		if (hdev->flags & HCI_INIT) {
			/* Don't process data packets in this states. */
			switch (skb->pkt_type) {
			case HCI_ACLDATA_PKT:
			case HCI_SCODATA_PKT:
				bluez_skb_free(skb);
				continue;
			};
		}

		if (hdev->flags & HCI_NORMAL) {
			/* Handle frame */
			switch (skb->pkt_type) {
			case HCI_EVENT_PKT:
				hci_event_packet(hdev, skb);
				break;

			case HCI_ACLDATA_PKT:
				DBG("%s ACL data packet", hdev->name);
				hci_acldata_packet(hdev, skb);
				break;

			case HCI_SCODATA_PKT:
				DBG("%s SCO data packet", hdev->name);
				hci_scodata_packet(hdev, skb);
				break;

			default:
				bluez_skb_free(skb);
				break;
			};
		} else {
			bluez_skb_free(skb);
		}
	}

	read_unlock(&hci_task_lock);
}

static void hci_tx_task(unsigned long arg)
{
	struct hci_dev *hdev = (struct hci_dev *) arg;
	struct sk_buff *skb;

	read_lock(&hci_task_lock);

	DBG("%s acl %d sco %d", hdev->name, hdev->acl_cnt, hdev->sco_cnt);

	/* Schedule queues and send stuff to HCI driver */

	hci_sched_acl(hdev);

	hci_sched_sco(hdev);

	/* Send next queued raw(unknown type) packet */
	while ((skb = skb_dequeue(&hdev->raw_q)))
		hci_send_frame(skb);

	read_unlock(&hci_task_lock);
}

static void hci_cmd_task(unsigned long arg)
{
	struct hci_dev *hdev = (struct hci_dev *) arg;
	struct sk_buff *skb;

	DBG("%s cmd %d", hdev->name, atomic_read(&hdev->cmd_cnt));

	/* Send queued commands */
	if (atomic_read(&hdev->cmd_cnt) && (skb = skb_dequeue(&hdev->cmd_q))) {
		if (hdev->cmd_sent)
			bluez_skb_free(hdev->cmd_sent);

		if ((hdev->cmd_sent = bluez_skb_clone(skb, GFP_ATOMIC))) {
			atomic_dec(&hdev->cmd_cnt);
			hci_send_frame(skb);
		} else {
			skb_queue_head(&hdev->cmd_q, skb);
			hci_sched_cmd(hdev);
		}
	}
}

/* Receive frame from HCI drivers */
int hci_recv_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;

	if (!hdev || !(hdev->flags & (HCI_UP | HCI_INIT))) {
		bluez_skb_free(skb);
		return -1;
	}

	DBG("%s type %d len %d", hdev->name, skb->pkt_type, skb->len);

	/* Incomming skb */
	bluez_cb(skb)->incomming = 1;

	/* Queue frame for rx task */
	skb_queue_tail(&hdev->rx_q, skb);
	hci_sched_rx(hdev);

	return 0;
}

/* ----- HCI Ioctl helpers ----- */
int hci_dev_reset(__u16 dev)
{
	struct hci_dev *hdev;
	int ret = 0;

	if (!(hdev = hci_dev_get(dev)))
		return -ENODEV;

	hci_req_lock(hdev);
	tasklet_disable(&hdev->tx_task);

	if (!(hdev->flags & HCI_UP))
		goto done;

	/* Drop queues */
	bluez_skb_queue_purge(&hdev->rx_q);
	bluez_skb_queue_purge(&hdev->cmd_q);

	inquiry_cache_flush(&hdev->inq_cache);

	hci_conn_hash_flush(hdev);

	if (hdev->flush)
		hdev->flush(hdev);

	atomic_set(&hdev->cmd_cnt, 1); 
	hdev->acl_cnt = 0; hdev->sco_cnt = 0;

	ret = __hci_request(hdev, hci_reset_req, 0, HCI_INIT_TIMEOUT);

done:
	tasklet_enable(&hdev->tx_task);
	hci_req_unlock(hdev);
	hci_dev_put(hdev);

	return ret;
}

int hci_dev_reset_stat(__u16 dev)
{
	struct hci_dev *hdev;
	int ret = 0;

	if (!(hdev = hci_dev_get(dev)))
		return -ENODEV;

	memset(&hdev->stat, 0, sizeof(struct hci_dev_stats));

	hci_dev_put(hdev);

	return ret;
}

int hci_dev_setauth(unsigned long arg)
{
	struct hci_dev *hdev;
	struct hci_dev_req dr;
	int ret = 0;

	if (copy_from_user(&dr, (void *) arg, sizeof(dr)))
		return -EFAULT;

	if (!(hdev = hci_dev_get(dr.dev_id)))
		return -ENODEV;

	ret = hci_request(hdev, hci_auth_req, dr.dev_opt, HCI_INIT_TIMEOUT);

	hci_dev_put(hdev);

	return ret;
}

int hci_dev_setscan(unsigned long arg)
{
	struct hci_dev *hdev;
	struct hci_dev_req dr;
	int ret = 0;

	if (copy_from_user(&dr, (void *) arg, sizeof(dr)))
		return -EFAULT;

	if (!(hdev = hci_dev_get(dr.dev_id)))
		return -ENODEV;

	ret = hci_request(hdev, hci_scan_req, dr.dev_opt, HCI_INIT_TIMEOUT);

	hci_dev_put(hdev);

	return ret;
}

int hci_dev_list(unsigned long arg)
{
	struct hci_dev_list_req *dl;
	struct hci_dev_req *dr;
	struct hci_dev *hdev;
	int i, n, size;
	__u16 dev_num;

	if (get_user(dev_num, (__u16 *) arg))
		return -EFAULT;

	size = dev_num * sizeof(struct hci_dev_req) + sizeof(__u16);

	if (verify_area(VERIFY_WRITE, (void *) arg, size))
		return -EFAULT;

	if (!(dl = kmalloc(size, GFP_KERNEL)))
		return -ENOMEM;
	dr = dl->dev_req;

	spin_lock_bh(&hdev_list_lock);
	for (i = 0, n = 0; i < HCI_MAX_DEV && n < dev_num; i++) {
		if ((hdev = hdev_list[i])) {
			(dr + n)->dev_id  = hdev->id;
			(dr + n)->dev_opt = hdev->flags;
			n++;
		}
	}
	spin_unlock_bh(&hdev_list_lock);

	dl->dev_num = n;
	size = n * sizeof(struct hci_dev_req) + sizeof(__u16);

	copy_to_user((void *) arg, dl, size);

	return 0;
}

int hci_dev_info(unsigned long arg)
{
	struct hci_dev *hdev;
	struct hci_dev_info di;
	int err = 0;

	if (copy_from_user(&di, (void *) arg, sizeof(di)))
		return -EFAULT;

	if (!(hdev = hci_dev_get(di.dev_id)))
		return -ENODEV;

	strcpy(di.name, hdev->name);
	di.type    = hdev->type;
	di.flags   = hdev->flags;
	di.acl_mtu = hdev->acl_mtu;
	di.acl_max = hdev->acl_max;
	di.sco_mtu = hdev->sco_mtu;
	di.sco_max = hdev->sco_max;
	di.bdaddr  = hdev->bdaddr;

	memcpy(&di.stat, &hdev->stat, sizeof(di.stat));

	if (copy_to_user((void *) arg, &di, sizeof(di)))
		err = -EFAULT;

	hci_dev_put(hdev);

	return err;
}

__u32 hci_dev_setmode(struct hci_dev *hdev, __u32 mode)
{
	__u32 omode = hdev->flags & HCI_MODE_MASK;

	hdev->flags &= ~HCI_MODE_MASK;
	hdev->flags |= (mode & HCI_MODE_MASK);

	return omode;
}

__u32 hci_dev_getmode(struct hci_dev *hdev)
{
	return hdev->flags & HCI_MODE_MASK;
}

int hci_inquiry(unsigned long arg)
{
	struct inquiry_cache *cache;
	struct hci_inquiry_req ir;
	struct hci_dev *hdev;
	int err = 0, do_inquiry = 0;
	long timeo;
	__u8 *buf, *ptr;

	ptr = (void *) arg;
	if (copy_from_user(&ir, ptr, sizeof(ir)))
		return -EFAULT;

	if (!(hdev = hci_dev_get(ir.dev_id)))
		return -ENODEV;

	cache = &hdev->inq_cache;

	inquiry_cache_lock(cache);
	if (inquiry_cache_age(cache) > INQUIRY_CACHE_AGE_MAX || ir.flags & IREQ_CACHE_FLUSH) {
		inquiry_cache_flush(cache);
		do_inquiry = 1;
	}
	inquiry_cache_unlock(cache);

	timeo = ir.length * 2 * HZ;
	if (do_inquiry && (err = hci_request(hdev, hci_inq_req, (unsigned long)&ir, timeo)) < 0)
		goto done;

	/* cache_dump can't sleep. Therefore we allocate temp buffer and then
	 * copy it to the user space.
	 */
	if (!(buf = kmalloc(sizeof(inquiry_info) * ir.num_rsp, GFP_KERNEL))) {
		err = -ENOMEM;
		goto done;
	}
	ir.num_rsp = inquiry_cache_dump(cache, ir.num_rsp, buf);

	DBG("num_rsp %d", ir.num_rsp);

	if (!verify_area(VERIFY_WRITE, ptr, sizeof(ir) + (sizeof(inquiry_info) * ir.num_rsp))) {
		copy_to_user(ptr, &ir, sizeof(ir));
		ptr += sizeof(ir);
	        copy_to_user(ptr, buf, sizeof(inquiry_info) * ir.num_rsp);
	} else 
		err = -EFAULT;

	kfree(buf);

done:
	hci_dev_put(hdev);

	return err;
}

int hci_core_init(void)
{
	/* Init locks */
	spin_lock_init(&hdev_list_lock);

	return 0;
}

int hci_core_cleanup(void)
{
	return 0;
}
