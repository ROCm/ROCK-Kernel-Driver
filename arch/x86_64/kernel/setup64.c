/* 
 * X86-64 specific CPU setup.
 * Copyright (C) 1995  Linus Torvalds
 * Copyright 2001, 2002, 2003 SuSE Labs / Andi Kleen.
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
#include <asm/percpu.h>
#include <asm/mtrr.h>
#include <asm/proto.h>

char x86_boot_params[2048] __initdata = {0,};

unsigned long cpu_initialized __initdata = 0;

struct x8664_pda cpu_pda[NR_CPUS] __cacheline_aligned; 

extern struct task_struct init_task;

extern unsigned char __per_cpu_start[], __per_cpu_end[]; 

extern struct desc_ptr cpu_gdt_descr[];
struct desc_ptr idt_descr = { 256 * 16, (unsigned long) idt_table }; 

char boot_cpu_stack[IRQSTACKSIZE] __cacheline_aligned;

unsigned long __supported_pte_mask = ~0UL;
static int do_not_nx = 1;

static int __init nonx_setup(char *str)
{
        if (!strncmp(str,"off",3)) { 
                __supported_pte_mask &= ~_PAGE_NX; 
                do_not_nx = 1; 
        } else if (!strncmp(str, "on",3)) { 
                do_not_nx = 0; 
                __supported_pte_mask |= _PAGE_NX; 
        } 
        return 1;
} 

__setup("noexec=", nonx_setup); 

#ifndef  __GENERIC_PER_CPU

unsigned long __per_cpu_offset[NR_CPUS];

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
		/* hide this from the compiler to avoid problems */ 
		unsigned long offset;
		asm("subq %[b],%0" : "=r" (offset) : "0" (ptr), [b] "r" (&__per_cpu_start));
		__per_cpu_offset[i] = offset;
		cpu_pda[i].cpudata_offset = offset;
		memcpy(ptr, __per_cpu_start, size);
	}
} 
#endif

void pda_init(int cpu)
{ 
        pml4_t *level4;
	struct x8664_pda *pda = &cpu_pda[cpu];

	/* Setup up data that may be needed in __get_free_pages early */
	asm volatile("movl %0,%%fs ; movl %0,%%gs" :: "r" (0)); 
	wrmsrl(MSR_GS_BASE, cpu_pda + cpu);

	pda->me = pda;
	pda->cpunumber = cpu; 
	pda->irqcount = -1;
	pda->cpudata_offset = 0;
	pda->kernelstack = 
		(unsigned long)stack_thread_info() - PDA_STACKOFFSET + THREAD_SIZE; 
	pda->active_mm = &init_mm;
	pda->mmu_state = 0;

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
} 

#define EXCEPTION_STK_ORDER 0 /* >= N_EXCEPTION_STACKS*EXCEPTION_STKSZ */
char boot_exception_stacks[N_EXCEPTION_STACKS*EXCEPTION_STKSZ];

void syscall_init(void)
{
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
	wrmsrl(MSR_SYSCALL_MASK, EF_TF|EF_DF|EF_IE|0x3000); 
}

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
	int cpu = stack_smp_processor_id();
#else
	int cpu = smp_processor_id();
#endif
	struct tss_struct * t = &init_tss[cpu];
	unsigned long v, efer; 
	char *estacks; 
	struct task_struct *me;

	/* CPU 0 is initialised in head64.c */
	if (cpu != 0) {
		pda_init(cpu);
		estacks = (char *)__get_free_pages(GFP_ATOMIC, 0); 
		if (!estacks)
			panic("Can't allocate exception stacks for CPU %d\n",cpu);
	} else 
		estacks = boot_exception_stacks; 

	me = current;

	if (test_and_set_bit(cpu, &cpu_initialized))
		panic("CPU#%d already initialized!\n", cpu);

	printk("Initializing CPU#%d\n", cpu);

		clear_in_cr4(X86_CR4_VME|X86_CR4_PVI|X86_CR4_TSD|X86_CR4_DE);

	/*
	 * Initialize the per-CPU GDT with the boot GDT,
	 * and set up the GDT descriptor:
	 */
	if (cpu) {
		memcpy(cpu_gdt_table[cpu], cpu_gdt_table[0], GDT_SIZE);
	}	

	cpu_gdt_descr[cpu].size = GDT_SIZE;
	cpu_gdt_descr[cpu].address = (unsigned long)cpu_gdt_table[cpu];
	__asm__ __volatile__("lgdt %0": "=m" (cpu_gdt_descr[cpu]));
	__asm__ __volatile__("lidt %0": "=m" (idt_descr));

	memcpy(me->thread.tls_array, cpu_gdt_table[cpu], GDT_ENTRY_TLS_ENTRIES * 8);

	/*
	 * Delete NT
	 */

	asm volatile("pushfq ; popq %%rax ; btr $14,%%rax ; pushq %%rax ; popfq" ::: "eax");

	syscall_init();

	wrmsrl(MSR_FS_BASE, 0);
	wrmsrl(MSR_KERNEL_GS_BASE, 0);
	barrier(); 

	rdmsrl(MSR_EFER, efer); 
        if (!(efer & EFER_NX) || do_not_nx) { 
                __supported_pte_mask &= ~_PAGE_NX; 
        }       

	/*
	 * set up and load the per-CPU TSS
	 */
	estacks += EXCEPTION_STKSZ;
	for (v = 0; v < N_EXCEPTION_STACKS; v++) {
		t->ist[v] = (unsigned long)estacks;
		estacks += EXCEPTION_STKSZ;
	}

	t->io_map_base = INVALID_IO_BITMAP_OFFSET;

	atomic_inc(&init_mm.mm_count);
	me->active_mm = &init_mm;
	if (me->mm)
		BUG();
	enter_lazy_tlb(&init_mm, me, cpu);

	set_tss_desc(cpu, t);
	load_TR_desc();
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
