/*
 * BK Id: SCCS/s.ns16550.c 1.7 05/18/01 06:20:29 patch
 */
/*
 * COM1 NS16550 support
 */

#include <linux/config.h>
#include <linux/serialP.h>
#include <linux/serial_reg.h>
#include <asm/serial.h>

/* Some machines, such as ones with a PReP memory map, initally have
 * their serial port at an offset of 0x80000000 from where they are
 * in <asm/serial.h>.  This tries to take that into account. */
#ifndef IOOFFSET
#define IOOFFSET 0
#endif

static struct serial_state rs_table[RS_TABLE_SIZE] = {
	SERIAL_PORT_DFNS	/* Defined in <asm/serial.h> */
};

static int shift;

volatile unsigned long serial_init(int chan) {
	unsigned long com_port;

	/* Get the base, and add any offset we need to deal with. */
	com_port = rs_table[chan].port + IOOFFSET;

	/* How far apart the registers are. */
	shift = rs_table[chan].iomem_reg_shift;

	/* See if port is present */
	*((unsigned char *)com_port + (UART_LCR << shift)) = 0x00;
	*((unsigned char *)com_port + (UART_IER << shift)) = 0x00;
	/* Access baud rate */
	*((unsigned char *)com_port + (UART_LCR << shift)) = 0x00;
#ifdef CONFIG_SERIAL_CONSOLE_NONSTD
	/* Input clock. */
	*((unsigned char *)com_port + (UART_DLL << shift)) = 
			(BASE_BAUD / CONFIG_SERIAL_CONSOLE_BAUD);
	*((unsigned char *)com_port + (UART_DLM << shift)) = 
		(BASE_BAUD / CONFIG_SERIAL_CONSOLE_BAUD) >> 8;
#endif
	 /* 8 data, 1 stop, no parity */
	*((unsigned char *)com_port + (UART_LCR << shift)) = 0x03;
	/* RTS/DTR */
	*((unsigned char *)com_port + (UART_MCR << shift)) = 0x03;
	/* Clear & enable FIFOs */
	*((unsigned char *)com_port + (UART_FCR << shift)) = 0x07;

	return (com_port);
}

void
serial_putc(volatile unsigned long com_port, unsigned char c)
{
	while ((*((volatile unsigned char *)com_port + (UART_LSR << shift)) &
				UART_LSR_THRE) == 0)
		;
	*(volatile unsigned char *)com_port = c;
}

unsigned char
serial_getc(volatile unsigned long com_port)
{
	while ((*((volatile unsigned char *)com_port + (UART_LSR << shift))
			& UART_LSR_DR) == 0)
		;
	return (*(volatile unsigned char *)com_port);
}

int
serial_tstc(volatile unsigned long com_port)
{
	return ((*((volatile unsigned char *)com_port + (UART_LSR << shift))
				& UART_LSR_DR) != 0);
}
