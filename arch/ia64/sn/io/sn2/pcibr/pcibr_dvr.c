/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001-2003 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <asm/sn/sgi.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/iograph.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/pci/pci_defs.h>

#include <asm/sn/prio.h> 
#include <asm/sn/sn_private.h>

/*
 * global variables to toggle the different levels of pcibr debugging.  
 *   -pcibr_debug_mask is the mask of the different types of debugging
 *    you want to enable.  See sys/PCI/pcibr_private.h 
 *   -pcibr_debug_module is the module you want to trace.  By default
 *    all modules are trace.  The format is something like "001c10".
 *   -pcibr_debug_widget is the widget you want to trace.  For TIO 
 *    based bricks use the corelet id.
 *   -pcibr_debug_slot is the pci slot you want to trace.
 */
uint32_t   	  pcibr_debug_mask;			/* 0x00000000 to disable */
static char      *pcibr_debug_module = "all";		/* 'all' for all modules */
static int	   pcibr_debug_widget = -1;		/* '-1' for all widgets  */
static int	   pcibr_debug_slot = -1;		/* '-1' for all slots    */


#if PCIBR_SOFT_LIST
pcibr_list_p            pcibr_list;
#endif

extern char *pci_space[];

/* =====================================================================
 *    Function Table of Contents
 *
 *      The order of functions in this file has stopped
 *      making much sense. We might want to take a look
 *      at it some time and bring back some sanity, or
 *      perhaps bust this file into smaller chunks.
 */

extern void		 do_pcibr_rrb_free_all(pcibr_soft_t, pciio_slot_t);
extern void              do_pcibr_rrb_autoalloc(pcibr_soft_t, int, int, int);
extern void		 pcibr_rrb_alloc_more(pcibr_soft_t pcibr_soft, int slot,
							int vchan, int more_rrbs);

extern int  		 pcibr_wrb_flush(vertex_hdl_t);
extern int               pcibr_rrb_alloc(vertex_hdl_t, int *, int *);
void            	 pcibr_rrb_alloc_more(pcibr_soft_t, int, int, int);

extern void              pcibr_rrb_flush(vertex_hdl_t);

static int                pcibr_try_set_device(pcibr_soft_t, pciio_slot_t, unsigned, uint64_t);
void                     pcibr_release_device(pcibr_soft_t, pciio_slot_t, uint64_t);

extern iopaddr_t         pcibr_bus_addr_alloc(pcibr_soft_t, pciio_win_info_t,
                                              pciio_space_t, int, int, int);
extern int		 hwgraph_vertex_name_get(vertex_hdl_t vhdl, char *buf, 
						 uint buflen);

int			 pcibr_detach(vertex_hdl_t);
void			 pcibr_directmap_init(pcibr_soft_t);
int			 pcibr_pcix_rbars_calc(pcibr_soft_t);
extern int               pcibr_ate_alloc(pcibr_soft_t, int, struct resource *);
extern void              pcibr_ate_free(pcibr_soft_t, int, int, struct resource *);
extern pciio_dmamap_t	 get_free_pciio_dmamap(vertex_hdl_t);
extern void		 free_pciio_dmamap(pcibr_dmamap_t);
extern int 		 pcibr_widget_to_bus(vertex_hdl_t pcibr_vhdl);

extern void 		ate_write(pcibr_soft_t, int, int, bridge_ate_t);

pcibr_info_t      pcibr_info_get(vertex_hdl_t);

static iopaddr_t         pcibr_addr_pci_to_xio(vertex_hdl_t, pciio_slot_t, pciio_space_t, iopaddr_t, size_t, unsigned);

pcibr_piomap_t          pcibr_piomap_alloc(vertex_hdl_t, device_desc_t, pciio_space_t, iopaddr_t, size_t, size_t, unsigned);
void                    pcibr_piomap_free(pcibr_piomap_t);
caddr_t                 pcibr_piomap_addr(pcibr_piomap_t, iopaddr_t, size_t);
void                    pcibr_piomap_done(pcibr_piomap_t);
caddr_t                 pcibr_piotrans_addr(vertex_hdl_t, device_desc_t, pciio_space_t, iopaddr_t, size_t, unsigned);
iopaddr_t               pcibr_piospace_alloc(vertex_hdl_t, device_desc_t, pciio_space_t, size_t, size_t);
void                    pcibr_piospace_free(vertex_hdl_t, pciio_space_t, iopaddr_t, size_t);

static iopaddr_t         pcibr_flags_to_d64(unsigned, pcibr_soft_t);
extern bridge_ate_t     pcibr_flags_to_ate(pcibr_soft_t, unsigned);

pcibr_dmamap_t          pcibr_dmamap_alloc(vertex_hdl_t, device_desc_t, size_t, unsigned);
void                    pcibr_dmamap_free(pcibr_dmamap_t);
extern bridge_ate_p     pcibr_ate_addr(pcibr_soft_t, int);
static iopaddr_t         pcibr_addr_xio_to_pci(pcibr_soft_t, iopaddr_t, size_t);
iopaddr_t               pcibr_dmamap_addr(pcibr_dmamap_t, paddr_t, size_t);
void                    pcibr_dmamap_done(pcibr_dmamap_t);
cnodeid_t		pcibr_get_dmatrans_node(vertex_hdl_t);
iopaddr_t               pcibr_dmatrans_addr(vertex_hdl_t, device_desc_t, paddr_t, size_t, unsigned);
void                    pcibr_dmamap_drain(pcibr_dmamap_t);
void                    pcibr_dmaaddr_drain(vertex_hdl_t, paddr_t, size_t);
iopaddr_t               pcibr_dmamap_pciaddr_get(pcibr_dmamap_t);

void                    pcibr_provider_startup(vertex_hdl_t);
void                    pcibr_provider_shutdown(vertex_hdl_t);

int                     pcibr_reset(vertex_hdl_t);
pciio_endian_t          pcibr_endian_set(vertex_hdl_t, pciio_endian_t, pciio_endian_t);
int                     pcibr_device_flags_set(vertex_hdl_t, pcibr_device_flags_t);

extern int		pcibr_slot_info_free(vertex_hdl_t,pciio_slot_t);
extern int              pcibr_slot_detach(vertex_hdl_t, pciio_slot_t, int,
                                                      char *, int *);

pciio_businfo_t		pcibr_businfo_get(vertex_hdl_t);

/* =====================================================================
 *    Device(x) register management
 */

/* pcibr_try_set_device: attempt to modify Device(x)
 * for the specified slot on the specified bridge
 * as requested in flags, limited to the specified
 * bits. Returns which BRIDGE bits were in conflict,
 * or ZERO if everything went OK.
 *
 * Caller MUST hold pcibr_lock when calling this function.
 */
static int
pcibr_try_set_device(pcibr_soft_t pcibr_soft,
		     pciio_slot_t slot,
		     unsigned flags,
		     uint64_t mask)
{
    pcibr_soft_slot_t       slotp;
    uint64_t		    old;
    uint64_t		    new;
    uint64_t		    chg;
    uint64_t		    bad;
    uint64_t		    badpmu;
    uint64_t		    badd32;
    uint64_t		    badd64;
    uint64_t		    fix;
    unsigned long	    s;

    slotp = &pcibr_soft->bs_slot[slot];

    s = pcibr_lock(pcibr_soft);

    old = slotp->bss_device;

    /* figure out what the desired
     * Device(x) bits are based on
     * the flags specified.
     */

    new = old;

    /* Currently, we inherit anything that
     * the new caller has not specified in
     * one way or another, unless we take
     * action here to not inherit.
     *
     * This is needed for the "swap" stuff,
     * since it could have been set via
     * pcibr_endian_set -- altho note that
     * any explicit PCIBR_BYTE_STREAM or
     * PCIBR_WORD_VALUES will freely override
     * the effect of that call (and vice
     * versa, no protection either way).
     *
     * I want to get rid of pcibr_endian_set
     * in favor of tracking DMA endianness
     * using the flags specified when DMA
     * channels are created.
     */

#define	BRIDGE_DEV_WRGA_BITS	(BRIDGE_DEV_PMU_WRGA_EN | BRIDGE_DEV_DIR_WRGA_EN)
#define	BRIDGE_DEV_SWAP_BITS	(BRIDGE_DEV_SWAP_PMU | BRIDGE_DEV_SWAP_DIR)

    /* Do not use Barrier, Write Gather,
     * or Prefetch unless asked.
     * Leave everything else as it
     * was from the last time.
     */
    new = new
	& ~BRIDGE_DEV_BARRIER
	& ~BRIDGE_DEV_WRGA_BITS
	& ~BRIDGE_DEV_PREF
	;

    /* Generic macro flags
     */
    if (flags & PCIIO_DMA_DATA) {
	new = (new
            & ~BRIDGE_DEV_BARRIER)      /* barrier off */
            | BRIDGE_DEV_PREF;          /* prefetch on */

    }
    if (flags & PCIIO_DMA_CMD) {
        new = ((new
            & ~BRIDGE_DEV_PREF)         /* prefetch off */
            & ~BRIDGE_DEV_WRGA_BITS)    /* write gather off */
            | BRIDGE_DEV_BARRIER;       /* barrier on */
    }
    /* Generic detail flags
     */
    if (flags & PCIIO_WRITE_GATHER)
	new |= BRIDGE_DEV_WRGA_BITS;
    if (flags & PCIIO_NOWRITE_GATHER)
	new &= ~BRIDGE_DEV_WRGA_BITS;

    if (flags & PCIIO_PREFETCH)
	new |= BRIDGE_DEV_PREF;
    if (flags & PCIIO_NOPREFETCH)
	new &= ~BRIDGE_DEV_PREF;

    if (flags & PCIBR_WRITE_GATHER)
	new |= BRIDGE_DEV_WRGA_BITS;
    if (flags & PCIBR_NOWRITE_GATHER)
	new &= ~BRIDGE_DEV_WRGA_BITS;

    if (flags & PCIIO_BYTE_STREAM)
	new |= BRIDGE_DEV_SWAP_DIR;
    if (flags & PCIIO_WORD_VALUES)
	new &= ~BRIDGE_DEV_SWAP_DIR;

    /* Provider-specific flags
     */
    if (flags & PCIBR_PREFETCH)
	new |= BRIDGE_DEV_PREF;
    if (flags & PCIBR_NOPREFETCH)
	new &= ~BRIDGE_DEV_PREF;

    if (flags & PCIBR_PRECISE)
	new |= BRIDGE_DEV_PRECISE;
    if (flags & PCIBR_NOPRECISE)
	new &= ~BRIDGE_DEV_PRECISE;

    if (flags & PCIBR_BARRIER)
	new |= BRIDGE_DEV_BARRIER;
    if (flags & PCIBR_NOBARRIER)
	new &= ~BRIDGE_DEV_BARRIER;

    if (flags & PCIBR_64BIT)
	new |= BRIDGE_DEV_DEV_SIZE;
    if (flags & PCIBR_NO64BIT)
	new &= ~BRIDGE_DEV_DEV_SIZE;

    /*
     * PIC BRINGUP WAR (PV# 855271):
     * Allow setting BRIDGE_DEV_VIRTUAL_EN on PIC iff we're a 64-bit
     * device.  The bit is only intended for 64-bit devices and, on
     * PIC, can cause problems for 32-bit devices.
     */
    if (mask == BRIDGE_DEV_D64_BITS &&
				PCIBR_WAR_ENABLED(PV855271, pcibr_soft)) {
	if (flags & PCIBR_VCHAN1) {
		new |= BRIDGE_DEV_VIRTUAL_EN;
		mask |= BRIDGE_DEV_VIRTUAL_EN;
	}
    }

    /* PIC BRINGUP WAR (PV# 878674):   Don't allow 64bit PIO accesses */
    if ((flags & PCIBR_64BIT) &&
				PCIBR_WAR_ENABLED(PV878674, pcibr_soft)) {
	new &= ~(1ull << 22);
    }

    chg = old ^ new;				/* what are we changing, */
    chg &= mask;				/* of the interesting bits */

    if (chg) {

	badd32 = slotp->bss_d32_uctr ? (BRIDGE_DEV_D32_BITS & chg) : 0;
	badpmu = slotp->bss_pmu_uctr ? (XBRIDGE_DEV_PMU_BITS & chg) : 0;
	badd64 = slotp->bss_d64_uctr ? (XBRIDGE_DEV_D64_BITS & chg) : 0;
	bad = badpmu | badd32 | badd64;

	if (bad) {

	    /* some conflicts can be resolved by
	     * forcing the bit on. this may cause
	     * some performance degredation in
	     * the stream(s) that want the bit off,
	     * but the alternative is not allowing
	     * the new stream at all.
	     */
            if ( (fix = bad & (BRIDGE_DEV_PRECISE |
                             BRIDGE_DEV_BARRIER)) ) {
		bad &= ~fix;
		/* don't change these bits if
		 * they are already set in "old"
		 */
		chg &= ~(fix & old);
	    }
	    /* some conflicts can be resolved by
	     * forcing the bit off. this may cause
	     * some performance degredation in
	     * the stream(s) that want the bit on,
	     * but the alternative is not allowing
	     * the new stream at all.
	     */
	    if ( (fix = bad & (BRIDGE_DEV_WRGA_BITS |
			     BRIDGE_DEV_PREF)) ) {
		bad &= ~fix;
		/* don't change these bits if
		 * we wanted to turn them on.
		 */
		chg &= ~(fix & new);
	    }
	    /* conflicts in other bits mean
	     * we can not establish this DMA
	     * channel while the other(s) are
	     * still present.
	     */
	    if (bad) {
		pcibr_unlock(pcibr_soft, s);
		PCIBR_DEBUG((PCIBR_DEBUG_DEVREG, pcibr_soft->bs_vhdl,
			    "pcibr_try_set_device: mod blocked by 0x%x\n", bad));
		return bad;
	    }
	}
    }
    if (mask == BRIDGE_DEV_PMU_BITS)
	slotp->bss_pmu_uctr++;
    if (mask == BRIDGE_DEV_D32_BITS)
	slotp->bss_d32_uctr++;
    if (mask == BRIDGE_DEV_D64_BITS)
	slotp->bss_d64_uctr++;

    /* the value we want to write is the
     * original value, with the bits for
     * our selected changes flipped, and
     * with any disabled features turned off.
     */
    new = old ^ chg;			/* only change what we want to change */

    if (slotp->bss_device == new) {
	pcibr_unlock(pcibr_soft, s);
	return 0;
    }
    
    pcireg_device_set(pcibr_soft, slot, new);
    slotp->bss_device = new;
    pcireg_tflush_get(pcibr_soft);	/* wait until Bridge PIO complete */
    pcibr_unlock(pcibr_soft, s);

    PCIBR_DEBUG((PCIBR_DEBUG_DEVREG, pcibr_soft->bs_vhdl,
		"pcibr_try_set_device: Device(%d): 0x%x\n", slot, new));
    return 0;
}

