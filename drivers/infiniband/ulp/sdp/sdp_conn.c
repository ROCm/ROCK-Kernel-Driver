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

  $Id: sdp_conn.c 32 2004-04-09 03:57:42Z roland $
*/

#include "sdp_main.h"

static char _send_post_name[] = TS_SDP_CONN_SEND_POST_NAME;
static char _recv_post_name[] = TS_SDP_CONN_RECV_POST_NAME;
static char _recv_pool_name[] = TS_SDP_SOCK_RECV_DATA_NAME;

static tSDP_DEV_ROOT_STRUCT _dev_root_s;
/* --------------------------------------------------------------------- */
/*                                                                       */
/* module specific functions                                             */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpInetAcceptQueuePut -- put a connection into a listen connection's accept Q. */
tINT32 tsSdpInetAcceptQueuePut
(
 tSDP_CONN listen_conn,
 tSDP_CONN accept_conn
 )
{
  tSDP_CONN next_conn;

  TS_CHECK_NULL(listen_conn, -EINVAL);
  TS_CHECK_NULL(accept_conn, -EINVAL);

  if (NULL != listen_conn->parent ||
      NULL != accept_conn->parent ||
      NULL == listen_conn->accept_next ||
      NULL == listen_conn->accept_prev) {

    return -EFAULT;
  } /* if */

  next_conn = listen_conn->accept_next;

  accept_conn->accept_next = listen_conn->accept_next;
  listen_conn->accept_next = accept_conn;
  accept_conn->accept_prev = listen_conn;
  next_conn->accept_prev   = accept_conn;

  accept_conn->parent = listen_conn;
  listen_conn->backlog_cnt++;
  /*
   * up ref until we release. One ref for GW and one for INET.
   */
  TS_SDP_CONN_HOLD(accept_conn); /* INET reference */

  return 0;
} /* tsSdpInetAcceptQueuePut */

/* ========================================================================= */
/*..tsSdpInetAcceptQueueGet -- get a conn from a listen conn's accept Q. */
tSDP_CONN tsSdpInetAcceptQueueGet
(
 tSDP_CONN listen_conn
 )
{
  tSDP_CONN prev_conn;
  tSDP_CONN accept_conn;

  TS_CHECK_NULL(listen_conn, NULL);

  if (NULL != listen_conn->parent ||
      NULL == listen_conn->accept_next ||
      NULL == listen_conn->accept_prev ||
      listen_conn == listen_conn->accept_next ||
      listen_conn == listen_conn->accept_prev) {

    return NULL;
  } /* if */
  /*
   * Return the next connection in the listening sockets accept
   * queue. The new connections lock is acquired, the caller must
   * unlock the connection before it is done with the connection.
   * Also the process context lock is used, so the function may
   * not be called from the CQ interrupt.
   */
  accept_conn = listen_conn->accept_prev;

  TS_SDP_CONN_LOCK(accept_conn);

  prev_conn   = accept_conn->accept_prev;

  listen_conn->accept_prev = accept_conn->accept_prev;
  prev_conn->accept_next   = listen_conn;

  accept_conn->accept_next = NULL;
  accept_conn->accept_prev = NULL;
  accept_conn->parent = NULL;

  listen_conn->backlog_cnt--;

  return accept_conn;
} /* tsSdpInetAcceptQueueGet */

/* ========================================================================= */
/*..tsSdpInetAcceptQueueRemove -- remove a conn from a conn's accept Q. */
tINT32 tsSdpInetAcceptQueueRemove
(
 tSDP_CONN accept_conn
 )
{
  tSDP_CONN next_conn;
  tSDP_CONN prev_conn;

  TS_CHECK_NULL(accept_conn, -EINVAL);

  if (NULL == accept_conn->parent) {

    return -EFAULT;
  } /* if */
  /*
   * Removes the connection from the listening sockets accept queue.
   * The listning connections lock must be acquired to access the
   * list. The process context lock is used, so the function may
   * not be called from the CQ interrupt.
   */
  TS_SDP_CONN_LOCK(accept_conn->parent);

  next_conn = accept_conn->accept_next;
  prev_conn = accept_conn->accept_prev;

  prev_conn->accept_next = accept_conn->accept_next;
  next_conn->accept_prev = accept_conn->accept_prev;

  accept_conn->parent->backlog_cnt--;

  TS_SDP_CONN_UNLOCK(accept_conn->parent);

  accept_conn->accept_next = NULL;
  accept_conn->accept_prev = NULL;
  accept_conn->parent = NULL;

  return 0;
} /* tsSdpInetAcceptQueueRemove */

/* ========================================================================= */
/*..tsSdpInetListenStart -- start listening for new connections on a socket */
tINT32 tsSdpInetListenStart
(
 tSDP_CONN conn
)
{
  unsigned long flags;

  TS_CHECK_NULL(conn, -EINVAL);

  if (TS_SDP_SOCK_ST_CLOSED != conn->istate) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Incorrect socket state <%04x:%04x> to start listening.",
	     conn->istate, conn->state);
    return -EBADFD;
  } /* if */

  conn->istate = TS_SDP_SOCK_ST_LISTEN;
  conn->accept_next = conn;
  conn->accept_prev = conn;
  /*
   * table lock
   */
  TS_SPIN_LOCK(&_dev_root_s.listen_lock, flags);
  /*
   * insert into listening list.
   */
  conn->lstn_next         = _dev_root_s.listen_list;
  _dev_root_s.listen_list = conn;
  conn->lstn_p_next       = &_dev_root_s.listen_list;

  if (NULL != conn->lstn_next) {

    conn->lstn_next->lstn_p_next = &conn->lstn_next;
  } /* if */

  TS_SPIN_UNLOCK(&_dev_root_s.listen_lock, flags);
  return 0;
} /* tsSdpInetListenStart */

/* ========================================================================= */
/*..tsSdpInetListenStop -- stop listening for new connections on a socket */
tINT32 tsSdpInetListenStop
(
 tSDP_CONN listen_conn
)
{
  tSDP_CONN accept_conn;
  tINT32 result;
  unsigned long flags;

  TS_CHECK_NULL(listen_conn, -EINVAL);

  if (TS_SDP_SOCK_ST_LISTEN != listen_conn->istate) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Incorrect socket state <%04x:%04x> to stop listening.",
	     listen_conn->istate, listen_conn->state);
    return -EBADFD;
  } /* if */

  listen_conn->istate = TS_SDP_SOCK_ST_CLOSED;
  /*
   * table lock
   */
  TS_SPIN_LOCK(&_dev_root_s.listen_lock, flags);
  /*
   * remove from listening list.
   */
  if (NULL != listen_conn->lstn_next) {
    listen_conn->lstn_next->lstn_p_next = listen_conn->lstn_p_next;
  } /* if */

  *(listen_conn->lstn_p_next) = listen_conn->lstn_next;

  listen_conn->lstn_p_next = NULL;
  listen_conn->lstn_next   = NULL;

  TS_SPIN_UNLOCK(&_dev_root_s.listen_lock, flags);
  /*
   * reject and delete all pending connections
   */
  while (NULL != (accept_conn = tsSdpInetAcceptQueueGet(listen_conn))) {
    /*
     * The connection is going to be dropped now, mark the
     * state as such in case of conntension for this conn.
     * Remember to unlock since the Get function will acquire
     * the lock.
     */
    accept_conn->istate = TS_SDP_SOCK_ST_CLOSED;

    result = tsSdpConnAbort(accept_conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "STRM: Error <%d> rejecting connection <%08x:%04x><%08x:%04x>",
	       result, accept_conn->src_addr, accept_conn->src_port,
	       accept_conn->dst_addr, accept_conn->dst_port);
    } /* if */

    TS_SDP_CONN_UNLOCK(accept_conn); /* AcceptQueueGet */
    TS_SDP_CONN_PUT(accept_conn); /* INET reference (AcceptQueuePut). */
  } /* if */

  listen_conn->accept_next = NULL;
  listen_conn->accept_prev = NULL;

  return 0;
} /* tsSdpInetListenStop */

/* ========================================================================= */
/*..tsSdpInetListenLookup -- lookup a connection in the listen list */
tSDP_CONN tsSdpInetListenLookup
(
 tUINT32 addr,
 tUINT16 port
)
{
  tSDP_CONN conn;
  unsigned long flags;
  /*
   * table lock
   */
  TS_SPIN_LOCK(&_dev_root_s.listen_lock, flags);
  /*
   * first find a listening connection
   */
  for (conn = _dev_root_s.listen_list; NULL != conn; conn = conn->lstn_next) {

    if (port == conn->src_port &&
	(INADDR_ANY == conn->src_addr ||
	 addr == conn->src_addr)) {

      TS_SDP_CONN_HOLD(conn);
      break;
    } /* if */
  } /* for */

  TS_SPIN_UNLOCK(&_dev_root_s.listen_lock, flags);
  return conn;
} /* tsSdpInetListenLookup */

