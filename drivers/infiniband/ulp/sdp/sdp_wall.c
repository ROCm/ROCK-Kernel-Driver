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

  $Id: sdp_wall.c 32 2004-04-09 03:57:42Z roland $
*/

#include "sdp_main.h"

/* --------------------------------------------------------------------- */
/*                                                                       */
/* SDP protocol (public functions)                                       */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpConnConnect -- callback to begin passive open */
tINT32 tsSdpConnConnect
(
 tSDP_CONN conn
)
{
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "CONN: <%d> <%08x:%04x><%08x:%04x> passive open <%04x>",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->state);
  /*
   * post the SDP hello message
   */
  result = tsSdpPostMsgHello(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Failed to post hello to start connection. <%d>", result);
    goto error;
  } /* if */

  return 0;
error:
  return result;
} /* tsSdpConnConnect */

/* ========================================================================= */
/*..tsSdpConnAccept -- callback to accept an active open */
tINT32 tsSdpConnAccept
(
 tSDP_CONN conn
)
{
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "CONN: <%d> <%08x:%04x><%08x:%04x> open accepted <%04x>",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->state);
  /*
   * Check to make sure a CM transition didn't put us into an error
   * state.
   */
  switch (conn->state) {
  case TS_SDP_CONN_ST_ERROR_CQ:
  case TS_SDP_CONN_ST_ERROR_CM:
    /*
     * delete connection, fail to interface.
     */

    break;
  case TS_SDP_CONN_ST_REQ_RECV:

    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_REP_SENT);

    result = tsSdpPostMsgHelloAck(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	       "STRM: Failed to ack connection/hello request. <%d>", result);

      goto failed;
    } /* if */

    break;
  default:
    /*
     * Initiate disconnect, fail to gateway, mark connection.
     */

    break;
  } /* switch */

  return 0;
failed:
  result = tsSdpSockDrop(conn);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  return 0;
} /* tsSdpConnAccept */

/* ========================================================================= */
/*..tsSdpConnReject -- callback to reject an active open */
tINT32 tsSdpConnReject
(
 tSDP_CONN conn
)
{
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_CONFIG,
	   "CONN: <%d> <%08x:%04x><%08x:%04x> open rejected <%04x>",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->state);
  /*
   * respond to the remote connection manager with a REQ_REJ
   */
  result = tsSdpCmReject(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Connection manager reject request failed. <%d>", result);
  } /* if */

  return 0;
} /* tsSdpConnReject */

/* ========================================================================= */
/*..tsSdpConnConfirm -- callback to confirm accepeted passive open */
tINT32 tsSdpConnConfirm
(
 tSDP_CONN conn
)
{
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "CONN: <%d> <%08x:%04x><%08x:%04x> open confirmed <%04x>",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->state);

  TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_RTU_SENT);
  /*
   * post receive buffers.
   */
  result = tsSdpRecvPost(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Error posting <%d> receive buffers. <%d>",
	     result, conn->recv_max);
    goto error;
  } /* if */
  /*
   * respond to the remote connection manager with a RTU
   */
  result = tsSdpCmConfirm(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Connection manager confirm request failed. <%d>", result);
    goto error;
  } /* if */

  return 0;
error:
  return result;
} /* tsSdpConnConfirm */

/* ========================================================================= */
/*..tsSdpConnFailed - callback to notify accepeted open of a failure */
tINT32 tsSdpConnFailed
(
 tSDP_CONN conn
)
{
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * finalize connection acceptance.
   */
  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "CONN: <%d> <%08x:%04x><%08x:%04x> open failed <%04x>",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->state);
  /*
   * respond to the remote connection manager with a REP_REJ
   */
  result = tsSdpCmFailed(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Connection manager failed request failed. <%d>", result);
  } /* if */

  return 0;
} /* tsSdpConnFailed */

