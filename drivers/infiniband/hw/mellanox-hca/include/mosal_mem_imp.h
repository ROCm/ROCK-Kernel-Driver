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


#ifndef H_MOSAL_MEM_IMP_H
#define H_MOSAL_MEM_IMP_H

                     
#if defined(__KERNEL__)
#include <linux/highmem.h>
#endif
#include <asm/page.h>


#if defined(__ia64__) && !defined(__KERNEL__) && defined(MT_SUSE)
#include <limits.h>

/*
 * mosal_log2()
 */
static int inline mosal_log2(u_int64_t arg)
{
  int i;
  u_int64_t  tmp;

  if ( arg == 0 ) {
    return INT_MIN; /* log2(0) = -infinity */
  }

  tmp = 1;
  i = 0;
  while ( tmp < arg ) {
    tmp = tmp << 1;
    ++i;
  }

  return i;
}

  #define MOSAL_SYS_PAGE_SHIFT ({                              \
                                    int page_shift;            \
                                    switch (PAGE_SIZE) {       \
                                    case 4096:                 \
                                        page_shift=12;         \
                                        break;                 \
                                    case 8192:                 \
                                        page_shift=13;         \
                                        break;                 \
                                    case 16384:                \
                                        page_shift=14;         \
                                        break;                 \
                                    case 65536:                \
                                        page_shift=16;         \
                                        break;                 \
                                    default:                   \
                                        page_shift=mosal_log2(PAGE_SIZE);         \
                                    }                          \
                                    page_shift;                \
                               })
  #define MOSAL_SYS_PAGE_SIZE PAGE_SIZE
#else
  #define MOSAL_SYS_PAGE_SHIFT PAGE_SHIFT
  #define MOSAL_SYS_PAGE_SIZE (1 << MOSAL_SYS_PAGE_SHIFT)
#endif


#ifdef __SSE__ 
#undef __SSE__
#endif

#if !defined(__KERNEL__) && defined(__MMX__) && defined(__i386__)
/* Cannot use MMX in kernel (we can but the overhead is too much to optimize a single 64b access)*/
static __inline__ void MOSAL_mmx_write_qword(u_int64_t data, volatile u_int64_t *target_p)
{
  __asm__ volatile ("movq (%0), %%mm0 \n"
                    "movq %%mm0, (%1) \n"
                    "emms               "
                    : : "r" (&data) , "r" (target_p)  : "memory");
}

static __inline__ u_int64_t MOSAL_mmx_read_qword(volatile u_int64_t *source_p)
{
  u_int64_t x;
  __asm__ volatile ("movq (%1), %%mm0 \n"
                    "movq  %%mm0, (%0)\n"
                    "emms               "
                    : : "r" (&x) , "r" (source_p) : "memory");
  return x;
}
#endif /* __MMX__ && !__KERNEL__ && __i386__ */




#if defined(__SSE__) && !defined(__KERNEL__) && defined(__i386__)
/* Cannot use XMM in kernel (we can but the overhead is too much to optimize a single 64b access)*/
static __inline__ void MOSAL_sse_write_qword(u_int64_t data, volatile u_int64_t *target_p)
{
  __asm__ volatile ("movq (%0), %%xmm0 \n"
                    "movq %%xmm0, (%1) \n"
                    : : "r" (&data) , "r" (target_p)  : "memory");
}

static __inline__ u_int64_t MOSAL_sse_read_qword(volatile u_int64_t *source_p)
{
  u_int64_t x;
  __asm__ volatile ("movq (%1), %%xmm0 \n"
                    "movq  %%xmm0, (%0)\n"
                    : : "r" (&x) , "r" (source_p) : "memory");
  return x;
}
#endif /* __SSE__ && !__KERNEL__ && __i386__*/



/************    access to memory-mapped physical memory **********************/	

#define MOSAL_MMAP_IO_READ_BYTE(reg)				(*(volatile u_int8_t *)(reg))
#define MOSAL_MMAP_IO_READ_WORD(reg)				(*(volatile u_int16_t *)(reg))
#define MOSAL_MMAP_IO_READ_DWORD(reg)				(*(volatile u_int32_t *)(reg))

