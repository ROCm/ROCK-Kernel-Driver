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

/*! \cidoxg_include_ci_tools_platform  */

#ifndef __CI_TOOLS_GCC_X86_H__
#define __CI_TOOLS_GCC_X86_H__


/**********************************************************************
 * Free-running cycle counters.
 */

#define CI_HAVE_FRC64
#define CI_HAVE_FRC32

#define ci_frc32(pval)  __asm__ __volatile__("rdtsc" : "=a" (*pval) : : "edx")

#if defined(__x86_64__)
ci_inline void ci_frc64(ci_uint64* pval) {
  /* temp fix until we figure how to get this out in one bite */	   
  ci_uint64 low, high;
  __asm__ __volatile__("rdtsc" : "=a" (low) , "=d" (high));	 	
  *pval = (high << 32) | low;
}

#else
#define ci_frc64(pval)  __asm__ __volatile__("rdtsc" : "=A" (*pval))
#endif

#define ci_frc_flush()  /* ?? Need a pipeline barrier. */


/**********************************************************************
 * Atomic integer.
 */

/*
** int  ci_atomic_read(a)         { return a->n;        }
** void ci_atomic_set(a, v)       { a->n = v;           }
** void ci_atomic_inc(a)          { ++a->n;             }
** void ci_atomic_dec(a)          { --a->n;             }
** int  ci_atomic_inc_and_test(a) { return ++a->n == 0; }
** int  ci_atomic_dec_and_test(a) { return --a->n == 0; }
** void ci_atomic_and(a, v)       { a->n &= v;          }
** void ci_atomic_or(a, v)        { a->n |= v;          }
*/

typedef struct { volatile ci_int32 n; } ci_atomic_t;

#define CI_ATOMIC_INITIALISER(i)  {(i)}

static inline ci_int32  ci_atomic_read(const ci_atomic_t* a) { return a->n; }
static inline void ci_atomic_set(ci_atomic_t* a, int v) { a->n = v; ci_wmb();   }

static inline void ci_atomic_inc(ci_atomic_t* a)
{ __asm__ __volatile__("lock; incl %0" : "+m" (a->n)); }

 
static inline void ci_atomic_dec(ci_atomic_t* a)
{ __asm__ __volatile__("lock; decl %0" : "+m" (a->n)); }

static inline int ci_atomic_inc_and_test(ci_atomic_t* a) {
  char r;
  __asm__ __volatile__("lock; incl %0; sete %1"
		       : "+m" (a->n), "=qm" (r));
  return r;
}

static inline int ci_atomic_dec_and_test(ci_atomic_t* a) {
  char r;
  __asm__ __volatile__("lock; decl %0; sete %1"
		       : "+m" (a->n), "=qm" (r));
  return r;
}

ci_inline int
ci_atomic_xadd (ci_atomic_t *a, int v) {
   __asm__ ("lock xadd %0, %1" : "=r" (v), "+m" (a->n) : "0" (v));
  return v;
}
ci_inline int
ci_atomic_xchg (ci_atomic_t *a, int v) {
   __asm__ ("lock xchg %0, %1" : "=r" (v), "+m" (a->n) : "0" (v));
  return v;
}

ci_inline void ci_atomic32_or(volatile ci_uint32* p, ci_uint32 mask)
{ __asm__ __volatile__("lock; orl %1, %0" : "+m" (*p) : "ir" (mask)); }

ci_inline void ci_atomic32_and(volatile ci_uint32* p, ci_uint32 mask)
{ __asm__ __volatile__("lock; andl %1, %0" : "+m" (*p) : "ir" (mask)); }

ci_inline void ci_atomic32_add(volatile ci_uint32* p, ci_uint32 v)
{ __asm__ __volatile__("lock; addl %1, %0" : "+m" (*p) : "ir" (v)); }

ci_inline void ci_atomic32_inc(volatile ci_uint32* p)
{ __asm__ __volatile__("lock; incl %0" : "+m" (*p)); }

ci_inline int ci_atomic32_dec_and_test(volatile ci_uint32* p) {
  char r;
  __asm__ __volatile__("lock; decl %0; sete %1" : "+m" (*p), "=qm" (r));
  return r;
}

#define ci_atomic_or(a, v)   ci_atomic32_or ((ci_uint32*) &(a)->n, (v))
#define ci_atomic_and(a, v)  ci_atomic32_and((ci_uint32*) &(a)->n, (v))
#define ci_atomic_add(a, v)  ci_atomic32_add((ci_uint32*) &(a)->n, (v))

