#ifdef __KERNEL__
#ifndef _ASM_PCI_BRIDGE_H
#define _ASM_PCI_BRIDGE_H

#include <linux/pci.h>

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

struct device_node;
struct pci_controller;

/* Get the PCI host controller for an OF device */
extern struct pci_controller*
pci_find_hose_for_OF_device(struct device_node* node);

enum phb_types { 
	phb_type_unknown    = 0x0,
	phb_type_hypervisor = 0x1,
	phb_type_python     = 0x10,
	phb_type_speedwagon = 0x11,
	phb_type_winnipeg   = 0x12,
	phb_type_apple      = 0xff
};

/*
 * Structure of a PCI controller (host bridge)
 */
struct pci_controller {
	char what[8];                     /* Eye catcher      */
	enum phb_types type;              /* Type of hardware */
	struct pci_controller *next;
	struct pci_bus *bus;
	void *arch_data;

	int first_busno;
	int last_busno;

	void *io_base_virt;
	unsigned long io_base_phys;

	/* Some machines have a non 1:1 mapping of
	 * the PCI memory space in the CPU bus space
	 */
	unsigned long pci_mem_offset;
	unsigned long pci_io_offset;

	struct pci_ops *ops;
	volatile unsigned int *cfg_addr;
	volatile unsigned char *cfg_data;

	/* Currently, we limit ourselves to 1 IO range and 3 mem
	 * ranges since the common pci_bus structure can't handle more
	 */
	struct resource io_resource;
	struct resource mem_resources[3];
	int mem_resource_count;
	int global_number;		
	int local_number;		
	unsigned long buid;
	unsigned long dma_window_base_cur;
	unsigned long dma_window_size;
};

/*
 * pci_device_loc returns the bus number and device/function number
 * for a device on a PCI bus, given its device_node struct.
 * It returns 0 if OK, -1 on error.
 */
int pci_device_loc(struct device_node *dev, unsigned char *bus_ptr,
		   unsigned char *devfn_ptr);

struct device_node *fetch_dev_dn(struct pci_dev *dev);

/* Get a device_node from a pci_dev.  This code must be fast except in the case
 * where the sysdata is incorrect and needs to be fixed up (hopefully just once)
 */
static inline struct device_node *pci_device_to_OF_node(struct pci_dev *dev)
{
	struct device_node *dn = (struct device_node *)(dev->sysdata);
	if (dn->devfn == dev->devfn && dn->busno == (dev->bus->number&0xff))
		return dn;	/* fast path.  sysdata is good */
	else
		return fetch_dev_dn(dev);
}

/* Use this macro after the PCI bus walk for max performance when it
 * is known that sysdata is correct.
 */
#define PCI_GET_DN(dev) ((struct device_node *)((dev)->sysdata))

#endif
#endif /* __KERNEL__ */
