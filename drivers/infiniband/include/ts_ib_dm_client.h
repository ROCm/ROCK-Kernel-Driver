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

  $Id: ts_ib_dm_client.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_DM_CLIENT_H
#define _TS_IB_DM_CLIENT_H

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(../dm_client,dm_client_export.ver)
#endif

#ifndef W2K_OS
#include <linux/types.h>
#endif

#include "ts_ib_core_types.h"
#include "ts_ib_client_query.h"

/* enum definitions */

typedef enum {
  TS_IB_DM_METHOD_GET                = 0x01,
  TS_IB_DM_METHOD_SET                = 0x02,
  TS_IB_DM_METHOD_GET_RESPONSE       = 0x81,
  TS_IB_DM_METHOD_TRAP               = 0x05,
  TS_IB_DM_METHOD_TRAP_REPRESS       = 0x07,
} tTS_IB_DM_METHOD;

enum {
  TS_IB_DM_ATTRIBUTE_CLASS_PORTINFO    = 0x0001,
  TS_IB_DM_ATTRIBUTE_NOTICE            = 0x0002,
  TS_IB_DM_ATTRIBUTE_IOU_INFO          = 0x0010,
  TS_IB_DM_ATTRIBUTE_IOC_PROFILE       = 0x0011,
  TS_IB_DM_ATTRIBUTE_SVC_ENTRIES       = 0x0012,
  TS_IB_DM_ATTRIBUTE_DIAG_TIMEOUT      = 0x0020,
  TS_IB_DM_ATTRIBUTE_PREPARE_TO_TEST   = 0x0021,
  TS_IB_DM_ATTRIBUTE_TEST_DEV_ONCE     = 0x0022,
  TS_IB_DM_ATTRIBUTE_TEST_DEV_LOOP     = 0x0023,
  TS_IB_DM_ATTRIBUTE_DIAG_CODE         = 0x0024,
};

enum {
  TS_IB_IOU_INFO_MAX_NUM_CONTROLLERS_IN_BYTE  = 128
};

enum {
  TS_IB_SVC_ENTRY_NAME_SIZE = 40,
  TS_IB_SVC_ENTRY_ID_SIZE = 8
};

enum {
  TS_IB_SVC_ENTRIES_NUM_SVC_ENTRY = 4
};

enum {
  TS_IB_DM_NOTICE_READY_TO_TEST_TRAP_NUM = 514
};

/* type definitions */

typedef uint32_t tTS_IB_CONTROLLER_ID;

typedef uint32_t tTS_IB_SVC_ENTRY_ID;

typedef struct tTS_IB_IOU_INFO_STRUCT tTS_IB_IOU_INFO_STRUCT,
  *tTS_IB_IOU_INFO;
typedef struct tTS_IB_IOC_PROFILE_STRUCT tTS_IB_IOC_PROFILE_STRUCT,
  *tTS_IB_IOC_PROFILE;
typedef struct tTS_IB_SVC_ENTRY_STRUCT tTS_IB_SVC_ENTRY_STRUCT,
  *tTS_IB_SVC_ENTRY;
typedef struct tTS_IB_SVC_ENTRIES_STRUCT tTS_IB_SVC_ENTRIES_STRUCT,
  *tTS_IB_SVC_ENTRIES;

struct tTS_IB_IOU_INFO_STRUCT {
  tTS_IB_LID  lid;
  uint16_t    change_id;
  uint8_t     max_controllers;
  uint8_t     op_rom;
  uint8_t     controller_list[TS_IB_IOU_INFO_MAX_NUM_CONTROLLERS_IN_BYTE];
};

struct tTS_IB_IOC_PROFILE_STRUCT {
  tTS_IB_CONTROLLER_ID    controller_id;
  tTS_IB_GUID             guid;
  uint32_t                vendor_id;
  uint32_t                device_id;
  uint16_t                device_version;
  uint32_t                subsystem_vendor_id;
  uint32_t                subsystem_id;
  uint16_t                io_class;
  uint16_t                io_subclass;
  uint16_t                protocol;
  uint16_t                protocol_version;
  uint16_t                service_conn;
  uint16_t                initiators_supported;
  uint16_t                send_msg_depth;
  uint16_t                rdma_read_depth;
  uint32_t                send_msg_size;
  uint32_t                rdma_xfer_size;
  uint8_t                 op_capability_mask;
  uint8_t                 svc_capability_mask;
  uint8_t                 num_svc_entries;
  uint8_t                 id_string[64];
};