void
pcibr_release_device(pcibr_soft_t pcibr_soft,
		     pciio_slot_t slot,
		     uint64_t mask)
{
    pcibr_soft_slot_t       slotp;
    unsigned long           s;

    slotp = &pcibr_soft->bs_slot[slot];

    s = pcibr_lock(pcibr_soft);

    if (mask == BRIDGE_DEV_PMU_BITS)
	slotp->bss_pmu_uctr--;
    if (mask == BRIDGE_DEV_D32_BITS)
	slotp->bss_d32_uctr--;
    if (mask == BRIDGE_DEV_D64_BITS)
	slotp->bss_d64_uctr--;

    pcibr_unlock(pcibr_soft, s);
}


/* =====================================================================
 *    Bridge (pcibr) "Device Driver" entry points
 */


static int
pcibr_mmap(struct file * file, struct vm_area_struct * vma)
{
	vertex_hdl_t		pcibr_vhdl = file->f_dentry->d_fsdata;
	pcibr_soft_t            pcibr_soft;
	void               *bridge;
	unsigned long		phys_addr;
	int			error = 0;

	pcibr_soft = pcibr_soft_get(pcibr_vhdl);
	bridge = pcibr_soft->bs_base;
	phys_addr = (unsigned long)bridge & ~0xc000000000000000; /* Mask out the Uncache bits */
        vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
        vma->vm_flags |= VM_RESERVED | VM_IO;
        error = io_remap_page_range(vma, phys_addr, vma->vm_start,
				    vma->vm_end - vma->vm_start,
				    vma->vm_page_prot);
	return error;
}

/*
 * This is the file operation table for the pcibr driver.
 * As each of the functions are implemented, put the
 * appropriate function name below.
 */
static int pcibr_mmap(struct file * file, struct vm_area_struct * vma);
struct file_operations pcibr_fops = {
	.owner		= THIS_MODULE,
	.mmap		= pcibr_mmap,
};


/* This is special case code used by grio. There are plans to make
 * this a bit more general in the future, but till then this should
 * be sufficient.
 */
pciio_slot_t
pcibr_device_slot_get(vertex_hdl_t dev_vhdl)
{
    char                    devname[MAXDEVNAME];
    vertex_hdl_t            tdev;
    pciio_info_t            pciio_info;
    pciio_slot_t            slot = PCIIO_SLOT_NONE;

    vertex_to_name(dev_vhdl, devname, MAXDEVNAME);

    /* run back along the canonical path
     * until we find a PCI connection point.
     */
    tdev = hwgraph_connectpt_get(dev_vhdl);
    while (tdev != GRAPH_VERTEX_NONE) {
	pciio_info = pciio_info_chk(tdev);
	if (pciio_info) {
	    slot = PCIBR_INFO_SLOT_GET_INT(pciio_info);
	    break;
	}
	hwgraph_vertex_unref(tdev);
	tdev = hwgraph_connectpt_get(tdev);
    }
    hwgraph_vertex_unref(tdev);

    return slot;
}

pcibr_info_t
pcibr_info_get(vertex_hdl_t vhdl)
{
    return (pcibr_info_t) pciio_info_get(vhdl);
}

pcibr_info_t
pcibr_device_info_new(
			 pcibr_soft_t pcibr_soft,
			 pciio_slot_t slot,
			 pciio_function_t rfunc,
			 pciio_vendor_id_t vendor,
			 pciio_device_id_t device)
{
    pcibr_info_t            pcibr_info;
    pciio_function_t        func;
    int                     ibit;

    func = (rfunc == PCIIO_FUNC_NONE) ? 0 : rfunc;

    /*
     * Create a pciio_info_s for this device.  pciio_device_info_new()
     * will set the c_slot (which is suppose to represent the external
     * slot (i.e the slot number silk screened on the back of the I/O
     * brick)).  So for PIC we need to adjust this "internal slot" num
     * passed into us, into its external representation.  See comment
     * for the PCIBR_DEVICE_TO_SLOT macro for more information.
     */
    pcibr_info = kmalloc(sizeof (*(pcibr_info)), GFP_KERNEL);
    if ( !pcibr_info ) {
	return NULL;
    }
    memset(pcibr_info, 0, sizeof (*(pcibr_info)));

    pciio_device_info_new(&pcibr_info->f_c, pcibr_soft->bs_vhdl,
			  PCIBR_DEVICE_TO_SLOT(pcibr_soft, slot),
			  rfunc, vendor, device);
    pcibr_info->f_dev = slot;

    /* Set PCI bus number */
    pcibr_info->f_bus = pcibr_widget_to_bus(pcibr_soft->bs_vhdl);

    if (slot != PCIIO_SLOT_NONE) {

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
	 *
	 * XXX- allow pcibr_hints to override default
	 * XXX- allow ADMIN to override pcibr_hints
	 */
	for (ibit = 0; ibit < 4; ++ibit)
	    pcibr_info->f_ibit[ibit] =
		(slot + 4 * ibit) & 7;

	/*
	 * Record the info in the sparse func info space.
	 */
	if (func < pcibr_soft->bs_slot[slot].bss_ninfo)
	    pcibr_soft->bs_slot[slot].bss_infos[func] = pcibr_info;
    }
    return pcibr_info;
}


/*
 * pcibr_device_unregister
 *	This frees up any hardware resources reserved for this PCI device
 * 	and removes any PCI infrastructural information setup for it.
 *	This is usually used at the time of shutting down of the PCI card.
 */
int
pcibr_device_unregister(vertex_hdl_t pconn_vhdl)
{
    pciio_info_t	 pciio_info;
    vertex_hdl_t	 pcibr_vhdl;
    pciio_slot_t	 slot;
    pcibr_soft_t	 pcibr_soft;
    int                  count_vchan0, count_vchan1;
    unsigned long	 s;
    int			 error_call;
    int			 error = 0;

    pciio_info = pciio_info_get(pconn_vhdl);

    pcibr_vhdl = pciio_info_master_get(pciio_info);
    slot = PCIBR_INFO_SLOT_GET_INT(pciio_info);

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    /* Clear all the hardware xtalk resources for this device */
    xtalk_widgetdev_shutdown(pcibr_soft->bs_conn, slot);

    /* Flush all the rrbs */
    pcibr_rrb_flush(pconn_vhdl);

    /*
     * If the RRB configuration for this slot has changed, set it 
     * back to the boot-time default
     */
    if (pcibr_soft->bs_rrb_valid_dflt[slot][VCHAN0] >= 0) {

        s = pcibr_lock(pcibr_soft);

        pcibr_soft->bs_rrb_res[slot] = pcibr_soft->bs_rrb_res[slot] +
                                       pcibr_soft->bs_rrb_valid[slot][VCHAN0] +
                                       pcibr_soft->bs_rrb_valid[slot][VCHAN1] +
                                       pcibr_soft->bs_rrb_valid[slot][VCHAN2] +
                                       pcibr_soft->bs_rrb_valid[slot][VCHAN3];

        /* Free the rrbs allocated to this slot, both the normal & virtual */
	do_pcibr_rrb_free_all(pcibr_soft, slot);

        count_vchan0 = pcibr_soft->bs_rrb_valid_dflt[slot][VCHAN0];
        count_vchan1 = pcibr_soft->bs_rrb_valid_dflt[slot][VCHAN1];

        pcibr_unlock(pcibr_soft, s);

        pcibr_rrb_alloc(pconn_vhdl, &count_vchan0, &count_vchan1);

    }

    /* Flush the write buffers !! */
    error_call = pcibr_wrb_flush(pconn_vhdl);

    if (error_call)
        error = error_call;

    /* Clear the information specific to the slot */
    error_call = pcibr_slot_info_free(pcibr_vhdl, slot);

    if (error_call)
        error = error_call;

    return error;
    
}

/*
 * pcibr_driver_reg_callback
 *      CDL will call this function for each device found in the PCI
 *      registry that matches the vendor/device IDs supported by 
 *      the driver being registered.  The device's connection vertex
 *      and the driver's attach function return status enable the
 *      slot's device status to be set.
 */
void
pcibr_driver_reg_callback(vertex_hdl_t pconn_vhdl,
			  int key1, int key2, int error)
{
    pciio_info_t	 pciio_info;
    pcibr_info_t         pcibr_info;
    vertex_hdl_t	 pcibr_vhdl;
    pciio_slot_t	 slot;
    pcibr_soft_t	 pcibr_soft;

    /* Do not set slot status for vendor/device ID wildcard drivers */
    if ((key1 == -1) || (key2 == -1))
        return;

    pciio_info = pciio_info_get(pconn_vhdl);
    pcibr_info = pcibr_info_get(pconn_vhdl);

    pcibr_vhdl = pciio_info_master_get(pciio_info);
    slot = PCIBR_INFO_SLOT_GET_INT(pciio_info);

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    pcibr_info->f_att_det_error = error;

#ifdef CONFIG_HOTPLUG_PCI_SGI
    pcibr_soft->bs_slot[slot].slot_status &= ~SLOT_STATUS_MASK;

    if (error) {
        pcibr_soft->bs_slot[slot].slot_status |= SLOT_STARTUP_INCMPLT;
    } else {
        pcibr_soft->bs_slot[slot].slot_status |= SLOT_STARTUP_CMPLT;
    }
#endif	/* CONFIG_HOTPLUG_PCI_SGI */
}

/*
 * pcibr_driver_unreg_callback
 *      CDL will call this function for each device found in the PCI
 *      registry that matches the vendor/device IDs supported by 
 *      the driver being unregistered.  The device's connection vertex
 *      and the driver's detach function return status enable the
 *      slot's device status to be set.
 */
void
pcibr_driver_unreg_callback(vertex_hdl_t pconn_vhdl, 
                            int key1, int key2, int error)
{
    pciio_info_t	 pciio_info;
    pcibr_info_t         pcibr_info;
    vertex_hdl_t	 pcibr_vhdl;
    pciio_slot_t	 slot;
    pcibr_soft_t	 pcibr_soft;

    /* Do not set slot status for vendor/device ID wildcard drivers */
    if ((key1 == -1) || (key2 == -1))
        return;

    pciio_info = pciio_info_get(pconn_vhdl);
    pcibr_info = pcibr_info_get(pconn_vhdl);

    pcibr_vhdl = pciio_info_master_get(pciio_info);
    slot = PCIBR_INFO_SLOT_GET_INT(pciio_info);

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    pcibr_info->f_att_det_error = error;
#ifdef CONFIG_HOTPLUG_PCI_SGI
    pcibr_soft->bs_slot[slot].slot_status &= ~SLOT_STATUS_MASK;

    if (error) {
        pcibr_soft->bs_slot[slot].slot_status |= SLOT_SHUTDOWN_INCMPLT;
    } else {
        pcibr_soft->bs_slot[slot].slot_status |= SLOT_SHUTDOWN_CMPLT;
    }
#endif	/* CONFIG_HOTPLUG_PCI_SGI */
}

/*
 * pcibr_detach:
 *	Detach the bridge device from the hwgraph after cleaning out all the 
 *	underlying vertices.
 */

