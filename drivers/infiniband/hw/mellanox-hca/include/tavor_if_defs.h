/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
*/

#ifndef H_TAVOR_IF_DEFS_H
#define H_TAVOR_IF_DEFS_H

// FW TEAM: timeouts are divided into 3 classes - values are in usec:
//#define TAVOR_IF_CMD_ETIME_CLASS_A				5000000
//#define TAVOR_IF_CMD_ETIME_CLASS_B				10000000
//#define TAVOR_IF_CMD_ETIME_CLASS_C				20000000
//#define TAVOR_IF_CMD_ETIME_UNKNOWN_LAT          50000000

/* TK: FW cannot guarantee commands completion timeout due to starvation with door-bells
   So we put the 15 minutes hoping that the DBs will stop and the
   command will have time to complete */
#define TAVOR_IF_CMD_ETIME_CLASS_A				300000000
#define TAVOR_IF_CMD_ETIME_CLASS_B				300000000
#define TAVOR_IF_CMD_ETIME_CLASS_C				300000000

/* macros to define the estimated time in microseconds to execute a command */
#define TAVOR_IF_CMD_ETIME_SYS_EN             TAVOR_IF_CMD_ETIME_CLASS_C
#define TAVOR_IF_CMD_ETIME_SYS_DIS            TAVOR_IF_CMD_ETIME_CLASS_C
#define TAVOR_IF_CMD_ETIME_QUERY_DEV_LIM      TAVOR_IF_CMD_ETIME_CLASS_A
#define TAVOR_IF_CMD_ETIME_QUERY_FW           TAVOR_IF_CMD_ETIME_CLASS_A
#define TAVOR_IF_CMD_ETIME_ACCESS_DDR         TAVOR_IF_CMD_ETIME_UNKNOWN_LAT
#define TAVOR_IF_CMD_ETIME_QUERY_DDR          TAVOR_IF_CMD_ETIME_CLASS_A
#define TAVOR_IF_CMD_ETIME_QUERY_ADAPTER      TAVOR_IF_CMD_ETIME_CLASS_A
#define TAVOR_IF_CMD_ETIME_INIT_HCA           TAVOR_IF_CMD_ETIME_CLASS_C
#define TAVOR_IF_CMD_ETIME_CLOSE_HCA          TAVOR_IF_CMD_ETIME_CLASS_C
#define TAVOR_IF_CMD_ETIME_INIT_IB            TAVOR_IF_CMD_ETIME_CLASS_A
#define TAVOR_IF_CMD_ETIME_CLOSE_IB           TAVOR_IF_CMD_ETIME_CLASS_A
#define TAVOR_IF_CMD_ETIME_QUERY_HCA          TAVOR_IF_CMD_ETIME_CLASS_C
#define TAVOR_IF_CMD_ETIME_SET_IB             TAVOR_IF_CMD_ETIME_CLASS_B

#define TAVOR_IF_CMD_ETIME_SW2HW_MPT          TAVOR_IF_CMD_ETIME_CLASS_B
#define TAVOR_IF_CMD_ETIME_QUERY_MPT          TAVOR_IF_CMD_ETIME_CLASS_A
#define TAVOR_IF_CMD_ETIME_HW2SW_MPT          TAVOR_IF_CMD_ETIME_CLASS_B
#define TAVOR_IF_CMD_ETIME_READ_MTT           TAVOR_IF_CMD_ETIME_CLASS_A
#define TAVOR_IF_CMD_ETIME_WRITE_MTT          TAVOR_IF_CMD_ETIME_CLASS_B
#define TAVOR_IF_CMD_ETIME_SYNC_TPT           TAVOR_IF_CMD_ETIME_CLASS_B

#define TAVOR_IF_CMD_ETIME_MAP_EQ             TAVOR_IF_CMD_ETIME_CLASS_B
#define TAVOR_IF_CMD_ETIME_SW2HW_EQ           TAVOR_IF_CMD_ETIME_CLASS_A
#define TAVOR_IF_CMD_ETIME_HW2SW_EQ           TAVOR_IF_CMD_ETIME_CLASS_A
#define TAVOR_IF_CMD_ETIME_QUERY_EQ           TAVOR_IF_CMD_ETIME_CLASS_A

#define TAVOR_IF_CMD_ETIME_SW2HW_CQ           TAVOR_IF_CMD_ETIME_CLASS_A
#define TAVOR_IF_CMD_ETIME_HW2SW_CQ           TAVOR_IF_CMD_ETIME_CLASS_A
#define TAVOR_IF_CMD_ETIME_QUERY_CQ           TAVOR_IF_CMD_ETIME_CLASS_A
#define TAVOR_IF_CMD_ETIME_RESIZE_CQ          TAVOR_IF_CMD_ETIME_CLASS_A

