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

  $Id: dm_client_async_notify.c 32 2004-04-09 03:57:42Z roland $
*/

#include "dm_client.h"

#include "ts_ib_mad.h"

#include "ts_kernel_trace.h"

#ifndef W2K_OS
#include <linux/kernel.h>
#include <linux/slab.h>

#include <asm/system.h>
#include <asm/byteorder.h>
#endif

#define TS_IB_DM_NOTICE_GENERIC_GET(notice) \
        (((notice)->generic__type__producer_type_or_device_id & 0x80000000) >> 31)
#define TS_IB_DM_NOTICE_GENERIC_SET(notice) \
        ((notice)->generic__type__producer_type_or_device_id = \
          (notice)->generic__type__producer_type_or_device_id | (0x1 << 31))
#define TS_IB_DM_NOTICE_GENERIC_CLR(notice) \
        ((notice)->generic__type__producer_type_or_device_id = \
          (notice)->generic__type__producer_type_or_device_id & (~(0x1 << 31)))

#define TS_IB_DM_NOTICE_TYPE_GET(notice) \
        (((notice)->generic__type__producer_type_or_device_id & 0x7F000000) >> 24)
#define TS_IB_DM_NOTICE_TYPE_SET(notice, value) \
        ((notice)->generic__type__producer_type_or_device_id = \
          ((notice)->generic__type__producer_type_or_device_id & 0x80FFFFFF) | ((value & 0x7F) << 24))

#define TS_IB_DM_NOTICE_PRODUCER_TYPE_OR_VENDOR_ID_GET(notice) \
        ((notice)->generic__type__producer_type_or_device_id & 0x00FFFFFF)
#define TS_IB_DM_NOTICE_PRODUCER_TYPE_OR_VENDER_ID_SET(notice, value) \
        ((notice)->generic__type__producer_type_or_device_id = \
           ((notice)->generic__type__producer_type_or_device_id & 0xFF000000) | (value &0x00FFFFFF))

#define TS_IB_DM_NOTICE_TRAP_TOGGLE_COUNT_SET(notice) \
        ((notice)->notice_toggle_count = 0)

#define TS_IB_DM_NOTICE_READY_TO_TEST_TRAP_NUM    514

static tTS_IB_DM_NOTICE_HANDLER_FUNC ready_to_test_handler;
static void *                        ready_to_test_arg;

typedef struct tTS_IB_DM_NOTICE_STRUCT tTS_IB_DM_NOTICE_STRUCT,
  *tTS_IB_DM_NOTICE;

struct tTS_IB_DM_NOTICE_STRUCT {
  uint8_t         reserved[40];
  uint32_t        generic__type__producer_type_or_device_id;
  uint16_t        trap_number_or_vender_id;
  tTS_IB_LID      issuer_lid;
  uint16_t        toggle__count;
  uint8_t         data[54];
  tTS_IB_GID      issuer_gid;
}  __attribute__((packed));

void tsIbDmAsyncNotifyHandler(
                              tTS_IB_MAD packet,
                              void *arg
                              ) {
  tTS_IB_DM_NOTICE mad_notice = (tTS_IB_DM_NOTICE)packet->payload;
  tTS_IB_COMMON_ATTRIB_NOTICE_STRUCT notice;

  /* Convert to host order */
  mad_notice->generic__type__producer_type_or_device_id =
    be32_to_cpu(mad_notice->generic__type__producer_type_or_device_id);
  mad_notice->trap_number_or_vender_id =
    be16_to_cpu(mad_notice->trap_number_or_vender_id);
  mad_notice->issuer_lid = be16_to_cpu(mad_notice->issuer_lid);
  mad_notice->toggle__count = be16_to_cpu(mad_notice->toggle__count);

  TS_TRACE(MOD_KERNEL_DM, T_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
           "DM client notice handler: "
           "generic=0x%x, trap_num=0x%x\n",
           TS_IB_DM_NOTICE_GENERIC_GET(mad_notice), mad_notice->trap_number_or_vender_id);

  if (TS_IB_DM_NOTICE_GENERIC_GET(mad_notice) == TS_IB_COMMON_NOTICE_GENERIC)
  {
    notice.define.generic.type =
      TS_IB_DM_NOTICE_TYPE_GET(mad_notice);
    notice.define.generic.producer_type =
      TS_IB_DM_NOTICE_PRODUCER_TYPE_OR_VENDOR_ID_GET(mad_notice);
    notice.define.generic.trap_num = mad_notice->trap_number_or_vender_id;
    notice.define.generic.issuer_lid = mad_notice->issuer_lid;
    notice.toggle = mad_notice->toggle__count >> 15;
    notice.count = mad_notice->toggle__count & 0x7FFF;
    memcpy(notice.detail.data, mad_notice->data, 54);
    memcpy(notice.issuer_gid, mad_notice->issuer_gid, sizeof(tTS_IB_GID));

    if (notice.define.generic.trap_num == TS_IB_DM_NOTICE_READY_TO_TEST_TRAP_NUM)
    {
      if (ready_to_test_handler) {
        TS_TRACE(MOD_KERNEL_DM, T_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
                 "DM client notice handler forward to specific trap handler\n");
        ready_to_test_handler(&notice, packet->device, packet->port, packet->slid, ready_to_test_arg);
      }
      else
      {
        TS_TRACE(MOD_KERNEL_DM, T_TERSE, TRACE_KERNEL_IB_DM_GEN,
                 "ready_to_test_handler is NULL\n");
      }
    }
    else
    {
      TS_TRACE(MOD_KERNEL_DM, T_TERSE, TRACE_KERNEL_IB_DM_GEN,
               "Unsupported DM trap number= %d\n", notice.define.generic.trap_num);
    }
  }
}

int tsIbDmAsyncNotifyRegister(
                              uint16_t trap_num,
                              tTS_IB_DM_NOTICE_HANDLER_FUNC notify_func,
                              void * notify_arg
                              ) {
  int rc = 0;

  if (trap_num == TS_IB_DM_NOTICE_READY_TO_TEST_TRAP_NUM)
  {
    ready_to_test_handler = notify_func;
    ready_to_test_arg = notify_arg;
  }
  else
  {
    TS_TRACE(MOD_KERNEL_DM, T_TERSE, TRACE_KERNEL_IB_DM_GEN,
             "Register to unsupported DM trap number= %d\n", trap_num);

    rc = -1;
  }

  return rc;
}
