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

#ifndef H_IB_DEFS_H
#define H_IB_DEFS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <mtl_types.h>

typedef u_int8_t  IB_port_t;
#define NULL_IB_PORT 0xFF

typedef u_int16_t IB_lid_t;
typedef u_int8_t IB_gid_t[16]; /* GID (aka IPv6) H-to-L (big) (network) endianess */

/* IB-spec. Vol.1 (chapter 4): LID ranges */
#define MIN_UC_LID 0x0001  /* Unicast LID limits */
#define MAX_UC_LID 0xBFFF
#define MIN_MC_LID 0xC000 /* Multicast limits */
#define MAX_MC_LID 0xFFFE
#define PERMIS_LID 0xFFFF /* Permissive DLID */

/* Special Multicast QP num */
#define IB_MULTICAST_QP 0xFFFFFF
#define IB_VALID_MULTICAST_GID(gid)  ((gid)[0] == 0xFF)
#define IB_VALID_MULTICAST_LID(lid)  (((lid) >= MIN_MC_LID) && ((lid) <= MIN_MC_LID))


typedef u_int32_t IB_wqpn_t;  /* Work QP number: Only 24 LSbits */
typedef u_int32_t IB_eecn_t;  /* EE Context number: Only 24 LSbits */
typedef u_int8_t IB_guid_t[8];    /* EUI-64: Big-Endinan (H-to-L) */
typedef u_int8_t IB_gid_prefix_t[8];    /* EUI-64: Big-Endinan (H-to-L) */
typedef u_int8_t  IB_sl_t;   /* 0-15 */
typedef u_int8_t  IB_vl_t;   /* 0-15 */
typedef u_int8_t  IB_arbitration_weight_t;
typedef enum{PRIO_HIGH=0,PRIO_LOW=1} IB_arbitration_prio_t;
typedef u_int8_t IB_high_prio_limit_t;
typedef u_int64_t IB_virt_addr_t;
typedef u_int32_t IB_rkey_t;
typedef u_int16_t IB_pkey_t;
typedef u_int32_t IB_qkey_t;

/*** Note *** The following enum must be maintained zero based without holes */
enum { IB_TS_RC, IB_TS_RD, IB_TS_UC, IB_TS_UD, IB_TS_RAW };
typedef u_int32_t IB_ts_t;



#define INVALID_PKEY ((IB_pkey_t)0)  /* invalid PKEY. 0x8000 is also invalid but we'll use 0 */

typedef  u_int32_t  IB_psn_t;

typedef u_int8_t IB_link_width_t; /* Set to a combination of following masks ("OR"ed) */
#define W1X 1
#define W4X 2
#define W12X 8
#define W_SET2SUPPORTED 255       /* Set LinkWidthEnabled to LinkWidthSupported */

/* 9.7.6.1.3 DETECTING LOST ACKNOWLEDGE MESSAGES AND TIMEOUTS (C9-140)*/
#define IB_LOCAL_ACK_TIMEOUT_NUM_BITS   5

/* IB-spec. 9.7.5.2.8, table 45 
 *   RNR timer values symbols/macros use the convention: IB_RNR_NAK_TIMER_MMM_mm
 *   for the encoding of MMM.mm milliseconds
 */
#define IB_RNR_NAK_TIMER_NUM_BITS   5
enum {
  IB_RNR_NAK_TIMER_655_36 = 0,
  IB_RNR_NAK_TIMER_0_01   = 1,
  IB_RNR_NAK_TIMER_0_02   = 2,
  IB_RNR_NAK_TIMER_0_03   = 3,
  IB_RNR_NAK_TIMER_0_04   = 4,
  IB_RNR_NAK_TIMER_0_06   = 5,
  IB_RNR_NAK_TIMER_0_08   = 6,
  IB_RNR_NAK_TIMER_0_12   = 7,
  IB_RNR_NAK_TIMER_0_16   = 8,
  IB_RNR_NAK_TIMER_0_24   = 9,
  IB_RNR_NAK_TIMER_0_32   = 10,
  IB_RNR_NAK_TIMER_0_48   = 11,
  IB_RNR_NAK_TIMER_0_64   = 12,
  IB_RNR_NAK_TIMER_0_96   = 13,
  IB_RNR_NAK_TIMER_1_28   = 14,
  IB_RNR_NAK_TIMER_1_92   = 15,
  IB_RNR_NAK_TIMER_2_56   = 16,
  IB_RNR_NAK_TIMER_3_84   = 17,
  IB_RNR_NAK_TIMER_5_12   = 18,
  IB_RNR_NAK_TIMER_7_68   = 19,
  IB_RNR_NAK_TIMER_10_24  = 20,
  IB_RNR_NAK_TIMER_15_36  = 21,
  IB_RNR_NAK_TIMER_20_48  = 22,
  IB_RNR_NAK_TIMER_30_72  = 23,
  IB_RNR_NAK_TIMER_40_96  = 24,
  IB_RNR_NAK_TIMER_61_44  = 25,
  IB_RNR_NAK_TIMER_81_92  = 26,
  IB_RNR_NAK_TIMER_122_88 = 27,
  IB_RNR_NAK_TIMER_163_84 = 28,
  IB_RNR_NAK_TIMER_245_76 = 29,
  IB_RNR_NAK_TIMER_327_68 = 30,
  IB_RNR_NAK_TIMER_491_52 = 31
};
typedef u_int32_t IB_rnr_nak_timer_code_t;

