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

/* Hopefully, we'll get the following fw restrictions out... */
#define SUPPORT_DESTROY_QPI_REUSE 1
#define SUPPORT_2ERR    1
#define SUPPORT_DESTROY 1
/* #define DELAY_CONF_SPECIAL_QPS 1 */

#include <tqpm.h>
#if defined(USE_STD_MEMORY)
# include <memory.h>
#endif
#include <mtl_common.h> 
#include <mosal.h> 
#include <epool.h>
#include <tlog2.h>
#include <cmdif.h>
#include <tmrwm.h>
#include <thh_uldm.h>
#include <thh_hob.h>
#include <tddrmm.h>
#include <vapi_common.h>
#include <vip_array.h>
#include <ib_defs.h>
#include <tavor_if_defs.h>
#include <sm_mad.h>

extern void MadBufPrint(void *madbuf);
 
static void printGIDTable(THH_qpm_t  qpm);
static void printPKeyTable(THH_qpm_t  qpm);

#define CMDRC2HH_ND(cmd_rc) ((cmd_rc == THH_CMD_STAT_OK) ? HH_OK : \
                          (cmd_rc == THH_CMD_STAT_EINTR) ? HH_EINTR : HH_EFATAL)

#define ELSE_ACQ_ERROR(f) else { MTL_ERROR1("%s MOSAL_mutex_acq failed\n", f); }
#define logIfErr(f) \
  if (rc != HH_OK) { MTL_ERROR1("%s: rc=%s\n", f, HH_strerror_sym(rc)); }


#if !defined(ARR_SIZE)
# define ARR_SIZE(a)  (sizeof(a)/sizeof(a[0]))
#endif

/* These are the tunebale parameters */
enum
{
  TUNABLE_ACK_REQ_FREQ = 10,
  TUNABLE_FLIGHT_LIMIT = 9
};


enum {WQE_CHUNK_SIZE_LOG2 = 6,
      WQE_CHUNK_SIZE      = 1ul << WQE_CHUNK_SIZE_LOG2,
      WQE_CHUNK_MASK      = WQE_CHUNK_SIZE - 1
     };



typedef struct
{
  IB_wqpn_t       qpn;
  HH_srq_hndl_t   srqn; /* If invalid, equals HH_EINVAL_SRQ_HNDL */   
  u_int32_t       pd;
  u_int32_t       rdd;
  u_int32_t       cqn_snd;
  u_int32_t       cqn_rcv;

  /* The following is kept merely to support query */
  VAPI_qp_cap_t   cap;

  /* kept to support modify-qp/query-qp */
  VAPI_rdma_atom_acl_t  remote_atomic_flags;/* Enable/Disable RDMA and atomic */
  u_int8_t              qp_ous_rd_atom;      /* Maximum number of oust. RDMA read/atomic as target */
  u_int8_t              ous_dst_rd_atom;     /* Number of outstanding RDMA rd/atomic ops at destination */
  
  u_int8_t        st; /* sufficient for  THH_service_type_t - enum */
  VAPI_qp_state_t state;
  unsigned int    ssc:1;
  unsigned int    rsc:1;

  MT_virt_addr_t  wqes_buf;     /* WQEs buffer virtual address */
  MT_size_t       wqes_buf_sz;
  VAPI_lkey_t     lkey;
  THH_uar_index_t uar_index;    /* index of UAR used for this QP */
  MT_phys_addr_t  pa_ddr;
#if defined(MT_SUSPEND_QP)
  MT_bool         is_suspended;
#endif
} TQPM_sw_qpc_t;


typedef struct
{
  signed char  tab[VAPI_ERR+1][VAPI_ERR+1]; /* -1 or THH_qpee_transition_t */
} State_machine;

static const VAPI_special_qp_t qp_types[] = {
  VAPI_SMI_QP, VAPI_GSI_QP,          /* _Used_ for special QPs */
  VAPI_RAW_IPV6_QP, VAPI_RAW_ETY_QP  /* Not used: but supported for query */
};
enum {n_qp_types = ARR_SIZE(qp_types)};

typedef struct
{
  u_int8_t                 n_ports;
  MT_bool                  configured;
  IB_wqpn_t                first_sqp_qpn;     /* Above FW reserved QPCs */
  THH_port_init_props_t*   port_props;
  TQPM_sw_qpc_t**          sqp_ctx;   /* Context for special QPs is outside of the VIP_array */
} Special_QPs;


/* The main QP-manager structure 
 * Note that {(EPool_t flist), (THH_ddrmm_t ddrmm)}
 * have their own mutex-es controlling multi-threads.
 */
typedef struct THH_qpm_st
{
  /* Capabilities */
  u_int8_t        log2_max_qp;
  u_int32_t       rdb_base_index;
  u_int8_t        log2_max_outs_rdma_atom;
  u_int8_t        log2_max_outs_dst_rd_atom;
  u_int32_t       max_outs_rdma_atom; /* convenient 2^log2_max_outs_rdma_atom */
  u_int32_t       idx_mask;           /* convenient (2^log2_max_qp) - 1 */

  /* SW resources tables */
  Special_QPs     sqp_info;
  VIP_array_p_t   qp_tbl;             /* Index 0 into the table is first_sqp_qpn+NUM_SQP */
  u_int32_t       first_rqp;          /* First regular QP */
  u_int8_t*       qpn_prefix;         /* persistant 8 bit prefix change for QP numbers */
  /* In order to avoid holding to much memory for QP numbers prefix we:
   * 1) Manipulate only upper 8 bits of the 24 bit QPN 
   * 2) Hold only MAX_QPN_PREFIX numbers to be shared among QPs with the same lsb of index */
  MOSAL_mutex_t   mtx; /* protect sqp_ctx and pkey/sgid tables */

  /* convenient handle saving  */
  THH_hob_t       hob;
  THH_cmd_t       cmd_if;
  THH_uldm_t      uldm;
  THH_mrwm_t      mrwm_internal;
  THH_ddrmm_t     ddrmm;
  
  /* mirror of the port gid tbl - for special qps*/
  IB_gid_t* sgid_tbl[NUM_PORTS];
  u_int16_t num_sgids[NUM_PORTS];

  /* mirror of the port qp1 pkey - values are kept CPU endian - little endian*/
  VAPI_pkey_t*    pkey_tbl[NUM_PORTS];   
  u_int16_t       pkey_tbl_sz[NUM_PORTS];
  VAPI_pkey_ix_t  qp1_pkey_idx[NUM_PORTS];
  MT_bool         port_active[NUM_PORTS];
} TQPM_t;

static MT_bool        constants_ok = TRUE; /* guarding this tqpm module */
static State_machine  state_machine; /* const after THH_qpm_init */
static const u_int32_t  valid_tavor_ibmtu_mask = 
                        (1ul << MTU256) | 
                        (1ul << MTU512) | 
                        (1ul << MTU1024) | 
                        (1ul << MTU2048);


static u_int8_t   native_page_shift;
static u_int32_t  native_page_size;
static u_int32_t  native_page_low_mask;

/************************************************************************/
/************************************************************************/
/*                         private functions                            */



/************************************************************************/
static inline MT_bool is_sqp(THH_qpm_t qpm,IB_wqpn_t qpn) 
{
    u_int32_t   qp_idx = qpn & qpm->idx_mask;

    return ((qp_idx >= qpm->sqp_info.first_sqp_qpn) && (qp_idx < qpm->first_rqp));
}

/************************************************************************/
static inline MT_bool is_sqp0(THH_qpm_t qpm,IB_wqpn_t qpn,IB_port_t* port_p) 
{
    u_int32_t   qp_idx = qpn & qpm->idx_mask;
    MT_bool     is_true;
    
    is_true = ((qp_idx >= qpm->sqp_info.first_sqp_qpn) && 
            (qp_idx < qpm->sqp_info.first_sqp_qpn + qpm->sqp_info.n_ports));
    if (is_true == TRUE) {
        *port_p = ((qp_idx-(qpm->sqp_info.first_sqp_qpn & qpm->idx_mask))%qpm->sqp_info.n_ports) + 1;
    }

    MTL_DEBUG1(MT_FLFMT("%s: qpn=0x%x, mask=0x%x, first=0x%x, nports=0x%x, *port = %d, ret=%s"),__func__,
               qpn, qpm->idx_mask,qpm->sqp_info.first_sqp_qpn, qpm->sqp_info.n_ports, *port_p,
               (is_true==FALSE)?"FALSE":"TRUE");
    return is_true;
}

/************************************************************************/
static inline MT_bool is_sqp1(THH_qpm_t qpm,IB_wqpn_t qpn,IB_port_t* port_p) 
{
  u_int32_t   qp_idx = qpn & qpm->idx_mask;
  MT_bool     is_true = FALSE;

  is_true = ((qp_idx >= qpm->sqp_info.first_sqp_qpn + qpm->sqp_info.n_ports) && 
          (qp_idx < qpm->sqp_info.first_sqp_qpn + (2 * qpm->sqp_info.n_ports) ));
  if (is_true == TRUE) {
      *port_p = ((qp_idx-((qpm->sqp_info.first_sqp_qpn & qpm->idx_mask)+qpm->sqp_info.n_ports))%qpm->sqp_info.n_ports) + 1;
  }
  MTL_DEBUG1(MT_FLFMT("%s: qpn=0x%x, mask=0x%x, first=0x%x, nports=0x%x, *port = %d, ret=%s"),__func__,
             qpn, qpm->idx_mask,qpm->sqp_info.first_sqp_qpn, qpm->sqp_info.n_ports, *port_p,
             (is_true==FALSE)?"FALSE":"TRUE");
  return is_true;
}
/************************************************************************/
static inline MT_bool check_2update_pkey(VAPI_qp_state_t cur_state,VAPI_qp_state_t new_state,
                                         VAPI_qp_attr_mask_t* attr_mask_p)
{
    if (*attr_mask_p & QP_ATTR_PKEY_IX)
    {
        /*obligatory */
        if ((cur_state == VAPI_RESET) && (new_state == VAPI_INIT))
        {
            return TRUE;
        }
        /* optional */
        if ((cur_state == VAPI_INIT) && (new_state == VAPI_RTR))
        {
            return TRUE;
        }
        /* optional */
        if ((cur_state == VAPI_SQD) && (new_state == VAPI_RTS))
        {
            return TRUE;
        }
    }
    return FALSE;
}
/************************************************************************/
static MT_bool  check_constants(void)
{
  static const  VAPI_qp_state_t  states[] = 
  {
    VAPI_INIT,VAPI_RESET,VAPI_RTR,VAPI_RTS,VAPI_SQD,VAPI_SQE,VAPI_ERR
  };
  static const IB_mtu_t  mtu_vals[] =  /* Check all IB MTU values */
  {                                    /*       not just Tavor's. */
    MTU256, MTU512, MTU1024, MTU2048, MTU4096
  };
  int  i;
  for (i = 0, constants_ok = TRUE;  i != ARR_SIZE(states);  ++i)
  {
    constants_ok = constants_ok && (0 <= states[i]) && (states[i] < VAPI_ERR+1);
  }
  for (i = 0;  i != ARR_SIZE(mtu_vals);  ++i)
  {
    constants_ok = constants_ok && (0 <= mtu_vals[i]) && (mtu_vals[i] < 32);
  }
  MTL_DEBUG4(MT_FLFMT("constants_ok=%d"), constants_ok);
  return constants_ok;
} /* check_constants */


/************************************************************************/
static inline int  defined_qp_state(int qp_state)
{
  int  def = ((VAPI_RESET <= qp_state) && (qp_state <= VAPI_ERR));
  MTL_DEBUG4(MT_FLFMT("qp_state=%d, def=%d"), qp_state, def);
  return def;
} /* defined_qp_state */


/************************************************************************/
static void init_state_machine(State_machine*  xs2s)
{
  int  fi, ti;

  for (fi = 0;  fi != (VAPI_ERR+1);  ++fi)
  {
    /* first, initialize as undefined */
    for (ti = 0;  ti != VAPI_ERR;  ++ti)
    {
      xs2s->tab[fi][ti] = -1;
    }
    xs2s->tab[fi][VAPI_ERR] = QPEE_TRANS_2ERR;   	/* undef later fi=VAPI_RESET */
    xs2s->tab[fi][VAPI_RESET] = QPEE_TRANS_ERR2RST;
  }

  xs2s->tab[VAPI_RESET][VAPI_ERR]  = -1; /* Apr/21/2002 meeting */

  /* see state graph in Tavor-PRM, (12.3 Command Summary) */
  xs2s->tab[VAPI_RESET][VAPI_INIT]  = QPEE_TRANS_RST2INIT;
  xs2s->tab[VAPI_INIT] [VAPI_INIT] 	= QPEE_TRANS_INIT2INIT;
  xs2s->tab[VAPI_INIT] [VAPI_RTR]   = QPEE_TRANS_INIT2RTR;
  xs2s->tab[VAPI_RTR]  [VAPI_RTS]   = QPEE_TRANS_RTR2RTS;
  xs2s->tab[VAPI_RTS]  [VAPI_RTS]   = QPEE_TRANS_RTS2RTS;
  xs2s->tab[VAPI_SQE]  [VAPI_RTS]   = QPEE_TRANS_SQERR2RTS;
  xs2s->tab[VAPI_RTS]  [VAPI_SQD]   = QPEE_TRANS_RTS2SQD;
  xs2s->tab[VAPI_SQD]  [VAPI_RTS]   = QPEE_TRANS_SQD2RTS;
  xs2s->tab[VAPI_ERR]  [VAPI_RESET] = QPEE_TRANS_ERR2RST;
} /* init_state_machine */
/*******************************************************************************/
/*
 *      init_sgid_table
 */
static HH_ret_t init_sgid_tbl(THH_qpm_t qpm)
{
    HH_ret_t ret = HH_OK;    
    HH_hca_hndl_t hca_hndl;
    u_int16_t     tbl_len_out;
    MT_bool destroy_tbl = FALSE;
    int i;

    if (qpm == NULL) {
        MTL_ERROR1("[%s]: ERROR: NULL qpm value \n",__FUNCTION__);
	return HH_EINVAL;
    }
    for (i=0; i< qpm->sqp_info.n_ports; i++)
    {
        qpm->num_sgids[i] = 0;
    }

    ret = THH_hob_get_hca_hndl(qpm->hob,&hca_hndl); 
    if (ret != HH_OK)
    {
        MTL_ERROR1("[%s]: ERROR: THH_hob_get_hca_hndl failed \n",__FUNCTION__);
        return ret;
    }
    
    for (i=0; i< qpm->sqp_info.n_ports; i++)
    {
        qpm->sgid_tbl[i] = (IB_gid_t*)TQPM_GOOD_ALLOC((sizeof(IB_gid_t) * DEFAULT_SGID_TBL_SZ)); 
        //query the gid table for all ports
        ret = THH_hob_init_gid_tbl(hca_hndl,i+1,DEFAULT_SGID_TBL_SZ,&tbl_len_out,qpm->sgid_tbl[i]);
        if (ret != HH_OK)
        {
            if (ret == HH_EAGAIN)
            {
                TQPM_GOOD_FREE(qpm->sgid_tbl[i],(sizeof(IB_gid_t) * DEFAULT_SGID_TBL_SZ));
                qpm->sgid_tbl[i] = (IB_gid_t*)TQPM_GOOD_ALLOC(sizeof(IB_gid_t) * tbl_len_out); 
                ret = THH_hob_init_gid_tbl(hca_hndl,i+1,tbl_len_out,&tbl_len_out,qpm->sgid_tbl[i]);
                if (ret != HH_OK)
                {
                    destroy_tbl = TRUE;
                }
        
            }else destroy_tbl = TRUE; 
        }
        
        if (destroy_tbl)
        {
            MTL_ERROR1("[%s]: ERROR: THH_hob_get_gid_tbl failed for port %d\n",__FUNCTION__,i+1);    
            TQPM_GOOD_FREE(qpm->sgid_tbl[i],(sizeof(IB_gid_t) * tbl_len_out) );
            qpm->sgid_tbl[i] = NULL;
            qpm->num_sgids[i] = 0;
        }else
        {
            qpm->num_sgids[i] = tbl_len_out;
        }
        
        destroy_tbl = FALSE;
    }
    
    //TBD: ret for one port failure
    MT_RETURN(ret);
}

