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

  $Id: sa_client_path_record.c 32 2004-04-09 03:57:42Z roland $
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

typedef struct tTS_IB_SA_PATH_RECORD_STRUCT tTS_IB_SA_PATH_RECORD_STRUCT,
  *tTS_IB_SA_PATH_RECORD;

typedef struct tTS_IB_SA_PATH_RECORD_QUERY_STRUCT tTS_IB_SA_PATH_RECORD_QUERY_STRUCT,
  *tTS_IB_SA_PATH_RECORD_QUERY;

#ifdef W2K_OS // Vipul
#pragma pack (push, 1)
#endif
struct tTS_IB_SA_PATH_RECORD_STRUCT {
  uint32_t      reserved1[2];
  tTS_IB_GID    dgid;
  tTS_IB_GID    sgid;
  tTS_IB_LID    dlid;
  tTS_IB_LID    slid;
  uint32_t      raw_flowlabel_hoplimit;
  uint8_t       traffic_class;
  uint8_t       numb_path;
  tTS_IB_PKEY   pkey;
  uint16_t      sl;
  uint8_t       mtu;
  uint8_t       rate;
  uint8_t       packet_life;
  uint8_t       preference;
  uint8_t       reserved2[6];
} __attribute__((packed));
#ifdef W2K_OS // Vipul
#pragma pack (pop)
#endif

struct tTS_IB_SA_PATH_RECORD_QUERY_STRUCT {
  tTS_IB_CLIENT_QUERY_TID            transaction_id;
  tTS_IB_PATH_RECORD_COMPLETION_FUNC completion_func;
  void *                             completion_arg;
};

static void _tsIbPathRecordResponse(
                                    tTS_IB_CLIENT_RESPONSE_STATUS status,
                                    tTS_IB_MAD packet,
                                    void *query_ptr
                                    ) {
  tTS_IB_SA_PATH_RECORD_QUERY query = query_ptr;

  switch (status) {
  case TS_IB_CLIENT_RESPONSE_OK:
    {
      tTS_IB_SA_PAYLOAD sa_payload = (tTS_IB_SA_PAYLOAD) &packet->payload;
      tTS_IB_SA_PATH_RECORD path_ptr = (tTS_IB_SA_PATH_RECORD) sa_payload->admin_data;
      tTS_IB_PATH_RECORD_STRUCT path_rec;

      memcpy(path_rec.dgid, path_ptr->dgid, sizeof path_rec.dgid);
      memcpy(path_rec.sgid, path_ptr->sgid, sizeof path_rec.sgid);

      path_rec.dlid        = be16_to_cpu(path_ptr->dlid);
      path_rec.slid        = be16_to_cpu(path_ptr->slid);
      path_rec.flowlabel   = 0; /* XXX */
      path_rec.tclass      = path_ptr->traffic_class;
      path_rec.pkey        = be16_to_cpu(path_ptr->pkey);
      path_rec.sl          = be16_to_cpu(path_ptr->sl) & 0xf;
      path_rec.mtu         = path_ptr->mtu & 0x2f;
      path_rec.rate        = path_ptr->rate & 0x2f;
      path_rec.packet_life = path_ptr->packet_life & 0x2f;

      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "SA client path record response: "
               "dlid=0x%04x slid=0x%04x sl=%d pkey=0x%04x",
               path_rec.dlid,
               path_rec.slid,
               path_rec.sl,
               path_rec.pkey);

      if (query->completion_func) {
        query->completion_func(query->transaction_id,
                               0,
                               &path_rec,
                               0,
                               query->completion_arg);
      }
    }
    break;

  case TS_IB_CLIENT_RESPONSE_ERROR:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "SA client path record MAD status 0x%04x",
             be16_to_cpu(packet->status));
    if (query->completion_func) {
      query->completion_func(query->transaction_id,
                             -EINVAL,
                             NULL,
                             0,
                             query->completion_arg);
    }
    break;

  case TS_IB_CLIENT_RESPONSE_TIMEOUT:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "SA client path record query timed out");
    if (query->completion_func) {
      query->completion_func(query->transaction_id,
                             -ETIMEDOUT,
                             NULL,
                             0,
                             query->completion_arg);
    }
    break;

  case TS_IB_CLIENT_RESPONSE_CANCEL:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "SA client path record query canceled");
    break;

  default:
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "Unknown status %d", status);
    break;
  }

  kfree(query);
}

int tsIbPathRecordRequest(
                          tTS_IB_DEVICE_HANDLE device,
                          tTS_IB_PORT port,
                          tTS_IB_GID sgid,
                          tTS_IB_GID dgid,
                          tTS_IB_PKEY pkey,
                          int flags,
                          int timeout_jiffies,
                          int cache_jiffies,
                          tTS_IB_PATH_RECORD_COMPLETION_FUNC completion_func,
                          void *completion_arg,
                          tTS_IB_CLIENT_QUERY_TID *transaction_id
                          ) {
  tTS_IB_MAD_STRUCT mad;
  tTS_IB_SA_PAYLOAD sa_payload = (tTS_IB_SA_PAYLOAD) &mad.payload;
  tTS_IB_SA_PATH_RECORD path_rec = (tTS_IB_SA_PATH_RECORD) sa_payload->admin_data;
  tTS_IB_SA_PATH_RECORD_QUERY query;

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
  mad.attribute_id       = cpu_to_be16(TS_IB_SA_ATTRIBUTE_PATH_RECORD);
  mad.attribute_modifier = 0xffffffff; /* match attributes */

  query->transaction_id  = mad.transaction_id;
  query->completion_func = completion_func;
  query->completion_arg  = completion_arg;

#ifdef W2K_OS // Vipul
  sa_payload->sa_header.component_mask = cpu_to_be64(0xcUL); /* source and dest gid */
#else
  sa_payload->sa_header.component_mask = cpu_to_be64(0x100cULL); /* num_path, source and dest gid */
#endif

  memcpy(path_rec->sgid, sgid, sizeof (tTS_IB_GID));
  memcpy(path_rec->dgid, dgid, sizeof (tTS_IB_GID));
  path_rec->numb_path = 1;
  if (pkey != TS_IB_SA_INVALID_PKEY)
  {
#ifdef W2K_OS // Vipul
    sa_payload->sa_header.component_mask |= cpu_to_be64(0x2000UL); /* pkey */
#else
    sa_payload->sa_header.component_mask |= cpu_to_be64(0x2000ULL);  /* pkey */
#endif
    path_rec->pkey = cpu_to_be16(pkey);
  }

  tsIbClientQuery(&mad, timeout_jiffies, _tsIbPathRecordResponse, query);

  *transaction_id = mad.transaction_id;

  return 0;
}
