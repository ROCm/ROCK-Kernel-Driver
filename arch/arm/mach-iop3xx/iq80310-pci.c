/*
 * arch/arm/mach-iop3xx/iq80310-pci.c
 *
 * PCI support for the Intel IQ80310 reference board
 *
 * Matt Porter <mporter@mvista.com>
 *
 * Copyright (C) 2001 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach/pci.h>
#include <asm/mach-types.h>

/*
 * The following macro is used to lookup irqs in a standard table
 * format for those systems that do not already have PCI
 * interrupts properly routed.  We assume 1 <= pin <= 4
 */
#define PCI_IRQ_TABLE_LOOKUP(minid,maxid)	\
({ int _ctl_ = -1;				\
   unsigned int _idsel = idsel - minid;		\
   if (_idsel <= maxid)				\
      _ctl_ = pci_irq_table[_idsel][pin-1];	\
   _ctl_; })

#define INTA	IRQ_IQ80310_INTA
#define INTB	IRQ_IQ80310_INTB
#define INTC	IRQ_IQ80310_INTC
#define INTD	IRQ_IQ80310_INTD

#define INTE	IRQ_IQ80310_I82559

typedef u8 irq_table[4];

/*
 * IRQ tables for primary bus.
 *
 * On a Rev D.1 and older board, INT A-C are not routed, so we
 * just fake it as INTA and than we take care of handling it
 * correctly in the IRQ demux routine.
 */
static irq_table pci_pri_d_irq_table[] = {
/* Pin:    A     B     C     D */
	{ INTA, INTD, INTA, INTA }, /*  PCI Slot J3 */
	{ INTD, INTA, INTA, INTA }, /*  PCI Slot J4 */
};

static irq_table pci_pri_f_irq_table[] = {
/* Pin:    A     B     C     D */
	{ INTC, INTD, INTA, INTB }, /*  PCI Slot J3 */
	{ INTD, INTA, INTB, INTC }, /*  PCI Slot J4 */
};

static int __init
iq80310_pri_map_irq(struct pci_dev *dev, u8 idsel, u8 pin)
{
	irq_table *pci_irq_table;

	BUG_ON(pin < 1 || pin > 4);

	if (!system_rev) {
		pci_irq_table = pci_pri_d_irq_table;
	} else {
		pci_irq_table = pci_pri_f_irq_table;
	}

	return PCI_IRQ_TABLE_LOOKUP(2, 3);
}

/*
 * IRQ tables for secondary bus.
 *
 * On a Rev D.1 and older board, INT A-C are not routed, so we
 * just fake it as INTA and than we take care of handling it
 * correctly in the IRQ demux routine.
 */
static irq_table pci_sec_d_irq_table[] = {
/* Pin:    A     B     C     D */
	{ INTA, INTA, INTA, INTD }, /*  PCI Slot J1 */
	{ INTA, INTA, INTD, INTA }, /*  PCI Slot J5 */
	{ INTE, INTE, INTE, INTE }, /*  P2P Bridge */
};

static irq_table pci_sec_f_irq_table[] = {
/* Pin:	   A     B     C     D */
	{ INTA, INTB, INTC, INTD }, /* PCI Slot J1 */
	{ INTB, INTC, INTD, INTA }, /* PCI Slot J5 */
	{ INTE, INTE, INTE, INTE }, /* P2P Bridge */
};

static int __init
iq80310_sec_map_irq(struct pci_dev *dev, u8 idsel, u8 pin)
{
	irq_table *pci_irq_table;

	BUG_ON(pin < 1 || pin > 4);

	if (!system_rev) {
		pci_irq_table = pci_sec_d_irq_table;
	} else {
		pci_irq_table = pci_sec_f_irq_table;
	}

	return PCI_IRQ_TABLE_LOOKUP(0, 2);
}

static int iq80310_pri_host;

static int iq80310_setup(int nr, struct pci_sys_data *sys)
{
	switch (nr) {
	case 0:
		if (!iq80310_pri_host)
			return 0;

		sys->map_irq = iq80310_pri_map_irq;
		break;

	case 1:
		sys->map_irq = iq80310_sec_map_irq;
		break;

	default:
		return 0;
	}

	return iop310_setup(nr, sys);
}

static void iq80310_preinit(void)
{
	iq80310_pri_host = *(volatile u32 *)IQ80310_BACKPLANE & 1;

	printk(KERN_INFO "PCI: IQ80310 is a%s\n",
		iq80310_pri_host ? " system controller" : "n agent");

	iop310_init();
}

static struct hw_pci iq80310_pci __initdata = {
	.swizzle	= pci_std_swizzle,
	.nr_controllers = 2,
	.setup		= iq80310_setup,
	.scan		= iop310_scan_bus,
	.preinit	= iq80310_preinit,
};

static int __init iq80310_pci_init(void)
{
	if (machine_is_iq80310())
		pci_common_init(&iq80310_pci);
	return 0;
}

subsys_initcall(iq80310_pci_init);
