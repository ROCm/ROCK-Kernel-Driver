/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001-2003 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <asm/sn/sgi.h>
#include <asm/sn/arch.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/pci/pci_defs.h>
#include <asm/sn/io.h>
#include <asm/sn/sn_private.h>

#ifdef __ia64
inline int
compare_and_swap_ptr(void **location, void *old_ptr, void *new_ptr)
{
	/* FIXME - compare_and_swap_ptr NOT ATOMIC */
	if (*location == old_ptr) {
		*location = new_ptr;
		return 1;
	}
	else
		return 0;
}
#endif

unsigned int		pcibr_intr_bits(pciio_info_t info, pciio_intr_line_t lines, int nslots);
pcibr_intr_t            pcibr_intr_alloc(vertex_hdl_t, device_desc_t, pciio_intr_line_t, vertex_hdl_t);
void                    pcibr_intr_free(pcibr_intr_t);
void              pcibr_setpciint(xtalk_intr_t);
int                     pcibr_intr_connect(pcibr_intr_t, intr_func_t, intr_arg_t);
void                    pcibr_intr_disconnect(pcibr_intr_t);

vertex_hdl_t            pcibr_intr_cpu_get(pcibr_intr_t);

extern pcibr_info_t      pcibr_info_get(vertex_hdl_t);

/* =====================================================================
 *    INTERRUPT MANAGEMENT
 */

unsigned int
pcibr_intr_bits(pciio_info_t info,
		pciio_intr_line_t lines, int nslots)
{
    pciio_slot_t            slot = PCIBR_INFO_SLOT_GET_INT(info);
    unsigned		    bbits = 0;

    /*
     * Currently favored mapping from PCI
     * slot number and INTA/B/C/D to Bridge
     * PCI Interrupt Bit Number:
     *
     *     SLOT     A B C D
     *      0       0 4 0 4
     *      1       1 5 1 5
     *      2       2 6 2 6
     *      3       3 7 3 7
     *      4       4 0 4 0
     *      5       5 1 5 1
     *      6       6 2 6 2
     *      7       7 3 7 3
     */

    if (slot < nslots) {
	if (lines & (PCIIO_INTR_LINE_A| PCIIO_INTR_LINE_C))
	    bbits |= 1 << slot;
	if (lines & (PCIIO_INTR_LINE_B| PCIIO_INTR_LINE_D))
	    bbits |= 1 << (slot ^ 4);
    }
    return bbits;
}


/*
 *	On SN systems there is a race condition between a PIO read response
 *	and DMA's.  In rare cases, the read response may beat the DMA, causing
 *	the driver to think that data in memory is complete and meaningful.
 *	This code eliminates that race.
 *	This routine is called by the PIO read routines after doing the read.
 *	This routine then forces a fake interrupt on another line, which
 *	is logically associated with the slot that the PIO is addressed to.
 *	(see sn_dma_flush_init() )
 *	It then spins while watching the memory location that the interrupt
 *	is targetted to.  When the interrupt response arrives, we are sure
 *	that the DMA has landed in memory and it is safe for the driver
 *	to proceed.
 */

extern struct sn_flush_nasid_entry flush_nasid_list[MAX_NASIDS];

