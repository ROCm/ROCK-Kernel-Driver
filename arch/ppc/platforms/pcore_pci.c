/*
 * arch/ppc/platforms/pcore_pci.c
 *
 * PCI support for Force PCORE boards
 *
 * Author: Matt Porter <mporter@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/mpc10x.h>

#include "pcore.h"

#undef DEBUG
#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif /* DEBUG */

static inline int __init
pcore_6750_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	/*
	 *      PCI IDSEL/INTPIN->INTLINE
	 *      A       B       C       D
	 */
	{
		{9,	10,	11,	12},	/* IDSEL 24 - DEC 21554 */
		{10,	0,	0,	0},	/* IDSEL 25 - DEC 21143 */
		{11,	12,	9,	10},	/* IDSEL 26 - PMC I */
		{12,	9,	10,	11},	/* IDSEL 27 - PMC II */
		{0,	0,	0,	0},	/* IDSEL 28 - unused */
		{0,	0,	9,	0},	/* IDSEL 29 - unused */
		{0,	0,	0,	0},	/* IDSEL 30 - Winbond */
		};
	const long min_idsel = 24, max_idsel = 30, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
};

static inline int __init
pcore_680_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	/*
	 *      PCI IDSEL/INTPIN->INTLINE
	 *      A       B       C       D
	 */
	{
		{9,	10,	11,	12},	/* IDSEL 24 - Sentinel */
		{10,	0,	0,	0},	/* IDSEL 25 - i82559 #1 */
		{11,	12,	9,	10},	/* IDSEL 26 - PMC I */
		{12,	9,	10,	11},	/* IDSEL 27 - PMC II */
		{9,	0,	0,	0},	/* IDSEL 28 - i82559 #2 */
		{0,	0,	0,	0},	/* IDSEL 29 - unused */
		{0,	0,	0,	0},	/* IDSEL 30 - Winbond */
		};
	const long min_idsel = 24, max_idsel = 30, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
};

void __init
pcore_pcibios_fixup(void)
{
	struct pci_dev *dev;

	if ((dev = pci_find_device(PCI_VENDOR_ID_WINBOND,
				PCI_DEVICE_ID_WINBOND_83C553,
				0)))
	{
		/* Reroute interrupts both IDE channels to 15 */
		pci_write_config_byte(dev,
				PCORE_WINBOND_IDE_INT,
				0xff);

		/* Route INTA-D to IRQ9-12, respectively */
		pci_write_config_word(dev,
				PCORE_WINBOND_PCI_INT,
				0x9abc);

		/*
		 * Set up 8259 edge/level triggering
		 */
 		outb(0x00, PCORE_WINBOND_PRI_EDG_LVL);
		outb(0x1e, PCORE_WINBOND_SEC_EDG_LVL);
	}
}

int __init
pcore_find_bridges(void)
{
	struct pci_controller* hose;
	int host_bridge, board_type;

	hose = pcibios_alloc_controller();
	if (!hose)
		return 0;

	mpc10x_bridge_init(hose,
			MPC10X_MEM_MAP_B,
			MPC10X_MEM_MAP_B,
			MPC10X_MAPB_EUMB_BASE);

	/* Determine board type */
	early_read_config_dword(hose,
			0,
			PCI_DEVFN(0,0),
			PCI_VENDOR_ID,
			&host_bridge);
	if (host_bridge == MPC10X_BRIDGE_106)
		board_type = PCORE_TYPE_6750;
	else /* MPC10X_BRIDGE_107 */
		board_type = PCORE_TYPE_680;

	hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

	ppc_md.pcibios_fixup = pcore_pcibios_fixup;
	ppc_md.pci_swizzle = common_swizzle;

	if (board_type == PCORE_TYPE_6750)
		ppc_md.pci_map_irq = pcore_6750_map_irq;
	else /* PCORE_TYPE_680 */
		ppc_md.pci_map_irq = pcore_680_map_irq;

	return board_type;
}
