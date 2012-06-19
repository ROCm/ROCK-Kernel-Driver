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
 * \author  djr
 *  \brief  Handy utility macros.
 *   \date  2003/01/17
 */

/*! \cidoxg_include_ci_compat  */

#ifndef __CI_COMPAT_UTILS_H__
#define __CI_COMPAT_UTILS_H__


/**********************************************************************
 * Alignment -- [align] must be a power of 2.
 **********************************************************************/

  /*! Align forward onto next boundary. */

#define CI_ALIGN_FWD(p, align)               (((p)+(align)-1u) & ~((align)-1u))


  /*! Align back onto prev boundary. */

#define CI_ALIGN_BACK(p, align)              ((p) & ~((align)-1u))


  /*! How far to next boundary? */

#define CI_ALIGN_NEEDED(p, align, signed_t)  (-(signed_t)(p) & ((align)-1u))


  /*! How far beyond prev boundary? */

#define CI_OFFSET(p, align)                  ((p) & ((align)-1u))


  /*! Does object fit in gap before next boundary? */

#define CI_FITS(p, size, align, signed_t)			\
  (CI_ALIGN_NEEDED((p) + 1, (align), signed_t) + 1 >= (size))


  /*! Align forward onto next boundary. */

#define CI_PTR_ALIGN_FWD(p, align)					   \
  ((char*) CI_ALIGN_FWD(((ci_ptr_arith_t)(p)), ((ci_ptr_arith_t)(align))))

  /*! Align back onto prev boundary. */

#define CI_PTR_ALIGN_BACK(p, align)					    \
  ((char*) CI_ALIGN_BACK(((ci_ptr_arith_t)(p)), ((ci_ptr_arith_t)(align))))

  /*! How far to next boundary? */

#define CI_PTR_ALIGN_NEEDED(p, align)					\
  CI_ALIGN_NEEDED(((ci_ptr_arith_t)(p)), ((ci_ptr_arith_t)(align)),	\
		  ci_ptr_arith_t)

  /*! How far to next boundary? NZ = not zero i.e. give align if on boundary  */

#define CI_PTR_ALIGN_NEEDED_NZ(p, align)					\
  ((align) - (((char*)p) -                                                      \
  ((char*) CI_ALIGN_BACK(((ci_ptr_arith_t)(p)), ((ci_ptr_arith_t)(align))))))

  /*! How far beyond prev boundary? */

#define CI_PTR_OFFSET(p, align)					\
  CI_OFFSET(((ci_ptr_arith_t)(p)), ((ci_ptr_arith_t)(align)))


  /* Same as CI_ALIGN_FWD and CI_ALIGN_BACK. */

#define CI_ROUND_UP(i, align)      (((i)+(align)-1u) & ~((align)-1u))

#define CI_ROUND_DOWN(i, align)    ((i) & ~((align)-1u))


/**********************************************************************
 * Byte-order
 **********************************************************************/

/* These are not flags.  They are enumeration values for use with
 * CI_MY_BYTE_ORDER. */
#define CI_BIG_ENDIAN          1
#define CI_LITTLE_ENDIAN       0

/*
** Note that these byte-swapping primitives may leave junk in bits above
** the range they operate on.
**
** The CI_BSWAP_nn() routines require that bits above [nn] are zero.  Use
** CI_BSWAPM_nn(x) if this cannot be guaranteed.
*/

/* ?? May be able to improve on some of these with inline assembler on some
** platforms.
*/

#define CI_BSWAP_16(v)    ((((v) & 0xff) << 8) | ((v) >> 8))
#define CI_BSWAPM_16(v)   ((((v) & 0xff) << 8) | (((v) & 0xff00) >> 8))

#define CI_BSWAP_32(v)    (((v) >> 24)               | 	\
			   (((v) & 0x00ff0000) >> 8) |	\
			   (((v) & 0x0000ff00) << 8) |	\
			   ((v) << 24))
#define CI_BSWAPM_32(v)   ((((v) & 0xff000000) >> 24) |	\
			   (((v) & 0x00ff0000) >> 8)  |	\
			   (((v) & 0x0000ff00) << 8)  |	\
			   ((v) << 24))

#define CI_BSWAP_64(v)    (((v) >> 56)                        |	\
			   (((v) & 0x00ff000000000000) >> 40) |	\
			   (((v) & 0x0000ff0000000000) >> 24) |	\
			   (((v) & 0x000000ff00000000) >> 8)  |	\
			   (((v) & 0x00000000ff000000) << 8)  |	\
			   (((v) & 0x0000000000ff0000) << 24) |	\
			   (((v) & 0x000000000000ff00) << 40) |	\
			   ((v) << 56))

