/*
 * include/asm-x86_64/i387.h
 *
 * Copyright (C) 1994 Linus Torvalds
 *
 * Pentium III FXSR, SSE support
 * General FPU state handling cleanups
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

#ifndef __ASM_X86_64_I387_H
#define __ASM_X86_64_I387_H

#include <linux/sched.h>
#include <linux/spinlock.h>
#include <asm/processor.h>
#include <asm/sigcontext.h>
#include <asm/user.h>

extern void init_fpu(void);

/*
 * FPU lazy state save handling...
 */
extern void save_fpu( struct task_struct *tsk );
extern void save_init_fpu( struct task_struct *tsk );
extern void restore_fpu( struct task_struct *tsk );

extern void kernel_fpu_begin(void);
#define kernel_fpu_end() do { stts(); preempt_enable(); } while(0)


#define unlazy_fpu( tsk ) do { \
	if (test_tsk_thread_flag(tsk, TIF_USEDFPU)) \
		save_init_fpu( tsk ); \
} while (0)

#define clear_fpu( tsk )					\
do {								\
	if (test_tsk_thread_flag(tsk, TIF_USEDFPU)) {		\
		asm volatile("fwait");				\
		clear_tsk_thread_flag(tsk, TIF_USEDFPU);	\
		stts();						\
	}							\
} while (0)

/*
 * FPU state interaction...
 */
extern unsigned short get_fpu_cwd( struct task_struct *tsk );
extern unsigned short get_fpu_swd( struct task_struct *tsk );
extern unsigned short get_fpu_twd( struct task_struct *tsk );
extern unsigned short get_fpu_mxcsr( struct task_struct *tsk );

extern void set_fpu_cwd( struct task_struct *tsk, unsigned short cwd );
extern void set_fpu_swd( struct task_struct *tsk, unsigned short swd );
extern void set_fpu_twd( struct task_struct *tsk, unsigned short twd );
extern void set_fpu_mxcsr( struct task_struct *tsk, unsigned short mxcsr );

#define load_mxcsr( val ) do { \
		unsigned long __mxcsr = ((unsigned long)(val) & 0xffbf); \
		asm volatile( "ldmxcsr %0" : : "m" (__mxcsr) ); \
} while (0)

/*
 * Signal frame handlers...
 */
extern int save_i387( struct _fpstate *buf );
extern int restore_i387( struct _fpstate *buf );

/*
 * ptrace request handers...
 */
extern int get_fpregs( struct user_i387_struct *buf,
		       struct task_struct *tsk );
extern int set_fpregs( struct task_struct *tsk,
		       struct user_i387_struct *buf );

/*
 * FPU state for core dumps...
 */
extern int dump_fpu( struct pt_regs *regs,
		     struct user_i387_struct *fpu );
extern int dump_extended_fpu( struct pt_regs *regs,
			      struct user_i387_struct *fpu );

#endif /* __ASM_X86_64_I387_H */
