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

  $Id: sdp_event.c,v 1.51 2004/02/24 23:48:47 roland Exp $
*/

#include "sdp_main.h"

/* --------------------------------------------------------------------- */
/*                                                                       */
/* Primary QP Event Handler                                              */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpEventLocked -- main per QP event handler */
tINT32 tsSdpEventLocked
(
 tTS_IB_CQ_ENTRY comp,
 tSDP_CONN       conn
)
{
  tINT32 result = 0;

  TS_CHECK_NULL(comp, -EINVAL);
  TS_CHECK_NULL(conn, -EINVAL);

  if (0 < (TS_SDP_ST_MASK_ERROR & conn->state)) {
    /*
     * Ignore events in error state, connection is being terminated,
     * connection cleanup will take care of freeing posted buffers.
     */
    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: Completions <%d:%d:%u:%d:%d> ignored in state. <%04x>",
	     comp->status,
	     comp->work_request_id,
	     comp->op,
	     comp->bytes_transferred,
	     comp->immediate_data,
	     conn->state);
    result = 0;
    goto done;
  } /* if */
  /*
   * event demultiplexing
   */
  switch (comp->op) {
  case TS_IB_OP_RECEIVE:

    result = tsSdpEventRecv(conn, comp);
    break;
  case TS_IB_OP_SEND:

    result = tsSdpEventSend(conn, comp);
    break;
  case TS_IB_OP_RDMA_READ:

    result = tsSdpEventRead(conn, comp);
    break;
  case TS_IB_OP_RDMA_WRITE:

    result = tsSdpEventWrite(conn, comp);
    break;
  default:
    /*
     * sometimes the errors come from the CQ but without an operation
     */
    result = 0;

    switch (comp->status) {
    case TS_IB_COMPLETION_STATUS_WORK_REQUEST_FLUSHED_ERROR:
      break;
    case TS_IB_COMPLETION_STATUS_SUCCESS:

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	       " EVENT: unknown IB event. <%d>", comp->op);
      result = -ERANGE;
      break;
    default:
      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: unhandled error status <%d> for unknown event. <%d>",
	       comp->status, comp->op);
      result = -EIO;
      break;
    } /* switch */

    break;
  } /* switch */
  /*
   * release socket before error processing.
   */
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	     "EVENT: Connection <%04x> ABORT. <%d:%d> <%u:%d:%u>",
	     conn->hashent,
	     result,
	     comp->status,
	     comp->work_request_id,
	     comp->op,
	     comp->bytes_transferred);
    /*
     * abort.
     */
    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_ERROR_CQ);

    result = tsSdpAbort(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	       "EVENT: error during abort. <%d>", result);
    } /* if */

    return -EFAULT;
  } /* if */

done:
  return result;
} /* tsSdpEventLocked */

