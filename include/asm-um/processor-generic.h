/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __UM_PROCESSOR_GENERIC_H
#define __UM_PROCESSOR_GENERIC_H

struct pt_regs;

struct task_struct;

#include "linux/config.h"
#include "linux/signal.h"
#include "asm/segment.h"
#include "asm/ptrace.h"
#include "asm/siginfo.h"

struct mm_struct;

#define current_text_addr() ((void *) 0)

#define cpu_relax()	do ; while (0)

struct thread_struct {
	int extern_pid;
	int tracing;
	int forking;
	unsigned long kernel_stack;
	int nsyscalls;
	struct pt_regs regs;
	unsigned long cr2;
	int err;
	void *fault_addr;
	void *fault_catcher;
	int vm_seq;
	struct task_struct *prev_sched;
	unsigned long temp_stack;
	int switch_pipe[2];
	void *jmp;
	struct arch_thread arch;
	int singlestep_syscall;
	struct {
		int op;
		union {
			struct {
				int pid;
			} fork, exec;
			struct {
				int (*proc)(void *);
				void *arg;
			} thread;
			struct {
				void (*proc)(void *);
				void *arg;
			} cb;
		} u;
	} request;
};

#define INIT_THREAD \
{ \
	extern_pid:		-1, \
	tracing:		0, \
	forking:		0, \
	kernel_stack:		0, \
	nsyscalls:		0, \
        regs:		   	EMPTY_REGS, \
	cr2:			0, \
	err:			0, \
	fault_addr:		NULL, \
	vm_seq:			0, \
	prev_sched:		NULL, \
	temp_stack:		0, \
	switch_pipe:		{ -1, -1 }, \
	jmp:			NULL, \
	arch:			INIT_ARCH_THREAD, \
	singlestep_syscall:	0, \
	request:		{ 0 } \
}

#define INIT_THREAD_SIZE (4 * PAGE_SIZE)

typedef struct {
	unsigned long seg;
} mm_segment_t;

extern struct task_struct *alloc_task_struct(void);
extern void free_task_struct(struct task_struct *task);

extern void release_thread(struct task_struct *);
extern int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);
extern void dump_thread(struct pt_regs *regs, struct user *u);

static inline void release_segments(struct mm_struct *mm)
{
}

static inline void copy_segments(struct task_struct *p, 
				 struct mm_struct *new_mm)
{
}

#define forget_segments() do ; while(0)

extern unsigned long thread_saved_pc(struct task_struct *t);

#define init_stack	(init_thread_union.stack)

/*
 * User space process size: 3GB (default).
 */
extern unsigned long task_size;

#define TASK_SIZE	(task_size)

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	(0x40000000)

extern void start_thread(struct pt_regs *regs, unsigned long entry, 
			 unsigned long stack);

struct cpuinfo_um {
	unsigned long loops_per_jiffy;
	int ipi_pipe[2];
};

extern struct cpuinfo_um boot_cpu_data;

#define my_cpu_data		cpu_data[smp_processor_id()]

#ifdef CONFIG_SMP
extern struct cpuinfo_um cpu_data[];
#define current_cpu_data cpu_data[smp_processor_id()]
#else
#define cpu_data (&boot_cpu_data)
#define current_cpu_data boot_cpu_data
#endif

#define KSTK_EIP(tsk) (PT_REGS_IP(&tsk->thread.regs))
#define KSTK_ESP(tsk) (PT_REGS_SP(&tsk->thread.regs))
#define get_wchan(p) (0)

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