/* Cannot use XMM in kernel (we can but the overhead is too much to optimize a single 64b access)*/
#if defined(__SSE__) && defined(__i386__) && !defined(__KERNEL__)
#define MOSAL_MMAP_IO_READ_QWORD(reg)  MOSAL_sse_read_qword((volatile u_int64_t*)(reg))

/* Cannot use MMX in kernel (we can but the overhead is too much to optimize a single 64b access)*/
#elif defined(__MMX__)  && defined(__i386__) && !defined(__KERNEL__)
#define MOSAL_MMAP_IO_READ_QWORD(reg)  MOSAL_mmx_read_qword((volatile u_int64_t*)(reg))

#else
#define MOSAL_MMAP_IO_READ_QWORD(reg)       (*(volatile u_int64_t *)(reg))      
#endif /*  MOSAL_MMAP_IO_READ_QWORD  */

#define MOSAL_MMAP_IO_READ_BUF_BYTE(reg,buf,num_of_bytes)  do {                              \
  MT_size_t MOSAL_MMAP_IO_i;                                                                 \
  for (MOSAL_MMAP_IO_i= 0; MOSAL_MMAP_IO_i < (num_of_bytes); MOSAL_MMAP_IO_i++)              \
    ((u_int8_t*)(buf))[MOSAL_MMAP_IO_i]=                                                     \
      MOSAL_MMAP_IO_READ_BYTE(((MT_virt_addr_t)(reg))+MOSAL_MMAP_IO_i);                         \
} while (0)
#define MOSAL_MMAP_IO_READ_BUF_WORD(reg,buf,num_of_words)	 do {                              \
  MT_size_t MOSAL_MMAP_IO_i;                                                                 \
  for (MOSAL_MMAP_IO_i= 0; MOSAL_MMAP_IO_i < (num_of_words); MOSAL_MMAP_IO_i++)              \
    ((u_int16_t*)(buf))[MOSAL_MMAP_IO_i]=                                                    \
      MOSAL_MMAP_IO_READ_WORD(((MT_virt_addr_t)(reg)) + (MOSAL_MMAP_IO_i<<1));                  \
} while (0)
#define MOSAL_MMAP_IO_READ_BUF_DWORD(reg,buf,num_of_dwords) do {                             \
  MT_size_t MOSAL_MMAP_IO_i;                                                                 \
  for (MOSAL_MMAP_IO_i= 0; MOSAL_MMAP_IO_i < (num_of_dwords); MOSAL_MMAP_IO_i++)             \
      ((u_int32_t*)(buf))[MOSAL_MMAP_IO_i]=                                                  \
        MOSAL_MMAP_IO_READ_DWORD(((MT_virt_addr_t)(reg)) + (MOSAL_MMAP_IO_i<<2));               \
} while (0)

#define MOSAL_MMAP_IO_WRITE_BYTE(reg,data)			(*(volatile u_int8_t*)(reg))  = (data)
#define MOSAL_MMAP_IO_WRITE_WORD(reg,data)			(*(volatile u_int16_t*)(reg)) = (data)
#define MOSAL_MMAP_IO_WRITE_DWORD(reg,data)			(*(volatile u_int32_t*)(reg)) = (data)


/* Cannot use XMM in kernel (we can but the overhead is too much to optimize a single 64b access)*/
#if defined(__SSE__) && defined(__i386__) && !defined(__KERNEL__)
#define MOSAL_MMAP_IO_WRITE_QWORD(reg,data)  MOSAL_sse_write_qword((data),(volatile u_int64_t*)(reg))
#define __MOSAL_MMAP_IO_WRITE_QWORD_ATOMIC__  /* Qword write is assured to be atomic */

/* Cannot use MMX in kernel (we can but the overhead is too much to optimize a single 64b access)*/
#elif defined(__MMX__) && defined(__i386__) && !defined(__KERNEL__)
#define MOSAL_MMAP_IO_WRITE_QWORD(reg,data)  MOSAL_mmx_write_qword((data),(volatile u_int64_t*)(reg))
#define __MOSAL_MMAP_IO_WRITE_QWORD_ATOMIC__  /* Qword write is assured to be atomic */

#else
#define MOSAL_MMAP_IO_WRITE_QWORD(reg,data)       (*(volatile u_int64_t *)(reg)) = (data)      
#if defined(__ia64__) 
#define __MOSAL_MMAP_IO_WRITE_QWORD_ATOMIC__  /* Qword write is assured to be atomic */
#endif
#endif /*  MOSAL_MMAP_IO_WRITE_QWORD  */