/* ========================================================================= */
/*..tsSdpEventHandler -- main per QP event handler, and demuxer */
void tsSdpEventHandler
(
 tTS_IB_CQ_HANDLE cq,
 tPTR             arg   /* tINT32 */
)
{
  tINT32 hashent = (unsigned long) arg;
  tSDP_CONN conn;
  tINT32 result;
#ifdef TS_KERNEL_2_6
  unsigned long flags;
#endif

#ifdef _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: Connection <%d> event on cq <%d>", hashent, cq);
#endif
  /*
   * get socket
   */
  conn = tsSdpConnTableLookup(hashent);
  if (NULL == conn) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "EVENT: unknown socket <%d> for cq <%d> event.", hashent, cq);
    goto done;
  } /* if */
  /*
   * lock the bottom half of the socket. If the connection is in use,
   * then queue the event, otherwise process this event.
   */
  TS_SDP_CONN_LOCK_BH(conn);
  /*
   * Check for event completions before CM has transitioned to
   * the established state. The CQ will not be polled or rearmed
   * until the CM makes the transition. Once the CM transition
   * has been made, the act of unlocking the connection will
   * drain the CQ.
   */
  if (0 == (TS_SDP_ST_MASK_EVENTS & conn->state)) {

    if (TS_SDP_CONN_ST_REP_SENT == conn->state ||  /* passive connect */
	TS_SDP_CONN_ST_RTU_SENT == conn->state) {  /* active connect */

      result = tsIbCmEstablish(conn->comm_id, 0);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));
    } /* if */
    else {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_INOUT,
	     "EVENT: socket <%d> unexpected state. <%04x>",
	       hashent, conn->state);
    } /* else */

    conn->flags |= TS_SDP_CONN_F_MASK_EVENT;
    goto unlock;
  } /* if */

  if (0 == conn->lock.users) {
    /*
     * dispatch CQ completions.
     */
    (void)tsSdpConnCqDrain(cq, conn);
  } /* if */
  else {
    /*
     * Mark the event which was received, for the unlock code to
     * process at a later time.
     */
    conn->flags |= ((cq == conn->recv_cq) ?
		    TS_SDP_CONN_F_RECV_CQ_PEND : TS_SDP_CONN_F_SEND_CQ_PEND);
  } /* else */

unlock:
  TS_SDP_CONN_UNLOCK_BH(conn);
  TS_SDP_CONN_PUT(conn);
done:
  return;
} /* tsSdpEventHandler */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* Connection establishment IB/CM callback functions                     */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsSdpEventHelloValidate -- validate the hello header */
static tINT32 _tsSdpEventHelloValidate
(
 tSDP_MSG_HELLO msg_hello,
 tINT32         size
)
{
  tINT32 result;

  TS_CHECK_NULL(msg_hello, -EINVAL);

  if (sizeof(tSDP_MSG_HELLO_STRUCT) != size) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: hello message size mismatch. <%d:%d>",
	     size, sizeof(tSDP_MSG_HELLO_STRUCT));
    return -EINVAL;
  } /* if */
  /*
   * endian swap
   */
  result = _tsSdpMsgWireToHostBSDH(&msg_hello->bsdh);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  result = _tsSdpMsgWireToHostHH(&msg_hello->hh);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * validation and consistency checks
   */
  if (msg_hello->bsdh.size != sizeof(tSDP_MSG_HELLO_STRUCT)) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: hello message size mismatch. (2) <%d:%d>",
	     msg_hello->bsdh.size, sizeof(tSDP_MSG_HELLO_STRUCT));
    return -EINVAL;
  } /* if */

  if (TS_SDP_MSG_MID_HELLO != msg_hello->bsdh.mid) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: hello message unexpected ID. <%d>", msg_hello->bsdh.mid);
    return -EINVAL;
  } /*if */

  if (!(0 < msg_hello->hh.max_adv)) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: hellomessage, bad zcopy advertisment. <%d>",
	     msg_hello->hh.max_adv);
    return -EINVAL;
  } /* if */

  if ((0xF0 & msg_hello->hh.version) != (0xF0 & TS_SDP_MSG_VERSION)) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: hello message, major version mismatch. <%d:%d>",
	     ((0xF0 & msg_hello->hh.version) >> 4),
	     ((0xF0 & TS_SDP_MSG_VERSION) >> 4));
    return -EINVAL;
  } /* if */
#ifdef _TS_SDP_MS_APRIL_ERROR_COMPAT
  if ((TS_SDP_MSG_IPVER & 0x0F) != (msg_hello->hh.ip_ver & 0x0F)) {
#else
  if ((TS_SDP_MSG_IPVER & 0xF0) != (msg_hello->hh.ip_ver & 0xF0)) {
#endif

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: hello message, ip version mismatch. <%d:%d>",
	     msg_hello->hh.ip_ver, TS_SDP_MSG_IPVER);
    return -EINVAL;
  } /* if */
