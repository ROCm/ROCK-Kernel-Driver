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

#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/unistd.h>
#ifdef __ia64__
  #undef _syscall2
  #define _syscall2(type,name,type1,arg1,type2,arg2)                      \
  type                                                                    \
  name (type1 arg1, type2 arg2)                                           \
  {                                                                       \
          register long dummy3 __asm__ ("out2") = 0;                      \
          register long dummy4 __asm__ ("out3") = 0;                      \
          register long dummy5 __asm__ ("out4") = 0;                      \
                                                                          \
          return __ia64_syscall((long) arg1, (long) arg2, dummy3, dummy4, \
                                dummy5, __NR_##name);                     \
  }
  static inline _syscall2(int,mlock,const void*,addr,size_t,len);
  static inline _syscall2(int,munlock,const void*,addr,size_t,len);
#endif
  
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
#include "mosal_priv.h"
#include <mosal_mlock.h>

#ifndef	 __ia64__
  #if !defined(powerpc) && !LINUX_KERNEL_2_6
    #if (!defined(RH_AS_3_0)) || (!defined(__x86_64__))
      #include <linux/syscall.h>
    #endif
  #endif  
#endif	

#if LINUX_KERNEL_2_6
#  ifndef CONFIG_KALLSYMS
#    error We need CONFIG_KALLSYMS to find mlock/munlock pointers
#  endif
#  include <linux/kallsyms.h>
#else
#  if (!defined(RH_AS_3_0)) || (!defined(__x86_64__))
#    include <sys/syscall.h>
#  endif
#  include <linux/compatmac.h>
#endif

#define MM_ROOT_ID 0

#define PARANOID
#define CLOSE_IOBUFS_LIST ((void *)(-1))

#ifndef list_for_each_safe
 #define list_for_each_safe(pos, n, head) \
         for (pos = (head)->next, n = pos->next; pos != (head); \
                 pos = n, n = pos->next)
#endif

#ifdef __ia64__
  #define ADDR_TYPE_PREFIX (void *)
#else
  #define ADDR_TYPE_PREFIX
#endif
                 
/* type to hold the sys_mlock and sys_munlock pointers */
typedef asmlinkage long (*sys_lock_ptr_t)(unsigned long, size_t);

static sys_lock_ptr_t mlock_ptr, munlock_ptr;


#if (!defined(RH_AS_3_0)) || (!defined(__x86_64__))
extern void * sys_call_table[]; /*syscall functions'  table*/
#endif

typedef int (*lock_func_t)(unsigned long , int);

typedef struct {
  struct list_head list; /* used to link a list of regions */
  unsigned long pg_start; /* start page number of this region */
  unsigned long pg_end; /* last page number of this region */
  u_int32_t count; /* reference count on the region */
}
mregion_t;


typedef struct {
  struct list_head head; /* head of the list of regions */
  MOSAL_mutex_t mtx;    /* mutext to protect the region */
}
mlist_t;

#define SMALL_PAGE_IDX 0
#define BIG_PAGE_IDX 1
#define MAX_PAGE_LISTS 2

#define HASH_SIZE  1543 /* prime number */
static struct list_head list_arr[HASH_SIZE];
static MOSAL_mutex_t hash_mtx;

typedef struct MOSAL_mlock_ctx_st {
  struct list_head list; /* list of contexts that hash to the same index in the hash */
  struct mm_struct *mm;
  mlist_t mlist[MAX_PAGE_LISTS];     /* there are two list each for one of two page sizes */
  MOSAL_iobuf_t iobufs_list;
  MOSAL_spinlock_t iobufs_list_spl;
  u_int32_t ref_cnt;
} hash_item_t;


static kmem_cache_t *mregion_cache = NULL;
static MOSAL_spinlock_t rel_spl; /* spinlock used to control the release of an hitem */

static call_result_t free_regions(hash_item_t *hitem);
static call_result_t priv_mosal_mlock(MT_virt_addr_t addr, MT_size_t size, unsigned int page_shift,
                                      hash_item_t **hitem_pp);
static call_result_t priv_mosal_munlock(MT_virt_addr_t addr, MT_size_t size, unsigned int page_shift,
                                        hash_item_t *hitem);


#define FREE_PAGES_WATERMARK (1<<9) /* 2MB @ page size=4K*/
inline static int sufficient_mem(void)
{
#if defined(RH_9_0) || (LINUX_VERSION_CODE == KERNEL_VERSION(2,4,7)) || defined(powerpc) || (LINUX_VERSION_CODE == KERNEL_VERSION(2,4,21)) || (LINUX_VERSION_CODE == KERNEL_VERSION(2,4,22)) || (LINUX_VERSION_CODE == KERNEL_VERSION(2,4,23)) || LINUX_KERNEL_2_6
  return 1;
#else
  if ( nr_free_pages() >= FREE_PAGES_WATERMARK ) {
    return 1;
  }
  else {
    MTL_DEBUG4(MT_FLFMT("Insufficient memory for locking (%d pages left)"), nr_free_pages());
    return 0;
  }
#endif
}

inline static unsigned long my_ulmin(unsigned long x, unsigned long y)
{
  if ( x < y ) {
    return x;
  }
  return y;
}

static void inline mt_yield(void)
{
#if (LINUX_VERSION_CODE == KERNEL_VERSION(2,4,7)) || ( defined(MT_SUSE_PRO_80) || defined(powerpc) )
  /* red-hat 7.2 */
//  current->policy = SCHED_YIELD;
  schedule();
#else
  yield();
#endif
}


/*call for sys_mlock in kernel space*/
static call_result_t lock_it(unsigned long a, size_t b) {
  int ret;
  __u32 old_cap;
  unsigned long tmp_l;

  old_cap = cap_t(current->cap_effective);
  cap_raise(current->cap_effective, CAP_IPC_LOCK);

#ifndef __ia64__
  ret = (*mlock_ptr)(a, b);
#else
  ret = mlock((void *)a, (size_t)b);
#endif

  cap_t(current->cap_effective) = old_cap;

  switch (ret) {
    case 0:
      if ( !sufficient_mem() ) {
        /* Invoking __get_free_pages() with MAX_ORDER-1 and no __GFP_WAIT should fail, 
         * but will wake kswapd up                                                    */
        if ( (tmp_l= __get_free_pages(0, MAX_ORDER-1)) ) {
          MTL_ERROR4(MT_FLFMT("%s: __get_free_pages(0,%d) succeeded but was expected to fail here!"),
                      __func__, MAX_ORDER-1);
          free_pages(tmp_l,MAX_ORDER-1);
        }
        else {
          /* If __get_free_pages() failed then kswapd has been put in the running queue
             and yield will make run before reaching the next call to sufficient_mem() */
          mt_yield();

          /* now we expect to have enough low memory */
          if ( !sufficient_mem() ) {
  #ifndef __ia64__
            (*munlock_ptr)(a, b);
  #else
            munlock((void *)a, (size_t)b);
  #endif
            MTL_ERROR4(MT_FLFMT("Rejecting locking due to insufficient physical memory (pid=%u , va=0x%p, size="SIZE_T_FMT),current->pid,(void*)a,b);
            return MT_EAGAIN;
          }
        }
      }
      return MT_OK;
    case -ENOMEM:
      return MT_ENOMEM;
    case -EPERM:
      /* this is not a typo. it is deliberate. It is because in ia64 machines
         this value returns when the process has locked lots of memory - in this case
         we want to interpret it as eagain */
      MTL_DEBUG1(MT_FLFMT("%s: sys_mlock retured -EPERM"), __func__);
      return MT_EAGAIN;
    case -EINVAL:
      return MT_EINVAL;
    case -EAGAIN:
      return MT_EAGAIN;
    default:
      MTL_ERROR4(MT_FLFMT("lock_it: unknown error (%d)"),ret);
      return MT_ERROR;
  }
}



/*call for sys_munlock in kernel space*/
static call_result_t unlock_it(unsigned long a, size_t b)
{
  int ret;

#ifndef __ia64__
  ret= (*munlock_ptr)(a, b);
#else
  ret= munlock((void *)a, (size_t)b);
#endif
  switch (ret) {
    case 0:
      return MT_OK;
    case -ENOMEM:
      return MT_ENOMEM;
    case -EINVAL:
      return MT_EINVAL;
    default:
      MTL_ERROR4(MT_FLFMT("unlock_it: unknown error (%d)"),ret);
      return MT_ERROR;
  }

}



static hash_item_t *hash_find(struct mm_struct *mm);
static call_result_t hash_insert(struct mm_struct *mm, hash_item_t **hitem_pp);
static void hash_item_release(hash_item_t *hitem_p);



#ifdef sys_sync
extern long sys_sync(void);
#endif

/*
 *  find the pointers to sys_mlock and sys_munlock functions
 */
int find_m_lock_unlock_ptrs(sys_lock_ptr_t *lock_p, sys_lock_ptr_t *unlock_p)
{
#ifdef __ia64__
  return 0;
#elif LINUX_KERNEL_2_6
  char *modname;
  const char *name;
  unsigned long offset, size, n;
  char namebuf[128];
  unsigned long cur_addr = (unsigned long)kernel_thread;

  *lock_p = *unlock_p = 0;

  n = 0;
  while ( (((*lock_p)==NULL) || ((*unlock_p)==NULL)) && (n<1000000) ) {
    name = kallsyms_lookup(cur_addr, &size, &offset, &modname, namebuf);
    if (!strcmp(name, "sys_mlock")) {
      *lock_p = (sys_lock_ptr_t) cur_addr;
    }
    if (!strcmp(name, "sys_munlock")) {
      *unlock_p = (sys_lock_ptr_t) cur_addr;
    }
    cur_addr += size;
    n++;
  }
  if ( *lock_p && *unlock_p ) {
    return 0;
  }
  else return -1;

#elif defined(sys_call_table) || defined(USE_SYS_CALL_TABLE) || ( defined(__x86_64__) && !defined(RH_AS_3_0) )
  *lock_p = (sys_lock_ptr_t)sys_call_table[__NR_mlock]; 
  *unlock_p = (sys_lock_ptr_t)sys_call_table[__NR_munlock]; 
  return 0;
#else
  unsigned long *uts_p = (unsigned long *)&system_utsname;
  u_int32_t i;

  for ( i=0; i<10000000; ++i, ++uts_p ) {
    if ( uts_p[0]==(unsigned long)sys_read && 
         uts_p[__NR_write-__NR_read]==(unsigned long)sys_write &&
         uts_p[__NR_close-__NR_read]==(unsigned long)sys_close 
#ifdef sys_sync
         && uts_p[__NR_sync-__NR_read]==(unsigned long)sys_sync
#endif
#ifdef sys_dup
         && uts_p[__NR_dup-__NR_read]==(unsigned long)sys_dup
#endif
#ifdef sys_chroot
         && uts_p[__NR_chroot-__NR_read]==(unsigned long)sys_chroot
#endif
#ifdef sys_chdir
         && uts_p[__NR_chdir-__NR_read]==(unsigned long)sys_chdir
#endif
#ifdef sys_fcntl
         && uts_p[__NR_fcntl-__NR_read]==(unsigned long)sys_fcntl
#endif
#ifdef sys_wait4
         && uts_p[__NR_wait4-__NR_read]==(unsigned long)sys_wait4
#endif
         ) {
      *lock_p = (sys_lock_ptr_t)uts_p[__NR_mlock-__NR_read];
      *unlock_p = (sys_lock_ptr_t)uts_p[__NR_munlock-__NR_read];
      return 0;
    }
  }
  return -1;
#endif
}


/*
 *  should_lock
 */
static inline int should_lock(struct vm_area_struct *vma)
{
  if ( (vma->vm_flags&VM_LOCKED) ||
       (vma->vm_flags&VM_IO)
     ) {
    return 0;
  }
  else {
    return 1;
  }
}


/*
 *  should_unlock
 */
static inline int should_unlock(struct vm_area_struct *vma)
{
  if (
       (vma->vm_flags&VM_IO)
#ifdef VM_NO_UNLOCK
       || (vma->vm_flags&VM_NO_UNLOCK)
#endif
     ) {
    return 0;
  }
  else {
    return 1;
  }
}

struct lock_item_st {
  struct lock_item_st *next;
  MT_virt_addr_t addr;
  MT_size_t size;
};


/*
 *  mark_vma_for_fork
 *    this function should be called after locking since
 *    locking the memory might split VMAs and we want to mark
 *    only those that we locked
 */
static call_result_t mark_vma_for_fork(MT_virt_addr_t va, MT_size_t size, int mark)
{
  struct vm_area_struct *vma;
  call_result_t rc = MT_OK;
  unsigned long addr=va, vma_sz;
  long sz=(long)size;
  unsigned long mask;


#ifdef VM_NO_COW
  if ( mark ) mask = VM_NO_COW;
  else mask = ~VM_NO_COW;
#else
  if ( mark ) mask = VM_DONTCOPY;
  else mask = ~VM_DONTCOPY;
#endif

	down_write(&current->mm->mmap_sem);
  while ( sz > 0 ) {
    vma = find_vma(current->mm, addr);
    if ( !vma || addr<vma->vm_start ) {
      rc = MT_ENOMEM;
      goto ex_mark_vma_for_fork;
    }
    else {
      if ( mark ) vma->vm_flags |= mask;
      else vma->vm_flags &= mask;
    }
    vma_sz = vma->vm_end - vma->vm_start;
    sz -= vma_sz;
    addr += vma_sz;
  }
ex_mark_vma_for_fork:
  up_write(&current->mm->mmap_sem);
  return rc;
}


/*
 *  cond_lock_region
 */
static call_result_t cond_lock_region(MT_virt_addr_t addr, MT_size_t size)
{
  struct vm_area_struct *vma;
  MT_virt_addr_t va = addr;
  MT_size_t lock_sz;
  long sz = (long)size;
  call_result_t rc=MT_OK;
  struct lock_item_st *litem_p=NULL, *new_item, *tmp;

  /* we must build a linked list on the fly since we cannot call
     sys_mlock() when mmap_sem is held */
  while ( sz > 0 ) {
    down_read(&current->mm->mmap_sem);
    vma = find_vma(current->mm, va);
    if ( vma ) {
      if ( va>=vma->vm_start ) {
        lock_sz = my_ulmin(vma->vm_end-va, sz);
        if ( should_lock(vma) ) {
          up_read(&current->mm->mmap_sem);
          new_item = TMALLOC(struct lock_item_st);
          if ( !new_item ) {
            MTL_ERROR1(MT_FLFMT("%s(%s,pid="MT_PID_FMT"): failed allocating memory"),
                       __func__, current->comm, MOSAL_getpid());
            rc = MT_EAGAIN;
            break;
          }
          new_item->next = litem_p;
          new_item->addr = va;
          new_item->size = lock_sz;
          litem_p = new_item;
        }
        else {
          up_read(&current->mm->mmap_sem);
        }
        va += lock_sz;
        sz -= lock_sz;
      }
      else {
        up_read(&current->mm->mmap_sem);
        va += (vma->vm_end-va);
        sz -= (vma->vm_end-va);
      }
    }
    else {
      up_read(&current->mm->mmap_sem);
      break;
    }
  }
  while ( litem_p ) {
    tmp = litem_p->next;
    if ( rc == MT_OK ) {
      rc = lock_it(litem_p->addr, litem_p->size);
    }
    FREE(litem_p);
    litem_p = tmp;
  }
  return rc;
}


/*
 *  cond_unlock_region
 */
static void cond_unlock_region(MT_virt_addr_t addr, MT_size_t size)
{
  struct vm_area_struct *vma;
  MT_virt_addr_t va = addr;
  MT_size_t lock_sz;
  long sz = (long)size;
  struct lock_item_st *litem_p=NULL, *new_item, *tmp;

  /* we must build a linked list on the fly since we cannot call
     sys_mlock() when mmap_sem is held */
  while ( sz > 0 ) {
    down_read(&current->mm->mmap_sem);
    vma = find_vma(current->mm, va);
    if ( vma ) {
      if ( va>=vma->vm_start ) {
        lock_sz = my_ulmin(vma->vm_end-va, sz);
        if ( should_unlock(vma) ) {
          up_read(&current->mm->mmap_sem);
          new_item = TMALLOC(struct lock_item_st);
          if ( !new_item ) {
            MTL_ERROR1(MT_FLFMT("%s(%s,pid="MT_PID_FMT"): failed allocating memory"),
                       __func__, current->comm, MOSAL_getpid());
            break;
          }
          new_item->next = litem_p;
          new_item->addr = va;
          new_item->size = lock_sz;
          litem_p = new_item;
        }
        else {
          up_read(&current->mm->mmap_sem);
        }
        va += lock_sz;
        sz -= lock_sz;
      }
      else {
        up_read(&current->mm->mmap_sem);
        va += (vma->vm_end-va);
        sz -= (vma->vm_end-va);
      }
    }
    else {
      up_read(&current->mm->mmap_sem);
      break;
    }
  }
  while ( litem_p ) {
    tmp = litem_p->next;
    unlock_it(litem_p->addr, litem_p->size);
    FREE(litem_p);
    litem_p = tmp;
  }
}

static call_result_t lock_region(unsigned long pg_start, unsigned long pg_end, u_int8_t page_shift)
{
  call_result_t rc;
  unsigned long start;
  size_t size;

  start = pg_start << page_shift;
  size = (pg_end - pg_start + 1) << page_shift;
  rc = lock_it(start, size);
#ifndef __KERNEL__

//  printf("locked a region: %ld - %ld\n", rgn->pg_start, rgn->pg_end);
#endif
  return rc;
}



#define NEWREG(start, end, cnt, prev_link) ({                                                                 \
                                              mregion_t *ptr;                                                 \
                                              ptr = (mregion_t *)kmem_cache_alloc(mregion_cache, GFP_KERNEL); \
                                              if ( ptr ) {                                                    \
                                                ptr->pg_start = (start);                                      \
                                                ptr->pg_end = (end);                                          \
                                                ptr->count = (cnt);                                           \
                                                list_add(&ptr->list, (prev_link));                            \
                                              }                                                               \
                                              ptr;                                                            \
                                           })


