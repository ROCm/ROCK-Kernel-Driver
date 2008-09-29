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
#include <linux/log2.h>
#include <linux/kfifo.h>


/********************************************************************
 *
 * List API
 *
 ********************************************************************/

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
 * Kfifo API
 *
 ********************************************************************/

static inline void kfifo_vfree(struct kfifo *fifo)
{
	vfree(fifo->buffer);
	kfree(fifo);
}

#endif /* __CI_EFRM_SYSDEP_LINUX_H__ */