#ifdef _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: BSDH <%04x:%02x:%02x:%08x:%08x:%08x>",
	   msg_hello->bsdh.recv_bufs,
	   msg_hello->bsdh.flags,
	   msg_hello->bsdh.mid,
	   msg_hello->bsdh.size,
	   msg_hello->bsdh.seq_num,
	   msg_hello->bsdh.seq_ack);
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: HH  <%02x:%02x:%02x:%08x:%08x:%04x:%08x:%08x>",
	   msg_hello->hh.max_adv,
	   msg_hello->hh.ip_ver,
	   msg_hello->hh.version,
	   msg_hello->hh.r_rcv_size,
	   msg_hello->hh.l_rcv_size,
	   msg_hello->hh.port,
	   msg_hello->hh.src.ipv4.addr,
	   msg_hello->hh.dst.ipv4.addr);
#endif

  return 0; /* success */
} /* _tsSdpEventHelloValidate */

/* ========================================================================= */
/*.._tsSdpEventHelloAckValidate -- validate the hello ack header */
static tINT32 _tsSdpEventHelloAckValidate
(
 tSDP_MSG_HELLO_ACK hello_ack,
 tINT32             size
)
{
  tINT32 result;

  TS_CHECK_NULL(hello_ack, -EINVAL);

  if (sizeof(tSDP_MSG_HELLO_ACK_STRUCT) != size) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: hello message size mismatch. <%d:%d>",
	     size, sizeof(tSDP_MSG_HELLO_ACK_STRUCT));
    return -EINVAL;
  } /* if */
  /*
   * endian swap
   */
  result = _tsSdpMsgWireToHostBSDH(&hello_ack->bsdh);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  result = _tsSdpMsgWireToHostHAH(&hello_ack->hah);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * validation and consistency checks
   */
  if (hello_ack->bsdh.size != sizeof(tSDP_MSG_HELLO_ACK_STRUCT)) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: hello ack, size mismatch. (2) <%d:%d>",
	     hello_ack->bsdh.size, sizeof(tSDP_MSG_HELLO_ACK_STRUCT));
    return -EINVAL;
  } /* if */

  if (TS_SDP_MSG_MID_HELLO_ACK != hello_ack->bsdh.mid) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: hello ack, unexpected message. <%d>",
	     hello_ack->bsdh.mid);
    return -EINVAL;
  } /*if */

  if (!(0 < hello_ack->hah.max_adv)) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: hello ack, bad zcopy advertisment. <%d>",
	     hello_ack->hah.max_adv);
    return -EINVAL;
  } /* if */

  if ((0xF0 & hello_ack->hah.version) != (0xF0 & TS_SDP_MSG_VERSION)) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: hello ack, major version mismatch. <%d:%d>",
	     ((0xF0 & hello_ack->hah.version) >> 4),
	     ((0xF0 & TS_SDP_MSG_VERSION) >> 4));
    return -EINVAL;
  } /* if */

#ifdef _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: BSDH <%04x:%02x:%02x:%08x:%08x:%08x>",
	   hello_ack->bsdh.recv_bufs,
	   hello_ack->bsdh.flags,
	   hello_ack->bsdh.mid,
	   hello_ack->bsdh.size,
	   hello_ack->bsdh.seq_num,
	   hello_ack->bsdh.seq_ack);
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: HAH <%02x:%02x:%08x>",
	   hello_ack->hah.max_adv,
	   hello_ack->hah.version,
	   hello_ack->hah.l_rcv_size);
