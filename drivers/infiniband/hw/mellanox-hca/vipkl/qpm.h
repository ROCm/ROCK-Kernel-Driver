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


#ifndef H_QPM_H
#define H_QPM_H

#include <vip.h>
#include <vip_array.h>
#include <vapi.h>
#include <pdm.h>


/*struct QPM_t;
typedef struct QPM_t* QPM_hndl_t;*/
/*typedef u_int32_t QPM_qp_hndl_t;*/
#define QPM_INVAL_QP_HNDL VAPI_INVAL_HNDL /* For denoting uninitialized handle */

/* attributes needed for QP initializing */
typedef struct {
  SRQM_srq_hndl_t   srq_hndl;             /* Optional: invalid if set to SRQM_INVAL_SRQ_HNDL */
  CQM_cq_hndl_t     sq_cq_hndl;           /* Completion Queue handle for Send Queue */
  CQM_cq_hndl_t     rq_cq_hndl;           /* Completion Queue handle for Receive Queue */
  VAPI_qp_cap_t     qp_cap;
  VAPI_sig_type_t   sq_sig_type;          /* Completion Queue Signal Type (ALL/UD)*/
  VAPI_sig_type_t   rq_sig_type;          /* Completion Queue Signal Type (ALL/UD)*/
  PDM_pd_hndl_t     pd_hndl;              /* Protection Domain Handle */
  VAPI_ts_type_t    ts_type;              /* Transport Service Type (RC/UC/RD/UD) */
} QPM_qp_init_attr_t;


typedef struct {
  VAPI_qp_attr_t    qp_mod_attr;          /* the modifiers attributes */
  CQM_cq_hndl_t     sq_cq_hndl;           /* Completion Queue handle for Send Queue */
  CQM_cq_hndl_t     rq_cq_hndl;           /* Completion Queue handle for Receive Queue */
  VAPI_sig_type_t   sq_sig_type;          /* Completion Queue Signal Type (ALL/UD)*/
  VAPI_sig_type_t   rq_sig_type;          /* Completion Queue Signal Type (ALL/UD)*/
  PDM_pd_hndl_t     pd_hndl;              /* Protection Domain Handle */
  VAPI_ts_type_t    ts_type;              /* Transport Service Type (RC/UC/RD/UD) */

}  QPM_qp_query_attr_t ;

typedef struct {
	MT_bool	valid;
	VAPI_qp_attr_mask_t	allowed_mask;
	VAPI_qp_attr_mask_t must_mask;
} QPM_qp_state_node_t;

enum {
   QPM_SMI_QP= VAPI_SMI_QP , 
   QPM_GSI_QP= VAPI_GSI_QP, 
   QPM_RAW_IPV6_QP= VAPI_RAW_IPV6_QP, 
   QPM_RAW_ETY_QP= VAPI_RAW_ETY_QP , 
   QPM_NORMAL_QP= VAPI_REGULAR_QP   /* Different than above types */
};

typedef u_int32_t QPM_qp_type_t;

#define QPM_NUM_SPECIAL_QP_TYPE 4



/******************************************************************************
 *  Function: QPM_new
 *
 *  Description: Create a QPM object associated with given HCA object.
 *
 *  Parameters:
 *    hca_hndl(IN) = HCA handle
 *    qpm_hndl_p(OUT)  = Queue Pair Manager for the HCA Object
 *
 *  Returns: VIP_OK
 *    VIP_EINVAL_HCA_HNDL : invalid HCA handle
 *    VIP_EAGAIN : out of resources
 *
 *****************************************************************************/
VIP_ret_t
QPM_new (HOBKL_hndl_t hca_hndl, QPM_hndl_t *qpm_hndl);

/******************************************************************************
 *  Function: QPM_delete
 *
 *  Description: Destroy a QPM instance from the VIP
 *
 *  Parameters:
 *    qpm_hndl(IN)  = the Queue Pair Manager handle
 *
 *  Returns: VIP_OK
 *    VIP_EINVAL_QPM_HNDL : invalid QPM handle
 *
 *  Note: If this QPM still has one or more QP object attached to it, it will 
 *        destroy them before destroying itself. 
 *
 *****************************************************************************/
VIP_ret_t
QPM_delete (QPM_hndl_t qpm_hndl);

