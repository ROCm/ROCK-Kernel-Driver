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

  $Id: sdp_iocb.c 32 2004-04-09 03:57:42Z roland $
*/

#include "sdp_main.h"

static kmem_cache_t *_tsSdpConnIocbCache = NULL;
static kmem_cache_t *_tsSdpConnIocbTable = NULL;

/* --------------------------------------------------------------------- */
/*                                                                       */
/* IOCB memory registration functions                                    */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpConnIocbRegister -- register an IOCBs memory for advertisment */
tINT32 tsSdpConnIocbRegister
(
 tSDP_IOCB iocb,
 tSDP_CONN conn
)
{
#ifdef _TS_SDP_AIO_SUPPORT
  struct kveclet *let;
  tINT32 result;
  tINT32 counter;

  TS_CHECK_NULL(iocb, -EINVAL);
  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * create page list.
   */
  iocb->page_count = iocb->cb.vec->nr;
  iocb->page_array = kmalloc((sizeof(tUINT64) * iocb->page_count), GFP_ATOMIC);

  if (NULL == iocb->page_array) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "POST: Failed to allocate IOCB page array. <%d:%d>",
	     sizeof(tUINT64) * iocb->page_count, iocb->page_count);

    result = -ENOMEM;
    goto error;
  } /* if */
  /*
   * iterate of physical page list.
   */
  iocb->page_offset = iocb->cb.vec->veclet->offset;

  for (counter = 0, let = iocb->cb.vec->veclet;
       counter < iocb->page_count;
       counter++, let++) {

    iocb->page_array[counter] = TS_SDP_IOCB_PAGE_TO_PHYSICAL(let->page);
  } /* for */
  /*
   * prime io address with physical address of first byte?
   */
  iocb->io_addr = iocb->page_array[0] + iocb->page_offset;
  /*
   * register IOCBs physical memory
   */
  result = tsIbFmrRegisterPhysical(conn->fmr_pool,
				   (uint64_t *)iocb->page_array,
				   iocb->page_count,
				   (uint64_t *)&iocb->io_addr,
				   iocb->page_offset,
				   &iocb->mem,
				   &iocb->l_key,
				   &iocb->r_key);
  if (0 != result) {
    if (-EAGAIN != result) {

      TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	       "POST: Error <%d> registering physical memory. <%d:%d:%d>",
	       result, iocb->len, iocb->page_count, iocb->page_offset);
    } /* if */

    goto error_register;
  } /* if */
  /*
   * some data may have already been consumed, adjust the io address
   * to take this into account
   */
  iocb->io_addr += iocb->post;

  return 0;
error_register:
  kfree(iocb->page_array);
  iocb->page_array = NULL;
error:
  return result;
#else
  return -EOPNOTSUPP;
#endif
} /* tsSdpConnIocbRegister */

/* ========================================================================= */
/*..tsSdpConnIocbRelease -- unregister an IOCBs memory  */
tINT32 tsSdpConnIocbRelease
(
 tSDP_IOCB iocb
)
{
  tINT32 result;

  TS_CHECK_NULL(iocb, -EINVAL);

  if (NULL != iocb->page_array) {

    result = tsIbFmrDeregister(iocb->mem);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	       "EVENT: Error <%d> releasing IOCB <%d> memory <%u>",
	       result, iocb->key, iocb->mem);
    } /* if */

    kfree(iocb->page_array);
  } /* if */

  iocb->page_array = NULL;
  iocb->io_addr    = 0;

  return 0;
} /* tsSdpConnIocbRelease */

/* ========================================================================= */
/*..tsSdpConnIocbComplete -- complete an IOCB */
tINT32 tsSdpConnIocbComplete
(
 tSDP_IOCB  iocb,
 tINT32     status
)
{
  tINT32 result;

  TS_CHECK_NULL(iocb, -EINVAL);
  /*
   * release memory
   */
  result = tsSdpConnIocbRelease(iocb);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: Error <%d> releasing IOCB <%d> memory <%u>",
	     result, iocb->key, iocb->mem);
  } /* if */
  /*
   * callback to complete IOCB
   */
  result = (0 < iocb->post) ? iocb->post : status;

#ifdef _TS_SDP_AIO_SUPPORT
  iocb->cb.fn(iocb->cb.data, iocb->cb.vec, result);
#endif
  /*
   * delete IOCB
   */
  result = tsSdpConnIocbDestroy(iocb);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_WARN,
	     "EVENT: Error <%d> deleting IOCB <%d> of status <%d>",
	     result, iocb->key, status);
    goto error;
  } /* if */

  return 0;
