/*
 * Exceptions for specific devices. Usually work-arounds for fatal design flaws.
 */

#include <linux/pci.h>
#include <linux/init.h>
#include "pci.h"


static void __devinit pci_fixup_i450nx(struct pci_dev *d)
{
	/*
	 * i450NX -- Find and scan all secondary buses on all PXB's.
	 */
	int pxb, reg;
	u8 busno, suba, subb;

	printk(KERN_WARNING "PCI: Searching for i450NX host bridges on %s\n", pci_name(d));
	reg = 0xd0;
	for(pxb=0; pxb<2; pxb++) {
		pci_read_config_byte(d, reg++, &busno);
		pci_read_config_byte(d, reg++, &suba);
		pci_read_config_byte(d, reg++, &subb);
		DBG("i450NX PXB %d: %02x/%02x/%02x\n", pxb, busno, suba, subb);
		if (busno)
			pci_scan_bus(busno, &pci_root_ops, NULL);	/* Bus A */
		if (suba < subb)
			pci_scan_bus(suba+1, &pci_root_ops, NULL);	/* Bus B */
	}
	pcibios_last_bus = -1;
}

static void __devinit pci_fixup_i450gx(struct pci_dev *d)
{
	/*
	 * i450GX and i450KX -- Find and scan all secondary buses.
	 * (called separately for each PCI bridge found)
	 */
	u8 busno;
	pci_read_config_byte(d, 0x4a, &busno);
	printk(KERN_INFO "PCI: i440KX/GX host bridge %s: secondary bus %02x\n", pci_name(d), busno);
	pci_scan_bus(busno, &pci_root_ops, NULL);
	pcibios_last_bus = -1;
}

static void __devinit  pci_fixup_umc_ide(struct pci_dev *d)
{
	/*
	 * UM8886BF IDE controller sets region type bits incorrectly,
	 * therefore they look like memory despite of them being I/O.
	 */
	int i;

	printk(KERN_WARNING "PCI: Fixing base address flags for device %s\n", pci_name(d));
	for(i=0; i<4; i++)
		d->resource[i].flags |= PCI_BASE_ADDRESS_SPACE_IO;
}

static void __devinit  pci_fixup_ncr53c810(struct pci_dev *d)
{
	/*
	 * NCR 53C810 returns class code 0 (at least on some systems).
	 * Fix class to be PCI_CLASS_STORAGE_SCSI
	 */
	if (!d->class) {
		printk(KERN_WARNING "PCI: fixing NCR 53C810 class code for %s\n", pci_name(d));
		d->class = PCI_CLASS_STORAGE_SCSI << 8;
	}
}

static void __devinit pci_fixup_ide_bases(struct pci_dev *d)
{
	int i;

	/*
	 * PCI IDE controllers use non-standard I/O port decoding, respect it.
	 */
	if ((d->class >> 8) != PCI_CLASS_STORAGE_IDE)
		return;
	DBG("PCI: IDE base address fixup for %s\n", pci_name(d));
	for(i=0; i<4; i++) {
		struct resource *r = &d->resource[i];
		if ((r->start & ~0x80) == 0x374) {
			r->start |= 2;
			r->end = r->start;
		}
	}
}

static void __devinit  pci_fixup_ide_trash(struct pci_dev *d)
{
	int i;

	/*
	 * There exist PCI IDE controllers which have utter garbage
	 * in first four base registers. Ignore that.
	 */
	DBG("PCI: IDE base address trash cleared for %s\n", pci_name(d));
	for(i=0; i<4; i++)
		d->resource[i].start = d->resource[i].end = d->resource[i].flags = 0;
}

static void __devinit  pci_fixup_latency(struct pci_dev *d)
{
	/*
	 *  SiS 5597 and 5598 chipsets require latency timer set to
	 *  at most 32 to avoid lockups.
	 */
	DBG("PCI: Setting max latency to 32\n");
	pcibios_max_latency = 32;
}

static void __devinit pci_fixup_piix4_acpi(struct pci_dev *d)
{
	/*
	 * PIIX4 ACPI device: hardwired IRQ9
	 */
	d->irq = 9;
}