#endif

  return 0; /* success */
} /* _tsSdpEventHelloAckValidate */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* State specific Connection Managment callback functions                */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsSdpCmReqRecv -- handler for passive connection open completion */
static tINT32 _tsSdpCmReqRecv
(
 tTS_IB_CM_COMM_ID            comm_id,
 tTS_IB_CM_REQ_RECEIVED_PARAM param,
 tPTR                         arg
)
{
  tSDP_MSG_HELLO msg_hello;
  tSDP_CONN      conn;
  tINT32         result;

  TS_CHECK_NULL(param, -EINVAL);
  TS_CHECK_NULL(param->remote_private_data, -EINVAL);

  msg_hello = (tSDP_MSG_HELLO)param->remote_private_data;

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: REQ. commID <%08x> service ID <%08x> ca <%p> port <%08x>",
	   (tUINT32)comm_id, (tUINT32)param->service_id,
	   param->device, (tUINT32)param->port);
  /*
   * check Hello Header, to determine if we want the connection.
   */
  result = _tsSdpEventHelloValidate(msg_hello, param->remote_private_data_len);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: REQ recveive, hello message <%d> failed. <%d>",
	     comm_id, result);
    goto done;
  } /* if */
  /*
   * Create a connection for this request.
   */
  conn = tsSdpConnAllocate(GFP_ATOMIC, comm_id); /* CM sk reference */
  if (NULL == conn) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: REQ receive, failed to create connection. <%d>", comm_id);
    result = -ENOMEM;
    goto done;
  } /* if */
  /*
   * Lock the new connection before insterting it into any tables.
   */
  TS_SDP_CONN_LOCK(conn);
  /*
   * associate connection with a hca/port, and allocate IB.
   */
  result = tsSdpConnAllocateIb(conn, param->device, param->port);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: REQ receive, Error <%d> binding connection to HCA/port.",
	     result);
    goto error;
  } /* if */

  conn->src_addr = msg_hello->hh.dst.ipv4.addr;
  conn->src_port = TS_SDP_MSG_SID_TO_PORT(param->service_id);
  conn->dst_addr = msg_hello->hh.src.ipv4.addr;
  conn->dst_port = msg_hello->hh.port;

  TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_REQ_RECV);
  memcpy(conn->d_gid, param->dgid, sizeof(conn->d_gid));
  conn->d_lid = param->dlid;
  conn->s_lid = param->slid;
  conn->d_qpn = param->remote_qpn;
  conn->s_qpn = param->local_qpn;
  /*
   * read remote information
   */
  conn->send_size = msg_hello->hh.l_rcv_size;
  conn->r_max_adv = msg_hello->hh.max_adv;
  conn->r_recv_bf = msg_hello->bsdh.recv_bufs;
  conn->recv_seq  = msg_hello->bsdh.seq_num;
  conn->advt_seq  = msg_hello->bsdh.seq_num;
  /*
   * the size we advertise to the stream peer cannot be larger then our
   * internal buffer size.
   */
  conn->send_size = min((tUINT16)tsSdpBuffMainBuffSize(),
			(tUINT16)(conn->send_size - TS_SDP_MSG_HDR_SIZE));

  result = tsIbCmCallbackModify(conn->comm_id,
				tsSdpCmEventHandler,
				(tPTR) (unsigned long) conn->hashent);
  if (0 != result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "CREATE: CM callback update failed. <%d>", result);
    goto error;
  } /* if */
  /*
   * initiate a connection to the stream interface.
   */
  result = tsSdpSockConnect(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: initiated connection failed. <%d>", result);
    goto error;
  } /* if */
  /*
   * unlock
   */
  TS_SDP_CONN_UNLOCK(conn);
  return TS_IB_CM_CALLBACK_DEFER; /* async response request */

error:
  conn->comm_id = TS_IB_CM_COMM_ID_INVALID;
  TS_SDP_CONN_UNLOCK(conn);
  TS_SDP_CONN_PUT(conn); /* CM sk reference */
done:
  return result;
} /* _tsSdpCmReqRecv */