/*
 *  merge_regions
 *     try to mergion the region pointed by pos with the region preciding it
 *     by extending the previous region and deleting the current region
 *     return 1 when regions are merged
 *            0 when regions are not merged
 */
static inline int merge_regions(struct list_head *head, struct list_head *pos)
{
  mregion_t *cur, *rprev;

  if ( pos==head ) {
    return 0;
  }
  cur = list_entry(pos, mregion_t, list);

  if ( pos->prev == head ) {
    return 0;
  }
  rprev = list_entry(pos->prev, mregion_t, list);

  if ( (cur->count==rprev->count) && ( (cur->pg_start-rprev->pg_end)==1 ) ) {
    rprev->pg_end = cur->pg_end;
    list_del(pos);
    kmem_cache_free(mregion_cache, cur);
    return 1;
  }

  return 0;
}


static call_result_t remove_region(struct list_head *head, unsigned long pg_start, unsigned long pg_end, u_int8_t page_shift)
{
  struct list_head *pos, *next, *save;
  mregion_t *cur=NULL;
  call_result_t rc=MT_OK;

  MTL_TRACE1(MT_FLFMT("%s: called for start=0x%lx, end=0x%lx"), __func__, pg_start, pg_end);
  if ( pg_start > pg_end ) {
    /* this case may happen deliberatly when insert_region fails
       and we call rollback when nothing was really done yet */
    return MT_EINVAL;
  }

  list_for_each_safe(pos, next, head) {
    cur = list_entry(pos, mregion_t, list);
    if ( pg_start <= cur->pg_end ) break;
  }

  if ( pos == next ) {
    /* list is empty */
    MTL_ERROR1(MT_FLFMT("%s: trying to unlock unavailable region: start=0x%lx, end=0x%lx"), __func__, pg_start<<page_shift, pg_end<<page_shift);
    return MT_ENOMEM;
  }

  if ( pg_start > cur->pg_end ) {
    MTL_ERROR1(MT_FLFMT("%s: trying to unlock unavailable region: start=0x%lx, end=0x%lx"), __func__, pg_start<<page_shift, pg_end<<page_shift);
    return MT_ENOMEM;
  }

  while ( 1 ) {
    if ( pg_start < cur->pg_start ) {
      unsigned long tmp_end = my_ulmin(pg_end, cur->pg_start-1);
      MTL_ERROR1(MT_FLFMT("%s: trying to unlock unavailable region: start=0x%lx, end=0x%lx"), __func__, pg_start<<page_shift, tmp_end<<page_shift);
      rc = MT_ENOMEM;
      if ( pg_end < cur->pg_start ) {
        return rc;
      }
      pg_start = cur->pg_start;
    }
    /* pg_start >= cur->pg_start */

    if ( (pg_start==cur->pg_start) && (pg_end==cur->pg_end) ) {
      if ( cur->count > 1 ) {
        cur->count--;
      }
      else {
        mark_vma_for_fork(cur->pg_start<<page_shift, (cur->pg_end-cur->pg_start+1)<<page_shift, 0);
        cond_unlock_region(pg_start<<page_shift, (pg_end-pg_start+1)<<page_shift);
        list_del(pos);
        kmem_cache_free(mregion_cache, cur);
      }
      return rc;
    }

    if ( (pg_start==cur->pg_start) && (pg_end<cur->pg_end) ) {
      cur->pg_start = pg_end+1;
      if ( cur->count == 1) {
        mark_vma_for_fork(pg_start<<page_shift, (pg_end-pg_start+1)<<page_shift, 0);
        cond_unlock_region(pg_start<<page_shift, (pg_end-pg_start+1)<<page_shift);
      }
      else {
        NEWREG(pg_start, pg_end, cur->count-1, pos->prev);
      }
      return rc;
    }

    if ( (pg_start==cur->pg_start) && (pg_end>cur->pg_end) ) {
      pg_start = cur->pg_end+1;
      save = pos->next;
      if ( cur->count > 1 ) {
        cur->count--;
      }
      else {
        cond_unlock_region(cur->pg_start<<page_shift, (cur->pg_end-cur->pg_start+1)<<page_shift);
        list_del(pos);
        kmem_cache_free(mregion_cache, cur);
      }
      if ( save == head ) {
        MTL_ERROR1(MT_FLFMT("%s: trying to unlock unavailable region: start=0x%lx, end=0x%lx"), __func__, pg_start<<page_shift, pg_end<<page_shift);
        return MT_ENOMEM;
      }
      else {
        pos = save;
        cur = list_entry(pos, mregion_t, list);
        continue;
      }
    }

    if ( pg_start > cur->pg_start ) {
#ifdef PARANOID
      if ( pg_start > cur->pg_end ) {
        MTL_ERROR1(MT_FLFMT("%s: algorithm bug: start=0x%lx, end=0x%lx, cur->pg_start=0x%lx, cur->pg_end=0x%lx"),
                   __func__, pg_start<<page_shift, pg_end<<page_shift, cur->pg_start<<page_shift, cur->pg_end<<page_shift);
        return MT_ENOMEM;
      }
#endif
      NEWREG(cur->pg_start, pg_start-1, cur->count, pos->prev);
      cur->pg_start = pg_start;
      continue;
    }
  }
}


