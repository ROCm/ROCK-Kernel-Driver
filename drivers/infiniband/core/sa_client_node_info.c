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

  $Id: sa_client_node_info.c 32 2004-04-09 03:57:42Z roland $
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

typedef struct tTS_IB_SA_NODE_INFO_STRUCT tTS_IB_SA_NODE_INFO_STRUCT,
  *tTS_IB_SA_NODE_INFO;

typedef struct tTS_IB_SA_NODE_INFO_QUERY_STRUCT tTS_IB_SA_NODE_INFO_QUERY_STRUCT,
  *tTS_IB_SA_NODE_INFO_QUERY;

struct tTS_IB_SA_NODE_INFO_STRUCT {
  tTS_IB_LID       port_lid;
  uint16_t         reserved;
  uint8_t          base_version;
  uint8_t          class_version;
  uint8_t          node_type;
  uint8_t          num_ports;
  tTS_IB_GUID      system_image_guid;
  tTS_IB_GUID      node_guid;
  tTS_IB_GUID      port_guid;
  uint16_t         partition_cap;
  uint16_t         dev_id;
  uint32_t         dev_rev;
  tTS_IB_PORT      local_port_num;
  uint8_t          vendor_id[3];
} __attribute__((packed));

struct tTS_IB_SA_NODE_INFO_QUERY_STRUCT {
  tTS_IB_CLIENT_QUERY_TID                 transaction_id;
  tTS_IB_NODE_INFO_QUERY_COMPLETION_FUNC  completion_func;
  void *                                  completion_arg;
};

#define ORACLE_DEMO_LANE15_HACK

static void _tsIbNodeInfoResponse(
                                  tTS_IB_CLIENT_RESPONSE_STATUS status,
                                  tTS_IB_MAD packet,
                                  void *query_ptr
                                  ) {
  tTS_IB_SA_NODE_INFO_QUERY query = query_ptr;

  switch (status) {
    case TS_IB_CLIENT_RESPONSE_OK:
    {
      tTS_IB_SA_PAYLOAD sa_payload = (tTS_IB_SA_PAYLOAD) &packet->payload;
      tTS_IB_SA_NODE_INFO node_info_ptr = (tTS_IB_SA_NODE_INFO) sa_payload->admin_data;
      tTS_IB_NODE_INFO_STRUCT node_info;

      memset(&node_info, 0, sizeof(node_info));
      node_info.base_version = node_info_ptr->base_version;
      node_info.class_version = node_info_ptr->class_version;
      node_info.node_type = node_info_ptr->node_type;
      node_info.num_ports = node_info_ptr->num_ports;
      memcpy(node_info.system_image_guid, node_info_ptr->system_image_guid, sizeof(tTS_IB_GUID));
      memcpy(node_info.node_guid, node_info_ptr->node_guid, sizeof(tTS_IB_GUID));
      memcpy(node_info.port_guid, node_info_ptr->port_guid, sizeof(tTS_IB_GUID));
      node_info.partition_cap = be16_to_cpu(node_info_ptr->partition_cap);
      node_info.dev_id = be16_to_cpu(node_info_ptr->dev_id);
      node_info.dev_rev = be32_to_cpu(node_info_ptr->dev_rev);
      node_info.local_port_num = node_info_ptr->local_port_num;

      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "SA client Node Info response: "
               "Node GUID= %02x%02x%02x%02x%02x%02x%02x%02x",
               node_info.node_guid[0], node_info.node_guid[1], node_info.node_guid[2], node_info.node_guid[3],
               node_info.node_guid[4], node_info.node_guid[5], node_info.node_guid[6], node_info.node_guid[7]);

      if (query->completion_func) {
        query->completion_func(query->transaction_id,
                               0,
                               &node_info,
                               query->completion_arg);
      }
    }
    break;

    case TS_IB_CLIENT_RESPONSE_ERROR:
      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "SA client node info MAD status 0x%04x",
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
               "SA client node info query timed out");
      if (query->completion_func) {
        query->completion_func(query->transaction_id,
                               -ETIMEDOUT,
                               NULL,
                               query->completion_arg);
      }
      break;

    case TS_IB_CLIENT_RESPONSE_CANCEL:
      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "SA client node info query canceled");
      break;

    default:
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "Unknown status %d", status);
      break;
  }

  kfree(query);
}

int tsIbNodeInfoQuery(
                      tTS_IB_DEVICE_HANDLE device,
                      tTS_IB_PORT port,
                      tTS_IB_LID port_lid,
                      int timeout_jiffies,
                      tTS_IB_NODE_INFO_QUERY_COMPLETION_FUNC completion_func,
                      void *completion_arg,
                      tTS_IB_CLIENT_QUERY_TID *transaction_id
                      ) {
  tTS_IB_MAD_STRUCT mad;
  tTS_IB_SA_PAYLOAD sa_payload = (tTS_IB_SA_PAYLOAD) &mad.payload;
  tTS_IB_SA_NODE_INFO node_info = (tTS_IB_SA_NODE_INFO) sa_payload->admin_data;
  tTS_IB_SA_NODE_INFO_QUERY query;

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
  mad.attribute_id       = cpu_to_be16(TS_IB_SA_ATTRIBUTE_NODE_RECORD);
  mad.attribute_modifier = 0;

  query->transaction_id  = mad.transaction_id;
  query->completion_func = completion_func;
  query->completion_arg  = completion_arg;

#ifdef W2K_OS // Vipul
  sa_payload->sa_header.component_mask = cpu_to_be64(0x1UL); /* port LID */
#else
  sa_payload->sa_header.component_mask = cpu_to_be64(0x1ULL); /* port LID */
#endif

  node_info->port_lid = port_lid;

  *transaction_id = mad.transaction_id;

  tsIbClientQuery(&mad, timeout_jiffies, _tsIbNodeInfoResponse, query);

  return 0;
}
