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
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <asm/errno.h>
#include <asm/current.h>
#include <asm/page.h>
#include <linux/list.h>
#include <linux/securebits.h>
#include <linux/utsname.h>
#include <linux/highmem.h>
#include <mtl_common.h>
#include <asm/pgalloc.h>
#include "mosal_iobuf_imp.h"
#include "mosal_priv.h"

#if !LINUX_KERNEL_2_6
#include <sys/syscall.h>
#include <linux/compatmac.h>
#else
#include <asm/tlbflush.h>
#endif


#ifndef VALID_PAGE
  #define VALID_PAGE(page) 1
#endif

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


                                  
#define COND_FREE(addr, sz) do {                              \
                              if ( (sz)>=VMALLOC_TRSHOLD ) {  \
                                VFREE(addr);                  \
                              }                               \
                              else {                          \
                                FREE(addr);                   \
                              }                               \
                            }                                 \
                            while(0)

/* number of free pages that must remain in the system after allocating a kiobuf */
#define MIN_LIMIT_PAGES  0x1000 /* 16 MB @ 4k page size */
static u_int32_t lowest_free_pages;
static u_int32_t total_mem_mb;

/*================ static functions prototypes ===================*/
static call_result_t create_mosal_iobuf(MT_virt_addr_t va,
                                   MT_size_t size,
                                   MOSAL_prot_ctx_t prot_ctx,
                                   MOSAL_iobuf_t *iobuf_p,
                                   u_int32_t flags,
                                   MOSAL_mem_perm_t req_perm,
                                   MT_bool big_pages,
                                   MT_bool any_big_pages,
                                   MT_bool iomem);

static void destroy_mosal_iobuf(MOSAL_iobuf_t iobuf);

static call_result_t mosal_get_pages(MOSAL_iobuf_t iobuf);
static void mosal_put_pages(MOSAL_iobuf_t iobuf);
static void print_iobuf(MOSAL_iobuf_t iobuf);
static call_result_t check_perm(MT_virt_addr_t va, MT_size_t size, MOSAL_prot_ctx_t prot_ctx,
                         MOSAL_mem_perm_t req_perm, MT_bool *bp_p, MT_bool *any_bp_p, MT_bool *iomem_p);
/*================ end static functions prototypes ===============*/


static kmem_cache_t *iobuf_cache = NULL;
static kmem_cache_t *map_item_cache = NULL;


/*
 *  perm_ok
 */
static int inline perm_ok(MOSAL_mem_perm_t req_perm, unsigned long vm_flags)
{
  int mosal_wr = req_perm & MOSAL_PERM_WRITE;
  int mosal_rd = req_perm & MOSAL_PERM_READ;
  int vm_wr = vm_flags & VM_WRITE;
  int vm_rd = vm_flags & VM_READ;

  return (!mosal_wr && !mosal_rd) ||
         (vm_wr && !mosal_rd) ||
         (!mosal_wr && vm_rd) ||
         (vm_wr && vm_rd);
}


/*
 *  rollback_vmas
 */
static void rollback_vmas(struct vm_area_struct *vma, struct vm_area_struct *vvma)
{
  while ( vma != vvma ) {
    spin_lock(&vma->vm_mm->page_table_lock);
    vma->vm_flags &= (~VM_DONTCOPY);
    spin_unlock(&vma->vm_mm->page_table_lock);
    vma = vma->vm_next;
  }
}


/*
 *  mark_dontcopy
 *  returns: 0 - do not mark vmas with VM_DONTCOPY
 *           1 - mark vmas with VM_DONTCOPY
 */
static inline int mark_dontcopy(MOSAL_iobuf_t iobuf)
{
  if ( iobuf->flags == 0 ) {
    return 0;
  }
  else if ( iobuf->flags & MOSAL_IOBUF_LNX_FLG_MARK_ALL_DONTCOPY ) {
    return 1;
  }
  else if ( iobuf->flags & MOSAL_IOBUF_LNX_FLG_MARK_FULL_PAGES_DONTCOPY ) {
    MT_virt_addr_t mask = (((MT_virt_addr_t)1) << iobuf->page_shift)-1;
    if ( (iobuf->va & mask) || ((iobuf->va+iobuf->size) & mask) ) {
      return 0;
    }
    else {
      return 1;
    }
  }
  else {
    return 0;
  }
}

/*
 *  set_vm_dontcopy_flags
 */
static call_result_t set_vm_dontcopy_flags(MOSAL_iobuf_t iobuf)
{
  unsigned long addr, last;
  struct vm_area_struct *vma, *prev, *start_vma;
  MT_virt_addr_t va = iobuf->va;
  MT_size_t size = iobuf->size;

  MTL_TRACE1(MT_FLFMT("%s: va="VIRT_ADDR_FMT", size="SIZE_T_FMT), __func__, va, size);

  if ( !mark_dontcopy(iobuf) ) {
    iobuf->os_dep_flags &= (u_int32_t)(~MOSAL_IOBUF_LNX_FLG_PROP_MARKED_DONT_COPY);
    MTL_DEBUG1(MT_FLFMT("%s(pid="MT_ULONG_PTR_FMT"): not marking VM_DONTCOPY va="VIRT_ADDR_FMT", size="SIZE_T_FMT),
               __func__, MOSAL_getpid(), iobuf->va, iobuf->size);
    return MT_OK;
  }

  addr = (unsigned long)va;

	down_read(&current->mm->mmap_sem);
  vma = find_vma(current->mm, addr);
  if ( !vma || addr<vma->vm_start) {
    /* addr is not included in the vma */
    up_read(&current->mm->mmap_sem);
    return MT_ENOMEM;
  }
  else {
    /* addr contained in vma */
    spin_lock(&vma->vm_mm->page_table_lock);
    MTL_DEBUG1(MT_FLFMT("%s: %lx-%lx marked VM_DONTCOPY"), __func__, vma->vm_start, vma->vm_end);
    vma->vm_flags |= VM_DONTCOPY;
    spin_unlock(&vma->vm_mm->page_table_lock);
  }

  last = addr + size;
  start_vma = vma;
  while ( last > vma->vm_end ) {
    prev = vma;
    vma = vma->vm_next;
    /* we require regions to be adjacent */
    if ( !vma || (prev->vm_end!=vma->vm_start) ) {
      /* rollback */
      rollback_vmas(start_vma, vma);
      up_read(&current->mm->mmap_sem);
      return MT_ENOMEM;
    }
    else {
      spin_lock(&vma->vm_mm->page_table_lock);
      vma->vm_flags |= VM_DONTCOPY;
      MTL_DEBUG1(MT_FLFMT("%s: %lx-%lx marked VM_DONTCOPY"), __func__, vma->vm_start, vma->vm_end);  // ?? trace
      spin_unlock(&vma->vm_mm->page_table_lock);
    }
  }
  
  iobuf->os_dep_flags |= MOSAL_IOBUF_LNX_FLG_PROP_MARKED_DONT_COPY;
  MTL_DEBUG1(MT_FLFMT("%s(pid="MT_ULONG_PTR_FMT"): marking VM_DONTCOPY va="VIRT_ADDR_FMT", size="SIZE_T_FMT),
             __func__, MOSAL_getpid(), iobuf->va, iobuf->size);
	up_read(&current->mm->mmap_sem);
  return MT_OK;
}




