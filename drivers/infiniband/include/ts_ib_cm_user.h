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

  $Id: ts_ib_cm_user.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_CM_USER_H
#define _TS_IB_CM_USER_H

#include "ts_ib_cm.h"
#include "ts_ib_core_types.h"

#ifndef __KERNEL__
#include <arpa/inet.h>		/* in_addr_t */
#else
typedef uint32_t in_addr_t;
#endif

#include <vapi_common.h>
#include <vapi.h>
#ifdef W2K_OS
#include <evapi.h>
#endif

/* ---------------- */
/* type definitions */
/* ---------------- */

typedef VAPI_qp_hndl_t tTS_IB_U_QP_HANDLE;
typedef VAPI_k_qp_hndl_t tTS_IB_U_K_QP_HANDLE;

#define TS_IB_QP_STATE_INVALID (-1)

/**
   Type for CM callback functions.  This is used for both active
   (connect) and passive (listen) callbacks.

   @param consumer_qp QP attached to connection
   @param comm_id Local Communication ID for connection
   @param state State that connection is in
   @param params Pointer to structure containing additional
   parameters for callback; type depends on state.
   @param arg Additional user supplied argument.

   @return 0 tells CM to continue handling the connection as usual;
   EWOULDBLOCK tells the CM to pause in handling the connection until
   called by the consumer; any other error tells the CM to tear down
   the connection.
*/
typedef int (*tTS_IB_U_CM_COMPLETION_FUNC)(tTS_IB_U_QP_HANDLE   consumer_qp,
					   tTS_IB_CM_COMM_ID    comm_id,
                                           tTS_IB_CM_EVENT      event,
					   void                *params,
					   void                *arg);

typedef struct tTS_IB_U_PATH_RECORD_STRUCT tTS_IB_U_PATH_RECORD_STRUCT,
    *tTS_IB_U_PATH_RECORD;

typedef struct tTS_IB_U_CM_QP_PARAM_STRUCT tTS_IB_U_CM_QP_PARAM_STRUCT,
    *tTS_IB_U_CM_QP_PARAM;
typedef struct tTS_IB_U_CM_REQ_RECEIVED_PARAM_STRUCT tTS_IB_U_CM_REQ_RECEIVED_PARAM_STRUCT,
    *tTS_IB_U_CM_REQ_RECEIVED_PARAM;
typedef struct tTS_IB_U_CM_REP_RECEIVED_PARAM_STRUCT tTS_IB_U_CM_REP_RECEIVED_PARAM_STRUCT,
    *tTS_IB_U_CM_REP_RECEIVED_PARAM;
typedef struct tTS_IB_U_CM_ESTABLISHED_PARAM_STRUCT tTS_IB_U_CM_ESTABLISHED_PARAM_STRUCT,
    *tTS_IB_U_CM_ESTABLISHED_PARAM;
typedef struct tTS_IB_U_CM_DREQ_RECEIVED_PARAM_STRUCT tTS_IB_U_CM_DREQ_RECEIVED_PARAM_STRUCT,
    *tTS_IB_U_CM_DREQ_RECEIVED_PARAM;
typedef struct tTS_IB_U_CM_DISCONNECTED_PARAM_STRUCT tTS_IB_U_CM_DISCONNECTED_PARAM_STRUCT,
    *tTS_IB_U_CM_DISCONNECTED_PARAM;
typedef struct tTS_IB_U_CM_IDLE_PARAM_STRUCT tTS_IB_U_CM_IDLE_PARAM_STRUCT,
    *tTS_IB_U_CM_IDLE_PARAM;

/* --------------------- */
/* structure definitions */
/* --------------------- */

struct tTS_IB_U_PATH_RECORD_STRUCT {
  tTS_IB_GID      dgid;
  tTS_IB_GID      sgid;
  tTS_IB_LID      dlid;
  tTS_IB_LID      slid;
  uint32_t        flowlabel;
  uint8_t         hoplmt;
  uint8_t         tclass;
  tTS_IB_PKEY     pkey;
  tTS_IB_SL       sl;
  tTS_IB_MTU      mtu;
  tTS_IB_RATE     rate;
  uint8_t         packet_life;
};

