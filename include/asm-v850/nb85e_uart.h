/*
 * include/asm-v850/nb85e_uart.h -- On-chip UART often used with the
 *	NB85E cpu core
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

/* There's not actually a single UART implementation used by nb85e
   derivatives, but rather a series of implementations that are all
   `close' to one another.  This file attempts to capture some
   commonality between them.  */

#ifndef __V850_NB85E_UART_H__
#define __V850_NB85E_UART_H__

#include <asm/types.h>
#include <asm/machdep.h>	/* Pick up chip-specific defs.  */


/* The base address of the UART control registers for channel N.
   The default is the address used on the V850E/MA1.  */
#ifndef NB85E_UART_BASE_ADDR
#define NB85E_UART_BASE_ADDR(n)		(0xFFFFFA00 + 0x10 * (n))
#endif 

/* Addresses of specific UART control registers for channel N.
   The defaults are the addresses used on the V850E/MA1; if a platform
   wants to redefine any of these, it must redefine them all.  */
#ifndef NB85E_UART_ASIM_ADDR
#define NB85E_UART_ASIM_ADDR(n)		(NB85E_UART_BASE_ADDR(n) + 0x0)
#define NB85E_UART_RXB_ADDR(n)		(NB85E_UART_BASE_ADDR(n) + 0x2)
#define NB85E_UART_ASIS_ADDR(n)		(NB85E_UART_BASE_ADDR(n) + 0x3)
#define NB85E_UART_TXB_ADDR(n)		(NB85E_UART_BASE_ADDR(n) + 0x4)
#define NB85E_UART_ASIF_ADDR(n)		(NB85E_UART_BASE_ADDR(n) + 0x5)
#define NB85E_UART_CKSR_ADDR(n)		(NB85E_UART_BASE_ADDR(n) + 0x6)
#define NB85E_UART_BRGC_ADDR(n)		(NB85E_UART_BASE_ADDR(n) + 0x7)
#endif

#ifndef NB85E_UART_CKSR_MAX_FREQ
#define NB85E_UART_CKSR_MAX_FREQ (25*1000*1000)
#endif

/* UART config registers.  */
#define NB85E_UART_ASIM(n)	(*(volatile u8 *)NB85E_UART_ASIM_ADDR(n))
/* Control bits for config registers.  */
#define NB85E_UART_ASIM_CAE	0x80 /* clock enable */
#define NB85E_UART_ASIM_TXE	0x40 /* transmit enable */
#define NB85E_UART_ASIM_RXE	0x20 /* receive enable */
#define NB85E_UART_ASIM_PS_MASK	0x18 /* mask covering parity-select bits */
#define NB85E_UART_ASIM_PS_NONE	0x00 /* no parity */
#define NB85E_UART_ASIM_PS_ZERO	0x08 /* zero parity */
#define NB85E_UART_ASIM_PS_ODD	0x10 /* odd parity */
#define NB85E_UART_ASIM_PS_EVEN	0x18 /* even parity */
#define NB85E_UART_ASIM_CL_8	0x04 /* char len is 8 bits (otherwise, 7) */
#define NB85E_UART_ASIM_SL_2	0x02 /* 2 stop bits (otherwise, 1) */
#define NB85E_UART_ASIM_ISRM	0x01 /* generate INTSR interrupt on errors
					(otherwise, generate INTSER) */

/* UART serial interface status registers.  */
#define NB85E_UART_ASIS(n)	(*(volatile u8 *)NB85E_UART_ASIS_ADDR(n))
/* Control bits for status registers.  */
#define NB85E_UART_ASIS_PE	0x04 /* parity error */
#define NB85E_UART_ASIS_FE	0x02 /* framing error */
#define NB85E_UART_ASIS_OVE	0x01 /* overrun error */

/* UART serial interface transmission status registers.  */
#define NB85E_UART_ASIF(n)	(*(volatile u8 *)NB85E_UART_ASIF_ADDR(n))
#define NB85E_UART_ASIF_TXBF	0x02 /* transmit buffer flag (data in TXB) */
#define NB85E_UART_ASIF_TXSF	0x01 /* transmit shift flag (sending data) */

/* UART receive buffer register.  */
#define NB85E_UART_RXB(n)	(*(volatile u8 *)NB85E_UART_RXB_ADDR(n))

/* UART transmit buffer register.  */
#define NB85E_UART_TXB(n)	(*(volatile u8 *)NB85E_UART_TXB_ADDR(n))

/* UART baud-rate generator control registers.  */
#define NB85E_UART_CKSR(n)	(*(volatile u8 *)NB85E_UART_CKSR_ADDR(n))
#define NB85E_UART_CKSR_MAX	11
#define NB85E_UART_BRGC(n)	(*(volatile u8 *)NB85E_UART_BRGC_ADDR(n))


/* This UART doesn't implement RTS/CTS by default, but some platforms
   implement them externally, so check to see if <asm/machdep.h> defined
   anything.  */
#ifdef NB85E_UART_CTS
#define nb85e_uart_cts(n)	NB85E_UART_CTS(n)
#else
#define nb85e_uart_cts(n)	(1)
#endif

/* Do the same for RTS.  */
#ifdef NB85E_UART_SET_RTS
#define nb85e_uart_set_rts(n,v)	NB85E_UART_SET_RTS(n,v)
#else
#define nb85e_uart_set_rts(n,v)	((void)0)
#endif

/* Return true if all characters awaiting transmission on uart channel N
   have been transmitted.  */
#define nb85e_uart_xmit_done(n)						      \
   (! (NB85E_UART_ASIF(n) & NB85E_UART_ASIF_TXBF))
/* Wait for this to be true.  */
#define nb85e_uart_wait_for_xmit_done(n)				      \
   do { } while (! nb85e_uart_xmit_done (n))

/* Return true if uart channel N is ready to transmit a character.  */
#define nb85e_uart_xmit_ok(n)						      \
   (nb85e_uart_xmit_done(n) && nb85e_uart_cts(n))
/* Wait for this to be true.  */
#define nb85e_uart_wait_for_xmit_ok(n)					      \
   do { } while (! nb85e_uart_xmit_ok (n))

/* Write character CH to uart channel N.  */
#define nb85e_uart_putc(n, ch)	(NB85E_UART_TXB(n) = (ch))


#define NB85E_UART_MINOR_BASE	64


#ifndef __ASSEMBLY__

/* Setup a console using channel 0 of the builtin uart.  */
extern void nb85e_uart_cons_init (unsigned chan);

/* Configure and turn on uart channel CHAN, using the termios `control
   modes' bits in CFLAGS, and a baud-rate of BAUD.  */
void nb85e_uart_configure (unsigned chan, unsigned cflags, unsigned baud);

/* If the macro NB85E_UART_PRE_CONFIGURE is defined (presumably by a
   <asm/machdep.h>), it is called from nb85e_uart_pre_configure before
   anything else is done, with interrupts disabled.  */

#endif /* !__ASSEMBLY__ */


#endif /* __V850_NB85E_UART_H__ */
