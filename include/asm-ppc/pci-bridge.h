/*
 * BK Id: SCCS/s.pci-bridge.h 1.11 05/21/01 01:31:30 cort
 */
#ifdef __KERNEL__
#ifndef _ASM_PCI_BRIDGE_H
#define _ASM_PCI_BRIDGE_H

struct device_node;
struct pci_controller;

/*
 * pci_io_base returns the memory address at which you can access
 * the I/O space for PCI bus number `bus' (or NULL on error).
 */
extern void *pci_bus_io_base(unsigned int bus);
extern unsigned long pci_bus_io_base_phys(unsigned int bus);
extern unsigned long pci_bus_mem_base_phys(unsigned int bus);

/*
 * PCI <-> OF matching functions 
 */
extern int pci_device_from_OF_node(struct device_node *node,
				   u8* bus, u8* devfn);
extern struct device_node* pci_device_to_OF_node(struct pci_dev *);

/* Get the PCI host controller for a bus */
extern struct pci_controller* pci_bus_to_hose(int bus);

/* Get the PCI host controller for an OF device */
extern struct pci_controller*
pci_find_hose_for_OF_device(struct device_node* node);

/* Fill up host controller resources from the OF node */
extern void
pci_process_bridge_OF_ranges(struct pci_controller *hose,
			   struct device_node *dev, int primary);

/*
 * Structure of a PCI controller (host bridge)
 */
struct pci_controller {
	int index;			/* used for pci_controller_num */
	struct pci_controller *next;
        struct pci_bus *bus;
	void *arch_data;

	int first_busno;
	int last_busno;
        
	void *io_base_virt;
	unsigned long io_base_phys;
	
	/* Some machines (PReP) have a non 1:1 mapping of
	 * the PCI memory space in the CPU bus space
	 */
	unsigned long pci_mem_offset;

	struct pci_ops *ops;
	volatile unsigned int *cfg_addr;
	volatile unsigned char *cfg_data;

	/* Currently, we limit ourselves to 1 IO range and 3 mem
	 * ranges since the common pci_bus structure can't handle more
	 */
	struct resource	io_resource;
	struct resource mem_resources[3];
	int mem_resource_count;
};

/* These are used for config access before all the PCI probing
   has been done. */
int early_read_config_byte(struct pci_controller *hose, int bus, int dev_fn, int where, u8 *val);
int early_read_config_word(struct pci_controller *hose, int bus, int dev_fn, int where, u16 *val);
int early_read_config_dword(struct pci_controller *hose, int bus, int dev_fn, int where, u32 *val);
int early_write_config_byte(struct pci_controller *hose, int bus, int dev_fn, int where, u8 val);
int early_write_config_word(struct pci_controller *hose, int bus, int dev_fn, int where, u16 val);
int early_write_config_dword(struct pci_controller *hose, int bus, int dev_fn, int where, u32 val);

#endif
#endif /* __KERNEL__ */
