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

#if !defined(H_TQPM_H)
#define H_TQPM_H

#include <mtl_common.h>
#include <vapi_types.h>
#include <hh.h>
#include <hhul.h>
#include <thh.h>


/************************************************************************
 *  Structure to pass to THH_qpm_create().  Make sure to initialize
 *  (via memset(&, 0, sizeof()) with zeros before setting fields.
 *  Thus future enhancement may ease backward compatible.
 * 
 *    log2_max_qp - (log2) Max. number of QPs (QPC table size)
 *    rdb_base_index - virtual index to area allocated by HOB (see PRM 5.2).
 *    log2_max_outs_rdma_atom - log2 of number allocated per each QP,
 *                              statrting from rdb_base_index.
 *    n_ports - Number of ports for this HCA. Needed for special QP allocation.
 */
typedef struct
{
  u_int32_t  rdb_base_index;
  u_int8_t   log2_max_qp;
  u_int8_t   log2_rsvd_qps;
  u_int8_t   log2_max_outs_rdma_atom;
  u_int8_t   log2_max_outs_dst_rd_atom;
  u_int8_t   n_ports;
  struct THH_port_init_props_st*  port_props; /* (cmd_types.h) indexed from 1 */
} THH_qpm_init_t;

#define DEFAULT_SGID_TBL_SZ 32
#define DEFAULT_PKEY_TBL_SZ 64
#define NUM_PORTS 2  /* Hardware limit. Real n_ports is limited by the init params. */
#define NUM_SQP_PER_PORT 4 /* SMI, GSI, RawEth, RawIPv6 */
#define NUM_SQP (NUM_PORTS * NUM_SQP_PER_PORT)
#define MAX_QPN_PREFIX_LOG 12
#define MAX_QPN_PREFIX (1<<MAX_QPN_PREFIX_LOG)
#define QPN_PREFIX_INDEX_MASK (MAX_QPN_PREFIX - 1)

#define TQPM_GOOD_ALLOC(sz) THH_SMART_MALLOC(sz)
#define TQPM_GOOD_FREE(ptr,sz) THH_SMART_FREE(ptr,sz)

/************************************************************************
 *  Function: THH_qpm_create
 *
 *  Arguments:
 *    hob -         The THH_hob object in which this object will be included
 *    init_attr_p   Initialization parameters - see above.
 *    cqm_p -       The allocated QPM object
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *    HH_EAGAIN - Not enough resources available
 *
 *  Description:
 *    This function creates the THH_qpm object.
 */
extern HH_ret_t  THH_qpm_create(
  THH_hob_t              hob,          /* IN  */
  const THH_qpm_init_t*  init_attr_p,  /* IN  */
  THH_qpm_t*             qpm_p         /* OUT */
);


/************************************************************************
 *  Function: THH_qpm_destroy
 *
 *  Arguments:
 *    qpm         - The object to destroy
 *    hca_failure - When TRUE an HCA failure requires the destruction
 *                  of this object.
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid handle
 *
 *  Description:
 *    Free all QPM related resources.
 */
extern HH_ret_t  THH_qpm_destroy(
  THH_qpm_t  qpm,        /* IN */
  MT_bool    hca_failure /* IN */
);

/************************************************************************
 *  Function: THH_qpm_create_qp
 *
 *  Arguments:
 *    qpm -               QPM object context
 *    prot_ctx            protection context of the calling thread
 *    init_attr_p -       QP's initial attributes
 *    mlx -               Ignore ts_type given in init_attr_p and make QP MLX
 *    qp_ul_resources_p - Resources passed from user level.
 *    qpn_p -             The allocated QP handle
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *    HH_EAGAIN - Not enough resources available to complete operation
 *
 *  Description:
 *    Allocate a QP resource in the HCA (QP created in the Reset state).
 */
extern HH_ret_t  THH_qpm_create_qp(
  THH_qpm_t               qpm,               /* IN  */
  HH_qp_init_attr_t*      init_attr_p,       /* IN  */
  MT_bool                 mlx,               /* IN  */
  THH_qp_ul_resources_t*  qp_ul_resources_p, /* IO  */
  IB_wqpn_t*              qpn_p              /* OUT */
);


