/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2002 Silicon Graphics, Inc. All rights reserved.
 */

int NeedXbridgeSwap = 0;

#include <linux/types.h>
#include <linux/config.h>
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

extern boolean_t                is_sys_critical_vertex(devfs_handle_t);

#undef PCIBR_ATE_DEBUG

#if 0
#define DEBUG 1	 /* To avoid lots of bad printk() formats leave off */
#endif
#define PCI_DEBUG 1
#define ATTACH_DEBUG 1
#define PCIBR_SOFT_LIST 1

#ifndef	LOCAL
#define	LOCAL		static
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

#define PCIBR_LLP_CONTROL_WAR
#if defined (PCIBR_LLP_CONTROL_WAR)
int                     pcibr_llp_control_war_cnt;
#endif				/* PCIBR_LLP_CONTROL_WAR */

int                     pcibr_devflag = D_MP;

#ifdef LATER
#define F(s,n)		{ 1l<<(s),-(s), n }

struct reg_desc         bridge_int_status_desc[] =
{
    F(31, "MULTI_ERR"),
    F(30, "PMU_ESIZE_EFAULT"),
    F(29, "UNEXPECTED_RESP"),
    F(28, "BAD_XRESP_PACKET"),
    F(27, "BAD_XREQ_PACKET"),
    F(26, "RESP_XTALK_ERROR"),
    F(25, "REQ_XTALK_ERROR"),
    F(24, "INVALID_ADDRESS"),
    F(23, "UNSUPPORTED_XOP"),
    F(22, "XREQ_FIFO_OFLOW"),
    F(21, "LLP_REC_SNERROR"),
    F(20, "LLP_REC_CBERROR"),
    F(19, "LLP_RCTY"),
    F(18, "LLP_TX_RETRY"),
    F(17, "LLP_TCTY"),
    F(16, "SSRAM_PERR"),
    F(15, "PCI_ABORT"),
    F(14, "PCI_PARITY"),
    F(13, "PCI_SERR"),
    F(12, "PCI_PERR"),
    F(11, "PCI_MASTER_TOUT"),
    F(10, "PCI_RETRY_CNT"),
    F(9, "XREAD_REQ_TOUT"),
    F(8, "GIO_BENABLE_ERR"),
    F(7, "INT7"),
    F(6, "INT6"),
    F(5, "INT5"),
    F(4, "INT4"),
    F(3, "INT3"),
    F(2, "INT2"),
    F(1, "INT1"),
    F(0, "INT0"),
    {0}
};

struct reg_values       space_v[] =
{
    {PCIIO_SPACE_NONE, "none"},
    {PCIIO_SPACE_ROM, "ROM"},
    {PCIIO_SPACE_IO, "I/O"},
    {PCIIO_SPACE_MEM, "MEM"},
    {PCIIO_SPACE_MEM32, "MEM(32)"},
    {PCIIO_SPACE_MEM64, "MEM(64)"},
    {PCIIO_SPACE_CFG, "CFG"},
    {PCIIO_SPACE_WIN(0), "WIN(0)"},
    {PCIIO_SPACE_WIN(1), "WIN(1)"},
    {PCIIO_SPACE_WIN(2), "WIN(2)"},
    {PCIIO_SPACE_WIN(3), "WIN(3)"},
    {PCIIO_SPACE_WIN(4), "WIN(4)"},
    {PCIIO_SPACE_WIN(5), "WIN(5)"},
    {PCIIO_SPACE_BAD, "BAD"},
    {0}
};

struct reg_desc         space_desc[] =
{
    {0xFF, 0, "space", 0, space_v},
    {0}
};

#if DEBUG
#define	device_desc	device_bits
LOCAL struct reg_desc   device_bits[] =
{
    {BRIDGE_DEV_ERR_LOCK_EN, 0, "ERR_LOCK_EN"},
    {BRIDGE_DEV_PAGE_CHK_DIS, 0, "PAGE_CHK_DIS"},
    {BRIDGE_DEV_FORCE_PCI_PAR, 0, "FORCE_PCI_PAR"},
    {BRIDGE_DEV_VIRTUAL_EN, 0, "VIRTUAL_EN"},
    {BRIDGE_DEV_PMU_WRGA_EN, 0, "PMU_WRGA_EN"},
    {BRIDGE_DEV_DIR_WRGA_EN, 0, "DIR_WRGA_EN"},
    {BRIDGE_DEV_DEV_SIZE, 0, "DEV_SIZE"},
    {BRIDGE_DEV_RT, 0, "RT"},
    {BRIDGE_DEV_SWAP_PMU, 0, "SWAP_PMU"},
    {BRIDGE_DEV_SWAP_DIR, 0, "SWAP_DIR"},
    {BRIDGE_DEV_PREF, 0, "PREF"},
    {BRIDGE_DEV_PRECISE, 0, "PRECISE"},
    {BRIDGE_DEV_COH, 0, "COH"},
    {BRIDGE_DEV_BARRIER, 0, "BARRIER"},
    {BRIDGE_DEV_GBR, 0, "GBR"},
    {BRIDGE_DEV_DEV_SWAP, 0, "DEV_SWAP"},
    {BRIDGE_DEV_DEV_IO_MEM, 0, "DEV_IO_MEM"},
    {BRIDGE_DEV_OFF_MASK, BRIDGE_DEV_OFF_ADDR_SHFT, "DEV_OFF", "%x"},
    {0}
};
#endif	/* DEBUG */

#ifdef SUPPORT_PRINTING_R_FORMAT
LOCAL struct reg_values xio_cmd_pactyp[] =
{
    {0x0, "RdReq"},
    {0x1, "RdResp"},
    {0x2, "WrReqWithResp"},
    {0x3, "WrResp"},
    {0x4, "WrReqNoResp"},
    {0x5, "Reserved(5)"},
    {0x6, "FetchAndOp"},
    {0x7, "Reserved(7)"},
    {0x8, "StoreAndOp"},
    {0x9, "Reserved(9)"},
    {0xa, "Reserved(a)"},
    {0xb, "Reserved(b)"},
    {0xc, "Reserved(c)"},
    {0xd, "Reserved(d)"},
    {0xe, "SpecialReq"},
    {0xf, "SpecialResp"},
    {0}
};

LOCAL struct reg_desc   xio_cmd_bits[] =
{
    {WIDGET_DIDN, -28, "DIDN", "%x"},
    {WIDGET_SIDN, -24, "SIDN", "%x"},
    {WIDGET_PACTYP, -20, "PACTYP", 0, xio_cmd_pactyp},
    {WIDGET_TNUM, -15, "TNUM", "%x"},
    {WIDGET_COHERENT, 0, "COHERENT"},
    {WIDGET_DS, 0, "DS"},
    {WIDGET_GBR, 0, "GBR"},
    {WIDGET_VBPM, 0, "VBPM"},
    {WIDGET_ERROR, 0, "ERROR"},
    {WIDGET_BARRIER, 0, "BARRIER"},
    {0}
};
#endif	/* SUPPORT_PRINTING_R_FORMAT */

#if PCIBR_FREEZE_TIME || PCIBR_ATE_DEBUG
LOCAL struct reg_desc   ate_bits[] =
{
    {0xFFFF000000000000ull, -48, "RMF", "%x"},
    {~(IOPGSIZE - 1) &			/* may trim off some low bits */
     0x0000FFFFFFFFF000ull, 0, "XIO", "%x"},
    {0x0000000000000F00ull, -8, "port", "%x"},
    {0x0000000000000010ull, 0, "Barrier"},
    {0x0000000000000008ull, 0, "Prefetch"},
    {0x0000000000000004ull, 0, "Precise"},
    {0x0000000000000002ull, 0, "Coherent"},
    {0x0000000000000001ull, 0, "Valid"},
    {0}
};
#endif

#if PCIBR_ATE_DEBUG
LOCAL struct reg_values ssram_sizes[] =
{
    {BRIDGE_CTRL_SSRAM_512K, "512k"},
    {BRIDGE_CTRL_SSRAM_128K, "128k"},
    {BRIDGE_CTRL_SSRAM_64K, "64k"},
    {BRIDGE_CTRL_SSRAM_1K, "1k"},
    {0}
};

LOCAL struct reg_desc   control_bits[] =
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
    {BRIDGE_CTRL_MAX_TRANS_MASK, -4, "MAX_TRANS", "%d"},
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
    pciio_piospace_t        next;	/* another space for this device */
    char                    free;	/* 1 if free, 0 if in use               */
    pciio_space_t           space;	/* Which space is in use                */
    iopaddr_t               start;	/* Starting address of the PIO space    */
    size_t                  count;	/* size of PIO space                    */
};

#if PCIBR_SOFT_LIST
pcibr_list_p            pcibr_list = 0;
#endif

#define	INFO_LBL_PCIBR_ASIC_REV	"_pcibr_asic_rev"

#define	PCIBR_D64_BASE_UNSET	(0xFFFFFFFFFFFFFFFF)
#define	PCIBR_D32_BASE_UNSET	(0xFFFFFFFF)

#define PCIBR_VALID_SLOT(s)	(s < 8)

#ifdef SN_XXX
extern int      hub_device_flags_set(devfs_handle_t       widget_dev,
                                     hub_widget_flags_t flags);
#endif
extern pciio_dmamap_t get_free_pciio_dmamap(devfs_handle_t);
extern void free_pciio_dmamap(pcibr_dmamap_t);

/*
 * This is the file operation table for the pcibr driver.
 * As each of the functions are implemented, put the 
 * appropriate function name below.
 */
struct file_operations pcibr_fops = {
        owner:  THIS_MODULE,
        llseek: NULL,
        read: NULL,
        write: NULL,
        readdir: NULL,
        poll: NULL,
        ioctl: NULL,
        mmap: NULL,
        open: NULL,
        flush: NULL,
        release: NULL,
        fsync: NULL,
        fasync: NULL,
        lock: NULL,
        readv: NULL,
        writev: NULL
};

extern devfs_handle_t hwgraph_root;
extern graph_error_t hwgraph_vertex_unref(devfs_handle_t vhdl);
extern int cap_able(uint64_t x);
extern uint64_t rmalloc(struct map *mp, size_t size);
extern void rmfree(struct map *mp, size_t size, uint64_t a);
extern int hwgraph_vertex_name_get(devfs_handle_t vhdl, char *buf, uint buflen);
extern long atoi(register char *p);
extern void *swap_ptr(void **loc, void *new);
extern char *dev_to_name(devfs_handle_t dev, char *buf, uint buflen);
extern cnodeid_t nodevertex_to_cnodeid(devfs_handle_t vhdl);
extern graph_error_t hwgraph_edge_remove(devfs_handle_t from, char *name, devfs_handle_t *toptr);
extern struct map *rmallocmap(uint64_t mapsiz);
extern void rmfreemap(struct map *mp);
extern int compare_and_swap_ptr(void **location, void *old_ptr, void *new_ptr);
extern int io_path_map_widget(devfs_handle_t vertex);



/* =====================================================================
 *    Function Table of Contents
 *
 *      The order of functions in this file has stopped
 *      making much sense. We might want to take a look
 *      at it some time and bring back some sanity, or
 *      perhaps bust this file into smaller chunks.
 */

LOCAL void              do_pcibr_rrb_clear(bridge_t *, int);
LOCAL void              do_pcibr_rrb_flush(bridge_t *, int);
LOCAL int               do_pcibr_rrb_count_valid(bridge_t *, pciio_slot_t);
LOCAL int               do_pcibr_rrb_count_avail(bridge_t *, pciio_slot_t);
LOCAL int               do_pcibr_rrb_alloc(bridge_t *, pciio_slot_t, int);
LOCAL int               do_pcibr_rrb_free(bridge_t *, pciio_slot_t, int);

LOCAL void              do_pcibr_rrb_autoalloc(pcibr_soft_t, int, int);

int			pcibr_wrb_flush(devfs_handle_t);
int                     pcibr_rrb_alloc(devfs_handle_t, int *, int *);
int                     pcibr_rrb_check(devfs_handle_t, int *, int *, int *, int *);
int                     pcibr_alloc_all_rrbs(devfs_handle_t, int, int, int, int, int, int, int, int, int);
void                    pcibr_rrb_flush(devfs_handle_t);

LOCAL int               pcibr_try_set_device(pcibr_soft_t, pciio_slot_t, unsigned, bridgereg_t);
void                    pcibr_release_device(pcibr_soft_t, pciio_slot_t, bridgereg_t);

LOCAL void              pcibr_clearwidint(bridge_t *);
LOCAL void              pcibr_setwidint(xtalk_intr_t);
LOCAL int               pcibr_probe_slot(bridge_t *, cfg_p, unsigned *);

void                    pcibr_init(void);
int                     pcibr_attach(devfs_handle_t);
int			pcibr_detach(devfs_handle_t);
int                     pcibr_open(devfs_handle_t *, int, int, cred_t *);
int                     pcibr_close(devfs_handle_t, int, int, cred_t *);
int                     pcibr_map(devfs_handle_t, vhandl_t *, off_t, size_t, uint);
int                     pcibr_unmap(devfs_handle_t, vhandl_t *);
int                     pcibr_ioctl(devfs_handle_t, int, void *, int, struct cred *, int *);

void                    pcibr_freeblock_sub(iopaddr_t *, iopaddr_t *, iopaddr_t, size_t);

LOCAL int               pcibr_init_ext_ate_ram(bridge_t *);
LOCAL int               pcibr_ate_alloc(pcibr_soft_t, int);
LOCAL void              pcibr_ate_free(pcibr_soft_t, int, int);

LOCAL pcibr_info_t      pcibr_info_get(devfs_handle_t);
LOCAL pcibr_info_t      pcibr_device_info_new(pcibr_soft_t, pciio_slot_t, pciio_function_t, pciio_vendor_id_t, pciio_device_id_t);
LOCAL void		pcibr_device_info_free(devfs_handle_t, pciio_slot_t);
LOCAL iopaddr_t         pcibr_addr_pci_to_xio(devfs_handle_t, pciio_slot_t, pciio_space_t, iopaddr_t, size_t, unsigned);

pcibr_piomap_t          pcibr_piomap_alloc(devfs_handle_t, device_desc_t, pciio_space_t, iopaddr_t, size_t, size_t, unsigned);
void                    pcibr_piomap_free(pcibr_piomap_t);
caddr_t                 pcibr_piomap_addr(pcibr_piomap_t, iopaddr_t, size_t);
void                    pcibr_piomap_done(pcibr_piomap_t);
caddr_t                 pcibr_piotrans_addr(devfs_handle_t, device_desc_t, pciio_space_t, iopaddr_t, size_t, unsigned);
iopaddr_t               pcibr_piospace_alloc(devfs_handle_t, device_desc_t, pciio_space_t, size_t, size_t);
void                    pcibr_piospace_free(devfs_handle_t, pciio_space_t, iopaddr_t, size_t);

LOCAL iopaddr_t         pcibr_flags_to_d64(unsigned, pcibr_soft_t);
LOCAL bridge_ate_t      pcibr_flags_to_ate(unsigned);

pcibr_dmamap_t          pcibr_dmamap_alloc(devfs_handle_t, device_desc_t, size_t, unsigned);
void                    pcibr_dmamap_free(pcibr_dmamap_t);
LOCAL bridge_ate_p      pcibr_ate_addr(pcibr_soft_t, int);
LOCAL iopaddr_t         pcibr_addr_xio_to_pci(pcibr_soft_t, iopaddr_t, size_t);
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

static unsigned		pcibr_intr_bits(pciio_info_t info, pciio_intr_line_t lines);
pcibr_intr_t            pcibr_intr_alloc(devfs_handle_t, device_desc_t, pciio_intr_line_t, devfs_handle_t);
void                    pcibr_intr_free(pcibr_intr_t);
LOCAL void              pcibr_setpciint(xtalk_intr_t);
int                     pcibr_intr_connect(pcibr_intr_t);
void                    pcibr_intr_disconnect(pcibr_intr_t);

devfs_handle_t            pcibr_intr_cpu_get(pcibr_intr_t);
void                    pcibr_xintr_preset(void *, int, xwidgetnum_t, iopaddr_t, xtalk_intr_vector_t);
void                    pcibr_intr_func(intr_arg_t);

void                    pcibr_provider_startup(devfs_handle_t);
void                    pcibr_provider_shutdown(devfs_handle_t);

int                     pcibr_reset(devfs_handle_t);
pciio_endian_t          pcibr_endian_set(devfs_handle_t, pciio_endian_t, pciio_endian_t);
int                     pcibr_priority_bits_set(pcibr_soft_t, pciio_slot_t, pciio_priority_t);
pciio_priority_t        pcibr_priority_set(devfs_handle_t, pciio_priority_t);
int                     pcibr_device_flags_set(devfs_handle_t, pcibr_device_flags_t);

LOCAL cfg_p             pcibr_config_addr(devfs_handle_t, unsigned);
uint64_t                pcibr_config_get(devfs_handle_t, unsigned, unsigned);
LOCAL uint64_t          do_pcibr_config_get(cfg_p, unsigned, unsigned);
void                    pcibr_config_set(devfs_handle_t, unsigned, unsigned, uint64_t);
LOCAL void              do_pcibr_config_set(cfg_p, unsigned, unsigned, uint64_t);

LOCAL pcibr_hints_t     pcibr_hints_get(devfs_handle_t, int);
void                    pcibr_hints_fix_rrbs(devfs_handle_t);
void                    pcibr_hints_dualslot(devfs_handle_t, pciio_slot_t, pciio_slot_t);
void			pcibr_hints_intr_bits(devfs_handle_t, pcibr_intr_bits_f *);
void                    pcibr_set_rrb_callback(devfs_handle_t, rrb_alloc_funct_t);
void                    pcibr_hints_handsoff(devfs_handle_t);
void                    pcibr_hints_subdevs(devfs_handle_t, pciio_slot_t, ulong);

