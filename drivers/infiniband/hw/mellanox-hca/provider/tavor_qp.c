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

  $Id: tavor_qp.c,v 1.10 2004/03/10 02:38:36 roland Exp $
*/

#include "tavor_priv.h"
#include "ts_ib_provider.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"
#include "ts_kernel_uintptr.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/slab.h>

static void _tsIbTavorRequestAttr(
                                  tTS_IB_QP_CREATE_PARAM param,
                                  VAPI_qp_init_attr_t   *request_attr
                                  ) {
  request_attr->sq_cq_hndl         =
    ((tTS_IB_TAVOR_CQ) tsIbCqFromHandle(param->send_queue)->private)->cq_handle;
  request_attr->rq_cq_hndl         =
    ((tTS_IB_TAVOR_CQ) tsIbCqFromHandle(param->receive_queue)->private)->cq_handle;
  request_attr->cap.max_oust_wr_sq = param->limit.max_outstanding_send_request;
  request_attr->cap.max_oust_wr_rq = param->limit.max_outstanding_receive_request;
  request_attr->cap.max_sg_size_sq = param->limit.max_send_gather_element;
  request_attr->cap.max_sg_size_rq = param->limit.max_receive_scatter_element;
  request_attr->sq_sig_type        =
    param->send_policy    == TS_IB_WQ_SIGNAL_ALL ? VAPI_SIGNAL_ALL_WR : VAPI_SIGNAL_REQ_WR;
  request_attr->rq_sig_type        =
    param->receive_policy == TS_IB_WQ_SIGNAL_ALL ? VAPI_SIGNAL_ALL_WR : VAPI_SIGNAL_REQ_WR;
  request_attr->pd_hndl            = *(VAPI_pd_hndl_t *) &tsIbPdFromHandle(param->pd)->private;

  switch (param->transport) {
  case TS_IB_TRANSPORT_UD:
    request_attr->ts_type = VAPI_TS_UD;
    break;
  case TS_IB_TRANSPORT_RD:
    request_attr->ts_type = VAPI_TS_RD;
    break;
  case TS_IB_TRANSPORT_UC:
    request_attr->ts_type = VAPI_TS_UC;
    break;
  case TS_IB_TRANSPORT_RC:
    request_attr->ts_type = VAPI_TS_RC;
    break;
  }
}

int tsIbTavorQpCreate(
                      tTS_IB_PD              pd,
                      tTS_IB_QP_CREATE_PARAM param,
                      tTS_IB_QP              qp
                      ) {
  tTS_IB_TAVOR_QP priv_qp;

  priv_qp = kmalloc(sizeof *priv_qp, GFP_KERNEL);
  if (!priv_qp) {
    return -ENOMEM;
  }

  if (param->device_specific) {
    /* register a user space QP */
    tTS_IB_TAVOR_QP_CREATE_PARAM tavor_param = param->device_specific;

    qp->qpn = tavor_param->qpn;
    priv_qp->is_user               = 1;
    priv_qp->cached_state          = TS_IB_QP_STATE_RESET;
    priv_qp->qp_handle.user_handle = (VAPI_k_qp_hndl_t) (uintptr_t) tavor_param->vapi_k_handle;
  } else {
    /* create a kernel space QP */
    tTS_IB_TAVOR_PRIVATE  priv = pd->device->private;
    VAPI_ret_t            ret;
    VAPI_qp_init_attr_t   request_attr;
    VAPI_qp_prop_t        qp_prop;

    priv_qp->is_user = 0;

    _tsIbTavorRequestAttr(param, &request_attr);
    ret = VAPI_create_qp(priv->vapi_handle,
                         &request_attr,
                         &priv_qp->qp_handle.kernel_handle,
                         &qp_prop);
    if (ret != VAPI_OK) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "%s: VAPI_create_qp failed, return code = %d (%s)",
                     pd->device->name, ret, VAPI_strerror(ret));
      goto error;
    }

    ret = EVAPI_set_priv_context4qp(priv->vapi_handle,
                                    priv_qp->qp_handle.kernel_handle,
                                    tsIbQpToHandle(qp));
    if (ret != VAPI_OK) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "%s: EVAPI_set_priv_context4qp failed, return code = %d (%s)",
                     pd->device->name, ret, VAPI_strerror(ret));
      VAPI_destroy_qp(priv->vapi_handle, priv_qp->qp_handle.kernel_handle);
      goto error;
    }
    qp->qpn = qp_prop.qp_num;
  }

  qp->private = priv_qp;

  return 0;

 error:
  kfree(priv_qp);
  return -EINVAL;
}

