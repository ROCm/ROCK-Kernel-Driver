/*
 * direct.c - Low-level direct PCI config space access
 */

#include <linux/pci.h>
#include <linux/init.h>
#include "pci.h"

/*
 * Functions for accessing PCI configuration space with type 1 accesses
 */

#define PCI_CONF1_ADDRESS(bus, dev, fn, reg) \
	(0x80000000 | (bus << 16) | (dev << 11) | (fn << 8) | (reg & ~3))

static int pci_conf1_read (int seg, int bus, int dev, int fn, int reg, int len, u32 *value)
{
	unsigned long flags;

	if (!value || (bus > 255) || (dev > 31) || (fn > 7) || (reg > 255))
		return -EINVAL;

	spin_lock_irqsave(&pci_config_lock, flags);

	outl(PCI_CONF1_ADDRESS(bus, dev, fn, reg), 0xCF8);

	switch (len) {
	case 1:
		*value = inb(0xCFC + (reg & 3));
		break;
	case 2:
		*value = inw(0xCFC + (reg & 2));
		break;
	case 4:
		*value = inl(0xCFC);
		break;
	}

	spin_unlock_irqrestore(&pci_config_lock, flags);

	return 0;
}

static int pci_conf1_write (int seg, int bus, int dev, int fn, int reg, int len, u32 value)
{
	unsigned long flags;

	if ((bus > 255) || (dev > 31) || (fn > 7) || (reg > 255)) 
		return -EINVAL;

	spin_lock_irqsave(&pci_config_lock, flags);

	outl(PCI_CONF1_ADDRESS(bus, dev, fn, reg), 0xCF8);

	switch (len) {
	case 1:
		outb((u8)value, 0xCFC + (reg & 3));
		break;
	case 2:
		outw((u16)value, 0xCFC + (reg & 2));
		break;
	case 4:
		outl((u32)value, 0xCFC);
		break;
	}

	spin_unlock_irqrestore(&pci_config_lock, flags);

	return 0;
}


#undef PCI_CONF1_ADDRESS

static int pci_conf1_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	int result; 
	u32 data;

	if (!value) 
		return -EINVAL;

	result = pci_conf1_read(0, dev->bus->number, PCI_SLOT(dev->devfn), 
		PCI_FUNC(dev->devfn), where, 1, &data);

	*value = (u8)data;

	return result;
}

static int pci_conf1_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	int result; 
	u32 data;

	if (!value) 
		return -EINVAL;

	result = pci_conf1_read(0, dev->bus->number, PCI_SLOT(dev->devfn), 
		PCI_FUNC(dev->devfn), where, 2, &data);

	*value = (u16)data;

	return result;
}

static int pci_conf1_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	if (!value) 
		return -EINVAL;

	return pci_conf1_read(0, dev->bus->number, PCI_SLOT(dev->devfn), 
		PCI_FUNC(dev->devfn), where, 4, value);
}

static int pci_conf1_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
	return pci_conf1_write(0, dev->bus->number, PCI_SLOT(dev->devfn), 
		PCI_FUNC(dev->devfn), where, 1, value);
}

static int pci_conf1_write_config_word(struct pci_dev *dev, int where, u16 value)
{
	return pci_conf1_write(0, dev->bus->number, PCI_SLOT(dev->devfn), 
		PCI_FUNC(dev->devfn), where, 2, value);
}

static int pci_conf1_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
	return pci_conf1_write(0, dev->bus->number, PCI_SLOT(dev->devfn), 
		PCI_FUNC(dev->devfn), where, 4, value);
}

static struct pci_ops pci_direct_conf1 = {
	pci_conf1_read_config_byte,
	pci_conf1_read_config_word,
	pci_conf1_read_config_dword,
	pci_conf1_write_config_byte,
	pci_conf1_write_config_word,
	pci_conf1_write_config_dword
};


/*
 * Functions for accessing PCI configuration space with type 2 accesses
 */

#define PCI_CONF2_ADDRESS(dev, reg)	(u16)(0xC000 | (dev << 8) | reg)

static int pci_conf2_read (int seg, int bus, int dev, int fn, int reg, int len, u32 *value)
{
	unsigned long flags;

	if (!value || (bus > 255) || (dev > 31) || (fn > 7) || (reg > 255))
		return -EINVAL;

	if (dev & 0x10) 
		return PCIBIOS_DEVICE_NOT_FOUND;

	spin_lock_irqsave(&pci_config_lock, flags);

	outb((u8)(0xF0 | (fn << 1)), 0xCF8);
	outb((u8)bus, 0xCFA);

	switch (len) {
	case 1:
		*value = inb(PCI_CONF2_ADDRESS(dev, reg));
		break;
	case 2:
		*value = inw(PCI_CONF2_ADDRESS(dev, reg));
		break;
	case 4:
		*value = inl(PCI_CONF2_ADDRESS(dev, reg));
		break;
	}

	outb (0, 0xCF8);

	spin_unlock_irqrestore(&pci_config_lock, flags);

	return 0;
}

static int pci_conf2_write (int seg, int bus, int dev, int fn, int reg, int len, u32 value)
{
	unsigned long flags;

	if ((bus > 255) || (dev > 31) || (fn > 7) || (reg > 255)) 
		return -EINVAL;

	if (dev & 0x10) 
		return PCIBIOS_DEVICE_NOT_FOUND;

	spin_lock_irqsave(&pci_config_lock, flags);

	outb((u8)(0xF0 | (fn << 1)), 0xCF8);
	outb((u8)bus, 0xCFA);

	switch (len) {
	case 1:
		outb ((u8)value, PCI_CONF2_ADDRESS(dev, reg));
		break;
	case 2:
		outw ((u16)value, PCI_CONF2_ADDRESS(dev, reg));
		break;
	case 4:
		outl ((u32)value, PCI_CONF2_ADDRESS(dev, reg));
		break;
	}

	outb (0, 0xCF8);    

	spin_unlock_irqrestore(&pci_config_lock, flags);

	return 0;
}

