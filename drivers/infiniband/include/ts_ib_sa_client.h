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

  $Id: ts_ib_sa_client.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_SA_CLIENT_H
#define _TS_IB_SA_CLIENT_H

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(../sa_client,sa_client_export.ver)
#endif

#ifndef W2K_OS
#include <linux/types.h>
#endif

#include "ts_ib_sa_types.h"
#include "ts_ib_client_query.h"

/* enum definitions */

typedef enum {
  TS_IB_MULTICAST_JOIN_FULL_MEMBER          = 1,
  TS_IB_MULTICAST_JOIN_NON_MEMBER           = 2,
  TS_IB_MULTICAST_JOIN_SEND_ONLY_NON_MEMBER = 4,
} tTS_IB_MULTICAST_JOIN_STATE;

typedef enum {
  TS_IB_PATH_RECORD_FORCE_LOCAL     = 1 << 0,
  TS_IB_PATH_RECORD_FORCE_REMOTE    = 1 << 1,
  TS_IB_PATH_RECORD_ALLOW_DUPLICATE = 1 << 2
} tTS_IB_PATH_RECORD_LOOKUP_FLAGS;

typedef enum {
  TS_IB_SA_GTE = 0,
  TS_IB_SA_LTE = 1,
  TS_IB_SA_EQ  = 2
} tTS_IB_SA_SELECTOR;

typedef enum {
  TS_IB_SA_METHOD_GET                = 0x01,
  TS_IB_SA_METHOD_SET                = 0x02,
  TS_IB_SA_METHOD_GET_RESPONSE       = 0x81,
  TS_IB_SA_METHOD_INFORM             = 0x10,
  TS_IB_SA_METHOD_INFORM_RESPONSE    = 0x90,
  TS_IB_SA_METHOD_REPORT             = 0x11,
  TS_IB_SA_METHOD_REPORT_RESPONSE    = 0x91,
  TS_IB_SA_METHOD_GET_TABLE          = 0x12,
  TS_IB_SA_METHOD_GET_TABLE_RESPONSE = 0x92,
  TS_IB_SA_METHOD_GET_BULK           = 0x13,
  TS_IB_SA_METHOD_GET_BULK_RESPONSE  = 0x93,
  TS_IB_SA_METHOD_CONFIG             = 0x15,
  TS_IB_SA_METHOD_CONFIG_RESPONSE    = 0x95
} tTS_IB_SA_METHOD;

enum {
  TS_IB_SA_ATTRIBUTE_CLASS_PORTINFO    = 0x0001,
  TS_IB_SA_ATTRIBUTE_NOTICE            = 0x0002,
  TS_IB_SA_ATTRIBUTE_INFORM_INFO       = 0x0003,
  TS_IB_SA_ATTRIBUTE_NODE_RECORD       = 0x0011,
  TS_IB_SA_ATTRIBUTE_PORT_INFO_RECORD  = 0x0012,
  TS_IB_SA_ATTRIBUTE_SL2VL_RECORD      = 0x0013,
  TS_IB_SA_ATTRIBUTE_SWITCH_RECORD     = 0x0014,
  TS_IB_SA_ATTRIBUTE_LINEAR_FDB_RECORD = 0x0015,
  TS_IB_SA_ATTRIBUTE_RANDOM_FDB_RECORD = 0x0016,
  TS_IB_SA_ATTRIBUTE_MCAST_FDB_RECORD  = 0x0017,
  TS_IB_SA_ATTRIBUTE_SM_INFO_RECORD    = 0x0018,
  TS_IB_SA_ATTRIBUTE_INFORM_RECORD     = 0x00f3,
  TS_IB_SA_ATTRIBUTE_NOTICE_RECORD     = 0x00f4,
  TS_IB_SA_ATTRIBUTE_LINK_RECORD       = 0x0020,
  TS_IB_SA_ATTRIBUTE_GUID_INFO_RECORD  = 0x0030,
  TS_IB_SA_ATTRIBUTE_SERVICE_RECORD    = 0x0031,
  TS_IB_SA_ATTRIBUTE_PARTITION_RECORD  = 0x0033,
  TS_IB_SA_ATTRIBUTE_RANGE_RECORD      = 0x0034,
  TS_IB_SA_ATTRIBUTE_PATH_RECORD       = 0x0035,
  TS_IB_SA_ATTRIBUTE_VL_ARB_RECORD     = 0x0036,
  TS_IB_SA_ATTRIBUTE_MC_GROUP_RECORD   = 0x0037,
  TS_IB_SA_ATTRIBUTE_MC_MEMBER_RECORD  = 0x0038,
  TS_IB_SA_ATTRIBUTE_SA_RESPONSE       = 0x8001
};