#define TAVOR_IF_CMD_ETIME_RST2INIT_QPEE      TAVOR_IF_CMD_ETIME_CLASS_C
#define TAVOR_IF_CMD_ETIME_INIT2INIT_QPEE     TAVOR_IF_CMD_ETIME_CLASS_C
#define TAVOR_IF_CMD_ETIME_INIT2RTR_QPEE      TAVOR_IF_CMD_ETIME_CLASS_C
#define TAVOR_IF_CMD_ETIME_RTR2RTS_QPEE       TAVOR_IF_CMD_ETIME_CLASS_C
#define TAVOR_IF_CMD_ETIME_RTS2RTS_QPEE       TAVOR_IF_CMD_ETIME_CLASS_C
#define TAVOR_IF_CMD_ETIME_SQERR2RTS_QPEE     TAVOR_IF_CMD_ETIME_CLASS_C
#define TAVOR_IF_CMD_ETIME_2ERR_QPEE          TAVOR_IF_CMD_ETIME_CLASS_C
#define TAVOR_IF_CMD_ETIME_RTS2SQD            TAVOR_IF_CMD_ETIME_CLASS_C
#define TAVOR_IF_CMD_ETIME_SQD2RTS_QPEE       TAVOR_IF_CMD_ETIME_CLASS_C
#define TAVOR_IF_CMD_ETIME_ERR2RST_QPEE       TAVOR_IF_CMD_ETIME_CLASS_C
#define TAVOR_IF_CMD_ETIME_QUERY_QPEE         TAVOR_IF_CMD_ETIME_CLASS_C

#define TAVOR_IF_CMD_ETIME_CONF_SPECIAL_QP    TAVOR_IF_CMD_ETIME_CLASS_B
#define TAVOR_IF_CMD_ETIME_MAD_IFC            TAVOR_IF_CMD_ETIME_CLASS_C

#define TAVOR_IF_CMD_ETIME_READ_MGM           TAVOR_IF_CMD_ETIME_CLASS_A
#define TAVOR_IF_CMD_ETIME_WRITE_MGM          TAVOR_IF_CMD_ETIME_CLASS_A
#define TAVOR_IF_CMD_ETIME_MGID_HASH          TAVOR_IF_CMD_ETIME_CLASS_A

#define TAVOR_IF_CMD_ETIME_CONF_PM            TAVOR_IF_CMD_ETIME_CLASS_C

#define TAVOR_IF_CMD_ETIME_CONF_NTU           TAVOR_IF_CMD_ETIME_CLASS_C
#define TAVOR_IF_CMD_ETIME_QUERY_NTU          TAVOR_IF_CMD_ETIME_CLASS_C

#define TAVOR_IF_CMD_ETIME_DIAG_RPRT          TAVOR_IF_CMD_ETIME_UNKNOWN_LAT
#define TAVOR_IF_CMD_ETIME_QUERY_DEBUG_MSG    TAVOR_IF_CMD_ETIME_UNKNOWN_LAT
#define TAVOR_IF_CMD_ETIME_SET_DEBUG_MSG      TAVOR_IF_CMD_ETIME_UNKNOWN_LAT


//////////////////////// ADDED BY FW TEAM /////////////////////////
#define	TAVOR_IF_STRIDE_QPC_BIT 	8
#define	TAVOR_IF_STRIDE_EEC_BIT 	8
#define	TAVOR_IF_STRIDE_SRQC_BIT 	5
#define	TAVOR_IF_STRIDE_CQC_BIT	  6
#define	TAVOR_IF_STRIDE_EQC_BIT	  6
#define	TAVOR_IF_STRIDE_MPT_BIT   6
#define	TAVOR_IF_STRIDE_MTT_BIT		3
#define	TAVOR_IF_STRIDE_EQPC_BIT	5
#define	TAVOR_IF_STRIDE_EEEC_BIT	5
#define	TAVOR_IF_STRIDE_APM_BIT		5
#define	TAVOR_IF_STRIDE_MCST_BIT	5
#define	TAVOR_IF_STRIDE_UARSCR_BIT 5

#define TAVOR_IF_STRIDE_QPC (1<<TAVOR_IF_STRIDE_QPC_BIT)
#define TAVOR_IF_STRIDE_EEC (1<<TAVOR_IF_STRIDE_EEC_BIT)
#define TAVOR_IF_STRIDE_SRQC (1<<TAVOR_IF_STRIDE_SRQC_BIT)
#define TAVOR_IF_STRIDE_CQC (1<<TAVOR_IF_STRIDE_CQC_BIT)
#define TAVOR_IF_STRIDE_EQC (1<<TAVOR_IF_STRIDE_EQC_BIT)
#define TAVOR_IF_STRIDE_MPT (1<<TAVOR_IF_STRIDE_MPT_BIT)
#define TAVOR_IF_STRIDE_MTT (1<<TAVOR_IF_STRIDE_MTT_BIT)
#define TAVOR_IF_STRIDE_EQPC (1<<TAVOR_IF_STRIDE_EQPC_BIT)
#define TAVOR_IF_STRIDE_EEEC (1<<TAVOR_IF_STRIDE_EEEC_BIT)
#define TAVOR_IF_STRIDE_APM (1<<TAVOR_IF_STRIDE_APM_BIT)
#define TAVOR_IF_STRIDE_MCST (1<<TAVOR_IF_STRIDE_MCST_BIT)
#define TAVOR_IF_STRIDE_UARSCR (1<<TAVOR_IF_STRIDE_UARSCR_BIT)