#undef PCI_CONF2_ADDRESS

static int pci_conf2_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	int result; 
	u32 data;
	result = pci_conf2_read(0, dev->bus->number, PCI_SLOT(dev->devfn), 
		PCI_FUNC(dev->devfn), where, 1, &data);
	*value = (u8)data;
	return result;
}

static int pci_conf2_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	int result; 
	u32 data;
	result = pci_conf2_read(0, dev->bus->number, PCI_SLOT(dev->devfn), 
		PCI_FUNC(dev->devfn), where, 2, &data);
	*value = (u16)data;
	return result;
}

static int pci_conf2_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	return pci_conf2_read(0, dev->bus->number, PCI_SLOT(dev->devfn), 
		PCI_FUNC(dev->devfn), where, 4, value);
}

static int pci_conf2_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
	return pci_conf2_write(0, dev->bus->number, PCI_SLOT(dev->devfn), 
		PCI_FUNC(dev->devfn), where, 1, value);
}

static int pci_conf2_write_config_word(struct pci_dev *dev, int where, u16 value)
{
	return pci_conf2_write(0, dev->bus->number, PCI_SLOT(dev->devfn), 
		PCI_FUNC(dev->devfn), where, 2, value);
}

static int pci_conf2_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
	return pci_conf2_write(0, dev->bus->number, PCI_SLOT(dev->devfn), 
		PCI_FUNC(dev->devfn), where, 4, value);
}

static struct pci_ops pci_direct_conf2 = {
	pci_conf2_read_config_byte,
	pci_conf2_read_config_word,
	pci_conf2_read_config_dword,
	pci_conf2_write_config_byte,
	pci_conf2_write_config_word,
	pci_conf2_write_config_dword
};


/*
 * Before we decide to use direct hardware access mechanisms, we try to do some
 * trivial checks to ensure it at least _seems_ to be working -- we just test
 * whether bus 00 contains a host bridge (this is similar to checking
 * techniques used in XFree86, but ours should be more reliable since we
 * attempt to make use of direct access hints provided by the PCI BIOS).
 *
 * This should be close to trivial, but it isn't, because there are buggy
 * chipsets (yes, you guessed it, by Intel and Compaq) that have no class ID.
 */
static int __devinit pci_sanity_check(struct pci_ops *o)
{
	u16 x;
	struct pci_bus bus;		/* Fake bus and device */
	struct pci_dev dev;

	if (pci_probe & PCI_NO_CHECKS)
		return 1;
	bus.number = 0;
	dev.bus = &bus;
	for(dev.devfn=0; dev.devfn < 0x100; dev.devfn++)
		if ((!o->read_word(&dev, PCI_CLASS_DEVICE, &x) &&
		     (x == PCI_CLASS_BRIDGE_HOST || x == PCI_CLASS_DISPLAY_VGA)) ||
		    (!o->read_word(&dev, PCI_VENDOR_ID, &x) &&
		     (x == PCI_VENDOR_ID_INTEL || x == PCI_VENDOR_ID_COMPAQ)))
			return 1;
	DBG("PCI: Sanity check failed\n");
	return 0;
}

static struct pci_ops * __devinit pci_check_direct(void)
{
	unsigned int tmp;
	unsigned long flags;

	local_irq_save(flags);

	/*
	 * Check if configuration type 1 works.
	 */
	if (pci_probe & PCI_PROBE_CONF1) {
		outb (0x01, 0xCFB);
		tmp = inl (0xCF8);
		outl (0x80000000, 0xCF8);
		if (inl (0xCF8) == 0x80000000 &&
		    pci_sanity_check(&pci_direct_conf1)) {
			outl (tmp, 0xCF8);
			local_irq_restore(flags);
			printk(KERN_INFO "PCI: Using configuration type 1\n");
			if (!request_region(0xCF8, 8, "PCI conf1"))
				return NULL;
			return &pci_direct_conf1;
		}
		outl (tmp, 0xCF8);
	}

	/*
	 * Check if configuration type 2 works.
	 */
	if (pci_probe & PCI_PROBE_CONF2) {
		outb (0x00, 0xCFB);
		outb (0x00, 0xCF8);
		outb (0x00, 0xCFA);
		if (inb (0xCF8) == 0x00 && inb (0xCFA) == 0x00 &&
		    pci_sanity_check(&pci_direct_conf2)) {
			local_irq_restore(flags);
			printk(KERN_INFO "PCI: Using configuration type 2\n");
			if (!request_region(0xCF8, 4, "PCI conf2"))
				return NULL;
			return &pci_direct_conf2;
		}
	}

	local_irq_restore(flags);
	return NULL;
}

static int __init pci_direct_init(void)
{
	if ((pci_probe & (PCI_PROBE_CONF1 | PCI_PROBE_CONF2)) 
		&& (pci_root_ops = pci_check_direct())) {
		if (pci_root_ops == &pci_direct_conf1) {
			pci_config_read = pci_conf1_read;
			pci_config_write = pci_conf1_write;
		}
		else {
			pci_config_read = pci_conf2_read;
			pci_config_write = pci_conf2_write;
		}
	}
	return 0;
}

arch_initcall(pci_direct_init);
