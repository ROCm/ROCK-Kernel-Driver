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

  $Id: sdp_sent.c,v 1.27 2004/02/24 23:48:52 roland Exp $
*/

#include "sdp_main.h"

/* --------------------------------------------------------------------- */
/*                                                                       */
/* Specific MID handler functions. (SEND)                                */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsSdpEventSendDisconnect --  */
static tINT32 _tsSdpEventSendDisconnect
(
 tSDP_CONN  conn,
 tSDP_BUFF      buff
)
{
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "STRM: <%d> <%08x:%04x><%08x:%04x> disconnect sent in state <%04x>",
	   conn->hashent, conn->src_addr, conn->src_port, conn->dst_addr,
	   conn->dst_port, conn->state);

  switch (conn->state) {
  case TS_SDP_CONN_ST_TIME_WAIT_2:
    /*
     * Nothing to do, CM disconnects have been exchanged.
     */
    break;
  case TS_SDP_CONN_ST_DIS_SEND_1:
    /*
     * Active disconnect message send completed
     */
    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_DIS_SENT_1);

    break;
  case TS_SDP_CONN_ST_DIS_SEND_2:
    /*
     * passive disconnect message send
     */
    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_DIS_SENT_2);
    /*
     * Begin IB/CM disconnect
     */
    result = tsSdpCmDisconnect(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "STRM: Error posting CM disconnect. <%d>", result);
      goto error;
    } /* if */

    break;
  case TS_SDP_CONN_ST_DIS_RECV_R:
    /*
     * simultaneous disconnect. Received a disconnect, after we
     * initiated one. This needs to be handled as the active stream
     * interface close that it is.
     */
    TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_TIME_WAIT_1);
    /*
     * acknowledge disconnect to framework
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
	     "STRM: Disconnect send completion in an unexpected state. <%04x>",
	     conn->state);
    result = -EFAULT;
    goto error;

    break;
  } /* switch */

  return 0;
error:
  return result;
} /* _tsSdpEventSendDisconnect */

/* ========================================================================= */
/*.._tsSdpEventSendAbortConn --  */
static tINT32 _tsSdpEventSendAbortConn
(
 tSDP_CONN  conn,
 tSDP_BUFF      buff
)
{
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);
  /*
   * The gateway interface should be in error state, initiate CM disconnect.
   */
  TS_EXPECT(MOD_LNX_SDP, (TS_SDP_CONN_ST_ERROR_STRM == conn->state));
  TS_SDP_CONN_ST_SET(conn, TS_SDP_CONN_ST_ERROR_STRM);

  result = tsSdpCmDisconnect(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Error posting CM disconnect. <%d>", result);
  } /* if */

  return result;
} /* _tsSdpEventSendAbortConn */

/* ========================================================================= */
/*.._tsSdpEventSendSendSm --  */
static tINT32 _tsSdpEventSendSendSm
(
 tSDP_CONN  conn,
 tSDP_BUFF      buff
)
{
  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  return 0;
} /* _tsSdpEventSendSendSm */

/* ========================================================================= */
/*.._tsSdpEventSendRdmaWriteComp --  */
static tINT32 _tsSdpEventSendRdmaWriteComp
(
 tSDP_CONN  conn,
 tSDP_BUFF      buff
)
{
  tSDP_MSG_RWCH rwch;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  rwch = (tSDP_MSG_RWCH)buff->data;
  buff->data = buff->data + sizeof(tSDP_MSG_RWCH_STRUCT);

  result = _tsSdpMsgWireToHostRWCH(rwch);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  return 0;
} /* _tsSdpEventSendRdmaWriteComp */

/* ========================================================================= */
/*.._tsSdpEventSendRdmaReadComp --  */
static tINT32 _tsSdpEventSendRdmaReadComp
(
 tSDP_CONN  conn,
 tSDP_BUFF      buff
)
{
  tSDP_MSG_RRCH rrch;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  rrch = (tSDP_MSG_RRCH)buff->data;
  buff->data = buff->data + sizeof(tSDP_MSG_RRCH_STRUCT);

  result = _tsSdpMsgWireToHostRRCH(rrch);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  return 0;
} /* _tsSdpEventSendRdmaReadComp */