/*
 *  restore_vm_flags
 */
static void restore_vm_flags(MOSAL_iobuf_t iobuf)
{
  unsigned long start, last;
  struct vm_area_struct *vma;
  MT_virt_addr_t va = iobuf->va;
  MT_size_t size = iobuf->size;

  if ( !(iobuf->os_dep_flags&MOSAL_IOBUF_LNX_FLG_PROP_MARKED_DONT_COPY) ) {
    return;
  }

  start = va;
  last = va + size;
	down_read(&current->mm->mmap_sem);
  vma = find_vma(current->mm, va);
  if ( !vma || start>=vma->vm_end ) {
    up_read(&current->mm->mmap_sem);
    return;
  }
  else {
    spin_lock(&vma->vm_mm->page_table_lock);
    vma->vm_flags &= (~VM_DONTCOPY);
    spin_unlock(&vma->vm_mm->page_table_lock);
  }

  while ( last > vma->vm_end ) {
    vma = vma->vm_next;
    if ( !vma ) break;
    spin_lock(&vma->vm_mm->page_table_lock);
    vma->vm_flags &= (~VM_DONTCOPY);
    spin_unlock(&vma->vm_mm->page_table_lock);
  }
  up_read(&current->mm->mmap_sem);
}

/*
 *  MOSAL_iobuf_get_props
 */
call_result_t MOSAL_iobuf_get_props(MOSAL_iobuf_t iobuf,
                                    MOSAL_iobuf_props_t *props_p)
{
  /* sanity check */
  if ( !iobuf ) return MT_EINVAL;

  props_p->size = iobuf->size;
  props_p->va = iobuf->va;
  props_p->nr_pages = iobuf->nr_pages;
  props_p->page_shift = iobuf->page_shift;
  props_p->prot_ctx = iobuf->prot_ctx;
  props_p->os_dep_flags = iobuf->os_dep_flags;
  return MT_OK;
}



#define BUF_PAGES(va, len, page_size)  (((va) + (len) + (page_size) - 1)/(page_size) - (va)/(page_size))
                                         


/*
 *  free_map_list
 */
static void free_map_list(map_item_t *head)
{
  static map_item_t *tmp;

  while ( head ) {
    tmp = head->next;
    kmem_cache_free(map_item_cache, head);
    head = tmp;
  }
}


/*
 *  alloc_map_list
 */
static map_item_t *alloc_map_list(u_int32_t arr_sz)
{
  map_item_t *head = NULL, *tmp;
  unsigned int elem_count = arr_sz % ELEMS_IN_MAP_ITEM;

  MTL_DEBUG1(MT_FLFMT("%s: arr_sz=%d"), __func__, arr_sz);
  while ( arr_sz > 0 ) {
    tmp = (map_item_t *)kmem_cache_alloc(map_item_cache, GFP_KERNEL);
    if ( !tmp ) {
      free_map_list(head);
      return NULL;
    }
    tmp->elem_count = elem_count;
    MTL_DEBUG1(MT_FLFMT("%s: elem_count=%d"), __func__, tmp->elem_count);
    arr_sz -=  elem_count;
    tmp->next = head;
    head = tmp;
    elem_count = ELEMS_IN_MAP_ITEM;
  }
  return head;
}




/*
 *  mosal_touch_big_pages
 */
int mosal_touch_big_pages(MT_virt_addr_t va, MT_size_t size)
{
#ifdef __i386__
  unsigned int page_shift = PMD_SHIFT;
  MT_virt_addr_t aligned_addr = MT_DOWN_ALIGNX_VIRT(va, page_shift), addr;
  int num_pages = BUF_PAGES(va, size, 1<<page_shift), dummy, i;

  MTL_DEBUG1(MT_FLFMT("%s: aligned_addr="VIRT_ADDR_FMT", num_pages=%d, page_shift=%d"), __func__, aligned_addr, num_pages, page_shift);
  for ( i=0, addr=aligned_addr; i<num_pages; ++i, addr+=(1<<page_shift) ) {
    copy_from_user(&dummy, (void *)addr, sizeof(dummy));
  }
#endif
  return 0;
}

/*
 *  MOSAL_iobuf_register
 */