/*
 *  insert_region
 *       insert a region into the list of regions to a list of regions
 *
 *       head_p(in) pointer to the head of the list
 *       pg_start(in) page number of the start of the region
 *       pg_end(in) page number of the end of the region
 */
static call_result_t insert_region(struct list_head *head, unsigned long pg_start, unsigned long pg_end, u_int8_t page_shift)
{
  struct list_head *pos, *next;
  mregion_t *cur=NULL;
  call_result_t rc;
  unsigned long orig_start = pg_start, rback_end=pg_start-1;


  MTL_TRACE1(MT_FLFMT("%s: called for start=0x%lx, end=0x%lx"), __func__, pg_start, pg_end);
  list_for_each_safe(pos, next, head) {
    cur = list_entry(pos, mregion_t, list);
    if ( pg_start <= cur->pg_end ) break;
  }

  if ( pos == next ) {
    /* this is the case when the list was empty so I just
       add the new entry to the head of the list */
    if ( 0 ) {
      rc = lock_region(pg_start, pg_end, page_shift);
    }
    else {
      rc = MT_OK;
    }
    if ( (rc==MT_OK) && !NEWREG(pg_start, pg_end, 1, head) ) {
      cond_unlock_region(pg_start<<page_shift, (pg_end-pg_start+1)<<page_shift);
      return MT_EAGAIN;
    }
    return rc;
  }

  if ( pg_start > cur->pg_end ) {
    /* all over this function the call to lock_region is neutralized
       since the whole region gets locked before calling this function */
    if ( 0 ) {		  
      rc = lock_region(pg_start, pg_end, page_shift);
	  }
	  else {
      rc = MT_OK;
	  }
    if ( (rc==MT_OK) && !NEWREG(pg_start, pg_end, 1, pos->prev) ) {
      cond_unlock_region(pg_start<<page_shift, (pg_end-pg_start+1)<<page_shift);
      return MT_EAGAIN;
    }
    return rc;
  }


  while ( 1 ) {

    if ( pg_start > cur->pg_end ) {
      if ( 0 ) {
        rc = lock_region(pg_start, pg_end, page_shift);
      }
      else {
        rc = MT_OK;
      }
      if ( rc == MT_OK ) {
        if ( !NEWREG(pg_start, pg_end, 1, pos->prev) ) {
          cond_unlock_region(pg_start<<page_shift, (pg_end-pg_start+1)<<page_shift);
          return MT_EAGAIN;
        }
      }
      return rc;
    }

    if ( pg_start < cur->pg_start ) {
      unsigned long tmp_end = my_ulmin(pg_end, cur->pg_start-1);

	  if ( 0 ) {
        rc = lock_region(pg_start, tmp_end, page_shift);
      }
      else {
        rc = MT_OK;
      }
      if ( rc == MT_OK ) {
        if ( ((cur->pg_start-pg_end)==1) && (cur->count==1) ) {
          /* cur may be expanded downward */
          cur->pg_start = pg_start;
          return rc;
        }
        else {
          if ( !NEWREG(pg_start, tmp_end, 1, pos->prev) ) {
            cond_unlock_region(pg_start<<page_shift, (tmp_end-pg_start+1)<<page_shift);
            /* rollback */
            remove_region(head, orig_start, rback_end, page_shift);
            return MT_EAGAIN;
          }
          if ( pg_end < cur->pg_start ) {
            return rc;
          }
          else {
            pg_start = cur->pg_start;
            rback_end = cur->pg_start-1;
            continue;
          }
        }
      }
      else {
        /* lock failed - rollback */
        remove_region(head, orig_start, rback_end, page_shift);
        return rc;
      }
    }
    else {
      /* pg_start >= cur->pg_start */
      if ( (pg_start==cur->pg_start) && (pg_end==cur->pg_end) ) {
        cur->count++;
        return MT_OK;
      }
      if ( (pg_start==cur->pg_start) && (pg_end<cur->pg_end) ) {
        if ( !NEWREG(pg_start, pg_end, cur->count+1, pos->prev) ) {
          /* rollback */
          remove_region(head, orig_start, rback_end, page_shift);
          return MT_EAGAIN;
        }
        cur->pg_start = pg_end+1;
        /* merge with prev ?? */
        return MT_OK;
      }
      if ( (pg_start==cur->pg_start) && (pg_end>cur->pg_end) ) {
        cur->count++;
        pg_start=cur->pg_end+1;
        rback_end=cur->pg_end;
        if ( pos->next == head  ) {
          if ( !NEWREG(pg_start, pg_end, 1, pos) ) {
            remove_region(head, orig_start, rback_end, page_shift);
            return MT_EAGAIN;
          }
          return MT_OK;
        }
        pos = pos->next;
        cur = list_entry(pos, mregion_t, list);
        continue;
      }

#ifdef PARANOID
      if ( pg_start <= cur->pg_start) {
        MTL_ERROR1(MT_FLFMT("%s: algorithm bug: start=0x%lx, end=0x%lx, cur->pg_start=0x%lx, cur->pg_end=0x%lx"),
                   __func__, pg_start<<page_shift, pg_end<<page_shift, cur->pg_start<<page_shift, cur->pg_end<<page_shift);
      }
#endif

      if ( pg_start > cur->pg_start) { // this if seems redundant !!! ???
        if ( !NEWREG(cur->pg_start, pg_start-1, cur->count, pos->prev) ) {
          /* rollback */
          remove_region(head, orig_start, rback_end, page_shift);
          return MT_EAGAIN;
        }
        cur->pg_start = pg_start;
        rback_end = pg_start-1;
        continue;
      }
    }
  }
}



