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

#ifdef __ia64
#define rmallocmap atemapalloc
#define rmfreemap atemapfree
#define rmfree atefree
#define rmalloc atealloc
#endif

/*
 * Macros related to the Lucent USS 302/312 usb timeout workaround.  It
 * appears that if the lucent part can get into a retry loop if it sees a
 * DAC on the bus during a pio read retry.  The loop is broken after about
 * 1ms, so we need to set up bridges holding this part to allow at least
 * 1ms for pio.
 */

#define USS302_TIMEOUT_WAR

#ifdef USS302_TIMEOUT_WAR
#define LUCENT_USBHC_VENDOR_ID_NUM	0x11c1
#define LUCENT_USBHC302_DEVICE_ID_NUM	0x5801
#define LUCENT_USBHC312_DEVICE_ID_NUM	0x5802
#define USS302_BRIDGE_TIMEOUT_HLD	4
#endif

int                     pcibr_devflag = D_MP;

/*
 * This is the file operation table for the pcibr driver.
 * As each of the functions are implemented, put the
 * appropriate function name below.
 */
struct file_operations pcibr_fops = {
	.owner =THIS_MODULE,
	.llseek = NULL,
	.read = NULL,
	.write = NULL,
	.readdir = NULL,
	.poll = NULL,
	.ioctl = NULL,
	.mmap = NULL,
	.open = NULL,
	.flush = NULL,
	.release = NULL,
	.fsync = NULL,
	.fasync = NULL,
	.lock = NULL,
	.readv = NULL,
	.writev = NULL
};

#ifdef LATER

#if PCIBR_ATE_DEBUG
static struct reg_values ssram_sizes[] =
{
    {BRIDGE_CTRL_SSRAM_512K, "512k"},
    {BRIDGE_CTRL_SSRAM_128K, "128k"},
    {BRIDGE_CTRL_SSRAM_64K, "64k"},
    {BRIDGE_CTRL_SSRAM_1K, "1k"},
    {0}
};

static struct reg_desc   control_bits[] =
{
    {BRIDGE_CTRL_FLASH_WR_EN, 0, "FLASH_WR_EN"},
    {BRIDGE_CTRL_EN_CLK50, 0, "EN_CLK50"},
    {BRIDGE_CTRL_EN_CLK40, 0, "EN_CLK40"},
    {BRIDGE_CTRL_EN_CLK33, 0, "EN_CLK33"},
    {BRIDGE_CTRL_RST_MASK, -24, "RST", "%x"},
    {BRIDGE_CTRL_IO_SWAP, 0, "IO_SWAP"},
    {BRIDGE_CTRL_MEM_SWAP, 0, "MEM_SWAP"},
    {BRIDGE_CTRL_PAGE_SIZE, 0, "PAGE_SIZE"},
    {BRIDGE_CTRL_SS_PAR_BAD, 0, "SS_PAR_BAD"},
    {BRIDGE_CTRL_SS_PAR_EN, 0, "SS_PAR_EN"},
    {BRIDGE_CTRL_SSRAM_SIZE_MASK, 0, "SSRAM_SIZE", 0, ssram_sizes},
    {BRIDGE_CTRL_F_BAD_PKT, 0, "F_BAD_PKT"},
    {BRIDGE_CTRL_LLP_XBAR_CRD_MASK, -12, "LLP_XBAR_CRD", "%d"},
    {BRIDGE_CTRL_CLR_RLLP_CNT, 0, "CLR_RLLP_CNT"},
    {BRIDGE_CTRL_CLR_TLLP_CNT, 0, "CLR_TLLP_CNT"},
    {BRIDGE_CTRL_SYS_END, 0, "SYS_END"},

    {BRIDGE_CTRL_BUS_SPEED_MASK, -4, "BUS_SPEED", "%d"},
    {BRIDGE_CTRL_WIDGET_ID_MASK, 0, "WIDGET_ID", "%x"},
    {0}
};
#endif
#endif	/* LATER */

/* kbrick widgetnum-to-bus layout */
int p_busnum[MAX_PORT_NUM] = {                  /* widget#      */
        0, 0, 0, 0, 0, 0, 0, 0,                 /* 0x0 - 0x7    */
        2,                                      /* 0x8          */
        1,                                      /* 0x9          */
        0, 0,                                   /* 0xa - 0xb    */
        5,                                      /* 0xc          */
        6,                                      /* 0xd          */
        4,                                      /* 0xe          */
        3,                                      /* 0xf          */
};

/*
 * Additional PIO spaces per slot are
 * recorded in this structure.
 */
struct pciio_piospace_s {
    pciio_piospace_t        next;       /* another space for this device */
    char                    free;       /* 1 if free, 0 if in use               */
    pciio_space_t           space;      /* Which space is in use                */
    iopaddr_t               start;      /* Starting address of the PIO space    */
    size_t                  count;      /* size of PIO space                    */
};

#if PCIBR_SOFT_LIST
pcibr_list_p            pcibr_list = 0;
#endif

extern int              hwgraph_vertex_name_get(devfs_handle_t vhdl, char *buf, uint buflen);
extern int              hub_device_flags_set(devfs_handle_t widget_dev, hub_widget_flags_t flags);
extern long             atoi(register char *p);
extern cnodeid_t        nodevertex_to_cnodeid(devfs_handle_t vhdl);
extern void             *swap_ptr(void **loc, void *new);
extern char             *dev_to_name(devfs_handle_t dev, char *buf, uint buflen);
extern struct map       *atemapalloc(uint64_t);
extern void             atefree(struct map *, size_t, uint64_t);
extern void             atemapfree(struct map *);
extern pciio_dmamap_t   get_free_pciio_dmamap(devfs_handle_t);
extern void		free_pciio_dmamap(pcibr_dmamap_t);

#define	ATE_WRITE()    ate_write(ate_ptr, ate_count, ate)
#if PCIBR_FREEZE_TIME
#define	ATE_FREEZE()	s = ate_freeze(pcibr_dmamap, &freeze_time, cmd_regs)
#else
#define	ATE_FREEZE()	s = ate_freeze(pcibr_dmamap, cmd_regs)
#endif /* PCIBR_FREEZE_TIME */

#if PCIBR_FREEZE_TIME
#define	ATE_THAW()	ate_thaw(pcibr_dmamap, ate_index, ate, ate_total, freeze_time, cmd_regs, s)
#else
#define	ATE_THAW()	ate_thaw(pcibr_dmamap, ate_index, cmd_regs, s)
#endif


/* =====================================================================
 *    Function Table of Contents
 *
 *      The order of functions in this file has stopped
 *      making much sense. We might want to take a look
 *      at it some time and bring back some sanity, or
 *      perhaps bust this file into smaller chunks.
 */

extern void              do_pcibr_rrb_clear(bridge_t *, int);
extern void              do_pcibr_rrb_flush(bridge_t *, int);
extern int               do_pcibr_rrb_count_valid(bridge_t *, pciio_slot_t);
extern int               do_pcibr_rrb_count_avail(bridge_t *, pciio_slot_t);
extern int               do_pcibr_rrb_alloc(bridge_t *, pciio_slot_t, int);
extern int               do_pcibr_rrb_free(bridge_t *, pciio_slot_t, int);

extern void              do_pcibr_rrb_autoalloc(pcibr_soft_t, int, int);

extern int  		 pcibr_wrb_flush(devfs_handle_t);
extern int               pcibr_rrb_alloc(devfs_handle_t, int *, int *);
extern int               pcibr_rrb_check(devfs_handle_t, int *, int *, int *, int *);
extern int               pcibr_alloc_all_rrbs(devfs_handle_t, int, int, int, int, int, int, int, int, int);
extern void              pcibr_rrb_flush(devfs_handle_t);

static int                pcibr_try_set_device(pcibr_soft_t, pciio_slot_t, unsigned, bridgereg_t);
void                     pcibr_release_device(pcibr_soft_t, pciio_slot_t, bridgereg_t);

extern void              pcibr_clearwidint(bridge_t *);
extern void              pcibr_setwidint(xtalk_intr_t);

void                     pcibr_init(void);
int                      pcibr_attach(devfs_handle_t);
int			 pcibr_detach(devfs_handle_t);
int                      pcibr_open(devfs_handle_t *, int, int, cred_t *);
int                      pcibr_close(devfs_handle_t, int, int, cred_t *);
int                      pcibr_map(devfs_handle_t, vhandl_t *, off_t, size_t, uint);
int                      pcibr_unmap(devfs_handle_t, vhandl_t *);
int                      pcibr_ioctl(devfs_handle_t, int, void *, int, struct cred *, int *);

void                     pcibr_freeblock_sub(iopaddr_t *, iopaddr_t *, iopaddr_t, size_t);

extern int               pcibr_init_ext_ate_ram(bridge_t *);
extern int               pcibr_ate_alloc(pcibr_soft_t, int);
extern void              pcibr_ate_free(pcibr_soft_t, int, int);

extern unsigned          ate_freeze(pcibr_dmamap_t pcibr_dmamap,
#if PCIBR_FREEZE_TIME
	   		 unsigned *freeze_time_ptr,
#endif
						unsigned *cmd_regs);
extern void              ate_write(bridge_ate_p ate_ptr, int ate_count, bridge_ate_t ate);
extern void              ate_thaw(pcibr_dmamap_t pcibr_dmamap, int ate_index,
#if PCIBR_FREEZE_TIME
	 				bridge_ate_t ate,
	 				int ate_total,
	 				unsigned freeze_time_start,
#endif
	 				unsigned *cmd_regs,
	 				unsigned s);

pcibr_info_t             pcibr_info_get(devfs_handle_t);

static iopaddr_t         pcibr_addr_pci_to_xio(devfs_handle_t, pciio_slot_t, pciio_space_t, iopaddr_t, size_t, unsigned);

pcibr_piomap_t          pcibr_piomap_alloc(devfs_handle_t, device_desc_t, pciio_space_t, iopaddr_t, size_t, size_t, unsigned);
void                    pcibr_piomap_free(pcibr_piomap_t);
caddr_t                 pcibr_piomap_addr(pcibr_piomap_t, iopaddr_t, size_t);
void                    pcibr_piomap_done(pcibr_piomap_t);
caddr_t                 pcibr_piotrans_addr(devfs_handle_t, device_desc_t, pciio_space_t, iopaddr_t, size_t, unsigned);
iopaddr_t               pcibr_piospace_alloc(devfs_handle_t, device_desc_t, pciio_space_t, size_t, size_t);
void                    pcibr_piospace_free(devfs_handle_t, pciio_space_t, iopaddr_t, size_t);

static iopaddr_t         pcibr_flags_to_d64(unsigned, pcibr_soft_t);
extern bridge_ate_t     pcibr_flags_to_ate(unsigned);

pcibr_dmamap_t          pcibr_dmamap_alloc(devfs_handle_t, device_desc_t, size_t, unsigned);
void                    pcibr_dmamap_free(pcibr_dmamap_t);
extern bridge_ate_p     pcibr_ate_addr(pcibr_soft_t, int);
static iopaddr_t         pcibr_addr_xio_to_pci(pcibr_soft_t, iopaddr_t, size_t);
iopaddr_t               pcibr_dmamap_addr(pcibr_dmamap_t, paddr_t, size_t);
alenlist_t              pcibr_dmamap_list(pcibr_dmamap_t, alenlist_t, unsigned);
void                    pcibr_dmamap_done(pcibr_dmamap_t);
cnodeid_t		pcibr_get_dmatrans_node(devfs_handle_t);
iopaddr_t               pcibr_dmatrans_addr(devfs_handle_t, device_desc_t, paddr_t, size_t, unsigned);
alenlist_t              pcibr_dmatrans_list(devfs_handle_t, device_desc_t, alenlist_t, unsigned);
void                    pcibr_dmamap_drain(pcibr_dmamap_t);
void                    pcibr_dmaaddr_drain(devfs_handle_t, paddr_t, size_t);
void                    pcibr_dmalist_drain(devfs_handle_t, alenlist_t);
iopaddr_t               pcibr_dmamap_pciaddr_get(pcibr_dmamap_t);

extern unsigned		pcibr_intr_bits(pciio_info_t info, pciio_intr_line_t lines);
extern pcibr_intr_t     pcibr_intr_alloc(devfs_handle_t, device_desc_t, pciio_intr_line_t, devfs_handle_t);
extern void             pcibr_intr_free(pcibr_intr_t);
extern void             pcibr_setpciint(xtalk_intr_t);
extern int              pcibr_intr_connect(pcibr_intr_t);
extern void             pcibr_intr_disconnect(pcibr_intr_t);

extern devfs_handle_t     pcibr_intr_cpu_get(pcibr_intr_t);
extern void             pcibr_xintr_preset(void *, int, xwidgetnum_t, iopaddr_t, xtalk_intr_vector_t);
extern void             pcibr_intr_func(intr_arg_t);

extern void             print_bridge_errcmd(uint32_t, char *);

extern void             pcibr_error_dump(pcibr_soft_t);
extern uint32_t         pcibr_errintr_group(uint32_t);
extern void	        pcibr_pioerr_check(pcibr_soft_t);
extern void             pcibr_error_intr_handler(intr_arg_t);

extern int              pcibr_addr_toslot(pcibr_soft_t, iopaddr_t, pciio_space_t *, iopaddr_t *, pciio_function_t *);
extern void             pcibr_error_cleanup(pcibr_soft_t, int);
extern void                    pcibr_device_disable(pcibr_soft_t, int);
extern int              pcibr_pioerror(pcibr_soft_t, int, ioerror_mode_t, ioerror_t *);
extern int              pcibr_dmard_error(pcibr_soft_t, int, ioerror_mode_t, ioerror_t *);
extern int              pcibr_dmawr_error(pcibr_soft_t, int, ioerror_mode_t, ioerror_t *);
extern int              pcibr_error_handler(error_handler_arg_t, int, ioerror_mode_t, ioerror_t *);
extern int              pcibr_error_devenable(devfs_handle_t, int);

void                    pcibr_provider_startup(devfs_handle_t);
void                    pcibr_provider_shutdown(devfs_handle_t);

int                     pcibr_reset(devfs_handle_t);
pciio_endian_t          pcibr_endian_set(devfs_handle_t, pciio_endian_t, pciio_endian_t);
int                     pcibr_priority_bits_set(pcibr_soft_t, pciio_slot_t, pciio_priority_t);
pciio_priority_t        pcibr_priority_set(devfs_handle_t, pciio_priority_t);
int                     pcibr_device_flags_set(devfs_handle_t, pcibr_device_flags_t);

extern cfg_p            pcibr_config_addr(devfs_handle_t, unsigned);
extern uint64_t         pcibr_config_get(devfs_handle_t, unsigned, unsigned);
extern void             pcibr_config_set(devfs_handle_t, unsigned, unsigned, uint64_t);
extern void             do_pcibr_config_set(cfg_p, unsigned, unsigned, uint64_t);

extern pcibr_hints_t    pcibr_hints_get(devfs_handle_t, int);
extern void             pcibr_hints_fix_rrbs(devfs_handle_t);
extern void             pcibr_hints_dualslot(devfs_handle_t, pciio_slot_t, pciio_slot_t);
extern void	 	pcibr_hints_intr_bits(devfs_handle_t, pcibr_intr_bits_f *);
extern void             pcibr_set_rrb_callback(devfs_handle_t, rrb_alloc_funct_t);
extern void             pcibr_hints_handsoff(devfs_handle_t);
extern void             pcibr_hints_subdevs(devfs_handle_t, pciio_slot_t, uint64_t);

#ifdef BRIDGE_B_DATACORR_WAR
extern int              ql_bridge_rev_b_war(devfs_handle_t);
extern int              bridge_rev_b_data_check_disable;
char                   *rev_b_datacorr_warning =
"***************************** WARNING! ******************************\n";
char                   *rev_b_datacorr_mesg =
"UNRECOVERABLE IO LINK ERROR. CONTACT SERVICE PROVIDER\n";
#endif