/**
   QP parameters which are sent to the remote.
*/
struct tTS_IB_U_CM_QP_PARAM_STRUCT {
  uint8_t responder_resources;
  uint8_t initiator_depth;
  uint8_t retry_count;
  uint8_t rnr_retry_count;
  uint8_t cm_response_timeout;
  uint8_t max_cm_retries;
};

/**
   Structure passed to listen callback when a REQ is received.

   consumer_qp should be set to a new QP (in state INIT).
   remote_private_data contains a pointer to private data in REQ packet.
*/
struct tTS_IB_U_CM_REQ_RECEIVED_PARAM_STRUCT {
    tTS_IB_LISTEN_HANDLE     listen_handle;
    tTS_IB_SERVICE_ID        service_id;
    tTS_IB_QPN               local_qpn;
    tTS_IB_QPN               remote_qpn;
    tTS_IB_GUID              remote_guid;
    tTS_IB_GID               dgid;
    tTS_IB_GID               sgid;
    unsigned short           dlid;
    unsigned short           slid;
    tTS_IB_PORT              port; /* local port - packet received on */
    void                    *remote_private_data;
    int                      remote_private_data_len;
};

/**
   Structure passed to connect callback when a REP is received.

   remote_private_data contains a pointer to private data in REP packet.
*/
struct tTS_IB_U_CM_REP_RECEIVED_PARAM_STRUCT {
    tTS_IB_QPN               local_qpn;
    tTS_IB_QPN               remote_qpn;
    void                    *remote_private_data;
    int                      remote_private_data_len;
};

#if 0
struct tTS_IB_U_CM_ESTABLISHED_PARAM_STRUCT {
};

struct tTS_IB_U_CM_DREQ_RECEIVED_PARAM_STRUCT {
};
#endif

struct tTS_IB_U_CM_DISCONNECTED_PARAM_STRUCT {
    tTS_IB_CM_DISCONNECTED_REASON reason;
};

/**
   rej_info_len and rej_info will be valid when the connection becomes
   idle due to a remote REJ.
*/
struct tTS_IB_U_CM_IDLE_PARAM_STRUCT {
    tTS_IB_CM_IDLE_REASON  reason;
    tTS_IB_CM_REJ_REASON   rej_reason;
    int                      rej_info_len;
    void                    *rej_info;
};

/* -------------------- */
/* function definitions */
/* -------------------- */

/**
   Allocate a locally administered service ID.  (See IB spec A3.2.3.3)
*/
tTS_IB_SERVICE_ID tsIbUCmServiceAssign(
                                       void
                                       );

/**
   Establish a connection using the CM.  (This is the "active" side)

   The primary and alternate port to use (on the local HCA) will be
   determined from the sgid member of primary_path and
   alternate_path.  In particular this means that sgid must be valid!

   @param qp_param Information passed to passive side.
   @param qp_handle QP handle (in state INIT).
   @param primary_path Primary path to remote port
   @param alternate_path Alternate path to remote port (or NULL)
   @param service_id Service ID to attempt connection to
   @param req_private_data Private data to include in REQ
   @param req_private_data_len Length of private data
   @param comp_func Callback function for this connection
   @param comp_arg Parameter to be passed to callback function
   @param comm_id Pointer for return of CM id of connection

   @return error code
*/
int tsIbUCmConnect(
                   VAPI_hca_hndl_t                hca_handle,
                   tTS_IB_U_QP_HANDLE             vqp_handle,
                   tTS_IB_QPN                     qpn,
		   tTS_IB_U_CM_QP_PARAM           qp_param,
		   tTS_IB_PATH_RECORD             primary_path,
		   tTS_IB_PATH_RECORD             alternate_path,
		   tTS_IB_SERVICE_ID              service_id,
		   void                          *req_private_data,
		   int                            req_private_data_len,
		   tTS_IB_U_CM_COMPLETION_FUNC    comp_func,
		   void                          *comp_arg,
		   tTS_IB_CM_COMM_ID             *comm_id
		   );

/**
   Begin listening for connections to a set of services.  (This is the
   "passive" side)

   @param service_id Base service ID to listen for
   @param service_mask Mask for service IDs to listen for (listen will
   apply to any connection attempts such that (connecting_id &
   service_mask) == service_id)
   @param comp_func CM callback function
   @param comp_arg Parameter to be passed to CM callback function
   @param listen_handle Will be set to handle to be used to cancel
   this listen request

   @return error code
*/
int tsIbUCmListen(
		  tTS_IB_SERVICE_ID           service_id,
		  tTS_IB_SERVICE_ID           service_mask,
		  tTS_IB_U_CM_COMPLETION_FUNC comp_func,
		  void                       *comp_arg,
		  tTS_IB_LISTEN_HANDLE       *listen_handle
		  );

