/*
 * include/asm-v850/nb85e_intc.h -- NB85E cpu core interrupt controller (INTC)
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_NB85E_INTC_H__
#define __V850_NB85E_INTC_H__


/* There are 4 16-bit `Interrupt Mask Registers' located contiguously
   starting from this base.  Each interrupt uses a single bit to
   indicated enabled/disabled status.  */
#define NB85E_INTC_IMR_BASE_ADDR  0xFFFFF100
#define NB85E_INTC_IMR_ADDR(irq)  (NB85E_INTC_IMR_BASE_ADDR + ((irq) >> 3))
#define NB85E_INTC_IMR_BIT(irq)	  ((irq) & 0x7)

/* Each maskable interrupt has a single-byte control register at this
   address.  */
#define NB85E_INTC_IC_BASE_ADDR	  0xFFFFF110
#define NB85E_INTC_IC_ADDR(irq)	  (NB85E_INTC_IC_BASE_ADDR + ((irq) << 1))
#define NB85E_INTC_IC(irq)	  (*(volatile u8 *)NB85E_INTC_IC_ADDR(irq))
/* Encode priority PR for storing in an interrupt control register.  */
#define NB85E_INTC_IC_PR(pr)	  (pr)
/* Interrupt disable bit in an interrupt control register.  */
#define NB85E_INTC_IC_MK_BIT	  6
#define NB85E_INTC_IC_MK	  (1 << NB85E_INTC_IC_MK_BIT)
/* Interrupt pending flag in an interrupt control register.  */
#define NB85E_INTC_IC_IF_BIT	  7
#define NB85E_INTC_IC_IF	  (1 << NB85E_INTC_IC_IF_BIT)

/* The ISPR (In-service priority register) contains one bit for each interrupt
   priority level, which is set to one when that level is currently being
   serviced (and thus blocking any interrupts of equal or lesser level).  */
#define NB85E_INTC_ISPR_ADDR	  0xFFFFF1FA
#define NB85E_INTC_ISPR		  (*(volatile u8 *)NB85E_INTC_ISPR_ADDR)


#ifndef __ASSEMBLY__

/* Enable interrupt handling for interrupt IRQ.  */
static inline void nb85e_intc_enable_irq (unsigned irq)
{
	__asm__ __volatile__ ("clr1 %0, [%1]"
			      :: "r" (NB85E_INTC_IMR_BIT (irq)),
			         "r" (NB85E_INTC_IMR_ADDR (irq))
			      : "memory");
}

/* Disable interrupt handling for interrupt IRQ.  Note that any
   interrupts received while disabled will be delivered once the
   interrupt is enabled again, unless they are explicitly cleared using
   `nb85e_intc_clear_pending_irq'.  */
static inline void nb85e_intc_disable_irq (unsigned irq)
{
	__asm__ __volatile__ ("set1 %0, [%1]"
			      :: "r" (NB85E_INTC_IMR_BIT (irq)),
			         "r" (NB85E_INTC_IMR_ADDR (irq))
			      : "memory");
}

/* Return true if interrupt handling for interrupt IRQ is enabled.  */
static inline int nb85e_intc_irq_enabled (unsigned irq)
{
	int rval;
	__asm__ __volatile__ ("tst1 %1, [%2]; setf z, %0"
			      : "=r" (rval)
			      : "r" (NB85E_INTC_IMR_BIT (irq)),
			        "r" (NB85E_INTC_IMR_ADDR (irq)));
	return rval;
}

/* Disable irqs from 0 until LIMIT.  LIMIT must be a multiple of 8.  */
static inline void _nb85e_intc_disable_irqs (unsigned limit)
{
	unsigned long addr;
	for (addr = NB85E_INTC_IMR_BASE_ADDR; limit >= 8; addr++, limit -= 8)
		*(char *)addr = 0xFF;
}

/* Disable all irqs.  This is purposely a macro, because NUM_MACH_IRQS
   will be only be defined later.  */
#define nb85e_intc_disable_irqs()   _nb85e_intc_disable_irqs (NUM_MACH_IRQS)

/* Clear any pending interrupts for IRQ.  */
static inline void nb85e_intc_clear_pending_irq (unsigned irq)
{
	__asm__ __volatile__ ("clr1 %0, 0[%1]"
			      :: "i" (NB85E_INTC_IC_IF_BIT),
			         "r" (NB85E_INTC_IC_ADDR (irq))
			      : "memory");
}

/* Return true if interrupt IRQ is pending (but disabled).  */
static inline int nb85e_intc_irq_pending (unsigned irq)
{
	int rval;
	__asm__ __volatile__ ("tst1 %1, 0[%2]; setf nz, %0"
			      : "=r" (rval)
			      : "i" (NB85E_INTC_IC_IF_BIT),
			        "r" (NB85E_INTC_IC_ADDR (irq)));
	return rval;
}


struct nb85e_intc_irq_init {
	const char *name;	/* name of interrupt type */

	/* Range of kernel irq numbers for this type:
	   BASE, BASE+INTERVAL, ..., BASE+INTERVAL*NUM  */
	unsigned base, num, interval;

	unsigned priority;	/* interrupt priority to assign */
};
struct hw_interrupt_type;	/* fwd decl */

/* Initialize HW_IRQ_TYPES for INTC-controlled irqs described in array
   INITS (which is terminated by an entry with the name field == 0).  */
extern void nb85e_intc_init_irq_types (struct nb85e_intc_irq_init *inits,
				       struct hw_interrupt_type *hw_irq_types);


#endif /* !__ASSEMBLY__ */


#endif /* __V850_NB85E_INTC_H__ */
