/*
 * alpha/entry.S
 *
 * kernel entry-points
 */

#include <linux/config.h>
#include <asm/system.h>
#include <asm/cache.h>
#include <asm/asm_offsets.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>

/*
 * stack offsets
 */
#define SP_OFF			184
#define SWITCH_STACK_SIZE	320

/*
 * This defines the normal kernel pt-regs layout.
 *
 * regs 9-15 preserved by C code
 * regs 16-18 saved by PAL-code
 * regs 29-30 saved and set up by PAL-code
 * JRP - Save regs 16-18 in a special area of the stack, so that
 * the palcode-provided values are available to the signal handler.
 */

#define SAVE_ALL			\
	subq	$30,SP_OFF,$30;		\
	stq	$0,0($30);		\
	stq	$1,8($30);		\
	stq	$2,16($30);		\
	stq	$3,24($30);		\
	stq	$4,32($30);		\
	stq	$28,144($30);		\
	lda	$2,alpha_mv;		\
	stq	$5,40($30);		\
	stq	$6,48($30);		\
	stq	$7,56($30);		\
	stq	$8,64($30);		\
	stq	$19,72($30);		\
	stq	$20,80($30);		\
	stq	$21,88($30);		\
	ldq	$2,HAE_CACHE($2);	\
	stq	$22,96($30);		\
	stq	$23,104($30);		\
	stq	$24,112($30);		\
	stq	$25,120($30);		\
	stq	$26,128($30);		\
	stq	$27,136($30);		\
	stq	$2,152($30);		\
	stq	$16,160($30);		\
	stq	$17,168($30);		\
	stq	$18,176($30)

#define RESTORE_ALL			\
	lda	$19,alpha_mv;		\
	ldq	$0,0($30);		\
	ldq	$1,8($30);		\
	ldq	$2,16($30);		\
	ldq	$3,24($30);		\
	ldq	$21,152($30);		\
	ldq	$20,HAE_CACHE($19);	\
	ldq	$4,32($30);		\
	ldq	$5,40($30);		\
	ldq	$6,48($30);		\
	ldq	$7,56($30);		\
	subq	$20,$21,$20;		\
	ldq	$8,64($30);		\
	beq	$20,99f;		\
	ldq	$20,HAE_REG($19);	\
	stq	$21,HAE_CACHE($19);	\
	stq	$21,0($20);		\
	ldq	$0,0($30);		\
	ldq	$1,8($30);		\
99:;					\
	ldq	$19,72($30);		\
	ldq	$20,80($30);		\
	ldq	$21,88($30);		\
	ldq	$22,96($30);		\
	ldq	$23,104($30);		\
	ldq	$24,112($30);		\
	ldq	$25,120($30);		\
	ldq	$26,128($30);		\
	ldq	$27,136($30);		\
	ldq	$28,144($30);		\
	addq	$30,SP_OFF,$30

.text
.set noat

.align 3
.globl	entInt
.ent	entInt
entInt:
	SAVE_ALL
	lda	$8,0x3fff
	lda	$26,ret_from_sys_call
	bic	$30,$8,$8
	jsr	$31,do_entInt
.end entInt

.align 3
.globl	entMM
.ent	entMM
entMM:
	SAVE_ALL
/* save $9 - $15 so the inline exception code can manipulate them.  */
	subq	$30,56,$30
	stq	$9,0($30)
	stq	$10,8($30)
	stq	$11,16($30)
	stq	$12,24($30)
	stq	$13,32($30)
	stq	$14,40($30)
	stq	$15,48($30)
	addq	$30,56,$19
/* handle the fault */
	lda	$8,0x3fff
	bic	$30,$8,$8
	jsr	$26,do_page_fault
/* reload the registers after the exception code played.  */
	ldq	$9,0($30)
	ldq	$10,8($30)
	ldq	$11,16($30)
	ldq	$12,24($30)
	ldq	$13,32($30)
	ldq	$14,40($30)
	ldq	$15,48($30)
	addq	$30,56,$30
