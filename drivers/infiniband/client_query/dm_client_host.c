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

  $Id: dm_client_host.c,v 1.5 2004/02/25 00:35:18 roland Exp $
*/

#include "dm_client.h"

#include "ts_ib_mad.h"
#include "ts_ib_sa_client.h"
#include "ts_ib_dm_client_host.h"

#include "ts_kernel_trace.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_hash.h"
#include "ts_kernel_timer.h"

#ifndef W2K_OS
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>

#include <asm/system.h>
#include <asm/byteorder.h>

#else
#include <os_dep/win/linux/list.h>
#include <os_dep/win/linux/spinlock.h>
#endif

#if 1  /* low memory - problem with host with 2G memory */

#define TS_IB_DM_CLIENT_HOST_QUERY_MAX_NUM_HOST_PORTS     4
#define TS_IB_DM_CLIENT_HOST_MAX_NUM_IO_PORT              16
#define TS_IB_DM_CLIENT_HOST_MIN_CONTROLLER_ID            1
#define TS_IB_DM_CLIENT_HOST_MAX_CONTROLLER_ID            255
#define TS_IB_DM_CLIENT_HOST_MIN_SVC_ENTRY_ID             0
#define TS_IB_DM_CLIENT_HOST_MAX_NUM_DM_IOC               2
#define TS_IB_DM_CLIENT_HOST_MAX_NUM_DM_SVC               64

#else

#define TS_IB_DM_CLIENT_HOST_QUERY_MAX_NUM_HOST_PORTS     4
#define TS_IB_DM_CLIENT_HOST_MAX_NUM_IO_PORT              10
#define TS_IB_DM_CLIENT_HOST_MIN_CONTROLLER_ID            1
#define TS_IB_DM_CLIENT_HOST_MAX_CONTROLLER_ID            255
#define TS_IB_DM_CLIENT_HOST_MIN_SVC_ENTRY_ID             0
#define TS_IB_DM_CLIENT_HOST_MAX_NUM_DM_IOC               256
#define TS_IB_DM_CLIENT_HOST_MAX_NUM_DM_SVC               256

#endif

enum {
  TS_IB_DM_CLIENT_HOST_MAX_OUTSTANDING_QUERIES = 10
};

enum {
  TS_IB_DM_CLIENT_MAX_IO_PORT = 10
};

enum {
  TS_IB_DM_CLIENT_HOST_QUERY_HASH_BITS = 5,
  TS_IB_DM_CLIENT_HOST_QUERY_HASH_SIZE = 1 << TS_IB_DM_CLIENT_HOST_QUERY_HASH_BITS,
  TS_IB_DM_CLIENT_HOST_QUERY_HASH_MASK = TS_IB_DM_CLIENT_HOST_QUERY_HASH_SIZE - 1
};

typedef enum {
  TS_IB_DM_PORT_INFO = 1,
  TS_IB_DM_PORT_INFO_TBL
} tTS_IB_DM_QUERY_TYPE;

typedef struct tTS_IB_DM_CLIENT_HOST_IOC_PROFILE_STRUCT tTS_IB_DM_CLIENT_HOST_IOC_PROFILE_STRUCT,
  *tTS_IB_DM_CLIENT_HOST_IOC_PROFILE;
typedef struct tTS_IB_DM_CLIENT_HOST_SVC_ENTRY_STRUCT tTS_IB_DM_CLIENT_HOST_SVC_ENTRY_STRUCT,
  *tTS_IB_DM_CLIENT_HOST_SVC_ENTRY;
typedef struct tTS_IB_DM_CLIENT_HOST_QUERY_STRUCT tTS_IB_DM_CLIENT_HOST_QUERY_STRUCT,
  *tTS_IB_DM_CLIENT_HOST_QUERY;
typedef struct tTS_IB_DM_CLIENT_HOST_IO_PORT_STRUCT tTS_IB_DM_CLIENT_HOST_IO_PORT_STRUCT,
  *tTS_IB_DM_CLIENT_HOST_IO_PORT;

struct tTS_IB_DM_CLIENT_HOST_IO_PORT_STRUCT {
  int                                     is_valid;
  tTS_IB_LID                              io_port_lid;
  tTS_IB_IOU_INFO_STRUCT                  iou_info;
  tTS_IB_IOC_PROFILE_STRUCT               ioc_profiles[TS_IB_DM_CLIENT_HOST_MAX_NUM_DM_IOC];
  tTS_IB_SVC_ENTRY_STRUCT                 svc_entries[TS_IB_DM_CLIENT_HOST_MAX_NUM_DM_IOC][TS_IB_DM_CLIENT_HOST_MAX_NUM_DM_SVC];
  int                                     total_svc_entries_queries;
  int                                     responded_svc_entries_queries;
  int                                     is_responded;
};

