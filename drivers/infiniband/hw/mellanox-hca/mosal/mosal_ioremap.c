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

#include <mtl_types.h>
#include <linux/list.h>
#include <mosal.h>
#include <mosal_ioremap.h>



#if (LINUX_VERSION_CODE == KERNEL_VERSION(2,4,7))
#define list_for_each_safe(pos, n, head)                   \
    for (pos = (head)->next, n = pos->next; pos != (head); \
	        pos = n, n = pos->next)  
#endif 


/* percent above which hash will grow */                                
#define HASH_TRESHOLD_PERCENT 80

#define VMALLOC_TRSHOLD 0x4000

#define COND_ALLOC(type, n) ({                                              \
                               type *p;                                     \
                               if ( (sizeof(type)*(n))>=VMALLOC_TRSHOLD ) { \
                                 p=TNVMALLOC(type,n);                       \
                               }                                            \
                               else {                                       \
                                 p=TNMALLOC(type,n);                        \
                               }                                            \
                               p;                                           \
                            })


                                  
#define COND_FREE(addr, type, n) do {                                     \
                              if ( (sizeof(type)*n)>=VMALLOC_TRSHOLD ) {  \
                                VFREE(addr);                              \
                              }                                           \
                              else {                                      \
                                FREE(addr);                               \
                              }                                           \
                            }                                             \
                            while(0)


#define NUM_PAGES(addr, len, page_size)  (((addr) + (len) + (page_size) - 1)/(page_size) - (addr)/(page_size))


static unsigned long primes_array[] = {
  /* aprox. 100% growth */
  4999ul, 10007ul, 20021ul,

  /* aprox. 50% growth */
  30029ul, 45119ul, 60103ul
};

typedef struct {
  unsigned long pgn; /* physical page number */
  MT_virt_addr_t va; /* va of the start of the page */
  unsigned int ref_cnt; /* number of references to this mapping */
  struct list_head list; /* link elements that hash to the same index in the array */
}
ioremap_hash_item_t;

typedef struct {
  MT_virt_addr_t va;
  unsigned int ref_cnt; /* number of times this address is in the hash */
  ioremap_hash_item_t *ioremap_hash_item_p;
  struct list_head list; /* link elements that hash to the same index in the array */
}
va_item_t;

 /* index into the primes_array defining the size of the array */
static unsigned long pgn_hash_primes_idx;
static unsigned long va_hash_primes_idx;

/* current size of hash array */
static unsigned long pgn_hash_size;
static unsigned long va_hash_size;

/* current number of items in the hash */
static unsigned long pgn_hash_items_count;
static unsigned long va_hash_items_count;

/* threshold above which hash grows */
static unsigned long pgn_hash_threshold;
static unsigned long va_hash_threshold;

static struct list_head *pgn2item_arr = NULL;
static struct list_head *va2item_arr = NULL;

static MOSAL_mutex_t ioremap_mtx; /* protect access hashes */


static kmem_cache_t *items_cache = NULL;
static kmem_cache_t *va_cache = NULL;


static call_result_t va_hash_item_del(MT_virt_addr_t va, unsigned long *pgn_p)
{
  unsigned long idx;
  va_item_t *cur;
  struct list_head *pos, *next;

  idx = va % va_hash_size;

  list_for_each_safe(pos, next, &va2item_arr[idx]) {
    cur = list_entry(pos, va_item_t, list);
    if ( cur->va == va ) {
      *pgn_p = cur->ioremap_hash_item_p->pgn;
      cur->ref_cnt--;
      if ( cur->ref_cnt == 0 ) {
        list_del(pos);
        kmem_cache_free(va_cache, cur);
        va_hash_items_count--;
      }
      return MT_OK;
    }
  }

  return MT_ENORSC;
}


/*
 *  enlarge_pgn_hash
 *     This function must be protected by the caller
 */
