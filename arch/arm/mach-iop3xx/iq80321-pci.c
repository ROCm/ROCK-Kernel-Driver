/*
 * arch/arm/mach-iop3xx/iq80321-pci.c
 *
 * PCI support for the Intel IQ80321 reference board
 *
 * Author: Rory Bolt <rorybolt@pacbell.net>
 * Copyright (C) 2002 Rory Bolt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach/pci.h>
#include <asm/mach-types.h>

/*
 * The following macro is used to lookup irqs in a standard table
 * format for those systems that do not already have PCI
 * interrupts properly routed.  We assume 1 <= pin <= 4
 */
#define PCI_IRQ_TABLE_LOOKUP(minid,maxid)	\
({ int _ctl_ = -1;				\
   unsigned int _idsel = idsel - minid;		\
   if (_idsel <= maxid)				\
      _ctl_ = pci_irq_table[_idsel][pin-1];	\
   _ctl_; })

#define INTA	IRQ_IQ80321_INTA
#define INTB	IRQ_IQ80321_INTB
#define INTC	IRQ_IQ80321_INTC
#define INTD	IRQ_IQ80321_INTD

#define INTE	IRQ_IQ80321_I82544

static inline int __init
iq80321_map_irq(struct pci_dev *dev, u8 idsel, u8 pin)
{
	static int pci_irq_table[][4] = {
		/*
		 * PCI IDSEL/INTPIN->INTLINE
		 * A       B       C       D
		 */
		{INTE, INTE, INTE, INTE}, /* Gig-E */
		{-1, -1, -1, -1}, 	  /* Unused */
		{INTC, INTD, INTA, INTB}, /* PCI-X Slot */
		{-1, -1, -1, -1},
	};

	BUG_ON(pin < 1 || pin > 4);

//	return PCI_IRQ_TABLE_LOOKUP(4, 7);
	return pci_irq_table[idsel%4][pin-1];
}

static int iq80321_setup(int nr, struct pci_sys_data *sys)
{
	struct resource *res;

	if(nr != 0)
		return 0;

	res = kmalloc(sizeof(struct resource) * 2, GFP_KERNEL);
	if (!res)
		panic("PCI: unable to alloc resources");

	memset(res, 0, sizeof(struct resource) * 2);

	res[0].start = IQ80321_PCI_IO_BASE + IQ80321_PCI_IO_OFFSET;
	res[0].end   = IQ80321_PCI_IO_BASE + IQ80321_PCI_IO_SIZE - 1 + IQ80321_PCI_IO_OFFSET;
	res[0].name  = "IQ80321 PCI I/O Space";
	res[0].flags = IORESOURCE_IO;

	res[1].start = IQ80321_PCI_MEM_BASE;
	res[1].end   = IQ80321_PCI_MEM_BASE + IQ80321_PCI_MEM_SIZE;
	res[1].name  = "IQ80321 PCI Memory Space";
	res[1].flags = IORESOURCE_MEM;

	request_resource(&ioport_resource, &res[0]);
	request_resource(&iomem_resource, &res[1]);

	/*
	 * Since the IQ80321 is a slave card on a PCI backplane,
	 * it uses BAR1 to reserve a portion of PCI memory space for
	 * use with the private devices on the secondary bus
	 * (GigE and PCI-X slot). We read BAR1 and configure
	 * our outbound translation windows to target that
	 * address range and assign all devices in that
	 * address range. W/O this, certain BIOSes will fail
	 * to boot as the IQ80321 claims addresses that are
	 * in use by other devices.
	 *
	 * Note that the same cannot be done  with I/O space,
	 * so hopefully the host will stick to the lower 64K for
	 * PCI I/O and leave us alone.
	 */
	sys->mem_offset = IQ80321_PCI_MEM_BASE -
		(*IOP321_IABAR1 & PCI_BASE_ADDRESS_MEM_MASK);

	sys->resource[0] = &res[0];
	sys->resource[1] = &res[1];
	sys->resource[2] = NULL;
	sys->io_offset   = IQ80321_PCI_IO_OFFSET;

	iop3xx_pcibios_min_io = IQ80321_PCI_IO_BASE;
	iop3xx_pcibios_min_mem = IQ80321_PCI_MEM_BASE;

	return 1;
}

static void iq80321_preinit(void)
{
	iop321_init();
}

static struct hw_pci iq80321_pci __initdata = {
	.swizzle	= pci_std_swizzle,
	.nr_controllers = 1,
	.setup		= iq80321_setup,
	.scan		= iop321_scan_bus,
	.preinit	= iq80321_preinit,
	.map_irq	= iq80321_map_irq
};

static int __init iq80321_pci_init(void)
{
	if (machine_is_iq80321())
		pci_common_init(&iq80321_pci);
	return 0;
}

subsys_initcall(iq80321_pci_init);




