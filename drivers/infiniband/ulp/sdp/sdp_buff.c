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

  $Id: sdp_buff.c 32 2004-04-09 03:57:42Z roland $
*/

#include "sdp_main.h"

static char _main_pool_name[] = TS_SDP_MAIN_POOL_NAME;
static tSDP_MAIN_POOL main_pool = NULL;
/*
 * data buffers managment API
 */
/* ========================================================================= */
/*.._tsSdpBuffPoolGet - Get a buffer from a specific pool */
static __inline__ tSDP_BUFF _tsSdpBuffPoolGet
(
 tSDP_POOL           pool,      /* buffer pool from which to get the buffer */
 tBOOLEAN            fifo,      /* retrieve data in fifo order */
 tSDP_BUFF_TEST_FUNC test_func, /* optional test function */
 tPTR                usr_arg    /* optional argument for test function */
)
{
  tSDP_BUFF buff;

  TS_CHECK_NULL(pool, NULL);

  if (NULL == pool->head) {

    return NULL;
  } /* if */

  if (TRUE == fifo) {

    buff = pool->head;
  } /* if */
  else {

    buff = pool->head->prev;
  } /* else */

  if (NULL == test_func ||
      0    == test_func(buff, usr_arg)) {

    if (buff->next == buff &&
	buff->prev == buff) {

      pool->head = NULL;
    } /* if */
    else {

      buff->next->prev = buff->prev;
      buff->prev->next = buff->next;

      pool->head = buff->next;
    } /* else */

    pool->size--;

    buff->next = NULL;
    buff->prev = NULL;
    buff->pool = NULL;
  } /* if */
  else {

    buff = NULL;
  } /* else */

  return buff;
} /* _tsSdpBuffPoolGet */