static call_result_t enlarge_pgn_hash(void)
{
  unsigned int i;
  struct list_head *new_arr;
  struct list_head *pos, *next;
  unsigned long new_pgn_hash_size;
  ioremap_hash_item_t *cur;
  unsigned int new_idx;


  if ( pgn_hash_primes_idx >= (sizeof(primes_array)/sizeof(unsigned long)-1) ) {
    /* no more enlarging of the hash */
    MTL_TRACE1(MT_FLFMT("%s: no more allowed enlargement of hash"), __func__);
    return MT_OK;
  }

  /* allocate new array */
  new_pgn_hash_size = primes_array[pgn_hash_primes_idx+1];
  new_arr = COND_ALLOC(struct list_head, new_pgn_hash_size);
  if ( !new_arr ) {
    MTL_ERROR1(MT_FLFMT("%s: COND_ALLOC failed"), __func__);
    return MT_OK;
  }

  /* init the new array */
  for ( i=0; i<new_pgn_hash_size; ++i ) {
    INIT_LIST_HEAD(&new_arr[i]);
  }

  /* move all items to the new hash array */
  for ( i=0; i<pgn_hash_size; ++i ) {
    list_for_each_safe(pos, next, &pgn2item_arr[i]) {
      list_del(pos);
      cur = list_entry(pos, ioremap_hash_item_t, list);
      new_idx = cur->pgn % new_pgn_hash_size;
      list_add(pos, &new_arr[new_idx]);
    }
  }
  
  /* free old allocation */
  COND_FREE(pgn2item_arr, ioremap_hash_item_t, pgn_hash_size);

  pgn2item_arr = new_arr;
  pgn_hash_primes_idx++;
  pgn_hash_size = new_pgn_hash_size;
  pgn_hash_threshold = (pgn_hash_size * HASH_TRESHOLD_PERCENT) / 100;

  return MT_OK;
}


/*
 *  enlarge_va_hash
 */
static call_result_t enlarge_va_hash(void)
{
  unsigned int i;
  struct list_head *new_arr;
  struct list_head *pos, *next;
  unsigned long new_va_hash_size;
  va_item_t *cur;
  unsigned int new_idx;

  if ( va_hash_primes_idx >= (sizeof(primes_array)/sizeof(unsigned long)-1) ) {
    /* no more enlarging of the hash */
    MTL_TRACE1(MT_FLFMT("%s: no more allowed enlargement of hash"), __func__);
    return MT_OK;
  }

  /* allocate new array */
  new_va_hash_size = primes_array[va_hash_primes_idx+1];
  new_arr = COND_ALLOC(struct list_head, new_va_hash_size);
  if ( !new_arr ) {
    MTL_TRACE1(MT_FLFMT("%s: COND_ALLOC failed"), __func__);
    return MT_OK;
  }

  /* init the new array */
  for ( i=0; i<new_va_hash_size; ++i ) {
    INIT_LIST_HEAD(&new_arr[i]);
  }

  /* move all items to the new hash array */
  for ( i=0; i<va_hash_size; ++i ) {
    list_for_each_safe(pos, next, &va2item_arr[i]) {
      list_del(pos);
      cur = list_entry(pos, va_item_t, list);
      new_idx = cur->va % new_va_hash_size;
      list_add(pos, &new_arr[new_idx]);
    }
  }
  
  /* free old allocation */
  COND_FREE(va2item_arr, va_item_t, va_hash_size);

  va2item_arr = new_arr;
  va_hash_primes_idx++;
  va_hash_size = new_va_hash_size;
  va_hash_threshold = (va_hash_size * HASH_TRESHOLD_PERCENT) / 100;

  return MT_OK;
}


/*
 *  pgn_hash_item_del
 */
static call_result_t pgn_hash_item_del(unsigned long pgn)
{
  unsigned long idx;
  ioremap_hash_item_t *cur;
  struct list_head *pos, *next;

  idx = pgn % pgn_hash_size;

  list_for_each_safe(pos, next, &pgn2item_arr[idx]) {
    cur = list_entry(pos, ioremap_hash_item_t, list);
    if ( cur->pgn == pgn ) {
      /* found an entry with the requested pgn */
      cur->ref_cnt--;
      if ( cur->ref_cnt == 0 ) {
        iounmap((void *)cur->va);
        list_del(pos);
        kmem_cache_free(items_cache, cur);
        pgn_hash_items_count--;
      }
      return MT_OK;
    }
  }

  return MT_ENORSC;
}

