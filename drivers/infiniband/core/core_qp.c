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

  $Id: core_qp.c,v 1.4 2004/02/25 00:35:17 roland Exp $
*/

#include "core_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/slab.h>

int tsIbQpCreate(
                 tTS_IB_QP_CREATE_PARAM param,
                 tTS_IB_QP_HANDLE      *qp_handle,
                 tTS_IB_QPN            *qpn
                 ) {
  tTS_IB_PD pd;
  tTS_IB_QP qp;
  int       ret;

  TS_IB_CHECK_MAGIC(param->pd, PD);
  pd = param->pd;

  if (!pd->device->qp_create) {
    return -ENOSYS;
  }

  qp = kmalloc(sizeof *qp, GFP_KERNEL);
  if (!qp) {
    return -ENOMEM;
  }

  INIT_LIST_HEAD(&qp->async_handler_list);
  spin_lock_init(&qp->async_handler_lock);

  ret = pd->device->qp_create(pd, param, qp);

  if (!ret) {
    TS_IB_SET_MAGIC(qp, QP);
    qp->device = pd->device;
    *qp_handle = qp;
    *qpn       = qp->qpn;
  } else {
    kfree(qp);
  }

  return ret;
}

int tsIbSpecialQpCreate(
                        tTS_IB_QP_CREATE_PARAM param,
                        tTS_IB_PORT            port,
                        tTS_IB_SPECIAL_QP_TYPE qp_type,
                        tTS_IB_QP_HANDLE      *qp_handle
                        ) {
  tTS_IB_PD pd;
  tTS_IB_QP qp;
  int       ret;

  TS_IB_CHECK_MAGIC(param->pd, PD);
  pd = param->pd;

  if (!pd->device->special_qp_create) {
    return -ENOSYS;
  }

  qp = kmalloc(sizeof *qp, GFP_KERNEL);
  if (!qp) {
    return -ENOMEM;
  }

  INIT_LIST_HEAD(&qp->async_handler_list);
  spin_lock_init(&qp->async_handler_lock);

  ret = pd->device->special_qp_create(pd, param, port, qp_type, qp);

  if (!ret) {
    TS_IB_SET_MAGIC(qp, QP);
    qp->device = pd->device;
    *qp_handle = qp;
  } else {
    kfree(qp);
  }

  return ret;
}

int tsIbQpModify(
                 tTS_IB_QP_HANDLE    qp_handle,
                 tTS_IB_QP_ATTRIBUTE attr
                 ) {
  tTS_IB_QP qp = qp_handle;
  TS_IB_CHECK_MAGIC(qp, QP);
  return qp->device->qp_modify ? qp->device->qp_modify(qp, attr) : -ENOSYS;
}

int tsIbQpQuery(
                tTS_IB_QP_HANDLE    qp_handle,
                tTS_IB_QP_ATTRIBUTE attr
                ) {
  tTS_IB_QP qp = qp_handle;
  TS_IB_CHECK_MAGIC(qp, QP);
  return qp->device->qp_query ? qp->device->qp_query(qp, attr) : -ENOSYS;
}

int tsIbQpQueryQpn(
                   tTS_IB_QP_HANDLE qp_handle,
                   tTS_IB_QPN      *qpn
                   ) {
  tTS_IB_QP qp = qp_handle;
  TS_IB_CHECK_MAGIC(qp, QP);

  *qpn = qp->qpn;
  return 0;
}

int tsIbQpDestroy(
                  tTS_IB_QP_HANDLE qp_handle
                  ) {
  tTS_IB_QP qp = qp_handle;
  int       ret;

  TS_IB_CHECK_MAGIC(qp, QP);

  if (!qp->device->qp_destroy) {
    return -ENOSYS;
  }

  if (!list_empty(&qp->async_handler_list)) {
    return -EBUSY;
  }

  ret = qp->device->qp_destroy(qp);
  if (!ret) {
    TS_IB_CLEAR_MAGIC(qp);
    kfree(qp);
  }

  return ret;
}

int tsIbSend(
             tTS_IB_QP_HANDLE  qp_handle,
             tTS_IB_SEND_PARAM param,
             int               num_work_requests
             ) {
  tTS_IB_QP qp = qp_handle;
  TS_IB_CHECK_MAGIC(qp, QP);
  return qp->device->send_post ? qp->device->send_post(qp, param, num_work_requests) : -ENOSYS;
}

int tsIbReceive(
                tTS_IB_QP_HANDLE     qp_handle,
                tTS_IB_RECEIVE_PARAM param,
                int                  num_work_requests
                ) {
  tTS_IB_QP qp = qp_handle;
  TS_IB_CHECK_MAGIC(qp, QP);
  return qp->device->receive_post ? qp->device->receive_post(qp, param, num_work_requests) : -ENOSYS;
}
