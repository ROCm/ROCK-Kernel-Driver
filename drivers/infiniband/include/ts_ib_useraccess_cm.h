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

  $Id: ts_ib_useraccess_cm.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_USERACCESS_CM_H
#define _TS_IB_USERACCESS_CM_H

#include "ts_ib_useraccess.h"
#include "ts_ib_cm.h"
#include "ts_ib_cm_user.h"

struct tTS_IB_QP_REGISTER_IOCTL_STRUCT {
  VAPI_k_qp_hndl_t      vk_qp_handle;
  tTS_IB_QP_STATE       qp_state;
  tTS_IB_QPN            qpn;
  tTS_IB_TRANSPORT      transport;
  tTS_IB_QP_HANDLE      qp_handle;		/* OUT */
};

struct tTS_IB_CM_CONNECT_IOCTL {
  unsigned long                cm_arg;
  uint8_t                      responder_resources;
  uint8_t                      initiator_depth;
  uint8_t                      retry_count;
  uint8_t                      rnr_retry_count;
  uint8_t                      cm_response_timeout;
  uint8_t                      max_cm_retries;
  tTS_IB_QP_HANDLE             qp_handle;
  tTS_IB_U_K_QP_HANDLE         k_qp_handle;
  tTS_IB_PATH_RECORD           primary_path;
  tTS_IB_PATH_RECORD           alternate_path;
  tTS_IB_SERVICE_ID            service_id;
  void                        *req_private_data;
  int                          req_private_data_len;
  tTS_IB_CM_COMM_ID            comm_id;			/* OUT */
  tTS_IB_QP_STATE              qp_state;		/* OUT */
};

typedef struct tTS_IB_CM_CONNECT_IOCTL tTS_IB_CM_CONNECT_IOCTL_STRUCT,
 *tTS_IB_CM_CONNECT_IOCTL;

struct tTS_IB_CM_LISTEN_IOCTL {
  unsigned long         cm_arg;
  tTS_IB_SERVICE_ID     service_id;
  tTS_IB_SERVICE_ID     service_mask;
  tTS_IB_LISTEN_HANDLE  listen_handle;
};

typedef struct tTS_IB_CM_LISTEN_IOCTL tTS_IB_CM_LISTEN_IOCTL_STRUCT,
 *tTS_IB_CM_LISTEN_IOCTL;

struct tTS_IB_CM_ALTERNATE_PATH_LOAD_IOCTL {
  tTS_IB_CM_COMM_ID     comm_id;
  tTS_IB_PATH_RECORD    alternate_path;
};

typedef struct tTS_IB_CM_ALTERNATE_PATH_LOAD_IOCTL tTS_IB_CM_ALTERNATE_PATH_LOAD_IOCTL_STRUCT,
 *tTS_IB_CM_ALTERNATE_PATH_LOAD_IOCTL;

struct tTS_IB_CM_ACCEPT_IOCTL {
  unsigned long         cm_arg;
  tTS_IB_CM_COMM_ID     comm_id;
  tTS_IB_QP_HANDLE      qp_handle;
  tTS_IB_U_K_QP_HANDLE  k_qp_handle;
  void                 *reply_data;
  int                   reply_size;
  tTS_IB_QP_STATE       qp_state;			/* OUT */
};

typedef struct tTS_IB_CM_ACCEPT_IOCTL tTS_IB_CM_ACCEPT_IOCTL_STRUCT,
 *tTS_IB_CM_ACCEPT_IOCTL;

struct tTS_IB_CM_REJECT_IOCTL {
  tTS_IB_CM_COMM_ID    comm_id;
  void                *reply_data;
  int                  reply_size;
};

typedef struct tTS_IB_CM_REJECT_IOCTL tTS_IB_CM_REJECT_IOCTL_STRUCT,
 *tTS_IB_CM_REJECT_IOCTL;

struct tTS_IB_CM_CONFIRM_IOCTL {
  tTS_IB_CM_COMM_ID    comm_id;
  tTS_IB_QP_HANDLE     qp_handle;
  void                *reply_data;
  int                  reply_size;

  tTS_IB_QP_STATE      qp_state;		/* OUT */
};

typedef struct tTS_IB_CM_CONFIRM_IOCTL tTS_IB_CM_CONFIRM_IOCTL_STRUCT,
 *tTS_IB_CM_CONFIRM_IOCTL;

