/*
 *  arch/s390/lib/uaccess.S
 *    fixup routines for copy_{from|to}_user functions.
 *
 *  s390
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
        l     1,__LC_PGM_OLD_PSW+4
        sll   4,1
        srl   4,1
0:      lhi   3,-4096
        sll   3,1
        srl   3,1
        n     3,__LC_TRANS_EXC_ADDR
        sr    3,4
        bm    4(1)
1:      mvcle 2,4,0
        b     4(1)
        .section __ex_table,"a"
	.long 1b,0b
        .previous

        .align 4
        .text
        .globl __copy_to_user_fixup
__copy_to_user_fixup:
        l     1,__LC_PGM_OLD_PSW+4
        sll   4,1
        srl   4,1
0:      lhi   5,-4096
        sll   5,1
        srl   5,1
        n     5,__LC_TRANS_EXC_ADDR
        sr    5,4
        bm    4(1)
1:      mvcle 4,2,0
        b     4(1)
        .section __ex_table,"a"
        .long 1b,0b
        .previous