LOCAL int		pcibr_slot_info_init(devfs_handle_t,pciio_slot_t);
LOCAL int		pcibr_slot_info_free(devfs_handle_t,pciio_slot_t);

#ifdef LATER
LOCAL int	        pcibr_slot_info_return(pcibr_soft_t, pciio_slot_t,
                                               pcibr_slot_info_resp_t);
LOCAL void       	pcibr_slot_func_info_return(pcibr_info_h, int,
                                                    pcibr_slot_func_info_resp_t);
#endif	/* LATER */

LOCAL int		pcibr_slot_addr_space_init(devfs_handle_t,pciio_slot_t);
LOCAL int		pcibr_slot_device_init(devfs_handle_t, pciio_slot_t);
LOCAL int		pcibr_slot_guest_info_init(devfs_handle_t,pciio_slot_t);
LOCAL int		pcibr_slot_initial_rrb_alloc(devfs_handle_t,pciio_slot_t);
LOCAL int		pcibr_slot_call_device_attach(devfs_handle_t,
						      pciio_slot_t, int);
LOCAL int		pcibr_slot_call_device_detach(devfs_handle_t,
						      pciio_slot_t, int);

LOCAL int               pcibr_slot_detach(devfs_handle_t, pciio_slot_t, int);
LOCAL int 		pcibr_is_slot_sys_critical(devfs_handle_t, pciio_slot_t);
#ifdef LATER
LOCAL int		pcibr_slot_query(devfs_handle_t, pcibr_slot_info_req_t);
#endif

/* =====================================================================
 *    RRB management
 */

#define LSBIT(word)		((word) &~ ((word)-1))

#define PCIBR_RRB_SLOT_VIRTUAL	8

LOCAL void
do_pcibr_rrb_clear(bridge_t *bridge, int rrb)
{
    bridgereg_t             status;

    /* bridge_lock must be held;
     * this RRB must be disabled.
     */

    /* wait until RRB has no outstanduing XIO packets. */
    while ((status = bridge->b_resp_status) & BRIDGE_RRB_INUSE(rrb)) {
	;				/* XXX- beats on bridge. bad idea? */
    }

    /* if the RRB has data, drain it. */
    if (status & BRIDGE_RRB_VALID(rrb)) {
	bridge->b_resp_clear = BRIDGE_RRB_CLEAR(rrb);

	/* wait until RRB is no longer valid. */
	while ((status = bridge->b_resp_status) & BRIDGE_RRB_VALID(rrb)) {
	    ;				/* XXX- beats on bridge. bad idea? */
	}
    }
}

LOCAL void
do_pcibr_rrb_flush(bridge_t *bridge, int rrbn)
{
    reg_p                   rrbp = &bridge->b_rrb_map[rrbn & 1].reg;
    bridgereg_t             rrbv;
    int                     shft = 4 * (rrbn >> 1);
    unsigned                ebit = BRIDGE_RRB_EN << shft;

    rrbv = *rrbp;
    if (rrbv & ebit)
	*rrbp = rrbv & ~ebit;

    do_pcibr_rrb_clear(bridge, rrbn);

    if (rrbv & ebit)
	*rrbp = rrbv;
}

/*
 *    pcibr_rrb_count_valid: count how many RRBs are
 *      marked valid for the specified PCI slot on this
 *      bridge.
 *
 *      NOTE: The "slot" parameter for all pcibr_rrb
 *      management routines must include the "virtual"
 *      bit; when manageing both the normal and the
 *      virtual channel, separate calls to these
 *      routines must be made. To denote the virtual
 *      channel, add PCIBR_RRB_SLOT_VIRTUAL to the slot
 *      number.
 *
 *      IMPL NOTE: The obvious algorithm is to iterate
 *      through the RRB fields, incrementing a count if
 *      the RRB is valid and matches the slot. However,
 *      it is much simpler to use an algorithm derived
 *      from the "partitioned add" idea. First, XOR in a
 *      pattern such that the fields that match this
 *      slot come up "all ones" and all other fields
 *      have zeros in the mismatching bits. Then AND
 *      together the bits in the field, so we end up
 *      with one bit turned on for each field that
 *      matched. Now we need to count these bits. This
 *      can be done either with a series of shift/add
 *      instructions or by using "tmp % 15"; I expect
 *      that the cascaded shift/add will be faster.
 */

LOCAL int
do_pcibr_rrb_count_valid(bridge_t *bridge,
			 pciio_slot_t slot)
{
    bridgereg_t             tmp;

    tmp = bridge->b_rrb_map[slot & 1].reg;
    tmp ^= 0x11111111 * (7 - slot / 2);
    tmp &= (0xCCCCCCCC & tmp) >> 2;
    tmp &= (0x22222222 & tmp) >> 1;
    tmp += tmp >> 4;
    tmp += tmp >> 8;
    tmp += tmp >> 16;
    return tmp & 15;
}

/*
 *    do_pcibr_rrb_count_avail: count how many RRBs are
 *      available to be allocated for the specified slot.
 *
 *      IMPL NOTE: similar to the above, except we are
 *      just counting how many fields have the valid bit
 *      turned off.
 */
LOCAL int
do_pcibr_rrb_count_avail(bridge_t *bridge,
			 pciio_slot_t slot)
{
    bridgereg_t             tmp;

    tmp = bridge->b_rrb_map[slot & 1].reg;
    tmp = (0x88888888 & ~tmp) >> 3;
    tmp += tmp >> 4;
    tmp += tmp >> 8;
    tmp += tmp >> 16;
    return tmp & 15;
}

/*
 *    do_pcibr_rrb_alloc: allocate some additional RRBs
 *      for the specified slot. Returns -1 if there were
 *      insufficient free RRBs to satisfy the request,
 *      or 0 if the request was fulfilled.
 *
 *      Note that if a request can be partially filled,
 *      it will be, even if we return failure.
 *
 *      IMPL NOTE: again we avoid iterating across all
 *      the RRBs; instead, we form up a word containing
 *      one bit for each free RRB, then peel the bits
 *      off from the low end.
 */
LOCAL int
do_pcibr_rrb_alloc(bridge_t *bridge,
		   pciio_slot_t slot,
		   int more)
{
    int                     rv = 0;
    bridgereg_t             reg, tmp, bit;

    reg = bridge->b_rrb_map[slot & 1].reg;
    tmp = (0x88888888 & ~reg) >> 3;
    while (more-- > 0) {
	bit = LSBIT(tmp);
	if (!bit) {
	    rv = -1;
	    break;
	}
	tmp &= ~bit;
	reg = ((reg & ~(bit * 15)) | (bit * (8 + slot / 2)));
    }
    bridge->b_rrb_map[slot & 1].reg = reg;
    return rv;
}

/*
 *    do_pcibr_rrb_free: release some of the RRBs that
 *      have been allocated for the specified
 *      slot. Returns zero for success, or negative if
 *      it was unable to free that many RRBs.
 *
 *      IMPL NOTE: We form up a bit for each RRB
 *      allocated to the slot, aligned with the VALID
 *      bitfield this time; then we peel bits off one at
 *      a time, releasing the corresponding RRB.
 */
LOCAL int
do_pcibr_rrb_free(bridge_t *bridge,
		  pciio_slot_t slot,
		  int less)
{
    int                     rv = 0;
    bridgereg_t             reg, tmp, clr, bit;
    int                     i;

    clr = 0;
    reg = bridge->b_rrb_map[slot & 1].reg;

    /* This needs to be done otherwise the rrb's on the virtual channel
     * for this slot won't be freed !!
     */
    tmp = reg & 0xbbbbbbbb;

    tmp ^= (0x11111111 * (7 - slot / 2));
    tmp &= (0x33333333 & tmp) << 2;
    tmp &= (0x44444444 & tmp) << 1;
    while (less-- > 0) {
	bit = LSBIT(tmp);
	if (!bit) {
	    rv = -1;
	    break;
	}
	tmp &= ~bit;
	reg &= ~bit;
	clr |= bit;
    }
    bridge->b_rrb_map[slot & 1].reg = reg;

    for (i = 0; i < 8; i++)
	if (clr & (8 << (4 * i)))
	    do_pcibr_rrb_clear(bridge, (2 * i) + (slot & 1));

    return rv;
}

LOCAL void
do_pcibr_rrb_autoalloc(pcibr_soft_t pcibr_soft,
		       int slot,
		       int more_rrbs)
{
    bridge_t               *bridge = pcibr_soft->bs_base;
    int                     got;

    for (got = 0; got < more_rrbs; ++got) {
	if (pcibr_soft->bs_rrb_res[slot & 7] > 0)
	    pcibr_soft->bs_rrb_res[slot & 7]--;
	else if (pcibr_soft->bs_rrb_avail[slot & 1] > 0)
	    pcibr_soft->bs_rrb_avail[slot & 1]--;
	else
	    break;
	if (do_pcibr_rrb_alloc(bridge, slot, 1) < 0)
	    break;
#if PCIBR_RRB_DEBUG
	printk( "do_pcibr_rrb_autoalloc: add one to slot %d%s\n",
		slot & 7, slot & 8 ? "v" : "");
#endif
	pcibr_soft->bs_rrb_valid[slot]++;
    }
#if PCIBR_RRB_DEBUG
    printk("%s: %d+%d free RRBs. Allocation list:\n", pcibr_soft->bs_name,
	    pcibr_soft->bs_rrb_avail[0],
	    pcibr_soft->bs_rrb_avail[1]);
    for (slot = 0; slot < 8; ++slot)
	printk("\t%d+%d+%d",
		0xFFF & pcibr_soft->bs_rrb_valid[slot],
		0xFFF & pcibr_soft->bs_rrb_valid[slot + PCIBR_RRB_SLOT_VIRTUAL],
		pcibr_soft->bs_rrb_res[slot]);
	printk("\n");
#endif
}

/*
 * Device driver interface to flush the write buffers for a specified
 * device hanging off the bridge.
 */
int
pcibr_wrb_flush(devfs_handle_t pconn_vhdl)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pciio_slot_t            pciio_slot = pciio_info_slot_get(pciio_info);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    bridge_t               *bridge = pcibr_soft->bs_base;
    volatile bridgereg_t   *wrb_flush;

    wrb_flush = &(bridge->b_wr_req_buf[pciio_slot].reg);
    while (*wrb_flush);

    return(0);
}
/*
 * Device driver interface to request RRBs for a specified device
 * hanging off a Bridge.  The driver requests the total number of
 * RRBs it would like for the normal channel (vchan0) and for the
 * "virtual channel" (vchan1).  The actual number allocated to each
 * channel is returned.
 *
 * If we cannot allocate at least one RRB to a channel that needs
 * at least one, return -1 (failure).  Otherwise, satisfy the request
 * as best we can and return 0.
 */
int
pcibr_rrb_alloc(devfs_handle_t pconn_vhdl,
		int *count_vchan0,
		int *count_vchan1)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pciio_slot_t            pciio_slot = pciio_info_slot_get(pciio_info);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    bridge_t               *bridge = pcibr_soft->bs_base;
    int                     desired_vchan0;
    int                     desired_vchan1;
    int                     orig_vchan0;
    int                     orig_vchan1;
    int                     delta_vchan0;
    int                     delta_vchan1;
    int                     final_vchan0;
    int                     final_vchan1;
    int                     avail_rrbs;
    unsigned long           s;
    int                     error;

    /*
     * TBD: temper request with admin info about RRB allocation,
     * and according to demand from other devices on this Bridge.
     *
     * One way of doing this would be to allocate two RRBs
     * for each device on the bus, before any drivers start
     * asking for extras. This has the weakness that one
     * driver might not give back an "extra" RRB until after
     * another driver has already failed to get one that
     * it wanted.
     */

    s = pcibr_lock(pcibr_soft);

    /* How many RRBs do we own? */
    orig_vchan0 = pcibr_soft->bs_rrb_valid[pciio_slot];
    orig_vchan1 = pcibr_soft->bs_rrb_valid[pciio_slot + PCIBR_RRB_SLOT_VIRTUAL];

    /* How many RRBs do we want? */
    desired_vchan0 = count_vchan0 ? *count_vchan0 : orig_vchan0;
    desired_vchan1 = count_vchan1 ? *count_vchan1 : orig_vchan1;

    /* How many RRBs are free? */
    avail_rrbs = pcibr_soft->bs_rrb_avail[pciio_slot & 1]
	+ pcibr_soft->bs_rrb_res[pciio_slot];

    /* Figure desired deltas */
    delta_vchan0 = desired_vchan0 - orig_vchan0;
    delta_vchan1 = desired_vchan1 - orig_vchan1;

    /* Trim back deltas to something
     * that we can actually meet, by
     * decreasing the ending allocation
     * for whichever channel wants
     * more RRBs. If both want the same
     * number, cut the second channel.
     * NOTE: do not change the allocation for
     * a channel that was passed as NULL.
     */
    while ((delta_vchan0 + delta_vchan1) > avail_rrbs) {
	if (count_vchan0 &&
	    (!count_vchan1 ||
	     ((orig_vchan0 + delta_vchan0) >
	      (orig_vchan1 + delta_vchan1))))
	    delta_vchan0--;
	else
	    delta_vchan1--;
    }

    /* Figure final RRB allocations
     */
    final_vchan0 = orig_vchan0 + delta_vchan0;
    final_vchan1 = orig_vchan1 + delta_vchan1;

    /* If either channel wants RRBs but our actions
     * would leave it with none, declare an error,
     * but DO NOT change any RRB allocations.
     */
    if ((desired_vchan0 && !final_vchan0) ||
	(desired_vchan1 && !final_vchan1)) {

	error = -1;

    } else {

	/* Commit the allocations: free, then alloc.
	 */
	if (delta_vchan0 < 0)
	    (void) do_pcibr_rrb_free(bridge, pciio_slot, -delta_vchan0);
	if (delta_vchan1 < 0)
	    (void) do_pcibr_rrb_free(bridge, PCIBR_RRB_SLOT_VIRTUAL + pciio_slot, -delta_vchan1);

	if (delta_vchan0 > 0)
	    (void) do_pcibr_rrb_alloc(bridge, pciio_slot, delta_vchan0);
	if (delta_vchan1 > 0)
	    (void) do_pcibr_rrb_alloc(bridge, PCIBR_RRB_SLOT_VIRTUAL + pciio_slot, delta_vchan1);

	/* Return final values to caller.
	 */
	if (count_vchan0)
	    *count_vchan0 = final_vchan0;
	if (count_vchan1)
	    *count_vchan1 = final_vchan1;

	/* prevent automatic changes to this slot's RRBs
	 */
	pcibr_soft->bs_rrb_fixed |= 1 << pciio_slot;

	/* Track the actual allocations, release
	 * any further reservations, and update the
	 * number of available RRBs.
	 */

	pcibr_soft->bs_rrb_valid[pciio_slot] = final_vchan0;
	pcibr_soft->bs_rrb_valid[pciio_slot + PCIBR_RRB_SLOT_VIRTUAL] = final_vchan1;
	pcibr_soft->bs_rrb_avail[pciio_slot & 1] =
	    pcibr_soft->bs_rrb_avail[pciio_slot & 1]
	    + pcibr_soft->bs_rrb_res[pciio_slot]
	    - delta_vchan0
	    - delta_vchan1;
	pcibr_soft->bs_rrb_res[pciio_slot] = 0;

#if PCIBR_RRB_DEBUG
	printk("pcibr_rrb_alloc: slot %d set to %d+%d; %d+%d free\n",
		pciio_slot, final_vchan0, final_vchan1,
		pcibr_soft->bs_rrb_avail[0],
		pcibr_soft->bs_rrb_avail[1]);
	for (pciio_slot = 0; pciio_slot < 8; ++pciio_slot)
	    printk("\t%d+%d+%d",
		    0xFFF & pcibr_soft->bs_rrb_valid[pciio_slot],
		    0xFFF & pcibr_soft->bs_rrb_valid[pciio_slot + PCIBR_RRB_SLOT_VIRTUAL],
		    pcibr_soft->bs_rrb_res[pciio_slot]);
	printk("\n");
#endif

	error = 0;
    }

    pcibr_unlock(pcibr_soft, s);
    return error;
}

/*
 * Device driver interface to check the current state
 * of the RRB allocations.
 *
 *   pconn_vhdl is your PCI connection point (specifies which
 *      PCI bus and which slot).
 *
 *   count_vchan0 points to where to return the number of RRBs
 *      assigned to the primary DMA channel, used by all DMA
 *      that does not explicitly ask for the alternate virtual
 *      channel.
 *
 *   count_vchan1 points to where to return the number of RRBs
 *      assigned to the secondary DMA channel, used when
 *      PCIBR_VCHAN1 and PCIIO_DMA_A64 are specified.
 *
 *   count_reserved points to where to return the number of RRBs
 *      that have been automatically reserved for your device at
 *      startup, but which have not been assigned to a
 *      channel. RRBs must be assigned to a channel to be used;
 *      this can be done either with an explicit pcibr_rrb_alloc
 *      call, or automatically by the infrastructure when a DMA
 *      translation is constructed. Any call to pcibr_rrb_alloc
 *      will release any unassigned reserved RRBs back to the
 *      free pool.
 *
 *   count_pool points to where to return the number of RRBs
 *      that are currently unassigned and unreserved. This
 *      number can (and will) change as other drivers make calls
 *      to pcibr_rrb_alloc, or automatically allocate RRBs for
 *      DMA beyond their initial reservation.
 *
 * NULL may be passed for any of the return value pointers
 * the caller is not interested in.
 *
 * The return value is "0" if all went well, or "-1" if
 * there is a problem. Additionally, if the wrong vertex
 * is passed in, one of the subsidiary support functions
 * could panic with a "bad pciio fingerprint."
 */