/* ========================================================================= */
/*..tsSdpInetPortGet -- bind a socket to a port. */
tINT32 tsSdpInetPortGet
(
 tSDP_CONN conn,
 tUINT16 port
)
{
  struct sock   *sk;
  struct sock    *srch;
  tSDP_CONN         look;
  tINT32            counter;
  tINT32            low_port;
  tINT32            top_port;
  tINT32            port_ok;
  tINT32            result;
  static tINT32     rover = -1;
  unsigned long     flags;

  TS_CHECK_NULL(conn, -EINVAL);

  sk = conn->sk;
  /*
   * lock table
   */
  TS_SPIN_LOCK(&_dev_root_s.bind_lock, flags);
  /*
   * simple linked list of sockets ordered on local port number.
   */
  if (0 < port) {

    for (look = _dev_root_s.bind_list, port_ok = 1;
	 NULL != look;
	 look = look->bind_next) {

      srch = look->sk;
      /*
       * 1) same port
       * 2) linux force reuse is off.
       * 3) same bound interface, or neither has a bound interface
       */
      if (look->src_port == port &&
	  !(1 < TS_SDP_OS_SK_REUSE(sk)) &&
	  !(1 < TS_SDP_OS_SK_REUSE(srch)) &&
	  TS_SDP_OS_SK_BOUND_IF(sk) == TS_SDP_OS_SK_BOUND_IF(srch)) {
	/*
	 * 3) either socket has reuse turned off
	 * 4) socket already listening on this port
	 */
	if (0 == TS_SDP_OS_SK_REUSE(sk) ||
	    0 == TS_SDP_OS_SK_REUSE(srch) ||
	    TS_SDP_SOCK_ST_LISTEN == look->istate) {
	  /*
	   * 5) neither socket is using a specific address
	   * 6) both sockets are trying for the same interface.
	   */
	  if (INADDR_ANY == conn->src_addr ||
	      INADDR_ANY == look->src_addr ||
	      conn->src_addr == look->src_addr) {

	    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
		     "SOCK: port rejected. <%04x><%d:%d><%d:%d><%04x><%u:%u>",
		     port,
		     TS_SDP_OS_SK_BOUND_IF(sk), TS_SDP_OS_SK_BOUND_IF(srch),
		     TS_SDP_OS_SK_REUSE(sk), TS_SDP_OS_SK_REUSE(srch),
		     look->state, conn->src_addr, look->src_addr);
	    port_ok = 0;
	    break;
	  } /* if */
	} /* if */
      } /* if */
    } /* for */

    if (0 == port_ok) {

      result = -EADDRINUSE;
      goto done;
    } /* else */
  } /* if */
  else {

    low_port = TS_SDP_PORT_RANGE_LOW;
    top_port = TS_SDP_PORT_RANGE_HIGH;
    rover    = (0 > rover) ? low_port : rover;

    for (counter  = (top_port - low_port) + 1; counter > 0; counter--) {

      rover++;
      if (rover < low_port ||
	  rover > top_port) {

	rover = low_port;
      } /* if */

      for (look = _dev_root_s.bind_list;
	   NULL != look && look->src_port != port;
	   look = look->bind_next) {
	/*
	 * pass
	 */
      } /* for */

      if (NULL == look) {

	port = rover;
	break;
      } /* else */
    } /* for */

    if (0 == port) {

      result = -EADDRINUSE;
      goto done;
    } /* else */
  } /* else */

  conn->src_port = port;
  /*
   * insert into listening list.
   */
  conn->bind_next       = _dev_root_s.bind_list;
  _dev_root_s.bind_list = conn;
  conn->bind_p_next     = &_dev_root_s.bind_list;

  if (NULL != conn->bind_next) {

    conn->bind_next->bind_p_next = &conn->bind_next;
  } /* if */

  result = 0;
done:
  TS_SPIN_UNLOCK(&_dev_root_s.bind_lock, flags);
  return result;
} /* tsSdpInetPortGet */

/* ========================================================================= */
/*..tsSdpInetPortPut -- unbind a socket from a port. */
tINT32 tsSdpInetPortPut
(
 tSDP_CONN conn
)
{
  unsigned long flags;

  TS_CHECK_NULL(conn, -EINVAL);

  if (NULL == conn->bind_p_next) {

    return -EADDRNOTAVAIL;
  } /* if */
  /*
   * lock table
   */
  TS_SPIN_LOCK(&_dev_root_s.bind_lock, flags);
  /*
   * remove from bind list.
   */
  if (NULL != conn->bind_next) {
    conn->bind_next->bind_p_next = conn->bind_p_next;
  } /* if */

  *(conn->bind_p_next) = conn->bind_next;

  conn->bind_p_next = NULL;
  conn->bind_next   = NULL;
  conn->src_port    = 0;

  TS_SPIN_UNLOCK(&_dev_root_s.bind_lock, flags);
  return 0;
} /* tsSdpInetPortPut */

/* ========================================================================= */
/*..tsSdpInetPortInherit -- inherit a port from another socket (accept) */
tINT32 tsSdpInetPortInherit
(
  tSDP_CONN parent,
  tSDP_CONN child
)
{
  tINT32 result;
  unsigned long flags;

  TS_CHECK_NULL(child, -EINVAL);
  TS_CHECK_NULL(parent, -EINVAL);
  TS_CHECK_NULL(parent->bind_p_next, -EINVAL);
  /*
   * lock table
   */
  TS_SPIN_LOCK(&_dev_root_s.bind_lock, flags);

  if (NULL != child->bind_p_next ||
      child->src_port != parent->src_port) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Parents <%d> child already bound. <%d>",
	     parent->src_port, child->src_port);
    result = -EADDRNOTAVAIL;
    goto done;
  } /* if */
  /*
   * insert into listening list.
   */
  child->bind_next   = parent->bind_next;
  parent->bind_next  = child;
  child->bind_p_next = &parent->bind_next;

  if (NULL != child->bind_next) {

    child->bind_next->bind_p_next = &child->bind_next;
  } /* if */

  result = 0;
done:
  TS_SPIN_UNLOCK(&_dev_root_s.bind_lock, flags);
  return result;
} /* tsSdpInetPortInherit */

/* ========================================================================= */
/*..tsSdpConnTableInsert -- insert a connection into the connection table */
tINT32 tsSdpConnTableInsert
(
 tSDP_CONN conn
)
{
  tINT32  counter;
  tINT32  result = -ENOMEM;
  unsigned long flags;

  TS_CHECK_NULL(conn, -EINVAL);

  if (TS_SDP_DEV_SK_INVALID != conn->hashent) {

    return -ERANGE;
  } /* if */
  /*
   * lock table
   */
  TS_SPIN_LOCK(&_dev_root_s.sock_lock, flags);
  /*
   * find an empty slot.
   */
  for (counter = 0;
       counter < _dev_root_s.sk_size;
       counter++, _dev_root_s.sk_rover++) {

    if (!(_dev_root_s.sk_rover < _dev_root_s.sk_size)) {

      _dev_root_s.sk_rover = 0;
    } /* if */

    if (NULL == _dev_root_s.sk_array[_dev_root_s.sk_rover]) {

      _dev_root_s.sk_array[_dev_root_s.sk_rover] = conn;
      _dev_root_s.sk_entry++;
      conn->hashent = _dev_root_s.sk_rover;

      result = 0;
      break;
    } /* if */
  } /* for */

#if 0 /* set for reproducibility from run-run. */
  _dev_root_s.sk_rover = 0;
#endif
  /*
   * unlock table
   */
  TS_SPIN_UNLOCK(&_dev_root_s.sock_lock, flags);
  return result;
} /* tsSdpConnTableInsert */

/* ========================================================================= */
/*..tsSdpConnTableRemove -- remove a connection from the connection table */
tINT32 tsSdpConnTableRemove
(
 tSDP_CONN conn
)
{
  tINT32  result = 0;
  unsigned long flags;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * lock table
   */
  TS_SPIN_LOCK(&_dev_root_s.sock_lock, flags);
  /*
   * validate entry
   */
  if (TS_SDP_DEV_SK_INVALID == conn->hashent) {

    goto done;
  } /* if */

  if (0 > conn->hashent ||
      conn != _dev_root_s.sk_array[conn->hashent]) {

    result = -ERANGE;
    goto done;
  } /* if */
  /*
   * drop entry
   */
  _dev_root_s.sk_array[conn->hashent] = NULL;
  _dev_root_s.sk_entry--;
  conn->hashent = TS_SDP_DEV_SK_INVALID;

done:
  /*
   * unlock table
   */
  TS_SPIN_UNLOCK(&_dev_root_s.sock_lock, flags);
  return result;
} /* tsSdpConnTableRemove */

