	#include <asm/calling.h>

	/*
	 * Save registers for the slow path of semaphores here to avoid
	 * disturbance of register allocation in fast paths with function calls.
	 * Written 2001 by Andi Kleen.
	 */
	
	.macro rwsem_thunk name,func
	.globl \name
\name:	
	SAVE_ARGS
	movq %rax,%rdi
	call \func
	jmp  restore
	.endm

	rwsem_thunk rwsem_down_read_failed_thunk,rwsem_down_read_failed
	rwsem_thunk rwsem_down_write_failed_thunk,rwsem_down_write_failed
	rwsem_thunk rwsem_wake_thunk,rwsem_wake
	/* This does not really belong here, but the macros are so
	   convenient. */ 
	rwsem_thunk do_softirq_thunk,do_softirq

restore:	
	RESTORE_ARGS
	ret
