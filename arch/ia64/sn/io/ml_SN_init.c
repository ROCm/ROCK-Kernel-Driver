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
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/nodemask.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/synergy.h>


#if defined (CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
#include <asm/sn/sn1/ip27config.h>
#include <asm/sn/sn1/hubdev.h>
#include <asm/sn/sn1/sn1.h>
#endif /* CONFIG_SGI_IP35 || CONFIG_IA64_SGI_SN1 */


extern int numcpus;
extern char arg_maxnodes[];
extern cpuid_t master_procid;
extern void * kmem_alloc_node(register size_t, register int , cnodeid_t);
extern synergy_da_t    *Synergy_da_indr[];

extern int hasmetarouter;

int		maxcpus;
cpumask_t	boot_cpumask;
hubreg_t	region_mask = 0;


extern xwidgetnum_t hub_widget_id(nasid_t);

#ifndef CONFIG_IA64_SGI_IO
#if defined (IP27)
short		cputype = CPU_IP27;
#elif defined (IP33)
short		cputype = CPU_IP33;
#elif defined (IP35)
short		cputype = CPU_IP35;
#else
#error <BOMB! define new cputype here >
#endif
#endif /* CONFIG_IA64_SGI_IO */

static int	fine_mode = 0;

#ifndef CONFIG_IA64_SGI_IO
/* Global variables */
pdaindr_t	pdaindr[MAXCPUS];
#endif

static cnodemask_t	hub_init_mask;	/* Mask of cpu in a node doing init */
static volatile cnodemask_t hub_init_done_mask;
					/* Node mask where we wait for
					 * per hub initialization
					 */
spinlock_t		hub_mask_lock;  /* Lock for hub_init_mask above. */

extern int valid_icache_reasons;	/* Reasons to flush the icache */
extern int valid_dcache_reasons;	/* Reasons to flush the dcache */
extern int numnodes;
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
		FIXME("mlreset: Enable when we support ioc3 ..");
#ifndef CONFIG_IA64_SGI_IO
		if (get_console_nasid() == master_nasid) 
			/* Set up the IOC3 */
			ioc3_mlreset((ioc3_cfg_t *)KL_CONFIG_CH_CONS_INFO(master_nasid)->config_base,
				     (ioc3_mem_t *)KL_CONFIG_CH_CONS_INFO(master_nasid)->memory_base);

		/*
		 * Initialize Master nvram base.
		 */
		nvram_baseinit();

		fine_mode = is_fine_dirmode();
#endif /* CONFIG_IA64_SGI_IO */

		/* We're the master processor */
		master_procid = smp_processor_id();
		master_nasid = cpuid_to_nasid(master_procid);

		/*
		 * master_nasid we get back better be same as one from
		 * get_nasid()
		 */
		ASSERT_ALWAYS(master_nasid == get_nasid());

#ifndef CONFIG_IA64_SGI_IO

	/*
	 * Activate when calias is implemented.
	 */
		/* Set all nodes' calias sizes to 8k */
		for (i = 0; i < maxnodes; i++) {
			nasid_t nasid;
			int	sn;

			nasid = COMPACT_TO_NASID_NODEID(i);

			/*
			 * Always have node 0 in the region mask, otherwise CALIAS accesses
			 * get exceptions since the hub thinks it is a node 0 address.
			 */
			for (sn=0; sn<NUM_SUBNODES; sn++) {
				REMOTE_HUB_PI_S(nasid, sn, PI_REGION_PRESENT, (region_mask | 1));
				REMOTE_HUB_PI_S(nasid, sn, PI_CALIAS_SIZE, PI_CALIAS_SIZE_8K);
			}

			/*
			 * Set up all hubs to havew a big window pointing at
			 * widget 0.
			 * Memory mode, widget 0, offset 0
			 */
			REMOTE_HUB_S(nasid, IIO_ITTE(SWIN0_BIGWIN),
				((HUB_PIO_MAP_TO_MEM << IIO_ITTE_IOSP_SHIFT) |
				(0 << IIO_ITTE_WIDGET_SHIFT)));
		}