/* ========================================================================= */
/*..tsSdpConnTableLookup -- look up connection in the connection table */
tSDP_CONN tsSdpConnTableLookup
(
 tINT32 entry
)
{
  tSDP_CONN conn;
  unsigned long flags;
  /*
   * lock table
   */
  TS_SPIN_LOCK(&_dev_root_s.sock_lock, flags);
#if 0
  /*
   * validate range
   */
  if (0 > entry ||
      !(_dev_root_s.sk_size > entry)) {

    conn = NULL;
    goto done;
  } /* if */
#endif

  conn = _dev_root_s.sk_array[entry];
  if (NULL == conn) {

    goto done;
  } /* if */

  TS_SDP_CONN_HOLD(conn);
done:
  TS_SPIN_UNLOCK(&_dev_root_s.sock_lock, flags);
  return conn;
} /* tsSdpConnTableLookup */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* connection allocation/deallocation                                    */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpConnDestruct -- final destructor for connection. */
tINT32 tsSdpConnDestruct
(
 tSDP_CONN conn
)
{
  tINT32 result = 0;
  tINT32 dump = 0;

  if (NULL == conn) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: sk destruct, no connection!");
    result = -EINVAL;
    goto done;
  } /* if */

  if (NULL == conn->sk) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: sk destruct, no connection! continuing.");
  } /* if */
  else {

    sk_free(conn->sk);
    conn->sk = NULL;
  } /* else */

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%d> destruct cb <%08x:%04x> <%08x:%04x> <%04x> <%u:%u>",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->state, conn->src_serv, conn->snk_serv);
  /*
   * If the socket is bound, return the port
   */
  (void)tsSdpInetPortPut(conn);
  /*
   * in case CM/IB are still tracking this connection.
   */
  (void)tsIbCmCallbackModify(conn->comm_id, NULL, NULL);
  /*
   * remove connection from table
   */
  result = tsSdpConnTableRemove(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "SOCK: Error <%d> removing connection <%d> from table. <%d:%d>",
	     result, conn->hashent, _dev_root_s.sk_entry, _dev_root_s.sk_size);
  } /* if */

  result = __tsSdpConnStatDump(conn);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * really there shouldn't be anything in these tables, but it's
   * really bad if we leave a dangling reference here.
   */
  (void)tsSdpBuffKvecCancelAll(conn, -ECANCELED);
  (void)tsSdpConnIocbTableClear(&conn->r_pend);
  (void)tsSdpConnIocbTableClear(&conn->r_snk);
  (void)tsSdpConnIocbTableClear(&conn->w_src);

  (void)tsSdpGenericTableClear(&conn->r_src);
  (void)tsSdpGenericTableClear(&conn->w_snk);
  /*
   * clear the buffer pools
   */
  (void)tsSdpBuffPoolClear(&conn->recv_pool);
  (void)tsSdpBuffPoolClear(&conn->send_post);
  (void)tsSdpBuffPoolClear(&conn->recv_post);
  /*
   * clear advertisment tables
   */
  (void)tsSdpConnAdvtTableClear(&conn->src_pend);
  (void)tsSdpConnAdvtTableClear(&conn->src_actv);
  (void)tsSdpConnAdvtTableClear(&conn->snk_pend);
  /*
   * generic table clear
   */
  (void)tsSdpGenericTableClear(&conn->send_ctrl);
  (void)tsSdpGenericTableClear(&conn->send_queue);
  /*
   * If the QP owner is not the CM, then destroy.
   */
   if (TS_IB_HANDLE_INVALID != conn->qp) {

     result = tsIbQpDestroy(conn->qp);
     if (0 > result && -EINVAL != result) {

       TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
		"CONN: Error <%d> detroying QP", result);
       dump++;
     } /* if */
  } /* if */
  /*
   * destroy CQs
   */
  if (TS_IB_HANDLE_INVALID != conn->recv_cq) {

    result = tsIbCqDestroy(conn->recv_cq);
    if (0 > result && -EINVAL != result) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	       "CONN: Error <%d> detroying recv CQ", result);
      dump++;
    } /* if */
  } /* if */

  if (TS_IB_HANDLE_INVALID != conn->send_cq) {

    result = tsIbCqDestroy(conn->send_cq);
    if (0 > result && -EINVAL != result) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	       "CONN: Error <%d> detroying send CQ", result);
      dump++;
    } /* if */
  } /* if */
  /*
   * check consistancy
   */
  if (0 > TS_SDP_ATOMIC_GET(conn->refcnt)) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	     "SOCK: destruct <%08x:%04x> <%08x:%04x> low ref <%04x>",
	     conn->src_addr, conn->src_port, conn->dst_addr, conn->dst_port,
	     TS_SDP_ATOMIC_GET(conn->refcnt));
  } /* if */

  kmem_cache_free(_dev_root_s.conn_cache, conn);

  result = 0;
done:

  if (0 != dump) {

    result = __tsSdpConnStateDump(conn);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  } /* if */

  return result;
} /* _tsSdpConnDestruct */

/* ========================================================================= */
/*..tsSdpConnLockInternalLock -- lock the connection (use only from macro) */
void tsSdpConnLockInternalLock
(
 tSDP_CONN conn
#if defined(TS_KERNEL_2_6)
/* Work around the temporary workaround for THCA locking bugs :) */
  ,unsigned long *flags
#endif
 )
{
  DECLARE_WAITQUEUE(wait, current);
#if defined(TS_KERNEL_2_6)
  unsigned long f = *flags;
#endif

  add_wait_queue_exclusive(&(conn->lock.waitq), &wait);
  for(;;) {
    current->state = TASK_UNINTERRUPTIBLE;
#if !defined(TS_KERNEL_2_6)
    spin_unlock_bh(&(conn->lock.slock));
#else
    spin_unlock_irqrestore(&(conn->lock.slock), f);
#endif
    schedule();
#if !defined(TS_KERNEL_2_6)
    spin_lock_bh(&(conn->lock.slock));
#else
    spin_lock_irqsave(&(conn->lock.slock), f);
    *flags = f;
#endif
    if (0 == conn->lock.users) {

      break;
    } /* if */
  } /* for */

  current->state = TASK_RUNNING;
  remove_wait_queue(&(conn->lock.waitq), &wait);

  return;
} /* tsSdpConnLockInternalLock */

/* ========================================================================= */
/*..tsSdpConnLockInternalRelock -- test the connection (use only from macro) */
void tsSdpConnLockInternalRelock
(
 tSDP_CONN conn
 )
{
  tTS_IB_CQ_ENTRY_STRUCT entry;
  tINT32 result_r;
  tINT32 result_s;
  tINT32 result;
  tINT32 rearm = 1;

  while (1) {

    result_r = tsIbCqPoll(conn->recv_cq, &entry);
    if (0 == result_r) {

      result = tsSdpEventLocked(&entry, conn);
      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "EVENT: Error <%d> from locked event handler.", result);
      } /* if */

      rearm = 1;
    } /* if */

    result_s = tsIbCqPoll(conn->send_cq, &entry);
    if (0 == result_s) {

      result = tsSdpEventLocked(&entry, conn);
      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "EVENT: Error <%d> from locked event handler.", result);
      } /* if */

      rearm = 1;
    } /* if */

    if (-EAGAIN == result_r && -EAGAIN == result_s) {

      if (0 < rearm) {

	result = tsIbCqRequestNotification(conn->recv_cq, 0);
	if (0 > result) {

	  TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
		   "EVENT: error <%d> rearming CQ. <%d:%d>",
		   result, conn->recv_cq, conn->hashent);
	} /* if */

	result = tsIbCqRequestNotification(conn->send_cq, 0);
	if (0 > result) {

	  TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
		   "EVENT: error <%d> rearming CQ. <%d:%d>",
		   result, conn->send_cq, conn->hashent);
	} /* if */

	rearm = 0;
      } /* if */
      else {
	/*
	 * exit CQ handler routine.
	 */
	break;
      } /* else */
    } /* if */
  } /* while */

  conn->flags &= ~TS_SDP_CONN_F_MASK_EVENT;

  return;
} /* tsSdpConnLockInternalRelock */

