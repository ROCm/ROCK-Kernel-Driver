/* $Id: bte_error.c,v 1.1 2002/02/28 17:31:25 marcelo Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000,2002 Silicon Graphics, Inc. All rights reserved.
 */


#include <linux/types.h>
#include <linux/slab.h>
#include <asm/smp.h>
#include <asm/sn/sgi.h>
#include <asm/sn/io.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/xtalk/xtalk.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/intr.h>
#include <asm/sn/ioerror.h>
#include <asm/sn/sn2/shubio.h>
#include <asm/sn/bte.h>

/************************************************************************
 *									*
 * 			 BTE ERROR RECOVERY				*
 *									*
 * Given a BTE error, the node causing the error must do the following: *
 *    a) Clear all crbs relating to that BTE				*
 *		1) Read CRBA value for crb in question			*
 *		2) Mark CRB as VALID, store local physical 		*
 *		   address known to be good in the address field	*
 *		   (bte_notification_targ is a known good local		*
 *		    address).						*
 *		3) Write CRBA						*
 *		4) Using ICCR, FLUSH the CRB, and wait for it to 	*
 *		   complete.						*
 *		... BTE BUSY bit should now be clear (or at least 	*
 *		    should be after ALL CRBs associated with the 	*
 *		    transfer are complete.				*
 *									*
 *    b) Re-enable BTE							*
 *		1) Write IMEM with BTE Enable + XXX bits
 *		2) Write IECLR with BTE clear bits
 *		3) Clear IIDSR INT_SENT bits.
 *									*
 ************************************************************************/

/* 
 * >>> bte_crb_error_handler needs to be broken into two parts.  The
 * first should cleanup the CRB.  The second should wait until all bte
 * related CRB's are complete and then do the error reset.
 */
void
bte_crb_error_handler(devfs_handle_t hub_v, int btenum, 
		      int crbnum, ioerror_t *ioe, int bteop)
/*
 * Function: 	bte_crb_error_handler
 * Purpose:	Process a CRB for a specific HUB/BTE
 * Parameters:	hub_v	- vertex of hub in HW graph
 *		btenum	- bte number on hub (0 == a, 1 == b)
 *		crbnum	- crb number being processed
 * Notes: 
 *	This routine assumes serialization at a higher level. A CRB 
 *	should not be processed more than once. The error recovery 
 *	follows the following sequence - if you change this, be real
 *	sure about what you are doing. 
 *
 */
{
        hubinfo_t	hinfo;
	icrba_t		crba; 
	icrbb_t		crbb; 
	nasid_t		n;
	hubreg_t	iidsr, imem, ieclr;

	hubinfo_get(hub_v, &hinfo);


	n = hinfo->h_nasid;
	

	/*
	 * The following 10 lines (or so) are adapted from IRIXs
	 * bte_crb_error function.  No clear documentation tells
	 * why the crb needs to complete normally in order for
	 * the BTE to resume normal operations.  This first step
	 * appears vital!
	 */

	/*
	 * Zero error and error code to prevent error_dump complaining
	 * about these CRBs. Copy the CRB to the notification line.
	 * The crb address is in shub format (physical address shifted
	 * right by cacheline size).
	 */
	crbb.ii_icrb0_b_regval = REMOTE_HUB_L(n, IIO_ICRB_B(crbnum));
	crbb.b_error=0;
	crbb.b_ecode=0;
	REMOTE_HUB_S(n, IIO_ICRB_B(crbnum), crbb.ii_icrb0_b_regval);

	crba.ii_icrb0_a_regval = REMOTE_HUB_L(n, IIO_ICRB_A(crbnum));
	crba.a_addr = TO_PHYS((u64)&nodepda->bte_if[btenum].notify) >> 3;
	crba.a_valid = 1;
	REMOTE_HUB_S(n, IIO_ICRB_A(crbnum), crba.ii_icrb0_a_regval);

	REMOTE_HUB_S(n, IIO_ICCR, 
		     IIO_ICCR_PENDING | IIO_ICCR_CMD_FLUSH | crbnum);

	while (REMOTE_HUB_L(n, IIO_ICCR) & IIO_ICCR_PENDING)
	    ;


	/* Terminate the BTE. */
	/* >>> The other bte transfer will need to be restarted. */
	HUB_L((shubreg_t *)((nodepda->bte_if[btenum].bte_base_addr +
		       IIO_IBCT0 - IIO_IBLS0)));

	imem = REMOTE_HUB_L(n, IIO_IMEM);
	ieclr = REMOTE_HUB_L(n, IIO_IECLR);
	if (btenum == 0) {
		imem |= IIO_IMEM_W0ESD | IIO_IMEM_B0ESD;
		ieclr|= IECLR_BTE0;
	} else {
		imem |= IIO_IMEM_W0ESD | IIO_IMEM_B1ESD;
		ieclr|= IECLR_BTE1;
	}
	REMOTE_HUB_S(n, IIO_IMEM, imem);
	REMOTE_HUB_S(n, IIO_IECLR, ieclr);
		
	iidsr  = REMOTE_HUB_L(n, IIO_IIDSR);
	iidsr &= ~IIO_IIDSR_SENT_MASK;
	iidsr |= IIO_IIDSR_ENB_MASK;
	REMOTE_HUB_S(n, IIO_IIDSR, iidsr);


 	bte_reset_nasid(n);

	*nodepda->bte_if[btenum].most_rcnt_na = IBLS_ERROR;
}