/* finish up the syscall as normal.  */
	br	ret_from_sys_call
.end entMM

.align 3
.globl	entArith
.ent	entArith
entArith:
	SAVE_ALL
	lda	$8,0x3fff
	lda	$26,ret_from_sys_call
	bic	$30,$8,$8
	jsr	$31,do_entArith
.end entArith

.align 3
.globl	entIF
.ent	entIF
entIF:
	SAVE_ALL
	lda	$8,0x3fff
	lda	$26,ret_from_sys_call
	bic	$30,$8,$8
	jsr	$31,do_entIF
.end entIF

.align 3
.globl	entDbg
.ent	entDbg
entDbg:
	SAVE_ALL
	lda	$8,0x3fff
	lda	$26,ret_from_sys_call
	bic	$30,$8,$8
	jsr	$31,do_entDbg
.end entDbg


/*
 * kernel_thread(fn, arg, clone_flags)
 */
	.align 4
	.globl	kernel_thread
	.ent	kernel_thread
kernel_thread:
	ldgp	$29,0($27)	/* we can be called from a module */
	.prologue 1
	subq	$30,SP_OFF+6*8,$30
	br	$1, 2f		/* load start address */

	/* We've now "returned" from a fake system call.  */
	unop
	blt	$0, 1f		/* error?  */
	ldi	$1, 0x3fff
	beq	$20, 1f		/* parent or child?  */

	bic	$30, $1, $8	/* in child.  */
	jsr	$26, ($27)
	ldgp	$29, 0($26)
	mov	$0, $16
	mov	$31, $26
	jmp	$31, sys_exit

1:	ret			/* in parent.  */

	.align 4
2:	/* Fake a system call stack frame, as we can't do system calls
	   from kernel space.  Note that we store FN and ARG as they 
	   need to be set up in the child for the call.  Also store $8
	   and $26 for use in the parent.  */
	stq	$31, SP_OFF($30)	/* ps */
	stq	$1, SP_OFF+8($30)	/* pc */
	stq	$29, SP_OFF+16($30)	/* gp */
	stq	$16, 136($30)		/* $27; FN for child */
	stq	$17, SP_OFF+24($30)	/* $16; ARG for child */
	stq	$8, 64($30)		/* $8 */
	stq	$26, 128($30)		/* $26 */
	/* Avoid the HAE being gratuitously wrong, to avoid restoring it.  */
	ldq	$2, alpha_mv+HAE_CACHE
	stq	$2, 152($30)		/* HAE */

	/* Shuffle FLAGS to the front; add CLONE_VM.  */
	ldi	$1, CLONE_VM
	or	$18, $1, $16
	bsr	$26, sys_clone

	/* We don't actually care for a3 success widgetry in the kernel.
	   Not for positive errno values.  */
	stq	$0, 0($30)		/* $0 */
	br	restore_all
.end	kernel_thread

/*
 * __kernel_execve(path, argv, envp, regs)
 */
.align 3
.globl	__kernel_execve
.ent	__kernel_execve
__kernel_execve:
	ldgp	$29,0($27)	/* we can be called from modules.  */
	subq	$30,16,$30
	.frame	$30,16,$26,0
	stq	$26,0($30)
	stq	$19,8($30)
	.prologue 1
	jsr	$26,do_execve
	bne	$0,1f		/* error! */
	ldq	$30,8($30)
	br	$31,ret_from_sys_call
1:	ldq	$26,0($30)
	addq	$30,16,$30
	ret
.end	__kernel_execve

