#ifndef __ARCH_DESC_H
#define __ARCH_DESC_H

#include <asm/ldt.h>

/*
 * The layout of the per-CPU GDT under Linux:
 *
 *   0 - null
 *   1 - Thread-Local Storage (TLS) segment
 *   2 - kernel code segment
 *   3 - kernel data segment
 *   4 - user code segment		<==== new cacheline
 *   5 - user data segment
 *   6 - TSS
 *   7 - LDT
 *   8 - APM BIOS support		<==== new cacheline
 *   9 - APM BIOS support
 *  10 - APM BIOS support
 *  11 - APM BIOS support
 *  12 - PNPBIOS support		<==== new cacheline
 *  13 - PNPBIOS support
 *  14 - PNPBIOS support
 *  15 - PNPBIOS support
 *  16 - PNPBIOS support		<==== new cacheline
 *  17 - not used
 *  18 - not used
 *  19 - not used
 */
#define TLS_ENTRY 1
#define TSS_ENTRY 6
#define LDT_ENTRY 7
/*
 * The interrupt descriptor table has room for 256 idt's,
 * the global descriptor table is dependent on the number
 * of tasks we can have..
 *
 * We pad the GDT to cacheline boundary.
 */
#define IDT_ENTRIES 256
#define GDT_ENTRIES 20

#ifndef __ASSEMBLY__

#include <asm/mmu.h>

#define GDT_SIZE (GDT_ENTRIES*sizeof(struct desc_struct))

extern struct desc_struct cpu_gdt_table[NR_CPUS][GDT_ENTRIES];

struct Xgt_desc_struct {
	unsigned short size;
	unsigned long address __attribute__((packed));
} __attribute__ ((packed));

extern struct Xgt_desc_struct idt_descr, cpu_gdt_descr[NR_CPUS];

#define load_TR_desc() __asm__ __volatile__("ltr %%ax"::"a" (TSS_ENTRY<<3))
#define load_LDT_desc() __asm__ __volatile__("lldt %%ax"::"a" (LDT_ENTRY<<3))

/*
 * This is the ldt that every process will get unless we need
 * something other than this.
 */
extern struct desc_struct default_ldt[];
extern void set_intr_gate(unsigned int irq, void * addr);

#define _set_tssldt_desc(n,addr,limit,type) \
__asm__ __volatile__ ("movw %w3,0(%2)\n\t" \
	"movw %%ax,2(%2)\n\t" \
	"rorl $16,%%eax\n\t" \
	"movb %%al,4(%2)\n\t" \
	"movb %4,5(%2)\n\t" \
	"movb $0,6(%2)\n\t" \
	"movb %%ah,7(%2)\n\t" \
	"rorl $16,%%eax" \
	: "=m"(*(n)) : "a" (addr), "r"(n), "ir"(limit), "i"(type))

static inline void set_tss_desc(unsigned int cpu, void *addr)
{
	_set_tssldt_desc(&cpu_gdt_table[cpu][TSS_ENTRY], (int)addr, 235, 0x89);
}

static inline void set_ldt_desc(unsigned int cpu, void *addr, unsigned int size)
{
	_set_tssldt_desc(&cpu_gdt_table[cpu][LDT_ENTRY], (int)addr, ((size << 3)-1), 0x82);
}

#define TLS_FLAGS_MASK			0x00000001

#define TLS_FLAG_WRITABLE		0x00000001

static inline void load_TLS_desc(struct thread_struct *t, unsigned int cpu)
{
	cpu_gdt_table[cpu][TLS_ENTRY] = t->tls_desc;
}

static inline void clear_LDT(void)
{
	set_ldt_desc(smp_processor_id(), &default_ldt[0], 5);
	load_LDT_desc();
}

/*
 * load one particular LDT into the current CPU
 */
static inline void load_LDT (mm_context_t *pc)
{
	void *segments = pc->ldt;
	int count = pc->size;

	if (likely(!count)) {
		segments = &default_ldt[0];
		count = 5;
	}
		
	set_ldt_desc(smp_processor_id(), segments, count);
	load_LDT_desc();
}

#endif /* !__ASSEMBLY__ */

#endif
