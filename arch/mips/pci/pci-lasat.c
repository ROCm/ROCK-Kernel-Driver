#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/bootinfo.h>

#include <asm/lasat/lasat.h>
#include <asm/gt64120.h>
#include <asm/nile4.h>

#define PCI_ACCESS_READ  0
#define PCI_ACCESS_WRITE 1

#undef DEBUG_PCI
#ifdef DEBUG_PCI
#define Dprintk(fmt...) printk(fmt)
#else
#define Dprintk(fmt...)
#endif

static int (*lasat_pcibios_config_access) (unsigned char access_type,
					   struct pci_bus * bus,
					   unsigned int devfn, int where,
					   u32 * val);

/*
 * Because of an error/peculiarity in the Galileo chip, we need to swap the 
 * bytes when running bigendian.
 */
#define GT_WRITE(ofs, data)  \
             *(volatile u32 *)(LASAT_GT_BASE+ofs) = cpu_to_le32(data)
#define GT_READ(ofs, data)   \
             data = le32_to_cpu(*(volatile u32 *)(LASAT_GT_BASE+ofs))


static int lasat_pcibios_config_access_100(unsigned char access_type,
					   struct pci_bus *bus,
					   unsigned int devfn, int where,
					   u32 * val)
{
	unsigned char busnum = bus->number;
	u32 intr;

	if ((busnum == 0) && (devfn >= PCI_DEVFN(31, 0)))
		return -1;	/* Because of a bug in the Galileo (for slot 31). */

	/* Clear cause register bits */
	GT_WRITE(GT_INTRCAUSE_OFS, ~(GT_INTRCAUSE_MASABORT0_BIT |
				     GT_INTRCAUSE_TARABORT0_BIT));

	/* Setup address */
	GT_WRITE(GT_PCI0_CFGADDR_OFS,
		 (busnum << GT_PCI0_CFGADDR_BUSNUM_SHF) |
		 (devfn << GT_PCI0_CFGADDR_FUNCTNUM_SHF) |
		 ((where / 4) << GT_PCI0_CFGADDR_REGNUM_SHF) |
		 GT_PCI0_CFGADDR_CONFIGEN_BIT);

	if (access_type == PCI_ACCESS_WRITE) {
		GT_WRITE(GT_PCI0_CFGDATA_OFS, *val);
	} else {
		GT_READ(GT_PCI0_CFGDATA_OFS, *val);
	}

	/* Check for master or target abort */
	GT_READ(GT_INTRCAUSE_OFS, intr);

	if (intr &
	    (GT_INTRCAUSE_MASABORT0_BIT | GT_INTRCAUSE_TARABORT0_BIT)) {
		/* Error occurred */

		/* Clear bits */
		GT_WRITE(GT_INTRCAUSE_OFS, ~(GT_INTRCAUSE_MASABORT0_BIT |
					     GT_INTRCAUSE_TARABORT0_BIT));

		return -1;
	}

	return 0;
}

#define LO(reg) (reg / 4)
#define HI(reg) (reg / 4 + 1)

volatile unsigned long *const vrc_pciregs = (void *) Vrc5074_BASE;