/*
 * Addresses issues with problems in the memory write queue timer in
 * certain VIA Northbridges.  This bugfix is per VIA's specifications,
 * except for the KL133/KM133: clearing bit 5 on those Northbridges seems
 * to trigger a bug in its integrated ProSavage video card, which
 * causes screen corruption.  We only clear bits 6 and 7 for that chipset,
 * until VIA can provide us with definitive information on why screen
 * corruption occurs, and what exactly those bits do.
 *
 * VIA 8363,8622,8361 Northbridges:
 *  - bits  5, 6, 7 at offset 0x55 need to be turned off
 * VIA 8367 (KT266x) Northbridges:
 *  - bits  5, 6, 7 at offset 0x95 need to be turned off
 * VIA 8363 rev 0x81/0x84 (KL133/KM133) Northbridges:
 *  - bits     6, 7 at offset 0x55 need to be turned off
 */

#define VIA_8363_KL133_REVISION_ID 0x81
#define VIA_8363_KM133_REVISION_ID 0x84

static void __devinit pci_fixup_via_northbridge_bug(struct pci_dev *d)
{
	u8 v;
	u8 revision;
	int where = 0x55;
	int mask = 0x1f; /* clear bits 5, 6, 7 by default */

	pci_read_config_byte(d, PCI_REVISION_ID, &revision);

	if (d->device == PCI_DEVICE_ID_VIA_8367_0) {
		/* fix pci bus latency issues resulted by NB bios error
		   it appears on bug free^Wreduced kt266x's bios forces
		   NB latency to zero */
		pci_write_config_byte(d, PCI_LATENCY_TIMER, 0);

		where = 0x95; /* the memory write queue timer register is 
				different for the KT266x's: 0x95 not 0x55 */
	} else if (d->device == PCI_DEVICE_ID_VIA_8363_0 &&
			(revision == VIA_8363_KL133_REVISION_ID || 
			revision == VIA_8363_KM133_REVISION_ID)) {
			mask = 0x3f; /* clear only bits 6 and 7; clearing bit 5
					causes screen corruption on the KL133/KM133 */
	}

	pci_read_config_byte(d, where, &v);
	if (v & ~mask) {
		printk(KERN_WARNING "Disabling VIA memory write queue (PCI ID %04x, rev %02x): [%02x] %02x & %02x -> %02x\n", \
			d->device, revision, where, v, mask, v & mask);
		v &= mask;
		pci_write_config_byte(d, where, v);
	}
}

/*
 * For some reasons Intel decided that certain parts of their
 * 815, 845 and some other chipsets must look like PCI-to-PCI bridges
 * while they are obviously not. The 82801 family (AA, AB, BAM/CAM,
 * BA/CA/DB and E) PCI bridges are actually HUB-to-PCI ones, according
 * to Intel terminology. These devices do forward all addresses from
 * system to PCI bus no matter what are their window settings, so they are
 * "transparent" (or subtractive decoding) from programmers point of view.
 */
static void __devinit pci_fixup_transparent_bridge(struct pci_dev *dev)
{
	if ((dev->class >> 8) == PCI_CLASS_BRIDGE_PCI &&
	    (dev->device & 0xff00) == 0x2400)
		dev->transparent = 1;
}

/*
 * Fixup for C1 Halt Disconnect problem on nForce2 systems.
 *
 * From information provided by "Allen Martin" <AMartin@nvidia.com>:
 *
 * A hang is caused when the CPU generates a very fast CONNECT/HALT cycle
 * sequence.  Workaround is to set the SYSTEM_IDLE_TIMEOUT to 80 ns.
 * This allows the state-machine and timer to return to a proper state within
 * 80 ns of the CONNECT and probe appearing together.  Since the CPU will not
 * issue another HALT within 80 ns of the initial HALT, the failure condition
 * is avoided.
 */
static void __init pci_fixup_nforce2(struct pci_dev *dev)
{
	u32 val, fixed_val;
	u8 rev;

	pci_read_config_byte(dev, PCI_REVISION_ID, &rev);

	/*
	 * Chip  Old value   New value
	 * C17   0x1F0FFF01  0x1F01FF01
	 * C18D  0x9F0FFF01  0x9F01FF01
	 *
	 * Northbridge chip version may be determined by
	 * reading the PCI revision ID (0xC1 or greater is C18D).
	 */
	fixed_val = rev < 0xC1 ? 0x1F01FF01 : 0x9F01FF01;

	pci_read_config_dword(dev, 0x6c, &val);
	if (val != fixed_val) {
		printk(KERN_WARNING "PCI: nForce2 C1 Halt Disconnect fixup\n");
		pci_write_config_dword(dev, 0x6c, fixed_val);
	}
}

