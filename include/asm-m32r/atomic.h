#ifndef _ASM_M32R_ATOMIC_H
#define _ASM_M32R_ATOMIC_H

/*
 *  linux/include/asm-m32r/atomic.h
 *
 *  M32R version:
 *    Copyright (C) 2001, 2002  Hitoshi Yamamoto
 *    Copyright (C) 2004  Hirokazu Takata <takata at linux-m32r.org>
 */

#include <linux/config.h>
#include <asm/system.h>

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

#undef LOAD
#undef STORE
#ifdef CONFIG_SMP
#define LOAD	"lock"
#define STORE	"unlock"
#else
#define LOAD	"ld"
#define STORE	"st"
#endif

/*
 * Make sure gcc doesn't try to be clever and move things around
 * on us. We need to use _exactly_ the address the user gave us,
 * not some alias that contains the same information.
 */
typedef struct { volatile int counter; } atomic_t;

#define ATOMIC_INIT(i)	{ (i) }

/**
 * atomic_read - read atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically reads the value of @v.
 */
#define atomic_read(v)	((v)->counter)

/**
 * atomic_set - set atomic variable
 * @v: pointer of type atomic_t
 * @i: required value
 *
 * Atomically sets the value of @v to @i.
 */
#define atomic_set(v,i)	(((v)->counter) = (i))

/**
 * atomic_add - add integer to atomic variable
 * @i: integer value to add
 * @v: pointer of type atomic_t
 *
 * Atomically adds @i to @v.
 */
static inline void atomic_add(int i, atomic_t *v)
{
	unsigned long flags;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# atomic_add			\n\t"
		DCACHE_CLEAR("r4", "r5", "%0")
		LOAD"	r4, @%0;		\n\t"
		"add	r4, %1;			\n\t"
		STORE"	r4, @%0;		\n\t"
		: /* no outputs */
		: "r" (&v->counter), "r" (i)
		: "memory", "r4"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r5"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);
}

/**
 * atomic_sub - subtract the atomic variable
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically subtracts @i from @v.
 */
static inline void atomic_sub(int i, atomic_t *v)
{
	unsigned long flags;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# atomic_sub			\n\t"
		DCACHE_CLEAR("r4", "r5", "%0")
		LOAD"	r4, @%0;		\n\t"
		"sub	r4, %1;			\n\t"
		STORE"	r4, @%0;		\n\t"
		: /* no outputs */
		: "r" (&v->counter), "r" (i)
		: "memory", "r4"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r5"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);
}

/**
 * atomic_add_return - add integer to atomic variable and return
 * @i: integer value to add
 * @v: pointer of type atomic_t
 *
 * Atomically adds @i to @v and return (@i + @v).
 */
static inline int atomic_add_return(int i, atomic_t *v)
{
	unsigned long flags;
	int result;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# atomic_add			\n\t"
		DCACHE_CLEAR("%0", "r4", "%1")
		LOAD"	%0, @%1;		\n\t"
		"add	%0, %2;			\n\t"
		STORE"	%0, @%1;		\n\t"
		: "=&r" (result)
		: "r" (&v->counter), "r" (i)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r4"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);

	return result;
}

/**
 * atomic_sub_return - subtract the atomic variable and return
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically subtracts @i from @v and return (@v - @i).
 */
static inline int atomic_sub_return(int i, atomic_t *v)
{
	unsigned long flags;
	int result;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# atomic_sub			\n\t"
		DCACHE_CLEAR("%0", "r4", "%1")
		LOAD"	%0, @%1;		\n\t"
		"sub	%0, %2;			\n\t"
		STORE"	%0, @%1;		\n\t"
		: "=&r" (result)
		: "r" (&v->counter), "r" (i)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r4"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);

	return result;
}

/**
 * atomic_sub_and_test - subtract value from variable and test result
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically subtracts @i from @v and returns
 * true if the result is zero, or false for all
 * other cases.
 */
#define atomic_sub_and_test(i,v) (atomic_sub_return((i), (v)) == 0)