void
sn_dma_flush(unsigned long addr)
{
	nasid_t nasid;
	int wid_num;
	struct sn_flush_device_list *p;
	int i,j;
	int bwin;
	unsigned long flags;

	nasid = NASID_GET(addr);
	wid_num = SWIN_WIDGETNUM(addr);
	bwin = BWIN_WINDOWNUM(addr);

	if (flush_nasid_list[nasid].widget_p == NULL) return;
	if (bwin > 0) {
		unsigned long itte = flush_nasid_list[nasid].iio_itte[bwin];

		wid_num = (itte >> IIO_ITTE_WIDGET_SHIFT) &
				  IIO_ITTE_WIDGET_MASK;
	}
	if (flush_nasid_list[nasid].widget_p == NULL) return;
	if (flush_nasid_list[nasid].widget_p[wid_num] == NULL) return;
	p = &flush_nasid_list[nasid].widget_p[wid_num][0];

	/* find a matching BAR */

	for (i=0; i<DEV_PER_WIDGET;i++) {
		for (j=0; j<PCI_ROM_RESOURCE;j++) {
			if (p->bar_list[j].start == 0) break;
			if (addr >= p->bar_list[j].start && addr <= p->bar_list[j].end) break;
		}
		if (j < PCI_ROM_RESOURCE && p->bar_list[j].start != 0) break;
		p++;
	}

	/* if no matching BAR, return without doing anything. */

	if (i == DEV_PER_WIDGET) return;

	spin_lock_irqsave(&p->flush_lock, flags);

	p->flush_addr = 0;

	/* force an interrupt. */

	*(volatile uint32_t *)(p->force_int_addr) = 1;

	/* wait for the interrupt to come back. */

	while (p->flush_addr != 0x10f);

	/* okay, everything is synched up. */
	spin_unlock_irqrestore(&p->flush_lock, flags);
}

EXPORT_SYMBOL(sn_dma_flush);

/*
 *	There are end cases where a deadlock can occur if interrupt 
 *	processing completes and the Bridge b_int_status bit is still set.
 *
 *	One scenerio is if a second PCI interrupt occurs within 60ns of
 *	the previous interrupt being cleared. In this case the Bridge
 *	does not detect the transition, the Bridge b_int_status bit
 *	remains set, and because no transition was detected no interrupt
 *	packet is sent to the Hub/Heart.
 *
 *	A second scenerio is possible when a b_int_status bit is being
 *	shared by multiple devices:
 *						Device #1 generates interrupt
 *						Bridge b_int_status bit set
 *						Device #2 generates interrupt
 *		interrupt processing begins
 *		  ISR for device #1 runs and
 *			clears interrupt
 *						Device #1 generates interrupt
 *		  ISR for device #2 runs and
 *			clears interrupt
 *						(b_int_status bit still set)
 *		interrupt processing completes
 *		  
 *	Interrupt processing is now complete, but an interrupt is still
 *	outstanding for Device #1. But because there was no transition of
 *	the b_int_status bit, no interrupt packet will be generated and
 *	a deadlock will occur.
 *
 *	To avoid these deadlock situations, this function is used
 *	to check if a specific Bridge b_int_status bit is set, and if so,
 *	cause the setting of the corresponding interrupt bit.
 *
 *	On a XBridge (SN1) and PIC (SN2), we do this by writing the appropriate Bridge Force 
 *	Interrupt register.
 */
void
pcibr_force_interrupt(pcibr_intr_t intr)
{
	unsigned	bit;
	unsigned	bits;
	pcibr_soft_t    pcibr_soft = intr->bi_soft;

	bits = intr->bi_ibits;
	for (bit = 0; bit < 8; bit++) {
		if (bits & (1 << bit)) {

			PCIBR_DEBUG((PCIBR_DEBUG_INTR, pcibr_soft->bs_vhdl,
		    		"pcibr_force_interrupt: bit=0x%x\n", bit));

			pcireg_force_intr_set(pcibr_soft, bit);
		}
	}
}