struct tTS_IB_DM_CLIENT_HOST_QUERY_STRUCT {
  tTS_IB_DM_CLIENT_HOST_TID               transaction_id;
  tTS_IB_DEVICE_HANDLE                    host_device;
  tTS_IB_PORT                             host_port;
  tTS_IB_DM_CLIENT_HOST_IO_PORT           io_port[TS_IB_DM_CLIENT_HOST_MAX_NUM_IO_PORT];

  int                                     timeout_jiffies;
  tTS_IB_DM_CLIENT_HOST_COMPLETION_FUNC   completion_function;
  void                                    *completion_arg;

  tTS_KERNEL_TIMER_STRUCT                 timer;
  uint16_t                                is_timer_running;
  tTS_IB_DM_QUERY_TYPE                    type;
  struct list_head                        list;
};

static struct list_head query_hash[TS_IB_DM_CLIENT_HOST_QUERY_HASH_SIZE];
static spinlock_t query_hash_lock = SPIN_LOCK_UNLOCKED;
static tTS_IB_DM_CLIENT_HOST_TID cur_transaction_id;

static tTS_IB_DM_CLIENT_HOST_QUERY _tsIbDmAllocQuery(void)
{
  tTS_IB_DM_CLIENT_HOST_QUERY query =
    (tTS_IB_DM_CLIENT_HOST_QUERY)kmalloc(sizeof(tTS_IB_DM_CLIENT_HOST_QUERY_STRUCT), GFP_KERNEL);

  if (query) {
    int i;

  	memset(query, 0, sizeof(tTS_IB_DM_CLIENT_HOST_QUERY_STRUCT));
    for (i = 0; i < TS_IB_DM_CLIENT_HOST_MAX_NUM_IO_PORT; i++)
    {
       query->io_port[i] = kmalloc(sizeof(tTS_IB_DM_CLIENT_HOST_IO_PORT_STRUCT), GFP_KERNEL);
       if (query->io_port[i] == NULL)
       {
         return NULL;
       }
       else
       {
         memset(query->io_port[i], 0, sizeof(tTS_IB_DM_CLIENT_HOST_IO_PORT_STRUCT));
       }
    }
  } else {
	TS_REPORT_FATAL(MOD_KERNEL_IB,"Memory allocation for %d bytes failed",
		sizeof(tTS_IB_DM_CLIENT_HOST_QUERY_STRUCT));
  }

  return query;
}

static void _tsIbDmFreeQuery(tTS_IB_DM_CLIENT_HOST_QUERY query)
{
  int i;

  for (i = 0; i < TS_IB_DM_CLIENT_HOST_MAX_NUM_IO_PORT; i++)
  {
    kfree(query->io_port[i]);
  }

  kfree(query);
}

static tTS_IB_DM_CLIENT_HOST_IO_PORT _tsIbDmAllocIoPort(tTS_IB_DM_CLIENT_HOST_QUERY query,
                                                        tTS_IB_LID port_lid)
{
  int i;

  for (i = 0; i < TS_IB_DM_CLIENT_HOST_MAX_NUM_IO_PORT; i++)
  {
    if (query->io_port[i]->is_valid == 0)
    {
      query->io_port[i]->is_valid = 1;
      query->io_port[i]->io_port_lid = port_lid;
      query->io_port[i]->total_svc_entries_queries = 0;
      query->io_port[i]->responded_svc_entries_queries = 0;
      query->io_port[i]->is_responded = 0;
      return query->io_port[i];
    }
  }

  return NULL;
}

static tTS_IB_DM_CLIENT_HOST_IO_PORT _tsIbDmFindIoPort(tTS_IB_DM_CLIENT_HOST_QUERY query,
                                                       tTS_IB_LID port_lid)
{
  int i;

  for (i = 0; i < TS_IB_DM_CLIENT_HOST_MAX_NUM_IO_PORT; i++)
  {
    if (query->io_port[i]->is_valid == 1 &&
        query->io_port[i]->io_port_lid == port_lid)
    {
      return query->io_port[i];
    }
  }

  return NULL;
}

