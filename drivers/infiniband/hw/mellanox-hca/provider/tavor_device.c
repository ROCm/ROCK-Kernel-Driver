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

  $Id: tavor_device.c,v 1.8 2004/03/04 02:10:04 roland Exp $
*/

#include "tavor_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>

int tsIbTavorDeviceQuery(
                         tTS_IB_DEVICE            device,
                         tTS_IB_DEVICE_PROPERTIES properties
                         ) {
  tTS_IB_TAVOR_PRIVATE priv = device->private;
  VAPI_ret_t           result;
  VAPI_hca_vendor_t    hca_vendor;
  VAPI_hca_cap_t       hca_cap;

  result = VAPI_query_hca_cap(priv->vapi_handle, &hca_vendor, &hca_cap);
  if (result != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: VAPI_query_hca_cap failed, return code = %d (%s)",
                   device->name, result, VAPI_strerror(result));
    return -EINVAL;
  }

  properties->vendor_id                        = hca_vendor.vendor_id;
  properties->device_id                        = hca_vendor.vendor_part_id;
  properties->hw_rev                           = hca_vendor.hw_ver;
  properties->fw_rev                           = hca_vendor.fw_ver;
  properties->max_qp                           = hca_cap.max_num_qp;
  properties->max_wr_per_qp                    = hca_cap.max_qp_ous_wr;
  properties->max_wr_per_post                  = hca_cap.max_qp_ous_wr;
  properties->max_sg_per_wr                    = hca_cap.max_num_sg_ent;
  properties->max_sg_per_wr_rd                 = hca_cap.max_num_sg_ent_rd;
  properties->max_cq                           = hca_cap.max_num_cq;
  properties->max_mr                           = hca_cap.max_num_mr;
  properties->max_mr_size                      = hca_cap.max_mr_size;
  properties->max_pd                           = hca_cap.max_pd_num;
  properties->page_size_cap                    = hca_cap.page_size_cap;
  properties->num_port                         = hca_cap.phys_port_num;
  properties->max_pkey                         = hca_cap.max_pkeys;
  properties->local_ca_ack_delay               = hca_cap.local_ca_ack_delay;
  properties->max_responder_per_qp             = hca_cap.max_qp_ous_rd_atom;
  properties->max_responder_per_eec            = hca_cap.max_ee_ous_rd_atom;
  properties->max_responder_per_hca            = hca_cap.max_res_rd_atom;
  properties->max_initiator_per_qp             = hca_cap.max_qp_init_rd_atom;
  properties->max_initiator_per_eec            = hca_cap.max_ee_init_rd_atom;
  properties->max_eec                          = hca_cap.max_ee_num;
  properties->max_rdd                          = hca_cap.max_rdd_num;
  properties->max_mw                           = hca_cap.max_mw_num;
  properties->max_raw_ipv6_qp                  = hca_cap.max_raw_ipv6_qp;
  properties->max_raw_ethertype_qp             = hca_cap.max_raw_ethy_qp;
  properties->max_mcg                          = hca_cap.max_mcast_grp_num;
  properties->max_mc_qp                        = hca_cap.max_total_mcast_qp_attach_num;
  properties->max_qp_per_mcg                   = hca_cap.max_mcast_qp_attach_num;
  properties->max_ah                           = hca_cap.max_ah_num;
  properties->max_fmr                          = hca_cap.max_num_fmr;
  properties->max_map_per_fmr                  = hca_cap.max_num_map_per_fmr;
  properties->is_switch                        = 0;
  properties->ah_port_num_check                = !!(hca_cap.flags & VAPI_UD_AV_PORT_ENFORCE_CAP);
  properties->rnr_nak_supported                = !!(hca_cap.flags & VAPI_RC_RNR_NAK_GEN_CAP);
  properties->port_shutdown_supported          = !!(hca_cap.flags & VAPI_SHUTDOWN_PORT_CAP);
  properties->init_type_supported              = !!(hca_cap.flags & VAPI_INIT_TYPE_CAP);
  properties->port_active_event_supported      = !!(hca_cap.flags & VAPI_PORT_ACTIVE_EV_CAP);
  properties->system_image_guid_supported      = !!(hca_cap.flags & VAPI_SYS_IMG_GUID_CAP);
  properties->bad_pkey_counter_supported       = !!(hca_cap.flags & VAPI_BAD_PKEY_COUNT_CAP);
  properties->qkey_violation_counter_supported = !!(hca_cap.flags & VAPI_BAD_QKEY_COUNT_CAP);
  properties->modify_wr_num_supported          = !!(hca_cap.flags & VAPI_RESIZE_OUS_WQE_CAP);
  properties->raw_multicast_supported          = !!(hca_cap.flags & VAPI_RAW_MULTI_CAP);
  properties->apm_supported                    = !!(hca_cap.flags & VAPI_AUTO_PATH_MIG_CAP);
  properties->qp_port_change_supported         = !!(hca_cap.flags & VAPI_CHANGE_PHY_PORT_CAP);

  switch (hca_cap.atomic_cap) {
  case VAPI_ATOMIC_CAP_NONE:
    properties->atomic_support = TS_IB_NO_ATOMIC_OPS;
    break;

  case VAPI_ATOMIC_CAP_HCA:
    properties->atomic_support = TS_IB_ATOMIC_HCA;
    break;

  case VAPI_ATOMIC_CAP_GLOB:
    properties->atomic_support = TS_IB_ATOMIC_ALL;
    break;
  }

  memcpy(properties->node_guid, hca_cap.node_guid, sizeof (tTS_IB_GUID));
  strncpy(properties->name, device->name, TS_IB_DEVICE_NAME_MAX);
  properties->provider = device->provider;

  return 0;
}

