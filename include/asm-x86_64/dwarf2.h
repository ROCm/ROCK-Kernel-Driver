#ifndef _DWARF2_H
#define _DWARF2_H 1

#include <linux/config.h>

#ifndef __ASSEMBLY__
#warning "asm/dwarf2.h should be only included in pure assembly files"
#endif

/* 
   Macros for dwarf2 CFI unwind table entries.
   See "as.info" for details on these pseudo ops. Unfortunately 
   they are only supported in very new binutils, so define them 
   away for older version. 
 */

#ifdef CONFIG_CFI_BINUTILS

#define CFI_STARTPROC .cfi_startproc
#define CFI_ENDPROC .cfi_endproc
#define CFI_DEF_CFA .cfi_def_cfa
#define CFI_DEF_CFA_REGISTER .cfi_def_cfa_register
#define CFI_DEF_CFA_OFFSET .cfi_def_cfa_offset
#define CFI_ADJUST_CFA_OFFSET .cfi_adjust_cfa_offset
#define CFI_OFFSET .cfi_offset
#define CFI_REL_OFFSET .cfi_rel_offset

#else

#ifdef __ASSEMBLY__
	.macro nothing
	.endm
	.macro nothing1 a
	.endm
	.macro nothing2 a,b
	.endm      
#endif

#define CFI_STARTPROC nothing
#define CFI_ENDPROC nothing
#define CFI_DEF_CFA nothing2
#define CFI_DEF_CFA_REGISTER nothing1
#define CFI_DEF_CFA_OFFSET nothing1
#define CFI_ADJUST_CFA_OFFSET nothing1
#define CFI_OFFSET nothing2
#define CFI_REL_OFFSET nothing2

#endif

#endif
