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

extern int	hubii_check_widget_disabled(nasid_t, int);

/* =====================================================================
 *    ERROR HANDLING
 */

#ifdef	DEBUG
#ifdef	ERROR_DEBUG
#define BRIDGE_PIOERR_TIMEOUT	100	/* Timeout with ERROR_DEBUG defined */
#else
#define BRIDGE_PIOERR_TIMEOUT	40	/* Timeout in debug mode  */
#endif
#else
#define BRIDGE_PIOERR_TIMEOUT	1	/* Timeout in non-debug mode                            */
#endif

#ifdef  DEBUG
#ifdef ERROR_DEBUG
bridgereg_t bridge_errors_to_dump = ~BRIDGE_ISR_INT_MSK;
#else
bridgereg_t bridge_errors_to_dump = BRIDGE_ISR_ERROR_DUMP;
#endif
#else
bridgereg_t bridge_errors_to_dump = BRIDGE_ISR_ERROR_FATAL |
                                BRIDGE_ISR_PCIBUS_PIOERR;
#endif

#if defined (PCIBR_LLP_CONTROL_WAR)
int                     pcibr_llp_control_war_cnt;
#endif				/* PCIBR_LLP_CONTROL_WAR */

/* FIXME: can these arrays be local ? */

#ifdef LATER

struct reg_values xio_cmd_pactyp[] =
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

struct reg_desc   xio_cmd_bits[] =
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

#define F(s,n)          { 1l<<(s),-(s), n }

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
#define	device_desc	device_bits
struct reg_desc   device_bits[] =
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

#endif	/* LATER */

void
print_bridge_errcmd(uint32_t cmdword, char *errtype)
{
    printk(
	    "\t    Bridge %s Error Command Word Register %R\n",
	    errtype, cmdword, xio_cmd_bits);
}

char             *pcibr_isr_errs[] =
{
    "", "", "", "", "", "", "", "",
    "08: GIO non-contiguous byte enable in crosstalk packet",
    "09: PCI to Crosstalk read request timeout",
    "10: PCI retry operation count exhausted.",
    "11: PCI bus device select timeout",
    "12: PCI device reported parity error",
    "13: PCI Address/Cmd parity error ",
    "14: PCI Bridge detected parity error",
    "15: PCI abort condition",
    "16: SSRAM parity error",
    "17: LLP Transmitter Retry count wrapped",
    "18: LLP Transmitter side required Retry",
    "19: LLP Receiver retry count wrapped",
    "20: LLP Receiver check bit error",
    "21: LLP Receiver sequence number error",
    "22: Request packet overflow",
    "23: Request operation not supported by bridge",
    "24: Request packet has invalid address for bridge widget",
    "25: Incoming request xtalk command word error bit set or invalid sideband",
    "26: Incoming response xtalk command word error bit set or invalid sideband",
    "27: Framing error, request cmd data size does not match actual",
    "28: Framing error, response cmd data size does not match actual",
    "29: Unexpected response arrived",
    "30: PMU Access Fault",
    "31: Multiple errors occurred",
};

#define BEM_ADD_STR(s)  printk("%s", (s))
#define BEM_ADD_VAR(v)  printk("\t%20s: 0x%x\n", #v, (v))
#define BEM_ADD_REG(r)  printk("\t%20s: %R\n", #r, (r), r ## _desc)
#define BEM_ADD_NSPC(n,s)       printk("\t%20s: %R\n", n, s, space_desc)
#define BEM_ADD_SPC(s)          BEM_ADD_NSPC(#s, s)

/*
 * display memory directory state
 */
void
pcibr_show_dir_state(paddr_t paddr, char *prefix)
{
	int state;
	uint64_t vec_ptr;
	hubreg_t elo;
	extern char *dir_state_str[];
	extern void get_dir_ent(paddr_t, int *, uint64_t *, hubreg_t *);

	get_dir_ent(paddr, &state, &vec_ptr, &elo);

	printf("%saddr 0x%x: state 0x%x owner 0x%x (%s)\n", 
		prefix, paddr, state, vec_ptr, dir_state_str[state]);
}


/*
 *	Dump relevant error information for Bridge error interrupts.
 */
/*ARGSUSED */
void
pcibr_error_dump(pcibr_soft_t pcibr_soft)
{
    bridge_t               *bridge = pcibr_soft->bs_base;
    bridgereg_t             int_status;
    bridgereg_t             mult_int;
    int			    bit;
    int                     i;
    char		    *reg_desc;
    paddr_t		    addr;

    int_status = (bridge->b_int_status & ~BRIDGE_ISR_INT_MSK);
    if (!int_status) {
	/* No error bits set */
	return;
    }

    /* Check if dumping the same error information multiple times */
    if (test_and_set_int((int *) &pcibr_soft->bs_errinfo.bserr_intstat,
	    int_status) == int_status) {
	return;
    }

    printk(KERN_ALERT "PCI BRIDGE ERROR: int_status is 0x%X for %s\n"
	"    Dumping relevant %sBridge registers for each bit set...\n",
	    int_status, pcibr_soft->bs_name,
	    (is_xbridge(bridge) ? "X" : ""));

    for (i = PCIBR_ISR_ERR_START; i < PCIBR_ISR_MAX_ERRS; i++) {
	bit = 1 << i;

	/*
	 * A number of int_status bits are only defined for Bridge.
	 * Ignore them in the case of an XBridge.
	 */
	if (is_xbridge(bridge) && ((bit == BRIDGE_ISR_MULTI_ERR) ||
		(bit == BRIDGE_ISR_SSRAM_PERR) ||
		(bit == BRIDGE_ISR_GIO_B_ENBL_ERR))) {
	    continue;
	}

	if (int_status & bit) {
	    printk("\t%s\n", pcibr_isr_errs[i]);

	    switch (bit) {
	    case BRIDGE_ISR_PAGE_FAULT:		/* PMU_PAGE_FAULT  (XBridge) */
/*	    case BRIDGE_ISR_PMU_ESIZE_FAULT:	   PMU_ESIZE_FAULT (Bridge) */
		if (is_xbridge(bridge))
		    reg_desc = "Map Fault Address";
		else
		    reg_desc = "SSRAM Parity Error";

		printk("\t    %s Register: 0x%x\n", reg_desc,
		    bridge->b_ram_perr_or_map_fault);
		break;

	    case BRIDGE_ISR_UNEXP_RESP:		/* UNEXPECTED_RESP */
		print_bridge_errcmd(bridge->b_wid_aux_err, "Aux");
		break;

	    case BRIDGE_ISR_BAD_XRESP_PKT:		/* BAD_RESP_PACKET */
	    case BRIDGE_ISR_RESP_XTLK_ERR:		/* RESP_XTALK_ERROR */
	    case BRIDGE_ISR_XREAD_REQ_TIMEOUT:		/* XREAD_REQ_TOUT */

		addr = (((uint64_t) (bridge->b_wid_resp_upper & 0xFFFF) << 32)
			| bridge->b_wid_resp_lower);
		printk(
		    "\t    Bridge Response Buffer Error Upper Address Register: 0x%x\n"
		    "\t    Bridge Response Buffer Error Lower Address Register: 0x%x\n"
		    "\t    dev-num %d buff-num %d addr 0x%x\n",
		    bridge->b_wid_resp_upper, bridge->b_wid_resp_lower,
		    ((bridge->b_wid_resp_upper >> 20) & 0x3),
		    ((bridge->b_wid_resp_upper >> 16) & 0xF),
		    addr);
		if (bit == BRIDGE_ISR_RESP_XTLK_ERR) {
			/* display memory directory associated with cacheline */
			pcibr_show_dir_state(addr, "\t    ");
		}
		break;

	    case BRIDGE_ISR_BAD_XREQ_PKT:		/* BAD_XREQ_PACKET */
	    case BRIDGE_ISR_REQ_XTLK_ERR:		/* REQ_XTALK_ERROR */
	    case BRIDGE_ISR_INVLD_ADDR:			/* INVALID_ADDRESS */
	    case BRIDGE_ISR_UNSUPPORTED_XOP:		/* UNSUPPORTED_XOP */
		print_bridge_errcmd(bridge->b_wid_aux_err, "");
		printk("\t    Bridge Error Upper Address Register: 0x%x\n"
		    "\t    Bridge Error Lower Address Register: 0x%x\n"
		    "\t    Bridge Error Address: 0x%x\n",
		    (uint64_t) bridge->b_wid_err_upper,
		    (uint64_t) bridge->b_wid_err_lower,
		    (((uint64_t) bridge->b_wid_err_upper << 32) |
		    bridge->b_wid_err_lower));
		break;

	    case BRIDGE_ISR_SSRAM_PERR:			/* SSRAM_PERR */
		if (!is_xbridge(bridge)) {	/* only defined on Bridge */
		    printk(
			"\t    Bridge SSRAM Parity Error Register: 0x%x\n",
			bridge->b_ram_perr);
		}
		break;

	    case BRIDGE_ISR_PCI_ABORT:			/* PCI_ABORT */
	    case BRIDGE_ISR_PCI_PARITY:			/* PCI_PARITY */
	    case BRIDGE_ISR_PCI_SERR:			/* PCI_SERR */
	    case BRIDGE_ISR_PCI_PERR:			/* PCI_PERR */
	    case BRIDGE_ISR_PCI_MST_TIMEOUT:		/* PCI_MASTER_TOUT */
	    case BRIDGE_ISR_PCI_RETRY_CNT:		/* PCI_RETRY_CNT */
	    case BRIDGE_ISR_GIO_B_ENBL_ERR:		/* GIO BENABLE_ERR */
		printk("\t    PCI Error Upper Address Register: 0x%x\n"
		    "\t    PCI Error Lower Address Register: 0x%x\n"
		    "\t    PCI Error Address: 0x%x\n",
		    (uint64_t) bridge->b_pci_err_upper,
		    (uint64_t) bridge->b_pci_err_lower,
		    (((uint64_t) bridge->b_pci_err_upper << 32) |
		    bridge->b_pci_err_lower));
		break;
	    }
	}
    }

    if (is_xbridge(bridge) && (bridge->b_mult_int & ~BRIDGE_ISR_INT_MSK)) {
	mult_int = bridge->b_mult_int;
	printk("    XBridge Multiple Interrupt Register is 0x%x\n",
		mult_int);
	for (i = PCIBR_ISR_ERR_START; i < PCIBR_ISR_MAX_ERRS; i++) {
	    if (mult_int & (1 << i))
		printk("\t%s\n", pcibr_isr_errs[i]);
	    }
	}
}

