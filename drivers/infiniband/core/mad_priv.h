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

  $Id: mad_priv.h 32 2004-04-09 03:57:42Z roland $
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
#  define IB_ASSIGN_STATIC_LID 1
#else
#  define IB_ASSIGN_STATIC_LID 0
#endif

/* For static LIDs, should we limit ourselves to 10-bit values? */
#define IB_LIMIT_STATIC_LID_TO_1K 1

/* Should we check for the "anafa2" provider, and not change hop
   counts on outgoing DR SMPs if we have an Anafa 2?  This is required
   if we are using Anafa 2 firmware that handles hop counts itself. */
#define IB_ANAFA2_HOP_COUNT_WORKAROUND 1

enum {
  IB_MAD_MAX_PORTS_PER_DEVICE = 2,
  IB_MAD_SENDS_PER_QP         = 32,
  IB_MAD_RECEIVES_PER_QP      = 128,
  TS_IB_MAD_GRH_SIZE             = 40,
  TS_IB_MAD_PACKET_SIZE          = 256,
  TS_IB_MAD_BUFFER_SIZE          = TS_IB_MAD_PACKET_SIZE + TS_IB_MAD_GRH_SIZE
};

struct ib_mad_private {
  int                     num_port;

  tTS_IB_PD_HANDLE        pd;
  tTS_IB_CQ_HANDLE        cq;
  tTS_IB_MR_HANDLE        mr;
  tTS_IB_LKEY             lkey;
  tTS_IB_QP_HANDLE        qp[IB_MAD_MAX_PORTS_PER_DEVICE + 1][2];

  struct ib_mad          *send_buf   [IB_MAD_MAX_PORTS_PER_DEVICE + 1][2][IB_MAD_SENDS_PER_QP];
  void                   *receive_buf[IB_MAD_MAX_PORTS_PER_DEVICE + 1][2][IB_MAD_RECEIVES_PER_QP];
  /* send_list is a list of queued MADs waiting to be sent */
  struct list_head        send_list[IB_MAD_MAX_PORTS_PER_DEVICE + 1][2];
  /* send_free is a count of how many send slots we have free */
  int                     send_free[IB_MAD_MAX_PORTS_PER_DEVICE + 1][2];
  /* send_next is an index to the next free send slot */
  int                     send_next[IB_MAD_MAX_PORTS_PER_DEVICE + 1][2];

  tTS_KERNEL_QUEUE_THREAD work_thread;

#if IB_ANAFA2_HOP_COUNT_WORKAROUND
  int                     is_anafa2;
#endif
};

struct ib_mad_filter_list {
  TS_IB_DECLARE_MAGIC
  struct ib_mad_filter         filter;
  tTS_IB_MAD_DISPATCH_FUNCTION function;
  void                        *arg;
  int                          matches;
  int                          in_callback;
  struct list_head             list;
};

struct ib_mad_work {
  enum {
    IB_MAD_WORK_SEND_POST,
    IB_MAD_WORK_SEND_DONE,
    IB_MAD_WORK_RECEIVE
  }                  type;
  void              *buf;
  int                status;
  int                index;
  struct list_head   list;
};

extern kmem_cache_t *mad_cache;

void ib_mad_completion(tTS_IB_CQ_HANDLE    cq,
                       struct ib_cq_entry *entry,
                       void               *device_handle);

int ib_mad_post_receive(struct ib_device *device,
			tTS_IB_PORT   	  port,
			tTS_IB_QPN    	  qpn,
			int           	  index);

void ib_mad_dispatch(struct ib_mad *mad);

int ib_mad_post_send(struct ib_mad *mad,
		     int            index);

int ib_mad_send_no_copy(struct ib_mad *mad);

void ib_mad_static_assign(struct ib_device *device,
			  tTS_IB_PORT       port);

void ib_mad_work_thread(struct list_head *entry,
			void *device_ptr);

static inline void ib_mad_queue_work(struct ib_mad_private *priv,
				     struct ib_mad_work    *work)
{
  tsKernelQueueThreadAdd(priv->work_thread, &work->list);
}

int  ib_mad_proc_setup(void);
void ib_mad_proc_cleanup(void);

void ib_mad_invoke_filters(struct ib_mad       *mad,
			   tTS_IB_MAD_DIRECTION direction);

int ib_mad_filter_get_by_index(int                        index,
			       struct ib_mad_filter_list *filter);

#endif /* _MAD_PRIV_H */

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
