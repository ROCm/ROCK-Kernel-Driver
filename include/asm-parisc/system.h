#ifndef __PARISC_SYSTEM_H
#define __PARISC_SYSTEM_H

#include <linux/config.h>
#include <asm/psw.h>

/* The program status word as bitfields.  */
struct pa_psw {
	unsigned int y:1;
	unsigned int z:1;
	unsigned int rv:2;
	unsigned int w:1;
	unsigned int e:1;
	unsigned int s:1;
	unsigned int t:1;

	unsigned int h:1;
	unsigned int l:1;
	unsigned int n:1;
	unsigned int x:1;
	unsigned int b:1;
	unsigned int c:1;
	unsigned int v:1;
	unsigned int m:1;

	unsigned int cb:8;

	unsigned int o:1;
	unsigned int g:1;
	unsigned int f:1;
	unsigned int r:1;
	unsigned int q:1;
	unsigned int p:1;
	unsigned int d:1;
	unsigned int i:1;
};

#define pa_psw(task) ((struct pa_psw *) ((char *) (task) + TASK_PT_PSW))

struct task_struct;

extern struct task_struct *_switch_to(struct task_struct *, struct task_struct *);

#define prepare_to_switch()	do { } while(0)
#define switch_to(prev, next, last) do {			\
	(last) = _switch_to(prev, next);			\
} while(0)

/* borrowed this from sparc64 -- probably the SMP case is hosed for us */
#ifdef CONFIG_SMP
#define smp_mb()	mb()
#define smp_rmb()	rmb()
#define smp_wmb()	wmb()
#else
/* This is simply the barrier() macro from linux/kernel.h but when serial.c
 * uses tqueue.h uses smp_mb() defined using barrier(), linux/kernel.h
 * hasn't yet been included yet so it fails, thus repeating the macro here.
 */
#define smp_mb()	__asm__ __volatile__("":::"memory");
#define smp_rmb()	__asm__ __volatile__("":::"memory");
#define smp_wmb()	__asm__ __volatile__("":::"memory");
#endif

/* interrupt control */
#define __save_flags(x)	__asm__ __volatile__("ssm 0, %0" : "=r" (x) : : "memory")
#define __restore_flags(x) __asm__ __volatile__("mtsm %0" : : "r" (x) : "memory")
#define __cli()	__asm__ __volatile__("rsm %0,%%r0\n" : : "i" (PSW_I) : "memory" )
#define __sti()	__asm__ __volatile__("ssm %0,%%r0\n" : : "i" (PSW_I) : "memory" )

#define local_irq_save(x) \
	__asm__ __volatile__("rsm %1,%0" : "=r" (x) :"i" (PSW_I) : "memory" )
#define local_irq_restore(x) \
	__asm__ __volatile__("mtsm %0" : : "r" (x) : "memory" )
#define local_irq_disable() __cli()
#define local_irq_enable()  __sti()

#ifdef CONFIG_SMP
#else
#define cli() __cli()
#define sti() __sti()
#define save_flags(x) __save_flags(x)
#define restore_flags(x) __restore_flags(x)
#endif


#define mfctl(reg)	({		\
	unsigned long cr;		\
	__asm__ __volatile__(		\
		"mfctl " #reg ",%0" :	\
		 "=r" (cr)		\
	);				\
	cr;				\
})

#define mtctl(gr, cr) \
	__asm__ __volatile__("mtctl %0,%1" \
		: /* no outputs */ \
		: "r" (gr), "i" (cr))

/* these are here to de-mystefy the calling code, and to provide hooks */
/* which I needed for debugging EIEM problems -PB */
#define get_eiem() mfctl(15)
static inline void set_eiem(unsigned long val)
{
	mtctl(val, 15);
}

#define mfsp(reg)	({		\
	unsigned long cr;		\
	__asm__ __volatile__(		\
		"mfsp " #reg ",%0" :	\
		 "=r" (cr)		\
	);				\
	cr;				\
})

#define mtsp(gr, cr) \
	__asm__ __volatile__("mtsp %0,%1" \
		: /* no outputs */ \
		: "r" (gr), "i" (cr))


#define mb()  __asm__ __volatile__ ("sync" : : :"memory")
#define wmb() mb()

extern unsigned long __xchg(unsigned long, unsigned long *, int);

#define xchg(ptr,x) \
 (__typeof__(*(ptr)))__xchg((unsigned long)(x),(unsigned long*)(ptr),sizeof(*(ptr)))

/* LDCW, the only atomic read-write operation PA-RISC has.  Sigh. */
#define __ldcw(a) ({ \
	unsigned __ret; \
	__asm__ __volatile__("ldcw 0(%1),%0" : "=r" (__ret) : "r" (a)); \
	__ret; \
})

#ifdef CONFIG_SMP
/*
 * Your basic SMP spinlocks, allowing only a single CPU anywhere
 */

typedef struct {
	volatile unsigned int __attribute__((aligned(16))) lock;
} spinlock_t;
#endif

#endif
