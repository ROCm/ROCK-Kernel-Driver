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

  $Id: sdp_advt.c 32 2004-04-09 03:57:42Z roland $
*/

#include "sdp_main.h"

static kmem_cache_t *_tsSdpConnAdvtCache = NULL;
static kmem_cache_t *_tsSdpConnAdvtTable = NULL;

/* --------------------------------------------------------------------- */
/*                                                                       */
/* module specific functions                                             */
/*                                                                       */
/* --------------------------------------------------------------------- */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* public advertisment object functions for FIFO object table            */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpConnAdvtCreate - create an advertisment object */
tSDP_ADVT tsSdpConnAdvtCreate
(
 void
)
{
  tSDP_ADVT advt;

  TS_CHECK_NULL(_tsSdpConnAdvtCache, NULL);

  advt = kmem_cache_alloc(_tsSdpConnAdvtCache, SLAB_ATOMIC);

  if (NULL != advt) {

    advt->next = NULL;
    advt->prev = NULL;
    advt->size = 0;
    advt->post = 0;
    advt->addr = 0;
    advt->rkey = 0;

    advt->type    = TS_SDP_GENERIC_TYPE_ADVT;
    advt->release = (tSDP_GENERIC_DESTRUCT_FUNC)tsSdpConnAdvtDestroy;
  } /* if */

  return advt;
} /* tsSdpConnAdvtCreate */

/* ========================================================================= */
/*..tsSdpConnAdvtDestroy - destroy an advertisment object */
tINT32 tsSdpConnAdvtDestroy
(
 tSDP_ADVT advt
)
{
  TS_CHECK_NULL(advt, -EINVAL);

  if (NULL != advt->next ||
      NULL != advt->prev) {

    return -EACCES;
  } /* if */
  /*
   * return the object to its cache
   */
  kmem_cache_free(_tsSdpConnAdvtCache, advt);

  return 0;
} /* tsSdpConnAdvtDestroy */

/* ========================================================================= */
/*..tsSdpConnAdvtTableGet - get, and remove, the object at the tables head */
tSDP_ADVT tsSdpConnAdvtTableGet
(
 tSDP_ADVT_TABLE table
)
{
  tSDP_ADVT advt;
  tSDP_ADVT next;
  tSDP_ADVT prev;

  TS_CHECK_NULL(table, NULL);

  advt = table->head;
  if (NULL == advt) {

    return NULL;
  } /* if */

  if (advt->next == advt &&
      advt->prev == advt) {

    table->head = NULL;
  } /* if */
  else {

    next = advt->next;
    prev = advt->prev;
    next->prev = prev;
    prev->next = next;

    table->head = next;
  } /* else */

  table->size--;

  advt->next = NULL;
  advt->prev = NULL;

  return advt;
} /* tsSdpConnAdvtTableGet */

/* ========================================================================= */
/*..tsSdpConnAdvtTableLook - get, without removing, the object at the head */
tSDP_ADVT tsSdpConnAdvtTableLook
(
 tSDP_ADVT_TABLE table
)
{
  TS_CHECK_NULL(table, NULL);

  return table->head;
} /* tsSdpConnAdvtTableLook */

/* ========================================================================= */
/*..tsSdpConnAdvtTablePut - put the advertisment object at the tables tail */
tINT32 tsSdpConnAdvtTablePut
(
 tSDP_ADVT_TABLE table,
 tSDP_ADVT       advt
)
{
  tSDP_ADVT next;
  tSDP_ADVT prev;

  TS_CHECK_NULL(table, -EINVAL);
  TS_CHECK_NULL(advt, -EINVAL);

  if (NULL == table->head) {

    advt->next  = advt;
    advt->prev  = advt;
    table->head = advt;
  } /* if */
  else {

    next = table->head;
    prev = next->prev;

    prev->next = advt;
    advt->prev = prev;
    advt->next = next;
    next->prev = advt;
  } /* else */

  table->size++;

  return 0;
} /* tsSdpConnAdvtTablePut */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* public table functions                                                */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpConnAdvtTableCreate - create an advertisment table */
tSDP_ADVT_TABLE tsSdpConnAdvtTableCreate
(
 tINT32  *result
)
{
  tSDP_ADVT_TABLE table = NULL;

  TS_CHECK_NULL(result, NULL);
  *result = -EINVAL;
  TS_CHECK_NULL(_tsSdpConnAdvtTable, NULL);

  table = kmem_cache_alloc(_tsSdpConnAdvtTable, SLAB_ATOMIC);
  if (NULL == table) {

    *result = -ENOMEM;
    return NULL;
  } /* if */

  table->head = NULL;
  table->size = 0;

  *result = 0;
  return table;
} /* tsSdpConnAdvtTableCreate */

