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

  $Id: dm_client_ioc_profile.c 32 2004-04-09 03:57:42Z roland $
*/

#include "dm_client.h"

#include "ts_ib_mad.h"

#include "ts_kernel_trace.h"

#ifndef W2K_OS
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include <asm/system.h>
#include <asm/byteorder.h>
#endif

#define TS_IB_DM_IOCPROFILE_GET_CONTROLLER_ID(x)     (x & 0x000000FF)
#define TS_IB_DM_IOCPROFILE_SET_CONTROLLER_ID(x,y)   (x | (y & 0x000000FF))
#define TS_IB_DM_IOCPROFILE_GET_VENDOR_ID(x)         ((x >> 8) & 0x00FFFFFF)
#define TS_IB_DM_IOCPROFILE__GET_SUBVENDOR_ID(x)     ((x >> 8) & 0x00FFFFFF)

typedef struct tTS_IB_DM_IOC_PROFILE_STRUCT tTS_IB_DM_IOC_PROFILE_STRUCT,
  *tTS_IB_DM_IOC_PROFILE;

typedef struct tTS_IB_DM_IOC_PROFILE_QUERY_STRUCT tTS_IB_DM_IOC_PROFILE_QUERY_STRUCT,
  *tTS_IB_DM_IOC_PROFILE_QUERY;

struct tTS_IB_DM_IOC_PROFILE_STRUCT {
  uint8_t     reserved[40];
  tTS_IB_GUID guid;               // controller guid
  uint32_t    vendor_id__reserved;  // Vendor ID (24), reserved (8)
  uint32_t    device_id;
  uint16_t    device_version;
  uint16_t    reserved2;
  uint32_t    subsystem_vendor_id__reserved; // Subsystem Vendor ID (24), reserved (8)
  uint32_t    subsystem_id;
  uint16_t    io_class;
  uint16_t    io_subclass;
  uint16_t    protocol;
  uint16_t    protocol_version;
  uint16_t    service_conn;
  uint16_t    initiators_supported;
  uint16_t    send_msg_depth;
  uint16_t    rdma_read_depth;
  uint32_t    send_msg_size;
  uint32_t    rdma_xfer_size;
  uint8_t     op_capability_mask;
  uint8_t     svc_capability_mask;
  uint8_t     num_svc_entries;
  uint8_t     reserved1[9];
  uint8_t     id_string[64];
} __attribute__((packed));

struct tTS_IB_DM_IOC_PROFILE_QUERY_STRUCT {
  tTS_IB_CLIENT_QUERY_TID                  transaction_id;
  tTS_IB_LID                               dlid;
  tTS_IB_IOC_PROFILE_COMPLETION_FUNC       completion_func;
  void *                                   completion_arg;
};

