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

  $Id: sdp_rcvd.c,v 1.36 2004/02/24 23:48:50 roland Exp $
*/

#include "sdp_main.h"

/* --------------------------------------------------------------------- */
/*                                                                       */
/* Specific MID handler functions. (RECV)                                */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsSdpEventRecvDisconnect --  */
static tINT32 _tsSdpEventRecvDisconnect
(
 tSDP_CONN  conn,
 tSDP_BUFF  buff
)
{
  tINT32 result = 0;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "STRM: <%d> <%08x:%04x><%08x:%04x> disconnect received <%04x>",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->state);

  switch (conn->state) {
  case TS_SDP_CONN_ST_ESTABLISHED:
    /*
     * transition state
     */
    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_DIS_RECV_1);
    /*
     * initiate disconnect to framework
     */
    result = tsSdpSockClose(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "STRM: Error initiating close to stream interface. <%d>",
	       result);
      goto error;
    } /* if */

    break;
  case TS_SDP_CONN_ST_DIS_PEND_1:

    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_DIS_PEND_R);

    break;
  case TS_SDP_CONN_ST_DIS_SEND_1:

    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_DIS_RECV_R);

    break;
  case TS_SDP_CONN_ST_DIS_SENT_1:
    /*
     * After receiving the final disconnet and posting the DREQ, the
     * next step is TIME_WAIT from the CM
     */
    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_DIS_RECV_R);
    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_TIME_WAIT_1);
    /*
     * acknowledge disconnect to framework (we're in active disconnect)
     */
    result = tsSdpSockClosing(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "STRM: Error confirming close to stream interface. <%d>",
	       result);
      goto error;
    } /* if */
    /*
     * Begin IB/CM disconnect
     */
    result = tsSdpCmDisconnect(conn);
    /*
     * if the remote DREQ was already received, but unprocessed, do
     * not treat it as an error
     */
    if (0 > result) {

      if (-EPROTO != result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "STRM: Error posting CM disconnect. <%d>", result);
	goto error;
      } /* if */
      else {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "STRM: CM disconnect of disconnected conn. OK");
      } /* else */
    } /* if */

    break;
  default:

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	     "STRM: Received disconnect in an unexpected state. <%04x>",
	     conn->state);
    result = -EFAULT;
    goto error;
    break;
  } /* switch */

  return 0;
error:
  return result;
} /* _tsSdpEventRecvDisconnect */

/* ========================================================================= */
/*.._tsSdpEventRecvAbortConn --  */
static tINT32 _tsSdpEventRecvAbortConn
(
 tSDP_CONN  conn,
 tSDP_BUFF      buff
)
{
  tINT32 result = 0;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "STRM: <%d> <%08x:%04x><%08x:%04x> abort received in state <%04x>",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->state);
  /*
   * Connection should be in some post DisConn recveived state.
   * Notify gateway interface about abort
   */
  switch (conn->state) {
  case TS_SDP_CONN_ST_DIS_RECV_1:
  case TS_SDP_CONN_ST_DIS_PEND_R:
  case TS_SDP_CONN_ST_DIS_RECV_R:
  case TS_SDP_CONN_ST_DIS_PEND_2:
  case TS_SDP_CONN_ST_DIS_SEND_2:
  case TS_SDP_CONN_ST_DIS_SENT_2:

    result = tsSdpAbort(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	       "EVENT: error <%d> during abort in state. <%04x>",
	       result, conn->state);
    } /* if */

    break;
  default:

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	     "EVENT: received ABORT message in incorrect state. <%04x>",
	     conn->state);
    result = -EPROTO;
  } /* switch */

  return result;
} /* _tsSdpEventRecvAbortConn */

/* ========================================================================= */
/*.._tsSdpEventRecvSendSm --  */
static tINT32 _tsSdpEventRecvSendSm
(
 tSDP_CONN  conn,
 tSDP_BUFF  buff
)
{
  tSDP_IOCB iocb;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);
  /*
   * 1) Conn is not in source cancel mode. Send active IOCB
   *    using buffered mode
   * 2) Conn is in source cancel, and this message acks the cancel.
   *    Release all active IOCBs in the source queue.
   * 3) Conn is in source cancel, but this message doesn't ack the cancel.
   *    Do nothing, can't send since the IOCB is being cancelled, but
   *    cannot release the IOCB since the cancel has yet to be ack'd
   */
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: SendSM. active <%d> count <%d> flags <%08x>",
	   conn->src_sent, conn->src_cncl, conn->flags);

  if (0 < (TS_SDP_CONN_F_SRC_CANCEL_L & conn->flags) &&
      TS_SDP_SEQ_GTE(buff->bsdh_hdr->seq_ack, conn->src_cseq)) {
    /*
     * drain the active source queue
     */
    while (NULL != (iocb = tsSdpConnIocbTableGetTail(&conn->w_src))) {

      TS_EXPECT(MOD_LNX_SDP, (0 < (TS_SDP_IOCB_F_ACTIVE & iocb->flags)));
      TS_EXPECT(MOD_LNX_SDP, (0 < (TS_SDP_IOCB_F_CANCEL & iocb->flags)));

      conn->src_sent--;

      result = tsSdpConnIocbComplete(iocb, 0);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));
    } /* while */
    /*
     * Cancel complete, clear the state.
     */
    conn->src_cncl = 0;
    conn->flags   &= ~(TS_SDP_CONN_F_SRC_CANCEL_L);
  } /* if */

  return 0;
} /* _tsSdpEventRecvSendSm */