static inline tTS_IB_DM_CLIENT_HOST_TID _tsIbDmClientHostQueryHash(tTS_IB_DM_CLIENT_HOST_TID transaction_id)
{
  return tsKernelHashFunction((transaction_id & 0xffff) ^ (transaction_id >> 16),
                              TS_IB_DM_CLIENT_HOST_QUERY_HASH_MASK);
}

static tTS_IB_DM_CLIENT_HOST_QUERY _tsIbDmClientHostQueryFind(tTS_IB_DM_CLIENT_HOST_TID transaction_id)
{
  uint32_t hash = _tsIbDmClientHostQueryHash(transaction_id);
  struct list_head *cur;
  tTS_IB_DM_CLIENT_HOST_QUERY query;
  unsigned long flags;

  spin_lock_irqsave(&query_hash_lock, flags);
  list_for_each(cur, &query_hash[hash]) {
    query = list_entry(cur, tTS_IB_DM_CLIENT_HOST_QUERY_STRUCT, list);
    if (query->transaction_id == transaction_id) {
      goto out;
    }
  }
  query = NULL;

 out:
  spin_unlock_irqrestore(&query_hash_lock, flags);
  return query;
}

static void _tsIbDmClientHostQueryDelete(tTS_IB_DM_CLIENT_HOST_TID tid)
{
  tTS_IB_DM_CLIENT_HOST_QUERY query = _tsIbDmClientHostQueryFind(tid);

  if (query == NULL)
  {
    return;
  }

  if (query->is_timer_running)
  {
    query->is_timer_running = 0;
    tsKernelTimerRemove(&query->timer);
  }

  {
    unsigned long flags;

    spin_lock_irqsave(&query_hash_lock, flags);
    list_del(&query->list);
    spin_unlock_irqrestore(&query_hash_lock, flags);
  }

  _tsIbDmFreeQuery(query);
}

static void _tsIbDmClientHostQueryCleanup(tTS_IB_DM_CLIENT_HOST_TID tid)
{
  int i;
  int is_done = 1;

  tTS_IB_DM_CLIENT_HOST_QUERY query = _tsIbDmClientHostQueryFind(tid);

  if (query == NULL)
  {
    return;
  }

  for (i = 0; i < TS_IB_DM_CLIENT_HOST_MAX_NUM_IO_PORT; i++)
  {
    if (query->io_port[i]->is_valid == 1 &&
        query->io_port[i]->is_responded != 1)
    {
      is_done = 0;
    }
  }

  if (is_done)
  {
    if (query->is_timer_running)
    {
      query->is_timer_running = 0;
      tsKernelTimerRemove(&query->timer);
    }

    query->completion_function(0, NULL, query->completion_arg);
    _tsIbDmClientHostQueryDelete(query->transaction_id);
  }
}

static void _tsIbHostIoQueryTimeout(void *arg)
{
  tTS_IB_DM_CLIENT_HOST_TID transaction_id = (unsigned long) arg ;
  tTS_IB_DM_CLIENT_HOST_QUERY host_query = _tsIbDmClientHostQueryFind(transaction_id);

  if (host_query == NULL)
    return;

  /* Call completion function */
  host_query->completion_function(-ETIMEDOUT, NULL, host_query->completion_arg);

  /* Call final completion function */
  host_query->completion_function(0, NULL, host_query->completion_arg);

  /* Delete query entry */
  _tsIbDmClientHostQueryDelete(host_query->transaction_id);
}