/*
 *  pgn_hash_ins_up - hash insert or update
 *    search the hash for an entry having this pgn.
 *    If found increment the reference count of the entry.
 *    If not found create a new one
 *   
 *   args:
 *     pgn(in) - physical page number 
 *     hitem_pp(out) - pointer to var to hold the address of the item inserted/updated
 */
static call_result_t pgn_hash_ins_up(unsigned long pgn, ioremap_hash_item_t **hitem_pp)
{
  unsigned long idx;
  ioremap_hash_item_t *cur=NULL; /* assignment to silence the compiler */
  void *va;
  struct list_head *pos, *next;
  call_result_t rc;

  if ( pgn_hash_items_count > pgn_hash_threshold ) {
    MTL_ERROR1(MT_FLFMT("%s: calling enlarge_pgn_hash(): pgn_hash_items_count=%lu pgn_hash_threshold=%lu"),
               __func__, pgn_hash_items_count, pgn_hash_threshold); // ?? trace
    rc = enlarge_pgn_hash();
    if ( rc != MT_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: enlarge_pgn_hash() failed - %s"), __func__, mtl_strerror(rc));
      return rc;
    }
  }

  /* hash function is modulo size of hash */
  idx = pgn % pgn_hash_size;

  /* find the item that has this pgn */
  list_for_each_safe(pos, next, &pgn2item_arr[idx]) {
    cur = list_entry(pos, ioremap_hash_item_t, list);
    if ( cur->pgn == pgn ) {
      /* found an entry with the requested pgn */
      break;
    }
  }
  if ( pos != &pgn2item_arr[idx] ) {
    /* this means that we found a matching entry */
    cur->ref_cnt++;
    MTL_TRACE1(MT_FLFMT("%s: entry found. incerementing ref_cnt"),__func__);
  }
  else {
    /* maching entry not found - create a new one */
    MTL_TRACE1(MT_FLFMT("%s: entry not found. create new entry"),__func__);
    cur = (ioremap_hash_item_t *)kmem_cache_alloc(items_cache, GFP_KERNEL);
    if ( !cur ) {
      MTL_ERROR1(MT_FLFMT("%s: failed to allocate item"), __func__);
      return MT_EAGAIN;
    }

    /* create the mapping for the page */
    va = ioremap_nocache(pgn<<PAGE_SHIFT, PAGE_SIZE);
    if ( !va ) {
      kmem_cache_free(items_cache, cur);
      MTL_ERROR1(MT_FLFMT("%s: ioremap_nocache failed, items in hash=%lu"), __func__, pgn_hash_items_count);
      return MT_EAGAIN;
    }

    MTL_TRACE1(MT_FLFMT("%s: pgn=%lu, va=%p"),__func__, pgn, va);

    cur->pgn = pgn;
    cur->va = (MT_virt_addr_t)va;
    cur->ref_cnt = 1;
    pgn_hash_items_count++;
    list_add(&cur->list, &pgn2item_arr[idx]);
  }
  *hitem_pp = cur;
  return MT_OK;
}


/*
 *  va_hash_insert
 *    insert an association of the va supplied as an argument to the corresponding
 *    item in the pgn hash
 *
 *   va(in) - virtual address (key)
 *   hitem_p(in) - item in hash
 *       
 */
static call_result_t va_hash_insert(MT_virt_addr_t va, ioremap_hash_item_t *hitem_p)
{
  unsigned long idx;
  va_item_t *cur=NULL; /* assignment to silence the compiler */
  struct list_head *pos, *next;
  call_result_t rc;

  if ( va_hash_items_count > va_hash_threshold ) {
    MTL_TRACE1(MT_FLFMT("%s: calling enlarge_va_hash(): va_hash_items_count=%lu va_hash_threshold=%lu"),
               __func__, va_hash_items_count, va_hash_threshold);
    rc = enlarge_va_hash();
    if ( rc != MT_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: enlarge_va_hash() failed - %s"), __func__, mtl_strerror(rc));
      return rc;
    }
  }

  /* hash function is modulo size of hash */
  idx = va % va_hash_size;

  /* search if there is already an item that has this va */
  list_for_each_safe(pos, next, &va2item_arr[idx]) {
    cur = list_entry(pos, va_item_t, list);
    if ( cur->va == va ) {
      cur->ref_cnt++;
      MTL_TRACE1(MT_FLFMT("%s: va="VIRT_ADDR_FMT" is already in the hash. new ref_cnt=%d"), __func__, va, cur->ref_cnt);
      return MT_OK;
    }
  }

  cur = (va_item_t *)kmem_cache_alloc(va_cache, GFP_KERNEL);
  cur->va = va;
  cur->ioremap_hash_item_p = hitem_p;
  cur->ref_cnt = 1;
  list_add(&cur->list, &va2item_arr[idx]);
  va_hash_items_count++;

  return MT_OK;
}

