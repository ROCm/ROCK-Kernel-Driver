/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_NODEPDA_H
#define _ASM_SN_NODEPDA_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <linux/config.h>
#include <asm/sn/agent.h>
#include <asm/sn/intr.h>
#include <asm/sn/router.h>
/* #include <SN/klkernvars.h> */
#ifdef IRIX
typedef struct module_s module_t;       /* Avoids sys/SN/module.h */
#else
#include <asm/sn/module.h>
#endif
/* #include <SN/slotnum.h> */

/*
 * NUMA Node-Specific Data structures are defined in this file.
 * In particular, this is the location of the node PDA.
 * A pointer to the right node PDA is saved in each CPU PDA.
 */

/*
 * Subnode PDA structures. Each node needs a few data structures that 
 * correspond to the PIs on the HUB chip that supports the node.
 *
 * WARNING!!!! 6.5.x compatibility requirements prevent us from
 * changing or reordering fields in the following structure for IP27.
 * It is essential that the data mappings not change for IP27 platforms.
 * It is OK to add fields that are IP35 specific if they are under #ifdef IP35.
 */
struct subnodepda_s {
	intr_vecblk_t	intr_dispatch0;
	intr_vecblk_t	intr_dispatch1;
	uint64_t	next_prof_timeout;
	int		prof_count;
};


typedef struct subnodepda_s subnode_pda_t;


struct ptpool_s;


/*
 * Node-specific data structure.
 *
 * One of these structures is allocated on each node of a NUMA system.
 * Non-NUMA systems are considered to be systems with one node, and
 * hence there will be one of this structure for the entire system.
 *
 * This structure provides a convenient way of keeping together 
 * all per-node data structures. 
 */


#ifndef CONFIG_IA64_SGI_IO
/*
 * The following structure is contained in the nodepda & contains
 * a lock & queue-head for sanon pages that belong to the node.
 * See the anon manager for more details.
 */
typedef struct {
	lock_t  sal_lock;
	plist_t sal_listhead;
} sanon_list_head_t;
#endif



struct nodepda_s {

#ifdef	NUMA_BASE

	/* 
	 * Pointer to this node's copy of Nodepdaindr 
	 */
	struct nodepda_s	**pernode_pdaindr; 

	/*
         * Data used for migration control
         */
	struct migr_control_data_s *mcd; 

	/*
         * Data used for replication control
         */
	struct repl_control_data_s *rcd;

        /*
         * Numa statistics
         */
	struct numa_stats_s *numa_stats;

        /*
         * Load distribution
         */
        uint memfit_assign;

        /*
         * New extended memory reference counters
         */
        void *migr_refcnt_counterbase;
        void *migr_refcnt_counterbuffer;
        size_t migr_refcnt_cbsize;
        int  migr_refcnt_numsets;

        /*
         * mem_tick quiescing lock
         */
        uint mem_tick_lock;

        /*
         * Migration candidate set
         * by migration prologue intr handler
         */
        uint64_t migr_candidate;

	/*
	 * Each node gets its own syswait counter to remove contention
	 * on the global one.
	 */
#ifndef CONFIG_IA64_SGI_IO
	struct syswait syswait;
#endif

#endif	/* NUMA_BASE */
	/*
	 * Node-specific Zone structures.
	 */
#ifndef CONFIG_IA64_SGI_IO
	zoneset_element_t	node_zones;
	pg_data_t	node_pg_data;	/* VM page data structures */ 
	plist_t	error_discard_plist;
#endif
	uint		error_discard_count;
	uint		error_page_count;
	uint		error_cleaned_count;
	spinlock_t	error_discard_lock;

	/* Information needed for SN Hub chip interrupt handling. */
	subnode_pda_t	snpda[NUM_SUBNODES];
	/* Distributed kernel support */
#ifndef CONFIG_IA64_SGI_IO
	kern_vars_t	kern_vars;
#endif
	/* Vector operation support */
	/* Change this to a sleep lock? */
	spinlock_t	vector_lock;
	/* State of the vector unit for this node */
	char		vector_unit_busy;
	cpuid_t         node_first_cpu; /* Starting cpu number for node */
	ushort          node_num_cpus;  /* Number of cpus present       */

	/* node utlbmiss info */
  	spinlock_t		node_utlbswitchlock;
	volatile cpumask_t	node_utlbmiss_flush;
	volatile signed char	node_need_utlbmiss_patch;
	volatile char		node_utlbmiss_patched;
	nodepda_router_info_t	*npda_rip_first;
	nodepda_router_info_t	**npda_rip_last;
	int		dependent_routers;
	devfs_handle_t 	xbow_vhdl;
	nasid_t		xbow_peer;	/* NASID of our peer hub on xbow */
	struct semaphore xbow_sema;	/* Sema for xbow synchronization */
	slotid_t	slotdesc;
	moduleid_t	module_id;	/* Module ID (redundant local copy) */
	module_t	*module;	/* Pointer to containing module */
	int		hub_chip_rev;	/* Rev of my Hub chip */
	char		nasid_mask[NASID_MASK_BYTES];
					/* Need a copy of the nasid mask
					 * on every node */
	xwidgetnum_t 	basew_id;
	devfs_handle_t 	basew_xc;
	spinlock_t	fprom_lock;
	char		ni_error_print; /* For printing ni error state
					 * only once during system panic
					 */
#ifndef CONFIG_IA64_SGI_IO
	md_perf_monitor_t node_md_perfmon;
	hubstat_t	hubstats;
	int		hubticks;
	int		huberror_ticks;
	sbe_info_t	*sbe_info;	/* ECC single-bit error statistics */
#endif	/* !CONFIG_IA64_SGI_IO */

