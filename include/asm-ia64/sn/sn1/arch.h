/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2001 Silicon Graphics, Inc. All rights reserved.
 */
#ifndef _ASM_IA64_SN_SN1_ARCH_H
#define _ASM_IA64_SN_SN1_ARCH_H

#if defined(N_MODE)
#error "ERROR constants defined only for M-mode"
#endif

#include <linux/threads.h>
#include <asm/types.h>

#define CPUS_PER_NODE           4       /* CPUs on a single hub */
#define CPUS_PER_SUBNODE        2       /* CPUs on a single hub PI */

/*
 * This is the maximum number of NASIDS that can be present in a system.
 * This include ALL nodes in ALL partitions connected via NUMALINK.
 * (Highest NASID plus one.)
 */
#define MAX_NASIDS              128

/*
 * This is the maximum number of nodes that can be part of a kernel.
 * Effectively, it's the maximum number of compact node ids (cnodeid_t).
 * This is not necessarily the same as MAX_NASIDS.
 */
#define MAX_COMPACT_NODES       128

/*
 * MAX_REGIONS refers to the maximum number of hardware partitioned regions.
 */
#define	MAX_REGIONS		64
#define MAX_NONPREMIUM_REGIONS  16
#define MAX_PREMIUM_REGIONS     MAX_REGIONS

/*
 * Slot constants for IP35
 */

#define MAX_MEM_SLOTS    8                     /* max slots per node */

#if defined(N_MODE)
#error "N-mode not supported"
#endif

#define SLOT_SHIFT              (30)
#define SLOT_MIN_MEM_SIZE       (64*1024*1024)


/*
 * MAX_PARITIONS refers to the maximum number of logically defined 
 * partitions the system can support.
 */
#define MAX_PARTITIONS		MAX_REGIONS


#define NASID_MASK_BYTES	((MAX_NASIDS + 7) / 8)

/*
 * New stuff in here from Irix sys/pfdat.h.
 */
#define SLOT_PFNSHIFT           (SLOT_SHIFT - PAGE_SHIFT)
#define PFN_NASIDSHFT           (NASID_SHFT - PAGE_SHIFT)
#define slot_getbasepfn(node,slot)  (mkpfn(COMPACT_TO_NASID_NODEID(node), slot<<SLOT_PFNSHIFT))
#define mkpfn(nasid, off)       (((pfn_t)(nasid) << PFN_NASIDSHFT) | (off))



/*
 * two PIs per bedrock, two CPUs per PI
 */
#define NUM_SUBNODES	2
#define SUBNODE_SHFT	1
#define SUBNODE_MASK	(0x1 << SUBNODE_SHFT)
#define LOCALCPU_SHFT	0
#define LOCALCPU_MASK	(0x1 << LOCALCPU_SHFT)
#define SUBNODE(slice)	(((slice) & SUBNODE_MASK) >> SUBNODE_SHFT)
#define LOCALCPU(slice)	(((slice) & LOCALCPU_MASK) >> LOCALCPU_SHFT)
#define TO_SLICE(subn, local)	(((subn) << SUBNODE_SHFT) | \
				 ((local) << LOCALCPU_SHFT))

#endif /* _ASM_IA64_SN_SN1_ARCH_H */