/*ARGSUSED */
pcibr_intr_t
pcibr_intr_alloc(vertex_hdl_t pconn_vhdl,
		 device_desc_t dev_desc,
		 pciio_intr_line_t lines,
		 vertex_hdl_t owner_dev)
{
    pcibr_info_t            pcibr_info = pcibr_info_get(pconn_vhdl);
    pciio_slot_t            pciio_slot = PCIBR_INFO_SLOT_GET_INT(pcibr_info);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pcibr_info->f_mfast;
    vertex_hdl_t            xconn_vhdl = pcibr_soft->bs_conn;
    int                     is_threaded = 0;

    xtalk_intr_t           *xtalk_intr_p;
    pcibr_intr_t           *pcibr_intr_p;
    pcibr_intr_list_t      *intr_list_p;

    unsigned                pcibr_int_bits;
    unsigned                pcibr_int_bit;
    xtalk_intr_t            xtalk_intr = (xtalk_intr_t)0;
    hub_intr_t		    hub_intr;
    pcibr_intr_t            pcibr_intr;
    pcibr_intr_list_t       intr_entry;
    pcibr_intr_list_t       intr_list;

    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_INTR_ALLOC, pconn_vhdl,
    		"pcibr_intr_alloc: %s%s%s%s%s\n",
		!(lines & 15) ? " No INTs?" : "",
		lines & 1 ? " INTA" : "",
		lines & 2 ? " INTB" : "",
		lines & 4 ? " INTC" : "",
		lines & 8 ? " INTD" : ""));

    pcibr_intr = kmalloc(sizeof (*(pcibr_intr)), GFP_KERNEL);
    if (!pcibr_intr)
	return NULL;
    memset(pcibr_intr, 0, sizeof (*(pcibr_intr)));

    pcibr_intr->bi_dev = pconn_vhdl;
    pcibr_intr->bi_lines = lines;
    pcibr_intr->bi_soft = pcibr_soft;
    pcibr_intr->bi_ibits = 0;		/* bits will be added below */
    pcibr_intr->bi_func = 0;            /* unset until connect */
    pcibr_intr->bi_arg = 0;             /* unset until connect */
    pcibr_intr->bi_flags = is_threaded ? 0 : PCIIO_INTR_NOTHREAD;
    pcibr_intr->bi_mustruncpu = CPU_NONE;
    pcibr_intr->bi_ibuf.ib_in = 0;
    pcibr_intr->bi_ibuf.ib_out = 0;
    spin_lock_init(&pcibr_intr->bi_ibuf.ib_lock);

    pcibr_int_bits = pcibr_soft->bs_intr_bits((pciio_info_t)pcibr_info, 
					lines, PCIBR_NUM_SLOTS(pcibr_soft));

    /*
     * For each PCI interrupt line requested, figure
     * out which Bridge PCI Interrupt Line it maps
     * to, and make sure there are xtalk resources
     * allocated for it.
     */
    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_INTR_ALLOC, pconn_vhdl,
		"pcibr_intr_alloc: pcibr_int_bits: 0x%x\n", pcibr_int_bits));
    for (pcibr_int_bit = 0; pcibr_int_bit < 8; pcibr_int_bit ++) {
	if (pcibr_int_bits & (1 << pcibr_int_bit)) {
	    xtalk_intr_p = &pcibr_soft->bs_intr[pcibr_int_bit].bsi_xtalk_intr;

	    xtalk_intr = *xtalk_intr_p;

	    if (xtalk_intr == NULL) {
		/*
		 * This xtalk_intr_alloc is constrained for two reasons:
		 * 1) Normal interrupts and error interrupts need to be delivered
		 *    through a single xtalk target widget so that there aren't any
		 *    ordering problems with DMA, completion interrupts, and error
		 *    interrupts. (Use of xconn_vhdl forces this.)
		 *
		 * 2) On SN1, addressing constraints on SN1 and Bridge force
		 *    us to use a single PI number for all interrupts from a
		 *    single Bridge. (SN1-specific code forces this).
		 */

		/*
		 * All code dealing with threaded PCI interrupt handlers
		 * is located at the pcibr level. Because of this,
		 * we always want the lower layers (hub/heart_intr_alloc, 
		 * intr_level_connect) to treat us as non-threaded so we
		 * don't set up a duplicate threaded environment. We make
		 * this happen by calling a special xtalk interface.
		 */
		xtalk_intr = xtalk_intr_alloc_nothd(xconn_vhdl, dev_desc, 
			owner_dev);

		PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_INTR_ALLOC, pconn_vhdl,
			    "pcibr_intr_alloc: xtalk_intr=0x%lx\n", xtalk_intr));

		/* both an assert and a runtime check on this:
		 * we need to check in non-DEBUG kernels, and
		 * the ASSERT gets us more information when
		 * we use DEBUG kernels.
		 */
		ASSERT(xtalk_intr != NULL);
		if (xtalk_intr == NULL) {
		    /* it is quite possible that our
		     * xtalk_intr_alloc failed because
		     * someone else got there first,
		     * and we can find their results
		     * in xtalk_intr_p.
		     */
		    if (!*xtalk_intr_p) {
			printk(KERN_ALERT "pcibr_intr_alloc %s: "
				"unable to get xtalk interrupt resources",
				pcibr_soft->bs_name);
			/* yes, we leak resources here. */
			return 0;
		    }
		} else if (compare_and_swap_ptr((void **) xtalk_intr_p, NULL, xtalk_intr)) {
		    /*
		     * now tell the bridge which slot is
		     * using this interrupt line.
		     */
		    pcireg_intr_device_bit_clr(pcibr_soft, 
			    BRIDGE_INT_DEV_MASK(pcibr_int_bit));
		    pcireg_intr_device_bit_set(pcibr_soft, 
			    (pciio_slot << BRIDGE_INT_DEV_SHFT(pcibr_int_bit)));

		    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_INTR_ALLOC, pconn_vhdl,
		    		"bridge intr bit %d clears my wrb\n",
				pcibr_int_bit));
		} else {
		    /* someone else got one allocated first;
		     * free the one we just created, and
		     * retrieve the one they allocated.
		     */
		    xtalk_intr_free(xtalk_intr);
		    xtalk_intr = *xtalk_intr_p;
		}
	    }

	    pcibr_intr->bi_ibits |= 1 << pcibr_int_bit;

	    intr_entry = kmalloc(sizeof (*(intr_entry)), GFP_KERNEL);
	    if ( !intr_entry ) {
		printk(KERN_ALERT "pcibr_intr_alloc %s: "
			"unable to get memory",
			pcibr_soft->bs_name);
		return 0;
	    }
	    memset(intr_entry, 0, sizeof (*(intr_entry)));

	    intr_entry->il_next = NULL;
	    intr_entry->il_intr = pcibr_intr;
	    intr_entry->il_soft = pcibr_soft;
	    intr_entry->il_slot = pciio_slot;
	    intr_list_p = 
		&pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap.iw_list;

	    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_INTR_ALLOC, pconn_vhdl,
			"Bridge bit 0x%x wrap=0x%lx\n", pcibr_int_bit,
			&(pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap)));

	    if (compare_and_swap_ptr((void **) intr_list_p, NULL, intr_entry)) {
		/* we are the first interrupt on this bridge bit.
		 */
		PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_INTR_ALLOC, pconn_vhdl,
			    "INT 0x%x (bridge bit %d) allocated [FIRST]\n",
			    pcibr_int_bits, pcibr_int_bit));
		continue;
	    }
	    intr_list = *intr_list_p;
	    pcibr_intr_p = &intr_list->il_intr;
	    if (compare_and_swap_ptr((void **) pcibr_intr_p, NULL, pcibr_intr)) {
		/* first entry on list was erased,
		 * and we replaced it, so we
		 * don't need our intr_entry.
		 */
		kfree(intr_entry);
		PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_INTR_ALLOC, pconn_vhdl,
			    "INT 0x%x (bridge bit %d) replaces erased first\n",
			    pcibr_int_bits, pcibr_int_bit));
		continue;
	    }
	    intr_list_p = &intr_list->il_next;
	    if (compare_and_swap_ptr((void **) intr_list_p, NULL, intr_entry)) {
		/* we are the new second interrupt on this bit.
		 */
		pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap.iw_shared = 1;
		PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_INTR_ALLOC, pconn_vhdl,
			    "INT 0x%x (bridge bit %d) is new SECOND\n",
			    pcibr_int_bits, pcibr_int_bit));
		continue;
	    }
	    while (1) {
		pcibr_intr_p = &intr_list->il_intr;
		if (compare_and_swap_ptr((void **) pcibr_intr_p, NULL, pcibr_intr)) {
		    /* an entry on list was erased,
		     * and we replaced it, so we
		     * don't need our intr_entry.
		     */
		    kfree(intr_entry);

		    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_INTR_ALLOC, pconn_vhdl,
				"INT 0x%x (bridge bit %d) replaces erase Nth\n",
				pcibr_int_bits, pcibr_int_bit));
		    break;
		}
		intr_list_p = &intr_list->il_next;
		if (compare_and_swap_ptr((void **) intr_list_p, NULL, intr_entry)) {
		    /* entry appended to share list
		     */
		    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_INTR_ALLOC, pconn_vhdl,
				"INT 0x%x (bridge bit %d) is new Nth\n",
				pcibr_int_bits, pcibr_int_bit));
		    break;
		}
		/* step to next record in chain
		 */
		intr_list = *intr_list_p;
	    }
	}
    }

    hub_intr = (hub_intr_t)xtalk_intr;
    pcibr_intr->bi_irq = hub_intr->i_bit;
    pcibr_intr->bi_cpu = hub_intr->i_cpuid;
    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_INTR_ALLOC, pconn_vhdl,
		"pcibr_intr_alloc complete: pcibr_intr=0x%lx\n", pcibr_intr));
    return pcibr_intr;
}

