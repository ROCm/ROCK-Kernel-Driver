/*
 * linux/arch/arm/mach-sa1100/neponset.c
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/serial_core.h>
#include <linux/device.h>
#include <linux/slab.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/serial_sa1100.h>
#include <asm/arch/assabet.h>
#include <asm/arch/neponset.h>
#include <asm/hardware/sa1111.h>
#include <asm/sizes.h>

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

static inline void __init neponset_init_irq(void)
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
	.set_mctrl	= neponset_set_mctrl,
	.get_mctrl	= neponset_get_mctrl,
};

/*
 * LDM power management.
 */
static int neponset_suspend(struct device *dev, u32 state, u32 level)
{
	/*
	 * Save state.
	 */
	if (level == SUSPEND_SAVE_STATE ||
	    level == SUSPEND_DISABLE ||
	    level == SUSPEND_POWER_DOWN) {
		if (!dev->saved_state)
			dev->saved_state = kmalloc(sizeof(unsigned int), GFP_KERNEL);
		if (!dev->saved_state)
			return -ENOMEM;

		*(unsigned int *)dev->saved_state = NCR_0;
	}

	return 0;
}

static int neponset_resume(struct device *dev, u32 level)
{
	if (level == RESUME_RESTORE_STATE || level == RESUME_ENABLE) {
		if (dev->saved_state) {
			NCR_0 = *(unsigned int *)dev->saved_state;
			kfree(dev->saved_state);
			dev->saved_state = NULL;
		}
	}

	return 0;
}

static struct device_driver neponset_device_driver = {
	.suspend = neponset_suspend,
	.resume  = neponset_resume,
};

static struct device neponset_device = {
	.name	= "Neponset",
	.bus_id	= "neponset",
	.driver = &neponset_device_driver,
};

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

	ret = register_sys_device(&neponset_device);
	if (ret)
		return ret;

	sa1100_register_uart_fns(&neponset_port_fns);

	neponset_init_irq();

	/*
	 * Disable GPIO 0/1 drivers so the buttons work on the module.
	 */
	NCR_0 = NCR_GP01_OFF;

	/*
	 * Neponset has SA1111 connected to CS4.  We know that after
	 * reset the chip will be configured for variable latency IO.
	 */
	/* FIXME: setup MSC2 */

	/*
	 * Probe and initialise the SA1111.
	 */
	return sa1111_init(0x40000000, IRQ_NEPONSET_SA1111);
}

arch_initcall(neponset_init);

static struct map_desc neponset_io_desc[] __initdata = {
 /* virtual     physical    length type */
  { 0xf3000000, 0x10000000, SZ_1M, MT_DEVICE }, /* System Registers */
  { 0xf4000000, 0x40000000, SZ_1M, MT_DEVICE }  /* SA-1111 */
};

void __init neponset_map_io(void)
{
	iotable_init(neponset_io_desc, ARRAY_SIZE(neponset_io_desc));
}