enum {
  TS_IB_SA_TRAP_IN_SERVICE          = 64,
  TS_IB_SA_TRAP_OUT_OF_SERVICE      = 65,
  TS_IB_SA_TRAP_MCAST_GROUP_GREATE  = 66,
  TS_IB_SA_TRAP_MCAST_GROUP_DELETE  = 67
};

enum {
  TS_IB_SA_DEFAULT_PKEY             = 0xFFFF,
  TS_IB_SA_INVALID_PKEY             = 0x0
};

/* type definitions */

typedef uint32_t tTS_IB_SA_RID;

typedef struct tTS_IB_SA_PAYLOAD_STRUCT tTS_IB_SA_PAYLOAD_STRUCT,
  *tTS_IB_SA_PAYLOAD;

typedef struct tTS_IB_SA_HEADER_STRUCT tTS_IB_SA_HEADER_STRUCT,
  *tTS_IB_SA_HEADER;

typedef uint8_t tTS_IB_ATS_IP_ADDR[16];

/**
   Type for callback functions passed to tsIbPathRecordRequest().

   @param transaction_id Transaction ID of request that triggered this
   completion.
   @param status 0 on success, or error code if request failed
   (-ETIMEDOUT if request times out, -ENOENT if no cached result for a
   local-only request)
   @param path Path record on success or NULL on failure.
   @param remaining Path records remaining to be returned after this
   call.
   @param arg User-supplied argument.

   @return 0 continues request, nonzero cancels further callbacks for
   this request.
*/
typedef int (*tTS_IB_PATH_RECORD_COMPLETION_FUNC)(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                                  int status,
                                                  tTS_IB_PATH_RECORD path,
                                                  int remaining,
                                                  void *arg);

/**
   Type for callback passed to tsIbMulticastGroupJoin
*/
typedef void (*tTS_IB_MULTICAST_JOIN_COMPLETION_FUNC)(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                                      int status,
                                                      tTS_IB_MULTICAST_MEMBER member,
                                                      void *arg);

/**
   Type for callback passed to tsIbMulticastGroupTableQuery
 */
typedef void (*tTS_IB_MCAST_GROUP_QUERY_COMPLETION_FUNC)(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                                         int status,
                                                         int num_groups_remaining,
                                                         tTS_IB_MULTICAST_MEMBER member,
                                                         void *arg);

/**
   Type for callback passed to tsIbPortInfoQuery
*/
typedef void (*tTS_IB_PORT_INFO_QUERY_COMPLETION_FUNC)(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                                       int status,
                                                       tTS_IB_PORT_INFO port_info,
                                                       void *arg);

/**
   Type for callback passed to tsIbInformInforSet
*/
typedef void (*tTS_IB_INFORM_INFO_SET_COMPLETION_FUNC)(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                                       int status,
                                                       tTS_IB_COMMON_ATTRIB_INFORM inform,
                                                       void *arg);

/**
   Type for callback passed to tsIbServiceSet
*/
typedef void (*tTS_IB_SERVICE_SET_COMPLETION_FUNC)(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                                   int status,
                                                   tTS_IB_COMMON_ATTRIB_SERVICE service,
                                                   void *arg);

/**
   Type for callback passed to tsIbServiceGetGid
*/
typedef void (*tTS_IB_SERVICE_GET_GID_COMPLETION_FUNC)(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                                       int status,
                                                       tTS_IB_GID gid,
                                                       void *arg);

/**
   Type for callback passed to tsIbServiceGetIp
*/
typedef void (*tTS_IB_SERVICE_GET_IP_COMPLETION_FUNC)(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                                      int status,
                                                      tTS_IB_ATS_IP_ADDR ip_addr,
                                                      void *arg);

