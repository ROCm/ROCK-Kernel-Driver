#ifndef _ASM_X86_SPECIAL_INSNS_H
#define _ASM_X86_SPECIAL_INSNS_H


#ifdef __KERNEL__

#include <asm/hypervisor.h>
#include <asm/maddr.h>

static inline void xen_clts(void)
{
	HYPERVISOR_fpu_taskswitch(0);
}

static inline void xen_stts(void)
{
	HYPERVISOR_fpu_taskswitch(1);
}

/*
 * Volatile isn't enough to prevent the compiler from reordering the
 * read/write functions for the control registers and messing everything up.
 * A memory clobber would solve the problem, but would prevent reordering of
 * all loads stores around it, which can hurt performance. Solution is to
 * use a variable and mimic reads and writes to it to enforce serialization
 */
static unsigned long __force_order;

static inline unsigned long xen_read_cr0(void)
{
	unsigned long val;
	asm volatile("mov %%cr0,%0\n\t" : "=r" (val), "=m" (__force_order));
	return val;
}

static inline void xen_write_cr0(unsigned long val)
{
	asm volatile("mov %0,%%cr0": : "r" (val), "m" (__force_order));
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

static inline unsigned long read_cr4(void)
{
	return xen_read_cr4();
}

static inline unsigned long read_cr4_safe(void)
{
	return xen_read_cr4_safe();
}

static inline void write_cr4(unsigned long x)
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

#define nop() asm volatile ("nop")


#endif /* __KERNEL__ */

#endif /* _ASM_X86_SPECIAL_INSNS_H */
