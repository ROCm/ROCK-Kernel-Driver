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

  $Id: client_query.c 32 2004-04-09 03:57:42Z roland $
*/

#include "client_query.h"

#include "ts_ib_core.h"
#include "ts_ib_mad.h"
#include "ts_ib_client_query.h"
#include "ts_ib_rmpp_mad_types.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_hash.h"
#include "ts_kernel_timer.h"

#ifndef W2K_OS
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#else
#include <os_dep/win/linux/list.h>
#include <os_dep/win/linux/spinlock.h>
#endif

enum {
  TS_IB_CLIENT_QUERY_HASH_BITS = 10,
};


#define  TS_IB_CLIENT_RMPP_RESPONSE_TIMEOUT   (1*HZ)


/*
  type definitions
*/

typedef struct tTS_IB_CLIENT_QUERY_STRUCT tTS_IB_CLIENT_QUERY_STRUCT,
  *tTS_IB_CLIENT_QUERY;

struct tTS_IB_CLIENT_QUERY_STRUCT {
  uint64_t                                transaction_id;



  uint8_t                                 r_method;
  int                                     timeout_jiffies;

  union {
    tTS_IB_CLIENT_RESPONSE_FUNCTION       function;
    tTS_IB_CLIENT_RMPP_RESPONSE_FUNCTION  rmpp_function;
  } callback;
  int                                     callback_running;

  void *                                  arg;
  tTS_KERNEL_TIMER_STRUCT                 timer;
  tTS_IB_CLIENT_RMPP_RCV                  rmpp_rcv;
  tTS_LOCKED_HASH_ENTRY_STRUCT            entry;
};

struct tTS_IB_CLIENT_RMPP_RCV_STRUCT {
  uint8_t                                 *header;
  uint32_t                                header_length;
  uint8_t                                 *data;
  uint32_t                                data_length;
  tTS_KERNEL_TIMER_STRUCT                 response_timer;
};

static spinlock_t alloc_tid_lock = SPIN_LOCK_UNLOCKED;

static uint64_t cur_transaction_id;

tTS_HASH_TABLE query_hash;

static tTS_IB_CLIENT_QUERY _tsIbClientQueryFind(
                                                uint64_t transaction_id
                                                ) {
  tTS_LOCKED_HASH_ENTRY entry =
    tsKernelLockedHashLookup(query_hash, (uint32_t) (transaction_id & 0xffffffff));
#ifndef W2K_OS
  return entry
    ? TS_KERNEL_HASH_ENTRY(entry, tTS_IB_CLIENT_QUERY_STRUCT, entry)
    : NULL;
#else
  return entry ?
       ((char *)entry - offsetof(tTS_IB_CLIENT_QUERY_STRUCT,entry)):NULL;
#endif
}

static inline void _tsIbClientQueryPut(
                                       tTS_IB_CLIENT_QUERY query
                                       ) {
  tsKernelLockedHashRelease(&query->entry);
}

static void _tsIbClientQueryDelete(
                                   tTS_IB_CLIENT_QUERY query,
                                   uint16_t is_delete_data
                                   ) {
  tsKernelLockedHashRemove(query_hash, &query->entry);

  if (query->rmpp_rcv) {
    tsKernelTimerRemove(&query->rmpp_rcv->response_timer);

    if (is_delete_data && (query->rmpp_rcv->data)) {
      tsIbRmppClientFree(query->rmpp_rcv->data);
      query->rmpp_rcv->data = NULL;
      query->rmpp_rcv->data_length = 0;
    }

    if (query->rmpp_rcv->header) {
      tsIbRmppClientFree(query->rmpp_rcv->header);
      query->rmpp_rcv->header = NULL;
      query->rmpp_rcv->header_length = 0;
    }

    kfree(query->rmpp_rcv);
  }

  tsKernelTimerRemove(&query->timer);

  kfree(query);
}

/*
  Free query (and drop lock) so that we don't deadlock if consumer
  calls back into our code.
*/
static void _tsIbClientQueryCallback(
                                     tTS_IB_CLIENT_QUERY           query,
                                     tTS_IB_CLIENT_RESPONSE_STATUS status,
                                     tTS_IB_MAD                    packet
                                     ) {
  tTS_IB_CLIENT_QUERY_TID         transaction_id = query->transaction_id;
  tTS_IB_CLIENT_RESPONSE_FUNCTION function = query->callback.function;
  void                           *arg      = query->arg;

  tsKernelTimerRemove(&query->timer);

  if (query->callback_running) {
    _tsIbClientQueryPut(query);
    return;
  }

  query->callback_running = 1;
  _tsIbClientQueryPut(query);

  function(status, packet, arg);

  query = _tsIbClientQueryFind(transaction_id);
  if (query) {
    query->callback_running = 0;

    _tsIbClientQueryDelete(query, 0);
  }
}

