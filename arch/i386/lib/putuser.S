/*
 * __put_user functions.
 *
 * (C) Copyright 1998 Linus Torvalds
 *
 * These functions have a non-standard call interface
 * to make them more efficient.
 */

/*
 * __put_user_X
 *
 * Inputs:	%eax contains the address
 *		%edx contains the value
 *
 * Outputs:	%eax is error code (0 or -EFAULT)
 *		%ecx is corrupted (will contain "current_task").
 *
 * These functions should not modify any other registers,
 * as they get called from within inline assembly.
 */

addr_limit = 12

.text
.align 4
.globl __put_user_1
__put_user_1:
	movl %esp,%ecx
	andl $0xffffe000,%ecx
	cmpl addr_limit(%ecx),%eax
	jae bad_put_user
1:	movb %dl,(%eax)
	xorl %eax,%eax
	ret

.align 4
.globl __put_user_2
__put_user_2:
	addl $1,%eax
	movl %esp,%ecx
	jc bad_put_user
	andl $0xffffe000,%ecx
	cmpl addr_limit(%ecx),%eax
	jae bad_put_user
2:	movw %dx,-1(%eax)
	xorl %eax,%eax
	ret

.align 4
.globl __put_user_4
__put_user_4:
	addl $3,%eax
	movl %esp,%ecx
	jc bad_put_user
	andl $0xffffe000,%ecx
	cmpl addr_limit(%ecx),%eax
	jae bad_put_user
3:	movl %edx,-3(%eax)
	xorl %eax,%eax
	ret

bad_put_user:
	movl $-14,%eax
	ret

.section __ex_table,"a"
	.long 1b,bad_put_user
	.long 2b,bad_put_user
	.long 3b,bad_put_user
.previous
