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

  $Id: sa_client_inform.c 32 2004-04-09 03:57:42Z roland $
*/

#include "sa_client.h"

#include "ts_ib_mad.h"

#include "ts_kernel_trace.h"

#ifndef W2K_OS
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include <asm/system.h>
#include <asm/byteorder.h>
#endif

/* INFORM */
#define TS_IB_INFORM_COMP_MASK_GID          0x1
#define TS_IB_INFORM_COMP_MASK_LID_BEGIN    0x2
#define TS_IB_INFORM_COMP_MASK_LID_END      0x4
#define TS_IB_INFORM_COMP_MASK_IS_GENERIC   0x10
#define TS_IB_INFORM_COMP_MASK_SUBSCRIBE    0x20
#define TS_IB_INFORM_COMP_MASK_TYPE         0x40
#define TS_IB_INFORM_COMP_MASK_TRAP_NUM     0x80
#define TS_IB_INFORM_COMP_MASK_QPN          0x100

typedef struct tTS_IB_SA_INFORM_INFO_STRUCT tTS_IB_SA_INFORM_INFO_STRUCT,
  *tTS_IB_SA_INFORM_INFO;

typedef struct tTS_IB_SA_INFORM_INFO_QUERY_STRUCT tTS_IB_SA_INFORM_INFO_QUERY_STRUCT,
  *tTS_IB_SA_INFORM_INFO_QUERY;

struct tTS_IB_SA_INFORM_INFO_STRUCT {
  tTS_IB_GID      gid;
  tTS_IB_LID      lid_range_begin;
  tTS_IB_LID      lid_range_end;
  uint16_t        reserve;
  uint8_t         is_generic;
  uint8_t         subscribe;
  uint16_t        type;
  union {
    struct {
      uint16_t    trap_num        __attribute__((packed));
      uint32_t    resp_time       __attribute__((packed));
      uint32_t    producer_type   __attribute__((packed));
    } generic;
    struct {
      uint16_t    device_id       __attribute__((packed));
      uint32_t    resp_time       __attribute__((packed));
      uint32_t    vendor_id       __attribute__((packed));
    } vendor;
  } define;

} __attribute__((packed));

struct tTS_IB_SA_INFORM_INFO_QUERY_STRUCT {
  tTS_IB_CLIENT_QUERY_TID                   transaction_id;
  tTS_IB_INFORM_INFO_SET_COMPLETION_FUNC    completion_func;
  void *                                    completion_arg;
};

static void _tsIbInformResponse(
                                tTS_IB_CLIENT_RESPONSE_STATUS status,
                                tTS_IB_MAD packet,
                                void *query_ptr
                                ) {
  tTS_IB_SA_INFORM_INFO_QUERY query = query_ptr;

  switch (status) {
    case TS_IB_CLIENT_RESPONSE_OK:
    {
      tTS_IB_SA_PAYLOAD sa_payload = (tTS_IB_SA_PAYLOAD) &packet->payload;
      tTS_IB_SA_INFORM_INFO mad_inform_info = (tTS_IB_SA_INFORM_INFO) sa_payload->admin_data;
      tTS_IB_COMMON_ATTRIB_INFORM_STRUCT inform_info;

      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "SA client set inform info status OK\n");

      memcpy(inform_info.gid, mad_inform_info->gid, sizeof(tTS_IB_GID));
      inform_info.lid_range_begin = be16_to_cpu(mad_inform_info->lid_range_begin);
      inform_info.lid_range_end = be16_to_cpu(mad_inform_info->lid_range_end);
      inform_info.is_generic = mad_inform_info->is_generic;
      inform_info.type = be16_to_cpu(mad_inform_info->type);
      if (mad_inform_info->is_generic == TS_IB_COMMON_INFORM_INFO_GENERIC)
      {
        inform_info.define.generic.trap_num =
          be16_to_cpu(mad_inform_info->define.generic.trap_num);
        inform_info.define.generic.resp_time =
          be32_to_cpu(mad_inform_info->define.generic.resp_time);
        inform_info.define.generic.producer_type =
          be32_to_cpu(mad_inform_info->define.generic.producer_type);
      }
      else
      {
        inform_info.define.vendor.device_id =
          be16_to_cpu(mad_inform_info->define.vendor.device_id);
        inform_info.define.vendor.resp_time =
          be32_to_cpu(mad_inform_info->define.vendor.resp_time);
        inform_info.define.vendor.vendor_id =
          be32_to_cpu(mad_inform_info->define.vendor.vendor_id);
      }

      if (query->completion_func) {
        query->completion_func(query->transaction_id,
                               0,
                               &inform_info,
                               query->completion_arg);
      }
    }
    break;

    case TS_IB_CLIENT_RESPONSE_ERROR:
      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "SA client inform info MAD status 0x%04x",
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
               "SA client multicast member query timed out");
      if (query->completion_func) {
        query->completion_func(query->transaction_id,
                               -ETIMEDOUT,
                               NULL,
                               query->completion_arg);
      }
      break;

    case TS_IB_CLIENT_RESPONSE_CANCEL:
      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "SA client inform info query canceled");
      break;

    default:
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "Unknown status %d", status);
      break;
  }

  kfree(query);
}

