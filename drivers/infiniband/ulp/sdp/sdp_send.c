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

  $Id: sdp_send.c,v 1.48 2004/02/24 23:48:51 roland Exp $
*/

#include "sdp_main.h"

/* --------------------------------------------------------------------- */
/*                                                                       */
/*                          COMMON functions                             */
/*                                                                       */
/* --------------------------------------------------------------------- */
#ifdef _TS_SDP_AIO_SUPPORT
/* ========================================================================= */
/*.._tsSdpWriteIocbCancelFunc -- lookup function for cancelation */
static tINT32 _tsSdpWriteIocbCancelFunc
(
 tSDP_GENERIC element,
 tPTR         arg
)
{
  tSDP_IOCB iocb  = (tSDP_IOCB)element;
  tINT32    value = (tINT32)(unsigned long)arg;

  TS_CHECK_NULL(element, -EINVAL);

  if (TS_SDP_GENERIC_TYPE_IOCB == element->type &&
      iocb->key == value) {

    return 0;
  } /* if */
  else {

    return -ERANGE;
  } /* else */
} /* _tsSdpWriteIocbCancelFunc */

/* ========================================================================= */
/*.._tsSdpInetWriteIocbCancel -- cancel an IO operation */
static tINT32 _tsSdpInetWriteIocbCancel
(
 struct kiocb *kiocb _TS_AIO_UNUSED_CANCEL_PARAM
)
{
  struct sock *sk;
  tSDP_CONN    conn;
  tSDP_IOCB    iocb;
  tINT32       result = 0;

  TS_CHECK_NULL(kiocb, -ERANGE);

  sk = (struct sock *)xchg(&kiocb->data, NULL);
  ts_aio_put_req(kiocb);

  if (NULL == sk) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_INOUT,
	     "SOCK: Cancel empty Kvec kiocb <%d:%d>",
	     kiocb->key, kiocb->users);

    result = -EAGAIN;
    goto done;
  } /* else */
  /*
   * lock the socket while we operate.
   */
  conn = TS_SDP_GET_CONN(sk);
  TS_SDP_CONN_LOCK(conn);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: Cancel Kvec <%08x:%04x> <%08x:%04x> <%04x> kiocb <%d:%d>",
	   conn->src_addr, conn->src_port, conn->dst_addr, conn->dst_port,
	   conn->state, kiocb->key, kiocb->users);
  /*
   * attempt to find the IOCB for this key. we don't have an indication
   * whether this is a read or write.
   */
  iocb = (tSDP_IOCB)tsSdpGenericTableLookup(&conn->send_queue,
					    _tsSdpWriteIocbCancelFunc,
					    (tPTR)(unsigned long)kiocb->key);
  if (NULL != iocb) {
    /*
     * always remove the IOCB.
     * If active, then place it into the correct active queue
     */
    result = tsSdpGenericTableRemove((tSDP_GENERIC)iocb);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	     "SOCK: Cancel IOCB <%d:%d> flags <%08x>",
	     kiocb->key, iocb->post, iocb->flags);

    if (0 < (TS_SDP_IOCB_F_ACTIVE & iocb->flags)) {

      if (0 < (TS_SDP_IOCB_F_RDMA_W & iocb->flags)) {

	result = tsSdpGenericTablePutTail(&conn->w_snk, (tSDP_GENERIC)iocb);
	TS_EXPECT(MOD_LNX_SDP, !(0 > result));
      } /* if */
      else {

	TS_EXPECT(MOD_LNX_SDP, (0 < (TS_SDP_IOCB_F_RDMA_R & iocb->flags)));

	result = tsSdpConnIocbTablePutTail(&conn->w_src, iocb);
	TS_EXPECT(MOD_LNX_SDP, !(0 > result));
      } /* else */
    } /* if */
    else {
      /*
       * callback to complete IOCB.
       */
      result = tsSdpConnIocbComplete(iocb, 0);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));

      result = -EAGAIN;
      goto unlock;
    } /* else */
  } /* if */
  /*
   * check the sink queue, not much to do, since the operation is
   * already in flight.
   */
  iocb = (tSDP_IOCB)tsSdpGenericTableLookup(&conn->w_snk,
					    _tsSdpWriteIocbCancelFunc,
					    (tPTR)(unsigned long)kiocb->key);
  if (NULL != iocb) {

    iocb->flags |= TS_SDP_IOCB_F_CANCEL;
    result = -EAGAIN;

    goto unlock;
  } /* if */
  /*
   * check source queue. If we're ing the source queue, then a cancel
   * needs to be issued.
   */
  iocb = tsSdpConnIocbTableLookup(&conn->w_src, kiocb->key);
  if (NULL != iocb) {
    /*
     * Unfortunetly there is only a course grain cancel in SDP, so
     * we have to cancel everything. This is OKish since it usually
     * only happens at connection termination, and the remaining
     * source probably will get cancel requests as well. The main
     * complexity is to take all the fine grain cancels from AIO and
     * once all out standing Src messages have been cancelled we can
     * issue the course grain SDP cancel. The connection is marked as
     * being in cancel processing so no other writes get into the
     * outbound pipe.
     */
    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "SOCK: Cancellation  <%d:%d> of active Source. <%d:%d>",
	     kiocb->key, kiocb->users,
	     tsSdpConnIocbTableSize(&conn->w_src), conn->src_cncl);

    if (0 == (TS_SDP_CONN_F_SRC_CANCEL_L & conn->flags) &&
	0 == (TS_SDP_IOCB_F_CANCEL & iocb->flags)) {

      conn->src_cncl++;
      iocb->flags |= TS_SDP_IOCB_F_CANCEL;

      if (conn->src_cncl == tsSdpConnIocbTableSize(&conn->w_src)) {

	result = tsSdpSendCtrlSrcCancel(conn);
	TS_EXPECT(MOD_LNX_SDP, !(0 > result));

	conn->flags |= TS_SDP_CONN_F_SRC_CANCEL_L;
      } /* if */
    } /* if */

    result = -EAGAIN;
    goto unlock;
  } /* if */
  /*
   * no IOCB found.
   */
  TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_INOUT,
	   "SOCK: Cancelled kvec with no IOCB. <%d:%d>",
	   kiocb->key, kiocb->users);

  if (0 < kiocb->users) {
    /*
     * shouldn't occur, but just incase, we don't want to hang
     * the user.
     */
    ts_aio_put_req(kiocb);
    result = 0;
  } /* if */
  else {
    /*
     * IOCB should be in completion processing at this point.
     */
    result = -EAGAIN;
  } /* else */

unlock:
  TS_SDP_CONN_UNLOCK(conn);
done:

  if (0 == result) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_INOUT,
	     "AIO: Kernel bug. Canceling IOCB <%d:%d> leaking <%d> bytes.",
	     kiocb->key, kiocb->users, kiocb->size);
  } /* if */

  return result;
} /* _tsSdpInetWriteIocbCancel */
#endif

/* ========================================================================= */
/*.._tsSdpSendBuffPost -- Post a buffer send on a SDP connection. */
static tINT32 _tsSdpSendBuffPost
(
 tSDP_CONN  conn,
 tSDP_BUFF  buff
)
{
  tTS_IB_SEND_PARAM_STRUCT send_param = {0};
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);
  TS_CHECK_NULL(buff->bsdh_hdr, -EINVAL);
  /*
   * write header send buffer.
   */
  conn->r_recv_bf--;
  conn->s_wq_size++;
  conn->l_advt_bf   = conn->l_recv_bf;
  conn->send_pipe  -= buff->data_size;
  conn->oob_offset -= (0 < conn->oob_offset) ? buff->data_size : 0;

  buff->ib_wrid             = conn->send_wrid++;
  buff->lkey                = conn->l_key;
  buff->bsdh_hdr->recv_bufs = conn->l_advt_bf;
  buff->bsdh_hdr->size      = buff->tail - buff->data;
  buff->bsdh_hdr->seq_num   = ++conn->send_seq;
  buff->bsdh_hdr->seq_ack   = conn->advt_seq;
  /*
   * endian swap
   */
  result = _tsSdpMsgHostToWireBSDH(buff->bsdh_hdr);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  send_param.op    = TS_IB_OP_SEND;
  /*
   * OOB processing. If there is a single OOB byte in flight then the
   * pending flag is set as early as possible. IF a second OOB byte
   * becomes queued then the pending flag for that byte will be in the
   * buffer which contains the data. Multiple outstanding OOB messages
   * is not well defined, this way we won't loose any, we'll get early
   * notification in the normal case, we adhear to the protocol, and
   * we don't need to track every message seperatly which would be
   * expensive.
   *
   * If the connections OOB flag is set and the oob
   * counter falls below 64K we set the pending flag, and clear the
   * the flag. This allows for at least one pending urgent message
   * to send early notification.
   */
  if (0 < (TS_SDP_CONN_F_OOB_SEND & conn->flags) &&
      !(0xFFFF < conn->oob_offset)) {

    TS_SDP_MSG_HDR_SET_OOB_PEND(buff->bsdh_hdr);
    TS_SDP_BUFF_F_SET_SE(buff);

    conn->flags &= ~(TS_SDP_CONN_F_OOB_SEND);
  } /* if */
  /*
   * The buffer flag is checked to see if the OOB data is in the buffer,
   * and present flag is set, potentially OOB offset is cleared. pending
   * is set if this buffer has never had pending set.
   */
  if (0 < (TS_SDP_BUFF_F_OOB_PRES & buff->flags)) {

    if (0 < conn->oob_offset) {

      TS_SDP_MSG_HDR_SET_OOB_PEND(buff->bsdh_hdr);
    } /* if */
    else {

      TS_EXPECT(MOD_LNX_SDP, !(0 > conn->oob_offset));
      conn->oob_offset = -1;
    } /* if*/

    TS_SDP_MSG_HDR_SET_OOB_PRES(buff->bsdh_hdr);
    TS_SDP_BUFF_F_SET_SE(buff);
  } /* if */
  /*
   * solicite event bit.
   */
  if (0 < TS_SDP_BUFF_F_GET_SE(buff)) {

    send_param.solicited_event = 1;
  } /* if */
  /*
   * unsignalled event
   */
  if (0 < TS_SDP_BUFF_F_GET_UNSIG(buff) &&
      TS_SDP_CONN_UNSIG_SEND_MAX > conn->send_cons) {

    conn->send_usig++;
    conn->send_cons++;
  } /* if */
  else {

    TS_SDP_BUFF_F_CLR_UNSIG(buff);
    send_param.signaled = 1;
    conn->send_cons = 0;
  } /* else */
  /*
   * check queue membership. (first send attempt vs. flush)
   */
  if (0 < tsSdpGenericTableMember((tSDP_GENERIC)buff)) {

    result = tsSdpGenericTableRemove((tSDP_GENERIC)buff);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  } /* if */
  /*
   * save the buffer for the event handler.
   */
  result = tsSdpBuffPoolPutTail(&conn->send_post, buff);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Error <%d> to queueing send buffer.", result);
    goto done;
  } /* if */
  /*
   * post send
   */
  send_param.work_request_id    = buff->ib_wrid;
  send_param.gather_list        = TS_SDP_BUFF_GAT_SCAT(buff);
  send_param.num_gather_entries = 1;

  result = tsIbSend(conn->qp, &send_param, 1);
  if (0 != result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: <%04x> Error <%d> posting send. <%d:%d> <%d:%d:%d>",
	     conn->hashent, result, conn->s_wq_cur, conn->s_wq_size,
	     tsSdpBuffPoolSize(&conn->send_post),
	     tsSdpGenericTableSize(&conn->r_src),
	     tsSdpGenericTableSize(&conn->w_snk));

    (void)tsSdpBuffPoolGetTail(&conn->send_post);
    goto done;
  } /* if */
  /*
   * source cancels require us to save the sequence number
   * for validation of the cancel's completion.
   */
  if (0 < (TS_SDP_CONN_F_SRC_CANCEL_L & conn->flags)) {

    conn->src_cseq = ((TS_SDP_MSG_MID_SRC_CANCEL == buff->bsdh_hdr->mid) ?
		      buff->bsdh_hdr->seq_num : conn->src_cseq);
  } /* if */

  return 0;