int
pcibr_rrb_check(devfs_handle_t pconn_vhdl,
		int *count_vchan0,
		int *count_vchan1,
		int *count_reserved,
		int *count_pool)
{
    pciio_info_t            pciio_info;
    pciio_slot_t            pciio_slot;
    pcibr_soft_t            pcibr_soft;
    unsigned long           s;
    int                     error = -1;

    if ((pciio_info = pciio_info_get(pconn_vhdl)) &&
	(pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info)) &&
	((pciio_slot = pciio_info_slot_get(pciio_info)) < 8)) {

	s = pcibr_lock(pcibr_soft);

	if (count_vchan0)
	    *count_vchan0 =
		pcibr_soft->bs_rrb_valid[pciio_slot];

	if (count_vchan1)
	    *count_vchan1 =
		pcibr_soft->bs_rrb_valid[pciio_slot + PCIBR_RRB_SLOT_VIRTUAL];

	if (count_reserved)
	    *count_reserved =
		pcibr_soft->bs_rrb_res[pciio_slot];

	if (count_pool)
	    *count_pool =
		pcibr_soft->bs_rrb_avail[pciio_slot & 1];

	error = 0;

	pcibr_unlock(pcibr_soft, s);
    }
    return error;
}

/* pcibr_alloc_all_rrbs allocates all the rrbs available in the quantities
 * requested for each of the devies.  The evn_odd argument indicates whether
 * allcoation for the odd or even rrbs is requested and next group of four pairse
 * are the amount to assign to each device (they should sum to <= 8) and
 * whether to set the viritual bit for that device (1 indictaes yes, 0 indicates no)
 * the devices in order are either 0, 2, 4, 6 or 1, 3, 5, 7
 * if even_odd is even we alloc even rrbs else we allocate odd rrbs
 * returns 0 if no errors else returns -1
 */

int
pcibr_alloc_all_rrbs(devfs_handle_t vhdl, int even_odd,
		     int dev_1_rrbs, int virt1, int dev_2_rrbs, int virt2,
		     int dev_3_rrbs, int virt3, int dev_4_rrbs, int virt4)
{
    devfs_handle_t            pcibr_vhdl;
    pcibr_soft_t            pcibr_soft = NULL;
    bridge_t               *bridge = NULL;

    uint32_t              rrb_setting = 0;
    int                     rrb_shift = 7;
    uint32_t              cur_rrb;
    int                     dev_rrbs[4];
    int                     virt[4];
    int                     i, j;
    unsigned long           s;

    if (GRAPH_SUCCESS ==
	hwgraph_traverse(vhdl, EDGE_LBL_PCI, &pcibr_vhdl)) {
	pcibr_soft = pcibr_soft_get(pcibr_vhdl);
	if (pcibr_soft)
	    bridge = pcibr_soft->bs_base;
	hwgraph_vertex_unref(pcibr_vhdl);
    }
    if (bridge == NULL)
	bridge = (bridge_t *) xtalk_piotrans_addr
	    (vhdl, NULL, 0, sizeof(bridge_t), 0);

    even_odd &= 1;

    dev_rrbs[0] = dev_1_rrbs;
    dev_rrbs[1] = dev_2_rrbs;
    dev_rrbs[2] = dev_3_rrbs;
    dev_rrbs[3] = dev_4_rrbs;

    virt[0] = virt1;
    virt[1] = virt2;
    virt[2] = virt3;
    virt[3] = virt4;

    if ((dev_1_rrbs + dev_2_rrbs + dev_3_rrbs + dev_4_rrbs) > 8) {
	return -1;
    }
    if ((dev_1_rrbs < 0) || (dev_2_rrbs < 0) || (dev_3_rrbs < 0) || (dev_4_rrbs < 0)) {
	return -1;
    }
    /* walk through rrbs */
    for (i = 0; i < 4; i++) {
	if (virt[i]) {
	    cur_rrb = i | 0xc;
	    cur_rrb = cur_rrb << (rrb_shift * 4);
	    rrb_shift--;
	    rrb_setting = rrb_setting | cur_rrb;
	    dev_rrbs[i] = dev_rrbs[i] - 1;
	}
	for (j = 0; j < dev_rrbs[i]; j++) {
	    cur_rrb = i | 0x8;
	    cur_rrb = cur_rrb << (rrb_shift * 4);
	    rrb_shift--;
	    rrb_setting = rrb_setting | cur_rrb;
	}
    }

    if (pcibr_soft)
	s = pcibr_lock(pcibr_soft);

    bridge->b_rrb_map[even_odd].reg = rrb_setting;

    if (pcibr_soft) {

	pcibr_soft->bs_rrb_fixed |= 0x55 << even_odd;

	/* since we've "FIXED" the allocations
	 * for these slots, we probably can dispense
	 * with tracking avail/res/valid data, but
	 * keeping it up to date helps debugging.
	 */

	pcibr_soft->bs_rrb_avail[even_odd] =
	    8 - (dev_1_rrbs + dev_2_rrbs + dev_3_rrbs + dev_4_rrbs);

	pcibr_soft->bs_rrb_res[even_odd + 0] = 0;
	pcibr_soft->bs_rrb_res[even_odd + 2] = 0;
	pcibr_soft->bs_rrb_res[even_odd + 4] = 0;
	pcibr_soft->bs_rrb_res[even_odd + 6] = 0;

	pcibr_soft->bs_rrb_valid[even_odd + 0] = dev_1_rrbs - virt1;
	pcibr_soft->bs_rrb_valid[even_odd + 2] = dev_2_rrbs - virt2;
	pcibr_soft->bs_rrb_valid[even_odd + 4] = dev_3_rrbs - virt3;
	pcibr_soft->bs_rrb_valid[even_odd + 6] = dev_4_rrbs - virt4;

	pcibr_soft->bs_rrb_valid[even_odd + 0 + PCIBR_RRB_SLOT_VIRTUAL] = virt1;
	pcibr_soft->bs_rrb_valid[even_odd + 2 + PCIBR_RRB_SLOT_VIRTUAL] = virt2;
	pcibr_soft->bs_rrb_valid[even_odd + 4 + PCIBR_RRB_SLOT_VIRTUAL] = virt3;
	pcibr_soft->bs_rrb_valid[even_odd + 6 + PCIBR_RRB_SLOT_VIRTUAL] = virt4;

	pcibr_unlock(pcibr_soft, s);
    }
    return 0;
}

/*
 *    pcibr_rrb_flush: chase down all the RRBs assigned
 *      to the specified connection point, and flush
 *      them.
 */
void
pcibr_rrb_flush(devfs_handle_t pconn_vhdl)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
    pciio_slot_t            pciio_slot = pciio_info_slot_get(pciio_info);
    bridge_t               *bridge = pcibr_soft->bs_base;
    unsigned long           s;
    reg_p                   rrbp;
    unsigned                rrbm;
    int                     i;
    int                     rrbn;
    unsigned                sval;
    unsigned                mask;

    sval = BRIDGE_RRB_EN | (pciio_slot >> 1);
    mask = BRIDGE_RRB_EN | BRIDGE_RRB_PDEV;
    rrbn = pciio_slot & 1;
    rrbp = &bridge->b_rrb_map[rrbn].reg;

    s = pcibr_lock(pcibr_soft);
    rrbm = *rrbp;
    for (i = 0; i < 8; ++i) {
	if ((rrbm & mask) == sval)
	    do_pcibr_rrb_flush(bridge, rrbn);
	rrbm >>= 4;
	rrbn += 2;
    }
    pcibr_unlock(pcibr_soft, s);
}

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
LOCAL int
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
LOCAL void
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
 * pcibr_probe_slot: read a config space word
 * while trapping any errors; reutrn zero if
 * all went OK, or nonzero if there was an error.
 * The value read, if any, is passed back
 * through the valp parameter.
 */
LOCAL int
pcibr_probe_slot(bridge_t *bridge,
		 cfg_p cfg,
		 unsigned *valp)
{
    int                     rv;
    bridgereg_t             old_enable, new_enable;
    int badaddr_val(volatile void *, int, volatile void *);


    old_enable = bridge->b_int_enable;
    new_enable = old_enable & ~BRIDGE_IMR_PCI_MST_TIMEOUT;

    bridge->b_int_enable = new_enable;

	/*
	 * The xbridge doesn't clear b_err_int_view unless
	 * multi-err is cleared...
	 */
	if (is_xbridge(bridge))
	    if (bridge->b_err_int_view & BRIDGE_ISR_PCI_MST_TIMEOUT) {
		bridge->b_int_rst_stat = BRIDGE_IRR_MULTI_CLR;
	    }

    if (bridge->b_int_status & BRIDGE_IRR_PCI_GRP) {
	bridge->b_int_rst_stat = BRIDGE_IRR_PCI_GRP_CLR;
	(void) bridge->b_wid_tflush;	/* flushbus */
    }
    rv = badaddr_val((void *) cfg, 4, valp);

	/*
	 * The xbridge doesn't set master timeout in b_int_status
	 * here.  Fortunately it's in error_interrupt_view.
	 */
	if (is_xbridge(bridge))
	    if (bridge->b_err_int_view & BRIDGE_ISR_PCI_MST_TIMEOUT) {
		bridge->b_int_rst_stat = BRIDGE_IRR_MULTI_CLR;
		rv = 1;		/* unoccupied slot */
	    }

    bridge->b_int_enable = old_enable;
    bridge->b_wid_tflush;		/* wait until Bridge PIO complete */

    return rv;
}

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

/*==========================================================================
 *	BRIDGE PCI SLOT RELATED IOCTLs
 */
char *pci_space_name[] = {"NONE", 
			  "ROM",
			  "IO",
			  "",
			  "MEM",
			  "MEM32",
			  "MEM64",
			  "CFG",
			  "WIN0",
			  "WIN1",
			  "WIN2",
			  "WIN3",
			  "WIN4",
			  "WIN5",
			  "",
			  "BAD"};


#ifdef LATER

void
pcibr_slot_func_info_return(pcibr_info_h pcibr_infoh,
                            int func,
                            pcibr_slot_func_info_resp_t funcp)
{
    pcibr_info_t                 pcibr_info = pcibr_infoh[func];
    int                          win;

    funcp->resp_f_status = 0;

    if (!pcibr_info) {
        return;
    }

    funcp->resp_f_status |= FUNC_IS_VALID;
#ifdef SUPPORT_PRINTING_V_FORMAT
    sprintf(funcp->resp_f_slot_name, "%v", pcibr_info->f_vertex);
#else
    sprintf(funcp->resp_f_slot_name, "%x", pcibr_info->f_vertex);
#endif

    if(is_sys_critical_vertex(pcibr_info->f_vertex)) {
        funcp->resp_f_status |= FUNC_IS_SYS_CRITICAL;
    }

    funcp->resp_f_bus = pcibr_info->f_bus;
    funcp->resp_f_slot = pcibr_info->f_slot;
    funcp->resp_f_func = pcibr_info->f_func;
#ifdef SUPPORT_PRINTING_V_FORMAT
    sprintf(funcp->resp_f_master_name, "%v", pcibr_info->f_master);
#else
    sprintf(funcp->resp_f_master_name, "%x", pcibr_info->f_master);
#endif
    funcp->resp_f_pops = pcibr_info->f_pops;
    funcp->resp_f_efunc = pcibr_info->f_efunc;
    funcp->resp_f_einfo = pcibr_info->f_einfo;

    funcp->resp_f_vendor = pcibr_info->f_vendor;
    funcp->resp_f_device = pcibr_info->f_device;

    for(win = 0 ; win < 6 ; win++) {
        funcp->resp_f_window[win].resp_w_base =
                                  pcibr_info->f_window[win].w_base;
        funcp->resp_f_window[win].resp_w_size =
                                  pcibr_info->f_window[win].w_size;
        sprintf(funcp->resp_f_window[win].resp_w_space,
                "%s",
                pci_space_name[pcibr_info->f_window[win].w_space]);
    }

    funcp->resp_f_rbase = pcibr_info->f_rbase;
    funcp->resp_f_rsize = pcibr_info->f_rsize;

    for (win = 0 ; win < 4; win++) {
        funcp->resp_f_ibit[win] = pcibr_info->f_ibit[win];
    }

    funcp->resp_f_att_det_error = pcibr_info->f_att_det_error;

}

int
pcibr_slot_info_return(pcibr_soft_t             pcibr_soft,
                       pciio_slot_t             slot,
                       pcibr_slot_info_resp_t   respp)
{
    pcibr_soft_slot_t            pss;
    int                          func;
    bridge_t                    *bridge = pcibr_soft->bs_base;
    reg_p                        b_respp;
    pcibr_slot_info_resp_t       slotp;
    pcibr_slot_func_info_resp_t  funcp;

    slotp = snia_kmem_zalloc(sizeof(*slotp), KM_SLEEP);
    if (slotp == NULL) {
        return(ENOMEM);
    }

    pss = &pcibr_soft->bs_slot[slot];
    
    printk("\nPCI INFRASTRUCTURAL INFO FOR SLOT %d\n\n", slot);

    slotp->resp_has_host = pss->has_host;
    slotp->resp_host_slot = pss->host_slot;
#ifdef SUPPORT_PRINTING_V_FORMAT
    sprintf(slotp->resp_slot_conn_name, "%v", pss->slot_conn);
#else
    sprintf(slotp->resp_slot_conn_name, "%x", pss->slot_conn);
#endif
    slotp->resp_slot_status = pss->slot_status;
    slotp->resp_l1_bus_num = io_path_map_widget(pcibr_soft->bs_vhdl);

    if (is_sys_critical_vertex(pss->slot_conn)) {
        slotp->resp_slot_status |= SLOT_IS_SYS_CRITICAL;
    }

    slotp->resp_bss_ninfo = pss->bss_ninfo;

    for (func = 0; func < pss->bss_ninfo; func++) {
        funcp = &(slotp->resp_func[func]);
        pcibr_slot_func_info_return(pss->bss_infos, func, funcp);
    }

    sprintf(slotp->resp_bss_devio_bssd_space, "%s",
            pci_space_name[pss->bss_devio.bssd_space]);
    slotp->resp_bss_devio_bssd_base = pss->bss_devio.bssd_base;
    slotp->resp_bss_device = pss->bss_device;

    slotp->resp_bss_pmu_uctr = pss->bss_pmu_uctr;
    slotp->resp_bss_d32_uctr = pss->bss_d32_uctr;
    slotp->resp_bss_d64_uctr = pss->bss_d64_uctr;

    slotp->resp_bss_d64_base = pss->bss_d64_base;
    slotp->resp_bss_d64_flags = pss->bss_d64_flags;
    slotp->resp_bss_d32_base = pss->bss_d32_base;
    slotp->resp_bss_d32_flags = pss->bss_d32_flags;

    slotp->resp_bss_ext_ates_active = atomic_read(&pss->bss_ext_ates_active);

    slotp->resp_bss_cmd_pointer = pss->bss_cmd_pointer;
    slotp->resp_bss_cmd_shadow = pss->bss_cmd_shadow;

    slotp->resp_bs_rrb_valid = pcibr_soft->bs_rrb_valid[slot];
    slotp->resp_bs_rrb_valid_v = pcibr_soft->bs_rrb_valid[slot +
                                                      PCIBR_RRB_SLOT_VIRTUAL];
    slotp->resp_bs_rrb_res = pcibr_soft->bs_rrb_res[slot];

    if (slot & 1) {
        b_respp = &bridge->b_odd_resp;
    } else {
        b_respp = &bridge->b_even_resp;
    }

    slotp->resp_b_resp = *b_respp;

    slotp->resp_b_int_device = bridge->b_int_device;
    slotp->resp_b_int_enable = bridge->b_int_enable;
    slotp->resp_b_int_host = bridge->b_int_addr[slot].addr;

    if (COPYOUT(slotp, respp, sizeof(*respp))) {
        return(EFAULT);
    }

    snia_kmem_free(slotp, sizeof(*slotp));

    return(0);
}

/*
 * pcibr_slot_query
 *	Return information about the PCI slot maintained by the infrastructure.
 *	Information is requested in the request structure.
 *
 *      Information returned in the response structure:
 *		Slot hwgraph name
 *		Vendor/Device info
 *		Base register info
 *		Interrupt mapping from device pins to the bridge pins
 *		Devio register
 *		Software RRB info
 *		RRB register info
 *		Host/Gues info
 *		PCI Bus #,slot #, function #
 *		Slot provider hwgraph name
 *		Provider Functions
 *		Error handler
 *		DMA mapping usage counters
 *		DMA direct translation info
 *		External SSRAM workaround info
 */
