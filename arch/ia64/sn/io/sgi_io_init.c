/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

#include <linux/types.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <asm/sn/sgi.h>
#include <asm/sn/agent.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/synergy.h>
#include <linux/smp.h>

extern void mlreset(int );
extern int init_hcl(void);
extern void klgraph_hack_init(void);
extern void per_hub_init(cnodeid_t);
extern void hubspc_init(void);
extern void pciba_init(void);
extern void pciio_init(void);
extern void pcibr_init(void);
extern void xtalk_init(void);
extern void xbow_init(void);
extern void xbmon_init(void);
extern void pciiox_init(void);
extern void usrpci_init(void);
extern void ioc3_init(void);
extern void initialize_io(void);
extern void init_platform_nodepda(nodepda_t *, cnodeid_t );
extern void intr_clear_all(nasid_t);
extern void klhwg_add_all_modules(devfs_handle_t);
extern void klhwg_add_all_nodes(devfs_handle_t);

void sn_mp_setup(void);
extern devfs_handle_t hwgraph_root;
extern void io_module_init(void);
extern cnodeid_t nasid_to_compact_node[];
extern void pci_bus_cvlink_init(void);
extern void temp_hack(void);
extern void init_platform_pda(cpuid_t cpu);

extern int pci_bus_to_hcl_cvlink(void);
extern synergy_da_t	*Synergy_da_indr[];

#define DEBUG_IO_INIT
#ifdef DEBUG_IO_INIT
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif /* DEBUG_IO_INIT */

/*
 * kern/ml/csu.s calls mlsetup
 *   mlsetup calls mlreset(master) - kern/os/startup.c
 *   j main
 *
 
 * SN/slave.s start_slave_loop calls slave_entry
 * SN/slave.s slave_entry calls slave_loop
 * SN/slave.s slave_loop calls bootstrap
 * bootstrap in SN1/SN1asm.s calls cboot
 * cboot calls mlreset(slave) - ml/SN/mp.c
 *
 * sgi_io_infrastructure_init() gets called right before pci_init() 
 * in Linux mainline.  This routine actually mirrors the IO Infrastructure 
 * call sequence in IRIX, ofcourse, nicely modified for Linux.
 *
 * It is very IMPORTANT that this call is only made by the Master CPU!
 *
 */

void
sgi_master_io_infr_init(void)
{
#ifdef Colin
	/*
	 * Simulate Big Window 0.
	 * Only when we build for lutsen etc. ..
	 */
	simulated_BW0_init();
#endif

	/*
	 * Do any early init stuff .. einit_tbl[] etc.
	 */
	DBG("--> sgi_master_io_infr_init: calling init_hcl().\n");
	init_hcl(); /* Sets up the hwgraph compatibility layer with devfs */

	/*
	 * initialize the Linux PCI to xwidget vertexes ..
	 */
	DBG("--> sgi_master_io_infr_init: calling pci_bus_cvlink_init().\n");
	pci_bus_cvlink_init();

	/*
	 * Hack to provide statically initialzed klgraph entries.
	 */
	DBG("--> sgi_master_io_infr_init: calling klgraph_hack_init()\n");
	klgraph_hack_init();

	/*
	 * This is the Master CPU.  Emulate mlsetup and main.c in Irix.
	 */
	DBG("--> sgi_master_io_infr_init: calling mlreset(0).\n");
	mlreset(0); /* Master .. */

	/*
	 * allowboot() is called by kern/os/main.c in main()
	 * Emulate allowboot() ...
	 *   per_cpu_init() - only need per_hub_init()
	 *   cpu_io_setup() - Nothing to do.
	 * 
	 */
	DBG("--> sgi_master_io_infr_init: calling sn_mp_setup().\n");
	sn_mp_setup();

	DBG("--> sgi_master_io_infr_init: calling per_hub_init(0).\n");
	per_hub_init(0); /* Need to get and send in actual cnode number */

	/* We can do headless hub cnodes here .. */

	/*
	 * io_init[] stuff.
	 *
	 * Get SGI IO Infrastructure drivers to init and register with 
	 * each other etc.
	 */

	DBG("--> sgi_master_io_infr_init: calling hubspc_init()\n");
	hubspc_init();

	DBG("--> sgi_master_io_infr_init: calling pciba_init()\n");
	pciba_init();

	DBG("--> sgi_master_io_infr_init: calling pciio_init()\n");
	pciio_init();

	DBG("--> sgi_master_io_infr_init: calling pcibr_init()\n");
	pcibr_init();

	DBG("--> sgi_master_io_infr_init: calling xtalk_init()\n");
	xtalk_init();

	DBG("--> sgi_master_io_infr_init: calling xbow_init()\n");
	xbow_init();

	DBG("--> sgi_master_io_infr_init: calling xbmon_init()\n");
	xbmon_init();

	DBG("--> sgi_master_io_infr_init: calling pciiox_init()\n");
	pciiox_init();

	DBG("--> sgi_master_io_infr_init: calling usrpci_init()\n");
	usrpci_init();

	DBG("--> sgi_master_io_infr_init: calling ioc3_init()\n");
	ioc3_init();

	/*
	 *
	 * Our IO Infrastructure drivers are in place .. 
	 * Initialize the whole IO Infrastructure .. xwidget/device probes.
	 *
	 */
	DBG("--> sgi_master_io_infr_init: Start Probe and IO Initialization\n");
	initialize_io();

	DBG("--> sgi_master_io_infr_init: Setting up SGI IO Links for Linux PCI\n");
	pci_bus_to_hcl_cvlink();

	DBG("--> Leave sgi_master_io_infr_init: DONE setting up SGI Links for PCI\n");
}

