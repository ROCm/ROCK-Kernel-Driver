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

#ifndef H_MOSAL_ARCH_H
#define H_MOSAL_ARCH_H
 
#include <mtl_types.h>
#include <mtl_errno.h>
#include <mtl_common.h>

#if defined (__i386__)

#define MT_LOCK_PREFIX "lock ; "
/* #ifdef __SMP__ */
/* #define MT_LOCK_PREFIX "lock ; " */
/* #define MT_LOCK_PREFIX  */
/* #else   */
/* #define MT_LOCK_PREFIX "" */
/* #endif */

static __inline__ int MOSAL_arch_test_set_bit32(int nr, volatile void * addr)
{
        int oldbit;

        __asm__ __volatile__( MT_LOCK_PREFIX
                        "btsl %2,%1\n\tsbbl %0,%0"
                        :"=r" (oldbit),"=m" (*(volatile long *) addr)
                        :"Ir" (nr) : "memory");
        return oldbit;
}


static __inline__ int MOSAL_arch_test_clear_bit32(int nr, volatile void * addr)
{
        int oldbit;

        __asm__ __volatile__( MT_LOCK_PREFIX
                        "btrl %2,%1\n\tsbbl %0,%0"
                        :"=r" (oldbit),"=m" (*(volatile long *) addr)
                        :"Ir" (nr) : "memory");
        return oldbit;
}

#endif  /* I386 */


#if defined (powerpc)


#ifdef CONFIG_SMP
#define MT_SMP_WMB     "eieio\n"
#define MT_SMP_MB      "\nsync"
#else
#define MT_SMP_WMB
#define MT_SMP_MB
#endif 

static __inline__ int MOSAL_arch_test_set_bit32(int nr, volatile void *addr)
{
        unsigned int old, t;
        unsigned int mask = 1 << (nr & 0x1f);
        volatile unsigned int *p = ((volatile unsigned int *)addr) + (nr >> 5);

        __asm__ __volatile__(MT_SMP_WMB "\n\
                1:  lwarx   %0,0,%4 \n\
                        or  %1,%0,%3 \n\
                        stwcx.  %1,0,%4 \n\
                        bne 1b"
                        MT_SMP_MB
                        : "=&r" (old), "=&r" (t), "=m" (*p)
                        : "r" (mask), "r" (p), "m" (*p)
                        : "cc", "memory");

        return (old & mask) != 0;
}


static __inline__ int MOSAL_arch_test_clear_bit32(int nr, volatile void *addr)
{
        unsigned int old, t;
        unsigned int mask = 1 << (nr & 0x1f);
        volatile unsigned int *p = ((volatile unsigned int *)addr) + (nr >> 5);

        __asm__ __volatile__(MT_SMP_WMB "\n\
                1:  lwarx   %0,0,%4 \n\
                        andc    %1,%0,%3 \n\
                        stwcx.  %1,0,%4 \n\
                        bne 1b"
                        MT_SMP_MB
                        : "=&r" (old), "=&r" (t), "=m" (*p)
                        : "r" (mask), "r" (p), "m" (*p)
                        : "cc", "memory");

        return (old & mask) != 0;
}

#endif  /* PPC */

#if defined (__ia64__)

extern long __cmpxchg_called_with_bad_pointer(void);