struct tTS_IB_CM_DELAYRESPONSE_IOCTL {
  tTS_IB_CM_COMM_ID    comm_id;
  int                  service_timeout;
  void                *mra_data;
  int                  mra_data_size;
};

typedef struct tTS_IB_CM_DELAYRESPONSE_IOCTL tTS_IB_CM_DELAYRESPONSE_IOCTL_STRUCT,
 *tTS_IB_CM_DELAYRESPONSE_IOCTL;

struct tTS_IB_CM_ESTABLISH_IOCTL {
  tTS_IB_CM_COMM_ID    comm_id;

  tTS_IB_QP_STATE      qp_state;		/* OUT */
};

typedef struct tTS_IB_CM_ESTABLISH_IOCTL tTS_IB_CM_ESTABLISH_IOCTL_STRUCT,
 *tTS_IB_CM_ESTABLISH_IOCTL;

struct tTS_IB_U_CM_GET_COMPLETION_IOCTL {
  tTS_IB_QP_HANDLE       qp_handle;
  tTS_IB_CM_COMM_ID      comm_id;
  tTS_IB_LISTEN_HANDLE   listen_handle;
  tTS_IB_CM_EVENT        event;
  unsigned long          cm_arg;
  void                  *params;
  ssize_t                params_size;
  tTS_IB_QP_STATE        qp_state;			/* OUT */
};

typedef struct tTS_IB_U_CM_GET_COMPLETION_IOCTL tTS_IB_U_CM_GET_COMPLETION_IOCTL_STRUCT,
 *tTS_IB_U_CM_GET_COMPLETION_IOCTL;

/* user mode CM filter ioctls */

/* we start numbering at 100 */

/* Make a connection */
#define TS_IB_IOCCMCONNECT       _IOWR(TS_IB_IOCTL_MAGIC, 100, tTS_IB_CM_CONNECT_IOCTL)

/* Begin listening */
#define TS_IB_IOCCMLISTEN        _IOWR(TS_IB_IOCTL_MAGIC, 101, tTS_IB_CM_LISTEN_IOCTL)

/* Cancel listen */
#define TS_IB_IOCCMLISTENSTOP    _IOW(TS_IB_IOCTL_MAGIC, 102, tTS_IB_LISTEN_HANDLE)

/* Disconnect a connection */
#define TS_IB_IOCCMDISCONNECT    _IOW(TS_IB_IOCTL_MAGIC, 103, tTS_IB_CM_COMM_ID)

/* Load an alternate path */
#define TS_IB_IOCCMALTPATHLOAD   _IOW(TS_IB_IOCTL_MAGIC, 104, tTS_IB_CM_ALTERNATE_PATH_LOAD_IOCTL)

/* Accept a connection (passive) */
#define TS_IB_IOCCMACCEPT        _IOW(TS_IB_IOCTL_MAGIC, 105, tTS_IB_CM_ACCEPT_IOCTL)

/* Reject a connection (passive) */
#define TS_IB_IOCCMREJECT        _IOW(TS_IB_IOCTL_MAGIC, 106, tTS_IB_CM_REJECT_IOCTL)

/* Confirm a connection (active) */
#define TS_IB_IOCCMCONFIRM       _IOWR(TS_IB_IOCTL_MAGIC, 107, tTS_IB_CM_CONFIRM_IOCTL)

/* Notify CM that a message has been received */
#define TS_IB_IOCCMESTABLISH     _IOWR(TS_IB_IOCTL_MAGIC, 109, tTS_IB_CM_ESTABLISH_IOCTL)

/* Drop consumer reference to connect */
#define TS_IB_IOCCMDROPCONSUMER  _IOW(TS_IB_IOCTL_MAGIC, 110, tTS_IB_CM_COMM_ID)

/* Get next completion buffer or block */
#define TS_IB_IOCCMGETCOMPLETION _IOWR(TS_IB_IOCTL_MAGIC, 111, tTS_IB_U_CM_GET_COMPLETION_IOCTL)

/* Assign a service id */
#define TS_IB_IOCCMSERVIDASSIGN  _IOW(TS_IB_IOCTL_MAGIC, 112, uint64_t *)

/* Delay response (passive) */
#define TS_IB_IOCCMDELAYRESPONSE _IOW(TS_IB_IOCTL_MAGIC, 113, tTS_IB_CM_DELAYRESPONSE_IOCTL)

#endif /* _TS_IB_USERACCESS_CM_H */
