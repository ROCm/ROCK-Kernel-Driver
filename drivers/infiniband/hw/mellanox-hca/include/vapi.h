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
#ifndef H_VAPI_H
#define H_VAPI_H

#include <mtl_common.h>
#include <vapi_types.h>
#include <evapi.h>
#include <vapi_features.h>


/********************************************************************************************
 * VAPI Calls Declarations
 *
 *
 ********************************************************************************************/

/*******************************************
 * 11.2.1 HCA
 *
 *******************************************/
/*************************************************************************
 * Function: VAPI_open_hca
 *
 * Arguments:
 *  hca_id: 		HCA identifier.
 * 	hca_hndl_p: 	Pointer to the HCA object handle.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EAGAIN: not enough resources.
 * 	VAPI_EINVAL_HCA_ID: invalid HCA identifier.
 *  VAPI_EBUSY: HCA already in use
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (open device file, or ioctl) 
 *                 has returned an error.
 *  
 *  
 *
 * Description:
 *  
 *  Creates a new HCA Object.
 * 
 *  The creation of an HCA Object will call HH_get_dev_prop in order to find out the 
 *  device capabilities and so allocate enough resource.
 *  
 *  After the resource allocation is completed a call to HH_open_hca will be made in order 
 *  to prepare the device for consumer use.
 *  This call will also create and CIO (Channel Interface Object) which is a container for 
 *  any object related to the opened HCA.
 *  
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API  VAPI_open_hca(
                         IN      VAPI_hca_id_t          hca_id,
                         OUT     VAPI_hca_hndl_t       *hca_hndl_p
                         );                


/*************************************************************************
 * Function: VAPI_query_hca_cap
 *
 * Arguments:
 *  hca_hndl: 	HCA object handle.
 * 	hca_vendor_p: 	Pointer to HCA vendor specific information object.
 *  hca_cap_p: Pointer to HCA capabilities object
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 * 	VAPI_EAGAIN: not enough resources.
 *  VAPI_EPERM: not enough permissions. 
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *  
 *
 * Description:
 *  
 *  Query HCA capabilities retrieves a structure of type VAPI_hca_vendor_t providing a 
 *  list of the vendor specific information about the HCA, and a structure of type 
 *  VAPI_hca_cap_t providing a detailed list of the HCA capabilities. Further informtion 
 *  on the hca ports can be retrieved using the verb VAPI_query_hca_port_prop and 
 *  VAPI_query_hca_port_tbl.
 *  
 *  
 *  This used to be AV, but it should be Add Handl
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_query_hca_cap(
                             IN      VAPI_hca_hndl_t      hca_hndl,
                             OUT     VAPI_hca_vendor_t   *hca_vendor_p,
                             OUT     VAPI_hca_cap_t      *hca_cap_p
                             );


/*************************************************************************
 * Function: VAPI_query_hca_port_prop
 *
 * Arguments:
 *  hca_hndl: 	HCA object handle.
 *  port_num:   Port number
 * 	hca_port_p: HCA port object describing the port properties.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *  VAPI_EINVAL_PORT: invalid port number
 *  VAPI_EAGAIN: not enough resources.
 *  VAPI_EPERM: not enough permissions. 
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *  
 *
 * Description:
 *  
 *  Query HCA port properties retrieves a structure of type VAPI_hca_port_t for the port 
 *  specified in port_num. The number of the HCA physical ports ca be obtained using the 
 *  verb VAPI_query_hca_cap. Further information about the port p-key table and gid 
 *  table can be obtained using the verb VAPI_query_hca_port_tbl.
 *  
 *  Upon successful completion, the verb returns in hca_port_p structure of type  
 *  VAPI_hca_port_t, which is described in the following table:
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_query_hca_port_prop(
                                   IN      VAPI_hca_hndl_t      hca_hndl,
                                   IN      IB_port_t            port_num,
                                   OUT     VAPI_hca_port_t     *hca_port_p  /* set to NULL if not interested */
                                   );


/*************************************************************************
 * Function: VAPI_query_hca_gid_tbl
 *
 * Arguments:
 *  hca_hndl: 	HCA object handle.
 *  port_num:   Port number
 *  tbl_len_in: Number of entries in given gid_tbl_p buffer.
 *  tbl_len_out: Actual number of entries in this port GID table
 *  gid_tbl_p:  The GID table buffer to return result in.
 *  
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *  VAPI_EINVAL_PORT: invalid port number
 *  VAPI_EAGAIN: tbl_len_out > tbl_len_in.
 *  VAPI_EINVAL_PARAM: .invalid port number. 
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *  
 *
 * Description:
 *  
 *  GID table of given port in returned in gid_tbl_p.
 *  If tbl_len_out (actual number of entries) is more than tbl_len_in, the function should be 
 *  called again with a larger buffer.
 *  
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_query_hca_gid_tbl(
                                 IN  VAPI_hca_hndl_t           hca_hndl,
                                 IN  IB_port_t                 port_num,
                                 IN  u_int16_t                 tbl_len_in,
                                 OUT u_int16_t                *tbl_len_out,
                                 OUT IB_gid_t                 *gid_tbl_p    
                                 );


/*************************************************************************
 * Function: VAPI_query_hca_pkey_tbl
 *
 * Arguments:
 *  hca_hndl: 	HCA object handle.
 *  port_num:   Port number
 *  tbl_len_in: Number of entries in given pkey_tbl_p buffer.
 *  tbl_len_out: Actual number of entries in this port PKEY table
 *  pkey_tbl_p: The PKEY table buffer to return result in.
 *  
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *  VAPI_EINVAL_PORT: invalid port number
 *  VAPI_EAGAIN: tbl_len_out > tbl_len_in.
 *  VAPI_EINVAL_PARAM: .invalid port number. 
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 * * Description:
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_query_hca_pkey_tbl(
                                  IN  VAPI_hca_hndl_t           hca_hndl,
                                  IN  IB_port_t                 port_num,
                                  IN  u_int16_t                 tbl_len_in,
                                  OUT u_int16_t                *tbl_len_out,
                                  OUT VAPI_pkey_t              *pkey_tbl_p
                                  );


/*************************************************************************
 * Function: VAPI_modify_hca_attr 
 *
 * Arguments:
 *  hca_hndl: Handle to HCA.
 *  port_num: Port number
 *  hca_attr_p: Pointer to the HCA attributes structure
 *  hca_attr_mask_p: Pointer to the HCA attributes mask
 *  
 *  
 * Returns: VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *  VAPI_EINVAL_PORT: Invalid port number
 *  VAPI_EAGAIN: failed on resource allocation
 *  VAPI_EGEN: function called not from user level context
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *
 * Description:
 *  
 *  Sets the HCA attributes specified in hca_attr_p to port number port_num. Only the 
 *  values specified in hca_attr_mask_p are being modified. hca_attr_p is a pointer to a 
 *  structure of type VAPI_hca_attr_t, which is specified in the following table:
 *  
  *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_modify_hca_attr(
                               IN  VAPI_hca_hndl_t          hca_hndl,
                               IN  IB_port_t                port_num,
                               IN  VAPI_hca_attr_t         *hca_attr_p,
                               IN  VAPI_hca_attr_mask_t    *hca_attr_mask_p
                               );


/*************************************************************************
 * Function: VAPI_close_hca 
 *
 * Arguments:
 *  hca_hndl: Handle to HCA.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *	VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *  
 *
 * Description:
 *  
 *  This call will deallocate all the structures allocated during the called to 
 *  VAPI_open_hca and any other resource in the domain of the CI. 
 *  
 *  It is the responsibility of the consumers to free resources allocated for the HCA that are 
 *  under its scope.
 *  
 *  VAPI will call HH_VClose_HCA in order to instruct the device to stop processing new 
 *  requests and close in-process ones. This will be done before releasing any resource 
 *  belonging to the CI.
 *  
 *  
 *  
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_close_hca(
                         IN      VAPI_hca_hndl_t       hca_hndl
                         );


/* Protection Domain Verbs */

