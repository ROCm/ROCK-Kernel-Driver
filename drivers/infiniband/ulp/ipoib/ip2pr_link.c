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

  $Id: ip2pr_link.c 32 2004-04-09 03:57:42Z roland $
*/

#include "ip2pr_priv.h"

#include <asm/byteorder.h>

static tTS_KERNEL_TIMER_STRUCT           _tsIp2prPathTimer;
static tIP2PR_PATH_LOOKUP_ID               _tsIp2prPathLookupId = 0;
static tTS_IB_ASYNC_EVENT_HANDLER_HANDLE _tsIp2prAsyncErrHandle[IP2PR_MAX_HCAS];
static tTS_IB_ASYNC_EVENT_HANDLER_HANDLE _tsIp2prAsyncActHandle[IP2PR_MAX_HCAS];

#ifdef TS_KERNEL_2_6
#define tq_struct work_struct
#define INIT_TQUEUE(x,y,z) INIT_WORK(x,y,z)
#define schedule_task schedule_work
#else
#define af_packet_priv data
#endif

static unsigned int ip2pr_total_req = 0;
static unsigned int ip2pr_arp_timeout = 0;
static unsigned int ip2pr_path_timeout = 0;
static unsigned int ip2pr_total_fail = 0;

static tIP2PR_LINK_ROOT_STRUCT             _tsIp2prLinkRoot     = {
  wait_list:  NULL,
  path_list:  NULL,
  wait_lock:  SPIN_LOCK_UNLOCKED,
  path_lock:  SPIN_LOCK_UNLOCKED,
  wait_cache: NULL,
  path_cache: NULL,
  src_gid_list:  NULL,
  src_gid_cache: NULL,
  gid_lock:   SPIN_LOCK_UNLOCKED
};

tINT32 _tsIp2PrnDelete
(
 tIP2PR_GID_PR_ELEMENT pr_elmt
);

static tTS_IB_GID nullgid = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

static struct tq_struct _arp_completion;

#define TS_IP2PR_PATH_LOOKUP_ID() \
      ((TS_IP2PR_PATH_LOOKUP_INVALID == ++_tsIp2prPathLookupId) ? \
       ++_tsIp2prPathLookupId : _tsIp2prPathLookupId)


/* --------------------------------------------------------------------- */
/*                                                                       */
/* Path Record lookup caching                                            */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsIp2prPathElementLookup -- lookup a path record entry */
static tIP2PR_PATH_ELEMENT _tsIp2prPathElementLookup
(
 tUINT32 ip_addr
)
{
  tIP2PR_PATH_ELEMENT path_elmt;

  for (path_elmt = _tsIp2prLinkRoot.path_list;
       NULL != path_elmt;
       path_elmt = path_elmt->next) {

    if (ip_addr == path_elmt->dst_addr) {

      break;
    } /* if */
  } /* for */

  return path_elmt;
} /* _tsIp2prPathElementLookup */

/* ========================================================================= */
/*.._tsIp2prPathElementCreate -- create an entry for a path record element */
static tINT32 _tsIp2prPathElementCreate
(
 tUINT32              dst_addr,
 tUINT32              src_addr,
 tTS_IB_PORT          hw_port,
 tTS_IB_DEVICE_HANDLE ca,
 tTS_IB_PATH_RECORD path_r,
 tIP2PR_PATH_ELEMENT  *return_elmt
)
{
  tIP2PR_PATH_ELEMENT path_elmt;
  unsigned long       flags;

  TS_CHECK_NULL(path_r, -EINVAL);
  TS_CHECK_NULL(return_elmt, -EINVAL);
  TS_CHECK_NULL(_tsIp2prLinkRoot.path_cache, -EINVAL);

  path_elmt = kmem_cache_alloc(_tsIp2prLinkRoot.path_cache, SLAB_ATOMIC);
  if (NULL == path_elmt) {

    return -ENOMEM;
  } /* if */

  memset(path_elmt, 0, sizeof(tIP2PR_PATH_ELEMENT_STRUCT));

  spin_lock_irqsave(&_tsIp2prLinkRoot.path_lock, flags);
  path_elmt->next      = _tsIp2prLinkRoot.path_list;
  _tsIp2prLinkRoot.path_list = path_elmt;
  path_elmt->p_next    = &_tsIp2prLinkRoot.path_list;

  if (NULL != path_elmt->next) {

    path_elmt->next->p_next = &path_elmt->next;
  } /* if */
  spin_unlock_irqrestore(&_tsIp2prLinkRoot.path_lock, flags);
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
} /* _tsIp2prPathElementCreate */

/* ========================================================================= */
/*.._tsIp2prPathElementDestroy -- destroy an entry for a path record element */
static tINT32 _tsIp2prPathElementDestroy
(
 tIP2PR_PATH_ELEMENT path_elmt
)
{
  unsigned long flags;

  TS_CHECK_NULL(path_elmt, -EINVAL);
  TS_CHECK_NULL(_tsIp2prLinkRoot.path_cache, -EINVAL);

  spin_lock_irqsave(&_tsIp2prLinkRoot.path_lock, flags);
  if (NULL != path_elmt->p_next) {

    if (NULL != path_elmt->next) {
      path_elmt->next->p_next = path_elmt->p_next;
    } /* if */

    *(path_elmt->p_next) = path_elmt->next;

    path_elmt->p_next = NULL;
    path_elmt->next   = NULL;
  } /* if */
  spin_unlock_irqrestore(&_tsIp2prLinkRoot.path_lock, flags);

  kmem_cache_free(_tsIp2prLinkRoot.path_cache, path_elmt);

  return 0;
} /* _tsIp2prPathElementDestroy */

/* ========================================================================= */
/*.._tsIp2prPathLookupComplete -- complete the resolution of a path record */
static tINT32 _tsIp2prPathLookupComplete
(
 tIP2PR_PATH_LOOKUP_ID plid,
 tINT32                status,
 tIP2PR_PATH_ELEMENT   path_elmt,
 tPTR                  funcptr,
 tPTR                  arg
)
{
  tIP2PR_PATH_LOOKUP_FUNC   func = (tIP2PR_PATH_LOOKUP_FUNC)funcptr;
  TS_CHECK_NULL(func, -EINVAL);

  if (status != 0)
    ip2pr_total_fail++;
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
} /* _tsIp2prPathLookupComplete */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* module specific functions                                             */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsIp2prIpoibWaitDestroy -- destroy an entry for an outstanding request */
static tINT32 _tsIp2prIpoibWaitDestroy
(
 tIP2PR_IPOIB_WAIT ipoib_wait,
 IP2PR_USE_LOCK	use_lock
)
{
  unsigned long flags = 0;

  TS_CHECK_NULL(ipoib_wait, -EINVAL);
  TS_CHECK_NULL(_tsIp2prLinkRoot.wait_cache, -EINVAL);

  if (use_lock)
  	spin_lock_irqsave(&_tsIp2prLinkRoot.wait_lock, flags);
  if (NULL != ipoib_wait->p_next) {

    if (NULL != ipoib_wait->next) {
      ipoib_wait->next->p_next = ipoib_wait->p_next;
    } /* if */

    *(ipoib_wait->p_next) = ipoib_wait->next;

    ipoib_wait->p_next = NULL;
    ipoib_wait->next   = NULL;
  } /* if */
  if (use_lock)
  	spin_unlock_irqrestore(&_tsIp2prLinkRoot.wait_lock, flags);

  kmem_cache_free(_tsIp2prLinkRoot.wait_cache, ipoib_wait);

  return 0;
} /* _tsIp2prIpoibWaitDestroy */

/* ========================================================================= */
/*.._tsIp2prIpoibWaitTimeout -- timeout function for link resolution */
static void _tsIp2prIpoibWaitTimeout
(
 tPTR arg
)
{
  tIP2PR_IPOIB_WAIT ipoib_wait = (tIP2PR_IPOIB_WAIT)arg;
  tINT32 result;

  if (NULL == ipoib_wait) {

    TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	     "TIME: Empty timeout for address resolution");
    return;
  } /* if */

  TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "TIME: Timout for address <%08x> resolution. retries <%d>",
	   ipoib_wait->dst_addr, ipoib_wait->retry);

  TS_IP2PR_IPOIB_FLAG_CLR_TIME(ipoib_wait);

  if (0 < TS_IP2PR_IPOIB_FLAGS_EMPTY(ipoib_wait)) {

    result = _tsIp2prIpoibWaitDestroy(ipoib_wait, IP2PR_LOCK_NOT_HELD);
    TS_EXPECT(MOD_IP2PR, !(0 > result));

    return;
  } /* if */

  ipoib_wait->retry--;
  if (0 < ipoib_wait->retry) {
    /*
     * rearm the timer (check for neighbour nud status?)
     */
    ipoib_wait->prev_timeout = (ipoib_wait->prev_timeout * 2); /* backoff */
    if (ipoib_wait->prev_timeout > _tsIp2prLinkRoot.backoff)
      ipoib_wait->prev_timeout = _tsIp2prLinkRoot.backoff;
    ipoib_wait->timer.run_time = jiffies + (ipoib_wait->prev_timeout * HZ) +
                                               (jiffies & 0x0f);
    ipoib_wait->timer.function = _tsIp2prIpoibWaitTimeout;
    ipoib_wait->timer.arg      = ipoib_wait;
    tsKernelTimerAdd(&ipoib_wait->timer);

    TS_IP2PR_IPOIB_FLAG_SET_TIME(ipoib_wait);

    /*
     * resend the ARP
     */
    ip2pr_arp_timeout++;
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

    result = _tsIp2prPathLookupComplete(ipoib_wait->plid,
				      -EHOSTUNREACH,
				      NULL,
				      ipoib_wait->func,
				      ipoib_wait->arg);
    if (0 > result) {

      TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	       "FUNC: Error <%d> timing out address resolution. <%08x>",
	       result, ipoib_wait->dst_addr);
    } /* if */

    TS_IP2PR_IPOIB_FLAG_CLR_FUNC(ipoib_wait);

    result = _tsIp2prIpoibWaitDestroy(ipoib_wait, IP2PR_LOCK_NOT_HELD);
    TS_EXPECT(MOD_IP2PR, !(0 > result));
  } /* else */

  return;
} /* _tsIp2prIpoibWaitTimeout */