/* ========================================================================= */
/*.._tsSdpBuffPoolPut - Place a buffer into a specific pool */
static __inline__ tINT32 _tsSdpBuffPoolPut
(
 tSDP_POOL pool,  /* buffer pool into which the buffer is placed */
 tSDP_BUFF buff,
 tBOOLEAN  fifo   /* false == tail, true == head */
)
{
  TS_CHECK_NULL(pool, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  if (NULL != buff->pool) {

    return -EINVAL;
  } /* if */

  if (NULL == pool->head) {

    buff->next = buff;
    buff->prev = buff;
    pool->head = buff;
  } /* if */
  else {

    buff->next = pool->head;
    buff->prev = pool->head->prev;

    buff->next->prev = buff;
    buff->prev->next = buff;

    if (TRUE == fifo) {
      pool->head = buff;
    }
  } /* else */

  pool->size++;
  buff->pool = pool;

  return 0;
} /* _tsSdpBuffPoolPut */

/* ========================================================================= */
/*.._tsSdpBuffPoolLook - look at a buffer from a specific pool */
static __inline__ tSDP_BUFF _tsSdpBuffPoolLook
(
 tSDP_POOL           pool,     /* buffer pool from which to get the buffer */
 tBOOLEAN            fifo      /* retrieve data in fifo order */
)
{
  TS_CHECK_NULL(pool, NULL);

  if (NULL == pool->head ||
      TRUE == fifo) {

    return pool->head;
  } /* if */
  else {

    return pool->head->prev;
  } /* else */
} /* _tsSdpBuffPoolLook */

/* ========================================================================= */
/*.._tsSdpBuffPoolRemove - remove a specific buffer from a specific pool */
static __inline__ tINT32 _tsSdpBuffPoolRemove
(
 tSDP_POOL pool,
 tSDP_BUFF buff
)
{
  tSDP_BUFF prev;
  tSDP_BUFF next;

  TS_CHECK_NULL(pool, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);
  TS_CHECK_NULL(buff->next, -EINVAL);
  TS_CHECK_NULL(buff->prev, -EINVAL);
  TS_CHECK_NULL(buff->pool, -EINVAL);

  if (pool != buff->pool) {

    return -EINVAL;
  } /* if */

  if (buff->next == buff &&
      buff->prev == buff) {

    pool->head = NULL;
  } /* if */
  else {

    next = buff->next;
    prev = buff->prev;
    next->prev = prev;
    prev->next = next;

    if (pool->head == buff) {

      pool->head = next;
    } /* if */
  } /* else */

  pool->size--;

  buff->pool = NULL;
  buff->next = NULL;
  buff->prev = NULL;

  return 0;
} /* _tsSdpBuffPoolRemove */

/* ========================================================================= */
/*..tsSdpBuffPoolInit - Init a pool drawing its buffers from the main pool */
tINT32 tsSdpBuffPoolInit
(
 tSDP_POOL pool,
 tSTR      name,
 tUINT32   size  /* number of buffer in this pool of buffers */
)
{
  tUINT32  counter;
  tINT32   result;

  TS_CHECK_NULL(pool, -EINVAL);
  TS_CHECK_NULL(main_pool, -EINVAL);

  if (TS_SDP_POOL_NAME_MAX < strlen(name)) {

    return -ENAMETOOLONG;
  } /* if */

#ifdef _TS_SDP_DEBUG_POOL_NAME
  pool->name = name;
#endif
  pool->head = NULL;
  pool->size = 0;

  for (counter = 0; counter < size; counter++) {

    result = tsSdpBuffPoolPut(pool, tsSdpBuffMainGet());
    if (0 > result) {

      result = -ENOMEM;
      goto error;
    } /* if */
  } /* for */

  return 0;
error:
  (void)tsSdpBuffPoolClear(pool);
  return result;
} /* tsSdpBuffPoolInit */

/* ========================================================================= */
/*..tsSdpBuffPoolRemove - remove a specific buffer from a specific pool */
tINT32 tsSdpBuffPoolRemove
(
 tSDP_BUFF buff
)
{
  tSDP_POOL pool;
  tINT32 result;

  TS_CHECK_NULL(buff, -EINVAL);
  TS_CHECK_NULL(buff->pool, -EINVAL);

  pool = buff->pool;

  result = _tsSdpBuffPoolRemove(pool, buff);

  return result;
} /* tsSdpBuffPoolRemove */

/* ========================================================================= */
/*..tsSdpBuffPoolGet - Get a buffer from a specific pool */
tSDP_BUFF tsSdpBuffPoolGet
(
 tSDP_POOL pool  /* buffer pool from which to get the buffer */
)
{
  tSDP_BUFF buff;

  buff = _tsSdpBuffPoolGet(pool, TRUE, NULL, NULL);

  return buff;
} /* tsSdpBuffPoolGet */

/* ========================================================================= */
/*..tsSdpBuffPoolGetHead - Get the buffer at the front of the pool */
tSDP_BUFF tsSdpBuffPoolGetHead
(
 tSDP_POOL pool  /* buffer pool from which to get the buffer */
)
{
  tSDP_BUFF buff;

  buff = _tsSdpBuffPoolGet(pool, TRUE, NULL, NULL);

  return buff;
} /* tsSdpBuffPoolGetHead */

/* ========================================================================= */
/*..tsSdpBuffPoolGetTail - Get the buffer at the end of the pool */
tSDP_BUFF tsSdpBuffPoolGetTail
(
 tSDP_POOL pool  /* buffer pool from which to get the buffer */
)
{
  tSDP_BUFF buff;

  buff = _tsSdpBuffPoolGet(pool, FALSE, NULL, NULL);

  return buff;
} /* tsSdpBuffPoolGetTail */

/* ========================================================================= */
/*..tsSdpBuffPoolLookHead - look at the buffer at the front of the pool */
tSDP_BUFF tsSdpBuffPoolLookHead
(
 tSDP_POOL pool  /* buffer pool from which to get the buffer */
)
{
  tSDP_BUFF buff;

  buff = _tsSdpBuffPoolLook(pool, TRUE);

  return buff;
} /* tsSdpBuffPoolLookHead */

/* ========================================================================= */
/*..tsSdpBuffPoolLookTail - look at the buffer at the end of the pool */
tSDP_BUFF tsSdpBuffPoolLookTail
(
 tSDP_POOL pool  /* buffer pool from which to get the buffer */
)
{
  tSDP_BUFF buff;

  buff = _tsSdpBuffPoolLook(pool, FALSE);

  return buff;
} /* tsSdpBuffPoolLookTail */

/* ========================================================================= */
/*..tsSdpBuffPoolFetchHead - Get the pools first buffer, if the test passes */
tSDP_BUFF tsSdpBuffPoolFetchHead
(
 tSDP_POOL           pool,  /* buffer pool from which to get the buffer */
 tSDP_BUFF_TEST_FUNC test_func,
 tPTR                usr_arg
)
{
  tSDP_BUFF buff;

  buff = _tsSdpBuffPoolGet(pool, TRUE, test_func, usr_arg);

  return buff;
} /* tsSdpBuffPoolFetchHead */

/* ========================================================================= */
/*..tsSdpBuffPoolFetchTail - Get the pools last buffer, if the test passes */
tSDP_BUFF tsSdpBuffPoolFetchTail
(
 tSDP_POOL           pool,  /* buffer pool from which to get the buffer */
 tSDP_BUFF_TEST_FUNC test_func,
 tPTR                usr_arg
)
{
  tSDP_BUFF buff;

  buff = _tsSdpBuffPoolGet(pool, FALSE, test_func, usr_arg);

  return buff;
} /* tsSdpBuffPoolFetchTail */

/* ========================================================================= */
/*..tsSdpBuffPoolFetch - Get the first matching buffer from the pool */
tSDP_BUFF tsSdpBuffPoolFetch
(
 tSDP_POOL           pool,  /* buffer pool from which to get the buffer */
 tSDP_BUFF_TEST_FUNC test_func,
 tPTR                usr_arg
)
{
  tSDP_BUFF buff;
  tINT32   result = 0;
  tINT32   counter;

  TS_CHECK_NULL(pool,      NULL);
  TS_CHECK_NULL(test_func, NULL);
  /*
   * check to see if there is anything to traverse.
   */
  if (NULL != pool->head) {
    /*
     * lock to prevent corruption of table
     */
    for (counter = 0, buff = pool->head;
	 counter < pool->size;
	 counter++, buff = buff->next) {

      result = test_func(buff, usr_arg);
      if (0 < result) {

	result = _tsSdpBuffPoolRemove(pool, buff);
	TS_EXPECT(MOD_LNX_SDP, !(0 > result));

	return buff;
      } /* if */

      if (0 > result) {

	break;
      } /* if */
    } /* for */
  } /* if */

  return NULL;
} /* tsSdpBuffPoolFetchHead */

/* ========================================================================= */
/*..tsSdpBuffPoolTraverseTail - traverse buffers in pool,from the tail */
tINT32 tsSdpBuffPoolTraverseTail
(
 tSDP_POOL           pool,
 tSDP_BUFF_TRAV_FUNC trav_func,
 tPTR                usr_arg
)
{
  tSDP_BUFF buff;
  tINT32   result = 0;
  tINT32   counter;

  TS_CHECK_NULL(pool, -EINVAL);
  TS_CHECK_NULL(trav_func, -EINVAL);
  /*
   * check to see if there is anything to traverse.
   */
  if (NULL != pool->head) {
    /*
     * lock to prevent corruption of table
     */
    for (counter = 0, buff = pool->head->prev;
	 counter < pool->size;
	 counter++, buff = buff->prev) {

      result = trav_func(buff, usr_arg);
      if (0  > result) {

	break;
      } /* if */
    } /* for */
  } /* if */

  return result;
} /* tsSdpBuffPoolTraverseTail */

/* ========================================================================= */
/*..tsSdpBuffPoolTraverseaHead - traverse buffers in pool, from the head */
tINT32 tsSdpBuffPoolTraverseHead
(
 tSDP_POOL           pool,
 tSDP_BUFF_TRAV_FUNC trav_func,
 tPTR                usr_arg
)
{
  tSDP_BUFF buff;
  tINT32   result = 0;
  tINT32   counter;

  TS_CHECK_NULL(pool, -EINVAL);
  TS_CHECK_NULL(trav_func, -EINVAL);
  /*
   * check to see if there is anything to traverse.
   */
  if (NULL != pool->head) {
    /*
     * lock to prevent corruption of table
     */
    for (counter = 0, buff = pool->head;
	 counter < pool->size;
	 counter++, buff = buff->next) {

      result = trav_func(buff, usr_arg);
      if (0  > result) {

	break;
      } /* if */
    } /* for */
  } /* if */

  return result;
} /* tsSdpBuffPoolTraverseHead */

/* ========================================================================= */
/*..tsSdpBuffPoolPut - Place a buffer into a specific pool */
tINT32 tsSdpBuffPoolPut
(
 tSDP_POOL pool,  /* buffer pool into which the buffer is placed */
 tSDP_BUFF buff
)
{
  tINT32  result;

  result = _tsSdpBuffPoolPut(pool, buff, TRUE);

  return result;
} /* tsSdpBuffPoolPut */

/* ========================================================================= */
/*..tsSdpBuffPoolPutHead - Place a buffer into the head of a specific pool */
tINT32 tsSdpBuffPoolPutHead
(
 tSDP_POOL pool,  /* buffer pool into which the buffer is placed */
 tSDP_BUFF buff
)
{
  tINT32  result;

  result = _tsSdpBuffPoolPut(pool, buff, TRUE);

  return result;
} /* tsSdpBuffPoolPutHead */

/* ========================================================================= */
/*..tsSdpBuffPoolPutTail - Place a buffer into the tail of a specific pool */
tINT32 tsSdpBuffPoolPutTail
(
 tSDP_POOL pool,  /* buffer pool into which the buffer is placed */
 tSDP_BUFF buff
)
{
  tINT32  result;

  result = _tsSdpBuffPoolPut(pool, buff, FALSE);

  return result;
} /* tsSdpBuffPoolPutTail */

/* ========================================================================= */
/*..tsSdpBuffPoolClear - clear the buffers out of a specific buffer pool */
tINT32 tsSdpBuffPoolClear
(
 tSDP_POOL pool
)
{
  tINT32    result;
  tSDP_BUFF buff;

  TS_CHECK_NULL(pool, -EINVAL);

  while (NULL != (buff = _tsSdpBuffPoolGet(pool, FALSE, NULL, NULL))) {

    result = tsSdpBuffMainPut(buff);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_VERY_VERBOSE, TRACE_FLOW_INOUT,
	       "BUFF: Error <%d> returning buffer to main. <%d>",
	       result, pool->size);
    } /* if */
  } /* while */

  return 0;
} /* tsSdpBuffPoolClear */
/* --------------------------------------------------------------------- */
/*                                                                       */
/* internal data buffer pool manager                                     */
/*                                                                       */
/* --------------------------------------------------------------------- */

