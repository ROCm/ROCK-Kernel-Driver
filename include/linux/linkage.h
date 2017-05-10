#ifndef _LINUX_LINKAGE_H
#define _LINUX_LINKAGE_H

#include <linux/compiler.h>
#include <linux/stringify.h>
#include <linux/export.h>
#include <asm/linkage.h>

/* Some toolchains use other characters (e.g. '`') to mark new line in macro */
#ifndef ASM_NL
#define ASM_NL		 ;
#endif

#ifdef __cplusplus
#define CPP_ASMLINKAGE extern "C"
#else
#define CPP_ASMLINKAGE
#endif

#ifndef asmlinkage
#define asmlinkage CPP_ASMLINKAGE
#endif

#ifndef cond_syscall
#define cond_syscall(x)	asm(				\
	".weak " VMLINUX_SYMBOL_STR(x) "\n\t"		\
	".set  " VMLINUX_SYMBOL_STR(x) ","		\
		 VMLINUX_SYMBOL_STR(sys_ni_syscall))
#endif

#ifndef SYSCALL_ALIAS
#define SYSCALL_ALIAS(alias, name) asm(			\
	".globl " VMLINUX_SYMBOL_STR(alias) "\n\t"	\
	".set   " VMLINUX_SYMBOL_STR(alias) ","		\
		  VMLINUX_SYMBOL_STR(name))
#endif

#define __page_aligned_data	__section(.data..page_aligned) __aligned(PAGE_SIZE)
#define __page_aligned_bss	__section(.bss..page_aligned) __aligned(PAGE_SIZE)

/*
 * For assembly routines.
 *
 * Note when using these that you must specify the appropriate
 * alignment directives yourself
 */
#define __PAGE_ALIGNED_DATA	.section ".data..page_aligned", "aw"
#define __PAGE_ALIGNED_BSS	.section ".bss..page_aligned", "aw"

/*
 * This is used by architectures to keep arguments on the stack
 * untouched by the compiler by keeping them live until the end.
 * The argument stack may be owned by the assembly-language
 * caller, not the callee, and gcc doesn't always understand
 * that.
 *
 * We have the return value, and a maximum of six arguments.
 *
 * This should always be followed by a "return ret" for the
 * protection to work (ie no more work that the compiler might
 * end up needing stack temporaries for).
 */
/* Assembly files may be compiled with -traditional .. */
#ifndef __ASSEMBLY__
#ifndef asmlinkage_protect
# define asmlinkage_protect(n, ret, args...)	do { } while (0)
#endif
#endif

#ifndef __ALIGN
#define __ALIGN		.align 4,0x90
#define __ALIGN_STR	".align 4,0x90"
#endif

#ifdef __ASSEMBLY__

/* SYM_T_FUNC -- type used by assembler to mark functions */
#ifndef SYM_T_FUNC
#define SYM_T_FUNC				STT_FUNC
#endif

/* SYM_T_OBJECT -- type used by assembler to mark data */
#ifndef SYM_T_OBJECT
#define SYM_T_OBJECT				STT_OBJECT
#endif

/* SYM_A_* -- align the symbol? */
#define SYM_A_ALIGN				ALIGN
#define SYM_A_NONE				/* nothing */

/* SYM_V_* -- visibility of symbols */
#define SYM_V_GLOBAL(name)			.globl name
#define SYM_V_WEAK(name)			.weak name
#define SYM_V_LOCAL(name)			/* nothing */

#ifndef LINKER_SCRIPT
#define ALIGN __ALIGN
#define ALIGN_STR __ALIGN_STR

/* === DEPRECATED annotations === */

#ifndef ENTRY
/* deprecated, use SYM_FUNC_START */
#define ENTRY(name) \
	SYM_FUNC_START(name)
#endif
#endif /* LINKER_SCRIPT */

#ifndef WEAK
/* deprecated, use SYM_FUNC_START_WEAK */
#define WEAK(name)	   \
	SYM_FUNC_START_WEAK(name)
#endif

#ifndef END
/* deprecated, use SYM_FUNC_END, SYM_DATA_END, or SYM_END */
#define END(name) \
	.size name, .-name
#endif

/* If symbol 'name' is treated as a subroutine (gets called, and returns)
 * then please use ENDPROC to mark 'name' as STT_FUNC for the benefit of
 * static analysis tools such as stack depth analyzer.
 */
#ifndef ENDPROC
/* deprecated, use SYM_FUNC_END */
#define ENDPROC(name) \
	SYM_FUNC_END(name)
#endif

/* === generic annotations === */

/* SYM_ENTRY -- use only if you have to for non-paired symbols */
#ifndef SYM_ENTRY
#define SYM_ENTRY(name, visibility, align...)		\
	visibility(name) ASM_NL				\
	align ASM_NL					\
	name:
#endif

/* SYM_START -- use only if you have to */
#ifndef SYM_START
#define SYM_START(name, visibility, align...)		\
	SYM_ENTRY(name, visibility, align)
