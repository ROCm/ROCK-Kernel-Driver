/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains non-contiguous I/O buffers support.
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

#include <ci/efrm/nic_table.h>
#include <ci/efhw/iopage.h>
#include <ci/driver/efab/hardware.h>
#include <ci/efrm/private.h>
#include <ci/efrm/iobufset.h>
#include <ci/efrm/vi_resource_manager.h>
#include <ci/efrm/buffer_table.h>
#include <ci/efrm/efrm_client.h>
#include "efrm_internal.h"


#define EFRM_IOBUFSET_MAX_NUM_INSTANCES 0x00010000

struct iobufset_resource_manager {
	struct efrm_resource_manager rm;
	struct kfifo *free_ids;
};

struct iobufset_resource_manager *efrm_iobufset_manager;

#define iobsrs(rs1)  iobufset_resource(rs1)

/* Returns size of iobufset resource data structure. */
static inline size_t iobsrs_size(int n_pages)
{
	return offsetof(struct iobufset_resource, bufs) +
	    n_pages * sizeof(struct efhw_iopage);
}

void efrm_iobufset_resource_free(struct iobufset_resource *rs)
{
	unsigned int i;
	int id;

	EFRM_RESOURCE_ASSERT_VALID(&rs->rs, 1);

	if (!rs->linked && rs->buf_tbl_alloc.base != (unsigned) -1)
		efrm_buffer_table_free(&rs->buf_tbl_alloc);

	/* see comment on call to efhw_iopage_alloc in the alloc routine above
	   for discussion on use of efrm_nic_tablep->a_nic here */
	EFRM_ASSERT(efrm_nic_tablep->a_nic);
	if (rs->linked) {
		/* Nothing to do. */
	} else if (rs->chunk_order == 0) {
		for (i = 0; i < rs->n_bufs; ++i)
			efhw_iopage_free(efrm_nic_tablep->a_nic, &rs->bufs[i]);
	} else {
		/* it is important that this is executed in increasing page
		 * order because some implementations of
		 * efhw_iopages_init_from_iopage() assume this */
		for (i = 0; i < rs->n_bufs;
		     i += rs->pages_per_contiguous_chunk) {
			struct efhw_iopages iopages;
			efhw_iopages_init_from_iopage(&iopages, &rs->bufs[i],
						    rs->chunk_order);
			efhw_iopages_free(efrm_nic_tablep->a_nic, &iopages);
		}
	}

	/* free the instance number */
	id = EFRM_RESOURCE_INSTANCE(rs->rs.rs_handle);
	EFRM_VERIFY_EQ(kfifo_put(efrm_iobufset_manager->free_ids,
				 (unsigned char *)&id, sizeof(id)), sizeof(id));

	efrm_vi_resource_release(rs->evq);
	if (rs->linked)
		efrm_iobufset_resource_release(rs->linked);

	efrm_client_put(rs->rs.rs_client);
	if (iobsrs_size(rs->n_bufs) < PAGE_SIZE) {
		EFRM_DO_DEBUG(memset(rs, 0, sizeof(*rs)));
		kfree(rs);
	} else {
		EFRM_DO_DEBUG(memset(rs, 0, sizeof(*rs)));
		vfree(rs);
	}
}
EXPORT_SYMBOL(efrm_iobufset_resource_free);


void efrm_iobufset_resource_release(struct iobufset_resource *iobrs)
{
	if (__efrm_resource_release(&iobrs->rs))
		efrm_iobufset_resource_free(iobrs);
}
EXPORT_SYMBOL(efrm_iobufset_resource_release);



int
efrm_iobufset_resource_alloc(int32_t n_pages,
			     int32_t pages_per_contiguous_chunk,
			     struct vi_resource *vi_evq,
			     struct iobufset_resource *linked,
			     bool phys_addr_mode,
			     struct iobufset_resource **iobrs_out)
{
	struct iobufset_resource *iobrs;
	int rc, instance, object_size;
	unsigned int i;

	EFRM_ASSERT(iobrs_out);
	EFRM_ASSERT(efrm_iobufset_manager);
	EFRM_RESOURCE_MANAGER_ASSERT_VALID(&efrm_iobufset_manager->rm);
	EFRM_RESOURCE_ASSERT_VALID(&vi_evq->rs, 0);
	EFRM_ASSERT(EFRM_RESOURCE_TYPE(vi_evq->rs.rs_handle) ==
		    EFRM_RESOURCE_VI);
	EFRM_ASSERT(efrm_nic_tablep->a_nic);

	if (linked) {
		/* This resource will share properties and memory with
		 * another.  Only difference is that we'll program it into
		 * the buffer table of another nic.
		 */
		n_pages = linked->n_bufs;
		pages_per_contiguous_chunk = linked->pages_per_contiguous_chunk;
		phys_addr_mode = linked->buf_tbl_alloc.base == (unsigned) -1;
	}

