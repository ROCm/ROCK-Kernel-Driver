/*
 * FILE NAME
 *	arch/mips/vr41xx/victor-mpc30x/pci_fixup.c
 *
 * BRIEF MODULE DESCRIPTION
 *	The Victor MP-C303/304 specific PCI fixups.
 *
 * Copyright 2002 Yoichi Yuasa
 *                yuasa@hh.iij4u.or.jp
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 */
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/vr41xx/vrc4173.h>
#include <asm/vr41xx/mpc30x.h>

/*
 * Shortcuts
 */
#define PCMCIA1	VRC4173_PCMCIA1_IRQ
#define PCMCIA2	VRC4173_PCMCIA2_IRQ
#define MQ	MQ200_IRQ

static const int internal_func_irqs[8] __initdata = {
	VRC4173_CASCADE_IRQ,
	VRC4173_AC97_IRQ,
	VRC4173_USB_IRQ,
	
};

static char irq_tab_mpc30x[][5] __initdata = {
 [12] = { PCMCIA1, PCMCIA1, 0, 0 },
 [13] = { PCMCIA2, PCMCIA2, 0, 0 },
 [29] = {      MQ,      MQ, 0, 0 },		/* mediaQ MQ-200 */
};

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	if (slot == 30)
		return internal_func_irqs[PCI_FUNC(dev->devfn)];

	return irq_tab_mpc30x[slot][pin];
}