done:
  conn->r_recv_bf++;
  conn->send_seq--;
  conn->s_wq_size--;
  return result;
} /* _tsSdpSendBuffPost */

/* --------------------------------------------------------------------- */
/*                                                                       */
/*                          DATA functions                               */
/*                                                                       */
/* --------------------------------------------------------------------- */

/* ========================================================================= */
/*.._tsSdpSendDataBuffPost -- Post data for buffered transmission */
static tINT32 _tsSdpSendDataBuffPost
(
 tSDP_CONN conn,
 tSDP_BUFF buff
)
{
  tSDP_ADVT advt;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);
  /*
   * check state to determine OK to send:
   *
   * 1) sufficient remote buffer advertisments for data transmission
   * 2) outstanding source advertisments, data must be held.
   * 3) buffer from head of queue or as parameter
   * 4) nodelay check.
   */
  if (3 > conn->r_recv_bf ||
      0 < conn->src_sent) {

    return ENOBUFS;
  } /* if */
  /*
   * The rest of the checks can proceed if there is a signalled event
   * in the pipe, otherwise we could stall...
   */
  if (conn->send_usig < tsSdpBuffPoolSize(&conn->send_post) ||
      0 < tsSdpGenericTableSize(&conn->w_snk)) {

    if (buff->tail < buff->end &&
	0 == (TS_SDP_BUFF_F_OOB_PRES & buff->flags) &&
	0 == conn->nodelay) {
      /*
       * If the buffer is not full, and there is already data in the
       * SDP pipe, then hold on to the buffer to fill it up with more
       * data. If SDP acks clear the pipe they'll grab this buffer,
       * or send will flush once it's full, which ever comes first.
       */
      return ENOBUFS;
    } /* if */
    /*
     * slow start to give sink advertisments a chance for asymmetric
     * connections. This is desirable to offload the remote host.
     */
    if (!(conn->s_wq_cur > conn->s_wq_size)) {
      /*
       * slow down the up take in the send data path to give the remote
       * side some time to post available sink advertisments.
       */
      if (TS_SDP_DEV_SEND_POST_MAX > conn->s_wq_cur) {

	if (TS_SDP_DEV_SEND_POST_COUNT > conn->s_wq_par) {

	  conn->s_wq_par++;
	} /* if */
	else {

	  conn->s_wq_cur++;
	  conn->s_wq_par = 0;
	} /* else */
      }  /* if */

      return ENOBUFS;
    } /* if */
  } /* if */
  /*
   * setup header.
   */
  buff->data            -= sizeof(tSDP_MSG_BSDH_STRUCT);
  buff->bsdh_hdr         = (tSDP_MSG_BSDH)buff->data;
  buff->bsdh_hdr->mid    = TS_SDP_MSG_MID_DATA;
  buff->bsdh_hdr->flags  = TS_SDP_MSG_FLAG_NON_FLAG;
  /*
   * signalled? With no delay turned off, data transmission may be
   * waiting for a send completion.
   */
  TS_SDP_BUFF_F_SET_UNSIG(buff);
  /*
   * update non-discard counter.
   * Make consideration for a pending sink. (can be forced by OOB)
   */
  if (0 < tsSdpConnAdvtTableSize(&conn->snk_pend)) {
    /*
     * As sink advertisment needs to be discarded. We always complete an
     * advertisment if there is not enough room for an entire buffers
     * worth of data, this allows us to not need to check how much room
     * is going to be consumed by this buffer, and only one discard is
     * needed. (remember the spec makes sure that the sink is bigger then
     * the buffer.)
     */
    advt = tsSdpConnAdvtTableGet(&conn->snk_pend);
    TS_EXPECT(MOD_LNX_SDP, (NULL != advt));

    result = tsSdpConnAdvtDestroy(advt);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
    /*
     * update sink advertisments.
     */
    conn->snk_recv--;
  } /* if */
  else {

    conn->nond_send++;
  } /* if */
  /*
   * transmision time
   */
  result = _tsSdpSendBuffPost(conn, buff);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Error posting buffered data to SEND queue. <%d>",
	     result);
    goto error;
  } /* if */

  return 0;
error:
  return result;
} /* _tsSdpSendDataBuffPost */

/* ========================================================================= */
/*.._tsSdpSendDataBuffSink -- Post data for buffered transmission */
static tINT32 _tsSdpSendDataBuffSink
(
 tSDP_CONN conn,
 tSDP_BUFF buff
)
{
  tTS_IB_SEND_PARAM_STRUCT send_param = {0};
  tSDP_ADVT advt;
  tINT32 result;
  tINT32 zcopy;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);
  /*
   * check state to determine OK to send:
   *
   * 1) sufficient send resources.
   */
  if (!(TS_SDP_DEV_SEND_POST_MAX > conn->s_wq_size)) {

    return ENOBUFS;
  } /* if */
  /*
   * confirm type
   */
  if (TS_SDP_GENERIC_TYPE_BUFF != buff->type) {

    return -ENOBUFS;
  } /* if */
  /*
   * nodelay buffering
   */
#if 0
  if (buff->tail < buff->end &&
      0 == conn->nodelay &&
      conn->send_usig < tsSdpBuffPoolSize(&conn->send_post)) {
    /*
     * If the buffer is not full, and there is already data in the
     * SDP pipe, then hold on to the buffer to fill it up with more
     * data. If SDP acks clear the pipe they'll grab this buffer,
     * or send will flush once it's full, which ever comes first.
     */
    return ENOBUFS;
  } /* if */
#endif
  /*
   * get advertisment.
   */
  advt = tsSdpConnAdvtTableLook(&conn->snk_pend);
  if (NULL == advt) {

    return ENOBUFS;
  } /* if */
  /*
   * signalled? With no delay turned off, data transmission may be
   * waiting for a send completion.
   */
#if 0
  TS_SDP_BUFF_F_SET_UNSIG(buff);
#endif
  /*
   * setup RDMA write
   */
  send_param.op                 = TS_IB_OP_RDMA_WRITE;
  send_param.remote_address     = advt->addr;
  send_param.rkey               = advt->rkey;
  send_param.signaled           = 1;

  buff->ib_wrid = conn->send_wrid++;
  buff->lkey = conn->l_key;

  advt->wrid  = buff->ib_wrid;
  advt->size -= (buff->tail - buff->data);
  advt->addr += (buff->tail - buff->data);
  advt->post += (buff->tail - buff->data);

#ifdef _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "POST: Write BUFF wrid <%08x> of <%u> bytes <%d>.",
	   buff->ib_wrid, (buff->tail - buff->data), advt->size);
#endif
  /*
   * dequeue if needed and the queue buffer
   */
  if (0 < tsSdpGenericTableMember((tSDP_GENERIC)buff)) {

    result = tsSdpGenericTableRemove((tSDP_GENERIC)buff);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  } /* if */

  result = tsSdpGenericTablePutTail(&conn->w_snk, (tSDP_GENERIC)buff);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Error <%d> queueing generic WRITE BUFF. <%d>",
	     result, tsSdpGenericTableSize(&conn->w_snk));
    goto error;
  } /* if */
  /*
   * update send queue depth
   */
  conn->s_wq_size++;
  conn->send_pipe  -= buff->data_size;
  conn->oob_offset -= (0 < conn->oob_offset) ? buff->data_size : 0;
  /*
   * post RDMA
   */
  send_param.work_request_id    = buff->ib_wrid;
  send_param.gather_list        = TS_SDP_BUFF_GAT_SCAT(buff);
  send_param.num_gather_entries = 1;

  result = tsIbSend(conn->qp, &send_param, 1);
  if (0 != result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: send (rdma write) failure. <%d>", result);

    conn->s_wq_size--;
    goto error;
  } /* if */
  /*
   * If the available space is smaller then send size, complete the
   * advertisment.
   */
  if (conn->send_size > advt->size) {

    advt = tsSdpConnAdvtTableGet(&conn->snk_pend);
    TS_EXPECT(MOD_LNX_SDP, (NULL != advt));

    zcopy = advt->post;

    result = tsSdpConnAdvtDestroy(advt);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));

    result = tsSdpSendCtrlRdmaWrComp(conn, zcopy);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "POST: Error <%d> completing sink advertisment. <%d>",
	       result, zcopy);
      result = -ENODEV;
      goto error;
    } /* if */
    /*
     * update sink advertisments.
     */
    conn->snk_recv--;
  } /* if */

  return 0;
error:
  return result;
} /* _tsSdpSendDataBuffSink */

/* ========================================================================= */
/*.._tsSdpSendDataIocbSnk -- process a zcopy write advert in the data path */
tINT32 _tsSdpSendDataIocbSnk
(
 tSDP_CONN conn,
 tSDP_IOCB iocb
)
{
  tTS_IB_SEND_PARAM_STRUCT send_param = {0};
  tTS_IB_GATHER_SCATTER_STRUCT sg_val;
  tSDP_ADVT advt;
  tINT32 result;
  tINT32 zcopy;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(iocb, -EINVAL);
  /*
   * register IOCBs physical memory, we check for previous registration,
   * since multiple writes may have been required to fill the advertisement
   */
  if (NULL == iocb->page_array) {

    result = tsSdpConnIocbRegister(iocb, conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	       "POST: Error <%d> registering IOCB. <%d:%d>",
	       result, iocb->key, iocb->len);
      goto error;
    } /* if */
  } /* if */
  /*
   * check queue depth
   */
  while (0 < iocb->len &&
	 TS_SDP_DEV_SEND_POST_MAX > conn->s_wq_size) {
    /*
     * get the pending sink advertisment.
     */
    advt = tsSdpConnAdvtTableLook(&conn->snk_pend);
    if (NULL == advt) {

      break;
    } /* if */
    /*
     * amount of data to zcopy.
     */
    zcopy = min(advt->size, iocb->len);

    sg_val.address = iocb->io_addr;
    sg_val.key     = iocb->l_key;
    sg_val.length  = zcopy;

    send_param.op             = TS_IB_OP_RDMA_WRITE;
    send_param.remote_address = advt->addr;
    send_param.rkey           = advt->rkey;
    send_param.signaled       = 1;

    iocb->wrid     = conn->send_wrid++;
    iocb->len     -= zcopy;
    iocb->post    += zcopy;
    iocb->io_addr += zcopy;
    iocb->flags   |= TS_SDP_IOCB_F_ACTIVE;
    iocb->flags   |= TS_SDP_IOCB_F_RDMA_W;

    advt->wrid     = iocb->wrid;
    advt->size    -= zcopy;
    advt->addr    += zcopy;
    advt->post    += zcopy;

#ifdef _TS_SDP_DATA_PATH_DEBUG
    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "POST: Write IOCB wrid <%08x> of <%u> bytes <%d:%d>.",
	     iocb->wrid, zcopy, iocb->len, advt->size);