int tsIbTavorPortQuery(
                       tTS_IB_DEVICE          device,
                       tTS_IB_PORT            port,
                       tTS_IB_PORT_PROPERTIES properties
                       ) {
  tTS_IB_TAVOR_PRIVATE priv = device->private;
  VAPI_ret_t           result;
  VAPI_hca_port_t      hca_port;

  result = VAPI_query_hca_port_prop(priv->vapi_handle, port, &hca_port);
  if (result != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: VAPI_query_hca_port_prop failed, return code = %d (%s)",
                   device->name, result, VAPI_strerror(result));
    return -EINVAL;
  }

  properties->max_mtu                = hca_port.max_mtu;
  properties->max_message_size       = hca_port.max_msg_sz;
  properties->lid                    = hca_port.lid;
  properties->lmc                    = hca_port.lmc;
  properties->port_state             = hca_port.state;
  properties->gid_table_length       = hca_port.gid_tbl_len;
  properties->pkey_table_length      = hca_port.pkey_tbl_len;
  properties->max_vl                 = hca_port.max_vl_num;
  properties->bad_pkey_counter       = hca_port.bad_pkey_counter;
  properties->qkey_violation_counter = hca_port.qkey_viol_counter;
  properties->init_type_reply        = hca_port.initTypeReply;
  properties->sm_lid                 = hca_port.sm_lid;
  properties->sm_sl                  = hca_port.sm_sl;
  properties->subnet_timeout         = hca_port.subnet_timeout;
  properties->capability_mask        = hca_port.capability_mask;

  /* Sometimes we see THCA return 16 for the GID table length.  Print
     a warning the first time this happens, just so we can try to find
     a pattern/cause. */
  if (properties->gid_table_length != 32) {
    static int warned;

    if (!warned++) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "WARNING: VAPI_query_hca_port_prop() returned %d for gid_tbl_len.",
                     hca_port.gid_tbl_len);
    }
  }

  return 0;
}

int tsIbTavorPortModify(
                        tTS_IB_DEVICE              device,
                        tTS_IB_PORT                port,
                        tTS_IB_PORT_PROPERTIES_SET properties
                        ) {
  tTS_IB_TAVOR_PRIVATE priv = device->private;
  VAPI_ret_t           result;
  VAPI_hca_attr_t      hca_attr;
  VAPI_hca_attr_mask_t mask;

  if (properties->valid_fields & (TS_IB_PORT_SHUTDOWN_PORT | TS_IB_PORT_INIT_TYPE)) {
    return -EOPNOTSUPP;
  }

  HCA_ATTR_MASK_CLR_ALL(mask);

  /* Mellanox VAPI doesn't have flag value for Q_Key violation counter */
  hca_attr.reset_qkey_counter =
    !!(properties->valid_fields & TS_IB_PORT_QKEY_VIOLATION_COUNTER_RESET) &&
    properties->qkey_violation_counter_reset;

  if (properties->valid_fields & TS_IB_PORT_IS_SM) {
    hca_attr.is_sm = properties->is_sm;
    HCA_ATTR_MASK_SET(mask, HCA_ATTR_IS_SM);
  }

  if (properties->valid_fields & TS_IB_PORT_IS_SNMP_TUNNELING_SUPPORTED) {
    hca_attr.is_snmp_tun_sup = properties->is_snmp_tunneling_supported;
    HCA_ATTR_MASK_SET(mask, HCA_ATTR_IS_SNMP_TUN_SUP);
  }

  if (properties->valid_fields & TS_IB_PORT_IS_DEVICE_MANAGEMENT_SUPPORTED) {
    hca_attr.is_dev_mgt_sup = properties->is_device_management_supported;
    HCA_ATTR_MASK_SET(mask, HCA_ATTR_IS_DEV_MGT_SUP);
  }

  if (properties->valid_fields & TS_IB_PORT_IS_VENDOR_CLASS_SUPPORTED) {
    hca_attr.is_vendor_cls_sup = properties->is_vendor_class_supported;
    HCA_ATTR_MASK_SET(mask, HCA_ATTR_IS_VENDOR_CLS_SUP);
  }

  result = VAPI_modify_hca_attr(priv->vapi_handle,
                                port,
                                &hca_attr,
                                &mask);

  if (result != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: VAPI_modify_hca_attr failed, return code = %d (%s)",
                   device->name, result, VAPI_strerror(result));
    return -EINVAL;
  }

  return 0;
}