extern int ci_glibc_uses_nptl (void) CI_HF;
extern int ci_glibc_nptl_broken(void) CI_HF;
extern int ci_glibc_gs_get_is_multihreaded_offset (void) CI_HF;
extern int ci_glibc_gs_is_multihreaded_offset CI_HV;

#if !defined(__x86_64__)
#ifdef __GLIBC__
/* Returns non-zero if the calling process might be mulithreaded, returns 0 if
 * it definitely isn't (i.e. if reimplementing this function for other
 * architectures and platforms, you can safely just return 1).
 */
static inline int ci_is_multithreaded (void) {

  while (1) {
    if (ci_glibc_gs_is_multihreaded_offset >= 0) {
      /* NPTL keeps a variable that tells us this hanging off gs (i.e. in thread-
       * local storage); just return this
       */
      int r;
      __asm__ __volatile__ ("movl %%gs:(%1), %0"
                            : "=r" (r)
                            : "r" (ci_glibc_gs_is_multihreaded_offset));
      return r;
    }

    if (ci_glibc_gs_is_multihreaded_offset == -2) {
      /* This means we've already determined that the libc version is NOT good
       * for our funky "is multithreaded" hack
       */
      return 1;
    }

    /* If we get here, it means this is the first time the function has been
     * called -- detect the libc version and go around again.
     */
    ci_glibc_gs_is_multihreaded_offset = ci_glibc_gs_get_is_multihreaded_offset ();

    /* Go around again.  We do the test here rather than at the top so that we go
     * quicker in the common the case
     */
  }
}

#else    /* def __GLIBC__ */

#define ci_is_multithreaded() 1 /* ?? Is the the POSIX way of finding out */
                                /*    whether the appication is single */
                                /*    threaded? */

#endif   /* def __GLIBC__ */

#else    /* defined __x86_64__ */

static inline int ci_is_multithreaded (void) {
  /* Now easy way to tell on x86_64; so assume we're multithreaded */
  return 1;
}

#endif    /* defined __x86_64__ */


/**********************************************************************
 * Compare and swap.
 */

#define CI_HAVE_COMPARE_AND_SWAP

ci_inline int ci_cas32_succeed(volatile ci_int32* p, ci_int32 oldval,
                               ci_int32 newval) {
  char ret;
  ci_int32 prevval;
  __asm__ __volatile__("lock; cmpxchgl %3, %1; sete %0"
		       : "=q"(ret), "+m"(*p), "=a"(prevval)
		       : "r"(newval), "a"(oldval));
  return ret;
}

ci_inline int ci_cas32_fail(volatile ci_int32* p, ci_int32 oldval,
                            ci_int32 newval) {
  char ret;
  ci_int32 prevval;
  __asm__ __volatile__("lock; cmpxchgl %3, %1; setne %0"
		       : "=q"(ret), "+m"(*p), "=a"(prevval)
		       : "r"(newval), "a"(oldval));
  return ret;
}

#ifdef __x86_64__
ci_inline int ci_cas64_succeed(volatile ci_int64* p, ci_int64 oldval,
			       ci_int64 newval) {
  char ret;
  ci_int64 prevval;
  __asm__ __volatile__("lock; cmpxchgq %3, %1; sete %0"
		       : "=q"(ret), "+m"(*p), "=a"(prevval)
		       : "r"(newval), "a"(oldval));
  return ret;
}

ci_inline int ci_cas64_fail(volatile ci_int64* p, ci_int64 oldval,
			    ci_int64 newval) {
  char ret;
  ci_int64 prevval;
  __asm__ __volatile__("lock; cmpxchgq %3, %1; setne %0"
		       : "=q"(ret), "+m"(*p), "=a"(prevval)
		       : "r"(newval), "a"(oldval));
  return ret;
}
#endif

ci_inline int ci_cas32u_succeed(volatile ci_uint32* p, ci_uint32 oldval, ci_uint32 newval) {
  char ret;
  ci_uint32 prevval;
  __asm__ __volatile__("lock; cmpxchgl %3, %1; sete %0"
		       : "=q"(ret), "+m"(*p), "=a"(prevval)
		       : "r"(newval), "a"(oldval));
  return ret;
}

