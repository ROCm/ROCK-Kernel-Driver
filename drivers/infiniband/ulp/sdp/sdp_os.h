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

  $Id: sdp_os.h 35 2004-04-09 05:34:32Z roland $
*/

#ifndef _TS_SDP_OS_H
#define _TS_SDP_OS_H
/*
 * topspin specific includes.
 */
#include <ib_legacy_types.h>
#include "sdp_types.h"
#include "sdp_proto.h"
/* ---------------------------------------------------------------------- */
/*                                                                        */
/* SDP connection structure                                               */
/*                                                                        */
/* ---------------------------------------------------------------------- */

#define TS_SDP_OS_GET_PID()    (current->pid)
/*
 * Linux kernel 2.6 vs. 2.4 differences.
 */
#ifdef TS_KERNEL_2_6
#define TS_SDP_GET_CONN(sk) *((tSDP_CONN *)&(sk)->sk_protinfo)
#define TS_SDP_SET_CONN(sk, conn) *((tSDP_CONN *)&(sk)->sk_protinfo) = (conn)

#define TS_SDP_OS_CONN_SET_ERR(conn, val) \
        (conn)->error = (conn)->sk->sk_err = (val)
#define TS_SDP_OS_CONN_GET_ERR(conn)      (conn)->error

#define TS_SDP_OS_SK_ERR(sk)          ((sk)->sk_err)
#define TS_SDP_OS_SK_SOCKET(sk)       ((sk)->sk_socket)
#define TS_SDP_OS_SK_CB_LOCK(sk)      ((sk)->sk_callback_lock)
#define TS_SDP_OS_SK_SLEEP(sk)        ((sk)->sk_sleep)
#define TS_SDP_OS_SK_STAMP(sk)        ((sk)->sk_stamp)
#define TS_SDP_OS_SK_LINGERTIME(sk)   ((sk)->sk_lingertime)
#define TS_SDP_OS_SK_RCVLOWAT(sk)     ((sk)->sk_rcvlowat)
#define TS_SDP_OS_SK_DEBUG(sk)        ((sk)->sk_debug)
#define TS_SDP_OS_SK_LOCALROUTE(sk)   ((sk)->sk_localroute)
#define TS_SDP_OS_SK_SNDBUF(sk)       ((sk)->sk_sndbuf)
#define TS_SDP_OS_SK_RCVBUF(sk)       ((sk)->sk_rcvbuf)
#define TS_SDP_OS_SK_NO_CHECK(sk)     ((sk)->sk_no_check)
#define TS_SDP_OS_SK_PRIORITY(sk)     ((sk)->sk_priority)
#define TS_SDP_OS_SK_RCVTSTAMP(sk)    ((sk)->sk_rcvtstamp)
#define TS_SDP_OS_SK_RCVTIMEOUT(sk)   ((sk)->sk_rcvtimeo)
#define TS_SDP_OS_SK_SNDTIMEOUT(sk)   ((sk)->sk_sndtimeo)
#define TS_SDP_OS_SK_PROTOCOL(sk)     ((sk)->sk_protocol)
#define TS_SDP_OS_SK_REUSE(sk)        ((sk)->__sk_common.skc_reuse)
#define TS_SDP_OS_SK_BOUND_IF(sk)     ((sk)->__sk_common.skc_bound_dev_if)
#define TS_SDP_OS_SK_FAMILY(sk)       ((sk)->__sk_common.skc_family)

#define TS_SDP_OS_SK_DESTRUCT(sk)     ((sk)->sk_destruct)
#define TS_SDP_OS_SK_WRITE_SPACE(sk)  ((sk)->sk_write_space)
#define TS_SDP_OS_SK_STATE_CHANGE(sk) ((sk)->sk_state_change)
#define TS_SDP_OS_SK_DATA_READY(sk)   ((sk)->sk_data_ready)
#define TS_SDP_OS_SK_ERROR_REPORT(sk) ((sk)->sk_error_report)

#define TS_SDP_OS_SK_LINGER(sk)       sock_flag((sk), SOCK_LINGER)
#define TS_SDP_OS_SK_URGINLINE(sk)    sock_flag((sk), SOCK_URGINLINE)

#define TS_SDP_OS_SK_INET_FREEBIND(sk)  ((inet_sk(sk))->freebind)
#define TS_SDP_OS_SK_INET_RCV_SADDR(sk) ((inet_sk(sk))->rcv_saddr)
#define TS_SDP_OS_SK_INET_SADDR(sk)     ((inet_sk(sk))->saddr)
#define TS_SDP_OS_SK_INET_SPORT(sk)     ((inet_sk(sk))->sport)
#define TS_SDP_OS_SK_INET_DADDR(sk)     ((inet_sk(sk))->daddr)
#define TS_SDP_OS_SK_INET_DPORT(sk)     ((inet_sk(sk))->dport)
#define TS_SDP_OS_SK_INET_NUM(sk)       ((inet_sk(sk))->num)

#define TS_SDP_OS_SK_USERLOCKS_GET(sk)         ((sk)->sk_userlocks)
#define TS_SDP_OS_SK_USERLOCKS_SET(sk, val)    ((sk)->sk_userlocks |= (val))
#else /* TS_KERNEL_2_6 */
#define TS_SDP_GET_CONN(sk) *((tSDP_CONN *)&(sk)->tp_pinfo)
#define TS_SDP_SET_CONN(sk, conn) *((tSDP_CONN *)&(sk)->tp_pinfo) = (conn)

