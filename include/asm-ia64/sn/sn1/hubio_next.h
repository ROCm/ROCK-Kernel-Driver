/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_SN1_HUBIO_NEXT_H
#define _ASM_SN_SN1_HUBIO_NEXT_H

/*
 * Slightly friendlier names for some common registers.
 */
#define IIO_WIDGET              IIO_WID      /* Widget identification */
#define IIO_WIDGET_STAT         IIO_WSTAT    /* Widget status register */
#define IIO_WIDGET_CTRL         IIO_WCR      /* Widget control register */
#define IIO_PROTECT             IIO_ILAPR    /* IO interface protection */
#define IIO_PROTECT_OVRRD       IIO_ILAPO    /* IO protect override */
#define IIO_OUTWIDGET_ACCESS    IIO_IOWA     /* Outbound widget access */
#define IIO_INWIDGET_ACCESS     IIO_IIWA     /* Inbound widget access */
#define IIO_INDEV_ERR_MASK      IIO_IIDEM    /* Inbound device error mask */
#define IIO_LLP_CSR             IIO_ILCSR    /* LLP control and status */
#define IIO_LLP_LOG             IIO_ILLR     /* LLP log */
#define IIO_XTALKCC_TOUT        IIO_IXCC     /* Xtalk credit count timeout*/
#define IIO_XTALKTT_TOUT        IIO_IXTT     /* Xtalk tail timeout */
#define IIO_IO_ERR_CLR          IIO_IECLR    /* IO error clear */
#define IIO_IGFX_0 		IIO_IGFX0
#define IIO_IGFX_1 		IIO_IGFX1
#define IIO_IBCT_0		IIO_IBCT0
#define IIO_IBCT_1		IIO_IBCT1
#define IIO_IBLS_0		IIO_IBLS0
#define IIO_IBLS_1		IIO_IBLS1
#define IIO_IBSA_0		IIO_IBSA0
#define IIO_IBSA_1		IIO_IBSA1
#define IIO_IBDA_0		IIO_IBDA0
#define IIO_IBDA_1		IIO_IBDA1
#define IIO_IBNA_0		IIO_IBNA0
#define IIO_IBNA_1		IIO_IBNA1
#define IIO_IBIA_0		IIO_IBIA0
#define IIO_IBIA_1		IIO_IBIA1
#define IIO_IOPRB_0		IIO_IPRB0
#define IIO_PRTE_0      	IIO_IPRTE0        /* PIO Read address table entry 0 */
#define IIO_PRTE(_x)    	(IIO_PRTE_0 + (8 * (_x)))

#define IIO_LLP_CSR_IS_UP               0x00002000
#define IIO_LLP_CSR_LLP_STAT_MASK       0x00003000
#define IIO_LLP_CSR_LLP_STAT_SHFT       12

#define IIO_LLP_CB_MAX  0xffff	/* in ILLR CB_CNT, Max Check Bit errors */
#define IIO_LLP_SN_MAX  0xffff	/* in ILLR SN_CNT, Max Sequence Number errors */

/* key to IIO_PROTECT_OVRRD */
#define IIO_PROTECT_OVRRD_KEY   0x53474972756c6573ull   /* "SGIrules" */

/* BTE register names */
#define IIO_BTE_STAT_0          IIO_IBLS_0   /* Also BTE length/status 0 */
#define IIO_BTE_SRC_0           IIO_IBSA_0   /* Also BTE source address  0 */
#define IIO_BTE_DEST_0          IIO_IBDA_0   /* Also BTE dest. address 0 */
#define IIO_BTE_CTRL_0          IIO_IBCT_0   /* Also BTE control/terminate 0 */
#define IIO_BTE_NOTIFY_0        IIO_IBNA_0   /* Also BTE notification 0 */
#define IIO_BTE_INT_0           IIO_IBIA_0   /* Also BTE interrupt 0 */
#define IIO_BTE_OFF_0           0            /* Base offset from BTE 0 regs. */
#define IIO_BTE_OFF_1   IIO_IBLS_1 - IIO_IBLS_0 /* Offset from base to BTE 1 */