/************************************************************************
 *  Function: THH_qpm_get_special_qp
 *  
 *  Arguments:
 *    qpm -               QPM object context 
 *    qp_type -           Special QP type 
 *    port -              Special QP on given port 
 *    init_attr_p -       QP's initial attributes 
 *    qp_ul_resources_p - Resources passed from user level 
 *    sqp_hndl_p -        The allocated special QP handle (QPC index)
 *  
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *    HH_EAGAIN - Not enough resources available to complete operation
 *    HH_EBUSY -  Given QP type on given port is already in use 
 */
extern HH_ret_t  THH_qpm_get_special_qp(
 THH_qpm_t               qpm,                /* IN  */
 VAPI_special_qp_t       qp_type,            /* IN  */
 IB_port_t               port,               /* IN  */
 HH_qp_init_attr_t*      init_attr_p,        /* IN  */
 THH_qp_ul_resources_t*  qp_ul_resources_p,  /* IO  */
 IB_wqpn_t*              sqp_hndl_p          /* OUT */
);


/************************************************************************
 *  Function: THH_qpm_modify_qp
 *
 *  Arguments:
 *    qpm -            THH_qpm object
 *    qpn -            The QP to modify
 *    cur_qp_state -   Assumed current QP state (modify from).
 *    qp_attr_p -      QP attributes structure
 *    qp_attr_mask_p - Attributes actually valid in qp_attr_p
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL -          Invalid parameters (NULL ptrs. etc.)
 *    HH_EAGAIN -          Not enough resources available to complete operation
 *    HH_EINVAL_QP_NUM -   Unknown QP
 *    HH_EINVAL_QP_STATE - current_qp_state does not match actual QP state.
 *
 *  Description:
 *    Modify QP attributes and state.
 */
extern HH_ret_t  THH_qpm_modify_qp(
  THH_qpm_t             qpm,             /* IN  */
  IB_wqpn_t             qpn,             /* IN  */
  VAPI_qp_state_t       cur_qp_state,    /* IN  */
  VAPI_qp_attr_t*       qp_attr_p,       /* IN  */
  VAPI_qp_attr_mask_t*  qp_attr_mask_p   /* IN  */
);


/************************************************************************
 *  Function:  THH_qpm_query_qp
 *  
 *  Arguments:
 *    qpm -       THH_qpm object
 *    qpn -       The QP to modify
 *    qp_attr_p - Returned QP attributes
 *  
 *  Returns:
 *    HH_OK
 *    HH_EINVAL -      Invalid handles
 *    HH_EINVAL_QP_NUM
 *  
 *  Description:
 *    Query QP state and attributes.
 */
extern HH_ret_t  THH_qpm_query_qp(
  THH_qpm_t        qpm,       /* IN  */
  IB_wqpn_t        qpn,       /* IN  */
  VAPI_qp_attr_t*  qp_attr_p  /* IN  */
);

/************************************************************************
 *  Function: THH_qpm_destroy_qp
 *  
 *  Arguments:
 *    qpm - The THH_qpm object handle
 *    qp - The QP to destroy
 *  
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid handles
 *  
 *  Description: Free QP resources.
 */

extern HH_ret_t  THH_qpm_destroy_qp(
  THH_qpm_t  qpm, /* IN */
  IB_wqpn_t  qp   /* IN */
);

/************************************************************************
 *  Function:   THH_qpm_process_local_mad
 *  
 *  Arguments:
 *    qpm - The THH_qpm object handle
 *    port - the port the mad came in 
 *    proc_mad_opts - for setting non-default options
 *    mad_in - 
 *    mad_out - 
 *  
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid handles
 *    HH_EINVAL_PORT - Invalid port 
 *    HH_ERR - error processing the mad
 *  
 *  Description: process the mad (FW) in the port 
 */