error:
  return result;
} /* tsSdpConnIocbComplete */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* IOCB object managment                                                 */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpConnIocbTableRemove - remove the object from the table */
tINT32 tsSdpConnIocbTableRemove
(
 tSDP_IOCB iocb
)
{
  tSDP_IOCB_TABLE table;
  tSDP_IOCB next;
  tSDP_IOCB prev;

  TS_CHECK_NULL(iocb, -EINVAL);
  TS_CHECK_NULL(iocb->next, -EINVAL);
  TS_CHECK_NULL(iocb->prev, -EINVAL);
  TS_CHECK_NULL(iocb->table, -EINVAL);
  TS_CHECK_NULL(iocb->table->head, -EINVAL);

  table = iocb->table;

  if (iocb->next == iocb &&
      iocb->prev == iocb) {

    table->head = NULL;
  } /* if */
  else {

    next = iocb->next;
    prev = iocb->prev;
    next->prev = prev;
    prev->next = next;

    if (table->head == iocb) {

      table->head = next;
    } /* if */
  } /* else */

  table->size--;

  iocb->table = NULL;
  iocb->next  = NULL;
  iocb->prev  = NULL;

  return 0;
} /* tsSdpConnIocbTableRemove */

/* ========================================================================= */
/*..tsSdpConnIocbTableLookup - find an iocb based on key, without removing */
tSDP_IOCB tsSdpConnIocbTableLookup
(
 tSDP_IOCB_TABLE table,
 tUINT32         key
)
{
  tSDP_IOCB iocb = NULL;
  tINT32 counter;

  TS_CHECK_NULL(table, NULL);

  for (counter = 0, iocb = table->head;
       counter < table->size;
       counter++, iocb = iocb->next) {

    if (iocb->key == key) {

      return iocb;
    } /* if */
  } /* for */

  return NULL;
} /* tsSdpConnIocbTableLookup */

/* ========================================================================= */
/*..tsSdpConnIocbCreate - create an IOCB object */
tSDP_IOCB tsSdpConnIocbCreate
(
 void
)
{
  tSDP_IOCB iocb;

  TS_CHECK_NULL(_tsSdpConnIocbCache, NULL);

  iocb = kmem_cache_alloc(_tsSdpConnIocbCache, SLAB_ATOMIC);
  if (NULL != iocb) {

    memset(iocb, 0, sizeof(tSDP_IOCB_STRUCT));
    /*
     * non-zero initialization
     */
    iocb->key = TS_SDP_IOCB_KEY_INVALID;

    iocb->type    = TS_SDP_GENERIC_TYPE_IOCB;
    iocb->release = (tSDP_GENERIC_DESTRUCT_FUNC)tsSdpConnIocbDestroy;
  } /* if */

  return iocb;
} /* tsSdpConnIocbCreate */

/* ========================================================================= */
/*..tsSdpConnIocbDestroy - destroy an IOCB object */
tINT32 tsSdpConnIocbDestroy
(
 tSDP_IOCB iocb
)
{
  TS_CHECK_NULL(iocb, -EINVAL);

  if (NULL != iocb->next ||
      NULL != iocb->prev) {

    return -EACCES;
  } /* if */
  /*
   * page array
   */
  if (NULL != iocb->page_array) {

    kfree(iocb->page_array);
  } /* if */
  /*
   * clear IOCB to check for usage after free...
   */
#if 0
  memset(iocb, 0, sizeof(tSDP_IOCB_STRUCT));
#endif
  /*
   * return the object to its cache
   */
  kmem_cache_free(_tsSdpConnIocbCache, iocb);

  return 0;
} /* tsSdpConnIocbDestroy */

/* ========================================================================= */
/*..tsSdpConnIocbTableLook - get, without removing, the object at the head */
tSDP_IOCB tsSdpConnIocbTableLook
(
 tSDP_IOCB_TABLE table
)
{
  TS_CHECK_NULL(table, NULL);

  return table->head;
} /* tsSdpConnIocbTableLook */


/* ========================================================================= */
/*..tsSdpConnIocbTableGetKey - find an iocb based on key, and remove it */
tSDP_IOCB tsSdpConnIocbTableGetKey
(
 tSDP_IOCB_TABLE table,
 tUINT32         key
)
{
  tSDP_IOCB iocb;
  tINT32 result;

  iocb = tsSdpConnIocbTableLookup(table, key);
  if (NULL == iocb) {

    goto done;
  } /* if */

  result = tsSdpConnIocbTableRemove(iocb);
  if (0 > result) {

    iocb = NULL;
  } /* if */

done:
  return iocb;
} /* tsSdpConnIocbTableGetKey */

