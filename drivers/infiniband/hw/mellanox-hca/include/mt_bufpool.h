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

  Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
*/

#ifndef H_MT_BUFPOOL_H
#define H_MT_BUFPOOL_H

#include <mtl_types.h>

typedef struct MT_bufpool_st *MT_bufpool_t;
typedef u_int32_t MT_bufpool_flags_t;

/*************************************************************************
 * Function: MT_bufpool_create
 *
 * Arguments:
 *  item_bsize      : Size of each item in bytes.
 *  log2_alignment  : Required alignment of each buffer 
 *  min_num_of_items: Minimum numbers of items to keep in pool (minimum concurrency).
 *  flags: Flags for buffer pool attributes (e.g., "may grow", etc. - none supporrted, yet).
 *  bufpool_hndl_p  : Returned buffer pool handle.
 *  
 * Returns:
 *  MT_OK
 * 	MT_EAGAIN: Cannot allocate memory for pool.
 *
 * Description:
 *  
 *  Allocate the required data structures for the administration of a completion queue 
 *  including completion queue buffer space which has to be large enough to be adequate to 
 *  the maximum number of entries in the completion.
 *  
 *  Completion queue entries are accessed directly by the application.
 *
 *************************************************************************/ 
call_result_t MT_bufpool_create(
  MT_size_t           item_bsize,       /*IN*/
  u_int8_t            log2_alignment,   /*IN*/
  MT_size_t           min_num_of_items, /*IN*/
  MT_bufpool_flags_t  flags,            /*IN*/
  MT_bufpool_t       *bufpool_hndl_p    /*OUT*/
); 

/*************************************************************************
 * Function: MT_bufpool_alloc
 *
 * Arguments:
 *  bufpool_hndl  : Buffer pool handle.
 *  
 * Returns:
 *  Pointer to allocated buffer of size as defined in bufpool creation
 *  (NULL on failure - e.g., wait for buffer interrupted)
 *
 * Description:
 *  Allocate a buffer of size as defined on bufpool creation. 
 *  The call may block if all buffers are currently in use, until a buffer is freed.
 *  This function may not be invoked from a context that cannot sleep (DPC, ISR)
 *
 *************************************************************************/ 
void* MT_bufpool_alloc(MT_bufpool_t bufpool_hndl);


/*************************************************************************
 * Function: MT_bufpool_free
 *
 * Arguments:
 *  bufpool_hndl  : Buffer pool handle.
 *  buf_p         : Buffer pointer (as returned from MT_bufpool_alloc)
 *  
 * Returns: (void)
 *
 * Description:
 *   Free a buffer allocated with MT_bufpool_alloc.
 *   Given buf_p must be the exact pointer returned on allocation.
 *
 *************************************************************************/ 
void MT_bufpool_free(MT_bufpool_t bufpool_hndl, void *buf_p);



/*************************************************************************
 * Function: MT_bufpool_destroy
 *
 * Arguments:
 *  bufpool_hndl  : Buffer pool handle.
 *  
 * Returns: (void)
 *
 * Description:
 *  Free the MT_bufpool object with all its buffers.
 *  Note that this call will free even buffers that were not returned to 
 *  the pool using MT_bufpool_free.
 *  NOTE: Do NOT invoke this function while invoking MT_bufpool_alloc/free
 *
 *************************************************************************/ 
void MT_bufpool_destroy(MT_bufpool_t bufpool_hndl);

#endif

