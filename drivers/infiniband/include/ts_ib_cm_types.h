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

  $Id: ts_ib_cm_types.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_CM_TYPES_H
#define _TS_IB_CM_TYPES_H

#if defined(__KERNEL__)
#  ifndef W2K_OS
#    include <linux/types.h>
#  endif
#else
#  ifndef W2K_OS
#    include <stdint.h>
#  endif
#  include <stddef.h>             /* for size_t */
#endif

#ifdef W2K_OS
#include <all/common/include/w2k.h>
#endif

/* Visual C++ apparently can't handle empty structs. */
#if !defined(EMPTY_STRUCT)
#  ifdef W2K_OS
#    define EMPTY_STRUCT void *reserved;
#  else
#    define EMPTY_STRUCT
#  endif
#endif

#include "ts_ib_core_types.h"

typedef uint32_t tTS_IB_CM_COMM_ID;
typedef uint64_t tTS_IB_SERVICE_ID;
typedef void    *tTS_IB_LISTEN_HANDLE;

#define TS_IB_CM_COMM_ID_INVALID 0
#define TS_IB_CM_SERVICE_EXACT_MASK 0xffffffffffffffffULL

typedef enum {
  TS_IB_CM_CALLBACK_PROCEED,
  TS_IB_CM_CALLBACK_DEFER,
  TS_IB_CM_CALLBACK_ABORT
} tTS_IB_CM_CALLBACK_RETURN;

typedef enum {
  TS_IB_CM_REQ_RECEIVED,
  TS_IB_CM_REP_RECEIVED,
  TS_IB_CM_LAP_RECEIVED,
  TS_IB_CM_APR_RECEIVED,
  TS_IB_CM_ESTABLISHED,
  TS_IB_CM_DISCONNECTED,
  TS_IB_CM_IDLE
} tTS_IB_CM_EVENT;

typedef enum {
  TS_IB_REJ_NO_QP                            = 1,
  TS_IB_REJ_NO_EEC                           = 2,
  TS_IB_REJ_NO_RESOURCES                     = 3,
  TS_IB_REJ_TIMEOUT                          = 4,
  TS_IB_REJ_UNSUPPORTED                      = 5,
  TS_IB_REJ_INVALID_COMM_ID                  = 6,
  TS_IB_REJ_INVALID_COMM_INSTANCE            = 7,
  TS_IB_REJ_INVALID_SERVICE_ID               = 8,
  TS_IB_REJ_INVALID_TRANSPORT_TYPE           = 9,
  TS_IB_REJ_STALE_CONNECTION                 = 10,
  TS_IB_REJ_NO_RDC                           = 11,
  TS_IB_REJ_PRIMARY_GID_REJECTED             = 12,
  TS_IB_REJ_PRIMARY_LID_REJECTED             = 13,
  TS_IB_REJ_INVALID_PRIMARY_SL               = 14,
  TS_IB_REJ_INVALID_PRIMARY_TRAFFIC_CLASS    = 15,
  TS_IB_REJ_INVALID_PRIMARY_HOP_LIMIT        = 16,
  TS_IB_REJ_INVALID_PRIMARY_PACKET_RATE      = 17,
  TS_IB_REJ_ALTERNATE_GID_REJECTED           = 18,
  TS_IB_REJ_ALTERNATE_LID_REJECTED           = 19,
  TS_IB_REJ_INVALID_ALTERNATE_SL             = 20,
  TS_IB_REJ_INVALID_ALTERNATE_TRAFFIC_CLASS  = 21,
  TS_IB_REJ_INVALID_ALTERNATE_HOP_LIMIT      = 22,
  TS_IB_REJ_INVALID_ALTERNATE_PACKET_RATE    = 23,
  TS_IB_REJ_PORT_CM_REDIRECT                 = 24,
  TS_IB_REJ_PORT_REDIRECT                    = 25,
  TS_IB_REJ_INVALID_PATH_MTU                 = 26,
  TS_IB_REJ_INSUFFICIENT_RESPONDER_RESOURCES = 27,
  TS_IB_REJ_CONSUMER_REJECT                  = 28,
  TS_IB_REJ_RNR_REPLY_COUNT_REJECTED         = 29,
} tTS_IB_CM_REJ_REASON;

typedef enum {
  TS_IB_APR_PATH_LOADED         =  0,
  TS_IB_APR_INVALID_LID         =  1,
  TS_IB_APR_APM_NOT_SUPPORTED   =  2,
  TS_IB_APR_PATH_REJECTED       =  3,
  TS_IB_APR_REDIRECT            =  4,
  TS_IB_APR_PATH_IS_PRIMARY     =  5,
  TS_IB_APR_QPN_MISMATCH        =  6,
  TS_IB_APR_LID_REJECTED        =  7,
  TS_IB_APR_GID_REJECTED        =  8,
  TS_IB_APR_FLOWLABEL_REJECTED  =  9,
  TS_IB_APR_TCLASS_REJECTED     = 10,
  TS_IB_APR_HOPLIMIT_REJECTED   = 11,
  TS_IB_APR_STATICRATE_REJECTED = 12,
  TS_IB_APR_SL_REJECTED         = 13
} tTS_IB_CM_APR_STATUS;

