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

  $Id: ts_ib_client_query.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_CLIENT_QUERY_H
#define _TS_IB_CLIENT_QUERY_H

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(../client_query,client_query_export.ver)
#endif

#ifndef W2K_OS
#include <linux/types.h>
#include <linux/list.h>
#else
#include <os_dep/win/linux/list.h>
#endif

#include "ts_ib_core_types.h"
#include "ts_ib_mad_types.h"
#include "ts_ib_client_query_types.h"

/* #define */
#define TS_IB_CLIENT_QUERY_TID_INVALID  0xffffffff

/* enum definitions */
typedef enum {
  TS_IB_CLIENT_RESPONSE_OK,
  TS_IB_CLIENT_RESPONSE_ERROR,
  TS_IB_CLIENT_RESPONSE_TIMEOUT,
  TS_IB_CLIENT_RESPONSE_CANCEL
} tTS_IB_CLIENT_RESPONSE_STATUS;

/* typedef */
typedef uint64_t tTS_IB_CLIENT_QUERY_TID;

/**
  Type for callback function passed to tsIbClientQuery().

  @param status   Indicates success or failure as defined
  in tTS_IB_CLIENT_RESPONSE_STATUS of query.
  @param Packet   returned from query.
  @param arg      User-supplied argument.
*/
typedef void (*tTS_IB_CLIENT_RESPONSE_FUNCTION)(tTS_IB_CLIENT_RESPONSE_STATUS status,
                                                tTS_IB_MAD packet,
                                                void *arg);

/**
  Type for callback function passed to tsIbClientQuery().

  @param status    Indicates success or failure as defined
                   in tTS_IB_CLIENT_RESPONSE_STATUS of query.
  @param header    From query response.  Contains specific mgmt class header.
                   For example, SA header contains attrib_offset which is important
                   to otain attribute info from data.
  @param data      From query response
  @param data_size From query response in bytes
  @param arg       User-supplied argument.
*/
typedef void (*tTS_IB_CLIENT_RMPP_RESPONSE_FUNCTION)(tTS_IB_CLIENT_RESPONSE_STATUS status,
                                                     uint8_t *header,
                                                     uint8_t *data,
                                                     uint32_t data_size,
                                                     void *arg);

/**
  Function to perform the client query

  @param packet            MAD packet to be sent.
  @param timeout_jiffies   timeout value for query.
  @param function          callback function when query complete,
                           or timeout.
  @param arg               user supplied argument.
*/
int tsIbClientQuery(
                    tTS_IB_MAD packet,
                    int timeout_jiffies,
                    tTS_IB_CLIENT_RESPONSE_FUNCTION function,
                    void *arg
                    );

/**
  Function to perform the client query with RMPP

  @param packet            MAD packet to be sent.
  @param timeout_jiffies   timeout value for query.
  @param header_length     size of the header specific for this mgmt
                           class.  For example, SA header is 20 bytes.
  @param function          callback function when query complete,
                           or timeout.
  @param arg               user supplied argument.
*/
int tsIbRmppClientQuery(
                        tTS_IB_MAD packet,
                        int timeout_jiffies,
                        int header_length,
                        tTS_IB_CLIENT_RMPP_RESPONSE_FUNCTION function,
                        void *arg
                        );

/**
  Function to free the allocated memory.  This function should
  be called to free the memory that is passed to the user in
  tTS_IB_CLIENT_RMPP_RESPONSE_FUNCTION()
*/
int tsIbRmppClientFree(
                       uint8_t *data
                       );

/**
  Function to cancel query

  @param trasaction_id   transaction_id of the query
*/
int tsIbClientQueryCancel(
                          tTS_IB_CLIENT_QUERY_TID transaction_id
                          );

/**
  Function to be called to allocate transaction_id.

  @return transaction_id
*/
tTS_IB_CLIENT_QUERY_TID tsIbClientAllocTid(
                                           void
                                           );

/**
  Register a management class to be handled by the client query code.

  @mgmt_class   Packets whose management class matches this
                will be passed to the registered handler.
  @function     Callback function for matching MADs async MADs
                (notice, trap, trap repress).  If this is NULL, any
                handler for the given management class will be
                deregistered.
  @arg          Opaque data that will be passed to callback
                function.
*/
int tsIbClientMadHandlerRegister(
                                 uint8_t mgmt_class,
                                 tTS_IB_MAD_DISPATCH_FUNCTION function,
                                 void *arg
                                 );

#endif /* _TS_IB_CLIENT_QUERY_H */
