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

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: tavor_cq.c,v 1.6 2004/03/04 02:10:04 roland Exp $
*/

#include "tavor_priv.h"
#include "ts_ib_provider.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/slab.h>

static void _tsIbTavorCompletionHandler(
                                        VAPI_hca_hndl_t hca,
                                        VAPI_cq_hndl_t cq,
                                        void *cq_ptr
                                        ) {
  tsIbCompletionEventDispatch(cq_ptr);
}

int tsIbTavorCqCreate(
                      tTS_IB_DEVICE device,
                      int          *entries,
                      void         *device_specific,
                      tTS_IB_CQ     cq
                      ) {
  tTS_IB_TAVOR_CQ       priv_cq;
  tTS_IB_TAVOR_PRIVATE  priv = device->private;
  VAPI_ret_t            ret;
  VAPI_cqe_num_t        actual;

  if (device_specific) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: ignoring non-NULL device_specific value %p",
                   device->name, device_specific);
  }

  priv_cq = kmalloc(sizeof *priv_cq, GFP_KERNEL);
  if (!priv_cq) {
    return -ENOMEM;
  }

  ret = VAPI_create_cq(priv->vapi_handle,
                       *entries,
                       &priv_cq->cq_handle,
                       &actual);

  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: VAPI_create_cq failed, return code = %d (%s)",
                   device->name, ret, VAPI_strerror(ret));
    kfree(priv_cq);
    return -EINVAL;
  }

  ret = EVAPI_set_priv_context4cq(priv->vapi_handle,
                                  priv_cq->cq_handle,
                                  tsIbCqToHandle(cq));
  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: EVAPI_set_priv_context4cq failed, return code = %d (%s)",
                   device->name, ret, VAPI_strerror(ret));
    goto error;
  }

  ret = EVAPI_set_comp_eventh(priv->vapi_handle,
                              priv_cq->cq_handle,
                              _tsIbTavorCompletionHandler,
                              cq,
                              &priv_cq->handler);
  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: EVAPI_set_comp_eventh failed, return code = %d (%s)",
                   device->name, ret, VAPI_strerror(ret));
    goto error;
  }

  *entries = actual;
  cq->private = priv_cq;

  return 0;

 error:
  VAPI_destroy_cq(priv->vapi_handle, priv_cq->cq_handle);
  kfree(priv_cq);
  return -EINVAL;
}

int tsIbTavorCqDestroy(
                       tTS_IB_CQ cq
                       ) {
  tTS_IB_TAVOR_CQ      priv_cq = cq->private;
  tTS_IB_TAVOR_PRIVATE priv = cq->device->private;
  VAPI_ret_t           ret;

  ret = EVAPI_clear_comp_eventh(priv->vapi_handle, priv_cq->handler);
  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: EVAPI_clear_comp_eventh failed, return code = %d (%s)",
                   cq->device->name, ret, VAPI_strerror(ret));
  }

  ret = VAPI_destroy_cq(priv->vapi_handle, priv_cq->cq_handle);
  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: VAPI_destroy_cq failed, return code = %d (%s)",
                   cq->device->name, ret, VAPI_strerror(ret));
    return -EINVAL;
  }

  kfree(priv_cq);
  return 0;
}

int tsIbTavorCqResize(
                      tTS_IB_CQ cq,
                      int      *entries
                      ) {
  return -ENOSYS;
}

static inline int _tsIbTavorStatusTranslate(
                                            VAPI_wc_status_t status
                                            ) {
  switch (status) {
  case VAPI_SUCCESS:
    return TS_IB_COMPLETION_STATUS_SUCCESS;
  case VAPI_LOC_LEN_ERR:
    return TS_IB_COMPLETION_STATUS_LOCAL_LENGTH_ERROR;
  case VAPI_LOC_QP_OP_ERR:
    return TS_IB_COMPLETION_STATUS_LOCAL_QP_OPERATION_ERROR;
  case VAPI_LOC_EE_OP_ERR:
    return TS_IB_COMPLETION_STATUS_LOCAL_EEC_OPERATION_ERROR;
  case VAPI_LOC_PROT_ERR:
    return TS_IB_COMPLETION_STATUS_LOCAL_PROTECTION_ERROR;
  case VAPI_WR_FLUSH_ERR:
    return TS_IB_COMPLETION_STATUS_WORK_REQUEST_FLUSHED_ERROR;
  case VAPI_MW_BIND_ERR:
    return TS_IB_COMPLETION_STATUS_MEMORY_WINDOW_BIND_ERROR;
  case VAPI_BAD_RESP_ERR:
    return TS_IB_COMPLETION_STATUS_BAD_RESPONSE_ERROR;
  case VAPI_LOC_ACCS_ERR:
    return TS_IB_COMPLETION_STATUS_LOCAL_ACCESS_ERROR;
  case VAPI_REM_INV_REQ_ERR:
    return TS_IB_COMPLETION_STATUS_REMOTE_INVALID_REQUEST_ERROR;
  case VAPI_REM_ACCESS_ERR:
    return TS_IB_COMPLETION_STATUS_REMOTE_ACCESS_ERORR;
  case VAPI_REM_OP_ERR:
    return TS_IB_COMPLETION_STATUS_REMOTE_OPERATION_ERROR;
  case VAPI_RETRY_EXC_ERR:
    return TS_IB_COMPLETION_STATUS_TRANSPORT_RETRY_COUNTER_EXCEEDED;
  case VAPI_RNR_RETRY_EXC_ERR:
    return TS_IB_COMPLETION_STATUS_RNR_RETRY_COUNTER_EXCEEDED;
  case VAPI_LOC_RDD_VIOL_ERR:
    return TS_IB_COMPLETION_STATUS_LOCAL_RDD_VIOLATION_ERROR;
  case VAPI_REM_INV_RD_REQ_ERR:
    return TS_IB_COMPLETION_STATUS_REMOTE_INVALID_RD_REQUEST;
  case VAPI_REM_ABORT_ERR:
    return TS_IB_COMPLETION_STATUS_REMOTE_ABORTED_ERROR;
  case VAPI_INV_EECN_ERR:
    return TS_IB_COMPLETION_STATUS_INVALID_EEC_NUMBER;
  case VAPI_INV_EEC_STATE_ERR:
    return TS_IB_COMPLETION_STATUS_INVALID_EEC_STATE;
  default:
    TS_REPORT_WARN(MOD_KERNEL_IB, "unhandled VAPI error %d", status);
  case VAPI_COMP_FATAL_ERR:
  case VAPI_COMP_GENERAL_ERR:
    return TS_IB_COMPLETION_STATUS_UNKNOWN_ERROR;
  }
}

