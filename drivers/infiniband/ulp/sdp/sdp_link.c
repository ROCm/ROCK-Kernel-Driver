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

  $Id: sdp_link.c 32 2004-04-09 03:57:42Z roland $
*/

#include "sdp_main.h"

static tTS_KERNEL_TIMER_STRUCT           _tsSdpPathTimer;
static tSDP_PATH_LOOKUP_ID               _tsSdpPathLookupId = 0;
static tTS_IB_ASYNCHRONOUS_EVENT_HANDLER _tsSdpAsyncHandle  = NULL;
static tSDP_LINK_ROOT_STRUCT             _tsSdpLinkRoot     = {
  wait_list:  NULL,
  path_list:  NULL,
  wait_lock:  SPIN_LOCK_UNLOCKED,
  path_lock:  SPIN_LOCK_UNLOCKED,
  wait_cache: NULL,
  path_cache: NULL
};

static struct tq_struct _arp_completion;

#define TS_SDP_PATH_LOOKUP_ID() \
      ((TS_SDP_PATH_LOOKUP_INVALID == ++_tsSdpPathLookupId) ? \
       ++_tsSdpPathLookupId : _tsSdpPathLookupId)

/* --------------------------------------------------------------------- */
/*                                                                       */
/* Path Record lookup caching                                            */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsSdpPathElementLookup -- lookup a path record entry */
static tSDP_PATH_ELEMENT _tsSdpPathElementLookup
(
 tUINT32 ip_addr
)
{
  tSDP_PATH_ELEMENT path_elmt;

  for (path_elmt = _tsSdpLinkRoot.path_list;
       NULL != path_elmt;
       path_elmt = path_elmt->next) {

    if (ip_addr == path_elmt->dst_addr) {

      break;
    } /* if */
  } /* for */

  return path_elmt;
} /* _tsSdpPathElementLookup */

/* ========================================================================= */
/*.._tsSdpPathElementCreate -- create an entry for a path record element */
static tINT32 _tsSdpPathElementCreate
(
 tUINT32              dst_addr,
 tUINT32              src_addr,
 tTS_IB_PORT          hw_port,
 tTS_IB_DEVICE_HANDLE ca,
 tTS_IB_PATH_RECORD path_r,
 tSDP_PATH_ELEMENT  *return_elmt
)
{
  tSDP_PATH_ELEMENT path_elmt;

  TS_CHECK_NULL(path_r, -EINVAL);
  TS_CHECK_NULL(return_elmt, -EINVAL);
  TS_CHECK_NULL(_tsSdpLinkRoot.path_cache, -EINVAL);

  path_elmt = kmem_cache_alloc(_tsSdpLinkRoot.path_cache, SLAB_ATOMIC);
  if (NULL == path_elmt) {

    return -ENOMEM;
  } /* if */

  memset(path_elmt, 0, sizeof(tSDP_PATH_ELEMENT_STRUCT));

  path_elmt->next      = _tsSdpLinkRoot.path_list;
  _tsSdpLinkRoot.path_list = path_elmt;
  path_elmt->p_next    = &_tsSdpLinkRoot.path_list;

  if (NULL != path_elmt->next) {

    path_elmt->next->p_next = &path_elmt->next;
  } /* if */
  /*
   * set values
   */
  path_elmt->dst_addr = dst_addr;
  path_elmt->src_addr = src_addr;
  path_elmt->hw_port  = hw_port;
  path_elmt->ca       = ca;
  path_elmt->usage    = jiffies;
  memcpy(&path_elmt->path_s, path_r, sizeof(tTS_IB_PATH_RECORD_STRUCT));

  *return_elmt = path_elmt;

  return 0;
} /* _tsSdpPathElementCreate */

/* ========================================================================= */
/*.._tsSdpPathElementDestroy -- destroy an entry for a path record element */
static tINT32 _tsSdpPathElementDestroy
(
 tSDP_PATH_ELEMENT path_elmt
)
{
  TS_CHECK_NULL(path_elmt, -EINVAL);
  TS_CHECK_NULL(_tsSdpLinkRoot.path_cache, -EINVAL);

  if (NULL != path_elmt->p_next) {

    if (NULL != path_elmt->next) {
      path_elmt->next->p_next = path_elmt->p_next;
    } /* if */

    *(path_elmt->p_next) = path_elmt->next;

    path_elmt->p_next = NULL;
    path_elmt->next   = NULL;
  } /* if */

  kmem_cache_free(_tsSdpLinkRoot.path_cache, path_elmt);

  return 0;
} /* _tsSdpPathElementDestroy */

/* ========================================================================= */
/*.._tsSdpPathLookupComplete -- complete the resolution of a path record */
static tINT32 _tsSdpPathLookupComplete
(
 tSDP_PATH_LOOKUP_ID   plid,
 tINT32                status,
 tSDP_PATH_ELEMENT     path_elmt,
 tSDP_PATH_LOOKUP_FUNC func,
 tPTR                  arg
)
{
  TS_CHECK_NULL(func, -EINVAL);

  if (NULL != path_elmt) {

    return func(plid,
		status,
		path_elmt->src_addr,
		path_elmt->dst_addr,
		path_elmt->hw_port,
		path_elmt->ca,
		&path_elmt->path_s,
		arg);
  } /* if */
  else {

    return func(plid, status, 0, 0, 0, TS_IB_HANDLE_INVALID, NULL, arg);
  } /* else */
} /* _tsSdpPathLookupComplete */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* module specific functions                                             */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsSdpIpoibWaitDestroy -- destroy an entry for an outstanding request */
static tINT32 _tsSdpIpoibWaitDestroy
(
 tSDP_IPOIB_WAIT ipoib_wait
)
{
  TS_CHECK_NULL(ipoib_wait, -EINVAL);
  TS_CHECK_NULL(_tsSdpLinkRoot.wait_cache, -EINVAL);

  if (NULL != ipoib_wait->p_next) {

    if (NULL != ipoib_wait->next) {
      ipoib_wait->next->p_next = ipoib_wait->p_next;
    } /* if */

    *(ipoib_wait->p_next) = ipoib_wait->next;

    ipoib_wait->p_next = NULL;
    ipoib_wait->next   = NULL;
  } /* if */

  kmem_cache_free(_tsSdpLinkRoot.wait_cache, ipoib_wait);

  return 0;
} /* _tsSdpIpoibWaitDestroy */

