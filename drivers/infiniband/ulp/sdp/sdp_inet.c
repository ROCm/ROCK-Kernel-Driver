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

  $Id: sdp_inet.c 32 2004-04-09 03:57:42Z roland $
*/

#include "sdp_main.h"
#include <linux/tcp.h>
#include <asm/ioctls.h>
/*
 * list of connections waiting for an incomming connection
 */
static tUINT32 _proto_family   = TS_SDP_DEV_PROTO;
static tINT32 _buff_min        = TS_SDP_BUFFER_COUNT_MIN;
static tINT32 _buff_max        = TS_SDP_BUFFER_COUNT_MAX;
static tINT32 _conn_size       = TS_SDP_DEV_SK_LIST_SIZE;

MODULE_AUTHOR("Libor Michalek");
MODULE_DESCRIPTION("InfiniBand SDP module");
MODULE_LICENSE("Dual BSD/GPL");

MODULE_PARM(_proto_family, "i");
MODULE_PARM(_buff_min, "i");
MODULE_PARM(_buff_max, "i");
MODULE_PARM(_conn_size, "i");

/*
 * socket structure relevant fields:
 *
 * struct sock {
 *    unsigned short     num;         (src port, host    byte order)
 *    __u16              sport;       (src port, network byte order)
 *    __u32              rcv_saddr;   (src addr, network byte order)
 *    __u32              saddr;       (src addr, network byte order)
 *    __u32              daddr;       (dst addr, network byte order)
 *    __u16              dport;       (dst port, network byte order)
 *    unsigned char      shutdown;    (mask of directional close)
 *    wait_queue_head_t *sleep;       (wait for event queue)
 *    int                wmem_queued; (send bytes outstanding)
 *    int                sndbuf;      (possible send bytes outstanding)
 *    unsigned long      lingertime;  (close linger time)
 *    volatile char      linger;      (close linger time valid)
 *    union {}           tp_info;     (cast for STRM/LNX CONN specific data)
 *    int                err;         (error propogation from GW to socket if)
 *    unsigned short     ack_backlog;     (current accept backlog)
 *    unsigned short     max_ack_backlog; (accept max backlog)
 * };
 */
/* --------------------------------------------------------------------- */
/*                                                                       */
/* Notification of significant events.                                   */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpInetWakeSend -- test, set, and notify socket of write space */
void tsSdpInetWakeSend
(
 tSDP_CONN conn
 )
{
  struct sock *sk = conn->sk;

  if (NULL != TS_SDP_OS_SK_SOCKET(sk) &&
      0 < test_bit(SOCK_NOSPACE, &(TS_SDP_OS_SK_SOCKET(sk))->flags) &&
      0 < __tsSdpInetWritable(conn)) {

    read_lock(&TS_SDP_OS_SK_CB_LOCK(sk));
    clear_bit(SOCK_NOSPACE, &(TS_SDP_OS_SK_SOCKET(sk))->flags);

    if (NULL != TS_SDP_OS_SK_SLEEP(sk) &&
	0 < waitqueue_active(TS_SDP_OS_SK_SLEEP(sk))) {

      wake_up_interruptible(TS_SDP_OS_SK_SLEEP(sk));
    } /* if */
    /*
     * test, clear, and notify. SOCK_ASYNC_NOSPACE
     */
    sk_wake_async(sk, 2, POLL_OUT);
    read_unlock(&TS_SDP_OS_SK_CB_LOCK(sk));
  } /* if */

  return;
} /* tsSdpInetWakeSend */

/* ========================================================================= */
/*..tsSdpInetWakeGeneric -- wake up a socket */
void tsSdpInetWakeGeneric
(
 tSDP_CONN conn
)
{
  struct sock *sk = conn->sk;

  if (NULL != sk) {

    read_lock(&TS_SDP_OS_SK_CB_LOCK(sk));

    if (NULL != TS_SDP_OS_SK_SLEEP(sk) &&
	waitqueue_active(TS_SDP_OS_SK_SLEEP(sk))) {

      wake_up_interruptible_all(TS_SDP_OS_SK_SLEEP(sk));
    } /* if */

    read_unlock(&TS_SDP_OS_SK_CB_LOCK(sk));
  } /* if */

  return;
} /* tsSdpInetWakeGeneric */

/* ========================================================================= */
/*..tsSdpInetWakeRecv -- wake up a socket for read */
void tsSdpInetWakeRecv
(
 tSDP_CONN conn,
 tINT32 len
)
{
  struct sock *sk = conn->sk;

  read_lock(&TS_SDP_OS_SK_CB_LOCK(sk));
  if (NULL != TS_SDP_OS_SK_SLEEP(sk)) {

    wake_up_interruptible(TS_SDP_OS_SK_SLEEP(sk));
  } /* if  */

  sk_wake_async(sk,1,POLL_IN);
  read_unlock(&TS_SDP_OS_SK_CB_LOCK(sk));

  return;
} /* tsSdpInetWakeRecv */

/* ========================================================================= */
/*..tsSdpInetWakeError -- wake up a socket for error */
void tsSdpInetWakeError
(
 tSDP_CONN conn
 )
{
  struct sock *sk = conn->sk;

  read_lock(&TS_SDP_OS_SK_CB_LOCK(sk));
  if (NULL != TS_SDP_OS_SK_SLEEP(sk)) {

    wake_up_interruptible(TS_SDP_OS_SK_SLEEP(sk));
  } /* if */

  sk_wake_async(sk,0,POLL_ERR);
  read_unlock(&TS_SDP_OS_SK_CB_LOCK(sk));

  return;
} /* tsSdpInetWakeError */

