#ifndef X86_64_PDA_H
#define X86_64_PDA_H

#include <linux/stddef.h>
#ifndef ASM_OFFSET_H
#include <asm/offset.h>
#endif
#include <linux/cache.h>

struct task_struct; 

/* Per processor datastructure. %gs points to it while the kernel runs */ 
/* To use a new field with the *_pda macros it needs to be added to tools/offset.c */
struct x8664_pda {
	struct x8664_pda *me; 
	unsigned long kernelstack;  /* TOS for current process */ 
	unsigned long oldrsp; 	    /* user rsp for system call */
	unsigned long irqrsp;	    /* Old rsp for interrupts. */ 
	struct task_struct *pcurrent;	/* Current process */
        int irqcount;		    /* Irq nesting counter. Starts with -1 */  	
	int cpunumber;		    /* Logical CPU number */
	char *irqstackptr;	  
	unsigned int __softirq_pending;
	unsigned int __local_irq_count;
	unsigned int __local_bh_count;
	unsigned int __nmi_count;	/* arch dependent */
	struct task_struct * __ksoftirqd_task; /* waitqueue is too large */
	char irqstack[16 * 1024];   /* Stack used by interrupts */     
} ____cacheline_aligned;

#define PDA_STACKOFFSET (5*8)

extern struct x8664_pda cpu_pda[];

/* 
 * There is no fast way to get the base address of the PDA, all the accesses
 * have to mention %fs/%gs.  So it needs to be done this Torvaldian way.
 */ 
#define sizeof_field(type,field)  (sizeof(((type *)0)->field))
#define typeof_field(type,field)  typeof(((type *)0)->field)
#ifndef __STR
#define __STR(x) #x
#endif
#define __STR2(x) __STR(x) 

extern void __bad_pda_field(void);

#define pda_to_op(op,field,val) do { \
       switch (sizeof_field(struct x8664_pda, field)) { 		\
       case 2: asm volatile(op "w %0,%%gs:" __STR2(pda_ ## field) ::"r" (val):"memory"); break;	\
       case 4: asm volatile(op "l %0,%%gs:" __STR2(pda_ ## field) ::"r" (val):"memory"); break;	\
       case 8: asm volatile(op "q %0,%%gs:" __STR2(pda_ ## field) ::"r" (val):"memory"); break;	\
       default: __bad_pda_field(); 					\
       } \
       } while (0)


#define pda_from_op(op,field) ({ \
       typedef typeof_field(struct x8664_pda, field) T__; T__ ret__; \
       switch (sizeof_field(struct x8664_pda, field)) { 		\
       case 2: asm volatile (op "w %%gs:" __STR2(pda_ ## field) ",%0":"=r" (ret__)::"memory"); break;	\
       case 4: asm volatile (op "l %%gs:" __STR2(pda_ ## field) ",%0":"=r" (ret__)::"memory"); break;	\
       case 8: asm volatile (op "q %%gs:" __STR2(pda_ ## field) ",%0":"=r" (ret__)::"memory"); break;	\
       default: __bad_pda_field(); 					\
       } \
       ret__; })


#define read_pda(field) pda_from_op("mov",field)
#define write_pda(field,val) pda_to_op("mov",field,val)
#define add_pda(field,val) pda_to_op("add",field,val)
#define sub_pda(field,val) pda_to_op("sub",field,val)

#endif