call_result_t 
#ifndef MTL_TRACK_ALLOC
MOSAL_iobuf_register
#else
MOSAL_iobuf_register_memtrack                                  
#endif
                             (MT_virt_addr_t va,
                              MT_size_t size,
                              MOSAL_prot_ctx_t prot_ctx,
                              MOSAL_mem_perm_t req_perm,
                              MOSAL_iobuf_t *iobuf_p,
                              u_int32_t flags)
{
  call_result_t rc;
  MT_bool big_pages, any_big_pages, iomem;
//  extern int get_user_pages(struct task_struct *tsk, struct mm_struct *mm, unsigned long start,
//		int len, int write, int force, struct page **pages, struct vm_area_struct **vmas);

  /* check permissions */
  rc = check_perm(va, size, prot_ctx, req_perm, &big_pages, &any_big_pages, &iomem);
  if ( rc != MT_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: va="VIRT_ADDR_FMT", size="SIZE_T_FMT", requested permissions=0x%x- %s"), __func__, va, size, req_perm, mtl_strerror(rc));
    return rc;
  }



  rc = create_mosal_iobuf(va, size, prot_ctx ,iobuf_p, flags, req_perm, big_pages, any_big_pages, iomem);
  if ( rc != MT_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: create_mosal_iobuf failed"), __func__);
    return rc;
  }
  if ( prot_ctx == MOSAL_get_current_prot_ctx() ) {
    /* user space memory */
    rc = MOSAL_mlock_iobuf(va, size, *iobuf_p, (*iobuf_p)->page_shift);
    if ( rc != MT_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: MOSAL_mlock_iobuf failed"), __func__);
      free_map_list((*iobuf_p)->map_list);
      destroy_mosal_iobuf(*iobuf_p);
      if ( rc == MT_ENOMEM ) {
        /* this can happen when the user has too many physical pages registered in that case
           we want to return MT_EAGAIN. The real case of ENOMEM is validated in check_perm above */
        return MT_EAGAIN;
      }
      else {
        return rc;
      }
    }
    /* mark vmas as dontcopy */
    if ( set_vm_dontcopy_flags(*iobuf_p) != MT_OK ) {
      /* this should not happen unless someone pulled the carpet from under our legs */
      MTL_ERROR1(MT_FLFMT("%s: set_vm_dontcopy_flags failed - va="VIRT_ADDR_FMT", size="SIZE_T_FMT),
                 __func__, va, size);
      if ( MOSAL_munlock_iobuf(va, size, *iobuf_p,  (*iobuf_p)->page_shift) != MT_OK ) {
        MTL_ERROR1(MT_FLFMT("%s: MOSAL_munlock_iobuf failed"), __func__);
      }
      free_map_list((*iobuf_p)->map_list);
      destroy_mosal_iobuf(*iobuf_p);
      return MT_ERROR;
    }
  }

  /* increment reference count on the pages */
  rc = mosal_get_pages(*iobuf_p);
  if ( rc != MT_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: mosal_get_pages failed"), __func__);
    print_iobuf(*iobuf_p);
    if ( prot_ctx==MOSAL_get_current_prot_ctx() ) {
      restore_vm_flags(*iobuf_p);
      if ( MOSAL_munlock_iobuf(va, size, *iobuf_p,  (*iobuf_p)->page_shift) != MT_OK ) {
        MTL_ERROR1(MT_FLFMT("%s: MOSAL_munlock_iobuf failed"), __func__);
      }
    }
    free_map_list((*iobuf_p)->map_list);
    destroy_mosal_iobuf(*iobuf_p);
    return MT_EAGAIN;
  }
  return MT_OK;
}



/*
 *  MOSAL_iobuf_deregister
 */
call_result_t 
#ifndef MTL_TRACK_ALLOC
MOSAL_iobuf_deregister
#else
MOSAL_iobuf_deregister_memtrack                                  
#endif
(MOSAL_iobuf_t iobuf)
{
  call_result_t rc;

  /* sanity check */
  if ( !iobuf ) return MT_EINVAL;

  if ( iobuf->prot_ctx == MOSAL_get_current_prot_ctx() ) {
    if ( iobuf->mm == current->mm ) {
      restore_vm_flags(iobuf);
    }
    if ( (rc=MOSAL_munlock_iobuf(iobuf->va, iobuf->size, iobuf, iobuf->page_shift)) != MT_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: MOSAL_munlock_iobuf failed (%s)"), __func__,mtl_strerror_sym(rc));
      print_iobuf(iobuf);
    }
  }
  mosal_put_pages(iobuf);
  free_map_list(iobuf->map_list);
  destroy_mosal_iobuf(iobuf);
  return MT_OK;
}



/*
 *  MOSAL_iobuf_get_tpt
 */
call_result_t MOSAL_iobuf_get_tpt(MOSAL_iobuf_t iobuf,
                                  u_int32_t npages,
                                  MT_phys_addr_t *pa_arr,
                                  u_int32_t *page_size_p,
                                  u_int32_t *act_table_sz_p)
{
  u_int32_t i, n;
  MT_virt_addr_t addr;
  MT_phys_addr_t pa;

  /* sanity check */
  if ( !iobuf ) return MT_EINVAL;

  n = npages<=iobuf->nr_pages ? npages : iobuf->nr_pages;
  if ( iobuf->kmalloced ) {
    pa = mosal_page_to_phys(iobuf->map_arr[0]);
    for ( i=0; i<n; ++i, pa+=iobuf->page_size ) {
      pa_arr[i] = pa;
    }
  }
  else {
    addr = MT_DOWN_ALIGNX_VIRT(iobuf->va, iobuf->page_shift);
    if ( !iobuf->map_list ) {
      for ( i=0; i<n; ++i ) {
        pa_arr[i] = mosal_page_to_phys(iobuf->map_arr[i]);
        MTL_DEBUG4(MT_FLFMT("%s: va=0x" VIRT_ADDR_FMT", pa=0x" PHYS_ADDR_FMT), __func__, (MT_virt_addr_t)(addr+i*PAGE_SIZE), pa_arr[i]);
      }
    }
    else {
      struct map_item_st *tmp;
      u_int32_t j=0, stop=0;

      for ( tmp=iobuf->map_list; tmp && (!stop); tmp=tmp->next ) {
        /* this seems to solve a misterious bug in NUMA systems
           needs farther investigation */
        if ( !tmp ) {
          MTL_ERROR1(MT_FLFMT("%s: tmp=NULL"), __func__);
          return MT_ERROR;
        }
        for ( i=0; (i<tmp->elem_count) && (!stop); ++i, ++j ) {
          if ( j >= n ) {
            stop = 1;
            break;
          }
          pa_arr[j] = mosal_page_to_phys(tmp->arr[i]);
          MTL_DEBUG4(MT_FLFMT("%s: va=0x" VIRT_ADDR_FMT", pa=0x" PHYS_ADDR_FMT), __func__, (MT_virt_addr_t)(addr+j*PAGE_SIZE), pa_arr[j]);
        }
      }
    }
  }
  if (page_size_p) *page_size_p = iobuf->page_size;
  if (act_table_sz_p) *act_table_sz_p = iobuf->nr_pages;
  return MT_OK;
}

