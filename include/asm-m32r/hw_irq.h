#ifndef _ASM_M32R_HW_IRQ_H
#define _ASM_M32R_HW_IRQ_H

/* $Id$ */

#include <linux/profile.h>
#include <linux/sched.h>
#include <asm/sections.h>

static __inline__ void hw_resend_irq(struct hw_interrupt_type *h,
				     unsigned int i)
{
	/* Nothing to do */
}

static __inline__ void m32r_do_profile (struct pt_regs *regs)
{
        unsigned long pc = regs->bpc;

        profile_hook(regs);

        if (user_mode(regs))
                return;

        if (!prof_buffer)
                return;

        pc -= (unsigned long) &_stext;
        pc >>= prof_shift;
        /*
         * Don't ignore out-of-bounds PC values silently,
         * put them into the last histogram slot, so if
         * present, they will show up as a sharp peak.
         */
        if (pc > prof_len - 1)
                pc = prof_len - 1;
        atomic_inc((atomic_t *)&prof_buffer[pc]);
}

#endif /* _ASM_M32R_HW_IRQ_H */
