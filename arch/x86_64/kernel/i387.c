/*
 *  linux/arch/x86_64/kernel/i387.c
 *
 *  Copyright (C) 1994 Linus Torvalds
 *  Copyright (C) 2002 Andi Kleen, SuSE Labs
 *
 *  Pentium III FXSR, SSE support
 *  General FPU state handling cleanups
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 * 
 *  x86-64 rework 2002 Andi Kleen. 
 *  Does direct fxsave in and out of user space now for signal handlers.
 *  All the FSAVE<->FXSAVE conversion code has been moved to the 32bit emulation,
 *  the 64bit user space sees a FXSAVE frame directly. 
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <asm/processor.h>
#include <asm/i387.h>
#include <asm/sigcontext.h>
#include <asm/user.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>

static struct i387_fxsave_struct init_fpu_env; 

/*
 * Called at bootup to set up the initial FPU state that is later cloned
 * into all processes.
 */
void __init fpu_init(void)
{
	unsigned long oldcr0 = read_cr0();
	extern void __bad_fxsave_alignment(void);
		
	if (offsetof(struct task_struct, thread.i387.fxsave) & 15)
		__bad_fxsave_alignment();
	set_in_cr4(X86_CR4_OSFXSR);
	set_in_cr4(X86_CR4_OSXMMEXCPT);

	write_cr0(oldcr0 & ~((1UL<<3)|(1UL<<2))); /* clear TS and EM */

	asm("fninit"); 
	load_mxcsr(0x1f80); 
	/* initialize MMX state. normally this will be covered by fninit, but the 
	   architecture doesn't guarantee it so do it explicitely. */ 
	asm volatile("movq %0,%%mm0\n\t"
	    "movq %%mm0,%%mm1\n\t"
	    "movq %%mm0,%%mm2\n\t"
	    "movq %%mm0,%%mm3\n\t"
	    "movq %%mm0,%%mm4\n\t"
	    "movq %%mm0,%%mm5\n\t"
	    "movq %%mm0,%%mm6\n\t"
	    "movq %%mm0,%%mm7\n\t" :: "m" (0ULL));
	asm("emms");

	/* initialize XMM state */ 
	asm("xorpd %xmm0,%xmm0");
	asm("xorpd %xmm1,%xmm1");
	asm("xorpd %xmm2,%xmm2");
	asm("xorpd %xmm3,%xmm3");
	asm("xorpd %xmm4,%xmm4");
	asm("xorpd %xmm5,%xmm5");
	asm("xorpd %xmm6,%xmm6");
	asm("xorpd %xmm7,%xmm7");
	asm("xorpd %xmm8,%xmm8");
	asm("xorpd %xmm9,%xmm9");
	asm("xorpd %xmm10,%xmm10");
	asm("xorpd %xmm11,%xmm11");
	asm("xorpd %xmm12,%xmm12");
	asm("xorpd %xmm13,%xmm13");
	asm("xorpd %xmm14,%xmm14");
	asm("xorpd %xmm15,%xmm15");
	load_mxcsr(0x1f80);
	asm volatile("fxsave %0" : "=m" (init_fpu_env));

	/* clean state in init */
	stts();
	clear_thread_flag(TIF_USEDFPU);
	current->used_math = 0;
}

/*
 * The _current_ task is using the FPU for the first time
 * so initialize it and set the mxcsr to its default.
 * remeber the current task has used the FPU.
 */
void init_fpu(void)
{
#if 0
	asm("fninit"); 
	load_mxcsr(0x1f80);
#else
	asm volatile("fxrstor %0" :: "m" (init_fpu_env)); 
#endif
	current->used_math = 1;
}

/*
 * Signal frame handlers.
 */

int save_i387(struct _fpstate *buf)
{
	struct task_struct *tsk = current;
	int err = 0;

	{ 
		extern void bad_user_i387_struct(void); 
		if (sizeof(struct user_i387_struct) != sizeof(tsk->thread.i387.fxsave))
			bad_user_i387_struct();
	} 

	if (!tsk->used_math) 
		return 0;
	tsk->used_math = 0; /* trigger finit */ 
	if (test_thread_flag(TIF_USEDFPU)) { 
		err = save_i387_checking((struct i387_fxsave_struct *)buf);
		if (err) return err;
		stts();
		} else {
		if (__copy_to_user(buf, &tsk->thread.i387.fxsave, 
				   sizeof(struct i387_fxsave_struct)))
			return -1;
	} 
		return 1;
}

/*
 * ptrace request handlers.
 */

int get_fpregs(struct user_i387_struct *buf, struct task_struct *tsk)
{
	empty_fpu(tsk);
	return __copy_to_user((void *)buf, &tsk->thread.i387.fxsave,
			       sizeof(struct user_i387_struct)) ? -EFAULT : 0;
}

int set_fpregs(struct task_struct *tsk, struct user_i387_struct *buf)
{
	if (__copy_from_user(&tsk->thread.i387.fxsave, buf, 
			     sizeof(struct user_i387_struct)))
		return -EFAULT;
		return 0;
}

/*
 * FPU state for core dumps.
 */

int dump_fpu( struct pt_regs *regs, struct user_i387_struct *fpu )
{
	struct task_struct *tsk = current;

	if (!tsk->used_math) 
		return 0;

	unlazy_fpu(tsk);
	memcpy(fpu, &tsk->thread.i387.fxsave, sizeof(struct user_i387_struct)); 
	return 1; 
}
