#ifndef __ASM_SYSTEM_H
#define __ASM_SYSTEM_H

#include <linux/config.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#ifdef __KERNEL__

#ifdef CONFIG_SMP
#define LOCK_PREFIX "lock ; "
#else
#define LOCK_PREFIX ""
#endif

#define __STR(x) #x
#define STR(x) __STR(x)

#define __SAVE(reg,offset) "movq %%" #reg ",(14-" #offset ")*8(%%rsp)\n\t"
#define __RESTORE(reg,offset) "movq (14-" #offset ")*8(%%rsp),%%" #reg "\n\t"

#ifdef CONFIG_X86_REMOTE_DEBUG

/* full frame for the debug stub */
/* Should be replaced with a dwarf2 cie/fde description, then gdb could
   figure it out all by itself. */
struct save_context_frame { 
	unsigned long rbp; 
	unsigned long rbx;
	unsigned long r11;
	unsigned long r10;
	unsigned long r9;
	unsigned long r8;
	unsigned long rcx;
	unsigned long rdx;	
	unsigned long r15;
	unsigned long r14;
	unsigned long r13;
	unsigned long r12;
	unsigned long rdi;
	unsigned long rsi;
	unsigned long flags;
}; 

#define SAVE_CONTEXT \
	"pushfq\n\t"							\
	"subq $14*8,%%rsp\n\t" 						\
	__SAVE(rbx, 12) __SAVE(rdi,  1)					\
	__SAVE(rdx,  6) __SAVE(rcx,  7)					\
	__SAVE(r8,   8) __SAVE(r9,   9)					\
	__SAVE(r12,  2) __SAVE(r13,  3)					\
	__SAVE(r14,  4) __SAVE(r15,  5)					\
	__SAVE(r10, 10) __SAVE(r11, 11)					\
	__SAVE(rsi, 0)  __SAVE(rbp, 13) 				\


#define RESTORE_CONTEXT \
	__RESTORE(rbx, 12) __RESTORE(rdi,  1) 					\
	__RESTORE(rdx,  6) __RESTORE(rcx,  7)					\
	__RESTORE(r12,  2) __RESTORE(r13,  3)					\
	__RESTORE(r14,  4) __RESTORE(r15,  5)					\
	__RESTORE(r10, 10) __RESTORE(r11, 11)					\
	__RESTORE(r8,   8) __RESTORE(r9,   9)					\
	__RESTORE(rbp, 13) __RESTORE(rsi, 0) 		   		        \
	"addq $14*8,%%rsp\n\t" 							\
	"popfq\n\t"

#define __EXTRA_CLOBBER 

#else
/* frame pointer must be last for get_wchan */
#define SAVE_CONTEXT    "pushfq ; pushq %%rbp ; movq %%rsi,%%rbp\n\t"
#define RESTORE_CONTEXT "movq %%rbp,%%rsi ; popq %%rbp ; popfq\n\t" 

#define __EXTRA_CLOBBER  \
	,"rcx","rbx","rdx","r8","r9","r10","r11","r12","r13","r14","r15"
#endif

#define switch_to(prev,next,last) \
	asm volatile(SAVE_CONTEXT						    \
		     "movq %%rsp,%P[threadrsp](%[prev])\n\t" /* save RSP */	  \
		     "movq %P[threadrsp](%[next]),%%rsp\n\t" /* restore RSP */	  \
		     "call __switch_to\n\t"					  \
		     ".globl thread_return\n"					\
		     "thread_return:\n\t"					    \
		     "movq %%gs:%P[pda_pcurrent],%%rsi\n\t"			  \
		     "movq %P[thread_info](%%rsi),%%r8\n\t"			  \
		     "btr  %[tif_fork],%P[ti_flags](%%r8)\n\t"			  \
		     "movq %%rax,%%rdi\n\t" 					  \
		     "jc   ret_from_fork\n\t"					  \
		     RESTORE_CONTEXT						    \
		     : "=a" (last)					  	  \
		     : [next] "S" (next), [prev] "D" (prev),			  \
		       [threadrsp] "i" (offsetof(struct task_struct, thread.rsp)), \
		       [ti_flags] "i" (offsetof(struct thread_info, flags)),\
		       [tif_fork] "i" (TIF_FORK),			  \
		       [thread_info] "i" (offsetof(struct task_struct, thread_info)), \
		       [pda_pcurrent] "i" (offsetof(struct x8664_pda, pcurrent))   \
		     : "memory", "cc" __EXTRA_CLOBBER)
    
