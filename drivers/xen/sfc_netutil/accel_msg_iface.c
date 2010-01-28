/****************************************************************************
 * Solarflare driver for Xen network acceleration
 *
 * Copyright 2006-2008: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Maintained by Solarflare Communications <linux-xen-drivers@solarflare.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

#include <xen/evtchn.h>

#include "accel_util.h"
#include "accel_msg_iface.h"

#define NET_ACCEL_MSG_Q_SIZE (1024)
#define NET_ACCEL_MSG_Q_MASK (NET_ACCEL_MSG_Q_SIZE - 1)

#ifdef NDEBUG
#define NET_ACCEL_CHECK_MAGIC(_p, _errval)
#define NET_ACCEL_SHOW_QUEUE(_t, _q, _id)
#else
#define NET_ACCEL_CHECK_MAGIC(_p, _errval)				\
	if (_p->magic != NET_ACCEL_MSG_MAGIC) {				\
		printk(KERN_ERR "%s: passed invalid shared page %p!\n", \
		       __FUNCTION__, _p);				\
		return _errval;						\
	}
#define NET_ACCEL_SHOW_QUEUE(_t, _q, _id)				\
	printk(_t ": queue %d write %x read %x base %x limit %x\n",     \
	       _id, _q->write, _q->read, _q->base, _q->limit);
#endif

/*
 * We've been passed at least 2 pages. 1 control page and 1 or more
 * data pages.
 */
int net_accel_msg_init_page(void *mem, int len, int up)
{
	struct net_accel_shared_page *shared_page = 
		(struct net_accel_shared_page*)mem;

	if ((unsigned long)shared_page & NET_ACCEL_MSG_Q_MASK)
		return -EINVAL;

	shared_page->magic = NET_ACCEL_MSG_MAGIC;

	shared_page->aflags = 0;

	shared_page->net_dev_up = up;

	return 0;
}
EXPORT_SYMBOL_GPL(net_accel_msg_init_page);


void net_accel_msg_init_queue(sh_msg_fifo2 *queue,
			      struct net_accel_msg_queue *indices,
			      struct net_accel_msg *base, int size)
{
	queue->fifo = base;
	spin_lock_init(&queue->lock);
	sh_fifo2_init(queue, size-1, &indices->read, &indices->write);
}
EXPORT_SYMBOL_GPL(net_accel_msg_init_queue);


static inline int _net_accel_msg_send(struct net_accel_shared_page *sp,
				      sh_msg_fifo2 *queue,
				      struct net_accel_msg *msg,
				      int is_reply)
{
	int rc = 0;
	NET_ACCEL_CHECK_MAGIC(sp, -EINVAL);
	rmb();
	if (is_reply) {
		EPRINTK_ON(sh_fifo2_is_full(queue));
		sh_fifo2_put(queue, *msg);
	} else {
		if (sh_fifo2_not_half_full(queue)) {
			sh_fifo2_put(queue, *msg);
		} else {
			rc = -ENOSPC;
		}
	}
	wmb();
	return rc;
}

/* Notify after a batch of messages have been sent */
void net_accel_msg_notify(int irq)
{
	notify_remote_via_irq(irq);
}
EXPORT_SYMBOL_GPL(net_accel_msg_notify);

/* 
 * Send a message on the specified FIFO. Returns 0 on success, -errno
 * on failure. The message in msg is copied to the current slot of the
 * FIFO.
 */
int net_accel_msg_send(struct net_accel_shared_page *sp, sh_msg_fifo2 *q, 
		       struct net_accel_msg *msg)
{
	unsigned long flags;
	int rc;
	net_accel_msg_lock_queue(q, &flags);
	rc = _net_accel_msg_send(sp, q, msg, 0);
	net_accel_msg_unlock_queue(q, &flags);
	return rc;
}
EXPORT_SYMBOL_GPL(net_accel_msg_send);


/* As net_accel_msg_send but also posts a notification to the far end. */
int net_accel_msg_send_notify(struct net_accel_shared_page *sp, int irq, 
			      sh_msg_fifo2 *q, struct net_accel_msg *msg)
{
	unsigned long flags;
	int rc;
	net_accel_msg_lock_queue(q, &flags);
	rc = _net_accel_msg_send(sp, q, msg, 0);
	net_accel_msg_unlock_queue(q, &flags);
	if (rc >= 0)
		notify_remote_via_irq(irq);
	return rc;
}
EXPORT_SYMBOL_GPL(net_accel_msg_send_notify);


int net_accel_msg_reply(struct net_accel_shared_page *sp, sh_msg_fifo2 *q, 
		       struct net_accel_msg *msg)
{
	unsigned long flags;
	int rc;
	net_accel_msg_lock_queue(q, &flags);
	rc = _net_accel_msg_send(sp, q, msg, 1);
	net_accel_msg_unlock_queue(q, &flags);
	return rc;
}
EXPORT_SYMBOL_GPL(net_accel_msg_reply);