#define TAVOR_IF_MPT_HW_STATUS_OFFSET        0
#define TAVOR_IF_MPT_HW_START_ADDR_OFFSET   16
#define TAVOR_IF_MPT_HW_LEN_OFFSET          24
#define TAVOR_IF_MPT_HW_MEMKEY_OFFSET        8
#define TAVOR_IF_MPT_HW_LKEY_OFFSET         32

/* this constant is the flag masked on to the opcode modifier in TAVOR_IF_CMD_RTS2SQD_QPEE
   in order to request SQD notification  */
#define TAVOR_IF_SQD_EVENT_FLAG		    0x80000000

////////////////////////     FW TEAM      /////////////////////////

typedef enum tavor_if_cmd {
  /* initialization and general commands */
  TAVOR_IF_CMD_SYS_EN = 0x1,
  TAVOR_IF_CMD_SYS_DIS = 0x2,
  TAVOR_IF_CMD_MOD_STAT_CFG = 0x34,
  TAVOR_IF_CMD_QUERY_DEV_LIM = 0x3,
  TAVOR_IF_CMD_QUERY_FW = 0x4,
  TAVOR_IF_CMD_QUERY_DDR = 0x5,
  TAVOR_IF_CMD_QUERY_ADAPTER = 0x6,
  TAVOR_IF_CMD_INIT_HCA = 0x7,
  TAVOR_IF_CMD_CLOSE_HCA = 0x8,
  TAVOR_IF_CMD_INIT_IB = 0x9,
  TAVOR_IF_CMD_CLOSE_IB = 0xa,
  TAVOR_IF_CMD_QUERY_HCA = 0xb,
  TAVOR_IF_CMD_SET_IB = 0xc,
  TAVOR_IF_CMD_ACCESS_DDR = 0x2e,

  /* TPT commands */
  TAVOR_IF_CMD_SW2HW_MPT = 0xd,
  TAVOR_IF_CMD_QUERY_MPT = 0xe,
  TAVOR_IF_CMD_HW2SW_MPT = 0xf,
  TAVOR_IF_CMD_MODIFY_MPT = 0x39,
  TAVOR_IF_CMD_READ_MTT = 0x10,
  TAVOR_IF_CMD_WRITE_MTT = 0x11,
  TAVOR_IF_CMD_SYNC_TPT = 0x2f,

  /* EQ commands */
  TAVOR_IF_CMD_MAP_EQ = 0x12,
  TAVOR_IF_CMD_SW2HW_EQ = 0x13,
  TAVOR_IF_CMD_HW2SW_EQ = 0x14,
  TAVOR_IF_CMD_QUERY_EQ = 0x15,

  /* CQ commands */
  TAVOR_IF_CMD_SW2HW_CQ = 0x16,
  TAVOR_IF_CMD_HW2SW_CQ = 0x17,
  TAVOR_IF_CMD_QUERY_CQ = 0x18,
  TAVOR_IF_CMD_RESIZE_CQ= 0x2c,

  /* SRQ commands */
  TAVOR_IF_CMD_SW2HW_SRQ = 0x35,
  TAVOR_IF_CMD_HW2SW_SRQ = 0x36,
  TAVOR_IF_CMD_QUERY_SRQ = 0x37,

  /* QP/EE commands */
  TAVOR_IF_CMD_RST2INIT_QPEE = 0x19,
  TAVOR_IF_CMD_INIT2RTR_QPEE = 0x1a,
  TAVOR_IF_CMD_RTR2RTS_QPEE = 0x1b,
  TAVOR_IF_CMD_RTS2RTS_QPEE = 0x1c,
  TAVOR_IF_CMD_SQERR2RTS_QPEE = 0x1d,
  TAVOR_IF_CMD_2ERR_QPEE = 0x1e,
  TAVOR_IF_CMD_RTS2SQD_QPEE = 0x1f,
  TAVOR_IF_CMD_SQD2SQD_QPEE = 0x38,
  TAVOR_IF_CMD_SQD2RTS_QPEE = 0x20,
  TAVOR_IF_CMD_ERR2RST_QPEE = 0x21,
  TAVOR_IF_CMD_QUERY_QPEE = 0x22,
  TAVOR_IF_CMD_INIT2INIT_QPEE = 0x2d,
  TAVOR_IF_CMD_SUSPEND_QPEE = 0x32,
  TAVOR_IF_CMD_UNSUSPEND_QPEE = 0x33,
  /* special QPs and management commands */
  TAVOR_IF_CMD_CONF_SPECIAL_QP = 0x23,
  TAVOR_IF_CMD_MAD_IFC = 0x24,

  /* multicast commands */
  TAVOR_IF_CMD_READ_MGM = 0x25,
  TAVOR_IF_CMD_WRITE_MGM = 0x26,
  TAVOR_IF_CMD_MGID_HASH = 0x27,

  /* miscellaneous commands */
  //  TAVOR_IF_CMD_CONF_PM = 0,
  TAVOR_IF_CMD_DIAG_RPRT = 0x30,
  TAVOR_IF_CMD_NOP       = 0x31,

  /* debug commands */
  TAVOR_IF_CMD_QUERY_DEBUG_MSG = 0x2a,
  TAVOR_IF_CMD_SET_DEBUG_MSG = 0x2b,
  /* NTU commands */
  TAVOR_IF_CMD_CONF_NTU = 0x28,
  TAVOR_IF_CMD_QUERY_NTU = 0x29

}
tavor_if_cmd_t;