	router_queue_t  *visited_router_q;
	router_queue_t	*bfs_router_q; 
					/* Used for router traversal */
#if defined (CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
	router_map_ent_t router_map[MAX_RTR_BREADTH];
#endif
	int		num_routers;	/* Total routers in the system */

        char		membank_flavor;
	                                /* Indicates what sort of memory 
					 * banks are present on this node
					 */
	
	char		*hwg_node_name;	/* hwgraph node name */

	struct widget_info_t *widget_info;	/* Node as xtalk widget */
	devfs_handle_t	node_vertex;	/* Hwgraph vertex for this node */

	void 		*pdinfo;	/* Platform-dependent per-node info */
	uint64_t	*dump_stack;	/* Dump stack during nmi handling */
	int		dump_count;	/* To allow only one cpu-per-node */
#if defined BRINGUP
#ifndef CONFIG_IA64_SGI_IO
	io_perf_monitor_t node_io_perfmon;
#endif
#endif

	/*
	 * Each node gets its own pdcount counter to remove contention
	 * on the global one.
	 */

	int pdcount;			/* count of pdinserted pages */

#ifdef	NUMA_BASE
	void		*cached_global_pool;	/* pointer to cached vmpool */
#endif /* NUMA_BASE */

#ifndef CONFIG_IA64_SGI_IO
	sanon_list_head_t sanon_list_head;	/* head for sanon pages */	
#endif
#ifdef	NUMA_BASE
	struct ptpool_s	*ptpool;	/* ptpool for this node */
#endif /* NUMA_BASE */

	/*
	 * The BTEs on this node are shared by the local cpus
	 */
#if defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
#ifndef CONFIG_IA64_SGI_IO
	bteinfo_t	*node_bte_info[BTES_PER_NODE];
#endif
#endif
};

typedef struct nodepda_s nodepda_t;


#define NODE_MODULEID(_node)	(NODEPDA(_node)->module_id)
#define NODE_SLOTID(_node)	(NODEPDA(_node)->slotdesc)

#ifdef	NUMA_BASE
/*
 * Access Functions for node PDA.
 * Since there is one nodepda for each node, we need a convenient mechanism
 * to access these nodepdas without cluttering code with #ifdefs.
 * The next set of definitions provides this.
 * Routines are expected to use 
 *
 *	nodepda		-> to access PDA for the node on which code is running
 *	subnodepda	-> to access subnode PDA for the node on which code is running
 *
 *	NODEPDA(x)	-> to access node PDA for cnodeid 'x'
 *	SUBNODEPDA(x,s)	-> to access subnode PDA for cnodeid/slice 'x'
 */

#ifndef CONFIG_IA64_SGI_IO
#define	nodepda		private.p_nodepda	/* Ptr to this node's PDA */
#if CONFIG_SGI_IP35 || CONFIG_IA64_SGI_SN1 || CONFIG_IA64_GENERIC
#define subnodepda	private.p_subnodepda	/* Ptr to this node's subnode PDA */
#endif

#else
/*
 * Until we have a shared node local area defined, do it this way ..
 * like in Caliase space.  See above.
 */
extern nodepda_t        *nodepda;
extern subnode_pda_t	*subnodepda;
#endif

/* 
 * Nodepdaindr[]
 * This is a private data structure for use only in early initialization.
 * All users of nodepda should use the macro NODEPDA(nodenum) to get
 * the suitable nodepda structure.
 * This macro has the advantage of not requiring #ifdefs for NUMA and
 * non-NUMA code.
 */
extern nodepda_t	*Nodepdaindr[]; 
/*
 * NODEPDA_GLOBAL(x) macro should ONLY be used during early initialization.
 * Once meminit is complete, NODEPDA(x) is ready to use.
 * During early init, the system fills up Nodepdaindr.  By the time we
 * are in meminit(), all nodepdas are initialized, and hence
 * we can fill up the node_pdaindr array in each nodepda structure.
 */
#define	NODEPDA_GLOBAL(x)	Nodepdaindr[x]

/*
 * Returns a pointer to a given node's nodepda.
 */
#define	NODEPDA(x)		(nodepda->pernode_pdaindr[x])

