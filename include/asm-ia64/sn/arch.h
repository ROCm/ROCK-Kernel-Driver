/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI specific setup.
 *
 * Copyright (C) 1995 - 1997, 1999 Silcon Graphics, Inc.
 * Copyright (C) 1999 Ralf Baechle (ralf@gnu.org)
 */
#ifndef _ASM_SN_ARCH_H
#define _ASM_SN_ARCH_H

#include <linux/types.h>
#include <linux/config.h>

#if defined(CONFIG_IA64_SGI_IO)
#include <asm/sn/types.h>
#if defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_SGI_IP37) || defined(CONFIG_IA64_GENERIC)
#include <asm/sn/sn1/arch.h>
#endif
#endif	/* CONFIG_IA64_SGI_IO */


#if defined(_LANGUAGE_C) || defined(_LANGUAGE_C_PLUS_PLUS)
typedef u64	hubreg_t;
typedef u64	nic_t;
#if defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
typedef u64     bdrkreg_t;
#endif	/* CONFIG_SGI_xxxxx */
#endif	/* _LANGUAGE_C || _LANGUAGE_C_PLUS_PLUS */

#if defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
#define CPUS_PER_NODE		4	/* CPUs on a single hub */
#define CPUS_PER_NODE_SHFT	2	/* Bits to shift in the node number */
#define CPUS_PER_SUBNODE	2	/* CPUs on a single hub PI */
#endif
#define CNODE_NUM_CPUS(_cnode)		(NODEPDA(_cnode)->node_num_cpus)

#define CNODE_TO_CPU_BASE(_cnode)	(NODEPDA(_cnode)->node_first_cpu)

#define makespnum(_nasid, _slice)					\
		(((_nasid) << CPUS_PER_NODE_SHFT) | (_slice))

#if defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)

/*
 * There are 2 very similar macros for dealing with "slices". Make sure
 * you use the right one. 
 * Unfortunately, on all platforms except IP35 (currently), the 2 macros 
 * are interchangible. 
 *
 * On IP35, there are 4 cpus per node. Each cpu is refered to by it's slice.
 * The slices are numbered 0 thru 3. 
 *
 * There are also 2 PI interfaces per node. Each PI interface supports 2 cpus.
 * The term "local slice" specifies the cpu number relative to the PI.
 *
 * The cpus on the node are numbered:
 *	slice	localslice
 *	  0          0
 *	  1          1
 *	  2          0
 *	  3          1
 *
 *	cputoslice - returns a number 0..3 that is the slice of the specified cpu.
 *	cputolocalslice - returns a number 0..1 that identifies the local slice of
 *			the cpu within it's PI interface.
 */
#ifdef notyet
	/* These are dummied up for now ..... */
#define cputocnode(cpu)				\
               (pdaindr[(cpu)].p_nodeid)
#define cputonasid(cpu)				\
               (pdaindr[(cpu)].p_nasid)
#define cputoslice(cpu)				\
               (ASSERT(pdaindr[(cpu)].pda), (pdaindr[(cpu)].pda->p_slice))
#define cputolocalslice(cpu)			\
               (ASSERT(pdaindr[(cpu)].pda), (LOCALCPU(pdaindr[(cpu)].pda->p_slice)))
#define cputosubnode(cpu)			\
		(ASSERT(pdaindr[(cpu)].pda), (SUBNODE(pdaindr[(cpu)].pda->p_slice)))
#else
#define cputocnode(cpu) 0
#define cputonasid(cpu) 0
#define cputoslice(cpu) 0
#define cputolocalslice(cpu) 0
#define cputosubnode(cpu) 0
#endif	/* notyet */
#endif	/* CONFIG_SGI_IP35 */

#if defined(_LANGUAGE_C) || defined(_LANGUAGE_C_PLUS_PLUS)