int
pcibr_slot_query(devfs_handle_t pcibr_vhdl, pcibr_slot_info_req_t reqp)
{
    pcibr_soft_t            pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    pciio_slot_t            slot = reqp->req_slot;
    pciio_slot_t            tmp_slot;
    pcibr_slot_info_resp_t  respp = (pcibr_slot_info_resp_t) reqp->req_respp;
    int                     size = reqp->req_size;
    int                     error;

    /* Make sure that we are dealing with a bridge device vertex */
    if (!pcibr_soft) {
        return(EINVAL);
    }

    /* Make sure that we have a valid PCI slot number or PCIIO_SLOT_NONE */
    if ((!PCIBR_VALID_SLOT(slot)) && (slot != PCIIO_SLOT_NONE)) {
        return(EINVAL);
    }

    /* Return information for the requested PCI slot */
    if (slot != PCIIO_SLOT_NONE) {
        if (size < sizeof(*respp)) {
            return(EINVAL);
        }

        /* Acquire read access to the slot */
        mrlock(pcibr_soft->bs_slot[slot].slot_lock, MR_ACCESS, PZERO);

        error = pcibr_slot_info_return(pcibr_soft, slot, respp);

        /* Release the slot lock */
        mrunlock(pcibr_soft->bs_slot[slot].slot_lock);

        return(error);
    }

    /* Return information for all the slots */
    for (tmp_slot = 0; tmp_slot < 8; tmp_slot++) {

        if (size < sizeof(*respp)) {
            return(EINVAL);
        }

        /* Acquire read access to the slot */
        mrlock(pcibr_soft->bs_slot[tmp_slot].slot_lock, MR_ACCESS, PZERO);

        error = pcibr_slot_info_return(pcibr_soft, tmp_slot, respp);

        /* Release the slot lock */
        mrunlock(pcibr_soft->bs_slot[tmp_slot].slot_lock);

        if (error) {
            return(error);
        }

        ++respp;
        size -= sizeof(*respp);
    }

    return(error);
}
#endif	/* LATER */


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

    case PCIBR_SLOT_POWERUP:
	{
	    pciio_slot_t	slot;

	    if (!cap_able(CAP_DEVICE_MGT)) {
		error = EPERM;
		break;
	    }

	    slot = (pciio_slot_t)(uint64_t)arg;
	    error = pcibr_slot_powerup(pcibr_vhdl,slot);
	    break;
	}
    case PCIBR_SLOT_SHUTDOWN:
	    if (!cap_able(CAP_DEVICE_MGT)) {
		error = EPERM;
		break;
	    }

	    slot = (pciio_slot_t)(uint64_t)arg;
	    error = pcibr_slot_powerup(pcibr_vhdl,slot);
	    break;
	}
    case PCIBR_SLOT_QUERY:
	{
	    struct pcibr_slot_info_req_s        req;

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

/* Convert from ssram_bits in control register to number of SSRAM entries */
#define ATE_NUM_ENTRIES(n) _ate_info[n]

/* Possible choices for number of ATE entries in Bridge's SSRAM */
LOCAL int               _ate_info[] =
{
    0,					/* 0 entries */
    8 * 1024,				/* 8K entries */
    16 * 1024,				/* 16K entries */
    64 * 1024				/* 64K entries */
};

#define ATE_NUM_SIZES (sizeof(_ate_info) / sizeof(int))
#define ATE_PROBE_VALUE 0x0123456789abcdefULL

/*
 * Determine the size of this bridge's external mapping SSRAM, and set
 * the control register appropriately to reflect this size, and initialize
 * the external SSRAM.
 */
LOCAL int
pcibr_init_ext_ate_ram(bridge_t *bridge)
{
    int                     largest_working_size = 0;
    int                     num_entries, entry;
    int                     i, j;
    bridgereg_t             old_enable, new_enable;
    int                     s;

    /* Probe SSRAM to determine its size. */
    old_enable = bridge->b_int_enable;
    new_enable = old_enable & ~BRIDGE_IMR_PCI_MST_TIMEOUT;
    bridge->b_int_enable = new_enable;

    for (i = 1; i < ATE_NUM_SIZES; i++) {
	/* Try writing a value */
	bridge->b_ext_ate_ram[ATE_NUM_ENTRIES(i) - 1] = ATE_PROBE_VALUE;

	/* Guard against wrap */
	for (j = 1; j < i; j++)
	    bridge->b_ext_ate_ram[ATE_NUM_ENTRIES(j) - 1] = 0;

	/* See if value was written */
	if (bridge->b_ext_ate_ram[ATE_NUM_ENTRIES(i) - 1] == ATE_PROBE_VALUE)
	    largest_working_size = i;
    }
    bridge->b_int_enable = old_enable;
    bridge->b_wid_tflush;		/* wait until Bridge PIO complete */

    /*
     * ensure that we write and read without any interruption.
     * The read following the write is required for the Bridge war
     */

    s = splhi();
    bridge->b_wid_control = (bridge->b_wid_control
	& ~BRIDGE_CTRL_SSRAM_SIZE_MASK)
	| BRIDGE_CTRL_SSRAM_SIZE(largest_working_size);
    bridge->b_wid_control;		/* inval addr bug war */
    splx(s);

    num_entries = ATE_NUM_ENTRIES(largest_working_size);

#if PCIBR_ATE_DEBUG
    if (num_entries)
	printk("bridge at 0x%x: clearing %d external ATEs\n", bridge, num_entries);
    else
	printk("bridge at 0x%x: no externa9422l ATE RAM found\n", bridge);
#endif

    /* Initialize external mapping entries */
    for (entry = 0; entry < num_entries; entry++)
	bridge->b_ext_ate_ram[entry] = 0;

    return (num_entries);
}

/*
 * Allocate "count" contiguous Bridge Address Translation Entries
 * on the specified bridge to be used for PCI to XTALK mappings.
 * Indices in rm map range from 1..num_entries.  Indicies returned
 * to caller range from 0..num_entries-1.
 *
 * Return the start index on success, -1 on failure.
 */
LOCAL int
pcibr_ate_alloc(pcibr_soft_t pcibr_soft, int count)
{
    int                     index = 0;

    index = (int) rmalloc(pcibr_soft->bs_int_ate_map, (size_t) count);
/* printk("Colin: pcibr_ate_alloc - index %d count %d \n", index, count); */

    if (!index && pcibr_soft->bs_ext_ate_map)
	index = (int) rmalloc(pcibr_soft->bs_ext_ate_map, (size_t) count);

    /* rmalloc manages resources in the 1..n
     * range, with 0 being failure.
     * pcibr_ate_alloc manages resources
     * in the 0..n-1 range, with -1 being failure.
     */
    return index - 1;
}

LOCAL void
pcibr_ate_free(pcibr_soft_t pcibr_soft, int index, int count)
/* Who says there's no such thing as a free meal? :-) */
{
    /* note the "+1" since rmalloc handles 1..n but
     * we start counting ATEs at zero.
     */
/* printk("Colin: pcibr_ate_free - index %d count %d\n", index, count); */

    rmfree((index < pcibr_soft->bs_int_ate_size)
	   ? pcibr_soft->bs_int_ate_map
	   : pcibr_soft->bs_ext_ate_map,
	   count, index + 1);
}

LOCAL pcibr_info_t
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

void
pcibr_device_info_free(devfs_handle_t pcibr_vhdl, pciio_slot_t slot)
{
    pcibr_soft_t	pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    pcibr_info_t	pcibr_info;
    pciio_function_t	func;
    pcibr_soft_slot_t	slotp = &pcibr_soft->bs_slot[slot];
    int			nfunc = slotp->bss_ninfo;


    for (func = 0; func < nfunc; func++) {
	pcibr_info = slotp->bss_infos[func];

	if (!pcibr_info) 
	    continue;

	slotp->bss_infos[func] = 0;
	pciio_device_info_unregister(pcibr_vhdl, &pcibr_info->f_c);
	pciio_device_info_free(&pcibr_info->f_c);
	DEL(pcibr_info);
    }

    /* Clear the DEVIO(x) for this slot */
    slotp->bss_devio.bssd_space = PCIIO_SPACE_NONE;
    slotp->bss_devio.bssd_base = PCIBR_D32_BASE_UNSET;
    slotp->bss_device  = 0;

    
    /* Reset the mapping usage counters */
    slotp->bss_pmu_uctr = 0;
    slotp->bss_d32_uctr = 0;
    slotp->bss_d64_uctr = 0;

    /* Clear the Direct translation info */
    slotp->bss_d64_base = PCIBR_D64_BASE_UNSET;
    slotp->bss_d64_flags = 0;
    slotp->bss_d32_base = PCIBR_D32_BASE_UNSET;
    slotp->bss_d32_flags = 0;

    /* Clear out shadow info necessary for the external SSRAM workaround */
    slotp->bss_ext_ates_active = ATOMIC_INIT(0);
    slotp->bss_cmd_pointer = 0;
    slotp->bss_cmd_shadow = 0;

}

/* 
 * PCI_ADDR_SPACE_LIMITS_LOAD
 *	Gets the current values of 
 *		pci io base, 
 *		pci io last,
 *		pci low memory base,
 *		pci low memory last,
 *		pci high memory base,
 * 		pci high memory last
 */
#define PCI_ADDR_SPACE_LIMITS_LOAD()			\
    pci_io_fb = pcibr_soft->bs_spinfo.pci_io_base;	\
    pci_io_fl = pcibr_soft->bs_spinfo.pci_io_last;	\
    pci_lo_fb = pcibr_soft->bs_spinfo.pci_swin_base;	\
    pci_lo_fl = pcibr_soft->bs_spinfo.pci_swin_last;	\
    pci_hi_fb = pcibr_soft->bs_spinfo.pci_mem_base;	\
    pci_hi_fl = pcibr_soft->bs_spinfo.pci_mem_last;
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

#define PCI_ADDR_SPACE_LIMITS_PRINT()			\
    printf("+++++++++++++++++++++++\n"			\
	   "IO base 0x%x last 0x%x\n"			\
	   "SWIN base 0x%x last 0x%x\n"			\
	   "MEM base 0x%x last 0x%x\n"			\
	   "+++++++++++++++++++++++\n",			\
	   pcibr_soft->bs_spinfo.pci_io_base,		\
	   pcibr_soft->bs_spinfo.pci_io_last,		\
	   pcibr_soft->bs_spinfo.pci_swin_base,		\
	   pcibr_soft->bs_spinfo.pci_swin_last,		\
	   pcibr_soft->bs_spinfo.pci_mem_base,		\
	   pcibr_soft->bs_spinfo.pci_mem_last);

/*
 * pcibr_slot_info_init
 *	Probe for this slot and see if it is populated.
 *	If it is populated initialize the generic PCI infrastructural
 * 	information associated with this particular PCI device.
 */
int
pcibr_slot_info_init(devfs_handle_t 	pcibr_vhdl,
		     pciio_slot_t 	slot)
{
    pcibr_soft_t	    pcibr_soft;
    pcibr_info_h	    pcibr_infoh;
    pcibr_info_t	    pcibr_info;
    bridge_t		   *bridge;
    cfg_p                   cfgw;
    unsigned                idword;
    unsigned                pfail;
    unsigned                idwords[8];
    pciio_vendor_id_t       vendor;
    pciio_device_id_t       device;
    unsigned                htype;
    cfg_p                   wptr;
    int                     win;
    pciio_space_t           space;
    iopaddr_t		    pci_io_fb,	pci_io_fl;
    iopaddr_t		    pci_lo_fb,  pci_lo_fl;
    iopaddr_t		    pci_hi_fb,  pci_hi_fl;
    int			    nfunc;
    pciio_function_t	    rfunc;
    int			    func;
    devfs_handle_t	    conn_vhdl;
    pcibr_soft_slot_t	    slotp;
    
    /* Get the basic software information required to proceed */
    pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    if (!pcibr_soft)
	return(EINVAL);

    bridge = pcibr_soft->bs_base;
    if (!PCIBR_VALID_SLOT(slot))
	return(EINVAL);

    /* If we have a host slot (eg:- IOC3 has 2 PCI slots and the initialization
     * is done by the host slot then we are done.
     */
    if (pcibr_soft->bs_slot[slot].has_host) {
	return(0);    
    }

    /* Check for a slot with any system critical functions */
    if (pcibr_is_slot_sys_critical(pcibr_vhdl, slot))
        return(EPERM);

    /* Load the current values of allocated PCI address spaces */
    PCI_ADDR_SPACE_LIMITS_LOAD();
    
    /* Try to read the device-id/vendor-id from the config space */
    cfgw = bridge->b_type0_cfg_dev[slot].l;

    if (pcibr_probe_slot(bridge, cfgw, &idword)) 
	return(ENODEV);

    slotp = &pcibr_soft->bs_slot[slot];
    slotp->slot_status |= SLOT_POWER_UP;

    vendor = 0xFFFF & idword;
    /* If the vendor id is not valid then the slot is not populated
     * and we are done.
     */
    if (vendor == 0xFFFF) 
	return(ENODEV);			
    
    device = 0xFFFF & (idword >> 16);
    htype = do_pcibr_config_get(cfgw, PCI_CFG_HEADER_TYPE, 1);

    nfunc = 1;
    rfunc = PCIIO_FUNC_NONE;
    pfail = 0;

    /* NOTE: if a card claims to be multifunction
     * but only responds to config space 0, treat
     * it as a unifunction card.
     */

    if (htype & 0x80) {		/* MULTIFUNCTION */
	for (func = 1; func < 8; ++func) {
	    cfgw = bridge->b_type0_cfg_dev[slot].f[func].l;
	    if (pcibr_probe_slot(bridge, cfgw, &idwords[func])) {
		pfail |= 1 << func;
		continue;
	    }
	    vendor = 0xFFFF & idwords[func];
	    if (vendor == 0xFFFF) {
		pfail |= 1 << func;
		continue;
	    }
	    nfunc = func + 1;
	    rfunc = 0;
	}
	cfgw = bridge->b_type0_cfg_dev[slot].l;
    }
    NEWA(pcibr_infoh, nfunc);
    
    pcibr_soft->bs_slot[slot].bss_ninfo = nfunc;
    pcibr_soft->bs_slot[slot].bss_infos = pcibr_infoh;

    for (func = 0; func < nfunc; ++func) {
	unsigned                cmd_reg;
	
	if (func) {
	    if (pfail & (1 << func))
		continue;
	    
	    idword = idwords[func];
	    cfgw = bridge->b_type0_cfg_dev[slot].f[func].l;
	    
	    device = 0xFFFF & (idword >> 16);
	    htype = do_pcibr_config_get(cfgw, PCI_CFG_HEADER_TYPE, 1);
	    rfunc = func;
	}
	htype &= 0x7f;
	if (htype != 0x00) {
	    printk(KERN_WARNING  "%s pcibr: pci slot %d func %d has strange header type 0x%x\n",
		    pcibr_soft->bs_name, slot, func, htype);
	    continue;
	}
#if DEBUG && ATTACH_DEBUG
	printk(KERN_NOTICE   
		"%s pcibr: pci slot %d func %d: vendor 0x%x device 0x%x",
		pcibr_soft->bs_name, slot, func, vendor, device);
#endif	

	pcibr_info = pcibr_device_info_new
	    (pcibr_soft, slot, rfunc, vendor, device);
	conn_vhdl = pciio_device_info_register(pcibr_vhdl, &pcibr_info->f_c);
	if (func == 0)
	    slotp->slot_conn = conn_vhdl;

#ifdef LITTLE_ENDIAN
	cmd_reg = cfgw[(PCI_CFG_COMMAND ^ 4) / 4];
#else
	cmd_reg = cfgw[PCI_CFG_COMMAND / 4];
#endif
	
	wptr = cfgw + PCI_CFG_BASE_ADDR_0 / 4;

	for (win = 0; win < PCI_CFG_BASE_ADDRS; ++win) {
	    iopaddr_t               base, mask, code;
	    size_t                  size;

	    /*
	     * GET THE BASE & SIZE OF THIS WINDOW:
	     *
	     * The low two or four bits of the BASE register
	     * determines which address space we are in; the
	     * rest is a base address. BASE registers
	     * determine windows that are power-of-two sized
	     * and naturally aligned, so we can get the size
	     * of a window by writing all-ones to the
	     * register, reading it back, and seeing which
	     * bits are used for decode; the least
	     * significant nonzero bit is also the size of
	     * the window.
	     *
	     * WARNING: someone may already have allocated
	     * some PCI space to this window, and in fact
	     * PIO may be in process at this very moment
	     * from another processor (or even from this
	     * one, if we get interrupted)! So, if the BASE
	     * already has a nonzero address, be generous
	     * and use the LSBit of that address as the
	     * size; this could overstate the window size.
	     * Usually, when one card is set up, all are set
	     * up; so, since we don't bitch about
	     * overlapping windows, we are ok.
	     *
	     * UNFORTUNATELY, some cards do not clear their
	     * BASE registers on reset. I have two heuristics
	     * that can detect such cards: first, if the
	     * decode enable is turned off for the space
	     * that the window uses, we can disregard the
	     * initial value. second, if the address is
	     * outside the range that we use, we can disregard
	     * it as well.
	     *
	     * This is looking very PCI generic. Except for
	     * knowing how many slots and where their config
	     * spaces are, this window loop and the next one
	     * could probably be shared with other PCI host
	     * adapters. It would be interesting to see if
	     * this could be pushed up into pciio, when we
	     * start supporting more PCI providers.
	     */
#ifdef LITTLE_ENDIAN
	    base = wptr[((win*4)^4)/4];
#else
	    base = wptr[win];
#endif

	    if (base & PCI_BA_IO_SPACE) {
		/* BASE is in I/O space. */
		space = PCIIO_SPACE_IO;
		mask = -4;
		code = base & 3;
		base = base & mask;
		if (base == 0) {
		    ;		/* not assigned */
		} else if (!(cmd_reg & PCI_CMD_IO_SPACE)) {
		    base = 0;	/* decode not enabled */
		}
	    } else {
		/* BASE is in MEM space. */
		space = PCIIO_SPACE_MEM;
		mask = -16;
		code = base & PCI_BA_MEM_LOCATION;	/* extract BAR type */
		base = base & mask;
		if (base == 0) {
		    ;		/* not assigned */
		} else if (!(cmd_reg & PCI_CMD_MEM_SPACE)) {
		    base = 0;	/* decode not enabled */
		} else if (base & 0xC0000000) {
		    base = 0;	/* outside permissable range */
		} else if ((code == PCI_BA_MEM_64BIT) &&
#ifdef LITTLE_ENDIAN
			   (wptr[(((win + 1)*4)^4)/4] != 0)) {
#else 
			   (wptr[win + 1] != 0)) {
#endif /* LITTLE_ENDIAN */
		    base = 0;	/* outside permissable range */
		}
	    }

	    if (base != 0) {	/* estimate size */
		size = base & -base;
	    } else {		/* calculate size */
#ifdef LITTLE_ENDIAN
		wptr[((win*4)^4)/4] = ~0;	/* turn on all bits */
		size = wptr[((win*4)^4)/4];	/* get stored bits */
#else 
		wptr[win] = ~0;	/* turn on all bits */
		size = wptr[win];	/* get stored bits */
#endif /* LITTLE_ENDIAN */
		size &= mask;	/* keep addr */
		size &= -size;	/* keep lsbit */
		if (size == 0)
		    continue;
	    }	

	    pcibr_info->f_window[win].w_space = space;
	    pcibr_info->f_window[win].w_base = base;
	    pcibr_info->f_window[win].w_size = size;

	    /*
	     * If this window already has PCI space
	     * allocated for it, "subtract" that space from
	     * our running freeblocks. Don't worry about
	     * overlaps in existing allocated windows; we
	     * may be overstating their sizes anyway.
	     */

	    if (base && size) {
		if (space == PCIIO_SPACE_IO) {
		    pcibr_freeblock_sub(&pci_io_fb,
					&pci_io_fl,
					base, size);
		} else {
		    pcibr_freeblock_sub(&pci_lo_fb,
					&pci_lo_fl,
					base, size);
		    pcibr_freeblock_sub(&pci_hi_fb,
					&pci_hi_fl,
					base, size);
		}	
	    }
#if defined(IOC3_VENDOR_ID_NUM) && defined(IOC3_DEVICE_ID_NUM)
	    /*
	     * IOC3 BASE_ADDR* BUG WORKAROUND
	     *
	     
	     * If we write to BASE1 on the IOC3, the
	     * data in BASE0 is replaced. The
	     * original workaround was to remember
	     * the value of BASE0 and restore it
	     * when we ran off the end of the BASE
	     * registers; however, a later
	     * workaround was added (I think it was
	     * rev 1.44) to avoid setting up
	     * anything but BASE0, with the comment
	     * that writing all ones to BASE1 set
	     * the enable-parity-error test feature
	     * in IOC3's SCR bit 14.
	     *
	     * So, unless we defer doing any PCI
	     * space allocation until drivers
	     * attach, and set up a way for drivers
	     * (the IOC3 in paricular) to tell us
	     * generically to keep our hands off
	     * BASE registers, we gotta "know" about
	     * the IOC3 here.
	     *
	     * Too bad the PCI folks didn't reserve the
	     * all-zero value for 'no BASE here' (it is a
	     * valid code for an uninitialized BASE in
	     * 32-bit PCI memory space).
	     */
	    
	    if ((vendor == IOC3_VENDOR_ID_NUM) &&
		(device == IOC3_DEVICE_ID_NUM))
		break;
#endif
	    if (code == PCI_BA_MEM_64BIT) {
		win++;		/* skip upper half */
#ifdef LITTLE_ENDIAN
		wptr[((win*4)^4)/4] = 0;	/* which must be zero */
#else 
		wptr[win] = 0;	/* which must be zero */
#endif /* LITTLE_ENDIAN */
	    }
	}				/* next win */
    }				/* next func */

    /* Store back the values for allocated PCI address spaces */
    PCI_ADDR_SPACE_LIMITS_STORE();
    return(0);
}					

/*
 * pcibr_slot_info_free
 *	Remove all the PCI infrastructural information associated
 * 	with a particular PCI device.
 */
int
pcibr_slot_info_free(devfs_handle_t pcibr_vhdl,
                     pciio_slot_t slot)
{
    pcibr_soft_t	pcibr_soft;
    pcibr_info_h	pcibr_infoh;
    int			nfunc;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    if (!pcibr_soft || !PCIBR_VALID_SLOT(slot))
	return(EINVAL);

    nfunc = pcibr_soft->bs_slot[slot].bss_ninfo;

    pcibr_device_info_free(pcibr_vhdl, slot);

    pcibr_infoh = pcibr_soft->bs_slot[slot].bss_infos;
    DELA(pcibr_infoh,nfunc);
    pcibr_soft->bs_slot[slot].bss_ninfo = 0;

    return(0);
}

int as_debug = 0;
/*
 * pcibr_slot_addr_space_init
 *	Reserve chunks of PCI address space as required by 
 * 	the base registers in the card.
 */
int
pcibr_slot_addr_space_init(devfs_handle_t pcibr_vhdl,
			   pciio_slot_t	slot)
{
    pcibr_soft_t	pcibr_soft;
    pcibr_info_h	pcibr_infoh;
    pcibr_info_t	pcibr_info;
    bridge_t		*bridge;
    iopaddr_t		pci_io_fb, pci_io_fl;
    iopaddr_t		pci_lo_fb, pci_lo_fl;
    iopaddr_t		pci_hi_fb, pci_hi_fl;
    size_t              align;
    iopaddr_t           mask;
    int		    	nbars;
    int		       	nfunc;
    int			func;
    int			win;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    if (!pcibr_soft || !PCIBR_VALID_SLOT(slot))
	return(EINVAL);

    bridge = pcibr_soft->bs_base;

    /* Get the current values for the allocated PCI address spaces */
    PCI_ADDR_SPACE_LIMITS_LOAD();

    if (as_debug)
#ifdef LATER
    PCI_ADDR_SPACE_LIMITS_PRINT();
#endif
    /* allocate address space,
     * for windows that have not been
     * previously assigned.
     */
    if (pcibr_soft->bs_slot[slot].has_host) {
	return(0);
    }

    nfunc = pcibr_soft->bs_slot[slot].bss_ninfo;
    if (nfunc < 1)
	return(EINVAL);

    pcibr_infoh = pcibr_soft->bs_slot[slot].bss_infos;
    if (!pcibr_infoh)
	return(EINVAL);

    /*
     * Try to make the DevIO windows not
     * overlap by pushing the "io" and "hi"
     * allocation areas up to the next one
     * or two megabyte bound. This also
     * keeps them from being zero.
     *
     * DO NOT do this with "pci_lo" since
     * the entire "lo" area is only a
     * megabyte, total ...
     */
    align = (slot < 2) ? 0x200000 : 0x100000;
    mask = -align;
    pci_io_fb = (pci_io_fb + align - 1) & mask;
    pci_hi_fb = (pci_hi_fb + align - 1) & mask;

    for (func = 0; func < nfunc; ++func) {
	cfg_p                   cfgw;
	cfg_p                   wptr;
	pciio_space_t           space;
	iopaddr_t               base;
	size_t                  size;
	cfg_p                   pci_cfg_cmd_reg_p;
	unsigned                pci_cfg_cmd_reg;
	unsigned                pci_cfg_cmd_reg_add = 0;

	pcibr_info = pcibr_infoh[func];

	if (!pcibr_info)
	    continue;

	if (pcibr_info->f_vendor == PCIIO_VENDOR_ID_NONE)
	    continue;
	
	cfgw = bridge->b_type0_cfg_dev[slot].f[func].l;
	wptr = cfgw + PCI_CFG_BASE_ADDR_0 / 4;

	nbars = PCI_CFG_BASE_ADDRS;

	for (win = 0; win < nbars; ++win) {

	    space = pcibr_info->f_window[win].w_space;
	    base = pcibr_info->f_window[win].w_base;
	    size = pcibr_info->f_window[win].w_size;
	    
	    if (size < 1)
		continue;

	    if (base >= size) {
#if DEBUG && PCI_DEBUG
		printk("pcibr: slot %d func %d window %d is in %d[0x%x..0x%x], alloc by prom\n",
			slot, func, win, space, base, base + size - 1);
#endif
		continue;		/* already allocated */
	    }
	    align = size;		/* ie. 0x00001000 */
	    if (align < _PAGESZ)
		align = _PAGESZ;	/* ie. 0x00004000 */
	    mask = -align;		/* ie. 0xFFFFC000 */

	    switch (space) {
	    case PCIIO_SPACE_IO:
		base = (pci_io_fb + align - 1) & mask;
		if ((base + size) > pci_io_fl) {
		    base = 0;
		    break;
		}
		pci_io_fb = base + size;
		break;
		
	    case PCIIO_SPACE_MEM:
#ifdef LITTLE_ENDIAN
		if ((wptr[((win*4)^4)/4] & PCI_BA_MEM_LOCATION) ==
#else
		if ((wptr[win] & PCI_BA_MEM_LOCATION) ==
#endif  /* LITTLE_ENDIAN */
		    PCI_BA_MEM_1MEG) {
		    /* allocate from 20-bit PCI space */
		    base = (pci_lo_fb + align - 1) & mask;
		    if ((base + size) > pci_lo_fl) {
			base = 0;
			break;
		    }
		    pci_lo_fb = base + size;
		} else {
		    /* allocate from 32-bit or 64-bit PCI space */
		    base = (pci_hi_fb + align - 1) & mask;
		    if ((base + size) > pci_hi_fl) {
			base = 0;
			break;
		    }
		    pci_hi_fb = base + size;
		}
		break;
		
	    default:
		base = 0;
#if DEBUG && PCI_DEBUG
		printk("pcibr: slot %d window %d had bad space code %d\n",
			slot, win, space);
#endif
	    }
	    pcibr_info->f_window[win].w_base = base;
#ifdef LITTLE_ENDIAN
	    wptr[((win*4)^4)/4] = base;
#if DEBUG && PCI_DEBUG
		printk("Setting base address 0x%p base 0x%x\n", &(wptr[((win*4)^4)/4]), base);
#endif
#else
	    wptr[win] = base;
#endif  /* LITTLE_ENDIAN */

#if DEBUG && PCI_DEBUG
	    if (base >= size)
		printk("pcibr: slot %d func %d window %d is in %d [0x%x..0x%x], alloc by pcibr\n",
			slot, func, win, space, base, base + size - 1);
	    else
		printk("pcibr: slot %d func %d window %d, unable to alloc 0x%x in 0x%p\n",
			slot, func, win, size, space);
#endif
	}				/* next base */

	/*
	 * Allocate space for the EXPANSION ROM
	 * NOTE: DO NOT DO THIS ON AN IOC3,
	 * as it blows the system away.
	 */
	base = size = 0;
	if ((pcibr_soft->bs_slot[slot].bss_vendor_id != IOC3_VENDOR_ID_NUM) ||
	    (pcibr_soft->bs_slot[slot].bss_device_id != IOC3_DEVICE_ID_NUM)) {

	    wptr = cfgw + PCI_EXPANSION_ROM / 4;
#ifdef LITTLE_ENDIAN
	    wptr[1] = 0xFFFFF000;
	    mask = wptr[1];
#else
	    *wptr = 0xFFFFF000;
	    mask = *wptr;
#endif  /* LITTLE_ENDIAN */
	    if (mask & 0xFFFFF000) {
		size = mask & -mask;
		align = size;
		if (align < _PAGESZ)
		    align = _PAGESZ;
		mask = -align;
		base = (pci_hi_fb + align - 1) & mask;
		if ((base + size) > pci_hi_fl)
		    base = size = 0;
		else {
		    pci_hi_fb = base + size;
#ifdef LITTLE_ENDIAN
		    wptr[1] = base;
#else
		    *wptr = base;
#endif  /* LITTLE_ENDIAN */
#if DEBUG && PCI_DEBUG
		    printk("%s/%d ROM in 0x%lx..0x%lx (alloc by pcibr)\n",
			    pcibr_soft->bs_name, slot,
			    base, base + size - 1);
#endif
		}
	    }
	}
	pcibr_info->f_rbase = base;
	pcibr_info->f_rsize = size;
	
	/*
	 * if necessary, update the board's
	 * command register to enable decoding
	 * in the windows we added.
	 *
	 * There are some bits we always want to
	 * be sure are set.
	 */
	pci_cfg_cmd_reg_add |= PCI_CMD_IO_SPACE;

	/*
	 * The Adaptec 1160 FC Controller WAR #767995:
	 * The part incorrectly ignores the upper 32 bits of a 64 bit
	 * address when decoding references to it's registers so to
	 * keep it from responding to a bus cycle that it shouldn't
	 * we only use I/O space to get at it's registers.  Don't
	 * enable memory space accesses on that PCI device.
	 */
	#define FCADP_VENDID 0x9004 /* Adaptec Vendor ID from fcadp.h */
	#define FCADP_DEVID 0x1160  /* Adaptec 1160 Device ID from fcadp.h */

	if ((pcibr_info->f_vendor != FCADP_VENDID) ||
	    (pcibr_info->f_device != FCADP_DEVID))
	    pci_cfg_cmd_reg_add |= PCI_CMD_MEM_SPACE;

	pci_cfg_cmd_reg_add |= PCI_CMD_BUS_MASTER;

	pci_cfg_cmd_reg_p = cfgw + PCI_CFG_COMMAND / 4;
	pci_cfg_cmd_reg = *pci_cfg_cmd_reg_p;
#if PCI_FBBE	/* XXX- check here to see if dev can do fast-back-to-back */
	if (!((pci_cfg_cmd_reg >> 16) & PCI_STAT_F_BK_BK_CAP))
	    fast_back_to_back_enable = 0;
#endif
	pci_cfg_cmd_reg &= 0xFFFF;
	if (pci_cfg_cmd_reg_add & ~pci_cfg_cmd_reg)
	    *pci_cfg_cmd_reg_p = pci_cfg_cmd_reg | pci_cfg_cmd_reg_add;
	
    }				/* next func */

    /* Now that we have allocated new chunks of PCI address spaces to this
     * card we need to update the bookkeeping values which indicate
     * the current PCI address space allocations.
     */
    PCI_ADDR_SPACE_LIMITS_STORE();
    return(0);
}

/*
 * pcibr_slot_device_init
 * 	Setup the device register in the bridge for this PCI slot.
 */
int
pcibr_slot_device_init(devfs_handle_t pcibr_vhdl,
		       pciio_slot_t slot)
{
    pcibr_soft_t	 pcibr_soft;
    bridge_t		*bridge;
    bridgereg_t		 devreg;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    if (!pcibr_soft || !PCIBR_VALID_SLOT(slot))
	return(EINVAL);

    bridge = pcibr_soft->bs_base;

    /*
     * Adjustments to Device(x)
     * and init of bss_device shadow
     */
    devreg = bridge->b_device[slot].reg;
    devreg &= ~BRIDGE_DEV_PAGE_CHK_DIS;
    devreg |= BRIDGE_DEV_COH | BRIDGE_DEV_VIRTUAL_EN;
#ifdef LITTLE_ENDIAN
    devreg |= BRIDGE_DEV_DEV_SWAP;
#endif
    pcibr_soft->bs_slot[slot].bss_device = devreg;
    bridge->b_device[slot].reg = devreg;

#if DEBUG && PCI_DEBUG
	printk("pcibr Device(%d): 0x%lx\n", slot, bridge->b_device[slot].reg);
#endif

#if DEBUG && PCI_DEBUG
    printk("pcibr: PCI space allocation done.\n");
#endif

    return(0);
}

/*
 * pcibr_slot_guest_info_init
 *	Setup the host/guest relations for a PCI slot.
 */
int
pcibr_slot_guest_info_init(devfs_handle_t pcibr_vhdl,
			   pciio_slot_t	slot)
{
    pcibr_soft_t	pcibr_soft;
    pcibr_info_h	pcibr_infoh;
    pcibr_info_t	pcibr_info;
    pcibr_soft_slot_t	slotp;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    if (!pcibr_soft || !PCIBR_VALID_SLOT(slot))
	return(EINVAL);

    slotp = &pcibr_soft->bs_slot[slot];

    /* create info and verticies for guest slots;
     * for compatibilitiy macros, create info
     * for even unpopulated slots (but do not
     * build verticies for them).
     */
    if (pcibr_soft->bs_slot[slot].bss_ninfo < 1) {
	NEWA(pcibr_infoh, 1);
	pcibr_soft->bs_slot[slot].bss_ninfo = 1;
	pcibr_soft->bs_slot[slot].bss_infos = pcibr_infoh;

	pcibr_info = pcibr_device_info_new
	    (pcibr_soft, slot, PCIIO_FUNC_NONE,
	     PCIIO_VENDOR_ID_NONE, PCIIO_DEVICE_ID_NONE);

	if (pcibr_soft->bs_slot[slot].has_host) {
	    slotp->slot_conn = pciio_device_info_register
		(pcibr_vhdl, &pcibr_info->f_c);
	}
    }

    /* generate host/guest relations
     */
    if (pcibr_soft->bs_slot[slot].has_host) {
	int  host = pcibr_soft->bs_slot[slot].host_slot;
	pcibr_soft_slot_t host_slotp = &pcibr_soft->bs_slot[host];

	hwgraph_edge_add(slotp->slot_conn,
			 host_slotp->slot_conn,
			 EDGE_LBL_HOST);

	/* XXX- only gives us one guest edge per
	 * host. If/when we have a host with more than
	 * one guest, we will need to figure out how
	 * the host finds all its guests, and sorts
	 * out which one is which.
	 */
	hwgraph_edge_add(host_slotp->slot_conn,
			 slotp->slot_conn,
			 EDGE_LBL_GUEST);
    }

    return(0);
}

/*
 * pcibr_slot_initial_rrb_alloc
 *	Allocate a default number of rrbs for this slot on 
 * 	the two channels.  This is dictated by the rrb allocation
 * 	strategy routine defined per platform.
 */

int
pcibr_slot_initial_rrb_alloc(devfs_handle_t pcibr_vhdl,
			     pciio_slot_t slot)
{
    pcibr_soft_t	pcibr_soft;
    pcibr_info_h	pcibr_infoh;
    pcibr_info_t	pcibr_info;
    bridge_t		*bridge;
    int                 c0, c1;
    int			r;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    if (!pcibr_soft || !PCIBR_VALID_SLOT(slot))
	return(EINVAL);

    bridge = pcibr_soft->bs_base;

    /* How may RRBs are on this slot?
     */
    c0 = do_pcibr_rrb_count_valid(bridge, slot);
    c1 = do_pcibr_rrb_count_valid(bridge, slot + PCIBR_RRB_SLOT_VIRTUAL);

#if PCIBR_RRB_DEBUG
    printk("pcibr_attach: slot %d started with %d+%d\n", slot, c0, c1);
#endif

    /* Do we really need any?
     */
    pcibr_infoh = pcibr_soft->bs_slot[slot].bss_infos;
    pcibr_info = pcibr_infoh[0];
    if ((pcibr_info->f_vendor == PCIIO_VENDOR_ID_NONE) &&
	!pcibr_soft->bs_slot[slot].has_host) {
	if (c0 > 0)
	    do_pcibr_rrb_free(bridge, slot, c0);
	if (c1 > 0)
	    do_pcibr_rrb_free(bridge, slot + PCIBR_RRB_SLOT_VIRTUAL, c1);
	pcibr_soft->bs_rrb_valid[slot] = 0x1000;
	pcibr_soft->bs_rrb_valid[slot + PCIBR_RRB_SLOT_VIRTUAL] = 0x1000;
	return(ENODEV);
    }

    pcibr_soft->bs_rrb_avail[slot & 1] -= c0 + c1;
    pcibr_soft->bs_rrb_valid[slot] = c0;
    pcibr_soft->bs_rrb_valid[slot + PCIBR_RRB_SLOT_VIRTUAL] = c1;

    pcibr_soft->bs_rrb_avail[0] = do_pcibr_rrb_count_avail(bridge, 0);
    pcibr_soft->bs_rrb_avail[1] = do_pcibr_rrb_count_avail(bridge, 1);

    r = 3 - (c0 + c1);

    if (r > 0) {
	pcibr_soft->bs_rrb_res[slot] = r;
	pcibr_soft->bs_rrb_avail[slot & 1] -= r;
    }

#if PCIBR_RRB_DEBUG
    printk("\t%d+%d+%d",
	    0xFFF & pcibr_soft->bs_rrb_valid[slot],
	    0xFFF & pcibr_soft->bs_rrb_valid[slot + PCIBR_RRB_SLOT_VIRTUAL],
	    pcibr_soft->bs_rrb_res[slot]);
    printk("\n");
#endif

    return(0);
}

/*
 * pcibr_slot_call_device_attach
 *	This calls the associated driver attach routine for the PCI
 * 	card in this slot.
 */
int
pcibr_slot_call_device_attach(devfs_handle_t pcibr_vhdl,
			      pciio_slot_t slot,
			      int          drv_flags)
{
    pcibr_soft_t	pcibr_soft;
    pcibr_info_h	pcibr_infoh;
    pcibr_info_t	pcibr_info;
    async_attach_t	aa = NULL;
    int			func;
    devfs_handle_t	xconn_vhdl,conn_vhdl;
    int			nfunc;
    int                 error_func;
    int                 error_slot = 0;
    int                 error = ENODEV;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    if (!pcibr_soft || !PCIBR_VALID_SLOT(slot))
	return(EINVAL);


    if (pcibr_soft->bs_slot[slot].has_host) {
        return(EPERM);
    }
    
    xconn_vhdl = pcibr_soft->bs_conn;
    aa = async_attach_get_info(xconn_vhdl);

    nfunc = pcibr_soft->bs_slot[slot].bss_ninfo;
    pcibr_infoh = pcibr_soft->bs_slot[slot].bss_infos;

    for (func = 0; func < nfunc; ++func) {

	pcibr_info = pcibr_infoh[func];
	
	if (!pcibr_info)
	    continue;

	if (pcibr_info->f_vendor == PCIIO_VENDOR_ID_NONE)
	    continue;

	conn_vhdl = pcibr_info->f_vertex;

#ifdef LATER
	/*
	 * Activate if and when we support cdl.
	 */
	if (aa)
	    async_attach_add_info(conn_vhdl, aa);
#endif	/* LATER */

	error_func = pciio_device_attach(conn_vhdl, drv_flags);

        pcibr_info->f_att_det_error = error_func;

	if (error_func)
	    error_slot = error_func;

        error = error_slot;

    }				/* next func */

    if (error) {
	if ((error != ENODEV) && (error != EUNATCH))
	    pcibr_soft->bs_slot[slot].slot_status |= SLOT_STARTUP_INCMPLT;
    } else {
        pcibr_soft->bs_slot[slot].slot_status |= SLOT_STARTUP_CMPLT;
    }
        
    return(error);
}

/*
 * pcibr_slot_call_device_detach
 *	This calls the associated driver detach routine for the PCI
 * 	card in this slot.
 */
int
pcibr_slot_call_device_detach(devfs_handle_t pcibr_vhdl,
			      pciio_slot_t slot,
			      int          drv_flags)
{
    pcibr_soft_t	pcibr_soft;
    pcibr_info_h	pcibr_infoh;
    pcibr_info_t	pcibr_info;
    int			func;
    devfs_handle_t	conn_vhdl = GRAPH_VERTEX_NONE;
    int			nfunc;
    int                 error_func;
    int                 error_slot = 0;
    int                 error = ENODEV;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);

    if (!pcibr_soft || !PCIBR_VALID_SLOT(slot))
	return(EINVAL);

    if (pcibr_soft->bs_slot[slot].has_host)
        return(EPERM);

    /* Make sure that we do not detach a system critical function vertex */
    if(pcibr_is_slot_sys_critical(pcibr_vhdl, slot))
        return(EPERM);

    nfunc = pcibr_soft->bs_slot[slot].bss_ninfo;
    pcibr_infoh = pcibr_soft->bs_slot[slot].bss_infos;

    for (func = 0; func < nfunc; ++func) {

	pcibr_info = pcibr_infoh[func];
	
	if (!pcibr_info)
	    continue;

	if (pcibr_info->f_vendor == PCIIO_VENDOR_ID_NONE)
	    continue;

	conn_vhdl = pcibr_info->f_vertex;

	error_func = pciio_device_detach(conn_vhdl, drv_flags);

        pcibr_info->f_att_det_error = error_func;

	if (error_func)
	    error_slot = error_func;

	error = error_slot;

    }				/* next func */

    pcibr_soft->bs_slot[slot].slot_status &= ~SLOT_STATUS_MASK;

    if (error) {
	if ((error != ENODEV) && (error != EUNATCH))
            pcibr_soft->bs_slot[slot].slot_status |= SLOT_SHUTDOWN_INCMPLT;
    } else {
        if (conn_vhdl != GRAPH_VERTEX_NONE) 
            pcibr_device_unregister(conn_vhdl);
        pcibr_soft->bs_slot[slot].slot_status |= SLOT_SHUTDOWN_CMPLT;
    }
        
    return(error);
}

/*
 * pcibr_slot_detach
 *	This is a place holder routine to keep track of all the
 *	slot-specific freeing that needs to be done.
 */
int
pcibr_slot_detach(devfs_handle_t pcibr_vhdl,
		  pciio_slot_t slot,
		  int          drv_flags)
{
    int		  error;
    
    /* Call the device detach function */
    error = (pcibr_slot_call_device_detach(pcibr_vhdl, slot, drv_flags));
    return (error);

}

/*
 * pcibr_is_slot_sys_critical
 *      Check slot for any functions that are system critical.
 *      Return 1 if any are system critical or 0 otherwise.
 *
 *      This function will always return 0 when called by 
 *      pcibr_attach() because the system critical vertices 
 *      have not yet been set in the hwgraph.
 */
int
pcibr_is_slot_sys_critical(devfs_handle_t pcibr_vhdl,
                      pciio_slot_t slot)
{
    pcibr_soft_t        pcibr_soft;
    pcibr_info_h        pcibr_infoh;
    pcibr_info_t        pcibr_info;
    devfs_handle_t        conn_vhdl = GRAPH_VERTEX_NONE;
    int                 nfunc;
    int                 func;

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    if (!pcibr_soft || !PCIBR_VALID_SLOT(slot))
        return(0);

    nfunc = pcibr_soft->bs_slot[slot].bss_ninfo;
    pcibr_infoh = pcibr_soft->bs_slot[slot].bss_infos;

    for (func = 0; func < nfunc; ++func) {

        pcibr_info = pcibr_infoh[func];
        if (!pcibr_info)
            continue;

        if (pcibr_info->f_vendor == PCIIO_VENDOR_ID_NONE)
            continue;

        conn_vhdl = pcibr_info->f_vertex;
        if (is_sys_critical_vertex(conn_vhdl)) { 
#if defined(SUPPORT_PRINTING_V_FORMAT)
            printk(KERN_WARNING  "%v is a system critical device vertex\n", conn_vhdl);
#else
            printk(KERN_WARNING  "%p is a system critical device vertex\n", (void *)conn_vhdl);
#endif
            return(1); 
        }

    }

    return(0);
}

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

    /* Free the rrbs allocated to this slot */
    error_call = do_pcibr_rrb_free(bridge, slot, 
		                   pcibr_soft->bs_rrb_valid[slot] +
		                   pcibr_soft->bs_rrb_valid[slot + 
                                   PCIBR_RRB_SLOT_VIRTUAL]);

    if (error_call)
        error = ERANGE;

    pcibr_soft->bs_rrb_valid[slot] = 0;
    pcibr_soft->bs_rrb_valid[slot + PCIBR_RRB_SLOT_VIRTUAL] = 0;
    pcibr_soft->bs_rrb_res[slot] = 0;

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

#ifndef MEDUSA_HACK
    if ((bridge->b_wid_stat & BRIDGE_STAT_PCI_GIO_N) == 0)
	return -1;			/* someone else handles GIO bridges. */
#endif

    if (XWIDGET_PART_REV_NUM(bridge->b_wid_id) == XBRIDGE_PART_REV_A)
	NeedXbridgeSwap = 1;

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
	/*
	 * Determine the base address node id to be used for all 32-bit
	 * Direct Mapping I/O. The default is node 0, but this can be changed
	 * via a DEVICE_ADMIN directive and the PCIBUS_DMATRANS_NODE
	 * attribute in the irix.sm config file. A device driver can obtain
	 * this node value via a call to pcibr_get_dmatrans_node().
	 */
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
	for (entry = 0; entry < pcibr_soft->bs_int_ate_size; entry++)
	    bridge->b_int_ate_ram[entry].wr = 0;

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

#ifndef __ia64
    for (slot = 0; slot < 8; ++slot)  
	/* Set up convenience links */
	if (is_xbridge(bridge))
		if (pcibr_soft->bs_slot[slot].bss_ninfo > 0) /* if occupied */
			pcibr_bus_cnvlink(pcibr_info->f_vertex, slot);
#endif

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

LOCAL iopaddr_t
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
LOCAL iopaddr_t
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

/*
 * Convert PCI-generic software flags and Bridge-specific software flags
 * into Bridge-specific Address Translation Entry attribute bits.
 */
LOCAL bridge_ate_t
pcibr_flags_to_ate(unsigned flags)
{
    bridge_ate_t            attributes;

    /* default if nothing specified:
     * NOBARRIER
     * NOPREFETCH
     * NOPRECISE
     * COHERENT
     * Plus the valid bit
     */
    attributes = ATE_CO | ATE_V;

    /* Generic macro flags
     */
    if (flags & PCIIO_DMA_DATA) {	/* standard data channel */
	attributes &= ~ATE_BAR;		/* no barrier */
	attributes |= ATE_PREF;		/* prefetch on */
    }
    if (flags & PCIIO_DMA_CMD) {	/* standard command channel */
	attributes |= ATE_BAR;		/* barrier bit on */
	attributes &= ~ATE_PREF;	/* disable prefetch */
    }
    /* Generic detail flags
     */
    if (flags & PCIIO_PREFETCH)
	attributes |= ATE_PREF;
    if (flags & PCIIO_NOPREFETCH)
	attributes &= ~ATE_PREF;

    /* Provider-specific flags
     */
    if (flags & PCIBR_BARRIER)
	attributes |= ATE_BAR;
    if (flags & PCIBR_NOBARRIER)
	attributes &= ~ATE_BAR;

    if (flags & PCIBR_PREFETCH)
	attributes |= ATE_PREF;
    if (flags & PCIBR_NOPREFETCH)
	attributes &= ~ATE_PREF;

    if (flags & PCIBR_PRECISE)
	attributes |= ATE_PREC;
    if (flags & PCIBR_NOPRECISE)
	attributes &= ~ATE_PREC;

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

#ifdef IRIX
    NEWf(pcibr_dmamap, flags);
#else
    /*
     * On SNIA64, these maps are pre-allocated because pcibr_dmamap_alloc()
     * can be called within an interrupt thread.
     */
    pcibr_dmamap = (pcibr_dmamap_t)get_free_pciio_dmamap(pcibr_soft->bs_vhdl);
#endif

    if (!pcibr_dmamap)
	return 0;

    xtalk_dmamap = xtalk_dmamap_alloc(xconn_vhdl, dev_desc, req_size_max,
				      flags & DMAMAP_FLAGS);
    if (!xtalk_dmamap) {
#if PCIBR_ATE_DEBUG
	printk("pcibr_attach: xtalk_dmamap_alloc failed\n");
#endif
#ifdef IRIX
	DEL(pcibr_dmamap);
#else
	free_pciio_dmamap(pcibr_dmamap);
#endif
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
#ifdef IRIX
	DEL(pcibr_dmamap);
#else
	free_pciio_dmamap(pcibr_dmamap);
#endif
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
#ifdef IRIX
    DEL(pcibr_dmamap);
#else
    free_pciio_dmamap(pcibr_dmamap);
#endif
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
#ifdef IRIX
    DEL(pcibr_dmamap);
#else
    free_pciio_dmamap(pcibr_dmamap);
#endif
}

/*
 * Setup an Address Translation Entry as specified.  Use either the Bridge
 * internal maps or the external map RAM, as appropriate.
 */
LOCAL bridge_ate_p
pcibr_ate_addr(pcibr_soft_t pcibr_soft,
	       int ate_index)
{
    bridge_t *bridge = pcibr_soft->bs_base;

    return (ate_index < pcibr_soft->bs_int_ate_size)
	? &(bridge->b_int_ate_ram[ate_index].wr)
	: &(bridge->b_ext_ate_ram[ate_index]);
}

/*
 *    pcibr_addr_xio_to_pci: given a PIO range, hand
 *      back the corresponding base PCI MEM address;
 *      this is used to short-circuit DMA requests that
 *      loop back onto this PCI bus.
 */
LOCAL iopaddr_t
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

/* We are starting to get more complexity
 * surrounding writing ATEs, so pull
 * the writing code into this new function.
 */

#if PCIBR_FREEZE_TIME
#define	ATE_FREEZE()	s = ate_freeze(pcibr_dmamap, &freeze_time, cmd_regs)
#else
#define	ATE_FREEZE()	s = ate_freeze(pcibr_dmamap, cmd_regs)
#endif

LOCAL unsigned
ate_freeze(pcibr_dmamap_t pcibr_dmamap,
#if PCIBR_FREEZE_TIME
	   unsigned *freeze_time_ptr,
#endif
	   unsigned *cmd_regs)
{
    pcibr_soft_t            pcibr_soft = pcibr_dmamap->bd_soft;
#ifdef LATER
    int                     dma_slot = pcibr_dmamap->bd_slot;
#endif
    int                     ext_ates = pcibr_dmamap->bd_flags & PCIBR_DMAMAP_SSRAM;
    int                     slot;

    unsigned long           s;
    unsigned                cmd_reg;
    volatile unsigned      *cmd_lwa;
    unsigned                cmd_lwd;

    if (!ext_ates)
	return 0;

    /* Bridge Hardware Bug WAR #484930:
     * Bridge can't handle updating External ATEs
     * while DMA is occuring that uses External ATEs,
     * even if the particular ATEs involved are disjoint.
     */

    /* need to prevent anyone else from
     * unfreezing the grant while we
     * are working; also need to prevent
     * this thread from being interrupted
     * to keep PCI grant freeze time
     * at an absolute minimum.
     */
    s = pcibr_lock(pcibr_soft);

#ifdef LATER
    /* just in case pcibr_dmamap_done was not called */
    if (pcibr_dmamap->bd_flags & PCIBR_DMAMAP_BUSY) {
	pcibr_dmamap->bd_flags &= ~PCIBR_DMAMAP_BUSY;
	if (pcibr_dmamap->bd_flags & PCIBR_DMAMAP_SSRAM)
	    atomic_dec(&(pcibr_soft->bs_slot[dma_slot]. bss_ext_ates_active));
	xtalk_dmamap_done(pcibr_dmamap->bd_xtalk);
    }
#endif	/* LATER */
#if PCIBR_FREEZE_TIME
    *freeze_time_ptr = get_timestamp();
#endif

    cmd_lwa = 0;
    for (slot = 0; slot < 8; ++slot)
	if (atomic_read(&pcibr_soft->bs_slot[slot].bss_ext_ates_active)) {
	    cmd_reg = pcibr_soft->
		bs_slot[slot].
		bss_cmd_shadow;
	    if (cmd_reg & PCI_CMD_BUS_MASTER) {
		cmd_lwa = pcibr_soft->
		    bs_slot[slot].
		    bss_cmd_pointer;
		cmd_lwd = cmd_reg ^ PCI_CMD_BUS_MASTER;
		cmd_lwa[0] = cmd_lwd;
	    }
	    cmd_regs[slot] = cmd_reg;
	} else
	    cmd_regs[slot] = 0;

    if (cmd_lwa) {
	    bridge_t	*bridge = pcibr_soft->bs_base;

	    /* Read the last master bit that has been cleared. This PIO read
	     * on the PCI bus is to ensure the completion of any DMAs that
	     * are due to bus requests issued by PCI devices before the
	     * clearing of master bits.
	     */
	    cmd_lwa[0];

	    /* Flush all the write buffers in the bridge */
	    for (slot = 0; slot < 8; ++slot)
		    if (atomic_read(&pcibr_soft->bs_slot[slot].bss_ext_ates_active)) {
			    /* Flush the write buffer associated with this
			     * PCI device which might be using dma map RAM.
			     */
			    bridge->b_wr_req_buf[slot].reg;
		    }
    }
    return s;
}

#define	ATE_WRITE()    ate_write(ate_ptr, ate_count, ate)

LOCAL void
ate_write(bridge_ate_p ate_ptr,
	  int ate_count,
	  bridge_ate_t ate)
{
    while (ate_count-- > 0) {
	*ate_ptr++ = ate;
	ate += IOPGSIZE;
    }
}


#if PCIBR_FREEZE_TIME
#define	ATE_THAW()	ate_thaw(pcibr_dmamap, ate_index, ate, ate_total, freeze_time, cmd_regs, s)
#else
#define	ATE_THAW()	ate_thaw(pcibr_dmamap, ate_index, cmd_regs, s)
#endif

LOCAL void
ate_thaw(pcibr_dmamap_t pcibr_dmamap,
	 int ate_index,
#if PCIBR_FREEZE_TIME
	 bridge_ate_t ate,
	 int ate_total,
	 unsigned freeze_time_start,
#endif
	 unsigned *cmd_regs,
	 unsigned s)
{
    pcibr_soft_t            pcibr_soft = pcibr_dmamap->bd_soft;
    int                     dma_slot = pcibr_dmamap->bd_slot;
    int                     slot;
    bridge_t               *bridge = pcibr_soft->bs_base;
    int                     ext_ates = pcibr_dmamap->bd_flags & PCIBR_DMAMAP_SSRAM;

    unsigned                cmd_reg;

#if PCIBR_FREEZE_TIME
    unsigned                freeze_time;
    static unsigned         max_freeze_time = 0;
    static unsigned         max_ate_total;
#endif

    if (!ext_ates)
	return;

    /* restore cmd regs */
    for (slot = 0; slot < 8; ++slot)
	if ((cmd_reg = cmd_regs[slot]) & PCI_CMD_BUS_MASTER)
	    bridge->b_type0_cfg_dev[slot].l[PCI_CFG_COMMAND / 4] = cmd_reg;

    pcibr_dmamap->bd_flags |= PCIBR_DMAMAP_BUSY;
    atomic_inc(&(pcibr_soft->bs_slot[dma_slot]. bss_ext_ates_active));

#if PCIBR_FREEZE_TIME
    freeze_time = get_timestamp() - freeze_time_start;

    if ((max_freeze_time < freeze_time) ||
	(max_ate_total < ate_total)) {
	if (max_freeze_time < freeze_time)
	    max_freeze_time = freeze_time;
	if (max_ate_total < ate_total)
	    max_ate_total = ate_total;
	pcibr_unlock(pcibr_soft, s);
	printk("%s: pci freeze time %d usec for %d ATEs\n"
		"\tfirst ate: %R\n",
		pcibr_soft->bs_name,
		freeze_time * 1000 / 1250,
		ate_total,
		ate, ate_bits);
    } else
#endif
	pcibr_unlock(pcibr_soft, s);
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
		printk(KERN_WARNING   "pcibr_dmamap_addr :\n"
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
		printk(KERN_WARNING   "pcibr_dmamap_list :\n"
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
 *	On a XBridge (IP35), we do this by writing the appropriate Bridge Force 
 *	Interrupt register.
 */
void
pcibr_force_interrupt(pcibr_intr_wrap_t wrap)
{
	unsigned	bit;
	pcibr_soft_t    pcibr_soft = wrap->iw_soft;
	bridge_t       *bridge = pcibr_soft->bs_base;
	cpuid_t cpuvertex_to_cpuid(devfs_handle_t vhdl);

	bit = wrap->iw_intr;

	if (pcibr_soft->bs_xbridge) {
	    bridge->b_force_pin[bit].intr = 1;
	} else if ((1 << bit) & *wrap->iw_stat) {
	    cpuid_t	    cpu;
	    unsigned        intr_bit;
	    xtalk_intr_t    xtalk_intr =
				pcibr_soft->bs_intr[bit].bsi_xtalk_intr;

	    intr_bit = (short) xtalk_intr_vector_get(xtalk_intr);
	    cpu = cpuvertex_to_cpuid(xtalk_intr_cpu_get(xtalk_intr));
#if defined(CONFIG_IA64_SGI_SN1)
	    REMOTE_CPU_SEND_INTR(cpu, intr_bit);
#endif
	}
}

/* =====================================================================
 *    INTERRUPT MANAGEMENT
 */

static unsigned
pcibr_intr_bits(pciio_info_t info,
		pciio_intr_line_t lines)
{
    pciio_slot_t            slot = pciio_info_slot_get(info);
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

    if (slot < 8) {
	if (lines & (PCIIO_INTR_LINE_A| PCIIO_INTR_LINE_C))
	    bbits |= 1 << slot;
	if (lines & (PCIIO_INTR_LINE_B| PCIIO_INTR_LINE_D))
	    bbits |= 1 << (slot ^ 4);
    }
    return bbits;
}


/*ARGSUSED */
pcibr_intr_t
pcibr_intr_alloc(devfs_handle_t pconn_vhdl,
		 device_desc_t dev_desc,
		 pciio_intr_line_t lines,
		 devfs_handle_t owner_dev)
{
    pcibr_info_t            pcibr_info = pcibr_info_get(pconn_vhdl);
    pciio_slot_t            pciio_slot = pcibr_info->f_slot;
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pcibr_info->f_mfast;
    devfs_handle_t            xconn_vhdl = pcibr_soft->bs_conn;
    bridge_t               *bridge = pcibr_soft->bs_base;
    int                     is_threaded = 0;
    int                     thread_swlevel;

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
    bridgereg_t             int_dev;

#if DEBUG && INTR_DEBUG
    printk("%v: pcibr_intr_alloc\n"
	    "%v:%s%s%s%s%s\n",
	    owner_dev, pconn_vhdl,
	    !(lines & 15) ? " No INTs?" : "",
	    lines & 1 ? " INTA" : "",
	    lines & 2 ? " INTB" : "",
	    lines & 4 ? " INTC" : "",
	    lines & 8 ? " INTD" : "");
#endif

    NEW(pcibr_intr);
    if (!pcibr_intr)
	return NULL;

    if (dev_desc) {
	cpuid_t intr_target_from_desc(device_desc_t, int);
    } else {
	extern int default_intr_pri;

	is_threaded = 1; /* PCI interrupts are threaded, by default */
	thread_swlevel = default_intr_pri;
    }

    pcibr_intr->bi_dev = pconn_vhdl;
    pcibr_intr->bi_lines = lines;
    pcibr_intr->bi_soft = pcibr_soft;
    pcibr_intr->bi_ibits = 0;		/* bits will be added below */
    pcibr_intr->bi_flags = is_threaded ? 0 : PCIIO_INTR_NOTHREAD;
    pcibr_intr->bi_mustruncpu = CPU_NONE;
    mutex_spinlock_init(&pcibr_intr->bi_ibuf.ib_lock);

    pcibr_int_bits = pcibr_soft->bs_intr_bits((pciio_info_t)pcibr_info, lines);


    /*
     * For each PCI interrupt line requested, figure
     * out which Bridge PCI Interrupt Line it maps
     * to, and make sure there are xtalk resources
     * allocated for it.
     */
#if DEBUG && INTR_DEBUG
    printk("pcibr_int_bits: 0x%X\n", pcibr_int_bits);
#endif 
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
		 * 2) On IP35, addressing constraints on IP35 and Bridge force
		 *    us to use a single PI number for all interrupts from a
		 *    single Bridge. (IP35-specific code forces this, and we
		 *    verify in pcibr_setwidint.)
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
#if DEBUG && INTR_DEBUG
		printk("%v: xtalk_intr=0x%X\n", xconn_vhdl, xtalk_intr);
#endif

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
#ifdef SUPPORT_PRINTING_V_FORMAT
			printk(KERN_ALERT  
				"pcibr_intr_alloc %v: unable to get xtalk interrupt resources",
				xconn_vhdl);
#else
			printk(KERN_ALERT  
				"pcibr_intr_alloc 0x%p: unable to get xtalk interrupt resources",
				(void *)xconn_vhdl);
#endif
			/* yes, we leak resources here. */
			return 0;
		    }
		} else if (compare_and_swap_ptr((void **) xtalk_intr_p, NULL, xtalk_intr)) {
		    /*
		     * now tell the bridge which slot is
		     * using this interrupt line.
		     */
		    int_dev = bridge->b_int_device;
		    int_dev &= ~BRIDGE_INT_DEV_MASK(pcibr_int_bit);
		    int_dev |= pciio_slot << BRIDGE_INT_DEV_SHFT(pcibr_int_bit);
		    bridge->b_int_device = int_dev;	/* XXXMP */

#if DEBUG && INTR_DEBUG
		    printk("%v: bridge intr bit %d clears my wrb\n",
			    pconn_vhdl, pcibr_int_bit);
#endif
		} else {
		    /* someone else got one allocated first;
		     * free the one we just created, and
		     * retrieve the one they allocated.
		     */
		    xtalk_intr_free(xtalk_intr);
		    xtalk_intr = *xtalk_intr_p;
#if PARANOID
		    /* once xtalk_intr is set, we never clear it,
		     * so if the CAS fails above, this condition
		     * can "never happen" ...
		     */
		    if (!xtalk_intr) {
			printk(KERN_ALERT  
				"pcibr_intr_alloc %v: unable to set xtalk interrupt resources",
				xconn_vhdl);
			/* yes, we leak resources here. */
			return 0;
		    }
#endif
		}
	    }

	    pcibr_intr->bi_ibits |= 1 << pcibr_int_bit;

	    NEW(intr_entry);
	    intr_entry->il_next = NULL;
	    intr_entry->il_intr = pcibr_intr;
	    intr_entry->il_wrbf = &(bridge->b_wr_req_buf[pciio_slot].reg);
	    intr_list_p = 
		&pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap.iw_list;
#if DEBUG && INTR_DEBUG
#if defined(SUPPORT_PRINTING_V_FORMAT)
	    printk("0x%x: Bridge bit %d wrap=0x%x\n",
		pconn_vhdl, pcibr_int_bit,
		pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap);
#else
	    printk("%v: Bridge bit %d wrap=0x%x\n",
		pconn_vhdl, pcibr_int_bit,
		pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap);
#endif
#endif

	    if (compare_and_swap_ptr((void **) intr_list_p, NULL, intr_entry)) {
		/* we are the first interrupt on this bridge bit.
		 */
#if DEBUG && INTR_DEBUG
		printk("%v INT 0x%x (bridge bit %d) allocated [FIRST]\n",
			pconn_vhdl, pcibr_int_bits, pcibr_int_bit);
#endif
		continue;
	    }
	    intr_list = *intr_list_p;
	    pcibr_intr_p = &intr_list->il_intr;
	    if (compare_and_swap_ptr((void **) pcibr_intr_p, NULL, pcibr_intr)) {
		/* first entry on list was erased,
		 * and we replaced it, so we
		 * don't need our intr_entry.
		 */
		DEL(intr_entry);
#if DEBUG && INTR_DEBUG
		printk("%v INT 0x%x (bridge bit %d) replaces erased first\n",
			pconn_vhdl, pcibr_int_bits, pcibr_int_bit);
#endif
		continue;
	    }
	    intr_list_p = &intr_list->il_next;
	    if (compare_and_swap_ptr((void **) intr_list_p, NULL, intr_entry)) {
		/* we are the new second interrupt on this bit.
		 */
		pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap.iw_shared = 1;
#if DEBUG && INTR_DEBUG
		printk("%v INT 0x%x (bridge bit %d) is new SECOND\n",
			pconn_vhdl, pcibr_int_bits, pcibr_int_bit);
#endif
		continue;
	    }
	    while (1) {
		pcibr_intr_p = &intr_list->il_intr;
		if (compare_and_swap_ptr((void **) pcibr_intr_p, NULL, pcibr_intr)) {
		    /* an entry on list was erased,
		     * and we replaced it, so we
		     * don't need our intr_entry.
		     */
		    DEL(intr_entry);
#if DEBUG && INTR_DEBUG
		    printk("%v INT 0x%x (bridge bit %d) replaces erased Nth\n",
			    pconn_vhdl, pcibr_int_bits, pcibr_int_bit);
#endif
		    break;
		}
		intr_list_p = &intr_list->il_next;
		if (compare_and_swap_ptr((void **) intr_list_p, NULL, intr_entry)) {
		    /* entry appended to share list
		     */
#if DEBUG && INTR_DEBUG
		    printk("%v INT 0x%x (bridge bit %d) is new Nth\n",
			    pconn_vhdl, pcibr_int_bits, pcibr_int_bit);
#endif
		    break;
		}
		/* step to next record in chain
		 */
		intr_list = *intr_list_p;
	    }
	}
    }

