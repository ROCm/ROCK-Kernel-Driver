/*
 *  arch/s390x/lib/uaccess.S
 *    fixup routines for copy_{from|to}_user functions.
 *
 *  S390
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Authors(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  These functions have a non-standard call interface
 */

#include <asm/lowcore.h>

        .text
        .align 4
        .globl __copy_from_user_fixup
__copy_from_user_fixup:
        lg    1,__LC_PGM_OLD_PSW+8
0:      lghi  3,-4096
        ng    3,__LC_TRANS_EXC_ADDR
        sgr   3,4
        bm    4(1)
1:      mvcle 2,4,0
        b     4(1)
        .section __ex_table,"a"
        .align 8
	.quad 1b,0b
        .previous

        .align 4
        .text
        .globl __copy_to_user_fixup
__copy_to_user_fixup:
        lg    1,__LC_PGM_OLD_PSW+8
0:      lghi  5,-4096
        ng    5,__LC_TRANS_EXC_ADDR
        sgr   5,4
        bm    4(1)
1:      mvcle 4,2,0
        b     4(1)
        .section __ex_table,"a"
        .align 8
        .quad 1b,0b
        .previous