/* ========================================================================= */
/*..tsSdpInetWakeUrg -- wake up a socket for urgent data */
void tsSdpInetWakeUrg
(
 tSDP_CONN conn
 )
{
  struct sock *sk = conn->sk;
  /*
   * pid for SIGURG/SIGIO has been set. On positive send signal to
   * process, on negative send signal to processes group.
   */
  if (NULL != sk) {
#ifdef TS_KERNEL_2_6
    sk_send_sigurg(sk);
#else  /* TS_KERNEL_2_6 */

    if (0 != sk->proc) {

      if (0 < sk->proc) {

	kill_proc(sk->proc, SIGURG, 1);
      } /* if */
      else {

	kill_pg(-sk->proc, SIGURG, 1);
      } /* else */

      sk_wake_async(sk, 3, POLL_PRI);
    } /* if */
#endif /* TS_KERNEL_2_6 */
  } /* if */

  return;
} /* tsSdpInetWakeUrg */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* internal socket/handle managment functions                            */
/*                                                                       */
/* --------------------------------------------------------------------- */
#ifndef _TS_SDP_BREAK_INET
/* ========================================================================= */
/*.._tsSdpInetAbort -- abort an existing connection. */
static tINT32 _tsSdpInetAbort
(
  tSDP_CONN conn
)
{
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);

  conn->send_buf = 0;

  switch (conn->istate) {

  case TS_SDP_SOCK_ST_CONNECT:
  case TS_SDP_SOCK_ST_ACCEPTING:
  case TS_SDP_SOCK_ST_ACCEPTED:

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Unexpected abort state <%d> for socket. <%d>",
	     conn->istate, conn->pid);

  case TS_SDP_SOCK_ST_ESTABLISHED:
  case TS_SDP_SOCK_ST_CLOSE:
  case TS_SDP_SOCK_ST_DISCONNECT:
  case TS_SDP_SOCK_ST_CLOSING:

    result = tsSdpAbort(conn);
    if (0 > result) {

      result = -ECONNABORTED;
      TS_SDP_OS_CONN_SET_ERR(conn, ECONNABORTED);
      conn->istate = TS_SDP_SOCK_ST_ERROR;
    } /* if */

    break;
  case TS_SDP_SOCK_ST_LISTEN:
  case TS_SDP_SOCK_ST_CLOSED:
  case TS_SDP_SOCK_ST_ERROR:

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Unhandled abort state <%d> for socket. <%d>",
	     conn->istate, conn->pid);

    conn->istate   = TS_SDP_SOCK_ST_ERROR;
    result = -EINVAL;
    break;
  default:

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Unknown abort state <%d> for socket. <%d>",
	     conn->istate, conn->pid);

    conn->istate   = TS_SDP_SOCK_ST_ERROR;
    result = -EINVAL;
    break;
  } /* switch */

  return result;
} /* _tsSdpInetAbort */
#endif
/* ========================================================================= */
/*.._tsSdpInetPostDisconnect -- disconnect a connection. */
static tINT32 _tsSdpInetPostDisconnect
(
 tSDP_CONN conn
)
{
  tINT32 result = 0;

  TS_CHECK_NULL(conn, -EINVAL);

  switch (conn->istate) {

  case TS_SDP_SOCK_ST_CONNECT:

    result = tsSdpAbort(conn);
    if (0 > result) {

      result = -ECONNABORTED;
      TS_SDP_OS_CONN_SET_ERR(conn, ECONNABORTED);
      conn->istate = TS_SDP_SOCK_ST_ERROR;
    } /* if */

    break;
  case TS_SDP_SOCK_ST_ESTABLISHED:
  case TS_SDP_SOCK_ST_ACCEPTED:

    conn->istate   = TS_SDP_SOCK_ST_DISCONNECT;
    result = tsSdpConnClose(conn);
    if (0 > result) {

      result = -ECONNABORTED;
      TS_SDP_OS_CONN_SET_ERR(conn, ECONNABORTED);
      conn->istate = TS_SDP_SOCK_ST_ERROR;
    } /* if */

    break;
  case TS_SDP_SOCK_ST_CLOSE:

    conn->istate   = TS_SDP_SOCK_ST_CLOSING;
    result = tsSdpConnClosing(conn);
    if (0 > result) {

      result = -ECONNABORTED;
      TS_SDP_OS_CONN_SET_ERR(conn, ECONNABORTED);
      conn->istate = TS_SDP_SOCK_ST_ERROR;
    } /* if */

    break;
  case TS_SDP_SOCK_ST_ACCEPTING:
  case TS_SDP_SOCK_ST_DISCONNECT:
  case TS_SDP_SOCK_ST_CLOSING:
    /*
     * nothing to do, and somewhat unexpected state
     */
    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Unexpected disconnect state <%04x> for socket. <%d>",
	     conn->istate, conn->pid);
    break;
  case TS_SDP_SOCK_ST_LISTEN:
  case TS_SDP_SOCK_ST_CLOSED:
  case TS_SDP_SOCK_ST_ERROR:

    break;
  default:

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Unknown disconnect state <%04x> for socket. <%d>",
	     conn->istate, conn->pid);

    conn->istate   = TS_SDP_SOCK_ST_ERROR;
    result = -EINVAL;
    break;
  } /* switch */

  return result;
} /* _tsSdpInetPostDisconnect */

#ifndef _TS_SDP_BREAK_INET
/* --------------------------------------------------------------------- */
/*                                                                       */
/* Linux SOCKET interface, module specific functions                     */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsSdpInetRelease -- release/close a socket */
static tINT32 _tsSdpInetRelease
(
 struct socket *sock
)
{
  tSDP_CONN conn;
  struct sock *sk;
  tINT32 result;
  long timeout;
  tINT32 flags;

  TS_CHECK_NULL(sock, -EINVAL);
  TS_CHECK_NULL(sock->file, -EINVAL);

  if (NULL != sock->sk) {

    sk   = sock->sk;
    conn = TS_SDP_GET_CONN(sk);

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	     "SOCK: <%d> release <%08x:%04x> <%08x:%04x> <%04x> <%d:%d:%d:%d>",
	     conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	     conn->dst_port, conn->istate,
	     TS_SDP_OS_SK_LINGER(sk), TS_SDP_OS_SK_LINGERTIME(sk),
	     conn->byte_strm, conn->src_recv);
    /*
     * clear out sock, so we only do this once.
     */
    sock->sk = NULL;

    TS_SDP_CONN_LOCK(conn);
    conn->shutdown = TS_SDP_SHUTDOWN_MASK;

    if (TS_SDP_SOCK_ST_LISTEN == conn->istate) {
      /*
       * stop listening
       */
      result = tsSdpInetListenStop(conn);
      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "SOCK: Error <%d> while releasing listen socket <%d>",
		 result, conn->pid);
      } /* if */
    } /* if */
    else {
      /*
       * get blocking nature of the socket.
       */
      if (sock->file) {
        flags = (0 < (sock->file->f_flags & O_NONBLOCK)) ? MSG_DONTWAIT : 0;
      } else {
        flags = 0;
      }
      /*
       * If there is data in the receive queue, flush it, and consider
       * this an abort. Otherwise consider this a gracefull close.
       */
      if (0 < tsSdpBuffPoolSize(&conn->recv_pool) ||
	  0 < conn->src_recv ||
	  (0 < TS_SDP_OS_SK_LINGER(sk) &&
	   0 == TS_SDP_OS_SK_LINGERTIME(sk))) {
	/*
	 * abort.
	 */
	result = _tsSdpInetAbort(conn);
	if (0 > result) {

	  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		   "SOCK: Error <%d> while aborting socket <%d> on release.",
		   result, conn->pid);
	} /* if */
      } /* if */
      else {
	/*
	 * disconnect. (state dependant)
	 */
	result = _tsSdpInetPostDisconnect(conn);
	if (0 > result) {

	  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		   "SOCK: Error <%d> while disconnecting <%d> on release.",
		   result, conn->pid);
	} /* if */
	else {
	  /*
	   * Skip lingering/canceling if non-blocking and not exiting.
	   */
	  if (0 == (MSG_DONTWAIT & flags) ||
	      0 < (PF_EXITING & current->flags)) {
	    /*
	     * Wait if linger is set and process is not exiting.
	     */
	    if (0 < TS_SDP_OS_SK_LINGER(sk) &&
		!(PF_EXITING & current->flags)) {

	      DECLARE_WAITQUEUE(wait, current);
	      timeout = TS_SDP_OS_SK_LINGERTIME(sk);

	      add_wait_queue(TS_SDP_OS_SK_SLEEP(sk), &wait);
	      set_current_state(TASK_INTERRUPTIBLE);

	      while (0 < timeout &&
		     0 == (TS_SDP_ST_MASK_CLOSED & conn->istate)) {

		TS_SDP_CONN_UNLOCK(conn);
		timeout = schedule_timeout(timeout);
		TS_SDP_CONN_LOCK(conn);

		if (signal_pending(current)) {

		  break;
		} /* if */
	      } /* while */

	      set_current_state(TASK_RUNNING);
	      remove_wait_queue(TS_SDP_OS_SK_SLEEP(sk), &wait);
	    } /* if (linger) */
#if 0
	    /*
	     * On a blocking socket, if still draining after linger,
	     * Cancel write and close again to force closing the
	     * connection.
	     */
	    if (0 < (TS_SDP_ST_MASK_DRAIN & conn->istate)) {

	      result = tsSdpBuffKvecWriteCancelAll(conn, -ECANCELED);
	      TS_EXPECT(MOD_LNX_SDP, !(0 > result));

	      result = _tsSdpInetPostDisconnect(conn);
	      TS_EXPECT(MOD_LNX_SDP, !(0 > result));
	    } /* if */
#endif
	  } /* if (blocking) */

	} /* else (disconnect error) */
      } /* else (abortive) */
    } /* else (listen) */

    if (0 < (TS_SDP_ST_MASK_CLOSED & conn->istate)) {
      /*
       * pass
       */
    } /* if */
    else {


    } /* else */
    /*
     * finally drop socket reference. (socket API reference)
     */
    sock_orphan(sk);
    TS_SDP_CONN_UNLOCK(conn);
    TS_SDP_CONN_PUT(conn);
  } /* if */
  else {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	     "SOCK: empty release <%d:%d> <%08x>", sock->type, sock->state,
	     sock->flags);
  } /* else */

  return 0;
} /* _tsSdpInetRelease */