/*
 *      init_pkey_table
 */
static HH_ret_t init_pkey_tbl(THH_qpm_t qpm)
{
    HH_ret_t ret = HH_OK;    
    HH_hca_hndl_t hca_hndl;
    u_int16_t     tbl_len_out;
    MT_bool destroy_tbl = FALSE;
    int i;

    if (qpm == NULL) {
        MTL_ERROR1("[%s]: ERROR: NULL qpm value \n",__FUNCTION__);
	return HH_EINVAL;
    }
    for (i=0; i< qpm->sqp_info.n_ports; i++)
    {
        qpm->qp1_pkey_idx[i] = 0xffff;
    }
    
    for (i=0; i< qpm->sqp_info.n_ports; i++)
    {
        qpm->pkey_tbl_sz[i] = 0;
    }

    ret = THH_hob_get_hca_hndl(qpm->hob,&hca_hndl); 
    if (ret != HH_OK)
    {
        MTL_ERROR1("[%s]: ERROR: THH_hob_get_hca_hndl failed \n",__FUNCTION__);
        return ret;
    }
    
    for (i=0; i< qpm->sqp_info.n_ports; i++)
    {
        qpm->pkey_tbl[i] = (VAPI_pkey_t*)TQPM_GOOD_ALLOC((sizeof(VAPI_pkey_t)*DEFAULT_PKEY_TBL_SZ)); 
        
        //query the pkey table for all ports 
        ret = THH_hob_init_pkey_tbl(hca_hndl,i+1,DEFAULT_PKEY_TBL_SZ,&tbl_len_out,qpm->pkey_tbl[i]);
        if (ret != HH_OK)
        {
            if (ret == HH_EAGAIN)
            {
                TQPM_GOOD_FREE(qpm->pkey_tbl[i],(sizeof(VAPI_pkey_t)*DEFAULT_PKEY_TBL_SZ));
                qpm->pkey_tbl[i] = (VAPI_pkey_t*)TQPM_GOOD_ALLOC((sizeof(VAPI_pkey_t)* tbl_len_out)); 
                ret = THH_hob_init_pkey_tbl(hca_hndl,i+1,tbl_len_out,&tbl_len_out,qpm->pkey_tbl[i]);
                if (ret != HH_OK)
                {
                    destroy_tbl = TRUE;
                }
        
            }else destroy_tbl = TRUE; 
        }
        
        if (destroy_tbl)
        {
            MTL_ERROR1("[%s]: ERROR: THH_hob_get_pkey_tbl failed for port %d\n",__FUNCTION__,i+1);    
            TQPM_GOOD_FREE(qpm->pkey_tbl[i],(sizeof(VAPI_pkey_t)*tbl_len_out));
            qpm->pkey_tbl[i] = NULL;
            qpm->pkey_tbl_sz[i] = 0;
        }else 
        {
            qpm->pkey_tbl_sz[i] = tbl_len_out;
        }
            
        destroy_tbl = FALSE;
    }
    
    //TBD: ret for one port failure
    MT_RETURN(ret);
}

/************************************************************************/
static MT_bool  copy_port_props(TQPM_t* qpm, const THH_qpm_init_t* init_attr_p)
{
  MT_bool  ok = TRUE;
  const THH_port_init_props_t*  in_port_props = init_attr_p->port_props;
  if (in_port_props)
  {
    unsigned int            n_ports =   init_attr_p->n_ports;
    THH_port_init_props_t*  props = TNMALLOC(THH_port_init_props_t, n_ports);
    if (props)
    {
      memcpy(props, in_port_props, n_ports * sizeof(THH_port_init_props_t));
      qpm->sqp_info.port_props = props;
    }
    else
    {
      MTL_ERROR1(MT_FLFMT("Allocating port_props (%d) failed"), n_ports);
      ok = FALSE;
    }
  } else {
    qpm->sqp_info.port_props = NULL;
  }
  MTL_DEBUG4(MT_FLFMT("copy_port_props: qpm=0x%p, ok=%d"), qpm, ok);
  return ok;
} /* copy_port_props */



/************************************************************************/
static HH_ret_t  conf_special_qps(TQPM_t* qpm)
{
  HH_ret_t      rc = HH_OK;
  static const VAPI_special_qp_t qp01[2] = {VAPI_SMI_QP, VAPI_GSI_QP};
  Special_QPs*                   sqp = &qpm->sqp_info;
  unsigned int                   n_ports = sqp->n_ports;
  IB_wqpn_t                      qpn = sqp->first_sqp_qpn;
  unsigned                       ti;
  for (ti = 0;  (ti != 2) && (rc == HH_OK);  ++ti, qpn += n_ports)
  {
    VAPI_special_qp_t qp_type = qp01[ti];
    THH_cmd_status_t  cmd_rc = 
      THH_cmd_CONF_SPECIAL_QP(qpm->cmd_if, qp_type, qpn);
    switch(cmd_rc) {
    case THH_CMD_STAT_OK:
        rc = HH_OK;
        break;
    case THH_CMD_STAT_EINTR:
        rc = HH_EINTR;
        break;
    default:
        MTL_ERROR1(MT_FLFMT("THH_cmd_CONF_SPECIAL_QP ti=%d, qpn=0x%x, crc=%d=%s"),
                   ti, qpn, cmd_rc, str_THH_cmd_status_t(cmd_rc));
        rc = HH_EFATAL;
    }
  }
  MTL_DEBUG4(MT_FLFMT("rc=%d"), rc);
  return rc;
} /* conf_special_qps */


/************************************************************************/
/*  We ensure allocating and freeing DDR memory of sizes
 *  that are least native page size. The rational is that this is 
 *  the minimal mappable memory.
 */
static MT_size_t  complete_pg_sz(MT_size_t  sz)
{
  MTL_DEBUG4(MT_FLFMT("complete_pg_sz: sz="SIZE_T_FMT), sz);
  if ((sz & native_page_low_mask) != 0)
  {
    MTL_DEBUG4(MT_FLFMT("fixing up non page-aligned size="SIZE_T_FMT), sz);
    sz &= ~((MT_size_t)native_page_low_mask);
    sz += native_page_size;
    MTL_DEBUG4(MT_FLFMT("complete_pg_sz: enlarging to sz="SIZE_T_FMT), sz);
  }
  return sz;
} /* complete_pg_sz */


/************************************************************************/
/*  We ensure the buffer completely falls within a 4Gb block.
 *  Now, 4G = 4*1*K*1K*1K = 4*1024^3 = 2^(2+3*10) = 2^32  ==> 32 bits.
 *  We take advantage of being able to shift right by WQE_CHUNK_SIZE_LOG2 bits.
 *  So instead of testing 32-bit overflow,
 *  we test (32-WQE_CHUNK_SIZE_LOG2)-bit overflow.
 *  Thus being able to test using 32 bits calculations.
 */
static MT_bool  within_4GB(MT_virt_addr_t  buf, MT_size_t sz)
{
  static const unsigned int   shift_4GdivWQCZ = 32 - WQE_CHUNK_SIZE_LOG2;
  MT_virt_addr_t  bbeg_rsh =             buf >> WQE_CHUNK_SIZE_LOG2;
  MT_virt_addr_t  bend_rsh = bbeg_rsh + (sz  >> WQE_CHUNK_SIZE_LOG2);
  MT_bool      same_4GB = ((bbeg_rsh >> shift_4GdivWQCZ) ==
                           (bend_rsh >> shift_4GdivWQCZ));
  MTL_DEBUG4(MT_FLFMT("same_4GB=%d"), same_4GB);
  return same_4GB;
} /* within_4GB */


/************************************************************************/
/* If buffer is supplied, validate address.
 * If null buffer supplied, alloc a physical DDR buffer and map it.
 * We have to check for 'Within 4GB' anyway.
 * When mapping, we allow a second chance to pass the 4GB restriction.
 * If allocated, we also return the physical memory address.
 */
static HH_ret_t  check_make_wqes_buf(
  THH_qpm_t                qpm,              
  THH_qp_ul_resources_t*   qp_ul_resources_p,
  HH_pd_hndl_t             pd,
  MOSAL_protection_ctx_t*  ctx_p,
  MT_phys_addr_t*          pa_p
)
{
  HH_ret_t     rc = HH_OK;
  MT_virt_addr_t  wqes_buf = qp_ul_resources_p->wqes_buf;
  MT_size_t    buf_sz = qp_ul_resources_p->wqes_buf_sz;

  MTL_DEBUG4(MT_FLFMT("wqes_buf="VIRT_ADDR_FMT", buf_sz="SIZE_T_FMT), wqes_buf, buf_sz);
  if (wqes_buf != 0)
  {
    MT_virt_addr_t    unalligned_bits = wqes_buf & WQE_CHUNK_MASK;
    if ((unalligned_bits != 0) || !within_4GB(wqes_buf, buf_sz))
    {
      wqes_buf = 0;
      rc = HH_EINVAL_PARAM;
    }
  }
  else if (buf_sz != 0)  /* When uses SRQ, buf_sz may be 0 */
  {
    rc = THH_uldm_get_protection_ctx(qpm->uldm, pd, ctx_p);
    if (rc == HH_OK)
    {
      qp_ul_resources_p->wqes_buf_sz = buf_sz =
         complete_pg_sz(buf_sz);  /* fixed up size returned */
      rc = THH_ddrmm_alloc(qpm->ddrmm, buf_sz, native_page_shift, pa_p);
      MTL_DEBUG4(MT_FLFMT("rc=%d"), rc);
      if (rc == HH_OK)
      {
        static const MOSAL_mem_flags_t
          mem_flags = MOSAL_MEM_FLAGS_NO_CACHE |
                      MOSAL_MEM_FLAGS_PERM_READ |
                      MOSAL_MEM_FLAGS_PERM_WRITE;
        MT_virt_addr_t  va_1stmapped  = wqes_buf = (MT_virt_addr_t)
          MOSAL_map_phys_addr(*pa_p, buf_sz, mem_flags, *ctx_p);
        if (wqes_buf && !within_4GB(wqes_buf, buf_sz))
        { /* bad luck? give mapping another chance, to fit, before unmap!  */
          wqes_buf = (MT_virt_addr_t)MOSAL_map_phys_addr(
                                    *pa_p, buf_sz, mem_flags, *ctx_p);
          MOSAL_unmap_phys_addr(*ctx_p, (MT_virt_addr_t)va_1stmapped, buf_sz);
          if (wqes_buf && !within_4GB(wqes_buf, buf_sz))
          {
            MOSAL_unmap_phys_addr(*ctx_p, (MT_virt_addr_t)wqes_buf, buf_sz);
            wqes_buf = 0;
          }
        }
        qp_ul_resources_p->wqes_buf = wqes_buf;
        if (wqes_buf == 0)
        {
          THH_ddrmm_free(qpm->ddrmm, *pa_p, buf_sz);
          *pa_p = 0;
          rc = HH_EAGAIN;
        }
      }
    }
  }
  return rc;
} /* check_make_wqes_buf */


/************************************************************************/
static MT_bool  attr_hh2swqpc(
  const HH_qp_init_attr_t*  hh_attr,
  MT_bool                   mlx,
  TQPM_sw_qpc_t*            sw_qpc_p
)
{
  MT_bool  ok = TRUE;

  memset(sw_qpc_p, 0, sizeof(TQPM_sw_qpc_t));
  sw_qpc_p->pd      = hh_attr->pd;
  sw_qpc_p->rdd     = hh_attr->rdd;
  sw_qpc_p->srqn    = hh_attr->srq;
  sw_qpc_p->cqn_snd = hh_attr->sq_cq;
  sw_qpc_p->cqn_rcv = hh_attr->rq_cq;
  sw_qpc_p->cap     = hh_attr->qp_cap;

  /* st  THH_service_type_t */
  if (mlx)
  {
    sw_qpc_p->st = THH_ST_MLX;
  }
  else
  {
    switch (hh_attr->ts_type)
    {
      case VAPI_TS_RC:  sw_qpc_p->st = THH_ST_RC;  break;
//      JPM:  RD and UC are not currently supported
//      case VAPI_TS_RD:  summ_p->st = THH_ST_RD;  break;
      case VAPI_TS_UC:  sw_qpc_p->st = THH_ST_UC;  break;
      case VAPI_TS_UD:  sw_qpc_p->st = THH_ST_UD;  break;
      default: ok = FALSE; MTL_ERROR1(MT_FLFMT("ts_type=%d"), hh_attr->ts_type);
    }
  }

  sw_qpc_p->ssc = (hh_attr->sq_sig_type == VAPI_SIGNAL_ALL_WR);
  sw_qpc_p->rsc = (hh_attr->rq_sig_type == VAPI_SIGNAL_ALL_WR);
  return ok;
} /* attr_summary */


/************************************************************************/
HH_ret_t  create_qp(
  THH_qpm_t               qpm,               /* IN */
  HH_qp_init_attr_t*      init_attr_p,       /* IN */
  MT_bool                 mlx,               /* IN */
  THH_qp_ul_resources_t*  qp_ul_resources_p, /* IO */
  TQPM_sw_qpc_t*          new_qp_p           /* IN */
)
{
  VAPI_lkey_t             lkey;
  HH_ret_t                rc, mrc = HH_ERR; 
  MT_phys_addr_t             pa_ddr = 0;
  MOSAL_protection_ctx_t  ctx;

  rc = (attr_hh2swqpc(init_attr_p, mlx, new_qp_p) ? HH_OK : HH_EINVAL_SERVICE_TYPE);
  if (rc == HH_OK)
  {
    rc = check_make_wqes_buf(qpm, qp_ul_resources_p, init_attr_p->pd, 
                             &ctx, &pa_ddr);
  }
  if ((rc == HH_OK) && (qp_ul_resources_p->wqes_buf_sz != 0))
    /* If there is WQEs buffer to register */
  {
    THH_internal_mr_t  params;
    memset(&params, 0, sizeof(params));
    params.start        = qp_ul_resources_p->wqes_buf,
    params.size         = qp_ul_resources_p->wqes_buf_sz;
    params.pd           = init_attr_p->pd;
    params.vm_ctx       = ctx;
    params.force_memkey = FALSE;
    params.memkey       = (VAPI_lkey_t)0;
    if (pa_ddr)
    {
      VAPI_phy_addr_t  phy_array = (VAPI_phy_addr_t)pa_ddr;   /* 1 element array - for register internal */
      params.num_bufs     = 1;
      params.phys_buf_lst = &phy_array;        /* Addresses of automatic */
      params.buf_sz_lst   = &params.size;   /*   automatic variables! */
    }
    mrc = THH_mrwm_register_internal(qpm->mrwm_internal, &params, &lkey);
    rc = mrc;
  }
  if (rc == HH_OK)
  {
    /* save parameters in this manager */
    new_qp_p->state= VAPI_RESET;
    new_qp_p->wqes_buf= qp_ul_resources_p->wqes_buf;
    new_qp_p->wqes_buf_sz= qp_ul_resources_p->wqes_buf_sz;
    new_qp_p->uar_index= qp_ul_resources_p->uar_index;
    new_qp_p->lkey   = (qp_ul_resources_p->wqes_buf_sz != 0) ? lkey : 0/*Invalid*/;
    new_qp_p->pa_ddr = pa_ddr;
  }
  if (rc != HH_OK)
  { /* clean */
    if (mrc == HH_OK) { THH_mrwm_deregister_mr(qpm->mrwm_internal, lkey); }
    if (pa_ddr != 0)
    {
      MT_size_t  wqes_buf_sz = qp_ul_resources_p->wqes_buf_sz; /* pg complete */
      MOSAL_unmap_phys_addr(ctx, (MT_virt_addr_t)qp_ul_resources_p->wqes_buf, 
                            wqes_buf_sz);
      THH_ddrmm_free(qpm->ddrmm, pa_ddr, wqes_buf_sz);
    }
  }
  MTL_DEBUG4(MT_FLFMT("rc=%d, ul_res->wqes_buf="VIRT_ADDR_FMT), 
                      rc, qp_ul_resources_p->wqes_buf);
  return  rc;
} /* create_qp */