#define TS_SDP_OS_CONN_SET_ERR(conn, val) \
        (conn)->error = (conn)->sk->err = (val)
#define TS_SDP_OS_CONN_GET_ERR(conn)      (conn)->error

#define TS_SDP_OS_SK_ERR(sk)          ((sk)->err)
#define TS_SDP_OS_SK_SOCKET(sk)       ((sk)->socket)
#define TS_SDP_OS_SK_CB_LOCK(sk)      ((sk)->callback_lock)
#define TS_SDP_OS_SK_SLEEP(sk)        ((sk)->sleep)
#define TS_SDP_OS_SK_STAMP(sk)        ((sk)->stamp)
#define TS_SDP_OS_SK_LINGERTIME(sk)   ((sk)->lingertime)
#define TS_SDP_OS_SK_RCVLOWAT(sk)     ((sk)->rcvlowat)
#define TS_SDP_OS_SK_DEBUG(sk)        ((sk)->debug)
#define TS_SDP_OS_SK_LOCALROUTE(sk)   ((sk)->localroute)
#define TS_SDP_OS_SK_SNDBUF(sk)       ((sk)->sndbuf)
#define TS_SDP_OS_SK_RCVBUF(sk)       ((sk)->rcvbuf)
#define TS_SDP_OS_SK_NO_CHECK(sk)     ((sk)->no_check)
#define TS_SDP_OS_SK_PRIORITY(sk)     ((sk)->priority)
#define TS_SDP_OS_SK_RCVTSTAMP(sk)    ((sk)->rcvtstamp)
#define TS_SDP_OS_SK_RCVTIMEOUT(sk)   ((sk)->rcvtimeo)
#define TS_SDP_OS_SK_SNDTIMEOUT(sk)   ((sk)->sndtimeo)
#define TS_SDP_OS_SK_PROTOCOL(sk)     ((sk)->protocol)
#define TS_SDP_OS_SK_REUSE(sk)        ((sk)->reuse)
#define TS_SDP_OS_SK_BOUND_IF(sk)     ((sk)->bound_dev_if)
#define TS_SDP_OS_SK_FAMILY(sk)       ((sk)->family)

#define TS_SDP_OS_SK_DESTRUCT(sk)     ((sk)->destruct)
#define TS_SDP_OS_SK_WRITE_SPACE(sk)  ((sk)->write_space)
#define TS_SDP_OS_SK_STATE_CHANGE(sk) ((sk)->state_change)
#define TS_SDP_OS_SK_DATA_READY(sk)   ((sk)->data_ready)
#define TS_SDP_OS_SK_ERROR_REPORT(sk) ((sk)->error_report)

#define TS_SDP_OS_SK_LINGER(sk)       ((sk)->linger)
#define TS_SDP_OS_SK_URGINLINE(sk)    ((sk)->urginline)

#define TS_SDP_OS_SK_INET_FREEBIND(sk)  ((sk)->protinfo.af_inet.freebind)
#define TS_SDP_OS_SK_INET_RCV_SADDR(sk) ((sk)->rcv_saddr)
#define TS_SDP_OS_SK_INET_SADDR(sk)     ((sk)->saddr)
#define TS_SDP_OS_SK_INET_SPORT(sk)     ((sk)->sport)
#define TS_SDP_OS_SK_INET_DADDR(sk)     ((sk)->daddr)
#define TS_SDP_OS_SK_INET_DPORT(sk)     ((sk)->dport)
#define TS_SDP_OS_SK_INET_NUM(sk)       ((sk)->num)

#define TS_SDP_OS_SK_USERLOCKS_GET(sk)         ((sk)->userlocks)
#define TS_SDP_OS_SK_USERLOCKS_SET(sk, val)    ((sk)->userlocks |= (val))
#endif /* TS_KERNEL_2_6 */

/* ---------------------------------------------------------------------- */
/*                                                                        */
/* SDP connection inline functions                                        */
/*                                                                        */
/* ---------------------------------------------------------------------- */
/* ======================================================================== */
/*..__tsSdpOSConnWakeReady -- wake connection for ready event. */
static inline void __tsSdpOSConnWakeReady
(
 struct sock *sk
)
{
  tsSdpInetWakeGeneric(TS_SDP_GET_CONN(sk));
} /* __tsSdpOSConnWakeReady */

/* ======================================================================== */
/*..__tsSdpOSConnWakeError -- wake connection for error event. */
static inline void __tsSdpOSConnWakeError
(
 struct sock *sk
)
{
  tsSdpInetWakeError(TS_SDP_GET_CONN(sk));
} /* __tsSdpOSConnWakeError */

/* ======================================================================== */
/*..__tsSdpOSConnWakeRead -- wake connection for read event. */
static inline void __tsSdpOSConnWakeRead
(
 struct sock *sk,
 int len
)
{
  tsSdpInetWakeRecv(TS_SDP_GET_CONN(sk), len);
} /* __tsSdpOSConnWakeRead */

/* ======================================================================== */
/*..__tsSdpOSConnWakeWrite -- wake connection for write event. */
static inline void __tsSdpOSConnWakeWrite
(
 struct sock *sk
)
{
  tsSdpInetWakeSend(TS_SDP_GET_CONN(sk));
} /* __tsSdpOSConnWakeWrite */

#endif /* _TS_SDP_OS_H */
