/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_PCI_PCIBR_PRIVATE_H
#define _ASM_SN_PCI_PCIBR_PRIVATE_H

/*
 * pcibr_private.h -- private definitions for pcibr
 * only the pcibr driver (and its closest friends)
 * should ever peek into this file.
 */

#include <asm/sn/pci/pciio_private.h>

/*
 * convenience typedefs
 */

typedef uint64_t pcibr_DMattr_t;
typedef uint32_t pcibr_ATEattr_t;

typedef struct pcibr_info_s *pcibr_info_t, **pcibr_info_h;
typedef struct pcibr_soft_s *pcibr_soft_t;
typedef struct pcibr_soft_slot_s *pcibr_soft_slot_t;
typedef struct pcibr_hints_s *pcibr_hints_t;
typedef struct pcibr_intr_list_s *pcibr_intr_list_t;
typedef struct pcibr_intr_wrap_s *pcibr_intr_wrap_t;

/*
 * Bridge sets up PIO using this information.
 */
struct pcibr_piomap_s {
    struct pciio_piomap_s   bp_pp;	/* generic stuff */

#define	bp_flags	bp_pp.pp_flags	/* PCIBR_PIOMAP flags */
#define	bp_dev		bp_pp.pp_dev	/* associated pci card */
#define	bp_slot		bp_pp.pp_slot	/* which slot the card is in */
#define	bp_space	bp_pp.pp_space	/* which address space */
#define	bp_pciaddr	bp_pp.pp_pciaddr	/* starting offset of mapping */
#define	bp_mapsz	bp_pp.pp_mapsz	/* size of this mapping */
#define	bp_kvaddr	bp_pp.pp_kvaddr	/* kernel virtual address to use */

    iopaddr_t               bp_xtalk_addr;	/* corresponding xtalk address */
    xtalk_piomap_t          bp_xtalk_pio;	/* corresponding xtalk resource */
    pcibr_piomap_t	    bp_next;	/* Next piomap on the list */
    pcibr_soft_t	    bp_soft;	/* backpointer to bridge soft data */
    int			    bp_toc[1];	/* PCI timeout counter */

};

/*
 * Bridge sets up DMA using this information.
 */
struct pcibr_dmamap_s {
    struct pciio_dmamap_s   bd_pd;
#define	bd_flags	bd_pd.pd_flags	/* PCIBR_DMAMAP flags */
#define	bd_dev		bd_pd.pd_dev	/* associated pci card */
#define	bd_slot		bd_pd.pd_slot	/* which slot the card is in */
    struct pcibr_soft_s    *bd_soft;	/* pcibr soft state backptr */
    xtalk_dmamap_t          bd_xtalk;	/* associated xtalk resources */

    size_t                  bd_max_size;	/* maximum size of mapping */
    xwidgetnum_t            bd_xio_port;	/* target XIO port */
    iopaddr_t               bd_xio_addr;	/* target XIO address */
    iopaddr_t               bd_pci_addr;	/* via PCI address */

    int                     bd_ate_index;	/* Address Translation Entry Index */
    int                     bd_ate_count;	/* number of ATE's allocated */
    bridge_ate_p            bd_ate_ptr;		/* where to write first ATE */
    bridge_ate_t            bd_ate_proto;	/* prototype ATE (for xioaddr=0) */
    bridge_ate_t            bd_ate_prime;	/* value of 1st ATE written */
};

/*
 * Bridge sets up interrupts using this information.
 */

struct pcibr_intr_s {
    struct pciio_intr_s     bi_pi;
#define	bi_flags	bi_pi.pi_flags	/* PCIBR_INTR flags */
#define	bi_dev		bi_pi.pi_dev	/* associated pci card */
#define	bi_lines	bi_pi.pi_lines	/* which PCI interrupt line(s) */
#define	bi_func		bi_pi.pi_func	/* handler function (when connected) */
#define	bi_arg		bi_pi.pi_arg	/* handler parameter (when connected) */
#define bi_tinfo	bi_pi.pi_tinfo	/* Thread info (when connected) */
#define bi_mustruncpu	bi_pi.pi_mustruncpu /* Where we must run. */
#define bi_irq		bi_pi.pi_irq	/* IRQ assigned. */
#define bi_cpu		bi_pi.pi_cpu	/* cpu assigned. */
    unsigned                bi_ibits;	/* which Bridge interrupt bit(s) */
    pcibr_soft_t            bi_soft;	/* shortcut to soft info */
};

