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

  $Id: sdp_kvec.c 32 2004-04-09 03:57:42Z roland $
*/

#include "sdp_main.h"

/* ------------------------------------------------------------------- */
/*                                                                     */
/* static kvec internal functions                                      */
/*                                                                     */
/* ------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsSdpBuffKvecCancelLookupFunc -- lookup function for cancelation */
static tINT32 _tsSdpBuffKvecCancelLookupFunc
(
 tSDP_GENERIC element,
 tPTR         arg
)
{
  TS_CHECK_NULL(element, -EINVAL);

  return ((TS_SDP_GENERIC_TYPE_IOCB == element->type) ? 0 : -ERANGE);
} /* _tsSdpBuffKvecCancelLookupFunc */
/* ------------------------------------------------------------------- */
/*                                                                     */
/* Public write functions                                              */
/*                                                                     */
/* ------------------------------------------------------------------- */

/* ------------------------------------------------------------------- */
/*                                                                     */
/* Cancel operations                                                   */
/*                                                                     */
/* ------------------------------------------------------------------- */

/* ========================================================================= */
/*.._tsSdpBuffKvecReadCancelPending -- cancel all pending read AIOs */
static tINT32 _tsSdpBuffKvecReadCancelPending
(
 tSDP_CONN conn,
 tINT32    error
)
{
  TS_CHECK_NULL(conn, -EINVAL);

  return tsSdpConnIocbTableCancel(&conn->r_pend, TS_SDP_IOCB_F_ALL, error);
} /* _tsSdpBuffKvecReadCancelPending */

/* ========================================================================= */
/*.._tsSdpBuffKvecReadCancelSource -- cancel all pending read AIOs */
static tINT32 _tsSdpBuffKvecReadCancelSource
(
 tSDP_CONN conn,
 tINT32    error
)
{
  tSDP_IOCB iocb;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);

  while (NULL != (iocb = (tSDP_IOCB)tsSdpGenericTableLookup(&conn->r_src,
 					       _tsSdpBuffKvecCancelLookupFunc,
							    NULL))) {

    result = tsSdpConnIocbTableRemove(iocb);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));

    result = tsSdpConnIocbComplete(iocb, error);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  } /* while */

  return 0;
} /* _tsSdpBuffKvecReadCancelSource */

/* ========================================================================= */
/*.._tsSdpBuffKvecReadCancelSink -- cancel all pending read AIOs */
static tINT32 _tsSdpBuffKvecReadCancelSink
(
 tSDP_CONN conn,
 tINT32    error
)
{
  TS_CHECK_NULL(conn, -EINVAL);

  return tsSdpConnIocbTableCancel(&conn->r_snk, TS_SDP_IOCB_F_ALL, error);
} /* _tsSdpBuffKvecReadCancelSink */

/* ========================================================================= */
/*.._tsSdpBuffKvecWriteCancelPending -- cancel all pending read AIOs */
static tINT32 _tsSdpBuffKvecWriteCancelPending
(
 tSDP_CONN conn,
 tINT32    error
)
{
  tSDP_IOCB iocb;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);

  while (NULL != (iocb = (tSDP_IOCB)tsSdpGenericTableLookup(&conn->send_queue,
 					       _tsSdpBuffKvecCancelLookupFunc,
							    NULL))) {

    result = tsSdpConnIocbTableRemove(iocb);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));

    result = tsSdpConnIocbComplete(iocb, error);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  } /* while */

  return 0;
} /* _tsSdpBuffKvecWriteCancelPending */

/* ========================================================================= */
/*.._tsSdpBuffKvecWriteCancelSource -- cancel all pending source AIOs */
static tINT32 _tsSdpBuffKvecWriteCancelSource
(
 tSDP_CONN conn,
 tINT32    error
)
{
  TS_CHECK_NULL(conn, -EINVAL);

  return tsSdpConnIocbTableCancel(&conn->w_src, TS_SDP_IOCB_F_ALL, error);
} /* _tsSdpBuffKvecWriteCancelSource */

/* ========================================================================= */
/*.._tsSdpBuffKvecWriteCancelSink -- cancel all pending sink AIOs */
static tINT32 _tsSdpBuffKvecWriteCancelSink
(
 tSDP_CONN conn,
 tINT32    error
)
{
  tSDP_IOCB iocb;
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);

  while (NULL != (iocb = (tSDP_IOCB)tsSdpGenericTableLookup(&conn->w_snk,
 					       _tsSdpBuffKvecCancelLookupFunc,
							    NULL))) {

    result = tsSdpConnIocbTableRemove(iocb);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));

    result = tsSdpConnIocbComplete(iocb, error);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  } /* while */

  return 0;
} /* _tsSdpBuffKvecWriteCancelSink */

/* ========================================================================= */
/*..tsSdpBuffKvecReadCancelAll -- cancel all outstanding read AIOs */
tINT32 tsSdpBuffKvecReadCancelAll
(
 tSDP_CONN conn,
 tINT32    error
)
{
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);

  result = _tsSdpBuffKvecReadCancelPending(conn, error);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  result = _tsSdpBuffKvecReadCancelSink(conn, error);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  result = _tsSdpBuffKvecReadCancelSource(conn, error);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  return 0;
} /* tsSdpBuffKvecReadCancelAll */

/* ========================================================================= */
/*..tsSdpBuffKvecWriteCancelAll -- cancel all outstanding write AIOs */
tINT32 tsSdpBuffKvecWriteCancelAll
(
 tSDP_CONN conn,
 tINT32    error
)
{
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);

  result = _tsSdpBuffKvecWriteCancelPending(conn, error);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  result = _tsSdpBuffKvecWriteCancelSource(conn, error);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  result = _tsSdpBuffKvecWriteCancelSink(conn, error);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  return 0;
} /* tsSdpBuffKvecWriteCancelAll */

/* ========================================================================= */
/*..tsSdpBuffKvecCancelAll -- cancel all outstanding AIOs */
tINT32 tsSdpBuffKvecCancelAll
(
 tSDP_CONN conn,
 tINT32    error
)
{
  tINT32 result;

  TS_CHECK_NULL(conn, -EINVAL);

  result = tsSdpBuffKvecReadCancelAll(conn, error);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  result = tsSdpBuffKvecWriteCancelAll(conn, error);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  return 0;
} /* tsSdpBuffKvecCancelAll */