/* ========================================================================= */
/*.._tsSdpIpoibWaitTimeout -- timeout function for link resolution */
static void _tsSdpIpoibWaitTimeout
(
 tPTR arg
)
{
  tSDP_IPOIB_WAIT ipoib_wait = (tSDP_IPOIB_WAIT)arg;
  tINT32 result;

  if (NULL == ipoib_wait) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "TIME: Empty timeout for address resolution");
    return;
  } /* if */

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "TIME: Timout for address <%08x> resolution. retries <%d>",
	   ipoib_wait->dst_addr, ipoib_wait->retry);

  TS_SDP_IPOIB_FLAG_CLR_TIME(ipoib_wait);

  if (0 < TS_SDP_IPOIB_FLAGS_EMPTY(ipoib_wait)) {

    result = _tsSdpIpoibWaitDestroy(ipoib_wait);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));

    return;
  } /* if */

  ipoib_wait->retry--;
  if (0 < ipoib_wait->retry) {
    /*
     * rearm the timer (check for neighbour nud status?)
     */
    ipoib_wait->timer.run_time = jiffies + TS_SDP_IPOIB_RETRY_INTERVAL;
    ipoib_wait->timer.function = _tsSdpIpoibWaitTimeout;
    ipoib_wait->timer.arg      = ipoib_wait;
    tsKernelTimerAdd(&ipoib_wait->timer);

    TS_SDP_IPOIB_FLAG_SET_TIME(ipoib_wait);
    /*
     * resend the ARP
     */
    arp_send(ARPOP_REQUEST,
	     ETH_P_ARP,
	     ipoib_wait->gw_addr,
	     ipoib_wait->dev,
	     ipoib_wait->src_addr,
	     NULL,
	     ipoib_wait->dev->dev_addr,
	     NULL);
  } /* if */
  else {

    result = _tsSdpPathLookupComplete(ipoib_wait->plid,
				      -EHOSTUNREACH,
				      NULL,
				      ipoib_wait->func,
				      ipoib_wait->arg);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	       "FUNC: Error <%d> timing out address resolution. <%08x>",
	       result, ipoib_wait->dst_addr);
    } /* if */

    TS_SDP_IPOIB_FLAG_CLR_FUNC(ipoib_wait);

    result = _tsSdpIpoibWaitDestroy(ipoib_wait);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  } /* else */

  return;
} /* _tsSdpIpoibWaitTimeout */

/* ========================================================================= */
/*.._tsSdpIpoibWaitCreate -- create an entry for an outstanding request */
static tSDP_IPOIB_WAIT _tsSdpIpoibWaitCreate
(
 tSDP_PATH_LOOKUP_ID   plid,
 tUINT32               dst_addr,
 tUINT32               src_addr,
 tUINT8                localroute,
 tINT32                bound_dev_if,
 tSDP_PATH_LOOKUP_FUNC func,
 tPTR                  arg
)
{
  tSDP_IPOIB_WAIT ipoib_wait;

  TS_CHECK_NULL(_tsSdpLinkRoot.wait_cache, NULL);

  ipoib_wait = kmem_cache_alloc(_tsSdpLinkRoot.wait_cache, SLAB_ATOMIC);
  if (NULL != ipoib_wait) {

    memset(ipoib_wait, 0, sizeof(tSDP_IPOIB_WAIT_STRUCT));

    tsKernelTimerInit(&ipoib_wait->timer);

    ipoib_wait->dst_addr  = dst_addr;
    ipoib_wait->src_addr  = src_addr;
    ipoib_wait->local_rt  = localroute;
    ipoib_wait->bound_dev = bound_dev_if;
    ipoib_wait->gw_addr   = 0;
    ipoib_wait->arg       = arg;
    ipoib_wait->func      = func;
    ipoib_wait->plid      = plid;
    ipoib_wait->dev       = 0;
    ipoib_wait->retry     = TS_SDP_IPOIB_RETRY_VALUE;
    ipoib_wait->tid       = TS_IB_CLIENT_QUERY_TID_INVALID;
    ipoib_wait->hw_port   = 0;
    ipoib_wait->ca        = TS_IB_HANDLE_INVALID;

    ipoib_wait->timer.run_time = jiffies + TS_SDP_IPOIB_RETRY_INTERVAL;
    ipoib_wait->timer.function = _tsSdpIpoibWaitTimeout;
    ipoib_wait->timer.arg      = ipoib_wait;
    /*
     * flags
     */
    TS_SDP_IPOIB_FLAG_SET_FUNC(ipoib_wait);
  } /* if */

  return ipoib_wait;
} /* _tsSdpIpoibWaitCreate */

