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

  $Id: sdp_post.c,v 1.40 2004/02/24 23:48:50 roland Exp $
*/

#include "sdp_main.h"

/* --------------------------------------------------------------------- */
/*                                                                       */
/* Connection establishment functions                                    */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsSdpPostPathComplete-- path lookup complete, initiate SDP connection */
static tINT32 _tsSdpPostPathComplete
(
 tIP2PR_PATH_LOOKUP_ID  plid,
 tINT32               status,
 tUINT32              src_addr,
 tUINT32              dst_addr,
 tTS_IB_PORT          hw_port,
 tTS_IB_DEVICE_HANDLE ca,
 tTS_IB_PATH_RECORD   path,
 tPTR                 arg
)
{
  tTS_IB_CM_ACTIVE_PARAM_STRUCT active_param;
  tSDP_MSG_HELLO hello_msg;
  tSDP_CONN conn = (tSDP_CONN)arg;
  tINT32 result = 0;
  tSDP_BUFF buff;
  tINT32 expect;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * lock the socket
   */
  TS_SDP_CONN_LOCK(conn);
  /*
   * path lookup is complete
   */
  if (plid != conn->plid) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Path record request ID mismatch. <%016llx:%016llx>",
	     plid, conn->plid);
    goto done;
  } /* if */

  if (TS_SDP_CONN_ST_REQ_PATH != conn->state) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Incorrect state <%d> for path record completion.",
	     conn->state);
    goto done;
  } /* if */

  conn->plid      = TS_IP2PR_PATH_LOOKUP_INVALID;
  TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_REQ_SENT);
  /*
   * update addresses.
   */
  conn->src_addr = ntohl(src_addr);
  conn->dst_addr = ntohl(dst_addr);
  /*
   * create address handle
   */
  if (0 != status) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Path record completion with error <%d> status.", status);

    goto failed;
  } /* if */

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "POST: <%d> Path record lookup complete. <%016llx:%016llx:%d>",
	   conn->hashent, TS_GW_SWAP_64(*(tUINT64 *)path->dgid),
	   TS_GW_SWAP_64(*(tUINT64 *)(path->dgid + sizeof(tUINT64))),
	   path->dlid);
  /*
   * allocate IB resources.
   */
  result = tsSdpConnAllocateIb(conn, ca, hw_port);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "SOCK: Error <%d> allocating sockets <%d> IB components.",
	     result, conn->hashent);
    goto failed;
  } /* if */
  /*
   * create the hello message . (don't need to worry about header
   * space reservation)
   */
  buff = tsSdpBuffMainGet();
  if (NULL == buff) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failed to allocate buffer for Hello Message.");
    goto failed;
  } /* if */

  hello_msg  = (tSDP_MSG_HELLO)buff->data;
  buff->tail = buff->data + sizeof(tSDP_MSG_HELLO_STRUCT);

  memset(hello_msg, 0, sizeof(tSDP_MSG_HELLO_STRUCT));

  conn->l_advt_bf = conn->recv_max;
  conn->l_max_adv = TS_SDP_MSG_MAX_ADVS;

  hello_msg->bsdh.recv_bufs = conn->l_advt_bf;
  hello_msg->bsdh.flags     = TS_SDP_MSG_FLAG_NON_FLAG;
  hello_msg->bsdh.mid       = TS_SDP_MSG_MID_HELLO;
  hello_msg->bsdh.size      = sizeof(tSDP_MSG_HELLO_STRUCT);
  hello_msg->bsdh.seq_num   = conn->send_seq;
  hello_msg->bsdh.seq_ack   = conn->advt_seq;

  hello_msg->hh.max_adv       = conn->l_max_adv;
  hello_msg->hh.ip_ver        = TS_SDP_MSG_IPVER;
  hello_msg->hh.version       = TS_SDP_MSG_VERSION;
  hello_msg->hh.r_rcv_size    = conn->recv_size;
  hello_msg->hh.l_rcv_size    = conn->recv_size;
  hello_msg->hh.port          = conn->src_port;
  hello_msg->hh.src.ipv4.addr = conn->src_addr;
  hello_msg->hh.dst.ipv4.addr = conn->dst_addr;

  memcpy(conn->d_gid, path->dgid, sizeof(conn->d_gid));
  conn->d_lid = path->dlid;
  conn->s_lid = path->slid;

  /*
   * endian swap
   */
  result = _tsSdpMsgHostToWireBSDH(&hello_msg->bsdh);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  result = _tsSdpMsgHostToWireHH(&hello_msg->hh);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * save message
   */
  result = tsSdpBuffPoolPut(&conn->send_post, buff);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Error <%d> buffering hello packet.", result);

    expect = tsSdpBuffMainPut(buff);
    TS_EXPECT(MOD_LNX_SDP, !(0 > expect));

    goto failed;
  } /* if */