typedef enum tavor_if_cmd_status {
  TAVOR_IF_CMD_STAT_OK = 0x00,             /* command completed successfully */
  TAVOR_IF_CMD_STAT_INTERNAL_ERR = 0x01,   /* Internal error (such as a bus error) occurred while processing command */
  TAVOR_IF_CMD_STAT_BAD_OP = 0x02,         /* Operation/command not supported or opcode modifier not supported */
  TAVOR_IF_CMD_STAT_BAD_PARAM = 0x03,      /* Parameter not supported or parameter out of range */
  TAVOR_IF_CMD_STAT_BAD_SYS_STATE = 0x04,  /* System not enabled or bad system state */
  TAVOR_IF_CMD_STAT_BAD_RESOURCE = 0x05,   /* Attempt to access reserved or unallocaterd resource */
  TAVOR_IF_CMD_STAT_RESOURCE_BUSY = 0x06,  /* Requested resource is currently executing a command, or is otherwise busy */
  TAVOR_IF_CMD_STAT_DDR_MEM_ERR = 0x07,    /* memory error */
  TAVOR_IF_CMD_STAT_EXCEED_LIM = 0x08,     /* Required capability exceeds device limits */
  TAVOR_IF_CMD_STAT_BAD_RES_STATE = 0x09,  /* Resource is not in the appropriate state or ownership */
  TAVOR_IF_CMD_STAT_BAD_INDEX = 0x0a,      /* Index out of range */
  TAVOR_IF_CMD_STAT_BAD_NVMEM = 0x0b,      /* FW image corrupted */
  TAVOR_IF_CMD_STAT_BAD_QPEE_STATE = 0x10, /* Attempt to modify a QP/EE which is not in the presumed state */
  TAVOR_IF_CMD_STAT_BAD_SEG_PARAM = 0x20,  /* Bad segment parameters (Address/Size) */
  TAVOR_IF_CMD_STAT_REG_BOUND = 0x21,      /* Memory Region has Memory Windows bound to */
  TAVOR_IF_CMD_STAT_BAD_PKT = 0x30,        /* Bad management packet (silently discarded) */
  TAVOR_IF_CMD_STAT_BAD_SIZE = 0x40        /* More outstanding CQEs in CQ than new CQ size */
}
tavor_if_cmd_status_t;

/* special QP types */
typedef enum tavor_if_spqp {
  TAVOR_IF_SPQP_SMI = 0,
  TAVOR_IF_SPQP_GSI = 1,
  TAVOR_IF_SPQP_RAW_IPV6 = 2,
  TAVOR_IF_SPQP_RAW_ETHERTYPE = 3
}
tavor_if_spqp_t;

/* optparammask of QP/EE transition commands. (Tavor-PRM 13.6.x) */
enum tavor_if_qpee_optpar {
  TAVOR_IF_QPEE_OPTPAR_ALT_ADDR_PATH     = 1 << 0,
  TAVOR_IF_QPEE_OPTPAR_RRE               = 1 << 1,
  TAVOR_IF_QPEE_OPTPAR_RAE               = 1 << 2,
  TAVOR_IF_QPEE_OPTPAR_REW               = 1 << 3,
  TAVOR_IF_QPEE_OPTPAR_PKEY_INDEX        = 1 << 4,
  TAVOR_IF_QPEE_OPTPAR_Q_KEY             = 1 << 5,
  TAVOR_IF_QPEE_OPTPAR_RNR_TIMEOUT       = 1 << 6,
  TAVOR_IF_QPEE_OPTPAR_PRIMARY_ADDR_PATH = 1 << 7,
  TAVOR_IF_QPEE_OPTPAR_SRA_MAX           = 1 << 8,
  TAVOR_IF_QPEE_OPTPAR_RRA_MAX           = 1 << 9,
  TAVOR_IF_QPEE_OPTPAR_PM_STATE          = 1 << 10,
  TAVOR_IF_QPEE_OPTPAR_PORT_NUM          = 1 << 11,
  TAVOR_IF_QPEE_OPTPAR_RETRY_COUNT       = 1 << 12,
  TAVOR_IF_QPEE_OPTPAR_ALT_RNR_RETRY     = 1 << 13,
  TAVOR_IF_QPEE_OPTPAR_ACK_TIMEOUT       = 1 << 14,
  TAVOR_IF_QPEE_OPTPAR_RNR_RETRY         = 1 << 15,
  TAVOR_IF_QPEE_OPTPAR_SCHED_QUEUE       = 1 << 16,
  TAVOR_IF_QPEE_OPTPAR_ALL               = (1 << 17) -1
};