/* ========================================================================= */
/*.._tsSdpEventRecvRdmaWriteComp --  */
static tINT32 _tsSdpEventRecvRdmaWriteComp
(
 tSDP_CONN  conn,
 tSDP_BUFF  buff
)
{
  tSDP_MSG_RWCH rwch;
  tSDP_IOCB iocb;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  rwch = (tSDP_MSG_RWCH)buff->data;
  buff->data = buff->data + sizeof(tSDP_MSG_RWCH_STRUCT);

  result = _tsSdpMsgWireToHostRWCH(rwch);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * lookup active IOCB read.
   */
  iocb = tsSdpConnIocbTableLook(&conn->r_snk);
  if (NULL == iocb) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "EVENT: Cannot find outstanding IOCB for RdmaRead Completion.");
    result = -EPROTO;
    goto error;
  } /* if */

  TS_EXPECT(MOD_LNX_SDP, (0 < (TS_SDP_IOCB_F_RDMA_W & iocb->flags)));

#ifdef _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: RDMA Write Complete <%d> <%d:%d:%d> mode <%d> active <%d>",
	   rwch->size, iocb->len, iocb->size, iocb->key,
	   conn->recv_mode, conn->snk_sent);
#endif
  /*
   * update IOCB
   */
  if (rwch->size > iocb->len) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "EVENT: IOCB and RdmaWrite Completion size mismatch. <%d:%d>",
	     rwch->size, iocb->len);
    result = -EPROTO;
    goto error;
  } /* if */
  /*
   * Iocb is done, deregister memory, and generate completion.
   */
  iocb = tsSdpConnIocbTableGetHead(&conn->r_snk);
  TS_EXPECT(MOD_LNX_SDP, (NULL != iocb));

  conn->snk_sent--;

  iocb->len  -= rwch->size;
  iocb->post += rwch->size;

  TS_SDP_CONN_STAT_SNK_INC(conn);
  TS_SDP_CONN_STAT_READ_INC(conn, iocb->post);
  TS_SDP_CONN_STAT_RQ_DEC(conn, iocb->size);

  result = tsSdpConnIocbComplete(iocb, 0);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: Error <%d> completing iocb. <%d>", result, iocb->key);
    (void)tsSdpConnIocbDestroy(iocb);
    goto error;
  } /* if */

  return 0;
error:
  return result;
} /* _tsSdpEventRecvRdmaWriteComp */

/* ========================================================================= */
/*.._tsSdpEventRecvRdmaReadComp --  */
static tINT32 _tsSdpEventRecvRdmaReadComp
(
 tSDP_CONN  conn,
 tSDP_BUFF  buff
)
{
  tSDP_MSG_RRCH rrch;
  tSDP_IOCB iocb;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  rrch = (tSDP_MSG_RRCH)buff->data;
  buff->data = buff->data + sizeof(tSDP_MSG_RRCH_STRUCT);

  result = _tsSdpMsgWireToHostRRCH(rrch);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * lookup IOCB read.
   */
  iocb = tsSdpConnIocbTableLook(&conn->w_src);
  if (NULL == iocb) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "EVENT: Cannot find outstanding IOCB for RdmaRead Completion.");
    result = -EPROTO;
    goto error;
  } /* if */

  TS_SDP_CONN_STAT_SRC_INC(conn);
#ifdef _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: RDMA Read Complete <%d> <%d:%d:%d> mode <%d> active <%d>",
	   rrch->size, iocb->len, iocb->size, iocb->key,
	   conn->recv_mode, conn->src_sent);
#endif
  /*
   * update IOCB
   */
  if (rrch->size > iocb->len) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "EVENT: IOCB and RdmaRead Completion size mismatch. <%d:%d>",
	     rrch->size, iocb->len);
    result = -EPROTO;
    goto error;
  } /* if */
  /*
   * In combined mode the total RDMA size is going to be the buffer
   * size minus the size sent in the SrcAvail. We could fix up the
   * iocb->post in the SrcAvailSend function, but it's better to do
   * it on the first successful RDMA to make sure we don't get a
   * false positive of data sent. (specification ambiguity/pain)
   */
  iocb->post += (0 == iocb->post) ? (iocb->size - iocb->len) : 0;
  iocb->len  -= rrch->size;
  iocb->post += rrch->size;

  conn->send_pipe  -= rrch->size;
  conn->oob_offset -= (0 < conn->oob_offset) ? rrch->size : 0;

  /*
   * If iocb is done, deregister memory, and generate completion.
   */
  if (!(0 < iocb->len)) {

    iocb = tsSdpConnIocbTableGetHead(&conn->w_src);
    TS_EXPECT(MOD_LNX_SDP, (NULL != iocb));

    conn->src_sent--;

    TS_SDP_CONN_STAT_WRITE_INC(conn, iocb->post);
    TS_SDP_CONN_STAT_WQ_DEC(conn, iocb->size);

    result = tsSdpConnIocbComplete(iocb, 0);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: Error <%d> completing iocb. <%d>", result, iocb->key);
      (void)tsSdpConnIocbDestroy(iocb);
      goto error;
    } /* if */
  } /* if */
  /*
   * If Source Cancel was in process, and there are no more outstanding
   * advertisments, then it should now be cleared.
   */
  if (0 < (TS_SDP_CONN_F_SRC_CANCEL_L & conn->flags) &&
      0 == tsSdpConnIocbTableSize(&conn->w_src)) {

    conn->src_cncl = 0;
    conn->flags   &= ~(TS_SDP_CONN_F_SRC_CANCEL_L);
  } /* if */

  return 0;
