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



#ifndef H_MTL_COMMON_H
#define H_MTL_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__mppc__) && ! defined(MT_BIG_ENDIAN)
#define MT_BIG_ENDIAN 1
#endif

#ifdef VXWORKS_OS

/*
 * General include files required by VxWorks applications.
 */ 
#include "vxWorks.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "stdarg.h"
#include "errno.h"
#include "taskLib.h"
#include "semLib.h"

/*
 * No print kernel under VxWorks.
 */
#define printk printf

/* 
 * MDDK assumes that random returns values upto 32k which is true only to rand. 
 */ 
#define random rand
#define srandom srand

/*
 * If CPU is already big endian, degenerate big endian macros.
 */
#ifdef MT_BIG_ENDIAN
#error MT_BIG_ENDIAN defined
#define __cpu_to_be32(x) (x)
#define __be32_to_cpu(x) (x)
#define __cpu_to_be64(x) (x)
#define __be64_to_cpu(x) (x)
#define __cpu_to_be16(x) (x)
#define __be16_to_cpu(x) (x)
#endif /* MT_BIG_ENDIAN */

/*
 * Limits definitions.
 */
#include "limits.h"

/*
 * Global semaphore which prevents more than one application to access CR space.
 */
extern SEM_ID appl_is_running_sem;

#endif /* VXWORKS_OS */

#include <mtl_types.h>
#include <bit_ops.h>

#define MAX_MTL_LOG_TYPES      3
#define MAX_MTL_LOG_SEVERITIES 8
#define MAX_MTL_LOG_LEN        512
#define MAX_MTL_LOG_LAYER        32
#define MAX_MTL_LOG_INFO        512
typedef enum {
    mtl_log_trace=0,
    mtl_log_debug=1,
    mtl_log_error=2
} mtl_log_types;

/*  Prepend File-name + Line-number to formatted message.
 *  Example:  MTL_DEBUG4(MT_FLFMT("(1+1/n)^n -> %g (%s)"), M_E, "Euler")
 */
#define MT_FLFMT(fmt)  "%s[%d]: " fmt "\n", mtl_basename(__FILE__), __LINE__

/* OS-dependent stuff */
#include <sys/mtl_sys_defs.h>

/* Convenient macros doing  cast & sizeof */
#define TNMALLOC(t, n)      (t*)MALLOC((n) * sizeof(t))
#define TMALLOC(t)          TNMALLOC(t, 1)
#define TNVMALLOC(t, n)     (t*)VMALLOC((n) * sizeof(t))
#define TVMALLOC(t)         TNVMALLOC(t, 1)
#define TNINTR_MALLOC(t, n) (t*)INTR_MALLOC((n) * sizeof(t))
#define TINTR_MALLOC(t)     TNINTR_MALLOC(t, 1)


#ifndef POWER2
#define POWER2(power) (1 << (power))
#else
#error use different name for power of 2
#endif

#ifdef USE_RELAY_MOD_NAME
extern void mtl_log_set_name( char * mod_name );
#endif


extern const char* mtl_strerror( call_result_t errnum);
extern const char* mtl_strerror_sym( call_result_t errnum);
extern const char* mtl_basename(const char* filename); /* trim dir-path */

/******************************************************************************
 *  Function: mtl_log_set
 *
 *  Description: Setup log print in kernel module.
 *
 *  Parameters:
 *    layer(IN)  (LEN s) char*
 *    info(IN)	 (LEN s) char*
 *
 *  Returns:
 ******************************************************************************/
void  mtl_log_set(char* layer, char *info);


extern void  mtl_log(const char* layer, mtl_log_types log_type, char sev,
                     const char *fmt, ...) __attribute__ ((format (printf, 4, 5)));

extern void mtl_common_cleanup(void);

/*  leo: defining and using constants cause compiler warning */
#if 0
static const u_int32_t	MTZERO32 = { 0 };
static const u_int32_t	MTONES32 = { 0xFFFFFFFF };
#endif


/**
 * MT_DOWN_XXX
 *
 * Clears lower 'mask' bit of 'value'.
 * 
 * e.g. MT_MASKX(0xFFFFF,8) -> 0xFFF00
 */
 
/**
 * MT_UP_XXX
 *
 * Upward aligns 'value' to is lower 'mask' bits.
 * 
 * e.g. MT_UP_ALIGNX(0x1002, 8) -> 0x1100
 */

/* for MT_virt_addr_t type of value */
#define MT_DOWN_ALIGNX_VIRT(value, mask)     ((MT_virt_addr_t)(value) & (~((MT_virt_addr_t)0) << (mask)))
#define MT_UP_ALIGNX_VIRT(value, mask)       MT_DOWN_ALIGNX_VIRT(((value) +  ~(~((MT_virt_addr_t)0) << (mask))), (mask))

