/*
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001-2002 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <asm/sn/sgi.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/xtalk/xwidget.h>
#include <asm/sn/pci/bridge.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/pci/pci_defs.h>
#include <asm/sn/prio.h>
#include <asm/sn/xtalk/xbow.h>
#include <asm/sn/ioc3.h>
#include <asm/sn/eeprom.h>
#include <asm/sn/io.h>
#include <asm/sn/sn_private.h>

pcibr_hints_t           pcibr_hints_get(devfs_handle_t, int);
void                    pcibr_hints_fix_rrbs(devfs_handle_t);
void                    pcibr_hints_dualslot(devfs_handle_t, pciio_slot_t, pciio_slot_t);
void			pcibr_hints_intr_bits(devfs_handle_t, pcibr_intr_bits_f *);
void                    pcibr_set_rrb_callback(devfs_handle_t, rrb_alloc_funct_t);
void                    pcibr_hints_handsoff(devfs_handle_t);
void                    pcibr_hints_subdevs(devfs_handle_t, pciio_slot_t, uint64_t);

pcibr_hints_t
pcibr_hints_get(devfs_handle_t xconn_vhdl, int alloc)
{
    arbitrary_info_t        ainfo = 0;
    graph_error_t	    rv;
    pcibr_hints_t           hint;

    rv = hwgraph_info_get_LBL(xconn_vhdl, INFO_LBL_PCIBR_HINTS, &ainfo);

    if (alloc && (rv != GRAPH_SUCCESS)) {

	NEW(hint);
	hint->rrb_alloc_funct = NULL;
	hint->ph_intr_bits = NULL;
	rv = hwgraph_info_add_LBL(xconn_vhdl, 
				  INFO_LBL_PCIBR_HINTS, 	
				  (arbitrary_info_t) hint);
	if (rv != GRAPH_SUCCESS)
	    goto abnormal_exit;

	rv = hwgraph_info_get_LBL(xconn_vhdl, INFO_LBL_PCIBR_HINTS, &ainfo);
	
	if (rv != GRAPH_SUCCESS)
	    goto abnormal_exit;

	if (ainfo != (arbitrary_info_t) hint)
	    goto abnormal_exit;
    }
    return (pcibr_hints_t) ainfo;

abnormal_exit:
#ifdef LATER
    printf("SHOULD NOT BE HERE\n");
#endif
    DEL(hint);
    return(NULL);

}

void
pcibr_hints_fix_some_rrbs(devfs_handle_t xconn_vhdl, unsigned mask)
{
    pcibr_hints_t           hint = pcibr_hints_get(xconn_vhdl, 1);

    if (hint)
	hint->ph_rrb_fixed = mask;
#if DEBUG
    else
	printk("pcibr_hints_fix_rrbs: pcibr_hints_get failed at\n"
		"\t%p\n", xconn_vhdl);
#endif
}

void
pcibr_hints_fix_rrbs(devfs_handle_t xconn_vhdl)
{
    pcibr_hints_fix_some_rrbs(xconn_vhdl, 0xFF);
}

void
pcibr_hints_dualslot(devfs_handle_t xconn_vhdl,
		     pciio_slot_t host,
		     pciio_slot_t guest)
{
    pcibr_hints_t           hint = pcibr_hints_get(xconn_vhdl, 1);

    if (hint)
	hint->ph_host_slot[guest] = host + 1;
#if DEBUG
    else
	printk("pcibr_hints_dualslot: pcibr_hints_get failed at\n"
		"\t%p\n", xconn_vhdl);
#endif
}

void
pcibr_hints_intr_bits(devfs_handle_t xconn_vhdl,
		      pcibr_intr_bits_f *xxx_intr_bits)
{
    pcibr_hints_t           hint = pcibr_hints_get(xconn_vhdl, 1);

    if (hint)
	hint->ph_intr_bits = xxx_intr_bits;
#if DEBUG
    else
	printk("pcibr_hints_intr_bits: pcibr_hints_get failed at\n"
	       "\t%p\n", xconn_vhdl);
#endif
}

void
pcibr_set_rrb_callback(devfs_handle_t xconn_vhdl, rrb_alloc_funct_t rrb_alloc_funct)
{
    pcibr_hints_t           hint = pcibr_hints_get(xconn_vhdl, 1);

    if (hint)
	hint->rrb_alloc_funct = rrb_alloc_funct;
}

void
pcibr_hints_handsoff(devfs_handle_t xconn_vhdl)
{
    pcibr_hints_t           hint = pcibr_hints_get(xconn_vhdl, 1);

    if (hint)
	hint->ph_hands_off = 1;
#if DEBUG
    else
	printk("pcibr_hints_handsoff: pcibr_hints_get failed at\n"
		"\t%p\n", xconn_vhdl);
#endif
}

void
pcibr_hints_subdevs(devfs_handle_t xconn_vhdl,
		    pciio_slot_t slot,
		    uint64_t subdevs)
{
    arbitrary_info_t        ainfo = 0;
    char                    sdname[16];
    devfs_handle_t            pconn_vhdl = GRAPH_VERTEX_NONE;

    sprintf(sdname, "pci/%d", slot);
    (void) hwgraph_path_add(xconn_vhdl, sdname, &pconn_vhdl);
    if (pconn_vhdl == GRAPH_VERTEX_NONE) {
#if DEBUG
	printk("pcibr_hints_subdevs: hwgraph_path_create failed at\n"
		"\t%p (seeking %s)\n", xconn_vhdl, sdname);
#endif
	return;
    }
    hwgraph_info_get_LBL(pconn_vhdl, INFO_LBL_SUBDEVS, &ainfo);
    if (ainfo == 0) {
	uint64_t                *subdevp;

	NEW(subdevp);
	if (!subdevp) {
#if DEBUG
	    printk("pcibr_hints_subdevs: subdev ptr alloc failed at\n"
		    "\t%p\n", pconn_vhdl);
#endif
	    return;
	}
	*subdevp = subdevs;
	hwgraph_info_add_LBL(pconn_vhdl, INFO_LBL_SUBDEVS, (arbitrary_info_t) subdevp);
	hwgraph_info_get_LBL(pconn_vhdl, INFO_LBL_SUBDEVS, &ainfo);
	if (ainfo == (arbitrary_info_t) subdevp)
	    return;
	DEL(subdevp);
	if (ainfo == (arbitrary_info_t) NULL) {
#if DEBUG
	    printk("pcibr_hints_subdevs: null subdevs ptr at\n"
		    "\t%p\n", pconn_vhdl);
#endif
	    return;
	}
#if DEBUG
	printk("pcibr_subdevs_get: dup subdev add_LBL at\n"
		"\t%p\n", pconn_vhdl);
#endif
    }
    *(uint64_t *) ainfo = subdevs;
}
