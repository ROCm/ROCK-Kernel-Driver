/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

/*
 * ip37.c
 *	Support for IP35/IP37 machines
 */

#include <linux/types.h>
#include <linux/config.h>

#if defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
#include <asm/sn/sgi.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/sn1/hubdev.h>
#include <asm/sn/pci/bridge.h>     /* for bridge_t */


xwidgetnum_t
hub_widget_id(nasid_t nasid)
{
	hubii_wcr_t	ii_wcr;	/* the control status register */
		
	ii_wcr.wcr_reg_value = REMOTE_HUB_L(nasid,IIO_WCR);

	printk("hub_widget_id: Found Hub Widget ID 0x%x from Register 0x%p\n", ii_wcr.wcr_fields_s.wcr_widget_id, REMOTE_HUB_ADDR(nasid, IIO_WCR));

	printk("hub_widget_id: Found Hub Widget 0x%lx wcr_reg_value 0x%lx\n", REMOTE_HUB_L(nasid,IIO_WCR), ii_wcr.wcr_reg_value);

	return ii_wcr.wcr_fields_s.wcr_widget_id;
}

/*
 * get_nasid() returns the physical node id number of the caller.
 */
nasid_t
get_nasid(void)
{
	return (nasid_t)((LOCAL_HUB_L(LB_REV_ID) & LRI_NODEID_MASK) >> LRI_NODEID_SHFT);
}

int
get_slice(void)
{
	return LOCAL_HUB_L(PI_CPU_NUM);
}

int
is_fine_dirmode(void)
{
	return (((LOCAL_HUB_L(LB_REV_ID) & LRI_SYSTEM_SIZE_MASK)
		>> LRI_SYSTEM_SIZE_SHFT) == SYSTEM_SIZE_SMALL);

}

hubreg_t
get_hub_chiprev(nasid_t nasid)
{

	printk("get_hub_chiprev: Hub Chip Rev 0x%lx\n",
		(REMOTE_HUB_L(nasid, LB_REV_ID) & LRI_REV_MASK) >> LRI_REV_SHFT);
	return ((REMOTE_HUB_L(nasid, LB_REV_ID) & LRI_REV_MASK)
		                                         >> LRI_REV_SHFT);
}

int
verify_snchip_rev(void)
{
	int hub_chip_rev;
	int i;
	static int min_hub_rev = 0;
	nasid_t nasid;
	static int first_time = 1;
	extern int maxnodes;

        
	if (first_time) {
	    for (i = 0; i < maxnodes; i++) {	
		nasid = COMPACT_TO_NASID_NODEID(i);
		hub_chip_rev = get_hub_chiprev(nasid);

		if ((hub_chip_rev < min_hub_rev) || (i == 0))
		    min_hub_rev = hub_chip_rev;
	    }

	
	    first_time = 0;
	}

	return min_hub_rev;
	
}

#ifdef SN1_USE_POISON_BITS
int
hub_bte_poison_ok(void)
{
	/*
	 * For now, assume poisoning is ok. If it turns out there are chip
	 * bugs that prevent its use in early revs, there is some neat code
	 * to steal from the IP27 equivalent of this code.
	 */

#ifdef BRINGUP	/* temp disable BTE poisoning - might be sw bugs in this area */
	return 0;
#else
	return 1;
#endif
}
#endif /* SN1_USE_POISON_BITS */
                

void
ni_reset_port(void)
{
	LOCAL_HUB_S(NI_RESET_ENABLE, NRE_RESETOK);
	LOCAL_HUB_S(NI_PORT_RESET, NPR_PORTRESET | NPR_LOCALRESET);
}

#endif	/* CONFIG_SGI_IP35 || CONFIG_IA64_SGI_SN1 */
