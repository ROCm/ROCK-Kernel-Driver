/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file provides efhw_page_t and efhw_iopage_t for Linux kernel.
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
#include <ci/efhw/debug.h>

/*--------------------------------------------------------------------
 *
 * efhw_page_t: A single page of memory.  Directly mapped in the driver,
 * and can be mapped to userlevel.
 *
 *--------------------------------------------------------------------*/

typedef struct {
	unsigned long kva;
} efhw_page_t;

static inline int efhw_page_alloc(efhw_page_t *p)
{
	p->kva = __get_free_page(in_interrupt()? GFP_ATOMIC : GFP_KERNEL);
	return p->kva ? 0 : -ENOMEM;
}

static inline int efhw_page_alloc_zeroed(efhw_page_t *p)
{
	p->kva = get_zeroed_page(in_interrupt()? GFP_ATOMIC : GFP_KERNEL);
	return p->kva ? 0 : -ENOMEM;
}

static inline void efhw_page_free(efhw_page_t *p)
{
	free_page(p->kva);
	EFHW_DO_DEBUG(memset(p, 0, sizeof(*p)));
}

static inline char *efhw_page_ptr(efhw_page_t *p)
{
	return (char *)p->kva;
}

static inline unsigned efhw_page_pfn(efhw_page_t *p)
{
	return (unsigned)(__pa(p->kva) >> PAGE_SHIFT);
}

static inline void efhw_page_mark_invalid(efhw_page_t *p)
{
	p->kva = 0;
}

static inline int efhw_page_is_valid(efhw_page_t *p)
{
	return p->kva != 0;
}

static inline void efhw_page_init_from_va(efhw_page_t *p, void *va)
{
	p->kva = (unsigned long)va;
}

/*--------------------------------------------------------------------
 *
 * efhw_iopage_t: A single page of memory.  Directly mapped in the driver,
 * and can be mapped to userlevel.  Can also be accessed by the NIC.
 *
 *--------------------------------------------------------------------*/

typedef struct {
	efhw_page_t p;
	dma_addr_t dma_addr;
} efhw_iopage_t;

static inline dma_addr_t efhw_iopage_dma_addr(efhw_iopage_t *p)
{
	return p->dma_addr;
}

#define efhw_iopage_ptr(iop)		efhw_page_ptr(&(iop)->p)
#define efhw_iopage_pfn(iop)		efhw_page_pfn(&(iop)->p)
#define efhw_iopage_mark_invalid(iop)	efhw_page_mark_invalid(&(iop)->p)
#define efhw_iopage_is_valid(iop)	efhw_page_is_valid(&(iop)->p)

/*--------------------------------------------------------------------
 *
 * efhw_iopages_t: A set of pages that are contiguous in physical memory.
 * Directly mapped in the driver, and can be mapped to userlevel.  Can also
 * be accessed by the NIC.
 *
 * NB. The O/S may be unwilling to allocate many, or even any of these.  So
 * only use this type where the NIC really needs a physically contiguous
 * buffer.
 *
 *--------------------------------------------------------------------*/

typedef struct {
	caddr_t kva;
	unsigned order;
	dma_addr_t dma_addr;
} efhw_iopages_t;

static inline caddr_t efhw_iopages_ptr(efhw_iopages_t *p)
{
	return p->kva;
}

static inline unsigned efhw_iopages_pfn(efhw_iopages_t *p)
{
	return (unsigned)(__pa(p->kva) >> PAGE_SHIFT);
}

static inline dma_addr_t efhw_iopages_dma_addr(efhw_iopages_t *p)
{
	return p->dma_addr;
}

static inline unsigned efhw_iopages_size(efhw_iopages_t *p)
{
	return 1u << (p->order + PAGE_SHIFT);
}

/* efhw_iopage_t <-> efhw_iopages_t conversions for handling physically
 * contiguous allocations in iobufsets for iSCSI.  This allows the
 * essential information about contiguous allocations from
 * efhw_iopages_alloc() to be saved away in the efhw_iopage_t array in an
 * iobufset.  (Changing the iobufset resource to use a union type would
 * involve a lot of code changes, and make the iobufset's metadata larger
 * which could be bad as it's supposed to fit into a single page on some
 * platforms.)
 */
static inline void
efhw_iopage_init_from_iopages(efhw_iopage_t *iopage,
			    efhw_iopages_t *iopages, unsigned pageno)
{
	iopage->p.kva = ((unsigned long)efhw_iopages_ptr(iopages))
	    + (pageno * PAGE_SIZE);
	iopage->dma_addr = efhw_iopages_dma_addr(iopages) +
	    (pageno * PAGE_SIZE);
}

static inline void
efhw_iopages_init_from_iopage(efhw_iopages_t *iopages,
			    efhw_iopage_t *iopage, unsigned order)
{
	iopages->kva = (caddr_t) efhw_iopage_ptr(iopage);
	EFHW_ASSERT(iopages->kva);
	iopages->order = order;
	iopages->dma_addr = efhw_iopage_dma_addr(iopage);
}

#endif /* __CI_EFHW_IOPAGE_LINUX_H__ */
