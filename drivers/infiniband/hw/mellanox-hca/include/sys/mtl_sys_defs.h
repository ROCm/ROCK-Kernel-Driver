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

#ifndef H_MTL_SYS_DEFS_H
#define H_MTL_SYS_DEFS_H

/* common for kernel and user */
#include <mtl_log.h>
#include <mtl_types.h>


#if defined(IN) || defined(OUT)
  #error MACROS IN and OUT are in use, do not override 
#endif
#define IN
#define OUT

/* an absent constant in Linux */
#define _UI64_MAX	0xFFFFFFFFFFFFFFFFULL
#define _UI32_MAX	0xFFFFFFFFUL

/* long long constants */
#define MAKE_LONGLONG(a)	(a##LL)
#define MAKE_ULONGLONG(a)	(a##ULL)

/* allocation on stack */
#define ALLOCA(a)			alloca(a)
#define FREEA(a)	    

/* 
 *
 * Endian conversions
 *
 */
 
#define mt_generic_16_swap(x) \
({ \
	u_int16_t __x = (x); \
	((u_int16_t)( \
		(((u_int16_t)(__x) & (u_int16_t)0x00ffU) << 8) | \
		(((u_int16_t)(__x) & (u_int16_t)0xff00U) >> 8) )); \
})

#define mt_generic_32_swap(x) \
({ \
	u_int32_t __x = (x); \
	((u_int32_t)( \
		(((u_int32_t)(__x) & (u_int32_t)0x000000ffUL) << 24) | \
		(((u_int32_t)(__x) & (u_int32_t)0x0000ff00UL) <<  8) | \
		(((u_int32_t)(__x) & (u_int32_t)0x00ff0000UL) >>  8) | \
		(((u_int32_t)(__x) & (u_int32_t)0xff000000UL) >> 24) )); \
})