/*************************************************************************
 * Function: VAPI_alloc_pd
 *
 * Arguments:
 *  hca_hndl: Handle to HCA.
 *	pd_hndl_p: Pointer to Handle to Protection Domain object.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *  VAPI_EAGAIN: not enough resources.
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  					
 *  
 *  
 *
 * Description:
 *  
 *  This call register a new protection domain by calling VIP_Register_PD. Into the VIP 
 *  layer it is the responsibility of the PDA to keep track of the different Protection 
 *  Domains and the object associated to it.
 *  
 *  After registering the new allocated PD in the PDA. The VIP will call HH_register_PD. 
 *  Some HCA HW implementation do not keep track of any Protection Domain Object 
 *  internally turning the call to HH_register_PD into a dummy call.
 *  
 *  
 *  
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_alloc_pd(
                        IN      VAPI_hca_hndl_t       hca_hndl,
                        OUT     VAPI_pd_hndl_t       *pd_hndl_p
                        );


/*************************************************************************
 * Function: VAPI_dealloc_pd
 *
 * Arguments:
 *  hca_hndl: Handle to HCA.
 *	pd_hndl: Handle to Protection Domain Object.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: Invalid HCA handle.
 *  VAPI_EINVAL_PD_HNDL: Invalid Protection Domain
 *  VAPI_EBUSY: Protection Domain in use.
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *  
 *
 * Description:
 *  
 *  Deregister the Protection Domain from the PDA. The PDA is responsible to validate 
 *  that there are no objects associated to the Protection Domain being deallocated.
 *  
 *  After deregistering the allocated PD from the PDA the VIP will call 
 *  HH_deregister_PD. Some HCA HW implementation do not keep track of any Protec-
 *  tion Domain Object internally turning the call to HH_deregister_PD into a dummy call.
 *  
 *   
 *  
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_dealloc_pd(
                          IN      VAPI_hca_hndl_t       hca_hndl,
                          IN      VAPI_pd_hndl_t        pd_hndl
                          );


/* RD Are not supported at this rev */
#if 0

/* Reliable Datagram Domain Verbs */

/*************************************************************************
 * Function: VAPI_alloc_rdd
 *
 * Arguments:
 *  hca_hndl: HCA Handle.
 *	rdd_hndl_p: Pointer to Reliable Datagram Domain object handle.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *	VAPI_EAGAIN: out of resources.
 *  VAPI_EINVAL_RD_UNSUPPORTED: RD is not supported
 *  VAPI_EPERM: not enough permissions.
 *  
 *  
 *
 * Description:
 *  
 *  Allocates an RD domain
 *    
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_alloc_rdd (
                          IN      VAPI_hca_hndl_t        hca_hndl,
                          OUT     VAPI_rdd_hndl_t       *rdd_hndl_p
                          );


/*************************************************************************
 * Function: VAPI_dealloc_rdd
 *
 * Arguments:
 *  hca_hndl: HCA Handle.
 *  rdd_hndl : Reliable Datagram Domain object handle.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *	VAPI_EAGAIN: out of resources.
 *  VAPI_EINVAL_RD_UNSUPPORTED: RD is not supported
 *  VAPI_EPERM: not enough permissions.
 *  
 *  
 *
 * Description:
 *  
 *  DeAllocates an RD domain
 *    
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_dealloc_rdd(
                           IN      VAPI_hca_hndl_t       hca_hndl,
                           IN      VAPI_rdd_hndl_t       rdd_hndl
                           );
#endif

/*******************************************
 * 11.2.2 Address Management Verbs
 *
 *******************************************/
 /*************************************************************************
 * Function: VAPI_create_addr_hndl 
 *
 * Arguments:
 *  hca_hndl: Handle to HCA.
 *  pd_hndl: Protection domain handle	
 *	av_p: Pointer to Address Vector structure.
 *  av_hndl_p: Handle of Address Vector.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: Invalid HCA handle.
 *  VAPI_EINVAL_PD_HNDL: Invalid Protection Domain handle.
 *  VAPI_EAGAIN: Not enough resources.
 *  VAPI_EPERM:  Not enough permissions.
 *  VAPI_EINVAL_PARAM: Invalid parameter
 *  VAPI_EINVAL_PORT: Invalid port number
 *  
 *  
 *
 * Description:
 *  
 *  Creates a new Address Vector Handle that can be used  later when posting a WR to a 
 *  UD QP.
 *  The AVL (Address Vector Library) does the accounting of the different Address Vec-
 *  tor the user creates, and responses to the JOD when the last posts descriptors to the 
 *  device. The fields of the address vector are specified in the following  table:
 *  
  *************************************************************************/ 
VAPI_ret_t MT_API VAPI_create_addr_hndl(
                                IN      VAPI_hca_hndl_t       hca_hndl,
                                IN      VAPI_pd_hndl_t        pd_hndl,
                                IN      VAPI_ud_av_t             *av_p,
                                OUT     VAPI_ud_av_hndl_t        *av_hndl_p
                                );


/*************************************************************************
 * Function: VAPI_modify_addr_hndl 
 *
 * Arguments:
 *  hca_hndl: Handle to HCA.		
 *	av_hndl : Handle of Address Vector Handle
 *	av_p: Pointer to Address Vector structure.				
 *  
 *  
 *  
 *
 * Returns:
 *  VAPI_OK				
 *	VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *  VAPI_EINVAL_AV_HNDL: invalid Address Vector handle.
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_EINVAL_PORT: Invalid port number
 *  
 *  
 *  
 *
 * Description:
 *  
 *  Modify existent address vector handle to a new address vector. For address vector 
 *  fields, refer to Table 6, “VAPI_av_t,” on page 20.
 *  
 *  
 *  
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_modify_addr_hndl(
                                IN      VAPI_hca_hndl_t       hca_hndl,
                                IN      VAPI_ud_av_hndl_t        av_hndl,
                                IN      VAPI_ud_av_t             *av_p
                                );


/*************************************************************************
 * Function: VAPI_query_addr_hndl 
 *
 * Arguments:
 *  hca_hndl: Handle to HCA.		
 *  av_hndl : Handle of Address Vector 
 *  av_p: Pointer to Address Vector structure.				
 *  
 *  
 *  
 *
 * Returns:
 *  VAPI_OK				
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *  VAPI_EINVAL_AV_HNDL: invalid address vector handle.
 *  VAPI_EPERM:  not enough permission.
 *  
 *  
 *
 * Description:
 *  
 *  Returns pointer to ADDR_VECP with information of the UD Address Vector repre-
 *  sented by AddrVecHandle. For address vector fields, refer to Table 6, “VAPI_av_t,” on 
 *  page 20.
 *  
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_query_addr_hndl(
                               IN      VAPI_hca_hndl_t       hca_hndl,
                               IN      VAPI_ud_av_hndl_t        av_hndl,
                               OUT     VAPI_ud_av_t             *av_p
                               );


/*************************************************************************
 * Function: VAPI_destroy_addr_hndl 
 *
 * Arguments:
 *  hca_hndl: Handle to HCA.		
 *  av_hndl : Handle of Address Vector 
 *  						 		
 *
 * Returns:
 *  VAPI_OK				
 *	VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *	VAPI_EINVAL_AV_HNDL: invalid address vector handle.
 *  VAPI_EPERM:  not enough permission.
 *  
 *  
 *
 * Description:
 *  
 *  Removes address handle.
  *************************************************************************/ 
