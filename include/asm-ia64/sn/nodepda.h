/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2002 Silicon Graphics, Inc. All rights reserved.
 */
#ifndef _ASM_IA64_SN_NODEPDA_H
#define _ASM_IA64_SN_NODEPDA_H


#include <linux/config.h>
#include <asm/irq.h>
#include <asm/sn/intr.h>
#include <asm/sn/router.h>
#if defined(CONFIG_IA64_SGI_SN1)
#include <asm/sn/sn1/synergy.h>
#endif
#include <asm/sn/pda.h>
#include <asm/sn/module.h>
#include <asm/sn/bte.h>

#if defined(CONFIG_IA64_SGI_SN1)
#include <asm/sn/sn1/hubstat.h>
#endif

/*
 * NUMA Node-Specific Data structures are defined in this file.
 * In particular, this is the location of the node PDA.
 * A pointer to the right node PDA is saved in each CPU PDA.
 */

/*
 * Subnode PDA structures. Each node needs a few data structures that 
 * correspond to the PIs on the HUB chip that supports the node.
 */
#if defined(CONFIG_IA64_SGI_SN1)
struct subnodepda_s {
	intr_vecblk_t	intr_dispatch0;
	intr_vecblk_t	intr_dispatch1;
};

typedef struct subnodepda_s subnode_pda_t;


struct synergy_perf_s;
#endif


/*
 * Node-specific data structure.
 *
 * One of these structures is allocated on each node of a NUMA system.
 *
 * This structure provides a convenient way of keeping together 
 * all per-node data structures. 
 */



struct nodepda_s {


	cpuid_t         node_first_cpu; /* Starting cpu number for node */
					/* WARNING: no guarantee that   */
					/*  the second cpu on a node is */
					/*  node_first_cpu+1.           */

	devfs_handle_t 	xbow_vhdl;
	nasid_t		xbow_peer;	/* NASID of our peer hub on xbow */
	struct semaphore xbow_sema;	/* Sema for xbow synchronization */
	slotid_t	slotdesc;
	moduleid_t	module_id;	/* Module ID (redundant local copy) */
	module_t	*module;	/* Pointer to containing module */
	xwidgetnum_t 	basew_id;
	devfs_handle_t 	basew_xc;
	int		hubticks;
	int		num_routers;	/* XXX not setup! Total routers in the system */

	
	char		*hwg_node_name;	/* hwgraph node name */
	devfs_handle_t	node_vertex;	/* Hwgraph vertex for this node */

	void 		*pdinfo;	/* Platform-dependent per-node info */


	nodepda_router_info_t	*npda_rip_first;
	nodepda_router_info_t	**npda_rip_last;


	/*
	 * The BTEs on this node are shared by the local cpus
	 */
	bteinfo_t	node_bte_info[BTES_PER_NODE];

#if defined(CONFIG_IA64_SGI_SN1)
	subnode_pda_t	snpda[NUM_SUBNODES];
	/*
	 * New extended memory reference counters
 	 */
	void			*migr_refcnt_counterbase;
	void			*migr_refcnt_counterbuffer;
	size_t			migr_refcnt_cbsize;
	int			migr_refcnt_numsets;
	hubstat_t		hubstats;
	int			synergy_perf_enabled;
        int       		synergy_perf_freq;
	spinlock_t		synergy_perf_lock;
        uint64_t       		synergy_inactive_intervals;
        uint64_t       		synergy_active_intervals;
        struct synergy_perf_s	*synergy_perf_data;
        struct synergy_perf_s	*synergy_perf_first; /* reporting consistency .. */
#endif /* CONFIG_IA64_SGI_SN1 */

	/* 
	 * Array of pointers to the nodepdas for each node.
	 */
	struct nodepda_s	*pernode_pdaindr[MAX_COMPACT_NODES]; 

};

typedef struct nodepda_s nodepda_t;

#ifdef CONFIG_IA64_SGI_SN2
struct irqpda_s {
	int num_irq_used;
	char irq_flags[NR_IRQS];
};

typedef struct irqpda_s irqpda_t;

#endif /* CONFIG_IA64_SGI_SN2 */



/*
 * Access Functions for node PDA.
 * Since there is one nodepda for each node, we need a convenient mechanism
 * to access these nodepdas without cluttering code with #ifdefs.
 * The next set of definitions provides this.
 * Routines are expected to use 
 *
 *	nodepda			-> to access node PDA for the node on which code is running
 *	subnodepda		-> to access subnode PDA for the subnode on which code is running
 *
 *	NODEPDA(cnode)		-> to access node PDA for cnodeid 
 *	SUBNODEPDA(cnode,sn)	-> to access subnode PDA for cnodeid/subnode
 */

#define	nodepda		pda.p_nodepda		/* Ptr to this node's PDA */
#define	NODEPDA(cnode)		(nodepda->pernode_pdaindr[cnode])

#if defined(CONFIG_IA64_SGI_SN1)
#define subnodepda	pda.p_subnodepda	/* Ptr to this node's subnode PDA */
#define	SUBNODEPDA(cnode,sn)	(&(NODEPDA(cnode)->snpda[sn]))
#define	SNPDA(npda,sn)		(&(npda)->snpda[sn])
#endif


/*
 * Macros to access data structures inside nodepda 
 */
#define NODE_MODULEID(cnode)	(NODEPDA(cnode)->module_id)
#define NODE_SLOTID(cnode)	(NODEPDA(cnode)->slotdesc)


/*
 * Quickly convert a compact node ID into a hwgraph vertex
 */
#define cnodeid_to_vertex(cnodeid) (NODEPDA(cnodeid)->node_vertex)


/*
 * Check if given a compact node id the corresponding node has all the
 * cpus disabled. 
 */
#define is_headless_node(cnode)		((cnode == CNODEID_NONE) ||			\
					 (node_data(cnode)->active_cpu_count == 0))

/*
 * Check if given a node vertex handle the corresponding node has all the
 * cpus disabled. 
 */
#define is_headless_node_vertex(_nodevhdl) \
			is_headless_node(nodevertex_to_cnodeid(_nodevhdl))


#endif /* _ASM_IA64_SN_NODEPDA_H */