#endif /* CONFIG_IA64_SGI_IO */

		/* Set up the hub initialization mask and init the lock */
		CNODEMASK_CLRALL(hub_init_mask);
		CNODEMASK_CLRALL(hub_init_done_mask);

		spin_lock_init(&hub_mask_lock);

		/* early initialization of iograph */
		iograph_early_init();

		/* Initialize Hub Pseudodriver Management */
		hubdev_init();

#ifndef CONFIG_IA64_SGI_IO
		/*
		 * Our IO system doesn't require cache writebacks.  Set some
		 * variables appropriately.
		 */
		cachewrback = 0;
		valid_icache_reasons &= ~(CACH_AVOID_VCES | CACH_IO_COHERENCY);
		valid_dcache_reasons &= ~(CACH_AVOID_VCES | CACH_IO_COHERENCY);

		/*
		 * make sure we are running with the right rev of chips
		 */
		verify_snchip_rev();

		/*
                 * Since we've wiped out memory at this point, we
                 * need to reset the ARCS vector table so that it
                 * points to appropriate functions in the kernel
                 * itself.  In this way, we can maintain the ARCS
                 * vector table conventions without having to actually
                 * keep redundant PROM code in memory.
                 */
		he_arcs_set_vectors();
#endif /* CONFIG_IA64_SGI_IO */

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
	int	  sn;
	cnodeid_t i;
	ushort *numcpus_p;

	extern void router_map_init(nodepda_t *);
	extern void router_queue_init(nodepda_t *,cnodeid_t);
#if defined(DEBUG)
	extern lock_t		intr_dev_targ_map_lock;
	extern uint64_t 	intr_dev_targ_map_size;

	/* Initialize the lock to access the device - target cpu mapping
	 * table. This table is explicitly for debugging purposes only and
	 * to aid the "intrmap" idbg command
	 */
	if (node == 0) {
		/* Make sure we do this only once .
		 * There is always a cnode 0 present.
		 */
		intr_dev_targ_map_size = 0;
		init_spinlock(&intr_dev_targ_map_lock,"dtmap_lock",0);
	}
#endif	/* DEBUG */
	/* Allocate per-node platform-dependent data */
	hubinfo = (hubinfo_t)kmem_alloc_node(sizeof(struct hubinfo_s), GFP_ATOMIC, node);

	ASSERT_ALWAYS(hubinfo);
	npda->pdinfo = (void *)hubinfo;
	hubinfo->h_nodepda = npda;
	hubinfo->h_cnodeid = node;
	hubinfo->h_nasid = COMPACT_TO_NASID_NODEID(node);

	printk("init_platform_nodepda: hubinfo 0x%p, &hubinfo->h_crblock 0x%p\n", hubinfo, &hubinfo->h_crblock);

	spin_lock_init(&hubinfo->h_crblock);

	hubinfo->h_widgetid = hub_widget_id(hubinfo->h_nasid);
	npda->xbow_peer = INVALID_NASID;
	/* Initialize the linked list of
	 * router info pointers to the dependent routers
	 */
	npda->npda_rip_first = NULL;
	/* npda_rip_last always points to the place
	 * where the next element is to be inserted
	 * into the list 
	 */
	npda->npda_rip_last = &npda->npda_rip_first;
	npda->dependent_routers = 0;
	npda->module_id = INVALID_MODULE;

	/*
	 * Initialize the subnodePDA.
	 */
	for (sn=0; sn<NUM_SUBNODES; sn++) {
		SNPDA(npda,sn)->prof_count = 0;
		SNPDA(npda,sn)->next_prof_timeout = 0;
// ajm
#ifndef CONFIG_IA64_SGI_IO
		intr_init_vecblk(npda, node, sn);
#endif
	}

	npda->vector_unit_busy = 0;

	spin_lock_init(&npda->vector_lock);
	init_MUTEX_LOCKED(&npda->xbow_sema); /* init it locked? */
	spin_lock_init(&npda->fprom_lock);

	spin_lock_init(&npda->node_utlbswitchlock);
	npda->ni_error_print = 0;