/* ========================================================================= */
/*.._tsSdpConnIocbTableGet - get, and remove, the object at the tables head */
static tSDP_IOCB _tsSdpConnIocbTableGet
(
 tSDP_IOCB_TABLE table,
 tBOOLEAN        head
)
{
  tSDP_IOCB iocb;
  tSDP_IOCB next;
  tSDP_IOCB prev;

  TS_CHECK_NULL(table, NULL);

  if (NULL == table->head) {

    return NULL;
  } /* if */

  if (TRUE == head) {

    iocb = table->head;
  } /* if */
  else {

    iocb = table->head->prev;
  } /* else */

  if (iocb->next == iocb &&
      iocb->prev == iocb) {

    table->head = NULL;
  } /* if */
  else {

    next = iocb->next;
    prev = iocb->prev;
    next->prev = prev;
    prev->next = next;

    table->head = next;
  } /* else */

  table->size--;

  iocb->table = NULL;
  iocb->next  = NULL;
  iocb->prev  = NULL;

  return iocb;
} /* tsSdpConnIocbTableGet */

/* ========================================================================= */
/*.._tsSdpConnIocbTablePut - put the IOCB object at the tables tail */
tINT32 _tsSdpConnIocbTablePut
(
 tSDP_IOCB_TABLE table,
 tSDP_IOCB       iocb,
 tBOOLEAN        head
)
{
  tSDP_IOCB next;
  tSDP_IOCB prev;

  TS_CHECK_NULL(table, -EINVAL);
  TS_CHECK_NULL(iocb, -EINVAL);

  if (NULL == table->head) {

    iocb->next  = iocb;
    iocb->prev  = iocb;
    table->head = iocb;
  } /* if */
  else {

    next = table->head;
    prev = next->prev;

    prev->next = iocb;
    iocb->prev = prev;
    iocb->next = next;
    next->prev = iocb;

    if (TRUE == head) {
      table->head = iocb;
    }
  } /* else */

  table->size++;

  iocb->table = table;

  return 0;
} /* _tsSdpConnIocbTablePut */

/* ========================================================================= */
/*..tsSdpConnIocbTableGetTail - get an IOCB object from the tables tail */
tSDP_IOCB tsSdpConnIocbTableGetTail
(
 tSDP_IOCB_TABLE table
)
{
  return _tsSdpConnIocbTableGet(table, FALSE);
} /* tsSdpConnIocbTableGetTail */

/* ========================================================================= */
/*..tsSdpConnIocbTableGetHead - get an IOCB object from the tables head */
tSDP_IOCB tsSdpConnIocbTableGetHead
(
 tSDP_IOCB_TABLE table
)
{
  return _tsSdpConnIocbTableGet(table, TRUE);
} /* tsSdpConnIocbTableGetHead */

/* ========================================================================= */
/*..tsSdpConnIocbTablePutTail - put the IOCB object at the tables tail */
tINT32 tsSdpConnIocbTablePutTail
(
 tSDP_IOCB_TABLE table,
 tSDP_IOCB       iocb
)
{
  return _tsSdpConnIocbTablePut(table, iocb, FALSE);
} /* tsSdpConnIocbTablePutTail */

/* ========================================================================= */
/*..tsSdpConnIocbTablePutHead - put the IOCB object at the tables head */
tINT32 tsSdpConnIocbTablePutHead
(
 tSDP_IOCB_TABLE table,
 tSDP_IOCB       iocb
)
{
  return _tsSdpConnIocbTablePut(table, iocb, TRUE);
} /* tsSdpConnIocbTablePutHead */

/* ========================================================================= */
/*..tsSdpConnIocbTableCancel -- cancel all outstanding AIOs in a queue */
tINT32 tsSdpConnIocbTableCancel
(
 tSDP_IOCB_TABLE table,
 tUINT32         mask,
 tINT32          comp
)
{
  tSDP_IOCB iocb;
  tSDP_IOCB next;
  tINT32 counter;
  tINT32 result;
  tINT32 total;

  TS_CHECK_NULL(table, -EINVAL);
  /*
   * loop through IOCBs, completing each one with either a partial data
   * result, or a cancelled error.
   */
  for (counter = 0, iocb = table->head, total = table->size;
       counter < total;
       counter++) {

    next = iocb->next;

    if (0 < (mask & iocb->flags) ||
	TS_SDP_IOCB_F_ALL == mask) {

      TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_WARN,
	       "IOCB: Cancel <%d> iocb <%d> flags <%04x> of size <%d:%d:%d>",
	       comp, iocb->key, iocb->flags, iocb->size,
	       iocb->post, iocb->len);

      result = tsSdpConnIocbTableRemove(iocb);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));

      result = tsSdpConnIocbComplete(iocb, comp);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));
    } /* if */

    iocb = next;
  } /* for */

  return 0;
} /* tsSdpConnIocbTableCancel */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* public table functions                                                */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpConnIocbTableCreate - create an IOCB table */
tSDP_IOCB_TABLE tsSdpConnIocbTableCreate
(
 tINT32  *result
)
{
  tSDP_IOCB_TABLE table = NULL;

  TS_CHECK_NULL(result, NULL);
  *result = -EINVAL;
  TS_CHECK_NULL(_tsSdpConnIocbTable, NULL);

  table = kmem_cache_alloc(_tsSdpConnIocbTable, SLAB_ATOMIC);
  if (NULL == table) {

    *result = -ENOMEM;
    return NULL;
  } /* if */

  table->head = NULL;
  table->size = 0;

  *result = 0;
  return table;
} /* tsSdpConnIocbTableCreate */

