/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001-2003 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/interrupt.h>
#include <asm/sn/sgi.h>
#include <asm/sn/addrs.h>
#include <asm/sn/iograph.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/pci/pci_defs.h>


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
#define BRIDGE_PIOERR_TIMEOUT	1	/* Timeout in non-debug mode */
#endif

#ifdef  DEBUG
#ifdef ERROR_DEBUG
uint64_t bridge_errors_to_dump = ~BRIDGE_ISR_INT_MSK;
#else
uint64_t bridge_errors_to_dump = BRIDGE_ISR_ERROR_DUMP;
#endif
#else
uint64_t bridge_errors_to_dump = BRIDGE_ISR_ERROR_FATAL |
                                   BRIDGE_ISR_PCIBUS_PIOERR;
#endif

int pcibr_pioerr_dump = 1;	/* always dump pio errors */

/*
 * register values
 * map between numeric values and symbolic values
 */
struct reg_values {
	unsigned long long rv_value;
	char *rv_name;
};

/*
 * register descriptors are used for formatted prints of register values
 * rd_mask and rd_shift must be defined, other entries may be null
 */
struct reg_desc {
	unsigned long long rd_mask;	/* mask to extract field */
	int rd_shift;		/* shift for extracted value, - >>, + << */
	char *rd_name;		/* field name */
	char *rd_format;	/* format to print field */
	struct reg_values *rd_values;	/* symbolic names of values */
};

/* Crosstalk Packet Types */
static struct reg_values xtalk_cmd_pactyp[] =
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

