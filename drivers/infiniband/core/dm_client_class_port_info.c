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

  $Id: dm_client_class_port_info.c 32 2004-04-09 03:57:42Z roland $
*/

#include "dm_client.h"

#include "ts_ib_mad.h"
#include "ts_ib_core.h"

#include "ts_kernel_trace.h"

#ifndef W2K_OS
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include <asm/system.h>
#include <asm/byteorder.h>
#endif

typedef struct tTS_IB_DM_CLASS_PORT_INFO_STRUCT tTS_IB_DM_CLASS_PORT_INFO_STRUCT,
  *tTS_IB_DM_CLASS_PORT_INFO;

typedef struct tTS_IB_DM_CLASS_PORT_INFO_QUERY_STRUCT tTS_IB_DM_CLASS_PORT_INFO_QUERY_STRUCT,
  *tTS_IB_DM_CLASS_PORT_INFO_QUERY;

struct tTS_IB_DM_CLASS_PORT_INFO_STRUCT {
  uint8_t  reserved[40];
  uint8_t  base_version;
  uint8_t  class_version;
  uint16_t capability_mask;
  uint32_t resp_time;         /* reserved(27), resp_time(5) */
  tTS_IB_GID  redirect_gid;
  uint32_t redirect_tc_sl_fl; /* TC(8), SL(4), FL(20) */
  tTS_IB_LID  redirect_lid;
  tTS_IB_PKEY redirect_pkey;
  tTS_IB_QPN  redirect_qp;    /* reserved(8), qpn(24) */
  tTS_IB_QKEY redirect_qkey;
  tTS_IB_GID  trap_gid;
  uint32_t    trap_tc_sl_fl;  /* TC(8), SL(4), FL(20) */
  tTS_IB_LID  trap_lid;
  tTS_IB_PKEY trap_pkey;
  uint32_t    trap_hl_qp;     /* HopLimit(8), QP(24) */
  tTS_IB_QKEY trap_qkey;
} __attribute__((packed));

struct tTS_IB_DM_CLASS_PORT_INFO_QUERY_STRUCT {
  tTS_IB_CLIENT_QUERY_TID                   transaction_id;
  tTS_IB_LID                                dlid;
  tTS_IB_DM_CLASS_PORT_INFO_COMPLETION_FUNC completion_func;
  void *                                    completion_arg;
};

