/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Alan Mayer
 */


#include <linux/types.h>
#include <linux/slab.h>
#include <asm/smp.h>
#include <asm/sn/sgi.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/nodemask.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/synergy.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/xtalk/xtalk.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/intr.h>

extern void hubni_eint_init(cnodeid_t cnode);
extern void hubii_eint_init(cnodeid_t cnode);
extern void hubii_eint_handler (int irq, void *arg, struct pt_regs *ep);
extern void snia_error_intr_handler(int irq, void *devid, struct pt_regs *pt_regs);

extern int maxcpus;

#define HUB_ERROR_PERIOD        (120 * HZ)      /* 2 minutes */


void
hub_error_clear(nasid_t nasid)
{
	int i;
	hubreg_t idsr;
	int sn;

	for(sn=0; sn<NUM_SUBNODES; sn++) {
		REMOTE_HUB_PI_S(nasid, sn, PI_ERR_INT_PEND, -1);
		REMOTE_HUB_PI_S(nasid, sn, PI_ERR_STATUS0_A_CLR, -1);
		REMOTE_HUB_PI_S(nasid, sn, PI_ERR_STATUS0_B_CLR, -1);
		REMOTE_HUB_PI_S(nasid, sn, PI_SPURIOUS_HDR_0, 0);
		REMOTE_HUB_PI_S(nasid, sn, PI_SPURIOUS_HDR_1, 0);
	}

	REMOTE_HUB_L(nasid, MD_DIR_ERROR_CLR);
	REMOTE_HUB_L(nasid, MD_MEM_ERROR_CLR);
	REMOTE_HUB_L(nasid, MD_MISC1_ERROR_CLR);
	REMOTE_HUB_L(nasid, MD_PROTOCOL_ERR_CLR);

    /*
     * Make sure spurious write response errors are cleared
     * (values are from hub_set_prb())
     */
    for (i = 0; i <= HUB_WIDGET_ID_MAX - HUB_WIDGET_ID_MIN + 1; i++) {
        iprb_t prb;

	prb.iprb_regval = REMOTE_HUB_L(nasid, IIO_IOPRB_0 + (i * sizeof(hubreg_t)));

        /* Clear out some fields */
        prb.iprb_ovflow = 1;
        prb.iprb_bnakctr = 0;
        prb.iprb_anakctr = 0;

	/*
	 * PIO reads in fire-and-forget mode on bedrock 1.0 don't
	 * frob the credit count properly, making the responses appear
	 * spurious.  So don't use fire-and-forget mode.  Bug 761802.
	 */
        prb.iprb_ff = 0;        /* disable fire-and-forget mode by default */

        prb.iprb_xtalkctr = 3;  /* approx. PIO credits for the widget */

        REMOTE_HUB_S(nasid, IIO_IOPRB_0 + (i * sizeof(hubreg_t)), prb.iprb_regval);
    }

    REMOTE_HUB_S(nasid, IIO_IO_ERR_CLR, -1);
    idsr = REMOTE_HUB_L(nasid, IIO_IIDSR);
    REMOTE_HUB_S(nasid, IIO_IIDSR, (idsr & ~(IIO_IIDSR_SENT_MASK)));

    REMOTE_HUB_L(nasid, NI_PORT_ERROR_CLEAR);
    /* No need to clear NI_PORT_HEADER regs; they are continually overwritten*/

    REMOTE_HUB_S(nasid, LB_ERROR_MASK_CLR, -1);
    REMOTE_HUB_S(nasid, LB_ERROR_HDR1, 0);

    /* Clear XB error regs, in order */
    for (i = 0;
         i <= XB_FIRST_ERROR_CLEAR - XB_POQ0_ERROR_CLEAR;
         i += sizeof(hubreg_t)) {
        REMOTE_HUB_S(nasid, XB_POQ0_ERROR_CLEAR + i, 0);
    }
}


/*
 * Function	: hub_error_init
 * Purpose	: initialize the error handling requirements for a given hub.
 * Parameters	: cnode, the compact nodeid.
 * Assumptions	: Called only once per hub, either by a local cpu. Or by a 
 *			remote cpu, when this hub is headless.(cpuless)
 * Returns	: None
 */

