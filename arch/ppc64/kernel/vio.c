/*
 * IBM PowerPC Virtual I/O Infrastructure Support.
 *
 *    Copyright (c) 2003 IBM Corp.
 *     Dave Engebretsen engebret@us.ibm.com
 *     Santiago Leon santil@us.ibm.com
 *     Hollis Blanchard <hollisb@us.ibm.com>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/console.h>
#include <linux/pci.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/mm.h>
#include <asm/rtas.h>
#include <asm/pci_dma.h>
#include <asm/dma.h>
#include <asm/ppcdebug.h>
#include <asm/vio.h>
#include <asm/hvcall.h>

#define DBGENTER() pr_debug("%s entered\n", __FUNCTION__)

extern struct TceTable *build_tce_table(struct TceTable *tbl);

extern dma_addr_t get_tces(struct TceTable *, unsigned order,
			   void *page, unsigned numPages, int direction);
extern void tce_free(struct TceTable *tbl, dma_addr_t dma_addr,
		     unsigned order, unsigned num_pages);

static int vio_num_address_cells;
static struct vio_dev *vio_bus_device; /* fake "parent" device */

/* convert from struct device to struct vio_dev and pass to driver.
 * dev->driver has already been set by generic code because vio_bus_match
 * succeeded. */
static int vio_bus_probe(struct device *dev)
{
	struct vio_dev *viodev = to_vio_dev(dev);
	struct vio_driver *viodrv = to_vio_driver(dev->driver);
	const struct vio_device_id *id;
	int error = -ENODEV;

	DBGENTER();

	if (!viodrv->probe)
		return error;

	id = vio_match_device(viodrv->id_table, viodev);
	if (id) {
		error = viodrv->probe(viodev, id);
	}

	return error;
}

/* convert from struct device to struct vio_dev and pass to driver. */
static int vio_bus_remove(struct device *dev)
{
	struct vio_dev *viodev = to_vio_dev(dev);
	struct vio_driver *viodrv = to_vio_driver(dev->driver);

	DBGENTER();

	if (viodrv->remove) {
		return viodrv->remove(viodev);
	}

	/* driver can't remove */
	return 1;
}

/**
 * vio_register_driver: - Register a new vio driver
 * @drv:	The vio_driver structure to be registered.
 */
int vio_register_driver(struct vio_driver *viodrv)
{
	printk(KERN_DEBUG "%s: driver %s registering\n", __FUNCTION__,
		viodrv->name);

	/* fill in 'struct driver' fields */
	viodrv->driver.name = viodrv->name;
	viodrv->driver.bus = &vio_bus_type;
	viodrv->driver.probe = vio_bus_probe;
	viodrv->driver.remove = vio_bus_remove;

	return driver_register(&viodrv->driver);
}
EXPORT_SYMBOL(vio_register_driver);

/**
 * vio_unregister_driver - Remove registration of vio driver.
 * @driver:	The vio_driver struct to be removed form registration
 */
void vio_unregister_driver(struct vio_driver *viodrv)
{
	driver_unregister(&viodrv->driver);
}
EXPORT_SYMBOL(vio_unregister_driver);

/**
 * vio_match_device: - Tell if a VIO device has a matching VIO device id structure.
 * @ids: 	array of VIO device id structures to search in
 * @dev: 	the VIO device structure to match against
 *
 * Used by a driver to check whether a VIO device present in the
 * system is in its list of supported devices. Returns the matching
 * vio_device_id structure or NULL if there is no match.
 */
const struct vio_device_id * vio_match_device(const struct vio_device_id *ids,
	const struct vio_dev *dev)
{
	DBGENTER();

	while (ids->type) {
		if ((strncmp(dev->archdata->type, ids->type, strlen(ids->type)) == 0) &&
			device_is_compatible((struct device_node*)dev->archdata, ids->compat))
			return ids;
		ids++;
	}
	return NULL;
}

/**
 * vio_bus_init: - Initialize the virtual IO bus
 */
static int __init vio_bus_init(void)
{
	struct device_node *node_vroot, *of_node;
	int err;

	err = bus_register(&vio_bus_type);
	if (err) {
		printk(KERN_ERR "failed to register VIO bus\n");
		return err;
	}

	/* the fake parent of all vio devices, just to give us a nice directory */
	vio_bus_device = kmalloc(sizeof(struct vio_dev), GFP_KERNEL);
	if (!vio_bus_device) {
		return 1;
	}
	memset(vio_bus_device, 0, sizeof(struct vio_dev));
	strcpy(vio_bus_device->dev.bus_id, "vdevice");

	err = device_register(&vio_bus_device->dev);
	if (err) {
		printk(KERN_WARNING "%s: device_register returned %i\n", __FUNCTION__,
			err);
		kfree(vio_bus_device);
		return err;
	}

	node_vroot = find_devices("vdevice");
	if ((node_vroot == NULL) || (node_vroot->child == NULL)) {
		printk(KERN_INFO "VIO: missing or empty /vdevice node; no virtual IO"
			" devices present.\n");
		return 0;
	}

	vio_num_address_cells = prom_n_addr_cells(node_vroot->child);

	/*
	 * Create struct vio_devices for each virtual device in the device tree.
	 * Drivers will associate with them later.
	 */
	for (of_node = node_vroot->child;
			of_node != NULL;
			of_node = of_node->sibling) {
		printk(KERN_DEBUG "%s: processing %p\n", __FUNCTION__, of_node);

		vio_register_device(of_node);
	}

	return 0;
}