	/* allocate the resource data structure. */
	object_size = iobsrs_size(n_pages);
	if (object_size < PAGE_SIZE) {
		/* this should be OK from a tasklet */
		/* Necessary to do atomic alloc() as this
		   can be called from a weird-ass iSCSI context that is
		   !in_interrupt but is in_atomic - See BUG3163 */
		iobrs = kmalloc(object_size, GFP_ATOMIC);
	} else {		/* can't do this within a tasklet */
#ifndef NDEBUG
		if (in_interrupt() || in_atomic()) {
			EFRM_ERR("%s(): alloc->u.iobufset.in_n_pages=%d",
				 __func__, n_pages);
			EFRM_ASSERT(!in_interrupt());
			EFRM_ASSERT(!in_atomic());
		}
#endif
		iobrs = (struct iobufset_resource *) vmalloc(object_size);
	}
	if (iobrs == NULL) {
		EFRM_WARN("%s: failed to allocate container", __func__);
		rc = -ENOMEM;
		goto fail1;
	}

	/* Allocate an instance number. */
	rc = kfifo_get(efrm_iobufset_manager->free_ids,
		       (unsigned char *)&instance, sizeof(instance));
	if (rc != sizeof(instance)) {
		EFRM_WARN("%s: out of instances", __func__);
		EFRM_ASSERT(rc == 0);
		rc = -EBUSY;
		goto fail3;
	}

	efrm_resource_init(&iobrs->rs, EFRM_RESOURCE_IOBUFSET, instance);

	iobrs->evq = vi_evq;
	iobrs->linked = linked;
	iobrs->n_bufs = n_pages;
	iobrs->pages_per_contiguous_chunk = pages_per_contiguous_chunk;
	iobrs->chunk_order = fls(iobrs->pages_per_contiguous_chunk - 1);
	iobrs->buf_tbl_alloc.base = (unsigned) -1;

	EFRM_TRACE("%s: " EFRM_RESOURCE_FMT " %u pages", __func__,
		   EFRM_RESOURCE_PRI_ARG(iobrs->rs.rs_handle), iobrs->n_bufs);

	/* Allocate the iobuffers. */
	if (linked) {
		memcpy(iobrs->bufs, linked->bufs,
		       iobrs->n_bufs * sizeof(iobrs->bufs[0]));
	} else if (iobrs->chunk_order == 0) {
		memset(iobrs->bufs, 0, iobrs->n_bufs * sizeof(iobrs->bufs[0]));
		for (i = 0; i < iobrs->n_bufs; ++i) {
			/* due to bug2426 we have to specifiy a NIC when
			 * allocating a DMAable page, which is a bit messy.
			 * For now we assume that if the page is suitable
			 * (e.g. DMAable) by one nic (efrm_nic_tablep->a_nic),
			 * it is suitable for all NICs.
			 * XXX I bet that breaks in Solaris.
			 */
			rc = efhw_iopage_alloc(efrm_nic_tablep->a_nic,
					     &iobrs->bufs[i]);
			if (rc < 0) {
				EFRM_WARN("%s: failed (rc %d) to allocate "
					  "page (i=%u)", __func__, rc, i);
				goto fail4;
			}
		}
	} else {
		struct efhw_iopages iopages;
		unsigned j;

		memset(iobrs->bufs, 0, iobrs->n_bufs * sizeof(iobrs->bufs[0]));
		for (i = 0; i < iobrs->n_bufs;
		     i += iobrs->pages_per_contiguous_chunk) {
			rc = efhw_iopages_alloc(efrm_nic_tablep->a_nic,
						&iopages, iobrs->chunk_order);
			if (rc < 0) {
				EFRM_WARN("%s: failed (rc %d) to allocate "
					  "pages (i=%u order %d)",
					  __func__, rc, i,
					  iobrs->chunk_order);
				goto fail4;
			}
			for (j = 0; j < iobrs->pages_per_contiguous_chunk;
			     j++) {
				/* some implementation of
				 * efhw_iopage_init_from_iopages() rely on
				 * this function being called for
				 * _all_ pages in the chunk */
				efhw_iopage_init_from_iopages(
							&iobrs->bufs[i + j],
							&iopages, j);
			}
		}
	}

	if (!phys_addr_mode) {
		unsigned owner_id = EFAB_VI_RESOURCE_INSTANCE(iobrs->evq);

		if (!linked) {
			/* Allocate space in the NIC's buffer table. */
			rc = efrm_buffer_table_alloc(fls(iobrs->n_bufs - 1),
						     &iobrs->buf_tbl_alloc);
			if (rc < 0) {
				EFRM_WARN("%s: failed (%d) to alloc %d buffer "
					  "table entries", __func__, rc,
					  iobrs->n_bufs);
				goto fail5;
			}
			EFRM_ASSERT(((unsigned)1 << iobrs->buf_tbl_alloc.order)
				    >= (unsigned) iobrs->n_bufs);
		} else {
			iobrs->buf_tbl_alloc = linked->buf_tbl_alloc;
		}

		/* Initialise the buffer table entries. */
		for (i = 0; i < iobrs->n_bufs; ++i) {
			/*\ ?? \TODO burst them! */
			efrm_buffer_table_set(&iobrs->buf_tbl_alloc,
					      vi_evq->rs.rs_client->nic,
					      i,
					      efhw_iopage_dma_addr(&iobrs->
								   bufs[i]),
					      owner_id);
		}
		efrm_buffer_table_commit();
	}