#define INVALID_NASID		(nasid_t)-1
#define INVALID_CNODEID		(cnodeid_t)-1
#define INVALID_PNODEID		(pnodeid_t)-1
#define INVALID_MODULE		(moduleid_t)-1
#define	INVALID_PARTID		(partid_t)-1

#if defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
extern int     get_slice(void);
extern cpuid_t get_cnode_cpu(cnodeid_t);
extern int get_cpu_slice(cpuid_t);
extern cpuid_t cnodetocpu(cnodeid_t);
// extern cpuid_t cnode_slice_to_cpuid(cnodeid_t, int);

extern int cnode_exists(cnodeid_t cnode);
extern cnodeid_t cpuid_to_compact_node[MAXCPUS];
#endif	/* CONFIG_IP35 */

extern nasid_t get_nasid(void);
extern cnodeid_t get_cpu_cnode(int);
extern int get_cpu_slice(cpuid_t);

/*
 * NO ONE should access these arrays directly.  The only reason we refer to
 * them here is to avoid the procedure call that would be required in the
 * macros below.  (Really want private data members here :-)
 */
extern cnodeid_t nasid_to_compact_node[MAX_NASIDS];
extern nasid_t compact_to_nasid_node[MAX_COMPACT_NODES];

/*
 * These macros are used by various parts of the kernel to convert
 * between the three different kinds of node numbering.   At least some
 * of them may change to procedure calls in the future, but the macros
 * will continue to work.  Don't use the arrays above directly.
 */

#define	NASID_TO_REGION(nnode)	      	\
    ((nnode) >> \
     (is_fine_dirmode() ? NASID_TO_FINEREG_SHFT : NASID_TO_COARSEREG_SHFT))

extern cnodeid_t nasid_to_compact_node[MAX_NASIDS];
extern nasid_t compact_to_nasid_node[MAX_COMPACT_NODES];
extern cnodeid_t cpuid_to_compact_node[MAXCPUS];

#if !defined(DEBUG)

#define NASID_TO_COMPACT_NODEID(nnode)	(nasid_to_compact_node[nnode])
#define COMPACT_TO_NASID_NODEID(cnode)	(compact_to_nasid_node[cnode])
#define CPUID_TO_COMPACT_NODEID(cpu)	(cpuid_to_compact_node[(cpu)])
#else

/*
 * These functions can do type checking and fail if they need to return
 * a bad nodeid, but they're not as fast so just use 'em for debug kernels.
 */
cnodeid_t nasid_to_compact_nodeid(nasid_t nasid);
nasid_t compact_to_nasid_nodeid(cnodeid_t cnode);

#define NASID_TO_COMPACT_NODEID(nnode)	nasid_to_compact_nodeid(nnode)
#define COMPACT_TO_NASID_NODEID(cnode)	compact_to_nasid_nodeid(cnode)
#define CPUID_TO_COMPACT_NODEID(cpu)	(cpuid_to_compact_node[(cpu)])
#endif

extern int node_getlastslot(cnodeid_t);

#endif /* _LANGUAGE_C || _LANGUAGE_C_PLUS_PLUS */

#define SLOT_BITMASK    	(MAX_MEM_SLOTS - 1)
#define SLOT_SIZE		(1LL<<SLOT_SHIFT)

#define node_getnumslots(node)	(MAX_MEM_SLOTS)
#define NODE_MAX_MEM_SIZE	SLOT_SIZE * MAX_MEM_SLOTS

/*
 * New stuff in here from Irix sys/pfdat.h.
 */
#define	SLOT_PFNSHIFT		(SLOT_SHIFT - PAGE_SHIFT)
#define	PFN_NASIDSHFT		(NASID_SHFT - PAGE_SHIFT)
#define mkpfn(nasid, off)	(((pfn_t)(nasid) << PFN_NASIDSHFT) | (off))
#define slot_getbasepfn(node,slot) \
		(mkpfn(COMPACT_TO_NASID_NODEID(node), slot<<SLOT_PFNSHIFT))
#endif /* _ASM_SN_ARCH_H */
