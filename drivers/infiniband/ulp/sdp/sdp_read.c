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

  $Id: sdp_read.c,v 1.18 2004/02/24 23:48:51 roland Exp $
*/

#include "sdp_main.h"

/* --------------------------------------------------------------------- */
/*                                                                       */
/* RDMA read processing functions                                        */
/*                                                                       */
/* --------------------------------------------------------------------- */

/* ========================================================================= */
/*.._tsSdpEventReadAdvt -- RDMA read event handler for source advertisments. */
static tINT32 _tsSdpEventReadAdvt
(
 tSDP_CONN       conn,
 tTS_IB_CQ_ENTRY comp
)
{
  tSDP_ADVT advt;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(comp, -EINVAL);
  /*
   * if this was the last RDMA read for an advertisment, post a notice.
   * Might want to post multiple RDMA read completion messages per
   * advertisment, to open up a sending window? Have to test to see
   * what MS does... (Either choice is correct)
   */
  advt = tsSdpConnAdvtTableLook(&conn->src_actv);
  if (NULL != advt &&
      advt->wrid == comp->work_request_id) {

    advt = tsSdpConnAdvtTableGet(&conn->src_actv);
    TS_EXPECT(MOD_LNX_SDP, (NULL != advt));

    conn->src_recv--;

    result = tsSdpSendCtrlRdmaRdComp(conn, advt->post);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));

    result = tsSdpConnAdvtDestroy(advt);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
    /*
     * If a SrcAvailCancel was received, and all RDMA reads have been
     * flushed, perform tail processing
     */
    if (0 < (TS_SDP_CONN_F_SRC_CANCEL_R & conn->flags) &&
	0 == conn->src_recv) {

      conn->flags &= ~TS_SDP_CONN_F_SRC_CANCEL_R;
      conn->advt_seq = conn->recv_seq;
      /*
       * If any data was canceled, post a SendSm, also
       */
      if (0 < (TS_SDP_CONN_F_SRC_CANCEL_C & conn->flags)) {

	result = tsSdpSendCtrlSendSm(conn);
	if (0 > result) {

	  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		   "EVENT: Error posting SendSm in RDMA read. <%d>", result);
	  goto error;
	} /* if */

	conn->flags &= ~TS_SDP_CONN_F_SRC_CANCEL_C;
      } /* if */
    } /* if */
  } /* if */
  else {

    advt = tsSdpConnAdvtTableLook(&conn->src_pend);
    if (NULL != advt &&
	advt->wrid == comp->work_request_id) {

      advt->flag &= ~TS_SDP_ADVT_F_READ;
    } /* if */
  } /* else */

  return 0;
error:
  return result;
} /* _tsSdpEventReadAdvt */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* RDMA read QP Event Handler                                            */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpEventRead -- RDMA read event handler. */
tINT32 tsSdpEventRead
(
 tSDP_CONN       conn,
 tTS_IB_CQ_ENTRY comp
)
{
  tSDP_IOCB iocb;
  tSDP_BUFF buff;
  tINT32 result;
  tINT32 type;

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
      result = tsSdpGenericTableClear(&conn->r_src);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));

      result = 0;
      break;
    default:
      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: unhandled READ error status <%d>.", comp->status);
      result = -EIO;
    } /* switch */

    goto done;
  } /* if */

#ifdef _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: RDMA read <%08x> of <%u> bytes.",
	   comp->work_request_id, comp->bytes_transferred);