/* ========================================================================= */
/*.._tsSdpEventSendModeChange --  */
static tINT32 _tsSdpEventSendModeChange
(
 tSDP_CONN  conn,
 tSDP_BUFF      buff
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

  return 0;
} /* _tsSdpEventSendModeChange */

/* ========================================================================= */
/*.._tsSdpEventSendSrcCancel --  */
static tINT32 _tsSdpEventSendSrcCancel
(
 tSDP_CONN  conn,
 tSDP_BUFF  buff
)
{
  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  return 0;
} /* _tsSdpEventSendSrcCancel */

/* ========================================================================= */
/*.._tsSdpEventSendSinkCancel --  */
static tINT32 _tsSdpEventSendSinkCancel
(
 tSDP_CONN  conn,
 tSDP_BUFF  buff
)
{
  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  return 0;
} /* _tsSdpEventSendSinkCancel */

/* ========================================================================= */
/*.._tsSdpEventSendSinkCancelAck --  */
static tINT32 _tsSdpEventSendSinkCancelAck
(
 tSDP_CONN  conn,
 tSDP_BUFF  buff
)
{
  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  return 0;
} /* _tsSdpEventSendSinkCancelAck */

/* ========================================================================= */
/*.._tsSdpEventSendChangeRcvBufAck --  */
static tINT32 _tsSdpEventSendChangeRcvBufAck
(
 tSDP_CONN   conn,
 tSDP_BUFF       buff
)
{
  tSDP_MSG_CRBAH crbah;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  crbah = (tSDP_MSG_CRBAH)buff->data;
  buff->data = buff->data + sizeof(tSDP_MSG_CRBAH_STRUCT);

  result = _tsSdpMsgWireToHostCRBAH(crbah);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  return 0;
} /* _tsSdpEventSendChangeRcvBufAck */

/* ========================================================================= */
/*.._tsSdpEventSendSuspend --  */
static tINT32 _tsSdpEventSendSuspend
(
 tSDP_CONN  conn,
 tSDP_BUFF      buff
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
} /* _tsSdpEventSendSuspend */

/* ========================================================================= */
/*.._tsSdpEventSendSuspendAck --  */
static tINT32 _tsSdpEventSendSuspendAck
(
 tSDP_CONN   conn,
 tSDP_BUFF       buff
)
{
  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  return 0;
} /* _tsSdpEventSendSuspendAck */

/* ========================================================================= */
/*.._tsSdpEventSendSnkAvail --  */
static tINT32 _tsSdpEventSendSnkAvail
(
 tSDP_CONN  conn,
 tSDP_BUFF  buff
)
{
  tSDP_MSG_SNKAH snkah;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  snkah = (tSDP_MSG_SNKAH)buff->data;
  buff->data = buff->data + sizeof(tSDP_MSG_SNKAH_STRUCT);

  result = _tsSdpMsgWireToHostSNKAH(snkah);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  return 0;
} /* _tsSdpEventSendSnkAvail */

/* ========================================================================= */
/*.._tsSdpEventSendSrcAvail --  */
static tINT32 _tsSdpEventSendSrcAvail
(
 tSDP_CONN  conn,
 tSDP_BUFF  buff
)
{
  tSDP_MSG_SRCAH srcah;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  srcah = (tSDP_MSG_SRCAH)buff->data;
  buff->data = buff->data + sizeof(tSDP_MSG_SRCAH_STRUCT);

  result = _tsSdpMsgWireToHostSRCAH(srcah);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  return 0;
} /* _tsSdpEventSendSrcAvail */

