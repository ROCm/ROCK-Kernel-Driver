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

#ifndef H_MOSAL_MEM_H
#define H_MOSAL_MEM_H
 
#include <mtl_types.h>
#include <mtl_errno.h>
#include <mtl_common.h>
#include <mosal_arch.h>
#include "mosal_mlock.h"


/*============ macro definitions =============================================*/
#define VA_NULL ((MT_virt_addr_t)0)
#define PA_NULL ((MT_phys_addr_t)0)
#define INVAL_PHYS_ADDR ((MT_phys_addr_t)_UI64_MAX)

typedef u_int32_t mem_attr_t;

#if !defined(__DARWIN__) || !defined(MT_KERNEL)
typedef char * MOSAL_shmem_key_t;

/*MOSAL shared memory flags */
#define MOSAL_SHM_CREATE    0x1      /*Create a new shared memory region */
#define MOSAL_SHM_EXCL      0x2      /*Ensure that the new region has been created*/
#define MOSAL_SHM_READONLY  0x4      /*Create read-only region*/
#define MOSAL_SHM_HUGEPAGE  0x8	     /*Create huge page */
/************************************/
#endif  /* !defined(__DARWIN__) || !defined(MT_KERNEL) */


#define MIN_PAGE_SZ						MOSAL_SYS_PAGE_SIZE
#define MIN_PAGE_SZ_MASK				(~((MT_virt_addr_t)MIN_PAGE_SZ - 1)) 			
#define MIN_PAGE_SZ_ALIGN(x)		    (((x) + ~MIN_PAGE_SZ_MASK) & MIN_PAGE_SZ_MASK) 	

#define MOSAL_PAGE_ALIGN(va, size)		(((va) + ~MOSAL_PAGE_MASK((va)+(size))) \
												& MOSAL_PAGE_MASK(va + size))
											
typedef enum {
  MOSAL_MEM_FLAGS_NO_CACHE  =1,/* non-chached mapping (to be used for address not in main memory) */ 
  MOSAL_MEM_FLAGS_PERM_READ = (1<<1), /* Read permission */
  MOSAL_MEM_FLAGS_PERM_WRITE= (1<<2)  /* Write permission */
  /* Note: currently write permission implies read permissions too */ 
} MOSAL_mem_flags_enum_t;

typedef u_int32_t MOSAL_mem_flags_t;  /* To be used with flags from MOSAL_mem_flags_enum_t */


static __INLINE__ const char *MOSAL_prot_ctx_str(const MOSAL_prot_ctx_t prot_ctx)
{
  if ( prot_ctx == MOSAL_get_kernel_prot_ctx() ) {
    return "KERNEL";
  }
  else if ( prot_ctx == MOSAL_get_current_prot_ctx() ) {
    return "USER";
  }
  else {
    return "INVALID";
  }
}



/******************************************************************************
 *
 *  Function(User Space only): MOSAL_io_remap
 *
 *  Description: Map a physical contigous buffer to virtual address.
 *
 *  Parameters: 
 *      pa   (IN) MT_phys_addr_t
 *           Physical address.
 *      size (IN) u_int32_t
 *           Size of memory buffer in bytes
 *               
 *
 *  Returns: On success returns pointer to new virtual memory buffer else
 *           returns zero.
 *
 *  Notes: The returned address will be page alligned. In case 'size' is not 
 *         page alligned the amount of allocated memory can be bigger than the 
 *         requested.
 *
 ******************************************************************************/
MT_virt_addr_t 
#ifndef MTL_TRACK_ALLOC
MOSAL_io_remap
#else
MOSAL_io_remap_memtrack
#endif
                        (MT_phys_addr_t pa, MT_size_t size); 

#ifdef MTL_TRACK_ALLOC
#define MOSAL_io_remap(pa, size)                                                                                  \
                             ({                                                                                   \
                                MT_virt_addr_t rc;                                                                \
                                rc = MOSAL_io_remap_memtrack((pa), (size));                                           \
                                if ( rc != VA_NULL ) {                                                            \
                                  memtrack_alloc(MEMTRACK_IOREMAP, (unsigned long)(rc), (size), __FILE__, __LINE__);\
                                }                                                                                 \
                                rc;                                                                               \
                             })
                             
#endif

void
#ifndef MTL_TRACK_ALLOC
MOSAL_io_unmap
#else
MOSAL_io_unmap_memtrack
#endif
                        (MT_virt_addr_t va);