#ifndef ia64_cmpxchg
#define ia64_cmpxchg(sem,ptr,old,_new,size)                                              \
({                                                                                      \
        __typeof__(ptr) _p_ = (ptr);                                                    \
        __typeof__(_new) _n_ = (_new);                                                    \
        __u64 _o_, _r_;                                                                 \
                                                                                        \
        switch (size) {                                                                 \
              case 1: _o_ = (__u8 ) (long) (old); break;                                \
              case 2: _o_ = (__u16) (long) (old); break;                                \
              case 4: _o_ = (__u32) (long) (old); break;                                \
              case 8: _o_ = (__u64) (long) (old); break;                                \
              default: break;                                                           \
        }                                                                               \
         __asm__ __volatile__ ("mov ar.ccv=%0;;" :: "rO"(_o_));                         \
        switch (size) {                                                                 \
              case 1:                                                                   \
                __asm__ __volatile__ ("cmpxchg1."sem" %0=[%1],%2,ar.ccv"                \
                                      : "=r"(_r_) : "r"(_p_), "r"(_n_) : "memory");     \
                break;                                                                  \
                                                                                        \
              case 2:                                                                   \
                __asm__ __volatile__ ("cmpxchg2."sem" %0=[%1],%2,ar.ccv"                \
                                      : "=r"(_r_) : "r"(_p_), "r"(_n_) : "memory");     \
                break;                                                                  \
                                                                                        \
              case 4:                                                                   \
                __asm__ __volatile__ ("cmpxchg4."sem" %0=[%1],%2,ar.ccv"                \
                                      : "=r"(_r_) : "r"(_p_), "r"(_n_) : "memory");     \
                break;                                                                  \
                                                                                        \
              case 8:                                                                   \
                __asm__ __volatile__ ("cmpxchg8."sem" %0=[%1],%2,ar.ccv"                \
                                      : "=r"(_r_) : "r"(_p_), "r"(_n_) : "memory");     \
                break;                                                                  \
                                                                                        \
              default:                                                                  \
                _r_ = __cmpxchg_called_with_bad_pointer();                              \
                break;                                                                  \
        }                                                                               \
        (__typeof__(old)) _r_;                                                          \
})
#endif

#ifndef cmpxchg_acq
#define cmpxchg_acq(ptr,o,n)    ia64_cmpxchg("acq", (ptr), (o), (n), sizeof(*(ptr)))
#define cmpxchg_rel(ptr,o,n)    ia64_cmpxchg("rel", (ptr), (o), (n), sizeof(*(ptr)))
#endif

#ifdef CONFIG_IA64_DEBUG_CMPXCHG
# define CMPXCHG_BUGCHECK_DECL  int _cmpxchg_bugcheck_count = 128;
# define CMPXCHG_BUGCHECK(v)                                                    \
  do {                                                                          \
        if (_cmpxchg_bugcheck_count-- <= 0) {                                   \
                void *ip;                                                       \
                extern int printk(const char *fmt, ...);                        \
                asm ("mov %0=ip" : "=r"(ip));                                   \
                printk("CMPXCHG_BUGCHECK: stuck at %p on word %p\n", ip, (v));  \
                break;                                                          \
        }                                                                       \
  } while (0)
#else /* !CONFIG_IA64_DEBUG_CMPXCHG */
# define CMPXCHG_BUGCHECK_DECL
# define CMPXCHG_BUGCHECK(v)
#endif /* !CONFIG_IA64_DEBUG_CMPXCHG */

static __inline__ int MOSAL_arch_test_set_bit32(int nr, volatile void * addr)
{
        __u32 bit, old, _new;
        volatile __u32 *m;
        CMPXCHG_BUGCHECK_DECL

        m = (volatile __u32 *) addr + (nr >> 5);
        bit = 1 << (nr & 31);
        do {
                CMPXCHG_BUGCHECK(m);
                old = *m;
                _new = old | bit;
        } while (cmpxchg_acq(m, old, _new) != old);
        return (old & bit) != 0;
}

static __inline__ int MOSAL_arch_test_clear_bit32(int nr, volatile void * addr)
{
        __u32 mask, old, _new;
        volatile __u32 *m;
        CMPXCHG_BUGCHECK_DECL

        m = (volatile __u32 *) addr + (nr >> 5);
        mask = ~(1 << (nr & 31));
        do {
                CMPXCHG_BUGCHECK(m);
                old = *m;
                _new = old & mask;
        } while (cmpxchg_acq(m, old, _new) != old);
        return (old & ~mask) != 0;
}
#endif

#endif