/* ========================================================================= */
/*.._tsSdpInetBind -- bind a socket to an address/interface */
static tINT32 _tsSdpInetBind
(
 struct socket   *sock,
 struct sockaddr *uaddr,
 tINT32           size
)
{
  struct sockaddr_in *addr = (struct sockaddr_in *)uaddr;
  struct sock        *sk;
  tSDP_CONN           conn;
  tINT32              result;
  tINT32              addr_result = RTN_UNSPEC;
  tUINT16             bind_port;

  TS_CHECK_NULL(sock, -EINVAL);
  TS_CHECK_NULL(sock->sk, -EINVAL);
  TS_CHECK_NULL(uaddr, -EINVAL);

  sk   = sock->sk;
  conn = TS_SDP_GET_CONN(sk);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d> bind operation <%d> <%d> <%08x:%04x>",
	   conn->hashent, conn->pid, addr->sin_family,
	   addr->sin_addr.s_addr, addr->sin_port);

  if (size < sizeof(struct sockaddr_in)) {

    return -EINVAL;
  } /* if */

  if (_proto_family != addr->sin_family &&
      AF_INET       != addr->sin_family &&
      AF_UNSPEC     != addr->sin_family) {

    return -EAFNOSUPPORT;
  } /* if */
  /*
   * Basically we're OK with INADDR_ANY or a local interface (TODO: loopback)
   */
  if (INADDR_ANY != addr->sin_addr.s_addr) {
    /*
     * make sure we have a valid binding address
     */
    addr_result = inet_addr_type(addr->sin_addr.s_addr);

    if (TS_SDP_OS_SK_INET_FREEBIND(sk) == 0 &&
	RTN_LOCAL != addr_result &&
	RTN_MULTICAST != addr_result &&
	RTN_BROADCAST != addr_result) {

      return -EADDRNOTAVAIL;
    } /* if */
  } /* if */
  /*
   * check bind permission for low ports.
   */
  bind_port = ntohs(addr->sin_port);
  if (0 < bind_port &&
      bind_port < PROT_SOCK &&
      0 == capable(CAP_NET_BIND_SERVICE)) {

    return  -EACCES;
  } /* if */
  /*
   * socket checks.
   */
  TS_SDP_CONN_LOCK(conn);

  if (TS_SDP_SOCK_ST_CLOSED != conn->istate ||
      0 < conn->src_port) {

    result = -EINVAL;
    goto done;
  } /* if */

  conn->src_addr = ntohl(addr->sin_addr.s_addr);

  if (RTN_MULTICAST == addr_result ||
      RTN_BROADCAST == addr_result) {

    conn->src_addr = 0;
  } /* if */

  result = tsSdpInetPortGet(conn, bind_port);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "SOCK: Error getting port during bind. <%d>", result);

    conn->src_addr = 0;

    goto done;
  } /* if */

  if (INADDR_ANY != conn->src_addr) {

    TS_SDP_OS_SK_USERLOCKS_SET(sk, SOCK_BINDADDR_LOCK);
  } /* if */

  if (0 < bind_port) {

    TS_SDP_OS_SK_USERLOCKS_SET(sk, SOCK_BINDADDR_LOCK);
  } /* if */

  TS_SDP_OS_SK_INET_RCV_SADDR(sk) = htonl(conn->src_addr);
  TS_SDP_OS_SK_INET_SADDR(sk)     = htonl(conn->src_addr);

  TS_SDP_OS_SK_INET_NUM(sk)       = conn->src_port;
  TS_SDP_OS_SK_INET_SPORT(sk)     = htons(conn->src_port);
  TS_SDP_OS_SK_INET_DADDR(sk)     = 0;
  TS_SDP_OS_SK_INET_DPORT(sk)     = 0;

  result = 0;
done:
  TS_SDP_CONN_UNLOCK(conn);
  return result;
} /* _tsSdpInetBind */