/* ========================================================================= */
/*.._tsSdpEventSendData -- SDP data message event received */
static tINT32 _tsSdpEventSendData
(
 tSDP_CONN  conn,
 tSDP_BUFF  buff
)
{
  tINT32 result = 0;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(conn->sk,   -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  conn->send_qud -= buff->data_size;

return result;
} /* _tsSdpEventSendData */

/* ========================================================================= */
/*.._tsSdpEventSendUnsupported -- Valid messages we're not sending */
static tINT32 _tsSdpEventSendUnsupported
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
   * which we never send.
   */
  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	   "EVENT: Unexpected SDP message <%02x> sent!",
	   buff->bsdh_hdr->mid);

  return 0;
} /* _tsSdpEventSendUnsupported */
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
static tGW_SDP_EVENT_CB_FUNC send_event_funcs[TS_SDP_MSG_EVENT_TABLE_SIZE] = {
  NULL,                           /* TS_SDP_MID_HELLO            0x00 */
  NULL,                           /* TS_SDP_MID_HELLO_ACK        0x01 */
  _tsSdpEventSendDisconnect,      /* TS_SDP_MID_DISCONNECT       0x02 */
  _tsSdpEventSendAbortConn,       /* TS_SDP_MID_ABORT_CONN       0x03 */
  _tsSdpEventSendSendSm,          /* TS_SDP_MID_SEND_SM          0x04 */
  _tsSdpEventSendRdmaWriteComp,   /* TS_SDP_MID_RDMA_WR_COMP     0x05 */
  _tsSdpEventSendRdmaReadComp,    /* TS_SDP_MID_RDMA_RD_COMP     0x06 */
  _tsSdpEventSendModeChange,      /* TS_SDP_MID_MODE_CHANGE      0x07 */
  _tsSdpEventSendSrcCancel,       /* TS_SDP_MID_SRC_CANCEL       0x08 */
  _tsSdpEventSendSinkCancel,      /* TS_SDP_MID_SNK_CANCEL       0x09 */
  _tsSdpEventSendSinkCancelAck,   /* TS_SDP_MID_SNK_CANCEL_ACK   0x0A */
  _tsSdpEventSendUnsupported,     /* TS_SDP_MID_CH_RECV_BUF      0x0B */
  _tsSdpEventSendChangeRcvBufAck, /* TS_SDP_MID_CH_RECV_BUF_ACK  0x0C */
  _tsSdpEventSendSuspend,         /* TS_SDP_MID_SUSPEND          0x0D */
  _tsSdpEventSendSuspendAck,      /* TS_SDP_MID_SUSPEND_ACK      0x0E */
  NULL,                           /* reserved                    0x0F */
  NULL,                           /* reserved                    0xF0 */
  NULL,                           /* reserved                    0xF1 */
  NULL,                           /* reserved                    0xF2 */
  NULL,                           /* reserved                    0xF3 */
  NULL,                           /* reserved                    0xF4 */
  NULL,                           /* reserved                    0xF5 */
  NULL,                           /* reserved                    0xF6 */
  NULL,                           /* reserved                    0xF7 */
  NULL,                           /* reserved                    0xF8 */
  NULL,                           /* reserved                    0xF9 */
  NULL,                           /* reserved                    0xFA */
  NULL,                           /* reserved                    0xFB */
  NULL,                           /* reserved                    0xFC */
  _tsSdpEventSendSnkAvail,        /* TS_SDP_MID_SNK_AVAIL        0xFD */
  _tsSdpEventSendSrcAvail,        /* TS_SDP_MID_SRC_AVAIL        0xFE */
  _tsSdpEventSendData             /* TS_SDP_MID_DATA             0xFF */
}; /* send_event_funcs */