/* ========================================================================= */
/*.._tsSdpBuffSegmentRelease -- release buffers from the segment */
static tINT32 _tsSdpBuffSegmentRelease
(
 tSDP_MEMORY_SEGMENT mem_seg
 )
{
  TS_CHECK_NULL(mem_seg, -EINVAL);
  /*
   * loop through pages.
   */
  while (0 < mem_seg->head.size) {

    mem_seg->head.size--;
    free_page((unsigned long) mem_seg->list[mem_seg->head.size].head);
  } /* for */
  /*
   * free  descriptor page
   */
  free_page((unsigned long) mem_seg);

  return 0;
} /* _tsSdpBuffSegmentRelease */

/* ========================================================================= */
/*.._tsSdpBuffSegmentReleaseAll -- release buffers from the segment */
static tINT32 _tsSdpBuffSegmentReleaseAll
(
 tSDP_MAIN_POOL m_pool
)
{
  tSDP_MEMORY_SEGMENT mem_seg;
  tINT32 result;

  TS_CHECK_NULL(m_pool, -EINVAL);
  /*
   * loop through pages.
   */
  while (NULL != m_pool->segs) {

    mem_seg = m_pool->segs;
    m_pool->segs = mem_seg->head.next;

    m_pool->buff_cur -= mem_seg->head.size;

    result = _tsSdpBuffSegmentRelease(mem_seg);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  } /* while */

  return 0;
} /* _tsSdpBuffSegmentReleaseAll */

