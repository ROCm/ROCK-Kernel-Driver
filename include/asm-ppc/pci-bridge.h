#ifdef __KERNEL__
#ifndef _ASM_PCI_BRIDGE_H
#define _ASM_PCI_BRIDGE_H

void pmac_find_bridges(void);

/*
 * pci_io_base returns the memory address at which you can access
 * the I/O space for PCI bus number `bus' (or NULL on error).
 * 
 * NOTE: This doesn't handle the new Uni-N chip which requires
 *       per-device io_base. 
 */
void *pci_io_base(unsigned int bus);

/* This version handles the new Uni-N host bridge, the iobase is now
 * a per-device thing. I also added the memory base so PReP can
 * be fixed to return 0xc0000000 (I didn't actually implement it)
 *
 * pci_dev_io_base() returns either a virtual (ioremap'ed) address or
 * a physical address. In-kernel clients will use logical while the
 * sys_pciconfig_iobase syscall returns a physical one to userland.
 */
void *pci_dev_io_base(unsigned char bus, unsigned char devfn, int physical);
void *pci_dev_mem_base(unsigned char bus, unsigned char devfn);

/* Returns the root-bridge number (Uni-N number) of a device */
int pci_dev_root_bridge(unsigned char bus, unsigned char devfn);

/*
 * pci_device_loc returns the bus number and device/function number
 * for a device on a PCI bus, given its device_node struct.
 * It returns 0 if OK, -1 on error.
 */
int pci_device_loc(struct device_node *dev, unsigned char *bus_ptr,
		   unsigned char *devfn_ptr);

struct bridge_data {
	volatile unsigned int *cfg_addr;
	volatile unsigned char *cfg_data;
	void *io_base;		/* virtual */
	unsigned long io_base_phys;
	int bus_number;
	int max_bus;
	struct bridge_data *next;
	struct device_node *node;
};

#endif
#endif /* __KERNEL__ */
