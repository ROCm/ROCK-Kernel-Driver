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
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/version.h>
#include <linux/autoconf.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/mman.h>
#if defined(__i386__) || defined(__ia64__)
#include <linux/rwsem.h>
#endif
#include <linux/pci.h>
#include <asm/pgtable.h>
#include <linux/highmem.h>



#include "mosal_priv.h"

#if !LINUX_KERNEL_2_6
#include <sys/syscall.h>
#include <linux/wrapper.h>
#endif


/****************  *********************/
#if defined(VM_ACCOUNT) && !LINUX_KERNEL_2_6
#define MOSAL_DO_MUNMAP(mm,virt,size) ({                                       \
                                         int rc = 0;                           \
                                         if (mm!=NULL) {                       \
                                           down_write(&mm->mmap_sem);          \
                                           rc = do_munmap(mm,virt,size,1);     \
                                           up_write(&mm->mmap_sem);            \
                                         }                                     \
                                         rc;                                   \
                                      })

#else 
#define MOSAL_DO_MUNMAP(mm,virt,size) ({                                       \
                                         int rc=0;                             \
                                         if (mm!=NULL) {                       \
                                           down_write(&mm->mmap_sem);          \
                                           rc = do_munmap(mm,virt,size);       \
                                           up_write(&mm->mmap_sem);            \
                                         }                                     \
                                         rc;                                   \
                                      })                                          
#endif


/*
 *  in_address_space - good for user space only
 */
static inline int in_address_space(MT_virt_addr_t va)
{
  struct vm_area_struct *vma;

	down_read(&current->mm->mmap_sem);
  vma = find_vma(current->mm, va);
  up_read(&current->mm->mmap_sem);
  if ( !vma || (va<vma->vm_start) ) {
    return 0;
  }
  return 1;
}



/*
 *  MOSAL_virt_to_phys
 */
call_result_t MOSAL_virt_to_phys(MOSAL_prot_ctx_t prot_ctx, const MT_virt_addr_t va, MT_phys_addr_t *pa_p)
{
  return MOSAL_virt_to_phys_ex(prot_ctx, va, NULL, pa_p, 0);
}


#if !defined(__ia64__) && !defined(pgprot_noncached)
/*
 * copied from linux/drivers/char/mem.c
 * This should probably be per-architecture in <asm/pgtable.h>
 */
static inline pgprot_t pgprot_noncached(pgprot_t _prot)
{
	unsigned long prot = pgprot_val(_prot);

#if defined(__i386__) || defined(__x86_64__)
	/* On PPro and successors, PCD alone doesn't always mean 
	    uncached because of interactions with the MTRRs. PCD | PWT
	    means definitely uncached. */ 
	if (boot_cpu_data.x86 > 3)
		prot |= _PAGE_PCD | _PAGE_PWT;
#elif defined(__powerpc__)
	prot |= _PAGE_NO_CACHE | _PAGE_GUARDED;
#elif defined(__mc68000__)
#ifdef SUN3_PAGE_NOCACHE
	if (MMU_IS_SUN3)
		prot |= SUN3_PAGE_NOCACHE;
	else
#endif
	if (MMU_IS_851 || MMU_IS_030)
		prot |= _PAGE_NOCACHE030;
	/* Use no-cache mode, serialized */
	else if (MMU_IS_040 || MMU_IS_060)
		prot = (prot & _CACHEMASK040) | _PAGE_NOCACHE_S;
#endif

	return __pgprot(prot);
}
#endif

/*
 *  MOSAL_map_phys_addr
 */