/*
 * MOSAL_io_remap
 */
MT_virt_addr_t
#ifndef MTL_TRACK_ALLOC
MOSAL_io_remap
#else
MOSAL_io_remap_memtrack
#endif
(MT_phys_addr_t pa, MT_size_t size)
{
  unsigned long nop;
  unsigned long pgn;
  MT_virt_addr_t va;
  call_result_t rc, rc1;
  ioremap_hash_item_t *hitem_p;

  /* calculate the number of pages */
  nop = NUM_PAGES(pa, size, PAGE_SIZE);

  if ( nop > 1 ) {
    /* currently we support optimization for one page allocations only */
    MTL_TRACE1(MT_FLFMT("%s: pa="PHYS_ADDR_FMT", size="SIZE_T_FMT), __func__, pa, size);
    return (MT_virt_addr_t)ioremap_nocache(pa, size);
  }
  else {
    /* hashing is done on physical page numbers */

    MTL_TRACE1(MT_FLFMT("%s: pa="PHYS_ADDR_FMT", size="SIZE_T_FMT), __func__, pa, size);
    pgn = pa >> PAGE_SHIFT;

    MOSAL_mutex_acq_ui(&ioremap_mtx);
    rc = pgn_hash_ins_up(pgn, &hitem_p);
    if ( rc != MT_OK ) {
      MOSAL_mutex_rel(&ioremap_mtx);
      MTL_ERROR1(MT_FLFMT("%s: pgn_hash_ins_up failed: pgn=0x%lu - %s"), __func__, pgn, mtl_strerror(rc));
      return VA_NULL;
    }
    else {
      /* calculate the exact virtual address according to the address returned and the offset in the page */
      va = hitem_p->va + (pa & (PAGE_SIZE-1));
      rc = va_hash_insert(va, hitem_p);
      if ( rc != MT_OK ) {
        MOSAL_mutex_rel(&ioremap_mtx);
        MTL_ERROR1(MT_FLFMT("%s: va_hash_insert failed - %s"), __func__, mtl_strerror(rc));
        if ( (rc1=pgn_hash_item_del(pgn)) != MT_OK ) {
          MTL_ERROR1(MT_FLFMT("%s: pgn_hash_item_del failed: databases inconsistency - %s"), __func__, mtl_strerror(rc1));
        }
        return VA_NULL;
      }
    }
    MOSAL_mutex_rel(&ioremap_mtx);
  }
  return va;
}



/*
 *  MOSAL_io_unmap
 */
void
#ifndef MTL_TRACK_ALLOC
MOSAL_io_unmap
#else
MOSAL_io_unmap_memtrack                                  
#endif
(MT_virt_addr_t va)
{
  unsigned long pgn;
  call_result_t rc;

  MOSAL_mutex_acq_ui(&ioremap_mtx);
  rc = va_hash_item_del(va, &pgn);
  if ( rc == MT_OK ) {
    /* found va in hash */
    if ( (rc=pgn_hash_item_del(pgn)) != MT_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: pgn_hash_item_del failed - %s"), __func__, mtl_strerror(rc));
    }
    MOSAL_mutex_rel(&ioremap_mtx);
  }
  else {
    MOSAL_mutex_rel(&ioremap_mtx);
    iounmap((void *)va);
  }
}



/*
 *  MOSAL_ioremap_init
 */