/* ========================================================================= */
/*..tsSdpConnAdvtTableInit - initialize a new empty advertisment table */
tINT32 tsSdpConnAdvtTableInit
(
 tSDP_ADVT_TABLE table
)
{
  TS_CHECK_NULL(_tsSdpConnAdvtCache, -EINVAL);
  TS_CHECK_NULL(table, -EINVAL);

  table->head = NULL;
  table->size = 0;

  return 0;
} /* tsSdpConnAdvtTableInit */

/* ========================================================================= */
/*..tsSdpConnAdvtTableClear - clear the contents of an advertisment table */
tINT32 tsSdpConnAdvtTableClear
(
 tSDP_ADVT_TABLE table
)
{
  tSDP_ADVT advt;
  tINT32 result;

  TS_CHECK_NULL(_tsSdpConnAdvtCache, -EINVAL);
  TS_CHECK_NULL(table, -EINVAL);
  /*
   * drain the table of any objects
   */
  while (NULL != (advt = tsSdpConnAdvtTableGet(table))) {

    result = tsSdpConnAdvtDestroy(advt);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  } /* while */

  return 0;
} /* tsSdpConnAdvtTableClear */

/* ========================================================================= */
/*..tsSdpConnAdvtTableDestroy - destroy an advertisment table */
tINT32 tsSdpConnAdvtTableDestroy
(
 tSDP_ADVT_TABLE table
)
{
  tSDP_ADVT advt;
  tINT32 result;

  TS_CHECK_NULL(_tsSdpConnAdvtTable, -EINVAL);
  TS_CHECK_NULL(table, -EINVAL);
  /*
   * drain the table of any objects
   */
  while (NULL != (advt = tsSdpConnAdvtTableGet(table))) {

    result = tsSdpConnAdvtDestroy(advt);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  } /* while */
  /*
   * return the table to the cache
   */
  kmem_cache_free(_tsSdpConnAdvtTable, table);

  return 0;
} /* tsSdpConnAdvtTableDestroy */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* primary initialization/cleanup functions                              */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpConnAdvtMainInit -- initialize the advertisment caches. */
tINT32 tsSdpConnAdvtMainInit
(
 void
)
{
  tINT32 result;

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "INIT: Advertisment cache initialization.");
  /*
   * initialize the caches only once.
   */
  if (NULL != _tsSdpConnAdvtCache ||
      NULL != _tsSdpConnAdvtTable) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "INIT: Advertisment caches already initialized.");
    return -EINVAL;
  } /* if */

  _tsSdpConnAdvtCache = kmem_cache_create("SdpAdvtCache",
					  sizeof(tSDP_ADVT_STRUCT),
					  0, SLAB_HWCACHE_ALIGN, NULL, NULL);
  if (NULL == _tsSdpConnAdvtCache) {

    result = -ENOMEM;
    goto error_advt_c;
  } /* if */

  _tsSdpConnAdvtTable = kmem_cache_create("SdpAdvtTable",
					  sizeof(tSDP_ADVT_TABLE_STRUCT),
					  0, SLAB_HWCACHE_ALIGN, NULL, NULL);
  if (NULL == _tsSdpConnAdvtTable) {

    result = -ENOMEM;
    goto error_advt_t;
  } /* if */

  return 0;
error_advt_t:
  kmem_cache_destroy(_tsSdpConnAdvtCache);
  _tsSdpConnAdvtCache = NULL;
error_advt_c:
  return 0;
} /* tsSdpConnAdvtMainInit */

/* ========================================================================= */
/*..tsSdpConnAdvtMainCleanup -- cleanup the advertisment caches. */
tINT32 tsSdpConnAdvtMainCleanup
(
 void
)
{
  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "INIT: Advertisment cache cleanup.");
  /*
   * cleanup the caches
   */
  kmem_cache_destroy(_tsSdpConnAdvtCache);
  kmem_cache_destroy(_tsSdpConnAdvtTable);
  /*
   * null out entries.
   */
  _tsSdpConnAdvtCache = NULL;
  _tsSdpConnAdvtTable = NULL;

  return 0;
} /* tsSdpConnAdvtMainCleanup */
