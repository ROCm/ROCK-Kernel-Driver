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

  $Id: ts_ib_dm_client_host.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_DM_CLIENT_HOST_H
#define _TS_IB_DM_CLIENT_HOST_H

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(../dm_client,dm_client_export.ver)
#endif

#ifndef W2K_OS
#include <linux/types.h>
#include <linux/list.h>
#else
#include <os_dep/win/linux/list.h>
#endif

#include "ts_ib_core_types.h"
#include "ts_ib_dm_client.h"

/* enum definitions */

typedef uint32_t tTS_IB_DM_CLIENT_HOST_TID;
#define TS_IB_DM_CLIENT_HOST_TID_INVALID  0xffffffff

typedef struct tTS_IB_DM_CLIENT_HOST_IO_SVC_STRUCT tTS_IB_DM_CLIENT_HOST_IO_SVC_STRUCT,
  *tTS_IB_DM_CLIENT_HOST_IO_SVC;
typedef struct tTS_IB_DM_CLIENT_HOST_IO_LIST_STRUCT tTS_IB_DM_CLIENT_HOST_IO_LIST_STRUCT,
  *tTS_IB_DM_CLIENT_HOST_IO_LIST;

struct tTS_IB_DM_CLIENT_HOST_IO_SVC_STRUCT
{
  tTS_IB_LID                port_lid;
  tTS_IB_GUID               controller_guid;
  tTS_IB_SVC_ENTRY_STRUCT   svc_entry;

  struct list_head          list;
};

struct tTS_IB_DM_CLIENT_HOST_IO_LIST_STRUCT
{
  struct list_head                      io_svc_list;
};

/**
   Type for callback passed to tsIbIocProfileRequest()

   @param status - status of the query response (0, -ENOMEM, -ETIMEOUT, -EINVAL)
   @param io_list - list contains all the io_svc assigned to the host_port. It's
   NULL if status is not 0.
   @param arg - completion argument given in tsIbHostIoQuery()

   NOTE: io_list memory is a variable list of io_svc entries.  The memory
   needs to be freed using tsIbHostIoListFree() after the called is done
   with io_list.
*/
typedef void (*tTS_IB_DM_CLIENT_HOST_COMPLETION_FUNC)(int status,
                                                      tTS_IB_DM_CLIENT_HOST_IO_LIST io_list,
                                                      void* arg);


/* function declarations */

/**
   Cancel callbacks for a pending host IO query
   @param transaction_id Transaction ID of request to cancel
*/

void tsIbHostIoQueryCancel(tTS_IB_DM_CLIENT_HOST_TID transaction_id);

/**
   Initialize host IO query data structure
*/
int tsIbHostIoQueryInit(void);

/**
   Clean up internal resouce for host IO
*/
void tsIbHostIoQueryCleanup(void);

/**
   Initiate a host IO query to a specific IO based on io_port_lid

   @param device Device on the host machine that is used.
   @param host_port - port on the host machine that is used.
   @param io_port_lid - destination IO port LID.
   @param timeout_jiffies - Time (in jiffies) to allow before
   considering request time out.
   @param completion_function - Function to call on completion of
   the lookup.
   @param completion_arg - Argument to supply to completion_function
   @param transaction_id - Pointer used to return transaction_id which
   can be used in tsIbDmClientHostQueryCancel()
*/
int tsIbHostIoPortQuery(
                        tTS_IB_DEVICE_HANDLE device,
                        tTS_IB_PORT host_port,
                        tTS_IB_LID io_port_lid,
                        int timeout_jiffies,
                        tTS_IB_DM_CLIENT_HOST_COMPLETION_FUNC completion_function,
                        void *completion_arg,
                        tTS_IB_DM_CLIENT_HOST_TID *transaction_id
                        );

/**
   Initiate a host IO query.

   @param device Device on the host machine that is used.
   @param host_port - port on the host machine that is used.
   @param timeout_jiffies - Time (in jiffies) to allow before
   considering request time out.
   @param completion_function - Function to call on completion of
   the lookup.
   @param completion_arg - Argument to supply to completion_function
   @param transaction_id - Pointer used to return transaction_id which
   can be used in tsIbDmClientHostQueryCancel()
*/
int tsIbHostIoQuery(
                    tTS_IB_DEVICE_HANDLE device,
                    tTS_IB_PORT host_port,
                    int timeout_jiffies,
                    tTS_IB_DM_CLIENT_HOST_COMPLETION_FUNC completion_function,
                    void *completion_arg,
                    tTS_IB_DM_CLIENT_HOST_TID *transaction_id
                    );

/**
   Free the io_list that is returned to the called in the completion_function

   io_list - io_list argument in  tTS_IB_DM_CLIENT_HOST_COMPLETION_FUNC.
*/
int tsIbHostIoListFree(tTS_IB_DM_CLIENT_HOST_IO_LIST io_list);

#endif /* _TS_IB_DM_CLIENT_HOST_H */