/* ========================================================================= */
/*.._tsSdpInetConnect -- connect a socket to a remote address */
static tINT32 _tsSdpInetConnect
(
 struct socket   *sock,
 struct sockaddr *uaddr,
 tINT32           size,
 tINT32           flags
)
{
  struct sockaddr_in *addr = (struct sockaddr_in *)uaddr;
  struct sock        *sk;
  tSDP_CONN           conn;
  long                timeout;
  tINT32              result;

  TS_CHECK_NULL(sock, -EINVAL);
  TS_CHECK_NULL(sock->sk, -EINVAL);
  TS_CHECK_NULL(uaddr, -EINVAL);

  sk   = sock->sk;
  conn = TS_SDP_GET_CONN(sk);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d> connect operation <%d> <%d> <%08x:%04x>",
	   conn->hashent, conn->pid, addr->sin_family,
	   addr->sin_addr.s_addr, addr->sin_port);

  if (size < sizeof(struct sockaddr_in)) {

    return -EINVAL;
  } /* if */

  if (_proto_family != addr->sin_family &&
      AF_INET       != addr->sin_family &&
      AF_UNSPEC     != addr->sin_family) {

    return -EAFNOSUPPORT;
  } /* if */


  if (MULTICAST(addr->sin_addr.s_addr)   ||
      BADCLASS(addr->sin_addr.s_addr)    ||
      ZERONET(addr->sin_addr.s_addr)     ||
      LOCAL_MCAST(addr->sin_addr.s_addr) ||
      INADDR_ANY == addr->sin_addr.s_addr) {

    return -EINVAL;
  } /* if */
  /*
   * lock socket
   */
  TS_SDP_CONN_LOCK(conn);

  switch (sock->state) {
  case SS_UNCONNECTED:

    if (0 == (TS_SDP_ST_MASK_CLOSED & conn->istate)) {

      result = -EISCONN;
      goto done;
    } /* if */

    if (0 == conn->src_port) {

      result = tsSdpInetPortGet(conn, 0);
      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
		 "SOCK: Error getting port during connect. <%d>", result);
	goto done;
      } /* if */

      TS_SDP_OS_SK_INET_NUM(sk)   = conn->src_port;
      TS_SDP_OS_SK_INET_SPORT(sk) = htons(conn->src_port);
    } /* if */

    TS_SDP_OS_CONN_SET_ERR(conn, 0);

    sock->state  = SS_CONNECTING;
    conn->istate = TS_SDP_SOCK_ST_CONNECT;

    conn->dst_addr = ntohl(addr->sin_addr.s_addr);
    conn->dst_port = ntohs(addr->sin_port);

    TS_SDP_CONN_HOLD(conn); /* CM reference */
    /*
     * close, allow connection completion notification.
     */
    set_bit(SOCK_NOSPACE, &sock->flags);

    result = tsSdpConnConnect(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	       "SOCK: Error connecting to gateway interface. <%d>", result);

      conn->dst_addr = 0;
      conn->dst_port = 0;

      sock->state  = SS_UNCONNECTED;
      conn->istate = TS_SDP_SOCK_ST_CLOSED;

      TS_SDP_CONN_PUT(conn); /* CM reference */

      goto done;
    } /* if */

    TS_SDP_OS_SK_INET_DADDR(sk)      = htonl(conn->dst_addr);
    TS_SDP_OS_SK_INET_DPORT(sk)      = htons(conn->dst_port);

    result = -EINPROGRESS;
    break;
  case SS_CONNECTING:

    result = -EALREADY;
    break;
  case SS_CONNECTED:

    result = -EISCONN;
    goto done;
  default:

    result = -EINVAL;
    goto done;
  } /* switch */
  /*
   * wait for connection to complete.
   */
  timeout = sock_sndtimeo(sk, (O_NONBLOCK & flags));
  if (0 < timeout) {

    DECLARE_WAITQUEUE(wait, current);
    add_wait_queue(TS_SDP_OS_SK_SLEEP(sk), &wait);
    set_current_state(TASK_INTERRUPTIBLE);

    while (0 < timeout &&
	   TS_SDP_SOCK_ST_CONNECT == conn->istate) {

      TS_SDP_CONN_UNLOCK(conn);
      timeout = schedule_timeout(timeout);
      TS_SDP_CONN_LOCK(conn);

      if (signal_pending(current)) {

	break;
      } /* if */
    } /* while */

    set_current_state(TASK_RUNNING);
    remove_wait_queue(TS_SDP_OS_SK_SLEEP(sk), &wait);

    if (TS_SDP_SOCK_ST_CONNECT == conn->istate) {

      if (0 < timeout) {

	result = sock_intr_errno(timeout);
      } /* if */

      goto done;
    } /* if */
  } /* if */
  /*
   * check state before exiting. It's possible the that connection
   * error'd or is being closed after reaching ESTABLISHED at this
   * point. In this case connect should return normally and allow
   * the normal mechnaism for detecting these states.
   */
  switch (conn->istate) {
  case TS_SDP_SOCK_ST_CONNECT:
    break;
  case TS_SDP_SOCK_ST_ESTABLISHED:
  case TS_SDP_SOCK_ST_CLOSE:

    sock->state = SS_CONNECTED;
    result = 0;
    break;
  case TS_SDP_SOCK_ST_CLOSED:
  case TS_SDP_SOCK_ST_ERROR:

    result = TS_SDP_CONN_ERROR(conn) ? : -ECONNABORTED;
    sock->state = SS_UNCONNECTED;
    break;
  default:

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "SOCK: Unexpected connection state leaving connect. <%d:%d>",
	     conn->istate, sock->state);
    break;
  } /* switch */

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d> connect complete <%08x:%04x> state <%04x>",
	   conn->hashent, addr->sin_addr.s_addr, addr->sin_port, conn->istate);

done:
  TS_SDP_CONN_UNLOCK(conn);
  return result;
} /* _tsSdpInetConnect */

/* ========================================================================= */
/*.._tsSdpInetListen -- listen on a socket for incomming addresses. */
static tINT32 _tsSdpInetListen
(
 struct socket *sock,
 tINT32         backlog
)
{
  struct sock *sk;
  tSDP_CONN    conn;
  tINT32       result;

  TS_CHECK_NULL(sock, -EINVAL);
  TS_CHECK_NULL(sock->sk, -EINVAL);

  sk   = sock->sk;
  conn = TS_SDP_GET_CONN(sk);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d> listen operation <%d> <%08x:%04x> <%04x>",
	   conn->hashent, conn->pid, conn->src_addr, conn->src_port,
	   conn->istate);

  TS_SDP_CONN_LOCK(conn);

  if (SS_UNCONNECTED != sock->state ||
      (TS_SDP_SOCK_ST_CLOSED != conn->istate &&
       TS_SDP_SOCK_ST_LISTEN != conn->istate)) {

    result = -EINVAL;
    goto done;
  } /* if */

  if (TS_SDP_SOCK_ST_LISTEN != conn->istate) {

    result = tsSdpInetListenStart(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "SOCK: Failed to start listening. <%d>", result);
      goto done;
    } /* if */

    if (0 == conn->src_port) {

      result = tsSdpInetPortGet(conn, 0);
      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "SOCK: Error getting port during listen. <%d>", result);
	goto done;
      } /* if */

      TS_SDP_OS_SK_INET_NUM(sk)   = conn->src_port;
      TS_SDP_OS_SK_INET_SPORT(sk) = htons(conn->src_port);
    } /* if */
  } /* if */

#if 0 /* BUG 2034 workaround. */
  conn->backlog_max = backlog;
#else
  conn->backlog_max = 1024;
#endif
  result = 0;

done:
  TS_SDP_CONN_UNLOCK(conn);
  return result;
} /* _tsSdpInetListen */