/* ========================================================================= */
/*..tsSdpConnCqDrain -- drain one of the the connection's CQs */
tINT32 tsSdpConnCqDrain
(
 tTS_IB_CQ_HANDLE cq,
 tSDP_CONN conn
)
{
  tTS_IB_CQ_ENTRY_STRUCT entry;
  tINT32 result;
  tINT32 rearm = 1;
  tINT32 calls = 0;
  /*
   * the function should only be called under the connection locks
   * spinlock to ensure the call is serialized to avoid races.
   */
  for (;;) {
    /*
     * poll for a completion
     */
    result = tsIbCqPoll(cq, &entry);
    if (0 == result) {
      /*
       * dispatch completion, and mark that the CQ needs to be armed.
       */
      result = tsSdpEventLocked(&entry, conn);
      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "EVENT: Error <%d> from locked event handler.", result);
      } /* if */

      rearm = 1;
      calls++;
    } /* if */
    else {

      if (-EAGAIN == result) {

	if (0 < rearm) {

	  result = tsIbCqRequestNotification(cq, 0);
	  if (0 > result) {

	    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
		     "EVENT: error <%d> rearming CQ. <%d:%d>",
		     result, cq, conn->hashent);
	  } /* if */

	  rearm = 0;
	} /* if */
	else {
	  /*
	   * exit CQ handler routine.
	   */
	  break;
	} /* else */
      } /* if */
      else {
	/*
	 * unexpected CQ error
	 */
	TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
		 "EVENT: CQ <%d:%d> poll error. <%d>",
		 cq, conn->hashent, result);
      } /* else */
    } /* else */
  } /* for */

  return calls;
}

/* ========================================================================= */
/*..tsSdpConnLockInternalUnlock -- lock the connection (use only from macro) */
void tsSdpConnLockInternalUnlock
(
 tSDP_CONN conn
 )
{
  tINT32 calls = 0;
  /*
   * poll CQs for events.
   */
  if (NULL != conn) {

    if (0 < (TS_SDP_CONN_F_RECV_CQ_PEND & conn->flags)) {

      calls += tsSdpConnCqDrain(conn->recv_cq, conn);
    } /* if */

    if (0 < (TS_SDP_CONN_F_SEND_CQ_PEND & conn->flags)) {

      calls += tsSdpConnCqDrain(conn->send_cq, conn);
    } /* if */

    conn->flags &= ~TS_SDP_CONN_F_MASK_EVENT;
  } /* if */

  return;
} /* tsSdpConnLockInternalUnlock */

/* ========================================================================= */
/*.._tsSdpConnLockInit -- initialize connection lock */
static tINT32 _tsSdpConnLockInit
(
 tSDP_CONN conn
 )
{
  TS_CHECK_NULL(conn, -EINVAL);

  TS_SPIN_LOCK_INIT(&(conn->lock.slock));
  conn->lock.users = 0;
  init_waitqueue_head(&(conn->lock.waitq));

  return 0;
} /* _tsSdpConnLockInit */

/* ========================================================================= */
/*..tsSdpConnAllocateIb -- allocate IB structures for a new connection. */
tINT32 tsSdpConnAllocateIb
(
 tSDP_CONN conn,
 tTS_IB_DEVICE_HANDLE device,
 tTS_IB_PORT hw_port
)
{
  tTS_IB_QP_CREATE_PARAM_STRUCT qp_create = { { 0 } };
  tTS_IB_CQ_CALLBACK_STRUCT     callback;
  tTS_IB_QP_ATTRIBUTE           qp_attr;
  tSDP_DEV_PORT                 port = NULL;
  tSDP_DEV_HCA                  hca = NULL;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * look up correct HCA and port
   */
  for (hca = _dev_root_s.hca_list; NULL != hca; hca = hca->next) {

    if (device == hca->ca) {

      for (port = hca->port_list; NULL != port; port = port->next) {

	if (hw_port == port->index) {

	  break;
	} /* if */
      } /* for */

      break;
    } /* if */
  } /* for */

  if (NULL == hca ||
      NULL == port) {

    return -ERANGE;
  } /* if */
  /*
   * set port specific connection parameters.
   */
  conn->ca       = hca->ca;
  conn->pd       = hca->pd;
  conn->hw_port  = port->index;
  conn->l_key    = hca->l_key;
  conn->fmr_pool = hca->fmr_pool;

  memcpy(conn->s_gid, port->gid, sizeof(tTS_IB_GID));
  /*
   * allocate IB CQ's and QP
   */
  if (TS_IB_HANDLE_INVALID == conn->send_cq) {

    callback.context        = TS_IB_CQ_CALLBACK_INTERRUPT;
    callback.policy         = TS_IB_CQ_CONSUMER_REARM;
    callback.function.event = tsSdpEventHandler;
    callback.arg            = (tPTR) (unsigned long) conn->hashent;

    result = tsIbCqCreate(conn->ca,
			  &conn->send_cq_size,
			  &callback,
                          NULL,
			  &conn->send_cq);
    if (0 != result) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	       "INIT: Error <%d> creating send completion queue (size <%d>)",
	       result, conn->send_cq_size);
      goto error_scq;
    } /* if */

    result = tsIbCqRequestNotification(conn->send_cq, 0);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	       "EVENT: error <%d> arming send CQ. <%d:%d>",
	       result, conn->send_cq, conn->hashent);
      goto error_rcq;
    } /* if */
  } /* if */

  if (TS_IB_HANDLE_INVALID == conn->recv_cq) {

    callback.context        = TS_IB_CQ_CALLBACK_INTERRUPT;
    callback.policy         = TS_IB_CQ_CONSUMER_REARM;
    callback.function.event = tsSdpEventHandler;
    callback.arg            = (tPTR) (unsigned long) conn->hashent;

    result = tsIbCqCreate(conn->ca,
			  &conn->recv_cq_size,
                          &callback,
                          NULL,
			  &conn->recv_cq);
    if (0 != result) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	       "INIT: Error <%d> creating recv completion queue (size <%d>)",
	       result, conn->recv_cq_size);
      goto error_rcq;
    } /* if */

    result = tsIbCqRequestNotification(conn->recv_cq, 0);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	       "EVENT: error <%d> arming recv CQ. <%d:%d>",
	       result, conn->recv_cq, conn->hashent);
      goto error_qp;
    } /* if */
  } /* if */

  if (TS_IB_HANDLE_INVALID == conn->qp) {

    qp_create.limit.max_outstanding_send_request    = TS_SDP_QP_LIMIT_WQE_SEND;
    qp_create.limit.max_outstanding_receive_request = TS_SDP_QP_LIMIT_WQE_RECV;
    qp_create.limit.max_send_gather_element         = TS_SDP_QP_LIMIT_SG_SEND;
    qp_create.limit.max_receive_scatter_element     = TS_SDP_QP_LIMIT_SG_RECV;

    qp_create.pd             = conn->pd;
    qp_create.send_queue     = conn->send_cq;
    qp_create.receive_queue  = conn->recv_cq;
    qp_create.send_policy    = TS_IB_WQ_SIGNAL_SELECTABLE;
    qp_create.receive_policy = TS_IB_WQ_SIGNAL_ALL;
    qp_create.transport      = TS_IB_TRANSPORT_RC;

    result = tsIbQpCreate(&qp_create,
			  &conn->qp,
			  &conn->s_qpn);
    if (0 != result) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	       "INIT: Error <%d> creating queue pair.", result);
      goto error_qp;
    } /* if */
    /*
     * modify QP to INIT
     */
    qp_attr = kmalloc(sizeof(tTS_IB_QP_ATTRIBUTE_STRUCT),  GFP_KERNEL);
    if (NULL == qp_attr) {

      result = -ENOMEM;
      goto error_attr;
    } /* if */

    memset(qp_attr, 0, sizeof(tTS_IB_QP_ATTRIBUTE_STRUCT));

    qp_attr->valid_fields     |= TS_IB_QP_ATTRIBUTE_STATE;
    qp_attr->state             = TS_IB_QP_STATE_INIT;

    qp_attr->valid_fields     |= TS_IB_QP_ATTRIBUTE_RDMA_ATOMIC_ENABLE;
    qp_attr->enable_rdma_read  = 1;
    qp_attr->enable_rdma_write = 1;

    qp_attr->valid_fields     |= TS_IB_QP_ATTRIBUTE_PORT;
    qp_attr->port              = conn->hw_port;

    qp_attr->valid_fields     |= TS_IB_QP_ATTRIBUTE_PKEY_INDEX;
    qp_attr->pkey_index        = 0;

    result = tsIbQpModify(conn->qp, qp_attr);
    kfree(qp_attr);

    if (0 != result) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	       "INIT: Error <%d> modifying queue pair.", result);
      goto error_mod;
    } /* if */
  } /* if */

  return 0;
error_mod:
error_attr:
  (void)tsIbQpDestroy(conn->qp);
error_qp:
  (void)tsIbCqDestroy(conn->recv_cq);
error_rcq:
  (void)tsIbCqDestroy(conn->send_cq);
