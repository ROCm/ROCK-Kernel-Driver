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

#ifndef H_THH_SRQM_H
#define H_THH_SRQM_H

#include <mtl_common.h>
#include <vapi_types.h>
#include <hh.h>
#include <hhul.h>
#include <thh.h>

/************************************************************************
 *  Function: THH_srqm_create
 *
 *  Arguments:
 *    hob           - The THH_hob object in which this object will be included
 *    log2_max_srq  - Size of SRQC table
 *    log2_rsvd_srq - Log2 number of reserved SRQs
 *    srqm_p        - Returned SRQ object
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *    HH_EAGAIN - Not enough resources available
 *
 *  Description:
 *    This function creates the THH_srqm object.
 */
HH_ret_t  THH_srqm_create(
  THH_hob_t              hob,           /* IN  */
  u_int8_t               log2_max_srq,  /* IN  */
  u_int8_t               log2_rsvd_srq, /* IN  */
  THH_srqm_t*            srqm_p         /* OUT */
);


/************************************************************************
 *  Function: THH_srqm_destroy
 *
 *  Arguments:
 *    srqm        - The object to destroy
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid handle
 *
 *  Description:
 *    Free all SRQM related resources.
 */
HH_ret_t  THH_srqm_destroy(
  THH_srqm_t  srqm        /* IN */
);


/************************************************************************
 *  Function: THH_srqm_create_srq
 *
 *  Arguments:
 *    srqm - HCA (SRQM) context
 *    pd   - PD of SRQ to create
 *    srq_ul_resources_p - THH's private SRQ attributes (WQEs buffer, etc.)
 *    srq_p - New SRQ handle
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *    HH_EAGAIN - Not enough resources available to complete operation
 *
 *  Description:
 *    Allocate a SRQ resource in the HCA.
 */
HH_ret_t  THH_srqm_create_srq(
  THH_srqm_t         srqm,                    /* IN */
  HH_pd_hndl_t       pd,                      /* IN */
  THH_srq_ul_resources_t *srq_ul_resources_p, /* IO  */
  HH_srq_hndl_t     *srq_p                    /* OUT */
);

/************************************************************************
 *  Function: THH_srqm_destroy_srq
 *
 *  Arguments:
 *    srqm - HCA (SRQM) context
 *    srq  - SRQ to destroy
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *
 *  Description:
 *    Release a SRQ resource. No checks for associated QPs (VIP's responsibility).
 */
HH_ret_t  THH_srqm_destroy_srq(
  THH_srqm_t         srqm,                    /* IN */
  HH_srq_hndl_t      srq                      /* IN */
);


/************************************************************************
 *  Function: THH_srqm_query_srq
 *
 *  Arguments:
 *    srqm - HCA (SRQM) context
 *    srq  - SRQ to query
 *    limit_p - Current SRQ limit
 *
 *  Returns:
 *    HH_OK
 *    HH_ESRQ   - SRQ is in error state
 *    HH_EINVAL - Invalid parameters
 *
 *  Description:
 *    Query SRQ's limit (and state).
 */
HH_ret_t  THH_srqm_query_srq(
  THH_srqm_t         srqm,                    /* IN */
  HH_srq_hndl_t      srq,                     /* IN */
  u_int32_t          *limit_p                 /* OUT */
);

/************************************************************************
 *  Function: THH_srqm_modify_srq
 *
 *  Arguments:
 *    srqm - HCA (SRQM) context
 *    srq  - SRQ to modify
 *    
 *
 *  Returns:
 *    HH_OK
 *    HH_ESRQ   - SRQ is in error state
 *    HH_EINVAL - Invalid parameters
 *    HH_EAGAIN - Not enough resources available to complete operation
 *
 *  Description:
 *    Modify SRQ's size or limit.
 */
HH_ret_t  THH_srqm_modify_srq(
  THH_srqm_t         srqm,                    /* IN */
  HH_srq_hndl_t      srq,                     /* IN */
  THH_srq_ul_resources_t *srq_ul_resources_p  /* IO */
);

#endif