/* ========================================================================= */
/*.._tsSdpBuffSegmentAllocate -- allocate more buffers for the main pool */
static tSDP_MEMORY_SEGMENT _tsSdpBuffSegmentAllocate
(
 void
)
{
  tSDP_MEMORY_SEGMENT mem_seg;
  tUINT32             counter;
  tINT32              result;
  /*
   * get descriptor page
   */
  mem_seg = (tSDP_MEMORY_SEGMENT)__get_free_page(GFP_KERNEL);
  if (NULL == mem_seg) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "BUFFER: Failed to allocate descriptor page.");

    goto error;
  } /* if */
  /*
   * loop
   */
  for (counter = 0, mem_seg->head.size = 0;
       counter < TS_SDP_BUFF_COUNT;
       counter++, mem_seg->head.size++) {

    mem_seg->list[counter].head = (tPTR)__get_free_page(GFP_KERNEL);
    if (NULL == mem_seg->list[counter].head) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	       "BUFFER: Failed to allocate buffer page. <%d>", counter);

      goto error_free;
    } /* if */

    mem_seg->list[counter].end     = mem_seg->list[counter].head + PAGE_SIZE;
    mem_seg->list[counter].data    = mem_seg->list[counter].head;
    mem_seg->list[counter].tail    = mem_seg->list[counter].head;
    mem_seg->list[counter].lkey    = 0;
    mem_seg->list[counter].real    = 0;
    mem_seg->list[counter].size    = 0;
    mem_seg->list[counter].u_id    = 0;
    mem_seg->list[counter].pool    = NULL;
    mem_seg->list[counter].type    = TS_SDP_GENERIC_TYPE_BUFF;
    mem_seg->list[counter].release = ((tSDP_GENERIC_DESTRUCT_FUNC)
				      tsSdpBuffMainPut);
  } /* for */
  /*
   * return segment
   */
  return mem_seg;