static struct reg_desc   xtalk_cmd_bits[] =
{
    {WIDGET_DIDN, -28, "DIDN", "%x"},
    {WIDGET_SIDN, -24, "SIDN", "%x"},
    {WIDGET_PACTYP, -20, "PACTYP", 0, xtalk_cmd_pactyp},
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

static char             *pcibr_isr_errs[] =
{
    "", "", "", "", "", "", "", "",
    "08: Reserved Bit 08",
    "09: PCI to Crosstalk read request timeout",
    "10: PCI retry operation count exhausted.",
    "11: PCI bus device select timeout",
    "12: PCI device reported parity error",
    "13: PCI Address/Cmd parity error ",
    "14: PCI Bridge detected parity error",
    "15: PCI abort condition",
    "16: Reserved Bit 16",
    "17: LLP Transmitter Retry count wrapped",	/* PIC ONLY */
    "18: LLP Transmitter side required Retry",	/* PIC ONLY */
    "19: LLP Receiver retry count wrapped",	/* PIC ONLY */
    "20: LLP Receiver check bit error",		/* PIC ONLY */
    "21: LLP Receiver sequence number error",	/* PIC ONLY */
    "22: Request packet overflow",
    "23: Request operation not supported by bridge",
    "24: Request packet has invalid address for bridge widget",
    "25: Incoming request xtalk command word error bit set or invalid sideband",
    "26: Incoming response xtalk command word error bit set or invalid sideband",
    "27: Framing error, request cmd data size does not match actual",
    "28: Framing error, response cmd data size does not match actual",
    "29: Unexpected response arrived",
    "30: PMU Access Fault",
    "31: Reserved Bit 31",
    "32: PCI-X address or attribute cycle parity error",
    "33: PCI-X data cycle parity error",
    "34: PCI-X master timeout (ie. master abort)",
    "35: PCI-X pio retry counter exhausted",
    "36: PCI-X SERR",
    "37: PCI-X PERR", 
    "38: PCI-X target abort",
    "39: PCI-X read request timeout",
    "40: PCI / PCI-X device requestin arbitration error",
    "41: internal RAM parity error",
    "42: PCI-X unexpected completion cycle to master",
    "43: PCI-X split completion timeout",
    "44: PCI-X split completion error message",
    "45: PCI-X split completion message parity error",
};

/*
 * print_register() allows formatted printing of bit fields.  individual
 * bit fields are described by a struct reg_desc, multiple bit fields within
 * a single word can be described by multiple reg_desc structures.
 * %r outputs a string of the format "<bit field descriptions>"
 * %R outputs a string of the format "0x%x<bit field descriptions>"
 *
 * The fields in a reg_desc are:
 *	unsigned long long rd_mask; An appropriate mask to isolate the bit field
 *				within a word, and'ed with val
 *
 *	int rd_shift;		A shift amount to be done to the isolated
 *				bit field.  done before printing the isolate
 *				bit field with rd_format and before searching
 *				for symbolic value names in rd_values
 *
 *	char *rd_name;		If non-null, a bit field name to label any
 *				out from rd_format or searching rd_values.
 *				if neither rd_format or rd_values is non-null
 *				rd_name is printed only if the isolated
 *				bit field is non-null.
 *
 *	char *rd_format;	If non-null, the shifted bit field value
 *				is printed using this format.
 *
 *	struct reg_values *rd_values;	If non-null, a pointer to a table
 *				matching numeric values with symbolic names.
 *				rd_values are searched and the symbolic
 *				value is printed if a match is found, if no
 *				match is found "???" is printed.
 *				
 */

static void
print_register(unsigned long long reg, struct reg_desc *addr)
{
	register struct reg_desc *rd;
	register struct reg_values *rv;
	unsigned long long field;
	int any;

	printk("<");
	any = 0;
	for (rd = addr; rd->rd_mask; rd++) {
		field = reg & rd->rd_mask;
		field = (rd->rd_shift > 0) ? field << rd->rd_shift : field >> -rd->rd_shift;
		if (any && (rd->rd_format || rd->rd_values || (rd->rd_name && field)))
			printk(",");
		if (rd->rd_name) {
			if (rd->rd_format || rd->rd_values || field) {
				printk("%s", rd->rd_name);
				any = 1;
			}
			if (rd->rd_format || rd->rd_values) {
				printk("=");
				any = 1;
			}
		}
		/* You can have any format so long as it is %x */
		if (rd->rd_format) {
			printk("%llx", field);
			any = 1;
			if (rd->rd_values)
				printk(":");
		}
		if (rd->rd_values) {
			any = 1;
			for (rv = rd->rd_values; rv->rv_name; rv++) {
				if (field == rv->rv_value) {
					printk("%s", rv->rv_name);
					break;
				}
			}
			if (rv->rv_name == NULL)
				printk("???");
		}
	}
	printk(">\n");
}


/*
 * display memory directory state
 */
static void
pcibr_show_dir_state(paddr_t paddr, char *prefix)
{
#ifdef PCIBR_LATER
	int state;
	uint64_t vec_ptr;
	hubreg_t elo;
	extern char *dir_state_str[];
	extern void get_dir_ent(paddr_t, int *, uint64_t *, hubreg_t *);

	get_dir_ent(paddr, &state, &vec_ptr, &elo);

	printf("%saddr 0x%lx: state 0x%x owner 0x%lx (%s)\n", 
		prefix, (uint64_t)paddr, state, (uint64_t)vec_ptr, 
		dir_state_str[state]);
#endif /* PCIBR_LATER */
}


void
print_bridge_errcmd(pcibr_soft_t pcibr_soft, uint32_t cmdword, char *errtype)
{
    printk(
	    "\t    Bridge %sError Command Word Register ", errtype);
    print_register(cmdword, xtalk_cmd_bits);
}


/*
 *	Dump relevant error information for Bridge error interrupts.
 */
/*ARGSUSED */
void
pcibr_error_dump(pcibr_soft_t pcibr_soft)
{
    uint64_t		    int_status;
    uint64_t		    mult_int;
    uint64_t		    bit;
    int                     i;

    int_status = (pcireg_intr_status_get(pcibr_soft) & ~BRIDGE_ISR_INT_MSK);

    if (!int_status) {
	/* No error bits set */
	return;
    }

    /* Check if dumping the same error information multiple times */
    if ( pcibr_soft->bs_errinfo.bserr_intstat == int_status )
	return;
    pcibr_soft->bs_errinfo.bserr_intstat = int_status;

    printk(KERN_ALERT "PCI BRIDGE ERROR: int_status is 0x%lx for %s\n"
	"    Dumping relevant %s registers for each bit set...\n",
	    int_status, pcibr_soft->bs_name,
	    "PIC");

    for (i = PCIBR_ISR_ERR_START; i < 64; i++) {
	bit = 1ull << i;

	/* A number of int_status bits are only valid for PIC's bus0 */
	if ((pcibr_soft->bs_busnum != 0) && 
	    ((bit == BRIDGE_ISR_UNSUPPORTED_XOP) ||
	     (bit == BRIDGE_ISR_LLP_REC_SNERR) ||
	     (bit == BRIDGE_ISR_LLP_REC_CBERR) ||
	     (bit == BRIDGE_ISR_LLP_RCTY) ||
	     (bit == BRIDGE_ISR_LLP_TX_RETRY) ||
	     (bit == BRIDGE_ISR_LLP_TCTY))) {
	    continue;
	}

	if (int_status & bit) {
	    printk("\t%s\n", pcibr_isr_errs[i]);

	    switch (bit) {

	    case PIC_ISR_INT_RAM_PERR:	    /* bit41	INT_RAM_PERR */
		/* XXX: should breakdown meaning of bits in reg */
		printk("\t	Internal RAM Parity Error: 0x%lx\n",
		    pcireg_parity_err_get(pcibr_soft));
		break;

	    case PIC_ISR_PCIX_ARB_ERR:	    /* bit40	PCI_X_ARB_ERR */
		/* XXX: should breakdown meaning of bits in reg */
		printk("\t	Arbitration Reg: 0x%lx\n",
		    pcireg_arbitration_get(pcibr_soft));
		break;

	    case PIC_ISR_PCIX_REQ_TOUT:	    /* bit39	PCI_X_REQ_TOUT */
		/* XXX: should breakdown meaning of attribute bit */
		printk(
		    "\t	   PCI-X DMA Request Error Address Reg: 0x%lx\n"
		    "\t	   PCI-X DMA Request Error Attribute Reg: 0x%lx\n",
		    pcireg_pcix_req_err_addr_get(pcibr_soft),
		    pcireg_pcix_req_err_attr_get(pcibr_soft));
		break;

	    case PIC_ISR_PCIX_SPLIT_MSG_PE: /* bit45	PCI_X_SPLIT_MES_PE */
	    case PIC_ISR_PCIX_SPLIT_EMSG:   /* bit44	PCI_X_SPLIT_EMESS */
	    case PIC_ISR_PCIX_SPLIT_TO:	    /* bit43	PCI_X_SPLIT_TO */
		/* XXX: should breakdown meaning of attribute bit */
		printk(
		    "\t	   PCI-X Split Request Address Reg: 0x%lx\n"
		    "\t	   PCI-X Split Request Attribute Reg: 0x%lx\n",
		    pcireg_pcix_pio_split_addr_get(pcibr_soft),
		    pcireg_pcix_pio_split_attr_get(pcibr_soft));
		/* FALL THRU */

	    case PIC_ISR_PCIX_UNEX_COMP:    /* bit42	PCI_X_UNEX_COMP */
	    case PIC_ISR_PCIX_TABORT:	    /* bit38	PCI_X_TABORT */
	    case PIC_ISR_PCIX_PERR:	    /* bit37	PCI_X_PERR */
	    case PIC_ISR_PCIX_SERR:	    /* bit36	PCI_X_SERR */
	    case PIC_ISR_PCIX_MRETRY:	    /* bit35	PCI_X_MRETRY */
	    case PIC_ISR_PCIX_MTOUT:	    /* bit34	PCI_X_MTOUT */
	    case PIC_ISR_PCIX_DA_PARITY:    /* bit33	PCI_X_DA_PARITY */
	    case PIC_ISR_PCIX_AD_PARITY:    /* bit32	PCI_X_AD_PARITY */
		/* XXX: should breakdown meaning of attribute bit */
		printk(
		    "\t	   PCI-X Bus Error Address Reg: 0x%lx\n"
		    "\t	   PCI-X Bus Error Attribute Reg: 0x%lx\n"
		    "\t	   PCI-X Bus Error Data Reg: 0x%lx\n",
		    pcireg_pcix_bus_err_addr_get(pcibr_soft),
		    pcireg_pcix_bus_err_attr_get(pcibr_soft),
		    pcireg_pcix_bus_err_data_get(pcibr_soft));
		break;

	    case BRIDGE_ISR_PAGE_FAULT:	/* bit30    PMU_PAGE_FAULT */
		printk("\t    Map Fault Address Reg: 0x%lx\n",
		    pcireg_map_fault_get(pcibr_soft));
		break;

	    case BRIDGE_ISR_UNEXP_RESP:		/* bit29    UNEXPECTED_RESP */
		print_bridge_errcmd(pcibr_soft,
			    pcireg_linkside_err_get(pcibr_soft), "Aux ");

		/* PIC in PCI-X mode, dump the PCIX DMA Request registers */
		if (IS_PCIX(pcibr_soft)) {
		    /* XXX: should breakdown meaning of attr bit */
		    printk( 
			"\t    PCI-X DMA Request Error Addr Reg: 0x%lx\n"
			"\t    PCI-X DMA Request Error Attr Reg: 0x%lx\n",
			pcireg_pcix_req_err_addr_get(pcibr_soft),
			pcireg_pcix_req_err_attr_get(pcibr_soft));
		}
		break;

	    case BRIDGE_ISR_BAD_XRESP_PKT:	/* bit28    BAD_RESP_PACKET */
	    case BRIDGE_ISR_RESP_XTLK_ERR:	/* bit26    RESP_XTALK_ERROR */
		print_bridge_errcmd(pcibr_soft,
				pcireg_linkside_err_get(pcibr_soft), "Aux ");
		 
		/* PCI-X mode, DMA Request Error registers are valid.  But
		 * in PCI mode, Response Buffer Address register are valid.
		 */
		if (IS_PCIX(pcibr_soft)) {
		    /* XXX: should breakdown meaning of attribute bit */
		    printk(
			"\t    PCI-X DMA Request Error Addr Reg: 0x%lx\n"
		        "\t    PCI-X DMA Request Error Attribute Reg: 0x%lx\n",
			pcireg_pcix_req_err_addr_get(pcibr_soft),
			pcireg_pcix_req_err_attr_get(pcibr_soft));
		} else {
		    printk(
		        "\t    Bridge Response Buf Error Addr Reg: 0x%lx\n"
		        "\t    dev-num %d buff-num %d addr 0x%lx\n",
			pcireg_resp_err_get(pcibr_soft),
			(int)pcireg_resp_err_dev_get(pcibr_soft),
			(int)pcireg_resp_err_buf_get(pcibr_soft),
			pcireg_resp_err_addr_get(pcibr_soft));
		    if (bit == BRIDGE_ISR_RESP_XTLK_ERR) {
			/* display memory directory associated with cacheline */
			pcibr_show_dir_state(
				    pcireg_resp_err_get(pcibr_soft), "\t    ");
		    }
		}
		break;

	    case BRIDGE_ISR_BAD_XREQ_PKT:	/* bit27    BAD_XREQ_PACKET */
	    case BRIDGE_ISR_REQ_XTLK_ERR:	/* bit25    REQ_XTALK_ERROR */
	    case BRIDGE_ISR_INVLD_ADDR:		/* bit24    INVALID_ADDRESS */
		print_bridge_errcmd(pcibr_soft,
				pcireg_cmdword_err_get(pcibr_soft), "");
		printk(
		    "\t    Bridge Error Address Register: 0x%lx\n"
		    "\t    Bridge Error Address: 0x%lx\n",
		    pcireg_bus_err_get(pcibr_soft),
		    pcireg_bus_err_get(pcibr_soft));
		break;

	    case BRIDGE_ISR_UNSUPPORTED_XOP:	/* bit23    UNSUPPORTED_XOP */
		print_bridge_errcmd(pcibr_soft,
				pcireg_linkside_err_get(pcibr_soft), "Aux ");
		printk("\t    Address Holding Link Side Error Reg: 0x%lx\n",
			pcireg_linkside_err_addr_get(pcibr_soft));
		break;

	    case BRIDGE_ISR_XREQ_FIFO_OFLOW:	/* bit22    XREQ_FIFO_OFLOW */
		print_bridge_errcmd(pcibr_soft,
				pcireg_linkside_err_get(pcibr_soft), "Aux ");
		printk("\t    Address Holding Link Side Error Reg: 0x%lx\n",
			pcireg_linkside_err_addr_get(pcibr_soft));
		break;

	    case BRIDGE_ISR_PCI_ABORT:		/* bit15    PCI_ABORT */
	    case BRIDGE_ISR_PCI_PARITY:		/* bit14    PCI_PARITY */
	    case BRIDGE_ISR_PCI_SERR:		/* bit13    PCI_SERR */
	    case BRIDGE_ISR_PCI_PERR:		/* bit12    PCI_PERR */
	    case BRIDGE_ISR_PCI_MST_TIMEOUT:	/* bit11    PCI_MASTER_TOUT */
	    case BRIDGE_ISR_PCI_RETRY_CNT:	/* bit10    PCI_RETRY_CNT */
		printk("\t    PCI Error Address Register: 0x%lx\n"
		    "\t    PCI Error Address: 0x%lx\n",
		    pcireg_pci_bus_addr_get(pcibr_soft),
		    pcireg_pci_bus_addr_addr_get(pcibr_soft));
		break;

	    case BRIDGE_ISR_XREAD_REQ_TIMEOUT:	/* bit09    XREAD_REQ_TOUT */
		printk("\t    Bridge Response Buf Error Addr Reg: 0x%lx\n"
		    "\t    dev-num %d buff-num %d addr 0x%lx\n",
		    pcireg_resp_err_get(pcibr_soft),
		    (int)pcireg_resp_err_dev_get(pcibr_soft),
		    (int)pcireg_resp_err_buf_get(pcibr_soft),
		    pcireg_resp_err_get(pcibr_soft));
		break;
	    }
	}
    }

    mult_int = pcireg_intr_multiple_get(pcibr_soft);

    if (mult_int & ~BRIDGE_ISR_INT_MSK) {
	printk("    %s Multiple Interrupt Register is 0x%lx\n",
		pcibr_soft->bs_asic_name, mult_int);
	for (i = PCIBR_ISR_ERR_START; i < 64; i++) {
	    if (mult_int & (1ull << i))
		printk( "\t%s\n", pcibr_isr_errs[i]);
	}
    }
}

/* pcibr_pioerr_check():
 *	Check to see if this pcibr has a PCI PIO
 *	TIMEOUT error; if so, bump the timeout-count
 *	on any piomaps that could cover the address.
 */
static void
pcibr_pioerr_check(pcibr_soft_t soft)
{
    uint64_t		    int_status;
    iopaddr_t		    pci_addr;
    pciio_slot_t	    slot;
    pcibr_piomap_t	    map;
    iopaddr_t		    base;
    size_t		    size;
    unsigned		    win;
    int			    func;

    int_status = pcireg_intr_status_get(soft);

    if (int_status & BRIDGE_ISR_PCIBUS_PIOERR) {
	pci_addr = pcireg_pci_bus_addr_get(soft);

	slot = PCIBR_NUM_SLOTS(soft);
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
			base += soft->bs_slot[slot].bss_window[win].bssw_base;
		    else if (map->bp_space == PCIIO_SPACE_ROM)
			base += pcibr_info->f_rbase;
		    if ((pci_addr >= base) && (pci_addr < (base + size)))
			atomic_inc(&map->bp_toc);
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

irqreturn_t
pcibr_error_intr_handler(int irq, void *arg, struct pt_regs *ep)
{
    pcibr_soft_t            pcibr_soft;
    void               *bridge;
    uint64_t		    int_status;
    uint64_t		    err_status;
    int                     i;
    uint64_t		    disable_errintr_mask = 0;
    nasid_t		    nasid;


#if PCIBR_SOFT_LIST
    /*
     * Defensive code for linked pcibr_soft structs
     */
    {
	extern pcibr_list_p	pcibr_list;
	pcibr_list_p            entry;

	entry = pcibr_list;
	while (1) {
	    if (entry == NULL) {
		printk("pcibr_error_intr_handler: (0x%lx) is not a pcibr_soft!",
	 	      (uint64_t)arg);
    		return IRQ_NONE;
	    }
	    if ((intr_arg_t) entry->bl_soft == arg)
		break;
	    entry = entry->bl_next;
	}
    }
#endif /* PCIBR_SOFT_LIST */
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
	DECLARE_WAIT_QUEUE_HEAD(wq);
	sleep_on_timeout(&wq, BRIDGE_PIOERR_TIMEOUT*HZ );  /* sleep */
	pcibr_soft->bs_errinfo.bserr_toutcnt++;
	/* Let's go recursive */
	return(pcibr_error_intr_handler(irq, arg, ep));
    }

    int_status = pcireg_intr_status_get(pcibr_soft);

    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_INTR_ERROR, pcibr_soft->bs_conn,
		"pcibr_error_intr_handler: int_status=0x%lx\n", int_status));