/*
 *  hash_find()
 *         find the pointer to the list holding the object
 *         havig pgrp
 */
static hash_item_t *hash_find(struct mm_struct *mm)
{
  u_int32_t idx;
  struct list_head *pos, *n;
  hash_item_t *cur;

  idx = (unsigned long)mm % HASH_SIZE;
  list_for_each_safe(pos, n, &list_arr[idx]) {
    cur = list_entry(pos, hash_item_t, list);
    if (cur->mm == mm) {
      return cur;
    }
  }
  return NULL;
}


/*
 *  hash_insert()
 *         if the entry is not in the hash insert it and
 *         return a pointer to the new item. If the entry
 *         exists, remove it (it is saved in mlock_ctx)
 *         If failed return MT_EAGAIN
 *         
 */
static call_result_t hash_insert(struct mm_struct *mm, hash_item_t **hitem_pp)
{
  u_int32_t idx, i;
  struct list_head *pos, *n;
  hash_item_t *cur= NULL;

  FUNC_IN;
  idx = (unsigned long)mm % HASH_SIZE;
  list_for_each_safe(pos, n, &list_arr[idx]) {
    cur = list_entry(pos, hash_item_t, list);
    if ( cur->mm == mm )  break;/* entry is already in the hash (from "previous life" of mm) */
  }
  /* remove from hash old entry (keep item to be freed in MOSAL_cleanu) */
  if (pos != &list_arr[idx]) list_del(&cur->list); 
  
  /* add a new item */
  cur = TMALLOC(hash_item_t);
  if ( !cur ) {
    MTL_ERROR1(MT_FLFMT("%s: allocation failed"), __func__);
    MT_RETURN(MT_EAGAIN);
  }
  cur->mm = mm;
  for ( i=0; i<MAX_PAGE_LISTS; ++i ) {
    INIT_LIST_HEAD(&cur->mlist[i].head);
    MOSAL_mutex_init(&cur->mlist[i].mtx);
  }
  cur->iobufs_list = NULL;
  MOSAL_spinlock_init(&cur->iobufs_list_spl);
  cur->ref_cnt = 1;
  list_add(&cur->list, &list_arr[idx]); /* link the item to the head */
  *hitem_pp = cur;
  MT_RETURN(MT_OK);
}