/* As net_accel_msg_send but also posts a notification to the far end. */
int net_accel_msg_reply_notify(struct net_accel_shared_page *sp, int irq, 
			      sh_msg_fifo2 *q, struct net_accel_msg *msg)
{
	unsigned long flags;
	int rc;
	net_accel_msg_lock_queue(q, &flags);
	rc = _net_accel_msg_send(sp, q, msg, 1);
	net_accel_msg_unlock_queue(q, &flags);
	if (rc >= 0)
		notify_remote_via_irq(irq);
	return rc;
}
EXPORT_SYMBOL_GPL(net_accel_msg_reply_notify);


/*
 * Look at a received message, if any, so a decision can be made about
 * whether to read it now or not.  Cookie is a bit of debug which is
 * set here and checked when passed to net_accel_msg_recv_next()
 */
int net_accel_msg_peek(struct net_accel_shared_page *sp, 
		       sh_msg_fifo2 *queue, 
		       struct net_accel_msg *msg, int *cookie)
{
	unsigned long flags;
	int rc = 0;
	NET_ACCEL_CHECK_MAGIC(sp, -EINVAL);
	net_accel_msg_lock_queue(queue, &flags);
	rmb();
	if (sh_fifo2_is_empty(queue)) {
		rc = -ENOENT;
	} else {
		*msg = sh_fifo2_peek(queue);
		*cookie = *(queue->fifo_rd_i);
	}
	net_accel_msg_unlock_queue(queue, &flags);
	return rc;
}
EXPORT_SYMBOL_GPL(net_accel_msg_peek);


/*
 * Move the queue onto the next element, used after finished with a
 * peeked msg 
 */
int net_accel_msg_recv_next(struct net_accel_shared_page *sp, 
			    sh_msg_fifo2 *queue, int cookie)
{
	unsigned long flags;
	NET_ACCEL_CHECK_MAGIC(sp, -EINVAL);
	net_accel_msg_lock_queue(queue, &flags);
	rmb();
	/* Mustn't be empty */
	BUG_ON(sh_fifo2_is_empty(queue));
	/* 
	 * Check cookie matches, i.e. we're advancing over the same message
	 * as was got using peek 
	 */
	BUG_ON(cookie != *(queue->fifo_rd_i));
	sh_fifo2_rd_next(queue);
	wmb();
	net_accel_msg_unlock_queue(queue, &flags);
	return 0;
}
EXPORT_SYMBOL_GPL(net_accel_msg_recv_next);


/* 
 * Receive a message on the specified FIFO. Returns 0 on success,
 * -errno on failure.
 */
int net_accel_msg_recv(struct net_accel_shared_page *sp, sh_msg_fifo2 *queue, 
		       struct net_accel_msg *msg)
{
	unsigned long flags;
	int rc = 0;
	NET_ACCEL_CHECK_MAGIC(sp, -EINVAL);
	net_accel_msg_lock_queue(queue, &flags);
	rmb();
	if (sh_fifo2_is_empty(queue)) {
		rc = -ENOENT;
	} else {
		sh_fifo2_get(queue, msg);
	}
	wmb();
	net_accel_msg_unlock_queue(queue, &flags);
	return rc;
}
EXPORT_SYMBOL_GPL(net_accel_msg_recv);


/* 
 * Start sending a message without copying. returns a pointer to a message
 * that will be filled out in place. The queue is locked until the message 
 * is sent.
 */
struct net_accel_msg *net_accel_msg_start_send(struct net_accel_shared_page *sp,
					       sh_msg_fifo2 *queue, unsigned long *flags)
{
	struct net_accel_msg *msg;
	NET_ACCEL_CHECK_MAGIC(sp, NULL);
	net_accel_msg_lock_queue(queue, flags);
	rmb();
	if (sh_fifo2_not_half_full(queue)) {
		msg = sh_fifo2_pokep(queue);
	} else {
		net_accel_msg_unlock_queue(queue, flags);
		msg = NULL;
	}
	return msg;
}
EXPORT_SYMBOL_GPL(net_accel_msg_start_send);


static inline void _msg_complete(struct net_accel_shared_page *sp,
				 sh_msg_fifo2 *queue,
				 unsigned long *flags)
{
	sh_fifo2_wr_next(queue);
	net_accel_msg_unlock_queue(queue, flags);
}

/*
 * Complete the sending of a message started with net_accel_msg_start_send. The 
 * message is implicit since the queue was locked by _start
 */
void net_accel_msg_complete_send(struct net_accel_shared_page *sp,
				 sh_msg_fifo2 *queue,
				 unsigned long *flags)
{
	_msg_complete(sp, queue, flags);
}
EXPORT_SYMBOL_GPL(net_accel_msg_complete_send);

/* As net_accel_msg_complete_send but does the notify. */
void net_accel_msg_complete_send_notify(struct net_accel_shared_page *sp, 
					sh_msg_fifo2 *queue, 
					unsigned long *flags, int irq)
{
	_msg_complete(sp, queue, flags);
	notify_remote_via_irq(irq);
}
EXPORT_SYMBOL_GPL(net_accel_msg_complete_send_notify);
