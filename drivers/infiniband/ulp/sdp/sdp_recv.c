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

  $Id: sdp_recv.c,v 1.48 2004/02/24 23:48:51 roland Exp $
*/

#include "sdp_main.h"

static char _peek_queue_name[] = TS_SDP_INET_PEEK_DATA_NAME;
/* --------------------------------------------------------------------- */
/*                                                                       */
/* Receive posting function(s)                                           */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsSdpRecvPostRecvBuff -- post a single buffers for data recv */
static tINT32 _tsSdpRecvPostRecvBuff
(
 tSDP_CONN  conn
)
{
  tTS_IB_RECEIVE_PARAM_STRUCT receive_param = {0};
  tINT32 result;
  tSDP_BUFF buff;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * get a buffer
   */
  buff = tsSdpBuffMainGet();
  if (NULL == buff) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: failed to allocate buffer for receive queue.");
    result = -ENOMEM;
    goto error;
  } /* if */
  /*
   * The data pointer is backed up based on what the stream interface
   * peer advertised to us plus the required header. This way the
   * data we end up passing to the interface will always be within
   * the correct range.
   */
  buff->tail = buff->end;
  buff->data = buff->tail - conn->recv_size;
  buff->lkey = conn->l_key;
  buff->ib_wrid = TS_SDP_WRID_RECV_MASK & conn->recv_wrid++;

  conn->l_recv_bf++;
  /*
   * save the buffer for the event handler. Make sure it's before actually
   * posting the thing. Completion event can happen before post function
   * returns.
   */
  result = tsSdpBuffPoolPutTail(&conn->recv_post, buff);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Error <%d> queuing receive buffer.", result);
    goto drop;
  } /* if */
#ifdef _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "POST: RECV BUFF wrid <%08x> of <%u> bytes.",
	   buff->ib_wrid, (buff->tail - buff->data));
#endif
  /*
   * post recv
   */
  receive_param.work_request_id     = buff->ib_wrid;
  receive_param.scatter_list        = TS_SDP_BUFF_GAT_SCAT(buff);
  receive_param.num_scatter_entries = 1;
  receive_param.signaled            = 1;

  result =  tsIbReceive(conn->qp, &receive_param, 1);
  if (0 != result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: receive failure. <%d>", result);

    (void)tsSdpBuffPoolGetTail(&conn->recv_post);
    goto drop;
  } /* if */

  return 0;
drop:
  (void)tsSdpBuffMainPut(buff);
  conn->l_recv_bf--;
error:
  return result;
} /* _tsSdpRecvPostRecvBuff */

/* ========================================================================= */
/*.._tsSdpRecvPostRdmaBuff -- post a single buffers for rdma read on a conn */
static tINT32 _tsSdpRecvPostRdmaBuff
(
 tSDP_CONN  conn
)
{
  tTS_IB_SEND_PARAM_STRUCT send_param = {0};
  tSDP_ADVT advt;
  tINT32 result;
  tSDP_BUFF buff;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * check queue depth
   */
  if (!(TS_SDP_DEV_SEND_POST_MAX > conn->s_wq_size)) {

    result = ENODEV;
    goto done;
  } /* if */
  /*
   * get a reference to the first SrcAvail advertisment.
   */
  advt = tsSdpConnAdvtTableLook(&conn->src_pend);
  if (NULL == advt) {

    result = ENODEV;
    goto done;
  } /* if */
  /*
   * get a buffer
   */
  buff = tsSdpBuffMainGet();
  if (NULL == buff) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: failed to allocate buffer for receive queue.");

    result = -ENOMEM;
    goto error;
  } /* if */
  /*
   * The data pointer is backed up based on what the stream interface
   * peer advertised to us plus the required header. This way the
   * data we end up passing to the interface will always be within
   * the correct range.
   */
  buff->tail = buff->end;
  buff->data = buff->tail - min((tINT32)conn->recv_size, advt->size);
  buff->lkey = conn->l_key;

  buff->ib_wrid = TS_SDP_WRID_READ_FLAG | conn->recv_wrid++;

  send_param.op             = TS_IB_OP_RDMA_READ;
  send_param.remote_address = advt->addr;
  send_param.rkey           = advt->rkey;
  send_param.signaled       = 1;

  advt->wrid  = buff->ib_wrid;
  advt->size -= (buff->tail - buff->data);
  advt->addr += (buff->tail - buff->data);
  advt->post += (buff->tail - buff->data);
  advt->flag |= TS_SDP_ADVT_F_READ;
  /*
   * If there is no more advertised space move the advertisment to the
   * active list, and match the WRID.
   */
  if (!(0 < advt->size)) {

    advt = tsSdpConnAdvtTableGet(&conn->src_pend);
    if (NULL == advt) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "POST: src available advertisment disappeared.");
      result = -ENODEV;
      goto drop;
    } /* if */

    result = tsSdpConnAdvtTablePut(&conn->src_actv, advt);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "POST: Error <%d> queuing active advertisment.", result);

      (void)tsSdpConnAdvtDestroy(advt);
      goto drop;
    } /*  if */
  } /* if */
  /*
   * save the buffer for the event handler. Make sure it's before actually
   * posting the thing. Completion event can happen before post function
   * returns.
   */
  result = tsSdpGenericTablePutTail(&conn->r_src, (tSDP_GENERIC)buff);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Error <%d> queuing rdma read.", result);
    goto drop;
  } /* if */
#ifdef _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "POST: READ BUFF wrid <%08x> of <%u> bytes.",
	   buff->ib_wrid, (buff->tail - buff->data));
#endif
  /*
   * update send queue depth
   */
  conn->s_wq_size++;
  /*
   * post rdma
   */
  send_param.work_request_id    = buff->ib_wrid;
  send_param.gather_list        = TS_SDP_BUFF_GAT_SCAT(buff);
  send_param.num_gather_entries = 1;

  result = tsIbSend(conn->qp, &send_param, 1);
  if (0 != result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: send (rdma read) failure. <%d>", result);

    (void)tsSdpGenericTableGetTail(&conn->r_src);
    conn->s_wq_size--;

    goto drop;
  } /* if */

  return 0;
drop:
  (void)tsSdpBuffMainPut(buff);
error:
done:
  return result;
} /* _tsSdpRecvPostRdmaBuff */