int tsIbTavorSpecialQpCreate(
                             tTS_IB_PD              pd,
                             tTS_IB_QP_CREATE_PARAM param,
                             tTS_IB_PORT            port,
                             tTS_IB_SPECIAL_QP_TYPE qp_type,
                             tTS_IB_QP              qp
                             ) {
  tTS_IB_TAVOR_QP       priv_qp;
  tTS_IB_TAVOR_PRIVATE  priv = pd->device->private;
  VAPI_ret_t            ret;
  VAPI_qp_init_attr_t   request_attr;
  VAPI_qp_cap_t         qp_cap;

  if (qp_type != TS_IB_SMI_QP && qp_type != TS_IB_GSI_QP) {
    return -ENOSYS;
  }

  priv_qp = kmalloc(sizeof *priv_qp, GFP_KERNEL);
  if (!priv_qp) {
    return -ENOMEM;
  }

  priv_qp->is_user = 0;

  if (param->device_specific) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: ignoring non-NULL device_specific value %p",
                   pd->device->name, param->device_specific);
  }

  _tsIbTavorRequestAttr(param, &request_attr);
  ret = VAPI_get_special_qp(priv->vapi_handle,
                            port,
                            qp_type == TS_IB_SMI_QP ? VAPI_SMI_QP : VAPI_GSI_QP,
                            &request_attr,
                            &priv_qp->qp_handle.kernel_handle,
                            &qp_cap);
  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: VAPI_get_special_qp failed, return code = %d (%s)",
                   pd->device->name, ret, VAPI_strerror(ret));
    goto error;
  }

  ret = EVAPI_set_priv_context4qp(priv->vapi_handle, priv_qp->qp_handle.kernel_handle, qp);
  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s, EVAPI_set_priv_context4qp failed, return code = %d (%s)",
                   pd->device->name, ret, VAPI_strerror(ret));
    VAPI_destroy_qp(priv->vapi_handle, priv_qp->qp_handle.kernel_handle);
    goto error;
  }

  qp->qpn     = qp_type == TS_IB_SMI_QP ? 0 : 1;
  qp->private = priv_qp;

  return 0;

 error:
  kfree(priv_qp);
  return -EINVAL;
}

static inline VAPI_qp_state_t _tsIbTavorStateToVapi(
                                                    tTS_IB_QP_STATE state
                                                    ) {
  switch (state) {
  case TS_IB_QP_STATE_RESET:
    return VAPI_RESET;
  case TS_IB_QP_STATE_INIT:
    return VAPI_INIT;
  case TS_IB_QP_STATE_RTR:
    return VAPI_RTR;
  case TS_IB_QP_STATE_RTS:
    return VAPI_RTS;
  case TS_IB_QP_STATE_SQD:
    return VAPI_SQD;
  case TS_IB_QP_STATE_SQE:
    return VAPI_SQE;
  case TS_IB_QP_STATE_ERROR:
    return VAPI_ERR;
  default:
    return -1;
  }
}