/* ========================================================================= */
/*.._tsSdpIpoibWaitListInsert -- insert an entry into the wait list */
static tINT32 _tsSdpIpoibWaitListInsert
(
 tSDP_IPOIB_WAIT ipoib_wait
)
{
  TS_CHECK_NULL(ipoib_wait, -EINVAL);

  if (NULL != ipoib_wait->next ||
      NULL != ipoib_wait->p_next) {

    return -EFAULT;
  } /* if */

  ipoib_wait->next    = _tsSdpLinkRoot.wait_list;
  _tsSdpLinkRoot.wait_list = ipoib_wait;
  ipoib_wait->p_next  = &_tsSdpLinkRoot.wait_list;

  if (NULL != ipoib_wait->next) {

    ipoib_wait->next->p_next = &ipoib_wait->next;
  } /* if */

  TS_SDP_IPOIB_FLAG_SET_TIME(ipoib_wait);
  tsKernelTimerAdd(&ipoib_wait->timer);

  return 0;
} /* _tsSdpIpoibWaitListInsert */

/* ========================================================================= */
/*.._tsSdpIpoibWaitPlidLookup -- lookup an entry for an outstanding request */
static tSDP_IPOIB_WAIT tsSdpIpoibWaitPlidLookup
(
 tSDP_PATH_LOOKUP_ID plid
)
{
  tSDP_IPOIB_WAIT ipoib_wait;

  for (ipoib_wait = _tsSdpLinkRoot.wait_list;
       NULL != ipoib_wait;
       ipoib_wait = ipoib_wait->next) {

    if (plid == ipoib_wait->plid) {

      break;
    } /* if */
  } /* for */

  return ipoib_wait;
} /* _tsSdpIpoibWaitPlidLookup */

#if 0
/* ========================================================================= */
/*.._tsSdpIpoibWaitIpLookup -- lookup an entry for an outstanding request */
static tSDP_IPOIB_WAIT _tsSdpIpoibWaitIpLookup
(
 tUINT32 ip_addr
)
{
  tSDP_IPOIB_WAIT ipoib_wait;

  for (ipoib_wait = _tsSdpLinkRoot.wait_list;
       NULL != ipoib_wait;
       ipoib_wait = ipoib_wait->next) {

    if (ip_addr == ipoib_wait->dst_addr) {

      break;
    } /* if */
  } /* for */

  return ipoib_wait;
} /* _tsSdpIpoibWaitIpLookup */

/* ========================================================================= */
/*.._tsSdpIpoibWaitIpGet -- get an entry for an outstanding request */
static tSDP_IPOIB_WAIT _tsSdpIpoibWaitIpGet
(
 tUINT32 ip_addr
)
{
  tSDP_IPOIB_WAIT ipoib_wait;

  TS_CHECK_NULL(_tsSdpLinkRoot.wait_cache, NULL);
  /*
   * find entry
   */
  for (ipoib_wait = _tsSdpLinkRoot.wait_list;
       NULL != ipoib_wait;
       ipoib_wait = ipoib_wait->next) {

    if (ip_addr == ipoib_wait->dst_addr) {

      break;
    } /* if */
  } /* for */
  /*
   * remove entry
   */
  if (NULL != ipoib_wait) {

    if (NULL != ipoib_wait->next) {
      ipoib_wait->next->p_next = ipoib_wait->p_next;
    } /* if */

    *(ipoib_wait->p_next) = ipoib_wait->next;

    ipoib_wait->p_next = NULL;
    ipoib_wait->next   = NULL;
  } /* if */

  return ipoib_wait;
} /* _tsSdpIpoibWaitDestroy */
#endif
/* ========================================================================= */
/*..tsSdpPathElementTableDump - dump the path record element table to proc */
tINT32 tsSdpPathElementTableDump
(
 tSTR   buffer,
 tINT32 max_size,
 tINT32 start_index,
 long *end_index
)
{
  tSDP_PATH_ELEMENT path_elmt;
  tINT32 counter = 0;
  tINT32 offset = 0;

  TS_CHECK_NULL(buffer, -EINVAL);
  TS_CHECK_NULL(end_index, -EINVAL);

  *end_index = 0;
  /*
   * header should only be printed once
   */
  if (0 == start_index) {

    offset += sprintf((buffer + offset),
		      "dst ip addr  subnet prefix   destination guid dlid "
		      "slid pkey mtu    hca    pt last use\n");
    offset += sprintf((buffer + offset),
		      "----------- ---------------- ---------------- ---- "
		      "---- ---- ---- -------- -- --------\n");
  } /* if */
  /*
   * loop across connections.
   */
  for (path_elmt = _tsSdpLinkRoot.path_list, counter = 0;
       NULL != path_elmt &&
	 !(TS_SDP_PATH_PROC_DUMP_SIZE > (max_size - offset));
       path_elmt = path_elmt->next, counter++) {

    if (!(start_index > counter)) {

      offset += sprintf((buffer + offset),
			"%02x.%02x.%02x.%02x %016llx %016llx %04x %04x %04x "
			"%04x %08x %02x %08x\n",
			(path_elmt->dst_addr         & 0xff),
			((path_elmt->dst_addr >> 8)  & 0xff),
			((path_elmt->dst_addr >> 16) & 0xff),
			((path_elmt->dst_addr >> 24) & 0xff),
			(unsigned long long)cpu_to_be64(*(tUINT64 *)path_elmt->path_s.dgid),
			(unsigned long long)cpu_to_be64(*(tUINT64 *)(path_elmt->path_s.dgid +
                                                                        sizeof(tUINT64))),
			path_elmt->path_s.dlid,
			path_elmt->path_s.slid,
			path_elmt->path_s.pkey,
			path_elmt->path_s.mtu,
			path_elmt->ca,
			path_elmt->hw_port,
			path_elmt->usage);
    } /* if */
  } /* for */

  if (!(start_index > counter)) {

    *end_index = counter - start_index;
  } /* if */

  return offset;
} /* tsSdpPathElementTableDump */

