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

#ifndef DRIVER_LINUX_RESOURCE_KERNEL_COMPAT_H
#define DRIVER_LINUX_RESOURCE_KERNEL_COMPAT_H

#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/io.h>
#include <linux/pci.h>

/********* pci_map_*() ********************/

extern void *efrm_dma_alloc_coherent(struct device *dev, size_t size,
				     dma_addr_t *dma_addr, int flag);

extern void efrm_dma_free_coherent(struct device *dev, size_t size,
				   void *ptr, dma_addr_t dma_addr);

static inline void *efrm_pci_alloc_consistent(struct pci_dev *hwdev,
					      size_t size,
					      dma_addr_t *dma_addr)
{
	return efrm_dma_alloc_coherent(&hwdev->dev, size, dma_addr,
				       GFP_ATOMIC);
}

static inline void efrm_pci_free_consistent(struct pci_dev *hwdev, size_t size,
					    void *ptr, dma_addr_t dma_addr)
{
	efrm_dma_free_coherent(&hwdev->dev, size, ptr, dma_addr);
}


#endif /* DRIVER_LINUX_RESOURCE_KERNEL_COMPAT_H */
