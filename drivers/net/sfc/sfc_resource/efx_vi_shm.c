/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file provides implementation of EFX VI API, used from Xen
 * acceleration driver.
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

#include "linux_resource_internal.h"
#include <ci/efrm/vi_resource_manager.h>
#include <ci/driver/resource/efx_vi.h>
#include <ci/efrm/filter.h>
#include <ci/efrm/buffer_table.h>
#include <ci/efrm/efrm_client.h>
#include <linux/pci.h>
#include "kernel_compat.h"

#if EFX_VI_STATIC_FILTERS
struct filter_list_t {
	struct filter_list_t *next;
	struct filter_resource *fres;
};
#endif

struct efx_vi_state {
	struct vi_resource *vi_res;

	int ifindex;
	struct efrm_client *efrm_client;
	struct efhw_nic *nic;

	void (*callback_fn)(void *arg, int is_timeout);
	void *callback_arg;

	struct completion flush_completion;

#if EFX_VI_STATIC_FILTERS
	struct filter_list_t fres[EFX_VI_STATIC_FILTERS];
	struct filter_list_t *free_fres;
	struct filter_list_t *used_fres;
#endif
};

static void efx_vi_flush_complete(void *state_void)
{
	struct efx_vi_state *state = (struct efx_vi_state *)state_void;

	complete(&state->flush_completion);
}

static inline int alloc_ep(struct efx_vi_state *state)
{
	int rc;

	rc = efrm_vi_resource_alloc(state->efrm_client, NULL, EFHW_VI_JUMBO_EN,
				    efx_vi_eventq_size,
				    FALCON_DMA_Q_DEFAULT_TX_SIZE,
				    FALCON_DMA_Q_DEFAULT_RX_SIZE,
				    0, 0, &state->vi_res, NULL, NULL, NULL,
				    NULL);
	if (rc < 0) {
		EFRM_ERR("%s: ERROR efrm_vi_resource_alloc error %d",
			 __func__, rc);
		return rc;
	}

	efrm_vi_register_flush_callback(state->vi_res, &efx_vi_flush_complete,
					(void *)state);

	return 0;
}

static int free_ep(struct efx_vi_state *efx_state)
{
	efrm_vi_resource_release(efx_state->vi_res);

	return 0;
}

#if EFX_VI_STATIC_FILTERS
static int efx_vi_alloc_static_filters(struct efx_vi_state *efx_state)
{
	int i;
	int rc;

	efx_state->free_fres = efx_state->used_fres = NULL;

	for (i = 0; i < EFX_VI_STATIC_FILTERS; i++) {
		rc = efrm_filter_resource_alloc(efx_state->vi_res,
						&efx_state->fres[i].fres);
		if (rc < 0) {
			EFRM_ERR("%s: efrm_filter_resource_alloc failed: %d",
			     __func__, rc);
			while (i > 0) {
				i--;
				efrm_filter_resource_release(efx_state->
							     fres[i].fres);
			}
			efx_state->free_fres = NULL;
			return rc;
		}
		efx_state->fres[i].next = efx_state->free_fres;
		efx_state->free_fres = &efx_state->fres[i];
	}

	return 0;
}
#endif

int efx_vi_alloc(struct efx_vi_state **vih_out, int ifindex)
{
	struct efx_vi_state *efx_state;
	int rc;

	efx_state = kmalloc(sizeof(struct efx_vi_state), GFP_KERNEL);

	if (!efx_state) {
		EFRM_ERR("%s: failed to allocate memory for efx_vi_state",
			 __func__);
		rc = -ENOMEM;
		goto fail;
	}

	efx_state->ifindex = ifindex;
	rc = efrm_client_get(ifindex, NULL, NULL, &efx_state->efrm_client);
	if (rc < 0) {
		EFRM_ERR("%s: efrm_client_get(%d) failed: %d", __func__,
			 ifindex, rc);
		rc = -ENODEV;
		goto fail_no_ifindex;
	}
	efx_state->nic = efrm_client_get_nic(efx_state->efrm_client);

	init_completion(&efx_state->flush_completion);

	/* basically allocate_pt_endpoint() */
	rc = alloc_ep(efx_state);
	if (rc) {
		EFRM_ERR("%s: alloc_ep failed: %d", __func__, rc);
		goto fail_no_pt;
	}
#if EFX_VI_STATIC_FILTERS
	/* Statically allocate a set of filter resources - removes the
	   restriction on not being able to use efx_vi_filter() from
	   in_atomic() */
	rc = efx_vi_alloc_static_filters(efx_state);
	if (rc)
		goto fail_no_filters;
#endif

	*vih_out = efx_state;

	return 0;
#if EFX_VI_STATIC_FILTERS
fail_no_filters:
	free_ep(efx_state);
#endif
fail_no_pt:
	efrm_client_put(efx_state->efrm_client);
fail_no_ifindex:
	kfree(efx_state);
fail:
	return rc;
}
EXPORT_SYMBOL(efx_vi_alloc);

