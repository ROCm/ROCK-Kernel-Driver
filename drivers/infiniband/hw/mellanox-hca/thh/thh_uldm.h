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

#ifndef H_THH_ULDM_H
#define H_THH_ULDM_H

#include <mtl_common.h>
#include <mosal.h>
#include <hh.h>
#include <thh.h>
#include <thh_hob.h>


/******************************************************************************
 *  Function:     THH_uldm_create
 *
 *  Arguments:
 *        hob -  The THH_hob object in which this object will be included
 *        uar_base -  Physical base address of UARs (address of UAR0)
 *        log2_max_uar -  Log2 of number of UARs (including 0 and 1)
 *        log2_uar_pg_sz -  UAR page size as set in INIT_HCA
 *        max_pd -  Maximum PDs allowed to be allocated
 *        uldm_p -  Allocated THH_uldm object
 
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameter
 *    HH_EAGAIN - Not enough resources for creating this object
 *
 *  Description:
 *    Create object context which manages the UAR and PD resources.
 */
HH_ret_t THH_uldm_create( /*IN*/  THH_hob_t    hob, 
                          /*IN*/  MT_phys_addr_t  uar_base, 
                          /*IN*/  u_int8_t     log2_max_uar, 
                          /*IN*/  u_int8_t     log2_uar_pg_sz,
                          /*IN*/  u_int32_t    max_pd, 
                          /*OUT*/ THH_uldm_t   *uldm_p );

/******************************************************************************
 *  Function:     THH_uldm_destroy
 *
 *  Arguments:
 *        uldm -  THH_uldm object to destroy
 
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameter
 *
 *  Description:
 *    Free resources of the THH_uldm object.
 */
HH_ret_t THH_uldm_destroy( /*IN*/ THH_uldm_t uldm );

/*************************************************************************
 * Function: THH_uldm_alloc_ul_res
 *
 *  Arguments:
 *        uldm 
 *        prot_ctx - Protection context of user level
 *        hca_ul_resources_p - Returned user level resources
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameter
 *    HH_EAGAIN - No available resources to allocate
 *
 *  Description:
 *    Allocate user level resources (UAR).
 */
HH_ret_t THH_uldm_alloc_ul_res( /*IN*/ THH_uldm_t              uldm, 
                                /*IN*/ MOSAL_protection_ctx_t  prot_ctx, 
                                /*OUT*/ THH_hca_ul_resources_t *hca_ul_resources_p );

/*************************************************************************
 * Function: THH_uldm_free_ul_res
 *
 *  Arguments:
 *        uldm 
 *        hca_ul_resources_p - A copy of resources structure returned on
 *                             resources allocation
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameter
 *
 *  Description:
 *    Free the resources allocated using THH_uldm_alloc_ul_res()
 */
HH_ret_t THH_uldm_free_ul_res( /*IN*/ THH_uldm_t             uldm, 
                               /*IN*/ THH_hca_ul_resources_t *hca_ul_resources_p);

/*************************************************************************
 * Function: THH_uldm_alloc_uar
 *
 *  Arguments:
 *        uldm
 *        prot_ctx -  User level protection context to map UAR to 
 *        uar_index - Returned index of allocated UAR
 *        uar_map -   Virtual address in user level context to which the
 *                    allocated UAR is mapped
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameter
 *    HH_EAGAIN - No available UAR
 *
 *  Description:
 *    Allocate an available UAR and map it to user level memory space.
 */
HH_ret_t THH_uldm_alloc_uar( /*IN*/ THH_uldm_t              uldm, 
                             /*IN*/ MOSAL_protection_ctx_t  prot_ctx, 
                             /*OUT*/ u_int32_t              *uar_index, 
                             /*OUT*/ MT_virt_addr_t            *uar_map );

/******************************************************************************
 *  Function:     THH_uldm_free_uar
 *
 *  Arguments:
 *        uldm
 *        uar_index - Index of UAR to free
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameter
 *
 *  Description:
 *    Unmap given UAR from user level memory space and return it to free UARs pool.
 */
HH_ret_t THH_uldm_free_uar( /*IN*/ THH_uldm_t   uldm, 
                            /*IN*/ u_int32_t    uar_index );

/******************************************************************************
 *  Function:     THH_uldm_alloc_pd
 *
 *  Arguments:
 *        uldm
 *        prot_ctx -  Protection context of user asking for this PD
 *        pd_ul_resources_p - Mostly UD AV table memory to register 
 *        pd_p - Allocated PD handle
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameter
 *    HH_EAGAIN - No free PDs to allocate
 *
 *  Description:
 *    Allocate a PD for given protection context.
 */
HH_ret_t THH_uldm_alloc_pd( /*IN*/ THH_uldm_t                   uldm, 
                            /*IN*/ MOSAL_protection_ctx_t       prot_ctx, 
                            /*IN*/ THH_pd_ul_resources_t        *pd_ul_resources_p, 
                            /*OUT*/ HH_pd_hndl_t                *pd_p );

/******************************************************************************
 *  Function:     THH_uldm_free_pd 
 *
 *  Arguments:
 *        uldm
 *        pd   - PD to free
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameter
 *
 *  Description:
 *    Free the PD.
 */
HH_ret_t THH_uldm_free_pd( /*IN*/ THH_uldm_t    uldm, 
                           /*IN*/ HH_pd_hndl_t  pd ) ;

/******************************************************************************
 *  Function:     THH_uldm_get_protection_ctx 
 *
 *  Arguments:
 *        uldm
 *        pd -  PD for which the protection context is required
 *        prot_ctx_p - Returned protection context 
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameter
 *
 *  Description:
 *    This function returns the protection context associated with a PD.  It is used by
 *    THH_mrwm_register_internal() in memory locking and mapping of WQE buffers to physical
 *    pages. (the mrwm is given a PD handle, and needs to retrieve the associated protection
 *    context).
 */
HH_ret_t THH_uldm_get_protection_ctx( /*IN*/ THH_uldm_t                 uldm, 
                                      /*IN*/ HH_pd_hndl_t               pd, 
                                      /*OUT*/ MOSAL_protection_ctx_t    *prot_ctx_p );

/******************************************************************************
 *  Function:     THH_uldm_get_num_objs 
 *
 *  Arguments:
 *        uldm
 *        num_alloc_us_res_p -  allocated resource count
 *        num_alloc_pds_p    -  allocated PDs count
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameter (bad handle, or both return value ptrs are NULL
 *
 *  Description:
 *    For debugging -- returns allocated resource count and/or number of allocated PDs.
 *         either num_alloc_us_res_p or num_alloc_pds_p (but not both) may be NULL;
 */
HH_ret_t THH_uldm_get_num_objs( /*IN*/ THH_uldm_t uldm, u_int32_t *num_alloc_us_res_p,
                                           u_int32_t  *num_alloc_pds_p);

#endif