/*ARGSUSED */
void
pcibr_intr_free(pcibr_intr_t pcibr_intr)
{
    unsigned                pcibr_int_bits = pcibr_intr->bi_ibits;
    pcibr_soft_t            pcibr_soft = pcibr_intr->bi_soft;
    unsigned                pcibr_int_bit;
    pcibr_intr_list_t       intr_list;
    int			    intr_shared;
    xtalk_intr_t	    *xtalk_intrp;

    for (pcibr_int_bit = 0; pcibr_int_bit < 8; pcibr_int_bit++) {
	if (pcibr_int_bits & (1 << pcibr_int_bit)) {
	    for (intr_list = 
		     pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap.iw_list;
		 intr_list != NULL;
		 intr_list = intr_list->il_next)
		if (compare_and_swap_ptr((void **) &intr_list->il_intr, 
					 pcibr_intr, 
					 NULL)) {

		    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_INTR_ALLOC, 
				pcibr_intr->bi_dev,
		    		"pcibr_intr_free: cleared hdlr from bit 0x%x\n",
				pcibr_int_bit));
		}
	    /* If this interrupt line is not being shared between multiple
	     * devices release the xtalk interrupt resources.
	     */
	    intr_shared = 
		pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap.iw_shared;
	    xtalk_intrp = &pcibr_soft->bs_intr[pcibr_int_bit].bsi_xtalk_intr;

	    if ((!intr_shared) && (*xtalk_intrp)) {

		xtalk_intr_free(*xtalk_intrp);
		*xtalk_intrp = 0;

		/* Clear the PCI device interrupt to bridge interrupt pin
		 * mapping.
		 */
		pcireg_intr_device_bit_clr(pcibr_soft, 
			BRIDGE_INT_DEV_MASK(pcibr_int_bit));
	    }
	}
    }
    kfree(pcibr_intr);
}

