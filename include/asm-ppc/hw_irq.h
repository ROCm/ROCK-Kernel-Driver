/*
 * $Id: irq_control.h,v 1.8 1999/09/15 23:58:48 cort Exp $
 *
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 */
#ifdef __KERNEL__
#ifndef _PPC_HW_IRQ_H
#define _PPC_HW_IRQ_H

struct int_control_struct
{
	void (*int_cli)(void);
	void (*int_sti)(void);
	void (*int_restore_flags)(unsigned long);
	void (*int_save_flags)(unsigned long *);
	void (*int_set_lost)(unsigned long);
};
extern struct int_control_struct int_control;
extern unsigned long timer_interrupt_intercept;
extern unsigned long do_IRQ_intercept;
int timer_interrupt(struct pt_regs *);

extern void __no_use_sti(void);
extern void __no_use_cli(void);
extern void __no_use_restore_flags(unsigned long);
extern void __no_use_save_flags(unsigned long *);
extern void __no_use_set_lost(unsigned long);

#define __cli() int_control.int_cli()
#define __sti() int_control.int_sti()
#define __save_flags(flags) int_control.int_save_flags((unsigned long *)&flags)
#define __restore_flags(flags) int_control.int_restore_flags((unsigned long)flags)
#define __save_and_cli(flags) ({__save_flags(flags);__cli();})
#define __set_lost(irq) ({ if ((unsigned long)int_control.int_set_lost) int_control.int_set_lost(irq); })

extern void do_lost_interrupts(unsigned long);

#define mask_irq(irq) ({if (irq_desc[irq].handler && irq_desc[irq].handler->disable) irq_desc[irq].handler->disable(irq);})
#define unmask_irq(irq) ({if (irq_desc[irq].handler && irq_desc[irq].handler->enable) irq_desc[irq].handler->enable(irq);})
#define ack_irq(irq) ({if (irq_desc[irq].handler && irq_desc[irq].handler->ack) irq_desc[irq].handler->ack(irq);})

/* Should we handle this via lost interrupts and IPIs or should we don't care like
 * we do now ? --BenH.
 */
struct hw_interrupt_type;
static inline void hw_resend_irq(struct hw_interrupt_type *h, unsigned int i) {}


#endif /* _PPC_HW_IRQ_H */
#endif /* __KERNEL__ */