/**
   Type for TrapHandler
*/
typedef void (*tTS_IB_SA_NOTICE_HANDLER_FUNC)(tTS_IB_COMMON_ATTRIB_NOTICE notice,
                                              tTS_IB_PORT port,
                                              void *arg);

/**
   Type for callback passed to tsIbNodeInfoQuery
*/
typedef void (*tTS_IB_NODE_INFO_QUERY_COMPLETION_FUNC)(tTS_IB_CLIENT_QUERY_TID transaction_id,
                                                       int status,
                                                       tTS_IB_NODE_INFO node_info,
                                                       void *arg);

/* structure definitions */
struct tTS_IB_SA_HEADER_STRUCT {
  tTS_IB_MKEY   sm_key;
  uint16_t      attrib_offset;
  uint16_t      reserved;
  uint64_t      component_mask;
} __attribute__((packed));

#ifdef W2K_OS // Vipul
#pragma pack (push, 1)
#endif
struct tTS_IB_SA_PAYLOAD_STRUCT {
  uint8_t                  rmpp_version;
  uint8_t                  rmpp_type;
  uint8_t                  rmpp_time_flags;
  uint8_t                  rmpp_status;
  uint32_t                 rmpp_data1;
  uint32_t                 rmpp_data2;
  tTS_IB_SA_HEADER_STRUCT  sa_header;
  uint8_t                  admin_data[196];
} __attribute__((packed));
#ifdef W2K_OS
#pragma pack (pop)
#endif
/* function declarations */

/**
   Initiate a path record lookup.

   @param device Device to use to send query
   @param port Local port to use to send query
   @param sgid Source GID
   @param dgid Destination GID
   @param pkey P_KEY value.  If TS_IB_SA_INVALID_PKEY is used,
   pkey component mask will not be set in the query.
   @param flags Flags as in tTS_IB_PATH_RECORD_LOOKUP_FLAGS
   @param timeout_jiffies Time (in jiffies) to allow before
   considering request timed out
   @param cache_jiffies Time (in jiffies) to consider responses valid
   in cache.
   @param completion_func Function to call on completion of the lookup
   @param completion_arg Argument to supply to completion_func
   @param transaction_id Pointer used to return transaction ID

   @return error code
*/

int tsIbPathRecordRequest(
                          tTS_IB_DEVICE_HANDLE device,
                          tTS_IB_PORT port,
                          tTS_IB_GID sgid,
                          tTS_IB_GID dgid,
                          tTS_IB_PKEY pkey,
                          int flags,
                          int timeout_jiffies,
                          int cache_jiffies,
                          tTS_IB_PATH_RECORD_COMPLETION_FUNC completion_func,
                          void *completion_arg,
                          tTS_IB_CLIENT_QUERY_TID *transaction_id
                          );

/**
   Join a multicast group.

   @param device Device to use to send query
   @param port Local port to use to send query and have join group
   @param mgid GID of multicast group to join
   @param pkey Partition key for this multicast group
   @param join_state scope of membership
   @param timeout_jiffies Time (in jiffies) to allow before
   considering request timed out
   @param completion_func Function to call on completion of the lookup
   @param completion_arg Argument to supply to completion_func
   @param transaction_id Pointer used to return transaction ID

   @return error code
*/
int tsIbMulticastGroupJoin(
                           tTS_IB_DEVICE_HANDLE device,
                           tTS_IB_PORT port,
                           tTS_IB_GID mgid,
                           tTS_IB_PKEY pkey,
                           tTS_IB_MULTICAST_JOIN_STATE join_state,
                           int timeout_jiffies,
                           tTS_IB_MULTICAST_JOIN_COMPLETION_FUNC completion_func,
                           void *completion_arg,
                           tTS_IB_CLIENT_QUERY_TID *transaction_id
                           );

/**
   Leave a multicast group.
*/
int tsIbMulticastGroupLeave(
                            tTS_IB_DEVICE_HANDLE device,
                            tTS_IB_PORT port,
                            tTS_IB_GID mgid
                            );

/**
   Query multicast table given a partition
 */
