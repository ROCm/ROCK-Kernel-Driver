/*
 * linux/arch/arm/mach-sa1100/h3600.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/sched.h>

#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>
#include <linux/serial_core.h>

#include "generic.h"


/*
 * Bitsy has extended, write-only memory-mapped GPIO's
 */

static int h3600_egpio = EGPIO_H3600_RS232_ON;

void init_h3600_egpio(void)
{
#ifdef CONFIG_IPAQ_H3100
	int h3100_controls = (GPIO_H3100_BT_ON
			      | GPIO_H3100_QMUTE
			      | GPIO_H3100_LCD_3V_ON 
			      | GPIO_H3100_AUD_ON
			      | GPIO_H3100_AUD_PWR_ON
			      | GPIO_H3100_IR_ON
			      | GPIO_H3100_IR_FSEL);
	GPDR |= h3100_controls;
	GPCR = h3100_controls;
	GAFR = GPIO_SSP_CLK;
#endif
}

void clr_h3600_egpio(unsigned long x)
{
#ifdef CONFIG_IPAQ_H3100
	unsigned long gpcr = 0;
	if (x&EGPIO_H3600_QMUTE)
		gpcr |= GPIO_H3100_QMUTE;
	if (x&EGPIO_H3600_LCD_ON)
		gpcr |= GPIO_H3100_LCD_3V_ON;
	if (x&EGPIO_H3600_AUD_AMP_ON)
		gpcr |= GPIO_H3100_AUD_ON;
	if (x&EGPIO_H3600_AUD_PWR_ON)
		gpcr |= GPIO_H3100_AUD_PWR_ON;
	if (x&EGPIO_H3600_IR_ON)
		gpcr |= GPIO_H3100_IR_ON;
	if (x&EGPIO_H3600_IR_FSEL)
		gpcr |= GPIO_H3100_IR_FSEL;
	GPCR = gpcr;
#endif
	h3600_egpio &= ~x;
	H3600_EGPIO = h3600_egpio;
}

void set_h3600_egpio(unsigned long x)
{
#ifdef CONFIG_IPAQ_H3100
	unsigned long gpsr = 0;
	if (x&EGPIO_H3600_QMUTE)
		gpsr |= GPIO_H3100_QMUTE;
	if (x&EGPIO_H3600_LCD_ON)
		gpsr |= GPIO_H3100_LCD_3V_ON;
	if (x&EGPIO_H3600_AUD_AMP_ON)
		gpsr |= GPIO_H3100_AUD_ON;
	if (x&EGPIO_H3600_AUD_PWR_ON)
		gpsr |= GPIO_H3100_AUD_PWR_ON;
	if (x&EGPIO_H3600_IR_ON)
		gpsr |= GPIO_H3100_IR_ON;
	if (x&EGPIO_H3600_IR_FSEL)
		gpsr |= GPIO_H3100_IR_FSEL;
	GPSR = gpsr;
#endif
	h3600_egpio |= x;
	H3600_EGPIO = h3600_egpio;
}

EXPORT_SYMBOL(clr_h3600_egpio);
EXPORT_SYMBOL(set_h3600_egpio);


/*
 * Low-level UART features.
 *
 * Note that RTS, CTS and DCD are all active low.
 */

static void h3600_uart_set_mctrl(struct uart_port *port, u_int mctrl)
{
	if (port->mapbase == _Ser3UTCR0) {
		if (mctrl & TIOCM_RTS)
			GPCR = GPIO_H3600_COM_RTS;
		else
			GPSR = GPIO_H3600_COM_RTS;
	}
}

static int h3600_uart_get_mctrl(struct uart_port *port)
{
	int ret = TIOCM_CD | TIOCM_CTS | TIOCM_DSR;

	if (port->mapbase == _Ser3UTCR0) {
		int gplr = GPLR;
		if (gplr & GPIO_H3600_COM_DCD)
			ret &= ~TIOCM_CD;
		if (gplr & GPIO_H3600_COM_CTS)
			ret &= ~TIOCM_CTS;
	}

	return ret;
}

static void h3600_dcd_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_info *info = dev_id;
	/* Note: should only call this if something has changed */
	uart_handle_dcd_change(info, !(GPLR & GPIO_H3600_COM_DCD));
}

