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

  $Id: sdp_proto.h 35 2004-04-09 05:34:32Z roland $
*/

#ifndef _TS_SDP_PROTO_H
#define _TS_SDP_PROTO_H
/*
 * types used in the prototype file.
 */
#include <ib_legacy_types.h>
#include <ts_ib_core.h>
#include "sdp_types.h"
/* --------------------------------------------------------------------- */
/* Buffer managment                                                      */
/* --------------------------------------------------------------------- */
tSDP_BUFF tsSdpBuffMainGet(
  void
  );

tINT32 tsSdpBuffMainPut(
  tSDP_BUFF buff
  );

tINT32 tsSdpBuffMainChainPut(
  tSDP_BUFF buff,
  tINT32 count
  );

tINT32 tsSdpBuffMainChainLink(
  tSDP_BUFF head,
  tSDP_BUFF buff
  );

tINT32 tsSdpBuffMainSize(
  void
  );

tINT32 tsSdpBuffMainBuffSize(
  void
  );

tINT32 tsSdpBuffPoolInit(
  tSDP_POOL pool,
  tSTR     name,
  tUINT32  size
  );

tINT32 tsSdpBuffPoolClear(
  tSDP_POOL pool
  );

tINT32 tsSdpBuffPoolRemove(
  tSDP_BUFF buff
  );

tSDP_BUFF tsSdpBuffPoolGet(
  tSDP_POOL pool
  );

tSDP_BUFF tsSdpBuffPoolGetHead(
  tSDP_POOL pool
  );

tSDP_BUFF tsSdpBuffPoolGetTail(
  tSDP_POOL pool
  );

tSDP_BUFF tsSdpBuffPoolLookHead(
  tSDP_POOL pool
  );

tSDP_BUFF tsSdpBuffPoolLookTail(
  tSDP_POOL pool
  );

tINT32 tsSdpBuffPoolPut(
  tSDP_POOL pool,
  tSDP_BUFF buff
  );

tINT32 tsSdpBuffPoolPutHead(
  tSDP_POOL pool,
  tSDP_BUFF buff
  );

tINT32 tsSdpBuffPoolPutTail(
  tSDP_POOL pool,
  tSDP_BUFF buff
  );

tSDP_BUFF tsSdpBuffPoolFetchHead(
  tSDP_POOL           pool,
  tSDP_BUFF_TEST_FUNC test_func,
  tPTR                usr_arg
  );

tSDP_BUFF tsSdpBuffPoolFetchTail(
  tSDP_POOL           pool,
  tSDP_BUFF_TEST_FUNC test_func,
  tPTR                usr_arg
  );

tINT32 tsSdpBuffPoolTraverseTail(
  tSDP_POOL           pool,
  tSDP_BUFF_TRAV_FUNC trav_func,
  tPTR                usr_arg
  );

tINT32 tsSdpBuffPoolTraverseHead(
  tSDP_POOL           pool,
  tSDP_BUFF_TRAV_FUNC trav_func,
  tPTR                usr_arg
  );

tSDP_BUFF tsSdpBuffPoolFetch(
  tSDP_POOL           pool,
  tSDP_BUFF_TEST_FUNC test_func,
  tPTR                usr_arg
  );

tINT32 tsSdpBuffMainInit(
  tUINT32 buff_min,
  tUINT32 buff_max
  );

void tsSdpBuffMainDestroy(
  void
  );

tINT32 tsSdpBuffMainDump(
  tSTR   buffer,
  tINT32 max_size,
  tINT32 start_index,
  long *end_index
  );

/* --------------------------------------------------------------------- */
/* Wall between userspace protocol and SDP protocol proper               */
/* --------------------------------------------------------------------- */

tINT32 tsSdpConnConnect(
  tSDP_CONN    conn
  );

tINT32 tsSdpConnAccept(
  tSDP_CONN conn
  );

tINT32 tsSdpConnReject(
  tSDP_CONN conn
  );

tINT32 tsSdpConnConfirm(
  tSDP_CONN conn
  );

tINT32 tsSdpConnFailed(
  tSDP_CONN conn
  );

tINT32 tsSdpConnBuffAck(
  tSDP_CONN conn,
  tUINT32   size
  );

tINT32 tsSdpConnClose(
  tSDP_CONN conn
  );

tINT32 tsSdpConnClosing(
  tSDP_CONN conn
  );