/* ========================================================================= */
/*.._tsSdpInetAccept -- accept a new socket from a listen socket. */
static tINT32 _tsSdpInetAccept
(
 struct socket *listen_sock,
 struct socket *accept_sock,
 tINT32         flags
)
{
  struct sock        *listen_sk;
  struct sock        *accept_sk = NULL;
  tSDP_CONN           listen_conn;
  tSDP_CONN           accept_conn = NULL;
  tINT32              result;
  long                timeout;

  TS_CHECK_NULL(listen_sock, -EINVAL);
  TS_CHECK_NULL(accept_sock, -EINVAL);
  TS_CHECK_NULL(listen_sock->sk, -EINVAL);

  listen_sk   = listen_sock->sk;
  listen_conn = TS_SDP_GET_CONN(listen_sk);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d> accept operation <%d> <%08x:%04x> <%04x>",
	   listen_conn->hashent, listen_conn->pid, listen_conn->src_addr,
	   listen_conn->src_port, listen_conn->istate);

  TS_SDP_CONN_LOCK(listen_conn);

  if (TS_SDP_SOCK_ST_LISTEN != listen_conn->istate) {

    result = -EINVAL;
    goto listen_done;
  } /* if */

  timeout = sock_rcvtimeo(listen_sk, (O_NONBLOCK & flags));
  /*
   * if there is no socket on the queue, wait for one. It' done in a
   * loop in case there is a problem with the first socket we hit.
   */
  while (NULL == accept_conn) {
    /*
     * No pending socket wait.
     */
    accept_conn = tsSdpInetAcceptQueueGet(listen_conn);
    if (NULL == accept_conn) {

      DECLARE_WAITQUEUE(wait, current);
      add_wait_queue(TS_SDP_OS_SK_SLEEP(listen_sk), &wait);
      set_current_state(TASK_INTERRUPTIBLE);

      while (0 < timeout &&
	     TS_SDP_SOCK_ST_LISTEN == listen_conn->istate &&
	     0 == listen_conn->backlog_cnt) {

	TS_SDP_CONN_UNLOCK(listen_conn);
	timeout = schedule_timeout(timeout);
	TS_SDP_CONN_LOCK(listen_conn);

	if (signal_pending(current)) {

	  break;
	} /* if */
      } /* while */

      set_current_state(TASK_RUNNING);
      remove_wait_queue(TS_SDP_OS_SK_SLEEP(listen_sk), &wait);
      /*
       * process result
       */
      if (0 == listen_conn->backlog_cnt) {
	result = 0;

	if (TS_SDP_SOCK_ST_LISTEN != listen_conn->istate) {

	  result = -EINVAL;
	} /* if */
	if (signal_pending(current)) {

	  result = sock_intr_errno(timeout);
	} /* if */
	if (0 == timeout) {

	  result = -EAGAIN;
	} /* if */

	goto listen_done;
      } /* if */
    } /* if */
    else {

      accept_sk = accept_conn->sk;

      switch (accept_conn->istate) {
      case TS_SDP_SOCK_ST_ACCEPTED:

	sock_graft(accept_sk, accept_sock);

	accept_conn->pid = TS_SDP_OS_GET_PID();
	accept_sock->state = SS_CONNECTED;

	accept_conn->istate = TS_SDP_SOCK_ST_ESTABLISHED;
	tsSdpInetWakeSend(accept_conn);

	break;
      case TS_SDP_SOCK_ST_ACCEPTING:

	sock_graft(accept_sk, accept_sock);

	accept_conn->pid = TS_SDP_OS_GET_PID();
	accept_sock->state = SS_CONNECTED;

	accept_conn->istate = TS_SDP_SOCK_ST_ACCEPTED;
	/*
	 * connection completion/establishment will open this up
	 */
	set_bit(SOCK_NOSPACE, &accept_sock->flags);

	break;
      case TS_SDP_SOCK_ST_CLOSE:

	sock_graft(accept_sk, accept_sock);

	accept_conn->pid = TS_SDP_OS_GET_PID();
	accept_sock->state = SS_CONNECTED;

	tsSdpInetWakeSend(accept_conn);

	break;
      default:

	TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
		 "SOCK: Serious accept error. bad state <%04x:%04x>",
		 accept_conn->istate, accept_conn->state);

      case TS_SDP_SOCK_ST_CLOSED:
      case TS_SDP_SOCK_ST_CLOSING:
      case TS_SDP_SOCK_ST_ERROR:
	/*
	 * this accept socket has problems, keep trying.
	 */
	TS_SDP_CONN_UNLOCK(accept_conn); /* AcceptQueueGet */
	TS_SDP_CONN_PUT(accept_conn);    /* INET reference (AcceptQueue ref) */

	accept_sk       = NULL;
	accept_sock->sk = NULL;
	accept_conn     = NULL;

	break;
      } /* switch */

      if (NULL != accept_conn) {
	/*
	 * Connections returned from the AcceptQueue are holding
	 * their lock, before returning the connection to the
	 * user, release the lock
	 */
	TS_SDP_CONN_UNLOCK(accept_conn); /* AcceptQueueGet */
      } /* if */
    } /* else */
  } /* while */

  result = 0;
listen_done:
  TS_SDP_CONN_UNLOCK(listen_conn);

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d:%d> accept complete <%08x:%04x><%08x:%04x> state <%04x>",
	   listen_conn->hashent,
	   (NULL == accept_conn ?
	    TS_SDP_DEV_SK_INVALID : accept_conn->hashent),
	   (NULL == accept_sk ? 0 : accept_conn->src_addr),
	   (NULL == accept_sk ? 0 : accept_conn->src_port),
	   (NULL == accept_sk ? 0 : accept_conn->dst_addr),
	   (NULL == accept_sk ? 0 : accept_conn->dst_port),
	   (NULL == accept_sk ? 0 : accept_conn->istate),
	   listen_conn->backlog_cnt);

  return result;
} /* _tsSdpInetAccept */

/* ========================================================================= */
/*.._tsSdpInetGetName -- return a sockets address information */
static tINT32 _tsSdpInetGetName
(
 struct socket   *sock,
 struct sockaddr *uaddr,
 tINT32          *size,
 tINT32           peer
)
{
  struct sockaddr_in *addr = (struct sockaddr_in *)uaddr;
  struct sock        *sk;
  tSDP_CONN           conn;

  TS_CHECK_NULL(sock, -EINVAL);
  TS_CHECK_NULL(sock->sk, -EINVAL);
  TS_CHECK_NULL(addr, -EINVAL);
  TS_CHECK_NULL(size, -EINVAL);

  sk   = sock->sk;
  conn = TS_SDP_GET_CONN(sk);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d> get name operation <%08x:%04x> <%08x:%04x> <%04x>",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->istate);

  addr->sin_family = _proto_family;
  if (0 < peer) {

    if (0 < htons(conn->dst_port) &&
	0 == (TS_SDP_ST_MASK_CLOSED & conn->istate)) {

      addr->sin_port        = htons(conn->dst_port);
      addr->sin_addr.s_addr = htonl(conn->dst_addr);
    } /* if */
    else {

      return -ENOTCONN;
    } /* else */
  } /* if */
  else {

    addr->sin_port        = htons(conn->src_port);
    addr->sin_addr.s_addr = htonl(conn->src_addr);
  } /* else */

  *size = sizeof(struct sockaddr_in);

  return 0;
} /* _tsSdpInetGetName */

/* ========================================================================= */
/*.._tsSdpInetPoll -- poll a socket for activity */
static tUINT32 _tsSdpInetPoll
(
 struct file   *file,
 struct socket *sock,
 poll_table    *wait
)
{
  struct sock *sk;
  tSDP_CONN    conn;
  tUINT32      mask = 0;

  TS_CHECK_NULL(sock, 0);
  TS_CHECK_NULL(sock->sk, 0);
  /*
   * file and/or wait can be NULL, once poll is asleep and needs to
   * recheck the falgs on being woken.
   */
  sk   = sock->sk;
  conn = TS_SDP_GET_CONN(sk);

#ifdef  _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d> poll <%08x:%04x> <%08x:%04x> state <%04x> flags <%08x>",
           conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->istate, sock->flags);