extern void load_gs_index(unsigned); 

/*
 * Load a segment. Fall back on loading the zero
 * segment if something goes wrong..
 */
#define loadsegment(seg,value)	\
	asm volatile("\n"			\
		"1:\t"				\
		"movl %k0,%%" #seg "\n"		\
		"2:\n"				\
		".section .fixup,\"ax\"\n"	\
		"3:\t"				\
		"movl %1,%%" #seg "\n\t" 	\
		"jmp 2b\n"			\
		".previous\n"			\
		".section __ex_table,\"a\"\n\t"	\
		".align 8\n\t"			\
		".quad 1b,3b\n"			\
		".previous"			\
		: :"r" (value), "r" (0))

#define set_debug(value,register) \
                __asm__("movq %0,%%db" #register  \
		: /* no output */ \
		:"r" ((unsigned long) value))


#ifdef __KERNEL__
struct alt_instr { 
	__u8 *instr; 		/* original instruction */
	__u8 *replacement;
	__u8  cpuid;		/* cpuid bit set for replacement */
	__u8  instrlen;		/* length of original instruction */
	__u8  replacementlen; 	/* length of new instruction, <= instrlen */ 
	__u8  pad[5];
}; 
#endif

/*
 * Clear and set 'TS' bit respectively
 */
#define clts() __asm__ __volatile__ ("clts")

static inline unsigned long read_cr0(void)
{ 
	unsigned long cr0;
	asm volatile("movq %%cr0,%0" : "=r" (cr0));
	return cr0;
} 

static inline void write_cr0(unsigned long val) 
{ 
	asm volatile("movq %0,%%cr0" :: "r" (val));
} 

static inline unsigned long read_cr3(void)
{ 
	unsigned long cr3;
	asm("movq %%cr3,%0" : "=r" (cr3));
	return cr3;
} 

static inline unsigned long read_cr4(void)
{ 
	unsigned long cr4;
	asm("movq %%cr4,%0" : "=r" (cr4));
	return cr4;
} 

static inline void write_cr4(unsigned long val)
{ 
	asm volatile("movq %0,%%cr4" :: "r" (val));
} 

#define stts() write_cr0(8 | read_cr0())

#define wbinvd() \
	__asm__ __volatile__ ("wbinvd": : :"memory");

#endif	/* __KERNEL__ */

#define nop() __asm__ __volatile__ ("nop")

#define xchg(ptr,v) ((__typeof__(*(ptr)))__xchg((unsigned long)(v),(ptr),sizeof(*(ptr))))

#define tas(ptr) (xchg((ptr),1))

#define __xg(x) ((volatile long *)(x))

extern inline void set_64bit(volatile unsigned long *ptr, unsigned long val)
{
	*ptr = val;
}

#define _set_64bit set_64bit

/*
 * Note: no "lock" prefix even on SMP: xchg always implies lock anyway
 * Note 2: xchg has side effect, so that attribute volatile is necessary,
 *	  but generally the primitive is invalid, *ptr is output argument. --ANK
 */
static inline unsigned long __xchg(unsigned long x, volatile void * ptr, int size)
{
	switch (size) {
		case 1:
			__asm__ __volatile__("xchgb %b0,%1"
				:"=q" (x)
				:"m" (*__xg(ptr)), "0" (x)
				:"memory");
			break;
		case 2:
			__asm__ __volatile__("xchgw %w0,%1"
				:"=r" (x)
				:"m" (*__xg(ptr)), "0" (x)
				:"memory");
			break;
		case 4:
			__asm__ __volatile__("xchgl %k0,%1"
				:"=r" (x)
				:"m" (*__xg(ptr)), "0" (x)
				:"memory");
			break;
		case 8:
			__asm__ __volatile__("xchgq %0,%1"
				:"=r" (x)
				:"m" (*__xg(ptr)), "0" (x)
				:"memory");
			break;
	}
	return x;
}