void efx_vi_free(struct efx_vi_state *vih)
{
	struct efx_vi_state *efx_state = vih;

	/* TODO flush dma channels, init dma queues?.  See ef_free_vnic() */
#if EFX_VI_STATIC_FILTERS
	int i;

	for (i = 0; i < EFX_VI_STATIC_FILTERS; i++)
		efrm_filter_resource_release(efx_state->fres[i].fres);
#endif

	if (efx_state->vi_res)
		free_ep(efx_state);

	efrm_client_put(efx_state->efrm_client);

	kfree(efx_state);
}
EXPORT_SYMBOL(efx_vi_free);

void efx_vi_reset(struct efx_vi_state *vih)
{
	struct efx_vi_state *efx_state = vih;

	efrm_pt_flush(efx_state->vi_res);

	while (wait_for_completion_timeout(&efx_state->flush_completion, HZ)
	       == 0)
		efrm_vi_resource_flush_retry(efx_state->vi_res);

	/* Bosch the eventq */
	efrm_eventq_reset(efx_state->vi_res);
	return;
}
EXPORT_SYMBOL(efx_vi_reset);

static void
efx_vi_eventq_callback(void *context, int is_timeout, struct efhw_nic *nic)
{
	struct efx_vi_state *efx_state = (struct efx_vi_state *)context;

	EFRM_ASSERT(efx_state->callback_fn);

	return efx_state->callback_fn(efx_state->callback_arg, is_timeout);
}

int
efx_vi_eventq_register_callback(struct efx_vi_state *vih,
			void (*callback)(void *context, int is_timeout),
			void *context)
{
	struct efx_vi_state *efx_state = vih;

	efx_state->callback_fn = callback;
	efx_state->callback_arg = context;

	/* Register the eventq timeout event callback */
	efrm_eventq_register_callback(efx_state->vi_res,
				      efx_vi_eventq_callback, efx_state);

	return 0;
}
EXPORT_SYMBOL(efx_vi_eventq_register_callback);

int efx_vi_eventq_kill_callback(struct efx_vi_state *vih)
{
	struct efx_vi_state *efx_state = vih;

	if (efx_state->vi_res->evq_callback_fn)
		efrm_eventq_kill_callback(efx_state->vi_res);

	efx_state->callback_fn = NULL;
	efx_state->callback_arg = NULL;

	return 0;
}
EXPORT_SYMBOL(efx_vi_eventq_kill_callback);

struct efx_vi_dma_map_state {
	struct efhw_buffer_table_allocation bt_handle;
	int n_pages;
	dma_addr_t *dma_addrs;
};