/* ========================================================================= */
/*.._tsSdpCmRepRecv -- handler for active connection open completion */
static tINT32 _tsSdpCmRepRecv
(
 tTS_IB_CM_COMM_ID            comm_id,
 tTS_IB_CM_REP_RECEIVED_PARAM param,
 tSDP_CONN                    conn
)
{
  tSDP_MSG_HELLO_ACK hello_ack;
  tSDP_BUFF buff;
  tINT32 result;
  tINT32 error;

  TS_CHECK_NULL(param, -EINVAL);
  TS_CHECK_NULL(param->remote_private_data, -EINVAL);

  if (NULL == conn) {
    return -EINVAL;
  } /* if */

  hello_ack = (tSDP_MSG_HELLO_ACK)param->remote_private_data;

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: <%d> REP receive. comm ID <%08x> qpn <%06x:%06x>",
	   conn->hashent, (tINT32)comm_id,
	   param->local_qpn, param->remote_qpn);
  /*
   * lock the connection
   */
  switch (conn->state) {
  case TS_SDP_CONN_ST_ERROR_STRM:

    result = tsSdpCmReject(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	       "STRM: Connection manager reject request failed. <%d>", result);
      error = result;
      goto done;
    } /* if */

    TS_SDP_CONN_PUT(conn);

    break;
  case TS_SDP_CONN_ST_REQ_SENT:

    if (comm_id != conn->comm_id) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: REP received. comm ID mismatch. <%08x:%08x>",
	       conn->comm_id, comm_id);
    } /* if */
    /*
     * check Hello Header Ack, to determine if we want the connection.
     */
    result = _tsSdpEventHelloAckValidate(hello_ack,
					 param->remote_private_data_len);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: REP receive <%d> hello ack error. <%d>",
	       result, comm_id);
      error = result;
      goto reject;
    } /* if */

    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_REP_RECV);
    /*
     * read remote information
     */
    conn->send_size = hello_ack->hah.l_rcv_size;
    conn->r_max_adv = hello_ack->hah.max_adv;
    conn->r_recv_bf = hello_ack->bsdh.recv_bufs;
    conn->recv_seq  = hello_ack->bsdh.seq_num;
    conn->advt_seq  = hello_ack->bsdh.seq_num;
    conn->d_qpn = param->remote_qpn;
    conn->s_qpn = param->local_qpn;
    /*
     * Pop the hello message that was sent
     */
    buff = tsSdpBuffPoolGetHead(&conn->send_post);
    if (NULL == buff) {

      TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: REP receive <%d> hello msg missing in send pool.",
	       comm_id);
    } /* if */
    else {

      result = tsSdpBuffMainPut(buff);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));
    } /* else */
    /*
     * accept the connection to the gateway stream interface. The maximum
     * segment size we can recv from the stream interface is maximum the
     * remote host can receive minus the room that we need for headers.
     * Both optional and required headers are included since we won't
     * know which we're including until data transfer.
     */
    conn->send_size = min((tUINT16)tsSdpBuffMainBuffSize(),
			  (tUINT16)(conn->send_size - TS_SDP_MSG_HDR_SIZE));

    result = tsSdpSockAccept(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: REP receive <%d>. accept failed. <%d>",
	       comm_id, result);
      error = result;
      goto done;
    } /* if */

    break;
  default:
    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: REP received in an unknown state. <%d>", conn->state);
    /*
     * drop CM reference
     */
    result = tsSdpSockDrop(conn);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));

    error = -EPROTO;
    goto done;
  } /* switch */

  error = TS_IB_CM_CALLBACK_DEFER; /* async response request */
  goto out;

reject:
  result = tsSdpSockReject(conn, EPROTO);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
done:
  conn->comm_id = TS_IB_CM_COMM_ID_INVALID;
out:
  return error;
} /* _tsSdpCmRepRecv */