/*
 *  MOSAL_mlock_init
 */
call_result_t MOSAL_mlock_init(void)
{
  u_int32_t i;

  MTL_DEBUG7(MT_FLFMT("==> MOSAL_mlock_init"));
  if ( find_m_lock_unlock_ptrs(&mlock_ptr, &munlock_ptr) != 0 ) {
    MTL_ERROR1(MT_FLFMT("could not find the pointers for sys_mlock and sys_munlock"));
    MTL_DEBUG7(MT_FLFMT("<== MOSAL_mlock_init"));
    return MT_ENORSC;
  }

  mregion_cache = kmem_cache_create("MOSAL::mregion_t", sizeof(mregion_t), 0, SLAB_HWCACHE_ALIGN, NULL, NULL);
  if ( !mregion_cache ) {
    MTL_ERROR1(MT_FLFMT("failed to allocate cache"));
    MTL_DEBUG7(MT_FLFMT("<== MOSAL_mlock_init"));
    return MT_EAGAIN;
  }

  for ( i=0; i<HASH_SIZE; ++i ) {
    INIT_LIST_HEAD(&list_arr[i]);
  }

  MOSAL_mutex_init(&hash_mtx);
  MOSAL_spinlock_init(&rel_spl);

  MTL_DEBUG7(MT_FLFMT("<== MOSAL_mlock_init"));
  return MT_OK;
}