/*
 * Returns a pointer to a given node/slice's subnodepda.
 *	SUBNODEPDA(cnode, subnode) - uses cnode as first arg
 *	SNPDA(npda, subnode)	   - uses pointer to nodepda as first arg
 */
#define	SUBNODEPDA(x,sn)	(&nodepda->pernode_pdaindr[x]->snpda[sn])
#define	SNPDA(npda,sn)		(&(npda)->snpda[sn])

#define NODEPDA_ERROR_FOOTPRINT(node, cpu) \
                   (&(NODEPDA(node)->error_stamp[cpu]))
#define NODEPDA_MDP_MON(node)	(&(NODEPDA(node)->node_md_perfmon))
#define NODEPDA_IOP_MON(node)	(&(NODEPDA(node)->node_io_perfmon))

/*
 * Macros to access data structures inside nodepda 
 */
#if NUMA_MIGR_CONTROL
#define NODEPDA_MCD(node) (NODEPDA(node)->mcd)
#endif /* NUMA_MIGR_CONTROL */

#if NUMA_REPL_CONTROL
#define NODEPDA_RCD(node) (NODEPDA(node)->rcd)
#endif /* NUMA_REPL_CONTROL */

#if (NUMA_MIGR_CONTROL || NUMA_REPL_CONTROL)
#define NODEPDA_LRS(node) (NODEPDA(node)->lrs)
#endif /* (NUMA_MIGR_CONTROL || NUMA_REPL_CONTROL) */

/* 
 * Exported functions
 */
extern nodepda_t *nodepda_alloc(void);

#else	/* !NUMA_BASE */
/*
 * For a single-node system we will just have one global nodepda pointer
 * allocated at startup.  The global nodepda will point to this nodepda 
 * structure.
 */
extern nodepda_t	*Nodepdaindr; 

/*
 * On non-NUMA systems, NODEPDA_GLOBAL and NODEPDA macros collapse to
 * be the same.
 */
#define	NODEPDA_GLOBAL(x)	Nodepdaindr

/*
 * Returns a pointer to a given node's nodepda.
 */
#define	NODEPDA(x)	Nodepdaindr

/*
 * nodepda can also be defined as private.p_nodepda.
 * But on non-NUMA systems, there is only one nodepda, and there is
 * no reason to go through the PDA to access this pointer.
 * Hence nodepda aliases to the global nodepda directly.
 *
 * Routines should use nodepda to access the local node's PDA.
 */
#define	nodepda		(Nodepdaindr)

#endif	/* NUMA_BASE */

/* Quickly convert a compact node ID into a hwgraph vertex */
#define cnodeid_to_vertex(cnodeid) (NODEPDA(cnodeid)->node_vertex)


/* Check if given a compact node id the corresponding node has all the
 * cpus disabled. 
 */
#define is_headless_node(_cnode)	((_cnode == CNODEID_NONE) || \
					 (CNODE_NUM_CPUS(_cnode) == 0))
/* Check if given a node vertex handle the corresponding node has all the
 * cpus disabled. 
 */
#define is_headless_node_vertex(_nodevhdl) \
			is_headless_node(nodevertex_to_cnodeid(_nodevhdl))

#ifdef	__cplusplus
}
#endif

#ifdef NUMA_BASE
/*
 * To remove contention on the global syswait counter each node will have
 * its own.  Each clock tick the clock cpu will re-calculate the global
 * syswait counter by summing from each of the nodes.  The other cpus will
 * continue to read the global one during their clock ticks.   This does 
 * present a problem when a thread increments the count on one node and wakes
 * up on a different node and decrements it there.  Eventually the count could
 * overflow if this happens continually for a long period.  To prevent this
 * second_thread() periodically preserves the current syswait state and
 * resets the counters.
 */
#define ADD_SYSWAIT(_field)	atomicAddInt(&nodepda->syswait._field, 1)
#define SUB_SYSWAIT(_field)	atomicAddInt(&nodepda->syswait._field, -1)
#else
#define ADD_SYSWAIT(_field)				\
{							\
	ASSERT(syswait._field >= 0);			\
	atomicAddInt(&syswait._field, 1);		\
}
#define SUB_SYSWAIT(_field)				\
{							\
	ASSERT(syswait._field > 0);			\
	atomicAddInt(&syswait._field, -1);		\
}
#endif /* NUMA_BASE */

#ifdef NUMA_BASE
/*
 * Another global variable to remove contention from: pdcount.
 * See above comments for SYSWAIT.
 */
#define ADD_PDCOUNT(_n)					\
{							\
	atomicAddInt(&nodepda->pdcount, _n);		\
	if (_n > 0 && !pdflag)				\
		pdflag = 1;				\
}
#else
#define ADD_PDCOUNT(_n)					\
{							\
	ASSERT(&pdcount >= 0);				\
	atomicAddInt(&pdcount, _n);			\
	if (_n > 0 && !pdflag)				\
		pdflag = 1;				\
}
#endif /* NUMA_BASE */

#endif /* _ASM_SN_NODEPDA_H */
