/*
 * arch/ppc/platforms/menf1_pci.c
 * 
 * PCI support for MEN F1
 *
 * Author: Matt Porter <mporter@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.1.  This program
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

#include "menf1.h"

#undef DEBUG
#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif /* DEBUG */ 

static inline int __init
menf1_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	/*
	 *      PCI IDSEL/INTPIN->INTLINE
	 *      A       B       C       D
	 */ 
	{
		{10,	11,	7,	9},	/* IDSEL 26 - PCMIP 0 */
		{0,	0,	0,	0},	/* IDSEL 27 - M5229 IDE */
		{0,	0,	0,	0},	/* IDSEL 28 - M7101 PMU */
		{9,	10,	11,	7},	/* IDSEL 29 - PCMIP 1 */
		{10,	11,	7,	9},	/* IDSEL 30 - P2P Bridge */
	};
	const long min_idsel = 26, max_idsel = 30, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
};

static int
menf1_exclude_device(u_char bus, u_char devfn)
{
	if ((bus == 0) && (devfn == 0xe0)) {
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	else {
		return PCIBIOS_SUCCESSFUL;
	}
}

void __init
menf1_find_bridges(void)
{
	struct pci_controller* hose;

	hose = pcibios_alloc_controller();
	if (!hose)
		return;

	hose->first_busno = 0;
	hose->last_busno = 0xff;

	ppc_md.pci_exclude_device = menf1_exclude_device;

	mpc10x_bridge_init(hose,
			MPC10X_MEM_MAP_B,
			MPC10X_MEM_MAP_B,
			MPC10X_MAPB_EUMB_BASE);

	hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

	{
		/* Add ISA bus wait states */
		unsigned char isa_control;

		early_read_config_byte(hose, 0, 0x90, 0x43, &isa_control);
		isa_control |= 0x33;
		early_write_config_byte(hose, 0, 0x90, 0x43, isa_control);
	}

	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = menf1_map_irq;
}