extern int		pcibr_slot_reset(devfs_handle_t,pciio_slot_t);
extern int		pcibr_slot_info_init(devfs_handle_t,pciio_slot_t);
extern int		pcibr_slot_info_free(devfs_handle_t,pciio_slot_t);
extern int		pcibr_slot_addr_space_init(devfs_handle_t,pciio_slot_t);
extern int		pcibr_slot_device_init(devfs_handle_t, pciio_slot_t);
extern int		pcibr_slot_guest_info_init(devfs_handle_t,pciio_slot_t);
extern int		pcibr_slot_call_device_attach(devfs_handle_t, pciio_slot_t, int);
extern int		pcibr_slot_call_device_detach(devfs_handle_t, pciio_slot_t, int);
extern int              pcibr_slot_attach(devfs_handle_t, pciio_slot_t, int, char *, int *);
extern int              pcibr_slot_detach(devfs_handle_t, pciio_slot_t, int);
extern int 		pcibr_is_slot_sys_critical(devfs_handle_t, pciio_slot_t);

#ifdef LATER
extern int		pcibr_slot_startup(devfs_handle_t, pcibr_slot_req_t);
extern int		pcibr_slot_shutdown(devfs_handle_t, pcibr_slot_req_t);
extern int		pcibr_slot_query(devfs_handle_t, pcibr_slot_req_t);
#endif

extern int		pcibr_slot_initial_rrb_alloc(devfs_handle_t, pciio_slot_t);
extern int		pcibr_initial_rrb(devfs_handle_t, pciio_slot_t, pciio_slot_t);



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
		     bridgereg_t mask)
{
    bridge_t               *bridge;
    pcibr_soft_slot_t       slotp;
    bridgereg_t             old;
    bridgereg_t             new;
    bridgereg_t             chg;
    bridgereg_t             bad;
    bridgereg_t             badpmu;
    bridgereg_t             badd32;
    bridgereg_t             badd64;
    bridgereg_t             fix;
    unsigned long           s;
    bridgereg_t             xmask;

    xmask = mask;
    if (pcibr_soft->bs_xbridge) {
    	if (mask == BRIDGE_DEV_PMU_BITS)
		xmask = XBRIDGE_DEV_PMU_BITS;
	if (mask == BRIDGE_DEV_D64_BITS)
		xmask = XBRIDGE_DEV_D64_BITS;
    }

    slotp = &pcibr_soft->bs_slot[slot];

    s = pcibr_lock(pcibr_soft);

    bridge = pcibr_soft->bs_base;

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
	new |= (pcibr_soft->bs_xbridge) ? 
			BRIDGE_DEV_SWAP_DIR : BRIDGE_DEV_SWAP_BITS;
    if (flags & PCIIO_WORD_VALUES)
	new &= (pcibr_soft->bs_xbridge) ? 
			~BRIDGE_DEV_SWAP_DIR : ~BRIDGE_DEV_SWAP_BITS;

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

    chg = old ^ new;				/* what are we changing, */
    chg &= xmask;				/* of the interesting bits */

    if (chg) {

	badd32 = slotp->bss_d32_uctr ? (BRIDGE_DEV_D32_BITS & chg) : 0;
	if (pcibr_soft->bs_xbridge) {
		badpmu = slotp->bss_pmu_uctr ? (XBRIDGE_DEV_PMU_BITS & chg) : 0;
		badd64 = slotp->bss_d64_uctr ? (XBRIDGE_DEV_D64_BITS & chg) : 0;
	} else {
		badpmu = slotp->bss_pmu_uctr ? (BRIDGE_DEV_PMU_BITS & chg) : 0;
		badd64 = slotp->bss_d64_uctr ? (BRIDGE_DEV_D64_BITS & chg) : 0;
	}
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
                             BRIDGE_DEV_BARRIER)) ){
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
#if (DEBUG && PCIBR_DEV_DEBUG)
		printk("pcibr_try_set_device: mod blocked by %R\n", bad, device_bits);
#endif
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
    bridge->b_device[slot].reg = new;
    slotp->bss_device = new;
    bridge->b_wid_tflush;		/* wait until Bridge PIO complete */
    pcibr_unlock(pcibr_soft, s);
#if DEBUG && PCIBR_DEV_DEBUG
    printk("pcibr Device(%d): 0x%p\n", slot, bridge->b_device[slot].reg);
#endif

    return 0;
}

void
pcibr_release_device(pcibr_soft_t pcibr_soft,
		     pciio_slot_t slot,
		     bridgereg_t mask)
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

/*
 * flush write gather buffer for slot
 */
static void
pcibr_device_write_gather_flush(pcibr_soft_t pcibr_soft,
              pciio_slot_t slot)
{
    bridge_t               *bridge;
    unsigned long          s;
    volatile uint32_t     wrf;
    s = pcibr_lock(pcibr_soft);
    bridge = pcibr_soft->bs_base;
    wrf = bridge->b_wr_req_buf[slot].reg;
    pcibr_unlock(pcibr_soft, s);
}

/* =====================================================================
 *    Bridge (pcibr) "Device Driver" entry points
 */


/*
 *    pcibr_init: called once during system startup or
 *      when a loadable driver is loaded.
 *
 *      The driver_register function should normally
 *      be in _reg, not _init.  But the pcibr driver is
 *      required by devinit before the _reg routines
 *      are called, so this is an exception.
 */
void
pcibr_init(void)
{
#if DEBUG && ATTACH_DEBUG
    printk("pcibr_init\n");
#endif

    xwidget_driver_register(XBRIDGE_WIDGET_PART_NUM,
			    XBRIDGE_WIDGET_MFGR_NUM,
			    "pcibr_",
			    0);
    xwidget_driver_register(BRIDGE_WIDGET_PART_NUM,
			    BRIDGE_WIDGET_MFGR_NUM,
			    "pcibr_",
			    0);
}

/*
 * open/close mmap/munmap interface would be used by processes
 * that plan to map the PCI bridge, and muck around with the
 * registers. This is dangerous to do, and will be allowed
 * to a select brand of programs. Typically these are
 * diagnostics programs, or some user level commands we may
 * write to do some weird things.
 * To start with expect them to have root priveleges.
 * We will ask for more later.
 */
/* ARGSUSED */
int
pcibr_open(devfs_handle_t *devp, int oflag, int otyp, cred_t *credp)
{
    return 0;
}

/*ARGSUSED */
int
pcibr_close(devfs_handle_t dev, int oflag, int otyp, cred_t *crp)
{
    return 0;
}

/*ARGSUSED */
int
pcibr_map(devfs_handle_t dev, vhandl_t *vt, off_t off, size_t len, uint prot)
{
    int                     error;
    devfs_handle_t            vhdl = dev_to_vhdl(dev);
    devfs_handle_t            pcibr_vhdl = hwgraph_connectpt_get(vhdl);
    pcibr_soft_t            pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    bridge_t               *bridge = pcibr_soft->bs_base;

    hwgraph_vertex_unref(pcibr_vhdl);

    ASSERT(pcibr_soft);
    len = ctob(btoc(len));		/* Make len page aligned */
    error = v_mapphys(vt, (void *) ((__psunsigned_t) bridge + off), len);

    /*
     * If the offset being mapped corresponds to the flash prom
     * base, and if the mapping succeeds, and if the user
     * has requested the protections to be WRITE, enable the
     * flash prom to be written.
     *
     * XXX- deprecate this in favor of using the
     * real flash driver ...
     */
    if (!error &&
	((off == BRIDGE_EXTERNAL_FLASH) ||
	 (len > BRIDGE_EXTERNAL_FLASH))) {
	int                     s;

	/*
	 * ensure that we write and read without any interruption.
	 * The read following the write is required for the Bridge war
	 */
	s = splhi();
	bridge->b_wid_control |= BRIDGE_CTRL_FLASH_WR_EN;
	bridge->b_wid_control;		/* inval addr bug war */
	splx(s);
    }

    return error;
}

/*ARGSUSED */
int
pcibr_unmap(devfs_handle_t dev, vhandl_t *vt)
{
    devfs_handle_t            pcibr_vhdl = hwgraph_connectpt_get((devfs_handle_t) dev);
    pcibr_soft_t            pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    bridge_t               *bridge = pcibr_soft->bs_base;

    hwgraph_vertex_unref(pcibr_vhdl);

    /*
     * If flashprom write was enabled, disable it, as
     * this is the last unmap.
     */
    if (bridge->b_wid_control & BRIDGE_CTRL_FLASH_WR_EN) {
	int                     s;

	/*
	 * ensure that we write and read without any interruption.
	 * The read following the write is required for the Bridge war
	 */
	s = splhi();
	bridge->b_wid_control &= ~BRIDGE_CTRL_FLASH_WR_EN;
	bridge->b_wid_control;		/* inval addr bug war */
	splx(s);
    }
    return 0;
}

/* This is special case code used by grio. There are plans to make
 * this a bit more general in the future, but till then this should
 * be sufficient.
 */
pciio_slot_t
pcibr_device_slot_get(devfs_handle_t dev_vhdl)
{
    char                    devname[MAXDEVNAME];
    devfs_handle_t            tdev;
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
	    slot = pciio_info_slot_get(pciio_info);
	    break;
	}
	hwgraph_vertex_unref(tdev);
	tdev = hwgraph_connectpt_get(tdev);
    }
    hwgraph_vertex_unref(tdev);

    return slot;
}

/*ARGSUSED */
int
pcibr_ioctl(devfs_handle_t dev,
	    int cmd,
	    void *arg,
	    int flag,
	    struct cred *cr,
	    int *rvalp)
{
    devfs_handle_t            pcibr_vhdl = hwgraph_connectpt_get((devfs_handle_t)dev);
#ifdef LATER
    pcibr_soft_t            pcibr_soft = pcibr_soft_get(pcibr_vhdl);
#endif
    int                     error = 0;

    hwgraph_vertex_unref(pcibr_vhdl);

    switch (cmd) {
#ifdef LATER
    case GIOCSETBW:
	{
	    grio_ioctl_info_t       info;
	    pciio_slot_t            slot = 0;

	    if (!cap_able((uint64_t)CAP_DEVICE_MGT)) {
		error = EPERM;
		break;
	    }
	    if (COPYIN(arg, &info, sizeof(grio_ioctl_info_t))) {
		error = EFAULT;
		break;
	    }
#ifdef GRIO_DEBUG
	    printk("pcibr:: prev_vhdl: %d reqbw: %lld\n",
		    info.prev_vhdl, info.reqbw);
#endif				/* GRIO_DEBUG */

	    if ((slot = pcibr_device_slot_get(info.prev_vhdl)) ==
		PCIIO_SLOT_NONE) {
		error = EIO;
		break;
	    }
	    if (info.reqbw)
		pcibr_priority_bits_set(pcibr_soft, slot, PCI_PRIO_HIGH);
	    break;
	}

    case GIOCRELEASEBW:
	{
	    grio_ioctl_info_t       info;
	    pciio_slot_t            slot = 0;

	    if (!cap_able(CAP_DEVICE_MGT)) {
		error = EPERM;
		break;
	    }
	    if (COPYIN(arg, &info, sizeof(grio_ioctl_info_t))) {
		error = EFAULT;
		break;
	    }
#ifdef GRIO_DEBUG
	    printk("pcibr:: prev_vhdl: %d reqbw: %lld\n",
		    info.prev_vhdl, info.reqbw);
#endif				/* GRIO_DEBUG */

	    if ((slot = pcibr_device_slot_get(info.prev_vhdl)) ==
		PCIIO_SLOT_NONE) {
		error = EIO;
		break;
	    }
	    if (info.reqbw)
		pcibr_priority_bits_set(pcibr_soft, slot, PCI_PRIO_LOW);
	    break;
	}

    case PCIBR_SLOT_STARTUP:
	{
	    struct pcibr_slot_req_s        req;

	    if (!cap_able(CAP_DEVICE_MGT)) {
		error = EPERM;
		break;
	    }

            if (COPYIN(arg, &req, sizeof(req))) {
                error = EFAULT;
                break;
            }

	    error = pcibr_slot_startup(pcibr_vhdl, &req);
	    break;
	}
    case PCIBR_SLOT_SHUTDOWN:
	{
	    struct pcibr_slot_req_s        req;

	    if (!cap_able(CAP_DEVICE_MGT)) {
		error = EPERM;
		break;
	    }

            if (COPYIN(arg, &req, sizeof(req))) {
                error = EFAULT;
                break;
            }

	    error = pcibr_slot_shutdown(pcibr_vhdl, &req);
	    break;
	}
    case PCIBR_SLOT_QUERY:
	{
	    struct pcibr_slot_req_s        req;

	    if (!cap_able(CAP_DEVICE_MGT)) {
		error = EPERM;
		break;
	    }

            if (COPYIN(arg, &req, sizeof(req))) {
                error = EFAULT;
                break;
            }

            error = pcibr_slot_query(pcibr_vhdl, &req);
	    break;
	}
#endif	/* LATER */
    default:
	break;

    }

    return error;
}

void
pcibr_freeblock_sub(iopaddr_t *free_basep,
		    iopaddr_t *free_lastp,
		    iopaddr_t base,
		    size_t size)
{
    iopaddr_t               free_base = *free_basep;
    iopaddr_t               free_last = *free_lastp;
    iopaddr_t               last = base + size - 1;

    if ((last < free_base) || (base > free_last));	/* free block outside arena */

    else if ((base <= free_base) && (last >= free_last))
	/* free block contains entire arena */
	*free_basep = *free_lastp = 0;

    else if (base <= free_base)
	/* free block is head of arena */
	*free_basep = last + 1;

    else if (last >= free_last)
	/* free block is tail of arena */
	*free_lastp = base - 1;

    /*
     * We are left with two regions: the free area
     * in the arena "below" the block, and the free
     * area in the arena "above" the block. Keep
     * the one that is bigger.
     */

    else if ((base - free_base) > (free_last - last))
	*free_lastp = base - 1;		/* keep lower chunk */
    else
	*free_basep = last + 1;		/* keep upper chunk */
}

pcibr_info_t
pcibr_info_get(devfs_handle_t vhdl)
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

    NEW(pcibr_info);

    pciio_device_info_new(&pcibr_info->f_c,
			  pcibr_soft->bs_vhdl,
			  slot, rfunc,
			  vendor, device);

/* pfg - this is new ..... */
    /* Set PCI bus number */
    pcibr_info->f_bus = io_path_map_widget(pcibr_soft->bs_vhdl);

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


/* FIXME:  for now this is needed by both pcibr.c and
 * pcibr_slot.c.  Need to find a better way, the least
 * of which would be to move it to pcibr_private.h
 */

/*
 * PCI_ADDR_SPACE_LIMITS_STORE
 *	Sets the current values of
 *		pci io base, 
 *		pci io last,
 *		pci low memory base,
 *		pci low memory last,
 *		pci high memory base,
 * 		pci high memory last
 */
#define PCI_ADDR_SPACE_LIMITS_STORE()			\
    pcibr_soft->bs_spinfo.pci_io_base = pci_io_fb;	\
    pcibr_soft->bs_spinfo.pci_io_last = pci_io_fl;	\
    pcibr_soft->bs_spinfo.pci_swin_base = pci_lo_fb;	\
    pcibr_soft->bs_spinfo.pci_swin_last = pci_lo_fl;	\
    pcibr_soft->bs_spinfo.pci_mem_base = pci_hi_fb;	\
    pcibr_soft->bs_spinfo.pci_mem_last = pci_hi_fl;


/*
 * pcibr_device_unregister
 *	This frees up any hardware resources reserved for this PCI device
 * 	and removes any PCI infrastructural information setup for it.
 *	This is usually used at the time of shutting down of the PCI card.
 */
