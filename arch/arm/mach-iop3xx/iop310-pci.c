/*
 * arch/arm/mach-iop3xx/iop310-pci.c
 *
 * PCI support for the Intel IOP310 chipset
 *
 * Matt Porter <mporter@mvista.com>
 *
 * Copyright (C) 2001 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/ioport.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/hardware.h>
#include <asm/mach/pci.h>

#include <asm/arch/iop310.h>

/*
 *    *** Special note - why the IOP310 should NOT be used ***
 *
 * The PCI ATU is a brain dead implementation, only allowing 32-bit
 * accesses to PCI configuration space.  This is especially brain
 * dead for writes to this space.  A simple for-instance:
 *
 *  You want to modify the command register *without* corrupting the
 *  status register.
 *
 *  To perform this, you need to read *32* bits of data from offset 4,
 *  mask off the low 16, replace them with the new data, and write *32*
 *  bits back.
 *
 *  Writing the status register at offset 6 with status bits set *clears*
 *  the status.
 *
 * Hello?  Could we have a *SANE* implementation of a PCI ATU some day
 * *PLEASE*?
 */
#undef DEBUG
#ifdef DEBUG
#define  DBG(x...) printk(x)
#else
#define  DBG(x...) do { } while (0)
#endif

/*
 * Calculate the address, etc from the bus, devfn and register
 * offset.  Note that we have two root buses, so we need some
 * method to determine whether we need config type 0 or 1 cycles.
 * We use a root bus number in our bus->sysdata structure for this.
 */
static u32 iop310_cfg_address(struct pci_bus *bus, int devfn, int where)
{
	struct pci_sys_data *sys = bus->sysdata;
	u32 addr;

	if (sys->busnr == bus->number)
		addr = 1 << (PCI_SLOT(devfn) + 16);
	else
		addr = bus->number << 16 | PCI_SLOT(devfn) << 11 | 1;

	addr |=	PCI_FUNC(devfn) << 8 | (where & ~3);

	return addr;
}

/*
 * Primary PCI interface support.
 */
static int iop310_pri_pci_status(void)
{
	unsigned int status;
	int ret = 0;

	status = *IOP310_PATUSR;
	if (status & 0xf900) {
		*IOP310_PATUSR = status & 0xf900;
		ret = 1;
	}
	status = *IOP310_PATUISR;
	if (status & 0x0000018f) {
		*IOP310_PATUISR = status & 0x0000018f;
		ret = 1;
	}
	status = *IOP310_PSR;
	if (status & 0xf900) {
		*IOP310_PSR = status & 0xf900;
		ret = 1;
	}
	status = *IOP310_PBISR;
	if (status & 0x003f) {
		*IOP310_PBISR = status & 0x003f;
		ret = 1;
	}
	return ret;
}

/*
 * Simply write the address register and read the configuration
 * data.  Note that the 4 nop's ensure that we are able to handle
 * a delayed abort (in theory.)
 */
static inline u32 iop310_pri_read(unsigned long addr)
{
	u32 val;

	__asm__ __volatile__(
		"str	%1, [%2]\n\t"
		"ldr	%0, [%3]\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		: "=r" (val)
		: "r" (addr), "r" (IOP310_POCCAR), "r" (IOP310_POCCDR));

	return val;
}

static int
iop310_pri_read_config(struct pci_bus *bus, unsigned int devfn, int where,
		       int size, u32 *value)
{
	unsigned long addr = iop310_cfg_address(bus, devfn, where);
	u32 val = iop310_pri_read(addr) >> ((where & 3) * 8);

	if (iop310_pri_pci_status())
		val = 0xffffffff;

	*value = val;

	return PCIBIOS_SUCCESSFUL;
}