#define MOSAL_MMAP_IO_WRITE_BUF_BYTE(reg,buf,num_of_bytes)	do {                             \
  MT_size_t MOSAL_MMAP_IO_i;                                                                 \
  for (MOSAL_MMAP_IO_i= 0; MOSAL_MMAP_IO_i < (num_of_bytes); MOSAL_MMAP_IO_i++)              \
    MOSAL_MMAP_IO_WRITE_BYTE(((MT_virt_addr_t)(reg)) + MOSAL_MMAP_IO_i,                         \
                             ((u_int8_t*)(buf))[MOSAL_MMAP_IO_i]);                           \
} while (0)
#define MOSAL_MMAP_IO_WRITE_BUF_WORD(reg,buf,num_of_words)	do {                             \
  MT_size_t MOSAL_MMAP_IO_i;                                                                 \
  for (MOSAL_MMAP_IO_i= 0; MOSAL_MMAP_IO_i < (num_of_words); MOSAL_MMAP_IO_i++)              \
    MOSAL_MMAP_IO_WRITE_WORD(((MT_virt_addr_t)(reg)) + (MOSAL_MMAP_IO_i<<1),                    \
                             ((u_int16_t*)(buf))[MOSAL_MMAP_IO_i]);                          \
} while (0) 
#define MOSAL_MMAP_IO_WRITE_BUF_DWORD(reg,buf,num_of_dwords)	do {                           \
  MT_size_t MOSAL_MMAP_IO_i;                                                                 \
  for (MOSAL_MMAP_IO_i= 0; MOSAL_MMAP_IO_i < (num_of_dwords); MOSAL_MMAP_IO_i++)             \
    MOSAL_MMAP_IO_WRITE_DWORD(((MT_virt_addr_t)(reg)) + (MOSAL_MMAP_IO_i<<2),                   \
      ((u_int32_t*)(buf))[MOSAL_MMAP_IO_i]);                                                 \
} while (0)



/******************************************************************************
 *  MOSAL_pci_virt_alloc_consistent
 *
 *  Description:
 *    allocate virtually contigous consistent memory (coherent)
 *
 *  Parameters: 
 *    size(IN) the required allocation size in bytes
 *    alignment (IN) the required alignment in bytes
 *
 *  Returns: virtual address of allocated area
 *           0 if failed
 *
 ******************************************************************************/
/* void *MOSAL_pci_virt_alloc_consistent(MT_size_t size, u_int8_t alignment); */
#ifdef CONFIG_NOT_COHERENT_CACHE
  /* none coherent cache */
  #ifdef __KERNEL__
    #define MOSAL_pci_virt_alloc_consistent(size, alignment)         \
          ({                                                         \
             dma_addr_t dma_handle;                                  \
             void *ret;                                              \
             ret = pci_alloc_consistent(NULL, (size), &dma_handle);  \
             ret;                                                    \
          })
  #else
    /* no support in user level */
  #endif
#else
  #define MOSAL_pci_virt_alloc_consistent(size, alignment) VMALLOC((size))
#endif      


/******************************************************************************
 *  MOSAL_pci_virt_free_consistent
 *
 *  Description:
 *    de-allocate virtually contigous consistent memory (coherent)
 *
 *  Parameters: 
 *    vaddr(IN) address of freed allocation
 *    size(IN) size of area to be freed in bytes
 *
 *  Returns:
 *
 ******************************************************************************/
/*void MOSAL_pci_virt_free_consistent(void *vaddr, MT_size_t size);*/
#ifdef CONFIG_NOT_COHERENT_CACHE
  /* none coherent cache */
  #ifdef __KERNEL__
    #define MOSAL_pci_virt_free_consistent(vaddr, size)  pci_free_consistent(NULL, (size), vaddr, (dma_addr_t)0)
  #else
    /* no support in user level */
  #endif
#else  
  /* system with coherent cache */
  #define MOSAL_pci_virt_free_consistent(vaddr, size) VFREE((vaddr))
#endif      


/******************************************************************************
 *  MOSAL_pci_phys_alloc_consistent
 *
 *  Description:
 *    allocate physically contigous consistent memory (coherent)
 *
 *  Parameters: 
 *    size(IN) the required allocation size in bytes
 *    alignment(IN) the required alignment in bytes
 *
 *  Returns: virtual address of allocated area
 *           0 if failed
 *
 ******************************************************************************/