.align 3
.ent	do_switch_stack
do_switch_stack:
	lda	$30,-SWITCH_STACK_SIZE($30)
	stq	$9,0($30)
	stq	$10,8($30)
	stq	$11,16($30)
	stq	$12,24($30)
	stq	$13,32($30)
	stq	$14,40($30)
	stq	$15,48($30)
	stq	$26,56($30)
	stt	$f0,64($30)
	stt	$f1,72($30)
	stt	$f2,80($30)
	stt	$f3,88($30)
	stt	$f4,96($30)
	stt	$f5,104($30)
	stt	$f6,112($30)
	stt	$f7,120($30)
	stt	$f8,128($30)
	stt	$f9,136($30)
	stt	$f10,144($30)
	stt	$f11,152($30)
	stt	$f12,160($30)
	stt	$f13,168($30)
	stt	$f14,176($30)
	stt	$f15,184($30)
	stt	$f16,192($30)
	stt	$f17,200($30)
	stt	$f18,208($30)
	stt	$f19,216($30)
	stt	$f20,224($30)
	stt	$f21,232($30)
	stt	$f22,240($30)
	stt	$f23,248($30)
	stt	$f24,256($30)
	stt	$f25,264($30)
	stt	$f26,272($30)
	stt	$f27,280($30)
	mf_fpcr	$f0		# get fpcr
	stt	$f28,288($30)
	stt	$f29,296($30)
	stt	$f30,304($30)
	stt	$f0,312($30)	# save fpcr in slot of $f31
	ldt	$f0,64($30)	# dont let "do_switch_stack" change fp state.
	ret	$31,($1),1
.end do_switch_stack

.align 3
.ent	undo_switch_stack
undo_switch_stack:
	ldq	$9,0($30)
	ldq	$10,8($30)
	ldq	$11,16($30)
	ldq	$12,24($30)
	ldq	$13,32($30)
	ldq	$14,40($30)
	ldq	$15,48($30)
	ldq	$26,56($30)
	ldt	$f30,312($30)	# get saved fpcr
	ldt	$f0,64($30)
	ldt	$f1,72($30)
	ldt	$f2,80($30)
	ldt	$f3,88($30)
	mt_fpcr	$f30		# install saved fpcr
	ldt	$f4,96($30)
	ldt	$f5,104($30)
	ldt	$f6,112($30)
	ldt	$f7,120($30)
	ldt	$f8,128($30)
	ldt	$f9,136($30)
	ldt	$f10,144($30)
	ldt	$f11,152($30)
	ldt	$f12,160($30)
	ldt	$f13,168($30)
	ldt	$f14,176($30)
	ldt	$f15,184($30)
	ldt	$f16,192($30)
	ldt	$f17,200($30)
	ldt	$f18,208($30)
	ldt	$f19,216($30)
	ldt	$f20,224($30)
	ldt	$f21,232($30)
	ldt	$f22,240($30)
	ldt	$f23,248($30)
	ldt	$f24,256($30)
	ldt	$f25,264($30)
	ldt	$f26,272($30)
	ldt	$f27,280($30)
	ldt	$f28,288($30)
	ldt	$f29,296($30)
	ldt	$f30,304($30)
	lda	$30,SWITCH_STACK_SIZE($30)
	ret	$31,($1),1
.end undo_switch_stack