/* BTE register offsets from base */
#define BTEOFF_STAT             0
#define BTEOFF_SRC              (IIO_BTE_SRC_0 - IIO_BTE_STAT_0)
#define BTEOFF_DEST             (IIO_BTE_DEST_0 - IIO_BTE_STAT_0)
#define BTEOFF_CTRL             (IIO_BTE_CTRL_0 - IIO_BTE_STAT_0)
#define BTEOFF_NOTIFY           (IIO_BTE_NOTIFY_0 - IIO_BTE_STAT_0)
#define BTEOFF_INT              (IIO_BTE_INT_0 - IIO_BTE_STAT_0)


/* names used in hub_diags.c; carried over from SN0 */
#define IIO_BASE_BTE0   IIO_IBLS_0		
#define IIO_BASE_BTE1   IIO_IBLS_1		
#if 0
#define IIO_BASE        IIO_WID
#define IIO_BASE_PERF   IIO_IPCR   /* IO Performance Control */
#define IIO_PERF_CNT    IIO_IPPR   /* IO Performance Profiling */
#endif


/* GFX Flow Control Node/Widget Register */
#define IIO_IGFX_W_NUM_BITS	4	/* size of widget num field */
#define IIO_IGFX_W_NUM_MASK	((1<<IIO_IGFX_W_NUM_BITS)-1)
#define IIO_IGFX_W_NUM_SHIFT	0
#define IIO_IGFX_PI_NUM_BITS	1	/* size of PI num field */
#define IIO_IGFX_PI_NUM_MASK	((1<<IIO_IGFX_PI_NUM_BITS)-1)
#define IIO_IGFX_PI_NUM_SHIFT	4
#define IIO_IGFX_N_NUM_BITS	8	/* size of node num field */
#define IIO_IGFX_N_NUM_MASK	((1<<IIO_IGFX_N_NUM_BITS)-1)
#define IIO_IGFX_N_NUM_SHIFT	5
#define IIO_IGFX_P_NUM_BITS	1	/* size of processor num field */
#define IIO_IGFX_P_NUM_MASK	((1<<IIO_IGFX_P_NUM_BITS)-1)
#define IIO_IGFX_P_NUM_SHIFT	16
#define IIO_IGFX_INIT(widget, pi, node, cpu)				(\
	(((widget) & IIO_IGFX_W_NUM_MASK) << IIO_IGFX_W_NUM_SHIFT) |	 \
	(((pi)     & IIO_IGFX_PI_NUM_MASK)<< IIO_IGFX_PI_NUM_SHIFT)|	 \
	(((node)   & IIO_IGFX_N_NUM_MASK) << IIO_IGFX_N_NUM_SHIFT) |	 \
	(((cpu)    & IIO_IGFX_P_NUM_MASK) << IIO_IGFX_P_NUM_SHIFT))


/* Scratch registers (all bits available) */
#define IIO_SCRATCH_REG0        IIO_ISCR0
#define IIO_SCRATCH_REG1        IIO_ISCR1
#define IIO_SCRATCH_MASK        0xffffffffffffffff

#define IIO_SCRATCH_BIT0_0      0x0000000000000001
#define IIO_SCRATCH_BIT0_1      0x0000000000000002
#define IIO_SCRATCH_BIT0_2      0x0000000000000004
#define IIO_SCRATCH_BIT0_3      0x0000000000000008
#define IIO_SCRATCH_BIT0_4      0x0000000000000010
#define IIO_SCRATCH_BIT0_5      0x0000000000000020
#define IIO_SCRATCH_BIT0_6      0x0000000000000040
#define IIO_SCRATCH_BIT0_7      0x0000000000000080
#define IIO_SCRATCH_BIT0_8      0x0000000000000100
#define IIO_SCRATCH_BIT0_9      0x0000000000000200
#define IIO_SCRATCH_BIT0_A      0x0000000000000400

#define IIO_SCRATCH_BIT1_0      0x0000000000000001
#define IIO_SCRATCH_BIT1_1      0x0000000000000002
/* IO Translation Table Entries */
#define IIO_NUM_ITTES   7               /* ITTEs numbered 0..6 */
                                        /* Hw manuals number them 1..7! */
/*
 * IIO_IMEM Register fields.
 */
#define IIO_IMEM_W0ESD  0x1             /* Widget 0 shut down due to error */
#define IIO_IMEM_B0ESD  (1 << 4)        /* BTE 0 shut down due to error */
#define IIO_IMEM_B1ESD  (1 << 8)        /* BTE 1 Shut down due to error */

