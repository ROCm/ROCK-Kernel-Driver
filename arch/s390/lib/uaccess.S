/*
 *  arch/s390/lib/uaccess.S
 *    fixup routines for copy_{from|to}_user functions.
 *
 *  s390
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Authors(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  These functions have standard call interface
 */

#include <asm/lowcore.h>

        .text
        .align 4
        .globl __copy_from_user_asm
__copy_from_user_asm:
	lr	%r5,%r3
	sacf	512
0:	mvcle	%r2,%r4,0
	jo	0b
1:	sacf	0
	lr	%r2,%r5
	br	%r14
2:      sll	%r4,1
        srl	%r4,1
        lhi	%r3,-4096
        sll	%r3,1
        srl	%r3,1
        n	%r3,__LC_TRANS_EXC_ADDR
        sr	%r3,%r4
        jm	1b
	j	0b
        .section __ex_table,"a"
	.long	0b,2b
        .previous

        .align 4
        .text
        .globl __copy_to_user_asm
__copy_to_user_asm:
	lr	%r5,%r3
	sacf	512
0:	mvcle	%r4,%r2,0
	jo	0b
1:	sacf	0
	lr	%r2,%r3
	br	%r14
2:      sll	%r4,1
        srl	%r4,1
        lhi	%r5,-4096
        sll	%r5,1
        srl	%r5,1
        n	%r5,__LC_TRANS_EXC_ADDR
        sr	%r5,%r4
        jm	1b
	j	0b
        .section __ex_table,"a"
	.long   0b,2b
        .previous

        .align 4
        .text
        .globl __clear_user_asm
__clear_user_asm:
	lr	%r4,%r2
	lr	%r5,%r3
	sr	%r2,%r2
	sr	%r3,%r3
	sacf	512
0:	mvcle	%r4,%r2,0
	jo	0b
1:	sacf	0
	lr	%r2,%r3
	br	%r14
2:      sll	%r4,1
        srl	%r4,1
        lhi	%r5,-4096
        sll	%r5,1
        srl	%r5,1
        n	%r5,__LC_TRANS_EXC_ADDR
        sr	%r5,%r4
	jm	1b
	j	0b
        .section __ex_table,"a"
	.long   0b,2b
        .previous