#if DEBUG && INTR_DEBUG
    printk("%v pcibr_intr_alloc complete\n", pconn_vhdl);
#endif
    hub_intr = (hub_intr_t)xtalk_intr;
    pcibr_intr->bi_irq = hub_intr->i_bit;
    pcibr_intr->bi_cpu = hub_intr->i_cpuid;
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
#if DEBUG && INTR_DEBUG
		    printk("%s: cleared a handler from bit %d\n",
			    pcibr_soft->bs_name, pcibr_int_bit);
#endif
		}
	    /* If this interrupt line is not being shared between multiple
	     * devices release the xtalk interrupt resources.
	     */
	    intr_shared = 
		pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap.iw_shared;
	    xtalk_intrp = &pcibr_soft->bs_intr[pcibr_int_bit].bsi_xtalk_intr;

	    if ((!intr_shared) && (*xtalk_intrp)) {

		bridge_t 	*bridge = pcibr_soft->bs_base;
		bridgereg_t	int_dev;

		xtalk_intr_free(*xtalk_intrp);
		*xtalk_intrp = 0;

		/* Clear the PCI device interrupt to bridge interrupt pin
		 * mapping.
		 */
		int_dev = bridge->b_int_device;
		int_dev &= ~BRIDGE_INT_DEV_MASK(pcibr_int_bit);
		bridge->b_int_device = int_dev;

	    }
	}
    }
    DEL(pcibr_intr);
}

