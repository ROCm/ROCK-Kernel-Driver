/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */
#ifndef _ASM_IA64_SN_PCI_PIC_H
#define _ASM_IA64_SN_PCI_PIC_H

/*
 * PIC AS DEVICE ZERO
 * ------------------
 *
 * PIC handles PCI/X busses.  PCI/X requires that the 'bridge' (i.e. PIC)
 * be designated as 'device 0'.   That is a departure from earlier SGI
 * PCI bridges.  Because of that we use config space 1 to access the
 * config space of the first actual PCI device on the bus. 
 * Here's what the PIC manual says:
 *
 *     The current PCI-X bus specification now defines that the parent
 *     hosts bus bridge (PIC for example) must be device 0 on bus 0. PIC
 *     reduced the total number of devices from 8 to 4 and removed the
 *     device registers and windows, now only supporting devices 0,1,2, and
 *     3. PIC did leave all 8 configuration space windows. The reason was
 *     there was nothing to gain by removing them. Here in lies the problem.
 *     The device numbering we do using 0 through 3 is unrelated to the device
 *     numbering which PCI-X requires in configuration space. In the past we
 *     correlated Configs pace and our device space 0 <-> 0, 1 <-> 1, etc.
 *     PCI-X requires we start a 1, not 0 and currently the PX brick
 *     does associate our:
 * 
 *         device 0 with configuration space window 1,
 *         device 1 with configuration space window 2, 
 *         device 2 with configuration space window 3,
 *         device 3 with configuration space window 4.
 *
 * The net effect is that all config space access are off-by-one with 
 * relation to other per-slot accesses on the PIC.   
 * Here is a table that shows some of that:
 *
 *                               Internal Slot#
 *           |
 *           |     0         1        2         3
 * ----------|---------------------------------------
 * config    |  0x21000   0x22000  0x23000   0x24000
 *           |
 * even rrb  |  0[0]      n/a      1[0]      n/a	[] == implied even/odd
 *           |
 * odd rrb   |  n/a       0[1]     n/a       1[1]
 *           |
 * int dev   |  00       01        10        11
 *           |
 * ext slot# |  1        2         3         4
 * ----------|---------------------------------------
 */


#ifdef __KERNEL__
#include <linux/types.h>
#include <asm/sn/xtalk/xwidget.h>	/* generic widget header */
#else
#include <xtalk/xwidget.h>
#endif

#include <asm/sn/pci/pciio.h>


/*
 *    bus provider function table
 *
 *	Normally, this table is only handed off explicitly
 *	during provider initialization, and the PCI generic
 *	layer will stash a pointer to it in the vertex; however,
 *	exporting it explicitly enables a performance hack in
 *	the generic PCI provider where if we know at compile
 *	time that the only possible PCI provider is a
 *	pcibr, we can go directly to this ops table.
 */

extern pciio_provider_t pci_pic_provider;


/*
 * misc defines
 *
 */

#define PIC_WIDGET_PART_NUM_BUS0 0xd102
#define PIC_WIDGET_PART_NUM_BUS1 0xd112
#define PIC_WIDGET_MFGR_NUM 0x24
#define PIC_WIDGET_REV_A  0x1
#define PIC_WIDGET_REV_B  0x2
#define PIC_WIDGET_REV_C  0x3

#define PIC_XTALK_ADDR_MASK                     0x0000FFFFFFFFFFFF
#define PIC_INTERNAL_ATES                       1024


#define IS_PIC_PART_REV_A(rev) \
	((rev == (PIC_WIDGET_PART_NUM_BUS0 << 4 | PIC_WIDGET_REV_A)) || \
	(rev == (PIC_WIDGET_PART_NUM_BUS1 << 4 | PIC_WIDGET_REV_A)))
#define IS_PIC_PART_REV_B(rev) \
        ((rev == (PIC_WIDGET_PART_NUM_BUS0 << 4 | PIC_WIDGET_REV_B)) || \
        (rev == (PIC_WIDGET_PART_NUM_BUS1 << 4 | PIC_WIDGET_REV_B)))