struct tTS_IB_SVC_ENTRY_STRUCT
{
  uint8_t   service_name[TS_IB_SVC_ENTRY_NAME_SIZE];
  uint8_t   service_id[TS_IB_SVC_ENTRY_ID_SIZE];
};

struct tTS_IB_SVC_ENTRIES_STRUCT
{
  tTS_IB_CONTROLLER_ID            controller_id;
  tTS_IB_SVC_ENTRY_ID             begin_svc_entry_id;
  tTS_IB_SVC_ENTRY_ID             end_svc_entry_id;
  tTS_IB_SVC_ENTRY_STRUCT         service_entries[TS_IB_SVC_ENTRIES_NUM_SVC_ENTRY];
};

/**
   Type for callback functions passed to tsIbIouInfoRequest().

   @param transaction_id Transaction ID of request that triggered this
   completion.
   @param status 0 on success, or error code if request failed
   (-ETIMEDOUT if request times out)
   @param lid lid of the io port.
   @param class_port_info on success or NULL on failure.
   @param arg User-supplied argument.

   @return 0 continues request, nonzero cancels further callbacks for
   this request.
*/
typedef void (*tTS_IB_DM_CLASS_PORT_INFO_COMPLETION_FUNC)(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                                          int status,
                                                          tTS_IB_LID lid,
                                                          tTS_IB_COMMON_ATTRIB_CPI class_port_info,
                                                          void *arg);

/**
   Type for callback functions passed to tsIbIouInfoRequest().

   @param transaction_id Transaction ID of request that triggered this
   completion.
   @param status 0 on success, or error code if request failed
   (-ETIMEDOUT if request times out)
   @param lid lid of the io port.
   @param iou info on success or NULL on failure.
   @param arg User-supplied argument.

   @return 0 continues request, nonzero cancels further callbacks for
   this request.
*/
typedef void (*tTS_IB_IOU_INFO_COMPLETION_FUNC)(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                                int status,
                                                tTS_IB_LID lid,
                                                tTS_IB_IOU_INFO iou_info,
                                                void *arg);

/**
   Type for callback passed to tsIbIocProfileRequest()
*/
typedef void (*tTS_IB_IOC_PROFILE_COMPLETION_FUNC)(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                                   int status,
                                                   tTS_IB_LID lid,
                                                   tTS_IB_IOC_PROFILE ioc_profile,
                                                   void *arg);

/**
   Type for callback passed to tsIbSvcEntriesRequest()
*/
typedef void (*tTS_IB_SVC_ENTRIES_COMPLETION_FUNC)(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                                   int status,
                                                   tTS_IB_LID lid,
                                                   tTS_IB_SVC_ENTRIES svc_entries,
                                                   void *arg);

/**
   Type for callback passed to tsIbDmAsynNotify()
*/
typedef void (*tTS_IB_DM_NOTICE_HANDLER_FUNC)(tTS_IB_COMMON_ATTRIB_NOTICE notice,
                                              tTS_IB_DEVICE_HANDLE dev,
                                              tTS_IB_PORT port,
                                              tTS_IB_LID trap_lid,
                                              void *arg);

/* function declarations */

/**
   Cancel callbacks for a pending DM query (IOU query, IOC profile,...).

   @param transaction_id Transaction ID of request to cancel
*/

void tsIbDmClientQueryCancel(
                             tTS_IB_CLIENT_QUERY_TID transaction_id
                             );

/**
   Initiate a Device Management Class Port Info Query.

   @param device Device to use to send query
   @param port Local port to use to send query
   @param dst_port_lid LID of the port where IOU resides
   @param timeout_jiffies Time (in jiffies) to allow before
   considering request timed out
   @param completion_func Function to call on completion of the lookup
   @param completion_arg Argument to supply to completion_func
   @param transaction_id Pointer used to return transaction ID

   @return error code
*/