int
pcibr_detach(vertex_hdl_t xconn)
{
    pciio_slot_t	 slot;
    vertex_hdl_t	 pcibr_vhdl;
    pcibr_soft_t	 pcibr_soft;
    unsigned long        s;

    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_DETACH, xconn, "pcibr_detach\n"));

    /* Get the bridge vertex from its xtalk connection point */
    if (hwgraph_traverse(xconn, EDGE_LBL_PCI, &pcibr_vhdl) != GRAPH_SUCCESS)
	return 1;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    /* Disable the interrupts from the bridge */
    s = pcibr_lock(pcibr_soft);
    pcireg_intr_enable_set(pcibr_soft, 0);
    pcibr_unlock(pcibr_soft, s);

    /* Detach all the PCI devices talking to this bridge */
    for (slot = pcibr_soft->bs_min_slot; 
				slot < PCIBR_NUM_SLOTS(pcibr_soft); ++slot) {
	pcibr_slot_detach(pcibr_vhdl, slot, 0, (char *)NULL, (int *)NULL);
    }

    /* Unregister the no-slot connection point */
    pciio_device_info_unregister(pcibr_vhdl,
				 &(pcibr_soft->bs_noslot_info->f_c));

    kfree(pcibr_soft->bs_name);
    
    /* Disconnect the error interrupt and free the xtalk resources 
     * associated with it.
     */
    xtalk_intr_disconnect(pcibr_soft->bsi_err_intr);
    xtalk_intr_free(pcibr_soft->bsi_err_intr);

    /* Clear the software state maintained by the bridge driver for this
     * bridge.
     */
    kfree(pcibr_soft);

    /* Remove the Bridge revision labelled info */
    (void)hwgraph_info_remove_LBL(pcibr_vhdl, INFO_LBL_PCIBR_ASIC_REV, NULL);

    return 0;
}


/*
 * Set the Bridge's 32-bit PCI to XTalk Direct Map register to the most useful
 * value we can determine.  Note that we must use a single xid for all of:
 * 	-direct-mapped 32-bit DMA accesses
 *	-direct-mapped 64-bit DMA accesses
 * 	-DMA accesses through the PMU
 *	-interrupts
 * This is the only way to guarantee that completion interrupts will reach a
 * CPU after all DMA data has reached memory.
 */
void
pcibr_directmap_init(pcibr_soft_t pcibr_soft)
{
    paddr_t		paddr;
    iopaddr_t		xbase;
    uint64_t		diroff;
    cnodeid_t		cnodeid = 0;	/* We need api for diroff api */
    nasid_t		nasid;

    nasid = cnodeid_to_nasid(cnodeid);
    paddr = NODE_OFFSET(nasid) + 0;

    /* Assume that if we ask for a DMA mapping to zero the XIO host will
     * transmute this into a request for the lowest hunk of memory.
     */
    xbase = xtalk_dmatrans_addr(pcibr_soft->bs_conn, 0, paddr, PAGE_SIZE, 0);

    diroff = xbase >> BRIDGE_DIRMAP_OFF_ADDRSHFT;
    pcireg_dirmap_diroff_set(pcibr_soft, diroff);
    pcireg_dirmap_wid_set(pcibr_soft, pcibr_soft->bs_mxid);
    pcibr_soft->bs_dir_xport = pcibr_soft->bs_mxid;
    if (xbase  == (512 << 20)) { /* 512Meg */
	pcireg_dirmap_add512_set(pcibr_soft);
	pcibr_soft->bs_dir_xbase = (512 << 20);
    } else {
	pcireg_dirmap_add512_clr(pcibr_soft);
	pcibr_soft->bs_dir_xbase = diroff << BRIDGE_DIRMAP_OFF_ADDRSHFT;
    }
}


int
pcibr_asic_rev(vertex_hdl_t pconn_vhdl)
{
    vertex_hdl_t            pcibr_vhdl;
    int			    rc;
    arbitrary_info_t        ainfo;

    if (GRAPH_SUCCESS !=
	hwgraph_traverse(pconn_vhdl, EDGE_LBL_MASTER, &pcibr_vhdl))
	return -1;

    rc = hwgraph_info_get_LBL(pcibr_vhdl, INFO_LBL_PCIBR_ASIC_REV, &ainfo);

    /*
     * Any hwgraph function that returns a vertex handle will implicity
     * increment that vertex's reference count.  The caller must explicity
     * decrement the vertex's referece count after the last reference to
     * that vertex.
     *
     * Decrement reference count incremented by call to hwgraph_traverse().
     *
     */
    hwgraph_vertex_unref(pcibr_vhdl);

    if (rc != GRAPH_SUCCESS) 
	return -1;

    return (int) ainfo;
}

/* =====================================================================
 *    PIO MANAGEMENT
 */

static iopaddr_t
pcibr_addr_pci_to_xio(vertex_hdl_t pconn_vhdl,
		      pciio_slot_t slot,
		      pciio_space_t space,
		      iopaddr_t pci_addr,
		      size_t req_size,
		      unsigned flags)
{
    pcibr_info_t            pcibr_info = pcibr_info_get(pconn_vhdl);
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    unsigned                bar;	/* which BASE reg on device is decoding */
    iopaddr_t               xio_addr = XIO_NOWHERE;
    iopaddr_t               base = 0;
    iopaddr_t               limit = 0;

    pciio_space_t           wspace;	/* which space device is decoding */
    iopaddr_t               wbase;	/* base of device decode on PCI */
    size_t                  wsize;	/* size of device decode on PCI */

    int                     try;	/* DevIO(x) window scanning order control */
    int			    maxtry, halftry;
    int                     win;	/* which DevIO(x) window is being used */
    pciio_space_t           mspace;	/* target space for devio(x) register */
    iopaddr_t               mbase;	/* base of devio(x) mapped area on PCI */
    size_t                  msize;	/* size of devio(x) mapped area on PCI */
    size_t                  mmask;	/* addr bits stored in Device(x) */

    unsigned long           s;

    s = pcibr_lock(pcibr_soft);

    if (pcibr_soft->bs_slot[slot].has_host) {
	slot = pcibr_soft->bs_slot[slot].host_slot;
	pcibr_info = pcibr_soft->bs_slot[slot].bss_infos[0];

	/*
	 * Special case for dual-slot pci devices such as ioc3 on IP27
	 * baseio.  In these cases, pconn_vhdl should never be for a pci
	 * function on a subordiate PCI bus, so we can safely reset pciio_info
	 * to be the info struct embedded in pcibr_info.  Failure to do this
	 * results in using a bogus pciio_info_t for calculations done later
	 * in this routine.
	 */

	pciio_info = &pcibr_info->f_c;
    }
    if (space == PCIIO_SPACE_NONE)
	goto done;

    if (space == PCIIO_SPACE_CFG) {
	/*
	 * Usually, the first mapping
	 * established to a PCI device
	 * is to its config space.
	 *
	 * In any case, we definitely
	 * do NOT need to worry about
	 * PCI BASE registers, and
	 * MUST NOT attempt to point
	 * the DevIO(x) window at
	 * this access ...
	 */
	if (((flags & PCIIO_BYTE_STREAM) == 0) &&
	    ((pci_addr + req_size) <= BRIDGE_TYPE0_CFG_FUNC_OFF))
	    xio_addr = pci_addr + PCIBR_TYPE0_CFG_DEV(pcibr_soft, slot);

	goto done;
    }
    if (space == PCIIO_SPACE_ROM) {
	/* PIO to the Expansion Rom.
	 * Driver is responsible for
	 * enabling and disabling
	 * decodes properly.
	 */
	wbase = pciio_info->c_rbase;
	wsize = pciio_info->c_rsize;

	/*
	 * While the driver should know better
	 * than to attempt to map more space
	 * than the device is decoding, he might
	 * do it; better to bail out here.
	 */
	if ((pci_addr + req_size) > wsize)
	    goto done;

	pci_addr += wbase;
	space = PCIIO_SPACE_MEM;
    }
    /*
     * reduce window mappings to raw
     * space mappings (maybe allocating
     * windows), and try for DevIO(x)
     * usage (setting it if it is available).
     */
    bar = space - PCIIO_SPACE_WIN0;
    if (bar < 6) {
	wspace = pciio_info->c_window[bar].w_space;
	if (wspace == PCIIO_SPACE_NONE)
	    goto done;

	/* get PCI base and size */
	wbase = pciio_info->c_window[bar].w_base;
	wsize = pciio_info->c_window[bar].w_size;

	/*
	 * While the driver should know better
	 * than to attempt to map more space
	 * than the device is decoding, he might
	 * do it; better to bail out here.
	 */
	if ((pci_addr + req_size) > wsize)
	    goto done;

	/* shift from window relative to
	 * decoded space relative.
	 */
	pci_addr += wbase;
	space = wspace;
    } else
	bar = -1;

    /* Scan all the DevIO(x) windows twice looking for one
     * that can satisfy our request. The first time through,
     * only look at assigned windows; the second time, also
     * look at PCIIO_SPACE_NONE windows. Arrange the order
     * so we always look at our own window first.
     *
     * We will not attempt to satisfy a single request
     * by concatinating multiple windows.
     */
    maxtry = PCIBR_NUM_SLOTS(pcibr_soft) * 2;
    halftry = PCIBR_NUM_SLOTS(pcibr_soft) - 1;
    for (try = 0; try < maxtry; ++try) {
	uint64_t		devreg;
	unsigned                offset;

	/* calculate win based on slot, attempt, and max possible
	   devices on bus */
	win = (try + slot) % PCIBR_NUM_SLOTS(pcibr_soft);

	/* If this DevIO(x) mapping area can provide
	 * a mapping to this address, use it.
	 */
	msize = (win < 2) ? 0x200000 : 0x100000;
	mmask = -msize;
	if (space != PCIIO_SPACE_IO)
	    mmask &= 0x3FFFFFFF;

	offset = pci_addr & (msize - 1);

	/* If this window can't possibly handle that request,
	 * go on to the next window.
	 */
	if (((pci_addr & (msize - 1)) + req_size) > msize)
	    continue;

	devreg = pcibr_soft->bs_slot[win].bss_device;

	/* Is this window "nailed down"?
	 * If not, maybe we can use it.
	 * (only check this the second time through)
	 */
	mspace = pcibr_soft->bs_slot[win].bss_devio.bssd_space;
	if ((try > halftry) && (mspace == PCIIO_SPACE_NONE)) {

	    /* If this is the primary DevIO(x) window
	     * for some other device, skip it.
	     */
	    if ((win != slot) &&
		(PCIIO_VENDOR_ID_NONE !=
		 pcibr_soft->bs_slot[win].bss_vendor_id))
		continue;

	    /* It's a free window, and we fit in it.
	     * Set up Device(win) to our taste.
	     */
	    mbase = pci_addr & mmask;

	    /* check that we would really get from
	     * here to there.
	     */
	    if ((mbase | offset) != pci_addr)
		continue;

	    devreg &= ~BRIDGE_DEV_OFF_MASK;
	    if (space != PCIIO_SPACE_IO)
		devreg |= BRIDGE_DEV_DEV_IO_MEM;
	    else
		devreg &= ~BRIDGE_DEV_DEV_IO_MEM;
	    devreg |= (mbase >> 20) & BRIDGE_DEV_OFF_MASK;

	    /* default is WORD_VALUES.
	     * if you specify both,
	     * operation is undefined.
	     */
	    if (flags & PCIIO_BYTE_STREAM)
		devreg |= BRIDGE_DEV_DEV_SWAP;
	    else
		devreg &= ~BRIDGE_DEV_DEV_SWAP;

	    if (pcibr_soft->bs_slot[win].bss_device != devreg) {
		pcireg_device_set(pcibr_soft, win, devreg);
		pcibr_soft->bs_slot[win].bss_device = devreg;
		pcireg_tflush_get(pcibr_soft);	

		PCIBR_DEBUG((PCIBR_DEBUG_DEVREG, pconn_vhdl, 
			    "pcibr_addr_pci_to_xio: Device(%d): 0x%x\n",
			    win, devreg));
	    }
	    pcibr_soft->bs_slot[win].bss_devio.bssd_space = space;
	    pcibr_soft->bs_slot[win].bss_devio.bssd_base = mbase;
	    xio_addr = PCIBR_BRIDGE_DEVIO(pcibr_soft, win) + (pci_addr - mbase);

            /* Increment this DevIO's use count */
            pcibr_soft->bs_slot[win].bss_devio.bssd_ref_cnt++;

            /* Save the DevIO register index used to access this BAR */
            if (bar != -1)
                pcibr_info->f_window[bar].w_devio_index = win;

	    PCIBR_DEBUG((PCIBR_DEBUG_PIOMAP, pconn_vhdl,
		    "pcibr_addr_pci_to_xio: map to space %s [0x%lx..0x%lx] "
		    "for slot %d allocates DevIO(%d) Device(%d) set to %lx\n",
		    pci_space[space], pci_addr, pci_addr + req_size - 1,
		    slot, win, win, devreg));

	    goto done;
	}				/* endif DevIO(x) not pointed */
	mbase = pcibr_soft->bs_slot[win].bss_devio.bssd_base;

	/* Now check for request incompat with DevIO(x)
	 */
	if ((mspace != space) ||
	    (pci_addr < mbase) ||
	    ((pci_addr + req_size) > (mbase + msize)) ||
	    ((flags & PCIIO_BYTE_STREAM) && !(devreg & BRIDGE_DEV_DEV_SWAP)) ||
	    (!(flags & PCIIO_BYTE_STREAM) && (devreg & BRIDGE_DEV_DEV_SWAP)))
	    continue;

	/* DevIO(x) window is pointed at PCI space
	 * that includes our target. Calculate the
	 * final XIO address, release the lock and
	 * return.
	 */
	xio_addr = PCIBR_BRIDGE_DEVIO(pcibr_soft, win) + (pci_addr - mbase);

        /* Increment this DevIO's use count */
        pcibr_soft->bs_slot[win].bss_devio.bssd_ref_cnt++;

        /* Save the DevIO register index used to access this BAR */
        if (bar != -1)
            pcibr_info->f_window[bar].w_devio_index = win;

	PCIBR_DEBUG((PCIBR_DEBUG_PIOMAP, pconn_vhdl,
		"pcibr_addr_pci_to_xio: map to space %s [0x%lx..0x%lx] "
		"for slot %d uses DevIO(%d)\n", pci_space[space],
		pci_addr, pci_addr + req_size - 1, slot, win));
	goto done;
    }

    switch (space) {
	/*
	 * Accesses to device decode
	 * areas that do a not fit
	 * within the DevIO(x) space are
	 * modified to be accesses via
	 * the direct mapping areas.
	 *
	 * If necessary, drivers can
	 * explicitly ask for mappings
	 * into these address spaces,
	 * but this should never be needed.
	 */
    case PCIIO_SPACE_MEM:		/* "mem space" */
    case PCIIO_SPACE_MEM32:		/* "mem, use 32-bit-wide bus" */
	if (IS_PIC_BUSNUM_SOFT(pcibr_soft, 0)) {	/* PIC bus 0 */
		base = PICBRIDGE0_PCI_MEM32_BASE;
		limit = PICBRIDGE0_PCI_MEM32_LIMIT;
	} else if (IS_PIC_BUSNUM_SOFT(pcibr_soft, 1)) {	/* PIC bus 1 */
		base = PICBRIDGE1_PCI_MEM32_BASE;
		limit = PICBRIDGE1_PCI_MEM32_LIMIT;
	} else {
		printk("pcibr_addr_pci_to_xio(): unknown bridge type");
		return (iopaddr_t)0;
	}

	if ((pci_addr + base + req_size - 1) <= limit)
	    xio_addr = pci_addr + base;
	break;

    case PCIIO_SPACE_MEM64:		/* "mem, use 64-bit-wide bus" */
	if (IS_PIC_BUSNUM_SOFT(pcibr_soft, 0)) {	/* PIC bus 0 */
		base = PICBRIDGE0_PCI_MEM64_BASE;
		limit = PICBRIDGE0_PCI_MEM64_LIMIT;
	} else if (IS_PIC_BUSNUM_SOFT(pcibr_soft, 1)) {	/* PIC bus 1 */
		base = PICBRIDGE1_PCI_MEM64_BASE;
		limit = PICBRIDGE1_PCI_MEM64_LIMIT;
	} else {
		printk("pcibr_addr_pci_to_xio(): unknown bridge type");
		return (iopaddr_t)0;
	}

	if ((pci_addr + base + req_size - 1) <= limit)
	    xio_addr = pci_addr + base;
	break;

    case PCIIO_SPACE_IO:		/* "i/o space" */
	/*
	 * PIC bridges do not support big-window aliases into PCI I/O space
	 */
	xio_addr = XIO_NOWHERE;
	break;
    }

    /* Check that "Direct PIO" byteswapping matches,
     * try to change it if it does not.
     */
    if (xio_addr != XIO_NOWHERE) {
	unsigned                bst;	/* nonzero to set bytestream */
	unsigned               *bfp;	/* addr of record of how swapper is set */
	uint64_t		swb;	/* which control bit to mung */
	unsigned                bfo;	/* current swapper setting */
	unsigned                bfn;	/* desired swapper setting */

	bfp = ((space == PCIIO_SPACE_IO)
	       ? (&pcibr_soft->bs_pio_end_io)
	       : (&pcibr_soft->bs_pio_end_mem));

	bfo = *bfp;

	bst = flags & PCIIO_BYTE_STREAM;

	bfn = bst ? PCIIO_BYTE_STREAM : PCIIO_WORD_VALUES;

	if (bfn == bfo) {		/* we already match. */
	    ;
	} else if (bfo != 0) {		/* we have a conflict. */
	    PCIBR_DEBUG((PCIBR_DEBUG_PIOMAP, pconn_vhdl,
		    "pcibr_addr_pci_to_xio: swap conflict in %s, "
		    "was%s%s, want%s%s\n", pci_space[space],
		    bfo & PCIIO_BYTE_STREAM ? " BYTE_STREAM" : "",
		    bfo & PCIIO_WORD_VALUES ? " WORD_VALUES" : "",
		    bfn & PCIIO_BYTE_STREAM ? " BYTE_STREAM" : "",
		    bfn & PCIIO_WORD_VALUES ? " WORD_VALUES" : ""));
	    xio_addr = XIO_NOWHERE;
	} else {			/* OK to make the change. */
	    swb = (space == PCIIO_SPACE_IO) ? 0: BRIDGE_CTRL_MEM_SWAP;
	    if (bst) {
		pcireg_control_bit_set(pcibr_soft, swb);
	    } else {
		pcireg_control_bit_clr(pcibr_soft, swb);
	    }

	    *bfp = bfn;			/* record the assignment */

	    PCIBR_DEBUG((PCIBR_DEBUG_PIOMAP, pconn_vhdl,
		    "pcibr_addr_pci_to_xio: swap for %s set to%s%s\n",
		    pci_space[space],
		    bfn & PCIIO_BYTE_STREAM ? " BYTE_STREAM" : "",
		    bfn & PCIIO_WORD_VALUES ? " WORD_VALUES" : ""));
	}
    }
  done:
    pcibr_unlock(pcibr_soft, s);
    return xio_addr;
}