/* ========================================================================= */
/*.._tsIp2prIpoibWaitCreate -- create an entry for an outstanding request */
static tIP2PR_IPOIB_WAIT _tsIp2prIpoibWaitCreate
(
 tIP2PR_PATH_LOOKUP_ID   plid,
 tUINT32               dst_addr,
 tUINT32               src_addr,
 tUINT8                localroute,
 tINT32                bound_dev_if,
 tIP2PR_PATH_LOOKUP_FUNC func,
 tPTR                  arg,
 tINT32                ltype
)
{
  tIP2PR_IPOIB_WAIT ipoib_wait;

  TS_CHECK_NULL(_tsIp2prLinkRoot.wait_cache, NULL);

  ipoib_wait = kmem_cache_alloc(_tsIp2prLinkRoot.wait_cache, SLAB_ATOMIC);
  if (NULL != ipoib_wait) {

    memset(ipoib_wait, 0, sizeof(tIP2PR_IPOIB_WAIT_STRUCT));

    /*
     * start timer only for IP to PR lookups
     */
    if (LOOKUP_IP2PR == ltype) {
      tsKernelTimerInit(&ipoib_wait->timer);
      ipoib_wait->timer.run_time = jiffies +
                                       (_tsIp2prLinkRoot.retry_timeout * HZ);
      ipoib_wait->timer.function = _tsIp2prIpoibWaitTimeout;
      ipoib_wait->timer.arg      = ipoib_wait;
    }
    ipoib_wait->type = ltype;
    ipoib_wait->dst_addr  = dst_addr;
    ipoib_wait->src_addr  = src_addr;
    ipoib_wait->local_rt  = localroute;
    ipoib_wait->bound_dev = bound_dev_if;
    ipoib_wait->gw_addr   = 0;
    ipoib_wait->arg       = arg;
    ipoib_wait->func      = (tPTR)func;
    ipoib_wait->plid      = plid;
    ipoib_wait->dev       = 0;
    ipoib_wait->retry     = _tsIp2prLinkRoot.max_retries;
    ipoib_wait->prev_timeout = _tsIp2prLinkRoot.retry_timeout;
    ipoib_wait->tid       = TS_IB_CLIENT_QUERY_TID_INVALID;
    ipoib_wait->hw_port   = 0;
    ipoib_wait->ca        = TS_IB_HANDLE_INVALID;
    ipoib_wait->state     = IP2PR_STATE_ARP_WAIT;
    ipoib_wait->lock	  = SPIN_LOCK_UNLOCKED;

    /*
     * flags
     */
    TS_IP2PR_IPOIB_FLAG_SET_FUNC(ipoib_wait);
  } /* if */

  return ipoib_wait;
} /* _tsIp2prIpoibWaitCreate */

/* ========================================================================= */
/*.._tsIp2prIpoibWaitListInsert -- insert an entry into the wait list */
static tINT32 _tsIp2prIpoibWaitListInsert
(
 tIP2PR_IPOIB_WAIT ipoib_wait
)
{
  unsigned long flags;

  TS_CHECK_NULL(ipoib_wait, -EINVAL);

  if (NULL != ipoib_wait->next ||
      NULL != ipoib_wait->p_next) {

    return -EFAULT;
  } /* if */
  spin_lock_irqsave(&_tsIp2prLinkRoot.wait_lock, flags);

  ipoib_wait->next    = _tsIp2prLinkRoot.wait_list;
  _tsIp2prLinkRoot.wait_list = ipoib_wait;
  ipoib_wait->p_next  = &_tsIp2prLinkRoot.wait_list;

  if (NULL != ipoib_wait->next) {

    ipoib_wait->next->p_next = &ipoib_wait->next;
  } /* if */
  spin_unlock_irqrestore(&_tsIp2prLinkRoot.wait_lock, flags);

  /*
   * Start timer only for IP 2 PR lookup
   */
  TS_IP2PR_IPOIB_FLAG_SET_TIME(ipoib_wait);
  if (LOOKUP_IP2PR == ipoib_wait->type) {
    ipoib_wait->timer.run_time = jiffies + (1 * HZ) + (jiffies & 0x0f);
    tsKernelTimerAdd(&ipoib_wait->timer);
  }

  return 0;
} /* _tsIp2prIpoibWaitListInsert */

/* ========================================================================= */
/*.._tsIp2prIpoibWaitPlidLookup -- lookup an entry for an outstanding request */
static tIP2PR_IPOIB_WAIT tsIp2prIpoibWaitPlidLookup
(
 tIP2PR_PATH_LOOKUP_ID plid
)
{
  unsigned long flags;
  tIP2PR_IPOIB_WAIT ipoib_wait;

  spin_lock_irqsave(&_tsIp2prLinkRoot.wait_lock, flags);
  for (ipoib_wait = _tsIp2prLinkRoot.wait_list;
       NULL != ipoib_wait;
       ipoib_wait = ipoib_wait->next) {

    if (plid == ipoib_wait->plid) {

      break;
    } /* if */
  } /* for */
  spin_unlock_irqrestore(&_tsIp2prLinkRoot.wait_lock, flags);

  return ipoib_wait;
} /* _tsIp2prIpoibWaitPlidLookup */

/* ========================================================================= */
/*..tsIp2prPathElementTableDump - dump the path record element table to proc */
tINT32 tsIp2prPathElementTableDump
(
 tSTR   buffer,
 tINT32 max_size,
 tINT32 start_index,
 long *end_index
)
{
  tIP2PR_PATH_ELEMENT path_elmt;
  tINT32 counter = 0;
  tINT32 offset = 0;
  unsigned long flags;

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
  spin_lock_irqsave(&_tsIp2prLinkRoot.path_lock, flags);
  for (path_elmt = _tsIp2prLinkRoot.path_list, counter = 0;
       NULL != path_elmt &&
	 !(TS_IP2PR_PATH_PROC_DUMP_SIZE > (max_size - offset));
       path_elmt = path_elmt->next, counter++) {

    if (!(start_index > counter)) {

      offset += sprintf((buffer + offset),
			"%02x.%02x.%02x.%02x %016llx %016llx %04x %04x %04x "
			"%04x %02x %08x\n",
			(path_elmt->dst_addr         & 0xff),
			((path_elmt->dst_addr >> 8)  & 0xff),
			((path_elmt->dst_addr >> 16) & 0xff),
			((path_elmt->dst_addr >> 24) & 0xff),
			(unsigned long long)be64_to_cpu(*(tUINT64 *)path_elmt->path_s.dgid),
			(unsigned long long)be64_to_cpu(*(tUINT64 *)(path_elmt->path_s.dgid +
                                                                        sizeof(tUINT64))),
			path_elmt->path_s.dlid,
			path_elmt->path_s.slid,
			path_elmt->path_s.pkey,
			path_elmt->path_s.mtu,
			path_elmt->hw_port,
			path_elmt->usage);
    } /* if */
  } /* for */
  spin_unlock_irqrestore(&_tsIp2prLinkRoot.path_lock, flags);

  if (!(start_index > counter)) {

    *end_index = counter - start_index;
  } /* if */

  return offset;
} /* tsIp2prPathElementTableDump */