/* ========================================================================= */
/*.._tsSdpRecvPostRdmaIocbSrc -- post a iocb for rdma read on a conn */
static tINT32 _tsSdpRecvPostRdmaIocbSrc
(
 tSDP_CONN conn
)
{
  tTS_IB_SEND_PARAM_STRUCT send_param ={0};
  tTS_IB_GATHER_SCATTER_STRUCT sg_val;
  tSDP_IOCB iocb;
  tSDP_ADVT advt;
  tINT32 result;
  tINT32 zcopy;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * check queue depth
   */
  if (!(TS_SDP_DEV_SEND_POST_MAX > conn->s_wq_size)) {

    result = ENODEV;
    goto done;
  } /* if */
  /*
   * get a reference to the first SrcAvail advertisment.
   */
  advt = tsSdpConnAdvtTableLook(&conn->src_pend);
  if (NULL == advt) {

    result = ENODEV;
    goto done;
  } /* if */
  /*
   * get a reference to the first IOCB pending.
   *
   * check if the IOCB is in cancel processing.
   * (final complete RDMA will clear it out.)
   */
  iocb = tsSdpConnIocbTableLook(&conn->r_pend);
  if (NULL == iocb) {

    result = ENODEV;
    goto done;
  } /* if */
  /*
   * register IOCBs physical memory if this is the first time for
   * this IOCB.
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
   * amount of data to zcopy.
   */
  zcopy = min(advt->size, iocb->len);

  sg_val.address = iocb->io_addr;
  sg_val.key     = iocb->l_key;
  sg_val.length  = zcopy;

  send_param.op             = TS_IB_OP_RDMA_READ;
  send_param.remote_address = advt->addr;
  send_param.rkey           = advt->rkey;
  send_param.signaled       = 1;

  iocb->wrid     = TS_SDP_WRID_READ_FLAG | conn->recv_wrid++;
  iocb->len     -= zcopy;
  iocb->post    += zcopy;
  iocb->io_addr += zcopy;
  iocb->flags   |= TS_SDP_IOCB_F_ACTIVE;
  iocb->flags   |= TS_SDP_IOCB_F_RDMA_R;

  advt->wrid     = iocb->wrid;
  advt->size    -= zcopy;
  advt->addr    += zcopy;
  advt->post    += zcopy;
  advt->flag    |= TS_SDP_ADVT_F_READ;
  /*
   * if there is no more advertised space,  queue the
   * advertisment for completion
   */
  if (!(0 < advt->size)) {

    advt = tsSdpConnAdvtTableGet(&conn->src_pend);
    if (NULL == advt) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "POST: src available advertisment disappeared.");
      result = -ENODEV;
      goto error;
    } /* if */

    result = tsSdpConnAdvtTablePut(&conn->src_actv, advt);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "POST: Error <%d> queuing active advertisment.", result);

      (void)tsSdpConnAdvtDestroy(advt);
      goto error;
    } /*  if */
  } /* if */
  /*
   * if there is no more iocb space queue the it for completion
   */
  if (!(0 < iocb->len)) {

    iocb = tsSdpConnIocbTableGetHead(&conn->r_pend);
    if (NULL == iocb) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "POST: read IOCB has disappeared from table.");
      result = -ENODEV;
      goto error;
    } /* if */

    result = tsSdpGenericTablePutTail(&conn->r_src, (tSDP_GENERIC)iocb);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "POST: Error <%d> queuing active read IOCB.", result);

      (void)tsSdpConnIocbDestroy(iocb);
      goto error;
    } /*  if */
  } /* if */
#ifdef _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "POST: READ IOCB wrid <%08x> of <%u> bytes <%d:%d>.",
	   iocb->wrid, zcopy, iocb->len, advt->size);
#endif
  /*
   * update send queue depth
   */
  conn->s_wq_size++;
  /*
   * post RDMA
   */
  send_param.work_request_id    = iocb->wrid;
  send_param.gather_list        = &sg_val;
  send_param.num_gather_entries = 1;

  result = tsIbSend(conn->qp, &send_param, 1);
  if (0 != result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: send (rdma read) failure. <%d>", result);

    conn->s_wq_size--;
    goto error;
  } /* if */

  return 0;
error:
done:
  return result;
} /* _tsSdpRecvPostRdmaIocbSrc */

/* ========================================================================= */
/*.._tsSdpRecvPostRdmaIocbSnk -- post a iocb for rdma read on a conn */
static tINT32 _tsSdpRecvPostRdmaIocbSnk
(
 tSDP_CONN conn
)
{
  tINT32 result = 0;
  tSDP_IOCB iocb;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * check if sink cancel is pending
   */
  if (0 < (TS_SDP_CONN_F_SNK_CANCEL & conn->flags)) {

    result = ENODEV;
    goto error;
  } /* if */
  /*
   * get the pending iocb
   */
  iocb = tsSdpConnIocbTableLook(&conn->r_pend);
  if (NULL == iocb) {

    result = ENODEV;
    goto error;
  } /* if */
  /*
   * check zcopy threshold
   */
  if (conn->snk_zthresh > iocb->len) {

    result = ENODEV;
    goto error;
  } /* if */
  /*
   * check number of outstanding sink advertisments
   */
  if (!(conn->r_max_adv > conn->snk_sent)) {

    result = ENODEV;
    goto error;
  } /* if */
  /*
   * registration
   */
  if (NULL == iocb->page_array) {

    result = tsSdpConnIocbRegister(iocb, conn);
    if (0 > result) {
      if (-EAGAIN == result) {

	result = EAGAIN;
      } /* if */
      else {

	TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
		 "POST: Error <%d> registering IOCB. <%d:%d>",
		 result, iocb->key, iocb->len);
      } /* else */

      goto error;
    } /* if */
  } /* if */
  /*
   * IOCB
   */
  iocb->flags |= TS_SDP_IOCB_F_ACTIVE;
  iocb->flags |= TS_SDP_IOCB_F_RDMA_W;
  /*
   * queue IOCB
   */
  iocb = tsSdpConnIocbTableGetHead(&conn->r_pend);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
             "WRITE: Failed to get queued write IOCB <%d> from pending table",
             result, iocb->key, tsSdpConnIocbTableSize(&conn->r_pend));
    goto release;
  } /* if */

  result = tsSdpConnIocbTablePutTail(&conn->r_snk, iocb);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
             "WRITE: Error <%d> queueing write IOCB <%d> to active table",
             result, iocb->key, tsSdpConnIocbTableSize(&conn->r_snk));

    goto re_q;
  } /* if */
  /*
   * Either post a send, or buffer the packet in the tx queue
   */
  result = tsSdpSendCtrlSnkAvail(conn, iocb->len, iocb->r_key, iocb->io_addr);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failed to post buffered control message. <%d>", result);
    goto de_q;
  } /* if */

  conn->snk_sent++;

  return 0;
de_q:
  iocb = tsSdpConnIocbTableGetTail(&conn->r_snk);
re_q:
  (void)tsSdpConnIocbTablePutHead(&conn->r_pend, iocb);
release:
  iocb->flags &= ~TS_SDP_IOCB_F_ACTIVE;
  iocb->flags &= ~TS_SDP_IOCB_F_RDMA_W;

  (void)tsSdpConnIocbRelease(iocb);
error:
  return result;
} /* _tsSdpRecvPostRdmaIocbSnk */