int
pcibr_device_unregister(devfs_handle_t pconn_vhdl)
{
    pciio_info_t	 pciio_info;
    devfs_handle_t	 pcibr_vhdl;
    pciio_slot_t	 slot;
    pcibr_soft_t	 pcibr_soft;
    bridge_t		*bridge;
    int                  count_vchan0, count_vchan1;
    unsigned             s;
    int			 error_call;
    int			 error = 0;

    pciio_info = pciio_info_get(pconn_vhdl);

    pcibr_vhdl = pciio_info_master_get(pciio_info);
    slot = pciio_info_slot_get(pciio_info);

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    bridge = pcibr_soft->bs_base;

    /* Clear all the hardware xtalk resources for this device */
    xtalk_widgetdev_shutdown(pcibr_soft->bs_conn, slot);

    /* Flush all the rrbs */
    pcibr_rrb_flush(pconn_vhdl);

    /*
     * If the RRB configuration for this slot has changed, set it 
     * back to the boot-time default
     */
    if (pcibr_soft->bs_rrb_valid_dflt[slot] >= 0) {

        s = pcibr_lock(pcibr_soft);

        /* Free the rrbs allocated to this slot */
        error_call = do_pcibr_rrb_free(bridge, slot, 
		                       pcibr_soft->bs_rrb_valid[slot] +
		                       pcibr_soft->bs_rrb_valid[slot + 
                                       PCIBR_RRB_SLOT_VIRTUAL]);

        if (error_call)
            error = ERANGE;
   
         pcibr_soft->bs_rrb_res[slot] = pcibr_soft->bs_rrb_res[slot] +
                                        pcibr_soft->bs_rrb_valid[slot] +
                                        pcibr_soft->bs_rrb_valid[slot +
                                        PCIBR_RRB_SLOT_VIRTUAL];

        count_vchan0 = pcibr_soft->bs_rrb_valid_dflt[slot];
        count_vchan1 = pcibr_soft->bs_rrb_valid_dflt[slot +
                                                     PCIBR_RRB_SLOT_VIRTUAL];

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

    return(error);
    
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
pcibr_driver_reg_callback(devfs_handle_t pconn_vhdl,
			  int key1, int key2, int error)
{
    pciio_info_t	 pciio_info;
    pcibr_info_t         pcibr_info;
    devfs_handle_t	 pcibr_vhdl;
    pciio_slot_t	 slot;
    pcibr_soft_t	 pcibr_soft;

    /* Do not set slot status for vendor/device ID wildcard drivers */
    if ((key1 == -1) || (key2 == -1))
        return;

    pciio_info = pciio_info_get(pconn_vhdl);
    pcibr_info = pcibr_info_get(pconn_vhdl);

    pcibr_vhdl = pciio_info_master_get(pciio_info);
    slot = pciio_info_slot_get(pciio_info);

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    /* This may be a loadable driver so lock out any pciconfig actions */
    mrlock(pcibr_soft->bs_bus_lock, MR_UPDATE, PZERO);

    pcibr_info->f_att_det_error = error;

    pcibr_soft->bs_slot[slot].slot_status &= ~SLOT_STATUS_MASK;

    if (error) {
        pcibr_soft->bs_slot[slot].slot_status |= SLOT_STARTUP_INCMPLT;
    } else {
        pcibr_soft->bs_slot[slot].slot_status |= SLOT_STARTUP_CMPLT;
    }
        
    /* Release the bus lock */
    mrunlock(pcibr_soft->bs_bus_lock);

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
pcibr_driver_unreg_callback(devfs_handle_t pconn_vhdl, 
                            int key1, int key2, int error)
{
    pciio_info_t	 pciio_info;
    pcibr_info_t         pcibr_info;
    devfs_handle_t	 pcibr_vhdl;
    pciio_slot_t	 slot;
    pcibr_soft_t	 pcibr_soft;

    /* Do not set slot status for vendor/device ID wildcard drivers */
    if ((key1 == -1) || (key2 == -1))
        return;

    pciio_info = pciio_info_get(pconn_vhdl);
    pcibr_info = pcibr_info_get(pconn_vhdl);

    pcibr_vhdl = pciio_info_master_get(pciio_info);
    slot = pciio_info_slot_get(pciio_info);

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    /* This may be a loadable driver so lock out any pciconfig actions */
    mrlock(pcibr_soft->bs_bus_lock, MR_UPDATE, PZERO);

    pcibr_info->f_att_det_error = error;

    pcibr_soft->bs_slot[slot].slot_status &= ~SLOT_STATUS_MASK;

    if (error) {
        pcibr_soft->bs_slot[slot].slot_status |= SLOT_SHUTDOWN_INCMPLT;
    } else {
        pcibr_soft->bs_slot[slot].slot_status |= SLOT_SHUTDOWN_CMPLT;
    }
        
    /* Release the bus lock */
    mrunlock(pcibr_soft->bs_bus_lock);

}

/* 
 * build a convenience link path in the
 * form of ".../<iobrick>/bus/<busnum>"
 * 
 * returns 1 on success, 0 otherwise
 *
 * depends on hwgraph separator == '/'
 */
int
pcibr_bus_cnvlink(devfs_handle_t f_c, int slot)
{
        char dst[MAXDEVNAME];
	char *dp = dst;
        char *cp, *xp;
        int widgetnum;
        char pcibus[8];
	devfs_handle_t nvtx, svtx;
	int rv;

#if DEBUG
	printk("pcibr_bus_cnvlink: slot= %d f_c= %p\n", 
		slot, f_c);
	{
		int pos;
		char dname[256];
		pos = devfs_generate_path(f_c, dname, 256);
		printk("%s : path= %s\n", __FUNCTION__, &dname[pos]);
	}
#endif

	if (GRAPH_SUCCESS != hwgraph_vertex_name_get(f_c, dst, MAXDEVNAME))
		return 0;

	/* dst example == /hw/module/001c02/Pbrick/xtalk/8/pci/direct */

	/* find the widget number */
	xp = strstr(dst, "/"EDGE_LBL_XTALK"/");
	if (xp == NULL)
		return 0;
	widgetnum = atoi(xp+7);
	if (widgetnum < XBOW_PORT_8 || widgetnum > XBOW_PORT_F)
		return 0;

	/* remove "/pci/direct" from path */
	cp = strstr(dst, "/" EDGE_LBL_PCI "/" "direct");
	if (cp == NULL)
		return 0;
	*cp = (char)NULL;

	/* get the vertex for the widget */
	if (GRAPH_SUCCESS != hwgraph_traverse(NULL, dp, &svtx))	
		return 0;

	*xp = (char)NULL;		/* remove "/xtalk/..." from path */

	/* dst example now == /hw/module/001c02/Pbrick */

	/* get the bus number */
        strcat(dst, "/bus");
        sprintf(pcibus, "%d", p_busnum[widgetnum]);

	/* link to bus to widget */
	rv = hwgraph_path_add(NULL, dp, &nvtx);
	if (GRAPH_SUCCESS == rv)
		rv = hwgraph_edge_add(nvtx, svtx, pcibus);

	return (rv == GRAPH_SUCCESS);
}


/*
 *    pcibr_attach: called every time the crosstalk
 *      infrastructure is asked to initialize a widget
 *      that matches the part number we handed to the
 *      registration routine above.
 */
/*ARGSUSED */
int
pcibr_attach(devfs_handle_t xconn_vhdl)
{
    /* REFERENCED */
    graph_error_t           rc;
    devfs_handle_t            pcibr_vhdl;
    devfs_handle_t            ctlr_vhdl;
    bridge_t               *bridge = NULL;
    bridgereg_t             id;
    int                     rev;
    pcibr_soft_t            pcibr_soft;
    pcibr_info_t            pcibr_info;
    xwidget_info_t          info;
    xtalk_intr_t            xtalk_intr;
    device_desc_t           dev_desc = (device_desc_t)0;
    int                     slot;
    int                     ibit;
    devfs_handle_t            noslot_conn;
    char                    devnm[MAXDEVNAME], *s;
    pcibr_hints_t           pcibr_hints;
    bridgereg_t             b_int_enable;
    unsigned                rrb_fixed = 0;

    iopaddr_t               pci_io_fb, pci_io_fl;
    iopaddr_t               pci_lo_fb, pci_lo_fl;
    iopaddr_t               pci_hi_fb, pci_hi_fl;

    int                     spl_level;
#ifdef LATER
    char		    *nicinfo = (char *)0;
#endif

#if PCI_FBBE
    int                     fast_back_to_back_enable;
#endif
    l1sc_t		    *scp;
    nasid_t		    nasid;

    async_attach_t          aa = NULL;

    aa = async_attach_get_info(xconn_vhdl);

#if DEBUG && ATTACH_DEBUG
    printk("pcibr_attach: xconn_vhdl=  %p\n", xconn_vhdl);
    {
	int pos;
	char dname[256];
	pos = devfs_generate_path(xconn_vhdl, dname, 256);
	printk("%s : path= %s \n", __FUNCTION__, &dname[pos]);
    }
#endif

    /* Setup the PRB for the bridge in CONVEYOR BELT
     * mode. PRBs are setup in default FIRE-AND-FORGET
     * mode during the initialization.
     */
    hub_device_flags_set(xconn_vhdl, HUB_PIO_CONVEYOR);

    bridge = (bridge_t *)
	xtalk_piotrans_addr(xconn_vhdl, NULL,
			    0, sizeof(bridge_t), 0);

    /*
     * Create the vertex for the PCI bus, which we
     * will also use to hold the pcibr_soft and
     * which will be the "master" vertex for all the
     * pciio connection points we will hang off it.
     * This needs to happen before we call nic_bridge_vertex_info
     * as we are some of the *_vmc functions need access to the edges.
     *
     * Opening this vertex will provide access to
     * the Bridge registers themselves.
     */
    rc = hwgraph_path_add(xconn_vhdl, EDGE_LBL_PCI, &pcibr_vhdl);
    ASSERT(rc == GRAPH_SUCCESS);

    ctlr_vhdl = NULL;
    ctlr_vhdl = hwgraph_register(pcibr_vhdl, EDGE_LBL_CONTROLLER,
                0, DEVFS_FL_AUTO_DEVNUM,
                0, 0,
                S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP, 0, 0,
                &pcibr_fops, NULL);

    ASSERT(ctlr_vhdl != NULL);

    /*
     * decode the nic, and hang its stuff off our
     * connection point where other drivers can get
     * at it.
     */
#ifdef LATER
    nicinfo = BRIDGE_VERTEX_MFG_INFO(xconn_vhdl, (nic_data_t) & bridge->b_nic);
#endif

    /*
     * Get the hint structure; if some NIC callback
     * marked this vertex as "hands-off" then we
     * just return here, before doing anything else.
     */
    pcibr_hints = pcibr_hints_get(xconn_vhdl, 0);

    if (pcibr_hints && pcibr_hints->ph_hands_off)
	return -1;			/* generic operations disabled */

    id = bridge->b_wid_id;
    rev = XWIDGET_PART_REV_NUM(id);

    hwgraph_info_add_LBL(pcibr_vhdl, INFO_LBL_PCIBR_ASIC_REV, (arbitrary_info_t) rev);

    /*
     * allocate soft state structure, fill in some
     * fields, and hook it up to our vertex.
     */
    NEW(pcibr_soft);
    BZERO(pcibr_soft, sizeof *pcibr_soft);
    pcibr_soft_set(pcibr_vhdl, pcibr_soft);

    pcibr_soft->bs_conn = xconn_vhdl;
    pcibr_soft->bs_vhdl = pcibr_vhdl;
    pcibr_soft->bs_base = bridge;
    pcibr_soft->bs_rev_num = rev;
    pcibr_soft->bs_intr_bits = pcibr_intr_bits;
    if (is_xbridge(bridge)) {
	pcibr_soft->bs_int_ate_size = XBRIDGE_INTERNAL_ATES;
	pcibr_soft->bs_xbridge = 1;
    } else {
	pcibr_soft->bs_int_ate_size = BRIDGE_INTERNAL_ATES;
	pcibr_soft->bs_xbridge = 0;
    }

    nasid = NASID_GET(bridge);
    scp = &NODEPDA( NASID_TO_COMPACT_NODEID(nasid) )->module->elsc;
    pcibr_soft->bs_l1sc = scp;
    pcibr_soft->bs_moduleid = iobrick_module_get(scp);
    pcibr_soft->bsi_err_intr = 0;

    /* Bridges up through REV C
     * are unable to set the direct
     * byteswappers to BYTE_STREAM.
     */
    if (pcibr_soft->bs_rev_num <= BRIDGE_PART_REV_C) {
	pcibr_soft->bs_pio_end_io = PCIIO_WORD_VALUES;
	pcibr_soft->bs_pio_end_mem = PCIIO_WORD_VALUES;
    }
#if PCIBR_SOFT_LIST
    {
	pcibr_list_p            self;

	NEW(self);
	self->bl_soft = pcibr_soft;
	self->bl_vhdl = pcibr_vhdl;
	self->bl_next = pcibr_list;
	self->bl_next = swap_ptr((void **) &pcibr_list, (void *)self);
    }
#endif

    /*
     * get the name of this bridge vertex and keep the info. Use this
     * only where it is really needed now: like error interrupts.
     */
    s = dev_to_name(pcibr_vhdl, devnm, MAXDEVNAME);
    pcibr_soft->bs_name = kmalloc(strlen(s) + 1, GFP_KERNEL);
    strcpy(pcibr_soft->bs_name, s);

#if SHOW_REVS || DEBUG
#if !DEBUG
    if (kdebug)
#endif
	printk("%sBridge ASIC: rev %s (code=0x%x) at %s\n",
		is_xbridge(bridge) ? "X" : "",
		(rev == BRIDGE_PART_REV_A) ? "A" :
		(rev == BRIDGE_PART_REV_B) ? "B" :
		(rev == BRIDGE_PART_REV_C) ? "C" :
		(rev == BRIDGE_PART_REV_D) ? "D" :
		(rev == XBRIDGE_PART_REV_A) ? "A" :
		(rev == XBRIDGE_PART_REV_B) ? "B" :
		"unknown",
		rev, pcibr_soft->bs_name);
#endif

    info = xwidget_info_get(xconn_vhdl);
    pcibr_soft->bs_xid = xwidget_info_id_get(info);
    pcibr_soft->bs_master = xwidget_info_master_get(info);
    pcibr_soft->bs_mxid = xwidget_info_masterid_get(info);

    /*
     * Init bridge lock.
     */
    spin_lock_init(&pcibr_soft->bs_lock);

    /*
     * If we have one, process the hints structure.
     */
    if (pcibr_hints) {
	rrb_fixed = pcibr_hints->ph_rrb_fixed;

	pcibr_soft->bs_rrb_fixed = rrb_fixed;

	if (pcibr_hints->ph_intr_bits)
	    pcibr_soft->bs_intr_bits = pcibr_hints->ph_intr_bits;

	for (slot = 0; slot < 8; ++slot) {
	    int                     hslot = pcibr_hints->ph_host_slot[slot] - 1;

	    if (hslot < 0) {
		pcibr_soft->bs_slot[slot].host_slot = slot;
	    } else {
		pcibr_soft->bs_slot[slot].has_host = 1;
		pcibr_soft->bs_slot[slot].host_slot = hslot;
	    }
	}
    }
    /*
     * set up initial values for state fields
     */
    for (slot = 0; slot < 8; ++slot) {
	pcibr_soft->bs_slot[slot].bss_devio.bssd_space = PCIIO_SPACE_NONE;
	pcibr_soft->bs_slot[slot].bss_d64_base = PCIBR_D64_BASE_UNSET;
	pcibr_soft->bs_slot[slot].bss_d32_base = PCIBR_D32_BASE_UNSET;
	pcibr_soft->bs_slot[slot].bss_ext_ates_active = ATOMIC_INIT(0);
    }

    for (ibit = 0; ibit < 8; ++ibit) {
	pcibr_soft->bs_intr[ibit].bsi_xtalk_intr = 0;
	pcibr_soft->bs_intr[ibit].bsi_pcibr_intr_wrap.iw_soft = pcibr_soft;
	pcibr_soft->bs_intr[ibit].bsi_pcibr_intr_wrap.iw_list = NULL;
	pcibr_soft->bs_intr[ibit].bsi_pcibr_intr_wrap.iw_stat = 
							&(bridge->b_int_status);
	pcibr_soft->bs_intr[ibit].bsi_pcibr_intr_wrap.iw_hdlrcnt = 0;
	pcibr_soft->bs_intr[ibit].bsi_pcibr_intr_wrap.iw_shared = 0;
	pcibr_soft->bs_intr[ibit].bsi_pcibr_intr_wrap.iw_connected = 0;
    }

    /*
     * Initialize various Bridge registers.
     */

    /*
     * On pre-Rev.D bridges, set the PCI_RETRY_CNT
     * to zero to avoid dropping stores. (#475347)
     */
    if (rev < BRIDGE_PART_REV_D)
	bridge->b_bus_timeout &= ~BRIDGE_BUS_PCI_RETRY_MASK;

    /*
     * Clear all pending interrupts.
     */
    bridge->b_int_rst_stat = (BRIDGE_IRR_ALL_CLR);

    /*
     * Until otherwise set up,
     * assume all interrupts are
     * from slot 7.
     */
    bridge->b_int_device = (uint32_t) 0xffffffff;

    {
	bridgereg_t             dirmap;
	paddr_t                 paddr;
	iopaddr_t               xbase;
	xwidgetnum_t            xport;
	iopaddr_t               offset;
	int                     num_entries = 0;
	int                     entry;
	cnodeid_t		cnodeid;
	nasid_t			nasid;

	/* Set the Bridge's 32-bit PCI to XTalk
	 * Direct Map register to the most useful
	 * value we can determine.  Note that we
	 * must use a single xid for all of:
	 *      direct-mapped 32-bit DMA accesses
	 *      direct-mapped 64-bit DMA accesses
	 *      DMA accesses through the PMU
	 *      interrupts
	 * This is the only way to guarantee that
	 * completion interrupts will reach a CPU
	 * after all DMA data has reached memory.
	 * (Of course, there may be a few special
	 * drivers/controlers that explicitly manage
	 * this ordering problem.)
	 */

	cnodeid = 0;  /* default node id */
	nasid = COMPACT_TO_NASID_NODEID(cnodeid);
	paddr = NODE_OFFSET(nasid) + 0;

	/* currently, we just assume that if we ask
	 * for a DMA mapping to "zero" the XIO
	 * host will transmute this into a request
	 * for the lowest hunk of memory.
	 */
	xbase = xtalk_dmatrans_addr(xconn_vhdl, 0,
				    paddr, _PAGESZ, 0);

	if (xbase != XIO_NOWHERE) {
	    if (XIO_PACKED(xbase)) {
		xport = XIO_PORT(xbase);
		xbase = XIO_ADDR(xbase);
	    } else
		xport = pcibr_soft->bs_mxid;

	    offset = xbase & ((1ull << BRIDGE_DIRMAP_OFF_ADDRSHFT) - 1ull);
	    xbase >>= BRIDGE_DIRMAP_OFF_ADDRSHFT;

	    dirmap = xport << BRIDGE_DIRMAP_W_ID_SHFT;

	    if (xbase)
		dirmap |= BRIDGE_DIRMAP_OFF & xbase;
	    else if (offset >= (512 << 20))
		dirmap |= BRIDGE_DIRMAP_ADD512;

	    bridge->b_dir_map = dirmap;
	}
	/*
	 * Set bridge's idea of page size according to the system's
	 * idea of "IO page size".  TBD: The idea of IO page size
	 * should really go away.
	 */
	/*
	 * ensure that we write and read without any interruption.
	 * The read following the write is required for the Bridge war
	 */
	spl_level = splhi();
#if IOPGSIZE == 4096
	bridge->b_wid_control &= ~BRIDGE_CTRL_PAGE_SIZE;
#elif IOPGSIZE == 16384
	bridge->b_wid_control |= BRIDGE_CTRL_PAGE_SIZE;
#else
	<<<Unable to deal with IOPGSIZE >>>;
#endif
	bridge->b_wid_control;		/* inval addr bug war */
	splx(spl_level);

	/* Initialize internal mapping entries */
	for (entry = 0; entry < pcibr_soft->bs_int_ate_size; entry++) {
	    bridge->b_int_ate_ram[entry].wr = 0;
	}

	/*
	 * Determine if there's external mapping SSRAM on this
	 * bridge.  Set up Bridge control register appropriately,
	 * inititlize SSRAM, and set software up to manage RAM
	 * entries as an allocatable resource.
	 *
	 * Currently, we just use the rm* routines to manage ATE
	 * allocation.  We should probably replace this with a
	 * Best Fit allocator.
	 *
	 * For now, if we have external SSRAM, avoid using
	 * the internal ssram: we can't turn PREFETCH on
	 * when we use the internal SSRAM; and besides,
	 * this also guarantees that no allocation will
	 * straddle the internal/external line, so we
	 * can increment ATE write addresses rather than
	 * recomparing against BRIDGE_INTERNAL_ATES every
	 * time.
	 */
	if (is_xbridge(bridge))
		num_entries = 0;
	else
		num_entries = pcibr_init_ext_ate_ram(bridge);

	/* we always have 128 ATEs (512 for Xbridge) inside the chip
	 * even if disabled for debugging.
	 */
	pcibr_soft->bs_int_ate_map = rmallocmap(pcibr_soft->bs_int_ate_size);
	pcibr_ate_free(pcibr_soft, 0, pcibr_soft->bs_int_ate_size);
#if PCIBR_ATE_DEBUG
	printk("pcibr_attach: %d INTERNAL ATEs\n", pcibr_soft->bs_int_ate_size);
#endif

	if (num_entries > pcibr_soft->bs_int_ate_size) {
#if PCIBR_ATE_NOTBOTH			/* for debug -- forces us to use external ates */
	    printk("pcibr_attach: disabling internal ATEs.\n");
	    pcibr_ate_alloc(pcibr_soft, pcibr_soft->bs_int_ate_size);
#endif
	    pcibr_soft->bs_ext_ate_map = rmallocmap(num_entries);
	    pcibr_ate_free(pcibr_soft, pcibr_soft->bs_int_ate_size,
			   num_entries - pcibr_soft->bs_int_ate_size);
#if PCIBR_ATE_DEBUG
	    printk("pcibr_attach: %d EXTERNAL ATEs\n",
		    num_entries - pcibr_soft->bs_int_ate_size);
#endif
	}
    }

    {
	bridgereg_t             dirmap;
	iopaddr_t               xbase;

	/*
	 * now figure the *real* xtalk base address
	 * that dirmap sends us to.
	 */
	dirmap = bridge->b_dir_map;
	if (dirmap & BRIDGE_DIRMAP_OFF)
	    xbase = (iopaddr_t)(dirmap & BRIDGE_DIRMAP_OFF)
			<< BRIDGE_DIRMAP_OFF_ADDRSHFT;
	else if (dirmap & BRIDGE_DIRMAP_ADD512)
	    xbase = 512 << 20;
	else
	    xbase = 0;

	pcibr_soft->bs_dir_xbase = xbase;

	/* it is entirely possible that we may, at this
	 * point, have our dirmap pointing somewhere
	 * other than our "master" port.
	 */
	pcibr_soft->bs_dir_xport =
	    (dirmap & BRIDGE_DIRMAP_W_ID) >> BRIDGE_DIRMAP_W_ID_SHFT;
    }

    /* pcibr sources an error interrupt;
     * figure out where to send it.
     *
     * If any interrupts are enabled in bridge,
     * then the prom set us up and our interrupt
     * has already been reconnected in mlreset
     * above.
     *
     * Need to set the D_INTR_ISERR flag
     * in the dev_desc used for allocating the
     * error interrupt, so our interrupt will
     * be properly routed and prioritized.
     *
     * If our crosstalk provider wants to
     * fix widget error interrupts to specific
     * destinations, D_INTR_ISERR is how it
     * knows to do this.
     */

    xtalk_intr = xtalk_intr_alloc(xconn_vhdl, dev_desc, pcibr_vhdl);
    ASSERT(xtalk_intr != NULL);

    pcibr_soft->bsi_err_intr = xtalk_intr;

    /*
     * On IP35 with XBridge, we do some extra checks in pcibr_setwidint
     * in order to work around some addressing limitations.  In order
     * for that fire wall to work properly, we need to make sure we
     * start from a known clean state.
     */
    pcibr_clearwidint(bridge);

    xtalk_intr_connect(xtalk_intr, (xtalk_intr_setfunc_t)pcibr_setwidint, (void *)bridge);

    /*
     * now we can start handling error interrupts;
     * enable all of them.
     * NOTE: some PCI ints may already be enabled.
     */
    b_int_enable = bridge->b_int_enable | BRIDGE_ISR_ERRORS;


    bridge->b_int_enable = b_int_enable;
    bridge->b_int_mode = 0;		/* do not send "clear interrupt" packets */

    bridge->b_wid_tflush;		/* wait until Bridge PIO complete */

    /*
     * Depending on the rev of bridge, disable certain features.
     * Easiest way seems to be to force the PCIBR_NOwhatever
     * flag to be on for all DMA calls, which overrides any
     * PCIBR_whatever flag or even the setting of whatever
     * from the PCIIO_DMA_class flags (or even from the other
     * PCIBR flags, since NO overrides YES).
     */
    pcibr_soft->bs_dma_flags = 0;

    /* PREFETCH:
     * Always completely disabled for REV.A;
     * at "pcibr_prefetch_enable_rev", anyone
     * asking for PCIIO_PREFETCH gets it.
     * Between these two points, you have to ask
     * for PCIBR_PREFETCH, which promises that
     * your driver knows about known Bridge WARs.
     */
    if (pcibr_soft->bs_rev_num < BRIDGE_PART_REV_B)
	pcibr_soft->bs_dma_flags |= PCIBR_NOPREFETCH;
    else if (pcibr_soft->bs_rev_num < 
		(BRIDGE_WIDGET_PART_NUM << 4 | pcibr_prefetch_enable_rev))
	pcibr_soft->bs_dma_flags |= PCIIO_NOPREFETCH;

    /* WRITE_GATHER:
     * Disabled up to but not including the
     * rev number in pcibr_wg_enable_rev. There
     * is no "WAR range" as with prefetch.
     */
    if (pcibr_soft->bs_rev_num < 
		(BRIDGE_WIDGET_PART_NUM << 4 | pcibr_wg_enable_rev))
	pcibr_soft->bs_dma_flags |= PCIBR_NOWRITE_GATHER;

    pciio_provider_register(pcibr_vhdl, &pcibr_provider);
    pciio_provider_startup(pcibr_vhdl);

    pci_io_fb = 0x00000004;		/* I/O FreeBlock Base */
    pci_io_fl = 0xFFFFFFFF;		/* I/O FreeBlock Last */

    pci_lo_fb = 0x00000010;		/* Low Memory FreeBlock Base */
    pci_lo_fl = 0x001FFFFF;		/* Low Memory FreeBlock Last */

    pci_hi_fb = 0x00200000;		/* High Memory FreeBlock Base */
    pci_hi_fl = 0x3FFFFFFF;		/* High Memory FreeBlock Last */


    PCI_ADDR_SPACE_LIMITS_STORE();

    /* build "no-slot" connection point
     */
    pcibr_info = pcibr_device_info_new
	(pcibr_soft, PCIIO_SLOT_NONE, PCIIO_FUNC_NONE,
	 PCIIO_VENDOR_ID_NONE, PCIIO_DEVICE_ID_NONE);
    noslot_conn = pciio_device_info_register
	(pcibr_vhdl, &pcibr_info->f_c);

    /* Remember the no slot connection point info for tearing it
     * down during detach.
     */
    pcibr_soft->bs_noslot_conn = noslot_conn;
    pcibr_soft->bs_noslot_info = pcibr_info;
#if PCI_FBBE
    fast_back_to_back_enable = 1;
#endif

#if PCI_FBBE
    if (fast_back_to_back_enable) {
	/*
	 * All devices on the bus are capable of fast back to back, so
	 * we need to set the fast back to back bit in all devices on
	 * the bus that are capable of doing such accesses.
	 */
    }
#endif

#ifdef LATER
    /* If the bridge has been reset then there is no need to reset
     * the individual PCI slots.
     */
    for (slot = 0; slot < 8; ++slot)  
	/* Reset all the slots */
	(void)pcibr_slot_reset(pcibr_vhdl, slot);
#endif

    for (slot = 0; slot < 8; ++slot)
	/* Find out what is out there */
	(void)pcibr_slot_info_init(pcibr_vhdl,slot);

    for (slot = 0; slot < 8; ++slot)  
	/* Set up the address space for this slot in the pci land */
	(void)pcibr_slot_addr_space_init(pcibr_vhdl,slot);

    for (slot = 0; slot < 8; ++slot)  
	/* Setup the device register */
	(void)pcibr_slot_device_init(pcibr_vhdl, slot);

    for (slot = 0; slot < 8; ++slot)  
	/* Setup host/guest relations */
	(void)pcibr_slot_guest_info_init(pcibr_vhdl,slot);

    for (slot = 0; slot < 8; ++slot)  
	/* Initial RRB management */
	(void)pcibr_slot_initial_rrb_alloc(pcibr_vhdl,slot);

    /* driver attach routines should be called out from generic linux code */
    for (slot = 0; slot < 8; ++slot)  
	/* Call the device attach */
	(void)pcibr_slot_call_device_attach(pcibr_vhdl, slot, 0);

    /*
     * Each Pbrick PCI bus only has slots 1 and 2.   Similarly for
     * widget 0xe on Ibricks.  Allocate RRB's accordingly.
     */
    if (pcibr_soft->bs_moduleid > 0) {
	switch (MODULE_GET_BTCHAR(pcibr_soft->bs_moduleid)) {
	case 'p':		/* Pbrick */
		do_pcibr_rrb_autoalloc(pcibr_soft, 1, 8);
		do_pcibr_rrb_autoalloc(pcibr_soft, 2, 8);
		break;
	case 'i':		/* Ibrick */
	  	/* port 0xe on the Ibrick only has slots 1 and 2 */
		if (pcibr_soft->bs_xid == 0xe) {
			do_pcibr_rrb_autoalloc(pcibr_soft, 1, 8);
			do_pcibr_rrb_autoalloc(pcibr_soft, 2, 8);
		}
		else {
		    	/* allocate one RRB for the serial port */
			do_pcibr_rrb_autoalloc(pcibr_soft, 0, 1);
		}
		break;
	} /* switch */
    }

#ifdef LATER
    if (strstr(nicinfo, XTALK_PCI_PART_NUM)) {
	do_pcibr_rrb_autoalloc(pcibr_soft, 1, 8);
#if PCIBR_RRB_DEBUG
	printf("\n\nFound XTALK_PCI (030-1275) at %v\n", xconn_vhdl);

	printf("pcibr_attach: %v Shoebox RRB MANAGEMENT: %d+%d free\n",
		pcibr_vhdl,
		pcibr_soft->bs_rrb_avail[0],
		pcibr_soft->bs_rrb_avail[1]);

	for (slot = 0; slot < 8; ++slot)
	    printf("\t%d+%d+%d",
	    0xFFF & pcibr_soft->bs_rrb_valid[slot],
	    0xFFF & pcibr_soft->bs_rrb_valid[slot + PCIBR_RRB_SLOT_VIRTUAL],
	    pcibr_soft->bs_rrb_res[slot]);

	printf("\n");
#endif
    }
#else
	FIXME("pcibr_attach: Call do_pcibr_rrb_autoalloc nicinfo\n");
#endif

    if (aa)
	    async_attach_add_info(noslot_conn, aa);

    pciio_device_attach(noslot_conn, 0);


    /* 
     * Tear down pointer to async attach info -- async threads for
     * bridge's descendants may be running but the bridge's work is done.
     */
    if (aa)
	    async_attach_del_info(xconn_vhdl);

    return 0;
}
/*
 * pcibr_detach:
 *	Detach the bridge device from the hwgraph after cleaning out all the 
 *	underlying vertices.
 */
int
pcibr_detach(devfs_handle_t xconn)
{
    pciio_slot_t	slot;
    devfs_handle_t	pcibr_vhdl;
    pcibr_soft_t	pcibr_soft;
    bridge_t		*bridge;

    /* Get the bridge vertex from its xtalk connection point */
    if (hwgraph_traverse(xconn, EDGE_LBL_PCI, &pcibr_vhdl) != GRAPH_SUCCESS)
	return(1);

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    bridge = pcibr_soft->bs_base;

    /* Disable the interrupts from the bridge */
    bridge->b_int_enable = 0;

    /* Detach all the PCI devices talking to this bridge */
    for(slot = 0; slot < 8; slot++) {
#ifdef DEBUG
	printk("pcibr_device_detach called for %p/%d\n",
		pcibr_vhdl,slot);
#endif
	pcibr_slot_detach(pcibr_vhdl, slot, 0);
    }

    /* Unregister the no-slot connection point */
    pciio_device_info_unregister(pcibr_vhdl,
				 &(pcibr_soft->bs_noslot_info->f_c));

    spin_lock_destroy(&pcibr_soft->bs_lock);
    kfree(pcibr_soft->bs_name);
    
    /* Error handler gets unregistered when the widget info is 
     * cleaned 
     */
    /* Free the soft ATE maps */
    if (pcibr_soft->bs_int_ate_map)
	rmfreemap(pcibr_soft->bs_int_ate_map);
    if (pcibr_soft->bs_ext_ate_map)
	rmfreemap(pcibr_soft->bs_ext_ate_map);

    /* Disconnect the error interrupt and free the xtalk resources 
     * associated with it.
     */
    xtalk_intr_disconnect(pcibr_soft->bsi_err_intr);
    xtalk_intr_free(pcibr_soft->bsi_err_intr);

    /* Clear the software state maintained by the bridge driver for this
     * bridge.
     */
    DEL(pcibr_soft);
    /* Remove the Bridge revision labelled info */
    (void)hwgraph_info_remove_LBL(pcibr_vhdl, INFO_LBL_PCIBR_ASIC_REV, NULL);
    /* Remove the character device associated with this bridge */
    (void)hwgraph_edge_remove(pcibr_vhdl, EDGE_LBL_CONTROLLER, NULL);
    /* Remove the PCI bridge vertex */
    (void)hwgraph_edge_remove(xconn, EDGE_LBL_PCI, NULL);

    return(0);
}

int
pcibr_asic_rev(devfs_handle_t pconn_vhdl)
{
    devfs_handle_t            pcibr_vhdl;
    arbitrary_info_t        ainfo;

    if (GRAPH_SUCCESS !=
	hwgraph_traverse(pconn_vhdl, EDGE_LBL_MASTER, &pcibr_vhdl))
	return -1;

    if (GRAPH_SUCCESS !=
	hwgraph_info_get_LBL(pcibr_vhdl, INFO_LBL_PCIBR_ASIC_REV, &ainfo))
	return -1;

    return (int) ainfo;
}

int
pcibr_write_gather_flush(devfs_handle_t pconn_vhdl)
{
    pciio_info_t  pciio_info = pciio_info_get(pconn_vhdl);
    pcibr_soft_t  pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    pciio_slot_t  slot;
    slot = pciio_info_slot_get(pciio_info);
    pcibr_device_write_gather_flush(pcibr_soft, slot);
    return 0;
}

/* =====================================================================
 *    PIO MANAGEMENT
 */

static iopaddr_t
pcibr_addr_pci_to_xio(devfs_handle_t pconn_vhdl,
		      pciio_slot_t slot,
		      pciio_space_t space,
		      iopaddr_t pci_addr,
		      size_t req_size,
		      unsigned flags)
{
    pcibr_info_t            pcibr_info = pcibr_info_get(pconn_vhdl);
    pciio_info_t            pciio_info = &pcibr_info->f_c;
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    bridge_t               *bridge = pcibr_soft->bs_base;

    unsigned                bar;	/* which BASE reg on device is decoding */
    iopaddr_t               xio_addr = XIO_NOWHERE;

    pciio_space_t           wspace;	/* which space device is decoding */
    iopaddr_t               wbase;	/* base of device decode on PCI */
    size_t                  wsize;	/* size of device decode on PCI */

    int                     try;	/* DevIO(x) window scanning order control */
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
	    xio_addr = pci_addr + BRIDGE_TYPE0_CFG_DEV(slot);

	goto done;
    }
    if (space == PCIIO_SPACE_ROM) {
	/* PIO to the Expansion Rom.
	 * Driver is responsible for
	 * enabling and disabling
	 * decodes properly.
	 */
	wbase = pcibr_info->f_rbase;
	wsize = pcibr_info->f_rsize;

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
	wspace = pcibr_info->f_window[bar].w_space;
	if (wspace == PCIIO_SPACE_NONE)
	    goto done;

	/* get PCI base and size */
	wbase = pcibr_info->f_window[bar].w_base;
	wsize = pcibr_info->f_window[bar].w_size;

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
    for (try = 0; try < 16; ++try) {
	bridgereg_t             devreg;
	unsigned                offset;

	win = (try + slot) % 8;

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
	if ((try > 7) && (mspace == PCIIO_SPACE_NONE)) {

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
		bridge->b_device[win].reg = devreg;
		pcibr_soft->bs_slot[win].bss_device = devreg;
		bridge->b_wid_tflush;	/* wait until Bridge PIO complete */

#if DEBUG && PCI_DEBUG
		printk("pcibr Device(%d): 0x%lx\n", win, bridge->b_device[win].reg);
#endif
	    }
	    pcibr_soft->bs_slot[win].bss_devio.bssd_space = space;
	    pcibr_soft->bs_slot[win].bss_devio.bssd_base = mbase;
	    xio_addr = BRIDGE_DEVIO(win) + (pci_addr - mbase);

#if DEBUG && PCI_DEBUG
	    printk("%s LINE %d map to space %d space desc 0x%x[%lx..%lx] for slot %d allocates DevIO(%d) devreg 0x%x\n", 
		    __FUNCTION__, __LINE__, space, space_desc,
		    pci_addr, pci_addr + req_size - 1,
		    slot, win, devreg);
#endif

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
	xio_addr = BRIDGE_DEVIO(win) + (pci_addr - mbase);

#if DEBUG && PCI_DEBUG
	printk("%s LINE %d map to space %d [0x%p..0x%p] for slot %d uses DevIO(%d)\n",
		__FUNCTION__, __LINE__, space,  pci_addr, pci_addr + req_size - 1, slot, win);
#endif
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
	if ((pci_addr + BRIDGE_PCI_MEM32_BASE + req_size - 1) <=
	    BRIDGE_PCI_MEM32_LIMIT)
	    xio_addr = pci_addr + BRIDGE_PCI_MEM32_BASE;
	break;

    case PCIIO_SPACE_MEM64:		/* "mem, use 64-bit-wide bus" */
	if ((pci_addr + BRIDGE_PCI_MEM64_BASE + req_size - 1) <=
	    BRIDGE_PCI_MEM64_LIMIT)
	    xio_addr = pci_addr + BRIDGE_PCI_MEM64_BASE;
	break;

    case PCIIO_SPACE_IO:		/* "i/o space" */
	/* Bridge Hardware Bug WAR #482741:
	 * The 4G area that maps directly from
	 * XIO space to PCI I/O space is busted
	 * until Bridge Rev D.
	 */
	if ((pcibr_soft->bs_rev_num > BRIDGE_PART_REV_C) &&
	    ((pci_addr + BRIDGE_PCI_IO_BASE + req_size - 1) <=
	     BRIDGE_PCI_IO_LIMIT))
	    xio_addr = pci_addr + BRIDGE_PCI_IO_BASE;
	break;
    }

    /* Check that "Direct PIO" byteswapping matches,
     * try to change it if it does not.
     */
    if (xio_addr != XIO_NOWHERE) {
	unsigned                bst;	/* nonzero to set bytestream */
	unsigned               *bfp;	/* addr of record of how swapper is set */
	unsigned                swb;	/* which control bit to mung */
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
#if DEBUG && PCI_DEBUG
	    printk("pcibr_addr_pci_to_xio: swap conflict in space %d , was%s%s, want%s%s\n",
		    space, 
		    bfo & PCIIO_BYTE_STREAM ? " BYTE_STREAM" : "",
		    bfo & PCIIO_WORD_VALUES ? " WORD_VALUES" : "",
		    bfn & PCIIO_BYTE_STREAM ? " BYTE_STREAM" : "",
		    bfn & PCIIO_WORD_VALUES ? " WORD_VALUES" : "");
#endif
	    xio_addr = XIO_NOWHERE;
	} else {			/* OK to make the change. */
	    bridgereg_t             octl, nctl;

	    swb = (space == PCIIO_SPACE_IO) ? BRIDGE_CTRL_IO_SWAP : BRIDGE_CTRL_MEM_SWAP;
	    octl = bridge->b_wid_control;
	    nctl = bst ? octl | swb : octl & ~swb;

	    if (octl != nctl)		/* make the change if any */
		bridge->b_wid_control = nctl;

	    *bfp = bfn;			/* record the assignment */

#if DEBUG && PCI_DEBUG
	    printk("pcibr_addr_pci_to_xio: swap for space %d  set to%s%s\n",
		    space, 
		    bfn & PCIIO_BYTE_STREAM ? " BYTE_STREAM" : "",
		    bfn & PCIIO_WORD_VALUES ? " WORD_VALUES" : "");
#endif
	}
    }
  done:
    pcibr_unlock(pcibr_soft, s);
    return xio_addr;
}

/*ARGSUSED6 */
pcibr_piomap_t
pcibr_piomap_alloc(devfs_handle_t pconn_vhdl,
		   device_desc_t dev_desc,
		   pciio_space_t space,
		   iopaddr_t pci_addr,
		   size_t req_size,
		   size_t req_size_max,
		   unsigned flags)
{
    pcibr_info_t	    pcibr_info = pcibr_info_get(pconn_vhdl);
    pciio_info_t            pciio_info = &pcibr_info->f_c;
    pciio_slot_t            pciio_slot = pciio_info_slot_get(pciio_info);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    devfs_handle_t            xconn_vhdl = pcibr_soft->bs_conn;

    pcibr_piomap_t         *mapptr;
    pcibr_piomap_t          maplist;
    pcibr_piomap_t          pcibr_piomap;
    iopaddr_t               xio_addr;
    xtalk_piomap_t          xtalk_piomap;
    unsigned long           s;

    /* Make sure that the req sizes are non-zero */
    if ((req_size < 1) || (req_size_max < 1))
	return NULL;

    /*
     * Code to translate slot/space/addr
     * into xio_addr is common between
     * this routine and pcibr_piotrans_addr.
     */
    xio_addr = pcibr_addr_pci_to_xio(pconn_vhdl, pciio_slot, space, pci_addr, req_size, flags);

    if (xio_addr == XIO_NOWHERE)
	return NULL;

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
	NEW(pcibr_piomap);
    }

    pcibr_piomap->bp_dev = pconn_vhdl;
    pcibr_piomap->bp_slot = pciio_slot;
    pcibr_piomap->bp_flags = flags;
    pcibr_piomap->bp_space = space;
    pcibr_piomap->bp_pciaddr = pci_addr;
    pcibr_piomap->bp_mapsz = req_size;
    pcibr_piomap->bp_soft = pcibr_soft;
    pcibr_piomap->bp_toc[0] = ATOMIC_INIT(0);

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
    return pcibr_piomap;
}

/*ARGSUSED */
void
pcibr_piomap_free(pcibr_piomap_t pcibr_piomap)
{
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
    return xtalk_piomap_addr(pcibr_piomap->bp_xtalk_pio,
			     pcibr_piomap->bp_xtalk_addr +
			     pci_addr - pcibr_piomap->bp_pciaddr,
			     req_size);
}

/*ARGSUSED */
void
pcibr_piomap_done(pcibr_piomap_t pcibr_piomap)
{
    xtalk_piomap_done(pcibr_piomap->bp_xtalk_pio);
}

/*ARGSUSED */
caddr_t
pcibr_piotrans_addr(devfs_handle_t pconn_vhdl,
		    device_desc_t dev_desc,
		    pciio_space_t space,
		    iopaddr_t pci_addr,
		    size_t req_size,
		    unsigned flags)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pciio_slot_t            pciio_slot = pciio_info_slot_get(pciio_info);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    devfs_handle_t            xconn_vhdl = pcibr_soft->bs_conn;

    iopaddr_t               xio_addr;

    xio_addr = pcibr_addr_pci_to_xio(pconn_vhdl, pciio_slot, space, pci_addr, req_size, flags);

    if (xio_addr == XIO_NOWHERE)
	return NULL;

    return xtalk_piotrans_addr(xconn_vhdl, 0, xio_addr, req_size, flags & PIOMAP_FLAGS);
}