/************************************************************************/
static void  udav2qpc_path(const VAPI_ud_av_t* av, THH_address_path_t* path)
{
  path->sl            = av->sl;
  path->my_lid_path_bits = av->src_path_bits;
  path->flow_label    = av->flow_label;
  path->hop_limit     = av->hop_limit;
  path->max_stat_rate = (av->static_rate == 0) ? 0 : 1 ; /* IPD=0 -> 0 , IPD=3 -> 1, everything else ->1 */
  path->g             = av->grh_flag;
  path->mgid_index    = av->sgid_index;
  path->rlid          = av->dlid;
  path->tclass        = av->traffic_class;
  memcpy(&path->rgid, &av->dgid, sizeof(path->rgid));
} /* udav2qpc_path */


/************************************************************************/
static IB_mtu_t  log2mtu_to_ib_mtu(u_int8_t  lg2mtu)
{
  IB_mtu_t  ib_mtu = TAVOR_LOG2_MAX_MTU;
  switch (lg2mtu)
  {
    case  8: ib_mtu = MTU256;   break;
    case  9: ib_mtu = MTU512;   break;
    case 10: ib_mtu = MTU1024;  break;
    case 11: ib_mtu = MTU2048;  break;
    case 12: ib_mtu = MTU4096;  break;
    default:
      MTL_ERROR1(MT_FLFMT("Unsupported MTU for log2(max_msg)=%d, use ibmtu=%d"),
                 lg2mtu, ib_mtu);
  }
  return ib_mtu;
} /* log2mtu_to_ib_mtu */


/************************************************************************/
static void  qpc_path2udav(const THH_address_path_t* path, VAPI_ud_av_t* av)
{
  av->sl            = path->sl;
  av->src_path_bits = path->my_lid_path_bits;
  av->flow_label    = path->flow_label;
  av->hop_limit     = path->hop_limit;
  av->static_rate   = (path->max_stat_rate == 0) ?  0 : 3;
  av->grh_flag      = path->g;
  av->sgid_index    = path->mgid_index;
  av->dlid          = path->rlid;
  av->traffic_class = path->tclass;
  memcpy(&av->dgid, &path->rgid, sizeof(path->rgid));
} /* qpc_path2udav */


/************************************************************************/
static void  qpc_default(THH_qpee_context_t*  qpc_p)
{
   memset(qpc_p, 0, sizeof(THH_qpee_context_t));
   qpc_p->ver          = 0;
   //qpc_p->te           = 1;
    /*qpc_p->ce = 1; */
   qpc_p->ack_req_freq = TUNABLE_ACK_REQ_FREQ;
   qpc_p->flight_lim   = TUNABLE_FLIGHT_LIMIT;
   qpc_p->ric          = FALSE;  /* Provide E2E credits in ACKs */
   qpc_p->sic          = FALSE;  /* Consider to E2E credits */
   qpc_p->msg_max      = 31; /* HW checks message <= (QP MTU, UD msg_max) */
   qpc_p->mtu          = MTU2048;
} /* qpc_default */


/************************************************************************/
/* Initialize THH_qpee_context_t structure with
 * attributes given or computed upon QP creation.
 *
 */
static void  init2qpc_using_create_values(
  const TQPM_sw_qpc_t* qp_p,
  THH_qpee_context_t*  qpc_p
)
{

  qpc_p->st           = qp_p->st;
  qpc_p->pd           = qp_p->pd;
  qpc_p->rdd          = qp_p->rdd;
  qpc_p->srq          = (qp_p->srqn != HH_INVAL_SRQ_HNDL);  
  qpc_p->srqn         = qp_p->srqn;
  qpc_p->cqn_snd      = qp_p->cqn_snd;
  qpc_p->cqn_rcv      = qp_p->cqn_rcv;
  qpc_p->ssc          = qp_p->ssc;
  qpc_p->rsc          = qp_p->rsc;

  qpc_p->usr_page     = qp_p->uar_index;
  qpc_p->wqe_base_adr = (sizeof(MT_virt_addr_t) <= 4 
                         ? (u_int32_t)0
                         : (u_int32_t)(((u_int64_t)qp_p->wqes_buf) >> 32));
  qpc_p->wqe_lkey     = qp_p->lkey;
} /* init2qpc_using_create_values */



/************************************************************************/
static void qpc2vapi_attr(
  const THH_qpee_context_t* qpc_p, 
  VAPI_qp_attr_t* qp_attr_p
)
{
  VAPI_rdma_atom_acl_t  aflags = 0;
  memset(qp_attr_p, 0, sizeof(*qp_attr_p));
  qp_attr_p->qp_state            = qpc_p->state;
  qp_attr_p->sq_draining		 = qpc_p->sq_draining;
  qp_attr_p->qp_num              = qpc_p->local_qpn_een;
  aflags |= (qpc_p->rae ? VAPI_EN_REM_ATOMIC_OP : 0);
  aflags |= (qpc_p->rwe ? VAPI_EN_REM_WRITE : 0);
  aflags |= (qpc_p->rre ? VAPI_EN_REM_READ : 0);
  qp_attr_p->remote_atomic_flags = aflags;
  qp_attr_p->qkey                = qpc_p->q_key;
  qp_attr_p->path_mtu            = qpc_p->mtu;
  switch (qpc_p->pm_state)
  {
    case PM_STATE_MIGRATED: qp_attr_p->path_mig_state = VAPI_MIGRATED; break;
    case PM_STATE_REARM:    qp_attr_p->path_mig_state = VAPI_REARM;    break;
    case PM_STATE_ARMED:    qp_attr_p->path_mig_state = VAPI_ARMED;    break;
    default: ; /* hmmm... */
  }
  qp_attr_p->rq_psn              = qpc_p->next_rcv_psn;
  qp_attr_p->sq_psn              = qpc_p->next_send_psn;
  qp_attr_p->qp_ous_rd_atom      = ((qpc_p->rae || qpc_p->rre)
                                   ? 1u << qpc_p->rra_max : 0);
  qp_attr_p->ous_dst_rd_atom     = ((qpc_p->sre==0)&&(qpc_p->sae)==0) ? 0 : 1u << qpc_p->sra_max;
  qp_attr_p->min_rnr_timer       = qpc_p->min_rnr_nak;
  qp_attr_p->dest_qp_num         = qpc_p->remote_qpn_een;
  qp_attr_p->pkey_ix             = qpc_p->primary_address_path.pkey_index;
  qp_attr_p->port                = qpc_p->primary_address_path.port_number;
  qpc_path2udav(&qpc_p->primary_address_path, &qp_attr_p->av);
  qp_attr_p->timeout             = qpc_p->primary_address_path.ack_timeout;
  qp_attr_p->retry_count         = qpc_p->retry_count;
  qp_attr_p->rnr_retry           = qpc_p->primary_address_path.rnr_retry;
  qp_attr_p->alt_pkey_ix         = qpc_p->alternative_address_path.pkey_index;
  qp_attr_p->alt_port            = qpc_p->alternative_address_path.port_number;
  qpc_path2udav(&qpc_p->alternative_address_path, &qp_attr_p->alt_av);
  qp_attr_p->alt_timeout         = qpc_p->alternative_address_path.ack_timeout;
  /* qp_attr_p->alt_retry_count     = qpc_p->alternative_address_path. */
  //qp_attr_p->alt_rnr_retry       = qpc_p->alternative_address_path.rnr_retry;
} /* qpc2vapi_attr */



/************************************************************************/
/* Transfer VAPI_qp_attr_t struct to THH_qpee_context_t struct          
 * Consider the caller attr_mask for generate opt_mask for the 
 * command interface.
 */