call_result_t MOSAL_iobuf_init(void)
{
  
  iobuf_cache = kmem_cache_create("MOSAL_iobuf_t", sizeof(struct mosal_iobuf_st), 0, SLAB_HWCACHE_ALIGN, NULL, NULL);
  if ( !iobuf_cache ) {
    MTL_ERROR1(MT_FLFMT("failed to allocate cache"));
    return MT_EAGAIN;
  }
  lowest_free_pages = MIN_LIMIT_PAGES + (num_physpages>>6);
  total_mem_mb = (((MT_phys_addr_t)lowest_free_pages)<<PAGE_SHIFT)>>20;
  MTL_TRACE1(MT_FLFMT("%s: num_phys_pages=%ld, reserved=%d"), __func__, num_physpages, lowest_free_pages);

  map_item_cache = kmem_cache_create("MOSAL::map_item_t", sizeof(map_item_t), 0, SLAB_HWCACHE_ALIGN, NULL, NULL);
  if ( !map_item_cache ) {
    if ( kmem_cache_destroy(iobuf_cache) != 0 ) {
      MTL_ERROR1(MT_FLFMT("%s: MOSAL_iobuf_t cache destroy failed - probably a memory leak"), __func__);
    }
  }

  return MT_OK;
}


void MOSAL_iobuf_cleanup(void)
{
  if ( kmem_cache_destroy(iobuf_cache) != 0 ) {
    MTL_ERROR1(MT_FLFMT("MOSAL_iobuf_t cache destroy failed - probably a memory leak"));
  }

  if ( kmem_cache_destroy(map_item_cache) != 0 ) {
    MTL_ERROR1(MT_FLFMT("MOSAL_iobuf_t cache destroy failed - probably a memory leak"));
  }
}


 
/*
 *  create_mosal_iobuf
 */
static call_result_t create_mosal_iobuf(MT_virt_addr_t va,
                                        MT_size_t size,
                                        MOSAL_prot_ctx_t prot_ctx,
                                        MOSAL_iobuf_t *iobuf_p,
                                        u_int32_t flags,
                                        MOSAL_mem_perm_t req_perm,
                                        MT_bool big_pages,
                                        MT_bool any_big_pages,
                                        MT_bool iomem)
{
  struct mosal_iobuf_st *new_iobuf;
  u_int32_t nr_pages, arr_sz;
  unsigned int page_size, page_shift;
  call_result_t rc;

  if ( !iobuf_p ) return MT_EINVAL;
  if ( size == 0 ) return MT_EINVAL;
  if ( va == VA_NULL ) return MT_EINVAL;


  rc = MOSAL_get_page_shift(prot_ctx, va, &page_shift);
  if ( rc != MT_OK ) {
    return rc;
  }
  page_size = 1<<page_shift;



  new_iobuf = (struct mosal_iobuf_st *)kmem_cache_alloc(iobuf_cache, GFP_KERNEL);
  if ( !new_iobuf ) {
    MTL_ERROR1(MT_FLFMT("%s: allocation of struct mosal_iobuf_st failed"), __func__);
    return MT_EAGAIN;
  }

  nr_pages = BUF_PAGES(va, size, page_size);

  /* check if this buffer is kmalloced */
  if ( (prot_ctx==MOSAL_get_kernel_prot_ctx()) && !vmalloced_addr(va) ) {
    new_iobuf->kmalloced = TRUE;
    /* kmalloced buffers are physically contiguos so we need
       to hold only the first struct pte pointer */
    arr_sz = 1;
    MTL_DEBUG1(MT_FLFMT("%s: kmalloced iobuf"), __func__);
  }
  else {
    new_iobuf->kmalloced = FALSE;
    arr_sz = nr_pages;
    MTL_DEBUG1(MT_FLFMT("%s: none kmalloced iobuf"), __func__);
  }

  if ( arr_sz < ELEMS_IN_MAP_ITEM ) {
    new_iobuf->map_arr = COND_ALLOC(struct page *, arr_sz);
    if ( !new_iobuf->map_arr ) {
      kmem_cache_free(iobuf_cache, new_iobuf);
      MTL_ERROR1(MT_FLFMT("%s: allocation of %d pointers failed"), __func__, arr_sz);
      return MT_EAGAIN;
    }
    new_iobuf->map_list = NULL;
  }
  else {
    new_iobuf->map_list = alloc_map_list(arr_sz);
    if ( !new_iobuf->map_list ) {
      kmem_cache_free(iobuf_cache, new_iobuf);
      MTL_ERROR1(MT_FLFMT("%s: allocation of %d pointers failed"), __func__, arr_sz);
      return MT_EAGAIN;
    }
    new_iobuf->map_arr = NULL;
  }

  new_iobuf->map_arr_sz = arr_sz;
  new_iobuf->page_size = page_size;
  new_iobuf->page_shift = page_shift;
  new_iobuf->page_sz_ratio = new_iobuf->page_size/MOSAL_SYS_PAGE_SIZE;
  if ( new_iobuf->page_size != (MOSAL_SYS_PAGE_SIZE*new_iobuf->page_sz_ratio) ) {
    if ( !new_iobuf->map_list ) {
      COND_FREE(new_iobuf->map_arr, sizeof(struct page *)*arr_sz);
    }
    else {
      free_map_list(new_iobuf->map_list);
    }
    kmem_cache_free(iobuf_cache, new_iobuf);
    MTL_ERROR1(MT_FLFMT("%s: page size=%d, system page size=%lu"), __func__, new_iobuf->page_size, PAGE_SIZE);
    return MT_ERROR;
  }
  new_iobuf->nr_pages = nr_pages;
  new_iobuf->va = va;
  new_iobuf->pgalign_va = MT_DOWN_ALIGNX_VIRT(va, new_iobuf->page_shift);
  new_iobuf->size = size;
  new_iobuf->last_addr = new_iobuf->pgalign_va + (BUF_PAGES(va,size,new_iobuf->page_size)<<new_iobuf->page_shift)-1;
  new_iobuf->prot_ctx = prot_ctx;
  new_iobuf->pid = MOSAL_getpid();
  new_iobuf->next=NULL;
  new_iobuf->prev=NULL;
  new_iobuf->mlock_ctx = NULL;
  new_iobuf->mm = current->mm;
  new_iobuf->flags = flags;
  new_iobuf->perm = req_perm;
  new_iobuf->os_dep_flags = 0;
  new_iobuf->big_pages = big_pages;
  new_iobuf->any_big_pages = any_big_pages;
  new_iobuf->iomem = iomem;
  *iobuf_p = new_iobuf;
  return MT_OK;
}