int tsIbTavorQpModify(
                      tTS_IB_QP           qp,
                      tTS_IB_QP_ATTRIBUTE attr
                      ) {
  tTS_IB_TAVOR_QP      priv_qp = qp->private;
  tTS_IB_TAVOR_PRIVATE priv = qp->device->private;
  VAPI_qp_attr_t      *request_attr;
  VAPI_qp_attr_mask_t  request_mask;
  VAPI_qp_cap_t        qp_cap;
  VAPI_ret_t           ret;

  request_attr = kmalloc(sizeof *request_attr, GFP_KERNEL);
  if (!request_attr) {
    return -ENOMEM;
  }

  QP_ATTR_MASK_CLR_ALL(request_mask);

  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_STATE) {
    request_attr->qp_state = _tsIbTavorStateToVapi(attr->state);
    priv_qp->cached_state = attr->state;
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_QP_STATE);
  }
  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_SEND_PSN) {
    request_attr->sq_psn = attr->send_psn;
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_SQ_PSN);
  }
  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_RECEIVE_PSN) {
    request_attr->rq_psn = attr->receive_psn;
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_RQ_PSN);
  }
  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_DESTINATION_QPN) {
    request_attr->dest_qp_num = attr->destination_qpn;
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_DEST_QP_NUM);
  }
  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_QKEY) {
    request_attr->qkey = attr->qkey;
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_QKEY);
  }
  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_PATH_MTU) {
    request_attr->path_mtu = attr->path_mtu;
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_PATH_MTU);
  }
  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_MIGRATION_STATE) {
    request_attr->path_mig_state = attr->migration_state;
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_PATH_MIG_STATE);
  }
  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_INITIATOR_DEPTH) {
    request_attr->ous_dst_rd_atom = attr->initiator_depth;
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_OUS_DST_RD_ATOM);
  }
  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_RESPONDER_RESOURCES) {
    request_attr->qp_ous_rd_atom = attr->responder_resources;
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_QP_OUS_RD_ATOM);
  }
  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_RETRY_COUNT) {
    request_attr->retry_count = attr->retry_count;
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_RETRY_COUNT);
  }
  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_RNR_RETRY_COUNT) {
    request_attr->rnr_retry = attr->rnr_retry_count;
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_RNR_RETRY);
  }
  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_RNR_TIMEOUT) {
    request_attr->min_rnr_timer = attr->rnr_timeout;
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_MIN_RNR_TIMER);
  }
  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_PKEY_INDEX) {
    request_attr->pkey_ix = attr->pkey_index;
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_PKEY_IX);
  }
  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_PORT) {
    request_attr->port = attr->port;
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_PORT);
  }
  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_ADDRESS) {
    request_attr->av.sl            = attr->address.service_level;
    request_attr->av.grh_flag      = attr->address.use_grh;
    request_attr->av.dlid          = attr->address.dlid;
    request_attr->av.static_rate   = attr->address.static_rate;
    request_attr->av.src_path_bits = attr->address.source_path_bits;
    if (attr->address.use_grh) {
      request_attr->av.flow_label    = attr->address.flow_label;
      request_attr->av.hop_limit     = attr->address.hop_limit;
      request_attr->av.traffic_class = attr->address.traffic_class;
      request_attr->av.sgid_index    = attr->address.source_gid_index;
      memcpy(request_attr->av.dgid, attr->address.dgid,
             sizeof attr->address.dgid);
    }
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_AV);
  }
  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_LOCAL_ACK_TIMEOUT) {
    request_attr->timeout = attr->local_ack_timeout;
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_TIMEOUT);
  }
  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_ALT_PKEY_INDEX) {
    request_attr->alt_pkey_ix = attr->alt_pkey_index;
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_ALT_PATH);
  }
  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_ALT_PORT) {
    request_attr->alt_port = attr->alt_port;
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_ALT_PATH);
  }
  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_ALT_ADDRESS) {
    request_attr->alt_av.sl            = attr->alt_address.service_level;
    request_attr->alt_av.grh_flag      = attr->alt_address.use_grh;
    request_attr->alt_av.dlid          = attr->alt_address.dlid;
    request_attr->alt_av.static_rate   = attr->alt_address.static_rate;
    request_attr->alt_av.src_path_bits = attr->alt_address.source_path_bits;
    if (attr->alt_address.use_grh) {
      request_attr->alt_av.flow_label    = attr->alt_address.flow_label;
      request_attr->alt_av.hop_limit     = attr->alt_address.hop_limit;
      request_attr->alt_av.traffic_class = attr->alt_address.traffic_class;
      request_attr->alt_av.sgid_index    = attr->alt_address.source_gid_index;
      memcpy(request_attr->alt_av.dgid, attr->alt_address.dgid,
             sizeof attr->alt_address.dgid);
    }
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_ALT_PATH);
  }
  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_ALT_LOCAL_ACK_TIMEOUT) {
    request_attr->alt_timeout = attr->alt_local_ack_timeout;
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_ALT_PATH);
  }
  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_RDMA_ATOMIC_ENABLE) {
    request_attr->remote_atomic_flags =
      (!!attr->enable_atomic     * VAPI_EN_REM_ATOMIC_OP) |
      (!!attr->enable_rdma_read  * VAPI_EN_REM_READ)      |
      (!!attr->enable_rdma_write * VAPI_EN_REM_WRITE);
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_REMOTE_ATOMIC_FLAGS);
  }

  if (attr->valid_fields & TS_IB_QP_ATTRIBUTE_SQD_ASYNC_EVENT_ENABLE) {
    request_attr->en_sqd_asyn_notif = attr->sqd_async_event_enable;
    QP_ATTR_MASK_SET(request_mask, QP_ATTR_EN_SQD_ASYN_NOTIF);
  }

  if (priv_qp->is_user) {
    ret = EVAPI_k_modify_qp(priv->vapi_handle,
                            priv_qp->qp_handle.user_handle,
                            request_attr,
                            &request_mask);
    if (ret != VAPI_OK) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "%s: EVAPI_k_modify_qp failed, return code = %d (%s)",
                     qp->device->name, ret, VAPI_strerror(ret));
      goto out;
    }
  } else {
    ret = VAPI_modify_qp(priv->vapi_handle,
                         priv_qp->qp_handle.kernel_handle,
                         request_attr,
                         &request_mask,
                         &qp_cap);
    if (ret != VAPI_OK) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "%s: VAPI_modify_qp failed, return code = %d (%s)",
                     qp->device->name, ret, VAPI_strerror(ret));
      goto out;
    }
  }

 out:
  kfree(request_attr);
  return (ret == VAPI_OK) ? 0 : -EINVAL;
}