/* NOPCODE field enumeration for doorbells and send-WQEs */
typedef enum tavor_if_nopcode {
  TAVOR_IF_NOPCODE_NOP          = 0,  /* NOP */
  TAVOR_IF_NOPCODE_RDMAW        = 8,  /* RDMA-write */
  TAVOR_IF_NOPCODE_RDMAW_IMM    = 9,  /* RDMA-write w/immediate */
  TAVOR_IF_NOPCODE_SEND         = 10, /* Send */
  TAVOR_IF_NOPCODE_SEND_IMM     = 11, /* Send w/immediate */
  TAVOR_IF_NOPCODE_RDMAR        = 16, /* RDMA-read */
  TAVOR_IF_NOPCODE_ATOM_CMPSWP  = 17, /* Atomic Compare & Swap */
  TAVOR_IF_NOPCODE_ATOM_FTCHADD = 18, /* Atomic Fetch & Add */
  TAVOR_IF_NOPCODE_BIND_MEMWIN  = 24  /* Bind memory window */
}
tavor_if_nopcode_t;


/* event types */
/*=============*/
typedef enum tavor_if_eventt_num {
/* Completion Events */
  TAVOR_IF_EV_TYPE_CQ_COMP  =                  0,
/* IB - affiliated events */
  TAVOR_IF_EV_TYPE_PATH_MIG                 = 0x01,
  TAVOR_IF_EV_TYPE_COMM_EST                 = 0x02,
  TAVOR_IF_EV_TYPE_SEND_Q_DRAINED           = 0x03,
  /* IB - affiliated errors CQ  */
  TAVOR_IF_EV_TYPE_CQ_ERR                   = 0x04,
  TAVOR_IF_EV_TYPE_LOCAL_WQ_CATAS_ERR       = 0x05,
  TAVOR_IF_EV_TYPE_LOCAL_EE_CATAS_ERR       = 0x06,
  TAVOR_IF_EV_TYPE_PATH_MIG_FAIL            = 0x07,
  TAVOR_IF_EV_TYPE_LOCAL_SRQ_CATAS_ERR      = 0x12,
  TAVOR_IF_EV_TYPE_SRQ_QP_LAST_WQE_REACHED  = 0x13,
  /* Unaffiliated errors */
  TAVOR_IF_EV_TYPE_LOCAL_CATAS_ERR          = 0x08,
  TAVOR_IF_EV_TYPE_PORT_ERR                 = 0x09,
  TAVOR_IF_EV_TYPE_LOCAL_WQ_INVALID_REQ_ERR = 0x10,
  TAVOR_IF_EV_TYPE_LOCAL_WQ_ACCESS_VIOL_ERR = 0x11,
  /* Command Interface */
  TAVOR_IF_EV_TYPE_CMD_IF_COMP              = 0x0A,
  /* Address translation */
  TAVOR_IF_EV_TYPE_WQE_DATA_BUFF_PAGE_FAULT = 0x0B,
  TAVOR_IF_EV_TYPE_UNSUPP_PAGE_FAULT        = 0x0C,
  /* Performance Tuning Events  */
  TAVOR_IF_EV_TYPE_PERF_MONITOR_EVENTS      = 0x0D,
  /*Debug Events */
  TAVOR_IF_EV_TYPE_DEBUG                    = 0x0E,
  TAVOR_IF_EV_TYPE_OVERRUN                  = 0x0F
} tavor_if_eventt_num_t;

/* events sub-types */
typedef enum tavor_if_debug_eventt_subtype {
    TAVOR_IF_RAW_DBG_INFO1 = 0xF0,
    TAVOR_IF_RAW_DBG_INFO2 = 0xF1,
    TAVOR_IF_RAW_DBG_INFO3 = 0xF2,
    TAVOR_IF_RAW_DBG_ERROR1 = 0xF4,
    TAVOR_IF_RAW_DBG_ERROR2 = 0xF5,
    TAVOR_IF_RAW_DBG_ERROR3 = 0xF6
}tavor_if_debug_eventt_subtype_t;


typedef enum tavor_if_port_event_subtype {
    TAVOR_IF_SUB_EV_PORT_DOWN = 0x1,
    TAVOR_IF_SUB_EV_PORT_UP = 0x4
}tavor_if_port_event_subtype_t;