static void _tsIbClientQueryRmppCallback(
                                         tTS_IB_CLIENT_QUERY           query,
                                         tTS_IB_CLIENT_RESPONSE_STATUS status,
                                         uint8_t                      *header,
                                         uint8_t                      *data,
                                         uint32_t                      data_size,
                                         uint16_t                      is_delete_data
                                         ) {
  tTS_IB_CLIENT_RMPP_RESPONSE_FUNCTION function = query->callback.rmpp_function;
  void                                *arg      = query->arg;

  _tsIbClientQueryDelete(query, is_delete_data);

  function(status, header, data, data_size, arg);
}

static void _tsIbClientQueryTimeout(
                                    void * arg
                                    ) {
  uint64_t transaction_id = (unsigned long) arg;
  tTS_IB_CLIENT_QUERY query = _tsIbClientQueryFind(transaction_id);

  if (query) {
    if (!query->rmpp_rcv) {
      _tsIbClientQueryCallback(query, TS_IB_CLIENT_RESPONSE_TIMEOUT, NULL);
    }
    else {
      _tsIbClientQueryRmppCallback(query, TS_IB_CLIENT_RESPONSE_TIMEOUT, NULL, NULL, 0, 1);
    }
  }
}

static void _tsIbClientRmppResponseTimeout(
                                           void * arg
                                           )
{
  uint64_t transaction_id = (unsigned long) arg;
  tTS_IB_CLIENT_QUERY query = _tsIbClientQueryFind(transaction_id);

  if (query) {
    if (!query->rmpp_rcv) {
      /* ERROR */
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "RMPP timeout for non-RMPP query");
      _tsIbClientQueryPut(query);
    }
    else {
      /* rmpp response timeout - different from transaction timeout */
      /* Right now, treat response timeout and transaction timeout to be the same */

      _tsIbClientQueryPut(query);
      _tsIbClientQueryTimeout(arg);
    }
  }
}

static tTS_IB_CLIENT_QUERY _tsIbClientQueryNew(
                                               tTS_IB_MAD packet,
                                               int timeout_jiffies,
                                               tTS_IB_CLIENT_RESPONSE_FUNCTION function,
                                               void *arg
                                               ) {
  tTS_IB_CLIENT_QUERY query = kmalloc(sizeof *query, GFP_ATOMIC);

  if (!query) {
    return NULL;
  }

  query->callback_running = 0;

  query->transaction_id       = packet->transaction_id;
  query->r_method             = packet->r_method;
  query->timeout_jiffies      = timeout_jiffies;
  query->callback.function    = function;
  query->arg                  = arg;

  tsKernelTimerInit(&query->timer);
  if (timeout_jiffies > 0) {
    query->timer.run_time   = jiffies + timeout_jiffies;
    query->timer.function   = _tsIbClientQueryTimeout;
    query->timer.arg        = (void *) (unsigned long) query->transaction_id;
    tsKernelTimerAdd(&query->timer);
  }

  query->rmpp_rcv = NULL;

  query->entry.key = (uint32_t) (query->transaction_id & 0xffffffff);
  tsKernelLockedHashStore(query_hash, &query->entry);

  return query;
}