static int _tsIbInformInfoSet(
                              tTS_IB_DEVICE_HANDLE device,
                              tTS_IB_PORT port,
                              tTS_IB_COMMON_ATTRIB_INFORM inform_info,
                              int timeout_jiffies,
                              tTS_IB_INFORM_INFO_SET_COMPLETION_FUNC completion_func,
                              void *completion_arg,
                              tTS_IB_CLIENT_QUERY_TID *transaction_id
                              ) {
  tTS_IB_MAD_STRUCT mad;
  tTS_IB_SA_PAYLOAD sa_payload = (tTS_IB_SA_PAYLOAD) &mad.payload;
  tTS_IB_SA_INFORM_INFO mad_inform_info = (tTS_IB_SA_INFORM_INFO) sa_payload->admin_data;
  tTS_IB_SA_INFORM_INFO_QUERY query;

  query = kmalloc(sizeof *query, GFP_ATOMIC);
  if (!query) {
    return -ENOMEM;
  }

  tsIbSaClientMadInit(&mad, device, port);
  mad.r_method           = TS_IB_MGMT_METHOD_SET;
  mad.attribute_id       = cpu_to_be16(TS_IB_SA_ATTRIBUTE_INFORM_INFO);
  mad.attribute_modifier = 0;

  query->transaction_id  = mad.transaction_id;
  query->completion_func = completion_func;
  query->completion_arg  = completion_arg;

  /* fill in data */
  memcpy(mad_inform_info->gid, inform_info->gid, sizeof(tTS_IB_GID));
  mad_inform_info->lid_range_begin = cpu_to_be16(inform_info->lid_range_begin);
  mad_inform_info->lid_range_end = cpu_to_be16(inform_info->lid_range_end);
  mad_inform_info->is_generic = inform_info->is_generic;
  mad_inform_info->subscribe = inform_info->subscribe;
  mad_inform_info->type = cpu_to_be16(inform_info->type);
  if (inform_info->is_generic == TS_IB_COMMON_INFORM_INFO_GENERIC)
  {
    mad_inform_info->define.generic.trap_num =
      cpu_to_be16(inform_info->define.generic.trap_num);
    mad_inform_info->define.generic.resp_time =
      cpu_to_be32(inform_info->define.generic.resp_time);
    mad_inform_info->define.generic.producer_type =
      cpu_to_be32(inform_info->define.generic.producer_type);
  }
  else
  {
    mad_inform_info->define.vendor.device_id =
      cpu_to_be16(inform_info->define.vendor.device_id);
    mad_inform_info->define.vendor.resp_time =
      cpu_to_be32(inform_info->define.vendor.resp_time);
    mad_inform_info->define.vendor.vendor_id =
      cpu_to_be32(inform_info->define.vendor.vendor_id);
  }

  *transaction_id = mad.transaction_id;

  tsIbClientQuery(&mad, timeout_jiffies, _tsIbInformResponse, query);

  return 0;
}