/*
 * PIO Space allocation and management.
 *      Allocate and Manage the PCI PIO space (mem and io space)
 *      This routine is pretty simplistic at this time, and
 *      does pretty trivial management of allocation and freeing..
 *      The current scheme is prone for fragmentation..
 *      Change the scheme to use bitmaps.
 */

/*ARGSUSED */
iopaddr_t
pcibr_piospace_alloc(devfs_handle_t pconn_vhdl,
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

    iopaddr_t              *pciaddr, *pcilast;
    iopaddr_t               start_addr;
    size_t                  align_mask;

    /*
     * Check for proper alignment
     */
    ASSERT(alignment >= NBPP);
    ASSERT((alignment & (alignment - 1)) == 0);

    align_mask = alignment - 1;
    s = pcibr_lock(pcibr_soft);

    /*
     * First look if a previously allocated chunk exists.
     */
    if ((piosp = pcibr_info->f_piospace)) {
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

    switch (space) {
    case PCIIO_SPACE_IO:
	pciaddr = &pcibr_soft->bs_spinfo.pci_io_base;
	pcilast = &pcibr_soft->bs_spinfo.pci_io_last;
	break;
    case PCIIO_SPACE_MEM:
    case PCIIO_SPACE_MEM32:
	pciaddr = &pcibr_soft->bs_spinfo.pci_mem_base;
	pcilast = &pcibr_soft->bs_spinfo.pci_mem_last;
	break;
    default:
	ASSERT(0);
	pcibr_unlock(pcibr_soft, s);
	return 0;
    }

    start_addr = *pciaddr;

    /*
     * Align start_addr.
     */
    if (start_addr & align_mask)
	start_addr = (start_addr + align_mask) & ~align_mask;

    if ((start_addr + req_size) > *pcilast) {
	/*
	 * If too big a request, reject it.
	 */
	pcibr_unlock(pcibr_soft, s);
	return 0;
    }
    *pciaddr = (start_addr + req_size);

    NEW(piosp);
    piosp->free = 0;
    piosp->space = space;
    piosp->start = start_addr;
    piosp->count = req_size;
    piosp->next = pcibr_info->f_piospace;
    pcibr_info->f_piospace = piosp;

    pcibr_unlock(pcibr_soft, s);
    return start_addr;
}

/*ARGSUSED */
void
pcibr_piospace_free(devfs_handle_t pconn_vhdl,
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
    if (pcibr_soft->bs_xbridge) {
    	if (flags & PCIIO_BYTE_STREAM)
        	attributes |= PCI64_ATTR_SWAP;
    	if (flags & PCIIO_WORD_VALUES)
        	attributes &= ~PCI64_ATTR_SWAP;
    }

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

    return (attributes);
}

/*ARGSUSED */
pcibr_dmamap_t
pcibr_dmamap_alloc(devfs_handle_t pconn_vhdl,
		   device_desc_t dev_desc,
		   size_t req_size_max,
		   unsigned flags)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    devfs_handle_t            xconn_vhdl = pcibr_soft->bs_conn;
    pciio_slot_t            slot;
    xwidgetnum_t            xio_port;

    xtalk_dmamap_t          xtalk_dmamap;
    pcibr_dmamap_t          pcibr_dmamap;
    int                     ate_count;
    int                     ate_index;

    /* merge in forced flags */
    flags |= pcibr_soft->bs_dma_flags;

    /*
     * On SNIA64, these maps are pre-allocated because pcibr_dmamap_alloc()
     * can be called within an interrupt thread.
     */
    pcibr_dmamap = (pcibr_dmamap_t)get_free_pciio_dmamap(pcibr_soft->bs_vhdl);

    if (!pcibr_dmamap)
	return 0;

    xtalk_dmamap = xtalk_dmamap_alloc(xconn_vhdl, dev_desc, req_size_max,
				      flags & DMAMAP_FLAGS);
    if (!xtalk_dmamap) {
#if PCIBR_ATE_DEBUG
	printk("pcibr_attach: xtalk_dmamap_alloc failed\n");
#endif
	free_pciio_dmamap(pcibr_dmamap);
	return 0;
    }
    xio_port = pcibr_soft->bs_mxid;
    slot = pciio_info_slot_get(pciio_info);

    pcibr_dmamap->bd_dev = pconn_vhdl;
    pcibr_dmamap->bd_slot = slot;
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
	     * consistant with any previous DMA
	     * mappings using shared resources.
	     */

	    pci_addr = pcibr_flags_to_d64(flags, pcibr_soft);

	    pcibr_dmamap->bd_flags = flags;
	    pcibr_dmamap->bd_xio_addr = 0;
	    pcibr_dmamap->bd_pci_addr = pci_addr;

	    /* Make sure we have an RRB (or two).
	     */
	    if (!(pcibr_soft->bs_rrb_fixed & (1 << slot))) {
		if (flags & PCIBR_VCHAN1)
		    slot += PCIBR_RRB_SLOT_VIRTUAL;
		have_rrbs = pcibr_soft->bs_rrb_valid[slot];
		if (have_rrbs < 2) {
		    if (pci_addr & PCI64_ATTR_PREF)
			min_rrbs = 2;
		    else
			min_rrbs = 1;
		    if (have_rrbs < min_rrbs)
			do_pcibr_rrb_autoalloc(pcibr_soft, slot, min_rrbs - have_rrbs);
		}
	    }
#if PCIBR_ATE_DEBUG
	    printk("pcibr_dmamap_alloc: using direct64\n");
#endif
	    return pcibr_dmamap;
	}
