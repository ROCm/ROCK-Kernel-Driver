/* 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2002 Silicon Graphics, Inc. All rights reserved.
 */

/*
 * ip37.c
 *	Support for IP35/IP37 machines
 */

#include <linux/types.h>

#include <asm/sn/sgi.h>
#include <asm/sn/io.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/pci/bridge.h>     /* for bridge_t */


xwidgetnum_t
hub_widget_id(nasid_t nasid)
{
	hubii_wcr_t	ii_wcr;	/* the control status register */
		
	ii_wcr.wcr_reg_value = REMOTE_HUB_L(nasid,IIO_WCR);

	return ii_wcr.wcr_fields_s.wcr_widget_id;
}

int
is_fine_dirmode(void)
{
	return (((LOCAL_HUB_L(LB_REV_ID) & LRI_SYSTEM_SIZE_MASK)
		>> LRI_SYSTEM_SIZE_SHFT) == SYSTEM_SIZE_SMALL);

}


void
ni_reset_port(void)
{
	LOCAL_HUB_S(NI_RESET_ENABLE, NRE_RESETOK);
	LOCAL_HUB_S(NI_PORT_RESET, NPR_PORTRESET | NPR_LOCALRESET);
}