static int _tsIbSetGenericSmNoticeHandler(
                                          uint16_t trap_num,
                                          tTS_IB_DEVICE_HANDLE device,
                                          tTS_IB_PORT port,
                                          tTS_IB_GID gid,
                                          tTS_IB_LID lid_begin,
                                          tTS_IB_LID lid_end,
                                          tTS_IB_SA_NOTICE_HANDLER_FUNC handler,
                                          void *handler_arg,
                                          int timeout_jiffies,
                                          tTS_IB_INFORM_INFO_SET_COMPLETION_FUNC completion_func,
                                          void *completion_arg,
                                          tTS_IB_CLIENT_QUERY_TID *transaction_id
                                         )
{
  int rc;
  tTS_IB_COMMON_ATTRIB_INFORM_STRUCT inform_info;

  /* Construct inform_info */
  memset(&inform_info, 0, sizeof(tTS_IB_COMMON_ATTRIB_INFORM_STRUCT));
  memcpy(inform_info.gid, gid, sizeof(tTS_IB_GID));
  inform_info.lid_range_begin = lid_begin;
  inform_info.lid_range_end = lid_end;
  inform_info.is_generic = TS_IB_COMMON_INFORM_INFO_GENERIC;
  if (handler) {
    inform_info.subscribe = TS_IB_COMMON_INFORM_SUBSCRIBE;
  }
  else {
    inform_info.subscribe = TS_IB_COMMON_INFORM_UNSUBSCRIBE;
  }
  inform_info.type = TS_IB_COMMON_NOTICE_TYPE_SM;
  inform_info.define.generic.trap_num = trap_num;
  inform_info.define.generic.producer_type = TS_IB_COMMON_TRAP_PRODUCER_TYPE_SM;

  /* Subscribe/unsubscribe to SA */
  rc = _tsIbInformInfoSet(device,
                          port,
                          &inform_info,
                          timeout_jiffies,
                          completion_func,
                          completion_arg,
                          transaction_id);

  /* Set internal handler */
  if (rc == 0) {
    rc = tsIbSaNoticeHandlerRegister(trap_num,
                                     device, port, handler, handler_arg);
  }

  return rc;
}

int tsIbSetInServiceNoticeHandler(
                                  tTS_IB_DEVICE_HANDLE device,
                                  tTS_IB_PORT port,
                                  tTS_IB_GID gid,
                                  tTS_IB_LID lid_begin,
                                  tTS_IB_LID lid_end,
                                  tTS_IB_SA_NOTICE_HANDLER_FUNC handler,
                                  void *handler_arg,
                                  int timeout_jiffies,
                                  tTS_IB_INFORM_INFO_SET_COMPLETION_FUNC completion_func,
                                  void *completion_arg,
                                  tTS_IB_CLIENT_QUERY_TID *transaction_id
                                  )
{
  int rc;

  TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
           "SA client register InService notice: "
           "gid=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x "
           "lid begin=0x%x, lid end=0x%x\n",
           gid[0], gid[1], gid[2], gid[3], gid[4], gid[5], gid[6], gid[7],
           gid[8], gid[9], gid[10], gid[11], gid[12], gid[13], gid[14], gid[15],
           lid_begin, lid_end);

  rc = _tsIbSetGenericSmNoticeHandler(TS_IB_GENERIC_TRAP_NUM_IN_SVC,
                                      device,
                                      port,
                                      gid,
                                      lid_begin,
                                      lid_end,
                                      handler,
                                      handler_arg,
                                      timeout_jiffies,
                                      completion_func,
                                      completion_arg,
                                      transaction_id);

  return rc;
}