/**
   Cancel a listen request

   @param listen_handle Handle to listen request to cancel

   @return error code
*/
int tsIbUCmListenStop(
		      tTS_IB_LISTEN_HANDLE listen_handle
		      );

/**
   Disconnect a connection managed by CM

   @param comm_id Local CM id of connection to disconnect

   @return error code
*/
int tsIbUCmDisconnect(
		      tTS_IB_CM_COMM_ID comm_id
		      );

/**
   Load a new alternate path for a connection (and send a LAP).  Can
   only be called by the active (client) side of a connection.

   @param comm_id Local CM id of connection
   @param alternate_path New alternate path to load

   @return error code
*/
int tsIbUCmAlternatePathLoad(
			     tTS_IB_CM_COMM_ID  comm_id,
			     tTS_IB_PATH_RECORD alternate_path
			     );

/**
   Update the argument (context pointer) passed to CM state
   transition function.

   @param comm_id CM id for connection
   @param comp_func New callback function (NULL will result in no
   more completions)
   @param comp_arg New context pointer
*/
int tsIbUCmCallbackUpdate(
			  tTS_IB_CM_COMM_ID           comm_id,
			  tTS_IB_U_CM_COMPLETION_FUNC comp_func,
			  void                       *comp_arg
			  );

/**
   Accept a deferred connection request (called by the passive side.)

   @param comm_id CM id for connection
   @param consumer_qp The handle of a new QP in state INIT
   @param reply_data Private data
   @param reply_data_len Length of private data
*/
int tsIbUCmAccept(
		  tTS_IB_CM_COMM_ID              comm_id,
                  VAPI_hca_hndl_t                hca_handle,
                  tTS_IB_U_QP_HANDLE             vqp_handle,
                  tTS_IB_QPN                     qpn,
		  tTS_IB_U_CM_COMPLETION_FUNC    comp_func,
		  void                          *comp_arg,
		  void                          *reply_data,
		  int                            reply_size
		  );

/**
   Reject a deferred connection request (called by the passive side.)

   @param comm_id CM id for connection
   @param reply_data Private data
   @param reply_data_len Length of private data
*/
int tsIbUCmReject(
		  tTS_IB_CM_COMM_ID    comm_id,
		  void                *reply_data,
		  int                  reply_size
		  );

/**
   Confirm a connection request (called by the active side.)

   @param comm_id CM id for connection
   @param reply_data Private data
   @param reply_data_len Length of private data
*/
int tsIbUCmConfirm(
		   tTS_IB_CM_COMM_ID    comm_id,
		   void                *reply_data,
		   int                  reply_size
		   );

/**
   Delay response (MRA) for a received connection (called by the passive side.)

   @param comm_id CM id for connection
   @param service_timeout Timeout for MRA
   @param mra_data Private data
   @param mra_data_size Length of private data
*/
int tsIbUCmDelayResponse(
		         tTS_IB_CM_COMM_ID    comm_id,
                         int                  service_timeout,
		         void                *mra_data,
		         int                  mra_data_size
		         );

/**
   Notify the CM that a message has been received on a connection.
   This is for the case where a message is received before the RTU.
   The CM will move the corresponding QP to RTS and update its
   internal state to show the connection as ESTABLISHED.

   @param comm_id CM id for connection
*/
int tsIbUCmEstablish(
		     tTS_IB_CM_COMM_ID comm_id
		     );

/**
   Drop consumer reference to the connection object. Used to notify
   the CM to clean its state when the consumer destroys the QP.

   @param comm_id CM id for connection
*/
int tsIbUCmDropConsumer(
			tTS_IB_CM_COMM_ID comm_id
			);

/**
   Obtain a path record for a destination.

   @param dst Destination IP address
   @param path_record Path record structure to store in
*/
int tsIbUCmPathRecord(
                      in_addr_t dst_addr,
                      tTS_IB_PATH_RECORD path_record
                      );

#endif /* _TS_IB_CM_USER_H */