static int
iop310_pri_write_config(struct pci_bus *bus, unsigned int devfn, int where,
			int size, u32 value)
{
	unsigned long addr = iop310_cfg_address(bus, devfn, where);
	u32 val;

	if (size != 4) {
		val = iop310_pri_read(addr);
		if (!iop310_pri_pci_status() == 0)
			return PCIBIOS_SUCCESSFUL;

		where = (where & 3) * 8;

		if (size == 1)
			val &= ~(0xff << where);
		else
			val &= ~(0xffff << where);

		*IOP310_POCCDR = val | value << where;
	} else {
		asm volatile(
			"str	%1, [%2]\n\t"
			"str	%0, [%3]\n\t"
			"nop\n\t"
			"nop\n\t"
			"nop\n\t"
			"nop\n\t"
			:
			: "r" (value), "r" (addr),
			  "r" (IOP310_POCCAR), "r" (IOP310_POCCDR));
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops iop310_primary_ops = {
	.read	= iop310_pri_read_config,
	.write	= iop310_pri_write_config,
};

/*
 * Secondary PCI interface support.
 */
static int iop310_sec_pci_status(void)
{
	unsigned int usr, uisr;
	int ret = 0;

	usr = *IOP310_SATUSR;
	uisr = *IOP310_SATUISR;
	if (usr & 0xf900) {
		*IOP310_SATUSR = usr & 0xf900;
		ret = 1;
	}
	if (uisr & 0x0000069f) {
		*IOP310_SATUISR = uisr & 0x0000069f;
		ret = 1;
	}
	if (ret)
		DBG("ERROR (%08x %08x)", usr, uisr);
	return ret;
}

/*
 * Simply write the address register and read the configuration
 * data.  Note that the 4 nop's ensure that we are able to handle
 * a delayed abort (in theory.)
 */
static inline u32 iop310_sec_read(unsigned long addr)
{
	u32 val;

	__asm__ __volatile__(
		"str	%1, [%2]\n\t"
		"ldr	%0, [%3]\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		: "=r" (val)
		: "r" (addr), "r" (IOP310_SOCCAR), "r" (IOP310_SOCCDR));

	return val;
}

static int
iop310_sec_read_config(struct pci_bus *bus, unsigned int devfn, int where,
		       int size, u32 *value)
{
	unsigned long addr = iop310_cfg_address(bus, devfn, where);
	u32 val = iop310_sec_read(addr) >> ((where & 3) * 8);

	if (iop310_sec_pci_status())
		val = 0xffffffff;

	*value = val;

	return PCIBIOS_SUCCESSFUL;
}

static int
iop310_sec_write_config(struct pci_bus *bus, unsigned int devfn, int where,
			int size, u32 value)
{
	unsigned long addr = iop310_cfg_address(bus, devfn, where);
	u32 val;

	if (size != 4) {
		val = iop310_sec_read(addr);

		if (!iop310_sec_pci_status() == 0)
			return PCIBIOS_SUCCESSFUL;

		where = (where & 3) * 8;

		if (size == 1)
			val &= ~(0xff << where);
		else
			val &= ~(0xffff << where);

		*IOP310_SOCCDR = val | value << where;
	} else {
		asm volatile(
			"str	%1, [%2]\n\t"
			"str	%0, [%3]\n\t"
			"nop\n\t"
			"nop\n\t"
			"nop\n\t"
			"nop\n\t"
			:
			: "r" (value), "r" (addr),
			  "r" (IOP310_SOCCAR), "r" (IOP310_SOCCDR));
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops iop310_secondary_ops = {
	.read	= iop310_sec_read_config,
	.write	= iop310_sec_write_config,
};

/*
 * When a PCI device does not exist during config cycles, the 80200 gets
 * an external abort instead of returning 0xffffffff.  If it was an
 * imprecise abort, we need to correct the return address to point after
 * the instruction.  Also note that the Xscale manual says:
 *
 *  "if a stall-until-complete LD or ST instruction triggers an
 *  imprecise fault, then that fault will be seen by the program
 *  within 3 instructions."
 *
 * This does not appear to be the case.  With 8 NOPs after the load, we
 * see the imprecise abort occurring on the STM of iop310_sec_pci_status()
 * which is about 10 instructions away.
 *
 * Always trust reality!
 */
static int
iop310_pci_abort(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	DBG("PCI abort: address = 0x%08lx fsr = 0x%03x PC = 0x%08lx LR = 0x%08lx\n",
		addr, fsr, regs->ARM_pc, regs->ARM_lr);

	/*
	 * If it was an imprecise abort, then we need to correct the
	 * return address to be _after_ the instruction.
	 */
	if (fsr & (1 << 10))
		regs->ARM_pc += 4;

	return 0;
}

/*
 * Scan an IOP310 PCI bus.  sys->bus defines which bus we scan.
 */
struct pci_bus *iop310_scan_bus(int nr, struct pci_sys_data *sys)
{
	struct pci_ops *ops;

	if (nr)
		ops = &iop310_secondary_ops;
	else
		ops = &iop310_primary_ops;

	return pci_scan_bus(sys->busnr, ops, sys);
}

/*
 * Setup the system data for controller 'nr'.   Return 0 if none found,
 * 1 if found, or negative error.
 *
 * We can alter:
 *  io_offset   - offset between IO resources and PCI bus BARs
 *  mem_offset  - offset between mem resources and PCI bus BARs
 *  resource[0] - parent IO resource
 *  resource[1] - parent non-prefetchable memory resource
 *  resource[2] - parent prefetchable memory resource
 *  swizzle     - bridge swizzling function
 *  map_irq     - irq mapping function
 *
 * Note that 'io_offset' and 'mem_offset' are left as zero since
 * the IOP310 doesn't attempt to perform any address translation
 * on accesses from the host to the bus.
 */
int iop310_setup(int nr, struct pci_sys_data *sys)
{
	struct resource *res;

	if (nr >= 2)
		return 0;

	res = kmalloc(sizeof(struct resource) * 2, GFP_KERNEL);
	if (!res)
		panic("PCI: unable to alloc resources");

	memset(res, 0, sizeof(struct resource) * 2);

	switch (nr) {
	case 0:
		res[0].start = IOP310_PCIPRI_LOWER_IO + 0x6e000000;
		res[0].end   = IOP310_PCIPRI_LOWER_IO + 0x6e00ffff;
		res[0].name  = "PCI IO Primary";
		res[0].flags = IORESOURCE_IO;

		res[1].start = IOP310_PCIPRI_LOWER_MEM;
		res[1].end   = IOP310_PCIPRI_LOWER_MEM + IOP310_PCI_WINDOW_SIZE;
		res[1].name  = "PCI Memory Primary";
		res[1].flags = IORESOURCE_MEM;
		break;

	case 1:
		res[0].start = IOP310_PCISEC_LOWER_IO + 0x6e000000;
		res[0].end   = IOP310_PCISEC_LOWER_IO + 0x6e00ffff;
		res[0].name  = "PCI IO Secondary";
		res[0].flags = IORESOURCE_IO;

		res[1].start = IOP310_PCISEC_LOWER_MEM;
		res[1].end   = IOP310_PCISEC_LOWER_MEM + IOP310_PCI_WINDOW_SIZE;
		res[1].name  = "PCI Memory Secondary";
		res[1].flags = IORESOURCE_MEM;
		break;
	}

	request_resource(&ioport_resource, &res[0]);
	request_resource(&iomem_resource, &res[1]);

	sys->resource[0] = &res[0];
	sys->resource[1] = &res[1];
	sys->resource[2] = NULL;
	sys->io_offset   = 0x6e000000;

	return 1;
}

void iop310_init(void)
{
	DBG("PCI:  Intel 80312 PCI-to-PCI init code.\n");
	DBG("  ATU secondary: ATUCR =0x%08x\n", *IOP310_ATUCR);
	DBG("  ATU secondary: SOMWVR=0x%08x  SOIOWVR=0x%08x\n",
		*IOP310_SOMWVR,	*IOP310_SOIOWVR);
	DBG("  ATU secondary: SIABAR=0x%08x  SIALR  =0x%08x SIATVR=%08x\n",
		*IOP310_SIABAR, *IOP310_SIALR, *IOP310_SIATVR);
	DBG("  ATU primary:   POMWVR=0x%08x  POIOWVR=0x%08x\n",
		*IOP310_POMWVR,	*IOP310_POIOWVR);
	DBG("  ATU primary:   PIABAR=0x%08x  PIALR  =0x%08x PIATVR=%08x\n",
		*IOP310_PIABAR, *IOP310_PIALR, *IOP310_PIATVR);
	DBG("  P2P: PCR=0x%04x BCR=0x%04x EBCR=0x%04x\n",
		*IOP310_PCR, *IOP310_BCR, *IOP310_EBCR);

	/*
	 * Windows have to be carefully opened via a nice set of calls
	 * here or just some direct register fiddling in the board
	 * specific init when we want transactions to occur between the
	 * two PCI hoses.
	 *
	 * To do this, we will have manage RETRY assertion between the
	 * firmware and the kernel.  This will ensure that the host
	 * system's enumeration code is held off until we have tweaked
	 * the interrupt routing and public/private IDSELs.
	 *
	 * For now we will simply default to disabling the integrated type
	 * 81 P2P bridge.
	 */
	*IOP310_PCR &= 0xfff8;

	hook_fault_code(16+6, iop310_pci_abort, SIGBUS, "imprecise external abort");
}
