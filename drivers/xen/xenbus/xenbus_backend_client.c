/******************************************************************************
 * Backend-client-facing interface for the Xenbus driver.  In other words, the
 * interface between the Xenbus and the device-specific code in the backend
 * driver.
 *
 * Copyright (C) 2005-2006 XenSource Ltd
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <xen/gnttab.h>
#include <xen/xenbus.h>

static int unmap_ring_vfree(struct xenbus_device *dev, struct vm_struct *area,
			    unsigned int nr, grant_handle_t handles[])
{
	unsigned int i;
	int err = 0;

	for (i = 0; i < nr; ++i) {
		struct gnttab_unmap_grant_ref op;

		gnttab_set_unmap_op(&op,
				    (unsigned long)area->addr + i * PAGE_SIZE,
				    GNTMAP_host_map, handles[i]);

		if (HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref,
					      &op, 1))
			BUG();
		if (op.status == GNTST_okay)
			continue;

		xenbus_dev_error(dev, op.status,
				 "unmapping page %u (handle %#x)",
				 i, handles[i]);
		err = -EINVAL;
	}

	if (!err) {
		free_vm_area(area);
		kfree(handles);
	}

	return err;
}

/* Based on Rusty Russell's skeleton driver's map_page */
struct vm_struct *xenbus_map_ring_valloc(struct xenbus_device *dev,
					 const grant_ref_t refs[],
					 unsigned int nr)
{
	grant_handle_t *handles = kmalloc(nr * sizeof(*handles), GFP_KERNEL);
	struct vm_struct *area;
	unsigned int i;

	if (!handles)
		return ERR_PTR(-ENOMEM);

	area = alloc_vm_area(nr * PAGE_SIZE, NULL);
	if (!area) {
		kfree(handles);
		return ERR_PTR(-ENOMEM);
	}

	for (i = 0; i < nr; ++i) {
		struct gnttab_map_grant_ref op;

		gnttab_set_map_op(&op,
				  (unsigned long)area->addr + i * PAGE_SIZE,
				  GNTMAP_host_map, refs[i],
				  dev->otherend_id);
	
		gnttab_check_GNTST_eagain_do_while(GNTTABOP_map_grant_ref,
						   &op);

		if (op.status == GNTST_okay) {
			handles[i] = op.handle;
			continue;
		}

		unmap_ring_vfree(dev, area, i, handles);
		xenbus_dev_fatal(dev, op.status,
				 "mapping page %u (ref %#x, dom%d)",
				 i, refs[i], dev->otherend_id);
		BUG_ON(!IS_ERR(ERR_PTR(op.status)));
		return ERR_PTR(-EINVAL);
	}

	/* Stuff the handle array in an unused field. */
	area->phys_addr = (unsigned long)handles;

	return area;
}
EXPORT_SYMBOL_GPL(xenbus_map_ring_valloc);


/* Based on Rusty Russell's skeleton driver's unmap_page */
int xenbus_unmap_ring_vfree(struct xenbus_device *dev, struct vm_struct *area)
{
	return unmap_ring_vfree(dev, area,
				get_vm_area_size(area) >> PAGE_SHIFT,
				(void *)(unsigned long)area->phys_addr);
}
EXPORT_SYMBOL_GPL(xenbus_unmap_ring_vfree);


int xenbus_dev_is_online(struct xenbus_device *dev)
{
	int rc, val;

	rc = xenbus_scanf(XBT_NIL, dev->nodename, "online", "%d", &val);
	if (rc != 1)
		val = 0; /* no online node present */

	return val;
}
EXPORT_SYMBOL_GPL(xenbus_dev_is_online);

MODULE_LICENSE("Dual BSD/GPL");
