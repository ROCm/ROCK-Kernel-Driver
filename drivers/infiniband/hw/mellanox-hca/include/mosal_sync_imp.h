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

#ifndef H_MOSAL_SYNC_IMP_H
#define H_MOSAL_SYNC_IMP_H

/* For spinlock deadlocks debug:                                                  */
/* Uncomment line below and recomplile mlxsys/mosal + debugged module (e.g., HCA) */
/* #define MOSAL_SPINLOCK_DEBUG */

#ifdef __KERNEL__

typedef enum {
  SPL_NORMAL,
  SPL_DPC_SAFE,
  SPL_IRQ_SAFE,
  SPL_DEADLOCKED
}
spinlock_level_t;

/* sync object */
struct MOSAL_syncobj {
  wait_queue_head_t wq;
  volatile MT_bool  signalled;
};

/* semaphore */
#define MOSAL_semaphore 	semaphore

/* mutex */
typedef struct semaphore MOSAL_mutex_t;


#ifndef MOSAL_SPINLOCK_DEBUG
/* spinlock */
typedef struct {
  spinlock_t       lock;
  unsigned long    flags;
  spinlock_level_t level;
} MOSAL_spinlock_t;

static inline call_result_t MOSAL_spinlock_lock(MOSAL_spinlock_t *sp)
{
  spin_lock(&sp->lock);
  sp->level = SPL_NORMAL;
  return MT_OK;
}


static inline call_result_t MOSAL_spinlock_irq_lock(MOSAL_spinlock_t *sp)
{
  unsigned long tmp_flags; /* must use temporary flags in order to protect previously save flags */

  spin_lock_irqsave(&sp->lock, tmp_flags);
  sp->flags= tmp_flags;
  sp->level = SPL_IRQ_SAFE;
  return MT_OK;
}


static inline call_result_t MOSAL_spinlock_dpc_lock(MOSAL_spinlock_t *sp)
{
  spin_lock_bh(&sp->lock);
  sp->level = SPL_DPC_SAFE;
  return MT_OK;
}

static inline void MOSAL_spinlock_unlock(MOSAL_spinlock_t *sp)
{
  unsigned long tmp_flags;

  switch ( sp->level ) {
    case SPL_NORMAL:
      spin_unlock(&sp->lock);
      break;

    case SPL_DPC_SAFE:
      spin_unlock_bh(&sp->lock);
      break;

    case SPL_IRQ_SAFE:
      tmp_flags = sp->flags;
      spin_unlock_irqrestore(&sp->lock, tmp_flags);
      break;

    default:
      /* this realy should not happen */
      MTL_ERROR1(MT_FLFMT("corrupt spinlock"));
  }
}


#else /* MOSAL_SPINLOCK_DEBUG */

#define MOSAL_SPINLOCK_MAX_SPIN 100000 /* Limit to spin until declaring failure/deadlock */
/* debug spinlock */
typedef struct {
  spinlock_t       lock;
  /* In case we fail in acquiring this lock, we must have a (CPU) private state context */
  spinlock_level_t level[NR_CPUS];
  unsigned long    flags[NR_CPUS];
} MOSAL_spinlock_t;

static inline MT_bool debug_spinlock_lock(spinlock_t *lock_p)
{
  unsigned long i;
  for (i= MOSAL_SPINLOCK_MAX_SPIN; i; i--) {
    if (spin_trylock(lock_p))  return TRUE;
  }
  MTL_ERROR1("MOSAL::spinlock: Failed acquiring spinlock (cpuid=%u, pid=%u, lock_p=0x%p)"
             " after spinning for %u times !\n",
             smp_processor_id(),in_interrupt() ? 0 : current->pid, lock_p, MOSAL_SPINLOCK_MAX_SPIN);
  show_stack(0);
  return FALSE;
}

static inline call_result_t MOSAL_spinlock_lock(MOSAL_spinlock_t *sp)
{
  if (!debug_spinlock_lock(&sp->lock))  {
    sp->level[smp_processor_id()] = SPL_DEADLOCKED;
    return MT_ERROR;
  }
  sp->level[smp_processor_id()] = SPL_NORMAL;
  return MT_OK;
}


static inline call_result_t MOSAL_spinlock_dpc_lock(MOSAL_spinlock_t *sp)
{
  local_bh_disable(); 
  if (!debug_spinlock_lock(&sp->lock)) {
    local_bh_enable();
    sp->level[smp_processor_id()] = SPL_DEADLOCKED;
    return MT_ERROR;
  }
  sp->level[smp_processor_id()] = SPL_DPC_SAFE;
  return MT_OK;
}


