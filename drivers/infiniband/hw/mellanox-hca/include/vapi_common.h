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

#ifndef H_VAPI_COMMON_H
#define H_VAPI_COMMON_H

#include <vapi_types.h>

extern const char* VAPI_strerror( VAPI_ret_t errnum);
extern const char* VAPI_strerror_sym( VAPI_ret_t errnum);

/* Mask to symbol function. User supplies buffer (buf) which is ensured
 * not to overflow beyond bufsz. The buf pointer is conveniently returned.
 */
extern const char* VAPI_hca_cap_sym(char* buf, int bufsz, u_int32_t mask);
extern const char* VAPI_hca_attr_mask_sym(char* buf, int bufsz, u_int32_t mask);
extern const char* VAPI_qp_attr_mask_sym(char* buf, int bufsz, u_int32_t mask);
extern const char* VAPI_mrw_acl_mask_sym(char* buf, int bufsz, u_int32_t mask);
extern const char* VAPI_mr_change_mask_sym(char* buf, int bufsz, u_int32_t msk);
extern const char* VAPI_rdma_atom_acl_mask_sym(char*, int, u_int32_t);

extern const char* VAPI_atomic_cap_sym(VAPI_atomic_cap_t e);
extern const char* VAPI_sig_type_sym(VAPI_sig_type_t e);
extern const char* VAPI_ts_type_sym(VAPI_ts_type_t e);
extern const char* VAPI_qp_state_sym(VAPI_qp_state_t e);
extern const char* VAPI_mig_state_sym(VAPI_mig_state_t e);
extern const char* VAPI_special_qp_sym(VAPI_special_qp_t e);
extern const char* VAPI_mrw_type_sym(VAPI_mrw_type_t e);
extern const char* VAPI_remote_node_addr_sym(VAPI_remote_node_addr_type_t e);
extern const char* VAPI_wr_opcode_sym(VAPI_wr_opcode_t e);
extern const char* VAPI_cqe_opcode_sym(VAPI_cqe_opcode_t e);
extern const char* VAPI_wc_status_sym(VAPI_wc_status_t e);
extern const char* VAPI_comp_type_sym(VAPI_comp_type_t e);
extern const char* VAPI_cq_notif_sym(VAPI_cq_notif_type_t e);
extern const char* VAPI_event_record_sym(VAPI_event_record_type_t e);
extern const char* VAPI_event_syndrome_sym(VAPI_event_syndrome_t e);


#define VAPI_RET_PRINT(ret)  { MTL_ERROR1("%s: %d : %s (%s).\n", __func__, __LINE__,VAPI_strerror(ret),VAPI_strerror_sym(ret));} 
#define VAPI_CHECK        if ( ret != VAPI_OK) VAPI_RET_PRINT(ret);
#define VAPI_CHECK_RET    if ( ret != VAPI_OK) { VAPI_RET_PRINT(ret); return(ret); } 
                                                                               

#endif