struct pci_fixup pcibios_fixups[] = {
	{
		.pass		= PCI_FIXUP_HEADER,
		.vendor		= PCI_VENDOR_ID_INTEL,
		.device		= PCI_DEVICE_ID_INTEL_82451NX,
		.hook		= pci_fixup_i450nx
	},
	{
		.pass		= PCI_FIXUP_HEADER,
		.vendor		= PCI_VENDOR_ID_INTEL,
		.device		= PCI_DEVICE_ID_INTEL_82454GX,
		.hook		= pci_fixup_i450gx
	},
	{
		.pass		= PCI_FIXUP_HEADER,
		.vendor		= PCI_VENDOR_ID_UMC,
		.device		= PCI_DEVICE_ID_UMC_UM8886BF,
		.hook		= pci_fixup_umc_ide
	},
	{
		.pass		= PCI_FIXUP_HEADER,
		.vendor		= PCI_VENDOR_ID_SI,
		.device		= PCI_DEVICE_ID_SI_5513,
		.hook		= pci_fixup_ide_trash
	},
	{
		.pass		= PCI_FIXUP_HEADER,
		.vendor		= PCI_ANY_ID,
		.device		= PCI_ANY_ID,
		.hook		= pci_fixup_ide_bases
	},
	{
		.pass		= PCI_FIXUP_HEADER,
		.vendor		= PCI_VENDOR_ID_SI,
		.device		= PCI_DEVICE_ID_SI_5597,
		.hook		= pci_fixup_latency
	},
	{
		.pass		= PCI_FIXUP_HEADER,
		.vendor		= PCI_VENDOR_ID_SI,
		.device		= PCI_DEVICE_ID_SI_5598,
		.hook		= pci_fixup_latency
	},
 	{
		.pass		= PCI_FIXUP_HEADER,
		.vendor		= PCI_VENDOR_ID_INTEL,
		.device		= PCI_DEVICE_ID_INTEL_82371AB_3,
		.hook		= pci_fixup_piix4_acpi
	},
 	{
		.pass		= PCI_FIXUP_HEADER,
		.vendor		= PCI_VENDOR_ID_INTEL,
		.device		= PCI_DEVICE_ID_INTEL_82801CA_10,
		.hook		= pci_fixup_ide_trash
	},
 	{
		.pass		= PCI_FIXUP_HEADER,
		.vendor		= PCI_VENDOR_ID_INTEL,
		.device		= PCI_DEVICE_ID_INTEL_82801CA_11,
		.hook		= pci_fixup_ide_trash
	},
 	{
		.pass		= PCI_FIXUP_HEADER,
		.vendor		= PCI_VENDOR_ID_INTEL,
		.device		= PCI_DEVICE_ID_INTEL_82801DB_9,
		.hook		= pci_fixup_ide_trash
	},
	{
		.pass		= PCI_FIXUP_HEADER,
		.vendor		= PCI_VENDOR_ID_VIA,
		.device		= PCI_DEVICE_ID_VIA_8363_0,
		.hook		= pci_fixup_via_northbridge_bug
	},
	{
		.pass		= PCI_FIXUP_HEADER,
		.vendor		= PCI_VENDOR_ID_VIA,
		.device		= PCI_DEVICE_ID_VIA_8622,
		.hook		= pci_fixup_via_northbridge_bug
	},
	{
		.pass		= PCI_FIXUP_HEADER,
		.vendor		= PCI_VENDOR_ID_VIA,
		.device		= PCI_DEVICE_ID_VIA_8361,
		.hook		= pci_fixup_via_northbridge_bug
	},
	{
		.pass		= PCI_FIXUP_HEADER,
		.vendor		= PCI_VENDOR_ID_VIA,
		.device		= PCI_DEVICE_ID_VIA_8367_0,
		.hook		= pci_fixup_via_northbridge_bug
	},
	{
		.pass		= PCI_FIXUP_HEADER,
		.vendor		= PCI_VENDOR_ID_NCR,
		.device		= PCI_DEVICE_ID_NCR_53C810,
		.hook		= pci_fixup_ncr53c810
	},
	{
		.pass		= PCI_FIXUP_HEADER,
		.vendor		= PCI_VENDOR_ID_INTEL,
		.device		= PCI_ANY_ID,
		.hook		= pci_fixup_transparent_bridge
	},
	{
		.pass		= PCI_FIXUP_HEADER,
		.vendor		= PCI_VENDOR_ID_NVIDIA,
		.device		= PCI_DEVICE_ID_NVIDIA_NFORCE2,
		.hook		= pci_fixup_nforce2
	},
	{ .pass = 0 }
};