#endif
    /*
     * update send queue depth
     */
    conn->s_wq_size++;
    conn->send_pipe  -= zcopy;
    conn->oob_offset -= (0 < conn->oob_offset) ? zcopy : 0;
    /*
     * post RDMA
     */
    send_param.work_request_id    = iocb->wrid;
    send_param.gather_list        = &sg_val;
    send_param.num_gather_entries = 1;

    result = tsIbSend(conn->qp, &send_param, 1);
    if (0 != result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "POST: send (rdma write) failure. <%d>", result);

      conn->s_wq_size--;
      goto error;
    } /* if */
    /*
     * if there is no more advertised space,  remove the advertisment
     * from the queue, and get it ready for completion. (see note in
     * buffered send during outstanding sink advertisment to see how
     * the advt size remaining is picked.)
     */
    if (conn->send_size > advt->size) {

      advt = tsSdpConnAdvtTableGet(&conn->snk_pend);
      if (NULL == advt) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "POST: sinkc available advertisment disappeared.");
	result = -ENODEV;
	goto error;
      } /* if */

      zcopy = advt->post;

      result = tsSdpConnAdvtDestroy(advt);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));

      result = tsSdpSendCtrlRdmaWrComp(conn, zcopy);
      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "POST: Error <%d> completing sink advertisment. <%d>",
		 result, zcopy);
	result = -ENODEV;
	goto error;
      } /* if */
      /*
       * update sink advertisments.
       */
      conn->snk_recv--;
    } /* if */
  } /* while */

  return iocb->len;
error:
  return result;
} /* _tsSdpSendDataIocbSnk */

/* ========================================================================= */
/*.._tsSdpSendDataIocbSrc -- send a zcopy read advertisment in the data path */
tINT32 _tsSdpSendDataIocbSrc
(
 tSDP_CONN conn,
 tSDP_IOCB iocb
)
{
  tSDP_MSG_SRCAH src_ah;
  tSDP_BUFF buff;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(iocb, -EINVAL);
  /*
   * 1) local source cancel is pending
   * 2) sufficient send credits for buffered transmission.
   */
  if (0 < (TS_SDP_CONN_F_SRC_CANCEL_L & conn->flags) ||
      3 > conn->r_recv_bf) {

    return ENOBUFS;
  } /* if */

  switch (conn->send_mode) {
  case TS_SDP_MODE_PIPE:

    if (!(conn->s_cur_adv > conn->src_sent)) {

      return ENOBUFS;
    } /* if */

    if (conn->s_cur_adv < conn->r_max_adv) {

      if (!(TS_SDP_SRC_AVAIL_FRACTION > conn->s_par_adv)) {

	conn->s_cur_adv++;
	conn->s_par_adv = 0;
      } /* if */
      else {

	conn->s_par_adv++;
      } /* else */
    }  /* if */
    else {

      conn->s_cur_adv = conn->r_max_adv;
      conn->s_par_adv = 0;
    } /* else */
#if 0

    conn->s_cur_adv = ((conn->s_cur_adv < conn->r_max_adv) ?
		       conn->s_cur_adv + 1 : conn->r_max_adv);
#endif
    break;
  case TS_SDP_MODE_COMB:

    if (0 < conn->src_sent) {

      return ENOBUFS;
    } /* if */

    break;
  default:

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Unexpected SrcAvail mode. <%d>", conn->send_mode);
    return -EPROTO;
  } /* switch */
  /*
   * get buffer
   */
  buff = tsSdpBuffMainGet();
  if (NULL == buff) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failed to allocate SrcAvail buffer. <%d>", iocb->key);
    return -ENOMEM;
  } /* if */
  /*
   * register IOCBs physical memory, we check for previous registration,
   * since SrcAvail revocations can get us to this point multiple times
   * for the same IOCB.
   */
  if (NULL == iocb->page_array) {

    result = tsSdpConnIocbRegister(iocb, conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	       "POST: Error <%d> registering IOCB. <%d:%d>",
	       result, iocb->key, iocb->len);
      goto drop;
    } /* if */
  } /* if */
  /*
   * format SrcAvail
   */
  buff->tail             = buff->data;
  buff->bsdh_hdr         = (tSDP_MSG_BSDH)buff->data;
  buff->bsdh_hdr->mid    = TS_SDP_MSG_MID_SRC_AVAIL;
  buff->bsdh_hdr->flags  = TS_SDP_MSG_FLAG_NON_FLAG;
  buff->tail            += sizeof(tSDP_MSG_BSDH_STRUCT);
  src_ah                 = (tSDP_MSG_SRCAH)buff->tail;
  src_ah->size           = iocb->len;
  src_ah->r_key          = iocb->r_key;
  src_ah->addr           = iocb->io_addr;
  buff->tail            += sizeof(tSDP_MSG_SRCAH_STRUCT);
  buff->data_size        = 0;
  iocb->flags           |= TS_SDP_IOCB_F_ACTIVE;
  iocb->flags           |= TS_SDP_IOCB_F_RDMA_R;

  TS_SDP_BUFF_F_CLR_SE(buff);
  TS_SDP_BUFF_F_CLR_UNSIG(buff);

  if (TS_SDP_MODE_COMB == conn->send_mode) {
#ifdef _TS_SDP_AIO_SUPPORT
    tPTR   vaddr;
    tINT32 offset;
    /*
     * In combined mode, it's a protocol requirment to send at
     * least a byte of data in the SrcAvail.
     */
    if (TS_SDP_SRC_AVAIL_GRATUITOUS > iocb->kvec.src.let->length) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "POST: Error copying data <%d> from IOCB to SRC. <%d:%d>",
	       TS_SDP_SRC_AVAIL_GRATUITOUS,
	       iocb->kvec.src.let->length,
	       iocb->kvec.src.let->offset);

      result = -EFAULT;
      goto error;
    } /* if */
    /*
     * map, copy, unmap.
     */
    vaddr  = __tsSdpKmap(iocb->kvec.src.let->page);
    offset = iocb->kvec.src.let->offset;

    memcpy(buff->tail, (vaddr + offset), TS_SDP_SRC_AVAIL_GRATUITOUS);

    __tsSdpKunmap(iocb->kvec.src.let->page);
#endif
    /*
     * update pointers
     */
    buff->data_size  = TS_SDP_SRC_AVAIL_GRATUITOUS;
    buff->tail      += TS_SDP_SRC_AVAIL_GRATUITOUS;
    iocb->len       -= TS_SDP_SRC_AVAIL_GRATUITOUS;

    conn->nond_send++;
  } /* if */

  conn->src_sent++;
  /*
   * endian swap of extended header
   */
  result = _tsSdpMsgHostToWireSRCAH(src_ah);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * queue/send SrcAvail message
   */
  result = _tsSdpSendBuffPost(conn, buff);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Error <%d> posting Src Avail for IOCB. <%d>",
	     result, iocb->key);
    goto release;
  } /* if */

  return 0;
release:
  conn->nond_send -= (TS_SDP_MODE_COMB == conn->send_mode) ? 1 : 0;
  conn->src_sent--;
#ifdef _TS_SDP_AIO_SUPPORT
error:
#endif
  iocb->flags &= ~(TS_SDP_IOCB_F_RDMA_R|TS_SDP_IOCB_F_ACTIVE);
  iocb->len   += ((TS_SDP_MODE_COMB == conn->send_mode) ?
		  TS_SDP_SRC_AVAIL_GRATUITOUS : 0);
drop:
  (void)tsSdpBuffMainPut(buff);
  return result;
} /* _tsSdpSendDataIocbSrc */

#ifdef _TS_SDP_AIO_SUPPORT
/* ========================================================================= */
/*.._tsSdpSendDataIocbBuffKvec -- write into a SDP buffer from a kvec */
static tINT32 _tsSdpSendDataIocbBuffKvec
(
 struct kvec_dst *src,
 tSDP_BUFF        buff,
 tINT32           len
)
{
  tPTR   tail;
  tINT32 part;
  tINT32 left;
  tINT32 copy;

  TS_CHECK_NULL(src, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);
  /*
   * activate source memory kmap
   */
  TS_SDP_KVEC_DST_MAP(src);
  /*
   * copy from source to buffer
   */
  copy = min(len, (tINT32)(buff->end - buff->tail));
  tail = buff->tail;

  for (left = copy; 0 < left; ) {

    part = min(left, src->space);
#ifndef _TS_SDP_DATA_PATH_NULL
    memcpy(buff->tail, src->dst, part);
#endif

    buff->tail += part;
    src->space -= part;
    src->dst   += part;
    left       -= part;

    if (0 < left &&
	0 == src->space) {

      TS_SDP_KVEC_DST_UNMAP(src);
      src->let++;
      src->offset = 0;
      TS_SDP_KVEC_DST_MAP(src);

      if (!(0 < src->space)) { /* sanity check */

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "SEND: Kvec to IOCB sanity check failed. <%d:%d:%d:%d>",
		 len, left, copy, src->space);

	buff->tail = tail;
	copy = -EFAULT;
	goto error;
      } /* if */
    } /* if */
  } /* for */

error:
  /*
   * release destination memory kmap
   */
  TS_SDP_KVEC_DST_UNMAP(src);

  return copy;
} /* _tsSdpSendDataIocbBuffKvec */
#endif

/* ========================================================================= */
/*.._tsSdpSendDataIocbBuff -- write multiple SDP buffers from an ioc */
static tINT32 _tsSdpSendDataIocbBuff
(
 tSDP_CONN conn,
 tSDP_IOCB iocb
)
{
  tSDP_BUFF buff;
  tINT32    copy;
  tINT32    partial = 0;
  tINT32    result;
  tINT32    w_space;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(iocb, -EINVAL);

  if (0 < conn->src_sent) {

    return ENOBUFS;
  } /* if */
  /*
   * loop through queued buffers and copy them to the destination
   */
  w_space = __tsSdpInetWriteSpace(conn, 0);

  while (0 < w_space &&
	 0 < iocb->len &&
	 2 < conn->r_recv_bf &&
	 TS_SDP_DEV_SEND_POST_MAX > conn->s_wq_size) {
    /*
     * get a buffer for posting.
     */
    buff = tsSdpBuffMainGet();
    if (NULL == buff) {

      result = -ENOMEM;
      goto error;
    } /* if */
    /*
     * setup header.
     */
    buff->tail = buff->end - conn->send_size;
    buff->data = buff->tail;

    buff->data            -= sizeof(tSDP_MSG_BSDH_STRUCT);
    buff->bsdh_hdr         = (tSDP_MSG_BSDH)buff->data;
    buff->bsdh_hdr->mid    = TS_SDP_MSG_MID_DATA;
    buff->bsdh_hdr->flags  = TS_SDP_MSG_FLAG_NON_FLAG;

    TS_SDP_BUFF_F_CLR_SE(buff);
    TS_SDP_BUFF_F_CLR_UNSIG(buff);

    copy = min(iocb->len, w_space);
    /*
     * TODO: need to be checking OOB here.
     */
#ifdef _TS_SDP_AIO_SUPPORT
    partial = _tsSdpSendDataIocbBuffKvec(&iocb->kvec.src, buff, copy);
    if (0 > partial) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
               "WRITE: error <%d> copying data <%d> from vector.",
               partial, copy);

      result = partial;
      goto drop;
    } /* if */