int
efx_vi_dma_map_pages(struct efx_vi_state *vih, struct page **pages,
		     int n_pages, struct efx_vi_dma_map_state **dmh_out)
{
	struct efx_vi_state *efx_state = vih;
	int order = fls(n_pages - 1), rc, i, evq_id;
	dma_addr_t dma_addr;
	struct efx_vi_dma_map_state *dm_state;

	if (n_pages != (1 << order)) {
		EFRM_WARN("%s: Can only allocate buffers in power of 2 "
			  "sizes (not %d)", __func__, n_pages);
		return -EINVAL;
	}

	dm_state = kmalloc(sizeof(struct efx_vi_dma_map_state), GFP_KERNEL);
	if (!dm_state)
		return -ENOMEM;

	dm_state->dma_addrs = kmalloc(sizeof(dma_addr_t) * n_pages,
				      GFP_KERNEL);
	if (!dm_state->dma_addrs) {
		kfree(dm_state);
		return -ENOMEM;
	}

	rc = efrm_buffer_table_alloc(order, &dm_state->bt_handle);
	if (rc < 0) {
		kfree(dm_state->dma_addrs);
		kfree(dm_state);
		return rc;
	}

	evq_id = EFRM_RESOURCE_INSTANCE(efx_state->vi_res->rs.rs_handle);
	for (i = 0; i < n_pages; i++) {
		/* TODO do we need to get_page() here ? */

		dma_addr = pci_map_page(linux_efhw_nic(efx_state->nic)->
					  pci_dev, pages[i], 0, PAGE_SIZE,
					PCI_DMA_TODEVICE);

		efrm_buffer_table_set(&dm_state->bt_handle, efx_state->nic,
				      i, dma_addr, evq_id);

		dm_state->dma_addrs[i] = dma_addr;

		/* Would be nice to not have to call commit each time, but
		 * comment says there are hardware restrictions on how often
		 * you can go without it, so do this to be safe */
		efrm_buffer_table_commit();
	}

	dm_state->n_pages = n_pages;

	*dmh_out = dm_state;

	return 0;
}
EXPORT_SYMBOL(efx_vi_dma_map_pages);

/* Function needed as Xen can't get pages for grants in dom0, but can
   get dma address */
int
efx_vi_dma_map_addrs(struct efx_vi_state *vih,
		     unsigned long long *bus_dev_addrs,
		     int n_pages, struct efx_vi_dma_map_state **dmh_out)
{
	struct efx_vi_state *efx_state = vih;
	int order = fls(n_pages - 1), rc, i, evq_id;
	dma_addr_t dma_addr;
	struct efx_vi_dma_map_state *dm_state;

	if (n_pages != (1 << order)) {
		EFRM_WARN("%s: Can only allocate buffers in power of 2 "
			  "sizes (not %d)", __func__, n_pages);
		return -EINVAL;
	}

	dm_state = kmalloc(sizeof(struct efx_vi_dma_map_state), GFP_KERNEL);
	if (!dm_state)
		return -ENOMEM;

	dm_state->dma_addrs = kmalloc(sizeof(dma_addr_t) * n_pages,
				      GFP_KERNEL);
	if (!dm_state->dma_addrs) {
		kfree(dm_state);
		return -ENOMEM;
	}

	rc = efrm_buffer_table_alloc(order, &dm_state->bt_handle);
	if (rc < 0) {
		kfree(dm_state->dma_addrs);
		kfree(dm_state);
		return rc;
	}

	evq_id = EFRM_RESOURCE_INSTANCE(efx_state->vi_res->rs.rs_handle);
#if 0
	EFRM_WARN("%s: mapping %d pages to evq %d, bt_ids %d-%d\n",
		  __func__, n_pages, evq_id,
		  dm_state->bt_handle.base,
		  dm_state->bt_handle.base + n_pages);
#endif
	for (i = 0; i < n_pages; i++) {

		dma_addr = (dma_addr_t)bus_dev_addrs[i];

		efrm_buffer_table_set(&dm_state->bt_handle, efx_state->nic,
				      i, dma_addr, evq_id);

		dm_state->dma_addrs[i] = dma_addr;

		/* Would be nice to not have to call commit each time, but
		 * comment says there are hardware restrictions on how often
		 * you can go without it, so do this to be safe */
		efrm_buffer_table_commit();
	}

	dm_state->n_pages = n_pages;

	*dmh_out = dm_state;

	return 0;
}
EXPORT_SYMBOL(efx_vi_dma_map_addrs);

void
efx_vi_dma_unmap_pages(struct efx_vi_state *vih,
		       struct efx_vi_dma_map_state *dmh)
{
	struct efx_vi_state *efx_state = vih;
	struct efx_vi_dma_map_state *dm_state =
	    (struct efx_vi_dma_map_state *)dmh;
	int i;