/* ========================================================================= */
/*.._tsSdpCmIdle -- handler for connection idle completion */
static tINT32 _tsSdpCmIdle
(
 tTS_IB_CM_COMM_ID    comm_id,
 tTS_IB_CM_IDLE_PARAM param,
 tSDP_CONN            conn
)
{
  tINT32 result = 0;
  tINT32 expect;

  TS_CHECK_NULL(param, -EINVAL);

  if (NULL == conn) {
    return -EINVAL;
  } /* if */
  /*
   * IDLE should only be called after some other action on the comm_id,
   * which means the callback argument will be a SDP conn, since the
   * only time it is not a SDP conn is the first callback in a passive
   * open.
   */
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: <%d> IDLE, comm ID <%08x> reason <%d> state <%04x>",
	   conn->hashent, (tINT32)comm_id, param->reason, conn->state);
  /*
   * last comm reference
   */
  conn->comm_id = TS_IB_CM_COMM_ID_INVALID;
  /*
   * check state
   */
  switch(conn->state) {
  case TS_SDP_CONN_ST_REQ_PATH:
    /*
     * cancel address resolution
     */
    result = tsIp2prPathRecordCancel(conn->plid);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
    /*
     * fall through
     */
  case TS_SDP_CONN_ST_REQ_SENT: /* active open, Hello msg sent */

    result = tsSdpSockReject(conn, ECONNREFUSED);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: error while rejecting conn to gateway. <%d>", result);
      goto error;
    } /* if */

    break;
  case TS_SDP_CONN_ST_REP_SENT: /* passive open, Hello ack msg sent */

    result = tsSdpSockFailed(conn, ECONNREFUSED);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: error while failing conn to gateway. <%d>", result);
      goto error;
    } /* if */

    break;
  case TS_SDP_CONN_ST_REP_RECV: /* active open, Hello ack msg recv'd */
  case TS_SDP_CONN_ST_REQ_RECV: /* passive open, Hello msg recv'd */
    /*
     * connection state is outstanding to the gateway, so the connection
     * cannot be destroyed, until the gateway responds. The connection
     * is marked for destruction. No return error, since this is the
     * last CM callback.
     */
    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_ERROR_CM);
    break;
  case TS_SDP_CONN_ST_TIME_WAIT_1:
  case TS_SDP_CONN_ST_ESTABLISHED:

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: IDLE, unexpected connection state. <%04x>", conn->state);
    /*
     * fall through
     */
  case TS_SDP_CONN_ST_ERROR_STRM:
  case TS_SDP_CONN_ST_TIME_WAIT_2:
    /*
     * Connection is finally dead. Drop the CM reference
     */
    result = tsSdpSockDrop(conn);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));

    break;
  default:

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	     "EVENT: IDLE, unknown connection state. <%04x>", conn->state);
    result = -EINVAL;
    goto error;
    break;
  } /* switch */

  return 0;
error:
  /*
   * failed to notify INET of SDP termination of this connection,
   * last attempt to drop the CM reference.
   */
  expect = tsSdpSockDrop(conn);
  TS_EXPECT(MOD_LNX_SDP, !(0 > expect));

  return result;
} /* _tsSdpCmIdle */