/* ========================================================================= */
/*..tsIp2prIpoibWaitTableDump - dump the address resolution wait table to proc */
tINT32 tsIp2prIpoibWaitTableDump
(
 tSTR   buffer,
 tINT32 max_size,
 tINT32 start_index,
 long *end_index
)
{
  tIP2PR_IPOIB_WAIT ipoib_wait;
  tINT32 counter = 0;
  tINT32 offset = 0;
  unsigned long flags;

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
  spin_lock_irqsave(&_tsIp2prLinkRoot.wait_lock, flags);
  for (ipoib_wait = _tsIp2prLinkRoot.wait_list, counter = 0;
       NULL != ipoib_wait &&
	 !(TS_IP2PR_IPOIB_PROC_DUMP_SIZE > (max_size - offset));
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
  spin_unlock_irqrestore(&_tsIp2prLinkRoot.wait_lock, flags);

  if (!(start_index > counter)) {

    *end_index = counter - start_index;
  } /* if */

  return offset;
} /* tsIp2prIpoibWaitTableDump */

/* ..tsIp2prProcReadInt. dump integer value to /proc file */
tINT32  tsIp2prProcReadInt(
 tSTR   buffer,
 tINT32 max_size,
 tINT32 start_index,
 long *end_index,
 int  val
)
{
  tINT32 offset = 0;

  TS_CHECK_NULL(buffer, -EINVAL);
  TS_CHECK_NULL(end_index, -EINVAL);

  *end_index = -1;

  offset =  sprintf(buffer, "%d\n", val);

  return (offset);
}

/* ..tsIp2prProcMaxRetriesRead. dump current retry value */
tINT32 tsIp2prProcRetriesRead(
 tSTR   buffer,
 tINT32 max_size,
 tINT32 start_index,
 long *end_index
)
{

  return (tsIp2prProcReadInt(buffer,
                             max_size,
                             start_index,
                             end_index,
                             _tsIp2prLinkRoot.max_retries));
}

/* ..tsIp2prProcTimeoutRead. dump current timeout value */
tINT32 tsIp2prProcTimeoutRead(
 tSTR   buffer,
 tINT32 max_size,
 tINT32 start_index,
 long *end_index
)
{

  return (tsIp2prProcReadInt(buffer,
                             max_size,
                             start_index,
                             end_index,
                             _tsIp2prLinkRoot.retry_timeout));
}

/* ..tsIp2prProcBackoutRead. dump current backout value */
tINT32 tsIp2prProcBackoffRead(
 tSTR   buffer,
 tINT32 max_size,
 tINT32 start_index,
 long *end_index
)
{

  return (tsIp2prProcReadInt(buffer,
                             max_size,
                             start_index,
                             end_index,
                             _tsIp2prLinkRoot.backoff));
}

/* ..tsIp2prProcCacheTimeoutRead. dump current cache timeout value */
tINT32 tsIp2prProcCacheTimeoutRead(
 tSTR   buffer,
 tINT32 max_size,
 tINT32 start_index,
 long *end_index
)
{

  return (tsIp2prProcReadInt(buffer,
                             max_size,
                             start_index,
                             end_index,
                             _tsIp2prLinkRoot.cache_timeout));
}

/* ..tsIp2prProcMaxRetriesRead. dump current retry value */
tINT32 tsIp2prProcTotalReq(
 tSTR   buffer,
 tINT32 max_size,
 tINT32 start_index,
 long *end_index
)
{

  return (tsIp2prProcReadInt(buffer,
                             max_size,
                             start_index,
                             end_index,
                             ip2pr_total_req));
}

/* ..tsIp2prProcMaxRetriesRead. dump current retry value */
tINT32 tsIp2prProcArpTimeout(
 tSTR   buffer,
 tINT32 max_size,
 tINT32 start_index,
 long *end_index
)
{

  return (tsIp2prProcReadInt(buffer,
                             max_size,
                             start_index,
                             end_index,
                             ip2pr_arp_timeout));
}

/* ..tsIp2prProcMaxRetriesRead. dump current retry value */
tINT32 tsIp2prProcPathTimeout(
 tSTR   buffer,
 tINT32 max_size,
 tINT32 start_index,
 long *end_index
)
{

  return (tsIp2prProcReadInt(buffer,
                             max_size,
                             start_index,
                             end_index,
                             ip2pr_path_timeout));
}

/* ..tsIp2prProcMaxRetriesRead. dump current retry value */
tINT32 tsIp2prProcTotalFail(
 tSTR   buffer,
 tINT32 max_size,
 tINT32 start_index,
 long *end_index
)
{

  return (tsIp2prProcReadInt(buffer,
                             max_size,
                             start_index,
                             end_index,
                             ip2pr_total_fail));
}

/* ..tsIp2prProcWriteInt. scan integer value from /proc file */
ssize_t tsIp2prProcWriteInt(
  struct file *file,
  const char *buffer,
  size_t count,
  loff_t *pos,
  int    *val
)
{
  char kernel_buf[256];
  int ret;

  if (count > sizeof kernel_buf) {
    count = sizeof kernel_buf;
  }

  if (copy_from_user(kernel_buf, buffer, count)) {
    return -EFAULT;
  }

  ret = count;

  kernel_buf[count - 1] = '\0';

  sscanf(kernel_buf, "%d", val);

  return (ret);
}

/* ..tsIp2prProcMaxRetriesWrite. scan max retries value */
ssize_t tsIp2prProcRetriesWrite(
  struct file *file,
  const char *buffer,
  size_t count,
  loff_t *pos
)
{
  int val;
  int ret;

  ret =  tsIp2prProcWriteInt(file,
                              buffer,
                              count,
                              pos,
                              &val);
  if (val <= TS_IP2PR_PATH_MAX_RETRIES)
    _tsIp2prLinkRoot.max_retries = val;

  return (ret);
}

/* ..tsIp2prProcTimeoutWrite. scan timeout value */
ssize_t tsIp2prProcTimeoutWrite(
  struct file *file,
  const char *buffer,
  size_t count,
  loff_t *pos
)
{
  int val;
  int ret;

  ret =  tsIp2prProcWriteInt(file,
                              buffer,
                              count,
                              pos,
                              &val);
  if (val <= TS_IP2PR_MAX_DEV_PATH_WAIT)
    _tsIp2prLinkRoot.retry_timeout = val;

  return (ret);
}

/* ..tsIp2prProcBackoutWrite. scan backout value */
ssize_t tsIp2prProcBackoffWrite(
  struct file *file,
  const char *buffer,
  size_t count,
  loff_t *pos
)
{
  int val;
  int ret;

  ret =  tsIp2prProcWriteInt(file,
                              buffer,
                              count,
                              pos,
                              &val);
  if (val <= TS_IP2PR_PATH_MAX_BACKOFF)
    _tsIp2prLinkRoot.backoff = val;

  return (ret);
}

/* ..tsIp2prProcCacheTimeoutWrite. scan cache timeout value */
ssize_t tsIp2prProcCacheTimeoutWrite(
  struct file *file,
  const char *buffer,
  size_t count,
  loff_t *pos
)
{
  int val;
  int ret;

  ret =  tsIp2prProcWriteInt(file,
                              buffer,
                              count,
                              pos,
                              &val);
  if (val <= TS_IP2PR_PATH_MAX_CACHE_TIMEOUT)
    _tsIp2prLinkRoot.cache_timeout = val;

  return (ret);
}

/* --------------------------------------------------------------------- */
/*                                                                       */
/* Path record completion                                                */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsIp2prPathRecordComplete -- path lookup complete, save result */
static tINT32 _tsIp2prPathRecordComplete
(
 tTS_IB_CLIENT_QUERY_TID  tid,
 tINT32                   status,
 tTS_IB_PATH_RECORD       path,
 tINT32                   remaining,
 tPTR                     arg
)
{
  tIP2PR_IPOIB_WAIT ipoib_wait = (tIP2PR_IPOIB_WAIT)arg;
  tIP2PR_PATH_ELEMENT path_elmt = NULL;
  tINT32 result;

  TS_CHECK_NULL(ipoib_wait, -EINVAL);
  TS_CHECK_NULL(path, -EINVAL);

  if (tid != ipoib_wait->tid) {

    TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	     "PATH: result TID mismatch. <%016llx:%016llx>",
	     tid, ipoib_wait->tid);
    return -EFAULT;
  } /* if */
  /*
   * path lookup is complete
   */
  if (0 != remaining) {

    TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: multi-part <%d> result unsupported.", remaining);
  } /* if */

  /*
   * Save result.
   */
  switch (status) {
  case -ETIMEDOUT:
    if (0 < ipoib_wait->retry--) {
      ip2pr_path_timeout++;
      ipoib_wait->prev_timeout = (ipoib_wait->prev_timeout * 2); /* backoff */
      if (ipoib_wait->prev_timeout > _tsIp2prLinkRoot.backoff)
        ipoib_wait->prev_timeout = _tsIp2prLinkRoot.backoff;

      /*
       * reinitiate path record resolution
       */
      result = tsIbPathRecordRequest(ipoib_wait->ca,
                                     ipoib_wait->hw_port,
                                     ipoib_wait->src_gid,
                                     ipoib_wait->dst_gid,
                                     ipoib_wait->pkey,
                                     TS_IB_PATH_RECORD_FORCE_REMOTE,
                                     (ipoib_wait->prev_timeout * HZ) +
                                          (jiffies & 0x0f),
                                     0,
                                     _tsIp2prPathRecordComplete,
                                     ipoib_wait,
                                     &ipoib_wait->tid);
      if (0 != result) {

	TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
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

    TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	     "POST: Path record lookup complete. <%016llx:%016llx:%d>",
	     be64_to_cpu(*(tUINT64 *)path->dgid),
	     be64_to_cpu(*(tUINT64 *)(path->dgid + sizeof(tUINT64))),
	     path->dlid);

    result = _tsIp2prPathElementCreate(ipoib_wait->dst_addr,
				     ipoib_wait->src_addr,
				     ipoib_wait->hw_port,
				     ipoib_wait->ca,
				     path,
				     &path_elmt);
    if (0 > result) {

      TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	       "POST: Error <%d> creating path element.", result);
      status = result;
    } /* if */

    goto callback;
  default:

    TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Error <%d> in path record completion.", status);

    goto callback;
  } /* switch */

  return 0;
callback:

  if (0 < TS_IP2PR_IPOIB_FLAG_GET_FUNC(ipoib_wait)) {

    result = _tsIp2prPathLookupComplete(ipoib_wait->plid,
				      status,
				      path_elmt,
				      ipoib_wait->func,
				      ipoib_wait->arg);
    if (0 > result) {

      TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	       "PATH: Error <%d> completing Path Record Lookup.", result);
    } /* if */

    TS_IP2PR_IPOIB_FLAG_CLR_FUNC(ipoib_wait);
  } /* if */

  if (0 < TS_IP2PR_IPOIB_FLAGS_EMPTY(ipoib_wait)) {

    result = _tsIp2prIpoibWaitDestroy(ipoib_wait, IP2PR_LOCK_NOT_HELD);
    TS_EXPECT(MOD_IP2PR, !(0 > result));
  } /* if */

  return 0;
} /* _tsIp2prPathRecordComplete */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* Address resolution                                                    */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsIp2prLinkFindComplete -- complete the resolution of an ip address  */
static tINT32 _tsIp2prLinkFindComplete
(
 tIP2PR_IPOIB_WAIT ipoib_wait,
 tINT32          status,
 IP2PR_USE_LOCK  use_lock
)
{
  tINT32 result = 0;
  tINT32 expect;
  unsigned long flags;


  TS_CHECK_NULL(ipoib_wait, -EINVAL);

  if (0 < TS_IP2PR_IPOIB_FLAGS_EMPTY(ipoib_wait)) {
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

    TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "FUNC: Error <%d> on shadow cache lookup.", result);
    return (0);
  } /* if */
  /*
   * reset retry counter
   */
  ipoib_wait->retry = _tsIp2prLinkRoot.max_retries;
  ipoib_wait->prev_timeout = _tsIp2prLinkRoot.retry_timeout;

  /*
   * initiate path record resolution
   */
  spin_lock_irqsave(&ipoib_wait->lock, flags);
  if (ipoib_wait->state == IP2PR_STATE_ARP_WAIT) {
    result = tsIbPathRecordRequest(ipoib_wait->ca,
                                   ipoib_wait->hw_port,
                                   ipoib_wait->src_gid,
                                   ipoib_wait->dst_gid,
                                   ipoib_wait->pkey,
                                   TS_IB_PATH_RECORD_FORCE_REMOTE,
                                   (ipoib_wait->prev_timeout * HZ) +
                                        (jiffies & 0x0f),
                                   0,
                                   _tsIp2prPathRecordComplete,
                                   ipoib_wait,
                                   &ipoib_wait->tid);
    if (0 != result) {

      TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	       "FUNC: Error initiating path record request. <%d>", result);
      status = result;
      spin_unlock_irqrestore(&ipoib_wait->lock, flags);
      goto done;
    } /* if */
    ipoib_wait->state = IP2PR_STATE_PATH_WAIT;
  } else {
      TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	       "FUNC: Invalid state <Path wait>");
  }
  spin_unlock_irqrestore(&ipoib_wait->lock, flags);

  return 0;
done:
  if (0 < TS_IP2PR_IPOIB_FLAG_GET_FUNC(ipoib_wait)) {

    result = _tsIp2prPathLookupComplete(ipoib_wait->plid,
				      status,
				      NULL,
				      ipoib_wait->func,
				      ipoib_wait->arg);
    if (0 > result) {

      TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	       "FUNC: Error <%d> completing address resolution. <%d:%08x>",
	       result, status, ipoib_wait->dst_addr);
    } /* if */

    TS_IP2PR_IPOIB_FLAG_CLR_FUNC(ipoib_wait);
  } /* if */

  if (0 < TS_IP2PR_IPOIB_FLAGS_EMPTY(ipoib_wait)) {

    expect = _tsIp2prIpoibWaitDestroy(ipoib_wait, use_lock);
    TS_EXPECT(MOD_IP2PR, !(0 > expect));
  } /* if */

  return 0;
} /*_tsIp2prLinkFindComplete  */