	efrm_buffer_table_free(&dm_state->bt_handle);

	for (i = 0; i < dm_state->n_pages; ++i)
		pci_unmap_page(linux_efhw_nic(efx_state->nic)->pci_dev,
			       dm_state->dma_addrs[i], PAGE_SIZE,
			       PCI_DMA_TODEVICE);

	kfree(dm_state->dma_addrs);
	kfree(dm_state);

	return;
}
EXPORT_SYMBOL(efx_vi_dma_unmap_pages);

void
efx_vi_dma_unmap_addrs(struct efx_vi_state *vih,
		       struct efx_vi_dma_map_state *dmh)
{
	struct efx_vi_dma_map_state *dm_state =
	    (struct efx_vi_dma_map_state *)dmh;

	efrm_buffer_table_free(&dm_state->bt_handle);

	kfree(dm_state->dma_addrs);
	kfree(dm_state);

	return;
}
EXPORT_SYMBOL(efx_vi_dma_unmap_addrs);

unsigned
efx_vi_dma_get_map_addr(struct efx_vi_state *vih,
			struct efx_vi_dma_map_state *dmh)
{
	struct efx_vi_dma_map_state *dm_state =
	    (struct efx_vi_dma_map_state *)dmh;

	return EFHW_BUFFER_ADDR(dm_state->bt_handle.base, 0);
}
EXPORT_SYMBOL(efx_vi_dma_get_map_addr);

#if EFX_VI_STATIC_FILTERS
static int
get_filter(struct efx_vi_state *efx_state,
	   efrm_resource_handle_t pthandle, struct filter_resource **fres_out)
{
	struct filter_list_t *flist;
	if (efx_state->free_fres == NULL)
		return -ENOMEM;
	else {
		flist = efx_state->free_fres;
		efx_state->free_fres = flist->next;
		flist->next = efx_state->used_fres;
		efx_state->used_fres = flist;
		*fres_out = flist->fres;
		return 0;
	}
}
#endif

static void
release_filter(struct efx_vi_state *efx_state, struct filter_resource *fres)
{
#if EFX_VI_STATIC_FILTERS
	struct filter_list_t *flist = efx_state->used_fres, *prev = NULL;
	while (flist) {
		if (flist->fres == fres) {
			if (prev)
				prev->next = flist->next;
			else
				efx_state->used_fres = flist->next;
			flist->next = efx_state->free_fres;
			efx_state->free_fres = flist;
			return;
		}
		prev = flist;
		flist = flist->next;
	}
	EFRM_ERR("%s: couldn't find filter", __func__);
#else
	return efrm_filter_resource_release(fres);
#endif
}

int
efx_vi_filter(struct efx_vi_state *vih, int protocol,
	      unsigned ip_addr_be32, int port_le16,
	      struct filter_resource_t **fh_out)
{
	struct efx_vi_state *efx_state = vih;
	struct filter_resource *uninitialized_var(frs);
	int rc;

#if EFX_VI_STATIC_FILTERS
	rc = get_filter(efx_state, efx_state->vi_res->rs.rs_handle, &frs);
#else
	rc = efrm_filter_resource_alloc(efx_state->vi_res, &frs);
#endif
	if (rc < 0)
		return rc;

	/* Add the hardware filter. We pass in the source port and address
	 * as 0 (wildcard) to minimise the number of filters needed. */
	if (protocol == IPPROTO_TCP) {
		rc = efrm_filter_resource_tcp_set(frs, 0, 0, ip_addr_be32,
						  port_le16);
	} else {
		rc = efrm_filter_resource_udp_set(frs, 0, 0, ip_addr_be32,
						  port_le16);
	}

	*fh_out = (struct filter_resource_t *)frs;

	return rc;
}
EXPORT_SYMBOL(efx_vi_filter);