/*
 * As a permanent workaround for a bug in the PI side of the hub, we've
 * redefined big window 7 as small window 0.
 XXX does this still apply for SN1??
 */
#define HUB_NUM_BIG_WINDOW      IIO_NUM_ITTES - 1

/*
 * Use the top big window as a surrogate for the first small window
 */
#define SWIN0_BIGWIN            HUB_NUM_BIG_WINDOW

#define IIO_NUM_PRTES   8               /* Total number of PRB table entries */

#define ILCSR_WARM_RESET        0x100

/*
 * CRB manipulation macros
 *      The CRB macros are slightly complicated, since there are up to
 *      four registers associated with each CRB entry.
 */
#define IIO_NUM_CRBS            15      /* Number of CRBs */
#define IIO_NUM_NORMAL_CRBS     12      /* Number of regular CRB entries */
#define IIO_NUM_PC_CRBS         4       /* Number of partial cache CRBs */
#define IIO_ICRB_OFFSET         8
#define IIO_ICRB_0              IIO_ICRB0_A
#define IIO_ICRB_ADDR_SHFT	2	/* Shift to get proper address */
/* XXX - This is now tuneable:
        #define IIO_FIRST_PC_ENTRY 12
 */

#define IIO_ICRB_A(_x)  (IIO_ICRB_0 + (4 * IIO_ICRB_OFFSET * (_x)))
#define IIO_ICRB_B(_x)  (IIO_ICRB_A(_x) + 1*IIO_ICRB_OFFSET)
#define IIO_ICRB_C(_x)  (IIO_ICRB_A(_x) + 2*IIO_ICRB_OFFSET)
#define IIO_ICRB_D(_x)  (IIO_ICRB_A(_x) + 3*IIO_ICRB_OFFSET)

#define TNUM_TO_WIDGET_DEV(_tnum)	(_tnum & 0x7)

/*
 * values for "ecode" field
 */
#define IIO_ICRB_ECODE_DERR     0       /* Directory error due to IIO access */
#define IIO_ICRB_ECODE_PERR     1       /* Poison error on IO access */
#define IIO_ICRB_ECODE_WERR     2       /* Write error by IIO access
                                         * e.g. WINV to a Read only line. */
#define IIO_ICRB_ECODE_AERR     3       /* Access error caused by IIO access */
#define IIO_ICRB_ECODE_PWERR    4       /* Error on partial write       */
#define IIO_ICRB_ECODE_PRERR    5       /* Error on partial read        */
#define IIO_ICRB_ECODE_TOUT     6       /* CRB timeout before deallocating */
#define IIO_ICRB_ECODE_XTERR    7       /* Incoming xtalk pkt had error bit */

/*
 * Number of credits Hub widget has while sending req/response to
 * xbow.
 * Value of 3 is required by Xbow 1.1
 * We may be able to increase this to 4 with Xbow 1.2.
 */
#define       HUBII_XBOW_CREDIT       3
#define       HUBII_XBOW_REV2_CREDIT  4

/*************************************************************************

 Some of the IIO field masks and shifts are defined here.
 This is in order to maintain compatibility in SN0 and SN1 code
 
**************************************************************************/

/*
 * ICMR register fields
 * (Note: the IIO_ICMR_P_CNT and IIO_ICMR_PC_VLD from Hub are not
 * present in Bedrock)
 */

#define IIO_ICMR_CRB_VLD_SHFT   20
#define IIO_ICMR_CRB_VLD_MASK   (0x7fffUL << IIO_ICMR_CRB_VLD_SHFT)

#define IIO_ICMR_FC_CNT_SHFT    16
#define IIO_ICMR_FC_CNT_MASK    (0xf << IIO_ICMR_FC_CNT_SHFT)

#define IIO_ICMR_C_CNT_SHFT     4
#define IIO_ICMR_C_CNT_MASK     (0xf << IIO_ICMR_C_CNT_SHFT)

#define IIO_ICMR_PRECISE        (1UL << 52)
#define IIO_ICMR_CLR_RPPD       (1UL << 13)
#define IIO_ICMR_CLR_RQPD       (1UL << 12)

/*
 * IIO PIO Deallocation register field masks : (IIO_IPDR)
 XXX present but not needed in bedrock?  See the manual.
 */
#define IIO_IPDR_PND    (1 << 4)

/*
 * IIO CRB deallocation register field masks: (IIO_ICDR)
 */
