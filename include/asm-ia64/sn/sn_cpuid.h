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
#include <asm/sn/mmzone_sn1.h>

/*
 * Functions for converting between cpuids, nodeids and NASIDs.
 * 
 * These are for SGI platforms only.
 *
 */




/*
 * The following assumes the following mappings for LID register values:
 *
 *         LID
 *		31:24 - id   Contains the NASID
 *		23:16 - eid  Contains 0-3 to identify the cpu on the node
 *				bit 17 - synergy number
 *				bit 16 - FSB number 
 *
 * 	   SAPICID
 *		This is the same as 31:24 of LID
 *
 * The macros convert between cpuid & slice/fsb/synergy/nasid/cnodeid.
 * These terms are described below:
 *
 *
 *          -----   -----           -----   -----       CPU
 *          | 0 |   | 1 |           | 2 |   | 3 |       SLICE
 *          -----   -----           -----   -----
 *            |       |               |       |
 *            |       |               |       |
 *          0 |       | 1           0 |       | 1       FSB
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



#define sapicid_to_nasid(sid)		((sid) >> 8)
#define sapicid_to_synergy(sid)		(((sid) >> 1) & 1)
#define sapicid_to_fsb(sid)		((sid) & 1)
#define sapicid_to_slice(sid)		((sid) & 3)

/*
 * NOTE: id & eid refer to Intels definitions of the LID register
 *	(id = NASID, eid = slice)
 * NOTE: on non-MP systems, only cpuid 0 exists
 */
#define id_eid_to_sapicid(id,eid)       (((id)<<8) | (eid))
#define id_eid_to_cpuid(id,eid)         ((NASID_TO_CNODEID(id)<<2) | (eid))


/*
 * The following table/struct is for translating between sapicid and cpuids.
 * It is also used for managing PTC coherency domains.
 */
typedef struct {
	u8	domain;
	u8	reserved;
	u16	sapicid;
} sn_sapicid_info_t;

extern sn_sapicid_info_t	sn_sapicid_info[];	/* indexed by cpuid */



/*
 * cpuid_to_spaicid  - Convert a cpuid to a SAPIC id of the cpu. 
 * The SAPIC id is the same as bits 31:16 of the LID register.
 */
static __inline__ int
cpuid_to_spaicid(int cpuid)
{
#ifdef CONFIG_SMP
	return cpu_physical_id(cpuid);
#else
	return ((ia64_get_lid() >> 16) & 0xffff);
#endif
}


/*
 * cpuid_to_fsb_slot  - convert a cpuid to the fsb slot number that it is in.
 *   (there are 2 cpus per FSB. This function returns 0 or 1)
 */
static __inline__ int
cpuid_to_fsb_slot(int cpuid)
{
	return sapicid_to_fsb(cpuid_to_spaicid(cpuid));
}


/*
 * cpuid_to_synergy  - convert a cpuid to the synergy that it resides on
 *   (there are 2 synergies per node. Function returns 0 or 1 to
 *    specify which synergy the cpu is on)
 */
static __inline__ int
cpuid_to_synergy(int cpuid)
{
	return sapicid_to_synergy(cpuid_to_spaicid(cpuid));
}


/*
 * cpuid_to_slice  - convert a cpuid to the slice that it resides on
 *  There are 4 cpus per node. This function returns 0 .. 3)
 */
static __inline__ int
cpuid_to_slice(int cpuid)
{
	return sapicid_to_slice(cpuid_to_spaicid(cpuid));
}


/*
 * cpuid_to_nasid  - convert a cpuid to the NASID that it resides on
 */
static __inline__ int
cpuid_to_nasid(int cpuid)
{
	return sapicid_to_nasid(cpuid_to_spaicid(cpuid));
}


/*
 * cpuid_to_cnodeid  - convert a cpuid to the cnode that it resides on
 */
static __inline__ int
cpuid_to_cnodeid(int cpuid)
{
	return nasid_map[cpuid_to_nasid(cpuid)];
}

static __inline__ int
cnodeid_to_nasid(int cnodeid)
{
	int i;
	for (i = 0; i < MAXNASIDS; i++) {
		if (nasid_map[i] == cnodeid) {
			return(i);
		}
	}
	return(-1);
}

static __inline__ int
cnode_slice_to_cpuid(int cnodeid, int slice) {
	return(id_eid_to_cpuid(cnodeid_to_nasid(cnodeid),slice));
}

static __inline__ int
cpuid_to_subnode(int cpuid) {
	int ret = cpuid_to_slice(cpuid);
	if (ret < 2) return 0;
	else return 1;
}

static __inline__ int
cpuid_to_localslice(int cpuid) {
	return(cpuid_to_slice(cpuid) & 1);
}


#endif /* _ASM_IA64_SN_SN_CPUID_H */