/* ========================================================================= */
/*.._tsIp2prArpQuery -- query arp cache */
static int tsIp2prArpQuery
(
 tIP2PR_IPOIB_WAIT ipoib_wait,
 tUINT32 *state
)
{
  struct neighbour *neigh;
  extern struct neigh_table arp_tbl;

  neigh = neigh_lookup(&arp_tbl, &ipoib_wait->dst_addr, ipoib_wait->dev);
  if (neigh) {
    read_lock_bh(&neigh->lock);
    *state = neigh->nud_state;
    memcpy(ipoib_wait->hw, neigh->ha, sizeof(ipoib_wait->hw));
    read_unlock_bh(&neigh->lock);

    return (0);
  } else {
    memset(ipoib_wait->hw, 0, sizeof(ipoib_wait->hw));
    *state = 0;

    return (-ENOENT);
  }
} /*.._tsIp2prArpQuery */

/* ========================================================================= */
/*.._tsIp2prLinkFind -- resolve an ip address to a ipoib link address. */
static tINT32 _tsIp2prLinkFind
(
 tIP2PR_IPOIB_WAIT ipoib_wait
)
{
  tINT32 result;
  tUINT32 state;
  struct rtable *rt;
  char  devname[20];
  int   i;

  TS_CHECK_NULL(ipoib_wait, -EINVAL);

#if defined(RHEL_ROUTE_FLOW_API) || defined(TS_KERNEL_2_6)
  {
    struct flowi fl = {
      .oif = ipoib_wait->bound_dev, /* oif */
      .nl_u = {
        .ip4_u = {
          .daddr = ipoib_wait->dst_addr, /* dst */
          .saddr = ipoib_wait->src_addr, /* src */
          .tos   = 0, /* tos */
        }
      },
      .proto = 0, /* protocol */
      .uli_u = {
        .ports = {
          .sport = 0, /* sport */
          .dport = 0, /* dport */
        }
      }
    };

    result = ip_route_output_key(&rt, &fl);
  }
#else
  result = ip_route_connect(&rt,
			    ipoib_wait->dst_addr,
			    ipoib_wait->src_addr,
			    ipoib_wait->local_rt,
			    ipoib_wait->bound_dev);
#endif
  if (0 > result ||
      NULL == rt) {

    TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_WARN,
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

    TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "FIND: No neighbour found for <%08x:%08x>",
	     rt->rt_src, rt->rt_dst);

    result = -EINVAL;
    goto error;
  } /* if */

  if (0 > TS_IP2PR_IPOIB_DEV_TOPSPIN(rt->u.dst.neighbour->dev) &&
      0 == (IFF_LOOPBACK & rt->u.dst.neighbour->dev->flags)) {

    TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	     "FIND: Destination or neighbour device is not IPoIB. <%s:%08x>",
	     rt->u.dst.neighbour->dev->name, rt->u.dst.neighbour->dev->flags);

    result = -ENETUNREACH;
    goto error;
  } /* if */

  TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_WARN,
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

  if (NULL == ipoib_wait->dev) {
    TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_WARN,
             "network device is null\n",
             rt->rt_src, ipoib_wait->dev->name);
    result = -EINVAL;
    goto error;
  }

  /*
   * if loopback, check if src device is an ib device. Allow lo device
   */
  if (((0 > TS_IP2PR_IPOIB_DEV_TOPSPIN(ipoib_wait->dev)) &&
      (0 != strncmp(ipoib_wait->dev->name, "lo", 2))) &&
      (IFF_LOOPBACK & rt->u.dst.neighbour->dev->flags)) {
    TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_WARN,
             "<%08x> is loopback, but is on device %s\n",
             rt->rt_src, ipoib_wait->dev->name);
    result = -EINVAL;
    goto error;
  }

  if (0 < (IFF_LOOPBACK & ipoib_wait->dev->flags)) {

    TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_WARN,
                   "dev->flags 0x%lx means loopback",
                   ipoib_wait->dev->flags);

    /*
     * Get ipoib interface which is active
     */
    for (i = 0; i < (IP2PR_MAX_HCAS * 2); i++) {
      sprintf(devname, "ib%d", i);
      if (NULL != (ipoib_wait->dev = dev_get_by_name(devname))) {
        if (0 < (IFF_UP & ipoib_wait->dev->flags)) {
          break;
        }
      }
    }
    if (IP2PR_MAX_HCAS == i)
      ipoib_wait->dev = NULL;
  } /* if */
  /*
   * Verify device.
   */
  if (NULL == ipoib_wait->dev) {

    TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_WARN,
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
			       ipoib_wait->src_gid,
                               &ipoib_wait->pkey);

  if (0 > result) {

    TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	     "FUNC: Error <%d> looking up local device information.", result);
    goto error;
  } /* if */

  TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "FIND: hca <%04x> for port <%02x> gidp <%p>",
	   ipoib_wait->ca, ipoib_wait->hw_port, ipoib_wait->src_gid);

  /*
   * if this is a loopback connection, find the local source interface
   * and get the associated HW address.
   */
  if (rt->u.dst.neighbour->dev->flags & IFF_LOOPBACK) {
    memcpy((char *)ipoib_wait->hw,
        (char *)ipoib_wait->dev->dev_addr,
        sizeof(ipoib_wait->hw));
  } else {
    /*
     * Not Lookback. Get the Mac address from arp
     */
    result = tsIp2prArpQuery(ipoib_wait, &state);
    if ((result) || (state & NUD_FAILED) ||
        ((ipoib_wait->hw[0] == 0) &&
         (ipoib_wait->hw[1] == 0) &&
         (ipoib_wait->hw[2] == 0) &&
         (ipoib_wait->hw[3] == 0) &&
         (ipoib_wait->hw[4] == 0) &&
         (ipoib_wait->hw[5] == 0))) {
      /*
       * No arp entry. Create a Wait entry and send Arp request
       */
      result = _tsIp2prIpoibWaitListInsert(ipoib_wait);
      if (0 > result) {

        TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	         "FIND: Error <%d> inserting wait for address resolution.",
	         result);
        goto error;
      } /* if */

      arp_send(ARPOP_REQUEST,
	       ETH_P_ARP,
	       rt->rt_gateway,
	       rt->u.dst.neighbour->dev,
	       ipoib_wait->src_addr,
	       NULL,
	       rt->u.dst.neighbour->dev->dev_addr,
	       NULL);
      return (0);
    }
  }

  /*
   * We have a valid arp entry or this is a loopback interface.
   */
  result = _tsIp2prLinkFindComplete(ipoib_wait, 0, 1);
  if (0 > result) {

    TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_WARN,
             "FIND: Error <%d> completing address lookup. <%08x:%08x>",
             result, ipoib_wait->src_addr, ipoib_wait->dst_addr);
    goto error;
  } /* if */
  return (0);
error:
  return result;

} /* _tsIp2prLinkFind */
/* --------------------------------------------------------------------- */
/*                                                                       */
/* Arp packet reception for completions                                  */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsIp2prArpRecvComplete -- receive all ARP packets. */
static void _tsIp2prArpRecvComplete
(
 tPTR arg
)
{
  tIP2PR_IPOIB_WAIT ipoib_wait;
  tIP2PR_IPOIB_WAIT next_wait;
  tUINT32 ip_addr = (unsigned long) arg;
  tINT32 result;
  unsigned long flags, flags1;

  TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "RECV: Arp completion for <%08x>.", ip_addr);

  spin_lock_irqsave(&_tsIp2prLinkRoot.wait_lock, flags);
  ipoib_wait = _tsIp2prLinkRoot.wait_list;
  while (NULL != ipoib_wait) {

    next_wait = ipoib_wait->next;

    if ((ip_addr == ipoib_wait->gw_addr) &&
        (LOOKUP_IP2PR == ipoib_wait->type)) {

        spin_lock_irqsave(&ipoib_wait->lock, flags1);
	if ((ipoib_wait->state &  IP2PR_STATE_ARP_WAIT) == 0) {
          spin_unlock_irqrestore(&ipoib_wait->lock, flags1);
    	  ipoib_wait = next_wait;
	  continue;
	}
        spin_unlock_irqrestore(&ipoib_wait->lock, flags1);

      TS_IP2PR_IPOIB_FLAG_CLR_TASK(ipoib_wait);

      result = _tsIp2prLinkFindComplete(ipoib_wait, 0, 0);
      if (0 > result) {

	TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_WARN,
		 "FIND: Error <%d> completing address lookup. <%08x>",
		 result, ipoib_wait->dst_addr);

	result = _tsIp2prIpoibWaitDestroy(ipoib_wait, IP2PR_LOCK_HELD);
	TS_EXPECT(MOD_IP2PR, !(0 > result));
      } /* if */
    } /* if */

    ipoib_wait = next_wait;
  } /* while */
  spin_unlock_irqrestore(&_tsIp2prLinkRoot.wait_lock, flags);

  return;
} /* _tsIp2prArpRecvComplete */

/* ========================================================================= */
/*.._tsIp2prArpRecv -- receive all ARP packets. */
static tINT32 _tsIp2prArpRecv
(
 struct sk_buff *skb,
 struct net_device *dev,
 struct packet_type *pt
)
{
  tIP2PR_IPOIB_WAIT ipoib_wait;
  tIP2PR_IPOIB_ARP arp_hdr;
  tINT32 counter;
  unsigned long flags;

  TS_CHECK_NULL(dev, -EINVAL);
  TS_CHECK_NULL(skb, -EINVAL);
  TS_CHECK_NULL(skb->nh.raw, -EINVAL);

  arp_hdr = (tIP2PR_IPOIB_ARP)skb->nh.raw;


#if 0
  TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	   "RECV: Arp packet <%04x> <%08x:%08x> from device.",
	   arp_hdr->cmd, arp_hdr->src_ip, arp_hdr->dst_ip);