static HH_ret_t  vapi2qpc_modify(
  THH_qpm_t              qpm,
  TQPM_sw_qpc_t*         qp_p,
  const VAPI_qp_attr_t*  attr_p,
  const THH_qpee_transition_t  trans,
  const u_int32_t        attr_mask,
  THH_qpee_context_t*    qpc_p,
  u_int32_t*             opt_mask_p
)
{
  HH_ret_t    rc = HH_OK;
  u_int32_t   opt_mask = 0;
  IB_port_t   sqp_port;
  MT_bool		  is_sqp = (is_sqp0(qpm,qp_p->qpn,&sqp_port) || is_sqp1(qpm,qp_p->qpn,&sqp_port));
  
  qpc_p->st= qp_p->st;

  if (attr_mask & QP_ATTR_CAP)
  {
    /* resizing WQ size (QP size) not supported */
    rc = HH_ENOSYS;
    goto done;
  }
  
  if (attr_mask & QP_ATTR_SCHED_QUEUE) {
    qpc_p->sched_queue = attr_p->sched_queue;
    opt_mask |= TAVOR_IF_QPEE_OPTPAR_SCHED_QUEUE; /* For INIT2RTR and SQD2RTS */
  } else { 
    /* The default assignment below will be effective only on RST2INIT 
     * when sched_queue is not explicitly provided (but is required parameter) */
    qpc_p->sched_queue = attr_p->av.sl; 
  }
  
  /* if (1 || attr_mask & QP_ATTR_QP_NUM) */
  {
    qpc_p->local_qpn_een = attr_p->qp_num;
  }
  qpc_p->sae = qpc_p->swe = qpc_p->sre = 1; /* Enforcement only on responder side (per IB) */
  
  
  if (attr_mask & QP_ATTR_PKEY_IX)
  {
    /* error should have been checked in upper level, so just C-implicit mask */
    qpc_p->primary_address_path.pkey_index = attr_p->pkey_ix;
    opt_mask |= TAVOR_IF_QPEE_OPTPAR_PKEY_INDEX;
  }
  
  if ((attr_mask & QP_ATTR_PORT) ||
      ((is_sqp == TRUE) && (trans == TAVOR_IF_CMD_RST2INIT_QPEE)) )  { 
    /* "The following attributes are not applicable if the QP specified is a Special QP:"... */
    /* (IB-spec. 1.1: Page 512)  - But Tavor requires them for SQ association                */

    /* according to change in tavor_if_defs.h (23.12.2002 - port was seperated from AV). */
    opt_mask |= TAVOR_IF_QPEE_OPTPAR_PORT_NUM;
    // no port changes for special QPs!
    if ( is_sqp == FALSE ) {
      qpc_p->primary_address_path.port_number = attr_p->port;
    } else {
      qpc_p->primary_address_path.port_number = sqp_port;
    }
  }

  if (attr_mask & QP_ATTR_QKEY)
  {
    qpc_p->q_key = attr_p->qkey;
    opt_mask |= TAVOR_IF_QPEE_OPTPAR_Q_KEY;
  }
  if (attr_mask & QP_ATTR_AV)
  {
    udav2qpc_path(&attr_p->av, &qpc_p->primary_address_path);
	opt_mask |= TAVOR_IF_QPEE_OPTPAR_PRIMARY_ADDR_PATH;
  }
  
  // special QPs get msg_max & MTU of UD QPs.
  if (qp_p->st == THH_ST_UD || is_sqp )
  {
    qpc_p->msg_max = TAVOR_LOG2_MAX_MTU;
    qpc_p->mtu = log2mtu_to_ib_mtu(TAVOR_LOG2_MAX_MTU);
  }
  else if (attr_mask & QP_ATTR_PATH_MTU) {
    {    /* See check_constants() that verifies using following shift is fine. */
      if (((1ul << attr_p->path_mtu) & valid_tavor_ibmtu_mask) != 0)
      {  
        qpc_p->mtu = attr_p->path_mtu;
      }
      else
      {
        MTL_ERROR1(MT_FLFMT("Unsupported mtu=%d value"), attr_p->path_mtu);
        rc = HH_EINVAL_PARAM;
      }
    }
  }
  
  if (attr_mask & QP_ATTR_TIMEOUT){
      qpc_p->primary_address_path.ack_timeout = attr_p->timeout;
      /*sqd->rts: this attr is optional , rtr->rts: this attr is mandatory */
      opt_mask |= TAVOR_IF_QPEE_OPTPAR_ACK_TIMEOUT;
  }
   
  if (attr_mask & QP_ATTR_RETRY_COUNT)
  {
	/* according to change in tavor_if_defs.h (23.12.2002 - retry_count was seperated from AV). */
	opt_mask |= TAVOR_IF_QPEE_OPTPAR_RETRY_COUNT;
    qpc_p->retry_count = attr_p->retry_count;
  }
   
  if (attr_mask & QP_ATTR_RNR_RETRY) 
  {
    qpc_p->primary_address_path.rnr_retry   = attr_p->rnr_retry;
    opt_mask |= TAVOR_IF_QPEE_OPTPAR_RNR_RETRY;
	  qpc_p->alternative_address_path.rnr_retry = attr_p->rnr_retry;
    opt_mask |= TAVOR_IF_QPEE_OPTPAR_ALT_RNR_RETRY;
  }
  /*if (attr_mask & QP_ATTR_RQ_PSN)*/
  {
    qpc_p->next_rcv_psn = attr_p->rq_psn;
  }
  
  if (attr_mask & QP_ATTR_REMOTE_ATOMIC_FLAGS)
  {
    VAPI_rdma_atom_acl_t  flags = attr_p->remote_atomic_flags;
    qpc_p->rae = (flags & VAPI_EN_REM_ATOMIC_OP) ? 1 : 0;
    qpc_p->rwe = (flags & VAPI_EN_REM_WRITE) ? 1 : 0;
    qpc_p->rre = (flags & VAPI_EN_REM_READ) ? 1 : 0;

    /* if current outstanding rd-atomic value is 0, disable rdma-read and atomic capability*/
    if ((trans == QPEE_TRANS_RTR2RTS)||(trans==QPEE_TRANS_RTS2RTS)||(trans==QPEE_TRANS_SQERR2RTS)) {
        if (qp_p->qp_ous_rd_atom == 0) {
          MTL_DEBUG3(MT_FLFMT("%s: setting rae/rre to zero, because qp_ous_rd_atom is 0. Trans=%d"), 
                     __func__,trans);
          qpc_p->rae = qpc_p->rre = 0;
        }
    }
    opt_mask |= TAVOR_IF_QPEE_OPTPAR_RRE |
                TAVOR_IF_QPEE_OPTPAR_RAE |
                TAVOR_IF_QPEE_OPTPAR_REW;

  }
  
  if (attr_mask & QP_ATTR_QP_OUS_RD_ATOM)
  {
    if (attr_p->qp_ous_rd_atom != 0)
    {
      qpc_p->rra_max = ceil_log2(attr_p->qp_ous_rd_atom);
      if (qpc_p->rra_max > qpm->log2_max_outs_rdma_atom)
      {
        MTL_ERROR1(MT_FLFMT("Error rra_max=0x%x > QPM's log2_max=0x%x, attr_p->qp_ous_rd_atom = 0x%x"),
                   qpc_p->rra_max, qpm->log2_max_outs_rdma_atom,attr_p->qp_ous_rd_atom);
        rc = HH_EINVAL_PARAM;
      } else {
        if ((trans==QPEE_TRANS_SQD2RTS)&&(qp_p->qp_ous_rd_atom==0)) {
              /* outstanding rd/atomics was previously zero, so need to restore rd/atomic flags */
              MTL_DEBUG3(MT_FLFMT("%s: restoring rae/rre to requested values, because qp_ous_rd_atom changed from 0. Trans=%d"), 
                         __func__,trans);
              qpc_p->rae = (qp_p->remote_atomic_flags & VAPI_EN_REM_ATOMIC_OP) ? 1 : 0;
              qpc_p->rre = (qp_p->remote_atomic_flags & VAPI_EN_REM_READ) ? 1 : 0;
              opt_mask |= TAVOR_IF_QPEE_OPTPAR_RRE | TAVOR_IF_QPEE_OPTPAR_RAE;
          }
      }
    } else {
      qpc_p->rra_max = 0;
      if (qpc_p->rre || qpc_p->rae) 
      {
         MTL_ERROR1(MT_FLFMT("%s: Warning: resetting rre+rae bits for qp_ous_rd_atom=0. Trans=%d"),
                    __func__, trans);
         qpc_p->rre = qpc_p->rae = 0;
         opt_mask |= (TAVOR_IF_QPEE_OPTPAR_RRE | TAVOR_IF_QPEE_OPTPAR_RAE);
      }
    }
    opt_mask |= TAVOR_IF_QPEE_OPTPAR_RRA_MAX;
  }
  

  if (attr_mask & QP_ATTR_OUS_DST_RD_ATOM)
  {
    qpc_p->sra_max = (attr_p->ous_dst_rd_atom == 0) ? 0: floor_log2(attr_p->ous_dst_rd_atom);
    qpc_p->swe = 1;
    if ((attr_p->ous_dst_rd_atom)==0) {
        qpc_p->sre = qpc_p->sae = 0;
    } else {
        if (qpc_p->sra_max > qpm->log2_max_outs_dst_rd_atom)
        {
          MTL_ERROR1(MT_FLFMT("Error sra_max=0x%x > QPM's log2_max=0x%x, attr_p->qp_ous_dst_rd_atom = 0x%x"),
                     qpc_p->sra_max, qpm->log2_max_outs_dst_rd_atom,attr_p->ous_dst_rd_atom);
          rc = HH_EINVAL_PARAM;
        }
        qpc_p->sre = qpc_p->sae = 1;
    }
    opt_mask |= TAVOR_IF_QPEE_OPTPAR_SRA_MAX;
  }
  
  if (attr_mask & QP_ATTR_ALT_PATH)
  {
    udav2qpc_path(&attr_p->alt_av, &qpc_p->alternative_address_path);
    opt_mask |= TAVOR_IF_QPEE_OPTPAR_ALT_ADDR_PATH;
  //}
  //if (attr_mask & QP_ATTR_ALT_TIMEOUT)
  //{
    qpc_p->alternative_address_path.ack_timeout = attr_p->alt_timeout;
    //opt_mask |= TAVOR_IF_QPEE_OPTPAR_ALT_ADDR_PATH;
  //}
  //if (attr_mask & QP_ATTR_ALT_RETRY_COUNT)
  //{
//    qpc_p->alternative_address_path.ack_timeout = attr_p->alt_timeout;
    //opt_mask |= TAVOR_IF_QPEE_OPTPAR_ALT_ADDR_PATH;
  //}
  //if (attr_mask & QP_ATTR_ALT_RNR_RETRY)
  //{
    /* according to change in tavor_if_defs.h (23.12.2002). */
  //	qpc_p->alternative_address_path.rnr_retry = attr_p->alt_rnr_retry;
  //  opt_mask |= TAVOR_IF_QPEE_OPTPAR_ALT_RNR_RETRY;
	
  //}
  //if (attr_mask & QP_ATTR_ALT_PKEY_IX)
  //{
    qpc_p->alternative_address_path.pkey_index = attr_p->alt_pkey_ix;
    //opt_mask |= TAVOR_IF_QPEE_OPTPAR_ALT_ADDR_PATH;
  //}
  //if (attr_mask & QP_ATTR_ALT_PORT)
  //{
    qpc_p->alternative_address_path.port_number = attr_p->alt_port;
    //opt_mask |= TAVOR_IF_QPEE_OPTPAR_ALT_ADDR_PATH;
  }
  if (attr_mask & QP_ATTR_MIN_RNR_TIMER)
  {
    qpc_p->min_rnr_nak = attr_p->min_rnr_timer;
    opt_mask |= TAVOR_IF_QPEE_OPTPAR_RNR_TIMEOUT;
  }
  if (attr_mask & QP_ATTR_SQ_PSN)
  {
    qpc_p->next_send_psn = attr_p->sq_psn;
  }
  
  if (attr_mask & QP_ATTR_PATH_MIG_STATE)
  {
    switch (attr_p->path_mig_state)
    {
      case VAPI_MIGRATED: qpc_p->pm_state = PM_STATE_MIGRATED; break;
      case VAPI_REARM:    qpc_p->pm_state = PM_STATE_REARM;    break;
      case VAPI_ARMED:    qpc_p->pm_state = PM_STATE_ARMED;    break;
      default: rc = HH_EINVAL_PARAM;
    }
    opt_mask |= TAVOR_IF_QPEE_OPTPAR_PM_STATE;
  } else {  /* Default required in order to assure initialization */
    qpc_p->pm_state = PM_STATE_MIGRATED;
  }
  
  if (attr_mask & QP_ATTR_DEST_QP_NUM)
  {
    qpc_p->remote_qpn_een = attr_p->dest_qp_num;
  }
  *opt_mask_p = opt_mask;

done:
  MTL_DEBUG4(MT_FLFMT("vapi2qpc_modify: rc=%d"), rc);
  return rc;
} /* vapi2qpc_modify */

/************************************************************************/
/* Track rdma/atomic parameter changes         
 */
static void  track_rdma_atomic(
  const VAPI_qp_attr_t*  attr_p,
  const u_int32_t        attr_mask,
  TQPM_sw_qpc_t*         qp_p
)
{
  
  if (attr_mask & QP_ATTR_REMOTE_ATOMIC_FLAGS)
  {
    qp_p->remote_atomic_flags = attr_p->remote_atomic_flags;

  }
  
  if (attr_mask & QP_ATTR_QP_OUS_RD_ATOM)
  {
    if (attr_p->qp_ous_rd_atom != 0)
    {
      qp_p->qp_ous_rd_atom = (1<<ceil_log2(attr_p->qp_ous_rd_atom));
    } else{
      qp_p->qp_ous_rd_atom = 0;
    }
  }

  if (attr_mask & QP_ATTR_OUS_DST_RD_ATOM)
  {
    qp_p->ous_dst_rd_atom = attr_p->ous_dst_rd_atom;
  }
  
  return;
} /* vapi2qpc_modify */


/************************************************************************/
static  HH_ret_t  prepare_special_qp(
  THH_qpm_t              qpm,  
  IB_wqpn_t              qpn,
  THH_qpee_transition_t  trans
)
{
  IB_port_t   port = 0; /* regular=0, non-special [1..) */
  HH_ret_t    rc = HH_OK;
  
  MTL_DEBUG4(MT_FLFMT("entry point."));
  
  if (((trans == QPEE_TRANS_INIT2RTR) || (trans == QPEE_TRANS_ERR2RST)) &&
      (is_sqp0(qpm,qpn,&port))) 
  { 
    if (!qpm->sqp_info.configured)
    {
      MTL_DEBUG4(MT_FLFMT("calling conf_special_qps() ."));
      rc = conf_special_qps(qpm);
      if (rc == HH_OK)
      {
        qpm->sqp_info.configured = TRUE;
      }
    }
    if ((rc == HH_OK) && (qpm->sqp_info.port_props != NULL))
    {
      THH_cmd_status_t  cmd_rc = THH_CMD_STAT_OK;
	  
      MTL_DEBUG1(MT_FLFMT("%s: port = %d, qpn = 0x%x"), __func__, port, qpn);
	  if( trans == QPEE_TRANS_INIT2RTR && (qpm->port_active[port-1] == FALSE) ) {
	    cmd_rc = THH_cmd_INIT_IB(qpm->cmd_if, port, 
                               &qpm->sqp_info.port_props[port-1]);
		if( cmd_rc == THH_CMD_STAT_OK )
			qpm->port_active[port-1] = TRUE;
	  }
	  
	  else if( trans == QPEE_TRANS_ERR2RST && (qpm->port_active[port-1] == TRUE) ) {
      cmd_rc = THH_cmd_CLOSE_IB(qpm->cmd_if, port);
		  if ( cmd_rc == THH_CMD_STAT_OK )
			  qpm->port_active[port-1] = FALSE;
	  }

      rc = (CMDRC2HH_ND(cmd_rc));
      MTL_DEBUG4(MT_FLFMT("cmd_rc=%d=%s, trans=%d"), 
                 cmd_rc, str_THH_cmd_status_t(cmd_rc), trans);
    }
  }

  return rc;
} /* prepare_special_qp */


/************************************************************************/
/* Following Tavor-PRM 13.6.x   optparammask possible bits              */
static inline u_int32_t  x_optmask(THH_qpee_transition_t t)
{
  static const u_int32_t  common_mask =
    TAVOR_IF_QPEE_OPTPAR_ALT_ADDR_PATH |
	TAVOR_IF_QPEE_OPTPAR_ALT_RNR_RETRY |
    TAVOR_IF_QPEE_OPTPAR_RRE           |
    TAVOR_IF_QPEE_OPTPAR_RAE           |
    TAVOR_IF_QPEE_OPTPAR_REW           |
    TAVOR_IF_QPEE_OPTPAR_Q_KEY         |
    TAVOR_IF_QPEE_OPTPAR_RNR_TIMEOUT;

  u_int32_t   mask = 0;
  switch (t) /* cases of mask=0, use above defauly and commented out */
  {
    /* case QPEE_TRANS_RST2INIT :  mask=0 */
    case QPEE_TRANS_INIT2INIT:
	  mask = 
		TAVOR_IF_QPEE_OPTPAR_RRE        |
		TAVOR_IF_QPEE_OPTPAR_RAE        |
		TAVOR_IF_QPEE_OPTPAR_REW        |
		TAVOR_IF_QPEE_OPTPAR_Q_KEY      |
		TAVOR_IF_QPEE_OPTPAR_PORT_NUM 	| 
		TAVOR_IF_QPEE_OPTPAR_PKEY_INDEX;
        break;

    case QPEE_TRANS_INIT2RTR :
      mask = common_mask | TAVOR_IF_QPEE_OPTPAR_PKEY_INDEX | TAVOR_IF_QPEE_OPTPAR_SCHED_QUEUE;
      break;

    case QPEE_TRANS_RTR2RTS  :
      mask = common_mask | TAVOR_IF_QPEE_OPTPAR_PM_STATE;
      break;

    case QPEE_TRANS_RTS2RTS  :
      mask = common_mask | TAVOR_IF_QPEE_OPTPAR_PM_STATE;
      break;

    case QPEE_TRANS_SQERR2RTS:
      mask =
        TAVOR_IF_QPEE_OPTPAR_RRE       |
        TAVOR_IF_QPEE_OPTPAR_RAE       |
        TAVOR_IF_QPEE_OPTPAR_REW       |
        TAVOR_IF_QPEE_OPTPAR_Q_KEY     |
        TAVOR_IF_QPEE_OPTPAR_RNR_TIMEOUT;
      break;
    /* case QPEE_TRANS_2ERR     : mask=0 */
    /* case QPEE_TRANS_RTS2SQD  : mask=0 */
    case QPEE_TRANS_SQD2RTS  :
      mask = TAVOR_IF_QPEE_OPTPAR_ALL | TAVOR_IF_QPEE_OPTPAR_SCHED_QUEUE;
      break;
    /* case QPEE_TRANS_ERR2RST  : mask=0 */
    default:;
  }
  return mask;
} /* x_optmask */



/************************************************************************/
static inline void rst2init_dummy_attributes(THH_qpee_context_t*  qpc_p, MT_bool is_sqp, IB_port_t port )
{
    qpc_p->primary_address_path.pkey_index = 0;
    qpc_p->primary_address_path.port_number = is_sqp ? port : 1;
    qpc_p->q_key = 1;
}

