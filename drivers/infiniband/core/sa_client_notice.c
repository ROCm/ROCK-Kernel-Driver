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

  $Id: sa_client_notice.c 32 2004-04-09 03:57:42Z roland $
*/

#include "sa_client.h"

#include "ts_ib_mad.h"
#include "ts_ib_sa_client.h"
#include "ts_ib_client_query.h"

#include "ts_kernel_trace.h"

#ifndef W2K_OS
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <asm/system.h>
#include <asm/byteorder.h>
#else
#include <os_dep/win/linux/string.h>
#include <os_dep/win/linux/module.h>
#endif

#define TS_IB_COMMON_NOTICE_GENERIC_GET(notice) \
        (((notice)->generic__type__producer_type_or_device_id & 0x80000000) >> 31)
#define TS_IB_COMMON_NOTICE_GENERIC_SET(notice) \
        ((notice)->generic__type__producer_type_or_device_id = \
          (notice)->generic__type__producer_type_or_device_id | (0x1 << 31))
#define TS_IB_COMMON_NOTICE_GENERIC_CLR(notice) \
        ((notice)->generic__type__producer_type_or_device_id = \
          (notice)->generic__type__producer_type_or_device_id & (~(0x1 << 31)))

#define TS_IB_COMMON_NOTICE_TYPE_GET(notice) \
        (((notice)->generic__type__producer_type_or_device_id & 0x7F000000) >> 24)
#define TS_IB_COMMON_NOTICE_TYPE_SET(notice, value) \
        ((notice)->generic__type__producer_type_or_device_id = \
          ((notice)->generic__type__producer_type_or_device_id & 0x80FFFFFF) | ((value & 0x7F) << 24))

#define TS_IB_COMMON_NOTICE_PRODUCER_TYPE_OR_VENDOR_ID_GET(notice) \
        ((notice)->generic__type__producer_type_or_device_id & 0x00FFFFFF)
#define TS_IB_COMMON_NOTICE_PRODUCER_TYPE_OR_VENDER_ID_SET(notice, value) \
        ((notice)->generic__type__producer_type_or_device_id = \
           ((notice)->generic__type__producer_type_or_device_id & 0xFF000000) | (value &0x00FFFFFF))

#define TS_IB_COMMON_NOTICE_TRAP_TOGGLE_COUNT_SET(notice) \
        ((notice)->notice_toggle_count = 0)

typedef struct tTS_IB_SA_NOTICE_STRUCT tTS_IB_SA_NOTICE_STRUCT,
  *tTS_IB_SA_NOTICE;
typedef struct tTS_IB_SA_NOTICE_HANDLER_STRUCT tTS_IB_SA_NOTICE_HANDLER_STRUCT,
 *tTS_IB_SA_NOTICE_HANDLER;

struct tTS_IB_SA_NOTICE_STRUCT {
  uint32_t        generic__type__producer_type_or_device_id;
  uint16_t        trap_number_or_vender_id;
  tTS_IB_LID      issuer_lid;
  uint16_t        toggle__count;
  uint8_t         data[54];
  tTS_IB_GID      issuer_gid;
}  __attribute__((packed));

struct tTS_IB_SA_NOTICE_HANDLER_STRUCT {
  int16_t                        trap_num;
  tTS_IB_DEVICE_HANDLE           device;
  tTS_IB_PORT                    port;
  tTS_IB_SA_NOTICE_HANDLER_FUNC  handler;
  void*                          arg;
  struct list_head               list;
};

static struct list_head  notice_handler_list;
static spinlock_t notice_handler_list_lock = SPIN_LOCK_UNLOCKED;

tTS_IB_SA_NOTICE_HANDLER _tsIbSaNoticeHandlerFind(int16_t trap_num,
                                                  tTS_IB_DEVICE_HANDLE device,
                                                  tTS_IB_PORT port) {
  struct list_head* cur;
  tTS_IB_SA_NOTICE_HANDLER notice_handler;
  unsigned long flags;

  spin_lock_irqsave(&notice_handler_list_lock, flags);
  list_for_each(cur, &notice_handler_list) {
    notice_handler = list_entry(cur, tTS_IB_SA_NOTICE_HANDLER_STRUCT, list);
    if (notice_handler->trap_num == trap_num &&
        notice_handler->device == device &&
        notice_handler->port == port)
    {
      goto out;
    }
  }
  notice_handler = NULL;

out:
  spin_unlock_irqrestore(&notice_handler_list_lock, flags);

  return notice_handler;
}

int  _tsIbSaNoticeHandlerDelete(
                                tTS_IB_SA_NOTICE_HANDLER notice_handler
                               ) {
  unsigned long flags;

  if (notice_handler) {
    spin_lock_irqsave(&notice_handler_list_lock, flags);
    list_del(&notice_handler->list);
    spin_unlock_irqrestore(&notice_handler_list_lock, flags);

    kfree(notice_handler);
  }

  return 0;
}