__initcall(vio_bus_init);


/* vio_dev refcount hit 0 */
static void __devinit vio_dev_release(struct device *dev)
{
	struct vio_dev *viodev = to_vio_dev(dev);

	DBGENTER();

	/* XXX free TCE table */
	of_node_put(viodev->archdata);
	kfree(viodev);
}

/**
 * vio_register_device: - Register a new vio device.
 * @of_node:	The OF node for this device.
 *
 * Creates and initializes a vio_dev structure from the data in
 * of_node (archdata) and adds it to the list of virtual devices.
 * Returns a pointer to the created vio_dev or NULL if node has
 * NULL device_type or compatible fields.
 */
struct vio_dev * __devinit vio_register_device(struct device_node *of_node)
{
	struct vio_dev *viodev;
	unsigned int *unit_address;
	unsigned int *irq_p;

	DBGENTER();

	/* we need the 'device_type' property, in order to match with drivers */
	if ((NULL == of_node->type)) {
		printk(KERN_WARNING
			"%s: node %s missing 'device_type'\n", __FUNCTION__,
			of_node->name ? of_node->name : "<unknown>");
		return NULL;
	}

	unit_address = (unsigned int *)get_property(of_node, "reg", NULL);
	if (!unit_address) {
		printk(KERN_WARNING "%s: node %s missing 'reg'\n", __FUNCTION__,
			of_node->name ? of_node->name : "<unknown>");
		return NULL;
	}

	/* allocate a vio_dev for this node */
	viodev = kmalloc(sizeof(struct vio_dev), GFP_KERNEL);
	if (!viodev) {
		return NULL;
	}
	memset(viodev, 0, sizeof(struct vio_dev));

	viodev->archdata = (void *)of_node_get(of_node);
	viodev->unit_address = *unit_address;
	viodev->tce_table = vio_build_tce_table(viodev);

	viodev->irq = (unsigned int) -1;
	irq_p = (unsigned int *)get_property(of_node, "interrupts", 0);
	if (irq_p) {
		viodev->irq = irq_offset_up(*irq_p);
	}

	/* init generic 'struct device' fields: */
	viodev->dev.parent = &vio_bus_device->dev;
	viodev->dev.bus = &vio_bus_type;
	snprintf(viodev->dev.bus_id, BUS_ID_SIZE, "%s@%lx",
		of_node->name, viodev->unit_address);
	viodev->dev.release = vio_dev_release;

	/* register with generic device framework */
	if (device_register(&viodev->dev)) {
		printk(KERN_ERR "%s: failed to register device %s\n", __FUNCTION__,
			viodev->dev.bus_id);
		/* XXX free TCE table */
		kfree(viodev);
		return NULL;
	}

	return viodev;
}
EXPORT_SYMBOL(vio_register_device);

void __devinit vio_unregister_device(struct vio_dev *viodev)
{
	DBGENTER();
	device_unregister(&viodev->dev);
}
EXPORT_SYMBOL(vio_unregister_device);

/**
 * vio_get_attribute: - get attribute for virtual device
 * @vdev:	The vio device to get property.
 * @which:	The property/attribute to be extracted.
 * @length:	Pointer to length of returned data size (unused if NULL).
 *
 * Calls prom.c's get_property() to return the value of the
 * attribute specified by the preprocessor constant @which
*/
const void * vio_get_attribute(struct vio_dev *vdev, void* which, int* length)
{
	return get_property((struct device_node *)vdev->archdata, (char*)which, length);
}
EXPORT_SYMBOL(vio_get_attribute);

/**
 * vio_build_tce_table: - gets the dma information from OF and builds the TCE tree.
 * @dev: the virtual device.
 *
 * Returns a pointer to the built tce tree, or NULL if it can't
 * find property.
*/
struct TceTable * vio_build_tce_table(struct vio_dev *dev)
{
	unsigned int *dma_window;
	struct TceTable *newTceTable;
	unsigned long offset;
	unsigned long size;
	int dma_window_property_size;

	dma_window = (unsigned int *) get_property((struct device_node *)dev->archdata, "ibm,my-dma-window", &dma_window_property_size);
	if(!dma_window) {
		return NULL;
	}