VAPI_ret_t MT_API VAPI_destroy_addr_hndl(
                                 IN      VAPI_hca_hndl_t       hca_hndl,
                                 IN      VAPI_ud_av_hndl_t        av_hndl
                                 );


/*******************************************
 * 11.2.3 Queue Pair
 *
 *******************************************/

 /*************************************************************************
 * Function: VAPI_create_qp 
 *
 * Arguments:
 *  hca_hndl : HCA Handle.
 *	qp_init_attr_p: Pointer to QP attribute to used for initialization.
 *	qp_hndl_p: Pointer to returned QP Handle number.
 *	qp_prop_p: Pointer to properties of created QP.
 *  						
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *	VAPI_EAGAIN: not enough resources.
 *	VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *	VAPI_EINVAL_CQ_HNDL: invalid CQ handle.
 *	VAPI_E2BIG_WR_NUM: number of WR exceeds HCA cap.
 *	VAPI_E2BIG_SG_NUM: number of SG exceeds HCA cap.
 *  VAPI_EINVAL_PD_HNDL: invalid protection domain handle.
 *  VAPI_EINVAL_SERVICE_TYPE: invalid service type for this QP.
 *  VAPI_EINVAL_RDD:	Invalid RD domain handle.
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *  
 *
 * Description:
 *   Create a QP resource (in the reset state).
 *  
  *************************************************************************/ 
VAPI_ret_t MT_API VAPI_create_qp(
                         IN      VAPI_hca_hndl_t       hca_hndl,
                         IN      VAPI_qp_init_attr_t  *qp_init_attr_p,
                         OUT     VAPI_qp_hndl_t       *qp_hndl_p,
                         OUT     VAPI_qp_prop_t       *qp_prop_p
                         );

 /*************************************************************************
 * Function: VAPI_create_qp_ext 
 *
 * Arguments:
 *  hca_hndl : HCA Handle.
 *  qp_init_attr_p: Pointer to QP attribute to used for initialization.
 *  qp_ext_attr_p: Extended QP attributes (take care to init. with VAPI_QP_INIT_ATTR_EXT_T_INIT)
 *  qp_hndl_p: Pointer to returned QP Handle number.
 *  qp_prop_p: Pointer to properties of created QP.
 *  						
 * Returns:
 *  VAPI_OK
 *  VAPI_EAGAIN: not enough resources.
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *  VAPI_EINVAL_CQ_HNDL: invalid CQ handle.
 *  VAPI_EINVAL_SRQ_HNDL: Given SRQ handle does not exist (when srq_handle!=VAPI_SRQ_INVAL_HNDL)
 *  VAPI_EINVAL_PD_HNDL: invalid protection domain handle 
 *    OR (When SRQ is associated with this QP and HCA requires SRQ's PD to be as QP's) 
 *                       Given PD is different than associated SRQ's.
 *  VAPI_E2BIG_WR_NUM: number of WR exceeds HCA cap.
 *  VAPI_E2BIG_SG_NUM: number of SG exceeds HCA cap.
 *  VAPI_EINVAL_SERVICE_TYPE: invalid service type for this QP.
 *  VAPI_EINVAL_RDD:	Invalid RD domain handle.
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 * Description:
 *   Create a QP resource in the reset state - extended version.
 *  
  *************************************************************************/ 
VAPI_ret_t MT_API VAPI_create_qp_ext(
                         IN      VAPI_hca_hndl_t       hca_hndl,
                         IN      VAPI_qp_init_attr_t  *qp_init_attr_p,
                         IN      VAPI_qp_init_attr_ext_t *qp_ext_attr_p,
                         OUT     VAPI_qp_hndl_t       *qp_hndl_p,
                         OUT     VAPI_qp_prop_t       *qp_prop_p
                         );


/*************************************************************************
 * Function: VAPI_modify_qp 
 *
 * Arguments:
 *  hca_hndl: HCA handle.
 *  qp_hndl: QP handle
 * 	qp_attr_p: Pointer to QP attributes to be modified.
 *  qp_attr_mask_p: Pointer to the attributes mask to be modified.
 *  qp_cap_p: Pointer to QP actual capabilities returned.
 *  					
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EAGAIN: out of resources.
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *  VAPI_EINVAL_QP_HNDL: invalid QP handle.
 *  VAPI_ENOSYS_ATTR: QP attribute is not supported.
 *  VAPI_EINVAL_ATTR: can not change QP attribute.
 *  VAPI_EINVAL_PKEY_IX: PKey index out of range.
 *  VAPI_EINVAL_PKEY_TBL_ENTRY: Pkey index points to an invalid entry in pkey table. 
 *  VAPI_EINVAL_QP_STATE: invalid QP state.
 *  VAPI_EINVAL_RDD_HNDL: invalid RDD domain handle.
 *  VAPI_EINVAL_MIG_STATE: invalid path migration state.
 *  VAPI_EINVAL_MTU: MTU exceeds HCA port capabilities
 *  VAPI_EINVAL_PORT: invalid port
 *  VAPI_EINVAL_SERVICE_TYPE: invalid service type
 *  VAPI_E2BIG_WR_NUM: maximum number of WR requested exceeds HCA capabilities
 *  VAPI_EINVAL_RNR_NAK_TIMER: invalid RNR NAK timer value
 *  VAPI_EINVAL_LOCAL_ACK_TIMEOUT: invalid Local ACK timeout value (either primary or alt)
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *  
 *
 * Description:
 *  
 *  Modify the QP attributes and transition into a new state. Note that only a subset of all 
 *  the attributes can be modified in during a certain transition into a QP state. The 
 *  qp_attr_mask_p specifies the actual attributes to be modified. 
 *  
 *  The QP attributes specified are of type VAPI_qp_attr_t and are specified in the follow-
 *  ing table:
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_modify_qp(
                         IN      VAPI_hca_hndl_t       hca_hndl,
                         IN      VAPI_qp_hndl_t        qp_hndl,
                         IN      VAPI_qp_attr_t       *qp_attr_p,
                         IN      VAPI_qp_attr_mask_t  *qp_attr_mask_p,
                         OUT     VAPI_qp_cap_t        *qp_cap_p
                         );


/*************************************************************************
 * Function: VAPI_query_qp
 *
 * Arguments:
 *  hca_hndl: HCA Handle.
 *  qp_hndl: QP Handle.
 *  qp_attr_p: Pointer to QP attributes.					
 *  qp_attr_mask_p: Pointer to QP attributes mask.					
 *  qp_init_attr_p: Pointer to init attributes
 *  
 * Returns:
 *  VAPI_OK
 * 	VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 * 	VAPI_EINVAL_QP_HNDL: invalid QP handle.
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *
 * Description:
 *  
 *  Returns a VAPI_qp_attr_t structure to the application with all the relevant information 
 *  that applies to the QP matching qp_hndl.
 *  Note that only the relevant fields in qp_attr_p and qp_init_attr_p are valid. The valid 
 *  fields in qp_attr_p are marked in the mask returned by qp_attr_mask_p.
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_query_qp(
                        IN      VAPI_hca_hndl_t       hca_hndl,
                        IN      VAPI_qp_hndl_t        qp_hndl,
                        OUT     VAPI_qp_attr_t       *qp_attr_p,
                        OUT     VAPI_qp_attr_mask_t  *qp_attr_mask_p,
                        OUT     VAPI_qp_init_attr_t  *qp_init_attr_p
                        );

/*************************************************************************
 * Function: VAPI_query_qp_ext
 *
 * Arguments:
 *  hca_hndl: HCA Handle.
 *  qp_hndl: QP Handle.
 *  qp_attr_p: Pointer to QP attributes.					
 *  qp_attr_mask_p: Pointer to QP attributes mask.					
 *  qp_init_attr_p: Pointer to init attributes
 *  qp_init_attr_ext_p: Pointer to extended init attributes
 *  
 * Returns:
 *  VAPI_OK
 * 	VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 * 	VAPI_EINVAL_QP_HNDL: invalid QP handle.
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *
 * Description:
 *   Same as VAPI_query_qp() but includes extended init. attributes.
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_query_qp_ext(                        
                        IN      VAPI_hca_hndl_t       hca_hndl,
                        IN      VAPI_qp_hndl_t        qp_hndl,
                        OUT     VAPI_qp_attr_t       *qp_attr_p,
                        OUT     VAPI_qp_attr_mask_t  *qp_attr_mask_p,
                        OUT     VAPI_qp_init_attr_t  *qp_init_attr_p,
                        OUT     VAPI_qp_init_attr_ext_t *qp_init_attr_ext_p
                        );

/*************************************************************************
 * Function: VAPI_destroy_qp
 *
 * Arguments:
 *  hca_hndl: HCA Handle.
 *  qp_hndl: QP Handle.
 *  					  
 * Returns:
 *  VAPI_OK
 * 	VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 * 	VAPI_EINVAL_QP_HNDL: invalid QP handle.
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_EBUSY: QP is in use
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *  
 *
 * Description:
 * releases all resources allocated by the CI to the qp

 *************************************************************************/
 VAPI_ret_t MT_API VAPI_destroy_qp(
                          IN      VAPI_hca_hndl_t       hca_hndl,
                          IN      VAPI_qp_hndl_t        qp_hndl
                          );


