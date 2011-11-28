 /*****************************************************************************
 * drivers/xen/tpmback/interface.c
 *
 * Vritual TPM interface management.
 *
 * Copyright (c) 2005, IBM Corporation
 *
 * Author: Stefan Berger, stefanb@us.ibm.com
 *
 * This code has been derived from drivers/xen/netback/interface.c
 * Copyright (c) 2004, Keir Fraser
 */

#include "common.h"
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/vmalloc.h>
#include <xen/balloon.h>
#include <xen/evtchn.h>
#include <xen/gnttab.h>

static struct kmem_cache *tpmif_cachep;
int num_frontends = 0;

LIST_HEAD(tpmif_list);

static tpmif_t *alloc_tpmif(domid_t domid, struct backend_info *bi)
{
	tpmif_t *tpmif;

	tpmif = kmem_cache_zalloc(tpmif_cachep, GFP_KERNEL);
	if (tpmif == NULL)
		goto out_of_memory;

	tpmif->domid = domid;
	tpmif->status = DISCONNECTED;
	tpmif->bi = bi;
	snprintf(tpmif->devname, sizeof(tpmif->devname), "tpmif%d", domid);
	atomic_set(&tpmif->refcnt, 1);

	tpmif->mmap_pages = alloc_empty_pages_and_pagevec(TPMIF_TX_RING_SIZE);
	if (tpmif->mmap_pages == NULL)
		goto out_of_memory;

	list_add(&tpmif->tpmif_list, &tpmif_list);
	num_frontends++;

	return tpmif;

 out_of_memory:
	if (tpmif != NULL)
		kmem_cache_free(tpmif_cachep, tpmif);
	pr_err("%s: out of memory\n", __FUNCTION__);
	return ERR_PTR(-ENOMEM);
}

static void free_tpmif(tpmif_t * tpmif)
{
	num_frontends--;
	list_del(&tpmif->tpmif_list);
	free_empty_pages_and_pagevec(tpmif->mmap_pages, TPMIF_TX_RING_SIZE);
	kmem_cache_free(tpmif_cachep, tpmif);
}

tpmif_t *tpmif_find(domid_t domid, struct backend_info *bi)
{
	tpmif_t *tpmif;

	list_for_each_entry(tpmif, &tpmif_list, tpmif_list) {
		if (tpmif->bi == bi) {
			if (tpmif->domid == domid) {
				tpmif_get(tpmif);
				return tpmif;
			} else {
				return ERR_PTR(-EEXIST);
			}
		}
	}

	return alloc_tpmif(domid, bi);
}

int tpmif_map(tpmif_t *tpmif, grant_ref_t ring_ref, evtchn_port_t evtchn)
{
	struct vm_struct *area;
	int err;

	if (tpmif->irq)
		return 0;

	area = xenbus_map_ring_valloc(tpmif->bi->dev, ring_ref);
	if (IS_ERR(area))
		return PTR_ERR(area);
	tpmif->tx_area = area;

	tpmif->tx = (tpmif_tx_interface_t *)area->addr;
	clear_page(tpmif->tx);

	err = bind_interdomain_evtchn_to_irqhandler(
		tpmif->domid, evtchn, tpmif_be_int, 0, tpmif->devname, tpmif);
	if (err < 0) {
		xenbus_unmap_ring_vfree(tpmif->bi->dev, area);
		return err;
	}
	tpmif->irq = err;

	tpmif->active = 1;

	return 0;
}

void tpmif_disconnect_complete(tpmif_t *tpmif)
{
	if (tpmif->irq)
		unbind_from_irqhandler(tpmif->irq, tpmif);

	if (tpmif->tx)
		xenbus_unmap_ring_vfree(tpmif->bi->dev, tpmif->tx_area);

	free_tpmif(tpmif);
}

int __init tpmif_interface_init(void)
{
	tpmif_cachep = kmem_cache_create("tpmif_cache", sizeof (tpmif_t),
					 0, 0, NULL);
	return tpmif_cachep ? 0 : -ENOMEM;
}

void tpmif_interface_exit(void)
{
	kmem_cache_destroy(tpmif_cachep);
}