#ifdef MTL_TRACK_ALLOC
#define MOSAL_io_unmap(va)  do {                                                                        \
                              memtrack_free(MEMTRACK_IOREMAP, (unsigned long)(va), __FILE__, __LINE__); \
                              MOSAL_io_unmap_memtrack(va);                                                       \
                            }                                                                           \
                            while (0)
#endif


/******************************************************************************
 *
 *  Function: MOSAL_map_phys_addr
 *
 *  Description: 
 *   Map physical address range to given process/memory context 
 *   MOSAL_io_remap is mapped to this with 
 *    prot_ctx==MOSAL_PROT_CTX_KERNEL and flags=MOSAL_MEM_FLAGS_NO_CACHE
 *
 *  Parameters: 
 *      pa   (IN) MT_phys_addr_t
 *           Physical address.
 *      bsize (IN) u_int32_t
 *           Size of memory buffer in bytes
 *      flags (IN) MOSAL_mem_flags_t
 *           Mapping attributes 
 *      prot_ctx (IN) MOSAL_prot_ctx_t
 *           Protection/memory context to map to (kernel or current user level)
 *
 *  Returns:
 *    On success returns pointer to new virtual address to which this 
 *    physical memory is mapped (in given prot_ctx).
 *    NULL if failed.
 *    Note: Mapping must be made for IO memory only and not for RAM.
 *
 ******************************************************************************/
MT_virt_addr_t MOSAL_map_phys_addr(MT_phys_addr_t pa, MT_size_t bsize,
                          MOSAL_mem_flags_t flags, MOSAL_prot_ctx_t prot_ctx);

/******************************************************************************
 *
 *  Function: MOSAL_unmap_phys_addr
 *
 *  Description: 
 *   Unmap physical address range previously mapped using MOSAL_map_phys_addr
 *
 *  Parameters: 
 *      prot_ctx (IN) MOSAL_prot_ctx_t
 *          Protection context of memory space of given virtual address.
 *      virt (IN) MT_virt_addr_t
 *          Mapping address as returned by MOSAL_map_phys_addr (page aligned).
 *      bsize (IN) u_int32_t
 *           Size of memory buffer in bytes
 *
 *  Returns: HH_OK, HH_EINVAL - invalid address
 *
 *
 ******************************************************************************/
call_result_t MOSAL_unmap_phys_addr(MOSAL_prot_ctx_t prot_ctx, MT_virt_addr_t virt, 
                                    MT_size_t bsize);


/******************************************************************************
 *
 *  Function: MOSAL_virt_to_phys
 *
 *  Description: Translate virtual address to physical.
 *
 *  Parameters: 
 *      prot_ctx(IN) source of the address (kernel or user)
 *      va (IN) const MT_virt_addr_t 
 *          Virtual address.
 *      pa_p(OUT) returned physical address
 *              
 *  Returns: MT_OK On success
 *           MT_ENOMEM when not in address space
 *           MT_ENORSC when physical page is not available
 *           MT_EINVAL invalid value of prot_ctx
 *
 ******************************************************************************/
call_result_t  MOSAL_virt_to_phys(MOSAL_prot_ctx_t prot_ctx,
                                  const MT_virt_addr_t va, MT_phys_addr_t *pa_p);

/******************************************************************************
 *
 *  Function (Kernel space only): MOSAL_virt_to_phys_ex
 *
 *  Description: Translate virtual address to physical.
 *
 *  Parameters: 
 *      prot_ctx(IN) source of the address (kernel or user)
 *      va (IN) Virtual address
 *      page_pp(OUT) pointer to return struct page * mapped to va
 *      pa_p(OUT) returned physical address
 *      wr_enable(IN) if set pte is made write enabled
 *              
 *  Returns: MT_OK On success
 *           MT_ENOMEM when not in address space
 *           MT_ENORSC when physical page is not available
 *           MT_EINVAL invalid value of prot_ctx
 *
 ******************************************************************************/
#if defined(__KERNEL__) && !defined(VXWORKS_OS) 
call_result_t MOSAL_virt_to_phys_ex(MOSAL_prot_ctx_t prot_ctx, const MT_virt_addr_t va,
                                    struct page **page_pp, MT_phys_addr_t *pa_p);
#endif