/*************************************************************************
 * Function: VAPI_get_special_qp
 *
 * Arguments:
 *  hca_hndl: HCA Handle.
 *  port: Physical port (valid only for QP0 and QP1
 *  qp: the qp type
 *  qp_init_attr_p: pointer to init attribute struct.
 *  qp_hndl: QP Handle.
 *  qp_cap_p: pointer to qp capabilities struct					
 *
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *  VAPI_EINVAL_QP_HNDL: invalid QP handle.
 *  VAPI_EINVAL_PORT: invalid port
 *  VAPI_EINVAL_PD_HNDL: invalid PD
 *  VAPI_EINVAL_CQ_HNDL: invalid CQ
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_EAGAIN: not enough resources
 *  VAPI_EGEN: general error
 *  VAPI_EINVAL_PARAM : invalid parameter
 *  VAPI_EBUSY: resource is busy/in-use
 *  VAPI_ENOSYS: not supported (legacy mode only)
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *
 * Description: this call creates a special qp that can generate MADs, 
 *              RAW IPV6 or ethertype packets
 *  
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_get_special_qp(
                              IN      VAPI_hca_hndl_t      hca_hndl,
                              IN      IB_port_t            port,
                              IN      VAPI_special_qp_t    qp,
                              IN      VAPI_qp_init_attr_t *qp_init_attr_p,
                              OUT     VAPI_qp_hndl_t      *qp_hndl_p,
                              OUT     VAPI_qp_cap_t       *qp_cap_p
                              );



/*******************************************
 * Shared Receive Queue (SRQ)
 *
 *******************************************/
/*************************************************************************
 * Function: VAPI_create_srq 
 *
 * Arguments:
 *  hca_hndl    : HCA Handle.
 *  srq_attr_p : Requested SRQ's attributes
 *  srq_hndl_p  : Returned SRQ handle
 *  actual_attr_p : Actual SRQ attributes 
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *  VAPI_EINVAL_PD_HNDL: invalid protection domain handle.
 *  VAPI_E2BIG_WR_NUM: max_outs_wr exceeds HCA cap.
 *  VAPI_E2BIG_SG_NUM: max_sentries exceeds HCA cap.
 *  VAPI_EAGAIN: not enough resources.
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: Kernel trap (IOCTL/system-call) failure.
 *  VAPI_ENOSYS: HCA does not support SRQs
 *  
 * Description:
 *   Create a shared RQ with given attributes.
 *  
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_create_srq(
                         IN      VAPI_hca_hndl_t    hca_hndl,
                         IN      VAPI_srq_attr_t  *srq_props_p,
                         OUT     VAPI_srq_hndl_t  *srq_hndl_p,
                         OUT     VAPI_srq_attr_t  *actual_srq_props_p
                         );
                         

/*************************************************************************
 * Function: VAPI_query_srq 
 *
 * Arguments:
 *  hca_hndl        : HCA Handle.
 *  srq_hndl        : SRQ to query
 *  srq_attr_p      : Returned SRQ attributes
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *  VAPI_EINVAL_SRQ_HNDL: invalid SRQ handle.
 *  VAPI_ESRQ: SRQ in error state
 *  VAPI_ESYSCALL: Kernel trap (IOCTL/system-call) failure.
 *  VAPI_ENOSYS: HCA does not support SRQs
 *  
 * Description:
 *   Query a shared RQ.
 *  
  *************************************************************************/ 
VAPI_ret_t MT_API VAPI_query_srq(
                         IN      VAPI_hca_hndl_t   hca_hndl,
                         IN      VAPI_srq_hndl_t   srq_hndl,
                         OUT     VAPI_srq_attr_t   *srq_attr_p
			 );
			 

/*************************************************************************
 * Function: VAPI_modify_srq
 *
 * Arguments:
 *  hca_hndl    : HCA Handle.
 *  srq_hndl    : SRQ to modify
 *  srq_attr_p  : Requested SRQ's new attributes
 *  srq_attr_mask : Mask of valid attributes in *srq_attr_p (attr. to modify)
 *  actual_attr_p : Actual SRQ attributes 
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *  VAPI_EINVAL_SRQ_HNDL: invalid SRQ handle.
 *  VAPI_E2BIG_WR_NUM: max_outs_wr exceeds HCA cap. 
 *                     OR smaller than number of currently outstanding WQEs
 *  VAPI_E2BIG_SRQ_LIMIT : Requested SRQ limit is larger than actual new size 
 *  VAPI_ESRQ: SRQ in error state
 *  VAPI_EAGAIN: not enough resources.
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: Kernel trap (IOCTL/system-call) failure.
 *  VAPI_ENOSYS: HCA does not support requested SRQ modifications.
 *  
 * Description:
 *   Modify a shared RQ with given attributes.
 *  
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_modify_srq(
                         IN      VAPI_hca_hndl_t    hca_hndl,
                         IN      VAPI_srq_hndl_t   srq_hndl,
                         IN      VAPI_srq_attr_t  *srq_attr_p,
                         IN      VAPI_srq_attr_mask_t srq_attr_mask,
                         OUT     u_int32_t        *max_outs_wr_p 
			 );


/*************************************************************************
 * Function: VAPI_destroy_srq 
 *
 * Arguments:
 *  hca_hndl : HCA Handle.
 *  srq_hndl : SRQ to destroy
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *  VAPI_EINVAL_SRQ_HNDL: invalid SRQ handle.
 *  VAPI_EBUSY: SRQ still has QPs associated with it.
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: Kernel trap (IOCTL/system-call) failure.
 *  VAPI_ENOSYS: HCA does not support SRQs
 *  
 * Description:
 *   Destroy a shared RQ.
 *  
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_destroy_srq(
                         IN      VAPI_hca_hndl_t    hca_hndl,
                         IN      VAPI_srq_hndl_t   srq_hndl
			 );


/************************************************************************
 * Function: VAPI_post_srq
 *
 * Arguments:
 *  hca_hndl : HCA Handle.
 *  srq_hndl : SRQ Handle.
 *  rwqe_num : Number of posted receive WQEs
 *  rwqe_array: Pointer to an array of rwqe_num receive work requests.
 *  rwqe_posted_p: Returned actual number of posted WQEs.
 *  
 * returns: 
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_SRQ_HNDL: invalid SRQ handle
 *  VAPI_E2BIG_WR_NUM: Too many posted work requests.
 *  VAPI_EINVAL_SG_NUM: invalid scatter list length
 *  VAPI_EINVAL_OP: invalid operation
 *  VAPI_EPERM: not enough permissions.
 *  
 * Description:
 *  Post a receive request descriptor to the shared receive queue.
 *  An error refers only to the first WQE that was not posted (index *rwqe_posted_p).
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_post_srq(
                       IN VAPI_hca_hndl_t       hca_hndl,
                       IN VAPI_srq_hndl_t       srq_hndl,
                       IN u_int32_t             rwqe_num,
                       IN VAPI_rr_desc_t       *rwqe_array,
                       OUT u_int32_t           *rwqe_posted_p);




/*******************************************
 * 11.2.5 Compeletion Queue
 *
 *******************************************/