int tsIbTavorPkeyQuery(
                       tTS_IB_DEVICE device,
                       tTS_IB_PORT   port,
                       int           index,
                       tTS_IB_PKEY  *pkey
                       ) {
  tTS_IB_TAVOR_PRIVATE priv = device->private;
  VAPI_ret_t           result;
  uint16_t             len;
  tTS_IB_PKEY         *pkey_tbl;

  result = VAPI_query_hca_pkey_tbl(priv->vapi_handle,
                                   port,
                                   0,
                                   &len,
                                   NULL);
  if (result != VAPI_EAGAIN) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: VAPI_query_hca_pkey_tbl failed, return code = %d (%s)",
                   device->name, result, VAPI_strerror(result));
    return -EINVAL;
  }

  if (index >= len) {
    return -EINVAL;
  }

  pkey_tbl = kmalloc(len * sizeof *pkey_tbl, GFP_KERNEL);
  if (!pkey_tbl) {
    return -ENOMEM;
  }

  result = VAPI_query_hca_pkey_tbl(priv->vapi_handle,
                                   port,
                                   len,
                                   &len,
                                   pkey_tbl);
  if (result != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: VAPI_query_hca_pkey_tbl failed, return code = %d (%s)",
                   device->name, result, VAPI_strerror(result));
    kfree(pkey_tbl);
    return -EINVAL;
  }

  *pkey = pkey_tbl[index];
  kfree(pkey_tbl);
  return 0;
}

int tsIbTavorGidQuery(
                      tTS_IB_DEVICE device,
                      tTS_IB_PORT   port,
                      int           index,
                      tTS_IB_GID    gid
                      ) {
  tTS_IB_TAVOR_PRIVATE priv = device->private;
  VAPI_ret_t           result;
  uint16_t             len;
  tTS_IB_GID          *gid_tbl;

  result = VAPI_query_hca_gid_tbl(priv->vapi_handle,
                                  port,
                                  0,
                                  &len,
                                  NULL);
  if (result != VAPI_EAGAIN) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: VAPI_query_hca_gid_tbl failed, return code = %d (%s)",
                   device->name, result, VAPI_strerror(result));
    return -EINVAL;
  }

  if (index >= len) {
    return -EINVAL;
  }

  gid_tbl = kmalloc(len * sizeof (tTS_IB_GID), GFP_KERNEL);
  if (!gid_tbl) {
    return -ENOMEM;
  }

  result = VAPI_query_hca_gid_tbl(priv->vapi_handle,
                                  port,
                                  len,
                                  &len,
                                  gid_tbl);
  if (result != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: VAPI_query_hca_gid_tbl failed, return code = %d (%s)",
                   device->name, result, VAPI_strerror(result));
    kfree(gid_tbl);
    return -EINVAL;
  }

  memcpy(gid, gid_tbl[index], sizeof (tTS_IB_GID));
  kfree(gid_tbl);
  return 0;
}

int tsIbTavorDeviceQueryVapiHandle(
                                   tTS_IB_DEVICE            device,
                                   VAPI_hca_hndl_t         *vapi_handle
                                   ) {
  tTS_IB_TAVOR_PRIVATE priv = device->private;

  *vapi_handle = priv->vapi_handle;

  return 0;
}

EXPORT_SYMBOL(tsIbTavorDeviceQueryVapiHandle);