#endif
  /*
   * Remeber, this function is in the bottom half!
   */
  if (0 > TS_IP2PR_IPOIB_DEV_TOPSPIN(dev) ||
      (arp_hdr->cmd != __constant_htons(ARPOP_REPLY) &&
       arp_hdr->cmd != __constant_htons(ARPOP_REQUEST))) {

    goto done;
  } /* if */
  /*
   * determine if anyone is waiting for this ARP response.
   */
  spin_lock_irqsave(&_tsIp2prLinkRoot.wait_lock, flags);
  for (counter = 0, ipoib_wait = _tsIp2prLinkRoot.wait_list;
       NULL != ipoib_wait;
       ipoib_wait = ipoib_wait->next) {

    /* skip gid2pr lookup entries */
    if (LOOKUP_GID2PR == ipoib_wait->type) {
      continue;
    }

    if (arp_hdr->src_ip == ipoib_wait->gw_addr) {

      TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
          "RECV: Arp Recv for <%08x>.", arp_hdr->src_ip);
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
      TS_IP2PR_IPOIB_FLAG_SET_TASK(ipoib_wait);
      TS_IP2PR_IPOIB_FLAG_CLR_TIME(ipoib_wait);

      counter++;
    } /* if */
  } /* for */
  spin_unlock_irqrestore(&_tsIp2prLinkRoot.wait_lock, flags);

  /*
   * Schedule the ARP completion.
   */
  if (0 < counter) {
    INIT_TQUEUE(&_arp_completion, _tsIp2prArpRecvComplete, (void *) (unsigned long) arp_hdr->src_ip);

    schedule_task(&_arp_completion);
  } /* if */

done:
  kfree_skb(skb);
  return 0;
} /* _tsIp2prArpRecv */

/* ========================================================================= */
/*.._tsIp2prAsyncEventFunc -- IB async event handler, for clearing caches */
static void _tsIp2prAsyncEventFunc
(
 tTS_IB_ASYNC_EVENT_RECORD record,
 void *arg
)
{
  tIP2PR_PATH_ELEMENT path_elmt;
  tINT32 result;
  tIP2PR_SGID_ELEMENT sgid_elmt;
  unsigned long         flags;
  tIP2PR_GID_PR_ELEMENT prn_elmt;

  if (NULL == record) {

    TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	     "ASYNC: Event with no record of what happened?");
    return;
  } /* if */

  TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	   "ASYNC: Event <%d> reported, clearing cache.");
  /*
   * destroy all cached path record elements.
   */
  while (NULL != (path_elmt = _tsIp2prLinkRoot.path_list)) {

    result = _tsIp2prPathElementDestroy(path_elmt);
    TS_EXPECT(MOD_IP2PR, !(0 > result));
  } /* while */

  /*
   * Mark the source gid node based on port state
   */
  spin_lock_irqsave(&_tsIp2prLinkRoot.gid_lock, flags);
  for (sgid_elmt = _tsIp2prLinkRoot.src_gid_list;
       NULL != sgid_elmt;
       sgid_elmt = sgid_elmt->next) {
    if ((sgid_elmt->ca == record->device) &&
        (sgid_elmt->port == record->modifier.port)) {
      sgid_elmt->port_state = (record->event == TS_IB_PORT_ACTIVE) ?
					TS_IB_PORT_STATE_ACTIVE :
					TS_IB_PORT_STATE_DOWN;

      /* Gid could have changed. Get the gid */
      if (tsIbGidEntryGet(record->device,
                          record->modifier.port,
                          0,
                          sgid_elmt->gid)) {
        TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	   "Could not get GID: on hca=<%d>,port=<%d>, event=%d", record->device,
           record->modifier.port, record->event);

	/* for now zero it. Will get it, when user queries */
	memcpy(sgid_elmt->gid, nullgid, sizeof (tTS_IB_GID));
      }
      /* clear the Gid pr cache */
      while (NULL != (prn_elmt = sgid_elmt->pr_list)) {
        _tsIp2PrnDelete(prn_elmt);
      }
      break;
    }
  }
  spin_unlock_irqrestore(&_tsIp2prLinkRoot.gid_lock, flags);

  TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	   "Async Port Event on hca=<%d>,port=<%d>, event=%d", record->device,
           record->modifier.port, record->event);

  return;
} /* _tsIp2prAsyncEventFunc */

/* ========================================================================= */
/*.._tsIp2prPathSweepTimerFunc --sweep path cache to reap old entries. */
static void _tsIp2prPathSweepTimerFunc
(
 tPTR arg
)
{
  tIP2PR_PATH_ELEMENT path_elmt;
  tIP2PR_PATH_ELEMENT next_elmt;
  tINT32 result;
  tIP2PR_SGID_ELEMENT      sgid_elmt;
  tIP2PR_GID_PR_ELEMENT prn_elmt, next_prn;

  /* cache_timeout of zero implies static path records. */
  if (_tsIp2prLinkRoot.cache_timeout) {
    /*
     * arg entry is unused.
     */
    path_elmt = _tsIp2prLinkRoot.path_list;
    while (NULL != path_elmt) {
      next_elmt = path_elmt->next;
      if (!((_tsIp2prLinkRoot.cache_timeout * HZ) >
	    (tINT32)(jiffies - path_elmt->usage))) {

        TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	         "PATH: Deleting old <%u:%u> path entry <%08x>.",
	         path_elmt->usage, jiffies, htonl(path_elmt->dst_addr));

        result = _tsIp2prPathElementDestroy(path_elmt);
        TS_EXPECT(MOD_IP2PR, !(0 > result));
      } /* if */

      path_elmt = next_elmt;
    } /* while */

    /*
     * Go thru' the GID List
     */
    sgid_elmt = _tsIp2prLinkRoot.src_gid_list;
    while (NULL != sgid_elmt) {
      prn_elmt = sgid_elmt->pr_list;
      while (NULL != prn_elmt) {
        next_prn = prn_elmt->next;
        if (!((_tsIp2prLinkRoot.cache_timeout * HZ) >
	      (tINT32)(jiffies - prn_elmt->usage))) {

          TS_TRACE(MOD_IP2PR, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	           "GID: Deleting old <%u:%u>.",
	           prn_elmt->usage, jiffies);
          _tsIp2PrnDelete(prn_elmt);
        }
        prn_elmt = next_prn;
      }
      sgid_elmt = sgid_elmt->next;
    }
  }

  /*
   * rearm timer.
   */
  _tsIp2prPathTimer.run_time = jiffies + TS_IP2PR_PATH_TIMER_INTERVAL;
  tsKernelTimerAdd(&_tsIp2prPathTimer);

  return;
} /* _tsIp2prPathSweepTimerFunc */
/* --------------------------------------------------------------------- */
/*                                                                       */
/* Path record lookup functions                                          */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpPathRecordLookup -- resolve an ip address to a path record */
tINT32 tsIp2prPathRecordLookup
(
 tUINT32               dst_addr,      /* NBO */
 tUINT32               src_addr,      /* NBO */
 tUINT8                localroute,
 tINT32                bound_dev_if,
 tIP2PR_PATH_LOOKUP_FUNC func,
 tPTR                  arg,
 tIP2PR_PATH_LOOKUP_ID  *plid
)
{
  tIP2PR_PATH_ELEMENT path_elmt;
  tIP2PR_IPOIB_WAIT ipoib_wait;
  tINT32 result = 0;
  tINT32 expect;

  TS_CHECK_NULL(plid, -EINVAL);
  TS_CHECK_NULL(func, -EINVAL);
  /*
   * new plid
   */
  *plid = TS_IP2PR_PATH_LOOKUP_ID();
  /*
   * perform a lookup to see if a path element structure exists.
   */
  path_elmt = _tsIp2prPathElementLookup(dst_addr);
  if (NULL != path_elmt) {
    /*
     * update last used time.
     */
    path_elmt->usage = jiffies;

    result = _tsIp2prPathLookupComplete(*plid, 0, path_elmt, func, arg);
    if (0 > result) {

      TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	       "PATH: Error <%d> completing Path Record Lookup.", result);
      goto error;
    } /* if */
  } /* if */
  else {

    ipoib_wait = _tsIp2prIpoibWaitCreate(*plid,
				       dst_addr,
				       src_addr,
				       localroute,
				       bound_dev_if,
				       func,
				       arg,
                                       LOOKUP_IP2PR);
    if (NULL == ipoib_wait) {

      TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	       "PATH: Error creating address resolution wait object");
      result = -ENOMEM;
      goto error;
    } /* if */

    ip2pr_total_req++;
    result = _tsIp2prLinkFind(ipoib_wait);
    if (0 > result) {

      TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	       "PATH: Error <%d> starting address resolution.", result);

      expect = _tsIp2prIpoibWaitDestroy(ipoib_wait, IP2PR_LOCK_NOT_HELD);
      TS_EXPECT(MOD_IP2PR, !(0 > expect));

      goto error;
    } /* if */
  } /* else */

  return 0;
error:
  return result;
} /* tsIp2prPathRecordLookup */

/* ========================================================================= */
/*..tsIp2prPathRecordCancel -- cancel a lookup for an address. */
tINT32 tsIp2prPathRecordCancel
(
 tIP2PR_PATH_LOOKUP_ID plid
)
{
  tIP2PR_IPOIB_WAIT ipoib_wait;
  tINT32 result;

  if (TS_IP2PR_PATH_LOOKUP_INVALID == plid) {

    return -ERANGE;
  } /* if */

  ipoib_wait = tsIp2prIpoibWaitPlidLookup(plid);
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

  TS_IP2PR_IPOIB_FLAG_CLR_FUNC(ipoib_wait);
  TS_IP2PR_IPOIB_FLAG_CLR_TIME(ipoib_wait);

  if (0 < TS_IP2PR_IPOIB_FLAGS_EMPTY(ipoib_wait)) {

    result = _tsIp2prIpoibWaitDestroy(ipoib_wait, IP2PR_LOCK_NOT_HELD);
    TS_EXPECT(MOD_IP2PR, !(0 > result));
  } /* if */

  return 0;
} /* tsIp2prPathRecordCancel */

/*..tsGid2prCancel -- cancel a lookup for an address. */
tINT32 tsGid2prCancel
(
 tIP2PR_PATH_LOOKUP_ID plid
)
{
  tIP2PR_IPOIB_WAIT ipoib_wait;
  tINT32 result;

  if (TS_IP2PR_PATH_LOOKUP_INVALID == plid) {

    return -ERANGE;
  } /* if */

  ipoib_wait = tsIp2prIpoibWaitPlidLookup(plid);
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

  TS_IP2PR_IPOIB_FLAG_CLR_FUNC(ipoib_wait);
  TS_IP2PR_IPOIB_FLAG_CLR_TIME(ipoib_wait);

  if (0 < TS_IP2PR_IPOIB_FLAGS_EMPTY(ipoib_wait)) {

    result = _tsIp2prIpoibWaitDestroy(ipoib_wait, IP2PR_LOCK_NOT_HELD);
    TS_EXPECT(MOD_IP2PR, !(0 > result));
  } /* if */

  return 0;
} /* tsGid2prCancel */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* primary initialization/cleanup functions                              */
/*                                                                       */
/* --------------------------------------------------------------------- */
static struct packet_type _sdp_arp_type = {
  .type           = __constant_htons(ETH_P_ARP),
  .func           = _tsIp2prArpRecv,
  .af_packet_priv = (void*) 1, /* understand shared skbs */
};