error_free:
  result = _tsSdpBuffSegmentRelease(mem_seg);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));
error:
  return NULL;
} /* _tsSdpBuffSegmentAllocate */

/* ========================================================================= */
/*.._tsSdpBuffMainAllocate -- allocate more buffers for the main pool */
static tINT32 _tsSdpBuffMainAllocate
(
 tSDP_MAIN_POOL m_pool,
 tUINT32        size
 )
{
  tSDP_MEMORY_SEGMENT head_seg = NULL;
  tSDP_MEMORY_SEGMENT mem_seg;
  tUINT32             counter = 0;
  tUINT32             total = 0;
  tINT32              result;

  TS_CHECK_NULL(m_pool, -EINVAL);
  /*
   * check pool limits.
   */
  if (m_pool->buff_max < (m_pool->buff_cur + size)) {

    goto error;
  } /* if */
  /*
   * first allocate the requested number of buffers. Once complete
   * place them all into the main pool.
   */
  while (total < size) {

    mem_seg = _tsSdpBuffSegmentAllocate();
    if (NULL == mem_seg) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "BUFFER: Failed to allocate segment.");

      goto error;
    } /* if */

    mem_seg->head.next = head_seg;
    head_seg           = mem_seg;

    total += mem_seg->head.size;
  } /* while */
  /*
   * insert each segment into the list, and insert each buffer into
   * the main pool
   */
  while (NULL != head_seg) {

    mem_seg = head_seg;
    head_seg = mem_seg->head.next;

    mem_seg->head.next = m_pool->segs;
    m_pool->segs = mem_seg;

    for (counter = 0; counter < mem_seg->head.size; counter++) {

      mem_seg->list[counter].u_id = m_pool->buff_cur++;

      result = tsSdpBuffPoolPut(&main_pool->pool, &mem_seg->list[counter]);
      TS_EXPECT(MOD_LNX_SDP, !(0 > result));
    } /* for */
  } /* while */

  return total;
error:

  while (NULL != head_seg) {

    mem_seg = head_seg;
    head_seg = mem_seg->head.next;

    result = _tsSdpBuffSegmentRelease(mem_seg);
    TS_EXPECT(MOD_LNX_SDP, !(0 > result));
  } /* while */

  return -ENOMEM;
} /* _tsSdpBuffMainAllocate */