#define IIO_ICDR_PND    (1 << 4)

/* 
 * IO BTE Length/Status (IIO_IBLS) register bit field definitions
 */
#define IBLS_BUSY		(0x1 << 20)
#define IBLS_ERROR_SHFT		16
#define IBLS_ERROR		(0x1 << IBLS_ERROR_SHFT)
#define IBLS_LENGTH_MASK	0xffff

/*
 * IO BTE Control/Terminate register (IBCT) register bit field definitions
 */
#define IBCT_POISON		(0x1 << 8)
#define IBCT_NOTIFY		(0x1 << 4)
#define IBCT_ZFIL_MODE		(0x1 << 0)

/*
 * IO Error Clear register bit field definitions
 */
#define IECLR_PI1_FWD_INT	(1 << 31)  /* clear PI1_FORWARD_INT in iidsr */
#define IECLR_PI0_FWD_INT	(1 << 30)  /* clear PI0_FORWARD_INT in iidsr */
#define IECLR_SPUR_RD_HDR	(1 << 29)  /* clear valid bit in ixss reg */
#define IECLR_BTE1		(1 << 18)  /* clear bte error 1 */
#define IECLR_BTE0		(1 << 17)  /* clear bte error 0 */
#define IECLR_CRAZY		(1 << 16)  /* clear crazy bit in wstat reg */
#define IECLR_PRB_F		(1 << 15)  /* clear err bit in PRB_F reg */
#define IECLR_PRB_E		(1 << 14)  /* clear err bit in PRB_E reg */
#define IECLR_PRB_D		(1 << 13)  /* clear err bit in PRB_D reg */
#define IECLR_PRB_C		(1 << 12)  /* clear err bit in PRB_C reg */
#define IECLR_PRB_B		(1 << 11)  /* clear err bit in PRB_B reg */
#define IECLR_PRB_A		(1 << 10)  /* clear err bit in PRB_A reg */
#define IECLR_PRB_9		(1 << 9)   /* clear err bit in PRB_9 reg */
#define IECLR_PRB_8		(1 << 8)   /* clear err bit in PRB_8 reg */
#define IECLR_PRB_0		(1 << 0)   /* clear err bit in PRB_0 reg */

/*
 * IIO CRB control register Fields: IIO_ICCR 
 */
#define	IIO_ICCR_PENDING	(0x10000)
#define	IIO_ICCR_CMD_MASK	(0xFF)
#define	IIO_ICCR_CMD_SHFT	(7)
#define	IIO_ICCR_CMD_NOP	(0x0)	/* No Op */
#define	IIO_ICCR_CMD_WAKE	(0x100) /* Reactivate CRB entry and process */
#define	IIO_ICCR_CMD_TIMEOUT	(0x200)	/* Make CRB timeout & mark invalid */
#define	IIO_ICCR_CMD_EJECT	(0x400)	/* Contents of entry written to memory 
					 * via a WB
					 */
#define	IIO_ICCR_CMD_FLUSH	(0x800)

/*
 *
 * CRB Register description.
 *
 * WARNING * WARNING * WARNING * WARNING * WARNING * WARNING * WARNING
 * WARNING * WARNING * WARNING * WARNING * WARNING * WARNING * WARNING
 * WARNING * WARNING * WARNING * WARNING * WARNING * WARNING * WARNING
 * WARNING * WARNING * WARNING * WARNING * WARNING * WARNING * WARNING
 * WARNING * WARNING * WARNING * WARNING * WARNING * WARNING * WARNING
 *
 * Many of the fields in CRB are status bits used by hardware
 * for implementation of the protocol. It's very dangerous to
 * mess around with the CRB registers.
 *
 * It's OK to read the CRB registers and try to make sense out of the
 * fields in CRB.
 *
 * Updating CRB requires all activities in Hub IIO to be quiesced.
 * otherwise, a write to CRB could corrupt other CRB entries.
 * CRBs are here only as a back door peek to hub IIO's status.
 * Quiescing implies  no dmas no PIOs
 * either directly from the cpu or from sn0net.
 * this is not something that can be done easily. So, AVOID updating
 * CRBs.
 */

#ifdef _LANGUAGE_C

/*
 * Easy access macros for CRBs, all 4 registers (A-D)
 */
