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
#include <asm/sn/pci/pciba.h>
#include <linux/smp.h>

extern void mlreset(void);
extern int init_hcl(void);
extern void klgraph_hack_init(void);
extern void hubspc_init(void);
extern void pciio_init(void);
extern void pcibr_init(void);
extern void xtalk_init(void);
extern void xbow_init(void);
extern void xbmon_init(void);
extern void pciiox_init(void);
extern void pic_init(void);
extern void usrpci_init(void);
extern void ioc3_init(void);
extern void initialize_io(void);
extern void klhwg_add_all_modules(devfs_handle_t);
extern void klhwg_add_all_nodes(devfs_handle_t);

void sn_mp_setup(void);
extern devfs_handle_t hwgraph_root;
extern void io_module_init(void);
extern void pci_bus_cvlink_init(void);
extern void temp_hack(void);

extern int pci_bus_to_hcl_cvlink(void);

/* #define DEBUG_IO_INIT 1 */
#ifdef DEBUG_IO_INIT
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif /* DEBUG_IO_INIT */

/*
 * per_hub_init
 *
 * 	This code is executed once for each Hub chip.
 */
static void
per_hub_init(cnodeid_t cnode)
{
	nasid_t		nasid;
	nodepda_t	*npdap;
	ii_icmr_u_t	ii_icmr;
	ii_ibcr_u_t	ii_ibcr;

	nasid = COMPACT_TO_NASID_NODEID(cnode);

	ASSERT(nasid != INVALID_NASID);
	ASSERT(NASID_TO_COMPACT_NODEID(nasid) == cnode);

	npdap = NODEPDA(cnode);

	REMOTE_HUB_S(nasid, IIO_IWEIM, 0x8000);

	/*
	 * Set the total number of CRBs that can be used.
	 */
	ii_icmr.ii_icmr_regval= 0x0;
	ii_icmr.ii_icmr_fld_s.i_c_cnt = 0xf;
	REMOTE_HUB_S(nasid, IIO_ICMR, ii_icmr.ii_icmr_regval);

	/*
	 * Set the number of CRBs that both of the BTEs combined
	 * can use minus 1.
	 */
	ii_ibcr.ii_ibcr_regval= 0x0;
	ii_ibcr.ii_ibcr_fld_s.i_count = 0x8;
	REMOTE_HUB_S(nasid, IIO_IBCR, ii_ibcr.ii_ibcr_regval);

	/*
	 * Set CRB timeout to be 10ms.
	 */
#ifdef BRINGUP2
	REMOTE_HUB_S(nasid, IIO_ICTP, 0xffffff );
	REMOTE_HUB_S(nasid, IIO_ICTO, 0xff);
	//REMOTE_HUB_S(nasid, IIO_IWI, 0x00FF00FF00FFFFFF);
#endif

	/* Initialize error interrupts for this hub. */
	hub_error_init(cnode);
}

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
sgi_master_io_infr_init(void)
{
	int cnode;
	extern void kdba_io_init();

	/*
	 * Do any early init stuff .. einit_tbl[] etc.
	 */
	init_hcl(); /* Sets up the hwgraph compatibility layer with devfs */

	/*
	 * initialize the Linux PCI to xwidget vertexes ..
	 */
	pci_bus_cvlink_init();

	kdba_io_init();

#ifdef BRINGUP
	/*
	 * Hack to provide statically initialzed klgraph entries.
	 */
	DBG("--> sgi_master_io_infr_init: calling klgraph_hack_init()\n");
	klgraph_hack_init();
#endif /* BRINGUP */

	/*
	 * This is the Master CPU.  Emulate mlsetup and main.c in Irix.
	 */
	mlreset();

	/*
	 * allowboot() is called by kern/os/main.c in main()
	 * Emulate allowboot() ...
	 *   per_cpu_init() - only need per_hub_init()
	 *   cpu_io_setup() - Nothing to do.
	 * 
	 */
	sn_mp_setup();

	for (cnode = 0; cnode < numnodes; cnode++) {
		per_hub_init(cnode);
	}

	/* We can do headless hub cnodes here .. */

	/*
	 * io_init[] stuff.
	 *
	 * Get SGI IO Infrastructure drivers to init and register with 
	 * each other etc.
	 */

	hubspc_init();
	pciio_init();
	pcibr_init();
	pic_init();
	xtalk_init();
	xbow_init();
	xbmon_init();
	pciiox_init();
	usrpci_init();
	ioc3_init();

	/*
	 *
	 * Our IO Infrastructure drivers are in place .. 
	 * Initialize the whole IO Infrastructure .. xwidget/device probes.
	 *
	 */
	initialize_io();
	pci_bus_to_hcl_cvlink();

#ifdef CONFIG_PCIBA
	DBG("--> sgi_master_io_infr_init: calling pciba_init()\n");
#ifndef BRINGUP2
 	pciba_init();
#endif
#endif
}

/*
 * One-time setup for MP SN.
 * Allocate per-node data, slurp prom klconfig information and
 * convert it to hwgraph information.
 */
void
sn_mp_setup(void)
{
	cpuid_t		cpu;

	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		/* Skip holes in CPU space */
		if (cpu_enabled(cpu)) {
			init_platform_pda(cpu);
		}
	}

	/*
	 * Initialize platform-dependent vertices in the hwgraph:
	 *	module
	 *	node
	 *	cpu
	 *	memory
	 *	slot
	 *	hub
	 *	router
	 *	xbow
	 */

	io_module_init(); /* Use to be called module_init() .. */
	klhwg_add_all_modules(hwgraph_root);
	klhwg_add_all_nodes(hwgraph_root);
}