#define IS_PIC_PART_REV_C(rev) \
        ((rev == (PIC_WIDGET_PART_NUM_BUS0 << 4 | PIC_WIDGET_REV_C)) || \
        (rev == (PIC_WIDGET_PART_NUM_BUS1 << 4 | PIC_WIDGET_REV_C)))


/*
 * misc typedefs
 *
 */
typedef uint64_t picreg_t;
typedef uint64_t picate_t;

/*
 * PIC Bridge MMR defines
 */

/*
 * PIC STATUS register          offset 0x00000008
 */

#define PIC_STAT_PCIX_ACTIVE_SHFT       33

/*
 * PIC CONTROL register         offset 0x00000020
 */

#define PIC_CTRL_PCI_SPEED_SHFT         4
#define PIC_CTRL_PCI_SPEED              (0x3 << PIC_CTRL_PCI_SPEED_SHFT)
#define PIC_CTRL_PAGE_SIZE_SHFT         21
#define PIC_CTRL_PAGE_SIZE              (0x1 << PIC_CTRL_PAGE_SIZE_SHFT)


/*
 * PIC Intr Destination Addr    offset 0x00000038
 */

#define PIC_INTR_DEST_ADDR              0x0000FFFFFFFFFFFF
#define PIC_INTR_DEST_TID_SHFT          48
#define PIC_INTR_DEST_TID               (0xFull << PIC_INTR_DEST_TID_SHFT)

/*
 * PIC PCI Responce Buffer      offset 0x00000068
 */
#define PIC_RSP_BUF_ADDR                0x0000FFFFFFFFFFFF
#define PIC_RSP_BUF_NUM_SHFT            48
#define PIC_RSP_BUF_NUM                 (0xFull << PIC_RSP_BUF_NUM_SHFT)
#define PIC_RSP_BUF_DEV_NUM_SHFT        52
#define PIC_RSP_BUF_DEV_NUM             (0x3ull << PIC_RSP_BUF_DEV_NUM_SHFT)

/*
 * PIC PCI DIRECT MAP register  offset 0x00000080
 */
#define PIC_DIRMAP_DIROFF_SHFT          0
#define PIC_DIRMAP_DIROFF               (0x1FFFF << PIC_DIRMAP_DIROFF_SHFT)
#define PIC_DIRMAP_ADD512_SHFT          17
#define PIC_DIRMAP_ADD512               (0x1 << PIC_DIRMAP_ADD512_SHFT)
#define PIC_DIRMAP_WID_SHFT             20
#define PIC_DIRMAP_WID                  (0xF << PIC_DIRMAP_WID_SHFT)

#define PIC_DIRMAP_OFF_ADDRSHFT         31

/*
 * Interrupt Status register            offset 0x00000100
 */
