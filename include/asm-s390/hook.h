#ifndef __ASM_S390_HOOK_H
#define __ASM_S390_HOOK_H
/*
 * Kernel Hooks optimized for s390.
 * 
 * Authors: Mike Grundy <grundym@us.ibm.com> s390
 */
#include <asm-generic/hook.h>

#if defined(CONFIG_HOOK) || defined(CONFIG_HOOK_MODULE)

#define IF_HOOK_ENABLED(h, hk) _IF_HOOK_ENABLED(h, #hk)
#define _IF_HOOK_ENABLED(h, hk) \
	register int tmp; \
	__asm__ __volatile__ (".global "hk"; "hk":lhi %0, 0x00":"=r"(tmp)); \
	if (unlikely(tmp))

#endif /* CONFIG_HOOK || CONFIG_HOOK_MODULE */

/*
 * Sanity check the hook location for valid instructions at hook location.
 * At hook location, we should find these instructions:
 *  a7 18 00 00             lhi     %r1,0
 *  12 11                   ltr     %r1,%r1
 * We can check for the lhi and ltr instructions. As the lhi instruction encodes
 * the register name in it, and we can't guarantee which register will be used,
 * we'll mask out the bits corresponding to the target register.
 */
#define OPCODE_MOV2_1			0xa7 /* LHI first byte */
#define OPCODE_MOV2_2			0x08 /* LHI second byte */
#define OPCODE_MOV2_1_MASK		0xff
#define OPCODE_MOV2_2_MASK		0x0f
/* Compiler generates LTR opcode 12, but second op not tested */
		
static inline int is_asm_hook(unsigned char * addr)
{
	if (!addr){
		return 0;
	}
	if (((addr[0] & OPCODE_MOV2_1_MASK) == OPCODE_MOV2_1) && 
		    ((addr[1] & OPCODE_MOV2_2_MASK) == OPCODE_MOV2_2)) {
		/* was checking a 32bit val, need to check 16, cheated with 8+8 */
		if (addr[2]== 0 && addr[3]== 0){
			return 1;
		}
	}
	return 0;
}
#endif /* __ASM_S390_HOOK_H */