/* ========================================================================= */
/*..tsSdpIpoibWaitTableDump - dump the address resolution wait table to proc */
tINT32 tsSdpIpoibWaitTableDump
(
 tSTR   buffer,
 tINT32 max_size,
 tINT32 start_index,
 long *end_index
)
{
  tSDP_IPOIB_WAIT ipoib_wait;
  tINT32 counter = 0;
  tINT32 offset = 0;

  TS_CHECK_NULL(buffer, -EINVAL);
  TS_CHECK_NULL(end_index, -EINVAL);

  *end_index = 0;
  /*
   * header should only be printed once
   */
  if (0 == start_index) {

    offset += sprintf((buffer + offset),
		      "ip  address gw  address rt fg\n");
    offset += sprintf((buffer + offset),
		      "----------- ----------- -- --\n");
  } /* if */
  /*
   * loop across connections.
   */
  for (ipoib_wait = _tsSdpLinkRoot.wait_list, counter = 0;
       NULL != ipoib_wait &&
	 !(TS_SDP_IPOIB_PROC_DUMP_SIZE > (max_size - offset));
       ipoib_wait = ipoib_wait->next, counter++) {

    if (!(start_index > counter)) {

      offset += sprintf((buffer + offset),
			"%02x.%02x.%02x.%02x %02x.%02x.%02x.%02x %02x %02x\n",
			(ipoib_wait->dst_addr         & 0xff),
			((ipoib_wait->dst_addr >> 8)  & 0xff),
			((ipoib_wait->dst_addr >> 16) & 0xff),
			((ipoib_wait->dst_addr >> 24) & 0xff),
			(ipoib_wait->gw_addr         & 0xff),
			((ipoib_wait->gw_addr >> 8)  & 0xff),
			((ipoib_wait->gw_addr >> 16) & 0xff),
			((ipoib_wait->gw_addr >> 24) & 0xff),
			ipoib_wait->retry,
			ipoib_wait->flags);
    } /* if */
  } /* for */

  if (!(start_index > counter)) {

    *end_index = counter - start_index;
  } /* if */

  return offset;
} /* tsSdpIpoibWaitTableDump */
/* --------------------------------------------------------------------- */
/*                                                                       */
/* Path record completion                                                */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsSdpPathRecordComplete -- path lookup complete, save result */
static tINT32 _tsSdpPathRecordComplete
(
 tTS_IB_CLIENT_QUERY_TID  tid,
 tINT32                   status,
 tTS_IB_PATH_RECORD       path,
 tINT32                   remaining,
 tPTR                     arg
)
{
  tSDP_IPOIB_WAIT ipoib_wait = (tSDP_IPOIB_WAIT)arg;
  tSDP_PATH_ELEMENT path_elmt = NULL;
  tINT32 result;

  TS_CHECK_NULL(ipoib_wait, -EINVAL);
  TS_CHECK_NULL(path, -EINVAL);

  if (tid != ipoib_wait->tid) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "PATH: result TID mismatch. <%016llx:%016llx>",
	     tid, ipoib_wait->tid);
    return -EFAULT;
  } /* if */
  /*
   * path lookup is complete
   */
  if (0 != remaining) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: multi-part <%d> result unsupported.", remaining);
  } /* if */

  ipoib_wait->tid = TS_IB_CLIENT_QUERY_TID_INVALID;
  /*
   * Save result.
   */
  switch (status) {
  case -ETIMEDOUT:

    if (0 < ipoib_wait->retry--) {
      /*
       * reinitiate path record resolution
       */
      result = tsIbPathRecordRequest(ipoib_wait->ca,
                                     ipoib_wait->hw_port,
				     ipoib_wait->src_gid,
				     ipoib_wait->dst_gid,
				     TS_IB_PATH_RECORD_FORCE_REMOTE,
				     TS_SDP_DEV_PATH_WAIT,
				     0,
				     _tsSdpPathRecordComplete,
				     ipoib_wait,
				     &ipoib_wait->tid);
      if (0 != result) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
		 "FUNC: Error initiating path record request. <%d>", result);
	status = result;
	goto callback;
      } /* if */
    } /* if */
    else {
      /*
       * no more retries, failure.
       */
      goto callback;
    } /* else */

    break;
  case 0:

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "POST: Path record lookup complete. <%016llx:%016llx:%d>",
	     cpu_to_be64(*(tUINT64 *)path->dgid),
	     cpu_to_be64(*(tUINT64 *)(path->dgid + sizeof(tUINT64))),
	     path->dlid);

    result = _tsSdpPathElementCreate(ipoib_wait->dst_addr,
				     ipoib_wait->src_addr,
				     ipoib_wait->hw_port,
				     ipoib_wait->ca,
				     path,
				     &path_elmt);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "POST: Error <%d> creating path element.", result);
      status = result;
    } /* if */

    goto callback;
  default:

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Error <%d> in path record completion.", status);

    goto callback;
  } /* switch */

  return 0;