/* event type mask */
typedef enum tavor_if_eventt_mask_enum {
  TAVOR_IF_EV_MASK_CQ_COMP                     = 1<<TAVOR_IF_EV_TYPE_CQ_COMP,
  TAVOR_IF_EV_MASK_PATH_MIG   		           = 1<<TAVOR_IF_EV_TYPE_PATH_MIG,
  TAVOR_IF_EV_MASK_COMM_EST 		           = 1<<TAVOR_IF_EV_TYPE_COMM_EST,
  TAVOR_IF_EV_MASK_SEND_Q_DRAINED		       = 1<<TAVOR_IF_EV_TYPE_SEND_Q_DRAINED,
  TAVOR_IF_EV_MASK_CQ_ERR				       = 1<<TAVOR_IF_EV_TYPE_CQ_ERR,
  TAVOR_IF_EV_MASK_LOCAL_WQ_CATAS_ERR          = 1<<TAVOR_IF_EV_TYPE_LOCAL_WQ_CATAS_ERR,
  TAVOR_IF_EV_MASK_LOCAL_EE_CATAS_ERR          = 1<<TAVOR_IF_EV_TYPE_LOCAL_EE_CATAS_ERR,
  TAVOR_IF_EV_MASK_PATH_MIG_FAIL               = 1<<TAVOR_IF_EV_TYPE_PATH_MIG_FAIL,
  TAVOR_IF_EV_MASK_LOCAL_CATAS_ERR             = 1<<TAVOR_IF_EV_TYPE_LOCAL_CATAS_ERR,
  TAVOR_IF_EV_MASK_PORT_ERR                    = 1<<TAVOR_IF_EV_TYPE_PORT_ERR,
  TAVOR_IF_EV_MASK_CMD_IF_COMP                 = 1<<TAVOR_IF_EV_TYPE_CMD_IF_COMP,
  TAVOR_IF_EV_MASK_WQE_DATA_BUFF_PAGE_FAULT    = 1<<TAVOR_IF_EV_TYPE_WQE_DATA_BUFF_PAGE_FAULT,
  TAVOR_IF_EV_MASK_UNSUPP_PAGE_FAULT           = 1<<TAVOR_IF_EV_TYPE_UNSUPP_PAGE_FAULT,
  TAVOR_IF_EV_MASK_PERF_MONITOR_EVENTS         = 1<<TAVOR_IF_EV_TYPE_PERF_MONITOR_EVENTS,
  TAVOR_IF_EV_MASK_DEBUG                       = 1<<TAVOR_IF_EV_TYPE_DEBUG,
  TAVOR_IF_EV_MASK_LOCAL_WQ_INVALID_REQ_ERR    = 1<<TAVOR_IF_EV_TYPE_LOCAL_WQ_INVALID_REQ_ERR,
  TAVOR_IF_EV_MASK_LOCAL_WQ_ACCESS_VIOL_ERR    = 1<<TAVOR_IF_EV_TYPE_LOCAL_WQ_ACCESS_VIOL_ERR,
  TAVOR_IF_EV_MASK_LOCAL_SRQ_CATAS_ERR         = 1<<TAVOR_IF_EV_TYPE_LOCAL_SRQ_CATAS_ERR,
  TAVOR_IF_EV_MASK_SRQ_QP_LAST_WQE_REACHED     = 1<<TAVOR_IF_EV_TYPE_SRQ_QP_LAST_WQE_REACHED
} tavor_if_eventt_mask_enum_t;

typedef u_int64_t tavor_if_eventt_mask_t; /* To be used with tavor_if_eventt_mask_enum_t */


typedef enum tavor_if_ev_catas_error_type {
  TAVOR_IF_EV_CATAS_ERR_FW_INTERNAL_ERR = 0x00,
  TAVOR_IF_EV_CATAS_ERR_MISBEHAVED_UAR_PAGE = 0x02,
  TAVOR_IF_EV_CATAS_ERR_UPLINK_BUS_ERR = 0x03,
  TAVOR_IF_EV_CATAS_ERR_HCA_DDR_DATA_ERR = 0x04,
  TAVOR_IF_EV_CATAS_ERR_INTERNAL_PARITY_ERR = 0x05,
  TAVOR_IF_EV_CATAS_ERR_EQ_ERR = 0x06
}tavor_if_ev_catas_error_type_t;

#define TAVOR_IF_UNMAP_QP_BIT  0x80000000

