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

  $Id: sa_client_port_info.c 37 2004-04-10 17:11:21Z roland $
*/

#include "sa_client.h"

#include "ts_ib_mad.h"
#include "ts_ib_rmpp_mad_types.h"

#include "ts_kernel_trace.h"

#ifndef W2K_OS
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include <asm/system.h>
#include <asm/byteorder.h>
#endif

typedef struct tTS_IB_SA_PORT_INFO_STRUCT tTS_IB_SA_PORT_INFO_STRUCT,
  *tTS_IB_SA_PORT_INFO;

typedef struct tTS_IB_SA_PORT_INFO_QUERY_STRUCT tTS_IB_SA_PORT_INFO_QUERY_STRUCT,
  *tTS_IB_SA_PORT_INFO_QUERY;

struct tTS_IB_SA_PORT_INFO_STRUCT {
  tTS_IB_LID      port_lid;
  tTS_IB_PORT     port_num;
  uint8_t         reserved;
  tTS_IB_MKEY     mkey;
  uint8_t         gid_prefix[8];
  tTS_IB_LID      lid;
  tTS_IB_LID      master_sm_lid;
  uint32_t        capability_mask;
  uint16_t        diag_code;
  uint16_t        mkey_lease_period;
  uint8_t         local_port_num;
  uint8_t         link_width_enable;
  uint8_t         link_width_supported;
  uint8_t         link_width_active;
  uint8_t         link_speed_supported__port_state;
  uint8_t         port_phy_state__link_down_default_state;
  uint8_t         mkey_protect_bits__reserve__lmc;
  uint8_t         link_speed_active__linke_speed_enabled;
  uint8_t         nighbor_mtu__master_smsl;
  uint8_t         vlcap__reserved;
  uint8_t         vlhigh_limit;
  uint8_t         vl_arbitration_high_cap;
  uint8_t         vl_arbitration_low_cap;
  uint8_t         reserve__mtu_cap;
  uint8_t         vl_stall_count__hoqlife;
  uint8_t         operational_vl__pin__pout__fin__fout;
  uint16_t        mkey_violation;
  uint16_t        pkey_violation;
  uint16_t        qkey_violation;
  uint8_t         guid_cap;
  uint8_t         reserve__subnet_timeout;
  uint8_t         reserve__resp_time_value;
  uint8_t         local_phy_error__overrun_error;
} __attribute__((packed));

struct tTS_IB_SA_PORT_INFO_QUERY_STRUCT {
  tTS_IB_CLIENT_QUERY_TID                 transaction_id;
  tTS_IB_PORT_INFO_QUERY_COMPLETION_FUNC  completion_func;
  void *                                  completion_arg;
};

#define ORACLE_DEMO_LANE15_HACK