#define PIC_ISR_PCIX_SPLIT_MSG_PE     (0x1ull << 45)
#define PIC_ISR_PCIX_SPLIT_EMSG       (0x1ull << 44)
#define PIC_ISR_PCIX_SPLIT_TO         (0x1ull << 43)
#define PIC_ISR_PCIX_UNEX_COMP        (0x1ull << 42)
#define PIC_ISR_INT_RAM_PERR          (0x1ull << 41)
#define PIC_ISR_PCIX_ARB_ERR          (0x1ull << 40)
#define PIC_ISR_PCIX_REQ_TOUT         (0x1ull << 39)
#define PIC_ISR_PCIX_TABORT           (0x1ull << 38)
#define PIC_ISR_PCIX_PERR             (0x1ull << 37)
#define PIC_ISR_PCIX_SERR             (0x1ull << 36)
#define PIC_ISR_PCIX_MRETRY           (0x1ull << 35)
#define PIC_ISR_PCIX_MTOUT            (0x1ull << 34)
#define PIC_ISR_PCIX_DA_PARITY        (0x1ull << 33)
#define PIC_ISR_PCIX_AD_PARITY        (0x1ull << 32)
#define PIC_ISR_PMU_PAGE_FAULT        (0x1ull << 30)
#define PIC_ISR_UNEXP_RESP            (0x1ull << 29)
#define PIC_ISR_BAD_XRESP_PKT         (0x1ull << 28)
#define PIC_ISR_BAD_XREQ_PKT          (0x1ull << 27)
#define PIC_ISR_RESP_XTLK_ERR         (0x1ull << 26)
#define PIC_ISR_REQ_XTLK_ERR          (0x1ull << 25)
#define PIC_ISR_INVLD_ADDR            (0x1ull << 24)
#define PIC_ISR_UNSUPPORTED_XOP       (0x1ull << 23)
#define PIC_ISR_XREQ_FIFO_OFLOW       (0x1ull << 22)
#define PIC_ISR_LLP_REC_SNERR         (0x1ull << 21)
#define PIC_ISR_LLP_REC_CBERR         (0x1ull << 20)
#define PIC_ISR_LLP_RCTY              (0x1ull << 19)
#define PIC_ISR_LLP_TX_RETRY          (0x1ull << 18)
#define PIC_ISR_LLP_TCTY              (0x1ull << 17)
#define PIC_ISR_PCI_ABORT             (0x1ull << 15)
#define PIC_ISR_PCI_PARITY            (0x1ull << 14)
#define PIC_ISR_PCI_SERR              (0x1ull << 13)
#define PIC_ISR_PCI_PERR              (0x1ull << 12)
#define PIC_ISR_PCI_MST_TIMEOUT       (0x1ull << 11)
#define PIC_ISR_PCI_RETRY_CNT         (0x1ull << 10)
#define PIC_ISR_XREAD_REQ_TIMEOUT     (0x1ull << 9)
#define PIC_ISR_INT_MSK               (0xffull << 0)
#define PIC_ISR_INT(x)                (0x1ull << (x))

#define PIC_ISR_LINK_ERROR            \
                (PIC_ISR_LLP_REC_SNERR|PIC_ISR_LLP_REC_CBERR|       \
                 PIC_ISR_LLP_RCTY|PIC_ISR_LLP_TX_RETRY|             \
                 PIC_ISR_LLP_TCTY)

#define PIC_ISR_PCIBUS_PIOERR         \
                (PIC_ISR_PCI_MST_TIMEOUT|PIC_ISR_PCI_ABORT|         \
                 PIC_ISR_PCIX_MTOUT|PIC_ISR_PCIX_TABORT)

#define PIC_ISR_PCIBUS_ERROR          \
                (PIC_ISR_PCIBUS_PIOERR|PIC_ISR_PCI_PERR|            \
                 PIC_ISR_PCI_SERR|PIC_ISR_PCI_RETRY_CNT|            \
                 PIC_ISR_PCI_PARITY|PIC_ISR_PCIX_PERR|              \
                 PIC_ISR_PCIX_SERR|PIC_ISR_PCIX_MRETRY|             \
                 PIC_ISR_PCIX_AD_PARITY|PIC_ISR_PCIX_DA_PARITY|     \
                 PIC_ISR_PCIX_REQ_TOUT|PIC_ISR_PCIX_UNEX_COMP|      \
                 PIC_ISR_PCIX_SPLIT_TO|PIC_ISR_PCIX_SPLIT_EMSG|     \
                 PIC_ISR_PCIX_SPLIT_MSG_PE)

#define PIC_ISR_XTALK_ERROR           \
                (PIC_ISR_XREAD_REQ_TIMEOUT|PIC_ISR_XREQ_FIFO_OFLOW| \
                 PIC_ISR_UNSUPPORTED_XOP|PIC_ISR_INVLD_ADDR|        \
                 PIC_ISR_REQ_XTLK_ERR|PIC_ISR_RESP_XTLK_ERR|        \
                 PIC_ISR_BAD_XREQ_PKT|PIC_ISR_BAD_XRESP_PKT|        \
                 PIC_ISR_UNEXP_RESP)

