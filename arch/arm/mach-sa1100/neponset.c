/*
 * linux/arch/arm/mach-sa1100/neponset.c
 *
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/serial_core.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/serial_sa1100.h>
#include <asm/arch/assabet.h>
#include <asm/hardware/sa1111.h>

#include "sa1111.h"


/*
 * Install handler for Neponset IRQ.  Note that we have to loop here
 * since the ETHERNET and USAR IRQs are level based, and we need to
 * ensure that the IRQ signal is deasserted before returning.  This
 * is rather unfortunate.
 */
static void
neponset_irq_handler(unsigned int irq, struct irqdesc *desc, struct pt_regs *regs)
{
	unsigned int irr;

	while (1) {
		struct irqdesc *d;

		/*
		 * Acknowledge the parent IRQ.
		 */
		desc->chip->ack(irq);

		/*
		 * Read the interrupt reason register.  Let's have all
		 * active IRQ bits high.  Note: there is a typo in the
		 * Neponset user's guide for the SA1111 IRR level.
		 */
		irr = IRR ^ (IRR_ETHERNET | IRR_USAR);

		if ((irr & (IRR_ETHERNET | IRR_USAR | IRR_SA1111)) == 0)
			break;

		/*
		 * Since there is no individual mask, we have to
		 * mask the parent IRQ.  This is safe, since we'll
		 * recheck the register for any pending IRQs.
		 */
		if (irr & (IRR_ETHERNET | IRR_USAR)) {
			desc->chip->mask(irq);

			if (irr & IRR_ETHERNET) {
				d = irq_desc + IRQ_NEPONSET_SMC9196;
				d->handle(IRQ_NEPONSET_SMC9196, d, regs);
			}

			if (irr & IRR_USAR) {
				d = irq_desc + IRQ_NEPONSET_USAR;
				d->handle(IRQ_NEPONSET_USAR, d, regs);
			}

			desc->chip->unmask(irq);
		}

		if (irr & IRR_SA1111) {
			d = irq_desc + IRQ_NEPONSET_SA1111;
			d->handle(IRQ_NEPONSET_SA1111, d, regs);
		}
	}
}

static void __init neponset_init_irq(void)
{
	/*
	 * Install handler for GPIO25.
	 */
	set_irq_type(IRQ_GPIO25, IRQT_RISING);
	set_irq_chained_handler(IRQ_GPIO25, neponset_irq_handler);

	/*
	 * Setup other Neponset IRQs.  SA1111 will be done by the
	 * generic SA1111 code.
	 */
	set_irq_handler(IRQ_NEPONSET_SMC9196, do_simple_IRQ);
	set_irq_flags(IRQ_NEPONSET_SMC9196, IRQF_VALID | IRQF_PROBE);
	set_irq_handler(IRQ_NEPONSET_USAR, do_simple_IRQ);
	set_irq_flags(IRQ_NEPONSET_USAR, IRQF_VALID | IRQF_PROBE);
}