.align 3
.globl	entUna
.ent	entUna
entUna:
	lda	$30,-256($30)
	stq	$0,0($30)
	ldq	$0,256($30)	/* get PS */
	stq	$1,8($30)
	stq	$2,16($30)
	stq	$3,24($30)
	and	$0,8,$0		/* user mode? */
	stq	$4,32($30)
	bne	$0,entUnaUser	/* yup -> do user-level unaligned fault */
	stq	$5,40($30)
	stq	$6,48($30)
	stq	$7,56($30)
	stq	$8,64($30)
	stq	$9,72($30)
	stq	$10,80($30)
	stq	$11,88($30)
	stq	$12,96($30)
	stq	$13,104($30)
	stq	$14,112($30)
	stq	$15,120($30)
	/* 16-18 PAL-saved */
	stq	$19,152($30)
	stq	$20,160($30)
	stq	$21,168($30)
	stq	$22,176($30)
	stq	$23,184($30)
	stq	$24,192($30)
	stq	$25,200($30)
	stq	$26,208($30)
	stq	$27,216($30)
	stq	$28,224($30)
	stq	$29,232($30)
	lda	$8,0x3fff
	stq	$31,248($30)
	bic	$30,$8,$8
	jsr	$26,do_entUna
	ldq	$0,0($30)
	ldq	$1,8($30)
	ldq	$2,16($30)
	ldq	$3,24($30)
	ldq	$4,32($30)
	ldq	$5,40($30)
	ldq	$6,48($30)
	ldq	$7,56($30)
	ldq	$8,64($30)
	ldq	$9,72($30)
	ldq	$10,80($30)
	ldq	$11,88($30)
	ldq	$12,96($30)
	ldq	$13,104($30)
	ldq	$14,112($30)
	ldq	$15,120($30)
	/* 16-18 PAL-saved */
	ldq	$19,152($30)
	ldq	$20,160($30)
	ldq	$21,168($30)
	ldq	$22,176($30)
	ldq	$23,184($30)
	ldq	$24,192($30)
	ldq	$25,200($30)
	ldq	$26,208($30)
	ldq	$27,216($30)
	ldq	$28,224($30)
	ldq	$29,232($30)
	lda	$30,256($30)
	call_pal PAL_rti
.end entUna

.align 3
.ent	entUnaUser
entUnaUser:
	ldq	$0,0($30)	/* restore original $0 */
	lda	$30,256($30)	/* pop entUna's stack frame */
	SAVE_ALL		/* setup normal kernel stack */
	lda	$30,-56($30)
	stq	$9,0($30)
	stq	$10,8($30)
	stq	$11,16($30)
	stq	$12,24($30)
	stq	$13,32($30)
	stq	$14,40($30)
	stq	$15,48($30)
	lda	$8,0x3fff
	addq	$30,56,$19
	bic	$30,$8,$8
	jsr	$26,do_entUnaUser
	ldq	$9,0($30)
	ldq	$10,8($30)
	ldq	$11,16($30)
	ldq	$12,24($30)
	ldq	$13,32($30)
	ldq	$14,40($30)
	ldq	$15,48($30)
	lda	$30,56($30)
	br	ret_from_sys_call
.end	entUnaUser

/*
 * A fork is the same as clone(SIGCHLD, 0);
 */
.align 3
.globl	sys_fork
.ent	sys_fork
sys_fork:
	bsr	$1,do_switch_stack
	bis	$31,SIGCHLD,$16
	mov	$31,$17
	mov	$30,$18
	jsr	$26,alpha_clone
	bsr	$1,undo_switch_stack
	ret	$31,($26),1
.end	sys_fork

.align 3
.globl	sys_clone
.ent	sys_clone
sys_clone:
	bsr	$1,do_switch_stack
	/* arg1 and arg2 come from the user */
	mov	$30,$18
	jsr	$26,alpha_clone
	bsr	$1,undo_switch_stack
	ret	$31,($26),1
.end	sys_clone

.align 3
.globl	sys_vfork
.ent	sys_vfork
sys_vfork:
	bsr	$1,do_switch_stack
	mov	$30,$16
	jsr	$26,alpha_vfork
	bsr	$1,undo_switch_stack
	ret	$31,($26),1
.end	sys_vfork

.align 3
.globl  alpha_switch_to
.ent    alpha_switch_to
alpha_switch_to:
	.prologue 0
	bsr	$1,do_switch_stack
	call_pal PAL_swpctx
	lda	$8,0x3fff
	bsr	$1,undo_switch_stack
	bic	$30,$8,$8
	ret	$31,($26),1
.end alpha_switch_to