#if PCIBR_ATE_DEBUG
	printk("pcibr_dmamap_alloc: unable to use direct64\n");
#endif
	flags &= ~PCIIO_DMA_A64;
    }
    if (flags & PCIIO_FIXED) {
	/* warning: mappings may fail later,
	 * if direct32 can't get to the address.
	 */
	if (!pcibr_try_set_device(pcibr_soft, slot, flags, BRIDGE_DEV_D32_BITS)) {
	    /* User desires DIRECT A32 operations,
	     * and the attributes of the DMA are
	     * consistant with any previous DMA
	     * mappings using shared resources.
	     * Mapping calls may fail if target
	     * is outside the direct32 range.
	     */
#if PCIBR_ATE_DEBUG
	    printk("pcibr_dmamap_alloc: using direct32\n");
#endif
	    pcibr_dmamap->bd_flags = flags;
	    pcibr_dmamap->bd_xio_addr = pcibr_soft->bs_dir_xbase;
	    pcibr_dmamap->bd_pci_addr = PCI32_DIRECT_BASE;
	    return pcibr_dmamap;
	}
#if PCIBR_ATE_DEBUG
	printk("pcibr_dmamap_alloc: unable to use direct32\n");
#endif
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

    ate_index = pcibr_ate_alloc(pcibr_soft, ate_count);

    if (ate_index != -1) {
	if (!pcibr_try_set_device(pcibr_soft, slot, flags, BRIDGE_DEV_PMU_BITS)) {
	    bridge_ate_t            ate_proto;
	    int                     have_rrbs;
	    int                     min_rrbs;

#if PCIBR_ATE_DEBUG
	    printk("pcibr_dmamap_alloc: using PMU\n");
#endif

	    ate_proto = pcibr_flags_to_ate(flags);

	    pcibr_dmamap->bd_flags = flags;
	    pcibr_dmamap->bd_pci_addr =
		PCI32_MAPPED_BASE + IOPGSIZE * ate_index;
	    /*
	     * for xbridge the byte-swap bit == bit 29 of PCI address
	     */
	    if (pcibr_soft->bs_xbridge) {
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
	    }
	    pcibr_dmamap->bd_xio_addr = 0;
	    pcibr_dmamap->bd_ate_ptr = pcibr_ate_addr(pcibr_soft, ate_index);
	    pcibr_dmamap->bd_ate_index = ate_index;
	    pcibr_dmamap->bd_ate_count = ate_count;
	    pcibr_dmamap->bd_ate_proto = ate_proto;

	    /* Make sure we have an RRB (or two).
	     */
	    if (!(pcibr_soft->bs_rrb_fixed & (1 << slot))) {
		have_rrbs = pcibr_soft->bs_rrb_valid[slot];
		if (have_rrbs < 2) {
		    if (ate_proto & ATE_PREF)
			min_rrbs = 2;
		    else
			min_rrbs = 1;
		    if (have_rrbs < min_rrbs)
			do_pcibr_rrb_autoalloc(pcibr_soft, slot, min_rrbs - have_rrbs);
		}
	    }
	    if (ate_index >= pcibr_soft->bs_int_ate_size && 
				!pcibr_soft->bs_xbridge) {
		bridge_t               *bridge = pcibr_soft->bs_base;
		volatile unsigned      *cmd_regp;
		unsigned                cmd_reg;
		unsigned long           s;

		pcibr_dmamap->bd_flags |= PCIBR_DMAMAP_SSRAM;

		s = pcibr_lock(pcibr_soft);
		cmd_regp = &(bridge->
			     b_type0_cfg_dev[slot].
			     l[PCI_CFG_COMMAND / 4]);
		cmd_reg = *cmd_regp;
		pcibr_soft->bs_slot[slot].bss_cmd_pointer = cmd_regp;
		pcibr_soft->bs_slot[slot].bss_cmd_shadow = cmd_reg;
		pcibr_unlock(pcibr_soft, s);
	    }
	    return pcibr_dmamap;
	}
#if PCIBR_ATE_DEBUG
	printk("pcibr_dmamap_alloc: unable to use PMU\n");
#endif
	pcibr_ate_free(pcibr_soft, ate_index, ate_count);
    }
    /* total failure: sorry, you just can't
     * get from here to there that way.
     */
#if PCIBR_ATE_DEBUG
    printk("pcibr_dmamap_alloc: complete failure.\n");
#endif
    xtalk_dmamap_free(xtalk_dmamap);
    free_pciio_dmamap(pcibr_dmamap);
    return 0;
}