tINT32 tsSdpConnAbort(
  tSDP_CONN conn
  );

tINT32 tsSdpSockConnect(
  tSDP_CONN conn
  );

tINT32 tsSdpSockAccept(
  tSDP_CONN conn
  );

tINT32 tsSdpSockReject(
  tSDP_CONN conn,
  tINT32    error
  );

tINT32 tsSdpSockConfirm(
  tSDP_CONN conn
  );

tINT32 tsSdpSockFailed(
  tSDP_CONN conn,
  tINT32    error
  );

tINT32 tsSdpSockBuffRecv(
  tSDP_CONN conn,
  tSDP_BUFF buff
  );

tINT32 tsSdpSockClose(
  tSDP_CONN conn
  );

tINT32 tsSdpSockClosing(
  tSDP_CONN conn
  );

tINT32 tsSdpSockAbort(
  tSDP_CONN conn
  );

tINT32 tsSdpSockDrop(
  tSDP_CONN conn
  );

tINT32 tsSdpAbort(
  tSDP_CONN conn
  );

/* --------------------------------------------------------------------- */
/* Zcopy advertisment managment                                          */
/* --------------------------------------------------------------------- */
tINT32 tsSdpConnAdvtMainInit(
  void
  );

tINT32 tsSdpConnAdvtMainCleanup(
  void
  );

tSDP_ADVT_TABLE tsSdpConnAdvtTableCreate(
  tINT32  *result
  );

tINT32 tsSdpConnAdvtTableInit(
  tSDP_ADVT_TABLE table
  );

tINT32 tsSdpConnAdvtTableClear(
  tSDP_ADVT_TABLE table
  );

tINT32 tsSdpConnAdvtTableDestroy(
  tSDP_ADVT_TABLE table
  );

tSDP_ADVT tsSdpConnAdvtCreate(
  void
  );

tINT32 tsSdpConnAdvtDestroy(
  tSDP_ADVT advt
  );

tSDP_ADVT tsSdpConnAdvtTableGet(
  tSDP_ADVT_TABLE table
  );

tSDP_ADVT tsSdpConnAdvtTableLook(
  tSDP_ADVT_TABLE table
  );

tINT32 tsSdpConnAdvtTablePut(
  tSDP_ADVT_TABLE table,
  tSDP_ADVT       advt
  );
/* --------------------------------------------------------------------- */
/* Zcopy IOCB managment                                                  */
/* --------------------------------------------------------------------- */
tINT32 tsSdpConnIocbMainInit(
  void
  );

tINT32 tsSdpConnIocbMainCleanup(
  void
  );

tSDP_IOCB_TABLE tsSdpConnIocbTableCreate(
  tINT32  *result
  );

tINT32 tsSdpConnIocbTableInit(
  tSDP_IOCB_TABLE table
  );

tINT32 tsSdpConnIocbTableClear(
  tSDP_IOCB_TABLE table
  );

tINT32 tsSdpConnIocbTableDestroy(
  tSDP_IOCB_TABLE table
  );

tSDP_IOCB tsSdpConnIocbCreate(
  void
  );

tINT32 tsSdpConnIocbDestroy(
  tSDP_IOCB iocb
  );

tSDP_IOCB tsSdpConnIocbTableLook(
  tSDP_IOCB_TABLE table
  );

tSDP_IOCB tsSdpConnIocbTableGetHead(
  tSDP_IOCB_TABLE table
  );

tSDP_IOCB tsSdpConnIocbTableGetTail(
  tSDP_IOCB_TABLE table
  );

tINT32 tsSdpConnIocbTablePutHead(
  tSDP_IOCB_TABLE table,
  tSDP_IOCB       iocb
  );

tINT32 tsSdpConnIocbTablePutTail(
  tSDP_IOCB_TABLE table,
  tSDP_IOCB       iocb
  );

tSDP_IOCB tsSdpConnIocbTableGetKey(
  tSDP_IOCB_TABLE table,
  tUINT32         key
  );

tSDP_IOCB tsSdpConnIocbTableLookup(
  tSDP_IOCB_TABLE table,
  tUINT32         key
  );

tINT32 tsSdpConnIocbTableCancel(
  tSDP_IOCB_TABLE table,
  tUINT32         mask,
  tINT32          comp
  );

tINT32 tsSdpConnIocbTableRemove(
  tSDP_IOCB iocb
  );

