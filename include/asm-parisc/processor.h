/*
 * include/asm-parisc/processor.h
 *
 * Copyright (C) 1994 Linus Torvalds
 */

#ifndef __ASM_PARISC_PROCESSOR_H
#define __ASM_PARISC_PROCESSOR_H

#ifndef __ASSEMBLY__
#include <linux/threads.h>

#include <asm/hardware.h>
#include <asm/page.h>
#include <asm/pdc.h>
#include <asm/ptrace.h>
#include <asm/types.h>
#endif /* __ASSEMBLY__ */

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */

/* We cannot use MFIA as it was added for PA2.0 - prumpf

   At one point there were no "0f/0b" type local symbols in gas for
   PA-RISC.  This is no longer true, but this still seems like the
   nicest way to implement this. */

#define current_text_addr() ({ void *pc; __asm__("\n\tblr 0,%0\n\tnop":"=r" (pc)); pc; })

#define TASK_SIZE	    (PAGE_OFFSET)
#define TASK_UNMAPPED_BASE  (TASK_SIZE / 3)

#ifndef __ASSEMBLY__

/*
** Data detected about CPUs at boot time which is the same for all CPU's.
** HP boxes are SMP - ie identical processors.
**
** FIXME: some CPU rev info may be processor specific...
*/
struct system_cpuinfo_parisc {
	unsigned int	cpu_count;
	unsigned int	cpu_hz;
	unsigned int	hversion;
	unsigned int	sversion;
	enum cpu_type	cpu_type;

	struct {
		struct pdc_model model;
		struct pdc_model_cpuid /* ARGH */ versions;
		struct pdc_model_cpuid cpuid;
#if 0
		struct pdc_model_caps caps;
#endif
		char   sys_model_name[81]; /* PDC-ROM returnes this model name */
	} pdc;

	char	 	*model_name;
	char		*cpu_name;
	char		*family_name;
};


/*
** Per CPU data structure - ie varies per CPU.
*/
struct cpuinfo_parisc {
	unsigned cpuid;

	struct irq_region *region;

	unsigned long it_value; /* Interval Timer value at last timer interrupt */
	unsigned long it_delta; /* Interval Timer delta (tic_10ms / HZ * 100) */

	unsigned long hpa;	/* Host Physical address */
	unsigned long txn_addr;	/* External Interrupt Register or id_eid */

	unsigned long bh_count;		/* number of times bh was invoked */
	unsigned long irq_count;	/* number of IRQ's since boot */
	unsigned long irq_max_cr16;	/* longest time to handle a single IRQ */
};

extern struct system_cpuinfo_parisc boot_cpu_data;
extern struct cpuinfo_parisc cpu_data[NR_CPUS];
#define current_cpu_data cpu_data[smp_processor_id()]

extern void identify_cpu(struct cpuinfo_parisc *);

#define EISA_bus 0 /* we don't have ISA support yet */
#define EISA_bus__is_a_macro /* for versions in ksyms.c */
#define MCA_bus 0
#define MCA_bus__is_a_macro /* for versions in ksyms.c */

typedef struct {
	int seg;  
} mm_segment_t;

struct thread_struct {
	struct pt_regs regs;
	unsigned long  pg_tables;
	unsigned long  flags;
}; 

/* Thread struct flags. */
#define PARISC_KERNEL_DEATH	(1UL << 31)	/* see die_if_kernel()... */

#define INIT_MMAP { &init_mm, 0, 0, NULL, PAGE_SHARED, \
		    VM_READ | VM_WRITE | VM_EXEC, 1, NULL, NULL }

#define INIT_THREAD { {			\
	{ 0, 0, 0, 0, 0, 0, 0, 0,	\
	  0, 0, 0, 0, 0, 0, 0, 0,	\
	  0, 0, 0, 0, 0, 0, 0, 0,	\
	  0, 0, 0, 0, 0, 0, 0, 0 },	\
	{ 0, 0, 0, 0, 0, 0, 0, 0,	\
	  0, 0, 0, 0, 0, 0, 0, 0,	\
	  0, 0, 0, 0, 0, 0, 0, 0,	\
	  0, 0, 0, 0, 0, 0, 0, 0 },	\
	{ 0, 0, 0, 0, 0, 0, 0, 0 },	\
	{ 0, 0}, { 0, 0}, 0, 0, 0, 0	\
	}, __pa((unsigned long) swapper_pg_dir) }