#ifndef CONFIG_IA64_SGI_IO
	if (need_utlbmiss_patch) {
		npda->node_need_utlbmiss_patch = 1;
		npda->node_utlbmiss_patched = 1;
	}
#endif

	/*
	 * Clear out the nasid mask.
	 */
	for (i = 0; i < NASID_MASK_BYTES; i++)
		npda->nasid_mask[i] = 0;

	for (i = 0; i < numnodes; i++) {
		nasid_t nasid = COMPACT_TO_NASID_NODEID(i);

		/* Set my mask bit */
		npda->nasid_mask[nasid / 8] |= (1 << nasid % 8);
	}

#ifndef CONFIG_IA64_SGI_IO
	npda->node_first_cpu = get_cnode_cpu(node);
#endif

	if (npda->node_first_cpu != CPU_NONE) {
		/*
		 * Count number of cpus only if first CPU is valid.
		 */
		numcpus_p = &npda->node_num_cpus;
		*numcpus_p = 0;
		for (i = npda->node_first_cpu; i < MAXCPUS; i++) {
			if (CPUID_TO_COMPACT_NODEID(i) != node)
			    break;
			else
			    (*numcpus_p)++;
		}
	} else {
		npda->node_num_cpus = 0; 
	}

	/* Allocate memory for the dump stack on each node 
	 * This is useful during nmi handling since we
	 * may not be guaranteed shared memory at that time
	 * which precludes depending on a global dump stack
	 */
#ifndef CONFIG_IA64_SGI_IO
	npda->dump_stack = (uint64_t *)kmem_zalloc_node(DUMP_STACK_SIZE,VM_NOSLEEP,
							  node);
	ASSERT_ALWAYS(npda->dump_stack);
	ASSERT(npda->dump_stack);
#endif
	/* Initialize the counter which prevents
	 * both the cpus on a node to proceed with nmi
	 * handling.
	 */
#ifndef CONFIG_IA64_SGI_IO
	npda->dump_count = 0;

	/* Setup the (module,slot) --> nic mapping for all the routers
	 * in the system. This is useful during error handling when
	 * there is no shared memory.
	 */
	router_map_init(npda);

	/* Allocate memory for the per-node router traversal queue */
	router_queue_init(npda,node);
	npda->sbe_info = kmem_zalloc_node_hint(sizeof (sbe_info_t), 0, node);
	ASSERT(npda->sbe_info);

#ifdef CONFIG_SGI_IP35 || CONFIG_IA64_SGI_SN1 || CONFIG_IA64_GENERIC
	/*
	 * Initialize bte info pointers to NULL
	 */
	for (i = 0; i < BTES_PER_NODE; i++) {
		npda->node_bte_info[i] = (bteinfo_t *)NULL;
	}
#endif
#endif /* CONFIG_IA64_SGI_IO */
}

/* XXX - Move the interrupt stuff to intr.c ? */
/*
 * Set up the platform-dependent fields in the processor pda.
 * Must be done _after_ init_platform_nodepda().
 * If we need a lock here, something else is wrong!
 */