static inline tTS_IB_QP_STATE _tsIbTavorStateFromVapi(
                                                      VAPI_qp_state_t state
                                                      ) {
  switch (state) {
  case VAPI_RESET:
    return TS_IB_QP_STATE_RESET;
  case VAPI_INIT:
    return TS_IB_QP_STATE_INIT;
  case VAPI_RTR:
    return TS_IB_QP_STATE_RTR;
  case VAPI_RTS:
    return TS_IB_QP_STATE_RTS;
  case VAPI_SQD:
    return TS_IB_QP_STATE_SQD;
  case VAPI_SQE:
    return TS_IB_QP_STATE_SQE;
  case VAPI_ERR:
    return TS_IB_QP_STATE_ERROR;
  default:
    return -1;
  }
}

int tsIbTavorQpQuery(
                     tTS_IB_QP           qp,
                     tTS_IB_QP_ATTRIBUTE attr
                     ) {
  tTS_IB_TAVOR_QP      priv_qp = qp->private;
  tTS_IB_TAVOR_PRIVATE priv = qp->device->private;

  VAPI_qp_attr_t      *qp_attr;
  VAPI_qp_attr_mask_t  qp_attr_mask;
  VAPI_qp_init_attr_t  qp_init_attr;
  VAPI_ret_t           ret;

  if (priv_qp->is_user) {
    if (attr->valid_fields == TS_IB_QP_ATTRIBUTE_STATE) {
      attr->state = priv_qp->cached_state;
      return 0;
    }

    return -EINVAL;
  }

  qp_attr = kmalloc(sizeof *qp_attr, GFP_KERNEL);
  if (!qp_attr) {
    return -ENOMEM;
  }

  ret = VAPI_query_qp(priv->vapi_handle,
                      priv_qp->qp_handle.kernel_handle,
                      qp_attr,
                      &qp_attr_mask,
                      &qp_init_attr);
  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: VAPI_query_qp failed, return code = %d (%s)",
                   qp->device->name, ret, VAPI_strerror(ret));
    kfree(qp_attr);
    return -EINVAL;
  }

  attr->valid_fields = 0;

  attr->sq_drained = !qp_attr->sq_draining;

  if (QP_ATTR_IS_SET(qp_attr_mask, QP_ATTR_QP_STATE)) {
    attr->valid_fields |= TS_IB_QP_ATTRIBUTE_STATE;
    attr->state         = _tsIbTavorStateFromVapi(qp_attr->qp_state);
  }
  if (QP_ATTR_IS_SET(qp_attr_mask, QP_ATTR_EN_SQD_ASYN_NOTIF)) {
    attr->valid_fields          |= TS_IB_QP_ATTRIBUTE_SQD_ASYNC_EVENT_ENABLE;
    attr->sqd_async_event_enable = qp_attr->en_sqd_asyn_notif;
  }
  if (QP_ATTR_IS_SET(qp_attr_mask, QP_ATTR_REMOTE_ATOMIC_FLAGS)) {
    attr->valid_fields     |= TS_IB_QP_ATTRIBUTE_RDMA_ATOMIC_ENABLE;
    attr->enable_atomic     = !!(qp_attr->remote_atomic_flags & VAPI_EN_REM_ATOMIC_OP);
    attr->enable_rdma_read  = !!(qp_attr->remote_atomic_flags & VAPI_EN_REM_READ);
    attr->enable_rdma_write = !!(qp_attr->remote_atomic_flags & VAPI_EN_REM_WRITE);
    /* XXX */
  }
  if (QP_ATTR_IS_SET(qp_attr_mask, QP_ATTR_PKEY_IX)) {
    attr->valid_fields |= TS_IB_QP_ATTRIBUTE_PKEY_INDEX;
    attr->pkey_index    = qp_attr->pkey_ix;
  }
  if (QP_ATTR_IS_SET(qp_attr_mask, QP_ATTR_PORT)) {
    attr->valid_fields |= TS_IB_QP_ATTRIBUTE_PORT;
    attr->port          = qp_attr->port;
  }
  if (QP_ATTR_IS_SET(qp_attr_mask, QP_ATTR_QKEY)) {
    attr->valid_fields |= TS_IB_QP_ATTRIBUTE_QKEY;
    attr->qkey          = qp_attr->qkey;
  }
  if (QP_ATTR_IS_SET(qp_attr_mask, QP_ATTR_AV)) {
    attr->valid_fields |= TS_IB_QP_ATTRIBUTE_ADDRESS;
    /* XXX */
  }
  if (QP_ATTR_IS_SET(qp_attr_mask, QP_ATTR_PATH_MTU)) {
    attr->valid_fields |= TS_IB_QP_ATTRIBUTE_PATH_MTU;
    attr->path_mtu      = qp_attr->path_mtu;
  }
  if (QP_ATTR_IS_SET(qp_attr_mask, QP_ATTR_TIMEOUT)) {
    attr->valid_fields     |= TS_IB_QP_ATTRIBUTE_LOCAL_ACK_TIMEOUT;
    attr->local_ack_timeout = qp_attr->timeout;
  }
  if (QP_ATTR_IS_SET(qp_attr_mask, QP_ATTR_RETRY_COUNT)) {
    attr->valid_fields |= TS_IB_QP_ATTRIBUTE_RETRY_COUNT;
    attr->retry_count   = qp_attr->retry_count;
  }
  if (QP_ATTR_IS_SET(qp_attr_mask, QP_ATTR_RNR_RETRY)) {
    attr->valid_fields   |= TS_IB_QP_ATTRIBUTE_RNR_RETRY_COUNT;
    attr->rnr_retry_count = qp_attr->rnr_retry;
  }
  if (QP_ATTR_IS_SET(qp_attr_mask, QP_ATTR_RQ_PSN)) {
    attr->valid_fields |= TS_IB_QP_ATTRIBUTE_RECEIVE_PSN;
    attr->receive_psn   = qp_attr->rq_psn;
  }
  if (QP_ATTR_IS_SET(qp_attr_mask, QP_ATTR_QP_OUS_RD_ATOM)) {
    attr->valid_fields       |= TS_IB_QP_ATTRIBUTE_RESPONDER_RESOURCES;
    attr->responder_resources = qp_attr->qp_ous_rd_atom;
  }
  if (QP_ATTR_IS_SET(qp_attr_mask, QP_ATTR_ALT_PATH)) {
    attr->valid_fields |= (TS_IB_QP_ATTRIBUTE_ALT_PKEY_INDEX |
                           TS_IB_QP_ATTRIBUTE_ALT_PORT       |
                           TS_IB_QP_ATTRIBUTE_ALT_ADDRESS    |
                           TS_IB_QP_ATTRIBUTE_ALT_LOCAL_ACK_TIMEOUT);
    /* XXX */
  }
  if (QP_ATTR_IS_SET(qp_attr_mask, QP_ATTR_MIN_RNR_TIMER)) {
    attr->valid_fields |= TS_IB_QP_ATTRIBUTE_RNR_TIMEOUT;
    attr->rnr_timeout   = qp_attr->min_rnr_timer;
  }
  if (QP_ATTR_IS_SET(qp_attr_mask, QP_ATTR_SQ_PSN)) {
    attr->valid_fields |= TS_IB_QP_ATTRIBUTE_SEND_PSN;
    attr->send_psn      = qp_attr->sq_psn;
  }
  if (QP_ATTR_IS_SET(qp_attr_mask, QP_ATTR_OUS_DST_RD_ATOM)) {
    attr->valid_fields   |= TS_IB_QP_ATTRIBUTE_INITIATOR_DEPTH;
    attr->initiator_depth = qp_attr->ous_dst_rd_atom;
  }
  if (QP_ATTR_IS_SET(qp_attr_mask, QP_ATTR_PATH_MIG_STATE)) {
    attr->valid_fields   |= TS_IB_QP_ATTRIBUTE_MIGRATION_STATE;
    switch (qp_attr->path_mig_state) {
    case VAPI_MIGRATED:
      attr->migration_state = TS_IB_MIGRATED;
      break;
    case VAPI_REARM:
      attr->migration_state = TS_IB_REARM;
      break;
    case VAPI_ARMED:
      attr->migration_state = TS_IB_ARMED;
      break;
    }
  }
  if (QP_ATTR_IS_SET(qp_attr_mask, QP_ATTR_DEST_QP_NUM)) {
    attr->valid_fields   |= TS_IB_QP_ATTRIBUTE_DESTINATION_QPN;
    attr->destination_qpn = qp_attr->dest_qp_num;
  }

  kfree(qp_attr);
  return 0;
}