/* ========================================================================= */
/*..tsSdpBuffMainInit - Initialize the main buffer pool of memory */
tINT32 tsSdpBuffMainInit
(
 tUINT32              buff_min,
 tUINT32              buff_max
)
{
  tINT32     result;

  if (NULL != main_pool) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "BUFFER: Main pool already initialized!");
    return -EEXIST;
  } /* if */

  if (!(0 < buff_min) ||
      buff_max < buff_min) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "BUFFER: Pool allocation count error. <%d:%d>",
	     buff_min, buff_max);
    return -ERANGE;
  } /* if */
  /*
   * allocate the main pool structures
   */
  main_pool = kmalloc(sizeof(tSDP_MAIN_POOL_STRUCT), GFP_KERNEL);
  if (NULL == main_pool) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "BUFFER: Main pool initialization failed.");
    result = -ENOMEM;
    goto done;
  } /* if */

  memset(main_pool, 0, sizeof(tSDP_MAIN_POOL_STRUCT));

  main_pool->buff_size = PAGE_SIZE;
  main_pool->buff_min  = buff_min;
  main_pool->buff_max  = buff_max;

  TS_SPIN_LOCK_INIT(&main_pool->lock);

  result = tsSdpBuffPoolInit(&main_pool->pool, _main_pool_name, 0);
  TS_EXPECT(MOD_LNX_SDP, !(0 > result));

  main_pool->pool_cache = kmem_cache_create("SdpBuffPool",
					    sizeof(tSDP_POOL_STRUCT),
					    0, SLAB_HWCACHE_ALIGN,
					    NULL, NULL);
  if (NULL == main_pool->pool_cache) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "BUFFER: Failed to allocate pool cache.");
    result = -ENOMEM;
    goto error_pool;
  } /* if */
  /*
   * allocate the minimum number of buffers.
   */
  result = _tsSdpBuffMainAllocate(main_pool, buff_min);
  if (0 > result) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "BUFFER: Error <%d> allocating buffers. <%d>", result, buff_min);
    goto error_alloc;
  } /* if */
  /*
   * done
   */
  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INIT,
	   "BUFFER: Main pool initialized with min:max <%d:%d> buffers.",
	   buff_min, buff_max);

  return 0; /* success */
error_alloc:
  kmem_cache_destroy(main_pool->pool_cache);
error_pool:
  kfree(main_pool);
done:
  main_pool = NULL;
  return result;
} /* tsSdpBuffMainInit */

/* ========================================================================= */
/*..tsSdpBuffMainDestroy - Destroy the main buffer pool and free its memory */
void tsSdpBuffMainDestroy
(
 void
)
{
  if (NULL == main_pool) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "BUFFER: Main pool dosn't exist.");
    return;
  } /* if */
  /*
   * Free all the memory regions
   */
  (void)_tsSdpBuffSegmentReleaseAll(main_pool);
  /*
   * free pool cache
   */
  kmem_cache_destroy(main_pool->pool_cache);
  /*
   * free main
   */
  kfree(main_pool);
  main_pool = NULL;
  /*
   * done
   */
  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_CLEANUP,
	   "BUFFER: Main pool destroyed.");

  return;
} /* tsSdpBuffMainDestroy */