static HH_ret_t  modify_qp_checks(
  THH_qpm_t               qpm,           /* IN  */
  TQPM_sw_qpc_t*          qp_p,          /* IN  */
  VAPI_qp_state_t         cur_qp_state,  /* IN  */
  VAPI_qp_attr_t*         qp_attr_p,     /* IN  */
  VAPI_qp_attr_mask_t     attr_mask,     /* IN  */
  THH_qpee_transition_t*  trans_p,       /* OUT */
  VAPI_qp_attr_t*         altfix_attr_p,  /* OUT */
  MT_bool*                trivial_rst2rst /* OUT */
)
{
  HH_ret_t        rc = HH_OK;
  IB_port_t		  port;
  MT_bool	      is_sqp;	
  
  is_sqp = (is_sqp0(qpm,qp_attr_p->qp_num,&port)) | (is_sqp1(qpm,qp_attr_p->qp_num,&port));	
  
  *trivial_rst2rst = FALSE;

  if ( (cur_qp_state        == VAPI_RESET) &&
       (qp_p->state         == VAPI_RESET) &&
       (qp_attr_p->qp_state == VAPI_RESET) )
  {
    rc = HH_OK;
    *trivial_rst2rst = TRUE;
  }           
  
  else if ( ((cur_qp_state != qp_p->state) &&
             (cur_qp_state != VAPI_ERR) &&  /* user may know of error */
             (cur_qp_state != VAPI_SQE)) || /* may know of send-queue error */
             !defined_qp_state(cur_qp_state) ||
             !defined_qp_state(qp_attr_p->qp_state)
          )
  {
    rc = HH_EINVAL_QP_STATE;  
    MTL_ERROR1(MT_FLFMT("mismatch: state, cur_qp_state=%s, qp_p->state=%s."),
               VAPI_qp_state_sym(cur_qp_state), VAPI_qp_state_sym(qp_p->state));
    MTL_ERROR1(MT_FLFMT("mismatch cont.: curr_qp_state=%s,qp_attr_p->qp_state=%s."),
               VAPI_qp_state_sym(cur_qp_state),VAPI_qp_state_sym(qp_attr_p->qp_state));
  }
  else
  {
  /* Support for RESET->ERR transition.  First do RESET->INIT*/
    if ((cur_qp_state == VAPI_RESET) && (qp_attr_p->qp_state == VAPI_ERR) &&
       (qp_p->state == VAPI_RESET)) {
      /* pre transition to init state if requesting 2ERR from RESET state*/
      THH_qpee_context_t  qpc;
      THH_cmd_status_t    rce;
      qpc_default(&qpc);
      init2qpc_using_create_values(qp_p, &qpc);
      rst2init_dummy_attributes(&qpc, is_sqp, port);
      qpc.local_qpn_een = qp_p->qpn;
      rce = THH_cmd_MODIFY_QP(qpm->cmd_if, qp_p->qpn, QPEE_TRANS_RST2INIT, &qpc, 0);
      MTL_DEBUG1(MT_FLFMT("pre 2INIT, rce=%d=%s"),rce,str_THH_cmd_status_t(rce));
      rc = ((rce == THH_CMD_STAT_OK) ? HH_OK : 
            (rce == THH_CMD_STAT_RESOURCE_BUSY) ? HH_EBUSY :
            (rce == THH_CMD_STAT_EINTR) ? HH_EINTR : HH_EFATAL );
      cur_qp_state = VAPI_INIT; /* we just did move to */

      /* QP with SRQ modified to reset - must first modify to ERR to flush all WQEs */
    } else if ((qp_attr_p->qp_state == VAPI_RESET) &&  (qp_p->state != VAPI_ERR) &&
               (qp_p->srqn != HH_INVAL_SRQ_HNDL)) {
      THH_cmd_status_t    rce;
      MTL_DEBUG4(
        MT_FLFMT("%s: Moving QP 0x%X to error state before moving to reset (uses SRQ 0x%X)"),
        __func__, qp_p->qpn, qp_p->srqn);
      rce = THH_cmd_MODIFY_QP(qpm->cmd_if, qp_p->qpn, QPEE_TRANS_2ERR, 0, 0);
      rc = ((rce == THH_CMD_STAT_OK) ? HH_OK : 
            (rce == THH_CMD_STAT_RESOURCE_BUSY) ? HH_EBUSY :
            (rce == THH_CMD_STAT_EINTR) ? HH_EINTR : HH_EFATAL );
      if (rc == HH_OK) {
        cur_qp_state = VAPI_ERR; /* we just did move to */
        qp_p->state= VAPI_ERR;
      }
    }
  }
  if (rc == HH_OK)
  {
    *trans_p = state_machine.tab[cur_qp_state][qp_attr_p->qp_state];
    /* if qp_attr_p->en_sqd_asyn_notif was set, we add a flag to xition value passed
	   to THH_cmd_MODIFY_QPEE(). no need to check (qp_attr_p->qp_state == VAPI_SQD) - 
	   te flag is masked off anyway upon entry of THH_cmd_MODIFY_QPEE()*/
	if( qp_attr_p->en_sqd_asyn_notif && (*trans_p == QPEE_TRANS_RTS2SQD) ) {
	  *trans_p = QPEE_TRANS_RTS2SQD_WITH_EVENT;
	}
	MTL_DEBUG4(MT_FLFMT("cur=%s, next=%s, trans=%d"),
               VAPI_qp_state_sym(cur_qp_state), 
               VAPI_qp_state_sym(qp_attr_p->qp_state), *trans_p);
    if ( (*trans_p == (THH_qpee_transition_t)(-1)) && (trivial_rst2rst == FALSE) )
    {
      rc = HH_EINVAL_QP_STATE;    MTL_DEBUG4(MT_FLFMT("bad trans"));
    }
    /*
	// since all alt_av related fields were combined under QP_ATTR_ALT_PATH
	// there is no need to check for partial delivery of them.
	else
    {
       if (!fix_partial_alternate(attr_mask, qpm->cmd_if, qpn, qp_attr_p,
                                  altfix_attr_p, &qp_attr_p))
       {
         rc = HH_EINVAL_PARAM;
       }
    }
	*/
  }
  MTL_DEBUG4(MT_FLFMT("rc=%d=%s, trans=%d"), rc, HH_strerror_sym(rc), *trans_p);
  return rc;
} /* modify_qp_checks */



/************************************************************************/
/************************************************************************/
/*                         interface functions                          */


/************************************************************************/
HH_ret_t  THH_qpm_create(
  THH_hob_t              hob,          /* IN  */
  const THH_qpm_init_t*  init_attr_p,  /* IN  */
  THH_qpm_t*             qpm_p         /* OUT */
)
{
  HH_ret_t       rc = HH_EAGAIN;
  VIP_common_ret_t vret;
  TQPM_t*        qpm;
  u_int8_t       log2_max_qp = init_attr_p->log2_max_qp;
  u_int8_t       log2_max_outs = init_attr_p->log2_max_outs_rdma_atom;
  u_int32_t      rdb_base_align_mask = (1ul << log2_max_outs) - 1,i;
  unsigned long  tavor_num_reserved_qps = 1ul << init_attr_p->log2_rsvd_qps;
  unsigned long  nqp = 1ul << log2_max_qp;
  unsigned long  nsqp= NUM_SQP_PER_PORT * init_attr_p->n_ports; /* Number of special QPs */
  unsigned long  nrqp= nqp - tavor_num_reserved_qps - nsqp;     /* Number of regular QPs */
  
  *qpm_p = NULL; // needed to know if to free mutex. will be non-NULL only if everything OK
  if ((!constants_ok) || (log2_max_qp > 24) || 
      (nqp <= tavor_num_reserved_qps) || (init_attr_p->rdb_base_index & rdb_base_align_mask) ) {
    MTL_ERROR1(MT_FLFMT("%s: Invalid initialization parameters for THH_qpm"),__func__);
    return HH_EINVAL;
  }

  qpm = TMALLOC(TQPM_t);
  if (qpm == NULL) {
    MTL_ERROR1(MT_FLFMT("%s: Failed allocation of THH_qpm object"),__func__);
    return HH_EAGAIN;
  }
  memset(qpm, 0, sizeof(TQPM_t));

  qpm->qpn_prefix= (u_int8_t *)MALLOC(MAX_QPN_PREFIX);
  if (qpm->qpn_prefix == NULL) {
    MTL_ERROR1(MT_FLFMT("%s: Failed allocation of qpn_prefix table (%u entries)"),__func__,
               MAX_QPN_PREFIX);
    goto failed_qpn_prefix;
  }
  memset(qpm->qpn_prefix,0,MAX_QPN_PREFIX);

  vret= VIP_array_create_maxsize(nrqp > 1024 ? 1024 : nrqp, nrqp, &qpm->qp_tbl);
  if (vret != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("%s: Failed VIP_array_create(1024) (vret=%d)"),__func__,vret);
    rc= HH_EAGAIN;
    goto failed_qp_tbl;
  }

  MTL_DEBUG4("{THH_qpm_create: hob=%p, log2MaxQP=%d, qpm=%p, rsvd_qps=%lu, " 
             "ra_idx=0x%x, log2_max_outs=%d\n", 
             hob, log2_max_qp, qpm,  tavor_num_reserved_qps,
             init_attr_p->rdb_base_index, init_attr_p->log2_max_outs_rdma_atom);

  MTL_DEBUG4("{THH_qpm_create: constants_ok=%d, rdb_base_index=0x%x, align_mask=0x%x\n",
		 constants_ok, init_attr_p->rdb_base_index, rdb_base_align_mask); 
  
  if ((THH_hob_get_cmd_if(hob, &qpm->cmd_if) != HH_OK) ||
      (THH_hob_get_mrwm(hob, &qpm->mrwm_internal) != HH_OK) ||
      (THH_hob_get_ddrmm(hob, &qpm->ddrmm) != HH_OK) ||
      (THH_hob_get_uldm(hob, &qpm->uldm) != HH_OK))
  {
    MTL_ERROR1(MT_FLFMT("%s: Failed getting internal HOB objects"),__func__);
    rc= HH_ERR;
    goto failed_obj_get;
  }
  rc = HH_OK;
  
  qpm->hob                     = hob;
  qpm->log2_max_qp             = log2_max_qp;
  
  /* speacial QPs info */
  qpm->sqp_info.sqp_ctx= TNMALLOC(TQPM_sw_qpc_t*, nsqp);
  if (qpm->sqp_info.sqp_ctx == NULL) {
    MTL_ERROR1(MT_FLFMT("%s: Failed allocating sqp_ctx"),__func__);
    goto failed_sqp_ctx;
  }
  memset(qpm->sqp_info.sqp_ctx, 0, nsqp * sizeof(TQPM_sw_qpc_t*));
  qpm->sqp_info.first_sqp_qpn  = tavor_num_reserved_qps;
  qpm->sqp_info.configured = FALSE; /* configure on demand */
  qpm->sqp_info.n_ports= init_attr_p->n_ports;
  if (!copy_port_props(qpm, init_attr_p))
  {
    goto failed_port_props;
  }

  
  qpm->first_rqp= qpm->sqp_info.first_sqp_qpn + nsqp; /* Index of first QP in qp_tbl */
  qpm->rdb_base_index          = init_attr_p->rdb_base_index;
  qpm->log2_max_outs_rdma_atom = log2_max_outs;
  qpm->log2_max_outs_dst_rd_atom = init_attr_p->log2_max_outs_dst_rd_atom;
  qpm->max_outs_rdma_atom      = (1ul << log2_max_outs);
  qpm->idx_mask                = (1ul << log2_max_qp) - 1;
    
	if (qpm->sqp_info.port_props)  {/* used as flag for non legacy behavior */
    for(i = 0;i < init_attr_p->n_ports;i++) {
      qpm->port_active[i] = FALSE;
    }
#if !defined(DELAY_CONF_SPECIAL_QPS)
      rc = conf_special_qps(qpm);
      if (rc != HH_OK)  goto failed_conf_sqp;
#endif
  }
  MOSAL_mutex_init(&qpm->mtx);
    
  init_sgid_tbl(qpm);
  init_pkey_tbl(qpm);

  MTL_TRACE1("}THH_qpm_create: qpm=%p\n", qpm);
  logIfErr("THH_qpm_create");
  *qpm_p = qpm;
  return  HH_OK;

  failed_conf_sqp:
    if (qpm->sqp_info.port_props != NULL)  FREE(qpm->sqp_info.port_props);
  failed_port_props:
    FREE(qpm->sqp_info.sqp_ctx);
  failed_sqp_ctx:
  failed_obj_get:
    VIP_array_destroy(qpm->qp_tbl,NULL);
  failed_qp_tbl:
    FREE(qpm->qpn_prefix);
  failed_qpn_prefix:
    FREE(qpm);
    return rc;
} /* THH_qpm_create */

static void TQPM_free_sw_qpc(void *sw_qpc)
{
  TQPM_sw_qpc_t* qp_p= (TQPM_sw_qpc_t*)sw_qpc;
  if (qp_p == NULL) {
    MTL_ERROR1(MT_FLFMT("%s: Invoked for NULL SW QP context"), __func__);
  } else {
    MTL_ERROR1(MT_FLFMT("%s: Cleaning QP left-overs (qpn=0x%X)"), __func__, qp_p->qpn);
    FREE(sw_qpc);
  }
}

/************************************************************************/
HH_ret_t  THH_qpm_destroy(THH_qpm_t qpm /* IN */,  MT_bool hca_failure /* IN */)
{
  int i;
  VIP_common_ret_t vret=VIP_OK;
  u_int32_t nsqp= qpm->first_rqp - qpm->sqp_info.first_sqp_qpn; /* Number of special QPs */

  MTL_TRACE1("{THH_qpm_destroy: qpm=%p, hfail=%d\n", qpm, hca_failure);
  /* Clean regular QPs "left-overs" */
  MTL_TRACE2(MT_FLFMT("%s: Cleaning VIP_array..."), __func__);
  vret= VIP_array_destroy(qpm->qp_tbl, TQPM_free_sw_qpc);
  if (vret != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("%s: Failed VIP_array_destroy for qp_tbl (%d - %s)"), __func__,
               vret, VAPI_strerror_sym(vret));
    /* Continue - the show must go on... */
  }

  /* Cleaning special QPs left-overs */
  MTL_TRACE2(MT_FLFMT("%s: Cleaning special QPs..."), __func__);
  for (i= 0; i < (int)nsqp; i++) {
    if (qpm->sqp_info.sqp_ctx[i] != NULL)  {FREE(qpm->sqp_info.sqp_ctx[i]);}
  }
  FREE(qpm->sqp_info.sqp_ctx);
  if (qpm->sqp_info.port_props != NULL) {
    MTL_TRACE2(MT_FLFMT("%s: Cleaning port_props..."), __func__);
    FREE(qpm->sqp_info.port_props);
  }
  
/* free pkey & sgid tbl */  
  MTL_TRACE2(MT_FLFMT("%s: Cleaning SGID table..."), __func__);
  for (i=0; i< qpm->sqp_info.n_ports; i++)
  {
    if (qpm->sgid_tbl[i] != NULL)
    {
        TQPM_GOOD_FREE(qpm->sgid_tbl[i],(sizeof(IB_gid_t) * qpm->num_sgids[i]));
    }
  }

  MTL_TRACE2(MT_FLFMT("%s: Cleaning Pkey table..."), __func__);
  for (i=0; i< qpm->sqp_info.n_ports; i++)
  {
    if (qpm->pkey_tbl[i] != NULL)
    {
        TQPM_GOOD_FREE(qpm->pkey_tbl[i],(sizeof(VAPI_pkey_t)*qpm->pkey_tbl_sz[i]));
    }
  }

  MTL_TRACE2(MT_FLFMT("%s: Cleaning qpn_prefix..."), __func__);
  FREE(qpm->qpn_prefix);
  MOSAL_mutex_free(&qpm->mtx);
  FREE(qpm);
  MTL_TRACE1("}THH_qpm_destroy\n");
  return  HH_OK;
} /* THH_qpm_destroy */