int tsIbDmClassPortInfoRequest(
                               tTS_IB_DEVICE_HANDLE device,
                               tTS_IB_PORT port,
                               tTS_IB_LID dst_port_lid,
                               int timeout_jiffies,
                               tTS_IB_DM_CLASS_PORT_INFO_COMPLETION_FUNC completion_func,
                               void *completion_arg,
                               tTS_IB_CLIENT_QUERY_TID *transaction_id
                               );

/**
   Register for a asynchronous callback when a DM trap is received.

   @param tTS_IB_DM_ASYNC_NOTIFY_FUNC callback function
   @param trap_num trap_num
*/
int tsIbDmAsyncNotifyRegister(
                              uint16_t trap_num,
                              tTS_IB_DM_NOTICE_HANDLER_FUNC notify_func,
                              void * notify_arg
                              );

/**
   Initiate a IOU INFO lookup.

   @param device Device to use to send query
   @param port Local port to use to send query
   @param dst_port_lid LID of the port where IOU resides
   @param timeout_jiffies Time (in jiffies) to allow before
   considering request timed out
   @param completion_func Function to call on completion of the lookup
   @param completion_arg Argument to supply to completion_func
   @param transaction_id Pointer used to return transaction ID

   @return error code
*/

int tsIbIouInfoRequest(
                       tTS_IB_DEVICE_HANDLE device,
                       tTS_IB_PORT port,
                       tTS_IB_LID dst_port_lid,
                       int timeout_jiffies,
                       tTS_IB_IOU_INFO_COMPLETION_FUNC completion_func,
                       void *completion_arg,
                       tTS_IB_CLIENT_QUERY_TID *transaction_id
                       );

int tsIbIsIocPresent(
                     tTS_IB_IOU_INFO iou_info,
                     tTS_IB_CONTROLLER_ID controller_id
                     );

/**
   Initiate a IOC Profile lookup.

   @param device Device to use to send query
   @param port Local port to use to send query
   @param dst_port_lid LID of the port where IOC resides
   @param controller_id ID of the controller
   @param timeout_jiffies Time (in jiffies) to allow before
   considering request timed out
   @param completion_func Function to call on completion of the lookup
   @param completion_arg Argument to supply to completion_func
   @param transaction_id Pointer used to return transaction ID

   @return error code
*/

int tsIbIocProfileRequest(
                          tTS_IB_DEVICE_HANDLE device,
                          tTS_IB_PORT port,
                          tTS_IB_LID dst_port_lid,
                          tTS_IB_CONTROLLER_ID controller_id,
                          int timeout_jiffies,
                          tTS_IB_IOC_PROFILE_COMPLETION_FUNC completion_func,
                          void *completion_arg,
                          tTS_IB_CLIENT_QUERY_TID *transaction_id
                          );

/**
   Initiate a Svc Entries lookup.

   @param device Device to use to send query
   @param port Local port to use to send query
   @param dst_port_lid LID of the port where IOC resides
   @param controller_id ID of the controller
   @param begin_svc_id ID of the beginning of the svc entry query
   @param end_svc_id ID of the end of the svc entry query
   (< begin_svc_id + 4)
   @param timeout_jiffies Time (in jiffies) to allow before
   considering request timed out
   @param completion_func Function to call on completion of the lookup
   @param completion_arg Argument to supply to completion_func
   @param transaction_id Pointer used to return transaction ID

   @return error code
*/

int tsIbSvcEntriesRequest(
                          tTS_IB_DEVICE_HANDLE device,
                          tTS_IB_PORT port,
                          tTS_IB_LID dst_port_lid,
                          tTS_IB_CONTROLLER_ID controller_id,
                          tTS_IB_SVC_ENTRY_ID begin_svc_id,
                          tTS_IB_SVC_ENTRY_ID end_svc_id,
                          int timeout_jiffies,
                          tTS_IB_SVC_ENTRIES_COMPLETION_FUNC completion_func,
                          void *completion_arg,
                          tTS_IB_CLIENT_QUERY_TID *transaction_id
                          );

#endif /* _TS_IB_DM_CLIENT_H */