/*ARGSUSED */
void
pcibr_dmamap_free(pcibr_dmamap_t pcibr_dmamap)
{
    pcibr_soft_t            pcibr_soft = pcibr_dmamap->bd_soft;
    pciio_slot_t            slot = pcibr_dmamap->bd_slot;

    unsigned                flags = pcibr_dmamap->bd_flags;

    /* Make sure that bss_ext_ates_active
     * is properly kept up to date.
     */

    if (PCIBR_DMAMAP_BUSY & flags)
	if (PCIBR_DMAMAP_SSRAM & flags)
	    atomic_dec(&(pcibr_soft->bs_slot[slot]. bss_ext_ates_active));

    xtalk_dmamap_free(pcibr_dmamap->bd_xtalk);

    if (pcibr_dmamap->bd_flags & PCIIO_DMA_A64) {
	pcibr_release_device(pcibr_soft, slot, BRIDGE_DEV_D64_BITS);
    }
    if (pcibr_dmamap->bd_ate_count) {
	pcibr_ate_free(pcibr_dmamap->bd_soft,
		       pcibr_dmamap->bd_ate_index,
		       pcibr_dmamap->bd_ate_count);
	pcibr_release_device(pcibr_soft, slot, BRIDGE_DEV_PMU_BITS);
    }

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

    if ((xio_addr >= BRIDGE_PCI_MEM32_BASE) &&
	(xio_lim <= BRIDGE_PCI_MEM32_LIMIT)) {
	pci_addr = xio_addr - BRIDGE_PCI_MEM32_BASE;
	return pci_addr;
    }
    if ((xio_addr >= BRIDGE_PCI_MEM64_BASE) &&
	(xio_lim <= BRIDGE_PCI_MEM64_LIMIT)) {
	pci_addr = xio_addr - BRIDGE_PCI_MEM64_BASE;
	return pci_addr;
    }
    for (slot = 0; slot < 8; ++slot)
	if ((xio_addr >= BRIDGE_DEVIO(slot)) &&
	    (xio_lim < BRIDGE_DEVIO(slot + 1))) {
	    bridgereg_t             dev;

	    dev = soft->bs_slot[slot].bss_device;
	    pci_addr = dev & BRIDGE_DEV_OFF_MASK;
	    pci_addr <<= BRIDGE_DEV_OFF_ADDR_SHFT;
	    pci_addr += xio_addr - BRIDGE_DEVIO(slot);
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

#if DEBUG && PCIBR_DMA_DEBUG
	printk("pcibr_dmamap_addr (direct64):\n"
		"\twanted paddr [0x%x..0x%x]\n"
		"\tXIO port 0x%x offset 0x%x\n"
		"\treturning PCI 0x%x\n",
		paddr, paddr + req_size - 1,
		xio_port, xio_addr, pci_addr);
#endif
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

#if DEBUG && PCIBR_DMA_DEBUG
	printk("pcibr_dmamap_addr (direct32):\n"
		"\twanted paddr [0x%x..0x%x]\n"
		"\tXIO port 0x%x offset 0x%x\n"
		"\treturning PCI 0x%x\n",
		paddr, paddr + req_size - 1,
		xio_port, xio_addr, pci_addr);
#endif
    } else {
	bridge_t               *bridge = pcibr_soft->bs_base;
	iopaddr_t               offset = IOPGOFF(xio_addr);
	bridge_ate_t            ate_proto = pcibr_dmamap->bd_ate_proto;
	int                     ate_count = IOPG(offset + req_size - 1) + 1;

	int                     ate_index = pcibr_dmamap->bd_ate_index;
	unsigned                cmd_regs[8];
	unsigned                s;

#if PCIBR_FREEZE_TIME
	int                     ate_total = ate_count;
	unsigned                freeze_time;
#endif

#if PCIBR_ATE_DEBUG
	bridge_ate_t            ate_cmp;
	bridge_ate_p            ate_cptr;
	unsigned                ate_lo, ate_hi;
	int                     ate_bad = 0;
	int                     ate_rbc = 0;
#endif
	bridge_ate_p            ate_ptr = pcibr_dmamap->bd_ate_ptr;
	bridge_ate_t            ate;

	/* Bridge Hardware WAR #482836:
	 * If the transfer is not cache aligned
	 * and the Bridge Rev is <= B, force
	 * prefetch to be off.
	 */
	if (flags & PCIBR_NOPREFETCH)
	    ate_proto &= ~ATE_PREF;

	ate = ate_proto
	    | (xio_port << ATE_TIDSHIFT)
	    | (xio_addr - offset);

	pci_addr = pcibr_dmamap->bd_pci_addr + offset;

	/* Fill in our mapping registers
	 * with the appropriate xtalk data,
	 * and hand back the PCI address.
	 */

	ASSERT(ate_count > 0);
	if (ate_count <= pcibr_dmamap->bd_ate_count) {
		ATE_FREEZE();
		ATE_WRITE();
		ATE_THAW();
		bridge->b_wid_tflush;	/* wait until Bridge PIO complete */
	} else {
		/* The number of ATE's required is greater than the number
		 * allocated for this map. One way this can happen is if
		 * pcibr_dmamap_alloc() was called with the PCIBR_NO_ATE_ROUNDUP
		 * flag, and then when that map is used (right now), the
		 * target address tells us we really did need to roundup.
		 * The other possibility is that the map is just plain too
		 * small to handle the requested target area.
		 */
#if PCIBR_ATE_DEBUG
		printk(KERN_WARNING "pcibr_dmamap_addr :\n"
			"\twanted paddr [0x%x..0x%x]\n"
			"\tate_count 0x%x bd_ate_count 0x%x\n"
			"\tATE's required > number allocated\n",
			paddr, paddr + req_size - 1,
			ate_count, pcibr_dmamap->bd_ate_count);
#endif
		pci_addr = 0;
	}

    }
    return pci_addr;
}

/*ARGSUSED */
alenlist_t
pcibr_dmamap_list(pcibr_dmamap_t pcibr_dmamap,
		  alenlist_t palenlist,
		  unsigned flags)
{
    pcibr_soft_t            pcibr_soft;
    bridge_t               *bridge=NULL;

    unsigned                al_flags = (flags & PCIIO_NOSLEEP) ? AL_NOSLEEP : 0;
    int                     inplace = flags & PCIIO_INPLACE;

    alenlist_t              pciio_alenlist = 0;
    alenlist_t              xtalk_alenlist;
    size_t                  length;
    iopaddr_t               offset;
    unsigned                direct64;
    int                     ate_index = 0;
    int                     ate_count = 0;
    int                     ate_total = 0;
    bridge_ate_p            ate_ptr = (bridge_ate_p)0;
    bridge_ate_t            ate_proto = (bridge_ate_t)0;
    bridge_ate_t            ate_prev;
    bridge_ate_t            ate;
    alenaddr_t              xio_addr;
    xwidgetnum_t            xio_port;
    iopaddr_t               pci_addr;
    alenaddr_t              new_addr;
    unsigned                cmd_regs[8];
    unsigned                s = 0;

#if PCIBR_FREEZE_TIME
    unsigned                freeze_time;
#endif
    int			    ate_freeze_done = 0;	/* To pair ATE_THAW
							 * with an ATE_FREEZE
							 */

    pcibr_soft = pcibr_dmamap->bd_soft;

    xtalk_alenlist = xtalk_dmamap_list(pcibr_dmamap->bd_xtalk, palenlist,
				       flags & DMAMAP_FLAGS);
    if (!xtalk_alenlist)
	goto fail;

    alenlist_cursor_init(xtalk_alenlist, 0, NULL);

    if (inplace) {
	pciio_alenlist = xtalk_alenlist;
    } else {
	pciio_alenlist = alenlist_create(al_flags);
	if (!pciio_alenlist)
	    goto fail;
    }

    direct64 = pcibr_dmamap->bd_flags & PCIIO_DMA_A64;
    if (!direct64) {
	bridge = pcibr_soft->bs_base;
	ate_ptr = pcibr_dmamap->bd_ate_ptr;
	ate_index = pcibr_dmamap->bd_ate_index;
	ate_proto = pcibr_dmamap->bd_ate_proto;
	ATE_FREEZE();
	ate_freeze_done = 1;	/* Remember that we need to do an ATE_THAW */
    }
    pci_addr = pcibr_dmamap->bd_pci_addr;

    ate_prev = 0;			/* matches no valid ATEs */
    while (ALENLIST_SUCCESS ==
	   alenlist_get(xtalk_alenlist, NULL, 0,
			&xio_addr, &length, al_flags)) {
	if (XIO_PACKED(xio_addr)) {
	    xio_port = XIO_PORT(xio_addr);
	    xio_addr = XIO_ADDR(xio_addr);
	} else
	    xio_port = pcibr_dmamap->bd_xio_port;

	if (xio_port == pcibr_soft->bs_xid) {
	    new_addr = pcibr_addr_xio_to_pci(pcibr_soft, xio_addr, length);
	    if (new_addr == PCI_NOWHERE)
		goto fail;
	} else if (direct64) {
	    new_addr = pci_addr | xio_addr
		| ((uint64_t) xio_port << PCI64_ATTR_TARG_SHFT);

	    /* Bridge Hardware WAR #482836:
	     * If the transfer is not cache aligned
	     * and the Bridge Rev is <= B, force
	     * prefetch to be off.
	     */
	    if (flags & PCIBR_NOPREFETCH)
		new_addr &= ~PCI64_ATTR_PREF;

	} else {
	    /* calculate the ate value for
	     * the first address. If it
	     * matches the previous
	     * ATE written (ie. we had
	     * multiple blocks in the
	     * same IOPG), then back up
	     * and reuse that ATE.
	     *
	     * We are NOT going to
	     * aggressively try to
	     * reuse any other ATEs.
	     */
	    offset = IOPGOFF(xio_addr);
	    ate = ate_proto
		| (xio_port << ATE_TIDSHIFT)
		| (xio_addr - offset);
	    if (ate == ate_prev) {
#if PCIBR_ATE_DEBUG
		printk("pcibr_dmamap_list: ATE share\n");
#endif
		ate_ptr--;
		ate_index--;
		pci_addr -= IOPGSIZE;
	    }
	    new_addr = pci_addr + offset;

	    /* Fill in the hardware ATEs
	     * that contain this block.
	     */
	    ate_count = IOPG(offset + length - 1) + 1;
	    ate_total += ate_count;

	    /* Ensure that this map contains enough ATE's */
	    if (ate_total > pcibr_dmamap->bd_ate_count) {
#if PCIBR_ATE_DEBUG
		printk(KERN_WARNING "pcibr_dmamap_list :\n"
			"\twanted xio_addr [0x%x..0x%x]\n"
			"\tate_total 0x%x bd_ate_count 0x%x\n"
			"\tATE's required > number allocated\n",
			xio_addr, xio_addr + length - 1,
			ate_total, pcibr_dmamap->bd_ate_count);
#endif
		goto fail;
	    }

	    ATE_WRITE();

	    ate_index += ate_count;
	    ate_ptr += ate_count;

	    ate_count <<= IOPFNSHIFT;
	    ate += ate_count;
	    pci_addr += ate_count;
	}

	/* write the PCI DMA address
	 * out to the scatter-gather list.
	 */
	if (inplace) {
	    if (ALENLIST_SUCCESS !=
		alenlist_replace(pciio_alenlist, NULL,
				 &new_addr, &length, al_flags))
		goto fail;
	} else {
	    if (ALENLIST_SUCCESS !=
		alenlist_append(pciio_alenlist,
				new_addr, length, al_flags))
		goto fail;
	}
    }
    if (!inplace)
	alenlist_done(xtalk_alenlist);

    /* Reset the internal cursor of the alenlist to be returned back
     * to the caller.
     */
    alenlist_cursor_init(pciio_alenlist, 0, NULL);


    /* In case an ATE_FREEZE was done do the ATE_THAW to unroll all the
     * changes that ATE_FREEZE has done to implement the external SSRAM
     * bug workaround.
     */
    if (ate_freeze_done) {
	ATE_THAW();
	bridge->b_wid_tflush;		/* wait until Bridge PIO complete */
    }
    return pciio_alenlist;

  fail:
    /* There are various points of failure after doing an ATE_FREEZE
     * We need to do an ATE_THAW. Otherwise the ATEs are locked forever.
     * The decision to do an ATE_THAW needs to be based on whether a
     * an ATE_FREEZE was done before.
     */
    if (ate_freeze_done) {
	ATE_THAW();
	bridge->b_wid_tflush;
    }
    if (pciio_alenlist && !inplace)
	alenlist_destroy(pciio_alenlist);
    return 0;
}