ci_inline int ci_cas32u_fail(volatile ci_uint32* p, ci_uint32 oldval, ci_uint32 newval) {
  char ret;
  ci_uint32 prevval;
  __asm__ __volatile__("lock; cmpxchgl %3, %1; setne %0"
		       : "=q"(ret), "+m"(*p), "=a"(prevval)
		       : "r"(newval), "a"(oldval));
  return ret;
}

ci_inline int ci_cas64u_succeed(volatile ci_uint64* p, ci_uint64 oldval,
			       ci_uint64 newval) {
  char ret;
  ci_uint64 prevval;
  __asm__ __volatile__("lock; cmpxchgq %3, %1; sete %0"
		       : "=q"(ret), "+m"(*p), "=a"(prevval)
		       : "r"(newval), "a"(oldval));
  return ret;
}

ci_inline int ci_cas64u_fail(volatile ci_uint64* p, ci_uint64 oldval,
			    ci_uint64 newval) {
  char ret;
  ci_uint64 prevval;
  __asm__ __volatile__("lock; cmpxchgq %3, %1; setne %0"
		       : "=q"(ret), "+m"(*p), "=a"(prevval)
		       : "r"(newval), "a"(oldval));
  return ret;
}

#ifdef __x86_64__

# define ci_cas_uintptr_succeed(p,o,n)				\
    ci_cas64u_succeed((volatile ci_uint64*) (p), (o), (n))
# define ci_cas_uintptr_fail(p,o,n)				\
    ci_cas64u_fail((volatile ci_uint64*) (p), (o), (n))

#else

# define ci_cas_uintptr_succeed(p,o,n)				\
    ci_cas32u_succeed((volatile ci_uint32*) (p), (o), (n))
# define ci_cas_uintptr_fail(p,o,n)				\
    ci_cas32u_fail((volatile ci_uint32*) (p), (o), (n))

#endif


/**********************************************************************
 * Atomic bit field.
 */

typedef ci_uint32  ci_bits;
#define CI_BITS_N			32u

#define CI_BITS_DECLARE(name, n)			\
  ci_bits name[((n) + CI_BITS_N - 1u) / CI_BITS_N]

ci_inline void ci_bits_clear_all(volatile ci_bits* b, int n_bits)
{ memset((void*) b, 0, (n_bits+CI_BITS_N-1u) / CI_BITS_N * sizeof(ci_bits)); }

ci_inline void ci_bit_set(volatile ci_bits* b, int i) {
  __asm__ __volatile__("lock; btsl %1, %0"
		       : "=m" (*b)
		       : "Ir" (i));
}

ci_inline void ci_bit_clear(volatile ci_bits* b, int i) {
  __asm__ __volatile__("lock; btrl %1, %0"
		       : "=m" (*b)
		       : "Ir" (i));
}

ci_inline int  ci_bit_test(volatile ci_bits* b, int i) {
  char rc;
  __asm__("btl %2, %1; setc %0"
	  : "=r" (rc)
	  : "m" (*b), "Ir" (i));
  return rc;
}

ci_inline int ci_bit_test_and_set(volatile ci_bits* b, int i) {
  char rc;
  __asm__ __volatile__("lock; btsl %2, %1; setc %0"
		       : "=r" (rc), "+m" (*b)
		       : "Ir" (i));
  return rc;
}

ci_inline int ci_bit_test_and_clear(volatile ci_bits* b, int i) {
  char rc;
  __asm__ __volatile__("lock; btrl %2, %1; setc %0"
		       : "=r" (rc), "+m" (*b)
		       : "Ir" (i));
  return rc;
}

/* These mask ops only work within a single ci_bits word. */
#define ci_bit_mask_set(b,m)	ci_atomic32_or((b), (m))
#define ci_bit_mask_clear(b,m)	ci_atomic32_and((b), ~(m))


/**********************************************************************
 * Misc.
 */

#if __GNUC__ >= 3
# define ci_spinloop_pause()  __asm__("pause") 
#else
# define ci_spinloop_pause()  __asm__(".byte 0xf3, 0x90")
#endif


#define CI_HAVE_ADDC32
#define ci_add_carry32(sum, v)  __asm__("addl %1, %0 ;"			  \
					"adcl $0, %0 ;"			  \
					: "=r" (sum)			  \
					: "g" ((ci_uint32) v), "0" (sum))


#endif  /* __CI_TOOLS_GCC_X86_H__ */

/*! \cidoxg_end */