static void _tsIbPortInfoResponse(
                                  tTS_IB_CLIENT_RESPONSE_STATUS status,
                                  tTS_IB_MAD packet,
                                  void *query_ptr
                                  ) {
  tTS_IB_SA_PORT_INFO_QUERY query = query_ptr;

  switch (status) {
    case TS_IB_CLIENT_RESPONSE_OK:
    {
      tTS_IB_SA_PAYLOAD sa_payload = (tTS_IB_SA_PAYLOAD) &packet->payload;
      tTS_IB_SA_PORT_INFO port_info_ptr = (tTS_IB_SA_PORT_INFO) sa_payload->admin_data;
      tTS_IB_PORT_INFO_STRUCT port_info;

      memcpy(port_info.mkey, port_info_ptr->mkey, 8);
      memcpy(port_info.gid_prefix, port_info_ptr->gid_prefix, 8);
      port_info.lid = be16_to_cpu(port_info_ptr->lid);
      port_info.master_sm_lid = be16_to_cpu(port_info_ptr->master_sm_lid);
      port_info.capability_mask = be32_to_cpu(port_info_ptr->capability_mask);
      port_info.local_port_num = port_info_ptr->local_port_num;
      port_info.link_width_enabled = port_info_ptr->link_width_enable;
      port_info.link_width_supported = port_info_ptr->link_width_supported;
      port_info.link_width_active = port_info_ptr->link_width_active;
      port_info.link_speed_supported = (port_info_ptr->link_speed_supported__port_state & 0xF0) >> 4;
      port_info.port_state = port_info_ptr->link_speed_supported__port_state & 0x0F;
      port_info.phy_state = (port_info_ptr->port_phy_state__link_down_default_state & 0xF0) >> 4;
      port_info.down_default_state = port_info_ptr->port_phy_state__link_down_default_state & 0x0F;
      port_info.mkey_protect = port_info_ptr->mkey_protect_bits__reserve__lmc >> 6;
      port_info.lmc = port_info_ptr->mkey_protect_bits__reserve__lmc & 0x07;
      port_info.link_speed_active = (port_info_ptr->link_speed_active__linke_speed_enabled & 0xF0) >> 4;
      port_info.link_speed_enabled = port_info_ptr->link_speed_active__linke_speed_enabled & 0x0F;
      port_info.neighbor_mtu = port_info_ptr->nighbor_mtu__master_smsl >> 4;
      port_info.master_sm_sl = port_info_ptr->nighbor_mtu__master_smsl & 0x0F;
      port_info.vl_cap = port_info_ptr->vlcap__reserved >> 4;
      port_info.vl_high_limit = port_info_ptr->vlhigh_limit;
      port_info.vl_arb_high_cap = port_info_ptr->vl_arbitration_high_cap;
      port_info.vl_arb_low_cap = port_info_ptr->vl_arbitration_low_cap;
      port_info.mtu_cap = port_info_ptr->reserve__mtu_cap & 0x0F;
      port_info.vl_stall_count = port_info_ptr->vl_stall_count__hoqlife >> 3;
      port_info.hoq_life = port_info_ptr->vl_stall_count__hoqlife & 0x7;
      port_info.operational_vl = port_info_ptr->operational_vl__pin__pout__fin__fout >> 4;
      port_info.partition_enforcement_inbound = (port_info_ptr->operational_vl__pin__pout__fin__fout & 0x08) >> 3;
      port_info.partition_enforcement_outbound = (port_info_ptr->operational_vl__pin__pout__fin__fout & 0x04) >> 2;
      port_info.filter_raw_inbound = (port_info_ptr->operational_vl__pin__pout__fin__fout & 0x02) >> 1;
      port_info.filter_raw_outbound = port_info_ptr->operational_vl__pin__pout__fin__fout & 0x01;
      port_info.mkey_violations = be16_to_cpu(port_info_ptr->mkey_violation);
      port_info.pkey_violations = be16_to_cpu(port_info_ptr->pkey_violation);
      port_info.qkey_violations = be16_to_cpu(port_info_ptr->qkey_violation);
      port_info.guid_cap = port_info_ptr->guid_cap;
      port_info.subnet_timeout = port_info_ptr->reserve__subnet_timeout & 0x1F;
      port_info.resp_time_val = port_info_ptr->reserve__resp_time_value & 0x1F;
      port_info.local_phy_errs = port_info_ptr->local_phy_error__overrun_error >> 4;
      port_info.overrun_errs = port_info_ptr->local_phy_error__overrun_error & 0x0F;

      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "SA client Port Info response: "
               "lid=0x%04x, capability mask=0x%08x",
               port_info.lid, port_info.capability_mask);

      if (query->completion_func) {
        query->completion_func(query->transaction_id,
                               0,
                               &port_info,
                               query->completion_arg);
      }
    }
    break;

    case TS_IB_CLIENT_RESPONSE_ERROR:
      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "SA client port info MAD status 0x%04x",
               be16_to_cpu(packet->status));
      if (query->completion_func) {
        query->completion_func(query->transaction_id,
                               -EINVAL,
                               NULL,
                               query->completion_arg);
      }
      break;

    case TS_IB_CLIENT_RESPONSE_TIMEOUT:
      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "SA client port info query timed out");
      if (query->completion_func) {
        query->completion_func(query->transaction_id,
                               -ETIMEDOUT,
                               NULL,
                               query->completion_arg);
      }
      break;

    case TS_IB_CLIENT_RESPONSE_CANCEL:
      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "SA client port info query canceled");
      break;

    default:
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "Unknown status %d", status);
      break;
  }

  kfree(query);
}

