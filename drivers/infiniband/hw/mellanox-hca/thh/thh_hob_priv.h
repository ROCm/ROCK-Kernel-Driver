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

#ifndef H_THH_HOB_PRIV_H
#define H_THH_HOB_PRIV_H

#include <mosal.h>
#include <vapi.h>
#include <hh.h>
#include <tavor_dev_defs.h>
#include <thh.h>
#include <thh_hob.h>
#include <epool.h>
#include <tddrmm.h>
#include <cmdif.h>
#include <uar.h>
#include <thh_uldm.h>
#include <tmrwm.h>
#include <tcqm.h>
#include <tqpm.h>
#include <udavm.h>
#include <mcgm.h>
#include <eventp.h>
#include <vip_delay_unlock.h>

/********************* TEMPORARY DEFINES, INCLUDED UNTIL THINGS GET FIXED UP ****************/


/* ******************* END TEMPORARY DEFINES ******************* */

#define THHOBP(hca_hndl)   ((THH_hob_t)(hca_hndl->device))
#define THHOB(hca_id)    ((THH_hob_t)(HH_hca_dev_tbl[hca_id].device))

#define THH_RESERVED_PD        0
#define THH_DEV_LIM_MCG_ENABLED(hob)      (hob->dev_lims.udm == 1)


typedef struct THH_ib_props_st {
        int dummy;
} THH_ib_props_t;

/* minimum required firmware -- major_rev=1, minor_rev = 0x0015, sub_minor (patch) = 0 */
#define  THH_MIN_FW_VERSION  MAKE_ULONGLONG(0x0000000100150000)
#define  THH_MIN_FW_ERRBUF_VERSION  MAKE_ULONGLONG(0x0000000100180000)
#define  THH_MIN_FW_HIDE_DDR_VERSION  MAKE_ULONGLONG(0x0000000100180000)
#define  THH_MIN_FW_VERSION_SRQ  MAKE_ULONGLONG(0x0000000300010000)

/* Definitions for default profile */
/* NOTE:  All table sizes MUST be a power of 2 */

#define THH_DDR_ALLOCATION_VEC_SIZE     10        /* EEC not implemented yet */
typedef struct THH_ddr_allocation_vector_st {
    MT_size_t   log2_mtt_size;
    MT_size_t   log2_mpt_size;
    MT_size_t   log2_qpc_size;
    MT_size_t   log2_eqpc_size; /* QPC alt path */
    MT_size_t   log2_srqc_size;
    MT_size_t   log2_cqc_size;
    MT_size_t   log2_rdb_size;      /* in-flight rdma */
    MT_size_t   log2_uar_scratch_size;
    MT_size_t   log2_eqc_size;
    MT_size_t   log2_mcg_size;
    MT_size_t   log2_eec_size;
    MT_size_t   log2_eeec_size;    /* EEC alt path */
#if 0
    MT_size_t   log2_wqe_pool_size;
    MT_size_t   log2_uplink_qp_size;
    MT_size_t   log2_uplink_mem_size;
#endif
} THH_ddr_allocation_vector_t;

typedef struct THH_ddr_base_addr_vector_st {
    MT_phys_addr_t   mtt_base_addr;
    MT_phys_addr_t   mpt_base_addr;
    MT_phys_addr_t   qpc_base_addr;
    MT_phys_addr_t   eqpc_base_addr;
    MT_phys_addr_t   srqc_base_addr;
    MT_phys_addr_t   cqc_base_addr;
    MT_phys_addr_t   rdb_base_addr;      /* in-flight rdma */
    MT_phys_addr_t   uar_scratch_base_addr;
    MT_phys_addr_t   eqc_base_addr;
    MT_phys_addr_t   mcg_base_addr;
    MT_phys_addr_t   eec_base_addr;
    MT_phys_addr_t   eeec_base_addr;
#if 0
    MT_phys_addr_t   log2_wqe_pool_size;
    MT_phys_addr_t   log2_uplink_qp_size;
    MT_phys_addr_t   log2_uplink_mem_size;
#endif
} THH_ddr_base_addr_vector_t;

