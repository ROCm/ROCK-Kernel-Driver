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

#ifndef __MOSAL_IOBUF_IMP__H
#define __MOSAL_IOBUF_IMP__H
 
 
#ifdef MT_KERNEL
#include <mosal.h>
#include <linux/list.h>
#include <mosal_iobuf.h>

#define MIN_ELEM_NUM 2
#define ELEMS_IN_MAP_ITEM (MIN_ELEM_NUM+1000)

typedef struct map_item_st {
  struct map_item_st *next; /* link to next item*/
  unsigned int elem_count; /* number of valid elements in the list */
  struct page *arr[ELEMS_IN_MAP_ITEM];
}
map_item_t;

struct mosal_iobuf_st {
  MT_virt_addr_t va;  /* virtual address of the buffer */
  MT_size_t size;     /* size in bytes of the buffer */
  MT_virt_addr_t pgalign_va;  /* address aligned to PAGE_SIZE */
  MT_virt_addr_t last_addr; /* last valid address in the range */
  MT_bool kmalloced;         /* kmalloced buffer */
  u_int32_t map_arr_sz;
  u_int32_t nr_pages;
  u_int32_t page_size;
  u_int32_t page_shift;
  u_int32_t page_sz_ratio;
  MOSAL_prot_ctx_t prot_ctx;
  void *mlock_ctx;
  struct mosal_iobuf_st *next; /* used to link mosal_iobufs */
  struct mosal_iobuf_st *prev; /* used to link mosal_iobufs */
  struct page **map_arr; /* pointer to an array of struct page * mapping the buffer */
  map_item_t *map_list; /* pointer to first element in a list objects containing the mapping */
  MOSAL_pid_t pid;   /* pid of the process who created the iobuf */
  struct mm_struct *mm; /* mm of owner of this iobuf */
  MOSAL_mem_perm_t perm; /* permissions of the region */
  MT_bool big_pages; /* states if the region contain big pages */
  MT_bool any_big_pages;
  MT_bool iomem;
};

/* iterator for getting segments of tpt */
struct mosal_iobuf_iter_st {
  map_item_t *item_p;  /* the item from where to take the next translations */
  unsigned int elem_idx; /* index from where to take the next translation */
};


#endif

 
call_result_t MOSAL_iobuf_init(void);
void MOSAL_iobuf_cleanup(void);
 
#endif /* __MOSAL_IOBUF_IMP__H */