/*ARGSUSED6 */
pcibr_piomap_t
pcibr_piomap_alloc(vertex_hdl_t pconn_vhdl,
		   device_desc_t dev_desc,
		   pciio_space_t space,
		   iopaddr_t pci_addr,
		   size_t req_size,
		   size_t req_size_max,
		   unsigned flags)
{
    pcibr_info_t	    pcibr_info = pcibr_info_get(pconn_vhdl);
    pciio_info_t            pciio_info = &pcibr_info->f_c;
    pciio_slot_t            pciio_slot = PCIBR_INFO_SLOT_GET_INT(pciio_info);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    vertex_hdl_t            xconn_vhdl = pcibr_soft->bs_conn;

    pcibr_piomap_t         *mapptr;
    pcibr_piomap_t          maplist;
    pcibr_piomap_t          pcibr_piomap;
    iopaddr_t               xio_addr;
    xtalk_piomap_t          xtalk_piomap;
    unsigned long           s;

    /* Make sure that the req sizes are non-zero */
    if ((req_size < 1) || (req_size_max < 1)) {
	PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_PIOMAP, pconn_vhdl,
		    "pcibr_piomap_alloc: req_size | req_size_max < 1\n"));
	return NULL;
    }

    /*
     * Code to translate slot/space/addr
     * into xio_addr is common between
     * this routine and pcibr_piotrans_addr.
     */
    xio_addr = pcibr_addr_pci_to_xio(pconn_vhdl, pciio_slot, space, pci_addr, req_size, flags);

    if (xio_addr == XIO_NOWHERE) {
	PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_PIOMAP, pconn_vhdl,
		    "pcibr_piomap_alloc: xio_addr == XIO_NOWHERE\n"));
	return NULL;
    }

    /* Check the piomap list to see if there is already an allocated
     * piomap entry but not in use. If so use that one. Otherwise
     * allocate a new piomap entry and add it to the piomap list
     */
    mapptr = &(pcibr_info->f_piomap);

    s = pcibr_lock(pcibr_soft);
    for (pcibr_piomap = *mapptr;
	 pcibr_piomap != NULL;
	 pcibr_piomap = pcibr_piomap->bp_next) {
	if (pcibr_piomap->bp_mapsz == 0)
	    break;
    }

    if (pcibr_piomap)
	mapptr = NULL;
    else {
	pcibr_unlock(pcibr_soft, s);
	pcibr_piomap = kmalloc(sizeof (*(pcibr_piomap)), GFP_KERNEL);
	if ( !pcibr_piomap ) {
		PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_PIOMAP, pconn_vhdl,
		    	"pcibr_piomap_alloc: malloc fails\n"));
		return NULL;
	}
	memset(pcibr_piomap, 0, sizeof (*(pcibr_piomap)));
    }

    pcibr_piomap->bp_dev = pconn_vhdl;
    pcibr_piomap->bp_slot = PCIBR_DEVICE_TO_SLOT(pcibr_soft, pciio_slot);
    pcibr_piomap->bp_flags = flags;
    pcibr_piomap->bp_space = space;
    pcibr_piomap->bp_pciaddr = pci_addr;
    pcibr_piomap->bp_mapsz = req_size;
    pcibr_piomap->bp_soft = pcibr_soft;
    pcibr_piomap->bp_toc = ATOMIC_INIT(0);

    if (mapptr) {
	s = pcibr_lock(pcibr_soft);
	maplist = *mapptr;
	pcibr_piomap->bp_next = maplist;
	*mapptr = pcibr_piomap;
    }
    pcibr_unlock(pcibr_soft, s);


    if (pcibr_piomap) {
	xtalk_piomap =
	    xtalk_piomap_alloc(xconn_vhdl, 0,
			       xio_addr,
			       req_size, req_size_max,
			       flags & PIOMAP_FLAGS);
	if (xtalk_piomap) {
	    pcibr_piomap->bp_xtalk_addr = xio_addr;
	    pcibr_piomap->bp_xtalk_pio = xtalk_piomap;
	} else {
	    pcibr_piomap->bp_mapsz = 0;
	    pcibr_piomap = 0;
	}
    }
    
    PCIBR_DEBUG((PCIBR_DEBUG_PIOMAP, pconn_vhdl,
		"pcibr_piomap_alloc: map=0x%lx\n", pcibr_piomap));

    return pcibr_piomap;
}

/*ARGSUSED */
void
pcibr_piomap_free(pcibr_piomap_t pcibr_piomap)
{
    PCIBR_DEBUG((PCIBR_DEBUG_PIOMAP, pcibr_piomap->bp_dev,
		"pcibr_piomap_free: map=0x%lx\n", pcibr_piomap));

    xtalk_piomap_free(pcibr_piomap->bp_xtalk_pio);
    pcibr_piomap->bp_xtalk_pio = 0;
    pcibr_piomap->bp_mapsz = 0;
}

/*ARGSUSED */
caddr_t
pcibr_piomap_addr(pcibr_piomap_t pcibr_piomap,
		  iopaddr_t pci_addr,
		  size_t req_size)
{
    caddr_t	addr;
    addr = xtalk_piomap_addr(pcibr_piomap->bp_xtalk_pio,
			     pcibr_piomap->bp_xtalk_addr +
			     pci_addr - pcibr_piomap->bp_pciaddr,
			     req_size);
    PCIBR_DEBUG((PCIBR_DEBUG_PIOMAP, pcibr_piomap->bp_dev,
                "pcibr_piomap_addr: map=0x%lx, addr=0x%lx\n", 
		pcibr_piomap, addr));

    return addr;
}

/*ARGSUSED */
void
pcibr_piomap_done(pcibr_piomap_t pcibr_piomap)
{
    PCIBR_DEBUG((PCIBR_DEBUG_PIOMAP, pcibr_piomap->bp_dev,
		"pcibr_piomap_done: map=0x%lx\n", pcibr_piomap));
    xtalk_piomap_done(pcibr_piomap->bp_xtalk_pio);
}

/*ARGSUSED */
caddr_t
pcibr_piotrans_addr(vertex_hdl_t pconn_vhdl,
		    device_desc_t dev_desc,
		    pciio_space_t space,
		    iopaddr_t pci_addr,
		    size_t req_size,
		    unsigned flags)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pciio_slot_t            pciio_slot = PCIBR_INFO_SLOT_GET_INT(pciio_info);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    vertex_hdl_t            xconn_vhdl = pcibr_soft->bs_conn;

    iopaddr_t               xio_addr;
    caddr_t		    addr;

    xio_addr = pcibr_addr_pci_to_xio(pconn_vhdl, pciio_slot, space, pci_addr, req_size, flags);

    if (xio_addr == XIO_NOWHERE) {
	PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_PIODIR, pconn_vhdl,
		    "pcibr_piotrans_addr: xio_addr == XIO_NOWHERE\n"));
	return NULL;
    }

    addr = xtalk_piotrans_addr(xconn_vhdl, 0, xio_addr, req_size, flags & PIOMAP_FLAGS);
    PCIBR_DEBUG((PCIBR_DEBUG_PIODIR, pconn_vhdl,
		"pcibr_piotrans_addr: xio_addr=0x%lx, addr=0x%lx\n",
		xio_addr, addr));
    return addr;
}