static int lasat_pcibios_config_access_200(unsigned char access_type,
					   struct pci_bus *bus,
					   unsigned int devfn, int where,
					   u32 * val)
{
	unsigned char busnum = bus->number;
	u32 adr, mask, err;

	if ((busnum == 0) && (PCI_SLOT(devfn) > 8))
		/* The addressing scheme chosen leaves room for just
		 * 8 devices on the first busnum (besides the PCI
		 * controller itself) */
		return -1;

	if ((busnum == 0) && (devfn == PCI_DEVFN(0, 0))) {
		/* Access controller registers directly */
		if (access_type == PCI_ACCESS_WRITE) {
			vrc_pciregs[(0x200 + where) >> 2] = *val;
		} else {
			*val = vrc_pciregs[(0x200 + where) >> 2];
		}
		return 0;
	}

	/* Temporarily map PCI Window 1 to config space */
	mask = vrc_pciregs[LO(NILE4_PCIINIT1)];
	vrc_pciregs[LO(NILE4_PCIINIT1)] =
	    0x0000001a | (busnum ? 0x200 : 0);

	/* Clear PCI Error register. This also clears the Error Type
	 * bits in the Control register */
	vrc_pciregs[LO(NILE4_PCIERR)] = 0;
	vrc_pciregs[HI(NILE4_PCIERR)] = 0;

	/* Setup address */
	if (busnum == 0)
		adr =
		    KSEG1ADDR(PCI_WINDOW1) +
		    ((1 << (PCI_SLOT(devfn) + 15)) | (PCI_FUNC(devfn) << 8)
		     | (where & ~3));
	else
		adr =
		    KSEG1ADDR(PCI_WINDOW1) | (busnum << 16) | (devfn << 8)
		    | (where & ~3);

#ifdef DEBUG_PCI
	printk("PCI config %s: adr %x",
	       access_type == PCI_ACCESS_WRITE ? "write" : "read", adr);
#endif

	if (access_type == PCI_ACCESS_WRITE) {
		*(u32 *) adr = *val;
	} else {
		*val = *(u32 *) adr;
	}

#ifdef DEBUG_PCI
	printk(" value = %x\n", *val);
#endif

	/* Check for master or target abort */
	err = (vrc_pciregs[HI(NILE4_PCICTRL)] >> 5) & 0x7;

	/* Restore PCI Window 1 */
	vrc_pciregs[LO(NILE4_PCIINIT1)] = mask;

	if (err) {
		/* Error occured */
#ifdef DEBUG_PCI
		printk("\terror %x at adr %x\n", err,
		       vrc_pciregs[LO(NILE4_PCIERR)]);
#endif
		return -1;
	}

	return 0;
}

static int lasat_pcibios_read(struct pci_bus *bus, unsigned int devfn,
			      int where, int size, u32 * val)
{
	u32 data = 0;

	if ((size == 2) && (where & 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;
	else if ((size == 4) && (where & 3))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (lasat_pcibios_config_access(PCI_ACCESS_READ, bus, devfn, where,
					&data))
		return -1;

	if (size == 1)
		*val = (data >> ((where & 3) << 3)) & 0xff;
	else if (size == 2)
		*val = (data >> ((where & 3) << 3)) & 0xffff;
	else
		*val = data;

	return PCIBIOS_SUCCESSFUL;
}

static int lasat_pcibios_write(struct pci_bus *bus, unsigned int devfn,
			       int where, int size, u32 val)
{
	u32 data = 0;

	if ((size == 2) && (where & 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;
	else if ((size == 4) && (where & 3))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (lasat_pcibios_config_access(PCI_ACCESS_READ, bus, devfn, where,
					&data))
		return -1;

	if (size == 1)
		data = (data & ~(0xff << ((where & 3) << 3))) |
		    (val << ((where & 3) << 3));
	else if (size == 2)
		data = (data & ~(0xffff << ((where & 3) << 3))) |
		    (val << ((where & 3) << 3));
	else
		data = val;

	if (lasat_pcibios_config_access
	    (PCI_ACCESS_WRITE, bus, devfn, where, &data))
		return -1;

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops lasat_pci_ops = {
	.read = lasat_pcibios_read,
	.write = lasat_pcibios_write,
};

static int __init pcibios_init(void)
{
	switch (mips_machtype) {
	case MACH_LASAT_100:
		lasat_pcibios_config_access =
		    &lasat_pcibios_config_access_100;
		break;
	case MACH_LASAT_200:
		lasat_pcibios_config_access =
		    &lasat_pcibios_config_access_200;
		break;
	default:
		panic("pcibios_init: mips_machtype incorrect");
	}

	Dprintk("pcibios_init()\n");
	pci_scan_bus(0, &lasat_pci_ops, NULL);
	return 0;
}

subsys_initcall(pcibios_init);