/* ========================================================================= */
/*.._tsSdpRecvPostRdma -- post a rdma based requests for a connection */
static tINT32 _tsSdpRecvPostRdma
(
 tSDP_CONN conn
)
{
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * Since RDMA Reads rely on posting to the Send WQ, stop if
   * we're not in an appropriate state. It's possible to queue
   * the sink advertisment, something to explore, but SrcAvail
   * slow start might make that unneccessart?
   */
  if (0 == (TS_SDP_ST_MASK_SEND_OK & conn->state)) {

    return 0;
  } /* if */
  /*
   * loop flushing IOCB RDMAs. Read sources, otherwise post sinks.
   */
  if (0 < tsSdpConnAdvtTableSize(&conn->src_pend)) {

    if (0 == tsSdpGenericTableTypeSize(&conn->r_src,
				       TS_SDP_GENERIC_TYPE_BUFF)) {

      while (0 == (result = _tsSdpRecvPostRdmaIocbSrc(conn))) {
	  /*
	   * pass, nothing to do in loop.
	   */
      } /* while */
      /*
       * check non-zero result
       */
      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "POST: failure posting RDMA IOCB read. <%d>", result);
	goto done;
      } /* if */
    } /* if */
    /*
     * loop posting RDMA reads, if there is room.
     */
    if (0 == tsSdpConnIocbTableSize(&conn->r_pend)) {

      while (0 < tsSdpConnAdvtTableSize(&conn->src_pend) &&
	     TS_SDP_RECV_BUFFERS_MAX > tsSdpBuffPoolSize(&conn->recv_pool) &&
	     conn->rwin_max > conn->byte_strm) {

	if (0 != (result =  _tsSdpRecvPostRdmaBuff(conn))) {

	  if (0 > result) {

	    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		     "POST: failure posting RDMA read. <%d>", result);
	    goto done;
	  } /* if */
	  else {
	    /*
	     * No more posts allowed.
	     */
	    break;
	  } /* else */
	} /* if */
      } /* while */
    } /* if */
  } /* if */
  else {

    if (0 < tsSdpConnIocbTableSize(&conn->r_pend) &&
	TS_SDP_MODE_PIPE == conn->recv_mode &&
	0 == tsSdpConnAdvtTableSize(&conn->src_actv)) {

      while (0 == (result = _tsSdpRecvPostRdmaIocbSnk(conn))) {
	/*
	 * pass
	 */
      } /* while */

      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "POST: Error <%d> sending sink advertisment.", result);
	goto done;
      } /* if */
    } /* if */
  } /* else */

  result = 0;
done:
  return result;
} /* _tsSdpRecvPostRdma */

/* ========================================================================= */
/*..tsSdpRecvPost -- post a certain number of buffers on a connection */
tINT32 tsSdpRecvPost
(
 tSDP_CONN conn
)
{
  tINT32 result;
  tINT32 counter;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * verify that the connection is in a posting state
   */
  if (0 == (TS_SDP_ST_MASK_RCV_POST & conn->state)) {

    return 0;
  } /* if */
  /*
   * loop posting receive buffers onto the queue
   */
  /*
   * 1) Calculate available space in the receive queue. Take the smallest
   *    between bytes available for buffering and maximum number of buffers
   *    allowed in the queue. (this prevents a flood of small buffers)
   * 2) Subtract buffers already posted
   * 3) Take the smallest buffer count between those needed to fill
   *    the buffered receive/receive posted queue, and the maximum
   *    number which are allowed to be posted at a given time.
   */
  counter = min((tINT32)((conn->rwin_max - conn->byte_strm)/conn->recv_size),
                (tINT32)(TS_SDP_RECV_BUFFERS_MAX -
			 tsSdpBuffPoolSize(&conn->recv_pool)));
  counter -= conn->l_recv_bf;

  counter = min(counter, (conn->recv_max - conn->l_recv_bf));

  while (0 < counter--) {

    if (0 != (result =  _tsSdpRecvPostRecvBuff(conn))) {

      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "POST: failure posting receive. <%d>", result);
	goto done;
      } /* if */
      else {
	/*
	 * No more recv buffers allowed.
	 */
	break;
      } /* else */
    } /* if */
  } /* while */
  /*
   * If we are in Sink Cancel processing, and the active sink queue has
   * been consumed, we can come out of sink processing.
   */
  if (0 < (TS_SDP_CONN_F_SNK_CANCEL & conn->flags) &&
      0 == tsSdpConnIocbTableSize(&conn->r_snk)) {

    conn->flags &= ~TS_SDP_CONN_F_SNK_CANCEL;
  } /* if */
  /*
   * Next the connection should consume RDMA Source advertisments or
   * create RDMA Sink advertisments, either way setup for RDMA's for
   * data flowing from the remote connection peer to the local
   * connection peer.
   */
  result = _tsSdpRecvPostRdma(conn);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Error <%d> while posting RDMA. <%04x>",
	     result, conn->state);
    goto done;
  } /* if */
  /*
   * Gratuitous increase of remote send credits. Independant of posting
   * recveive buffers, it may be neccessary to notify the remote client
   * of how many buffers are available. For small numbers advertise more
   * often, then for large numbers. Always advertise when we add the
   * first two buffers.
   *
   * 1) Fewer advertised buffers then actual posted buffers.
   * 2) Less then three buffers advertised. (OR'd with the next two
   *    because we can have lots of sinks, but still need to send
   *    since those sinks may never get used. (EOF))
   * 3) The discrepency between posted and advertised is greater then three
   * 4) The peer has no source or sink advertisments pending. In process
   *    advertisments generate completions, that's why no ack.
   */
  if ((3 > conn->l_advt_bf &&
       conn->l_recv_bf > conn->l_advt_bf) ||
      (TS_SDP_CONN_RECV_POST_ACK < (conn->l_recv_bf - conn->l_advt_bf) &&
       0 == ((tUINT32)conn->snk_recv + (tUINT32)conn->src_recv))) {

    result = tsSdpSendCtrlAck(conn);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "POST: failed to post ACK while growing recveive pool. <%d>",
	       result);
      goto done;
    } /* if */
  } /* if */

  result = 0;
done:
  return result;
} /* tsSdpRecvPost */
/* --------------------------------------------------------------------- */
/*                                                                       */
/* Receive incoming data function(s)                                     */
/*                                                                       */
/* --------------------------------------------------------------------- */
#ifdef _TS_SDP_AIO_SUPPORT
/* ========================================================================= */
/*.._tsSdpBuffKvecRead -- read a SDP buffer into a kvec */
static tINT32 _tsSdpBuffKvecRead
(
 struct kvec_dst *dst,
 tSDP_BUFF        buff,
 tINT32           len
)
{
  tPTR   data;
  tPTR   tail;
  tINT32 part;
  tINT32 left;
  tINT32 copy;

  TS_CHECK_NULL(dst, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);
  /*
   * OOB buffer adjustment. We basically throw away OOB data
   * when writing into AIO buffer. We can't split it into it's
   * own AIO buffer, because that would violate outstanding
   * advertisment calculations.
   */
  data        = buff->data;
  tail        = buff->tail;
  buff->tail -= (0 < (TS_SDP_BUFF_F_OOB_PRES & buff->flags)) ? 1 : 0;
  /*
   * activate destination memory kmap
   */
  TS_SDP_KVEC_DST_MAP(dst);
  /*
   * copy buffer to dst
   */
  copy = min(len, (tINT32)(buff->tail - buff->data));

  for (left = copy; 0 < left; ) {

    part = min(left, dst->space);
#ifndef _TS_SDP_DATA_PATH_NULL
    memcpy(dst->dst, buff->data, part);
#endif

    buff->data += part;
    dst->space -= part;
    dst->dst   += part;
    left       -= part;

    if (0 < left &&
	0 == dst->space) {

      TS_SDP_KVEC_DST_UNMAP(dst);
      dst->let++;
      dst->offset = 0;
      TS_SDP_KVEC_DST_MAP(dst);

      if (!(0 < dst->space)) { /* sanity check */

	buff->data = data;
	copy = -EFAULT;
	goto error;
      } /* if */
    } /* if */
  } /* for */

error:
  /*
   * release destination memory kmap
   */
  TS_SDP_KVEC_DST_UNMAP(dst);
  /*
   * restore tail from OOB offset.
   */
  buff->tail = tail;

  return copy;
} /* _tsSdpBuffKvecRead */