MT_virt_addr_t MOSAL_map_phys_addr(MT_phys_addr_t pa, MT_size_t bsize,
                                   MOSAL_mem_flags_t flags, MOSAL_prot_ctx_t prot_ctx)
{
  unsigned long virt_map; /* location where physical buffer was mapped */
  unsigned long prot_flags= 0; 
  unsigned long vm_flags= 0;
  pgprot_t pgprot_flags= __pgprot(0);
  MT_bool map_failed = FALSE;

  MTL_DEBUG4("MOSAL_map_phys_addr(pa=0x"PHYS_ADDR_FMT",bsize="SIZE_T_FMT",flags=%d,prot_ctx=%s)\n",
    pa,bsize,flags, prot_ctx == MOSAL_PROT_CTX_KERNEL ? "kernel" : "user");

  if (prot_ctx == MOSAL_PROT_CTX_KERNEL) {  /* Mapping to kernel virtual address */
    /* physical address above host mem */
    if (flags & MOSAL_MEM_FLAGS_NO_CACHE) 
      return MOSAL_io_remap(pa, bsize);
    else
      return (MT_virt_addr_t)ioremap(pa,bsize);
  }

  /* else: MOSAL_PROT_CTX_CURRENT_USER */
  if (current->mm == NULL)  return VA_NULL; /* No virtual memory context */

  /* Compute protection flags based on given flags parameter */
  if ((flags & MOSAL_MEM_FLAGS_PERM_READ))  {
    prot_flags= PROT_READ;
    vm_flags= MAP_PRIVATE;
    pgprot_flags= PAGE_READONLY;
  }
  if ((flags & MOSAL_MEM_FLAGS_PERM_WRITE)) {
    prot_flags|= PROT_WRITE;    
    vm_flags= MAP_SHARED;
    pgprot_flags= PAGE_SHARED;
    /* Cannot permit write without read due to limitations of some architectures..*/
  }
  if (flags & MOSAL_MEM_FLAGS_NO_CACHE)  pgprot_flags= pgprot_noncached(pgprot_flags);
  vm_flags |= MAP_ANONYMOUS;
  
  down_write(&current->mm->mmap_sem);
  virt_map= do_mmap_pgoff(NULL,0,bsize,prot_flags,vm_flags,0);
  up_write(&current->mm->mmap_sem);
  if ( IS_ERR((void *)virt_map) ) {
    MTL_ERROR1(MT_FLFMT("do_mmap_pgoff(NULL, 0x"PHYS_ADDR_FMT", "SIZE_T_FMT", 0x%lx, 0x%lx, 0) returned error: %ld"), pa, bsize, prot_flags, vm_flags, virt_map);
    if ( virt_map == -ENFILE ) {
      MTL_ERROR1(MT_FLFMT("You have too many open files !!!"));
    }
    return VA_NULL;
  }
  MTL_DEBUG2("MOSAL_map_phys_addr: Mapped phys.=0x"PHYS_ADDR_FMT" to virt.=0x%p\n",
    pa,(void*)virt_map);

  {
    /* this is io memory */
    /* mark the vma as VM_IO and VM_NO_UNLOCK */
    struct vm_area_struct *vma;

    down_write(&current->mm->mmap_sem);
    vma = find_vma(current->mm, virt_map);
    if ( vma &&
         (virt_map==vma->vm_start) &&
         ((vma->vm_end-vma->vm_start)==bsize)
       ) {
      vma->vm_flags |= (VM_IO
#ifdef VM_NO_UNLOCK
                        | VM_NO_UNLOCK
#endif
                        );
    }
    else {
      map_failed = TRUE;
    }
    up_write(&current->mm->mmap_sem);
  }

  if ( map_failed ) {
    MOSAL_DO_MUNMAP(current->mm, virt_map, bsize);
    return MT_EINVAL;
  }

  down_write(&current->mm->mmap_sem);
  if (remap_page_range(REMAP_PREFIX virt_map,pa,bsize,pgprot_flags)) {
    up_write(&current->mm->mmap_sem);
    MTL_ERROR2("MOSAL_map_phys_addr: Failed remap_page_range.\n");
    MOSAL_DO_MUNMAP(current->mm,virt_map,bsize);
    return VA_NULL;
  }
  up_write(&current->mm->mmap_sem);
  return (MT_virt_addr_t)virt_map;
}

/*
 *  MOSAL_unmap_phys_addr
 */
call_result_t MOSAL_unmap_phys_addr(MOSAL_prot_ctx_t prot_ctx, MT_virt_addr_t virt, 
                                    MT_size_t bsize)
{
  MTL_DEBUG4("MOSAL_unmap_phys_addr(prot_ctx=%s,virt=0x"VIRT_ADDR_FMT",bsize="SIZE_T_FMT")\n",
    MOSAL_PROT_CTX_KERNEL ? "kernel" : "user",virt,bsize);
  if (prot_ctx == MOSAL_PROT_CTX_KERNEL) {  /* Mapping to kernel virtual address */
    MOSAL_io_unmap(virt);
  } else {
    /* else: MOSAL_PROT_CTX_CURRENT_USER (if mm was already freed, it is unmapped already) */
    if ((current->mm !=NULL) && (MOSAL_DO_MUNMAP(current->mm,(unsigned long)virt,bsize))) {
      MTL_ERROR2("MOSAL_unmap_phys_addr: Failed unmapping address 0x"VIRT_ADDR_FMT".\n",
        virt);
      return MT_EINVAL;
    }
  }

  return MT_OK;
}