typedef enum {
  TS_IB_CM_DISCONNECTED_REMOTE_TIMEOUT,
  TS_IB_CM_DISCONNECTED_REMOTE_CLOSE,
  TS_IB_CM_DISCONNECTED_LOCAL_CLOSE,
  TS_IB_CM_DISCONNECTED_STALE_CONNECTION
} tTS_IB_CM_DISCONNECTED_REASON;

typedef enum {
  TS_IB_CM_IDLE_LOCAL_REJECT,
  TS_IB_CM_IDLE_REMOTE_REJECT,
  TS_IB_CM_IDLE_REMOTE_TIMEOUT,
  TS_IB_CM_IDLE_TIME_WAIT_DONE
} tTS_IB_CM_IDLE_REASON;

typedef tTS_IB_CM_CALLBACK_RETURN (*tTS_IB_CM_CALLBACK_FUNCTION)(tTS_IB_CM_EVENT   event,
                                                                 tTS_IB_CM_COMM_ID comm_id,
                                                                 void             *param,
                                                                 void             *arg);

typedef struct ib_cm_active_param tTS_IB_CM_ACTIVE_PARAM_STRUCT,
  *tTS_IB_CM_ACTIVE_PARAM;
typedef struct ib_cm_passive_param tTS_IB_CM_PASSIVE_PARAM_STRUCT,
  *tTS_IB_CM_PASSIVE_PARAM;
typedef struct ib_cm_req_received_param tTS_IB_CM_REQ_RECEIVED_PARAM_STRUCT,
  *tTS_IB_CM_REQ_RECEIVED_PARAM;
typedef struct ib_cm_rep_received_param tTS_IB_CM_REP_RECEIVED_PARAM_STRUCT,
  *tTS_IB_CM_REP_RECEIVED_PARAM;
typedef struct ib_cm_lap_received_param tTS_IB_CM_LAP_RECEIVED_PARAM_STRUCT,
  *tTS_IB_CM_LAP_RECEIVED_PARAM;
typedef struct ib_cm_arp_received_param tTS_IB_CM_APR_RECEIVED_PARAM_STRUCT,
  *tTS_IB_CM_APR_RECEIVED_PARAM;
typedef struct ib_cm_established_param tTS_IB_CM_ESTABLISHED_PARAM_STRUCT,
  *tTS_IB_CM_ESTABLISHED_PARAM;
typedef struct ib_cm_disconnected_param tTS_IB_CM_DISCONNECTED_PARAM_STRUCT,
  *tTS_IB_CM_DISCONNECTED_PARAM;
typedef struct ib_cm_idle_param tTS_IB_CM_IDLE_PARAM_STRUCT,
  *tTS_IB_CM_IDLE_PARAM;

struct ib_cm_active_param {
  tTS_IB_QP_HANDLE qp;
  void            *req_private_data;
  int              req_private_data_len;
  uint8_t          responder_resources;
  uint8_t          initiator_depth;
  uint8_t          retry_count;
  uint8_t          rnr_retry_count;
  uint8_t          cm_response_timeout;
  uint8_t          max_cm_retries;
  int              flow_control:1;
};

struct ib_cm_passive_param {
  tTS_IB_QP_HANDLE qp;
  void *           reply_private_data;
  int              reply_private_data_len;
  uint8_t          responder_resources;
  uint8_t          initiator_depth;
  uint8_t          rnr_retry_count;
  int              flow_control:1;
  int              failover_accepted:1;
};

struct ib_cm_req_received_param {
  struct ib_cm_passive_param accept_param;
  tTS_IB_LISTEN_HANDLE       listen_handle;
  tTS_IB_SERVICE_ID          service_id;
  tTS_IB_QPN                 local_qpn;
  tTS_IB_QPN                 remote_qpn;
  tTS_IB_GUID                remote_guid;
  tTS_IB_GID                 dgid;
  tTS_IB_GID                 sgid;
  tTS_IB_LID                 dlid;
  tTS_IB_LID                 slid;
  tTS_IB_DEVICE_HANDLE       device;
  tTS_IB_PORT                port;
  void *                     remote_private_data;
  int                        remote_private_data_len;
};

struct ib_cm_rep_received_param {
  tTS_IB_QPN local_qpn;
  tTS_IB_QPN remote_qpn;
  void *     remote_private_data;
  int        remote_private_data_len;
  void *     reply_private_data;
  int        reply_private_data_len;
};

struct ib_cm_lap_received_param {
  struct ib_path_record alternate_path;
  void                 *remote_private_data;
  int                   remote_private_data_len;
  void                 *reply_private_data;
  int                   reply_private_data_len;
};

struct ib_cm_apr_received_param {
  uint8_t ap_status;
  uint8_t apr_info_len;
  void   *apr_info;
};

struct ib_cm_established_param {
  EMPTY_STRUCT
};

struct ib_cm_disconnected_param {
  tTS_IB_CM_DISCONNECTED_REASON reason;
};

struct ib_cm_idle_param {
  tTS_IB_CM_IDLE_REASON reason;
  tTS_IB_CM_REJ_REASON  rej_reason;
  int                   rej_info_len;
  void                 *rej_info;
};

#endif /* _TS_IB_CM_TYPES_H */

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