int tsIbMulticastGroupTableQuery(
                                 tTS_IB_DEVICE_HANDLE device,
                                 tTS_IB_PORT port,
                                 int timeout_jiffies,
                                 tTS_IB_PKEY partition,
                                 tTS_IB_MCAST_GROUP_QUERY_COMPLETION_FUNC completion_func,
                                 void *completion_arg,
                                 tTS_IB_CLIENT_QUERY_TID *transaction_id
                                );

/**
   Query port info by specific port LID

   @param device Device to use to send query
   @param port Local port to use to send query and have join group
   @param port_lid End port LID
   @param port_num For a switch port number, for a channel adapter or
   router, reserved
   @param timeout_jiffies Time (in jiffies) to allow
   considering request timed out
   @param completion_func Function to call on completion of the lookup
   @param completion_arg Argument to supply to completion_func
   @param transaction_id Pointer used to return transaction ID

   @return error code
*/
int tsIbPortInfoQuery(
                      tTS_IB_DEVICE_HANDLE device,
                      tTS_IB_PORT port,
                      tTS_IB_LID port_lid,
                      tTS_IB_PORT port_num,
                      int timeout_jiffies,
                      tTS_IB_PORT_INFO_QUERY_COMPLETION_FUNC completion_func,
                      void *completion_arg,
                      tTS_IB_CLIENT_QUERY_TID *transaction_id
                      );

/**
   Query port info table

   @param device Device to use to send query
   @param port Local port to use to send query and have join group
   @param timeout_jiffies Time (in jiffies) to allow
   considering request timed out
   @param completion_func Function to call on completion of the lookup
   @param completion_arg Argument to supply to completion_func
   @param transaction_id Pointer used to return transaction ID

   @return error code
*/
int tsIbPortInfoTblQuery(
                         tTS_IB_DEVICE_HANDLE device,
                         tTS_IB_PORT port,
                         int timeout_jiffies,
                         tTS_IB_PORT_INFO_QUERY_COMPLETION_FUNC completion_func,
                         void *completion_arg,
                         tTS_IB_CLIENT_QUERY_TID *transaction_id
                         );

/**
   Set in service notice handler - This function first sends inform info
   to register with SA to receive in-service notice.  Then, it sets the
   handler to be called when in-service notice is received.

   @param device          Device to use to send query
   @param port            Local port to use to send query and have join group
   @param gid             Specifies specific GID to subscribe for.  Set to all
                          zeros if not desired.
   @param lid_begin       Ignored if GID is nonzero.  Specifies the lowest LID
                          in a range of LID to subscribe for.  Address 0xFFFF
                          denotes all endports mananged by the manager.
   @param lid_end         Ignored if GID is nonzero.  Specifies the highest LID
                          in a range of LID addresses to subscribe for.  Set to 0
                          if no range desired.  Ignored if subscribe_lid_begin is 0xFFFF.
   @param handler         Callback function when InService notice is received.  If
                          handler is NULL, SA is informed to unsubscribe.
   @param timeout_jiffies Time (in jiffies) to allow considering request timed out
   @param completion_func Function to call on completion of the lookup
   @param completion_arg  Argument to supply to completion_func
   @param transaction_id  Pointer used to return transaction ID

   @return error code
*/
int tsIbSetInServiceNoticeHandler(
                                  tTS_IB_DEVICE_HANDLE device,
                                  tTS_IB_PORT port,
                                  tTS_IB_GID gid,
                                  tTS_IB_LID lid_begin,
                                  tTS_IB_LID lid_end,
                                  tTS_IB_SA_NOTICE_HANDLER_FUNC handler,
                                  void *handler_arg,
                                  int timeout_jiffies,
                                  tTS_IB_INFORM_INFO_SET_COMPLETION_FUNC completion_func,
                                  void *completion_arg,
                                  tTS_IB_CLIENT_QUERY_TID *transaction_id
                                  );

