#ifndef __ARCH_H8300_ATOMIC__
#define __ARCH_H8300_ATOMIC__

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

typedef struct { int counter; } atomic_t;
#define ATOMIC_INIT(i)	{ (i) }

#define atomic_read(v)		((v)->counter)
#define atomic_set(v, i)	(((v)->counter) = i)

#include <asm/system.h>
#include <linux/kernel.h>

static __inline__ int atomic_add_return(int i, atomic_t *v)
{
	int ret,flags;
	local_irq_save(flags);
	ret = v->counter += i;
	local_irq_restore(flags);
	return ret;
}

#define atomic_add(i, v) atomic_add_return(i, v)

static __inline__ int atomic_sub_return(int i, atomic_t *v)
{
	int ret,flags;
	local_irq_save(flags);
	ret = v->counter -= i;
	local_irq_restore(flags);
	return ret;
}

#define atomic_sub(i, v) atomic_sub_return(i, v)

static __inline__ int atomic_inc_return(atomic_t *v)
{
	int ret,flags;
	local_irq_save(flags);
	v->counter++;
	ret = v->counter;
	local_irq_restore(flags);
	return ret;
}

#define atomic_inc(v) atomic_inc_return(v)

static __inline__ int atomic_dec_return(atomic_t *v)
{
	int ret,flags;
	local_irq_save(flags);
	--v->counter;
	ret = v->counter;
	local_irq_restore(flags);
	return ret;
}

#define atomic_dec(v) atomic_dec_return(v)

static __inline__ int atomic_dec_and_test(atomic_t *v)
{
	int ret,flags;
	local_irq_save(flags);
	--v->counter;
	ret = v->counter;
	local_irq_restore(flags);
	return ret == 0;
}

static __inline__ void atomic_clear_mask(unsigned long mask, unsigned long *v)
{
	__asm__ __volatile__("stc ccr,r2l\n\t"
	                     "orc #0x80,ccr\n\t"
	                     "mov.l %0,er0\n\t"
	                     "mov.l %1,er1\n\t"
	                     "and.l er1,er0\n\t"
	                     "mov.l er0,%0\n\t"
	                     "ldc r2l,ccr" 
                             : "=m" (*v) : "ir" (~(mask)) :"er0","er1","er2");
}

static __inline__ void atomic_set_mask(unsigned long mask, unsigned long *v)
{
	__asm__ __volatile__("stc ccr,r2l\n\t"
	                     "orc #0x80,ccr\n\t"
	                     "mov.l %0,er0\n\t"
	                     "mov.l %1,er1\n\t"
	                     "or.l er1,er0\n\t"
	                     "mov.l er0,%0\n\t"
	                     "ldc r2l,ccr" 
                             : "=m" (*v) : "ir" (mask) :"er0","er1","er2");
}

/* Atomic operations are already serializing */
#define smp_mb__before_atomic_dec()    barrier()
#define smp_mb__after_atomic_dec() barrier()
#define smp_mb__before_atomic_inc()    barrier()
#define smp_mb__after_atomic_inc() barrier()

#define atomic_sub_and_test(i,v) (atomic_sub_return((i), (v)) == 0)
#define atomic_dec_and_test(v) (atomic_sub_return(1, (v)) == 0)

#endif /* __ARCH_H8300_ATOMIC __ */