error:
  return result;
} /* _tsSdpEventRecvRdmaReadComp */

/* ========================================================================= */
/*.._tsSdpEventRecvModeChange --  */
static tINT32 _tsSdpEventRecvModeChange
(
 tSDP_CONN  conn,
 tSDP_BUFF  buff
)
{
  tSDP_MSG_MCH  mch;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  mch = (tSDP_MSG_MCH)buff->data;
  buff->data = buff->data + sizeof(tSDP_MSG_MCH_STRUCT);

  result = _tsSdpMsgWireToHostMCH(mch);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: Mode transition request <%d> from current mode. <%d:%d>",
	   TS_SDP_MSG_MCH_GET_MODE(mch), conn->recv_mode, conn->send_mode);
  /*
   * Check if the mode change is to the same mode.
   */
  if (((TS_SDP_MSG_MCH_GET_MODE(mch) & 0x7) ==
       ((0 < (TS_SDP_MSG_MCH_GET_MODE(mch) & 0x8)) ?
	conn->send_mode : conn->recv_mode))) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: Mode transition <%d> is a nop. <%d:%d>",
	     TS_SDP_MSG_MCH_GET_MODE(mch), conn->recv_mode, conn->send_mode);
    result = -EPROTO;
    goto error;
  } /* if */
  /*
   * process mode change requests based on which state we're in
   */
  switch (TS_SDP_MSG_MCH_GET_MODE(mch)) {

  case TS_SDP_MSG_MCH_BUFF_RECV:  /* source to sink */

    if (TS_SDP_MODE_COMB != conn->recv_mode) {

      goto mode_error;
      result = -EPROTO;
    } /* if */

    if (0 < conn->src_recv) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: Illegal mode transition <%d> SrcAvail pending. <%d>",
	       TS_SDP_MSG_MCH_GET_MODE(mch), conn->src_recv);
      result = -EPROTO;
      goto error;
    } /* if */

    break;
  case TS_SDP_MSG_MCH_COMB_SEND:  /* sink to source */

    if (TS_SDP_MODE_BUFF != conn->send_mode) {

      goto mode_error;
      result = -EPROTO;
    } /* if */

    break;
  case TS_SDP_MSG_MCH_PIPE_RECV:  /* source to sink */

    if (TS_SDP_MODE_COMB != conn->recv_mode) {

      goto mode_error;
      result = -EPROTO;
    } /* if */

    break;
  case TS_SDP_MSG_MCH_COMB_RECV:  /* source to sink */

    if (TS_SDP_MODE_PIPE != conn->recv_mode) {

      goto mode_error;
      result = -EPROTO;
    } /* if */
    /*
     * drop all srcAvail message, they will be reissued, with combined
     * mode constraints. No snkAvails outstanding on this half of the
     * connection. How do I know which srcAvail RDMA's completed?
     */


    break;
  default:

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: Invalid mode transition <%d> requested.",
	     TS_SDP_MSG_MCH_GET_MODE(mch));
    result = -EPROTO;
    goto error;
  } /* switch */
  /*
   * assign new mode
   */
  if (0 < (TS_SDP_MSG_MCH_GET_MODE(mch) & 0x8)) {

    conn->send_mode = TS_SDP_MSG_MCH_GET_MODE(mch) & 0x7;
  } /* if */
  else {

    conn->recv_mode = TS_SDP_MSG_MCH_GET_MODE(mch) & 0x7;
  } /* else */

  return 0;

mode_error:
  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	   "EVENT: Invalid mode <%d:%d> for requested transition. <%d>",
	   conn->recv_mode, conn->send_mode, TS_SDP_MSG_MCH_GET_MODE(mch));
error:
  return result;
} /* _tsSdpEventRecvModeChange */

/* ========================================================================= */
/*.._tsSdpEventRecvSrcCancel --  */
static tINT32 _tsSdpEventRecvSrcCancel
(
 tSDP_CONN  conn,
 tSDP_BUFF  buff
)
{
  tSDP_ADVT advt;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: RDMA Source Cancel. active <%d> pending <%d> mode <%d>",
	   tsSdpConnAdvtTableSize(&conn->src_actv),
	   tsSdpConnAdvtTableSize(&conn->src_pend),
	   conn->send_mode);
  /*
   * If there are no outstanding advertisments, then there is nothing
   * to do.
   */
  if (!(0 < conn->src_recv)) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "EVENT: No SrcAvail advertisments to cancel.");
    result = 0;
    goto done;
  } /* if */
  /*
   * Get and terminate the remainder of the oldest advertisment, only
   * if it's already processed data.
   */
  advt = tsSdpConnAdvtTableLook(&conn->src_pend);
  if (NULL != advt &&
      0 < advt->post) {
    /*
     * If active, move to the active queue. Otherwise generate an
     * immediate completion
     */
    if (0 < (TS_SDP_ADVT_F_READ & advt->flag)) {

      advt = tsSdpConnAdvtTableGet(&conn->src_pend);
      TS_EXPECT(MOD_LNX_SDP, (NULL != advt));

      result = tsSdpConnAdvtTablePut(&conn->src_actv, advt);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));
      /*
       * keep track of cancellations
       */
      conn->flags |= TS_SDP_CONN_F_SRC_CANCEL_C;
    } /* if */
    else {

      result = tsSdpSendCtrlRdmaRdComp(conn, advt->post);
      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "EVENT: Error posting RDMA read completion. <%d>", result);
	goto done;
      } /* if */
    } /* else */
  } /* if */
  /*
   * drop the pending advertisment queue.
   */
  while (NULL != (advt = tsSdpConnAdvtTableGet(&conn->src_pend))) {

    conn->flags |= TS_SDP_CONN_F_SRC_CANCEL_C;

    conn->src_recv--;

    result = tsSdpConnAdvtDestroy(advt);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  } /* while */
  /*
   * If there are active reads, mark the connection as being in
   * source cancel. Otherwise
   */
  if (0 < tsSdpConnAdvtTableSize(&conn->src_actv)) {
    /*
     * Set flag. Adjust sequence number ack. (spec dosn't want the
     * seq ack in subsequent messages updated until the cancel has
     * been processed. all would be simpler with an explicit cancel
     * ack, but...)
     */
    conn->flags |= TS_SDP_CONN_F_SRC_CANCEL_R;
    conn->advt_seq--;
  } /* if */
  else {
    /*
     * If a source was dropped, generate an ack.
     */
    if (0 < (TS_SDP_CONN_F_SRC_CANCEL_C & conn->flags)) {

      result = tsSdpSendCtrlSendSm(conn);
      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "EVENT: Error posting SendSm in response to SrcCancel. <%d>",
		 result);
	goto done;
      } /* if */

      conn->flags &= ~TS_SDP_CONN_F_SRC_CANCEL_C;
    } /* if */
  } /* else */

  return 0;