/*
 *  MOSAL_mlock_cleanup
 */
void MOSAL_mlock_cleanup(void)
{
  /* If this function is invoked, it means no "device file" is open for MOSAL,
   * thus all MOSAL_mlock contexts were cleaned
   */
  if ( kmem_cache_destroy(mregion_cache) != 0 ) {
    MTL_ERROR1(MT_FLFMT("kmem_cache_destroy failed - probably a memory leak"));
  }
}


/*
 *  MOSAL_mlock
 */
call_result_t MOSAL_mlock(MT_virt_addr_t addr, MT_size_t size)
{
  call_result_t rc;
  unsigned int page_shift;

  rc = MOSAL_get_page_shift(MOSAL_get_current_prot_ctx(), addr, &page_shift);
  if ( rc != MT_OK ) {
    return rc;
  }
  return priv_mosal_mlock(addr, size, page_shift, NULL);
}


/*
 *  MOSAL_munlock
 */
call_result_t MOSAL_munlock(MT_virt_addr_t addr, MT_size_t size)
{
  hash_item_t *hitem;
  call_result_t rc;
  unsigned int page_shift;

  FUNC_IN;

  if ( !current->mm ) {
    /* mm context does not exist for this process - soon MOSAL_mlock_ctx_cleanup
     * will be called and all allocations will be freed */
    return MT_OK;
  }

  rc = MOSAL_get_page_shift(MOSAL_get_current_prot_ctx(), addr, &page_shift);
  if ( rc != MT_OK ) {
    return rc;
  }

  MOSAL_mutex_acq_ui(&hash_mtx);
  hitem = hash_find(current->mm);
  MOSAL_mutex_rel(&hash_mtx);
  if ( !hitem ) {
    MTL_ERROR1(MT_FLFMT("%s: could not find mm=0x%p (pid=%d) in the hash"), __func__, 
               current->mm,current->pid);
    return MT_ENORSC;
  }

  
  rc = priv_mosal_munlock(addr, size, page_shift, hitem);
  MT_RETURN(rc);
}


/*
 *  free_regions
 */