/*
 * PIO Space allocation and management.
 *      Allocate and Manage the PCI PIO space (mem and io space)
 *      This routine is pretty simplistic at this time, and
 *      does pretty trivial management of allocation and freeing.
 *      The current scheme is prone for fragmentation.
 *      Change the scheme to use bitmaps.
 */

/*ARGSUSED */
iopaddr_t
pcibr_piospace_alloc(vertex_hdl_t pconn_vhdl,
		     device_desc_t dev_desc,
		     pciio_space_t space,
		     size_t req_size,
		     size_t alignment)
{
    pcibr_info_t            pcibr_info = pcibr_info_get(pconn_vhdl);
    pciio_info_t            pciio_info = &pcibr_info->f_c;
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);

    pciio_piospace_t        piosp;
    unsigned long           s;

    iopaddr_t               start_addr;
    size_t                  align_mask;

    /*
     * Check for proper alignment
     */
    ASSERT(alignment >= PAGE_SIZE);
    ASSERT((alignment & (alignment - 1)) == 0);

    align_mask = alignment - 1;
    s = pcibr_lock(pcibr_soft);

    /*
     * First look if a previously allocated chunk exists.
     */
    piosp = pcibr_info->f_piospace;
    if (piosp) {
	/*
	 * Look through the list for a right sized free chunk.
	 */
	do {
	    if (piosp->free &&
		(piosp->space == space) &&
		(piosp->count >= req_size) &&
		!(piosp->start & align_mask)) {
		piosp->free = 0;
		pcibr_unlock(pcibr_soft, s);
		return piosp->start;
	    }
	    piosp = piosp->next;
	} while (piosp);
    }
    ASSERT(!piosp);

    /*
     * Allocate PCI bus address, usually for the Universe chip driver;
     * do not pass window info since the actual PCI bus address
     * space will never be freed.  The space may be reused after it
     * is logically released by pcibr_piospace_free().
     */
    switch (space) {
    case PCIIO_SPACE_IO:
        start_addr = pcibr_bus_addr_alloc(pcibr_soft, NULL,
                                          PCIIO_SPACE_IO,
                                          0, req_size, alignment);
	break;

    case PCIIO_SPACE_MEM:
    case PCIIO_SPACE_MEM32:
        start_addr = pcibr_bus_addr_alloc(pcibr_soft, NULL,
                                          PCIIO_SPACE_MEM32,
                                          0, req_size, alignment);
	break;

    default:
	ASSERT(0);
	pcibr_unlock(pcibr_soft, s);
	PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_PIOMAP, pconn_vhdl,
		    "pcibr_piospace_alloc: unknown space %d\n", space));
	return 0;
    }

    /*
     * If too big a request, reject it.
     */
    if (!start_addr) {
	pcibr_unlock(pcibr_soft, s);
	PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_PIOMAP, pconn_vhdl,
		    "pcibr_piospace_alloc: request 0x%lx to big\n", req_size));
	return 0;
    }

    piosp = kmalloc(sizeof (*(piosp)), GFP_KERNEL);
    if ( !piosp ) {
	PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_PIOMAP, pconn_vhdl,
		    "pcibr_piospace_alloc: malloc fails\n"));
	return 0;
    }
    memset(piosp, 0, sizeof (*(piosp)));

    piosp->free = 0;
    piosp->space = space;
    piosp->start = start_addr;
    piosp->count = req_size;
    piosp->next = pcibr_info->f_piospace;
    pcibr_info->f_piospace = piosp;

    pcibr_unlock(pcibr_soft, s);

    PCIBR_DEBUG((PCIBR_DEBUG_PIOMAP, pconn_vhdl,
		"pcibr_piospace_alloc: piosp=0x%lx\n", piosp));

    return start_addr;
}

#define ERR_MSG "!Device %s freeing size (0x%lx) different than allocated (0x%lx)"
/*ARGSUSED */
void
pcibr_piospace_free(vertex_hdl_t pconn_vhdl,
		    pciio_space_t space,
		    iopaddr_t pciaddr,
		    size_t req_size)
{
    pcibr_info_t            pcibr_info = pcibr_info_get(pconn_vhdl);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pcibr_info->f_mfast;
    pciio_piospace_t        piosp;
    unsigned long           s;
    char                    name[1024];

    /*
     * Look through the bridge data structures for the pciio_piospace_t
     * structure corresponding to  'pciaddr'
     */
    s = pcibr_lock(pcibr_soft);
    piosp = pcibr_info->f_piospace;
    while (piosp) {
	/*
	 * Piospace free can only be for the complete
	 * chunk and not parts of it..
	 */
	if (piosp->start == pciaddr) {
	    if (piosp->count == req_size)
		break;
	    /*
	     * Improper size passed for freeing..
	     * Print a message and break;
	     */
	    hwgraph_vertex_name_get(pconn_vhdl, name, 1024);
	    printk(KERN_WARNING  "pcibr_piospace_free: error");
	    printk(KERN_WARNING  "Device %s freeing size (0x%lx) different than allocated (0x%lx)",
					name, req_size, piosp->count);
	    printk(KERN_WARNING  "Freeing 0x%lx instead", piosp->count);
	    break;
	}
	piosp = piosp->next;
    }

    if (!piosp) {
	printk(KERN_WARNING  
		"pcibr_piospace_free: Address 0x%lx size 0x%lx - No match\n",
		pciaddr, req_size);
	pcibr_unlock(pcibr_soft, s);
	return;
    }
    piosp->free = 1;
    pcibr_unlock(pcibr_soft, s);

    PCIBR_DEBUG((PCIBR_DEBUG_PIOMAP, pconn_vhdl,
		"pcibr_piospace_free: piosp=0x%lx\n", piosp));
    return;
}

/* =====================================================================
 *    DMA MANAGEMENT
 *
 *      The Bridge ASIC provides three methods of doing
 *      DMA: via a "direct map" register available in
 *      32-bit PCI space (which selects a contiguous 2G
 *      address space on some other widget), via
 *      "direct" addressing via 64-bit PCI space (all
 *      destination information comes from the PCI
 *      address, including transfer attributes), and via
 *      a "mapped" region that allows a bunch of
 *      different small mappings to be established with
 *      the PMU.
 *
 *      For efficiency, we most prefer to use the 32-bit
 *      direct mapping facility, since it requires no
 *      resource allocations. The advantage of using the
 *      PMU over the 64-bit direct is that single-cycle
 *      PCI addressing can be used; the advantage of
 *      using 64-bit direct over PMU addressing is that
 *      we do not have to allocate entries in the PMU.
 */

/*
 * Convert PCI-generic software flags and Bridge-specific software flags
 * into Bridge-specific Direct Map attribute bits.
 */
static iopaddr_t
pcibr_flags_to_d64(unsigned flags, pcibr_soft_t pcibr_soft)
{
    iopaddr_t               attributes = 0;

    /* Sanity check: Bridge only allows use of VCHAN1 via 64-bit addrs */
#ifdef LATER
    ASSERT_ALWAYS(!(flags & PCIBR_VCHAN1) || (flags & PCIIO_DMA_A64));
#endif

    /* Generic macro flags
     */
    if (flags & PCIIO_DMA_DATA) {	/* standard data channel */
	attributes &= ~PCI64_ATTR_BAR;	/* no barrier bit */
	attributes |= PCI64_ATTR_PREF;	/* prefetch on */
    }
    if (flags & PCIIO_DMA_CMD) {	/* standard command channel */
	attributes |= PCI64_ATTR_BAR;	/* barrier bit on */
	attributes &= ~PCI64_ATTR_PREF;	/* disable prefetch */
    }
    /* Generic detail flags
     */
    if (flags & PCIIO_PREFETCH)
	attributes |= PCI64_ATTR_PREF;
    if (flags & PCIIO_NOPREFETCH)
	attributes &= ~PCI64_ATTR_PREF;

    /* the swap bit is in the address attributes for xbridge */
    if (flags & PCIIO_BYTE_STREAM)
       	attributes |= PCI64_ATTR_SWAP;
    if (flags & PCIIO_WORD_VALUES)
       	attributes &= ~PCI64_ATTR_SWAP;

    /* Provider-specific flags
     */
    if (flags & PCIBR_BARRIER)
	attributes |= PCI64_ATTR_BAR;
    if (flags & PCIBR_NOBARRIER)
	attributes &= ~PCI64_ATTR_BAR;

    if (flags & PCIBR_PREFETCH)
	attributes |= PCI64_ATTR_PREF;
    if (flags & PCIBR_NOPREFETCH)
	attributes &= ~PCI64_ATTR_PREF;

    if (flags & PCIBR_PRECISE)
	attributes |= PCI64_ATTR_PREC;
    if (flags & PCIBR_NOPRECISE)
	attributes &= ~PCI64_ATTR_PREC;

    if (flags & PCIBR_VCHAN1)
	attributes |= PCI64_ATTR_VIRTUAL;
    if (flags & PCIBR_VCHAN0)
	attributes &= ~PCI64_ATTR_VIRTUAL;

    /* PIC in PCI-X mode only supports barrier & swap */
    if (IS_PCIX(pcibr_soft)) {
	attributes &= (PCI64_ATTR_BAR | PCI64_ATTR_SWAP);
    }

    return attributes;
}

