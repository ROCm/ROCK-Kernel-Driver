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
 
#ifndef H_VIP_DEVMM_H
#define H_VIP_DEVMM_H


#include <mtl_common.h>
#include <vapi.h>
#include <vip.h>
#include <hh.h>
#include <VIP_rsct.h>

#define DEVMM_INVAL_HNDL VAPI_INVAL_HNDL

/*************************************************************************
 * Function: DEVMM_new
 *
 * Arguments:
 *  hca_hndl (IN) - HCA for which this DEVMM is created
 *  devmm_p (OUT) - Pointer to DEVMM_obj_handle_t to return new obj_hndl instance in
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_HCA_HNDL: invalid HCA handle.
 *  VIP_EAGAIN: not enough resources
 *
 * Description:
 *   Creates new DEVMM object for new HOB.
 *
 *************************************************************************/ 
VIP_ret_t DEVMM_new( HOBKL_hndl_t hca_hndl, DEVMM_hndl_t *devmm_p);


/*************************************************************************
 * Function: DEVMM_delete
 *
 * Arguments:
 *  devmm (IN) - DEVMM object to destroy
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EBUSY: there are still chunks of device memory allocated (and force  == false )
 *
 * Description:
 *   Cleanup resources of given DEVMM object.
 *
 *************************************************************************/ 
VIP_ret_t DEVMM_delete(DEVMM_hndl_t devmm);

 VIP_ret_t DEVMM_alloc_map_devmem(VIP_RSCT_t usr_ctx,DEVMM_hndl_t devmm, EVAPI_devmem_type_t mem_type, 
                              VAPI_size_t  bsize,u_int8_t align_shift,
                              VAPI_phy_addr_t* buf_p,void ** virt_addr_p,
                              DEVMM_dm_hndl_t* dm_p);

 
 VIP_ret_t DEVMM_query_devmem(VIP_RSCT_t usr_ctx,DEVMM_hndl_t devmm, EVAPI_devmem_type_t mem_type,
                              u_int8_t align_shift,EVAPI_devmem_info_t*  devmem_info_p);

VIP_ret_t DEVMM_free_unmap_devmem(VIP_RSCT_t usr_ctx,DEVMM_hndl_t devmm,DEVMM_dm_hndl_t dm);

/*************************************************************************
 * Function: DEVMM_get_num_alloc_chunks
 *
 * Arguments:
 *  devmm (IN) - DEVMEM object 
 *  num_alloc_chunks (OUT) - returns number of currently allocated chunks
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_CQM_HNDL
 *  VIP_EINVAL_PARAM -- NULL pointer given for num_alloc_chunks
 *
 * Description:
 *   returns number of currently allocated chunks
 *************************************************************************/ 
VIP_ret_t DEVMM_get_num_alloc_chunks(DEVMM_hndl_t devmm, u_int32_t *num_alloc_chunks);
#endif