/*************************************************************************
 * Function: VAPI_create_cq
 *
 * Arguments:
 *  hca_hndl: HCA Handle.
 *  cqe_num: Minimum required number of entries in CQ.
 *  cq_hndl_p: Pointer to the created CQ handle
 *  num_of_entries_p: 	Actual number of entries in CQ
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 * 	VAPI_EAGAIN: out of resources
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_E2BIG_CQ_NUM: number of entries in CQ exceeds HCA 
 *       capabilities										
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *  
 *
 * Description:
 *  
 *  Allocate the required data structures for the administration of a completion queue 
 *  including completion queue buffer space which has to be large enough to be adequate to 
 *  the maximum number of entries in the completion.
 *  
 *  Completion queue entries are accessed directly by the application.
 *  
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_create_cq(
                         IN  VAPI_hca_hndl_t         hca_hndl,   
                         IN  VAPI_cqe_num_t          cqe_num,
                         OUT VAPI_cq_hndl_t          *cq_hndl_p,
                         OUT VAPI_cqe_num_t          *num_of_entries_p
                         );           


/*************************************************************************
 * Function: VAPI_query_cq
 *
 * Arguments:
 *  hca_hndl:		 HCA handle.
 *  cq_hndl: Completion Queue Handle.
 *  num_of_entries_p: Pointer to actual number of entries in CQ.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_CQ_HNDL: invalid CQ handle. 
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *  
 *
 * Description:
 *  
 *  Retrieves the number of  entries in the CQ.
 *  
 *  
 *  
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_query_cq(
                        IN  VAPI_hca_hndl_t          hca_hndl,
                        IN  VAPI_cq_hndl_t           cq_hndl,
                        OUT VAPI_cqe_num_t          *num_of_entries_p
                        );           


/*************************************************************************
 * Function: VAPI_resize_cq
 *
 * Arguments:
 *  hca_hndl: HCA Handle.
 *  cq_hndl: CQ Handle.
 *  cqe_num: Minimum entries required in resized CQ.
 *  num_of_entries_p: Pointer to actual number of entries in resized CQ.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EAGAIN: out of resources
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_CQ_HNDL: invalid CQ handle or the given CQ is in invalid state to resize (CQ error). 
 *  VAPI_E2BIG_CQ_NUM: number of entries in CQ exceeds HCA 
 *       capabilities or number of currently outstanding entries 
 *       in CQ exceeds required size. 
 *  VAPI_EBUSY: Another VAPI_resize_cq invocation for the same CQ is in progress
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *              
 *
 * Description:
 *  Resize given CQ.
 *  Number of curretly outstanding CQEs in the CQ should be no more than the size of the new CQ.
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_resize_cq(
                         IN  VAPI_hca_hndl_t         hca_hndl,
                         IN  VAPI_cq_hndl_t          cq_hndl,
                         IN  VAPI_cqe_num_t          cqe_num,
                         OUT VAPI_cqe_num_t          *num_of_entries_p
                         );


/*************************************************************************
 * Function: VAPI_destroy_cq
 *
 * Arguments:
 *  hca_hndl: HCA Handle.
 *  cq_hndl: CQ Handle.
 *
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 * 	VAPI_EINVAL_CQ_HNDL: invalid CQ handle. 
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_EBUSY.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *  
 *
 * Description:
 * destroys cq and releases all resources associated to it. 
 *
 *  
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_destroy_cq(
                          IN  VAPI_hca_hndl_t         hca_hndl,
                          IN  VAPI_cq_hndl_t          cq_hndl
                          );



/*******************************************
 *  11.2.6 EE Context
 *
 *******************************************/
/************ EEC is not supported on this revision ********************/
#if 0

/*************************************************************************
 * Function: VAPI_create_eec
 *
 * Arguments:
 *  hca_hndl: HCA Handle. 
 *  rdd: RD domain.
 *  eec_hndl_p: Pointer to EE returned Context Handle.  
 *  
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EAGAIN: out of resources
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle 
 *  VAPI_EPERM: not enough permissions.
 *  
 *  
 *
 * Description: creates an ee context
 *  
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_create_eec(
                          IN      VAPI_hca_hndl_t     hca_hndl,
                          IN      VAPI_rdd_t              rdd,
                          OUT     VAPI_eec_hndl_t     *eec_hndl_p
                          );


/*************************************************************************
 * Function: VAPI_modify_eec_attr
 *
 * Arguments:
 *  hca_hndl:	 HCA Handle.
 *  eec_hndl: EE Context Handle
 *  eec_attr_p: Pointer to EE Context Attributes Structure.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EAGAIN: out of resources
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_EEC_HNDL: invalid EEC handle
 *  CANNOT_CHANGE_EE_CONTEXT_ATTR
 *  VAPI_EINVAL_EEC_STATE: invalid EEC state
 *  VAPI_EINVAL_RDD: invalid RD domain
 *  INVALID_CHANNEL_MIGRATION_STATE
 *  VAPI_EPERM: not enough permissions.
 *  
 *  
 *
 * Description: modifies ee attributes
 *  
 *  
 *  
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_modify_eec_attr(
                               IN      VAPI_hca_hndl_t     hca_hndl,
                               IN      VAPI_eec_hndl_t     eec_hndl,
                               IN      VAPI_eec_attr_t    *eec_attr_p    
                                );  


/*************************************************************************
 * Function: VAPI_query_eec_attr 
 *
 * Arguments:
 *  hca_hndl: HCA Handle.
 *  eec_hndl: EE context handle.
 *  eec_attr_p: Pointer to EE Context Attributes Structure.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_EEC_HNDL: invalid EEC handle
 *  VAPI_EPERM: not enough permissions.
 *  
 *  
 *
 * Description: submits a query on eec attributes
 *  
 *  
 *  
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_query_eec_attr(
                              IN      VAPI_hca_hndl_t    hca_hndl,
                              IN      VAPI_eec_hndl_t    eec_hndl,
                              OUT     VAPI_eec_attr_t    *eec_attr_p
                              );
 

/*************************************************************************
 * Function: VAPI_destroy_eec 
 *
 * Arguments:
 *  hca_hndl: HCA Handle.
 *  eec_hndl: EE context handle.
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_EEC_HNDL: invalid EEC handle
 *  VAPI_EPERM: not enough permissions.
 *  
 *  
 *
 * Description: destroys an eec context
 *  
 *  
 *  
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_destroy_eec  (
                              IN      VAPI_hca_hndl_t    hca_hndl,
                              IN      VAPI_eec_hndl_t    eec_hndl,
                             );

#endif

/*******************************************
 * 11.2.7 Memory Managemnet
 *
 *******************************************/


