/*
 * BK Id: %F% %I% %G% %U% %#%
 */

#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <asm/io.h>
#include <asm/i8259.h>

static volatile unsigned char *pci_intack; /* RO, gives us the irq vector */

unsigned char cached_8259[2] = { 0xff, 0xff };
#define cached_A1 (cached_8259[0])
#define cached_21 (cached_8259[1])

static spinlock_t i8259_lock = SPIN_LOCK_UNLOCKED;

int i8259_pic_irq_offset;

/* Acknowledge the irq using the PCI host bridge's interrupt acknowledge
 * feature. (Polling is somehow broken on some IBM and Motorola PReP boxes.)
 */
int i8259_irq(struct pt_regs *regs)
{
	int irq;

	spin_lock/*_irqsave*/(&i8259_lock/*, flags*/);

	irq = *pci_intack;
	if (irq==7) {
		/*
		 * This may be a spurious interrupt.
		 *
		 * Read the interrupt status register (ISR). If the most
		 * significant bit is not set then there is no valid
		 * interrupt.
		 */
		if(~inb(0x20)&0x80) {
			irq = -1;
		}
	}
	spin_unlock/*_irqrestore*/(&i8259_lock/*, flags*/);
	return irq;
}

/* Poke the 8259's directly using poll commands. */
int i8259_poll(void)
{
	int irq;

	spin_lock/*_irqsave*/(&i8259_lock/*, flags*/);
	/*
	 * Perform an interrupt acknowledge cycle on controller 1
	 */
	outb(0x0C, 0x20); /* prepare for poll */
	irq = inb(0x20) & 7;
	if (irq == 2) {
		/*
		 * Interrupt is cascaded so perform interrupt
		 * acknowledge on controller 2
		 */
		outb(0x0C, 0xA0); /* prepare for poll */
		irq = (inb(0xA0) & 7) + 8;
	} else if (irq==7) {
		/*
		 * This may be a spurious interrupt
		 *
		 * Read the interrupt status register. If the most
		 * significant bit is not set then there is no valid
		 * interrupt
		 */
		outb(0x0b, 0x20);
		if(~inb(0x20)&0x80) {
			spin_unlock/*_irqrestore*/(&i8259_lock/*, flags*/);
			return -1;
		}
	}
	spin_unlock/*_irqrestore*/(&i8259_lock/*, flags*/);
	return irq;
}

static void i8259_mask_and_ack_irq(unsigned int irq_nr)
{
	unsigned long flags;

	spin_lock_irqsave(&i8259_lock, flags);
	if ( irq_nr >= i8259_pic_irq_offset )
		irq_nr -= i8259_pic_irq_offset;

	if (irq_nr > 7) {
		cached_A1 |= 1 << (irq_nr-8);
		inb(0xA1); /* DUMMY */
		outb(cached_A1,0xA1);
		outb(0x20,0xA0); /* Non-specific EOI */
		outb(0x20,0x20); /* Non-specific EOI to cascade */
	} else {
		cached_21 |= 1 << irq_nr;
		inb(0x21); /* DUMMY */
		outb(cached_21,0x21);
		outb(0x20,0x20); /* Non-specific EOI */
	}
	spin_unlock_irqrestore(&i8259_lock, flags);
}

static void i8259_set_irq_mask(int irq_nr)
{
	outb(cached_A1,0xA1);
	outb(cached_21,0x21);
}
 
static void i8259_mask_irq(unsigned int irq_nr)
{
	unsigned long flags;

	spin_lock_irqsave(&i8259_lock, flags);
	if ( irq_nr >= i8259_pic_irq_offset )
		irq_nr -= i8259_pic_irq_offset;
	if ( irq_nr < 8 )
		cached_21 |= 1 << irq_nr;
	else
		cached_A1 |= 1 << (irq_nr-8);
	i8259_set_irq_mask(irq_nr);
	spin_unlock_irqrestore(&i8259_lock, flags);
}

static void i8259_unmask_irq(unsigned int irq_nr)
{
	unsigned long flags;

	spin_lock_irqsave(&i8259_lock, flags);
	if ( irq_nr >= i8259_pic_irq_offset )
		irq_nr -= i8259_pic_irq_offset;
	if ( irq_nr < 8 )
		cached_21 &= ~(1 << irq_nr);
	else
		cached_A1 &= ~(1 << (irq_nr-8));
	i8259_set_irq_mask(irq_nr);
	spin_unlock_irqrestore(&i8259_lock, flags);
}

static void i8259_end_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		i8259_unmask_irq(irq);
}

struct hw_interrupt_type i8259_pic = {
	" i8259    ",
	NULL,
	NULL,
	i8259_unmask_irq,
	i8259_mask_irq,
	i8259_mask_and_ack_irq,
	i8259_end_irq,
	NULL
};

static struct resource pic1_iores = {
	"8259 (master)", 0x20, 0x21, IORESOURCE_BUSY
};

static struct resource pic2_iores = {
	"8259 (slave)", 0xa0, 0xa1, IORESOURCE_BUSY
};

static struct resource pic_edgectrl_iores = {
	"8259 edge control", 0x4d0, 0x4d1, IORESOURCE_BUSY
};

void __init i8259_init(long intack_addr)
{
	unsigned long flags;

	spin_lock_irqsave(&i8259_lock, flags);
	/* init master interrupt controller */
	outb(0x11, 0x20); /* Start init sequence */
	outb(0x00, 0x21); /* Vector base */
	outb(0x04, 0x21); /* edge tiggered, Cascade (slave) on IRQ2 */
	outb(0x01, 0x21); /* Select 8086 mode */

	/* init slave interrupt controller */
	outb(0x11, 0xA0); /* Start init sequence */
	outb(0x08, 0xA1); /* Vector base */
	outb(0x02, 0xA1); /* edge triggered, Cascade (slave) on IRQ2 */
	outb(0x01, 0xA1); /* Select 8086 mode */

	/* always read ISR */
	outb(0x0B, 0x20);
	outb(0x0B, 0xA0);

	/* Mask all interrupts */
	outb(cached_A1, 0xA1);
	outb(cached_21, 0x21);

	spin_unlock_irqrestore(&i8259_lock, flags);

	/* reserve our resources */
	request_irq( i8259_pic_irq_offset + 2, no_action, SA_INTERRUPT,
				"82c59 secondary cascade", NULL );
	request_resource(&ioport_resource, &pic1_iores);
	request_resource(&ioport_resource, &pic2_iores);
	request_resource(&ioport_resource, &pic_edgectrl_iores);

	if (intack_addr)
		pci_intack = ioremap(intack_addr, 1);
}