/**
   Set in service notice handler - This function first sends inform info
   to register with SA to receive out-of-service notice.  Then, it sets the
   handler to be called when out-of-service notice is received.

   @param device          Device to use to send query
   @param port            Local port to use to send query and have join group
   @param gid             Specifies specific GID to subscribe for.  Set to all
                          zeros if not desired.
   @param lid_begin       Ignored if GID is nonzero.  Specifies the lowest LID
                          in a range of LID to subscribe for.  Address 0xFFFF
                          denotes all endports mananged by the manager.
   @param lid_end         Ignored if GID is nonzero.  Specifies the highest LID
                          in a range of LID addresses to subscribe for.  Set to 0
                          if no range desired.  Ignored if subscribe_lid_begin is 0xFFFF.
   @param handler         Callback function when InService notice is received.  If
                          handler is NULL, SA is informed to unsubscribe.
   @param timeout_jiffies Time (in jiffies) to allow considering request timed out
   @param completion_func Function to call on completion of the lookup
   @param completion_arg  Argument to supply to completion_func
   @param transaction_id  Pointer used to return transaction ID

   @return error code
*/
int tsIbSetOutofServiceNoticeHandler(
                                     tTS_IB_DEVICE_HANDLE device,
                                     tTS_IB_PORT port,
                                     tTS_IB_GID gid,
                                     tTS_IB_LID lid_begin,
                                     tTS_IB_LID lid_end,
                                     tTS_IB_SA_NOTICE_HANDLER_FUNC handler,
                                     void *handler_arg,
                                     int timeout_jiffies,
                                     tTS_IB_INFORM_INFO_SET_COMPLETION_FUNC completion_func,
                                     void *completion_arg,
                                     tTS_IB_CLIENT_QUERY_TID *transaction_id
                                     );

/**
  Set multicast group creation notice handler - This function first sends
  inform info to register with SA to receive multicast group creation
  notice.  Then, it sets the handler to be called when multicast
  group create notice is received.

   @param device          Device to use to send query
   @param port            Local port to use to send query and have join group
   @param gid             Specifies specific MGID to subscribe for.  Set to all
                          zeros if wild card MGID is used.
   @param handler         Callback function when Mcast group create notice is
                          received.  If handler is NULL, SA is informed to unsubscribe.
   @param timeout_jiffies Time (in jiffies) to allow considering request timed out
   @param completion_func Function to call on completion of the lookup
   @param completion_arg  Argument to supply to completion_func
   @param transaction_id  Pointer used to return transaction ID

   @return error code
 */
int tsIbSetMcastGroupCreateNoticeHandler(
                                         tTS_IB_DEVICE_HANDLE device,
                                         tTS_IB_PORT port,
                                         tTS_IB_GID gid,
                                         tTS_IB_LID lid_begin,
                                         tTS_IB_LID lid_end,
                                         tTS_IB_SA_NOTICE_HANDLER_FUNC handler,
                                         void *handler_arg,
                                         int timeout_jiffies,
                                         tTS_IB_INFORM_INFO_SET_COMPLETION_FUNC completion_func,
                                         void *completion_arg,
                                         tTS_IB_CLIENT_QUERY_TID *transaction_id
                                        );

/**
  Set multicast group creation notice handler - This function first sends
  inform info to register with SA to receive multicast group creation
  notice.  Then, it sets the handler to be called when multicast
  group create notice is received.


   @param device          Device to use to send query
   @param port            Local port to use to send query and have join group
   @param gid             Specifies specific MGID to subscribe for.  Set to all
                          zeros if wild card MGID is used.
   @param handler         Callback function when Mcast group create notice is
                          received.  If handler is NULL, SA is informed to unsubscribe.
   @param timeout_jiffies Time (in jiffies) to allow considering request timed out
   @param completion_func Function to call on completion of the lookup
   @param completion_arg  Argument to supply to completion_func
   @param transaction_id  Pointer used to return transaction ID

   @return error code
 */
int tsIbSetMcastGroupDeleteNoticeHandler(
                                         tTS_IB_DEVICE_HANDLE device,
                                         tTS_IB_PORT port,
                                         tTS_IB_GID gid,
                                         tTS_IB_LID lid_begin,
                                         tTS_IB_LID lid_end,
                                         tTS_IB_SA_NOTICE_HANDLER_FUNC handler,
                                         void *handler_arg,
                                         int timeout_jiffies,
                                         tTS_IB_INFORM_INFO_SET_COMPLETION_FUNC completion_func,
                                         void *completion_arg,
                                         tTS_IB_CLIENT_QUERY_TID *transaction_id
                                        );

