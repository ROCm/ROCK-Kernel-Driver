#ifndef __ASM_GENERIC_HOOK_H
#define __ASM_GENERIC_HOOK_H
/*
 * Kernel Hooks common code for many archs.
 * 
 * Authors: Vamsi Krishna S. <vamsi_krishna@in.ibm.com>
 */
#include <asm/cacheflush.h>

static inline void deactivate_asm_hook(struct hook *hook)
{
	unsigned char *addr = (unsigned char *) (hook->hook_addr);
	addr[2] = 0;
	flush_icache_range((unsigned long) addr + 2, (unsigned long) addr + 2);
	return;
}

static inline void activate_asm_hook(struct hook *hook)
{
	unsigned char *addr = (unsigned char *) (hook->hook_addr);
	addr[2] = 1;
	flush_icache_range((unsigned long) addr + 2, (unsigned long) addr + 2);
	return;
}
#endif /* __ASM_GENERIC_HOOK_H */
