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

  $Id: sdp_queue.c 32 2004-04-09 03:57:42Z roland $
*/

#include "sdp_main.h"

static kmem_cache_t *_tsSdpGenericTable = NULL;

/* --------------------------------------------------------------------- */
/*                                                                       */
/* module specific functions                                             */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*.._tsSdpGenericTableGet - Get an element from a specific table */
static tSDP_GENERIC _tsSdpGenericTableGet
(
 tSDP_GENERIC_TABLE table, /* table from which to get the elemente */
 tBOOLEAN           fifo   /* retrieve data in fifo order */
)
{
  tSDP_GENERIC element;

  TS_CHECK_NULL(table, NULL);

  if (NULL == table->head) {

    return NULL;
  } /* if */

  if (TRUE == fifo) {

    element = table->head;
  } /* if */
  else {

    element = table->head->prev;
  } /* else */

  if (element->next == element &&
      element->prev == element) {

    table->head = NULL;
  } /* if */
  else {

    element->next->prev = element->prev;
    element->prev->next = element->next;

    table->head = element->next;
  } /* else */

  table->size--;
  table->count[element->type] -= ((TS_SDP_GENERIC_TYPE_NONE > element->type) ?
				  1 : 0);

  element->next = NULL;
  element->prev = NULL;
  element->table = NULL;

  return element;
} /* _tsSdpGenericTableGet */

/* ========================================================================= */
/*.._tsSdpGenericTablePut - Place an element into a specific table */
static __inline__ tINT32 _tsSdpGenericTablePut
(
 tSDP_GENERIC_TABLE table,   /* table into which the element is placed */
 tSDP_GENERIC       element,
 tBOOLEAN  fifo              /* false == tail, true == head */
)
{
  TS_CHECK_NULL(table, -EINVAL);
  TS_CHECK_NULL(element, -EINVAL);

  if (NULL != element->table) {

    return -EINVAL;
  } /* if */

  if (NULL == table->head) {

    element->next = element;
    element->prev = element;
    table->head = element;
  } /* if */
  else {

    element->next = table->head;
    element->prev = table->head->prev;

    element->next->prev = element;
    element->prev->next = element;

    if (TRUE == fifo) {
      table->head = element;
    }
  } /* else */

  table->size++;
  table->count[element->type] += ((TS_SDP_GENERIC_TYPE_NONE > element->type) ?
				  1 : 0);
  element->table = table;

  return 0;
} /* _tsSdpGenericTablePut */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* public advertisment object functions for FIFO object table            */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpGenericTableRemove - remove a specific element from a table */
tINT32 tsSdpGenericTableRemove
(
 tSDP_GENERIC element
)
{
  tSDP_GENERIC_TABLE table;
  tSDP_GENERIC       prev;
  tSDP_GENERIC       next;

  TS_CHECK_NULL(element, -EINVAL);
  TS_CHECK_NULL(element->table, -EINVAL);
  TS_CHECK_NULL(element->next, -EINVAL);
  TS_CHECK_NULL(element->prev, -EINVAL);

  table = element->table;

  if (element->next == element &&
      element->prev == element) {

    table->head = NULL;
  } /* if */
  else {

    next = element->next;
    prev = element->prev;
    next->prev = prev;
    prev->next = next;

    if (table->head == element) {

      table->head = next;
    } /* if */
  } /* else */

  table->size--;
  table->count[element->type] -= ((TS_SDP_GENERIC_TYPE_NONE > element->type) ?
				  1 : 0);

  element->table = NULL;
  element->next = NULL;
  element->prev = NULL;

  return 0;
} /* tsSdpGenericTableRemove */

/* ========================================================================= */
/*..tsSdpGenericTableLookup - search and return an element from the table */
tSDP_GENERIC tsSdpGenericTableLookup
(
 tSDP_GENERIC_TABLE       table,
 tSDP_GENERIC_LOOKUP_FUNC lookup_func,
 tPTR                     arg
)
{
  tSDP_GENERIC element;
  tINT32 counter;

  TS_CHECK_NULL(table, NULL);
  TS_CHECK_NULL(lookup_func, NULL);

  for (counter = 0, element = table->head;
       counter < table->size;
       counter++, element = element->next) {

    if (0 == lookup_func(element, arg)) {

      return element;
    } /* if */
  } /* return */

  return NULL;
} /* tsSdpGenericTableLookup */