/* Completion status encoding (as given in CQE.immediate[31:24]) */
typedef enum tavor_if_comp_status {
  TAVOR_IF_COMP_STATUS_SUCCESS              = 0x0,
  TAVOR_IF_COMP_STATUS_ERR_LCL_LEN          = 0x1,
  TAVOR_IF_COMP_STATUS_ERR_LCL_QP_OP        = 0x2,
  TAVOR_IF_COMP_STATUS_ERR_LCL_EE_OP        = 0x3,
  TAVOR_IF_COMP_STATUS_ERR_LCL_PROT         = 0x4,
  TAVOR_IF_COMP_STATUS_ERR_FLUSH            = 0x5,
  TAVOR_IF_COMP_STATUS_ERR_MWIN_BIND        = 0x6,
  TAVOR_IF_COMP_STATUS_ERR_BAD_RESP         = 0x10,
  TAVOR_IF_COMP_STATUS_ERR_LCL_ACCS         = 0x11,
  TAVOR_IF_COMP_STATUS_ERR_RMT_INVAL_REQ    = 0x12,
  TAVOR_IF_COMP_STATUS_ERR_RMT_ACCSS        = 0x13,
  TAVOR_IF_COMP_STATUS_ERR_RMT_OP           = 0x14,
  TAVOR_IF_COMP_STATUS_ERR_TRANS_RETRY_EX   = 0x15,
  TAVOR_IF_COMP_STATUS_ERR_RNR_RETRY_EX     = 0x16,
  TAVOR_IF_COMP_STATUS_ERR_LCL_RDD_VIOL     = 0x20,
  TAVOR_IF_COMP_STATUS_ERR_RMT_INVAL_REQ_RD = 0x21,
  TAVOR_IF_COMP_STATUS_ERR_RMT_ABORT        = 0x22,
  TAVOR_IF_COMP_STATUS_ERR_INVAL_EEC_NUM    = 0x23,
  TAVOR_IF_COMP_STATUS_ERR_INVAL_EEC_STT    = 0x24
} tavor_if_comp_status_t;


/* different enums for error events */
typedef enum tavor_if_cq_err_synd {
  TAVOR_IF_CQ_OVERRUN = 1,
  TAVOR_IF_CQ_ACCSS_VIOL_ERR = 2
}tavor_if_cq_err_synd_t;


typedef enum tavor_if_port_change_synd {
  TAVOR_IF_PORT_LOG_DN_PHY_DN = 0,
  TAVOR_IF_PORT_LOG_DN_PHY_UP = 1,
  TAVOR_IF_PORT_LOG_UP_PHY_UP = 2
}tavor_if_port_change_synd_t;

typedef enum tavor_if_addr_trans_fault_type{
  TAVOR_IF_FALT_REGION_INVALID              =0,
  TAVOR_IF_FALT_REGION_PD_MIS               =1,
  TAVOR_IF_FALT_REGION_WRT_ACC_VIOL         =2,
  TAVOR_IF_FALT_REGION_ATOMIC_ACC_VIOL      =3,
  /*4-7 - reserved */
  TAVOR_IF_FALT_PAGE_NOT_PRESENT            =8,
  /* RESERVED 9 */
  TAVOR_IF_FALT_PAGE_WRT_ACC_VIOL           =10,
  /* RESERVED 11-13 */
  TAVOR_IF_FALT_UNSUPP_NON_PRESENT_PAGE_FLT =14,
  TAVOR_IF_FALT_UNSUPP_WRT_ACC_VIOL         =15
}tavor_if_addr_trans_fault_type_t;

typedef enum tavor_if_perf_mntr_num {
  TAVOR_IF_MNTR_NUM_SQPC = 1,
  TAVOR_IF_MNTR_NUM_RQPC = 2,
  TAVOR_IF_MNTR_NUM_CQC  = 3,
  TAVOR_IF_MNTR_NUM_RKEY = 4,
  TAVOR_IF_MNTR_NUM_TLB  = 5,
  TAVOR_IF_MNTR_NUM_PORT_0 = 6,
  TAVOR_IF_MNTR_NUM_PORT_1 = 7
} tavor_if_perf_mntr_num_t;

/* CQ-command doorbell command encoding */
typedef enum tavor_if_uar_cq_cmd {
  TAVOR_IF_UAR_CQ_INC_CI     =1,/* Increment CQ's consumer index */
  TAVOR_IF_UAR_CQ_NOTIF_NEXT_COMP =2,/* Request notif. for next comp. event (param=consumer index)*/
  TAVOR_IF_UAR_CQ_NOTIF_SOLIC_COMP=3,/* Req. notif. for next solicited comp.(param=consumer index)*/
  TAVOR_IF_UAR_CQ_SET_CI     =4,/* Set CQ's consumer index to value given in param. */
  TAVOR_IF_UAR_CQ_NOTIF_NCOMP=5 /* Request notif. when N CQEs are outstanding (PI-CI>=N=cq_param)*/
} tavor_if_uar_cq_cmd_t;

/* EQ-command doorbell command encoding */
typedef enum tavor_if_uar_eq_cmd {
  TAVOR_IF_UAR_EQ_INC_CI     =1,/* Increment EQ's consumer index */
  TAVOR_IF_UAR_EQ_INT_ARM    =2,/* Request interrupt for next event (next EQE posted)*/
  TAVOR_IF_UAR_EQ_DISARM_CQ  =3,/* Disarm CQ request notification state machine (param= CQ num)*/
  TAVOR_IF_UAR_EQ_SET_CI     =4,/* Set EQ's consumer index to value given in param. */
  TAVOR_IF_UAR_EQ_INT_ALWAYS_ARM =5 /* interrupts are generated for every EQE generated */
} tavor_if_uar_eq_cmd_t;

