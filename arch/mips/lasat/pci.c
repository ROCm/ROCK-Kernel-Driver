#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/bootinfo.h>

extern struct pci_ops nile4_pci_ops;
extern struct pci_ops gt64120_pci_ops;

void __init pcibios_init(void)
{
	struct pci_ops *pci_ops;

	switch (mips_machtype) {
	case MACH_LASAT_100:
		pci_ops = &gt64120_pci_ops;
		break;
	case MACH_LASAT_200:
		pci_ops = &nile4_pci_ops;
		break;
	default:
		panic("pcibios_init: mips_machtype incorrect");
	}

	pci_scan_bus(0, pci_ops, NULL);
}