/* ========================================================================= */
/*.._tsSdpCmEstablished -- handler for connection established completion */
static tINT32 _tsSdpCmEstablished
(
 tTS_IB_CM_COMM_ID           comm_id,
 tTS_IB_CM_ESTABLISHED_PARAM param,
 tSDP_CONN                   conn
)
{
  tINT32 result = 0;
  tINT32 expect;
  tSDP_BUFF buff;

  TS_CHECK_NULL(param, -EINVAL);

  if (NULL == conn) {
    return -EINVAL;
  } /* if */

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: <%d> ESTABLISHED, comm ID <%08x> state <%04x>",
	   conn->hashent, (tINT32)comm_id, conn->state);
  /*
   * release disconnects.
   */
  conn->flags &= ~TS_SDP_CONN_F_DIS_HOLD;
  /*
   * check state
   */
  switch(conn->state) {

  case TS_SDP_CONN_ST_REP_SENT: /* passive open, Hello ack msg sent */

    buff = tsSdpBuffPoolGetHead(&conn->send_post);
    if (NULL == buff) {

      TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: ESTABLISHED, hello ack missing in send pool.");
    } /* if */
    else {

      result = tsSdpBuffMainPut(buff);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));
    } /* else */

    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_ESTABLISHED);

    result = tsSdpSockConfirm(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: error while confirming conn to gateway. <%d>", result);
      goto done; /* CM sk reference released by Confirm error processing  */
    } /* if */

    result = tsSdpSendFlush(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: ESTABLISHED, Failure during send queue flush. <%d>",
	       result);
      goto error;
    } /* if */

    result = tsSdpRecvPost(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: Receive buffer post failed on RDMA read. <%d>", result);
      goto error;
    } /* if */

    break;
  case TS_SDP_CONN_ST_RTU_SENT:   /* active open, Hello ack ack'd */
    /*
     * Set state.
     */
    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_ESTABLISHED);
    /*
     * fall through
     */
  case TS_SDP_CONN_ST_DIS_PEND_1: /* active open, and active close */
  case TS_SDP_CONN_ST_DIS_PEND_R: /* active open, and active close, confirm */
  case TS_SDP_CONN_ST_DIS_PEND_2:

    result = tsSdpSendFlush(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: ESTABLISHED, Failure during send queue flush. <%d>",
	       result);
      goto error;
    } /* if */

    break;
  case TS_SDP_CONN_ST_ERROR_STRM:
    /*
     * Sockets has released reference, begin abortive disconnect.
     * Leave state unchanged, time_wait and idle will handle the
     * existing state correctly.
     */
    result = tsSdpCmDisconnect(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	       "EVENT: Error posting CM disconnect. <%d>", result);
    } /* if */

    break;
  default:

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	     "EVENT: ESTABLISHED, unexpected connection state. <%04x>",
	     conn->state);
    result = -EINVAL;
    goto error;
    break;
  } /* switch */

  return 0;
error:
  expect = tsSdpSockDrop(conn);
  TS_EXPECT(MOD_LNX_SDP, !(0 > expect));
done:
  conn->comm_id = TS_IB_CM_COMM_ID_INVALID;
  return result;
} /* _tsSdpCmEstablished */

/* ========================================================================= */
/*.._tsSdpCmTimeWait -- handler for connection Time Wait completion */
static tINT32 _tsSdpCmTimeWait
(
 tTS_IB_CM_COMM_ID            comm_id,
 tTS_IB_CM_DISCONNECTED_PARAM param,
 tSDP_CONN                    conn
)
{
  tINT32 result = 0;
  tINT32 expect;

  TS_CHECK_NULL(param, -EINVAL);

  if (NULL == conn) {

    return -EINVAL;
  } /* if */

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: <%d> TIME WAIT, ID <%08x> reason <%d> state <%04x>",
	   conn->hashent, (tINT32)comm_id, param->reason, conn->state);
  /*
   * Clear out posted receives now, vs after IDLE timeout, which consumes
   * too many buffers when lots of connections are being established and
   * torn down. Here is a good spot since we know that the QP has gone to
   * reset, and pretty much all take downs end up here.
   */
  (void)tsSdpBuffPoolClear(&conn->recv_post);
  /*
   * check state
   */
  switch (conn->state) {
  case TS_SDP_CONN_ST_ERROR_STRM:
    /*
     * error on stream interface, no more call to/from those interfaces.
     */
    break;
  case TS_SDP_CONN_ST_DIS_RECV_R:
  case TS_SDP_CONN_ST_DIS_SEND_2:
  case TS_SDP_CONN_ST_DIS_SENT_2:
  case TS_SDP_CONN_ST_TIME_WAIT_1:
    /*
     * SDP disconnect messages have been exchanged, and DREQ/DREP received,
     * wait for idle timer.
     */
    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_TIME_WAIT_2);
    break;
  case TS_SDP_CONN_ST_DIS_PEND_1:
  case TS_SDP_CONN_ST_DIS_SEND_1:
  case TS_SDP_CONN_ST_DIS_SENT_1:
  case TS_SDP_CONN_ST_DIS_RECV_1:
    /*
     * connection is being closed without a disconnect message, abortive
     * close.
     */
  case TS_SDP_CONN_ST_ESTABLISHED:
    /*
     * Change state, so we only need to wait for the abort callback, and
     * idle. Call the abort callback.
     */
    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_TIME_WAIT_1);

    result = tsSdpAbort(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	       "EVENT: error during abort. <%d>", result);
      goto error;
    } /* if */

    break;
  default:

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	     "EVENT: TIME_WAIT, unexpected state <%04x> reason. <%d>",
	     conn->state, param->reason);
    result = -EINVAL;
    goto error;
    break;
  } /* switch */

  return 0;