/* ========================================================================= */
/*..tsSdpConnClose -- callback to accept an active close */
tINT32 tsSdpConnClose
(
 tSDP_CONN conn
)
{
  tSDP_BUFF buff;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "CONN: <%d> <%08x:%04x><%08x:%04x> close request <%04x>",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->state);
  /*
   * close buffered data transmission space
   */
  conn->send_buf = 0;
  /*
   * Generate a Disconnect message, and mark self as disconnecting.
   */
  switch (conn->state) {
  case TS_SDP_CONN_ST_REP_SENT:
    /*
     * clear out the sent HelloAck message
     */
    buff = tsSdpBuffPoolGetHead(&conn->send_post);
    if (NULL == buff) {

      TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: ESTABLISHED, hello ack missing in send pool.");
    } /* if */
    else {

      result = tsSdpBuffMainPut(buff);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));
    } /* else */
    /*
     * fall through
     */
  case TS_SDP_CONN_ST_RTU_SENT:
    /*
     * wait for established before attempting to send. this way we'll
     * reduce the number of state permutations.
     */
    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_DIS_PEND_1);

    break;
  case TS_SDP_CONN_ST_ESTABLISHED:
    /*
     * Send a disconnect message
     */
    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_DIS_PEND_1);

    result = tsSdpSendCtrlDisconnect(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "STRM: Error sending disconnect request. <%d>", result);
      goto error;
    } /* if */

    break;
  default:
    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	     "STRM: Incorrect connection state <%04x> for disconnect request.",
	     conn->state);
    result = -EBADE;
    goto error;
  } /* switch */

  return 0;
error:
  /*
   * abortive close.
   */
  TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_ERROR_STRM);

  result = tsSdpCmDisconnect(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
             "STRM: Error posting CM disconnect. <%d>", result);
  } /* if */

  return result;
} /* tsSdpConnClose */

/* ========================================================================= */
/*..tsSdpConnClosing -- callback to confirm a passive close */
tINT32 tsSdpConnClosing
(
 tSDP_CONN conn
)
{
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "CONN: <%d> <%08x:%04x><%08x:%04x> close confirm request <%04x>",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->state);
  /*
   * close buffered data transmission space
   */
  conn->send_buf = 0;
  /*
   * Generate a response Disconnect message, and mark self as such.
   */
  switch (conn->state) {
  case TS_SDP_CONN_ST_DIS_RECV_1:
    /*
     * Change state, and send a disconnect request
     */
    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_DIS_PEND_2);

    result = tsSdpSendCtrlDisconnect(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "STRM: Error posting disconnect request. <%d>", result);
      goto error;
    } /* if */
    break;
  default:

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	     "STRM: State <%04x> error for disconnect confirmation.",
	     conn->state);
    result = -EBADE;
    goto error;
  } /* switch */

  return 0;
error:
  /*
   * abortive close.
   */
  TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_ERROR_STRM);

  result = tsSdpCmDisconnect(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
             "STRM: Error posting CM disconnect. <%d>", result);
  } /* if */

  return result;
} /* tsSdpConnClosing */

/* ========================================================================= */
/*..tsSdpConnAbort -- callback to accept an active abort */
tINT32 tsSdpConnAbort
(
 tSDP_CONN conn
)
{
  tINT32 result = 0;

  TS_CHECK_NULL(conn, -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "CONN: <%d> <%08x:%04x><%08x:%04x> abort request <%04x>",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->state);

  switch (conn->state) {

  case TS_SDP_CONN_ST_DIS_SENT_1: /* IBTA v1.1 spec A4.5.3.2 */
    /*
     * clear the pending control buffer.
     */
    result = tsSdpGenericTableClear(&conn->send_ctrl);
    TS_EXPECT(MOD_STRM_TCP, !(0 > result));
    /*
     * fall through
     */
  case TS_SDP_CONN_ST_DIS_SEND_1: /* don't touch control queue, diconnect
				     message may still be queued. */

    result = tsSdpGenericTableClear(&conn->send_queue);
    TS_EXPECT(MOD_STRM_TCP, !(0 > result));
    /*
     * post abort
     */
    result = tsSdpSendCtrlAbort(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "STRM: Error posting Abort message. <%d>", result);
      goto error;
    } /* if */
    /*
     * state change, no more STRM interface calls from here on out.
     */
    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_ERROR_STRM);

    break;
  case TS_SDP_CONN_ST_CLOSED:
  case TS_SDP_CONN_ST_ERROR_CM:
  case TS_SDP_CONN_ST_TIME_WAIT_2:
    /*
     * no more CM callbacks will be made.
     */
    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_CLOSED);

    break;
  case TS_SDP_CONN_ST_TIME_WAIT_1:
    /*
     * waiting for idle.
     */
    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_TIME_WAIT_2);

    break;
  case TS_SDP_CONN_ST_REQ_PATH:
    /*
     * cancel address resolution
     */