/* ========================================================================= */
/*..tsSdpEventSend -- send event handler. */
tINT32 tsSdpEventSend
(
 tSDP_CONN       conn,
 tTS_IB_CQ_ENTRY comp
)
{
  tGW_SDP_EVENT_CB_FUNC dispatch_func;
  tINT32 free_count = 0;
  tUINT32 current_wrid = 0;
  tINT32 offset;
  tINT32 result;
  tSDP_BUFF buff;
  tSDP_BUFF head = NULL;

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
      result = tsSdpBuffPoolClear(&conn->send_post);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));

      result = 0;
      break;
    default:
      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: <%04x> unhandled SEND error status <%d>.",
	       conn->hashent, comp->status);
      result = -EIO;
    } /* switch */

    goto done;
  } /* if */
  /*
   * get buffer.
   */
  while (NULL != (buff = tsSdpBuffPoolGetHead(&conn->send_post))) {
    /*
     * sanity checks
     */
    if (NULL == buff->bsdh_hdr) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_WARN,
	       "EVENT: send header is missing?!");
      result = -ENODATA;
      goto drop;
    } /* if */

    if (TS_SDP_WRID_LT(comp->work_request_id, buff->ib_wrid)) {
      /*
       * error
       */
      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: send complete, wrid mismatch. <%u:%u:%d> mid <%02x>",
	       comp->work_request_id, buff->ib_wrid,
	       conn->send_usig, buff->bsdh_hdr->mid);
      result = -EINVAL;
      goto drop;
    } /* if */
    /*
     * execute the send dispatch function, for specific actions.
     */
#ifdef _TS_SDP_DATA_PATH_DEBUG
    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "EVENT: sent BSDH <%04x:%02x:%02x:%08x:%08x:%08x> <%08x> <%u>",
	     buff->bsdh_hdr->recv_bufs,
	     buff->bsdh_hdr->flags,
	     buff->bsdh_hdr->mid,
	     buff->bsdh_hdr->size,
	     buff->bsdh_hdr->seq_num,
	     buff->bsdh_hdr->seq_ack,
	     buff->flags,
	     buff->ib_wrid);
#endif
    /*
     * data fast path we collapse the next level dispatch function.
     * For all other buffers we go the slow path.
     */
    if (TS_SDP_MSG_MID_DATA == buff->bsdh_hdr->mid) {

      conn->send_qud -= buff->data_size;
    } /* if */
    else {

      offset = buff->bsdh_hdr->mid & 0x1F;

      if (!(offset < TS_SDP_MSG_EVENT_TABLE_SIZE) ||
	  NULL == send_event_funcs[offset]) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "EVENT: send completion, unknown message ID <%d>",
		 buff->bsdh_hdr->mid);
	result = -EINVAL;
	goto drop;
      } /* if */
      else {

	TS_SDP_CONN_STAT_SEND_MID_INC(conn, offset);

	dispatch_func = send_event_funcs[offset];
	result = dispatch_func(conn, buff);
	if (0 > result) {

	  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		   "EVENT: receive completion, dispatch error. <%d>", result);
	  goto drop;
	} /* if */
      } /* else */
    } /* else */

    current_wrid = buff->ib_wrid;
    /*
     * send queue size reduced by one.
     */
    conn->s_wq_size--;

    if (0 < TS_SDP_BUFF_F_GET_UNSIG(buff)) {

      conn->send_usig--;
    } /* else/if */
    /*
     * create a link of buffers which will be returned to
     * the free pool in one group.
     */
    result = tsSdpBuffMainChainLink(head, buff);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));

    head = buff;
    free_count++;

    if (comp->work_request_id == current_wrid) {

      break;
    } /* if */
  } /* while */

  result = tsSdpBuffMainChainPut(head, free_count);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  if (!(0 < free_count) || 0 > conn->send_usig) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: send processing error. mismatch. <%u:%u:%d:%d>",
	     comp->work_request_id, current_wrid, free_count, conn->send_usig);
    result = -EINVAL;
    goto done;
  } /* if */
  /*
   * Flush queued send data into the post queue if there is room.
   */
  result = tsSdpSendFlush(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: Failure during send queue flush. <%d>", result);
    goto done;
  } /* if */

  return 0;
drop:
  (void)tsSdpBuffMainPut(buff);
  (void)tsSdpBuffMainChainPut(head, free_count);
done:
  return result;
} /* tsSdpEventSend */