static void _tsIbHostIoNotifyCaller(tTS_IB_DM_CLIENT_HOST_QUERY host_query,
                                    tTS_IB_DM_CLIENT_HOST_IO_PORT io_port)
{
  tTS_IB_DM_CLIENT_HOST_IO_LIST io_list;

  TS_TRACE(MOD_KERNEL_DM, T_VERY_VERBOSE, TRACE_KERNEL_IB_DM_GEN, "_tsIbHostIoNotifyCaller()");

  /* Cancel the timer */
  if (host_query->is_timer_running)
  {
    host_query->is_timer_running = 0;
    tsKernelTimerRemove(&host_query->timer);
  }

  /* Make the list of IO for the host to use */
  io_list = kmalloc(sizeof(tTS_IB_DM_CLIENT_HOST_IO_LIST_STRUCT), GFP_KERNEL);
  if (!io_list)
  {
    TS_REPORT_FATAL(MOD_KERNEL_IB, "Couldn't allocate space of io list");

    host_query->completion_function(-ENOMEM, NULL, host_query->completion_arg);

    _tsIbDmClientHostQueryDelete(host_query->transaction_id);

    return;
  }
  else
  {
    int controller_id;
    tTS_IB_DM_CLIENT_HOST_IO_SVC io_svc;

    memset(io_list, 0, sizeof(tTS_IB_DM_CLIENT_HOST_IO_LIST_STRUCT));

    INIT_LIST_HEAD(&io_list->io_svc_list);

    for (controller_id = TS_IB_DM_CLIENT_HOST_MIN_CONTROLLER_ID;
         controller_id <= io_port->iou_info.max_controllers;
         controller_id++)
    {
      int svc_id;
      int is_present = tsIbIsIocPresent(&io_port->iou_info, controller_id);

      if (is_present == 0)
      {
        continue;
      }

      for (svc_id = TS_IB_DM_CLIENT_HOST_MIN_SVC_ENTRY_ID;
           svc_id < io_port->ioc_profiles[controller_id].num_svc_entries;
           svc_id++)
      {
        io_svc = kmalloc(sizeof(tTS_IB_DM_CLIENT_HOST_IO_SVC_STRUCT), GFP_KERNEL);

        if (!io_svc)
        {
          tsIbHostIoListFree(io_list);

          host_query->completion_function(-ENOMEM, NULL, host_query->completion_arg);

          _tsIbDmClientHostQueryDelete(host_query->transaction_id);

          return;
        }
        else
        {
          io_svc->port_lid = io_port->io_port_lid;
          memcpy(io_svc->controller_guid, io_port->ioc_profiles[controller_id].guid, sizeof(tTS_IB_GUID));
          memcpy(&io_svc->svc_entry, &io_port->svc_entries[controller_id][svc_id], sizeof(tTS_IB_SVC_ENTRY_STRUCT));

          list_add_tail(&io_svc->list,
                        &io_list->io_svc_list);
        }
      } /* svc */
    } /* contoller */
  }

  host_query->completion_function(0, io_list, host_query->completion_arg);
  _tsIbDmClientHostQueryCleanup(host_query->transaction_id);
}

static void _tsIbHostIoSvcEntriesCompletion(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                            int status,
                                            tTS_IB_LID lid,
                                            tTS_IB_SVC_ENTRIES svc_entries,
                                            void* arg)
{
  tTS_IB_DM_CLIENT_HOST_TID host_transaction_id = (unsigned long) arg;
  tTS_IB_DM_CLIENT_HOST_QUERY host_query = _tsIbDmClientHostQueryFind(host_transaction_id);

  uint32_t i;
  uint32_t done = 1;
  int num_entries_to_query;
  tTS_IB_CLIENT_QUERY_TID tid;

  if (!host_query)
      return;

  if (status == 0)
  {
    uint32_t controller_id = svc_entries->controller_id;
    uint32_t begin_svc_entry_id = svc_entries->begin_svc_entry_id;
    uint32_t end_svc_entry_id = svc_entries->end_svc_entry_id;
    uint32_t svc_entry_id;
    tTS_IB_DM_CLIENT_HOST_IO_PORT io_port = _tsIbDmFindIoPort(host_query, lid);

    TS_TRACE(MOD_KERNEL_DM, T_VERY_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
             "SVC Entries (LID= 0x%x, controller_id= %d, begin_id= %d, end_id= %d) lookup done, status %d\n",
             lid,
             controller_id,
             begin_svc_entry_id,
             end_svc_entry_id,
             status);

    if (!io_port) {
      /* WARNING */
      TS_TRACE(MOD_KERNEL_DM, T_TERSE, TRACE_KERNEL_IB_DM_GEN,
               "Failed to find io_port on lid= 0x%x\n", lid);
      return;
    }

    /* store svc entries */
    for (i = begin_svc_entry_id; i <= end_svc_entry_id; i++)
    {
      svc_entry_id = i % TS_IB_SVC_ENTRIES_NUM_SVC_ENTRY;
      memcpy(&io_port->svc_entries[controller_id][i],
             &svc_entries->service_entries[svc_entry_id],
             sizeof(tTS_IB_SVC_ENTRY_STRUCT));
    }

    ++io_port->responded_svc_entries_queries;

    /* get the next block of svc entries */
    end_svc_entry_id += 1;
    if (end_svc_entry_id + TS_IB_SVC_ENTRIES_NUM_SVC_ENTRY <= io_port->ioc_profiles[controller_id].num_svc_entries)
    {
      num_entries_to_query = TS_IB_SVC_ENTRIES_NUM_SVC_ENTRY;
    }
    else
    {
      num_entries_to_query = io_port->ioc_profiles[controller_id].num_svc_entries - end_svc_entry_id;
    }

    if (io_port->responded_svc_entries_queries != io_port->total_svc_entries_queries)
    {
      done = 0;
    }

    if (done)
    {
      io_port->is_responded = 1;
      _tsIbHostIoNotifyCaller(host_query, io_port);
    }
    else
    {
      tsIbSvcEntriesRequest(host_query->host_device,
                            host_query->host_port,
                            io_port->io_port_lid,
                            controller_id,
                            end_svc_entry_id,
                            end_svc_entry_id + num_entries_to_query - 1,
                            HZ,
                            _tsIbHostIoSvcEntriesCompletion,
                            (void *) (unsigned long) host_query->transaction_id,
                            &tid);
    }
  }
  else
  {
    tTS_IB_DM_CLIENT_HOST_IO_PORT io_port = _tsIbDmFindIoPort(host_query, lid);

    if (io_port && io_port->is_responded == 0)
    {
      TS_TRACE(MOD_KERNEL_DM, T_VERY_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
               "SVC Entries (LID= 0x%x) lookup done, status %d\n",
               lid,
               status);

      host_query->completion_function(status, NULL, host_query->completion_arg);

      io_port->is_responded = 1;
      _tsIbDmClientHostQueryCleanup(host_query->transaction_id);
    }
  }
}