#endif

  poll_wait(file, TS_SDP_OS_SK_SLEEP(sk), wait);
  /*
   * general poll philosophy: too many mask bits are better then too few.
   * POLLHUP is not direction maskable, and the recv path in more interesting
   * for hang up. However, after receiving an EOF we want to be able to
   * still select on write, if POLLHUP was set, this would not be possible.
   */
  /*
   * no locking, should be safe as is.
   */

  switch (conn->istate) {

  case TS_SDP_SOCK_ST_LISTEN:

    mask |= (0 < conn->backlog_cnt) ? (POLLIN | POLLRDNORM) : 0;
    break;
  case TS_SDP_SOCK_ST_ERROR:

    mask |= POLLERR;
    break;
  case TS_SDP_SOCK_ST_CLOSED:

    mask |= POLLHUP;
    break;
  case TS_SDP_SOCK_ST_ESTABLISHED:
    /*
     * fall through
     */
  default:
    /*
     * recv EOF _and_ recv data
     */
    if (!(conn->byte_strm < TS_SDP_OS_SK_RCVLOWAT(sk)) ||
	0 < (TS_SDP_SHUTDOWN_RECV & conn->shutdown)) {

      mask |= POLLIN | POLLRDNORM;
    } /* if */
    /*
     * send EOF _or_ send data space. (Some poll() Linux documentation
     *                                 says that POLLHUP is incompatible
     *                                 with the POLLOUT/POLLWR flags)
     */
    if (0 < (TS_SDP_SHUTDOWN_SEND & conn->shutdown)) {

      mask |= POLLHUP;
    } /* if */
    else {
      /*
       * avoid race by setting flags, and only clearing them if the test
       * is passed. Setting after the test, we can end up with them set
       * and a passing test.
       */
      set_bit(SOCK_ASYNC_NOSPACE, &sock->flags);
      set_bit(SOCK_NOSPACE, &sock->flags);

      if (0 < __tsSdpInetWritable(conn)) {

	mask |= POLLOUT | POLLWRNORM;

	clear_bit(SOCK_ASYNC_NOSPACE, &sock->flags);
	clear_bit(SOCK_NOSPACE, &sock->flags);
      } /* if */
    } /* else */

    if (0 < conn->rcv_urg_cnt) {

      mask |= POLLPRI;
    } /* if */
  } /* switch */

#ifdef  _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: poll mask <%08x> flags <%08x> <%d:%d:%d>", mask,
	   sock->flags, conn->send_buf, conn->send_qud,
	   __tsSdpInetWritable(conn));
#endif

  return mask;
} /* _tsSdpInetPoll */

/* ========================================================================= */
/*.._tsSdpInetIoctl -- serivce an ioctl request on a socket */
static tINT32 _tsSdpInetIoctl
(
 struct socket *sock,
 tUINT32        cmd,
 unsigned long  arg
)
{
  struct sock *sk;
  tSDP_CONN    conn;
  tSDP_BUFF    buff;
  tINT32       result = 0;
  tINT32       value;

  TS_CHECK_NULL(sock, -EINVAL);
  TS_CHECK_NULL(sock->sk, -EINVAL);

  sk   = sock->sk;
  conn = TS_SDP_GET_CONN(sk);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d> ioctl <%d> <%08x:%04x> <%08x:%04x> <%04x>", cmd,
           conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->istate);
  /*
   * check IOCTLs
   */
  switch(cmd) {
    /*
     * standard INET IOCTLs
     */
  case SIOCGSTAMP:
    if (0 == TS_SDP_OS_SK_STAMP(sk).tv_sec) {

      result = -ENOENT;
    } /* if */
    else {

      result = copy_to_user((void *)arg,
			    &TS_SDP_OS_SK_STAMP(sk),
			    sizeof(struct timeval));
      if (0 != result) {

	result = -EFAULT;
      } /* if */
    } /* else */

    break;
    /*
     * Standard routing IOCTLs
     */
  case SIOCADDRT:
  case SIOCDELRT:
  case SIOCRTMSG:

    result = ip_rt_ioctl(cmd, (void *)arg);
    break;
    /*
     * Standard ARP IOCTLs
     */
  case SIOCDARP:
  case SIOCGARP:
  case SIOCSARP:
#if 0 /* currently not exported by the kernel :( */
    result = arp_ioctl(cmd, (void *)arg);
#else
    result = -ENOIOCTLCMD;
#endif
    break;
    /*
     * standard INET device IOCTLs
     */
  case SIOCGIFADDR:
  case SIOCSIFADDR:
  case SIOCGIFBRDADDR:
  case SIOCSIFBRDADDR:
  case SIOCGIFNETMASK:
  case SIOCSIFNETMASK:
  case SIOCGIFDSTADDR:
  case SIOCSIFDSTADDR:
  case SIOCSIFPFLAGS:
  case SIOCGIFPFLAGS:
  case SIOCSIFFLAGS:

    result = devinet_ioctl(cmd, (void *)arg);
    break;
    /*
     * stadard INET STREAM IOCTLs
     */
  case SIOCINQ:

    TS_SDP_CONN_LOCK(conn);

    if (TS_SDP_SOCK_ST_LISTEN != conn->istate) {
      /*
       * TODO need to subtract/add URG (inline vs. OOB)
       */
      value = conn->byte_strm;
      result = put_user(value, (tINT32 *)arg);
    } /* if */
    else {

      result = -EINVAL;
    } /* else */

    TS_SDP_CONN_UNLOCK(conn);
    break;
  case SIOCOUTQ:

    TS_SDP_CONN_LOCK(conn);

    if (TS_SDP_SOCK_ST_LISTEN != conn->istate) {

      value = conn->send_qud;
      result = put_user(value, (tINT32 *)arg);
    } /* if */
    else {

      result = -EINVAL;
    } /* else */

    TS_SDP_CONN_UNLOCK(conn);
    break;
  case SIOCATMARK:

    TS_SDP_CONN_LOCK(conn);

    value = 0;

    if (0 < conn->rcv_urg_cnt) {

      buff = tsSdpBuffPoolLookHead(&conn->recv_pool);
      if (NULL != buff &&
	  0 < (TS_SDP_BUFF_F_OOB_PRES & buff->flags) &&
	  1 == (buff->tail - buff->data)) {

	value = 1;
      } /* if */
    } /* if */

    result = put_user(value, (tINT32 *)arg);

    TS_SDP_CONN_UNLOCK(conn);
    break;
    /*
     * In the 2.6 kernel a number of IOCTLs were moved to the socket.c
     * level, and are no longer passed to the protocol family provider
     */
#ifndef TS_KERNEL_2_6
  case FIOSETOWN:
  case SIOCSPGRP:
    result = get_user(value, (int *) arg);
    if (0 == result) {

      if (current->pid != value &&
	  current->pgrp != -value &&
	  !capable(CAP_NET_ADMIN)) {

	result = -EPERM;
      } /* if */
      else {

	sk->proc = value;
      } /* else */
    } /* if */

    break;
  case FIOGETOWN:
  case SIOCGPGRP:

    result = put_user(sk->proc, (int *)arg);
    break;
#endif
  default:

    result = dev_ioctl(cmd, (void *)arg);
    break;
  } /* switch */

  return result;
} /* _tsSdpInetIoctl */