typedef enum  {
    THH_DDR_SIZE_BAD,
    THH_DDR_SIZE_32M = 1,
    THH_DDR_SIZE_64M,
    THH_DDR_SIZE_128M,
    THH_DDR_SIZE_256M,
    THH_DDR_SIZE_512M,
    THH_DDR_SIZE_1024M,
    THH_DDR_SIZE_2048M,
    THH_DDR_SIZE_4096M,
    THH_DDR_SIZE_BIG,
} THH_ddr_size_enum_t;

typedef struct THH_non_ddr_defaults_st {
    MT_bool               use_priv_udav;
    THH_ddr_size_enum_t   ddr_size_code;
    MT_size_t             ddr_size;
    MT_size_t             max_num_pds;
    MT_size_t             num_external_mem_regions;
    MT_size_t             num_mem_windows;
    MT_size_t             ddr_alloc_vec_size;
    MT_size_t             log2_max_uar;
    MT_size_t             log2_max_qps;
    MT_size_t             max_num_qps;
    MT_size_t             log2_max_srqs;
    MT_size_t             max_num_srqs;
    MT_size_t             log2_wqe_ddr_space_per_qp;
    MT_size_t             log2_max_cqs;
    MT_size_t             max_num_cqs;
    MT_size_t             log2_max_eecs;
    MT_size_t             log2_max_mcgs;
    MT_size_t             log2_mcg_entry_size;
    MT_size_t             log2_mcg_hash_size;
    MT_size_t             qps_per_mcg;
    MT_size_t             log2_max_mpt_entries;
    MT_size_t             log2_max_mtt_entries;
    MT_size_t             log2_mtt_entries_per_seg;
    MT_size_t             log2_mtt_segs_per_region;
    u_int8_t              log2_max_eqs;
    u_int8_t              log2_uar_pg_size;
    MT_size_t             max_priv_udavs;
    u_int8_t              log2_inflight_rdma_per_qp;
} THH_profile_t;

#define THH_DEF_CQ_PER_QP       1

#define TAVOR_MAX_EQ            64
#define THH_COMPL_EQ_IX          0
#define THH_IB_EQ_IX             1
#define THH_CMD_EQ_IX            2
#define THH_MT_EQ_IX             3

typedef struct THH_hob_port_info_st {
       u_int32_t    capability_bits;
}THH_hob_port_info_t;

/* catastrophic error thread structure */
typedef struct THH_hob_cat_err_thread_st {
    MOSAL_thread_t  mto;
    MOSAL_mutex_t mutex;
    MOSAL_syncobj_t start_sync; /* sync object needed on start of thread */
    MOSAL_syncobj_t stop_sync; /* sync object needed on exit of thread */
    MOSAL_syncobj_t fatal_err_sync; /* wait on fatal_err_sync object */
    struct THH_hob_st *hob;         /* pointer to this thread's HOB object */
    volatile MT_bool    have_fatal; /*TRUE ==> catastrophic error has occurred */
                           /*FALSE ==> just exit. */
} THH_hob_cat_err_thread_t;

typedef struct THH_hob_pci_info_st {
    MT_bool     is_valid;
    u_int8_t    bus;
    u_int8_t    dev_func;
    MOSAL_pci_dev_t pci_dev;
    u_int32_t   config[64];
} THH_hob_pci_info_t;