	EFRM_TRACE("%s: " EFRM_RESOURCE_FMT " %d pages @ "
		   EFHW_BUFFER_ADDR_FMT, __func__,
		   EFRM_RESOURCE_PRI_ARG(iobrs->rs.rs_handle),
		   iobrs->n_bufs, EFHW_BUFFER_ADDR(iobrs->buf_tbl_alloc.base,
						   0));
	efrm_resource_ref(&iobrs->evq->rs);
	if (linked != NULL)
		efrm_resource_ref(&linked->rs);
	efrm_client_add_resource(vi_evq->rs.rs_client, &iobrs->rs);
	*iobrs_out = iobrs;
	return 0;

fail5:
	i = iobrs->n_bufs;
fail4:
	/* see comment on call to efhw_iopage_alloc above for a discussion
	 * on use of efrm_nic_tablep->a_nic here */
	if (linked) {
		/* Nothing to do. */
	} else if (iobrs->chunk_order == 0) {
		while (i--) {
			struct efhw_iopage *page = &iobrs->bufs[i];
			efhw_iopage_free(efrm_nic_tablep->a_nic, page);
		}
	} else {
		unsigned int j;
		for (j = 0; j < i; j += iobrs->pages_per_contiguous_chunk) {
			struct efhw_iopages iopages;

			EFRM_ASSERT(j % iobrs->pages_per_contiguous_chunk
				    == 0);
			/* it is important that this is executed in increasing
			 * page order because some implementations of
			 * efhw_iopages_init_from_iopage() assume this */
			efhw_iopages_init_from_iopage(&iopages,
						      &iobrs->bufs[j],
						      iobrs->chunk_order);
			efhw_iopages_free(efrm_nic_tablep->a_nic, &iopages);
		}
	}
fail3:
	if (object_size < PAGE_SIZE)
		kfree(iobrs);
	else
		vfree(iobrs);
fail1:
	return rc;
}
EXPORT_SYMBOL(efrm_iobufset_resource_alloc);

static void iobufset_rm_dtor(struct efrm_resource_manager *rm)
{
	EFRM_ASSERT(&efrm_iobufset_manager->rm == rm);
	kfifo_vfree(efrm_iobufset_manager->free_ids);
}

int
efrm_create_iobufset_resource_manager(struct efrm_resource_manager **rm_out)
{
	int rc, max;

	EFRM_ASSERT(rm_out);

	efrm_iobufset_manager =
	    kmalloc(sizeof(*efrm_iobufset_manager), GFP_KERNEL);
	if (efrm_iobufset_manager == 0)
		return -ENOMEM;
	memset(efrm_iobufset_manager, 0, sizeof(*efrm_iobufset_manager));

	/*
	 * Bug 1145, 1370: We need to set initial size of both the resource
	 * table and instance id table so they never need to grow as we
	 * want to be allocate new iobufset at tasklet time. Lets make
	 * a pessimistic guess at maximum number of iobufsets possible.
	 * Could be less because
	 *   - jumbo frames have same no of packets per iobufset BUT more
	 *     pages per buffer
	 *   - buffer table entries used independently of iobufsets by
	 *     sendfile
	 *
	 * Based on TCP/IP stack setting of PKTS_PER_SET_S=5 ...
	 *  - can't use this define here as it breaks the layering.
	 */
#define MIN_PAGES_PER_IOBUFSET  (1 << 4)

	max = efrm_buffer_table_size() / MIN_PAGES_PER_IOBUFSET;
	max = min_t(int, max, EFRM_IOBUFSET_MAX_NUM_INSTANCES);

	/* HACK: There currently exists an option to allocate buffers that
	 * are not programmed into the buffer table, so the max number is
	 * not limited by the buffer table size.  I'm hoping this usage
	 * will go away eventually.
	 */
	max = 32768;

	rc = efrm_kfifo_id_ctor(&efrm_iobufset_manager->free_ids,
				0, max, &efrm_iobufset_manager->rm.rm_lock);
	if (rc != 0)
		goto fail1;

	rc = efrm_resource_manager_ctor(&efrm_iobufset_manager->rm,
					iobufset_rm_dtor, "IOBUFSET",
					EFRM_RESOURCE_IOBUFSET);
	if (rc < 0)
		goto fail2;

	*rm_out = &efrm_iobufset_manager->rm;
	return 0;

fail2:
	kfifo_vfree(efrm_iobufset_manager->free_ids);
fail1:
	EFRM_DO_DEBUG(memset(efrm_iobufset_manager, 0,
			     sizeof(*efrm_iobufset_manager)));
	kfree(efrm_iobufset_manager);
	return rc;
}
