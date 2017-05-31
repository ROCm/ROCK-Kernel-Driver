#ifndef _ASM_X86_UNDWARF_H
#define _ASM_X86_UNDWARF_H

#include "undwarf-types.h"

#ifdef __ASSEMBLY__

/*
 * In asm, there are two kinds of code: normal C-type callable functions and
 * the rest.  The normal callable functions can be called by other code, and
 * don't do anything unusual with the stack.  Such normal callable functions
 * are annotated with the ENTRY/ENDPROC macros.  Most asm code falls in this
 * category.  In this case, no special debugging annotations are needed because
 * objtool can automatically generate the .undwarf section which the undwarf
 * unwinder reads at runtime.
 *
 * Anything which doesn't fall into the above category, such as syscall and
 * interrupt handlers, tends to not be called directly by other functions, and
 * often does unusual non-C-function-type things with the stack pointer.  Such
 * code needs to be annotated such that objtool can understand it.  The
 * following CFI hint macros are for this type of code.
 *
 * These macros provide hints to objtool about the state of the stack at each
 * instruction.  Objtool starts from the hints and follows the code flow,
 * making automatic CFI adjustments when it sees pushes and pops, adjusting the
 * debuginfo as necessary.  It will also warn if it sees any inconsistencies.
 */
.macro CFI_HINT cfa_reg=UNDWARF_REG_SP cfa_offset=0 type=UNDWARF_TYPE_CFA
#ifdef CONFIG_STACK_VALIDATION
999:
	.pushsection .discard.undwarf_cfi_hints
		/* struct undwarf_cfi_hint */
		.long 999b - .		/* ip		*/
		.short \cfa_offset	/* cfa_offset	*/
		.byte \cfa_reg		/* cfa_reg */
		.byte \type		/* type */
	.popsection
#endif
.endm

.macro CFI_EMPTY
	CFI_HINT cfa_reg=UNDWARF_REG_UNDEFINED
.endm

.macro CFI_REGS base=rsp offset=0 indirect=0 extra=1 iret=0
	.if \base == rsp && \indirect
		.set cfa_reg, UNDWARF_REG_SP_INDIRECT
	.elseif \base == rsp
		.set cfa_reg, UNDWARF_REG_SP
	.elseif \base == rbp
		.set cfa_reg, UNDWARF_REG_BP
	.elseif \base == rdi
		.set cfa_reg, UNDWARF_REG_DI
	.elseif \base == rdx
		.set cfa_reg, UNDWARF_REG_DX
	.else
		.error "CFI_REGS: bad base register"
	.endif

	.if \iret
		.set type, UNDWARF_TYPE_REGS_IRET
	.else
		.set type, UNDWARF_TYPE_REGS
	.endif

	CFI_HINT cfa_reg=cfa_reg cfa_offset=\offset type=type
.endm

.macro CFI_IRET_REGS base=rsp offset=0
	CFI_REGS base=\base offset=\offset iret=1
.endm

.macro CFI_FUNC cfa_offset=8
	CFI_HINT cfa_offset=\cfa_offset
.endm

#else /* !__ASSEMBLY__ */

#define CFI_HINT(cfa_reg, cfa_offset, type)			\
	"999: \n\t"						\
	".pushsection .discard.undwarf_cfi_hints\n\t"		\
	/* struct undwarf_cfi_hint */				\
	".long 999b - .\n\t"					\
	".short " __stringify(cfa_offset) "\n\t"		\
	".byte " __stringify(cfa_reg) "\n\t"			\
	".byte " __stringify(type) "\n\t"			\
	".popsection\n\t"

#define CFI_SAVE CFI_HINT(0, 0, CFI_HINT_TYPE_SAVE)

#define CFI_RESTORE CFI_HINT(0, 0, CFI_HINT_TYPE_RESTORE)


#endif /* __ASSEMBLY__ */


#endif /* _ASM_X86_UNDWARF_H */