static int __init neponset_init(void)
{
	int ret;

	/*
	 * The Neponset is only present on the Assabet machine type.
	 */
	if (!machine_is_assabet())
		return -ENODEV;

	/*
	 * Ensure that the memory bus request/grant signals are setup,
	 * and the grant is held in its inactive state, whether or not
	 * we actually have a Neponset attached.
	 */
	sa1110_mb_disable();

	if (!machine_has_neponset()) {
		printk(KERN_DEBUG "Neponset expansion board not present\n");
		return -ENODEV;
	}

	if (WHOAMI != 0x11) {
		printk(KERN_WARNING "Neponset board detected, but "
			"wrong ID: %02x\n", WHOAMI);
		return -ENODEV;
	}

	neponset_init_irq();

	/*
	 * Disable GPIO 0/1 drivers so the buttons work on the module.
	 */
	NCR_0 |= NCR_GP01_OFF;

	/*
	 * Neponset has SA1111 connected to CS4.  We know that after
	 * reset the chip will be configured for variable latency IO.
	 */
	/* FIXME: setup MSC2 */

	/*
	 * Probe for a SA1111.
	 */
	ret = sa1111_probe(0x40000000);
	if (ret < 0)
		return ret;

	/*
	 * We found it.  Wake the chip up.
	 */
	sa1111_wake();

	/*
	 * The SDRAM configuration of the SA1110 and the SA1111 must
	 * match.  This is very important to ensure that SA1111 accesses
	 * don't corrupt the SDRAM.  Note that this ungates the SA1111's
	 * MBGNT signal, so we must have called sa1110_mb_disable()
	 * beforehand.
	 */
	sa1111_configure_smc(1,
			     FExtr(MDCNFG, MDCNFG_SA1110_DRAC0),
			     FExtr(MDCNFG, MDCNFG_SA1110_TDL0));

	/*
	 * We only need to turn on DCLK whenever we want to use the
	 * DMA.  It can otherwise be held firmly in the off position.
	 */
	SKPCR |= SKPCR_DCLKEN;

	/*
	 * Enable the SA1110 memory bus request and grant signals.
	 */
	sa1110_mb_enable();

	/*
	 * Initialise SA1111 IRQs
	 */
	sa1111_init_irq(IRQ_NEPONSET_SA1111);

	return 0;
}

__initcall(neponset_init);

static struct map_desc neponset_io_desc[] __initdata = {
 /* virtual     physical    length      domain     r  w  c  b */
  { 0xf3000000, 0x10000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* System Registers */
  { 0xf4000000, 0x40000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* SA-1111 */
  LAST_DESC
};

static void neponset_set_mctrl(struct uart_port *port, u_int mctrl)
{
	u_int mdm_ctl0 = MDM_CTL_0;

	if (port->mapbase == _Ser1UTCR0) {
		if (mctrl & TIOCM_RTS)
			mdm_ctl0 &= ~MDM_CTL0_RTS2;
		else
			mdm_ctl0 |= MDM_CTL0_RTS2;

		if (mctrl & TIOCM_DTR)
			mdm_ctl0 &= ~MDM_CTL0_DTR2;
		else
			mdm_ctl0 |= MDM_CTL0_DTR2;
	} else if (port->mapbase == _Ser3UTCR0) {
		if (mctrl & TIOCM_RTS)
			mdm_ctl0 &= ~MDM_CTL0_RTS1;
		else
			mdm_ctl0 |= MDM_CTL0_RTS1;

		if (mctrl & TIOCM_DTR)
			mdm_ctl0 &= ~MDM_CTL0_DTR1;
		else
			mdm_ctl0 |= MDM_CTL0_DTR1;
	}

	MDM_CTL_0 = mdm_ctl0;
}

static u_int neponset_get_mctrl(struct uart_port *port)
{
	u_int ret = TIOCM_CD | TIOCM_CTS | TIOCM_DSR;
	u_int mdm_ctl1 = MDM_CTL_1;

	if (port->mapbase == _Ser1UTCR0) {
		if (mdm_ctl1 & MDM_CTL1_DCD2)
			ret &= ~TIOCM_CD;
		if (mdm_ctl1 & MDM_CTL1_CTS2)
			ret &= ~TIOCM_CTS;
		if (mdm_ctl1 & MDM_CTL1_DSR2)
			ret &= ~TIOCM_DSR;
	} else if (port->mapbase == _Ser3UTCR0) {
		if (mdm_ctl1 & MDM_CTL1_DCD1)
			ret &= ~TIOCM_CD;
		if (mdm_ctl1 & MDM_CTL1_CTS1)
			ret &= ~TIOCM_CTS;
		if (mdm_ctl1 & MDM_CTL1_DSR1)
			ret &= ~TIOCM_DSR;
	}

	return ret;
}

static struct sa1100_port_fns neponset_port_fns __initdata = {
	set_mctrl:	neponset_set_mctrl,
	get_mctrl:	neponset_get_mctrl,
};

void __init neponset_map_io(void)
{
	iotable_init(neponset_io_desc);
	if (machine_has_neponset())
		sa1100_register_uart_fns(&neponset_port_fns);
}