static call_result_t free_regions(hash_item_t *hitem)
{
  u_int32_t i;
  struct list_head *pos, *next;
  mregion_t *cur;
  call_result_t rc=MT_OK;

  for ( i=0; i<MAX_PAGE_LISTS; ++i ) {
    MOSAL_mutex_acq_ui(&hitem->mlist[i].mtx);
    list_for_each_safe(pos, next, &hitem->mlist[i].head) {
      cur = list_entry(pos, mregion_t, list);
      list_del(pos);
      kmem_cache_free(mregion_cache, cur);
    }
    MOSAL_mutex_rel(&hitem->mlist[i].mtx);
  }
  return rc;
}

call_result_t MOSAL_mlock_ctx_init(MOSAL_mlock_ctx_t *mlock_ctx_p)
{
  call_result_t rc;

#ifdef PARANOID
  if ( !current->mm ) {
    MTL_ERROR4(MT_FLFMT("%s: current->mm=NULL"), __func__);
    return MT_ERROR;
  }
#endif
  MTL_DEBUG4(MT_FLFMT("MOSAL_mlock_ctx_init (pid=%d,mm=0x%p)"),current->pid,current->mm);
  MOSAL_mutex_acq_ui(&hash_mtx);
  rc = hash_insert(current->mm, mlock_ctx_p);
  MOSAL_mutex_rel(&hash_mtx);
  if ( rc!=MT_OK ) {
    MTL_ERROR4(MT_FLFMT("MOSAL_init_mlock_ctx failed for pid=%d (%s)"),
               current->pid,mtl_strerror_sym(rc) );
  }
  MTL_DEBUG4(MT_FLFMT("MOSAL_mlock_ctx_init allocated mlock_ctx=0x%p"),*mlock_ctx_p);
  return rc;
}

call_result_t MOSAL_mlock_ctx_cleanup(MOSAL_mlock_ctx_t mlock_ctx)
{
  hash_item_t *hitem;
  call_result_t rc;
  MOSAL_iobuf_t iobuf;
  MT_bool free_hitem;

  MTL_DEBUG4(MT_FLFMT("MOSAL_mlock_ctx_cleanup (mlock_ctx=0x%p,pid=%d,mm=0x%p)"),
             mlock_ctx,current->pid,current->mm);
  if (current->mm != NULL) {
    MTL_ERROR2(MT_FLFMT("MOSAL_mlock_ctx_cleanup: mm still exists (pid=%d) - continue cleanup"),
               current->pid);
  }
  MOSAL_mutex_acq_ui(&hash_mtx);
  hitem = hash_find(mlock_ctx->mm);
  if (hitem == mlock_ctx) {  /* given context is still in the hash table */
    list_del(&hitem->list);  /* Remove from hash table bucket */
  }
  MOSAL_mutex_rel(&hash_mtx);

  rc = free_regions(mlock_ctx);
  if (rc != MT_OK) {
    MTL_ERROR2(MT_FLFMT("MOSAL_mlock_ctx_cleanup: free_regions failed (%s)"),mtl_strerror_sym(rc) );
    /* Let's continue to free anyway */
  }

  /* invalidate the mem_ctx in all the iobufs */
  MOSAL_spinlock_lock(&rel_spl);
  MOSAL_spinlock_lock(&mlock_ctx->iobufs_list_spl);
  iobuf = mlock_ctx->iobufs_list;
  mlock_ctx->iobufs_list = CLOSE_IOBUFS_LIST; /* avoid linking anymore iobufs */
  while ( iobuf ) {
    iobuf->mlock_ctx = NULL;
    iobuf = iobuf->next;
  }
  MOSAL_spinlock_unlock(&mlock_ctx->iobufs_list_spl);
  mlock_ctx->ref_cnt--;
  free_hitem = (mlock_ctx->ref_cnt == 0);
  MOSAL_spinlock_unlock(&rel_spl);

  if ( free_hitem ) {
    FREE(mlock_ctx);
  }
  
  return rc;
}



/*
 *  MOSAL_mlock_iobuf
 */
call_result_t MOSAL_mlock_iobuf(MT_virt_addr_t addr, MT_size_t size, MOSAL_iobuf_t mosal_iobuf,
                                unsigned int page_shift)
{
  call_result_t rc;
  hash_item_t *hitem_p;

  rc = priv_mosal_mlock(addr, size, page_shift, &hitem_p);
  if ( rc != MT_OK ) {
    MTL_ERROR4(MT_FLFMT("%s: Failed (%s) priv_mosal_mlock for addr="VIRT_ADDR_FMT" size="SIZE_T_FMT),
               __func__,mtl_strerror_sym(rc),addr,size);
    return rc;
  }

  /* link the iobuf */
  MOSAL_spinlock_lock(&rel_spl);
  MOSAL_spinlock_lock(&hitem_p->iobufs_list_spl);
  mosal_iobuf->mlock_ctx = hitem_p;
  if ( hitem_p->iobufs_list != CLOSE_IOBUFS_LIST ) {
    if ( !hitem_p->iobufs_list ) {
      hitem_p->iobufs_list = mosal_iobuf;
    }
    else {
      hitem_p->iobufs_list->prev = mosal_iobuf;
      mosal_iobuf->next = hitem_p->iobufs_list;
      hitem_p->iobufs_list = mosal_iobuf;
    }
  }
  MOSAL_spinlock_unlock(&hitem_p->iobufs_list_spl);
  hitem_p->ref_cnt--;
  MOSAL_spinlock_unlock(&rel_spl);

  return MT_OK;
}


/*
 *  MOSAL_munlock_iobuf
 */