#if !defined(__DARWIN__)
/******************************************************************************
 *
 *  Function (Kernel space only): MOSAL_phys_to_virt
 *
 *  Description: Translate physical address to virtual.
 *
 *  Parameters: 
 *      pa (IN) const MT_phys_addr_t 
 *          Physical address.
 *              
 *  Returns: On success returns a virtual address in current address space, corresponding to pa. 
 *		Else returns zero.
 *
 ******************************************************************************/
MT_virt_addr_t MOSAL_phys_to_virt(const MT_phys_addr_t pa);


/******************************************************************************
 *
 *  Function (user_only): MOSAL_virt_to_bus
 *
 *  Description: Translate a virtual address to a bus address.
 *
 *  Parameters: 
 *      va (IN) MT_virt_addr_t
 *           Virtual address.
 *              
 *  Returns: On success returns bus address pointed by va. Else returns zero.
 *
 ******************************************************************************/
MT_phys_addr_t  MOSAL_virt_to_bus(MT_virt_addr_t va);


/******************************************************************************
 *
 *  Function (differ when kernel or user): MOSAL_phys_ctg_get
 *
 *  Description: allocate a physically contiguous pinned memory region. 
 *
 *  Parameters: 
 *      size (IN) 
 *           size of physically contiguous memory to be allocate.
 *     
 *  Returns: virtual address of memory or NULL if failed. 
 *
 ******************************************************************************/
MT_virt_addr_t  MOSAL_phys_ctg_get(MT_size_t size);

/******************************************************************************
 *
 *  Function (differ when kernel or user): MOSAL_phys_ctg_free
 *
 *  Description: free an allocate physically contiguous pinned memory region.
 *
 *  Parameters: 
 *      va (IN) 
 *          address of region to be freed.
 *     
 *  Returns: void
 *******************************************************************************/
call_result_t  MOSAL_phys_ctg_free(MT_virt_addr_t addr, MT_size_t size);

#endif /* __DARWIN__) */

/******************************************************************************
 *
 *  Function (only include): MOSAL_test_set_bit32
 *
 *  Description: atomically set bit and return its previous value. 
 *
 *  Parameters: 
 *      off (IN) bit offset 0-31 
 *       va (IN) void pointer to dword containing bit.
 *     
 *  Returns: previous bit value.
 *
 ******************************************************************************/
#define MOSAL_test_set_bit32(off, va)   MOSAL_arch_test_set_bit32((off), (va))


/******************************************************************************
 *
 *  Function (only include): MOSAL_test_clear_bit32
 *
 *  Description: atomically clear bit and return its previous value. 
 *
 *  Parameters: 
 *      off (IN) bit offset 0-32  
 *       va (IN) void pointer to dword containing bit.
 *     
 *  Returns: previous bit value.
 *
 ******************************************************************************/
#define MOSAL_test_clear_bit32(off, va)   MOSAL_arch_test_clear_bit32((off), (va))

#define PAGE_SIZE_4M  0x400000
#define PAGE_SIZE_2M  0x200000
#define PAGE_SIZE_4K  0x1000
#define PAGE_SIZE_8K  0x2000
#define PAGE_SIZE_16K 0x4000
#define PAGE_SIZE_64K 0x10000
#define PAGE_SHIFT_4M 22
#define PAGE_SHIFT_2M 21
#define PAGE_SHIFT_4K 12
#define PAGE_SHIFT_8K 13
#define PAGE_SHIFT_16K 14
#define PAGE_SHIFT_64K 16

/******************************************************************************
 *  Function: MOSAL_get_page_shift
 *
 *  Description: 
 *    get page shift for the va in the protection context specified by prot_ctx.
 *
 *  Parameters: 
 *    prot_ctx (IN) protection context
 *    va (IN) virtual address 
 *    page_shift_p (OUT) returned page shift
 *
 *  Returns: MT_OK
 *           MT_ENOMEM when address not valid
 *
 *  Note: If the address va does not belong to address space in the specified
 *        context the function fails.
 *
 ******************************************************************************/
call_result_t MOSAL_get_page_shift( MOSAL_prot_ctx_t prot_ctx, MT_virt_addr_t va,
                                   unsigned *page_shift_p);

/******************************************************************************
 *  Function: (inline function) MOSAL_get_page_size
 *
 *  Description:
 *    get page size for the va in the protection context specified by prot_ctx.
 *
 *  Parameters: 
 *    prot_ctx(IN) protection context
 *    va(IN) virtual address 
 *    page_size_p(OUT) returned page size
 *
 *  Returns: MT_OK
 *           MT_ENOMEM when address not valid
 *
 *  Note: If the address va does not belong to address space in the specified
 *        context the function fails.
 *
 ******************************************************************************/
