#ifndef __ASM_I386_HOOK_H
#define __ASM_I386_HOOK_H
/*
 * Kernel Hooks optimized for ia32.
 * 
 * Authors: Richard J Moore <richardj_moore@uk.ibm.com>
 *	    Vamsi Krishna S. <vamsi_krishna@in.ibm.com>
 */
#include <linux/smp.h>
#include <asm/cacheflush.h>

#if defined(CONFIG_HOOK) || defined(CONFIG_HOOK_MODULE)

#define IF_HOOK_ENABLED(h, hk) _IF_HOOK_ENABLED(h, #hk)
#define _IF_HOOK_ENABLED(h, hk) \
	register int tmp; \
	__asm__ __volatile__ (".global "hk"; "hk":movl $0, %0":"=r"(tmp)); \
	if (unlikely(tmp))

#endif /* CONFIG_HOOK || CONFIG_HOOK_MODULE */

/*
 * Sanity check the hook location for expected instructions at hook location.
 * movl $0, %reg, testl %reg, %reg
 * test doesn't have to follow movl, so don't test for that.
 */
#define OPCODE_MOV1			0xb0
#define OPCODE_MOV1_MASK		0xf0
#define OPCODE_MOV2_1			0xc6 /* first byte */
#define OPCODE_MOV2_2			0xc0 /* second byte */
#define OPCODE_MOV2_1_MASK		0xfe
#define OPCODE_MOV2_2_MASK		0xf8
		
static inline int is_asm_hook(unsigned char *addr)
{
	if (!addr)
		return 0;
	if((addr[0] & OPCODE_MOV1_MASK) == OPCODE_MOV1) {
		if (*((unsigned long *)(addr+1)) == 0)
			return 1;
	} else if (((addr[0] & OPCODE_MOV2_1_MASK) == OPCODE_MOV2_1) && 
		    ((addr[1] & OPCODE_MOV2_2_MASK) == OPCODE_MOV2_2)) {
		if (*((unsigned long *)(addr+2)) == 0)
			return 1;
	}
	return 0;
}
 
#if defined(CONFIG_SMP)
/*
 * This routine loops around a memory flag and once it is set to one, syncronizes the
 * instruction cache by executing CPUID instruction.
 */ 
static void wait_for_memory_flag(atomic_t *memory_flag) 
{
	while (!atomic_read(memory_flag))
		;
	__asm__ __volatile__ ("cpuid" : : : "ax", "bx", "cx", "dx");
	return;
}

#endif
static inline void deactivate_asm_hook(struct hook *hook)
{
	unsigned char *addr = (unsigned char *) (hook->hook_addr);
/*
 * Fix for Intel Pentium and P6 family processors E49 errata (Unsyncronized 
 * cross modifying code). Send an IPI to all CPUs except self and then modify
 * the contents of hook_addr before other CPUs return from IPI.
 */
#if defined(CONFIG_SMP)
	atomic_set(&hook->hook_deactivate, 0); 
	mb(); 
	smp_call_function((void *)wait_for_memory_flag, &hook->hook_deactivate, 1, 0); 
#endif
	addr[2] = 0;
	flush_icache_range(addr + 2, addr + 2);
#if defined(CONFIG_SMP)
	atomic_set(&hook->hook_deactivate, 1); 
	mb(); 
#endif
	return;
}

static inline void activate_asm_hook(struct hook *hook)
{
	unsigned char *addr = (unsigned char *) (hook->hook_addr);
/*
 * Fix for Intel Pentium and P6 family processors E49 errata (Unsyncronized 
 * cross modifying code). Send an IPI to all CPUs except self and then modify 
 * the contents of hook_addr before other CPUs return from IPI.
 */
#if defined(CONFIG_SMP)
	atomic_set(&hook->hook_activate, 0); 
	mb(); 
	smp_call_function((void *)wait_for_memory_flag, &hook->hook_activate, 1, 0); 
#endif
	addr[2] = 1;
	flush_icache_range(addr + 2, addr + 2);
#if defined(CONFIG_SMP)
	atomic_set(&hook->hook_activate, 1); 
	mb(); 
#endif
	return;
}
#endif /* __ASM_I386_HOOK_H */