typedef ii_icrb0_a_u_t icrba_t;	/* what it was called on SN0/hub */
#define a_error         ii_icrb0_a_fld_s.ia_error
#define a_ecode         ii_icrb0_a_fld_s.ia_errcode
#define a_lnetuce       ii_icrb0_a_fld_s.ia_ln_uce
#define a_mark          ii_icrb0_a_fld_s.ia_mark
#define a_xerr          ii_icrb0_a_fld_s.ia_xt_err
#define a_sidn          ii_icrb0_a_fld_s.ia_sidn
#define a_tnum          ii_icrb0_a_fld_s.ia_tnum
#define a_addr          ii_icrb0_a_fld_s.ia_addr
#define a_valid         ii_icrb0_a_fld_s.ia_vld
#define a_iow           ii_icrb0_a_fld_s.ia_iow
#define a_regvalue	ii_icrb0_a_regval

typedef ii_icrb0_b_u_t icrbb_t;
#define b_btenum        ii_icrb0_b_fld_s.ib_bte_num
#define b_cohtrans      ii_icrb0_b_fld_s.ib_ct
#define b_xtsize        ii_icrb0_b_fld_s.ib_size
#define b_source        ii_icrb0_b_fld_s.ib_source
#define b_imsgtype      ii_icrb0_b_fld_s.ib_imsgtype
#define b_imsg          ii_icrb0_b_fld_s.ib_imsg
#define b_initiator     ii_icrb0_b_fld_s.ib_init
#define b_regvalue	ii_icrb0_b_regval

typedef ii_icrb0_c_u_t icrbc_t;
#define c_pricnt        ii_icrb0_c_fld_s.ic_pr_cnt
#define c_pripsc        ii_icrb0_c_fld_s.ic_pr_psc
#define c_bteop         ii_icrb0_c_fld_s.ic_bte_op
#define c_bteaddr       ii_icrb0_c_fld_s.ic_pa_be /* ic_pa_be fld has 2 names*/
#define c_benable       ii_icrb0_c_fld_s.ic_pa_be /* ic_pa_be fld has 2 names*/
#define c_suppl         ii_icrb0_c_fld_s.ic_suppl
#define c_barrop        ii_icrb0_c_fld_s.ic_bo
#define c_doresp        ii_icrb0_c_fld_s.ic_resprqd
#define c_gbr           ii_icrb0_c_fld_s.ic_gbr
#define c_regvalue	ii_icrb0_c_regval

typedef ii_icrb0_d_u_t icrbd_t;
#define icrbd_ctxtvld   ii_icrb0_d_fld_s.id_cvld
#define icrbd_toutvld   ii_icrb0_d_fld_s.id_tvld
#define icrbd_context   ii_icrb0_d_fld_s.id_context
#define d_regvalue	ii_icrb0_d_regval

#endif /* LANGUAGE_C */

/* Number of widgets supported by hub */
#define HUB_NUM_WIDGET          9
#define HUB_WIDGET_ID_MIN       0x8
#define HUB_WIDGET_ID_MAX       0xf

#define HUB_WIDGET_PART_NUM     0xc110
#define MAX_HUBS_PER_XBOW       2

#ifdef _LANGUAGE_C
/* A few more #defines for backwards compatibility */
#define iprb_t          ii_iprb0_u_t
#define iprb_regval     ii_iprb0_regval
#define iprb_ovflow     ii_iprb0_fld_s.i_of_cnt
#define iprb_error      ii_iprb0_fld_s.i_error
#define iprb_ff         ii_iprb0_fld_s.i_f
#define iprb_mode       ii_iprb0_fld_s.i_m
#define iprb_bnakctr    ii_iprb0_fld_s.i_nb
#define iprb_anakctr    ii_iprb0_fld_s.i_na
#define iprb_xtalkctr   ii_iprb0_fld_s.i_c
#endif

#define LNK_STAT_WORKING        0x2

#define IIO_WSTAT_ECRAZY        (1ULL << 32)    /* Hub gone crazy */
#define IIO_WSTAT_TXRETRY       (1ULL << 9)     /* Hub Tx Retry timeout */
#define IIO_WSTAT_TXRETRY_MASK  (0x7F)   /* should be 0xFF?? */
#define IIO_WSTAT_TXRETRY_SHFT  (16)
#define IIO_WSTAT_TXRETRY_CNT(w)        (((w) >> IIO_WSTAT_TXRETRY_SHFT) & \
                                          IIO_WSTAT_TXRETRY_MASK)