extern HH_ret_t THH_qpm_process_local_mad(THH_qpm_t  qpm,
                                          IB_port_t port, IB_lid_t   slid, /* For Mkey violation trap */
                                          EVAPI_proc_mad_opt_t proc_mad_opts, 
                                          void* mad_in,void* mad_out_p);

/************************************************************************
 *  Function:   THH_qpm_get_sgid
 *  
 *  Arguments:
 *    qpm - The THH_qpm object handle
 *    port - the port of the pkey table 
 *    idx - the index in the gid tbl
 *    gid_p -  
 *  
 *  Returns:
 *    HH_OK
 *  
 *  Description:  
 */
HH_ret_t THH_qpm_get_sgid(THH_qpm_t  qpm,IB_port_t  port,u_int8_t idx, IB_gid_t* gid_p);

/************************************************************************
 *  Function:   THH_qpm_get_all_sgids
 *  
 *  Arguments:
 *    qpm - The THH_qpm object handle
 *    port - the port of the pkey table 
 *    num_out_entries -  the number of entries in the output gid tbl
 *    gid_p - pointer to the output gid table 
 *  
 *  Returns:
 *    HH_OK
 *  
 *  Description:  
 */
HH_ret_t THH_qpm_get_all_sgids(THH_qpm_t  qpm,IB_port_t  port,u_int8_t num_out_entries, IB_gid_t* gid_p);

/************************************************************************
 *  Function:   THH_qpm_get_qp1_pkey
 *  
 *  Arguments:
 *    qpm - The THH_qpm object handle
 *    port - the port of the pkey table 
 *    pkey_p - 
 *  
 *  Returns:
 *    HH_OK
 *  
 *  Description:  
 */
HH_ret_t THH_qpm_get_qp1_pkey(THH_qpm_t  qpm,IB_port_t  port,VAPI_pkey_t* pkey);

/************************************************************************
 *  Function:   THH_qpm_get_qp1_pkey
 *  
 *  Arguments:
 *    qpm - The THH_qpm object handle
 *    port - the port of the pkey table 
 *    pkey_index - Index of Pkey to return
 *    pkey_p - returned pkey
 *  
 *  Returns:
 *    HH_EINVAL_PORT
 *    HH_EINVAL_PARAM
 *    HH_OK
 *  
 *  Description: 
 *    Return Pkey at given {port,index}
 */
HH_ret_t THH_qpm_get_pkey(THH_qpm_t  qpm, /* IN */
                          IB_port_t  port,/*IN */
                          VAPI_pkey_ix_t pkey_index,/*IN*/
                          VAPI_pkey_t* pkey_p/*OUT*/);

/************************************************************************
 *  Function:   THH_qpm_get_all_pkeys
 *  
 *  Arguments:
 *    qpm - The THH_qpm object handle
 *    port - the port of the pkey table 
 *    out_num_pkey_entries - num of pkey entry slots in provided pkey table
 *    pkey_p - output pkey table
 *  
 *  Returns:
 *    HH_OK
 *  
 *  Description:  
 */
HH_ret_t THH_qpm_get_all_pkeys(THH_qpm_t  qpm, IB_port_t  port,
                 u_int16_t  out_num_pkey_entries, VAPI_pkey_t* pkey_p );



/************************************************************************
 *  Function:   THH_qpm_get_num_qps
 *  
 *  Arguments:
 *    qpm - The THH_qpm object handle
 *    num_qps_p - ptr to returned current num qps
 *  
 *  Returns:
 *    HH_OK
 *    HH_EINVAL -- if qpm invalid, or if vip array invalid.
 *  
 *  Description:  
 *    returns number of currently allocated QPs
 */
HH_ret_t  THH_qpm_get_num_qps(THH_qpm_t qpm /* IN */,  u_int32_t *num_qps_p /*OUT*/);

#if defined(MT_SUSPEND_QP)
extern HH_ret_t  THH_qpm_suspend_qp(
  THH_qpm_t        qpm,       /* IN  */
  IB_wqpn_t        qpn,       /* IN  */
  MT_bool          suspend_flag /* IN  */
);
#endif

#endif /* H_TQPM_H */