typedef enum {
  S_NOP=0,
  S2GB5=1
} IB_link_speed_t;

typedef enum {
  PORT_NOP=0, /* No state change */
  PORT_DOWN=1,
  PORT_INITIALIZE=2,
  PORT_ARMED=3,
  PORT_ACTIVE=4
} IB_port_state_t;

typedef enum {
  PHY_NOP=0, /* No state change */
  PHY_SLEEP=1,
  PHY_POLLING=2,
  PHY_DISABLED=3,
  PHY_PORT_CONF_TRAINING=4,
  PHY_LINK_UP=5,
  PHY_LINK_ERR_REC0=6
} IB_phy_state_t;

enum{MTU256=1,MTU512=2,MTU1024=3,MTU2048=4,MTU4096=5};
typedef u_int32_t IB_mtu_t;

typedef enum{VL0=1,VL0_1=2,VL0_3=3,VL0_7=4,VL0_14=5} IB_vl_cap_t;

typedef u_int8_t IB_static_rate_t;     /* IPD encoding: IB-spec. 9.11.1, table 63 */

typedef enum{NODE_CA=1,NODE_SWITCH=2,NODE_ROUTER=3} IB_node_type_t;
  
typedef u_int16_t IB_dev_id_t;
  
typedef enum {
/* 0: Reserved  */
/* 1: */  IB_CAP_MASK_IS_SM               = (1<<1),
/* 2: */  IB_CAP_MASK_IS_NOTICE_SUP       = (1<<2),
/* 3: */  IB_CAP_MASK_IS_TRAP_SUP         = (1<<3),
/* 4:Reserved  */
/* 5: */  IB_CAP_MASK_IS_AUTO_MIGR_SUP    = (1<<5),
/* 6: */  IB_CAP_MASK_IS_SL_MAP_SUP       = (1<<6),
/* 7: */  IB_CAP_MASK_IS_MKEY_NVRAM       = (1<<7),
/* 8: */  IB_CAP_MASK_IS_PKEY_NVRAM       = (1<<8),
/* 9: */  IB_CAP_MASK_IS_LED_INFO_SUP     = (1<<9),
/*10: */  IB_CAP_MASK_IS_SM_DISABLED      = (1<<10),
/*11: */  IB_CAP_MASK_IS_SYS_IMAGE_GUID_SUP = (1<<11),
/*12: */  IB_CAP_MASK_IS_PKEY_SW_EXT_PORT_TRAP_SUP = (1<<12),
/*13 - 15: RESERVED  */
/*16: */  IB_CAP_MASK_IS_CONN_MGMT_SUP    = (1<<16),
/*17: */  IB_CAP_MASK_IS_SNMP_TUNN_SUP    = (1<<17),
/*18: */  IB_CAP_MASK_IS_REINIT_SUP       = (1<<18),
/*19: */  IB_CAP_MASK_IS_DEVICE_MGMT_SUP  = (1<<19),
/*20: */  IB_CAP_MASK_IS_VENDOR_CLS_SUP   = (1<<20),
/*21: */  IB_CAP_MASK_IS_DR_NOTICE_SUP    = (1<<21),
/*22: */  IB_CAP_MASK_IS_CAP_MASK_NOTICE_SUP = (1<<22),
/*23: */  IB_CAP_MASK_IS_BOOT_MGMT_SUP    = (1<<23)
/*24 - 31: RESERVED */
} IB_capability_mask_bits_t;


typedef u_int32_t IB_port_cap_mask_t;  /* To be used with flags in IB_capability_mask_bits_t */

#define IB_CAP_MASK_CLR_ALL(mask)   ((mask)=0)
#define IB_CAP_MASK_SET(mask,attr)  ((mask)|=(attr))
#define IB_CAP_MASK_CLR(mask,attr)  ((mask)&=(~(attr)))
#define IB_CAP_IS_SET(mask,attr)    (((mask)&(attr))!=0)
/*
 * This is an internal representation of PortInfo. 
 * It does not map directly to PortInfo bits.
 */
