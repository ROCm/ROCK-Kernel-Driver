/*
 * Interrupt handling assembler for Linux/CRIS
 *
 * Copyright (c) 2000 Axis Communications AB
 *
 * Authors:   Bjorn Wesen (bjornw@axis.com)
 *
 */

#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

/*
 *	linux/include/asm-cris/irq.h
 */

#include <linux/linkage.h>
#include <asm/segment.h>

#include <asm/sv_addr_ag.h>

#define NR_IRQS 32

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#define disable_irq_nosync      disable_irq
#define enable_irq_nosync       enable_irq

/* our fine, global, etrax irq vector! the pointer lives in the head.S file. */

typedef void (*irqvectptr)(void);

struct etrax_interrupt_vector {
	irqvectptr v[256];
};

extern struct etrax_interrupt_vector *etrax_irv;
void set_int_vector(int n, irqvectptr addr, irqvectptr saddr);
void set_break_vector(int n, irqvectptr addr);

#define __STR(x) #x
#define STR(x) __STR(x)
 
/* SAVE_ALL saves registers so they match pt_regs */

#define SAVE_ALL \
  "move irp,[sp=sp-16]\n\t" /* push instruction pointer and fake SBFS struct */ \
  "push srp\n\t"       /* push subroutine return pointer */ \
  "push dccr\n\t"      /* push condition codes */ \
  "push mof\n\t"       /* push multiply overflow reg */ \
  "di\n\t"             /* need to disable irq's at this point */\
  "subq 14*4,sp\n\t"   /* make room for r0-r13 */ \
  "movem r13,[sp]\n\t" /* push the r0-r13 registers */ \
  "push r10\n\t"       /* push orig_r10 */ \
  "clear.d [sp=sp-4]\n\t"  /* frametype - this is a normal stackframe */

  /* BLOCK_IRQ and UNBLOCK_IRQ do the same as mask_irq and unmask_irq in irq.c */

#define BLOCK_IRQ(mask,nr) \
  "move.d " #mask ",r0\n\t" \
  "move.d r0,[0xb00000d8]\n\t" 
  
#define UNBLOCK_IRQ(mask) \
  "move.d " #mask ",r0\n\t" \
  "move.d r0,[0xb00000dc]\n\t" 

#define IRQ_NAME2(nr) nr##_interrupt(void)
#define IRQ_NAME(nr) IRQ_NAME2(IRQ##nr)
#define sIRQ_NAME(nr) IRQ_NAME2(sIRQ##nr)
#define BAD_IRQ_NAME(nr) IRQ_NAME2(bad_IRQ##nr)

  /* the asm IRQ handler makes sure the causing IRQ is blocked, then it calls
   * do_IRQ (with irq disabled still). after that it unblocks and jumps to
   * ret_from_intr (entry.S)
   */

#define BUILD_IRQ(nr,mask) \
void IRQ_NAME(nr); \
void sIRQ_NAME(nr); \
void BAD_IRQ_NAME(nr); \
__asm__ ( \
          ".text\n\t" \
          "_IRQ" #nr "_interrupt:\n\t" \
	  SAVE_ALL \
	  "_sIRQ" #nr "_interrupt:\n\t" /* shortcut for the multiple irq handler */ \
	  BLOCK_IRQ(mask,nr) /* this must be done to prevent irq loops when we ei later */ \
	  "moveq "#nr",r10\n\t" \
	  "move.d sp,r11\n\t" \
	  "jsr _do_IRQ\n\t" /* irq.c, r10 and r11 are arguments */ \
	  UNBLOCK_IRQ(mask) \
	  "moveq 0,r9\n\t" /* make ret_from_intr realise we came from an irq */ \
	  "jump _ret_from_intr\n\t" \
          "_bad_IRQ" #nr "_interrupt:\n\t" \
	  "push r0\n\t" \
	  BLOCK_IRQ(mask,nr) \
	  "pop r0\n\t" \
          "reti\n\t" \
          "nop\n");


#endif  /* _ASM_IRQ_H */


