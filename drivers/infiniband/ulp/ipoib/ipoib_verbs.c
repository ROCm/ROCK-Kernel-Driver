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

  $Id: ipoib_verbs.c,v 1.23 2004/02/25 00:37:52 roland Exp $
*/

#include "ipoib.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_cache.h"

/* =============================================================== */
/*..tsIpoibPkeyFind - get index of a P_Key                         */
static int tsIpoibPkeyFind(
                           tTS_IB_DEVICE_HANDLE device,
                           tTS_IB_PORT port,
                           tTS_IB_PKEY pkey,
                           int        *pkey_index
                           ) {
  tTS_IB_PKEY t;
  int ret = -ENOENT;

  *pkey_index = 0;
  while (!tsIbPkeyEntryGet(device, port, *pkey_index, &t)) {
    t &= 0x7fff;

    if (t && t == (pkey & 0x7fff)) {
      return 0;
    }

    ++*pkey_index;
  }

  {
    tTS_IB_DEVICE_PROPERTIES_STRUCT props;

     tsIbDevicePropertiesGet(device, &props);
    TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_GEN,
             "Couldn't find P_Key 0x%04x for %s port %d",
             pkey, props.name, port);
  }

  return ret;
}

/* =================================================================== */
/*.. tsIpoibDeviceCheckPkeyPresence - Check for the interface P_Key presence */
void tsIpoibDeviceCheckPkeyPresence(
                                    struct net_device *dev
                                    ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  int pkey_index = 0;

  if (tsIpoibPkeyFind(priv->ca, priv->port, priv->pkey, &pkey_index))
    clear_bit(TS_IPOIB_PKEY_ASSIGNED, &priv->flags);
  else
    set_bit(TS_IPOIB_PKEY_ASSIGNED, &priv->flags);

} /* tsIpoibPkeyPresent */

int tsIpoibDeviceMulticastAttach(
                                 struct net_device *dev,
                                 tTS_IB_LID mlid,
                                 tTS_IB_GID mgid
                                 ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  tTS_IB_QP_ATTRIBUTE qp_attr;
  int ret, pkey_index;

  ret = -ENOMEM;
  qp_attr = kmalloc(sizeof *qp_attr, GFP_ATOMIC);
  if (!qp_attr) {
    goto out;
  }

  if (tsIpoibPkeyFind(priv->ca, priv->port, priv->pkey, &pkey_index)) {
    clear_bit(TS_IPOIB_PKEY_ASSIGNED, &priv->flags);
    ret = -ENXIO;
    goto out;
  }
  set_bit(TS_IPOIB_PKEY_ASSIGNED, &priv->flags);

  /* set correct QKey for QP */
  qp_attr->qkey = priv->qkey;
  qp_attr->valid_fields = TS_IB_QP_ATTRIBUTE_QKEY;
  ret = tsIbQpModify(priv->qp, qp_attr);
  if (ret) {
    TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to modify QP, ret = %d",
                    dev->name, ret);
    return ret;
  }

  /* attach QP to multicast group */
  down(&priv->mcast_mutex);
  ret = tsIbMulticastAttach(mlid, mgid, priv->qp);
  up(&priv->mcast_mutex);
  if (ret) {
    TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to attach to multicast group, ret = %d",
                    dev->name, ret);
    return ret;
  }

 out:
  kfree(qp_attr);
  return ret;
}

int tsIpoibDeviceMulticastDetach(
                                 struct net_device *dev,
                                 tTS_IB_LID mlid,
                                 tTS_IB_GID mgid
                                 ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  int ret;

  down(&priv->mcast_mutex);
  ret = tsIbMulticastDetach(mlid, mgid, priv->qp);
  up(&priv->mcast_mutex);
  if (ret) {
    TS_REPORT_WARN(MOD_IB_NET,
                   "%s: tsIbMulticastDetach failed (result = %d)",
                   dev->name, ret);
  }

  return ret;
}

