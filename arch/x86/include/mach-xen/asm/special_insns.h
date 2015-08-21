#ifndef _ASM_X86_SPECIAL_INSNS_H
#define _ASM_X86_SPECIAL_INSNS_H


#ifdef __KERNEL__

#include <asm/barrier.h>
#include <asm/hypervisor.h>
#include <asm/maddr.h>
#include <asm/nops.h>

DECLARE_PER_CPU(unsigned long, xen_x86_cr0);
DECLARE_PER_CPU(unsigned long, xen_x86_cr0_upd);

static inline unsigned long xen_read_cr0_upd(void)
{
	unsigned long upd = raw_cpu_read_l(xen_x86_cr0_upd);
	rmb();
	return upd;
}

static inline void xen_clear_cr0_upd(void)
{
	wmb();
	raw_cpu_write_l(xen_x86_cr0_upd, 0);
}

static inline void xen_clts(void)
{
	if (unlikely(xen_read_cr0_upd()))
		HYPERVISOR_fpu_taskswitch(0);
	else if (raw_cpu_read_4(xen_x86_cr0) & X86_CR0_TS) {
		raw_cpu_write_4(xen_x86_cr0_upd, X86_CR0_TS);
		HYPERVISOR_fpu_taskswitch(0);
		raw_cpu_and_4(xen_x86_cr0, ~X86_CR0_TS);
		xen_clear_cr0_upd();
	}
}

static inline void xen_stts(void)
{
	if (unlikely(xen_read_cr0_upd()))
		HYPERVISOR_fpu_taskswitch(1);
	else if (!(raw_cpu_read_4(xen_x86_cr0) & X86_CR0_TS)) {
		raw_cpu_write_4(xen_x86_cr0_upd, X86_CR0_TS);
		HYPERVISOR_fpu_taskswitch(1);
		raw_cpu_or_4(xen_x86_cr0, X86_CR0_TS);
		xen_clear_cr0_upd();
	}
}

/*
 * Volatile isn't enough to prevent the compiler from reordering the
 * read/write functions for the control registers and messing everything up.
 * A memory clobber would solve the problem, but would prevent reordering of
 * all loads stores around it, which can hurt performance. Solution is to
 * use a variable and mimic reads and writes to it to enforce serialization
 */
extern unsigned long __force_order;

static inline unsigned long native_read_cr0(void)
{
	unsigned long val;
	asm volatile("mov %%cr0,%0\n\t" : "=r" (val), "=m" (__force_order));
	return val;
}

static inline unsigned long xen_read_cr0(void)
{
	return likely(!xen_read_cr0_upd()) ?
	       raw_cpu_read_l(xen_x86_cr0) : native_read_cr0();
}

static inline void native_write_cr0(unsigned long val)
{
	asm volatile("mov %0,%%cr0": : "r" (val), "m" (__force_order));
}

static inline void xen_write_cr0(unsigned long val)
{
	unsigned long upd = val ^ raw_cpu_read_l(xen_x86_cr0);

	if (unlikely(percpu_cmpxchg_op(xen_x86_cr0_upd, 0, upd))) {
		native_write_cr0(val);
		return;
	}
	switch (upd) {
	case 0:
		return;
	case X86_CR0_TS:
		HYPERVISOR_fpu_taskswitch(!!(val & X86_CR0_TS));
		break;
	default:
		native_write_cr0(val);
		break;
	}
	raw_cpu_write_l(xen_x86_cr0, val);
	xen_clear_cr0_upd();
}

#define xen_read_cr2() vcpu_info_read(arch.cr2)
#define xen_write_cr2(val) vcpu_info_write(arch.cr2, val)

static inline unsigned long xen_read_cr3(void)
{
	unsigned long val;
	asm volatile("mov %%cr3,%0\n\t" : "=r" (val), "=m" (__force_order));
#ifdef CONFIG_X86_32
	return mfn_to_pfn(xen_cr3_to_pfn(val)) << PAGE_SHIFT;
#else
	return machine_to_phys(val);
#endif
}

static inline void xen_write_cr3(unsigned long val)
{
#ifdef CONFIG_X86_32
	val = xen_pfn_to_cr3(pfn_to_mfn(val >> PAGE_SHIFT));
#else
	val = phys_to_machine(val);
#endif
	asm volatile("mov %0,%%cr3": : "r" (val), "m" (__force_order));
}

static inline unsigned long xen_read_cr4(void)
{
	unsigned long val;
	asm volatile("mov %%cr4,%0\n\t" : "=r" (val), "=m" (__force_order));
	return val;
}

#define xen_read_cr4_safe() xen_read_cr4()

static inline void xen_write_cr4(unsigned long val)
{
	asm volatile("mov %0,%%cr4": : "r" (val), "m" (__force_order));
}