done:
  return result;
} /* _tsSdpEventRecvSrcCancel */

/* ========================================================================= */
/*.._tsSdpEventRecvSinkCancel --  */
static tINT32 _tsSdpEventRecvSinkCancel
(
 tSDP_CONN  conn,
 tSDP_BUFF  buff
)
{
  tSDP_ADVT advt;
  tINT32 counter;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: RDMA Sink Cancel. active <%d> mode <%d>",
	   conn->snk_recv, conn->send_mode);
  /*
   * If there are no outstanding advertisments, they we've completed since
   * the message was sent, and there is nothing to do.
   */
  if (!(0 < conn->snk_recv)) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "EVENT: No SnkAvail advertisments to cancel.");
    result = 0;
    goto done;
  } /* if */
  /*
   * Get the oldest advertisment, and complete it if it's partially
   * consumed. Throw away all unprocessed advertisments, and ack
   * the cancel. Since all the active writes and sends are fenced,
   * it's possible to handle the entire Cancel here.
   */
  advt = tsSdpConnAdvtTableLook(&conn->snk_pend);
  if (NULL != advt &&
      0 < advt->post) {
    /*
     * Generate completion
     */
    result = tsSdpSendCtrlRdmaWrComp(conn, advt->post);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: Error posting RDMA write completion. <%d>", result);
      goto done;
    } /* if */
    /*
     * reduce cancel counter
     */
    counter = -1;
  } /* if */
  else {
    /*
     * cancel count.
     */
    counter = 0;
  } /* else */
  /*
   * drain the advertisments which have yet to be processed.
   */
  while (NULL != (advt = tsSdpConnAdvtTableGet(&conn->snk_pend))) {

    counter++;
    conn->snk_recv--;

    result = tsSdpConnAdvtDestroy(advt);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  } /* while */
  /*
   * A cancel ack is sent only if we cancelled an advertisment without
   * sending a completion
   */
  if (0 < counter) {

    result = tsSdpSendCtrlSnkCancelAck(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: Error posting SnkCacelAck response to SnkCancel. <%d>",
	       result);
      goto done;
    } /* if */
  } /* if */

  return 0;
done:
  return result;
} /* _tsSdpEventRecvSinkCancel */

/* ========================================================================= */
/*.._tsSdpEventRecvSinkCancelAck -- sink cancel confirmantion */
static tINT32 _tsSdpEventRecvSinkCancelAck
(
 tSDP_CONN  conn,
 tSDP_BUFF  buff
)
{
  tSDP_IOCB iocb;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: RDMA Sink Cancel Ack. active <%d> mode <%d> flags <%08x>",
	   conn->snk_sent, conn->recv_mode, conn->flags);

  if (0 == (TS_SDP_CONN_F_SNK_CANCEL & conn->flags)) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: Connection not in sink cancel processing. <%08x>",
	     conn->flags);
    result = -EPROTO;
    goto done;
  } /* if */
  /*
   * drain and complete all active IOCBs
   */
  while (NULL != (iocb = tsSdpConnIocbTableGetHead(&conn->r_snk))) {

    conn->snk_sent--;

    result = tsSdpConnIocbComplete(iocb, 0);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: Error <%d> completing iocb. <%d>", result, iocb->key);
      (void)tsSdpConnIocbDestroy(iocb);
      goto done;
    } /* if */
  } /* while */
  /*
   * cancellation is complete. Cancel flag is cleared in RECV post.
   */
  return 0;
done:
  return result;
} /* _tsSdpEventRecvSinkCancelAck */

/* ========================================================================= */
/*.._tsSdpEventRecvChangeRcvBuf --  buffer size change request */
static tINT32 _tsSdpEventRecvChangeRcvBuf
(
 tSDP_CONN  conn,
 tSDP_BUFF      buff
)
{
  tSDP_MSG_CRBH crbh;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  crbh = (tSDP_MSG_CRBH)buff->data;
  buff->data = buff->data + sizeof(tSDP_MSG_CRBH_STRUCT);

  result = _tsSdpMsgWireToHostCRBH(crbh);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * request to change our recv buffer size, we're pretty much locked into
   * the size we're using, once the connection is set up, so we reject the
   * request.
   */
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: connection receive buffer size change request. <%d:%d>",
	   crbh->size, conn->recv_size);

  result = tsSdpSendCtrlResizeBuffAck(conn, conn->recv_size);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "EVENT: Error acking buffer size change request. <%d>", result);
    goto error;
  } /* if */

  return 0;
