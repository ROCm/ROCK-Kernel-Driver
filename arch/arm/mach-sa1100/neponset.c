/*
 * linux/arch/arm/mach-sa1100/neponset.c
 *
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/arch/irq.h>
#include <asm/mach/serial_sa1100.h>
#include <linux/serial_core.h>

#include "sa1111.h"


/*
 * Install handler for Neponset IRQ.  Yes, yes... we are way down the IRQ
 * cascade which is not good for IRQ latency, but the hardware has been
 * designed that way...
 */

static void neponset_IRQ_demux( int irq, void *dev_id, struct pt_regs *regs )
{
	int irr;

	for(;;){
		irr = IRR & (IRR_ETHERNET | IRR_USAR | IRR_SA1111);
		/* Let's have all active IRQ bits high.
		 * Note: there is a typo in the Neponset user's guide
		 * for the SA1111 IRR level.
		 */
		irr ^= (IRR_ETHERNET | IRR_USAR);
		if (!irr) break;

		if( irr & IRR_ETHERNET )
			do_IRQ(NEPONSET_ETHERNET_IRQ, regs);

		if( irr & IRR_USAR )
			do_IRQ(NEPONSET_USAR_IRQ, regs);

		if( irr & IRR_SA1111 )
			sa1111_IRQ_demux(irq, dev_id, regs);
	}
}

static struct irqaction neponset_irq = {
	name:		"Neponset",
	handler:	neponset_IRQ_demux,
	flags:		SA_INTERRUPT
};

static void __init neponset_init_irq(void)
{
	int irq;

	sa1111_init_irq(-1);	/* SA1111 IRQ not routed to a GPIO */

	/* setup extra Neponset IRQs */
	irq = NEPONSET_ETHERNET_IRQ;
	irq_desc[irq].valid	= 1;
	irq_desc[irq].probe_ok	= 1;
	irq = NEPONSET_USAR_IRQ;
	irq_desc[irq].valid	= 1;
	irq_desc[irq].probe_ok	= 1;
	set_GPIO_IRQ_edge( GPIO_NEP_IRQ, GPIO_RISING_EDGE );
	setup_arm_irq( IRQ_GPIO_NEP_IRQ, &neponset_irq );
}

static int __init neponset_init(void)
{
	/* only on assabet */
	if (!machine_is_assabet())
		return 0;

	if (machine_has_neponset()) {
		LEDS = WHOAMI;

		if (sa1111_init() < 0)
			return -EINVAL;
		/*
		 * Assabet is populated by default with two Samsung
		 * KM416S8030T-G8
		 * 128Mb SDRAMs, which are organized as 12-bit (row addr) x
		 * 9-bit
		 * (column addr), according to the data sheet. Apparently, the
		 * bank selects factor into the row address, as Angel sets up
		 * the
		 * SA-1110 to use 14x9 addresses. The SDRAM datasheet specifies
		 * that when running at 100-125MHz, the CAS latency for -8
		 * parts
		 * is 3 cycles, which is consistent with Angel.
		 */
		SMCR = (SMCR_DTIM | SMCR_MBGE |
			FInsrt(FExtr(MDCNFG, MDCNFG_SA1110_DRAC0), SMCR_DRAC) |
			((FExtr(MDCNFG, MDCNFG_SA1110_TDL0)==3) ? SMCR_CLAT : 0));
		SKPCR |= SKPCR_DCLKEN;

		neponset_init_irq();
	} else
		printk("Neponset expansion board not present\n");

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

static int neponset_get_mctrl(struct uart_port *port)
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
