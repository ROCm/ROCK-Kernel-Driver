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

  $Id: tavor_async.c,v 1.5 2004/03/04 02:10:03 roland Exp $
*/

#include "tavor_priv.h"
#include "ts_ib_provider.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>

static struct {
  enum {
    QP,
    EEC,
    CQ,
    PORT,
    NONE
  }                  mod;
  tTS_IB_ASYNC_EVENT event;
} event_table[] = {
  [VAPI_QP_PATH_MIGRATED]             = { QP,   TS_IB_QP_PATH_MIGRATED                },
  [VAPI_EEC_PATH_MIGRATED]            = { EEC,  TS_IB_EEC_PATH_MIGRATED               },
  [VAPI_QP_COMM_ESTABLISHED]          = { QP,   TS_IB_QP_COMMUNICATION_ESTABLISHED    },
  [VAPI_EEC_COMM_ESTABLISHED]         = { EEC,  TS_IB_EEC_COMMUNICATION_ESTABLISHED   },
  [VAPI_SEND_QUEUE_DRAINED]           = { QP,   TS_IB_SEND_QUEUE_DRAINED              },
  [VAPI_CQ_ERROR]                     = { CQ,   TS_IB_CQ_ERROR                        },
  [VAPI_LOCAL_WQ_INV_REQUEST_ERROR]   = { QP,   TS_IB_LOCAL_WQ_INVALID_REQUEST_ERROR  },
  [VAPI_LOCAL_WQ_ACCESS_VIOL_ERROR]   = { QP,   TS_IB_LOCAL_WQ_ACCESS_VIOLATION_ERROR },
  [VAPI_LOCAL_WQ_CATASTROPHIC_ERROR]  = { QP,   TS_IB_LOCAL_WQ_CATASTROPHIC_ERROR     },
  [VAPI_PATH_MIG_REQ_ERROR]           = { QP,   TS_IB_PATH_MIGRATION_ERROR            },
  [VAPI_LOCAL_EEC_CATASTROPHIC_ERROR] = { EEC,  TS_IB_LOCAL_EEC_CATASTROPHIC_ERROR    },
  [VAPI_LOCAL_CATASTROPHIC_ERROR]     = { NONE, TS_IB_LOCAL_CATASTROPHIC_ERROR        },
  [VAPI_PORT_ERROR]                   = { PORT, TS_IB_PORT_ERROR                      },
  [VAPI_PORT_ACTIVE]                  = { PORT, TS_IB_PORT_ACTIVE                     }
};

static const int TS_IB_NUM_ASYNC_EVENT = sizeof event_table / sizeof event_table[0];

void tsIbTavorAsyncHandler(
                           VAPI_hca_hndl_t      hca,
                           VAPI_event_record_t *vapi_record,
                           void                *device_ptr
                           ) {
  tTS_IB_ASYNC_EVENT_RECORD_STRUCT record;
  tTS_IB_DEVICE                    device = device_ptr;
  tTS_IB_TAVOR_PRIVATE             priv   = device->private;
  VAPI_ret_t                       ret;

  if (vapi_record->type < 0 || vapi_record->type >= TS_IB_NUM_ASYNC_EVENT) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: Unknown async event %d",
                   device->name, vapi_record->type);
  }

  record.device = tsIbDeviceToHandle(device);
  record.event  = event_table[vapi_record->type].event;

  switch (event_table[vapi_record->type].mod) {
  case QP:
    ret = EVAPI_get_priv_context4qp(priv->vapi_handle,
                                    vapi_record->modifier.qp_hndl,
                                    &record.modifier.qp);
    if (ret != VAPI_OK) {
      /* Can happen in normal operation for a userspace QP */
      return;
    }
    break;

  case CQ:
    ret = EVAPI_get_priv_context4cq(priv->vapi_handle,
                                    vapi_record->modifier.qp_hndl,
                                    &record.modifier.cq);
    if (ret != VAPI_OK) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "%s: EVAPI_get_priv_context4cq failed, return code = %d (%s)",
                     device->name, ret, VAPI_strerror(ret));
      return;
    }
    break;

  case EEC:
    break;

  case PORT:
    record.modifier.port = vapi_record->modifier.port_num;
    break;

  case NONE:
    break;
  }

  tsIbAsyncEventDispatch(&record);
}
