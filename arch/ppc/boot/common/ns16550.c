/*
 * BK Id: SCCS/s.ns16550.c 1.9 07/30/01 17:19:40 trini
 */
/*
 * COM1 NS16550 support
 */

#include <linux/config.h>
#include <linux/serialP.h>
#include <linux/serial_reg.h>
#include <asm/serial.h>

extern void outb(int port, unsigned char val);
extern unsigned char inb(int port);
extern unsigned long ISA_io;

static struct serial_state rs_table[RS_TABLE_SIZE] = {
	SERIAL_PORT_DFNS	/* Defined in <asm/serial.h> */
};

static int shift;

unsigned long serial_init(int chan) {
	unsigned long com_port;

	/* We need to find out which type io we're expecting.  If it's
	 * 'SERIAL_IO_PORT', we get an offset from the isa_io_base.
	 * If it's 'SERIAL_IO_MEM', we can the exact location.  -- Tom */
	switch (rs_table[chan].io_type) {
		case SERIAL_IO_PORT:
			com_port = rs_table[chan].port;
			break;
		case SERIAL_IO_MEM:
			com_port = (unsigned long)rs_table[chan].iomem_base;
			break;
		default:
			/* We can't deal with it. */
			return -1;
	}

	/* How far apart the registers are. */
	shift = rs_table[chan].iomem_reg_shift;

	/* See if port is present */
	outb(com_port + (UART_LCR << shift), 0x00);
	outb(com_port + (UART_IER << shift), 0x00);
	/* Access baud rate */
	outb(com_port + (UART_LCR << shift), 0x80);
#ifdef CONFIG_SERIAL_CONSOLE_NONSTD
	/* Input clock. */
	outb(com_port + (UART_DLL << shift), 
			(BASE_BAUD / CONFIG_SERIAL_CONSOLE_BAUD));
	outb(com_port + (UART_DLM << shift), 
		(BASE_BAUD / CONFIG_SERIAL_CONSOLE_BAUD) >> 8);
#endif
	 /* 8 data, 1 stop, no parity */
	outb(com_port + (UART_LCR << shift), 0x03);
	/* RTS/DTR */
	outb(com_port + (UART_MCR << shift), 0x03);
	/* Clear & enable FIFOs */
	outb(com_port + (UART_FCR << shift), 0x07);

	return (com_port);
}

void
serial_putc(unsigned long com_port, unsigned char c)
{
	while ((inb(com_port + (UART_LSR << shift)) & UART_LSR_THRE) == 0)
		;
	outb(com_port, c);
}

unsigned char
serial_getc(unsigned long com_port)
{
	while ((inb(com_port + (UART_LSR << shift)) & UART_LSR_DR) == 0)
		;
	return inb(com_port);
}

int
serial_tstc(unsigned long com_port)
{
	return ((inb(com_port + (UART_LSR << shift)) & UART_LSR_DR) != 0);
}
