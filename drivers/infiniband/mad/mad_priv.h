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

  $Id: mad_priv.h,v 1.15 2004/02/25 00:35:21 roland Exp $
*/

#ifndef _MAD_PRIV_H
#define _MAD_PRIV_H

#ifndef __KERNEL__
#  define __KERNEL__
#endif
#ifndef MODULE
#  define MODULE
#endif

#ifndef W2K_OS
#  include <linux/config.h>
#endif
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#endif

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  ifndef W2K_OS
#    include <linux/modversions.h>
#  endif
#endif

#include "ts_ib_mad.h"
#include "ts_ib_core.h"
#include "ts_ib_provider.h"

#include "ts_kernel_thread.h"

#include <linux/slab.h>

/* Should we assign a static local LID? */
#if defined(TS_HOST_DRIVER)
#  define TS_IB_ASSIGN_STATIC_LID 1
#else
#  define TS_IB_ASSIGN_STATIC_LID 0
#endif

/* For static LIDs, should we limit ourselves to 10-bit values? */
#define TS_IB_LIMIT_STATIC_LID_TO_1K 1

/* Should we check for the "anafa2" provider, and not change hop
   counts on outgoing DR SMPs if we have an Anafa 2?  This is required
   if we are using Anafa 2 firmware that handles hop counts itself. */
#define TS_IB_ANAFA2_HOP_COUNT_WORKAROUND 1

enum {
  TS_IB_MAD_MAX_PORTS_PER_DEVICE = 2,
  TS_IB_MAD_SENDS_PER_QP         = 32,
  TS_IB_MAD_RECEIVES_PER_QP      = 128,
  TS_IB_MAD_GRH_SIZE             = 40,
  TS_IB_MAD_PACKET_SIZE          = 256,
  TS_IB_MAD_BUFFER_SIZE          = TS_IB_MAD_PACKET_SIZE + TS_IB_MAD_GRH_SIZE
};

typedef struct tTS_IB_MAD_PRIVATE_STRUCT tTS_IB_MAD_PRIVATE_STRUCT,
  *tTS_IB_MAD_PRIVATE;
typedef struct tTS_IB_MAD_FILTER_LIST_STRUCT tTS_IB_MAD_FILTER_LIST_STRUCT,
  *tTS_IB_MAD_FILTER_LIST;
typedef struct tTS_IB_MAD_WORK_STRUCT tTS_IB_MAD_WORK_STRUCT,
  *tTS_IB_MAD_WORK;

struct tTS_IB_MAD_PRIVATE_STRUCT {
  int                     num_port;

  tTS_IB_PD_HANDLE        pd;
  tTS_IB_CQ_HANDLE        cq;
  tTS_IB_MR_HANDLE        mr;
  tTS_IB_LKEY             lkey;
  tTS_IB_QP_HANDLE        qp[TS_IB_MAD_MAX_PORTS_PER_DEVICE + 1][2];

  tTS_IB_MAD              send_buf[TS_IB_MAD_MAX_PORTS_PER_DEVICE + 1][2][TS_IB_MAD_SENDS_PER_QP];
  void                   *receive_buf[TS_IB_MAD_MAX_PORTS_PER_DEVICE + 1][2][TS_IB_MAD_RECEIVES_PER_QP];
  /* send_list is a list of queued MADs waiting to be sent */
  struct list_head        send_list[TS_IB_MAD_MAX_PORTS_PER_DEVICE + 1][2];
  /* send_free is a count of how many send slots we have free */
  int                     send_free[TS_IB_MAD_MAX_PORTS_PER_DEVICE + 1][2];
  /* send_next is an index to the next free send slot */
  int                     send_next[TS_IB_MAD_MAX_PORTS_PER_DEVICE + 1][2];

  tTS_KERNEL_QUEUE_THREAD work_thread;

#if TS_IB_ANAFA2_HOP_COUNT_WORKAROUND
  int                     is_anafa2;
#endif
};

struct tTS_IB_MAD_FILTER_LIST_STRUCT {
  TS_IB_DECLARE_MAGIC
  tTS_IB_MAD_FILTER_STRUCT     filter;
  tTS_IB_MAD_DISPATCH_FUNCTION function;
  void                        *arg;
  int                          matches;
  int                          in_callback;
  struct list_head             list;
};

struct tTS_IB_MAD_WORK_STRUCT {
  enum {
    TS_IB_MAD_WORK_SEND_POST,
    TS_IB_MAD_WORK_SEND_DONE,
    TS_IB_MAD_WORK_RECEIVE
  }                  type;
  void              *buf;
  int                status;
  int                index;
  struct list_head   list;
};

extern kmem_cache_t *mad_cache;

void tsIbMadCompletion(
                       tTS_IB_CQ_HANDLE cq,
                       tTS_IB_CQ_ENTRY  entry,
                       void *           device_handle
                       );

int tsIbMadPostReceive(
                       tTS_IB_DEVICE device,
                       tTS_IB_PORT   port,
                       tTS_IB_QPN    qpn,
                       int           index
                       );

void tsIbMadDispatch(
                     tTS_IB_MAD mad
                     );

int tsIbMadPostSend(
                    tTS_IB_MAD mad,
                    int        index
                    );

int tsIbMadSendNoCopy(
                      tTS_IB_MAD mad
                      );

void tsIbMadStaticAssign(
                         tTS_IB_DEVICE device,
                         tTS_IB_PORT   port
                         );

void tsIbMadWorkThread(
                       struct list_head *entry,
                       void *device_ptr
                       );

static inline void tsIbMadQueueWork(
                                    tTS_IB_MAD_PRIVATE priv,
                                    tTS_IB_MAD_WORK    work
                                    ) {
  tsKernelQueueThreadAdd(priv->work_thread, &work->list);
}

int tsIbMadProcSetup(
                     void
                     );

void tsIbMadProcCleanup(
                        void
                        );

void tsIbMadInvokeFilters(
                          tTS_IB_MAD            mad,
                          tTS_IB_MAD_DIRECTION  direction
                          );

int tsIbMadFilterGetByIndex(
                            int                    index,
                            tTS_IB_MAD_FILTER_LIST filter
                            );

#endif /* _MAD_PRIV_H */