/* ========================================================================= */
/*..tsSdpGenericTableGetAll - Get the element at the front of the table */
tSDP_GENERIC tsSdpGenericTableGetAll
(
 tSDP_GENERIC_TABLE table  /* table from which to get the element */
)
{
  tSDP_GENERIC head;

  head = table->head;

  table->head = NULL;
  table->size = 0;

  memset(table->count, 0, sizeof(table->count));

  return head;
} /* tsSdpGenericTableGetAll */

/* ========================================================================= */
/*..tsSdpGenericTableGetHead - Get the element at the front of the table */
tSDP_GENERIC tsSdpGenericTableGetHead
(
 tSDP_GENERIC_TABLE table  /* table from which to get the element */
)
{
  return _tsSdpGenericTableGet(table, TRUE);
} /* tsSdpGenericTableGetHead */

/* ========================================================================= */
/*..tsSdpGenericTableGetTail - Get the element at the end of the table */
tSDP_GENERIC tsSdpGenericTableGetTail
(
 tSDP_GENERIC_TABLE table  /* table from which to get the element */
)
{
  return _tsSdpGenericTableGet(table, FALSE);
} /* tsSdpGenericTableGetTail */

/* ========================================================================= */
/*..tsSdpGenericTablePutHead - Place an element into the head of a table */
tINT32 tsSdpGenericTablePutHead
(
 tSDP_GENERIC_TABLE table,  /* table into which the element is placed */
 tSDP_GENERIC element
)
{
  return _tsSdpGenericTablePut(table, element, TRUE);
} /* tsSdpGenericTablePutHead */

/* ========================================================================= */
/*..tsSdpGenericTablePutTail - Place an element into the tail of a table */
tINT32 tsSdpGenericTablePutTail
(
 tSDP_GENERIC_TABLE table,  /* table into which the element is placed */
 tSDP_GENERIC element
)
{
  return _tsSdpGenericTablePut(table, element, FALSE);
} /* tsSdpGenericTablePutTail */

/* ========================================================================= */
/*..tsSdpGenericTableLookHead - look at the front of the table */
tSDP_GENERIC tsSdpGenericTableLookHead
(
 tSDP_GENERIC_TABLE table  /* table from which to get the element */
)
{
  TS_CHECK_NULL(table, NULL);

  return table->head;
} /* tsSdpGenericTableLookHead */

/* ========================================================================= */
/*..tsSdpGenericTableLookTail - look at the end of the table */
tSDP_GENERIC tsSdpGenericTableLookTail
(
 tSDP_GENERIC_TABLE table  /* table from which to get the element */
)
{
  TS_CHECK_NULL(table, NULL);

  return ((NULL == table->head) ? NULL : table->head->prev);
} /* tsSdpGenericTableLookTail */

/* ========================================================================= */
/*..tsSdpGenericTableTypeHead - look at the type at the front of the table */
tINT32 tsSdpGenericTableTypeHead
(
 tSDP_GENERIC_TABLE table  /* table from which to get the element */
)
{
  TS_CHECK_NULL(table, -EINVAL);

  if (NULL == table->head) {

    return TS_SDP_GENERIC_TYPE_NONE;
  } /* if */
  else {

    return table->head->type;
  } /* else */
} /* tsSdpGenericTableTypeHead */

/* ========================================================================= */
/*..tsSdpGenericTableTypeTail - look at the type at the end of the table */
tINT32 tsSdpGenericTableTypeTail
(
 tSDP_GENERIC_TABLE table  /* table from which to get the element */
)
{
  TS_CHECK_NULL(table, -EINVAL);

  if (NULL == table->head) {

    return TS_SDP_GENERIC_TYPE_NONE;
  } /* if */
  else {

    return table->head->prev->type;
  } /* else */
} /* tsSdpGenericTableTypeTail */

/* ========================================================================= */
/*..tsSdpGenericTableLookTypeHead - look at a specific object */
tSDP_GENERIC tsSdpGenericTableLookTypeHead
(
 tSDP_GENERIC_TABLE table,
 tSDP_GENERIC_TYPE  type
)
{
  TS_CHECK_NULL(table, NULL);

  if (NULL == table->head) {

    return NULL;
  } /* if */
  else {

    return ((type == table->head->type) ? table->head : NULL);
  } /* else */
} /* tsSdpGenericTableLookTypeHead */

/* ========================================================================= */
/*..tsSdpGenericTableLookTypeTail - look at the type at the end of the table */
tSDP_GENERIC tsSdpGenericTableLookTypeTail
(
 tSDP_GENERIC_TABLE table,
 tSDP_GENERIC_TYPE  type
)
{
  TS_CHECK_NULL(table, NULL);

  if (NULL == table->head) {

    return NULL;
  } /* if */
  else {

    return ((type == table->head->prev->type) ? table->head->prev : NULL);
  } /* else */
} /* tsSdpGenericTableLookTypeTail */