/* ========================================================================= */
/*..tsSdpConnIocbTableInit - initialize a new empty IOCB table */
tINT32 tsSdpConnIocbTableInit
(
 tSDP_IOCB_TABLE table
)
{
  TS_CHECK_NULL(_tsSdpConnIocbCache, -EINVAL);
  TS_CHECK_NULL(table, -EINVAL);

  table->head = NULL;
  table->size = 0;

  return 0;
} /* tsSdpConnIocbTableInit */

/* ========================================================================= */
/*..tsSdpConnIocbTableClear - clear the contents of an IOCB table */
tINT32 tsSdpConnIocbTableClear
(
 tSDP_IOCB_TABLE table
)
{
  tSDP_IOCB iocb;
  tINT32 result;

  TS_CHECK_NULL(_tsSdpConnIocbCache, -EINVAL);
  TS_CHECK_NULL(table, -EINVAL);
  /*
   * drain the table of any objects
   */
  while (NULL != (iocb = tsSdpConnIocbTableGetHead(table))) {

    result = tsSdpConnIocbDestroy(iocb);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  } /* while */

  return 0;
} /* tsSdpConnIocbTableClear */

/* ========================================================================= */
/*..tsSdpConnIocbTableDestroy - destroy an IOCB table */
tINT32 tsSdpConnIocbTableDestroy
(
 tSDP_IOCB_TABLE table
)
{
  tINT32 result;

  TS_CHECK_NULL(_tsSdpConnIocbTable, -EINVAL);
  TS_CHECK_NULL(table, -EINVAL);
  /*
   * drain the table of any objects
   */
  result = tsSdpConnIocbTableClear(table);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * return the table to the cache
   */
  kmem_cache_free(_tsSdpConnIocbTable, table);

  return 0;
} /* tsSdpConnIocbTableDestroy */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* primary initialization/cleanup functions                              */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpConnIocbMainInit -- initialize the advertisment caches. */
tINT32 tsSdpConnIocbMainInit
(
 void
)
{
  tINT32 result;

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "INIT: IOCB cache initialization.");
  /*
   * initialize kernel mapping space for IRQ mappings.
   */
  __tsSdpKmapInit();
  /*
   * initialize the caches only once.
   */
  if (NULL != _tsSdpConnIocbCache ||
      NULL != _tsSdpConnIocbTable) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "INIT: IOCB caches already initialized.");
    return -EINVAL;
  } /* if */

  _tsSdpConnIocbCache = kmem_cache_create("SdpIocbCache",
					  sizeof(tSDP_IOCB_STRUCT),
					  0, SLAB_HWCACHE_ALIGN, NULL, NULL);
  if (NULL == _tsSdpConnIocbCache) {

    result = -ENOMEM;
    goto error_iocb_c;
  } /* if */

  _tsSdpConnIocbTable = kmem_cache_create("SdpIocbTable",
					  sizeof(tSDP_IOCB_TABLE_STRUCT),
					  0, SLAB_HWCACHE_ALIGN, NULL, NULL);
  if (NULL == _tsSdpConnIocbTable) {

    result = -ENOMEM;
    goto error_iocb_t;
  } /* if */

  return 0;
error_iocb_t:
  kmem_cache_destroy(_tsSdpConnIocbCache);
  _tsSdpConnIocbCache = NULL;
error_iocb_c:
  return result;
} /* tsSdpConnIocbMainInit */

/* ========================================================================= */
/*..tsSdpConnIocbMainCleanup -- cleanup the advertisment caches. */
tINT32 tsSdpConnIocbMainCleanup
(
 void
)
{
  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "INIT: IOCB cache cleanup.");
  /*
   * cleanup the caches
   */
  kmem_cache_destroy(_tsSdpConnIocbCache);
  kmem_cache_destroy(_tsSdpConnIocbTable);
  /*
   * null out entries.
   */
  _tsSdpConnIocbCache = NULL;
  _tsSdpConnIocbTable = NULL;

  return 0;
} /* tsSdpConnIocbMainCleanup */
