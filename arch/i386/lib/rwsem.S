/* rwsem.S: R/W semaphores, register saving wrapper function stubs
 *
 * Written by David Howells (dhowells@redhat.com).
 * Derived from arch/i386/kernel/semaphore.c
 */

.text
.align 4
.globl __rwsem_down_read_failed
__rwsem_down_read_failed:
	pushl	%edx
	pushl	%ecx
	call	rwsem_down_read_failed
	popl	%ecx
	popl	%edx
	ret

.align 4
.globl __rwsem_down_write_failed
__rwsem_down_write_failed:
	pushl	%edx
	pushl	%ecx
	call	rwsem_down_write_failed
	popl	%ecx
	popl	%edx
	ret

.align 4
.globl __rwsem_wake
__rwsem_wake:
	pushl	%edx
	pushl	%ecx
	call	rwsem_wake
	popl	%ecx
	popl	%edx
	ret