#if 1
  /*
   * Mellanox performance bug workaround.
   */
  if (TS_IB_MTU_1024 < path->mtu) {

    path->mtu = TS_IB_MTU_1024;
  } /* if */
#endif
  /*
   * set QP/CM parameters.
   */
  active_param.qp                   = conn->qp;
  active_param.req_private_data     = (tPTR)hello_msg,
  active_param.req_private_data_len = (tINT32)(buff->tail - buff->data);
  active_param.responder_resources  = 4;
  active_param.initiator_depth      = 4;
  active_param.retry_count          = TS_SDP_CM_PARAM_RETRY;
  active_param.rnr_retry_count      = TS_SDP_CM_PARAM_RNR_RETRY;
  active_param.cm_response_timeout  = 20; /* 4 seconds */
  active_param.max_cm_retries       = 15;
  active_param.flow_control         = 1;

  /* XXX set timeout to default value of 14 */
  path->packet_life = 13;

  /*
   * initiate connection
   */
  result = tsIbCmConnect(&active_param,
                         path,
                         NULL,
                         TS_SDP_MSG_PORT_TO_SID(conn->dst_port),
                         0,
                         tsSdpCmEventHandler,
                         (tPTR) (unsigned long) conn->hashent,
                         &conn->comm_id);
  if (0 != result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Connection manager connect attempt failed. <%d>",
	     result);
    goto failed;
  } /* if */

  result = 0;
  goto done;
failed:

  result = tsSdpSockReject(conn, (0 - status));
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: error while failing conn to gateway. <%d>", result);

    expect = tsSdpSockDrop(conn);
    TS_EXPECT(MOD_LNX_SDP, !(0 > expect));
  } /* if */

done:
  TS_SDP_CONN_UNLOCK(conn);
  TS_SDP_CONN_PUT(conn);

  return 0;
} /* _tsSdpPostPathComplete */

/* ========================================================================= */
/*..tsSdpPostMsgHello -- initiate a SDP connection with a hello message. */
tINT32 tsSdpPostMsgHello
(
 tSDP_CONN  conn
)
{
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * get the buffer size we'll use for this connection. (and all others)
   */
  if (sizeof(tSDP_MSG_HELLO_STRUCT) > conn->recv_size) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: buffer size <%d> too small. <%d>",
	     conn->recv_size, sizeof(tSDP_MSG_HELLO_STRUCT));
    result = -ENOBUFS;
    goto error;
  } /* if */

  TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_REQ_PATH);
  /*
   * lookup the remote address
   */
  TS_SDP_CONN_HOLD(conn);
  TS_SDP_CONN_UNLOCK(conn);

  result = tsIp2prPathRecordLookup(htonl(conn->dst_addr),
				   htonl(conn->src_addr),
				   TS_SDP_OS_SK_LOCALROUTE(conn->sk),
				   TS_SDP_OS_SK_BOUND_IF(conn->sk),
				   _tsSdpPostPathComplete,
				   conn,
				   &conn->plid);
  TS_SDP_CONN_LOCK(conn);

  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Error <%d> getting link <%08x:%08x> address.",
	     result, htonl(conn->dst_addr), htonl(conn->src_addr));
    /*
     * callback dosn't have this socket.
     */
    TS_SDP_CONN_PUT(conn);

    result = -EDESTADDRREQ;
    goto error;
  } /* if */

  return 0;
error:
  return result;
} /* tsSdpPostMsgHello */