#endif

    buff->data_size += partial;
    conn->send_qud  += partial;
    iocb->len       -= partial;
    iocb->post      += partial;
    w_space         -= partial;

    conn->nond_send++;
    /*
     * transmision time. An update of send_pipe is not needed, since
     * the IOCB queue took care of the increment.
     */
    result = _tsSdpSendBuffPost(conn, buff);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "POST: Error posting buffered data to SEND queue. <%d>",
	       result);
      goto drop;
    } /* if */
  } /* while */

  return iocb->len;
drop:
  (void)tsSdpBuffMainPut(buff);
error:
  return result;
} /* _tsSdpSendDataIocbBuff */

/* ========================================================================= */
/*.._tsSdpSendDataIocb -- Post IOCB data for  transmission */
static tINT32 _tsSdpSendDataIocb
(
 tSDP_CONN conn,
 tSDP_IOCB iocb
)
{
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(iocb, -EINVAL);

  if (!(TS_SDP_DEV_SEND_POST_MAX > conn->s_wq_size)) {

    return ENOBUFS;
  } /* if */
  /*
   * confirm IOCB usage.
   */
  if (TS_SDP_GENERIC_TYPE_IOCB != iocb->type) {

    return -ENOBUFS;
  } /* if */
  /*
   * determin if we are sending Buffered, Source or Sink.
   */
  if (0 < tsSdpConnAdvtTableSize(&conn->snk_pend)) {

    result = _tsSdpSendDataIocbSnk(conn, iocb);
    if (0 == result) {
      /*
       * IOCB completely processed. Otherwise we allow the callers to
       * determine the fate of the IOCB on failure or partial processing.
       */
      if (0 < tsSdpGenericTableMember((tSDP_GENERIC)iocb)) {

	result = tsSdpGenericTableRemove((tSDP_GENERIC)iocb);
	TS_EXPECT(MOD_LNX_SDP, !(0 > result));
      } /* if */

      result = tsSdpGenericTablePutTail(&conn->w_snk, (tSDP_GENERIC)iocb);
      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "POST: Error <%d> queuing active write IOCB.", result);
      } /* if */
    } /* if */
  } /* if */
  else {
    /*
     * If there are active sink IOCBs we want to stall, in the hope that
     * a new sink advertisment will arrive, because sinks are more
     * efficient.
     */
    if (0 == conn->sink_actv) {

      if (conn->src_zthresh > iocb->len ||
	  TS_SDP_MODE_BUFF == conn->send_mode ||
	  0 < (TS_SDP_IOCB_F_BUFF & iocb->flags)) {

	result = _tsSdpSendDataIocbBuff(conn, iocb);
	if (0 == result) {
	  /*
	   * complete this IOCB
	   */
	  if (0 < tsSdpGenericTableMember((tSDP_GENERIC)iocb)) {

	    result = tsSdpGenericTableRemove((tSDP_GENERIC)iocb);
	    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
	  } /* if */

	  TS_SDP_CONN_STAT_WRITE_INC(conn, iocb->post);
	  TS_SDP_CONN_STAT_WQ_DEC(conn, iocb->size);

	  result = tsSdpConnIocbComplete(iocb, 0);
	  if (0 > result) {

	    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		     "WRITE: error <%d> completing iocb. <%d>",
		     result, iocb->key);
	  } /* if */
	} /* if */
      } /* if */
      else {

	result = _tsSdpSendDataIocbSrc(conn, iocb);
	if (0 == result) {
	  /*
	   * queue IOCB
	   */
	  if (0 < tsSdpGenericTableMember((tSDP_GENERIC)iocb)) {

	    result = tsSdpGenericTableRemove((tSDP_GENERIC)iocb);
	    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
	  } /* if */

	  result = tsSdpConnIocbTablePutTail(&conn->w_src, iocb);
	  if (0 > result) {

	    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		     "WRITE: Error <%d> queueing write <%d> to active table",
		     result, iocb->key,
		     tsSdpConnIocbTableSize(&conn->w_src));
	  } /* if */
	} /* if */
      } /* else */
    } /* if */
    else {

      result = ENOBUFS;
    } /* else */
  } /* else */

  return result;
} /* _tsSdpSendDataIocb */

/* ========================================================================= */
/*.._tsSdpSendDataQueueTest -- send data buffer if conditions are met */
static tINT32 _tsSdpSendDataQueueTest
(
 tSDP_CONN    conn,
 tSDP_GENERIC element
)
{
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(element, -EINVAL);
  /*
   * Notify caller to buffer data:
   * 1) Invalid state for transmission
   * 2) source advertisment cancel in progress.
   */
  if (0 == (TS_SDP_ST_MASK_SEND_OK & conn->state) ||
      0 <  (TS_SDP_CONN_F_SRC_CANCEL_L & conn->flags)) {

    return ENOBUFS;
  } /* if */

  if (TS_SDP_GENERIC_TYPE_BUFF == element->type) {

    if (0 == tsSdpConnAdvtTableLook(&conn->snk_pend) ||
	0 < (TS_SDP_BUFF_F_OOB_PRES & ((tSDP_BUFF)element)->flags)) {

      result = _tsSdpSendDataBuffPost(conn, (tSDP_BUFF)element);
    } /* if */
    else {

      result = _tsSdpSendDataBuffSink(conn, (tSDP_BUFF)element);
    } /* else */
  } /* if */
  else {

    TS_EXPECT(MOD_LNX_SDP, (TS_SDP_GENERIC_TYPE_IOCB == element->type));

    result = _tsSdpSendDataIocb(conn, (tSDP_IOCB)element);
  } /* else */

  return result;
} /* _tsSdpSendDataQueueTest */


/* ========================================================================= */
/*.._tsSdpSendDataQueueFlush -- Flush data from send queue, to send post. */
static tINT32 _tsSdpSendDataQueueFlush
(
 tSDP_CONN  conn
)
{
  tSDP_GENERIC element;
  tINT32 result = 0;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * As long as there is data, try to post buffered data, until a non-zero
   * result is generated. (positive: no space; negative: error)
   */
  while (0 < tsSdpGenericTableSize(&conn->send_queue)) {

    element = tsSdpGenericTableLookHead(&conn->send_queue);
    TS_EXPECT(MOD_LNX_SDP, (NULL != element));

    result = _tsSdpSendDataQueueTest(conn, element);
    if (0 != result) {
      /*
       * error
       */
      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "POST: failed to post data message during flush. <%d>",
		 result);
	/*
	 * check for dangling element reference, since called functions
	 * can dequeue the element, and not know how to requeue it.
	 */
	if (0 == tsSdpGenericTableMember(element)) {

	  result = tsSdpGenericTablePutHead(&conn->send_queue, element);
	  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
	} /* if */
      } /* if */

      break;
    } /* if */
  } /* while */

  return result;
} /* _tsSdpSendDataQueueFlush */

/* ========================================================================= */
/*.._tsSdpSendDataQueue -- send using the data queue if necessary. */
static tINT32 _tsSdpSendDataQueue
(
 tSDP_CONN    conn,
 tSDP_GENERIC element
)
{
  tINT32 result = 0;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(element, -EINVAL);
  /*
   * If data is being buffered, save and return, send/recv completions
   * will flush the queue.
   * If data is not being buffered, attempt to send, a positive result
   * requires us to buffer, a negative result is an error, a return
   * value of zero is a successful transmission
   */
  if (0 < tsSdpGenericTableSize(&conn->send_queue) ||
      0 < (result = _tsSdpSendDataQueueTest(conn, element))) {

    result = tsSdpGenericTablePutTail(&conn->send_queue, element);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "POST: Error <%d> queueing data buffer for later send post.",
	       result);
      goto done;
    } /* if */
    /*
     * Potentially request a switch to pipelined mode.
     */
    if (TS_SDP_MODE_COMB == conn->send_mode &&
	!(TS_SDP_DEV_SEND_BACKLOG >
	  tsSdpGenericTableSize(&conn->send_queue))) {

      result = tsSdpSendCtrlModeChange(conn, TS_SDP_MSG_MCH_PIPE_RECV);
      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "POST: Error <%d> posting mode change. <%d>",
		 result, conn->send_mode);
	goto done;
      } /* if */
    } /* if */
  } /* if */

  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failure during data send posting. <%d>", result);
    goto done;
  } /* if */

  return 0;
done:
  return result;
} /* _tsSdpSendDataQueue */

/* ========================================================================= */
/*.._tsSdpSendDataBuffGet -- get an appropriate write buffer for send */
static __inline__ tSDP_BUFF _tsSdpSendDataBuffGet
(
 tSDP_CONN conn
)
{
  tSDP_BUFF buff;

  TS_CHECK_NULL(conn, NULL);
  /*
   * If there is no available buffer get a new one.
   */
  buff = (tSDP_BUFF)tsSdpGenericTableLookTypeTail(&conn->send_queue,
						  TS_SDP_GENERIC_TYPE_BUFF);
  if (NULL == buff ||
      buff->tail == buff->end ||
      0 < (TS_SDP_BUFF_F_OOB_PRES & buff->flags)) {

    buff = tsSdpBuffMainGet();
    if (NULL != buff) {

      buff->tail = buff->end - conn->send_size;
      buff->data = buff->tail;
#if 0
      /*
       * validate that there is room for the header. sanity check.
       */
      if (TS_SDP_MSG_HDR_SIZE > (buff->data - buff->head)) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "POST: send, not enough room in buffer for headers. <%d:%d>",
		 TS_SDP_MSG_HDR_SIZE, (buff->data - buff->head));

	(void)tsSdpBuffMainPut(buff);
	buff = NULL;
      } /* if */
#endif
    } /* if */
  } /* else */

  return buff;
} /* _tsSdpSendDataBuffGet */