int  tsIbSaNoticeHandlerRegister(
                                 int16_t trap_num,
                                 tTS_IB_DEVICE_HANDLE device,
                                 tTS_IB_PORT port,
                                 tTS_IB_SA_NOTICE_HANDLER_FUNC handler,
                                 void *arg
                                 ) {
  if (handler)
  {
    /* Register */
    tTS_IB_SA_NOTICE_HANDLER notice_handler;

    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "Register to receive SA notice (trap_number=%d, dev=0x%x, port=%d)\n",
             trap_num, device, port);

    if (_tsIbSaNoticeHandlerFind(trap_num, device, port)) {
      /* Already registered */
      return 0;
    }

    notice_handler = kmalloc(sizeof *notice_handler, GFP_ATOMIC);

    if (!notice_handler) {
      return -ENOMEM;
    }

    notice_handler->trap_num = trap_num;
    notice_handler->device = device;
    notice_handler->port = port;
    notice_handler->handler = handler;
    notice_handler->arg = arg;

    {
      unsigned long flags;

      spin_lock_irqsave(&notice_handler_list_lock, flags);
      list_add(&notice_handler->list,
               &notice_handler_list);
      spin_unlock_irqrestore(&notice_handler_list_lock, flags);
    }
  }
  else
  {
    /* Unregister */
    tTS_IB_SA_NOTICE_HANDLER notice_handler;

    TS_TRACE(MOD_KERNEL_IB, T_TERSE, TRACE_KERNEL_IB_GEN,
             "Unregister to receive SA notice(trap_number= %d, dev= 0x%x, port=%d)\n",
             trap_num, device, port);

    notice_handler = _tsIbSaNoticeHandlerFind(trap_num, device, port);

    if (notice_handler) {
      _tsIbSaNoticeHandlerDelete(notice_handler);
    }
  }

  return 0;
}

tTS_IB_SA_NOTICE_HANDLER_FUNC  tsIbSaNoticeHandlerGet(
                                                      int16_t trap_num,
                                                      tTS_IB_DEVICE_HANDLE device,
                                                      tTS_IB_PORT port
                                                      ) {
  tTS_IB_SA_NOTICE_HANDLER notice_handler = _tsIbSaNoticeHandlerFind(trap_num, device, port);

  if (notice_handler) {
    return notice_handler->handler;
  }

  return NULL;
}

void *tsIbNoticeHandlerArgGet(
                              int16_t trap_num,
                              tTS_IB_DEVICE_HANDLE device,
                              tTS_IB_PORT port
                              ) {
  tTS_IB_SA_NOTICE_HANDLER notice_handler = _tsIbSaNoticeHandlerFind(trap_num, device, port);

  if (notice_handler) {
    return notice_handler->arg;
  }

  return NULL;
}

void tsIbSaNoticeHandler(
                         tTS_IB_MAD mad, void *arg
                         ) {
  tTS_IB_SA_PAYLOAD sa_payload = (tTS_IB_SA_PAYLOAD)mad->payload;
  tTS_IB_SA_NOTICE mad_notice = (tTS_IB_SA_NOTICE) sa_payload->admin_data;
  tTS_IB_COMMON_ATTRIB_NOTICE_STRUCT notice;
  tTS_IB_SA_NOTICE_HANDLER_FUNC handler;
  void* handler_arg;

  /* Convert to host order */
  mad_notice->generic__type__producer_type_or_device_id =
    be32_to_cpu(mad_notice->generic__type__producer_type_or_device_id);
  mad_notice->trap_number_or_vender_id =
    be16_to_cpu(mad_notice->trap_number_or_vender_id);
  mad_notice->issuer_lid = be16_to_cpu(mad_notice->issuer_lid);
  mad_notice->toggle__count = be16_to_cpu(mad_notice->toggle__count);

  TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
           "SA client notice handler: "
           "generic=0x%x, trap_num=0x%x\n",
           TS_IB_COMMON_NOTICE_GENERIC_GET(mad_notice), mad_notice->trap_number_or_vender_id);

  if (TS_IB_COMMON_NOTICE_GENERIC_GET(mad_notice) == TS_IB_COMMON_NOTICE_GENERIC)
  {
    notice.define.generic.type =
      TS_IB_COMMON_NOTICE_TYPE_GET(mad_notice);
    notice.define.generic.producer_type =
      TS_IB_COMMON_NOTICE_PRODUCER_TYPE_OR_VENDOR_ID_GET(mad_notice);
    notice.define.generic.trap_num = mad_notice->trap_number_or_vender_id;
    notice.define.generic.issuer_lid = mad_notice->issuer_lid;
    notice.toggle = mad_notice->toggle__count >> 15;
    notice.count = mad_notice->toggle__count & 0x7FFF;
    memcpy(notice.detail.data, mad_notice->data, 54);
    memcpy(notice.issuer_gid, mad_notice->issuer_gid, sizeof(tTS_IB_GID));

    handler = tsIbSaNoticeHandlerGet(notice.define.generic.trap_num,
                                     mad->device, mad->port);
    if (handler) {
      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "SA client notice handler forward to specific trap handler\n");

      handler_arg = tsIbNoticeHandlerArgGet(notice.define.generic.trap_num,
                                            mad->device, mad->port);
      handler(&notice, mad->port, handler_arg);
    }
  }
}

int tsIbSaNoticeInit(
                      void
                     )
{
  INIT_LIST_HEAD(&notice_handler_list);

  return 0;
}