// void init_platform_pda(pda_t *ppda, cpuid_t cpu)
void init_platform_pda(cpuid_t cpu)
{
	hub_intmasks_t *intmasks;
	cpuinfo_t cpuinfo;
	int i;
	cnodeid_t	cnode;
	synergy_da_t	*sda;
	int	which_synergy;

#ifndef CONFIG_IA64_SGI_IO
	/* Allocate per-cpu platform-dependent data */
	cpuinfo = (cpuinfo_t)kmem_alloc_node(sizeof(struct cpuinfo_s), GFP_ATOMIC, cputocnode(cpu));
	ASSERT_ALWAYS(cpuinfo);
	ppda->pdinfo = (void *)cpuinfo;
	cpuinfo->ci_cpupda = ppda;
	cpuinfo->ci_cpuid = cpu;
#endif

	cnode = cpuid_to_cnodeid(cpu);
	which_synergy = cpuid_to_synergy(cpu);
	sda = Synergy_da_indr[(cnode * 2) + which_synergy];
	// intmasks = &ppda->p_intmasks;
	intmasks = &sda->s_intmasks;

#ifndef CONFIG_IA64_SGI_IO
	ASSERT_ALWAYS(&ppda->p_nodepda);
#endif

	/* Clear INT_PEND0 masks. */
	for (i = 0; i < N_INTPEND0_MASKS; i++)
		intmasks->intpend0_masks[i] = 0;

	/* Set up pointer to the vector block in the nodepda. */
	/* (Cant use SUBNODEPDA - not working yet) */
	intmasks->dispatch0 = &Nodepdaindr[cnode]->snpda[cputosubnode(cpu)].intr_dispatch0;
	intmasks->dispatch1 = &Nodepdaindr[cnode]->snpda[cputosubnode(cpu)].intr_dispatch1;

	/* Clear INT_PEND1 masks. */
	for (i = 0; i < N_INTPEND1_MASKS; i++)
		intmasks->intpend1_masks[i] = 0;


#ifndef CONFIG_IA64_SGI_IO
	/* Don't read the routers unless we're the master. */
	ppda->p_routertick = 0;
#endif

}

#if (defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)) && !defined(BRINGUP)	/* protect low mem for IP35/7 */
#error "need protect_hub_calias, protect_nmi_handler_data"
#endif

#ifndef CONFIG_IA64_SGI_IO
/*
 * For now, just protect the first page (exception handlers). We
 * may want to protect more stuff later.
 */
void
protect_hub_calias(nasid_t nasid)
{
	paddr_t pa = NODE_OFFSET(nasid) + 0; /* page 0 on node nasid */
	int i;

	for (i = 0; i < MAX_REGIONS; i++) {
		if (i == nasid_to_region(nasid))
			continue;
#ifndef BRINGUP
		/* Protect the exception handlers. */
		*(__psunsigned_t *)BDPRT_ENTRY(pa, i) = MD_PROT_NO;

		/* Protect the ARCS SPB. */
		*(__psunsigned_t *)BDPRT_ENTRY(pa + 4096, i) = MD_PROT_NO;
#endif
	}
}

/*
 * Protect the page of low memory used to communicate with the NMI handler.
 */
void
protect_nmi_handler_data(nasid_t nasid, int slice)
{
	paddr_t pa = NODE_OFFSET(nasid) + NMI_OFFSET(nasid, slice);
	int i;

	for (i = 0; i < MAX_REGIONS; i++) {
		if (i == nasid_to_region(nasid))
			continue;
#ifndef BRINGUP
		*(__psunsigned_t *)BDPRT_ENTRY(pa, i) = MD_PROT_NO;
#endif
	}
}
#endif /* CONFIG_IA64_SGI_IO */


#ifdef IRIX
/*
 * Protect areas of memory that we access uncached by marking them as
 * poisoned so the T5 can't read them speculatively and erroneously
 * mark them dirty in its cache only to write them back with old data
 * later.
 */
static void
protect_low_memory(nasid_t nasid)
{
	/* Protect low memory directory */
	poison_state_alter_range(KLDIR_ADDR(nasid), KLDIR_SIZE, 1);

	/* Protect klconfig area */
	poison_state_alter_range(KLCONFIG_ADDR(nasid), KLCONFIG_SIZE(nasid), 1);

	/* Protect the PI error spool area. */
	poison_state_alter_range(PI_ERROR_ADDR(nasid), PI_ERROR_SIZE(nasid), 1);

	/* Protect CPU A's cache error eframe area. */
	poison_state_alter_range(TO_NODE_UNCAC(nasid, CACHE_ERR_EFRAME),
				CACHE_ERR_AREA_SIZE, 1);

	/* Protect CPU B's area */
	poison_state_alter_range(TO_NODE_UNCAC(nasid, CACHE_ERR_EFRAME)
				^ UALIAS_FLIP_BIT,
				CACHE_ERR_AREA_SIZE, 1);
#error "SN1 not handled correctly"
}
#endif	/* IRIX */

/*
 * per_hub_init
 *
 * 	This code is executed once for each Hub chip.
 */
