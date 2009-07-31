/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains abstraction of the buffer table on the NIC.
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

/*
** Might be worth keeping a bitmap of which entries are clear.  Then we
** wouldn't need to clear them all again when we free an allocation.
*/

#include <ci/efrm/debug.h>
#include <ci/driver/efab/hardware.h>
#include <ci/efrm/nic_table.h>
#include <ci/efrm/buffer_table.h>
#include <ci/efrm/buddy.h>

/*! Comment? */
struct efrm_buffer_table {
	spinlock_t lock;
	struct efrm_buddy_allocator buddy;
};

/* Efab buffer state. */
static struct efrm_buffer_table efrm_buffers;

int efrm_buffer_table_ctor(unsigned low, unsigned high)
{
	int log2_n_entries, rc, i;

	EFRM_ASSERT(high > 0);
	EFRM_ASSERT(low < high);

	EFRM_TRACE("%s: low=%u high=%u", __func__, low, high);
	EFRM_NOTICE("%s: low=%u high=%u", __func__, low, high);

	log2_n_entries = fls(high - 1);

	rc = efrm_buddy_ctor(&efrm_buffers.buddy, log2_n_entries);
	if (rc < 0) {
		EFRM_ERR("efrm_buffer_table_ctor: efrm_buddy_ctor(%d) "
			 "failed (%d)", log2_n_entries, rc);
		return rc;
	}
	for (i = 0; i < (1 << log2_n_entries); ++i) {
		rc = efrm_buddy_alloc(&efrm_buffers.buddy, 0);
		EFRM_ASSERT(rc >= 0);
		EFRM_ASSERT(rc < (1 << log2_n_entries));
	}
	for (i = low; i < (int) high; ++i)
		efrm_buddy_free(&efrm_buffers.buddy, i, 0);

	spin_lock_init(&efrm_buffers.lock);

	EFRM_TRACE("%s: done", __func__);

	return 0;
}

void efrm_buffer_table_dtor(void)
{
	/* ?? debug check that all allocations have been freed? */

	spin_lock_destroy(&efrm_buffers.lock);
	efrm_buddy_dtor(&efrm_buffers.buddy);

	EFRM_TRACE("%s: done", __func__);
}

/**********************************************************************/

int
efrm_buffer_table_alloc(unsigned order,
			struct efhw_buffer_table_allocation *a)
{
	irq_flags_t lock_flags;
	int rc;

	EFRM_ASSERT(&efrm_buffers.buddy);
	EFRM_ASSERT(a);

	/* Round up to multiple of two, as the buffer clear logic works in
	 * pairs when not in "full" mode. */
	order = max_t(unsigned, order, 1);

	spin_lock_irqsave(&efrm_buffers.lock, lock_flags);
	rc = efrm_buddy_alloc(&efrm_buffers.buddy, order);
	spin_unlock_irqrestore(&efrm_buffers.lock, lock_flags);

	if (rc < 0) {
		EFRM_ERR("efrm_buffer_table_alloc: failed (n=%ld) rc %d",
			 1ul << order, rc);
		return rc;
	}

	EFRM_TRACE("efrm_buffer_table_alloc: base=%d n=%ld",
		   rc, 1ul << order);
	a->order = order;
	a->base = (unsigned)rc;
	return 0;
}

void efrm_buffer_table_free(struct efhw_buffer_table_allocation *a)
{
	irq_flags_t lock_flags;
	struct efhw_nic *nic;
	int nic_i;

	EFRM_ASSERT(&efrm_buffers.buddy);
	EFRM_ASSERT(a);
	EFRM_ASSERT(a->base != -1);
	EFRM_ASSERT((unsigned long)a->base + (1ul << a->order) <=
		    efrm_buddy_size(&efrm_buffers.buddy));

	EFRM_TRACE("efrm_buffer_table_free: base=%d n=%ld",
		   a->base, (1ul << a->order));

	EFRM_FOR_EACH_NIC(nic_i, nic)
	    efhw_nic_buffer_table_clear(nic, a->base, 1ul << a->order);

	spin_lock_irqsave(&efrm_buffers.lock, lock_flags);
	efrm_buddy_free(&efrm_buffers.buddy, a->base, a->order);
	spin_unlock_irqrestore(&efrm_buffers.lock, lock_flags);

	EFRM_DO_DEBUG(a->base = a->order = -1);
}

/**********************************************************************/

void
efrm_buffer_table_set(struct efhw_buffer_table_allocation *a,
		      struct efhw_nic *nic,
		      unsigned i, dma_addr_t dma_addr, int owner)
{
	EFRM_ASSERT(a);
	EFRM_ASSERT(i < (unsigned)1 << a->order);

	efhw_nic_buffer_table_set(nic, dma_addr, EFHW_NIC_PAGE_SIZE,
				  0, owner, a->base + i);
}


int efrm_buffer_table_size(void)
{
	return efrm_buddy_size(&efrm_buffers.buddy);
}

/**********************************************************************/

int
efrm_page_register(struct efhw_nic *nic, dma_addr_t dma_addr, int owner,
		   efhw_buffer_addr_t *buf_addr_out)
{
	struct efhw_buffer_table_allocation alloc;
	int rc;

	rc = efrm_buffer_table_alloc(0, &alloc);
	if (rc == 0) {
		efrm_buffer_table_set(&alloc, nic, 0, dma_addr, owner);
		efrm_buffer_table_commit();
		*buf_addr_out = EFHW_BUFFER_ADDR(alloc.base, 0);
	}
	return rc;
}
EXPORT_SYMBOL(efrm_page_register);

void efrm_page_unregister(efhw_buffer_addr_t buf_addr)
{
	struct efhw_buffer_table_allocation alloc;

	alloc.order = 0;
	alloc.base = EFHW_BUFFER_PAGE(buf_addr);
	efrm_buffer_table_free(&alloc);
}
EXPORT_SYMBOL(efrm_page_unregister);

void efrm_buffer_table_commit(void)
{
	struct efhw_nic *nic;
	int nic_i;

	EFRM_FOR_EACH_NIC(nic_i, nic)
	    efhw_nic_buffer_table_commit(nic);
}