tINT32 tsSdpConnIocbRegister(
  tSDP_IOCB iocb,
  tSDP_CONN conn
  );

tINT32 tsSdpConnIocbRelease(
  tSDP_IOCB iocb
  );

tINT32 tsSdpConnIocbComplete(
  tSDP_IOCB  iocb,
  tINT32     status
  );
/* --------------------------------------------------------------------- */
/* Generic object managment                                              */
/* --------------------------------------------------------------------- */
tINT32 tsSdpGenericTableRemove(
  tSDP_GENERIC element
  );

tSDP_GENERIC tsSdpGenericTableGetAll(
  tSDP_GENERIC_TABLE table
  );

tSDP_GENERIC tsSdpGenericTableGetHead(
  tSDP_GENERIC_TABLE table
  );

tSDP_GENERIC tsSdpGenericTableGetTail(
  tSDP_GENERIC_TABLE table
  );

tINT32 tsSdpGenericTablePutHead(
  tSDP_GENERIC_TABLE table,
  tSDP_GENERIC element
  );

tINT32 tsSdpGenericTablePutTail(
  tSDP_GENERIC_TABLE table,
  tSDP_GENERIC element
  );

tSDP_GENERIC tsSdpGenericTableLookHead(
  tSDP_GENERIC_TABLE table
  );

tSDP_GENERIC tsSdpGenericTableLookTail(
  tSDP_GENERIC_TABLE table
  );

tINT32 tsSdpGenericTableTypeHead(
  tSDP_GENERIC_TABLE table
  );

tINT32 tsSdpGenericTableTypeTail(
  tSDP_GENERIC_TABLE table
  );

tSDP_GENERIC tsSdpGenericTableLookTypeHead(
  tSDP_GENERIC_TABLE table,
  tSDP_GENERIC_TYPE  type
  );

tSDP_GENERIC tsSdpGenericTableLookTypeTail(
  tSDP_GENERIC_TABLE table,
  tSDP_GENERIC_TYPE  type
  );

tSDP_GENERIC tsSdpGenericTableLookup(
  tSDP_GENERIC_TABLE       table,
  tSDP_GENERIC_LOOKUP_FUNC lookup_func,
  tPTR                     arg
  );

tINT32 tsSdpGenericTableTypeSize(
  tSDP_GENERIC_TABLE table,
  tSDP_GENERIC_TYPE  type
  );

tINT32 tsSdpGenericTableSize(
  tSDP_GENERIC_TABLE table
  );

tSDP_GENERIC_TABLE tsSdpGenericTableCreate(
  tINT32  *result
  );

tINT32 tsSdpGenericTableInit(
  tSDP_GENERIC_TABLE table
  );

tINT32 tsSdpGenericTableClear(
  tSDP_GENERIC_TABLE table
  );

tINT32 tsSdpGenericTableDestroy(
  tSDP_GENERIC_TABLE table
  );

tINT32 tsSdpGenericMainInit(
  void
  );

tINT32 tsSdpGenericMainCleanup(
  void
  );
/* --------------------------------------------------------------------- */
/* proc entry managment                                                  */
/* --------------------------------------------------------------------- */
tINT32 tsSdpProcFsInit(
  void
  );

tINT32 tsSdpProcFsCleanup(
  void
  );
/* --------------------------------------------------------------------- */
/* connection table                                                      */
/* --------------------------------------------------------------------- */
tINT32 tsSdpConnTableInit(
  tINT32 proto_family,
  tINT32 conn_size,
  tINT32 buff_size,
  tINT32 buff_num
  );

tINT32 tsSdpConnTableClear(
  void
  );

tINT32 tsSdpConnTableMainDump(
  tSTR   buffer,
  tINT32 max_size,
  tINT32 start_index,
  long *end_index
  );

tINT32 tsSdpConnTableDataDump(
  tSTR   buffer,
  tINT32 max_size,
  tINT32 start_index,
  long *end_index
  );

tINT32 tsSdpConnTableRdmaDump(
  tSTR   buffer,
  tINT32 max_size,
  tINT32 start_index,
  long *end_index
  );

tINT32 tsSdpSoptTableDump(
  tSTR   buffer,
  tINT32 max_size,
  tINT32 start_index,
  long *end_index
  );

tINT32 tsSdpDeviceTableDump(
  tSTR   buffer,
  tINT32 max_size,
  tINT32 start_index,
  long *end_index
  );