static void _tsIbDmClassPortInfoResponse(tTS_IB_CLIENT_RESPONSE_STATUS status,
                                         tTS_IB_MAD packet,
                                         void *query_ptr)
{
  tTS_IB_DM_CLASS_PORT_INFO_QUERY query = query_ptr;

  switch (status)
  {
    case TS_IB_CLIENT_RESPONSE_OK:
    {
      tTS_IB_DM_CLASS_PORT_INFO cpi_ptr = (tTS_IB_DM_CLASS_PORT_INFO) &packet->payload;
      tTS_IB_COMMON_ATTRIB_CPI_STRUCT cpi;

      cpi.base_version = cpi_ptr->base_version;
      cpi.class_version = cpi_ptr->class_version;
      cpi.capability_mask = be16_to_cpu(cpi_ptr->capability_mask);
      cpi.resp_time = be32_to_cpu(cpi_ptr->resp_time);
      memcpy(cpi.redirect_gid, cpi_ptr->redirect_gid, sizeof(tTS_IB_GID));
      cpi.redirect_tc = (be32_to_cpu(cpi_ptr->redirect_tc_sl_fl) & 0xFF000000) >> 24;
      cpi.redirect_sl = (be32_to_cpu(cpi_ptr->redirect_tc_sl_fl) & 0x00F00000) >> 20;
      cpi.redirect_fl = (be32_to_cpu(cpi_ptr->redirect_tc_sl_fl) & 0x000FFFFF);
      cpi.redirect_lid = be16_to_cpu(cpi_ptr->redirect_lid);
      cpi.redirect_pkey = be16_to_cpu(cpi_ptr->redirect_pkey);
      cpi.redirect_qpn = be32_to_cpu(cpi_ptr->redirect_qp) & 0x00FFFFFF;
      cpi.redirect_qkey = be32_to_cpu(cpi_ptr->redirect_qkey);
      memcpy(cpi.trap_gid, cpi_ptr->trap_gid, sizeof(tTS_IB_GID));
      cpi.trap_tc = (be32_to_cpu(cpi_ptr->trap_tc_sl_fl) & 0xFF000000) >> 24;
      cpi.trap_sl = (be32_to_cpu(cpi_ptr->trap_tc_sl_fl) & 0x00F00000) >> 20;
      cpi.trap_fl = (be32_to_cpu(cpi_ptr->trap_tc_sl_fl) & 0x000FFFFF);
      cpi.trap_lid = be16_to_cpu(cpi_ptr->trap_lid);
      cpi.trap_pkey = be16_to_cpu(cpi_ptr->trap_pkey);
      cpi.trap_hop_limit = (be32_to_cpu(cpi_ptr->trap_hl_qp) & 0xFF000000) >> 24;
      cpi.trap_qp = be32_to_cpu(cpi_ptr->trap_hl_qp) & 0x00FFFFFF;
      cpi.trap_qkey = be32_to_cpu(cpi_ptr->trap_qkey);

      TS_TRACE(MOD_KERNEL_DM, T_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
               "DmClassPortInfo trap LID= 0x%04x, PKEY= 0x%04x\n",
               cpi.trap_lid, cpi.trap_pkey);

      if (query->completion_func) {
        query->completion_func(query->transaction_id,
                               0,
                               packet->slid,
                               &cpi,
                               query->completion_arg);
      }
    }
    break;

  case TS_IB_CLIENT_RESPONSE_ERROR:
    TS_TRACE(MOD_KERNEL_DM, T_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
             "DM client Class Port Info query status 0x%04x",
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
             "DM client Class Port Info query timed out");
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
             "DM client Class Port Info query canceled");
    break;

  default:
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "DM client Class Port Info query unknown status %d", status);
    break;
  }

  kfree(query);
}

int tsIbDmClassPortInfoRequest(
                               tTS_IB_DEVICE_HANDLE device,
                               tTS_IB_PORT port,
                               tTS_IB_LID dst_port_lid,
                               int timeout_jiffies,
                               tTS_IB_DM_CLASS_PORT_INFO_COMPLETION_FUNC completion_func,
                               void *completion_arg,
                               tTS_IB_CLIENT_QUERY_TID *transaction_id
                               ) {

  tTS_IB_MAD_STRUCT mad;
  tTS_IB_DM_CLASS_PORT_INFO cpi;
  tTS_IB_PORT_LID_STRUCT lid_info;
  tTS_IB_GID gid;
  tTS_IB_DM_CLASS_PORT_INFO_QUERY query;

  query = kmalloc(sizeof *query, GFP_KERNEL);
  if (!query) {
    return -ENOMEM;
  }

  tsIbDmClientMadInit(&mad, device, port, dst_port_lid, TS_IB_GSI_QP,
                      TS_IB_DM_METHOD_SET, TS_IB_DM_ATTRIBUTE_CLASS_PORTINFO, 0);

  /* Set trap LID to notify TS SRP Mgr to forward trap info */
  cpi = (tTS_IB_DM_CLASS_PORT_INFO)mad.payload;
  tsIbCachedLidGet(device, port, &lid_info);
  cpi->trap_lid = cpu_to_be16(lid_info.lid);
  tsIbCachedGidGet(device, port, 0, gid);
  memcpy(cpi->trap_gid, gid, sizeof(tTS_IB_GID));

  query->transaction_id  = mad.transaction_id;
  query->dlid = dst_port_lid;
  query->completion_func = completion_func;
  query->completion_arg  = completion_arg;

  *transaction_id = mad.transaction_id;

  tsIbClientQuery(&mad, timeout_jiffies, _tsIbDmClassPortInfoResponse, query);

  return 0;
}
