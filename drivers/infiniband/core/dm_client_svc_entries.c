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

  $Id: dm_client_svc_entries.c 32 2004-04-09 03:57:42Z roland $
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

#define TS_IB_DM_SE_GET_CONTROLLER_ID(x)       ((x >> 16) & 0xFFFF)
#define TS_IB_DM_SE_SET_CONTROLLER_ID(x,y)     (x | ((y & 0xFFFF) << 16))
#define TS_IB_DM_SE_GET_END_ENTRY_ID(x)        ((x >> 8) & 0xFF)
#define TS_IB_DM_SE_SET_END_ENTRY_ID(x,y)      (x | ((y & 0xFF) << 8))
#define TS_IB_DM_SE_GET_BEG_ENTRY_ID(x)        (x & 0xFF)
#define TS_IB_DM_SE_SET_BEG_ENTRY_ID(x,y)      (x | (y & 0xFF))

typedef struct tTS_IB_DM_SVC_ENTRY_STRUCT tTS_IB_DM_SVC_ENTRY_STRUCT,
  *tTS_IB_DM_SVC_ENTRY;
typedef struct tTS_IB_DM_SVC_ENTRIES_STRUCT tTS_IB_DM_SVC_ENTRIES_STRUCT,
  *tTS_IB_DM_SVC_ENTRIES;
typedef struct tTS_IB_DM_SVC_ENTRIES_QUERY_STRUCT tTS_IB_DM_SVC_ENTRIES_QUERY_STRUCT,
  *tTS_IB_DM_SVC_ENTRIES_QUERY;

struct tTS_IB_DM_SVC_ENTRY_STRUCT {
  uint8_t  service_name[TS_IB_SVC_ENTRY_NAME_SIZE];
  uint8_t   service_id[TS_IB_SVC_ENTRY_ID_SIZE];
} __attribute__((packed));

struct tTS_IB_DM_SVC_ENTRIES_STRUCT {
  uint8_t                       reserved[40];
  tTS_IB_DM_SVC_ENTRY_STRUCT    service_entries[TS_IB_SVC_ENTRIES_NUM_SVC_ENTRY];
} __attribute__((packed));

struct tTS_IB_DM_SVC_ENTRIES_QUERY_STRUCT {
  tTS_IB_CLIENT_QUERY_TID               transaction_id;
  tTS_IB_LID                            dlid;
  tTS_IB_SVC_ENTRIES_COMPLETION_FUNC    completion_func;
  void *                                completion_arg;
};

static void _tsIbSvcEntriesResponse(tTS_IB_CLIENT_RESPONSE_STATUS status,
                                    tTS_IB_MAD packet,
                                    void *query_ptr)
{
  tTS_IB_DM_SVC_ENTRIES_QUERY query = query_ptr;

  switch (status)
  {
    case TS_IB_CLIENT_RESPONSE_OK:
    {
      tTS_IB_DM_SVC_ENTRIES svc_entries_ptr = (tTS_IB_DM_SVC_ENTRIES) &packet->payload;
      tTS_IB_SVC_ENTRIES_STRUCT svc_entries;
      uint32_t attribute_modifier = be32_to_cpu(packet->attribute_modifier);
      int i;

      svc_entries.controller_id = TS_IB_DM_SE_GET_CONTROLLER_ID(attribute_modifier);
      svc_entries.begin_svc_entry_id = TS_IB_DM_SE_GET_BEG_ENTRY_ID(attribute_modifier);
      svc_entries.end_svc_entry_id = TS_IB_DM_SE_GET_END_ENTRY_ID(attribute_modifier);
      for (i = 0; i < TS_IB_SVC_ENTRIES_NUM_SVC_ENTRY; i++)
      {
        memcpy(&svc_entries.service_entries[i],
               &svc_entries_ptr->service_entries[i],
               sizeof(tTS_IB_DM_SVC_ENTRY_STRUCT));
      }

      for (i = 0; i < TS_IB_SVC_ENTRIES_NUM_SVC_ENTRY; i++)
      {
        TS_TRACE(MOD_KERNEL_DM, T_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
                 "SvcEntries (controller %d)(id %d)..  Name: %s, ID: %02x%02x%02x%02x%02x%02x%02x%02x\n ",
                 svc_entries.controller_id,
                 svc_entries.begin_svc_entry_id + i,
                 svc_entries.service_entries[i].service_name,
                 svc_entries.service_entries[i].service_id[0], svc_entries.service_entries[i].service_id[1],
                 svc_entries.service_entries[i].service_id[2], svc_entries.service_entries[i].service_id[3],
                 svc_entries.service_entries[i].service_id[4], svc_entries.service_entries[i].service_id[5],
                 svc_entries.service_entries[i].service_id[6], svc_entries.service_entries[i].service_id[7]);
      }

      if (query->completion_func) {
        query->completion_func(query->transaction_id,
                               0,
                               packet->slid,
                               &svc_entries,
                               query->completion_arg);
      }
    }
    break;

  case TS_IB_CLIENT_RESPONSE_ERROR:
    TS_TRACE(MOD_KERNEL_DM, T_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
             "DM client Svc Entries MAD slid= 0x%04x, status 0x%04x",
             packet->slid, be16_to_cpu(packet->status));
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
             "DM client Svc Entris query timed out");
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
             "DM client Svc Entries query canceled");
    break;

  default:
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "Unknown status %d", status);
    break;
  }

  kfree(query);
}

int tsIbSvcEntriesRequest(
                          tTS_IB_DEVICE_HANDLE device,
                          tTS_IB_PORT port,
                          tTS_IB_LID dst_port_lid,
                          tTS_IB_CONTROLLER_ID controller_id,
                          tTS_IB_SVC_ENTRY_ID begin_svc_entry_id,
                          tTS_IB_SVC_ENTRY_ID end_svc_entry_id,
                          int timeout_jiffies,
                          tTS_IB_SVC_ENTRIES_COMPLETION_FUNC completion_func,
                          void *completion_arg,
                          tTS_IB_CLIENT_QUERY_TID *transaction_id
                          ) {
  tTS_IB_MAD_STRUCT mad;
  tTS_IB_DM_SVC_ENTRIES_QUERY query;
  uint32_t attrib_mod = 0;

  query = kmalloc(sizeof *query, GFP_KERNEL);
  if (!query) {
    return -ENOMEM;
  }

  attrib_mod = TS_IB_DM_SE_SET_CONTROLLER_ID(attrib_mod, controller_id);
  attrib_mod = TS_IB_DM_SE_SET_BEG_ENTRY_ID(attrib_mod, begin_svc_entry_id);
  attrib_mod = TS_IB_DM_SE_SET_END_ENTRY_ID(attrib_mod, end_svc_entry_id);
  tsIbDmClientMadInit(&mad, device, port, dst_port_lid, TS_IB_GSI_QP,
                      TS_IB_DM_METHOD_GET, TS_IB_DM_ATTRIBUTE_SVC_ENTRIES,
                      attrib_mod);

  query->transaction_id  = mad.transaction_id;
  query->dlid = dst_port_lid;
  query->completion_func = completion_func;
  query->completion_arg  = completion_arg;

  *transaction_id = mad.transaction_id;

  tsIbClientQuery(&mad, timeout_jiffies, _tsIbSvcEntriesResponse, query);

  return 0;
}
