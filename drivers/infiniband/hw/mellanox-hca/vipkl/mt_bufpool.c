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

#include <mosal.h>
#include "mt_bufpool.h"

#define MAX_MALLOC_SZ (MOSAL_SYS_PAGE_SIZE*2)

typedef struct MT_malloc_item_st {
  void* chunk_p;
  struct MT_malloc_item_st *next;
} MT_malloc_item_t;

struct MT_bufpool_st {
  MOSAL_semaphore_t free_list_sem;  /* Control flow of allocators */
  MOSAL_spinlock_t free_list_lock;       /* Control insert/remove from list */
  void *free_buf_list_p; /* list to allocate buffer from ("next" is managed in the buffer itself)*/
  
  MT_malloc_item_t *orig_malloc_list_p; /* list of buffers allocated    */
  /* Note: The orig_buf_list_p are not necessarily buffers of item_bsize size 
   *      (i.e., allocation can be optimized by allocating several buffers in one MALLOC) */
  
  MT_size_t item_bsize;    /* Buffer item byte size */
  u_int8_t log2_alignment; /* Item alignment */
  MT_size_t min_num_of_items;
  MT_bufpool_flags_t flags;/* bufpool options */
  MT_size_t chunk_size;    /* Size of chunk in memory allocation (multiple of item_bsize) */
  
};


call_result_t MT_bufpool_create(
  MT_size_t           item_bsize,       /*IN*/
  u_int8_t            log2_alignment,   /*IN*/
  MT_size_t           min_num_of_items, /*IN*/
  MT_bufpool_flags_t  flags,            /*IN*/
  MT_bufpool_t       *bufpool_hndl_p    /*OUT*/
)
{
  MT_bufpool_t new_bufpool= NULL;
  MT_size_t chunk_size;       /* Size of chunk in memory allocation (multiple of item_bsize) */
  MT_size_t items_per_chunk;
  MT_size_t min_num_of_chunks,i,j;
  static u_int8_t log2_min_buf_align;
  MT_size_t alignment_appendix; /* Bytes to add on chunk allocation for alignment */
  void *cur_buf_p;
  MT_malloc_item_t *malloc_item_p;

  if (item_bsize == 0)  return MT_EINVAL;
  /* Assure item alignment is at least pointer size */
  if (sizeof(void*) == 4) { /* 32b */
    log2_min_buf_align= (log2_alignment > 2) ? log2_alignment : 2;
  } else if (sizeof(void*) == 8) {
    log2_min_buf_align= (log2_alignment > 3) ? log2_alignment : 3;
  } else {
    MTL_ERROR1(MT_FLFMT("%s: Unsupported pointer size ("SIZE_T_FMT" bytes)"), __func__, sizeof(void*));
    return MT_ENOSYS;
  }

  /* Assure each buffer is aligned to pointer size */
  item_bsize= MT_UP_ALIGNX_SIZE(item_bsize, log2_min_buf_align); 

  min_num_of_items= (min_num_of_items < 1) ? 1 : min_num_of_items;
  
  new_bufpool= MALLOC(sizeof(struct MT_bufpool_st));
  if (new_bufpool == NULL)  return MT_EAGAIN;
  memset(new_bufpool, 0, sizeof(struct MT_bufpool_st));

  /* If MAX_MALLOC_SZ is less than one item, each chunk is an item of its size (with VMALLOC) */
  alignment_appendix= MASK32(log2_min_buf_align);
  items_per_chunk= (MAX_MALLOC_SZ - alignment_appendix) / item_bsize;
  if (items_per_chunk > 0) {
    chunk_size= MAX_MALLOC_SZ;
  } else {
    chunk_size= item_bsize + alignment_appendix;
    items_per_chunk= 1;
  }
  
  /* Num. of chunks is the round-up of the division in items_per_chunk */
  min_num_of_chunks= (min_num_of_items + items_per_chunk - 1)/ items_per_chunk;
  
  MTL_DEBUG3(MT_FLFMT("%s: Allocating bufpool (hndl=0x%p) of item_bsize="SIZE_T_FMT" B , log2_alignment=%u , "
                      "min_num_of_items="SIZE_T_FMT" , flags=0x%X , chunk_size="SIZE_T_FMT" B , "
                      "items_per_chunk="SIZE_T_FMT" , min_num_of_chunks="SIZE_T_FMT""), __func__,
                      new_bufpool, item_bsize, log2_min_buf_align, min_num_of_items, flags, 
                      chunk_size, items_per_chunk, min_num_of_chunks);
  
  for (i= 0; i < min_num_of_chunks; i++) {
    malloc_item_p= MALLOC(sizeof(MT_malloc_item_t));
    if (malloc_item_p == NULL) {
      MTL_ERROR4(MT_FLFMT("%s: Failed allocation of malloc list item #"SIZE_T_FMT), __func__, i);
      goto malloc_item_failed;
    }
    malloc_item_p->chunk_p= (chunk_size > MAX_MALLOC_SZ) ? 
                            VMALLOC(chunk_size): MALLOC(chunk_size);
    MTL_DEBUG4("%s: "SIZE_T_FMT"] Allocated chunk at 0x%p ,", __func__, i, malloc_item_p->chunk_p);
    if (malloc_item_p->chunk_p == NULL) {
      MTL_ERROR4(MT_FLFMT("%s: Failed allocation of "SIZE_T_FMT"-th chunk of "SIZE_T_FMT" bytes"), __func__,
                 i, chunk_size);
      FREE(malloc_item_p);
      goto chunk_alloc_failed;
    }
    /* link new chunk as first */
    malloc_item_p->next= new_bufpool->orig_malloc_list_p; 
    new_bufpool->orig_malloc_list_p= malloc_item_p;          

    cur_buf_p= (void*)MT_UP_ALIGNX_VIRT(((u_int8_t*)malloc_item_p->chunk_p), log2_min_buf_align);
    for (j= 0; j < items_per_chunk; j++) { /* Put buffer items of new chunk in buffer's free list */
      /* Attach at free list head */
      MTL_DEBUG5(MT_FLFMT("%s: Insert cur_buf_p=0x%p"), __func__, cur_buf_p);
      *(void**)cur_buf_p= new_bufpool->free_buf_list_p;
      new_bufpool->free_buf_list_p= cur_buf_p;
      cur_buf_p= (u_int8_t*)cur_buf_p + item_bsize; /* next buffer in chunk */
    }
  } /* for min_num_of_chunks */

  MOSAL_sem_init(&new_bufpool->free_list_sem, min_num_of_chunks * items_per_chunk); 
  MOSAL_spinlock_init(&new_bufpool->free_list_lock);       /* Control insert/remove from list */
  /* Save attributes */
  new_bufpool->item_bsize= item_bsize;              
  new_bufpool->log2_alignment= log2_min_buf_align;  
  new_bufpool->min_num_of_items= min_num_of_items;
  new_bufpool->flags= flags;          
  new_bufpool->chunk_size= chunk_size;

  *bufpool_hndl_p= new_bufpool;

  return MT_OK;


  malloc_item_failed:
  chunk_alloc_failed:
    while (new_bufpool->orig_malloc_list_p != NULL) {
      malloc_item_p= new_bufpool->orig_malloc_list_p;   /* save ptr to free */
      new_bufpool->orig_malloc_list_p= malloc_item_p->next; /* Remove chunk from list */
      if (chunk_size > MAX_MALLOC_SZ) {
        VFREE(malloc_item_p->chunk_p);
      } else {
        FREE(malloc_item_p->chunk_p);
      }
      FREE(malloc_item_p);
    }
    FREE(new_bufpool);
    return MT_EAGAIN;
}


