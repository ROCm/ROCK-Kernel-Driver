/*
 * linux/arch/arm/mach-footbridge/cats-pci.c
 *
 * PCI bios-type initialisation for PCI machines
 *
 * Bits taken from various places.
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/irq.h>
#include <asm/mach/pci.h>

/* cats host-specific stuff */
static int irqmap_cats[] __initdata = { IRQ_PCI, IRQ_IN0, IRQ_IN1, IRQ_IN3 };

static int __init cats_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	if (dev->irq >= 128)
		return dev->irq & 0x1f;

	if (dev->irq >= 1 && dev->irq <= 4)
		return irqmap_cats[dev->irq - 1];

	if (dev->irq != 0)
		printk("PCI: device %02x:%02x has unknown irq line %x\n",
		       dev->bus->number, dev->devfn, dev->irq);

	return -1;
}

/*
 * why not the standard PCI swizzle?  does this prevent 4-port tulip
 * cards being used (ie, pci-pci bridge based cards)?
 */
struct hw_pci cats_pci __initdata = {
	swizzle:		NULL,
	map_irq:		cats_map_irq,
	nr_controllers:		1,
	setup:			dc21285_setup,
	scan:			dc21285_scan_bus,
	preinit:		dc21285_preinit,
	postinit:		dc21285_postinit,
};