struct IB_port_info_st {
  u_int64_t       m_key;
  IB_gid_prefix_t gid_prefix;  /* Big-endinan (H-to-L) */
  IB_lid_t        lid;
  IB_lid_t        master_sm_lid;
  IB_port_cap_mask_t  capability_mask;
  u_int16_t       diag_code;
  u_int16_t       m_key_lease_period;
  IB_port_t       local_port_num;
  IB_link_width_t link_width_enabled;
  IB_link_width_t link_width_supported;
  IB_link_width_t link_width_active;
  IB_link_speed_t link_speed_supported;
  IB_port_state_t port_state;
  IB_phy_state_t  phy_state;
  IB_phy_state_t  down_default_state;
  u_int8_t        m_key_protect;           /* 0-3 */
  u_int8_t        lmc;                     /* 0-7 */
  IB_link_speed_t link_speed_active;
  IB_link_speed_t link_speed_enabled;
  IB_mtu_t        neighbor_mtu;
  IB_sl_t         master_sm_sl;           /* 0-15 */
  IB_vl_cap_t     vl_cap;
  u_int8_t        vl_high_limit;
  u_int8_t        vl_arbitration_high_cap;
  u_int8_t        vl_arbitration_low_cap;
  IB_mtu_t        mtu_cap;
  u_int8_t        vl_stall_count;         /* 0-7 */
  u_int8_t        hoq_life;               /* 0-31 */
  IB_vl_cap_t     operational_vl;
  MT_bool            partition_enforcement_inbound;
  MT_bool            partition_enforcement_outbound;
  MT_bool            filter_raw_inbound;
  MT_bool            filter_raw_outbound;
  u_int16_t       m_key_violations;
  u_int16_t       p_key_violations;
  u_int16_t       q_key_violations;
  u_int8_t        guid_cap;                /* 0-15 */
  u_int8_t        subnet_t_o;  /* SubnetTimeOut: 0-31 */
  u_int8_t        resp_time_val;  /* 0-15 */
  u_int8_t        local_phy_errs; /* 0-15 */
  u_int8_t        overrun_errs;   /* 0-15 */
};
 
struct IB_node_info_st {
  u_int8_t base_version;
  u_int8_t class_version;
  IB_node_type_t node_type;
  u_int8_t num_ports;
  IB_guid_t node_guid;
  IB_guid_t port_guid;
  u_int16_t partition_cap;
  IB_dev_id_t dev_id;
  u_int32_t dev_rev;
  IB_port_t local_port_num;
  u_int32_t vendor_id;    /* Only 24 LS-bits are valid */
};

typedef u_int8_t IB_node_description_t[64]; /* consider other UNICODE string representation */

struct IB_switch_info_st {
  u_int16_t linear_fdb_cap;
  u_int16_t random_fdb_cap;
  u_int16_t mcast_fdb_cap;
  u_int16_t linear_fdb_top;
  IB_port_t default_port;
  IB_port_t default_mcast_primary_port;
  IB_port_t default_mcast_not_primary_port;
  u_int8_t lifetime_val;            /*  Only 5 LS-bits are valid */
  MT_bool port_state_change;
  u_int16_t lids_per_port;
  u_int16_t partition_enforcement_cap;
  MT_bool inbound_enforcement_cap;
  MT_bool outbound_enforcement_cap;
  MT_bool filter_raw_packet_inbound_cap;
  MT_bool filter_raw_packet_outbound_cap;
};

typedef struct IB_grh_st {
  u_int8_t IP_version;      /* Only 4 LS-bits */
  u_int8_t traffic_class;
  u_int32_t flow_label;     /* Only 20 LS-bits */
  u_int16_t payload_length;
  u_int8_t next_header;
  u_int8_t hop_limit;
  IB_gid_t sgid;        /* H-to-L (big) (network) endianess */
  IB_gid_t dgid;        
}IB_grh_t;

/* IB headers sizes in bytes */
#define IB_LRH_LEN  8
#define IB_GRH_LEN  40   /* size of the GRH (in the actual packet) */
#define IB_BTH_LEN  12
#define IB_DETH_LEN 8
#define IB_MAD_LEN  256   /* size of a MAD payload */


struct IB_vl_weight_element_st {
  IB_vl_t vl;
  IB_arbitration_weight_t weight;
};
#define SET_END_OF_VL_WEIGHT_TAB(vlw) (vlw).weight = 0
#define IS_END_OF_VL_WEIGHT_TAB(vlw) ((vlw).weight == 0)
#define IB_MAX_VL_ARBITRATION_ENTRIES 64