LOCAL void
pcibr_setpciint(xtalk_intr_t xtalk_intr)
{
    iopaddr_t               addr = xtalk_intr_addr_get(xtalk_intr);
    xtalk_intr_vector_t     vect = xtalk_intr_vector_get(xtalk_intr);
    bridgereg_t            *int_addr = (bridgereg_t *)
    xtalk_intr_sfarg_get(xtalk_intr);

#ifdef CONFIG_IA64_SGI_SN2
    *int_addr = ((BRIDGE_INT_ADDR_HOST & (addr >> 26)) |
		 (BRIDGE_INT_ADDR_FLD & vect));
#elif CONFIG_IA64_SGI_SN1
    *int_addr = ((BRIDGE_INT_ADDR_HOST & (addr >> 30)) |
		 (BRIDGE_INT_ADDR_FLD & vect));
#endif
}

/*ARGSUSED */
int
pcibr_intr_connect(pcibr_intr_t pcibr_intr)
{
    pcibr_soft_t            pcibr_soft = pcibr_intr->bi_soft;
    bridge_t               *bridge = pcibr_soft->bs_base;
    unsigned                pcibr_int_bits = pcibr_intr->bi_ibits;
    unsigned                pcibr_int_bit;
    bridgereg_t             b_int_enable;
    unsigned long           s;

    if (pcibr_intr == NULL)
	return -1;

#if DEBUG && INTR_DEBUG
    printk("%v: pcibr_intr_connect\n",
	    pcibr_intr->bi_dev);
#endif

    *((volatile unsigned *)&pcibr_intr->bi_flags) |= PCIIO_INTR_CONNECTED;

    /*
     * For each PCI interrupt line requested, figure
     * out which Bridge PCI Interrupt Line it maps
     * to, and make sure there are xtalk resources
     * allocated for it.
     */
    for (pcibr_int_bit = 0; pcibr_int_bit < 8; pcibr_int_bit++)
	if (pcibr_int_bits & (1 << pcibr_int_bit)) {
	    xtalk_intr_t            xtalk_intr;

	    xtalk_intr = pcibr_soft->bs_intr[pcibr_int_bit].bsi_xtalk_intr;

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
	    xtalk_intr_connect(xtalk_intr, (xtalk_intr_setfunc_t) pcibr_setpciint,
			       (void *)&(bridge->b_int_addr[pcibr_int_bit].addr));
	    pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap.iw_connected = 1;

#if DEBUG && INTR_DEBUG
	    printk("%v bridge bit %d wrapper connected\n",
		    pcibr_intr->bi_dev, pcibr_int_bit);
#endif
	}
    s = pcibr_lock(pcibr_soft);
    b_int_enable = bridge->b_int_enable;
    b_int_enable |= pcibr_int_bits;
    bridge->b_int_enable = b_int_enable;
    bridge->b_wid_tflush;		/* wait until Bridge PIO complete */
    pcibr_unlock(pcibr_soft, s);

    return 0;
}

