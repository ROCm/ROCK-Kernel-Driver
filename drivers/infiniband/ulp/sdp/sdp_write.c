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

  $Id: sdp_write.c,v 1.14 2004/02/24 23:48:53 roland Exp $
*/

#include "sdp_main.h"

/* --------------------------------------------------------------------- */
/*                                                                       */
/* RDMA read QP Event Handler                                            */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpEventWrite -- RDMA write event handler. */
tINT32 tsSdpEventWrite
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
      result = tsSdpGenericTableClear(&conn->w_snk);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));

      result = 0;
      break;
    default:
      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: unhandled WRITE error status <%d>.", comp->status);
      result = -EIO;
    } /* switch */

    goto error;
  } /* if */

#ifdef _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "EVENT: RDMA write <%08x> of <%u> bytes.",
	   comp->work_request_id, comp->bytes_transferred);
#endif
  /*
   * Four basic scenarios:
   *
   * 1) IOCB at the head of the active sink table is completed by this
   *    write event completion
   * 2) BUFF at the head of the active sink table is completed by this
   *    write event completion
   * 2) IOCB at the head of the active sink table is not associated
   *    with this event, meaning a later event in flight will be the
   *    write to complete it, no IOCB is completed by this event.
   * 3) No IOCBs are in the active table, the head of the send pending
   *    table, matches the work request ID of the event.
   */
  type = tsSdpGenericTableTypeHead(&conn->w_snk);
  switch (type) {
  case TS_SDP_GENERIC_TYPE_BUFF:

    buff = (tSDP_BUFF)tsSdpGenericTableGetHead(&conn->w_snk);
    TS_EXPECT(MOD_LNX_SDP, (NULL != buff));

    conn->send_qud -= buff->data_size;

    result = tsSdpBuffMainPut(buff);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));

    break;
  case TS_SDP_GENERIC_TYPE_IOCB:

    iocb = (tSDP_IOCB)tsSdpGenericTableLookHead(&conn->w_snk);
    if (NULL != iocb &&
	iocb->wrid == comp->work_request_id) {

      iocb = (tSDP_IOCB)tsSdpGenericTableGetHead(&conn->w_snk);
      TS_EXPECT(MOD_LNX_SDP, (NULL != iocb));

      iocb->flags &= ~(TS_SDP_IOCB_F_ACTIVE|TS_SDP_IOCB_F_RDMA_W);

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

    break;
  case TS_SDP_GENERIC_TYPE_NONE:

    iocb = (tSDP_IOCB)tsSdpGenericTableLookTypeHead(&conn->send_queue,
						    TS_SDP_GENERIC_TYPE_IOCB);
    if (NULL == iocb) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: Missing IOCB on write completion. <%d:%d:%d>",
	       comp->work_request_id, tsSdpGenericTableSize(&conn->w_snk),
	       tsSdpGenericTableSize(&conn->send_queue));

      result = -EPROTO;
      goto error;
    } /* if */

    if (iocb->wrid == comp->work_request_id) {
      /*
       * clear flags on a previously active partially satisfied IOCB
       */
      iocb->flags &= ~(TS_SDP_IOCB_F_ACTIVE|TS_SDP_IOCB_F_RDMA_W);
    } /* if */

    break;
  default:

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: Unknown type <%d> at head of WRITE SINK queue. <%d>",
	     type, tsSdpGenericTableSize(&conn->w_snk));
    result = -EPROTO;
    goto error;
  } /* switch */
  /*
   * update queue depth
   */
  conn->s_wq_size--;
  conn->sink_actv--;
  /*
   * It's possible that the "send" queue was opened up by the completion
   * of some more sends.
   */
  result = tsSdpSendFlush(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: Flush failure during receive completion. <%d>", result);
    goto error;
  } /* if */
  /*
   * The completion of the RDMA read may allow us to post additional RDMA
   * reads.
   */
  result = tsSdpRecvPost(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: Receive buffer post failed on RDMA read. <%d>", result);
    goto error;
  } /* if */

  return 0;
error:
  return result;
} /* tsSdpEventWrite */