/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */

#define __HAVE_ARCH_CMPXCHG 1

static inline unsigned long __cmpxchg(volatile void *ptr, unsigned long old,
				      unsigned long new, int size)
{
	unsigned long prev;
	switch (size) {
	case 1:
		__asm__ __volatile__(LOCK_PREFIX "cmpxchgb %b1,%2"
				     : "=a"(prev)
				     : "q"(new), "m"(*__xg(ptr)), "0"(old)
				     : "memory");
		return prev;
	case 2:
		__asm__ __volatile__(LOCK_PREFIX "cmpxchgw %w1,%2"
				     : "=a"(prev)
				     : "q"(new), "m"(*__xg(ptr)), "0"(old)
				     : "memory");
		return prev;
	case 4:
		__asm__ __volatile__(LOCK_PREFIX "cmpxchgl %k1,%2"
				     : "=a"(prev)
				     : "q"(new), "m"(*__xg(ptr)), "0"(old)
				     : "memory");
		return prev;
	case 8:
		__asm__ __volatile__(LOCK_PREFIX "cmpxchgq %1,%2"
				     : "=a"(prev)
				     : "q"(new), "m"(*__xg(ptr)), "0"(old)
				     : "memory");
		return prev;
	}
	return old;
}

#define cmpxchg(ptr,o,n)\
	((__typeof__(*(ptr)))__cmpxchg((ptr),(unsigned long)(o),\
					(unsigned long)(n),sizeof(*(ptr))))

#ifdef CONFIG_SMP
#define smp_mb()	mb()
#define smp_rmb()	rmb()
#define smp_wmb()	wmb()
#define smp_read_barrier_depends()	do {} while(0)
#else
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define smp_read_barrier_depends()	do {} while(0)
#endif

    
/*
 * Force strict CPU ordering.
 * And yes, this is required on UP too when we're talking
 * to devices.
 */
#define mb() 	asm volatile("mfence":::"memory")
#define rmb()	asm volatile("lfence":::"memory")

/* could use SFENCE here, but it would be only needed for unordered SSE
   store instructions and we always do an explicit sfence with them currently.
   the ordering of normal stores is serialized enough. Just make it a compile
   barrier. */
#define wmb()	asm volatile("" ::: "memory")
#define read_barrier_depends()	do {} while(0)
#define set_mb(var, value) do { xchg(&var, value); } while (0)
#define set_wmb(var, value) do { var = value; wmb(); } while (0)

#define warn_if_not_ulong(x) do { unsigned long foo; (void) (&(x) == &foo); } while (0)

/* interrupt control.. */
#define local_save_flags(x)	do { warn_if_not_ulong(x); __asm__ __volatile__("# save_flags \n\t pushfq ; popq %q0":"=g" (x): /* no input */ :"memory"); } while (0)
#define local_irq_restore(x) 	__asm__ __volatile__("# restore_flags \n\t pushq %0 ; popfq": /* no output */ :"g" (x):"memory", "cc")
#define local_irq_disable() 	__asm__ __volatile__("cli": : :"memory")
#define local_irq_enable()	__asm__ __volatile__("sti": : :"memory")
/* used in the idle loop; sti takes one instruction cycle to complete */
#define safe_halt()		__asm__ __volatile__("sti; hlt": : :"memory")
#define irqs_disabled()			\
({					\
	unsigned long flags;		\
	local_save_flags(flags);	\
	!(flags & (1<<9));		\
})

/* For spinlocks etc */
#define local_irq_save(x) 	do { warn_if_not_ulong(x); __asm__ __volatile__("# local_irq_save \n\t pushfq ; popq %0 ; cli":"=g" (x): /* no input */ :"memory"); } while (0)

/*
 * disable hlt during certain critical i/o operations
 */
#define HAVE_DISABLE_HLT
void disable_hlt(void);
void enable_hlt(void);

#define HAVE_EAT_KEY
void eat_key(void);

#endif