#endif

/* SYM_END -- use only if you have to */
#ifndef SYM_END
#define SYM_END(name, sym_type)				\
	.type name sym_type ASM_NL			\
	.size name, .-name
#endif

/* === code annotations === */

/* SYM_FUNC_START_LOCAL_ALIAS -- use where there are two local names for one code */
#ifndef SYM_FUNC_START_LOCAL_ALIAS
#define SYM_FUNC_START_LOCAL_ALIAS(name)		\
	SYM_START(name, SYM_V_LOCAL, SYM_A_ALIGN)
#endif

/* SYM_FUNC_START_ALIAS -- use where there are two global names for one code */
#ifndef SYM_FUNC_START_ALIAS
#define SYM_FUNC_START_ALIAS(name)			\
	SYM_START(name, SYM_V_GLOBAL, SYM_A_ALIGN)
#endif

/* SYM_FUNC_START -- use for global functions */
#ifndef SYM_FUNC_START
/*
 * The same as SYM_FUNC_START_ALIAS, but we will need to distinguish these two
 * later.
 */
#define SYM_FUNC_START(name)				\
	SYM_START(name, SYM_V_GLOBAL, SYM_A_ALIGN)
#endif

/* SYM_FUNC_START_LOCAL -- use for local functions */
#ifndef SYM_FUNC_START_LOCAL
/* the same as SYM_FUNC_START_LOCAL_ALIAS, see comment near SYM_FUNC_START */
#define SYM_FUNC_START_LOCAL(name)			\
	SYM_START(name, SYM_V_LOCAL, SYM_A_ALIGN)
#endif

/* SYM_FUNC_START_WEAK -- use for weak functions */
#ifndef SYM_FUNC_START_WEAK
#define SYM_FUNC_START_WEAK(name)			\
	SYM_START(name, SYM_V_WEAK, SYM_A_ALIGN)
#endif

/* SYM_FUNC_END_ALIAS -- the end of LOCAL_ALIASed or ALIASed code */
#ifndef SYM_FUNC_END_ALIAS
#define SYM_FUNC_END_ALIAS(name)			\
	SYM_END(name, SYM_T_FUNC)
#endif

/*
 * SYM_FUNC_END -- the end of SYM_FUNC_START_LOCAL, SYM_FUNC_START,
 * SYM_FUNC_START_WEAK, ...
 */
#ifndef SYM_FUNC_END
/* the same as SYM_FUNC_END_ALIAS, see comment near SYM_FUNC_START */
#define SYM_FUNC_END(name)				\
	SYM_END(name, SYM_T_FUNC)
#endif

/* SYM_FUNC_INNER_LABEL -- only for global labels to the middle of functions */
#ifndef SYM_FUNC_INNER_LABEL
#define SYM_FUNC_INNER_LABEL(name)			\
	.type name SYM_T_FUNC ASM_NL			\
	SYM_ENTRY(name, SYM_V_GLOBAL, SYM_A_NONE)
#endif

/* === data annotations === */

/* SYM_DATA_START -- global data symbol */
#ifndef SYM_DATA_START
#define SYM_DATA_START(name)				\
	SYM_START(name, SYM_V_GLOBAL, SYM_A_NONE)
#endif

/* SYM_DATA_START -- local data symbol */
#ifndef SYM_DATA_START_LOCAL
#define SYM_DATA_START_LOCAL(name)			\
	SYM_START(name, SYM_V_LOCAL, SYM_A_NONE)
#endif

/* SYM_DATA_END -- the end of SYM_DATA_START symbol */
#ifndef SYM_DATA_END
#define SYM_DATA_END(name)				\
	SYM_END(name, SYM_T_OBJECT)
#endif

/* SYM_DATA_END_LABEL -- the labeled end of SYM_DATA_START symbol */
#ifndef SYM_DATA_END_LABEL
#define SYM_DATA_END_LABEL(name, visibility, label)	\
	visibility(label) ASM_NL			\
	.type label SYM_T_OBJECT ASM_NL			\
	label:						\
	SYM_END(name, SYM_T_OBJECT)
#endif

/* SYM_DATA_SIMPLE -- start+end wrapper around simple global data */
#ifndef SYM_DATA_SIMPLE
#define SYM_DATA_SIMPLE(name, data)				\
	SYM_DATA_START(name) ASM_NL				\
	data ASM_NL						\
	SYM_DATA_END(name)
#endif

/* SYM_DATA_SIMPLE_LOCAL -- start+end wrapper around simple local data */
#ifndef SYM_DATA_SIMPLE_LOCAL
#define SYM_DATA_SIMPLE_LOCAL(name, data...)			\
	SYM_DATA_START_LOCAL(name) ASM_NL			\
	data ASM_NL						\
	SYM_DATA_END(name)
#endif

#endif /* __ASSEMBLY__ */

#endif /* _LINUX_LINKAGE_H */
