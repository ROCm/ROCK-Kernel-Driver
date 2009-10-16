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
 *  \brief  Really-and-truely-honestly internal stuff for libef.
 *   \date  2004/06/13
 */

/*! \cidoxg_include_ci_ul */
#ifndef __CI_EF_VI_INTERNAL_H__
#define __CI_EF_VI_INTERNAL_H__


/* These flags share space with enum ef_vi_flags. */
#define EF_VI_BUG5692_WORKAROUND  0x10000


/* ***********************************************************************
 * COMPILATION CONTROL FLAGS (see ef_vi.h for "workaround" controls)
 */

#define EF_VI_DO_MAGIC_CHECKS 1


/**********************************************************************
 * Headers
 */

#include <etherfabric/ef_vi.h>
#include "sysdep.h"
#include "ef_vi_falcon.h"


/**********************************************************************
 * Debugging.
 */

#ifndef NDEBUG

# define _ef_assert(exp, file, line) BUG_ON(!(exp));

# define _ef_assert2(exp, x, y, file, line)  do {	\
		if (unlikely(!(exp)))		\
			BUG();				\
	} while (0)

#else

# define _ef_assert(exp, file, line)
# define _ef_assert2(e, x, y, file, line)

#endif

#define ef_assert(a)          do{ _ef_assert((a),__FILE__,__LINE__); } while(0)
#define ef_assert_equal(a,b)  _ef_assert2((a)==(b),(a),(b),__FILE__,__LINE__)
#define ef_assert_eq          ef_assert_equal
#define ef_assert_lt(a,b)     _ef_assert2((a)<(b),(a),(b),__FILE__,__LINE__)
#define ef_assert_le(a,b)     _ef_assert2((a)<=(b),(a),(b),__FILE__,__LINE__)
#define ef_assert_nequal(a,b) _ef_assert2((a)!=(b),(a),(b),__FILE__,__LINE__)
#define ef_assert_ne          ef_assert_nequal
#define ef_assert_ge(a,b)     _ef_assert2((a)>=(b),(a),(b),__FILE__,__LINE__)
#define ef_assert_gt(a,b)     _ef_assert2((a)>(b),(a),(b),__FILE__,__LINE__)

/**********************************************************************
 * Debug checks. ******************************************************
 **********************************************************************/

#ifdef NDEBUG
# define EF_VI_MAGIC_SET(p, type)
# define EF_VI_CHECK_VI(p)
# define EF_VI_CHECK_EVENT_Q(p)
# define EF_VI_CHECK_IOBUFSET(p)
# define EF_VI_CHECK_FILTER(p)
# define EF_VI_CHECK_SHMBUF(p)
# define EF_VI_CHECK_PT_EP(p)
#else
# define EF_VI                    0x3
# define EF_EPLOCK                0x6
# define EF_IOBUFSET              0x9
# define EF_FILTER                0xa
# define EF_SHMBUF                0x11

# define EF_VI_MAGIC(p, type)				\
	(((unsigned)(type) << 28) |			\
	 (((unsigned)(intptr_t)(p)) & 0x0fffffffu))

# if !EF_VI_DO_MAGIC_CHECKS
#  define EF_VI_MAGIC_SET(p, type)
#  define EF_VI_MAGIC_CHECK(p, type)
# else
#  define EF_VI_MAGIC_SET(p, type)			\
	do {						\
		(p)->magic = EF_VI_MAGIC((p), (type));	\
	} while (0)

# define EF_VI_MAGIC_OKAY(p, type)                      \
	((p)->magic == EF_VI_MAGIC((p), (type)))

# define EF_VI_MAGIC_CHECK(p, type)                     \
	ef_assert(EF_VI_MAGIC_OKAY((p), (type)))

#endif /* EF_VI_DO_MAGIC_CHECKS */

# define EF_VI_CHECK_VI(p)			\
	ef_assert(p);				\
	EF_VI_MAGIC_CHECK((p), EF_VI);

# define EF_VI_CHECK_EVENT_Q(p)			\
	ef_assert(p);				\
	EF_VI_MAGIC_CHECK((p), EF_VI);		\
	ef_assert((p)->evq_base);		\
	ef_assert((p)->evq_mask);

# define EF_VI_CHECK_PT_EP(p)			\
	ef_assert(p);				\
	EF_VI_MAGIC_CHECK((p), EF_VI);		\
	ef_assert((p)->ep_state);

# define EF_VI_CHECK_IOBUFSET(p)		\
	ef_assert(p);				\
	EF_VI_MAGIC_CHECK((p), EF_IOBUFSET)

# define EF_VI_CHECK_FILTER(p)			\
	ef_assert(p);				\
	EF_VI_MAGIC_CHECK((p), EF_FILTER);

# define EF_VI_CHECK_SHMBUF(p)			\
	ef_assert(p);				\
	EF_VI_MAGIC_CHECK((p), EF_SHMBUF);

#endif

#ifndef NDEBUG
# define EF_DRIVER_MAGIC 0x00f00ba4
# define EF_ASSERT_THIS_DRIVER_VALID(driver)				\
	do{ ef_assert(driver);						\
		EF_VI_MAGIC_CHECK((driver), EF_DRIVER_MAGIC);		\
		ef_assert((driver)->init);               }while(0)