/************************************************************************/
HH_ret_t  THH_qpm_create_qp(
  THH_qpm_t               qpm,               /* IN  */
  HH_qp_init_attr_t*      init_attr_p,       /* IN  */
  MT_bool                 mlx,               /* IN  */
  THH_qp_ul_resources_t*  qp_ul_resources_p, /* IO  */
  IB_wqpn_t*              qpn_p              /* OUT */
)
{
  HH_ret_t     rc = HH_EAGAIN;
  VIP_common_ret_t vret;
  u_int32_t    qp_idx;
  TQPM_sw_qpc_t *new_qp_p;
  VIP_array_handle_t qp_hndl;
  u_int32_t  wild_bits;

  MTL_TRACE1("{THH_qpm_create_qp: qpm=%p, mlx=%d\n", qpm, mlx);
  if ((init_attr_p->srq != HH_INVAL_SRQ_HNDL) && (init_attr_p->ts_type != VAPI_TS_RC)) {
    /* SRQs are supported only for RC QPs in Tavor */
    MTL_ERROR2(MT_FLFMT("%s: SRQ association with transport service type %s(%d)"
                        " - only RC QPs are allowed with SRQs."),
               __func__, VAPI_ts_type_sym(init_attr_p->ts_type), init_attr_p->ts_type);
    return HH_ENOSYS;
  }

  new_qp_p= TMALLOC(TQPM_sw_qpc_t);
  if (new_qp_p == NULL) {
    MTL_ERROR1(MT_FLFMT("%s: Failed allocating memory for new SW-QPC"),__func__);
    return HH_EAGAIN;
  }
  memset(new_qp_p,0,sizeof(TQPM_sw_qpc_t));
  vret= VIP_array_insert(qpm->qp_tbl, new_qp_p, &qp_hndl ); 
  if (vret != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("%s: Failed allocating QP (%d - %s), qpm->qp_tbl=%p"),__func__, 
               vret, VAPI_strerror_sym(vret), qpm->qp_tbl);
    rc= (HH_ret_t)vret;
    goto failed_array_insert;
  }
  qp_idx= qp_hndl + qpm->first_rqp;
  if (qp_idx >= (1U<<qpm->log2_max_qp)) {
      MTL_ERROR1(MT_FLFMT("%s: QP index (0x%x) greater than  (1<<log2_max_qp)-1 (0x%x)"),__func__, 
                 qp_idx, (1<<qpm->log2_max_qp)-1);
  }
  rc = create_qp(qpm, init_attr_p, mlx, qp_ul_resources_p, new_qp_p);
  if (rc != HH_OK)  goto failed_create_qp;
  
  /* perturb high bits */
  wild_bits = qpm->qpn_prefix[qp_idx & QPN_PREFIX_INDEX_MASK]++;
  new_qp_p->qpn = ( (wild_bits << qpm->log2_max_qp) | qp_idx ) & 0xFFFFFF;
  if (new_qp_p->qpn == 0xFFFFFF)  new_qp_p->qpn= qp_idx; /* 0xFFFFFF is reserved for multicast */
  
  *qpn_p = new_qp_p->qpn;
  MTL_TRACE1("}THH_qpm_create_qp: qpn=0x%x\n", *qpn_p);
  logIfErr("THH_qpm_create_qp");
  return  rc;

  failed_create_qp:
    VIP_array_erase(qpm->qp_tbl, qp_hndl, NULL);
  failed_array_insert:
    FREE(new_qp_p);
    return rc;
} /* THH_qpm_create_qp */


/************************************************************************/
HH_ret_t  THH_qpm_get_special_qp(
 THH_qpm_t               qpm,                /* IN  */
 VAPI_special_qp_t       qp_type,            /* IN  */
 IB_port_t               port,               /* IN  */
 HH_qp_init_attr_t*      init_attr_p,        /* IN  */
 THH_qp_ul_resources_t*  qp_ul_resources_p,  /* IO  */
 IB_wqpn_t*              sqp_hndl_p          /* OUT */
)
{
  const Special_QPs*  sqp_info = &qpm->sqp_info;
  HH_ret_t            rc = HH_OK;
  unsigned int        port_idx = port - 1;
  unsigned int        qpti = 0; /* SQP Type index */
  MTL_TRACE1("{THH_qpm_get_special_qp: qpm=%p\n", qpm);
  if (qpm->sqp_info.port_props == NULL)
  {
    MTL_ERROR1(MT_FLFMT("get_special_qp: not supported in legacy mode"));
    rc = HH_ENOSYS;
  }
  else
  {
    if (port_idx >= sqp_info->n_ports)
    {
      MTL_ERROR1(MT_FLFMT("THH_qpm_get_special_qp: bad port=%d"), port);
      rc = HH_EINVAL_PORT;
    }
    for (qpti = 0; (qpti != n_qp_types) && (qp_types[qpti] != qp_type);  ++qpti);
    if (qpti == NUM_SQP_PER_PORT)
    {
      MTL_ERROR1(MT_FLFMT("THH_qpm_get_special_qp: bad qp_type=%d"), qp_type);
      rc = HH_EINVAL_PARAM;
    }
    

#if defined(DELAY_CONF_SPECIAL_QPS)
    if ((rc == HH_OK) && (!qpm->sqp_info.configured))
    {
      rc = conf_special_qps(qpm);
      if (rc == HH_OK)
      {
        qpm->sqp_info.configured = TRUE;
      }
    }
#endif
  }
  if (rc == HH_OK)
  {
    u_int32_t  sqp_indx= (qpm->sqp_info.n_ports * qpti) + port_idx;
    if (MOSAL_mutex_acq(&qpm->mtx, TRUE) != MT_OK)  return HH_EINTR;

      if (qpm->sqp_info.sqp_ctx[sqp_indx] == NULL) { /* This SQP is not used */
        // making sure of MLX xport service for special QPs:
        qpm->sqp_info.sqp_ctx[sqp_indx]= TMALLOC(TQPM_sw_qpc_t);
        if (qpm->sqp_info.sqp_ctx[sqp_indx] == NULL) {
          MTL_ERROR1(MT_FLFMT("%s: Failed allocating memory for new SW-QPC"),__func__);
          rc= HH_EAGAIN;
        } else {
          init_attr_p->ts_type = THH_ST_MLX; 
          rc = create_qp(qpm, init_attr_p, TRUE, qp_ul_resources_p, qpm->sqp_info.sqp_ctx[sqp_indx]);
          if (rc != HH_OK) {
            FREE(qpm->sqp_info.sqp_ctx[sqp_indx]);
            qpm->sqp_info.sqp_ctx[sqp_indx]= NULL;
          } else {
            qpm->sqp_info.sqp_ctx[sqp_indx]->qpn= qpm->sqp_info.first_sqp_qpn + sqp_indx;
            MTL_DEBUG4(MT_FLFMT(
              "%s: Allocated SQP of type %d (port %d) with qpn=0x%X "
              "(qpti=%u sqp_indx=%u first_sqp_qpn=0x%X)"), __func__,
                       qp_type, port, qpm->sqp_info.sqp_ctx[sqp_indx]->qpn, 
                       qpti,sqp_indx,qpm->sqp_info.first_sqp_qpn);
            *sqp_hndl_p = qpm->sqp_info.sqp_ctx[sqp_indx]->qpn;
          }
        }
      }
      else
      {
        rc = HH_EBUSY;
      }
      MOSAL_mutex_rel(&qpm->mtx);
    
  }
  MTL_TRACE1("}THH_qpm_get_special_qp\n");
  logIfErr("THH_qpm_get_special_qp");
  return rc;
} /* THH_qpm_get_special_qp */

static inline HH_ret_t THH_modify_cmdrc2rc(THH_cmd_status_t  cmd_rc)
{
    HH_ret_t rc;
    switch(cmd_rc){
      case THH_CMD_STAT_OK:
          rc = HH_OK;
          break;
      case THH_CMD_STAT_EINTR:
        rc = HH_EINTR;
        break;
      case THH_CMD_STAT_BAD_PARAM:
      case THH_CMD_STAT_BAD_INDEX:
          rc = HH_EINVAL_PARAM;
          break;
      case THH_CMD_STAT_BAD_RESOURCE:  /* accessing reserved qp/ee */
      case THH_CMD_STAT_RESOURCE_BUSY:
          rc = HH_EBUSY;
          break;
      case THH_CMD_STAT_BAD_QPEE_STATE:
          rc = HH_EINVAL_QP_STATE;
          break;
      case THH_CMD_STAT_BAD_RES_STATE:
          rc = HH_EINVAL_MIG_STATE;
          break;
      case THH_CMD_STAT_BAD_SYS_STATE:
          rc = HH_ERR;  /* HCA is disabled */
          break;
      default:
          rc = HH_EFATAL;
    }
    return rc;
}
/************************************************************************/
/* We protect against erroneous application modifying same QP
 * in multi-threads. We use a mutex per QPM.
 * It may be more efficient to have a mutex per QP,
 * but we leave it for future consideration.
 */
HH_ret_t  THH_qpm_modify_qp(
  THH_qpm_t             qpm,             /* IN  */
  IB_wqpn_t             qpn,             /* IN  */
  VAPI_qp_state_t       cur_qp_state,    /* IN  */
  VAPI_qp_attr_t*       qp_attr_p,       /* IN  */
  VAPI_qp_attr_mask_t*  qp_attr_mask_p   /* IN  */
)
{
  VAPI_qp_attr_t         altfix_attr;
  THH_qpee_transition_t  trans;
  VIP_array_obj_t        qp_obj;
  TQPM_sw_qpc_t*         qp_p;
  HH_ret_t               rc = HH_EAGAIN;
  VIP_common_ret_t       vret;
  int i;
  u_int32_t              qp_idx = qpn & qpm->idx_mask;
  MT_bool  trivial_rst2rst;

  MTL_DEBUG1("{THH_qpm_modify_qp: qpm=%p, qpn=0x%x, curr_state=%d\n, next_state=%d", 
             qpm, qpn, cur_qp_state,qp_attr_p->qp_state);
  if (is_sqp(qpm,qpn)) {
    if (MOSAL_mutex_acq(&qpm->mtx, TRUE) != MT_OK)  return HH_EINTR;
    qp_p= qpm->sqp_info.sqp_ctx[qpn - qpm->sqp_info.first_sqp_qpn];
    if (qp_p == NULL) {
      MTL_ERROR1(MT_FLFMT("%s: Given special QP handle is not active (qpn=0x%X)"),__func__,qpn);
      MOSAL_mutex_rel(&qpm->mtx);
      return HH_EINVAL_QP_NUM;
    }
  } else { /* regular RQ */
    vret= VIP_array_find_hold(qpm->qp_tbl, qp_idx - qpm->first_rqp, &qp_obj);
    qp_p= (TQPM_sw_qpc_t*)qp_obj;
    if ((vret != VIP_OK) || (qpn != qp_p->qpn)) {
      MTL_ERROR1(MT_FLFMT("%s: Invalid QP handle (qpn=0x%X)"),__func__,qpn);
      if (vret == VIP_OK)  VIP_array_find_release(qpm->qp_tbl, qp_idx - qpm->first_rqp);
      return HH_EINVAL_QP_NUM;
    }
  }
    
	rc = modify_qp_checks(qpm, qp_p, cur_qp_state, qp_attr_p, *qp_attr_mask_p, 
                        &trans, &altfix_attr,&trivial_rst2rst);
    MTL_DEBUG4(MT_FLFMT("trans=%d, rst2rst=%d"), trans, trivial_rst2rst);
    if (rc == HH_OK && !trivial_rst2rst)
    {
      THH_qpee_context_t  qpc;
      u_int32_t           opt_mask;
      u_int32_t           qp_idx = qpn & qpm->idx_mask;
	    MT_bool			        legacy_mode;

      qpc_default(&qpc);
      qpc.state = qp_attr_p->qp_state;
      if ((qp_attr_p->qp_state == VAPI_INIT) && (cur_qp_state != VAPI_INIT))
      {
        init2qpc_using_create_values(qp_p, &qpc);
      }
      
	  // just making sure qpn is correct.
	  qp_attr_p->qp_num = qpn;
	  rc = vapi2qpc_modify(qpm, qp_p, qp_attr_p, trans, *qp_attr_mask_p, 
                         &qpc, &opt_mask);
      
	  qpc.local_qpn_een = qpn;
    qpc.ra_buff_indx = qpm->rdb_base_index + qp_idx * qpm->max_outs_rdma_atom;
	  //MTL_ERROR1("%s: opt mask before screening was: 0x%x", __func__,opt_mask);
	  opt_mask &= x_optmask(trans);
	  //MTL_ERROR1("%s: opt mask after screening was: 0x%x", __func__,opt_mask);
      
	  /*
	  prepare_special_qp() will call INIT_IB/CLOSE_IB 
	  for special QPs & their associated port.
	  this should be done only when operating in non legacy
	  mode (legacy mode has executed INIT_IB from THH_hob_open_hca() ).
	  */
	  if( (rc == HH_OK) && (is_sqp(qpm,qpn)) )  {
		  rc = THH_hob_get_legacy_mode(qpm->hob,&legacy_mode);
      if( rc == HH_OK && (legacy_mode == FALSE) ) {
		    MTL_TRACE2("%s: operating under non legacy mode - activating port.", __func__);
		    rc = prepare_special_qp(qpm, qpn, trans);
		  }
    }
      
	  if (rc == HH_OK)
      {
        THH_cmd_status_t  cmd_rc = 
          THH_cmd_MODIFY_QP(qpm->cmd_if, qpn, trans, &qpc, opt_mask);

        rc = THH_modify_cmdrc2rc(cmd_rc);
        MTL_DEBUG4(MT_FLFMT("cmd_rc=%d=%s"), 
                   cmd_rc, str_THH_cmd_status_t(cmd_rc));
        if (rc == HH_OK)
        {
           IB_port_t sqp1_port;
          /* check whether to update pkey index of qp1 in our struct*/  
          if (is_sqp1(qpm,qpn,&sqp1_port))
          {
                if (check_2update_pkey(cur_qp_state,qp_attr_p->qp_state,qp_attr_mask_p))
                {
                  MTL_DEBUG4("updating pkey in the required transition. port %d \n",sqp1_port);
                  qpm->qp1_pkey_idx[sqp1_port-1/*idx!*/] = qp_attr_p->pkey_ix;
                  for (i=0; i< qpm->sqp_info.n_ports; i++)
                  {
                    MTL_DEBUG1("port %d: qp1 pkey idx:%x \n",i+1,qpm->qp1_pkey_idx[i]);
                  }
                }
          }

          qp_p->state = qp_attr_p->qp_state;
          track_rdma_atomic(qp_attr_p,*qp_attr_mask_p, qp_p); 
        }
      }
    }
    
  if (is_sqp(qpm,qpn)) {
    MOSAL_mutex_rel(&qpm->mtx);
  } else { /* regular RQ */
    VIP_array_find_release(qpm->qp_tbl, qp_idx - qpm->first_rqp);
  }
  
  MTL_TRACE1("}THH_qpm_modify_qp\n");
  logIfErr("THH_qpm_modify_qp");
  return rc;
} /* THH_qpm_modify_qp */


