/* 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2002 Silicon Graphics, Inc. All rights reserved.
 */


#ifndef _ASM_IA64_SN_SN_CPUID_H
#define _ASM_IA64_SN_SN_CPUID_H

#include <linux/config.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/mmzone.h>
#include <asm/sn/types.h>
#include <asm/current.h>
#include <asm/nodedata.h>


/*
 * Functions for converting between cpuids, nodeids and NASIDs.
 * 
 * These are for SGI platforms only.
 *
 */




/*
 *  Definitions of terms (these definitions are for IA64 ONLY. Other architectures
 *  use cpuid/cpunum quite defferently):
 *
 *	   CPUID - a number in range of 0..NR_CPUS-1 that uniquely identifies
 *		the cpu. The value cpuid has no significance on IA64 other than
 *		the boot cpu is 0.
 *			smp_processor_id() returns the cpuid of the current cpu.
 *
 *	   CPUNUM - On IA64, a cpunum and cpuid are the same. This is NOT true
 *		on other architectures like IA32.
 *
 * 	   CPU_PHYSICAL_ID (also known as HARD_PROCESSOR_ID)
 *		This is the same as 31:24 of the processor LID register
 *			hard_smp_processor_id()- cpu_physical_id of current processor
 *			cpu_physical_id(cpuid) - convert a <cpuid> to a <physical_cpuid>
 *			cpu_logical_id(phy_id) - convert a <physical_cpuid> to a <cpuid> 
 *				* not real efficient - dont use in perf critical code
 *
 *         LID - processor defined register (see PRM V2).
 *
 *           On SN1
 *		31:24 - id   Contains the NASID
 *		23:16 - eid  Contains 0-3 to identify the cpu on the node
 *				bit 17 - synergy number
 *				bit 16 - FSB slot number 
 *           On SN2
 *		31:28 - id   Contains 0-3 to identify the cpu on the node
 *		27:16 - eid  Contains the NASID
 *
 *
 *
 * The following assumes the following mappings for LID register values:
 *
 * The macros convert between cpu physical ids & slice/fsb/synergy/nasid/cnodeid.
 * These terms are described below:
 *
 *
 *          -----   -----           -----   -----       CPU
 *          | 0 |   | 1 |           | 2 |   | 3 |       SLICE
 *          -----   -----           -----   -----
 *            |       |               |       |
 *            |       |               |       |
 *          0 |       | 1           0 |       | 1       FSB SLOT
 *             -------                 -------  
 *                |                       |
 *                |                       |
 *             -------                 -------
 *             |     |                 |     |
 *             |  0  |                 |  1  |         SYNERGY (SN1 only)
 *             |     |                 |     |
 *             -------                 -------
 *                |                       |
 *                |                       |
 *             -------------------------------
 *             |                             |
 *             |         BEDROCK / SHUB      |        NASID   (0..MAX_NASIDS)
 *             |                             |        CNODEID (0..num_compact_nodes-1)
 *             |                             |
 *             |                             |
 *             -------------------------------
 *                           |
 *
 */

#ifndef CONFIG_SMP
#define cpu_logical_id(cpu)				0
#define cpu_physical_id(cpuid)			((ia64_get_lid() >> 16) & 0xffff)
#endif

#ifdef CONFIG_IA64_SGI_SN1
/*
 * macros for some of these exist in sn/addrs.h & sn/arch.h, etc. However, 
 * trying #include these files here causes circular dependencies.
 */
#define cpu_physical_id_to_nasid(cpi)		((cpi) >> 8)
#define cpu_physical_id_to_synergy(cpi)		(((cpi) >> 1) & 1)
#define cpu_physical_id_to_fsb_slot(cpi)	((cpi) & 1)
#define cpu_physical_id_to_slice(cpi)		((cpi) & 3)
#define get_nasid()				((ia64_get_lid() >> 24))
#define get_slice()				((ia64_get_lid() >> 16) & 3)
#define get_node_number(addr)			(((unsigned long)(addr)>>33) & 0x7f)
#else
#define cpu_physical_id_to_nasid(cpi)		((cpi) &0xfff)
#define cpu_physical_id_to_slice(cpi)		((cpi>>12) & 3)
#define get_nasid()				((ia64_get_lid() >> 16) & 0xfff)
#define get_slice()				((ia64_get_lid() >> 28) & 0xf)
#define get_node_number(addr)			(((unsigned long)(addr)>>38) & 0x7ff)
#endif

