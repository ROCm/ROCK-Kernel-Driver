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

#include <epool.h>
#include <mtl_common.h>

#define  EPOOL_SAFE    1

#if defined(EPOOL_TEST)
# undef MOSAL_spinlock_irq_lock
# undef MOSAL_spinlock_unlock
# define MOSAL_spinlock_irq_lock(p)  0
# define MOSAL_spinlock_unlock(p)
# define ELSE_ERROR(f)
#else
# define ELSE_ERROR(f)  else { MTL_ERROR1("%s MOSAL_spinlock_irq_lock failed\n", f); }
#endif

typedef unsigned long  Index;
typedef Index*         Index_p;


/************************************************************************/
/************************************************************************/
/*                         private functions                            */


#if EPOOL_SAFE
/************************************************************************/
static MT_bool   index_ok(const EPool_t*  lst, Index  i)
{
  MT_bool  ok = (i < lst->size) || (i == EPOOL_NULL);
  if (!ok)
  {
    MTL_ERROR1(MT_FLFMT("Error: i=0x%lx >= size=0x%lx"), i, lst->size);
  }
  return ok;
} /* index_ok */

# define INDEX_CHECK(lst, i, msg)   if (!index_ok(lst, i)) { \
     MTL_ERROR1(MT_FLFMT("%s: index check failed."), msg); }

#else
# define INDEX_CHECK(lst, i, msg)
#endif



/************************************************************************/
/* Returns the address of the 'next' offset                             */ 
static inline Index_p  addr_next(const EPool_meta_t* meta, char* entry)
{
  return (Index_p)(entry + meta->next_struct_offset);
} /* addr_next */


/************************************************************************/
/* Returns the next value, and its memory address                       */
static inline Index get_next(const EPool_meta_t* meta, char* entry, Index_p* a)
{
  Index_p  va = addr_next(meta, entry);
  *a = va;
  return *va;
} /* get_next */


/************************************************************************/
/* Returns the address of the 'prev' offset                             */ 
static inline Index_p  addr_prev(const EPool_meta_t* meta, char* entry)
{
  return (Index_p)(entry + meta->prev_struct_offset);
} /* addr_prev */


/************************************************************************/
/* Returns the prev value, and its memory address                       */
static inline Index get_prev(const EPool_meta_t* meta, char* entry, Index_p* a)
{
  Index_p  va = addr_prev(meta, entry);
  *a = va;
  return *va;
} /* get_prev */


/************************************************************************/
/* link between 'p->prev' and 'p->next' and return next                 */
static Index   link_over(EPool_t* lst, Index  p)
{
  const EPool_meta_t*  meta = lst->meta;
  char          *entry_next, *entry_prev;
  unsigned int  entry_size = meta->entry_size;
  char*         centries = (char*)lst->entries;
  char*         entry = centries + (p * entry_size);
  Index_p       pnext, pprev;
  Index         next = get_next(meta, entry, &pnext);
  Index         prev = get_prev(meta, entry, &pprev);
  INDEX_CHECK(lst, next, "next")
  INDEX_CHECK(lst, prev, "prev")
  entry_next = centries + (next * entry_size);
  entry_prev = centries + (prev * entry_size);
  pnext = addr_next(meta, entry_prev);
  pprev = addr_prev(meta, entry_next);
  
  *pnext = next;
  *pprev = prev;
  return next;
} /* link_over */


/************************************************************************/
/************************************************************************/
/*                         interface functions                          */


/************************************************************************/
void epool_init(EPool_t* lst)
{
  const EPool_meta_t*  meta = lst->meta;
  unsigned long  i;
  unsigned int   sz = lst->size, 
                 entry_size = meta->entry_size;
  MTL_TRACE1(MT_FLFMT("{ epool_init: lst=%p, size=0x%lx"), lst, lst->size);
  lst->head = EPOOL_NULL;
#if !defined(EPOOL_TEST)
  MOSAL_spinlock_init(&lst->spinlock);
#endif
  if (sz)
  {
    char*          centry = (char*)lst->entries;
    Index_p        pnext = 0, pprev = 0; /* zero, just to silent warning */
    for (i = 0;  i != sz;  ++i, centry += entry_size)
    {
      pnext = addr_next(meta, centry);
      pprev = addr_prev(meta, centry);
      *pnext = i + 1;
      *pprev = i - 1;
    }
    /* Now fix the open ended list into cycle */
    *pnext = 0;
    pprev = addr_prev(meta, (char*)lst->entries);
    *pprev = sz - 1;
    lst->head = 0;
  }
  MTL_TRACE1(MT_FLFMT("} epool_init: lst=%p"), lst);
} /* epool_init */

/************************************************************************/
void epool_cleanup(EPool_t* lst)
{
  /* Nothing to clean */
} /* epool_cleanup */