/*
 * Return saved PC of a blocked thread.  This is used by ps mostly.
 */

extern inline unsigned long thread_saved_pc(struct thread_struct *t)
{
	return 0xabcdef;
}

/*
 * Start user thread in another space.
 *
 * Note that we set both the iaoq and r31 to the new pc. When
 * the kernel initially calls execve it will return through an
 * rfi path that will use the values in the iaoq. The execve
 * syscall path will return through the gateway page, and
 * that uses r31 to branch to.
 *
 * For ELF we clear r23, because the dynamic linker uses it to pass
 * the address of the finalizer function.
 *
 * We also initialize sr3 to an illegal value (illegal for our
 * implementation, not for the architecture).
 */

/* The ELF abi wants things done a "wee bit" differently than
 * som does.  Supporting this behavior here avoids
 * having our own version of create_elf_tables.
 *
 * Oh, and yes, that is not a typo, we are really passing argc in r25
 * and argv in r24 (rather than r26 and r25).  This is because that's
 * where __libc_start_main wants them.
 *
 * Duplicated from dl-machine.h for the benefit of readers:
 *
 *  Our initial stack layout is rather different from everyone else's
 *  due to the unique PA-RISC ABI.  As far as I know it looks like
 *  this:

   -----------------------------------  (user startup code creates this frame)
   |         32 bytes of magic       |
   |---------------------------------|
   | 32 bytes argument/sp save area  |
   |---------------------------------|  ((current->mm->env_end) + 63 & ~63)
   |         N bytes of slack        |
   |---------------------------------|
   |      envvar and arg strings     |
   |---------------------------------|
   |	    ELF auxiliary info	     |
   |         (up to 28 words)        |
   |---------------------------------|
   |  Environment variable pointers  |
   |         upwards to NULL	     |
   |---------------------------------|
   |        Argument pointers        |
   |         upwards to NULL	     |
   |---------------------------------|
   |          argc (1 word)          |
   -----------------------------------

 *  The pleasant part of this is that if we need to skip arguments we
 *  can just decrement argc and move argv, because the stack pointer
 *  is utterly unrelated to the location of the environment and
 *  argument vectors.
 *
 * Note that the S/390 people took the easy way out and hacked their
 * GCC to make the stack grow downwards.  */

#define start_thread_som(regs, new_pc, new_sp) do {		\
	unsigned long *sp = (unsigned long *)new_sp;	\
	__u32 spaceid = (__u32)current->mm->context;	\
	unsigned long pc = (unsigned long)new_pc;	\
	/* offset pc for priv. level */			\
	pc |= 3;					\
							\
	set_fs(USER_DS);				\
	regs->iasq[0] = spaceid;			\
	regs->iasq[1] = spaceid;			\
	regs->iaoq[0] = pc;				\
	regs->iaoq[1] = pc;				\
	regs->sr[2] = LINUX_GATEWAY_SPACE;              \
	regs->sr[3] = 0xffff;				\
	regs->sr[4] = spaceid;				\
	regs->sr[5] = spaceid;				\
	regs->sr[6] = spaceid;				\
	regs->sr[7] = spaceid;				\
	regs->gr[ 0] = USER_INIT_PSW;			\
	regs->gr[30] = ((new_sp)+63)&~63;		\
	regs->gr[31] = pc;				\
							\
	get_user(regs->gr[26],&sp[0]);			\
	get_user(regs->gr[25],&sp[-1]); 		\
	get_user(regs->gr[24],&sp[-2]); 		\
	get_user(regs->gr[23],&sp[-3]); 		\
							\
	regs->cr30 = (u32) current;			\
} while(0)