/* ========================================================================= */
/*..tsSdpPostMsgHelloAck -- respond to a connection attempt with an ack */
tINT32 tsSdpPostMsgHelloAck
(
 tSDP_CONN  conn
)
{
  tTS_IB_CM_PASSIVE_PARAM_STRUCT passive_param;
  tSDP_MSG_HELLO_ACK hello_ack;
  tINT32 result;
  tINT32 expect;
  tSDP_BUFF buff;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * build listen response headers
   */
  if (sizeof(tSDP_MSG_HELLO_ACK_STRUCT) > conn->recv_size) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: buffer size <%d> too small. <%d>",
	     conn->recv_size, sizeof(tSDP_MSG_HELLO_ACK_STRUCT));
    result = -ENOBUFS;
    goto error;
  } /* if */
  /*
   * get a buffer, in which we will create the hello header ack.
   * (don't need to worry about header space reservation on sends)
   */
  buff = tsSdpBuffMainGet();
  if (NULL == buff) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Failed to allocaate buffer for Hello Ack Header.");
    result = -ENOMEM;
    goto error;
  } /* if */

  hello_ack  = (tSDP_MSG_HELLO_ACK)buff->data;
  buff->tail = buff->data + sizeof(tSDP_MSG_HELLO_ACK_STRUCT);
  /*
   * create the message
   */
  memset(hello_ack, 0, sizeof(tSDP_MSG_HELLO_ACK_STRUCT));

  conn->l_advt_bf = conn->recv_max;
  conn->l_max_adv = TS_SDP_MSG_MAX_ADVS;

  hello_ack->bsdh.recv_bufs = conn->l_advt_bf;
  hello_ack->bsdh.flags     = TS_SDP_MSG_FLAG_NON_FLAG;
  hello_ack->bsdh.mid       = TS_SDP_MSG_MID_HELLO_ACK;
  hello_ack->bsdh.size      = sizeof(tSDP_MSG_HELLO_ACK_STRUCT);
  hello_ack->bsdh.seq_num   = conn->send_seq;
  hello_ack->bsdh.seq_ack   = conn->advt_seq;

  hello_ack->hah.max_adv    = conn->l_max_adv;
  hello_ack->hah.version    = TS_SDP_MSG_VERSION;
  hello_ack->hah.l_rcv_size = conn->recv_size;
  /*
   * endian swap
   */
  result = _tsSdpMsgHostToWireBSDH(&hello_ack->bsdh);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  result = _tsSdpMsgHostToWireHAH(&hello_ack->hah);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * save message
   */
  result = tsSdpBuffPoolPut(&conn->send_post, buff);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Error <%d> buffering hello ack packet.", result);

    expect = tsSdpBuffMainPut(buff);
    TS_EXPECT(MOD_LNX_SDP, !(0 > expect));

    goto error;
  } /* if */
  /*
   * Post receive buffers for this connection
   */
  result = tsSdpRecvPost(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Error posting <%d> receive buffers. <%d>",
	     result, conn->recv_max);
    goto error;
  } /* if */
  /*
   * send REP message to remote CM to continue connection.
   */
  passive_param.qp                     = conn->qp;
  passive_param.reply_private_data     = hello_ack;
  passive_param.reply_private_data_len = buff->tail - buff->data;
  passive_param.responder_resources    = 4;
  passive_param.initiator_depth        = 4;
  passive_param.rnr_retry_count        = TS_SDP_CM_PARAM_RNR_RETRY;
  passive_param.flow_control           = 1;
  passive_param.failover_accepted      = 0;

  result = tsIbCmAccept(conn->comm_id,
                        &passive_param);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Connection manager accept request failed. <%d>", result);

    goto error;
  } /* if */

  return 0;
error:
  return result;
} /* tsSdpPostMsgHelloAck */

/* ========================================================================= */
/*..tsSdpPostListenStart --  start listening on all possible socket ports */
tINT32 tsSdpPostListenStart
(
 tSDP_DEV_ROOT dev_root
)
{
  tINT32 result = 0;

  TS_CHECK_NULL(dev_root, -EINVAL);
  /*
   * start listening
   */
  result = tsIbCmListen(TS_SDP_MSG_SERVICE_ID_VALUE,
                        TS_SDP_MSG_SERVICE_ID_MASK,
                        tsSdpCmEventHandler,         /* listen function */
                        (tPTR)TS_SDP_DEV_SK_INVALID, /* listen func arg */
                        &dev_root->listen_handle);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failed to start listening for SDP connections. <%d>",
	     result);
  } /* if */
  else {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "POST: Started listening for incomming SDP connections");
  } /* else */

  return result;
} /* tsSdpPostListenStart */

/* ========================================================================= */
/*..tsSdpPostListenStop --  stop listening on all possible socket ports */
tINT32 tsSdpPostListenStop
(
 tSDP_DEV_ROOT dev_root
)
{
  tINT32 result = 0;

  TS_CHECK_NULL(dev_root, -EINVAL);

  result = tsIbCmListenStop(dev_root->listen_handle);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failed to stop listening for SDP connections. <%d>",
	     result);
  } /* if */
  else {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "POST: Stopped listening for incomming SDP connections");
  } /* else */

  return result;
} /* tsSdpPostListenStop */

/* ========================================================================= */
/*.._tsSdpCmDisconnect -- initiate a disconnect request using the CM */
static void _tsSdpCmDisconnect
(
 tPTR arg
)
{
  tSDP_CONN  conn = (tSDP_CONN)arg;
  tINT32 result;

  if (NULL == conn) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "POST: Error, posting disconnect for NULL conn");
    return ;
  } /* if */

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "POST: <%d> Executing defered disconnect for conn. <%08x> <%04x>",
	   conn->hashent, conn->comm_id, conn->state);
  /*
   * send a disconnect request using the connection manager
   */
  result = tsIbCmDisconnect(conn->comm_id);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "POST: Error <%d> posting disconnect conn <%d> state <%04x> ",
	     result, conn->comm_id, conn->state);
  } /* if */

  TS_SDP_CONN_PUT(conn);

  return;
} /* _tsSdpCmDisconnect */