static __INLINE__ call_result_t MOSAL_get_page_size(MOSAL_prot_ctx_t prot_ctx,
                                                    MT_virt_addr_t va, 
                                                    unsigned *page_size_p)
{
  unsigned int page_shift;
  call_result_t rc;

  rc = MOSAL_get_page_shift(prot_ctx, va, &page_shift);
  if ( rc != MT_OK ) {
    return rc;
  }
  *page_size_p = 1<<page_shift;
  MTL_TRACE1(MT_FLFMT("%s: va="VIRT_ADDR_FMT", prot=%s, page_size=%d"), __func__, va, MOSAL_prot_ctx_str(prot_ctx), *page_size_p);
  return rc;
}

#if !defined(__DARWIN__) || !defined(MT_KERNEL)
/******************************************************************************
 *  Function 
 *    MOSAL_shmget:
 *
 *  Description:
 *    Retrieve a shared memory region for the given key
 *  Parameters: 
 *    key(IN) MOSAL_shmem_key_t - A unique key for the memory region 
 *    size(IN) MT_size_t - size of needed memory region
 *    flags(IN) - permitions flags that may be:
 *      MOSAL_SHM_CREATE    - create 
 *      MOSAL_SHM_EXCL      - fall if the memory region with the given key 
 *      MOSAL_SHM_READONLY  - readonly region (by default region will be created 
 * with read-write permitions
 * already exists
 *    
 *    id_p(OUT) MOSAL_shmid_t - id of created region (in case of success)
 *
 *  Returns:
 *    MT_OK - in case of success
 *    MT_EACCES - if user has no permitions to access this memory region
 *    MT_EAGAIN - no resources
 *    MT_EBUSY  - if MOSAL_SHM_CREATE | MOSAL_SHM_EXCL was set in flags and the region with 
 * such a key already exists
 *    MT_EINVAL  - otherwise
 *
 ******************************************************************************/
call_result_t MOSAL_shmget(MOSAL_shmem_key_t key, 
                           MT_size_t size, 
                           u_int32_t flags, 
                           MOSAL_shmid_t * id_p);

 
/******************************************************************************
 *  Function 
 *    MOSAL_shmat:
 *
 *  Description:
 *    Attaches previously allocated shared memory region to virtual address 
 *  space of the calling process.
 *  Parameters: 
 *    id(IN) MOSAL_shmem_key_t - ID of shared memory region 
 *    flags(IN) - permitions flags that may be:
 *      MOSAL_SHM_READONLY  - create read-only memory region
 *      otherwise the region will be mapped with read-write permitions
 *    addr_p(OUT) - Start virtual address of the mapped shared region in case of success
 *
 *  Returns:
 *    MT_OK - incase of seccess
 *    MT_EACCES - If calling process has no permissions to access the region with
 *  the given ID
 *    MT_EINVAL - Bad id
 *    MT_EAGAIN - Couldn't allocate page tables for a new address range or for a descriptor
 *    MT_ERROR  - otherwise
 *
 ******************************************************************************/
call_result_t MOSAL_shmat(MOSAL_shmid_t id, int flags, void ** addr_p);


/******************************************************************************
 *  Function 
 *    MOSAL_shmdt:
 *
 *  Description:
 *    Detaches virtual address mapping of previously attached shared memory region .
 *  Parameters: 
 *    addr(IN) void * - Virtual address do detach
 *  Returns:
 *      MT_OK - success
 *      MT_EINVAL - Bad address
 ******************************************************************************/
call_result_t  MOSAL_shmdt(void * addr);

/******************************************************************************
 *  Function 
 *    MOSAL_shmrm:
 *
 *  Description:
 *    Schadules for removing the shared memory region with the given ID
 *  Parameters: 
 *    id(IN) MOSAL_shmid_t - ID of the memory region
 *  Returns:
 *    MT_OK in case of success
 *    MT_EACCES if called by the user which is not a creator of the region
 *    MT_EINVAL -otherwise
 ******************************************************************************/
call_result_t MOSAL_shmrm(MOSAL_shmid_t id);

#endif /* !defined(__DARWIN__) || !defined(MT_KERNEL) */

#endif /* H_MOSAL_MEM_H */