#if 0
    /*
     * instead of canceling allow the path completion to determine that
     * the socket has moved to an error state.
     */
    result = tsIp2prPathRecordCancel(conn->plid);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
#endif

    result = tsSdpSockDrop(conn);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
    /*
     * fall through
     */
  case TS_SDP_CONN_ST_REQ_SENT:
  case TS_SDP_CONN_ST_REP_SENT:
    /*
     * outstanding CM request. Mark it in error, and CM completion needs
     * to complete the closing.
     */
    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_ERROR_STRM);

    break;
  case TS_SDP_CONN_ST_ESTABLISHED: /* protocol errors and others. */
  case TS_SDP_CONN_ST_DIS_RECV_1:  /* Abort msg after disconnect msg */
  case TS_SDP_CONN_ST_DIS_PEND_1:  /* data recv after send/receive close */
  case TS_SDP_CONN_ST_ERROR_CQ:    /* begin CM level disconnect */

    /*
     * post abort
     */
    goto abort;
  default:
    /*
     * post abort
     */
    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
             "STRM: Unexpected connection state <%04x> for abort",
	     conn->state);
    goto error;
  } /* switch */

  return 0;
abort:
error:
  /*
   * abortive close.
   */
  TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_ERROR_STRM);

  result = tsSdpCmDisconnect(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
             "STRM: Error posting CM disconnect. <%d>", result);
  } /* if */

  return 0;
} /* tsSdpConnAbort */
/* --------------------------------------------------------------------- */
/*                                                                       */
/* SDP INET (public functions)                                           */
/*                                                                       */
/* --------------------------------------------------------------------- */