/* ========================================================================= */
/*.._tsSdpBuffKvecReadFlush -- read multiple SDP buffers into a kvec */
tINT32 _tsSdpBuffKvecReadFlush
(
 struct sock     *sk,
 struct kvec_dst *dst,
 tINT32           len
)
{
  tSDP_CONN conn;
  tSDP_BUFF buff;
  tINT32    copied = 0;
  tINT32    partial;
  tINT32    result;
  tINT32    expect;

  TS_CHECK_NULL(sk, -EINVAL);
  TS_CHECK_NULL(dst, -EINVAL);
  conn = TS_SDP_GET_CONN(sk);
  /*
   * loop through queued buffers and copy them to the destination
   */
  while (copied < len &&
	 NULL != (buff = tsSdpBuffPoolGetHead(&conn->recv_pool))) {
    /*
     * OOB data is trown away.
     */
    if (0 < (TS_SDP_BUFF_F_OOB_PRES & buff->flags) &&
	1 == (buff->tail - buff->data)) {

      conn->rcv_urg_cnt -= 1;
      conn->byte_strm   -= 1;
      buff->data        += 1;
    } /* if */
    else {

      partial = _tsSdpBuffKvecRead(dst, buff, (len - copied));
      if (0 > partial) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "READ: error <%d> copying data <%d:%d> to vector.",
		 partial, copied, len);

	expect = tsSdpBuffPoolPutHead(&conn->recv_pool, buff);
	TS_EXPECT(MOD_LNX_SDP, !(0 > expect));

	result = partial;
	goto done;
      } /* if */

      copied          += partial;
      conn->byte_strm -= partial;
    } /* else */
    /*
     * consume buffer
     */
    if (buff->tail > buff->data) {

      expect = tsSdpBuffPoolPutHead(&conn->recv_pool, buff);
      TS_EXPECT(MOD_LNX_SDP, !(0 > expect));
    } /* if */
    else {

      expect = tsSdpBuffMainPut(buff);
      TS_EXPECT(MOD_LNX_SDP, !(0 > expect));
    } /* else */
  } /* while */

  result = 0;
done:

  return ((0 < copied) ? copied : result);
} /* _tsSdpBuffKvecReadFlush */
#endif

/* ========================================================================= */
/*.._tsSdpSockBuffReadActive -- Ease AIO read pending pressure */
static tINT32 _tsSdpSockBuffReadActive
(
 tSDP_CONN conn,
 tSDP_BUFF buff
)
{
#ifdef _TS_SDP_AIO_SUPPORT
  tSDP_IOCB iocb;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);
  /*
   * Get the IOCB, We'll fill with exactly one
   */
  iocb = tsSdpConnIocbTableGetHead(&conn->r_snk);
  if (NULL == iocb) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Empty active IOCB queue. <%d>",
	     tsSdpConnIocbTableSize(&conn->r_snk));
    return -EPROTO;
  } /* if */

  TS_EXPECT(MOD_LNX_SDP, (0 < (TS_SDP_IOCB_F_RDMA_W & iocb->flags)));
  /*
   * TODO: need to be checking OOB here.
   */
  result = _tsSdpBuffKvecRead(&iocb->kvec.dst, buff, iocb->len);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "READ: error <%d> copying data <%d:%d> to vector.",
	     result, iocb->len, (buff->tail - buff->data));

    (void)tsSdpConnIocbTablePutHead(&conn->r_snk, iocb);
    goto error;
  } /* if */

  iocb->len         -= result;
  iocb->post        += result;

  TS_SDP_CONN_STAT_READ_INC(conn, iocb->post);
  TS_SDP_CONN_STAT_RQ_DEC(conn, iocb->size);

  conn->nond_recv--;
  conn->snk_sent--;
  /*
   * callback to complete IOCB
   */
  result = tsSdpConnIocbComplete(iocb, 0);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: Error <%d> completing iocb. <%d>", result, iocb->key);

    (void)tsSdpConnIocbDestroy(iocb);
  } /* if */

  return (buff->tail - buff->data);
error:
  return result;
#else
  return 0;
#endif
} /* _tsSdpSockBuffReadActive */

/* ========================================================================= */
/*.._tsSdpSockBuffReadPending -- Ease AIO read pending pressure */
static tINT32 _tsSdpSockBuffReadPending
(
 tSDP_CONN conn,
 tSDP_BUFF buff
)
{
#ifdef _TS_SDP_AIO_SUPPORT
  tSDP_IOCB iocb;
  tINT32 copied;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);
  /*
   * check the IOCB
   */
  iocb = tsSdpConnIocbTableLook(&conn->r_pend);
  if (NULL == iocb) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "STRM: Empty pending IOCB queue. <%d>",
	     tsSdpConnIocbTableSize(&conn->r_pend));
    return -EPROTO;
  } /* if */
  /*
   * TODO: need to be checking OOB here.
   */
  copied = _tsSdpBuffKvecRead(&iocb->kvec.dst, buff, iocb->len);
  if (0 > copied) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "READ: error <%d> copying data <%d:%d> to vector.",
	     copied, iocb->len, (buff->tail - buff->data));
    goto error;
  } /* if */

  iocb->len       -= copied;
  iocb->post      += copied;
  /*
   * If the amount of data moved into the IOCB is greater then the
   * socket recv low water mark, then complete the IOCB, otherwise
   * this loop is done.
   */
  if (!(conn->sk->rcvlowat > iocb->post)) {
    /*
     * complete IOCB
     */
    iocb = tsSdpConnIocbTableGetHead(&conn->r_pend);
    TS_EXPECT(MOD_LNX_SDP, (NULL != iocb));

    TS_SDP_CONN_STAT_READ_INC(conn, iocb->post);
    TS_SDP_CONN_STAT_RQ_DEC(conn, iocb->size);
    /*
     * callback to complete IOCB
     */
    result = tsSdpConnIocbComplete(iocb, 0);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: Error <%d> completing iocb. <%d>", result, iocb->key);

      (void)tsSdpConnIocbDestroy(iocb);
    } /* if */
  } /* if */

  return (buff->tail - buff->data);
error:
  return copied;
#else
  return 0;
#endif
} /* _tsSdpSockBuffReadPending */