/* ========================================================================= */
/*.._tsSdpCmReject -- initiate a reject request using the CM */
static void _tsSdpCmReject
(
 tPTR arg
)
{
  tSDP_CONN  conn = (tSDP_CONN)arg;
  tINT32 result;

  if (NULL == conn) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "POST: Error, posting reject for NULL conn");
  } /* if */

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "POST: <%d> Executing defered reject for conn. <%08x> <%04x>",
	   conn->hashent, conn->comm_id, conn->state);
  /*
   * send a reject request using the connection manager
   */
  result = tsIbCmReject(conn->comm_id, NULL, 0);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "POST: Error <%d> posting reject for conn <%d>",
	     result, conn->comm_id);
  } /* if */

  TS_SDP_CONN_PUT(conn);

  return;
} /* _tsSdpCmReject */

/* ========================================================================= */
/*.._tsSdpCmConfirm -- initiate a confirm request using the CM */
static void _tsSdpCmConfirm
(
 tPTR arg
)
{
  tSDP_CONN  conn = (tSDP_CONN)arg;
  tINT32 result;

  if (NULL == conn) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "POST: Error, posting confirm for NULL conn");
  } /* if */

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "POST: <%d> Executing defered confirm for conn. <%08x> <%04x>",
	   conn->hashent, conn->comm_id, conn->state);
  /*
   * send a confirm request using the connection manager
   */
  result = tsIbCmConfirm(conn->comm_id, NULL, 0);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "POST: Error <%d> posting confirm for conn <%d>",
	     result, conn->comm_id);
  } /* if */

  TS_SDP_CONN_PUT(conn);

  return;
} /* _tsSdpCmConfirm */

/* ========================================================================= */
/*.._tsSdpCmFailed -- initiate a Failed request using the CM */
static void _tsSdpCmFailed
(
 tPTR arg
)
{
  tSDP_CONN  conn = (tSDP_CONN)arg;
  tINT32 result;

  if (NULL == conn) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "POST: Error, posting failed for NULL conn");
  } /* if */

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "POST: <%d> Executing defered failed for conn. <%08x> <%04x>",
	   conn->hashent, conn->comm_id, conn->state);
  /*
   * send a failed request using the connection manager
   */
  result = tsIbCmReject(conn->comm_id, NULL, 0);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "POST: Error <%d> posting failed for conn <%d>",
	     result, conn->comm_id);
  } /* if */

  TS_SDP_CONN_PUT(conn);

  return;
} /* _tsSdpCmFailed */

/* ========================================================================= */
/*.._tsSdpCmDeferredGeneric -- initiate a defered request using the*/
static tINT32 _tsSdpCmDeferredGeneric
(
 tSDP_CONN  conn,
 tTS_KERNEL_TIMER_FUNCTION func
)
{
  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(func, -EINVAL);
  /*
   * send a potentially defered failed request.
   */
  TS_SDP_CONN_HOLD(conn);

  if (in_interrupt()) {

    if (!tsKernelTimerPending(&conn->cm_exec)) {

      conn->cm_exec.function = func;
      conn->cm_exec.run_time = jiffies;
      conn->cm_exec.arg      = conn;
      tsKernelTimerAdd(&conn->cm_exec);
    } /* if */
    else {

      TS_SDP_CONN_PUT(conn);
    } /* else */
  } /* if */
  else {

    func(conn);
  } /* else */

  return 0;
} /* _tsSdpCmDeferredGeneric */

/* ========================================================================= */
/*..tsSdpCmDisconnect -- initiate a disconnect request using the CM */
tINT32 tsSdpCmDisconnect
(
 tSDP_CONN  conn
)
{
  return _tsSdpCmDeferredGeneric(conn, _tsSdpCmDisconnect);
} /* tsSdpCmDisconnect */

/* ========================================================================= */
/*..tsSdpCmReject -- initiate a reject request using the CM */
tINT32 tsSdpCmReject
(
 tSDP_CONN  conn
)
{
  return _tsSdpCmDeferredGeneric(conn, _tsSdpCmReject);
} /* tsSdpCmReject */

/* ========================================================================= */
/*..tsSdpCmConfirm -- initiate a confirm request using the CM */
tINT32 tsSdpCmConfirm
(
 tSDP_CONN  conn
)
{
  return _tsSdpCmDeferredGeneric(conn, _tsSdpCmConfirm);
} /* tsSdpCmConfirm */

/* ========================================================================= */
/*..tsSdpCmFailed -- initiate a failed request using the CM */
tINT32 tsSdpCmFailed
(
 tSDP_CONN  conn
)
{
  return _tsSdpCmDeferredGeneric(conn, _tsSdpCmFailed);
} /* tsSdpCmFailed */