static void _tsIbHostIoIocProfileCompletion(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                            int status,
                                            tTS_IB_LID lid,
                                            tTS_IB_IOC_PROFILE ioc_profile,
                                            void* arg)
{
  tTS_IB_CLIENT_QUERY_TID tid;
  tTS_IB_DM_CLIENT_HOST_TID host_transaction_id = (unsigned long) arg;
  tTS_IB_DM_CLIENT_HOST_QUERY host_query = _tsIbDmClientHostQueryFind(host_transaction_id);

  if (!host_query)
      return;

  if (!status)
  {
    int svc_entry_id;
    int num_entries_to_query;
    tTS_IB_DM_CLIENT_HOST_IO_PORT io_port = _tsIbDmFindIoPort(host_query, lid);

    TS_TRACE(MOD_KERNEL_DM, T_VERY_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
             "IOC Profile (LID= 0x%x, controller_id %d) lookup done, status %d\n",
             lid,
             ioc_profile->controller_id,
             status);

    if (!io_port) {
      /* WARNING */
      TS_TRACE(MOD_KERNEL_DM, T_TERSE, TRACE_KERNEL_IB_DM_GEN,
               "Failed to find io_port on lid= 0x%x\n", lid);
      return;
    }

    memcpy(&io_port->ioc_profiles[ioc_profile->controller_id],
           ioc_profile,
           sizeof(tTS_IB_IOC_PROFILE_STRUCT));

    for (svc_entry_id = TS_IB_DM_CLIENT_HOST_MIN_SVC_ENTRY_ID;
         svc_entry_id < io_port->ioc_profiles[ioc_profile->controller_id].num_svc_entries;
         /* svc_entry_id incremented inside the loop */)
    {
      if ((svc_entry_id + TS_IB_SVC_ENTRIES_NUM_SVC_ENTRY) <= io_port->ioc_profiles[ioc_profile->controller_id].num_svc_entries)
      {
        num_entries_to_query = TS_IB_SVC_ENTRIES_NUM_SVC_ENTRY;
      }
      else
      {
        num_entries_to_query = io_port->ioc_profiles[ioc_profile->controller_id].num_svc_entries - svc_entry_id;
      }

      svc_entry_id += num_entries_to_query;

      ++io_port->total_svc_entries_queries;
    }

    if (TS_IB_SVC_ENTRIES_NUM_SVC_ENTRY <= io_port->ioc_profiles[ioc_profile->controller_id].num_svc_entries)
    {
      num_entries_to_query = TS_IB_SVC_ENTRIES_NUM_SVC_ENTRY;
    }
    else
    {
      num_entries_to_query = io_port->ioc_profiles[ioc_profile->controller_id].num_svc_entries - TS_IB_DM_CLIENT_HOST_MIN_SVC_ENTRY_ID;
    }

    tsIbSvcEntriesRequest(host_query->host_device,
                          host_query->host_port,
                          io_port->io_port_lid,
                          ioc_profile->controller_id,
                          TS_IB_DM_CLIENT_HOST_MIN_SVC_ENTRY_ID,
                          TS_IB_DM_CLIENT_HOST_MIN_SVC_ENTRY_ID + num_entries_to_query - 1,
                          HZ,
                          _tsIbHostIoSvcEntriesCompletion,
                          (void *) (unsigned long) host_query->transaction_id,
                          &tid);
  }
  else
  {
    tTS_IB_DM_CLIENT_HOST_IO_PORT io_port = _tsIbDmFindIoPort(host_query, lid);

    if (io_port && io_port->is_responded == 0)
    {
      TS_TRACE(MOD_KERNEL_DM, T_VERY_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
               "IOC Profile (LID= 0x%x) lookup done, status %d\n",
               lid,
               status);

      host_query->completion_function(status, NULL, host_query->completion_arg);

      io_port->is_responded = 1;
      _tsIbDmClientHostQueryCleanup(host_query->transaction_id);
    }
  }
}

