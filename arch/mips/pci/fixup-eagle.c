/*
 * arch/mips/vr41xx/nec-eagle/pci_fixup.c
 *
 * The NEC Eagle/Hawk Board specific PCI fixups.
 *
 * Author: Yoichi Yuasa <you@mvista.com, or source@mvista.com>
 *
 * 2001-2002,2004 (c) MontaVista, Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/vr41xx/eagle.h>
#include <asm/vr41xx/vrc4173.h>

/*
 * Shortcuts
 */
#define INTA	CP_INTA_IRQ
#define INTB	CP_INTB_IRQ
#define INTC	CP_INTC_IRQ
#define INTD	CP_INTD_IRQ
#define PCMCIA1	VRC4173_PCMCIA1_IRQ
#define PCMCIA2	VRC4173_PCMCIA2_IRQ
#define LAN	LANINTA_IRQ
#define SLOT	PCISLOT_IRQ

static char irq_tab_eagle[][5] __initdata = {
 [ 8] = { 0,    INTA, INTB, INTC, INTD },
 [ 9] = { 0,    INTD, INTA, INTB, INTC },
 [10] = { 0,    INTC, INTD, INTA, INTB },
 [12] = { 0, PCMCIA1,    0,    0,    0 },
 [13] = { 0, PCMCIA2,    0,    0,    0 },
 [28] = { 0,     LAN,    0,    0,    0 },
 [29] = { 0,    SLOT, INTB, INTC, INTD },
};

/*
 * This is a multifunction device.
 */
static char irq_func_tab[] __initdata = {
	VRC4173_CASCADE_IRQ,
	VRC4173_AC97_IRQ,
	VRC4173_USB_IRQ
};

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	if (slot == 30)
		return irq_func_tab[PCI_FUNC(dev->devfn)];

	return irq_tab_eagle[slot][pin];
}

struct pci_fixup pcibios_fixups[] __initdata = {
	{	.pass = 0,	},
};