/*
 * NOTE: id & eid refer to Intels definitions of the LID register
 *	(id = NASID, eid = slice)
 * NOTE: on non-MP systems, only cpuid 0 exists
 */
#define id_eid_to_cpu_physical_id(id,eid)       (((id)<<8) | (eid))
#define id_eid_to_cpuid(id,eid)         	(cpu_logical_id(id_eid_to_cpu_physical_id((id),(eid))))


/*
 * The following table/struct  is used for managing PTC coherency domains.
 */
typedef struct {
	u8	domain;
	u8	reserved;
	u16	sapicid;
} sn_sapicid_info_t;

extern sn_sapicid_info_t	sn_sapicid_info[];	/* indexed by cpuid */



#ifdef CONFIG_IA64_SGI_SN1
/*
 * cpuid_to_fsb_slot  - convert a cpuid to the fsb slot number that it is in.
 *   (there are 2 cpus per FSB. This function returns 0 or 1)
 */
#define cpuid_to_fsb_slot(cpuid)	(cpu_physical_id_to_fsb_slot(cpu_physical_id(cpuid)))


/*
 * cpuid_to_synergy  - convert a cpuid to the synergy that it resides on
 *   (there are 2 synergies per node. Function returns 0 or 1 to
 *    specify which synergy the cpu is on)
 */
#define cpuid_to_synergy(cpuid)		(cpu_physical_id_to_synergy(cpu_physical_id(cpuid)))

#endif

/*
 * cpuid_to_slice  - convert a cpuid to the slice that it resides on
 *  There are 4 cpus per node. This function returns 0 .. 3)
 */
#define cpuid_to_slice(cpuid)		(cpu_physical_id_to_slice(cpu_physical_id(cpuid)))


/*
 * cpuid_to_nasid  - convert a cpuid to the NASID that it resides on
 */
#define cpuid_to_nasid(cpuid)		(cpu_physical_id_to_nasid(cpu_physical_id(cpuid)))


/*
 * cpuid_to_cnodeid  - convert a cpuid to the cnode that it resides on
 */
#define cpuid_to_cnodeid(cpuid)		(local_node_data->physical_node_map[cpuid_to_nasid(cpuid)])


/*
 * cnodeid_to_nasid - convert a cnodeid to a NASID
 *	Macro relies on pg_data for a node being on the node itself.
 *	Just extract the NASID from the pointer.
 *
 */
#define cnodeid_to_nasid(cnodeid)	(get_node_number(local_node_data->pg_data_ptrs[cnodeid]))
 

/*
 * nasid_to_cnodeid - convert a NASID to a cnodeid
 */
#define nasid_to_cnodeid(nasid)		(local_node_data->physical_node_map[nasid])


/*
 * cnode_slice_to_cpuid - convert a codeid & slice to a cpuid
 */
#define cnode_slice_to_cpuid(cnodeid,slice) (id_eid_to_cpuid(cnodeid_to_nasid(cnodeid),(slice)))
 

/*
 * cpuid_to_subnode - convert a cpuid to the subnode it resides on.
 *   slice 0 & 1 are on subnode 0
 *   slice 2 & 3 are on subnode 1.
 */
#define cpuid_to_subnode(cpuid)		((cpuid_to_slice(cpuid)<2) ? 0 : 1)
 

/*
 * cpuid_to_localslice - convert a cpuid to a local slice
 *    slice 0 & 2 are local slice 0
 *    slice 1 & 3 are local slice 1
 */
#define cpuid_to_localslice(cpuid)	(cpuid_to_slice(cpuid) & 1)
 

#define smp_physical_node_id()			(cpuid_to_nasid(smp_processor_id()))


/*
 * cnodeid_to_cpuid - convert a cnode  to a cpuid of a cpu on the node.
 *	returns -1 if no cpus exist on the node
 */
extern int cnodeid_to_cpuid(int cnode);


#endif /* _ASM_IA64_SN_SN_CPUID_H */