/* ========================================================================= */
/*.._tsSdpSendDataBuffPut -- place a buffer into the send queue */
static __inline__ tINT32 _tsSdpSendDataBuffPut
(
 tSDP_CONN conn,
 tSDP_BUFF buff,
 tINT32    size,
 tINT32    urg
)
{
  tINT32 result = 0;
  tINT32 expect;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);
  /*
   * See note on send OOB implementation in SendBuffPost.
   */
  if (0 < urg) {

    buff->flags |= TS_SDP_BUFF_F_OOB_PRES;
    /*
     * The OOB PEND and PRES flags need to match up as pairs.
     */
    if (0 > conn->oob_offset) {

      conn->oob_offset = conn->send_pipe + size;
      conn->flags     |= TS_SDP_CONN_F_OOB_SEND;
    } /* if */
  } /* if */
  /*
   * if the buffer is already queue, then this was a fill of a partial
   * buffer and dosn't need to be queued now.
   */
  if (0 < (TS_SDP_BUFF_F_QUEUED & buff->flags)) {

    buff->data_size += size;
    conn->send_qud  += size;
    conn->send_pipe += size;
  } /* if */
  else {

    buff->data_size  = buff->tail - buff->data;
    conn->send_qud  += buff->data_size;
    conn->send_pipe += buff->data_size;

    buff->flags     |= TS_SDP_BUFF_F_QUEUED;
    /*
     * finally send the data buffer
     */
    result = _tsSdpSendDataQueue(conn, (tSDP_GENERIC)buff);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "POST: Error posting buffered data to SEND queue. <%d>",
	       result);

      expect = tsSdpBuffMainPut(buff);
      TS_EXPECT(MOD_LNX_SDP, !(0 > expect));
    } /* if */
  } /* else */

  return result;
} /* _tsSdpSendDataBuffPut */

/* --------------------------------------------------------------------- */
/*                                                                       */
/*                          CONTROL functions                            */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsSdpSendCtrlBuffTest -- determine if it's OK to post a control msg */
static tINT32 _tsSdpSendCtrlBuffTest
(
 tSDP_CONN conn,
 tSDP_BUFF buff
)
{
  tINT32 result = 0;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  if (0 == (TS_SDP_ST_MASK_CTRL_OK & conn->state) ||
      !(TS_SDP_DEV_SEND_POST_MAX > conn->s_wq_size) ||
      !(0 < conn->r_recv_bf) ||
      (conn->l_recv_bf == conn->l_advt_bf &&
       1 == conn->r_recv_bf)) {

    return ENOBUFS;
  } /* if */
  /*
   * post the control buffer
   */
  result = _tsSdpSendBuffPost(conn, buff);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: failed to post control send. <%d>", result);
    goto error;
  } /* if */

  return 0;
error:
  return result;
} /* _tsSdpSendCtrlBuffTest */

/* ========================================================================= */
/*.._tsSdpSendCtrlBuffFlush -- Flush control buffers, to send post. */
static tINT32 _tsSdpSendCtrlBuffFlush
(
 tSDP_CONN  conn
)
{
  tSDP_GENERIC element;
  tINT32 result = 0;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * As long as there are buffers, try to post  until a non-zero
   * result is generated. (positive: no space; negative: error)
   */
  while (0 < tsSdpGenericTableSize(&conn->send_ctrl)) {

    element = tsSdpGenericTableLookHead(&conn->send_ctrl);
    TS_EXPECT(MOD_LNX_SDP, (NULL != element));

    result = _tsSdpSendCtrlBuffTest(conn, (tSDP_BUFF)element);
    if (0 != result) {

      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "POST: failed to post control message on flush. <%d>",
		 result);

        if (0 == tsSdpGenericTableMember(element)) {

          result = tsSdpGenericTablePutHead(&conn->send_ctrl, element);
          TS_EXPECT(MOD_LNX_SDP, !(0 > result));
        } /* if */
      } /* if */

      break;
    } /* if */
  } /* while */

  return result;
} /* _tsSdpSendCtrlBuffFlush */

/* ========================================================================= */
/*.._tsSdpSendCtrlBuffBuffered -- Send a buffered control message. */
static tINT32 _tsSdpSendCtrlBuffBuffered
(
 tSDP_CONN conn,
 tSDP_BUFF buff
)
{
  tINT32 result = 0;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);
  /*
   * Either post a send, or buffer the packet in the tx queue
   */
  if (0 < tsSdpGenericTableSize(&conn->send_ctrl) ||
      0 < (result = _tsSdpSendCtrlBuffTest(conn, buff))) {
    /*
     * save the buffer for later flushing into the post queue.
     */
    result = tsSdpGenericTablePutTail(&conn->send_ctrl, (tSDP_GENERIC)buff);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "POST: Error <%d> queueing control buffer for later send post.",
	       result);
      goto error;
    } /* if */
  } /* else */

  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failure during control send posting. <%d>", result);
    goto error;
  } /* if */

  return 0;
error:
  return result;
} /* _tsSdpSendCtrlBuffBuffered */

/* ========================================================================= */
/*.._tsSdpSendCtrlBuff -- Create and Send a buffered control message. */
static tINT32 _tsSdpSendCtrlBuff
(
 tSDP_CONN conn,
 tUINT8    mid,
 tBOOLEAN  se,
 tBOOLEAN  sig
)
{
  tINT32 result = 0;
  tSDP_BUFF buff;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * create the message, which contains just the bsdh header.
   * (don't need to worry about header space reservation)
   */
  buff = tsSdpBuffMainGet();
  if (NULL == buff) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failed to allocaate buffer for Disconnect.");
    result = -ENOMEM;
    goto error;
  } /* if */
  /*
   * setup header.
   */
  buff->bsdh_hdr        = (tSDP_MSG_BSDH)buff->data;
  buff->bsdh_hdr->mid   = mid;
  buff->bsdh_hdr->flags = TS_SDP_MSG_FLAG_NON_FLAG;
  buff->tail               = buff->data + sizeof(tSDP_MSG_BSDH_STRUCT);
  buff->data_size  = 0;
  /*
   * solicite event flag for IB sends.
   */
  if (TRUE == se) {

    TS_SDP_BUFF_F_SET_SE(buff);
  } /* if */
  else {

    TS_SDP_BUFF_F_CLR_SE(buff);
  } /* else */
  /*
   * try for unsignalled?
   */
  if (TRUE == sig) {

    TS_SDP_BUFF_F_CLR_UNSIG(buff);
  } /* if */
  else {

    TS_SDP_BUFF_F_SET_UNSIG(buff);
  } /* else */
  /*
   * Either post a send, or buffer the packet in the tx queue
   */
  result = _tsSdpSendCtrlBuffBuffered(conn, buff);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failed to post buffered control message. <%d>", result);
    (void)tsSdpBuffMainPut(buff);
  } /* if */

error:
  return result;
} /* _tsSdpSendCtrlBuff */

/* ========================================================================= */
/*.._tsSdpSendCtrlDisconnect -- Send a disconnect request. */
static tINT32 _tsSdpSendCtrlDisconnect
(
 tSDP_CONN  conn
)
{
  tINT32 result = 0;
  tSDP_BUFF buff;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * create the disconnect message, which contains just the bsdh header.
   * (don't need to worry about header space reservation)
   */
  buff = tsSdpBuffMainGet();
  if (NULL == buff) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failed to allocaate buffer for Disconnect.");
    result = -ENOMEM;
    goto error;
  } /* if */
  /*
   * setup header.
   */
  buff->bsdh_hdr        = (tSDP_MSG_BSDH)buff->data;
  buff->bsdh_hdr->mid   = TS_SDP_MSG_MID_DISCONNECT;
  buff->bsdh_hdr->flags = TS_SDP_MSG_FLAG_NON_FLAG;
  buff->tail            = buff->data + sizeof(tSDP_MSG_BSDH_STRUCT);
  buff->data_size  = 0;

  TS_SDP_BUFF_F_CLR_SE(buff);
  TS_SDP_BUFF_F_CLR_UNSIG(buff);
  /*
   * change state to reflect disconnect is queued. DIS_PEND_X to DIS_SEND_X
   */
  TS_SDP_ST_PEND_2_SEND(conn);
  /*
   * send
   */
  result = _tsSdpSendCtrlBuffBuffered(conn, buff);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: failed to post data send for disconnect. <%d>", result);
    goto error;
  } /* if */

  return 0;
error:
  return result;
} /* _tsSdpSendCtrlDisconnect */

/* ========================================================================= */
/*..tsSdpSendCtrlDisconnect -- potentially send a disconnect request. */
tINT32 tsSdpSendCtrlDisconnect
(
 tSDP_CONN  conn
)
{
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * Only create/post the message if there is no data in the data queue,
   * otherwise ignore the call. The flush queue will see to it, that a
   * Disconnect message gets queued/sent once the data queue is flushed
   * clean. The state is now in a disconnect send, the message will be
   * sent once data is flushed.
   */
  if (0 < (TS_SDP_ST_MASK_DIS_PEND & conn->state)) {

    if (0 == (TS_SDP_CONN_F_DIS_HOLD & conn->flags) &&
	0 == tsSdpGenericTableSize(&conn->send_queue) &&
	0 == conn->src_sent) {

      result =  _tsSdpSendCtrlDisconnect(conn);
    } /* if */
    else {

      result = 0;
    } /* else */
  } /* if */
  else {

    result = -EPROTO;
  } /* else */
  return result;
} /* tsSdpSendCtrlDisconnect */

/* ========================================================================= */
/*..tsSdpSendCtrlAck -- Send a gratuitous Ack. */
tINT32 tsSdpSendCtrlAck
(
 tSDP_CONN  conn
)
{
  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * The gratuitous ack is not really and ack, but an update of the number
   * of buffers posted for receive. Important when traffic is only moving
   * in one direction. We check to see if the buffer credits are going to
   * be sent in an already scheduled message before posting. The send
   * queue has different constraints/stalling conditions then the control
   * queue, so there is more checking to be done, then whether there is
   * data in the queue.
   */
  if (0 < tsSdpGenericTableSize(&conn->send_ctrl) ||
      (0 < tsSdpGenericTableSize(&conn->send_queue) &&
       2 < conn->l_advt_bf)) {

    return 0;
  } /* if */

  return _tsSdpSendCtrlBuff(conn, TS_SDP_MSG_MID_DATA, FALSE, FALSE);
} /* tsSdpSendCtrlAck */

/* ========================================================================= */
/*..tsSdpSendCtrlSendSm -- Send a request for buffered mode. */
tINT32 tsSdpSendCtrlSendSm
(
 tSDP_CONN  conn
)
{
  return _tsSdpSendCtrlBuff(conn, TS_SDP_MSG_MID_SEND_SM, TRUE, TRUE);
} /* tsSdpSendCtrlSendSm */

/* ========================================================================= */
/*..tsSdpSendCtrlSrcCancel -- Send a source cancel */
tINT32 tsSdpSendCtrlSrcCancel
(
 tSDP_CONN  conn
)
{
  return _tsSdpSendCtrlBuff(conn, TS_SDP_MSG_MID_SRC_CANCEL, TRUE, TRUE);
} /* tsSdpSendCtrlSrcCancel */

/* ========================================================================= */
/*..tsSdpSendCtrlSnkCancel -- Send a sink cancel */
tINT32 tsSdpSendCtrlSnkCancel
(
 tSDP_CONN  conn
)
{
  return _tsSdpSendCtrlBuff(conn, TS_SDP_MSG_MID_SNK_CANCEL, TRUE, TRUE);
} /* tsSdpSendCtrlSnkCancel */