static void h3600_cts_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_info *info = dev_id;
	/* Note: should only call this if something has changed */
	uart_handle_cts_change(info, !(GPLR & GPIO_H3600_COM_CTS));
}

static void h3600_uart_pm(struct uart_port *port, u_int state, u_int oldstate)
{
	if (port->mapbase == _Ser2UTCR0) {
		if (state == 0) {
			set_h3600_egpio(EGPIO_H3600_IR_ON);
		} else {
			clr_h3600_egpio(EGPIO_H3600_IR_ON);
		}
	} else if (port->mapbase == _Ser3UTCR0) {
		if (state == 0) {
			set_h3600_egpio(EGPIO_H3600_RS232_ON);
		} else {
			clr_h3600_egpio(EGPIO_H3600_RS232_ON);
		}
	}
}

static int h3600_uart_open(struct uart_port *port, struct uart_info *info)
{
	int ret = 0;

	if (port->mapbase == _Ser2UTCR0) {
		Ser2UTCR4 = UTCR4_HSE;
		Ser2HSCR0 = 0;
		Ser2HSSR0 = HSSR0_EIF | HSSR0_TUR |
			    HSSR0_RAB | HSSR0_FRE;
	} else if (port->mapbase == _Ser3UTCR0) {
		GPDR &= ~(GPIO_H3600_COM_DCD|GPIO_H3600_COM_CTS);
		GPDR |= GPIO_H3600_COM_RTS;
		set_GPIO_IRQ_edge(GPIO_H3600_COM_DCD|GPIO_H3600_COM_CTS,
				  GPIO_BOTH_EDGES);

		ret = request_irq(IRQ_GPIO_H3600_COM_DCD, h3600_dcd_intr,
				  0, "RS232 DCD", info);
		if (ret)
			return ret;

		ret = request_irq(IRQ_GPIO_H3600_COM_CTS, h3600_cts_intr,
				  0, "RS232 CTS", info);
		if (ret)
			free_irq(IRQ_GPIO_H3600_COM_DCD, info);
	}
	return ret;
}

static void h3600_uart_close(struct uart_port *port, struct uart_info *info)
{
	if (port->mapbase == _Ser3UTCR0) {
		free_irq(IRQ_GPIO_H3600_COM_DCD, info);
		free_irq(IRQ_GPIO_H3600_COM_CTS, info);
	}
}

static struct sa1100_port_fns h3600_port_fns __initdata = {
	set_mctrl:	h3600_uart_set_mctrl,
	get_mctrl:	h3600_uart_get_mctrl,
	pm:		h3600_uart_pm,
	open:		h3600_uart_open,
	close:		h3600_uart_close,
};

static struct map_desc h3600_io_desc[] __initdata = {
 /* virtual     physical    length      domain     r  w  c  b */
  { 0xe8000000, 0x00000000, 0x02000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 0 */
  { 0xf0000000, 0x49000000, 0x00100000, DOMAIN_IO, 0, 1, 0, 0 }, /* EGPIO 0 */
  { 0xf1000000, 0x10000000, 0x02800000, DOMAIN_IO, 1, 1, 0, 0 }, /* static memory bank 2 */
  { 0xf3800000, 0x40000000, 0x00800000, DOMAIN_IO, 1, 1, 0, 0 }, /* static memory bank 4 */
  LAST_DESC
};

static void __init h3600_map_io(void)
{
	sa1100_map_io();
	iotable_init(h3600_io_desc);

	sa1100_register_uart_fns(&h3600_port_fns);
	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1); /* isn't this one driven elsewhere? */
	init_h3600_egpio();

	/*
	 * Default GPIO settings.
	 */
	GPCR = 0x0fffffff;
	GPDR = 0x0401f3fc;

	/*
	 * Ensure those pins are outputs and driving low.
	 */
	PPDR |= PPC_TXD4 | PPC_SCLK | PPC_SFRM;
	PPSR &= ~(PPC_TXD4 | PPC_SCLK | PPC_SFRM);

	/* Configure suspend conditions */
	PGSR = 0;
	PWER = 0x1 | (1 << 31);
	PCFR = PCFR_OPDE | PCFR_FP | PCFR_FS;
}

MACHINE_START(H3600, "Compaq iPAQ")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	BOOT_PARAMS(0xc0000100)
	MAPIO(h3600_map_io)
	INITIRQ(sa1100_init_irq)
MACHINE_END
