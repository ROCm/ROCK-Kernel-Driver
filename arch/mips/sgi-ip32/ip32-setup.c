/*
 * IP32 basic setup
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Harald Koerfgen
 * Copyright (C) 2002, 03 Ilya A. Volynets
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/console.h>

#include <asm/time.h>
#include <asm/mipsregs.h>
#include <asm/bootinfo.h>
#include <asm/mmu_context.h>
#include <asm/sgialib.h>
#include <asm/traps.h>
#include <asm/io.h>
#include <asm/ip32/mace.h>
#include <asm/ip32/ip32_ints.h>

#ifdef CONFIG_SGI_O2MACE_ETH
/*
 * This is taken care of in here 'cause they say using Arc later on is
 * problematic
 */
extern char o2meth_eaddr[8];
static inline unsigned char str2hexnum(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return 0; /* foo */
}

static inline void str2eaddr(unsigned char *ea, unsigned char *str)
{
	int i;

	for (i = 0; i < 6; i++) {
		unsigned char num;

		if(*str == ':')
			str++;
		num = str2hexnum(*str++) << 4;
		num |= (str2hexnum(*str++));
		ea[i] = num;
	}
}
#endif

extern void ip32_time_init(void);
extern void ip32_be_init(void);
extern void __init ip32_timer_setup (struct irqaction *irq);
extern void __init crime_init (void);

#ifdef CONFIG_SERIAL_8250
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
extern int __init early_serial_setup(struct uart_port *port);

#define STD_COM_FLAGS (ASYNC_SKIP_TEST)
#define BASE_BAUD (1843200 / 16)

#endif /* CONFIG_SERIAL_8250 */

static int __init ip32_setup(void)
{
	set_io_port_base((unsigned long) ioremap(MACEPCI_LOW_IO, 0x2000000));

	crime_init();

#ifdef CONFIG_SERIAL_8250
 	{
		static struct uart_port o2_serial[2];

		memset(o2_serial, 0, sizeof(o2_serial));
		o2_serial[0].type	= PORT_16550A;
		o2_serial[0].line	= 0;
		o2_serial[0].irq	= MACEISA_SERIAL1_IRQ;
		o2_serial[0].flags	= STD_COM_FLAGS | UPF_RESOURCES;
		o2_serial[0].uartclk	= BASE_BAUD * 16;
		o2_serial[0].iotype	= UPIO_MEM;
		o2_serial[0].membase	= (char *)&mace->isa.serial1;
		o2_serial[0].fifosize	= 14;
                /* How much to shift register offset by. Each UART register
		 * is replicated over 256 byte space */
		o2_serial[0].regshift	= 8;
		o2_serial[1].type	= PORT_16550A;
		o2_serial[1].line	= 1;
		o2_serial[1].irq	= MACEISA_SERIAL2_IRQ;
		o2_serial[1].flags	= STD_COM_FLAGS | UPF_RESOURCES;
		o2_serial[1].uartclk	= BASE_BAUD * 16;
		o2_serial[1].iotype	= UPIO_MEM;
		o2_serial[1].membase	= (char *)&mace->isa.serial2;
		o2_serial[1].fifosize	= 14;
		o2_serial[1].regshift	= 8;

		early_serial_setup(&o2_serial[0]);
		early_serial_setup(&o2_serial[1]);
	}
#endif
#ifdef CONFIG_SGI_O2MACE_ETH
	{
		char *mac = ArcGetEnvironmentVariable("eaddr");
		str2eaddr(o2meth_eaddr, mac);
	}
#endif

#if defined(CONFIG_SERIAL_CORE_CONSOLE)
	{
		char* con = ArcGetEnvironmentVariable("console");
		if (con && *con == 'd') {
			static char options[8];
			char *baud = ArcGetEnvironmentVariable("dbaud");
			if (baud)
				strcpy(options, baud);
			add_preferred_console("ttyS", *(con + 1) == '2' ? 1 : 0,
					      baud ? options : NULL);
		}
	}
#endif

	board_be_init = ip32_be_init;
	board_time_init = ip32_time_init;
	board_timer_setup = ip32_timer_setup;

	return 0;
}

early_initcall(ip32_setup);