/*
 * Allocate physically contiguous memory
 * 
 *
 */
MT_virt_addr_t  MOSAL_phys_ctg_get(MT_size_t size)
{
	return VA_NULL;
}

call_result_t  MOSAL_phys_ctg_free(MT_virt_addr_t va, MT_size_t size)
{
	return MT_ENORSC;
}


/*
 *  mosal_get_page_table_lock
 */
static inline spinlock_t *mosal_get_page_table_lock(const MOSAL_prot_ctx_t prot_ctx)
{
  if ( prot_ctx == MOSAL_get_current_prot_ctx() ) {
    return &current->mm->page_table_lock;
  }
  else if ( prot_ctx == MOSAL_get_kernel_prot_ctx() ) {
    return &init_mm.page_table_lock;
  }
  else {
    return NULL;
  }
}


/*
 *  mosal_get_pgd
 */
static inline pgd_t *mosal_get_pgd(const MOSAL_prot_ctx_t prot_ctx, const MT_virt_addr_t va)
{
  pgd_t *pgd;

  if ( prot_ctx == MOSAL_get_current_prot_ctx() ) {
    pgd = pgd_offset(current->mm, va);
  }
  else if ( prot_ctx == MOSAL_get_kernel_prot_ctx() ) {
    pgd = pgd_offset_k(va);
  }
  else {
    MTL_ERROR1(MT_FLFMT("%s: invalid prot_ctx arg(%d)"), __func__, prot_ctx);
    return NULL;
  }

	if ( pgd_none(*pgd) || pgd_bad(*pgd) ) {
    return NULL;
  }

  return pgd;
}


/*
 *  mosal_get_pmd
 */
static inline pmd_t *mosal_get_pmd(MOSAL_prot_ctx_t prot_ctx, const pgd_t *pgd_p, const MT_virt_addr_t va)
{
  pmd_t *pmd_p;

  pmd_p = pmd_offset((pgd_t *)pgd_p, va);

  if ( pmd_none(*pmd_p)  ) {
    return NULL;
  }

  return pmd_p;
}


/*
 *  mosal_is_big_page
 */
static inline int mosal_is_big_page(const pmd_t *pmd)
{
#ifdef __i386__
  #if !LINUX_KERNEL_2_6
    if ( pmd_val(*pmd) & _PAGE_PSE ) return 1;
    else return 0;
  #else
    if ( pmd_large(*pmd) ) return 1;
    else return 0;
  #endif
#else
  return 0;
#endif
}


/*
 *  get_bp_page_p
 */
static inline struct page *get_bp_page_p(const pmd_t *pmd_p, const MT_virt_addr_t va)
{
#ifdef __i386__
  struct page *page_p;
  unsigned long mask;

  mask = (1UL<<PMD_SHIFT) - 1;
  page_p = (struct page *)pmd_page(*pmd_p) + (va & mask) / PAGE_SIZE;
#ifdef VALID_PAGE
  if( !VALID_PAGE(page_p) ) {
    return NULL;
  }
#endif
  return page_p;
#else
  return NULL;
#endif
}


/*
 *  mosal_get_pte
 */
static inline pte_t mosal_get_pte(pmd_t *pmd_p, const MT_virt_addr_t va, spinlock_t *lock, MT_u_int_t wr_enable)
{
  pte_t *pte_p, pte;
#if (LINUX_VERSION_CODE == KERNEL_VERSION(2,4,9)) || defined(RH_AS_3_0)
  pte_t *pte_b;
  struct page *pg;
#endif

#if (LINUX_VERSION_CODE == KERNEL_VERSION(2,4,9)) || defined(RH_AS_3_0)
  pg = pmd_page(*pmd_p);
  MTL_TRACE1(MT_FLFMT("pg=%p"), pg);
  /* advanced server 2.1 */
  spin_unlock(lock); /* free the lock since kmap may block */
  pte_b = kmap(pg);
  spin_lock(lock);
  if ( pte_b ) {
    pte_p = pte_b + __pte_offset(va);
    pte = *pte_p;
    if ( pte_present(pte) && wr_enable ) {
      pte = pte_mkwrite(pte);
      set_pte(pte_p, pte);
    }
    kunmap(pg);
  }
  else {
    pte_t zpte={0};
    return zpte; /* 0 is none present pte */
  }
#elif defined(pte_offset_map)
	pte_p = pte_offset_map(pmd_p, va);
	if ( !pte_p ) {
    pte_t zpte={0};
    return zpte; /* 0 is none present pte */
  }
	pte = *pte_p;
	pte_unmap(pte_p);
#else

  pte_p = pte_offset(pmd_p, va);
  pte = *pte_p;
  if ( pte_present(pte) && wr_enable ) {
    pte = pte_mkwrite(pte);
    set_pte(pte_p, pte);
  }

#ifdef MT_SUSE_PRO_80
  pte_kunmap(pte_p);
#endif /* MT_SUSE_PRO_80*/
#endif
  
  return pte;
}