/*ARGSUSED */
void
pcibr_dmamap_done(pcibr_dmamap_t pcibr_dmamap)
{
    /*
     * We could go through and invalidate ATEs here;
     * for performance reasons, we don't.
     * We also don't enforce the strict alternation
     * between _addr/_list and _done, but Hub does.
     */

    if (pcibr_dmamap->bd_flags & PCIBR_DMAMAP_BUSY) {
	pcibr_dmamap->bd_flags &= ~PCIBR_DMAMAP_BUSY;

	if (pcibr_dmamap->bd_flags & PCIBR_DMAMAP_SSRAM)
	    atomic_dec(&(pcibr_dmamap->bd_soft->bs_slot[pcibr_dmamap->bd_slot]. bss_ext_ates_active));
    }
    xtalk_dmamap_done(pcibr_dmamap->bd_xtalk);
}


/*
 * For each bridge, the DIR_OFF value in the Direct Mapping Register
 * determines the PCI to Crosstalk memory mapping to be used for all
 * 32-bit Direct Mapping memory accesses. This mapping can be to any
 * node in the system. This function will return that compact node id.
 */

/*ARGSUSED */
cnodeid_t
pcibr_get_dmatrans_node(devfs_handle_t pconn_vhdl)
{

	pciio_info_t	pciio_info = pciio_info_get(pconn_vhdl);
	pcibr_soft_t	pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);

	return(NASID_TO_COMPACT_NODEID(NASID_GET(pcibr_soft->bs_dir_xbase)));
}

/*ARGSUSED */
iopaddr_t
pcibr_dmatrans_addr(devfs_handle_t pconn_vhdl,
		    device_desc_t dev_desc,
		    paddr_t paddr,
		    size_t req_size,
		    unsigned flags)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    devfs_handle_t            xconn_vhdl = pcibr_soft->bs_conn;
    pciio_slot_t            pciio_slot = pciio_info_slot_get(pciio_info);
    pcibr_soft_slot_t       slotp = &pcibr_soft->bs_slot[pciio_slot];

    xwidgetnum_t            xio_port;
    iopaddr_t               xio_addr;
    iopaddr_t               pci_addr;

    int                     have_rrbs;
    int                     min_rrbs;

    /* merge in forced flags */
    flags |= pcibr_soft->bs_dma_flags;

    xio_addr = xtalk_dmatrans_addr(xconn_vhdl, 0, paddr, req_size,
				   flags & DMAMAP_FLAGS);

    if (!xio_addr) {
#if PCIBR_DMA_DEBUG
	printk("pcibr_dmatrans_addr:\n"
		"\tpciio connection point %v\n"
		"\txtalk connection point %v\n"
		"\twanted paddr [0x%x..0x%x]\n"
		"\txtalk_dmatrans_addr returned 0x%x\n",
		pconn_vhdl, xconn_vhdl,
		paddr, paddr + req_size - 1,
		xio_addr);
#endif
	return 0;
    }
    /*
     * find which XIO port this goes to.
     */
    if (XIO_PACKED(xio_addr)) {
	if (xio_addr == XIO_NOWHERE) {
#if PCIBR_DMA_DEBUG
	    printk("pcibr_dmatrans_addr:\n"
		    "\tpciio connection point %v\n"
		    "\txtalk connection point %v\n"
		    "\twanted paddr [0x%x..0x%x]\n"
		    "\txtalk_dmatrans_addr returned 0x%x\n",
		    pconn_vhdl, xconn_vhdl,
		    paddr, paddr + req_size - 1,
		    xio_addr);
#endif
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

	    pci_addr |= xio_addr
		| ((uint64_t) xio_port << PCI64_ATTR_TARG_SHFT);

#if DEBUG && PCIBR_DMA_DEBUG
#if HWG_PERF_CHECK
	    if (xio_addr != 0x20000000)
#endif
		printk("pcibr_dmatrans_addr: [reuse]\n"
			"\tpciio connection point %v\n"
			"\txtalk connection point %v\n"
			"\twanted paddr [0x%x..0x%x]\n"
			"\txtalk_dmatrans_addr returned 0x%x\n"
			"\tdirect 64bit address is 0x%x\n",
			pconn_vhdl, xconn_vhdl,
			paddr, paddr + req_size - 1,
			xio_addr, pci_addr);
#endif
	    return (pci_addr);
	}
	if (!pcibr_try_set_device(pcibr_soft, pciio_slot, flags, BRIDGE_DEV_D64_BITS)) {
	    pci_addr = pcibr_flags_to_d64(flags, pcibr_soft);
	    slotp->bss_d64_flags = flags;
	    slotp->bss_d64_base = pci_addr;
	    pci_addr |= xio_addr
		| ((uint64_t) xio_port << PCI64_ATTR_TARG_SHFT);

	    /* Make sure we have an RRB (or two).
	     */
	    if (!(pcibr_soft->bs_rrb_fixed & (1 << pciio_slot))) {
		if (flags & PCIBR_VCHAN1)
		    pciio_slot += PCIBR_RRB_SLOT_VIRTUAL;
		have_rrbs = pcibr_soft->bs_rrb_valid[pciio_slot];
		if (have_rrbs < 2) {
		    if (pci_addr & PCI64_ATTR_PREF)
			min_rrbs = 2;
		    else
			min_rrbs = 1;
		    if (have_rrbs < min_rrbs)
			do_pcibr_rrb_autoalloc(pcibr_soft, pciio_slot, min_rrbs - have_rrbs);
		}
	    }
#if PCIBR_DMA_DEBUG
#if HWG_PERF_CHECK
	    if (xio_addr != 0x20000000)
#endif
		printk("pcibr_dmatrans_addr:\n"
			"\tpciio connection point %v\n"
			"\txtalk connection point %v\n"
			"\twanted paddr [0x%x..0x%x]\n"
			"\txtalk_dmatrans_addr returned 0x%x\n"
			"\tdirect 64bit address is 0x%x\n"
			"\tnew flags: 0x%x\n",
			pconn_vhdl, xconn_vhdl,
			paddr, paddr + req_size - 1,
			xio_addr, pci_addr, (uint64_t) flags);
#endif
	    return (pci_addr);
	}
	/* our flags conflict with Device(x).
	 */
	flags = flags
	    & ~PCIIO_DMA_A64
	    & ~PCIBR_VCHAN0
	    ;

#if PCIBR_DMA_DEBUG
	printk("pcibr_dmatrans_addr:\n"
		"\tpciio connection point %v\n"
		"\txtalk connection point %v\n"
		"\twanted paddr [0x%x..0x%x]\n"
		"\txtalk_dmatrans_addr returned 0x%x\n"
		"\tUnable to set Device(x) bits for Direct-64\n",
		pconn_vhdl, xconn_vhdl,
		paddr, paddr + req_size - 1,
		xio_addr);
#endif
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
#if PCIBR_DMA_DEBUG
	    printk("pcibr_dmatrans_addr:\n"
		    "\tpciio connection point %v\n"
		    "\txtalk connection point %v\n"
		    "\twanted paddr [0x%x..0x%x]\n"
		    "\txtalk_dmatrans_addr returned 0x%x\n"
		    "\txio region outside direct32 target\n",
		    pconn_vhdl, xconn_vhdl,
		    paddr, paddr + req_size - 1,
		    xio_addr);
#endif
	} else {
	    pci_addr = slotp->bss_d32_base;
	    if ((pci_addr != PCIBR_D32_BASE_UNSET) &&
		(flags == slotp->bss_d32_flags)) {

		pci_addr |= offset;

#if DEBUG && PCIBR_DMA_DEBUG
		printk("pcibr_dmatrans_addr: [reuse]\n"
			"\tpciio connection point %v\n"
			"\txtalk connection point %v\n"
			"\twanted paddr [0x%x..0x%x]\n"
			"\txtalk_dmatrans_addr returned 0x%x\n"
			"\tmapped via direct32 offset 0x%x\n"
			"\twill DMA via pci addr 0x%x\n",
			pconn_vhdl, xconn_vhdl,
			paddr, paddr + req_size - 1,
			xio_addr, offset, pci_addr);
#endif
		return (pci_addr);
	    }
	    if (!pcibr_try_set_device(pcibr_soft, pciio_slot, flags, BRIDGE_DEV_D32_BITS)) {

		pci_addr = PCI32_DIRECT_BASE;
		slotp->bss_d32_flags = flags;
		slotp->bss_d32_base = pci_addr;
		pci_addr |= offset;

		/* Make sure we have an RRB (or two).
		 */
		if (!(pcibr_soft->bs_rrb_fixed & (1 << pciio_slot))) {
		    have_rrbs = pcibr_soft->bs_rrb_valid[pciio_slot];
		    if (have_rrbs < 2) {
			if (slotp->bss_device & BRIDGE_DEV_PREF)
			    min_rrbs = 2;
			else
			    min_rrbs = 1;
			if (have_rrbs < min_rrbs)
			    do_pcibr_rrb_autoalloc(pcibr_soft, pciio_slot, min_rrbs - have_rrbs);
		    }
		}
#if PCIBR_DMA_DEBUG
#if HWG_PERF_CHECK
		if (xio_addr != 0x20000000)
#endif
		    printk("pcibr_dmatrans_addr:\n"
			    "\tpciio connection point %v\n"
			    "\txtalk connection point %v\n"
			    "\twanted paddr [0x%x..0x%x]\n"
			    "\txtalk_dmatrans_addr returned 0x%x\n"
			    "\tmapped via direct32 offset 0x%x\n"
			    "\twill DMA via pci addr 0x%x\n"
			    "\tnew flags: 0x%x\n",
			    pconn_vhdl, xconn_vhdl,
			    paddr, paddr + req_size - 1,
			    xio_addr, offset, pci_addr, (uint64_t) flags);
#endif
		return (pci_addr);
	    }
	    /* our flags conflict with Device(x).
	     */
#if PCIBR_DMA_DEBUG
	    printk("pcibr_dmatrans_addr:\n"
		    "\tpciio connection point %v\n"
		    "\txtalk connection point %v\n"
		    "\twanted paddr [0x%x..0x%x]\n"
		    "\txtalk_dmatrans_addr returned 0x%x\n"
		    "\tUnable to set Device(x) bits for Direct-32\n",
		    pconn_vhdl, xconn_vhdl,
		    paddr, paddr + req_size - 1,
		    xio_addr);
#endif
	}
    }

#if PCIBR_DMA_DEBUG
    printk("pcibr_dmatrans_addr:\n"
	    "\tpciio connection point %v\n"
	    "\txtalk connection point %v\n"
	    "\twanted paddr [0x%x..0x%x]\n"
	    "\txtalk_dmatrans_addr returned 0x%x\n"
	    "\tno acceptable PCI address found or constructable\n",
	    pconn_vhdl, xconn_vhdl,
	    paddr, paddr + req_size - 1,
	    xio_addr);
#endif

    return 0;
}