/*ARGSUSED */
void
pcibr_intr_disconnect(pcibr_intr_t pcibr_intr)
{
    pcibr_soft_t            pcibr_soft = pcibr_intr->bi_soft;
    bridge_t               *bridge = pcibr_soft->bs_base;
    unsigned                pcibr_int_bits = pcibr_intr->bi_ibits;
    unsigned                pcibr_int_bit;
    bridgereg_t             b_int_enable;
    unsigned long           s;

    /* Stop calling the function. Now.
     */
    *((volatile unsigned *)&pcibr_intr->bi_flags) &= ~PCIIO_INTR_CONNECTED;
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
    b_int_enable = bridge->b_int_enable;
    b_int_enable &= ~pcibr_int_bits;
    bridge->b_int_enable = b_int_enable;
    bridge->b_wid_tflush;		/* wait until Bridge PIO complete */
    pcibr_unlock(pcibr_soft, s);

    for (pcibr_int_bit = 0; pcibr_int_bit < 8; pcibr_int_bit++)
	if (pcibr_int_bits & (1 << pcibr_int_bit)) {
	    /* if the interrupt line is now shared,
	     * do not disconnect it.
	     */
	    if (pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap.iw_shared)
		continue;

	    xtalk_intr_disconnect(pcibr_soft->bs_intr[pcibr_int_bit].bsi_xtalk_intr);
	    pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap.iw_connected = 0;

#if DEBUG && INTR_DEBUG
	    printk("%s: xtalk disconnect done for Bridge bit %d\n",
		pcibr_soft->bs_name, pcibr_int_bit);
#endif

	    /* if we are sharing the interrupt line,
	     * connect us up; this closes the hole
	     * where the another pcibr_intr_alloc()
	     * was in progress as we disconnected.
	     */
	    if (!pcibr_soft->bs_intr[pcibr_int_bit].bsi_pcibr_intr_wrap.iw_shared)
		continue;

	    xtalk_intr_connect(pcibr_soft->bs_intr[pcibr_int_bit].bsi_xtalk_intr,
			       (xtalk_intr_setfunc_t)pcibr_setpciint,
			       (void *) &(bridge->b_int_addr[pcibr_int_bit].addr));
	}
}