/*************************************************************************
 * Function: VAPI_register_mr
 *
 * Arguments:
 *  hca_hndl :	HCA Handle.
 *  req_mrw_p: Pointer to the requested memory region properties.
 *  mr_hndl_p: Pointer to the memory region handle.
 *  rep_mrw: Pointer to the responded memory region properties.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EAGAIN: out of resources
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_PD_HNDL: invalid PD handle
 *  VAPI_EINVAL_VA: invalid virtual address
 *  VAPI_EINVAL_LEN: invalid length
 *  VAPI_EINVAL_ACL: invalid ACL specifier (remote write or atomic , without local write)
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *  
 *
 * Description:
 *  
 *  The MMU administrates a list of memory regions/windows. The current version of the 
 *  VIP supports only pinned buffers. In the future an extension of the MMU a support for 
 *  pageable buffer will be considered.
 *  
 *  Memory Translation and protection tables are not store on the VIP but at the device 
 *  driver due to their device specific orientation.
 *  
 *  The caller should fill the req_mrw_p structure fields with the type(VAPI_MR, VAPI_MPR), virtual start 
 *  address, size, protection domain handle (pd_hndl) and the access control list (acl). 
 *  for registration of physical mr, the caller should also fill the fields iova offset (offset 
 *  of virt. start adrs from page start), pbuf_list_p (list of physical buffers) and pbuf_list_len. 
 *
 *  Upon successfull completion, the rep_mrw_p will include the l_key, the r_key (if 
 *  remote access was requested). The memory region handle is returned in mr_hndl_p. 
 *  VAPI_mr_t  is described in the following table: 
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_register_mr(
                           IN  VAPI_hca_hndl_t  hca_hndl,
                           IN  VAPI_mr_t       *req_mrw_p,
                           OUT VAPI_mr_hndl_t  *mr_hndl_p,
                           OUT VAPI_mr_t       *rep_mrw_p
                           );



/*************************************************************************
 * Function: VAPI_query_mr
 *
 * Arguments:
 *  hca_hndl: HCA handle.
 *  mr_hndl: Memory Region Handle.
 *  rep_mrw_p: Pointer to Memory Region Attributes 
 *  remote_start_p: Pointer to the remotly start address returned value
 *  remote_size_p: Pointer to the remotely size of the region returned 
 *                 value
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_MR_HNDL: invalid Memory Region handle
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *  
 *
 * Description:
 *  
 *  Queries a memory region handle and returns a VAPI_mr_t. Upon successful comple-
 *  tion, the structure includes all the memory region properties: protection domain handle, 
 *  ACL, LKey, RKey and actual protection bounds. The protection bounds returned in 
 *  rep_mrw_p are the local protection bounds enforced by the HCA. The remote protec-
 *  tion bounds are returned in remote_start_p and remote_size_p and are valid only 
 *  when remote access was requested.
 *  
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_query_mr(
                        IN  VAPI_hca_hndl_t      hca_hndl,
                        IN  VAPI_mr_hndl_t       mr_hndl,
                        OUT VAPI_mr_t           *rep_mrw_p,
                        OUT VAPI_virt_addr_t    *remote_start_p,
                        OUT VAPI_virt_addr_t    *remote_size_p
                        );


/*************************************************************************
 * Function: VAPI_deregister_mr
 *
 * Arguments:
 *  hca_hndl: HCA handle.
 *  mr_hndl: Memory Region Handle
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_MR_HNDL: invalid memory region handle
 *  VAPI_EBUSY: memory region still has bound window(s)
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *  
 *
 * Description:
 *  
 *  Destroy a registered memory region. The memory region deregistering has to be invali-
 *  dated from the CI. 
 *  
 *  It is the roll of the MMU to validate that there are no bounded memory windows in 
 *  order to allow the de-registration of the memory region.
 *  
 *  After the deregistration takes place is under the scope of the MMU to unpin all those 
 *  memory pages that were not pinned before the memory registration was done.
 *  
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_deregister_mr(
                             IN VAPI_hca_hndl_t      hca_hndl,
                             IN VAPI_mr_hndl_t       mr_hndl
                             );


/*************************************************************************
 * Function: VAPI_reregister_mr 
 *
 * Arguments:
 *  hca_hndl: HCA Handle.
 *  mr_hndl: Old Memory Region Handle.
 *  change_type: requested change type.
 *  req_mrw_p: Pointer to the requested memory region properties.
 *  rep_mr_hndl_p: Pointer to the returned new memory region handle
 *  rep_mrw_p: Pointer to the returned memory region properties.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EAGAIN: out of resources
 *  VAPI_EINVAL_PARAM: invalid change type
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_MR_HNDL: invalid memory region handle
 *  VAPI_EINVAL_VA: invalid virtual address
 *  VAPI_EINVAL_LEN: invalid length
 *  VAPI_EINVAL_PD_HNDL: invalid protection domain handle
 *  VAPI_EINVAL_ACL: invalid ACL specifier
 *  VAPI_EBUSY: memory region still has bound window(s)
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *  
 *  
 *
 * Description:
 *  
 *  Reregisters the memory region associated with the mr_hndl. The changes to be applied 
 *  to the memory region are any combination of the following three flags, specified in the 
 *  change_type input modifier:
 *  
 *  MR_CHANGE_TRANS - Change translation. The req_mr_p should contain the 
 *  new start and size of the region as well as the new mr type:in mr_type (VAPI_MR,VAPI_MSHAR, VAPI_MPR ).
 *  
 *  MR_CHANGE_PD - Change the PD associated with this region. The req_mr_p 
 *  should contain the new PD.
 *  
 *  MR_CHANGE_ACL - Change the ACL. The req_mr_p should contain the new 
 *  ACL for this region.
 *  
 *  for registration of physical mr, the caller should also fill the fields iova offset (offset 
 *  of virt. start adrs from page start), pbuf_list_p (list of physical buffers) and pbuf_list_len. 
 * 
 *  Upon successful completion, the verb returns the new handle for this memory region, 
 *  which may or may be not identical to the original one, but must be used for further ref-
 *  erences to this region. The LKey and the RKey (only when remote access permission 
 *  was granted) are returned in the rep_mr_p.
 *  
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_reregister_mr(
                             IN  VAPI_hca_hndl_t       hca_hndl,
                             IN  VAPI_mr_hndl_t        mr_hndl,
                             IN  VAPI_mr_change_t      change_type,
                             IN  VAPI_mr_t            *req_mrw_p,
                             OUT VAPI_mr_hndl_t       *rep_mr_hndl_p,
                             OUT VAPI_mr_t            *rep_mrw_p
                             );


/*************************************************************************
 * Function: VAPI_register_smr
 *
 * Arguments:
 *  hca_hndl : HCA handle.
 *  orig_mr_hndl: Original memory region handle.
 *  req_mrw_p: Pointer to the requested memory region properties (valid fields:pd,ACL,start virt adrs)
 *  mr_hndl_p: Pointer to the responded memory region handle.
 *  rep_mrw: Pointer to the responded memory region properties.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EAGAIN: out of resources
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_VA: invalid virtual address
 *  VAPI_EINVAL_MR_HNDL: invlalid MR handle
 *  VAPI_EINVAL_PD_HNDL: invalid PD handle
 *  VAPI_EINVAL_ACL: invalid ACL specifier
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *  
 *
 * Description:
 *  
 *  Registers a shared memory region associated with the same physical buffers of an exist-
 *  ing memory region referenced by orig_mr_hndl. The req_mrw_p is a pointer to the 
 *  requested memory region properties.the struct should contain the 
 *  requested start virtual address (start field), the protection domain handle and the ACL.
 *  
 *  Upon successful completion, the new memory region handle is returned in mr_hndl_p 
 *  and a struct rep_mrw_p of type VAPI_mr_t contains the actually assigned virtual 
 *  address (start field), the LKey and the RKey (only if remote access rights were 
 *  requested).
 *  
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_register_smr(
                            IN  VAPI_hca_hndl_t      hca_hndl,
                            IN  VAPI_mr_hndl_t       orig_mr_hndl,
                            IN  VAPI_mr_t           *req_mrw_p,
                            OUT VAPI_mr_hndl_t      *mr_hndl_p,
                            OUT VAPI_mr_t           *rep_mrw_p
                            );




/*************************************************************************
 * Function: VAPI_alloc_mw 
 *
 * Arguments:
 *  hca_hnd: HCA Handle.
 *  pd_hndl: Protection Domain Handle.
 *  mw_hndl_p: Pointer to new allocated windows handle.
 *  rkey_p:  Pointer to Windows unbounded Rkey
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EAGAIN
 *  VAPI_EINVAL_HCA_HNDL
 *  VAPI_EINVAL_PD_HNDL
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *  
 *
 * Description:
 *  
 *  Allocate a MWO object than can be later bound to an RKey.
 *  
 *  The MMU will validate that there enough resources available for this allocations.
 *  
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_alloc_mw(
                        IN      VAPI_hca_hndl_t     hca_hndl,
                        IN      VAPI_pd_hndl_t      pd,
                        OUT     VAPI_mw_hndl_t      *mw_hndl_p,
                        OUT     VAPI_rkey_t         *rkey_p
                        );


/*************************************************************************
 * Function: VAPI_query_mw
 *
 * Arguments:
 *  hca_window: HCA Handle.
 *  mw_hndl: Windows Handle.
 *  r_key_p: pointer to Rkey of Window.
 *  pd: pointer to rotection Domain Handle of Window.
 *  
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL
 *  VAPI_EINVAL_MW_HNDL
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *  
 *
 * Description:
 *  
 *  This call will return the current PD associated with the memory domain which will be 
 *  retrieved from the PDA (no access to HW required).
 *  
 *  
 *  
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_query_mw(
                        IN      VAPI_hca_hndl_t     hca_hndl,
                        IN      VAPI_mw_hndl_t      mw_hndl,
                        OUT     VAPI_rkey_t         *rkey_p,
                        OUT     VAPI_pd_hndl_t      *pd_p
                        );


/*************************************************************************
 * Function: VAPI_bind_mw
 *
 * Arguments:
 *  hca_hndl: HCA Handle.
 *  mw_hndl: Handle of memory windows.
 *  bind_prop_p: Binding properties.
 *  qp: QP to use for posting this binding request 
 *  id: Work request ID to be used in this binding request 
 *  comp_type Create CQE or not (for QPs set to singaling per request) 
 *  new_rkey_p: pointer to RKey of bound windows.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL
 *  VAPI_EINVAL_MW_HNDL
 *  VAPI_EINVAL_PARAM
 *  VAPI_EAGAIN
 *
 *  
 *
 * Description:
 *  
 *  This called is performed completely in user mode. The posted descriptor will return on 
 *  completion an RKey that can be used in subsequent remote access to the bounded mem-
 *  ory region.
 *  
 *  Success of the operation is receive through any one of the immediate errors specified 
 *  about or through the completion entry of the bind windows operation.
 *  
 *  The VAPI_bind_mw call is equivalent to the posting of descriptors. The implication of 
 *  this is that both the MMU and the JOD will have to be involved in this call.
 *  
 *  
 *  
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_bind_mw(
  IN    VAPI_hca_hndl_t        hca_hndl,
  IN    VAPI_mw_hndl_t         mw_hndl,
  IN    const VAPI_mw_bind_t*  bind_prop_p,
  IN    VAPI_qp_hndl_t         qp,
  IN    VAPI_wr_id_t           id,
  IN    VAPI_comp_type_t       comp_type,
  /* IN    MT_bool                fence, - This should be added in order to be IB 1.1 compliant */
  OUT   VAPI_rkey_t*           new_rkey_p
  );


