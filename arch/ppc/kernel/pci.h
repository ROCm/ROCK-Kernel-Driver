/*
 * BK Id: SCCS/s.pci.h 1.10 08/08/01 16:35:43 paulus
 */

#ifndef __PPC_KERNEL_PCI_H__
#define __PPC_KERNEL_PCI_H__

/* Configure those in your xxx_init() or xxx_setup_arch() function */
extern unsigned long isa_io_base;
extern unsigned long isa_mem_base;
extern unsigned long pci_dram_offset;

/* Set this to 1 if you want the kernel to re-assign all PCI
 * bus numbers
 */
extern int pci_assign_all_busses;


extern struct pci_controller* pcibios_alloc_controller(void);
extern struct pci_controller* pci_find_hose_for_OF_device(
			struct device_node* node);

extern void setup_indirect_pci(struct pci_controller* hose,
			u32 cfg_addr, u32 cfg_data);
extern void setup_grackle(struct pci_controller *hose);

#endif /* __PPC_KERNEL_PCI_H__ */