error_scq:
  conn->send_cq = TS_IB_HANDLE_INVALID;
  conn->recv_cq = TS_IB_HANDLE_INVALID;
  conn->qp      = TS_IB_HANDLE_INVALID;

  return result;
} /* tsSdpConnAllocateIb  */

/* ========================================================================= */
/*..tsSdpConnAllocate -- allocate a new socket, and init. */
tSDP_CONN tsSdpConnAllocate
(
 tINT32             priority,
 tTS_IB_CM_COMM_ID  comm_id
)
{
  tSDP_CONN conn;
  struct sock *sk;
  tINT32 result;

#ifdef TS_KERNEL_2_6
  sk = sk_alloc(_dev_root_s.proto, priority, 1, NULL);
#else
  sk = sk_alloc(_dev_root_s.proto, priority, 1);
#endif
  if (NULL == sk) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "CREATE: failed to create socket for protocol. <%d:%d>",
	     _dev_root_s.proto, priority);
    return NULL;
  } /* if */
  /*
   * initialize base socket
   */
  sock_init_data(NULL, sk); /* refcnt set to 1 */
  /*
   * other non-zero sock initialization.
   */
  TS_SDP_OS_SK_FAMILY(sk)   = _dev_root_s.proto;
  TS_SDP_OS_SK_PROTOCOL(sk) = IPPROTO_TCP;
  /*
   * some callbacks. higher levels of linux (e.g. setsockopt) call
   * these functions so they should be shim's to our functions...
   */
  TS_SDP_OS_SK_DESTRUCT(sk)     = NULL;
  TS_SDP_OS_SK_WRITE_SPACE(sk)  = __tsSdpOSConnWakeWrite;
  TS_SDP_OS_SK_STATE_CHANGE(sk) = __tsSdpOSConnWakeReady;
  TS_SDP_OS_SK_DATA_READY(sk)   = __tsSdpOSConnWakeRead;
  TS_SDP_OS_SK_ERROR_REPORT(sk) = __tsSdpOSConnWakeError;
  /*
   * Allocate must be called from process context, since QP
   * create/modifies must be in that context.
   */
  conn = kmem_cache_alloc(_dev_root_s.conn_cache, priority);
  if (NULL == conn) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "CREATE: Error allocating connection. <%d>",
	     sizeof(tSDP_CONN_STRUCT));
    result = -ENOMEM;
    goto error;
  } /* if */

  memset(conn, 0, sizeof(tSDP_CONN_STRUCT));
  /*
   * The STRM interface specific data is map/cast over the TCP specific
   * area of the sock.
   */
  TS_SDP_SET_CONN(sk, conn);
  TS_SDP_CONN_ST_INIT(conn);

  conn->oob_offset  = -1;
  conn->rcv_urg_cnt = 0;

  conn->nodelay     = 0;
  conn->src_zthresh = SDP_ZCOPY_THRSH_SRC_DEFAULT;
  conn->snk_zthresh = SDP_ZCOPY_THRSH_SNK_DEFAULT;

  conn->accept_next = NULL;
  conn->accept_prev = NULL;
  conn->parent      = NULL;

  conn->pid       = 0;
  conn->sk        = sk;
  conn->hashent   = TS_SDP_DEV_SK_INVALID;
  conn->istate    = TS_SDP_SOCK_ST_CLOSED;
  conn->flags     = 0;
  conn->shutdown  = TS_SDP_SHUTDOWN_NONE;
  conn->recv_mode = TS_SDP_MODE_COMB;
  conn->send_mode = TS_SDP_MODE_COMB;

  TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_CLOSED);

  conn->comm_id   = comm_id;
  conn->send_seq  = 0;
  conn->recv_seq  = 0;
  conn->advt_seq  = 0;

  conn->nond_recv = 0;
  conn->nond_send = 0;

  conn->recv_max  = TS_SDP_DEV_RECV_POST_MAX;
  conn->send_max  = TS_SDP_DEV_SEND_POST_MAX;
  conn->rwin_max  = TS_SDP_DEV_RECV_WIN_MAX;
  conn->s_wq_size = 0;

  conn->send_buf  = 0;
  conn->send_qud  = 0;
  conn->send_pipe = 0;

  conn->recv_size = tsSdpBuffMainBuffSize();
  conn->send_size = 0;

  conn->src_serv  = 0;
  conn->snk_serv  = 0;
  conn->s_cur_adv = 1;
  conn->s_par_adv = 0;

  conn->src_recv  = 0;
  conn->snk_recv  = 0;
  conn->src_sent  = 0;
  conn->snk_sent  = 0;

  conn->send_bytes   = 0;
  conn->recv_bytes   = 0;
  conn->write_bytes  = 0;
  conn->read_bytes   = 0;
  conn->write_queued = 0;
  conn->read_queued  = 0;

  conn->send_usig = 0;
  conn->send_cons = 0;

  conn->s_wq_cur = TS_SDP_DEV_SEND_POST_MAX;
  conn->s_wq_par = 0;

  conn->plid    = TS_IP2PR_PATH_LOOKUP_INVALID;

  conn->send_cq_size = TS_SDP_DEV_SEND_CQ_SIZE;
  conn->recv_cq_size = TS_SDP_DEV_RECV_CQ_SIZE;

  conn->send_cq  = TS_IB_HANDLE_INVALID;
  conn->recv_cq  = TS_IB_HANDLE_INVALID;
  conn->qp       = TS_IB_HANDLE_INVALID;
  conn->ca       = TS_IB_HANDLE_INVALID;
  conn->pd       = TS_IB_HANDLE_INVALID;
  conn->l_key    = 0;
  conn->fmr_pool = NULL;
  conn->hw_port  = 0;
   /*
   * generic send queue
   */
  result = tsSdpGenericTableInit(&conn->send_queue);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  result = tsSdpGenericTableInit(&conn->send_ctrl);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * create buffer pools for posted events
   */
  result = tsSdpBuffPoolInit(&conn->recv_post, _recv_post_name, 0);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  result = tsSdpBuffPoolInit(&conn->recv_pool, _recv_pool_name, 0);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  result = tsSdpBuffPoolInit(&conn->send_post, _send_post_name, 0);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * initialize zcopy advertisment tables
   */
  result = tsSdpConnAdvtTableInit(&conn->src_pend);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  result = tsSdpConnAdvtTableInit(&conn->src_actv);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  result = tsSdpConnAdvtTableInit(&conn->snk_pend);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * initialize zcopy iocb tables
   */
  result = tsSdpConnIocbTableInit(&conn->r_pend);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  result = tsSdpConnIocbTableInit(&conn->r_snk);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  result = tsSdpConnIocbTableInit(&conn->w_src);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  result = tsSdpGenericTableInit(&conn->r_src);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  result = tsSdpGenericTableInit(&conn->w_snk);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * defered CM execution timer
   */
  tsKernelTimerInit(&conn->cm_exec);
  /*
   * connection lock
   */
  result = _tsSdpConnLockInit(conn);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * insert connection into lookup table
   */
  result = tsSdpConnTableInsert(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "CREATE: Error <%d> inserting conn <%d> into table. <%d:%d>",
	     result, conn->hashent, _dev_root_s.sk_entry, _dev_root_s.sk_size);
    goto error_conn;
  } /* if */
  /*
   * set reference
   */
  TS_SDP_ATOMIC_SET(conn->refcnt, 1);
  /*
   * hold disconnect messages till established state has been reached.
   */
  conn->flags |= TS_SDP_CONN_F_DIS_HOLD;
  /*
   * done
   */
  return conn;
error_conn:
  kmem_cache_free(_dev_root_s.conn_cache, conn);
error:
  sk_free(sk);
  return NULL;
} /* tsSdpConnAllocate  */
/* --------------------------------------------------------------------- */
/*                                                                       */
/* module public functions                                               */
/*                                                                       */
/* --------------------------------------------------------------------- */

