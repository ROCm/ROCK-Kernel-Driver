/*
 *  linux/include/asm-arm/mach/pci.h
 *
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

struct pci_sys_data;
struct pci_bus;

struct hw_pci {
	/* START OF OLD STUFF */
	/* Initialise the hardware */
	void		(*init)(void *);

	/* Setup bus resources */
	void		(*setup_resources)(struct resource **);

	/* IRQ swizzle */
	u8		(*swizzle)(struct pci_dev *dev, u8 *pin);

	/* IRQ mapping */
	int		(*map_irq)(struct pci_dev *dev, u8 slot, u8 pin);

	/* END OF OLD STUFF */

	/* NEW STUFF */
	int		nr_controllers;
	int		(*setup)(int nr, struct pci_sys_data *);
	struct pci_bus *(*scan)(int nr, struct pci_sys_data *);
	void		(*preinit)(void);
	void		(*postinit)(void);
};

/*
 * Per-controller structure
 */
struct pci_sys_data {
	int		busnr;		/* primary bus number			*/
	unsigned long	mem_offset;	/* bus->cpu memory mapping offset	*/
	unsigned long	io_offset;	/* bus->cpu IO mapping offset		*/
	struct pci_bus	*bus;		/* PCI bus				*/
	struct resource *resource[3];	/* Primary PCI bus resources		*/
					/* Bridge swizzling			*/
	u8		(*swizzle)(struct pci_dev *, u8 *);
					/* IRQ mapping				*/
	int		(*map_irq)(struct pci_dev *, u8, u8);
	struct hw_pci	*hw;
};

/*
 * This is the standard PCI-PCI bridge swizzling algorithm.
 */
u8 pci_std_swizzle(struct pci_dev *dev, u8 *pinp);

/*
 * PCI controllers
 */
extern int iop310_setup(int nr, struct pci_sys_data *);
extern struct pci_bus *iop310_scan_bus(int nr, struct pci_sys_data *);

extern int dc21285_setup(int nr, struct pci_sys_data *);
extern struct pci_bus *dc21285_scan_bus(int nr, struct pci_sys_data *);
extern void dc21285_preinit(void);
extern void dc21285_postinit(void);

extern int via82c505_setup(int nr, struct pci_sys_data *);
extern struct pci_bus *via82c505_scan_bus(int nr, struct pci_sys_data *);
extern void __init via82c505_init(void *sysdata);

extern int pci_v3_setup(int nr, struct pci_sys_data *);
extern struct pci_bus *pci_v3_scan_bus(int nr, struct pci_sys_data *);
extern void pci_v3_preinit(void);
extern void pci_v3_postinit(void);