/*ARGSUSED */
devfs_handle_t
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
LOCAL void
pcibr_clearwidint(bridge_t *bridge)
{
    bridge->b_wid_int_upper = 0;
    bridge->b_wid_int_lower = 0;
}

LOCAL void
pcibr_setwidint(xtalk_intr_t intr)
{
    xwidgetnum_t            targ = xtalk_intr_target_get(intr);
    iopaddr_t               addr = xtalk_intr_addr_get(intr);
    xtalk_intr_vector_t     vect = xtalk_intr_vector_get(intr);
    widgetreg_t		    NEW_b_wid_int_upper, NEW_b_wid_int_lower;
    widgetreg_t		    OLD_b_wid_int_upper, OLD_b_wid_int_lower;

    bridge_t               *bridge = (bridge_t *)xtalk_intr_sfarg_get(intr);

    NEW_b_wid_int_upper = ( (0x000F0000 & (targ << 16)) |
			       XTALK_ADDR_TO_UPPER(addr));
    NEW_b_wid_int_lower = XTALK_ADDR_TO_LOWER(addr);

    OLD_b_wid_int_upper = bridge->b_wid_int_upper;
    OLD_b_wid_int_lower = bridge->b_wid_int_lower;

    /* Verify that all interrupts from this Bridge are using a single PI */
    if ((OLD_b_wid_int_upper != 0) && (OLD_b_wid_int_lower != 0)) {
	/*
	 * Once set, these registers shouldn't change; they should
	 * be set multiple times with the same values.
	 *
	 * If we're attempting to change these registers, it means
	 * that our heuristics for allocating interrupts in a way
	 * appropriate for IP35 have failed, and the admin needs to
	 * explicitly direct some interrupts (or we need to make the
	 * heuristics more clever).
	 *
	 * In practice, we hope this doesn't happen very often, if
	 * at all.
	 */
	if ((OLD_b_wid_int_upper != NEW_b_wid_int_upper) ||
	    (OLD_b_wid_int_lower != NEW_b_wid_int_lower)) {
		printk(KERN_WARNING  "Interrupt allocation is too complex.\n");
		printk(KERN_WARNING  "Use explicit administrative interrupt targetting.\n");
		printk(KERN_WARNING  "bridge=0x%lx targ=0x%x\n", (unsigned long)bridge, targ);
		printk(KERN_WARNING  "NEW=0x%x/0x%x  OLD=0x%x/0x%x\n",
			NEW_b_wid_int_upper, NEW_b_wid_int_lower,
			OLD_b_wid_int_upper, OLD_b_wid_int_lower);
		PRINT_PANIC("PCI Bridge interrupt targetting error\n");
	}
    }

    bridge->b_wid_int_upper = NEW_b_wid_int_upper;
    bridge->b_wid_int_lower = NEW_b_wid_int_lower;
    bridge->b_int_host_err = vect;
}

/*
 * pcibr_intr_preset: called during mlreset time
 * if the platform specific code needs to route
 * one of the Bridge's xtalk interrupts before the
 * xtalk infrastructure is available.
 */
void
pcibr_xintr_preset(void *which_widget,
		   int which_widget_intr,
		   xwidgetnum_t targ,
		   iopaddr_t addr,
		   xtalk_intr_vector_t vect)
{
    bridge_t               *bridge = (bridge_t *) which_widget;

    if (which_widget_intr == -1) {
	/* bridge widget error interrupt */
	bridge->b_wid_int_upper = ( (0x000F0000 & (targ << 16)) |
				   XTALK_ADDR_TO_UPPER(addr));
	bridge->b_wid_int_lower = XTALK_ADDR_TO_LOWER(addr);
	bridge->b_int_host_err = vect;

	/* turn on all interrupts except
	 * the PCI interrupt requests,
	 * at least at heart.
	 */
	bridge->b_int_enable |= ~BRIDGE_IMR_INT_MSK;

    } else {
	/* routing a PCI device interrupt.
	 * targ and low 38 bits of addr must
	 * be the same as the already set
	 * value for the widget error interrupt.
	 */
	bridge->b_int_addr[which_widget_intr].addr =
	    ((BRIDGE_INT_ADDR_HOST & (addr >> 30)) |
	     (BRIDGE_INT_ADDR_FLD & vect));
	/*
	 * now bridge can let it through;
	 * NB: still should be blocked at
	 * xtalk provider end, until the service
	 * function is set.
	 */
	bridge->b_int_enable |= 1 << vect;
    }
    bridge->b_wid_tflush;		/* wait until Bridge PIO complete */
}


/*
 * pcibr_intr_func()
 *
 * This is the pcibr interrupt "wrapper" function that is called,
 * in interrupt context, to initiate the interrupt handler(s) registered
 * (via pcibr_intr_alloc/connect) for the occuring interrupt. Non-threaded 
 * handlers will be called directly, and threaded handlers will have their 
 * thread woken up.
 */
void
pcibr_intr_func(intr_arg_t arg)
{
    pcibr_intr_wrap_t       wrap = (pcibr_intr_wrap_t) arg;
    reg_p                   wrbf;
    pcibr_intr_t            intr;
    pcibr_intr_list_t       list;
    int                     clearit;
    int			    do_nonthreaded = 1;
    int			    is_threaded = 0;
    int			    x = 0;

	/*
	 * If any handler is still running from a previous interrupt
	 * just return. If there's a need to call the handler(s) again,
	 * another interrupt will be generated either by the device or by
	 * pcibr_force_interrupt().
	 */

	if (wrap->iw_hdlrcnt) {
		return;
	}

    /*
     * Call all interrupt handlers registered.
     * First, the pcibr_intrd threads for any threaded handlers will be
     * awoken, then any non-threaded handlers will be called sequentially.
     */
	
	clearit = 1;
	while (do_nonthreaded) {
	    for (list = wrap->iw_list; list != NULL; list = list->il_next) {
		if ((intr = list->il_intr) &&
		    (intr->bi_flags & PCIIO_INTR_CONNECTED)) {

		/*
		 * This device may have initiated write
		 * requests since the bridge last saw
		 * an edge on this interrupt input; flushing
		 * the buffer prior to invoking the handler
		 * should help but may not be sufficient if we 
		 * get more requests after the flush, followed
		 * by the card deciding it wants service, before
		 * the interrupt handler checks to see if things need
		 * to be done.
		 *
		 * There is a similar race condition if
		 * an interrupt handler loops around and
		 * notices further service is required.
		 * Perhaps we need to have an explicit
		 * call that interrupt handlers need to
		 * do between noticing that DMA to memory
		 * has completed, but before observing the
		 * contents of memory?
		 */

			if ((do_nonthreaded) && (!is_threaded)) {
			/* Non-threaded. 
			 * Call the interrupt handler at interrupt level
			 */

			/* Only need to flush write buffers if sharing */

			if ((wrap->iw_shared) && (wrbf = list->il_wrbf)) {
			    if ((x = *wrbf))	/* write request buffer flush */
#ifdef SUPPORT_PRINTING_V_FORMAT
				printk(KERN_ALERT  "pcibr_intr_func %v: \n"
				    "write buffer flush failed, wrbf=0x%x\n", 
				    list->il_intr->bi_dev, wrbf);
#else
				printk(KERN_ALERT  "pcibr_intr_func %p: \n"
				    "write buffer flush failed, wrbf=0x%lx\n", 
				    (void *)list->il_intr->bi_dev, (long) wrbf);
#endif
			}
		    }

		    clearit = 0;
		}
	    }

		do_nonthreaded = 0;
		/*
		 * If the non-threaded handler was the last to complete,
		 * (i.e., no threaded handlers still running) force an
		 * interrupt to avoid a potential deadlock situation.
		 */
		if (wrap->iw_hdlrcnt == 0) {
			pcibr_force_interrupt(wrap);
		}
	}

	/* If there were no handlers,
	 * disable the interrupt and return.
	 * It will get enabled again after
	 * a handler is connected.
	 * If we don't do this, we would
	 * sit here and spin through the
	 * list forever.
	 */
	if (clearit) {
	    pcibr_soft_t            pcibr_soft = wrap->iw_soft;
	    bridge_t               *bridge = pcibr_soft->bs_base;
	    bridgereg_t             b_int_enable;
	    bridgereg_t		    mask = 1 << wrap->iw_intr;
	    unsigned long           s;

	    s = pcibr_lock(pcibr_soft);
	    b_int_enable = bridge->b_int_enable;
	    b_int_enable &= ~mask;
	    bridge->b_int_enable = b_int_enable;
	    bridge->b_wid_tflush;	/* wait until Bridge PIO complete */
	    pcibr_unlock(pcibr_soft, s);
	    return;
	}
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

#ifdef LITTLE_ENDIAN
/*
 * on sn-ia we need to twiddle the the addresses going out
 * the pci bus because we use the unswizzled synergy space
 * (the alternative is to use the swizzled synergy space
 * and byte swap the data)
 */
#define	CB(b,r)	(((volatile uint8_t *) b)[((r)^4)])
#define	CS(b,r)	(((volatile uint16_t *) b)[((r^4)/2)])
#define	CW(b,r)	(((volatile uint32_t *) b)[((r^4)/4)])
#else
#define	CB(b,r)	(((volatile uint8_t *) cfgbase)[(r)^3])
#define	CS(b,r)	(((volatile uint16_t *) cfgbase)[((r)/2)^1])
#define	CW(b,r)	(((volatile uint32_t *) cfgbase)[(r)/4])
#endif /* LITTLE_ENDIAN */


LOCAL                   cfg_p
pcibr_config_addr(devfs_handle_t conn,
		  unsigned reg)
{
    pcibr_info_t            pcibr_info;
    pciio_slot_t            pciio_slot;
    pciio_function_t        pciio_func;
    pcibr_soft_t            pcibr_soft;
    bridge_t               *bridge;
    cfg_p                   cfgbase = (cfg_p)0;

    pcibr_info = pcibr_info_get(conn);

    pciio_slot = pcibr_info->f_slot;
    if (pciio_slot == PCIIO_SLOT_NONE)
	pciio_slot = PCI_TYPE1_SLOT(reg);

    pciio_func = pcibr_info->f_func;
    if (pciio_func == PCIIO_FUNC_NONE)
	pciio_func = PCI_TYPE1_FUNC(reg);

    pcibr_soft = (pcibr_soft_t) pcibr_info->f_mfast;

    bridge = pcibr_soft->bs_base;

    cfgbase = bridge->b_type0_cfg_dev[pciio_slot].f[pciio_func].l;

    return cfgbase;
}

uint64_t
pcibr_config_get(devfs_handle_t conn,
		 unsigned reg,
		 unsigned size)
{
    return do_pcibr_config_get(pcibr_config_addr(conn, reg),
			       PCI_TYPE1_REG(reg), size);
}

LOCAL uint64_t
do_pcibr_config_get(
		       cfg_p cfgbase,
		       unsigned reg,
		       unsigned size)
{
    unsigned                value;

   
    value = CW(cfgbase, reg);

    if (reg & 3)
	value >>= 8 * (reg & 3);
    if (size < 4)
	value &= (1 << (8 * size)) - 1;

    return value;
}

void
pcibr_config_set(devfs_handle_t conn,
		 unsigned reg,
		 unsigned size,
		 uint64_t value)
{
    do_pcibr_config_set(pcibr_config_addr(conn, reg),
			PCI_TYPE1_REG(reg), size, value);
}

LOCAL void
do_pcibr_config_set(cfg_p cfgbase,
		    unsigned reg,
		    unsigned size,
		    uint64_t value)
{
    switch (size) {
    case 1:
	CB(cfgbase, reg) = value;
	break;
    case 2:
	if (reg & 1) {
	    CB(cfgbase, reg) = value;
	    CB(cfgbase, reg + 1) = value >> 8;
	} else
	    CS(cfgbase, reg) = value;
	break;
    case 3:
	if (reg & 1) {
	    CB(cfgbase, reg) = value;
	    CS(cfgbase, (reg + 1)) = value >> 8;
	} else {
	    CS(cfgbase, reg) = value;
	    CB(cfgbase, reg + 2) = value >> 16;
	}
	break;

    case 4:
	CW(cfgbase, reg) = value;
	break;
    }
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

LOCAL                   pcibr_hints_t
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


#ifdef LATER

#include <sys/idbg.h>
#include <sys/idbgentry.h>

char *pci_space[] = {"NONE", 
		     "ROM",
		     "IO",
		     "",
		     "MEM",
		     "MEM32",
		     "MEM64",
		     "CFG",
		     "WIN0",
		     "WIN1",
		     "WIN2",
		     "WIN3",
		     "WIN4",
		     "WIN5",
		     "",
		     "BAD"};

void
idbg_pss_func(pcibr_info_h pcibr_infoh, int func)
{
    pcibr_info_t	pcibr_info = pcibr_infoh[func];
    char		name[MAXDEVNAME];
    int			win;
    
    if (!pcibr_info)
	return;
    qprintf("Per-slot Function Info\n");
#ifdef SUPPORT_PRINTING_V_FORMAT
    sprintf(name, "%v", pcibr_info->f_vertex);
#endif
    qprintf("\tSlot Name : %s\n",name);
    qprintf("\tPCI Bus : %d ",pcibr_info->f_bus);
    qprintf("Slot : %d ", pcibr_info->f_slot);
    qprintf("Function : %d ", pcibr_info->f_func);
    qprintf("VendorId : 0x%x " , pcibr_info->f_vendor);
    qprintf("DeviceId : 0x%x\n", pcibr_info->f_device);
#ifdef SUPPORT_PRINTING_V_FORMAT
    sprintf(name, "%v", pcibr_info->f_master);
#endif
    qprintf("\tBus provider : %s\n",name);
    qprintf("\tProvider Fns : 0x%x ", pcibr_info->f_pops);
    qprintf("Error Handler : 0x%x Arg 0x%x\n", 
	    pcibr_info->f_efunc,pcibr_info->f_einfo);
    for(win = 0 ; win < 6 ; win++) 
	qprintf("\tBase Reg #%d space %s base 0x%x size 0x%x\n",
		win,pci_space[pcibr_info->f_window[win].w_space],
		pcibr_info->f_window[win].w_base,
		pcibr_info->f_window[win].w_size);

    qprintf("\tRom base 0x%x size 0x%x\n", 
	    pcibr_info->f_rbase,pcibr_info->f_rsize);

    qprintf("\tInterrupt Bit Map\n");
    qprintf("\t\tPCI Int#\tBridge Pin#\n");
    for (win = 0 ; win < 4; win++)
	qprintf("\t\tINT%c\t\t%d\n",win+'A',pcibr_info->f_ibit[win]);
    qprintf("\n");
}


void
idbg_pss_info(pcibr_soft_t pcibr_soft, pciio_slot_t slot)
{
    pcibr_soft_slot_t	pss;
    char		slot_conn_name[MAXDEVNAME];
    int			func;

    pss = &pcibr_soft->bs_slot[slot];
    qprintf("PCI INFRASTRUCTURAL INFO FOR SLOT %d\n", slot);
    qprintf("\tHost Present ? %s ", pss->has_host ? "yes" : "no");
    qprintf("\tHost Slot : %d\n",pss->host_slot);
    sprintf(slot_conn_name, "%v", pss->slot_conn);
    qprintf("\tSlot Conn : %s\n",slot_conn_name);	
    qprintf("\t#Functions : %d\n",pss->bss_ninfo);
    for (func = 0; func < pss->bss_ninfo; func++)
	idbg_pss_func(pss->bss_infos,func);
    qprintf("\tSpace : %s ",pci_space[pss->bss_devio.bssd_space]);
    qprintf("\tBase : 0x%x ", pss->bss_devio.bssd_base);
    qprintf("\tShadow Devreg : 0x%x\n", pss->bss_device);
    qprintf("\tUsage counts : pmu %d d32 %d d64 %d\n",
	    pss->bss_pmu_uctr,pss->bss_d32_uctr,pss->bss_d64_uctr);
    
    qprintf("\tDirect Trans Info : d64_base 0x%x d64_flags 0x%x"
	    "d32_base 0x%x d32_flags 0x%x\n",
	    pss->bss_d64_base, pss->bss_d64_flags,
	    pss->bss_d32_base, pss->bss_d32_flags);
    
    qprintf("\tExt ATEs active ? %s", 
	    atomic_read(&pss->bss_ext_ates_active) ? "yes" : "no");
    qprintf(" Command register : 0x%x ", pss->bss_cmd_pointer);
    qprintf(" Shadow command val : 0x%x\n", pss->bss_cmd_shadow);

    qprintf("\tRRB Info : Valid %d+%d Reserved %d\n",
	    pcibr_soft->bs_rrb_valid[slot],
	    pcibr_soft->bs_rrb_valid[slot + PCIBR_RRB_SLOT_VIRTUAL],
	    pcibr_soft->bs_rrb_res[slot]);
		
}

int	ips = 0;

void
idbg_pss(pcibr_soft_t pcibr_soft)
{
    pciio_slot_t	slot;

    
    if (ips >= 0 && ips < 8)
	idbg_pss_info(pcibr_soft,ips);
    else if (ips < 0)
	for (slot = 0; slot < 8; slot++) 
	    idbg_pss_info(pcibr_soft,slot);
    else
	qprintf("Invalid ips %d\n",ips);
}

#endif /* LATER */

int
pcibr_dma_enabled(devfs_handle_t pconn_vhdl)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);
	

    return xtalk_dma_enabled(pcibr_soft->bs_conn);
}