void
hub_error_init(cnodeid_t cnode)
{
	nasid_t nasid;

    nasid = cnodeid_to_nasid(cnode);
    hub_error_clear(nasid);

#ifdef ajm
    if (cnode == 0) {
	/*
	 * Allocate log for storing the node specific error info
	 */
	for (i = 0; i < numnodes; i++) {
	    kl_error_log[i]  = kmem_zalloc_node(sizeof(sn0_error_log_t), 
						KM_NOSLEEP, i);
	    hub_err_count[i] = kmem_zalloc_node(sizeof(hub_errcnt_t),
						VM_DIRECT | KM_NOSLEEP, i);
	    ASSERT_ALWAYS(kl_error_log[i] && hub_err_count[i]);
	}
    }

    /*
     * Assumption: There will be only one cpu who will initialize
     * a hub. we need to setup the ii and each pi error interrupts.
     * The SN1 hub (bedrock) has two PI, one for up to two processors.
     */

    if (cpuid_to_cnodeid(smp_processor_id()) == cnode) { 
	int generic_intr_mask = PI_ERR_GENERIC; /* These interrupts are sent to only 1 CPU per NODE */

	ASSERT_ALWAYS(kl_error_log[cnode]);
	ASSERT_ALWAYS(hub_err_count[cnode]);
	MD_ERR_LOG_INIT(kl_error_log[cnode]);

	/* One for each CPU */
	recover_error_init(RECOVER_ERROR_TABLE(cnode, 0));
	recover_error_init(RECOVER_ERROR_TABLE(cnode, 1));
	recover_error_init(RECOVER_ERROR_TABLE(cnode, 2));
	recover_error_init(RECOVER_ERROR_TABLE(cnode, 3));

	/*
	 * Setup error intr masks.
	 */
	for(sn=0; sn<NUM_SUBNODES; sn++) {
		int cpuA_present = REMOTE_HUB_PI_L(nasid, sn, PI_CPU_ENABLE_A);
		int cpuB_present = REMOTE_HUB_PI_L(nasid, sn, PI_CPU_ENABLE_B);

		if (cpuA_present) {
			if (cpuB_present) {		/* A && B */
	    			REMOTE_HUB_PI_S(nasid, sn, PI_ERR_INT_MASK_A,
					(PI_FATAL_ERR_CPU_B | PI_MISC_ERR_CPU_A|generic_intr_mask));
	    			REMOTE_HUB_PI_S(nasid, sn, PI_ERR_INT_MASK_B,
					(PI_FATAL_ERR_CPU_A | PI_MISC_ERR_CPU_B));

			} else {			/* A && !B */
	    			REMOTE_HUB_PI_S(nasid, sn, PI_ERR_INT_MASK_A,
					(PI_FATAL_ERR_CPU_A | PI_MISC_ERR_CPU_A|generic_intr_mask));
			}
			generic_intr_mask = 0;
		} else {
			if (cpuB_present) {		/* !A && B */
	    			REMOTE_HUB_PI_S(nasid, sn, PI_ERR_INT_MASK_B,
					(PI_FATAL_ERR_CPU_B | PI_MISC_ERR_CPU_B|generic_intr_mask));
				generic_intr_mask = 0;

			} else {			/* !A && !B */
				/* nothing to set up */
			}
		}
	}

	/*
	 * Turn off UNCAC_UNCORR interrupt in the masks. Anyone interested
	 * in these errors will peek at the int pend register to see if its
	 * set.
	 */ 
	for(sn=0; sn<NUM_SUBNODES; sn++) {
		misc = REMOTE_HUB_PI_L(nasid, sn, PI_ERR_INT_MASK_A);
		REMOTE_HUB_PI_S(nasid, sn, PI_ERR_INT_MASK_A, (misc & ~PI_ERR_UNCAC_UNCORR_A));
		misc = REMOTE_HUB_PI_L(nasid, sn, PI_ERR_INT_MASK_B);
		REMOTE_HUB_PI_S(nasid, sn, PI_ERR_INT_MASK_B, (misc & ~PI_ERR_UNCAC_UNCORR_B));
	}

	/*
	 * enable all error indicators to turn on, in case of errors.
	 *
	 * This is not good on single cpu node boards.
	 **** LOCAL_HUB_S(PI_SYSAD_ERRCHK_EN, PI_SYSAD_CHECK_ALL);
	 */
	for(sn=0; sn<NUM_SUBNODES; sn++) {
		REMOTE_HUB_PI_S(nasid, sn, PI_ERR_STATUS1_A_CLR, 0);
		REMOTE_HUB_PI_S(nasid, sn, PI_ERR_STATUS1_B_CLR, 0);
	}

	/* Set up stack for each present processor */
	for(sn=0; sn<NUM_SUBNODES; sn++) {
		if (REMOTE_HUB_PI_L(nasid, sn, PI_CPU_PRESENT_A)) {
	    	SN0_ERROR_LOG(cnode)->el_spool_cur_addr[0] =
			SN0_ERROR_LOG(cnode)->el_spool_last_addr[0] =
		    	REMOTE_HUB_PI_L(nasid, sn, PI_ERR_STACK_ADDR_A);
		}
	    
		if (REMOTE_HUB_PI_L(nasid, sn, PI_CPU_PRESENT_B)) {
	    	SN0_ERROR_LOG(cnode)->el_spool_cur_addr[1] =
			SN0_ERROR_LOG(cnode)->el_spool_last_addr[1] =
		    	REMOTE_HUB_PI_L(nasid, sn, PI_ERR_STACK_ADDR_B);
		}
	}


	PI_SPOOL_SIZE_BYTES = 
	    ERR_STACK_SIZE_BYTES(REMOTE_HUB_L(nasid, PI_ERR_STACK_SIZE));

#ifdef BRINGUP
/* BRINGUP: The following code looks like a check to make sure
the prom set up the error spool correctly for 2 processors.  I
don't think it is needed.  */
	for(sn=0; sn<NUM_SUBNODES; sn++) {
		if (REMOTE_HUB_PI_L(nasid, sn, PI_CPU_PRESENT_B)) {
			__psunsigned_t addr_a = REMOTE_HUB_PI_L(nasid, sn, PI_ERR_STACK_ADDR_A);
			__psunsigned_t addr_b = REMOTE_HUB_PI_L(nasid, sn, PI_ERR_STACK_ADDR_B);
			if ((addr_a & ~0xff) == (addr_b & ~0xff)) {
			    REMOTE_HUB_PI_S(nasid, sn, PI_ERR_STACK_ADDR_B, 	
					addr_b + PI_SPOOL_SIZE_BYTES);
	
			    SN0_ERROR_LOG(cnode)->el_spool_cur_addr[1] =
				SN0_ERROR_LOG(cnode)->el_spool_last_addr[1] =
				    REMOTE_HUB_PI_L(nasid, sn, PI_ERR_STACK_ADDR_B);
	
		    }
		}
	}
#endif /* BRINGUP */

	/* programming our own hub. Enable error_int_pend intr.
	 * If both present, CPU A takes CPU b's error interrupts and any
	 * generic ones. CPU B takes CPU A error ints.
	 */
	if (cause_intr_connect (SRB_ERR_IDX,
				(intr_func_t)(hubpi_eint_handler),
				SR_ALL_MASK|SR_IE)) {
	    cmn_err(ERR_WARN, 
		    "hub_error_init: cause_intr_connect failed on %d", cnode);
	}
    }
    else {
	/* programming remote hub. The only valid reason that this
	 * is called will be on headless hubs. No interrupts 
	 */
	for(sn=0; sn<NUM_SUBNODES; sn++) {
		REMOTE_HUB_PI_S(nasid, sn, PI_ERR_INT_MASK_A, 0); /* not necessary */
		REMOTE_HUB_PI_S(nasid, sn, PI_ERR_INT_MASK_B, 0); /* not necessary */
	}
    }
#endif /* ajm */
    /*
     * Now setup the hub ii and ni error interrupt handler.
     */

    hubii_eint_init(cnode);
    hubni_eint_init(cnode);

#ifdef ajm
    /*** XXX FIXME XXX resolve the following***/
    /* INT_PEND1 bits set up for one hub only:
     *	SHUTDOWN_INTR
     *	MD_COR_ERR_INTR
     *  COR_ERR_INTR_A and COR_ERR_INTR_B should be sent to the
     *  appropriate CPU only.
     */

    if (cnode == 0) {
	    error_consistency_check.eps_state = 0;
	    error_consistency_check.eps_cpuid = -1;
	    spinlock_init(&error_consistency_check.eps_lock, "error_dump_lock");
    }
#endif

    nodepda->huberror_ticks = HUB_ERROR_PERIOD;
    return;
}