#endif
  /*
   * update queue depth
   */
  conn->s_wq_size--;
  /*
   * Four basic scenarios:
   *
   * 1) BUFF at the head of the active read table is completed by this
   *    read event completion
   * 2) IOCB at the head of the active read table is completed by this
   *    read event completion
   * 3) IOCB at the head of the active read table is not associated
   *    with this event, meaning a later event in flight will complete
   *    it, no IOCB is completed by this event.
   * 4) No IOCBs are in the active table, the head of the read pending
   *    table, matches the work request ID of the event and the recv
   *    low water mark has been satisfied.
   */
  /*
   * check type at head of queue
   */
  type = tsSdpGenericTableTypeHead(&conn->r_src);
  switch (type) {
  case TS_SDP_GENERIC_TYPE_BUFF:

    buff = (tSDP_BUFF)tsSdpGenericTableGetHead(&conn->r_src);
    TS_EXPECT(MOD_LNX_SDP, (NULL != buff));

    if (comp->work_request_id != buff->ib_wrid) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: work request ID mismatch <%08x:%08x> on BUFF READ.",
	       comp->work_request_id, buff->ib_wrid);

      (void)tsSdpBuffMainPut(buff);
      result = -EPROTO;
      goto done;
    } /* if */
    /*
     * post data to the stream interface
     */
    result = tsSdpSockBuffRecv(conn, buff);
    if (0 < result) {
      /*
       * count number of bytes buffered by the connection, zero byte
       * buffers can be returned.
       */
      conn->byte_strm += result;
    } /* if */
    else {

      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "EVENT: gateway buffer recv error on RDMA read. <%d>",
		 result);
      } /* if */

      result = tsSdpBuffMainPut(buff);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));
    } /* if */

    break;
  case TS_SDP_GENERIC_TYPE_IOCB:

    iocb = (tSDP_IOCB)tsSdpGenericTableLookHead(&conn->r_src);
    if (NULL != iocb &&
	iocb->wrid == comp->work_request_id) {

      iocb = (tSDP_IOCB)tsSdpGenericTableGetHead(&conn->r_src);
      TS_EXPECT(MOD_LNX_SDP, (NULL != iocb));

      iocb->flags &= ~(TS_SDP_IOCB_F_ACTIVE|TS_SDP_IOCB_F_RDMA_R);

      TS_SDP_CONN_STAT_READ_INC(conn, iocb->post);
      TS_SDP_CONN_STAT_RQ_DEC(conn, iocb->size);

      result = tsSdpConnIocbComplete(iocb, 0);
      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "EVENT: Error <%d> completing iocb. <%d>", result, iocb->key);
	(void)tsSdpConnIocbDestroy(iocb);
	goto done;
      } /* if */
    } /* if */

    break;
  case TS_SDP_GENERIC_TYPE_NONE:

    iocb = tsSdpConnIocbTableLook(&conn->r_pend);
    if (NULL == iocb) {

      result = -EPROTO;
      goto done;
    } /* if */

    if (iocb->wrid == comp->work_request_id) {

      iocb->flags &= ~(TS_SDP_IOCB_F_ACTIVE|TS_SDP_IOCB_F_RDMA_R);

      if (!(TS_SDP_OS_SK_RCVLOWAT(conn->sk) > iocb->post)) {

	iocb = tsSdpConnIocbTableGetHead(&conn->r_pend);
	TS_EXPECT(MOD_LNX_SDP, (NULL != iocb));

	TS_SDP_CONN_STAT_READ_INC(conn, iocb->post);
	TS_SDP_CONN_STAT_RQ_DEC(conn, iocb->size);

	result = tsSdpConnIocbComplete(iocb, 0);
	if (0 > result) {

	  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		   "EVENT: Error <%d> completing iocb. <%d>",
		   result, iocb->key);
	  (void)tsSdpConnIocbDestroy(iocb);
	  goto done;
	} /* if */
      } /* if */
    } /* if */

    break;
  default:

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: Unknown type <%d> at head of READ SOURCE queue. <%d>",
	     type, tsSdpGenericTableSize(&conn->r_src));
    result = -EPROTO;
    goto done;
  } /* switch */
  /*
   * The advertisment which generated this READ needs to be checked.
   */
  result = _tsSdpEventReadAdvt(conn, comp);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: Error <%d> handling READ advertisment. <%08x>",
	     result, comp->work_request_id);
    goto done;
  } /* if */
  /*
   * It's possible that the "send" queue was opened up by the completion
   * of some RDMAs
   */
  result = tsSdpSendFlush(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: Flush failure during receive completion. <%d>", result);
    goto done;
  } /* if */
  /*
   * The completion of the RDMA read may allow us to post additional RDMA
   * reads.
   */
  result = tsSdpRecvPost(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: Receive buffer post failed on RDMA read. <%d>", result);
    goto done;
  } /* if */

done:
  return result;
} /* tsSdpEventRead */