error:
  return result;
} /* _tsSdpEventRecvChangeRcvBuf */

/* ========================================================================= */
/*.._tsSdpEventRecvSuspend --  */
static tINT32 _tsSdpEventRecvSuspend
(
 tSDP_CONN  conn,
 tSDP_BUFF  buff
)
{
  tSDP_MSG_SCH  sch;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  sch = (tSDP_MSG_SCH)buff->data;
  buff->data = buff->data + sizeof(tSDP_MSG_SCH_STRUCT);

  result = _tsSdpMsgWireToHostSCH(sch);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  return 0;
} /* _tsSdpEventRecvSuspend */

/* ========================================================================= */
/*.._tsSdpEventRecvSuspendAck --  */
static tINT32 _tsSdpEventRecvSuspendAck
(
 tSDP_CONN   conn,
 tSDP_BUFF       buff
)
{
  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  return 0;
} /* _tsSdpEventRecvSuspendAck */

/* ========================================================================= */
/*.._tsSdpEventRecvSinkAvail --  */
static tINT32 _tsSdpEventRecvSinkAvail
(
 tSDP_CONN   conn,
 tSDP_BUFF       buff
)
{
  tSDP_MSG_SNKAH snkah;
  tSDP_ADVT advt;
  tSDP_IOCB iocb;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  snkah = (tSDP_MSG_SNKAH)buff->data;
  buff->data = buff->data + sizeof(tSDP_MSG_SNKAH_STRUCT);

  result = _tsSdpMsgWireToHostSNKAH(snkah);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

#ifdef _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: SnkAvail received. <%d:%d:%016llx> mode <%d>",
	   snkah->size, snkah->r_key, snkah->addr, conn->send_mode);
#endif
  /*
   * check our send mode, and make sure parameters are within reason.
   */
  if (TS_SDP_MODE_PIPE != conn->send_mode) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: SinkAvail, source mode <%d> is not pipelined.",
	     conn->send_mode);
    result = -EPROTO;
    goto error;
  } /* if */

  if (TS_SDP_MSG_MAX_ADVS == (conn->src_recv + conn->snk_recv)) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: SinkAvail, too many RDMA advertisments. <%d>",
	     (conn->src_recv + conn->snk_recv));
    result = -EPROTO;
    goto error;
  } /* if */
  /*
   * Save the advertisment, if it's not stale.
   */
  if (conn->nond_send == snkah->non_disc) {
    /*
     * check advertisment size.
     */
    if (snkah->size < conn->send_size) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: SinkAvail, advertisment size <%d> too small. <%d>",
	       snkah->size, conn->send_size);
      result = -EPROTO;
      goto error;
    } /* if */
    /*
     * If there are outstanding SrcAvail messages, they are now invalid
     * and the queue needs to be fixed up.
     */
    if (0 < conn->src_sent) {

      while (NULL != (iocb = tsSdpConnIocbTableGetTail(&conn->w_src))) {

	TS_EXPECT(MOD_LNX_SDP, (0 < (TS_SDP_IOCB_F_ACTIVE & iocb->flags)));

	iocb->flags &= ~TS_SDP_IOCB_F_ACTIVE;
	conn->src_sent--;
	/*
	 * Either move the active queue, back to the pending queue, or
	 * if the operations are in cancel processing they need to be
	 * completed.
	 */
	if (0 == (TS_SDP_IOCB_F_CANCEL & iocb->flags)) {

	  result = tsSdpGenericTablePutHead(&conn->send_queue,
					    (tSDP_GENERIC)iocb);
	  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
	} /* if */
	else {

	  result = tsSdpConnIocbComplete(iocb, 0);
	  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
	} /* else */
      } /* while */
      /*
       * If Source Cancel was in process, it should now be cleared.
       */
      if (0 < (TS_SDP_CONN_F_SRC_CANCEL_L & conn->flags)) {

	conn->src_cncl = 0;
	conn->flags   &= ~(TS_SDP_CONN_F_SRC_CANCEL_L);
      } /* if */
    } /* if */
    /*
     * create and queue new advertisment
     */
    advt = tsSdpConnAdvtCreate();
    if (NULL == advt) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: SrcAvail cannot be copied.");
      result = -ENOMEM;
      goto error;
    } /* if */

    advt->post = 0;
    advt->size = snkah->size;
    advt->addr = snkah->addr;
    advt->rkey = snkah->r_key;

    conn->snk_recv++;
    conn->sink_actv++;

    conn->s_cur_adv = 1;
    conn->s_par_adv = 0;

    result = tsSdpConnAdvtTablePut(&conn->snk_pend, advt);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: SnkAvail cannot be saved. <%d>", result);
      goto advt_error;
    } /* if */
  } /* if */
  else {

    conn->nond_send--;
  } /* else */

  conn->s_wq_cur = TS_SDP_DEV_SEND_POST_SLOW;
  conn->s_wq_par = 0;
  /*
   * consume any data in the advertisment for the other direction.
   */
  if (0 < (buff->tail - buff->data)) {

    result = tsSdpSockBuffRecv(conn, buff);
    if (0 < result) {
      /*
       * count number of bytes buffered by the connection, zero byte
       * buffers or errors can be returned, the buffer will be
       * dispossed of by the caller.
       */
      conn->byte_strm += result;
    } /* if */
    else {

      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "EVENT: gateway buffer recv error. <%d>", result);
      } /* if */
    } /* else */
  } /* if */
  else {

    result = 0;
  } /* if */
  /*
   * PostRecv will take care of consuming this advertisment, based on result.
   */
  return result;