/* Number of II perf. counters we can multiplex at once */

#define IO_PERF_SETS	32

#ifdef BRINGUP
#if __KERNEL__
#if _LANGUAGE_C
/* XXX moved over from SN/SN0/hubio.h -- each should be checked for SN1 */
#include <asm/sn/alenlist.h>
#include <asm/sn/dmamap.h>
#include <asm/sn/iobus.h>
#include <asm/sn/xtalk/xtalk.h>

/* Bit for the widget in inbound access register */
#define IIO_IIWA_WIDGET(_w)     ((uint64_t)(1ULL << _w))
/* Bit for the widget in outbound access register */
#define IIO_IOWA_WIDGET(_w)     ((uint64_t)(1ULL << _w))

/* NOTE: The following define assumes that we are going to get
 * widget numbers from 8 thru F and the device numbers within
 * widget from 0 thru 7.
 */
#define IIO_IIDEM_WIDGETDEV_MASK(w, d)  ((uint64_t)(1ULL << (8 * ((w) - 8) + (d))))

/* IO Interrupt Destination Register */
#define IIO_IIDSR_SENT_SHIFT    28
#define IIO_IIDSR_SENT_MASK     0x10000000
#define IIO_IIDSR_ENB_SHIFT     24
#define IIO_IIDSR_ENB_MASK      0x01000000
#define IIO_IIDSR_NODE_SHIFT    8
#define IIO_IIDSR_NODE_MASK     0x0000ff00
#define IIO_IIDSR_PI_ID_SHIFT   8
#define IIO_IIDSR_PI_ID_MASK    0x00000010
#define IIO_IIDSR_LVL_SHIFT     0
#define IIO_IIDSR_LVL_MASK      0x0000007f

/* Xtalk timeout threshhold register (IIO_IXTT) */
#define IXTT_RRSP_TO_SHFT	55	   /* read response timeout */
#define IXTT_RRSP_TO_MASK	(0x1FULL << IXTT_RRSP_TO_SHFT)
#define IXTT_RRSP_PS_SHFT	32	   /* read responsed TO prescalar */
#define IXTT_RRSP_PS_MASK	(0x7FFFFFULL << IXTT_RRSP_PS_SHFT)
#define IXTT_TAIL_TO_SHFT	0	   /* tail timeout counter threshold */
#define IXTT_TAIL_TO_MASK	(0x3FFFFFFULL << IXTT_TAIL_TO_SHFT)

/*
 * The IO LLP control status register and widget control register
 */

#ifdef LITTLE_ENDIAN

typedef union hubii_wcr_u {
        uint64_t      wcr_reg_value;
        struct {
	  uint64_t	wcr_widget_id:   4,     /* LLP crossbar credit */
			wcr_tag_mode:	 1,	/* Tag mode */
			wcr_rsvd1:	 8,	/* Reserved */
			wcr_xbar_crd:	 3,	/* LLP crossbar credit */
			wcr_f_bad_pkt:	 1,	/* Force bad llp pkt enable */
			wcr_dir_con:	 1,	/* widget direct connect */
			wcr_e_thresh:	 5,	/* elasticity threshold */
			wcr_rsvd:	41;	/* unused */
        } wcr_fields_s;
} hubii_wcr_t;

#else

typedef union hubii_wcr_u {
	uint64_t	wcr_reg_value;
	struct {
	    uint64_t	wcr_rsvd:	41,	/* unused */
			wcr_e_thresh:	 5,	/* elasticity threshold */
			wcr_dir_con:	 1,	/* widget direct connect */
			wcr_f_bad_pkt:	 1,	/* Force bad llp pkt enable */
			wcr_xbar_crd:	 3,	/* LLP crossbar credit */
			wcr_rsvd1:	 8,	/* Reserved */
			wcr_tag_mode:	 1,	/* Tag mode */
			wcr_widget_id:	 4;	/* LLP crossbar credit */
	} wcr_fields_s;
} hubii_wcr_t;

#endif

#define iwcr_dir_con    wcr_fields_s.wcr_dir_con

/* The structures below are defined to extract and modify the ii
performance registers */

/* io_perf_sel allows the caller to specify what tests will be
   performed */
#ifdef LITTLE_ENDIAN