#define PCIBR_ERRINTR_GROUP(error)	\
		(( error & (BRIDGE_IRR_PCI_GRP|BRIDGE_IRR_GIO_GRP)

uint32_t
pcibr_errintr_group(uint32_t error)
{
    uint32_t              group = BRIDGE_IRR_MULTI_CLR;

    if (error & BRIDGE_IRR_PCI_GRP)
	group |= BRIDGE_IRR_PCI_GRP_CLR;
    if (error & BRIDGE_IRR_SSRAM_GRP)
	group |= BRIDGE_IRR_SSRAM_GRP_CLR;
    if (error & BRIDGE_IRR_LLP_GRP)
	group |= BRIDGE_IRR_LLP_GRP_CLR;
    if (error & BRIDGE_IRR_REQ_DSP_GRP)
	group |= BRIDGE_IRR_REQ_DSP_GRP_CLR;
    if (error & BRIDGE_IRR_RESP_BUF_GRP)
	group |= BRIDGE_IRR_RESP_BUF_GRP_CLR;
    if (error & BRIDGE_IRR_CRP_GRP)
	group |= BRIDGE_IRR_CRP_GRP_CLR;

    return group;

}


/* pcibr_pioerr_check():
 *	Check to see if this pcibr has a PCI PIO
 *	TIMEOUT error; if so, bump the timeout-count
 *	on any piomaps that could cover the address.
 */
static void
pcibr_pioerr_check(pcibr_soft_t soft)
{
    bridge_t		   *bridge;
    bridgereg_t		    b_int_status;
    bridgereg_t		    b_pci_err_lower;
    bridgereg_t		    b_pci_err_upper;
    iopaddr_t		    pci_addr;
    pciio_slot_t	    slot;
    pcibr_piomap_t	    map;
    iopaddr_t		    base;
    size_t		    size;
    unsigned		    win;
    int			    func;

    bridge = soft->bs_base;
    b_int_status = bridge->b_int_status;
    if (b_int_status & BRIDGE_ISR_PCIBUS_PIOERR) {
	b_pci_err_lower = bridge->b_pci_err_lower;
	b_pci_err_upper = bridge->b_pci_err_upper;
	b_int_status = bridge->b_int_status;
	if (b_int_status & BRIDGE_ISR_PCIBUS_PIOERR) {

	    pci_addr = b_pci_err_upper & BRIDGE_ERRUPPR_ADDRMASK;
	    pci_addr = (pci_addr << 32) | b_pci_err_lower;

	    slot = 8;
	    while (slot-- > 0) {
		int 		nfunc = soft->bs_slot[slot].bss_ninfo;
		pcibr_info_h	pcibr_infoh = soft->bs_slot[slot].bss_infos;

		for (func = 0; func < nfunc; func++) {
		    pcibr_info_t 	pcibr_info = pcibr_infoh[func];

		    if (!pcibr_info)
			continue;

		    for (map = pcibr_info->f_piomap;
			 map != NULL; map = map->bp_next) {
			base = map->bp_pciaddr;
			size = map->bp_mapsz;
			win = map->bp_space - PCIIO_SPACE_WIN(0);
			if (win < 6)
			    base += 
				soft->bs_slot[slot].bss_window[win].bssw_base;
			else if (map->bp_space == PCIIO_SPACE_ROM)
			    base += pcibr_info->f_rbase;
			if ((pci_addr >= base) && (pci_addr < (base + size)))
			    atomicAddInt(map->bp_toc, 1);
		    }
		}
	    }
	}
    }
}

/*
 * PCI Bridge Error interrupt handler.
 *      This gets invoked, whenever a PCI bridge sends an error interrupt.
 *      Primarily this servers two purposes.
 *              - If an error can be handled (typically a PIO read/write
 *                error, we try to do it silently.
 *              - If an error cannot be handled, we die violently.
 *      Interrupt due to PIO errors:
 *              - Bridge sends an interrupt, whenever a PCI operation
 *                done by the bridge as the master fails. Operations could
 *                be either a PIO read or a PIO write.
 *                PIO Read operation also triggers a bus error, and it's
 *                We primarily ignore this interrupt in that context..
 *                For PIO write errors, this is the only indication.
 *                and we have to handle with the info from here.
 *
 *                So, there is no way to distinguish if an interrupt is
 *                due to read or write error!.
 */


void
pcibr_error_intr_handler(intr_arg_t arg)
{
    pcibr_soft_t            pcibr_soft;
    bridge_t               *bridge;
    bridgereg_t             int_status;
    bridgereg_t             err_status;
    int                     i;

    /* REFERENCED */
    bridgereg_t             disable_errintr_mask = 0;
    int 		    rv;
    int 		    error_code = IOECODE_DMA | IOECODE_READ;
    ioerror_mode_t 	    mode = MODE_DEVERROR;
    ioerror_t 	            ioe;
    nasid_t		    nasid;

#if PCIBR_SOFT_LIST
    {
	extern pcibr_list_p	pcibr_list;
	pcibr_list_p            entry;

	entry = pcibr_list;
	while (1) {
	    if (entry == NULL) {
		PRINT_PANIC(
			"pcibr_error_intr_handler:\n"
			"\tmy parameter (0x%x) is not a pcibr_soft!",
			arg);
	    }
	    if ((intr_arg_t) entry->bl_soft == arg)
		break;
	    entry = entry->bl_next;
	}
    }
#endif
    pcibr_soft = (pcibr_soft_t) arg;
    bridge = pcibr_soft->bs_base;

    /*
     * pcibr_error_intr_handler gets invoked whenever bridge encounters
     * an error situation, and the interrupt for that error is enabled.
     * This routine decides if the error is fatal or not, and takes
     * action accordingly.
     *
     * In the case of PIO read/write timeouts, there is no way
     * to know if it was a read or write request that timed out.
     * If the error was due to a "read", a bus error will also occur
     * and the bus error handling code takes care of it. 
     * If the error is due to a "write", the error is currently logged 
     * by this routine. For SN1 and SN0, if fire-and-forget mode is 
     * disabled, a write error response xtalk packet will be sent to 
     * the II, which will cause an II error interrupt. No write error 
     * recovery actions of any kind currently take place at the pcibr 
     * layer! (e.g., no panic on unrecovered write error)
     *
     * Prior to reading the Bridge int_status register we need to ensure
     * that there are no error bits set in the lower layers (hubii)
     * that have disabled PIO access to the widget. If so, there is nothing
     * we can do until the bits clear, so we setup a timeout and try again
     * later.
     */

    nasid = NASID_GET(bridge);
    if (hubii_check_widget_disabled(nasid, pcibr_soft->bs_xid)) {
	timeout(pcibr_error_intr_handler, pcibr_soft, BRIDGE_PIOERR_TIMEOUT);
	pcibr_soft->bs_errinfo.bserr_toutcnt++;
	return;
    }

    /* int_status is which bits we have to clear;
     * err_status is the bits we haven't handled yet.
     */

    int_status = bridge->b_int_status &  ~BRIDGE_ISR_INT_MSK;
    err_status = int_status & ~BRIDGE_ISR_MULTI_ERR;

    if (!(int_status & ~BRIDGE_ISR_INT_MSK)) {
	/*
	 * No error bit set!!.
	 */
	return;
    }
    /*
     * If we have a PCIBUS_PIOERR, hand it to the logger.
     */
    if (int_status & BRIDGE_ISR_PCIBUS_PIOERR) {
	pcibr_pioerr_check(pcibr_soft);
    }

    if (err_status) {
	struct bs_errintr_stat_s *bs_estat = pcibr_soft->bs_errintr_stat;

	for (i = PCIBR_ISR_ERR_START; i < PCIBR_ISR_MAX_ERRS; i++, bs_estat++) {
	    if (err_status & (1 << i)) {
		uint32_t              errrate = 0;
		uint32_t              errcount = 0;
		uint32_t              errinterval = 0, current_tick = 0;
		int                     llp_tx_retry_errors = 0;
		int                     is_llp_tx_retry_intr = 0;

		bs_estat->bs_errcount_total++;

		current_tick = lbolt;
		errinterval = (current_tick - bs_estat->bs_lasterr_timestamp);
		errcount = (bs_estat->bs_errcount_total -
			    bs_estat->bs_lasterr_snapshot);

		is_llp_tx_retry_intr = (BRIDGE_ISR_LLP_TX_RETRY == (1 << i));

		/* Check for the divide by zero condition while
		 * calculating the error rates.
		 */

		if (errinterval) {
		    errrate = errcount / errinterval;
		    /* If able to calculate error rate
		     * on a LLP transmitter retry interrupt, check
		     * if the error rate is nonzero and we have seen
		     * a certain minimum number of errors.
		     *
		     * NOTE : errcount is being compared to
		     * PCIBR_ERRTIME_THRESHOLD to make sure that we are not
		     * seeing cases like x error interrupts per y ticks for
		     * very low x ,y (x > y ) which could result in a
		     * rate > 100/tick.
		     */
		    if (is_llp_tx_retry_intr &&
			errrate &&
			(errcount >= PCIBR_ERRTIME_THRESHOLD)) {
			llp_tx_retry_errors = 1;
		    }
		} else {
		    errrate = 0;
		    /* Since we are not able to calculate the
		     * error rate check if we exceeded a certain
		     * minimum number of errors for LLP transmitter
		     * retries. Note that this can only happen
		     * within the first tick after the last snapshot.
		     */
		    if (is_llp_tx_retry_intr &&
			(errcount >= PCIBR_ERRINTR_DISABLE_LEVEL)) {
			llp_tx_retry_errors = 1;
		    }
		}

		/*
		 * If a non-zero error rate (which is equivalent to
		 * to 100 errors/tick at least) for the LLP transmitter
		 * retry interrupt was seen, check if we should print
		 * a warning message.
		 */

		if (llp_tx_retry_errors) {
		    static uint32_t       last_printed_rate;

		    if (errrate > last_printed_rate) {
			last_printed_rate = errrate;
			/* Print the warning only if the error rate
			 * for the transmitter retry interrupt
			 * exceeded the previously printed rate.
			 */
			printk(KERN_WARNING
				"%s: %s, Excessive error interrupts : %d/tick\n",
				pcibr_soft->bs_name,
				pcibr_isr_errs[i],
				errrate);

		    }
		    /*
		     * Update snapshot, and time
		     */
		    bs_estat->bs_lasterr_timestamp = current_tick;
		    bs_estat->bs_lasterr_snapshot =
			bs_estat->bs_errcount_total;

		}
		/*
		 * If the error rate is high enough, print the error rate.
		 */
		if (errinterval > PCIBR_ERRTIME_THRESHOLD) {

		    if (errrate > PCIBR_ERRRATE_THRESHOLD) {
			printk(KERN_NOTICE "%s: %s, Error rate %d/tick",
				pcibr_soft->bs_name,
				pcibr_isr_errs[i],
				errrate);
			/*
			 * Update snapshot, and time
			 */
			bs_estat->bs_lasterr_timestamp = current_tick;
			bs_estat->bs_lasterr_snapshot =
			    bs_estat->bs_errcount_total;
		    }
		}
		if (bs_estat->bs_errcount_total > PCIBR_ERRINTR_DISABLE_LEVEL) {
		    /*
		     * We have seen a fairly large number of errors of
		     * this type. Let's disable the interrupt. But flash
		     * a message about the interrupt being disabled.
		     */
		    printk(KERN_NOTICE
			    "%s Disabling error interrupt type %s. Error count %d",
			    pcibr_soft->bs_name,
			    pcibr_isr_errs[i],
			    bs_estat->bs_errcount_total);
		    disable_errintr_mask |= (1 << i);
		}
	    }
	}
    }

    if (disable_errintr_mask) {
	/*
	 * Disable some high frequency errors as they
	 * could eat up too much cpu time.
	 */
	bridge->b_int_enable &= ~disable_errintr_mask;
    }
    /*
     * If we leave the PROM cacheable, T5 might
     * try to do a cache line sized writeback to it,
     * which will cause a BRIDGE_ISR_INVLD_ADDR.
     */
    if ((err_status & BRIDGE_ISR_INVLD_ADDR) &&
	(0x00000000 == bridge->b_wid_err_upper) &&
	(0x00C00000 == (0xFFC00000 & bridge->b_wid_err_lower)) &&
	(0x00402000 == (0x00F07F00 & bridge->b_wid_err_cmdword))) {
	err_status &= ~BRIDGE_ISR_INVLD_ADDR;
    }
#if defined (PCIBR_LLP_CONTROL_WAR)
    /*
     * The bridge bug, where the llp_config or control registers
     * need to be read back after being written, affects an MP
     * system since there could be small windows between writing
     * the register and reading it back on one cpu while another
     * cpu is fielding an interrupt. If we run into this scenario,
     * workaround the problem by ignoring the error. (bug 454474)
     * pcibr_llp_control_war_cnt keeps an approximate number of
     * times we saw this problem on a system.
     */

    if ((err_status & BRIDGE_ISR_INVLD_ADDR) &&
	((((uint64_t) bridge->b_wid_err_upper << 32) | (bridge->b_wid_err_lower))
	 == (BRIDGE_INT_RST_STAT & 0xff0))) {
#if 0
	if (kdebug)
	    printk(KERN_NOTICE "%s bridge: ignoring llp/control address interrupt",
		    pcibr_soft->bs_name);
#endif
	pcibr_llp_control_war_cnt++;
	err_status &= ~BRIDGE_ISR_INVLD_ADDR;
    }
#endif				/* PCIBR_LLP_CONTROL_WAR */

#ifdef EHE_ENABLE
    /* Check if this is the RESP_XTALK_ERROR interrupt. 
     * This can happen due to a failed DMA READ operation.
     */
    if (err_status & BRIDGE_ISR_RESP_XTLK_ERR) {
	/* Phase 1 : Look at the error state in the bridge and further
	 * down in the device layers.
	 */
	(void)error_state_set(pcibr_soft->bs_conn, ERROR_STATE_LOOKUP);
	IOERROR_SETVALUE(&ioe, widgetnum, pcibr_soft->bs_xid);
	(void)pcibr_error_handler((error_handler_arg_t)pcibr_soft,
				  error_code,
				  mode,
				  &ioe);
	/* Phase 2 : Perform the action agreed upon in phase 1.
	 */
	(void)error_state_set(pcibr_soft->bs_conn, ERROR_STATE_ACTION);
	rv = pcibr_error_handler((error_handler_arg_t)pcibr_soft,
				 error_code,
				 mode,
				 &ioe);
    }
    if (rv != IOERROR_HANDLED) {
#endif /* EHE_ENABLE */

    /* Dump/Log Bridge error interrupt info */
    if (err_status & bridge_errors_to_dump) {
	printk("BRIDGE ERR_STATUS 0x%x\n", err_status);
	pcibr_error_dump(pcibr_soft);
    }

    if (err_status & BRIDGE_ISR_ERROR_FATAL) {
	machine_error_dump("");
	cmn_err_tag(14, CE_PANIC, "PCI Bridge Error interrupt killed the system");
	    /*NOTREACHED */
    }

#ifdef EHE_ENABLE
    }
#endif

    /*
     * We can't return without re-enabling the interrupt, since
     * it would cause problems for devices like IOC3 (Lost
     * interrupts ?.). So, just cleanup the interrupt, and
     * use saved values later..
     */
    bridge->b_int_rst_stat = pcibr_errintr_group(int_status);

    /* Zero out bserr_intstat field */
    test_and_set_int((int *) &pcibr_soft->bs_errinfo.bserr_intstat, 0);
}

/*
 * pcibr_addr_toslot
 *      Given the 'pciaddr' find out which slot this address is
 *      allocated to, and return the slot number.
 *      While we have the info handy, construct the
 *      function number, space code and offset as well.
 *
 * NOTE: if this routine is called, we don't know whether
 * the address is in CFG, MEM, or I/O space. We have to guess.
 * This will be the case on PIO stores, where the only way
 * we have of getting the address is to check the Bridge, which
 * stores the PCI address but not the space and not the xtalk
 * address (from which we could get it).
 */
int
pcibr_addr_toslot(pcibr_soft_t pcibr_soft,
		  iopaddr_t pciaddr,
		  pciio_space_t *spacep,
		  iopaddr_t *offsetp,
		  pciio_function_t *funcp)
{
    int                     s, f, w;
    iopaddr_t               base;
    size_t                  size;
    pciio_piospace_t        piosp;

    /*
     * Check if the address is in config space
     */

    if ((pciaddr >= BRIDGE_CONFIG_BASE) && (pciaddr < BRIDGE_CONFIG_END)) {

	if (pciaddr >= BRIDGE_CONFIG1_BASE)
	    pciaddr -= BRIDGE_CONFIG1_BASE;
	else
	    pciaddr -= BRIDGE_CONFIG_BASE;

	s = pciaddr / BRIDGE_CONFIG_SLOT_SIZE;
	pciaddr %= BRIDGE_CONFIG_SLOT_SIZE;

	if (funcp) {
	    f = pciaddr / 0x100;
	    pciaddr %= 0x100;
	}
	if (spacep)
	    *spacep = PCIIO_SPACE_CFG;
	if (offsetp)
	    *offsetp = pciaddr;
	if (funcp)
	    *funcp = f;

	return s;
    }
    for (s = 0; s < 8; s++) {
	int                     nf = pcibr_soft->bs_slot[s].bss_ninfo;
	pcibr_info_h            pcibr_infoh = pcibr_soft->bs_slot[s].bss_infos;

	for (f = 0; f < nf; f++) {
	    pcibr_info_t            pcibr_info = pcibr_infoh[f];

	    if (!pcibr_info)
		continue;
	    for (w = 0; w < 6; w++) {
		if (pcibr_info->f_window[w].w_space
		    == PCIIO_SPACE_NONE) {
		    continue;
		}
		base = pcibr_info->f_window[w].w_base;
		size = pcibr_info->f_window[w].w_size;

		if ((pciaddr >= base) && (pciaddr < (base + size))) {
		    if (spacep)
			*spacep = PCIIO_SPACE_WIN(w);
		    if (offsetp)
			*offsetp = pciaddr - base;
		    if (funcp)
			*funcp = f;
		    return s;
		}			/* endif match */
	    }				/* next window */
	}				/* next func */
    }					/* next slot */

    /*
     * Check if the address was allocated as part of the
     * pcibr_piospace_alloc calls.
     */
    for (s = 0; s < 8; s++) {
	int                     nf = pcibr_soft->bs_slot[s].bss_ninfo;
	pcibr_info_h            pcibr_infoh = pcibr_soft->bs_slot[s].bss_infos;

	for (f = 0; f < nf; f++) {
	    pcibr_info_t            pcibr_info = pcibr_infoh[f];

	    if (!pcibr_info)
		continue;
	    piosp = pcibr_info->f_piospace;
	    while (piosp) {
		if ((piosp->start <= pciaddr) &&
		    ((piosp->count + piosp->start) > pciaddr)) {
		    if (spacep)
			*spacep = piosp->space;
		    if (offsetp)
			*offsetp = pciaddr - piosp->start;
		    return s;
		}			/* endif match */
		piosp = piosp->next;
	    }				/* next piosp */
	}				/* next func */
    }					/* next slot */

    /*
     * Some other random address on the PCI bus ...
     * we have no way of knowing whether this was
     * a MEM or I/O access; so, for now, we just
     * assume that the low 1G is MEM, the next
     * 3G is I/O, and anything above the 4G limit
     * is obviously MEM.
     */

    if (spacep)
	*spacep = ((pciaddr < (1ul << 30)) ? PCIIO_SPACE_MEM :
		   (pciaddr < (4ul << 30)) ? PCIIO_SPACE_IO :
		   PCIIO_SPACE_MEM);
    if (offsetp)
	*offsetp = pciaddr;

    return PCIIO_SLOT_NONE;

}

void
pcibr_error_cleanup(pcibr_soft_t pcibr_soft, int error_code)
{
    bridge_t               *bridge = pcibr_soft->bs_base;

    ASSERT(error_code & IOECODE_PIO);
    error_code = error_code;

    bridge->b_int_rst_stat =
	(BRIDGE_IRR_PCI_GRP_CLR | BRIDGE_IRR_MULTI_CLR);
    (void) bridge->b_wid_tflush;	/* flushbus */
}

/*
 * pcibr_error_extract
 *      Given the 'pcibr vertex handle' find out which slot
 *      the bridge status error address (from pcibr_soft info
 *      hanging off the vertex)
 *      allocated to, and return the slot number.
 *      While we have the info handy, construct the
 *      space code and offset as well.
 *
 * NOTE: if this routine is called, we don't know whether
 * the address is in CFG, MEM, or I/O space. We have to guess.
 * This will be the case on PIO stores, where the only way
 * we have of getting the address is to check the Bridge, which
 * stores the PCI address but not the space and not the xtalk
 * address (from which we could get it).
 *
 * XXX- this interface has no way to return the function
 * number on a multifunction card, even though that data
 * is available.
 */

pciio_slot_t
pcibr_error_extract(devfs_handle_t pcibr_vhdl,
		    pciio_space_t *spacep,
		    iopaddr_t *offsetp)
{
    pcibr_soft_t            pcibr_soft = 0;
    iopaddr_t               bserr_addr;
    bridge_t               *bridge;
    pciio_slot_t            slot = PCIIO_SLOT_NONE;
    arbitrary_info_t	    rev;

    /* Do a sanity check as to whether we really got a 
     * bridge vertex handle.
     */
    if (hwgraph_info_get_LBL(pcibr_vhdl, INFO_LBL_PCIBR_ASIC_REV, &rev) !=
	GRAPH_SUCCESS) 
	return(slot);

    pcibr_soft = pcibr_soft_get(pcibr_vhdl);
    if (pcibr_soft) {
	bridge = pcibr_soft->bs_base;
	bserr_addr =
	    bridge->b_pci_err_lower |
	    ((uint64_t) (bridge->b_pci_err_upper &
			   BRIDGE_ERRUPPR_ADDRMASK) << 32);

	slot = pcibr_addr_toslot(pcibr_soft, bserr_addr,
				 spacep, offsetp, NULL);
    }
    return slot;
}

/*ARGSUSED */
void
pcibr_device_disable(pcibr_soft_t pcibr_soft, int devnum)
{
    /*
     * XXX
     * Device failed to handle error. Take steps to
     * disable this device ? HOW TO DO IT ?
     *
     * If there are any Read response buffers associated
     * with this device, it's time to get them back!!
     *
     * We can disassociate any interrupt level associated
     * with this device, and disable that interrupt level
     *
     * For now it's just a place holder
     */
}

/*
 * pcibr_pioerror
 *      Handle PIO error that happened at the bridge pointed by pcibr_soft.
 *
 *      Queries the Bus interface attached to see if the device driver
 *      mapping the device-number that caused error can handle the
 *      situation. If so, it will clean up any error, and return
 *      indicating the error was handled. If the device driver is unable
 *      to handle the error, it expects the bus-interface to disable that
 *      device, and takes any steps needed here to take away any resources
 *      associated with this device.
 */

#define BEM_ADD_STR(s)	printk("%s", (s))
#define BEM_ADD_VAR(v)	printk("\t%20s: 0x%x\n", #v, (v))
#define BEM_ADD_REG(r)	printk("\t%20s: %R\n", #r, (r), r ## _desc)

#define BEM_ADD_NSPC(n,s)	printk("\t%20s: %R\n", n, s, space_desc)
#define BEM_ADD_SPC(s)		BEM_ADD_NSPC(#s, s)

/* BEM_ADD_IOE doesn't dump the whole ioerror, it just
 * decodes the PCI specific portions -- we count on our
 * callers to dump the raw IOE data.
 */
#define BEM_ADD_IOE(ioe)						\
	do {								\
	    if (IOERROR_FIELDVALID(ioe, busspace)) {			\
		unsigned		spc;				\
		unsigned		win;				\
									\
		spc = IOERROR_GETVALUE(ioe, busspace);			\
		win = spc - PCIIO_SPACE_WIN(0);				\
									\
		switch (spc) {						\
		case PCIIO_SPACE_CFG:					\
		    printk(					\
			    "\tPCI Slot %d Func %d CFG space Offset 0x%x\n",	\
	    pciio_widgetdev_slot_get(IOERROR_GETVALUE(ioe, widgetdev)),	\
	    pciio_widgetdev_func_get(IOERROR_GETVALUE(ioe, widgetdev)),	\
			    IOERROR_GETVALUE(ioe, busaddr));		\
		    break;						\
		case PCIIO_SPACE_IO:					\
		    printk(					\
			    "\tPCI I/O space  Offset 0x%x\n",		\
			    IOERROR_GETVALUE(ioe, busaddr));		\
		    break;						\
		case PCIIO_SPACE_MEM:					\
		case PCIIO_SPACE_MEM32:					\
		case PCIIO_SPACE_MEM64:					\
		    printk(					\
			    "\tPCI MEM space Offset 0x%x\n",		\
			    IOERROR_GETVALUE(ioe, busaddr));		\
		    break;						\
		default:						\
		    if (win < 6) {					\
		    printk(					\
			    "\tPCI Slot %d Func %d Window %d Offset 0x%x\n",\
	    pciio_widgetdev_slot_get(IOERROR_GETVALUE(ioe, widgetdev)),	\
	    pciio_widgetdev_func_get(IOERROR_GETVALUE(ioe, widgetdev)),	\
			    win,					\
			    IOERROR_GETVALUE(ioe, busaddr));		\
		    }							\
		    break;						\
		}							\
	    }								\
	} while (0)

/*ARGSUSED */
int
pcibr_pioerror(
		  pcibr_soft_t pcibr_soft,
		  int error_code,
		  ioerror_mode_t mode,
		  ioerror_t *ioe)
{
    int                     retval = IOERROR_HANDLED;

    devfs_handle_t            pcibr_vhdl = pcibr_soft->bs_vhdl;
    bridge_t               *bridge = pcibr_soft->bs_base;

    iopaddr_t               bad_xaddr;

    pciio_space_t           raw_space;	/* raw PCI space */
    iopaddr_t               raw_paddr;	/* raw PCI address */

    pciio_space_t           space;	/* final PCI space */
    pciio_slot_t            slot;	/* final PCI slot, if appropriate */
    pciio_function_t        func;	/* final PCI func, if appropriate */
    iopaddr_t               offset;	/* final PCI offset */
    
    int                     cs, cw, cf;
    pciio_space_t           wx;
    iopaddr_t               wb;
    size_t                  ws;
    iopaddr_t               wl;


    /*
     * We expect to have an "xtalkaddr" coming in,
     * and need to construct the slot/space/offset.
     */

    bad_xaddr = IOERROR_GETVALUE(ioe, xtalkaddr);

    slot = PCIIO_SLOT_NONE;
    func = PCIIO_FUNC_NONE;
    raw_space = PCIIO_SPACE_NONE;
    raw_paddr = 0;

    if ((bad_xaddr >= BRIDGE_TYPE0_CFG_DEV0) &&
	(bad_xaddr < BRIDGE_TYPE1_CFG)) {
	raw_paddr = bad_xaddr - BRIDGE_TYPE0_CFG_DEV0;
	slot = raw_paddr / BRIDGE_TYPE0_CFG_SLOT_OFF;
	raw_paddr = raw_paddr % BRIDGE_TYPE0_CFG_SLOT_OFF;
	raw_space = PCIIO_SPACE_CFG;
    }
    if ((bad_xaddr >= BRIDGE_TYPE1_CFG) &&
	(bad_xaddr < (BRIDGE_TYPE1_CFG + 0x1000))) {
	/* Type 1 config space:
	 * slot and function numbers not known.
	 * Perhaps we can read them back?
	 */
	raw_paddr = bad_xaddr - BRIDGE_TYPE1_CFG;
	raw_space = PCIIO_SPACE_CFG;
    }
    if ((bad_xaddr >= BRIDGE_DEVIO0) &&
	(bad_xaddr < BRIDGE_DEVIO(BRIDGE_DEV_CNT))) {
	int                     x;

	raw_paddr = bad_xaddr - BRIDGE_DEVIO0;
	x = raw_paddr / BRIDGE_DEVIO_OFF;
	raw_paddr %= BRIDGE_DEVIO_OFF;
	/* first two devio windows are double-sized */
	if ((x == 1) || (x == 3))
	    raw_paddr += BRIDGE_DEVIO_OFF;
	if (x > 0)
	    x--;
	if (x > 1)
	    x--;
	/* x is which devio reg; no guarantee
	 * PCI slot x will be responding.
	 * still need to figure out who decodes
	 * space/offset on the bus.
	 */
	raw_space = pcibr_soft->bs_slot[x].bss_devio.bssd_space;
	if (raw_space == PCIIO_SPACE_NONE) {
	    /* Someone got an error because they
	     * accessed the PCI bus via a DevIO(x)
	     * window that pcibr has not yet assigned
	     * to any specific PCI address. It is
	     * quite possible that the Device(x)
	     * register has been changed since they
	     * made their access, but we will give it
	     * our best decode shot.
	     */
	    raw_space = pcibr_soft->bs_slot[x].bss_device
		& BRIDGE_DEV_DEV_IO_MEM
		? PCIIO_SPACE_MEM
		: PCIIO_SPACE_IO;
	    raw_paddr +=
		(pcibr_soft->bs_slot[x].bss_device &
		 BRIDGE_DEV_OFF_MASK) <<
		BRIDGE_DEV_OFF_ADDR_SHFT;
	} else
	    raw_paddr += pcibr_soft->bs_slot[x].bss_devio.bssd_base;
    }
    if ((bad_xaddr >= BRIDGE_PCI_MEM32_BASE) &&
	(bad_xaddr <= BRIDGE_PCI_MEM32_LIMIT)) {
	raw_space = PCIIO_SPACE_MEM32;
	raw_paddr = bad_xaddr - BRIDGE_PCI_MEM32_BASE;
    }
    if ((bad_xaddr >= BRIDGE_PCI_MEM64_BASE) &&
	(bad_xaddr <= BRIDGE_PCI_MEM64_LIMIT)) {
	raw_space = PCIIO_SPACE_MEM64;
	raw_paddr = bad_xaddr - BRIDGE_PCI_MEM64_BASE;
    }
    if ((bad_xaddr >= BRIDGE_PCI_IO_BASE) &&
	(bad_xaddr <= BRIDGE_PCI_IO_LIMIT)) {
	raw_space = PCIIO_SPACE_IO;
	raw_paddr = bad_xaddr - BRIDGE_PCI_IO_BASE;
    }
    space = raw_space;
    offset = raw_paddr;

    if ((slot == PCIIO_SLOT_NONE) && (space != PCIIO_SPACE_NONE)) {
	/* we've got a space/offset but not which
	 * PCI slot decodes it. Check through our
	 * notions of which devices decode where.
	 *
	 * Yes, this "duplicates" some logic in
	 * pcibr_addr_toslot; the difference is,
	 * this code knows which space we are in,
	 * and can really really tell what is
	 * going on (no guessing).
	 */

	for (cs = 0; (cs < 8) && (slot == PCIIO_SLOT_NONE); cs++) {
	    int                     nf = pcibr_soft->bs_slot[cs].bss_ninfo;
	    pcibr_info_h            pcibr_infoh = pcibr_soft->bs_slot[cs].bss_infos;

	    for (cf = 0; (cf < nf) && (slot == PCIIO_SLOT_NONE); cf++) {
		pcibr_info_t            pcibr_info = pcibr_infoh[cf];

		if (!pcibr_info)
		    continue;
		for (cw = 0; (cw < 6) && (slot == PCIIO_SLOT_NONE); ++cw) {
		    if (((wx = pcibr_info->f_window[cw].w_space) != PCIIO_SPACE_NONE) &&
			((wb = pcibr_info->f_window[cw].w_base) != 0) &&
			((ws = pcibr_info->f_window[cw].w_size) != 0) &&
			((wl = wb + ws) > wb) &&
			((wb <= offset) && (wl > offset))) {
			/* MEM, MEM32 and MEM64 need to
			 * compare as equal ...
			 */
			if ((wx == space) ||
			    (((wx == PCIIO_SPACE_MEM) ||
			      (wx == PCIIO_SPACE_MEM32) ||
			      (wx == PCIIO_SPACE_MEM64)) &&
			     ((space == PCIIO_SPACE_MEM) ||
			      (space == PCIIO_SPACE_MEM32) ||
			      (space == PCIIO_SPACE_MEM64)))) {
			    slot = cs;
			    func = cf;
			    space = PCIIO_SPACE_WIN(cw);
			    offset -= wb;
			}		/* endif window space match */
		    }			/* endif window valid and addr match */
		}			/* next window unless slot set */
	    }				/* next func unless slot set */
	}				/* next slot unless slot set */
	/* XXX- if slot is still -1, no PCI devices are
	 * decoding here using their standard PCI BASE
	 * registers. This would be a really good place
	 * to cross-coordinate with the pciio PCI
	 * address space allocation routines, to find
	 * out if this address is "allocated" by any of
	 * our subsidiary devices.
	 */
    }
    /* Scan all piomap records on this PCI bus to update
     * the TimeOut Counters on all matching maps. If we
     * don't already know the slot number, take it from
     * the first matching piomap. Note that we have to
     * compare maps against raw_space and raw_paddr
     * since space and offset could already be
     * window-relative.
     *
     * There is a chance that one CPU could update
     * through this path, and another CPU could also
     * update due to an interrupt. Closing this hole
     * would only result in the possibility of some
     * errors never getting logged at all, and since the
     * use for bp_toc is as a logical test rather than a
     * strict count, the excess counts are not a
     * problem.
     */
    for (cs = 0; cs < 8; ++cs) {
	int 		nf = pcibr_soft->bs_slot[cs].bss_ninfo;
	pcibr_info_h	pcibr_infoh = pcibr_soft->bs_slot[cs].bss_infos;

	for (cf = 0; cf < nf; cf++) {
	    pcibr_info_t 	pcibr_info = pcibr_infoh[cf];
	    pcibr_piomap_t	map;    

	    if (!pcibr_info)
		continue;

	    for (map = pcibr_info->f_piomap;
	     map != NULL; map = map->bp_next) {
	    wx = map->bp_space;
	    wb = map->bp_pciaddr;
	    ws = map->bp_mapsz;
	    cw = wx - PCIIO_SPACE_WIN(0);
	    if (cw < 6) {
		wb += pcibr_soft->bs_slot[cs].bss_window[cw].bssw_base;
		wx = pcibr_soft->bs_slot[cs].bss_window[cw].bssw_space;
	    }
	    if (wx == PCIIO_SPACE_ROM) {
		wb += pcibr_info->f_rbase;
		wx = PCIIO_SPACE_MEM;
	    }
	    if ((wx == PCIIO_SPACE_MEM32) ||
		(wx == PCIIO_SPACE_MEM64))
		wx = PCIIO_SPACE_MEM;
	    wl = wb + ws;
	    if ((wx == raw_space) && (raw_paddr >= wb) && (raw_paddr < wl)) {
		atomicAddInt(map->bp_toc, 1);
		if (slot == PCIIO_SLOT_NONE) {
		    slot = cs;
		    space = map->bp_space;
		    if (cw < 6)
			offset -= pcibr_soft->bs_slot[cs].bss_window[cw].bssw_base;
		}
	    }
	    }
	}
    }

    if (space != PCIIO_SPACE_NONE) {
	if (slot != PCIIO_SLOT_NONE) 
	    if (func != PCIIO_FUNC_NONE)
		IOERROR_SETVALUE(ioe, widgetdev, 
				 pciio_widgetdev_create(slot,func));
	    else
    		IOERROR_SETVALUE(ioe, widgetdev, 
				 pciio_widgetdev_create(slot,0));

	IOERROR_SETVALUE(ioe, busspace, space);
	IOERROR_SETVALUE(ioe, busaddr, offset);
    }
    if (mode == MODE_DEVPROBE) {
	/*
	 * During probing, we don't really care what the
	 * error is. Clean up the error in Bridge, notify
	 * subsidiary devices, and return success.
	 */
	pcibr_error_cleanup(pcibr_soft, error_code);

	/* if appropriate, give the error handler for this slot
	 * a shot at this probe access as well.
	 */
	return (slot == PCIIO_SLOT_NONE) ? IOERROR_HANDLED :
	    pciio_error_handler(pcibr_vhdl, error_code, mode, ioe);
    }
    /*
     * If we don't know what "PCI SPACE" the access
     * was targeting, we may have problems at the
     * Bridge itself. Don't touch any bridge registers,
     * and do complain loudly.
     */

    if (space == PCIIO_SPACE_NONE) {
	printk("XIO Bus Error at %s\n"
		"\taccess to XIO bus offset 0x%x\n"
		"\tdoes not correspond to any PCI address\n",
		pcibr_soft->bs_name, bad_xaddr);

	/* caller will dump contents of ioe struct */
	return IOERROR_XTALKLEVEL;
    }

    /*
     * Actual PCI Error handling situation.
     * Typically happens when a user level process accesses
     * PCI space, and it causes some error.
     *
     * Due to PCI Bridge implementation, we get two indication
     * for a read error: an interrupt and a Bus error.
     * We like to handle read error in the bus error context.
     * But the interrupt comes and goes before bus error
     * could make much progress. (NOTE: interrupd does
     * come in _after_ bus error processing starts. But it's
     * completed by the time bus error code reaches PCI PIO
     * error handling.
     * Similarly write error results in just an interrupt,
     * and error handling has to be done at interrupt level.
     * There is no way to distinguish at interrupt time, if an
     * error interrupt is due to read/write error..
     */

    /* We know the xtalk addr, the raw PCI bus space,
     * the raw PCI bus address, the decoded PCI bus
     * space, the offset within that space, and the
     * decoded PCI slot (which may be "PCIIO_SLOT_NONE" if no slot
     * is known to be involved).
     */

    /*
     * Hand the error off to the handler registered
     * for the slot that should have decoded the error,
     * or to generic PCI handling (if pciio decides that
     * such is appropriate).
     */
    retval = pciio_error_handler(pcibr_vhdl, error_code, mode, ioe);

    if (retval != IOERROR_HANDLED) {

	/* Generate a generic message for IOERROR_UNHANDLED
	 * since the subsidiary handlers were silent, and
	 * did no recovery.
	 */
	if (retval == IOERROR_UNHANDLED) {
	    retval = IOERROR_PANIC;

	    /* we may or may not want to print some of this,
	     * depending on debug level and which error code.
	     */

	    printk(KERN_ALERT
		    "PIO Error on PCI Bus %s",
		    pcibr_soft->bs_name);
	    /* this decodes part of the ioe; our caller
	     * will dump the raw details in DEBUG and
	     * kdebug kernels.
	     */
	    BEM_ADD_IOE(ioe);
	}
#if defined(FORCE_ERRORS)
	if (0) {
#elif !DEBUG
	if (kdebug) {
#endif
	    /*
	     * Dump raw data from Bridge/PCI layer.
	     */

	    BEM_ADD_STR("Raw info from Bridge/PCI layer:\n");
	    if (bridge->b_int_status & BRIDGE_ISR_PCIBUS_PIOERR)
		pcibr_error_dump(pcibr_soft);
	    BEM_ADD_SPC(raw_space);
	    BEM_ADD_VAR(raw_paddr);
	    if (IOERROR_FIELDVALID(ioe, widgetdev)) {

		slot = pciio_widgetdev_slot_get(IOERROR_GETVALUE(ioe, 
								 widgetdev));
		func = pciio_widgetdev_func_get(IOERROR_GETVALUE(ioe, 
								 widgetdev));
		if (slot < 8) {
		    bridgereg_t             device = bridge->b_device[slot].reg;

		    BEM_ADD_VAR(slot);
		    BEM_ADD_VAR(func);
		    BEM_ADD_REG(device);
		}
	    }
#if !DEBUG || defined(FORCE_ERRORS)
	}
#endif

	/*
	 * Since error could not be handled at lower level,
	 * error data logged has not  been cleared.
	 * Clean up errors, and
	 * re-enable bridge to interrupt on error conditions.
	 * NOTE: Wheather we get the interrupt on PCI_ABORT or not is
	 * dependent on INT_ENABLE register. This write just makes sure
	 * that if the interrupt was enabled, we do get the interrupt.
	 *
	 * CAUTION: Resetting bit BRIDGE_IRR_PCI_GRP_CLR, acknowledges
	 *      a group of interrupts. If while handling this error,
	 *      some other error has occured, that would be
	 *      implicitly cleared by this write.
	 *      Need a way to ensure we don't inadvertently clear some
	 *      other errors.
	 */
	if (IOERROR_FIELDVALID(ioe, widgetdev))
	    pcibr_device_disable(pcibr_soft, 
				 pciio_widgetdev_slot_get(
					  IOERROR_GETVALUE(ioe, widgetdev)));

	if (mode == MODE_DEVUSERERROR)
	    pcibr_error_cleanup(pcibr_soft, error_code);
    }
    return retval;
}

/*
 * bridge_dmaerror
 *      Some error was identified in a DMA transaction.
 *      This routine will identify the <device, address> that caused the error,
 *      and try to invoke the appropriate bus service to handle this.
 */

#define BRIDGE_DMA_READ_ERROR (BRIDGE_ISR_RESP_XTLK_ERR|BRIDGE_ISR_XREAD_REQ_TIMEOUT)

int
pcibr_dmard_error(
		     pcibr_soft_t pcibr_soft,
		     int error_code,
		     ioerror_mode_t mode,
		     ioerror_t *ioe)
{
    devfs_handle_t            pcibr_vhdl = pcibr_soft->bs_vhdl;
    bridge_t               *bridge = pcibr_soft->bs_base;
    bridgereg_t             bus_lowaddr, bus_uppraddr;
    int                     retval = 0;
    int                     bufnum;

    /*
     * In case of DMA errors, bridge should have logged the
     * address that caused the error.
     * Look up the address, in the bridge error registers, and
     * take appropriate action
     */
    ASSERT(IOERROR_GETVALUE(ioe, widgetnum) == pcibr_soft->bs_xid);
    ASSERT(bridge);

    /*
     * read error log registers
     */
    bus_lowaddr = bridge->b_wid_resp_lower;
    bus_uppraddr = bridge->b_wid_resp_upper;

    bufnum = BRIDGE_RESP_ERRUPPR_BUFNUM(bus_uppraddr);
    IOERROR_SETVALUE(ioe, widgetdev, 
		     pciio_widgetdev_create(
				    BRIDGE_RESP_ERRUPPR_DEVICE(bus_uppraddr),
				    0));
    IOERROR_SETVALUE(ioe, busaddr,
		     (bus_lowaddr |
		      ((iopaddr_t)
		       (bus_uppraddr &
			BRIDGE_ERRUPPR_ADDRMASK) << 32)));

    /*
     * need to ensure that the xtalk adress in ioe
     * maps to PCI error address read from bridge.
     * How to convert PCI address back to Xtalk address ?
     * (better idea: convert XTalk address to PCI address
     * and then do the compare!)
     */

    retval = pciio_error_handler(pcibr_vhdl, error_code, mode, ioe);
    if (retval != IOERROR_HANDLED)
	pcibr_device_disable(pcibr_soft, 
			     pciio_widgetdev_slot_get(
				      IOERROR_GETVALUE(ioe,widgetdev)));

    /*
     * Re-enable bridge to interrupt on BRIDGE_IRR_RESP_BUF_GRP_CLR
     * NOTE: Wheather we get the interrupt on BRIDGE_IRR_RESP_BUF_GRP_CLR or
     * not is dependent on INT_ENABLE register. This write just makes sure
     * that if the interrupt was enabled, we do get the interrupt.
     */
    bridge->b_int_rst_stat = BRIDGE_IRR_RESP_BUF_GRP_CLR;

    /*
     * Also, release the "bufnum" back to buffer pool that could be re-used.
     * This is done by "disabling" the buffer for a moment, then restoring
     * the original assignment.
     */

    {
	reg_p                   regp;
	bridgereg_t             regv;
	bridgereg_t             mask;

	regp = (bufnum & 1)
	    ? &bridge->b_odd_resp
	    : &bridge->b_even_resp;

	mask = 0xF << ((bufnum >> 1) * 4);

	regv = *regp;
	*regp = regv & ~mask;
	*regp = regv;
    }

    return retval;
}

/*
 * pcibr_dmawr_error:
 *      Handle a dma write error caused by a device attached to this bridge.
 *
 *      ioe has the widgetnum, widgetdev, and memaddr fields updated
 *      But we don't know the PCI address that corresponds to "memaddr"
 *      nor do we know which device driver is generating this address.
 *
 *      There is no easy way to find out the PCI address(es) that map
 *      to a specific system memory address. Bus handling code is also
 *      of not much help, since they don't keep track of the DMA mapping
 *      that have been handed out.
 *      So it's a dead-end at this time.
 *
 *      If translation is available, we could invoke the error handling
 *      interface of the device driver.
 */
/*ARGSUSED */
int
pcibr_dmawr_error(
		     pcibr_soft_t pcibr_soft,
		     int error_code,
		     ioerror_mode_t mode,
		     ioerror_t *ioe)
{
    devfs_handle_t            pcibr_vhdl = pcibr_soft->bs_vhdl;
    int                     retval;

    retval = pciio_error_handler(pcibr_vhdl, error_code, mode, ioe);

    if (retval != IOERROR_HANDLED) {
	pcibr_device_disable(pcibr_soft, 
			     pciio_widgetdev_slot_get(
				      IOERROR_GETVALUE(ioe, widgetdev)));

    }
    return retval;
}

/*
 * Bridge error handler.
 *      Interface to handle all errors that involve bridge in some way.
 *
 *      This normally gets called from xtalk error handler.
 *      ioe has different set of fields set depending on the error that
 *      was encountered. So, we have a bit field indicating which of the
 *      fields are valid.
 *
 * NOTE: This routine could be operating in interrupt context. So,
 *      don't try to sleep here (till interrupt threads work!!)
 */
int
pcibr_error_handler(
		       error_handler_arg_t einfo,
		       int error_code,
		       ioerror_mode_t mode,
		       ioerror_t *ioe)
{
    pcibr_soft_t            pcibr_soft;
    int                     retval = IOERROR_BADERRORCODE;

#ifdef EHE_ENABLE
    devfs_handle_t	    xconn_vhdl,pcibr_vhdl;
    error_state_t	    e_state;
#endif /* EHE_ENABLE */

    pcibr_soft = (pcibr_soft_t) einfo;

#ifdef EHE_ENABLE
    xconn_vhdl = pcibr_soft->bs_conn;
    pcibr_vhdl = pcibr_soft->bs_vhdl;

    e_state = error_state_get(xconn_vhdl);
    
    if (error_state_set(pcibr_vhdl, e_state) == 
	ERROR_RETURN_CODE_CANNOT_SET_STATE)
	return(IOERROR_UNHANDLED);

    /* If we are in the action handling phase clean out the error state
     * on the xswitch.
     */
    if (e_state == ERROR_STATE_ACTION)
	(void)error_state_set(xconn_vhdl, ERROR_STATE_NONE);
#endif /* EHE_ENABLE */

#if DEBUG && ERROR_DEBUG
    printk("%s: pcibr_error_handler\n", pcibr_soft->bs_name);
#endif

    ASSERT(pcibr_soft != NULL);

    if (error_code & IOECODE_PIO)
	retval = pcibr_pioerror(pcibr_soft, error_code, mode, ioe);

    if (error_code & IOECODE_DMA) {
	if (error_code & IOECODE_READ) {
	    /*
	     * DMA read error occurs when a device attached to the bridge
	     * tries to read some data from system memory, and this
	     * either results in a timeout or access error.
	     * First case is indicated by the bit "XREAD_REQ_TOUT"
	     * and second case by "RESP_XTALK_ERROR" bit in bridge error
	     * interrupt status register.
	     *
	     * pcibr_error_intr_handler would get invoked first, and it has
	     * the responsibility of calling pcibr_error_handler with
	     * suitable parameters.
	     */

	    retval = pcibr_dmard_error(pcibr_soft, error_code, MODE_DEVERROR, ioe);
	}
	if (error_code & IOECODE_WRITE) {
	    /*
	     * A device attached to this bridge has been generating
	     * bad DMA writes. Find out the device attached, and
	     * slap on it's wrist.
	     */

	    retval = pcibr_dmawr_error(pcibr_soft, error_code, MODE_DEVERROR, ioe);
	}
    }
    return retval;

}

/*
 * Reenable a device after handling the error.
 * This is called by the lower layers when they wish to be reenabled
 * after an error.
 * Note that each layer would be calling the previous layer to reenable
 * first, before going ahead with their own re-enabling.
 */

int
pcibr_error_devenable(devfs_handle_t pconn_vhdl, int error_code)
{
    pciio_info_t            pciio_info = pciio_info_get(pconn_vhdl);
    pciio_slot_t            pciio_slot = pciio_info_slot_get(pciio_info);
    pcibr_soft_t            pcibr_soft = (pcibr_soft_t) pciio_info_mfast_get(pciio_info);

    ASSERT(error_code & IOECODE_PIO);

    /* If the error is not known to be a write,
     * we have to call devenable.
     * write errors are isolated to the bridge.
     */
    if (!(error_code & IOECODE_WRITE)) {
	devfs_handle_t            xconn_vhdl = pcibr_soft->bs_conn;
	int                     rc;

	rc = xtalk_error_devenable(xconn_vhdl, pciio_slot, error_code);
	if (rc != IOERROR_HANDLED)
	    return rc;
    }
    pcibr_error_cleanup(pcibr_soft, error_code);
    return IOERROR_HANDLED;
}
