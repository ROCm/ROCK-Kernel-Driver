/*
 * arch/ppc/platforms/zx4500_pci.c
 * 
 * PCI setup routines for Znyx ZX4500 cPCI boards.
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * 2000-2001 (c) MontaVista, Software, Inc.  This file is licensed under
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
#include <asm/uaccess.h>
#include <asm/machdep.h>
#include <asm/mpc10x.h>
#include <asm/pci-bridge.h>

#include "zx4500.h"

/*
 * Znyx ZX4500 interrupt routes.
 */
static inline int
zx4500_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	/*
	 *	PCI IDSEL/INTPIN->INTLINE 
	 * 	   A   B   C   D
	 */
	{
		{ 19,  0,  0,  0 },	/* IDSEL 21 - 21554 PCI-cPCI bridge */
		{ 18,  0,  0,  0 },	/* IDSEL 22 - BCM5600 INTA */
		{ 16, 20, 16, 20 },	/* IDSEL 23 - PPMC Slot */
	};

	const long min_idsel = 21, max_idsel = 23, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
}

void __init
zx4500_board_init(struct pci_controller *hose)
{
	uint	val;
	u_char	sysctl;

	/*
	 * CPLD Registers are mapped in by BAT 3 in zx4500_setup_arch().
	 *
	 * Turn off all interrupts routed through the CPLD.
	 * Also, turn off watchdog timer and drive PMC EREADY low.
	 */
	sysctl = in_8((volatile u_char *)ZX4500_CPLD_SYSCTL);
	sysctl &= ~(ZX4500_CPLD_SYSCTL_PMC |
		    ZX4500_CPLD_SYSCTL_BCM |
		    ZX4500_CPLD_SYSCTL_SINTA |
		    ZX4500_CPLD_SYSCTL_WD |
		    ZX4500_CPLD_SYSCTL_PMC_TRI);
	out_8((volatile u_char *)ZX4500_CPLD_SYSCTL, sysctl);

	/*
	 * Kludge the size that BAR2 of the 21554 asks for
	 * (i.e., set Upstream I/O or Memory 0 Setup Register).
	 * Old versions of SROM wants 1 GB which is too large, make it ask
	 * for 256 MB.
	 */
	early_read_config_dword(hose, 0, PCI_DEVFN(21,0), 0xc4, &val);

	if (val != 0) {
		early_write_config_dword(hose,
					 0,
					 PCI_DEVFN(21,0),
					 0xc4,
					 val | 0xf0000000);
	}

	return;
}

static int                     
zx4500_exclude_device(u_char bus, u_char devfn)
{
	if ((bus == 0) && (PCI_SLOT(devfn) == ZX4500_HOST_BRIDGE_IDSEL)) {
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	else {
		return PCIBIOS_SUCCESSFUL;
	}
}

void __init
zx4500_find_bridges(void)
{
	struct pci_controller	*hose;

	hose = pcibios_alloc_controller();

	if (!hose)
		return;

	hose->first_busno = 0;
	hose->last_busno = 0xff;

	if (mpc10x_bridge_init(hose,
			       MPC10X_MEM_MAP_B,
			       MPC10X_MEM_MAP_B,
			       MPC10X_MAPB_EUMB_BASE) == 0) {

		hose->mem_resources[0].end = 0xffffffff;
	
		/* Initialize the board */
		zx4500_board_init(hose);

		/* scan PCI bus */
		ppc_md.pci_exclude_device = zx4500_exclude_device;
		hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

		ppc_md.pcibios_fixup = NULL;
		ppc_md.pcibios_fixup_bus = NULL;
		ppc_md.pci_swizzle = common_swizzle;
		ppc_md.pci_map_irq = zx4500_map_irq;
	}
	else {
		if (ppc_md.progress)
			ppc_md.progress("Bridge init failed", 0x100);
		printk("Host bridge init failed\n");
	}

	return;
}