/* ========================================================================= */
/*..tsSdpSockConnect -- callback to begin passive open */
tINT32 tsSdpSockConnect
(
 tSDP_CONN accept_conn
)
{
  tSDP_CONN listen_conn;
  struct sock *listen_sk;
  struct sock *accept_sk;
  tINT32 result;

  TS_CHECK_NULL(accept_conn, -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d> <%08x:%04x><%08x:%04x> passive open",
	   accept_conn->hashent, accept_conn->src_addr, accept_conn->src_port,
	   accept_conn->dst_addr, accept_conn->dst_port);
  /*
   * first find a listening connection
   */
  listen_conn = tsSdpInetListenLookup(accept_conn->src_addr,
				      accept_conn->src_port);
  if (NULL == listen_conn) {
    /*
     * no connection, reject
     */
    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_CONFIG,
	     "STRM: no socket listening for connection. <%08x:%04x>",
	     accept_conn->src_addr, accept_conn->src_port);
    result = -ECONNREFUSED;
    goto error;
  } /* if */

  TS_SDP_CONN_LOCK(listen_conn);
  /*
   * check backlog
   */
  listen_sk = listen_conn->sk;
  accept_sk = accept_conn->sk;

  if (listen_conn->backlog_cnt > listen_conn->backlog_max) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_CONFIG,
	     "STRM: Listen backlog <%d> too big to accept new connection.",
	     listen_conn->backlog_cnt);
    result = -ECONNREFUSED;
    goto error;
  } /* if */

  result = tsSdpInetPortInherit(listen_conn, accept_conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "CREATE: failed to inherit port from listen. <%d>", result);
    result = -EFAULT;
    goto error;
  } /* if */
  /*
   * insert accept socket into listen sockets list.
   * TODO: needs to be a FIFO not a LIFO, as is now.
   */
  accept_conn->istate   = TS_SDP_SOCK_ST_ACCEPTING;

  TS_SDP_OS_SK_INET_NUM(accept_sk)       = accept_conn->src_port;
  TS_SDP_OS_SK_INET_SPORT(accept_sk)     = htons(accept_conn->src_port);
  TS_SDP_OS_SK_INET_RCV_SADDR(accept_sk) = htonl(accept_conn->src_addr);
  TS_SDP_OS_SK_INET_SADDR(accept_sk)     = htonl(accept_conn->src_addr);
  TS_SDP_OS_SK_INET_DADDR(accept_sk)     = htonl(accept_conn->dst_addr);
  TS_SDP_OS_SK_INET_DPORT(accept_sk)     = htons(accept_conn->dst_port);
  /*
   * relevant options, and others... TCP does a full copy, I'd like to
   * know what I'm inheriting.
   */
  TS_SDP_OS_SK_LINGERTIME(accept_sk)   = TS_SDP_OS_SK_LINGERTIME(listen_sk);
  TS_SDP_OS_SK_RCVLOWAT(accept_sk)     = TS_SDP_OS_SK_RCVLOWAT(listen_sk);
  TS_SDP_OS_SK_DEBUG(accept_sk)        = TS_SDP_OS_SK_DEBUG(listen_sk);
  TS_SDP_OS_SK_LOCALROUTE(accept_sk)   = TS_SDP_OS_SK_LOCALROUTE(listen_sk);
  TS_SDP_OS_SK_SNDBUF(accept_sk)       = TS_SDP_OS_SK_SNDBUF(listen_sk);
  TS_SDP_OS_SK_RCVBUF(accept_sk)       = TS_SDP_OS_SK_RCVBUF(listen_sk);
  TS_SDP_OS_SK_NO_CHECK(accept_sk)     = TS_SDP_OS_SK_NO_CHECK(listen_sk);
  TS_SDP_OS_SK_PRIORITY(accept_sk)     = TS_SDP_OS_SK_PRIORITY(listen_sk);
  TS_SDP_OS_SK_RCVTSTAMP(accept_sk)    = TS_SDP_OS_SK_RCVTSTAMP(listen_sk);
  TS_SDP_OS_SK_RCVTIMEOUT(accept_sk)   = TS_SDP_OS_SK_RCVTIMEOUT(listen_sk);
  TS_SDP_OS_SK_SNDTIMEOUT(accept_sk)   = TS_SDP_OS_SK_SNDTIMEOUT(listen_sk);
  TS_SDP_OS_SK_REUSE(accept_sk)        = TS_SDP_OS_SK_REUSE(listen_sk);
  TS_SDP_OS_SK_BOUND_IF(accept_sk)     = TS_SDP_OS_SK_BOUND_IF(listen_sk);

  TS_SDP_OS_SK_USERLOCKS_SET(accept_sk,
			     (TS_SDP_OS_SK_USERLOCKS_GET(listen_sk) &
			      ~SOCK_BINDPORT_LOCK));

#ifdef TS_KERNEL_2_6
  accept_sk->sk_flags     = ((SOCK_URGINLINE|SOCK_LINGER|SOCK_BROADCAST) &
			     listen_sk->sk_flags);
#else /* TS_KERNEL_2_6 */
  accept_sk->linger       = listen_sk->linger;
  accept_sk->urginline    = listen_sk->urginline;
  accept_sk->broadcast    = listen_sk->broadcast;
  accept_sk->bsdism       = listen_sk->bsdism;
#endif /* TS_KERNEL_2_6 */

  accept_conn->src_zthresh = listen_conn->src_zthresh;
  accept_conn->snk_zthresh = listen_conn->snk_zthresh;
  accept_conn->nodelay     = listen_conn->nodelay;

  result = tsSdpConnAccept(accept_conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "SOCK: Error <%d> accepting conn <%08x:%04x><%08x:%04x>",
	     result,
	     accept_conn->src_addr, accept_conn->src_port,
	     accept_conn->dst_addr, accept_conn->dst_port);
    goto error;
  } /* if */

  result = tsSdpInetAcceptQueuePut(listen_conn, accept_conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "CREATE: Error adding socket to accept queue. <%d>", result);
    result = -EFAULT;
    goto error;
  } /* if */

  tsSdpInetWakeRecv(listen_conn,  0);

  TS_SDP_CONN_UNLOCK(listen_conn);
  TS_SDP_CONN_PUT(listen_conn); /* ListenLookup reference. */

  return 0;
error:
  return result;
} /* tsSdpSockConnect */