/*ARGSUSED */
pcibr_dmamap_t
pcibr_dmamap_alloc(vertex_hdl_t pconn_vhdl,
		   device_desc_t dev_desc,
		   size_t req_size_max,
		   unsigned flags)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    vertex_hdl_t            xconn_vhdl = pcibr_soft->bs_conn;
    pciio_slot_t            slot;
    xwidgetnum_t            xio_port;

    xtalk_dmamap_t          xtalk_dmamap;
    pcibr_dmamap_t          pcibr_dmamap;
    int                     ate_count;
    int                     ate_index;
    int			    vchan = VCHAN0;
    unsigned long	    s;

    /* merge in forced flags */
    flags |= pcibr_soft->bs_dma_flags;

    /*
     * On SNIA64, these maps are pre-allocated because pcibr_dmamap_alloc()
     * can be called within an interrupt thread.
     */
    s = pcibr_lock(pcibr_soft);
    pcibr_dmamap = (pcibr_dmamap_t)get_free_pciio_dmamap(pcibr_soft->bs_vhdl);
    pcibr_unlock(pcibr_soft, s);

    if (!pcibr_dmamap)
	return 0;

    xtalk_dmamap = xtalk_dmamap_alloc(xconn_vhdl, dev_desc, req_size_max,
				      flags & DMAMAP_FLAGS);
    if (!xtalk_dmamap) {
	PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_DMAMAP, pconn_vhdl,
		    "pcibr_dmamap_alloc: xtalk_dmamap_alloc failed\n"));
	free_pciio_dmamap(pcibr_dmamap);
	return 0;
    }
    xio_port = pcibr_soft->bs_mxid;
    slot = PCIBR_INFO_SLOT_GET_INT(pciio_info);

    pcibr_dmamap->bd_dev = pconn_vhdl;
    pcibr_dmamap->bd_slot = PCIBR_DEVICE_TO_SLOT(pcibr_soft, slot);
    pcibr_dmamap->bd_soft = pcibr_soft;
    pcibr_dmamap->bd_xtalk = xtalk_dmamap;
    pcibr_dmamap->bd_max_size = req_size_max;
    pcibr_dmamap->bd_xio_port = xio_port;

    if (flags & PCIIO_DMA_A64) {
	if (!pcibr_try_set_device(pcibr_soft, slot, flags, BRIDGE_DEV_D64_BITS)) {
	    iopaddr_t               pci_addr;
	    int                     have_rrbs;
	    int                     min_rrbs;

	    /* Device is capable of A64 operations,
	     * and the attributes of the DMA are
	     * consistent with any previous DMA
	     * mappings using shared resources.
	     */

	    pci_addr = pcibr_flags_to_d64(flags, pcibr_soft);

	    pcibr_dmamap->bd_flags = flags;
	    pcibr_dmamap->bd_xio_addr = 0;
	    pcibr_dmamap->bd_pci_addr = pci_addr;

	    /* If in PCI mode, make sure we have an RRB (or two). 
	     */
	    if (IS_PCI(pcibr_soft) && 
		!(pcibr_soft->bs_rrb_fixed & (1 << slot))) {
		if (flags & PCIBR_VCHAN1)
		    vchan = VCHAN1;
		have_rrbs = pcibr_soft->bs_rrb_valid[slot][vchan];
		if (have_rrbs < 2) {
		    if (pci_addr & PCI64_ATTR_PREF)
			min_rrbs = 2;
		    else
			min_rrbs = 1;
		    if (have_rrbs < min_rrbs)
			pcibr_rrb_alloc_more(pcibr_soft, slot, vchan,
					       min_rrbs - have_rrbs);
		}
	    }
	    PCIBR_DEBUG((PCIBR_DEBUG_DMAMAP | PCIBR_DEBUG_DMADIR, pconn_vhdl,
		 	"pcibr_dmamap_alloc: using direct64, map=0x%lx\n",
			pcibr_dmamap));
	    return pcibr_dmamap;
	}
	PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_DMAMAP | PCIBR_DEBUG_DMADIR, pconn_vhdl,
		    "pcibr_dmamap_alloc: unable to use direct64\n"));

	/* PIC in PCI-X mode only supports 64-bit direct mapping so
	 * don't fall thru and try 32-bit direct mapping or 32-bit
	 * page mapping
	 */
	if (IS_PCIX(pcibr_soft)) {
	    kfree(pcibr_dmamap);
	    return 0;
	}

	flags &= ~PCIIO_DMA_A64;
    }
    if (flags & PCIIO_FIXED) {
	/* warning: mappings may fail later,
	 * if direct32 can't get to the address.
	 */
	if (!pcibr_try_set_device(pcibr_soft, slot, flags, BRIDGE_DEV_D32_BITS)) {
	    /* User desires DIRECT A32 operations,
	     * and the attributes of the DMA are
	     * consistent with any previous DMA
	     * mappings using shared resources.
	     * Mapping calls may fail if target
	     * is outside the direct32 range.
	     */
	    PCIBR_DEBUG((PCIBR_DEBUG_DMAMAP | PCIBR_DEBUG_DMADIR, pconn_vhdl,
			"pcibr_dmamap_alloc: using direct32, map=0x%lx\n", 
			pcibr_dmamap));
	    pcibr_dmamap->bd_flags = flags;
	    pcibr_dmamap->bd_xio_addr = pcibr_soft->bs_dir_xbase;
	    pcibr_dmamap->bd_pci_addr = PCI32_DIRECT_BASE;
	    return pcibr_dmamap;
	}
	PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_DMAMAP | PCIBR_DEBUG_DMADIR, pconn_vhdl,
		    "pcibr_dmamap_alloc: unable to use direct32\n"));

	/* If the user demands FIXED and we can't
	 * give it to him, fail.
	 */
	xtalk_dmamap_free(xtalk_dmamap);
	free_pciio_dmamap(pcibr_dmamap);
	return 0;
    }
    /*
     * Allocate Address Translation Entries from the mapping RAM.
     * Unless the PCIBR_NO_ATE_ROUNDUP flag is specified,
     * the maximum number of ATEs is based on the worst-case
     * scenario, where the requested target is in the
     * last byte of an ATE; thus, mapping IOPGSIZE+2
     * does end up requiring three ATEs.
     */
    if (!(flags & PCIBR_NO_ATE_ROUNDUP)) {
	ate_count = IOPG((IOPGSIZE - 1)	/* worst case start offset */
		     +req_size_max	/* max mapping bytes */
		     - 1) + 1;		/* round UP */
    } else {	/* assume requested target is page aligned */
	ate_count = IOPG(req_size_max   /* max mapping bytes */
		     - 1) + 1;		/* round UP */
    }

    ate_index = pcibr_ate_alloc(pcibr_soft, ate_count, &pcibr_dmamap->resource);

    if (ate_index != -1) {
	if (!pcibr_try_set_device(pcibr_soft, slot, flags, BRIDGE_DEV_PMU_BITS)) {
	    bridge_ate_t            ate_proto;
	    int                     have_rrbs;
	    int                     min_rrbs;

	    PCIBR_DEBUG((PCIBR_DEBUG_DMAMAP, pconn_vhdl,
			"pcibr_dmamap_alloc: using PMU, ate_index=%d, "
			"pcibr_dmamap=0x%lx\n", ate_index, pcibr_dmamap));

	    ate_proto = pcibr_flags_to_ate(pcibr_soft, flags);

	    pcibr_dmamap->bd_flags = flags;
	    pcibr_dmamap->bd_pci_addr =
		PCI32_MAPPED_BASE + IOPGSIZE * ate_index;

	    if (flags & PCIIO_BYTE_STREAM)
		    ATE_SWAP_ON(pcibr_dmamap->bd_pci_addr);
	    /*
	     * If swap was set in bss_device in pcibr_endian_set()
	     * we need to change the address bit.
	     */
	    if (pcibr_soft->bs_slot[slot].bss_device & 
						BRIDGE_DEV_SWAP_PMU)
		    ATE_SWAP_ON(pcibr_dmamap->bd_pci_addr);
	    if (flags & PCIIO_WORD_VALUES)
		    ATE_SWAP_OFF(pcibr_dmamap->bd_pci_addr);
	    pcibr_dmamap->bd_xio_addr = 0;
	    pcibr_dmamap->bd_ate_ptr = pcibr_ate_addr(pcibr_soft, ate_index);
	    pcibr_dmamap->bd_ate_index = ate_index;
	    pcibr_dmamap->bd_ate_count = ate_count;
	    pcibr_dmamap->bd_ate_proto = ate_proto;

	    /* Make sure we have an RRB (or two).
	     */
	    if (!(pcibr_soft->bs_rrb_fixed & (1 << slot))) {
		have_rrbs = pcibr_soft->bs_rrb_valid[slot][vchan];
		if (have_rrbs < 2) {
		    if (ate_proto & ATE_PREF)
			min_rrbs = 2;
		    else
			min_rrbs = 1;
		    if (have_rrbs < min_rrbs)
			pcibr_rrb_alloc_more(pcibr_soft, slot, vchan,
					       min_rrbs - have_rrbs);
		}
	    }
	    return pcibr_dmamap;
	}
	PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_DMAMAP, pconn_vhdl,
		    "pcibr_dmamap_alloc: PMU use failed, ate_index=%d\n",
		    ate_index));

	pcibr_ate_free(pcibr_soft, ate_index, ate_count, &pcibr_dmamap->resource);
    }
    /* total failure: sorry, you just can't
     * get from here to there that way.
     */
    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_DMAMAP, pconn_vhdl,
		"pcibr_dmamap_alloc: complete failure.\n"));
    xtalk_dmamap_free(xtalk_dmamap);
    free_pciio_dmamap(pcibr_dmamap);
    return 0;
}

/*ARGSUSED */
void
pcibr_dmamap_free(pcibr_dmamap_t pcibr_dmamap)
{
    pcibr_soft_t            pcibr_soft = pcibr_dmamap->bd_soft;
    pciio_slot_t            slot = PCIBR_SLOT_TO_DEVICE(pcibr_soft,
							pcibr_dmamap->bd_slot);

    xtalk_dmamap_free(pcibr_dmamap->bd_xtalk);

    if (pcibr_dmamap->bd_flags & PCIIO_DMA_A64) {
	pcibr_release_device(pcibr_soft, slot, BRIDGE_DEV_D64_BITS);
    }
    if (pcibr_dmamap->bd_ate_count) {
	pcibr_ate_free(pcibr_dmamap->bd_soft,
		       pcibr_dmamap->bd_ate_index,
		       pcibr_dmamap->bd_ate_count,
		       &pcibr_dmamap->resource);
	pcibr_release_device(pcibr_soft, slot, XBRIDGE_DEV_PMU_BITS);
    }

    PCIBR_DEBUG((PCIBR_DEBUG_DMAMAP, pcibr_dmamap->bd_dev,
		"pcibr_dmamap_free: pcibr_dmamap=0x%lx\n", pcibr_dmamap));

    free_pciio_dmamap(pcibr_dmamap);
}

/*
 *    pcibr_addr_xio_to_pci: given a PIO range, hand
 *      back the corresponding base PCI MEM address;
 *      this is used to short-circuit DMA requests that
 *      loop back onto this PCI bus.
 */
static iopaddr_t
pcibr_addr_xio_to_pci(pcibr_soft_t soft,
		      iopaddr_t xio_addr,
		      size_t req_size)
{
    iopaddr_t               xio_lim = xio_addr + req_size - 1;
    iopaddr_t               pci_addr;
    pciio_slot_t            slot;

    if (IS_PIC_BUSNUM_SOFT(soft, 0)) {
    	if ((xio_addr >= PICBRIDGE0_PCI_MEM32_BASE) &&
	    (xio_lim <= PICBRIDGE0_PCI_MEM32_LIMIT)) {
	    pci_addr = xio_addr - PICBRIDGE0_PCI_MEM32_BASE;
	    return pci_addr;
    	}
    	if ((xio_addr >= PICBRIDGE0_PCI_MEM64_BASE) &&
	    (xio_lim <= PICBRIDGE0_PCI_MEM64_LIMIT)) {
	    pci_addr = xio_addr - PICBRIDGE0_PCI_MEM64_BASE;
	    return pci_addr;
    	}
    } else if (IS_PIC_BUSNUM_SOFT(soft, 1)) {
    	if ((xio_addr >= PICBRIDGE1_PCI_MEM32_BASE) &&
	    (xio_lim <= PICBRIDGE1_PCI_MEM32_LIMIT)) {
	    pci_addr = xio_addr - PICBRIDGE1_PCI_MEM32_BASE;
	    return pci_addr;
    	}
    	if ((xio_addr >= PICBRIDGE1_PCI_MEM64_BASE) &&
	    (xio_lim <= PICBRIDGE1_PCI_MEM64_LIMIT)) {
	    pci_addr = xio_addr - PICBRIDGE1_PCI_MEM64_BASE;
	    return pci_addr;
    	}
    } else {
	printk("pcibr_addr_xio_to_pci(): unknown bridge type");
	return (iopaddr_t)0;
    }
    for (slot = soft->bs_min_slot; slot < PCIBR_NUM_SLOTS(soft); ++slot)
	if ((xio_addr >= PCIBR_BRIDGE_DEVIO(soft, slot)) &&
	    (xio_lim < PCIBR_BRIDGE_DEVIO(soft, slot + 1))) {
	    uint64_t		dev;

	    dev = soft->bs_slot[slot].bss_device;
	    pci_addr = dev & BRIDGE_DEV_OFF_MASK;
	    pci_addr <<= BRIDGE_DEV_OFF_ADDR_SHFT;
	    pci_addr += xio_addr - PCIBR_BRIDGE_DEVIO(soft, slot);
	    return (dev & BRIDGE_DEV_DEV_IO_MEM) ? pci_addr : PCI_NOWHERE;
	}
    return 0;
}

/*ARGSUSED */
iopaddr_t
pcibr_dmamap_addr(pcibr_dmamap_t pcibr_dmamap,
		  paddr_t paddr,
		  size_t req_size)
{
    pcibr_soft_t            pcibr_soft;
    iopaddr_t               xio_addr;
    xwidgetnum_t            xio_port;
    iopaddr_t               pci_addr;
    unsigned                flags;

    ASSERT(pcibr_dmamap != NULL);
    ASSERT(req_size > 0);
    ASSERT(req_size <= pcibr_dmamap->bd_max_size);

    pcibr_soft = pcibr_dmamap->bd_soft;

    flags = pcibr_dmamap->bd_flags;

    xio_addr = xtalk_dmamap_addr(pcibr_dmamap->bd_xtalk, paddr, req_size);
    if (XIO_PACKED(xio_addr)) {
	xio_port = XIO_PORT(xio_addr);
	xio_addr = XIO_ADDR(xio_addr);
    } else
	xio_port = pcibr_dmamap->bd_xio_port;

    /* If this DMA is to an address that
     * refers back to this Bridge chip,
     * reduce it back to the correct
     * PCI MEM address.
     */
    if (xio_port == pcibr_soft->bs_xid) {
	pci_addr = pcibr_addr_xio_to_pci(pcibr_soft, xio_addr, req_size);
    } else if (flags & PCIIO_DMA_A64) {
	/* A64 DMA:
	 * always use 64-bit direct mapping,
	 * which always works.
	 * Device(x) was set up during
	 * dmamap allocation.
	 */

	/* attributes are already bundled up into bd_pci_addr.
	 */
	pci_addr = pcibr_dmamap->bd_pci_addr
	    | ((uint64_t) xio_port << PCI64_ATTR_TARG_SHFT)
	    | xio_addr;

	/* Bridge Hardware WAR #482836:
	 * If the transfer is not cache aligned
	 * and the Bridge Rev is <= B, force
	 * prefetch to be off.
	 */
	if (flags & PCIBR_NOPREFETCH)
	    pci_addr &= ~PCI64_ATTR_PREF;

	PCIBR_DEBUG((PCIBR_DEBUG_DMAMAP | PCIBR_DEBUG_DMADIR, 
		    pcibr_dmamap->bd_dev,
		    "pcibr_dmamap_addr: (direct64): wanted paddr [0x%lx..0x%lx] "
		    "XIO port 0x%x offset 0x%lx, returning PCI 0x%lx\n",
		    paddr, paddr + req_size - 1, xio_port, xio_addr, pci_addr));

    } else if (flags & PCIIO_FIXED) {
	/* A32 direct DMA:
	 * always use 32-bit direct mapping,
	 * which may fail.
	 * Device(x) was set up during
	 * dmamap allocation.
	 */

	if (xio_port != pcibr_soft->bs_dir_xport)
	    pci_addr = 0;		/* wrong DIDN */
	else if (xio_addr < pcibr_dmamap->bd_xio_addr)
	    pci_addr = 0;		/* out of range */
	else if ((xio_addr + req_size) >
		 (pcibr_dmamap->bd_xio_addr + BRIDGE_DMA_DIRECT_SIZE))
	    pci_addr = 0;		/* out of range */
	else
	    pci_addr = pcibr_dmamap->bd_pci_addr +
		xio_addr - pcibr_dmamap->bd_xio_addr;

	PCIBR_DEBUG((PCIBR_DEBUG_DMAMAP | PCIBR_DEBUG_DMADIR, 
		    pcibr_dmamap->bd_dev,
		    "pcibr_dmamap_addr (direct32): wanted paddr [0x%lx..0x%lx] "
		    "XIO port 0x%x offset 0x%lx, returning PCI 0x%lx\n",
		    paddr, paddr + req_size - 1, xio_port, xio_addr, pci_addr));

    } else {
	iopaddr_t               offset = IOPGOFF(xio_addr);
	bridge_ate_t            ate_proto = pcibr_dmamap->bd_ate_proto;
	int                     ate_count = IOPG(offset + req_size - 1) + 1;
	int                     ate_index = pcibr_dmamap->bd_ate_index;
	bridge_ate_t            ate;

	ate = ate_proto | (xio_addr - offset);
	ate |= (xio_port << ATE_TIDSHIFT);

	pci_addr = pcibr_dmamap->bd_pci_addr + offset;

	/* Fill in our mapping registers
	 * with the appropriate xtalk data,
	 * and hand back the PCI address.
	 */

	ASSERT(ate_count > 0);
	if (ate_count <= pcibr_dmamap->bd_ate_count) {
		ate_write(pcibr_soft, ate_index, ate_count, ate);

		PCIBR_DEBUG((PCIBR_DEBUG_DMAMAP, pcibr_dmamap->bd_dev,
			    "pcibr_dmamap_addr (PMU) : wanted paddr "
			    "[0x%lx..0x%lx] returning PCI 0x%lx\n", 
			    paddr, paddr + req_size - 1, pci_addr));

	} else {
		/* The number of ATE's required is greater than the number
		 * allocated for this map. One way this can happen is if
		 * pcibr_dmamap_alloc() was called with the PCIBR_NO_ATE_ROUNDUP
		 * flag, and then when that map is used (right now), the
		 * target address tells us we really did need to roundup.
		 * The other possibility is that the map is just plain too
		 * small to handle the requested target area.
		 */
		PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_DMAMAP, pcibr_dmamap->bd_dev, 
		            "pcibr_dmamap_addr (PMU) : wanted paddr "
			    "[0x%lx..0x%lx] ate_count 0x%x bd_ate_count 0x%x "
			    "ATE's required > number allocated\n",
			     paddr, paddr + req_size - 1,
			     ate_count, pcibr_dmamap->bd_ate_count));
		pci_addr = 0;
	}

    }
    return pci_addr;
}