/* ========================================================================= */
/*..tsSdpConnTableMainDump - dump the connection table to /proc */
tINT32 tsSdpConnTableMainDump
(
 tSTR   buffer,
 tINT32 max_size,
 tINT32 start_index,
 long *end_index
)
{
  tSDP_CONN conn;
  tINT32 counter = 0;
  tINT32 offset = 0;
  tUINT64 s_guid;
  tUINT64 d_guid;
  unsigned long flags;

  TS_CHECK_NULL(buffer, -EINVAL);
  TS_CHECK_NULL(end_index, -EINVAL);

  *end_index = 0;
  /*
   * header should only be printed once
   */
  if (0 == start_index) {

    offset += sprintf((buffer + offset),
		      "dst address:port src address:port  ID  comm_id  pid  "
		      "    dst guid         src guid     dlid slid dqpn   "
		      "sqpn   data sent buff'd data rcvd_buff'd "
		      "  data written      data read     src_serv snk_serv\n");

    offset += sprintf((buffer + offset),
		      "---------------- ---------------- ---- -------- ---- "
		      "---------------- ---------------- ---- ---- ------ "
		      "------ ---------------- ---------------- "
		      "---------------- ---------------- -------- --------\n");
  } /* if */
  /*
   * lock table
   */
  TS_SPIN_LOCK(&_dev_root_s.sock_lock, flags);
  /*
   * loop across connections.
   */
  if (start_index < _dev_root_s.sk_size) {

    for (counter = start_index; counter < _dev_root_s.sk_size &&
	   !(TS_SDP_CONN_PROC_MAIN_SIZE > (max_size - offset)); counter++) {

      if (NULL != _dev_root_s.sk_array[counter]) {

	conn = _dev_root_s.sk_array[counter];

	d_guid = cpu_to_be64(*(tUINT64 *)(conn->d_gid + sizeof(tUINT64)));
	s_guid = cpu_to_be64(*(tUINT64 *)(conn->s_gid + sizeof(tUINT64)));

	offset += sprintf((buffer + offset),
			  "%02x.%02x.%02x.%02x:%04x %02x.%02x.%02x.%02x:%04x "
			  "%04x %08x %04x %08x%08x %08x%08x %04x %04x "
			  "%06x %06x %08x%08x %08x%08x %08x%08x %08x%08x "
			  "%08x %08x\n",
			  (conn->dst_addr         & 0xff),
			  ((conn->dst_addr >> 8)  & 0xff),
			  ((conn->dst_addr >> 16) & 0xff),
			  ((conn->dst_addr >> 24) & 0xff),
			  conn->dst_port,
			  (conn->src_addr         & 0xff),
			  ((conn->src_addr >> 8)  & 0xff),
			  ((conn->src_addr >> 16) & 0xff),
			  ((conn->src_addr >> 24) & 0xff),
			  conn->src_port,
			  conn->hashent,
			  conn->comm_id,
			  conn->pid,
			  (tUINT32)((d_guid >> 32) & 0xffffffff),
			  (tUINT32)(d_guid         & 0xffffffff),
			  (tUINT32)((s_guid >> 32) & 0xffffffff),
			  (tUINT32)(s_guid         & 0xffffffff),
			  conn->d_lid,
			  conn->s_lid,
			  conn->d_qpn,
			  conn->s_qpn,
			  (tUINT32)((conn->send_bytes >> 32) & 0xffffffff),
			  (tUINT32)(conn->send_bytes         & 0xffffffff),
			  (tUINT32)((conn->recv_bytes >> 32) & 0xffffffff),
			  (tUINT32)(conn->recv_bytes         & 0xffffffff),
			  (tUINT32)((conn->write_bytes >> 32) & 0xffffffff),
			  (tUINT32)(conn->write_bytes         & 0xffffffff),
			  (tUINT32)((conn->read_bytes >> 32) & 0xffffffff),
			  (tUINT32)(conn->read_bytes         & 0xffffffff),
			  conn->src_serv,
			  conn->snk_serv);
      } /* if */
    } /* for */

    *end_index = counter - start_index;
  } /* if */

  TS_SPIN_UNLOCK(&_dev_root_s.sock_lock, flags);
  return offset;
} /* tsSdpConnTableMainDump */

/* ========================================================================= */
/*..tsSdpConnTableDataDump - dump the connection table to /proc */
tINT32 tsSdpConnTableDataDump
(
 tSTR   buffer,
 tINT32 max_size,
 tINT32 start_index,
 long *end_index
)
{
  struct sock *sk;
  tSDP_CONN conn;
  tINT32 counter = 0;
  tINT32 offset = 0;
  unsigned long flags;

  TS_CHECK_NULL(buffer, -EINVAL);
  TS_CHECK_NULL(end_index, -EINVAL);

  *end_index = 0;
  /*
   * header should only be printed once
   */
  if (0 == start_index) {

    offset += sprintf((buffer + offset),
		      " ID  conn inet r s fl sh "
		      "send_buf recv_buf send q'd recv q'd "
		      "send_seq recv_seq advt_seq smax rmax recv_max lrcv "
		      "lavt rrcv sd sc sp rd rp swqs rbuf sbuf "
		      "us cu send_oob rurg back maxb\n");
    offset += sprintf((buffer + offset),
		      "---- ---- ---- - - -- -- "
		      "-------- -------- -------- -------- "
		      "-------- -------- -------- ---- ---- -------- ---- "
		      "---- ---- -- -- -- -- -- ---- ---- ---- "
		      "-- -- -------- ---- ---- ----\n");
  } /* if */
  /*
   * lock table
   */
  TS_SPIN_LOCK(&_dev_root_s.sock_lock, flags);
  /*
   * loop across connections.
   */
  if (start_index < _dev_root_s.sk_size) {

    for (counter = start_index; counter < _dev_root_s.sk_size &&
	   !(TS_SDP_CONN_PROC_DATA_SIZE > (max_size - offset)); counter++) {

      if (NULL != _dev_root_s.sk_array[counter]) {

	conn = _dev_root_s.sk_array[counter];
	sk   = conn->sk;

	offset += sprintf((buffer + offset),
			  "%04x %04x %04x %01x %01x %02x %02x "
			  "%08x %08x %08x %08x %08x %08x "
			  "%08x %04x %04x %08x %04x %04x %04x %02x %02x "
			  "%02x %02x %02x %04x %04x %04x %02x "
			  "%02x %08x %04x %04x %04x\n",
			  conn->hashent,
			  conn->state,
			  conn->istate,
			  conn->recv_mode,
			  conn->send_mode,
			  conn->flags,
			  conn->shutdown,
			  conn->send_buf,
			  TS_SDP_OS_SK_RCVBUF(sk),
			  conn->send_qud,
			  conn->byte_strm,
			  conn->send_seq,
			  conn->recv_seq,
			  conn->advt_seq,
			  conn->send_max,
			  conn->recv_max,
			  conn->rwin_max,
			  conn->l_recv_bf,
			  conn->l_advt_bf,
			  conn->r_recv_bf,
			  tsSdpGenericTableSize(&conn->send_queue),
			  tsSdpGenericTableSize(&conn->send_ctrl),
			  tsSdpBuffPoolSize(&conn->send_post),
			  tsSdpBuffPoolSize(&conn->recv_pool),
			  tsSdpBuffPoolSize(&conn->recv_post),
			  conn->s_wq_size,
			  conn->recv_size,
			  conn->send_size,
			  conn->send_usig,
			  conn->send_cons,
			  conn->oob_offset,
			  conn->rcv_urg_cnt,
			  conn->backlog_cnt,
			  conn->backlog_max);
      } /* if */
    } /* for */

    *end_index = counter - start_index;
  } /* if */

  TS_SPIN_UNLOCK(&_dev_root_s.sock_lock, flags);
  return offset;
} /* tsSdpConnTableDataDump */

/* ========================================================================= */
/*..tsSdpConnTableRdmaDump - dump the connection table to /proc */
tINT32 tsSdpConnTableRdmaDump
(
 tSTR   buffer,
 tINT32 max_size,
 tINT32 start_index,
 long *end_index
)
{
  tSDP_CONN conn;
  tINT32 counter = 0;
  tINT32 offset = 0;
  unsigned long flags;

  TS_CHECK_NULL(buffer, -EINVAL);
  TS_CHECK_NULL(end_index, -EINVAL);

  *end_index = 0;
  /*
   * header should only be printed once
   */
  if (0 == start_index) {

    offset += sprintf((buffer + offset),
		      " ID  rr rw lr lw "
		      "sa ap aa as rpnd rsnk wsrc rsrc wsnk ra la sc sp "
		      "non_recv non_send "
		      " readq    writeq \n");
    offset += sprintf((buffer + offset),
		      "---- -- -- -- -- "
		      "-- -- -- -- ---- ---- ---- ---- ---- -- -- -- -- "
		      "-------- -------- "
		      "-------- --------\n");
  } /* if */
  /*
   * lock table
   */
  TS_SPIN_LOCK(&_dev_root_s.sock_lock, flags);
  /*
   * loop across connections.
   */
  if (start_index < _dev_root_s.sk_size) {

    for (counter = start_index; counter < _dev_root_s.sk_size &&
	   !(TS_SDP_CONN_PROC_RDMA_SIZE > (max_size - offset)); counter++) {

      if (NULL != _dev_root_s.sk_array[counter]) {

	conn = _dev_root_s.sk_array[counter];

	offset += sprintf((buffer + offset),
			  "%04x %02x %02x %02x %02x %02x %02x %02x %02x "
			  "%04x %04x %04x %04x %04x %02x %02x %02x %02x %08x "
			  "%08x %08x %08x\n",
			  conn->hashent,
			  conn->src_recv,
			  conn->snk_recv,
			  conn->src_sent,
			  conn->snk_sent,
			  conn->sink_actv,
			  tsSdpConnAdvtTableSize(&conn->src_pend),
			  tsSdpConnAdvtTableSize(&conn->src_actv),
			  tsSdpConnAdvtTableSize(&conn->snk_pend),
			  tsSdpConnIocbTableSize(&conn->r_pend),
			  tsSdpConnIocbTableSize(&conn->r_snk),
			  tsSdpConnIocbTableSize(&conn->w_src),
			  tsSdpGenericTableSize(&conn->r_src),
			  tsSdpGenericTableSize(&conn->w_snk),
			  conn->r_max_adv,
			  conn->l_max_adv,
			  conn->s_cur_adv,
			  conn->s_par_adv,
			  conn->nond_recv,
			  conn->nond_send,
			  conn->read_queued,
			  conn->write_queued);
      } /* if */
    } /* for */

    *end_index = counter - start_index;
  } /* if */

  TS_SPIN_UNLOCK(&_dev_root_s.sock_lock, flags);
  return offset;
} /* tsSdpConnTableRdmaDump */

