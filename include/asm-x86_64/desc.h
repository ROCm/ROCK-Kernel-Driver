/* Written 2000 by Andi Kleen */ 
#ifndef __ARCH_DESC_H
#define __ARCH_DESC_H

#include <linux/threads.h>
#include <asm/ldt.h>

#ifndef __ASSEMBLY__

/* Keep this syncronized with kernel/head.S */
#define TSS_START (8 * 8)
#define LDT_START (TSS_START + 16) 

#define __TSS(n)  (TSS_START + (n)*64)
#define __LDT(n)  (LDT_START + (n)*64)

extern __u8 tss_start[]; 
extern __u8 gdt_table[];
extern __u8 gdt_end[];

enum { 
	GATE_INTERRUPT = 0xE, 
	GATE_TRAP = 0xF, 	
	GATE_CALL = 0xC,
}; 	

// 16byte gate
struct gate_struct {          
	u16 offset_low;
	u16 segment; 
	unsigned ist : 3, zero0 : 5, type : 5, dpl : 2, p : 1;
	u16 offset_middle;
	u32 offset_high;
	u32 zero1; 
} __attribute__((packed));

#define PTR_LOW(x) ((unsigned long)(x) & 0xFFFF) 
#define PTR_MIDDLE(x) (((unsigned long)(x) >> 16) & 0xFFFF)
#define PTR_HIGH(x) ((unsigned long)(x) >> 32)

// 8 byte segment descriptor
struct desc_struct { 
	u16 limit0;
	u16 base0;
	unsigned base1 : 8, type : 4, s : 1, dpl : 2, p : 1;
	unsigned limit : 4, avl : 1, l : 1, d : 1, g : 1, base2 : 8;
} __attribute__((packed)); 

enum { 
	DESC_TSS = 0x9,
	DESC_LDT = 0x2,
}; 

// LDT or TSS descriptor in the GDT. 16 bytes.
struct ldttss_desc { 
	u16 limit0;
	u16 base0;
	unsigned base1 : 8, type : 5, dpl : 2, p : 1;
	unsigned limit1 : 4, zero0 : 3, g : 1, base2 : 8;
	u32 base3;
	u32 zero1; 
} __attribute__((packed)); 

struct desc_ptr {
	unsigned short size;
	unsigned long address;
} __attribute__((packed)) ;

#define load_TR(n) asm volatile("ltr %w0"::"r" (__TSS(n)))
#define __load_LDT(n) asm volatile("lldt %w0"::"r" (__LDT(n)))
#define clear_LDT(n)  asm volatile("lldt %w0"::"r" (0))

/*
 * This is the ldt that every process will get unless we need
 * something other than this.
 */
extern struct desc_struct default_ldt[];
extern struct gate_struct idt_table[]; 

static inline void _set_gate(void *adr, unsigned type, unsigned long func, unsigned dpl, unsigned ist)  
{
	struct gate_struct s; 	
	s.offset_low = PTR_LOW(func); 
	s.segment = __KERNEL_CS;
	s.ist = ist; 
	s.p = 1;
	s.dpl = dpl; 
	s.zero0 = 0;
	s.zero1 = 0; 
	s.type = type; 
	s.offset_middle = PTR_MIDDLE(func); 
	s.offset_high = PTR_HIGH(func); 
	/* does not need to be atomic because it is only done once at setup time */ 
	memcpy(adr, &s, 16); 
} 

static inline void set_intr_gate(int nr, void *func) 
{ 
	_set_gate(&idt_table[nr], GATE_INTERRUPT, (unsigned long) func, 3, 0); 
} 

static inline void set_intr_gate_ist(int nr, void *func, unsigned ist) 
{ 
	_set_gate(&idt_table[nr], GATE_INTERRUPT, (unsigned long) func, 3, ist); 
} 

static inline void set_system_gate(int nr, void *func) 
{ 
	_set_gate(&idt_table[nr], GATE_INTERRUPT, (unsigned long) func, 3, 0); 
} 

static inline void set_tssldt_descriptor(void *ptr, unsigned long tss, unsigned type, 
					 unsigned size) 
{ 
	struct ldttss_desc d;
	memset(&d,0,sizeof(d)); 
	d.limit0 = size & 0xFFFF;
	d.base0 = PTR_LOW(tss); 
	d.base1 = PTR_MIDDLE(tss) & 0xFF; 
	d.type = type;
	d.p = 1; 
	d.limit1 = 0xF;
	d.base2 = (PTR_MIDDLE(tss) >> 8) & 0xFF; 
	d.base3 = PTR_HIGH(tss); 
	memcpy(ptr, &d, 16); 
}

static inline void set_tss_desc(unsigned n, void *addr)
{ 
	set_tssldt_descriptor((__u8*)gdt_table + __TSS(n), (unsigned long)addr, 
			      DESC_TSS,
			      sizeof(struct tss_struct)); 
} 

static inline void set_ldt_desc(unsigned n, void *addr, int size)
{ 
	set_tssldt_descriptor((__u8*)gdt_table + __LDT(n), (unsigned long)addr, 
			      DESC_LDT, size); 
}

/*
 * load one particular LDT into the current CPU
 */
extern inline void load_LDT (mm_context_t *pc)
{
	int cpu = smp_processor_id();
	int count = pc->size;

	if (!count) {
		clear_LDT(cpu);
		return;
	}
		
	set_ldt_desc(cpu, pc->ldt, count);
	__load_LDT(cpu);
}

#endif /* !__ASSEMBLY__ */

#endif
