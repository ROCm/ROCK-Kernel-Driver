/* 
 * X86-64 specific CPU setup.
 * Copyright (C) 1995  Linus Torvalds
 * Copyright 2001, 2002 SuSE Labs / Andi Kleen.
 * See setup.c for older changelog.
 * $Id: setup64.c,v 1.12 2002/03/21 10:09:17 ak Exp $
 */ 
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/bootmem.h>
#include <asm/pda.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/desc.h>
#include <asm/bitops.h>
#include <asm/atomic.h>
#include <asm/mmu_context.h>
#include <asm/smp.h>
#include <asm/i387.h>

char x86_boot_params[2048] __initdata = {0,};

static unsigned long cpu_initialized __initdata = 0;

struct x8664_pda cpu_pda[NR_CPUS] __cacheline_aligned; 

extern void system_call(void); 
extern void ia32_cstar_target(void); 

extern struct task_struct init_task;

extern unsigned char __per_cpu_start[], __per_cpu_end[]; 

struct desc_ptr gdt_descr = { 0 /* filled in */, (unsigned long) gdt_table }; 
struct desc_ptr idt_descr = { 256 * 16, (unsigned long) idt_table }; 

char boot_cpu_stack[IRQSTACKSIZE] __cacheline_aligned;

void __init setup_per_cpu_areas(void)
{ 
	unsigned long size, i;
	unsigned char *ptr;

	/* Copy section for each CPU (we discard the original) */
	size = ALIGN(__per_cpu_end - __per_cpu_start, SMP_CACHE_BYTES);
	if (!size)
		return;

	ptr = alloc_bootmem(size * NR_CPUS);

	for (i = 0; i < NR_CPUS; i++, ptr += size) {
		cpu_pda[cpu_logical_map(i)].cpudata_offset = ptr - __per_cpu_start;
		memcpy(ptr, __per_cpu_start, size);
	}
} 

void pda_init(int cpu)
{ 
        pml4_t *level4;
	struct x8664_pda *pda = &cpu_pda[cpu];

	if (cpu == 0) {
		/* others are initialized in smpboot.c */
		pda->pcurrent = &init_task;
		pda->irqstackptr = boot_cpu_stack; 
		level4 = init_level4_pgt; 
	} else {
		pda->irqstackptr = (char *)
			__get_free_pages(GFP_ATOMIC, IRQSTACK_ORDER);
		if (!pda->irqstackptr)
			panic("cannot allocate irqstack for cpu %d\n", cpu); 
		level4 = (pml4_t *)__get_free_pages(GFP_ATOMIC, 0); 
	}
	if (!level4) 
		panic("Cannot allocate top level page for cpu %d", cpu); 

	pda->level4_pgt = (unsigned long *)level4; 
	if (level4 != init_level4_pgt)
		memcpy(level4, &init_level4_pgt, PAGE_SIZE); 
	set_pml4(level4 + 510, mk_kernel_pml4(__pa_symbol(boot_vmalloc_pgt)));
	asm volatile("movq %0,%%cr3" :: "r" (__pa(level4))); 

	pda->irqstackptr += IRQSTACKSIZE-64;
	pda->cpunumber = cpu; 
	pda->irqcount = -1;
	pda->kernelstack = 
		(unsigned long)stack_thread_info() - PDA_STACKOFFSET + THREAD_SIZE; 
	pda->me = pda;
	pda->cpudata_offset = 0;

	pda->active_mm = &init_mm;
	pda->mmu_state = 0;
	
	asm volatile("movl %0,%%fs ; movl %0,%%gs" :: "r" (0)); 
	wrmsrl(MSR_GS_BASE, cpu_pda + cpu);
} 

#define EXCEPTION_STK_ORDER 0 /* >= N_EXCEPTION_STACKS*EXCEPTION_STKSZ */
char boot_exception_stacks[N_EXCEPTION_STACKS*EXCEPTION_STKSZ];

/*
 * cpu_init() initializes state that is per-CPU. Some data is already
 * initialized (naturally) in the bootstrap process, such as the GDT
 * and IDT. We reload them nevertheless, this function acts as a
 * 'CPU state barrier', nothing should get across.
 * A lot of state is already set up in PDA init.
 */
void __init cpu_init (void)
{
#ifdef CONFIG_SMP
	int nr = stack_smp_processor_id();
#else
	int nr = smp_processor_id();
#endif
	struct tss_struct * t = &init_tss[nr];
	unsigned long v; 
	char *estacks; 
	struct task_struct *me;

	/* CPU 0 is initialised in head64.c */
	if (nr != 0) {
		estacks = (char *)__get_free_pages(GFP_ATOMIC, 0); 
		if (!estacks)
			panic("Can't allocate exception stacks for CPU %d\n",nr);
		pda_init(nr);  
	} else 
		estacks = boot_exception_stacks; 

	me = current;

	if (test_and_set_bit(nr, &cpu_initialized))
		panic("CPU#%d already initialized!\n", nr);

	printk("Initializing CPU#%d\n", nr);

		clear_in_cr4(X86_CR4_VME|X86_CR4_PVI|X86_CR4_TSD|X86_CR4_DE);

	gdt_descr.size = (__u8*) gdt_end - (__u8*)gdt_table; 

	__asm__ __volatile__("lgdt %0": "=m" (gdt_descr));
	__asm__ __volatile__("lidt %0": "=m" (idt_descr));

	/*
	 * Delete NT
	 */

	asm volatile("pushfq ; popq %%rax ; btr $14,%%rax ; pushq %%rax ; popfq" ::: "eax");

	/* 
	 * LSTAR and STAR live in a bit strange symbiosis.
	 * They both write to the same internal register. STAR allows to set CS/DS
	 * but only a 32bit target. LSTAR sets the 64bit rip. 	 
	 */ 
	wrmsrl(MSR_STAR,  ((u64)__USER32_CS)<<48  | ((u64)__KERNEL_CS)<<32); 
	wrmsrl(MSR_LSTAR, system_call); 

#ifdef CONFIG_IA32_EMULATION   		
	wrmsrl(MSR_CSTAR, ia32_cstar_target); 
#endif

	/* Flags to clear on syscall */
	wrmsrl(MSR_SYSCALL_MASK, EF_TF|EF_DF|EF_IE); 

	wrmsrl(MSR_FS_BASE, 0);
	wrmsrl(MSR_KERNEL_GS_BASE, 0);
	barrier(); 

	/*
	 * set up and load the per-CPU TSS
	 */
	estacks += EXCEPTION_STKSZ;
	for (v = 0; v < N_EXCEPTION_STACKS; v++) {
		t->ist[v] = (unsigned long)estacks;
		estacks += EXCEPTION_STKSZ;
	}

	atomic_inc(&init_mm.mm_count);
	me->active_mm = &init_mm;
	if (me->mm)
		BUG();
	enter_lazy_tlb(&init_mm, me, nr);

	set_tss_desc(nr, t);
	load_TR(nr);
	load_LDT(&init_mm.context);

	/*
	 * Clear all 6 debug registers:
	 */

	set_debug(0UL, 0);
	set_debug(0UL, 1);
	set_debug(0UL, 2);
	set_debug(0UL, 3);
	set_debug(0UL, 6);
	set_debug(0UL, 7);

	fpu_init(); 
}
