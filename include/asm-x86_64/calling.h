/* Some macros to handle stack frames */ 

	.macro SAVE_ARGS	
	pushq %rdi
	pushq %rsi
	pushq %rdx
	pushq %rcx
	pushq %rax
	pushq %r8
	pushq %r9
	pushq %r10
	pushq %r11
	.endm

	.macro RESTORE_ARGS
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rax
	popq %rcx	
	popq %rdx	
	popq %rsi	
	popq %rdi	
	.endm	

	.macro LOAD_ARGS offset
	movq \offset(%rsp),%r11
	movq \offset+8(%rsp),%r10
	movq \offset+16(%rsp),%r9
	movq \offset+24(%rsp),%r8
	movq \offset+40(%rsp),%rcx
	movq \offset+48(%rsp),%rdx
	movq \offset+56(%rsp),%rsi
	movq \offset+64(%rsp),%rdi
	movq \offset+72(%rsp),%rax
	.endm
			
	.macro SAVE_REST
	pushq %rbx
	pushq %rbp
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15
	.endm		

	.macro RESTORE_REST
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %rbp
	popq %rbx
	.endm
		
	.macro SAVE_ALL
	SAVE_ARGS
	SAVE_REST
	.endm
		
	.macro RESTORE_ALL
	RESTORE_REST
	RESTORE_ARGS
	.endm


R15 = 0
R14 = 8
R13 = 16
R12 = 24
RBP = 36
RBX = 40
/* arguments: interrupts/non tracing syscalls only save upto here*/
R11 = 48
R10 = 56	
R9 = 64
R8 = 72
RAX = 80
RCX = 88
RDX = 96
RSI = 104
RDI = 112
ORIG_RAX = 120       /* = ERROR */ 
/* end of arguments */ 	
/* cpu exception frame or undefined in case of fast syscall. */
RIP = 128
CS = 136
EFLAGS = 144
RSP = 152
SS = 160
ARGOFFSET = R11

	.macro SYSRET32
	.byte 0x0f,0x07
	.endm

	.macro SYSRET64
	.byte 0x48,0x0f,0x07
	.endm