static void _tsIbHostIoIouInfoCompletion(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                         int status,
                                         tTS_IB_LID lid,
                                         tTS_IB_IOU_INFO iou_info,
                                         void *arg)
{
  tTS_IB_CLIENT_QUERY_TID tid;
  tTS_IB_DM_CLIENT_HOST_TID host_transaction_id = (unsigned long) arg;
  tTS_IB_DM_CLIENT_HOST_QUERY host_query = _tsIbDmClientHostQueryFind(host_transaction_id);

  if (!host_query)
      return;

  TS_TRACE(MOD_KERNEL_DM, T_VERY_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
           "IOU info (IO port LID = 0x%x) lookup done, status %d\n",
           lid, status);

  if (!status)
  {
    tTS_IB_CONTROLLER_ID controller_id;
    tTS_IB_DM_CLIENT_HOST_IO_PORT io_port = _tsIbDmFindIoPort(host_query, lid);

    if (!io_port) {
      /* WARNING */
      TS_TRACE(MOD_KERNEL_DM, T_TERSE, TRACE_KERNEL_IB_DM_GEN,
               "Failed to find io_port on lid= 0x%x\n", lid);

      return;
    }

    memcpy(&io_port->iou_info, iou_info, sizeof(tTS_IB_IOU_INFO_STRUCT));

    /* Query IOC info */
    for (controller_id = TS_IB_DM_CLIENT_HOST_MIN_CONTROLLER_ID;
         controller_id <= io_port->iou_info.max_controllers;
         controller_id++)
    {
      int is_present = tsIbIsIocPresent(&io_port->iou_info, controller_id);

      if (is_present)
      {
        TS_TRACE(MOD_KERNEL_DM, T_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
               "Send IOC query for io_port on lid= 0x%x\n", io_port->io_port_lid);

        tsIbIocProfileRequest(host_query->host_device,
                              host_query->host_port,
                              io_port->io_port_lid,
                              controller_id,
                              HZ,
                              _tsIbHostIoIocProfileCompletion,
                              (void *) (unsigned long) host_query->transaction_id,
                              &tid);
      }
    }
  }
  else
  {
    tTS_IB_DM_CLIENT_HOST_IO_PORT io_port = _tsIbDmFindIoPort(host_query, lid);

    if (io_port && io_port->is_responded == 0)
    {
      TS_TRACE(MOD_KERNEL_DM, T_VERY_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
               "IOU Info (LID= 0x%x) lookup done, status %d\n",
               lid,
               status);

      host_query->completion_function(status, NULL, host_query->completion_arg);

      io_port->is_responded = 1;

      _tsIbDmClientHostQueryCleanup(host_query->transaction_id);
    }
  }
}

static void _tsIbHostIoClassPortInfoCopletion(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                              int status,
                                              tTS_IB_LID lid,
                                              tTS_IB_COMMON_ATTRIB_CPI class_port_info,
                                              void *arg)
{
  /* Nothing to do here */
  if (!status)
  {
    TS_TRACE(MOD_KERNEL_DM, T_VERY_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
             "DM ClassPortInfoSet(LID= 0x%x) complete successfully\n",
             lid);
  }
  else
  {
    TS_TRACE(MOD_KERNEL_DM, T_VERY_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
             "DM ClassPortInfoSet(LID= 0x%x) failed\n",
             lid);
  }
}