static tTS_IB_CLIENT_QUERY _tsIbClientRmppQueryNew(
                                                   tTS_IB_MAD packet,
                                                   int timeout_jiffies,
                                                   int header_length,
                                                   tTS_IB_CLIENT_RMPP_RESPONSE_FUNCTION function,
                                                   void *arg
                                                   ) {
  tTS_IB_CLIENT_QUERY query = kmalloc(sizeof *query, GFP_ATOMIC);

  if (!query) {
    return NULL;
  }

  TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN, "_tsIbClientRmppQueryNew()\n");

  query->transaction_id         = packet->transaction_id;
  query->r_method               = packet->r_method;
  query->timeout_jiffies        = timeout_jiffies;
  query->callback.rmpp_function = function;
  query->arg                    = arg;

  tsKernelTimerInit(&query->timer);
  if (timeout_jiffies > 0) {
    query->timer.run_time   = jiffies + timeout_jiffies;
    query->timer.function   = _tsIbClientQueryTimeout;
    query->timer.arg        = (void *) (unsigned long) query->transaction_id;
    tsKernelTimerAdd(&query->timer);
  }

  query->rmpp_rcv = kmalloc(sizeof *query->rmpp_rcv, GFP_ATOMIC);
  if (header_length > 0) {
    query->rmpp_rcv->header = kmalloc(header_length, GFP_ATOMIC);
  }
  else {
    query->rmpp_rcv->header = NULL;
  }
  query->rmpp_rcv->header_length = header_length;
  query->rmpp_rcv->data = NULL;
  query->rmpp_rcv->data_length = 0;
  tsKernelTimerInit(&query->rmpp_rcv->response_timer);
  query->rmpp_rcv->response_timer.run_time = jiffies + TS_IB_CLIENT_RMPP_RESPONSE_TIMEOUT;
  query->rmpp_rcv->response_timer.function = _tsIbClientRmppResponseTimeout;
  query->rmpp_rcv->response_timer.arg = (void *) (unsigned long) query->transaction_id;
  tsKernelTimerAdd(&query->rmpp_rcv->response_timer);

  query->entry.key = (uint32_t) (query->transaction_id & 0xffffffff);
  tsKernelLockedHashStore(query_hash, &query->entry);

  return query;
}

static void _tsIbClientQueryRmppRcvMad(
                                       tTS_IB_CLIENT_QUERY query,
                                       tTS_IB_MAD mad
                                       )
{
  tTS_IB_CLIENT_RMPP_MAD rmpp_mad = (tTS_IB_CLIENT_RMPP_MAD)mad;
  uint16_t status;
  uint16_t attribute_id;
  uint32_t attribute_modifier;
  uint8_t flag;
  int header_length = query->rmpp_rcv->header_length;
  int bytes_to_copy;

  /* Convert to host format */
  status = be16_to_cpu(mad->status);
  attribute_id = be16_to_cpu(mad->attribute_id);
  attribute_modifier = be32_to_cpu(mad->attribute_modifier);
  flag = rmpp_mad->resp_time__flags & 0x0F;

  TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN, "_tsIbClientQueryRmppRcvMad(flag= 0x%x)\n", flag);

  /* Basic checking */
  if ((flag & TS_IB_CLIENT_RMPP_FLAG_ACTIVE) == 0)
    return;

  switch (rmpp_mad->type)
  {
    case TS_IB_CLIENT_RMPP_TYPE_DATA:
      {
        uint32_t segment_number = be32_to_cpu(rmpp_mad->specific.data.segment_number);
        uint32_t payload_length = be32_to_cpu(rmpp_mad->specific.data.payload_length);

        TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
                 "Data - segment_number= %d, payload_length= %d\n", segment_number, payload_length);

        /* stop response timer */
        tsKernelTimerRemove(&query->rmpp_rcv->response_timer);

        /* if first - allocate data */
        if (flag & TS_IB_CLIENT_RMPP_FLAG_DATA_FIRST) {
          if (query->rmpp_rcv->data) {
            /* WARNING */
          }
          else {
            TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN, "Allocate data on first segment\n");
            query->rmpp_rcv->data_length = payload_length;
            query->rmpp_rcv->data = kmalloc(payload_length, GFP_ATOMIC);
          }

          if (payload_length > (TS_IB_CLIENT_RMPP_PAYLOAD_LENGTH - header_length))
          {
            bytes_to_copy = TS_IB_CLIENT_RMPP_PAYLOAD_LENGTH - header_length;
          }
          else
          {
            bytes_to_copy = payload_length - header_length;
          }
        }
        else if (flag & TS_IB_CLIENT_RMPP_FLAG_DATA_LAST) {
          bytes_to_copy = payload_length - header_length;
        }
        else {
          bytes_to_copy = TS_IB_CLIENT_RMPP_PAYLOAD_LENGTH - header_length;
        }

        /* copy header */
        if (query->rmpp_rcv->header_length > 0) {
          memcpy(query->rmpp_rcv->header, rmpp_mad->payload, query->rmpp_rcv->header_length);
        }

        /* copy data */
        {
          int query_data_offset = (segment_number - 1) * (TS_IB_CLIENT_RMPP_PAYLOAD_LENGTH-header_length);

          memcpy(query->rmpp_rcv->data + query_data_offset,
                 rmpp_mad->payload + header_length,
                 bytes_to_copy);
        }

        /* if last - call user supplied completion */
        if (flag & TS_IB_CLIENT_RMPP_FLAG_DATA_LAST) {
          TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN, "get last segment of transaction\n");

          tsKernelTimerRemove(&query->timer);

          _tsIbClientQueryRmppCallback(query,
                                       TS_IB_CLIENT_RESPONSE_OK,
                                        query->rmpp_rcv->header,
                                        query->rmpp_rcv->data,
                                        query->rmpp_rcv->data_length - (segment_number*query->rmpp_rcv->header_length),
                                       0);
        } else {
          _tsIbClientQueryPut(query);
        }

        /* send back ack with sliding window of 1 */
        TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN, "Send back ack, dlid= %d\n", mad->slid);
        mad->dlid = mad->slid;
        mad->completion_func = NULL;
        rmpp_mad->type = TS_IB_CLIENT_RMPP_TYPE_ACK;
        rmpp_mad->specific.ack.segment_number = cpu_to_be32(segment_number);
        rmpp_mad->specific.ack.new_window_last = cpu_to_be32(segment_number + 1);
        tsIbMadSend(mad);
      }
      break;
    case TS_IB_CLIENT_RMPP_TYPE_ACK:
      /* Only implement receiver - don't need to handle ack */
      break;
    case TS_IB_CLIENT_RMPP_TYPE_STOP:
    case TS_IB_CLIENT_RMPP_TYPE_ABORT:
      {
        /* call the callback with error code and delete query */
        _tsIbClientQueryRmppCallback(query,
                                     TS_IB_CLIENT_RESPONSE_ERROR,
                                     NULL,
                                     NULL,
                                     0,
                                     1);
      }
      break;
    default:
      break;
  }

  _tsIbClientQueryPut(query);
}