static inline call_result_t MOSAL_spinlock_irq_lock(MOSAL_spinlock_t *sp)
{
  unsigned long tmp_flags; /* must use temporary flags in order to protect previously save flags */
  
  local_irq_save(tmp_flags); 
  if (!debug_spinlock_lock(&sp->lock)) {
    local_irq_restore(tmp_flags);
    sp->level[smp_processor_id()] = SPL_DEADLOCKED;
    return MT_ERROR;
  }
  sp->flags[smp_processor_id()]= tmp_flags;
  sp->level[smp_processor_id()] = SPL_IRQ_SAFE;
  return MT_OK;
}

static inline void MOSAL_spinlock_unlock(MOSAL_spinlock_t *sp)
{

  switch ( sp->level[smp_processor_id()] ) {
    case SPL_NORMAL:
      spin_unlock(&sp->lock);
      break;

    case SPL_DPC_SAFE:
      spin_unlock_bh(&sp->lock);
      break;

    case SPL_IRQ_SAFE:
      spin_unlock_irqrestore(&sp->lock, sp->flags[smp_processor_id()]);
      break;

    case SPL_DEADLOCKED:
      MTL_ERROR1("MOSAL_spinlock_unlock: Invoked for deadlocked lock (cpuid=%d, pid=%u, lock_p=0x%p)\n",
                  smp_processor_id(), in_interrupt() ? 0 : current->pid,
                  &sp->lock);
      show_stack(0);
      break;
    
    default:
      /* this realy should not happen */
      MTL_ERROR1(MT_FLFMT("corrupt spinlock (cpuid=%d, pid=%u, lock_p=0x%p, level=%d)"),
        smp_processor_id(), in_interrupt() ? 0 : current->pid, 
        &sp->lock, sp->level[smp_processor_id()]);
      show_stack(0);
  }
}
#endif /* MOSAL_SPINLOCK_DEBUG */


#else /* __KERNEL__ */
#include <semaphore.h>
#include <pthread.h>

/* user mode */

/* sync object - TBD */
struct MOSAL_syncobj {
  pthread_cond_t cond;
  pthread_mutex_t mutex;
};

/* semaphore - TBD */
struct MOSAL_semaphore {
  sem_t sem;
};

/* mutex object */
#define MOSAL_mutex_t pthread_mutex_t

/* spinlock */
typedef struct {
  volatile unsigned int lock;
}
MOSAL_spinlock_t;

#ifdef __i386__
  #define UL_SPIN_LOCK_UNLOCKED (MOSAL_spinlock_t){1}
#elif defined powerpc
  #define UL_SPIN_LOCK_UNLOCKED (MOSAL_spinlock_t){0}
#elif defined __ia64
  #define UL_SPIN_LOCK_UNLOCKED (MOSAL_spinlock_t){0}
#elif defined __x86_64__
  #define UL_SPIN_LOCK_UNLOCKED (MOSAL_spinlock_t){1}
#else
  #error UL_SPIN_LOCK_UNLOCKED not defined for this architecture
#endif


#define ul_spin_lock_init(x)	do { *(x) = UL_SPIN_LOCK_UNLOCKED; } while(0)

#define MOSAL_UL_SPINLOCK_STATIC_INIT  UL_SPIN_LOCK_UNLOCKED