static void _tsIbHostIoPortInfoCompletion(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                          int status,
                                          tTS_IB_PORT_INFO port_info,
                                          void* arg)
{
  tTS_IB_CLIENT_QUERY_TID tid;
  tTS_IB_DM_CLIENT_HOST_TID host_transaction_id = (unsigned long) arg;
  tTS_IB_DM_CLIENT_HOST_QUERY host_query = _tsIbDmClientHostQueryFind(host_transaction_id);

  if (!host_query)
      return;

  if (status == 0)
  {
    if (TS_IB_PORT_CAP_GET(port_info, TS_IB_PORT_CAP_DM) != 0)
    {
      tTS_IB_DM_CLIENT_HOST_IO_PORT io_port = _tsIbDmAllocIoPort(host_query, port_info->lid);

      if (io_port)
      {
        TS_TRACE(MOD_KERNEL_DM, T_VERY_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
                 "Port Info (LID= 0x%x) lookup w/DM bit set, status %d\n", port_info->lid, status);

        /* Send DM ClassPortInfo to register for DM related traps */
        tsIbDmClassPortInfoRequest(host_query->host_device,
                                   host_query->host_port,
                                   io_port->io_port_lid,
                                   HZ,
                                   _tsIbHostIoClassPortInfoCopletion,
                                   (void *) (unsigned long) host_query->transaction_id,
                                   &tid);

        /* Query IOU info */
        tsIbIouInfoRequest(host_query->host_device,
                           host_query->host_port,
                           io_port->io_port_lid,
                           HZ,
                           _tsIbHostIoIouInfoCompletion,
                           (void *) (unsigned long) host_query->transaction_id,
                           &tid);
      }
      else
      {
        TS_TRACE(MOD_KERNEL_DM, T_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
                 "Port Info complete alloc error\n", status);
        status = -ENOMEM;
        goto QUERY_COMPLETE;

      }
    }
    else
    {
      if ( host_query->type == TS_IB_DM_PORT_INFO )
      {
        TS_TRACE(MOD_KERNEL_DM, T_VERY_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
                 "Port Info complete not managed Device\n", status);
        status = -EINVAL;
        goto QUERY_COMPLETE;
      }
    }
  }
  else
  {
    TS_TRACE(MOD_KERNEL_DM, T_VERY_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
             "Port Info lookup done, status %d\n", status);

    goto QUERY_COMPLETE;
  }

  return;

QUERY_COMPLETE:
    if (host_query->is_timer_running)
    {
      host_query->is_timer_running = 0;
      tsKernelTimerRemove(&host_query->timer);
    }

    /* Call completion function */
    host_query->completion_function(status, NULL, host_query->completion_arg);

    /* Call final completion function */
    host_query->completion_function(0, NULL, host_query->completion_arg);

    /* Delete query entry */
    _tsIbDmClientHostQueryDelete(host_query->transaction_id);

    return;
}

void tsIbHostIoQueryCancel(tTS_IB_DM_CLIENT_HOST_TID host_transaction_id)
{
  tTS_IB_DM_CLIENT_HOST_QUERY query = _tsIbDmClientHostQueryFind(host_transaction_id);

  if (query) {
    _tsIbDmClientHostQueryDelete(query->transaction_id);
  } else {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "canceling unknown TID 0x%016" TS_U64_FMT "x",
                   host_transaction_id);
  }
}

int tsIbHostIoQueryInit(void)
{
  int i;

  for (i = 0; i < TS_IB_DM_CLIENT_HOST_QUERY_HASH_SIZE; ++i) {
    INIT_LIST_HEAD(&query_hash[i]);
  }

  get_random_bytes(&cur_transaction_id, sizeof cur_transaction_id);

  if (cur_transaction_id == TS_IB_DM_CLIENT_HOST_TID_INVALID) {
    ++cur_transaction_id;
  }

  return 0;
}

void tsIbHostIoQueryCleanup(void)
{
}