/* void *MOSAL_pci_phys_alloc_consistent(MT_size_t size, u_int8_t alignment ); */
#ifdef __KERNEL__
  /* kernel space */
  #ifdef CONFIG_NOT_COHERENT_CACHE
    /* none coherent cache */
    #define MOSAL_pci_phys_alloc_consistent(size,alignment)        \
        ({                                                         \
           dma_addr_t dma_handle;                                  \
           void *ret;                                              \
           ret = pci_alloc_consistent(NULL, (size), &dma_handle);  \
           ret;                                                    \
        })
  #else
    /* system with coherent cache */
    #ifndef MTL_TRACK_ALLOC
      #define MOSAL_pci_phys_alloc_consistent(size,alignment) INTR_MALLOC(size)
    #else
      #define MOSAL_pci_phys_alloc_consistent(size,alignment)      \
        ({                                                         \
           void *ret;                                              \
           ret = INTR_MALLOC(size);                                     \
           if ( ret ) {                                            \
             memtrack_alloc(MEMTRACK_PHYS_CONST, (unsigned long)ret, size, __FILE__, __LINE__);  \
           }                                                       \
           ret;                                                    \
        })
    #endif        
  #endif  
#else
  /* no support in user level */
#endif


/******************************************************************************
 *  MOSAL_pci_phys_free_consistent
 *
 *  Description:
 *    de-allocate physically contigous consistent memory (coherent)
 *
 *  Parameters: 
 *    vaddr(IN) address of freed allocation
 *    size(IN) size of area to be freed in bytes
 *
 *  Returns:
 *
 ******************************************************************************/
/*void MOSAL_pci_phys_free_consistent(void *vaddr, MT_size_t size);*/
#ifdef __KERNEL__
  /* kernel space */
  #ifdef CONFIG_NOT_COHERENT_CACHE
    /* none coherent cache */
    #define MOSAL_pci_phys_free_consistent(vaddr, size)  pci_free_consistent(NULL, (size), vaddr, (dma_addr_t)0)
  #else
    /* system with coheren cache */
    #ifndef MTL_TRACK_ALLOC
      #define MOSAL_pci_phys_free_consistent(vaddr, size)  FREE(vaddr)
    #else
      #define  MOSAL_pci_phys_free_consistent(vaddr, size) do {                                                                            \
                                                             FREE(vaddr);                                                                  \
                                                             memtrack_free(MEMTRACK_PHYS_CONST, (unsigned long)vaddr, __FILE__, __LINE__); \
                                                           }                                                                               \
                                                           while(0)
    #endif  
  #endif  
#else
  /* no support in user level */
#endif


#if defined(__KERNEL__)
    #  if defined(__i386__) && (LINUX_VERSION_CODE == KERNEL_VERSION(2,4,9)) && defined(CONFIG_HIGHMEM64G_HIGHPTE)
    /* Work around RH AS 2.1 configuration bug */
    #    define mosal_page_to_phys(page, iomem) ({  \
                                                   MT_phys_addr_t pa; \
                                                   if ( !iomem ) { \
                                                     pa = ((u64)(page - mem_map) << PAGE_SHIFT); \
                                                   } \
                                                   else { \
                                                     pa = ((MT_phys_addr_t)(unsigned long)page) << PAGE_SHIFT; \
                                                   } \
                                                   pa; \
                                                })
    #  else
    /* normal page_to_phys() is fine */
    #    define mosal_page_to_phys(page, iomem) ({  \
                                                   MT_phys_addr_t pa; \
                                                   if ( !iomem ) { \
                                                     pa = page_to_phys(page); \
                                                   } \
                                                   else { \
                                                     pa = ((MT_phys_addr_t)(unsigned long)page) << PAGE_SHIFT; \
                                                   } \
                                                   pa; \
                                                })
    #  endif
#endif



#if defined(__KERNEL__)
/*
 *  vmalloced_addr
 */
static inline int vmalloced_addr(MT_virt_addr_t va)
{
  if ( (va>=VMALLOC_START) && (va<VMALLOC_END) ) {
    return 1;
  }
  return 0;
}
#endif






#endif /* H_MOSAL_MEM_IMP_H */