/* ========================================================================= */
/*..tsSdpGenericTableTypeSize - return the number of elements in the table */
tINT32 tsSdpGenericTableTypeSize
(
 tSDP_GENERIC_TABLE table,
 tSDP_GENERIC_TYPE  type
)
{
  TS_CHECK_NULL(table, -EINVAL);

  return ((TS_SDP_GENERIC_TYPE_NONE > type) ? table->count[type] : -ERANGE);
} /* tsSdpGenericTableTypeSize */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* public table functions                                                */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpGenericTableCreate - create/allocate a generic table */
tSDP_GENERIC_TABLE tsSdpGenericTableCreate
(
 tINT32  *result
)
{
  tSDP_GENERIC_TABLE table = NULL;

  TS_CHECK_NULL(result, NULL);
  *result = -EINVAL;
  TS_CHECK_NULL(_tsSdpGenericTable, NULL);

  table = kmem_cache_alloc(_tsSdpGenericTable, SLAB_ATOMIC);
  if (NULL == table) {

    *result = -ENOMEM;
    return NULL;
  } /* if */

  table->head = NULL;
  table->size = 0;

  memset(table, 0, sizeof(tSDP_GENERIC_TABLE_STRUCT));

  *result = 0;
  return table;
} /* tsSdpGenericTableCreate */

/* ========================================================================= */
/*..tsSdpGenericTableInit - initialize a new empty generic table */
tINT32 tsSdpGenericTableInit
(
 tSDP_GENERIC_TABLE table
)
{
  TS_CHECK_NULL(table, -EINVAL);

  table->head = NULL;
  table->size = 0;

  memset(table, 0, sizeof(tSDP_GENERIC_TABLE_STRUCT));

  return 0;
} /* tsSdpGenericTableInit */

/* ========================================================================= */
/*..tsSdpGenericTableClear - clear the contents of a generic table */
tINT32 tsSdpGenericTableClear
(
 tSDP_GENERIC_TABLE table
)
{
  tSDP_GENERIC_DESTRUCT_FUNC release_func;
  tSDP_GENERIC element;
  tINT32 result;

  TS_CHECK_NULL(table, -EINVAL);
  /*
   * drain the table of any objects
   */
  while (NULL != (element = tsSdpGenericTableGetHead(table))) {

    release_func = element->release;

    if (NULL != release_func) {

      result = release_func(element);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));
    } /* if */
  } /* while */

  return 0;
} /* tsSdpGenericTableClear */

/* ========================================================================= */
/*..tsSdpGenericTableDestroy - destroy a generic table */
tINT32 tsSdpGenericTableDestroy
(
 tSDP_GENERIC_TABLE table
)
{
  tINT32 result;

  TS_CHECK_NULL(table, -EINVAL);
  /*
   * drain the table of any objects
   */
  result = tsSdpGenericTableClear(table);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  /*
   * return the table to the cache
   */
  kmem_cache_free(_tsSdpGenericTable, table);

  return 0;
} /* tsSdpGenericTableDestroy */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* primary initialization/cleanup functions                              */
/*                                                                       */
/* --------------------------------------------------------------------- */
/* ========================================================================= */
/*..tsSdpGenericMainInit -- initialize the generic table caches. */
tINT32 tsSdpGenericMainInit
(
 void
)
{
  tINT32 result;

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "INIT: Generic table cache initialization.");
  /*
   * initialize the caches only once.
   */
  if (NULL != _tsSdpGenericTable) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "INIT: Generic table caches already initialized.");
    return -EINVAL;
  } /* if */

  _tsSdpGenericTable = kmem_cache_create("SdpGenericTable",
					  sizeof(tSDP_GENERIC_TABLE_STRUCT),
					  0, SLAB_HWCACHE_ALIGN, NULL, NULL);
  if (NULL == _tsSdpGenericTable) {

    result = -ENOMEM;
    goto error_advt_t;
  } /* if */

  return 0;
error_advt_t:
  return 0;
} /* tsSdpGenericMainInit */

/* ========================================================================= */
/*..tsSdpGenericMainCleanup -- cleanup the generic table caches. */
tINT32 tsSdpGenericMainCleanup
(
 void
)
{
  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "INIT: Generic table cache cleanup.");
  /*
   * cleanup the caches
   */
  kmem_cache_destroy(_tsSdpGenericTable);
  /*
   * null out entries.
   */
  _tsSdpGenericTable = NULL;

  return 0;
} /* tsSdpGenericMainCleanup */