	newTceTable = (struct TceTable *) kmalloc(sizeof(struct TceTable), GFP_KERNEL);

	/* RPA docs say that #address-cells is always 1 for virtual
		devices, but some older boxes' OF returns 2.  This should
		be removed by GA, unless there is legacy OFs that still
		have 2 for #address-cells */
	size = ((dma_window[1+vio_num_address_cells]
		>> PAGE_SHIFT) << 3) >> PAGE_SHIFT;

	/* This is just an ugly kludge. Remove as soon as the OF for all
	machines actually follow the spec and encodes the offset field
	as phys-encode (that is, #address-cells wide)*/
	if (dma_window_property_size == 12) {
		size = ((dma_window[1] >> PAGE_SHIFT) << 3) >> PAGE_SHIFT;
	} else if (dma_window_property_size == 20) {
		size = ((dma_window[4] >> PAGE_SHIFT) << 3) >> PAGE_SHIFT;
	} else {
		printk(KERN_WARNING "vio_build_tce_table: Invalid size of ibm,my-dma-window=%i, using 0x80 for size\n", dma_window_property_size);
		size = 0x80;
	}

	/*  There should be some code to extract the phys-encoded offset
		using prom_n_addr_cells(). However, according to a comment
		on earlier versions, it's always zero, so we don't bother */
	offset = dma_window[1] >>  PAGE_SHIFT;

	/* TCE table size - measured in units of pages of tce table */
	newTceTable->size = size;
	/* offset for VIO should always be 0 */
	newTceTable->startOffset = offset;
	newTceTable->busNumber   = 0;
	newTceTable->index       = (unsigned long)dma_window[0];
	newTceTable->tceType     = TCE_VB;

	return build_tce_table(newTceTable);
}

int vio_enable_interrupts(struct vio_dev *dev)
{
	int rc = h_vio_signal(dev->unit_address, VIO_IRQ_ENABLE);
	if (rc != H_Success) {
		printk(KERN_ERR "vio: Error 0x%x enabling interrupts\n", rc);
	}
	return rc;
}
EXPORT_SYMBOL(vio_enable_interrupts);

int vio_disable_interrupts(struct vio_dev *dev)
{
	int rc = h_vio_signal(dev->unit_address, VIO_IRQ_DISABLE);
	if (rc != H_Success) {
		printk(KERN_ERR "vio: Error 0x%x disabling interrupts\n", rc);
	}
	return rc;
}
EXPORT_SYMBOL(vio_disable_interrupts);


dma_addr_t vio_map_single(struct vio_dev *dev, void *vaddr,
			  size_t size, int direction )
{
	struct TceTable * tbl;
	dma_addr_t dma_handle = NO_TCE;
	unsigned long uaddr;
	unsigned order, nPages;

	if(direction == PCI_DMA_NONE) BUG();

	uaddr = (unsigned long)vaddr;
	nPages = PAGE_ALIGN( uaddr + size ) - ( uaddr & PAGE_MASK );
	order = get_order( nPages & PAGE_MASK );
	nPages >>= PAGE_SHIFT;

 	/* Client asked for way to much space.  This is checked later anyway */
	/* It is easier to debug here for the drivers than in the tce tables.*/
 	if(order >= NUM_TCE_LEVELS) {
 		printk("VIO_DMA: vio_map_single size to large: 0x%lx \n",size);
 		return NO_TCE;
 	}

	tbl = dev->tce_table;

	if(tbl) {
		dma_handle = get_tces(tbl, order, vaddr, nPages, direction);
		dma_handle |= (uaddr & ~PAGE_MASK);
	}

	return dma_handle;
}
EXPORT_SYMBOL(vio_map_single);

void vio_unmap_single(struct vio_dev *dev, dma_addr_t dma_handle,
		      size_t size, int direction)
{
	struct TceTable * tbl;
	unsigned order, nPages;

	if (direction == PCI_DMA_NONE) BUG();

	nPages = PAGE_ALIGN( dma_handle + size ) - ( dma_handle & PAGE_MASK );
	order = get_order( nPages & PAGE_MASK );
	nPages >>= PAGE_SHIFT;

 	/* Client asked for way to much space.  This is checked later anyway */
	/* It is easier to debug here for the drivers than in the tce tables.*/
 	if(order >= NUM_TCE_LEVELS) {
 		printk("VIO_DMA: vio_unmap_single 0x%lx size to large: 0x%lx \n",(unsigned long)dma_handle,(unsigned long)size);
 		return;
 	}

	tbl = dev->tce_table;
	if(tbl) tce_free(tbl, dma_handle, order, nPages);
}
EXPORT_SYMBOL(vio_unmap_single);