/* ========================================================================= */
/*.._tsSdpInetSetOpt -- set a socket option. */
static tINT32 _tsSdpInetSetOpt
(
 struct socket *sock,
 tINT32         level,
 tINT32         optname,
 tSTR           optval,
 tINT32         optlen
)
{
  struct sock *sk;
  tSDP_CONN conn;
  tINT32 value;
  tINT32 result, ret = 0;

  TS_CHECK_NULL(sock, -EINVAL);
  TS_CHECK_NULL(sock->sk, -EINVAL);
  TS_CHECK_NULL(optval, -EINVAL);

  sk   = sock->sk;
  conn = TS_SDP_GET_CONN(sk);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d> set option <%d:%d> <%08x:%04x> <%08x:%04x> <%04x>",
	   conn->hashent, optname, level, conn->src_addr, conn->src_port,
	   conn->dst_addr, conn->dst_port, conn->istate);

  if (SOL_TCP != level &&
      SOL_SDP != level) {

    return 0;
  } /* if */

  if (optlen < sizeof(tINT32)) {

    return -EINVAL;
  } /* if */

  if (get_user(value, (tINT32 *)optval)) {

    return -EFAULT;
  } /* if */

  TS_SDP_CONN_LOCK(conn);

  switch(optname) {
  case TCP_NODELAY:

    conn->nodelay = (0 == value) ? 0 : 1;

    if (0 < conn->nodelay) {

      result = tsSdpSendFlush(conn);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));
    } /* if */

    break;
  case SDP_ZCOPY_THRSH:

    conn->src_zthresh = value;
    conn->snk_zthresh = ((value > (conn->recv_size - TS_SDP_MSG_HDR_SIZE)) ?
			 value : (conn->recv_size - TS_SDP_MSG_HDR_SIZE));
    break;
  case SDP_ZCOPY_THRSH_SRC:

    conn->src_zthresh = value;
    break;
  case SDP_ZCOPY_THRSH_SNK:

    conn->snk_zthresh = ((value > (conn->recv_size - TS_SDP_MSG_HDR_SIZE)) ?
			 value : (conn->recv_size - TS_SDP_MSG_HDR_SIZE));
    break;
  case SDP_UNBIND:

    ret = tsSdpInetPortPut(conn);
    break;
  default:

    TS_TRACE(MOD_LNX_SDP, T_VERY_TERSE, TRACE_FLOW_WARN,
	     "SOCK: SETSOCKOPT unimplemented option <%d>.", optname);
    break;
  } /* switch */

  TS_SDP_CONN_UNLOCK(conn);
  return ret;
} /* _tsSdpInetSetOpt */

/* ========================================================================= */
/*.._tsSdpInetGetOpt -- get a socket option. */
static tINT32 _tsSdpInetGetOpt
(
 struct socket *sock,
 tINT32         level,
 tINT32         optname,
 tSTR           optval,
 tINT32        *optlen
)
{
  struct sock *sk;
  tSDP_CONN conn;
  tINT32    value;
  tINT32    len;

  TS_CHECK_NULL(sock, -EINVAL);
  TS_CHECK_NULL(sock->sk, -EINVAL);
  TS_CHECK_NULL(optval, -EINVAL);
  TS_CHECK_NULL(optlen, -EINVAL);

  sk   = sock->sk;
  conn = TS_SDP_GET_CONN(sk);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d> get option <%d:%d> <%08x:%04x> <%08x:%04x> <%04x>",
	   conn->hashent, optname, level, conn->src_addr, conn->src_port,
	   conn->dst_addr, conn->dst_port, conn->istate);

  if (SOL_TCP != level &&
      SOL_SDP != level) {

    return 0;
  } /* if */

  if (get_user(len, optlen)) {

    return -EFAULT;
  } /* if */

  len = min(len, (tINT32)sizeof(tINT32));
  if (len < sizeof(tINT32)) {

    return -EINVAL;
  } /* if */

  TS_SDP_CONN_LOCK(conn);

  switch(optname) {
  case TCP_NODELAY:

    value = (1 == conn->nodelay);
    break;
  case SDP_ZCOPY_THRSH:

    value = ((conn->src_zthresh == conn->snk_zthresh) ?
	     conn->snk_zthresh : -EPROTO);
    break;
  case SDP_ZCOPY_THRSH_SRC:

    value = conn->src_zthresh;
    break;
  case SDP_ZCOPY_THRSH_SNK:

    value = conn->snk_zthresh;
    break;
  default:

    TS_TRACE(MOD_LNX_SDP, T_VERY_TERSE, TRACE_FLOW_WARN,
	     "SOCK: GETSOCKOPT unimplemented option <%d>.", optname);
    break;
  } /* switch */

  TS_SDP_CONN_UNLOCK(conn);

  if (put_user(len, optlen)) {

    return -EFAULT;
  } /* if */

  if (copy_to_user(optval, &value, len)) {

    return -EFAULT;
  } /* if */

  return 0;
} /* _tsSdpInetGetOpt */