static void _tsIbPortInfoTblResponse(tTS_IB_CLIENT_RESPONSE_STATUS status,
                                     uint8_t *header,
                                     uint8_t *data,
                                     uint32_t data_size,
                                     void *query_ptr)
{
  tTS_IB_SA_PORT_INFO_QUERY query = (tTS_IB_SA_PORT_INFO_QUERY) query_ptr;

  switch (status) {
    case TS_IB_CLIENT_RESPONSE_OK:
    {
      tTS_IB_SA_HEADER sa_header = (tTS_IB_SA_HEADER)header;
      int16_t attrib_offset = be16_to_cpu(sa_header->attrib_offset) * 8;
      int32_t port_info_size = sizeof(tTS_IB_SA_PORT_INFO_STRUCT);
      int32_t num_entries = 0;
      int32_t i;

      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "_tsIbPortInfoTblResponse(data_size= %d, port-info-size= %d, attrib_offset= %d, num-entries= %d)\n",
               data_size, port_info_size, attrib_offset, num_entries);
      
      if (attrib_offset > 0)
      {
        num_entries = data_size / attrib_offset;

        for (i = 0; i < num_entries; i++)
        {
          tTS_IB_SA_PORT_INFO port_info_ptr = (tTS_IB_SA_PORT_INFO) (data + i * attrib_offset);
          tTS_IB_PORT_INFO_STRUCT port_info;

          memcpy(port_info.mkey, port_info_ptr->mkey, 8);
          memcpy(port_info.gid_prefix, port_info_ptr->gid_prefix, 8);
          port_info.lid = be16_to_cpu(port_info_ptr->lid);
          port_info.master_sm_lid = be16_to_cpu(port_info_ptr->master_sm_lid);
          port_info.capability_mask = be32_to_cpu(port_info_ptr->capability_mask);
          port_info.local_port_num = port_info_ptr->local_port_num;
          port_info.link_width_enabled = port_info_ptr->link_width_enable;
          port_info.link_width_supported = port_info_ptr->link_width_supported;
          port_info.link_width_active = port_info_ptr->link_width_active;
          port_info.link_speed_supported = (port_info_ptr->link_speed_supported__port_state & 0xF0) >> 4;
          port_info.port_state = port_info_ptr->link_speed_supported__port_state & 0x0F;
          port_info.phy_state = (port_info_ptr->port_phy_state__link_down_default_state & 0xF0) >> 4;
          port_info.down_default_state = port_info_ptr->port_phy_state__link_down_default_state & 0x0F;
          port_info.mkey_protect = port_info_ptr->mkey_protect_bits__reserve__lmc >> 6;
          port_info.lmc = port_info_ptr->mkey_protect_bits__reserve__lmc & 0x07;
          port_info.link_speed_active = (port_info_ptr->link_speed_active__linke_speed_enabled & 0xF0) >> 4;
          port_info.link_speed_enabled = port_info_ptr->link_speed_active__linke_speed_enabled & 0x0F;
          port_info.neighbor_mtu = port_info_ptr->nighbor_mtu__master_smsl >> 4;
          port_info.master_sm_sl = port_info_ptr->nighbor_mtu__master_smsl & 0x0F;
          port_info.vl_cap = port_info_ptr->vlcap__reserved >> 4;
          port_info.vl_high_limit = port_info_ptr->vlhigh_limit;
          port_info.vl_arb_high_cap = port_info_ptr->vl_arbitration_high_cap;
          port_info.vl_arb_low_cap = port_info_ptr->vl_arbitration_low_cap;
          port_info.mtu_cap = port_info_ptr->reserve__mtu_cap & 0x0F;
          port_info.vl_stall_count = port_info_ptr->vl_stall_count__hoqlife >> 3;
          port_info.hoq_life = port_info_ptr->vl_stall_count__hoqlife & 0x7;
          port_info.operational_vl = port_info_ptr->operational_vl__pin__pout__fin__fout >> 4;
          port_info.partition_enforcement_inbound = (port_info_ptr->operational_vl__pin__pout__fin__fout & 0x08) >> 3;
          port_info.partition_enforcement_outbound = (port_info_ptr->operational_vl__pin__pout__fin__fout & 0x04) >> 2;
          port_info.filter_raw_inbound = (port_info_ptr->operational_vl__pin__pout__fin__fout & 0x02) >> 1;
          port_info.filter_raw_outbound = port_info_ptr->operational_vl__pin__pout__fin__fout & 0x01;
          port_info.mkey_violations = be16_to_cpu(port_info_ptr->mkey_violation);
          port_info.pkey_violations = be16_to_cpu(port_info_ptr->pkey_violation);
          port_info.qkey_violations = be16_to_cpu(port_info_ptr->qkey_violation);
          port_info.guid_cap = port_info_ptr->guid_cap;
          port_info.subnet_timeout = port_info_ptr->reserve__subnet_timeout & 0x1F;
          port_info.resp_time_val = port_info_ptr->reserve__resp_time_value & 0x1F;
          port_info.local_phy_errs = port_info_ptr->local_phy_error__overrun_error >> 4;
          port_info.overrun_errs = port_info_ptr->local_phy_error__overrun_error & 0x0F;

          TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
                   "SA client Port Info table response: "
                   "lid=0x%04x, capability mask=0x%08x",
                   port_info.lid, port_info.capability_mask);

          if (query->completion_func) {
            query->completion_func(query->transaction_id,
                                   0,
                                   &port_info,
                                   query->completion_arg);
          }
        }
      }
      else
      {
        TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
                 "SA client port info table with attrib_offset = 0");
        if (query->completion_func) {
          query->completion_func(query->transaction_id,
                                 -EINVAL,
                                 NULL,
                                 query->completion_arg);
        }
      }
    }
    break;

    case TS_IB_CLIENT_RESPONSE_ERROR:
      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "SA client port info table ERROR status");
      if (query->completion_func) {
        query->completion_func(query->transaction_id,
                               -EINVAL,
                               NULL,
                               query->completion_arg);
      }
      break;

    case TS_IB_CLIENT_RESPONSE_TIMEOUT:
      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "SA client port info table query timed out");
      if (query->completion_func) {
        query->completion_func(query->transaction_id,
                               -ETIMEDOUT,
                               NULL,
                               query->completion_arg);
      }
      break;

    case TS_IB_CLIENT_RESPONSE_CANCEL:
      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "SA client port info table query canceled");
      break;

    default:
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "Unknown status %d", status);
      break;
  }

  tsIbRmppClientFree(data);

  kfree(query);
}