/* ========================================================================= */
/*..tsSdpSoptTableDump - dump the options portion of each conn to /proc */
tINT32 tsSdpSoptTableDump
(
 tSTR   buffer,
 tINT32 max_size,
 tINT32 start_index,
 long *end_index
)
{
  tSDP_CONN conn;
  tINT32 counter = 0;
  tINT32 offset = 0;
  unsigned long flags;

  TS_CHECK_NULL(buffer, -EINVAL);
  TS_CHECK_NULL(end_index, -EINVAL);

  *end_index = 0;
  /*
   * header should only be printed once
   */
  if (0 == start_index) {

    offset += sprintf((buffer + offset),
		      "dst address:port src address:port src zcpy "
		      "snk zcpy nd\n");
    offset += sprintf((buffer + offset),
		      "---------------- ---------------- -------- "
		      "-------- --\n");
  } /* if */
  /*
   * lock table
   */
  TS_SPIN_LOCK(&_dev_root_s.sock_lock, flags);
  /*
   * loop across connections.
   */
  if (start_index < _dev_root_s.sk_size) {

    for (counter = start_index; counter < _dev_root_s.sk_size &&
	   !(TS_SDP_SOPT_PROC_DUMP_SIZE > (max_size - offset)); counter++) {

      if (NULL != _dev_root_s.sk_array[counter]) {

	conn = _dev_root_s.sk_array[counter];

	offset += sprintf((buffer + offset),
			  "%02x.%02x.%02x.%02x:%04x %02x.%02x.%02x.%02x:%04x "
			  "%08x %08x %04x\n",
			  (conn->dst_addr         & 0xff),
			  ((conn->dst_addr >> 8)  & 0xff),
			  ((conn->dst_addr >> 16) & 0xff),
			  ((conn->dst_addr >> 24) & 0xff),
			  conn->dst_port,
			  (conn->src_addr         & 0xff),
			  ((conn->src_addr >> 8)  & 0xff),
			  ((conn->src_addr >> 16) & 0xff),
			  ((conn->src_addr >> 24) & 0xff),
			  conn->src_port,
			  conn->src_zthresh,
			  conn->snk_zthresh,
			  conn->nodelay);
      } /* if */
    } /* for */

    *end_index = counter - start_index;
  } /* if */

  TS_SPIN_UNLOCK(&_dev_root_s.sock_lock, flags);
  return offset;
} /* tsSdpSoptTableDump */

/* ========================================================================= */
/*..tsSdpDeviceTableDump - dump the primary device table to /proc */
tINT32 tsSdpDeviceTableDump
(
 tSTR   buffer,
 tINT32 max_size,
 tINT32 start_index,
 long *end_index
)
{
  tSDP_DEV_PORT port;
  tSDP_DEV_HCA  hca;
  tUINT64 subnet_prefix;
  tUINT64 guid;
  tINT32 hca_count;
  tINT32 port_count;
  tINT32 offset = 0;

  TS_CHECK_NULL(buffer, -EINVAL);
  TS_CHECK_NULL(end_index, -EINVAL);

  *end_index = 0;
  /*
   * header should only be printed once
   */
  if (0 == start_index) {

    offset += sprintf((buffer + offset),
		      "connection table maximum: <%d>\n",
		      _dev_root_s.sk_size);
    offset += sprintf((buffer + offset),
		      "connection table entries: <%d>\n",
		      _dev_root_s.sk_entry);
    offset += sprintf((buffer + offset),
		      "connection table  rover:  <%d>\n",
		      _dev_root_s.sk_rover);
    offset += sprintf((buffer + offset),
		      "HCAs:\n");
  } /* if */
  /*
   * HCA loop
   */
  for (hca = _dev_root_s.hca_list, hca_count = 0;
       NULL != hca;
       hca = hca->next, hca_count++) {

    offset += sprintf((buffer + offset),
		      "  hca %02x: ca <%p> pd <%p> mem <%p> "
		      "l_key <%08x>\n",
		      hca_count,
		      hca->ca,
		      hca->pd,
		      hca->mem_h,
		      hca->l_key);

    for (port = hca->port_list, port_count = 0;
	 NULL != port;
	 port = port->next, port_count++) {

      subnet_prefix = cpu_to_be64(*(tUINT64 *)(port->gid));
      guid = cpu_to_be64(*(tUINT64 *)(port->gid + sizeof(tUINT64)));

      offset += sprintf((buffer + offset),
			"    port %02x: index <%d> gid <%08x%08x:%08x%08x>\n",
			port_count,
			port->index,
			(tUINT32)((subnet_prefix >> 32) & 0xffffffff),
			(tUINT32)(subnet_prefix         & 0xffffffff),
			(tUINT32)((guid >> 32) & 0xffffffff),
			(tUINT32)(guid         & 0xffffffff));
    } /* for */
  } /* for */

  return offset;
} /* tsSdpDeviceTableDump */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* initialization/cleanup functions                                      */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsSdpDevHcaInit -- create hca list */
static tINT32 _tsSdpDevHcaInit
(
 tSDP_DEV_ROOT dev_root
)
{
  tTS_IB_PHYSICAL_BUFFER_STRUCT buffer_list;
  tTS_IB_FMR_POOL_PARAM_STRUCT fmr_param_s;
  tTS_IB_DEVICE_PROPERTIES_STRUCT node_info;
  tTS_IB_DEVICE_HANDLE hca_handle;
  tSDP_DEV_PORT port;
  tSDP_DEV_HCA  hca;
  tINT32 result;
  tINT32 hca_count;
  tINT32 port_count;
  tINT32 fmr_size;

  TS_CHECK_NULL(dev_root, -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "INIT: Probing HCA/Port list.");
  /*
   * first count number of HCA's
   */
  hca_count = 0;

  while (TS_IB_HANDLE_INVALID != tsIbDeviceGetByIndex(hca_count)) {

    hca_count++;
  } /* while */

  fmr_size = TS_SDP_FMR_POOL_SIZE/hca_count;

  for (hca_count = 0;
       TS_IB_HANDLE_INVALID != (hca_handle = tsIbDeviceGetByIndex(hca_count));
       hca_count++) {

    result = tsIbDevicePropertiesGet(hca_handle, &node_info);
    if (0 != result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_FATAL,
	       "INIT: Error <%d> fetching HCA <%x:%d> type.",
	       result, hca_handle, hca_count);
      goto error;
    } /* if */
    /*
     * allocate per-HCA structure
     */
    hca = kmalloc(sizeof(tSDP_DEV_HCA_STRUCT), GFP_KERNEL);
    if (NULL == hca) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_FATAL,
	       "INIT: Error allocating HCA <%x:%d> memory.",
	       hca_handle, hca_count);
      result = -ENOMEM;
      goto error;
    } /* if */
    /*
     * init and insert into list.
     */
    memset(hca, 0, sizeof(tSDP_DEV_HCA_STRUCT));

    hca->next = dev_root->hca_list;
    dev_root->hca_list = hca;

    hca->fmr_pool = NULL;
    hca->mem_h    = TS_IB_HANDLE_INVALID;
    hca->pd       = TS_IB_HANDLE_INVALID;
    hca->ca       = hca_handle;
    /*
     * protection domain
     */
    result = tsIbPdCreate(hca->ca, NULL, &hca->pd);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	       "INIT: Error <%d> creating HCA <%x:%d> protection domain.",
	       result, hca_handle, hca_count);
      goto error;
    } /* if */
    /*
     * memory registration
     */
    buffer_list.address = 0;
    buffer_list.size    = (unsigned long) high_memory - PAGE_OFFSET;

    hca->iova = 0;

    result = tsIbMemoryRegisterPhysical(hca->pd,
					&buffer_list,
					1, /* list_len */
					&hca->iova,
					(unsigned long)(high_memory -
							PAGE_OFFSET),
					0, /* iova_offset */
					TS_IB_ACCESS_LOCAL_WRITE,
					&hca->mem_h,
					&hca->l_key,
					&hca->r_key);
    if (0 != result) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	       "INIT: Error <%d> registering HCA <%x:%d> memory.",
	       result, hca_handle, hca_count);
      goto error;
    } /* if */
    /*
     * FMR allocation
     */
    fmr_param_s.pool_size         = fmr_size;
    fmr_param_s.dirty_watermark   = TS_SDP_FMR_DIRTY_SIZE;
    fmr_param_s.cache             = 1;
    fmr_param_s.max_pages_per_fmr = TS_SDP_IOCB_PAGES_MAX;
    fmr_param_s.access            = (TS_IB_ACCESS_LOCAL_WRITE|
                                     TS_IB_ACCESS_REMOTE_WRITE|
                                     TS_IB_ACCESS_REMOTE_READ);

    fmr_param_s.flush_function = NULL;
    /*
     * create SDP memory pool
     */
    result = tsIbFmrPoolCreate(hca->pd,
                               &fmr_param_s,
                               &hca->fmr_pool);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	       "INIT: Error <%d> creating HCA <%d:%d> fast memory pool.",
	       result, hca->ca, hca_count);
      goto error;
    } /* if */
    /*
     * port allocation
     */
    for (port_count = 0; port_count < node_info.num_port; port_count++) {

      port = kmalloc(sizeof(tSDP_DEV_PORT_STRUCT), GFP_KERNEL);
      if (NULL == port) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_FATAL,
		 "INIT: Error allocating HCA <%d:%d> port <%x:%d> memory.",
		 hca_handle, hca_count, port_count, node_info.num_port);

	result = -ENOMEM;
	goto error;
      } /* if */

      memset(port, 0, sizeof(tSDP_DEV_PORT_STRUCT));

      port->index = port_count + 1;
      port->next = hca->port_list;
      hca->port_list = port;

      result = tsIbGidEntryGet(hca->ca,
			       port->index,
			       0, /* index */
			       port->gid);
      if (0 != result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_FATAL,
		 "INIT: Error <%d> getting GID for HCA <%d:%d> port <%d:%d>",
		 result, hca->ca, hca_count, port->index,
		 node_info.num_port);
	goto error;
      } /* if */
    } /* for */
  } /* for */

  return 0;
