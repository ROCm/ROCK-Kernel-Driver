/****************************************************************************
 * Solarflare driver for Xen network acceleration
 *
 * Copyright 2006-2008: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Maintained by Solarflare Communications <linux-xen-drivers@solarflare.com>
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

#include <linux/if_ether.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/hypercall.h>
#include <xen/xenbus.h>
#include <xen/driver_util.h>
#include <xen/gnttab.h>

#include "accel_util.h"

#ifdef EFX_GCOV
#include "gcov.h"

static int __init net_accel_init(void)
{
	gcov_provider_init(THIS_MODULE);
	return 0;
}
module_init(net_accel_init);

static void __exit net_accel_exit(void)
{
	gcov_provider_fini(THIS_MODULE);
}
module_exit(net_accel_exit);
#endif

/* Shutdown remote domain that is misbehaving */
int net_accel_shutdown_remote(int domain)
{
	struct sched_remote_shutdown sched_shutdown = {
		.domain_id = domain,
		.reason = SHUTDOWN_crash
	};

	EPRINTK("Crashing domain %d\n", domain);

	return HYPERVISOR_sched_op(SCHEDOP_remote_shutdown, &sched_shutdown);
}
EXPORT_SYMBOL(net_accel_shutdown_remote);


/* Based on xenbus_backend_client.c:xenbus_map_ring() */
static int net_accel_map_grant(struct xenbus_device *dev, int gnt_ref,
			       grant_handle_t *handle, void *vaddr, 
			       u64 *dev_bus_addr, unsigned flags)
{
	struct gnttab_map_grant_ref op;
	
	gnttab_set_map_op(&op, (unsigned long)vaddr, flags,
			  gnt_ref, dev->otherend_id);

	BUG_ON(HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, &op, 1));

	if (op.status != GNTST_okay) {
		xenbus_dev_error
			(dev, op.status,
			 "failed mapping in shared page %d from domain %d\n",
			 gnt_ref, dev->otherend_id);
	} else {
		*handle = op.handle;
		if (dev_bus_addr)
			*dev_bus_addr = op.dev_bus_addr;
	}

	return op.status;
}


/* Based on xenbus_backend_client.c:xenbus_unmap_ring() */
static int net_accel_unmap_grant(struct xenbus_device *dev, 
				 grant_handle_t handle,
				 void *vaddr, u64 dev_bus_addr,
				 unsigned flags)
{
	struct gnttab_unmap_grant_ref op;

	gnttab_set_unmap_op(&op, (unsigned long)vaddr, flags, handle);
	
	if (dev_bus_addr)
		op.dev_bus_addr = dev_bus_addr;

	BUG_ON(HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, &op, 1));

	if (op.status != GNTST_okay)
		xenbus_dev_error(dev, op.status,
				 "failed unmapping page at handle %d error %d\n",
				 handle, op.status);

	return op.status;
}


int net_accel_map_device_page(struct xenbus_device *dev,  
			      int gnt_ref, grant_handle_t *handle,
			      u64 *dev_bus_addr)
{
	return net_accel_map_grant(dev, gnt_ref, handle, 0, dev_bus_addr,
				   GNTMAP_device_map);
}
EXPORT_SYMBOL_GPL(net_accel_map_device_page);

 
int net_accel_unmap_device_page(struct xenbus_device *dev,
				grant_handle_t handle, u64 dev_bus_addr)
{
	return net_accel_unmap_grant(dev, handle, 0, dev_bus_addr, 
				     GNTMAP_device_map);
}
EXPORT_SYMBOL_GPL(net_accel_unmap_device_page);


struct net_accel_valloc_grant_mapping {
	struct vm_struct *vm;
	int pages;
	grant_handle_t grant_handles[0];
};

/* Map a series of grants into a contiguous virtual area */
static void *net_accel_map_grants_valloc(struct xenbus_device *dev, 
					 unsigned *grants, int npages, 
					 unsigned flags, void **priv)
{
	struct net_accel_valloc_grant_mapping *map;
	struct vm_struct *vm;
	void *addr;
	int i, j, rc;

	vm  = alloc_vm_area(PAGE_SIZE * npages);
	if (vm == NULL) {
		EPRINTK("No memory from alloc_vm_area.\n");
		return NULL;
	}
	/* 
	 * Get a structure in which we will record all the info needed
	 * to undo the mapping.
	 */
	map = kzalloc(sizeof(struct net_accel_valloc_grant_mapping)  + 
		      npages * sizeof(grant_handle_t), GFP_KERNEL);
	if (map == NULL) {
		EPRINTK("No memory for net_accel_valloc_grant_mapping\n");
		free_vm_area(vm);
		return NULL;
	}
	map->vm = vm;
	map->pages = npages;

	/* Do the actual mapping */
	addr = vm->addr;
	for (i = 0; i < npages; i++) {
		rc = net_accel_map_grant(dev, grants[i], map->grant_handles + i, 
					 addr, NULL, flags);
		if (rc != 0)
			goto undo;
		addr = (void*)((unsigned long)addr + PAGE_SIZE);
	}

	if (priv)
		*priv = (void *)map;
	else
		kfree(map);

	return vm->addr;

 undo:
	EPRINTK("Aborting contig map due to single map failure %d (%d of %d)\n",
		rc, i+1, npages);
	for (j = 0; j < i; j++) {
		addr = (void*)((unsigned long)vm->addr + (j * PAGE_SIZE));
		net_accel_unmap_grant(dev, map->grant_handles[j], addr, 0,
				      flags);
	}
	free_vm_area(vm);
	kfree(map);
	return NULL;
}