/*
 * Function	: hubii_eint_init
 * Parameters	: cnode
 * Purpose	: to initialize the hub iio error interrupt.
 * Assumptions	: Called once per hub, by the cpu which will ultimately
 *			handle this interrupt.
 * Returns	: None.
 */


void
hubii_eint_init(cnodeid_t cnode)
{
    int			bit, rv;
    ii_iidsr_u_t    	hubio_eint;
    hubinfo_t		hinfo; 
    cpuid_t		intr_cpu;
    devfs_handle_t 	hub_v;
    ii_ilcsr_u_t	ilcsr;

    hub_v = (devfs_handle_t)cnodeid_to_vertex(cnode);
    ASSERT_ALWAYS(hub_v);
    hubinfo_get(hub_v, &hinfo);

    ASSERT(hinfo);
    ASSERT(hinfo->h_cnodeid == cnode);

    ilcsr.ii_ilcsr_regval = REMOTE_HUB_L(hinfo->h_nasid, IIO_ILCSR);

    if ((ilcsr.ii_ilcsr_fld_s.i_llp_stat & 0x2) == 0) {
	/* 
	 * HUB II link is not up. 
	 * Just disable LLP, and don't connect any interrupts.
	 */
	ilcsr.ii_ilcsr_fld_s.i_llp_en = 0;
	REMOTE_HUB_S(hinfo->h_nasid, IIO_ILCSR, ilcsr.ii_ilcsr_regval);
	return;
    }
    /* Select a possible interrupt target where there is a free interrupt
     * bit and also reserve the interrupt bit for this IO error interrupt
     */
    intr_cpu = intr_heuristic(hub_v,0,INTRCONNECT_ANYBIT,II_ERRORINT,hub_v,
			      "HUB IO error interrupt",&bit);
    if (intr_cpu == CPU_NONE) {
	printk("hubii_eint_init: intr_reserve_level failed, cnode %d", cnode);
	return;
    }
	
    rv = intr_connect_level(intr_cpu, bit, 0,(intr_func_t)(NULL),
			    (void *)(long)hub_v, NULL);
    synergy_intr_connect(bit, intr_cpu);
    request_irq(bit_pos_to_irq(bit) + (intr_cpu << 8), hubii_eint_handler, 0, NULL, (void *)hub_v);
    ASSERT_ALWAYS(rv >= 0);
    hubio_eint.ii_iidsr_regval = 0;
    hubio_eint.ii_iidsr_fld_s.i_enable = 1;
    hubio_eint.ii_iidsr_fld_s.i_level = bit;/* Take the least significant bits*/
    hubio_eint.ii_iidsr_fld_s.i_node = COMPACT_TO_NASID_NODEID(cnode);
    hubio_eint.ii_iidsr_fld_s.i_pi_id = cpuid_to_subnode(intr_cpu);
    REMOTE_HUB_S(hinfo->h_nasid, IIO_IIDSR, hubio_eint.ii_iidsr_regval);

}

