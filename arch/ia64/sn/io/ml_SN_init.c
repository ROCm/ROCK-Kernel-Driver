/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2002 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <linux/bootmem.h>
#include <asm/sn/sgi.h>
#include <asm/sn/io.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/snconfig.h>

extern int numcpus;
extern char arg_maxnodes[];
extern cpuid_t master_procid;
#if defined(CONFIG_IA64_SGI_SN1)
extern synergy_da_t    *Synergy_da_indr[];
#endif

extern int hasmetarouter;

int		maxcpus;
cpumask_t	boot_cpumask;
hubreg_t	region_mask = 0;


extern xwidgetnum_t hub_widget_id(nasid_t);

extern int valid_icache_reasons;	/* Reasons to flush the icache */
extern int valid_dcache_reasons;	/* Reasons to flush the dcache */
extern u_char miniroot;
extern volatile int	need_utlbmiss_patch;
extern void iograph_early_init(void);

nasid_t master_nasid = INVALID_NASID;


/*
 * mlreset(int slave)
 * 	very early machine reset - at this point NO interrupts have been
 * 	enabled; nor is memory, tlb, p0, etc setup.
 *
 * 	slave is zero when mlreset is called for the master processor and
 *	is nonzero thereafter.
 */


void
mlreset(int slave)
{
	if (!slave) {
		/*
		 * We are the master cpu and node.
		 */ 
		master_nasid = get_nasid();
		set_master_bridge_base();

		/* We're the master processor */
		master_procid = smp_processor_id();
		master_nasid = cpuid_to_nasid(master_procid);

		/*
		 * master_nasid we get back better be same as one from
		 * get_nasid()
		 */
		ASSERT_ALWAYS(master_nasid == get_nasid());

		/* early initialization of iograph */
		iograph_early_init();

		/* Initialize Hub Pseudodriver Management */
		hubdev_init();

	} else { /* slave != 0 */
		/*
		 * This code is performed ONLY by slave processors.
		 */

	}
}


/* XXX - Move the meat of this to intr.c ? */
/*
 * Set up the platform-dependent fields in the nodepda.
 */
void init_platform_nodepda(nodepda_t *npda, cnodeid_t node)
{
	hubinfo_t hubinfo;
#ifdef CONFIG_IA64_SGI_SN1
	int	  sn;
#endif

	extern void router_map_init(nodepda_t *);
	extern void router_queue_init(nodepda_t *,cnodeid_t);
	extern void intr_init_vecblk(nodepda_t *, cnodeid_t, int);

	/* Allocate per-node platform-dependent data */
	hubinfo = (hubinfo_t)alloc_bootmem_node(NODE_DATA(node), sizeof(struct hubinfo_s));

	npda->pdinfo = (void *)hubinfo;
	hubinfo->h_nodepda = npda;
	hubinfo->h_cnodeid = node;
	hubinfo->h_nasid = COMPACT_TO_NASID_NODEID(node);

	spin_lock_init(&hubinfo->h_crblock);

	hubinfo->h_widgetid = hub_widget_id(hubinfo->h_nasid);
	npda->xbow_peer = INVALID_NASID;

	/* 
	 * Initialize the linked list of
	 * router info pointers to the dependent routers
	 */
	npda->npda_rip_first = NULL;

	/*
	 * npda_rip_last always points to the place
	 * where the next element is to be inserted
	 * into the list 
	 */
	npda->npda_rip_last = &npda->npda_rip_first;
	npda->module_id = INVALID_MODULE;

#ifdef CONFIG_IA64_SGI_SN1
	/*
	* Initialize the interrupts.
	* On sn2, this is done at pci init time,
	* because sn2 needs the cpus checked in
	* when it initializes interrupts.  This is
	* so we don't see all the nodes as headless.
	*/
	for (sn=0; sn<NUM_SUBNODES; sn++) {
		intr_init_vecblk(npda, node, sn);
	}
#endif /* CONFIG_IA64_SGI_SN1 */

	mutex_init_locked(&npda->xbow_sema); /* init it locked? */

#ifdef	LATER

	/* Setup the (module,slot) --> nic mapping for all the routers
	 * in the system. This is useful during error handling when
	 * there is no shared memory.
	 */
	router_map_init(npda);

	/* Allocate memory for the per-node router traversal queue */
	router_queue_init(npda,node);
	npda->sbe_info = alloc_bootmem_node(NODE_DATA(node), sizeof (sbe_info_t));
	ASSERT(npda->sbe_info);

#endif /* LATER */
}

/* XXX - Move the interrupt stuff to intr.c ? */
/*
 * Set up the platform-dependent fields in the processor pda.
 * Must be done _after_ init_platform_nodepda().
 * If we need a lock here, something else is wrong!
 */
void init_platform_pda(cpuid_t cpu)
{
#if defined(CONFIG_IA64_SGI_SN1)
	hub_intmasks_t *intmasks;
	int i, subnode;
	cnodeid_t	cnode;
	synergy_da_t	*sda;
	int	which_synergy;


	cnode = cpuid_to_cnodeid(cpu);
	which_synergy = cpuid_to_synergy(cpu);

	sda = Synergy_da_indr[(cnode * 2) + which_synergy];
	intmasks = &sda->s_intmasks;

	/* Clear INT_PEND0 masks. */
	for (i = 0; i < N_INTPEND0_MASKS; i++)
		intmasks->intpend0_masks[i] = 0;

	/* Set up pointer to the vector block in the nodepda. */
	/* (Cant use SUBNODEPDA - not working yet) */
	subnode = cpuid_to_subnode(cpu);
	intmasks->dispatch0 = &NODEPDA(cnode)->snpda[cpuid_to_subnode(cpu)].intr_dispatch0;
	intmasks->dispatch1 = &NODEPDA(cnode)->snpda[cpuid_to_subnode(cpu)].intr_dispatch1;
	if (intmasks->dispatch0 !=  &SUBNODEPDA(cnode, subnode)->intr_dispatch0 ||
	   intmasks->dispatch1 !=  &SUBNODEPDA(cnode, subnode)->intr_dispatch1)
	   	panic("xxx");
	intmasks->dispatch0 = &SUBNODEPDA(cnode, subnode)->intr_dispatch0;
	intmasks->dispatch1 = &SUBNODEPDA(cnode, subnode)->intr_dispatch1;

	/* Clear INT_PEND1 masks. */
	for (i = 0; i < N_INTPEND1_MASKS; i++)
		intmasks->intpend1_masks[i] = 0;
#endif	/* CONFIG_IA64_SGI_SN1 */
}

void
update_node_information(cnodeid_t cnodeid)
{
	nodepda_t *npda = NODEPDA(cnodeid);
	nodepda_router_info_t *npda_rip;
	
	/* Go through the list of router info 
	 * structures and copy some frequently
	 * accessed info from the info hanging
	 * off the corresponding router vertices
	 */
	npda_rip = npda->npda_rip_first;
	while(npda_rip) {
		if (npda_rip->router_infop) {
			npda_rip->router_portmask = 
				npda_rip->router_infop->ri_portmask;
			npda_rip->router_slot = 
				npda_rip->router_infop->ri_slotnum;
		} else {
			/* No router, no ports. */
			npda_rip->router_portmask = 0;
		}
		npda_rip = npda_rip->router_next;
	}
}