    /* int_status is which bits we have to clear;
     * err_status is the bits we haven't handled yet.
     */
    err_status = int_status;

    if (!(int_status & ~BRIDGE_ISR_INT_MSK)) {
	/*
	 * No error bit set!!.
	 */
	return IRQ_HANDLED;
    }
    /*
     * If we have a PCIBUS_PIOERR, hand it to the logger.
     */
    if (int_status & BRIDGE_ISR_PCIBUS_PIOERR) {
	pcibr_pioerr_check(pcibr_soft);
    }

    if (err_status) {
	struct bs_errintr_stat_s *bs_estat ;
        bs_estat = &pcibr_soft->bs_errintr_stat[PCIBR_ISR_ERR_START];

	for (i = PCIBR_ISR_ERR_START; i < 64; i++, bs_estat++) {
	    if (err_status & (1ull << i)) {
		uint32_t              errrate = 0;
		uint32_t              errcount = 0;
		uint32_t              errinterval = 0, current_tick = 0;
		int                     llp_tx_retry_errors = 0;
		int                     is_llp_tx_retry_intr = 0;

		bs_estat->bs_errcount_total++;

		current_tick = jiffies;
		errinterval = (current_tick - bs_estat->bs_lasterr_timestamp);
		errcount = (bs_estat->bs_errcount_total -
			    bs_estat->bs_lasterr_snapshot);

		/* LLP interrrupt errors are only valid on BUS0 of the PIC */
		if (pcibr_soft->bs_busnum == 0)
		    is_llp_tx_retry_intr = (BRIDGE_ISR_LLP_TX_RETRY==(1ull << i));

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
		/* PIC BRINGUP WAR (PV# 856155):
		 * Dont disable PCI_X_ARB_ERR interrupts, we need the
		 * interrupt inorder to clear the DEV_BROKE bits in
		 * b_arb register to re-enable the device.
		 */
		if (!(err_status & PIC_ISR_PCIX_ARB_ERR) &&
				PCIBR_WAR_ENABLED(PV856155, pcibr_soft)) {

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
		    disable_errintr_mask |= (1ull << i);
		}
		} /* PIC: WAR for PV 856155 end-of-if */
	    }
	}
    }

    if (disable_errintr_mask) {
	unsigned long s;
	/*
	 * Disable some high frequency errors as they
	 * could eat up too much cpu time.
	 */
	s = pcibr_lock(pcibr_soft);
	pcireg_intr_enable_bit_clr(pcibr_soft, disable_errintr_mask);
	pcibr_unlock(pcibr_soft, s);
    }
    /*
     * If we leave the PROM cacheable, T5 might
     * try to do a cache line sized writeback to it,
     * which will cause a BRIDGE_ISR_INVLD_ADDR.
     */
    if ((err_status & BRIDGE_ISR_INVLD_ADDR) &&
	(0x00C00000 == (pcireg_bus_err_get(pcibr_soft) & 0xFFFFFFFFFFC00000)) &&
	(0x00402000 == (0x00F07F00 & pcireg_cmdword_err_get(pcibr_soft)))) {
	err_status &= ~BRIDGE_ISR_INVLD_ADDR;
    }
    /*
     * pcibr_pioerr_dump is a systune that make be used to not
     * print bridge registers for interrupts generated by pio-errors.
     * Some customers do early probes and expect a lot of failed
     * pios.
     */
    if (!pcibr_pioerr_dump) {
	bridge_errors_to_dump &= ~BRIDGE_ISR_PCIBUS_PIOERR;
    } else {
	bridge_errors_to_dump |= BRIDGE_ISR_PCIBUS_PIOERR;
    }

    /* Dump/Log Bridge error interrupt info */
    if (err_status & bridge_errors_to_dump) {
	printk("BRIDGE ERR_STATUS 0x%lx\n", err_status);
	pcibr_error_dump(pcibr_soft);
    }

    /* PIC BRINGUP WAR (PV# 867308):
     * Make BRIDGE_ISR_LLP_REC_SNERR & BRIDGE_ISR_LLP_REC_CBERR fatal errors
     * so we know we've hit the problem defined in PV 867308 that we believe
     * has only been seen in simulation
     */
    if (PCIBR_WAR_ENABLED(PV867308, pcibr_soft) &&
	(err_status & (BRIDGE_ISR_LLP_REC_SNERR | BRIDGE_ISR_LLP_REC_CBERR))) {
	printk("BRIDGE ERR_STATUS 0x%lx\n", err_status);
	pcibr_error_dump(pcibr_soft);
	/* machine_error_dump(""); */
	panic("PCI Bridge Error interrupt killed the system");
    }

    if (err_status & BRIDGE_ISR_ERROR_FATAL) {
	panic("PCI Bridge Error interrupt killed the system");
	    /*NOTREACHED */
    }


    /*
     * We can't return without re-enabling the interrupt, since
     * it would cause problems for devices like IOC3 (Lost
     * interrupts ?.). So, just cleanup the interrupt, and
     * use saved values later..
     * 
     * PIC doesn't require groups of interrupts to be cleared...
     */
    pcireg_intr_reset_set(pcibr_soft, (int_status | BRIDGE_IRR_MULTI_CLR));

    /* PIC BRINGUP WAR (PV# 856155):
     * On a PCI_X_ARB_ERR error interrupt clear the DEV_BROKE bits from
     * the b_arb register to re-enable the device.
     */
    if ((err_status & PIC_ISR_PCIX_ARB_ERR) &&
		PCIBR_WAR_ENABLED(PV856155, pcibr_soft)) {
	pcireg_arbitration_bit_set(pcibr_soft, (0xf << 20));
    }

    /* Zero out bserr_intstat field */
    pcibr_soft->bs_errinfo.bserr_intstat = 0;
    return IRQ_HANDLED;
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
static int
pcibr_addr_toslot(pcibr_soft_t pcibr_soft,
		  iopaddr_t pciaddr,
		  pciio_space_t *spacep,
		  iopaddr_t *offsetp,
		  pciio_function_t *funcp)
{
    int                     s, f = 0, w;
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
    for (s = pcibr_soft->bs_min_slot; s < PCIBR_NUM_SLOTS(pcibr_soft); ++s) {
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
    for (s = pcibr_soft->bs_min_slot; s < PCIBR_NUM_SLOTS(pcibr_soft); ++s) {
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
    uint64_t	clr_bits = BRIDGE_IRR_ALL_CLR;

    ASSERT(error_code & IOECODE_PIO);
    error_code = error_code;

    pcireg_intr_reset_set(pcibr_soft, clr_bits);

    pcireg_tflush_get(pcibr_soft);	/* flushbus */
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
pcibr_error_extract(vertex_hdl_t pcibr_vhdl,
		    pciio_space_t *spacep,
		    iopaddr_t *offsetp)
{
    pcibr_soft_t            pcibr_soft = 0;
    iopaddr_t               bserr_addr;
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
	bserr_addr = pcireg_pci_bus_addr_get(pcibr_soft);
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
 *
 * A note about slots:
 *
 * 	PIC-based bridges use zero-based device numbering when devices to
 * 	internal registers.  However, the physical slots are numbered using a
 *	one-based scheme because in PCI-X, device 0 is reserved (see comments
 * 	in pcibr_private.h for a better description).
 *
 * 	When building up the hwgraph, we use the external (one-based) number
 *	scheme when numbering slot components so that hwgraph more accuratly
 * 	reflects what is silkscreened on the bricks.
 *
 * 	Since pciio_error_handler() needs to ultimatly be able to do a hwgraph
 *	lookup, the ioerror that gets built up in pcibr_pioerror() encodes the
 *	external (one-based) slot number.  However, loops in pcibr_pioerror() 
 * 	which attempt to translate the virtual address into the correct
 * 	PCI physical address use the device (zero-based) numbering when 
 * 	walking through bridge structures.
 *
 * 	To that end, pcibr_pioerror() uses device to denote the 
 *	zero-based device number, and external_slot to denote the corresponding
 *	one-based slot number.  Loop counters (eg. cs) are always device based.
 */

/* BEM_ADD_IOE doesn't dump the whole ioerror, it just
 * decodes the PCI specific portions -- we count on our
 * callers to dump the raw IOE data.
 */
#define BEM_ADD_IOE(ioe)						\
	do {								\
	    if (IOERROR_FIELDVALID(ioe, busspace)) {			\
		iopaddr_t		spc;				\
		iopaddr_t		win;				\
		short			widdev;				\
		iopaddr_t		busaddr;			\
									\
		IOERROR_GETVALUE(spc, ioe, busspace);			\
		win = spc - PCIIO_SPACE_WIN(0);				\
		IOERROR_GETVALUE(busaddr, ioe, busaddr);		\
		IOERROR_GETVALUE(widdev, ioe, widgetdev);		\
									\
		switch (spc) {						\
		case PCIIO_SPACE_CFG:					\
		    printk("\tPCI Slot %d Func %d CFG space Offset 0x%lx\n",\
			    	pciio_widgetdev_slot_get(widdev),	\
	    			pciio_widgetdev_func_get(widdev),	\
				busaddr);				\
		    break;						\
		case PCIIO_SPACE_IO:					\
		    printk("\tPCI I/O space  Offset 0x%lx\n", busaddr);	\
		    break;						\
		case PCIIO_SPACE_MEM:					\
		case PCIIO_SPACE_MEM32:					\
		case PCIIO_SPACE_MEM64:					\
		    printk("\tPCI MEM space Offset 0x%lx\n", busaddr);	\
		    break;						\
		default:						\
		    if (win < 6) {					\
		    printk("\tPCI Slot %d Func %d Window %ld Offset 0x%lx\n",\
	    			pciio_widgetdev_slot_get(widdev),	\
	    			pciio_widgetdev_func_get(widdev),	\
			    	win,					\
			    	busaddr);				\
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

    vertex_hdl_t            pcibr_vhdl = pcibr_soft->bs_vhdl;
    iopaddr_t               bad_xaddr;

    pciio_space_t           raw_space;	/* raw PCI space */
    iopaddr_t               raw_paddr;	/* raw PCI address */

    pciio_space_t           space;	/* final PCI space */
    pciio_slot_t            device;	/* final PCI device if appropriate */
    pciio_slot_t            external_slot;/* external slot for device */
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

    IOERROR_GETVALUE(bad_xaddr, ioe, xtalkaddr);

    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_ERROR_HDLR, pcibr_soft->bs_conn,
                "pcibr_pioerror: pcibr_soft=0x%lx, bad_xaddr=0x%lx\n",
		pcibr_soft, bad_xaddr));

    device = PCIIO_SLOT_NONE;
    func = PCIIO_FUNC_NONE;
    raw_space = PCIIO_SPACE_NONE;
    raw_paddr = 0;

    if ((bad_xaddr >= PCIBR_BUS_TYPE0_CFG_DEV(pcibr_soft, 0)) &&
	(bad_xaddr < PCIBR_TYPE1_CFG(pcibr_soft))) {
	raw_paddr = bad_xaddr - PCIBR_BUS_TYPE0_CFG_DEV(pcibr_soft, 0);
	device = raw_paddr / BRIDGE_CONFIG_SLOT_SIZE;
	raw_paddr = raw_paddr % BRIDGE_CONFIG_SLOT_SIZE;
	raw_space = PCIIO_SPACE_CFG;
    }
    if ((bad_xaddr >= PCIBR_TYPE1_CFG(pcibr_soft)) &&
	(bad_xaddr < (PCIBR_TYPE1_CFG(pcibr_soft) + 0x1000))) {
	/* Type 1 config space:
	 * slot and function numbers not known.
	 * Perhaps we can read them back?
	 */
	raw_paddr = bad_xaddr - PCIBR_TYPE1_CFG(pcibr_soft);
	raw_space = PCIIO_SPACE_CFG;
    }
    if ((bad_xaddr >= PCIBR_BRIDGE_DEVIO(pcibr_soft, 0)) &&
	(bad_xaddr < PCIBR_BRIDGE_DEVIO(pcibr_soft, BRIDGE_DEV_CNT))) {
	int                     x;

	raw_paddr = bad_xaddr - PCIBR_BRIDGE_DEVIO(pcibr_soft, 0);
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

    if (IS_PIC_BUSNUM_SOFT(pcibr_soft, 0)) {
    	if ((bad_xaddr >= PICBRIDGE0_PCI_MEM32_BASE) &&
	    (bad_xaddr <= PICBRIDGE0_PCI_MEM32_LIMIT)) {
	    raw_space = PCIIO_SPACE_MEM32;
	    raw_paddr = bad_xaddr - PICBRIDGE0_PCI_MEM32_BASE;
    	}
    	if ((bad_xaddr >= PICBRIDGE0_PCI_MEM64_BASE) &&
	    (bad_xaddr <= PICBRIDGE0_PCI_MEM64_LIMIT)) {
	    raw_space = PCIIO_SPACE_MEM64;
	    raw_paddr = bad_xaddr - PICBRIDGE0_PCI_MEM64_BASE;
    	}
    } else if (IS_PIC_BUSNUM_SOFT(pcibr_soft, 1)) {
    	if ((bad_xaddr >= PICBRIDGE1_PCI_MEM32_BASE) &&
	    (bad_xaddr <= PICBRIDGE1_PCI_MEM32_LIMIT)) {
	    raw_space = PCIIO_SPACE_MEM32;
	    raw_paddr = bad_xaddr - PICBRIDGE1_PCI_MEM32_BASE;
    	}
    	if ((bad_xaddr >= PICBRIDGE1_PCI_MEM64_BASE) &&
	    (bad_xaddr <= PICBRIDGE1_PCI_MEM64_LIMIT)) {
	    raw_space = PCIIO_SPACE_MEM64;
	    raw_paddr = bad_xaddr - PICBRIDGE1_PCI_MEM64_BASE;
    	}
    } else {
	printk("pcibr_pioerror(): unknown bridge type");
	return IOERROR_UNHANDLED;
    }
    space = raw_space;
    offset = raw_paddr;

    if ((device == PCIIO_SLOT_NONE) && (space != PCIIO_SPACE_NONE)) {
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

	for (cs = pcibr_soft->bs_min_slot; 
		(cs < PCIBR_NUM_SLOTS(pcibr_soft)) && 
				(device == PCIIO_SLOT_NONE); cs++) {
	    int                     nf = pcibr_soft->bs_slot[cs].bss_ninfo;
	    pcibr_info_h            pcibr_infoh = pcibr_soft->bs_slot[cs].bss_infos;

	    for (cf = 0; (cf < nf) && (device == PCIIO_SLOT_NONE); cf++) {
		pcibr_info_t            pcibr_info = pcibr_infoh[cf];

		if (!pcibr_info)
		    continue;
		for (cw = 0; (cw < 6) && (device == PCIIO_SLOT_NONE); ++cw) {
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
			    device = cs;
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
    for (cs = pcibr_soft->bs_min_slot; 
				cs < PCIBR_NUM_SLOTS(pcibr_soft); ++cs) {
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
	    if (cw >= 0 && cw < 6) {
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
		atomic_inc(&map->bp_toc);
		if (device == PCIIO_SLOT_NONE) {
		    device = cs;
		    func = cf;
		    space = map->bp_space;
		    if (cw >= 0 && cw < 6)
			offset -= pcibr_soft->bs_slot[device].bss_window[cw].bssw_base;
		}

		break;
	    }
	    }
	}
    }

    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_ERROR_HDLR, pcibr_soft->bs_conn,
                "pcibr_pioerror: space=%d, offset=0x%lx, dev=0x%x, func=0x%x\n",
		space, offset, device, func));

    if (space != PCIIO_SPACE_NONE) {
	if (device != PCIIO_SLOT_NONE)  {
	    external_slot = PCIBR_DEVICE_TO_SLOT(pcibr_soft, device);

	    if (func != PCIIO_FUNC_NONE)
		IOERROR_SETVALUE(ioe, widgetdev, 
				 pciio_widgetdev_create(external_slot,func));
	    else
    		IOERROR_SETVALUE(ioe, widgetdev, 
				 pciio_widgetdev_create(external_slot,0));
	}
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
	return (device == PCIIO_SLOT_NONE) ? IOERROR_HANDLED :
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
		"\taccess to XIO bus offset 0x%lx\n"
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
	    BEM_ADD_IOE(ioe);
	}

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
	 *      some other error has occurred, that would be
	 *      implicitly cleared by this write.
	 *      Need a way to ensure we don't inadvertently clear some
	 *      other errors.
	 */
	if (IOERROR_FIELDVALID(ioe, widgetdev)) {
	    short widdev;
	    IOERROR_GETVALUE(widdev, ioe, widgetdev);
	    external_slot = pciio_widgetdev_slot_get(widdev);
	    device = PCIBR_SLOT_TO_DEVICE(pcibr_soft, external_slot);
	    pcibr_device_disable(pcibr_soft, device);
	}
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

int
pcibr_dmard_error(
		     pcibr_soft_t pcibr_soft,
		     int error_code,
		     ioerror_mode_t mode,
		     ioerror_t *ioe)
{
    vertex_hdl_t            pcibr_vhdl = pcibr_soft->bs_vhdl;
    int                     retval = 0;
    int                     bufnum, device;

    /*
     * In case of DMA errors, bridge should have logged the
     * address that caused the error.
     * Look up the address, in the bridge error registers, and
     * take appropriate action
     */
    {
	short tmp;
	IOERROR_GETVALUE(tmp, ioe, widgetnum);
	ASSERT(tmp == pcibr_soft->bs_xid);
    }

    /*
     * read error log registers
     */
    bufnum = pcireg_resp_err_buf_get(pcibr_soft);
    device = pcireg_resp_err_dev_get(pcibr_soft);
    IOERROR_SETVALUE(ioe, widgetdev, pciio_widgetdev_create(device, 0));
    IOERROR_SETVALUE(ioe, busaddr, pcireg_resp_err_get(pcibr_soft));

    /*
     * need to ensure that the xtalk address in ioe
     * maps to PCI error address read from bridge.
     * How to convert PCI address back to Xtalk address ?
     * (better idea: convert XTalk address to PCI address
     * and then do the compare!)
     */

    retval = pciio_error_handler(pcibr_vhdl, error_code, mode, ioe);
    if (retval != IOERROR_HANDLED) {
	short tmp;
	IOERROR_GETVALUE(tmp, ioe, widgetdev);
	pcibr_device_disable(pcibr_soft, pciio_widgetdev_slot_get(tmp));
    }

    /*
     * Re-enable bridge to interrupt on BRIDGE_IRR_RESP_BUF_GRP_CLR
     * NOTE: Wheather we get the interrupt on BRIDGE_IRR_RESP_BUF_GRP_CLR or
     * not is dependent on INT_ENABLE register. This write just makes sure
     * that if the interrupt was enabled, we do get the interrupt.
     */
    pcireg_intr_reset_set(pcibr_soft, BRIDGE_IRR_RESP_BUF_GRP_CLR);

    /*
     * Also, release the "bufnum" back to buffer pool that could be re-used.
     * This is done by "disabling" the buffer for a moment, then restoring
     * the original assignment.
     */

    {
	uint64_t		rrb_reg;
	uint64_t		mask;

	rrb_reg = pcireg_rrb_get(pcibr_soft, (bufnum & 1));
	mask = 0xF << ((bufnum >> 1) * 4);
	pcireg_rrb_set(pcibr_soft, (bufnum & 1), (rrb_reg & ~mask));
	pcireg_rrb_set(pcibr_soft, (bufnum & 1), rrb_reg);
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
    vertex_hdl_t            pcibr_vhdl = pcibr_soft->bs_vhdl;
    int                     retval;

    retval = pciio_error_handler(pcibr_vhdl, error_code, mode, ioe);

    if (retval != IOERROR_HANDLED) {
	short tmp;

	IOERROR_GETVALUE(tmp, ioe, widgetdev);
	pcibr_device_disable(pcibr_soft, pciio_widgetdev_slot_get(tmp));
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

    pcibr_soft = (pcibr_soft_t) einfo;

    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_ERROR_HDLR, pcibr_soft->bs_conn,
		"pcibr_error_handler: pcibr_soft=0x%lx, error_code=0x%x\n",
		pcibr_soft, error_code));

#if DEBUG && ERROR_DEBUG
    printk( "%s: pcibr_error_handler\n", pcibr_soft->bs_name);
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
 * PIC has 2 busses under a single widget so pcibr_attach2 registers this
 * wrapper function rather than pcibr_error_handler() for PIC.  It's upto
 * this wrapper to call pcibr_error_handler() with the correct pcibr_soft
 * struct (ie. the pcibr_soft struct for the bus that saw the error).
 *
 * NOTE: this wrapper function is only registered for PIC ASICs and will
 * only be called for a PIC
 */
int
pcibr_error_handler_wrapper(
		       error_handler_arg_t einfo,
		       int error_code,
		       ioerror_mode_t mode,
		       ioerror_t *ioe)
{
    pcibr_soft_t       pcibr_soft = (pcibr_soft_t) einfo;
    int                pio_retval = -1; 
    int		       dma_retval = -1;

    PCIBR_DEBUG_ALWAYS((PCIBR_DEBUG_ERROR_HDLR, pcibr_soft->bs_conn,
                "pcibr_error_handler_wrapper: pcibr_soft=0x%lx, "
		"error_code=0x%x\n", pcibr_soft, error_code));

    /*
     * It is possible that both a IOECODE_PIO and a IOECODE_DMA, and both
     * IOECODE_READ and IOECODE_WRITE could be set in error_code so we must
     * process all.  Since we are a wrapper for pcibr_error_handler(), and
     * will be calling it several times within this routine, we turn off the
     * error_code bits we don't want it to be processing during that call.
     */
    /* 
     * If the error was a result of a PIO, we tell what bus on the PIC saw
     * the error from the PIO address.
     */

    if (error_code & IOECODE_PIO) {
	iopaddr_t               bad_xaddr;
	/*
	 * PIC bus0 PIO space 0x000000 - 0x7fffff or 0x40000000 - 0xbfffffff
	 *     bus1 PIO space 0x800000 - 0xffffff or 0xc0000000 - 0x13fffffff
	 */
	IOERROR_GETVALUE(bad_xaddr, ioe, xtalkaddr);
	if ((bad_xaddr <= 0x7fffff) ||
	    ((bad_xaddr >= 0x40000000) && (bad_xaddr <= 0xbfffffff))) {
	    /* bus 0 saw the error */
	    pio_retval = pcibr_error_handler((error_handler_arg_t)pcibr_soft,
			 (error_code & ~IOECODE_DMA), mode, ioe);
	} else if (((bad_xaddr >= 0x800000) && (bad_xaddr <= 0xffffff)) ||
	    ((bad_xaddr >= 0xc0000000) && (bad_xaddr <= 0x13fffffff))) {
	    /* bus 1 saw the error */
	    pcibr_soft = pcibr_soft->bs_peers_soft;
	    if (!pcibr_soft) {
#if DEBUG
		printk(KERN_WARNING "pcibr_error_handler: "
			"bs_peers_soft==NULL. bad_xaddr= 0x%lx mode= 0x%lx\n",
						bad_xaddr, mode);
#endif
  		pio_retval = IOERROR_HANDLED;
	    } else
	        pio_retval= pcibr_error_handler((error_handler_arg_t)pcibr_soft,
			 (error_code & ~IOECODE_DMA), mode, ioe);
	} else {
	    printk(KERN_WARNING "pcibr_error_handler_wrapper(): IOECODE_PIO: "
		    "saw an invalid pio address: 0x%lx\n", bad_xaddr);
	    pio_retval = IOERROR_UNHANDLED;
	}
    } 

    /* 
     * If the error was a result of a DMA Write, we tell what bus on the PIC
     * saw the error by looking at tnum.
     */
    if ((error_code & IOECODE_DMA) && (error_code & IOECODE_WRITE)) {
	short tmp;
	/*
         * For DMA writes [X]Bridge encodes the TNUM field of a Xtalk
         * packet like this:
         *              bits  value
         *              4:3   10b
         *              2:0   device number
         *
         * BUT PIC needs the bus number so it does this:
         *              bits  value
         *              4:3   10b
         *              2     busnumber
         *              1:0   device number
	 *
	 * Pull out the bus number from `tnum' and reset the `widgetdev'
	 * since when hubiio_crb_error_handler() set `widgetdev' it had
	 * no idea if it was a PIC or a BRIDGE ASIC so it set it based
	 * off bits 2:0
	 */
	IOERROR_GETVALUE(tmp, ioe, tnum);
	IOERROR_SETVALUE(ioe, widgetdev, (tmp & 0x3));
	if ((tmp & 0x4) == 0) {
	    /* bus 0 saw the error. */
	    dma_retval = pcibr_error_handler((error_handler_arg_t)pcibr_soft,
			 (error_code & ~(IOECODE_PIO|IOECODE_READ)), mode, ioe);
	} else {
	    /* bus 1 saw the error */
	    pcibr_soft = pcibr_soft->bs_peers_soft;
	    dma_retval = pcibr_error_handler((error_handler_arg_t)pcibr_soft,
			 (error_code & ~(IOECODE_PIO|IOECODE_READ)), mode, ioe);
	}
    } 
    
    /* 
     * If the error was a result of a DMA READ, XXX ???
     */
    if ((error_code & IOECODE_DMA) && (error_code & IOECODE_READ)) {
	/*
	 * A DMA Read error will result in a BRIDGE_ISR_RESP_XTLK_ERR
	 * or BRIDGE_ISR_BAD_XRESP_PKT bridge error interrupt which 
	 * are fatal interrupts (ie. BRIDGE_ISR_ERROR_FATAL) causing
	 * pcibr_error_intr_handler() to panic the system.  So is the
	 * error handler even going to get called???  It appears that
	 * the pcibr_dmard_error() attempts to clear the interrupts
	 * so pcibr_error_intr_handler() won't see them, but there
	 * appears to be nothing to prevent pcibr_error_intr_handler()
	 * from running before pcibr_dmard_error() has a chance to
	 * clear the interrupt.
	 *
	 * Since we'll be panicing anyways, don't bother handling the
	 * error for now until we can fix this race condition mentioned
	 * above.
	 */
	dma_retval = IOERROR_UNHANDLED;
    } 
    
    /* XXX: pcibr_error_handler() should probably do the same thing, it over-
     * write it's return value as it processes the different "error_code"s.
     */
    if ((pio_retval == -1) && (dma_retval == -1)) {
    	return IOERROR_BADERRORCODE;
    } else if ((dma_retval != IOERROR_HANDLED) && (dma_retval != -1)) {
	return dma_retval;
    } else if ((pio_retval != IOERROR_HANDLED) && (pio_retval != -1)) {
	return pio_retval;
    } else {
	return IOERROR_HANDLED;
    }
}
