/*
 * IBM PowerPC Virtual I/O Infrastructure Support.
 *
 *    Copyright (c) 2003 IBM Corp.
 *     Dave Engebretsen engebret@us.ibm.com
 *     Santiago Leon santil@us.ibm.com
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _VIO_H
#define _VIO_H

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <asm/hvcall.h>
#include <asm/prom.h>
#include <asm/scatterlist.h>
/* 
 * Architecture-specific constants for drivers to
 * extract attributes of the device using vio_get_attribute()
*/
#define VETH_MAC_ADDR "local-mac-address"
#define VETH_MCAST_FILTER_SIZE "ibm,mac-address-filters"

/* End architecture-specific constants */

#define h_vio_signal(ua, mode) \
  plpar_hcall_norets(H_VIO_SIGNAL, ua, mode)

#define VIO_IRQ_DISABLE		0UL
#define VIO_IRQ_ENABLE		1UL

struct vio_dev;
struct vio_driver;
struct vio_device_id;
struct TceTable;

int vio_register_driver(struct vio_driver *drv);
void vio_unregister_driver(struct vio_driver *drv);
const struct vio_device_id * vio_match_device(const struct vio_device_id *ids, 
						const struct vio_dev *dev);
struct vio_dev * __devinit vio_register_device(struct device_node *node_vdev);
void __devinit vio_unregister_device(struct vio_dev *dev);
const void * vio_get_attribute(struct vio_dev *vdev, void* which, int* length);
int vio_get_irq(struct vio_dev *dev);
struct TceTable * vio_build_tce_table(struct vio_dev *dev);
int vio_enable_interrupts(struct vio_dev *dev);
int vio_disable_interrupts(struct vio_dev *dev);

dma_addr_t vio_map_single(struct vio_dev *dev, void *vaddr, 
			  size_t size, int direction);
void vio_unmap_single(struct vio_dev *dev, dma_addr_t dma_handle, 
		      size_t size, int direction);
int vio_map_sg(struct vio_dev *vdev, struct scatterlist *sglist, 
	       int nelems, int direction);
void vio_unmap_sg(struct vio_dev *vdev, struct scatterlist *sglist, 
		  int nelems, int direction);
void *vio_alloc_consistent(struct vio_dev *dev, size_t size, 
			   dma_addr_t *dma_handle);
void vio_free_consistent(struct vio_dev *dev, size_t size, void *vaddr, 
			 dma_addr_t dma_handle);

extern struct bus_type vio_bus_type;

struct vio_device_id {
	char *type;
	char *compat;
};

struct vio_driver {
	struct list_head node;
	char *name;
	const struct vio_device_id *id_table;	/* NULL if wants all devices */
	int  (*probe)  (struct vio_dev *dev, const struct vio_device_id *id);	/* New device inserted */
	int (*remove) (struct vio_dev *dev);	/* Device removed (NULL if not a hot-plug capable driver) */
	unsigned long driver_data;

	struct device_driver driver;
};

static inline struct vio_driver *to_vio_driver(struct device_driver *drv)
{
	return container_of(drv, struct vio_driver, driver);
}

/*
 * The vio_dev structure is used to describe virtual I/O devices.
 */
struct vio_dev {
	struct device_node *archdata;   /* Open Firmware node */
	void *driver_data;              /* data private to the driver */
	unsigned long unit_address;	
	struct TceTable *tce_table;     /* vio_map_* uses this */
	unsigned int irq;

	struct device dev;
};

static inline struct vio_dev *to_vio_dev(struct device *dev)
{
	return container_of(dev, struct vio_dev, dev);
}

#endif /* _PHYP_H */