/* ========================================================================= */
/*..tsSdpSockBuffRecv -- Process a new buffer based on queue type. */
tINT32 tsSdpSockBuffRecv
(
 tSDP_CONN conn,
 tSDP_BUFF buff
)
{
  tINT32 result;
  tINT32 buffered;

  TS_CHECK_NULL(conn, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

#ifdef _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: <%08x:%04x><%08x:%04x>  <%04x> recv "
	   "id <%08x> bytes <%d>",
	   conn->src_addr, conn->src_port, conn->dst_addr, conn->dst_port,
	   conn->state, buff->u_id, (buff->tail - buff->data));
#endif
  /*
   * To emulate RFC 1122 (page 88) a connection should be reset/aborted
   * if data is received and the receive half of the connection has been
   * closed. This notifies the peer that the data was not received.
   */
  if (0 < (TS_SDP_SHUTDOWN_RECV & conn->shutdown)) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "EVENT: Received Data, receive path closed. <%02x:%04x>",
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
   * oob notification.
   */
  if (0 < (TS_SDP_BUFF_F_OOB_PEND & buff->flags)) {

    conn->rcv_urg_cnt++;
    tsSdpInetWakeUrg(conn);
  } /* if */
  /*
   * loop while there are available IOCB's, break if there is no more data
   * to read
   */
  while (0 < (tsSdpConnIocbTableSize(&conn->r_pend) +
	      tsSdpConnIocbTableSize(&conn->r_snk))) {
    /*
     * if there is OOB data in a buffer, the two functions below
     * will leave the byte in the buffer, and potentially loop
     * to here. In which case we are done and the buffer is queued.
     * this allows POLL notification to work, and the OOB byte(s)
     * will not be consumed until the next AIO buffer is posted,
     * or a socket recv (regular or OOB) is called.
     */
    if (0 < (TS_SDP_BUFF_F_OOB_PRES & buff->flags) &&
	1 == (buff->tail - buff->data)) {

      break;
    } /* if */
    /*
     * process either a sink available IOCB which needs to be
     * discarded with exactly one buffer, or process a pending
     * IOCB.
     */
    if (0 < conn->snk_sent) {

      result = _tsSdpSockBuffReadActive(conn, buff);
    } /* if */
    else {

      result = _tsSdpSockBuffReadPending(conn, buff);
    } /* else */
    /*
     * Check result. Postitive result is data left in the buffer
     */
    if (0 == result) {

      break;
    } /* if */
    else {

      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "EVENT: Error <%d> processing buffered IOCB. <%d:%d:%d>",
		 result, conn->snk_sent,
		 tsSdpConnIocbTableSize(&conn->r_pend),
		 tsSdpConnIocbTableSize(&conn->r_snk));
	goto done;
      } /* if */
    }  /* else */
  } /* while */
  /*
   * If there is still data in the buffer then queue it for later.
   */
  buffered = buff->tail - buff->data;

  if (0 < buffered) {

    result = tsSdpBuffPoolPutTail(&conn->recv_pool, buff);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
    /*
     * if there is still data left, signal user.
     */
    tsSdpInetWakeRecv(conn, conn->byte_strm);
  } /* if */

  return buffered;
done:
  return result;
} /* tsSdpSockBuffRecv */
/* --------------------------------------------------------------------- */
/*                                                                       */
/* User initiated receive data function(s)                               */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsSdpInetRecvUrgTestFunc -- recv queue urgent data cleanup function */
static tINT32 _tsSdpInetRecvUrgTestFunc
(
 tSDP_BUFF buff,
 tPTR     arg
)
{
  TS_CHECK_NULL(buff, -EINVAL);

  return ((buff->tail == buff->head) ? 1 : 0);
} /* _tsSdpInetUrgRecvTestFunc */

/* ========================================================================= */
/*.._tsSdpInetRecvUrgTravFunc -- recv queue urgent data retreival function */
static tINT32 _tsSdpInetRecvUrgTravFunc
(
 tSDP_BUFF buff,
 tPTR     arg
)
{
  tUINT8 *value = (tUINT8 *)arg;
  tUINT8 update;

  TS_CHECK_NULL(buff, -EINVAL);
  TS_CHECK_NULL(value, -EINVAL);

  if (0 < (TS_SDP_BUFF_F_OOB_PRES & buff->flags)) {

    TS_EXPECT(MOD_LNX_SDP, (buff->tail > buff->data));

    update = *value;
    *value = *(tUINT8 *)(buff->tail - 1);

    if (0 < update) {

      buff->tail--;
      buff->flags &= ~TS_SDP_BUFF_F_OOB_PRES;
    } /* if */

    return -ERANGE;
  } /* if */

  return 0;
} /* _tsSdpInetUrgRecvTravFunc */

/* ========================================================================= */
/*.._tsSdpInetRecvUrg -- recv urgent data from the network to user space */
static tINT32 _tsSdpInetRecvUrg
(
 struct sock   *sk,
 struct msghdr *msg,
 tINT32         size,
 tINT32         flags
)
{
  tSDP_CONN conn;
  tSDP_BUFF buff;
  tINT32 result = 0;
  tUINT8 value;

  TS_CHECK_NULL(sk, -EINVAL);
  TS_CHECK_NULL(msg, -EINVAL);
  conn = TS_SDP_GET_CONN(sk);

  if (0 <  TS_SDP_OS_SK_URGINLINE(sk) ||
      0 == conn->rcv_urg_cnt) {

    return -EINVAL;
  } /* if */
  /*
   * don't cosume data on PEEK, but do consume data on TRUNC
   */
#if 0
  value = (0 < (MSG_PEEK & flags)) || (0 == size) ? 0 : 1;
#else
  value = (0 < (MSG_PEEK & flags)) ? 0 : 1;
#endif

  result = tsSdpBuffPoolTraverseHead(&conn->recv_pool,
				     _tsSdpInetRecvUrgTravFunc,
				     (tPTR)&value);
  if (-ERANGE == result) {

    msg->msg_flags |= MSG_OOB;
    if (0 < size) {

      result = memcpy_toiovec(msg->msg_iov, &value, 1);
      if (0 != result) {

	goto done;
      } /* if */
      /*
       * clear urgent pointer on consumption
       */
      if (0 == (MSG_PEEK & flags)) {

	conn->rcv_urg_cnt -= 1;
	conn->byte_strm   -= 1;

	TS_SDP_CONN_STAT_RECV_INC(conn, 1);
	/*
	 * we've potentially emptied a buffer, if so find and dispose
	 * of it, and repost if appropriate.
	 */
	buff = tsSdpBuffPoolFetch(&conn->recv_pool,
				  _tsSdpInetRecvUrgTestFunc,
				  (tPTR)0);
	if (NULL != buff) {

	  result = tsSdpBuffMainPut(buff);
	  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

	  result = tsSdpRecvPost(conn);
	  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
	} /* if */

	result = 1;
      } /* if */
    } /* if */
    else {

      msg->msg_flags |= MSG_TRUNC;
      result = 0;
    } /* else */
  } /* if */
  else {

    result = (0 != result) ? result : -EAGAIN;
  } /* else */
done:
  return result;
} /* _tsSdpInetRecvUrg */

/* ========================================================================= */
/*..tsSdpInetRecv -- recv data from the network to user space. */
tINT32 tsSdpInetRecv
(
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
)
{
  struct sock      *sk;
  tSDP_CONN         conn;
  tSDP_BUFF         buff;
  tSDP_BUFF         head = NULL;
  long              timeout;
  size_t            length;
  tINT32            result = 0;
  tINT32            expect;
  tINT32            low_water;
  tINT32            copied = 0;
  tINT32            copy;
  tINT32            update;
  tINT32            free_count = 0;
  tINT8             oob = 0;
  tINT8             ack = 0;
  tSDP_POOL_STRUCT  peek_queue;

  TS_CHECK_NULL(sock, -EINVAL);
  TS_CHECK_NULL(sock->sk, -EINVAL);
  TS_CHECK_NULL(msg, -EINVAL);

  sk   = sock->sk;
  conn = TS_SDP_GET_CONN(sk);

#ifdef _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: recv <%08x:%04x> <%08x:%04x> <%04x> flags <%08x> "
	   "size <%d> pending <%d>",
	   conn->src_addr, conn->src_port, conn->dst_addr, conn->dst_port,
	   conn->state, flags, size, conn->byte_strm);