int tsIpoibDeviceTransportQpCreate(
                                   struct net_device *dev
                                   ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  int ret, pkey_index;
  tTS_IB_QP_CREATE_PARAM_STRUCT qp_create = {
    .limit = {
      .max_outstanding_send_request       = TS_IPOIB_TX_RING_SIZE,
      .max_outstanding_receive_request    = TS_IPOIB_RX_RING_SIZE,
      .max_send_gather_element            = 1,
      .max_receive_scatter_element        = 1,
    },
    .pd = priv->pd,
    .send_queue = priv->cq,
    .receive_queue = priv->cq,
    .transport = TS_IB_TRANSPORT_UD,
  };
  tTS_IB_QP_ATTRIBUTE_STRUCT qp_attr;

  /* Search trought the port P_Key table for the requested pkey value.      */
  /* The port has to be assigned to the respective IB partition in advance. */
  ret = tsIpoibPkeyFind(priv->ca, priv->port, priv->pkey, &pkey_index);

  if (ret) {
    clear_bit(TS_IPOIB_PKEY_ASSIGNED, &priv->flags);
    return ret;
  }
  set_bit(TS_IPOIB_PKEY_ASSIGNED, &priv->flags);

  ret = tsIbQpCreate(&qp_create,
                     &priv->qp,
                     &priv->local_qpn);
  if (ret) {
    TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to create QP",
                    dev->name);
    return ret;
  }

  qp_attr.state      = TS_IB_QP_STATE_INIT;
  qp_attr.qkey       = 0;
  qp_attr.port       = priv->port;
  qp_attr.pkey_index = pkey_index;
  qp_attr.valid_fields = TS_IB_QP_ATTRIBUTE_QKEY
    | TS_IB_QP_ATTRIBUTE_PORT
    | TS_IB_QP_ATTRIBUTE_PKEY_INDEX
    | TS_IB_QP_ATTRIBUTE_STATE;
  ret = tsIbQpModify(priv->qp, &qp_attr);
  if (ret) {
    TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to modify QP to init, ret = %d",
                    dev->name, ret);
    goto out_fail;
  }

  qp_attr.state    = TS_IB_QP_STATE_RTR;
  /* Can't set this in a INIT->RTR transition */
  qp_attr.valid_fields &= ~TS_IB_QP_ATTRIBUTE_PORT;
  ret = tsIbQpModify(priv->qp, &qp_attr);
  if (ret) {
    TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to modify QP to RTR, ret = %d",
                    dev->name, ret);
    goto out_fail;
  }

  qp_attr.state = TS_IB_QP_STATE_RTS;
  qp_attr.send_psn = 0x12345678;
  qp_attr.send_psn = 0;
  qp_attr.valid_fields |= TS_IB_QP_ATTRIBUTE_SEND_PSN;
  qp_attr.valid_fields &= ~TS_IB_QP_ATTRIBUTE_PKEY_INDEX;
  ret = tsIbQpModify(priv->qp, &qp_attr);
  if (ret) {
    TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to modify QP to RTS, ret = %d",
                    dev->name, ret);
    goto out_fail;
  }

  return 0;

 out_fail:
  tsIbQpDestroy(priv->qp);
  priv->qp = TS_IB_HANDLE_INVALID;

  return -EINVAL;
}

void tsIpoibDeviceTransportQpDestroy(
                                     struct net_device *dev
                                     ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  if (tsIbQpDestroy(priv->qp)) {
    TS_REPORT_WARN(MOD_IB_NET,
                   "%s: tsIbQpDestroy failed",
                   dev->name);
  }
  priv->qp = TS_IB_HANDLE_INVALID;
}

int tsIpoibDeviceTransportInit(
                               struct net_device *dev,
                               tTS_IB_DEVICE_HANDLE ca
                               ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  tTS_IB_CQ_CALLBACK_STRUCT cq_callback = {
    .context = TS_IB_CQ_CALLBACK_PROCESS,
    .policy  = TS_IB_CQ_PROVIDER_REARM,
    .function = {
      .entry = tsIpoibDriverCompletion,
    },
    .arg     = dev,
  };
  int entries;

  if (tsIbPdCreate(priv->ca, NULL, &priv->pd)) {
    TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to allocate PD",
                    dev->name);
    return -ENODEV;
  }

  entries = TS_IPOIB_TX_RING_SIZE + TS_IPOIB_RX_RING_SIZE + 1;
  if (tsIbCqCreate(priv->ca, &entries, &cq_callback, NULL, &priv->cq)) {
    TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to create CQ",
                    dev->name);
    goto out_free_pd;
  }
  TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_GEN,
           "%s: CQ with %d entries", dev->name, entries);

  {
    /* XXX we assume physical memory starts at address 0. */
    tTS_IB_PHYSICAL_BUFFER_STRUCT buffer_list = {
      .address = 0,
      .size    = 1
    };
    uint64_t dummy_iova = PAGE_OFFSET;
    unsigned long tsize = (unsigned long) high_memory - PAGE_OFFSET;
    tTS_IB_RKEY rkey;

    /* make our region have size the size of low memory rounded up to
       the next power of 2 (so we use as few TPT entries as possible) */
    while (tsize) {
      buffer_list.size <<= 1;
      tsize            >>= 1;
    }

    if (tsIbMemoryRegisterPhysical(priv->pd,
                                   &buffer_list,
                                   1, /* list_len */
                                   &dummy_iova,
                                   buffer_list.size,
                                   0, /* iova_offset */
                                   TS_IB_ACCESS_LOCAL_WRITE | TS_IB_ACCESS_REMOTE_WRITE,
                                   &priv->mr,
                                   &priv->lkey,
                                   &rkey)) {
      TS_REPORT_FATAL(MOD_IB_NET,
                      "%s: tsIbMemoryRegisterPhysical failed",
                      dev->name);
      goto out_free_cq;
    }
  }

  return 0;

 out_free_cq:
  tsIbCqDestroy(priv->cq);

 out_free_pd:
  tsIbPdDestroy(priv->pd);
  return -ENODEV;
}

