/*
 * arch/arm/mach-iop310/iop310-pci.c
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
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
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
#define  DBG(x...)
#endif

extern int (*external_fault)(unsigned long, struct pt_regs *);

static u32 iop310_cfg_address(struct pci_dev *dev, int where)
{
	struct pci_sys_data *sys = dev->sysdata;
	u32 addr;

	where &= ~3;

	if (sys->busnr == dev->bus->number)
		addr = 1 << (PCI_SLOT(dev->devfn) + 16);
	else
		addr = dev->bus->number << 16 |
		       PCI_SLOT(dev->devfn) << 11 | 1;

	addr |=	PCI_FUNC(dev->devfn) << 8 | where;

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

static int
iop310_pri_rd_cfg_byte(struct pci_dev *dev, int where, u8 *p)
{
	int ret;
	u8 val;

	*IOP310_POCCAR = iop310_cfg_address(dev, where);

	val = (*IOP310_POCCDR) >> ((where & 3) * 8);
	__asm__ __volatile__("nop; nop; nop; nop");

	ret = iop310_pri_pci_status();
	if (ret)
		val = 0xff;

	*p = val;

	return PCIBIOS_SUCCESSFUL;
}

static int
iop310_pri_rd_cfg_word(struct pci_dev *dev, int where, u16 *p)
{
	int ret;
	u16 val;

	*IOP310_POCCAR = iop310_cfg_address(dev, where);

	val = (*IOP310_POCCDR) >> ((where & 2) * 8);
	__asm__ __volatile__("nop; nop; nop; nop");

	ret = iop310_pri_pci_status();
	if (ret)
		val = 0xffff;

	*p = val;

	return PCIBIOS_SUCCESSFUL;
}

static int
iop310_pri_rd_cfg_dword(struct pci_dev *dev, int where, u32 *p)
{
	int ret;
	u32 val;

	*IOP310_POCCAR = iop310_cfg_address(dev, where);

	val = *IOP310_POCCDR;
	__asm__ __volatile__("nop; nop; nop; nop");

	ret = iop310_pri_pci_status();
	if (ret)
		val = 0xffffffff;

	*p = val;

	return PCIBIOS_SUCCESSFUL;
}

static int
iop310_pri_wr_cfg_byte(struct pci_dev *dev, int where, u8 v)
{
	int ret;
	u32 val;

	*IOP310_POCCAR = iop310_cfg_address(dev, where);

	val = *IOP310_POCCDR;
	__asm__ __volatile__("nop; nop; nop; nop");

	ret = iop310_pri_pci_status();
	if (ret == 0) {
		where = (where & 3) * 8;
		val &= ~(0xff << where);
		val |= v << where;
		*IOP310_POCCDR = val;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int
iop310_pri_wr_cfg_word(struct pci_dev *dev, int where, u16 v)
{
	int ret;
	u32 val;

	*IOP310_POCCAR = iop310_cfg_address(dev, where);

	val = *IOP310_POCCDR;
	__asm__ __volatile__("nop; nop; nop; nop");

	ret = iop310_pri_pci_status();
	if (ret == 0) {
		where = (where & 2) * 8;
		val &= ~(0xffff << where);
		val |= v << where;
		*IOP310_POCCDR = val;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int
iop310_pri_wr_cfg_dword(struct pci_dev *dev, int where, u32 v)
{
	*IOP310_POCCAR = iop310_cfg_address(dev, where);
	*IOP310_POCCDR = v;
	__asm__ __volatile__("nop; nop; nop; nop");

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops iop310_primary_ops = {
	iop310_pri_rd_cfg_byte,
	iop310_pri_rd_cfg_word,
	iop310_pri_rd_cfg_dword,
	iop310_pri_wr_cfg_byte,
	iop310_pri_wr_cfg_word,
	iop310_pri_wr_cfg_dword,
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
//if (ret) printk("ERROR (%08lx %08lx)", usr, uisr);
	return ret;
}

static int
iop310_sec_rd_cfg_byte(struct pci_dev *dev, int where, u8 *p)
{
	int ret;
	u8 val;
//printk("rdb: %d:%02x.%x %02x ", dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn), where);
	*IOP310_SOCCAR = iop310_cfg_address(dev, where);

	val = (*IOP310_SOCCDR) >> ((where & 3) * 8);
	__asm__ __volatile__("nop; nop; nop; nop");
//printk(">= %08lx ", val);
	ret = iop310_sec_pci_status();
	if (ret)
		val = 0xff;
//printk("\n");
	*p = val;

	return PCIBIOS_SUCCESSFUL;
}

static int
iop310_sec_rd_cfg_word(struct pci_dev *dev, int where, u16 *p)
{
	int ret;
	u16 val;
//printk("rdw: %d:%02x.%x %02x ", dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn), where);
	*IOP310_SOCCAR = iop310_cfg_address(dev, where);

	val = (*IOP310_SOCCDR) >> ((where & 3) * 8);
	__asm__ __volatile__("nop; nop; nop; nop");
//printk(">= %08lx ", val);
	ret = iop310_sec_pci_status();
	if (ret)
		val = 0xffff;
//printk("\n");
	*p = val;

	return PCIBIOS_SUCCESSFUL;
}

static int
iop310_sec_rd_cfg_dword(struct pci_dev *dev, int where, u32 *p)
{
	int ret;
	u32 val;
//printk("rdl: %d:%02x.%x %02x ", dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn), where);
	*IOP310_SOCCAR = iop310_cfg_address(dev, where);

	val = *IOP310_SOCCDR;
	__asm__ __volatile__("nop; nop; nop; nop");
//printk(">= %08lx ", val);
	ret = iop310_sec_pci_status();
	if (ret)
		val = 0xffffffff;
//printk("\n");
	*p = val;

	return PCIBIOS_SUCCESSFUL;
}

static int
iop310_sec_wr_cfg_byte(struct pci_dev *dev, int where, u8 v)
{
	int ret;
	u32 val;
//printk("wrb: %d:%02x.%x %02x ", dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn), where);
	*IOP310_SOCCAR = iop310_cfg_address(dev, where);

	val = *IOP310_SOCCDR;
	__asm__ __volatile__("nop; nop; nop; nop");
//printk("<= %08lx", v);
	ret = iop310_sec_pci_status();
	if (ret == 0) {
		where = (where & 3) * 8;
		val &= ~(0xff << where);
		val |= v << where;
		*IOP310_SOCCDR = val;
	}
//printk("\n");
	return PCIBIOS_SUCCESSFUL;
}

static int
iop310_sec_wr_cfg_word(struct pci_dev *dev, int where, u16 v)
{
	int ret;
	u32 val;
//printk("wrw: %d:%02x.%x %02x ", dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn), where);
	*IOP310_SOCCAR = iop310_cfg_address(dev, where);

	val = *IOP310_SOCCDR;
	__asm__ __volatile__("nop; nop; nop; nop");
//printk("<= %08lx", v);
	ret = iop310_sec_pci_status();
	if (ret == 0) {
		where = (where & 2) * 8;
		val &= ~(0xffff << where);
		val |= v << where;
		*IOP310_SOCCDR = val;
	}
//printk("\n");
	return PCIBIOS_SUCCESSFUL;
}

static int
iop310_sec_wr_cfg_dword(struct pci_dev *dev, int where, u32 v)
{
//printk("wrl: %d:%02x.%x %02x ", dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn), where);
	*IOP310_SOCCAR = iop310_cfg_address(dev, where);
	*IOP310_SOCCDR = v;
	__asm__ __volatile__("nop; nop; nop; nop");
//printk("<= %08lx\n", v);
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops iop310_secondary_ops = {
	iop310_sec_rd_cfg_byte,
	iop310_sec_rd_cfg_word,
	iop310_sec_rd_cfg_dword,
	iop310_sec_wr_cfg_byte,
	iop310_sec_wr_cfg_word,
	iop310_sec_wr_cfg_dword,
};

/*
 * When a PCI device does not exist during config cycles, the 80200 gets a
 * bus error instead of returning 0xffffffff. This handler simply returns.
 */
