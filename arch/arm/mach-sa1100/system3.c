/*
 * linux/arch/arm/mach-sa1100/system3.c
 *
 * Copyright (C) 2001 Stefan Eletzhofer <stefan.eletzhofer@eletztrick.de>
 *
 * $Id: system3.c,v 1.1.6.1 2001/12/04 17:28:06 seletz Exp $
 *
 * This file contains all PT Sytsem 3 tweaks. Based on original work from
 * Nicolas Pitre's assabet fixes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * $Log: system3.c,v $
 * Revision 1.1.6.1  2001/12/04 17:28:06  seletz
 * - merged from previous branch
 *
 * Revision 1.1.4.3  2001/12/04 15:16:31  seletz
 * - merged from linux_2_4_13_ac5_rmk2
 *
 * Revision 1.1.4.2  2001/11/19 17:18:57  seletz
 * - more code cleanups
 *
 * Revision 1.1.4.1  2001/11/16 13:52:05  seletz
 * - PT Digital Board Support Code
 *
 * Revision 1.1.2.2  2001/11/05 16:46:18  seletz
 * - cleanups
 *
 * Revision 1.1.2.1  2001/10/15 16:00:43  seletz
 * - first revision working with new board
 *
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/cpufreq.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/setup.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/serial_sa1100.h>

#include <linux/serial_core.h>

#include "generic.h"
#include <asm/hardware/sa1111.h>

#define DEBUG 1

#ifdef DEBUG
#	define DPRINTK( x, args... )	printk( "%s: line %d: "x, __FUNCTION__, __LINE__, ## args  );
#else
#	define DPRINTK( x, args... )	/* nix */
#endif

/**********************************************************************
 *  prototypes
 */

/* init funcs */
static int __init system3_init(void);
static void __init system3_init_irq(void);
static void __init system3_map_io(void);

static u_int system3_get_mctrl(struct uart_port *port);
static void system3_set_mctrl(struct uart_port *port, u_int mctrl);
static void system3_uart_pm(struct uart_port *port, u_int state, u_int oldstate);
static int sdram_notifier(struct notifier_block *nb, unsigned long event, void *data);

static void system3_lcd_power(int on);
static void system3_backlight_power(int on);


/**********************************************************************
 *  global data
 */

/**********************************************************************
 *  static data
 */

static struct map_desc system3_io_desc[] __initdata = {
 /* virtual     physical        length      type */
  { 0xf3000000, PT_CPLD_BASE,   0x00100000, MT_DEVICE }, /* System Registers */
  { 0xf4000000, PT_SA1111_BASE, 0x00100000, MT_DEVICE }  /* SA-1111 */
};

static struct sa1100_port_fns system3_port_fns __initdata = {
	.set_mctrl	= system3_set_mctrl,
	.get_mctrl	= system3_get_mctrl,
	.pm		= system3_uart_pm,
};

static struct notifier_block system3_clkchg_block = {
	.notifier_call	= sdram_notifier,
};

/**********************************************************************
 *  Static functions
 */

static void __init system3_map_io(void)
{
	DPRINTK( "%s\n", "START" );
	sa1100_map_io();
	iotable_init(system3_io_desc, ARRAY_SIZE(system3_io_desc));

	sa1100_register_uart_fns(&system3_port_fns);
	sa1100_register_uart(0, 1);	/* com port */
	sa1100_register_uart(1, 2);
	sa1100_register_uart(2, 3);	/* radio module */

	Ser1SDCR0 |= SDCR0_SUS;
}


/*********************************************************************
 * Install IRQ handler
 */