/*ARGSUSED */
alenlist_t
pcibr_dmatrans_list(devfs_handle_t pconn_vhdl,
		    device_desc_t dev_desc,
		    alenlist_t palenlist,
		    unsigned flags)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    devfs_handle_t            xconn_vhdl = pcibr_soft->bs_conn;
    pciio_slot_t            pciio_slot = pciio_info_slot_get(pciio_info);
    pcibr_soft_slot_t       slotp = &pcibr_soft->bs_slot[pciio_slot];
    xwidgetnum_t            xio_port;

    alenlist_t              pciio_alenlist = 0;
    alenlist_t              xtalk_alenlist = 0;

    int                     inplace;
    unsigned                direct64;
    unsigned                al_flags;

    iopaddr_t               xio_base;
    alenaddr_t              xio_addr;
    size_t                  xio_size;

    size_t                  map_size;
    iopaddr_t               pci_base;
    alenaddr_t              pci_addr;

    unsigned                relbits = 0;

    /* merge in forced flags */
    flags |= pcibr_soft->bs_dma_flags;

    inplace = flags & PCIIO_INPLACE;
    direct64 = flags & PCIIO_DMA_A64;
    al_flags = (flags & PCIIO_NOSLEEP) ? AL_NOSLEEP : 0;

    if (direct64) {
	map_size = 1ull << 48;
	xio_base = 0;
	pci_base = slotp->bss_d64_base;
	if ((pci_base != PCIBR_D64_BASE_UNSET) &&
	    (flags == slotp->bss_d64_flags)) {
	    /* reuse previous base info */
	} else if (pcibr_try_set_device(pcibr_soft, pciio_slot, flags, BRIDGE_DEV_D64_BITS) < 0) {
	    /* DMA configuration conflict */
	    goto fail;
	} else {
	    relbits = BRIDGE_DEV_D64_BITS;
	    pci_base =
		pcibr_flags_to_d64(flags, pcibr_soft);
	}
    } else {
	xio_base = pcibr_soft->bs_dir_xbase;
	map_size = 1ull << 31;
	pci_base = slotp->bss_d32_base;
	if ((pci_base != PCIBR_D32_BASE_UNSET) &&
	    (flags == slotp->bss_d32_flags)) {
	    /* reuse previous base info */
	} else if (pcibr_try_set_device(pcibr_soft, pciio_slot, flags, BRIDGE_DEV_D32_BITS) < 0) {
	    /* DMA configuration conflict */
	    goto fail;
	} else {
	    relbits = BRIDGE_DEV_D32_BITS;
	    pci_base = PCI32_DIRECT_BASE;
	}
    }

    xtalk_alenlist = xtalk_dmatrans_list(xconn_vhdl, 0, palenlist,
					 flags & DMAMAP_FLAGS);
    if (!xtalk_alenlist)
	goto fail;

    alenlist_cursor_init(xtalk_alenlist, 0, NULL);

    if (inplace) {
	pciio_alenlist = xtalk_alenlist;
    } else {
	pciio_alenlist = alenlist_create(al_flags);
	if (!pciio_alenlist)
	    goto fail;
    }

    while (ALENLIST_SUCCESS ==
	   alenlist_get(xtalk_alenlist, NULL, 0,
			&xio_addr, &xio_size, al_flags)) {

	/*
	 * find which XIO port this goes to.
	 */
	if (XIO_PACKED(xio_addr)) {
	    if (xio_addr == XIO_NOWHERE) {
#if PCIBR_DMA_DEBUG
		printk("pcibr_dmatrans_addr:\n"
			"\tpciio connection point %v\n"
			"\txtalk connection point %v\n"
			"\twanted paddr [0x%x..0x%x]\n"
			"\txtalk_dmatrans_addr returned 0x%x\n",
			pconn_vhdl, xconn_vhdl,
			paddr, paddr + req_size - 1,
			xio_addr);
#endif
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
	    pci_addr = pcibr_addr_xio_to_pci(pcibr_soft, xio_addr, xio_size);
	    if ( (pci_addr == (alenaddr_t)NULL) )
		goto fail;
	} else if (direct64) {
	    ASSERT(xio_port != 0);
	    pci_addr = pci_base | xio_addr
		| ((uint64_t) xio_port << PCI64_ATTR_TARG_SHFT);
	} else {
	    iopaddr_t               offset = xio_addr - xio_base;
	    iopaddr_t               endoff = xio_size + offset;

	    if ((xio_size > map_size) ||
		(xio_addr < xio_base) ||
		(xio_port != pcibr_soft->bs_dir_xport) ||
		(endoff > map_size))
		goto fail;

	    pci_addr = pci_base + (xio_addr - xio_base);
	}

	/* write the PCI DMA address
	 * out to the scatter-gather list.
	 */
	if (inplace) {
	    if (ALENLIST_SUCCESS !=
		alenlist_replace(pciio_alenlist, NULL,
				 &pci_addr, &xio_size, al_flags))
		goto fail;
	} else {
	    if (ALENLIST_SUCCESS !=
		alenlist_append(pciio_alenlist,
				pci_addr, xio_size, al_flags))
		goto fail;
	}
    }

    if (relbits) {
	if (direct64) {
	    slotp->bss_d64_flags = flags;
	    slotp->bss_d64_base = pci_base;
	} else {
	    slotp->bss_d32_flags = flags;
	    slotp->bss_d32_base = pci_base;
	}
    }
    if (!inplace)
	alenlist_done(xtalk_alenlist);

    /* Reset the internal cursor of the alenlist to be returned back
     * to the caller.
     */
    alenlist_cursor_init(pciio_alenlist, 0, NULL);
    return pciio_alenlist;

  fail:
    if (relbits)
	pcibr_release_device(pcibr_soft, pciio_slot, relbits);
    if (pciio_alenlist && !inplace)
	alenlist_destroy(pciio_alenlist);
    return 0;
}

void
pcibr_dmamap_drain(pcibr_dmamap_t map)
{
    xtalk_dmamap_drain(map->bd_xtalk);
}

void
pcibr_dmaaddr_drain(devfs_handle_t pconn_vhdl,
		    paddr_t paddr,
		    size_t bytes)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    devfs_handle_t            xconn_vhdl = pcibr_soft->bs_conn;

    xtalk_dmaaddr_drain(xconn_vhdl, paddr, bytes);
}

void
pcibr_dmalist_drain(devfs_handle_t pconn_vhdl,
		    alenlist_t list)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    devfs_handle_t            xconn_vhdl = pcibr_soft->bs_conn;

    xtalk_dmalist_drain(xconn_vhdl, list);
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
    return (pcibr_dmamap->bd_pci_addr);
}

/* =====================================================================
 *    CONFIGURATION MANAGEMENT
 */
/*ARGSUSED */
void
pcibr_provider_startup(devfs_handle_t pcibr)
{
}

/*ARGSUSED */
void
pcibr_provider_shutdown(devfs_handle_t pcibr)
{
}

int
pcibr_reset(devfs_handle_t conn)
{
    pciio_info_t            pciio_info = pciio_info_get(conn);
    pciio_slot_t            pciio_slot = pciio_info_slot_get(pciio_info);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    bridge_t               *bridge = pcibr_soft->bs_base;
    bridgereg_t             ctlreg;
    unsigned                cfgctl[8];
    unsigned long           s;
    int                     f, nf;
    pcibr_info_h            pcibr_infoh;
    pcibr_info_t            pcibr_info;
    int                     win;

    if (pcibr_soft->bs_slot[pciio_slot].has_host) {
	pciio_slot = pcibr_soft->bs_slot[pciio_slot].host_slot;
	pcibr_info = pcibr_soft->bs_slot[pciio_slot].bss_infos[0];
    }
    if (pciio_slot < 4) {
	s = pcibr_lock(pcibr_soft);
	nf = pcibr_soft->bs_slot[pciio_slot].bss_ninfo;
	pcibr_infoh = pcibr_soft->bs_slot[pciio_slot].bss_infos;
	for (f = 0; f < nf; ++f)
	    if (pcibr_infoh[f])
		cfgctl[f] = bridge->b_type0_cfg_dev[pciio_slot].f[f].l[PCI_CFG_COMMAND / 4];

	ctlreg = bridge->b_wid_control;
	bridge->b_wid_control = ctlreg | BRIDGE_CTRL_RST(pciio_slot);
	/* XXX delay? */
	bridge->b_wid_control = ctlreg;
	/* XXX delay? */

	for (f = 0; f < nf; ++f)
	    if ((pcibr_info = pcibr_infoh[f]))
		for (win = 0; win < 6; ++win)
		    if (pcibr_info->f_window[win].w_base != 0)
			bridge->b_type0_cfg_dev[pciio_slot].f[f].l[PCI_CFG_BASE_ADDR(win) / 4] =
			    pcibr_info->f_window[win].w_base;
	for (f = 0; f < nf; ++f)
	    if (pcibr_infoh[f])
		bridge->b_type0_cfg_dev[pciio_slot].f[f].l[PCI_CFG_COMMAND / 4] = cfgctl[f];
	pcibr_unlock(pcibr_soft, s);

	return 0;
    }
#ifdef SUPPORT_PRINTING_V_FORMAT
    printk(KERN_WARNING   "%v: pcibr_reset unimplemented for slot %d\n",
	    conn, pciio_slot);
#endif
    return -1;
}

pciio_endian_t
pcibr_endian_set(devfs_handle_t pconn_vhdl,
		 pciio_endian_t device_end,
		 pciio_endian_t desired_end)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pciio_slot_t            pciio_slot = pciio_info_slot_get(pciio_info);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    bridgereg_t             devreg;
    unsigned long           s;

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
	bridge_t               *bridge = pcibr_soft->bs_base;

	bridge->b_device[pciio_slot].reg = devreg;
	pcibr_soft->bs_slot[pciio_slot].bss_device = devreg;
	bridge->b_wid_tflush;		/* wait until Bridge PIO complete */
    }
    pcibr_unlock(pcibr_soft, s);

#if DEBUG && PCIBR_DEV_DEBUG
    printk("pcibr Device(%d): 0x%p\n", pciio_slot, bridge->b_device[pciio_slot].reg);
#endif

    return desired_end;
}

/* This (re)sets the GBR and REALTIME bits and also keeps track of how
 * many sets are outstanding. Reset succeeds only if the number of outstanding
 * sets == 1.
 */
int
pcibr_priority_bits_set(pcibr_soft_t pcibr_soft,
			pciio_slot_t pciio_slot,
			pciio_priority_t device_prio)
{
    unsigned long           s;
    int                    *counter;
    bridgereg_t             rtbits = 0;
    bridgereg_t             devreg;
    int                     rc = PRIO_SUCCESS;

    /* in dual-slot configurations, the host and the
     * guest have separate DMA resources, so they
     * have separate requirements for priority bits.
     */

    counter = &(pcibr_soft->bs_slot[pciio_slot].bss_pri_uctr);

    /*
     * Bridge supports PCI notions of LOW and HIGH priority
     * arbitration rings via a "REAL_TIME" bit in the per-device
     * Bridge register. The "GBR" bit controls access to the GBR
     * ring on the xbow. These two bits are (re)set together.
     *
     * XXX- Bug in Rev B Bridge Si:
     * Symptom: Prefetcher starts operating incorrectly. This happens
     * due to corruption of the address storage ram in the prefetcher
     * when a non-real time PCI request is pulled and a real-time one is
     * put in it's place. Workaround: Use only a single arbitration ring
     * on PCI bus. GBR and RR can still be uniquely used per
     * device. NETLIST MERGE DONE, WILL BE FIXED IN REV C.
     */

    if (pcibr_soft->bs_rev_num != BRIDGE_PART_REV_B)
	rtbits |= BRIDGE_DEV_RT;

    /* NOTE- if we ever put DEV_RT or DEV_GBR on
     * the disabled list, we will have to take
     * it into account here.
     */

    s = pcibr_lock(pcibr_soft);
    devreg = pcibr_soft->bs_slot[pciio_slot].bss_device;
    if (device_prio == PCI_PRIO_HIGH) {
	if ((++*counter == 1)) {
	    if (rtbits)
		devreg |= rtbits;
	    else
		rc = PRIO_FAIL;
	}
    } else if (device_prio == PCI_PRIO_LOW) {
	if (*counter <= 0)
	    rc = PRIO_FAIL;
	else if (--*counter == 0)
	    if (rtbits)
		devreg &= ~rtbits;
    }
    if (pcibr_soft->bs_slot[pciio_slot].bss_device != devreg) {
	bridge_t               *bridge = pcibr_soft->bs_base;

	bridge->b_device[pciio_slot].reg = devreg;
	pcibr_soft->bs_slot[pciio_slot].bss_device = devreg;
	bridge->b_wid_tflush;		/* wait until Bridge PIO complete */
    }
    pcibr_unlock(pcibr_soft, s);

    return rc;
}

pciio_priority_t
pcibr_priority_set(devfs_handle_t pconn_vhdl,
		   pciio_priority_t device_prio)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pciio_slot_t            pciio_slot = pciio_info_slot_get(pciio_info);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);

    (void) pcibr_priority_bits_set(pcibr_soft, pciio_slot, device_prio);

    return device_prio;
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
pcibr_device_flags_set(devfs_handle_t pconn_vhdl,
		       pcibr_device_flags_t flags)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pciio_slot_t            pciio_slot = pciio_info_slot_get(pciio_info);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    bridgereg_t             set = 0;
    bridgereg_t             clr = 0;

    ASSERT((flags & PCIBR_DEVICE_FLAGS) == flags);

    if (flags & PCIBR_WRITE_GATHER)
	set |= BRIDGE_DEV_PMU_WRGA_EN;
    if (flags & PCIBR_NOWRITE_GATHER)
	clr |= BRIDGE_DEV_PMU_WRGA_EN;

    if (flags & PCIBR_WRITE_GATHER)
	set |= BRIDGE_DEV_DIR_WRGA_EN;
    if (flags & PCIBR_NOWRITE_GATHER)
	clr |= BRIDGE_DEV_DIR_WRGA_EN;

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

    if (set || clr) {
	bridgereg_t             devreg;
	unsigned long           s;

	s = pcibr_lock(pcibr_soft);
	devreg = pcibr_soft->bs_slot[pciio_slot].bss_device;
	devreg = (devreg & ~clr) | set;
	if (pcibr_soft->bs_slot[pciio_slot].bss_device != devreg) {
	    bridge_t               *bridge = pcibr_soft->bs_base;

	    bridge->b_device[pciio_slot].reg = devreg;
	    pcibr_soft->bs_slot[pciio_slot].bss_device = devreg;
	    bridge->b_wid_tflush;	/* wait until Bridge PIO complete */
	}
	pcibr_unlock(pcibr_soft, s);
#if DEBUG && PCIBR_DEV_DEBUG
	printk("pcibr Device(%d): %R\n", pciio_slot, bridge->b_device[pciio_slot].regbridge->b_device[pciio_slot].reg, device_bits);
#endif
    }
    return (1);
}

pciio_provider_t        pcibr_provider =
{
    (pciio_piomap_alloc_f *) pcibr_piomap_alloc,
    (pciio_piomap_free_f *) pcibr_piomap_free,
    (pciio_piomap_addr_f *) pcibr_piomap_addr,
    (pciio_piomap_done_f *) pcibr_piomap_done,
    (pciio_piotrans_addr_f *) pcibr_piotrans_addr,
    (pciio_piospace_alloc_f *) pcibr_piospace_alloc,
    (pciio_piospace_free_f *) pcibr_piospace_free,

    (pciio_dmamap_alloc_f *) pcibr_dmamap_alloc,
    (pciio_dmamap_free_f *) pcibr_dmamap_free,
    (pciio_dmamap_addr_f *) pcibr_dmamap_addr,
    (pciio_dmamap_list_f *) pcibr_dmamap_list,
    (pciio_dmamap_done_f *) pcibr_dmamap_done,
    (pciio_dmatrans_addr_f *) pcibr_dmatrans_addr,
    (pciio_dmatrans_list_f *) pcibr_dmatrans_list,
    (pciio_dmamap_drain_f *) pcibr_dmamap_drain,
    (pciio_dmaaddr_drain_f *) pcibr_dmaaddr_drain,
    (pciio_dmalist_drain_f *) pcibr_dmalist_drain,

    (pciio_intr_alloc_f *) pcibr_intr_alloc,
    (pciio_intr_free_f *) pcibr_intr_free,
    (pciio_intr_connect_f *) pcibr_intr_connect,
    (pciio_intr_disconnect_f *) pcibr_intr_disconnect,
    (pciio_intr_cpu_get_f *) pcibr_intr_cpu_get,

    (pciio_provider_startup_f *) pcibr_provider_startup,
    (pciio_provider_shutdown_f *) pcibr_provider_shutdown,
    (pciio_reset_f *) pcibr_reset,
    (pciio_write_gather_flush_f *) pcibr_write_gather_flush,
    (pciio_endian_set_f *) pcibr_endian_set,
    (pciio_priority_set_f *) pcibr_priority_set,
    (pciio_config_get_f *) pcibr_config_get,
    (pciio_config_set_f *) pcibr_config_set,

    (pciio_error_devenable_f *) 0,
    (pciio_error_extract_f *) 0,

#ifdef LATER
    (pciio_driver_reg_callback_f *) pcibr_driver_reg_callback,
    (pciio_driver_unreg_callback_f *) pcibr_driver_unreg_callback,
#else
    (pciio_driver_reg_callback_f *) 0,
    (pciio_driver_unreg_callback_f *) 0,
#endif
    (pciio_device_unregister_f 	*) pcibr_device_unregister,
    (pciio_dma_enabled_f		*) pcibr_dma_enabled,
};

int
pcibr_dma_enabled(devfs_handle_t pconn_vhdl)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
	

    return xtalk_dma_enabled(pcibr_soft->bs_conn);
}