/* ========================================================================= */
/*..tsSdpSockAccept -- callback to accept an active open */
tINT32 tsSdpSockAccept
(
 tSDP_CONN conn
)
{
  struct sock *sk;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d> <%08x:%04x><%08x:%04x> <%04x> open accepted",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->istate);

  sk = conn->sk;
  /*
   * only reason not to confirm is if the connection state has changed
   * from under us, and the change wasn't followed up with a gateway
   * Abort(), which it should have been.
   */
  if (TS_SDP_SOCK_ST_CONNECT == conn->istate) {
    /*
     * finalize connection acceptance.
     */
    result = tsSdpConnConfirm(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_FATAL,
	       "STRM: open accepted but confirmation failed. <%d>", result);
      goto error;
    } /* if */
    else {
      /*
       * wake the accepting connection
       */
      TS_SDP_OS_SK_SOCKET(sk)->state = SS_CONNECTED;
      conn->istate      = TS_SDP_SOCK_ST_ESTABLISHED;
      conn->send_buf    = TS_SDP_INET_SEND_WIN;

      TS_SDP_OS_SK_INET_SADDR(sk)     = htonl(conn->src_addr);
      TS_SDP_OS_SK_INET_RCV_SADDR(sk) = htonl(conn->src_addr);
      /*
       * write/read ready. (for those waiting on just one...)
       */
      tsSdpInetWakeSend(conn);
      tsSdpInetWakeRecv(conn, 0);
    } /* else */
  } /* if */
  else {
    /*
     * fail this connection
     */
    result = tsSdpConnFailed(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_FATAL,
	       "STRM: open accepted but confirmation failed. <%d>", result);
      goto error;
    } /* if */

    TS_SDP_OS_CONN_SET_ERR(conn, EPROTO);
    conn->istate = TS_SDP_SOCK_ST_ERROR;

    goto drop;
  } /* else */

  return 0;
error:
  TS_SDP_OS_CONN_SET_ERR(conn, result);
  conn->istate = TS_SDP_SOCK_ST_ERROR;
drop:
  tsSdpInetWakeError(conn);

  TS_SDP_CONN_PUT(conn); /* CM sk reference */

  return result;
} /* tsSdpSockAccept */

/* ========================================================================= */
/*..tsSdpSockReject -- callback to reject an active open */
tINT32 tsSdpSockReject
(
 tSDP_CONN conn,
 tINT32    error
)
{
  tINT32 result;

  TS_CHECK_NULL(conn,   -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d> <%08x:%04x><%08x:%04x> <%04x> open rejected <%d>",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->istate, error);
  /*
   * the connection has been rejected, move to error, and notify anyone
   * waiting of the state change.
   */
  TS_SDP_OS_CONN_SET_ERR(conn, error);
  TS_SDP_OS_SK_SOCKET(conn->sk)->state = SS_UNCONNECTED;
  conn->istate   = TS_SDP_SOCK_ST_ERROR;
  conn->shutdown = TS_SDP_SHUTDOWN_MASK;

  result = tsSdpBuffKvecCancelAll(conn, (0 - error));
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Error <%d> canceling <%d> outstanding read IOCBs", result);
  } /* if */

  tsSdpInetWakeError(conn);

  TS_SDP_CONN_PUT(conn); /* CM reference */

  return 0;
} /* tsSdpSockReject */