#if CONFIG_SMP || CONFIG_PREEMPT
.globl  ret_from_fork
.align 3
.ent ret_from_fork
ret_from_fork:
	lda	$26,ret_from_sys_call
	mov	$17,$16
	jmp	$31,schedule_tail
.end ret_from_fork
#endif

/*
 * Oh, well.. Disassembling OSF/1 binaries to find out how the
 * system calls work isn't much fun.
 *
 * entSys is special in that the PAL-code doesn't save a0-a2, so
 * we start off by doing that by hand.
 */
.align 3
.globl	entSys
.globl	ret_from_sys_call
.ent	entSys
entSys:
	SAVE_ALL
	lda	$8,0x3fff
	bic	$30,$8,$8
	lda	$4,NR_SYSCALLS($31)
	stq	$16,SP_OFF+24($30)
	lda	$5,sys_call_table
	lda	$27,sys_ni_syscall
	cmpult	$0,$4,$4
	ldl	$3,TI_FLAGS($8)
	stq	$17,SP_OFF+32($30)
	s8addq	$0,$5,$5
	stq	$18,SP_OFF+40($30)
	blbs	$3,strace
	beq	$4,1f
	ldq	$27,0($5)
1:	jsr	$26,($27),alpha_ni_syscall
	ldgp	$29,0($26)
	blt	$0,syscall_error	/* the call failed */
	stq	$0,0($30)
	stq	$31,72($30)		/* a3=0 => no error */

.align 3
ret_from_sys_call:
	cmovne	$26,0,$19		/* $19 = 0 => non-restartable */
	ldq	$0,SP_OFF($30)
	and	$0,8,$0
	beq	$0,restore_all
ret_from_reschedule:
	/* Make sure need_resched and sigpending don't change between
		sampling and the rti.  */
	lda	$16,7
	call_pal PAL_swpipl
	ldl	$5,TI_FLAGS($8)
	and	$5,_TIF_WORK_MASK,$2
	bne	$5,work_pending
restore_all:
	RESTORE_ALL
	call_pal PAL_rti

work_pending:
	and	$5,_TIF_NEED_RESCHED,$2
	beq	$2,work_notifysig

work_resched:
	subq	$30,16,$30
	stq	$19,0($30)              /* save syscall nr */
	stq	$20,8($30)              /* and error indication (a3) */
	jsr	$26,schedule
	ldq	$19,0($30)
	ldq	$20,8($30)
	addq	$30,16,$30
	/* Make sure need_resched and sigpending don't change between
		sampling and the rti.  */
	lda	$16,7
	call_pal PAL_swpipl
	ldl	$5,TI_FLAGS($8)
	and	$5,_TIF_WORK_MASK,$2
	beq	$2,restore_all
	and	$5,_TIF_NEED_RESCHED,$2
	bne	$2,work_resched

work_notifysig:
	mov	$30,$17
	br	$1,do_switch_stack
	mov	$5,$21
	mov	$30,$18
	mov	$31,$16
	jsr	$26,do_notify_resume
	bsr	$1,undo_switch_stack
	br	restore_all

/* PTRACE syscall handler */
.align 3
strace:
	/* set up signal stack, call syscall_trace */
	bsr	$1,do_switch_stack
	jsr	$26,syscall_trace
	bsr	$1,undo_switch_stack

	/* get the system call number and the arguments back.. */
	ldq	$0,0($30)
	ldq	$16,SP_OFF+24($30)
	ldq	$17,SP_OFF+32($30)
	ldq	$18,SP_OFF+40($30)
	ldq	$19,72($30)
	ldq	$20,80($30)
	ldq	$21,88($30)

	/* get the system call pointer.. */
	lda	$1,NR_SYSCALLS($31)
	lda	$2,sys_call_table
	lda	$27,alpha_ni_syscall
	cmpult	$0,$1,$1
	s8addq	$0,$2,$2
	beq	$1,1f
	ldq	$27,0($2)
