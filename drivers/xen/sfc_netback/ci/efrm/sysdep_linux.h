/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file provides version-independent Linux kernel API for efrm library.
 * Only kernels >=2.6.9 are supported.
 *
 * Copyright 2005-2007: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Kfifo API is partially stolen from linux-2.6.22/include/linux/list.h
 * Copyright (C) 2004 Stelian Pop <stelian@popies.net>
 *
 * Developed and maintained by Solarflare Communications:
 *                      <linux-xen-drivers@solarflare.com>
 *                      <onload-dev@solarflare.com>
 *
 * Certain parts of the driver were implemented by
 *          Alexandra Kossovsky <Alexandra.Kossovsky@oktetlabs.ru>
 *          OKTET Labs Ltd, Russia,
 *          http://oktetlabs.ru, <info@oktetlabs.ru>
 *          by request of Solarflare Communications
 *
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

#ifndef __CI_EFRM_SYSDEP_LINUX_H__
#define __CI_EFRM_SYSDEP_LINUX_H__

#include <linux/version.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/hardirq.h>
#include <linux/kernel.h>
#include <linux/if_ether.h>
#include <linux/completion.h>
#include <linux/in.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
/* get roundup_pow_of_two(), which was in kernel.h in early kernel versions */
#include <linux/log2.h>
#endif

/********************************************************************
 *
 * List API
 *
 ********************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
static inline void
list_replace_init(struct list_head *old, struct list_head *new)
{
	new->next = old->next;
	new->next->prev = new;
	new->prev = old->prev;
	new->prev->next = new;
	INIT_LIST_HEAD(old);
}
#endif

static inline struct list_head *list_pop(struct list_head *list)
{
	struct list_head *link = list->next;
	list_del(link);
	return link;
}

static inline struct list_head *list_pop_tail(struct list_head *list)
{
	struct list_head *link = list->prev;
	list_del(link);
	return link;
}

/********************************************************************
 *
 * Workqueue API
 *
 ********************************************************************/

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
#define NEED_OLD_WORK_API

/**
 * The old and new work function prototypes just change
 * the type of the pointer in the only argument, so it's
 * safe to cast one function type to the other
 */
typedef void (*efrm_old_work_func_t) (void *p);

#undef INIT_WORK
#define INIT_WORK(_work, _func)					\
	do {							\
		INIT_LIST_HEAD(&(_work)->entry);		\
		(_work)->pending = 0;				\
		PREPARE_WORK((_work),				\
			     (efrm_old_work_func_t) (_func),	\
			     (_work));				\
	} while (0)

#endif

/********************************************************************
 *
 * Kfifo API
 *
 ********************************************************************/

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)

#if !defined(RHEL_RELEASE_CODE) || (RHEL_RELEASE_CODE < 1029)
typedef unsigned gfp_t;
#endif

#define HAS_NO_KFIFO

struct kfifo {
	unsigned char *buffer;	/* the buffer holding the data */
	unsigned int size;	/* the size of the allocated buffer */
	unsigned int in;	/* data is added at offset (in % size) */
	unsigned int out;	/* data is extracted from off. (out % size) */
	spinlock_t *lock;	/* protects concurrent modifications */
};

extern struct kfifo *kfifo_init(unsigned char *buffer, unsigned int size,
				gfp_t gfp_mask, spinlock_t *lock);
extern struct kfifo *kfifo_alloc(unsigned int size, gfp_t gfp_mask,
				 spinlock_t *lock);
extern void kfifo_free(struct kfifo *fifo);
extern unsigned int __kfifo_put(struct kfifo *fifo,
				unsigned char *buffer, unsigned int len);
extern unsigned int __kfifo_get(struct kfifo *fifo,
				unsigned char *buffer, unsigned int len);

/**
 * kfifo_put - puts some data into the FIFO
 * @fifo: the fifo to be used.
 * @buffer: the data to be added.
 * @len: the length of the data to be added.
 *
 * This function copies at most @len bytes from the @buffer into
 * the FIFO depending on the free space, and returns the number of
 * bytes copied.
 */
static inline unsigned int
kfifo_put(struct kfifo *fifo, unsigned char *buffer, unsigned int len)
{
	unsigned long flags;
	unsigned int ret;

	spin_lock_irqsave(fifo->lock, flags);

	ret = __kfifo_put(fifo, buffer, len);

	spin_unlock_irqrestore(fifo->lock, flags);

	return ret;
}

/**
 * kfifo_get - gets some data from the FIFO
 * @fifo: the fifo to be used.
 * @buffer: where the data must be copied.
 * @len: the size of the destination buffer.
 *
 * This function copies at most @len bytes from the FIFO into the
 * @buffer and returns the number of copied bytes.
 */
static inline unsigned int
kfifo_get(struct kfifo *fifo, unsigned char *buffer, unsigned int len)
{
	unsigned long flags;
	unsigned int ret;

	spin_lock_irqsave(fifo->lock, flags);

	ret = __kfifo_get(fifo, buffer, len);

	/*
	 * optimization: if the FIFO is empty, set the indices to 0
	 * so we don't wrap the next time
	 */
	if (fifo->in == fifo->out)
		fifo->in = fifo->out = 0;

	spin_unlock_irqrestore(fifo->lock, flags);

	return ret;
}

/**
 * __kfifo_len - returns the number of bytes available in the FIFO, no locking version
 * @fifo: the fifo to be used.
 */
static inline unsigned int __kfifo_len(struct kfifo *fifo)
{
	return fifo->in - fifo->out;
}

/**
 * kfifo_len - returns the number of bytes available in the FIFO
 * @fifo: the fifo to be used.
 */
static inline unsigned int kfifo_len(struct kfifo *fifo)
{
	unsigned long flags;
	unsigned int ret;

	spin_lock_irqsave(fifo->lock, flags);

	ret = __kfifo_len(fifo);

	spin_unlock_irqrestore(fifo->lock, flags);

	return ret;
}

#else
#include <linux/kfifo.h>
#endif

static inline void kfifo_vfree(struct kfifo *fifo)
{
	vfree(fifo->buffer);
	kfree(fifo);
}

#endif /* __CI_EFRM_SYSDEP_LINUX_H__ */
