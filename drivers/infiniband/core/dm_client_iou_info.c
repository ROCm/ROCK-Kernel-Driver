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

  $Id: dm_client_iou_info.c 32 2004-04-09 03:57:42Z roland $
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

typedef struct tTS_IB_DM_IOU_INFO_STRUCT tTS_IB_DM_IOU_INFO_STRUCT,
  *tTS_IB_DM_IOU_INFO;

typedef struct tTS_IB_DM_IOU_INFO_QUERY_STRUCT tTS_IB_DM_IOU_INFO_QUERY_STRUCT,
  *tTS_IB_DM_IOU_INFO_QUERY;

struct tTS_IB_DM_IOU_INFO_STRUCT {
  uint8_t     reserved[40];
  uint16_t    change_id;   /* Incremented, with rollover, to any change to
                              ControllerList */
  uint8_t     max_controllers; /* Number of slots in ControllerList */
  uint8_t     rsvd_op_rom;      /* rsvd (7), Optional ROM (1) */
  uint8_t     controller_list[128];   /* A series of 4bit nibbles with each
                                         representing a slot in the IOU */
} __attribute__((packed));

struct tTS_IB_DM_IOU_INFO_QUERY_STRUCT {
  tTS_IB_CLIENT_QUERY_TID               transaction_id;
  tTS_IB_LID                            dlid;
  tTS_IB_IOU_INFO_COMPLETION_FUNC       completion_func;
  void *                                completion_arg;
};

static void _tsIbIouInfoResponse(tTS_IB_CLIENT_RESPONSE_STATUS status,
                                 tTS_IB_MAD packet,
                                 void *query_ptr)
{
  tTS_IB_DM_IOU_INFO_QUERY query = query_ptr;

  switch (status)
  {
    case TS_IB_CLIENT_RESPONSE_OK:
    {
      tTS_IB_DM_IOU_INFO iou_info_ptr = (tTS_IB_DM_IOU_INFO) &packet->payload;
      tTS_IB_IOU_INFO_STRUCT iou_info;

      iou_info.lid = packet->slid;
      iou_info.change_id = be16_to_cpu(iou_info_ptr->change_id);
      iou_info.max_controllers = iou_info_ptr->max_controllers;
      iou_info.op_rom = iou_info_ptr->rsvd_op_rom & 0x1;
      memcpy(iou_info.controller_list, iou_info_ptr->controller_list,
             TS_IB_IOU_INFO_MAX_NUM_CONTROLLERS_IN_BYTE);

      TS_TRACE(MOD_KERNEL_DM, T_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
               "IOUNITINFO(LID= 0x%x) - Change ID=%d; MaxController= %d\n",
               iou_info.lid, iou_info.change_id, iou_info.max_controllers);

      if (query->completion_func) {
        query->completion_func(query->transaction_id,
                               0,
                               packet->slid,
                               &iou_info,
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

int tsIbIouInfoRequest(
                       tTS_IB_DEVICE_HANDLE device,
                       tTS_IB_PORT port,
                       tTS_IB_LID dst_port_lid,
                       int timeout_jiffies,
                       tTS_IB_IOU_INFO_COMPLETION_FUNC completion_func,
                       void *completion_arg,
                       tTS_IB_CLIENT_QUERY_TID *transaction_id
                       ) {
  tTS_IB_MAD_STRUCT mad;
  tTS_IB_DM_IOU_INFO_QUERY query;

  query = kmalloc(sizeof *query, GFP_KERNEL);
  if (!query) {
    return -ENOMEM;
  }

  tsIbDmClientMadInit(&mad, device, port, dst_port_lid, TS_IB_GSI_QP,
                      TS_IB_DM_METHOD_GET, TS_IB_DM_ATTRIBUTE_IOU_INFO, 0);

  query->transaction_id  = mad.transaction_id;
  query->dlid = dst_port_lid;
  query->completion_func = completion_func;
  query->completion_arg  = completion_arg;

  *transaction_id = mad.transaction_id;

  tsIbClientQuery(&mad, timeout_jiffies, _tsIbIouInfoResponse, query);

  return 0;
}

int tsIbIsIocPresent(tTS_IB_IOU_INFO iou_info, tTS_IB_CONTROLLER_ID controller_id)
{
  int16_t index;
  uint8_t is_present;

  if ( controller_id < 1 )
  {
    /* Minimum controller ID is 1 */
    return 0;
  }

  index = (controller_id - 1) / 2;

  if ( controller_id & 0x1 )
  {
    is_present = ((iou_info->controller_list[index] >> 4) & 0x0F);
  }
  else
  {
    is_present = (iou_info->controller_list[index] & 0x0F);
  }

  if (is_present == 0x01)
    return 1;
  else
    return 0;
}