/* Undo the result of the mapping */
static void net_accel_unmap_grants_vfree(struct xenbus_device *dev, 
					 unsigned flags, void *priv)
{
	struct net_accel_valloc_grant_mapping *map = 
		(struct net_accel_valloc_grant_mapping *)priv;

	void *addr = map->vm->addr;
	int npages = map->pages;
	int i;

	for (i = 0; i < npages; i++) {
		net_accel_unmap_grant(dev, map->grant_handles[i], addr, 0,
				      flags);
		addr = (void*)((unsigned long)addr + PAGE_SIZE);
	}
	free_vm_area(map->vm);
	kfree(map);
}


void *net_accel_map_grants_contig(struct xenbus_device *dev,
				unsigned *grants, int npages, 
				void **priv)
{
	return net_accel_map_grants_valloc(dev, grants, npages,
					   GNTMAP_host_map, priv);
}
EXPORT_SYMBOL(net_accel_map_grants_contig);


void net_accel_unmap_grants_contig(struct xenbus_device *dev,
				   void *priv)
{
	net_accel_unmap_grants_vfree(dev, GNTMAP_host_map, priv);
}
EXPORT_SYMBOL(net_accel_unmap_grants_contig);


void *net_accel_map_iomem_page(struct xenbus_device *dev, int gnt_ref,
			     void **priv)
{
	return net_accel_map_grants_valloc(dev, &gnt_ref, 1, 
					   GNTMAP_host_map, priv);
}
EXPORT_SYMBOL(net_accel_map_iomem_page);


void net_accel_unmap_iomem_page(struct xenbus_device *dev, void *priv)
{
	net_accel_unmap_grants_vfree(dev, GNTMAP_host_map, priv);
}
EXPORT_SYMBOL(net_accel_unmap_iomem_page);


int net_accel_grant_page(struct xenbus_device *dev, unsigned long mfn, 
			 int is_iomem)
{
	int err = gnttab_grant_foreign_access(dev->otherend_id, mfn,
					      is_iomem ? GTF_PCD : 0);
	if (err < 0)
		xenbus_dev_error(dev, err, "failed granting access to page\n");
	return err;
}
EXPORT_SYMBOL_GPL(net_accel_grant_page);


int net_accel_ungrant_page(grant_ref_t gntref)
{
	if (unlikely(gnttab_query_foreign_access(gntref) != 0)) {
		EPRINTK("%s: remote domain still using grant %d\n", __FUNCTION__, 
			gntref);
		return -EBUSY;
	}

	gnttab_end_foreign_access(gntref, 0);
	return 0;
}
EXPORT_SYMBOL_GPL(net_accel_ungrant_page);


int net_accel_xen_net_read_mac(struct xenbus_device *dev, u8 mac[])
{
	char *s, *e, *macstr;
	int i;

	macstr = s = xenbus_read(XBT_NIL, dev->nodename, "mac", NULL);
	if (IS_ERR(macstr))
		return PTR_ERR(macstr);

	for (i = 0; i < ETH_ALEN; i++) {
		mac[i] = simple_strtoul(s, &e, 16);
		if ((s == e) || (*e != ((i == ETH_ALEN-1) ? '\0' : ':'))) {
			kfree(macstr);
			return -ENOENT;
		}
		s = e+1;
	}

	kfree(macstr);
	return 0;
}
EXPORT_SYMBOL_GPL(net_accel_xen_net_read_mac);


void net_accel_update_state(struct xenbus_device *dev, int state)
{
	struct xenbus_transaction tr;
	int err;

	DPRINTK("%s: setting accelstate to %s\n", __FUNCTION__,
		xenbus_strstate(state));

	if (xenbus_exists(XBT_NIL, dev->nodename, "")) {
		VPRINTK("%s: nodename %s\n", __FUNCTION__, dev->nodename);
	again:
		err = xenbus_transaction_start(&tr);
		if (err == 0)
			err = xenbus_printf(tr, dev->nodename, "accelstate",
					    "%d", state);
		if (err != 0) {
			xenbus_transaction_end(tr, 1);
		} else {
			err = xenbus_transaction_end(tr, 0);
			if (err == -EAGAIN)
				goto again;
		}
	}
}
EXPORT_SYMBOL_GPL(net_accel_update_state);

MODULE_LICENSE("GPL");
