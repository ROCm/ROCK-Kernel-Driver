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

#ifndef H_UAR_H
#define H_UAR_H

#include <mtl_common.h>
#include <ib_defs.h>
#include <vapi_types.h>
#include <mosal.h>
#include <hh.h>
#include <thh_common.h>
#include <tavor_if_defs.h>



typedef struct {
  
  IB_wqpn_t qpn;              /* QP number */
  tavor_if_nopcode_t nopcode; /* Next Send descriptor opcode (encoded) */
  MT_bool fence;              /* Fence bit set */
  u_int32_t next_addr_32lsb;  /* Address of next WQE (the one linked) */
  u_int32_t next_size  ;      /* Size of next WQE (16-byte chunks) */

} THH_uar_sendq_dbell_t;

typedef struct {
  
  IB_wqpn_t qpn;              /* QP number */
  u_int32_t next_addr_32lsb;  /* Address of next WQE (the one linked) */
  u_int32_t next_size  ;      /* Size of next WQE (16-byte chunks) */
  u_int8_t credits;           /* Number of WQEs attached with this doorbell (255 max.) */

} THH_uar_recvq_dbell_t;

typedef tavor_if_uar_cq_cmd_t THH_uar_cq_cmd_t;
typedef tavor_if_uar_eq_cmd_t THH_uar_eq_cmd_t;


/************************************************************************
 *  Function: THH_uar_create
 *
 *  Arguments:
 *     version_p 
 *     uar_index
 *     uar_base - Virtual address mapped to associated UAR
 *     uar_p -    Created THH_uar object handle
 *
 *  Returns:
 *    HH_OK
 *    HH_EAGAIN - Not enough resources to create object
 *    HH_EINVAL - Invalid parameters (NULL ptrs.etc.)
 *
 *  Description: Create the THH_uar object.
 */
HH_ret_t  THH_uar_create(
  /*IN*/  THH_ver_info_t  *version_p, 
  /*IN*/  THH_uar_index_t uar_index, 
  /*IN*/  void            *uar_base, 
  /*OUT*/ THH_uar_t       *uar_p
  );


/************************************************************************
 *
 *  Function: THH_uar_destroy
 *
 *  Arguments:
 *    uar - Object handle
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid handle
 *
 *  Description: Free UAR object context.
 */
HH_ret_t THH_uar_destroy(/*IN*/ THH_uar_t uar);


/************************************************************************
 *
 *  Function: THH_uar_get_index
 *
 *  Arguments:
 *    uar - Object handle
 *    uar_index_p - Returned UAR page index of UAR associated with this object
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL 
 *
 *  Description: Get associated UAR page index.
 */
HH_ret_t THH_uar_get_index(
  /*IN*/ THH_uar_t uar, 
  /*OUT*/ THH_uar_index_t *uar_index_p
  );


/************************************************************************
 *  Function: THH_uar_sendq_dbell
 *
 *  Arguments:
 *    uar -           The THH_uar object handle
 *    sendq_dbell_p - Send queue doorbel data
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL -Invalid handles or NULL pointer
 *
 *  Description:
 *    Ring the  send   section of the UAR.
 */
HH_ret_t  THH_uar_sendq_dbell(
  /*IN*/  THH_uar_t              uar,          
  /*IN*/  THH_uar_sendq_dbell_t* sendq_dbell_p
  );


/************************************************************************
 *  Function: THH_uar_sendq_rd_dbell
 *
 *  Arguments:
 *    uar -           The THH_uar object handle
 *    sendq_dbell_p - Send queue doorbell data
 *    een -           The EE context number for posted request
 *
 *  Returns:
 *    HH_OK HH_EINVAL -Invalid handles or NULL pointer
 *  Description:
 *    Ring the  rdd-send   section of the UAR.
 */
HH_ret_t  THH_uar_sendq_rd_dbell(
  /*IN*/  THH_uar_t               uar,            
  /*IN*/  THH_uar_sendq_dbell_t*  sendq_dbell_p,  
  /*IN*/  IB_eecn_t               een             
  );


/************************************************************************
 *  Function: THH_uar_recvq_dbell
 *
 *  Arguments:
 *    uar -           The THH_uar object handle
 *    recvq_dbell_p - Receive queue doorbell data
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL -Invalid handles
 *
 *  Description:
 *    Ring the  receive   section of the UAR for posting receive WQEs..
 */
HH_ret_t THH_uar_recvq_dbell(
  /*IN*/  THH_uar_t              uar,          
  /*IN*/  THH_uar_recvq_dbell_t* recvq_dbell_p 
  );


/************************************************************************
 *  Function: THH_uar_cq_cmd
 *
 *  Arguments:
 *    uar -   The THH_uar object handle
 *    cmd -   The CQ command code
 *    cqn -   The CQC index of the CQ to perform command on
 *    param - The 32 bit parameter  (local CPU endianess)
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL -Invalid handles
 *
 *  Description:
 *    Invoke a CQ context update through the CQ_cmd section of the UAR.
 */
HH_ret_t THH_uar_cq_cmd(
  THH_uar_t         uar,    /* IN */
  THH_uar_cq_cmd_t  cmd,    /* IN */
  HH_cq_hndl_t      cqn,    /* IN */
  u_int32_t         param   /* IN */);


/************************************************************************
 *  Function: THH_uar_eq_cmd
 *
 *  Arguments:
 *    uar -   The THH_uar object handle
 *    cmd -   The EQ command code
 *    eqn -   The EQC index of the CQ to perform command on
 *    param - The 32 bit parameter
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL -Invalid handles
 *
 *  Description:
 *    Invoke a EQ context update through the EQ_cmd section of the UAR.
 */
HH_ret_t THH_uar_eq_cmd(
  THH_uar_t         uar,  /* IN */
  THH_uar_eq_cmd_t  cmd,  /* IN */
  THH_eqn_t         eqn,  /* IN */
  u_int32_t         param /* IN */);


/************************************************************************
 *  Function: THH_uar_blast
 *
 *  Arguments:
 *    uar -    The THH_uar object handle
 *    wqe_p -  A pointer to the WQE structure to push to the "flame"
 *    wqe_sz - WQE size
 *    sendq_dbell_p ­ Send queue doorbell data
 *    een ­ The EE context number for posted request (valid for RD-send)
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid handles or NULL pointer
 *
 *  Description:
 *
 *  This function pushes the given WQE descriptor to the InfiniBlast(tm)
 *  buffer through the infini_blast section of the UAR, and then rings the
 *  "send" doorbell (in order to assure atomicity between the writing of the 
 *  InfiniBlast buffer and the ringing of the "send" doorbell.
 *  If given een is valid (0-0xFFFFFF) the "rd-send" doorbell is used.
 */
HH_ret_t THH_uar_blast(
  THH_uar_t  uar,    /* IN */
  void*      wqe_p,  /* IN */
  MT_size_t  wqe_sz, /* IN */
  THH_uar_sendq_dbell_t	*sendq_dbell_p,  /* IN */
  IB_eecn_t	een                          /* IN */
);

#endif /* H_UAR_H */