/************************************************************************/
Index  epool_alloc(EPool_t* lst)
{
  Index  p = EPOOL_NULL;
  int    acq = MOSAL_spinlock_irq_lock(&lst->spinlock);
  MTL_TRACE1(MT_FLFMT("{ epool_alloc: lst=%p"), lst);
  if (acq == 0)
  {
    p = lst->head;
    if (p != EPOOL_NULL)
    {
      Index  next = link_over(lst, p);
      lst->head = ((next != p) ? next : EPOOL_NULL);
    }
    MOSAL_spinlock_unlock(&lst->spinlock);
  }
  ELSE_ERROR("epool_alloc")
  MTL_TRACE1(MT_FLFMT("} epool_alloc: p=0x%lx"), p);
  return p;
} /* epool_alloc */


/************************************************************************/
void  epool_free(EPool_t* lst,  Index p)
{
  const EPool_meta_t*  meta = lst->meta;
  unsigned int         entry_size = meta->entry_size;
  char*                centries = (char*)lst->entries;
  char*                entry = centries + (p * entry_size);
  Index_p              pprev, pnext;
  Index                next;
  int                  acq = MOSAL_spinlock_irq_lock(&lst->spinlock);
  MTL_TRACE1(MT_FLFMT("{ epool_free: lst=%p, p=0x%lx"), lst, p);
  if (acq == 0)
  {
    INDEX_CHECK(lst, p, "p")
    next = lst->head;
    if (next != EPOOL_NULL)
    {
      char*   entry_next = centries + (next * entry_size);
      Index   prev = get_prev(meta, entry_next, &pprev);
      char*   entry_prev = centries + (prev * entry_size);
      INDEX_CHECK(lst, prev, "prev")
      pnext = addr_next(meta, entry_prev);
      *pprev = *pnext = p;
      pprev = addr_prev(meta, entry);
      pnext = addr_next(meta, entry);
      *pprev = prev;
      *pnext = next;
    }
    else /* was empty, now a singleton cycle */
    {
      pprev = addr_prev(meta, entry);
      pnext = addr_next(meta, entry);
      *pprev = *pnext = p;
    }
    lst->head = p;
    MOSAL_spinlock_unlock(&lst->spinlock);
  }
  ELSE_ERROR("epool_free")
  
  MTL_TRACE1(MT_FLFMT("} epool_free: lst=%p"), lst);
} /* epool_free */


/************************************************************************/
unsigned long  epool_reserve(
   EPool_t*       lst,
   unsigned long  start_index,
   unsigned long  res_size)
{
  unsigned long  unreserved = res_size; 
  int            acq = MOSAL_spinlock_irq_lock(&lst->spinlock);
  MTL_TRACE1(MT_FLFMT("{ epool_reserve: lst=%p, start=0x%lx, sz=0x%lx"), 
                      lst, start_index, res_size);
  if (acq == 0)
  {
    Index          end_index = start_index + res_size;
    Index          head;
    head = lst->head;
    INDEX_CHECK(lst, start_index, "start_index");
    INDEX_CHECK(lst, res_size ? end_index-1 : 0, "end_index-1");
    if (head != EPOOL_NULL)
    {
      const  EPool_meta_t*  meta = lst->meta;
      Index_p               ip;
      unsigned int          entry_size = meta->entry_size;
      char*                 centries = (char*)lst->entries;
      char*                 entry = centries + head * entry_size;
      Index                 p = get_next(meta, entry, &ip), next;
      INDEX_CHECK(lst, p, "p")
      /* Postpone dealing with the head, to:
       * + control end of iterations 
       * + simplify update of lst->head
       */
      while ((p != head) && (unreserved != 0))
      {
        if ((start_index <= p) && (p < end_index))
        {
          next = link_over(lst, p);
          --unreserved;
        }
        else
        {
          next = get_next(meta, centries + p*entry_size, &ip);
          INDEX_CHECK(lst, next, "next");
        }
        p = next;
      }
      if ((start_index <= head) && (head < end_index))
      {
        next = link_over(lst, head);
        --unreserved;
        lst->head = (head == next ? EPOOL_NULL : next);
      }
    }
    MOSAL_spinlock_unlock(&lst->spinlock);
  }
  ELSE_ERROR("epool_reserve")
  MTL_TRACE1(MT_FLFMT("} epool_reserve: unreserved=%ld"), unreserved);
  return unreserved;
} /* epool_reserve */


/************************************************************************/
void  epool_unreserve( 
  EPool_t*       lst,
  unsigned long  start_index,
  unsigned long  res_size)
{
  Index  i, e;
  MTL_TRACE1(MT_FLFMT("{ epool_unreserve: lst=%p, start=0x%lx, sz=0x%lx"), 
                      lst, start_index, res_size);
  INDEX_CHECK(lst, start_index, "start_index");
  INDEX_CHECK(lst, (res_size ? start_index+res_size-1 : 0), "end_index-1");
  for (i = start_index, e = start_index + res_size;  i != e;  ++i)
  {
    epool_free(lst, i);
  }
  MTL_TRACE1(MT_FLFMT("} epool_unreserve"));
} /* epool_unreserve */