call_result_t MOSAL_ioremap_init(void)
{
  unsigned int i;


  pgn_hash_primes_idx = 0;
  va_hash_primes_idx = 0;

  pgn_hash_size = primes_array[pgn_hash_primes_idx];
  va_hash_size = primes_array[va_hash_primes_idx];

  pgn_hash_items_count = 0;
  va_hash_items_count = 0;

  /* calculate growth threshold */
  pgn_hash_threshold = (pgn_hash_size * HASH_TRESHOLD_PERCENT) / 100;
  va_hash_threshold = (va_hash_size *  HASH_TRESHOLD_PERCENT) / 100;

  /* create items cache */
  items_cache = kmem_cache_create("ioremap_item", sizeof(ioremap_hash_item_t), 0, SLAB_HWCACHE_ALIGN, NULL, NULL);
  if ( !items_cache ) {
    MTL_ERROR1(MT_FLFMT("%s: failed to allocate cache"), __func__);
    return MT_EAGAIN;
  }

  /* create va items cache */
  va_cache = kmem_cache_create("va_item_t", sizeof(va_item_t), 0, SLAB_HWCACHE_ALIGN, NULL, NULL);
  if ( !va_cache ) {
    MTL_ERROR1(MT_FLFMT("%s: failed to allocate cache"), __func__);
    if ( kmem_cache_destroy(items_cache) != 0 ) {
      MTL_ERROR1(MT_FLFMT("%s: kmem_cache_destroy - kernel inconsistency"), __func__);
    }
    return MT_EAGAIN;
  }

  /* initialize mutex */
  MOSAL_mutex_init(&ioremap_mtx);

  /* create hash arrays */
  pgn2item_arr = COND_ALLOC(struct list_head, pgn_hash_size);
  if ( !pgn2item_arr ) {
    MTL_ERROR1(MT_FLFMT("%s: COND_ALLOC failed"), __func__);
    if ( kmem_cache_destroy(va_cache) != 0 ) {
      MTL_ERROR1(MT_FLFMT("%s: kmem_cache_destroy - kernel inconsistency"), __func__);
    }
    if ( kmem_cache_destroy(items_cache) != 0 ) {
      MTL_ERROR1(MT_FLFMT("%s: kmem_cache_destroy - kernel inconsistency"), __func__);
    }
    return MT_EAGAIN;
  }


  va2item_arr = COND_ALLOC(struct list_head, va_hash_size);
  if ( !va2item_arr ) {
    MTL_ERROR1(MT_FLFMT("%s: COND_ALLOC failed"), __func__);
    COND_FREE(pgn2item_arr, struct list_head, pgn_hash_size);
    if ( kmem_cache_destroy(va_cache) != 0 ) {
      MTL_ERROR1(MT_FLFMT("%s: kmem_cache_destroy - kernel inconsistency"), __func__);
    }
    if ( kmem_cache_destroy(items_cache) != 0 ) {
      MTL_ERROR1(MT_FLFMT("%s: kmem_cache_destroy - kernel inconsistency"), __func__);
    }
    return MT_EAGAIN;
  }




  /* initialize list heads */
  for ( i=0; i<pgn_hash_size; ++i ) {
    INIT_LIST_HEAD(&pgn2item_arr[i]);
  }

  for ( i=0; i<va_hash_size; ++i ) {
    INIT_LIST_HEAD(&va2item_arr[i]);
  }

  return MT_OK;
}



/*
 *  MOSAL_ioremap_cleanup
 */
void MOSAL_ioremap_cleanup(void)
{
  if ( pgn_hash_items_count || va_hash_items_count ) {
    MTL_ERROR1(MT_FLFMT("%s: hashes contain items. pgn_hash_items_count=%lu, va_hash_items_count=%lu"), __func__, pgn_hash_items_count, va_hash_items_count);
    return;
  }

  if ( kmem_cache_destroy(va_cache) != 0 ) {
    MTL_ERROR1(MT_FLFMT("%s: kmem_cache_destroy - kernel inconsistency"), __func__);
  }
  if ( kmem_cache_destroy(items_cache) != 0 ) {
    MTL_ERROR1(MT_FLFMT("%s: kmem_cache_destroy - kernel inconsistency"), __func__);
  }

  COND_FREE(pgn2item_arr, struct list_head, pgn_hash_size);
  COND_FREE(va2item_arr, struct list_head, va_hash_size);
}