int tsIbHostIoPortQuery(
                        tTS_IB_DEVICE_HANDLE device,
                        tTS_IB_PORT host_port,
                        tTS_IB_LID io_port_lid,
                        int timeout_jiffies,
                        tTS_IB_DM_CLIENT_HOST_COMPLETION_FUNC completion_function,
                        void *completion_arg,
                        tTS_IB_DM_CLIENT_HOST_TID *transaction_id
                        ) {
  tTS_IB_CLIENT_QUERY_TID tid;
  tTS_IB_DM_CLIENT_HOST_QUERY query = _tsIbDmAllocQuery();

  if (!query)
  {
    TS_REPORT_FATAL( MOD_KERNEL_DM, "no mem");
    return -ENOMEM;
  }

  query->type = TS_IB_DM_PORT_INFO;
  query->host_device = device;
  query->host_port = host_port;
  query->completion_function = completion_function;
  query->completion_arg = completion_arg;
  query->transaction_id = cur_transaction_id++;
  if (cur_transaction_id == TS_IB_DM_CLIENT_HOST_TID_INVALID)
  {
    ++cur_transaction_id;
  }

  /* start timer */
  tsKernelTimerInit(&query->timer);
  if (timeout_jiffies > 0)
  {
    query->timer.run_time = jiffies + timeout_jiffies;
    query->timer.function = _tsIbHostIoQueryTimeout;
    query->timer.arg      = (void *) (unsigned long) query->transaction_id;
    tsKernelTimerAdd(&query->timer);
    query->is_timer_running = 1;
  }

  /* link to the query list */
  {
    unsigned long flags;

    spin_lock_irqsave(&query_hash_lock, flags);
    list_add(&query->list,
             &query_hash[_tsIbDmClientHostQueryHash(query->transaction_id)]);
    spin_unlock_irqrestore(&query_hash_lock, flags);
  }

  /* Start with port info query */
  tsIbPortInfoQuery(query->host_device,
                    query->host_port,
                    io_port_lid,
                    0,
                    HZ,
                    _tsIbHostIoPortInfoCompletion,
                    (void *) (unsigned long) query->transaction_id,
                    &tid);

  *transaction_id = query->transaction_id;
  return 0;
}

int tsIbHostIoQuery(
                    tTS_IB_DEVICE_HANDLE device,
                    tTS_IB_PORT port,
                    int timeout_jiffies,
                    tTS_IB_DM_CLIENT_HOST_COMPLETION_FUNC completion_function,
                    void *completion_arg,
                    tTS_IB_DM_CLIENT_HOST_TID *transaction_id
                    ) {
  tTS_IB_CLIENT_QUERY_TID tid;
  tTS_IB_DM_CLIENT_HOST_QUERY query = _tsIbDmAllocQuery();

  if (!query)
  {
    TS_REPORT_FATAL( MOD_KERNEL_DM, "no mem");
    return -ENOMEM;
  }

  query->type = TS_IB_DM_PORT_INFO_TBL;
  query->host_device = device;
  query->host_port = port;
  query->completion_function = completion_function;
  query->completion_arg = completion_arg;
  query->transaction_id = cur_transaction_id++;
  if (cur_transaction_id == TS_IB_DM_CLIENT_HOST_TID_INVALID)
  {
    ++cur_transaction_id;
  }

  /* start timer */
  tsKernelTimerInit(&query->timer);
  if (timeout_jiffies > 0)
  {
    query->timer.run_time = jiffies + timeout_jiffies;
    query->timer.function = _tsIbHostIoQueryTimeout;
    query->timer.arg      = (void *) (unsigned long) query->transaction_id;
    tsKernelTimerAdd(&query->timer);
    query->is_timer_running = 1;
  }

  /* link to the query list */
  {
    unsigned long flags;

    spin_lock_irqsave(&query_hash_lock, flags);
    list_add(&query->list,
             &query_hash[_tsIbDmClientHostQueryHash(query->transaction_id)]);
    spin_unlock_irqrestore(&query_hash_lock, flags);
  }

  /* Start with port info query */
  if (use_port_info_tbl)
  {
    tsIbPortInfoTblQuery(query->host_device,
                         query->host_port,
                         5*HZ,
                         _tsIbHostIoPortInfoCompletion,
                         (void *) (unsigned long) query->transaction_id,
                         &tid);
  }
  else
  {
    /* XXX - currently we only support one IO port in the IB fabric */
    tsIbPortInfoQuery(query->host_device,
                      query->host_port,
                      0,
                      0,
                      HZ,
                      _tsIbHostIoPortInfoCompletion,
                      (void *) (unsigned long) query->transaction_id,
                      &tid);
  }

  *transaction_id = query->transaction_id;
  return 0;
}

int tsIbHostIoListFree(tTS_IB_DM_CLIENT_HOST_IO_LIST io_list)
{
  struct list_head *ptr;

  if (!io_list)
    return 0;

  for (ptr = io_list->io_svc_list.next;
       ptr != &io_list->io_svc_list;)
  {
    tTS_IB_DM_CLIENT_HOST_IO_SVC io_svc = list_entry(ptr, tTS_IB_DM_CLIENT_HOST_IO_SVC_STRUCT, list);;

    ptr = ptr->next;

    kfree(io_svc);
  }

  kfree(io_list);

  return 0;
}