#define PIC_ISR_ERRORS                \
                (PIC_ISR_LINK_ERROR|PIC_ISR_PCIBUS_ERROR|           \
                 PIC_ISR_XTALK_ERROR|                                 \
                 PIC_ISR_PMU_PAGE_FAULT|PIC_ISR_INT_RAM_PERR)

/*
 * PIC RESET INTR register      offset 0x00000110
 */

#define PIC_IRR_ALL_CLR                 0xffffffffffffffff

/*
 * PIC PCI Host Intr Addr       offset 0x00000130 - 0x00000168
 */
#define PIC_HOST_INTR_ADDR              0x0000FFFFFFFFFFFF
#define PIC_HOST_INTR_FLD_SHFT          48
#define PIC_HOST_INTR_FLD               (0xFFull << PIC_HOST_INTR_FLD_SHFT)


/*
 * PIC MMR structure mapping
 */

/* NOTE: PIC WAR. PV#854697.  PIC does not allow writes just to [31:0]
 * of a 64-bit register.  When writing PIC registers, always write the 
 * entire 64 bits.
 */

typedef volatile struct pic_s {

    /* 0x000000-0x00FFFF -- Local Registers */

    /* 0x000000-0x000057 -- Standard Widget Configuration */
    picreg_t		p_wid_id;			/* 0x000000 */
    picreg_t		p_wid_stat;			/* 0x000008 */
    picreg_t		p_wid_err_upper;		/* 0x000010 */
    picreg_t		p_wid_err_lower;		/* 0x000018 */
    #define p_wid_err p_wid_err_lower
    picreg_t		p_wid_control;			/* 0x000020 */
    picreg_t		p_wid_req_timeout;		/* 0x000028 */
    picreg_t		p_wid_int_upper;		/* 0x000030 */
    picreg_t		p_wid_int_lower;		/* 0x000038 */
    #define p_wid_int p_wid_int_lower
    picreg_t		p_wid_err_cmdword;		/* 0x000040 */
    picreg_t		p_wid_llp;			/* 0x000048 */
    picreg_t		p_wid_tflush;			/* 0x000050 */

    /* 0x000058-0x00007F -- Bridge-specific Widget Configuration */
    picreg_t		p_wid_aux_err;			/* 0x000058 */
    picreg_t		p_wid_resp_upper;		/* 0x000060 */
    picreg_t		p_wid_resp_lower;		/* 0x000068 */
    #define p_wid_resp p_wid_resp_lower
    picreg_t		p_wid_tst_pin_ctrl;		/* 0x000070 */
    picreg_t		p_wid_addr_lkerr;		/* 0x000078 */

    /* 0x000080-0x00008F -- PMU & MAP */
    picreg_t		p_dir_map;			/* 0x000080 */
    picreg_t		_pad_000088;			/* 0x000088 */

    /* 0x000090-0x00009F -- SSRAM */
    picreg_t		p_map_fault;			/* 0x000090 */
    picreg_t		_pad_000098;			/* 0x000098 */

    /* 0x0000A0-0x0000AF -- Arbitration */
    picreg_t		p_arb;				/* 0x0000A0 */
    picreg_t		_pad_0000A8;			/* 0x0000A8 */

    /* 0x0000B0-0x0000BF -- Number In A Can or ATE Parity Error */
    picreg_t		p_ate_parity_err;		/* 0x0000B0 */
    picreg_t		_pad_0000B8;			/* 0x0000B8 */

    /* 0x0000C0-0x0000FF -- PCI/GIO */
    picreg_t		p_bus_timeout;			/* 0x0000C0 */
    picreg_t		p_pci_cfg;			/* 0x0000C8 */
    picreg_t		p_pci_err_upper;		/* 0x0000D0 */
    picreg_t		p_pci_err_lower;		/* 0x0000D8 */
    #define p_pci_err p_pci_err_lower
    picreg_t		_pad_0000E0[4];			/* 0x0000{E0..F8} */

    /* 0x000100-0x0001FF -- Interrupt */
    picreg_t		p_int_status;			/* 0x000100 */
    picreg_t		p_int_enable;			/* 0x000108 */
    picreg_t		p_int_rst_stat;			/* 0x000110 */
    picreg_t		p_int_mode;			/* 0x000118 */
    picreg_t		p_int_device;			/* 0x000120 */
    picreg_t		p_int_host_err;			/* 0x000128 */
    picreg_t		p_int_addr[8];			/* 0x0001{30,,,68} */
    picreg_t		p_err_int_view;			/* 0x000170 */
    picreg_t		p_mult_int;			/* 0x000178 */
    picreg_t		p_force_always[8];		/* 0x0001{80,,,B8} */
    picreg_t		p_force_pin[8];			/* 0x0001{C0,,,F8} */

    /* 0x000200-0x000298 -- Device */
    picreg_t		p_device[4];			/* 0x0002{00,,,18} */
    picreg_t		_pad_000220[4];			/* 0x0002{20,,,38} */
    picreg_t		p_wr_req_buf[4];		/* 0x0002{40,,,58} */
    picreg_t		_pad_000260[4];			/* 0x0002{60,,,78} */
    picreg_t		p_rrb_map[2];			/* 0x0002{80,,,88} */
    #define p_even_resp p_rrb_map[0]			/* 0x000280 */
    #define p_odd_resp  p_rrb_map[1]			/* 0x000288 */
    picreg_t		p_resp_status;			/* 0x000290 */
    picreg_t		p_resp_clear;			/* 0x000298 */

    picreg_t		_pad_0002A0[12];		/* 0x0002{A0..F8} */

    /* 0x000300-0x0003F8 -- Buffer Address Match Registers */
    struct {
	picreg_t	upper;				/* 0x0003{00,,,F0} */
	picreg_t	lower;				/* 0x0003{08,,,F8} */
    } p_buf_addr_match[16];

    /* 0x000400-0x0005FF -- Performance Monitor Registers (even only) */
    struct {
	picreg_t	flush_w_touch;			/* 0x000{400,,,5C0} */
	picreg_t	flush_wo_touch;			/* 0x000{408,,,5C8} */
	picreg_t	inflight;			/* 0x000{410,,,5D0} */
	picreg_t	prefetch;			/* 0x000{418,,,5D8} */
	picreg_t	total_pci_retry;		/* 0x000{420,,,5E0} */
	picreg_t	max_pci_retry;			/* 0x000{428,,,5E8} */
	picreg_t	max_latency;			/* 0x000{430,,,5F0} */
	picreg_t	clear_all;			/* 0x000{438,,,5F8} */
    } p_buf_count[8];

    
    /* 0x000600-0x0009FF -- PCI/X registers */
    picreg_t		p_pcix_bus_err_addr;		/* 0x000600 */
    picreg_t		p_pcix_bus_err_attr;		/* 0x000608 */
    picreg_t		p_pcix_bus_err_data;		/* 0x000610 */
    picreg_t		p_pcix_pio_split_addr;		/* 0x000618 */
    picreg_t		p_pcix_pio_split_attr;		/* 0x000620 */
    picreg_t		p_pcix_dma_req_err_attr;	/* 0x000628 */
    picreg_t		p_pcix_dma_req_err_addr;	/* 0x000630 */
    picreg_t		p_pcix_timeout;			/* 0x000638 */

    picreg_t		_pad_000640[120];		/* 0x000{640,,,9F8} */

    /* 0x000A00-0x000BFF -- PCI/X Read&Write Buffer */
    struct {
	picreg_t	p_buf_addr;			/* 0x000{A00,,,AF0} */
	picreg_t	p_buf_attr;			/* 0X000{A08,,,AF8} */
    } p_pcix_read_buf_64[16];

    struct {
	picreg_t	p_buf_addr;			/* 0x000{B00,,,BE0} */
	picreg_t	p_buf_attr;			/* 0x000{B08,,,BE8} */
	picreg_t	p_buf_valid;			/* 0x000{B10,,,BF0} */
	picreg_t	__pad1;				/* 0x000{B18,,,BF8} */
    } p_pcix_write_buf_64[8];

    /* End of Local Registers -- Start of Address Map space */

    char		_pad_000c00[0x010000 - 0x000c00];

    /* 0x010000-0x011fff -- Internal ATE RAM (Auto Parity Generation) */
    picate_t		p_int_ate_ram[1024];		/* 0x010000-0x011fff */

    /* 0x012000-0x013fff -- Internal ATE RAM (Manual Parity Generation) */
    picate_t		p_int_ate_ram_mp[1024];		/* 0x012000-0x013fff */

    char		_pad_014000[0x18000 - 0x014000];

    /* 0x18000-0x197F8 -- PIC Write Request Ram */
    picreg_t		p_wr_req_lower[256];		/* 0x18000 - 0x187F8 */
    picreg_t		p_wr_req_upper[256];		/* 0x18800 - 0x18FF8 */
    picreg_t		p_wr_req_parity[256];		/* 0x19000 - 0x197F8 */

    char		_pad_019800[0x20000 - 0x019800];

    /* 0x020000-0x027FFF -- PCI Device Configuration Spaces */
    union {
	uint8_t		c[0x1000 / 1];			/* 0x02{0000,,,7FFF} */
	uint16_t	s[0x1000 / 2];			/* 0x02{0000,,,7FFF} */
	uint32_t	l[0x1000 / 4];			/* 0x02{0000,,,7FFF} */
	uint64_t	d[0x1000 / 8];			/* 0x02{0000,,,7FFF} */
	union {
	    uint8_t	c[0x100 / 1];
	    uint16_t	s[0x100 / 2];
	    uint32_t	l[0x100 / 4];
	    uint64_t	d[0x100 / 8];
	} f[8];
    } p_type0_cfg_dev[8];				/* 0x02{0000,,,7FFF} */

    /* 0x028000-0x028FFF -- PCI Type 1 Configuration Space */
    union {
	uint8_t		c[0x1000 / 1];			/* 0x028000-0x029000 */
	uint16_t	s[0x1000 / 2];			/* 0x028000-0x029000 */
	uint32_t	l[0x1000 / 4];			/* 0x028000-0x029000 */
	uint64_t	d[0x1000 / 8];			/* 0x028000-0x029000 */
	union {
	    uint8_t	c[0x100 / 1];
	    uint16_t	s[0x100 / 2];
	    uint32_t	l[0x100 / 4];
	    uint64_t	d[0x100 / 8];
	} f[8];
    } p_type1_cfg;					/* 0x028000-0x029000 */

    char		_pad_029000[0x030000-0x029000];

    /* 0x030000-0x030007 -- PCI Interrupt Acknowledge Cycle */
    union {
	uint8_t		c[8 / 1];
	uint16_t	s[8 / 2];
	uint32_t	l[8 / 4];
	uint64_t	d[8 / 8];
    } p_pci_iack;					/* 0x030000-0x030007 */

    char		_pad_030007[0x040000-0x030008];

    /* 0x040000-0x030007 -- PCIX Special Cycle */
    union {
	uint8_t		c[8 / 1];
	uint16_t	s[8 / 2];
	uint32_t	l[8 / 4];
	uint64_t	d[8 / 8];
    } p_pcix_cycle;					/* 0x040000-0x040007 */
} pic_t;

#endif                          /* _ASM_IA64_SN_PCI_PIC_H */