int iop310_pci_abort_handler(unsigned long addr, struct pt_regs *regs)
{
//	printk("PCI abort: address = %08x PC = %08x LR = %08lx\n",
//		addr, regs->ARM_pc, regs->ARM_lr);
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

	switch (nr) {
	case 0:
		res[0].start = IOP310_PCIPRI_LOWER_IO + 0x6e000000;
		res[0].end   = IOP310_PCIPRI_LOWER_IO + 0x6e00ffff;
		res[0].name  = "PCI IO Primary";

		res[1].start = IOP310_PCIPRI_LOWER_MEM;
		res[1].end   = IOP310_PCIPRI_LOWER_MEM + IOP310_PCI_WINDOW_SIZE;
		res[1].name  = "PCI Memory Primary";
		break;

	case 1:
		res[0].start = IOP310_PCISEC_LOWER_IO + 0x6e000000;
		res[0].end   = IOP310_PCISEC_LOWER_IO + 0x6e00ffff;
		res[0].name  = "PCI IO Primary";

		res[1].start = IOP310_PCISEC_LOWER_MEM;
		res[1].end   = IOP310_PCISEC_LOWER_MEM + IOP310_PCI_WINDOW_SIZE;
		res[1].name  = "PCI Memory Primary";
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
	DBG("  ATU secondary: IOP310_SOMWVR=0x%04x, IOP310_SOIOWVR=0x%04x\n",
			*IOP310_SOMWVR,
			*IOP310_SOIOWVR);
	DBG("  ATU secondary: IOP310_ATUCR=0x%08x\n", *IOP310_ATUCR);
	DBG("  ATU secondary: IOP310_SIABAR=0x%08x IOP310_SIALR=0x%08x IOP310_SIATVR=%08x\n", *IOP310_SIABAR, *IOP310_SIALR, *IOP310_SIATVR);

	DBG("  ATU primary: IOP310_POMWVR=0x%04x, IOP310_POIOWVR=0x%04x\n",
			*IOP310_POMWVR,
			*IOP310_POIOWVR);
	DBG("  ATU secondary: IOP310_PIABAR=0x%08x IOP310_PIALR=0x%08x IOP310_PIATVR=%08x\n", *IOP310_PIABAR, *IOP310_PIALR, *IOP310_PIATVR);

	DBG("  P2P: IOP310_PCR=0x%04x IOP310_BCR=0x%04x IOP310_EBCR=0x%04x\n", *IOP310_PCR, *IOP310_BCR, *IOP310_EBCR);

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

	external_fault = iop310_pci_abort_handler;

}