static void
system3_irq_handler(unsigned int irq, struct irqdesc *desc, struct pt_regs *regs)
{
	u_char irr;

	//DPRINTK( "irq=%d, desc=%p, regs=%p\n", irq, desc, regs );

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
		//irr = PT_IRQSR & (PT_IRR_LAN | PT_IRR_SA1111);
		irr = PT_IRQSR & (PT_IRR_SA1111);

		/* SMC IRQ is low-active, so "switch" bit over */
		//irr ^= (PT_IRR_LAN);

		//DPRINTK( "irr=0x%02x\n", irr );

		if ((irr & (PT_IRR_LAN | PT_IRR_SA1111)) == 0)
			break;

		/*
		 * Since there is no individual mask, we have to
		 * mask the parent IRQ.  This is safe, since we'll
		 * recheck the register for any pending IRQs.
		 */
		if (irr & (PT_IRR_LAN)) {
			desc->chip->mask(irq);

			if (irr & PT_IRR_LAN) {
				//DPRINTK( "SMC9196, irq=%d\n", IRQ_SYSTEM3_SMC9196 );
				d = irq_desc + IRQ_SYSTEM3_SMC9196;
				d->handle(IRQ_SYSTEM3_SMC9196, d, regs);
			}

#if 0 /* no SSP yet on system 4 */
			if (irr & IRR_USAR) {
				d = irq_desc + IRQ_NEPONSET_USAR;
				d->handle(IRQ_NEPONSET_USAR, d, regs);
			}
#endif

			desc->chip->unmask(irq);
		}

		if (irr & PT_IRR_SA1111) {
			//DPRINTK( "SA1111, irq=%d\n", IRQ_SYSTEM3_SA1111 );
			d = irq_desc + IRQ_SYSTEM3_SA1111;
			d->handle(IRQ_SYSTEM3_SA1111, d, regs);
		}
	}
}

static void __init system3_init_irq(void)
{
	/*
	 * Install handler for GPIO25.
	 */
	set_irq_type(IRQ_GPIO25, IRQT_RISING);
	set_irq_chained_handler(IRQ_GPIO25, system3_irq_handler);

	/*
	 * install eth irq
	 */
	set_irq_handler(IRQ_SYSTEM3_SMC9196, do_simple_IRQ);
	set_irq_flags(IRQ_SYSTEM3_SMC9196, IRQF_VALID | IRQF_PROBE);
}

/**********************************************************************
 * On system 3 limit cpu frequency to 206 Mhz
 */
static int sdram_notifier(struct notifier_block *nb, unsigned long event,
		void *data)
{
	struct cpufreq_policy *policy = data;
	switch (event) {
		case CPUFREQ_ADJUST:
		case CPUFREQ_INCOMPATIBLE:
			cpufreq_verify_within_limits(policy, 147500, 206000);
			break;
		case CPUFREQ_NOTIFY:
			if ((policy->min < 147500) || 
			    (policy->max > 206000))
				panic("cpufreq failed to limit the speed\n");
			break;
	}
	return 0;
}

/**
 *	system3_uart_pm - powermgmt callback function for system 3 UART
 *	@port: uart port structure
 *	@state: pm state
 *	@oldstate: old pm state
 *
 */
static void system3_uart_pm(struct uart_port *port, u_int state, u_int oldstate)
{
	/* TODO: switch on/off uart in powersave mode */
}

/*
 * Note! this can be called from IRQ context.
 * FIXME: Handle PT Digital Board CTRL regs irq-safe.
 *
 * NB: system3 uses COM_RTS and COM_DTR for both UART1 (com port)
 * and UART3 (radio module).  We only handle them for UART1 here.
 */
static void system3_set_mctrl(struct uart_port *port, u_int mctrl)
{
	if (port->mapbase == _Ser1UTCR0) {
		u_int set = 0, clear = 0;

		if (mctrl & TIOCM_RTS)
			set |= PT_CTRL2_RS1_RTS;
		else
			clear |= PT_CTRL2_RS1_RTS;

		if (mctrl & TIOCM_DTR)
			set |= PT_CTRL2_RS1_DTR;
		else
			clear |= PT_CTRL2_RS1_DTR;

		PTCTRL2_clear(clear);
		PTCTRL2_set(set);
	}
}

static u_int system3_get_mctrl(struct uart_port *port)
{
	u_int ret = 0;
	u_int irqsr = PT_IRQSR;

	/* need 2 reads to read current value */
	irqsr = PT_IRQSR;

	/* TODO: check IRQ source register for modem/com
	 status lines and set them correctly. */

	ret = TIOCM_CD | TIOCM_CTS | TIOCM_DSR;

	return ret;
}

/**
 *	system3_lcd_backlight_on - switch system 3 lcd backlight on
 *
 */
int system3_lcd_backlight_on( void )
{
	PTCTRL0_set( PT_CTRL0_LCD_BL );
	return 0;
}

/**
 *	system3_lcd_backlight_off - switch system 3 lcd backlight off
 *
 */
static void system3_lcd_backlight_off(void)
{
	PTCTRL0_clear( PT_CTRL0_LCD_BL );
}

/**
 *	system3_lcd_on - switch system 3 lcd on
 *
 */
