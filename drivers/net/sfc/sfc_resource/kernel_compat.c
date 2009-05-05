/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file provides compatibility layer for various Linux kernel versions
 * (starting from 2.6.9 RHEL kernel).
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

#define IN_KERNEL_COMPAT_C
#include <linux/types.h>
#include <ci/efrm/debug.h>
#include "kernel_compat.h"

/* Set this to 1 to enable very basic counting of iopage(s) allocations, then
 * call dump_iopage_counts() to show the number of current allocations of
 * orders 0-7.
 */
#define EFRM_IOPAGE_COUNTS_ENABLED 0


/****************************************************************************
 *
 * allocate a buffer suitable for DMA to/from the NIC
 *
 ****************************************************************************/

#if EFRM_IOPAGE_COUNTS_ENABLED

static int iopage_counts[8];

void dump_iopage_counts(void)
{
	EFRM_NOTICE("iopage counts: %d %d %d %d %d %d %d %d", iopage_counts[0],
		    iopage_counts[1], iopage_counts[2], iopage_counts[3],
		    iopage_counts[4], iopage_counts[5], iopage_counts[6],
		    iopage_counts[7]);
}

#endif



/*********** pci_alloc_consistent / pci_free_consistent ***********/

void *efrm_dma_alloc_coherent(struct device *dev, size_t size,
			      dma_addr_t *dma_addr, int flag)
{
	void *ptr;
	unsigned order;

	order = __ffs(size/PAGE_SIZE);
	EFRM_ASSERT(size == (PAGE_SIZE<<order));

	/* Can't take a spinlock here since the allocation can
	 * block. */
	ptr = dma_alloc_coherent(dev, size, dma_addr, flag);
	if (ptr == NULL)
		return ptr;

#if EFRM_IOPAGE_COUNTS_ENABLED
	if (order < 8)
		iopage_counts[order]++;
	else
		EFRM_ERR("Huge iopages alloc (order=%d) ??? (not counted)",
			 order);
#endif

	return ptr;
}

void efrm_dma_free_coherent(struct device *dev, size_t size,
			    void *ptr, dma_addr_t dma_addr)
{
	unsigned order;

	order = __ffs(size/PAGE_SIZE);
	EFRM_ASSERT(size == (PAGE_SIZE<<order));

#if EFRM_IOPAGE_COUNTS_ENABLED
	if (order < 8)
		--iopage_counts[order];
	else
		EFRM_ERR("Huge iopages free (order=%d) ??? (not counted)",
			 order);
#endif

	dma_free_coherent(dev, size, ptr, dma_addr);
}
