/*
 * Interrupt handling for the VIA ISA bridge.
 *
 * Everything the same ... just different ...
 */
#include <linux/kernel.h>
#include <asm/cobalt.h>
#include <asm/ptrace.h>
#include <asm/io.h>

extern asmlinkage void do_IRQ(int irq, struct pt_regs * regs);

extern unsigned char cache_21;
extern unsigned char cache_A1;

/*
 * (un)mask_irq, disable_irq() and enable_irq() only handle (E)ISA and
 * PCI devices.  Other onboard hardware needs specific routines.
 */
void mask_irq(unsigned int irq_nr)
{
	unsigned char mask;

	mask = 1 << (irq_nr & 7);
	if (irq_nr < 8) {
		cache_21 |= mask;
		outb(cache_21, 0x10000021);
	} else {
		cache_A1 |= mask;
		outb(cache_A1, 0x100000a1);
	}
}

void unmask_irq(unsigned int irq_nr)
{
	unsigned char mask;

	mask = ~(1 << (irq_nr & 7));
	if (irq_nr < 8) {
		cache_21 &= mask;
		outb(cache_21, 0x10000021);
	} else {
		cache_A1 &= mask;
		outb(cache_A1, 0x100000a1);
	}
}

asmlinkage void via_irq(struct pt_regs *regs)
{
	char mstat, sstat;
  
	/* Read Master Status */
	VIA_PORT_WRITE(0x20, 0x0C);
	mstat = VIA_PORT_READ(0x20);
 
	if (mstat < 0) {
		mstat &= 0x7f;
		if (mstat != 2) {     	
			do_IRQ(mstat, regs);
			VIA_PORT_WRITE(0x20, mstat | 0x20);
		} else {
			sstat = VIA_PORT_READ(0xA0);

			/* Slave interrupt */
			VIA_PORT_WRITE(0xA0, 0x0C);
			sstat = VIA_PORT_READ(0xA0);
   
			if (sstat < 0) {
				do_IRQ((sstat + 8) & 0x7f, regs);
				VIA_PORT_WRITE(0x20, 0x22);       
				VIA_PORT_WRITE(0xA0, (sstat & 0x7f) | 0x20);
			} else {
				printk("Spurious slave interrupt...\n");
			}
		}
	} else
		printk("Spurious master interrupt...");
}

asmlinkage void galileo_irq(struct pt_regs *regs)
{
	unsigned long irq_src = *((unsigned long *) 0xb4000c18); 
  
	/* Check for timer irq ... */
	if (irq_src & 0x00000100) {
		*((volatile unsigned long *) 0xb4000c18) = 0;
		do_IRQ(0, regs);
	} else
		printk("Spurious Galileo interrupt...\n");
}