/* ========================================================================= */
/*..tsSdpSockConfirm -- callback to confirm accepeted passive open */
tINT32 tsSdpSockConfirm
(
 tSDP_CONN conn
)
{
  tINT32 result = 0;

  TS_CHECK_NULL(conn,   -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d> <%08x:%04x><%08x:%04x> <%04x> open confirmed",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->istate);

  switch (conn->istate) {
  case TS_SDP_SOCK_ST_ACCEPTING:

    conn->istate    = TS_SDP_SOCK_ST_ACCEPTED;
    conn->send_buf  = TS_SDP_INET_SEND_WIN;

    break;
  case TS_SDP_SOCK_ST_ACCEPTED:

    conn->istate    = TS_SDP_SOCK_ST_ESTABLISHED;
    conn->send_buf  = TS_SDP_INET_SEND_WIN;

    tsSdpInetWakeSend(conn);

    break;
  default:

    conn->istate = TS_SDP_SOCK_ST_ERROR;

    tsSdpInetWakeError(conn);

    TS_SDP_CONN_PUT(conn); /* CM sk reference */

    result = -EPROTO;
  } /* switch */

  return result;
} /* tsSdpSockConfirm */

/* ========================================================================= */
/*..tsSdpSockFailed - callback to notify accepted open of a failure */
tINT32 tsSdpSockFailed
(
 tSDP_CONN conn,
 tINT32    error
)
{
  TS_CHECK_NULL(conn,   -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d> <%08x:%04x><%08x:%04x> <%04x> open failed <%d>",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->istate, error);
  /*
   * the connection has failed, move to error, and notify anyone
   * waiting of the state change.
   */
  TS_EXPECT(MOD_LNX_SDP, (TS_SDP_SOCK_ST_ACCEPTED == conn->istate));

  switch (conn->istate) {
  default:

    TS_SDP_OS_CONN_SET_ERR(conn, error);
    TS_SDP_OS_SK_SOCKET(conn->sk)->state = SS_UNCONNECTED;
    tsSdpInetWakeError(conn);
    /*
     * fall through
     */
  case TS_SDP_SOCK_ST_ACCEPTING:

    conn->istate   = TS_SDP_SOCK_ST_ERROR;
    conn->shutdown = TS_SDP_SHUTDOWN_MASK;
    break;
  } /* switch */

  TS_SDP_CONN_PUT(conn); /* CM reference */

  return 0;
} /* tsSdpSockFailed */

/* ========================================================================= */
/*..tsSdpSockClose -- callback to accept an active close */
tINT32 tsSdpSockClose
(
 tSDP_CONN conn
)
{
  tINT32 result;

  TS_CHECK_NULL(conn,   -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "STRM: <%d> <%08x:%04x><%08x:%04x>  <%04x> close request",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->istate);
  /*
   * wake the closing connection, and change state to close received.
   */
  switch (conn->istate) {
  case TS_SDP_SOCK_ST_ACCEPTING:
  case TS_SDP_SOCK_ST_ACCEPTED:
    /*
     * shift to close.
     */
    conn->istate  = TS_SDP_SOCK_ST_CLOSE;
    conn->shutdown |= TS_SDP_SHUTDOWN_RECV;

    break;
  default:

    conn->istate  = TS_SDP_SOCK_ST_CLOSE;
    conn->shutdown |= TS_SDP_SHUTDOWN_RECV;
    /*
     * cancel all outstanding read AIO's since there will be no more data
     * from the peer.
     */
    result = tsSdpBuffKvecReadCancelAll(conn, 0);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "STRM: Error <%d> canceling outstanding read IOCBs", result);
    } /* if */
    /*
     * async notification. POLL_HUP on full duplex close only.
     */
    tsSdpInetWakeGeneric(conn);
    sk_wake_async(conn->sk, 1, POLL_IN);

    break;
  } /* switch */

  return 0;
} /* tsSdpSockClose */

/* ========================================================================= */
/*..tsSdpSockClosing -- callback for a close confirmation */
tINT32 tsSdpSockClosing
(
 tSDP_CONN conn
)
{
  tINT32 result;

  TS_CHECK_NULL(conn,   -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d> <%08x:%04x><%08x:%04x>  <%04x> closing request",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->istate);
  /*
   * change state, finalize the close, and wake the closer.
   */
  TS_EXPECT(MOD_LNX_SDP, (TS_SDP_SOCK_ST_DISCONNECT == conn->istate));

  conn->send_buf  = 0;
  conn->istate    = TS_SDP_SOCK_ST_CLOSED;
  conn->shutdown |= TS_SDP_SHUTDOWN_RECV;
  /*
   * cancel all outstanding read AIO's since there will be no more data
   * from the peer.
   */
  result = tsSdpBuffKvecReadCancelAll(conn, 0);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Error <%d> canceling <%d> outstanding read IOCBs", result);
  } /* if */

  tsSdpInetWakeGeneric(conn);
  /*
   * async notification. POLL_HUP on full duplex close only.
   */
  sk_wake_async(conn->sk, 1, POLL_HUP);

  return 0;
} /* tsSdpSockClosing */