static void _tsIbIocProfileResponse(tTS_IB_CLIENT_RESPONSE_STATUS status,
                                    tTS_IB_MAD packet,
                                    void *query_ptr)
{
  tTS_IB_DM_IOC_PROFILE_QUERY query = query_ptr;

  switch (status)
  {
    case TS_IB_CLIENT_RESPONSE_OK:
    {
      tTS_IB_DM_IOC_PROFILE ioc_profile_ptr = (tTS_IB_DM_IOC_PROFILE) &packet->payload;
      tTS_IB_IOC_PROFILE_STRUCT ioc_profile;

      ioc_profile.controller_id = TS_IB_DM_IOCPROFILE_GET_CONTROLLER_ID(be32_to_cpu(packet->attribute_modifier));
      memcpy(ioc_profile.guid, ioc_profile_ptr->guid, sizeof(tTS_IB_GUID));
      ioc_profile.vendor_id = (be32_to_cpu(ioc_profile_ptr->vendor_id__reserved)) >> 8;
      ioc_profile.device_id = be32_to_cpu(ioc_profile_ptr->device_id);
      ioc_profile.device_version = be16_to_cpu(ioc_profile_ptr->device_version);
      ioc_profile.subsystem_vendor_id = (be32_to_cpu(ioc_profile_ptr->subsystem_vendor_id__reserved)) >> 8;
      ioc_profile.subsystem_id = be32_to_cpu(ioc_profile_ptr->subsystem_id);
      ioc_profile.io_class = be16_to_cpu(ioc_profile_ptr->io_class);
      ioc_profile.io_subclass = be16_to_cpu(ioc_profile_ptr->io_subclass);
      ioc_profile.protocol = be16_to_cpu(ioc_profile_ptr->protocol);
      ioc_profile.protocol_version = be16_to_cpu(ioc_profile_ptr->protocol_version);
      ioc_profile.service_conn = be16_to_cpu(ioc_profile_ptr->service_conn);
      ioc_profile.initiators_supported = be16_to_cpu(ioc_profile_ptr->initiators_supported);
      ioc_profile.send_msg_depth = be16_to_cpu(ioc_profile_ptr->send_msg_depth);
      ioc_profile.rdma_read_depth = be16_to_cpu(ioc_profile_ptr->rdma_read_depth);
      ioc_profile.send_msg_size = be32_to_cpu(ioc_profile_ptr->send_msg_size);
      ioc_profile.rdma_xfer_size = be32_to_cpu(ioc_profile_ptr->rdma_xfer_size);
      ioc_profile.op_capability_mask = ioc_profile_ptr->op_capability_mask;
      ioc_profile.num_svc_entries = ioc_profile_ptr->num_svc_entries;
      memcpy(ioc_profile.id_string, ioc_profile_ptr->id_string, 64);

      TS_TRACE(MOD_KERNEL_DM, T_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
               "IOCProfile (ID=%d, guid=%02x%02x%02x%02x%02x%02x%02x%02x, numSvcEntries = %d)\n",
               ioc_profile.controller_id,
               ioc_profile.guid[0], ioc_profile.guid[1], ioc_profile.guid[2], ioc_profile.guid[3],
               ioc_profile.guid[4], ioc_profile.guid[5], ioc_profile.guid[6], ioc_profile.guid[7],
               ioc_profile.num_svc_entries);

      if (query->completion_func) {
        query->completion_func(query->transaction_id,
                               0,
                               packet->slid,
                               &ioc_profile,
                               query->completion_arg);
      }
    }
    break;

    case TS_IB_CLIENT_RESPONSE_ERROR:
      TS_TRACE(MOD_KERNEL_DM, T_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
               "DM client IOU Info MAD status 0x%04x",
               be16_to_cpu(packet->status));
      if (query->completion_func) {
        query->completion_func(query->transaction_id,
                               -EINVAL,
                               packet->slid,
                               NULL,
                               query->completion_arg);
      }
      break;

    case TS_IB_CLIENT_RESPONSE_TIMEOUT:
      TS_TRACE(MOD_KERNEL_DM, T_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
               "DM client IOU info query timed out");
      if (query->completion_func) {
        query->completion_func(query->transaction_id,
                               -ETIMEDOUT,
                               query->dlid,
                               NULL,
                               query->completion_arg);
      }
      break;

    case TS_IB_CLIENT_RESPONSE_CANCEL:
      TS_TRACE(MOD_KERNEL_DM, T_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
               "DM client IOU Info query canceled");
      break;

    default:
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "Unknown status %d", status);
      break;
  }

  kfree(query);
}

int tsIbIocProfileRequest(
                          tTS_IB_DEVICE_HANDLE device,
                          tTS_IB_PORT port,
                          tTS_IB_LID dst_port_lid,
                          tTS_IB_CONTROLLER_ID controller_id,
                          int timeout_jiffies,
                          tTS_IB_IOC_PROFILE_COMPLETION_FUNC completion_func,
                          void *completion_arg,
                          tTS_IB_CLIENT_QUERY_TID *transaction_id
                          ) {
  tTS_IB_MAD_STRUCT mad;
  tTS_IB_DM_IOC_PROFILE_QUERY query;

  query = kmalloc(sizeof *query, GFP_KERNEL);
  if (!query) {
    return -ENOMEM;
  }

  tsIbDmClientMadInit(&mad, device, port, dst_port_lid, TS_IB_GSI_QP,
                      TS_IB_DM_METHOD_GET, TS_IB_DM_ATTRIBUTE_IOC_PROFILE,
                      TS_IB_DM_IOCPROFILE_GET_CONTROLLER_ID(controller_id));

  query->transaction_id  = mad.transaction_id;
  query->dlid = dst_port_lid;
  query->completion_func = completion_func;
  query->completion_arg  = completion_arg;

  *transaction_id = mad.transaction_id;

  tsIbClientQuery(&mad, timeout_jiffies, _tsIbIocProfileResponse, query);

  return 0;
}