tINT32 tsSdpConnTableRemove(
  tSDP_CONN conn
  );

tSDP_CONN tsSdpConnTableLookup(
  tINT32 entry
  );

tSDP_CONN tsSdpConnAllocate(
  tINT32             priority,
  tTS_IB_CM_COMM_ID  comm_id
  );

tINT32 tsSdpConnAllocateIb(
  tSDP_CONN conn,
  tTS_IB_DEVICE_HANDLE device,
  tTS_IB_PORT hw_port
  );

tINT32 tsSdpConnDestruct(
 tSDP_CONN conn
 );

void tsSdpInetWakeSend(
 tSDP_CONN conn
 );

void tsSdpInetWakeGeneric(
  tSDP_CONN conn
  );

void tsSdpInetWakeRecv(
  tSDP_CONN conn,
  tINT32 len
  );

void tsSdpInetWakeError(
  tSDP_CONN conn
  );

void tsSdpInetWakeUrg(
  tSDP_CONN conn
  );
/* --------------------------------------------------------------------- */
/* port/queue managment                                                  */
/* --------------------------------------------------------------------- */
tINT32 tsSdpInetAcceptQueuePut(
  tSDP_CONN listen_conn,
  tSDP_CONN accept_conn
  );

tSDP_CONN tsSdpInetAcceptQueueGet(
  tSDP_CONN listen_conn
  );

tINT32 tsSdpInetAcceptQueueRemove(
  tSDP_CONN accept_conn
  );

tINT32 tsSdpInetListenStart(
  tSDP_CONN listen_conn
  );

tINT32 tsSdpInetListenStop(
  tSDP_CONN listen_conn
  );

tSDP_CONN tsSdpInetListenLookup(
  tUINT32 addr,
  tUINT16 port
  );

tINT32 tsSdpInetPortGet(
  tSDP_CONN conn,
  tUINT16 port
  );

tINT32 tsSdpInetPortPut(
  tSDP_CONN conn
  );

tINT32 tsSdpInetPortInherit(
  tSDP_CONN parent,
  tSDP_CONN child
  );

/* --------------------------------------------------------------------- */
/* post functions                                                        */
/* --------------------------------------------------------------------- */
tINT32 tsSdpPostListenStart(
  tSDP_DEV_ROOT dev_root
  );

tINT32 tsSdpPostListenStop(
  tSDP_DEV_ROOT dev_root
  );

tINT32 tsSdpPostMsgHello(
  tSDP_CONN  conn
  );

tINT32 tsSdpPostMsgHelloAck(
  tSDP_CONN  conn
  );

tINT32 tsSdpCmDisconnect(
  tSDP_CONN  conn
  );

tINT32 tsSdpCmReject(
  tSDP_CONN  conn
  );

tINT32 tsSdpCmFailed(
  tSDP_CONN  conn
  );

tINT32 tsSdpCmConfirm(
  tSDP_CONN  conn
  );

tINT32 tsSdpRecvPost(
  tSDP_CONN  conn
  );

tINT32 tsSdpSendFlush(
  tSDP_CONN  conn
  );

tINT32 tsSdpSendCtrlAck(
  tSDP_CONN  conn
  );

tINT32 tsSdpSendCtrlDisconnect(
  tSDP_CONN  conn
  );

tINT32 tsSdpSendCtrlAbort(
  tSDP_CONN  conn
  );

tINT32 tsSdpSendCtrlSendSm(
  tSDP_CONN  conn
  );

tINT32 tsSdpSendCtrlSnkAvail(
  tSDP_CONN conn,
  tUINT32   size,
  tUINT32   rkey,
  tUINT64   addr
  );

tINT32 tsSdpSendCtrlResizeBuffAck(
  tSDP_CONN  conn,
  tUINT32    size
  );

tINT32 tsSdpSendCtrlRdmaRdComp(
  tSDP_CONN conn,
  tINT32    size
  );

tINT32 tsSdpSendCtrlRdmaWrComp(
  tSDP_CONN  conn,
  tUINT32    size
  );

tINT32 tsSdpSendCtrlModeChange(
  tSDP_CONN  conn,
  tUINT8     mode
  );

tINT32 tsSdpSendCtrlSrcCancel(
  tSDP_CONN  conn
  );

tINT32 tsSdpSendCtrlSnkCancel(
  tSDP_CONN  conn
  );