int tsIbSetOutofServiceNoticeHandler(
                                     tTS_IB_DEVICE_HANDLE device,
                                     tTS_IB_PORT port,
                                     tTS_IB_GID gid,
                                     tTS_IB_LID lid_begin,
                                     tTS_IB_LID lid_end,
                                     tTS_IB_SA_NOTICE_HANDLER_FUNC handler,
                                     void *handler_arg,
                                     int timeout_jiffies,
                                     tTS_IB_INFORM_INFO_SET_COMPLETION_FUNC completion_func,
                                     void *completion_arg,
                                     tTS_IB_CLIENT_QUERY_TID *transaction_id
                                     )
{
  int rc;

  TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
           "SA client register OutOfService notice: "
           "gid=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x "
           "lid begin=0x%x, lid end=0x%x\n",
           gid[0], gid[1], gid[2], gid[3], gid[4], gid[5], gid[6], gid[7],
           gid[8], gid[9], gid[10], gid[11], gid[12], gid[13], gid[14], gid[15],
           lid_begin, lid_end);

  rc = _tsIbSetGenericSmNoticeHandler(TS_IB_GENERIC_TRAP_NUM_OUT_OF_SVC,
                                      device,
                                      port,
                                      gid,
                                      lid_begin,
                                      lid_end,
                                      handler,
                                      handler_arg,
                                      timeout_jiffies,
                                      completion_func,
                                      completion_arg,
                                      transaction_id);

  return rc;
}

int tsIbSetMcastGroupCreateNoticeHandler(
                                         tTS_IB_DEVICE_HANDLE device,
                                         tTS_IB_PORT port,
                                         tTS_IB_GID gid,
                                         tTS_IB_LID lid_begin,
                                         tTS_IB_LID lid_end,
                                         tTS_IB_SA_NOTICE_HANDLER_FUNC handler,
                                         void *handler_arg,
                                         int timeout_jiffies,
                                         tTS_IB_INFORM_INFO_SET_COMPLETION_FUNC completion_func,
                                         void *completion_arg,
                                         tTS_IB_CLIENT_QUERY_TID *transaction_id
                                        )
{
  int rc;

  TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
           "SA client register McastGroupCreate notice: "
           "gid=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x "
           "lid begin=0x%x, lid end=0x%x\n",
           gid[0], gid[1], gid[2], gid[3], gid[4], gid[5], gid[6], gid[7],
           gid[8], gid[9], gid[10], gid[11], gid[12], gid[13], gid[14], gid[15],
           lid_begin, lid_end);

  rc = _tsIbSetGenericSmNoticeHandler(TS_IB_GENERIC_TRAP_NUM_CREATE_MC_GRP,
                                      device,
                                      port,
                                      gid,
                                      lid_begin,
                                      lid_end,
                                      handler,
                                      handler_arg,
                                      timeout_jiffies,
                                      completion_func,
                                      completion_arg,
                                      transaction_id);

  return rc;
}

int tsIbSetMcastGroupDeleteNoticeHandler(
                                         tTS_IB_DEVICE_HANDLE device,
                                         tTS_IB_PORT port,
                                         tTS_IB_GID gid,
                                         tTS_IB_LID lid_begin,
                                         tTS_IB_LID lid_end,
                                         tTS_IB_SA_NOTICE_HANDLER_FUNC handler,
                                         void *handler_arg,
                                         int timeout_jiffies,
                                         tTS_IB_INFORM_INFO_SET_COMPLETION_FUNC completion_func,
                                         void *completion_arg,
                                         tTS_IB_CLIENT_QUERY_TID *transaction_id
                                        )
{
  int rc;

  TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
           "SA client register McastGroupDelete notice: "
           "gid=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x "
           "lid begin=0x%x, lid end=0x%x\n",
           gid[0], gid[1], gid[2], gid[3], gid[4], gid[5], gid[6], gid[7],
           gid[8], gid[9], gid[10], gid[11], gid[12], gid[13], gid[14], gid[15],
           lid_begin, lid_end);

  rc = _tsIbSetGenericSmNoticeHandler(TS_IB_GENERIC_TRAP_NUM_DELETE_MC_GRP,
                                      device,
                                      port,
                                      gid,
                                      lid_begin,
                                      lid_end,
                                      handler,
                                      handler_arg,
                                      timeout_jiffies,
                                      completion_func,
                                      completion_arg,
                                      transaction_id);

  return rc;
}
