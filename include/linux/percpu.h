#ifndef __LINUX_PERCPU_H
#define __LINUX_PERCPU_H
#include <linux/spinlock.h> /* For preempt_disable() */
#include <linux/slab.h> /* For kmalloc() */
#include <linux/smp.h>
#include <linux/string.h> /* For memset() */
#include <asm/percpu.h>

/* Enough to cover all DEFINE_PER_CPUs in kernel, including modules. */
#ifndef PERCPU_ENOUGH_ROOM
#define PERCPU_ENOUGH_ROOM 32768
#endif

/* Must be an lvalue. */
#define get_cpu_var(var) (*({ preempt_disable(); &__get_cpu_var(var); }))
#define put_cpu_var(var) preempt_enable()

#ifdef CONFIG_SMP

struct percpu_data {
	void *ptrs[NR_CPUS];
	void *blkp;
};

/* 
 * Use this to get to a cpu's version of the per-cpu object allocated using
 * alloc_percpu.  If you want to get "this cpu's version", maybe you want
 * to use get_cpu_ptr... 
 */ 
#define per_cpu_ptr(ptr, cpu)                   \
({                                              \
        struct percpu_data *__p = (struct percpu_data *)~(unsigned long)(ptr); \
        (__typeof__(ptr))__p->ptrs[(cpu)];	\
})

extern void *__alloc_percpu(size_t size, size_t align);
extern void free_percpu(const void *);
extern void kmalloc_percpu_init(void);

#else /* CONFIG_SMP */

#define per_cpu_ptr(ptr, cpu) (ptr)

static inline void *__alloc_percpu(size_t size, size_t align)
{
	void *ret = kmalloc(size, GFP_KERNEL);
	if (ret)
		memset(ret, 0, size);
	return ret;
}
static inline void free_percpu(const void *ptr)
{	
	kfree(ptr);
}
static inline void kmalloc_percpu_init(void) { }

#endif /* CONFIG_SMP */

/* Simple wrapper for the common case: zeros memory. */
#define alloc_percpu(type) \
	((type *)(__alloc_percpu(sizeof(type), __alignof__(type))))

/* 
 * Use these with alloc_percpu. If
 * 1. You want to operate on memory allocated by alloc_percpu (dereference
 *    and read/modify/write)  AND 
 * 2. You want "this cpu's version" of the object AND 
 * 3. You want to do this safely since:
 *    a. On multiprocessors, you don't want to switch between cpus after 
 *    you've read the current processor id due to preemption -- this would 
 *    take away the implicit  advantage to not have any kind of traditional 
 *    serialization for per-cpu data
 *    b. On uniprocessors, you don't want another kernel thread messing
 *    up with the same per-cpu data due to preemption
 *    
 * So, Use get_cpu_ptr to disable preemption and get pointer to the 
 * local cpu version of the per-cpu object. Use put_cpu_ptr to enable
 * preemption.  Operations on per-cpu data between get_ and put_ is
 * then considered to be safe. And ofcourse, "Thou shalt not sleep between 
 * get_cpu_ptr and put_cpu_ptr"
 */
#define get_cpu_ptr(ptr) per_cpu_ptr(ptr, get_cpu())
#define put_cpu_ptr(ptr) put_cpu()

#endif /* __LINUX_PERCPU_H */