typedef union io_perf_sel {
        uint64_t perf_sel_reg;
        struct {
               uint64_t	perf_ippr0 :  4,
				perf_ippr1 :  4,
				perf_icct  :  8,
				perf_rsvd  : 48;
        } perf_sel_bits;
} io_perf_sel_t;

#else

typedef union io_perf_sel {
	uint64_t perf_sel_reg;
	struct {
		uint64_t	perf_rsvd  : 48,
				perf_icct  :  8,
				perf_ippr1 :  4,
				perf_ippr0 :  4;
	} perf_sel_bits;
} io_perf_sel_t;

#endif

/* io_perf_cnt is to extract the count from the hub registers. Due to
   hardware problems there is only one counter, not two. */

#ifdef LITTLE_ENDIAN

typedef union io_perf_cnt {
        uint64_t      perf_cnt;
        struct {
               uint64_t	perf_cnt   : 20,
				perf_rsvd2 : 12,
				perf_rsvd1 : 32;
        } perf_cnt_bits;

} io_perf_cnt_t;

#else

typedef union io_perf_cnt {
	uint64_t	perf_cnt;
	struct {
		uint64_t	perf_rsvd1 : 32,
				perf_rsvd2 : 12,
				perf_cnt   : 20;
	} perf_cnt_bits;

} io_perf_cnt_t;

#endif

#ifdef LITTLE_ENDIAN

typedef union iprte_a {
	bdrkreg_t	entry;
	struct {
		bdrkreg_t	i_rsvd_1                  :	 3;
		bdrkreg_t	i_addr			  :	38;
		bdrkreg_t	i_init			  :	 3;
		bdrkreg_t	i_source		  :	 8;
		bdrkreg_t	i_rsvd			  :	 2;
		bdrkreg_t	i_widget		  :	 4;
		bdrkreg_t	i_to_cnt		  :	 5;
		bdrkreg_t       i_vld                     :      1;
	} iprte_fields;
} iprte_a_t;

#else

typedef union iprte_a {
	bdrkreg_t	entry;
	struct {
		bdrkreg_t	i_vld			  :	 1;
		bdrkreg_t	i_to_cnt		  :	 5;
		bdrkreg_t	i_widget		  :	 4;
		bdrkreg_t	i_rsvd			  :	 2;
		bdrkreg_t	i_source		  :	 8;
		bdrkreg_t	i_init			  :	 3;
		bdrkreg_t	i_addr			  :	38;
		bdrkreg_t	i_rsvd_1		  :	 3;
	} iprte_fields;
} iprte_a_t;

#endif

/* PIO MANAGEMENT */
typedef struct hub_piomap_s *hub_piomap_t;

extern hub_piomap_t
hub_piomap_alloc(devfs_handle_t dev,      /* set up mapping for this device */
                device_desc_t dev_desc, /* device descriptor */
                iopaddr_t xtalk_addr,   /* map for this xtalk_addr range */
                size_t byte_count,
                size_t byte_count_max,  /* maximum size of a mapping */
                unsigned flags);                /* defined in sys/pio.h */

extern void hub_piomap_free(hub_piomap_t hub_piomap);

extern caddr_t
hub_piomap_addr(hub_piomap_t hub_piomap,        /* mapping resources */
                iopaddr_t xtalk_addr,           /* map for this xtalk addr */
                size_t byte_count);             /* map this many bytes */

extern void
hub_piomap_done(hub_piomap_t hub_piomap);

extern caddr_t
hub_piotrans_addr(      devfs_handle_t dev,       /* translate to this device */
                        device_desc_t dev_desc, /* device descriptor */
                        iopaddr_t xtalk_addr,   /* Crosstalk address */
                        size_t byte_count,      /* map this many bytes */
                        unsigned flags);        /* (currently unused) */

/* DMA MANAGEMENT */
typedef struct hub_dmamap_s *hub_dmamap_t;

extern hub_dmamap_t
hub_dmamap_alloc(       devfs_handle_t dev,       /* set up mappings for dev */
                        device_desc_t dev_desc, /* device descriptor */
                        size_t byte_count_max,  /* max size of a mapping */
                        unsigned flags);        /* defined in dma.h */

extern void
hub_dmamap_free(hub_dmamap_t dmamap);

extern iopaddr_t
hub_dmamap_addr(        hub_dmamap_t dmamap,    /* use mapping resources */
                        paddr_t paddr,          /* map for this address */
                        size_t byte_count);     /* map this many bytes */