/************************************************************************/
/* Same comment about mutex as above THH_qpm_modify_qp(...) applies     */
HH_ret_t  THH_qpm_query_qp(
  THH_qpm_t        qpm,       /* IN  */
  IB_wqpn_t        qpn,       /* IN  */
  VAPI_qp_attr_t*  qp_attr_p  /* IN  */
)
{
  HH_ret_t  rc = HH_OK;
  VIP_common_ret_t vret;
  IB_port_t   dummy_port;
  VIP_array_obj_t        qp_obj;
  TQPM_sw_qpc_t*         qp_p;
  u_int32_t              qp_idx = qpn & qpm->idx_mask;

  MTL_TRACE1("{THH_qpm_query_qp: qpm=%p, qpn=0x%x\n", qpm, qpn);
  if (is_sqp(qpm,qpn)) {
    if (MOSAL_mutex_acq(&qpm->mtx, TRUE) != MT_OK)  return HH_EINTR;
    qp_p= qpm->sqp_info.sqp_ctx[qpn - qpm->sqp_info.first_sqp_qpn];
    if (qp_p == NULL) {
      MTL_ERROR1(MT_FLFMT("%s: Given special QP handle is not active (qpn=0x%X)"),__func__,qpn);
      return HH_EINVAL_QP_NUM;
    }
  } else { /* regular RQ */
    vret= VIP_array_find_hold(qpm->qp_tbl, qp_idx - qpm->first_rqp, &qp_obj);
    qp_p= (TQPM_sw_qpc_t*)qp_obj;
    if ((vret != VIP_OK) || (qpn != qp_p->qpn)) {
      MTL_ERROR1(MT_FLFMT("%s: Invalid QP handle (qpn=0x%X)"),__func__,qpn);
      if (vret == VIP_OK)  VIP_array_find_release(qpm->qp_tbl, qp_idx - qpm->first_rqp);
      return HH_EINVAL_QP_NUM;
    }
  }
    
  memset(qp_attr_p, 0, sizeof(VAPI_qp_attr_t));

  switch (qp_p->state)
  {
    case VAPI_RESET:
      qp_attr_p->qp_state = VAPI_RESET;
      qp_attr_p->qp_num = qpn;
      if (is_sqp0(qpm,qp_attr_p->qp_num,&dummy_port)) {
           qp_attr_p->qp_num = 0;
      } else if (is_sqp1(qpm,qp_attr_p->qp_num,&dummy_port)) {
           qp_attr_p->qp_num = 1;
      }
      break;
    default:
      {
        THH_qpee_context_t  qpc;
        THH_cmd_status_t    crc = THH_cmd_QUERY_QP(qpm->cmd_if, qpn, &qpc);
        if (crc == THH_CMD_STAT_OK)
        {
          qpc2vapi_attr(&qpc, qp_attr_p);
          if (is_sqp0(qpm,qp_attr_p->qp_num,&dummy_port)) {
               qp_attr_p->qp_num = 0;
          } else if (is_sqp1(qpm,qp_attr_p->qp_num,&dummy_port)) {
               qp_attr_p->qp_num = 1;
          }
        } else {
          rc =  ((crc == THH_CMD_STAT_OK) ? HH_OK : 
                 (crc == THH_CMD_STAT_EINTR) ? HH_EINTR : 
                 (crc == THH_CMD_STAT_RESOURCE_BUSY) ? HH_EBUSY : HH_EFATAL);
          MTL_ERROR1(MT_FLFMT("ERROR: THH_cmd_QUERY_QP returned %s"),str_THH_cmd_status_t(crc));
        }
      }
  }
  if (rc == HH_OK)
  {
    qp_attr_p->cap = qp_p->cap; 
    qp_attr_p->ous_dst_rd_atom = qp_p->ous_dst_rd_atom;
    qp_attr_p->qp_ous_rd_atom = qp_p->qp_ous_rd_atom;
    qp_attr_p->remote_atomic_flags = qp_p->remote_atomic_flags;
  }


  if (is_sqp(qpm,qpn)) {
    MOSAL_mutex_rel(&qpm->mtx);
  } else { /* regular RQ */
    VIP_array_find_release(qpm->qp_tbl, qp_idx - qpm->first_rqp);
  }
  MTL_TRACE1("}THH_qpm_query_qp, state=%d=%s\n", 
             qp_attr_p->qp_state, VAPI_qp_state_sym(qp_attr_p->qp_state));
  logIfErr("THH_qpm_query_qp");
  return rc;
} /* THH_qpm_query_qp */


/************************************************************************/
/* Same comment about mutex as above THH_qpm_modify_qp(...) applies     */
HH_ret_t  THH_qpm_destroy_qp(
  THH_qpm_t     qpm,  /* IN */
  IB_wqpn_t     qpn   /* IN */
)
{
  HH_ret_t        rc = HH_OK;
  VIP_common_ret_t vret;
  u_int32_t       qp_idx = qpn & qpm->idx_mask;
  TQPM_sw_qpc_t*  qp2destroy;
  VIP_array_obj_t array_obj;
  
  MTL_TRACE1("{THH_qpm_destroy_qp: qpm=%p, qpn=0x%x\n", qpm, qpn);

  if (is_sqp(qpm,qpn)) {
    if (MOSAL_mutex_acq(&qpm->mtx, TRUE) != MT_OK)  return MT_EINTR;
    qp2destroy= qpm->sqp_info.sqp_ctx[qp_idx - qpm->sqp_info.first_sqp_qpn];
    if (qp2destroy == NULL) {
      MTL_ERROR1(MT_FLFMT("%s: Given special QP handle is not active (qpn=0x%X)"),__func__,qpn);
      MOSAL_mutex_rel(&qpm->mtx);
      return HH_EINVAL_QP_NUM;
    }
  } else {
    vret= VIP_array_erase_prepare(qpm->qp_tbl, qp_idx - qpm->first_rqp, &array_obj);
    qp2destroy= (TQPM_sw_qpc_t*)array_obj;
    if (vret != VIP_OK) {
      MTL_ERROR1(MT_FLFMT("%s: Failed VIP_array_erase_prepare for qpn=0x%X (%d - %s)"), __func__, 
                 qpn, vret, VAPI_strerror_sym(vret));
      return (vret == VIP_EINVAL_HNDL) ? HH_EINVAL_QP_NUM : (HH_ret_t)vret;
    }
    if (qpn != qp2destroy->qpn) {
      MTL_ERROR1(MT_FLFMT("%s: Invalid qpn=0x%X"), __func__, qpn);
      VIP_array_erase_undo(qpm->qp_tbl, qp_idx - qpm->first_rqp);
      return HH_EINVAL_QP_NUM;
    }
  }

#if defined(MT_SUSPEND_QP)
  /* if qp is suspended, unsuspend it here, directly calling command interface */
  {  
      THH_cmd_status_t    crc;
      if (qp2destroy->is_suspended == TRUE) {
          crc = THH_cmd_SUSPEND_QP(qpm->cmd_if, qpn, FALSE);
          if (crc != THH_CMD_STAT_OK){ 
              MTL_ERROR1(MT_FLFMT("%s: FAILED unsuspending QP 0x%x. "),__func__, qpn);
          }
      }
      qp2destroy->is_suspended=FALSE;
  }
#endif

  if (qp2destroy->state != VAPI_RESET) 
  { /* Assure QP is left in RESET (SW ownership) */
    THH_qpee_context_t  qpc;
    THH_cmd_status_t    rce; 
    qpc_default(&qpc);
    qpc.local_qpn_een = qpn;

    /* really ANY2RST transition, not ERR2RST */
    rce = THH_cmd_MODIFY_QP(qpm->cmd_if, qpn, QPEE_TRANS_ERR2RST, &qpc, 0);
    MTL_DEBUG4(MT_FLFMT("2RST: rc=%d=%s"), rce, str_THH_cmd_status_t(rce));
    rc = (((rce == THH_CMD_STAT_OK)||(rce == THH_CMD_STAT_EFATAL)) ? HH_OK : 
          (rce == THH_CMD_STAT_EINTR) ? HH_EINTR :
          (rce == THH_CMD_STAT_RESOURCE_BUSY) ? HH_EBUSY : HH_EINVAL);
  }

  if ((rc == HH_OK) && (qp2destroy->wqes_buf_sz != 0)) 
  { /* Release descriptor's memory region (if WQEs buffer exists - could happen with SRQ)*/
    rc= THH_mrwm_deregister_mr(qpm->mrwm_internal, qp2destroy->lkey); 
    if (rc != HH_OK) {
      MTL_ERROR2(MT_FLFMT("%s: Failed deregistering internal MR (qpn=0x%X lkey=0x%X)"
                          " ==> MR resource leak (%s)"), 
                 __func__, qp2destroy->qpn, qp2destroy->lkey, HH_strerror_sym(rc));
    }
    if (qp2destroy->pa_ddr != (MT_phys_addr_t)0)
    {
      MOSAL_protection_ctx_t  ctx; /* was not saved, so recover */
      rc = THH_uldm_get_protection_ctx(qpm->uldm, qp2destroy->pd, &ctx);
      if (rc != HH_OK)
      {
        MTL_ERROR1("THH_qpm_destroy_qp: failed recover protection ctx\n");
      }
      else
      {
        MT_size_t  buf_sz = complete_pg_sz(qp2destroy->wqes_buf_sz);
        MOSAL_unmap_phys_addr(ctx, qp2destroy->wqes_buf, buf_sz);
        THH_ddrmm_free(qpm->ddrmm, qp2destroy->pa_ddr, buf_sz);
      }
    }
  }
    
  if (rc == HH_OK)  {
    if (is_sqp(qpm,qpn)) {
      qpm->sqp_info.sqp_ctx[qp_idx - qpm->sqp_info.first_sqp_qpn]= NULL;
      MOSAL_mutex_rel(&qpm->mtx);
    } else { /* regular RQ */
      VIP_array_erase_done(qpm->qp_tbl, qp_idx - qpm->first_rqp, NULL);
    }
    FREE(qp2destroy);
    
  } else { /* Failure */
    if (is_sqp(qpm,qpn)) {
      MOSAL_mutex_rel(&qpm->mtx);
    } else { /* regular RQ */
      VIP_array_erase_undo(qpm->qp_tbl, qp_idx - qpm->first_rqp);
    }
  }

  
  MTL_TRACE1("}THH_qpm_destroy_qp\n");
  logIfErr("THH_qpm_destroy_qp");
  return  rc;
} /* THH_qpm_destroy_qp */

/************************************************************************/
/* Assumed to be the first called in this module, single thread.        */
void  THH_qpm_init(void)
{
  MTL_TRACE1("THH_qpm_init{ compiled: date=%s, time=%s\n", __DATE__, __TIME__);
  if (check_constants())
  {
    init_state_machine(&state_machine);
  }
  else
  {
    MTL_ERROR1(MT_FLFMT("THH_qpm_init: ERROR bad constants."));
  }
     
  native_page_shift = MOSAL_SYS_PAGE_SHIFT;
  native_page_size  = 1ul << native_page_shift;
  native_page_low_mask  = (1ul << native_page_shift)-1;
  MTL_DEBUG4(MT_FLFMT("native_page: shift=%d, size=0x%x, mask=0x%x"), 
             native_page_shift, native_page_size, native_page_low_mask);
  
  MTL_TRACE1("THH_qpm_init}\n");
} /* THH_qpm_init */


/***********************************************************************************/
/******************************************************************************
 *  Function:     process_local_mad
 *****************************************************************************/
HH_ret_t THH_qpm_process_local_mad(THH_qpm_t  qpm, /* IN */
                          IB_port_t  port,/*IN */
                          IB_lid_t   slid, /* For Mkey violation trap */
                          EVAPI_proc_mad_opt_t proc_mad_opts,/*IN */
                          void *   mad_in,/*IN */
                          void *   mad_out /*OUT*/
                           )
{
    THH_cmd_status_t  cmd_ret;
    HH_ret_t          ret = HH_OK;
    u_int8_t j,num_entries;
    u_int8_t* my_mad_in,*my_mad_out;
    u_int8_t* tbl_tmp = NULL;
    u_int32_t attr;
    SM_MAD_GUIDInfo_t guid_info;
    u_int32_t start_idx=0;
    SM_MAD_Pkey_table_t pkey_tbl;
    MT_bool set_op = FALSE;
    MT_bool validate_mkey = ((proc_mad_opts & EVAPI_MAD_IGNORE_MKEY) ? FALSE : TRUE);

    FUNC_IN;
    
    if (qpm == NULL) {
        MTL_ERROR1("[%s]: ERROR : Invalid qpm handle\n",__FUNCTION__);
        ret = HH_EINVAL;
        goto done;
    }
    
    if ((port > qpm->sqp_info.n_ports) || (port < 1)) {
       MTL_ERROR1("[%s]: ERROR : invalid port number (%d)\n",__FUNCTION__,port);
       ret = HH_EINVAL_PORT;
       goto done;
    }

    memset(mad_out, 0, IB_MAD_LEN);
    
    my_mad_in =(u_int8_t*)mad_in;  
    
    MTL_DEBUG4("%s: MAD IN: \n", __func__);
    MadBufPrint(my_mad_in);
    
    attr = MOSAL_be32_to_cpu(((u_int32_t*)my_mad_in)[4]) >> 16;
    
    MTL_DEBUG4("%s: method:0x%x  attr:0x%x validate_mkey: %s\n", __func__,
               my_mad_in[3],attr, (validate_mkey ? "TRUE" : "FALSE" ));
    
    if (my_mad_in[3] == IB_METHOD_SET) 
    {
      set_op = TRUE; 
    }

    
    cmd_ret = THH_cmd_MAD_IFC(qpm->cmd_if, validate_mkey, slid, port, mad_in, mad_out);
    if (cmd_ret != THH_CMD_STAT_OK) {
        MTL_ERROR2("[%s]: ERROR on port %d: %d \n",__FUNCTION__,port,cmd_ret);
        switch (cmd_ret) {
          case THH_CMD_STAT_EINTR: 
            ret= HH_EINTR; break;
          case THH_CMD_STAT_BAD_PKT: 
          case THH_CMD_STAT_EBADARG:
            ret= HH_EINVAL; break;
          case THH_CMD_STAT_BAD_INDEX: 
            ret= HH_EINVAL_PORT; break;
          default: 
            ret= HH_EFATAL;
        }
        goto done;
    } 
        
    my_mad_out = (u_int8_t*)mad_out;
    
    MTL_DEBUG4("%s: MAD OUT: \n", __func__);
    MadBufPrint(my_mad_out);
    
    if (set_op)
    {
        switch (attr)
        {
    
        case IB_SMP_ATTRIB_PORTINFO: 
          MTL_DEBUG2("[%s]: got  SET_PORTINFO, port %d \n",__FUNCTION__,port);
          tbl_tmp = (u_int8_t*)(qpm->sgid_tbl[port-1]); 
          num_entries = (u_int8_t)qpm->num_sgids[port-1]; 
    
          for (j =0; j< num_entries; j++)
            {
              /* update all the gids' prefixes in my table - BIG Endian*/ 
              memcpy(tbl_tmp + j*sizeof(IB_gid_t),((u_int8_t*)mad_out)+IB_SMP_DATA_START+8,8);  
            }
          MTL_DEBUG2("[%s]: preffix:%d.%d.%d.%d.%d.%d.%d.%d \n",__FUNCTION__,tbl_tmp[0],tbl_tmp[1],
                                tbl_tmp[2],tbl_tmp[3],tbl_tmp[4],tbl_tmp[5],
                                tbl_tmp[6],tbl_tmp[7]);
          break;
            
            //requested to set gids, update in my table
        case IB_SMP_ATTRIB_GUIDINFO:
            MTL_DEBUG2("[%s]: got  SET_GUIDINFO, port %d \n",__FUNCTION__,port);
            printGIDTable(qpm);

            tbl_tmp = (u_int8_t*)(qpm->sgid_tbl[port-1]); 
            num_entries = (u_int8_t)qpm->num_sgids[port-1]; 

            GUIDInfoMADToSt(&guid_info, my_mad_out);
            
            start_idx = MOSAL_be32_to_cpu(((u_int32_t*)my_mad_out)[5])*8;
    
            MTL_DEBUG2("%s: start idx %d \n", __func__,start_idx);
            /* skip in gid table to the starting idx to copy from */ 
            tbl_tmp += (start_idx * sizeof(IB_gid_t));
            
            for (j = 0; j < 8; j++) {
                /* check start index first, just in case already out of range */
                if (start_idx >= num_entries) {
                    break;
                }
                tbl_tmp += 8 /*sizeof gid prefix */;
                memcpy(tbl_tmp, &(guid_info.guid[j]), sizeof(IB_guid_t));
                tbl_tmp += sizeof(IB_guid_t);
                start_idx++;
              }
            printGIDTable(qpm);
            
            break;
        
        case IB_SMP_ATTRIB_PARTTABLE:
            MTL_DEBUG2("[%s]: got  SET_PORTTABLE, port %d \n",__FUNCTION__,port);
            printPKeyTable(qpm);

            num_entries = (u_int8_t)qpm->pkey_tbl_sz[port-1];
            
            /* Select only 16 LSBs */
            start_idx = ((MOSAL_be32_to_cpu(((u_int32_t*)my_mad_out)[5])) & 0xFFFF)*32;
            MTL_DEBUG2("%s: start idx %d \n", __func__,start_idx);
            
            /* copy & change the endieness */
            PKeyTableMADToSt(&pkey_tbl, my_mad_out);

            for (j = 0; j < 32; j++) {
                /* check start index first, just in case already out of range */
                if (start_idx >= num_entries) {
                    break;
                }
                qpm->pkey_tbl[port-1][start_idx++] = pkey_tbl.pkey[j];
            }
            printPKeyTable(qpm);
            
            break;
            
        default: MTL_DEBUG5("%s: no need to do anything \n", __func__);
        }
    }/*end if set_op*/
    
done:    
    MT_RETURN(ret);
}

