/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_SN1_ARCH_H
#define _ASM_SN_SN1_ARCH_H

#if defined(N_MODE)
#error "ERROR constants defined only for M-mode"
#endif

/*
 * This is the maximum number of NASIDS that can be present in a system.
 * (Highest NASID plus one.)
 */
#define MAX_NASIDS              128

/*
 * MAXCPUS refers to the maximum number of CPUs in a single kernel.
 * This is not necessarily the same as MAXNODES * CPUS_PER_NODE
 */
#define MAXCPUS                 512

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
 * MAX_PARITIONS refers to the maximum number of logically defined 
 * partitions the system can support.
 */
#define MAX_PARTITIONS		MAX_REGIONS


#define NASID_MASK_BYTES	((MAX_NASIDS + 7) / 8)

/*
 * Slot constants for IP35
 */

#define MAX_MEM_SLOTS    8                     /* max slots per node */

#if defined(N_MODE)
#error "N-mode not supported"
#endif

#define SLOT_SHIFT      	(30)
#define SLOT_MIN_MEM_SIZE	(64*1024*1024)

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

#endif /* _ASM_SN_SN1_ARCH_H */