int tsIbTavorQpDestroy(
                       tTS_IB_QP qp
                       ) {
  tTS_IB_TAVOR_QP      priv_qp = qp->private;
  tTS_IB_TAVOR_PRIVATE priv = qp->device->private;
  VAPI_ret_t           ret;

  if (!priv_qp->is_user) {
    ret = VAPI_destroy_qp(priv->vapi_handle, priv_qp->qp_handle.kernel_handle);
    if (ret != VAPI_OK) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "%s: VAPI_destroy_qp failed, return code = %d (%s)",
                     qp->device->name, ret, VAPI_strerror(ret));
      return -EINVAL;
    }
  }

  kfree(priv_qp);
  return 0;
}

static inline void _tsIbTavorSendTranslate(
                                           tTS_IB_SEND_PARAM param,
                                           VAPI_sr_desc_t   *desc
                                           ) {
  desc->id         = param->work_request_id;
  switch (param->op) {
  case TS_IB_OP_SEND:
    desc->opcode      = VAPI_SEND;
    break;
  case TS_IB_OP_SEND_IMMEDIATE:
    desc->opcode      = VAPI_SEND_WITH_IMM;
    desc->imm_data    = param->immediate_data;
    break;
  case TS_IB_OP_RDMA_WRITE:
    desc->opcode      = VAPI_RDMA_WRITE;
    desc->remote_addr = param->remote_address;
    desc->r_key       = param->rkey;
    break;
  case TS_IB_OP_RDMA_WRITE_IMMEDIATE:
    desc->opcode      = VAPI_RDMA_WRITE_WITH_IMM;
    desc->remote_addr = param->remote_address;
    desc->r_key       = param->rkey;
    desc->imm_data    = param->immediate_data;
    break;
  case TS_IB_OP_RDMA_READ:
    desc->opcode      = VAPI_RDMA_READ;
    desc->remote_addr = param->remote_address;
    desc->r_key       = param->rkey;
    break;
  case TS_IB_OP_COMPARE_SWAP:
    desc->opcode      = VAPI_ATOMIC_CMP_AND_SWP;
    desc->compare_add = param->compare_add;
    desc->swap        = param->swap;
    break;
  case TS_IB_OP_FETCH_ADD:
    desc->opcode      = VAPI_ATOMIC_FETCH_AND_ADD;
    desc->compare_add = param->compare_add;
    break;
  default:
    desc->opcode      = -1;
    break;
  }
  desc->comp_type   = param->signaled ? VAPI_SIGNALED : VAPI_UNSIGNALED;
  desc->sg_lst_p    = (VAPI_sg_lst_entry_t *) param->gather_list;
  desc->sg_lst_len  = param->num_gather_entries;
  desc->fence       = param->fence;
  if (param->dest_address != TS_IB_HANDLE_INVALID) {
    desc->remote_ah = *(VAPI_ud_av_hndl_t *) &tsIbAddressFromHandle(param->dest_address)->private;
  }
  desc->remote_qp   = param->dest_qpn;
  desc->remote_qkey = param->dest_qkey;
  desc->ethertype   = param->ethertype;
  desc->eecn        = param->eecn;
  desc->set_se      = param->solicited_event;
}