typedef struct THH_hob_st {
    /* THH_hob_create parameters */
    u_int32_t            hca_seq_num;
    THH_module_flags_t   module_flags;
    THH_hw_props_t       hw_props;

    char                 dev_name[20];
    u_int32_t            dev_id;
    HH_hca_hndl_t        hh_hca_hndl;

    THH_dev_lim_t        dev_lims;      /* QUERY_DEV_LIM */
    THH_adapter_props_t  adapter_props; /* QUERY_ADAPTER */
    THH_fw_props_t       fw_props;   /* QUERY_FW */
    THH_ddr_props_t      ddr_props;  /* QUERY_DDR */
    THH_ib_props_t       ib_props;   /* QUERY_IB  */

    THH_port_init_props_t *init_ib_props; /* VMALLOCed. One entry per port */
    /* HCA Props */
    THH_hca_props_t      hca_props;

    VAPI_hca_cap_t       hca_capabilities;   /* filled at end of open_hca, and saved for Query_hca */
    
    MT_virt_addr_t       fw_error_buf_start_va;
    u_int32_t *          fw_error_buf;  /* kmalloced buffer ready to hold info at cat error */

    THH_ddr_allocation_vector_t  ddr_alloc_size_vec;
    THH_ddr_base_addr_vector_t   ddr_alloc_base_addrs_vec;
    THH_profile_t        profile;

    /* HH Interface */
    HH_if_ops_t          if_ops;

    /* Version information */
    THH_ver_info_t       version_info;

    /* udavm information if privileged is used*/
    MT_bool              udavm_use_priv;
    VAPI_lkey_t          udavm_lkey;
    MT_virt_addr_t       udavm_table;
    MT_phys_addr_t       udavm_table_ddr;
    MT_size_t            udavm_table_size;

    /* EQ handles */
    THH_eqn_t            compl_eq;
    THH_eqn_t            ib_eq;

    /* Mutexes, etc */
    MOSAL_mutex_t        mtx;     /* used internally */ 

    /* CONTAINED OBJECTS HANDLES */
    THH_cmd_t            cmd;
    THH_ddrmm_t          ddrmm;
    THH_uldm_t           uldm;
    THH_mrwm_t           mrwm;
    THH_cqm_t            cqm;
   /*  THH_eecm_t           eecm; */
    THH_qpm_t            qpm;
    THH_srqm_t           srqm;
    THH_udavm_t          udavm;
    THH_mcgm_t           mcgm;
	THH_eventp_t         eventp;
	THH_uar_t            kar;
    MT_virt_addr_t       kar_addr;
    
    /* for THH_get_debug_info() */
    THH_mrwm_props_t     mrwm_props;

    /* fatal error handling fields */
    VAPI_event_syndrome_t fatal_syndrome;
    THH_hob_state_t      thh_state;
    MOSAL_syncobj_t	     thh_fatal_complete_syncobj;
    HH_async_eventh_t    async_eventh;   /* saved handler and context, registered by VIP */
    void*                async_ev_private_context;
    MOSAL_spinlock_t     async_spl;
    MOSAL_spinlock_t     fatal_spl;

    VIP_delay_unlock_t   delay_unlocks;
    THH_hob_cat_err_thread_t  fatal_thread_obj;

    THH_hob_pci_info_t  pci_bridge_info;
    THH_hob_pci_info_t  pci_hca_info;

} THH_hob_dev_t;

typedef struct {
  THH_hw_props_t hw_props;
  THH_profile_t profile;
  THH_ddr_base_addr_vector_t   ddr_addr_vec;
  THH_ddr_base_addr_vector_t   ddr_size_vec;
  u_int32_t                    num_ddr_addrs;
  THH_mrwm_props_t             mrwm_props;
  MT_bool                      hide_ddr;

  /* Allocated resources count */
  u_int32_t allocated_ul_res; 	/* From ULDM */
  /* (in current implementation ul_res num. is the same as UAR num., excluding UAR1) */
  u_int32_t allocated_pd;	  	/* From ULDM */
  u_int32_t allocated_cq;		/* From TCQM */
  u_int32_t allocated_qp;		/* From TQPM */
  u_int32_t allocated_mr_int;		/* From TMRWM */
  u_int32_t allocated_mr_ext;		/* From TMRWM */
  u_int32_t allocated_mw;		/* From TMRWM */
  u_int32_t allocated_mcg;		/* From MCGM */
} THH_debug_info_t ;	


HH_ret_t THH_get_debug_info(
	HH_hca_hndl_t hca_hndl, 		/*IN*/
	THH_debug_info_t *debug_info_p	/*OUT*/
);


#endif  /* H_THH_H */