callback:

  if (0 < TS_SDP_IPOIB_FLAG_GET_FUNC(ipoib_wait)) {

    result = _tsSdpPathLookupComplete(ipoib_wait->plid,
				      status,
				      path_elmt,
				      ipoib_wait->func,
				      ipoib_wait->arg);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "PATH: Error <%d> completing Path Record Lookup.", result);
    } /* if */

    TS_SDP_IPOIB_FLAG_CLR_FUNC(ipoib_wait);
  } /* if */

  if (0 < TS_SDP_IPOIB_FLAGS_EMPTY(ipoib_wait)) {

    result = _tsSdpIpoibWaitDestroy(ipoib_wait);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  } /* if */

  return 0;
} /* _tsSdpPostPathComplete */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* Address resolution                                                    */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsSdpLinkFindComplete -- complete the resolution of an ip address  */
static tINT32 _tsSdpLinkFindComplete
(
 tSDP_IPOIB_WAIT ipoib_wait,
 tINT32          status
)
{
  tINT32 result = 0;
  tINT32 expect;

  TS_CHECK_NULL(ipoib_wait, -EINVAL);

  if (0 < TS_SDP_IPOIB_FLAGS_EMPTY(ipoib_wait)) {
    /*
     * nothing is waiting for
     */
    status = -EFAULT;
    goto done;
  } /* if */

  if (0 != status) {

    goto done;
  } /* if */
  /*
   * lookup real address
   */
  result = tsIpoibDeviceArpGetGid(ipoib_wait->dev,
				  ipoib_wait->hw,
				  ipoib_wait->dst_gid);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "FUNC: Error <%d> on ARP cache lookup.", result);
    status = -ENOENT;
    goto done;
  } /* if */
  /*
   * reset retry counter
   */
  ipoib_wait->retry = TS_SDP_IPOIB_RETRY_VALUE;
  /*
   * initiate path record resolution
   */
  result = tsIbPathRecordRequest(tsIbDefaultDeviceGet(), /* XXX */
                                 ipoib_wait->hw_port,
				 ipoib_wait->src_gid,
				 ipoib_wait->dst_gid,
				 TS_IB_PATH_RECORD_FORCE_REMOTE,
				 TS_SDP_DEV_PATH_WAIT,
				 0,
				 _tsSdpPathRecordComplete,
				 ipoib_wait,
				 &ipoib_wait->tid);
  if (0 != result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "FUNC: Error initiating path record request. <%d>", result);
    status = result;
    goto done;
  } /* if */

  return 0;
done:
  if (0 < TS_SDP_IPOIB_FLAG_GET_FUNC(ipoib_wait)) {

    result = _tsSdpPathLookupComplete(ipoib_wait->plid,
				      status,
				      NULL,
				      ipoib_wait->func,
				      ipoib_wait->arg);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	       "FUNC: Error <%d> completing address resolution. <%d:%08x>",
	       result, status, ipoib_wait->dst_addr);
    } /* if */

    TS_SDP_IPOIB_FLAG_CLR_FUNC(ipoib_wait);
  } /* if */

  if (0 < TS_SDP_IPOIB_FLAGS_EMPTY(ipoib_wait)) {

    expect = _tsSdpIpoibWaitDestroy(ipoib_wait);
    TS_EXPECT(MOD_LNX_SDP, !(0 > expect));
  } /* if */

  return 0;
} /*_tsSdpLinkFindComplete  */