int vio_map_sg(struct vio_dev *vdev, struct scatterlist *sglist, int nelems,
	       int direction)
{
	int i;

	for (i = 0; i < nelems; i++) {

		/* 2.4 scsi scatterlists use address field.
		   Not sure about other subsystems. */
		void *vaddr;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,0)
		if (sglist->address)
			vaddr = sglist->address;
		else
#endif
			vaddr = page_address(sglist->page) + sglist->offset;

		sglist->dma_address = vio_map_single(vdev, vaddr,
						     sglist->length,
						     direction);
		sglist->dma_length = sglist->length;
		sglist++;
	}

	return nelems;
}
EXPORT_SYMBOL(vio_map_sg);

void vio_unmap_sg(struct vio_dev *vdev, struct scatterlist *sglist, int nelems,
		  int direction)
{
	while (nelems--) {
		vio_unmap_single(vdev, sglist->dma_address,
				 sglist->dma_length, direction);
		sglist++;
	}
}

void *vio_alloc_consistent(struct vio_dev *dev, size_t size,
			   dma_addr_t *dma_handle)
{
	struct TceTable * tbl;
	void *ret = NULL;
	unsigned order, nPages;
	dma_addr_t tce;

	size = PAGE_ALIGN(size);
	order = get_order(size);
	nPages = 1 << order;

 	/* Client asked for way to much space.  This is checked later anyway */
	/* It is easier to debug here for the drivers than in the tce tables.*/
 	if(order >= NUM_TCE_LEVELS) {
 		printk("VIO_DMA: vio_alloc_consistent size to large: 0x%lx \n",size);
 		return (void *)NO_TCE;
 	}

	tbl = dev->tce_table;

	if ( tbl ) {
		/* Alloc enough pages (and possibly more) */
		ret = (void *)__get_free_pages( GFP_ATOMIC, order );
		if ( ret ) {
			/* Page allocation succeeded */
			memset(ret, 0, nPages << PAGE_SHIFT);
			/* Set up tces to cover the allocated range */
			tce = get_tces( tbl, order, ret, nPages, PCI_DMA_BIDIRECTIONAL );
			if ( tce == NO_TCE ) {
				PPCDBG(PPCDBG_TCE, "vio_alloc_consistent: get_tces failed\n" );
				free_pages( (unsigned long)ret, order );
				ret = NULL;
			}
			else
				{
					*dma_handle = tce;
				}
		}
		else PPCDBG(PPCDBG_TCE, "vio_alloc_consistent: __get_free_pages failed for order = %d\n", order);
	}
	else PPCDBG(PPCDBG_TCE, "vio_alloc_consistent: get_tce_table failed for 0x%016lx\n", dev);

	PPCDBG(PPCDBG_TCE, "\tvio_alloc_consistent: dma_handle = 0x%16.16lx\n", *dma_handle);
	PPCDBG(PPCDBG_TCE, "\tvio_alloc_consistent: return     = 0x%16.16lx\n", ret);
	return ret;
}
EXPORT_SYMBOL(vio_alloc_consistent);

void vio_free_consistent(struct vio_dev *dev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	struct TceTable * tbl;
	unsigned order, nPages;

	PPCDBG(PPCDBG_TCE, "vio_free_consistent:\n");
	PPCDBG(PPCDBG_TCE, "\tdev = 0x%16.16lx, size = 0x%16.16lx, dma_handle = 0x%16.16lx, vaddr = 0x%16.16lx\n", dev, size, dma_handle, vaddr);

	size = PAGE_ALIGN(size);
	order = get_order(size);
	nPages = 1 << order;

 	/* Client asked for way to much space.  This is checked later anyway */
	/* It is easier to debug here for the drivers than in the tce tables.*/
 	if(order >= NUM_TCE_LEVELS) {
 		printk("PCI_DMA: pci_free_consistent size to large: 0x%lx \n",size);
 		return;
 	}

	tbl = dev->tce_table;

	if ( tbl ) {
		tce_free(tbl, dma_handle, order, nPages);
		free_pages( (unsigned long)vaddr, order );
	}
}
EXPORT_SYMBOL(vio_free_consistent);

static int vio_bus_match(struct device *dev, struct device_driver *drv)
{
	const struct vio_dev *vio_dev = to_vio_dev(dev);
	struct vio_driver *vio_drv = to_vio_driver(drv);
	const struct vio_device_id *ids = vio_drv->id_table;
	const struct vio_device_id *found_id;

	DBGENTER();

	if (!ids)
		return 0;

	found_id = vio_match_device(ids, vio_dev);
	if (found_id)
		return 1;

	return 0;
}

struct bus_type vio_bus_type = {
	.name = "vio",
	.match = vio_bus_match,
};

EXPORT_SYMBOL(plpar_hcall_norets);
EXPORT_SYMBOL(plpar_hcall_8arg_2ret);