int tsIbServiceSet(
                   tTS_IB_DEVICE_HANDLE	device,
                   tTS_IB_PORT		port,
                   uint64_t		service_id,
                   tTS_IB_GID		gid,
                   tTS_IB_PKEY		service_pkey,
                   uint8_t			*service_name,
                   int 			timeout_jiffies,
                   tTS_IB_SERVICE_SET_COMPLETION_FUNC
                   completion_func,
                   void *completion_arg,
                   tTS_IB_CLIENT_QUERY_TID *transaction_id
                  );

/**
  Set ATS service record - This function sends service record SET() to
  SA to register ATS service.

   @param device          Device to use to send query
   @param port            Local port to use to send query and have join group
   @param gid             Specifies specific local GID for port
   @param ip_addr         Specifies specific IP address for port
   @param timeout_jiffies Time (in jiffies) to allow considering request timed out
   @param completion_func Function to call on completion of the lookup
   @param completion_arg  Argument to supply to completion_func
   @param transaction_id  Pointer used to return transaction ID

   @return error code
 */
int tsIbAtsServiceSet(
                      tTS_IB_DEVICE_HANDLE device,
                      tTS_IB_PORT port,
                      tTS_IB_GID gid,
                      tTS_IB_ATS_IP_ADDR ip_addr,
                      int timeout_jiffies,
                      tTS_IB_SERVICE_SET_COMPLETION_FUNC completion_func,
                      void *completion_arg,
                      tTS_IB_CLIENT_QUERY_TID *transaction_id
                      );

/**
  Get ATS service GID - This function sends service record GET() to
  SA to find the GID corresponding to the specified IP address.

   @param device          Device to use to send query
   @param port            Local port to use to send query and have join group
   @param ip_addr         Specifies specific IP address for port
   @param timeout_jiffies Time (in jiffies) to allow considering request timed out
   @param completion_func Function to call on completion of the lookup
   @param completion_arg  Argument to supply to completion_func
   @param transaction_id  Pointer used to return transaction ID

   @return error code
 */
int tsIbAtsServiceGetGid(
                         tTS_IB_DEVICE_HANDLE device,
                         tTS_IB_PORT port,
                         tTS_IB_ATS_IP_ADDR ip_addr,
                         int timeout_jiffies,
                         tTS_IB_SERVICE_GET_GID_COMPLETION_FUNC completion_func,
                         void *completion_arg,
                         tTS_IB_CLIENT_QUERY_TID *transaction_id);

/**
  Get ATS service GID - This function sends service record GET() to
  SA to find the IP address  corresponding to the specified GID.

   @param device          Device to use to send query
   @param port            Local port to use to send query and have join group
   @param gid             Specifies specific local GID for port
   @param timeout_jiffies Time (in jiffies) to allow considering request timed out
   @param completion_func Function to call on completion of the lookup
   @param completion_arg  Argument to supply to completion_func
   @param transaction_id  Pointer used to return transaction ID

   @return error code
 */
int tsIbAtsServiceGetIp(
                        tTS_IB_DEVICE_HANDLE device,
                        tTS_IB_PORT port,
                        tTS_IB_GID gid,
                        int timeout_jiffies,
                        tTS_IB_SERVICE_GET_IP_COMPLETION_FUNC completion_func,
                        void *completion_arg,
                        tTS_IB_CLIENT_QUERY_TID *transaction_id);

/**
   Query node info by specific port LID

   @param device Device to use to send query
   @param port Local port to use to send query and have join group
   @param port_lid End port LID
   @param timeout_jiffies Time (in jiffies) to allow
   considering request timed out
   @param completion_func Function to call on completion of the lookup
   @param completion_arg Argument to supply to completion_func
   @param transaction_id Pointer used to return transaction ID

   @return error code
*/
int tsIbNodeInfoQuery(
                      tTS_IB_DEVICE_HANDLE device,
                      tTS_IB_PORT port,
                      tTS_IB_LID port_lid,
                      int timeout_jiffies,
                      tTS_IB_NODE_INFO_QUERY_COMPLETION_FUNC completion_func,
                      void *completion_arg,
                      tTS_IB_CLIENT_QUERY_TID *transaction_id
                      );

#endif /* _TS_IB_SA_CLIENT_H */