int
efx_vi_filter_stop(struct efx_vi_state *vih, struct filter_resource_t *fh)
{
	struct efx_vi_state *efx_state = vih;
	struct filter_resource *frs = (struct filter_resource *)fh;
	int rc;

	rc = efrm_filter_resource_clear(frs);
	release_filter(efx_state, frs);

	return rc;
}
EXPORT_SYMBOL(efx_vi_filter_stop);

int
efx_vi_hw_resource_get_virt(struct efx_vi_state *vih,
			    struct efx_vi_hw_resource_metadata *mdata,
			    struct efx_vi_hw_resource *hw_res_array,
			    int *length)
{
	EFRM_NOTICE("%s: TODO!", __func__);

	return 0;
}
EXPORT_SYMBOL(efx_vi_hw_resource_get_virt);

int
efx_vi_hw_resource_get_phys(struct efx_vi_state *vih,
			    struct efx_vi_hw_resource_metadata *mdata,
			    struct efx_vi_hw_resource *hw_res_array,
			    int *length)
{
	struct efx_vi_state *efx_state = vih;
	struct linux_efhw_nic *lnic = linux_efhw_nic(efx_state->nic);
	unsigned long phys = lnic->ctr_ap_pci_addr;
	struct efrm_resource *ep_res = &efx_state->vi_res->rs;
	unsigned ep_mmap_bytes;
	int i;

	if (*length < EFX_VI_HW_RESOURCE_MAXSIZE)
		return -EINVAL;

	mdata->nic_arch = efx_state->nic->devtype.arch;
	mdata->nic_variant = efx_state->nic->devtype.variant;
	mdata->nic_revision = efx_state->nic->devtype.revision;

	mdata->evq_order =
	    efx_state->vi_res->nic_info.evq_pages.iobuff.order;
	mdata->evq_offs = efx_state->vi_res->nic_info.evq_pages.iobuff_off;
	mdata->evq_capacity = efx_vi_eventq_size;
	mdata->instance = EFRM_RESOURCE_INSTANCE(ep_res->rs_handle);
	mdata->rx_capacity = FALCON_DMA_Q_DEFAULT_RX_SIZE;
	mdata->tx_capacity = FALCON_DMA_Q_DEFAULT_TX_SIZE;

	ep_mmap_bytes = FALCON_DMA_Q_DEFAULT_MMAP;
	EFRM_ASSERT(ep_mmap_bytes == PAGE_SIZE * 2);

#ifndef NDEBUG
	{
		/* Sanity about doorbells */
		unsigned long tx_dma_page_addr, rx_dma_page_addr;

		/* get rx doorbell address */
		rx_dma_page_addr =
		    phys + falcon_rx_dma_page_addr(mdata->instance);
		/* get tx doorbell address */
		tx_dma_page_addr =
		    phys + falcon_tx_dma_page_addr(mdata->instance);

		/* Check the lower bits of the TX doorbell will be
		 * consistent. */
		EFRM_ASSERT((TX_DESC_UPD_REG_PAGE4_OFST &
			     FALCON_DMA_PAGE_MASK) ==
			    (TX_DESC_UPD_REG_PAGE123K_OFST &
			     FALCON_DMA_PAGE_MASK));

		/* Check the lower bits of the RX doorbell will be
		 * consistent. */
		EFRM_ASSERT((RX_DESC_UPD_REG_PAGE4_OFST &
			     FALCON_DMA_PAGE_MASK) ==
			    (RX_DESC_UPD_REG_PAGE123K_OFST &
			     FALCON_DMA_PAGE_MASK));

		/* Check that the doorbells will be in the same page. */
		EFRM_ASSERT((TX_DESC_UPD_REG_PAGE4_OFST & PAGE_MASK) ==
			    (RX_DESC_UPD_REG_PAGE4_OFST & PAGE_MASK));

		/* Check that the doorbells are in the same page. */
		EFRM_ASSERT((tx_dma_page_addr & PAGE_MASK) ==
			    (rx_dma_page_addr & PAGE_MASK));

		/* Check that the TX doorbell offset is correct. */
		EFRM_ASSERT((TX_DESC_UPD_REG_PAGE4_OFST & ~PAGE_MASK) ==
			    (tx_dma_page_addr & ~PAGE_MASK));

		/* Check that the RX doorbell offset is correct. */
		EFRM_ASSERT((RX_DESC_UPD_REG_PAGE4_OFST & ~PAGE_MASK) ==
			    (rx_dma_page_addr & ~PAGE_MASK));
	}
#endif

	i = 0;
	hw_res_array[i].type = EFX_VI_HW_RESOURCE_TXDMAQ;
	hw_res_array[i].mem_type = EFX_VI_HW_RESOURCE_PERIPHERAL;
	hw_res_array[i].more_to_follow = 0;
	hw_res_array[i].length = PAGE_SIZE;
	hw_res_array[i].address =
		(unsigned long)efx_state->vi_res->nic_info.
			dmaq_pages[EFRM_VI_RM_DMA_QUEUE_TX].kva;

	i++;
	hw_res_array[i].type = EFX_VI_HW_RESOURCE_RXDMAQ;
	hw_res_array[i].mem_type = EFX_VI_HW_RESOURCE_PERIPHERAL;
	hw_res_array[i].more_to_follow = 0;
	hw_res_array[i].length = PAGE_SIZE;
	hw_res_array[i].address =
		(unsigned long)efx_state->vi_res->nic_info.
			dmaq_pages[EFRM_VI_RM_DMA_QUEUE_RX].kva;

	i++;
	hw_res_array[i].type = EFX_VI_HW_RESOURCE_EVQTIMER;
	hw_res_array[i].mem_type = EFX_VI_HW_RESOURCE_PERIPHERAL;
	hw_res_array[i].more_to_follow = 0;
	hw_res_array[i].length = PAGE_SIZE;
	hw_res_array[i].address =
		(unsigned long)phys + falcon_timer_page_addr(mdata->instance);

	/* NB EFX_VI_HW_RESOURCE_EVQPTR not used on Falcon */

	i++;
	switch (efx_state->nic->devtype.variant) {
	case 'A':
		hw_res_array[i].type = EFX_VI_HW_RESOURCE_EVQRPTR;
		hw_res_array[i].mem_type = EFX_VI_HW_RESOURCE_PERIPHERAL;
		hw_res_array[i].more_to_follow = 0;
		hw_res_array[i].length = PAGE_SIZE;
		hw_res_array[i].address = (unsigned long)phys +
			EVQ_RPTR_REG_OFST +
			(FALCON_REGISTER128 * mdata->instance);
		break;
	case 'B':
		hw_res_array[i].type = EFX_VI_HW_RESOURCE_EVQRPTR_OFFSET;
		hw_res_array[i].mem_type = EFX_VI_HW_RESOURCE_PERIPHERAL;
		hw_res_array[i].more_to_follow = 0;
		hw_res_array[i].length = PAGE_SIZE;
		hw_res_array[i].address =
			(unsigned long)FALCON_EVQ_RPTR_REG_P0;
		break;
	default:
		EFRM_ASSERT(0);
		break;
	}

	i++;
	hw_res_array[i].type = EFX_VI_HW_RESOURCE_EVQMEMKVA;
	hw_res_array[i].mem_type = EFX_VI_HW_RESOURCE_IOBUFFER;
	hw_res_array[i].more_to_follow = 0;
	hw_res_array[i].length = PAGE_SIZE;
	hw_res_array[i].address = (unsigned long)efx_state->vi_res->
		nic_info.evq_pages.iobuff.kva;

	i++;
	hw_res_array[i].type = EFX_VI_HW_RESOURCE_BELLPAGE;
	hw_res_array[i].mem_type = EFX_VI_HW_RESOURCE_PERIPHERAL;
	hw_res_array[i].more_to_follow = 0;
	hw_res_array[i].length = PAGE_SIZE;
	hw_res_array[i].address =
		(unsigned long)(phys +
				falcon_tx_dma_page_addr(mdata->instance))
		>> PAGE_SHIFT;

	i++;

	EFRM_ASSERT(i <= *length);

	*length = i;

	return 0;
}
EXPORT_SYMBOL(efx_vi_hw_resource_get_phys);