/*
 *  destroy_mosal_iobuf
 */
static void destroy_mosal_iobuf(MOSAL_iobuf_t iobuf)
{
  if ( !iobuf->map_list ) {
    COND_FREE(iobuf->map_arr, sizeof(struct page *)*iobuf->map_arr_sz);
  }
  kmem_cache_free(iobuf_cache, iobuf);
}



/*
 *  mosal_scan_region
 */
static call_result_t mosal_scan_region(MOSAL_iobuf_t iobuf, MT_u_int_t en_wr)
{
  MT_virt_addr_t start;
  MT_phys_addr_t pa;
  u_int32_t i, k;
  struct page *page;
  call_result_t rc;

  if ( en_wr && (iobuf->prot_ctx==MOSAL_get_kernel_prot_ctx()) ) {
    return MT_EINVAL;
  }

  start = MT_DOWN_ALIGNX_VIRT(iobuf->va, iobuf->page_shift);
  if ( iobuf->kmalloced ) {
    /* we do not increment ref count on kmalloced pages */
    rc = MOSAL_virt_to_phys_ex(iobuf->prot_ctx, start, &page, &pa, 0);
    if ( rc != MT_OK ) return MT_ERROR;
    iobuf->map_arr[0] = page;
    return MT_OK;
  }

  if ( !iobuf->map_list ) {
    for ( i=0; i<iobuf->nr_pages; ++i, start+=iobuf->page_size ) {
      rc = MOSAL_virt_to_phys_ex(iobuf->prot_ctx, start, &page, &pa, en_wr);
      if ( rc != MT_OK ) {
        if ( !en_wr ) {
          MTL_ERROR1(MT_FLFMT("%s: MOSAL_virt_to_phys_ex returned invalid physical address: va="VIRT_ADDR_FMT), __func__, start);
          for ( k=i; k>0; --k ) {
            page = iobuf->map_arr[k-1];
            if ( !(iobuf->any_big_pages) && VALID_PAGE(page) && !iobuf->iomem ) put_page(page);
          }
        }
        return rc;
      }
      if ( !en_wr ) {
        iobuf->map_arr[i] = page;
        MTL_DEBUG1(MT_FLFMT("%s: addr="VIRT_ADDR_FMT", count before get_page=%u"), __func__, start, 
                   VALID_PAGE(page) ? atomic_read(&page->count) : 
                                      0/*pages beyond host mem. - no ref.cnt.*/);
        if ( !(iobuf->any_big_pages) && VALID_PAGE(page) && !iobuf->iomem ) get_page(page);
      }
    }
  }
  else {
    struct map_item_st *tmp=iobuf->map_list, *rback;

    while ( tmp ) {
      MTL_DEBUG1(MT_FLFMT("%s: iobuf=%p, elem_count=%d"), __func__, iobuf, tmp->elem_count);
      for ( i=0; i<tmp->elem_count; ++i ) {
        rc = MOSAL_virt_to_phys_ex(iobuf->prot_ctx, start, &page, &pa, en_wr);
        if ( rc != MT_OK ) {
          if ( !en_wr ) {
            MTL_ERROR1(MT_FLFMT("%s: MOSAL_virt_to_phys_ex returned invalid physical address: va=0x"VIRT_ADDR_FMT), __func__, start);
            for( rback=iobuf->map_list; rback&&(rback!=tmp); rback=rback->next ) {
              for ( k=rback->elem_count; k>0; --k ) {
                page = rback->arr[k-1];
                if ( !(iobuf->any_big_pages) && VALID_PAGE(page) && !iobuf->iomem ) put_page(page);
              }
            }
            for ( k=i; k>0; --k ) {
              page = tmp->arr[k-1];
              if ( !(iobuf->any_big_pages) && VALID_PAGE(page) && !iobuf->iomem ) put_page(page);
            }
          }
          return rc;
        }
        if ( !en_wr ) {
          tmp->arr[i] = page;
          MTL_DEBUG1(MT_FLFMT("%s: addr="VIRT_ADDR_FMT", count before get_page=%u"), __func__, start, 
            VALID_PAGE(page) ? atomic_read(&page->count) : 
                               0/*pages beyond host mem. - no ref.cnt.*/);
          if ( !(iobuf->any_big_pages) && VALID_PAGE(page) && !iobuf->iomem ) get_page(page);
        }
        start+=iobuf->page_size;
      }
      tmp = tmp->next;
    }
  }
  return MT_OK;
}

/*
 *  mosal_get_pages
 */
static call_result_t mosal_get_pages(MOSAL_iobuf_t iobuf)
{
  return mosal_scan_region(iobuf, 0);
}


/*
 *  mosal_put_pages
 */
