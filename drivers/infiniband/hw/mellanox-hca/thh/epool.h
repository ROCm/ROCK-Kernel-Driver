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

#if !defined(_EPOOL_H)
#define _EPOOL_H

#if !defined(EPOOL_TEST)
# include <mosal.h>
#endif

#define EPOOL_NULL ((const u_int32_t) ~0ul)

typedef struct
{  
  /* all in bytes */
  unsigned int   entry_size;          /* size of user entry structure    */
                                      /* offsets within user entry to:  */
  unsigned int   prev_struct_offset;  /*   unsigned long  'prev' index   */
  unsigned int   next_struct_offset;  /*   unsigned long  'next' index   */
} EPool_meta_t;

typedef struct
{  
  void*                entries; /* allocated by user */
  unsigned long        size;    /* number of entries */
  const EPool_meta_t*  meta;    /* can be shared */
  unsigned long        head;    /* used internally */
#if !defined(EPOOL_TEST)
  MOSAL_spinlock_t     spinlock;     /* used internally */
#endif
} EPool_t;

#ifdef  __cplusplus
 extern "C" {
#endif

/*  Notes:
 *    
 *  + The complexity of 
 *    epool_alloc(.), epool_free(..)
 *    is constant O(1).
 *  + The complexity of epool_reserve(...) is
 *       O(res_size) where n is the size of the free list
 *  + The complexity of epool_unreserve(...) is O(res_size).
 */ 

/*  Function:  epool_init
 *  Arguments:
 *    l - EPool_t to initialize. User is reponsible to pre-allocate
 *        l->entries, and set l->size and l->meta accordingly.
 *  Return:
 *    None.
 */
extern void  epool_init(EPool_t* l);

/*  Function:  epool_cleanup
 *  Arguments:
 *    l - EPool_t to clean. Note that entries are not freed.
 *  Return:
 *    None.
 *  Currently does noting.
 */
extern void  epool_cleanup(EPool_t* l);

/*  Function:  epool_alloc
 *  Arguments:
 *    l - EPool_t to get a free index from.
 *  Return:
 *    An index within l->entries, or EPOOL_NULL.
 */
extern unsigned long epool_alloc(EPool_t* l); /* EPOOL_NULL iff fail */

/*  Function:  epool_free
 *  Arguments:
 *    l -     EPool_t to free an index to.
 *    index - Index to free.
 *  Return:
 *    None.
 */
extern void  epool_free(EPool_t* l,  unsigned long index);

/*  Function:  epool_reserve
 *  Arguments:
 *    l -           EPool_t from which to reserve range of indices.
 *    start_index - start of indices range.
 *    size -        size of range.
 *  Return:
 *    0 for success, or number of un-reserved indices.
 */
extern unsigned long  epool_reserve(
  EPool_t*       l,
  unsigned long  start_index,
  unsigned long  res_size);

/*  Function:  epool_unreserve
 *  Arguments:
 *    l -           EPool_t from which to unreserve range of indices.
 *    start_index - start of indices range.
 *    size -        size of range.
 *  Return:
 *    None.
 *
 *  Notes: It is the responsiblity of the caller to provide valid range.
 */
extern void  epool_unreserve(
  EPool_t*       l,
  unsigned long  start_index,
  unsigned long  res_size);

#ifdef  __cplusplus
 }
#endif

#endif /* _EPOOL_H */