# define CI_BSWAPPED_16_IF(c,v)  ((c) ? CI_BSWAP_16(v) : (v))
# define CI_BSWAPPED_32_IF(c,v)  ((c) ? CI_BSWAP_32(v) : (v))
# define CI_BSWAPPED_64_IF(c,v)  ((c) ? CI_BSWAP_64(v) : (v))
# define CI_BSWAP_16_IF(c,v)     do{ if((c)) (v) = CI_BSWAP_16(v); }while(0)
# define CI_BSWAP_32_IF(c,v)     do{ if((c)) (v) = CI_BSWAP_32(v); }while(0)
# define CI_BSWAP_64_IF(c,v)     do{ if((c)) (v) = CI_BSWAP_64(v); }while(0)

#if (CI_MY_BYTE_ORDER == CI_LITTLE_ENDIAN)
# define CI_BSWAP_LE16(v)    (v)
# define CI_BSWAP_LE32(v)    (v)
# define CI_BSWAP_LE64(v)    (v)
# define CI_BSWAP_BE16(v)    CI_BSWAP_16(v)
# define CI_BSWAP_BE32(v)    CI_BSWAP_32(v)
# define CI_BSWAP_BE64(v)    CI_BSWAP_64(v)
# define CI_BSWAPM_LE16(v)   (v)
# define CI_BSWAPM_LE32(v)   (v)
# define CI_BSWAPM_LE64(v)   (v)
# define CI_BSWAPM_BE16(v)   CI_BSWAPM_16(v)
# define CI_BSWAPM_BE32(v)   CI_BSWAPM_32(v)
#elif (CI_MY_BYTE_ORDER == CI_BIG_ENDIAN)
# define CI_BSWAP_BE16(v)    (v)
# define CI_BSWAP_BE32(v)    (v)
# define CI_BSWAP_BE64(v)    (v)
# define CI_BSWAP_LE16(v)    CI_BSWAP_16(v)
# define CI_BSWAP_LE32(v)    CI_BSWAP_32(v)
# define CI_BSWAP_LE64(v)    CI_BSWAP_64(v)
# define CI_BSWAPM_BE16(v)   (v)
# define CI_BSWAPM_BE32(v)   (v)
# define CI_BSWAPM_BE64(v)   (v)
# define CI_BSWAPM_LE16(v)   CI_BSWAPM_16(v)
# define CI_BSWAPM_LE32(v)   CI_BSWAPM_32(v)
#else
# error Bad endian.
#endif


/**********************************************************************
 * Get pointer to struct from pointer to member
 **********************************************************************/

#define CI_MEMBER_OFFSET(c_type, mbr_name)  \
  ((ci_uint32) (ci_uintptr_t)(&((c_type*)0)->mbr_name))

#define CI_MEMBER_SIZE(c_type, mbr_name)        \
  sizeof(((c_type*)0)->mbr_name)

#define __CI_CONTAINER(c_type, mbr_name, p_mbr)  \
  ( (c_type*) ((char*)(p_mbr) - CI_MEMBER_OFFSET(c_type, mbr_name)) )

#ifndef CI_CONTAINER
# define CI_CONTAINER(t,m,p)  __CI_CONTAINER(t,m,p)
#endif


/**********************************************************************
 * Structure member initialiser.
 **********************************************************************/

#ifndef CI_STRUCT_MBR
# define CI_STRUCT_MBR(name, val)	.name = val
#endif


/**********************************************************************
 * min / max
 **********************************************************************/ 

#define CI_MIN(x,y) (((x) < (y)) ? (x) : (y))
#define CI_MAX(x,y) (((x) > (y)) ? (x) : (y))

/**********************************************************************
 * abs
 **********************************************************************/ 

#define CI_ABS(x) (((x) < 0) ? -(x) : (x))

/**********************************************************************
 * Conditional debugging
 **********************************************************************/ 

#ifdef NDEBUG
# define CI_DEBUG(x)
# define CI_NDEBUG(x)      x
# define CI_IF_DEBUG(y,n)  (n)
# define CI_DEBUG_ARG(x)
#else
# define CI_DEBUG(x)       x
# define CI_NDEBUG(x)
# define CI_IF_DEBUG(y,n)  (y)
# define CI_DEBUG_ARG(x)   ,x
#endif

#ifdef __KERNEL__
#define CI_KERNEL_ARG(x)   ,x
#else
#define CI_KERNEL_ARG(x)
#endif

#ifdef _WIN32
# define CI_KERNEL_ARG_WIN(x) CI_KERNEL_ARG(x)
# define CI_ARG_WIN(x) ,x
#else
# define CI_KERNEL_ARG_WIN(x)
# define CI_ARG_WIN(x) 
#endif

#ifdef __unix__
# define CI_KERNEL_ARG_UNIX(x) CI_KERNEL_ARG(x)
# define CI_ARG_UNIX(x) ,x
#else
# define CI_KERNEL_ARG_UNIX(x)
# define CI_ARG_UNIX(x) 
#endif

#ifdef __linux__
# define CI_KERNEL_ARG_LINUX(x) CI_KERNEL_ARG(x)
# define CI_ARG_LINUX(x) ,x
#else
# define CI_KERNEL_ARG_LINUX(x)
# define CI_ARG_LINUX(x) 
#endif


#endif  /* __CI_COMPAT_UTILS_H__ */
/*! \cidoxg_end */