/*************************************************************************
 * Function: VAPI_dealloc_mw 
 *
 * Arguments:
 *  hca_hnd: HCA Handle.
 *  mw_hndl: New allocated windows handle.
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EAGAIN
 *  VAPI_EINVAL_HCA_HNDL
 *  VAPI_EINVAL_MW_HNDL
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *  
 *
 * Description:
 *  
 *  DeAllocate a MWO object.  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_dealloc_mw(
                          IN      VAPI_hca_hndl_t     hca_hndl,
                          IN      VAPI_mw_hndl_t      mw_hndl
                          );

/*******************************************
 *   11.3 Multicast Group
 *******************************************/


 /*************************************************************************
 * Function: VAPI_attach_to_multicast
 *
 * Arguments:
 *  hca_hndl:  HCA Handle.
 *  mcg_dgid:  gid address of multicast group
 *  qp_hndl:   QP Handle
 *  mcg_lid:   lid of MCG. Currently ignored
 * 
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EAGAIN              - Insufficient resources to complete request
 *  VAPI_E2BIG_MCG_SIZE      - Number of QPs attached to multicast groups exceeded.
 *  VAPI_EINVAL_MCG_GID      - Invalid multicast DGID
 *  VAPI_EINVAL_QP_HNDL      - Invalid QP handle
 *  VAPI_EINVAL_HCA_HNDL     - Invalid HCA handle
 *  VAPI_EINVAL_SERVICE_TYPE - Invalid Service Type for this QP.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *
 * Description:
 *  
 *  Attaches qp to multicast group..
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_attach_to_multicast(
                                IN      VAPI_hca_hndl_t     hca_hndl,
                                IN      IB_gid_t            mcg_dgid,
                                IN      VAPI_qp_hndl_t      qp_hndl,
                                IN      IB_lid_t            mcg_dlid);


/*************************************************************************
 * Function: VAPI_detach_from_multicast
 *
 * Arguments:
 *  hca_hndl: HCA Handle.
 *  mcg_dgid: multicast group -GID
 *  qp_hndl:  QP Handle
 *  mcg_dlid: DLID of MCG. Currently ignored
 *  
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL     - Invalid HCA handle
 *  VAPI_EINVAL_MCG_GID      - Invalid multicast DGID
 *  VAPI_EINVAL_QP_HNDL      - Invalid QP handle
 *  VAPI_EINVAL_SERVICE_TYPE - Invalid Service Type for this QP.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 * 
 *  
 *
 * Description:
 *  
 *  Detaches qp from multicast group..
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_detach_from_multicast(
                                IN      VAPI_hca_hndl_t     hca_hndl,
                                IN      IB_gid_t            mcg_dgid,
                                IN      VAPI_qp_hndl_t      qp_hndl,
                                IN      IB_lid_t            mcg_dlid);
                               
/*******************************************
 *  11.4 Work Request Processing
 *******************************************/