void
pcibr_setpciint(xtalk_intr_t xtalk_intr)
{
    iopaddr_t		 addr;
    xtalk_intr_vector_t	 vect;
    vertex_hdl_t	 vhdl;
    int			 bus_num;
    int			 pcibr_int_bit;
    void		 *bridge;
    
    addr = xtalk_intr_addr_get(xtalk_intr);
    vect = xtalk_intr_vector_get(xtalk_intr);
    vhdl = xtalk_intr_dev_get(xtalk_intr);

    /* bus and int_bits are stored in sfarg, bus bit3, int_bits bit2:0 */
    pcibr_int_bit = *((int *)xtalk_intr_sfarg_get(xtalk_intr)) & 0x7;
    bus_num = ((*((int *)xtalk_intr_sfarg_get(xtalk_intr)) & 0x8) >> 3);

    bridge = pcibr_bridge_ptr_get(vhdl, bus_num);
    pcireg_bridge_intr_addr_vect_set(bridge, pcibr_int_bit, vect);
    pcireg_bridge_intr_addr_addr_set(bridge, pcibr_int_bit, addr);
}

/*ARGSUSED */
int
pcibr_intr_connect(pcibr_intr_t pcibr_intr, intr_func_t intr_func, intr_arg_t intr_arg)
{
    pcibr_soft_t            pcibr_soft = pcibr_intr->bi_soft;
    unsigned                pcibr_int_bits = pcibr_intr->bi_ibits;
    unsigned                pcibr_int_bit;
    unsigned long	    s;

    if (pcibr_intr == NULL)
	return -1;

    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_INTR_ALLOC, pcibr_intr->bi_dev,
		"pcibr_intr_connect: intr_func=0x%lx, intr_arg=0x%lx\n",
		intr_func, intr_arg));

    pcibr_intr->bi_func = intr_func;
    pcibr_intr->bi_arg = intr_arg;
    *((volatile unsigned *)&pcibr_intr->bi_flags) |= PCIIO_INTR_CONNECTED;

    /*
     * For each PCI interrupt line requested, figure
     * out which Bridge PCI Interrupt Line it maps
     * to, and make sure there are xtalk resources
     * allocated for it.
     */
    for (pcibr_int_bit = 0; pcibr_int_bit < 8; pcibr_int_bit++)
	if (pcibr_int_bits & (1 << pcibr_int_bit)) {
            pcibr_intr_wrap_t       intr_wrap;
	    xtalk_intr_t            xtalk_intr;
            void                   *int_addr;

	    xtalk_intr = pcibr_soft->bs_intr[pcibr_int_bit].bsi_xtalk_intr;
	    intr_wrap = &pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap;

	    /*
	     * If this interrupt line is being shared and the connect has
	     * already been done, no need to do it again.
	     */
	    if (pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap.iw_connected)
		continue;


	    /*
	     * Use the pcibr wrapper function to handle all Bridge interrupts
	     * regardless of whether the interrupt line is shared or not.
	     */
	    int_addr = pcireg_intr_addr_addr(pcibr_soft, pcibr_int_bit);
	    pcibr_soft->bs_intr[pcibr_int_bit].bsi_int_bit = 
			       ((pcibr_soft->bs_busnum << 3) | pcibr_int_bit);
	    xtalk_intr_connect(xtalk_intr,
			       NULL,
			       (intr_arg_t) intr_wrap,
			       (xtalk_intr_setfunc_t) pcibr_setpciint,
			       &pcibr_soft->bs_intr[pcibr_int_bit].bsi_int_bit);

	    pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap.iw_connected = 1;

	    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_INTR_ALLOC, pcibr_intr->bi_dev,
			"pcibr_setpciint: int_addr=0x%lx, *int_addr=0x%lx, "
			"pcibr_int_bit=0x%x\n", int_addr, 
			pcireg_intr_addr_get(pcibr_soft, pcibr_int_bit),
			pcibr_int_bit));
	}

	s = pcibr_lock(pcibr_soft);
	pcireg_intr_enable_bit_set(pcibr_soft, pcibr_int_bits);
	pcireg_tflush_get(pcibr_soft);
	pcibr_unlock(pcibr_soft, s);

    return 0;
}

