/*
 *  drivers/char/serial_tx3912.h
 *
 *  Copyright (C) 1999 Harald Koerfgen
 *  Copyright (C) 2000 Jim Pick <jim@jimpick.com>
 *  Copyright (C) 2001 Steven J. Hill (sjhill@realitydiluted.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Serial driver for TMPR3912/05 and PR31700 processors
 */
#include <linux/serialP.h>
#include <linux/generic_serial.h>

/* UART Interrupt (Interrupt 2) bits (UARTA,UARTB) */
#define UART_RX_INT         9  /* receiver holding register full  (31, 21) */
#define UART_RXOVERRUN_INT  8  /* receiver overrun error          (30, 20) */
#define UART_FRAMEERR_INT   7  /* receiver frame error            (29, 19) */
#define UART_BREAK_INT      6  /* received break signal           (28, 18) */
#define UART_PARITYERR_INT  5  /* receiver parity error           (27, 17) */
#define UART_TX_INT         4  /* transmit holding register empty (26, 16) */
#define UART_TXOVERRUN_INT  3  /* transmit overrun error          (25, 15) */
#define UART_EMPTY_INT      2  /* both trans/recv regs empty      (24, 14) */
#define UART_DMAFULL_INT    1  /* DMA at end of buffer            (23, 13) */
#define UART_DMAHALF_INT    0  /* DMA halfway through buffer      (22, 12) */

#define UARTA_SHIFT        22
#define UARTB_SHIFT        12

#define INTTYPE(interrupttype)            (1 << interrupttype)

/* 
 * This driver can spew a whole lot of debugging output at you. If you
 * need maximum performance, you should disable the DEBUG define.
 */
#undef TX3912_UART_DEBUG

#ifdef TX3912_UART_DEBUG
#define TX3912_UART_DEBUG_OPEN		0x00000001
#define TX3912_UART_DEBUG_SETTING	0x00000002
#define TX3912_UART_DEBUG_FLOW		0x00000004
#define TX3912_UART_DEBUG_MODEMSIGNALS	0x00000008
#define TX3912_UART_DEBUG_TERMIOS	0x00000010
#define TX3912_UART_DEBUG_TRANSMIT	0x00000020
#define TX3912_UART_DEBUG_RECEIVE	0x00000040
#define TX3912_UART_DEBUG_INTERRUPTS	0x00000080
#define TX3912_UART_DEBUG_PROBE		0x00000100
#define TX3912_UART_DEBUG_INIT		0x00000200
#define TX3912_UART_DEBUG_CLEANUP	0x00000400
#define TX3912_UART_DEBUG_CLOSE		0x00000800
#define TX3912_UART_DEBUG_FIRMWARE	0x00001000
#define TX3912_UART_DEBUG_MEMTEST	0x00002000
#define TX3912_UART_DEBUG_THROTTLE	0x00004000
#define TX3912_UART_DEBUG_ALL		0xffffffff

int rs_debug = TX3912_UART_DEBUG_ALL & ~TX3912_UART_DEBUG_TRANSMIT;

#define rs_dprintk(f, str...) if (rs_debug & f) printk (str)
#define func_enter() rs_dprintk (TX3912_UART_DEBUG_FLOW,	\
				"rs: enter " __FUNCTION__ "\n")
#define func_exit() rs_dprintk (TX3912_UART_DEBUG_FLOW,	\
				"rs: exit " __FUNCTION__ "\n")

#else
#define rs_dprintk(f, str...)
#define func_enter()
#define func_exit()

#endif	/* TX3912_UART_DEBUG */

/*
 * Number of serial ports
 */
#define TX3912_UART_NPORTS  2

/*
 * Hardware specific serial port structure
 */
struct rs_port { 	
	struct gs_port		gs;		/* Must be first field! */

	unsigned long		base;
	int			intshift;	/* Register shift */
	struct wait_queue	*shutdown_wait; 
	int			stat_flags;
        struct async_icount	icount;		/* Counters for 4 input IRQs */
	int			read_status_mask;
	int			ignore_status_mask;
	int			x_char;		/* XON/XOFF character */
}; 
