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

  $Id: dm_client_host.c 32 2004-04-09 03:57:42Z roland $
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

#define TS_IB_DM_CLIENT_HOST_TIMEOUT 10 

enum {
  TS_IB_DM_CLIENT_HOST_MAX_OUTSTANDING_QUERIES = 10
};

enum {
  TS_IB_DM_CLIENT_MAX_IO_PORT = 10
};
  
enum {
  TS_IB_DM_CLIENT_HOST_QUERY_HASH_BITS = 10,
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
  uint64_t                                transaction_id;
  tTS_IB_DEVICE_HANDLE                    host_device;
  tTS_IB_PORT                             host_port;
  tTS_IB_DM_CLIENT_HOST_IO_PORT           io_port[TS_IB_DM_CLIENT_HOST_MAX_NUM_IO_PORT];

  int                                     timeout_jiffies;
  tTS_IB_DM_CLIENT_HOST_COMPLETION_FUNC   completion_function;
  void                                    *completion_arg;

  tTS_KERNEL_TIMER_STRUCT                 timer;
  tTS_IB_DM_QUERY_TYPE                    type;

  int                                     callback_running;
  tTS_LOCKED_HASH_ENTRY_STRUCT            entry;
};

static spinlock_t alloc_tid_lock = SPIN_LOCK_UNLOCKED;

static uint64_t cur_transaction_id;

tTS_HASH_TABLE query_hash;

uint64_t _tsIbHostIoAllocTid(void)
{
  uint64_t tid;
  uint64_t invalid_tid = TS_IB_DM_CLIENT_HOST_TID_INVALID & 0xFFFFFFFF;
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

static tTS_IB_DM_CLIENT_HOST_QUERY _tsIbDmClientHostQueryFind(uint64_t transaction_id) 
{
  tTS_LOCKED_HASH_ENTRY entry =
    tsKernelLockedHashLookup(query_hash, (uint32_t) (transaction_id & 0xffffffff));
#ifndef W2K_OS
  return entry
    ? TS_KERNEL_HASH_ENTRY(entry, tTS_IB_DM_CLIENT_HOST_QUERY_STRUCT, entry)
    : NULL;
#else
  return entry ?
       ((char *)entry - offsetof(tTS_IB_DM_CLIENT_HOST_QUERY_STRUCT,entry)):NULL;
#endif
}

static inline void _tsIbDmClientHostQueryPut(tTS_IB_DM_CLIENT_HOST_QUERY query) 
{
  tsKernelLockedHashRelease(&query->entry);
}

static void _tsIbDmClientHostQueryDelete(tTS_IB_DM_CLIENT_HOST_QUERY query) 
{
  tsKernelLockedHashRemove(query_hash, &query->entry);

  tsKernelTimerRemove(&query->timer);

  _tsIbDmFreeQuery(query);
}

static void _tsIbDmClientHostQueryComplete(tTS_IB_DM_CLIENT_HOST_QUERY query,
                                           int status,
                                           tTS_IB_DM_CLIENT_HOST_IO_LIST io_list,
                                           int force_done)
{
  int i;
  int is_done = 1;
  uint64_t transaction_id = query->transaction_id;
  tTS_IB_DM_CLIENT_HOST_COMPLETION_FUNC function = query->completion_function;
  void *arg = query->completion_arg;

  function(status, io_list, arg);

  if (force_done == 0)
  {
    for (i = 0; i < TS_IB_DM_CLIENT_HOST_MAX_NUM_IO_PORT; i++)
    {
      if (query->io_port[i]->is_valid == 1 &&
          query->io_port[i]->is_responded != 1)
      {
        is_done = 0;
      }
    }
  }

  if (is_done)
  {
    function(0, NULL, query->completion_arg);
      
    query = _tsIbDmClientHostQueryFind(transaction_id);
    if (query) {
 
      query->callback_running = 0;

      _tsIbDmClientHostQueryDelete(query);
    }
  }
}

static void _tsIbHostIoQueryTimeout(void *arg)
{
  uint64_t host_transaction_id = (unsigned long) arg ;
  tTS_IB_DM_CLIENT_HOST_QUERY query = _tsIbDmClientHostQueryFind(host_transaction_id);

  while(query && query->callback_running) {
    _tsIbDmClientHostQueryPut(query);
    schedule();
    query = _tsIbDmClientHostQueryFind(host_transaction_id);
  }
  
  if (!query) {
    return;
  }

  query->callback_running = 1;

  _tsIbDmClientHostQueryPut(query);

  _tsIbDmClientHostQueryComplete(query, -ETIMEDOUT, NULL, 1);
 
  return;
}

static void _tsIbHostIoNotifyCaller(tTS_IB_DM_CLIENT_HOST_QUERY host_query,
                                    tTS_IB_DM_CLIENT_HOST_IO_PORT io_port)
{
  tTS_IB_DM_CLIENT_HOST_IO_LIST io_list;

  TS_TRACE(MOD_KERNEL_DM, T_VERY_VERBOSE, TRACE_KERNEL_IB_DM_GEN, "_tsIbHostIoNotifyCaller()");
  
  /* Make the list of IO for the host to use */
  io_list = kmalloc(sizeof(tTS_IB_DM_CLIENT_HOST_IO_LIST_STRUCT), GFP_KERNEL);
  if (!io_list)
  {
    TS_REPORT_FATAL(MOD_KERNEL_IB, "Couldn't allocate space of io list");
    
    _tsIbDmClientHostQueryComplete(host_query, -ENOMEM, NULL, 1);
    
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
          
          _tsIbDmClientHostQueryComplete(host_query, -ENOMEM, NULL, 1);
    
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
    
  _tsIbDmClientHostQueryComplete(host_query, 0, io_list, 0);
}

static void _tsIbHostIoSvcEntriesCompletion(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                            int status,
                                            tTS_IB_LID lid,
                                            tTS_IB_SVC_ENTRIES svc_entries,
                                            void* arg)
{
  uint64_t host_transaction_id = (unsigned long) arg;
  tTS_IB_DM_CLIENT_HOST_QUERY host_query = _tsIbDmClientHostQueryFind(host_transaction_id);

  uint32_t i;
  uint32_t done = 1;
  int num_entries_to_query;
  tTS_IB_CLIENT_QUERY_TID tid;

  /* Do a little query house keeping */
  if (!host_query) {
      return;
  } else {

    if (host_query->callback_running) {
      _tsIbDmClientHostQueryPut(host_query);
      return;
    }

    host_query->callback_running = 1;
    _tsIbDmClientHostQueryPut(host_query);
  }

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
                            TS_IB_DM_CLIENT_HOST_TIMEOUT*HZ,
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

      io_port->is_responded = 1;
      
      _tsIbDmClientHostQueryComplete(host_query, status, NULL, 0);
    }
  }
  
  host_query = _tsIbDmClientHostQueryFind(host_transaction_id);
  if (host_query) {
    host_query->callback_running = 0;

    _tsIbDmClientHostQueryPut(host_query);
  }
}

