/*
 * mmconfig.c - Low-level direct PCI config space access via MMCONFIG
 */

#include <linux/pci.h>
#include <linux/init.h>
#include "pci.h"

/* The physical address of the MMCONFIG aperture.  Set from ACPI tables. */
u32 pci_mmcfg_base_addr;

#define mmcfg_virt_addr (fix_to_virt(FIX_PCIE_MCFG))

/* The base address of the last MMCONFIG device accessed */
static u32 mmcfg_last_accessed_device;

/*
 * Functions for accessing PCI configuration space with MMCONFIG accesses
 */

static inline void pci_exp_set_dev_base(int bus, int devfn)
{
	u32 dev_base = pci_mmcfg_base_addr | (bus << 20) | (devfn << 12);
	if (dev_base != mmcfg_last_accessed_device) {
		mmcfg_last_accessed_device = dev_base;
		set_fixmap(FIX_PCIE_MCFG, dev_base);
	}
}

static int pci_mmcfg_read(int seg, int bus, int devfn, int reg, int len, u32 *value)
{
	unsigned long flags;

	if (!value || (bus > 255) || (devfn > 255) || (reg > 4095))
		return -EINVAL;

	spin_lock_irqsave(&pci_config_lock, flags);

	pci_exp_set_dev_base(bus, devfn);

	switch (len) {
	case 1:
		*value = readb(mmcfg_virt_addr + reg);
		break;
	case 2:
		*value = readw(mmcfg_virt_addr + reg);
		break;
	case 4:
		*value = readl(mmcfg_virt_addr + reg);
		break;
	}

	spin_unlock_irqrestore(&pci_config_lock, flags);

	return 0;
}

static int pci_mmcfg_write(int seg, int bus, int devfn, int reg, int len, u32 value)
{
	unsigned long flags;

	if ((bus > 255) || (devfn > 255) || (reg > 4095)) 
		return -EINVAL;

	spin_lock_irqsave(&pci_config_lock, flags);

	pci_exp_set_dev_base(bus, devfn);

	switch (len) {
	case 1:
		writeb(value, mmcfg_virt_addr + reg);
		break;
	case 2:
		writew(value, mmcfg_virt_addr + reg);
		break;
	case 4:
		writel(value, mmcfg_virt_addr + reg);
		break;
	}

	/* Dummy read to flush PCI write */
	readl(mmcfg_virt_addr);

	spin_unlock_irqrestore(&pci_config_lock, flags);

	return 0;
}

static struct pci_raw_ops pci_mmcfg = {
	.read =		pci_mmcfg_read,
	.write =	pci_mmcfg_write,
};

static int __init pci_mmcfg_init(void)
{
	if ((pci_probe & PCI_PROBE_MMCONF) == 0)
		goto out;
	if (!pci_mmcfg_base_addr)
		goto out;

	printk(KERN_INFO "PCI: Using MMCONFIG\n");
	raw_pci_ops = &pci_mmcfg;
	pci_probe = (pci_probe & ~PCI_PROBE_MASK) | PCI_PROBE_MMCONF;

 out:
	return 0;
}

arch_initcall(pci_mmcfg_init);