/******************************************************************************
 *  Function:     THH_qpm_get_sgid
 *****************************************************************************/
HH_ret_t THH_qpm_get_sgid(THH_qpm_t  qpm, /* IN */
                 IB_port_t  port,/*IN */
                 u_int8_t index, /*IN */
                 IB_gid_t* gid_p/*OUT*/
                 )
{
    HH_ret_t ret= HH_OK;

    if ((port<1) || (port > qpm->sqp_info.n_ports))
    {
        return HH_EINVAL_PORT;
    }
    
    if (qpm->sgid_tbl[port-1] == NULL) 
    {
        MTL_ERROR1("[%s]: ERROR: failure getting port %d gid tbl\n",__FUNCTION__,port);
        return HH_EINVAL_PARAM;
    }

    if (index >= qpm->num_sgids[port-1]) 
    {
        MTL_ERROR1("[%s]: ERROR: invalid index",__FUNCTION__);
        return HH_EINVAL_PARAM;
    }
    printGIDTable(qpm);
    memcpy(*gid_p,qpm->sgid_tbl[port-1][index],sizeof(IB_gid_t));
    return ret;
}
                 
/******************************************************************************
 *  Function:     THH_qpm_get_all_sgids
 *****************************************************************************/
HH_ret_t THH_qpm_get_all_sgids(THH_qpm_t  qpm, /* IN */
                 IB_port_t  port,/*IN */
                 u_int8_t num_out_entries, /*IN */
                 IB_gid_t* gid_p/*OUT*/
                 )
{
    HH_ret_t ret= HH_OK;

    if ((port<1) || (port > qpm->sqp_info.n_ports))
    {
        return HH_EINVAL_PORT;
    }
    
    if (qpm->sgid_tbl[port-1] == NULL) 
    {
        MTL_ERROR1("[%s]: ERROR: failure getting port %d gid tbl\n",__FUNCTION__,port);
        return HH_EINVAL_PARAM;
    }

    if (num_out_entries < qpm->num_sgids[port-1]) 
    {
        MTL_ERROR1("[%s]: ERROR: not enough space in output gid table",__FUNCTION__);
        return HH_EAGAIN;
    }
    memcpy(*gid_p,qpm->sgid_tbl[port-1],sizeof(IB_gid_t) * num_out_entries);
    return ret;
}
                 

/******************************************************************************
 *  Function:     THH_qpm_get_qp1_pkey
 *****************************************************************************/
HH_ret_t THH_qpm_get_qp1_pkey(THH_qpm_t  qpm, /* IN */
                 IB_port_t  port,/*IN */
                  VAPI_pkey_t* pkey_p/*OUT*/
                 )
{

    if ((port<1) || (port > qpm->sqp_info.n_ports))
    {
      MTL_ERROR1("%s: port number (%d) not valid\n", __func__,port);
      return HH_EINVAL_PORT;
    }
    

    if (qpm->pkey_tbl[port-1] == NULL) 
    {
        MTL_ERROR1("[%s]: ERROR: failure getting port %d pkey tbl\n",__func__,port);
        return HH_EINVAL_PARAM;
    }

    //qp1 pkey isn't initialized yet
    if (qpm->qp1_pkey_idx[port-1] == 0xffff)
    {
        MTL_ERROR1("[%s]: ERROR: qp1 pkey for port %d isn't initialized yet \n",__func__,port);
        return HH_ERR;
    }
    MTL_DEBUG4("get Pkey: port %d idx: %d \n",port,qpm->qp1_pkey_idx[port-1]);

    *pkey_p = qpm->pkey_tbl[port-1][qpm->qp1_pkey_idx[port-1]];
    return HH_OK;
}

/******************************************************************************
 *  Function:     THH_qpm_get_pkey
 *****************************************************************************/
HH_ret_t THH_qpm_get_pkey(THH_qpm_t  qpm, /* IN */
                          IB_port_t  port,/*IN */
                          VAPI_pkey_ix_t pkey_index,/*IN*/
                          VAPI_pkey_t* pkey_p/*OUT*/)
{
  if ((port<1) || (port > qpm->sqp_info.n_ports)) {
    MTL_ERROR1("%s: port number (%d) not valid\n", __func__,port);
    return HH_EINVAL_PORT;
  }

  if (qpm->pkey_tbl[port-1] == NULL) {
    MTL_ERROR1("%s: ERROR: failure getting port %d pkey tbl\n",__func__,port);
    return HH_EINVAL_PARAM;
  }

  if (pkey_index >= qpm->pkey_tbl_sz[port-1]) {
    MTL_ERROR1("%s: given pkey_index (%d) is beyond pkey table end (%d entries)\n",__func__,
               pkey_index,qpm->pkey_tbl_sz[port-1]);
    return HH_EINVAL_PARAM;
  }

  *pkey_p = qpm->pkey_tbl[port-1][pkey_index];
  return HH_OK;
}

/******************************************************************************
 *  Function:     THH_qpm_get_all_pkeys
 *****************************************************************************/
HH_ret_t THH_qpm_get_all_pkeys(THH_qpm_t  qpm, /* IN */
                 IB_port_t  port,/*IN */
                 u_int16_t  out_num_pkey_entries, /*IN */
                  VAPI_pkey_t* pkey_p /*OUT*/
                 )
{

    if ((port<1) || (port > qpm->sqp_info.n_ports))
    {
        return HH_EINVAL_PORT;
    }
    

    if (qpm->pkey_tbl[port-1] == NULL) 
    {
        MTL_ERROR1("[%s]: ERROR: failure getting port %d pkey tbl\n",__FUNCTION__,port);
        return HH_EINVAL_PARAM;
    }

    if (qpm->pkey_tbl_sz[port-1] > out_num_pkey_entries) {
        MTL_ERROR1("[%s]: ERROR: pkey out table too small (is %d, should be %d) \n",__FUNCTION__,
                   out_num_pkey_entries, qpm->pkey_tbl_sz[port-1]);
        return HH_ERR;
    }
    MTL_DEBUG4("get Pkey table: port %d\n",port);

    memcpy(pkey_p, qpm->pkey_tbl[port-1], sizeof(VAPI_pkey_t)*qpm->pkey_tbl_sz[port-1]);
    return HH_OK;
}


static void printPKeyTable(THH_qpm_t  qpm)
{
#if defined(MAX_DEBUG) && 5 <= MAX_DEBUG
    int i,j;
    
    for (i=0; i< qpm->sqp_info.n_ports; i++)
    {
        MTL_DEBUG5("port %d pkey tbl: \n",i+1);
        for (j=0; j< qpm->pkey_tbl_sz[i]; j++)
        {
            MTL_DEBUG5(" 0x%x ",qpm->pkey_tbl[i][j]);
        }
        MTL_DEBUG5("\n");
    }
#else
    return;
#endif
}

static void printGIDTable(THH_qpm_t  qpm)
{
#if defined(MAX_DEBUG) && 5 <= MAX_DEBUG
    int i,k;
    
    for (k=0; k< qpm->sqp_info.n_ports; k++)
    {
        MTL_DEBUG5("port %d sgid tbl: \n",k+1);
        for (i=0; i< qpm->num_sgids[k]; i++)
        {
            MTL_DEBUG5("GID[%d] = %x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x\n", i, 
             qpm->sgid_tbl[k][i][0],qpm->sgid_tbl[k][i][1],qpm->sgid_tbl[k][i][2],qpm->sgid_tbl[k][i][3],
             qpm->sgid_tbl[k][i][4],qpm->sgid_tbl[k][i][5],qpm->sgid_tbl[k][i][6],qpm->sgid_tbl[k][i][7],
             qpm->sgid_tbl[k][i][8],qpm->sgid_tbl[k][i][9],qpm->sgid_tbl[k][i][10],qpm->sgid_tbl[k][i][11],
             qpm->sgid_tbl[k][i][12],qpm->sgid_tbl[k][i][13],qpm->sgid_tbl[k][i][14],qpm->sgid_tbl[k][i][15]);          
     
        }
        MTL_DEBUG5("\n");
    }
#else
    return;
#endif
}


HH_ret_t  THH_qpm_get_num_qps(THH_qpm_t qpm /* IN */,  u_int32_t *num_qps_p /*OUT*/)
{
  u_int32_t num_objs;

  if (qpm == NULL) {
      MTL_ERROR1("[%s]: ERROR : Invalid qpm handle\n",__FUNCTION__);
      return HH_EINVAL;
  }

  num_objs= VIP_array_get_num_of_objects(qpm->qp_tbl); 
  if (num_objs == (u_int32_t) VIP_EINVAL_HNDL) {
      return HH_EINVAL;
  } else {
      *num_qps_p = num_objs;
      return HH_OK;
  }
}

#if defined(MT_SUSPEND_QP)
/************************************************************************/
/* Same comment about mutex as above THH_qpm_modify_qp(...) applies     */
HH_ret_t  THH_qpm_suspend_qp(
  THH_qpm_t        qpm,       /* IN  */
  IB_wqpn_t        qpn,       /* IN  */
  MT_bool          suspend_flag  /* IN  */
)
{
  HH_ret_t  rc = HH_OK;
  VIP_common_ret_t vret;
  VIP_array_obj_t        qp_obj;
  TQPM_sw_qpc_t*         qp_p;
  u_int32_t              qp_idx = qpn & qpm->idx_mask;

  MTL_TRACE1("{THH_qpm_suspend_qp: qpm=%p, qpn=0x%x, suspend_flag=%s\n", 
             qpm, qpn, ((suspend_flag == TRUE) ? "TRUE" : "FALSE" ));
  if (is_sqp(qpm,qpn)) {
    if (MOSAL_mutex_acq(&qpm->mtx, TRUE) != MT_OK)  return HH_EINTR;
    qp_p= qpm->sqp_info.sqp_ctx[qpn - qpm->sqp_info.first_sqp_qpn];
    if (qp_p == NULL) {
      MTL_ERROR1(MT_FLFMT("%s: Given special QP handle is not active (qpn=0x%X)"),__func__,qpn);
      return HH_EINVAL_QP_NUM;
    }
  } else { /* regular QP */
    vret= VIP_array_find_hold(qpm->qp_tbl, qp_idx - qpm->first_rqp, &qp_obj);
    qp_p= (TQPM_sw_qpc_t*)qp_obj;
    if ((vret != VIP_OK) || (qpn != qp_p->qpn)) {
      MTL_ERROR1(MT_FLFMT("%s: Invalid QP handle (qpn=0x%X)"),__func__,qpn);
      if (vret == VIP_OK)  VIP_array_find_release(qpm->qp_tbl, qp_idx - qpm->first_rqp);
      return HH_EINVAL_QP_NUM;
    }
  }
    
  /* issue tavor command in all cases, since we are not adding a "suspend" state to QP */
  do {
     THH_cmd_status_t    crc;
     
     rc = HH_OK;

     if (qp_p->is_suspended == suspend_flag) {
        /* already in requested suspension state */
         MTL_ERROR1(MT_FLFMT("%s: qpn=0x%X is already in requested state (suspend = %s)"),
                    __func__,qpn, (suspend_flag == FALSE)?"FALSE":"TRUE");
         break;
     }
     if (suspend_flag == FALSE) {
         /* unsuspend request -- restore the internal region */
         /* lkey = 0 ==> no send and no receive WQEs  */
         if (qp_p->lkey != 0) {
             rc = THH_mrwm_suspend_internal(qpm->mrwm_internal,qp_p->lkey,FALSE);
             if (rc != HH_OK) {
                    MTL_ERROR1(MT_FLFMT("%s: THH_mrwm_(un)suspend_internal failed (%d:%s). Region stays suspended"),
                            __func__, rc, HH_strerror_sym(rc));
                    break;
             }
         }
     }
     crc = THH_cmd_SUSPEND_QP(qpm->cmd_if, qpn, suspend_flag);
     if (crc == THH_CMD_STAT_OK)
      { 
         rc = HH_OK;
         if (suspend_flag == TRUE) {
             /* suspend request -- suspend the internal region */
             /* lkey = 0 ==> no send and no receive WQEs  */
             if (qp_p->lkey != 0) {
                 rc = THH_mrwm_suspend_internal(qpm->mrwm_internal,qp_p->lkey,TRUE);
                 if (rc != HH_OK) {
                     MTL_ERROR1(MT_FLFMT("%s: suspend. THH_mrwm_suspend_internal failed (%d:%s). Suspended anyway"),
                                __func__, rc, HH_strerror_sym(rc));
                     rc = HH_OK;
                 }
             }
         }
      } else {
          rc =  ((crc == THH_CMD_STAT_BAD_PARAM) ? HH_EINVAL_PARAM :
                 (crc == THH_CMD_STAT_BAD_INDEX) ? HH_EINVAL_QP_NUM : 
                 (crc == THH_CMD_STAT_BAD_RESOURCE) ? HH_EINVAL_QP_NUM : 
                 (crc == THH_CMD_STAT_BAD_RES_STATE) ? HH_EINVAL_QP_STATE : 
                 (crc == THH_CMD_STAT_BAD_QPEE_STATE) ? HH_EINVAL_QP_STATE : 
                 (crc == THH_CMD_STAT_BAD_QPEE_STATE) ? HH_EINVAL_QP_STATE : 
                 (crc == THH_CMD_STAT_BAD_SYS_STATE) ? HH_EINVAL_HCA_HNDL : 
                 (crc == THH_CMD_STAT_RESOURCE_BUSY) ? HH_EBUSY : HH_ERR);
          MTL_ERROR1(MT_FLFMT("ERROR: THH_cmd_SUSPEND_QP returned %s"),str_THH_cmd_status_t(crc));
      }
      qp_p->is_suspended = suspend_flag;
  } while(0);

  if (is_sqp(qpm,qpn)) {
    MOSAL_mutex_rel(&qpm->mtx);
  } else { /* regular RQ */
    VIP_array_find_release(qpm->qp_tbl, qp_idx - qpm->first_rqp);
  }
  logIfErr("THH_qpm_suspend_qp");
  return rc;
} /* THH_qpm_query_qp */
#endif
