/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file provides struct efhw_page and struct efhw_iopage for Linux
 * kernel.
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

#ifndef __CI_EFHW_IOPAGE_LINUX_H__
#define __CI_EFHW_IOPAGE_LINUX_H__

#include <linux/gfp.h>
#include <linux/hardirq.h>
#include <linux/errno.h>
#include <ci/efhw/debug.h>

/*--------------------------------------------------------------------
 *
 * struct efhw_page: A single page of memory.  Directly mapped in the
 * driver, and can be mapped to userlevel.
 *
 *--------------------------------------------------------------------*/

struct efhw_page {
	unsigned long kva;
};

static inline int efhw_page_alloc(struct efhw_page *p)
{
	p->kva = __get_free_page(in_interrupt()? GFP_ATOMIC : GFP_KERNEL);
	return p->kva ? 0 : -ENOMEM;
}

static inline int efhw_page_alloc_zeroed(struct efhw_page *p)
{
	p->kva = get_zeroed_page(in_interrupt()? GFP_ATOMIC : GFP_KERNEL);
	return p->kva ? 0 : -ENOMEM;
}

static inline void efhw_page_free(struct efhw_page *p)
{
	free_page(p->kva);
	EFHW_DO_DEBUG(memset(p, 0, sizeof(*p)));
}

static inline char *efhw_page_ptr(struct efhw_page *p)
{
	return (char *)p->kva;
}

static inline unsigned efhw_page_pfn(struct efhw_page *p)
{
	return (unsigned)(__pa(p->kva) >> PAGE_SHIFT);
}

static inline void efhw_page_mark_invalid(struct efhw_page *p)
{
	p->kva = 0;
}

static inline int efhw_page_is_valid(struct efhw_page *p)
{
	return p->kva != 0;
}

static inline void efhw_page_init_from_va(struct efhw_page *p, void *va)
{
	p->kva = (unsigned long)va;
}

/*--------------------------------------------------------------------
 *
 * struct efhw_iopage: A single page of memory.  Directly mapped in the driver,
 * and can be mapped to userlevel.  Can also be accessed by the NIC.
 *
 *--------------------------------------------------------------------*/

struct efhw_iopage {
	struct efhw_page p;
	dma_addr_t dma_addr;
};

static inline dma_addr_t efhw_iopage_dma_addr(struct efhw_iopage *p)
{
	return p->dma_addr;
}

#define efhw_iopage_ptr(iop)		efhw_page_ptr(&(iop)->p)
#define efhw_iopage_pfn(iop)		efhw_page_pfn(&(iop)->p)
#define efhw_iopage_mark_invalid(iop)	efhw_page_mark_invalid(&(iop)->p)
#define efhw_iopage_is_valid(iop)	efhw_page_is_valid(&(iop)->p)

/*--------------------------------------------------------------------
 *
 * struct efhw_iopages: A set of pages that are contiguous in physical
 * memory.  Directly mapped in the driver, and can be mapped to userlevel.
 * Can also be accessed by the NIC.
 *
 * NB. The O/S may be unwilling to allocate many, or even any of these.  So
 * only use this type where the NIC really needs a physically contiguous
 * buffer.
 *
 *--------------------------------------------------------------------*/

struct efhw_iopages {
	caddr_t kva;
	unsigned order;
	dma_addr_t dma_addr;
};

static inline caddr_t efhw_iopages_ptr(struct efhw_iopages *p)
{
	return p->kva;
}

static inline unsigned efhw_iopages_pfn(struct efhw_iopages *p)
{
	return (unsigned)(__pa(p->kva) >> PAGE_SHIFT);
}

static inline dma_addr_t efhw_iopages_dma_addr(struct efhw_iopages *p)
{
	return p->dma_addr;
}

static inline unsigned efhw_iopages_size(struct efhw_iopages *p)
{
	return 1u << (p->order + PAGE_SHIFT);
}

/* struct efhw_iopage <-> struct efhw_iopages conversions for handling
 * physically contiguous allocations in iobufsets for iSCSI.  This allows
 * the essential information about contiguous allocations from
 * efhw_iopages_alloc() to be saved away in the struct efhw_iopage array in
 * an iobufset.  (Changing the iobufset resource to use a union type would
 * involve a lot of code changes, and make the iobufset's metadata larger
 * which could be bad as it's supposed to fit into a single page on some
 * platforms.)
 */
static inline void
efhw_iopage_init_from_iopages(struct efhw_iopage *iopage,
			      struct efhw_iopages *iopages, unsigned pageno)
{
	iopage->p.kva = ((unsigned long)efhw_iopages_ptr(iopages))
	    + (pageno * PAGE_SIZE);
	iopage->dma_addr = efhw_iopages_dma_addr(iopages) +
	    (pageno * PAGE_SIZE);
}

static inline void
efhw_iopages_init_from_iopage(struct efhw_iopages *iopages,
			      struct efhw_iopage *iopage, unsigned order)
{
	iopages->kva = (caddr_t) efhw_iopage_ptr(iopage);
	EFHW_ASSERT(iopages->kva);
	iopages->order = order;
	iopages->dma_addr = efhw_iopage_dma_addr(iopage);
}

#endif /* __CI_EFHW_IOPAGE_LINUX_H__ */