static void _tsIbClientGetResponse(
                                   tTS_IB_MAD mad
                                   ) {
  tTS_IB_CLIENT_QUERY query = _tsIbClientQueryFind(mad->transaction_id);
  tTS_IB_CLIENT_RESPONSE_STATUS resp_status;

  if (!query) {
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "packet received for unknown TID 0x%016" TS_U64_FMT "x",
             mad->transaction_id);
    return;
  }

  if (query->r_method == mad->r_method) {
    _tsIbClientQueryPut(query);
    return;
  }

  if (mad->status) {
    resp_status = TS_IB_CLIENT_RESPONSE_ERROR;
  }
  else
  {
    resp_status = TS_IB_CLIENT_RESPONSE_OK;
  }

  if (!query->rmpp_rcv) {

    _tsIbClientQueryCallback(query, resp_status, mad);
  }
  else {
    _tsIbClientQueryRmppRcvMad(query, mad);
  }
}

int tsIbClientQuery(
                    tTS_IB_MAD packet,
                    int timeout_jiffies,
                    tTS_IB_CLIENT_RESPONSE_FUNCTION function,
                    void *arg
                    ) {
  tTS_IB_CLIENT_QUERY query;

  /* Allocate new query */
  query = _tsIbClientQueryNew(packet,
                              timeout_jiffies,
                              function,
                              arg);

  if (!query) {
    return -ENOMEM;
  }

  _tsIbClientQueryPut(query);

#ifndef W2K_OS
  if (0) {
    int i;

    TS_TRACE(MOD_KERNEL_IB, T_VERY_VERBOSE, TRACE_KERNEL_IB_GEN,
             "Sending query packet:");

    for (i = 0; i < 256; ++i) {
      if (i % 8 == 0) {
        printk(KERN_INFO "   ");
      }
      printk(" %02x", ((uint8_t *) packet)[i]);
      if ((i + 1) % 8 == 0) {
        printk("\n");
      }
    }
  }
#endif
  /* Send out MAD */
  tsIbMadSend(packet);

  return 0;
}

int tsIbClientQueryCancel(
                          tTS_IB_CLIENT_QUERY_TID transaction_id
                          ) {
  tTS_IB_CLIENT_QUERY query = _tsIbClientQueryFind(transaction_id);

  while (query && query->callback_running) {
    _tsIbClientQueryPut(query);
    set_current_state(TASK_RUNNING);
    schedule();
    query = _tsIbClientQueryFind(transaction_id);
  }

  if (!query) {
    return -ENOENT;
  }

  if (!query->rmpp_rcv) {
    query->callback.function(TS_IB_CLIENT_RESPONSE_CANCEL, NULL, query->arg);
  } else {
    query->callback.rmpp_function(TS_IB_CLIENT_RESPONSE_CANCEL,
                                  NULL, NULL, 0, query->arg);
  }

  _tsIbClientQueryDelete(query, 1);

  return 0;
}