static void _tsIbHostIoIocProfileCompletion(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                            int status,
                                            tTS_IB_LID lid,
                                            tTS_IB_IOC_PROFILE ioc_profile,
                                            void* arg)
{
  tTS_IB_CLIENT_QUERY_TID tid;
  uint64_t host_transaction_id = (unsigned long) arg;
  tTS_IB_DM_CLIENT_HOST_QUERY host_query = _tsIbDmClientHostQueryFind(host_transaction_id);

  /* Do a little query house keeping */
  if (!host_query) {
      return;
  } else {

    if (host_query->callback_running) {
      _tsIbDmClientHostQueryPut(host_query);
      return;
    }

    host_query->callback_running = 1;
    _tsIbDmClientHostQueryPut(host_query);
  }
  
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
                          TS_IB_DM_CLIENT_HOST_TIMEOUT*HZ,
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

      io_port->is_responded = 1;
      _tsIbDmClientHostQueryComplete(host_query, status, NULL, 0);
    }
  }
  
  host_query = _tsIbDmClientHostQueryFind(host_transaction_id);
  if (host_query) {
    host_query->callback_running = 0;
    
    _tsIbDmClientHostQueryPut(host_query);
  }
}

static void _tsIbHostIoIouInfoCompletion(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                         int status,
                                         tTS_IB_LID lid,
                                         tTS_IB_IOU_INFO iou_info,
                                         void *arg)
{
  tTS_IB_CLIENT_QUERY_TID tid;
  uint64_t host_transaction_id = (unsigned long) arg;
  tTS_IB_DM_CLIENT_HOST_QUERY host_query = _tsIbDmClientHostQueryFind(host_transaction_id);

  /* Do a little query house keeping */
  if (!host_query) {
      return;
  } else {

    if (host_query->callback_running) {
      _tsIbDmClientHostQueryPut(host_query);
      return;
    }

    host_query->callback_running = 1;
    _tsIbDmClientHostQueryPut(host_query);
  }

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
                              TS_IB_DM_CLIENT_HOST_TIMEOUT*HZ,
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

      io_port->is_responded = 1;
      _tsIbDmClientHostQueryComplete(host_query, status, NULL, 0);
    }
  }

  host_query = _tsIbDmClientHostQueryFind(host_transaction_id);
  if (host_query) {
    host_query->callback_running = 0;

    _tsIbDmClientHostQueryPut(host_query);
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
  uint64_t host_transaction_id = (unsigned long) arg;
  tTS_IB_DM_CLIENT_HOST_QUERY host_query = _tsIbDmClientHostQueryFind(host_transaction_id);

  /* Do a little query house keeping */
  if (!host_query) {
      TS_REPORT_WARN(MOD_KERNEL_DM,"no query %d", (uint32_t)host_transaction_id );
      return;
  } else {

    if (host_query->callback_running) {
      _tsIbDmClientHostQueryPut(host_query);
      return;
    }

    host_query->callback_running = 1;
    _tsIbDmClientHostQueryPut(host_query);
  }
  
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
                                   TS_IB_DM_CLIENT_HOST_TIMEOUT*HZ,
                                   _tsIbHostIoClassPortInfoCopletion,
                                   (void *) (unsigned long) host_query->transaction_id,
                                   &tid);

        /* Query IOU info */
        tsIbIouInfoRequest(host_query->host_device,
                           host_query->host_port,
                           io_port->io_port_lid,
                           TS_IB_DM_CLIENT_HOST_TIMEOUT*HZ,
                           _tsIbHostIoIouInfoCompletion,
                           (void *) (unsigned long) host_query->transaction_id,
                           &tid);
      }
      else
      {
        TS_TRACE(MOD_KERNEL_DM, T_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
                 "Port Info complete alloc error\n", status);
        status = -ENOMEM;
      }
    }
    else
    {
      if ( host_query->type == TS_IB_DM_PORT_INFO )
      {
        TS_TRACE(MOD_KERNEL_DM, T_VERY_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
                 "Port Info complete not managed Device\n", status);
        status = -EINVAL;
      }
    }
  }
  else
  {
    TS_TRACE(MOD_KERNEL_DM, T_VERY_VERBOSE, TRACE_KERNEL_IB_DM_GEN,
             "Port Info lookup done, status %d\n", status);
  }

  /* Call completion function */
  if ( status )
    _tsIbDmClientHostQueryComplete(host_query, status, NULL, 1);
  
  host_query = _tsIbDmClientHostQueryFind(host_transaction_id);
  if (host_query) {
    host_query->callback_running = 0;
    
    _tsIbDmClientHostQueryPut(host_query);
  }

  return;
}