void tsIpoibDeviceTransportCleanup(
                                   struct net_device *dev
                                   )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  if (priv->qp != TS_IB_HANDLE_INVALID) {
    if (tsIbQpDestroy(priv->qp)) {
      TS_REPORT_WARN(MOD_IB_NET,
                     "%s: tsIbQpDestroy failed",
                     dev->name);
    }

    priv->qp = TS_IB_HANDLE_INVALID;
    clear_bit(TS_IPOIB_PKEY_ASSIGNED, &priv->flags);
  }

  if (tsIbMemoryDeregister(priv->mr)) {
    TS_REPORT_WARN(MOD_IB_NET,
                   "%s: tsIbMemoryDeregister failed",
                   dev->name);
  }

  if (tsIbCqDestroy(priv->cq)) {
    TS_REPORT_WARN(MOD_IB_NET,
                   "%s: tsIbCqDestroy failed",
                   dev->name);
  }

  if (tsIbPdDestroy(priv->pd)) {
    TS_REPORT_WARN(MOD_IB_NET,
                   "%s: tsIbPdDestroy failed",
                   dev->name);
  }
}

static void _tsIpoibAsyncEvent(
                               tTS_IB_ASYNC_EVENT_RECORD record,
                               void *arg
                               )
{
  struct list_head *tmp;

  switch (record->event) {
  case TS_IB_PORT_ACTIVE:
    TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_GEN,
             "Port Active Event on %d", record->device);

    list_for_each(tmp, &ipoib_device_list) {
      struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = list_entry(tmp, struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT, list);

      if (priv->ca == record->device
          && priv->port == record->modifier.port) {
        tsIpoibDeviceIbFlush(&priv->dev);
      }
    }
    break;

  default:
    TS_REPORT_WARN(MOD_IB_NET,
                   "Unexpected event %d", record->event);
    break;
  }
}

int tsIpoibTransportCreateDevices(
                                  void
                                  )
{
  int i, j;
  tTS_IB_DEVICE_HANDLE hca_device;

  for (i = 0;
       (hca_device = tsIbDeviceGetByIndex(i)) != TS_IB_HANDLE_INVALID;
       ++i) {
    tTS_IB_DEVICE_PROPERTIES_STRUCT props;

    if (tsIbDevicePropertiesGet(hca_device, &props)) {
      TS_REPORT_FATAL(MOD_IB_NET, "tsIbDevicePropertiesGet() failed");
      return -EINVAL;
    }

    TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_FLOW_CONFIG,
             "device %d: name %s", i, props.name);

    if (props.is_switch) {
      tsIpoIbDriverAddPort("ib%d", hca_device, 0, 0);
    } else {
      for (j = 0; j < props.num_port; j++) {
        tsIpoIbDriverAddPort("ib%d", hca_device, j + 1, 0);
      }
    }

#ifndef TS_HOST_DRIVER
    /*
     * Now we setup interfaces with a prefix of "ts". These will be
     * used by the chassis to communicate between cards and sits on a
     * different multicast domain.
     */
    if (props.is_switch) {
      tsIpoIbDriverAddPort("ts%d", hca_device, 0, 1);
    } else {
      for (j = 0; j < props.num_port; j++) {
        tsIpoIbDriverAddPort("ts%d", hca_device, j + 1, 1);
      }
    }
#endif
  }

  return 0;
}

int tsIpoibTransportPortMonitorStart(
                                     struct net_device *dev
                                     )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  tTS_IB_ASYNC_EVENT_RECORD_STRUCT event_record = {
    .device = priv->ca,
    .event  = TS_IB_PORT_ACTIVE,
  };

  if (tsIbAsyncEventHandlerRegister(&event_record,
                                    _tsIpoibAsyncEvent,
                                    priv,
                                    &priv->active_handler)) {
    TS_REPORT_FATAL(MOD_IB_NET,
                    "tsIbAsynchronousEventHandlerRegister failed for TS_IB_PORT_ACTIVE");
    return -EINVAL;
  }

  return 0;
}

void tsIpoibTransportPortMonitorStop(
                                     struct net_device *dev
                                     )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  tsIbAsyncEventHandlerDeregister(priv->active_handler);
}