int tsIbTavorSendPost(
                      tTS_IB_QP         qp,
                      tTS_IB_SEND_PARAM param,
                      int               num_work_requests
                      ) {
  tTS_IB_TAVOR_QP      priv_qp = qp->private;
  tTS_IB_TAVOR_PRIVATE priv = qp->device->private;
  VAPI_ret_t           ret;

  /* We make sure our structure layouts match.  The compiler will
     not generate any code for this unless something is wrong. */
  if (sizeof ((tTS_IB_GATHER_SCATTER) 0)->address     != sizeof ((VAPI_sg_lst_entry_t *) 0)->addr ||
      offsetof(tTS_IB_GATHER_SCATTER_STRUCT, address) != offsetof(VAPI_sg_lst_entry_t, addr)      ||
      sizeof ((tTS_IB_GATHER_SCATTER) 0)->length      != sizeof ((VAPI_sg_lst_entry_t *) 0)->len  ||
      offsetof(tTS_IB_GATHER_SCATTER_STRUCT, length)  != offsetof(VAPI_sg_lst_entry_t, len)       ||
      sizeof ((tTS_IB_GATHER_SCATTER) 0)->key         != sizeof ((VAPI_sg_lst_entry_t *) 0)->lkey ||
      offsetof(tTS_IB_GATHER_SCATTER_STRUCT, key)     != offsetof(VAPI_sg_lst_entry_t, lkey)) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "Struct layout of tTS_IB_GATHER_SCATTER_STRUCT doesn't match VAPI_sg_lst_entry_t");
    return -EINVAL;
  }

  if (num_work_requests == 1) {
    VAPI_sr_desc_t work_request;
    _tsIbTavorSendTranslate(param, &work_request);
    if (qp->qpn != 1) {
      ret = VAPI_post_sr(priv->vapi_handle,
                         priv_qp->qp_handle.kernel_handle,
                         &work_request);
      if (ret != VAPI_OK) {
        TS_REPORT_WARN(MOD_KERNEL_IB,
                       "%s: VAPI_post_sr failed, return code = %d (%s)",
                       qp->device->name, ret, VAPI_strerror(ret));
        return -EINVAL;
      }
    } else {
      ret = EVAPI_post_gsi_sr(priv->vapi_handle,
                              priv_qp->qp_handle.kernel_handle,
                              &work_request,
                              param->pkey_index);
      if (ret != VAPI_OK) {
        TS_REPORT_WARN(MOD_KERNEL_IB,
                       "%s: EVAPI_post_gsi_sr failed, return code = %d (%s)",
                       qp->device->name, ret, VAPI_strerror(ret));
        return -EINVAL;
      }
    }
  } else {
    /* We only support 1 work request at a time for now */
    return -ENOSYS;
  }

  return 0;
}

