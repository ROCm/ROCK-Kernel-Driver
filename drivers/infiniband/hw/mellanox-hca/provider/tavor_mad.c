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

  $Id: tavor_mad.c,v 1.11 2004/03/04 02:09:48 roland Exp $
*/

#include "tavor_priv.h"
#include "ts_ib_provider.h"
#include "ts_ib_mad_types.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/utsname.h>      /* Need this to get hostname */

enum tTS_IB_SM_ATTRIBUTE_ID {
  TS_IB_SM_NODE_DESCRIPTION = 0x0010,
  TS_IB_SM_PORT_INFO        = 0x0015,
  TS_IB_SM_PKEY_TABLE       = 0x0016,
  TS_IB_SM_SM_INFO          = 0x0020,
  TS_IB_SM_VENDOR_START     = 0xff00
};

static void _tsIbTavorMadSnoop(
                               tTS_IB_DEVICE device,
                               tTS_IB_MAD in_mad
                               ) {
  tTS_IB_ASYNC_EVENT_RECORD_STRUCT record;

  if (in_mad->dqpn         == 0 &&
      (in_mad->mgmt_class  == TS_IB_MGMT_CLASS_SUBN_LID_ROUTED ||
       in_mad->mgmt_class  == TS_IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) &&
      in_mad->r_method     == TS_IB_MGMT_METHOD_SET) {
    if (in_mad->attribute_id == cpu_to_be16(TS_IB_SM_PORT_INFO)) {
      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "%s: Got set of port info", device->name);

      record.device        = tsIbDeviceToHandle(device);
      record.event         = TS_IB_LID_CHANGE;
      record.modifier.port = in_mad->port;
      tsIbAsyncEventDispatch(&record);
    }    

    if (in_mad->attribute_id == cpu_to_be16(TS_IB_SM_PKEY_TABLE)) {
      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "%s: Got set of P_Key table", device->name);

      record.device        = tsIbDeviceToHandle(device);
      record.event         = TS_IB_PKEY_CHANGE;
      record.modifier.port = in_mad->port;
      tsIbAsyncEventDispatch(&record);
    }
  }   
}

tTS_IB_MAD_RESULT tsIbTavorMadProcess(
                                      tTS_IB_DEVICE device,
                                      int           ignore_mkey,
                                      tTS_IB_MAD    in_mad,
                                      tTS_IB_MAD    response_mad
                                      ) {
  tTS_IB_TAVOR_PRIVATE priv = device->private;
  VAPI_ret_t           ret;

  if (in_mad->dqpn       == 0                                &&
      in_mad->mgmt_class == TS_IB_MGMT_CLASS_SUBN_LID_ROUTED &&
      in_mad->r_method   == TS_IB_MGMT_METHOD_TRAP           &&
      in_mad->slid       == 0) {
    /* XXX locally generated trap from Tavor, forward to SM */
    return TS_IB_MAD_RESULT_SUCCESS | TS_IB_MAD_RESULT_CONSUMED;
  }

  /* Only handle SM gets, sets and trap represses for QP0 */
  if (in_mad->dqpn == 0) {
    if ((in_mad->mgmt_class != TS_IB_MGMT_CLASS_SUBN_LID_ROUTED &&
         in_mad->mgmt_class != TS_IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) ||
        (in_mad->r_method   != TS_IB_MGMT_METHOD_GET &&
         in_mad->r_method   != TS_IB_MGMT_METHOD_SET &&
         in_mad->r_method   != TS_IB_MGMT_METHOD_TRAP_REPRESS)) {
      return TS_IB_MAD_RESULT_SUCCESS;
    }

    /* Don't process SMInfo queries or vendor-specific MADs -- the SMA
       can't handle them. */
    if (be16_to_cpu(in_mad->attribute_id) == TS_IB_SM_SM_INFO ||
        be16_to_cpu(in_mad->attribute_id) >= TS_IB_SM_VENDOR_START) {
      TS_TRACE(MOD_KERNEL_IB, T_VERY_VERBOSE, TRACE_KERNEL_IB_GEN,
               "%s: Dropping unhandled SMP with attribute ID 0x%04x on port %d",
               device->name, be16_to_cpu(in_mad->attribute_id), in_mad->port);
      return TS_IB_MAD_RESULT_SUCCESS;
    }
  }

  /* Only handle PMA gets and sets for QP1 */
  if (in_mad->dqpn == 1 &&
      (in_mad->mgmt_class != TS_IB_MGMT_CLASS_PERF ||
       (in_mad->r_method  != TS_IB_MGMT_METHOD_GET &&
        in_mad->r_method  != TS_IB_MGMT_METHOD_SET))) {
    return TS_IB_MAD_RESULT_SUCCESS;
  }

  TS_TRACE(MOD_KERNEL_IB, T_VERY_VERBOSE, TRACE_KERNEL_IB_GEN,
           "%s: Processing MAD port %d slid 0x%04x (ignore_mkey=%d)",
           device->name, in_mad->port, in_mad->slid, ignore_mkey);

  ret = EVAPI_process_local_mad(priv->vapi_handle,
                                in_mad->port,
                                EVAPI_LOCAL_MAD_SLID(in_mad->slid)
                                EVAPI_PROC_MAD_OPTS(ignore_mkey ? EVAPI_MAD_IGNORE_MKEY : 0)
                                in_mad,
                                response_mad);
  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: EVAPI_process_local_mad failed, return code = %d (%s)",
                   device->name, ret, VAPI_strerror(ret));
    return TS_IB_MAD_RESULT_FAILURE;
  }

  /* set return bit in status of directed route responses*/
  if (in_mad->mgmt_class == TS_IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)
    response_mad->status |= cpu_to_be16(1 << 15);

  _tsIbTavorMadSnoop(device, in_mad);

  if (TS_IB_TAVOR_OVERRIDE_NODE_DESCRIPTION) {
    if (in_mad->dqpn         == 0 &&
        (in_mad->mgmt_class  == TS_IB_MGMT_CLASS_SUBN_LID_ROUTED ||
         in_mad->mgmt_class  == TS_IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) &&
        in_mad->r_method     == TS_IB_MGMT_METHOD_GET &&
        in_mad->attribute_id == cpu_to_be16(TS_IB_SM_NODE_DESCRIPTION)) {
      memcpy(response_mad->payload + 40, priv->node_desc, sizeof (tTS_IB_NODE_DESC));
    }
  }

  if (in_mad->r_method == TS_IB_MGMT_METHOD_TRAP_REPRESS) {
    /* no response for trap repress */
    return TS_IB_MAD_RESULT_SUCCESS;
  }

  return TS_IB_MAD_RESULT_SUCCESS | TS_IB_MAD_RESULT_REPLY;
}

void tsIbTavorSetNodeDesc(
                          tTS_IB_TAVOR_PRIVATE priv,
                          int                  index
                          ) {
  if (TS_IB_TAVOR_OVERRIDE_NODE_DESCRIPTION) {
    snprintf(priv->node_desc, sizeof (tTS_IB_NODE_DESC),
             "%s HCA-%d (Topspin HCA)",
             system_utsname.nodename, index + 1);
    priv->node_desc[sizeof (tTS_IB_NODE_DESC) - 1] = '\0';
  }
}
