#ifndef _ASM_IA64_SN_SN_SAL_H
#define _ASM_IA64_SN_SN_SAL_H

/*
 * System Abstraction Layer definitions for IA64
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All rights reserved.
 */


#include <asm/sal.h>
#include <asm/sn/sn_cpuid.h>


// SGI Specific Calls
#define  SN_SAL_POD_MODE                           0x02000001
#define  SN_SAL_SYSTEM_RESET                       0x02000002
#define  SN_SAL_PROBE                              0x02000003
#define  SN_SAL_GET_CONSOLE_NASID                  0x02000004
#define	 SN_SAL_GET_KLCONFIG_ADDR		   0x02000005
#define  SN_SAL_LOG_CE				   0x02000006
#define  SN_SAL_REGISTER_CE			   0x02000007


u64 ia64_sn_probe_io_slot(long paddr, long size, void *data_ptr);

/*
 * Returns the master console nasid, if the call fails, return an illegal
 * value.
 */
static inline u64
ia64_sn_get_console_nasid(void)
{
	struct ia64_sal_retval ret_stuff;

	ret_stuff.status = (uint64_t)0;
	ret_stuff.v0 = (uint64_t)0;
	ret_stuff.v1 = (uint64_t)0;
	ret_stuff.v2 = (uint64_t)0;
	SAL_CALL(ret_stuff, SN_SAL_GET_CONSOLE_NASID, 0, 0, 0, 0, 0, 0, 0);

	if (ret_stuff.status < 0)
		return ret_stuff.status;

	/* Master console nasid is in 'v0' */
	return ret_stuff.v0;
}

static inline u64
ia64_sn_get_klconfig_addr(nasid_t nasid)
{
	struct ia64_sal_retval ret_stuff;
	extern u64 klgraph_addr[];
	int cnodeid;

	cnodeid = nasid_to_cnodeid(nasid);
	if (klgraph_addr[cnodeid] == 0) {
		ret_stuff.status = (uint64_t)0;
		ret_stuff.v0 = (uint64_t)0;
		ret_stuff.v1 = (uint64_t)0;
		ret_stuff.v2 = (uint64_t)0;
		SAL_CALL(ret_stuff, SN_SAL_GET_KLCONFIG_ADDR, (u64)nasid, 0, 0, 0, 0, 0, 0);

		/*
	 	* We should panic if a valid cnode nasid does not produce
	 	* a klconfig address.
	 	*/
		if (ret_stuff.status != 0) {
			panic("ia64_sn_get_klconfig_addr: Returned error %lx\n", ret_stuff.status);
		}

		klgraph_addr[cnodeid] = ret_stuff.v0;
	}
	return(klgraph_addr[cnodeid]);

}
#endif /* _ASM_IA64_SN_SN_SAL_H */