#ifdef CONFIG_X86_64
static inline unsigned long xen_read_cr8(void)
{
	return 0;
}

static inline void xen_write_cr8(unsigned long val)
{
	BUG_ON(val);
}
#endif

static inline void native_wbinvd(void)
{
	asm volatile("wbinvd": : :"memory");
}

extern void xen_load_gs_index(unsigned);

static inline unsigned long read_cr0(void)
{
	return xen_read_cr0();
}

static inline void write_cr0(unsigned long x)
{
	xen_write_cr0(x);
}

static inline unsigned long read_cr2(void)
{
	return xen_read_cr2();
}

static inline void write_cr2(unsigned long x)
{
	xen_write_cr2(x);
}

static inline unsigned long read_cr3(void)
{
	return xen_read_cr3();
}

static inline void write_cr3(unsigned long x)
{
	xen_write_cr3(x);
}

static inline unsigned long __read_cr4(void)
{
	return xen_read_cr4();
}

static inline unsigned long __read_cr4_safe(void)
{
	return xen_read_cr4_safe();
}

static inline void __write_cr4(unsigned long x)
{
	xen_write_cr4(x);
}

static inline void wbinvd(void)
{
	native_wbinvd();
}

#ifdef CONFIG_X86_64

static inline unsigned long read_cr8(void)
{
	return xen_read_cr8();
}

static inline void write_cr8(unsigned long x)
{
	xen_write_cr8(x);
}

static inline void load_gs_index(unsigned selector)
{
	xen_load_gs_index(selector);
}

#endif

/* Clear the 'TS' bit */
static inline void clts(void)
{
	xen_clts();
}

static inline void stts(void)
{
	xen_stts();
}

static inline void clflush(volatile void *__p)
{
	asm volatile("clflush %0" : "+m" (*(volatile char __force *)__p));
}

static inline void clflushopt(volatile void *__p)
{
	alternative_io(".byte " __stringify(NOP_DS_PREFIX) "; clflush %P0",
		       ".byte 0x66; clflush %P0",
		       X86_FEATURE_CLFLUSHOPT,
		       "+m" (*(volatile char __force *)__p));
}

static inline void clwb(volatile void *__p)
{
	volatile struct { char x[64]; } *p = __p;

	asm volatile(ALTERNATIVE_2(
		".byte " __stringify(NOP_DS_PREFIX) "; clflush (%[pax])",
		".byte 0x66; clflush (%[pax])", /* clflushopt (%%rax) */
		X86_FEATURE_CLFLUSHOPT,
		".byte 0x66, 0x0f, 0xae, 0x30",  /* clwb (%%rax) */
		X86_FEATURE_CLWB)
		: [p] "+m" (*p)
		: [pax] "a" (p));
}

/**
 * pcommit_sfence() - persistent commit and fence
 *
 * The PCOMMIT instruction ensures that data that has been flushed from the
 * processor's cache hierarchy with CLWB, CLFLUSHOPT or CLFLUSH is accepted to
 * memory and is durable on the DIMM.  The primary use case for this is
 * persistent memory.
 *
 * This function shows how to properly use CLWB/CLFLUSHOPT/CLFLUSH and PCOMMIT
 * with appropriate fencing.
 *
 * Example:
 * void flush_and_commit_buffer(void *vaddr, unsigned int size)
 * {
 *         unsigned long clflush_mask = boot_cpu_data.x86_clflush_size - 1;
 *         void *vend = vaddr + size;
 *         void *p;
 *
 *         for (p = (void *)((unsigned long)vaddr & ~clflush_mask);
 *              p < vend; p += boot_cpu_data.x86_clflush_size)
 *                 clwb(p);
 *
 *         // SFENCE to order CLWB/CLFLUSHOPT/CLFLUSH cache flushes
 *         // MFENCE via mb() also works
 *         wmb();
 *
 *         // PCOMMIT and the required SFENCE for ordering
 *         pcommit_sfence();
 * }
 *
 * After this function completes the data pointed to by 'vaddr' has been
 * accepted to memory and will be durable if the 'vaddr' points to persistent
 * memory.
 *
 * PCOMMIT must always be ordered by an MFENCE or SFENCE, so to help simplify
 * things we include both the PCOMMIT and the required SFENCE in the
 * alternatives generated by pcommit_sfence().
 */
static inline void pcommit_sfence(void)
{
	alternative(ASM_NOP7,
		    ".byte 0x66, 0x0f, 0xae, 0xf8\n\t" /* pcommit */
		    "sfence",
		    X86_FEATURE_PCOMMIT);
}

#define nop() asm volatile ("nop")


#endif /* __KERNEL__ */

#endif /* _ASM_X86_SPECIAL_INSNS_H */