advt_error:
  (void)tsSdpConnAdvtDestroy(advt);
error:
  return result;
} /* _tsSdpEventRecvSinkAvail */

/* ========================================================================= */
/*.._tsSdpEventRecvSrcAvail --  */
static tINT32 _tsSdpEventRecvSrcAvail
(
 tSDP_CONN conn,
 tSDP_BUFF buff
)
{
  tSDP_MSG_SRCAH srcah;
  tSDP_ADVT advt;
  tINT32 result;
  tINT32 size;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  srcah = (tSDP_MSG_SRCAH)buff->data;
  buff->data = buff->data + sizeof(tSDP_MSG_SRCAH_STRUCT);

  result = _tsSdpMsgWireToHostSRCAH(srcah);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  size = buff->tail - buff->data;

#ifdef _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: SrcAvail received. <%d:%d:%d:%016llx> mode <%d>",
	   srcah->size, srcah->r_key, size, srcah->addr, conn->recv_mode);
#endif

  if (0 < conn->snk_sent) {
    /*
     * crossed SrcAvail and SnkAvail, the source message is discarded.
     */
#ifdef _TS_SDP_DATA_PATH_DEBUG
    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: Dropping SrcAvail because <%d> SnkAvail in mode <%d>",
	     conn->snk_sent, conn->recv_mode);
#endif
    result = 0;
    goto done;
  } /* if */

  if (0 < (TS_SDP_CONN_F_SRC_CANCEL_R & conn->flags)) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: SrcAvail during SrcAvailCancel processing. <%d>",
	     conn->src_recv);
    result = -EFAULT;
    goto done;
  } /* if */
  /*
   * To emulate RFC 1122 (page 88) a connection should be reset/aborted
   * if data is received and the receive half of the connection has been
   * closed. This notifies the peer that the data was not received.
   */
  if (0 < (TS_SDP_SHUTDOWN_RECV & conn->shutdown)) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "EVENT: Received SrcAvail, receive path closed. <%02x:%04x>",
	     conn->shutdown, conn->state);
    /*
     * abort connection (send reset)
     */
    result = tsSdpAbort(conn);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
    /*
     * drop packet
     */
    result = 0;
    goto done;
  } /* if */
  /*
   * save the advertisment
   */
  advt = tsSdpConnAdvtCreate();
  if (NULL == advt) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: SrcAvail cannot be copied.");
    result = -ENOMEM;
    goto done;
  } /* if */
  /*
   * consume the advertisment, if it's allowed, first check the recv path
   * mode to determine if all is cool for the advertisment.
   */
  switch (conn->recv_mode) {

  case TS_SDP_MODE_BUFF:

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: SrcAvail message received in buffered mode.");

    result = -EPROTO;
    goto advt_error;

    break;
  case TS_SDP_MODE_COMB:

    if (0 < conn->src_recv ||
	!(0 < size) ||
	!(srcah->size > size)) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: SrcAvail mode <%d> mismatch. <%d:%d:%d>",
	       conn->recv_mode, conn->src_recv, size, srcah->size);

      result = -EPROTO;
      goto advt_error;
    } /* if */

    advt->rkey = srcah->r_key;
    advt->post = 0 - ((TS_SDP_SRC_AVAIL_THRESHOLD > size) ? size : 0);
    advt->size = srcah->size - ((TS_SDP_SRC_AVAIL_THRESHOLD > size) ?
				0 : size);
    advt->addr = srcah->addr + ((TS_SDP_SRC_AVAIL_THRESHOLD > size) ?
				0 : size);

    break;
  case TS_SDP_MODE_PIPE:

    if (TS_SDP_MSG_MAX_ADVS == (conn->src_recv + conn->snk_recv) ||
	0 != size) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: SrcAvail mode <%d> mismatch. <%d:%d>",
	       conn->recv_mode, (conn->src_recv + conn->snk_recv), size);

      result = -EPROTO;
      goto advt_error;
    } /* if */

    advt->post = 0;
    advt->size = srcah->size;
    advt->addr = srcah->addr;
    advt->rkey = srcah->r_key;

    break;
  default:

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: SrcAvail message in unknown mode. <%d>", conn->recv_mode);
    result = -EPROTO;
    goto advt_error;
  } /* switch */
  /*
   * save advertisment
   */
  conn->src_recv++;

  result = tsSdpConnAdvtTablePut(&conn->src_pend, advt);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: SrcAvail cannot be saved. <%d>", result);
    goto advt_error;
  } /* if */
  /*
   * process the ULP data in the message
   */
  if (0 < size) {
    /*
     * update non-discard for sink advertisment management
     */
    conn->nond_recv++;

    if (!(TS_SDP_SRC_AVAIL_THRESHOLD > size)) {

      result = tsSdpSockBuffRecv(conn, buff);
      if (0 < result) {
	/*
	 * count number of bytes buffered by the connection, zero byte
	 * buffers or errors can be returned, the buffer will be
	 * dispossed of by the caller.
	 */
	conn->byte_strm += result;
      } /* if */
      else {

	if (0 > result) {

	  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		   "EVENT: gateway buffer recv error. <%d>", result);
	} /* if */
      } /* else */
    } /* if */
    else {

      result = 0;
    } /* if */
  } /* if */
  /*
   * PostRecv will take care of consuming this advertisment.
   */
  return result;