#endif

  if (0 < (MSG_TRUNC & flags)) {
    /*
     * TODO: unhandled, but need to be handled.
     */
    return -EOPNOTSUPP;
  } /* if */

  if (0 < (MSG_PEEK & flags)) {

    (void)tsSdpBuffPoolInit(&peek_queue, _peek_queue_name, 0);
    msg->msg_flags |= MSG_PEEK;
  } /* if */

  TS_SDP_CONN_LOCK(conn);

  if (TS_SDP_SOCK_ST_LISTEN == conn->istate ||
      TS_SDP_SOCK_ST_CLOSED == conn->istate) {

    result = -ENOTCONN;
    goto done;
  } /* if */
  /*
   * process urgent data
   */
  if (0 < (MSG_OOB & flags)) {

    result = _tsSdpInetRecvUrg(sk, msg, size, flags);
    copied = (0 < result) ? result : 0;
    result = (0 < result) ? 0 : result;
    goto done;
  } /* if */
  /*
   * get socket values we'll need.
   */
  timeout   = sock_rcvtimeo(sk, (MSG_DONTWAIT & flags));
  low_water = sock_rcvlowat(sk, (MSG_WAITALL & flags), size);
  /*
   * process data first, and then check condition, then wait
   */
  while (copied < size) {
    /*
     * first copy any data that might be present
     */
    while (copied < size &&
	   NULL != (buff = tsSdpBuffPoolGetHead(&conn->recv_pool))) {

      length = buff->tail - buff->data;
      update = 0;

      if (0 < (TS_SDP_BUFF_F_OOB_PRES & buff->flags)) {
	/*
	 * if data has already been read, and the next byte is the
	 * urgent byte, reading needs to terminate, taking precidence
	 * over the low water mark. There needs to be a break in the
	 * the read stream around the OOB byte regarless if it is
	 * inline or not, to ensure that the user has a chance to
	 * read the byte.
	 */
	if (1 < length) {

	  length--;
	} /* if */
	else {

	  if (0 < copied) {
	    /*
	     * update such that we pass through the copy phase,
	     * return the buffer, and break.
	     */
	    length = 0;
	    update = 0;
	    oob    = 1; /* break on oob */
	  } /* if */
	  else {

	    if (0 == TS_SDP_OS_SK_URGINLINE(sk)) {
	      /*
	       * skip this byte, but make sure it's counted.
	       */
	      length = 0;
	      update = (0 < (MSG_PEEK & flags)) ? 0 : 1;
	    } /* if (OOB) */
	  } /* else (copied) */
	} /* else (sitting on URG byte) */
      } /* if (urgent present) */

      copy = min((size_t)(size - copied), length);

      if (0 < copy) {
#ifndef _TS_SDP_DATA_PATH_NULL
	result = memcpy_toiovec(msg->msg_iov, buff->data, copy);
	if (0 > result) {

	  expect = tsSdpBuffPoolPutHead(&conn->recv_pool, buff);
	  TS_EXPECT(MOD_LNX_SDP, !(0 > expect));

	  goto done;
	} /* if */
#endif
        update = (0 < (MSG_PEEK & flags)) ? 0 : copy;
      } /* if */

      TS_SDP_CONN_STAT_RECV_INC(conn, update);

      conn->byte_strm -= update;
      buff->data      += update;
      copied          += copy;

      if (0 < (buff->tail - buff->data)) {

	expect = tsSdpBuffPoolPutHead(&conn->recv_pool, buff);
	TS_EXPECT(MOD_LNX_SDP, !(0 > expect));
	/*
	 * always break, PEEK and OOB together could throw us into a
	 * loop without a forced break here, since the buffer data
	 * pointer wasn't really updated. OOB data at the head of
	 * stream, after data has already been copied relies on this
	 * break as well.
	 */
	break;
      } /* if */
      else {

	if (0 < (MSG_PEEK & flags)) {

	  expect = tsSdpBuffPoolPutHead(&peek_queue, buff);
	  TS_EXPECT(MOD_LNX_SDP, !(0 > expect));
	} /* if */
	else {

	  if (0 < (TS_SDP_BUFF_F_OOB_PRES & buff->flags)) {

	    conn->rcv_urg_cnt -= 1;
	  } /* if */
	  /*
	   * create a link of buffers which will be returned to
	   * the free pool in one group.
	   */
	  expect = tsSdpBuffMainChainLink(head, buff);
	  TS_EXPECT(MOD_LNX_SDP, !(0 > expect));

	  head = buff;
	  free_count++;
	  /*
	   * post additional recv buffers if needed, but check only
	   * every N buffers...
	   */
	  if (TS_SDP_CONN_RECV_POST_FREQ < ++ack) {

	    result = tsSdpRecvPost(conn);
	    if (0 > result) {

	      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		       "RECV: device buffer ack error. <%d>", result);
	      goto done;
	    } /* if */

	    ack = 0;
	  } /* if */

	} /* else */
      } /* else */
    } /* while */
    /*
     * urgent data needs to break up the data stream, regardless of
     * low water mark, or whether there is room in the buffer.
     */
    if (0 < oob) {

      result = 0;
      break;
    } /* if */
    /*
     * If there is more room for data, cycle the connection lock to
     * potentially flush events into the recv queue. This is done
     * before the low water mark is checked to optimize the number
     * of syscalls a read process needs to make for a given amount
     * of data.
     */
    if (copied < size) {
      /*
       * process backlog
       */
      TS_SDP_CONN_RELOCK(conn);

      if (0 < tsSdpBuffPoolSize(&conn->recv_pool)) {

	continue;
      } /* if */
    } /* if */
    /*
     * If not enough data has been copied to userspace, check for
     * connection errors, and then wait for more data.
     */
    if (copied < low_water) {
      /*
       * check status. POSIX 1003.1g order.
       */
      if (0 != TS_SDP_OS_CONN_GET_ERR(conn)) {

	result = (0 < copied) ? 0 : TS_SDP_CONN_ERROR(conn);
	break;
      } /* if */

      if (0 < (TS_SDP_SHUTDOWN_RECV & conn->shutdown)) {

	result = 0;
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
      /*
       * wait
       */
      {
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(TS_SDP_OS_SK_SLEEP(sk), &wait);
	set_current_state(TASK_INTERRUPTIBLE);

	set_bit(SOCK_ASYNC_WAITDATA, &TS_SDP_OS_SK_SOCKET(sk)->flags);

	if (0 == tsSdpBuffPoolSize(&conn->recv_pool)) {

	  TS_SDP_CONN_UNLOCK(conn);
	  timeout = schedule_timeout(timeout);
	  TS_SDP_CONN_LOCK(conn);
	} /* if */

	clear_bit(SOCK_ASYNC_WAITDATA, &TS_SDP_OS_SK_SOCKET(sk)->flags);
	remove_wait_queue(TS_SDP_OS_SK_SLEEP(sk), &wait);
	set_current_state(TASK_RUNNING);
      }
      /*
       * check signal pending
       */
      if (signal_pending(current)) {

	result = (0 < timeout) ? sock_intr_errno(timeout) : -EAGAIN;
	break;
      } /* if */
    } /* if */
    else {

      break;
    } /* else */
  } /* while */