/*ARGSUSED */
void
pcibr_dmamap_done(pcibr_dmamap_t pcibr_dmamap)
{
    xtalk_dmamap_done(pcibr_dmamap->bd_xtalk);

    PCIBR_DEBUG((PCIBR_DEBUG_DMAMAP, pcibr_dmamap->bd_dev,
		"pcibr_dmamap_done: pcibr_dmamap=0x%lx\n", pcibr_dmamap));
}


/*
 * For each bridge, the DIR_OFF value in the Direct Mapping Register
 * determines the PCI to Crosstalk memory mapping to be used for all
 * 32-bit Direct Mapping memory accesses. This mapping can be to any
 * node in the system. This function will return that compact node id.
 */

/*ARGSUSED */
cnodeid_t
pcibr_get_dmatrans_node(vertex_hdl_t pconn_vhdl)
{

	pciio_info_t	pciio_info = pciio_info_get(pconn_vhdl);
	pcibr_soft_t	pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);

	return nasid_to_cnodeid(NASID_GET(pcibr_soft->bs_dir_xbase));
}

/*ARGSUSED */
iopaddr_t
pcibr_dmatrans_addr(vertex_hdl_t pconn_vhdl,
		    device_desc_t dev_desc,
		    paddr_t paddr,
		    size_t req_size,
		    unsigned flags)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    vertex_hdl_t            xconn_vhdl = pcibr_soft->bs_conn;
    pciio_slot_t            pciio_slot = PCIBR_INFO_SLOT_GET_INT(pciio_info);
    pcibr_soft_slot_t       slotp = &pcibr_soft->bs_slot[pciio_slot];

    xwidgetnum_t            xio_port;
    iopaddr_t               xio_addr;
    iopaddr_t               pci_addr;

    int                     have_rrbs;
    int                     min_rrbs;
    int			    vchan = VCHAN0;

    /* merge in forced flags */
    flags |= pcibr_soft->bs_dma_flags;

    xio_addr = xtalk_dmatrans_addr(xconn_vhdl, 0, paddr, req_size,
				   flags & DMAMAP_FLAGS);
    if (!xio_addr) {
	PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_DMADIR, pconn_vhdl,
		    "pcibr_dmatrans_addr: wanted paddr [0x%lx..0x%lx], "
		    "xtalk_dmatrans_addr failed with 0x%lx\n",
		    paddr, paddr + req_size - 1, xio_addr));
	return 0;
    }
    /*
     * find which XIO port this goes to.
     */
    if (XIO_PACKED(xio_addr)) {
	if (xio_addr == XIO_NOWHERE) {
	    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_DMADIR, pconn_vhdl,
		        "pcibr_dmatrans_addr: wanted paddr [0x%lx..0x%lx], "
		        "xtalk_dmatrans_addr failed with XIO_NOWHERE\n",
		        paddr, paddr + req_size - 1));
	    return 0;
	}
	xio_port = XIO_PORT(xio_addr);
	xio_addr = XIO_ADDR(xio_addr);

    } else
	xio_port = pcibr_soft->bs_mxid;

    /*
     * If this DMA comes back to us,
     * return the PCI MEM address on
     * which it would land, or NULL
     * if the target is something
     * on bridge other than PCI MEM.
     */
    if (xio_port == pcibr_soft->bs_xid) {
	pci_addr = pcibr_addr_xio_to_pci(pcibr_soft, xio_addr, req_size);
        PCIBR_DEBUG((PCIBR_DEBUG_DMADIR, pconn_vhdl,
		    "pcibr_dmatrans_addr:  wanted paddr [0x%lx..0x%lx], "
		    "xio_port=0x%x, pci_addr=0x%lx\n",
		    paddr, paddr + req_size - 1, xio_port, pci_addr));
	return pci_addr;
    }
    /* If the caller can use A64, try to
     * satisfy the request with the 64-bit
     * direct map. This can fail if the
     * configuration bits in Device(x)
     * conflict with our flags.
     */

    if (flags & PCIIO_DMA_A64) {
	pci_addr = slotp->bss_d64_base;
	if (!(flags & PCIBR_VCHAN1))
	    flags |= PCIBR_VCHAN0;
	if ((pci_addr != PCIBR_D64_BASE_UNSET) &&
	    (flags == slotp->bss_d64_flags)) {

	    pci_addr |= xio_addr |
		((uint64_t) xio_port << PCI64_ATTR_TARG_SHFT);
	    PCIBR_DEBUG((PCIBR_DEBUG_DMADIR, pconn_vhdl,
			"pcibr_dmatrans_addr:  wanted paddr [0x%lx..0x%lx], "
			"xio_port=0x%x, direct64: pci_addr=0x%lx\n",
			paddr, paddr + req_size - 1, xio_addr, pci_addr));
	    return pci_addr;
	}
	if (!pcibr_try_set_device(pcibr_soft, pciio_slot, flags, BRIDGE_DEV_D64_BITS)) {
	    pci_addr = pcibr_flags_to_d64(flags, pcibr_soft);
	    slotp->bss_d64_flags = flags;
	    slotp->bss_d64_base = pci_addr;
            pci_addr |= xio_addr
		| ((uint64_t) xio_port << PCI64_ATTR_TARG_SHFT);

	    /* If in PCI mode, make sure we have an RRB (or two).
	     */
	    if (IS_PCI(pcibr_soft) && 
		!(pcibr_soft->bs_rrb_fixed & (1 << pciio_slot))) {
		if (flags & PCIBR_VCHAN1)
		    vchan = VCHAN1;
		have_rrbs = pcibr_soft->bs_rrb_valid[pciio_slot][vchan];
		if (have_rrbs < 2) {
		    if (pci_addr & PCI64_ATTR_PREF)
			min_rrbs = 2;
		    else
			min_rrbs = 1;
		    if (have_rrbs < min_rrbs)
			pcibr_rrb_alloc_more(pcibr_soft, pciio_slot, vchan,
					       min_rrbs - have_rrbs);
		}
	    }
	    PCIBR_DEBUG((PCIBR_DEBUG_DMADIR, pconn_vhdl,
			"pcibr_dmatrans_addr:  wanted paddr [0x%lx..0x%lx], "
			"xio_port=0x%x, direct64: pci_addr=0x%lx, "
			"new flags: 0x%x\n", paddr, paddr + req_size - 1,
			xio_addr, pci_addr, (uint64_t) flags));
	    return pci_addr;
	}

	PCIBR_DEBUG((PCIBR_DEBUG_DMADIR, pconn_vhdl,
		    "pcibr_dmatrans_addr:  wanted paddr [0x%lx..0x%lx], "
		    "xio_port=0x%x, Unable to set direct64 Device(x) bits\n",
		    paddr, paddr + req_size - 1, xio_addr));

	/* PIC only supports 64-bit direct mapping in PCI-X mode */
	if (IS_PCIX(pcibr_soft)) {
	    return 0;
	}

	/* our flags conflict with Device(x). try direct32*/
	flags = flags & ~(PCIIO_DMA_A64 | PCIBR_VCHAN0);
    } else {
	/* BUS in PCI-X mode only supports 64-bit direct mapping */
	if (IS_PCIX(pcibr_soft)) {
	    return 0;
	}
    }
    /* Try to satisfy the request with the 32-bit direct
     * map. This can fail if the configuration bits in
     * Device(x) conflict with our flags, or if the
     * target address is outside where DIR_OFF points.
     */
    {
	size_t                  map_size = 1ULL << 31;
	iopaddr_t               xio_base = pcibr_soft->bs_dir_xbase;
	iopaddr_t               offset = xio_addr - xio_base;
	iopaddr_t               endoff = req_size + offset;

	if ((req_size > map_size) ||
	    (xio_addr < xio_base) ||
	    (xio_port != pcibr_soft->bs_dir_xport) ||
	    (endoff > map_size)) {

	    PCIBR_DEBUG((PCIBR_DEBUG_DMADIR, pconn_vhdl,
			"pcibr_dmatrans_addr:  wanted paddr [0x%lx..0x%lx], "
			"xio_port=0x%x, xio region outside direct32 target\n",
			paddr, paddr + req_size - 1, xio_addr));
	} else {
	    pci_addr = slotp->bss_d32_base;
	    if ((pci_addr != PCIBR_D32_BASE_UNSET) &&
		(flags == slotp->bss_d32_flags)) {

		pci_addr |= offset;

		PCIBR_DEBUG((PCIBR_DEBUG_DMADIR, pconn_vhdl,
                            "pcibr_dmatrans_addr:  wanted paddr [0x%lx..0x%lx],"
                            " xio_port=0x%x, direct32: pci_addr=0x%lx\n",
                            paddr, paddr + req_size - 1, xio_addr, pci_addr));

		return pci_addr;
	    }
	    if (!pcibr_try_set_device(pcibr_soft, pciio_slot, flags, BRIDGE_DEV_D32_BITS)) {

		pci_addr = PCI32_DIRECT_BASE;
		slotp->bss_d32_flags = flags;
		slotp->bss_d32_base = pci_addr;
		pci_addr |= offset;

		/* Make sure we have an RRB (or two).
		 */
		if (!(pcibr_soft->bs_rrb_fixed & (1 << pciio_slot))) {
		    have_rrbs = pcibr_soft->bs_rrb_valid[pciio_slot][vchan];
		    if (have_rrbs < 2) {
			if (slotp->bss_device & BRIDGE_DEV_PREF)
			    min_rrbs = 2;
			else
			    min_rrbs = 1;
			if (have_rrbs < min_rrbs)
			    pcibr_rrb_alloc_more(pcibr_soft, pciio_slot, 
						   vchan, min_rrbs - have_rrbs);
		    }
		}
		PCIBR_DEBUG((PCIBR_DEBUG_DMADIR, pconn_vhdl,
                            "pcibr_dmatrans_addr:  wanted paddr [0x%lx..0x%lx],"
                            " xio_port=0x%x, direct32: pci_addr=0x%lx, "
			    "new flags: 0x%x\n", paddr, paddr + req_size - 1,
			    xio_addr, pci_addr, (uint64_t) flags));

		return pci_addr;
	    }
	    /* our flags conflict with Device(x).
	     */
	    PCIBR_DEBUG((PCIBR_DEBUG_DMADIR, pconn_vhdl,
                    "pcibr_dmatrans_addr:  wanted paddr [0x%lx..0x%lx], "
                    "xio_port=0x%x, Unable to set direct32 Device(x) bits\n",
                    paddr, paddr + req_size - 1, xio_port));
	}
    }

    PCIBR_DEBUG((PCIBR_DEBUG_DMADIR, pconn_vhdl,
		"pcibr_dmatrans_addr:  wanted paddr [0x%lx..0x%lx], "
		"xio_port=0x%x, No acceptable PCI address found\n",
		paddr, paddr + req_size - 1, xio_port));

    return 0;
}