tINT32 tsSdpSendCtrlSnkCancelAck(
  tSDP_CONN  conn
  );

/* --------------------------------------------------------------------- */
/* inet functions                                                        */
/* --------------------------------------------------------------------- */
/* --------------------------------------------------------------------- */
/* event functions                                                       */
/* --------------------------------------------------------------------- */
tINT32 tsSdpEventLocked(
  tTS_IB_CQ_ENTRY comp,
  tSDP_CONN       conn
  );

void tsSdpEventHandler(
  tTS_IB_CQ_HANDLE cq,
  tPTR             arg
  );

tTS_IB_CM_CALLBACK_RETURN tsSdpCmEventHandler(
  tTS_IB_CM_EVENT   event,
  tTS_IB_CM_COMM_ID comm_id,
  tPTR params,
  tPTR arg
  );

tINT32 tsSdpEventRecv(
  tSDP_CONN       conn,
  tTS_IB_CQ_ENTRY comp
  );

tINT32 tsSdpEventSend(
  tSDP_CONN       conn,
  tTS_IB_CQ_ENTRY comp
  );

tINT32 tsSdpEventRead(
  tSDP_CONN       conn,
  tTS_IB_CQ_ENTRY comp
  );

tINT32 tsSdpEventWrite(
  tSDP_CONN       conn,
  tTS_IB_CQ_ENTRY comp
  );

/* --------------------------------------------------------------------- */
/* internal connection lock functions                                    */
/* --------------------------------------------------------------------- */
void tsSdpConnLockInternalLock(
  tSDP_CONN conn
#if defined(TS_KERNEL_2_6)
/* Work around the temporary workaround for THCA locking bugs :) */
  ,unsigned long *flags
#endif
  );

void tsSdpConnLockInternalUnlock(
  tSDP_CONN conn
  );

void tsSdpConnLockInternalRelock(
  tSDP_CONN conn
  );

tINT32 tsSdpConnCqDrain(
  tTS_IB_CQ_HANDLE cq,
  tSDP_CONN conn
  );

/* --------------------------------------------------------------------- */
/* address and link layer services                                       */
/* --------------------------------------------------------------------- */
tINT32 tsSdpLinkAddrInit(
  void
  );

tINT32 tsSdpLinkAddrCleanup(
  void
  );

tINT32 tsSdpIpoibWaitTableDump(
  tSTR   buffer,
  tINT32 max_size,
  tINT32 start_index,
  long *end_index
  );

tINT32 tsSdpPathElementTableDump(
  tSTR   buffer,
  tINT32 max_size,
  tINT32 start_index,
  long *end_index
  );
/* --------------------------------------------------------------------- */
/* DATA transport                                                        */
/* --------------------------------------------------------------------- */
tINT32 tsSdpInetSend(
#ifdef TS_KERNEL_2_6
  struct kiocb      *iocb,
  struct socket     *sock,
  struct msghdr     *msg,
  size_t             size
#else
  struct socket     *sock,
  struct msghdr     *msg,
  tINT32             size,
  struct scm_cookie *scm
#endif
  );

tINT32 tsSdpInetRecv(
#ifdef TS_KERNEL_2_6
  struct kiocb      *iocb,
  struct socket     *sock,
  struct msghdr     *msg,
  size_t             size,
  tINT32             flags
#else
  struct socket     *sock,
  struct msghdr     *msg,
  tINT32             size,
  tINT32             flags,
  struct scm_cookie *scm
#endif
  );

#ifdef _TS_SDP_AIO_SUPPORT
tINT32 tsSdpInetWrite(
  struct socket *sock,
  kvec_cb_t      cb,
  size_t         size
  );

tINT32 tsSdpInetRead(
  struct socket *sock,
  kvec_cb_t      cb,
  size_t         size
  );
#endif

/* --------------------------------------------------------------------- */
/* AIO extensions.                                                       */
/* --------------------------------------------------------------------- */

tINT32 tsSdpBuffKvecReadCancelAll(
  tSDP_CONN conn,
  tINT32    error
  );

tINT32 tsSdpBuffKvecWriteCancelAll(
  tSDP_CONN conn,
  tINT32    error
  );

tINT32 tsSdpBuffKvecCancelAll(
  tSDP_CONN conn,
  tINT32    error
  );

#endif /* _TS_SDP_PROTO_H */