static inline void _tsIbTavorReceiveTranslate(
                                              tTS_IB_RECEIVE_PARAM param,
                                              VAPI_rr_desc_t      *desc
                                              ) {
  desc->id         = param->work_request_id;
  desc->opcode     = VAPI_RECEIVE;
  desc->comp_type  = param->signaled ? VAPI_SIGNALED : VAPI_UNSIGNALED;
  desc->sg_lst_p   = (VAPI_sg_lst_entry_t *) param->scatter_list;
  desc->sg_lst_len = param->num_scatter_entries;
}

int tsIbTavorReceivePost(
                         tTS_IB_QP            qp,
                         tTS_IB_RECEIVE_PARAM param,
                         int                  num_work_requests
                         ) {
  tTS_IB_TAVOR_QP      priv_qp = qp->private;
  tTS_IB_TAVOR_PRIVATE priv = qp->device->private;
  VAPI_ret_t           ret;

  /* We make sure our structure layouts match.  The compiler will
     not generate any code for this unless something is wrong. */
  if (sizeof ((tTS_IB_GATHER_SCATTER) 0)->address     != sizeof ((VAPI_sg_lst_entry_t *) 0)->addr ||
      offsetof(tTS_IB_GATHER_SCATTER_STRUCT, address) != offsetof(VAPI_sg_lst_entry_t, addr)      ||
      sizeof ((tTS_IB_GATHER_SCATTER) 0)->length      != sizeof ((VAPI_sg_lst_entry_t *) 0)->len  ||
      offsetof(tTS_IB_GATHER_SCATTER_STRUCT, length)  != offsetof(VAPI_sg_lst_entry_t, len)       ||
      sizeof ((tTS_IB_GATHER_SCATTER) 0)->key         != sizeof ((VAPI_sg_lst_entry_t *) 0)->lkey ||
      offsetof(tTS_IB_GATHER_SCATTER_STRUCT, key)     != offsetof(VAPI_sg_lst_entry_t, lkey)) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "Struct layout of tTS_IB_GATHER_SCATTER_STRUCT doesn't match VAPI_sg_lst_entry_t");
    return -EINVAL;
  }

  if (num_work_requests == 1) {
    VAPI_rr_desc_t work_request;
    _tsIbTavorReceiveTranslate(param, &work_request);
    ret = VAPI_post_rr(priv->vapi_handle, priv_qp->qp_handle.kernel_handle, &work_request);
    if (ret != VAPI_OK) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "%s: VAPI_post_rr failed, return code = %d (%s)",
                     qp->device->name, ret, VAPI_strerror(ret));
      return -EINVAL;
    }
  } else {
    /* We only support 1 work request at a time for now */
    return -ENOSYS;
  }

  return 0;
}