/*
 * sgi_slave_io_infr_init - This routine must be called on all cpus except 
 * the Master CPU.
 */
void
sgi_slave_io_infr_init(void)
{
	/* Emulate cboot() .. */
	mlreset(1); /* This is a slave cpu */

	per_hub_init(0); /* Need to get and send in actual cnode number */

	/* Done */
}

/*
 * One-time setup for MP SN.
 * Allocate per-node data, slurp prom klconfig information and
 * convert it to hwgraph information.
 */
void
sn_mp_setup(void)
{
	cnodeid_t	cnode;
	extern int	maxnodes;
	cpuid_t		cpu;

	DBG("sn_mp_setup: Entered.\n");
	/*
	 * NODEPDA(x) Macro depends on nodepda
	 * subnodepda is also statically set to calias space which we 
	 * do not currently support yet .. just a hack for now.
	 */
#ifdef NUMA_BASE
	DBG("sn_mp_setup(): maxnodes= %d  numnodes= %d\n", maxnodes,numnodes);
        maxnodes = numnodes;
#ifdef SIMULATED_KLGRAPH
	maxnodes = 1;
	numnodes = 1;
#endif /* SIMULATED_KLGRAPH */
        printk("sn_mp_setup(): Allocating backing store for *Nodepdaindr[%2d] \n",
                maxnodes);

        /*
         * Initialize Nodpdaindr and per-node nodepdaindr array
         */
        *Nodepdaindr = (nodepda_t *) kmalloc(sizeof(nodepda_t *)*numnodes, GFP_KERNEL);
        for (cnode=0; cnode<maxnodes; cnode++) {
            Nodepdaindr[cnode] = (nodepda_t *) kmalloc(sizeof(struct nodepda_s),
                                                                GFP_KERNEL);
	    Synergy_da_indr[cnode * 2] = (synergy_da_t *) kmalloc(
		sizeof(synergy_da_t), GFP_KERNEL);
	    Synergy_da_indr[(cnode * 2) + 1] = (synergy_da_t *) kmalloc(
		sizeof(synergy_da_t), GFP_KERNEL);
            Nodepdaindr[cnode]->pernode_pdaindr = Nodepdaindr;
            subnodepda = &Nodepdaindr[cnode]->snpda[cnode];
        }
        nodepda = Nodepdaindr[0];
#else
        Nodepdaindr = (nodepda_t *) kmalloc(sizeof(struct nodepda_s), GFP_KERNEL);
        nodepda = Nodepdaindr[0];
        subnodepda = &Nodepdaindr[0]->snpda[0];

#endif /* NUMA_BASE */

	/*
	 * Before we let the other processors run, set up the platform specific
	 * stuff in the nodepda.
	 *
	 * ???? maxnodes set in mlreset .. who sets it now ????
	 * ???? cpu_node_probe() called in mlreset to set up the following:
	 *      compact_to_nasid_node[] - cnode id gives nasid
	 *      nasid_to_compact_node[] - nasid gives cnode id
	 *
	 *	do_cpumask() sets the following:
	 *      cpuid_to_compact_node[] - cpuid gives cnode id
	 *
	 *      nasid comes from gdap->g_nasidtable[]
	 *      ml/SN/promif.c
	 */

	for (cnode = 0; cnode < maxnodes; cnode++) {
		/*
		 * Set up platform-dependent nodepda fields.
		 * The following routine actually sets up the hubinfo struct
		 * in nodepda.
		 */
		DBG("sn_mp_io_setup: calling init_platform_nodepda(%2d)\n",cnode);
		init_platform_nodepda(Nodepdaindr[cnode], cnode);

		/*
		 * This routine clears the Hub's Interrupt registers.
		 */
#ifndef CONFIG_IA64_SGI_IO
		/*
		 * We need to move this intr_clear_all() routine 
		 * from SN/intr.c to a more appropriate file.
		 * Talk to Al Mayer.
		 */
                intr_clear_all(COMPACT_TO_NASID_NODEID(cnode));
#endif
	}

#ifdef CONFIG_IA64_SGI_IO
	for (cpu = 0; cpu < smp_num_cpus; cpu++) {
		/* Skip holes in CPU space */
		if (cpu_enabled(cpu)) {
			init_platform_pda(cpu);
		}
	}
#endif

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

	DBG("sn_mp_io_setup: calling io_module_init()\n");
	io_module_init(); /* Use to be called module_init() .. */

	DBG("sn_mp_setup: calling klhwg_add_all_modules()\n");
	klhwg_add_all_modules(hwgraph_root);
	DBG("sn_mp_setup: calling klhwg_add_all_nodes()\n");
	klhwg_add_all_nodes(hwgraph_root);
}
