#ifndef __ASM_PPC_HOOK_H
#define __ASM_PPC_HOOK_H
/*
 * Kernel Hooks optimized for PPC64.
 * 
 * Authors: Mike Grundy <grundym@us.ibm.com> PPC64
 */
#include <asm-generic/hook.h>

#if defined(CONFIG_HOOK) || defined(CONFIG_HOOK_MODULE)

#define IF_HOOK_ENABLED(h, hk) _IF_HOOK_ENABLED(h, #hk)
#define _IF_HOOK_ENABLED(h, hk) \
	register int tmp; \
	__asm__ __volatile__ (".global "hk"; "hk":li %0, 0x00":"=r"(tmp)); \
	if (unlikely(tmp))

#endif /* CONFIG_HOOK || CONFIG_HOOK_MODULE */


/*
 * Sanity check the hook location for valid instructions at hook location.
 * At hook location, we should find these instructions:
 *	38 00 00 00       	li    	r0,0
 *	2c 00 00 00            	cmpwi	r0,0
 *	
 * We can check for li and cmpwi instructions. As these instructions encode
 * the register name in the second byte and the register cannot be predicted, 
 * we mask out the bits corresponding to registers in the opcode before comparing.
 * PPC opcodes are six bits, hence mask of 0xFC
 */
#define OPCODE_MOV1			0x38 /* LI (really an extended mnemonic for addi */   
#define OPCODE_MOV1_MASK		0xFC
/* Compiler generates 2c 00 00 00     cmpwi   r0,0 */
		
static inline int is_asm_hook(unsigned char * addr)
{
	if (!addr)
		return 0;
	
	if((addr[0] & OPCODE_MOV1_MASK) == OPCODE_MOV1) {
		if (*((unsigned short *)(addr+1)) == 0)
			return 1;
	}
	return 0;
}
#endif /* __ASM_PPC_HOOK_H */