done:
  /*
   * acknowledge moved data
   */
  if (0 < ack) {

    expect = tsSdpRecvPost(conn);
    if (0 > expect) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "RECV: device buffer ack error. <%d>", expect);
    } /* if */
  } /* if */

  (void)tsSdpBuffMainChainPut(head, free_count);
  /*
   * return any peeked buffers to the recv queue, in the correct order.
   */
  if (0 < (MSG_PEEK & flags)) {

    while (NULL != (buff = tsSdpBuffPoolGetTail(&peek_queue))) {

      expect = tsSdpBuffPoolPutHead(&conn->recv_pool, buff);
      TS_EXPECT(MOD_LNX_SDP, !(0 > expect));
    } /* while */
  } /* if */

  TS_SDP_CONN_UNLOCK(conn);
  return ((0 < copied) ? copied : result);
} /* tsSdpInetRecv */

#ifdef _TS_SDP_AIO_SUPPORT
/* ========================================================================= */
/*.._tsSdpReadIocbCancelFunc -- lookup function for cancelation */
static tINT32 _tsSdpReadIocbCancelFunc
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
} /* _tsSdpReadIocbCancelFunc */

/* ========================================================================= */
/*.._tsSdpInetReadIocbCancel -- cancel an IO operation */
static tINT32 _tsSdpInetReadIocbCancel
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

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
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
  iocb = tsSdpConnIocbTableLookup(&conn->r_pend, kiocb->key);
  if (NULL != iocb) {
    /*
     * always remove the IOCB.
     * If active, then place it into the correct active queue
     */
    result = tsSdpConnIocbTableRemove(iocb);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));

    if (0 < (TS_SDP_IOCB_F_ACTIVE & iocb->flags)) {

      if (0 < (TS_SDP_IOCB_F_RDMA_W & iocb->flags)) {

	result = tsSdpConnIocbTablePutTail(&conn->r_snk, iocb);
	TS_EXPECT(MOD_LNX_SDP, !(0 > result));
      } /* if */
      else {

	TS_EXPECT(MOD_LNX_SDP, (0 < (TS_SDP_IOCB_F_RDMA_R & iocb->flags)));

	result = tsSdpGenericTablePutTail(&conn->r_src, (tSDP_GENERIC)iocb);
	TS_EXPECT(MOD_LNX_SDP, !(0 > result));
      } /* else */
    } /* if */
    else {
      /*
       * callback to complete IOCB, or drop reference
       */
      result = tsSdpConnIocbComplete(iocb, 0);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));

      result = -EAGAIN;
      goto unlock;
    } /* else */
  } /* if */
  /*
   * check the source queue, not much to do, since the operation is
   * already in flight.
   */
  iocb = (tSDP_IOCB)tsSdpGenericTableLookup(&conn->r_src,
					    _tsSdpReadIocbCancelFunc,
					    (tPTR)(unsigned long)kiocb->key);
  if (NULL != iocb) {

    iocb->flags |= TS_SDP_IOCB_F_CANCEL;
    result = -EAGAIN;

    goto unlock;
  } /* if */
  /*
   * check sink queue. If we're in the sink queue, then a cancel
   * needs to be issued.      */
  iocb = tsSdpConnIocbTableLookup(&conn->r_snk, kiocb->key);
  if (NULL != iocb) {
    /*
     * Unfortunetly there is only a course grain cancel in SDP, so
     * we have to cancel everything. This is OKish since it usually
     * only happens at connection termination, and the remaining
     * source probably will get cancel requests as well.
     */
    if (0 == (TS_SDP_CONN_F_SNK_CANCEL & conn->flags)) {

      result = tsSdpSendCtrlSnkCancel(conn);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));

      conn->flags |= TS_SDP_CONN_F_SNK_CANCEL;
    } /* if */

    iocb->flags |= TS_SDP_IOCB_F_CANCEL;
    result = -EAGAIN;

    goto unlock;
  } /* if */
  /*
   * no IOCB found.
   */
  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
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
} /* _tsSdpInetReadIocbCancel */

/* ========================================================================= */
/*..tsSdpInetRead -- recv AIO data from the network to userspace. */
tINT32 tsSdpInetRead
(
 struct socket *sock,
 kvec_cb_t      cb,
 size_t         size
)
{
  struct kiocb   *req;
  struct kvec_dst dst;
  struct sock    *sk;
  tSDP_CONN       conn;
  tSDP_IOCB       iocb;
  tINT32          copied = 0;
  tINT32          result = 0;
  tINT32          expect;

  TS_CHECK_NULL(sock,       -EINVAL);
  TS_CHECK_NULL(sock->sk,   -EINVAL);
  TS_CHECK_NULL(cb.fn,      -EINVAL);
  TS_CHECK_NULL(cb.data,    -EINVAL);
  TS_CHECK_NULL(cb.vec,     -EINVAL);
  TS_CHECK_EXPR((0 < size), -EINVAL);

  sk   = sock->sk;
  conn = TS_SDP_GET_CONN(sk);
  req  = (struct kiocb *)cb.data;

#ifdef _TS_SDP_DATA_PATH_DEBUG
  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: Read <%08x:%04x> <%08x:%04x> <%04x> size <%d>",
	   conn->src_addr, conn->src_port, conn->dst_addr, conn->dst_port,
	   conn->istate, size);
  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "SOCK: Read Kvec <%d:%d> veclet <%d:%d> fn <%08x> kiocb "
	   "<%d:%d:%08x>",
	   cb.vec->max_nr, cb.vec->nr,
	   cb.vec->veclet->offset, cb.vec->veclet->length,
	   (tUINT32)cb.fn, req->key, req->users, (tUINT32)req->data);
#endif
  /*
   * initialize memory destination
   */
  kvec_dst_init(&dst, KM_USER0);
  /*
   * set target destination address to cb vector
   */
  kvec_dst_set(&dst, cb.vec->veclet);
  /*
   * lock socket
   */
  TS_SDP_CONN_LOCK(conn);
  /*
   * sanity check the connection
   */
  if (TS_SDP_SOCK_ST_LISTEN == conn->istate ||
      TS_SDP_SOCK_ST_CLOSED == conn->istate) {

    result = -ENOTCONN;
    goto error;
  } /* if */
  /*
   * copy data that's already buffered.
   */
  copied = _tsSdpBuffKvecReadFlush(sk, &dst, size);
  if (0 > copied) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "READ: error <%d> flushing data <%d> to dst <%d:%d:%d>",
	     copied, size, dst.space, dst.offset, dst.type);
    result = copied;
    goto error;
  } /* if */
  /*
   * check errors, and in those cases fire CB even if the region
   * is not full
   */
  if (0 != TS_SDP_OS_CONN_GET_ERR(conn)) {

    result = TS_SDP_CONN_ERROR(conn);
    goto error;
  } /* if */

  if (0 < (TS_SDP_SHUTDOWN_RECV & conn->shutdown)) {

    result = 0;
    goto error;
  } /* if */

  if (TS_SDP_SOCK_ST_ERROR == conn->istate) {
    /*
     * conn->error should always be set, but just in case
     */
    result = -ENOTCONN;
    goto error;
  } /* if */
  /*
   * if there was no error and all less data then the recv low water
   * mark was moved, save the kvec in an IOCB and pass it to the
   * SDP Conn.
   */
  if (sk->rcvlowat > copied) {
    /*
     * more data, save the cb for processing as data arrives.
     */
    iocb = tsSdpConnIocbCreate();
    if (NULL == iocb) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "READ: Failed to allocate iocb to save cb. <%d:%d>",
	       size, copied);
      result = -ENOMEM;
      goto error; /* synchronous error if no data copied */
    } /* if */

    iocb->req      = req;
    iocb->kvec.dst = dst;
    iocb->cb       = cb;
    iocb->len      = size - copied;
    iocb->post     = copied;
    iocb->size     = size;
    iocb->key      = req->key;

    TS_SDP_CONN_STAT_RQ_INC(conn, iocb->size);

    result = tsSdpConnIocbTablePutTail(&conn->r_pend, iocb);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "READ: Error <%d> while queueing IOCB. <%d:%d>",
	       result, size, copied);

      (void)tsSdpConnIocbDestroy(iocb);
      goto error; /* synchronous error if no data copied */
    } /* if */

    req->data   = sk;
    req->cancel = _tsSdpInetReadIocbCancel;
    result = 0;
  } /* if */
  else {

#if 1 /* performance cheat. LFM */
    if (!(conn->snk_zthresh > size)) {

      conn->nond_recv--;

      result = tsSdpSendCtrlSnkAvail(conn, 0, 0, 0);
      if (0 > result) {
	/*
	 * since the message did not go out, back out the non_discard
	 * counter
	 */
	conn->nond_recv++;
      } /* if */
    } /* if */
#endif

    TS_SDP_CONN_STAT_READ_INC(conn, copied);
    result = copied;
  } /* else */
  /*
   * refresh consumed buffers, and/or consume zcopy advertisment
   */
  expect = tsSdpRecvPost(conn);
  if (0 > expect) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "RECV: device buffer ack <%d> error. <%d>", copied, expect);
  } /* if */


  TS_SDP_CONN_UNLOCK(conn);
  return result;