void tsIbClientMadHandler(
                          tTS_IB_MAD mad,
                          void *arg
                          ) {
  TS_TRACE(MOD_KERNEL_IB, T_VERY_VERBOSE, TRACE_KERNEL_IB_GEN,
           "query packet received, TID 0x%016" TS_U64_FMT "x",
           mad->transaction_id);
#ifndef W2K_OS
  if (0) {
    int i;

    /* Dump packet */

    for (i = 0; i < 256; ++i) {
      if (i % 8 == 0) {
        printk(KERN_INFO "   ");
      }
      printk(" %02x", ((uint8_t *) mad)[i]);
      if ((i + 1) % 8 == 0) {
        printk("\n");
      }
    }
  }
#endif
  switch (mad->r_method) {
  case TS_IB_MGMT_METHOD_REPORT:
  {
    /* Send back REPORT RESPONSE */
    tTS_IB_MAD_STRUCT report_resp;

    memcpy(&report_resp, mad, sizeof(report_resp));
    report_resp.r_method = TS_IB_MGMT_METHOD_REPORT_RESPONSE;
    report_resp.slid = mad->dlid;
    report_resp.dlid = mad->slid;
    report_resp.sqpn = mad->dqpn;
    report_resp.dqpn = mad->sqpn;
    report_resp.completion_func = NULL;

    tsIbMadSend(&report_resp);

    /* Continue to call the async handler */
  }
  case TS_IB_MGMT_METHOD_TRAP:
  case TS_IB_MGMT_METHOD_TRAP_REPRESS:
  {
    tTS_IB_MAD_DISPATCH_FUNCTION dispatch =
      tsIbClientAsyncMadHandlerGet(mad->mgmt_class);

    if (dispatch) {
      dispatch(mad, tsIbClientAsynMadHandlerArgGet(mad->mgmt_class));
    }
    return;
  }

  default:
    _tsIbClientGetResponse(mad);
    return;
  }
}

int tsIbClientQueryInit(
                        void
                        ) {
  uint64_t invalid_tid = TS_IB_CLIENT_QUERY_TID_INVALID & 0xFFFFFFFF;

  if (tsKernelHashTableCreate(TS_IB_CLIENT_QUERY_HASH_BITS, &query_hash)) {
    return -ENOMEM;
  }

  get_random_bytes(&cur_transaction_id, sizeof cur_transaction_id);

  cur_transaction_id &= 0xFFFFFFFF;
  if (cur_transaction_id == invalid_tid) {
    cur_transaction_id = 0;
  }

  return 0;
}

void tsIbClientQueryCleanup(
                            void
                            ) {
  /* XXX cancel all queries and delete timers */

  tsKernelHashTableDestroy(query_hash);
}

tTS_IB_CLIENT_QUERY_TID tsIbClientAllocTid()
{
  tTS_IB_CLIENT_QUERY_TID tid;
  uint64_t invalid_tid = TS_IB_CLIENT_QUERY_TID_INVALID & 0xFFFFFFFF;
  unsigned long flags;

  spin_lock_irqsave(&alloc_tid_lock, flags);
  tid  = cur_transaction_id++;
  cur_transaction_id &= 0xFFFFFFFF;
  if (cur_transaction_id == invalid_tid) {
    cur_transaction_id = 0;
  }
  spin_unlock_irqrestore(&alloc_tid_lock, flags);

  return tid;
}

int tsIbRmppClientQuery(
                        tTS_IB_MAD packet,
                        int timeout_jiffies,
                        int header_length,
                        tTS_IB_CLIENT_RMPP_RESPONSE_FUNCTION function,
                        void *arg
                        ) {
  tTS_IB_CLIENT_QUERY query;

  TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN, "tsIbRmppClientQuery()\n");

  query = _tsIbClientRmppQueryNew(packet,
                                  timeout_jiffies,
                                  header_length,
                                  function,
                                  arg);

  if (!query) {
    return -ENOMEM;
  }

  _tsIbClientQueryPut(query);

  tsIbMadSend(packet);

  return 0;
}

int tsIbRmppClientFree(
                       uint8_t *data
                       ) {
  if (data) {
    kfree(data);
  }

  return 0;
}