call_result_t MOSAL_munlock_iobuf(MT_virt_addr_t addr, MT_size_t size, MOSAL_iobuf_t mosal_iobuf,
                                  unsigned int page_shift)
{
  call_result_t rc=MT_OK;
  hash_item_t *hitem;

  MOSAL_spinlock_lock(&rel_spl);
  hitem = mosal_iobuf->mlock_ctx;
  if ( hitem && (hitem->ref_cnt>0) ) {
    if ( (hitem->mm!=current->mm) && current->mm ) {
      MTL_ERROR1(MT_FLFMT("%s: hitem->mm=%p, current->mm=%p"), __func__, hitem->mm, current->mm);
      MOSAL_spinlock_unlock(&rel_spl);
      return MT_ERROR;
    }
    hitem->ref_cnt++;
    MOSAL_spinlock_unlock(&rel_spl);
    rc = priv_mosal_munlock(mosal_iobuf->va, mosal_iobuf->size, page_shift, hitem);
    if ( rc!=MT_OK) {
      MTL_ERROR1(MT_FLFMT("%s: %s"), __func__, mtl_strerror(rc));
    }
    /* remove from the list */
    MOSAL_spinlock_lock(&hitem->iobufs_list_spl);
    if ( hitem->iobufs_list==mosal_iobuf ) hitem->iobufs_list = mosal_iobuf->next;
    if ( mosal_iobuf->prev ) mosal_iobuf->prev->next = mosal_iobuf->next;
    if ( mosal_iobuf->next ) mosal_iobuf->next->prev = mosal_iobuf->prev;
    MOSAL_spinlock_unlock(&hitem->iobufs_list_spl);
    hash_item_release(hitem);
  }
  else {
    MOSAL_spinlock_unlock(&rel_spl);
  }

  return rc;
}



/*
 *  priv_mosal_mlock
 */
static call_result_t priv_mosal_mlock(MT_virt_addr_t addr, MT_size_t size, unsigned int page_shift,
                                      hash_item_t **hitem_pp)
{
  hash_item_t *hitem;
  int page_type;
  unsigned long pg_start, pg_end;
  call_result_t rc;
  
  FUNC_IN;

  if (( size == 0 ) || (addr+size-1 < addr)) { 
    /* make sure size is not too small (0) and not too big (wrap around) */
    MTL_ERROR4(MT_FLFMT("%s: Invalid size (addr="VIRT_ADDR_FMT" size="SIZE_T_FMT")"),__func__,
               addr,size);
    return MT_EINVAL;
  }


  if ( !current->mm ) { /* process has already freed whole its VM */
    return MT_ENORSC;
  }

  MOSAL_mutex_acq_ui(&hash_mtx);
  hitem = hash_find(current->mm);
  MOSAL_mutex_rel(&hash_mtx);
  if ( !hitem ) {
    MTL_ERROR1(MT_FLFMT("%s: could not find mm=0x%p (pid=%d) in the hash"), __func__, 
               current->mm,current->pid);
    return MT_ENORSC;
  }
  page_type = page_shift==MOSAL_SYS_PAGE_SHIFT ? SMALL_PAGE_IDX : BIG_PAGE_IDX;
  pg_start = (unsigned long)(addr >> page_shift);
  pg_end = (unsigned long)((addr+size-1) >> page_shift);
  MOSAL_mutex_acq_ui(&hitem->mlist[page_type].mtx);
  MTL_TRACE8(MT_FLFMT("calling insert_region for va=0x"VIRT_ADDR_FMT", size="SIZE_T_FMT), addr, size);
  rc = insert_region(&hitem->mlist[page_type].head, pg_start, pg_end, page_shift);
  if ( rc == MT_OK ) {
    rc = cond_lock_region(addr, size);
    if ( rc != MT_OK ) {
      remove_region(&hitem->mlist[page_type].head, pg_start, pg_end, page_shift);
    }
    else {
      rc = mark_vma_for_fork(addr, size, 1);
      if ( rc != MT_OK ) {
        MTL_ERROR1(MT_FLFMT("could not find all the VMAs that were just locked: addr="VIRT_ADDR_FMT", size="SIZE_T_FMT), addr, size);
      }
    }
  }
  MOSAL_mutex_rel(&hitem->mlist[page_type].mtx);
  if ( hitem_pp ) {
    if ( rc == MT_OK ) {
      MOSAL_spinlock_lock(&rel_spl);
      hitem->ref_cnt++;
      MOSAL_spinlock_unlock(&rel_spl);
    }
    *hitem_pp = hitem;
  }
  FUNC_OUT;
  return rc;
}



static call_result_t priv_mosal_munlock(MT_virt_addr_t addr, MT_size_t size, unsigned int page_shift,
                                        hash_item_t *hitem)
{
  int page_type;
  unsigned long pg_start, pg_end;
  call_result_t rc;

  FUNC_IN;
  if (( size == 0 ) || (addr+size-1 < addr)) { 
    /* make sure size is not too small (0) and not too big (wrap around) */
    MTL_ERROR4(MT_FLFMT("%s: Invalid size (addr="VIRT_ADDR_FMT" size="SIZE_T_FMT")"),__func__,
               addr,size);
    return MT_EINVAL;
  }

  if ( !current->mm ) {
    /* mm context does not exist for this process - soon MOSAL_mlock_ctx_cleanup
     * will be called and all allocations will be freed */
    MT_RETURN(MT_OK);
  }

  page_type = page_shift==PAGE_SHIFT ? SMALL_PAGE_IDX : BIG_PAGE_IDX;
  pg_start = (unsigned long)(addr >> page_shift);
  pg_end = (unsigned long)((addr+size-1) >> page_shift);
  MOSAL_mutex_acq_ui(&hitem->mlist[page_type].mtx);
  rc = remove_region(&hitem->mlist[page_type].head, pg_start, pg_end, page_shift);
  MOSAL_mutex_rel(&hitem->mlist[page_type].mtx);
  MT_RETURN(rc);
}



/*
 *  hash_item_release
 */
static void hash_item_release(hash_item_t *hitem_p)
{
  MT_bool free_hitem;

  MOSAL_spinlock_lock(&rel_spl);
  hitem_p->ref_cnt--;
  free_hitem= (hitem_p->ref_cnt == 0);
  MOSAL_spinlock_unlock(&rel_spl);
  if ( free_hitem ) {
    FREE(hitem_p);
  }

}
