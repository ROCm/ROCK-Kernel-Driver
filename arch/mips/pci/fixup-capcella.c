/*
 * FILE NAME
 *	arch/mips/vr41xx/zao-capcella/pci_fixup.c
 *
 * BRIEF MODULE DESCRIPTION
 *	The ZAO Networks Capcella specific PCI fixups.
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

#include <asm/vr41xx/capcella.h>

/*
 * Shortcuts
 */
#define INT1	RTL8139_1_IRQ
#define INT2	RTL8139_2_IRQ
#define INTA	PC104PLUS_INTA_IRQ
#define INTB	PC104PLUS_INTB_IRQ
#define INTC	PC104PLUS_INTC_IRQ
#define INTD	PC104PLUS_INTD_IRQ

static char irq_tab_capcella[][5] __initdata = {
 [11] = { -1, INT1, INT1, INT1, INT1 },
 [12] = { -1, INT2, INT2, INT2, INT2 },
 [14] = { -1, INTA, INTB, INTC, INTD }
};

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	return irq_tab_capcella[slot][pin];
}