/* ========================================================================= */
/*..tsSdpSendCtrlSnkCancelAck -- Send an ack for a sink cancel */
tINT32 tsSdpSendCtrlSnkCancelAck
(
 tSDP_CONN  conn
)
{
  return _tsSdpSendCtrlBuff(conn, TS_SDP_MSG_MID_SNK_CANCEL_ACK, TRUE, TRUE);
} /* tsSdpSendCtrlSnkCancelAck */

/* ========================================================================= */
/*..tsSdpSendCtrlAbort -- Send an abort message. */
tINT32 tsSdpSendCtrlAbort
(
 tSDP_CONN  conn
)
{
  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * send
   */
  return _tsSdpSendCtrlBuff(conn, TS_SDP_MSG_MID_ABORT_CONN, TRUE, TRUE);
} /* tsSdpSendCtrlAbort */

/* ========================================================================= */
/*..tsSdpSendCtrlResizeBuffAck -- Send an ack for a buffer size change */
tINT32 tsSdpSendCtrlResizeBuffAck
(
 tSDP_CONN  conn,
 tUINT32       size
)
{
  tSDP_MSG_CRBAH crbah;
  tINT32 result = 0;
  tSDP_BUFF buff;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * create the message, which contains just the bsdh header.
   * (don't need to worry about header space reservation)
   */
  buff = tsSdpBuffMainGet();
  if (NULL == buff) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failed to allocaate buffer for Disconnect.");
    result = -ENOMEM;
    goto error;
  } /* if */
  /*
   * setup header.
   */
  buff->tail                = buff->data;
  buff->bsdh_hdr         = (tSDP_MSG_BSDH)buff->tail;
  buff->bsdh_hdr->mid    = TS_SDP_MSG_MID_CH_RECV_BUF_ACK;
  buff->bsdh_hdr->flags  = TS_SDP_MSG_FLAG_NON_FLAG;
  buff->tail               += sizeof(tSDP_MSG_BSDH_STRUCT);
  crbah                     = (tSDP_MSG_CRBAH)buff->tail;
  crbah->size               = size;
  buff->tail               += sizeof(tSDP_MSG_CRBAH_STRUCT);

  TS_SDP_BUFF_F_CLR_SE(buff);
  TS_SDP_BUFF_F_CLR_UNSIG(buff);
  /*
   * endian swap of extended header
   */
  result = _tsSdpMsgHostToWireCRBAH(crbah);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * Either post a send, or buffer the packet in the tx queue
   */
  result = _tsSdpSendCtrlBuffBuffered(conn, buff);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failed to post buffered control message. <%d>", result);
    (void)tsSdpBuffMainPut(buff);
  } /* if */

error:
  return result;
} /* tsSdpSendCtrlResizeBuffAck */

/* ========================================================================= */
/*..tsSdpSendCtrlRdmaRdComp -- Send an rdma read completion */
tINT32 tsSdpSendCtrlRdmaRdComp
(
 tSDP_CONN  conn,
 tINT32     size
)
{
  tSDP_MSG_RRCH rrch;
  tINT32 result = 0;
  tSDP_BUFF buff;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * check size
   */
  if (0 > size) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: RDMA read completion size <%d> too small.", size);
    return -ERANGE;
  } /* if */
  /*
   * create the message, which contains just the bsdh header.
   * (don't need to worry about header space reservation)
   */
  buff = tsSdpBuffMainGet();
  if (NULL == buff) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failed to allocate buffer for RDMA read completion.");
    result = -ENOMEM;
    goto error;
  } /* if */
  /*
   * setup header.
   */
  buff->tail                = buff->data;
  buff->bsdh_hdr         = (tSDP_MSG_BSDH)buff->tail;
  buff->bsdh_hdr->mid    = TS_SDP_MSG_MID_RDMA_RD_COMP;
  buff->bsdh_hdr->flags  = TS_SDP_MSG_FLAG_NON_FLAG;
  buff->tail               += sizeof(tSDP_MSG_BSDH_STRUCT);
  rrch                      = (tSDP_MSG_RRCH)buff->tail;
  rrch->size                = (tUINT32)size;
  buff->tail               += sizeof(tSDP_MSG_RRCH_STRUCT);
  /*
   * solicit event
   */
#ifdef _TS_SDP_SE_UNSIG_BUG_WORKAROUND
  TS_SDP_BUFF_F_CLR_SE(buff);
#else
  TS_SDP_BUFF_F_SET_SE(buff);
#endif
  TS_SDP_BUFF_F_SET_UNSIG(buff);
  /*
   * set PIPE bit to request switch into pipeline mode.
   */
  TS_SDP_MSG_HDR_SET_REQ_PIPE(buff->bsdh_hdr);
  /*
   * endian swap of extended header
   */
  result = _tsSdpMsgHostToWireRRCH(rrch);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * Either post a send, or buffer the packet in the tx queue
   */
  result = _tsSdpSendCtrlBuffBuffered(conn, buff);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failed to post buffered control message. <%d>", result);
    (void)tsSdpBuffMainPut(buff);
  } /* if */

error:
  return result;
} /* tsSdpSendCtrlRdmaRdComp */

/* ========================================================================= */
/*..tsSdpSendCtrlRdmaWrComp -- Send an rdma write completion */
tINT32 tsSdpSendCtrlRdmaWrComp
(
 tSDP_CONN  conn,
 tUINT32    size
)
{
  tSDP_MSG_RWCH rwch;
  tINT32 result = 0;
  tSDP_BUFF buff;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * create the message, which contains just the bsdh header.
   * (don't need to worry about header space reservation)
   */
  buff = tsSdpBuffMainGet();
  if (NULL == buff) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failed to allocate buffer for RDMA write completion.");
    result = -ENOMEM;
    goto error;
  } /* if */
  /*
   * setup header.
   */
  buff->tail             = buff->data;
  buff->bsdh_hdr         = (tSDP_MSG_BSDH)buff->tail;
  buff->bsdh_hdr->mid    = TS_SDP_MSG_MID_RDMA_WR_COMP;
  buff->bsdh_hdr->flags  = TS_SDP_MSG_FLAG_NON_FLAG;
  buff->tail            += sizeof(tSDP_MSG_BSDH_STRUCT);
  rwch                   = (tSDP_MSG_RWCH)buff->tail;
  rwch->size             = size;
  buff->tail            += sizeof(tSDP_MSG_RWCH_STRUCT);
  /*
   * solicit event
   */
#ifdef _TS_SDP_SE_UNSIG_BUG_WORKAROUND
  TS_SDP_BUFF_F_CLR_SE(buff);
#else
  TS_SDP_BUFF_F_SET_SE(buff);
#endif
  TS_SDP_BUFF_F_SET_UNSIG(buff);
  /*
   * endian swap of extended header
   */
  result = _tsSdpMsgHostToWireRWCH(rwch);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * Either post a send, or buffer the packet in the tx queue
   */
  result = _tsSdpSendCtrlBuffBuffered(conn, buff);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failed to post buffered control message. <%d>", result);
    (void)tsSdpBuffMainPut(buff);
  } /* if */

error:
  return result;
} /* tsSdpSendCtrlRdmaWrComp */

/* ========================================================================= */
/*..tsSdpSendCtrlSnkAvail -- Send a sink available message */
tINT32 tsSdpSendCtrlSnkAvail
(
 tSDP_CONN conn,
 tUINT32   size,
 tUINT32   rkey,
 tUINT64   addr
)
{
  tSDP_MSG_SNKAH snkah;
  tINT32 result = 0;
  tSDP_BUFF buff;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * check mode
   */
  if (TS_SDP_MODE_PIPE != conn->recv_mode) {

    result = -EPROTO;
    goto error;
  } /* if */
  /*
   * create the message, which contains just the bsdh header.
   * (don't need to worry about header space reservation)
   */
  buff = tsSdpBuffMainGet();
  if (NULL == buff) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failed to allocate buffer for RDMA read completion.");
    result = -ENOMEM;
    goto error;
  } /* if */
  /*
   * setup header.
   */
  buff->tail             = buff->data;
  buff->bsdh_hdr         = (tSDP_MSG_BSDH)buff->tail;
  buff->bsdh_hdr->mid    = TS_SDP_MSG_MID_SNK_AVAIL;
  buff->bsdh_hdr->flags  = TS_SDP_MSG_FLAG_NON_FLAG;
  buff->tail            += sizeof(tSDP_MSG_BSDH_STRUCT);
  snkah                  = (tSDP_MSG_SNKAH)buff->tail;
  snkah->size            = size;
  snkah->r_key           = rkey;
  snkah->addr            = addr;
  snkah->non_disc        = conn->nond_recv;
  buff->tail            += sizeof(tSDP_MSG_SNKAH_STRUCT);
  buff->data_size        = 0;

  TS_SDP_BUFF_F_CLR_SE(buff);
  TS_SDP_BUFF_F_SET_UNSIG(buff);
  /*
   * endian swap of extended header
   */
  result = _tsSdpMsgHostToWireSNKAH(snkah);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * Either post a send, or buffer the packet in the tx queue
   */
  result = _tsSdpSendCtrlBuffBuffered(conn, buff);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failed to post buffered control message. <%d>", result);
    goto error;
  } /* if */

  result = 0;

error:
  return result;
} /* tsSdpSendCtrlSnkAvail */