# define EF_ASSERT_DRIVER_VALID() EF_ASSERT_THIS_DRIVER_VALID(&ci_driver)
#else
# define EF_ASSERT_THIS_DRIVER_VALID(driver)
# define EF_ASSERT_DRIVER_VALID()
#endif


/* *************************************
 * Power of 2 FIFO
 */

#define EF_VI_FIFO2_M(f, x)  ((x) & ((f)->fifo_mask))
#define ef_vi_fifo2_valid(f) ((f) && (f)->fifo && (f)->fifo_mask > 0 &&	\
			      (f)->fifo_rd_i <= (f)->fifo_mask       &&	\
			      (f)->fifo_wr_i <= (f)->fifo_mask       &&	\
			      EF_VI_IS_POW2((f)->fifo_mask+1u))

#define ef_vi_fifo2_init(f, cap)			\
	do{ ef_assert(EF_VI_IS_POW2((cap) + 1));	\
		(f)->fifo_rd_i = (f)->fifo_wr_i = 0u;	\
		(f)->fifo_mask = (cap);			\
	}while(0)

#define ef_vi_fifo2_is_empty(f) ((f)->fifo_rd_i == (f)->fifo_wr_i)
#define ef_vi_fifo2_capacity(f) ((f)->fifo_mask)
#define ef_vi_fifo2_buf_size(f) ((f)->fifo_mask + 1u)
#define ef_vi_fifo2_end(f)      ((f)->fifo + ef_vi_fifo2_buf_size(f))
#define ef_vi_fifo2_peek(f)     ((f)->fifo[(f)->fifo_rd_i])
#define ef_vi_fifo2_poke(f)     ((f)->fifo[(f)->fifo_wr_i])
#define ef_vi_fifo2_num(f)   EF_VI_FIFO2_M((f),(f)->fifo_wr_i-(f)->fifo_rd_i)

#define ef_vi_fifo2_wr_prev(f)						\
	do{ (f)->fifo_wr_i = EF_VI_FIFO2_M((f), (f)->fifo_wr_i - 1u); }while(0)
#define ef_vi_fifo2_wr_next(f)						\
	do{ (f)->fifo_wr_i = EF_VI_FIFO2_M((f), (f)->fifo_wr_i + 1u); }while(0)
#define ef_vi_fifo2_rd_adv(f, n)					\
	do{ (f)->fifo_rd_i = EF_VI_FIFO2_M((f), (f)->fifo_rd_i + (n)); }while(0)
#define ef_vi_fifo2_rd_prev(f)						\
	do{ (f)->fifo_rd_i = EF_VI_FIFO2_M((f), (f)->fifo_rd_i - 1u); }while(0)
#define ef_vi_fifo2_rd_next(f)						\
	do{ (f)->fifo_rd_i = EF_VI_FIFO2_M((f), (f)->fifo_rd_i + 1u); }while(0)

#define ef_vi_fifo2_put(f, v)						\
	do{ ef_vi_fifo2_poke(f) = (v); ef_vi_fifo2_wr_next(f); }while(0)
#define ef_vi_fifo2_get(f, pv)						\
	do{ *(pv) = ef_vi_fifo2_peek(f); ef_vi_fifo2_rd_next(f); }while(0)


/* *********************************************************************
 * Eventq handling
 */

typedef union {
	uint64_t    u64;
	struct {
		uint32_t  a;
		uint32_t  b;
	} opaque;
} ef_vi_event;


#define EF_VI_EVENT_OFFSET(q, i)					\
	(((q)->evq_state->evq_ptr - (i) * sizeof(ef_vi_event)) & (q)->evq_mask)

#define EF_VI_EVENT_PTR(q, i)                                           \
	((ef_vi_event*) ((q)->evq_base + EF_VI_EVENT_OFFSET((q), (i))))

/* *********************************************************************
 * Miscellaneous goodies
 */
#ifdef NDEBUG
# define EF_VI_DEBUG(x)
#else
# define EF_VI_DEBUG(x)            x
#endif

#define EF_VI_ROUND_UP(i, align)   (((i)+(align)-1u) & ~((align)-1u))
#define EF_VI_ALIGN_FWD(p, align)  (((p)+(align)-1u) & ~((align)-1u))
#define EF_VI_ALIGN_BACK(p, align) ((p) & ~((align)-1u))
#define EF_VI_PTR_ALIGN_BACK(p, align)					\
	((char*)EF_VI_ALIGN_BACK(((intptr_t)(p)), ((intptr_t)(align))))
#define EF_VI_IS_POW2(x)           ((x) && ! ((x) & ((x) - 1)))


/* ******************************************************************** 
 */

extern void falcon_vi_init(ef_vi*, void* vvis ) EF_VI_HF;
extern void ef_eventq_state_init(ef_vi* evq) EF_VI_HF;
extern void __ef_init(void) EF_VI_HF;


#endif  /* __CI_EF_VI_INTERNAL_H__ */

