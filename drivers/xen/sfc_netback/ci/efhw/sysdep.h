/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file provides version-independent Linux kernel API for efhw library.
 * Only kernels >=2.6.9 are supported.
 *
 * Copyright 2005-2007: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
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

#ifndef __CI_EFHW_SYSDEP_LINUX_H__
#define __CI_EFHW_SYSDEP_LINUX_H__

#include <linux/version.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/if_ether.h>

#include <linux/netdevice.h> /* necessary for etherdevice.h on some kernels */
#include <linux/etherdevice.h>

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,21)
static inline int is_local_ether_addr(const u8 *addr)
{
	return (0x02 & addr[0]);
}
#endif

typedef unsigned long irq_flags_t;

#define spin_lock_destroy(l_)  do {} while (0)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
#define HAS_NET_NAMESPACE
#endif

/* Funny, but linux has round_up for x86 only, defined in
 * x86-specific header */
#ifndef round_up
#define round_up(x, y) (((x) + (y) - 1) & ~((y)-1))
#endif

#endif /* __CI_EFHW_SYSDEP_LINUX_H__ */