void
per_hub_init(cnodeid_t cnode)
{
	uint64_t	done;
	nasid_t		nasid;
	nodepda_t	*npdap;
#if defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)	/* SN1 specific */
	ii_icmr_u_t	ii_icmr;
	ii_ibcr_u_t	ii_ibcr;
#endif
#ifndef CONFIG_IA64_SGI_IO
	int i;
#endif

#ifdef SIMULATED_KLGRAPH
	compact_to_nasid_node[0] = 0;
	nasid_to_compact_node[0] = 0;
	FIXME("per_hub_init: SIMULATED_KLCONFIG: compact_to_nasid_node[0] = 0\n");
#endif /* SIMULATED_KLGRAPH */
	nasid = COMPACT_TO_NASID_NODEID(cnode);

	ASSERT(nasid != INVALID_NASID);
	ASSERT(NASID_TO_COMPACT_NODEID(nasid) == cnode);

	/* Grab the hub_mask lock. */
	spin_lock(&hub_mask_lock);

	/* Test our bit. */
	if (!(done = CNODEMASK_TSTB(hub_init_mask, cnode))) {

		/* Turn our bit on in the mask. */
		CNODEMASK_SETB(hub_init_mask, cnode);
	}

#if defined(SN0_HWDEBUG)
	hub_config_setup();
#endif
	/* Release the hub_mask lock. */
	spin_unlock(&hub_mask_lock);

	/*
	 * Do the actual initialization if it hasn't been done yet.
	 * We don't need to hold a lock for this work.
	 */
	if (!done) {
		npdap = NODEPDA(cnode);

		npdap->hub_chip_rev = get_hub_chiprev(nasid);

#ifndef CONFIG_IA64_SGI_IO
		for (i = 0; i < CPUS_PER_NODE; i++) {
			cpu = cnode_slice_to_cpuid(cnode, i);
			if (!cpu_enabled(cpu))
			    SET_CPU_LEDS(nasid, i, 0xf);
		}
#endif /* CONFIG_IA64_SGI_IO */

#if defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC) /* SN1 specific */

		/*
		 * Set the total number of CRBs that can be used.
		 */
		ii_icmr.ii_icmr_regval= 0x0;
		ii_icmr.ii_icmr_fld_s.i_c_cnt = 0xF;
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
		REMOTE_HUB_S(nasid, IIO_ICTP, 0x1000 );
		REMOTE_HUB_S(nasid, IIO_ICTO, 0xff);

#endif /* SN0_HWDEBUG */


#ifndef CONFIG_IA64_SGI_IO

		/* Reserve all of the hardwired interrupt levels. */
		intr_reserve_hardwired(cnode);

		/* Initialize error interrupts for this hub. */
		hub_error_init(cnode);

		/* Set up correctable memory/directory ECC error interrupt. */
		install_eccintr(cnode);

		/* Protect our exception vectors from accidental corruption. */
		protect_hub_calias(nasid);

		/* Enable RT clock interrupts */
		hub_rtc_init(cnode);
		hub_migrintr_init(cnode); /* Enable migration interrupt */
#endif

		spin_lock(&hub_mask_lock);
		CNODEMASK_SETB(hub_init_done_mask, cnode);
		spin_unlock(&hub_mask_lock);

	} else {
		/*
		 * Wait for the other CPU to complete the initialization.
		 */
		while (CNODEMASK_TSTB(hub_init_done_mask, cnode) == 0)
			/* LOOP */
			;
	}
}

extern void
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

hubreg_t
get_region(cnodeid_t cnode)
{
	if (fine_mode)
		return COMPACT_TO_NASID_NODEID(cnode) >> NASID_TO_FINEREG_SHFT;
	else
		return COMPACT_TO_NASID_NODEID(cnode) >> NASID_TO_COARSEREG_SHFT;
}

hubreg_t
nasid_to_region(nasid_t nasid)
{
	if (fine_mode)
		return nasid >> NASID_TO_FINEREG_SHFT;
	else
		return nasid >> NASID_TO_COARSEREG_SHFT;
}