void* MT_bufpool_alloc(MT_bufpool_t bufpool_hndl)
{
  void* returned_buf_p;

  /* wait for available buffer */
  if (MOSAL_sem_acq(&bufpool_hndl->free_list_sem, TRUE) != MT_OK) return NULL; 

  MOSAL_spinlock_lock(&bufpool_hndl->free_list_lock);
  returned_buf_p= bufpool_hndl->free_buf_list_p; /* take first */
  if (returned_buf_p != NULL) { /* Put next as first */
    bufpool_hndl->free_buf_list_p= *(void**)returned_buf_p;
  }
  MOSAL_spinlock_unlock(&bufpool_hndl->free_list_lock);

  return returned_buf_p;
}


void MT_bufpool_free(MT_bufpool_t bufpool_hndl, void *buf_p)
{
  MOSAL_spinlock_lock(&bufpool_hndl->free_list_lock);
  *(void**)buf_p= bufpool_hndl->free_buf_list_p;    /* link before first */
  bufpool_hndl->free_buf_list_p= buf_p;
  MOSAL_spinlock_unlock(&bufpool_hndl->free_list_lock);
  MOSAL_sem_rel(&bufpool_hndl->free_list_sem);
}



void MT_bufpool_destroy(MT_bufpool_t bufpool_hndl)
{
  MT_malloc_item_t *malloc_item_p;
  const MT_size_t chunk_size= bufpool_hndl->chunk_size;

  MTL_DEBUG4(MT_FLFMT("%s: destroying bufpool_hndl=0x%p"), __func__, bufpool_hndl);

  while (bufpool_hndl->orig_malloc_list_p != NULL) {
    malloc_item_p= bufpool_hndl->orig_malloc_list_p;   /* save ptr to free */
    bufpool_hndl->orig_malloc_list_p= malloc_item_p->next; /* Remove chunk from list */
    MTL_DEBUG5(MT_FLFMT("%s: Freeing chunk at 0x%p"), __func__, malloc_item_p->chunk_p);
    if (chunk_size > MAX_MALLOC_SZ) {
      VFREE(malloc_item_p->chunk_p);
    } else {
      FREE(malloc_item_p->chunk_p);
    }
    FREE(malloc_item_p);
  }
  FREE(bufpool_hndl);
}