void tsIbHostIoQueryCancel(tTS_IB_DM_CLIENT_HOST_TID host_transaction_id)
{
  tTS_IB_DM_CLIENT_HOST_QUERY query = _tsIbDmClientHostQueryFind(host_transaction_id);

  while(query && query->callback_running) {
    _tsIbDmClientHostQueryPut(query);
    schedule();
    query = _tsIbDmClientHostQueryFind(host_transaction_id);
  }

  if (!query) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "canceling unknown TID 0x%016" TS_U64_FMT "x",
                   host_transaction_id);
    return;
  }

  _tsIbDmClientHostQueryDelete(query);

  return;
}

int tsIbHostIoQueryInit(void)
{ 
  uint64_t invalid_tid = TS_IB_DM_CLIENT_HOST_TID_INVALID & 0xFFFFFFFF;
 
  if (tsKernelHashTableCreate(TS_IB_DM_CLIENT_HOST_QUERY_HASH_BITS, &query_hash)) {
    return -ENOMEM;
  }

  get_random_bytes(&cur_transaction_id, sizeof cur_transaction_id);
  
  cur_transaction_id &= 0xFFFFFFFF;
  if (cur_transaction_id == invalid_tid) {
    cur_transaction_id = 0;
  }

  return 0;
}

void tsIbHostIoQueryCleanup(void)
{
  /* XXX cancel all queries and delete timers */

  tsKernelHashTableDestroy(query_hash);
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
  query->transaction_id = (tTS_IB_DM_CLIENT_HOST_TID)_tsIbHostIoAllocTid();

  /* start timer */
  tsKernelTimerInit(&query->timer);
  if (timeout_jiffies > 0)
  {
    query->timer.run_time = jiffies + timeout_jiffies;
    query->timer.function = _tsIbHostIoQueryTimeout;
    query->timer.arg      = (void *) (unsigned long) query->transaction_id;
    tsKernelTimerAdd(&query->timer);
  }
  
  query->entry.key = (uint32_t) (query->transaction_id & 0xffffffff);
  tsKernelLockedHashStore(query_hash, &query->entry);
  
  *transaction_id = query->transaction_id;
  
  _tsIbDmClientHostQueryPut(query);

  /* Start with port info query */
  tsIbPortInfoQuery(query->host_device,
                    query->host_port,
                    io_port_lid,
                    0,
                    TS_IB_DM_CLIENT_HOST_TIMEOUT*HZ,
                    _tsIbHostIoPortInfoCompletion,
                    (void *) (unsigned long) query->transaction_id,
                    &tid);

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
  query->transaction_id = (tTS_IB_DM_CLIENT_HOST_TID)_tsIbHostIoAllocTid();

  /* start timer */
  tsKernelTimerInit(&query->timer);
  if (timeout_jiffies > 0)
  {
    query->timer.run_time = jiffies + timeout_jiffies;
    query->timer.function = _tsIbHostIoQueryTimeout;
    query->timer.arg      = (void *) (unsigned long) query->transaction_id;
    tsKernelTimerAdd(&query->timer);
  }
  
  query->entry.key = (uint32_t) (query->transaction_id & 0xffffffff);
  tsKernelLockedHashStore(query_hash, &query->entry);
  
  *transaction_id = query->transaction_id;
  
  _tsIbDmClientHostQueryPut(query);

  /* Start with port info query */
  tsIbPortInfoTblQuery(query->host_device,
                       query->host_port,
                       TS_IB_DM_CLIENT_HOST_TIMEOUT*HZ,
                       _tsIbHostIoPortInfoCompletion,  
                       (void *) (unsigned long) query->transaction_id,
                       &tid);

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
