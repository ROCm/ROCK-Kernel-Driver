/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 * Putting things on the screen/serial line using YAMONs facilities.
 *
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/serialP.h>
#include <linux/serial_reg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/serial.h>


#ifdef CONFIG_MIPS_ATLAS 
/* 
 * Atlas registers are memory mapped on 64-bit aligned boundaries and 
 * only word access are allowed.
 * When reading the UART 8 bit registers only the LSB are valid.
 */
unsigned int atlas_serial_in(struct async_struct *info, int offset)
{
	return (*(volatile unsigned int *)(info->port + mips_io_port_base + offset*8) & 0xff);
}

void atlas_serial_out(struct async_struct *info, int offset, int value)
{
	*(volatile unsigned int *)(info->port + mips_io_port_base + offset*8) = value;
}

#define serial_in  atlas_serial_in
#define serial_out atlas_serial_out

#else

static unsigned int serial_in(struct async_struct *info, int offset)
{
	return inb(info->port + offset);
}

static void serial_out(struct async_struct *info, int offset,
				int value)
{
	outb(value, info->port + offset);
}
#endif

static struct serial_state rs_table[] = {
	SERIAL_PORT_DFNS	/* Defined in serial.h */
};

/*
 * Hooks to fake "prom" console I/O before devices 
 * are fully initialized. 
 */
static struct async_struct prom_port_info = {0};

void __init setup_prom_printf(int tty_no) {
	struct serial_state *ser = &rs_table[tty_no];

	prom_port_info.state = ser;
	prom_port_info.magic = SERIAL_MAGIC;
	prom_port_info.port = ser->port;
	prom_port_info.flags = ser->flags;

	/* No setup of UART - assume YAMON left in sane state */
}

int putPromChar(char c)
{
        if (!prom_port_info.state) { 	/* need to init device first */
		return 0;
	}

	while ((serial_in(&prom_port_info, UART_LSR) & UART_LSR_THRE) == 0)
		;

	serial_out(&prom_port_info, UART_TX, c);

	return 1;
}

char getPromChar(void)
{
	if (!prom_port_info.state) { 	/* need to init device first */
		return 0;
	}

	while (!(serial_in(&prom_port_info, UART_LSR) & 1))
		;

	return(serial_in(&prom_port_info, UART_RX));
}

static char buf[1024];

void __init prom_printf(char *fmt, ...)
{
	va_list args;
	int l;
	char *p, *buf_end;
	long flags;

	int putPromChar(char);

	/* Low level, brute force, not SMP safe... */
	save_and_cli(flags);
	va_start(args, fmt);
	l = vsprintf(buf, fmt, args); /* hopefully i < sizeof(buf) */
	va_end(args);

	buf_end = buf + l;

	for (p = buf; p < buf_end; p++) {
		/* Crude cr/nl handling is better than none */
		if(*p == '\n')putPromChar('\r');
		putPromChar(*p);
	}
	restore_flags(flags);
}