error:
  TS_SDP_CONN_UNLOCK(conn);
  /*
   * zero error needs to generate a completion so it's not
   * mistaken as an AIO in progress.
   */
  result = (0 < copied) ? copied : result;

  if (0 == result) {

    cb.fn(cb.data, cb.vec, 0);
  } /* if */

  return result;
} /* tsSdpInetRead */
#endif
/***************************************************************************
*                       Modification History
*
*    $Log: sdp_recv.c,v $
*    Revision 1.48  2004/02/24 23:48:51  roland
*    Add open source license headers
*
*    Revision 1.47  2004/02/24 17:24:31  roland
*    Change #include <sdp_xxx> to #include "sdp_xxx" so we can build under drivers/infiniband in 2.6 kernel
*
*    Revision 1.46  2004/02/24 08:27:13  libor
*    Initial Linux kernel 2.6 support
*
*    Revision 1.45  2003/12/18 01:20:05  libor
*    Poll CQs to improve performance.
*
*    Revision 1.44  2003/12/12 02:38:37  libor
*    Initial pass at TS_API NG
*
*    Revision 1.43  2003/11/05 23:54:48  johannes
*    64-bit portability fixes
*    kvec_read and kvec_write are prototyped with size_t which is variable length
*    depending on the architecture
*    Remove some warnings about casting from pointers to integers of different
*    sizes (safe since it's always a 32 bit integer store in a pointer)
*
*    Revision 1.42  2003/10/24 04:47:42  libor
*    expanded RHEL 3.0 AIO support
*
*    Revision 1.41  2003/10/14 06:37:36  libor
*    kernel IOCB should not have data reference count decremented
*    when the field is empty.
*    Bugzilla number: 2690
*
*    Revision 1.40  2003/10/03 17:22:22  libor
*    Receiving data after a close should result in an abortive
*    close when emulating TCP according to RFC 1122. (page 88)
*
*    Revision 1.39  2003/08/22 00:42:26  libor
*    Posted RDMA Reads would disappear if posted before the QP was in
*    Ready To Send (SDP ESTABLISHED) state. Bugzilla number: 2391
*
*    Revision 1.38  2003/08/20 01:57:29  libor
*     RedHat Enterprise Linux 3.0 support changes
*
*    Revision 1.37  2003/07/31 01:44:48  libor
*    verbose data path fix
*
*    Revision 1.36  2003/07/12 00:56:27  libor
*    Cancel bug workaround/fix (bug 2169)
*
*    Revision 1.35  2003/07/03 20:51:52  libor
*    cast needs to be a signed integer
*
*    Revision 1.34  2003/07/02 03:30:40  roland
*    Use a cast to (tUINT32) so that comparison is between two values of
*    the same type.
*
*    Revision 1.33  2003/07/02 02:04:07  libor
*    check lock before going into wait.
*
*    Revision 1.32  2003/07/02 01:36:53  libor
*    recv buffer posting algorithm fix. (bug2072)
*
*    Revision 1.31  2003/06/25 00:27:36  libor
*    urgent data recv with AIO fix.
*
*    Revision 1.30  2003/06/16 21:36:11  libor
*    reductions in lock aquisition to imporce performance.
*
*    Revision 1.29  2003/06/13 00:27:45  libor
*    OOB/URG data fixes for sockets and AIO.
*
*    Revision 1.28  2003/06/12 03:22:04  libor
*    OOB handling and some performance improvments.
*
*    Revision 1.27  2003/05/30 19:06:26  libor
*    bug fix for error reporting for AIO connection failure
*
*    Revision 1.26  2003/05/29 01:05:43  libor
*    limit sink advertisments
*
*    Revision 1.25  2003/05/28 18:03:29  libor
*    some bug fixes
*
*    Revision 1.24  2003/05/07 19:51:11  libor
*    socket/TCP compatability fixes and Oracle bug.
*
*    Revision 1.23  2003/05/02 20:44:29  libor
*    SDP support for multiple HCAs
*
*    Revision 1.22  2003/04/30 18:24:07  libor
*    third pass at some simplifications for porting
*
*    Revision 1.21  2003/04/29 17:34:56  libor
*    second pass at some simplifications for porting
*
*    Revision 1.20  2003/04/28 20:33:15  libor
*    first pass at some simplifications for porting
*
*    Revision 1.19  2003/04/16 20:06:55  roland
*    64-bit portability cleanups
*
*    Revision 1.18  2003/04/16 16:11:49  libor
*    RDMA read completion simplification.
*
*    Revision 1.17  2003/04/15 01:45:48  libor
*    RDMA write for some buffered sends.
*
*    Revision 1.16  2003/03/06 17:20:31  libor
*    move to per connection wrids
*
*    Revision 1.15  2003/02/26 02:34:14  libor
*    compile out stats by default
*
*    Revision 1.14  2003/02/18 23:13:16  libor
*    new locking
*
*    Revision 1.13  2003/02/11 02:19:42  libor
*    connection reference bug, plus partial source cancel
*
*    Revision 1.12  2003/02/10 21:57:06  libor
*    Sink Available cancellation
*
*    Revision 1.11  2003/02/08 02:09:05  libor
*    write cancellation changes.
*
*    Revision 1.10  2003/02/07 02:00:43  libor
*    sink advertisment tunning.
*
*    Revision 1.9  2003/02/06 04:57:34  libor
*    SDP Write ZCOPY for AIO.
*
*    Revision 1.8  2003/01/22 02:45:30  libor
*    limit rdma/send queue depths
*
*    Revision 1.7  2003/01/20 20:43:42  libor
*    Move event notification from thread to IRQ
*
*    Revision 1.6  2002/12/20 02:12:26  libor
*    AIO cancellation crashes and hangs resolved.
*
*    Revision 1.5  2002/12/10 01:44:47  libor
*    AIO cancel of active reads.
*
*    Revision 1.4  2002/12/06 05:33:23  libor
*    use FMR cache for zcopy read/writes
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