advt_error:
  (void)tsSdpConnAdvtDestroy(advt);
done:
  return result;
} /* _tsSdpEventRecvSrcAvail */

/* ========================================================================= */
/*.._tsSdpEventRecvData -- SDP data message event received */
static tINT32 _tsSdpEventRecvData
(
 tSDP_CONN  conn,
 tSDP_BUFF  buff
)
{
  tINT32 result = 0;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  if (buff->tail > buff->data) {
    /*
     * If we are processing a SrcAvail, there should be no buffered data
     */
    if (0 < conn->src_recv) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: Error, received buffered data, with SrcAvail active.");
      result = -EPROTO;
      goto done;
    } /* if */
    /*
     * check for out-of-band data, and mark the buffer if there is a
     * pending urgent message. If the OOB data is in this buffer, pull
     * it out.
     */
    if (TS_SDP_MSG_HDR_GET_OOB_PEND(buff->bsdh_hdr)) {

      buff->flags |= TS_SDP_BUFF_F_OOB_PEND;
    } /* if */

    if (TS_SDP_MSG_HDR_GET_OOB_PRES(buff->bsdh_hdr)) {

      buff->flags |= TS_SDP_BUFF_F_OOB_PRES;
    } /* if */
    /*
     * update non-discard for sink advertisment management
     */
    conn->nond_recv++;

    result = tsSdpSockBuffRecv(conn, buff);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: gateway buffer recv error. <%d>", result);
    } /* if */
    /*
     * result contains the number of bytes in the buffer which are being
     * kept by the connection. (zero buffered means me can dispose of the
     * buffer.
     */
    conn->byte_strm += result;
  } /* if */

done:
  return result;
} /* _tsSdpEventRecvData */

/* ========================================================================= */
/*.._tsSdpEventRecvUnsupported -- Valid messages we're not expecting */
static tINT32 _tsSdpEventRecvUnsupported
(
 tSDP_CONN   conn,
 tSDP_BUFF       buff
)
{
  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);
  /*
   * Since the gateway only initates RDMA's but is never a target, and
   * for a few other reasons, there are certain valid SDP messages
   * which we never expect to see.
   */
  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	   "EVENT: Unexpected SDP message <%02x> received!",
	   buff->bsdh_hdr->mid);

  return 0;
} /* _tsSdpEventRecvUnsupported */
/*
 * Event Dispatch table. For performance a dispatch table is used to avoid
 * a giant case statment for every single SDP event. This is a bit more
 * confusing, relies on the layout of the Message IDs, and is less
 * flexable. However, it is faster.
 *
 * Sparse table, the full table would be 16x16, where the low 4 bits, of
 * the MID byte, are one dimension, and the high 4 bits are the other
 * dimension. Since all rows, except for the first and last, are empty,
 * only those are represented in the table.
 */
#define TS_SDP_MSG_EVENT_TABLE_SIZE 0x20

static tGW_SDP_EVENT_CB_FUNC recv_event_funcs[TS_SDP_MSG_EVENT_TABLE_SIZE] = {
  NULL,                             /* TS_SDP_MID_HELLO            0x00 */
  NULL,                             /* TS_SDP_MID_HELLO_ACK        0x01 */
  _tsSdpEventRecvDisconnect,      /* TS_SDP_MID_DISCONNECT       0x02 */
  _tsSdpEventRecvAbortConn,       /* TS_SDP_MID_ABORT_CONN       0x03 */
  _tsSdpEventRecvSendSm,          /* TS_SDP_MID_SEND_SM          0x04 */
  _tsSdpEventRecvRdmaWriteComp,   /* TS_SDP_MID_RDMA_WR_COMP     0x05 */
  _tsSdpEventRecvRdmaReadComp,    /* TS_SDP_MID_RDMA_RD_COMP     0x06 */
  _tsSdpEventRecvModeChange,      /* TS_SDP_MID_MODE_CHANGE      0x07 */
  _tsSdpEventRecvSrcCancel,       /* TS_SDP_MID_SRC_CANCEL       0x08 */
  _tsSdpEventRecvSinkCancel,      /* TS_SDP_MID_SNK_CANCEL       0x09 */
  _tsSdpEventRecvSinkCancelAck,   /* TS_SDP_MID_SNK_CANCEL_ACK   0x0A */
  _tsSdpEventRecvChangeRcvBuf,    /* TS_SDP_MID_CH_RECV_BUF      0x0B */
  _tsSdpEventRecvUnsupported,     /* TS_SDP_MID_CH_RECV_BUF_ACK  0x0C */
  _tsSdpEventRecvSuspend,         /* TS_SDP_MID_SUSPEND          0x0D */
  _tsSdpEventRecvSuspendAck,      /* TS_SDP_MID_SUSPEND_ACK      0x0E */
  NULL,                             /* reserved                    0x0F */
  NULL,                             /* reserved                    0xF0 */
  NULL,                             /* reserved                    0xF1 */
  NULL,                             /* reserved                    0xF2 */
  NULL,                             /* reserved                    0xF3 */
  NULL,                             /* reserved                    0xF4 */
  NULL,                             /* reserved                    0xF5 */
  NULL,                             /* reserved                    0xF6 */
  NULL,                             /* reserved                    0xF7 */
  NULL,                             /* reserved                    0xF8 */
  NULL,                             /* reserved                    0xF9 */
  NULL,                             /* reserved                    0xFA */
  NULL,                             /* reserved                    0xFB */
  NULL,                             /* reserved                    0xFC */
  _tsSdpEventRecvSinkAvail,       /* TS_SDP_MID_SNK_AVAIL        0xFD */
  _tsSdpEventRecvSrcAvail,        /* TS_SDP_MID_SRC_AVAIL        0xFE */
  _tsSdpEventRecvData             /* TS_SDP_MID_DATA             0xFF */
}; /* recv_event_funcs */