/* ========================================================================= */
/*.._tsSdpInetShutdown -- shutdown a socket. */
static tINT32 _tsSdpInetShutdown
(
 struct socket *sock,
 tINT32         flag
)
{
  tINT32       result = 0;
  tSDP_CONN    conn;

  TS_CHECK_NULL(sock, -EINVAL);
  TS_CHECK_NULL(sock->sk, -EINVAL);
  conn = TS_SDP_GET_CONN(sock->sk);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d> shutdown <%d> <%08x:%04x> <%08x:%04x> <%04x>",
	   conn->hashent, flag, conn->src_addr, conn->src_port,
	   conn->dst_addr, conn->dst_port, conn->istate);
  /*
   * flag: 0 - recv shutdown
   *       1 - send shutdown
   *       2 - send/recv shutdown.
   */
  if (0 > flag || 2 < flag) {

    return -EINVAL;
  } /* if */
  else {

    flag++; /* match shutdown mask. */
  } /* else */

  TS_SDP_CONN_LOCK(conn);

  conn->shutdown |=flag;

  switch (conn->istate) {
  case TS_SDP_SOCK_ST_CLOSED:

    result = -ENOTCONN;
    break;
  case TS_SDP_SOCK_ST_LISTEN:
    /*
     * Send shutdown is benign.
     */
    if (0 < (TS_SDP_SHUTDOWN_RECV & flag)) {

      result = tsSdpInetListenStop(conn);
      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "SOCK: Error <%d> while releasing listen socket <%d>",
		 result, conn->pid);
      } /* if */
    } /* if */

    break;
  case TS_SDP_SOCK_ST_ERROR:

    result = TS_SDP_CONN_ERROR(conn);
    result = (result < 0) ? result : -ECONNABORTED;
    break;
  case TS_SDP_SOCK_ST_ACCEPTED:
  case TS_SDP_SOCK_ST_CONNECT:

    result = _tsSdpInetAbort(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "SOCK: Error <%d> aborting from peer.", result);
    } /* if */

    break;
  case TS_SDP_SOCK_ST_ESTABLISHED:
  case TS_SDP_SOCK_ST_CLOSE:

    result = _tsSdpInetPostDisconnect(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "SOCK: Error <%d> disconnecting from peer.", result);
    } /* if */

    break;
  case TS_SDP_SOCK_ST_DISCONNECT:
  case TS_SDP_SOCK_ST_CLOSING:
    /*
     * nothing.
     */
    break;
  case TS_SDP_SOCK_ST_ACCEPTING:

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "SOCK: User shouldn't have a socket in this state. <%d>",
	     conn->istate);

    conn->istate = TS_SDP_SOCK_ST_ERROR;
    TS_SDP_OS_CONN_SET_ERR(conn, EPROTO);
    result = -EFAULT;

    break;
  default:

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	     "SOCK: Unknown socket state. <%d>", conn->istate);

    conn->istate = TS_SDP_SOCK_ST_ERROR;
    TS_SDP_OS_CONN_SET_ERR(conn, EPROTO);
    result = -EFAULT;
  } /* switch */


  if (0 > result) {
    tsSdpInetWakeGeneric(conn);
  } /* if */
  else {
    tsSdpInetWakeError(conn);
  } /* else */

  TS_SDP_CONN_UNLOCK(conn);
  return result;
} /* _tsSdpInetShutdown */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* Primary socket initialization                                         */
/*                                                                       */
/* --------------------------------------------------------------------- */
struct proto_ops _lnx_stream_ops = {
  family:         TS_SDP_DEV_PROTO,
  release:        _tsSdpInetRelease,
  bind:           _tsSdpInetBind,
  connect:        _tsSdpInetConnect,
  listen:         _tsSdpInetListen,
  accept:         _tsSdpInetAccept,
  sendmsg:         tsSdpInetSend,
  recvmsg:         tsSdpInetRecv,
  getname:        _tsSdpInetGetName,
  poll:           _tsSdpInetPoll,
  setsockopt:     _tsSdpInetSetOpt,
  getsockopt:     _tsSdpInetGetOpt,
  shutdown:       _tsSdpInetShutdown,
  ioctl:          _tsSdpInetIoctl,
#ifdef _TS_SDP_AIO_SUPPORT
  kvec_read:       tsSdpInetRead,
  kvec_write:      tsSdpInetWrite,
#endif
#ifdef _TS_SDP_SENDPAGE_SUPPORT
  sendpage:       sock_no_sendpage,
#endif
  socketpair:     sock_no_socketpair,
  mmap:           sock_no_mmap,
#ifdef TS_KERNEL_2_6
  owner:          THIS_MODULE,
#endif
}; /* _lnx_stream_ops */
#endif

/* ========================================================================= */
/*.._tsSdpInetCreate -- create a socket */
static tINT32 _tsSdpInetCreate
(
 struct socket *sock,
 tINT32 protocol
)
{
#ifndef _TS_SDP_BREAK_INET
  tSDP_CONN conn;

  TS_CHECK_NULL(sock, -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "STRM: create <%d:%d> <%d:%08x>",
	   sock->type, protocol, sock->state, sock->flags);

  if (SOCK_STREAM  != sock->type ||
      (IPPROTO_IP  != protocol &&
       IPPROTO_TCP != protocol)) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "CREATE: unsupported socket type/protocol. <%d:%d>",
	     sock->type, protocol);
    return -EPROTONOSUPPORT;
  } /* if */

  conn = tsSdpConnAllocate(GFP_KERNEL, TS_SDP_CONN_COMM_ID_NULL);
  if (NULL == conn) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "CREATE: failed to create socket for protocol. <%d:%d>",
	     sock->type, protocol);
    return -ENOMEM;
  } /* if */

  sock->ops   = &_lnx_stream_ops;
  sock->state = SS_UNCONNECTED;

  sock_graft(conn->sk, sock);

  conn->pid = TS_SDP_OS_GET_PID();

#if 0 /* CPU affinity testing... */
#if 1
  current->cpus_allowed = (1 << 1);
#else
  current->cpus_allowed = (1 << 2) | (1 << 3);
#endif
#endif

  return 0;
#else
  return -EAFNOSUPPORT;
#endif
} /* _tsSdpInetCreate */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* INET module initialization functions                                  */
/*                                                                       */
/* --------------------------------------------------------------------- */
static struct net_proto_family _sdp_proto = {
  family: TS_SDP_DEV_PROTO,
  create: _tsSdpInetCreate,
#ifdef TS_KERNEL_2_6
  owner:          THIS_MODULE,
#endif
}; /* _pf_family */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* SDP host module load/unload functions                                 */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..sdp_init -- initialize the sdp host module */
int __init sdp_init
(
 void
)
{
  tINT32 result = 0;

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "INIT: SDP module load.");
  /*
   * workaround for RHEL 3.0 AIO kernels. (see sdp_iocb.h)
   */
#if defined(_TS_SDP_AIO_SUPPORT) && defined(_TS_FILE_OP_TABLE_ADDR)
  {
    struct file_operations *socket_file_ops;
    socket_file_ops = (struct file_operations *)_TS_FILE_OP_TABLE_ADDR;

    if (NULL != socket_file_ops) {

      socket_file_ops->aio_read  = generic_file_aio_read;
      socket_file_ops->aio_write = generic_file_aio_write;
    } /* if */
  }
#endif
  /*
   * proc entries
   */
  result = tsSdpProcFsInit();
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "INIT: Error <%d> creating proc entries.", result);
    goto error_proc;
  } /* if */
  /*
   * generic table
   */
  result = tsSdpGenericMainInit();
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "INIT: Error <%d> initializing generic table <%d>", result);
    goto error_generic;
  } /* if */
  /*
   * advertisment table
   */
  result = tsSdpConnAdvtMainInit();
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "INIT: Error <%d> initializing SDP advertisments <%d>", result);
    goto error_advt;
  } /* if */
  /*
   * connection table
   */
  result = tsSdpConnTableInit(_proto_family,
			      _conn_size,
			      _buff_min,
			      _buff_max);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
             "INIT: Error <%d> initializing connection table.", result);
    goto error_conn;
  } /* if */
  /*
   * register
   */
  _sdp_proto.family = _proto_family;

  result = sock_register(&_sdp_proto);
  if (result < 0) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "INIT: Error <%d> registering protocol family <%d>",
	     result, _sdp_proto.family);
    goto error_sock;
  } /* if */

  return 0;
error_sock:
  (void)tsSdpConnTableClear();
error_conn:
  (void)tsSdpConnAdvtMainCleanup();
error_advt:
  (void)tsSdpGenericMainCleanup();
error_generic:
  (void)tsSdpProcFsCleanup();
error_proc:
  return result; /* success */
} /* sdp_init */

/* ========================================================================= */
/*..sdp_exit -- cleanup the sdp host module */
void sdp_exit
(
 void
)
{
  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "INIT: SDP module unload.");
  /*
   * unregister
   */
  sock_unregister(_sdp_proto.family);
  /*
   * connection table
   */
  (void)tsSdpConnTableClear();
  /*
   * delete advertisment table
   */
  (void)tsSdpConnAdvtMainCleanup();
  /*
   * delete generic table
   */
  (void)tsSdpGenericMainCleanup();
  /*
   * proc tables
   */
  (void)tsSdpProcFsCleanup();

  return;
} /* sdp_exit */

module_init(sdp_init);
module_exit(sdp_exit);