/* ========================================================================= */
/*..tsSdpBuffMainGet - Get a buffer from the main buffer pool */
tSDP_BUFF tsSdpBuffMainGet
(
 void
)
{
  tSDP_BUFF buff;
  tINT32 result;
  unsigned long flags;

  TS_CHECK_NULL(main_pool, NULL);
  /*
   * get buffer
   */
  TS_SPIN_LOCK(&main_pool->lock, flags);

  if (NULL == main_pool->pool.head) {

    result = _tsSdpBuffMainAllocate(main_pool, TS_SDP_BUFFER_COUNT_INC);
    if (0 > result) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	       "BUFFER: Error <%d> allocating buffers.", result);

      TS_SPIN_UNLOCK(&main_pool->lock, flags);
      return NULL;
    } /* if */
  } /* if */

  buff = main_pool->pool.head;

  if (buff->next == buff) {

    main_pool->pool.head = NULL;
  } /* if */
  else {

    buff->next->prev = buff->prev;
    buff->prev->next = buff->next;

    main_pool->pool.head = buff->next;
  } /* else */

  main_pool->pool.size--;

  TS_SPIN_UNLOCK(&main_pool->lock, flags);

  buff->next = NULL;
  buff->prev = NULL;
  buff->pool = NULL;
  /*
   * main pool specific reset
   */
  buff->bsdh_hdr  = NULL;
  buff->flags     = 0;
  buff->lkey      = 0;

  buff->data_size = 0;
  buff->ib_wrid   = 0;

  return buff;
} /* tsSdpBuffMainGet */

/* ========================================================================= */
/*..tsSdpBuffMainPut - Return a buffer to the main buffer pool */
tINT32 tsSdpBuffMainPut
(
 tSDP_BUFF buff
)
{
  unsigned long flags;

  TS_CHECK_NULL(main_pool, -EINVAL);

  if (NULL == buff ||
      NULL != buff->pool) {

    return -EINVAL;
  } /* if */

  if (NULL != buff->next ||
      NULL != buff->prev) {

    return -ETOOMANYREFS;
  } /* if */
  /*
   * reset pointers
   */
  buff->data = buff->head;
  buff->tail = buff->head;
  buff->pool = &main_pool->pool;

  TS_SPIN_LOCK(&main_pool->lock, flags);

  if (NULL == main_pool->pool.head) {

    buff->next           = buff;
    buff->prev           = buff;
    main_pool->pool.head = buff;
  } /* if */
  else {

    buff->next = main_pool->pool.head;
    buff->prev = main_pool->pool.head->prev;

    buff->next->prev = buff;
    buff->prev->next = buff;
  } /* else */

  main_pool->pool.size++;

  TS_SPIN_UNLOCK(&main_pool->lock, flags);

  return 0;
} /* tsSdpBuffMainPut */

/* ========================================================================= */
/*..tsSdpBuffMainChainLink - create a chain of buffers which can be returned */
tINT32 tsSdpBuffMainChainLink
(
 tSDP_BUFF  head,
 tSDP_BUFF  buff
)
{
  TS_CHECK_NULL(main_pool, -EINVAL);
  TS_CHECK_NULL(buff, -EINVAL);

  buff->data = buff->head;
  buff->tail = buff->head;
  buff->pool = &main_pool->pool;

  if (NULL == head) {

    buff->next = buff;
    buff->prev = buff;
  } /* if */
  else {

    buff->next = head;
    buff->prev = head->prev;

    buff->next->prev = buff;
    buff->prev->next = buff;
  } /* else */

  return 0;
} /* tsSdpBuffMainChainLink */

/* ========================================================================= */
/*..tsSdpBuffMainChainPut - Return a buffer to the main buffer pool */
tINT32 tsSdpBuffMainChainPut
(
 tSDP_BUFF buff,
 tINT32 count
)
{
  unsigned long flags;
  tSDP_BUFF next;
  tSDP_BUFF prev;
  /*
   * return an entire Link of buffers to the queue, this save on
   * lock contention for the buffer pool, for code paths where
   * a number of buffers are processed in a loop, before being
   * returned. (e.g. send completions, recv to userspace.
   */
  TS_CHECK_NULL(main_pool, -EINVAL);

  if (NULL == buff ||
      !(0 < count)) {

    return -EINVAL;
  } /* if */

  TS_SPIN_LOCK(&main_pool->lock, flags);

  if (NULL == main_pool->pool.head) {

    main_pool->pool.head = buff;
  } /* if */
  else {

    prev = buff->prev;
    next = main_pool->pool.head->next;

    buff->prev = main_pool->pool.head;
    main_pool->pool.head->next = buff;

    prev->next = next;
    next->prev = prev;
  } /* else */

  main_pool->pool.size += count;

  TS_SPIN_UNLOCK(&main_pool->lock, flags);

  return 0;
} /* tsSdpBuffMainChainPut */