static inline call_result_t priv_spinlock_lock(MOSAL_spinlock_t *sp)
{
#if defined(__i386__)
  __asm__ __volatile__(
                      "\n1:\t" "lock ; decb %0\n\t"
                      "js 2f\n" ".section .text.lock,\"ax\"\n"
                      "2:\t" "cmpb $0,%0\n\t" "rep;nop\n\t" "jle 2b\n\t"
                      "jmp 1b\n"
                      ".previous"
                      :"=m" (sp->lock) : : "memory");
  return MT_OK;
#elif defined powerpc
  unsigned long tmp;
  __asm__ __volatile__(
                      "b      1f                      # spin_lock\n\
2:      lwzx    %0,0,%1\n\
	cmpwi   0,%0,0\n\
	bne+    2b\n\
1:      lwarx   %0,0,%1\n\
	cmpwi   0,%0,0\n\
	bne-    2b\n\
	stwcx.  %2,0,%1\n\
	bne-    2b\n\
	isync"
	: "=&r"(tmp)
	: "r"(&sp->lock), "r"(1)
	: "cr0", "memory");
   return MT_OK;
#elif defined __ia64__
   register char *addr __asm__ ("r31") = (char *) &sp->lock;
   
   __asm__ __volatile__ (
		"mov r30=1\n"
		"mov ar.ccv=r0\n"
		";;\n"
		"cmpxchg4.acq r30=[%0],r30,ar.ccv\n"
		";;\n"
		"cmp.ne p15,p0=r30,r0\n"
		"(p15) br.cond.spnt.few 2f\n"
		";;\n"
                "br 1f\n"
		";;\n"
                "2:\n"
                "mov r30=ar.itc\n"
                ";;\n"
                "and r28=0x3f,r30\n"
                ";;\n"
                "3:\n"
                "add r29=r30,r28\n"
                "shl r28=r28,1\n"
                ";;\n"
                "dep r28=r28,r0,0,13\n"	/*  limit delay to 8192 cycles */
                ";;\n"
                /*  delay a little... */
                "4:\n"
                "sub r30=r30,r29\n"
                "or r28=0xf,r28\n"	/*  make sure delay is non-zero (otherwise we get stuck with 0) */
                ";;\n"
                "cmp.lt p15,p0=r30,r0\n"
                "mov r30=ar.itc\n"
                "(p15)	br.cond.sptk 4b\n"
                ";;\n"
                "ld4 r30=[r31]\n"
                ";;\n"
                "cmp.ne p15,p0=r30,r0\n"
                "mov r30=ar.itc\n"
                "(p15)	br.cond.sptk 3b\n"	/*  lock is still busy */
                ";;\n"
                /*  try acquiring lock (we know ar.ccv is still zero!): */
                "mov r30=1\n"
                ";;\n"
                "cmpxchg4.acq r30=[r31],r30,ar.ccv\n"
                ";;\n"
                "cmp.eq p15,p0=r30,r0\n"
                "mov r30=ar.itc\n"
                "(p15)	br.cond.sptk.few 1f\n"	/*  got lock -> return */
                "br 3b\n"		/*  still no luck, retry */
		";;\n"
		"1:\n"				/* force a new bundle */

		:: "r"(addr)
		: "ar.ccv", "ar.pfs", "b7", "p15", "r28", "r29", "r30", "memory");
   return MT_OK;
#elif defined(__x86_64__)
   __asm__ __volatile__(
           "\n1:\t" "lock ; decb %0\n\t" "js 2f\n" ".subsection 1\n\t" "" ".ifndef " ".text.lock." "KBUILD_BASENAME" "\n\t" ".text.lock." "KBUILD_BASENAME" ":\n\t" ".endif\n\t" "2:\t" "cmpb $0,%0\n\t" "rep;nop\n\t" "jle 2b\n\t" "jmp 1b\n" ".previous\n\t"
           :"=m" (sp->lock) : : "memory");
   return MT_OK;
#else
  #error User-level spinlock_lock function not defined for this architecture
#endif
}

/*
 *  MOSAL_spinlock_lock
 */
static inline call_result_t MOSAL_spinlock_lock(MOSAL_spinlock_t *sp)
{
  return priv_spinlock_lock(sp);
}



/*
 *  MOSAL_spinlock_dpc_lock
 */
static inline call_result_t MOSAL_spinlock_dpc_lock(MOSAL_spinlock_t *sp)
{
  return priv_spinlock_lock(sp);
}



/*
 *  MOSAL_spinlock_irq_lock
 */
static inline call_result_t MOSAL_spinlock_irq_lock(MOSAL_spinlock_t *sp)
{
  return priv_spinlock_lock(sp);
}



/*
 *  MOSAL_spinlock_unlock
 */
static inline void MOSAL_spinlock_unlock(MOSAL_spinlock_t *sp)
{
#if defined(__i386__)
  __asm__ __volatile__(
                      "movb $1,%0"
                      :"=m" (sp->lock) : : "memory");
#elif defined powerpc
  __asm__ __volatile__("eieio             # spin_unlock": : :"memory");
  sp->lock = 0;
#elif defined __ia64__
  __asm__ __volatile__("": : :"memory");
  sp->lock = 0;
#elif defined(__x86_64__)
  __asm__ __volatile__(
          "movb $1,%0"
          :"=m" (sp->lock) : : "memory");
#else  
  #error funcion not defined for this architecture
#endif
}

#endif /* __KERNEL__ */

typedef struct MOSAL_syncobj MOSAL_syncobj_t;
typedef struct MOSAL_semaphore MOSAL_semaphore_t;

/* "free" functions are dummy for both kernel and user space under linux*/
static inline call_result_t MOSAL_syncobj_free(MOSAL_syncobj_t *obj_p)
{
    return MT_OK;
}

static inline call_result_t MOSAL_sem_free(MOSAL_semaphore_t *sem_p)
{
    return MT_OK;
}

static inline call_result_t MOSAL_mutex_free(MOSAL_mutex_t *mtx_p)
{
    return MT_OK;
}


#define MOSAL_SYNC_TIMEOUT_INFINITE 0

#endif /* H_MOSAL_SYNC_IMP_H */