/* for MT_phys_addr_t type of value */
#define MT_DOWN_ALIGNX_PHYS(value, mask)     ((MT_phys_addr_t)(value) & (~((MT_phys_addr_t)0) << (mask)))
#define MT_UP_ALIGNX_PHYS(value, mask)       MT_DOWN_ALIGNX_PHYS(((value) +  ~(~((MT_phys_addr_t)0) << (mask))), (mask))

/* for u_int32_t type of value */
#define MT_DOWN_ALIGNX_U32(value, mask)     ((u_int32_t)(value) & (~((u_int32_t)0) << (mask)))
#define MT_UP_ALIGNX_U32(value, mask)       MT_DOWN_ALIGNX_U32(((value) +  ~(~((u_int32_t)0) << (mask))), (mask))

/* for u_int64_t type of value */
#define MT_DOWN_ALIGNX_U64(value, mask)     ((u_int64_t)(value) & (~((u_int64_t)0) << (mask)))
#define MT_UP_ALIGNX_U64(value, mask)       MT_DOWN_ALIGNX_U64(((value) +  ~(~((u_int64_t)0) << (mask))), (mask))

/* for MT_ulong_ptr_t type of value */
#define MT_DOWN_ALIGNX_ULONG_PTR(value, mask)     ((MT_ulong_ptr_t)(value) & (~((MT_ulong_ptr_t)0) << (mask)))
#define MT_UP_ALIGNX_ULONG_PTR(value, mask)       MT_DOWN_ALIGNX_ULONG_PTR(((value) +  ~(~((MT_ulong_ptr_t)0) << (mask))), (mask))

/* for MT_size_t type of value */
#define MT_DOWN_ALIGNX_SIZE(value, mask)     ((MT_size_t)(value) & (~((MT_size_t)0) << (mask)))
#define MT_UP_ALIGNX_SIZE(value, mask)       MT_DOWN_ALIGNX_SIZE(((value) +  ~(~((MT_size_t)0) << (mask))), (mask))

/* for unsigned long type of value */
#define MT_DOWN_ALIGNX_ULONG(value, mask)     ((unsigned long)(value) & (~((unsigned long)0) << (mask)))
#define MT_UP_ALIGNX_ULONG(value, mask)       MT_DOWN_ALIGNX_ULONG(((value) +  ~(~((unsigned long)0) << (mask))), (mask))

/* for unsigned long type of value , */
/* PLEASE DON'T USE THIS MACRO. IT's KEPT JUST FOR BACKWARD COMPATABILITY REASONS */
#define MT_DOWN_ALIGNX(value, mask)     ((unsigned long)(value) & (~((unsigned long)0) << (mask)))
#define MT_UP_ALIGNX(value, mask)       MT_DOWN_ALIGNX(((value) +  ~(~((unsigned long)0) << (mask))), (mask))


enum {
    MT_MELLANOX_IEEE_VENDOR_ID = 0x02c9,
    MT_MELLANOX_PCI_VENDOR_ID  = 0x15B3,
    MT_TOPSPIN_PCI_VENDOR_ID   = 0x1867
};

/* some standard macros for tracing */
#define FUNC_IN MTL_DEBUG2("==> %s\n", __func__)
#define FUNC_OUT MTL_DEBUG2("<== %s\n", __func__)
#define MT_RETURN(rc) { FUNC_OUT; \
                        return (rc); }
                   
#define MT_RETV { FUNC_OUT ; \
                  return; }



#if defined(__LINUX__) && defined(MT_KERNEL) && defined(__i386__)
#define STACK_OK  (         \
  {                      \
  u_int32_t vsp=0, left, ret;                    \
  asm ("movl %%esp, %0;"                    \
      : "=r"(vsp)                           \
      : );                                  \
  left = vsp-((u_int32_t)current+sizeof(struct task_struct)); \
  if ( left < 0x400 ) { \
    MTL_ERROR1("you have less then 0x400 bytes of stack left\n");  \
	ret = 0;                    \
  }                 \
  else {    \
    MTL_DEBUG1("%s: stack depth left = %d bytes\n", __FUNCTION__, left);   \
	ret = 1; \
  }    \
  ret;  \
}   \
)

#define MT_RETURN_IF_LOW_STACK(stack_watermark) {\
  u_int32_t vsp=0, left;                    \
  asm ("movl %%esp, %0;"                    \
      : "=r"(vsp)                           \
      : );                                  \
  left = vsp-((u_int32_t)current+sizeof(struct task_struct)); \
  if ( left < stack_watermark) { \
    MTL_ERROR1(MT_FLFMT("%s: you have less then %u bytes of stack left (%uB left)\n"),__func__,\
                          stack_watermark,left);  \
        return -255;\
  }\
}

#else /* __LINUX__ && defined MT_KERNEL */
#define STACK_OK 1
#define MT_RETURN_IF_LOW_STACK(stack_watermark) do {} while (0)
#endif

/* an empty macro */
#define EMPTY

#ifdef __cplusplus
}
#endif

#endif  /* H_MTL_COMMON_H */
