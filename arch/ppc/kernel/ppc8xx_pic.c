#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <asm/irq.h>
#include <asm/8xx_immap.h>
#include <asm/mpc8xx.h>
#include "ppc8xx_pic.h"

/* The 8xx internal interrupt controller.  It is usually
 * the only interrupt controller.  Some boards, like the MBX and
 * Sandpoint have the 8259 as a secondary controller.  Depending
 * upon the processor type, the internal controller can have as
 * few as 16 interrups or as many as 64.  We could use  the
 * "clear_bit()" and "set_bit()" functions like other platforms,
 * but they are overkill for us.
 */

static void m8xx_mask_irq(unsigned int irq_nr)
{
	int	bit, word;

	bit = irq_nr & 0x1f;
	word = irq_nr >> 5;

	ppc_cached_irq_mask[word] &= ~(1 << (31-bit));
	((immap_t *)IMAP_ADDR)->im_siu_conf.sc_simask =
						ppc_cached_irq_mask[word];
}

static void m8xx_unmask_irq(unsigned int irq_nr)
{
	int	bit, word;

	bit = irq_nr & 0x1f;
	word = irq_nr >> 5;

	ppc_cached_irq_mask[word] |= (1 << (31-bit));
	((immap_t *)IMAP_ADDR)->im_siu_conf.sc_simask =
						ppc_cached_irq_mask[word];
}

static void m8xx_mask_and_ack(unsigned int irq_nr)
{
	int	bit, word;

	bit = irq_nr & 0x1f;
	word = irq_nr >> 5;

	ppc_cached_irq_mask[word] &= ~(1 << (31-bit));
	((immap_t *)IMAP_ADDR)->im_siu_conf.sc_simask =
						ppc_cached_irq_mask[word];
	((immap_t *)IMAP_ADDR)->im_siu_conf.sc_sipend = 1 << (31-bit);
}

struct hw_interrupt_type ppc8xx_pic = {
	" 8xx SIU  ",
	NULL,
	NULL,
	m8xx_unmask_irq,
	m8xx_mask_irq,
	m8xx_mask_and_ack,
	0
};

#if 0
void
m8xx_do_IRQ(struct pt_regs *regs,
	   int            cpu,
           int            isfake)
{
	int irq;
        unsigned long bits = 0;

        /* For MPC8xx, read the SIVEC register and shift the bits down
         * to get the irq number.         */
        bits = ((immap_t *)IMAP_ADDR)->im_siu_conf.sc_sivec;
        irq = bits >> 26;
#if 0
        irq += ppc8xx_pic.irq_offset;
#endif
        bits = 1UL << irq;

	if (irq < 0) {
		printk(KERN_DEBUG "Bogus interrupt %d from PC = %lx\n",
		       irq, regs->nip);
		ppc_spurious_interrupts++;
	}
	else {
                ppc_irq_dispatch_handler( regs, irq );
	}

}
#endif


int
m8xx_get_irq(struct pt_regs *regs)
{
	int irq;
        unsigned long bits = 0;

        /* For MPC8xx, read the SIVEC register and shift the bits down
         * to get the irq number.         */
        bits = ((immap_t *)IMAP_ADDR)->im_siu_conf.sc_sivec;
        irq = bits >> 26;
#if 0
        irq += ppc8xx_pic.irq_offset;
#endif
	return irq;
}

/* The MBX is the only 8xx board that uses the 8259.
*/
#if defined(CONFIG_MBX) && defined(CONFIG_PCI)
void mbx_i8259_action(int cpl, void *dev_id, struct pt_regs *regs)
{
	int bits, irq;

	/* A bug in the QSpan chip causes it to give us 0xff always
	 * when doing a character read.  So read 32 bits and shift.
	 * This doesn't seem to return useful values anyway, but
	 * read it to make sure things are acked.
	 * -- Cort
	 */
	irq = (inl(0x508) >> 24)&0xff;
	if ( irq != 0xff ) printk("iack %d\n", irq);
	
	outb(0x0C, 0x20);
	irq = inb(0x20) & 7;
	if (irq == 2)
	{
		outb(0x0C, 0xA0);
		irq = inb(0xA0);
		irq = (irq&7) + 8;
	}
	bits = 1UL << irq;
	irq += i8259_pic.irq_offset;
	ppc_irq_dispatch_handler( regs, irq );
}
#endif

/* Only the MBX uses the external 8259.  This allows us to catch standard
 * drivers that may mess up the internal interrupt controllers, and also
 * allow them to run without modification on the MBX.
 */
int request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
	unsigned long irqflags, const char * devname, void *dev_id)
{

#if defined(CONFIG_MBX) && defined(CONFIG_PCI)
	irq += i8259_pic.irq_offset;
	return (request_8xxirq(irq, handler, irqflags, devname, dev_id));
#else
	panic("request_irq");
#endif
}