/**
 * atomic_inc - increment atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1.
 */
static inline void atomic_inc(atomic_t *v)
{
	unsigned long flags;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# atomic_inc			\n\t"
		DCACHE_CLEAR("r4", "r5", "%0")
		LOAD"	r4, @%0; 		\n\t"
		"addi	r4, #1;			\n\t"
		STORE"	r4, @%0;		\n\t"
		: /* no outputs */
		: "r" (&v->counter)
		: "memory", "r4"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r5"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);
}

/**
 * atomic_dec - decrement atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1.
 */
static inline void atomic_dec(atomic_t *v)
{
	unsigned long flags;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# atomic_dec			\n\t"
		DCACHE_CLEAR("r4", "r5", "%0")
		LOAD"	r4, @%0;		\n\t"
		"addi	r4, #-1;		\n\t"
		STORE"	r4, @%0;		\n\t"
		: /* no outputs */
		: "r" (&v->counter)
		: "memory", "r4"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r5"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);
}

/**
 * atomic_inc_return - increment atomic variable and return
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1 and returns the result.
 */
static inline int atomic_inc_return(atomic_t *v)
{
	unsigned long flags;
	int result;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# atomic_dec_and_test		\n\t"
		DCACHE_CLEAR("%0", "r4", "%1")
		LOAD"	%0, @%1;		\n\t"
		"addi	%0, #1;			\n\t"
		STORE"	%0, @%1;		\n\t"
		: "=&r" (result)
		: "r" (&v->counter)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r4"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);

	return result;
}

/**
 * atomic_dec_return - decrement atomic variable and return
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1 and returns the result.
 */
static inline int atomic_dec_return(atomic_t *v)
{
	unsigned long flags;
	int result;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# atomic_dec_and_test		\n\t"
		DCACHE_CLEAR("%0", "r4", "%1")
		LOAD"	%0, @%1;		\n\t"
		"addi	%0, #-1;		\n\t"
		STORE"	%0, @%1;		\n\t"
		: "=&r" (result)
		: "r" (&v->counter)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r4"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);

	return result;
}

/**
 * atomic_inc_and_test - increment and test
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#define atomic_inc_and_test(v) (atomic_inc_return(v) == 0)

/**
 * atomic_dec_and_test - decrement and test
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1 and
 * returns true if the result is 0, or false for all
 * other cases.
 */
#define atomic_dec_and_test(v) (atomic_dec_return(v) == 0)

/**
 * atomic_add_negative - add and test if negative
 * @v: pointer of type atomic_t
 * @i: integer value to add
 *
 * Atomically adds @i to @v and returns true
 * if the result is negative, or false when
 * result is greater than or equal to zero.
 */
#define atomic_add_negative(i,v) (atomic_add_return((i), (v)) < 0)

static inline void atomic_clear_mask(unsigned long  mask, atomic_t *addr)
{
	unsigned long flags;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# atomic_set_mask		\n\t"
		DCACHE_CLEAR("r4", "r5", "%0")
		LOAD"	r4, @%0;		\n\t"
		"and	r4, %1;			\n\t"
		STORE"	r4, @%0;		\n\t"
		: /* no outputs */
		: "r" (addr), "r" (~mask)
		: "memory", "r4"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r5"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);
}

static inline void atomic_set_mask(unsigned long  mask, atomic_t *addr)
{
	unsigned long flags;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# atomic_set_mask		\n\t"
		DCACHE_CLEAR("r4", "r5", "%0")
		LOAD"	r4, @%0;		\n\t"
		"or	r4, %1;			\n\t"
		STORE"	r4, @%0;		\n\t"
		: /* no outputs */
		: "r" (addr), "r" (mask)
		: "memory", "r4"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r5"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);
}

/* Atomic operations are already serializing on m32r */
#define smp_mb__before_atomic_dec()	barrier()
#define smp_mb__after_atomic_dec()	barrier()
#define smp_mb__before_atomic_inc()	barrier()
#define smp_mb__after_atomic_inc()	barrier()

#endif	/* _ASM_M32R_ATOMIC_H */