#if defined(__ia64__) && ( defined(MT_SUSE) || defined(RH_AS_3_0) )
#define VTP_CAST(x) ((void *)x)
#else
#define VTP_CAST(x) x
#endif



/*
 *  MOSAL_virt_to_phys_ex
 */
call_result_t MOSAL_virt_to_phys_ex(const MOSAL_prot_ctx_t prot_ctx, const MT_virt_addr_t va,
                                    struct page **page_pp, MT_phys_addr_t *pa_p, MT_u_int_t wr_enable)
{
	spinlock_t *page_table_lock_p;
  pgd_t *pgd_p;
  pmd_t *pmd_p;
	pte_t pte;
  call_result_t rc;
  int big_page, vtp_avail=0;
  struct page *page_p=0;
  unsigned long offset;

  MTL_TRACE1(MT_FLFMT("%s: prot_ctx=%s, va="VIRT_ADDR_FMT), __func__, MOSAL_prot_ctx_str(prot_ctx), va);

  if ( prot_ctx==MOSAL_get_kernel_prot_ctx() ) {
    if ( !vmalloced_addr(va) ) {
      /* kmalloced address */
      page_p = virt_to_page(VTP_CAST(va));
      if ( !page_p ) {
        rc = MT_ENOMEM;
        goto err_lock;
      }
    }
    else {
#ifdef vmalloc_to_page
      vtp_avail = 1;
      page_p = vmalloc_to_page((void *)va);
      if ( !page_p ) {
        rc = MT_ENOMEM;
        goto err_lock;
      }
#endif
    }
  }

  if ( (prot_ctx==MOSAL_get_current_prot_ctx()) ||
       ((prot_ctx==MOSAL_get_kernel_prot_ctx()) && vmalloced_addr(va) && (vtp_avail==0)) )  {
    page_table_lock_p = mosal_get_page_table_lock(prot_ctx);
    if ( !page_table_lock_p ) {
      MTL_ERROR1(MT_FLFMT("%s: invalid prot_ctx arg(%d)"), __func__, prot_ctx);
      rc = MT_EINVAL;
      goto err_lock;
    }

    spin_lock(page_table_lock_p);
    pgd_p = mosal_get_pgd(prot_ctx, va);
    if ( !pgd_p ) {
      MTL_ERROR1(MT_FLFMT("%s: cannot retireve pgd_p: prot_ctx=%s, va="VIRT_ADDR_FMT), __func__, MOSAL_prot_ctx_str(prot_ctx), va);
      rc = MT_ENOMEM;
      goto exit_unlock;
    }

    pmd_p = mosal_get_pmd(prot_ctx, pgd_p, va);
    if ( !pmd_p ) {
      MTL_ERROR1(MT_FLFMT("%s: cannot retireve pmd_p: prot_ctx=%s, va="VIRT_ADDR_FMT), __func__, MOSAL_prot_ctx_str(prot_ctx), va);
      rc = MT_ENOMEM;
      goto exit_unlock;
    }

    big_page = mosal_is_big_page(pmd_p);
    if ( big_page ) {
      MTL_DEBUG1(MT_FLFMT("%s: big page found: prot_ctx=%s, va="VIRT_ADDR_FMT), __func__, MOSAL_prot_ctx_str(prot_ctx), va);
      page_p = get_bp_page_p(pmd_p, va);
      if ( !page_p ) {
        MTL_ERROR1(MT_FLFMT("%s: cannot retireve page_p: prot_ctx=%s, va="VIRT_ADDR_FMT), __func__, MOSAL_prot_ctx_str(prot_ctx), va);
        rc = MT_ENOMEM;
        goto exit_unlock;
      }
    }
    else {
      pte = mosal_get_pte(pmd_p, va, page_table_lock_p, wr_enable);
      if ( !pte_present(pte) ) {
        MTL_ERROR1(MT_FLFMT("%s: cannot retireve pte: prot_ctx=%s, va="VIRT_ADDR_FMT), __func__, MOSAL_prot_ctx_str(prot_ctx), va);
        rc = MT_ENOMEM;
        goto exit_unlock;
      }
      page_p = pte_page(pte);
    }
    spin_unlock(page_table_lock_p);
  }