error:
  expect = tsSdpSockDrop(conn);
  TS_EXPECT(MOD_LNX_SDP, !(0 > expect));

  conn->comm_id = TS_IB_CM_COMM_ID_INVALID;
  return result;
} /* _tsSdpCmTimeWait */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* Primary Connection Managment callback function                        */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpCmEventHandler -- handler for CM state transitions request */
tTS_IB_CM_CALLBACK_RETURN tsSdpCmEventHandler
(
  tTS_IB_CM_EVENT event,
  tTS_IB_CM_COMM_ID comm_id,
  tPTR params,
  tPTR arg
)
{
  tINT32 hashent = (unsigned long) arg;
  tSDP_CONN conn = NULL;
  tINT32 result = 0;

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: CM state transition <%d> for comm ID <%08x> conn <%d>",
	   event, comm_id, hashent);
  /*
   * lookup the connection, on a REQ_RECV the sk will be empty.
   */
  conn = tsSdpConnTableLookup(hashent);
  if (NULL != conn) {

    TS_SDP_CONN_LOCK(conn);
  } /* if */
  else {

    if (TS_IB_CM_REQ_RECEIVED != event) {

      TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	       "EVENT: No socket for CM state <%d> comm ID <%08x> sock <%d>",
	       event, comm_id, hashent);
    } /* if */
  } /* else */

  switch (event) {
  case TS_IB_CM_REQ_RECEIVED:

    result = _tsSdpCmReqRecv(comm_id, params, arg);
    break;
  case TS_IB_CM_IDLE:

    result = _tsSdpCmIdle(comm_id, params, conn);
    break;
  case TS_IB_CM_REP_RECEIVED:

    result = _tsSdpCmRepRecv(comm_id, params, conn);
    break;
  case TS_IB_CM_ESTABLISHED:

    result = _tsSdpCmEstablished(comm_id, params, conn);
    break;
  case TS_IB_CM_DISCONNECTED:

    result = _tsSdpCmTimeWait(comm_id, params, conn);
    break;
  case TS_IB_CM_LAP_RECEIVED:
  case TS_IB_CM_APR_RECEIVED:
    /*
     * null transitions
     */
    result = TS_IB_CM_CALLBACK_PROCEED;
    break;
  default:

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: Unexpected CM state transition! <%d>", event);
    result = -EINVAL;
  } /* switch */
  /*
   * if a socket was found, release the lock, and put the reference.
   */
  if (NULL != conn) {

    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	       "EVENT: CM state error <%d> <%d:%04x>",
	       result, event, conn->state);
      /*
       * dump connection state if it is being recorded.
       */
      (void)__tsSdpConnStateDump(conn);
    } /* if */

    TS_SDP_CONN_UNLOCK(conn);
    TS_SDP_CONN_PUT(conn);
  } /* if */

  if (0 > result) {
    /*
     * Error in processing needs to be communicated back to the CM
     */
    result = TS_IB_CM_CALLBACK_ABORT;
  } /* if */

  return result;
} /* tsSdpCmEventHandler */
