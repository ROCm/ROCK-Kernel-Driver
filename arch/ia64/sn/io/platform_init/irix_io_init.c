/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <asm/sn/sgi.h>
#include <asm/sn/io.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/sn_private.h>
#include <linux/smp.h>
#include <asm/sn/simulator.h>

extern void init_all_devices(void);
extern void klhwg_add_all_modules(vertex_hdl_t);
extern void klhwg_add_all_nodes(vertex_hdl_t);

extern vertex_hdl_t hwgraph_root;
extern void io_module_init(void);
extern int pci_bus_to_hcl_cvlink(void);
extern void mlreset(void);

/* #define DEBUG_IO_INIT 1 */
#ifdef DEBUG_IO_INIT
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif /* DEBUG_IO_INIT */

/*
 * This routine is responsible for the setup of all the IRIX hwgraph style
 * stuff that's been pulled into linux.  It's called by sn_pci_find_bios which
 * is called just before the generic Linux PCI layer does its probing (by 
 * platform_pci_fixup aka sn_pci_fixup).
 *
 * It is very IMPORTANT that this call is only made by the Master CPU!
 *
 */

void
irix_io_init(void)
{
	cnodeid_t cnode;

	/*
	 * This is the Master CPU.  Emulate mlsetup and main.c in Irix.
	 */
	mlreset();

        /*
         * Initialize platform-dependent vertices in the hwgraph:
         *      module
         *      node
         *      cpu
         *      memory
         *      slot
         *      hub
         *      router
         *      xbow
         */

        io_module_init(); /* Use to be called module_init() .. */
        klhwg_add_all_modules(hwgraph_root);
        klhwg_add_all_nodes(hwgraph_root);

	for (cnode = 0; cnode < numnodes; cnode++) {
		extern void per_hub_init(cnodeid_t);
		per_hub_init(cnode);
	}

	/* We can do headless hub cnodes here .. */

	/*
	 *
	 * Our IO Infrastructure drivers are in place .. 
	 * Initialize the whole IO Infrastructure .. xwidget/device probes.
	 *
	 */
	init_all_devices();
	pci_bus_to_hcl_cvlink();
}