  offset = va & (PAGE_SIZE-1);
  *pa_p = mosal_page_to_phys(page_p) | offset;
  if ( page_pp ) *page_pp=page_p;
  MTL_TRACE1(MT_FLFMT("%s: prot_ctx=%s, va="VIRT_ADDR_FMT", pa="PHYS_ADDR_FMT", %s"), __func__, MOSAL_prot_ctx_str(prot_ctx), va, *pa_p, vmalloced_addr(va) ? "VMALLOCED" : "KMALLOCED");
  return MT_OK;

exit_unlock:
  spin_unlock(page_table_lock_p);
err_lock:
  return rc;
}

/*
 *  MOSAL_get_page_shift
 */
call_result_t MOSAL_get_page_shift(MOSAL_prot_ctx_t prot_ctx, MT_virt_addr_t va, unsigned int *page_shift_p)
{
#ifdef __ia64__
  if ( prot_ctx == MOSAL_get_kernel_prot_ctx() ) {
    *page_shift_p = MOSAL_SYS_PAGE_SHIFT;
  }
  else {
    if ( !in_address_space(va) ) {
      return MT_ENOMEM;
    }
    else {
      *page_shift_p = MOSAL_SYS_PAGE_SHIFT;
    }
  }
  return MT_OK;
#else
  struct vm_area_struct *vma;

  if ( prot_ctx == MOSAL_get_current_prot_ctx() ) {
    /* user space address */
    down_read(&current->mm->mmap_sem);
    vma = find_vma(current->mm, va);
    if ( !vma || (va<vma->vm_start) ) {
      up_read(&current->mm->mmap_sem);
      return MT_ENOMEM;
    }
#ifdef VM_BIGPAGE
    if ( vma->vm_flags&VM_BIGPAGE ) {
      *page_shift_p = PMD_SHIFT;
    }
    else {
      *page_shift_p = MOSAL_SYS_PAGE_SHIFT;
    }
#else
  #ifdef VM_HUGETLB
    if ( vma->vm_flags&VM_HUGETLB ) {
      *page_shift_p = PMD_SHIFT;
    }
    else {
      *page_shift_p = MOSAL_SYS_PAGE_SHIFT;
    }
  #else
    *page_shift_p = MOSAL_SYS_PAGE_SHIFT;
  #endif
#endif
    up_read(&current->mm->mmap_sem);
    return MT_OK;
  }
  else {
    /* kernel space addresses */
    spinlock_t *page_table_lock_p;
    call_result_t rc = MT_ENOMEM;
    pgd_t *pgd_p;
    pmd_t *pmd_p;

    page_table_lock_p = mosal_get_page_table_lock(prot_ctx);
    if ( !page_table_lock_p ) {
      MTL_ERROR1(MT_FLFMT("%s: invalid prot_ctx arg(%d)"), __func__, prot_ctx);
      rc = MT_EINVAL;
      goto err_lock;
    }
    spin_lock(page_table_lock_p);
    /* we have to follow the kernel page tables */
    pgd_p = mosal_get_pgd(prot_ctx, va);
    if ( !pgd_p ) {
      MTL_ERROR1(MT_FLFMT("%s: cannot retireve pgd_p: prot_ctx=%s, va="VIRT_ADDR_FMT), __func__, MOSAL_prot_ctx_str(prot_ctx), va);
      goto exit_unlock;
    }

    pmd_p = mosal_get_pmd(prot_ctx, pgd_p, va);
    if ( !pmd_p ) {
      MTL_ERROR1(MT_FLFMT("%s: cannot retireve pmd_p: prot_ctx=%s, va="VIRT_ADDR_FMT), __func__, MOSAL_prot_ctx_str(prot_ctx), va);
      goto exit_unlock;
    }

    if ( mosal_is_big_page(pmd_p) ) {
      *page_shift_p = PMD_SHIFT;
    }
    else {
      *page_shift_p = MOSAL_SYS_PAGE_SHIFT;
    }
    rc = MT_OK;

exit_unlock:
    spin_unlock(page_table_lock_p);
err_lock:
    return rc;
  }
#endif
}