static void mosal_put_pages(MOSAL_iobuf_t iobuf)
{
  u_int32_t i;
  struct page *page;
  MT_virt_addr_t start;

  if ( iobuf->kmalloced ) {
    return;
  }

  start = MT_DOWN_ALIGNX_VIRT(iobuf->va, iobuf->page_shift);
  if ( !iobuf->map_list ) {
    for ( i=0; i<iobuf->nr_pages; ++i, start+=iobuf->page_size ) {
      page = iobuf->map_arr[i];
      if ( !(iobuf->any_big_pages) && VALID_PAGE(page) && !iobuf->iomem ) put_page(page);
      MTL_DEBUG1(MT_FLFMT("%s: addr="VIRT_ADDR_FMT", count after put_page=%u"), __func__, start, 
        VALID_PAGE(page) ? atomic_read(&page->count) : 
                           0/*pages beyond host mem. - no ref.cnt.*/);
      if ( iobuf->prot_ctx == MOSAL_get_current_prot_ctx() ) {
        /* this call in order to mark the pages as dirty since the HCA
           may have written to them but that wont mark them as dirty */
//         set_page_dirty(page+j); // Check VALID_PAGE() 
      }
    }
  }
  else {
    struct map_item_st *tmp=iobuf->map_list;
    
    while ( tmp ) {
      MTL_DEBUG1(MT_FLFMT("%s: iobuf=%p, elem_count=%d"), __func__, iobuf, tmp->elem_count);
      for ( i=0; i<tmp->elem_count; ++i, start+=iobuf->page_size ) {
        page = tmp->arr[i];
        if ( !(iobuf->any_big_pages) && VALID_PAGE(page) && !iobuf->iomem ) put_page(page);
        MTL_DEBUG1(MT_FLFMT("%s: addr="VIRT_ADDR_FMT", count after put_page=%u"), __func__, start, 
          VALID_PAGE(page) ? atomic_read(&page->count) : 
                             0/*pages beyond host mem. - no ref.cnt.*/);

      }
      tmp = tmp->next;
    }
  }
}

/*
 *  print_iobuf
 */
static void print_iobuf(MOSAL_iobuf_t iobuf)
{
  MTL_ERROR1("dump iobuf at %p\n", iobuf);
  MTL_ERROR1("va="VIRT_ADDR_FMT"\n", iobuf->va);
  MTL_ERROR1("size="SIZE_T_FMT"\n", iobuf->size);
  MTL_ERROR1("pgalign_va="VIRT_ADDR_FMT"\n", iobuf->pgalign_va);
  MTL_ERROR1("last_addr="VIRT_ADDR_FMT"\n", iobuf->last_addr);
  MTL_ERROR1("kmalloced=%s\n", iobuf->kmalloced ? "TRUE" : "FALSE");
  MTL_ERROR1("map_arr_sz=%d\n", iobuf->map_arr_sz);
  MTL_ERROR1("nr_pages=%d\n", iobuf->nr_pages);
  MTL_ERROR1("page_size=%d\n", iobuf->page_size);
  MTL_ERROR1("page_shift=%d\n", iobuf->page_shift);
  MTL_ERROR1("page_sz_ratio=%d\n", iobuf->page_sz_ratio);
  MTL_ERROR1("prot_ctx=%s\n", iobuf->prot_ctx==MOSAL_get_current_prot_ctx() ? "USER" : "KERNEL");
  MTL_ERROR1("mlock_ctx=%p\n", iobuf->mlock_ctx);
  MTL_ERROR1("next=%p\n", iobuf->next);
  MTL_ERROR1("prev=%p\n", iobuf->prev);
  MTL_ERROR1("map_arr=%p\n", iobuf->map_arr);
  MTL_ERROR1("map_list=%p\n", iobuf->map_list);
  MTL_ERROR1("pid="MT_PID_FMT"\n", iobuf->pid); /* pointer to an array of struct page * mapping the buffer */
  MTL_ERROR1("mm=%p\n", iobuf->mm);
  MTL_ERROR1("big_pages=%s\n", iobuf->big_pages ? "YES" : "NO");
  MTL_ERROR1("iomem=%s\n", iobuf->iomem ? "YES" : "NO");
  MTL_ERROR1("any_big_pages=%s\n", iobuf->any_big_pages ? "YES" : "NO");
}


typedef enum {
  BP_NO_INIT,
  BP_IS_BIG_PAGE,
  BP_IS_REG_PAGE,
  BP_IS_MIXED_PAGE
}
bp_state_t;


#if defined(VM_BIGPAGE) || defined(VM_HUGETLB)
/*
 *  big_page_state_trans
 */
static int big_page_state_trans(bp_state_t cur_state, int vma_bp)
{
  bp_state_t new_state;

  switch ( cur_state ) {
    case BP_NO_INIT:
      if ( vma_bp ) new_state = BP_IS_BIG_PAGE;
      else new_state = BP_IS_REG_PAGE;
      break;

    case BP_IS_BIG_PAGE:
      if ( vma_bp ) new_state = BP_IS_BIG_PAGE;
      else new_state = BP_IS_MIXED_PAGE;
      break;

    case BP_IS_REG_PAGE:
      if ( vma_bp ) new_state = BP_IS_MIXED_PAGE;
      else new_state = BP_IS_REG_PAGE;
      break;

    case BP_IS_MIXED_PAGE:
      new_state = BP_IS_MIXED_PAGE;
      break;

    default:
      MTL_ERROR1(MT_FLFMT("%s: called with invalid state (%d)"), __func__, cur_state);
      new_state = BP_IS_REG_PAGE;
  }
  return new_state;
}
#endif

/*
 *  check_perm
 */