/* ========================================================================= */
/*.._tsIp2prGidCacheLookup -- Lookup for GID in cache */
tINT32 _tsIp2prGidCacheLookup
(
 tTS_IB_GID    src_gid,
 tTS_IB_GID    dst_gid,
 tTS_IB_PATH_RECORD path_record,
 tIP2PR_SGID_ELEMENT *gid_node
)
{
  tIP2PR_SGID_ELEMENT        sgid_elmt;
  tIP2PR_GID_PR_ELEMENT prn_elmt;
  unsigned long         flags;


  *gid_node = NULL;
  spin_lock_irqsave(&_tsIp2prLinkRoot.gid_lock, flags);
  for (sgid_elmt = _tsIp2prLinkRoot.src_gid_list;
       NULL != sgid_elmt;
       sgid_elmt = sgid_elmt->next) {

    if (TS_IB_PORT_STATE_ACTIVE == sgid_elmt->port_state) {
      /*
       * if the port is active and the gid is zero, then getting the
       * gid in the async handler had failed. Try to get it now.
       */
      if (0 == memcmp(sgid_elmt->gid, nullgid, sizeof (tTS_IB_GID))) {
        if (tsIbGidEntryGet(sgid_elmt->ca,
                            sgid_elmt->port,
                            0,
                            sgid_elmt->gid)) {
          TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	     "Could not get GID: on hca=<%d>,port=<%d>",
	     sgid_elmt->ca,
             sgid_elmt->port);
	  continue;
	}
      }

      /* we have a valid GID */      
      if (0 == memcmp(sgid_elmt->gid, src_gid, sizeof (tTS_IB_GID))) {
        *gid_node = sgid_elmt;
        for (prn_elmt = sgid_elmt->pr_list;
             NULL != prn_elmt;
             prn_elmt = prn_elmt->next) {
          if (0 == memcmp(&prn_elmt->path_record.dgid,
                          dst_gid, sizeof (tTS_IB_GID))) {
            memcpy(path_record, &prn_elmt->path_record,
                   sizeof (tTS_IB_PATH_RECORD_STRUCT));
            prn_elmt->usage = jiffies;
  
            spin_unlock_irqrestore(&_tsIp2prLinkRoot.gid_lock, flags);
            return (0);
          }
        }
      }
    }
  }
  spin_unlock_irqrestore(&_tsIp2prLinkRoot.gid_lock, flags);

  return(-ENOENT);
}

/* ========================================================================= */
/*.._tsIp2prSrcGidNodeGet --  */
tINT32 _tsIp2prSrcGidNodeGet
(
 tTS_IB_GID    src_gid,
 tIP2PR_SGID_ELEMENT *gid_node
)
{
  tIP2PR_SGID_ELEMENT        sgid_elmt;
  unsigned long         flags;

  *gid_node = NULL;
  spin_lock_irqsave(&_tsIp2prLinkRoot.gid_lock, flags);
  for (sgid_elmt = _tsIp2prLinkRoot.src_gid_list;
       NULL != sgid_elmt;
       sgid_elmt = sgid_elmt->next) {
    if (0 == memcmp(sgid_elmt->gid, src_gid, sizeof (tTS_IB_GID))) {
      *gid_node = sgid_elmt;
      spin_unlock_irqrestore(&_tsIp2prLinkRoot.gid_lock, flags);
      return (0);
    }
  }
  spin_unlock_irqrestore(&_tsIp2prLinkRoot.gid_lock, flags);

  return (-EINVAL);
}

/* ========================================================================= */
/*.._tsIp2prGidElementAdd -- Add one node to  Source GID List. */
tINT32 _tsIp2prGidElementAdd
(
 tIP2PR_IPOIB_WAIT      ipoib_wait,
 tTS_IB_PATH_RECORD     path_record
)
{
  unsigned long flags;
  tIP2PR_SGID_ELEMENT gid_node = NULL;
  tIP2PR_GID_PR_ELEMENT prn_elmt;

  if (_tsIp2prSrcGidNodeGet(ipoib_wait->src_gid, &gid_node)) {
    return (-EINVAL);
  }

  prn_elmt = kmem_cache_alloc(_tsIp2prLinkRoot.gid_pr_cache, SLAB_ATOMIC);
  if (NULL == prn_elmt) {
    TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
             "PATH: Error Allocating prn memory.");
    return (-ENOMEM);
  }
  memcpy(&prn_elmt->path_record, path_record,
         sizeof (tTS_IB_PATH_RECORD_STRUCT));

  /*
   * Insert into the ccache list
   */
  spin_lock_irqsave(&_tsIp2prLinkRoot.gid_lock, flags);
  prn_elmt->next = gid_node->pr_list;
  gid_node->pr_list= prn_elmt;
  prn_elmt->p_next = &gid_node->pr_list;
  prn_elmt->usage = jiffies;

  if (NULL != prn_elmt->next) {
    prn_elmt->next->p_next = &prn_elmt->next;
  } /* if */

  spin_unlock_irqrestore(&_tsIp2prLinkRoot.gid_lock, flags);

  return (0);
}

tINT32 _tsIp2PrnDelete
(
 tIP2PR_GID_PR_ELEMENT prn_elmt
)
{
  if (NULL != prn_elmt->p_next) {

    if (NULL != prn_elmt->next) {
      prn_elmt->next->p_next = prn_elmt->p_next;
    } /* if */

    *(prn_elmt->p_next) = prn_elmt->next;

    prn_elmt->p_next = NULL;
    prn_elmt->next   = NULL;
  } /* if */
  kmem_cache_free(_tsIp2prLinkRoot.gid_pr_cache, prn_elmt);

  return (0);
}

/* ========================================================================= */
/*.._tsIp2prSrcGidDelete -- Cleanup one node in  Source GID List. */
tINT32 _tsIp2prSrcGidDelete
(
 tIP2PR_SGID_ELEMENT sgid_elmt
)
{
  unsigned long         flags;
  tIP2PR_GID_PR_ELEMENT prn_elmt;

  spin_lock_irqsave(&_tsIp2prLinkRoot.gid_lock, flags);

  /*
   * Clear Path Record List for this Source GID node
   */
  while (NULL != (prn_elmt = sgid_elmt->pr_list)) {
    _tsIp2PrnDelete(prn_elmt);
  } /* while */

  if (NULL != sgid_elmt->p_next) {

    if (NULL != sgid_elmt->next) {
      sgid_elmt->next->p_next = sgid_elmt->p_next;
    } /* if */

    *(sgid_elmt->p_next) = sgid_elmt->next;

    sgid_elmt->p_next = NULL;
    sgid_elmt->next   = NULL;
  } /* if */
  spin_unlock_irqrestore(&_tsIp2prLinkRoot.gid_lock, flags);

  kmem_cache_free(_tsIp2prLinkRoot.src_gid_cache, sgid_elmt);

  return (0);
}

/* ========================================================================= */
/*.._tsIp2prSrcGidAdd -- Add one node to  Source GID List. */
tINT32 _tsIp2prSrcGidAdd
(
 tTS_IB_DEVICE_HANDLE hca_device,
 tTS_IB_PORT port,
 tTS_IB_PORT_STATE port_state
)
{
  tIP2PR_SGID_ELEMENT        sgid_elmt;
  unsigned long         flags;

  sgid_elmt = kmem_cache_alloc(_tsIp2prLinkRoot.src_gid_cache, SLAB_ATOMIC);
  if (NULL == sgid_elmt) {
    TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
             "PATH: Error Allocating sgidn memory.");
    return (-ENOMEM);
  }

  memset(sgid_elmt, 0, sizeof (tIP2PR_SGID_ELEMENT_STRUCT));
  if (tsIbGidEntryGet(hca_device,
                      port,
                      0,
                      sgid_elmt->gid)) {
    kmem_cache_free(_tsIp2prLinkRoot.src_gid_cache, sgid_elmt);
    return (-EFAULT);
  }

  /*
   * set the fields
   */
  sgid_elmt->ca = hca_device;
  sgid_elmt->port = port;
  sgid_elmt->port_state = port_state;
  sgid_elmt->gid_index = 0;
  sgid_elmt->port_state = port_state;

  /*
   * insert it into the list
   */
  spin_lock_irqsave(&_tsIp2prLinkRoot.gid_lock, flags);
  sgid_elmt->next = _tsIp2prLinkRoot.src_gid_list;
  _tsIp2prLinkRoot.src_gid_list = sgid_elmt;
  sgid_elmt->p_next = &_tsIp2prLinkRoot.src_gid_list;

  if (NULL != sgid_elmt->next) {
    sgid_elmt->next->p_next = &sgid_elmt->next;
  } /* if */

  spin_unlock_irqrestore(&_tsIp2prLinkRoot.gid_lock, flags);

  return (0);
}

/* ========================================================================= */
/*.._tsGid2prComplete -- path lookup complete, save result */
static tINT32 _tsGid2prComplete
(
 tTS_IB_CLIENT_QUERY_TID  tid,
 tINT32                   status,
 tTS_IB_PATH_RECORD       path,
 tINT32                   remaining,
 tPTR                     arg
)
{
  tINT32                result;
  tIP2PR_IPOIB_WAIT ipoib_wait = (tIP2PR_IPOIB_WAIT)arg;
  tGID2PR_LOOKUP_FUNC   func;


  if (tid != ipoib_wait->tid) {
    TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	     "PATH: result TID mismatch. <%016llx:%016llx>",
	     tid, ipoib_wait->tid);
    return -EFAULT;
  } /* if */

  switch (status) {
    case -ETIMEDOUT:
      if (0 < ipoib_wait->retry--) {
        ipoib_wait->tid = TS_IB_CLIENT_QUERY_TID_INVALID;
        result = tsIbPathRecordRequest(ipoib_wait->ca,
                                       ipoib_wait->hw_port,
                                       ipoib_wait->src_gid,
                                       ipoib_wait->dst_gid,
                                       ipoib_wait->pkey,
                                       TS_IB_PATH_RECORD_FORCE_REMOTE,
                                       TS_IP2PR_DEV_PATH_WAIT,
                                       0,
                                       _tsGid2prComplete,
                                       ipoib_wait,
                                       &ipoib_wait->tid);
        if (0 > result) {
          status = result;
          TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
                   "PATH: Error <%d> Completing Path Record Request.", result);
          goto callback;
        }
      } else {
        goto callback;
      }
	  break;
    case 0:
      /*
       * Add to cache
       */
      _tsIp2prGidElementAdd(ipoib_wait, path);
      goto callback;

      break;
    default:
      goto callback;
  }
  return (0);