/*ARGSUSED */
void
pcibr_intr_disconnect(pcibr_intr_t pcibr_intr)
{
    pcibr_soft_t            pcibr_soft = pcibr_intr->bi_soft;
    unsigned                pcibr_int_bits = pcibr_intr->bi_ibits;
    unsigned                pcibr_int_bit;
    pcibr_intr_wrap_t	    intr_wrap;
    unsigned long	    s;

    /* Stop calling the function. Now.
     */
    *((volatile unsigned *)&pcibr_intr->bi_flags) &= ~PCIIO_INTR_CONNECTED;
    pcibr_intr->bi_func = 0;
    pcibr_intr->bi_arg = 0;
    /*
     * For each PCI interrupt line requested, figure
     * out which Bridge PCI Interrupt Line it maps
     * to, and disconnect the interrupt.
     */

    /* don't disable interrupts for lines that
     * are shared between devices.
     */
    for (pcibr_int_bit = 0; pcibr_int_bit < 8; pcibr_int_bit++)
	if ((pcibr_int_bits & (1 << pcibr_int_bit)) &&
	    (pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap.iw_shared))
	    pcibr_int_bits &= ~(1 << pcibr_int_bit);
    if (!pcibr_int_bits)
	return;

    s = pcibr_lock(pcibr_soft);
    pcireg_intr_enable_bit_clr(pcibr_soft, pcibr_int_bits);
    pcireg_tflush_get(pcibr_soft); 	/* wait until Bridge PIO complete */
    pcibr_unlock(pcibr_soft, s);

    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_INTR_ALLOC, pcibr_intr->bi_dev,
		"pcibr_intr_disconnect: disabled int_bits=0x%x\n", 
		pcibr_int_bits));

    for (pcibr_int_bit = 0; pcibr_int_bit < 8; pcibr_int_bit++)
	if (pcibr_int_bits & (1 << pcibr_int_bit)) {

	    /* if the interrupt line is now shared,
	     * do not disconnect it.
	     */
	    if (pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap.iw_shared)
		continue;

	    xtalk_intr_disconnect(pcibr_soft->bs_intr[pcibr_int_bit].bsi_xtalk_intr);
	    pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap.iw_connected = 0;

	    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_INTR_ALLOC, pcibr_intr->bi_dev,
			"pcibr_intr_disconnect: disconnect int_bits=0x%x\n",
			pcibr_int_bits));

	    /* if we are sharing the interrupt line,
	     * connect us up; this closes the hole
	     * where the another pcibr_intr_alloc()
	     * was in progress as we disconnected.
	     */
	    intr_wrap = &pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap;
	    if (!pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap.iw_shared)
		continue;

	    pcibr_soft->bs_intr[pcibr_int_bit].bsi_int_bit =
				((pcibr_soft->bs_busnum << 3) | pcibr_int_bit);
	    xtalk_intr_connect(pcibr_soft->bs_intr[pcibr_int_bit].bsi_xtalk_intr,
			       NULL,
			       (intr_arg_t) intr_wrap,
			       (xtalk_intr_setfunc_t) pcibr_setpciint,
			       &pcibr_soft->bs_intr[pcibr_int_bit].bsi_int_bit);

	    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_INTR_ALLOC, pcibr_intr->bi_dev,
			"pcibr_intr_disconnect: now-sharing int_bits=0x%x\n",
			pcibr_int_bit));
	}
}

