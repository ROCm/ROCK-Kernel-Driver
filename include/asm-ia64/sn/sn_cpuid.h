/* 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Jack Steiner (steiner@sgi.com)
 */


#ifndef _ASM_IA64_SN_SN_CPUID_H
#define _ASM_IA64_SN_SN_CPUID_H

#include <linux/config.h>
#include <asm/processor.h>
#include <asm/sn/mmzone_sn1.h>

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
 *		31:24 - id   Contains the NASID
 *		23:16 - eid  Contains 0-3 to identify the cpu on the node
 *				bit 17 - synergy number
 *				bit 16 - FSB slot number 
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
 *             |  0  |                 |  1  |         SYNERGY
 *             |     |                 |     |
 *             -------                 -------
 *                |                       |
 *                |                       |
 *             -------------------------------
 *             |                             |
 *             |         BEDROCK             |        NASID   (0..127)
 *             |                             |        CNODEID (0..numnodes-1)
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

#define cpu_physical_id_to_nasid(cpi)		((cpi) >> 8)
#define cpu_physical_id_to_synergy(cpi)		(((cpi) >> 1) & 1)
#define cpu_physical_id_to_fsb_slot(cpi)	((cpi) & 1)
#define cpu_physical_id_to_slice(cpi)		((cpi) & 3)

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



/*
 * cpuid_to_fsb_slot  - convert a cpuid to the fsb slot number that it is in.
 *   (there are 2 cpus per FSB. This function returns 0 or 1)
 */
static __inline__ int
cpuid_to_fsb_slot(int cpuid)
{
	return cpu_physical_id_to_fsb_slot(cpu_physical_id(cpuid));
}


/*
 * cpuid_to_synergy  - convert a cpuid to the synergy that it resides on
 *   (there are 2 synergies per node. Function returns 0 or 1 to
 *    specify which synergy the cpu is on)
 */
static __inline__ int
cpuid_to_synergy(int cpuid)
{
	return cpu_physical_id_to_synergy(cpu_physical_id(cpuid));
}


/*
 * cpuid_to_slice  - convert a cpuid to the slice that it resides on
 *  There are 4 cpus per node. This function returns 0 .. 3)
 */
static __inline__ int
cpuid_to_slice(int cpuid)
{
	return cpu_physical_id_to_slice(cpu_physical_id(cpuid));
}


/*
 * cpuid_to_nasid  - convert a cpuid to the NASID that it resides on
 */
static __inline__ int
cpuid_to_nasid(int cpuid)
{
	return cpu_physical_id_to_nasid(cpu_physical_id(cpuid));
}


/*
 * cpuid_to_cnodeid  - convert a cpuid to the cnode that it resides on
 */
static __inline__ int
cpuid_to_cnodeid(int cpuid)
{
	return nasid_map[cpuid_to_nasid(cpuid)];
}

/*
 * cnodeid_to_nasid - convert a cnodeid to a NASID
 */
static __inline__ int
cnodeid_to_nasid(int cnodeid)
{
	if (nasid_map[cnodeid_map[cnodeid]] != cnodeid)
		panic("cnodeid_to_nasid, cnode = %d", cnodeid);
	return cnodeid_map[cnodeid];
}

/*
 * nasid_to_cnodeid - convert a NASID to a cnodeid
 */
static __inline__ int
nasid_to_cnodeid(int nasid)
{
	if (cnodeid_map[nasid_map[nasid]] != nasid)
		panic("nasid_to_cnodeid");
	return nasid_map[nasid];
}


/*
 * cnode_slice_to_cpuid - convert a codeid & slice to a cpuid
 */
static __inline__ int
cnode_slice_to_cpuid(int cnodeid, int slice) {
	return(id_eid_to_cpuid(cnodeid_to_nasid(cnodeid),slice));
}

/*
 * cpuid_to_subnode - convert a cpuid to the subnode it resides on.
 *   slice 0 & 1 are on subnode 0
 *   slice 2 & 3 are on subnode 1.
 */
static __inline__ int
cpuid_to_subnode(int cpuid) {
	int ret = cpuid_to_slice(cpuid);
	if (ret < 2) return 0;
	else return 1;
}

/*
 * cpuid_to_localslice - convert a cpuid to a local slice
 *    slice 0 & 2 are local slice 0
 *    slice 1 & 3 are local slice 1
 */
static __inline__ int
cpuid_to_localslice(int cpuid) {
	return(cpuid_to_slice(cpuid) & 1);
}

static __inline__ int
cnodeid_to_cpuid(int cnode) {
	int cpu;

	for (cpu = 0; cpu < smp_num_cpus; cpu++) {
		if (cpuid_to_cnodeid(cpu) == cnode) {
			break;
		}
	}
	if (cpu == smp_num_cpus) cpu = -1;
	return cpu;
}


#endif /* _ASM_IA64_SN_SN_CPUID_H */