/* ========================================================================= */
/*.._tsSdpLinkFind -- resolve an ip address to a ipoib link address. */
static tINT32 _tsSdpLinkFind
(
 tSDP_IPOIB_WAIT ipoib_wait
)
{
  tINT32 result;
  tINT32 counter;
  struct rtable *rt;

  TS_CHECK_NULL(ipoib_wait, -EINVAL);

  result = ip_route_connect(&rt,
			    ipoib_wait->dst_addr,
			    ipoib_wait->src_addr,
			    ipoib_wait->local_rt,
			    ipoib_wait->bound_dev);
  if (0 > result ||
      NULL == rt) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "FIND: Error <%d> routing <%08x> from <%08x> (%d:%d)",
	     result, ipoib_wait->dst_addr, ipoib_wait->src_addr,
	     ipoib_wait->local_rt, ipoib_wait->bound_dev);
    return result;
  } /* if */
  /*
   * check route flags
   */
  if (0 < ((RTCF_MULTICAST|RTCF_BROADCAST) & rt->rt_flags)) {

    ip_rt_put(rt);
    return -ENETUNREACH;
  } /* if */
  /*
   * check that device is IPoIB
   */
  if (NULL == rt->u.dst.neighbour ||
      NULL == rt->u.dst.neighbour->dev) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "FIND: No neighbour found for <%08x:%08x>",
	     rt->rt_src, rt->rt_dst);

    result = -EINVAL;
    goto error;
  } /* if */

  if (0 > TS_SDP_IPOIB_DEV_TOPSPIN(rt->u.dst.neighbour->dev) &&
      0 == (IFF_LOOPBACK & rt->u.dst.neighbour->dev->flags)) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "FIND: Destination or neighbour device is not IPoIB. <%s:%08x>",
	     rt->u.dst.neighbour->dev->name, rt->u.dst.neighbour->dev->flags);

    result = -ENETUNREACH;
    goto error;
  } /* if */

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	   "FIND: Found neighbour. <%08x:%08x:%08x> nud state <%02x>",
	   rt->rt_src, rt->rt_dst, rt->rt_gateway,
	   rt->u.dst.neighbour->nud_state);

  ipoib_wait->gw_addr       = rt->rt_gateway;
  ipoib_wait->src_addr      = rt->rt_src;
  /*
   * device needs to be a valid IB device. Check for loopback.
   */
  ipoib_wait->dev = ((0 < (IFF_LOOPBACK & rt->u.dst.neighbour->dev->flags)) ?
		     ip_dev_find(rt->rt_src) : rt->u.dst.neighbour->dev);

  if (0 < (IFF_LOOPBACK & ipoib_wait->dev->flags)) {

    TS_REPORT_WARN(MOD_LNX_SDP,
                   "dev->flags 0x%lx means loopback",
                   ipoib_wait->dev->flags);

    counter = 0;
    while (NULL != (ipoib_wait->dev = dev_get_by_index(++counter))) {

      dev_put(ipoib_wait->dev);
      if (0 == TS_SDP_IPOIB_DEV_TOPSPIN(ipoib_wait->dev) &&
	  0 < (IFF_UP & ipoib_wait->dev->flags)) {

	break;
      } /* if */
    } /* while */
  } /* if */
  /*
   * Verify device.
   */
  if (NULL == ipoib_wait->dev) {

    TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "FIND: No device for IB communications <%s:%08x:%08x>",
	     rt->u.dst.neighbour->dev->name,
	     rt->u.dst.neighbour->dev->flags,
	     rt->rt_src);
    result = -EINVAL;
    goto error;
  } /* if */
  /*
   * lookup local information
   */

  result = tsIpoibDeviceHandle(ipoib_wait->dev,
			       &ipoib_wait->ca,
			       &ipoib_wait->hw_port,
			       ipoib_wait->src_gid);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "FUNC: Error <%d> looking up local device information.", result);
    goto error;
  } /* if */

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "FIND: hca <%04x> for port <%02x> gidp <%p>",
	   ipoib_wait->ca, ipoib_wait->hw_port, ipoib_wait->src_gid);
  /*
   * HA is invalid and ARP is required if we're no in one of these
   * states.
   */
  if (0 == ((NUD_CONNECTED|NUD_DELAY|NUD_PROBE) &
	    rt->u.dst.neighbour->nud_state)) {
    /*
     * Incomplete entry is an ARP in progress, otherwise start and ARP,
     * but create a wait entry.
     */
    result = _tsSdpIpoibWaitListInsert(ipoib_wait);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	       "FIND: Error <%d> inserting wait for address resolution.",
	       result);
      goto error;
    } /* if */

    if (0 == ((NUD_INCOMPLETE) & rt->u.dst.neighbour->nud_state)) {

      arp_send(ARPOP_REQUEST,
	       ETH_P_ARP,
	       rt->rt_gateway,
	       rt->u.dst.neighbour->dev,
	       ipoib_wait->src_addr,
	       NULL,
	       rt->u.dst.neighbour->dev->dev_addr,
	       NULL);
    } /* if */
  } /* if */
  else {
    /*
     * if this is a loopback connection, find the local source interface
     * and get the associated HW address.
     */
    if (0 < (IFF_LOOPBACK & rt->u.dst.neighbour->dev->flags)) {

      memcpy((char *)ipoib_wait->hw,
	     (char *)ipoib_wait->dev->dev_addr,
	     sizeof(ipoib_wait->hw));
    } /* if */
    else {

      memcpy(ipoib_wait->hw, rt->u.dst.neighbour->ha, sizeof(ipoib_wait->hw));
    } /* if */

    result = _tsSdpLinkFindComplete(ipoib_wait, 0);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	       "FIND: Error <%d> completing address lookup. <%08x:%08x>",
	       result, ipoib_wait->src_addr, ipoib_wait->dst_addr);
      goto error;
    } /* if */
  } /* else */

  return 0;
error:
  return result;

} /* _tsSdpLinkFind */
/* --------------------------------------------------------------------- */
/*                                                                       */
/* Arp packet reception for completions                                  */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsSdpArpRecvComplete -- receive all ARP packets. */
static void _tsSdpArpRecvComplete
(
 tPTR arg
)
{
  tSDP_IPOIB_WAIT ipoib_wait;
  tSDP_IPOIB_WAIT next_wait;
  tUINT32 ip_addr = (unsigned long) arg;
  tINT32 result;

  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "RECV: Arp completion for <%08x>.", ip_addr);

  ipoib_wait = _tsSdpLinkRoot.wait_list;
  while (NULL != ipoib_wait) {

    next_wait = ipoib_wait->next;

    if (ip_addr == ipoib_wait->gw_addr) {

      TS_SDP_IPOIB_FLAG_CLR_TASK(ipoib_wait);

      result = _tsSdpLinkFindComplete(ipoib_wait, 0);
      if (0 > result) {

	TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
		 "FIND: Error <%d> completing address lookup. <%08x>",
		 result, ipoib_wait->dst_addr);

	result = _tsSdpIpoibWaitDestroy(ipoib_wait);
	TS_EXPECT(MOD_LNX_SDP, !(0 > result));
      } /* if */
    } /* if */

    ipoib_wait = next_wait;
  } /* while */

  return;
} /* _tsSdpArpRecvComplete */

/* ========================================================================= */
/*.._tsSdpArpRecv -- receive all ARP packets. */
static tINT32 _tsSdpArpRecv
(
 struct sk_buff *skb,
 struct net_device *dev,
 struct packet_type *pt
)
{
  tSDP_IPOIB_WAIT ipoib_wait;
  tSDP_IPOIB_ARP arp_hdr;
  tINT32 counter;

  TS_CHECK_NULL(dev, -EINVAL);
  TS_CHECK_NULL(skb, -EINVAL);
  TS_CHECK_NULL(skb->nh.raw, -EINVAL);

  arp_hdr = (tSDP_IPOIB_ARP)skb->nh.raw;

#if 0
  TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "RECV: Arp packet <%04x> <%08x:%08x> from device.",
	   arp_hdr->cmd, arp_hdr->src_ip, arp_hdr->dst_ip);