callback:
  func = (tGID2PR_LOOKUP_FUNC)ipoib_wait->func;
  return func(tid,
              status,
              ipoib_wait->hw_port,
              ipoib_wait->ca,
              path,
              ipoib_wait->arg);
  return (0);
}



/* ========================================================================= */
/*..tsGid2prLookup -- Resolve a destination GD to Path Record */
tINT32 tsGid2prLookup
(
 tTS_IB_GID     src_gid,
 tTS_IB_GID     dst_gid,
 tTS_IB_PKEY    pkey,
 tGID2PR_LOOKUP_FUNC funcptr,
 tPTR           arg,
 tIP2PR_PATH_LOOKUP_ID  *plid
)
{
  tIP2PR_SGID_ELEMENT        gid_node;
  tINT32                result;
  tIP2PR_IPOIB_WAIT ipoib_wait;
  tTS_IB_PATH_RECORD_STRUCT path_record;
  tGID2PR_LOOKUP_FUNC   func;

  TS_CHECK_NULL(plid, -EINVAL);
  TS_CHECK_NULL(funcptr, -EINVAL);
  /*
   * new plid
   */
  *plid = TS_IP2PR_PATH_LOOKUP_ID();

  /*
   * Lookup cache first
   */
  if (0 == _tsIp2prGidCacheLookup(src_gid,
                                  dst_gid,
                                  &path_record,
                                  &gid_node)) {
    func = (tGID2PR_LOOKUP_FUNC)funcptr;
    result = func(*plid, 0, gid_node->port, gid_node->ca, &path_record, arg);
    if (0 != result) {
      TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
               "PATH: Error <%d> Completing Path Record Request.", result);
    }
    return (0);
  }

  /*
   * Source GID is not valid
   */
  if (NULL == gid_node) {
    return (-EHOSTUNREACH);
  }

  ipoib_wait = _tsIp2prIpoibWaitCreate(*plid,
		                       0,
				       0,
				       0,
				       0,
				       (tPTR)funcptr,
				       arg,
                                       LOOKUP_GID2PR);
  if (NULL == ipoib_wait) {

    TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
      "PATH: Error creating address resolution wait object");
    return (-ENOMEM);
  } /* if */
  ipoib_wait->ca = gid_node->ca;
  ipoib_wait->hw_port = gid_node->port;
  ipoib_wait->pkey = pkey;
  memcpy(ipoib_wait->src_gid, src_gid, sizeof (tTS_IB_GID));
  memcpy(ipoib_wait->dst_gid, dst_gid, sizeof (tTS_IB_GID));

  result = _tsIp2prIpoibWaitListInsert(ipoib_wait);
  if (0 > result) {
    _tsIp2prIpoibWaitDestroy(ipoib_wait, IP2PR_LOCK_NOT_HELD);
    return (result);
  }

  /*
   * Initiate  path record resolution
   */
  result = tsIbPathRecordRequest(gid_node->ca,
                                 gid_node->port,
            		         src_gid,
				 dst_gid,
                                 ipoib_wait->pkey,
				 TS_IB_PATH_RECORD_FORCE_REMOTE,
				 TS_IP2PR_DEV_PATH_WAIT,
				 0,
				 _tsGid2prComplete,
				 ipoib_wait,
				 &ipoib_wait->tid);
  if (0 != result) {
    TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
             "PATH: Error <%d> Path Record Request.", result);
  }

  return (result);
}

/* ========================================================================= */
/*..tsIp2prSrcGidCleanup -- Cleanup the Source GID List. */
tINT32 tsIp2prSrcGidCleanup
(
 void
)
{
  tIP2PR_SGID_ELEMENT  sgid_elmt;
  tINT32          result;

  while (NULL != (sgid_elmt = _tsIp2prLinkRoot.src_gid_list)) {

    result = _tsIp2prSrcGidDelete(sgid_elmt);
    TS_EXPECT(MOD_IP2PR, !(0 > result));
  } /* while */

  kmem_cache_destroy(_tsIp2prLinkRoot.src_gid_cache);
  kmem_cache_destroy(_tsIp2prLinkRoot.gid_pr_cache);

  return (0);
}

/* ========================================================================= */
/*..tsIp2prSrcGidInit -- initialize the Source GID List. */
tINT32 tsIp2prSrcGidInit
(
 void
)
{
  tINT32 result = 0;
  int i, j;
  tTS_IB_DEVICE_HANDLE hca_device;
  tTS_IB_DEVICE_PROPERTIES_STRUCT dev_prop;
  tTS_IB_PORT_PROPERTIES_STRUCT port_prop;

  _tsIp2prLinkRoot.src_gid_cache = kmem_cache_create("Ip2prSrcGidList",
                       			   sizeof(tIP2PR_SGID_ELEMENT_STRUCT),
					   0, SLAB_HWCACHE_ALIGN,
					   NULL, NULL);
  if (NULL == _tsIp2prLinkRoot.src_gid_cache) {

    TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	     "INIT: Failed to create src gid cache.");
    return (-ENOMEM);
  } /* if */

  _tsIp2prLinkRoot.gid_pr_cache = kmem_cache_create("Ip2prGidPrList",
	                                  sizeof(tIP2PR_GID_PR_ELEMENT_STRUCT),
	                                  0, SLAB_HWCACHE_ALIGN,
	                                  NULL, NULL);
  if (NULL == _tsIp2prLinkRoot.gid_pr_cache) {

    TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	     "INIT: Failed to create gid to pr list cache.");
    kmem_cache_destroy(_tsIp2prLinkRoot.src_gid_cache);
    return (-ENOMEM);
  } /* if */

  /*
   * Create SGID list for each port on hca
   */
  for (i = 0; ((hca_device = tsIbDeviceGetByIndex(i)) != NULL); ++i) {
    if (tsIbDevicePropertiesGet(hca_device, &dev_prop)) {
      TS_REPORT_FATAL(MOD_IB_NET, "tsIbDevicePropertiesGet() failed");
      return -EINVAL;
    }

    for (j = 1; j <= dev_prop.num_port; j++) {
      if (tsIbPortPropertiesGet(hca_device, j, &port_prop)) {
        continue;
      }

      result = _tsIp2prSrcGidAdd(hca_device, j, port_prop.port_state);
      if (0 > result) {
        goto port_err;
      }
    } /* for */
  } /* for */
  return (0);

port_err:
    kmem_cache_destroy(_tsIp2prLinkRoot.src_gid_cache);
    kmem_cache_destroy(_tsIp2prLinkRoot.gid_pr_cache);

    return (result);
}

/* ========================================================================= */
/*..tsIp2prLinkAddrInit -- initialize the advertisment caches. */
tINT32 tsIp2prLinkAddrInit
(
 void
)
{
  tINT32 result = 0;
  tTS_IB_ASYNC_EVENT_RECORD_STRUCT evt_rec;
  int   i;
  tTS_IB_DEVICE_HANDLE  hca_device;

  TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_INOUT,
	   "INIT: Link level services initialization.");

  if (NULL != _tsIp2prLinkRoot.wait_cache) {

    TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	     "INIT: Wait cache is already initialized!");

    result = -EINVAL;
    goto error;
  } /* if */
  /*
   * create cache
   */
  _tsIp2prLinkRoot.wait_cache = kmem_cache_create("Ip2prIpoibWait",
					    sizeof(tIP2PR_IPOIB_WAIT_STRUCT),
					    0, SLAB_HWCACHE_ALIGN,
					    NULL, NULL);
  if (NULL == _tsIp2prLinkRoot.wait_cache) {

    TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	     "INIT: Failed to create wait cache.");

    result = -ENOMEM;
    goto error_wait;
  } /* if */

  _tsIp2prLinkRoot.path_cache = kmem_cache_create("Ip2prPathLookup",
					    sizeof(tIP2PR_PATH_ELEMENT_STRUCT),
					    0, SLAB_HWCACHE_ALIGN,
					    NULL, NULL);
  if (NULL == _tsIp2prLinkRoot.path_cache) {

    TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	     "INIT: Failed to create path lookup cache.");

    result = -ENOMEM;
    goto error_path;
  } /* if */

  _tsIp2prLinkRoot.user_req = kmem_cache_create("Ip2prUserReq",
                                              sizeof (tIP2PR_USER_REQ), 0,
                                              SLAB_HWCACHE_ALIGN,
                                              NULL, NULL);
 if (NULL == _tsIp2prLinkRoot.user_req) {

    TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	     "INIT: Failed to create user request cache.");

    result = -ENOMEM;
    goto error_user;
  }
  /*
   * Install async event handler, to clear cache on port down
   */

  for (i = 0; i < IP2PR_MAX_HCAS; i++) {
      _tsIp2prAsyncErrHandle[i] =  TS_IP2PR_INVALID_ASYNC_HANDLE;
      _tsIp2prAsyncActHandle[i] =  TS_IP2PR_INVALID_ASYNC_HANDLE;
  }

  for (i = 0; ((hca_device = tsIbDeviceGetByIndex(i)) != NULL); ++i) {
    evt_rec.device = hca_device;
    evt_rec.event = TS_IB_PORT_ERROR;
    result = tsIbAsyncEventHandlerRegister(&evt_rec,
  						_tsIp2prAsyncEventFunc,
						NULL,
						&_tsIp2prAsyncErrHandle[i]);
    if (0 != result) {

      TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	       "INIT: Error <%d> registering event handler.", result);
      goto error_async;
    } /* if */

    evt_rec.device = hca_device;
    evt_rec.event = TS_IB_PORT_ACTIVE;
    result = tsIbAsyncEventHandlerRegister(&evt_rec,
  						_tsIp2prAsyncEventFunc,
						NULL,
						&_tsIp2prAsyncActHandle[i]);
    if (0 != result) {

      TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_WARN,
	       "INIT: Error <%d> registering event handler.", result);
      goto error_async;
    } /* if */
  } /* for */

  /*
   * create timer for pruning path record cache.
   */
  tsKernelTimerInit(&_tsIp2prPathTimer);
  _tsIp2prPathTimer.run_time = jiffies + TS_IP2PR_PATH_TIMER_INTERVAL;
  _tsIp2prPathTimer.function = _tsIp2prPathSweepTimerFunc;
  _tsIp2prPathTimer.arg      = NULL;
  tsKernelTimerAdd(&_tsIp2prPathTimer);
  /*
   * install device for receiving ARP packets in parallel to the normal
   * Linux ARP, this will be the IP2PR notifier that an ARP request has
   * completed.
   */
  dev_add_pack(&_sdp_arp_type);

  _tsIp2prLinkRoot.backoff =  TS_IP2PR_PATH_BACKOFF;
  _tsIp2prLinkRoot.max_retries = TS_IP2PR_PATH_RETRIES;
  _tsIp2prLinkRoot.retry_timeout = TS_IP2PR_DEV_PATH_WAIT;
  _tsIp2prLinkRoot.cache_timeout = TS_IP2PR_PATH_REAPING_AGE;

  return 0;