static call_result_t check_perm(MT_virt_addr_t va, MT_size_t size, MOSAL_prot_ctx_t prot_ctx,
                         MOSAL_mem_perm_t req_perm, MT_bool *bp_p, MT_bool *any_bp_p, MT_bool *iomem_p)
{
  unsigned long addr, last;
  struct vm_area_struct *vma, *prev, *start_vma;
  bp_state_t big_page_state=BP_NO_INIT;  /* unknown */
  MT_bool any_bp = FALSE;

  *iomem_p = FALSE;
  if ( prot_ctx == MOSAL_get_kernel_prot_ctx() ) {
    /* permissin granted by definition for kernel memory */
    *bp_p = FALSE;
    *any_bp_p = FALSE;
    return MT_OK;
  }

  MTL_TRACE1(MT_FLFMT("%s: va="VIRT_ADDR_FMT", size="SIZE_T_FMT", write_perm=%s, read_perm=%s"),
             __func__, va, size, req_perm&MOSAL_PERM_WRITE?"yes":"no", req_perm&MOSAL_PERM_READ?"yes":"no");
  addr = (unsigned long)va;

	down_read(&current->mm->mmap_sem);
  vma = find_vma(current->mm, addr);
  if ( !vma || addr<vma->vm_start) {
    /* addr is not included in the vma */
    up_read(&current->mm->mmap_sem);
    return MT_ENOMEM;
  }
  else {
    /* addr contained in vma */
    if ( !perm_ok(req_perm, vma->vm_flags) ) {
      /* permissions not granted */
      up_read(&current->mm->mmap_sem);
      return MT_EPERM;
    }
    else {
#ifdef VM_BIGPAGE
    if ( vma->vm_flags & VM_BIGPAGE ) big_page_state = big_page_state_trans(big_page_state, 1);
    else big_page_state = big_page_state_trans(big_page_state, 0);
    if (vma->vm_ops && /*(vma->vm_ops->nopage == shmem_nopage) && */I_BIGPAGE(vma->vm_file->f_dentry->d_inode)) any_bp = TRUE;
#else
  #ifdef VM_HUGETLB
    if ( vma->vm_flags & VM_HUGETLB ) big_page_state = big_page_state_trans(big_page_state, 1);
    else big_page_state = big_page_state_trans(big_page_state, 0);
    if ( vma->vm_flags & VM_HUGETLB ) any_bp = TRUE;
  #endif
#endif  
#ifdef VM_IOREMAP
    if ( vma->vm_flags & VM_IOREMAP ) *iomem_p = TRUE;
#endif
    }
  }

  last = addr + size;
  start_vma = vma;
  while ( last > vma->vm_end ) {
    prev = vma;
    vma = vma->vm_next;
    /* we require regions to be adjacent */
    if ( !vma || (prev->vm_end!=vma->vm_start) ) {
      up_read(&current->mm->mmap_sem);
      return MT_ENOMEM;
    }
    else {
      if ( !perm_ok(req_perm, vma->vm_flags) ) {
        /* permission not granted */
        up_read(&current->mm->mmap_sem);
        return MT_EPERM;
      }
      else {
#ifdef VM_BIGPAGE
        if ( vma->vm_flags & VM_BIGPAGE ) big_page_state = big_page_state_trans(big_page_state, 1);
        else big_page_state = big_page_state_trans(big_page_state, 0);
        if (vma->vm_ops && /*(vma->vm_ops->nopage == shmem_nopage) && */I_BIGPAGE(vma->vm_file->f_dentry->d_inode)) any_bp = TRUE;
#else
  #ifdef VM_HUGETLB
        if ( vma->vm_flags & VM_HUGETLB ) big_page_state = big_page_state_trans(big_page_state, 1);
        else big_page_state = big_page_state_trans(big_page_state, 0);
        if ( vma->vm_flags & VM_HUGETLB ) any_bp = TRUE;
  #endif
#endif  
#ifdef VM_IOREMAP
    if ( vma->vm_flags & VM_IOREMAP ) *iomem_p = TRUE;
#endif
      }
    }
  }
  
	up_read(&current->mm->mmap_sem);
  switch ( big_page_state ) {
    case BP_IS_BIG_PAGE:
      *bp_p = TRUE;
      break;

    case BP_NO_INIT:
    case BP_IS_REG_PAGE:
      *bp_p = FALSE;
      break;

    case BP_IS_MIXED_PAGE:
      return MT_EINVAL;

    default:
      return MT_ERROR;
  }
  *any_bp_p = any_bp;
  return MT_OK;
}


/*
 *  MOSAL_iobuf_cmp_tpt
 */
int MOSAL_iobuf_cmp_tpt(MOSAL_iobuf_t iobuf_1, MOSAL_iobuf_t iobuf_2)
{
  u_int32_t i;
  map_item_t *m1, *m2;


  /* sanity check */
  if ( !iobuf_1 || !iobuf_2 ) return MT_EINVAL;

  if ( iobuf_1->prot_ctx != iobuf_2->prot_ctx ) {
    /* iobufs do not belog to the same protection context i.e
       one is user spcae and the other is kernel space */
    return -1;
  }
  else if ( iobuf_1->prot_ctx == MOSAL_get_kernel_prot_ctx() ) {
    /* iobufs define kernel addresses */
    if ( (iobuf_1->va!=iobuf_2->va) || (iobuf_1->size!=iobuf_2->size) ) {
      return -1;
    }
    else {
      return 0;
    }
  }
  else {
    /* iobufs define user space addresses */
    if ( iobuf_1->page_sz_ratio != iobuf_2->page_sz_ratio ) {
      return -1;
    }
    if ( iobuf_1->page_size != iobuf_2->page_size ) {
      return -1;
    }
    if ( (!iobuf_1->map_list && iobuf_2->map_list) || (iobuf_1->map_list && !iobuf_2->map_list)  ) {
      return -1;
    }
    if ( iobuf_1->map_arr_sz != iobuf_2->map_arr_sz ) {
      /* this test is applicable to both cases of using array and linked list since
         in both cases in user space the map_arr_sz field contains the number of elemnts */
      return -1;
    }
    if ( !iobuf_1->map_list ) {
      for ( i=0; i<iobuf_1->map_arr_sz; ++i ) {
        if ( iobuf_1->map_arr[i] != iobuf_2->map_arr[i] ) {
          return -1;
        }
      }
      return 0;
    }
    else {
      /* since page sizes and number of items are identical in both iobufs I assume that
         the number of elements are identical so it is enough to check the m1 is not null */
      for ( m1=iobuf_1->map_list, m2=iobuf_2->map_list; m1; m1=m1->next, m2=m2->next ) {
        for ( i=0; i<m1->elem_count; ++i ) {
          if ( m1->arr[i] != m2->arr[i] ) {
            return -1;
          }
        }
      }
      return 0;
    }
  }
}