1:	jsr	$26,($27),sys_gettimeofday
	ldgp	$29,0($26)

	/* check return.. */
	blt	$0,strace_error	/* the call failed */
	stq	$31,72($30)		/* a3=0 => no error */
strace_success:
	stq	$0,0($30)		/* save return value */

	bsr	$1,do_switch_stack
	jsr	$26,syscall_trace
	bsr	$1,undo_switch_stack
	br	$31,ret_from_sys_call

	.align	3
strace_error:
	ldq	$19,0($30)	/* old syscall nr (zero if success) */
	beq	$19,strace_success
	ldq	$20,72($30)	/* .. and this a3 */

	subq	$31,$0,$0	/* with error in v0 */
	addq	$31,1,$1	/* set a3 for errno return */
	stq	$0,0($30)
	stq	$1,72($30)	/* a3 for return */

	bsr	$1,do_switch_stack
	mov	$19,$9		/* save old syscall number */
	mov	$20,$10		/* save old a3 */
	jsr	$26,syscall_trace
	mov	$9,$19
	mov	$10,$20
	bsr	$1,undo_switch_stack

	mov	$31,$26		/* tell "ret_from_sys_call" we can restart */
	br	ret_from_sys_call

	.align 3
syscall_error:
	/*
	 * Some system calls (e.g., ptrace) can return arbitrary
	 * values which might normally be mistaken as error numbers.
	 * Those functions must zero $0 (v0) directly in the stack
	 * frame to indicate that a negative return value wasn't an
	 * error number..
	 */
	ldq	$19,0($30)	/* old syscall nr (zero if success) */
	beq	$19,ret_success

	ldq	$20,72($30)	/* .. and this a3 */
	subq	$31,$0,$0	/* with error in v0 */
	addq	$31,1,$1	/* set a3 for errno return */
	stq	$0,0($30)
	mov	$31,$26		/* tell "ret_from_sys_call" we can restart */
	stq	$1,72($30)	/* a3 for return */
	br	ret_from_sys_call

ret_success:
	stq	$0,0($30)
	stq	$31,72($30)	/* a3=0 => no error */
	br	ret_from_sys_call
.end entSys

.align 3
.globl sys_sigreturn
.ent sys_sigreturn
sys_sigreturn:
	mov	$30,$17
	lda	$18,-SWITCH_STACK_SIZE($30)
	lda	$30,-SWITCH_STACK_SIZE($30)
	jsr	$26,do_sigreturn
	br	$1,undo_switch_stack
	br	ret_from_sys_call
.end sys_sigreturn

.align 3
.globl sys_rt_sigreturn
.ent sys_rt_sigreturn
sys_rt_sigreturn:
	mov	$30,$17
	lda	$18,-SWITCH_STACK_SIZE($30)
	lda	$30,-SWITCH_STACK_SIZE($30)
	jsr	$26,do_rt_sigreturn
	br	$1,undo_switch_stack
	br	ret_from_sys_call
.end sys_rt_sigreturn

.align 3
.globl sys_sigsuspend
.ent sys_sigsuspend
sys_sigsuspend:
	mov	$30,$17
	br	$1,do_switch_stack
	mov	$30,$18
	subq	$30,16,$30
	stq	$26,0($30)
	jsr	$26,do_sigsuspend
	ldq	$26,0($30)
	lda	$30,SWITCH_STACK_SIZE+16($30)
	ret	$31,($26),1
.end sys_sigsuspend

.align 3
.globl sys_rt_sigsuspend
.ent sys_rt_sigsuspend
sys_rt_sigsuspend:
	mov	$30,$18
	br	$1,do_switch_stack
	mov	$30,$19
	subq	$30,16,$30
	stq	$26,0($30)
	jsr	$26,do_rt_sigsuspend
	ldq	$26,0($30)
	lda	$30,SWITCH_STACK_SIZE+16($30)
	ret	$31,($26),1
.end sys_rt_sigsuspend