/*************************************************************************
 * Function: QPM_set_vapi_hca_hndl
 *
 * Arguments:
 *  qpm (IN): QPM hndl.
 *  hca_hndl (IN): VAPI hca handle for this CQM.
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_QPM_HNDL
 *
 * Description:
 *  Set VAPI HCA handle associated with this QPM - for destroy event callbacks.
 *
 *************************************************************************/ 
VIP_ret_t  QPM_set_vapi_hca_hndl(/*IN*/ QPM_hndl_t qpm, /*IN*/VAPI_hca_hndl_t hca_hndl);


/******************************************************************************
 *  Function: QPM_create_qp
 *
 *  Description: Add new Queue Pair Object into QPM
 *
 *  Parameters:
 *    qpm_hndl(IN) = QPM handle
 *    vapi_qp_hndl(IN) = VAPI handle to QP
 *    qp_ul_resources_p -- a pointer to copy of user level resources allocated for queue pair
 *    qp_init_attr_p(IN) = Pointer to a struct with all attributes needed for initialize
 *    async_hndl_ctx(IN) = handle for asynch error handler context
 *    qp_hndl_p(OUT) = Pointer to the new qp object handle
 *    qp_num_p(OUT) = Pointer to the returned qp number
 *    qp_cap_p(OUT) = Pointer to the actual capibilities (max_sq/rq_outs_wr & max_sq/rq_sg_entries) 
 *
 *  Returns: VIP_OK
 *    VIP_EAGAIN : out of resources
 *    VIP_EINVAL_QPM_HNDL : invalid QPM handle
 *    VIP_EINVAL_CQ_HNDL : invalid CQ handle
 *    VIP_E2SMALL_BUF : buffer too small for requirements
 *    VIP_E2BIG_WR_NUM : number of WR exceeded HCA cap
 *    VIP_E2BIG_SG_NUM : number of scatter/gather entries exceeded HCA cap 
 *    VIP_EINVAL_PD_HNDL : invalid protection domain handle
 *    VIP_EINVAL_SERVICE_TYPE : invalid service type 
 *
 *****************************************************************************/
VIP_ret_t
QPM_create_qp (VIP_RSCT_t usr_ctx,
               QPM_hndl_t qpm_hndl, 
               VAPI_qp_hndl_t vapi_qp_hndl,
               EM_async_ctx_hndl_t async_hndl_ctx,
               void *qp_ul_resources_p,
               QPM_qp_init_attr_t *qp_init_attr_p,
               QPM_qp_hndl_t *qp_hndl_p, 
               IB_wqpn_t     *qp_num_p);


/******************************************************************************
 *  Function: QPM_get_special_qp
 *
 *  Description: Add special Queue Pair Object into QPM
 *
 *
 *  Parameters:
 *    qpm_hndl(IN) = QPM handle
 *    qp_type(IN) = QP Special Type 
 *    phy_port(IN) = the Special QP physical port number
 *    vapi_qp_hndl(IN) = The handle to return for events affiliated with this QP
 *    qp_ul_resources_p(IN) = The user level resources of the QP 
 *                          (as taken from THHUL_get_special_qp_prep)
 *    qp_init_attr_p(IN) = Pointer to a struct with all regular attributes needed for initialize
 *    qp_hndl_p(OUT) = the new qp object handle
 *    qp_cap_p(OUT) = The actual capibilities (max_sq/rq_outs_wr & max_sq/rq_sg_entries) 
 *
 *  Returns: VIP_OK
 *    VIP_EAGAIN : out of resources
 *    VIP_EINVAL_QPM_HNDL : invalid QPM handle
 *    VIP_EINVAL_CQ_HNDL : invalid CQ handle
 *    VIP_E2SMALL_BUF : buffer too small for requirements              **  add to the vip errors **
 *    VIP_E2BIG_WR_NUM : number of WR exceeded HCA cap
 *    VIP_E2BIG_SG_NUM : number of scatter/gather entries exceeded HCA cap 
 *    VIP_EINVAL_PD_HNDL : invalid protection domain handle
 *    VIP_EINVAL_SERVICE_TYPE : invalid service type 
 *    VIP_EBUSY : QP already in use (GSI, SMI QPs only)
 *    VIP_E2BIG_RAW_DGRAM_NUM : number of raw datagram QP exceeded
 *    VIP_ENOSYS_RAW : raw datagram QPs are not supported
 *
 *****************************************************************************/