static inline int _tsIbTavorOpTranslate(
                                        VAPI_cqe_opcode_t opcode
                                        ) {
  switch (opcode) {
  case VAPI_CQE_SQ_SEND_DATA:
    return TS_IB_OP_SEND;
  case VAPI_CQE_SQ_RDMA_READ:
    return TS_IB_OP_RDMA_READ;
  case VAPI_CQE_SQ_RDMA_WRITE:
    return TS_IB_OP_RDMA_WRITE;
  case VAPI_CQE_RQ_RDMA_WITH_IMM:
    return TS_IB_OP_RDMA_WRITE_IMMEDIATE;
  case VAPI_CQE_SQ_COMP_SWAP:
    return TS_IB_OP_COMPARE_SWAP;
  case VAPI_CQE_SQ_FETCH_ADD:
    return TS_IB_OP_FETCH_ADD;
  case VAPI_CQE_RQ_SEND_DATA:
    return TS_IB_OP_RECEIVE;
  case VAPI_CQE_SQ_BIND_MRW:
    return TS_IB_OP_MEMORY_WINDOW_BIND;
  default:
    TS_REPORT_WARN(MOD_KERNEL_IB, "unhandled VAPI op %d", opcode);
  case VAPI_CQE_INVAL_OPCODE:
    return -1;
  }
}

int tsIbTavorCqPoll(
                    tTS_IB_CQ       cq,
                    tTS_IB_CQ_ENTRY entry
                    ) {
  tTS_IB_TAVOR_CQ       priv_cq = cq->private;
  tTS_IB_TAVOR_PRIVATE  priv = cq->device->private;
  VAPI_wc_desc_t        vapi_entry;
  VAPI_ret_t            ret;

  ret = VAPI_poll_cq(priv->vapi_handle, priv_cq->cq_handle, &vapi_entry);

  if (ret == VAPI_CQ_EMPTY) {
    return -EAGAIN;
  }
  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: VAPI_poll_cq failed, return code = %d (%s)",
                   cq->device->name, ret, VAPI_strerror(ret));
    return -EINVAL;
  }

  entry->work_request_id = vapi_entry.id;
  entry->status          = _tsIbTavorStatusTranslate(vapi_entry.status);

  if (entry->status == TS_IB_COMPLETION_STATUS_SUCCESS) {
    entry->op                   = _tsIbTavorOpTranslate(vapi_entry.opcode);
    entry->bytes_transferred    = vapi_entry.byte_len;
    entry->immediate_data       = vapi_entry.imm_data;
    entry->slid                 = vapi_entry.remote_node_addr.slid;
    entry->sqpn                 = vapi_entry.remote_node_addr.qp_ety.qp;
    entry->ethertype            = vapi_entry.remote_node_addr.qp_ety.ety;
    entry->local_eecn           = vapi_entry.remote_node_addr.ee_dlid.loc_eecn;
    entry->dlid_path_bits       = vapi_entry.remote_node_addr.ee_dlid.dst_path_bits;
    entry->sl                   = vapi_entry.remote_node_addr.sl;
    entry->pkey_index           = vapi_entry.pkey_ix;
    entry->freed_resource_count = vapi_entry.free_res_count;
    entry->device_specific      = NULL;
    entry->grh_present          = vapi_entry.grh_flag;
    entry->immediate_data_valid = vapi_entry.imm_data_valid;
  }

  return 0;
}

int tsIbTavorCqArm(
                   tTS_IB_CQ cq,
                   int       solicited
                   ) {
  tTS_IB_TAVOR_CQ       priv_cq = cq->private;
  tTS_IB_TAVOR_PRIVATE  priv = cq->device->private;
  VAPI_ret_t            ret;

  ret = VAPI_req_comp_notif(priv->vapi_handle,
                            priv_cq->cq_handle,
                            solicited ? VAPI_SOLIC_COMP : VAPI_NEXT_COMP);
  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: VAPI_req_comp_notif failed, return code = %d (%s)",
                   cq->device->name, ret, VAPI_strerror(ret));
    return -EINVAL;
  }

  return 0;
}
