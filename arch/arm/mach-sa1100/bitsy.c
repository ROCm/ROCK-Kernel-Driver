/*
 * linux/arch/arm/mach-sa1100/bitsy.c
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

static int bitsy_egpio = EGPIO_BITSY_RS232_ON;

void clr_bitsy_egpio(unsigned long x)
{
	bitsy_egpio &= ~x;
	BITSY_EGPIO = bitsy_egpio;
}

void set_bitsy_egpio(unsigned long x)
{
	bitsy_egpio |= x;
	BITSY_EGPIO = bitsy_egpio;
}

EXPORT_SYMBOL(clr_bitsy_egpio);
EXPORT_SYMBOL(set_bitsy_egpio);


/*
 * low-level UART features
 */

static void bitsy_uart_set_mctrl(struct uart_port *port, u_int mctrl)
{
	if (port->mapbase == _Ser3UTCR0) {
		if (mctrl & TIOCM_RTS)
			GPCR = GPIO_BITSY_COM_RTS;
		else
			GPSR = GPIO_BITSY_COM_RTS;
	}
}

static int bitsy_uart_get_mctrl(struct uart_port *port)
{
	int ret = TIOCM_CD | TIOCM_CTS | TIOCM_DSR;

	if (port->mapbase == _Ser3UTCR0) {
		int gplr = GPLR;
		if (gplr & GPIO_BITSY_COM_DCD)
			ret &= ~TIOCM_CD;
		if (gplr & GPIO_BITSY_COM_CTS)
			ret &= ~TIOCM_CTS;
	}

	return ret;
}

static void bitsy_dcd_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_info *info = dev_id;
	/* Note: should only call this if something has changed */
	uart_handle_dcd_change(info, GPLR & GPIO_BITSY_COM_DCD);
}

static void bitsy_cts_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_info *info = dev_id;
	/* Note: should only call this if something has changed */
	uart_handle_cts_change(info, GPLR & GPIO_BITSY_COM_CTS);
}

static void bitsy_uart_pm(struct uart_port *port, u_int state, u_int oldstate)
{
	if (port->mapbase == _Ser2UTCR0) {
		if (state == 0) {
			set_bitsy_egpio(EGPIO_BITSY_IR_ON);
		} else {
			clr_bitsy_egpio(EGPIO_BITSY_IR_ON);
		}
	} else if (port->mapbase == _Ser3UTCR0) {
		if (state == 0) {
			set_bitsy_egpio(EGPIO_BITSY_RS232_ON);
		} else {
			clr_bitsy_egpio(EGPIO_BITSY_RS232_ON);
		}
	}
}

static int bitsy_uart_open(struct uart_port *port, struct uart_info *info)
{
	int ret = 0;

	if (port->mapbase == _Ser2UTCR0) {
		Ser2UTCR4 = UTCR4_HSE;
		Ser2HSCR0 = 0;
		Ser2HSSR0 = HSSR0_EIF | HSSR0_TUR |
			    HSSR0_RAB | HSSR0_FRE;
	} else if (port->mapbase == _Ser3UTCR0) {
		GPDR &= ~(GPIO_BITSY_COM_DCD|GPIO_BITSY_COM_CTS);
		GPDR |= GPIO_BITSY_COM_RTS;
		set_GPIO_IRQ_edge(GPIO_BITSY_COM_DCD|GPIO_BITSY_COM_CTS,
				  GPIO_BOTH_EDGES);

		ret = request_irq(IRQ_GPIO_BITSY_COM_DCD, bitsy_dcd_intr,
				  0, "RS232 DCD", info);
		if (ret)
			return ret;

		ret = request_irq(IRQ_GPIO_BITSY_COM_CTS, bitsy_cts_intr,
				  0, "RS232 CTS", info);
		if (ret)
			free_irq(IRQ_GPIO_BITSY_COM_DCD, info);
	}
	return ret;
}

static void bitsy_uart_close(struct uart_port *port, struct uart_info *info)
{
	if (port->mapbase == _Ser3UTCR0) {
		free_irq(IRQ_GPIO_BITSY_COM_DCD, info);
		free_irq(IRQ_GPIO_BITSY_COM_CTS, info);
	}
}

static struct sa1100_port_fns bitsy_port_fns __initdata = {
	set_mctrl:	bitsy_uart_set_mctrl,
	get_mctrl:	bitsy_uart_get_mctrl,
	pm:		bitsy_uart_pm,
	open:		bitsy_uart_open,
	close:		bitsy_uart_close,
};

static struct map_desc bitsy_io_desc[] __initdata = {
 /* virtual     physical    length      domain     r  w  c  b */
  { 0xe8000000, 0x00000000, 0x02000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 0 */
  { 0xf0000000, 0x49000000, 0x00100000, DOMAIN_IO, 0, 1, 0, 0 }, /* EGPIO 0 */
  { 0xf1000000, 0x10000000, 0x02000000, DOMAIN_IO, 1, 1, 0, 0 }, /* static memory bank 2 */
  { 0xf3000000, 0x40000000, 0x02000000, DOMAIN_IO, 1, 1, 0, 0 }, /* static memory bank 4 */
  LAST_DESC
};

static void __init bitsy_map_io(void)
{
	sa1100_map_io();
	iotable_init(bitsy_io_desc);

	sa1100_register_uart_fns(&bitsy_port_fns);
	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1); /* isn't this one driven elsewhere? */
	sa1100_register_uart(2, 2);
}

MACHINE_START(BITSY, "Compaq iPAQ")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	BOOT_PARAMS(0xc0000100)
	MAPIO(bitsy_map_io)
	INITIRQ(sa1100_init_irq)
MACHINE_END
