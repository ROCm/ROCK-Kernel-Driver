/* $Id: huberror.c,v 1.1 2002/02/28 17:31:25 marcelo Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2002 Silicon Graphics, Inc. All rights reserved.
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

    /*
     * Now setup the hub ii and ni error interrupt handler.
     */

    hubii_eint_init(cnode);
    hubni_eint_init(cnode);

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
    int bit_pos_to_irq(int bit);
    int synergy_intr_connect(int bit, int cpuid);


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
	
    rv = intr_connect_level(intr_cpu, bit, 0, NULL);
    synergy_intr_connect(bit, intr_cpu);
    request_irq(bit_pos_to_irq(bit) + (intr_cpu << 8), hubii_eint_handler, 0, "SN hub error", (void *)hub_v);
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
		intr_connect_level(targ, intr_bit, II_ERRORINT, NULL);
	}
	request_irq(SGI_HUB_ERROR_IRQ + (targ << 8), snia_error_intr_handler, 0, "SN hub error", NULL);
	/* synergy masks are initialized in the prom to enable all interrupts. */
	/* We'll just leave them that way, here, for these interrupts. */
    }
}


/*ARGSUSED*/
void
hubii_eint_handler (int irq, void *arg, struct pt_regs *ep)
{

	panic("Hubii interrupt\n");
}