/* ========================================================================= */
/*..tsSdpEventRecv -- recv event demultiplexing into sdp messages. */
tINT32 tsSdpEventRecv
(
 tSDP_CONN       conn,
 tTS_IB_CQ_ENTRY comp
)
{
  tGW_SDP_EVENT_CB_FUNC dispatch_func;
  tSDP_BUFF buff;
  tUINT32  offset;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(comp, -EINVAL);
  /*
   * error handling
   */
  if (TS_IB_COMPLETION_STATUS_SUCCESS != comp->status) {

    switch (comp->status) {
    case TS_IB_COMPLETION_STATUS_WORK_REQUEST_FLUSHED_ERROR:
      /*
       * clear posted buffers from error'd queue
       */
      result = tsSdpBuffPoolClear(&conn->recv_post);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));

      result = 0;
      break;
    default:
      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: <%04x> unhandled RECV error status <%d>.",
	       conn->hashent, comp->status);
      result = -EIO;
    } /* switch */

    goto done;
  } /* if */
  /*
   * get data
   */
  buff = tsSdpBuffPoolGetHead(&conn->recv_post);
  if (NULL == buff) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: receive event, but no posted receive?!");
    result = -EINVAL;
    goto done;
  } /* if */

  if (comp->work_request_id != buff->ib_wrid) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
             "EVENT: completion, work request ID mismatch. <%u:%u>",
             comp->work_request_id, buff->ib_wrid);

    result = -ERANGE;
    goto drop;
  } /* if */

  /*
   * endian swap
   */
  conn->l_recv_bf--;
  conn->l_advt_bf--;

  buff->bsdh_hdr = (tSDP_MSG_BSDH)buff->data;

  result = _tsSdpMsgWireToHostBSDH(buff->bsdh_hdr);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  if (comp->bytes_transferred != buff->bsdh_hdr->size) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: receive completion, message size mismatch <%d:%d>",
	     comp->bytes_transferred, buff->bsdh_hdr->size);

    result = -EINVAL;
    goto drop;
  } /* if */

  buff->tail       = buff->data + buff->bsdh_hdr->size;
  buff->data       = buff->data + sizeof(tSDP_MSG_BSDH_STRUCT);
  /*
   * Do not update the advertised sequence number, until the SrcAvailCancel
   * message has been processed.
   */
  conn->recv_seq = buff->bsdh_hdr->seq_num;
  conn->advt_seq = (((TS_SDP_CONN_F_SRC_CANCEL_R & conn->flags) > 0) ?
		    conn->advt_seq : conn->recv_seq);
  /*
   * buffers advertised minus the difference in buffer count between
   * the number we've sent and the remote host has received.
   */
  conn->r_recv_bf = (buff->bsdh_hdr->recv_bufs -
		     abs((tINT32)conn->send_seq -
			 (tINT32)buff->bsdh_hdr->seq_ack));
  /*
   * dispatch
   */
#ifdef _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: recv BSDH <%04x:%02x:%02x:%08x:%08x:%08x>",
	   buff->bsdh_hdr->recv_bufs,
	   buff->bsdh_hdr->flags,
	   buff->bsdh_hdr->mid,
	   buff->bsdh_hdr->size,
	   buff->bsdh_hdr->seq_num,
	   buff->bsdh_hdr->seq_ack);
#endif
  /*
   * fast path data messages
   */
  if (TS_SDP_MSG_MID_DATA == buff->bsdh_hdr->mid) {

    result = _tsSdpEventRecvData(conn, buff);
  } /* if */
  else {

    offset = buff->bsdh_hdr->mid & 0x1F;

    if (!(offset < TS_SDP_MSG_EVENT_TABLE_SIZE) ||
	NULL == recv_event_funcs[offset]) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: receive completion, unknown message ID <%d>",
	       buff->bsdh_hdr->mid);
      result = -EINVAL;
      goto drop;
    } /* if */

    TS_SDP_CONN_STAT_RECV_MID_INC(conn, offset);

    dispatch_func = recv_event_funcs[offset];
    result = dispatch_func(conn, buff);
  } /* else */
  /*
   * process result.
   */
  if (0 == result) {

    result = tsSdpBuffMainPut(buff);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
    /*
     * If this buffer was consumed, then make sure sufficient recv
     * buffers are posted. Also we might be able to move data with a new
     * RDMA SrcAvail advertisment.
     */
    result = tsSdpRecvPost(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "EVENT: Receive buffer post failed freed message. <%d>",
	       result);
      goto done;
    } /* if */
  } /* if */
  else {

    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: receive completion, dispatch error. <%d>", result);

      goto drop;
    } /* if */
  } /* else */
  /*
   * It's possible that a new recv buffer advertisment opened up the recv
   * window and we can flush buffered send data
   */
  result = tsSdpSendFlush(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: Flush failure during receive completion. <%d>", result);
    goto done;
  } /* if */

  return 0;
drop:
  (void)tsSdpBuffMainPut(buff);
done:
  return result;
} /* tsSdpEventRecv */