/*ARGSUSED */
vertex_hdl_t
pcibr_intr_cpu_get(pcibr_intr_t pcibr_intr)
{
    pcibr_soft_t            pcibr_soft = pcibr_intr->bi_soft;
    unsigned                pcibr_int_bits = pcibr_intr->bi_ibits;
    unsigned                pcibr_int_bit;

    for (pcibr_int_bit = 0; pcibr_int_bit < 8; pcibr_int_bit++)
	if (pcibr_int_bits & (1 << pcibr_int_bit))
	    return xtalk_intr_cpu_get(pcibr_soft->bs_intr[pcibr_int_bit].bsi_xtalk_intr);
    return 0;
}

/* =====================================================================
 *    INTERRUPT HANDLING
 */
void
pcibr_clearwidint(pcibr_soft_t pcibr_soft)
{
    pcireg_intr_dst_set(pcibr_soft, 0);
}


void
pcibr_setwidint(xtalk_intr_t intr)
{
    xwidgetnum_t            targ = xtalk_intr_target_get(intr);
    iopaddr_t               addr = xtalk_intr_addr_get(intr);
    xtalk_intr_vector_t     vect = xtalk_intr_vector_get(intr);

    pcibr_soft_t	   bridge = (pcibr_soft_t)xtalk_intr_sfarg_get(intr);

    pcireg_intr_dst_target_id_set(bridge, targ);
    pcireg_intr_dst_addr_set(bridge, addr);
    pcireg_intr_host_err_set(bridge, vect);
}