static void system3_lcd_on(void)
{
	DPRINTK( "%s\n", "START" );
	PTCTRL0_set( PT_CTRL0_LCD_EN );

	/* brightness / contrast */
	sa1111_enable_device(SKPCR_PWMCLKEN);
	PB_DDR = 0xFFFFFFFF;
	SKPEN0 = 1;
	SKPEN1 = 1;
}

/**
 *	system3_lcd_off - switch system 3 lcd off
 *
 */
static void system3_lcd_off(void)
{
	DPRINTK( "%s\n", "START" );
	PTCTRL0_clear( PT_CTRL0_LCD_EN );
	SKPEN0 = 0;
	SKPEN1 = 0;
	sa1111_disable_device(SKPCR_PWMCLKEN);
}

/**
 *	system3_lcd_contrast - set system 3 contrast
 *	@value:		the new contrast
 *
 */
static void system3_lcd_contrast(unsigned char value)
{
	DPRINTK( "value=0x%02x\n", value );
	SYS3LCDCONTR = value;
}

/**
 *	system3_lcd_brightness - set system 3 brightness
 *	@value:		the new brightness
 *
 */
static void system3_lcd_brightness(unsigned char value)
{
	DPRINTK( "value=0x%02x\n", value );
	SYS3LCDBRIGHT = value;
}

static void system3_lcd_power(int on)
{
	if (on) {
		system3_lcd_on();
	} else {
		system3_lcd_off();
	}
}

static void system3_backlight_power(int on)
{
	if (on) {
		system3_lcd_backlight_on();
		system3_lcd_contrast(0x95);
		system3_lcd_brightness(240);
	} else {
		system3_lcd_backlight_off();
	}
}

static struct resource sa1111_resources[] = {
	[0] = {
		.start		= PT_SA1111_BASE,
		.end		= PT_SA1111_BASE + 0x00001fff,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IRQ_SYSTEM3_SA1111,
		.end		= IRQ_SYSTEM3_SA1111,
		.flags		= IORESOURCE_IRQ,
	},
};

static u64 sa1111_dmamask = 0xffffffffUL;

static struct platform_device sa1111_device = {
	.name		= "sa1111",
	.id		= 0,
	.dev		= {
		.dma_mask = &sa1111_dmamask,
	},
	.num_resources	= ARRAY_SIZE(sa1111_resources),
	.resource	= sa1111_resources,
};

static struct platform_device *devices[] __initdata = {
	&sa1111_device,
};

static int __init system3_init(void)
{
	int ret = 0;
	DPRINTK( "%s\n", "START" );

	if ( !machine_is_pt_system3() ) {
		ret = -EINVAL;
		goto DONE;
	}

	sa1100fb_lcd_power = system3_lcd_power;
	sa1100fb_backlight_power = system3_backlight_power;

	/* init control register */
	PT_CTRL0 = PT_CTRL0_INIT;
	PT_CTRL1 = 0x02;
	PT_CTRL2 = 0x00;
	DPRINTK( "CTRL[0]=0x%02x\n", PT_CTRL0 );
	DPRINTK( "CTRL[1]=0x%02x\n", PT_CTRL1 );
	DPRINTK( "CTRL[2]=0x%02x\n", PT_CTRL2 );

	/*
	 * Ensure that the memory bus request/grant signals are setup,
	 * and the grant is held in its inactive state.
	 */
	sa1110_mb_disable();

	system3_init_irq();

	/*
	 * Probe for a SA1111.
	 */
	ret = platform_add_devices(devices, ARRAY_SIZE(devices));
	if (ret < 0) {
		printk( KERN_WARNING"PT Digital Board: no SA1111 found!\n" );
		goto DONE;
	}

#ifdef CONFIG_CPU_FREQ
	ret = cpufreq_register_notifier(&system3_clkchg_block);
	if ( ret != 0 ) {
		printk( KERN_WARNING"PT Digital Board: could not register clock scale callback\n" );
		goto DONE;
	}
#endif


	ret = 0;
DONE:
	DPRINTK( "ret=%d\n", ret );
	return ret;
}

/**********************************************************************
 *  Exported Functions
 */

/**********************************************************************
 *  kernel magic macros
 */
arch_initcall(system3_init);

MACHINE_START(PT_SYSTEM3, "PT System 3")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	BOOT_PARAMS(0xc0000100)
	MAPIO(system3_map_io)
	INITIRQ(sa1100_init_irq)
MACHINE_END