/* ========================================================================= */
/*..tsSdpSockAbort -- abortive close notification */
tINT32 tsSdpSockAbort
(
 tSDP_CONN conn
)
{
  tINT32 result;

  TS_CHECK_NULL(conn,   -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d> <%08x:%04x><%08x:%04x>  <%04x> abort request",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->istate);
  /*
   * wake the connection in case it's doing anything, and mark it as
   * closed. Leave data in their buffers so the user can blead it
   * dry.
   */
  switch (conn->istate) {
  case TS_SDP_SOCK_ST_CONNECT:
  case TS_SDP_SOCK_ST_ACCEPTING:
  case TS_SDP_SOCK_ST_ACCEPTED:
    TS_SDP_OS_CONN_SET_ERR(conn, ECONNREFUSED);
    break;
  case TS_SDP_SOCK_ST_CLOSE:
    TS_SDP_OS_CONN_SET_ERR(conn, EPIPE);
    break;
  default:
    TS_SDP_OS_CONN_SET_ERR(conn, ECONNRESET);
    break;
  } /* switch */

  conn->send_buf = 0;
  conn->istate   = TS_SDP_SOCK_ST_ERROR;
  conn->shutdown = TS_SDP_SHUTDOWN_MASK;
  /*
   * cancel all outstanding IOCBs
   */
  result = tsSdpBuffKvecCancelAll(conn, -ECONNRESET);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Error <%d> canceling all outstanding IOCBs", result);
  } /* if */

  tsSdpInetWakeError(conn);

  return 0;
} /* tsSdpSockAbort */

/* ========================================================================= */
/*..tsSdpSockDrop -- drop SDP protocol reference to socket */
tINT32 tsSdpSockDrop
(
 tSDP_CONN conn
)
{
  tINT32 result;

  TS_CHECK_NULL(conn,   -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d> <%08x:%04x><%08x:%04x>  <%04x> drop request",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->istate);
  /*
   * Lock Note:
   * This function should only be called from the process context,
   * because AcceptQueueRemove will take the process context connection
   * lock. Generally this function is only called as a result of CM
   * state transitions.
   */
  switch (conn->istate) {
  case TS_SDP_SOCK_ST_ACCEPTING:
    /*
     * pull the listen sockets accept queue.
     */
    result = tsSdpInetAcceptQueueRemove(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	       "STRM: Error removing socket from accept queue. <%d>", result);
    } /* if */

    conn->istate = TS_SDP_SOCK_ST_CLOSED;

    break;
  case TS_SDP_SOCK_ST_CLOSING:

    conn->istate = TS_SDP_SOCK_ST_CLOSED;
    tsSdpInetWakeGeneric(conn);

    break;
  default:
    /*
     * wake the connection in case it's doing anything, and mark it as
     * closed. Leave data in their buffers so the user can blead it
     * dry.
     */
    TS_SDP_OS_CONN_SET_ERR(conn, ECONNRESET);
    conn->istate = TS_SDP_SOCK_ST_ERROR;
    /*
     * cancel all outstanding IOCBs
     */
    result = tsSdpBuffKvecCancelAll(conn, -ECONNRESET);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "STRM: Error <%d> canceling all outstanding IOCBs", result);
    } /* if */

    tsSdpInetWakeError(conn);

    break;
  } /* switch */

  conn->send_buf = 0;

  TS_SDP_CONN_PUT(conn); /* CM sk reference */

  return 0;
} /* tsSdpSockDrop */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* SDP common (public functions)                                         */
/*                                                                       */
/* --------------------------------------------------------------------- */

/* ========================================================================= */
/*..tsSdpAbort -- intiate socket dropping. */
tINT32 tsSdpAbort
(
 tSDP_CONN conn
)
{
  tINT32 result;

  TS_CHECK_NULL(conn,   -EINVAL);
  /*
   * notify both halves of the wall that the connection is being aborted.
   */
  result = tsSdpSockAbort(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "ABORT: Error <%d> aborting inet half of connection.", result);
  } /* if */

  result = tsSdpConnAbort(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "ABORT: Error <%d> aborting protocol half of connection.",
	     result);
  } /* if */

  return 0;
} /* tsSdpAbort */