/*
 * per-connect point pcibr data, including
 * standard pciio data in-line:
 */
struct pcibr_info_s {
    struct pciio_info_s	    f_c;	/* MUST BE FIRST. */
#define	f_vertex	f_c.c_vertex	/* back pointer to vertex */
#define	f_bus		f_c.c_bus	/* which bus the card is in */
#define	f_slot		f_c.c_slot	/* which slot the card is in */
#define	f_func		f_c.c_func	/* which func (on multi-func cards) */
#define	f_vendor	f_c.c_vendor	/* PCI card "vendor" code */
#define	f_device	f_c.c_device	/* PCI card "device" code */
#define	f_master	f_c.c_master	/* PCI bus provider */
#define	f_mfast		f_c.c_mfast	/* cached fastinfo from c_master */
#define	f_pops		f_c.c_pops	/* cached provider from c_master */
#define	f_efunc		f_c.c_efunc	/* error handling function */
#define	f_einfo		f_c.c_einfo	/* first parameter for efunc */
#define	f_window	f_c.c_window	/* state of BASE regs */
#define	f_rbase		f_c.c_rbase	/* expansion rom base */
#define	f_rsize		f_c.c_rsize	/* expansion rom size */
#define	f_piospace	f_c.c_piospace	/* additional I/O spaces allocated */

    /* pcibr-specific connection state */
    int			    f_ibit[4];	/* Bridge bit for each INTx */
    pcibr_piomap_t	    f_piomap;
};

/* =====================================================================
 *          Shared Interrupt Information
 */

struct pcibr_intr_list_s {
    pcibr_intr_list_t       il_next;
    pcibr_intr_t            il_intr;
    volatile bridgereg_t   *il_wrbf;	/* ptr to b_wr_req_buf[] */
};

/* =====================================================================
 *          Interrupt Wrapper Data
 */
struct pcibr_intr_wrap_s {
    pcibr_soft_t            iw_soft;	/* which bridge */
    volatile bridgereg_t   *iw_stat;	/* ptr to b_int_status */
    bridgereg_t             iw_intr;	/* bits in b_int_status */
    pcibr_intr_list_t       iw_list;	/* ghostbusters! */
};

#define	PCIBR_ISR_ERR_START	8
#define PCIBR_ISR_MAX_ERRS 	32

/* =====================================================================
 *            Bridge Device State structure
 *
 *      one instance of this structure is kept for each
 *      Bridge ASIC in the system.
 */

struct pcibr_soft_s {
    devfs_handle_t            bs_conn;	/* xtalk connection point */
    devfs_handle_t            bs_vhdl;	/* vertex owned by pcibr */
    int                     bs_int_enable;	/* Mask of enabled intrs */
    bridge_t               *bs_base;	/* PIO pointer to Bridge chip */
    char                   *bs_name;	/* hw graph name */
    xwidgetnum_t            bs_xid;	/* Bridge's xtalk ID number */
    devfs_handle_t            bs_master;	/* xtalk master vertex */
    xwidgetnum_t            bs_mxid;	/* master's xtalk ID number */

    iopaddr_t               bs_dir_xbase;	/* xtalk address for 32-bit PCI direct map */
    xwidgetnum_t	    bs_dir_xport;	/* xtalk port for 32-bit PCI direct map */

    struct map             *bs_int_ate_map;	/* rmalloc map for internal ATEs */
    struct map             *bs_ext_ate_map;	/* rmalloc map for external ATEs */
    short		    bs_int_ate_size;	/* number of internal ates */
    short		    bs_xbridge;		/* if 1 then xbridge */

    int                     bs_rev_num;	/* revision number of Bridge */

    unsigned                bs_dma_flags;	/* revision-implied DMA flags */

    /*
     * Lock used primarily to get mutual exclusion while managing any
     * bridge resources..
     */
    lock_t                  bs_lock;
    
    devfs_handle_t	    bs_noslot_conn;	/* NO-SLOT connection point */
    pcibr_info_t	    bs_noslot_info;
    struct pcibr_soft_slot_s {
	/* information we keep about each CFG slot */