/*
 *  MOSAL_iobuf_get_tpt_seg
 */
call_result_t MOSAL_iobuf_get_tpt_seg(MOSAL_iobuf_t iobuf, MOSAL_iobuf_iter_t *iterator_p,
                                      MT_size_t n_pages_in, MT_size_t *n_pages_out_p,
                                      MT_phys_addr_t *page_tbl_p)
{
  unsigned int total_left, i, j, n;
  MT_phys_addr_t pa;

  /* sanity check */
  if ( !iobuf || !iterator_p ) return MT_EINVAL;

  if ( !iobuf->map_list ) {
    total_left = iobuf->nr_pages - iterator_p->elem_idx; /* number of translations not yet provided */

    n = total_left <= n_pages_in ? total_left : n_pages_in;
    if ( iobuf->kmalloced ) {
      pa = mosal_page_to_phys(iobuf->map_arr[0]);
      pa += iterator_p->elem_idx * iobuf->page_size;
      for ( i=0; i<n; ++i, pa+=iobuf->page_size ) {
        page_tbl_p[i] = pa;
      }
    }
    else {
      for ( i=0, j=iterator_p->elem_idx; i<n; ++i, ++j ) {
        pa = mosal_page_to_phys(iobuf->map_arr[j]);
        page_tbl_p[i] = pa;
      }
    }
    iterator_p->elem_idx += n;
    *n_pages_out_p = n;
  }
  else {
    j = 0;
    *n_pages_out_p = 0;
    while ( iterator_p->item_p ) {
      total_left = iterator_p->item_p->elem_count - iterator_p->elem_idx; /* total left in this item */
      n = total_left <= n_pages_in ? total_left : n_pages_in;
      for ( i=0; i<n; ++i, ++iterator_p->elem_idx, ++j ) {
        pa = mosal_page_to_phys(iterator_p->item_p->arr[iterator_p->elem_idx]);
        page_tbl_p[j] = pa;
      }
      (*n_pages_out_p) += n;
      /* this seems to solve a misterious bug in NUMA systems
         needs farther investigation */
      if ( !iterator_p || !iterator_p->item_p ) {
        MTL_ERROR1(MT_FLFMT("%s: iterator_p=%p, iterator_p->item_p=%p"), __func__, iterator_p, iterator_p->item_p);
        return MT_ERROR;
      }
      if ( iterator_p->elem_idx < iterator_p->item_p->elem_count ) {
        break;
      }
      else {
        iterator_p->item_p = iterator_p->item_p->next;
        iterator_p->elem_idx = 0;
        n_pages_in -= n;
      }
    }
  }
  return MT_OK;
}


/*
 *  MOSAL_iobuf_iter_init
 */
call_result_t MOSAL_iobuf_iter_init(MOSAL_iobuf_t iobuf, MOSAL_iobuf_iter_t *iterator_p)
{
  /* sanity check */
  if ( !iobuf ) return MT_EINVAL;

  if ( iobuf->map_list ) {
    iterator_p->item_p = iobuf->map_list;
  }
  iterator_p->elem_idx = 0;
  return MT_OK;
}


/*
 *  mosal_flush_tlb
 */
static void mosal_flush_tlb(MOSAL_iobuf_t iobuf)
{
  /* first we try to prioritize the operation we do */
#if defined(CONFIG_SMP) && (defined(__i386__) || defined(__x86_64__))
  struct vm_area_struct *vma;
  MT_virt_addr_t va = iobuf->va;

  down_read(&current->mm->mmap_sem);
  vma = find_vma(current->mm, va);
  if ( vma && va>=vma->vm_start ) {
    while ( va < (iobuf->va+iobuf->size+1) ) {
      flush_tlb_page(vma, va);
      MTL_DEBUG1(MT_FLFMT("%s(pid="MT_PID_FMT"): calling flush_tlb_page for va="VIRT_ADDR_FMT),
                 __func__, MOSAL_getpid(), va);
      va += (1<<iobuf->page_shift);
    }
  }
  up_read(&current->mm->mmap_sem);
#else
#ifdef MT_FLUSH_TLB_TAKES_VMA
  struct vm_area_struct *vma;
  MT_virt_addr_t va = iobuf->va;

  down_read(&current->mm->mmap_sem);
  vma = find_vma(current->mm, va);
  if ( vma && va>=vma->vm_start ) {
    flush_tlb_range(vma, iobuf->va, iobuf->va+iobuf->size);
  }
  up_read(&current->mm->mmap_sem);
#else
  flush_tlb_range(current->mm, iobuf->va, iobuf->va+iobuf->size);
#endif
#endif
}



/*
 *  MOSAL_iobuf_restore_perm
 */
call_result_t MOSAL_iobuf_restore_perm(MOSAL_iobuf_t iobuf)
{
  call_result_t rc;

  /* sanity check */
  if ( !iobuf ) return MT_EINVAL;

  if ( !(iobuf->os_dep_flags&MOSAL_IOBUF_LNX_FLG_PROP_MARKED_DONT_COPY) && (iobuf->perm&MOSAL_PERM_WRITE) ) {
    /* enable write on all ptes */
    MTL_DEBUG1(MT_FLFMT("%s(pid="MT_ULONG_PTR_FMT"): restoring write permissions"), __func__, MOSAL_getpid());
    rc = mosal_scan_region(iobuf, 1);
    if ( rc == MT_OK ) {
      mosal_flush_tlb(iobuf);
    }
    return rc;
  }
  else {
    MTL_DEBUG1(MT_FLFMT("%s(pid="MT_ULONG_PTR_FMT"): ***NOT*** restoring write permissions, os_dep_flags=%d, perm=0x%X"), __func__, MOSAL_getpid(), iobuf->os_dep_flags, iobuf->perm);
  }
  return MT_OK;
}