/* ========================================================================= */
/*..tsSdpBuffMainSize - return the number of elements in the main buffer pool */
tINT32 tsSdpBuffMainSize
(
 void
)
{
  tINT32 size;
  unsigned long flags;

  if (NULL == main_pool) {

    return -1;
  } /* if */

  TS_SPIN_LOCK(&main_pool->lock, flags);
  size = tsSdpBuffPoolSize(&main_pool->pool);
  TS_SPIN_UNLOCK(&main_pool->lock, flags);

  return size;
} /* tsSdpBuffMainSize */

/* ========================================================================= */
/*..tsSdpBuffMainBuffSize - return the size of buffers in the main pool */
tINT32 tsSdpBuffMainBuffSize
(
 void
)
{
  tINT32 result;

  if (NULL == main_pool) {

    result = -1;
  } /* if */
  else {

    result = main_pool->buff_size;
  } /* else */

  return result;
} /* tsSdpBuffMainBuffSize */

/* ========================================================================= */
/*..tsSdpBuffMainDump -- place the buffer pool statistics into a file (/proc) */
tINT32 tsSdpBuffMainDump
(
 tSTR   buffer,
 tINT32 max_size,
 tINT32 start_index,
 long *end_index
)
{
  tSDP_MEMORY_SEGMENT mem_seg;
  tINT32   buff_count;
  tINT32   offset = 0;
  tINT32   counter;
  unsigned long flags;

  TS_CHECK_NULL(buffer, -EINVAL);
  /*
   * simple table read, without page boundry handling.
   */
  *end_index = 0;
  /*
   * lock the table
   */
  TS_SPIN_LOCK(&main_pool->lock, flags);

  if (0 == start_index) {

    offset += sprintf((buffer + offset), "Totals:\n");
    offset += sprintf((buffer + offset), "-------\n");

    offset += sprintf((buffer + offset), "  buffer size:         %8d\n",
		      main_pool->buff_size);
    offset += sprintf((buffer + offset), "  buffers maximum:     %8d\n",
		      main_pool->buff_max);
    offset += sprintf((buffer + offset), "  buffers minimum:     %8d\n",
		      main_pool->buff_min);
    offset += sprintf((buffer + offset), "  buffers allocated:   %8d\n",
		      main_pool->buff_cur);
    offset += sprintf((buffer + offset), "  buffers available:   %8d\n",
		      main_pool->pool.size);
    offset += sprintf((buffer + offset), "  buffers outstanding: %8d\n",
		      main_pool->buff_cur - main_pool->pool.size);
    offset += sprintf((buffer + offset), "\nBuffers:\n");
    offset += sprintf((buffer + offset), "--------\n");
    offset += sprintf((buffer + offset),
		      "     id    size pool name\n");
    offset += sprintf((buffer + offset),
		      "  -------- ---- ----------------\n");
  } /* if */
  /*
   * buffers
   */
  if (start_index < main_pool->buff_cur) {

    for (counter = 0, buff_count = 0, mem_seg = main_pool->segs;
	 NULL != mem_seg && TS_SDP_BUFF_OUT_LEN < (max_size - offset);
	 mem_seg = mem_seg->head.next) {

      for (counter = 0;
	   counter < mem_seg->head.size &&
	     TS_SDP_BUFF_OUT_LEN < (max_size-offset);
	   counter++, buff_count++) {

	if (!(start_index > buff_count)) {

	  offset += sprintf((buffer + offset),
			    "  %08x %04x %-16s\n",
			    mem_seg->list[counter].u_id,
			    (int) (mem_seg->list[counter].tail -
                                   mem_seg->list[counter].data),
#ifdef _TS_SDP_DEBUG_POOL_NAME
			    ((NULL != mem_seg->list[counter].pool) ? \
			     mem_seg->list[counter].pool->name : "<none>")
#else
			    "<off>"
#endif
			    );
	} /* if */
      } /* for */

    } /* for */

    *end_index = buff_count - start_index;
  } /* if */

  TS_SPIN_UNLOCK(&main_pool->lock, flags);

  return offset;
} /* tsSdpBuffMainDump */
