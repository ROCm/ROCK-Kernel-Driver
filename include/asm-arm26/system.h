#ifndef __ASM_ARM_SYSTEM_H
#define __ASM_ARM_SYSTEM_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/kernel.h>
#include <asm/proc-fns.h>

#define vectors_base()  (0)

static inline unsigned long __xchg(unsigned long x, volatile void *ptr, int size)
{
        extern void __bad_xchg(volatile void *, int);

        switch (size) {
                case 1: return cpu_xchg_1(x, ptr);
                case 4: return cpu_xchg_4(x, ptr);
                default: __bad_xchg(ptr, size);
        }
        return 0;
}

/*
 * We need to turn the caches off before calling the reset vector - RiscOS
 * messes up if we don't
 */
#define proc_hard_reset()       cpu_proc_fin()

/*
 * A couple of speedups for the ARM
 */

/*
 * Enable IRQs  (sti)
 */
#define local_irq_enable()                                      \
        do {                                    \
          unsigned long temp;                   \
          __asm__ __volatile__(                 \
"       mov     %0, pc          @ sti\n"        \
"       bic     %0, %0, #0x08000000\n"          \
"       teqp    %0, #0\n"                       \
          : "=r" (temp)                         \
          :                                     \
          : "memory");                          \
        } while(0)

/*
 * Disable IRQs (cli)
 */
#define local_irq_disable()                                     \
        do {                                    \
          unsigned long temp;                   \
          __asm__ __volatile__(                 \
"       mov     %0, pc          @ cli\n"        \
"       orr     %0, %0, #0x08000000\n"          \
"       teqp    %0, #0\n"                       \
          : "=r" (temp)                         \
          :                                     \
          : "memory");                          \
        } while(0)

/* Disable FIQs  (clf) */

#define __clf() do {                            \
        unsigned long temp;                     \
        __asm__ __volatile__(                   \
"       mov     %0, pc          @ clf\n"        \
"       orr     %0, %0, #0x04000000\n"          \
"       teqp    %0, #0\n"                       \
        : "=r" (temp));                         \
    } while(0)

/* Enable FIQs (stf) */

#define __stf() do {                            \
        unsigned long temp;                     \
        __asm__ __volatile__(                   \
"       mov     %0, pc          @ stf\n"        \
"       bic     %0, %0, #0x04000000\n"          \
"       teqp    %0, #0\n"                       \
        : "=r" (temp));                         \
    } while(0)

/*
 * save current IRQ & FIQ state
 */
#define local_save_flags(x)                             \
        do {                                    \
          __asm__ __volatile__(                 \
"       mov     %0, pc          @ save_flags\n" \
"       and     %0, %0, #0x0c000000\n"          \
          : "=r" (x));                          \
        } while (0)

/*
 * Save the current interrupt enable state & disable IRQs
 */
#define local_irq_save(x)                               \
        do {                                            \
          unsigned long temp;                           \
          __asm__ __volatile__(                         \
"       mov     %0, pc          @ save_flags_cli\n"     \
"       orr     %1, %0, #0x08000000\n"                  \
"       and     %0, %0, #0x0c000000\n"                  \
"       teqp    %1, #0\n"                               \
          : "=r" (x), "=r" (temp)                       \
          :                                             \
          : "memory");                                  \
        } while (0)

/*
 * restore saved IRQ & FIQ state
 */
#define local_irq_restore(x)                            \
        do {                                            \
          unsigned long temp;                           \
          __asm__ __volatile__(                         \
"       mov     %0, pc          @ restore_flags\n"      \
"       bic     %0, %0, #0x0c000000\n"                  \
"       orr     %0, %0, %1\n"                           \
"       teqp    %0, #0\n"                               \
          : "=&r" (temp)                                \
          : "r" (x)                                     \
          : "memory");                                  \
        } while (0)


struct thread_info;

/* information about the system we're running on */
extern unsigned int system_rev;
extern unsigned int system_serial_low;
extern unsigned int system_serial_high;

struct pt_regs;

void die(const char *msg, struct pt_regs *regs, int err)
		__attribute__((noreturn));

void die_if_kernel(const char *str, struct pt_regs *regs, int err);

void hook_fault_code(int nr, int (*fn)(unsigned long, unsigned int,
				       struct pt_regs *),
		     int sig, const char *name);

#define xchg(ptr,x) \
	((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

#define tas(ptr) (xchg((ptr),1))

extern asmlinkage void __backtrace(void);

/*
 * Include processor dependent parts
 */

#define mb() __asm__ __volatile__ ("" : : : "memory")
#define rmb() mb()
#define wmb() mb()
#define nop() __asm__ __volatile__("mov\tr0,r0\t@ nop\n\t");

#define prepare_to_switch()    do { } while(0)

/*
 * switch_to(prev, next) should switch from task `prev' to `next'
 * `prev' will never be the same as `next'.
 * The `mb' is to tell GCC not to cache `current' across this call.
 */
struct thread_info;

extern struct task_struct *__switch_to(struct thread_info *, struct thread_info *);

#define switch_to(prev,next,last)                                       \
        do {                                                            \
                __switch_to(prev->thread_info,next->thread_info);       \
                mb();                                                   \
        } while (0)


#ifdef CONFIG_SMP
#error SMP not supported
#endif /* CONFIG_SMP */

#define irqs_disabled()                 \
({                                      \
        unsigned long flags;            \
        local_save_flags(flags);        \
        flags & PSR_I_BIT;              \
})

#define set_mb(var, value)  do { var = value; mb(); } while (0)
#define smp_mb()		barrier()
#define smp_rmb()		barrier()
#define smp_wmb()		barrier()
#define smp_read_barrier_depends()              do { } while(0)

#define clf()			__clf()
#define stf()			__stf()

#endif /* __KERNEL__ */

#endif
