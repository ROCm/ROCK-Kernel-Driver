/* 
 * X86-64 specific setup part.
 * Copyright (C) 1995  Linus Torvalds
 * Copyright 2001 2002 SuSE Labs / Andi Kleen.
 * See setup.c for older changelog.
 */ 
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/pda.h>
#include <asm/pda.h>
#include <asm/processor.h>
#include <asm/desc.h>
#include <asm/bitops.h>
#include <asm/atomic.h>
#include <asm/mmu_context.h>

char x86_boot_params[2048] __initdata = {0,};

static unsigned long cpu_initialized __initdata = 0;

struct x8664_pda cpu_pda[NR_CPUS] __cacheline_aligned; 

extern void system_call(void); 
extern void ia32_cstar_target(void); 

struct desc_ptr gdt_descr = { 0 /* filled in */, (unsigned long) gdt_table }; 
struct desc_ptr idt_descr = { 256 * 16, (unsigned long) idt_table }; 

void pda_init(int cpu)
{ 
	cpu_pda[cpu].me = &cpu_pda[cpu]; 
	cpu_pda[cpu].cpunumber = cpu; 
	cpu_pda[cpu].irqcount = -1;
	cpu_pda[cpu].irqstackptr = cpu_pda[cpu].irqstack + sizeof(cpu_pda[0].irqstack);
	/* others are initialized in smpboot.c */
	if (cpu == 0) {
		cpu_pda[cpu].pcurrent = &init_task;
		cpu_pda[cpu].kernelstack = 
			(unsigned long)&init_thread_union+THREAD_SIZE-PDA_STACKOFFSET;
	}
	asm volatile("movl %0,%%gs ; movl %0,%%fs" :: "r" (0)); 
	wrmsrl(MSR_GS_BASE, cpu_pda + cpu);
} 

/*
 * cpu_init() initializes state that is per-CPU. Some data is already
 * initialized (naturally) in the bootstrap process, such as the GDT
 * and IDT. We reload them nevertheless, this function acts as a
 * 'CPU state barrier', nothing should get across.
 */
void __init cpu_init (void)
{
#ifdef CONFIG_SMP
	int nr = current_thread_info()->cpu;
#else
	int nr = smp_processor_id();
#endif
	struct tss_struct * t = &init_tss[nr];
	unsigned long v; 

	/* CPU 0 is initialised in head64.c */
	if (nr != 0) 
		pda_init(nr);  

	if (test_and_set_bit(nr, &cpu_initialized)) {
		printk("CPU#%d already initialized!\n", nr);
		for (;;) __sti();
	}
	printk("Initializing CPU#%d\n", nr);

	if (cpu_has_vme || cpu_has_tsc || cpu_has_de)
		clear_in_cr4(X86_CR4_VME|X86_CR4_PVI|X86_CR4_TSD|X86_CR4_DE);

	gdt_descr.size = (__u8*) gdt_end - (__u8*)gdt_table; 

	__asm__ __volatile__("lgdt %0": "=m" (gdt_descr));
	__asm__ __volatile__("lidt %0": "=m" (idt_descr));

	/*
	 * Delete NT
	 */

	__asm__ volatile("pushfq ; popq %%rax ; btr $14,%%rax ; pushq %%rax ; popfq" :: : "eax");

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

	rdmsrl(MSR_EFER, v); 
	wrmsrl(MSR_EFER, v|1); 
	
	/* Flags to clear on syscall */
	wrmsrl(MSR_SYSCALL_MASK, EF_TF|EF_DF|EF_IE); 


	wrmsrl(MSR_FS_BASE, 0);
	wrmsrl(MSR_KERNEL_GS_BASE, 0);
	barrier(); 

	/*
	 * set up and load the per-CPU TSS and LDT
	 */
	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;
	if(current->mm)
		BUG();
	enter_lazy_tlb(&init_mm, current, nr);

	set_tssldt_descriptor((__u8 *)tss_start + (nr*16), (unsigned long) t, 
			      DESC_TSS, 
			      offsetof(struct tss_struct, io_bitmap)); 
	load_TR(nr);
	load_LDT(&init_mm);

	/*
	 * Clear all 6 debug registers:
	 */

	set_debug(0UL, 0);
	set_debug(0UL, 1);
	set_debug(0UL, 2);
	set_debug(0UL, 3);
	set_debug(0UL, 6);
	set_debug(0UL, 7);

	/*
	 * Force FPU initialization:
	 */
	clear_thread_flag(TIF_USEDFPU); 
	current->used_math = 0;
	stts();
}