error:
  return result;
} /* _tsSdpDevHcaInit */

/* ========================================================================= */
/*.._tsSdpDevHcaCleanup -- delete hca list */
static tINT32 _tsSdpDevHcaCleanup
(
 tSDP_DEV_ROOT dev_root
)
{
  tSDP_DEV_PORT port;
  tSDP_DEV_HCA  hca;

  TS_CHECK_NULL(dev_root, -EINVAL);
  /*
   * free all hca/ports
   */
  for (hca = dev_root->hca_list; NULL != hca; hca = dev_root->hca_list) {

    dev_root->hca_list = hca->next;
    hca->next = NULL;

    for (port = hca->port_list; NULL != port; port = hca->port_list) {

      hca->port_list = port->next;
      port->next = NULL;

      kfree(port);
    } /* for */

    if (NULL != hca->fmr_pool) {

      (void)tsIbFmrPoolDestroy(hca->fmr_pool);
    } /* if */

    if (TS_IB_HANDLE_INVALID != hca->mem_h) {

      (void)tsIbMemoryDeregister(hca->mem_h);
    } /* if */

    if (TS_IB_HANDLE_INVALID != hca->pd) {

      (void)tsIbPdDestroy(hca->pd);
    } /* if */

    kfree(hca);
  } /* for */

  return 0;
} /* _tsSdpDevHcaCleanup */

/* ========================================================================= */
/*..tsSdpConnTableInit -- create a sdp connection table */
tINT32 tsSdpConnTableInit
(
 tINT32 proto_family,
 tINT32 conn_size,
 tINT32 buff_min,
 tINT32 buff_max
)
{
  tINT32 result;
  tINT32 byte_size;
  tINT32 page_size;

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "INIT: creating connection table.");

  memset(&_dev_root_s, 0, sizeof(tSDP_DEV_ROOT_STRUCT));
  /*
   * list
   */
  _dev_root_s.listen_list = NULL;
  _dev_root_s.bind_list   = NULL;

  TS_SPIN_LOCK_INIT(&_dev_root_s.sock_lock);
  TS_SPIN_LOCK_INIT(&_dev_root_s.bind_lock);
  TS_SPIN_LOCK_INIT(&_dev_root_s.listen_lock);
  /*
   * Initialize IB
   */
  _dev_root_s.proto = proto_family;
  /*
   * Get HCA/port list
   */
  result = _tsSdpDevHcaInit(&_dev_root_s);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
             "INIT: Error <%d> building HCA/port list.", result);
    goto error_hca;
  } /* if */
  /*
   * create socket table
   */
  if (!(0 < conn_size)) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
             "INIT: Invalid connection table size. <%d>", conn_size);
    result = -EINVAL;
    goto error_size;
  } /* if */

  byte_size = conn_size * sizeof(tSDP_CONN);
  page_size = (byte_size >> 12) + ((0xfff & byte_size) > 0 ? 1 : 0);
  for (_dev_root_s.sk_ordr = 0;
       (1 << _dev_root_s.sk_ordr) < page_size;
       _dev_root_s.sk_ordr++);

  _dev_root_s.sk_array = (tPTR)__get_free_pages(GFP_KERNEL,
						_dev_root_s.sk_ordr);
  if (NULL == _dev_root_s.sk_array) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
             "INIT: Failed to create connection table. <%d:%d:%d>",
	     byte_size, page_size, _dev_root_s.sk_ordr);
    result = -ENOMEM;
    goto error_array;
  } /* if */

  memset(_dev_root_s.sk_array, 0, byte_size);
  _dev_root_s.sk_size = conn_size - 1; /* top is reserved for invalid */
  /*
   * IOCB table
   */
  result = tsSdpConnIocbMainInit();
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "INIT: Error <%d> initializing SDP IOCB table <%d>", result);
    goto error_iocb;
  } /* if */
  /*
   * buffer memory
   */
  result =  tsSdpBuffMainInit(buff_min, buff_max);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
             "INIT: Error <%d> initializing buffer pool.", result);
    goto error_buff;
  } /* if */

  _dev_root_s.conn_cache = kmem_cache_create("SdpConnCache",
					     sizeof(tSDP_CONN_STRUCT),
					     0, SLAB_HWCACHE_ALIGN,
					     NULL, NULL);
  if (NULL == _dev_root_s.conn_cache) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
             "INIT: Failed to initialize connection cache.");
    result = -ENOMEM;
    goto error_conn;
  } /* if */
  /*
   * start listening
   */
  result = tsSdpPostListenStart(&_dev_root_s);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "INIET: failed to listen for connections on HCA. <%d>", result);
    goto error_listen;
  } /* if */

  return 0;
error_listen:
  kmem_cache_destroy(_dev_root_s.conn_cache);
error_conn:
  (void)tsSdpBuffMainDestroy();
error_buff:
  (void)tsSdpConnIocbMainCleanup();
error_iocb:
  free_pages((unsigned long) _dev_root_s.sk_array, _dev_root_s.sk_ordr);
error_array:
error_size:
error_hca:
  (void)_tsSdpDevHcaCleanup(&_dev_root_s);
  return result;
} /* tsSdpConnTableInit */

/* ========================================================================= */
/*..tsSdpConnTableClear -- destroy connection managment and tables */
tINT32 tsSdpConnTableClear
(
 void
)
{
#if 0
  tSDP_CONN conn;
  /*
   * drain all the connections
   */
  while (NULL != (conn = _dev_root_s.conn_list)) {


  } /* while */
#endif
  /*
   * delete list of HCAs/PORTs
   */
  (void)_tsSdpDevHcaCleanup(&_dev_root_s);
  /*
   * drop socket table
   */
  free_pages((unsigned long) _dev_root_s.sk_array, _dev_root_s.sk_ordr);
  /*
   * delete conn cache
   */
  kmem_cache_destroy(_dev_root_s.conn_cache);
  /*
   * stop listening
   */
  (void)tsSdpPostListenStop(&_dev_root_s);
  /*
   * delete buffer memory
   */
  (void)tsSdpBuffMainDestroy();
  /*
   * delete IOCB table
   */
  (void)tsSdpConnIocbMainCleanup();

  return 0;
} /* tsSdpConnTableClear */