void
hubni_eint_init(cnodeid_t cnode)
{
    int intr_bit;
    cpuid_t targ;


    if ((targ = cnodeid_to_cpuid(cnode)) == CPU_NONE)
	return;

	/* The prom chooses which cpu gets these interrupts, but we
	*  don't know which one it chose.  We will register all of the 
	*  cpus to be sure.  This only costs us an irqaction per cpu.
	*/
    for (; targ < CPUS_PER_NODE; targ++) {
	if (!cpu_enabled(targ) ) continue;
	/* connect the INTEND1 bits. */
	for (intr_bit = XB_ERROR; intr_bit <= MSC_PANIC_INTR; intr_bit++) {
		intr_connect_level(targ, intr_bit, II_ERRORINT, NULL, NULL, NULL);
	}
	request_irq(SGI_HUB_ERROR_IRQ + (targ << 8), snia_error_intr_handler, 0, NULL, NULL);
	/* synergy masks are initialized in the prom to enable all interrupts. */
	/* We'll just leave them that way, here, for these interrupts. */
    }
}


/*ARGSUSED*/
void
hubii_eint_handler (int irq, void *arg, struct pt_regs *ep)
{
    devfs_handle_t	hub_v;
    hubinfo_t		hinfo; 
    ii_wstat_u_t	wstat;
    hubreg_t		idsr;

	panic("Hubii interrupt\n");
#ifdef ajm
    /*
     * If the NI has a problem, everyone has a problem.  We shouldn't
     * even attempt to handle other errors when an NI error is present.
     */
    if (check_ni_errors()) {
	hubni_error_handler("II interrupt", 1);
	/* NOTREACHED */
    }

    /* two levels of casting avoids compiler warning.!! */
    hub_v = (devfs_handle_t)(long)(arg); 
    ASSERT(hub_v);

    hubinfo_get(hub_v, &hinfo);
    
    /* 
     * Identify the reason for error. 
     */
    wstat.ii_wstat_regval = REMOTE_HUB_L(hinfo->h_nasid, IIO_WSTAT);

    if (wstat.ii_wstat_fld_s.w_crazy) {
	char	*reason;
	/*
	 * We can do a couple of things here. 
	 * Look at the fields TX_MX_RTY/XT_TAIL_TO/XT_CRD_TO to check
	 * which of these caused the CRAZY bit to be set. 
	 * You may be able to check if the Link is up really.
	 */
	if (wstat.ii_wstat_fld_s.w_tx_mx_rty)
		reason = "Micro Packet Retry Timeout";
	else if (wstat.ii_wstat_fld_s.w_xt_tail_to)
		reason = "Crosstalk Tail Timeout";
	else if (wstat.ii_wstat_fld_s.w_xt_crd_to)
		reason = "Crosstalk Credit Timeout";
	else {
		hubreg_t	hubii_imem;
		/*
		 * Check if widget 0 has been marked as shutdown, or
		 * if BTE 0/1 has been marked.
		 */
		hubii_imem = REMOTE_HUB_L(hinfo->h_nasid, IIO_IMEM);
		if (hubii_imem & IIO_IMEM_W0ESD)
			reason = "Hub Widget 0 has been Shutdown";
		else if (hubii_imem & IIO_IMEM_B0ESD)
			reason = "BTE 0 has been shutdown";
		else if (hubii_imem & IIO_IMEM_B1ESD)
			reason = "BTE 1 has been shutdown";
		else	reason = "Unknown";
	
	}
	/*
	 * Note: we may never be able to print this, if the II talking
	 * to Xbow which hosts the console is dead. 
	 */
	printk("Hub %d to Xtalk Link failed (II_ECRAZY) Reason: %s", 
		hinfo->h_cnodeid, reason);
    }

    /* 
     * It's a toss as to which one among PRB/CRB to check first. 
     * Current decision is based on the severity of the errors. 
     * IO CRB errors tend to be more severe than PRB errors.
     *
     * It is possible for BTE errors to have been handled already, so we
     * may not see any errors handled here. 
     */
    (void)hubiio_crb_error_handler(hub_v, hinfo);
    (void)hubiio_prb_error_handler(hub_v, hinfo);
    /*
     * If we reach here, it indicates crb/prb handlers successfully
     * handled the error. So, re-enable II to send more interrupt
     * and return.
     */
    REMOTE_HUB_S(hinfo->h_nasid, IIO_IECLR, 0xffffff);
    idsr = REMOTE_HUB_L(hinfo->h_nasid, IIO_IIDSR) & ~IIO_IIDSR_SENT_MASK;
    REMOTE_HUB_S(hinfo->h_nasid, IIO_IIDSR, idsr);
#endif /* ajm */
}