error_async:

  for (i = 0; i < IP2PR_MAX_HCAS; i++) {
    if (_tsIp2prAsyncErrHandle[i] != TS_IP2PR_INVALID_ASYNC_HANDLE) {
      tsIbAsyncEventHandlerDeregister(_tsIp2prAsyncErrHandle[i]);
    }
    if (_tsIp2prAsyncActHandle[i] != TS_IP2PR_INVALID_ASYNC_HANDLE) {
      tsIbAsyncEventHandlerDeregister(_tsIp2prAsyncActHandle[i]);
    }
  }

  kmem_cache_destroy(_tsIp2prLinkRoot.user_req);
error_user:
  kmem_cache_destroy(_tsIp2prLinkRoot.path_cache);
error_path:
  kmem_cache_destroy(_tsIp2prLinkRoot.wait_cache);
error_wait:
error:
  return result;
} /* tsIp2prLinkAddrInit */

/* ========================================================================= */
/*..tsIp2prLinkAddrCleanup -- cleanup the advertisment caches. */
tINT32 tsIp2prLinkAddrCleanup
(
 void
)
{
  tIP2PR_PATH_ELEMENT path_elmt;
  tIP2PR_IPOIB_WAIT ipoib_wait;
  tUINT32 result;
  int   i;

  TS_CHECK_NULL(_tsIp2prLinkRoot.wait_cache, -EINVAL);

  TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_INOUT,
	   "INIT: Link level services cleanup.");
  /*
   * stop cache pruning timer
   */
  tsKernelTimerRemove(&_tsIp2prPathTimer);
  /*
   * remove ARP packet processing.
   */
  dev_remove_pack(&_sdp_arp_type);

  /*
   * release async event handler(s)
   */
  for (i = 0; i < IP2PR_MAX_HCAS; i++) {
    if (_tsIp2prAsyncErrHandle[i] != TS_IP2PR_INVALID_ASYNC_HANDLE) {
      tsIbAsyncEventHandlerDeregister(_tsIp2prAsyncErrHandle[i]);
    }
    if (_tsIp2prAsyncActHandle[i] != TS_IP2PR_INVALID_ASYNC_HANDLE) {
      tsIbAsyncEventHandlerDeregister(_tsIp2prAsyncActHandle[i]);
    }
  }

  /*
   * clear wait list
   */
  while (NULL != (ipoib_wait = _tsIp2prLinkRoot.wait_list)) {

    result = _tsIp2prIpoibWaitDestroy(ipoib_wait, IP2PR_LOCK_NOT_HELD);
    TS_EXPECT(MOD_IP2PR, !(0 > result));
  } /* while */

  while (NULL != (path_elmt = _tsIp2prLinkRoot.path_list)) {

    result = _tsIp2prPathElementDestroy(path_elmt);
    TS_EXPECT(MOD_IP2PR, !(0 > result));
  } /* while */
  /*
   * delete cache
   */
  kmem_cache_destroy(_tsIp2prLinkRoot.wait_cache);
  kmem_cache_destroy(_tsIp2prLinkRoot.path_cache);
  kmem_cache_destroy(_tsIp2prLinkRoot.user_req);

  return 0;
} /* tsIp2prLinkAddrCleanup */

/* ========================================================================= */
/*..tsIp2prCbInternal -- Callback for IP to Path Record Lookup */
static tINT32 _tsIp2prCbInternal
(
 tIP2PR_PATH_LOOKUP_ID  plid,
 tINT32               status,
 tUINT32              src_addr,
 tUINT32              dst_addr,
 tTS_IB_PORT          hw_port,
 tTS_IB_DEVICE_HANDLE ca,
 tTS_IB_PATH_RECORD   path,
 tPTR                 usr_arg
)
{
  tIP2PR_USER_REQ       ureq;

  if (usr_arg == NULL) {
    TS_REPORT_WARN(MOD_IP2PR, "Called with a NULL usr_arg");
    return -1;
  }
  ureq = (tIP2PR_USER_REQ)usr_arg;
  ureq->status = status;

  if (0 == status) {
    memcpy(&ureq->path_record, path, sizeof (tTS_IB_PATH_RECORD_STRUCT));
  }
  up(&ureq->sem);       /* wake up sleeping process */

  return (0);
}

/* ========================================================================= */
/*..tsIp2prCbInternal -- Callback for Gid to Path Record Lookup */
static tINT32 _tsGid2prCbInternal
(
 tIP2PR_PATH_LOOKUP_ID  plid,
 tINT32               status,
 tTS_IB_PORT          hw_port,
 tTS_IB_DEVICE_HANDLE ca,
 tTS_IB_PATH_RECORD   path,
 tPTR                 usr_arg
)
{
  tIP2PR_USER_REQ       ureq;

  if (usr_arg == NULL) {
    TS_REPORT_WARN(MOD_IP2PR, "Called with a NULL usr_arg");
    return -1;
  }
  ureq = (tIP2PR_USER_REQ)usr_arg;
  ureq->status = status;
  ureq->port = hw_port;
  ureq->device = ca;

  if (0 == status) {
    memcpy(&ureq->path_record, path, sizeof (tTS_IB_PATH_RECORD_STRUCT));
  }
  up(&ureq->sem);       /* wake up sleeping process */

  return (0);
}

/* ========================================================================= */
/*..tsIp2prUserLookup -- Process a IP to Path Record lookup ioctl request */
tINT32 _tsIp2prUserLookup
(
 unsigned long arg
)
{
  tIP2PR_USER_REQ       ureq;
  tIP2PR_LOOKUP_PARAM_STRUCT    param;
  tINT32                status;
  tIP2PR_PATH_LOOKUP_ID   plid;

  if (0 == arg) {
    return (-EINVAL);
  }
  if (copy_from_user(&param, (tIP2PR_LOOKUP_PARAM)arg,
      sizeof (tIP2PR_LOOKUP_PARAM_STRUCT))) {
    return (-EFAULT);
  }
  if (NULL == param.path_record) {
    return (-EINVAL);
  }

  ureq = kmem_cache_alloc(_tsIp2prLinkRoot.user_req, SLAB_ATOMIC);
  if (NULL == ureq) {
    return (-ENOMEM);
  }

  ureq->status = 0;
  sema_init(&ureq->sem, 0);
  status = tsIp2prPathRecordLookup(param.dst_addr, 0, 0, 0,
                                 _tsIp2prCbInternal, ureq, &plid);
  if (status < 0) {
    kmem_cache_free(_tsIp2prLinkRoot.user_req, ureq);
    return (-EFAULT);
  }

  status = down_interruptible(&ureq->sem);
  if (status) {
    tsIp2prPathRecordCancel(plid);
    kmem_cache_free(_tsIp2prLinkRoot.user_req, ureq);
    return (-EINTR);
  }

  if (ureq->status) {
    kmem_cache_free(_tsIp2prLinkRoot.user_req, ureq);
    return (-EHOSTUNREACH);
  }

  copy_to_user(param.path_record, &ureq->path_record,
               sizeof(tTS_IB_PATH_RECORD_STRUCT));
  kmem_cache_free(_tsIp2prLinkRoot.user_req, ureq);

  return (0);
}

/* ========================================================================= */
/*..tsGid2prUserLookup -- Process a Gid to Path Record lookup ioctl request */
tINT32 _tsGid2prUserLookup
(
 unsigned long arg
)
{
  tIP2PR_USER_REQ       ureq;
  tGID2PR_LOOKUP_PARAM_STRUCT    param;
  tGID2PR_LOOKUP_PARAM  upa;
  tINT32                status;
  tIP2PR_PATH_LOOKUP_ID   plid;

  if (0 == arg) {
    return (-EINVAL);
  }

  if (copy_from_user(&param, (tGID2PR_LOOKUP_PARAM)arg,
      sizeof (tGID2PR_LOOKUP_PARAM_STRUCT))) {
    return (-EFAULT);
  }

  if (NULL == param.path_record) {
    return (-EINVAL);
  }
  ureq = kmem_cache_alloc(_tsIp2prLinkRoot.user_req, SLAB_ATOMIC);
  if (NULL == ureq) {
    return (-ENOMEM);
  }

  ureq->status = 0;
  sema_init(&ureq->sem, 0);
  status = tsGid2prLookup(param.src_gid, param.dst_gid, param.pkey,
                                 _tsGid2prCbInternal, (tPTR)ureq, &plid);
  if (status < 0) {
    kmem_cache_free(_tsIp2prLinkRoot.user_req, ureq);
    return (-EFAULT);
  }

  status = down_interruptible(&ureq->sem);
  if (status) {
    tsGid2prCancel(plid);
    kmem_cache_free(_tsIp2prLinkRoot.user_req, ureq);
    return (-EINTR);
  }

  if (ureq->status) {
    kmem_cache_free(_tsIp2prLinkRoot.user_req, ureq);
    return (-EHOSTUNREACH);
  }

  upa = (tGID2PR_LOOKUP_PARAM)arg;
  copy_to_user(&upa->device, &ureq->device, sizeof (tTS_IB_DEVICE_HANDLE));
  copy_to_user(&upa->port, &ureq->port, sizeof (tTS_IB_PORT));
  copy_to_user(param.path_record, &ureq->path_record,
               sizeof(tTS_IB_PATH_RECORD_STRUCT));
  kmem_cache_free(_tsIp2prLinkRoot.user_req, ureq);

  return (0);
}

EXPORT_SYMBOL(tsIp2prPathRecordLookup);
EXPORT_SYMBOL(tsIp2prPathRecordCancel);
EXPORT_SYMBOL(tsGid2prLookup);
EXPORT_SYMBOL(tsGid2prCancel);
