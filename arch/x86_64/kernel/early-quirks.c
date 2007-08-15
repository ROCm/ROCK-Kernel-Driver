/* Various workarounds for chipset bugs.
   This code runs very early and can't use the regular PCI subsystem
   The entries are keyed to PCI bridges which usually identify chipsets
   uniquely.
   This is only for whole classes of chipsets with specific problems which
   need early invasive action (e.g. before the timers are initialized).
   Most PCI device specific workarounds can be done later and should be
   in standard PCI quirks
   Mainboard specific bugs should be handled by DMI entries.
   CPU specific bugs in setup.c */

#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/pci_ids.h>
#include <asm/pci-direct.h>
#include <asm/io_apic.h>
#include <asm/apic.h>
#include <asm/dma.h>

#ifdef CONFIG_X86_64
#include <asm/proto.h>
#endif

static void __init via_bugs(void)
{
#ifdef CONFIG_IOMMU
	if ((end_pfn > MAX_DMA32_PFN ||  force_iommu) &&
	    !iommu_aperture_allowed) {
		printk(KERN_INFO
  "Looks like a VIA chipset. Disabling IOMMU. Override with iommu=allowed\n");
		iommu_aperture_disabled = 1;
	}
#endif
}

static void __init nvidia_bugs(void)
{
#ifdef CONFIG_ACPI
#ifdef CONFIG_X86_IO_APIC
	/*
	 * All timer overrides on Nvidia NF3/NF4 are
	 * wrong.
	 */
	if (acpi_use_timer_override)
		return;

	acpi_skip_timer_override = 1;
	printk(KERN_INFO "Nvidia board detected. Ignoring ACPI timer override.\n");
	printk(KERN_INFO "If you got timer trouble try acpi_use_timer_override\n");
#endif
#endif
	/* RED-PEN skip them on mptables too? */

}

static void __init ati_bugs(void)
{
#ifndef CONFIG_XEN
#ifdef CONFIG_X86_IO_APIC
	if (timer_over_8254 == 1) {
		timer_over_8254 = 0;
		printk(KERN_INFO
	 	"ATI board detected. Disabling timer routing over 8254.\n");
	}
#endif
#endif
}

struct chipset {
	u16 vendor;
	void (*f)(void);
	int id;
};

static struct chipset early_qrk[] __initdata = {
	/* This list should cover at least one PCI ID from each NF3 or NF4
	   mainboard to handle a bug in their reference BIOS. May be incomplete. */
	{ PCI_VENDOR_ID_NVIDIA, nvidia_bugs, 0x00dd },	/* nforce 3 */
	{ PCI_VENDOR_ID_NVIDIA, nvidia_bugs, 0x00e1 },	/* nforce 3 */
	{ PCI_VENDOR_ID_NVIDIA, nvidia_bugs, 0x00ed },	/* nforce 3 */
	{ PCI_VENDOR_ID_NVIDIA, nvidia_bugs, 0x003d },	/* mcp 04 ?? */
	{ PCI_VENDOR_ID_NVIDIA, nvidia_bugs, 0x005c },	/* ck 804 */
	{ PCI_VENDOR_ID_NVIDIA, nvidia_bugs, 0x026f },	/* mcp 51 / nf4 ? */
	{ PCI_VENDOR_ID_NVIDIA, nvidia_bugs, 0x02f0 },	/* mcp 51 / nf4 ? */
	{ PCI_VENDOR_ID_VIA, via_bugs },
	{ PCI_VENDOR_ID_ATI, ati_bugs },
	{}
};

void __init early_quirks(void)
{
	int num, slot, func;

	if (!early_pci_allowed())
		return;

	/* Poor man's PCI discovery.
	   We just look for a chipset unique PCI bridge; not scan all devices */
	for (num = 0; num < 32; num++) {
		for (slot = 0; slot < 32; slot++) {
			for (func = 0; func < 8; func++) {
				u32 class;
				u32 vendor, device;
				u8 type;
				int i;
				class = read_pci_config(num,slot,func,
							PCI_CLASS_REVISION);
				if (class == 0xffffffff)
					break;

		       		if ((class >> 16) != PCI_CLASS_BRIDGE_PCI)
					continue;

				vendor = read_pci_config(num, slot, func,
							 PCI_VENDOR_ID);
				device = vendor >> 16;

				vendor &= 0xffff;

				for (i = 0; early_qrk[i].f; i++) {
					struct chipset *c = &early_qrk[i];
					if (c->vendor == vendor && (!c->id || (c->id && c->id==device))) {
						early_qrk[i].f();
						return;
					}
				}

				type = read_pci_config_byte(num, slot, func,
							    PCI_HEADER_TYPE);
				if (!(type & 0x80))
					break;
			}
		}
	}
}