/* Queue Pair Operations */

/* *************************************************************************
 * Function: VAPI_post_sr
 *
 * Arguments:
 *  hca_hndl	: HCA Handle.
 *  qp_hndl: QP Handle.
 *  sr_desc_p: Pointer to the send request descriptor attributes structure.
 *  
 *  
 * Returns:
 *  VAPI_OK
 *	VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *	VAPI_EINVAL_QP_HNDL: invalid QP handle
 *  VAPI_E2BIG_WR_NUM: Too many posted work requests.
 *  VAPI_EINVAL_OP: invalid operation
 *  VAPI_EINVAL_QP_STATE: invlaid QP state
 *  VAPI_EINVAL_SG_FMT: invalid scatter/gather list format
 *  VAPI_EINVAL_SG_NUM: invalid scatter/gather list length
 *  VAPI_EINVAL_AH: invalid address handle
 *  VAPI_EPERM: not enough permissions.
 *  
 * Description:
 *  The verb posts a send queue work request, the properties of which are specified in the 
 *  structure pointed by sr_desc_p, which is of type VAPI_sr_desc_t:
 *  The sg_lst_p points to a gather list, the length of which is sg_lst_len, which is an array 
 *  of local buffers used as the source of the data to be transmited in this Work Request. 
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_post_sr(
                       IN VAPI_hca_hndl_t       hca_hndl,
                       IN VAPI_qp_hndl_t        qp_hndl,
                       IN VAPI_sr_desc_t       *sr_desc_p
                       );


/************************************************************************
 * Function: VAPI_post_rr
 *
 * Arguments:
 *  hca_hndl	: HCA Handle.
 *  qp_hndl: QP Handle.
 *  rr_desc_p: Pointer to the receive request descriptor attributes structure.
 *  
 * returns: 
 *  VAPI_OK
 * 	VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 * 	VAPI_EINVAL_QP_HNDL: invalid QP handle
 *  VAPI_EINVAL_SRQ_HNDL: QP handle used for a QP associted with a SRQ (use VAPI_post_srq)
 *  VAPI_E2BIG_WR_NUM: Too many posted work requests.
 *  VAPI_EINVAL_OP: invalid operation
 *  VAPI_EINVAL_QP_STATE: invlaid QP state
 *  VAPI_EINVAL_SG_NUM: invalid scatter/gather list length
 *  VAPI_EPERM: not enough permissions.
 *  
 *  
 *
 * Description:
 *  
 *  The verb posts a receive request descriptor to the receive queue.
 */  
 VAPI_ret_t MT_API VAPI_post_rr(
                       IN VAPI_hca_hndl_t       hca_hndl,
                       IN VAPI_qp_hndl_t        qp_hndl,
                       IN VAPI_rr_desc_t       *rr_desc_p
                       );

/* Completion Queue Operations */


/*************************************************************************
 * Function: VAPI_poll_cq
 *
 * Arguments:
 *  hca_hndl: Handle to HCA.
 *  cq_hndl: CQ Handle.
 *  wc_desc_p: Pointer to work completion descriptor structure.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_CQ_HNDL: invalid CQ handle
 *  VAPI_CQ_EMPTY: CQ is empty 
 *  VAPI_EPERM: not enough permissions.
 *
 * Description:
 *  
 *  This call will retrieve an ICQE (Independent Completion Queue Entry), which is a 
 *  device independent structure used to retrieve completion status of WR posted to the 
 *  Send/Receive Queue including VAPI_bind_mw. 
 *  
 *  The verb retrieves a completion queue entry into the descriptor pointed by wc_desc_p 
 *  which is of type VAPI_wc_desc_t and described in the following table:
 *  
 *  
 *  
 *  The remote_node_address is of type VAPI_remote_node_addr_t and is valid only for 
 *  Datagram services. 
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_poll_cq(
                       IN  VAPI_hca_hndl_t      hca_hndl,
                       IN  VAPI_cq_hndl_t       cq_hndl,
                       OUT VAPI_wc_desc_t      *comp_desc_p
                       );


/*************************************************************************
 * Function: VAPI_req_comp_notif
 *
 * Arguments:
 *  hca_hndl: Handle to HCA.
 *  cq_hndl: CQ Handle.
 *  notif_type: CQ Notification type.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_CQ_HNDL: invalid CQ handle
 *  VAPI_EINVAL_NOTIF_TYPE: invalid notify type
 *  VAPI_EPERM: not enough permissions.
 *  
 *  
 *
 * Description:
 *  
 * the verb request a type specified in notif_type.  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API VAPI_req_comp_notif(
                              IN  VAPI_hca_hndl_t         hca_hndl,
                              IN  VAPI_cq_hndl_t          cq_hndl,
                              IN  VAPI_cq_notif_type_t    notif_type
                              );

/*  TK - only later #ifdef __KERNEL__ */

/*******************************************
 *  11.5 Event Handling - the global functions exposed only to kernel modules 
 *  See evapi.h for the user level functions
 *******************************************/
/*************************************************************************
 * Function: VAPI_set_comp_event_handler (kernel space only)
 *
 * Arguments:
 *  hca_hndl: HCA Handle 
 *  handler: Completion Event Handler function address.
 *  private_data: Pointer to handler context (handler specific).
 *  
 *  
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EPERM: not enough permissions.
 *  
 *  
 *
 * Description:
 *  
 *  Registers a completion event handler. Only one CQ event handler can be registered per 
 *  HCA.
 *  
 *  Exposed only to kernel modules
 *
 *  The CQ event handler function prototype is as follows:
 *  
 *  void
 *  VAPI_completion_event_handler
 *  (
 *    IN	VAPI_hca_hndl_t 	hca_hndl,
 *    IN	VAPI_cq_hndl_t 	cq_hndl,
 *    IN	void 		     *private_data
 *  )
 *  
 *  
 *  
 *  
 *
 *************************************************************************/ 
#ifdef __KERNEL__
VAPI_ret_t MT_API VAPI_set_comp_event_handler(
                                      IN VAPI_hca_hndl_t                  hca_hndl,
                                      IN VAPI_completion_event_handler_t  handler,
                                      IN void* private_data
                                      );
#endif

/*************************************************************************
 * Function: VAPI_set_async_event_handler (kernel space only)
 *
 * Arguments:
 *  hca_hndl: HCA Handle 
 *  handler: Async Event Handler function address.
 *  private_data: Pointer to handler context (handler specific).
 *  
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EPERM: not enough permissions.
 *  
 *  
 *
 * Description:
 *  
 *  Registers an async event handler.  
 *  Exposed only to kernel modules
 *
 *  The event handler function prototype is as follows:
 *  
 *  void
 *  VAPI_async_event_handler
 *  (
 *    IN	VAPI_hca_hndl_t 	hca_hndl,
 *    IN	VAPI_event_record_t     *event_record_p,
 *    IN	void 		     *private_data
 *  )
 *  
 *
 *************************************************************************/ 
#ifdef __KERNEL__
VAPI_ret_t MT_API VAPI_set_async_event_handler(
                                       IN VAPI_hca_hndl_t                  hca_hndl,
                                       IN VAPI_async_event_handler_t       handler,
                                       IN void* private_data
                                       );
#endif /* KERNEL */

#endif /*H_VAPI_H*/

