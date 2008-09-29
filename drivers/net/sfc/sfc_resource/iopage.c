/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file provides Linux-specific implementation for iopage API used
 * from efhw library.
 *
 * Copyright 2005-2007: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Developed and maintained by Solarflare Communications:
 *                      <linux-xen-drivers@solarflare.com>
 *                      <onload-dev@solarflare.com>
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

#include <ci/driver/resource/linux_efhw_nic.h>
#include "kernel_compat.h"
#include <ci/efhw/common_sysdep.h> /* for dma_addr_t */

int efhw_iopage_alloc(struct efhw_nic *nic, struct efhw_iopage *p)
{
	struct linux_efhw_nic *lnic = linux_efhw_nic(nic);
	dma_addr_t handle;
	void *kva;

	kva = efrm_pci_alloc_consistent(lnic->pci_dev, PAGE_SIZE,
					&handle);
	if (kva == 0)
		return -ENOMEM;

	EFHW_ASSERT((handle & ~PAGE_MASK) == 0);

	memset((void *)kva, 0, PAGE_SIZE);
	efhw_page_init_from_va(&p->p, kva);

	p->dma_addr = handle;

	return 0;
}

void efhw_iopage_free(struct efhw_nic *nic, struct efhw_iopage *p)
{
	struct linux_efhw_nic *lnic = linux_efhw_nic(nic);
	EFHW_ASSERT(efhw_page_is_valid(&p->p));

	efrm_pci_free_consistent(lnic->pci_dev, PAGE_SIZE,
				 efhw_iopage_ptr(p), p->dma_addr);
}

int
efhw_iopages_alloc(struct efhw_nic *nic, struct efhw_iopages *p,
		   unsigned order)
{
	unsigned bytes = 1u << (order + PAGE_SHIFT);
	struct linux_efhw_nic *lnic = linux_efhw_nic(nic);
	dma_addr_t handle;
	caddr_t addr;
	int gfp_flag;

	/* Set __GFP_COMP if available to make reference counting work.
	 * This is recommended here:
	 *   http://www.forbiddenweb.org/viewtopic.php?id=83167&page=4#348331
	 */
	gfp_flag = ((in_atomic() ? GFP_ATOMIC : GFP_KERNEL) | __GFP_COMP);
	addr = efrm_dma_alloc_coherent(&lnic->pci_dev->dev, bytes, &handle,
				       gfp_flag);
	if (addr == NULL)
		return -ENOMEM;

	EFHW_ASSERT((handle & ~PAGE_MASK) == 0);

	p->order = order;
	p->dma_addr = handle;
	p->kva = addr;

	return 0;
}

void efhw_iopages_free(struct efhw_nic *nic, struct efhw_iopages *p)
{
	unsigned bytes = 1u << (p->order + PAGE_SHIFT);
	struct linux_efhw_nic *lnic = linux_efhw_nic(nic);

	efrm_dma_free_coherent(&lnic->pci_dev->dev, bytes,
			       (void *)p->kva, p->dma_addr);
}