/* ========================================================================= */
/*..tsSdpSendCtrlModeChange -- Send a mode change command */
tINT32 tsSdpSendCtrlModeChange
(
 tSDP_CONN  conn,
 tUINT8        mode
)
{
  tSDP_MSG_MCH mch;
  tINT32 result = 0;
  tSDP_BUFF buff;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * validate that the requested mode transition is OK.
   */
  switch (mode) {

  case TS_SDP_MSG_MCH_BUFF_RECV:  /* source to sink */

    conn->send_mode = ((TS_SDP_MODE_COMB == conn->send_mode) ?
		       TS_SDP_MODE_BUFF : TS_SDP_MODE_ERROR);
    break;
  case TS_SDP_MSG_MCH_COMB_SEND:  /* sink to source */

    conn->recv_mode = ((TS_SDP_MODE_BUFF == conn->recv_mode) ?
		       TS_SDP_MODE_COMB : TS_SDP_MODE_ERROR);
    break;
  case TS_SDP_MSG_MCH_PIPE_RECV:  /* source to sink */

    conn->send_mode = ((TS_SDP_MODE_COMB == conn->send_mode) ?
		       TS_SDP_MODE_PIPE : TS_SDP_MODE_ERROR);
    break;
  case TS_SDP_MSG_MCH_COMB_RECV:  /* source to sink */

    conn->send_mode = ((TS_SDP_MODE_PIPE == conn->send_mode) ?
		       TS_SDP_MODE_COMB : TS_SDP_MODE_ERROR);
    break;
  default:

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Invalid mode transition <%d> requested.", mode);
    result = -EPROTO;
    goto error;
  } /* switch */

  if (TS_SDP_MODE_ERROR == conn->send_mode ||
      TS_SDP_MODE_ERROR == conn->recv_mode) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: mode <%d> transition error. <%d:%d>", mode,
	     conn->send_mode, conn->recv_mode);
    result = -EPROTO;
    goto error;
  } /* else */
  /*
   * create the message, which contains just the bsdh header.
   * (don't need to worry about header space reservation)
   */
  buff = tsSdpBuffMainGet();
  if (NULL == buff) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failed to allocate buffer for ModeChange.");
    result = -ENOMEM;
    goto error;
  } /* if */
  /*
   * setup header.
   */
  buff->tail             = buff->data;
  buff->bsdh_hdr         = (tSDP_MSG_BSDH)buff->tail;
  buff->bsdh_hdr->mid    = TS_SDP_MSG_MID_MODE_CHANGE;
  buff->bsdh_hdr->flags  = TS_SDP_MSG_FLAG_NON_FLAG;
  buff->tail            += sizeof(tSDP_MSG_BSDH_STRUCT);
  mch                    = (tSDP_MSG_MCH)buff->tail;
  buff->tail            += sizeof(tSDP_MSG_MCH_STRUCT);

  TS_SDP_BUFF_F_SET_SE(buff);
  TS_SDP_BUFF_F_CLR_UNSIG(buff);
  TS_SDP_MSG_MCH_SET_MODE(mch, mode);
  /*
   * endian swap of extended header
   */
  result = _tsSdpMsgHostToWireMCH(mch);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * Either post a send, or buffer the packet in the tx queue
   */
  result = _tsSdpSendCtrlBuffBuffered(conn, buff);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failed to post buffered control message. <%d>", result);
    (void)tsSdpBuffMainPut(buff);
  } /* if */

error:
  return result;
} /* tsSdpSendCtrlModeChange */

/* ========================================================================= */
/*.._tsSdpSendAdvtFlush -- Flush passive sink advertisments */
static tINT32 _tsSdpSendAdvtFlush
(
 tSDP_CONN  conn
)
{
  tSDP_ADVT advt;
  tINT32 result;
  /*
   * If there is no data in the pending or active send pipes, and a
   * partially complete sink advertisment is pending, then it needs
   * to be completed. It might be some time until more data is ready
   * for transmission, and the remote host needs to be notified of
   * present data. (rdma ping-pong letency test...)
   */
  if (0 == tsSdpGenericTableSize(&conn->send_queue)) {
    /*
     * might be more aggressive then we want it to be. maybe check if
     * the active sink queue is empty as well?
     */
    advt = tsSdpConnAdvtTableLook(&conn->snk_pend);
    if (NULL != advt &&
	0 < advt->post) {

      advt = tsSdpConnAdvtTableGet(&conn->snk_pend);
      TS_EXPECT(MOD_LNX_SDP, (NULL != advt));

      result = tsSdpSendCtrlRdmaWrComp(conn, advt->post);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));

      result = tsSdpConnAdvtDestroy(advt);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));
      /*
       * update sink advertisments.
       */
      conn->snk_recv--;
    } /* if */
  } /* if */

  return 0;
} /* _tsSdpSendAdvtFlush */

/* --------------------------------------------------------------------- */
/*                                                                       */
/*                          GENERAL functions                            */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpSendFlush -- Flush buffers from send queue, in to send post. */
tINT32 tsSdpSendFlush
(
 tSDP_CONN conn
)
{
  tINT32 result = 0;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * keep posting sends as long as there is room for an SDP post.
   * Priority goes to control messages, and we need to follow the
   * send credit utilization rules.
   */
  result = _tsSdpSendCtrlBuffFlush(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: failed to flush control. <%d>", result);
    goto done;
  } /* if */
  /*
   * data flush
   */
  result = _tsSdpSendDataQueueFlush(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: failed to flush data. <%d>", result);
    goto done;
  } /* if */
  /*
   * Sink advertisment flush.
   */
  if (0 < tsSdpConnAdvtTableSize(&conn->snk_pend)) {

    result = _tsSdpSendAdvtFlush(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "POST: failed to flush sink advertisments. <%d>", result);
      goto done;
    } /* if */
  } /* if */
  /*
   * disconnect flush
   */
  if (0 < (TS_SDP_ST_MASK_DIS_PEND & conn->state)) {

    result = tsSdpSendCtrlDisconnect(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "POST: failed to post disconnect during flush. <%d>", result);
      goto done;
    } /* if */
  } /* if */
  /*
   * see if there is enough buffer to wake/notify writers
   */
  tsSdpInetWakeSend(conn); /*  conn->sk->write_space(conn->sk); */

  return 0;
done:
  return result;
} /* tsSdpSendFlush */

/* ========================================================================= */
/*..tsSdpInetSend -- send data from user space to the network. */
tINT32 tsSdpInetSend
(
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
)
{
  struct sock *sk;
  tSDP_CONN    conn;
  tSDP_BUFF    buff;
  tINT32       result = 0;
  tINT32       copied = 0;
  tINT32       copy;
  tINT32       oob;
  long         timeout = -1;

  TS_CHECK_NULL(sock, -EINVAL);
  TS_CHECK_NULL(sock->sk, -EINVAL);
  TS_CHECK_NULL(msg, -EINVAL);

  sk   = sock->sk;
  conn = TS_SDP_GET_CONN(sk);

#ifdef _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: send <%08x:%04x> <%08x:%04x> <%04x> size <%d> flags <%08x>",
	   conn->src_addr, conn->src_port, conn->dst_addr, conn->dst_port,
	   conn->istate, size, msg->msg_flags);
#endif

  TS_SDP_CONN_LOCK(conn);
  /*
   * ESTABLISED and CLOSE can send, while CONNECT and ACCEPTED can
   * continue being processed, it'll wait below until the send window
   * is opened on sucessful connect, or error on an unsucessful attempt.
   */
  if (0 < (TS_SDP_ST_MASK_CLOSED & conn->istate)) {

    result = -EPIPE;
    goto done;
  } /* if */
  /*
   * clear ASYN space bit, it'll be reset if there is no space.
   */
  clear_bit(SOCK_ASYNC_NOSPACE, &TS_SDP_OS_SK_SOCKET(sk)->flags);
  oob = (MSG_OOB & msg->msg_flags);
  /*
   * process data first if window is open, next check conditions, then
   * wait if there is more work to be done. The absolute window size is
   * used to 'block' the caller if the connection is still connecting.
   */
  while (copied < size) {
    /*
     * send while there is room... (thresholds should be observed...)
     * use a different threshold for urgent data to allow some space
     * for sending.
     */
    while (0 < __tsSdpInetWriteSpace(conn, oob)) {

      buff = _tsSdpSendDataBuffGet(conn);
      if (NULL == buff) {

	result = -ENOMEM;
	goto done;
      } /* if */

      copy = min((size_t)(buff->end - buff->tail), (size_t)(size - copied));
      copy = min(copy, __tsSdpInetWriteSpace(conn, oob));

#ifndef _TS_SDP_DATA_PATH_NULL
      result = memcpy_fromiovec(buff->tail, msg->msg_iov, copy);
      if (0 > result) {

	(void)tsSdpBuffMainPut(buff);
	goto done;
      } /* if */
#endif
      buff->tail       += copy;
      copied           += copy;

      TS_SDP_CONN_STAT_SEND_INC(conn, copy);

      result = _tsSdpSendDataBuffPut(conn, buff, copy,
				     ((copied == size) ? oob : 0));
      if (0 > result) {

	goto done;
      } /* if */

      if (copied == size) {

	goto done;
      } /* if */
    } /* while */
    /*
     * onetime setup of timeout, but only if it's needed.
     */
    if (0 > timeout) {

      timeout = sock_sndtimeo(sk, (MSG_DONTWAIT & msg->msg_flags));
    } /* if */
    /*
     * check status.
     */
    set_bit(SOCK_ASYNC_NOSPACE, &TS_SDP_OS_SK_SOCKET(sk)->flags);
    set_bit(SOCK_NOSPACE, &TS_SDP_OS_SK_SOCKET(sk)->flags);

    if (0 != TS_SDP_OS_CONN_GET_ERR(conn)) {

      result = (0 < copied) ? 0 : TS_SDP_CONN_ERROR(conn);
      break;
    } /* if */

    if (0 < (TS_SDP_SHUTDOWN_SEND & conn->shutdown)) {

      result = -EPIPE;
      break;
    } /* if */

    if (TS_SDP_SOCK_ST_ERROR == conn->istate) {

      result = -EPROTO; /* error should always be set, but just in case */
      break;
    } /* if */

    if (0 == timeout) {

      result = -EAGAIN;
      break;
    } /* if */

    if (signal_pending(current)) {

      result = (0 < timeout) ? sock_intr_errno(timeout) : -EAGAIN;
      break;
    } /* if */
    /*
     * wait
     */
    {
      DECLARE_WAITQUEUE(wait, current);
      add_wait_queue(TS_SDP_OS_SK_SLEEP(sk), &wait);
      set_current_state(TASK_INTERRUPTIBLE);
      /*
       * ASYNC_NOSPACE is only set if we're not sleeping, while NOSPACE is
       * set whenever there is no space, and is only cleared once space
       * opens up, in DevConnAck()
       */
      clear_bit(SOCK_ASYNC_NOSPACE, &TS_SDP_OS_SK_SOCKET(sk)->flags);

      TS_SDP_CONN_UNLOCK(conn);
      if (!(0 < __tsSdpInetWriteSpace(conn, oob))) {

	timeout = schedule_timeout(timeout);
      } /* if */
      TS_SDP_CONN_LOCK(conn);

      remove_wait_queue(TS_SDP_OS_SK_SLEEP(sk), &wait);
      set_current_state(TASK_RUNNING);
    }
  } /* while */

done:
  TS_SDP_CONN_UNLOCK(conn);
  result = ((0 < copied) ? copied : result);

  if (-EPIPE == result &&
      0 == (MSG_NOSIGNAL & msg->msg_flags)) {

    send_sig(SIGPIPE, current, 0);
  } /* if */

  return result;
} /* tsSdpInetSend */

#ifdef _TS_SDP_AIO_SUPPORT
/* ========================================================================= */
/*.._tsSdpSendDataIocbFast -- write multiple SDP buffers from an iocb */
static tINT32 _tsSdpSendDataIocbFast
(
 tSDP_CONN        conn,
 struct kvec_dst *src,
 tINT32           len
)
{
  tSDP_BUFF buff;
  tINT32    copied = 0;
  tINT32    expect;
  tINT32    result;
  tINT32    copy;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(src, -EINVAL);
  /*
   * loop through queued buffers and copy them to the destination
   */
  while (copied < len &&
	 0 < __tsSdpInetWriteSpace(conn, 0)) {
    /*
     * get a buffer for posting.
     */
    buff = _tsSdpSendDataBuffGet(conn);
    if (NULL == buff) {

      result = -ENOMEM;
      goto done;
    } /* if */

    copy = min((len - copied), __tsSdpInetWriteSpace(conn, 0));

#ifdef _TS_SDP_AIO_SUPPORT
    result = _tsSdpSendDataIocbBuffKvec(src, buff, copy);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
               "WRITE: error <%d> copying data <%d> from vector.",
               result, copy);

      expect = tsSdpBuffMainPut(buff);
      TS_EXPECT(MOD_LNX_SDP, !(0 > expect));

      goto done;
    } /* if */