int tsIbPortInfoQuery(
                      tTS_IB_DEVICE_HANDLE device,
                      tTS_IB_PORT port,
                      tTS_IB_LID port_lid,
                      tTS_IB_PORT port_num,
                      int timeout_jiffies,
                      tTS_IB_PORT_INFO_QUERY_COMPLETION_FUNC completion_func,
                      void *completion_arg,
                      tTS_IB_CLIENT_QUERY_TID *transaction_id
                      ) {
  tTS_IB_MAD_STRUCT mad;
  tTS_IB_SA_PAYLOAD sa_payload = (tTS_IB_SA_PAYLOAD) &mad.payload;
  tTS_IB_SA_PORT_INFO port_info = (tTS_IB_SA_PORT_INFO) sa_payload->admin_data;
  tTS_IB_SA_PORT_INFO_QUERY query;

#ifdef W2K_OS // Vipul
  query = kmalloc(sizeof *query, GFP_KERNEL);
#else
  query = kmalloc(sizeof *query, GFP_ATOMIC);
#endif
  if (!query) {
    return -ENOMEM;
  }

  tsIbSaClientMadInit(&mad, device, port);
  mad.r_method           = TS_IB_MGMT_METHOD_GET;
  mad.attribute_id       = cpu_to_be16(TS_IB_SA_ATTRIBUTE_PORT_INFO_RECORD);
  mad.attribute_modifier = 0;

#ifdef W2K_OS // Vipul
  sa_payload->sa_header.component_mask = cpu_to_be64(0x1UL); /* port LID */
#else
  sa_payload->sa_header.component_mask = cpu_to_be64(0x1ULL); /* port LID  */
#endif

  port_info->port_lid = cpu_to_be16(port_lid);

  query->transaction_id  = mad.transaction_id;
  query->completion_func = completion_func;
  query->completion_arg  = completion_arg;

  *transaction_id = mad.transaction_id;

  tsIbClientQuery(&mad, timeout_jiffies, _tsIbPortInfoResponse, query);

  return 0;
}

int tsIbPortInfoTblQuery(
                         tTS_IB_DEVICE_HANDLE device,
                         tTS_IB_PORT port,
                         int timeout_jiffies,
                         tTS_IB_PORT_INFO_QUERY_COMPLETION_FUNC completion_func,
                         void *completion_arg,
                         tTS_IB_CLIENT_QUERY_TID *transaction_id
                         ) {
  tTS_IB_MAD_STRUCT mad;
  tTS_IB_SA_PORT_INFO_QUERY query;
  tTS_IB_CLIENT_RMPP_MAD rmpp_mad;

  query = kmalloc(sizeof *query, GFP_ATOMIC);
  if (!query) {
    return -ENOMEM;
  }

  tsIbSaClientMadInit(&mad, device, port);
  mad.r_method           = TS_IB_SA_METHOD_GET_TABLE;
  mad.attribute_id       = cpu_to_be16(TS_IB_SA_ATTRIBUTE_PORT_INFO_RECORD);
  mad.attribute_modifier = 0;

  /* rmpp header init */
  rmpp_mad = (tTS_IB_CLIENT_RMPP_MAD) &mad;
  rmpp_mad->version = 1;
  rmpp_mad->type = TS_IB_CLIENT_RMPP_TYPE_DATA;

  query->transaction_id  = mad.transaction_id;
  query->completion_func = completion_func;
  query->completion_arg  = completion_arg;

  *transaction_id = mad.transaction_id;

  tsIbRmppClientQuery(&mad, timeout_jiffies, sizeof (tTS_IB_SA_HEADER_STRUCT),
                      _tsIbPortInfoTblResponse, query);

  return 0;
}