#endif
  /*
   * Remeber, this function is in the bottom half!
   */
  if (0 > TS_SDP_IPOIB_DEV_TOPSPIN(dev) ||
      (arp_hdr->cmd != __constant_htons(ARPOP_REPLY) &&
       arp_hdr->cmd != __constant_htons(ARPOP_REQUEST))) {

    goto done;
  } /* if */
  /*
   * determine if anyone is waiting for this ARP response.
   */
  for (counter = 0, ipoib_wait = _tsSdpLinkRoot.wait_list;
       NULL != ipoib_wait;
       ipoib_wait = ipoib_wait->next) {

    if (arp_hdr->src_ip == ipoib_wait->gw_addr) {
      /*
       * remove timer, before scheduling the task.
       */
      tsKernelTimerRemove(&ipoib_wait->timer);
      /*
       * save results
       */
      memcpy(ipoib_wait->hw, arp_hdr->src_hw, sizeof(ipoib_wait->hw));
      /*
       * flags
       */
      TS_SDP_IPOIB_FLAG_SET_TASK(ipoib_wait);
      TS_SDP_IPOIB_FLAG_CLR_TIME(ipoib_wait);

      counter++;
    } /* if */
  } /* for */
  /*
   * Schedule the ARP completion.
   */
  if (0 < counter) {

    _arp_completion.routine = _tsSdpArpRecvComplete;
    _arp_completion.data    = (tPTR) (unsigned long) arp_hdr->src_ip;

    schedule_task(&_arp_completion);
  } /* if */

done:
  kfree_skb(skb);
  return 0;
} /* _tsSdpArpRecv */

/* ========================================================================= */
/*.._tsSdpAsyncEventFunc -- IB async event handler, for clearing caches */
static void _tsSdpAsyncEventFunc
(
 tTS_IB_ASYNCHRONOUS_EVENT_RECORD record,
 tPTR arg
)
{
  tSDP_PATH_ELEMENT path_elmt;
  tINT32 result;

  if (NULL == record) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "ASYNC: Event with no record of what happened?");
    return;
  } /* if */

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	   "ASYNC: Event <%d> reported, clearing cache.");
  /*
   * destroy all cached path record elements.
   */
  while (NULL != (path_elmt = _tsSdpLinkRoot.path_list)) {

    result = _tsSdpPathElementDestroy(path_elmt);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  } /* while */

  return;
} /* _tsSdpAsyncEventFunc */

/* ========================================================================= */
/*.._tsSdpPathSweepTimerFunc --sweep path cache to reap old entries. */
static void _tsSdpPathSweepTimerFunc
(
 tPTR arg
)
{
  tSDP_PATH_ELEMENT path_elmt;
  tSDP_PATH_ELEMENT next_elmt;
  tINT32 result;
  /*
   * arg entry is unused.
   */
  path_elmt = _tsSdpLinkRoot.path_list;

  while (NULL != path_elmt) {

    next_elmt = path_elmt->next;

    if (!(TS_SDP_PATH_REAPING_AGE > (tINT32)(jiffies - path_elmt->usage))) {

      TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	       "PATH: Deleting old <%u:%u> path entry <%08x>.",
	       path_elmt->usage, jiffies, htonl(path_elmt->dst_addr));

      result = _tsSdpPathElementDestroy(path_elmt);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));
    } /* if */

    path_elmt = next_elmt;
  } /* while */
  /*
   * rearm timer.
   */
  _tsSdpPathTimer.run_time = jiffies + TS_SDP_PATH_TIMER_INTERVAL;
  tsKernelTimerAdd(&_tsSdpPathTimer);

  return;
} /* _tsSdpPathSweepTimerFunc */
/* --------------------------------------------------------------------- */
/*                                                                       */
/* Path record lookup functions                                          */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpPathRecordLookup -- resolve an ip address to a path record */
tINT32 tsSdpPathRecordLookup
(
 tUINT32               dst_addr,      /* NBO */
 tUINT32               src_addr,      /* NBO */
 tUINT8                localroute,
 tINT32                bound_dev_if,
 tSDP_PATH_LOOKUP_FUNC func,
 tPTR                  arg,
 tSDP_PATH_LOOKUP_ID  *plid
)
{
  tSDP_PATH_ELEMENT path_elmt;
  tSDP_IPOIB_WAIT ipoib_wait;
  tINT32 result = 0;
  tINT32 expect;

  TS_CHECK_NULL(plid, -EINVAL);
  TS_CHECK_NULL(func, -EINVAL);
  /*
   * new plid
   */
  *plid = TS_SDP_PATH_LOOKUP_ID();
  /*
   * perform a lookup to see if a path element structure exists.
   */
  path_elmt = _tsSdpPathElementLookup(dst_addr);
  if (NULL != path_elmt) {
    /*
     * update last used time.
     */
    path_elmt->usage = jiffies;

    result = _tsSdpPathLookupComplete(*plid, 0, path_elmt, func, arg);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "PATH: Error <%d> completing Path Record Lookup.", result);
      goto error;
    } /* if */
  } /* if */
  else {

    ipoib_wait = _tsSdpIpoibWaitCreate(*plid,
				       dst_addr,
				       src_addr,
				       localroute,
				       bound_dev_if,
				       func,
				       arg);
    if (NULL == ipoib_wait) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "PATH: Error creating address resolution wait object");
      result = -ENOMEM;
      goto error;
    } /* if */

    result = _tsSdpLinkFind(ipoib_wait);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "PATH: Error <%d> starting address resolution.", result);

      expect = _tsSdpIpoibWaitDestroy(ipoib_wait);
      TS_EXPECT(MOD_LNX_SDP, !(0 > expect));

      goto error;
    } /* if */
  } /* else */

  return 0;
error:
  return result;
} /* tsSdpPathRecordLookup */