void
pcibr_dmamap_drain(pcibr_dmamap_t map)
{
    xtalk_dmamap_drain(map->bd_xtalk);
}

void
pcibr_dmaaddr_drain(vertex_hdl_t pconn_vhdl,
		    paddr_t paddr,
		    size_t bytes)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    vertex_hdl_t            xconn_vhdl = pcibr_soft->bs_conn;

    xtalk_dmaaddr_drain(xconn_vhdl, paddr, bytes);
}

/*
 * Get the starting PCIbus address out of the given DMA map.
 * This function is supposed to be used by a close friend of PCI bridge
 * since it relies on the fact that the starting address of the map is fixed at
 * the allocation time in the current implementation of PCI bridge.
 */
iopaddr_t
pcibr_dmamap_pciaddr_get(pcibr_dmamap_t pcibr_dmamap)
{
    return pcibr_dmamap->bd_pci_addr;
}

/* =====================================================================
 *    CONFIGURATION MANAGEMENT
 */
/*ARGSUSED */
void
pcibr_provider_startup(vertex_hdl_t pcibr)
{
}

/*ARGSUSED */
void
pcibr_provider_shutdown(vertex_hdl_t pcibr)
{
}

int
pcibr_reset(vertex_hdl_t conn)
{
	BUG();
	return -1;
}

pciio_endian_t
pcibr_endian_set(vertex_hdl_t pconn_vhdl,
		 pciio_endian_t device_end,
		 pciio_endian_t desired_end)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pciio_slot_t            pciio_slot = PCIBR_INFO_SLOT_GET_INT(pciio_info);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    uint64_t		    devreg;
    unsigned long	    s;

    /*
     * Bridge supports hardware swapping; so we can always
     * arrange for the caller's desired endianness.
     */

    s = pcibr_lock(pcibr_soft);
    devreg = pcibr_soft->bs_slot[pciio_slot].bss_device;
    if (device_end != desired_end)
	devreg |= BRIDGE_DEV_SWAP_BITS;
    else
	devreg &= ~BRIDGE_DEV_SWAP_BITS;

    /* NOTE- if we ever put SWAP bits
     * onto the disabled list, we will
     * have to change the logic here.
     */
    if (pcibr_soft->bs_slot[pciio_slot].bss_device != devreg) {
	pcireg_device_set(pcibr_soft, pciio_slot, devreg);
	pcibr_soft->bs_slot[pciio_slot].bss_device = devreg;
	pcireg_tflush_get(pcibr_soft);
    }
    pcibr_unlock(pcibr_soft, s);

    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_DEVREG, pconn_vhdl,
    		"pcibr_endian_set: Device(%d): 0x%x\n",
		pciio_slot, devreg));

    return desired_end;
}

/*
 * Interfaces to allow special (e.g. SGI) drivers to set/clear
 * Bridge-specific device flags.  Many flags are modified through
 * PCI-generic interfaces; we don't allow them to be directly
 * manipulated here.  Only flags that at this point seem pretty
 * Bridge-specific can be set through these special interfaces.
 * We may add more flags as the need arises, or remove flags and
 * create PCI-generic interfaces as the need arises.
 *
 * Returns 0 on failure, 1 on success
 */
int
pcibr_device_flags_set(vertex_hdl_t pconn_vhdl,
		       pcibr_device_flags_t flags)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pciio_slot_t            pciio_slot = PCIBR_INFO_SLOT_GET_INT(pciio_info);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    uint64_t		    set = 0;
    uint64_t		    clr = 0;

    ASSERT((flags & PCIBR_DEVICE_FLAGS) == flags);

    if (flags & PCIBR_WRITE_GATHER)
	set |= BRIDGE_DEV_PMU_WRGA_EN;
    if (flags & PCIBR_NOWRITE_GATHER)
	clr |= BRIDGE_DEV_PMU_WRGA_EN;

    if (flags & PCIBR_PREFETCH)
	set |= BRIDGE_DEV_PREF;
    if (flags & PCIBR_NOPREFETCH)
	clr |= BRIDGE_DEV_PREF;

    if (flags & PCIBR_PRECISE)
	set |= BRIDGE_DEV_PRECISE;
    if (flags & PCIBR_NOPRECISE)
	clr |= BRIDGE_DEV_PRECISE;

    if (flags & PCIBR_BARRIER)
	set |= BRIDGE_DEV_BARRIER;
    if (flags & PCIBR_NOBARRIER)
	clr |= BRIDGE_DEV_BARRIER;

    if (flags & PCIBR_64BIT)
	set |= BRIDGE_DEV_DEV_SIZE;
    if (flags & PCIBR_NO64BIT)
	clr |= BRIDGE_DEV_DEV_SIZE;

    /* PIC BRINGUP WAR (PV# 878674):   Don't allow 64bit PIO accesses */
    if ((flags & PCIBR_64BIT) && PCIBR_WAR_ENABLED(PV878674, pcibr_soft)) {
	set &= ~BRIDGE_DEV_DEV_SIZE;
    }

    if (set || clr) {
	uint64_t		devreg;
	unsigned long		s;

	s = pcibr_lock(pcibr_soft);
	devreg = pcibr_soft->bs_slot[pciio_slot].bss_device;
	devreg = (devreg & ~clr) | set;
	if (pcibr_soft->bs_slot[pciio_slot].bss_device != devreg) {
	    pcireg_device_set(pcibr_soft, pciio_slot, devreg);
	    pcibr_soft->bs_slot[pciio_slot].bss_device = devreg;
	    pcireg_tflush_get(pcibr_soft);
	}
	pcibr_unlock(pcibr_soft, s);

	PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_DEVREG, pconn_vhdl,
		    "pcibr_device_flags_set: Device(%d): 0x%x\n",
		    pciio_slot, devreg));
    }
    return 1;
}

/*
 * PIC has 16 RBARs per bus; meaning it can have a total of 16 outstanding 
 * split transactions.  If the functions on the bus have requested a total 
 * of 16 or less, then we can give them what they requested (ie. 100%). 
 * Otherwise we have make sure each function can get at least one buffer
 * and then divide the rest of the buffers up among the functions as ``A 
 * PERCENTAGE OF WHAT THEY REQUESTED'' (i.e. 0% - 100% of a function's
 * pcix_type0_status.max_out_split).  This percentage does not include the
 * one RBAR that all functions get by default.
 */
int
pcibr_pcix_rbars_calc(pcibr_soft_t pcibr_soft)
{
    /* 'percent_allowed' is the percentage of requested RBARs that functions
     * are allowed, ***less the 1 RBAR that all functions get by default***
     */
    int percent_allowed; 

    if (pcibr_soft->bs_pcix_num_funcs) {
	if (pcibr_soft->bs_pcix_num_funcs > NUM_RBAR) {
	    printk(KERN_WARNING
		"%s: Must oversubscribe Read Buffer Attribute Registers"
		"(RBAR).  Bus has %d RBARs but %d funcs need them.\n",
		pcibr_soft->bs_name, NUM_RBAR, pcibr_soft->bs_pcix_num_funcs);
	    percent_allowed = 0;
	} else {
	    percent_allowed = (((NUM_RBAR-pcibr_soft->bs_pcix_num_funcs)*100) /
		               pcibr_soft->bs_pcix_split_tot);

	    /* +1 to percentage to solve rounding errors that occur because
	     * we're not doing fractional math. (ie. ((3 * 66%) / 100) = 1)
	     * but should be "2" if doing true fractional math.  NOTE: Since
	     * the greatest number of outstanding transactions a function 
	     * can request is 32, this "+1" will always work (i.e. we won't
	     * accidentally oversubscribe the RBARs because of this rounding
	     * of the percentage).
	     */
	    percent_allowed=(percent_allowed > 100) ? 100 : percent_allowed+1;
	}
    } else {
	return -ENODEV;
    }

    return percent_allowed;
}

/*
 * pcibr_debug() is used to print pcibr debug messages to the console.  A
 * user enables tracing by setting the following global variables:
 *
 *    pcibr_debug_mask 	   -Bitmask of what to trace. see pcibr_private.h
 *    pcibr_debug_module   -Module to trace.  'all' means trace all modules
 *    pcibr_debug_widget   -Widget to trace. '-1' means trace all widgets
 *    pcibr_debug_slot	   -Slot to trace.  '-1' means trace all slots
 *
 * 'type' is the type of debugging that the current PCIBR_DEBUG macro is
 * tracing.  'vhdl' (which can be NULL) is the vhdl associated with the
 * debug statement.  If there is a 'vhdl' associated with this debug
 * statement, it is parsed to obtain the module, widget, and slot.  If the
 * globals above match the PCIBR_DEBUG params, then the debug info in the
 * parameter 'format' is sent to the console.
 */
void
pcibr_debug(uint32_t type, vertex_hdl_t vhdl, char *format, ...)
{
    char hwpath[MAXDEVNAME] = "\0";
    char copy_of_hwpath[MAXDEVNAME];
    char *buffer;
    char *module = "all";
    short widget = -1;
    short slot = -1;
    va_list ap;

    if (pcibr_debug_mask & type) {
        if (vhdl) {
            if (!hwgraph_vertex_name_get(vhdl, hwpath, MAXDEVNAME)) {
                char *cp;

                if (strcmp(module, pcibr_debug_module)) {
		    /* use a copy */
                    (void)strcpy(copy_of_hwpath, hwpath);
		    cp = strstr(copy_of_hwpath, "/" EDGE_LBL_MODULE "/");
                    if (cp) {
                        cp += strlen("/" EDGE_LBL_MODULE "/");
			module = strsep(&cp, "/");
                    }
                }
                if (pcibr_debug_widget != -1) {
		    cp = strstr(hwpath, "/" EDGE_LBL_XTALK "/");
                    if (cp) {
			cp += strlen("/" EDGE_LBL_XTALK "/");
                        widget = simple_strtoul(cp, NULL, 0);
                    }
                }
                if (pcibr_debug_slot != -1) {
		    cp = strstr(hwpath, "/" EDGE_LBL_PCIX_0 "/");
		    if (!cp) {
			cp = strstr(hwpath, "/" EDGE_LBL_PCIX_1 "/");
		    }
                    if (cp) {
                        cp += strlen("/" EDGE_LBL_PCIX_0 "/");
                        slot = simple_strtoul(cp, NULL, 0);
                    }
                }
            }
        }
        if ((vhdl == NULL) ||
            (!strcmp(module, pcibr_debug_module) &&
             (widget == pcibr_debug_widget) &&
             (slot == pcibr_debug_slot))) {

	    buffer = kmalloc(1024, GFP_KERNEL);
	    if (buffer) {
		printk("PCIBR_DEBUG<%d>\t: %s :", smp_processor_id(), hwpath);
		/*
		 * KERN_MSG translates to this 3 line sequence. Since
		 * we have a variable length argument list, we need to
		 * call KERN_MSG this way rather than directly
		 */
		va_start(ap, format);
		memset(buffer, 0, 1024);
		vsnprintf(buffer, 1024, format, ap);
		va_end(ap);
		printk("%s", buffer);
		kfree(buffer);
	    }
        }
    }
}

/*
 * given a xconn_vhdl and a bus number under that widget, return a 
 * bridge_t pointer.
 */
void *
pcibr_bridge_ptr_get(vertex_hdl_t widget_vhdl, int bus_num)
{
    void       *bridge;

    bridge = (void *)xtalk_piotrans_addr(widget_vhdl, 0, 0, 
							sizeof(bridge), 0);

    /* PIC ASIC has two bridges (ie. two buses) under a single widget */
    if (bus_num == 1) {
	bridge = (void *)((char *)bridge + PIC_BUS1_OFFSET);
    }
    return bridge;
}		


int
isIO9(nasid_t nasid)
{
	lboard_t *brd = (lboard_t *)KL_CONFIG_INFO(nasid);

	while (brd) {
		if (brd->brd_flags & LOCAL_MASTER_IO6) {
			return 1;
		}
                if (numionodes == numnodes)
                        brd = KLCF_NEXT_ANY(brd);
                else
                        brd = KLCF_NEXT(brd);
	}
	/* if it's dual ported, check the peer also */
	nasid = NODEPDA(nasid_to_cnodeid(nasid))->xbow_peer;
	if (nasid < 0) return 0;
	brd = (lboard_t *)KL_CONFIG_INFO(nasid);
	while (brd) {
		if (brd->brd_flags & LOCAL_MASTER_IO6) {
			return 1;
		}
                if (numionodes == numnodes)
                        brd = KLCF_NEXT_ANY(brd);
                else
                        brd = KLCF_NEXT(brd);

	}
	return 0;
}