	/* some devices (ioc3 in non-slotted
	 * configurations, sometimes) make use
	 * of more than one REQ/GNT/INT* signal
	 * sets. The slot corresponding to the
	 * IDSEL that the device responds to is
	 * called the host slot; the slot
	 * numbers that the device is stealing
	 * REQ/GNT/INT bits from are known as
	 * the guest slots.
	 */
	int                     has_host;
	pciio_slot_t            host_slot;
	devfs_handle_t		slot_conn;
	/* Potentially several connection points
	 * for this slot. bss_ninfo is how many,
	 * and bss_infos is a pointer to
	 * an array pcibr_info_t values (which are
	 * pointers to pcibr_info structs, stored
	 * as device_info in connection ponts).
	 */
	int			bss_ninfo;
	pcibr_info_h	        bss_infos;

	/* Temporary Compatibility Macros, for
	 * stuff that has moved out of bs_slot
	 * and into the info structure. These
	 * will go away when their users have
	 * converted over to multifunction-
	 * friendly use of bss_{ninfo,infos}.
	 */
#define	bss_vendor_id	bss_infos[0]->f_vendor
#define	bss_device_id	bss_infos[0]->f_device
#define	bss_window	bss_infos[0]->f_window
#define	bssw_space	w_space
#define	bssw_base	w_base
#define	bssw_size	w_size

	/* Where is DevIO(x) pointing? */
	/* bssd_space is NONE if it is not assigned. */
	struct {
	    pciio_space_t           bssd_space;
	    iopaddr_t               bssd_base;
	} bss_devio;

	/* Shadow value for Device(x) register,
	 * so we don't have to go to the chip.
	 */
	bridgereg_t             bss_device;

	/* Number of sets on GBR/REALTIME bit outstanding
	 * Used by Priority I/O for tracking reservations
	 */
	int                     bss_pri_uctr;

	/* Number of "uses" of PMU, 32-bit direct,
	 * and 64-bit direct DMA (0:none, <0: trans,
	 * >0: how many dmamaps). Device(x) bits
	 * controlling attribute of each kind of
	 * channel can't be changed by dmamap_alloc
	 * or dmatrans if the controlling counter
	 * is nonzero. dmatrans is forever.
	 */
	int                     bss_pmu_uctr;
	int                     bss_d32_uctr;
	int                     bss_d64_uctr;

	/* When the contents of mapping configuration
	 * information is locked down by dmatrans,
	 * repeated checks of the same flags should
	 * be shortcircuited for efficiency.
	 */
	iopaddr_t		bss_d64_base;
	unsigned		bss_d64_flags;
	iopaddr_t		bss_d32_base;
	unsigned		bss_d32_flags;

	/* Shadow information used for implementing
	 * Bridge Hardware WAR #484930
	 */
	int			bss_ext_ates_active;
        volatile unsigned      *bss_cmd_pointer;
	unsigned		bss_cmd_shadow;

    } bs_slot[8];

    pcibr_intr_bits_f	       *bs_intr_bits;

    /* RRB MANAGEMENT
     * bs_rrb_fixed: bitmap of slots whose RRB
     *	allocations we should not "automatically" change
     * bs_rrb_avail: number of RRBs that have not
     *  been allocated or reserved for {even,odd} slots
     * bs_rrb_res: number of RRBs reserved for the
     *	use of the index slot number
     * bs_rrb_valid: number of RRBs marked valid
     *	for the indexed slot number; indexes 8-15
     *	are for the virtual channels for slots 0-7.
     */
    int                     bs_rrb_fixed;
    int			    bs_rrb_avail[2];
    int			    bs_rrb_res[8];
    int			    bs_rrb_valid[16];

    struct {
	/* Each Bridge interrupt bit has a single XIO
	 * interrupt channel allocated.
	 */
	xtalk_intr_t            bsi_xtalk_intr;
	/*
	 * We do not like sharing PCI interrrupt lines
	 * between devices, but the Origin 200 PCI
	 * layout forces us to do so.
	 */
	pcibr_intr_list_t       bsi_pcibr_intr_list;
	pcibr_intr_wrap_t       bsi_pcibr_intr_wrap;
	int                     bsi_pcibr_wrap_set;

    } bs_intr[8];