#define mt_generic_64_swap(x) \
({ \
	u_int64_t __x = (x); \
	((u_int64_t)( \
		(u_int64_t)(((u_int64_t)(__x) & (u_int64_t)MAKE_ULONGLONG(0x00000000000000ff) << 56) | \
		(u_int64_t)(((u_int64_t)(__x) & (u_int64_t)MAKE_ULONGLONG(0x000000000000ff00) << 40) | \
		(u_int64_t)(((u_int64_t)(__x) & (u_int64_t)MAKE_ULONGLONG(0x0000000000ff0000) << 24) | \
		(u_int64_t)(((u_int64_t)(__x) & (u_int64_t)MAKE_ULONGLONG(0x00000000ff000000) <<  8) | \
	    (u_int64_t)(((u_int64_t)(__x) & (u_int64_t)MAKE_ULONGLONG(0x000000ff00000000) >>  8) | \
		(u_int64_t)(((u_int64_t)(__x) & (u_int64_t)MAKE_ULONGLONG(0x0000ff0000000000) >> 24) | \
		(u_int64_t)(((u_int64_t)(__x) & (u_int64_t)MAKE_ULONGLONG(0x00ff000000000000) >> 40) | \
		(u_int64_t)(((u_int64_t)(__x) & (u_int64_t)MAKE_ULONGLONG(0xff00000000000000) >> 56) )); \
})

#ifdef  MT_BIG_ENDIAN 

    #define mt_be16_swap(x)         (x)
    #define mt_be32_swap(x)         (x)
    #define mt_be64_swap(x)         (x)

    #define mt_swap16le(x)		    mt_generic_16_swap(x)
    #define mt_swap32le(x)			mt_generic_32_swap(x)
    #define mt_swap64le(x)		    mt_generic_64_swap(x)
    
#elif defined(MT_LITTLE_ENDIAN)  &&  ( defined(__i386__) )

    #define mt_swap16le(x)		    (x)
    #define mt_swap32le(x)			(x)
    #define mt_swap64le(x)		    (x)
	
    #define mt_be16_swap(x)		((((x) >> 8)&0xff) | (((x) << 8)&0xff00))

static __inline__ u_int32_t mt_be32_swap(u_int32_t x)
{
                    __asm__("bswap %0" : "=r" (x) : "0" (x));
                                return x;
}

static __inline__ u_int64_t mt_be64_swap(u_int64_t x)
{
  u_int32_t low, high;


  low = x>>32;
  high = x&0xffffffff;

  low = mt_be32_swap(low);
  high = mt_be32_swap(high);

  return ((u_int64_t)high)<<32 | low;
}

#elif defined(MT_LITTLE_ENDIAN)  &&  defined(__ia64__)

    #define mt_swap16le(x)		    (x)
    #define mt_swap32le(x)			(x)
    #define mt_swap64le(x)		    (x)
	
    #define mt_be16_swap(x)		((((x) >> 8)&0xff) | (((x) << 8)&0xff00))

static __inline__ u_int64_t mt_be64_swap(u_int64_t x)
{
	u_int64_t result;

        __asm__ ("mux1 %0=%1,@rev" : "=r" (result) : "r" (x));

	return result;
}

static __inline__ u_int32_t mt_be32_swap(u_int32_t x)
{
	return (mt_be64_swap(x) >> 32);
}

#elif defined(MT_LITTLE_ENDIAN)  && defined(__x86_64__)

    #define mt_swap16le(x)		    (x)
    #define mt_swap32le(x)			(x)
    #define mt_swap64le(x)		    (x)
	
#define mt_be16_swap(x) \
({ \
	u_int16_t __x = (x); \
	((u_int16_t)( \
		(((u_int16_t)(__x) & (u_int16_t)0x00ffU) << 8) | \
		(((u_int16_t)(__x) & (u_int16_t)0xff00U) >> 8) )); \
})

#define mt_be32_swap(x) \
({ \
	u_int32_t __x = (x); \
	((u_int32_t)( \
		(((u_int32_t)(__x) & (u_int32_t)0x000000ffUL) << 24) | \
		(((u_int32_t)(__x) & (u_int32_t)0x0000ff00UL) <<  8) | \
		(((u_int32_t)(__x) & (u_int32_t)0x00ff0000UL) >>  8) | \
		(((u_int32_t)(__x) & (u_int32_t)0xff000000UL) >> 24) )); \
})

#define mt_be64_swap(x) \
({ \
	u_int64_t __x = (x); \
	((u_int64_t)( \
		(u_int64_t)(((u_int64_t)(__x) & (u_int64_t)0x00000000000000ffULL) << 56) | \
		(u_int64_t)(((u_int64_t)(__x) & (u_int64_t)0x000000000000ff00ULL) << 40) | \
		(u_int64_t)(((u_int64_t)(__x) & (u_int64_t)0x0000000000ff0000ULL) << 24) | \
		(u_int64_t)(((u_int64_t)(__x) & (u_int64_t)0x00000000ff000000ULL) <<  8) | \
	    (u_int64_t)(((u_int64_t)(__x) & (u_int64_t)0x000000ff00000000ULL) >>  8) | \
		(u_int64_t)(((u_int64_t)(__x) & (u_int64_t)0x0000ff0000000000ULL) >> 24) | \
		(u_int64_t)(((u_int64_t)(__x) & (u_int64_t)0x00ff000000000000ULL) >> 40) | \
		(u_int64_t)(((u_int64_t)(__x) & (u_int64_t)0xff00000000000000ULL) >> 56) )); \
})

#endif


/**************************************************************************************************
 * Function (kernel only): MOSAL_cpu_to_be16
 *
 * Description: convert from CPU16 bit value  into 16 bit big endian value
 *
 * Parameters: cpu32: cpu value
 * 
 * Returns: big endian value
 * 
 *************************************************************************************************/
#define MOSAL_cpu_to_be16(x)			mt_be16_swap(x)

/**************************************************************************************************
 * Function (kernel only): MOSAL_be16_to_cpu
 *
 * Description: convert from 16bit big endian value into CPU16 bit value
 *
 * Parameters: be16: big endian value
 * 
 * Returns: cpu32 value
 * 
 *************************************************************************************************/
#define MOSAL_be16_to_cpu(x)		    mt_be16_swap(x)


/**************************************************************************************************
 * Function (kernel only): MOSAL_cpu_to_be32
 *
 * Description: convert from CPU32 bit value  into 32bit big endian value
 *
 * Parameters: cpu32: cpu value
 * 
 * Returns: big endian value
 * 
 *************************************************************************************************/
#define MOSAL_cpu_to_be32(x)			mt_be32_swap(x)



/**************************************************************************************************
 * Function (kernel only): MOSAL_be32_to_cpu
 *
 * Description: convert from 32bit big endian value into CPU32 bit value
 *
 * Parameters: be32: big endian value
 * 
 * Returns: cpu32 value
 * 
 *************************************************************************************************/
#define MOSAL_be32_to_cpu(x)		    mt_be32_swap(x)


/**************************************************************************************************
 * Function (kernel only): MOSAL_cpu_to_be64
 *
 * Description: convert from CPU 64 bit value  into 64 bit big endian value
 *
 * Parameters: cpu64: cpu value
 * 
 * Returns: big endian value
 * 
 *************************************************************************************************/
#define MOSAL_cpu_to_be64(x)			mt_be64_swap(x)



/**************************************************************************************************
 * Function (kernel only): MOSAL_be64_to_cpu
 *
 * Description: convert from 64bit big endian value into CPU64 bit value
 *
 * Parameters: be64: big endian value
 * 
 * Returns: cpu64 value
 * 
 *************************************************************************************************/
#define MOSAL_be64_to_cpu(x)		    mt_be64_swap(x)

#define MOSAL_cpu_to_le64(x)			mt_swap64le(x)
#define MOSAL_le64_to_cpu(x)		    mt_swap64le(x)
#define MOSAL_cpu_to_le32(x)			mt_swap32le(x)
#define MOSAL_le32_to_cpu(x)		    mt_swap32le(x)
#define MOSAL_cpu_to_le16(x)			mt_swap16le(x)
#define MOSAL_le16_to_cpu(x)		    mt_swap16le(x)

/* Definitions for malloc/free porting to kernel */
#if defined(__KERNEL__) && !defined(VXWORKS_OS) 

	#define __NO_VERSION__
	#include <linux/version.h>					 
	#include <linux/kernel.h>
	#include <linux/module.h>
	#include <linux/string.h>
	#include <linux/mm.h>
	#include <linux/slab.h>
	#include <linux/autoconf.h>
	#include <linux/types.h>
	#include <linux/ctype.h>
	#include <linux/sched.h>
	#include <linux/linkage.h>
	#include <linux/vmalloc.h>
	#include <linux/mm.h>
	#include <linux/pci.h>
	#include <linux/smp_lock.h>
  #include <linux/interrupt.h>
	#include <linux/fs.h>
	#include <linux/securebits.h>
	#include <asm/uaccess.h>
	#include <asm/io.h>
	#include <asm/current.h>
	#include <asm/errno.h>
	#include <asm/page.h>

	#define LINUX_KERNEL_2_2 ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)) && (LINUX_VERSION_CODE <= KERNEL_VERSION(2,2,255)))
	#define LINUX_KERNEL_2_4 ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)) && (LINUX_VERSION_CODE <= KERNEL_VERSION(2,4,255)))
  #define LINUX_KERNEL_2_6 ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)) && (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,255)))

#if !LINUX_KERNEL_2_6
   #include <linux/compatmac.h>
//   #include <sys/syscall.h>
#endif

# ifdef MTL_LOG_MALLOC
        extern void*  mtl_log_vmalloc(const char* fn, int ln, int bsize);
        extern void   mtl_log_vfree(const char* fn, int ln, void* ptr);
        extern void*  mtl_log_kmalloc(const char* fn, int ln, int bsize, 
                                      unsigned g);
        extern void   mtl_log_kfree(const char* fn, int ln, void* ptr);
	#define VMALLOC(bsize) mtl_log_vmalloc(__FILE__, __LINE__, bsize)
	#define VFREE(ptr)     mtl_log_vfree(__FILE__, __LINE__, ptr)
	#define MALLOC(bsize)  \
                   mtl_log_kmalloc(__FILE__, __LINE__, bsize, GFP_KERNEL)
	#define INTR_MALLOC(bsize) \
                   mtl_log_kmalloc(__FILE__, __LINE__, bsize, GFP_ATOMIC)
	#define FREE(ptr)      mtl_log_kfree(__FILE__, __LINE__, ptr)
# else
	#define VMALLOC(bsize)     QVMALLOC(bsize)
	#define VFREE(ptr)         QVFREE(ptr)
	#define MALLOC(bsize)      QMALLOC(bsize)
	#define INTR_MALLOC(bsize) QINTR_MALLOC(bsize)
	#define FREE(ptr)          QFREE(ptr)
# endif

#ifndef MTL_TRACK_ALLOC
#if LINUX_KERNEL_2_4
	#define QVMALLOC(bsize)     (in_interrupt() ? __vmalloc((bsize), GFP_ATOMIC | __GFP_HIGHMEM, PAGE_KERNEL) : vmalloc(bsize))
#else /* Unable to use VMALLOC in interrupt for kernel 2.2 */
	#define QVMALLOC(bsize)     vmalloc(bsize)
#endif
	#define QMALLOC(bsize)      (in_interrupt() ? kmalloc((bsize), GFP_ATOMIC) : kmalloc((bsize), GFP_KERNEL))
	#define QVFREE(ptr)         vfree(ptr)
	#define QINTR_MALLOC(bsize) kmalloc((bsize), GFP_ATOMIC)
	#define QCMALLOC(bsize,g) 	kmalloc((bsize), g)
	#define QFREE(ptr)          kfree(ptr)
#else /* MTL_TRACK_ALLOC */
	#include <memtrack.h>
	#define QVMALLOC(bsize)     TRACKQVMALLOC(bsize)
	#define QMALLOC(bsize)      TRACKQMALLOC(bsize)
	#define QVFREE(ptr)         TRACKQVFREE(ptr)
	#define QINTR_MALLOC(bsize) TRACKQINTR_MALLOC((bsize))
	#define QCMALLOC(bsize,g) 	TRACKQCMALLOC((bsize), g)
	#define QFREE(ptr)          TRACKQFREE(ptr)

	#define TRACKQVMALLOC(bsize) ({                                                                                                    \
                                  void *p;                                                                                           \
                                  p=(in_interrupt() ? __vmalloc((bsize), GFP_ATOMIC | __GFP_HIGHMEM, PAGE_KERNEL) : vmalloc(bsize)); \
                                  if ( p ) {                                                                                         \
                                    memtrack_alloc(MEMTRACK_VMALLOC, (unsigned long)p, bsize, __FILE__, __LINE__);                   \
                                  }                                                                                                  \
                                  p;                                                                                                 \
                               })
                               
	#define TRACKQMALLOC(bsize)  ({                                                                                   \
                                  void *p;                                                                          \
                                  p=(in_interrupt() ? kmalloc((bsize), GFP_ATOMIC) : kmalloc((bsize), GFP_KERNEL)); \
                                  if ( p ) {                                                                        \
                                    memtrack_alloc(MEMTRACK_KMALLOC, (unsigned long)p, bsize, __FILE__, __LINE__);  \
                                  }                                                                                 \
                                  p;                                                                                \
                               })
                               

	#define TRACKQCMALLOC(bsize,g)   ({                                                                                    \
                                  void *p;                                                                          \
                                  p=kmalloc((bsize), g);                                                            \
                                  if ( p ) {                                                                        \
                                    memtrack_alloc(MEMTRACK_KMALLOC, (unsigned long)p, bsize, __FILE__, __LINE__);  \
                                  }                                                                                 \
                                  p;                                                                                \
                               })
                               
	#define TRACKQVFREE(ptr)     do {                                                    \
                                 memtrack_free(MEMTRACK_VMALLOC, (unsigned long)ptr, __FILE__, __LINE__);  \
                                 vfree(ptr);                                           \
                               }                                                       \
                               while(0)
                                  
	#define TRACKQINTR_MALLOC(bsize) ({                                                                                   \
                                      void *p;                                                                          \
                                      p = kmalloc((bsize), GFP_ATOMIC);                                                 \
                                      if ( p ) {                                                                        \
                                        memtrack_alloc(MEMTRACK_KMALLOC, (unsigned long)p, bsize, __FILE__, __LINE__);  \
                                      }                                                                                 \
                                      p;                                                                                \
                                   })
  
	#define TRACKQFREE(ptr)      do {                                                    \
                                 memtrack_free(MEMTRACK_KMALLOC, (unsigned long)ptr, __FILE__, __LINE__);  \
                                 kfree(ptr);                                           \
                               }                                                       \
                               while(0)

#endif
	

#else /* not __KERNEL__ */
	#include <stdlib.h>
	#include <string.h>
  #ifdef MTL_LOG_MALLOC
    #define MALLOC(x)  ( {void* mdebug_tmp; mdebug_tmp= malloc(x); MTL_DEBUG5(MT_FLFMT("MT_MALLOC_DEBUG( %d bytes @ %p )"),x,mdebug_tmp); mdebug_tmp;} )
    #define FREE(x) MTL_DEBUG5(MT_FLFMT("MT_FREE_DEBUG( %p )\n"),x) ; free(x)
    #define VMALLOC(bsize) MALLOC(bsize)
    #define VFREE(bsize)   FREE(bsize)
    #define INTR_MALLOC(bsize) MALLOC(bsize)
  #else	
	#define VMALLOC(bsize) malloc(bsize)
	#define VFREE(bsize) free(bsize)
	#define MALLOC(bsize) malloc(bsize)
	#define INTR_MALLOC(bsize) malloc(bsize)
	#define FREE(ptr)  free(ptr)
  #endif /* MTL_LOG_MALLOC */
# endif /* __KERNEL__ */

#define __INLINE__  inline

#endif /* H_MTL_SYS_DEFS_H */
