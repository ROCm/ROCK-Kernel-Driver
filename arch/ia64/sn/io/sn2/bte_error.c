/* $Id$
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

#ifdef BTE_ERROR
// This routine is not called.  Yet.  It may be someday.  It probably
// *should* be someday.  Until then, ifdef it out.
bte_result_t
bte_error_handler(bte_handle_t *bh)
/*
 * Function: 	bte_error_handler
 * Purpose:	Process a BTE error after a transfer has failed.
 * Parameters:	bh - bte handle of bte that failed.
 * Returns:	The BTE error type.
 * Notes:
 */
{
    devfs_handle_t	hub_v;
    hubinfo_t		hinfo;
    int			il;
    hubreg_t		iidsr, imem, ieclr;
    hubreg_t		bte_status;

    bh->bh_bte->bte_error_count++;

    /* 
     * Process any CRB logs - we know that the bte_context contains
     * the BTE completion status, but to avoid a race with error
     * processing, we force a call to pick up any CRB errors pending. 
     * After this call, we know that we have any CRB errors related to 
     * this BTE transfer in the context.
     */
    hub_v = cnodeid_to_vertex(bh->bh_bte->bte_cnode);
    hubinfo_get(hub_v, &hinfo);
    (void)hubiio_crb_error_handler(hub_v, hinfo);

    /* Be sure BTE is stopped */

    (void)BTE_LOAD(bh->bh_bte->bte_base, BTEOFF_CTRL);

    /*	
     * Now clear up the rest of the error - be sure to hold crblock 
     * to avoid race with other cpu on this node.
     */
    imem = REMOTE_HUB_L(hinfo->h_nasid, IIO_IMEM);
    ieclr = REMOTE_HUB_L(hinfo->h_nasid, IIO_IECLR);
    if (bh->bh_bte->bte_num == 0) {
	imem |= IIO_IMEM_W0ESD | IIO_IMEM_B0ESD;
	ieclr|= IECLR_BTE0;
    } else {
	imem |= IIO_IMEM_W0ESD | IIO_IMEM_B1ESD;
	ieclr|= IECLR_BTE1;
    }

    REMOTE_HUB_S(hinfo->h_nasid, IIO_IMEM, imem);
    REMOTE_HUB_S(hinfo->h_nasid, IIO_IECLR, ieclr);

    iidsr  = REMOTE_HUB_L(hinfo->h_nasid, IIO_IIDSR);
    iidsr &= ~IIO_IIDSR_SENT_MASK;
    iidsr |= IIO_IIDSR_ENB_MASK;
    REMOTE_HUB_S(hinfo->h_nasid, IIO_IIDSR, iidsr);
    mutex_spinunlock(&hinfo->h_crblock, il);

    bte_status = BTE_LOAD(bh->bh_bte->bte_base, BTEOFF_STAT);
    BTE_STORE(bh->bh_bte->bte_base, BTEOFF_STAT, bte_status & ~IBLS_BUSY);
    ASSERT(!BTE_IS_BUSY(BTE_LOAD(bh->bh_bte->bte_base, BTEOFF_STAT)));

    switch(bh->bh_error) {
    case IIO_ICRB_ECODE_PERR:
	return(BTEFAIL_POISON);
    case IIO_ICRB_ECODE_WERR:
	return(BTEFAIL_PROT);
    case IIO_ICRB_ECODE_AERR:
	return(BTEFAIL_ACCESS);
    case IIO_ICRB_ECODE_TOUT:
	return(BTEFAIL_TOUT);
    case IIO_ICRB_ECODE_XTERR:
	return(BTEFAIL_ERROR);
    case IIO_ICRB_ECODE_DERR:
	return(BTEFAIL_DIR);
    case IIO_ICRB_ECODE_PWERR:
    case IIO_ICRB_ECODE_PRERR:
	/* NO BREAK */
    default:
	printk("BTE failure (%d) unexpected\n", 
		bh->bh_error);
	return(BTEFAIL_ERROR);
    }
}
#endif // BTE_ERROR

void
bte_crb_error_handler(devfs_handle_t hub_v, int btenum, 
		      int crbnum, ioerror_t *ioe)
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

	hubinfo_get(hub_v, &hinfo);


	n = hinfo->h_nasid;
	
	/* Step 1 */
	crba.ii_icrb0_a_regval = REMOTE_HUB_L(n, IIO_ICRB_A(crbnum));
	crbb.ii_icrb0_b_regval = REMOTE_HUB_L(n, IIO_ICRB_B(crbnum));


	/* Zero error and error code to prevent error_dump complaining
	 * about these CRBs. 
	 */
	crbb.b_error=0;
	crbb.b_ecode=0;

	/* Step 2 */
	REMOTE_HUB_S(n, IIO_ICRB_A(crbnum), crba.ii_icrb0_a_regval);
	/* Step 3 */
	REMOTE_HUB_S(n, IIO_ICCR, 
		     IIO_ICCR_PENDING | IIO_ICCR_CMD_FLUSH | crbnum);
	while (REMOTE_HUB_L(n, IIO_ICCR) & IIO_ICCR_PENDING)
	    ;
}