extern alenlist_t
hub_dmamap_list(        hub_dmamap_t dmamap,    /* use mapping resources */
                        alenlist_t alenlist,    /* map this Addr/Length List */
                        unsigned flags);

extern void
hub_dmamap_done(        hub_dmamap_t dmamap);   /* done w/ mapping resources */

extern iopaddr_t
hub_dmatrans_addr(      devfs_handle_t dev,       /* translate for this device */
                        device_desc_t dev_desc, /* device descriptor */
                        paddr_t paddr,          /* system physical address */
                        size_t byte_count,      /* length */
                        unsigned flags);                /* defined in dma.h */

extern alenlist_t
hub_dmatrans_list(      devfs_handle_t dev,       /* translate for this device */
                        device_desc_t dev_desc, /* device descriptor */
                        alenlist_t palenlist,   /* system addr/length list */
                        unsigned flags);                /* defined in dma.h */

extern void
hub_dmamap_drain(       hub_dmamap_t map);

extern void
hub_dmaaddr_drain(      devfs_handle_t vhdl,
                        paddr_t addr,
                        size_t bytes);

extern void
hub_dmalist_drain(      devfs_handle_t vhdl,
                        alenlist_t list);


/* INTERRUPT MANAGEMENT */
typedef struct hub_intr_s *hub_intr_t;

extern hub_intr_t
hub_intr_alloc( devfs_handle_t dev,               /* which device */
                device_desc_t dev_desc,         /* device descriptor */
                devfs_handle_t owner_dev);        /* owner of this interrupt */

extern void
hub_intr_free(hub_intr_t intr_hdl);

extern int
hub_intr_connect(       hub_intr_t intr_hdl,    /* xtalk intr resource hndl */
                        intr_func_t intr_func,  /* xtalk intr handler */
                        void *intr_arg,         /* arg to intr handler */
                        xtalk_intr_setfunc_t setfunc,
                                                /* func to set intr hw */
                        void *setfunc_arg,      /* arg to setfunc */
                        void *thread);          /* intr thread to use */

extern void
hub_intr_disconnect(hub_intr_t intr_hdl);

extern devfs_handle_t
hub_intr_cpu_get(hub_intr_t intr_hdl);

/* CONFIGURATION MANAGEMENT */

extern void
hub_provider_startup(devfs_handle_t hub);

extern void
hub_provider_shutdown(devfs_handle_t hub);

#define HUB_PIO_CONVEYOR        0x1     /* PIO in conveyor belt mode */
#define HUB_PIO_FIRE_N_FORGET   0x2     /* PIO in fire-and-forget mode */

/* Flags that make sense to hub_widget_flags_set */
#define HUB_WIDGET_FLAGS        (                               \
				 HUB_PIO_CONVEYOR       |       \
				 HUB_PIO_FIRE_N_FORGET          \
				)


typedef int     hub_widget_flags_t;

/* Set the PIO mode for a widget.  These two functions perform the
 * same operation, but hub_device_flags_set() takes a hardware graph
 * vertex while hub_widget_flags_set() takes a nasid and widget
 * number.  In most cases, hub_device_flags_set() should be used.
 */
extern int      hub_widget_flags_set(nasid_t            nasid,
                                     xwidgetnum_t       widget_num,
                                     hub_widget_flags_t flags);

/* Depending on the flags set take the appropriate actions */
extern int      hub_device_flags_set(devfs_handle_t       widget_dev,
                                     hub_widget_flags_t flags);
                                                    

/* Error Handling. */
extern int hub_ioerror_handler(devfs_handle_t, int, int, struct io_error_s *);
extern int kl_ioerror_handler(cnodeid_t, cnodeid_t, cpuid_t,
                              int, paddr_t, caddr_t, ioerror_mode_t);
extern void hub_widget_reset(devfs_handle_t, xwidgetnum_t);
extern int hub_error_devenable(devfs_handle_t, int, int);
extern void hub_widgetdev_enable(devfs_handle_t, int);
extern void hub_widgetdev_shutdown(devfs_handle_t, int);
extern int  hub_dma_enabled(devfs_handle_t);

#endif /* _LANGUAGE_C */
#endif /* _KERNEL */
#endif /* BRINGUP */
#endif  /* _ASM_SN_SN1_HUBIO_NEXT_H */
