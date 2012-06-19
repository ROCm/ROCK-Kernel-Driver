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

/*! \cidoxg_include_ci_compat  */

#ifndef __CI_COMPAT_GCC_H__
#define __CI_COMPAT_GCC_H__


#define CI_HAVE_INT64


#if defined(__linux__) && defined(__KERNEL__)

# include <linux/types.h>

typedef __u64                 ci_uint64;
typedef __s64                 ci_int64;
# if BITS_PER_LONG == 32
typedef __s32                 ci_ptr_arith_t;
typedef __u32                 ci_uintptr_t;
# else
typedef __s64                 ci_ptr_arith_t;
typedef __u64                 ci_uintptr_t;
# endif


/* it's not obvious to me why the below is wrong for x64_64, but
 * gcc seems to complain on this platform
 */
# if defined(__ia64__)
#  define CI_PRId64            "ld"
#  define CI_PRIi64            "li"
#  define CI_PRIo64            "lo"
#  define CI_PRIu64            "lu"
#  define CI_PRIx64            "lx"
#  define CI_PRIX64            "lX"
# else
#  define CI_PRId64            "lld"
#  define CI_PRIi64            "lli"
#  define CI_PRIo64            "llo"
#  define CI_PRIu64            "llu"
#  define CI_PRIx64            "llx"
#  define CI_PRIX64            "llX"
# endif

# define CI_PRId32            "d"
# define CI_PRIi32            "i"
# define CI_PRIo32            "o"
# define CI_PRIu32            "u"
# define CI_PRIx32            "x"
# define CI_PRIX32            "X"

#else

# include <stdint.h>
# include <inttypes.h>

typedef uint64_t              ci_uint64;
typedef int64_t               ci_int64;
typedef intptr_t              ci_ptr_arith_t;
typedef uintptr_t             ci_uintptr_t;

# define CI_PRId64            PRId64
# define CI_PRIi64            PRIi64
# define CI_PRIo64            PRIo64
# define CI_PRIu64            PRIu64
# define CI_PRIx64            PRIx64
# define CI_PRIX64            PRIX64

# define CI_PRId32            PRId32
# define CI_PRIi32            PRIi32
# define CI_PRIo32            PRIo32
# define CI_PRIu32            PRIu32
# define CI_PRIx32            PRIx32
# define CI_PRIX32            PRIX32

#endif


typedef ci_uint64                       ci_fixed_descriptor_t;

#define from_fixed_descriptor(desc) ((ci_uintptr_t)(desc))
#define to_fixed_descriptor(desc) ((ci_fixed_descriptor_t)(ci_uintptr_t)(desc))


#if __GNUC__ >= 3 && !defined(__cplusplus)
/*
** Checks that [p_mbr] has the same type as [&c_type::mbr_name].
*/
# define CI_CONTAINER(c_type, mbr_name, p_mbr)				\
   __builtin_choose_expr(						\
     __builtin_types_compatible_p(__typeof__(&((c_type*)0)->mbr_name),	\
				 __typeof__(p_mbr)),			\
     __CI_CONTAINER(c_type, mbr_name, p_mbr), (void)0)

# define ci_restrict  __restrict__
#endif


#if !defined(__KERNEL__) || defined(__unix__)
#define CI_HAVE_NPRINTF  1
#endif


/* At what version was this introduced? */
#if __GNUC__ >= 3 || (__GNUC__ == 2 && __GNUC_MINOR__ > 91)
# define CI_LIKELY(t)    __builtin_expect((t), 1)
# define CI_UNLIKELY(t)  __builtin_expect((t), 0)
#endif

/**********************************************************************
 * Attributes
 */
#if __GNUC__ >= 3 && defined(NDEBUG)
# define CI_HF __attribute__((visibility("hidden")))
# define CI_HV __attribute__((visibility("hidden")))
#else
# define CI_HF
# define CI_HV
#endif

#if __GNUC__ >= 4 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
# define ci_noinline  static __attribute__((__noinline__))
/* (Linux 2.6 defines its own "noinline", so we use the "__noinline__" form) */
#else
# define ci_noinline  static
#endif

#define CI_ALIGN(x) __attribute__ ((aligned (x)))

#define CI_PRINTF_LIKE(a,b) __attribute__((format(printf,a,b)))

#endif  /* __CI_COMPAT_GCC_H__ */

/*! \cidoxg_end */