#endif

    copied += result;

    result = _tsSdpSendDataBuffPut(conn, buff, result, 0);
    if (0 > result) {

      goto done;
    } /* if */
  } /* while */

  result = 0;
done:
  return (0 < copied) ? copied : result;
} /* _tsSdpSendDataIocbFast */

/* ========================================================================= */
/*..tsSdpInetWrite -- send AIO data from user space to the network. */
tINT32 tsSdpInetWrite
(
 struct socket *sock,
 kvec_cb_t      cb,
 size_t         size
)
{
  struct kiocb   *req;
  struct kvec_dst src;
  struct sock    *sk;
  tSDP_CONN       conn;
  tSDP_IOCB       iocb;
  tINT32 copied = 0;
  tINT32 result = 0;

  TS_CHECK_NULL(sock,     -EINVAL);
  TS_CHECK_NULL(sock->sk, -EINVAL);
  TS_CHECK_NULL(cb.fn,    -EINVAL);
  TS_CHECK_NULL(cb.data,  -EINVAL);
  TS_CHECK_NULL(cb.vec,   -EINVAL);

  sk   = sock->sk;
  conn = TS_SDP_GET_CONN(sk);
  req  = (struct kiocb *)cb.data;

#ifdef _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: Write <%08x:%04x> <%08x:%04x> <%04x> size <%d>",
	   conn->src_addr, conn->src_port, conn->dst_addr, conn->dst_port,
	   conn->istate, size);
  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: Write Kvec <%d:%d> veclet <%d:%d> args <%08x> kiocb "
	   "<%d:%d:%08x>",
	   cb.vec->max_nr, cb.vec->nr,
	   cb.vec->veclet->offset, cb.vec->veclet->length,
	   (tUINT32)cb.fn, req->key, req->users, (tUINT32)req->data);
#endif

  /*
   * initialize memory destination
   */
  kvec_dst_init(&src, KM_USER0);
  /*
   * set data source address to cb vectors address
   */
  kvec_dst_set(&src, cb.vec->veclet);

  TS_SDP_CONN_LOCK(conn);
  /*
   * check error conditions before allowing writes...
   * (send_mask and shutdown_mask should be redundant)
   */
  if (0 < (TS_SDP_ST_MASK_CLOSED & conn->istate) ||
      0 < (TS_SDP_SHUTDOWN_SEND & conn->shutdown)) {

    result = ((0 < TS_SDP_OS_CONN_GET_ERR(conn)) ?
	      (0 - TS_SDP_OS_CONN_GET_ERR(conn)) : -EPIPE);
    goto done;
  } /* if */
  /*
   * If the IOCB queue is empty and this is a candidate for BUFFERED
   * transmission, attempt to send.
   */
  if ((conn->src_zthresh > size ||
       TS_SDP_MODE_BUFF == conn->send_mode) &&
      0 == tsSdpGenericTableTypeSize(&conn->send_queue,
				     TS_SDP_GENERIC_TYPE_IOCB)) {
    /*
     * write data onto SDP if there is sufficient window space.
     */
    copied = _tsSdpSendDataIocbFast(conn, &src, size);
    if (0 > copied) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "WRITE: error <%d> flushing data <%d> from src <%d:%d:%d>",
	       copied, size, src.space, src.offset, src.type);
      result = copied;
      goto done;
    } /* if */
  } /* if */
  /*
   * If there is room left in the source save it in an IOCB, otherwise
   * report success in the callback.
   */
  if (size > copied) {
    /*
     * more data, save the cb for processing as data arrives.
     */
    iocb = tsSdpConnIocbCreate();
    if (NULL == iocb) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "WRITE: Failed to allocate iocb to save cb. <%d:%d>",
	       size, copied);
      result = (0 < copied) ? copied : -ENOMEM;
      goto done;
    } /* if */

    iocb->req      = req;
    iocb->kvec.src = src;
    iocb->cb       = cb;
    iocb->len      = size - copied;
    iocb->post     = copied;
    iocb->size     = size;
    iocb->key      = req->key;

    TS_SDP_CONN_STAT_WQ_INC(conn, iocb->size);

    req->data   = sk;
    req->cancel = _tsSdpInetWriteIocbCancel;

    conn->send_pipe += iocb->len;

    result = _tsSdpSendDataQueue(conn, (tSDP_GENERIC)iocb);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "WRITE: Error <%d> queueing write IOCB. <%d>",
	       result, iocb->key, tsSdpGenericTableSize(&conn->send_queue));

      (void)tsSdpConnIocbDestroy(iocb);

      result = (0 < copied) ? copied : result;
      goto done; /* synchronous error */
    } /* if */

    result = 0;
  } /* if */
  else {

    TS_SDP_CONN_STAT_WRITE_INC(conn, copied);
    result = copied;
  } /* else */

done:
  TS_SDP_CONN_UNLOCK(conn);

  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: Error <%d> writing IOCB <%d>.",
	     result, req->key);
  } /* if */
  /*
   * If no data copied, return result, otherwise only complete event
   * if there was an error.
   */
  return result;
} /* tsSdpInetWrite */
#endif

/***************************************************************************
*                       Modification History
*
*    $Log: sdp_send.c,v $
*    Revision 1.48  2004/02/24 23:48:51  roland
*    Add open source license headers
*
*    Revision 1.47  2004/02/24 17:24:31  roland
*    Change #include <sdp_xxx> to #include "sdp_xxx" so we can build under drivers/infiniband in 2.6 kernel
*
*    Revision 1.46  2004/02/24 08:27:13  libor
*    Initial Linux kernel 2.6 support
*
*    Revision 1.45  2004/02/21 01:14:42  libor
*    state transition bugs and slight simplification
*
*    Revision 1.44  2003/12/12 02:38:37  libor
*    Initial pass at TS_API NG
*
*    Revision 1.43  2003/11/05 23:54:47  johannes
*    64-bit portability fixes
*    kvec_read and kvec_write are prototyped with size_t which is variable length
*    depending on the architecture
*    Remove some warnings about casting from pointers to integers of different
*    sizes (safe since it's always a 32 bit integer store in a pointer)
*
*    Revision 1.42  2003/10/24 04:47:42  libor
*    expanded RHEL 3.0 AIO support
*
*    Revision 1.41  2003/10/14 07:17:58  libor
*    kernel IOCB should not have data reference count decremented
*    when the field is empty.
*    Bugzilla number: 2690
*
*    Revision 1.40  2003/09/23 00:54:09  libor
*    Send credits must be sent when the data send queue is stalled.
*    Bugzilla number: 2562
*
*    Revision 1.39  2003/09/18 01:36:14  libor
*    The send queue size needs to be checked more aggresively.
*    Bugzilla number: 2428
*
*    Revision 1.38  2003/08/20 01:57:29  libor
*     RedHat Enterprise Linux 3.0 support changes
*
*    Revision 1.37  2003/07/31 01:44:48  libor
*    verbose data path fix
*
*    Revision 1.36  2003/07/18 20:41:57  libor
*    Source Available Cancel implementation, source side.
*    Bugzilla number: 2214
*
*    Revision 1.35  2003/07/12 00:56:28  libor
*    Cancel bug workaround/fix (bug 2169)
*
*    Revision 1.34  2003/06/16 21:36:11  libor
*    reductions in lock aquisition to imporce performance.
*
*    Revision 1.33  2003/06/13 00:27:45  libor
*    OOB/URG data fixes for sockets and AIO.
*
*    Revision 1.32  2003/06/12 03:22:04  libor
*    OOB handling and some performance improvments.
*
*    Revision 1.31  2003/05/29 00:27:47  libor
*    unsignalled completion processing
*
*    Revision 1.30  2003/05/28 18:03:30  libor
*    some bug fixes
*
*    Revision 1.29  2003/05/10 01:12:33  libor
*    connection termination state machine fixes!
*
*    Revision 1.28  2003/05/08 18:24:39  libor
*    return error for send on a listen socket
*
*    Revision 1.27  2003/05/07 19:51:11  libor
*    socket/TCP compatability fixes and Oracle bug.
*
*    Revision 1.26  2003/05/02 20:44:29  libor
*    SDP support for multiple HCAs
*
*    Revision 1.25  2003/04/30 18:24:07  libor
*    third pass at some simplifications for porting
*
*    Revision 1.24  2003/04/29 17:34:56  libor
*    second pass at some simplifications for porting
*
*    Revision 1.23  2003/04/28 20:33:15  libor
*    first pass at some simplifications for porting
*
*    Revision 1.22  2003/04/16 20:06:55  roland
*    64-bit portability cleanups
*
*    Revision 1.21  2003/04/16 00:40:00  libor
*    simplify send flush path.
*
*    Revision 1.20  2003/04/15 01:45:48  libor
*    RDMA write for some buffered sends.
*
*    Revision 1.19  2003/03/06 17:20:31  libor
*    move to per connection wrids
*
*    Revision 1.18  2003/02/26 02:34:14  libor
*    compile out stats by default
*
*    Revision 1.17  2003/02/24 17:59:20  libor
*    bug fix
*
*    Revision 1.16  2003/02/22 02:17:54  libor
*    unsignalled completion support
*
*    Revision 1.15  2003/02/18 23:13:16  libor
*    new locking
*
*    Revision 1.14  2003/02/11 02:19:42  libor
*    connection reference bug, plus partial source cancel
*
*    Revision 1.13  2003/02/10 21:57:06  libor
*    Sink Available cancellation
*
*    Revision 1.12  2003/02/08 02:09:05  libor
*    write cancellation changes.
*
*    Revision 1.11  2003/02/07 02:00:43  libor
*    sink advertisment tunning.
*
*    Revision 1.10  2003/02/06 04:57:34  libor
*    SDP Write ZCOPY for AIO.
*
*    Revision 1.9  2003/01/22 02:45:30  libor
*    limit rdma/send queue depths
*
*    Revision 1.8  2003/01/22 01:18:40  libor
*    Fixup Combined mode SrcAvail message to contain ULP data
*    using new memory model.
*
*    Revision 1.7  2003/01/21 21:24:30  libor
*    IRQ safe AIO highmem managment code.
*
*    Revision 1.6  2003/01/20 20:43:42  libor
*    Move event notification from thread to IRQ
*
*    Revision 1.5  2002/12/20 02:12:26  libor
*    AIO cancellation crashes and hangs resolved.
*
*    Revision 1.4  2002/12/11 03:39:27  libor
*    AIO active write cancellation
*
*    Revision 1.3  2002/12/04 19:26:30  libor
*    some bug fixes. /proc rearranged.
*
*    Revision 1.2  2002/12/03 03:13:05  libor
*    couple AIO buffered mode bug fixes.
*
*    Revision 1.1  2002/12/02 23:44:51  libor
*    initial zcopy/rdma code
*
*
***************************************************************************/