#define start_thread(regs, new_pc, new_sp) do {		\
	unsigned long *sp = (unsigned long *)new_sp;	\
	__u32 spaceid = (__u32)current->mm->context;	\
        unsigned long pc = (unsigned long)new_pc;       \
        /* offset pc for priv. level */                 \
        pc |= 3;                                        \
							\
							\
	set_fs(USER_DS);				\
	regs->iasq[0] = spaceid;			\
	regs->iasq[1] = spaceid;			\
	regs->iaoq[0] = pc;				\
	regs->iaoq[1] = pc; 	               		\
	regs->sr[2] = LINUX_GATEWAY_SPACE;              \
	regs->sr[3] = 0xffff;				\
	regs->sr[4] = spaceid;				\
	regs->sr[5] = spaceid;				\
	regs->sr[6] = spaceid;				\
	regs->sr[7] = spaceid;				\
	regs->gr[ 0] = USER_INIT_PSW;			\
	regs->fr[ 0] = 0LL;                            	\
	regs->fr[ 1] = 0LL;                            	\
	regs->fr[ 2] = 0LL;                            	\
	regs->fr[ 3] = 0LL;                            	\
	regs->gr[30] = ((current->mm->env_end)+63)&~63;	\
	regs->gr[31] = pc;				\
							\
	get_user(regs->gr[25],&sp[0]);			\
	regs->gr[24] = (unsigned long) &sp[1];		\
	regs->gr[23] = 0;                            	\
							\
	regs->cr30 = (u32) current;			\
} while(0)

#ifdef __LP64__

/*
 * For 64 bit kernels we need a version of start thread for 32 bit
 * elf files.
 *
 * FIXME: It should be possible to not duplicate the above code
 *        by playing games with concatenation to form both
 *        macros at compile time. The only difference between
 *        this macro and the above is the name and the types
 *        for sp and pc.
 */

#define start_thread32(regs, new_pc, new_sp) do {         \
	__u32 *sp = (__u32 *)new_sp;                    \
	__u32 spaceid = (__u32)current->mm->context;	\
	__u32 pc = (__u32)new_pc;                       \
        /* offset pc for priv. level */                 \
        pc |= 3;                                        \
							\
	set_fs(USER_DS);				\
	regs->iasq[0] = spaceid;			\
	regs->iasq[1] = spaceid;			\
	regs->iaoq[0] = pc;				\
	regs->iaoq[1] = pc; 	               		\
	regs->sr[2] = LINUX_GATEWAY_SPACE;              \
	regs->sr[3] = 0xffff;				\
	regs->sr[4] = spaceid;				\
	regs->sr[5] = spaceid;				\
	regs->sr[6] = spaceid;				\
	regs->sr[7] = spaceid;				\
	regs->gr[ 0] = USER_INIT_PSW;			\
	regs->fr[ 0] = 0LL;                            	\
	regs->fr[ 1] = 0LL;                            	\
	regs->fr[ 2] = 0LL;                            	\
	regs->fr[ 3] = 0LL;                            	\
	regs->gr[30] = ((current->mm->env_end)+63)&~63;	\
	regs->gr[31] = pc;				\
							\
	get_user(regs->gr[25],&sp[0]);			\
	regs->gr[24] = (unsigned long) &sp[1];		\
	regs->gr[23] = 0;                            	\
							\
	regs->cr30 = (u32) current;			\
} while(0)

#endif

struct task_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);
extern int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);

#define copy_segments(tsk, mm)	do { } while (0)
#define release_segments(mm)	do { } while (0)

extern inline unsigned long get_wchan(struct task_struct *p)
{
	return 0xdeadbeef; /* XXX */
}

#define KSTK_EIP(tsk)	(0xdeadbeef)
#define KSTK_ESP(tsk)	(0xdeadbeef)

/* Be sure to hunt all references to this down when you change the size of
 * the kernel stack */

#endif /* __ASSEMBLY__ */

#define THREAD_SIZE	(4*PAGE_SIZE)

#define alloc_task_struct() \
	((struct task_struct *) __get_free_pages(GFP_KERNEL,2))
#define free_task_struct(p)     free_pages((unsigned long)(p),2)
#define get_task_struct(tsk)      atomic_inc(&virt_to_page(tsk)->count)

#define init_task (init_task_union.task) 
#define init_stack (init_task_union.stack)


#endif /* __ASM_PARISC_PROCESSOR_H */