typedef enum {
  IB_COMP_SUCCESS,
  IB_COMP_LOC_LEN_ERR,
  IB_COMP_LOC_QP_OP_ERR,
  IB_COMP_LOC_EE_OP_ERR,
  IB_COMP_LOC_PROT_ERR,
  IB_COMP_WR_FLUSH_ERR,
  IB_COMP_MW_BIND_ERR,
  IB_COMP_BAD_RESP_ERR,
  IB_COMP_LOC_ACCS_ERR,
  IB_COMP_REM_INV_REQ_ERR,
  IB_COMP_REM_ACCESS_ERR,
  IB_COMP_REM_OP_ERR,
  IB_COMP_RETRY_EXC_ERR,
  IB_COMP_RNR_RETRY_EXC_ERR,
  IB_COMP_LOC_RDD_VIOL_ERR,
  IB_COMP_REM_INV_RD_REQ_ERR,
  IB_COMP_REM_ABORT_ERR,
  IB_COMP_INV_EECN_ERR,
  IB_COMP_INV_EEC_STATE_ERR,
/*  IB_COMP_LOC_TOUT,*/ /* Use IB_COMP_RETRY_EXC_ERR instead */
/*  IB_COMP_RNR_TOUT,*/ /* Use IB_COMP_RNR_RETRY_EXC_ERR instead */

  IB_COMP_FATAL_ERR,
  IB_COMP_GENERAL_ERR
} IB_comp_status_t;

#define IB_PSN_MAX ((int32_t)0xffffff)
#define IB_PSN_ADD(a,b) (((int32_t)(a)+(int32_t)(b))& IB_PSN_MAX)
#define IB_PSN_SUB(a,b) (((int32_t)(a)-(int32_t)(b))& IB_PSN_MAX)
/* a <= b, that is b follows a: FIXME: might be off by one here */
#define IB_PSN_LE(a,b) (IB_PSN_SUB((b),(a)) <= IB_PSN_MAX/2)
#define IB_PSN_GE(a,b) (IB_PSN_LE((b),(a)))

#define IB_PSN_IS_VALID(a) ((((int32_t)(a)) & (~ IB_PSN_MAX)) == 0 ) 
#define IB_PSN_IS_INVALID(a) (((int32_t)(a)) & (~ IB_PSN_MAX))


/*
 * xCA interface general data strcutures.
 *
 */



typedef  void* IB_wrid_t;
#define IB_INVALID_WRID  0

typedef struct {
    u_int64_t   ibva;
     
    u_int32_t   ibva_l;        /* TBD - remove this in the future */
    u_int32_t   ibva_h;        /* TBD - remove this in the near future */

    IB_rkey_t   rkey;
} IB_raddr_t;

typedef struct {
    union
    {
        MT_virt_addr_t va;
        MT_phys_addr_t pa;
    } addr;

    u_int32_t   lkey;
    u_int32_t   size;
} IB_sge_t;

typedef struct {
    u_int32_t   byte_count;         /* Sum of size of all s/g entries */
    u_int32_t   entry_count;        /* Number of s/g entries in list  */

    enum {IB_SGE_VIRT, IB_SGE_PHYS} addr_type;
    
    IB_sge_t *list;
    
} IB_sge_list_t;


typedef enum {
  IB_WR_RDMA_WRITE,
  IB_WR_RDMA_WRITE_WITH_IMM,
  IB_WR_SEND,
  IB_WR_SEND_WITH_IMM,
  IB_WR_RDMA_READ,
  IB_WR_ATOMIC_CMP_AND_SWP,
  IB_WR_ATOMIC_FETCH_AND_ADD,
  IB_WR_RECEIVE
} IB_wr_opcode_t;

/* Address Vector */
typedef struct {
  IB_sl_t             sl;              /* Service Level 4 bits      */
  IB_lid_t            dlid;            /* Destination LID           */
  u_int8_t            src_path_bits;   /* Source path bits 7 bits   */
  IB_static_rate_t    static_rate;     /* Maximum static rate : 6 bits  */

  MT_bool             grh_flag;        /* Send GRH flag             */
  /* For global destination or Multicast address:*/ 
  u_int8_t            traffic_class;   /* TClass 8 bits             */
  u_int32_t           flow_label;      /* Flow Label 20 bits        */
  u_int8_t            hop_limit;       /* Hop Limit 8 bits          */
  u_int8_t            sgid_index;      /* SGID index in SGID table  */
  IB_gid_t            dgid;            /* Destination GID */

} IB_ud_av_t; 



#ifdef __cplusplus
}
#endif

#endif /* H_IB_DEFS_H */