    xtalk_intr_t		bsi_err_intr;

    /*
     * We stash away some information in this structure on getting
     * an error interrupt. This information is used during PIO read/
     * write error handling.
     *
     * As it stands now, we do not re-enable the error interrupt
     * till the error is resolved. Error resolution happens either at
     * bus error time for PIO Read errors (~100 microseconds), or at
     * the scheduled timeout time for PIO write errors (~milliseconds).
     * If this delay causes problems, we may need to move towards
     * a different scheme..
     *
     * Note that there is no locking while looking at this data structure.
     * There should not be any race between bus error code and
     * error interrupt code.. will look into this if needed.
     */
    struct br_errintr_info {
	int                     bserr_toutcnt;
#ifdef IRIX
	toid_t                  bserr_toutid;	/* Timeout started by errintr */
#endif
	iopaddr_t               bserr_addr;	/* Address where error occured */
	bridgereg_t             bserr_intstat;	/* interrupts active at error time */
    } bs_errinfo;

    /*
     * PCI Bus Space allocation data structure.
     * This info is used to satisfy the callers of pcibr_piospace_alloc
     * interface. Most of these users need "large" amounts of PIO
     * space (typically in Megabytes), and they generally tend to
     * take once and never release..
     * For Now use a simple algorithm to manage it. On allocation,
     * Update the _base field to reflect next free address.
     *
     * Freeing does nothing.. So, once allocated, it's gone for good.
     */
    struct br_pcisp_info {
	iopaddr_t               pci_io_base;
	iopaddr_t               pci_io_last;
	iopaddr_t               pci_swin_base;
	iopaddr_t               pci_swin_last;
	iopaddr_t               pci_mem_base;
	iopaddr_t               pci_mem_last;
    } bs_spinfo;

    struct bs_errintr_stat_s {
	uint32_t              bs_errcount_total;
	uint32_t              bs_lasterr_timestamp;
	uint32_t              bs_lasterr_snapshot;
    } bs_errintr_stat[PCIBR_ISR_MAX_ERRS];

    /*
     * Bridge-wide endianness control for
     * large-window PIO mappings
     *
     * These fields are set to PCIIO_BYTE_SWAP
     * or PCIIO_WORD_VALUES once the swapper
     * has been configured, one way or the other,
     * for the direct windows. If they are zero,
     * nobody has a PIO mapping through that window,
     * and the swapper can be set either way.
     */
    unsigned		bs_pio_end_io;
    unsigned		bs_pio_end_mem;
};

#define	PCIBR_ERRTIME_THRESHOLD		(100)
#define	PCIBR_ERRRATE_THRESHOLD		(100)

/*
 * pcibr will respond to hints dropped in its vertex
 * using the following structure.
 */
struct pcibr_hints_s {
    /* ph_host_slot is actually +1 so "0" means "no host" */
    pciio_slot_t            ph_host_slot[8];	/* REQ/GNT/INT in use by ... */
    unsigned                ph_rrb_fixed;	/* do not change RRB allocations */
    unsigned                ph_hands_off;	/* prevent further pcibr operations */
    rrb_alloc_funct_t       rrb_alloc_funct;	/* do dynamic rrb allocation */
    pcibr_intr_bits_f	   *ph_intr_bits;	/* map PCI INT[ABCD] to Bridge Int(n) */
};

extern int              pcibr_prefetch_enable_rev, pcibr_wg_enable_rev;

/*
 * Number of bridge non-fatal error interrupts we can see before
 * we decide to disable that interrupt.
 */
#define	PCIBR_ERRINTR_DISABLE_LEVEL	10000

/* =====================================================================
 *    Bridge (pcibr) state management functions
 *
 *      pcibr_soft_get is here because we do it in a lot
 *      of places and I want to make sure they all stay
 *      in step with each other.
 *
 *      pcibr_soft_set is here because I want it to be
 *      closely associated with pcibr_soft_get, even
 *      though it is only called in one place.
 */

#define pcibr_soft_get(v)       ((pcibr_soft_t)hwgraph_fastinfo_get((v)))
#define pcibr_soft_set(v,i)     (hwgraph_fastinfo_set((v), (arbitrary_info_t)(i)))

#endif				/* _ASM_SN_PCI_PCIBR_PRIVATE_H */
