/****************************************************************************
 * Copyright 2002-2005: Level 5 Networks Inc.
 * Copyright 2005-2008: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Maintained by Solarflare Communications
 *  <linux-xen-drivers@solarflare.com>
 *  <onload-dev@solarflare.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

/*
 * \author  stg
 *  \brief  System dependent support for ef vi lib
 *   \date  2007/05/10
 */

/*! \cidoxg_include_ci_ul */
#ifndef __CI_CIUL_SYSDEP_LINUX_H__
#define __CI_CIUL_SYSDEP_LINUX_H__

/**********************************************************************
 * Kernel version compatability
 */

#if defined(__GNUC__)

/* Linux kernel doesn't have stdint.h or [u]intptr_t. */
# if !defined(LINUX_VERSION_CODE)
#  include <linux/version.h>
# endif
# include <asm/io.h>

/* In Linux 2.6.24, linux/types.h has uintptr_t */
# if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
#  if BITS_PER_LONG == 32
   typedef __u32         uintptr_t;
#  else
   typedef __u64         uintptr_t;
#  endif
# endif

/* But even 2.6.24 doesn't define intptr_t */
# if BITS_PER_LONG == 32
   typedef __s32         intptr_t;
# else
   typedef __s64         intptr_t;
# endif

# if defined(__ia64__)
#  define EF_VI_PRIx64  "lx"
# else
#  define EF_VI_PRIx64  "llx"
# endif

# define EF_VI_HF __attribute__((visibility("hidden")))
# define EF_VI_HV __attribute__((visibility("hidden")))

# if defined(__i386__) || defined(__x86_64__)  /* GCC x86/x64 */
   typedef unsigned long long ef_vi_dma_addr_t; 
#  if __GNUC__ >= 3 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 96)
#   define ef_vi_wiob()  __asm__ __volatile__ ("sfence")
#  else
#   define ef_vi_wiob()  __asm__ __volatile__ (".byte 0x0F, 0xAE, 0xF8")
#  endif

# endif
#endif

#ifdef EFX_NOT_UPSTREAM

/* Stuff for architectures/compilers not officially supported */

#if !defined(__GNUC__)
# if defined(__PPC__)  /* GCC, PPC */
   typedef unsigned long     ef_vi_dma_addr_t;
#  define ef_vi_wiob()  wmb()

#  ifdef __powerpc64__
#   ifdef CONFIG_SMP
#    define CI_SMP_SYNC        "\n   eieio     \n"         /* memory cache sync */
#    define CI_SMP_ISYNC       "\n   isync     \n"         /* instr cache sync */
#   else
#    define CI_SMP_SYNC
#    define CI_SMP_ISYNC
#   endif
#  else	 /* for ppc32 systems */
#   ifdef CONFIG_SMP
#    define CI_SMP_SYNC        "\n   eieio     \n"
#    define CI_SMP_ISYNC       "\n   sync      \n"
#   else
#    define CI_SMP_SYNC
#    define CI_SMP_ISYNC
#   endif
#  endif

# elif defined(__ia64__)  /* GCC, IA64 */
   typedef unsigned long     ef_vi_dma_addr_t;
#  define ef_vi_wiob()  __asm__ __volatile__("mf.a": : :"memory")

# else
#  error Unknown processor - GNU C
# endif

#elif defined(__PGI)
# error PGI not supported 

#elif defined(__INTEL_COMPILER)

/* Intel compilers v7 claim to be very gcc compatible. */
# if __INTEL_COMPILER >= 700
#  if __GNUC__ >= 3 || (__GNUC__ == 2 && __GNUC_MINOR__ > 91)
#   define EF_VI_LIKELY(t)    __builtin_expect((t), 1)
#   define EF_VI_UNLIKELY(t)  __builtin_expect((t), 0)
#  endif

#  if __GNUC__ >= 3 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 96)
#   define ef_vi_wiob()  __asm__ __volatile__ ("sfence")
#  else
#   define ef_vi_wiob()  __asm__ __volatile__ (".byte 0x0F, 0xAE, 0xF8")
#  endif

# else
#  error Old Intel compiler not supported.
# endif

#else
# error Unknown compiler.
#endif

#endif


# include <linux/errno.h>


/**********************************************************************
 * Extracting bit fields.
 */

#define _QWORD_GET_LOW(f, v)                                    \
  (((v).u32[0] >> (f##_LBN)) & ((1u << f##_WIDTH) - 1u))
#define _QWORD_GET_HIGH(f, v)                                           \
  (((v).u32[1] >> (f##_LBN - 32u)) & ((1u << f##_WIDTH) - 1u))
#define _QWORD_GET_ANY(f, v)                                            \
  (((v).u64[0] >> f##_LBN) & (((uint64_t) 1u << f##_WIDTH) - 1u))

#define QWORD_GET(f, v)                                                     \
  ((f##_LBN + f##_WIDTH) <= 32u                                             \
   ? _QWORD_GET_LOW(f, (v))                                                 \
   : ((f##_LBN >= 32u) ? _QWORD_GET_HIGH(f, (v)) : _QWORD_GET_ANY(f, (v))))

#define QWORD_GET_U(f, v)  ((unsigned) QWORD_GET(f, (v)))

#define _QWORD_TEST_BIT_LOW(f, v)   ((v).u32[0] & (1u << (f##_LBN)))
#define _QWORD_TEST_BIT_HIGH(f, v)  ((v).u32[1] & (1u << (f##_LBN - 32u)))

#define QWORD_TEST_BIT(f, v)                                                  \
  (f##_LBN < 32 ? _QWORD_TEST_BIT_LOW(f, (v)) : _QWORD_TEST_BIT_HIGH(f, (v)))




#ifndef DECLSPEC_NORETURN
/* normally defined on Windows to expand to a declaration that the
   function will not return */
# define DECLSPEC_NORETURN
#endif

#endif  /* __CI_CIUL_SYSDEP_LINUX_H__ */