VIP_ret_t QPM_get_special_qp (VIP_RSCT_t          usr_ctx,
                              QPM_hndl_t          qpm_hndl,
                              VAPI_qp_hndl_t      vapi_qp_hndl, /* VAPI handle to the QP */
                              EM_async_ctx_hndl_t async_hndl_ctx,
                              void               *qp_ul_resources_p, 
                              VAPI_special_qp_t   qp_type,
                              IB_port_t           port,
                              QPM_qp_init_attr_t *qp_init_attr_p, 
                              QPM_qp_hndl_t      *qp_hndl_p, 
                              IB_wqpn_t          *qp_num_p);



/******************************************************************************
 *  Function: QPM_destroy_qp
 *
 *  Description: Destroy an existent Queue Pair. 
 *    Deallocation of all pending resources associated with the QP is the 
 *      responsibility of the consumer.
 *
 *  Parameters:
 *      qpm_hndl(IN) : QPM Handle.
 *      qp_hndl(IN) : QP Handle.
 *      in_rsct_cleanup(IN): TRUE if called by resource cleanup (in which case QP is detached from MCGs)
 *  Returns: VIP_OK
 *          VIP_EINVAL_QPM_HNDL: invalid QPM handle.
 *          VIP_EINVAL_QP_HNDL: invalid QP handle.
 *          VIP_EPERM: permission denied.       
 *****************************************************************************/
VIP_ret_t
QPM_destroy_qp (VIP_RSCT_t usr_ctx,QPM_hndl_t qpm_hndl, QPM_qp_hndl_t qp_hndl, MT_bool in_rsct_cleanup);

VIP_ret_t QPM_set_destroy_qp_cbk(
  /*IN*/ QPM_hndl_t               qpm_hndl,
  /*IN*/ QPM_qp_hndl_t            qp_hndl,
  /*IN*/ EVAPI_destroy_qp_cbk_t   cbk_func,
  /*IN*/ void*                    private_data
);
 
VIP_ret_t QPM_clear_destroy_qp_cbk(
  /*IN*/ QPM_hndl_t               qpm_hndl,
  /*IN*/ QPM_qp_hndl_t            qp_hndl
);

/******************************************************************************
 *  Function: QPM_modify_qp
 *
 *  Description: Modify existing parameters of a Queue Pair Context
 *               Usually used to change QP state.
 *
 *  Parameters:
 *    qpm_hndl(IN)  = the Queue Pair Manager handle
 *    qp_hndl(IN) = Queue Pair handle.
 *    qp_mod_mask_p(IN) =  Pointer to mask bit set of elements in the propeties structure to modify  
 *    qp_mod_attr_p(IN) = Pointer to Structure of Queue Pair attributes including new values. 
 *
 *
 *  Returns: VIP_OK
 *    VIP_EINVAL_QPM_HNDL : invalid QPM handle
 *    VIP_EINVAL_QP_HNDL: invalid QP handle.
 *    VIP_EAGAIN: out of resources.
 *    VIP_ENOSYS_ATTR : needs more attributes for the transition
 *    VIP_EINVAL_ATTR : can't change attribute
 *    VIP_ENOSYS_ATOMIC : atomic operation not supported
 *    VIP_EINVAL_PKEY_IX : Pkey index is out of range
 *    VIP_EINVAL_PKEY_TBL_ENTRY : Pkey index points to an invalid entry in pkey table
 *    VIP_EINVAL_QP_STATE : invalid QP state.
 *    VIP_EINVAL_MIG_STATE : invalid path migration state.
 *    VIP_E2BIG_MTU :   MTU exceeded HCA port capabilities
 *    VIP_EINVAL_PORT :  invalid port number
 *    VIP_EINVAL_RNR_NAK_TIMER : invalid RNR NAK timer value
 *    VIP_EINVAL_LOCAL_ACK_TIMEOUT : invalid Local ACK Timeout value
 *
 *  Note: Only a subset of the values can be modify during this call. 
 *        For each state transmition there is a subset of attributes that must be
 *        modified and a subset of attributes that can be modified .
 *        If one of the required attributes for the transmition is missing or one 
 *        of the attibutes asked to be modified can't be modified the operation shall fail.
 *        For more details see IB Spec.
 *
 *****************************************************************************/