/* ========================================================================= */
/*..tsSdpPathRecordCancel -- cancel a lookup for an address. */
tINT32 tsSdpPathRecordCancel
(
 tSDP_PATH_LOOKUP_ID plid
)
{
  tSDP_IPOIB_WAIT ipoib_wait;
  tINT32 result;

  if (TS_SDP_PATH_LOOKUP_INVALID == plid) {

    return -ERANGE;
  } /* if */

  ipoib_wait = tsSdpIpoibWaitPlidLookup(plid);
  if (NULL == ipoib_wait) {

    return -ENOENT;
  } /* if */
  /*
   * check tid
   */
  if (TS_IB_CLIENT_QUERY_TID_INVALID != ipoib_wait->tid) {

    (void)tsIbClientQueryCancel(ipoib_wait->tid);
    ipoib_wait->tid = TS_IB_CLIENT_QUERY_TID_INVALID;
  } /* if */
  /*
   * remove timer
   */
  tsKernelTimerRemove(&ipoib_wait->timer);

  TS_SDP_IPOIB_FLAG_CLR_FUNC(ipoib_wait);
  TS_SDP_IPOIB_FLAG_CLR_TIME(ipoib_wait);

  if (0 < TS_SDP_IPOIB_FLAGS_EMPTY(ipoib_wait)) {

    result = _tsSdpIpoibWaitDestroy(ipoib_wait);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  } /* if */

  return 0;
} /* tsSdpPathRecordCancel */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* primary initialization/cleanup functions                              */
/*                                                                       */
/* --------------------------------------------------------------------- */
static struct packet_type _sdp_arp_type = {
  type:   __constant_htons(ETH_P_ARP),
  func:   _tsSdpArpRecv,
  data:   (void*) 1, /* understand shared skbs */
};

/* ========================================================================= */
/*..tsSdpLinkAddrInit -- initialize the advertisment caches. */
tINT32 tsSdpLinkAddrInit
(
 void
)
{
  tINT32 result = 0;

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "INIT: Link level services initialization.");

  if (NULL != _tsSdpLinkRoot.wait_cache) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "INIT: Wait cache is already initialized!");

    result = -EINVAL;
    goto error;
  } /* if */
  /*
   * create cache
   */
  _tsSdpLinkRoot.wait_cache = kmem_cache_create("SdpIpoibWait",
					    sizeof(tSDP_IPOIB_WAIT_STRUCT),
					    0, SLAB_HWCACHE_ALIGN,
					    NULL, NULL);
  if (NULL == _tsSdpLinkRoot.wait_cache) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "INIT: Failed to create wait cache.");

    result = -ENOMEM;
    goto error_wait;
  } /* if */

  _tsSdpLinkRoot.path_cache = kmem_cache_create("SdpPathLookup",
					    sizeof(tSDP_PATH_ELEMENT_STRUCT),
					    0, SLAB_HWCACHE_ALIGN,
					    NULL, NULL);
  if (NULL == _tsSdpLinkRoot.path_cache) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "INIT: Failed to create path lookup cache.");

    result = -ENOMEM;
    goto error_path;
  } /* if */
  /*
   * Install async event handler, to clear cache on port down
   */
  result = tsIbAsynchronousEventHandlerRegister(TS_IB_PORT_ERROR,
						_tsSdpAsyncEventFunc,
						NULL,
						&_tsSdpAsyncHandle);
  if (0 != result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "INIT: Error <%d> registering event handler.", result);
    goto error_async;
  } /* if */
  /*
   * create timer for pruning path record cache.
   */
  tsKernelTimerInit(&_tsSdpPathTimer);
  _tsSdpPathTimer.run_time = jiffies + TS_SDP_PATH_TIMER_INTERVAL;
  _tsSdpPathTimer.function = _tsSdpPathSweepTimerFunc;
  _tsSdpPathTimer.arg      = NULL;
  tsKernelTimerAdd(&_tsSdpPathTimer);
  /*
   * install device for receiving ARP packets in parallel to the normal
   * Linux ARP, this will be the SDP notifier that an ARP request has
   * completed.
   */
  dev_add_pack(&_sdp_arp_type);

  return 0;
error_async:
  kmem_cache_destroy(_tsSdpLinkRoot.path_cache);
error_path:
  kmem_cache_destroy(_tsSdpLinkRoot.wait_cache);
error_wait:
error:
  return result;
} /* tsSdpLinkAddrInit */

/* ========================================================================= */
/*..tsSdpLinkAddrCleanup -- cleanup the advertisment caches. */
tINT32 tsSdpLinkAddrCleanup
(
 void
)
{
  tSDP_PATH_ELEMENT path_elmt;
  tSDP_IPOIB_WAIT ipoib_wait;
  tUINT32 result;

  TS_CHECK_NULL(_tsSdpLinkRoot.wait_cache, -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "INIT: Link level services cleanup.");
  /*
   * stop cache pruning timer
   */
  tsKernelTimerRemove(&_tsSdpPathTimer);
  /*
   * remove ARP packet processing.
   */
  dev_remove_pack(&_sdp_arp_type);
  /*
   * release async event handler
   */
  (void)tsIbAsynchronousEventHandlerUnregister(_tsSdpAsyncHandle);
  /*
   * clear wait list
   */
  while (NULL != (ipoib_wait = _tsSdpLinkRoot.wait_list)) {

    result = _tsSdpIpoibWaitDestroy(ipoib_wait);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  } /* while */

  while (NULL != (path_elmt = _tsSdpLinkRoot.path_list)) {

    result = _tsSdpPathElementDestroy(path_elmt);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  } /* while */
  /*
   * delete cache
   */
  kmem_cache_destroy(_tsSdpLinkRoot.wait_cache);
  kmem_cache_destroy(_tsSdpLinkRoot.path_cache);

  return 0;
} /* tsSdpLinkAddrCleanup */
