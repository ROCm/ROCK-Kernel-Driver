#ifndef __V850_HW_IRQ_H__
#define __V850_HW_IRQ_H__

extern inline void hw_resend_irq (struct hw_interrupt_type *h, unsigned int i)
{
}

extern irq_desc_t irq_desc [NR_IRQS];

#endif /* __V850_HW_IRQ_H__ */