VIP_ret_t
QPM_modify_qp (VIP_RSCT_t usr_ctx,QPM_hndl_t qpm_hndl, QPM_qp_hndl_t qp_hndl, VAPI_qp_attr_mask_t *qp_mod_mask_p,
               VAPI_qp_attr_t *qp_mod_attr_p);

/******************************************************************************
 *  Function:  QPM_query_qp
 *
 *  Description: Returns the attribute list and the current values for specified QP
 *
 *  Parameters:
 *    qpm_hndl(IN)  = the Queue Pair Manager handle
 *    qp_hndl(IN)  = the Queue Pair handle
 *    qp_query_prop_p(OUT) = pointer to the returned Queue Pair properties 
 *    qp_mod_mask_p(OUT) =  Pointer to mask bit set of elements in the mofiable propeties 
 *                          structure, to notify which values are valid.  
 *  Returns: VIP_OK
 *    VIP_EINVAL_QPM_HNDL : invalid QPM handle
 *    VIP_EINVAL_QP_HNDL: invalid QP handle.
 *
 *  Note: Part of the properties in the struct are not valid for some qp states 
 *         and / or Transition Srevice Types the user will be notified by the mask 
 *         which properties are valid. 
 *         Note that the initializing values are not included in the mask, 
 *          one can always retrieve the  initializing values.
 *         
 *
 *****************************************************************************/
VIP_ret_t
QPM_query_qp (VIP_RSCT_t usr_ctx,QPM_hndl_t qpm_hndl, QPM_qp_hndl_t qp_hndl, QPM_qp_query_attr_t *qp_query_prop_p,
              VAPI_qp_attr_mask_t *qp_mod_mask_p);



VIP_ret_t QPM_get_vapiqp_by_qp_num(QPM_hndl_t qpm_hndl, 
                                   VAPI_qp_num_t qp_num, 
                                   VAPI_qp_hndl_t *vapi_qp_hndl_p,
                                   EM_async_ctx_hndl_t *async_hndl_ctx);

VIP_ret_t MCG_detach_from_multicast(VIP_RSCT_t usr_ctx,QPM_hndl_t    qpm_hndl, 
                                    IB_gid_t      mcg_dgid,
                                    QPM_qp_hndl_t qp_hndl);



VIP_ret_t MCG_attach_to_multicast(VIP_RSCT_t usr_ctx,QPM_hndl_t    qpm_hndl, 
                                  IB_gid_t      mcg_dgid,
                                  QPM_qp_hndl_t qp_hndl);


/*************************************************************************
 * Function: QPM_get_num_qps
 *
 * Arguments:
 *  pdm (IN) - QPM object 
 *  num_qps (OUT) - returns number of currently allocated qps
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_QPM_HNDL
 *  VIP_EINVAL_PARAM -- NULL pointer given for num_qps
 *
 * Description:
 *   returns number of currently allocated qps
 *************************************************************************/ 
VIP_ret_t QPM_get_num_qps(QPM_hndl_t qpm_hndl, u_int32_t *num_qps);

struct qp_item_st;
typedef struct qp_data_st{
  IB_wqpn_t qpn;
  VAPI_qp_state_t qp_state;	
  HH_cq_hndl_t    rq_cq_id;
  HH_cq_hndl_t    sq_cq_id;
  IB_port_t       port;        /* Port Number of the QP */
  VAPI_ts_type_t  ts_type;         /* Transport Services Type */
  struct qp_data_st *next;
}
qp_data_t;



/*************************************************************************
 * Function: QPM_get_qp_list
 *
 * Arguments:
 *  qpm (IN) - QPM handle
 *  qp_list (OUT) - list of QPs in the array
 *
 * Returns:
 *
 * Description: This function allocates a list of resources. The resources
 *              must be freed by calling VIPKL_get_rsrc_str itteratively
 *              is described in VIPKL_get_rsrc_str
 *************************************************************************/ 
VIP_ret_t QPM_get_qp_list(QPM_hndl_t qpm, struct qp_item_st *qp_item_p);

#if defined(MT_SUSPEND_QP)
VIP_ret_t
QPM_suspend_qp (VIP_RSCT_t usr_ctx,QPM_hndl_t qpm_hndl, QPM_qp_hndl_t qp_hndl, MT_bool suspend_flag);
#endif

#endif