/* QP-state encoding */
typedef enum tavor_if_qp_state {
  TAVOR_IF_QP_STATE_RESET     = 0,
  TAVOR_IF_QP_STATE_INIT      = 1,
  TAVOR_IF_QP_STATE_RTR       = 2,
  TAVOR_IF_QP_STATE_RTS       = 3,
  TAVOR_IF_QP_STATE_SQER      = 4,
  TAVOR_IF_QP_STATE_SQD       = 5,
  TAVOR_IF_QP_STATE_ERR       = 6,
  TAVOR_IF_QP_STATE_DRAINING  = 7,
  TAVOR_IF_QP_STATE_BUSY      = 8,
  TAVOR_IF_QP_STATE_SUSPENDED = 9
} tavor_if_qp_state_t;

/* Old CQ state encoding (for STS,STC).
 * Kept here for informational purposes.
 *
  TAVOR_IF_CQ_STATE_DISARMED = 0,
  TAVOR_IF_CQ_STATE_ARMED = 1,
  TAVOR_IF_CQ_STATE_FIRED = 2
 *
 */

/* CQ state encoding */
typedef enum tavor_if_cq_state {
  TAVOR_IF_CQ_STATE_DISARMED = 0x0,
  TAVOR_IF_CQ_STATE_ARMED = 0x1,
  TAVOR_IF_CQ_STATE_ARMED_SOL = 0x4,
  TAVOR_IF_CQ_STATE_FIRED = 0xA
} tavor_if_cq_state_t;

/* EQ state encoding */

typedef enum tavor_if_eq_state {
  TAVOR_IF_EQ_STATE_ARMED = 0x1,
  TAVOR_IF_EQ_STATE_FIRED = 0x2,
  TAVOR_IF_EQ_STATE_ALWAYS_ARMED = 0x3
} tavor_if_eq_state_t;

/* Miscellaneous Values: limits, tunable paramaters, etc. */
enum
{
  TAVOR_IF_HOST_BIGENDIAN = 1,   /* host is big endian*/
  TAVOR_NUM_RESERVED_PDS  = 0,   /* Obselete: will be moved to internal FW define once QUERY_DEV_LIM is implemented in full in driver */
  TAVOR_NUM_RESERVED_EQS  = 0,   /* Obselete: will be moved to internal FW define once QUERY_DEV_LIM is implemented in full in driver */
  TAVOR_NUM_RESERVED_RDDS = 0,   /* Obselete: will be moved to internal FW define once QUERY_DEV_LIM is implemented in full in driver */
  TAVOR_IF_HOST_LTLENDIAN = 0,   /* host is little endian*/

  /* Limits on QP in UD mode: max message and MTU */
  TAVOR_LOG2_MAX_MTU      = 11,

  TAVOR_IF_MAX_MPT_PAGE_SIZE = 31  /* (log2) Maximum page size for an MPT */
                                   /*sharon: 23.3.2003: changed from 32 at the req of tziporet */
};

typedef enum tavor_sys_en_syn
{
  TAVOR_SYS_EN_SYN_OK   = 0x0, /* No syndrome: When command succeeds */
  TAVOR_SYS_EN_SYN_SPD  = 0x1, /* SPD error (e. g. checksum error,
                                 no response, error while reading) */
  TAVOR_SYS_EN_SYN_DIMM = 0x2, /* DIMM out of bounds (e. g. DIMM rows
                                  number is not between 7 and 14,
                                  DIMM type is not 2) */
  TAVOR_SYS_EN_SYN_CONF = 0x3, /* DIMM conflict (e.g. mix of registered and
                                  unbuffered DIMMs, CAS latency conflict) */
  TAVOR_SYS_EN_SYN_CALB = 0x4, /* Calibration error */
  TAVOR_SYS_EN_SYN_TSIZ  = 0x5,  /* Total size out of bounds:
                                  E.g. total memory size exceeds the
                                  maximum supported value.  */
  TAVOR_SYS_EN_SYN_DCHK  = 0x6   /*dimm check error occured*/
} tavor_sys_en_syn_t;

typedef enum tavor_diag_rprt
{
  TAVOR_DIAG_RPRT_QUERY_ERR = 0x2, /* Query transport and CI error counters */
  TAVOR_DIAG_RPRT_RESET_ERR = 0x3, /* Query and reset error counters */
  TAVOR_DIAG_RPRT_QUERY_PERF = 0x4, /* Query performance counters */
  TAVOR_DIAG_RPRT_RESET_PERF = 0x5, /* Query and reset performance counters */
  TAVOR_DIAG_RPRT_QUERY_MISC = 0x6, /* Query MISC counters */
  TAVOR_DIAG_RPRT_RESET_MISC = 0x7, /* Query and reset MISC counters */
} tavor_diag_rprt_t;

#define CMDIF_OUTPRM_ALIGN 16 /* alignment requirement for out params */

#endif /* H_TAVOR_IF_DEFS_H */

